/* gpu_encode.h - Vulkan Video H.264 encoder on cvk. Generates a low-bitrate proxy stream from
** downscaled NV12 frames: an encode session with generated SPS/PPS and rate control, a DPB for
** references, per-frame encode, and bitstream read-back. This header is the encode session and
** its generated parameter bytes; the per-frame encode builds on it. */
#ifndef CAST_GPU_ENCODE_H
#define CAST_GPU_ENCODE_H

#include <stddef.h>
#include <stdint.h>

#include "gpu_device.h"

typedef struct GpuEncoder GpuEncoder;

/* Creates an H.264 encode session on the shared device for width x height at the given keyframe
** interval and constant quantiser parameters (qpIntra/qpInter, 1..51; <=0 uses defaults). Writes
** diagnostics to pReport. Returns NULL on failure. */
GpuEncoder* GpuEncode_Create(GpuDevice* pGpu, unsigned width, unsigned height, unsigned bitrateKbps, unsigned gopLength, int qpIntra, int qpInter, unsigned frameRateNum, unsigned frameRateDen, char* pReport, size_t reportCapacity);

/* The encoded SPS + PPS as an Annex-B header (the sequence header for the muxer). Returns the
** byte length, 0 on failure; the bytes live until GpuEncode_Destroy. */
const uint8_t* GpuEncode_Header(GpuEncoder* pEncoder, uint32_t* pSize);

/* Whether the most recently encoded frame was an IDR keyframe (a GOP boundary or a forced key),
** so the muxer can mark it as a sync sample even when the caller did not request it. */
int GpuEncode_LastKeyframe(const GpuEncoder* pEncoder);

/* Encodes one downscaled frame given as a luma plane and an interleaved chroma plane (NV12-style
** source). forceIdr requests a keyframe; keyframes also occur at the GOP boundary. qpOverride
** (1..51, or <=0 for the session default) sets this frame's quantiser, which the motion-adaptive
** transcoder uses to spend fewer bits on low-motion frames. Returns a pointer to the coded
** picture's bytes (valid until the next call) and writes its length to pSize; NULL/0 on failure. */
const uint8_t* GpuEncode_Frame(GpuEncoder* pEncoder, const uint8_t* pLuma, unsigned lumaStride, const uint8_t* pChroma, unsigned chromaStride, int forceIdr, int qpOverride, uint32_t* pSize);

void GpuEncode_Destroy(GpuEncoder* pEncoder);

#endif
