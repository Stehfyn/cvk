#ifndef VULKAN_VA_ZERO_COPY_GFX_VAAPI_H
#define VULKAN_VA_ZERO_COPY_GFX_VAAPI_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <va/va.h>

#include "os/os_types.h"
#include "utl/utl_h264.h"
#include <stddef.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define VACHECK(expression)                 (Gfx_Vaapi_CheckError((expression), #expression, __FILE__, __LINE__))   ///< Evaluates a VA call and reports failures at its source location.
#define VACHECKVERSION(major, minor, micro) (Gfx_Vaapi_CheckVersion((major), (minor), (micro), __FILE__, __LINE__)) ///< Checks for an exact match with the libva header version.

#define VA_LINEAR_ROW_ALIGNMENT      (128U) ///< Row alignment for VA-importable linear storage.
#define VA_MAX_CODED_BUFFER_SEGMENTS (256)  ///< Traversal bound for linked VA coded-buffer segments.
#define VA_MAX_ENTRYPOINTS           (64)   ///< Local VA entrypoint-enumeration capacity.
#define VA_MAX_IMAGE_PLANES          (3)    ///< Maximum plane count supported by image-copy helpers.
#define VA_MAX_PROFILES              (64)   ///< Local VA profile-enumeration capacity.
#define VA_MAX_SURFACE_ATTRIBUTES    (64)   ///< Local VA surface-attribute enumeration capacity.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Converts a failed VA status into a fatal diagnostic at the originating call site.
 *
 * \param[in] status              Status returned by a VA function
 * \param[in] expression          Null-terminated expression text supplied by VACHECK()
 * \param[in] file                Null-terminated source filename containing the call
 * \param[in] line                One-based source line containing the call
 *
 * \retval    None
 */
void Gfx_Vaapi_CheckError(VAStatus status, const char* expression, const char* file, int32_t line);

/*************************************************************************************************/
/**
 * \brief Checks version values against the libva headers used to build this wrapper.
 *
 * \param[in] major               Expected libva header major version
 * \param[in] minor               Expected libva header minor version
 * \param[in] micro               Expected libva header micro version
 * \param[in] file                Unused source-location argument
 * \param[in] line                Unused source-location argument
 *
 * \retval    None
 */
void Gfx_Vaapi_CheckVersion(int32_t major, int32_t minor, int32_t micro, const char* file, int32_t line);

/*************************************************************************************************/
/**
 * \brief Initializes one VA H.264 current or reference picture.
 *
 * \param[out] picture           Receives all picture fields
 * \param[in]  surfaceId           Surface identifier, invalid exactly when nonExisting is TRUE
 * \param[in]  frameIndex        Absolute frame index used for field order counts; zero for an IDR
 * \param[in]  frameNumMask      Wrap mask applied to frame_idx; UINT32_MAX preserves the index
 * \param[in]  referencePicture  TRUE for a reference picture
 * \param[in]  longTermReference TRUE for a long-term reference
 * \param[in]  topField          TRUE for a top field
 * \param[in]  bottomField       TRUE for a bottom field; mutually exclusive with topField
 * \param[in]  nonExisting       TRUE for a missing reference picture
 *
 * \retval None
 */
void Gfx_Vaapi_InitPictureH264(VAPictureH264* picture,
                               VASurfaceID    surfaceId,
                               uint32_t       frameIndex,
                               uint32_t       frameNumMask,
                               bool_t         referencePicture,
                               bool_t         longTermReference,
                               bool_t         topField,
                               bool_t         bottomField,
                               bool_t         nonExisting);

/*************************************************************************************************/
/**
 * \brief Builds VA H.264 sequence parameters, including cropping and VUI fields.
 *
 * \param[out] sequence                            Receives initialized sequence parameters
 * \param[in]  profile                             Supported VA H.264 profile
 * \param[in]  bitDepthLuma                        Actual luma component bit depth
 * \param[in]  bitDepthChroma                      Actual chroma component bit depth
 * \param[in]  width                               Coded width in pixels
 * \param[in]  height                              Coded height in pixels
 * \param[in]  frameRateNumerator                  Nonzero frame-rate numerator
 * \param[in]  frameRateDenominator                Nonzero frame-rate denominator
 * \param[in]  intraPeriod                         Pictures between intra pictures
 * \param[in]  intraIdrPeriod                      Pictures between IDR pictures
 * \param[in]  ipPeriod                            Nonzero display-order I/P distance
 * \param[in]  bitsPerSecond                       Encoder bit rate in bits per second; zero when unused
 * \param[in]  maxReferenceFrames                  Maximum decoded-picture-buffer occupancy
 * \param[in]  interlaced                          Allows frame/field coding instead of requiring progressive frames
 * \param[in]  mbAdaptiveFrameField                Enable MBAFF; valid only when interlaced
 * \param[in]  scalingList4x4                      Six concatenated 16-element lists, or NULL
 * \param[in]  scalingList8x8                      Two concatenated 64-element lists, paired with scalingList4x4
 * \param[in]  fixedFrameRate                      Set fixed_frame_rate_flag in VUI
 * \param[in]  sampleAspectRatioWidth              SAR numerator; zero with height omits SAR
 * \param[in]  sampleAspectRatioHeight             SAR denominator; zero with width omits SAR
 * \param[in]  restrictMotionVectors               Enable VUI motion-vector restrictions
 * \param[in]  maxMotionVectorLengthHorizontal     Maximum horizontal magnitude in quarter-pel units
 * \param[in]  maxMotionVectorLengthVertical       Maximum vertical magnitude in quarter-pel units
 *
 * \retval    None
 */
void Gfx_Vaapi_InitSequenceParameterBufferH264(VAEncSequenceParameterBufferH264* sequence,
                                               VAProfile                         profile,
                                               uint32_t                          bitDepthLuma,
                                               uint32_t                          bitDepthChroma,
                                               uint32_t                          width,
                                               uint32_t                          height,
                                               uint32_t                          frameRateNumerator,
                                               uint32_t                          frameRateDenominator,
                                               uint32_t                          intraPeriod,
                                               uint32_t                          intraIdrPeriod,
                                               uint32_t                          ipPeriod,
                                               uint32_t                          bitsPerSecond,
                                               uint32_t                          maxReferenceFrames,
                                               bool_t                            interlaced,
                                               bool_t                            mbAdaptiveFrameField,
                                               const uint8_t                     scalingList4x4[UTL_H264_SCALING_LIST_4X4_TOTAL_ELEMENT_COUNT],
                                               const uint8_t                     scalingList8x8[UTL_H264_SCALING_LIST_8X8_TOTAL_ELEMENT_COUNT],
                                               bool_t                            fixedFrameRate,
                                               uint32_t                          sampleAspectRatioWidth,
                                               uint32_t                          sampleAspectRatioHeight,
                                               bool_t                            restrictMotionVectors,
                                               uint32_t                          maxMotionVectorLengthHorizontal,
                                               uint32_t                          maxMotionVectorLengthVertical);

/*************************************************************************************************/
/**
 * \brief Builds VA H.264 picture parameters and decoded-picture-buffer references.
 *
 * \param[out] picture                              Receives initialized picture parameters
 * \param[in]  profile                              VA H.264 profile controlling syntax features
 * \param[in]  intraIdrPeriod                       Pictures between IDR pictures
 * \param[in]  maxReferenceFrames                   Maximum decoded-picture-buffer occupancy
 * \param[in]  interlaced                           Allows frame/field coding instead of requiring progressive frames
 * \param[in]  reconstructionSurface                Surface receiving reconstructed pixels
 * \param[in]  codedBufId                           VA buffer receiving the coded access unit
 * \param[in]  frameIndex                           Absolute picture index used for frame_num and POC
 * \param[in]  pictureType                          VA intra, predictive, or bidirectional type
 * \param[in]  idrPicture                           Mark this picture as an instantaneous decoder refresh
 * \param[in]  topField                             Mark this picture as a top field
 * \param[in]  bottomField                          Mark this picture as a bottom field
 * \param[in]  referencePicture                     Retain reconstruction for later prediction
 * \param[in]  longTermReference                    Mark a retained reconstruction as long-term
 * \param[in]  lastPicture                          Add VA end-of-sequence and end-of-stream flags
 * \param[in]  quantizationParameter                Base picture QP in the H.264 range 0..51
 * \param[in]  useCabac                             Enable CABAC entropy coding
 * \param[in]  useWeightedPrediction                Enable weighted P prediction
 * \param[in]  weightedBipredIdc                    Weighted B-prediction mode in the range 0..2
 * \param[in]  constrainedIntraPrediction           Restrict intra prediction to intra-coded neighbors
 * \param[in]  use8x8Transform                      Enable the High-profile 8-by-8 transform
 * \param[in]  redundantPictures                    Enable redundant-picture syntax
 * \param[in]  picOrderPresent                      Include picture-order fields in slice headers
 * \param[in]  scalingList4x4                       Six concatenated 16-element lists, or NULL
 * \param[in]  scalingList8x8                       Two concatenated 64-element lists, or NULL
 * \param[in]  chromaQpIndexOffset                  First chroma QP offset in the range -12..12
 * \param[in]  secondChromaQpIndexOffset            Second chroma QP offset in the range -12..12
 * \param[in]  decodedPictureSurfaces               DPB surface identifiers
 * \param[in]  decodedPictureFrameIndices           Frame indices parallel to the DPB surfaces
 * \param[in]  decodedPictureLongTermReferences     Long-term flags parallel to the DPB surfaces
 * \param[in]  decodedPictureTopFields              Top-field flags parallel to the DPB surfaces
 * \param[in]  decodedPictureBottomFields           Bottom-field flags parallel to the DPB surfaces
 * \param[in]  decodedPictureNonExisting            Non-existing flags parallel to the DPB surfaces
 * \param[in]  decodedPictureCount                  Valid element count in every DPB array
 *
 * \retval    None
 */
void Gfx_Vaapi_InitPictureParameterBufferH264(VAEncPictureParameterBufferH264* picture,
                                              VAProfile                        profile,
                                              uint32_t                         intraIdrPeriod,
                                              uint32_t                         maxReferenceFrames,
                                              bool_t                           interlaced,
                                              VASurfaceID                      reconstructionSurface,
                                              VABufferID                       codedBufId,
                                              uint32_t                         frameIndex,
                                              VAEncPictureType                 pictureType,
                                              bool_t                           idrPicture,
                                              bool_t                           topField,
                                              bool_t                           bottomField,
                                              bool_t                           referencePicture,
                                              bool_t                           longTermReference,
                                              bool_t                           lastPicture,
                                              uint32_t                         quantizationParameter,
                                              bool_t                           useCabac,
                                              bool_t                           useWeightedPrediction,
                                              uint8_t                          weightedBipredIdc,
                                              bool_t                           constrainedIntraPrediction,
                                              bool_t                           use8x8Transform,
                                              bool_t                           redundantPictures,
                                              bool_t                           picOrderPresent,
                                              const uint8_t                    scalingList4x4[UTL_H264_SCALING_LIST_4X4_TOTAL_ELEMENT_COUNT],
                                              const uint8_t                    scalingList8x8[UTL_H264_SCALING_LIST_8X8_TOTAL_ELEMENT_COUNT],
                                              int8_t                           chromaQpIndexOffset,
                                              int8_t                           secondChromaQpIndexOffset,
                                              const VASurfaceID                decodedPictureSurfaces[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES],
                                              const uint32_t                   decodedPictureFrameIndices[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES],
                                              const bool_t                     decodedPictureLongTermReferences[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES],
                                              const bool_t                     decodedPictureTopFields[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES],
                                              const bool_t                     decodedPictureBottomFields[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES],
                                              const bool_t                     decodedPictureNonExisting[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES],
                                              size_t                           decodedPictureCount);

/*************************************************************************************************/
/**
 * \brief Builds one VA H.264 slice parameter and its active reference lists and weights.
 *
 * \param[out] slice                                Receives initialized slice parameters
 * \param[in]  width                                Coded width in pixels
 * \param[in]  height                               Coded height in pixels
 * \param[in]  interlaced                           Allows frame/field coding instead of requiring progressive frames
 * \param[in]  intraIdrPeriod                       Pictures between IDR pictures
 * \param[in]  frameIndex                           Absolute picture index
 * \param[in]  pictureType                          VA picture type for slice_type
 * \param[in]  idrPicture                           Mark the slice as belonging to an IDR picture
 * \param[in]  idrPicId                             Identifier of this IDR picture; ignored for other pictures
 * \param[in]  pictureQuantizationParameter         Base picture QP
 * \param[in]  useCabac                             Enable CABAC entropy coding
 * \param[in]  decodedPictureCount                  Number of active decoded pictures
 * \param[in]  sliceIndex                           Zero-based slice index
 * \param[in]  sliceCount                           Nonzero total slices partitioning the picture
 * \param[in]  macroblockInfoBufId                  Optional VA macroblock-info buffer identifier
 * \param[in]  quantizationParameterDelta           Signed slice QP adjustment
 * \param[in]  directSpatialMotionVectorPrediction  Select spatial direct prediction for B pictures
 * \param[in]  referenceList0                       Active list-0 entries
 * \param[in]  referenceList0Count                  Valid elements in referenceList0
 * \param[in]  referenceList1                       Active list-1 entries
 * \param[in]  referenceList1Count                  Valid elements in referenceList1
 * \param[in]  lumaLog2WeightDenominator            Base-two luma weight denominator exponent
 * \param[in]  chromaLog2WeightDenominator          Base-two chroma weight denominator exponent
 * \param[in]  list0LumaWeightsEnabled              Enable explicit list-0 luma weights and offsets
 * \param[in]  list0LumaWeights                     One weight per active list-0 reference
 * \param[in]  list0LumaOffsets                     Offsets parallel to list0LumaWeights
 * \param[in]  list0ChromaWeightsEnabled            Enable explicit list-0 chroma weights and offsets
 * \param[in]  list0ChromaWeights                   Interleaved Cb/Cr weights per list-0 reference
 * \param[in]  list0ChromaOffsets                   Interleaved Cb/Cr offsets parallel to the weights
 * \param[in]  list1LumaWeightsEnabled              Enable explicit list-1 luma weights and offsets
 * \param[in]  list1LumaWeights                     One weight per active list-1 reference
 * \param[in]  list1LumaOffsets                     Offsets parallel to list1LumaWeights
 * \param[in]  list1ChromaWeightsEnabled            Enable explicit list-1 chroma weights and offsets
 * \param[in]  list1ChromaWeights                   Interleaved Cb/Cr weights per list-1 reference
 * \param[in]  list1ChromaOffsets                   Interleaved Cb/Cr offsets parallel to the weights
 * \param[in]  cabacInitIdc                         CABAC initialization selector in the range 0..2
 * \param[in]  enableDeblocking                     Enable the in-loop deblocking filter
 * \param[in]  deblockingAlphaOffsetDiv2            Alpha offset divided by two, in the range -6..6
 * \param[in]  deblockingBetaOffsetDiv2             Beta offset divided by two, in the range -6..6
 *
 * \retval    None
 */
void Gfx_Vaapi_InitSliceParameterBufferH264(VAEncSliceParameterBufferH264* slice,
                                            uint32_t                       width,
                                            uint32_t                       height,
                                            bool_t                         interlaced,
                                            uint32_t                       intraIdrPeriod,
                                            uint32_t                       frameIndex,
                                            VAEncPictureType               pictureType,
                                            bool_t                         idrPicture,
                                            uint16_t                       idrPicId,
                                            uint32_t                       pictureQuantizationParameter,
                                            bool_t                         useCabac,
                                            size_t                         decodedPictureCount,
                                            uint32_t                       sliceIndex,
                                            uint32_t                       sliceCount,
                                            VABufferID                     macroblockInfoBufId,
                                            int8_t                         quantizationParameterDelta,
                                            bool_t                         directSpatialMotionVectorPrediction,
                                            const VAPictureH264*           referenceList0,
                                            size_t                         referenceList0Count,
                                            const VAPictureH264*           referenceList1,
                                            size_t                         referenceList1Count,
                                            uint8_t                        lumaLog2WeightDenominator,
                                            uint8_t                        chromaLog2WeightDenominator,
                                            bool_t                         list0LumaWeightsEnabled,
                                            const int16_t*                 list0LumaWeights,
                                            const int16_t*                 list0LumaOffsets,
                                            bool_t                         list0ChromaWeightsEnabled,
                                            const int16_t*                 list0ChromaWeights,
                                            const int16_t*                 list0ChromaOffsets,
                                            bool_t                         list1LumaWeightsEnabled,
                                            const int16_t*                 list1LumaWeights,
                                            const int16_t*                 list1LumaOffsets,
                                            bool_t                         list1ChromaWeightsEnabled,
                                            const int16_t*                 list1ChromaWeights,
                                            const int16_t*                 list1ChromaOffsets,
                                            uint8_t                        cabacInitIdc,
                                            bool_t                         enableDeblocking,
                                            int8_t                         deblockingAlphaOffsetDiv2,
                                            int8_t                         deblockingBetaOffsetDiv2);

/*************************************************************************************************/
/**
 * \brief Copies complete caller scaling lists into a VA H.264 inverse-quantization matrix.
 *
 * \param[out] matrix              Receives both complete scaling-list sets
 * \param[in]  scalingList4x4      Six concatenated 16-element scaling lists
 * \param[in]  scalingList8x8      Two concatenated 64-element scaling lists
 *
 * \retval    None
 */
void Gfx_Vaapi_InitIQMatrixBufferH264(VAIQMatrixBufferH264* matrix,
                                      const uint8_t         scalingList4x4[UTL_H264_SCALING_LIST_4X4_TOTAL_ELEMENT_COUNT],
                                      const uint8_t         scalingList8x8[UTL_H264_SCALING_LIST_8X8_TOTAL_ELEMENT_COUNT]);

/*************************************************************************************************/
/**
 * \brief Initializes a VA display and returns its runtime version.
 *
 * \param[in]  dpy                 Non-NULL platform VA display
 * \param[out] majorVersion        Receives the implementation major version
 * \param[out] minorVersion        Receives the implementation minor version
 *
 * \retval    None
 */
void Gfx_Vaapi_Initialize(VADisplay dpy, int32_t* majorVersion, int32_t* minorVersion);

/*************************************************************************************************/
/**
 * \brief Returns the implementation vendor string for an initialized display.
 *
 * \param[in] dpy                  Initialized VA display
 *
 * \return Driver-owned null-terminated vendor string.
 */
const char* Gfx_Vaapi_QueryVendorString(VADisplay dpy);

/*************************************************************************************************/
/**
 * \brief Returns the maximum number of profiles the display can report.
 *
 * \param[in] dpy                  Initialized VA display
 *
 * \return Positive driver-reported enumeration capacity.
 */
int Gfx_Vaapi_MaxNumProfiles(VADisplay dpy);

/*************************************************************************************************/
/**
 * \brief Returns the maximum number of entrypoints the display can report for one profile.
 *
 * \param[in] dpy                  Initialized VA display
 *
 * \return Positive driver-reported enumeration capacity.
 */
int Gfx_Vaapi_MaxNumEntrypoints(VADisplay dpy);

/*************************************************************************************************/
/**
 * \brief Enumerates configuration profiles exposed by an initialized display.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[out] profileList         Array with VA_MAX_PROFILES elements
 * \param[out] numProfiles         Receives the number of initialized array elements
 *
 * \retval    None
 */
void Gfx_Vaapi_QueryConfigProfiles(VADisplay dpy, VAProfile profileList[VA_MAX_PROFILES], int32_t* numProfiles);

/*************************************************************************************************/
/**
 * \brief Enumerates entrypoints exposed for a VA profile.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  profile             Profile whose entrypoints are queried
 * \param[out] entrypointList      Array with capacity VA_MAX_ENTRYPOINTS
 * \param[out] numEntrypoints      Receives the number of initialized array elements
 *
 * \retval    None
 */
void Gfx_Vaapi_QueryConfigEntrypoints(VADisplay dpy, VAProfile profile, VAEntrypoint entrypointList[VA_MAX_ENTRYPOINTS], int32_t* numEntrypoints);

/*************************************************************************************************/
/**
 * \brief Populates requested configuration attributes for a profile and entrypoint pair.
 *
 * \param[in]     dpy              Initialized VA display
 * \param[in]     profile          Configuration profile
 * \param[in]     entrypoint       Configuration entrypoint
 * \param[in,out] attribList       Array whose types select attributes and whose values receive support
 * \param[in]     numAttribs       Number of elements in attribList
 *
 * \retval    None
 */
void Gfx_Vaapi_GetConfigAttributes(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib* attribList, int32_t numAttribs);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VA configuration for a profile and entrypoint.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  profile             Supported profile
 * \param[in]  entrypoint          Supported entrypoint
 * \param[in]  attribList          Requested configuration attributes, or NULL when numAttribs is zero
 * \param[in]  numAttribs          Number of elements in attribList
 * \param[out] configId            Receives the created configuration identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_CreateConfig(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib* attribList, int32_t numAttribs, VAConfigID* configId);

/*************************************************************************************************/
/**
 * \brief Queries supported surface attributes for a VA configuration.
 *
 * \param[in]     dpy              Initialized VA display
 * \param[in]     configId         Valid configuration identifier
 * \param[out]    attribList       Destination array, or NULL to query required count
 * \param[in,out] numAttribs       Capacity on input and populated/required count on output
 *
 * \retval    None
 */
void Gfx_Vaapi_QuerySurfaceAttributes(VADisplay dpy, VAConfigID configId, VASurfaceAttrib* attribList, uint32_t* numAttribs);

/*************************************************************************************************/
/**
 * \brief Creates persistent VA surfaces with the requested format and attributes.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  format              VA_RT_FORMAT_* render-target format
 * \param[in]  width               Nonzero surface width in pixels
 * \param[in]  height              Nonzero surface height in pixels
 * \param[out] surfaces            Array receiving numSurfaces identifiers
 * \param[in]  numSurfaces         Nonzero number of surfaces to create
 * \param[in]  attribList          Surface attributes, or NULL when numAttribs is zero
 * \param[in]  numAttribs          Number of elements in attribList
 *
 * \retval    None
 */
void Gfx_Vaapi_CreateSurfaces(VADisplay dpy, uint32_t format, uint32_t width, uint32_t height, VASurfaceID* surfaces, uint32_t numSurfaces, VASurfaceAttrib* attribList, uint32_t numAttribs);

/*************************************************************************************************/
/**
 * \brief Creates one VA surface with an explicit pixel format.
 *
 * \param[in]  dpy          Initialized VA display
 * \param[in]  format       VA_RT_FORMAT_* render-target format
 * \param[in]  pixelFormat  Nonzero VA four-character pixel format
 * \param[in]  width        Nonzero surface width in pixels
 * \param[in]  height       Nonzero surface height in pixels
 * \param[out] surface      Initially invalid storage receiving the created surface
 *
 * \retval None
 */
void Gfx_Vaapi_CreateSurface(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, VASurfaceID* surface);

/*************************************************************************************************/
/**
 * \brief Creates one encoder surface intended for external-memory export.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  format              VA_RT_FORMAT_* render-target format
 * \param[in]  pixelFormat         VA four-character pixel format
 * \param[in]  width               Nonzero surface width in pixels
 * \param[in]  height              Nonzero surface height in pixels
 * \param[out] surface             Initially invalid storage receiving the created surface
 * \param[in]  additionalAttribList Additional surface attributes, or NULL when numAdditionalAttribs is zero
 * \param[in]  numAdditionalAttribs Number of elements in additionalAttribList
 *
 * \retval    None
 */
void Gfx_Vaapi_CreateExportableSurface(VADisplay              dpy,
                                       uint32_t               format,
                                       uint32_t               pixelFormat,
                                       uint32_t               width,
                                       uint32_t               height,
                                       VASurfaceID*           surface,
                                       const VASurfaceAttrib* additionalAttribList,
                                       uint32_t               numAdditionalAttribs);

/*************************************************************************************************/
/**
 * \brief Exports a VA surface through the requested external-memory handle type.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  surfaceId           Valid surface to export
 * \param[in]  memoryType          VA_SURFACE_ATTRIB_MEM_TYPE_* selected by the caller
 * \param[in]  flags               VA_EXPORT_SURFACE_* access and layer flags
 * \param[out] descriptor          Platform-specific export descriptor or handle storage
 *
 * \retval    None
 */
void Gfx_Vaapi_ExportSurfaceHandle(VADisplay dpy, VASurfaceID surfaceId, uint32_t memoryType, uint32_t flags, void* descriptor);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VA processing context over supplied render targets.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  configId            Configuration defining profile and entrypoint
 * \param[in]  pictureWidth        Nonzero coded width in pixels
 * \param[in]  pictureHeight       Nonzero coded height in pixels
 * \param[in]  flag                VA context flags
 * \param[in]  renderTargets       Compatible surface array
 * \param[in]  numRenderTargets    Number of elements in renderTargets
 * \param[out] context             Receives the created context identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_CreateContext(VADisplay dpy, VAConfigID configId, int32_t pictureWidth, int32_t pictureHeight, int32_t flag, VASurfaceID* renderTargets, int32_t numRenderTargets, VAContextID* context);

/*************************************************************************************************/
/**
 * \brief Tests whether an initialized display exposes a profile.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] profile              Profile to locate
 *
 * \retval TRUE                    The profile is present
 * \retval FALSE                   The profile is absent
 */
bool_t Gfx_Vaapi_GetProfileSupport(VADisplay dpy, VAProfile profile);

/*************************************************************************************************/
/**
 * \brief Tests whether a profile exposes a specific entrypoint.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] profile              Supported profile to query
 * \param[in] entrypoint           Entrypoint to locate
 *
 * \retval TRUE                    The profile exposes the entrypoint
 * \retval FALSE                   The entrypoint is absent
 */
bool_t Gfx_Vaapi_GetEntrypointSupport(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint);

/*************************************************************************************************/
/**
 * \brief Tests whether a profile supports normal or low-power slice encoding.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] profile              Profile to query
 *
 * \retval TRUE                    At least one slice-encode entrypoint is available
 * \retval FALSE                   Neither slice-encode entrypoint is available
 */
bool_t Gfx_Vaapi_GetEncodeSliceSupport(VADisplay dpy, VAProfile profile);

/*************************************************************************************************/
/**
 * \brief Selects the first supported profile from a preference-ordered list.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  desiredProfiles     Preference-ordered profile array
 * \param[in]  numProfiles         Number of elements in desiredProfiles
 * \param[out] profile             Receives the first supported profile, or VAProfileNone if none match
 *
 * \retval    None
 */
void Gfx_Vaapi_SelectProfile(VADisplay dpy, const VAProfile desiredProfiles[VA_MAX_PROFILES], int32_t numProfiles, VAProfile* profile);

/*************************************************************************************************/
/**
 * \brief Tests whether a configuration exposes an integer-valued surface attribute.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] configId             Valid configuration identifier
 * \param[in] type                 Attribute type to locate
 * \param[in] value                Required value or flag bits
 *
 * \retval TRUE                    A matching supported attribute contains the requested value
 * \retval FALSE                   No matching attribute supports the requested value
 */
bool_t Gfx_Vaapi_GetSurfaceAttributeSupport(VADisplay dpy, VAConfigID configId, VASurfaceAttribType type, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Tests whether a configuration supports a VA surface memory-type flag.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] configId             Valid configuration identifier
 * \param[in] memoryType           VA_SURFACE_ATTRIB_MEM_TYPE_* bit to test
 *
 * \return Result of querying VASurfaceAttribMemoryType.
 */
bool_t Gfx_Vaapi_GetSurfaceMemoryTypeSupport(VADisplay dpy, VAConfigID configId, uint32_t memoryType);

/*************************************************************************************************/
/**
 * \brief Tests whether a configuration supports a VA four-character pixel format.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] configId             Valid configuration identifier
 * \param[in] pixelFormat          VA_FOURCC value to test
 *
 * \return Result of querying VASurfaceAttribPixelFormat.
 */
bool_t Gfx_Vaapi_GetSurfacePixelFormatSupport(VADisplay dpy, VAConfigID configId, uint32_t pixelFormat);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VA buffer and optionally initializes all elements.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  context             Context owning the buffer
 * \param[in]  type                VA buffer payload type
 * \param[in]  size                Size of one element in bytes
 * \param[in]  numElements         Nonzero element count
 * \param[in]  data                Initial element storage, or NULL for uninitialized contents
 * \param[out] bufId               Receives the created buffer identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_CreateBuffer(VADisplay dpy, VAContextID context, VABufferType type, uint32_t size, uint32_t numElements, void* data, VABufferID* bufId);

/*************************************************************************************************/
/**
 * \brief Calculates the byte capacity of a macroblock-aligned surface.
 *
 * \par Details:
 *  Uses 16-bit sample storage for 10-bit and 12-bit YUV formats. The caller adds any
 *  encoded-header allowance. This size excludes driver-specific surface pitches.
 *
 * \param[in] format One VA_RT_FORMAT_* render-target format
 * \param[in] width Visible surface width
 * \param[in] height Visible surface height
 * \param[in] interlaced Align the height to two macroblock rows for field coding
 *
 * \return Surface size in bytes, fitting a VA buffer's uint32_t capacity.
 */
uint32_t Gfx_Vaapi_GetCodedSurfaceSize(uint32_t format, uint32_t width, uint32_t height, bool_t interlaced);

/*************************************************************************************************/
/**
 * \brief Creates a coded-output VA buffer with caller-specified capacity.
 *
 * \param[in] dpy Initialized VA display
 * \param[in] context Encoding context owning the buffer
 * \param[in] size Nonzero capacity in bytes, including any caller-added header allowance
 * \param[out] bufId Created coded-output buffer
 *
 * \retval None
 */
void Gfx_Vaapi_CreateCodedBuffer(VADisplay dpy, VAContextID context, uint32_t size, VABufferID* bufId);

/*************************************************************************************************/
/**
 * \brief Maps a VA buffer with explicit VA_MAPBUFFER_FLAG_* access flags.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  bufId               Valid buffer identifier
 * \param[out] pbuf                Receives the mapped address
 * \param[in]  flags               Mapping access flags supported by vaMapBuffer2()
 *
 * \retval    None
 */
void Gfx_Vaapi_MapBuffer2(VADisplay dpy, VABufferID bufId, void** pbuf, uint32_t flags);

/*************************************************************************************************/
/**
 * \brief Ends a mapping established for a VA buffer.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] bufId                Currently mapped buffer identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_UnmapBuffer(VADisplay dpy, VABufferID bufId);

/*************************************************************************************************/
/**
 * \brief Creates a persistent CPU-accessible image view of a VA surface.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  surface             Valid surface identifier
 * \param[out] image               Receives the derived image descriptor
 *
 * \retval    None
 */
void Gfx_Vaapi_DeriveImage(VADisplay dpy, VASurfaceID surface, VAImage* image);

/*************************************************************************************************/
/**
 * \brief Begins one VA picture submission against a render target.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] context              Encoding context
 * \param[in] renderTarget         Picture surface; contains source pixels for encoding
 *
 * \retval    None
 */
void Gfx_Vaapi_BeginPicture(VADisplay dpy, VAContextID context, VASurfaceID renderTarget);

/*************************************************************************************************/
/**
 * \brief Submits parameter and data buffers for the current VA picture.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] context              Encoding context between BeginPicture() and EndPicture()
 * \param[in] bufIds               Ordered array of VA buffer identifiers
 * \param[in] numBufIds            Number of elements in bufIds
 *
 * \retval    None
 */
void Gfx_Vaapi_RenderPicture(VADisplay dpy, VAContextID context, VABufferID* bufIds, int32_t numBufIds);

/*************************************************************************************************/
/**
 * \brief Completes parameter submission for the current VA picture.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] context              Encoding context with a begun picture
 *
 * \retval    None
 */
void Gfx_Vaapi_EndPicture(VADisplay dpy, VAContextID context);

/*************************************************************************************************/
/**
 * \brief Waits until all VA operations targeting a surface have completed.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] renderTarget         Surface whose pending work is drained
 *
 * \retval    None
 */
void Gfx_Vaapi_SyncSurface(VADisplay dpy, VASurfaceID renderTarget);

/*************************************************************************************************/
/**
 * \brief Queries the current state of a VA surface.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  renderTarget        Surface whose state is queried
 * \param[out] status              Receives rendering, displaying, ready, or skipped state
 *
 * \retval    None
 */
void Gfx_Vaapi_QuerySurfaceStatus(VADisplay dpy, VASurfaceID renderTarget, VASurfaceStatus* status);

/*************************************************************************************************/
/**
 * \brief Queries optional decode-error or hardware-reset diagnostics after surface synchronization.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  surface             Surface that was synchronized
 * \param[in]  errorStatus         Synchronization result accepted by vaQuerySurfaceError()
 * \param[out] errorInfo           Driver-owned diagnostic records; NULL when unavailable
 *
 * \return VA query result, allowing unavailable diagnostics to preserve the original sync result.
 */
VAStatus Gfx_Vaapi_QuerySurfaceError(VADisplay dpy, VASurfaceID surface, VAStatus errorStatus, void** errorInfo);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VA H.264 sequence-parameter buffer from initialized parameters.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  context             Encoding context owning the buffer
 * \param[in]  params              Complete sequence parameters copied during the call
 * \param[out] bufId               Receives the created buffer identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_CreateSequenceParameterBufferH264(VADisplay dpy, VAContextID context, const VAEncSequenceParameterBufferH264* params, VABufferID* bufId);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VA H.264 picture-parameter buffer from initialized parameters.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  context             Encoding context owning the buffer
 * \param[in]  params              Complete picture parameters copied during the call
 * \param[out] bufId               Receives the created buffer identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_CreatePictureParameterBufferH264(VADisplay dpy, VAContextID context, const VAEncPictureParameterBufferH264* params, VABufferID* bufId);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VA H.264 slice-parameter buffer containing one or more slices.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  context             Encoding context owning the buffer
 * \param[in]  params              Array of initialized slice parameters
 * \param[in]  paramCount          Number of elements in params
 * \param[out] bufId               Receives the created buffer identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_CreateSliceParameterBufferH264(VADisplay dpy, VAContextID context, const VAEncSliceParameterBufferH264* params, uint32_t paramCount, VABufferID* bufId);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VA packed-header parameter buffer.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  context             Encoding context owning the buffer
 * \param[in]  type                Packed-header category
 * \param[in]  bitLength           Exact header length in bits
 * \param[in]  hasEmulationBytes   TRUE when emulation-prevention bytes are already present
 * \param[out] bufId               Receives the created parameter-buffer identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_CreatePackedHeaderParameterBuffer(VADisplay dpy, VAContextID context, VAEncPackedHeaderType type, uint32_t bitLength, bool_t hasEmulationBytes, VABufferID* bufId);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VA packed-header data buffer.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  context             Encoding context owning the buffer
 * \param[in]  data                Header storage covering ceil(bitLength / 8) bytes
 * \param[in]  bitLength           Exact valid length in bits
 * \param[out] bufId               Receives the created data-buffer identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_CreatePackedHeaderDataBuffer(VADisplay dpy, VAContextID context, const void* data, uint32_t bitLength, VABufferID* bufId);

/*************************************************************************************************/
/**
 * \brief Creates the paired parameter and data buffers for one packed header.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  context             Encoding context owning both buffers
 * \param[in]  type                Packed-header category
 * \param[in]  bitLength           Exact valid data length in bits
 * \param[in]  hasEmulationBytes   TRUE when data already contains emulation-prevention bytes
 * \param[in]  data                Header storage covering ceil(bitLength / 8) bytes
 * \param[out] parameterBufId      Receives the parameter-buffer identifier
 * \param[out] dataBufId           Receives the data-buffer identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_CreatePackedHeaderBuffers(VADisplay             dpy,
                                         VAContextID           context,
                                         VAEncPackedHeaderType type,
                                         uint32_t              bitLength,
                                         bool_t                hasEmulationBytes,
                                         const void*           data,
                                         VABufferID*           parameterBufId,
                                         VABufferID*           dataBufId);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VA miscellaneous rate-control buffer.
 *
 * \param[in]  dpy                                  Initialized VA display
 * \param[in]  context                              Encoding context owning the buffer
 * \param[in]  targetBitRate                        Target rate in bits per second
 * \param[in]  maximumBitRate                       Maximum rate in bits per second
 * \param[in]  windowMilliseconds                   Sliding control-window duration
 * \param[in]  initialQuantizationParameter         Initial QP
 * \param[in]  minimumQuantizationParameter         Minimum permitted QP
 * \param[in]  maximumQuantizationParameter         Maximum permitted QP
 * \param[in]  basicUnitSize                        Macroblocks per control unit
 * \param[in]  reset                                Reset driver rate-control history
 * \param[in]  disableFrameSkip                     Prohibit frame skipping
 * \param[in]  disableBitStuffing                   Prohibit filler-bit insertion
 * \param[in]  macroblockRateControlMode            VA macroblock mode in the range 0..2
 * \param[in]  temporalLayer                        Temporal-layer identifier
 * \param[in]  constantFrameSizeIntraFrames         Include intra pictures in constant-size control
 * \param[in]  enableParallelRateControl            Enable VA parallel rate control
 * \param[in]  enableDynamicScaling                 Permit dynamic frame scaling
 * \param[in]  frameToleranceMode                   VA tolerance mode in the range 0..2
 * \param[in]  intelligentConstantQualityFactor     ICQ quality factor
 * \param[in]  qualityFactor                        Quality factor for quality-based modes
 * \param[in]  targetFrameSize                      Per-picture target size in bytes
 * \param[out] rateControlBufId                     Receives the created buffer identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_CreateRateControlBuffer(VADisplay   dpy,
                                       VAContextID context,
                                       uint32_t    targetBitRate,
                                       uint32_t    maximumBitRate,
                                       uint32_t    windowMilliseconds,
                                       uint32_t    initialQuantizationParameter,
                                       uint32_t    minimumQuantizationParameter,
                                       uint32_t    maximumQuantizationParameter,
                                       uint32_t    basicUnitSize,
                                       bool_t      reset,
                                       bool_t      disableFrameSkip,
                                       bool_t      disableBitStuffing,
                                       uint8_t     macroblockRateControlMode,
                                       uint8_t     temporalLayer,
                                       bool_t      constantFrameSizeIntraFrames,
                                       bool_t      enableParallelRateControl,
                                       bool_t      enableDynamicScaling,
                                       uint8_t     frameToleranceMode,
                                       uint32_t    intelligentConstantQualityFactor,
                                       uint32_t    qualityFactor,
                                       uint32_t    targetFrameSize,
                                       VABufferID* rateControlBufId);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VA miscellaneous frame-rate buffer.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  context             Encoding context owning the buffer
 * \param[in]  numerator           Nonzero frame-rate numerator
 * \param[in]  denominator         Nonzero frame-rate denominator
 * \param[in]  temporalLayer       Temporal-layer identifier
 * \param[out] frameRateBufId      Receives the created buffer identifier
 *
 * \retval    None
 */
void Gfx_Vaapi_CreateFrameRateBuffer(VADisplay dpy, VAContextID context, uint16_t numerator, uint16_t denominator, uint8_t temporalLayer, VABufferID* frameRateBufId);

/*************************************************************************************************/
/**
 * \brief Rewrites an existing packed-header parameter/data pair.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] parameterBufId       Existing packed-header parameter buffer
 * \param[in] dataBufId            Existing packed-header data buffer
 * \param[in] type                 Packed-header category
 * \param[in] bitLength            Exact valid data length in bits
 * \param[in] hasEmulationBytes    TRUE when data already contains emulation-prevention bytes
 * \param[in] data                 Header storage covering ceil(bitLength / 8) bytes
 *
 * \retval    None
 */
void Gfx_Vaapi_WritePackedHeaderBuffers(VADisplay dpy, VABufferID parameterBufId, VABufferID dataBufId, VAEncPackedHeaderType type, uint32_t bitLength, bool_t hasEmulationBytes, const void* data);

/*************************************************************************************************/
/**
 * \brief Maps a VA buffer for reading and copies exactly dstSize bytes.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  bufId               Readable buffer identifier
 * \param[out] dst                 Destination with at least dstSize bytes
 * \param[in]  dstSize             Exact byte count to copy
 *
 * \retval    None
 */
void Gfx_Vaapi_ReadMemory(VADisplay dpy, VABufferID bufId, void* dst, uint32_t dstSize);

/*************************************************************************************************/
/**
 * \brief Maps a VA buffer for writing and copies exactly srcSize bytes.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] bufId                Writable buffer identifier
 * \param[in] src                  Source containing srcSize bytes
 * \param[in] srcSize              Exact byte count to copy
 *
 * \retval    None
 */
void Gfx_Vaapi_WriteMemory(VADisplay dpy, VABufferID bufId, const void* src, uint32_t srcSize);

/*************************************************************************************************/
/**
 * \brief Copies a mapped VA image into tightly packed host plane storage.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  image               Derived image whose plane layout is validated
 * \param[out] dst                 Destination covering the computed packed plane size
 * \param[in]  dstSize             Destination capacity in bytes; must hold all packed image planes
 *
 * \retval    None
 */
void Gfx_Vaapi_ReadImageMemory(VADisplay dpy, const VAImage* image, void* dst, uint32_t dstSize);

/*************************************************************************************************/
/**
 * \brief Copies tightly packed host planes into a mapped VA image respecting VA pitches and offsets.
 *
 * \param[in] dpy                  Initialized VA display
 * \param[in] image                Derived image whose plane layout is validated
 * \param[in] src                  Source covering the computed packed plane size
 * \param[in] srcSize              Source capacity in bytes; must hold all packed image planes
 *
 * \retval    None
 */
void Gfx_Vaapi_WriteImageMemory(VADisplay dpy, const VAImage* image, const void* src, uint32_t srcSize);

/*************************************************************************************************/
/**
 * \brief Concatenates a linked VA coded-buffer chain into caller storage.
 *
 * \param[in]  dpy                 Initialized VA display
 * \param[in]  bufId               Coded-buffer identifier
 * \param[out] dst                 Destination with dstSize-byte capacity
 * \param[in]  dstSize             Maximum bytes accepted by dst
 * \param[out] codedSize           Receives the concatenated coded byte count
 *
 * \retval    None
 */
void Gfx_Vaapi_CopyCodedBufferSegments(VADisplay dpy, VABufferID bufId, void* dst, uint32_t dstSize, uint32_t* codedSize);

/*************************************************************************************************/
/**
 * \brief Computes tightly packed plane offsets, row sizes, row counts, and total byte size.
 *
 * \param[in]  image               VA image with supported format, dimensions, pitches, and offsets
 * \param[out] offsets             Packed destination offset for each image plane
 * \param[out] rowSizes            Active bytes copied per row for each plane
 * \param[out] rowCounts           Row count copied for each plane
 *
 * \return Total tightly packed byte size across all planes.
 */
uint32_t Gfx_Vaapi_GetImagePlaneLayout(const VAImage* image, uint32_t offsets[VA_MAX_IMAGE_PLANES], uint32_t rowSizes[VA_MAX_IMAGE_PLANES], uint32_t rowCounts[VA_MAX_IMAGE_PLANES]);

#endif /* VULKAN_VA_ZERO_COPY_GFX_VAAPI_H */
