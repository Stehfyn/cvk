/* gpu_transcode.h - the GPU proxy transcoder: decodes the source and re-encodes a low-latency
** scrub proxy on the graphics device through cvk's Vulkan Video wrappers, with no external tools.
**
** This is built in stages. Stage 1 (here) is capability detection: it finds a device that can
** encode H.264 through Vulkan Video and reports what it supports, which is the prerequisite for
** the decode -> scale -> encode -> mux pipeline that follows. Audio is not a Vulkan concern and is
** handled alongside on its own path. */
#ifndef CAST_GPU_TRANSCODE_H
#define CAST_GPU_TRANSCODE_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef struct GpuTranscodeCaps
{
    int      available;     /* the device can encode H.264 through Vulkan Video */
    int      canDecodeH264; /* the device can also decode H.264 (full GPU transcode) */
    char     deviceName[256];
    unsigned encodeQueueFamily;
    unsigned decodeQueueFamily;
    unsigned minCodedWidth, minCodedHeight;
    unsigned maxCodedWidth, maxCodedHeight;
    unsigned maxDpbSlots;
    unsigned maxActiveReferences;
} GpuTranscodeCaps;

/* Detects Vulkan Video H.264 encode (and decode) support on the graphics adapter and fills pCaps.
** Writes a human-readable summary to pReport. Returns 1 when H.264 encode is available. */
int GpuTranscode_Probe(GpuTranscodeCaps* pCaps, char* pReport, size_t reportCapacity);

/* The full GPU proxy transcoder: an MP4 source decoded on the GPU, box-downscaled to outWidth (its
** height derived from the source aspect ratio), and re-encoded as a low-bitrate H.264 proxy. */
typedef struct GpuTranscoder GpuTranscoder;

/* Tunable transcode/remux settings, exposed by the GUI sliders for live quality testing. */
typedef struct GpuTranscodeConfig
{
    unsigned width;          /* proxy width in pixels; height follows the source aspect */
    unsigned bitrateKbps;    /* informational target (constant-QP encode; reserved for rate control) */
    unsigned gopLength;      /* maximum frames between keyframes */
    int      qpIntra;        /* keyframe quantiser, 1..51 (lower = higher quality) */
    int      qpInter;        /* inter-frame quantiser, 1..51 */
    int      motionAdaptive; /* 1: use optical-flow motion to raise QP on still frames and cut on scene changes */
    int      motionStrength; /* 0..100: how aggressively low motion raises QP to save bits */
} GpuTranscodeConfig;

/* Fills pConfig with sensible proxy defaults (480 wide, QP 24/28, GOP 30, motion adaptation on). */
void GpuTranscode_DefaultConfig(GpuTranscodeConfig* pConfig);

/* Opens the source and creates the GPU decode and encode sessions for the given settings. Returns
** NULL on failure with a reason in pReport. */
GpuTranscoder* GpuTranscode_Open(const wchar_t* pFile, const GpuTranscodeConfig* pConfig, char* pReport, size_t reportCapacity);

/* The average inter-frame motion of the most recently produced frame (0..1), from the optical-flow
** estimator. Meaningful only when motionAdaptive is set. */
double GpuTranscode_LastMotion(const GpuTranscoder* pTranscoder);

/* Advances the decoder to the keyframe at or before approxFrame so a preview starts on an IDR. */
unsigned GpuTranscode_SeekKeyframe(GpuTranscoder* pTranscoder, unsigned approxFrame);

/* The encoded proxy SPS + PPS (Annex-B sequence header). Bytes live until GpuTranscode_Close. */
const uint8_t* GpuTranscode_Header(GpuTranscoder* pTranscoder, uint32_t* pSize);

/* Decodes, downscales and re-encodes the next source frame. Returns the coded picture's bytes
** (valid until the next call) and its length in pSize; sets *pKeyframe when it is a keyframe.
** Returns NULL with *pSize 0 at end of stream or on failure. */
const uint8_t* GpuTranscode_NextFrame(GpuTranscoder* pTranscoder, uint32_t* pSize, int* pKeyframe);

unsigned GpuTranscode_OutWidth(const GpuTranscoder* pTranscoder);
unsigned GpuTranscode_OutHeight(const GpuTranscoder* pTranscoder);
unsigned GpuTranscode_FrameCount(const GpuTranscoder* pTranscoder);

void GpuTranscode_Close(GpuTranscoder* pTranscoder);

/* One call: transcode pSource into a proxy MP4 at pDest (GPU H.264 video re-encode plus AAC audio
** passthrough, interleaved and timed from the source). If pCancel is non-NULL and becomes non-zero
** the transcode stops early. Writes diagnostics to pReport. Returns 1 on success. */
int GpuTranscode_ToMp4(const wchar_t* pSource, const wchar_t* pDest, const GpuTranscodeConfig* pConfig, unsigned startFrame, unsigned maxFrames, volatile long* pCancel, char* pReport, size_t reportCapacity);

/* Called once the fragmented proxy has its first playable fragment on disk, so the caller can begin
** serving it while the rest of the file keeps transcoding in the background. */
typedef void (*GpuTranscodeReadyFn)(void* pUser);

/* Transcodes the whole source into a fragmented (progressive) MP4 at pDest: an init segment plus a
** movie fragment per group of pictures, flushed to disk as produced. onReady(pUser) fires after the
** first fragment so playback can start within a fraction of a second instead of after the full
** transcode. Returns 1 on success. */
int GpuTranscode_ToFragmentedMp4(const wchar_t* pSource, const wchar_t* pDest, const GpuTranscodeConfig* pConfig, unsigned maxFrames, GpuTranscodeReadyFn onReady, void* pUser, volatile long* pCancel, char* pReport, size_t reportCapacity);

#endif
