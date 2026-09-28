/* h264_params.h - parses an H.264 SPS and PPS NAL unit into the Vulkan Video Std structures the
** decoder session is created from. Covers the common Baseline/Main/High cases; scaling-matrix and
** VUI bits are consumed for alignment but not stored (the decoder uses defaults). */
#ifndef CAST_H264_PARAMS_H
#define CAST_H264_PARAMS_H

#include "os/os_types.h"
#include <stdint.h>

#include <vk_video/vulkan_video_codec_h264std.h>

/* Parses one SPS NAL (without the length prefix, including the NAL header byte). Fills pSps and
** the coded/display dimensions. Returns 1 on success. */
int H264_ParseSps(const byte_t* pNal, uint32_t size, StdVideoH264SequenceParameterSet* pSps, unsigned* pWidth, unsigned* pHeight);

/* Parses one PPS NAL. Returns 1 on success. */
int H264_ParsePps(const byte_t* pNal, uint32_t size, StdVideoH264PictureParameterSet* pPps);

/* The parts of a slice header the decoder needs, plus the NAL header. */
typedef struct H264SliceHeader
{
    int      nalRefIdc;     /* 0..3 */
    int      nalUnitType;   /* 1 = non-IDR slice, 5 = IDR slice */
    int      isIdr;
    unsigned firstMbInSlice;
    unsigned sliceType;     /* 0/5 P, 1/6 B, 2/7 I, ... */
    unsigned frameNum;
    unsigned idrPicId;
    int      hasPocLsb;     /* pic_order_cnt_type 0 */
    unsigned picOrderCntLsb;
    int      deltaPicOrderCntBottom;
} H264SliceHeader;

/* Parses the header of one slice NAL using the SPS/PPS context. Returns 1 on success. */
int H264_ParseSliceHeader(const byte_t* pNal, uint32_t size, const StdVideoH264SequenceParameterSet* pSps, const StdVideoH264PictureParameterSet* pPps, H264SliceHeader* pHeader);

#endif
