/*=================================================================================================
** 1. References
**===============================================================================================*/

/** See utl_h264.h for the specification revision, provenance and retained encoder subset. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_h264.h"

#include "os/os_assert.h"
#include "os/os_types.h"
#include "utl/utl_mpeg4.h"
#include "crt/crt_memory.h"
#include "utl/utl_expgolomb.h"

#include <stddef.h>
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/* ITU-T H.264 clauses 7.3.2.11 and 7.4.2.11: RBSP trailing bits. */
#define RBSP_ALIGNMENT_BYTE           (0x80)
#define BYTE_BIT_COUNT                (8)    ///< Bits per serialized byte.
#define NAL_START_CODE_BIT_COUNT      (32)   ///< Four-byte Annex B zero/start sequence.
#define NAL_MAX_UNIT_TYPE             (31)   ///< Five-bit nal_unit_type maximum.
#define NAL_REFERENCE_SHIFT           (5)    ///< nal_ref_idc position in the NAL header byte.
#define ESCAPE_ZERO_COUNT             (2)    ///< Zero run before a potentially emulated start code.
#define LEVEL_LIMIT_COUNT             (17)   ///< Retained frame-size/rate level candidates.
#define RESERVED_CONSTRAINT_BITS_MASK (0x03) ///< reserved_zero_2bits in the SPS constraint byte.
#define SAR_NUMERATOR_SHIFT           (32)   ///< Packs a reduced ratio for exact case selection.

/* ITU-T H.264 clauses 7.3.2.1.1.1 and 7.4.2.1.1.1: scaling_list() coding. */
#define SCALING_LIST_INITIAL_SCALE (8)    ///< Initial lastScale and nextScale in scaling_list().
#define SCALING_LIST_SCALE_MODULUS (256)  ///< Modulus used to reconstruct each scaling coefficient.
#define SCALING_LIST_MIN_DELTA     (-128) ///< Minimum permitted delta_scale value.
#define SCALING_LIST_MAX_DELTA     (127)  ///< Maximum permitted delta_scale value.

/* ITU-T H.264 clause 7.3.1, Table 7-1, Annex B, and Annex D: NAL and SEI byte-stream values. */
#define NAL_STREAM_ESCAPE_CODE     (0x03)
#define NAL_REF_IDC_NONE           (0)    ///< Non-reference NAL priority value.
#define NAL_REF_IDC_HIGH           (3)    ///< Highest NAL reference priority value.
#define NAL_UNIT_TYPE_SEI          (6)    ///< Supplemental enhancement information NAL type.
#define NAL_UNIT_TYPE_SPS          (7)    ///< Sequence parameter set NAL type.
#define NAL_UNIT_TYPE_PPS          (8)    ///< Picture parameter set NAL type.
#define SEI_PAYLOAD_EXTENSION_BYTE (0xff) ///< Continuation byte for SEI type and size values.

/* ITU-T H.264 clauses 6.2 and 7.4.2.1.1: coded-frame geometry and cropping. */
#define MACROBLOCK_WIDTH                  (16) ///< Luma samples per macroblock row.
#define MACROBLOCK_HEIGHT                 (16) ///< Luma sample rows per macroblock.
#define CHROMA_420_CROP_UNIT_X            (2)  ///< Horizontal frame-crop unit for 4:2:0.
#define CHROMA_420_CROP_UNIT_Y            (2)  ///< Progressive vertical frame-crop unit for 4:2:0.
#define CHROMA_422_CROP_UNIT_X            (2)  ///< Horizontal frame-crop unit for 4:2:2.
#define CHROMA_422_CROP_UNIT_Y            (1)  ///< Progressive vertical frame-crop unit for 4:2:2.
#define CHROMA_444_CROP_UNIT_X            (1)  ///< Horizontal frame-crop unit for 4:4:4.
#define CHROMA_444_CROP_UNIT_Y            (1)  ///< Progressive vertical frame-crop unit for 4:4:4.
#define INTERLACED_CROP_UNIT_Y_MULTIPLIER (2)  ///< Vertical crop-unit multiplier for field coding.

/* ITU-T H.264 Annex E.2.1: VUI timing relationship for progressive frame streams. */
#define TIME_SCALE_MULTIPLIER (2) ///< Converts the frame-rate numerator to time_scale.

/* Implementation-defined SEI serialization capacity; H.264 does not prescribe this size. */
#define SEI_RBSP_BUFFER_CAPACITY (4096) ///< Maximum generated SEI RBSP bytes.

/* ITU-T H.264 Annex E.1.1: fixed-width VUI syntax fields. */
#define SAR_DIMENSION_BIT_COUNT    (16) ///< Width of sar_width and sar_height.
#define VIDEO_FORMAT_BIT_COUNT     (3)  ///< Width of video_format.
#define VUI_TIMING_VALUE_BIT_COUNT (32) ///< Width of num_units_in_tick and time_scale.

/* ITU-T H.264 clauses 7.3.2.2 and 7.4.2.2: fixed-width and biased PPS fields. */
#define WEIGHTED_BIPRED_IDC_BIT_COUNT (2) ///< Width of weighted_bipred_idc.

/* ITU-T H.264 clause 8.2.1: two field-order-count positions per frame. */
#define FIELD_ORDER_COUNT_MULTIPLIER (2)

/* ITU-T H.264 Annex A.3, Table A-1: MaxFS and MaxMBPS level limits. */
#define LEVEL_MAX_FRAME_DIMENSION_SQUARED_MULTIPLIER (8)        ///< H.264 MaxFS-derived dimension factor.
#define LEVEL_1_0_MAX_FRAME_MBS                      (99)       ///< Level 1.0 MaxFS in macroblocks.
#define LEVEL_1_0_MAX_MBS_PER_SECOND                 (1485)     ///< Level 1.0 MaxMBPS.
#define LEVEL_1_1_MAX_FRAME_MBS                      (396)      ///< Level 1.1 MaxFS in macroblocks.
#define LEVEL_1_1_MAX_MBS_PER_SECOND                 (3000)     ///< Level 1.1 MaxMBPS.
#define LEVEL_1_2_MAX_FRAME_MBS                      (396)      ///< Level 1.2 MaxFS in macroblocks.
#define LEVEL_1_2_MAX_MBS_PER_SECOND                 (6000)     ///< Level 1.2 MaxMBPS.
#define LEVEL_1_3_MAX_FRAME_MBS                      (396)      ///< Level 1.3 MaxFS in macroblocks.
#define LEVEL_1_3_MAX_MBS_PER_SECOND                 (11880)    ///< Level 1.3 MaxMBPS.
#define LEVEL_2_1_MAX_FRAME_MBS                      (792)      ///< Level 2.1 MaxFS in macroblocks.
#define LEVEL_2_1_MAX_MBS_PER_SECOND                 (19800)    ///< Level 2.1 MaxMBPS.
#define LEVEL_2_2_MAX_FRAME_MBS                      (1620)     ///< Level 2.2 MaxFS in macroblocks.
#define LEVEL_2_2_MAX_MBS_PER_SECOND                 (20250)    ///< Level 2.2 MaxMBPS.
#define LEVEL_3_0_MAX_FRAME_MBS                      (1620)     ///< Level 3.0 MaxFS in macroblocks.
#define LEVEL_3_0_MAX_MBS_PER_SECOND                 (40500)    ///< Level 3.0 MaxMBPS.
#define LEVEL_3_1_MAX_FRAME_MBS                      (3600)     ///< Level 3.1 MaxFS in macroblocks.
#define LEVEL_3_1_MAX_MBS_PER_SECOND                 (108000)   ///< Level 3.1 MaxMBPS.
#define LEVEL_3_2_MAX_FRAME_MBS                      (5120)     ///< Level 3.2 MaxFS in macroblocks.
#define LEVEL_3_2_MAX_MBS_PER_SECOND                 (216000)   ///< Level 3.2 MaxMBPS.
#define LEVEL_4_0_MAX_FRAME_MBS                      (8192)     ///< Level 4.0 MaxFS in macroblocks.
#define LEVEL_4_0_MAX_MBS_PER_SECOND                 (245760)   ///< Level 4.0 MaxMBPS.
#define LEVEL_4_2_MAX_FRAME_MBS                      (8704)     ///< Level 4.2 MaxFS in macroblocks.
#define LEVEL_4_2_MAX_MBS_PER_SECOND                 (522240)   ///< Level 4.2 MaxMBPS.
#define LEVEL_5_0_MAX_FRAME_MBS                      (22080)    ///< Level 5.0 MaxFS in macroblocks.
#define LEVEL_5_0_MAX_MBS_PER_SECOND                 (589824)   ///< Level 5.0 MaxMBPS.
#define LEVEL_5_1_MAX_FRAME_MBS                      (36864)    ///< Level 5.1 MaxFS in macroblocks.
#define LEVEL_5_1_MAX_MBS_PER_SECOND                 (983040)   ///< Level 5.1 MaxMBPS.
#define LEVEL_5_2_MAX_FRAME_MBS                      (36864)    ///< Level 5.2 MaxFS in macroblocks.
#define LEVEL_5_2_MAX_MBS_PER_SECOND                 (2073600)  ///< Level 5.2 MaxMBPS.
#define LEVEL_6_0_MAX_FRAME_MBS                      (139264)   ///< Level 6.0 MaxFS in macroblocks.
#define LEVEL_6_0_MAX_MBS_PER_SECOND                 (4177920)  ///< Level 6.0 MaxMBPS.
#define LEVEL_6_1_MAX_FRAME_MBS                      (139264)   ///< Level 6.1 MaxFS in macroblocks.
#define LEVEL_6_1_MAX_MBS_PER_SECOND                 (8355840)  ///< Level 6.1 MaxMBPS.
#define LEVEL_6_2_MAX_FRAME_MBS                      (139264)   ///< Level 6.2 MaxFS in macroblocks.
#define LEVEL_6_2_MAX_MBS_PER_SECOND                 (16711680) ///< Level 6.2 MaxMBPS.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Associates an H.264 level identifier with its frame-size and macroblock-rate limits.
 */
typedef struct H264LevelIdc_s
{
    uint32_t maximumFrameMacroblocks;     ///< MaxFS in macroblocks.
    uint32_t maximumMacroblocksPerSecond; ///< MaxMBPS in macroblocks per second.
    uint8_t  levelIdc;                    ///< H.264 level_idc.
} H264LevelIdc_t;

/*************************************************************************************************/
/**
 * \brief NAL header fields serialized explicitly in network bit order.
 */
typedef struct H264NalHeader_s
{
    uint8_t nalUnitType; ///< Five-bit nal_unit_type.
    uint8_t nalRefIdc;   ///< Two-bit nal_ref_idc.
} H264NalHeader_t;

/*************************************************************************************************/
/**
 * \brief One stable RBSP and the header byte of its enclosing Annex B NAL unit.
 */
typedef struct NalUnit_s
{
    Utl_Mpeg4_Span_t syntax;   ///< Complete accessible RBSP storage.
    uint32_t         bitCount; ///< Syntax bits before rbsp_trailing_bits().
    byte_t           header;   ///< Serialized forbidden-zero, reference and type fields.
} NalUnit_t;

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
 * \brief Determines whether an SPS profile uses high-profile syntax fields.
 *
 * \param[in] profileIdc        H.264 profile_idc value
 *
 * \retval TRUE  The profile requires chroma-format and bit-depth fields.
 * \retval FALSE Baseline/main-style SPS syntax applies.
 */
static bool_t IsHighProfile(uint8_t profileIdc);

/*************************************************************************************************/
/**
 * \brief Computes the greatest common divisor of two unsigned integers.
 *
 * \param[in] left         First Euclidean-algorithm operand
 * \param[in] right        Second Euclidean-algorithm operand
 *
 * \retval result Greatest common divisor; returns left when right is zero.
 */
static uint32_t GreatestCommonDivisor(uint32_t left, uint32_t right);

/*************************************************************************************************/
/**
 * \brief Appends an SEI payload type or size using 0xff extension bytes.
 *
 * \par Requirements:
 *  - 001 The writer shall be initialized and positioned at a byte boundary.
 *  - 002 Capacity shall be checked before iterating the extension-byte count.
 *  - 003 Failure shall preserve the writer position and leave its valid flag FALSE.
 *
 * \param[in,out] writer  Byte-aligned RBSP writer
 * \param[in]     value   Payload type or payload size to encode
 *
 * \retval None  No result value.
 */
static void WriteSEIExtendedValue(Utl_Mpeg4_BitWriter_t* writer, uint64_t value);

/*************************************************************************************************/
/**
 * \brief Serializes sequence parameters into a raw byte sequence payload.
 *
 * \param[out] rbsp             Destination receiving SPS syntax bits
 * \param[in]  rbspCapacity     Writable byte capacity of rbsp
 * \param[in]  params           Validated SPS syntax model
 *
 * \retval result Number of SPS syntax bits written before RBSP trailing bits.
 */
static uint32_t WriteSPS(byte_t* rbsp, uint64_t rbspCapacity, const Utl_H264_SpsParams_t* params);

/*************************************************************************************************/
/**
 * \brief Serializes picture parameters into a raw byte sequence payload.
 *
 * \param[out] rbsp             Destination receiving PPS syntax bits
 * \param[in]  rbspCapacity     Writable byte capacity of rbsp
 * \param[in]  params           Validated PPS syntax model
 *
 * \retval result Number of PPS syntax bits written before RBSP trailing bits.
 */
static uint32_t WritePPS(byte_t* rbsp, uint64_t rbspCapacity, const Utl_H264_PpsParams_t* params);

/*************************************************************************************************/
/**
 * \brief Serializes a supplemental message into a raw byte sequence payload.
 *
 * \param[out] rbsp                 Destination receiving SEI syntax bytes
 * \param[in]  rbspCapacity         Writable byte capacity of rbsp
 * \param[in]  payloadType          H.264 SEI payload_type value
 * \param[in]  pPayloadBytes        Payload bytes, or NULL when payloadByteCount is zero
 * \param[in]  payloadByteCount     Number of payload bytes
 *
 * \retval result Number of SEI syntax bits written before RBSP trailing bits.
 */
static uint32_t WriteSEI(byte_t*       rbsp,
                         uint64_t      rbspCapacity,
                         uint32_t      payloadType,
                         const byte_t* pPayloadBytes,
                         uint64_t      payloadByteCount);

/*************************************************************************************************/
/**
 * \brief Finalizes an RBSP and inserts it as an Annex B NAL unit with emulation prevention.
 *
 * \param[out]    bufferToPlacePacketIn             Destination receiving the complete Annex B NAL unit
 * \param[in]     sizeOfPacketBuffer         Writable byte capacity of bufferToPlacePacketIn
 * \param[in]     nalHeader          Header prepended to the encoded RBSP
 * \param[in]     rbsp               RBSP syntax bits before rbsp_trailing_bits
 * \param[in]     rbspCapacity       Writable byte capacity of rbsp
 * \param[in]     rbspBitOffset      Number of syntax bits already present in rbsp
 *
 * \retval result Number of Annex B NAL bytes written to bufferToPlacePacketIn.
 */
static uint64_t WriteNalUnit(byte_t*         bufferToPlacePacketIn,
                             uint64_t        sizeOfPacketBuffer,
                             H264NalHeader_t nalHeader,
                             const byte_t*   rbsp,
                             uint64_t        rbspCapacity,
                             uint32_t        rbspBitOffset);

/*************************************************************************************************/
/**
 * \brief Counts or writes the complete Annex B NAL bytes through the shared bit writer.
 *
 * \par Requirements:
 *  - 001 The writer and stable model shall satisfy WriteNalUnit's preflighted storage contract.
 *  - 002 Trailing bits shall be escaped with the RBSP, including at a full-byte boundary.
 *  - 003 A counter shall inspect the same bounded syntax as an output writer without storing bytes.
 *
 * \param[in,out] writer   Initialized byte-aligned writer or counter
 * \param[in]     unit     Stable header and bounded RBSP model
 *
 * \retval None  No result value.
 */
static void WriteNalBytes(Utl_Mpeg4_BitWriter_t* writer, const NalUnit_t* unit);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static bool_t IsHighProfile(uint8_t profileIdc)
{
    bool_t high_profile;

    high_profile = FALSE;
    switch (profileIdc)
    {
    case UTL_H264_PROFILE_CAVLC_444_INTRA:
    case UTL_H264_PROFILE_SCALABLE_BASELINE:
    case UTL_H264_PROFILE_SCALABLE_HIGH:
    case UTL_H264_PROFILE_HIGH:
    case UTL_H264_PROFILE_HIGH_10:
    case UTL_H264_PROFILE_MULTIVIEW_HIGH:
    case UTL_H264_PROFILE_HIGH_422:
    case UTL_H264_PROFILE_STEREO_HIGH:
    case UTL_H264_PROFILE_MFC_HIGH:
    case UTL_H264_PROFILE_MFC_DEPTH_HIGH:
    case UTL_H264_PROFILE_MULTIVIEW_DEPTH_HIGH:
    case UTL_H264_PROFILE_ENHANCED_MULTIVIEW_DEPTH_HIGH:
    case UTL_H264_PROFILE_HIGH_444_PREDICTIVE:
        high_profile = TRUE;
        break;

    default:
        break;
    }
    return high_profile;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint32_t GreatestCommonDivisor(uint32_t left, uint32_t right)
{
    uint32_t remainder;

    for (; 0 != right;)
    {
        remainder = left % right;
        left      = right;
        right     = remainder;
    }
    return left;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void WriteSEIExtendedValue(Utl_Mpeg4_BitWriter_t* writer, uint64_t value)
{
    Utl_Mpeg4_BitField_t field;

    uint64_t             available_byte_count;
    uint64_t             extension_byte_count;
    uint64_t             extension_byte_index;
    bool_t               has_capacity;

    LOGASSERT(NULL != writer);
    LOGASSERT(0 == writer->position % BYTE_BIT_COUNT);
    LOGASSERT(writer->position <= (uint64_t)writer->capacity * BYTE_BIT_COUNT);
    available_byte_count = writer->capacity - (writer->position / BYTE_BIT_COUNT);
    extension_byte_count = value / SEI_PAYLOAD_EXTENSION_BYTE;
    has_capacity         = (extension_byte_count < available_byte_count);
    writer->valid        = writer->valid && has_capacity;
    if (writer->valid)
    {
        for (extension_byte_index = 0; extension_byte_index < extension_byte_count; ++extension_byte_index)
        {
            field.value    = SEI_PAYLOAD_EXTENSION_BYTE;
            field.bitCount = BYTE_BIT_COUNT;
            Utl_Mpeg4_WriteBits(writer, field);
        }
        field.value    = value % SEI_PAYLOAD_EXTENSION_BYTE;
        field.bitCount = BYTE_BIT_COUNT;
        Utl_Mpeg4_WriteBits(writer, field);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The zero run covers RBSP bytes only. An escape is inserted immediately before the next byte
 *  when its value is at most three. The shared reader handles both complete and partial bytes.
 */
static void WriteNalBytes(Utl_Mpeg4_BitWriter_t* writer, const NalUnit_t* unit)
{
    Utl_Mpeg4_BitReader_t reader;
    uint32_t              whole_byte_count;
    uint32_t              partial_bit_count;
    Utl_Mpeg4_BitField_t  field;
    uint32_t              zero_count;
    uint32_t              byte_index;
    byte_t                value;
    uint64_t              partial;
    bool_t                follows_zeros;
    bool_t                resembles_start_code;
    bool_t                needs_escape;

    Utl_Mpeg4_InitializeBitReader(&reader, unit->syntax);
    whole_byte_count  = unit->bitCount / BYTE_BIT_COUNT;
    partial_bit_count = unit->bitCount % BYTE_BIT_COUNT;
    field.value       = 1;
    field.bitCount    = NAL_START_CODE_BIT_COUNT;
    Utl_Mpeg4_WriteBits(writer, field);
    field.value    = unit->header;
    field.bitCount = BYTE_BIT_COUNT;
    Utl_Mpeg4_WriteBits(writer, field);
    zero_count = 0;
    for (byte_index = 0; byte_index <= whole_byte_count; ++byte_index)
    {
        if (byte_index < whole_byte_count)
        {
            value = (byte_t)Utl_Mpeg4_ReadBits(&reader, BYTE_BIT_COUNT);
        }
        else
        {
            value = (byte_t)(RBSP_ALIGNMENT_BYTE >> partial_bit_count);
            if (0 < partial_bit_count)
            {
                partial = Utl_Mpeg4_ReadBits(&reader, partial_bit_count);
                value   = (byte_t)((partial << (BYTE_BIT_COUNT - partial_bit_count)) | value);
            }
        }
        follows_zeros        = (ESCAPE_ZERO_COUNT == zero_count);
        resembles_start_code = (NAL_STREAM_ESCAPE_CODE >= value);
        needs_escape         = follows_zeros && resembles_start_code;
        if (needs_escape)
        {
            field.value = NAL_STREAM_ESCAPE_CODE;
            Utl_Mpeg4_WriteBits(writer, field);
            zero_count = 0;
        }
        field.value = value;
        Utl_Mpeg4_WriteBits(writer, field);
        if (0 == value)
        {
            ++zero_count;
        }
        else
        {
            zero_count = 0;
        }
    }
    LOGASSERT(reader.valid);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Places the complete RBSP bytes after an Annex B zero_byte, the three-byte
 *  start_code_prefix_one_3bytes, and the supplied NAL header. This module always emits zero_byte:
 *  it is required for SPS, PPS, and the first NAL unit of an access unit, and is permitted
 *  otherwise. Both passes construct the final byte from any remaining RBSP syntax bits,
 *  rbsp_stop_one_bit and alignment zeros. Emulation prevention applies to that byte as well.
 *  Only a following byte in 0..3 triggers an escape after two zeros. The first pass determines
 *  the complete required capacity; the second emits explicit bytes without native packed storage.
 *
 *  The returned byte count includes zero_byte, the start-code prefix, NAL header, escaped RBSP,
 *  and inserted emulation-prevention bytes. If the complete NAL unit does not fit, zero is returned.
 */
static uint64_t WriteNalUnit(byte_t*         bufferToPlacePacketIn,
                             uint64_t        sizeOfPacketBuffer,
                             H264NalHeader_t nalHeader,
                             const byte_t*   rbsp,
                             uint64_t        rbspCapacity,
                             uint32_t        rbspBitOffset)
{
    NalUnit_t             unit;
    Utl_Mpeg4_BitWriter_t writer;
    uint64_t              written;
    bool_t                has_syntax;
    uint32_t              required;
    bool_t                has_capacity;
    bool_t                can_write;

    LOGASSERT(NULL != bufferToPlacePacketIn);
    LOGASSERT(NULL != rbsp);
    LOGASSERT(SEI_RBSP_BUFFER_CAPACITY >= rbspCapacity);
    LOGASSERT((uint64_t)rbspBitOffset <= rbspCapacity * BYTE_BIT_COUNT);
    LOGASSERT(NAL_REF_IDC_HIGH >= nalHeader.nalRefIdc);
    LOGASSERT(0 < nalHeader.nalUnitType);
    LOGASSERT(NAL_MAX_UNIT_TYPE >= nalHeader.nalUnitType);
    unit.syntax.data      = rbsp;
    unit.syntax.byteCount = (uint32_t)rbspCapacity;
    unit.bitCount         = rbspBitOffset;
    unit.header           = (byte_t)((nalHeader.nalRefIdc << NAL_REFERENCE_SHIFT) | nalHeader.nalUnitType);
    written               = 0;
    has_syntax            = (0 < rbspBitOffset);
    if (has_syntax)
    {
        Utl_Mpeg4_InitializeBitCounter(&writer);
        WriteNalBytes(&writer, &unit);
        required     = Utl_Mpeg4_GetBitWriterByteCount(&writer);
        has_capacity = (required <= sizeOfPacketBuffer);
        can_write    = writer.valid && has_capacity;
        if (can_write)
        {
            Utl_Mpeg4_InitializeBitWriter(&writer, bufferToPlacePacketIn, required);
            WriteNalBytes(&writer, &unit);
            LOGASSERT(writer.valid);
            written = Utl_Mpeg4_GetBitWriterByteCount(&writer);
        }
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Clears rbsp and writes seq_parameter_set_rbsp() starting at bit zero.
 *
 *  Conditional syntax:
 *  - High-profile fields are written when IsHighProfile(params->profileIdc) is TRUE.
 *  - mbAdaptiveFrameFieldFlag is written when frameMbsOnlyFlag is zero.
 *  - The four crop offsets are written when frameCroppingFlag is nonzero.
 *  - VUI syntax is written when vuiParametersPresentFlag is nonzero. Extended SAR, timing, and
 *    bitstream-restriction fields depend on their corresponding presence flags.
 *
 *  Supported subset:
 *  - picOrderCntType must be UTL_H264_PIC_ORDER_CNT_TYPE_0.
 *  - separateColourPlaneFlag, seqScalingMatrixPresentFlag, overscanInfoPresentFlag,
 *    videoSignalTypePresentFlag, chromaLocInfoPresentFlag, nalHrdParametersPresentFlag,
 *    vclHrdParametersPresentFlag, and picStructPresentFlag must be zero.
 *
 *  Returns the RBSP bit count before rbsp_trailing_bits(); WriteNalUnit adds trailing bits and NAL
 *  framing.
 */
static uint32_t WriteSPS(byte_t* rbsp, uint64_t rbspCapacity, const Utl_H264_SpsParams_t* params)
{
    Utl_Mpeg4_BitField_t  field;

    Utl_Mpeg4_BitWriter_t writer;
    uint32_t              rbsp_bit_offset;

    LOGASSERT(NULL != params);
    LOGASSERT(NULL != rbsp);
    LOGASSERT(0 != rbspCapacity);
    LOGASSERT(UTL_H264_PIC_ORDER_CNT_TYPE_0 == params->picOrderCntType);
    LOGASSERT(0 == params->separateColourPlaneFlag);
    LOGASSERT(0 == params->seqScalingMatrixPresentFlag);
    LOGASSERT(0 == params->overscanInfoPresentFlag);
    LOGASSERT(0 == params->videoSignalTypePresentFlag);
    LOGASSERT(0 == params->chromaLocInfoPresentFlag);
    LOGASSERT(0 == params->nalHrdParametersPresentFlag);
    LOGASSERT(0 == params->vclHrdParametersPresentFlag);
    LOGASSERT(0 == params->picStructPresentFlag);

    LOGASSERT(UINT32_MAX >= rbspCapacity);
    Utl_Mpeg4_InitializeBitWriter(&writer, rbsp, (uint32_t)rbspCapacity);
    CRT_Memory_Set(rbsp, 0, rbspCapacity);
    field.value    = params->profileIdc;
    field.bitCount = BYTE_BIT_COUNT;
    Utl_Mpeg4_WriteBits(&writer, field);
    field.value    = params->constraintSetFlags;
    field.bitCount = BYTE_BIT_COUNT;
    Utl_Mpeg4_WriteBits(&writer, field);
    field.value    = params->levelIdc;
    field.bitCount = BYTE_BIT_COUNT;
    Utl_Mpeg4_WriteBits(&writer, field);
    Utl_ExpGolomb_WriteUnsigned(&writer, params->seqParameterSetId);
    if (IsHighProfile(params->profileIdc))
    {
        Utl_ExpGolomb_WriteUnsigned(&writer, params->chromaFormatIdc);
        if (UTL_H264_CHROMA_FORMAT_444 == params->chromaFormatIdc)
        {
            field.value    = FALSE;
            field.bitCount = 1;
            Utl_Mpeg4_WriteBits(&writer, field);
        }
        Utl_ExpGolomb_WriteUnsigned(&writer, params->bitDepthLumaMinus8);
        Utl_ExpGolomb_WriteUnsigned(&writer, params->bitDepthChromaMinus8);
        field.value    = params->qpprimeYZeroTransformBypassFlag;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        field.value    = FALSE;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
    }
    Utl_ExpGolomb_WriteUnsigned(&writer, params->log2MaxFrameNumMinus4);
    Utl_ExpGolomb_WriteUnsigned(&writer, params->picOrderCntType);
    Utl_ExpGolomb_WriteUnsigned(&writer, params->log2MaxPicOrderCntLsbMinus4);
    Utl_ExpGolomb_WriteUnsigned(&writer, params->maxNumRefFrames);
    field.value    = params->gapsInFrameNumValueAllowedFlag;
    field.bitCount = 1;
    Utl_Mpeg4_WriteBits(&writer, field);
    Utl_ExpGolomb_WriteUnsigned(&writer, params->picWidthInMbsMinus1);
    Utl_ExpGolomb_WriteUnsigned(&writer, params->picHeightInMapUnitsMinus1);
    field.value    = params->frameMbsOnlyFlag;
    field.bitCount = 1;
    Utl_Mpeg4_WriteBits(&writer, field);
    if (!params->frameMbsOnlyFlag)
    {
        field.value    = params->mbAdaptiveFrameFieldFlag;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
    }
    field.value    = params->direct8x8InferenceFlag;
    field.bitCount = 1;
    Utl_Mpeg4_WriteBits(&writer, field);
    field.value    = params->frameCroppingFlag;
    field.bitCount = 1;
    Utl_Mpeg4_WriteBits(&writer, field);
    if (params->frameCroppingFlag)
    {
        Utl_ExpGolomb_WriteUnsigned(&writer, params->frameCropLeftOffset);
        Utl_ExpGolomb_WriteUnsigned(&writer, params->frameCropRightOffset);
        Utl_ExpGolomb_WriteUnsigned(&writer, params->frameCropTopOffset);
        Utl_ExpGolomb_WriteUnsigned(&writer, params->frameCropBottomOffset);
    }
    field.value    = params->vuiParametersPresentFlag;
    field.bitCount = 1;
    Utl_Mpeg4_WriteBits(&writer, field);
    if (params->vuiParametersPresentFlag)
    {
        field.value    = params->aspectRatioInfoPresentFlag;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        if (params->aspectRatioInfoPresentFlag)
        {
            field.value    = params->aspectRatioIdc;
            field.bitCount = BYTE_BIT_COUNT;
            Utl_Mpeg4_WriteBits(&writer, field);
            if (UTL_H264_ASPECT_RATIO_IDC_EXTENDED_SAR == params->aspectRatioIdc)
            {
                field.value    = params->sarWidth;
                field.bitCount = SAR_DIMENSION_BIT_COUNT;
                Utl_Mpeg4_WriteBits(&writer, field);
                field.value    = params->sarHeight;
                field.bitCount = SAR_DIMENSION_BIT_COUNT;
                Utl_Mpeg4_WriteBits(&writer, field);
            }
        }
        field.value    = params->overscanInfoPresentFlag;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        if (params->overscanInfoPresentFlag)
        {
            field.value    = params->overscanAppropriateFlag;
            field.bitCount = 1;
            Utl_Mpeg4_WriteBits(&writer, field);
        }
        field.value    = params->videoSignalTypePresentFlag;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        if (params->videoSignalTypePresentFlag)
        {
            field.value    = params->videoFormat;
            field.bitCount = VIDEO_FORMAT_BIT_COUNT;
            Utl_Mpeg4_WriteBits(&writer, field);
            field.value    = params->videoFullRangeFlag;
            field.bitCount = 1;
            Utl_Mpeg4_WriteBits(&writer, field);
            field.value    = params->colourDescriptionPresentFlag;
            field.bitCount = 1;
            Utl_Mpeg4_WriteBits(&writer, field);
            if (params->colourDescriptionPresentFlag)
            {
                field.value    = params->colourPrimaries;
                field.bitCount = BYTE_BIT_COUNT;
                Utl_Mpeg4_WriteBits(&writer, field);
                field.value    = params->transferCharacteristics;
                field.bitCount = BYTE_BIT_COUNT;
                Utl_Mpeg4_WriteBits(&writer, field);
                field.value    = params->matrixCoefficients;
                field.bitCount = BYTE_BIT_COUNT;
                Utl_Mpeg4_WriteBits(&writer, field);
            }
        }
        field.value    = params->chromaLocInfoPresentFlag;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        if (params->chromaLocInfoPresentFlag)
        {
            Utl_ExpGolomb_WriteUnsigned(&writer, params->chromaSampleLocTypeTopField);
            Utl_ExpGolomb_WriteUnsigned(&writer, params->chromaSampleLocTypeBottomField);
        }
        field.value    = params->timingInfoPresentFlag;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        if (params->timingInfoPresentFlag)
        {
            field.value    = params->numUnitsInTick;
            field.bitCount = VUI_TIMING_VALUE_BIT_COUNT;
            Utl_Mpeg4_WriteBits(&writer, field);
            field.value    = params->timeScale;
            field.bitCount = VUI_TIMING_VALUE_BIT_COUNT;
            Utl_Mpeg4_WriteBits(&writer, field);
            field.value    = params->fixedFrameRateFlag;
            field.bitCount = 1;
            Utl_Mpeg4_WriteBits(&writer, field);
        }
        field.value    = FALSE;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        field.value    = FALSE;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        field.value    = FALSE;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        field.value    = params->bitstreamRestrictionFlag;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        if (params->bitstreamRestrictionFlag)
        {
            field.value    = params->motionVectorsOverPicBoundariesFlag;
            field.bitCount = 1;
            Utl_Mpeg4_WriteBits(&writer, field);
            Utl_ExpGolomb_WriteUnsigned(&writer, params->maxBytesPerPicDenom);
            Utl_ExpGolomb_WriteUnsigned(&writer, params->maxBitsPerMbDenom);
            Utl_ExpGolomb_WriteUnsigned(&writer, params->log2MaxMvLengthHorizontal);
            Utl_ExpGolomb_WriteUnsigned(&writer, params->log2MaxMvLengthVertical);
            Utl_ExpGolomb_WriteUnsigned(&writer, params->maxNumReorderFrames);
            Utl_ExpGolomb_WriteUnsigned(&writer, params->maxDecFrameBuffering);
        }
    }
    rbsp_bit_offset = 0;
    if (writer.valid)
    {
        LOGASSERT(UINT32_MAX >= writer.position);
        rbsp_bit_offset = (uint32_t)writer.position;
    }
    return rbsp_bit_offset;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Clears rbsp and writes pic_parameter_set_rbsp() starting at bit zero. Base syntax through
 *  redundantPicCntPresentFlag is always written.
 *
 *  PPS extension syntax is written when transform8x8ModeFlag or picScalingMatrixPresentFlag is nonzero, or
 *  secondChromaQpIndexOffset differs from chromaQpIndexOffset. The extension contains
 *  transform8x8ModeFlag, optional scaling lists, and secondChromaQpIndexOffset. Six 4-by-4 lists are
 *  signaled whenever scaling matrices are present. With 8-by-8 transforms, two additional lists
 *  are signaled for chroma formats 0..2, or six for chroma format 3.
 *
 *  Supported subset:
 *  - numSliceGroupsMinus1 must be zero.
 *  - chromaFormatIdc must be in the range 0..3.
 *  - weightedBipredIdc must not exceed UTL_H264_MAX_WEIGHTED_BIPRED_IDC.
 *
 *  Returns the RBSP bit count before rbsp_trailing_bits(); WriteNalUnit adds trailing bits and NAL
 *  framing.
 */
static uint32_t WritePPS(byte_t* rbsp, uint64_t rbspCapacity, const Utl_H264_PpsParams_t* params)
{
    Utl_Mpeg4_BitField_t          field;

    Utl_Mpeg4_BitWriter_t         writer;
    uint32_t                      rbsp_bit_offset;
    uint32_t                      scaling_list_count;
    uint32_t                      scaling_list_index;
    uint32_t                      element_count;
    uint32_t                      element_index;
    int32_t                       next_scale;
    int32_t                       delta_scale;
    const Utl_H264_ScalingList_t* scaling_list;

    LOGASSERT(NULL != params);
    LOGASSERT(NULL != rbsp);
    LOGASSERT(0 != rbspCapacity);
    LOGASSERT(0 == params->numSliceGroupsMinus1);
    LOGASSERT(UTL_H264_MAX_WEIGHTED_BIPRED_IDC >= params->weightedBipredIdc);
    LOGASSERT(UTL_H264_CHROMA_FORMAT_444 >= params->chromaFormatIdc);

    LOGASSERT(UINT32_MAX >= rbspCapacity);
    Utl_Mpeg4_InitializeBitWriter(&writer, rbsp, (uint32_t)rbspCapacity);
    CRT_Memory_Set(rbsp, 0, rbspCapacity);
    Utl_ExpGolomb_WriteUnsigned(&writer, params->picParameterSetId);
    Utl_ExpGolomb_WriteUnsigned(&writer, params->seqParameterSetId);
    field.value    = params->entropyCodingModeFlag;
    field.bitCount = 1;
    Utl_Mpeg4_WriteBits(&writer, field);
    field.value    = params->bottomFieldPicOrderInFramePresentFlag;
    field.bitCount = 1;
    Utl_Mpeg4_WriteBits(&writer, field);
    Utl_ExpGolomb_WriteUnsigned(&writer, params->numSliceGroupsMinus1);
    Utl_ExpGolomb_WriteUnsigned(&writer, params->numRefIdxL0DefaultActiveMinus1);
    Utl_ExpGolomb_WriteUnsigned(&writer, params->numRefIdxL1DefaultActiveMinus1);
    field.value    = params->weightedPredFlag;
    field.bitCount = 1;
    Utl_Mpeg4_WriteBits(&writer, field);
    field.value    = params->weightedBipredIdc;
    field.bitCount = WEIGHTED_BIPRED_IDC_BIT_COUNT;
    Utl_Mpeg4_WriteBits(&writer, field);
    Utl_ExpGolomb_WriteSigned(&writer, params->picInitQpMinus26);
    Utl_ExpGolomb_WriteSigned(&writer, params->picInitQsMinus26);
    Utl_ExpGolomb_WriteSigned(&writer, params->chromaQpIndexOffset);
    field.value    = params->deblockingFilterControlPresentFlag;
    field.bitCount = 1;
    Utl_Mpeg4_WriteBits(&writer, field);
    field.value    = params->constrainedIntraPredFlag;
    field.bitCount = 1;
    Utl_Mpeg4_WriteBits(&writer, field);
    field.value    = params->redundantPicCntPresentFlag;
    field.bitCount = 1;
    Utl_Mpeg4_WriteBits(&writer, field);
    if (params->transform8x8ModeFlag || params->picScalingMatrixPresentFlag ||
        params->secondChromaQpIndexOffset != params->chromaQpIndexOffset)
    {
        field.value    = params->transform8x8ModeFlag;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        field.value    = params->picScalingMatrixPresentFlag;
        field.bitCount = 1;
        Utl_Mpeg4_WriteBits(&writer, field);
        if (params->picScalingMatrixPresentFlag)
        {
            scaling_list_count = UTL_H264_SCALING_LIST_4X4_COUNT;
            if (params->transform8x8ModeFlag)
            {
                if (UTL_H264_CHROMA_FORMAT_444 == params->chromaFormatIdc)
                {
                    scaling_list_count = UTL_H264_MAX_SCALING_LIST_COUNT;
                }
                else
                {
                    scaling_list_count += UTL_H264_SCALING_LIST_8X8_COUNT;
                }
            }
            for (scaling_list_index = 0; scaling_list_index < scaling_list_count; ++scaling_list_index)
            {
                scaling_list = &params->scalingLists[scaling_list_index];

                if (scaling_list_index < UTL_H264_SCALING_LIST_4X4_COUNT)
                {
                    element_count = UTL_H264_SCALING_LIST_4X4_ELEMENT_COUNT;
                }
                else
                {
                    element_count = UTL_H264_SCALING_LIST_8X8_ELEMENT_COUNT;
                }

                field.value    = scaling_list->presentFlag;
                field.bitCount = 1;
                Utl_Mpeg4_WriteBits(&writer, field);
                if (scaling_list->presentFlag)
                {
                    // next_scale == 0 selects the default list at index zero, or repeats the last
                    // coefficient thereafter. Neither form writes further delta_scale values.
                    next_scale = SCALING_LIST_INITIAL_SCALE;
                    for (element_index = 0; element_index < element_count; ++element_index)
                    {
                        delta_scale = scaling_list->deltaScale[element_index];
                        LOGASSERT(SCALING_LIST_MIN_DELTA <= delta_scale);
                        LOGASSERT(SCALING_LIST_MAX_DELTA >= delta_scale);

                        Utl_ExpGolomb_WriteSigned(&writer, delta_scale);
                        next_scale =
                            (next_scale + delta_scale + SCALING_LIST_SCALE_MODULUS) % SCALING_LIST_SCALE_MODULUS;

                        if (0 == next_scale)
                        {
                            break;
                        }
                    }
                }
            }
        }
        Utl_ExpGolomb_WriteSigned(&writer, params->secondChromaQpIndexOffset);
    }
    rbsp_bit_offset = 0;
    if (writer.valid)
    {
        LOGASSERT(UINT32_MAX >= writer.position);
        rbsp_bit_offset = (uint32_t)writer.position;
    }
    return rbsp_bit_offset;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint32_t WriteSEI(byte_t*       rbsp,
                         uint64_t      rbspCapacity,
                         uint32_t      payloadType,
                         const byte_t* pPayloadBytes,
                         uint64_t      payloadByteCount)
{
    Utl_Mpeg4_BitWriter_t writer;
    bool_t                has_payload;
    bool_t                has_storage;
    bool_t                valid_payload;
    bool_t                fits_span;
    Utl_Mpeg4_Span_t      payload;
    uint32_t              written;

    LOGASSERT(NULL != rbsp);
    LOGASSERT(0 < rbspCapacity);
    LOGASSERT(UINT32_MAX >= rbspCapacity);
    has_payload   = (0 < payloadByteCount);
    has_storage   = (NULL != pPayloadBytes);
    valid_payload = (FALSE == has_payload) || has_storage;
    LOGASSERT(valid_payload);

    CRT_Memory_Set(rbsp, 0, rbspCapacity);
    Utl_Mpeg4_InitializeBitWriter(&writer, rbsp, (uint32_t)rbspCapacity);
    WriteSEIExtendedValue(&writer, payloadType);
    WriteSEIExtendedValue(&writer, payloadByteCount);
    fits_span    = (UINT32_MAX >= payloadByteCount);
    writer.valid = writer.valid && fits_span;
    if (writer.valid)
    {
        payload.data      = pPayloadBytes;
        payload.byteCount = (uint32_t)payloadByteCount;
        Utl_Mpeg4_WriteAlignedBytes(&writer, payload);
    }
    written = 0;
    if (writer.valid)
    {
        LOGASSERT(UINT32_MAX >= writer.position);
        written = (uint32_t)writer.position;
    }
    return written;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Converts the supplied component bit depths to bit_depth_luma_minus8 and
 *  bit_depth_chroma_minus8 syntax values.
 *
 *  Only picOrderCntType zero is supported; type-one POC offsets and cycle data remain zeroed.
 *  Separate colour-plane coding and SPS scaling matrices are unsupported, so their presence flags
 *  are FALSE and every scaling list remains zeroed.
 *
 *  VUI overscan, video-signal and colour-description data, chroma-location data, NAL and VCL HRD
 *  parameters, low-delay HRD, and picture-structure signaling are unsupported. Their presence
 *  flags and associated storage remain FALSE or zero.
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
                            uint32_t              maxDecFrameBuffering)
{
    uint32_t width_in_mbs;
    uint32_t height_in_mbs;
    uint32_t height_in_map_units;
    uint32_t padded_width;
    uint32_t padded_height;
    uint32_t sar_width;
    uint32_t sar_height;
    uint8_t  aspect_ratio_idc;

    LOGASSERT(NULL != params);
    LOGASSERT(seqParameterSetId <= UTL_H264_MAX_SEQUENCE_PARAMETER_SET_ID);
    LOGASSERT(chromaFormatIdc <= UTL_H264_CHROMA_FORMAT_444);
    LOGASSERT((UTL_H264_MIN_BIT_DEPTH <= bitDepthLuma) && (UTL_H264_MAX_BIT_DEPTH >= bitDepthLuma));
    LOGASSERT((UTL_H264_MIN_BIT_DEPTH <= bitDepthChroma) && (UTL_H264_MAX_BIT_DEPTH >= bitDepthChroma));
    LOGASSERT((FALSE == qpprimeYZeroTransformBypassFlag) || (TRUE == qpprimeYZeroTransformBypassFlag));
    LOGASSERT(0 != width);
    LOGASSERT(0 != height);
    LOGASSERT(0 != frameRateNumerator);
    LOGASSERT(0 != frameRateDenominator);
    LOGASSERT(0 != maximumFrameNumber);
    LOGASSERT(0 != maximumPicOrderCntLsb);
    LOGASSERT(maxNumRefFrames <= UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES);
    LOGASSERT((FALSE == gapsInFrameNumValueAllowedFlag) || (TRUE == gapsInFrameNumValueAllowedFlag));
    LOGASSERT((FALSE == interlaced) || (TRUE == interlaced));
    LOGASSERT((FALSE == mbAdaptiveFrameFieldFlag) || (TRUE == mbAdaptiveFrameFieldFlag));
    LOGASSERT((TRUE == interlaced) || (FALSE == mbAdaptiveFrameFieldFlag));
    LOGASSERT((FALSE == direct8x8InferenceFlag) || (TRUE == direct8x8InferenceFlag));
    LOGASSERT((FALSE == vuiParametersPresentFlag) || (TRUE == vuiParametersPresentFlag));
    LOGASSERT((FALSE == timingInfoPresentFlag) || (TRUE == timingInfoPresentFlag));
    LOGASSERT((FALSE == fixedFrameRateFlag) || (TRUE == fixedFrameRateFlag));
    LOGASSERT((FALSE == bitstreamRestrictionFlag) || (TRUE == bitstreamRestrictionFlag));
    LOGASSERT((FALSE == motionVectorsOverPicBoundariesFlag) || (TRUE == motionVectorsOverPicBoundariesFlag));
    LOGASSERT(((0 == sampleAspectRatioWidth) && (0 == sampleAspectRatioHeight)) ||
              ((0 != sampleAspectRatioWidth) && (0 != sampleAspectRatioHeight)));
    LOGASSERT(maxNumReorderFrames <= maxDecFrameBuffering);

    Utl_H264_DeriveCodedDimensions(width,
                                   height,
                                   interlaced,
                                   &width_in_mbs,
                                   &height_in_mbs,
                                   &padded_width,
                                   &padded_height);
    height_in_map_units = height_in_mbs;
    if (interlaced)
    {
        height_in_map_units /= 2;
    }

    Utl_H264_DeriveAspectRatio(sampleAspectRatioWidth,
                               sampleAspectRatioHeight,
                               &aspect_ratio_idc,
                               &sar_width,
                               &sar_height);

    CRT_Memory_Set(params, 0, sizeof((*params)));
    params->profileIdc         = profileIdc;
    params->constraintSetFlags = constraintSetFlags;
    params->levelIdc = Utl_H264_DeriveLevelIdc(padded_width, padded_height, frameRateNumerator, frameRateDenominator);
    params->seqParameterSetId               = seqParameterSetId;
    params->chromaFormatIdc                 = chromaFormatIdc;
    params->bitDepthLumaMinus8              = bitDepthLuma - UTL_H264_MIN_BIT_DEPTH;
    params->bitDepthChromaMinus8            = bitDepthChroma - UTL_H264_MIN_BIT_DEPTH;
    params->qpprimeYZeroTransformBypassFlag = (uint8_t)qpprimeYZeroTransformBypassFlag;
    params->log2MaxFrameNumMinus4           = Utl_H264_DeriveLog2Minus4(maximumFrameNumber);
    params->picOrderCntType                 = UTL_H264_PIC_ORDER_CNT_TYPE_0;
    params->log2MaxPicOrderCntLsbMinus4     = Utl_H264_DeriveLog2Minus4(maximumPicOrderCntLsb);
    params->maxNumRefFrames                 = maxNumRefFrames;
    params->gapsInFrameNumValueAllowedFlag  = (uint8_t)gapsInFrameNumValueAllowedFlag;
    params->picWidthInMbsMinus1             = width_in_mbs - 1;
    params->picHeightInMapUnitsMinus1       = height_in_map_units - 1;
    params->frameMbsOnlyFlag                = (uint8_t)(interlaced ? FALSE : TRUE);
    params->mbAdaptiveFrameFieldFlag        = (uint8_t)mbAdaptiveFrameFieldFlag;
    params->direct8x8InferenceFlag          = (uint8_t)direct8x8InferenceFlag;
    params->frameCroppingFlag               = (uint8_t)Utl_H264_DeriveFrameCropOffsets(width,
                                                                                       height,
                                                                                       padded_width,
                                                                                       padded_height,
                                                                                       chromaFormatIdc,
                                                                                       interlaced,
                                                                                       &params->frameCropRightOffset,
                                                                                       &params->frameCropBottomOffset);
    params->vuiParametersPresentFlag        = (uint8_t)vuiParametersPresentFlag;
    params->aspectRatioInfoPresentFlag      = (uint8_t)(UTL_H264_ASPECT_RATIO_IDC_UNSPECIFIED != aspect_ratio_idc);
    params->aspectRatioIdc                  = aspect_ratio_idc;
    params->sarWidth                        = (uint16_t)sar_width;
    params->sarHeight                       = (uint16_t)sar_height;
    params->timingInfoPresentFlag           = (uint8_t)timingInfoPresentFlag;
    params->numUnitsInTick                  = frameRateDenominator;
    params->timeScale                       = Utl_H264_DeriveTimeScale(frameRateNumerator);
    params->fixedFrameRateFlag              = (uint8_t)fixedFrameRateFlag;
    params->bitstreamRestrictionFlag        = (uint8_t)bitstreamRestrictionFlag;
    params->motionVectorsOverPicBoundariesFlag = (uint8_t)motionVectorsOverPicBoundariesFlag;
    params->maxBytesPerPicDenom                = maxBytesPerPicDenom;
    params->maxBitsPerMbDenom                  = maxBitsPerMbDenom;
    Utl_H264_DeriveMotionVectorLimits(bitstreamRestrictionFlag,
                                      maxMotionVectorLengthHorizontal,
                                      maxMotionVectorLengthVertical,
                                      &params->log2MaxMvLengthHorizontal,
                                      &params->log2MaxMvLengthVertical);
    params->maxNumReorderFrames  = maxNumReorderFrames;
    params->maxDecFrameBuffering = maxDecFrameBuffering;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Scans raster-order coefficients in H.264 frame scan order and normalizes each difference into
 *  the delta_scale range -128..127. Positive coefficients keep nextScale nonzero, so every input
 *  coefficient is explicitly represented. Default-list and repeated-tail syntax can instead be
 *  supplied directly through Utl_H264_ScalingList_t.
 */
void Utl_H264_InitializeScalingList(Utl_H264_ScalingList_t* params, const uint8_t* coefficients, uint32_t elementCount)
{
    bool_t   is_four_by_four;
    bool_t   is_eight_by_eight;
    bool_t   valid_count;
    uint32_t side;
    int32_t  previous_scale;
    uint32_t row;
    uint32_t column;
    uint32_t element_index;
    uint32_t raster_index;
    int32_t  scale;
    int32_t  delta_scale;
    bool_t   is_even_diagonal;
    bool_t   at_last_column;
    bool_t   at_first_row;
    bool_t   at_last_row;
    bool_t   at_first_column;

    LOGASSERT(NULL != params);
    LOGASSERT(NULL != coefficients);
    is_four_by_four   = (UTL_H264_SCALING_LIST_4X4_ELEMENT_COUNT == elementCount);
    is_eight_by_eight = (UTL_H264_SCALING_LIST_8X8_ELEMENT_COUNT == elementCount);
    valid_count       = is_four_by_four || is_eight_by_eight;
    LOGASSERT(valid_count);
    side           = is_four_by_four ? 4 : 8;
    previous_scale = SCALING_LIST_INITIAL_SCALE;
    row            = 0;
    column         = 0;
    CRT_Memory_Set(params, 0, sizeof((*params)));
    params->presentFlag = TRUE;
    for (element_index = 0; element_index < elementCount; ++element_index)
    {
        raster_index = (row * side) + column;
        scale        = coefficients[raster_index];
        LOGASSERT(0 < scale);
        delta_scale = scale - previous_scale;
        if (SCALING_LIST_MAX_DELTA < delta_scale)
        {
            delta_scale -= SCALING_LIST_SCALE_MODULUS;
        }
        else if (SCALING_LIST_MIN_DELTA > delta_scale)
        {
            delta_scale += SCALING_LIST_SCALE_MODULUS;
        }
        else
        {
            /* The signed difference already fits delta_scale. */
        }
        params->deltaScale[element_index] = delta_scale;
        previous_scale                    = scale;
        is_even_diagonal                  = (0 == ((row + column) % 2));
        at_last_column                    = (side - 1 == column);
        at_first_row                      = (0 == row);
        at_last_row                       = (side - 1 == row);
        at_first_column                   = (0 == column);
        if (is_even_diagonal)
        {
            if (at_last_column)
            {
                ++row;
            }
            else if (at_first_row)
            {
                ++column;
            }
            else
            {
                --row;
                ++column;
            }
        }
        else if (at_last_row)
        {
            ++column;
        }
        else if (at_first_column)
        {
            ++row;
        }
        else
        {
            ++row;
            --column;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Initializes PPS coding defaults without scaling matrices. Callers may subsequently set
 *  picScalingMatrixPresentFlag and populate scalingLists with Utl_H264_InitializeScalingList.
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
                            int32_t               secondChromaQpIndexOffset)
{
    LOGASSERT(NULL != params);
    LOGASSERT(picParameterSetId <= UTL_H264_MAX_PICTURE_PARAMETER_SET_ID);
    LOGASSERT(seqParameterSetId <= UTL_H264_MAX_SEQUENCE_PARAMETER_SET_ID);
    LOGASSERT(picInitQp <= UTL_H264_MAX_QUANTIZATION_PARAMETER);
    LOGASSERT(picInitQs <= UTL_H264_MAX_QUANTIZATION_PARAMETER);
    LOGASSERT((0 == entropyCodingMode) || (1 == entropyCodingMode));
    LOGASSERT((0 == bottomFieldPicOrderInFramePresent) || (1 == bottomFieldPicOrderInFramePresent));
    LOGASSERT((0 < numRefIdxL0DefaultActive) && (UTL_H264_MAX_REFERENCE_LIST_ENTRIES >= numRefIdxL0DefaultActive));
    LOGASSERT((0 < numRefIdxL1DefaultActive) && (UTL_H264_MAX_REFERENCE_LIST_ENTRIES >= numRefIdxL1DefaultActive));
    LOGASSERT((0 == weightedPrediction) || (1 == weightedPrediction));
    LOGASSERT(UTL_H264_MAX_WEIGHTED_BIPRED_IDC >= weightedBipredIdc);
    LOGASSERT((UTL_H264_MIN_CHROMA_QP_INDEX_OFFSET <= chromaQpIndexOffset) &&
              (UTL_H264_MAX_CHROMA_QP_INDEX_OFFSET >= chromaQpIndexOffset));
    LOGASSERT((0 == deblockingFilterControlPresent) || (1 == deblockingFilterControlPresent));
    LOGASSERT((0 == constrainedIntraPrediction) || (1 == constrainedIntraPrediction));
    LOGASSERT((0 == redundantPictures) || (1 == redundantPictures));
    LOGASSERT((0 == transform8x8Mode) || (1 == transform8x8Mode));
    LOGASSERT(chromaFormatIdc <= UTL_H264_CHROMA_FORMAT_444);
    LOGASSERT((UTL_H264_MIN_CHROMA_QP_INDEX_OFFSET <= secondChromaQpIndexOffset) &&
              (UTL_H264_MAX_CHROMA_QP_INDEX_OFFSET >= secondChromaQpIndexOffset));

    CRT_Memory_Set(params, 0, sizeof((*params)));
    params->picParameterSetId                     = picParameterSetId;
    params->seqParameterSetId                     = seqParameterSetId;
    params->entropyCodingModeFlag                 = (uint8_t)entropyCodingMode;
    params->bottomFieldPicOrderInFramePresentFlag = (uint8_t)bottomFieldPicOrderInFramePresent;
    params->numSliceGroupsMinus1                  = 0;
    params->numRefIdxL0DefaultActiveMinus1        = numRefIdxL0DefaultActive - 1;
    params->numRefIdxL1DefaultActiveMinus1        = numRefIdxL1DefaultActive - 1;
    params->weightedPredFlag                      = (uint8_t)weightedPrediction;
    params->weightedBipredIdc                     = weightedBipredIdc;
    params->picInitQpMinus26                      = (int32_t)picInitQp - UTL_H264_QUANTIZATION_PARAMETER_BIAS;
    params->picInitQsMinus26                      = (int32_t)picInitQs - UTL_H264_QUANTIZATION_PARAMETER_BIAS;
    params->chromaQpIndexOffset                   = chromaQpIndexOffset;
    params->deblockingFilterControlPresentFlag    = (uint8_t)deblockingFilterControlPresent;
    params->constrainedIntraPredFlag              = (uint8_t)constrainedIntraPrediction;
    params->redundantPicCntPresentFlag            = (uint8_t)redundantPictures;
    params->transform8x8ModeFlag                  = (uint8_t)transform8x8Mode;
    params->picScalingMatrixPresentFlag           = (uint8_t)FALSE;
    params->chromaFormatIdc                       = chromaFormatIdc;
    params->secondChromaQpIndexOffset             = secondChromaQpIndexOffset;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint8_t Utl_H264_DeriveLevelIdc(uint32_t width,
                                uint32_t height,
                                uint32_t frameRateNumerator,
                                uint32_t frameRateDenominator)
{
    uint32_t       width_in_mbs;
    uint32_t       height_in_mbs;
    uint32_t       padded_width;
    uint32_t       padded_height;
    uint64_t       frame_mbs;
    uint8_t        selected;
    uint32_t       level_index;
    H264LevelIdc_t limits;
    bool_t         fits_frame;
    bool_t         fits_width;
    bool_t         fits_height;
    bool_t         fits_geometry;
    bool_t         fits_rate;

    LOGASSERT(0 < width);
    LOGASSERT(0 < height);
    LOGASSERT(0 < frameRateNumerator);
    LOGASSERT(0 < frameRateDenominator);
    Utl_H264_DeriveCodedDimensions(width, height, FALSE, &width_in_mbs, &height_in_mbs, &padded_width, &padded_height);
    frame_mbs = (uint64_t)width_in_mbs * height_in_mbs;
    selected  = 0;
    for (level_index = 0; level_index < LEVEL_LIMIT_COUNT; ++level_index)
    {
        switch (level_index)
        {
        case 0:
            limits.maximumFrameMacroblocks     = LEVEL_1_0_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_1_0_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_1_0_IDC;
            break;
        case 1:
            limits.maximumFrameMacroblocks     = LEVEL_1_1_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_1_1_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_1_1_IDC;
            break;
        case 2:
            limits.maximumFrameMacroblocks     = LEVEL_1_2_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_1_2_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_1_2_IDC;
            break;
        case 3:
            limits.maximumFrameMacroblocks     = LEVEL_1_3_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_1_3_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_1_3_IDC;
            break;
        case 4:
            limits.maximumFrameMacroblocks     = LEVEL_2_1_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_2_1_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_2_1_IDC;
            break;
        case 5:
            limits.maximumFrameMacroblocks     = LEVEL_2_2_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_2_2_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_2_2_IDC;
            break;
        case 6:
            limits.maximumFrameMacroblocks     = LEVEL_3_0_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_3_0_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_3_0_IDC;
            break;
        case 7:
            limits.maximumFrameMacroblocks     = LEVEL_3_1_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_3_1_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_3_1_IDC;
            break;
        case 8:
            limits.maximumFrameMacroblocks     = LEVEL_3_2_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_3_2_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_3_2_IDC;
            break;
        case 9:
            limits.maximumFrameMacroblocks     = LEVEL_4_0_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_4_0_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_4_0_IDC;
            break;
        case 10:
            limits.maximumFrameMacroblocks     = LEVEL_4_2_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_4_2_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_4_2_IDC;
            break;
        case 11:
            limits.maximumFrameMacroblocks     = LEVEL_5_0_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_5_0_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_5_0_IDC;
            break;
        case 12:
            limits.maximumFrameMacroblocks     = LEVEL_5_1_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_5_1_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_5_1_IDC;
            break;
        case 13:
            limits.maximumFrameMacroblocks     = LEVEL_5_2_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_5_2_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_5_2_IDC;
            break;
        case 14:
            limits.maximumFrameMacroblocks     = LEVEL_6_0_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_6_0_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_6_0_IDC;
            break;
        case 15:
            limits.maximumFrameMacroblocks     = LEVEL_6_1_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_6_1_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_6_1_IDC;
            break;
        case 16:
            limits.maximumFrameMacroblocks     = LEVEL_6_2_MAX_FRAME_MBS;
            limits.maximumMacroblocksPerSecond = LEVEL_6_2_MAX_MBS_PER_SECOND;
            limits.levelIdc                    = UTL_H264_LEVEL_6_2_IDC;
            break;
        default:
            limits.maximumFrameMacroblocks     = 0;
            limits.maximumMacroblocksPerSecond = 0;
            limits.levelIdc                    = 0;
            break;
        }
        fits_frame    = (frame_mbs <= limits.maximumFrameMacroblocks);
        fits_width    = ((uint64_t)width_in_mbs * width_in_mbs <=
                         (uint64_t)limits.maximumFrameMacroblocks * LEVEL_MAX_FRAME_DIMENSION_SQUARED_MULTIPLIER);
        fits_height   = ((uint64_t)height_in_mbs * height_in_mbs <=
                         (uint64_t)limits.maximumFrameMacroblocks * LEVEL_MAX_FRAME_DIMENSION_SQUARED_MULTIPLIER);
        fits_geometry = fits_frame && fits_width && fits_height;
        if (fits_geometry)
        {
            fits_rate =
                (frame_mbs * frameRateNumerator <= (uint64_t)limits.maximumMacroblocksPerSecond * frameRateDenominator);
            if (fits_rate)
            {
                selected = limits.levelIdc;
                break;
            }
        }
    }
    LOGASSERT(0 < selected);
    return selected;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  ITU-T H.264 (08/2024), A.2.1 through A.2.11. Profile eligibility is an intersection of
 *  restrictions, including the compatibility flags; profile_idc values are not capability ranks.
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
                                          int32_t secondChromaQpIndexOffset)
{
    bool_t valid_slice;
    bool_t valid_constraints;
    bool_t valid_bipred;
    bool_t first_qp_minimum;
    bool_t first_qp_maximum;
    bool_t second_qp_minimum;
    bool_t second_qp_maximum;
    bool_t valid_qp;
    bool_t is_valid;
    bool_t baseline;
    bool_t main_profile;
    bool_t extended;
    bool_t high_profile;
    bool_t intra_only;
    bool_t profile_supported;
    bool_t uses_cavlc;
    bool_t is_intra;
    bool_t is_predictive;
    bool_t is_baseline_slice;
    bool_t unweighted_prediction;
    bool_t unweighted_bipred;
    bool_t baseline_tools_valid;
    bool_t distinct_chroma_offsets;
    bool_t has_extension;
    bool_t legacy_profile;
    bool_t legacy_tools_valid;
    bool_t is_switching_predictive;
    bool_t is_switching_intra;
    bool_t is_switching;
    bool_t restricted_picture;
    bool_t needs_regular_picture;
    bool_t regular_picture;
    bool_t is_high;
    bool_t is_high10;
    bool_t permits_progressive_constraint;
    bool_t is_bidirectional;
    bool_t uses_bipred;
    bool_t uses_weighting;
    bool_t violates_constrained_high;
    bool_t constrained_high_valid;

    valid_slice       = (sliceType < 2 * UTL_H264_SLICE_TYPE_ALL_OFFSET);
    valid_constraints = TRUE;
    if (constraintSetFlags & RESERVED_CONSTRAINT_BITS_MASK)
    {
        valid_constraints = FALSE;
    }
    valid_bipred       = (weightedBipredIdc <= UTL_H264_MAX_WEIGHTED_BIPRED_IDC);
    first_qp_minimum   = (UTL_H264_MIN_CHROMA_QP_INDEX_OFFSET <= chromaQpIndexOffset);
    first_qp_maximum   = (UTL_H264_MAX_CHROMA_QP_INDEX_OFFSET >= chromaQpIndexOffset);
    second_qp_minimum  = (UTL_H264_MIN_CHROMA_QP_INDEX_OFFSET <= secondChromaQpIndexOffset);
    second_qp_maximum  = (UTL_H264_MAX_CHROMA_QP_INDEX_OFFSET >= secondChromaQpIndexOffset);
    valid_qp           = first_qp_minimum && first_qp_maximum && second_qp_minimum && second_qp_maximum;
    is_valid           = valid_slice && valid_constraints && valid_bipred && valid_qp;
    sliceType         %= UTL_H264_SLICE_TYPE_ALL_OFFSET;
    baseline           = (UTL_H264_PROFILE_BASELINE == profileIdc);
    main_profile       = (UTL_H264_PROFILE_MAIN == profileIdc);
    extended           = (UTL_H264_PROFILE_EXTENDED == profileIdc);
    if (constraintSetFlags & UTL_H264_CONSTRAINT_SET0_FLAG)
    {
        baseline = TRUE;
    }
    if (constraintSetFlags & UTL_H264_CONSTRAINT_SET1_FLAG)
    {
        main_profile = TRUE;
    }
    if (constraintSetFlags & UTL_H264_CONSTRAINT_SET2_FLAG)
    {
        extended = TRUE;
    }
    high_profile      = FALSE;
    intra_only        = FALSE;
    profile_supported = TRUE;
    uses_cavlc        = (FALSE == entropyCodingModeFlag);
    switch (profileIdc)
    {
    case UTL_H264_PROFILE_BASELINE:
    case UTL_H264_PROFILE_MAIN:
    case UTL_H264_PROFILE_EXTENDED:
        break;
    case UTL_H264_PROFILE_CAVLC_444_INTRA:
        is_valid     = is_valid && uses_cavlc;
        intra_only   = TRUE;
        high_profile = TRUE;
        break;
    case UTL_H264_PROFILE_HIGH:
    case UTL_H264_PROFILE_HIGH_10:
    case UTL_H264_PROFILE_HIGH_422:
    case UTL_H264_PROFILE_HIGH_444_PREDICTIVE:
        high_profile = TRUE;
        if (constraintSetFlags & UTL_H264_CONSTRAINT_SET3_FLAG)
        {
            intra_only = TRUE;
        }
        break;
    default:
        profile_supported = FALSE;
        break;
    }
    is_valid              = is_valid && profile_supported;
    is_intra              = (UTL_H264_SLICE_TYPE_I == sliceType);
    is_predictive         = (UTL_H264_SLICE_TYPE_P == sliceType);
    is_baseline_slice     = is_intra || is_predictive;
    unweighted_prediction = (FALSE == weightedPredFlag);
    unweighted_bipred     = (0 == weightedBipredIdc);
    baseline_tools_valid =
        frameMbsOnlyFlag && uses_cavlc && unweighted_prediction && unweighted_bipred && is_baseline_slice;
    if (baseline)
    {
        is_valid = is_valid && baseline_tools_valid;
    }
    distinct_chroma_offsets = (secondChromaQpIndexOffset != chromaQpIndexOffset);
    has_extension           = transform8x8ModeFlag || scalingMatrixPresentFlag || distinct_chroma_offsets;
    legacy_profile          = baseline || main_profile || extended;
    legacy_tools_valid      = (FALSE == has_extension);
    if (legacy_profile)
    {
        is_valid = is_valid && legacy_tools_valid;
    }
    if (extended)
    {
        is_valid = is_valid && uses_cavlc;
    }
    is_switching_predictive = (UTL_H264_SLICE_TYPE_SP == sliceType);
    is_switching_intra      = (UTL_H264_SLICE_TYPE_SI == sliceType);
    is_switching            = is_switching_predictive || is_switching_intra;
    restricted_picture      = redundantPicCntPresentFlag || is_switching;
    needs_regular_picture   = main_profile || high_profile;
    regular_picture         = (FALSE == restricted_picture);
    if (needs_regular_picture)
    {
        is_valid = is_valid && regular_picture;
    }
    if (intra_only)
    {
        is_valid = is_valid && is_intra;
    }
    is_high                        = (UTL_H264_PROFILE_HIGH == profileIdc);
    is_high10                      = (UTL_H264_PROFILE_HIGH_10 == profileIdc);
    permits_progressive_constraint = is_high || is_high10;
    if (constraintSetFlags & UTL_H264_CONSTRAINT_SET4_FLAG)
    {
        if (permits_progressive_constraint)
        {
            is_valid = is_valid && frameMbsOnlyFlag;
        }
        if (constraintSetFlags & UTL_H264_CONSTRAINT_SET5_FLAG)
        {
            is_bidirectional          = (UTL_H264_SLICE_TYPE_B == sliceType);
            uses_bipred               = (0 < weightedBipredIdc);
            uses_weighting            = weightedPredFlag || uses_bipred;
            violates_constrained_high = is_bidirectional || uses_weighting;
            constrained_high_valid    = (FALSE == violates_constrained_high);
            if (is_high)
            {
                is_valid = is_valid && constrained_high_valid;
            }
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_H264_DeriveLog2Minus4(uint32_t maximumValue)
{
    uint32_t bit_count;
    uint64_t value_limit;

    for (bit_count = UTL_H264_MIN_FRAME_NUM_BIT_COUNT; bit_count < UTL_H264_MAX_FRAME_NUM_BIT_COUNT; bit_count++)
    {
        value_limit = (uint64_t)1 << bit_count;

        if (value_limit > maximumValue)
        {
            break;
        }
    }

    return bit_count - UTL_H264_MIN_FRAME_NUM_BIT_COUNT;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_H264_DeriveMaximumFrameNumber(uint32_t intraIdrPeriod)
{
    LOGASSERT(intraIdrPeriod <= UINT16_MAX);

    return 0 != intraIdrPeriod ? intraIdrPeriod : UINT16_MAX;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Uses the same syntax width as the SPS frame-number and POC fields.
 */
uint32_t Utl_H264_DeriveValueMask(uint32_t maximumValue)
{
    return ((uint32_t)1 << (Utl_H264_DeriveLog2Minus4(maximumValue) + UTL_H264_MIN_FRAME_NUM_BIT_COUNT)) - 1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_H264_DeriveFieldOrderCount(uint32_t frameIndex)
{
    LOGASSERT(((uint32_t)INT32_MAX / FIELD_ORDER_COUNT_MULTIPLIER) >= frameIndex);

    return frameIndex * FIELD_ORDER_COUNT_MULTIPLIER;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_H264_DeriveMaximumPicOrderCntLsb(uint32_t maximumFrameNumber)
{
    return ((UINT16_MAX / FIELD_ORDER_COUNT_MULTIPLIER) >= maximumFrameNumber)
               ? maximumFrameNumber * FIELD_ORDER_COUNT_MULTIPLIER
               : UINT16_MAX;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint8_t Utl_H264_DeriveMotionVectorLengthLog2(uint32_t maximumLength)
{
    uint8_t  logarithm;
    uint64_t length_limit;

    LOGASSERT(0 != maximumLength);

    for (logarithm = 0; logarithm <= UTL_H264_MAX_MOTION_VECTOR_LENGTH_LOG2; logarithm++)
    {
        length_limit = (uint64_t)1 << logarithm;

        if (length_limit >= maximumLength)
        {
            break;
        }
    }

    return logarithm;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Rounds the width to a macroblock boundary. The height is rounded to one macroblock row for
 *  progressive coding or two macroblock rows for field-capable coding.
 */
void Utl_H264_DeriveCodedDimensions(uint32_t  width,
                                    uint32_t  height,
                                    bool_t    interlaced,
                                    uint32_t* widthInMbs,
                                    uint32_t* heightInMbs,
                                    uint32_t* paddedWidth,
                                    uint32_t* paddedHeight)
{
    uint32_t width_in_mbs;
    uint32_t height_in_map_units;
    uint32_t height_alignment;
    uint32_t padded_width;
    uint32_t padded_height;

    LOGASSERT(0 != width);
    LOGASSERT(0 != height);
    LOGASSERT(FALSE == interlaced || TRUE == interlaced);
    LOGASSERT(NULL != widthInMbs);
    LOGASSERT(NULL != heightInMbs);
    LOGASSERT(NULL != paddedWidth);
    LOGASSERT(NULL != paddedHeight);
    LOGASSERT(width <= UINT32_MAX - (MACROBLOCK_WIDTH - 1));

    height_alignment = interlaced ? MACROBLOCK_HEIGHT * 2 : MACROBLOCK_HEIGHT;
    LOGASSERT(height <= UINT32_MAX - (height_alignment - 1));

    width_in_mbs        = (width + MACROBLOCK_WIDTH - 1) / MACROBLOCK_WIDTH;
    height_in_map_units = (height + height_alignment - 1) / height_alignment;
    LOGASSERT(width_in_mbs <= UINT32_MAX / MACROBLOCK_WIDTH);
    LOGASSERT(height_in_map_units <= UINT32_MAX / height_alignment);

    padded_width  = width_in_mbs * MACROBLOCK_WIDTH;
    padded_height = height_in_map_units * height_alignment;

    (*widthInMbs)   = width_in_mbs;
    (*heightInMbs)  = padded_height / MACROBLOCK_HEIGHT;
    (*paddedWidth)  = padded_width;
    (*paddedHeight) = padded_height;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Selects the H.264 crop-unit dimensions for monochrome, 4:2:0, 4:2:2, or 4:4:4 chroma. Field
 *  coding doubles the vertical unit because crop offsets are expressed per coded frame.
 */
void Utl_H264_DeriveCropUnits(uint32_t chromaFormatIdc, bool_t interlaced, uint32_t* cropUnitX, uint32_t* cropUnitY)
{
    LOGASSERT(chromaFormatIdc <= UTL_H264_CHROMA_FORMAT_444);
    LOGASSERT(FALSE == interlaced || TRUE == interlaced);
    LOGASSERT(NULL != cropUnitX);
    LOGASSERT(NULL != cropUnitY);

    switch (chromaFormatIdc)
    {
    case UTL_H264_CHROMA_FORMAT_420:
        (*cropUnitX) = CHROMA_420_CROP_UNIT_X;
        (*cropUnitY) = CHROMA_420_CROP_UNIT_Y;
        break;

    case UTL_H264_CHROMA_FORMAT_422:
        (*cropUnitX) = CHROMA_422_CROP_UNIT_X;
        (*cropUnitY) = CHROMA_422_CROP_UNIT_Y;
        break;

    default:
        (*cropUnitX) = CHROMA_444_CROP_UNIT_X;
        (*cropUnitY) = CHROMA_444_CROP_UNIT_Y;
        break;
    }
    if (interlaced)
    {
        (*cropUnitY) *= INTERLACED_CROP_UNIT_Y_MULTIPLIER;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Left and top offsets are zero; padding is removed at the right and bottom edges.
 */
bool_t Utl_H264_DeriveFrameCropOffsets(uint32_t  width,
                                       uint32_t  height,
                                       uint32_t  paddedWidth,
                                       uint32_t  paddedHeight,
                                       uint32_t  chromaFormatIdc,
                                       bool_t    interlaced,
                                       uint32_t* cropRightOffset,
                                       uint32_t* cropBottomOffset)
{
    uint32_t crop_unit_x;
    uint32_t crop_unit_y;

    LOGASSERT(0 != width && width <= paddedWidth);
    LOGASSERT(0 != height && height <= paddedHeight);
    LOGASSERT(NULL != cropRightOffset);
    LOGASSERT(NULL != cropBottomOffset);

    Utl_H264_DeriveCropUnits(chromaFormatIdc, interlaced, &crop_unit_x, &crop_unit_y);
    LOGASSERT(0 == (paddedWidth - width) % crop_unit_x);
    LOGASSERT(0 == (paddedHeight - height) % crop_unit_y);

    (*cropRightOffset)  = (paddedWidth - width) / crop_unit_x;
    (*cropBottomOffset) = (paddedHeight - height) / crop_unit_y;
    return paddedWidth != width || paddedHeight != height;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_H264_DeriveTimeScale(uint32_t frameRateNumerator)
{
    LOGASSERT(0 != frameRateNumerator);
    LOGASSERT(frameRateNumerator <= UINT32_MAX / TIME_SCALE_MULTIPLIER);

    return frameRateNumerator * TIME_SCALE_MULTIPLIER;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Utl_H264_DeriveMotionVectorLimits(bool_t    restricted,
                                       uint32_t  maximumHorizontal,
                                       uint32_t  maximumVertical,
                                       uint32_t* log2Horizontal,
                                       uint32_t* log2Vertical)
{
    LOGASSERT(FALSE == restricted || TRUE == restricted);
    LOGASSERT(NULL != log2Horizontal);
    LOGASSERT(NULL != log2Vertical);
    LOGASSERT((FALSE == restricted && 0 == maximumHorizontal && 0 == maximumVertical) ||
              (TRUE == restricted && 0 != maximumHorizontal && 0 != maximumVertical));

    (*log2Horizontal) = Utl_H264_DeriveMotionVectorLengthLog2(restricted ? maximumHorizontal : 1);
    (*log2Vertical)   = Utl_H264_DeriveMotionVectorLengthLog2(restricted ? maximumVertical : 1);
    LOGASSERT((*log2Horizontal) <= UTL_H264_MAX_MOTION_VECTOR_LENGTH_LOG2);
    LOGASSERT((*log2Vertical) <= UTL_H264_MAX_MOTION_VECTOR_LENGTH_LOG2);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Utl_H264_DeriveAspectRatio(uint32_t  width,
                                uint32_t  height,
                                uint8_t*  aspectRatioIdc,
                                uint32_t* sarWidth,
                                uint32_t* sarHeight)
{
    bool_t   has_width;
    bool_t   has_height;
    bool_t   valid_pair;
    uint32_t divisor;
    uint64_t ratio;

    LOGASSERT(NULL != aspectRatioIdc);
    LOGASSERT(NULL != sarWidth);
    LOGASSERT(NULL != sarHeight);
    has_width  = (0 < width);
    has_height = (0 < height);
    valid_pair = (has_width == has_height);
    LOGASSERT(valid_pair);
    (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_UNSPECIFIED;
    (*sarWidth)       = 0;
    (*sarHeight)      = 0;
    if (has_width)
    {
        divisor  = GreatestCommonDivisor(width, height);
        width   /= divisor;
        height  /= divisor;
        ratio    = ((uint64_t)width << SAR_NUMERATOR_SHIFT) | height;
        switch (ratio)
        {
        case ((uint64_t)1 << SAR_NUMERATOR_SHIFT) | 1:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_1_TO_1;
            break;
        case ((uint64_t)12 << SAR_NUMERATOR_SHIFT) | 11:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_12_TO_11;
            break;
        case ((uint64_t)10 << SAR_NUMERATOR_SHIFT) | 11:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_10_TO_11;
            break;
        case ((uint64_t)16 << SAR_NUMERATOR_SHIFT) | 11:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_16_TO_11;
            break;
        case ((uint64_t)40 << SAR_NUMERATOR_SHIFT) | 33:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_40_TO_33;
            break;
        case ((uint64_t)24 << SAR_NUMERATOR_SHIFT) | 11:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_24_TO_11;
            break;
        case ((uint64_t)20 << SAR_NUMERATOR_SHIFT) | 11:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_20_TO_11;
            break;
        case ((uint64_t)32 << SAR_NUMERATOR_SHIFT) | 11:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_32_TO_11;
            break;
        case ((uint64_t)80 << SAR_NUMERATOR_SHIFT) | 33:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_80_TO_33;
            break;
        case ((uint64_t)18 << SAR_NUMERATOR_SHIFT) | 11:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_18_TO_11;
            break;
        case ((uint64_t)15 << SAR_NUMERATOR_SHIFT) | 11:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_15_TO_11;
            break;
        case ((uint64_t)64 << SAR_NUMERATOR_SHIFT) | 33:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_64_TO_33;
            break;
        case ((uint64_t)160 << SAR_NUMERATOR_SHIFT) | 99:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_160_TO_99;
            break;
        case ((uint64_t)4 << SAR_NUMERATOR_SHIFT) | 3:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_4_TO_3;
            break;
        case ((uint64_t)3 << SAR_NUMERATOR_SHIFT) | 2:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_3_TO_2;
            break;
        case ((uint64_t)2 << SAR_NUMERATOR_SHIFT) | 1:
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_2_TO_1;
            break;
        default:
            LOGASSERT(UINT16_MAX >= width);
            LOGASSERT(UINT16_MAX >= height);
            (*aspectRatioIdc) = UTL_H264_ASPECT_RATIO_IDC_EXTENDED_SAR;
            (*sarWidth)       = width;
            (*sarHeight)      = height;
            break;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint8_t Utl_H264_DeriveSliceType(Utl_H264_PictureType_t pictureType)
{
    uint8_t slice_type;

    switch (pictureType)
    {
    case UTL_H264_PICTURE_TYPE_IDR:
    case UTL_H264_PICTURE_TYPE_I:
        slice_type = UTL_H264_SLICE_TYPE_I;
        break;
    case UTL_H264_PICTURE_TYPE_P:
        slice_type = UTL_H264_SLICE_TYPE_P;
        break;
    case UTL_H264_PICTURE_TYPE_B:
        slice_type = UTL_H264_SLICE_TYPE_B;
        break;
    default:
        OS_AssertFailure("Unsupported H.264 picture type", __FILE__, __LINE__);
    }
    return slice_type;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Unused or empty lists retain the minimum syntax count of one.
 */
void Utl_H264_DeriveActiveReferenceCounts(uint8_t   sliceType,
                                          uint32_t  decodedPictureCount,
                                          uint32_t* list0Count,
                                          uint32_t* list1Count)
{
    LOGASSERT(UTL_H264_SLICE_TYPE_I == sliceType || UTL_H264_SLICE_TYPE_P == sliceType ||
              UTL_H264_SLICE_TYPE_B == sliceType);
    LOGASSERT(decodedPictureCount <= UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES);
    LOGASSERT(NULL != list0Count);
    LOGASSERT(NULL != list1Count);

    (*list0Count) = UTL_H264_SLICE_TYPE_I != sliceType && 0 != decodedPictureCount ? (uint32_t)decodedPictureCount : 1;
    (*list1Count) = UTL_H264_SLICE_TYPE_B == sliceType && 0 != decodedPictureCount ? (uint32_t)decodedPictureCount : 1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Preserves the encoder's nearest-earlier/list-0 and nearest-later/list-1 selection policy.
 */
uint64_t Utl_H264_DeriveReferenceListIndices(uint32_t        frameIndex,
                                             const uint32_t* frameIndices,
                                             uint64_t        pictureCount,
                                             bool_t          futureReferences,
                                             uint64_t*       referenceIndices)
{
    bool_t   selected_references[UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES];
    uint64_t reference_count;
    uint64_t picture_index;
    uint64_t selected_index;
    uint32_t distance;
    uint32_t selected_distance;

    LOGASSERT(pictureCount <= UTL_H264_MAX_DECODED_PICTURE_BUFFER_ENTRIES);
    LOGASSERT(0 == pictureCount || (NULL != frameIndices && NULL != referenceIndices));
    LOGASSERT(FALSE == futureReferences || TRUE == futureReferences);

    CRT_Memory_Set(selected_references, 0, sizeof(selected_references));

    for (reference_count = 0; reference_count < pictureCount; ++reference_count)
    {
        selected_index    = UINT64_MAX;
        selected_distance = UINT32_MAX;
        for (picture_index = 0; picture_index < pictureCount; ++picture_index)
        {
            if (selected_references[picture_index] || (futureReferences ? frameIndices[picture_index] <= frameIndex
                                                                        : frameIndices[picture_index] >= frameIndex))
            {
                continue;
            }
            distance =
                futureReferences ? frameIndices[picture_index] - frameIndex : frameIndex - frameIndices[picture_index];
            if (distance < selected_distance)
            {
                selected_index    = picture_index;
                selected_distance = distance;
            }
        }
        if (UINT64_MAX == selected_index)
        {
            break;
        }
        selected_references[selected_index] = TRUE;
        referenceIndices[reference_count]   = selected_index;
    }
    LOGASSERT(0 != reference_count);
    return reference_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint64_t Utl_H264_WriteSpsNalUnit(byte_t*                     bufferToPlaceSpsIn,
                                  uint64_t                    sizeOfSpsBuffer,
                                  const Utl_H264_SpsParams_t* params)
{
    byte_t          rbsp[UTL_H264_PARAMETER_SET_NAL_BUFFER_CAPACITY];
    H264NalHeader_t nal_header;
    uint32_t        rbsp_bit_offset;

    LOGASSERT(NULL != bufferToPlaceSpsIn);
    LOGASSERT(NULL != params);

    nal_header.nalRefIdc   = NAL_REF_IDC_HIGH;
    nal_header.nalUnitType = NAL_UNIT_TYPE_SPS;

    rbsp_bit_offset = WriteSPS(rbsp, sizeof(rbsp), params);

    return WriteNalUnit(bufferToPlaceSpsIn, sizeOfSpsBuffer, nal_header, rbsp, sizeof(rbsp), rbsp_bit_offset);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint64_t Utl_H264_WritePpsNalUnit(byte_t*                     bufferToPlacePpsIn,
                                  uint64_t                    sizeOfPpsBuffer,
                                  const Utl_H264_PpsParams_t* params)
{
    byte_t          rbsp[UTL_H264_PARAMETER_SET_NAL_BUFFER_CAPACITY];
    H264NalHeader_t nal_header;
    uint32_t        rbsp_bit_offset;

    LOGASSERT(NULL != bufferToPlacePpsIn);
    LOGASSERT(NULL != params);

    nal_header.nalRefIdc   = NAL_REF_IDC_HIGH;
    nal_header.nalUnitType = NAL_UNIT_TYPE_PPS;

    rbsp_bit_offset = WritePPS(rbsp, sizeof(rbsp), params);

    return WriteNalUnit(bufferToPlacePpsIn, sizeOfPpsBuffer, nal_header, rbsp, sizeof(rbsp), rbsp_bit_offset);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint64_t Utl_H264_WriteSeiNalUnit(byte_t*       bufferToPlaceSeiIn,
                                  uint64_t      sizeOfSeiBuffer,
                                  uint32_t      payloadType,
                                  const byte_t* pPayloadBytes,
                                  uint64_t      payloadByteCount)
{
    byte_t          rbsp[SEI_RBSP_BUFFER_CAPACITY];
    H264NalHeader_t nal_header;
    uint32_t        rbsp_bit_offset;

    LOGASSERT(NULL != bufferToPlaceSeiIn);
    LOGASSERT(0 == payloadByteCount || NULL != pPayloadBytes);

    nal_header.nalRefIdc   = NAL_REF_IDC_NONE;
    nal_header.nalUnitType = NAL_UNIT_TYPE_SEI;

    rbsp_bit_offset = WriteSEI(rbsp, sizeof(rbsp), payloadType, pPayloadBytes, payloadByteCount);

    return WriteNalUnit(bufferToPlaceSeiIn, sizeOfSeiBuffer, nal_header, rbsp, sizeof(rbsp), rbsp_bit_offset);
}
