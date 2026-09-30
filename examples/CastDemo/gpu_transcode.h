/* gpu_transcode.h - the GPU proxy transcoder: decodes the source and re-encodes a low-latency
** scrub proxy on the graphics device through cvk's Vulkan Video wrappers, with no external tools.
**
** This is built in stages. Stage 1 (here) is capability detection: it finds a device that can
** encode H.264 through Vulkan Video and reports what it supports, which is the prerequisite for
** the decode -> scale -> encode -> mux pipeline that follows. Audio is not a Vulkan concern and is
** handled alongside on its own path. */
#ifndef CAST_GPU_TRANSCODE_H
#define CAST_GPU_TRANSCODE_H

#include "os/os_types.h"
#include "gpu_device.h"
#include "os/os_atomic.h"
#include <stddef.h>
#include <stdint.h>
#include <tchar.h>

/* The full GPU proxy transcoder: an MP4 source decoded on the GPU, box-downscaled to outWidth (its
** height derived from the source aspect ratio), and re-encoded as a low-bitrate H.264 proxy. */
typedef struct GpuTranscoder GpuTranscoder;

/* Tunable transcode/remux settings, exposed by the GUI sliders for live quality testing. */
typedef struct GpuTranscodeConfig
{
    uint32_t width;          /* proxy width in pixels; height follows the source aspect */
    uint32_t bitrateKbps;    /* informational target (constant-QP encode; reserved for rate control) */
    uint32_t gopLength;      /* maximum frames between keyframes */
    int32_t  qpIntra;        /* keyframe quantiser, 1..51 (lower = higher quality) */
    int32_t  qpInter;        /* inter-frame quantiser, 1..51 */
    int32_t  motionAdaptive; /* 1: use optical-flow motion to raise QP on still frames and cut on scene changes */
    int32_t  motionStrength; /* 0..100: how aggressively low motion raises QP to save bits */
} GpuTranscodeConfig;

/* Called once the fragmented proxy has its first playable fragment on disk, so the caller can begin
** serving it while the rest of the file keeps transcoding in the background. */
typedef void (*GpuTranscodeReadyFn)(void* pUser);

/* Fills pConfig with sensible proxy defaults (480 wide, QP 24/28, GOP 30, motion adaptation on). */
void GpuTranscode_InitializeConfig(GpuTranscodeConfig* pConfig);

/* Opens the source and creates the GPU decode and encode sessions for the given settings. Returns
** NULL on failure with a reason in pReport. */
GpuTranscoder* GpuTranscode_Open(GpuDevice* deviceStorage, const TCHAR* pFile, const GpuTranscodeConfig* pConfig, char* pReport, size_t reportCapacity);

/* The average inter-frame motion of the most recently produced frame (0..1), from the optical-flow
** estimator. Meaningful only when motionAdaptive is set. */
double GpuTranscode_GetLastMotion(const GpuTranscoder* pTranscoder);

/* The encoded proxy SPS + PPS (Annex-B sequence header). Bytes live until GpuTranscode_Close. */
const byte_t* GpuTranscode_GetHeader(GpuTranscoder* pTranscoder, uint32_t* pSize);

unsigned GpuTranscode_GetOutputWidth(const GpuTranscoder* pTranscoder);

unsigned GpuTranscode_GetOutputHeight(const GpuTranscoder* pTranscoder);

unsigned GpuTranscode_GetFrameCount(const GpuTranscoder* pTranscoder);

/* Advances the decoder to the keyframe at or before approxFrame so a preview starts on an IDR. */
unsigned GpuTranscode_SeekKeyframe(GpuTranscoder* pTranscoder, unsigned approxFrame);

/* Decodes, downscales and re-encodes the next source frame. Returns the coded picture's bytes
** (valid until the next call) and its length in pSize; sets *pKeyframe when it is a keyframe.
** Returns NULL with *pSize 0 at end of stream or on failure. */
const byte_t* GpuTranscode_ReadNextFrame(GpuTranscoder* pTranscoder, uint32_t* pSize, int* pKeyframe);

void GpuTranscode_Close(GpuTranscoder* pTranscoder);

/* One call: transcode pSource into a proxy MP4 at pDest (GPU H.264 video re-encode plus AAC audio
** passthrough, interleaved and timed from the source). If pCancel is non-NULL and becomes non-zero
** the transcode stops early. Writes diagnostics to pReport. Returns 1 on success. */
int GpuTranscode_WriteMp4(GpuDevice*                deviceStorage,
                          const TCHAR*              pSource,
                          const TCHAR*              pDest,
                          const GpuTranscodeConfig* pConfig,
                          unsigned                  startFrame,
                          unsigned                  maxFrames,
                          volatile long*            pCancel,
                          char*                     pReport,
                          size_t                    reportCapacity);

/* Transcodes the whole source into a fragmented (progressive) MP4 at pDest: an init segment plus a
** movie fragment per group of pictures, flushed to disk as produced. onReady(pUser) fires after the
** first fragment so playback can start within a fraction of a second instead of after the full
** transcode. Paths are terminated native-order UTF-16; an optional cancellation word is
** read through OS_Atomic and must remain initialized/alive through return. Returns 1 on success. */
int GpuTranscode_WriteFragmentedMp4(GpuDevice*                deviceStorage,
                                    const uint16_t*           pSource,
                                    const uint16_t*           pDest,
                                    const GpuTranscodeConfig* pConfig,
                                    uint32_t                  maxFrames,
                                    GpuTranscodeReadyFn       onReady,
                                    void*                     pUser,
                                    const OS_Atomic32_t*      pCancel,
                                    char*                     pReport,
                                    uint32_t                  reportCapacity);

#endif
