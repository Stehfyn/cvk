/* h264_params.c - Exp-Golomb SPS/PPS parser to Vulkan Std H.264 structures. */
#include <stdint.h>
#include <vk_video/vulkan_video_codec_h264std.h>

#include "crt/crt_memory.h"
#include "h264_params.h"
#include "os/os_types.h"
#include "os/os_compiler.h"

#define MAX_PARAM_SET 4096U
#define H264_BITS_PER_BYTE 8
#define H264_LAST_BIT_IN_BYTE 7
#define H264_EXP_GOLOMB_MAX_PREFIX_BITS 32
#define H264_INITIAL_SCALING_FACTOR 8
#define H264_SCALING_FACTOR_MODULUS 256
#define H264_NAL_TYPE_MASK 0x1F
#define H264_NAL_TYPE_SPS 7
#define H264_NAL_TYPE_PPS 8
#define H264_NAL_TYPE_NON_IDR_SLICE 1
#define H264_NAL_TYPE_IDR_SLICE 5
#define H264_NAL_REF_IDC_SHIFT 5
#define H264_NAL_REF_IDC_MASK 0x03
#define H264_MACROBLOCK_EDGE 16
#define H264_SCALING_LIST_4X4_COUNT 6
#define H264_SCALING_LIST_4X4_SIZE 16
#define H264_SCALING_LIST_8X8_SIZE 64
#define H264_HIGH_PROFILE_SCALING_LIST_COUNT 12
#define H264_LEVEL_CODE_1_0 10
#define H264_LEVEL_CODE_1_1 11
#define H264_LEVEL_CODE_1_2 12
#define H264_LEVEL_CODE_1_3 13
#define H264_LEVEL_CODE_2_0 20
#define H264_LEVEL_CODE_2_1 21
#define H264_LEVEL_CODE_2_2 22
#define H264_LEVEL_CODE_3_0 30
#define H264_LEVEL_CODE_3_1 31
#define H264_LEVEL_CODE_3_2 32
#define H264_LEVEL_CODE_4_0 40
#define H264_LEVEL_CODE_4_1 41
#define H264_LEVEL_CODE_4_2 42
#define H264_LEVEL_CODE_5_0 50
#define H264_LEVEL_CODE_5_1 51
#define H264_LEVEL_CODE_5_2 52
#define H264_LEVEL_CODE_6_0 60
#define H264_LEVEL_CODE_6_1 61
#define H264_LEVEL_CODE_6_2 62
#define H264_PROFILE_CODE_44 44
#define H264_PROFILE_CODE_83 83
#define H264_PROFILE_CODE_86 86
#define H264_PROFILE_CODE_118 118
#define H264_PROFILE_CODE_128 128
#define H264_PROFILE_CODE_134 134
#define H264_PROFILE_CODE_135 135
#define H264_PROFILE_CODE_138 138
#define H264_PROFILE_CODE_139 139

typedef struct BitReader
{
    const byte_t* pData;
    uint32_t      size;
    uint32_t      bytePos;
    int           bitPos;  /* 0..7, MSB first */
    int           zeroRun; /* trailing zero bytes, for emulation-prevention removal */
} BitReader;

/*************************************************************************************************/
/**
 * \par Details:
 *  Borrows the NAL payload and resets byte, bit and emulation-prevention state.
 */
static FORCE_INLINE void BitReader_Init(BitReader* pReader, const byte_t* pData, uint32_t size)
{
    pReader->pData   = pData;
    pReader->size    = size;
    pReader->bytePos = 0;
    pReader->bitPos  = 0;
    pReader->zeroRun = 0;
}

/* Reads one bit, transparently skipping emulation-prevention 0x03 bytes after two 0x00 bytes. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Advances most-significant bit first and skips an emulation-prevention 0x03
 *  after two zero bytes. Exhausted input currently produces zero.
 */
static FORCE_INLINE uint32_t ReadBit(BitReader* pReader)
{
    uint8_t  byte;
    uint32_t bit;

    if (pReader->bytePos >= pReader->size)
    {
        return 0;
    }
    byte = pReader->pData[pReader->bytePos];
    bit  = (byte >> (H264_LAST_BIT_IN_BYTE - pReader->bitPos)) & 1U;
    if (++pReader->bitPos == H264_BITS_PER_BYTE)
    {
        pReader->bitPos = 0;
        /* Track the RBSP emulation-prevention sequence 00 00 03. */
        if (0 == byte)
        {
            ++pReader->zeroRun;
        }
        else
        {
            pReader->zeroRun = 0;
        }
        ++pReader->bytePos;
        if (pReader->zeroRun >= 2 && pReader->bytePos < pReader->size && pReader->pData[pReader->bytePos] == 0x03)
        {
            ++pReader->bytePos; /* skip the inserted 0x03 */
            pReader->zeroRun = 0;
        }
    }
    return bit;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Accumulates the requested bit count by repeated ReadBit calls.
 */
static FORCE_INLINE uint32_t ReadBits(BitReader* pReader, int count)
{
    uint32_t value = 0;
    int i;

    for (i = 0; i < count; ++i)
    {
        value = (value << 1) | ReadBit(pReader);
    }
    return value;
}

/* ue(v): unsigned Exp-Golomb. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Counts leading zero bits and combines the Exp-Golomb prefix with its suffix.
 */
static uint32_t ReadUe(BitReader* pReader)
{
    int      leadingZeros;
    uint32_t bit;

    leadingZeros = 0;
    for (;;)
    {
        if (pReader->bytePos >= pReader->size)
        {
            break;
        }
        bit = ReadBit(pReader);
        if (bit || (H264_EXP_GOLOMB_MAX_PREFIX_BITS == leadingZeros))
        {
            break;
        }
        ++leadingZeros;
    }
    if (H264_EXP_GOLOMB_MAX_PREFIX_BITS == leadingZeros)
    {
        return 0;
    }
    if (0 == leadingZeros)
    {
        return 0;
    }
    return ((1U << leadingZeros) - 1U) + ReadBits(pReader, leadingZeros);
}

/* se(v): signed Exp-Golomb. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Maps unsigned Exp-Golomb code numbers to alternating positive and negative
 *  signed values.
 */
static FORCE_INLINE int32_t ReadSe(BitReader* pReader)
{
    const uint32_t code = ReadUe(pReader);

    if (0 == code)
    {
        return 0;
    }
    return (code & 1U) ? (int32_t)((code + 1U) / 2U) : -(int32_t)(code / 2U);
}

/* Consumes a scaling list of the given size (values are not stored). */
/*************************************************************************************************/
/**
 * \par Details:
 *  Consumes signed scaling deltas and updates the rolling scale without storing
 *  the resulting matrix.
 */
static FORCE_INLINE void SkipScalingList(BitReader* pReader, int count)
{
    int32_t lastScale = H264_INITIAL_SCALING_FACTOR;
    int32_t nextScale = H264_INITIAL_SCALING_FACTOR;
    int j;

    for (j = 0; j < count; ++j)
    {
        if (0 != nextScale)
        {
            const int32_t delta = ReadSe(pReader);

            nextScale = (lastScale + delta + H264_SCALING_FACTOR_MODULUS) % H264_SCALING_FACTOR_MODULUS;
        }
        lastScale = (0 == nextScale) ? lastScale : nextScale;
    }
}

static void SkipScalingLists(BitReader* pReader, int count)
{
    int i;

    for (i = 0; i < count; ++i)
    {
        if (ReadBit(pReader))
        {
            SkipScalingList(pReader, i < H264_SCALING_LIST_4X4_COUNT ? H264_SCALING_LIST_4X4_SIZE : H264_SCALING_LIST_8X8_SIZE);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Maps recognized H.264 level codes to the Vulkan video enum; the existing
 *  fallback is level 4.1.
 */
static FORCE_INLINE StdVideoH264LevelIdc MapLevel(uint32_t levelIdc)
{
    switch (levelIdc)
    {
    case H264_LEVEL_CODE_1_0:
        return STD_VIDEO_H264_LEVEL_IDC_1_0;
    case H264_LEVEL_CODE_1_1:
        return STD_VIDEO_H264_LEVEL_IDC_1_1;
    case H264_LEVEL_CODE_1_2:
        return STD_VIDEO_H264_LEVEL_IDC_1_2;
    case H264_LEVEL_CODE_1_3:
        return STD_VIDEO_H264_LEVEL_IDC_1_3;
    case H264_LEVEL_CODE_2_0:
        return STD_VIDEO_H264_LEVEL_IDC_2_0;
    case H264_LEVEL_CODE_2_1:
        return STD_VIDEO_H264_LEVEL_IDC_2_1;
    case H264_LEVEL_CODE_2_2:
        return STD_VIDEO_H264_LEVEL_IDC_2_2;
    case H264_LEVEL_CODE_3_0:
        return STD_VIDEO_H264_LEVEL_IDC_3_0;
    case H264_LEVEL_CODE_3_1:
        return STD_VIDEO_H264_LEVEL_IDC_3_1;
    case H264_LEVEL_CODE_3_2:
        return STD_VIDEO_H264_LEVEL_IDC_3_2;
    case H264_LEVEL_CODE_4_0:
        return STD_VIDEO_H264_LEVEL_IDC_4_0;
    case H264_LEVEL_CODE_4_1:
        return STD_VIDEO_H264_LEVEL_IDC_4_1;
    case H264_LEVEL_CODE_4_2:
        return STD_VIDEO_H264_LEVEL_IDC_4_2;
    case H264_LEVEL_CODE_5_0:
        return STD_VIDEO_H264_LEVEL_IDC_5_0;
    case H264_LEVEL_CODE_5_1:
        return STD_VIDEO_H264_LEVEL_IDC_5_1;
    case H264_LEVEL_CODE_5_2:
        return STD_VIDEO_H264_LEVEL_IDC_5_2;
    case H264_LEVEL_CODE_6_0:
        return STD_VIDEO_H264_LEVEL_IDC_6_0;
    case H264_LEVEL_CODE_6_1:
        return STD_VIDEO_H264_LEVEL_IDC_6_1;
    case H264_LEVEL_CODE_6_2:
        return STD_VIDEO_H264_LEVEL_IDC_6_2;
    default:
        return STD_VIDEO_H264_LEVEL_IDC_4_1;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Reads SPS fields used by the decoder and derives cropped display dimensions.
 *  VUI data and reference-cycle offsets are not retained by this implementation.
 */
static FORCE_INLINE int HasHighProfileSyntax(uint32_t profileIdc)
{
    return profileIdc == STD_VIDEO_H264_PROFILE_IDC_HIGH || profileIdc == STD_VIDEO_H264_PROFILE_IDC_HIGH_10 ||
           profileIdc == STD_VIDEO_H264_PROFILE_IDC_HIGH_422 || profileIdc == STD_VIDEO_H264_PROFILE_IDC_HIGH_444_PREDICTIVE ||
           profileIdc == H264_PROFILE_CODE_44 || profileIdc == H264_PROFILE_CODE_83 || profileIdc == H264_PROFILE_CODE_86 ||
           profileIdc == H264_PROFILE_CODE_118 || profileIdc == H264_PROFILE_CODE_128 || profileIdc == H264_PROFILE_CODE_138 ||
           profileIdc == H264_PROFILE_CODE_139 || profileIdc == H264_PROFILE_CODE_134 || profileIdc == H264_PROFILE_CODE_135;
}

static FORCE_INLINE void ParseSpsHighProfile(BitReader* pReader, StdVideoH264SequenceParameterSet* pSps)
{
    const uint32_t chroma = ReadUe(pReader);

    pSps->chroma_format_idc = (StdVideoH264ChromaFormatIdc)chroma;
    if (3 == chroma)
    {
        pSps->flags.separate_colour_plane_flag = ReadBit(pReader);
    }
    pSps->bit_depth_luma_minus8                      = (uint8_t)ReadUe(pReader);
    pSps->bit_depth_chroma_minus8                    = (uint8_t)ReadUe(pReader);
    pSps->flags.qpprime_y_zero_transform_bypass_flag = ReadBit(pReader);
    if (ReadBit(pReader))
    {
        int count = (3 == chroma) ? H264_HIGH_PROFILE_SCALING_LIST_COUNT : H264_BITS_PER_BYTE;

        pSps->flags.seq_scaling_matrix_present_flag = 1;
        SkipScalingLists(pReader, count);
    }
}

int H264_ParseSps(const byte_t*                    pNal,
                  uint32_t                          size,
                  StdVideoH264SequenceParameterSet* pSps,
                  unsigned*                         pWidth,
                  unsigned*                         pHeight)
{
    BitReader reader;
    uint32_t  profileIdc;
    uint32_t  levelIdc;
    uint32_t  cropUnitX = 1;
    uint32_t  cropUnitY = 1;
    unsigned  width;
    unsigned  height;

    if (size < 2 || (pNal[0] & H264_NAL_TYPE_MASK) != H264_NAL_TYPE_SPS)
    {
        return 0; /* not an SPS NAL */
    }
    CRT_Memory_Set(pSps, 0, sizeof(*pSps));
    BitReader_Init(&reader, pNal + 1, size - 1); /* skip the 1-byte NAL header */

    profileIdc                       = ReadBits(&reader, H264_BITS_PER_BYTE);
    pSps->flags.constraint_set0_flag = ReadBit(&reader);
    pSps->flags.constraint_set1_flag = ReadBit(&reader);
    pSps->flags.constraint_set2_flag = ReadBit(&reader);
    pSps->flags.constraint_set3_flag = ReadBit(&reader);
    pSps->flags.constraint_set4_flag = ReadBit(&reader);
    pSps->flags.constraint_set5_flag = ReadBit(&reader);
    (void)ReadBits(&reader, 2); /* reserved_zero_2bits */
    levelIdc                   = ReadBits(&reader, H264_BITS_PER_BYTE);
    pSps->profile_idc          = (StdVideoH264ProfileIdc)profileIdc;
    pSps->level_idc            = MapLevel(levelIdc);
    pSps->seq_parameter_set_id = (uint8_t)ReadUe(&reader);
    pSps->chroma_format_idc    = STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;

    if (HasHighProfileSyntax(profileIdc))
    {
        ParseSpsHighProfile(&reader, pSps);
    }

    pSps->log2_max_frame_num_minus4 = (uint8_t)ReadUe(&reader);
    {
        uint32_t pocType = ReadUe(&reader);
        pSps->pic_order_cnt_type = (StdVideoH264PocType)pocType;
    }
    if (0 == pSps->pic_order_cnt_type)
    {
        pSps->log2_max_pic_order_cnt_lsb_minus4 = (uint8_t)ReadUe(&reader);
    }
    else if (1 == pSps->pic_order_cnt_type)
    {
        uint32_t count;
        uint32_t i;

        pSps->flags.delta_pic_order_always_zero_flag = ReadBit(&reader);
        pSps->offset_for_non_ref_pic                 = ReadSe(&reader);
        pSps->offset_for_top_to_bottom_field         = ReadSe(&reader);
        count                                        = ReadUe(&reader);
        pSps->num_ref_frames_in_pic_order_cnt_cycle  = (uint8_t)count;
        for (i = 0; i < count; ++i)
        {
            (void)ReadSe(&reader); /* offset_for_ref_frame[i]; not retained */
        }
    }
    pSps->max_num_ref_frames                         = (uint8_t)ReadUe(&reader);
    pSps->flags.gaps_in_frame_num_value_allowed_flag = ReadBit(&reader);
    pSps->pic_width_in_mbs_minus1                    = ReadUe(&reader);
    pSps->pic_height_in_map_units_minus1             = ReadUe(&reader);
    pSps->flags.frame_mbs_only_flag                  = ReadBit(&reader);
    if (!pSps->flags.frame_mbs_only_flag)
    {
        pSps->flags.mb_adaptive_frame_field_flag = ReadBit(&reader);
    }
    pSps->flags.direct_8x8_inference_flag = ReadBit(&reader);
    if (ReadBit(&reader)) /* frame_cropping_flag */
    {
        pSps->flags.frame_cropping_flag = 1;
        pSps->frame_crop_left_offset    = ReadUe(&reader);
        pSps->frame_crop_right_offset   = ReadUe(&reader);
        pSps->frame_crop_top_offset     = ReadUe(&reader);
        pSps->frame_crop_bottom_offset  = ReadUe(&reader);
    }
    /* vui_parameters_present_flag and the VUI itself are not needed by the decoder session. */

    width  = (pSps->pic_width_in_mbs_minus1 + 1) * H264_MACROBLOCK_EDGE;
    height = (pSps->pic_height_in_map_units_minus1 + 1) * H264_MACROBLOCK_EDGE * (pSps->flags.frame_mbs_only_flag ? 1U : 2U);
    if (pSps->flags.frame_cropping_flag)
    {
        if (1 == pSps->chroma_format_idc)
        {
            cropUnitX = 2;
            cropUnitY = pSps->flags.frame_mbs_only_flag ? 2U : 4U;
        }
        else if (2 == pSps->chroma_format_idc)
        {
            cropUnitX = 2;
            cropUnitY = pSps->flags.frame_mbs_only_flag ? 1U : 2U;
        }
        else
        {
            cropUnitY = pSps->flags.frame_mbs_only_flag ? 1U : 2U;
        }
        width  -= (pSps->frame_crop_left_offset + pSps->frame_crop_right_offset) * cropUnitX;
        height -= (pSps->frame_crop_top_offset + pSps->frame_crop_bottom_offset) * cropUnitY;
    }
    *pWidth  = width;
    *pHeight = height;
    return width > 0 && height > 0;
}

/* The index of the last set bit (the rbsp_stop_one_bit) in a byte range, so that more_rbsp_data
** can distinguish payload bits from the trailing stop bit and padding. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Searches backward for the last set bit so PPS parsing can distinguish payload
 *  from the RBSP stop bit and padding.
 */
static FORCE_INLINE int GetRbspLastBit(const byte_t* pData, uint32_t size)
{
    int byteIndex;

    for (byteIndex = (int)size - 1; byteIndex >= 0; --byteIndex)
    {
        if (pData[byteIndex] != 0)
        {
            int bit;

            for (bit = 0; bit < H264_BITS_PER_BYTE; ++bit)
            {
                if (pData[byteIndex] & (1U << bit))
                {
                    return (byteIndex * H264_BITS_PER_BYTE) + (H264_LAST_BIT_IN_BYTE - bit);
                }
            }
        }
    }
    return -1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Removes emulation-prevention bytes before locating trailing bits, rejects FMO
 *  slice groups and reads the optional transform/chroma-QP extension.
 */
int H264_ParsePps(const byte_t* pNal, uint32_t size, StdVideoH264PictureParameterSet* pPps)
{
    byte_t rbsp[MAX_PARAM_SET];
    uint32_t  rbspSize = 0;
    BitReader reader;
    int       stopBit;
    uint32_t  numSliceGroups;
    uint32_t  in;

    if (size < 2 || (pNal[0] & H264_NAL_TYPE_MASK) != H264_NAL_TYPE_PPS || size > MAX_PARAM_SET + 1)
    {
        return 0; /* not a PPS NAL, or too large */
    }
    CRT_Memory_Set(pPps, 0, sizeof(*pPps));

    /* Strip emulation-prevention bytes so more_rbsp_data can be computed by bit position. */
    for (in = 1; in < size; ++in)
    {
        if (in + 2 < size && pNal[in] == 0 && pNal[in + 1] == 0 && pNal[in + 2] == 3)
        {
            rbsp[rbspSize++]  = 0;
            rbsp[rbspSize++]  = 0;
            in               += 2; /* skip the inserted 0x03 */
        }
        else
        {
            rbsp[rbspSize++] = pNal[in];
        }
    }
    stopBit = GetRbspLastBit(rbsp, rbspSize);
    BitReader_Init(&reader, rbsp, rbspSize);

    pPps->pic_parameter_set_id                               = (uint8_t)ReadUe(&reader);
    pPps->seq_parameter_set_id                               = (uint8_t)ReadUe(&reader);
    pPps->flags.entropy_coding_mode_flag                     = ReadBit(&reader);
    pPps->flags.bottom_field_pic_order_in_frame_present_flag = ReadBit(&reader);
    numSliceGroups                                           = ReadUe(&reader);
    if (numSliceGroups > 0)
    {
        return 0; /* FMO slice groups are not supported by this transcoder */
    }
    pPps->num_ref_idx_l0_default_active_minus1         = (uint8_t)ReadUe(&reader);
    pPps->num_ref_idx_l1_default_active_minus1         = (uint8_t)ReadUe(&reader);
    pPps->flags.weighted_pred_flag                     = ReadBit(&reader);
    {
        uint32_t weightedBipredIdc = ReadBits(&reader, 2);
        pPps->weighted_bipred_idc = (StdVideoH264WeightedBipredIdc)weightedBipredIdc;
    }
    pPps->pic_init_qp_minus26                          = (int8_t)ReadSe(&reader);
    pPps->pic_init_qs_minus26                          = (int8_t)ReadSe(&reader);
    pPps->chroma_qp_index_offset                       = (int8_t)ReadSe(&reader);
    pPps->flags.deblocking_filter_control_present_flag = ReadBit(&reader);
    pPps->flags.constrained_intra_pred_flag            = ReadBit(&reader);
    pPps->flags.redundant_pic_cnt_present_flag         = ReadBit(&reader);
    pPps->second_chroma_qp_index_offset                = pPps->chroma_qp_index_offset;

    /* more_rbsp_data(): payload remains while the cursor is before the stop bit. High-profile
    ** streams carry the 8x8-transform flag and a second chroma QP offset here, and decoding is
    ** wrong without them. */
    if (((int)(reader.bytePos * H264_BITS_PER_BYTE) + reader.bitPos) < stopBit)
    {
        pPps->flags.transform_8x8_mode_flag = ReadBit(&reader);
        if (ReadBit(&reader)) /* pic_scaling_matrix_present_flag */
        {
            int count = H264_SCALING_LIST_4X4_COUNT + (pPps->flags.transform_8x8_mode_flag ? H264_SCALING_LIST_4X4_COUNT : 2);

            pPps->flags.pic_scaling_matrix_present_flag = 1;
            SkipScalingLists(&reader, count);
        }
        pPps->second_chroma_qp_index_offset = (int8_t)ReadSe(&reader);
    }
    return 1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Reads the coded-slice prefix required for frame and POC tracking using SPS/PPS
 *  bit widths. This does not parse the complete slice header.
 */
extern FORCE_INLINE int H264_ParseSliceHeader(const byte_t*                          pNal,
                          uint32_t                                size,
                          const StdVideoH264SequenceParameterSet* pSps,
                          const StdVideoH264PictureParameterSet*  pPps,
                          H264SliceHeader*                        pHeader)
{
    BitReader reader;

    if (size < 2)
    {
        return 0;
    }
    CRT_Memory_Set(pHeader, 0, sizeof(*pHeader));
    pHeader->nalRefIdc   = (pNal[0] >> H264_NAL_REF_IDC_SHIFT) & H264_NAL_REF_IDC_MASK;
    pHeader->nalUnitType = pNal[0] & H264_NAL_TYPE_MASK;
    if (pHeader->nalUnitType != H264_NAL_TYPE_NON_IDR_SLICE && pHeader->nalUnitType != H264_NAL_TYPE_IDR_SLICE)
    {
        return 0; /* not a coded slice */
    }
    pHeader->isIdr = (H264_NAL_TYPE_IDR_SLICE == pHeader->nalUnitType);
    BitReader_Init(&reader, pNal + 1, size - 1);

    pHeader->firstMbInSlice = ReadUe(&reader);
    pHeader->sliceType      = ReadUe(&reader);
    (void)ReadUe(&reader); /* pic_parameter_set_id */
    if (pSps->flags.separate_colour_plane_flag)
    {
        (void)ReadBits(&reader, 2); /* colour_plane_id */
    }
    pHeader->frameNum = ReadBits(&reader, pSps->log2_max_frame_num_minus4 + 4);
    if (!pSps->flags.frame_mbs_only_flag)
    {
        if (ReadBit(&reader)) /* field_pic_flag */
        {
            (void)ReadBit(&reader); /* bottom_field_flag; fields are not handled beyond parsing */
        }
    }
    if (pHeader->isIdr)
    {
        pHeader->idrPicId = ReadUe(&reader);
    }
    if (0 == pSps->pic_order_cnt_type)
    {
        pHeader->hasPocLsb      = 1;
        pHeader->picOrderCntLsb = ReadBits(&reader, pSps->log2_max_pic_order_cnt_lsb_minus4 + 4);
        if (pPps->flags.bottom_field_pic_order_in_frame_present_flag)
        {
            pHeader->deltaPicOrderCntBottom = ReadSe(&reader);
        }
    }
    return 1;
}
