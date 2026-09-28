/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_vaapi.h"

#include "os/os_assert.h"
#include "utl/utl_defines.h"
#include "utl/utl_h264.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/* ITU-T H.264 Table 6-1: chroma-array dimensions. */

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

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
 * \brief Routes libva diagnostic errors to standard error.
 *
 * \param[in] userContext        Unused libva callback context
 * \param[in] message            Driver-supplied, already formatted diagnostic text
 *
 * \retval    None
 */
static void ErrorCallback(void* userContext, const char* message);

/*************************************************************************************************/
/**
 * \brief Routes libva informational diagnostics to standard error.
 *
 * \param[in] userContext        Unused libva callback context
 * \param[in] message            Driver-supplied, already formatted diagnostic text
 *
 * \retval    None
 */
static void InfoCallback(void* userContext, const char* message);

/*************************************************************************************************/
/**
 * \brief Rejects an empty, overflowed, or corrupt coded-buffer segment.
 *
 * \param[in] seg        Mapped coded-buffer segment with valid payload and status fields
 *
 * \retval    None
 */
static void CheckSegmentIntegrity(const VACodedBufferSegment* seg);

/*************************************************************************************************/
/**
 * \brief Copies active image rows between independently pitched plane layouts.
 *
 * \param[out] dst              Base of destination image storage
 * \param[in] dstOffsets        Per-plane byte offsets from dst
 * \param[in] dstPitches        Per-plane byte distance between destination rows
 * \param[in] src               Base of source image storage
 * \param[in] srcOffsets        Per-plane byte offsets from src
 * \param[in] srcPitches        Per-plane byte distance between source rows
 * \param[in] rowSizes          Active bytes copied from each row, excluding padding
 * \param[in] rowCounts         Number of rows copied for each plane
 * \param[in] numPlanes         Number of valid entries in every layout array
 *
 * \retval    None
 */
static void CopyImagePlanes(void*           dst,
                            const uint32_t* dstOffsets,
                            const uint32_t* dstPitches,
                            const void*     src,
                            const uint32_t* srcOffsets,
                            const uint32_t* srcPitches,
                            const uint32_t* rowSizes,
                            const uint32_t* rowCounts,
                            uint32_t        numPlanes);

/*************************************************************************************************/
/**
 * \brief Initializes a VA H.264 picture entry to the required invalid sentinel.
 *
 * \param[out] picture        Entry cleared, assigned VA_INVALID_SURFACE, and marked invalid
 *
 * \retval    None
 */
static void InvalidateH264Picture(VAPictureH264* picture);

/*************************************************************************************************/
/**
 * \brief Populates a fixed VA picture array and invalidates unused entries.
 *
 * \param[out] destination         Complete fixed-capacity VA picture array
 * \param[in] destinationCount     Number of writable destination entries
 * \param[in] source               Source prefix; may be null only when sourceCount is zero
 * \param[in] sourceCount          Entries copied after destination invalidation
 *
 * \retval    None
 */
static void CopyH264Pictures(VAPictureH264* destination, size_t destinationCount, const VAPictureH264* source, size_t sourceCount);

/*************************************************************************************************/
/**
 * \brief Derives chroma sampling from a VA H.264 profile.
 *
 * \param[in] profile Supported VA H.264 encoding profile
 *
 * \return H.264 chroma_format_idc (4:2:0 or 4:2:2).
 */
static uint32_t DeriveProfileChromaFormatH264(VAProfile profile);

/*************************************************************************************************/
/**
 * \brief Converts a VA picture type to the shared H.264 slice-type derivation.
 *
 * \param[in] pictureType VA intra, predictive, or bidirectional picture type
 *
 * \return I, P, or B slice_type syntax value.
 */
static uint8_t DeriveSliceTypeH264(VAEncPictureType pictureType);

/*************************************************************************************************/
/**
 * \brief Builds VA picture flags from reference and field semantics.
 *
 * \param[in] referencePicture        Whether a short- or long-term reference bit is emitted
 * \param[in] referenceType           Short-term or long-term reference classification
 * \param[in] fieldMode               Frame, top-field, or bottom-field coding mode
 *
 * \return Bitwise combination of VA_PICTURE_H264 reference and field flags.
 */
static uint32_t DerivePictureFlagsH264(bool_t referencePicture, Utl_H264_ReferenceType_t referenceType, Utl_H264_FieldMode_t fieldMode);

/*************************************************************************************************/
/**
 * \brief Builds VA H.264 reference-picture descriptors from parallel metadata arrays.
 *
 * \param[out] pictures              Destination descriptors for pictureCount references
 * \param[in] pictureCapacity        Writable descriptor capacity
 * \param[in] surfaces               VA surface IDs, invalid only for non-existing references
 * \param[in] frameIndices           Absolute frame indices used for frame_num and field order counts
 * \param[in] longTermReferences     Per-entry long-term classification
 * \param[in] topFields              Per-entry top-field-only classification
 * \param[in] bottomFields           Per-entry bottom-field-only classification
 * \param[in] nonExisting            Per-entry non-existing reference classification
 * \param[in] pictureCount           Valid length shared by every source array
 * \param[in] frameNumMask           Mask reducing absolute indices to the SPS frame_num range
 *
 * \retval    None
 */
static void BuildReferencePicturesH264(VAPictureH264*     pictures,
                                       size_t             pictureCapacity,
                                       const VASurfaceID* surfaces,
                                       const uint32_t*    frameIndices,
                                       const bool_t*      longTermReferences,
                                       const bool_t*      topFields,
                                       const bool_t*      bottomFields,
                                       const bool_t*      nonExisting,
                                       size_t             pictureCount,
                                       uint32_t           frameNumMask);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ErrorCallback(void* userContext, const char* message)
{
    (void)userContext;
    LOGSTART(stderr);
    LOGPRINTS("VA-API error: ");
    LOGPRINTS(message);
    LOGEND();
}
/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InfoCallback(void* userContext, const char* message)
{
    (void)userContext;
    LOGSTART(stderr);
    LOGPRINTS("VA-API info: ");
    LOGPRINTS(message);
    LOGEND();
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CheckSegmentIntegrity(const VACodedBufferSegment* seg)
{
    LOGASSERT(NULL != seg->buf);
    LOGASSERT(0 != seg->size);
    LOGASSERT(0 == (seg->status & VA_CODED_BUF_STATUS_SLICE_OVERFLOW_MASK));
    LOGASSERT(0 == (seg->status & VA_CODED_BUF_STATUS_BITRATE_OVERFLOW));
    LOGASSERT(0 == (seg->status & VA_CODED_BUF_STATUS_FRAME_SIZE_OVERFLOW));
    LOGASSERT(0 == (seg->status & VA_CODED_BUF_STATUS_BAD_BITSTREAM));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CopyImagePlanes(void*           dst,
                            const uint32_t* dstOffsets,
                            const uint32_t* dstPitches,
                            const void*     src,
                            const uint32_t* srcOffsets,
                            const uint32_t* srcPitches,
                            const uint32_t* rowSizes,
                            const uint32_t* rowCounts,
                            uint32_t        numPlanes)
{
    byte_t*       dst_row;
    const byte_t* src_row;
    uint32_t      plane;
    uint32_t      row;

    LOGASSERT(NULL != dst);
    LOGASSERT(NULL != dstOffsets);
    LOGASSERT(NULL != dstPitches);
    LOGASSERT(NULL != src);
    LOGASSERT(NULL != srcOffsets);
    LOGASSERT(NULL != srcPitches);
    LOGASSERT(NULL != rowSizes);
    LOGASSERT(NULL != rowCounts);
    LOGASSERT(0 != numPlanes);

    for (plane = 0; plane < numPlanes; ++plane)
    {
        for (row = 0; row < rowCounts[plane]; ++row)
        {
            dst_row = (byte_t*)dst + dstOffsets[plane] + ((size_t)row * dstPitches[plane]);
            src_row = (const byte_t*)src + srcOffsets[plane] + ((size_t)row * srcPitches[plane]);

            (void)memcpy(dst_row, src_row, rowSizes[plane]);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InvalidateH264Picture(VAPictureH264* picture)
{
    LOGASSERT(NULL != picture);

    (void)memset(picture, 0, sizeof((*picture)));
    picture->picture_id = VA_INVALID_SURFACE;
    picture->flags      = VA_PICTURE_H264_INVALID;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CopyH264Pictures(VAPictureH264* destination, size_t destinationCount, const VAPictureH264* source, size_t sourceCount)
{
    size_t picture_index;

    LOGASSERT(NULL != destination);
    LOGASSERT(sourceCount <= destinationCount);
    LOGASSERT(0 == sourceCount || NULL != source);

    for (picture_index = 0; picture_index < destinationCount; ++picture_index)
    {
        InvalidateH264Picture(&destination[picture_index]);
    }
    if (0 != sourceCount)
    {
        (void)memcpy(destination, source, sourceCount * sizeof((*source)));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Maps supported profiles to 4:2:0 or 4:2:2 and rejects profiles with no implemented VA buffer
 *  representation.
 */
static uint32_t DeriveProfileChromaFormatH264(VAProfile profile)
{
    switch (profile)
    {
    case VAProfileH264ConstrainedBaseline:
    case VAProfileH264Main:
    case VAProfileH264High:
    case VAProfileH264High10:
    case VAProfileH264MultiviewHigh:
    case VAProfileH264StereoHigh:
        return UTL_H264_CHROMA_FORMAT_420;

    case VAProfileH264High422:
        return UTL_H264_CHROMA_FORMAT_422;

    default:
        LOGFATAL("unsupported H.264 profile chroma format");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint8_t DeriveSliceTypeH264(VAEncPictureType pictureType)
{
    switch (pictureType)
    {
    case VAEncPictureTypeIntra:
        return Utl_H264_DeriveSliceType(UTL_H264_PICTURE_TYPE_I);
    case VAEncPictureTypePredictive:
        return Utl_H264_DeriveSliceType(UTL_H264_PICTURE_TYPE_P);
    case VAEncPictureTypeBidirectional:
        return Utl_H264_DeriveSliceType(UTL_H264_PICTURE_TYPE_B);
    default:
        LOGFATAL("Unsupported H.264 picture type!");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint32_t DerivePictureFlagsH264(bool_t referencePicture, Utl_H264_ReferenceType_t referenceType, Utl_H264_FieldMode_t fieldMode)
{
    uint32_t flags = 0;

    LOGASSERT(referenceType >= UTL_H264_REFERENCE_TYPE_SHORT_TERM && referenceType <= UTL_H264_REFERENCE_TYPE_LONG_TERM);
    LOGASSERT(fieldMode >= UTL_H264_FIELD_MODE_FRAME && fieldMode <= UTL_H264_FIELD_MODE_BOTTOM);

    if (referencePicture)
    {
        flags |= UTL_H264_REFERENCE_TYPE_LONG_TERM == referenceType ? VA_PICTURE_H264_LONG_TERM_REFERENCE : VA_PICTURE_H264_SHORT_TERM_REFERENCE;
    }
    if (UTL_H264_FIELD_MODE_TOP == fieldMode)
    {
        flags |= VA_PICTURE_H264_TOP_FIELD;
    }
    else if (UTL_H264_FIELD_MODE_BOTTOM == fieldMode)
    {
        flags |= VA_PICTURE_H264_BOTTOM_FIELD;
    }
    return flags;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  frame_idx wraps within frameNumMask, while field order counts use the absolute frame index:
 *  top POC is twice that index and bottom POC is one greater.
 */
static void BuildReferencePicturesH264(VAPictureH264*     pictures,
                                       size_t             pictureCapacity,
                                       const VASurfaceID* surfaces,
                                       const uint32_t*    frameIndices,
                                       const bool_t*      longTermReferences,
                                       const bool_t*      topFields,
                                       const bool_t*      bottomFields,
                                       const bool_t*      nonExisting,
                                       size_t             pictureCount,
                                       uint32_t           frameNumMask)
{
    size_t picture_index;

    LOGASSERT(NULL != pictures);
    LOGASSERT(pictureCount <= pictureCapacity);
    LOGASSERT(0 == pictureCount || (NULL != surfaces && NULL != frameIndices && NULL != longTermReferences && NULL != topFields && NULL != bottomFields && NULL != nonExisting));

    for (picture_index = 0; picture_index < pictureCount; ++picture_index)
    {
        Gfx_Vaapi_InitPictureH264(&pictures[picture_index],
                                  surfaces[picture_index],
                                  frameIndices[picture_index],
                                  frameNumMask,
                                  TRUE,
                                  longTermReferences[picture_index],
                                  topFields[picture_index],
                                  bottomFields[picture_index],
                                  nonExisting[picture_index]);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CheckError(VAStatus status, const char* expression, const char* file, int32_t line)
{
    if (VA_STATUS_SUCCESS != status)
    {
        LOGSTART(stderr);
        LOGPRINTS("VA-API call failed: ");
        LOGPRINTS(expression);
        LOGPRINTS(" returned ");
        LOGPRINTI((int64_t)(status));
        LOGPRINTS(" (");
        LOGPRINTS(vaErrorStr(status));
        LOGPRINTS(") at ");
        LOGPRINTS(file);
        LOGPRINTS(":");
        LOGPRINTI((int64_t)(line));
        LOGPRINTS("\n");
        LOGEND();
    }
    LOGASSERT(VA_STATUS_SUCCESS == status);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CheckVersion(int32_t major, int32_t minor, int32_t micro, const char* file, int32_t line)
{
    (void)file;
    (void)line;
    LOGASSERT(VA_MAJOR_VERSION == major);
    LOGASSERT(VA_MINOR_VERSION == minor);
    LOGASSERT(VA_MICRO_VERSION == micro);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_InitPictureH264(VAPictureH264* picture,
                               VASurfaceID    surfaceId,
                               uint32_t       frameIndex,
                               uint32_t       frameNumMask,
                               bool_t         referencePicture,
                               bool_t         longTermReference,
                               bool_t         topField,
                               bool_t         bottomField,
                               bool_t         nonExisting)
{
    Utl_H264_FieldMode_t field_mode;

    LOGASSERT(NULL != picture);
    LOGASSERT(FALSE == topField || FALSE == bottomField);
    LOGASSERT(FALSE == nonExisting || VA_INVALID_SURFACE == surfaceId);
    LOGASSERT(TRUE == nonExisting || VA_INVALID_SURFACE != surfaceId);
    LOGASSERT(TRUE == referencePicture || FALSE == longTermReference);

    field_mode = topField ? UTL_H264_FIELD_MODE_TOP : (bottomField ? UTL_H264_FIELD_MODE_BOTTOM : UTL_H264_FIELD_MODE_FRAME);
    (void)memset(picture, 0, sizeof((*picture)));
    picture->picture_id = surfaceId;
    picture->frame_idx  = frameIndex & frameNumMask;
    picture->flags      = DerivePictureFlagsH264(referencePicture, longTermReference ? UTL_H264_REFERENCE_TYPE_LONG_TERM : UTL_H264_REFERENCE_TYPE_SHORT_TERM, field_mode);
    picture->flags |= nonExisting ? VA_PICTURE_H264_NON_EXISTING : 0u;
    picture->TopFieldOrderCnt    = (int32_t)Utl_H264_DeriveFieldOrderCount(frameIndex);
    picture->BottomFieldOrderCnt = picture->TopFieldOrderCnt + 1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_InitSequenceParameterBufferH264(VAEncSequenceParameterBufferH264* sps,
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
                                               uint32_t                          maxMotionVectorLengthVertical)
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

    LOGASSERT(NULL != sps);
    LOGASSERT(0 != width);
    LOGASSERT(0 != height);
    LOGASSERT(0 != frameRateNumerator);
    LOGASSERT(0 != frameRateDenominator);
    LOGASSERT(0 != ipPeriod);
    LOGASSERT((UTL_H264_MIN_BIT_DEPTH <= bitDepthLuma) && (UTL_H264_MAX_BIT_DEPTH >= bitDepthLuma));
    LOGASSERT((UTL_H264_MIN_BIT_DEPTH <= bitDepthChroma) && (UTL_H264_MAX_BIT_DEPTH >= bitDepthChroma));
    LOGASSERT(maxReferenceFrames <= UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES);
    LOGASSERT(FALSE == interlaced || TRUE == interlaced);
    LOGASSERT(FALSE == mbAdaptiveFrameField || TRUE == mbAdaptiveFrameField);
    LOGASSERT(FALSE == fixedFrameRate || TRUE == fixedFrameRate);
    LOGASSERT(FALSE == restrictMotionVectors || TRUE == restrictMotionVectors);
    LOGASSERT(FALSE != interlaced || FALSE == mbAdaptiveFrameField);
    LOGASSERT(intraIdrPeriod <= UINT16_MAX);
    LOGASSERT((NULL == scalingList4x4 && NULL == scalingList8x8) || (NULL != scalingList4x4 && NULL != scalingList8x8));
    LOGASSERT((0 == sampleAspectRatioWidth && 0 == sampleAspectRatioHeight) || (0 != sampleAspectRatioWidth && 0 != sampleAspectRatioHeight));
    LOGASSERT(VAProfileH264ConstrainedBaseline != profile || (FALSE == interlaced && NULL == scalingList4x4));
    LOGASSERT(VAProfileH264Main != profile || NULL == scalingList4x4);

    chroma_format_idc = DeriveProfileChromaFormatH264(profile);
    Utl_H264_DeriveCodedDimensions(width, height, interlaced, &width_in_mbs, &height_in_mbs, &padded_width, &padded_height);
    LOGASSERT(width_in_mbs <= UINT16_MAX);
    LOGASSERT(height_in_mbs <= UINT16_MAX);

    frame_num_range = Utl_H264_DeriveMaximumFrameNumber(intraIdrPeriod);
    poc_range       = Utl_H264_DeriveMaximumPicOrderCntLsb(frame_num_range);
    time_scale      = Utl_H264_DeriveTimeScale(frameRateNumerator);

    frame_cropping = Utl_H264_DeriveFrameCropOffsets(width, height, padded_width, padded_height, chroma_format_idc, interlaced, &crop_right_offset, &crop_bottom_offset);
    Utl_H264_DeriveAspectRatio(sampleAspectRatioWidth, sampleAspectRatioHeight, &aspect_ratio_idc, &sar_width, &sar_height);
    aspect_ratio_present = UTL_H264_ASPECT_RATIO_IDC_UNSPECIFIED != aspect_ratio_idc;
    Utl_H264_DeriveMotionVectorLimits(restrictMotionVectors, maxMotionVectorLengthHorizontal, maxMotionVectorLengthVertical, &log2_max_mv_length_horizontal, &log2_max_mv_length_vertical);

    LOGASSERT(0 != width_in_mbs);
    LOGASSERT(0 != height_in_mbs);
    LOGASSERT(chroma_format_idc <= UTL_H264_CHROMA_FORMAT_444);
    LOGASSERT(0 != frame_num_range);
    LOGASSERT(0 != poc_range);

    (void)memset(sps, 0, sizeof((*sps)));
    sps->seq_parameter_set_id                                    = 0;
    sps->level_idc                                               = Utl_H264_DeriveLevelIdc(padded_width, padded_height, frameRateNumerator, frameRateDenominator);
    sps->intra_period                                            = intraPeriod;
    sps->intra_idr_period                                        = intraIdrPeriod;
    sps->ip_period                                               = ipPeriod;
    sps->bits_per_second                                         = bitsPerSecond;
    sps->max_num_ref_frames                                      = maxReferenceFrames;
    sps->picture_width_in_mbs                                    = (uint16_t)width_in_mbs;
    sps->picture_height_in_mbs                                   = (uint16_t)height_in_mbs;
    sps->seq_fields.bits.chroma_format_idc                       = chroma_format_idc;
    sps->seq_fields.bits.frame_mbs_only_flag                     = FALSE == interlaced;
    sps->seq_fields.bits.mb_adaptive_frame_field_flag            = mbAdaptiveFrameField;
    sps->seq_fields.bits.seq_scaling_matrix_present_flag         = NULL != scalingList4x4;
    sps->seq_fields.bits.direct_8x8_inference_flag               = TRUE;
    sps->seq_fields.bits.log2_max_frame_num_minus4               = Utl_H264_DeriveLog2Minus4(frame_num_range);
    sps->seq_fields.bits.pic_order_cnt_type                      = UTL_H264_PIC_ORDER_CNT_TYPE_0;
    sps->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4       = Utl_H264_DeriveLog2Minus4(poc_range);
    sps->seq_fields.bits.delta_pic_order_always_zero_flag        = FALSE;
    sps->bit_depth_luma_minus8                                   = (uint8_t)(bitDepthLuma - UTL_H264_MIN_BIT_DEPTH);
    sps->bit_depth_chroma_minus8                                 = (uint8_t)(bitDepthChroma - UTL_H264_MIN_BIT_DEPTH);
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
    sps->vui_fields.bits.bitstream_restriction_flag              = restrictMotionVectors;
    sps->vui_fields.bits.log2_max_mv_length_horizontal           = log2_max_mv_length_horizontal;
    sps->vui_fields.bits.log2_max_mv_length_vertical             = log2_max_mv_length_vertical;
    sps->vui_fields.bits.fixed_frame_rate_flag                   = fixedFrameRate;
    sps->vui_fields.bits.low_delay_hrd_flag                      = FALSE;
    sps->vui_fields.bits.motion_vectors_over_pic_boundaries_flag = TRUE;
    sps->aspect_ratio_idc                                        = aspect_ratio_idc;
    sps->sar_width                                               = sar_width;
    sps->sar_height                                              = sar_height;
    sps->num_units_in_tick                                       = frameRateDenominator;
    sps->time_scale                                              = time_scale;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_InitPictureParameterBufferH264(VAEncPictureParameterBufferH264* pps,
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
                                              size_t                           decodedPictureCount)
{
    VAPictureH264 decoded_pictures[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];
    bool_t        idr_picture;
    uint8_t       last_picture_flags;
    uint32_t      active_reference_count_l0;
    uint32_t      active_reference_count_l1;
    uint32_t      frame_num_mask;
    uint32_t      frame_num;

    LOGASSERT(NULL != pps);
    LOGASSERT(VA_INVALID_SURFACE != reconstructionSurface);
    LOGASSERT(VA_INVALID_ID != codedBufId);
    LOGASSERT(decodedPictureCount <= UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES);
    LOGASSERT(decodedPictureCount <= maxReferenceFrames);
    LOGASSERT(quantizationParameter <= UTL_H264_MAX_QUANTIZATION_PARAMETER);
    LOGASSERT(pictureType >= VAEncPictureTypeIntra && pictureType <= VAEncPictureTypeBidirectional);
    LOGASSERT(FALSE == topField || FALSE == bottomField);
    LOGASSERT(FALSE != interlaced || (FALSE == topField && FALSE == bottomField));
    LOGASSERT((NULL == scalingList4x4 && NULL == scalingList8x8) || (NULL != scalingList4x4 && NULL != scalingList8x8));
    LOGASSERT(weightedBipredIdc <= UTL_H264_MAX_WEIGHTED_BIPRED_IDC);
    LOGASSERT(chromaQpIndexOffset >= UTL_H264_MIN_CHROMA_QP_INDEX_OFFSET && chromaQpIndexOffset <= UTL_H264_MAX_CHROMA_QP_INDEX_OFFSET);
    LOGASSERT(secondChromaQpIndexOffset >= UTL_H264_MIN_CHROMA_QP_INDEX_OFFSET && secondChromaQpIndexOffset <= UTL_H264_MAX_CHROMA_QP_INDEX_OFFSET);
    LOGASSERT(VAProfileH264ConstrainedBaseline != profile || FALSE == useCabac);
    LOGASSERT((VAProfileH264ConstrainedBaseline != profile && VAProfileH264Main != profile) || FALSE == use8x8Transform);
    LOGASSERT((VAProfileH264ConstrainedBaseline != profile && VAProfileH264Main != profile) || NULL == scalingList4x4);
    LOGASSERT(TRUE == referencePicture || FALSE == longTermReference);
    LOGASSERT(FALSE == idrPicture || VAEncPictureTypeIntra == pictureType);

    idr_picture = idrPicture;
    LOGASSERT(FALSE == idr_picture || (TRUE == referencePicture && 0 == decodedPictureCount));

    last_picture_flags = lastPicture ? H264_LAST_PICTURE_EOSEQ | H264_LAST_PICTURE_EOSTREAM : 0u;
    frame_num_mask     = Utl_H264_DeriveValueMask(Utl_H264_DeriveMaximumFrameNumber(intraIdrPeriod));
    frame_num          = idr_picture ? 0u : frameIndex & frame_num_mask;
    /* PPS defaults stay fixed; slices override them when their active lists differ. */
    Utl_H264_DeriveActiveReferenceCounts(UTL_H264_SLICE_TYPE_I, 0, &active_reference_count_l0, &active_reference_count_l1);
    BuildReferencePicturesH264(decoded_pictures,
                               UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES,
                               decodedPictureSurfaces,
                               decodedPictureFrameIndices,
                               decodedPictureLongTermReferences,
                               decodedPictureTopFields,
                               decodedPictureBottomFields,
                               decodedPictureNonExisting,
                               decodedPictureCount,
                               frame_num_mask);

    LOGASSERT(decodedPictureCount <= sizeof(pps->ReferenceFrames) / sizeof(pps->ReferenceFrames[0]));
    LOGASSERT((0 < active_reference_count_l0) && (UTL_H264_MAX_REFERENCE_LIST_ENTRIES >= active_reference_count_l0));
    LOGASSERT((0 < active_reference_count_l1) && (UTL_H264_MAX_REFERENCE_LIST_ENTRIES >= active_reference_count_l1));
    LOGASSERT((FALSE == idr_picture) || (TRUE == idr_picture));
    LOGASSERT((FALSE == referencePicture) || (TRUE == referencePicture));
    LOGASSERT((FALSE == useCabac) || (TRUE == useCabac));
    LOGASSERT((FALSE == useWeightedPrediction) || (TRUE == useWeightedPrediction));
    LOGASSERT((FALSE == constrainedIntraPrediction) || (TRUE == constrainedIntraPrediction));
    LOGASSERT((FALSE == use8x8Transform) || (TRUE == use8x8Transform));
    LOGASSERT((FALSE == redundantPictures) || (TRUE == redundantPictures));
    LOGASSERT((FALSE == picOrderPresent) || (TRUE == picOrderPresent));

    (void)memset(pps, 0, sizeof((*pps)));
    Gfx_Vaapi_InitPictureH264(&pps->CurrPic, reconstructionSurface, idr_picture ? 0u : frameIndex, frame_num_mask, referencePicture, longTermReference, topField, bottomField, FALSE);
    CopyH264Pictures(pps->ReferenceFrames, sizeof(pps->ReferenceFrames) / sizeof(pps->ReferenceFrames[0]), decoded_pictures, decodedPictureCount);
    pps->coded_buf                                              = codedBufId;
    pps->pic_parameter_set_id                                   = 0;
    pps->seq_parameter_set_id                                   = 0;
    pps->last_picture                                           = last_picture_flags;
    pps->frame_num                                              = (uint16_t)frame_num;
    pps->pic_init_qp                                            = (uint8_t)quantizationParameter;
    pps->num_ref_idx_l0_active_minus1                           = (uint8_t)(active_reference_count_l0 - 1);
    pps->num_ref_idx_l1_active_minus1                           = (uint8_t)(active_reference_count_l1 - 1);
    pps->chroma_qp_index_offset                                 = chromaQpIndexOffset;
    pps->second_chroma_qp_index_offset                          = secondChromaQpIndexOffset;
    pps->pic_fields.bits.idr_pic_flag                           = idr_picture;
    pps->pic_fields.bits.reference_pic_flag                     = referencePicture;
    pps->pic_fields.bits.entropy_coding_mode_flag               = useCabac;
    pps->pic_fields.bits.weighted_pred_flag                     = useWeightedPrediction;
    pps->pic_fields.bits.weighted_bipred_idc                    = weightedBipredIdc;
    pps->pic_fields.bits.constrained_intra_pred_flag            = constrainedIntraPrediction;
    pps->pic_fields.bits.transform_8x8_mode_flag                = use8x8Transform;
    pps->pic_fields.bits.deblocking_filter_control_present_flag = TRUE;
    pps->pic_fields.bits.redundant_pic_cnt_present_flag         = redundantPictures;
    pps->pic_fields.bits.pic_order_present_flag                 = picOrderPresent;
    pps->pic_fields.bits.pic_scaling_matrix_present_flag        = NULL != scalingList4x4;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
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
                                            size_t                         decodedPictureBufferCount,
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
                                            int8_t                         deblockingBetaOffsetDiv2)
{
    uint32_t total_macroblocks;
    uint32_t width_in_mbs;
    uint32_t height_in_mbs;
    uint32_t padded_width;
    uint32_t padded_height;
    uint32_t macroblocks_per_slice;
    uint32_t extra_macroblocks;
    uint32_t first_macroblock;
    uint32_t macroblock_count;
    uint32_t pic_order_count_mask;
    int32_t  slice_quantization_parameter;
    bool_t   idr_picture;
    uint32_t default_reference_count_l0;
    uint32_t default_reference_count_l1;
    bool_t   override_reference_counts;

    LOGASSERT(NULL != slice);
    LOGASSERT(0 != width);
    LOGASSERT(0 != height);
    LOGASSERT(decodedPictureBufferCount <= UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES);
    LOGASSERT(0 != sliceCount);
    LOGASSERT(sliceIndex < sliceCount);
    LOGASSERT(referenceList0Count <= UTL_H264_MAX_REFERENCE_LIST_ENTRIES);
    LOGASSERT(referenceList1Count <= UTL_H264_MAX_REFERENCE_LIST_ENTRIES);
    LOGASSERT(0 == referenceList0Count || NULL != referenceList0);
    LOGASSERT(0 == referenceList1Count || NULL != referenceList1);
    LOGASSERT(lumaLog2WeightDenominator <= UTL_H264_MAX_WEIGHT_DENOMINATOR_LOG2);
    LOGASSERT(chromaLog2WeightDenominator <= UTL_H264_MAX_WEIGHT_DENOMINATOR_LOG2);
    LOGASSERT((FALSE == list0LumaWeightsEnabled && NULL == list0LumaWeights && NULL == list0LumaOffsets) || (FALSE != list0LumaWeightsEnabled && NULL != list0LumaWeights && NULL != list0LumaOffsets));
    LOGASSERT((FALSE == list0ChromaWeightsEnabled && NULL == list0ChromaWeights && NULL == list0ChromaOffsets) ||
              (FALSE != list0ChromaWeightsEnabled && NULL != list0ChromaWeights && NULL != list0ChromaOffsets));
    LOGASSERT((FALSE == list1LumaWeightsEnabled && NULL == list1LumaWeights && NULL == list1LumaOffsets) || (FALSE != list1LumaWeightsEnabled && NULL != list1LumaWeights && NULL != list1LumaOffsets));
    LOGASSERT((FALSE == list1ChromaWeightsEnabled && NULL == list1ChromaWeights && NULL == list1ChromaOffsets) ||
              (FALSE != list1ChromaWeightsEnabled && NULL != list1ChromaWeights && NULL != list1ChromaOffsets));
    LOGASSERT(0 != referenceList0Count || (FALSE == list0LumaWeightsEnabled && FALSE == list0ChromaWeightsEnabled));
    LOGASSERT(0 != referenceList1Count || (FALSE == list1LumaWeightsEnabled && FALSE == list1ChromaWeightsEnabled));
    LOGASSERT(cabacInitIdc <= UTL_H264_MAX_CABAC_INIT_IDC);

    Utl_H264_DeriveCodedDimensions(width, height, interlaced, &width_in_mbs, &height_in_mbs, &padded_width, &padded_height);
    total_macroblocks = width_in_mbs * height_in_mbs;
    LOGASSERT(sliceCount <= total_macroblocks);

    macroblocks_per_slice        = total_macroblocks / sliceCount;
    extra_macroblocks            = total_macroblocks % sliceCount;
    first_macroblock             = sliceIndex * macroblocks_per_slice + (sliceIndex < extra_macroblocks ? sliceIndex : extra_macroblocks);
    macroblock_count             = macroblocks_per_slice + (sliceIndex < extra_macroblocks ? 1u : 0u);
    slice_quantization_parameter = (int32_t)pictureQuantizationParameter + quantizationParameterDelta;
    LOGASSERT(slice_quantization_parameter >= 0 && slice_quantization_parameter <= (int32_t)UTL_H264_MAX_QUANTIZATION_PARAMETER);

    LOGASSERT(pictureType >= VAEncPictureTypeIntra && pictureType <= VAEncPictureTypeBidirectional);
    LOGASSERT(FALSE == idrPicture || VAEncPictureTypeIntra == pictureType);

    idr_picture = idrPicture;
    LOGASSERT(VAEncPictureTypeIntra != pictureType || (0 == referenceList0Count && 0 == referenceList1Count));
    LOGASSERT(VAEncPictureTypePredictive != pictureType || (0 != referenceList0Count && 0 == referenceList1Count));
    LOGASSERT(VAEncPictureTypeBidirectional != pictureType || (0 != referenceList0Count && 0 != referenceList1Count));
    LOGASSERT(VAEncPictureTypeBidirectional == pictureType || FALSE == directSpatialMotionVectorPrediction);
    LOGASSERT(FALSE != useCabac || 0 == cabacInitIdc);
    LOGASSERT(referenceList0Count <= decodedPictureBufferCount);
    LOGASSERT(referenceList1Count <= decodedPictureBufferCount);

    pic_order_count_mask = Utl_H264_DeriveValueMask(Utl_H264_DeriveMaximumPicOrderCntLsb(Utl_H264_DeriveMaximumFrameNumber(intraIdrPeriod)));
    Utl_H264_DeriveActiveReferenceCounts(UTL_H264_SLICE_TYPE_I, 0, &default_reference_count_l0, &default_reference_count_l1);
    override_reference_counts =
        (0 != referenceList0Count ? (uint32_t)referenceList0Count : 1u) != default_reference_count_l0 || (0 != referenceList1Count ? (uint32_t)referenceList1Count : 1u) != default_reference_count_l1;

    LOGASSERT(0 != macroblock_count);
    LOGASSERT((uint8_t)directSpatialMotionVectorPrediction <= 1u);
    LOGASSERT(referenceList0Count <= sizeof(slice->RefPicList0) / sizeof(slice->RefPicList0[0]));
    LOGASSERT(referenceList1Count <= sizeof(slice->RefPicList1) / sizeof(slice->RefPicList1[0]));
    LOGASSERT((uint8_t)list0LumaWeightsEnabled <= 1u);
    LOGASSERT((uint8_t)list0ChromaWeightsEnabled <= 1u);
    LOGASSERT((uint8_t)list1LumaWeightsEnabled <= 1u);
    LOGASSERT((uint8_t)list1ChromaWeightsEnabled <= 1u);
    LOGASSERT(deblockingAlphaOffsetDiv2 >= UTL_H264_MIN_DEBLOCKING_FILTER_OFFSET_DIV2 && deblockingAlphaOffsetDiv2 <= UTL_H264_MAX_DEBLOCKING_FILTER_OFFSET_DIV2);
    LOGASSERT(deblockingBetaOffsetDiv2 >= UTL_H264_MIN_DEBLOCKING_FILTER_OFFSET_DIV2 && deblockingBetaOffsetDiv2 <= UTL_H264_MAX_DEBLOCKING_FILTER_OFFSET_DIV2);

    (void)memset(slice, 0, sizeof((*slice)));
    slice->macroblock_address               = first_macroblock;
    slice->num_macroblocks                  = macroblock_count;
    slice->macroblock_info                  = macroblockInfoBufId;
    slice->slice_type                       = DeriveSliceTypeH264(pictureType);
    slice->pic_parameter_set_id             = 0;
    slice->idr_pic_id                       = idr_picture ? idrPicId : 0;
    slice->pic_order_cnt_lsb                = idr_picture ? 0u : (uint16_t)(Utl_H264_DeriveFieldOrderCount(frameIndex) & pic_order_count_mask);
    slice->delta_pic_order_cnt_bottom       = 0;
    slice->delta_pic_order_cnt[0]           = 0;
    slice->delta_pic_order_cnt[1]           = 0;
    slice->direct_spatial_mv_pred_flag      = (uint8_t)directSpatialMotionVectorPrediction;
    slice->num_ref_idx_active_override_flag = (uint8_t)override_reference_counts;
    slice->num_ref_idx_l0_active_minus1     = (uint8_t)((0 != referenceList0Count ? (uint32_t)referenceList0Count : 1u) - 1u);
    slice->num_ref_idx_l1_active_minus1     = (uint8_t)((0 != referenceList1Count ? (uint32_t)referenceList1Count : 1u) - 1u);
    CopyH264Pictures(slice->RefPicList0, sizeof(slice->RefPicList0) / sizeof(slice->RefPicList0[0]), referenceList0, referenceList0Count);
    CopyH264Pictures(slice->RefPicList1, sizeof(slice->RefPicList1) / sizeof(slice->RefPicList1[0]), referenceList1, referenceList1Count);
    slice->luma_log2_weight_denom   = lumaLog2WeightDenominator;
    slice->chroma_log2_weight_denom = chromaLog2WeightDenominator;
    slice->luma_weight_l0_flag      = (uint8_t)list0LumaWeightsEnabled;
    slice->chroma_weight_l0_flag    = (uint8_t)list0ChromaWeightsEnabled;
    slice->luma_weight_l1_flag      = (uint8_t)list1LumaWeightsEnabled;
    slice->chroma_weight_l1_flag    = (uint8_t)list1ChromaWeightsEnabled;
    if (NULL != list0LumaWeights)
    {
        (void)memcpy(slice->luma_weight_l0, list0LumaWeights, referenceList0Count * sizeof(slice->luma_weight_l0[0]));
    }
    if (NULL != list0LumaOffsets)
    {
        (void)memcpy(slice->luma_offset_l0, list0LumaOffsets, referenceList0Count * sizeof(slice->luma_offset_l0[0]));
    }
    if (NULL != list0ChromaWeights)
    {
        (void)memcpy(slice->chroma_weight_l0, list0ChromaWeights, referenceList0Count * sizeof(slice->chroma_weight_l0[0]));
    }
    if (NULL != list0ChromaOffsets)
    {
        (void)memcpy(slice->chroma_offset_l0, list0ChromaOffsets, referenceList0Count * sizeof(slice->chroma_offset_l0[0]));
    }
    if (NULL != list1LumaWeights)
    {
        (void)memcpy(slice->luma_weight_l1, list1LumaWeights, referenceList1Count * sizeof(slice->luma_weight_l1[0]));
    }
    if (NULL != list1LumaOffsets)
    {
        (void)memcpy(slice->luma_offset_l1, list1LumaOffsets, referenceList1Count * sizeof(slice->luma_offset_l1[0]));
    }
    if (NULL != list1ChromaWeights)
    {
        (void)memcpy(slice->chroma_weight_l1, list1ChromaWeights, referenceList1Count * sizeof(slice->chroma_weight_l1[0]));
    }
    if (NULL != list1ChromaOffsets)
    {
        (void)memcpy(slice->chroma_offset_l1, list1ChromaOffsets, referenceList1Count * sizeof(slice->chroma_offset_l1[0]));
    }
    slice->cabac_init_idc                = cabacInitIdc;
    slice->slice_qp_delta                = quantizationParameterDelta;
    slice->disable_deblocking_filter_idc = enableDeblocking ? 0u : 1u;
    slice->slice_alpha_c0_offset_div2    = deblockingAlphaOffsetDiv2;
    slice->slice_beta_offset_div2        = deblockingBetaOffsetDiv2;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_InitIQMatrixBufferH264(VAIQMatrixBufferH264* matrix,
                                      const uint8_t         scalingList4x4[UTL_H264_SCALING_LIST_4X4_TOTAL_ELEMENT_COUNT],
                                      const uint8_t         scalingList8x8[UTL_H264_SCALING_LIST_8X8_TOTAL_ELEMENT_COUNT])
{
    LOGASSERT(NULL != matrix);
    LOGASSERT(NULL != scalingList4x4);
    LOGASSERT(NULL != scalingList8x8);

    (void)memset(matrix, 0, sizeof((*matrix)));
    (void)memcpy(matrix->ScalingList4x4, scalingList4x4, sizeof(matrix->ScalingList4x4));
    (void)memcpy(matrix->ScalingList8x8, scalingList8x8, sizeof(matrix->ScalingList8x8));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_Initialize(VADisplay dpy, int32_t* majorVersion, int32_t* minorVersion)
{
    int max_num_profiles;
    int max_num_entrypoints;

    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != majorVersion);
    LOGASSERT(NULL != minorVersion);

    (void)vaSetErrorCallback(dpy, ErrorCallback, NULL);
    (void)vaSetInfoCallback(dpy, InfoCallback, NULL);

    (*majorVersion) = 0;
    (*minorVersion) = 0;
    VACHECK(vaInitialize(dpy, majorVersion, minorVersion));

    max_num_profiles = Gfx_Vaapi_MaxNumProfiles(dpy);
    LOGASSERT(VA_MAX_PROFILES >= max_num_profiles);

    max_num_entrypoints = Gfx_Vaapi_MaxNumEntrypoints(dpy);
    LOGASSERT(VA_MAX_ENTRYPOINTS >= max_num_entrypoints);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
const char* Gfx_Vaapi_QueryVendorString(VADisplay dpy)
{
    const char* vendor_string;

    LOGASSERT(NULL != dpy);

    vendor_string = vaQueryVendorString(dpy);
    LOGASSERT(NULL != vendor_string);

    return vendor_string;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
int Gfx_Vaapi_MaxNumProfiles(VADisplay dpy)
{
    int max_num_profiles;

    LOGASSERT(NULL != dpy);

    max_num_profiles = vaMaxNumProfiles(dpy);
    LOGASSERT(0 < max_num_profiles);

    return max_num_profiles;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
int Gfx_Vaapi_MaxNumEntrypoints(VADisplay dpy)
{
    int max_num_entrypoints;

    LOGASSERT(NULL != dpy);

    max_num_entrypoints = vaMaxNumEntrypoints(dpy);
    LOGASSERT(0 < max_num_entrypoints);

    return max_num_entrypoints;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_QueryConfigProfiles(VADisplay dpy, VAProfile profileList[VA_MAX_PROFILES], int32_t* numProfiles)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != profileList);
    LOGASSERT(NULL != numProfiles);

    (*numProfiles) = 0;
    VACHECK(vaQueryConfigProfiles(dpy, profileList, numProfiles));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_QueryConfigEntrypoints(VADisplay dpy, VAProfile profile, VAEntrypoint entrypointList[VA_MAX_ENTRYPOINTS], int32_t* numEntrypoints)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != entrypointList);
    LOGASSERT(NULL != numEntrypoints);

    (*numEntrypoints) = 0;
    VACHECK(vaQueryConfigEntrypoints(dpy, profile, entrypointList, numEntrypoints));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_GetConfigAttributes(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib* attribList, int32_t numAttribs)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != attribList);

    VACHECK(vaGetConfigAttributes(dpy, profile, entrypoint, attribList, numAttribs));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreateConfig(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint, VAConfigAttrib* attribList, int32_t numAttribs, VAConfigID* configId)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != configId);

    VACHECK(vaCreateConfig(dpy, profile, entrypoint, attribList, numAttribs, configId));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_QuerySurfaceAttributes(VADisplay dpy, VAConfigID configId, VASurfaceAttrib* attribList, uint32_t* numAttribs)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != configId);
    LOGASSERT(NULL != numAttribs);

    if (NULL == attribList)
    {
        (*numAttribs) = 0;
    }
    else
    {
        LOGASSERT(0 != (*numAttribs));
        LOGASSERT(VA_MAX_SURFACE_ATTRIBUTES >= (*numAttribs));
    }
    VACHECK(vaQuerySurfaceAttributes(dpy, configId, attribList, numAttribs));
    LOGASSERT(VA_MAX_SURFACE_ATTRIBUTES >= (*numAttribs));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreateSurfaces(VADisplay dpy, uint32_t format, uint32_t width, uint32_t height, VASurfaceID* surfaces, uint32_t numSurfaces, VASurfaceAttrib* attribList, uint32_t numAttribs)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != surfaces);

    VACHECK(vaCreateSurfaces(dpy, format, width, height, surfaces, numSurfaces, attribList, numAttribs));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreateSurface(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, VASurfaceID* surface)
{
    VASurfaceAttrib attribute;

    LOGASSERT(NULL != dpy);
    LOGASSERT(0 != format);
    LOGASSERT(0 != pixelFormat);
    LOGASSERT(0 != width && 0 != height);
    LOGASSERT(NULL != surface);

    attribute.type          = VASurfaceAttribPixelFormat;
    attribute.flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attribute.value.type    = VAGenericValueTypeInteger;
    attribute.value.value.i = (int)pixelFormat;

    (*surface) = VA_INVALID_SURFACE;
    Gfx_Vaapi_CreateSurfaces(dpy, format, width, height, surface, 1, &attribute, 1);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreateExportableSurface(VADisplay              dpy,
                                       uint32_t               format,
                                       uint32_t               pixelFormat,
                                       uint32_t               width,
                                       uint32_t               height,
                                       VASurfaceID*           surface,
                                       const VASurfaceAttrib* additionalAttribList,
                                       uint32_t               numAdditionalAttribs)
{
    VASurfaceAttrib attrib_list[VA_MAX_SURFACE_ATTRIBUTES];
    uint32_t        num_attribs;

    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != surface);
    LOGASSERT(VA_INVALID_SURFACE == (*surface));
    LOGASSERT((NULL == additionalAttribList) == (0 == numAdditionalAttribs));
    LOGASSERT(numAdditionalAttribs <= (uint32_t)COUNT_OF(attrib_list) - 2u);

    (void)memset(attrib_list, 0, sizeof(attrib_list));
    if (0 != numAdditionalAttribs)
    {
        (void)memcpy(attrib_list, additionalAttribList, (size_t)numAdditionalAttribs * sizeof(attrib_list[0]));
    }
    num_attribs                            = numAdditionalAttribs;
    attrib_list[num_attribs].type          = VASurfaceAttribPixelFormat;
    attrib_list[num_attribs].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[num_attribs].value.type    = VAGenericValueTypeInteger;
    attrib_list[num_attribs].value.value.i = (int)pixelFormat;
    ++num_attribs;
    attrib_list[num_attribs].type          = VASurfaceAttribUsageHint;
    attrib_list[num_attribs].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[num_attribs].value.type    = VAGenericValueTypeInteger;
    attrib_list[num_attribs].value.value.i = VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER | VA_SURFACE_ATTRIB_USAGE_HINT_EXPORT;
    ++num_attribs;
    Gfx_Vaapi_CreateSurfaces(dpy, format, width, height, surface, 1, attrib_list, num_attribs);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_ExportSurfaceHandle(VADisplay dpy, VASurfaceID surfaceId, uint32_t memoryType, uint32_t flags, void* descriptor)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_SURFACE != surfaceId);
    LOGASSERT(NULL != descriptor);

    VACHECK(vaExportSurfaceHandle(dpy, surfaceId, memoryType, flags, descriptor));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreateContext(VADisplay dpy, VAConfigID configId, int32_t pictureWidth, int32_t pictureHeight, int32_t flag, VASurfaceID* renderTargets, int32_t numRenderTargets, VAContextID* context)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != configId);
    LOGASSERT(NULL != renderTargets);
    LOGASSERT(NULL != context);

    VACHECK(vaCreateContext(dpy, configId, pictureWidth, pictureHeight, flag, renderTargets, numRenderTargets, context));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreateBuffer(VADisplay dpy, VAContextID context, VABufferType type, uint32_t size, uint32_t numElements, void* data, VABufferID* bufId)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);
    LOGASSERT(NULL != bufId);

    VACHECK(vaCreateBuffer(dpy, context, type, size, numElements, data, bufId));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Uses the format's sample storage and coded dimensions without adding encoded headers.
 */
uint32_t Gfx_Vaapi_GetCodedSurfaceSize(uint32_t format, uint32_t width, uint32_t height, bool_t interlaced)
{
    uint32_t width_in_mbs;
    uint32_t height_in_mbs;
    uint32_t padded_width;
    uint32_t padded_height;
    uint64_t pixel_count;
    uint64_t size;

    Utl_H264_DeriveCodedDimensions(width, height, interlaced, &width_in_mbs, &height_in_mbs, &padded_width, &padded_height);
    pixel_count = (uint64_t)padded_width * padded_height;
    LOGASSERT(pixel_count <= UINT32_MAX);

    switch (format)
    {
    case VA_RT_FORMAT_YUV400:
        size = pixel_count;
        break;
    case VA_RT_FORMAT_YUV420:
    case VA_RT_FORMAT_YUV411:
        size = pixel_count * 3U / 2U;
        break;
    case VA_RT_FORMAT_YUV422:
    case VA_RT_FORMAT_RGB16:
        size = pixel_count * 2U;
        break;
    case VA_RT_FORMAT_YUV444:
    case VA_RT_FORMAT_YUV420_10:
    case VA_RT_FORMAT_YUV420_12:
    case VA_RT_FORMAT_RGBP:
        size = pixel_count * 3U;
        break;
    case VA_RT_FORMAT_YUV422_10:
    case VA_RT_FORMAT_YUV422_12:
    case VA_RT_FORMAT_RGB32:
    case VA_RT_FORMAT_RGB32_10:
        size = pixel_count * 4U;
        break;
    case VA_RT_FORMAT_YUV444_10:
    case VA_RT_FORMAT_YUV444_12:
        size = pixel_count * 6U;
        break;
    case VA_RT_FORMAT_PROTECTED:
    default:
        LOGFATAL("Unsupported coded-surface format!");
    }

    LOGASSERT(size <= UINT32_MAX);
    return (uint32_t)size;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreateCodedBuffer(VADisplay dpy, VAContextID context, uint32_t size, VABufferID* bufId)
{
    LOGASSERT(0 != size);

    Gfx_Vaapi_CreateBuffer(dpy, context, VAEncCodedBufferType, size, 1, NULL, bufId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreateSequenceParameterBufferH264(VADisplay dpy, VAContextID context, const VAEncSequenceParameterBufferH264* params, VABufferID* bufId)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);
    LOGASSERT(NULL != params);
    LOGASSERT(NULL != bufId);

    Gfx_Vaapi_CreateBuffer(dpy, context, VAEncSequenceParameterBufferType, (uint32_t)sizeof(VAEncSequenceParameterBufferH264), 1, (void*)params, bufId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreatePictureParameterBufferH264(VADisplay dpy, VAContextID context, const VAEncPictureParameterBufferH264* params, VABufferID* bufId)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);
    LOGASSERT(NULL != params);
    LOGASSERT(NULL != bufId);

    Gfx_Vaapi_CreateBuffer(dpy, context, VAEncPictureParameterBufferType, (uint32_t)sizeof(VAEncPictureParameterBufferH264), 1, (void*)params, bufId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreateSliceParameterBufferH264(VADisplay dpy, VAContextID context, const VAEncSliceParameterBufferH264* params, uint32_t paramCount, VABufferID* bufId)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);
    LOGASSERT(NULL != params);
    LOGASSERT(0 != paramCount);
    LOGASSERT(NULL != bufId);

    Gfx_Vaapi_CreateBuffer(dpy, context, VAEncSliceParameterBufferType, (uint32_t)sizeof(VAEncSliceParameterBufferH264), paramCount, (void*)params, bufId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreatePackedHeaderParameterBuffer(VADisplay dpy, VAContextID context, VAEncPackedHeaderType type, uint32_t bitLength, bool_t hasEmulationBytes, VABufferID* bufId)
{
    VAEncPackedHeaderParameterBuffer params;

    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);
    LOGASSERT(type >= VAEncPackedHeaderSequence && type <= VAEncPackedHeaderRawData);
    LOGASSERT(0 != bitLength);
    LOGASSERT(FALSE == hasEmulationBytes || TRUE == hasEmulationBytes);
    LOGASSERT(NULL != bufId);

    (void)memset(&params, 0, sizeof(params));
    params.type                = type;
    params.bit_length          = bitLength;
    params.has_emulation_bytes = (uint8_t)hasEmulationBytes;
    Gfx_Vaapi_CreateBuffer(dpy, context, VAEncPackedHeaderParameterBufferType, (uint32_t)sizeof(VAEncPackedHeaderParameterBuffer), 1, &params, bufId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreatePackedHeaderDataBuffer(VADisplay dpy, VAContextID context, const void* data, uint32_t bitLength, VABufferID* bufId)
{
    uint32_t data_size;

    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);
    LOGASSERT(0 != bitLength);
    LOGASSERT(NULL != bufId);

    data_size = (bitLength / CHAR_BIT) + (0 != bitLength % CHAR_BIT);
    Gfx_Vaapi_CreateBuffer(dpy, context, VAEncPackedHeaderDataBufferType, data_size, 1, (void*)data, bufId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreatePackedHeaderBuffers(VADisplay             dpy,
                                         VAContextID           context,
                                         VAEncPackedHeaderType type,
                                         uint32_t              bitLength,
                                         bool_t                hasEmulationBytes,
                                         const void*           data,
                                         VABufferID*           parameterBufId,
                                         VABufferID*           dataBufId)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);
    LOGASSERT(0 != bitLength);
    LOGASSERT(NULL != data);
    LOGASSERT(NULL != parameterBufId);
    LOGASSERT(NULL != dataBufId);

    Gfx_Vaapi_CreatePackedHeaderParameterBuffer(dpy, context, type, bitLength, hasEmulationBytes, parameterBufId);
    Gfx_Vaapi_CreatePackedHeaderDataBuffer(dpy, context, data, bitLength, dataBufId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
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
                                       VABufferID* rateControlBufId)
{
    _Alignas(VAEncMiscParameterRateControl) byte_t bytes[sizeof(VAEncMiscParameterBuffer) + sizeof(VAEncMiscParameterRateControl)];
    VAEncMiscParameterBuffer*                      header;
    VAEncMiscParameterRateControl*                 params;
    uint32_t                                       target_percentage;

    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);
    LOGASSERT(targetBitRate <= maximumBitRate);
    LOGASSERT((0 == targetBitRate) == (0 == maximumBitRate));
    LOGASSERT(initialQuantizationParameter <= UTL_H264_MAX_QUANTIZATION_PARAMETER);
    LOGASSERT(minimumQuantizationParameter <= UTL_H264_MAX_QUANTIZATION_PARAMETER);
    LOGASSERT(maximumQuantizationParameter <= UTL_H264_MAX_QUANTIZATION_PARAMETER);
    LOGASSERT(macroblockRateControlMode <= 2u);
    LOGASSERT(frameToleranceMode <= 2u);
    LOGASSERT(NULL != rateControlBufId);
    LOGASSERT(VA_INVALID_ID == (*rateControlBufId));

    target_percentage = 0;
    if (0 != maximumBitRate)
    {
        target_percentage = (uint32_t)(((uint64_t)targetBitRate * 100u + maximumBitRate - 1u) / maximumBitRate);
        LOGASSERT(0 != target_percentage);
        LOGASSERT(target_percentage <= 100u);
    }

    (void)memset(bytes, 0, sizeof(bytes));
    header                                       = (VAEncMiscParameterBuffer*)bytes;
    params                                       = (VAEncMiscParameterRateControl*)header->data;
    header->type                                 = VAEncMiscParameterTypeRateControl;
    params->bits_per_second                      = maximumBitRate;
    params->target_percentage                    = target_percentage;
    params->window_size                          = windowMilliseconds;
    params->initial_qp                           = initialQuantizationParameter;
    params->min_qp                               = minimumQuantizationParameter;
    params->basic_unit_size                      = basicUnitSize;
    params->rc_flags.bits.reset                  = reset;
    params->rc_flags.bits.disable_frame_skip     = disableFrameSkip;
    params->rc_flags.bits.disable_bit_stuffing   = disableBitStuffing;
    params->rc_flags.bits.mb_rate_control        = macroblockRateControlMode;
    params->rc_flags.bits.temporal_id            = temporalLayer;
    params->rc_flags.bits.cfs_I_frames           = constantFrameSizeIntraFrames;
    params->rc_flags.bits.enable_parallel_brc    = enableParallelRateControl;
    params->rc_flags.bits.enable_dynamic_scaling = enableDynamicScaling;
    params->rc_flags.bits.frame_tolerance_mode   = frameToleranceMode;
    params->ICQ_quality_factor                   = intelligentConstantQualityFactor;
    params->max_qp                               = maximumQuantizationParameter;
    params->quality_factor                       = qualityFactor;
    params->target_frame_size                    = targetFrameSize;
    Gfx_Vaapi_CreateBuffer(dpy, context, VAEncMiscParameterBufferType, (uint32_t)sizeof(bytes), 1, bytes, rateControlBufId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CreateFrameRateBuffer(VADisplay dpy, VAContextID context, uint16_t numerator, uint16_t denominator, uint8_t temporalLayer, VABufferID* frameRateBufId)
{
    _Alignas(VAEncMiscParameterFrameRate) byte_t bytes[sizeof(VAEncMiscParameterBuffer) + sizeof(VAEncMiscParameterFrameRate)];
    VAEncMiscParameterBuffer*                    header;
    VAEncMiscParameterFrameRate*                 params;

    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);
    LOGASSERT(0 != numerator);
    LOGASSERT(0 != denominator);
    LOGASSERT(NULL != frameRateBufId);
    LOGASSERT(VA_INVALID_ID == (*frameRateBufId));

    (void)memset(bytes, 0, sizeof(bytes));
    header                                   = (VAEncMiscParameterBuffer*)bytes;
    params                                   = (VAEncMiscParameterFrameRate*)header->data;
    header->type                             = VAEncMiscParameterTypeFrameRate;
    params->framerate                        = ((uint32_t)denominator << 16u) | numerator;
    params->framerate_flags.bits.temporal_id = temporalLayer;
    Gfx_Vaapi_CreateBuffer(dpy, context, VAEncMiscParameterBufferType, (uint32_t)sizeof(bytes), 1, bytes, frameRateBufId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_WritePackedHeaderBuffers(VADisplay dpy, VABufferID parameterBufId, VABufferID dataBufId, VAEncPackedHeaderType type, uint32_t bitLength, bool_t hasEmulationBytes, const void* data)
{
    VAEncPackedHeaderParameterBuffer params;
    uint32_t                         data_size;

    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != parameterBufId);
    LOGASSERT(VA_INVALID_ID != dataBufId);
    LOGASSERT(0 != bitLength);
    LOGASSERT(NULL != data);
    LOGASSERT(VAEncPackedHeaderSequence == type || VAEncPackedHeaderPicture == type || VAEncPackedHeaderSlice == type || VAEncPackedHeaderRawData == type);

    (void)memset(&params, 0, sizeof(params));
    params.type                = type;
    params.bit_length          = bitLength;
    params.has_emulation_bytes = (uint8_t)hasEmulationBytes;
    data_size                  = (bitLength / CHAR_BIT) + (0 != bitLength % CHAR_BIT);
    Gfx_Vaapi_WriteMemory(dpy, parameterBufId, &params, (uint32_t)sizeof(params));
    Gfx_Vaapi_WriteMemory(dpy, dataBufId, data, data_size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_MapBuffer2(VADisplay dpy, VABufferID bufId, void** pbuf, uint32_t flags)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != bufId);
    LOGASSERT(NULL != pbuf);

    (*pbuf) = NULL;
    VACHECK(vaMapBuffer2(dpy, bufId, pbuf, flags));
    LOGASSERT(NULL != (*pbuf));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_UnmapBuffer(VADisplay dpy, VABufferID bufId)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != bufId);

    VACHECK(vaUnmapBuffer(dpy, bufId));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_DeriveImage(VADisplay dpy, VASurfaceID surface, VAImage* image)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_SURFACE != surface);
    LOGASSERT(NULL != image);

    VACHECK(vaDeriveImage(dpy, surface, image));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_BeginPicture(VADisplay dpy, VAContextID context, VASurfaceID renderTarget)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);
    LOGASSERT(VA_INVALID_SURFACE != renderTarget);

    VACHECK(vaBeginPicture(dpy, context, renderTarget));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_RenderPicture(VADisplay dpy, VAContextID context, VABufferID* bufIds, int32_t numBufIds)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);
    LOGASSERT(NULL != bufIds);

    VACHECK(vaRenderPicture(dpy, context, bufIds, numBufIds));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_EndPicture(VADisplay dpy, VAContextID context)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != context);

    VACHECK(vaEndPicture(dpy, context));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_SyncSurface(VADisplay dpy, VASurfaceID renderTarget)
{
    VAStatus                       status;
    VAStatus                       query_status;
    void*                          error_info;
    const VASurfaceDecodeMBErrors* surface_error;

    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_SURFACE != renderTarget);

    status = vaSyncSurface(dpy, renderTarget);
    if (VA_STATUS_SUCCESS == status || VA_STATUS_ERROR_DECODING_ERROR == status || VA_STATUS_ERROR_HW_BUSY == status)
    {
        query_status = Gfx_Vaapi_QuerySurfaceError(dpy, renderTarget, status, &error_info);
        if (VA_STATUS_SUCCESS == query_status && NULL != error_info)
        {
            for (surface_error = (const VASurfaceDecodeMBErrors*)error_info; -1 != surface_error->status; ++surface_error)
            {
                if (1 == surface_error->status)
                {
                    LOGSTART(stderr);
                    LOGPRINTS("VA-API surface ");
                    LOGPRINTU((uint64_t)renderTarget);
                    LOGPRINTS(" diagnostic: type=");
                    LOGPRINTI((int64_t)surface_error->decode_error_type);
                    LOGPRINTS(" start_mb=");
                    LOGPRINTU((uint64_t)surface_error->start_mb);
                    LOGPRINTS(" end_mb=");
                    LOGPRINTU((uint64_t)surface_error->end_mb);
                    LOGPRINTS(" num_mb=");
                    LOGPRINTU((uint64_t)surface_error->num_mb);
                    LOGPRINTS("\n");
                    LOGEND();
                }
            }
        }
        else if (VA_STATUS_SUCCESS != query_status && VA_STATUS_SUCCESS != status)
        {
            LOGSTART(stderr);
            LOGPRINTS("VA-API surface ");
            LOGPRINTU((uint64_t)renderTarget);
            LOGPRINTS(" diagnostics unavailable: ");
            LOGPRINTS(vaErrorStr(query_status));
            LOGPRINTS("\n");
            LOGEND();
        }
    }
    Gfx_Vaapi_CheckError(status, "vaSyncSurface(dpy, renderTarget)", __FILE__, __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_QuerySurfaceStatus(VADisplay dpy, VASurfaceID renderTarget, VASurfaceStatus* status)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_SURFACE != renderTarget);
    LOGASSERT(NULL != status);

    VACHECK(vaQuerySurfaceStatus(dpy, renderTarget, status));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Diagnostic queries must not replace the original synchronization result.
 */
VAStatus Gfx_Vaapi_QuerySurfaceError(VADisplay dpy, VASurfaceID surface, VAStatus errorStatus, void** errorInfo)
{
    VAStatus status;

    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_SURFACE != surface);
    LOGASSERT(NULL != errorInfo);

    (*errorInfo) = NULL;
    status       = vaQuerySurfaceError(dpy, surface, errorStatus, errorInfo);
    if (VA_STATUS_SUCCESS != status)
    {
        (*errorInfo) = NULL;
    }
    return status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Vaapi_GetProfileSupport(VADisplay dpy, VAProfile profile)
{
    bool_t    supported;
    int32_t   num_profiles;
    VAProfile profiles[VA_MAX_PROFILES];
    uint32_t  index;

    LOGASSERT(NULL != dpy);

    Gfx_Vaapi_QueryConfigProfiles(dpy, profiles, &num_profiles);

    supported = FALSE;
    for (index = 0; index < (uint32_t)num_profiles; ++index)
    {
        if (profile == profiles[index])
        {
            supported = TRUE;
            break;
        }
    }

    return supported;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Vaapi_GetEntrypointSupport(VADisplay dpy, VAProfile profile, VAEntrypoint entrypoint)
{
    bool_t       supported;
    int32_t      num_entrypoints;
    VAEntrypoint entrypoints[VA_MAX_ENTRYPOINTS];
    uint32_t     index;

    LOGASSERT(NULL != dpy);

    supported = FALSE;
    if (Gfx_Vaapi_GetProfileSupport(dpy, profile))
    {
        Gfx_Vaapi_QueryConfigEntrypoints(dpy, profile, entrypoints, &num_entrypoints);

        for (index = 0; index < (uint32_t)num_entrypoints; ++index)
        {
            if (entrypoint == entrypoints[index])
            {
                supported = TRUE;
                break;
            }
        }
    }

    return supported;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Vaapi_GetEncodeSliceSupport(VADisplay dpy, VAProfile profile)
{
    return Gfx_Vaapi_GetEntrypointSupport(dpy, profile, VAEntrypointEncSlice) || Gfx_Vaapi_GetEntrypointSupport(dpy, profile, VAEntrypointEncSliceLP);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_SelectProfile(VADisplay dpy, const VAProfile preferredProfiles[VA_MAX_PROFILES], int32_t numPreferredProfiles, VAProfile* profile)
{
    VAProfile profiles[VA_MAX_PROFILES];
    int32_t   num_profiles;
    uint32_t  preferred_index;
    uint32_t  available_index;

    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != preferredProfiles);
    LOGASSERT(0 < numPreferredProfiles);
    LOGASSERT(VA_MAX_PROFILES >= numPreferredProfiles);
    LOGASSERT(NULL != profile);

    Gfx_Vaapi_QueryConfigProfiles(dpy, profiles, &num_profiles);

    (*profile) = VAProfileNone;
    for (preferred_index = 0; preferred_index < (uint32_t)numPreferredProfiles; ++preferred_index)
    {
        for (available_index = 0; available_index < (uint32_t)num_profiles; ++available_index)
        {
            if (profiles[available_index] == preferredProfiles[preferred_index])
            {
                (*profile) = preferredProfiles[preferred_index];
                return;
            }
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Vaapi_GetSurfaceAttributeSupport(VADisplay dpy, VAConfigID configId, VASurfaceAttribType type, uint32_t value)
{
    bool_t          supported;
    VASurfaceAttrib attrib_list[VA_MAX_SURFACE_ATTRIBUTES];
    uint32_t        num_attribs;
    uint32_t        index;
    bool_t          type_matches;
    bool_t          value_type_matches;
    bool_t          value_matches;

    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != configId);

    Gfx_Vaapi_QuerySurfaceAttributes(dpy, configId, NULL, &num_attribs);

    supported = FALSE;
    if (0 < num_attribs)
    {
        Gfx_Vaapi_QuerySurfaceAttributes(dpy, configId, attrib_list, &num_attribs);
        for (index = 0; index < (uint32_t)num_attribs; ++index)
        {
            type_matches       = type == attrib_list[index].type;
            value_type_matches = VAGenericValueTypeInteger == attrib_list[index].value.type;
            value_matches      = FALSE;

            if (type_matches && value_type_matches)
            {
                switch (type)
                {
                case VASurfaceAttribMemoryType:
                case VASurfaceAttribUsageHint:
                    value_matches = (0 != ((uint32_t)attrib_list[index].value.value.i & value));
                    break;

                default:
                    value_matches = (value == (uint32_t)attrib_list[index].value.value.i);
                    break;
                }
            }

            if (type_matches && value_type_matches && value_matches)
            {
                supported = TRUE;
                break;
            }
        }
    }

    return supported;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Vaapi_GetSurfaceMemoryTypeSupport(VADisplay dpy, VAConfigID configId, uint32_t memoryType)
{
    return Gfx_Vaapi_GetSurfaceAttributeSupport(dpy, configId, VASurfaceAttribMemoryType, memoryType);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Vaapi_GetSurfacePixelFormatSupport(VADisplay dpy, VAConfigID configId, uint32_t pixelFormat)
{
    return Gfx_Vaapi_GetSurfaceAttributeSupport(dpy, configId, VASurfaceAttribPixelFormat, pixelFormat);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_ReadMemory(VADisplay dpy, VABufferID bufId, void* dst, uint32_t dstSize)
{
    void* pbuf;

    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != bufId);
    LOGASSERT(NULL != dst);

    Gfx_Vaapi_MapBuffer2(dpy, bufId, (void**)&pbuf, VA_MAPBUFFER_FLAG_READ);

    (void)memcpy(dst, pbuf, (size_t)dstSize);

    Gfx_Vaapi_UnmapBuffer(dpy, bufId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_WriteMemory(VADisplay dpy, VABufferID bufId, const void* src, uint32_t srcSize)
{
    void* pbuf;

    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_ID != bufId);
    LOGASSERT(NULL != src);

    Gfx_Vaapi_MapBuffer2(dpy, bufId, &pbuf, VA_MAPBUFFER_FLAG_WRITE);

    (void)memcpy(pbuf, src, (size_t)srcSize);

    Gfx_Vaapi_UnmapBuffer(dpy, bufId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_ReadImageMemory(VADisplay dpy, const VAImage* image, void* dst, uint32_t dstSize)
{
    uint32_t      offsets[VA_MAX_IMAGE_PLANES];
    uint32_t      row_sizes[VA_MAX_IMAGE_PLANES];
    uint32_t      row_counts[VA_MAX_IMAGE_PLANES];
    uint32_t      layout_size;
    const byte_t* pbuf;

    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != image);
    LOGASSERT(NULL != dst);

    layout_size = Gfx_Vaapi_GetImagePlaneLayout(image, offsets, row_sizes, row_counts);
    LOGASSERT(dstSize >= layout_size);

    Gfx_Vaapi_MapBuffer2(dpy, image->buf, (void**)&pbuf, VA_MAPBUFFER_FLAG_READ);

    CopyImagePlanes(dst, offsets, row_sizes, pbuf, image->offsets, image->pitches, row_sizes, row_counts, image->num_planes);

    Gfx_Vaapi_UnmapBuffer(dpy, image->buf);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_WriteImageMemory(VADisplay dpy, const VAImage* image, const void* src, uint32_t srcSize)
{
    uint32_t offsets[VA_MAX_IMAGE_PLANES];
    uint32_t row_sizes[VA_MAX_IMAGE_PLANES];
    uint32_t row_counts[VA_MAX_IMAGE_PLANES];
    uint32_t layout_size;
    byte_t*  pbuf;

    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != image);
    LOGASSERT(NULL != src);

    layout_size = Gfx_Vaapi_GetImagePlaneLayout(image, offsets, row_sizes, row_counts);
    LOGASSERT(srcSize >= layout_size);

    Gfx_Vaapi_MapBuffer2(dpy, image->buf, (void**)&pbuf, VA_MAPBUFFER_FLAG_WRITE);

    CopyImagePlanes(pbuf, image->offsets, image->pitches, src, offsets, row_sizes, row_sizes, row_counts, image->num_planes);

    Gfx_Vaapi_UnmapBuffer(dpy, image->buf);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_CopyCodedBufferSegments(VADisplay dpy, VABufferID bufId, void* dst, uint32_t dstSize, uint32_t* codedSize)
{
    VACodedBufferSegment* seg;
    byte_t*               dst_bytes;
    uint32_t              offset;
    uint32_t              count;

    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != dst);
    LOGASSERT(NULL != codedSize);

    dst_bytes = (byte_t*)dst;
    Gfx_Vaapi_MapBuffer2(dpy, bufId, (void**)&seg, VA_MAPBUFFER_FLAG_READ);

    offset = 0;
    count  = 1;

    for (;;)
    {
        if (NULL == seg)
        {
            break;
        }

        if (VA_MAX_CODED_BUFFER_SEGMENTS <= count)
        {
            break;
        }

        CheckSegmentIntegrity(seg);

        LOGASSERT((dstSize - offset) >= seg->size);

        (void)memcpy(dst_bytes + offset, seg->buf, (size_t)seg->size);

        offset += seg->size;

        seg = (VACodedBufferSegment*)seg->next;

        ++count;
    }

    Gfx_Vaapi_UnmapBuffer(dpy, bufId);

    (*codedSize) = offset;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vaapi_GetImagePlaneLayout(const VAImage* image, uint32_t offsets[VA_MAX_IMAGE_PLANES], uint32_t rowSizes[VA_MAX_IMAGE_PLANES], uint32_t rowCounts[VA_MAX_IMAGE_PLANES])
{
    size_t   layout_size;
    size_t   packed_row_size;
    size_t   plane_end;
    size_t   plane_size;
    uint32_t plane_index;

    LOGASSERT(NULL != image);
    LOGASSERT(0 != image->width);
    LOGASSERT(0 != image->height);
    LOGASSERT(0 != image->num_planes && (VA_MAX_IMAGE_PLANES >= image->num_planes));

    (void)memset(offsets, 0, VA_MAX_IMAGE_PLANES * sizeof(uint32_t));
    (void)memset(rowSizes, 0, VA_MAX_IMAGE_PLANES * sizeof(uint32_t));
    (void)memset(rowCounts, 0, VA_MAX_IMAGE_PLANES * sizeof(uint32_t));

    if (1 == image->num_planes)
    {
        LOGASSERT(0 != image->format.bits_per_pixel);

        packed_row_size = ((size_t)image->width * image->format.bits_per_pixel + 7u) / 8u;
        LOGASSERT(UINT32_MAX >= packed_row_size);

        rowSizes[0]  = (uint32_t)packed_row_size;
        rowCounts[0] = image->height;
    }
    else
    {
        switch (image->format.fourcc)
        {
        case VA_FOURCC_NV12:
        case VA_FOURCC_NV21:
            LOGASSERT(2 == image->num_planes);
            LOGASSERT(0 == (image->width & 1u));
            LOGASSERT(0 == (image->height & 1u));

            rowSizes[0]  = image->width;
            rowSizes[1]  = image->width;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height / 2u;
            break;

        case VA_FOURCC_P010:
        case VA_FOURCC_P012:
        case VA_FOURCC_P016:
            LOGASSERT(2 == image->num_planes);
            LOGASSERT(0 == (image->width & 1u));
            LOGASSERT(0 == (image->height & 1u));

            rowSizes[0]  = (uint32_t)image->width * 2u;
            rowSizes[1]  = (uint32_t)image->width * 2u;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height / 2u;
            break;

        case VA_FOURCC_P208:
            LOGASSERT(2 == image->num_planes);
            LOGASSERT(0 == (image->width & 1u));

            rowSizes[0]  = image->width;
            rowSizes[1]  = image->width;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height;
            break;

        case VA_FOURCC_NV11:
            LOGASSERT(2 == image->num_planes);
            LOGASSERT(0 == (image->width & 3u));

            rowSizes[0]  = image->width;
            rowSizes[1]  = image->width / 2u;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height;
            break;

        case VA_FOURCC_I420:
        case VA_FOURCC_IYUV:
        case VA_FOURCC_YV12:
        case VA_FOURCC_IMC3:
            LOGASSERT(3 == image->num_planes);
            LOGASSERT(0 == (image->width & 1u));
            LOGASSERT(0 == (image->height & 1u));

            rowSizes[0]  = image->width;
            rowSizes[1]  = image->width / 2u;
            rowSizes[2]  = image->width / 2u;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height / 2u;
            rowCounts[2] = image->height / 2u;
            break;

        case VA_FOURCC_I010:
            LOGASSERT(3 == image->num_planes);
            LOGASSERT(0 == (image->width & 1u));
            LOGASSERT(0 == (image->height & 1u));

            rowSizes[0]  = (uint32_t)image->width * 2u;
            rowSizes[1]  = image->width;
            rowSizes[2]  = image->width;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height / 2u;
            rowCounts[2] = image->height / 2u;
            break;

        case VA_FOURCC_YV16:
        case VA_FOURCC_422H:
            LOGASSERT(3 == image->num_planes);
            LOGASSERT(0 == (image->width & 1u));

            rowSizes[0]  = image->width;
            rowSizes[1]  = image->width / 2u;
            rowSizes[2]  = image->width / 2u;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height;
            rowCounts[2] = image->height;
            break;

        case VA_FOURCC_422V:
            LOGASSERT(3 == image->num_planes);
            LOGASSERT(0 == (image->height & 1u));

            rowSizes[0]  = image->width;
            rowSizes[1]  = image->width;
            rowSizes[2]  = image->width;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height / 2u;
            rowCounts[2] = image->height / 2u;
            break;

        case VA_FOURCC_411P:
            LOGASSERT(3 == image->num_planes);
            LOGASSERT(0 == (image->width & 3u));

            rowSizes[0]  = image->width;
            rowSizes[1]  = image->width / 4u;
            rowSizes[2]  = image->width / 4u;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height;
            rowCounts[2] = image->height;
            break;

        case VA_FOURCC_411R:
            LOGASSERT(3 == image->num_planes);
            LOGASSERT(0 == (image->height & 3u));

            rowSizes[0]  = image->width;
            rowSizes[1]  = image->width;
            rowSizes[2]  = image->width;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height / 4u;
            rowCounts[2] = image->height / 4u;
            break;

        case VA_FOURCC_YV24:
        case VA_FOURCC_444P:
        case VA_FOURCC_RGBP:
        case VA_FOURCC_BGRP:
            LOGASSERT(3 == image->num_planes);

            rowSizes[0]  = image->width;
            rowSizes[1]  = image->width;
            rowSizes[2]  = image->width;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height;
            rowCounts[2] = image->height;
            break;

        case VA_FOURCC_Q416:
            LOGASSERT(3 == image->num_planes);

            rowSizes[0]  = (uint32_t)image->width * 2u;
            rowSizes[1]  = (uint32_t)image->width * 2u;
            rowSizes[2]  = (uint32_t)image->width * 2u;
            rowCounts[0] = image->height;
            rowCounts[1] = image->height;
            rowCounts[2] = image->height;
            break;

        default:
            LOGFATAL("unsupported VA image plane layout");
        }
    }

    layout_size = 0;
    for (plane_index = 0; plane_index < image->num_planes; ++plane_index)
    {
        LOGASSERT(rowSizes[plane_index] <= image->pitches[plane_index]);
        LOGASSERT(layout_size <= UINT32_MAX);

        offsets[plane_index] = (uint32_t)layout_size;
        plane_size           = (size_t)rowSizes[plane_index] * rowCounts[plane_index];
        LOGASSERT(SIZE_MAX - plane_size >= layout_size);

        layout_size += plane_size;
        plane_end = (size_t)image->offsets[plane_index] + (size_t)(rowCounts[plane_index] - 1u) * image->pitches[plane_index] + rowSizes[plane_index];
        LOGASSERT(plane_end <= image->data_size);
    }

    LOGASSERT(layout_size <= UINT32_MAX);

    return (uint32_t)layout_size;
}
