#ifndef UTL_H264_H
#define UTL_H264_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * ITU-T H.264 (08/2024), clauses 7.3/7.4, 9.1, Annexes A, B and E.
 * https://www.itu.int/rec/T-REC-H.264-202408-S/en
 * Adapted from C:/dev/vulkan-va-zero-copy/include/utl/utl_h264.h and its implementation.
 * The retained encoder models cover the documented SPS/PPS/SEI subset. They do not establish
 * complete H.264 parsing, level/profile conformance or coverage of every model field.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"

#include <stddef.h>
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/* ITU-T H.264 Annex D: SEI payload types. */
#define UTL_H264_SEI_PAYLOAD_TYPE_USER_DATA_UNREGISTERED (5) ///< user_data_unregistered SEI payload type.

/* ITU-T H.264 clause 7.4.2.1.1 and profile Annexes A, F, G, H, and I: profile_idc values. */
#define UTL_H264_PROFILE_CAVLC_444_INTRA               (44)  ///< CAVLC 4:4:4 Intra profile_idc.
#define UTL_H264_PROFILE_BASELINE                      (66)  ///< Baseline profile_idc.
#define UTL_H264_PROFILE_EXTENDED                      (88)  ///< Extended profile_idc.
#define UTL_H264_PROFILE_MAIN                          (77)  ///< Main profile_idc.
#define UTL_H264_PROFILE_SCALABLE_BASELINE             (83)  ///< Scalable Baseline profile_idc.
#define UTL_H264_PROFILE_SCALABLE_HIGH                 (86)  ///< Scalable High profile_idc.
#define UTL_H264_PROFILE_HIGH                          (100) ///< High profile_idc.
#define UTL_H264_PROFILE_HIGH_10                       (110) ///< High 10 profile_idc.
#define UTL_H264_PROFILE_MULTIVIEW_HIGH                (118) ///< Multiview High profile_idc.
#define UTL_H264_PROFILE_HIGH_422                      (122) ///< High 4:2:2 profile_idc.
#define UTL_H264_PROFILE_STEREO_HIGH                   (128) ///< Stereo High profile_idc.
#define UTL_H264_PROFILE_MFC_HIGH                      (134) ///< MFC High profile_idc.
#define UTL_H264_PROFILE_MFC_DEPTH_HIGH                (135) ///< MFC Depth High profile_idc.
#define UTL_H264_PROFILE_MULTIVIEW_DEPTH_HIGH          (138) ///< Multiview Depth High profile_idc.
#define UTL_H264_PROFILE_ENHANCED_MULTIVIEW_DEPTH_HIGH (139) ///< Enhanced Multiview Depth High profile_idc.
#define UTL_H264_PROFILE_HIGH_444_PREDICTIVE           (244) ///< High 4:4:4 Predictive profile_idc.

/* ITU-T H.264 clause 7.4.2.1.1: constraint_set flags. */
#define UTL_H264_CONSTRAINT_SET0_FLAG (0x80) ///< constraint_set0_flag mask in profile compatibility.
#define UTL_H264_CONSTRAINT_SET1_FLAG (0x40) ///< constraint_set1_flag mask in profile compatibility.
#define UTL_H264_CONSTRAINT_SET2_FLAG (0x20) ///< constraint_set2_flag mask in profile compatibility.
#define UTL_H264_CONSTRAINT_SET3_FLAG (0x10) ///< constraint_set3_flag mask in profile compatibility.
#define UTL_H264_CONSTRAINT_SET4_FLAG (0x08) ///< constraint_set4_flag mask in profile compatibility.
#define UTL_H264_CONSTRAINT_SET5_FLAG (0x04) ///< constraint_set5_flag mask in profile compatibility.

/* ITU-T H.264 Table 6-1 and clause 7.4.2.1.1: chroma_format_idc values. */
#define UTL_H264_CHROMA_FORMAT_MONOCHROME (0) ///< Monochrome chroma_format_idc.
#define UTL_H264_CHROMA_FORMAT_420        (1) ///< 4:2:0 chroma_format_idc.
#define UTL_H264_CHROMA_FORMAT_422        (2) ///< 4:2:2 chroma_format_idc.
#define UTL_H264_CHROMA_FORMAT_444        (3) ///< 4:4:4 chroma_format_idc.

/* ITU-T H.264 clause 7.4.2.1.1: component bit-depth bounds. */
#define UTL_H264_MIN_BIT_DEPTH (8)  ///< Minimum luma or chroma bit depth.
#define UTL_H264_MAX_BIT_DEPTH (14) ///< Maximum luma or chroma bit depth.

/* ITU-T H.264 Annex E.2.1: max_bytes_per_pic_denom and max_bits_per_mb_denom. */
#define UTL_H264_SIZE_LIMIT_UNSPECIFIED                                                                                \
    (0) ///< No per-picture byte or per-macroblock bit limit; bitrate is controlled separately.

/* ITU-T H.264 Annex E.2.1, Table E-1: sample-aspect-ratio identifiers. */
#define UTL_H264_ASPECT_RATIO_IDC_UNSPECIFIED  (0)   ///< Unspecified sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_1_TO_1       (1)   ///< 1:1 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_12_TO_11     (2)   ///< 12:11 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_10_TO_11     (3)   ///< 10:11 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_16_TO_11     (4)   ///< 16:11 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_40_TO_33     (5)   ///< 40:33 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_24_TO_11     (6)   ///< 24:11 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_20_TO_11     (7)   ///< 20:11 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_32_TO_11     (8)   ///< 32:11 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_80_TO_33     (9)   ///< 80:33 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_18_TO_11     (10)  ///< 18:11 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_15_TO_11     (11)  ///< 15:11 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_64_TO_33     (12)  ///< 64:33 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_160_TO_99    (13)  ///< 160:99 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_4_TO_3       (14)  ///< 4:3 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_3_TO_2       (15)  ///< 3:2 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_2_TO_1       (16)  ///< 2:1 sample aspect ratio.
#define UTL_H264_ASPECT_RATIO_IDC_EXTENDED_SAR (255) ///< Explicit sar_width and sar_height follow.

/* ITU-T H.264 clauses 7.4.2.1.1 and 8.2.1: picture-order-count types. */
#define UTL_H264_PIC_ORDER_CNT_TYPE_0 (0) ///< POC type using pic_order_cnt_lsb.
#define UTL_H264_PIC_ORDER_CNT_TYPE_1 (1) ///< POC type using delta cycles.
#define UTL_H264_PIC_ORDER_CNT_TYPE_2 (2) ///< POC type derived from frame_num.

/* ITU-T H.264 clause 7.4.3, Table 7-6: slice_type values. */
#define UTL_H264_SLICE_TYPE_P          (0) ///< Predictive slice_type permitting mixed slice types.
#define UTL_H264_SLICE_TYPE_B          (1) ///< Bidirectional slice_type permitting mixed slice types.
#define UTL_H264_SLICE_TYPE_I          (2) ///< Intra slice_type permitting mixed slice types.
#define UTL_H264_SLICE_TYPE_SP         (3) ///< Switching predictive slice_type.
#define UTL_H264_SLICE_TYPE_SI         (4) ///< Switching intra slice_type.
#define UTL_H264_SLICE_TYPE_ALL_OFFSET (5) ///< Offset denoting that all slices share the indicated type.
#define UTL_H264_SLICE_TYPE_ALL_P                                                                                      \
    (UTL_H264_SLICE_TYPE_P + UTL_H264_SLICE_TYPE_ALL_OFFSET) ///< Predictive slice_type declaring all slices predictive.
#define UTL_H264_SLICE_TYPE_ALL_B                                                                                      \
    (UTL_H264_SLICE_TYPE_B +                                                                                           \
     UTL_H264_SLICE_TYPE_ALL_OFFSET) ///< Bidirectional slice_type declaring all slices bidirectional.
#define UTL_H264_SLICE_TYPE_ALL_I                                                                                      \
    (UTL_H264_SLICE_TYPE_I + UTL_H264_SLICE_TYPE_ALL_OFFSET) ///< Intra slice_type declaring all slices intra-coded.

/* ITU-T H.264 clauses 7.4.2.1.1 and 7.4.3, plus Annex A.3: reference-picture and weighting bounds. */
#define UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES (16) ///< Maximum H.264 DPB entries supported here.
#define UTL_H264_MAX_REFERENCE_LIST_ENTRIES         (32) ///< Maximum entries in each H.264 reference list.

/* ITU-T H.264 clauses 7.3.2.1.1.1, 7.4.2.1.1.1, 8.5.6, and 8.5.7: scaling-list dimensions. */
#define UTL_H264_SCALING_LIST_4X4_COUNT         (6)  ///< Number of SPS/PPS 4-by-4 scaling lists.
#define UTL_H264_SCALING_LIST_4X4_ELEMENT_COUNT (16) ///< Elements in one 4-by-4 scaling list.
#define UTL_H264_SCALING_LIST_4X4_TOTAL_ELEMENT_COUNT                                                                  \
    (UTL_H264_SCALING_LIST_4X4_COUNT *                                                                                 \
     UTL_H264_SCALING_LIST_4X4_ELEMENT_COUNT)        ///< Total elements across all 4-by-4 scaling lists.
#define UTL_H264_SCALING_LIST_8X8_COUNT         (2)  ///< Number of supported 8-by-8 scaling lists.
#define UTL_H264_SCALING_LIST_8X8_ELEMENT_COUNT (64) ///< Elements in one 8-by-8 scaling list.
#define UTL_H264_SCALING_LIST_8X8_TOTAL_ELEMENT_COUNT                                                                  \
    (UTL_H264_SCALING_LIST_8X8_COUNT *                                                                                 \
     UTL_H264_SCALING_LIST_8X8_ELEMENT_COUNT) ///< Total elements across all 8-by-8 scaling lists.

/* Implementation-defined public output-buffer capacity; H.264 does not prescribe this size. */
#define UTL_H264_PARAMETER_SET_NAL_BUFFER_CAPACITY                                                                     \
    (2048) ///< Covers PPS scaling matrices, emulation prevention, and Annex B framing.

/* ITU-T H.264 clauses 7.4.2.1.1 and 7.4.2.2, plus Annex E.2.2: syntax-array bounds. */
#define UTL_H264_MAX_SCALING_LIST_COUNT (12) ///< Maximum SPS or PPS scaling-list count.
#define UTL_H264_MAX_SCALING_LIST_ELEMENT_COUNT                                                                        \
    (UTL_H264_SCALING_LIST_8X8_ELEMENT_COUNT) ///< Maximum elements in one scaling list.
#define UTL_H264_MAX_POC_CYCLE_LENGTH  (255)  ///< Maximum offsets in a POC type-one cycle.
#define UTL_H264_MAX_CPB_COUNT         (32)   ///< Maximum coded-picture-buffer entries.
#define UTL_H264_MAX_SLICE_GROUP_COUNT (8)    ///< Maximum flexible-macroblock-ordering slice groups.

/* ITU-T H.264 clauses 7.4.2 through 7.4.3 and Annex E.2: syntax-field bounds. */
#define UTL_H264_MAX_WEIGHTED_BIPRED_IDC       (2)   ///< Maximum legal weighted_bipred_idc.
#define UTL_H264_MAX_SEQUENCE_PARAMETER_SET_ID (31)  ///< Maximum seq_parameter_set_id.
#define UTL_H264_MAX_PICTURE_PARAMETER_SET_ID  (255) ///< Maximum pic_parameter_set_id.
#define UTL_H264_MIN_FRAME_NUM_BIT_COUNT       (4)   ///< Minimum frame_num bit width.
#define UTL_H264_MAX_FRAME_NUM_BIT_COUNT       (16)  ///< Maximum frame_num bit width.
#define UTL_H264_MAX_LOG2_MINUS4                                                                                       \
    (UTL_H264_MAX_FRAME_NUM_BIT_COUNT -                                                                                \
     UTL_H264_MIN_FRAME_NUM_BIT_COUNT) ///< Maximum log2_max_frame_num_minus4 and log2_max_pic_order_cnt_lsb_minus4.
#define UTL_H264_MAX_PIC_ORDER_CNT_TYPE (UTL_H264_PIC_ORDER_CNT_TYPE_2) ///< Maximum legal pic_order_cnt_type.
#define UTL_H264_MAX_REFERENCE_INDEX                                                                                   \
    (UTL_H264_MAX_REFERENCE_LIST_ENTRIES - 1)          ///< Maximum zero-based active reference index.
#define UTL_H264_MAX_MOTION_VECTOR_LENGTH_LOG2 (16)    ///< Maximum VUI motion-vector exponent.
#define UTL_H264_MAX_CABAC_INIT_IDC            (2)     ///< Maximum legal cabac_init_idc.
#define UTL_H264_MAX_WEIGHT_DENOMINATOR_LOG2   (7)     ///< Maximum luma/chroma weight denominator exponent.
#define UTL_H264_MAX_QUANTIZATION_PARAMETER    (51)    ///< Maximum H.264 luma quantization parameter.
#define UTL_H264_QUANTIZATION_PARAMETER_BIAS   (26)    ///< Bias encoded by pic_init_qp_minus26 and pic_init_qs_minus26.
#define UTL_H264_MIN_CHROMA_QP_INDEX_OFFSET    (-12)   ///< Minimum legal chroma QP offset.
#define UTL_H264_MAX_CHROMA_QP_INDEX_OFFSET    (12)    ///< Maximum legal chroma QP offset.
#define UTL_H264_MAX_DISABLE_DEBLOCKING_FILTER_IDC (2) ///< Maximum deblocking-filter mode.
#define UTL_H264_MIN_DEBLOCKING_FILTER_OFFSET_DIV2 (-6) ///< Minimum alpha/beta offset divided by two.
#define UTL_H264_MAX_DEBLOCKING_FILTER_OFFSET_DIV2 (6)  ///< Maximum alpha/beta offset divided by two.

/* ITU-T H.264 clause 7.4.2.1.1 and Annex A.3: level_idc values. */
#define UTL_H264_LEVEL_1_0_IDC (10) ///< level_idc for H.264 level 1.0.
#define UTL_H264_LEVEL_1_1_IDC (11) ///< level_idc for H.264 level 1.1.
#define UTL_H264_LEVEL_1_2_IDC (12) ///< level_idc for H.264 level 1.2.
#define UTL_H264_LEVEL_1_3_IDC (13) ///< level_idc for H.264 level 1.3.
#define UTL_H264_LEVEL_2_0_IDC (20) ///< level_idc for H.264 level 2.0.
#define UTL_H264_LEVEL_2_1_IDC (21) ///< level_idc for H.264 level 2.1.
#define UTL_H264_LEVEL_2_2_IDC (22) ///< level_idc for H.264 level 2.2.
#define UTL_H264_LEVEL_3_0_IDC (30) ///< level_idc for H.264 level 3.0.
#define UTL_H264_LEVEL_3_1_IDC (31) ///< level_idc for H.264 level 3.1.
#define UTL_H264_LEVEL_3_2_IDC (32) ///< level_idc for H.264 level 3.2.
#define UTL_H264_LEVEL_4_0_IDC (40) ///< level_idc for H.264 level 4.0.
#define UTL_H264_LEVEL_4_1_IDC (41) ///< level_idc for H.264 level 4.1.
#define UTL_H264_LEVEL_4_2_IDC (42) ///< level_idc for H.264 level 4.2.
#define UTL_H264_LEVEL_5_0_IDC (50) ///< level_idc for H.264 level 5.0.
#define UTL_H264_LEVEL_5_1_IDC (51) ///< level_idc for H.264 level 5.1.
#define UTL_H264_LEVEL_5_2_IDC (52) ///< level_idc for H.264 level 5.2.
#define UTL_H264_LEVEL_6_0_IDC (60) ///< level_idc for H.264 level 6.0.
#define UTL_H264_LEVEL_6_1_IDC (61) ///< level_idc for H.264 level 6.1.
#define UTL_H264_LEVEL_6_2_IDC (62) ///< level_idc for H.264 level 6.2.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Selects the prediction and random-access role of an encoded picture.
 */
typedef enum Utl_H264_PictureType_e
{
    UTL_H264_PICTURE_TYPE_IDR, ///< Intra-coded instantaneous decoder refresh picture.
    UTL_H264_PICTURE_TYPE_I,   ///< Intra-coded non-IDR picture.
    UTL_H264_PICTURE_TYPE_P,   ///< Forward-predicted picture.
    UTL_H264_PICTURE_TYPE_B    ///< Bidirectionally predicted picture.
} Utl_H264_PictureType_t;

/*************************************************************************************************/
/**
 * \brief Selects frame or individual-field coding for one picture.
 */
typedef enum Utl_H264_FieldMode_e
{
    UTL_H264_FIELD_MODE_FRAME, ///< Encode one complete progressive or paired-field frame.
    UTL_H264_FIELD_MODE_TOP,   ///< Encode only the top field.
    UTL_H264_FIELD_MODE_BOTTOM ///< Encode only the bottom field.
} Utl_H264_FieldMode_t;

/*************************************************************************************************/
/**
 * \brief Selects short- or long-term decoded-picture-buffer retention.
 */
typedef enum Utl_H264_ReferenceType_e
{
    UTL_H264_REFERENCE_TYPE_SHORT_TERM, ///< Reference identified by frame_num/POC progression.
    UTL_H264_REFERENCE_TYPE_LONG_TERM   ///< Reference retained under a stable long-term index.
} Utl_H264_ReferenceType_t;

/*************************************************************************************************/
/**
 * \brief Presence flag and coefficient deltas for a scaling list's bitstream representation.
 */
typedef struct Utl_H264_ScalingList_s
{
    uint8_t presentFlag; ///< Signals that this scaling list is explicitly present.
    int32_t deltaScale[UTL_H264_MAX_SCALING_LIST_ELEMENT_COUNT]; ///< Signed delta_scale values in scan order.
} Utl_H264_ScalingList_t;

/*************************************************************************************************/
/**
 * \brief Buffer schedules and timing-field widths describing the decoder's buffering model.
 */
typedef struct Utl_H264_HrdParams_s
{
    uint32_t cpbCntMinus1;                               ///< Number of coded-picture-buffer schedules minus one.
    uint8_t  bitRateScale;                               ///< Scale exponent applied to bitRateValueMinus1 entries.
    uint8_t  cpbSizeScale;                               ///< Scale exponent applied to cpbSizeValueMinus1 entries.
    uint32_t bitRateValueMinus1[UTL_H264_MAX_CPB_COUNT]; ///< Coded bit-rate values minus one for each CPB schedule.
    uint32_t cpbSizeValueMinus1[UTL_H264_MAX_CPB_COUNT]; ///< Coded CPB-size values minus one for each CPB schedule.
    uint8_t  cbrFlag[UTL_H264_MAX_CPB_COUNT];            ///< Constant-bit-rate flag for each CPB schedule.
    uint8_t  initialCpbRemovalDelayLengthMinus1;         ///< Initial CPB removal-delay field width minus one.
    uint8_t  cpbRemovalDelayLengthMinus1;                ///< CPB removal-delay field width minus one.
    uint8_t  dpbOutputDelayLengthMinus1;                 ///< DPB output-delay field width minus one.
    uint8_t  timeOffsetLength;                           ///< Signed time_offset field width in bits.
} Utl_H264_HrdParams_t;

/*************************************************************************************************/
/**
 * \brief Sequence-wide decoding parameters: profile, picture dimensions, ordering, and timing.
 */
typedef struct Utl_H264_SpsParams_s
{
    uint8_t                profileIdc;              ///< profile_idc selecting the decoding tool set.
    uint8_t                constraintSetFlags;      ///< Packed constraint_set0_flag through constraint_set5_flag masks.
    uint8_t                levelIdc;                ///< level_idc derived from picture size and macroblock rate.
    uint32_t               seqParameterSetId;       ///< seq_parameter_set_id in the range 0..31.
    uint32_t               chromaFormatIdc;         ///< Chroma sampling format in the range 0..3.
    uint8_t                separateColourPlaneFlag; ///< Enables separate colour-plane coding for 4:4:4 video.
    uint32_t               bitDepthLumaMinus8;      ///< Luma bit depth minus eight.
    uint32_t               bitDepthChromaMinus8;    ///< Chroma bit depth minus eight.
    uint8_t                qpprimeYZeroTransformBypassFlag; ///< Enables lossless transform bypass at QP prime zero.
    uint8_t                seqScalingMatrixPresentFlag;     ///< Signals sequence-level scaling matrices.
    Utl_H264_ScalingList_t scalingLists[UTL_H264_MAX_SCALING_LIST_COUNT]; ///< Sequence scaling lists in syntax order.
    uint32_t               log2MaxFrameNumMinus4;                         ///< frame_num bit width minus four.
    uint32_t               picOrderCntType;             ///< Picture-order-count derivation mode in the range 0..2.
    uint32_t               log2MaxPicOrderCntLsbMinus4; ///< POC-LSB bit width minus four for POC type zero.
    uint8_t                deltaPicOrderAlwaysZeroFlag; ///< Forces delta picture-order values to zero for POC type one.
    int32_t                offsetForNonRefPic;          ///< POC type-one offset applied to non-reference pictures.
    int32_t  offsetForTopToBottomField;      ///< POC type-one offset from a top field to its paired bottom field.
    uint32_t numRefFramesInPicOrderCntCycle; ///< Number of reference-picture offsets in one POC type-one cycle.
    int32_t
        offsetForRefFrame[UTL_H264_MAX_POC_CYCLE_LENGTH]; ///< Per-reference-picture offsets for a POC type-one cycle.
    uint32_t             maxNumRefFrames;                 ///< Maximum decoded-picture-buffer reference count.
    uint8_t              gapsInFrameNumValueAllowedFlag;  ///< Permits gaps in frame_num when nonzero.
    uint32_t             picWidthInMbsMinus1;             ///< Coded macroblock columns minus one.
    uint32_t             picHeightInMapUnitsMinus1;       ///< Coded map-unit rows minus one.
    uint8_t              frameMbsOnlyFlag;                ///< Selects frame-only macroblock coding when nonzero.
    uint8_t              mbAdaptiveFrameFieldFlag;        ///< Enables macroblock-adaptive frame/field coding.
    uint8_t              direct8x8InferenceFlag;          ///< Enables 8-by-8 direct-motion inference.
    uint8_t              frameCroppingFlag;               ///< Signals nonzero coded-frame crop offsets.
    uint32_t             frameCropLeftOffset;             ///< Left crop in chroma-dependent crop units.
    uint32_t             frameCropRightOffset;            ///< Right crop in chroma-dependent crop units.
    uint32_t             frameCropTopOffset;              ///< Top crop in frame/field-dependent crop units.
    uint32_t             frameCropBottomOffset;           ///< Bottom crop in frame/field-dependent crop units.
    uint8_t              vuiParametersPresentFlag;        ///< Signals the VUI syntax block when nonzero.
    uint8_t              aspectRatioInfoPresentFlag;      ///< Signals aspect_ratio_idc and optional extended SAR.
    uint8_t              aspectRatioIdc;                  ///< Table E-1 aspect-ratio identifier.
    uint16_t             sarWidth;                        ///< Extended-SAR numerator when aspectRatioIdc is 255.
    uint16_t             sarHeight;                       ///< Extended-SAR denominator when aspectRatioIdc is 255.
    uint8_t              overscanInfoPresentFlag;         ///< Signals VUI overscan suitability information.
    uint8_t              overscanAppropriateFlag; ///< Indicates that cropped output is suitable for overscan display.
    uint8_t              videoSignalTypePresentFlag; ///< Signals video format, range, and optional colour description.
    uint8_t              videoFormat;                ///< Source video-format code in the range 0..7.
    uint8_t              videoFullRangeFlag;         ///< Selects full-range sample values when nonzero.
    uint8_t              colourDescriptionPresentFlag;   ///< Signals colour primaries, transfer, and matrix fields.
    uint8_t              colourPrimaries;                ///< Colour-primary characteristics code.
    uint8_t              transferCharacteristics;        ///< Opto-electronic transfer-characteristics code.
    uint8_t              matrixCoefficients;             ///< Matrix-coefficient code used to derive luma and chroma.
    uint8_t              chromaLocInfoPresentFlag;       ///< Signals chroma sample-location information.
    uint32_t             chromaSampleLocTypeTopField;    ///< Chroma sample location for frames or top fields.
    uint32_t             chromaSampleLocTypeBottomField; ///< Chroma sample location for bottom fields.
    uint8_t              timingInfoPresentFlag;          ///< Signals num_units_in_tick and time_scale.
    uint32_t             numUnitsInTick;                 ///< VUI clock tick numerator.
    uint32_t             timeScale;                      ///< VUI clock frequency scale.
    uint8_t              fixedFrameRateFlag;             ///< Signals a fixed frame rate for the timing values.
    uint8_t              nalHrdParametersPresentFlag;    ///< Signals NAL HRD parameters in the VUI.
    Utl_H264_HrdParams_t nalHrdParameters;               ///< NAL hypothetical-reference-decoder parameters.
    uint8_t              vclHrdParametersPresentFlag;    ///< Signals VCL HRD parameters in the VUI.
    Utl_H264_HrdParams_t vclHrdParameters;               ///< VCL hypothetical-reference-decoder parameters.
    uint8_t              lowDelayHrdFlag;          ///< Signals low-delay HRD operation when HRD parameters are present.
    uint8_t              picStructPresentFlag;     ///< Signals pic_struct values in picture-timing SEI messages.
    uint8_t              bitstreamRestrictionFlag; ///< Signals motion-vector and buffering restrictions.
    uint8_t              motionVectorsOverPicBoundariesFlag; ///< Permits motion vectors crossing picture boundaries.
    uint32_t             maxBytesPerPicDenom;                ///< Maximum bytes-per-picture denominator exponent.
    uint32_t             maxBitsPerMbDenom;                  ///< Maximum bits-per-macroblock denominator exponent.
    uint32_t             log2MaxMvLengthHorizontal;          ///< Base-two horizontal motion-vector magnitude limit.
    uint32_t             log2MaxMvLengthVertical;            ///< Base-two vertical motion-vector magnitude limit.
    uint32_t             maxNumReorderFrames;                ///< Maximum decoded frames held for output reordering.
    uint32_t             maxDecFrameBuffering;               ///< Maximum decoded-picture-buffer occupancy.
} Utl_H264_SpsParams_t;

/*************************************************************************************************/
/**
 * \brief Picture coding defaults and slice-group layout associated with a sequence parameter set.
 */
typedef struct Utl_H264_PpsParams_s
{
    uint32_t picParameterSetId;                     ///< pic_parameter_set_id in the range 0..255.
    uint32_t seqParameterSetId;                     ///< Referenced seq_parameter_set_id in the range 0..31.
    uint8_t  entropyCodingModeFlag;                 ///< Selects CABAC when nonzero and CAVLC otherwise.
    uint8_t  bottomFieldPicOrderInFramePresentFlag; ///< Enables per-picture bottom-field POC syntax.
    uint32_t numSliceGroupsMinus1;                  ///< Flexible-macroblock-ordering slice groups minus one.
    uint32_t sliceGroupMapType;                     ///< Flexible-macroblock-ordering map type in the range 0..6.
    uint32_t runLengthMinus1[UTL_H264_MAX_SLICE_GROUP_COUNT]; ///< Run lengths minus one for interleaved slice groups.
    uint32_t topLeft[UTL_H264_MAX_SLICE_GROUP_COUNT - 1]; ///< Top-left map-unit indices for rectangular slice groups.
    uint32_t bottomRight[UTL_H264_MAX_SLICE_GROUP_COUNT -
                         1];                  ///< Bottom-right map-unit indices for rectangular slice groups.
    uint8_t  sliceGroupChangeDirectionFlag;   ///< Growth direction for dynamic slice-group maps.
    uint32_t sliceGroupChangeRateMinus1;      ///< Map-unit growth rate minus one for dynamic slice groups.
    uint32_t picSizeInMapUnitsMinus1;         ///< Number of entries minus one in an explicit slice-group map.
    const uint8_t*         pSliceGroupId;     ///< Explicit slice-group identifiers for map type six.
    uint32_t               sliceGroupIdCount; ///< Number of readable entries in pSliceGroupId.
    uint32_t               numRefIdxL0DefaultActiveMinus1; ///< Default active list-0 reference count minus one.
    uint32_t               numRefIdxL1DefaultActiveMinus1; ///< Default active list-1 reference count minus one.
    uint8_t                weightedPredFlag;               ///< Enables explicit weighted prediction for P slices.
    uint8_t                weightedBipredIdc;              ///< Weighted prediction mode for B slices in the range 0..2.
    int32_t                picInitQpMinus26;               ///< Initial luma QP relative to 26.
    int32_t                picInitQsMinus26;               ///< Initial SP/SI quantizer relative to 26.
    int32_t                chromaQpIndexOffset;            ///< First chroma QP offset in the range -12..12.
    uint8_t                deblockingFilterControlPresentFlag; ///< Enables slice-level deblocking control syntax.
    uint8_t                constrainedIntraPredFlag;           ///< Restricts intra prediction to intra-coded neighbors.
    uint8_t                redundantPicCntPresentFlag;         ///< Enables redundant_pic_cnt in slice headers.
    uint8_t                transform8x8ModeFlag;               ///< Enables High-profile 8-by-8 transforms.
    uint8_t                picScalingMatrixPresentFlag;        ///< Signals PPS scaling matrices when nonzero.
    uint32_t               chromaFormatIdc; ///< Referenced SPS chroma format used to size scaling-list syntax.
    Utl_H264_ScalingList_t scalingLists[UTL_H264_MAX_SCALING_LIST_COUNT]; ///< Picture scaling lists in syntax order.
    int32_t                secondChromaQpIndexOffset; ///< Second chroma QP offset in the range -12..12.
} Utl_H264_PpsParams_t;

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
 * \brief Derives and initializes the supported SPS syntax from stream configuration.
 *
 * \par Details:
 *  Unsupported syntax branches are initialized to FALSE or zero. Level, frame-number and POC
 *  widths, coded macroblock dimensions, cropping, aspect-ratio syntax, timing syntax, and
 *  motion-vector exponents are derived from the corresponding source values.
 *
 * \par Requirements:
 *  - 001 This function shall clear params and initialize every supported SPS field from the stream configuration and its derived syntax values.
 *  - 002 This function shall derive the bit-depth offsets, level, frame and POC widths, coded dimensions, crop offsets, SAR, timing, and motion-vector exponents from the source values.
 *  - 003 This function shall leave unsupported syntax fields FALSE or zero.
 *  - 004 This function shall fail an assert if params is NULL or the configuration is outside the supported ranges or is internally inconsistent.
 *
 * \param[out] params                               SPS model initialized from zero
 * \param[in]  profileIdc                           profile_idc
 * \param[in]  constraintSetFlags                   Packed constraint_set flags
 * \param[in]  seqParameterSetId                    seq_parameter_set_id
 * \param[in]  chromaFormatIdc                      chroma_format_idc
 * \param[in]  bitDepthLuma                         Luma component bit depth in the range 8..14
 * \param[in]  bitDepthChroma                       Chroma component bit depth in the range 8..14
 * \param[in]  qpprimeYZeroTransformBypassFlag      qpprime_y_zero_transform_bypass_flag
 * \param[in]  width                                Visible width in luma samples
 * \param[in]  height                               Visible height in luma samples
 * \param[in]  frameRateNumerator                   Frame-rate numerator
 * \param[in]  frameRateDenominator                 Frame-rate denominator
 * \param[in]  maximumFrameNumber                   Maximum frame_num value to represent
 * \param[in]  maximumPicOrderCntLsb                Maximum pic_order_cnt_lsb value to represent
 * \param[in]  maxNumRefFrames                      max_num_ref_frames
 * \param[in]  gapsInFrameNumValueAllowedFlag       gaps_in_frame_num_value_allowed_flag
 * \param[in]  interlaced                           TRUE for field-capable coding
 * \param[in]  mbAdaptiveFrameFieldFlag             mb_adaptive_frame_field_flag
 * \param[in]  direct8x8InferenceFlag               direct_8x8_inference_flag
 * \param[in]  vuiParametersPresentFlag             vui_parameters_present_flag
 * \param[in]  sampleAspectRatioWidth               Sample-aspect-ratio numerator, or zero
 * \param[in]  sampleAspectRatioHeight              Sample-aspect-ratio denominator, or zero
 * \param[in]  timingInfoPresentFlag                timing_info_present_flag
 * \param[in]  fixedFrameRateFlag                   fixed_frame_rate_flag
 * \param[in]  bitstreamRestrictionFlag             bitstream_restriction_flag
 * \param[in]  motionVectorsOverPicBoundariesFlag   motion_vectors_over_pic_boundaries_flag
 * \param[in]  maxBytesPerPicDenom                  max_bytes_per_pic_denom
 * \param[in]  maxBitsPerMbDenom                    max_bits_per_mb_denom
 * \param[in]  maxMotionVectorLengthHorizontal      Maximum horizontal motion-vector magnitude
 * \param[in]  maxMotionVectorLengthVertical        Maximum vertical motion-vector magnitude
 * \param[in]  maxNumReorderFrames                  max_num_reorder_frames
 * \param[in]  maxDecFrameBuffering                 max_dec_frame_buffering
 *
 * \retval None  No result value.
 */
void Utl_H264_InitializeSps(Utl_H264_SpsParams_t* params,
                            uint8_t               profileIdc,
                            uint8_t               constraintSetFlags,
                            uint32_t              seqParameterSetId,
                            uint32_t              chromaFormatIdc,
                            uint32_t              bitDepthLuma,
                            uint32_t              bitDepthChroma,
                            bool_t                qpprimeYZeroTransformBypassFlag,
                            uint32_t              width,
                            uint32_t              height,
                            uint32_t              frameRateNumerator,
                            uint32_t              frameRateDenominator,
                            uint32_t              maximumFrameNumber,
                            uint32_t              maximumPicOrderCntLsb,
                            uint32_t              maxNumRefFrames,
                            bool_t                gapsInFrameNumValueAllowedFlag,
                            bool_t                interlaced,
                            bool_t                mbAdaptiveFrameFieldFlag,
                            bool_t                direct8x8InferenceFlag,
                            bool_t                vuiParametersPresentFlag,
                            uint32_t              sampleAspectRatioWidth,
                            uint32_t              sampleAspectRatioHeight,
                            bool_t                timingInfoPresentFlag,
                            bool_t                fixedFrameRateFlag,
                            bool_t                bitstreamRestrictionFlag,
                            bool_t                motionVectorsOverPicBoundariesFlag,
                            uint32_t              maxBytesPerPicDenom,
                            uint32_t              maxBitsPerMbDenom,
                            uint32_t              maxMotionVectorLengthHorizontal,
                            uint32_t              maxMotionVectorLengthVertical,
                            uint32_t              maxNumReorderFrames,
                            uint32_t              maxDecFrameBuffering);

/*************************************************************************************************/
/**
 * \brief Initializes a present scaling list from positive, raster-order coefficients.
 *
 * \par Requirements:
 *  - 001 This function shall clear params and initialize one explicitly present scaling list.
 *  - 002 This function shall map raster-order coefficients to H.264 frame scan order and derive delta_scale values in the range -128..127.
 *  - 003 This function shall fail an assert if either pointer is NULL, elementCount is not 16 or 64, or any input coefficient is zero.
 *
 * \param[out] params          Receives scan-order delta_scale values; unused entries are cleared
 * \param[in]  coefficients    Raster-order coefficients, each in the range 1..255
 * \param[in]  elementCount    Number of coefficients: 16 for 4-by-4 or 64 for 8-by-8
 *
 * \retval    None  No result value.
 */
void Utl_H264_InitializeScalingList(Utl_H264_ScalingList_t* params, const uint8_t* coefficients, uint32_t elementCount);

/*************************************************************************************************/
/**
 * \brief Initializes PPS coding defaults with slice groups and scaling matrices disabled.
 *
 * \par Requirements:
 *  - 001 This function shall clear params and initialize every supported PPS field from the supplied configuration values.
 *  - 002 This function shall derive the active-reference-count offsets and initial-quantizer offsets, and leave slice-group and scaling-matrix fields zero, FALSE, or NULL.
 *  - 003 This function shall fail an assert if params is NULL or a configuration value is outside the supported range.
 *
 * \param[out] params                               PPS model initialized from zero
 * \param[in]  picParameterSetId                    pic_parameter_set_id
 * \param[in]  seqParameterSetId                    seq_parameter_set_id
 * \param[in]  entropyCodingMode                    Nonzero to enable CABAC
 * \param[in]  bottomFieldPicOrderInFramePresent    Nonzero to signal bottom-field POC syntax
 * \param[in]  numRefIdxL0DefaultActive             Default active list-0 reference count in the range 1..32
 * \param[in]  numRefIdxL1DefaultActive             Default active list-1 reference count in the range 1..32
 * \param[in]  weightedPrediction                   Nonzero to enable weighted P prediction
 * \param[in]  weightedBipredIdc                    Weighted B prediction mode in the range 0..2
 * \param[in]  picInitQp                            Initial luma quantization parameter in the range 0..51
 * \param[in]  picInitQs                            Initial SP/SI quantization parameter in the range 0..51
 * \param[in]  chromaQpIndexOffset                  First chroma QP offset in the range -12..12
 * \param[in]  deblockingFilterControlPresent       Nonzero to signal deblocking controls
 * \param[in]  constrainedIntraPrediction           Nonzero to restrict intra prediction
 * \param[in]  redundantPictures                    Nonzero to signal redundant picture counts
 * \param[in]  transform8x8Mode                     Nonzero to enable 8-by-8 transforms
 * \param[in]  chromaFormatIdc                      Chroma format of the referenced SPS
 * \param[in]  secondChromaQpIndexOffset            Second chroma QP offset in the range -12..12
 *
 * \retval    None  No result value.
 */
void Utl_H264_InitializePps(Utl_H264_PpsParams_t* params,
                            uint32_t              picParameterSetId,
                            uint32_t              seqParameterSetId,
                            bool_t                entropyCodingMode,
                            bool_t                bottomFieldPicOrderInFramePresent,
                            uint32_t              numRefIdxL0DefaultActive,
                            uint32_t              numRefIdxL1DefaultActive,
                            bool_t                weightedPrediction,
                            uint8_t               weightedBipredIdc,
                            uint32_t              picInitQp,
                            uint32_t              picInitQs,
                            int32_t               chromaQpIndexOffset,
                            bool_t                deblockingFilterControlPresent,
                            bool_t                constrainedIntraPrediction,
                            bool_t                redundantPictures,
                            bool_t                transform8x8Mode,
                            uint32_t              chromaFormatIdc,
                            int32_t               secondChromaQpIndexOffset);

/*************************************************************************************************/
/**
 * \brief Selects the lowest H.264 level satisfying coded-frame size and macroblock-rate limits.
 *
 * \par Requirements:
 *  - 001 This function shall return the lowest supported level_idc satisfying the rounded frame's MaxFS, MaxMBPS, and dimension limits.
 *  - 002 This function shall fail an assert if an input is zero, dimension rounding overflows, or no supported level can represent the stream.
 *
 * \param[in] width                 Nonzero coded width in pixels
 * \param[in] height                Nonzero coded height in pixels
 * \param[in] frameRateNumerator    Nonzero frame-rate numerator
 * \param[in] frameRateDenominator  Nonzero frame-rate denominator
 *
 * \retval result H.264 level_idc from level 1.0 through 6.2; exceeding level 6.2 is fatal.
 */
uint8_t Utl_H264_DeriveLevelIdc(uint32_t width,
                                uint32_t height,
                                uint32_t frameRateNumerator,
                                uint32_t frameRateDenominator);

/*************************************************************************************************/
/**
 * \brief Checks Annex A.2 frame, slice, and picture coding-tool restrictions for a candidate profile.
 *
 * \par Details:
 *  Covers the single-view profiles in A.2.1 through A.2.11, including applicable constraint_set
 *  restrictions. Unsupported profile_idc values return FALSE. This checks the supplied coding
 *  tools; SPS chroma/bit-depth limits, NAL-unit restrictions, and Annex A.3 level limits are separate.
 *  Scaling presence covers either SPS or PPS matrices. For profiles without PPS extension syntax,
 *  the second chroma offset must equal the first, as inferred by clause 7.4.2.2.
 *
 * \param[in] profileIdc Candidate profile_idc
 * \param[in] constraintSetFlags Packed constraint_set flags, with reserved bits zero
 * \param[in] sliceType slice_type in the range 0..9
 * \param[in] frameMbsOnlyFlag frame_mbs_only_flag
 * \param[in] entropyCodingModeFlag entropy_coding_mode_flag
 * \param[in] transform8x8ModeFlag transform_8x8_mode_flag
 * \param[in] scalingMatrixPresentFlag SPS or PPS scaling matrices are present
 * \param[in] weightedPredFlag weighted_pred_flag
 * \param[in] weightedBipredIdc weighted_bipred_idc
 * \param[in] redundantPicCntPresentFlag redundant_pic_cnt_present_flag
 * \param[in] chromaQpIndexOffset chroma_qp_index_offset
 * \param[in] secondChromaQpIndexOffset Explicit or inferred second_chroma_qp_index_offset
 *
 * \retval TRUE The supplied coding tools satisfy the profile restrictions.
 * \retval FALSE A supplied coding tool is incompatible or the profile is unsupported.
 */
bool_t Utl_H264_IsProfileCodingCompatible(uint8_t profileIdc,
                                          uint8_t constraintSetFlags,
                                          uint8_t sliceType,
                                          bool_t  frameMbsOnlyFlag,
                                          bool_t  entropyCodingModeFlag,
                                          bool_t  transform8x8ModeFlag,
                                          bool_t  scalingMatrixPresentFlag,
                                          bool_t  weightedPredFlag,
                                          uint8_t weightedBipredIdc,
                                          bool_t  redundantPicCntPresentFlag,
                                          int32_t chromaQpIndexOffset,
                                          int32_t secondChromaQpIndexOffset);

/*************************************************************************************************/
/**
 * \brief Derives the smallest log2 syntax value minus four that represents a maximum value.
 *
 * \par Requirements:
 *  - 001 This function shall return the smallest result in the range 0..12 for which 2^(result + 4) exceeds maximumValue, capped at 12.
 *
 * \param[in] maximumValue         Maximum frame_num or POC-LSB value
 *
 * \retval result Bit width minus four in the H.264 range 0..12.
 */
uint32_t Utl_H264_DeriveLog2Minus4(uint32_t maximumValue);

/*************************************************************************************************/
/**
 * \brief Derives the frame-number range from the configured IDR period.
 *
 * \param[in] intraIdrPeriod IDR period in the range 0..65535; zero selects the full 16-bit range
 *
 * \retval result Maximum frame number used to derive the frame_num syntax width.
 */
uint32_t Utl_H264_DeriveMaximumFrameNumber(uint32_t intraIdrPeriod);

/*************************************************************************************************/
/**
 * \brief Derives the wrap mask for a frame_num or pic_order_cnt_lsb syntax value.
 *
 * \param[in] maximumValue Maximum value used to derive the 4..16-bit syntax width
 *
 * \retval result Mask containing one bit for each bit of the derived syntax width.
 */
uint32_t Utl_H264_DeriveValueMask(uint32_t maximumValue);

/*************************************************************************************************/
/**
 * \brief Converts a frame index to its top-field picture-order count.
 *
 * \param[in] frameIndex Frame index to convert
 *
 * \retval result Even top-field picture-order count; the corresponding bottom-field count is one greater.
 */
uint32_t Utl_H264_DeriveFieldOrderCount(uint32_t frameIndex);

/*************************************************************************************************/
/**
 * \brief Derives the maximum POC-LSB value needed for a frame-number range.
 *
 * \param[in] maximumFrameNumber Maximum frame_num value to represent
 *
 * \retval result Twice maximumFrameNumber, saturated to the 16-bit POC-LSB limit.
 */
uint32_t Utl_H264_DeriveMaximumPicOrderCntLsb(uint32_t maximumFrameNumber);

/*************************************************************************************************/
/**
 * \brief Derives the ceiling base-two exponent of a motion-vector magnitude limit.
 *
 * \par Requirements:
 *  - 001 This function shall return the smallest exponent for which 2^exponent is not less than maximumLength.
 *  - 002 This function shall fail an assert if maximumLength is zero or requires an exponent greater than UTL_H264_MAX_MOTION_VECTOR_LENGTH_LOG2.
 *
 * \param[in] maximumLength        Nonzero motion-vector magnitude
 *
 * \retval result Exponent in the range 0..16.
 */
uint8_t Utl_H264_DeriveMotionVectorLengthLog2(uint32_t maximumLength);

/*************************************************************************************************/
/**
 * \brief Derives the macroblock and padded dimensions of an H.264 coded frame.
 *
 * Interlaced frames are padded to an even number of macroblock rows.
 *
 * \par Requirements:
 *  - 001 This function shall return macroblock counts and padded dimensions after rounding width to one macroblock and height to one progressive or two interlaced macroblock rows.
 *  - 002 This function shall return padded dimensions no smaller than the inputs and an even heightInMbs when interlaced is TRUE.
 *  - 003 This function shall fail an assert if a dimension is zero, interlaced is invalid, an output pointer is NULL, or arithmetic overflows.
 *
 * \param[in]  width          Nonzero visible width in luma samples
 * \param[in]  height         Nonzero visible height in luma samples
 * \param[in]  interlaced     TRUE for field-capable coding
 * \param[out] widthInMbs     Coded width in macroblocks
 * \param[out] heightInMbs    Coded frame height in macroblocks
 * \param[out] paddedWidth    Coded width in luma samples
 * \param[out] paddedHeight   Coded frame height in luma samples
 *
 * \retval None  No result value.
 */
void Utl_H264_DeriveCodedDimensions(uint32_t  width,
                                    uint32_t  height,
                                    bool_t    interlaced,
                                    uint32_t* widthInMbs,
                                    uint32_t* heightInMbs,
                                    uint32_t* paddedWidth,
                                    uint32_t* paddedHeight);

/*************************************************************************************************/
/**
 * \brief Derives horizontal and vertical frame-crop units for a chroma format.
 *
 * \par Requirements:
 *  - 001 This function shall return base crop units of (1, 1) for monochrome or 4:4:4, (2, 2) for 4:2:0, and (2, 1) for 4:2:2.
 *  - 002 This function shall double the vertical crop unit when interlaced is TRUE.
 *  - 003 This function shall fail an assert if chromaFormatIdc is unsupported, interlaced is invalid, or an output pointer is NULL.
 *
 * \param[in]  chromaFormatIdc Chroma sampling format in the range 0..3
 * \param[in]  interlaced      TRUE to account for field-coded vertical crop units
 * \param[out] cropUnitX       Horizontal crop unit in luma samples
 * \param[out] cropUnitY       Vertical crop unit in luma samples
 *
 * \retval None  No result value.
 */
void Utl_H264_DeriveCropUnits(uint32_t chromaFormatIdc, bool_t interlaced, uint32_t* cropUnitX, uint32_t* cropUnitY);

/*************************************************************************************************/
/**
 * \brief Derives right and bottom crop offsets for a visible image within its coded frame.
 *
 * \param[in]  width            Nonzero visible width in luma samples
 * \param[in]  height           Nonzero visible height in luma samples
 * \param[in]  paddedWidth      Coded width, no smaller than width
 * \param[in]  paddedHeight     Coded height, no smaller than height
 * \param[in]  chromaFormatIdc  Chroma sampling format in the range 0..3
 * \param[in]  interlaced       TRUE for field-capable coding
 * \param[out] cropRightOffset  Right crop offset in chroma-dependent crop units
 * \param[out] cropBottomOffset Bottom crop offset in chroma-dependent crop units
 *
 * \retval result TRUE when cropping is needed. Fails an assert if padding is not crop-unit aligned.
 */
bool_t Utl_H264_DeriveFrameCropOffsets(uint32_t  width,
                                       uint32_t  height,
                                       uint32_t  paddedWidth,
                                       uint32_t  paddedHeight,
                                       uint32_t  chromaFormatIdc,
                                       bool_t    interlaced,
                                       uint32_t* cropRightOffset,
                                       uint32_t* cropBottomOffset);

/*************************************************************************************************/
/**
 * \brief Derives VUI time_scale from the frame-rate numerator.
 *
 * \param[in] frameRateNumerator Nonzero numerator no larger than UINT32_MAX / 2
 *
 * \retval result Twice the numerator; num_units_in_tick uses the frame-rate denominator unchanged.
 */
uint32_t Utl_H264_DeriveTimeScale(uint32_t frameRateNumerator);

/*************************************************************************************************/
/**
 * \brief Derives both VUI motion-vector exponents from the configured restriction.
 *
 * \param[in]  restricted       TRUE when the bitstream restriction is enabled
 * \param[in]  maximumHorizontal Nonzero horizontal magnitude when restricted, otherwise zero
 * \param[in]  maximumVertical   Nonzero vertical magnitude when restricted, otherwise zero
 * \param[out] log2Horizontal    Horizontal exponent in the range 0..16, or zero when unrestricted
 * \param[out] log2Vertical      Vertical exponent in the range 0..16, or zero when unrestricted
 *
 * \retval None  No result value.
 */
void Utl_H264_DeriveMotionVectorLimits(bool_t    restricted,
                                       uint32_t  maximumHorizontal,
                                       uint32_t  maximumVertical,
                                       uint32_t* log2Horizontal,
                                       uint32_t* log2Vertical);

/*************************************************************************************************/
/**
 * \brief Maps a sample-aspect-ratio fraction to Table E-1 or extended-SAR syntax.
 *
 * Both dimensions must be zero to omit aspect-ratio signaling or both must be nonzero. Common
 * ratios use a predefined aspect_ratio_idc; all others use Extended_SAR with 16-bit dimensions.
 *
 * \par Requirements:
 *  - 001 This function shall map 0:0 to unspecified SAR, a known reduced fraction to its Table E-1 identifier, and any other reduced fraction to Extended_SAR.
 *  - 002 This function shall return zero SAR dimensions for unspecified or Table E-1 SAR and the reduced dimensions for Extended_SAR.
 *  - 003 This function shall fail an assert if an output pointer is NULL, only one dimension is zero, or an extended-SAR dimension exceeds UINT16_MAX.
 *
 * \param[in]  width              Sample-aspect-ratio numerator
 * \param[in]  height             Sample-aspect-ratio denominator
 * \param[out] aspectRatioIdc     Receives the Table E-1 identifier
 * \param[out] sarWidth           Receives extended-SAR width, or zero for a predefined ratio
 * \param[out] sarHeight          Receives extended-SAR height, or zero for a predefined ratio
 *
 * \retval    None  No result value.
 */
void Utl_H264_DeriveAspectRatio(uint32_t  width,
                                uint32_t  height,
                                uint8_t*  aspectRatioIdc,
                                uint32_t* sarWidth,
                                uint32_t* sarHeight);

/*************************************************************************************************/
/**
 * \brief Maps a public picture type to the corresponding H.264 slice_type value.
 *
 * \par Requirements:
 *  - 001 This function shall map IDR and I pictures to I slices, P pictures to P slices, and B pictures to B slices.
 *  - 002 This function shall fail an assert if pictureType is not a defined picture type.
 *
 * \param[in] pictureType        Picture role to map
 *
 * \retval result UTL_H264_SLICE_TYPE_I, UTL_H264_SLICE_TYPE_P, or UTL_H264_SLICE_TYPE_B.
 */
uint8_t Utl_H264_DeriveSliceType(Utl_H264_PictureType_t pictureType);

/*************************************************************************************************/
/**
 * \brief Derives default active reference counts for I, P, and B slices.
 *
 * \param[in]  sliceType           I, P, or B slice_type syntax value
 * \param[in]  decodedPictureCount Number of available decoded pictures, at most 16
 * \param[out] list0Count          Available count for P/B slices, or one when unused or empty
 * \param[out] list1Count          Available count for B slices, or one when unused or empty
 *
 * \retval None  No result value.
 */
void Utl_H264_DeriveActiveReferenceCounts(uint8_t   sliceType,
                                          uint32_t  decodedPictureCount,
                                          uint32_t* list0Count,
                                          uint32_t* list1Count);

/*************************************************************************************************/
/**
 * \brief Derives reference indices ordered by distance before or after the current frame.
 *
 * \par Details:
 *  At least one decoded picture must lie in the requested direction. This function asserts
 *  if no reference is selected, including when pictureCount is zero.
 *
 * \param[in]  frameIndex        Current absolute frame index
 * \param[in]  frameIndices      Decoded picture indices
 * \param[in]  pictureCount      Nonzero number of decoded pictures, at most 16
 * \param[in]  futureReferences  TRUE for later frames, FALSE for earlier frames
 * \param[out] referenceIndices  Storage for pictureCount indices; equal distances retain input order
 *
 * \retval result Nonzero number of selected indices. Frames equal to frameIndex are omitted.
 */
uint64_t Utl_H264_DeriveReferenceListIndices(uint32_t        frameIndex,
                                             const uint32_t* frameIndices,
                                             uint64_t        pictureCount,
                                             bool_t          futureReferences,
                                             uint64_t*       referenceIndices);

/*************************************************************************************************/
/**
 * \brief Inserts an SPS Annex B NAL packet into a caller-provided buffer.
 *
 * \par Requirements:
 *  - 001 This function shall serialize params as an SPS Annex B NAL unit with RBSP trailing bits and emulation-prevention bytes, then return the written byte count.
 *  - 002 This function shall emit the required Annex B zero_byte and three-byte start-code prefix followed by a zero forbidden bit, high nal_ref_idc, and SPS nal_unit_type.
 *  - 003 This function shall fail an assert if a pointer is NULL or params contains unsupported SPS syntax.
 *  - 004 This function shall return zero when the destination capacity is insufficient.
 *
 * \param[out] bufferToPlaceSpsIn Destination receiving the complete NAL packet
 * \param[in]  sizeOfSpsBuffer    Writable byte capacity of the destination
 * \param[in]  params             Validated SPS syntax model
 *
 * \retval result Number of bytes written, or zero when the destination is too small.
 */
uint64_t Utl_H264_WriteSpsNalUnit(byte_t*                     bufferToPlaceSpsIn,
                                  uint64_t                    sizeOfSpsBuffer,
                                  const Utl_H264_SpsParams_t* params);

/*************************************************************************************************/
/**
 * \brief Inserts a PPS Annex B NAL packet into a caller-provided buffer.
 *
 * \par Requirements:
 *  - 001 This function shall serialize params as a PPS Annex B NAL unit with RBSP trailing bits and emulation-prevention bytes, then return the written byte count.
 *  - 002 This function shall emit the required Annex B zero_byte and three-byte start-code prefix followed by a zero forbidden bit, high nal_ref_idc, and PPS nal_unit_type.
 *  - 003 This function shall fail an assert if a pointer is NULL or params contains unsupported PPS syntax.
 *  - 004 This function shall return zero when the destination capacity is insufficient.
 *
 * \param[out] bufferToPlacePpsIn Destination receiving the complete NAL packet
 * \param[in]  sizeOfPpsBuffer    Writable byte capacity of the destination
 * \param[in]  params             Validated PPS syntax model
 *
 * \retval result Number of bytes written, or zero when the destination is too small.
 */
uint64_t Utl_H264_WritePpsNalUnit(byte_t*                     bufferToPlacePpsIn,
                                  uint64_t                    sizeOfPpsBuffer,
                                  const Utl_H264_PpsParams_t* params);

/*************************************************************************************************/
/**
 * \brief Inserts one SEI Annex B NAL packet into a caller-provided buffer.
 *
 * \par Details:
 *  This function always emits Annex B zero_byte before the three-byte start-code prefix. zero_byte
 *  is required when the SEI is the first NAL unit of an access unit and permitted otherwise.
 *  Emulation-prevention bytes are inserted into the RBSP. A NULL payload is valid only when
 *  \p payloadByteCount is zero.
 *
 * \par Requirements:
 *  - 001 This function shall serialize payloadType, payloadByteCount, and the payload as an SEI Annex B NAL unit, then return the written byte count.
 *  - 002 This function shall encode extended type and size values and emit zero_byte, a three-byte start-code prefix, a non-reference SEI header, RBSP trailing bits, and emulation-prevention bytes.
 *  - 003 This function shall fail an assert if a pointer/count combination is invalid.
 *  - 004 This function shall return zero when the destination capacity is insufficient.
 *
 * \param[out] bufferToPlaceSeiIn Destination receiving the complete NAL packet
 * \param[in]  sizeOfSeiBuffer    Writable byte capacity of the destination
 * \param[in]  payloadType        H.264 SEI payload_type value
 * \param[in]  pPayloadBytes      Payload bytes, or NULL for an empty payload
 * \param[in]  payloadByteCount   Number of payload bytes
 *
 * \retval result Number of bytes written, or zero when the destination is too small.
 */
uint64_t Utl_H264_WriteSeiNalUnit(byte_t*       bufferToPlaceSeiIn,
                                  uint64_t      sizeOfSeiBuffer,
                                  uint32_t      payloadType,
                                  const byte_t* pPayloadBytes,
                                  uint64_t      payloadByteCount);

#endif /* UTL_H264_H */
