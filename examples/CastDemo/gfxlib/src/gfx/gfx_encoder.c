/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_encoder.h"

#include "os/os_assert.h"
#include "os/os_malloc.h"
#include "gfx/gfx_image.h"
#include "gfx/gfx_surface.h"
#include "utl/utl_h264.h"
#include "utl/utl_util.h"

#include <stddef.h>
#include <stdint.h>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4200)
#endif
#include <va/va.h>
#include <va/va_enc_h264.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "gfx/gfx_vaapi.h"
#include "utl/utl_defines.h"

#include <string.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define ENCODER_PARAMETER_SET_CAPACITY  (1024u)                                                 ///< Includes space for custom scaling-list syntax.
#define PARAMETER_BUFFER_CAPACITY       (10U)                                                   ///< Maximum VA buffers submitted for one encoded picture.
#define RECONSTRUCTION_SURFACE_CAPACITY (UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES + 1U)      ///< DPB capacity plus the current reconstruction target.
#define INPUT_SURFACE_COUNT             (1U)                                                    ///< Number of source surfaces attached to the encode context.
#define CONFIG_ATTRIBUTE_CAPACITY      (3U)                                                    ///< Render-target format, rate control, and packed headers.
#define RENDER_TARGET_CAPACITY          (RECONSTRUCTION_SURFACE_CAPACITY + INPUT_SURFACE_COUNT) ///< Total context render-target capacity.
#define QPPRIME_Y_ZERO_TRANSFORM_BYPASS (FALSE)                                                 ///< Disable transform bypass when QPprimeY is zero.
#define GAPS_IN_FRAME_NUM_VALUE_ALLOWED (FALSE)                                                 ///< Disallow gaps in SPS frame_num values.
#define REFERENCE_FRAME_NUM_MASK        (UINT32_MAX)                                            ///< Preserve absolute frame indices in reference lists.
#define REFERENCE_PICTURE               (TRUE)                                                  ///< Mark reference-list entries as reference pictures.
#define REFERENCE_NON_EXISTING          (FALSE)                                                 ///< Reference-list entries have valid decoded surfaces.
#define MAX_LIST1_REFERENCES_SHIFT      (16U)                                                   ///< Upper 16 bits of VAConfigAttribEncMaxRefFrames hold the list-1 limit.
#define DEBLOCKING_FILTER_CONTROL_PRESENT (TRUE)                                               ///< Signal deblocking filter control fields in slice headers.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Stream settings initialized before allocating encoder resources.
 *
 * Scaling lists are copied into the config so it can be retained by value.
 */
typedef struct Config_s
{
    uint32_t                  width;                                                         ///< Visible input width in pixels.
    uint32_t                  height;                                                        ///< Visible input height in pixels.
    Gfx_Encoder_H264Profile_t h264Profile;                                                   ///< Requested profile or automatic selection.
    Gfx_Encoder_Entrypoint_t  entrypoint;                                                    ///< Normal or low-power slice entrypoint.
    uint32_t                  frameRateNumerator;                                            ///< Nonzero frame-rate numerator, at most UINT16_MAX.
    uint32_t                  frameRateDenominator;                                          ///< Nonzero frame-rate denominator, at most UINT16_MAX.
    uint32_t                  intraPeriod;                                                   ///< Pictures between intra pictures; zero disables periodic intra.
    uint32_t                  intraIdrPeriod;                                                ///< Pictures between IDR pictures; at most UINT16_MAX.
    uint32_t                  ipPeriod;                                                      ///< Nonzero display-order distance between I/P pictures.
    uint32_t                  sliceCount;                                                    ///< 1..GFX_ENCODER_MAX_SLICES slices per picture, not exceeding its macroblock count.
    uint32_t                  targetBitRate;                                                 ///< Rate-control target in bits per second, or zero when unused.
    uint32_t                  maximumBitRate;                                                ///< Rate-control ceiling in bits per second, or zero when unused.
    Gfx_Encoder_RateControl_t rateControl;                                                   ///< Base rate-control algorithm.
    bool_t                    macroblockRateControl;                                         ///< Add VA macroblock-level rate control.
    bool_t                    parallelRateControl;                                           ///< Add VA parallel rate control.
    uint32_t                  rateControlWindowMilliseconds;                                 ///< Sliding rate-control window duration.
    uint32_t                  initialQuantizationParameter;                                  ///< Initial QP supplied to rate control.
    uint32_t                  minimumQuantizationParameter;                                  ///< Minimum QP supplied to rate control.
    uint32_t                  maximumQuantizationParameter;                                  ///< Maximum QP supplied to rate control.
    uint32_t                  basicUnitSize;                                                 ///< Macroblocks in one rate-control basic unit.
    bool_t                    resetRateControl;                                              ///< Reset backend rate-control history at creation.
    bool_t                    disableFrameSkip;                                              ///< Prohibit frame skipping by rate control.
    bool_t                    disableBitStuffing;                                            ///< Prohibit filler-bit insertion.
    uint8_t                   macroblockRateControlMode;                                     ///< VA macroblock mode in the inclusive range 0..2.
    uint8_t                   temporalLayer;                                                 ///< Temporal-layer identifier supplied to rate control.
    bool_t                    constantFrameSizeIntraFrames;                                  ///< Include intra pictures in constant-size control.
    bool_t                    enableDynamicScaling;                                          ///< Permit rate control to scale frames dynamically.
    uint8_t                   frameToleranceMode;                                            ///< VA frame-size tolerance mode in the range 0..2.
    uint32_t                  intelligentConstantQualityFactor;                              ///< ICQ quality factor when ICQ rate control is selected.
    uint32_t                  qualityFactor;                                                 ///< Backend quality factor for quality-based modes.
    uint32_t                  targetFrameSize;                                               ///< Per-picture coded-size target in bytes.
    uint32_t                  packedHeaders;                                                 ///< Bitwise OR of Gfx_Encoder_PackedHeader_t flags.
    uint32_t                  maxReferenceFrames;                                            ///< Maximum decoded-picture-buffer occupancy.
    uint32_t                  quantizationParameter;                                         ///< Base picture QP in the H.264 range 0..51.
    bool_t                    interlaced;                                                    ///< Allow frame/field coding instead of requiring progressive frames.
    bool_t                    mbAdaptiveFrameField;                                          ///< Enable MBAFF; valid only for interlaced encoding.
    bool_t                    fixedFrameRate;                                                ///< Set fixed_frame_rate_flag in VUI parameters.
    uint32_t                  sampleAspectRatioWidth;                                        ///< SAR numerator; zero with height omits SAR signaling.
    uint32_t                  sampleAspectRatioHeight;                                       ///< SAR denominator; zero with width omits SAR signaling.
    bool_t                    restrictMotionVectors;                                         ///< Signal VUI motion-vector length restrictions.
    uint32_t                  maxMotionVectorLengthHorizontal;                               ///< Nonzero maximum horizontal magnitude in quarter-pel units.
    uint32_t                  maxMotionVectorLengthVertical;                                 ///< Nonzero maximum vertical magnitude in quarter-pel units.
    bool_t                    useCabac;                                                      ///< Use CABAC instead of CAVLC entropy coding.
    bool_t                    useWeightedPrediction;                                         ///< Enable weighted prediction for P pictures.
    uint8_t                   weightedBipredIdc;                                             ///< H.264 weighted_bipred_idc value in the range 0..2.
    bool_t                    constrainedIntraPrediction;                                    ///< Restrict intra prediction to intra-coded neighbors.
    bool_t                    use8x8Transform;                                               ///< Enable the High-profile 8-by-8 transform.
    bool_t                    redundantPictures;                                             ///< Enable redundant-picture syntax.
    bool_t                    picOrderPresent;                                               ///< Include picture-order fields in slice headers.
    bool_t                    scalingListsPresent;                                           ///< Use the copied custom lists instead of defaults.
    uint8_t                   scalingList4x4[UTL_H264_SCALING_LIST_4X4_TOTAL_ELEMENT_COUNT]; ///< Six copied 16-element scaling lists.
    uint8_t                   scalingList8x8[UTL_H264_SCALING_LIST_8X8_TOTAL_ELEMENT_COUNT]; ///< Two copied 64-element scaling lists.
    int8_t                    chromaQpIndexOffset;                                           ///< First H.264 chroma QP offset in the range -12..12.
    int8_t                    secondChromaQpIndexOffset;                                     ///< Second H.264 chroma QP offset in the range -12..12.
    uint8_t                   cabacInitIdc;                                                  ///< CABAC initialization table selector in the range 0..2.
    bool_t                    enableDeblocking;                                              ///< Enable the in-loop deblocking filter.
    int8_t                    deblockingAlphaOffsetDiv2;                                     ///< Slice alpha offset divided by two, in the range -6..6.
    int8_t                    deblockingBetaOffsetDiv2;                                      ///< Slice beta offset divided by two, in the range -6..6.
} Config_t;

/*************************************************************************************************/
/**
 * \brief VA encoding context, stream configuration, reference-picture history, and submission state.
 */
struct Gfx_Encoder_s
{
    Config_t                       config;                                                                        ///< Retained stream settings with owned scaling-list storage.
    Gfx_Surface_t*                 surface;                                                                       ///< Input surface retained by the caller.
    VADisplay                      dpy;                                                                           ///< Initialized VA display owning all identifiers.
    VAProfile                      profile;                                                                       ///< Selected VA H.264 profile.
    uint8_t                        profileIdc;                                                                    ///< Selected H.264 profile_idc.
    uint8_t                        constraintSetFlags;                                                            ///< Selected H.264 constraint flags.
    VAConfigID                     configId;                                                                      ///< H.264 encode configuration.
    VAContextID                    context;                                                                       ///< H.264 encode context.
    VASurfaceID                    reconstructionSurfaces[RECONSTRUCTION_SURFACE_CAPACITY];                       ///< Persistent reconstruction-surface pool.
    VABufferID                     codedBufId;                                                                    ///< Driver coded-output buffer.
    VABufferID                     sequenceParameterBufId;                                                        ///< Reusable VA sequence-parameter buffer.
    VABufferID                     pictureParameterBufId;                                                         ///< Reusable VA picture-parameter buffer.
    VABufferID                     sliceParameterBufId;                                                           ///< Reusable VA slice-parameter buffer.
    VABufferID                     packedHeaderParameterBufId;                                                    ///< SPS packed-header parameter buffer.
    VABufferID                     packedHeaderDataBufId;                                                         ///< SPS packed-header data buffer.
    VABufferID                     packedPictureHeaderParameterBufId;                                             ///< PPS packed-header parameter buffer.
    VABufferID                     packedPictureHeaderDataBufId;                                                  ///< PPS packed-header data buffer.
    VABufferID                     iqMatrixBufId;                                                                 ///< Optional H.264 quantization matrix buffer.
    VABufferID                     rateControlBufId;                                                              ///< Persistent miscellaneous rate-control buffer.
    VABufferID                     frameRateBufId;                                                                ///< Persistent miscellaneous frame-rate buffer.
    VAEncSliceParameterBufferH264  slice[GFX_ENCODER_MAX_SLICES];                                                 ///< Fixed storage for the current picture's slice parameters.
    VASurfaceID                    decodedPictureSurfaces[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];           ///< DPB surface identifiers.
    uint32_t                       nextFrameNum;                                                                  ///< Next reference decoding-order frame number.
    uint32_t                       decodedPictureFrameNumbers[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];       ///< Stored reference frame_num values, independent of POC.
    uint32_t                       decodedPictureFrameIndices[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];       ///< Display indices parallel to DPB surfaces.
    bool_t                         decodedPictureLongTermReferences[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES]; ///< Long-term flags parallel to DPB surfaces.
    bool_t                         decodedPictureTopFields[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];          ///< Top-field flags parallel to DPB surfaces.
    bool_t                         decodedPictureBottomFields[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];       ///< Bottom-field flags parallel to DPB surfaces.
    bool_t                         decodedPictureNonExisting[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];        ///< Non-existing flags parallel to DPB surfaces.
    uint32_t                       decodedPictureCount;                                                           ///< Initialized entries in every DPB array.
    uint32_t                       idrFrameIndex;                                                                 ///< Absolute display-order origin of the active coded sequence.
    uint16_t                       idrPicId;                                                                      ///< Alternating identifier of IDR pictures.
    bool_t                         needsIdr;                                                                      ///< Requires an IDR after creation or end of stream.
    bool_t                         framePending;                                                                  ///< TRUE until Status() consumes the submitted picture.
    bool_t                         ready;                                                                         ///< TRUE after backend resource initialization.
};

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static const VAProfile c_vaPreferredProfiles[] =
{
    VAProfileH264High,
    VAProfileH264Main,
    VAProfileH264ConstrainedBaseline,
};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Enforces all VA/H.264 creation invariants before allocating encoder resources.
 *
 * \param[in] createInfo        VA encoder configuration including rate-control, GOP, slice, profile, and surface constraints
 *
 * \retval    None
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
static uint32_t GetVaapiRateControl(Gfx_Encoder_RateControl_t rateControl, bool_t macroblockRateControl, bool_t parallelRateControl);

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
static VAProfile SelectVaapiProfile(VADisplay dpy, VAProfile requestedProfile, Gfx_Encoder_Entrypoint_t requestedEntrypoint, VAEntrypoint preferredEntrypoint, uint8_t sliceType, bool_t interlaced, bool_t useCabac, bool_t use8x8Transform, bool_t scalingListsPresent, bool_t useWeightedPrediction, uint8_t weightedBipredIdc, bool_t redundantPictures, int8_t chromaQpIndexOffset, int8_t secondChromaQpIndexOffset, VAEntrypoint* entrypoint, uint8_t* profileIdc, uint8_t* constraintSetFlags);

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
static int32_t SelectVaapiConfig(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint, uint32_t format, Gfx_Encoder_RateControl_t rateControl, bool_t macroblockRateControl, bool_t parallelRateControl, uint32_t packedHeaders, VAConfigAttrib* attributes);

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
 * \retval None
 */
static void CreateVaapiEncoderResources(VADisplay        dpy,
                                        VAContextID      context,
                                        VAProfile        profile,
                                        uint8_t          profileIdc,
                                        uint8_t          constraintSetFlags,
                                        const Config_t*  config,
                                        uint32_t         codedBufferCapacity,
                                        VABufferID*      codedBufId,
                                        VABufferID*      sequenceParameterBufId,
                                        VABufferID*      pictureParameterBufId,
                                        VABufferID*      sliceParameterBufId,
                                        VABufferID*      packedHeaderParameterBufId,
                                        VABufferID*      packedHeaderDataBufId,
                                        VABufferID*      packedPictureHeaderParameterBufId,
                                        VABufferID*      packedPictureHeaderDataBufId,
                                        VABufferID*      rateControlBufId,
                                        VABufferID*      frameRateBufId);

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
 * \retval None
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
 * \brief Initializes every stream setting from a validated creation request.
 *
 * \param[out] config     Receives initialized stream settings
 * \param[in]  createInfo Creation request providing the input dimensions and stream settings
 *
 * \retval None
 */
static void InitConfig(Config_t* config, const Gfx_Encoder_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Creates the selected encoder backend.
 *
 * \param[out] encoder        Zero-initialized encoder marked ready after complete creation
 * \param[in] createInfo      Backend and codec configuration
 *
 * \retval    None
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
 * \retval None
 */
static void ResetIdrState(uint32_t frameIndex, uint32_t* decodedPictureCount, uint32_t* idrFrameIndex, uint16_t* idrPicId);

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
static bool_t IsDecodedPictureSurface(const VASurfaceID* decodedPictureSurfaces, uint32_t decodedPictureCount, VASurfaceID surfaceId);

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
static VASurfaceID GetReconstructionSurfaceId(const VASurfaceID* reconstructionSurfaces, uint32_t reconstructionSurfaceCount, const VASurfaceID* decodedPictureSurfaces, uint32_t decodedPictureCount);

/*************************************************************************************************/
/**
 * \brief Builds an ordered VA H.264 reference list from the decoded picture buffer.
 *
 * \par Details:
 *  Asserts if no decoded reference lies in the requested direction.
 *
 * \param[in]  frameIndex                       Current display-order index relative to the latest IDR
 * \param[in]  decodedPictureSurfaces            Decoded reference surface identifiers
 * \param[in]  decodedPictureFrameIndices        Display-order indices parallel to the surfaces
 * \param[in]  decodedPictureLongTermReferences  Long-term reference flags parallel to the surfaces
 * \param[in]  decodedPictureTopFields           Top-field flags parallel to the surfaces
 * \param[in]  decodedPictureBottomFields        Bottom-field flags parallel to the surfaces
 * \param[in]  decodedPictureCount               Active entries in every source array
 * \param[in]  futureReferences                  TRUE selects later pictures, FALSE selects earlier pictures
 * \param[out] referenceList                     Storage for UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES pictures
 *
 * \return Nonzero number of initialized reference pictures.
 */
static uint64_t BuildReferenceListH264(uint32_t           frameIndex,
                                       const VASurfaceID* decodedPictureSurfaces,
                                       const uint32_t*    decodedPictureFrameIndices,
                                       const uint32_t*    decodedPictureFrameNumbers,
                                       const bool_t*      decodedPictureLongTermReferences,
                                       const bool_t*      decodedPictureTopFields,
                                       const bool_t*      decodedPictureBottomFields,
                                       uint32_t           decodedPictureCount,
                                       bool_t             futureReferences,
                                       VAPictureH264*     referenceList);

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
 * \retval None
 */
static void ValidatePredictionWeightsH264(const Config_t* config, const Gfx_Encoder_EncodeInfo_t* encodeInfo, VAEncPictureType pictureType);

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
 * \retval None
 */
static void UpdateDecodedPictureBuffer(Gfx_Encoder_t* encoder, Utl_H264_ReferenceType_t referenceType, VASurfaceID reconstructionSurfaceId, uint32_t frameIndex, bool_t topField, bool_t bottomField);

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
 * \retval None
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

/*************************************************************************************************/
/**
 * \brief Collects the pending encoded access unit.
 *
 * \param[in,out] encoder        Encoder with at most one pending access unit
 * \param[out]    codedBytes Caller-owned destination used only during this call
 * \param[in]     codedCapacity Destination capacity in bytes
 * \param[out]    codedSize Receives access-unit size in bytes
 *
 * \retval TRUE  A successful encode result is available.
 * \retval FALSE No result is pending.
 */
static bool_t GetStatus(Gfx_Encoder_t* encoder, void* codedBytes, uint64_t codedCapacity, uint64_t* codedSize);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

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
    LOGASSERT(createInfo->h264Profile >= GFX_ENCODER_H264_PROFILE_AUTO && createInfo->h264Profile <= GFX_ENCODER_H264_PROFILE_HIGH);
    LOGASSERT(createInfo->entrypoint >= GFX_ENCODER_ENTRYPOINT_SLICE && createInfo->entrypoint <= GFX_ENCODER_ENTRYPOINT_AUTO);
    LOGASSERT(0 != createInfo->frameRateNumerator);
    LOGASSERT(0 != createInfo->frameRateDenominator);
    LOGASSERT(createInfo->frameRateNumerator <= UINT16_MAX);
    LOGASSERT(createInfo->frameRateDenominator <= UINT16_MAX);
    LOGASSERT(createInfo->rateControl >= GFX_ENCODER_RATE_CONTROL_NONE && createInfo->rateControl <= GFX_ENCODER_RATE_CONTROL_TRANSPORT_CONTROLLED_BIT_RATE);
    LOGASSERT(0 == (createInfo->packedHeaders & ~(GFX_ENCODER_PACKED_HEADER_SEQUENCE | GFX_ENCODER_PACKED_HEADER_PICTURE)));
    LOGASSERT(createInfo->targetBitRate <= createInfo->maximumBitRate);
    LOGASSERT((0 == createInfo->targetBitRate) == (0 == createInfo->maximumBitRate));
    LOGASSERT(createInfo->macroblockRateControlMode <= 2u);
    LOGASSERT(createInfo->frameToleranceMode <= 2u);

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
        LOGFATAL("Unsupported encoder rate-control mode!");
    }
    LOGASSERT(0 != createInfo->ipPeriod);
    LOGASSERT(0 != createInfo->sliceCount);
    LOGASSERT(createInfo->sliceCount <= GFX_ENCODER_MAX_SLICES);

    Utl_H264_DeriveCodedDimensions(createInfo->surface->width, createInfo->surface->height, createInfo->interlaced, &width_in_mbs, &height_in_mbs, &padded_width, &padded_height);
    macroblock_count = (uint64_t)width_in_mbs * height_in_mbs;
    LOGASSERT(createInfo->sliceCount <= macroblock_count);
    LOGASSERT(width_in_mbs <= UINT16_MAX && height_in_mbs <= UINT16_MAX);
    LOGASSERT((uint64_t)createInfo->sliceCount * sizeof(VAEncSliceParameterBufferH264) <= UINT32_MAX);
    LOGASSERT(createInfo->intraIdrPeriod <= UINT16_MAX);
    LOGASSERT(createInfo->maxReferenceFrames <= UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES);
    LOGASSERT(UTL_H264_MAX_QUANTIZATION_PARAMETER >= createInfo->quantizationParameter);
    LOGASSERT(FALSE != createInfo->interlaced || FALSE == createInfo->mbAdaptiveFrameField);
    LOGASSERT((NULL == createInfo->scalingList4x4 && NULL == createInfo->scalingList8x8) || (NULL != createInfo->scalingList4x4 && NULL != createInfo->scalingList8x8));
    LOGASSERT(NULL == createInfo->scalingList4x4 || 0 == (createInfo->packedHeaders & (GFX_ENCODER_PACKED_HEADER_SEQUENCE | GFX_ENCODER_PACKED_HEADER_PICTURE)));
    LOGASSERT((0 == createInfo->sampleAspectRatioWidth && 0 == createInfo->sampleAspectRatioHeight) || (0 != createInfo->sampleAspectRatioWidth && 0 != createInfo->sampleAspectRatioHeight));
    LOGASSERT((FALSE == createInfo->restrictMotionVectors && 0 == createInfo->maxMotionVectorLengthHorizontal && 0 == createInfo->maxMotionVectorLengthVertical) ||
              (FALSE != createInfo->restrictMotionVectors && 0 != createInfo->maxMotionVectorLengthHorizontal && 0 != createInfo->maxMotionVectorLengthVertical));
    LOGASSERT(createInfo->weightedBipredIdc <= UTL_H264_MAX_WEIGHTED_BIPRED_IDC);
    LOGASSERT(createInfo->chromaQpIndexOffset >= UTL_H264_MIN_CHROMA_QP_INDEX_OFFSET && createInfo->chromaQpIndexOffset <= UTL_H264_MAX_CHROMA_QP_INDEX_OFFSET);
    LOGASSERT(createInfo->secondChromaQpIndexOffset >= UTL_H264_MIN_CHROMA_QP_INDEX_OFFSET && createInfo->secondChromaQpIndexOffset <= UTL_H264_MAX_CHROMA_QP_INDEX_OFFSET);
    LOGASSERT(createInfo->cabacInitIdc <= UTL_H264_MAX_CABAC_INIT_IDC);
    LOGASSERT(FALSE != createInfo->useCabac || 0 == createInfo->cabacInitIdc);
    LOGASSERT(createInfo->deblockingAlphaOffsetDiv2 >= UTL_H264_MIN_DEBLOCKING_FILTER_OFFSET_DIV2 && createInfo->deblockingAlphaOffsetDiv2 <= UTL_H264_MAX_DEBLOCKING_FILTER_OFFSET_DIV2);
    LOGASSERT(createInfo->deblockingBetaOffsetDiv2 >= UTL_H264_MIN_DEBLOCKING_FILTER_OFFSET_DIV2 && createInfo->deblockingBetaOffsetDiv2 <= UTL_H264_MAX_DEBLOCKING_FILTER_OFFSET_DIV2);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Captures caller configuration before any encoder resources are allocated.
 */
static void InitConfig(Config_t* config, const Gfx_Encoder_CreateInfo_t* createInfo)
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
        (void)memcpy(config->scalingList4x4, createInfo->scalingList4x4, sizeof(config->scalingList4x4));
        (void)memcpy(config->scalingList8x8, createInfo->scalingList8x8, sizeof(config->scalingList8x8));
    }
    else
    {
        (void)memset(config->scalingList4x4, 0, sizeof(config->scalingList4x4));
        (void)memset(config->scalingList8x8, 0, sizeof(config->scalingList8x8));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint32_t GetVaapiRateControl(Gfx_Encoder_RateControl_t rateControl, bool_t macroblockRateControl, bool_t parallelRateControl)
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
        LOGFATAL("Unsupported VA rate-control mode!");
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
        LOGFATAL("Unsupported H.264 encoder profile!");
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
        LOGFATAL("Unsupported H.264 encoder profile!");
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
        LOGFATAL("Unsupported encoder entrypoint!");
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
        LOGFATAL("Unsupported H.264 picture type!");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static VAProfile SelectVaapiProfile(VADisplay dpy, VAProfile requestedProfile, Gfx_Encoder_Entrypoint_t requestedEntrypoint, VAEntrypoint preferredEntrypoint, uint8_t sliceType, bool_t interlaced, bool_t useCabac, bool_t use8x8Transform, bool_t scalingListsPresent, bool_t useWeightedPrediction, uint8_t weightedBipredIdc, bool_t redundantPictures, int8_t chromaQpIndexOffset, int8_t secondChromaQpIndexOffset, VAEntrypoint* entrypoint, uint8_t* profileIdc, uint8_t* constraintSetFlags)
{
    const VAProfile* profiles;
    uint32_t         profile_count;
    uint32_t         profile_index;
    uint8_t          profile_idc;
    uint8_t          constraint_set_flags;
    VAProfile        candidate_profile;
    VAProfile        selected_profile;
    VAProfile        fallback_profile;
    bool_t           profile_coding_compatible;
    bool_t           entrypoint_supported;
    VAConfigAttrib   references;
    uint32_t         max_list1_references;

    selected_profile = VAProfileNone;
    fallback_profile = VAProfileNone;
    (*entrypoint)    = VAEntrypointEncSlice;

    if (VAProfileNone == requestedProfile)
    {
        // Only AUTO may move through the preferred profile list.
        profiles      = c_vaPreferredProfiles;
        profile_count = COUNT_OF(c_vaPreferredProfiles);
    }
    else
    {
        profiles      = &requestedProfile;
        profile_count = 1;
    }

    for (profile_index = 0; profile_index < profile_count; ++profile_index)
    {
        candidate_profile = profiles[profile_index];
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
            entrypoint_supported = Gfx_Vaapi_GetEntrypointSupport(dpy, candidate_profile, preferredEntrypoint);
            if (entrypoint_supported && (UTL_H264_SLICE_TYPE_B == sliceType))
            {
                // Profile support alone does not guarantee list-1 references for B pictures.
                references.type  = VAConfigAttribEncMaxRefFrames;
                references.value = 0;
                Gfx_Vaapi_GetConfigAttributes(dpy, candidate_profile, preferredEntrypoint, &references, 1);
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
                    entrypoint_supported = Gfx_Vaapi_GetEntrypointSupport(dpy, candidate_profile, VAEntrypointEncSlice);
                    if (entrypoint_supported && (UTL_H264_SLICE_TYPE_B == sliceType))
                    {
                        references.type  = VAConfigAttribEncMaxRefFrames;
                        references.value = 0;
                        Gfx_Vaapi_GetConfigAttributes(dpy, candidate_profile, VAEntrypointEncSlice, &references, 1);
                        max_list1_references = references.value >> MAX_LIST1_REFERENCES_SHIFT;
                        entrypoint_supported = (VA_ATTRIB_NOT_SUPPORTED != references.value) && (0 != max_list1_references);
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
static int32_t SelectVaapiConfig(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint, uint32_t format, Gfx_Encoder_RateControl_t rateControl, bool_t macroblockRateControl, bool_t parallelRateControl, uint32_t packedHeaders, VAConfigAttrib* attributes)
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
    (void)memcpy(supported, attributes, sizeof(supported));
    // Query into a copy: the original values describe the exact requested feature masks.
    Gfx_Vaapi_GetConfigAttributes(dpy, profile, entrypoint, supported, (int32_t)COUNT_OF(supported));
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

static void CreateVaapiEncoderResources(VADisplay        dpy,
                                        VAContextID      context,
                                        VAProfile        profile,
                                        uint8_t          profileIdc,
                                        uint8_t          constraintSetFlags,
                                        const Config_t*  config,
                                        uint32_t         codedBufferCapacity,
                                        VABufferID*      codedBufId,
                                        VABufferID*      sequenceParameterBufId,
                                        VABufferID*      pictureParameterBufId,
                                        VABufferID*      sliceParameterBufId,
                                        VABufferID*      packedHeaderParameterBufId,
                                        VABufferID*      packedHeaderDataBufId,
                                        VABufferID*      packedPictureHeaderParameterBufId,
                                        VABufferID*      packedPictureHeaderDataBufId,
                                        VABufferID*      rateControlBufId,
                                        VABufferID*      frameRateBufId)
{
    VAEncSequenceParameterBufferH264 sps;
    uint32_t                         default_reference_count_l0;
    uint32_t                         default_reference_count_l1;
    Utl_H264_SpsParams_t             sps_parameters;
    Utl_H264_PpsParams_t             pps_parameters;
    byte_t                           packed_header_data[ENCODER_PARAMETER_SET_CAPACITY];
    uint64_t                         packed_header_size;
    uint32_t                         packed_header_bit_count;
    uint32_t                         maximum_frame_number;
    uint32_t                         maximum_pic_order_count_lsb;

    Gfx_Vaapi_CreateCodedBuffer(dpy, context, codedBufferCapacity, codedBufId);
    Gfx_Vaapi_CreateRateControlBuffer(dpy,
                                      context,
                                      config->targetBitRate,
                                      config->maximumBitRate,
                                      config->rateControlWindowMilliseconds,
                                      config->initialQuantizationParameter,
                                      config->minimumQuantizationParameter,
                                      config->maximumQuantizationParameter,
                                      config->basicUnitSize,
                                      config->resetRateControl,
                                      config->disableFrameSkip,
                                      config->disableBitStuffing,
                                      config->macroblockRateControlMode,
                                      config->temporalLayer,
                                      config->constantFrameSizeIntraFrames,
                                      config->parallelRateControl,
                                      config->enableDynamicScaling,
                                      config->frameToleranceMode,
                                      config->intelligentConstantQualityFactor,
                                      config->qualityFactor,
                                      config->targetFrameSize,
                                      rateControlBufId);
    Gfx_Vaapi_CreateFrameRateBuffer(dpy, context, (uint16_t)config->frameRateNumerator, (uint16_t)config->frameRateDenominator, config->temporalLayer, frameRateBufId);
    Gfx_Vaapi_InitSequenceParameterBufferH264(&sps,
                                              profile,
                                              UTL_H264_MIN_BIT_DEPTH,
                                              UTL_H264_MIN_BIT_DEPTH,
                                              config->width,
                                              config->height,
                                              config->frameRateNumerator,
                                              config->frameRateDenominator,
                                              config->intraPeriod,
                                              config->intraIdrPeriod,
                                              config->ipPeriod,
                                              config->maximumBitRate,
                                              config->maxReferenceFrames,
                                              config->interlaced,
                                              config->mbAdaptiveFrameField,
                                              config->scalingListsPresent ? config->scalingList4x4 : NULL,
                                              config->scalingListsPresent ? config->scalingList8x8 : NULL,
                                              config->fixedFrameRate,
                                              config->sampleAspectRatioWidth,
                                              config->sampleAspectRatioHeight,
                                              config->restrictMotionVectors,
                                              config->maxMotionVectorLengthHorizontal,
                                              config->maxMotionVectorLengthVertical);

    Gfx_Vaapi_CreateSequenceParameterBufferH264(dpy, context, &sps, sequenceParameterBufId);
    Gfx_Vaapi_CreateBuffer(dpy, context, VAEncPictureParameterBufferType, (uint32_t)sizeof(VAEncPictureParameterBufferH264), 1, NULL, pictureParameterBufId);
    Gfx_Vaapi_CreateBuffer(dpy, context, VAEncSliceParameterBufferType, (uint32_t)sizeof(VAEncSliceParameterBufferH264), config->sliceCount, NULL, sliceParameterBufId);
    maximum_frame_number        = Utl_H264_DeriveMaximumFrameNumber(config->intraIdrPeriod);
    maximum_pic_order_count_lsb = Utl_H264_DeriveMaximumPicOrderCntLsb(maximum_frame_number);
    if (config->packedHeaders & GFX_ENCODER_PACKED_HEADER_SEQUENCE)
    {
        Utl_H264_InitSPS(&sps_parameters,
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
        packed_header_size      = Utl_H264_InsertSPSNALPacket(packed_header_data, sizeof(packed_header_data), &sps_parameters);
        packed_header_bit_count = (uint32_t)packed_header_size * BYTE_TO_BITS;
        LOGASSERT(0 != packed_header_size);

        Gfx_Vaapi_CreatePackedHeaderBuffers(dpy, context, VAEncPackedHeaderSequence, packed_header_bit_count, TRUE, packed_header_data, packedHeaderParameterBufId, packedHeaderDataBufId);
    }
    else
    {
        (*packedHeaderParameterBufId) = VA_INVALID_ID;
        (*packedHeaderDataBufId)      = VA_INVALID_ID;
    }
    if (config->packedHeaders & GFX_ENCODER_PACKED_HEADER_PICTURE)
    {
        Utl_H264_DeriveActiveReferenceCounts(UTL_H264_SLICE_TYPE_I, 0, &default_reference_count_l0, &default_reference_count_l1);
        Utl_H264_InitPPS(&pps_parameters,
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
                scaling_list_coefficients = config->scalingList4x4 + (list_index * UTL_H264_SCALING_LIST_4X4_ELEMENT_COUNT);
                Utl_H264_InitScalingList(&pps_parameters.scalingLists[list_index], scaling_list_coefficients, UTL_H264_SCALING_LIST_4X4_ELEMENT_COUNT);
            }
            if (config->use8x8Transform)
            {
                for (list_index = 0; list_index < UTL_H264_SCALING_LIST_8X8_COUNT; ++list_index)
                {
                    scaling_list_coefficients = config->scalingList8x8 + (list_index * UTL_H264_SCALING_LIST_8X8_ELEMENT_COUNT);
                    Utl_H264_InitScalingList(&pps_parameters.scalingLists[UTL_H264_SCALING_LIST_4X4_COUNT + list_index], scaling_list_coefficients, UTL_H264_SCALING_LIST_8X8_ELEMENT_COUNT);
                }
            }
        }
        packed_header_size      = Utl_H264_InsertPPSNALPacket(packed_header_data, sizeof(packed_header_data), &pps_parameters);
        packed_header_bit_count = (uint32_t)packed_header_size * BYTE_TO_BITS;
        LOGASSERT(0 != packed_header_size);

        Gfx_Vaapi_CreatePackedHeaderBuffers(dpy,
                                            context,
                                            VAEncPackedHeaderPicture,
                                            packed_header_bit_count,
                                            TRUE,
                                            packed_header_data,
                                            packedPictureHeaderParameterBufId,
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
    uint8_t          slice_type;
    VAProfile        requested_profile;
    VAProfile        selected_profile;
    VAEntrypoint     entrypoint;
    VAEntrypoint     preferred_entrypoint;
    VAConfigID       config_id;
    VAConfigAttrib   attributes[CONFIG_ATTRIBUTE_CAPACITY];
    int32_t          attribute_count;
    int32_t          scan_mode;
    bool_t           pixel_format_supported;
    VASurfaceAttrib  reconstruction_attributes[2];
    VASurfaceID      render_targets[RENDER_TARGET_CAPACITY];
    uint32_t         reconstruction_surface_index;

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

    attribute_count = SelectVaapiConfig(dpy, selected_profile, entrypoint, format, rateControl, macroblockRateControl, parallelRateControl, packedHeaders, attributes);
    Gfx_Vaapi_CreateConfig(dpy, selected_profile, entrypoint, attributes, attribute_count, &config_id);

    pixel_format_supported = Gfx_Vaapi_GetSurfacePixelFormatSupport(dpy, config_id, pixelFormat);
    LOGASSERT(FALSE != pixel_format_supported);

    (void)memset(reconstruction_attributes, 0, sizeof(reconstruction_attributes));
    reconstruction_attributes[0].type          = VASurfaceAttribPixelFormat;
    reconstruction_attributes[0].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    reconstruction_attributes[0].value.type    = VAGenericValueTypeInteger;
    reconstruction_attributes[0].value.value.i = (int32_t)pixelFormat;
    reconstruction_attributes[1].type          = VASurfaceAttribUsageHint;
    reconstruction_attributes[1].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    reconstruction_attributes[1].value.type    = VAGenericValueTypeInteger;
    reconstruction_attributes[1].value.value.i = VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER;
    Gfx_Vaapi_CreateSurfaces(dpy, format, width, height, reconstructionSurfaces, reconstructionSurfaceCount, reconstruction_attributes, (uint32_t)COUNT_OF(reconstruction_attributes));

    // Put the input surface first, then append the reconstruction surfaces.
    render_targets[0] = surfaceId;
    for (reconstruction_surface_index = 0; reconstruction_surface_index < reconstructionSurfaceCount; ++reconstruction_surface_index)
    {
        render_targets[INPUT_SURFACE_COUNT + reconstruction_surface_index] = reconstructionSurfaces[reconstruction_surface_index];
    }

    Gfx_Vaapi_CreateContext(dpy, config_id, width, height, scan_mode, render_targets, (int32_t)(INPUT_SURFACE_COUNT + reconstructionSurfaceCount), context);

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
    Config_t    config;
    uint64_t    coded_buffer_capacity;
    VAConfigID  config_id;
    VAContextID context;
    VAProfile   profile;
    uint8_t     profile_idc;
    uint8_t     constraint_set_flags;
    VASurfaceID reconstruction_surfaces[RECONSTRUCTION_SURFACE_CAPACITY];
    VABufferID  coded_buf_id;
    VABufferID  sequence_parameter_buf_id;
    VABufferID  picture_parameter_buf_id;
    VABufferID  slice_parameter_buf_id;
    VABufferID  packed_header_parameter_buf_id;
    VABufferID  packed_header_data_buf_id;
    VABufferID  packed_picture_header_parameter_buf_id;
    VABufferID  packed_picture_header_data_buf_id;
    VABufferID  rate_control_buf_id = VA_INVALID_ID;
    VABufferID  frame_rate_buf_id   = VA_INVALID_ID;

    InitConfig(&config, createInfo);

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

    coded_buffer_capacity = Gfx_Vaapi_GetCodedSurfaceSize(createInfo->surface->va.format, config.width, config.height, config.interlaced);
    coded_buffer_capacity += (2 * ENCODER_PARAMETER_SET_CAPACITY); // Reserve space for one SPS and one PPS NAL unit.
    LOGASSERT(coded_buffer_capacity <= UINT32_MAX);

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

    encoder->config             = config;
    encoder->iqMatrixBufId      = VA_INVALID_ID;
    if (config.scalingListsPresent)
    {
        VAIQMatrixBufferH264 matrix;
        Gfx_Vaapi_InitIQMatrixBufferH264(&matrix, config.scalingList4x4, config.scalingList8x8);
        Gfx_Vaapi_CreateBuffer(createInfo->surface->va.dpy, context, VAIQMatrixBufferType, (uint32_t)sizeof(matrix), 1, &matrix, &encoder->iqMatrixBufId);
    }
    encoder->surface            = createInfo->surface;
    encoder->dpy                = createInfo->surface->va.dpy;
    encoder->profile            = profile;
    encoder->profileIdc         = profile_idc;
    encoder->constraintSetFlags = constraint_set_flags;
    encoder->configId           = config_id;
    encoder->context            = context;
    (void)memcpy(encoder->reconstructionSurfaces, reconstruction_surfaces, sizeof(reconstruction_surfaces));
    encoder->codedBufId                        = coded_buf_id;
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
static void ResetIdrState(uint32_t frameIndex, uint32_t* decodedPictureCount, uint32_t* idrFrameIndex, uint16_t* idrPicId)
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
static bool_t IsDecodedPictureSurface(const VASurfaceID* decodedPictureSurfaces, uint32_t decodedPictureCount, VASurfaceID surfaceId)
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
static VASurfaceID GetReconstructionSurfaceId(const VASurfaceID* reconstructionSurfaces, uint32_t reconstructionSurfaceCount, const VASurfaceID* decodedPictureSurfaces, uint32_t decodedPictureCount)
{
    uint32_t    reconstruction_surface_index;
    bool_t      surface_in_use;

    for (reconstruction_surface_index = 0; reconstruction_surface_index < reconstructionSurfaceCount; ++reconstruction_surface_index)
    {
        surface_in_use = IsDecodedPictureSurface(decodedPictureSurfaces, decodedPictureCount, reconstructionSurfaces[reconstruction_surface_index]);
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
static uint64_t BuildReferenceListH264(uint32_t           frameIndex,
                                       const VASurfaceID* decodedPictureSurfaces,
                                       const uint32_t*    decodedPictureFrameIndices,
                                       const uint32_t*    decodedPictureFrameNumbers,
                                       const bool_t*      decodedPictureLongTermReferences,
                                       const bool_t*      decodedPictureTopFields,
                                       const bool_t*      decodedPictureBottomFields,
                                       uint32_t           decodedPictureCount,
                                       bool_t             futureReferences,
                                       VAPictureH264*     referenceList)
{
    bool_t   has_requested_direction;
    uint32_t decoded_picture_index;
    uint64_t reference_count;
    uint64_t reference_indices[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];
    uint64_t reference_index;
    uint64_t selected_index;

    has_requested_direction = FALSE;

    // A B picture may use an earlier reference in both lists (low-delay B).
    // The utility deliberately requires at least one reference in its requested direction.
    for (decoded_picture_index = 0; decoded_picture_index < decodedPictureCount; ++decoded_picture_index)
    {
        if (futureReferences)
        {
            if (decodedPictureFrameIndices[decoded_picture_index] > frameIndex)
            {
                has_requested_direction = TRUE;
            }
        }
        else
        {
            if (decodedPictureFrameIndices[decoded_picture_index] < frameIndex)
            {
                has_requested_direction = TRUE;
            }
        }
    }
    if ((FALSE == has_requested_direction) && futureReferences)
    {
        futureReferences = FALSE;
    }
    reference_count = Utl_H264_DeriveReferenceListIndices(frameIndex, decodedPictureFrameIndices, decodedPictureCount, futureReferences, reference_indices);
    for (reference_index = 0; reference_index < reference_count; ++reference_index)
    {
        selected_index = reference_indices[reference_index];
        Gfx_Vaapi_InitPictureH264(&referenceList[reference_index],
                                  decodedPictureSurfaces[selected_index],
                                  decodedPictureFrameIndices[selected_index],
                                  REFERENCE_FRAME_NUM_MASK,
                                  REFERENCE_PICTURE,
                                  decodedPictureLongTermReferences[selected_index],
                                  decodedPictureTopFields[selected_index],
                                  decodedPictureBottomFields[selected_index],
                                  REFERENCE_NON_EXISTING);
        referenceList[reference_index].frame_idx = decodedPictureFrameNumbers[selected_index];
    }

    return reference_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ValidatePredictionWeightsH264(const Config_t* config, const Gfx_Encoder_EncodeInfo_t* encodeInfo, VAEncPictureType pictureType)
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
 *  None
 */
static void UpdateDecodedPictureBuffer(Gfx_Encoder_t* encoder, Utl_H264_ReferenceType_t referenceType, VASurfaceID reconstructionSurfaceId, uint32_t frameIndex, bool_t topField, bool_t bottomField)
{
    if (encoder->decodedPictureCount == encoder->config.maxReferenceFrames)
    {
        // Evict the oldest reference and keep its surface and metadata arrays aligned.
        encoder->decodedPictureCount--;
        (void)memmove(encoder->decodedPictureFrameNumbers,       &encoder->decodedPictureFrameNumbers[1],       encoder->decodedPictureCount * sizeof(uint32_t));
        (void)memmove(encoder->decodedPictureSurfaces,           &encoder->decodedPictureSurfaces[1],           encoder->decodedPictureCount * sizeof(VASurfaceID));
        (void)memmove(encoder->decodedPictureFrameIndices,       &encoder->decodedPictureFrameIndices[1],       encoder->decodedPictureCount * sizeof(uint32_t));
        (void)memmove(encoder->decodedPictureLongTermReferences, &encoder->decodedPictureLongTermReferences[1], encoder->decodedPictureCount * sizeof(bool_t));
        (void)memmove(encoder->decodedPictureTopFields,          &encoder->decodedPictureTopFields[1],          encoder->decodedPictureCount * sizeof(bool_t));
        (void)memmove(encoder->decodedPictureBottomFields,       &encoder->decodedPictureBottomFields[1],       encoder->decodedPictureCount * sizeof(bool_t));
        (void)memmove(encoder->decodedPictureNonExisting,        &encoder->decodedPictureNonExisting[1],        encoder->decodedPictureCount * sizeof(bool_t));
    }
    LOGASSERT(encoder->decodedPictureCount < encoder->config.maxReferenceFrames);

    // Append the submitted reconstruction as the newest reference.
    encoder->decodedPictureSurfaces[encoder->decodedPictureCount]           = reconstructionSurfaceId;
    encoder->decodedPictureFrameIndices[encoder->decodedPictureCount]       = frameIndex;
    encoder->decodedPictureFrameNumbers[encoder->decodedPictureCount]       = encoder->nextFrameNum;
    encoder->decodedPictureLongTermReferences[encoder->decodedPictureCount] = UTL_H264_REFERENCE_TYPE_LONG_TERM == referenceType;
    encoder->decodedPictureTopFields[encoder->decodedPictureCount]          = topField;
    encoder->decodedPictureBottomFields[encoder->decodedPictureCount]       = bottomField;
    encoder->decodedPictureNonExisting[encoder->decodedPictureCount]        = FALSE;
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
    uint32_t                        decoded_picture_index;
    VABufferID                      parameter_buf_ids[PARAMETER_BUFFER_CAPACITY];
    uint32_t                        parameter_buf_id_count;
    VASurfaceID                     reconstruction_surface_id;
    VAPictureH264                   reference_list_0[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];
    VAPictureH264                   reference_list_1[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];
    uint64_t                        reference_list_0_count;
    uint64_t                        reference_list_1_count;
    VAEncPictureType                va_picture_type;
    uint8_t                         slice_type;
    bool_t                          profile_coding_compatible;
    bool_t                          idr_picture;
    bool_t                          top_field;
    bool_t                          bottom_field;

    parameter_buf_id_count = 0;
    reference_list_0_count = 0;
    reference_list_1_count = 0;

    LOGASSERT(encoder->decodedPictureCount <= encoder->config.maxReferenceFrames);
    LOGASSERT(encodeInfo->fieldMode >= UTL_H264_FIELD_MODE_FRAME && encodeInfo->fieldMode <= UTL_H264_FIELD_MODE_BOTTOM);
    LOGASSERT(encodeInfo->referenceType >= UTL_H264_REFERENCE_TYPE_SHORT_TERM && encodeInfo->referenceType <= UTL_H264_REFERENCE_TYPE_LONG_TERM);

    va_picture_type = GetVaapiPictureTypeH264(encodeInfo->pictureType, &idr_picture);
    ValidatePredictionWeightsH264(&encoder->config, encodeInfo, va_picture_type);

    slice_type = Utl_H264_DeriveSliceType(encodeInfo->pictureType);
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
        ResetIdrState(encodeInfo->frameIndex, &encoder->decodedPictureCount, &encoder->idrFrameIndex, &encoder->idrPicId);
    }

    LOGASSERT(encodeInfo->frameIndex >= encoder->idrFrameIndex);
    frame_index = encodeInfo->frameIndex - encoder->idrFrameIndex;

    reconstruction_surface_id = GetReconstructionSurfaceId(encoder->reconstructionSurfaces, (uint32_t)COUNT_OF(encoder->reconstructionSurfaces), encoder->decodedPictureSurfaces, encoder->decodedPictureCount);

    if ((VAEncPictureTypeBidirectional == va_picture_type) || (VAEncPictureTypePredictive == va_picture_type))
    {
        reference_list_0_count = BuildReferenceListH264(frame_index,
                                                        encoder->decodedPictureSurfaces,
                                                        encoder->decodedPictureFrameIndices,
                                                        encoder->decodedPictureFrameNumbers,
                                                        encoder->decodedPictureLongTermReferences,
                                                        encoder->decodedPictureTopFields,
                                                        encoder->decodedPictureBottomFields,
                                                        encoder->decodedPictureCount,
                                                        FALSE,
                                                        reference_list_0);
    }
    if (VAEncPictureTypeBidirectional == va_picture_type)
    {
        reference_list_1_count = BuildReferenceListH264(frame_index,
                                                        encoder->decodedPictureSurfaces,
                                                        encoder->decodedPictureFrameIndices,
                                                        encoder->decodedPictureFrameNumbers,
                                                        encoder->decodedPictureLongTermReferences,
                                                        encoder->decodedPictureTopFields,
                                                        encoder->decodedPictureBottomFields,
                                                        encoder->decodedPictureCount,
                                                        TRUE,
                                                        reference_list_1);
    }

    Gfx_Vaapi_InitPictureParameterBufferH264(&pps,
                                             encoder->profile,
                                             encoder->config.intraIdrPeriod,
                                             encoder->config.maxReferenceFrames,
                                             encoder->config.interlaced,
                                             reconstruction_surface_id,
                                             encoder->codedBufId,
                                             frame_index,
                                             va_picture_type,
                                             idr_picture,
                                             top_field,
                                             bottom_field,
                                             encodeInfo->referencePicture,
                                             UTL_H264_REFERENCE_TYPE_LONG_TERM == encodeInfo->referenceType,
                                             encodeInfo->lastFrame,
                                             encoder->config.quantizationParameter,
                                             encoder->config.useCabac,
                                             encoder->config.useWeightedPrediction,
                                             encoder->config.weightedBipredIdc,
                                             encoder->config.constrainedIntraPrediction,
                                             encoder->config.use8x8Transform,
                                             encoder->config.redundantPictures,
                                             encoder->config.picOrderPresent,
                                             encoder->config.scalingListsPresent ? encoder->config.scalingList4x4 : NULL,
                                             encoder->config.scalingListsPresent ? encoder->config.scalingList8x8 : NULL,
                                             encoder->config.chromaQpIndexOffset,
                                             encoder->config.secondChromaQpIndexOffset,
                                             encoder->decodedPictureSurfaces,
                                             encoder->decodedPictureFrameIndices,
                                             encoder->decodedPictureLongTermReferences,
                                             encoder->decodedPictureTopFields,
                                             encoder->decodedPictureBottomFields,
                                             encoder->decodedPictureNonExisting,
                                             encoder->decodedPictureCount);
    // frame_num follows reference decoding order; POC follows display order.
    pps.frame_num         = (uint16_t)encoder->nextFrameNum;
    pps.CurrPic.frame_idx = encoder->nextFrameNum;
    for (decoded_picture_index = 0; decoded_picture_index < encoder->decodedPictureCount; ++decoded_picture_index)
    {
        pps.ReferenceFrames[decoded_picture_index].frame_idx = encoder->decodedPictureFrameNumbers[decoded_picture_index];
    }

    for (slice_index = 0; slice_index < encoder->config.sliceCount; ++slice_index)
    {
        Gfx_Vaapi_InitSliceParameterBufferH264(&encoder->slice[slice_index],
                                               encoder->config.width,
                                               encoder->config.height,
                                               encoder->config.interlaced,
                                               encoder->config.intraIdrPeriod,
                                               frame_index,
                                               va_picture_type,
                                               idr_picture,
                                               encoder->idrPicId,
                                               encoder->config.quantizationParameter,
                                               encoder->config.useCabac,
                                               encoder->decodedPictureCount,
                                               slice_index,
                                               encoder->config.sliceCount,
                                               VA_INVALID_ID,
                                               encodeInfo->quantizationParameterDelta,
                                               encodeInfo->directSpatialMotionVectorPrediction,
                                               reference_list_0,
                                               reference_list_0_count,
                                               reference_list_1,
                                               reference_list_1_count,
                                               encodeInfo->lumaLog2WeightDenominator,
                                               encodeInfo->chromaLog2WeightDenominator,
                                               encodeInfo->list0LumaWeightsEnabled,
                                               encodeInfo->list0LumaWeights,
                                               encodeInfo->list0LumaOffsets,
                                               encodeInfo->list0ChromaWeightsEnabled,
                                               encodeInfo->list0ChromaWeights,
                                               encodeInfo->list0ChromaOffsets,
                                               encodeInfo->list1LumaWeightsEnabled,
                                               encodeInfo->list1LumaWeights,
                                               encodeInfo->list1LumaOffsets,
                                               encodeInfo->list1ChromaWeightsEnabled,
                                               encodeInfo->list1ChromaWeights,
                                               encodeInfo->list1ChromaOffsets,
                                               encoder->config.cabacInitIdc,
                                               encoder->config.enableDeblocking,
                                               encoder->config.deblockingAlphaOffsetDiv2,
                                               encoder->config.deblockingBetaOffsetDiv2);
    }
    Gfx_Vaapi_WriteMemory(encoder->dpy, encoder->pictureParameterBufId, &pps, (uint32_t)sizeof(pps));
    Gfx_Vaapi_WriteMemory(encoder->dpy, encoder->sliceParameterBufId, encoder->slice, (uint32_t)(encoder->config.sliceCount * sizeof(VAEncSliceParameterBufferH264)));

    Gfx_Vaapi_BeginPicture(encoder->dpy, encoder->context, image->va.surfaceId);
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
    Gfx_Vaapi_RenderPicture(encoder->dpy, encoder->context, parameter_buf_ids, (int32_t)parameter_buf_id_count);
    Gfx_Vaapi_EndPicture(encoder->dpy, encoder->context);

    image->uploadPending = FALSE;

    if ((encodeInfo->referencePicture) && (0 != encoder->config.maxReferenceFrames))
    {
        UpdateDecodedPictureBuffer(encoder, encodeInfo->referenceType, reconstruction_surface_id, frame_index, top_field, bottom_field);
        encoder->nextFrameNum = (encoder->nextFrameNum + 1u) & Utl_H264_DeriveValueMask(Utl_H264_DeriveMaximumFrameNumber(encoder->config.intraIdrPeriod));
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

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static bool_t GetStatus(Gfx_Encoder_t* encoder, void* codedBytes, uint64_t codedCapacity, uint64_t* codedSize)
{
    VASurfaceStatus surface_status;
    uint32_t        coded_size;

    LOGASSERT(encoder->ready);

    if (FALSE == encoder->framePending)
    {
        (*codedSize) = 0;
        return FALSE;
    }

    Gfx_Vaapi_SyncSurface(encoder->dpy, encoder->surface->target.readImage->va.surfaceId);
    Gfx_Vaapi_QuerySurfaceStatus(encoder->dpy, encoder->surface->target.readImage->va.surfaceId, &surface_status);
    LOGASSERT(VASurfaceReady == surface_status);

    Gfx_Vaapi_CopyCodedBufferSegments(encoder->dpy, encoder->codedBufId, codedBytes, (uint32_t)codedCapacity, &coded_size);
    (*codedSize) = coded_size;
    
    encoder->surface->readPending = FALSE;
    encoder->framePending         = FALSE;

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
