/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cva.h"
#include "crt/crt_memory.h"
#include "gfx/gfx_encoder.h"

#include "os/os_assert.h"
#include "os/os_malloc.h"
#include "gfx/gfx_image.h"
#include "gfx/gfx_surface.h"
#include "utl/utl_h264.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

#include <va/va.h>
#include <va/va_enc_h264.h>

#include "utl/utl_defines.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define ENCODER_PARAMETER_SET_CAPACITY (1024) ///< Includes space for custom scaling-list syntax.
#define PARAMETER_BUFFER_CAPACITY      (10)   ///< Maximum VA buffers submitted for one encoded picture.
#define RECONSTRUCTION_SURFACE_CAPACITY                                                                                \
    (UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES + 1) ///< DPB capacity plus the current reconstruction target.
#define INPUT_SURFACE_COUNT       (1)                 ///< Number of source surfaces attached to the encode context.
#define CONFIG_ATTRIBUTE_CAPACITY (3)                 ///< Render-target format, rate control, and packed headers.
#define RENDER_TARGET_CAPACITY                                                                                         \
    (RECONSTRUCTION_SURFACE_CAPACITY + INPUT_SURFACE_COUNT) ///< Total context render-target capacity.
#define QPPRIME_Y_ZERO_TRANSFORM_BYPASS (FALSE)             ///< Disable transform bypass when QPprimeY is zero.
#define GAPS_IN_FRAME_NUM_VALUE_ALLOWED (FALSE)             ///< Disallow gaps in SPS frame_num values.
#define MAX_LIST1_REFERENCES_SHIFT      (16) ///< Upper 16 bits of VAConfigAttribEncMaxRefFrames hold the list-1 limit.
#define DEBLOCKING_FILTER_CONTROL_PRESENT (TRUE) ///< Signal deblocking filter control fields in slice headers.
#define RATE_PERCENTAGE_SCALE             (100)  ///< Percent units used by VA bitrate control.
#define FRAME_RATE_DENOMINATOR_SHIFT      (16)   ///< Upper half of VA's packed frame-rate fraction.
#define CODED_SEGMENT_CAPACITY            (256)  ///< Maximum driver segments inspected for one access unit.
#define COMPLETION_POLL_TIMEOUT           (0)    ///< Query completion without waiting for hardware progress.
#define PROFILE_CANDIDATE_CAPACITY        (3)    ///< High, Main, then Constrained Baseline.
/** Native flags which make an encoded access unit unusable. */
#define CODED_SEGMENT_ERROR_MASK                                                                                       \
    (VA_CODED_BUF_STATUS_SLICE_OVERFLOW_MASK | VA_CODED_BUF_STATUS_BITRATE_OVERFLOW |                                  \
     VA_CODED_BUF_STATUS_FRAME_SIZE_OVERFLOW | VA_CODED_BUF_STATUS_BAD_BITSTREAM)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Complete native reference lists and the active prefix of each list for one submission.
 */
typedef struct ReferenceLists_s
{
    uint32_t      count0; ///< Active past references, bounded by the decoded-picture-buffer count.
    uint32_t      count1; ///< Active future or low-delay references, bounded by the decoded-picture-buffer count.
    VAPictureH264 list0[UTL_H264_MAX_REFERENCE_LIST_ENTRIES]; ///< Remaining entries are native invalid pictures.
    VAPictureH264 list1[UTL_H264_MAX_REFERENCE_LIST_ENTRIES]; ///< Remaining entries are native invalid pictures.
} ReferenceLists_t;

/*************************************************************************************************/
/**
 * \brief Typed native rate-control packet, including the VA miscellaneous-parameter tag.
 */
typedef struct RateControlPacket_s
{
    VAEncMiscParameterType        type;       ///< VAEncMiscParameterTypeRateControl.
    VAEncMiscParameterRateControl parameters; ///< Fully initialized rate-control payload.
} RateControlPacket_t;

/*************************************************************************************************/
/**
 * \brief Typed native frame-rate packet, including the VA miscellaneous-parameter tag.
 */
typedef struct FrameRatePacket_s
{
    VAEncMiscParameterType      type;       ///< VAEncMiscParameterTypeFrameRate.
    VAEncMiscParameterFrameRate parameters; ///< Fully initialized frame-rate payload.
} FrameRatePacket_t;

_Static_assert(offsetof(RateControlPacket_t, parameters) == sizeof(VAEncMiscParameterBuffer),
               "Rate-control payload shall immediately follow the native miscellaneous header");
_Static_assert(sizeof(RateControlPacket_t) == sizeof(VAEncMiscParameterBuffer) + sizeof(VAEncMiscParameterRateControl),
               "Rate-control packet shall have exactly the native packet extent");
_Static_assert(offsetof(FrameRatePacket_t, parameters) == sizeof(VAEncMiscParameterBuffer),
               "Frame-rate payload shall immediately follow the native miscellaneous header");
_Static_assert(sizeof(FrameRatePacket_t) == sizeof(VAEncMiscParameterBuffer) + sizeof(VAEncMiscParameterFrameRate),
               "Frame-rate packet shall have exactly the native packet extent");

/*************************************************************************************************/
/**
 * \brief Stream settings initialized before allocating encoder resources.
 *
 * Scaling lists are copied into the config so it can be retained by value.
 */
typedef struct Config_s
{
    uint32_t                  width;                ///< Visible input width in pixels.
    uint32_t                  height;               ///< Visible input height in pixels.
    Gfx_Encoder_H264Profile_t h264Profile;          ///< Requested profile or automatic selection.
    Gfx_Encoder_Entrypoint_t  entrypoint;           ///< Normal or low-power slice entrypoint.
    uint32_t                  frameRateNumerator;   ///< Nonzero frame-rate numerator, at most UINT16_MAX.
    uint32_t                  frameRateDenominator; ///< Nonzero frame-rate denominator, at most UINT16_MAX.
    uint32_t                  intraPeriod;          ///< Pictures between intra pictures; zero disables periodic intra.
    uint32_t                  intraIdrPeriod;       ///< Pictures between IDR pictures; at most UINT16_MAX.
    uint32_t                  ipPeriod;             ///< Nonzero display-order distance between I/P pictures.
    uint32_t sliceCount;     ///< 1..GFX_ENCODER_MAX_SLICES slices per picture, not exceeding its macroblock count.
    uint32_t targetBitRate;  ///< Rate-control target in bits per second, or zero when unused.
    uint32_t maximumBitRate; ///< Rate-control ceiling in bits per second, or zero when unused.
    Gfx_Encoder_RateControl_t rateControl;                   ///< Base rate-control algorithm.
    bool_t                    macroblockRateControl;         ///< Add VA macroblock-level rate control.
    bool_t                    parallelRateControl;           ///< Add VA parallel rate control.
    uint32_t                  rateControlWindowMilliseconds; ///< Sliding rate-control window duration.
    uint32_t                  initialQuantizationParameter;  ///< Initial QP supplied to rate control.
    uint32_t                  minimumQuantizationParameter;  ///< Minimum QP supplied to rate control.
    uint32_t                  maximumQuantizationParameter;  ///< Maximum QP supplied to rate control.
    uint32_t                  basicUnitSize;                 ///< Macroblocks in one rate-control basic unit.
    bool_t                    resetRateControl;              ///< Reset backend rate-control history at creation.
    bool_t                    disableFrameSkip;              ///< Prohibit frame skipping by rate control.
    bool_t                    disableBitStuffing;            ///< Prohibit filler-bit insertion.
    uint8_t                   macroblockRateControlMode;     ///< VA macroblock mode in the inclusive range 0..2.
    uint8_t                   temporalLayer;                 ///< Temporal-layer identifier supplied to rate control.
    bool_t                    constantFrameSizeIntraFrames;  ///< Include intra pictures in constant-size control.
    bool_t                    enableDynamicScaling;          ///< Permit rate control to scale frames dynamically.
    uint8_t                   frameToleranceMode;            ///< VA frame-size tolerance mode in the range 0..2.
    uint32_t intelligentConstantQualityFactor;               ///< ICQ quality factor when ICQ rate control is selected.
    uint32_t qualityFactor;                                  ///< Backend quality factor for quality-based modes.
    uint32_t targetFrameSize;                                ///< Per-picture coded-size target in bytes.
    uint32_t packedHeaders;                                  ///< Bitwise OR of Gfx_Encoder_PackedHeader_t flags.
    uint32_t maxReferenceFrames;                             ///< Maximum decoded-picture-buffer occupancy.
    uint32_t quantizationParameter;                          ///< Base picture QP in the H.264 range 0..51.
    bool_t   interlaced;                      ///< Allow frame/field coding instead of requiring progressive frames.
    bool_t   mbAdaptiveFrameField;            ///< Enable MBAFF; valid only for interlaced encoding.
    bool_t   fixedFrameRate;                  ///< Set fixed_frame_rate_flag in VUI parameters.
    uint32_t sampleAspectRatioWidth;          ///< SAR numerator; zero with height omits SAR signaling.
    uint32_t sampleAspectRatioHeight;         ///< SAR denominator; zero with width omits SAR signaling.
    bool_t   restrictMotionVectors;           ///< Signal VUI motion-vector length restrictions.
    uint32_t maxMotionVectorLengthHorizontal; ///< Nonzero maximum horizontal magnitude in quarter-pel units.
    uint32_t maxMotionVectorLengthVertical;   ///< Nonzero maximum vertical magnitude in quarter-pel units.
    bool_t   useCabac;                        ///< Use CABAC instead of CAVLC entropy coding.
    bool_t   useWeightedPrediction;           ///< Enable weighted prediction for P pictures.
    uint8_t  weightedBipredIdc;               ///< H.264 weighted_bipred_idc value in the range 0..2.
    bool_t   constrainedIntraPrediction;      ///< Restrict intra prediction to intra-coded neighbors.
    bool_t   use8x8Transform;                 ///< Enable the High-profile 8-by-8 transform.
    bool_t   redundantPictures;               ///< Enable redundant-picture syntax.
    bool_t   picOrderPresent;                 ///< Include picture-order fields in slice headers.
    bool_t   scalingListsPresent;             ///< Use the copied custom lists instead of defaults.
    uint8_t  scalingList4x4[UTL_H264_SCALING_LIST_4X4_TOTAL_ELEMENT_COUNT]; ///< Six copied 16-element scaling lists.
    uint8_t  scalingList8x8[UTL_H264_SCALING_LIST_8X8_TOTAL_ELEMENT_COUNT]; ///< Two copied 64-element scaling lists.
    int8_t   chromaQpIndexOffset;       ///< First H.264 chroma QP offset in the range -12..12.
    int8_t   secondChromaQpIndexOffset; ///< Second H.264 chroma QP offset in the range -12..12.
    uint8_t  cabacInitIdc;              ///< CABAC initialization table selector in the range 0..2.
    bool_t   enableDeblocking;          ///< Enable the in-loop deblocking filter.
    int8_t   deblockingAlphaOffsetDiv2; ///< Slice alpha offset divided by two, in the range -6..6.
    int8_t   deblockingBetaOffsetDiv2;  ///< Slice beta offset divided by two, in the range -6..6.
} Config_t;

/*************************************************************************************************/
/**
 * \brief VA encoding context, stream configuration, reference-picture history, and submission state.
 */
struct Gfx_Encoder_s
{
    Config_t       config;             ///< Retained stream settings with owned scaling-list storage.
    Gfx_Surface_t* surface;            ///< Input surface retained by the caller.
    VADisplay      dpy;                ///< Initialized VA display owning all identifiers.
    VAProfile      profile;            ///< Selected VA H.264 profile.
    uint8_t        profileIdc;         ///< Selected H.264 profile_idc.
    uint8_t        constraintSetFlags; ///< Selected H.264 constraint flags.
    VAConfigID     configId;           ///< H.264 encode configuration.
    VAContextID    context;            ///< H.264 encode context.
    VASurfaceID    reconstructionSurfaces[RECONSTRUCTION_SURFACE_CAPACITY]; ///< Persistent reconstruction-surface pool.
    VABufferID     codedBufId;                                              ///< Driver coded-output buffer.
    uint32_t       codedBufferCapacity;                                     ///< Startup-owned coded-buffer byte extent.
    VABufferID     sequenceParameterBufId;                                  ///< Reusable VA sequence-parameter buffer.
    VABufferID     pictureParameterBufId;                                   ///< Reusable VA picture-parameter buffer.
    VABufferID     sliceParameterBufId;                                     ///< Reusable VA slice-parameter buffer.
    VABufferID     packedHeaderParameterBufId;                              ///< SPS packed-header parameter buffer.
    VABufferID     packedHeaderDataBufId;                                   ///< SPS packed-header data buffer.
    VABufferID     packedPictureHeaderParameterBufId;                       ///< PPS packed-header parameter buffer.
    VABufferID     packedPictureHeaderDataBufId;                            ///< PPS packed-header data buffer.
    VABufferID     iqMatrixBufId;    ///< Optional H.264 quantization matrix buffer.
    VABufferID     rateControlBufId; ///< Persistent miscellaneous rate-control buffer.
    VABufferID     frameRateBufId;   ///< Persistent miscellaneous frame-rate buffer.
    VAEncSliceParameterBufferH264
                slice[GFX_ENCODER_MAX_SLICES]; ///< Fixed storage for the current picture's slice parameters.
    VASurfaceID decodedPictureSurfaces[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES]; ///< DPB surface identifiers.
    uint32_t    nextFrameNum; ///< Next reference decoding-order frame number.
    uint32_t    decodedPictureFrameNumbers
        [UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES]; ///< Stored reference frame_num values, independent of POC.
    uint32_t decodedPictureFrameIndices
        [UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES]; ///< Display indices parallel to DPB surfaces.
    bool_t decodedPictureLongTermReferences
        [UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES]; ///< Long-term flags parallel to DPB surfaces.
    bool_t decodedPictureTopFields
        [UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES]; ///< Top-field flags parallel to DPB surfaces.
    bool_t decodedPictureBottomFields
        [UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES]; ///< Bottom-field flags parallel to DPB surfaces.
    bool_t decodedPictureNonExisting
        [UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES]; ///< Non-existing flags parallel to DPB surfaces.
    uint32_t decodedPictureCount;                      ///< Initialized entries in every DPB array.
    uint32_t idrFrameIndex;                            ///< Absolute display-order origin of the active coded sequence.
    uint16_t idrPicId;                                 ///< Alternating identifier of IDR pictures.
    bool_t   needsIdr;                                 ///< Requires an IDR after creation or end of stream.
    bool_t   framePending;                             ///< TRUE until Status() consumes the submitted picture.
    bool_t   ready;                                    ///< TRUE after backend resource initialization.
};

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Constructs native sequence policy from the retained encoder configuration.
 *
 * \par Requirements:
 *  - 001 The selected profile shall be one of the encoder's validated eight-bit profiles.
 *  - 002 Geometry, crop, timing, aspect and ordering fields shall use shared H.264 derivations.
 *  - 003 Every native field, including reserved storage, shall be initialized without acquiring resources.
 *
 * \param[in] config   Validated retained encoder settings
 * \param[in] profile   Selected Baseline, Main or High profile
 * \param[out] sps   Native sequence parameters
 *
 * \retval None   No result value.
 */
static void InitializeSequenceParameters(const Config_t*                   config,
                                         VAProfile                         profile,
                                         VAEncSequenceParameterBufferH264* sps);

/*************************************************************************************************/
/**
 * \brief Initializes complete native reference-list storage.
 *
 * \par Requirements:
 *  - 001 Both counts shall be zero and every entry shall be an invalid native picture.
 *  - 002 Initialization shall use caller-owned storage and acquire no resources.
 *
 * \param[out] lists   Required storage for both native lists
 *
 * \retval None   No result value.
 */
static void InitializeReferenceLists(ReferenceLists_t* lists);

/*************************************************************************************************/
/**
 * \brief Constructs picture parameters for the current encoder submission.
 *
 * \par Requirements:
 *  - 001 Retained decoding-order numbers shall remain distinct from display-order field counts.
 *  - 002 Field counts and frame numbers shall fit the native types before conversion.
 *  - 003 Unused references and all reserved fields shall receive their defined initial values.
 *  - 004 Configuration and request contracts shall hold before calling this constructor.
 *
 * \param[in] encoder   Initialized encoder and current decoded-picture buffer
 * \param[in] request   Validated picture request
 * \param[in] frameIndex   Display index relative to the current IDR
 * \param[in] reconstructionSurface   Available reconstruction target
 * \param[out] picture   Required native picture-parameter storage
 *
 * \retval None   No result value.
 */
static void InitializePictureParameters(const Gfx_Encoder_t*             encoder,
                                        const Gfx_Encoder_EncodeInfo_t*  request,
                                        uint32_t                         frameIndex,
                                        VASurfaceID                      reconstructionSurface,
                                        VAEncPictureParameterBufferH264* picture);

/*************************************************************************************************/
/**
 * \brief Initializes native slice storage with bounded active prediction weights.
 *
 * \par Requirements:
 *  - 001 Every field shall initially be zero; enabled weights shall copy only the active reference prefix.
 *  - 002 Enabled weights shall require a nonempty list and both weight and offset pointers.
 *  - 003 Disabled weights shall require NULL caller pointers and retain zero native arrays.
 *  - 004 Counts shall fit the native reference arrays before any caller storage is accessed.
 *
 * \param[out] slice   Native slice receiving initialized weight arrays and zero remaining fields
 * \param[in] request   Picture request retaining enabled weight spans
 * \param[in] lists   Initialized native reference lists and active counts
 *
 * \retval None   No result value.
 */
static void InitializeSliceWeights(VAEncSliceParameterBufferH264*  slice,
                                   const Gfx_Encoder_EncodeInfo_t* request,
                                   const ReferenceLists_t*         lists);

/*************************************************************************************************/
/**
 * \brief Constructs one bounded slice of the encoder submission.
 *
 * \par Requirements:
 *  - 001 Slice ranges shall partition the coded macroblocks without gaps or overlap.
 *  - 002 Active reference and weight counts shall fit both decoded and native list capacities.
 *  - 003 Only active caller weight prefixes shall be read; remaining native entries shall be zero.
 *  - 004 All constructor inputs shall satisfy the selected H.264 configuration and picture kind.
 *  - 005 Construction shall modify only the retained slice slot and acquire no resources.
 *
 * \param[in,out] encoder   Initialized encoder with retained slice storage
 * \param[in] request   Validated picture request
 * \param[in] frameIndex   Display index relative to the current IDR
 * \param[in] lists   Complete native reference lists with active counts
 * \param[in] sliceIndex   Existing slice slot within the configured picture partition
 *
 * \retval None   No result value.
 */
static void InitializeSliceParameters(Gfx_Encoder_t*                  encoder,
                                      const Gfx_Encoder_EncodeInfo_t* request,
                                      uint32_t                        frameIndex,
                                      const ReferenceLists_t*         lists,
                                      uint32_t                        sliceIndex);

/*************************************************************************************************/
/**
 * \brief Initializes every stream setting from a validated creation request.
 *
 * \param[out] config     Receives initialized stream settings
 * \param[in]  createInfo Creation request providing the input dimensions and stream settings
 *
 * \retval None   No result value.
 */
static void InitializeConfig(Config_t* config, const Gfx_Encoder_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Derives native flags for one retained decoded picture.
 *
 * \par Requirements:
 *  - 001 Index shall identify an initialized entry in the bounded decoded-picture buffer.
 *  - 002 Reference, field and missing-picture semantics shall agree with the retained metadata.
 *
 * \param[in] encoder   Initialized encoder with a valid decoded-picture buffer
 * \param[in] index   Reference entry index
 *
 * \retval flags   Native H.264 picture flags.
 */
static uint32_t GetDecodedPictureFlags(const Gfx_Encoder_t* encoder, uint32_t index);

/*************************************************************************************************/
/**
 * \brief Derives the encoder's startup coded-buffer capacity.
 *
 * \par Requirements:
 *  - 001 Sample storage shall use padded H.264 dimensions and the selected VA format.
 *  - 002 The result shall include the existing two-parameter-set reserve and fit the native capacity.
 *  - 003 This estimate shall not be treated as a proof that every compressed picture fits.
 *
 * \param[in] config   Validated encoder geometry
 * \param[in] format   Selected native render-target format
 *
 * \retval byteCount   Required startup buffer capacity.
 */
static uint32_t GetCodedBufferCapacity(const Config_t* config, uint32_t format);

/*************************************************************************************************/
/**
 * \brief Enforces all VA/H.264 creation invariants before allocating encoder resources.
 *
 * \param[in] createInfo        VA encoder configuration including rate-control, GOP, slice, profile, and surface constraints
 *
 * \retval    None   No result value.
 */
static void ValidateCreateInfo(const Gfx_Encoder_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Maps the public rate-control mode and modifiers to a VA_RC_* mask.
 *
 * \param[in] rateControl                  Base rate-control algorithm
 * \param[in] macroblockRateControl        Adds VA_RC_MB when true
 * \param[in] parallelRateControl          Adds VA_RC_PARALLEL when true
 *
 * \return VA rate-control capability mask; unsupported public modes are fatal.
 */
static uint32_t GetVaapiRateControl(Gfx_Encoder_RateControl_t rateControl,
                                    bool_t                    macroblockRateControl,
                                    bool_t                    parallelRateControl);

/*************************************************************************************************/
/**
 * \brief Maps public packed-header requests to VA packed-header capabilities.
 *
 * \param[in] packedHeaders        Sequence and/or picture request mask
 *
 * \return VA_ENC_PACKED_HEADER mask for sequence and picture headers.
 */
static uint32_t GetVaapiPackedHeaders(uint32_t packedHeaders);

/*************************************************************************************************/
/**
 * \brief Maps frame structure to the libva context scan-mode flag.
 *
 * \param[in] interlaced        TRUE for an interlaced context, FALSE for progressive
 *
 * \return Zero for interlaced coding or VA_PROGRESSIVE otherwise.
 */
static int32_t GetVaapiScanMode(bool_t interlaced);

/*************************************************************************************************/
/**
 * \brief Maps a requested H.264 profile to libva.
 *
 * \param[in] profile Requested profile or automatic selection
 *
 * \return VA profile, or VAProfileNone for automatic selection.
 */
static VAProfile GetVaapiProfile(Gfx_Encoder_H264Profile_t profile);

/*************************************************************************************************/
/**
 * \brief Maps a VA encoder profile to H.264 profile_idc and constraint_set flags.
 *
 * \param[in] profile Selected VA profile
 * \param[out] profileIdc H.264 profile_idc
 * \param[out] constraintSetFlags Required profile compatibility flags
 */
static void GetH264Profile(VAProfile profile, uint8_t* profileIdc, uint8_t* constraintSetFlags);

/*************************************************************************************************/
/**
 * \brief Maps the requested slice-encoding entrypoint to the first libva entrypoint to try.
 *
 * \param[in] entrypoint        Normal, low-power, or automatic slice encoder selection
 *
 * \return VAEntrypointEncSlice or VAEntrypointEncSliceLP; AUTO prefers low-power slices.
 */
static VAEntrypoint GetVaapiEntrypoint(Gfx_Encoder_Entrypoint_t entrypoint);

/*************************************************************************************************/
/**
 * \brief Maps an H.264 picture type to libva and identifies IDR pictures.
 *
 * \param[in]  pictureType H.264 picture type
 * \param[out] idrPicture  Receives TRUE for IDR pictures, FALSE otherwise; must not be NULL
 *
 * \return VA encoding picture type; unsupported H.264 picture types are fatal.
 */
static VAEncPictureType GetVaapiPictureTypeH264(Utl_H264_PictureType_t pictureType, bool_t* idrPicture);

/*************************************************************************************************/
/**
 * \brief Selects a compatible VA profile and supported encoding entrypoint.
 *
 * \par Details:
 *  Tries compatible profiles in the supplied order for the preferred entrypoint. AUTO entrypoint
 *  requests may fall back to the first compatible profile supporting normal slice encoding.
 *  Explicit profile requests try only that profile. Asserts if no supported selection exists.
 *
 * \param[in]  dpy                      Initialized VA display
 * \param[in]  requestedProfile         Mapped requested profile; VAProfileNone selects AUTO candidates
 * \param[in]  requestedEntrypoint      Requested entrypoint mode; only AUTO permits fallback
 * \param[in]  preferredEntrypoint      Mapped entrypoint to try before fallback
 * \param[in]  sliceType                Required H.264 slice type
 * \param[in]  interlaced               Allow interlaced coding
 * \param[in]  useCabac                 Require CABAC support
 * \param[in]  use8x8Transform          Require 8-by-8 transform support
 * \param[in]  scalingListsPresent      Require custom scaling-list support
 * \param[in]  useWeightedPrediction    Require weighted P prediction
 * \param[in]  weightedBipredIdc        Weighted B prediction mode
 * \param[in]  redundantPictures        Require redundant-picture syntax
 * \param[in]  chromaQpIndexOffset      First chroma QP offset
 * \param[in]  secondChromaQpIndexOffset Second chroma QP offset
 * \param[out] entrypoint               Selected encoding entrypoint
 * \param[out] profileIdc               Selected H.264 profile_idc
 * \param[out] constraintSetFlags       Selected H.264 constraint flags
 *
 * \return Selected VA profile.
 */
static VAProfile SelectVaapiProfile(VADisplay                dpy,
                                    VAProfile                requestedProfile,
                                    Gfx_Encoder_Entrypoint_t requestedEntrypoint,
                                    VAEntrypoint             preferredEntrypoint,
                                    uint8_t                  sliceType,
                                    bool_t                   interlaced,
                                    bool_t                   useCabac,
                                    bool_t                   use8x8Transform,
                                    bool_t                   scalingListsPresent,
                                    bool_t                   useWeightedPrediction,
                                    uint8_t                  weightedBipredIdc,
                                    bool_t                   redundantPictures,
                                    int8_t                   chromaQpIndexOffset,
                                    int8_t                   secondChromaQpIndexOffset,
                                    VAEntrypoint*            entrypoint,
                                    uint8_t*                 profileIdc,
                                    uint8_t*                 constraintSetFlags);

/*************************************************************************************************/
/**
 * \brief Selects supported VA encoding configuration attributes.
 *
 * \par Details:
 *  Asserts that every requested capability is supported. Unsupported attributes are omitted
 *  only when their requested value is zero; retained attributes are packed at the start of attributes.
 *
 * \param[in]  dpy                   Initialized VA display
 * \param[in]  profile               Selected encoding profile
 * \param[in]  entrypoint            Selected encoding entrypoint
 * \param[in]  format                Requested VA render-target format
 * \param[in]  rateControl           Requested rate-control algorithm
 * \param[in]  macroblockRateControl Enables macroblock rate control
 * \param[in]  parallelRateControl   Enables parallel rate control
 * \param[in]  packedHeaders         Requested sequence and picture packed-header mask
 * \param[out] attributes            Storage for CONFIG_ATTRIBUTE_CAPACITY configuration attributes
 *
 * \return Number of selected configuration attributes.
 */
static int32_t SelectVaapiConfig(VADisplay                 dpy,
                                 VAProfile                 profile,
                                 VAEntrypoint              entrypoint,
                                 uint32_t                  format,
                                 Gfx_Encoder_RateControl_t rateControl,
                                 bool_t                    macroblockRateControl,
                                 bool_t                    parallelRateControl,
                                 uint32_t                  packedHeaders,
                                 VAConfigAttrib*           attributes);

/*************************************************************************************************/
/**
 * \brief Checks whether a surface is retained in the decoded picture buffer.
 *
 * \param[in] decodedPictureSurfaces Active decoded reference surface identifiers
 * \param[in] decodedPictureCount    Number of active decoded references
 * \param[in] surfaceId              Surface identifier to find
 *
 * \retval TRUE  The surface is retained as a decoded reference.
 * \retval FALSE No decoded reference uses the surface.
 */
static bool_t IsDecodedPictureSurface(const VASurfaceID* decodedPictureSurfaces,
                                      uint32_t           decodedPictureCount,
                                      VASurfaceID        surfaceId);

/*************************************************************************************************/
/**
 * \brief Selects the first reconstruction surface not retained as a decoded reference.
 *
 * \par Details:
 *  Asserts if no valid reconstruction surface is available outside the decoded picture buffer.
 *
 * \param[in] reconstructionSurfaces     Reconstruction surface identifiers in selection order
 * \param[in] reconstructionSurfaceCount Number of reconstruction surfaces
 * \param[in] decodedPictureSurfaces     Active decoded reference surface identifiers
 * \param[in] decodedPictureCount        Number of active decoded references
 *
 * \return Available reconstruction surface identifier.
 */
static VASurfaceID GetReconstructionSurfaceId(const VASurfaceID* reconstructionSurfaces,
                                              uint32_t           reconstructionSurfaceCount,
                                              const VASurfaceID* decodedPictureSurfaces,
                                              uint32_t           decodedPictureCount);

/*************************************************************************************************/
/**
 * \brief Selects one ordered active reference list from the encoder's retained decoded pictures.
 *
 * \par Requirements:
 *  - 001 Native frame_idx shall use stored decoding-order numbers; POC shall use display-order indices.
 *  - 002 A low-delay B picture may reuse earlier references when no future reference is retained.
 *  - 003 Every selected index and field-order conversion shall be checked before use.
 *
 * \param[in] encoder   Encoder retaining all reference metadata
 * \param[in] frameIndex   Current display-order index relative to the IDR
 * \param[in] futureReferences   Select future references, with the documented low-delay fallback
 * \param[out] referenceList   Native list with capacity for the decoded-picture buffer
 *
 * \retval count   Initialized active references, at most the decoded-picture-buffer capacity.
 */
static uint32_t BuildReferenceListH264(const Gfx_Encoder_t* encoder,
                                       uint32_t             frameIndex,
                                       bool_t               futureReferences,
                                       VAPictureH264*       referenceList);

/*************************************************************************************************/
/**
 * \brief Validates per-picture prediction weights against the stream settings.
 *
 * \par Details:
 *  List-0 weights require a P picture with weighted prediction enabled or a B picture with
 *  explicit weighted biprediction (weighted_bipred_idc equal to 1). List-1 weights require
 *  a B picture with explicit weighted biprediction. Incompatible requests assert.
 *
 * \param[in] config     Stream prediction settings
 * \param[in] encodeInfo  Per-picture weight flags
 * \param[in] pictureType Mapped VA encoding picture type
 *
 * \retval None   No result value.
 */
static void ValidatePredictionWeightsH264(const Config_t*                 config,
                                          const Gfx_Encoder_EncodeInfo_t* encodeInfo,
                                          VAEncPictureType                pictureType);

/*************************************************************************************************/
/**
 * \brief Derives the complete byte count of a valid mapped access unit.
 *
 * \par Requirements:
 *  - 001 Output pointers shall be required; missing outputs shall assert.
 *  - 002 The driver shall retain a stable, readable segment chain until it is unmapped.
 *  - 003 The chain shall terminate within CODED_SEGMENT_CAPACITY entries.
 *  - 004 Empty, corrupt, overflowed or non-byte-aligned segments shall be rejected.
 *  - 005 Output values shall be published only after validating the complete chain.
 *  - 006 The complete payload shall fit the encoder's startup-allocated coded buffer.
 *
 * \param[in]  first         First driver segment, or NULL for an invalid empty chain
 * \param[in]  capacity      Nonzero native coded-buffer capacity in bytes
 * \param[out] segmentCount  Receives the complete segment count on success
 * \param[out] byteCount     Receives the complete access-unit byte count on success
 *
 * \retval TRUE   The complete mapped chain can be copied.
 * \retval FALSE  The chain is invalid; outputs are unchanged.
 */
static bool_t GetCodedByteCount(const VACodedBufferSegment* first,
                                uint32_t                    capacity,
                                uint32_t*                   segmentCount,
                                uint64_t*                   byteCount);

/*************************************************************************************************/
/**
 * \brief Polls and collects the pending encoded access unit.
 *
 * \par Requirements:
 *  - 001 The encoder shall be initialized and exclusively owned for this call.
 *  - 002 Destination storage shall be disjoint from encoder and native mapped storage.
 *  - 003 Hardware completion shall be polled without an unbounded synchronization wait.
 *  - 004 The complete segment chain shall be validated before any destination write.
 *  - 005 An undersized destination shall retain the pending access unit for retry.
 *  - 006 Invalid encoded output shall release the input and require the next picture to be IDR.
 *
 * \param[in,out] encoder        Encoder with at most one pending access unit
 * \param[out]    codedBytes     Caller-owned destination used only during this call
 * \param[in]     codedCapacity  Destination capacity in bytes
 * \param[out]    codedSize      Copied/required bytes, zero when unavailable, UINT64_MAX on failure
 *
 * \retval TRUE   A complete access unit was copied and its input was released.
 * \retval FALSE  No output was copied; codedSize distinguishes unavailable, capacity and failure.
 */
static bool_t GetStatus(Gfx_Encoder_t* encoder, void* codedBytes, uint64_t codedCapacity, uint64_t* codedSize);

/*************************************************************************************************/
/**
 * \brief Creates reusable VA encoding buffers.
 *
 * \param[in] dpy Initialized VA display
 * \param[in] context Encode context
 * \param[in] profile Selected H.264 profile
 * \param[in] profileIdc Selected H.264 profile_idc
 * \param[in] constraintSetFlags Selected H.264 constraint flags
 * \param[in] config Stream settings
 * \param[in] codedBufferCapacity Coded-output capacity including the caller's header allowance
 * \param[out] codedBufId Reusable driver coded-output buffer.
 * \param[out] sequenceParameterBufId Reusable VA sequence-parameter buffer.
 * \param[out] pictureParameterBufId Reusable VA picture-parameter buffer.
 * \param[out] sliceParameterBufId Reusable VA slice-parameter buffer.
 * \param[out] packedHeaderParameterBufId SPS packed-header parameter buffer.
 * \param[out] packedHeaderDataBufId SPS packed-header data buffer.
 * \param[out] packedPictureHeaderParameterBufId PPS packed-header parameter buffer.
 * \param[out] packedPictureHeaderDataBufId PPS packed-header data buffer.
 * \param[out] rateControlBufId Persistent miscellaneous rate-control buffer.
 * \param[out] frameRateBufId Persistent miscellaneous frame-rate buffer.
 *
 * \retval None   No result value.
 */
static void CreateVaapiEncoderResources(VADisplay       dpy,
                                        VAContextID     context,
                                        VAProfile       profile,
                                        uint8_t         profileIdc,
                                        uint8_t         constraintSetFlags,
                                        const Config_t* config,
                                        uint32_t        codedBufferCapacity,
                                        VABufferID*     codedBufId,
                                        VABufferID*     sequenceParameterBufId,
                                        VABufferID*     pictureParameterBufId,
                                        VABufferID*     sliceParameterBufId,
                                        VABufferID*     packedHeaderParameterBufId,
                                        VABufferID*     packedHeaderDataBufId,
                                        VABufferID*     packedPictureHeaderParameterBufId,
                                        VABufferID*     packedPictureHeaderDataBufId,
                                        VABufferID*     rateControlBufId,
                                        VABufferID*     frameRateBufId);

/*************************************************************************************************/
/**
 * \brief Selects the profile and entrypoint, then creates the VA configuration, surfaces, and context.
 *
 * \param[in] dpy Initialized VA display
 * \param[in] format VA render-target format
 * \param[in] pixelFormat VA pixel format
 * \param[in] surfaceId Input VA surface
 * \param[in] width Input width
 * \param[in] height Input height
 * \param[in] h264Profile Requested H.264 profile
 * \param[in] requestedEntrypoint Requested slice entrypoint
 * \param[in] rateControl Base rate-control mode
 * \param[in] macroblockRateControl Enable macroblock rate control
 * \param[in] parallelRateControl Enable parallel rate control
 * \param[in] packedHeaders Requested packed-header flags
 * \param[in] interlaced Allow interlaced coding
 * \param[in] useCabac Require CABAC support
 * \param[in] use8x8Transform Require 8-by-8 transform support
 * \param[in] scalingListsPresent Require custom scaling-list support
 * \param[in] ipPeriod Stream coding setting
 * \param[in] useWeightedPrediction Stream coding setting
 * \param[in] weightedBipredIdc Stream coding setting
 * \param[in] redundantPictures Stream coding setting
 * \param[in] chromaQpIndexOffset Stream coding setting
 * \param[in] secondChromaQpIndexOffset Stream coding setting
 * \param[out] reconstructionSurfaces Reconstruction surface storage
 * \param[in] reconstructionSurfaceCount Reconstruction surface storage capacity
 * \param[out] configId Created encode configuration
 * \param[out] context Created encode context
 * \param[out] profile Selected H.264 profile
 * \param[out] profileIdc Selected H.264 profile_idc
 * \param[out] constraintSetFlags Selected H.264 constraint flags
 *
 * \retval None   No result value.
 */
static void CreateVaapiEncoder(VADisplay                 dpy,
                               uint32_t                  format,
                               uint32_t                  pixelFormat,
                               VASurfaceID               surfaceId,
                               uint32_t                  width,
                               uint32_t                  height,
                               Gfx_Encoder_H264Profile_t h264Profile,
                               Gfx_Encoder_Entrypoint_t  requestedEntrypoint,
                               Gfx_Encoder_RateControl_t rateControl,
                               bool_t                    macroblockRateControl,
                               bool_t                    parallelRateControl,
                               uint32_t                  packedHeaders,
                               bool_t                    interlaced,
                               bool_t                    useCabac,
                               bool_t                    use8x8Transform,
                               bool_t                    scalingListsPresent,
                               uint32_t                  ipPeriod,
                               bool_t                    useWeightedPrediction,
                               uint8_t                   weightedBipredIdc,
                               bool_t                    redundantPictures,
                               int8_t                    chromaQpIndexOffset,
                               int8_t                    secondChromaQpIndexOffset,
                               VASurfaceID*              reconstructionSurfaces,
                               uint32_t                  reconstructionSurfaceCount,
                               VAConfigID*               configId,
                               VAContextID*              context,
                               VAProfile*                profile,
                               uint8_t*                  profileIdc,
                               uint8_t*                  constraintSetFlags);

/*************************************************************************************************/
/**
 * \brief Creates the selected encoder backend.
 *
 * \param[out] encoder        Zero-initialized encoder marked ready after complete creation
 * \param[in] createInfo      Backend and codec configuration
 *
 * \retval    None   No result value.
 */
static void CreateEncoder(Gfx_Encoder_t* encoder, const Gfx_Encoder_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Resets reference history and frame numbering for a new IDR picture.
 *
 * \par Details:
 *  Discards prior decoded references and makes frameIndex the new frame-number and POC origin.
 *  Alternates idr_pic_id between zero and one because H.264 clause 7.4.3 requires different
 *  identifiers when consecutive access units are both IDR access units.
 *
 * \param[in]     frameIndex          Absolute display-order index of the IDR picture
 * \param[out]    decodedPictureCount Receives zero to discard prior references
 * \param[out]    idrFrameIndex       Receives the new frame-number and POC origin
 * \param[in,out] idrPicId            IDR identifier to alternate between zero and one
 *
 * \retval None   No result value.
 */
static void ResetIdrState(uint32_t  frameIndex,
                          uint32_t* decodedPictureCount,
                          uint32_t* idrFrameIndex,
                          uint16_t* idrPicId);

/*************************************************************************************************/
/**
 * \brief Retains a submitted reference picture in the decoded picture buffer.
 *
 * \par Details:
 *  Called only for reference pictures when the configured reference limit is nonzero.
 *  When full, removes the oldest entry and shifts every parallel metadata array before appending
 *  the new surface and its reference metadata. The resulting entry count must fit the configured limit.
 *
 * \param[in,out] encoder               Encoder owning the decoded picture buffer
 * \param[in]     referenceType         Short-term or long-term reference classification
 * \param[in]     reconstructionSurfaceId Surface containing the submitted reconstruction
 * \param[in]     frameIndex            Display-order index relative to the latest IDR
 * \param[in]     topField              TRUE for a top-field reference
 * \param[in]     bottomField           TRUE for a bottom-field reference
 *
 * \retval None   No result value.
 */
static void UpdateDecodedPictureBuffer(Gfx_Encoder_t*           encoder,
                                       Utl_H264_ReferenceType_t referenceType,
                                       VASurfaceID              reconstructionSurfaceId,
                                       uint32_t                 frameIndex,
                                       bool_t                   topField,
                                       bool_t                   bottomField);

/*************************************************************************************************/
/**
 * \brief Dispatches one acquired image to the encoder backend.
 *
 * \par Details:
 *  Asserts if the requested picture coding is incompatible with the selected H.264 profile.
 *
 * \param[in,out] encoder    Initialized backend state and reference history
 * \param[in,out] image      Acquired input whose pending upload is consumed
 * \param[in]     encodeInfo Per-picture type, reference, prediction, weighting and end-of-stream settings
 *
 * \retval None   No result value.
 */
static void EncodeImage(Gfx_Encoder_t* encoder, Gfx_Image_t* image, const Gfx_Encoder_EncodeInfo_t* encodeInfo);

/*************************************************************************************************/
/**
 * \brief Submits one picture for encoding.
 *
 * \par Details:
 *  Asserts that the encoder is ready and surface acquisition has a published image.
 *
 * \param[in,out] encoder    Ready encoder retaining pending submissions
 * \param[in]     encodeInfo Per-picture encode request
 *
 * \retval TRUE  An image was submitted and retained until Status().
 * \retval FALSE A prior access unit is pending or an IDR is required.
 */
static bool_t Encode(Gfx_Encoder_t* encoder, const Gfx_Encoder_EncodeInfo_t* encodeInfo);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The selected encoder profile is eight-bit 4:2:0; native sequence fields follow the shared H.264 derivations.
 */
static void InitializeSequenceParameters(const Config_t*                   config,
                                         VAProfile                         profile,
                                         VAEncSequenceParameterBufferH264* sps)
{
    uint32_t width_in_mbs;
    uint32_t height_in_mbs;
    uint32_t padded_width;
    uint32_t padded_height;
    uint32_t chroma_format_idc;
    uint32_t crop_right_offset;
    uint32_t crop_bottom_offset;
    uint32_t frame_num_range;
    uint32_t poc_range;
    uint32_t time_scale;
    uint8_t  aspect_ratio_idc;
    uint32_t log2_max_mv_length_horizontal;
    uint32_t log2_max_mv_length_vertical;
    uint32_t sar_width;
    uint32_t sar_height;
    bool_t   frame_cropping;
    bool_t   aspect_ratio_present;

    bool_t   is_baseline;
    bool_t   is_main;
    bool_t   is_high;
    bool_t   is_legacy;
    bool_t   is_supported;

    LOGASSERT(NULL != config);
    LOGASSERT(NULL != sps);
    is_baseline  = VAProfileH264ConstrainedBaseline == profile;
    is_main      = VAProfileH264Main == profile;
    is_high      = VAProfileH264High == profile;
    is_legacy    = is_baseline || is_main;
    is_supported = is_legacy || is_high;
    LOGASSERT(is_supported);
    chroma_format_idc = UTL_H264_CHROMA_FORMAT_420;
    Utl_H264_DeriveCodedDimensions(config->width,
                                   config->height,
                                   config->interlaced,
                                   &width_in_mbs,
                                   &height_in_mbs,
                                   &padded_width,
                                   &padded_height);
    LOGASSERT(width_in_mbs <= UINT16_MAX);
    LOGASSERT(height_in_mbs <= UINT16_MAX);

    frame_num_range = Utl_H264_DeriveMaximumFrameNumber(config->intraIdrPeriod);
    poc_range       = Utl_H264_DeriveMaximumPicOrderCntLsb(frame_num_range);
    time_scale      = Utl_H264_DeriveTimeScale(config->frameRateNumerator);

    frame_cropping = Utl_H264_DeriveFrameCropOffsets(config->width,
                                                     config->height,
                                                     padded_width,
                                                     padded_height,
                                                     chroma_format_idc,
                                                     config->interlaced,
                                                     &crop_right_offset,
                                                     &crop_bottom_offset);
    Utl_H264_DeriveAspectRatio(config->sampleAspectRatioWidth,
                               config->sampleAspectRatioHeight,
                               &aspect_ratio_idc,
                               &sar_width,
                               &sar_height);
    aspect_ratio_present = UTL_H264_ASPECT_RATIO_IDC_UNSPECIFIED != aspect_ratio_idc;
    Utl_H264_DeriveMotionVectorLimits(config->restrictMotionVectors,
                                      config->maxMotionVectorLengthHorizontal,
                                      config->maxMotionVectorLengthVertical,
                                      &log2_max_mv_length_horizontal,
                                      &log2_max_mv_length_vertical);

    LOGASSERT(0 != width_in_mbs);
    LOGASSERT(0 != height_in_mbs);
    LOGASSERT(chroma_format_idc <= UTL_H264_CHROMA_FORMAT_444);
    LOGASSERT(0 != frame_num_range);
    LOGASSERT(0 != poc_range);

    CRT_Memory_Set(sps, 0, sizeof((*sps)));
    sps->seq_parameter_set_id = 0;
    sps->level_idc =
        Utl_H264_DeriveLevelIdc(padded_width, padded_height, config->frameRateNumerator, config->frameRateDenominator);
    sps->intra_period                                            = config->intraPeriod;
    sps->intra_idr_period                                        = config->intraIdrPeriod;
    sps->ip_period                                               = config->ipPeriod;
    sps->bits_per_second                                         = config->maximumBitRate;
    sps->max_num_ref_frames                                      = config->maxReferenceFrames;
    sps->picture_width_in_mbs                                    = (uint16_t)width_in_mbs;
    sps->picture_height_in_mbs                                   = (uint16_t)height_in_mbs;
    sps->seq_fields.bits.chroma_format_idc                       = chroma_format_idc;
    sps->seq_fields.bits.frame_mbs_only_flag                     = FALSE == config->interlaced;
    sps->seq_fields.bits.mb_adaptive_frame_field_flag            = config->mbAdaptiveFrameField;
    sps->seq_fields.bits.seq_scaling_matrix_present_flag         = config->scalingListsPresent;
    sps->seq_fields.bits.direct_8x8_inference_flag               = TRUE;
    sps->seq_fields.bits.log2_max_frame_num_minus4               = Utl_H264_DeriveLog2Minus4(frame_num_range);
    sps->seq_fields.bits.pic_order_cnt_type                      = UTL_H264_PIC_ORDER_CNT_TYPE_0;
    sps->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4       = Utl_H264_DeriveLog2Minus4(poc_range);
    sps->seq_fields.bits.delta_pic_order_always_zero_flag        = FALSE;
    sps->bit_depth_luma_minus8                                   = 0;
    sps->bit_depth_chroma_minus8                                 = 0;
    sps->num_ref_frames_in_pic_order_cnt_cycle                   = 0;
    sps->offset_for_non_ref_pic                                  = 0;
    sps->offset_for_top_to_bottom_field                          = 0;
    sps->frame_cropping_flag                                     = (uint8_t)frame_cropping;
    sps->frame_crop_left_offset                                  = 0;
    sps->frame_crop_right_offset                                 = crop_right_offset;
    sps->frame_crop_top_offset                                   = 0;
    sps->frame_crop_bottom_offset                                = crop_bottom_offset;
    sps->vui_parameters_present_flag                             = TRUE;
    sps->vui_fields.bits.aspect_ratio_info_present_flag          = aspect_ratio_present;
    sps->vui_fields.bits.timing_info_present_flag                = TRUE;
    sps->vui_fields.bits.bitstream_restriction_flag              = config->restrictMotionVectors;
    sps->vui_fields.bits.log2_max_mv_length_horizontal           = log2_max_mv_length_horizontal;
    sps->vui_fields.bits.log2_max_mv_length_vertical             = log2_max_mv_length_vertical;
    sps->vui_fields.bits.fixed_frame_rate_flag                   = config->fixedFrameRate;
    sps->vui_fields.bits.low_delay_hrd_flag                      = FALSE;
    sps->vui_fields.bits.motion_vectors_over_pic_boundaries_flag = TRUE;
    sps->aspect_ratio_idc                                        = aspect_ratio_idc;
    sps->sar_width                                               = sar_width;
    sps->sar_height                                              = sar_height;
    sps->num_units_in_tick                                       = config->frameRateDenominator;
    sps->time_scale                                              = time_scale;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Unused reference entries receive VA's invalid-picture sentinel, including every reserved field.
 */
static void InitializeReferenceLists(ReferenceLists_t* lists)
{
    uint32_t index;

    LOGASSERT(NULL != lists);
    lists->count0 = 0;
    lists->count1 = 0;
    for (index = 0; index < UTL_H264_MAX_REFERENCE_LIST_ENTRIES; ++index)
    {
        cvaInitPictureH264(&lists->list0[index], VA_INVALID_SURFACE, 0, VA_PICTURE_H264_INVALID, 0, 0);
        cvaInitPictureH264(&lists->list1[index], VA_INVALID_SURFACE, 0, VA_PICTURE_H264_INVALID, 0, 0);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Native frame_idx follows decoding order. Field-order counts follow display order since the IDR.
 */
static void InitializePictureParameters(const Gfx_Encoder_t*             encoder,
                                        const Gfx_Encoder_EncodeInfo_t*  request,
                                        uint32_t                         frameIndex,
                                        VASurfaceID                      reconstructionSurface,
                                        VAEncPictureParameterBufferH264* picture)
{
    const Config_t* config;
    uint32_t        flags;
    uint32_t        order_count;
    uint32_t        index;
    uint32_t        reference_flags;
    uint32_t        default_count0;
    uint32_t        default_count1;
    bool_t          idr_picture;
    bool_t          top_field;
    bool_t          bottom_field;
    bool_t          long_term;
    bool_t          field_picture;

    LOGASSERT(NULL != encoder);
    LOGASSERT(NULL != request);
    LOGASSERT(NULL != picture);
    LOGASSERT(VA_INVALID_SURFACE != reconstructionSurface);
    LOGASSERT(VA_INVALID_ID != encoder->codedBufId);
    LOGASSERT(UINT16_MAX >= encoder->nextFrameNum);

    config        = &encoder->config;
    idr_picture   = UTL_H264_PICTURE_TYPE_IDR == request->pictureType;
    top_field     = UTL_H264_FIELD_MODE_TOP == request->fieldMode;
    bottom_field  = UTL_H264_FIELD_MODE_BOTTOM == request->fieldMode;
    long_term     = UTL_H264_REFERENCE_TYPE_LONG_TERM == request->referenceType;
    field_picture = top_field || bottom_field;
    LOGASSERT(config->maxReferenceFrames >= encoder->decodedPictureCount);
    LOGASSERT(UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES >= encoder->decodedPictureCount);
    if (field_picture)
    {
        LOGASSERT(config->interlaced);
    }
    if (long_term)
    {
        LOGASSERT(request->referencePicture);
    }
    if (idr_picture)
    {
        LOGASSERT(request->referencePicture);
        LOGASSERT(0 == encoder->decodedPictureCount);
        LOGASSERT(0 == frameIndex);
        LOGASSERT(0 == encoder->nextFrameNum);
    }

    flags = 0;
    if (request->referencePicture)
    {
        flags = long_term ? VA_PICTURE_H264_LONG_TERM_REFERENCE : VA_PICTURE_H264_SHORT_TERM_REFERENCE;
    }
    if (top_field)
    {
        flags |= VA_PICTURE_H264_TOP_FIELD;
    }
    if (bottom_field)
    {
        flags |= VA_PICTURE_H264_BOTTOM_FIELD;
    }
    order_count = Utl_H264_DeriveFieldOrderCount(frameIndex);
    LOGASSERT((INT32_MAX - 1) >= order_count);

    Utl_H264_DeriveActiveReferenceCounts(UTL_H264_SLICE_TYPE_I, 0, &default_count0, &default_count1);
    LOGASSERT(0 < default_count0);
    LOGASSERT(UTL_H264_MAX_REFERENCE_LIST_ENTRIES >= default_count0);
    LOGASSERT(0 < default_count1);
    LOGASSERT(UTL_H264_MAX_REFERENCE_LIST_ENTRIES >= default_count1);

    CRT_Memory_Set(picture, 0, sizeof(*picture));
    picture->num_ref_idx_l0_active_minus1 = (uint8_t)(default_count0 - 1);
    picture->num_ref_idx_l1_active_minus1 = (uint8_t)(default_count1 - 1);
    cvaInitPictureH264(&picture->CurrPic,
                       reconstructionSurface,
                       encoder->nextFrameNum,
                       flags,
                       (int32_t)order_count,
                       (int32_t)order_count + 1);
    for (index = 0; index < UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES; ++index)
    {
        cvaInitPictureH264(&picture->ReferenceFrames[index], VA_INVALID_SURFACE, 0, VA_PICTURE_H264_INVALID, 0, 0);
    }
    for (index = 0; index < encoder->decodedPictureCount; ++index)
    {
        reference_flags = GetDecodedPictureFlags(encoder, index);
        order_count     = Utl_H264_DeriveFieldOrderCount(encoder->decodedPictureFrameIndices[index]);
        LOGASSERT((INT32_MAX - 1) >= order_count);
        cvaInitPictureH264(&picture->ReferenceFrames[index],
                           encoder->decodedPictureSurfaces[index],
                           encoder->decodedPictureFrameNumbers[index],
                           reference_flags,
                           (int32_t)order_count,
                           (int32_t)order_count + 1);
    }

    picture->coded_buf              = encoder->codedBufId;
    picture->pic_parameter_set_id   = 0;
    picture->seq_parameter_set_id   = 0;
    picture->last_picture           = request->lastFrame ? H264_LAST_PICTURE_EOSEQ | H264_LAST_PICTURE_EOSTREAM : 0;
    picture->frame_num              = (uint16_t)encoder->nextFrameNum;
    picture->pic_init_qp            = (uint8_t)config->quantizationParameter;
    picture->chroma_qp_index_offset = config->chromaQpIndexOffset;
    picture->second_chroma_qp_index_offset                          = config->secondChromaQpIndexOffset;
    picture->pic_fields.bits.idr_pic_flag                           = idr_picture ? 1 : 0;
    picture->pic_fields.bits.reference_pic_flag                     = request->referencePicture ? 1 : 0;
    picture->pic_fields.bits.entropy_coding_mode_flag               = config->useCabac ? 1 : 0;
    picture->pic_fields.bits.weighted_pred_flag                     = config->useWeightedPrediction ? 1 : 0;
    picture->pic_fields.bits.weighted_bipred_idc                    = config->weightedBipredIdc;
    picture->pic_fields.bits.constrained_intra_pred_flag            = config->constrainedIntraPrediction ? 1 : 0;
    picture->pic_fields.bits.transform_8x8_mode_flag                = config->use8x8Transform ? 1 : 0;
    picture->pic_fields.bits.deblocking_filter_control_present_flag = DEBLOCKING_FILTER_CONTROL_PRESENT;
    picture->pic_fields.bits.redundant_pic_cnt_present_flag         = config->redundantPictures ? 1 : 0;
    picture->pic_fields.bits.pic_order_present_flag                 = config->picOrderPresent ? 1 : 0;
    picture->pic_fields.bits.pic_scaling_matrix_present_flag        = config->scalingListsPresent ? 1 : 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Caller weights cover only the active prefixes. Native constructors consume complete fixed arrays.
 */
static void InitializeSliceWeights(VAEncSliceParameterBufferH264*  slice,
                                   const Gfx_Encoder_EncodeInfo_t* request,
                                   const ReferenceLists_t*         lists)
{
    LOGASSERT(NULL != slice);
    LOGASSERT(NULL != request);
    LOGASSERT(NULL != lists);
    LOGASSERT(UTL_H264_MAX_REFERENCE_LIST_ENTRIES >= lists->count0);
    LOGASSERT(UTL_H264_MAX_REFERENCE_LIST_ENTRIES >= lists->count1);
    CRT_Memory_Set(slice, 0, sizeof(*slice));
    if (request->list0LumaWeightsEnabled)
    {
        LOGASSERT(0 < lists->count0);
        LOGASSERT(NULL != request->list0LumaWeights);
        LOGASSERT(NULL != request->list0LumaOffsets);
        CRT_Memory_Copy(slice->luma_weight_l0,
                        request->list0LumaWeights,
                        lists->count0 * sizeof(slice->luma_weight_l0[0]));
        CRT_Memory_Copy(slice->luma_offset_l0,
                        request->list0LumaOffsets,
                        lists->count0 * sizeof(slice->luma_offset_l0[0]));
    }
    else
    {
        LOGASSERT(NULL == request->list0LumaWeights);
        LOGASSERT(NULL == request->list0LumaOffsets);
    }
    if (request->list0ChromaWeightsEnabled)
    {
        LOGASSERT(0 < lists->count0);
        LOGASSERT(NULL != request->list0ChromaWeights);
        LOGASSERT(NULL != request->list0ChromaOffsets);
        CRT_Memory_Copy(slice->chroma_weight_l0,
                        request->list0ChromaWeights,
                        lists->count0 * sizeof(slice->chroma_weight_l0[0]));
        CRT_Memory_Copy(slice->chroma_offset_l0,
                        request->list0ChromaOffsets,
                        lists->count0 * sizeof(slice->chroma_offset_l0[0]));
    }
    else
    {
        LOGASSERT(NULL == request->list0ChromaWeights);
        LOGASSERT(NULL == request->list0ChromaOffsets);
    }
    if (request->list1LumaWeightsEnabled)
    {
        LOGASSERT(0 < lists->count1);
        LOGASSERT(NULL != request->list1LumaWeights);
        LOGASSERT(NULL != request->list1LumaOffsets);
        CRT_Memory_Copy(slice->luma_weight_l1,
                        request->list1LumaWeights,
                        lists->count1 * sizeof(slice->luma_weight_l1[0]));
        CRT_Memory_Copy(slice->luma_offset_l1,
                        request->list1LumaOffsets,
                        lists->count1 * sizeof(slice->luma_offset_l1[0]));
    }
    else
    {
        LOGASSERT(NULL == request->list1LumaWeights);
        LOGASSERT(NULL == request->list1LumaOffsets);
    }
    if (request->list1ChromaWeightsEnabled)
    {
        LOGASSERT(0 < lists->count1);
        LOGASSERT(NULL != request->list1ChromaWeights);
        LOGASSERT(NULL != request->list1ChromaOffsets);
        CRT_Memory_Copy(slice->chroma_weight_l1,
                        request->list1ChromaWeights,
                        lists->count1 * sizeof(slice->chroma_weight_l1[0]));
        CRT_Memory_Copy(slice->chroma_offset_l1,
                        request->list1ChromaOffsets,
                        lists->count1 * sizeof(slice->chroma_offset_l1[0]));
    }
    else
    {
        LOGASSERT(NULL == request->list1ChromaWeights);
        LOGASSERT(NULL == request->list1ChromaOffsets);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Active weight prefixes are copied into fixed native arrays before cva consumes the entire arrays.
 *  cva's initializer copies its inputs before publishing its output, allowing the retained slice
 *  arrays to supply its native representation without a second large stack object.
 */
static void InitializeSliceParameters(Gfx_Encoder_t*                  encoder,
                                      const Gfx_Encoder_EncodeInfo_t* request,
                                      uint32_t                        frameIndex,
                                      const ReferenceLists_t*         lists,
                                      uint32_t                        sliceIndex)
{
    const Config_t*                      config;
    VAEncSliceParameterBufferH264*       slice;
    const VAEncSliceParameterBufferH264* arrays;
    uint32_t                             width_in_mbs;
    uint32_t                             height_in_mbs;
    uint32_t                             padded_width;
    uint32_t                             padded_height;
    uint32_t                             total_macroblocks;
    uint32_t                             per_slice;
    uint32_t                             extra;
    uint32_t                             first;
    uint32_t                             count;
    uint32_t                             frame_range;
    uint32_t                             poc_range;
    uint32_t                             poc_mask;
    uint32_t                             order_count;
    uint32_t                             active_count0;
    uint32_t                             active_count1;
    uint32_t                             default_count0;
    uint32_t                             default_count1;
    int32_t                              quantization_parameter;
    bool_t                               extra_macroblock;
    bool_t                               idr_picture;
    bool_t                               intra_picture;
    bool_t                               regular_intra;
    bool_t                               predictive_picture;
    bool_t                               bidirectional_picture;
    bool_t                               override_list0;
    bool_t                               override_list1;
    bool_t                               override_counts;

    LOGASSERT(NULL != encoder);
    LOGASSERT(NULL != request);
    LOGASSERT(NULL != lists);
    config = &encoder->config;
    LOGASSERT(0 < config->sliceCount);
    LOGASSERT(GFX_ENCODER_MAX_SLICES >= config->sliceCount);
    LOGASSERT(sliceIndex < config->sliceCount);
    LOGASSERT(encoder->decodedPictureCount >= lists->count0);
    LOGASSERT(encoder->decodedPictureCount >= lists->count1);
    LOGASSERT(UTL_H264_MAX_REFERENCE_LIST_ENTRIES >= lists->count0);
    LOGASSERT(UTL_H264_MAX_REFERENCE_LIST_ENTRIES >= lists->count1);
    LOGASSERT(UTL_H264_MAX_WEIGHT_DENOMINATOR_LOG2 >= request->lumaLog2WeightDenominator);
    LOGASSERT(UTL_H264_MAX_WEIGHT_DENOMINATOR_LOG2 >= request->chromaLog2WeightDenominator);

    idr_picture           = UTL_H264_PICTURE_TYPE_IDR == request->pictureType;
    regular_intra         = UTL_H264_PICTURE_TYPE_I == request->pictureType;
    intra_picture         = idr_picture || regular_intra;
    predictive_picture    = UTL_H264_PICTURE_TYPE_P == request->pictureType;
    bidirectional_picture = UTL_H264_PICTURE_TYPE_B == request->pictureType;
    if (intra_picture)
    {
        LOGASSERT(0 == lists->count0);
        LOGASSERT(0 == lists->count1);
    }
    if (predictive_picture)
    {
        LOGASSERT(0 < lists->count0);
        LOGASSERT(0 == lists->count1);
    }
    if (bidirectional_picture)
    {
        LOGASSERT(0 < lists->count0);
        LOGASSERT(0 < lists->count1);
    }
    else
    {
        LOGASSERT(FALSE == request->directSpatialMotionVectorPrediction);
    }
    if (FALSE == config->useCabac)
    {
        LOGASSERT(0 == config->cabacInitIdc);
    }

    Utl_H264_DeriveCodedDimensions(config->width,
                                   config->height,
                                   config->interlaced,
                                   &width_in_mbs,
                                   &height_in_mbs,
                                   &padded_width,
                                   &padded_height);
    LOGASSERT(0 < height_in_mbs);
    LOGASSERT((UINT32_MAX / height_in_mbs) >= width_in_mbs);
    total_macroblocks = width_in_mbs * height_in_mbs;
    LOGASSERT(total_macroblocks >= config->sliceCount);

    per_slice              = total_macroblocks / config->sliceCount;
    extra                  = total_macroblocks % config->sliceCount;
    extra_macroblock       = sliceIndex < extra;
    first                  = sliceIndex * per_slice + (extra_macroblock ? sliceIndex : extra);
    count                  = per_slice + (extra_macroblock ? 1 : 0);
    quantization_parameter = (int32_t)config->quantizationParameter + request->quantizationParameterDelta;
    LOGASSERT(0 <= quantization_parameter);
    LOGASSERT(UTL_H264_MAX_QUANTIZATION_PARAMETER >= quantization_parameter);

    frame_range   = Utl_H264_DeriveMaximumFrameNumber(config->intraIdrPeriod);
    poc_range     = Utl_H264_DeriveMaximumPicOrderCntLsb(frame_range);
    poc_mask      = Utl_H264_DeriveValueMask(poc_range);
    order_count   = idr_picture ? 0 : Utl_H264_DeriveFieldOrderCount(frameIndex) & poc_mask;
    active_count0 = (0 < lists->count0) ? lists->count0 : 1;
    active_count1 = (0 < lists->count1) ? lists->count1 : 1;
    Utl_H264_DeriveActiveReferenceCounts(UTL_H264_SLICE_TYPE_I, 0, &default_count0, &default_count1);
    override_list0  = default_count0 != active_count0;
    override_list1  = default_count1 != active_count1;
    override_counts = override_list0 || override_list1;
    LOGASSERT(UINT16_MAX >= order_count);

    slice  = &encoder->slice[sliceIndex];
    arrays = slice;
    InitializeSliceWeights(slice, request, lists);

    cvaInitEncSliceParameterBufferH264(slice,
                                       first,
                                       count,
                                       VA_INVALID_ID,
                                       Utl_H264_DeriveSliceType(request->pictureType),
                                       0,
                                       idr_picture ? encoder->idrPicId : 0,
                                       (uint16_t)order_count,
                                       0,
                                       arrays->delta_pic_order_cnt,
                                       request->directSpatialMotionVectorPrediction ? 1 : 0,
                                       override_counts ? 1 : 0,
                                       (uint8_t)(active_count0 - 1),
                                       (uint8_t)(active_count1 - 1),
                                       lists->list0,
                                       lists->list1,
                                       request->lumaLog2WeightDenominator,
                                       request->chromaLog2WeightDenominator,
                                       request->list0LumaWeightsEnabled ? 1 : 0,
                                       arrays->luma_weight_l0,
                                       arrays->luma_offset_l0,
                                       request->list0ChromaWeightsEnabled ? 1 : 0,
                                       arrays->chroma_weight_l0,
                                       arrays->chroma_offset_l0,
                                       request->list1LumaWeightsEnabled ? 1 : 0,
                                       arrays->luma_weight_l1,
                                       arrays->luma_offset_l1,
                                       request->list1ChromaWeightsEnabled ? 1 : 0,
                                       arrays->chroma_weight_l1,
                                       arrays->chroma_offset_l1,
                                       config->cabacInitIdc,
                                       request->quantizationParameterDelta,
                                       config->enableDeblocking ? 0 : 1,
                                       config->deblockingAlphaOffsetDiv2,
                                       config->deblockingBetaOffsetDiv2);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Captures caller configuration before any encoder resources are allocated.
 */
static void InitializeConfig(Config_t* config, const Gfx_Encoder_CreateInfo_t* createInfo)
{
    ValidateCreateInfo(createInfo);

    config->width                            = createInfo->surface->width;
    config->height                           = createInfo->surface->height;
    config->h264Profile                      = createInfo->h264Profile;
    config->entrypoint                       = createInfo->entrypoint;
    config->frameRateNumerator               = createInfo->frameRateNumerator;
    config->frameRateDenominator             = createInfo->frameRateDenominator;
    config->intraPeriod                      = createInfo->intraPeriod;
    config->intraIdrPeriod                   = createInfo->intraIdrPeriod;
    config->ipPeriod                         = createInfo->ipPeriod;
    config->sliceCount                       = createInfo->sliceCount;
    config->targetBitRate                    = createInfo->targetBitRate;
    config->maximumBitRate                   = createInfo->maximumBitRate;
    config->rateControl                      = createInfo->rateControl;
    config->macroblockRateControl            = createInfo->macroblockRateControl;
    config->parallelRateControl              = createInfo->parallelRateControl;
    config->rateControlWindowMilliseconds    = createInfo->rateControlWindowMilliseconds;
    config->initialQuantizationParameter     = createInfo->initialQuantizationParameter;
    config->minimumQuantizationParameter     = createInfo->minimumQuantizationParameter;
    config->maximumQuantizationParameter     = createInfo->maximumQuantizationParameter;
    config->basicUnitSize                    = createInfo->basicUnitSize;
    config->resetRateControl                 = createInfo->resetRateControl;
    config->disableFrameSkip                 = createInfo->disableFrameSkip;
    config->disableBitStuffing               = createInfo->disableBitStuffing;
    config->macroblockRateControlMode        = createInfo->macroblockRateControlMode;
    config->temporalLayer                    = createInfo->temporalLayer;
    config->constantFrameSizeIntraFrames     = createInfo->constantFrameSizeIntraFrames;
    config->enableDynamicScaling             = createInfo->enableDynamicScaling;
    config->frameToleranceMode               = createInfo->frameToleranceMode;
    config->intelligentConstantQualityFactor = createInfo->intelligentConstantQualityFactor;
    config->qualityFactor                    = createInfo->qualityFactor;
    config->targetFrameSize                  = createInfo->targetFrameSize;
    config->packedHeaders                    = createInfo->packedHeaders;
    config->maxReferenceFrames               = createInfo->maxReferenceFrames;
    config->quantizationParameter            = createInfo->quantizationParameter;
    config->interlaced                       = createInfo->interlaced;
    config->mbAdaptiveFrameField             = createInfo->mbAdaptiveFrameField;
    config->fixedFrameRate                   = createInfo->fixedFrameRate;
    config->sampleAspectRatioWidth           = createInfo->sampleAspectRatioWidth;
    config->sampleAspectRatioHeight          = createInfo->sampleAspectRatioHeight;
    config->restrictMotionVectors            = createInfo->restrictMotionVectors;
    config->maxMotionVectorLengthHorizontal  = createInfo->maxMotionVectorLengthHorizontal;
    config->maxMotionVectorLengthVertical    = createInfo->maxMotionVectorLengthVertical;
    config->useCabac                         = createInfo->useCabac;
    config->useWeightedPrediction            = createInfo->useWeightedPrediction;
    config->weightedBipredIdc                = createInfo->weightedBipredIdc;
    config->constrainedIntraPrediction       = createInfo->constrainedIntraPrediction;
    config->use8x8Transform                  = createInfo->use8x8Transform;
    config->redundantPictures                = createInfo->redundantPictures;
    config->picOrderPresent                  = createInfo->picOrderPresent;
    config->scalingListsPresent              = NULL != createInfo->scalingList4x4;
    config->chromaQpIndexOffset              = createInfo->chromaQpIndexOffset;
    config->secondChromaQpIndexOffset        = createInfo->secondChromaQpIndexOffset;
    config->cabacInitIdc                     = createInfo->cabacInitIdc;
    config->enableDeblocking                 = createInfo->enableDeblocking;
    config->deblockingAlphaOffsetDiv2        = createInfo->deblockingAlphaOffsetDiv2;
    config->deblockingBetaOffsetDiv2         = createInfo->deblockingBetaOffsetDiv2;

    if (config->scalingListsPresent)
    {
        CRT_Memory_Copy(config->scalingList4x4, createInfo->scalingList4x4, sizeof(config->scalingList4x4));
        CRT_Memory_Copy(config->scalingList8x8, createInfo->scalingList8x8, sizeof(config->scalingList8x8));
    }
    else
    {
        CRT_Memory_Set(config->scalingList4x4, 0, sizeof(config->scalingList4x4));
        CRT_Memory_Set(config->scalingList8x8, 0, sizeof(config->scalingList8x8));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The encoder owns one decoding-order frame number and a separate display-order POC per reference.
 */
static uint32_t GetDecodedPictureFlags(const Gfx_Encoder_t* encoder, uint32_t index)
{
    uint32_t flags;
    bool_t   top_field;
    bool_t   bottom_field;
    bool_t   both_fields;

    LOGASSERT(NULL != encoder);
    LOGASSERT(index < encoder->decodedPictureCount);
    LOGASSERT(UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES >= encoder->decodedPictureCount);

    top_field    = encoder->decodedPictureTopFields[index];
    bottom_field = encoder->decodedPictureBottomFields[index];
    both_fields  = top_field && bottom_field;
    LOGASSERT(FALSE == both_fields);

    flags = VA_PICTURE_H264_SHORT_TERM_REFERENCE;
    if (encoder->decodedPictureLongTermReferences[index])
    {
        flags = VA_PICTURE_H264_LONG_TERM_REFERENCE;
    }
    if (top_field)
    {
        flags |= VA_PICTURE_H264_TOP_FIELD;
    }
    if (bottom_field)
    {
        flags |= VA_PICTURE_H264_BOTTOM_FIELD;
    }
    if (encoder->decodedPictureNonExisting[index])
    {
        LOGASSERT(VA_INVALID_SURFACE == encoder->decodedPictureSurfaces[index]);
        flags |= VA_PICTURE_H264_NON_EXISTING;
    }
    else
    {
        LOGASSERT(VA_INVALID_SURFACE != encoder->decodedPictureSurfaces[index]);
    }
    return flags;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Retains the original sample-storage estimate plus SPS/PPS reserve; encoded overflow is a completion failure.
 */
static uint32_t GetCodedBufferCapacity(const Config_t* config, uint32_t format)
{
    uint32_t width_in_mbs;
    uint32_t height_in_mbs;
    uint32_t padded_width;
    uint32_t padded_height;
    uint64_t pixel_count;
    uint64_t size;

    LOGASSERT(NULL != config);
    size = 0;
    Utl_H264_DeriveCodedDimensions(config->width,
                                   config->height,
                                   config->interlaced,
                                   &width_in_mbs,
                                   &height_in_mbs,
                                   &padded_width,
                                   &padded_height);
    pixel_count = (uint64_t)padded_width * padded_height;
    LOGASSERT(pixel_count <= UINT32_MAX);

    switch (format)
    {
    case VA_RT_FORMAT_YUV400:
        size = pixel_count;
        break;
    case VA_RT_FORMAT_YUV420:
    case VA_RT_FORMAT_YUV411:
        size = pixel_count * 3 / 2;
        break;
    case VA_RT_FORMAT_YUV422:
    case VA_RT_FORMAT_RGB16:
        size = pixel_count * 2;
        break;
    case VA_RT_FORMAT_YUV444:
    case VA_RT_FORMAT_YUV420_10:
    case VA_RT_FORMAT_YUV420_12:
    case VA_RT_FORMAT_RGBP:
        size = pixel_count * 3;
        break;
    case VA_RT_FORMAT_YUV422_10:
    case VA_RT_FORMAT_YUV422_12:
    case VA_RT_FORMAT_RGB32:
    case VA_RT_FORMAT_RGB32_10:
        size = pixel_count * 4;
        break;
    case VA_RT_FORMAT_YUV444_10:
    case VA_RT_FORMAT_YUV444_12:
        size = pixel_count * 6;
        break;
    case VA_RT_FORMAT_PROTECTED:
    default:
        OS_AssertFailure("Supported coded-surface format", __FILE__, __LINE__);
    }

    size += 2 * ENCODER_PARAMETER_SET_CAPACITY;
    LOGASSERT(size <= UINT32_MAX);
    return (uint32_t)size;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ValidateCreateInfo(const Gfx_Encoder_CreateInfo_t* createInfo)
{
    uint64_t macroblock_count;
    uint32_t width_in_mbs;
    uint32_t height_in_mbs;
    uint32_t padded_width;
    uint32_t padded_height;

    LOGASSERT(NULL != createInfo->surface);
    LOGASSERT(GFX_SURFACE_KIND_STORAGE == createInfo->surface->kind);
    LOGASSERT(NULL != createInfo->surface->va.dpy);
    LOGASSERT(NULL != createInfo->surface->target.readImage);
    LOGASSERT(VA_INVALID_SURFACE != createInfo->surface->target.readImage->va.surfaceId);
    LOGASSERT(createInfo->h264Profile >= GFX_ENCODER_H264_PROFILE_AUTO &&
              createInfo->h264Profile <= GFX_ENCODER_H264_PROFILE_HIGH);
    LOGASSERT(createInfo->entrypoint >= GFX_ENCODER_ENTRYPOINT_SLICE &&
              createInfo->entrypoint <= GFX_ENCODER_ENTRYPOINT_AUTO);
    LOGASSERT(0 != createInfo->frameRateNumerator);
    LOGASSERT(0 != createInfo->frameRateDenominator);
    LOGASSERT(createInfo->frameRateNumerator <= UINT16_MAX);
    LOGASSERT(createInfo->frameRateDenominator <= UINT16_MAX);
    LOGASSERT(createInfo->rateControl >= GFX_ENCODER_RATE_CONTROL_NONE &&
              createInfo->rateControl <= GFX_ENCODER_RATE_CONTROL_TRANSPORT_CONTROLLED_BIT_RATE);
    LOGASSERT(0 ==
              (createInfo->packedHeaders & ~(GFX_ENCODER_PACKED_HEADER_SEQUENCE | GFX_ENCODER_PACKED_HEADER_PICTURE)));
    LOGASSERT(createInfo->targetBitRate <= createInfo->maximumBitRate);
    LOGASSERT((0 == createInfo->targetBitRate) == (0 == createInfo->maximumBitRate));
    LOGASSERT(createInfo->macroblockRateControlMode <= 2);
    LOGASSERT(createInfo->frameToleranceMode <= 2);

    switch (createInfo->rateControl)
    {
    case GFX_ENCODER_RATE_CONTROL_CONSTANT_BIT_RATE:
        LOGASSERT(0 != createInfo->targetBitRate);
        LOGASSERT(createInfo->targetBitRate == createInfo->maximumBitRate);

        break;

    case GFX_ENCODER_RATE_CONTROL_VARIABLE_BIT_RATE:
    case GFX_ENCODER_RATE_CONTROL_VIDEO_CONFERENCE:
    case GFX_ENCODER_RATE_CONTROL_CONSTRAINED_VARIABLE_BIT_RATE:
    case GFX_ENCODER_RATE_CONTROL_QUALITY_VARIABLE_BIT_RATE:
    case GFX_ENCODER_RATE_CONTROL_AVERAGE_VARIABLE_BIT_RATE:
    case GFX_ENCODER_RATE_CONTROL_TRANSPORT_CONTROLLED_BIT_RATE:
        LOGASSERT(0 != createInfo->targetBitRate);
        LOGASSERT(0 != createInfo->maximumBitRate);

        break;

    case GFX_ENCODER_RATE_CONTROL_NONE:
    case GFX_ENCODER_RATE_CONTROL_CONSTANT_QUANTIZATION_PARAMETER:
    case GFX_ENCODER_RATE_CONTROL_INTELLIGENT_CONSTANT_QUALITY:
    case GFX_ENCODER_RATE_CONTROL_CONSTANT_FRAME_SIZE:
        LOGASSERT(0 == createInfo->targetBitRate);
        LOGASSERT(0 == createInfo->maximumBitRate);

        break;

    default:
        OS_AssertFailure("Unsupported encoder rate-control mode!", __FILE__, __LINE__);
    }
    LOGASSERT(0 != createInfo->ipPeriod);
    LOGASSERT(0 != createInfo->sliceCount);
    LOGASSERT(createInfo->sliceCount <= GFX_ENCODER_MAX_SLICES);

    Utl_H264_DeriveCodedDimensions(createInfo->surface->width,
                                   createInfo->surface->height,
                                   createInfo->interlaced,
                                   &width_in_mbs,
                                   &height_in_mbs,
                                   &padded_width,
                                   &padded_height);
    macroblock_count = (uint64_t)width_in_mbs * height_in_mbs;
    LOGASSERT(createInfo->sliceCount <= macroblock_count);
    LOGASSERT(width_in_mbs <= UINT16_MAX && height_in_mbs <= UINT16_MAX);
    LOGASSERT((uint64_t)createInfo->sliceCount * sizeof(VAEncSliceParameterBufferH264) <= UINT32_MAX);
    LOGASSERT(createInfo->intraIdrPeriod <= UINT16_MAX);
    LOGASSERT(createInfo->maxReferenceFrames <= UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES);
    LOGASSERT(UTL_H264_MAX_QUANTIZATION_PARAMETER >= createInfo->quantizationParameter);
    LOGASSERT(UTL_H264_MAX_QUANTIZATION_PARAMETER >= createInfo->initialQuantizationParameter);
    LOGASSERT(UTL_H264_MAX_QUANTIZATION_PARAMETER >= createInfo->minimumQuantizationParameter);
    LOGASSERT(UTL_H264_MAX_QUANTIZATION_PARAMETER >= createInfo->maximumQuantizationParameter);
    LOGASSERT(FALSE != createInfo->interlaced || FALSE == createInfo->mbAdaptiveFrameField);
    LOGASSERT((NULL == createInfo->scalingList4x4 && NULL == createInfo->scalingList8x8) ||
              (NULL != createInfo->scalingList4x4 && NULL != createInfo->scalingList8x8));
    LOGASSERT(
        NULL == createInfo->scalingList4x4 ||
        0 == (createInfo->packedHeaders & (GFX_ENCODER_PACKED_HEADER_SEQUENCE | GFX_ENCODER_PACKED_HEADER_PICTURE)));
    LOGASSERT((0 == createInfo->sampleAspectRatioWidth && 0 == createInfo->sampleAspectRatioHeight) ||
              (0 != createInfo->sampleAspectRatioWidth && 0 != createInfo->sampleAspectRatioHeight));
    LOGASSERT((FALSE == createInfo->restrictMotionVectors && 0 == createInfo->maxMotionVectorLengthHorizontal &&
               0 == createInfo->maxMotionVectorLengthVertical) ||
              (FALSE != createInfo->restrictMotionVectors && 0 != createInfo->maxMotionVectorLengthHorizontal &&
               0 != createInfo->maxMotionVectorLengthVertical));
    LOGASSERT(createInfo->weightedBipredIdc <= UTL_H264_MAX_WEIGHTED_BIPRED_IDC);
    LOGASSERT(createInfo->chromaQpIndexOffset >= UTL_H264_MIN_CHROMA_QP_INDEX_OFFSET &&
              createInfo->chromaQpIndexOffset <= UTL_H264_MAX_CHROMA_QP_INDEX_OFFSET);
    LOGASSERT(createInfo->secondChromaQpIndexOffset >= UTL_H264_MIN_CHROMA_QP_INDEX_OFFSET &&
              createInfo->secondChromaQpIndexOffset <= UTL_H264_MAX_CHROMA_QP_INDEX_OFFSET);
    LOGASSERT(createInfo->cabacInitIdc <= UTL_H264_MAX_CABAC_INIT_IDC);
    LOGASSERT(FALSE != createInfo->useCabac || 0 == createInfo->cabacInitIdc);
    LOGASSERT(createInfo->deblockingAlphaOffsetDiv2 >= UTL_H264_MIN_DEBLOCKING_FILTER_OFFSET_DIV2 &&
              createInfo->deblockingAlphaOffsetDiv2 <= UTL_H264_MAX_DEBLOCKING_FILTER_OFFSET_DIV2);
    LOGASSERT(createInfo->deblockingBetaOffsetDiv2 >= UTL_H264_MIN_DEBLOCKING_FILTER_OFFSET_DIV2 &&
              createInfo->deblockingBetaOffsetDiv2 <= UTL_H264_MAX_DEBLOCKING_FILTER_OFFSET_DIV2);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint32_t GetVaapiRateControl(Gfx_Encoder_RateControl_t rateControl,
                                    bool_t                    macroblockRateControl,
                                    bool_t                    parallelRateControl)
{
    uint32_t rate_control;

    switch (rateControl)
    {
    case GFX_ENCODER_RATE_CONTROL_NONE:
        rate_control = VA_RC_NONE;
        break;
    case GFX_ENCODER_RATE_CONTROL_CONSTANT_BIT_RATE:
        rate_control = VA_RC_CBR;
        break;
    case GFX_ENCODER_RATE_CONTROL_VARIABLE_BIT_RATE:
        rate_control = VA_RC_VBR;
        break;
    case GFX_ENCODER_RATE_CONTROL_VIDEO_CONFERENCE:
        rate_control = VA_RC_VCM;
        break;
    case GFX_ENCODER_RATE_CONTROL_CONSTANT_QUANTIZATION_PARAMETER:
        rate_control = VA_RC_CQP;
        break;
    case GFX_ENCODER_RATE_CONTROL_CONSTRAINED_VARIABLE_BIT_RATE:
        rate_control = VA_RC_VBR_CONSTRAINED;
        break;
    case GFX_ENCODER_RATE_CONTROL_INTELLIGENT_CONSTANT_QUALITY:
        rate_control = VA_RC_ICQ;
        break;
    case GFX_ENCODER_RATE_CONTROL_CONSTANT_FRAME_SIZE:
        rate_control = VA_RC_CFS;
        break;
    case GFX_ENCODER_RATE_CONTROL_QUALITY_VARIABLE_BIT_RATE:
        rate_control = VA_RC_QVBR;
        break;
    case GFX_ENCODER_RATE_CONTROL_AVERAGE_VARIABLE_BIT_RATE:
        rate_control = VA_RC_AVBR;
        break;
    case GFX_ENCODER_RATE_CONTROL_TRANSPORT_CONTROLLED_BIT_RATE:
        rate_control = VA_RC_TCBRC;
        break;
    default:
        OS_AssertFailure("Unsupported VA rate-control mode!", __FILE__, __LINE__);
    }
    if (macroblockRateControl)
    {
        rate_control |= VA_RC_MB;
    }
    if (parallelRateControl)
    {
        rate_control |= VA_RC_PARALLEL;
    }

    return rate_control;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint32_t GetVaapiPackedHeaders(uint32_t packedHeaders)
{
    uint32_t packed_headers;

    packed_headers = VA_ENC_PACKED_HEADER_NONE;

    if (packedHeaders & GFX_ENCODER_PACKED_HEADER_SEQUENCE)
    {
        packed_headers |= VA_ENC_PACKED_HEADER_SEQUENCE;
    }
    if (packedHeaders & GFX_ENCODER_PACKED_HEADER_PICTURE)
    {
        packed_headers |= VA_ENC_PACKED_HEADER_PICTURE;
    }

    return packed_headers;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static int32_t GetVaapiScanMode(bool_t interlaced)
{
    if (interlaced)
    {
        return 0;
    }
    else
    {
        return VA_PROGRESSIVE;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  AUTO permits profile selection; explicit requests map to one VA profile.
 */
static VAProfile GetVaapiProfile(Gfx_Encoder_H264Profile_t profile)
{
    switch (profile)
    {
    case GFX_ENCODER_H264_PROFILE_AUTO:
        return VAProfileNone;
    case GFX_ENCODER_H264_PROFILE_CONSTRAINED_BASELINE:
        return VAProfileH264ConstrainedBaseline;
    case GFX_ENCODER_H264_PROFILE_MAIN:
        return VAProfileH264Main;
    case GFX_ENCODER_H264_PROFILE_HIGH:
        return VAProfileH264High;
    default:
        OS_AssertFailure("Unsupported H.264 encoder profile!", __FILE__, __LINE__);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Constrained Baseline is profile_idc 66 with constraint_set1_flag set (Annex A.2.1.1).
 */
static void GetH264Profile(VAProfile profile, uint8_t* profileIdc, uint8_t* constraintSetFlags)
{
    switch (profile)
    {
    case VAProfileH264Main:
        (*profileIdc)         = UTL_H264_PROFILE_MAIN;
        (*constraintSetFlags) = 0;
        break;
    case VAProfileH264High:
        (*profileIdc)         = UTL_H264_PROFILE_HIGH;
        (*constraintSetFlags) = 0;
        break;
    case VAProfileH264ConstrainedBaseline:
        (*profileIdc)         = UTL_H264_PROFILE_BASELINE;
        (*constraintSetFlags) = UTL_H264_CONSTRAINT_SET1_FLAG;
        break;
    case VAProfileNone:
    case VAProfileMPEG2Simple:
    case VAProfileMPEG2Main:
    case VAProfileMPEG4Simple:
    case VAProfileMPEG4AdvancedSimple:
    case VAProfileMPEG4Main:
    case VAProfileVC1Simple:
    case VAProfileVC1Main:
    case VAProfileVC1Advanced:
    case VAProfileH263Baseline:
    case VAProfileJPEGBaseline:
    case VAProfileVP8Version0_3:
    case VAProfileH264MultiviewHigh:
    case VAProfileH264StereoHigh:
    case VAProfileHEVCMain:
    case VAProfileHEVCMain10:
    case VAProfileVP9Profile0:
    case VAProfileVP9Profile1:
    case VAProfileVP9Profile2:
    case VAProfileVP9Profile3:
    case VAProfileHEVCMain12:
    case VAProfileHEVCMain422_10:
    case VAProfileHEVCMain422_12:
    case VAProfileHEVCMain444:
    case VAProfileHEVCMain444_10:
    case VAProfileHEVCMain444_12:
    case VAProfileHEVCSccMain:
    case VAProfileHEVCSccMain10:
    case VAProfileHEVCSccMain444:
    case VAProfileAV1Profile0:
    case VAProfileAV1Profile1:
    case VAProfileHEVCSccMain444_10:
    case VAProfileProtected:
    case VAProfileH264High10:
    case VAProfileVVCMain10:
    case VAProfileVVCMultilayerMain10:
    case VAProfileAV1Profile2:
    case VAProfileH264High422:
    default:
        OS_AssertFailure("Unsupported H.264 encoder profile!", __FILE__, __LINE__);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static VAEntrypoint GetVaapiEntrypoint(Gfx_Encoder_Entrypoint_t entrypoint)
{
    switch (entrypoint)
    {
    case GFX_ENCODER_ENTRYPOINT_SLICE:
        return VAEntrypointEncSlice;
    case GFX_ENCODER_ENTRYPOINT_LOW_POWER_SLICE:
    case GFX_ENCODER_ENTRYPOINT_AUTO:
        return VAEntrypointEncSliceLP;
    default:
        OS_AssertFailure("Unsupported encoder entrypoint!", __FILE__, __LINE__);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static VAEncPictureType GetVaapiPictureTypeH264(Utl_H264_PictureType_t pictureType, bool_t* idrPicture)
{
    LOGASSERT(NULL != idrPicture);

    switch (pictureType)
    {
    case UTL_H264_PICTURE_TYPE_IDR:
        (*idrPicture) = TRUE;
        return VAEncPictureTypeIntra;
    case UTL_H264_PICTURE_TYPE_I:
        (*idrPicture) = FALSE;
        return VAEncPictureTypeIntra;
    case UTL_H264_PICTURE_TYPE_P:
        (*idrPicture) = FALSE;
        return VAEncPictureTypePredictive;
    case UTL_H264_PICTURE_TYPE_B:
        (*idrPicture) = FALSE;
        return VAEncPictureTypeBidirectional;
    default:
        OS_AssertFailure("Unsupported H.264 picture type!", __FILE__, __LINE__);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static VAProfile SelectVaapiProfile(VADisplay                dpy,
                                    VAProfile                requestedProfile,
                                    Gfx_Encoder_Entrypoint_t requestedEntrypoint,
                                    VAEntrypoint             preferredEntrypoint,
                                    uint8_t                  sliceType,
                                    bool_t                   interlaced,
                                    bool_t                   useCabac,
                                    bool_t                   use8x8Transform,
                                    bool_t                   scalingListsPresent,
                                    bool_t                   useWeightedPrediction,
                                    uint8_t                  weightedBipredIdc,
                                    bool_t                   redundantPictures,
                                    int8_t                   chromaQpIndexOffset,
                                    int8_t                   secondChromaQpIndexOffset,
                                    VAEntrypoint*            entrypoint,
                                    uint8_t*                 profileIdc,
                                    uint8_t*                 constraintSetFlags)
{
    uint32_t       profile_count;
    uint32_t       profile_index;
    uint8_t        profile_idc;
    uint8_t        constraint_set_flags;
    VAProfile      candidate_profile;
    VAProfile      selected_profile;
    VAProfile      fallback_profile;
    bool_t         profile_coding_compatible;
    bool_t         entrypoint_supported;
    VAConfigAttrib references;
    uint32_t       max_list1_references;

    selected_profile = VAProfileNone;
    fallback_profile = VAProfileNone;
    (*entrypoint)    = VAEntrypointEncSlice;

    if (VAProfileNone == requestedProfile)
    {
        // Only AUTO may move through the preferred profile list.
        profile_count = PROFILE_CANDIDATE_CAPACITY;
    }
    else
    {
        profile_count = 1;
    }

    for (profile_index = 0; profile_index < profile_count; ++profile_index)
    {
        candidate_profile = requestedProfile;
        if (VAProfileNone == requestedProfile)
        {
            switch (profile_index)
            {
            case 0:
                candidate_profile = VAProfileH264High;
                break;
            case 1:
                candidate_profile = VAProfileH264Main;
                break;
            default:
                candidate_profile = VAProfileH264ConstrainedBaseline;
                break;
            }
        }
        GetH264Profile(candidate_profile, &profile_idc, &constraint_set_flags);
        profile_coding_compatible = Utl_H264_IsProfileCodingCompatible(profile_idc,
                                                                       constraint_set_flags,
                                                                       sliceType,
                                                                       !interlaced,
                                                                       useCabac,
                                                                       use8x8Transform,
                                                                       scalingListsPresent,
                                                                       useWeightedPrediction,
                                                                       weightedBipredIdc,
                                                                       redundantPictures,
                                                                       chromaQpIndexOffset,
                                                                       secondChromaQpIndexOffset);
        if (profile_coding_compatible)
        {
            entrypoint_supported = (0 != cvaGetConfigSupport(dpy, candidate_profile, preferredEntrypoint));
            if (entrypoint_supported && (UTL_H264_SLICE_TYPE_B == sliceType))
            {
                // Profile support alone does not guarantee list-1 references for B pictures.
                references.type  = VAConfigAttribEncMaxRefFrames;
                references.value = 0;
                cvaGetConfigAttributes(dpy, candidate_profile, preferredEntrypoint, &references, 1);
                max_list1_references = references.value >> MAX_LIST1_REFERENCES_SHIFT;
                entrypoint_supported = (VA_ATTRIB_NOT_SUPPORTED != references.value) && (0 != max_list1_references);
            }
            if (entrypoint_supported)
            {
                selected_profile      = candidate_profile;
                (*profileIdc)         = profile_idc;
                (*constraintSetFlags) = constraint_set_flags;
                (*entrypoint)         = preferredEntrypoint;
                break;
            }
            else if (GFX_ENCODER_ENTRYPOINT_AUTO == requestedEntrypoint)
            {
                if (VAProfileNone == fallback_profile)
                {
                    // Keep the first full-featured candidate while looking for a low-power match.
                    entrypoint_supported = (0 != cvaGetConfigSupport(dpy, candidate_profile, VAEntrypointEncSlice));
                    if (entrypoint_supported && (UTL_H264_SLICE_TYPE_B == sliceType))
                    {
                        references.type  = VAConfigAttribEncMaxRefFrames;
                        references.value = 0;
                        cvaGetConfigAttributes(dpy, candidate_profile, VAEntrypointEncSlice, &references, 1);
                        max_list1_references = references.value >> MAX_LIST1_REFERENCES_SHIFT;
                        entrypoint_supported =
                            (VA_ATTRIB_NOT_SUPPORTED != references.value) && (0 != max_list1_references);
                    }
                    if (entrypoint_supported)
                    {
                        fallback_profile      = candidate_profile;
                        (*profileIdc)         = profile_idc;
                        (*constraintSetFlags) = constraint_set_flags;
                    }
                }
            }
        }
    }
    if ((GFX_ENCODER_ENTRYPOINT_AUTO == requestedEntrypoint) && (VAProfileNone == selected_profile))
    {
        selected_profile = fallback_profile;
    }
    LOGASSERT(VAProfileNone != selected_profile);

    return selected_profile;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static int32_t SelectVaapiConfig(VADisplay                 dpy,
                                 VAProfile                 profile,
                                 VAEntrypoint              entrypoint,
                                 uint32_t                  format,
                                 Gfx_Encoder_RateControl_t rateControl,
                                 bool_t                    macroblockRateControl,
                                 bool_t                    parallelRateControl,
                                 uint32_t                  packedHeaders,
                                 VAConfigAttrib*           attributes)
{
    VAConfigAttrib supported[CONFIG_ATTRIBUTE_CAPACITY];
    uint32_t       index;
    int32_t        attribute_count;

    attributes[0].type  = VAConfigAttribRTFormat;
    attributes[0].value = format;
    attributes[1].type  = VAConfigAttribRateControl;
    attributes[1].value = GetVaapiRateControl(rateControl, macroblockRateControl, parallelRateControl);
    attributes[2].type  = VAConfigAttribEncPackedHeaders;
    attributes[2].value = GetVaapiPackedHeaders(packedHeaders);
    CRT_Memory_Copy(supported, attributes, sizeof(supported));
    // Query into a copy: the original values describe the exact requested feature masks.
    cvaGetConfigAttributes(dpy, profile, entrypoint, supported, (int32_t)COUNT_OF(supported));
    attribute_count = 0;
    for (index = 0; index < COUNT_OF(supported); ++index)
    {
        if (VA_ATTRIB_NOT_SUPPORTED == supported[index].value)
        {
            // An unavailable optional attribute may be omitted only when no bits were requested.
            LOGASSERT(0 == attributes[index].value);
            continue;
        }
        LOGASSERT(attributes[index].value == (supported[index].value & attributes[index].value));
        // Compact the supported requests for the subsequent vaCreateConfig call.
        attributes[attribute_count++] = attributes[index];
    }

    return attribute_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static bool_t IsDecodedPictureSurface(const VASurfaceID* decodedPictureSurfaces,
                                      uint32_t           decodedPictureCount,
                                      VASurfaceID        surfaceId)
{
    uint32_t decoded_picture_index;

    for (decoded_picture_index = 0; decoded_picture_index < decodedPictureCount; ++decoded_picture_index)
    {
        if (surfaceId == decodedPictureSurfaces[decoded_picture_index])
        {
            return TRUE;
        }
    }
    return FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static VASurfaceID GetReconstructionSurfaceId(const VASurfaceID* reconstructionSurfaces,
                                              uint32_t           reconstructionSurfaceCount,
                                              const VASurfaceID* decodedPictureSurfaces,
                                              uint32_t           decodedPictureCount)
{
    uint32_t reconstruction_surface_index;
    bool_t   surface_in_use;

    for (reconstruction_surface_index = 0; reconstruction_surface_index < reconstructionSurfaceCount;
         ++reconstruction_surface_index)
    {
        surface_in_use = IsDecodedPictureSurface(decodedPictureSurfaces,
                                                 decodedPictureCount,
                                                 reconstructionSurfaces[reconstruction_surface_index]);
        if (!surface_in_use)
        {
            LOGASSERT(VA_INVALID_SURFACE != reconstructionSurfaces[reconstruction_surface_index]);
            return reconstructionSurfaces[reconstruction_surface_index];
        }
    }
    LOGASSERT(FALSE);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint32_t BuildReferenceListH264(const Gfx_Encoder_t* encoder,
                                       uint32_t             frameIndex,
                                       bool_t               futureReferences,
                                       VAPictureH264*       referenceList)
{
    bool_t   has_requested_direction;
    bool_t   is_earlier;
    bool_t   is_later;
    bool_t   matches_direction;
    bool_t   missing_direction;
    bool_t   use_fallback;
    bool_t   select_future;
    uint32_t decoded_picture_index;
    uint64_t reference_count;
    uint32_t flags;
    uint32_t order_count;
    uint64_t reference_indices[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];
    uint64_t reference_index;
    uint64_t selected_index;

    LOGASSERT(NULL != encoder);
    LOGASSERT(NULL != referenceList);
    LOGASSERT(UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES >= encoder->decodedPictureCount);
    has_requested_direction = FALSE;

    for (decoded_picture_index = 0; decoded_picture_index < encoder->decodedPictureCount; ++decoded_picture_index)
    {
        is_earlier              = encoder->decodedPictureFrameIndices[decoded_picture_index] < frameIndex;
        is_later                = encoder->decodedPictureFrameIndices[decoded_picture_index] > frameIndex;
        matches_direction       = futureReferences ? is_later : is_earlier;
        has_requested_direction = has_requested_direction || matches_direction;
    }
    missing_direction = FALSE == has_requested_direction;
    use_fallback      = missing_direction && futureReferences;
    select_future     = futureReferences;
    if (use_fallback)
    {
        select_future = FALSE;
    }
    reference_count = Utl_H264_DeriveReferenceListIndices(frameIndex,
                                                          encoder->decodedPictureFrameIndices,
                                                          encoder->decodedPictureCount,
                                                          select_future,
                                                          reference_indices);
    LOGASSERT(UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES >= reference_count);
    for (reference_index = 0; reference_index < reference_count; ++reference_index)
    {
        selected_index = reference_indices[reference_index];
        LOGASSERT(encoder->decodedPictureCount > selected_index);
        flags       = GetDecodedPictureFlags(encoder, (uint32_t)selected_index);
        order_count = Utl_H264_DeriveFieldOrderCount(encoder->decodedPictureFrameIndices[selected_index]);
        LOGASSERT((INT32_MAX - 1) >= order_count);
        cvaInitPictureH264(&referenceList[reference_index],
                           encoder->decodedPictureSurfaces[selected_index],
                           encoder->decodedPictureFrameNumbers[selected_index],
                           flags,
                           (int32_t)order_count,
                           (int32_t)order_count + 1);
    }

    return (uint32_t)reference_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ValidatePredictionWeightsH264(const Config_t*                 config,
                                          const Gfx_Encoder_EncodeInfo_t* encodeInfo,
                                          VAEncPictureType                pictureType)
{
    if (encodeInfo->list0LumaWeightsEnabled || encodeInfo->list0ChromaWeightsEnabled)
    {
        if (VAEncPictureTypePredictive == pictureType)
        {
            LOGASSERT(config->useWeightedPrediction);
        }
        else
        {
            LOGASSERT(VAEncPictureTypeBidirectional == pictureType);
            LOGASSERT(1 == config->weightedBipredIdc);
        }
    }
    if (encodeInfo->list1LumaWeightsEnabled || encodeInfo->list1ChromaWeightsEnabled)
    {
        LOGASSERT(VAEncPictureTypeBidirectional == pictureType);
        LOGASSERT(1 == config->weightedBipredIdc);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The traversal bound also rejects cycles. Every segment must fit the remaining native-buffer
 *  capacity before its size is added. No destination bytes are written during validation.
 */
static bool_t GetCodedByteCount(const VACodedBufferSegment* first,
                                uint32_t                    capacity,
                                uint32_t*                   segmentCount,
                                uint64_t*                   byteCount)
{
    const VACodedBufferSegment* segment;
    uint32_t                    count;
    uint64_t                    total;
    bool_t                      is_valid;
    bool_t                      has_payload;
    bool_t                      has_bytes;
    bool_t                      is_byte_aligned;
    bool_t                      fits_buffer;
    bool_t                      is_error_free;
    bool_t                      is_terminated;

    LOGASSERT(NULL != segmentCount);
    LOGASSERT(NULL != byteCount);
    LOGASSERT(0 < capacity);

    segment  = first;
    count    = 0;
    total    = 0;
    is_valid = (NULL != segment);

    for (; count < CODED_SEGMENT_CAPACITY; ++count)
    {
        if (NULL == segment)
        {
            break;
        }

        has_payload     = (NULL != segment->buf);
        has_bytes       = (0 < segment->size);
        is_byte_aligned = (0 == segment->bit_offset);
        fits_buffer     = (segment->size <= (capacity - total));
        is_error_free   = TRUE;
        if (segment->status & CODED_SEGMENT_ERROR_MASK)
        {
            is_error_free = FALSE;
        }
        is_valid = has_payload && has_bytes && is_byte_aligned && fits_buffer && is_error_free;
        if (FALSE == is_valid)
        {
            break;
        }

        total   += segment->size;
        segment  = segment->next;
    }

    is_terminated = (NULL == segment);
    is_valid      = is_valid && is_terminated;
    if (is_valid)
    {
        (*segmentCount) = count;
        (*byteCount)    = total;
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A zero-timeout coded-buffer poll is preferred. Drivers without that operation use the surface
 *  readiness query; neither path requests an indefinite wait. Mapped segments remain driver-owned
 *  and stable through both validation and copying. Mapping and unmapping are paired on every path.
 */
static bool_t GetStatus(Gfx_Encoder_t* encoder, void* codedBytes, uint64_t codedCapacity, uint64_t* codedSize)
{
    bool_t                      copied;
    bool_t                      failed;
    VAStatus                    status;
    bool_t                      is_ready;
    bool_t                      needs_surface_query;
    VASurfaceStatus             surface_status;
    void*                       mapped;
    const VACodedBufferSegment* first;
    uint32_t                    segment_count;
    uint64_t                    required;
    bool_t                      is_valid;
    bool_t                      has_capacity;
    const VACodedBufferSegment* segment;
    byte_t*                     destination;
    uint64_t                    offset;
    uint32_t                    segment_index;
    bool_t                      can_release;

    LOGASSERT(encoder->ready);
    LOGASSERT(0 < codedCapacity);
    LOGASSERT(UINT32_MAX >= codedCapacity);

    (*codedSize) = 0;
    copied       = FALSE;
    failed       = FALSE;
    if (encoder->framePending)
    {
        status              = cvaSyncBuffer(encoder->dpy, encoder->codedBufId, COMPLETION_POLL_TIMEOUT);
        is_ready            = (VA_STATUS_SUCCESS == status);
        failed              = (VA_STATUS_ERROR_ENCODING_ERROR == status);
        needs_surface_query = (VA_STATUS_ERROR_UNIMPLEMENTED == status);
        if (needs_surface_query)
        {
            cvaQuerySurfaceStatus(encoder->dpy, encoder->surface->target.readImage->va.surfaceId, &surface_status);
            is_ready = (VASurfaceReady == surface_status);
        }

        if (is_ready)
        {
            mapped = NULL;
            cvaMapBuffer2(encoder->dpy, encoder->codedBufId, &mapped, VA_MAPBUFFER_FLAG_READ);
            first    = mapped;
            is_valid = GetCodedByteCount(first, encoder->codedBufferCapacity, &segment_count, &required);
            failed   = (FALSE == is_valid);
            if (is_valid)
            {
                (*codedSize) = required;
                has_capacity = (required <= codedCapacity);
                if (has_capacity)
                {
                    segment     = first;
                    destination = codedBytes;
                    offset      = 0;
                    for (segment_index = 0; segment_index < segment_count; ++segment_index)
                    {
                        CRT_Memory_Copy(destination + offset, segment->buf, segment->size);
                        offset  += segment->size;
                        segment  = segment->next;
                    }
                    copied = TRUE;
                }
            }
            cvaUnmapBuffer(encoder->dpy, encoder->codedBufId);
        }
    }

    can_release = copied || failed;
    if (can_release)
    {
        encoder->surface->readPending = FALSE;
        encoder->framePending         = FALSE;
    }
    if (failed)
    {
        (*codedSize)      = UINT64_MAX;
        encoder->needsIdr = TRUE;
    }

    return copied;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */

static void CreateVaapiEncoderResources(VADisplay       dpy,
                                        VAContextID     context,
                                        VAProfile       profile,
                                        uint8_t         profileIdc,
                                        uint8_t         constraintSetFlags,
                                        const Config_t* config,
                                        uint32_t        codedBufferCapacity,
                                        VABufferID*     codedBufId,
                                        VABufferID*     sequenceParameterBufId,
                                        VABufferID*     pictureParameterBufId,
                                        VABufferID*     sliceParameterBufId,
                                        VABufferID*     packedHeaderParameterBufId,
                                        VABufferID*     packedHeaderDataBufId,
                                        VABufferID*     packedPictureHeaderParameterBufId,
                                        VABufferID*     packedPictureHeaderDataBufId,
                                        VABufferID*     rateControlBufId,
                                        VABufferID*     frameRateBufId)
{
    uint32_t                         target_percentage;
    RateControlPacket_t              rate_control;
    FrameRatePacket_t                frame_rate;

    VAEncSequenceParameterBufferH264 sps;
    uint32_t                         default_reference_count_l0;
    uint32_t                         default_reference_count_l1;
    Utl_H264_SpsParams_t             sps_parameters;
    Utl_H264_PpsParams_t             pps_parameters;
    byte_t                           packed_header_data[ENCODER_PARAMETER_SET_CAPACITY];
    uint64_t                         packed_header_size;
    uint32_t                         packed_header_bit_count;
    VAEncPackedHeaderParameterBuffer packed_header_parameters;
    uint32_t                         maximum_frame_number;
    uint32_t                         maximum_pic_order_count_lsb;

    LOGASSERT(config->targetBitRate <= config->maximumBitRate);
    LOGASSERT(0 < config->frameRateNumerator);
    LOGASSERT(UINT16_MAX >= config->frameRateNumerator);
    LOGASSERT(0 < config->frameRateDenominator);
    LOGASSERT(UINT16_MAX >= config->frameRateDenominator);
    LOGASSERT(VA_INVALID_ID == (*rateControlBufId));
    LOGASSERT(VA_INVALID_ID == (*frameRateBufId));

    target_percentage = 0;
    if (0 < config->maximumBitRate)
    {
        target_percentage =
            (uint32_t)(((uint64_t)config->targetBitRate * RATE_PERCENTAGE_SCALE + config->maximumBitRate - 1) /
                       config->maximumBitRate);
    }

    CRT_Memory_Set(&rate_control, 0, sizeof(rate_control));
    rate_control.type                                            = VAEncMiscParameterTypeRateControl;
    rate_control.parameters.bits_per_second                      = config->maximumBitRate;
    rate_control.parameters.target_percentage                    = target_percentage;
    rate_control.parameters.window_size                          = config->rateControlWindowMilliseconds;
    rate_control.parameters.initial_qp                           = config->initialQuantizationParameter;
    rate_control.parameters.min_qp                               = config->minimumQuantizationParameter;
    rate_control.parameters.basic_unit_size                      = config->basicUnitSize;
    rate_control.parameters.rc_flags.bits.reset                  = config->resetRateControl ? 1 : 0;
    rate_control.parameters.rc_flags.bits.disable_frame_skip     = config->disableFrameSkip ? 1 : 0;
    rate_control.parameters.rc_flags.bits.disable_bit_stuffing   = config->disableBitStuffing ? 1 : 0;
    rate_control.parameters.rc_flags.bits.mb_rate_control        = config->macroblockRateControlMode;
    rate_control.parameters.rc_flags.bits.temporal_id            = config->temporalLayer;
    rate_control.parameters.rc_flags.bits.cfs_I_frames           = config->constantFrameSizeIntraFrames ? 1 : 0;
    rate_control.parameters.rc_flags.bits.enable_parallel_brc    = config->parallelRateControl ? 1 : 0;
    rate_control.parameters.rc_flags.bits.enable_dynamic_scaling = config->enableDynamicScaling ? 1 : 0;
    rate_control.parameters.rc_flags.bits.frame_tolerance_mode   = config->frameToleranceMode;
    rate_control.parameters.ICQ_quality_factor                   = config->intelligentConstantQualityFactor;
    rate_control.parameters.max_qp                               = config->maximumQuantizationParameter;
    rate_control.parameters.quality_factor                       = config->qualityFactor;
    rate_control.parameters.target_frame_size                    = config->targetFrameSize;

    CRT_Memory_Set(&frame_rate, 0, sizeof(frame_rate));
    frame_rate.type = VAEncMiscParameterTypeFrameRate;
    frame_rate.parameters.framerate =
        (config->frameRateDenominator << FRAME_RATE_DENOMINATOR_SHIFT) | config->frameRateNumerator;
    frame_rate.parameters.framerate_flags.bits.temporal_id = config->temporalLayer;

    InitializeSequenceParameters(config, profile, &sps);

    cvaCreateBuffer(dpy, context, VAEncCodedBufferType, codedBufferCapacity, 1, NULL, codedBufId);
    cvaCreateBuffer(dpy,
                    context,
                    VAEncMiscParameterBufferType,
                    (uint32_t)sizeof(rate_control),
                    1,
                    &rate_control,
                    rateControlBufId);
    cvaCreateBuffer(dpy,
                    context,
                    VAEncMiscParameterBufferType,
                    (uint32_t)sizeof(frame_rate),
                    1,
                    &frame_rate,
                    frameRateBufId);

    cvaCreateEncSequenceParameterBufferH264(dpy, context, 1, &sps, sequenceParameterBufId);
    cvaCreateBuffer(dpy,
                    context,
                    VAEncPictureParameterBufferType,
                    (uint32_t)sizeof(VAEncPictureParameterBufferH264),
                    1,
                    NULL,
                    pictureParameterBufId);
    cvaCreateBuffer(dpy,
                    context,
                    VAEncSliceParameterBufferType,
                    (uint32_t)sizeof(VAEncSliceParameterBufferH264),
                    config->sliceCount,
                    NULL,
                    sliceParameterBufId);
    maximum_frame_number        = Utl_H264_DeriveMaximumFrameNumber(config->intraIdrPeriod);
    maximum_pic_order_count_lsb = Utl_H264_DeriveMaximumPicOrderCntLsb(maximum_frame_number);
    if (config->packedHeaders & GFX_ENCODER_PACKED_HEADER_SEQUENCE)
    {
        Utl_H264_InitializeSps(&sps_parameters,
                               profileIdc,
                               constraintSetFlags,
                               sps.seq_parameter_set_id,
                               sps.seq_fields.bits.chroma_format_idc,
                               UTL_H264_MIN_BIT_DEPTH,
                               UTL_H264_MIN_BIT_DEPTH,
                               QPPRIME_Y_ZERO_TRANSFORM_BYPASS,
                               config->width,
                               config->height,
                               config->frameRateNumerator,
                               config->frameRateDenominator,
                               maximum_frame_number,
                               maximum_pic_order_count_lsb,
                               sps.max_num_ref_frames,
                               GAPS_IN_FRAME_NUM_VALUE_ALLOWED,
                               config->interlaced,
                               sps.seq_fields.bits.mb_adaptive_frame_field_flag,
                               sps.seq_fields.bits.direct_8x8_inference_flag,
                               sps.vui_parameters_present_flag,
                               config->sampleAspectRatioWidth,
                               config->sampleAspectRatioHeight,
                               sps.vui_fields.bits.timing_info_present_flag,
                               sps.vui_fields.bits.fixed_frame_rate_flag,
                               sps.vui_fields.bits.bitstream_restriction_flag,
                               sps.vui_fields.bits.motion_vectors_over_pic_boundaries_flag,
                               UTL_H264_SIZE_LIMIT_UNSPECIFIED,
                               UTL_H264_SIZE_LIMIT_UNSPECIFIED,
                               config->maxMotionVectorLengthHorizontal,
                               config->maxMotionVectorLengthVertical,
                               sps.max_num_ref_frames,
                               sps.max_num_ref_frames);
        packed_header_size = Utl_H264_WriteSpsNalUnit(packed_header_data, sizeof(packed_header_data), &sps_parameters);
        LOGASSERT(0 < packed_header_size);
        LOGASSERT(sizeof(packed_header_data) >= packed_header_size);
        LOGASSERT((UINT32_MAX / CHAR_BIT) >= packed_header_size);
        packed_header_bit_count = (uint32_t)packed_header_size * CHAR_BIT;

        cvaInitEncPackedHeaderParameterBuffer(&packed_header_parameters,
                                              VAEncPackedHeaderSequence,
                                              packed_header_bit_count,
                                              TRUE);
        cvaCreateBuffer(dpy,
                        context,
                        VAEncPackedHeaderParameterBufferType,
                        (uint32_t)sizeof(packed_header_parameters),
                        1,
                        &packed_header_parameters,
                        packedHeaderParameterBufId);
        cvaCreateBuffer(dpy,
                        context,
                        VAEncPackedHeaderDataBufferType,
                        (uint32_t)packed_header_size,
                        1,
                        packed_header_data,
                        packedHeaderDataBufId);
    }
    else
    {
        (*packedHeaderParameterBufId) = VA_INVALID_ID;
        (*packedHeaderDataBufId)      = VA_INVALID_ID;
    }
    if (config->packedHeaders & GFX_ENCODER_PACKED_HEADER_PICTURE)
    {
        Utl_H264_DeriveActiveReferenceCounts(UTL_H264_SLICE_TYPE_I,
                                             0,
                                             &default_reference_count_l0,
                                             &default_reference_count_l1);
        Utl_H264_InitializePps(&pps_parameters,
                               0,
                               sps.seq_parameter_set_id,
                               config->useCabac,
                               config->picOrderPresent,
                               default_reference_count_l0,
                               default_reference_count_l1,
                               config->useWeightedPrediction,
                               config->weightedBipredIdc,
                               config->quantizationParameter,
                               UTL_H264_QUANTIZATION_PARAMETER_BIAS,
                               config->chromaQpIndexOffset,
                               DEBLOCKING_FILTER_CONTROL_PRESENT,
                               config->constrainedIntraPrediction,
                               config->redundantPictures,
                               config->use8x8Transform,
                               sps.seq_fields.bits.chroma_format_idc,
                               config->secondChromaQpIndexOffset);
        if (config->scalingListsPresent)
        {
            uint32_t       list_index;
            const uint8_t* scaling_list_coefficients;

            pps_parameters.picScalingMatrixPresentFlag = (uint8_t)TRUE;
            for (list_index = 0; list_index < UTL_H264_SCALING_LIST_4X4_COUNT; ++list_index)
            {
                scaling_list_coefficients =
                    config->scalingList4x4 + (list_index * UTL_H264_SCALING_LIST_4X4_ELEMENT_COUNT);
                Utl_H264_InitializeScalingList(&pps_parameters.scalingLists[list_index],
                                               scaling_list_coefficients,
                                               UTL_H264_SCALING_LIST_4X4_ELEMENT_COUNT);
            }
            if (config->use8x8Transform)
            {
                for (list_index = 0; list_index < UTL_H264_SCALING_LIST_8X8_COUNT; ++list_index)
                {
                    scaling_list_coefficients =
                        config->scalingList8x8 + (list_index * UTL_H264_SCALING_LIST_8X8_ELEMENT_COUNT);
                    Utl_H264_InitializeScalingList(
                        &pps_parameters.scalingLists[UTL_H264_SCALING_LIST_4X4_COUNT + list_index],
                        scaling_list_coefficients,
                        UTL_H264_SCALING_LIST_8X8_ELEMENT_COUNT);
                }
            }
        }
        packed_header_size = Utl_H264_WritePpsNalUnit(packed_header_data, sizeof(packed_header_data), &pps_parameters);
        LOGASSERT(0 < packed_header_size);
        LOGASSERT(sizeof(packed_header_data) >= packed_header_size);
        LOGASSERT((UINT32_MAX / CHAR_BIT) >= packed_header_size);
        packed_header_bit_count = (uint32_t)packed_header_size * CHAR_BIT;

        cvaInitEncPackedHeaderParameterBuffer(&packed_header_parameters,
                                              VAEncPackedHeaderPicture,
                                              packed_header_bit_count,
                                              TRUE);
        cvaCreateBuffer(dpy,
                        context,
                        VAEncPackedHeaderParameterBufferType,
                        (uint32_t)sizeof(packed_header_parameters),
                        1,
                        &packed_header_parameters,
                        packedPictureHeaderParameterBufId);
        cvaCreateBuffer(dpy,
                        context,
                        VAEncPackedHeaderDataBufferType,
                        (uint32_t)packed_header_size,
                        1,
                        packed_header_data,
                        packedPictureHeaderDataBufId);
    }
    else
    {
        (*packedPictureHeaderParameterBufId) = VA_INVALID_ID;
        (*packedPictureHeaderDataBufId)      = VA_INVALID_ID;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Explicit profile and entrypoint requests must be supported. AUTO profiles are tried in
 *  preference order; AUTO entrypoints prefer low-power encoding with normal slices as fallback.
 */
static void CreateVaapiEncoder(VADisplay                 dpy,
                               uint32_t                  format,
                               uint32_t                  pixelFormat,
                               VASurfaceID               surfaceId,
                               uint32_t                  width,
                               uint32_t                  height,
                               Gfx_Encoder_H264Profile_t h264Profile,
                               Gfx_Encoder_Entrypoint_t  requestedEntrypoint,
                               Gfx_Encoder_RateControl_t rateControl,
                               bool_t                    macroblockRateControl,
                               bool_t                    parallelRateControl,
                               uint32_t                  packedHeaders,
                               bool_t                    interlaced,
                               bool_t                    useCabac,
                               bool_t                    use8x8Transform,
                               bool_t                    scalingListsPresent,
                               uint32_t                  ipPeriod,
                               bool_t                    useWeightedPrediction,
                               uint8_t                   weightedBipredIdc,
                               bool_t                    redundantPictures,
                               int8_t                    chromaQpIndexOffset,
                               int8_t                    secondChromaQpIndexOffset,
                               VASurfaceID*              reconstructionSurfaces,
                               uint32_t                  reconstructionSurfaceCount,
                               VAConfigID*               configId,
                               VAContextID*              context,
                               VAProfile*                profile,
                               uint8_t*                  profileIdc,
                               uint8_t*                  constraintSetFlags)
{
    uint8_t         slice_type;
    VAProfile       requested_profile;
    VAProfile       selected_profile;
    VAEntrypoint    entrypoint;
    VAEntrypoint    preferred_entrypoint;
    VAConfigID      config_id;
    VAConfigAttrib  attributes[CONFIG_ATTRIBUTE_CAPACITY];
    int32_t         attribute_count;
    int32_t         scan_mode;
    bool_t          pixel_format_supported;
    VASurfaceAttrib surface_attributes[CVA_MAX_SURFACE_ATTRIBUTES];
    uint32_t        surface_attribute_count;
    uint32_t        surface_attribute_index;
    bool_t          is_pixel_format;
    bool_t          is_integer;
    bool_t          can_compare;
    VASurfaceAttrib reconstruction_attributes[2];
    VASurfaceID     render_targets[RENDER_TARGET_CAPACITY];
    uint32_t        reconstruction_surface_index;

    LOGASSERT(((uint64_t)reconstructionSurfaceCount + INPUT_SURFACE_COUNT) <= COUNT_OF(render_targets));

    preferred_entrypoint = GetVaapiEntrypoint(requestedEntrypoint);
    requested_profile    = GetVaapiProfile(h264Profile);
    scan_mode            = GetVaapiScanMode(interlaced);

    if (1 < ipPeriod)
    {
        slice_type = UTL_H264_SLICE_TYPE_B;
    }
    else
    {
        slice_type = UTL_H264_SLICE_TYPE_P;
    }

    selected_profile = SelectVaapiProfile(dpy,
                                          requested_profile,
                                          requestedEntrypoint,
                                          preferred_entrypoint,
                                          slice_type,
                                          interlaced,
                                          useCabac,
                                          use8x8Transform,
                                          scalingListsPresent,
                                          useWeightedPrediction,
                                          weightedBipredIdc,
                                          redundantPictures,
                                          chromaQpIndexOffset,
                                          secondChromaQpIndexOffset,
                                          &entrypoint,
                                          profileIdc,
                                          constraintSetFlags);

    attribute_count = SelectVaapiConfig(dpy,
                                        selected_profile,
                                        entrypoint,
                                        format,
                                        rateControl,
                                        macroblockRateControl,
                                        parallelRateControl,
                                        packedHeaders,
                                        attributes);
    cvaCreateConfig(dpy, selected_profile, entrypoint, attributes, attribute_count, &config_id);

    cvaQuerySurfaceAttributes(dpy, config_id, surface_attributes, &surface_attribute_count);
    pixel_format_supported = FALSE;
    for (surface_attribute_index = 0; surface_attribute_index < surface_attribute_count; ++surface_attribute_index)
    {
        is_pixel_format = (VASurfaceAttribPixelFormat == surface_attributes[surface_attribute_index].type);
        is_integer      = (VAGenericValueTypeInteger == surface_attributes[surface_attribute_index].value.type);
        can_compare     = is_pixel_format && is_integer;
        if (can_compare)
        {
            pixel_format_supported =
                (pixelFormat == (uint32_t)surface_attributes[surface_attribute_index].value.value.i);
            if (pixel_format_supported)
            {
                break;
            }
        }
    }
    LOGASSERT(FALSE != pixel_format_supported);

    CRT_Memory_Set(reconstruction_attributes, 0, sizeof(reconstruction_attributes));
    reconstruction_attributes[0].type          = VASurfaceAttribPixelFormat;
    reconstruction_attributes[0].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    reconstruction_attributes[0].value.type    = VAGenericValueTypeInteger;
    reconstruction_attributes[0].value.value.i = (int32_t)pixelFormat;
    reconstruction_attributes[1].type          = VASurfaceAttribUsageHint;
    reconstruction_attributes[1].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    reconstruction_attributes[1].value.type    = VAGenericValueTypeInteger;
    reconstruction_attributes[1].value.value.i = VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER;
    cvaCreateSurfaces(dpy,
                      format,
                      width,
                      height,
                      reconstructionSurfaces,
                      reconstructionSurfaceCount,
                      reconstruction_attributes,
                      (uint32_t)COUNT_OF(reconstruction_attributes));

    // Put the input surface first, then append the reconstruction surfaces.
    render_targets[0] = surfaceId;
    for (reconstruction_surface_index = 0; reconstruction_surface_index < reconstructionSurfaceCount;
         ++reconstruction_surface_index)
    {
        render_targets[INPUT_SURFACE_COUNT + reconstruction_surface_index] =
            reconstructionSurfaces[reconstruction_surface_index];
    }

    cvaCreateContext(dpy,
                     config_id,
                     width,
                     height,
                     scan_mode,
                     render_targets,
                     (int32_t)(INPUT_SURFACE_COUNT + reconstructionSurfaceCount),
                     context);

    (*configId) = config_id;
    (*profile)  = selected_profile;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateEncoder(Gfx_Encoder_t* encoder, const Gfx_Encoder_CreateInfo_t* createInfo)
{
    Config_t                    config;
    uint32_t                    coded_buffer_capacity;
    VAConfigID                  config_id;
    VAContextID                 context;
    VAProfile                   profile;
    uint8_t                     profile_idc;
    uint8_t                     constraint_set_flags;
    VASurfaceID                 reconstruction_surfaces[RECONSTRUCTION_SURFACE_CAPACITY];
    VABufferID                  coded_buf_id;
    VABufferID                  sequence_parameter_buf_id;
    VABufferID                  picture_parameter_buf_id;
    VABufferID                  slice_parameter_buf_id;
    VABufferID                  packed_header_parameter_buf_id;
    VABufferID                  packed_header_data_buf_id;
    VABufferID                  packed_picture_header_parameter_buf_id;
    VABufferID                  packed_picture_header_data_buf_id;
    VABufferID                  rate_control_buf_id;
    VABufferID                  frame_rate_buf_id;
    VAIQMatrixBufferH264        matrix;
    const VAIQMatrixBufferH264* scaling_lists;

    rate_control_buf_id = VA_INVALID_ID;
    frame_rate_buf_id   = VA_INVALID_ID;
    InitializeConfig(&config, createInfo);
    coded_buffer_capacity = GetCodedBufferCapacity(&config, createInfo->surface->va.format);

    CreateVaapiEncoder(createInfo->surface->va.dpy,
                       createInfo->surface->va.format,
                       createInfo->surface->va.pixelFormat,
                       createInfo->surface->target.readImage->va.surfaceId,
                       config.width,
                       config.height,
                       config.h264Profile,
                       config.entrypoint,
                       config.rateControl,
                       config.macroblockRateControl,
                       config.parallelRateControl,
                       config.packedHeaders,
                       config.interlaced,
                       config.useCabac,
                       config.use8x8Transform,
                       config.scalingListsPresent,
                       config.ipPeriod,
                       config.useWeightedPrediction,
                       config.weightedBipredIdc,
                       config.redundantPictures,
                       config.chromaQpIndexOffset,
                       config.secondChromaQpIndexOffset,
                       reconstruction_surfaces,
                       (uint32_t)COUNT_OF(reconstruction_surfaces),
                       &config_id,
                       &context,
                       &profile,
                       &profile_idc,
                       &constraint_set_flags);

    CreateVaapiEncoderResources(createInfo->surface->va.dpy,
                                context,
                                profile,
                                profile_idc,
                                constraint_set_flags,
                                &config,
                                (uint32_t)coded_buffer_capacity,
                                &coded_buf_id,
                                &sequence_parameter_buf_id,
                                &picture_parameter_buf_id,
                                &slice_parameter_buf_id,
                                &packed_header_parameter_buf_id,
                                &packed_header_data_buf_id,
                                &packed_picture_header_parameter_buf_id,
                                &packed_picture_header_data_buf_id,
                                &rate_control_buf_id,
                                &frame_rate_buf_id);

    encoder->config        = config;
    encoder->iqMatrixBufId = VA_INVALID_ID;
    if (config.scalingListsPresent)
    {
        CRT_Memory_Copy(matrix.ScalingList4x4, config.scalingList4x4, sizeof(matrix.ScalingList4x4));
        CRT_Memory_Copy(matrix.ScalingList8x8, config.scalingList8x8, sizeof(matrix.ScalingList8x8));
        scaling_lists = &matrix;
        cvaInitIQMatrixBufferH264(&matrix, scaling_lists->ScalingList4x4, scaling_lists->ScalingList8x8);
        cvaCreateIQMatrixBufferH264(createInfo->surface->va.dpy, context, 1, &matrix, &encoder->iqMatrixBufId);
    }
    encoder->surface            = createInfo->surface;
    encoder->dpy                = createInfo->surface->va.dpy;
    encoder->profile            = profile;
    encoder->profileIdc         = profile_idc;
    encoder->constraintSetFlags = constraint_set_flags;
    encoder->configId           = config_id;
    encoder->context            = context;
    CRT_Memory_Copy(encoder->reconstructionSurfaces, reconstruction_surfaces, sizeof(reconstruction_surfaces));
    encoder->codedBufId                        = coded_buf_id;
    encoder->codedBufferCapacity               = (uint32_t)coded_buffer_capacity;
    encoder->sequenceParameterBufId            = sequence_parameter_buf_id;
    encoder->pictureParameterBufId             = picture_parameter_buf_id;
    encoder->sliceParameterBufId               = slice_parameter_buf_id;
    encoder->packedHeaderParameterBufId        = packed_header_parameter_buf_id;
    encoder->packedHeaderDataBufId             = packed_header_data_buf_id;
    encoder->packedPictureHeaderParameterBufId = packed_picture_header_parameter_buf_id;
    encoder->packedPictureHeaderDataBufId      = packed_picture_header_data_buf_id;
    encoder->rateControlBufId                  = rate_control_buf_id;
    encoder->frameRateBufId                    = frame_rate_buf_id;
    encoder->decodedPictureCount               = 0;
    encoder->idrFrameIndex                     = 0;
    encoder->idrPicId                          = 1;
    encoder->needsIdr                          = TRUE;
    encoder->framePending                      = FALSE;
    encoder->ready                             = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ResetIdrState(uint32_t  frameIndex,
                          uint32_t* decodedPictureCount,
                          uint32_t* idrFrameIndex,
                          uint16_t* idrPicId)
{
    // Pictures before this IDR are no longer available as references.
    (*decodedPictureCount) = 0;

    // Subtract this origin so the IDR starts at frame number and picture order count zero.
    (*idrFrameIndex) = frameIndex;

    // Alternate IDs so consecutive IDR access units have different idr_pic_id values.
    if (0 == (*idrPicId))
    {
        (*idrPicId) = 1;
    }
    else
    {
        (*idrPicId) = 0;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void UpdateDecodedPictureBuffer(Gfx_Encoder_t*           encoder,
                                       Utl_H264_ReferenceType_t referenceType,
                                       VASurfaceID              reconstructionSurfaceId,
                                       uint32_t                 frameIndex,
                                       bool_t                   topField,
                                       bool_t                   bottomField)
{
    if (encoder->decodedPictureCount == encoder->config.maxReferenceFrames)
    {
        // Evict the oldest reference and keep its surface and metadata arrays aligned.
        encoder->decodedPictureCount--;
        (void)memmove(encoder->decodedPictureFrameNumbers,
                      &encoder->decodedPictureFrameNumbers[1],
                      encoder->decodedPictureCount * sizeof(uint32_t));
        (void)memmove(encoder->decodedPictureSurfaces,
                      &encoder->decodedPictureSurfaces[1],
                      encoder->decodedPictureCount * sizeof(VASurfaceID));
        (void)memmove(encoder->decodedPictureFrameIndices,
                      &encoder->decodedPictureFrameIndices[1],
                      encoder->decodedPictureCount * sizeof(uint32_t));
        (void)memmove(encoder->decodedPictureLongTermReferences,
                      &encoder->decodedPictureLongTermReferences[1],
                      encoder->decodedPictureCount * sizeof(bool_t));
        (void)memmove(encoder->decodedPictureTopFields,
                      &encoder->decodedPictureTopFields[1],
                      encoder->decodedPictureCount * sizeof(bool_t));
        (void)memmove(encoder->decodedPictureBottomFields,
                      &encoder->decodedPictureBottomFields[1],
                      encoder->decodedPictureCount * sizeof(bool_t));
        (void)memmove(encoder->decodedPictureNonExisting,
                      &encoder->decodedPictureNonExisting[1],
                      encoder->decodedPictureCount * sizeof(bool_t));
    }
    LOGASSERT(encoder->decodedPictureCount < encoder->config.maxReferenceFrames);

    // Append the submitted reconstruction as the newest reference.
    encoder->decodedPictureSurfaces[encoder->decodedPictureCount]     = reconstructionSurfaceId;
    encoder->decodedPictureFrameIndices[encoder->decodedPictureCount] = frameIndex;
    encoder->decodedPictureFrameNumbers[encoder->decodedPictureCount] = encoder->nextFrameNum;
    encoder->decodedPictureLongTermReferences[encoder->decodedPictureCount] =
        UTL_H264_REFERENCE_TYPE_LONG_TERM == referenceType;
    encoder->decodedPictureTopFields[encoder->decodedPictureCount]    = topField;
    encoder->decodedPictureBottomFields[encoder->decodedPictureCount] = bottomField;
    encoder->decodedPictureNonExisting[encoder->decodedPictureCount]  = FALSE;
    encoder->decodedPictureCount++;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void EncodeImage(Gfx_Encoder_t* encoder, Gfx_Image_t* image, const Gfx_Encoder_EncodeInfo_t* encodeInfo)
{
    VAEncPictureParameterBufferH264 pps;
    uint32_t                        slice_index;
    uint32_t                        frame_index;
    VABufferID                      parameter_buf_ids[PARAMETER_BUFFER_CAPACITY];
    uint32_t                        parameter_buf_id_count;
    VASurfaceID                     reconstruction_surface_id;
    ReferenceLists_t                lists;
    VAEncPictureType                va_picture_type;
    uint8_t                         slice_type;
    bool_t                          profile_coding_compatible;
    bool_t                          idr_picture;
    bool_t                          top_field;
    bool_t                          bottom_field;
    void*                           mapped_parameters;

    parameter_buf_id_count = 0;
    InitializeReferenceLists(&lists);

    LOGASSERT(encoder->decodedPictureCount <= encoder->config.maxReferenceFrames);
    LOGASSERT(encodeInfo->fieldMode >= UTL_H264_FIELD_MODE_FRAME &&
              encodeInfo->fieldMode <= UTL_H264_FIELD_MODE_BOTTOM);
    LOGASSERT(encodeInfo->referenceType >= UTL_H264_REFERENCE_TYPE_SHORT_TERM &&
              encodeInfo->referenceType <= UTL_H264_REFERENCE_TYPE_LONG_TERM);

    va_picture_type = GetVaapiPictureTypeH264(encodeInfo->pictureType, &idr_picture);
    ValidatePredictionWeightsH264(&encoder->config, encodeInfo, va_picture_type);

    slice_type                = Utl_H264_DeriveSliceType(encodeInfo->pictureType);
    profile_coding_compatible = Utl_H264_IsProfileCodingCompatible(encoder->profileIdc,
                                                                   encoder->constraintSetFlags,
                                                                   slice_type,
                                                                   !encoder->config.interlaced,
                                                                   encoder->config.useCabac,
                                                                   encoder->config.use8x8Transform,
                                                                   encoder->config.scalingListsPresent,
                                                                   encoder->config.useWeightedPrediction,
                                                                   encoder->config.weightedBipredIdc,
                                                                   encoder->config.redundantPictures,
                                                                   encoder->config.chromaQpIndexOffset,
                                                                   encoder->config.secondChromaQpIndexOffset);
    LOGASSERT(profile_coding_compatible);

    top_field    = UTL_H264_FIELD_MODE_TOP == encodeInfo->fieldMode;
    bottom_field = UTL_H264_FIELD_MODE_BOTTOM == encodeInfo->fieldMode;
    if (idr_picture)
    {
        encoder->nextFrameNum = 0;
        ResetIdrState(encodeInfo->frameIndex,
                      &encoder->decodedPictureCount,
                      &encoder->idrFrameIndex,
                      &encoder->idrPicId);
    }

    LOGASSERT(encodeInfo->frameIndex >= encoder->idrFrameIndex);
    frame_index = encodeInfo->frameIndex - encoder->idrFrameIndex;

    reconstruction_surface_id = GetReconstructionSurfaceId(encoder->reconstructionSurfaces,
                                                           (uint32_t)COUNT_OF(encoder->reconstructionSurfaces),
                                                           encoder->decodedPictureSurfaces,
                                                           encoder->decodedPictureCount);

    if ((VAEncPictureTypeBidirectional == va_picture_type) || (VAEncPictureTypePredictive == va_picture_type))
    {
        lists.count0 = BuildReferenceListH264(encoder, frame_index, FALSE, lists.list0);
    }
    if (VAEncPictureTypeBidirectional == va_picture_type)
    {
        lists.count1 = BuildReferenceListH264(encoder, frame_index, TRUE, lists.list1);
    }

    InitializePictureParameters(encoder, encodeInfo, frame_index, reconstruction_surface_id, &pps);

    for (slice_index = 0; slice_index < encoder->config.sliceCount; ++slice_index)
    {
        InitializeSliceParameters(encoder, encodeInfo, frame_index, &lists, slice_index);
    }
    cvaMapBuffer2(encoder->dpy, encoder->pictureParameterBufId, &mapped_parameters, VA_MAPBUFFER_FLAG_WRITE);
    LOGASSERT(NULL != mapped_parameters);
    CRT_Memory_Copy(mapped_parameters, &pps, sizeof(pps));
    cvaUnmapBuffer(encoder->dpy, encoder->pictureParameterBufId);
    cvaMapBuffer2(encoder->dpy, encoder->sliceParameterBufId, &mapped_parameters, VA_MAPBUFFER_FLAG_WRITE);
    LOGASSERT(NULL != mapped_parameters);
    CRT_Memory_Copy(mapped_parameters, encoder->slice, sizeof(encoder->slice[0]) * encoder->config.sliceCount);
    cvaUnmapBuffer(encoder->dpy, encoder->sliceParameterBufId);

    cvaBeginPicture(encoder->dpy, encoder->context, image->va.surfaceId);
    parameter_buf_ids[parameter_buf_id_count++] = encoder->sequenceParameterBufId;
    parameter_buf_ids[parameter_buf_id_count++] = encoder->pictureParameterBufId;
    parameter_buf_ids[parameter_buf_id_count++] = encoder->rateControlBufId;
    parameter_buf_ids[parameter_buf_id_count++] = encoder->frameRateBufId;
    if (VA_INVALID_ID != encoder->iqMatrixBufId)
    {
        parameter_buf_ids[parameter_buf_id_count++] = encoder->iqMatrixBufId;
    }
    if (encoder->config.packedHeaders & GFX_ENCODER_PACKED_HEADER_SEQUENCE)
    {
        parameter_buf_ids[parameter_buf_id_count++] = encoder->packedHeaderParameterBufId;
        parameter_buf_ids[parameter_buf_id_count++] = encoder->packedHeaderDataBufId;
    }
    if (encoder->config.packedHeaders & GFX_ENCODER_PACKED_HEADER_PICTURE)
    {
        parameter_buf_ids[parameter_buf_id_count++] = encoder->packedPictureHeaderParameterBufId;
        parameter_buf_ids[parameter_buf_id_count++] = encoder->packedPictureHeaderDataBufId;
    }
    parameter_buf_ids[parameter_buf_id_count++] = encoder->sliceParameterBufId;
    LOGASSERT(parameter_buf_id_count <= COUNT_OF(parameter_buf_ids));
    cvaRenderPicture(encoder->dpy, encoder->context, parameter_buf_ids, (int32_t)parameter_buf_id_count);
    cvaEndPicture(encoder->dpy, encoder->context);

    image->uploadPending = FALSE;

    if ((encodeInfo->referencePicture) && (0 != encoder->config.maxReferenceFrames))
    {
        UpdateDecodedPictureBuffer(encoder,
                                   encodeInfo->referenceType,
                                   reconstruction_surface_id,
                                   frame_index,
                                   top_field,
                                   bottom_field);
        encoder->nextFrameNum =
            (encoder->nextFrameNum + 1) &
            Utl_H264_DeriveValueMask(Utl_H264_DeriveMaximumFrameNumber(encoder->config.intraIdrPeriod));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static bool_t Encode(Gfx_Encoder_t* encoder, const Gfx_Encoder_EncodeInfo_t* encodeInfo)
{
    LOGASSERT(encoder->ready);

    if (encoder->framePending || (encoder->needsIdr && (UTL_H264_PICTURE_TYPE_IDR != encodeInfo->pictureType)))
    {
        return FALSE;
    }

    Gfx_Surface_Acquire(encoder->surface);

    EncodeImage(encoder, encoder->surface->acquiredImage, encodeInfo);

    encoder->surface->readPending   = TRUE;
    encoder->surface->acquiredImage = NULL;
    encoder->framePending           = TRUE;
    encoder->needsIdr               = encodeInfo->lastFrame;

    return TRUE;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Encoder_Create(Gfx_Encoder_t** encoder, Gfx_Encoder_CreateInfo_t* createInfo)
{
    LOGASSERT(NULL != encoder);
    LOGASSERT(NULL != createInfo);

    (*encoder) = (Gfx_Encoder_t*)OS_CALLOC(sizeof(Gfx_Encoder_t));
    CreateEncoder((*encoder), createInfo);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Encoder_Encode(Gfx_Encoder_t* encoder, const Gfx_Encoder_EncodeInfo_t* encodeInfo)
{
    LOGASSERT(NULL != encoder);
    LOGASSERT(NULL != encodeInfo);

    return Encode(encoder, encodeInfo);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Encoder_Status(Gfx_Encoder_t* encoder, void* codedBytes, uint64_t codedCapacity, uint64_t* codedSize)
{
    LOGASSERT(NULL != encoder);
    LOGASSERT(NULL != codedBytes);
    LOGASSERT(NULL != codedSize);

    return GetStatus(encoder, codedBytes, codedCapacity, codedSize);
}
