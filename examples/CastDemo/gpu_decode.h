/* gpu_decode.h - Vulkan Video H.264 decoder built on cvk. This increment creates the decode
** device, video session and session parameters from parsed SPS/PPS; the frame decode loop (DPB,
** reference management, per-picture submission) builds on top of it. */
#ifndef CAST_GPU_DECODE_H
#define CAST_GPU_DECODE_H

#include <stddef.h>

#include "gpu_device.h"
#include "h264_params.h"

typedef struct GpuDecoder GpuDecoder;

/* Creates a Vulkan H.264 decode session on the shared device, sized for width x height and loaded
** with the given SPS/PPS. maxDpbSlots is the DPB size (max_num_ref_frames + a couple). Writes
** diagnostics to pReport. Returns NULL on failure. */
GpuDecoder* GpuDecode_Create(GpuDevice*                              pGpu,
                             const StdVideoH264SequenceParameterSet* pSps,
                             const StdVideoH264PictureParameterSet*  pPps,
                             unsigned                                width,
                             unsigned                                height,
                             unsigned                                maxDpbSlots,
                             int                                     nalLengthSize,
                             char*                                   pReport,
                             size_t                                  reportCapacity);

/* Decodes one access unit (the AVCC length-prefixed sample bytes of one frame) on the GPU. On
** success the decoded luma is available through GpuDecode_Luma. Returns 1 on success. */
int GpuDecode_DecodeFrame(GpuDecoder* pDecoder, const uint8_t* pSample, uint32_t size);

/* The most recently decoded frame's luma plane (8-bit), its row stride, and dimensions. */
const uint8_t* GpuDecode_Luma(const GpuDecoder* pDecoder, unsigned* pWidth, unsigned* pHeight, unsigned* pStride);

/* The most recently decoded frame as NV12: the returned pointer is the luma plane; ppChroma points
** to the interleaved half-resolution Cb/Cr plane. Strides and dimensions are written out. */
const uint8_t* GpuDecode_Nv12(const GpuDecoder* pDecoder, unsigned* pWidth, unsigned* pHeight, unsigned* pLumaStride, const uint8_t** ppChroma, unsigned* pChromaStride);

/* A decoded frame described by the decoder's actual output format, so the pipeline does not assume
** one pixel layout. chromaInterleaved is 1 for 2-plane Cb/Cr (NV12-style), 0 for a 3-plane planar
** layout (I420-style) where pCr is the third plane. Chroma dimensions follow the subsampling. */
typedef struct DecodedFrame
{
    const uint8_t* pLuma;
    const uint8_t* pCb;   /* interleaved Cb/Cr when chromaInterleaved, else the Cb plane */
    const uint8_t* pCr;   /* the Cr plane when planar, else NULL */
    unsigned       width;
    unsigned       height;
    unsigned       lumaStride;
    unsigned       chromaStride;
    unsigned       chromaWidth;
    unsigned       chromaHeight;
    int            chromaInterleaved;
} DecodedFrame;

/* Fills pFrame with a view of the most recently decoded frame in whatever format the decoder chose. */
void GpuDecode_Frame(const GpuDecoder* pDecoder, DecodedFrame* pFrame);

void GpuDecode_Destroy(GpuDecoder* pDecoder);

#endif
