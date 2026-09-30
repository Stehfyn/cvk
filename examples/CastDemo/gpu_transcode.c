/* gpu_transcode.c - Stage 1: Vulkan Video H.264 encode/decode capability detection through cvk. */
#define WIN32_LEAN_AND_MEAN
#include <Windows.h> // IWYU pragma: keep
#include <stdint.h>
#include <minwindef.h>
#include <winnt.h>
#include <synchapi.h>
#include <WinBase.h>
#include <vk_video/vulkan_video_codec_h264std.h>
#include <processthreadsapi.h>
#include <handleapi.h>

#include <stdarg.h>
#include <string.h>

#include <volk.h>

#include "gpu_transcode.h"
#include "crt/crt_format.h"
#include "crt/crt_memory.h"
#include "os/os_atomic.h"
#include "os/os_compiler.h"
#include "os/os_types.h"

/* One appended line of the human-readable report. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Appends formatted diagnostic text within the unused portion of the report buffer.
 */
static void Report(char* pReport, size_t capacity, const char* pFormat, ...)
{
    va_list arguments;

    if ((NULL == pReport) || (capacity > UINT32_MAX))
    {
        return;
    }
    va_start(arguments, pFormat);
    (void)CRT_Format_VAppend(pReport, (uint32_t)capacity, pFormat, &arguments);
    va_end(arguments);
}

/* ---- The full decode -> downscale -> encode proxy pipeline. ---------------------------------- */

#include "mp4_demux.h"
#include "h264_params.h"
#include "gpu_device.h"
#include "gpu_decode.h"
#include "gpu_encode.h"

#define PIPELINE_SLOTS 3
#define H264_NAL_TYPE_MASK              (0x1FU)
#define H264_NAL_TYPE_SPS               (7U)
#define H264_NAL_TYPE_PPS               (8U)
#define GPU_TRANSCODE_DEFAULT_WIDTH     (1280U)
#define GPU_TRANSCODE_DEFAULT_BITRATE   (4000U)
#define GPU_TRANSCODE_DEFAULT_GOP       (12U)
#define GPU_TRANSCODE_DEFAULT_QP_INTRA  (20)
#define GPU_TRANSCODE_DEFAULT_QP_INTER  (23)
#define GPU_TRANSCODE_DEFAULT_MOTION    (30)
#define GPU_TRANSCODE_REPORT_CAPACITY   (1600U)
#define GPU_TRANSCODE_DEVICE_REPORT_CAPACITY (256U)
#define GPU_TRANSCODE_CODEC_REPORT_CAPACITY  (512U)
#define GPU_TRANSCODE_MIN_WIDTH         (16U)
#define GPU_TRANSCODE_MAX_QP            (50)
#define GPU_TRANSCODE_HEAD_START        (8U)
#define GPU_TRANSCODE_SCENE_RESIDUAL    (0.18)
#define GPU_TRANSCODE_SCENE_MOTION      (0.35)
#define GPU_TRANSCODE_MOTION_QP_RANGE   (8.0)
#define GPU_TRANSCODE_ROUND_HALF        (0.5)
#define GPU_TRANSCODE_BYTE_MAX          (255.0)
#define GPU_TRANSCODE_AXIS_COUNT        (2.0)

/* One decoded full-resolution frame handed from the decode thread to the encode thread, in whatever
** YCbCr layout the decoder produced (interleaved 2-plane or planar 3-plane). */
typedef struct FrameSlot
{
    byte_t*  planes; /* luma, then the chroma plane(s), allocated once */
    unsigned width;
    unsigned height;
    unsigned lumaStride;
    unsigned chromaStride;
    unsigned chromaWidth;
    unsigned chromaHeight;
    int      chromaInterleaved;
    int      sourceKeyframe;
    int      eof; /* the producer reached the end (or failed) */
} FrameSlot;

struct GpuTranscoder
{
    GpuDevice*         owner;
    GpuDevice*         device;
    Mp4Demux*          demux;
    GpuDecoder*        decoder;
    GpuEncoder*        encoder;
    unsigned           outWidth;
    unsigned           outHeight;
    unsigned           sourceWidth;
    unsigned           sourceHeight;
    unsigned           sampleIndex;
    unsigned           sampleCount;
    byte_t*            sampleBuffer;
    uint32_t           sampleCapacity;
    uint64_t           frameCapacity;
    byte_t*            dstLuma;
    byte_t*            dstChroma;
    GpuTranscodeConfig config;
    byte_t*            prevLuma; /* previous frame's downscaled luma, for optical flow */
    int                havePrev;
    double             lastMotion; /* 0..1 average inter-frame motion of the last frame */

    /* Decode-ahead pipeline: the producer thread decodes into a bounded ring while the caller's
    ** thread downscales and encodes, so the decode and encode engines run in parallel. */
    FrameSlot     ring[PIPELINE_SLOTS];
    HANDLE        emptySlots;  /* counts ring slots the producer may fill */
    HANDLE        filledSlots; /* counts ring slots the consumer may drain */
    HANDLE        producerThread;
    volatile LONG stopProducer;
    int           producerStarted;
    int           sawEof;
    unsigned      head; /* producer writes here */
    unsigned      tail; /* consumer reads here */
};

typedef char GpuTranscoder_StorageSizeCheck[(sizeof(GpuTranscoder) <= GPU_SESSION_STORAGE_CAPACITY) ? 1 : -1];
typedef char GpuTranscoder_StorageAlignmentCheck[(__alignof(GpuTranscoder) <= __alignof(uint64_t)) ? 1 : -1];

/* Area-average downscale of a decoded frame (in whatever YCbCr layout the decoder produced) into the
** encoder's NV12 input: luma to ow x oh, chroma to (ow/2) x (oh/2) interleaved. Averaging rather than
** point sampling keeps the shrunk proxy free of aliasing, and reading chroma through the frame's
** interleaved/planar flag makes the pipeline format-agnostic. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Area-averages source luma and chroma into reduced luma and interleaved chroma,
 *  using the decoded frame's strides and planar/interleaved layout.
 */
static FORCE_INLINE void DownscaleLuma(const FrameSlot* pSrc, byte_t* pDstLuma, unsigned ow, unsigned oh)
{
    const byte_t* p_luma = pSrc->planes;
    unsigned luma_stride = pSrc->lumaStride;
    unsigned sw = pSrc->width;
    unsigned sh = pSrc->height;
    unsigned oy;

    for (oy = 0; oy < oh; ++oy)
    {
        unsigned sy0;
        unsigned sy1;
        unsigned ox;

        sy0 = oy * sh / oh;
        sy1 = (oy + 1U) * sh / oh;
        if (sy1 <= sy0)
        {
            sy1 = sy0 + 1U;
        }
        for (ox = 0; ox < ow; ++ox)
        {
            unsigned sx0;
            unsigned sx1;
            unsigned sy;
            uint32_t sum;
            uint32_t count;

            sx0 = ox * sw / ow;
            sx1 = (ox + 1U) * sw / ow;
            if (sx1 <= sx0)
            {
                sx1 = sx0 + 1U;
            }
            sum   = 0;
            count = 0;
            for (sy = sy0; sy < sy1 && sy < sh; ++sy)
            {
                unsigned sx;

                for (sx = sx0; sx < sx1 && sx < sw; ++sx)
                {
                    sum += p_luma[((size_t)sy * luma_stride) + sx];
                    ++count;
                }
            }
            pDstLuma[((size_t)oy * ow) + ox] = (uint8_t)(sum / (count ? count : 1U));
        }
    }

}

static FORCE_INLINE void SampleChroma(const FrameSlot* pSrc,
                         unsigned cx0,
                         unsigned cx1,
                         unsigned cy0,
                         unsigned cy1,
                         byte_t* pDst)
{
    unsigned chroma_stride = pSrc->chromaStride;
    unsigned src_chroma_w = pSrc->chromaWidth;
    unsigned src_chroma_h = pSrc->chromaHeight;
    const byte_t* p_cb = pSrc->planes + ((size_t)pSrc->lumaStride * pSrc->height);
    const byte_t* p_cr = pSrc->chromaInterleaved ? p_cb : (p_cb + ((size_t)chroma_stride * src_chroma_h));
    unsigned cb_step = pSrc->chromaInterleaved ? 2U : 1U;
    unsigned cy;
    uint32_t sum_cb = 0;
    uint32_t sum_cr = 0;
    uint32_t count = 0;

    for (cy = cy0; cy < cy1 && cy < src_chroma_h; ++cy)
    {
        unsigned cx;

        for (cx = cx0; cx < cx1 && cx < src_chroma_w; ++cx)
        {
            sum_cb += p_cb[((uint64_t)cy * chroma_stride) + ((uint64_t)cx * cb_step)];
            sum_cr += p_cr[((uint64_t)cy * chroma_stride) + ((uint64_t)cx * cb_step) +
                           (pSrc->chromaInterleaved ? 1 : 0)];
            ++count;
        }
    }
    pDst[0] = (uint8_t)(sum_cb / (count ? count : 1U));
    pDst[1] = (uint8_t)(sum_cr / (count ? count : 1U));
}

static void DownscaleChroma(const FrameSlot* pSrc, byte_t* pDstChroma, unsigned ow, unsigned oh)
{
    unsigned src_chroma_w = pSrc->chromaWidth;
    unsigned src_chroma_h = pSrc->chromaHeight;
    unsigned dst_chroma_w;
    unsigned dst_chroma_h;
    unsigned oy;

    dst_chroma_w = (ow + 1U) / 2U;
    dst_chroma_h = (oh + 1U) / 2U;
    for (oy = 0; oy < dst_chroma_h; ++oy)
    {
        unsigned cy0;
        unsigned cy1;
        unsigned ox;

        cy0 = oy * src_chroma_h / dst_chroma_h;
        cy1 = (oy + 1U) * src_chroma_h / dst_chroma_h;
        if (cy1 <= cy0)
        {
            cy1 = cy0 + 1U;
        }
        for (ox = 0; ox < dst_chroma_w; ++ox)
        {
            unsigned cx0;
            unsigned cx1;

            cx0 = ox * src_chroma_w / dst_chroma_w;
            cx1 = (ox + 1U) * src_chroma_w / dst_chroma_w;
            if (cx1 <= cx0)
            {
                cx1 = cx0 + 1U;
            }
            SampleChroma(pSrc, cx0, cx1, cy0, cy1,
                         &pDstChroma[(((size_t)oy * dst_chroma_w) + ox) * 2U]);
        }
    }
}

static FORCE_INLINE void DownscaleFrame(const FrameSlot* pSrc, byte_t* pDstLuma, byte_t* pDstChroma, unsigned ow, unsigned oh)
{
    DownscaleLuma(pSrc, pDstLuma, ow, oh);
    DownscaleChroma(pSrc, pDstChroma, ow, oh);
}

/* A coarse optical-flow estimate: for a grid of blocks, search a small window for the best match in
** the previous frame and accumulate the motion magnitude, normalised to 0..1. This is the motion
** signal that drives adaptive QP (spend fewer bits when the scene barely moves) and scene-cut
** keyframes (a sudden jump in residual means a new shot, which deserves a fresh IDR). */
/*************************************************************************************************/
/**
 * \par Details:
 *  Searches nearby matches for sampled blocks in the previous luma frame.
 *  Zero-motion matches seed the search so ambiguous flat blocks stay stationary.
 */
#define GPU_TRANSCODE_MOTION_BLOCK 16U
#define GPU_TRANSCODE_MOTION_SEARCH 4

static FORCE_INLINE uint32_t MotionBlockSad(const byte_t* pPrev,
                               const byte_t* pCurr,
                               unsigned width,
                               unsigned bx,
                               unsigned by,
                               int dx,
                               int dy)
{
    uint32_t sad = 0;
    unsigned y;

    for (y = 0; y < GPU_TRANSCODE_MOTION_BLOCK; y += 2)
    {
        unsigned x;

        for (x = 0; x < GPU_TRANSCODE_MOTION_BLOCK; x += 2)
        {
            int a = pCurr[(((uint64_t)by + y) * width) + (uint64_t)bx + x];
            int b = pPrev[((uint64_t)((int64_t)by + y + dy) * width) + (uint64_t)((int64_t)bx + x + dx)];
            sad += (uint32_t)(a > b ? a - b : b - a);
        }
    }
    return sad;
}

static FORCE_INLINE void FindBestBlockMatch(const byte_t* pPrev,
                               const byte_t* pCurr,
                               unsigned width,
                               unsigned height,
                               unsigned bx,
                               unsigned by,
                               int* pDx,
                               int* pDy,
                               uint32_t* pSad)
{
    int dy;

    *pDx = 0;
    *pDy = 0;
    *pSad = MotionBlockSad(pPrev, pCurr, width, bx, by, 0, 0);
    for (dy = -GPU_TRANSCODE_MOTION_SEARCH; dy <= GPU_TRANSCODE_MOTION_SEARCH; ++dy)
    {
        int dx;

        if ((int)by + dy < 0 || by + GPU_TRANSCODE_MOTION_BLOCK + (unsigned)dy > height)
        {
            continue;
        }
        for (dx = -GPU_TRANSCODE_MOTION_SEARCH; dx <= GPU_TRANSCODE_MOTION_SEARCH; ++dx)
        {
            uint32_t sad;

            if ((int)bx + dx < 0 || bx + GPU_TRANSCODE_MOTION_BLOCK + (unsigned)dx > width)
            {
                continue;
            }
            sad = MotionBlockSad(pPrev, pCurr, width, bx, by, dx, dy);
            if (sad < *pSad)
            {
                *pSad = sad;
                *pDx = dx;
                *pDy = dy;
            }
        }
    }
}

static double EstimateMotion(const byte_t* pPrev,
                             const byte_t* pCurr,
                             unsigned width,
                             unsigned height,
                             double* pResidual)
{
    unsigned by;
    uint64_t motion_sum = 0;
    uint64_t residual_sum = 0;
    unsigned block_count = 0;

    for (by = 0; by + GPU_TRANSCODE_MOTION_BLOCK <= height; by += GPU_TRANSCODE_MOTION_BLOCK)
    {
        unsigned bx;

        for (bx = 0; bx + GPU_TRANSCODE_MOTION_BLOCK <= width; bx += GPU_TRANSCODE_MOTION_BLOCK)
        {
            int best_dx;
            int best_dy;
            uint32_t best_sad;

            FindBestBlockMatch(pPrev, pCurr, width, height, bx, by, &best_dx, &best_dy, &best_sad);
            motion_sum += (uint64_t)(best_dx < 0 ? -best_dx : best_dx) +
                          (uint64_t)(best_dy < 0 ? -best_dy : best_dy);
            residual_sum += best_sad;
            ++block_count;
        }
    }
    if (0 == block_count)
    {
        if (NULL != pResidual)
        {
            *pResidual = 0.0;
        }
        return 0.0;
    }
    if (NULL != pResidual)
    {
        const double half_block = (double)GPU_TRANSCODE_MOTION_BLOCK / GPU_TRANSCODE_AXIS_COUNT;

        *pResidual = (double)residual_sum / block_count / (half_block * half_block) / GPU_TRANSCODE_BYTE_MAX;
    }
    return (double)motion_sum / block_count /
           (GPU_TRANSCODE_AXIS_COUNT * GPU_TRANSCODE_MOTION_SEARCH);
}

static FORCE_INLINE bool_t CopyDecodedFramePlanes(FrameSlot* slot, const DecodedFrame* frame, uint64_t capacity)
{
    size_t        luma_bytes;
    size_t        chroma_bytes;
    uint64_t      safe_luma;
    uint64_t      chroma_factor;
    uint64_t      copy_bytes;
    bool_t        fits_luma;
    bool_t        fits_chroma;
    bool_t        fits_frame;
    byte_t        discard_output = 0;
    byte_t        discard_input = 0;
    byte_t*       destination;
    const byte_t* source;

    luma_bytes   = (size_t)frame->lumaStride * frame->height;
    chroma_bytes = (size_t)frame->chromaStride * frame->chromaHeight;
    fits_luma    = ((uint64_t)luma_bytes <= capacity);
    safe_luma    = fits_luma ? (uint64_t)luma_bytes : 0;
    chroma_factor = frame->chromaInterleaved ? 1ULL : 2ULL;
    fits_chroma  = ((uint64_t)chroma_bytes <= (capacity - safe_luma) / chroma_factor);
    fits_frame   = fits_luma && fits_chroma;
    copy_bytes   = fits_frame ? ((uint64_t)luma_bytes + (chroma_factor * chroma_bytes)) : 0;
    destination  = fits_frame ? slot->planes : &discard_output;
    source       = fits_frame ? frame->pLuma : &discard_input;
    CRT_Memory_Copy(destination, source, copy_bytes);
    return fits_frame;
}

/* The decode-ahead producer: reads a source sample, decodes it, and copies the full-resolution NV12
** into the next ring slot for the encode thread. Runs until the source ends or a stop is signalled.
** Decode runs on its own engine and transfer queue, so it overlaps the encode side. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Reads and decodes ahead into semaphore-controlled ring slots. Publishes each
 *  slot only after its plane data and frame metadata have been copied.
 */
static DWORD WINAPI ProducerThread(LPVOID pParameter)
{
    GpuTranscoder* pTranscoder = (GpuTranscoder*)pParameter;

    for (;;)
    {
        FrameSlot*       p_slot;
        const Mp4Sample* p_sample;
        DecodedFrame     df;
        uint32_t         bytes;

        WaitForSingleObject(pTranscoder->emptySlots, INFINITE);
        if (0 != pTranscoder->stopProducer)
        {
            break;
        }
        p_slot = &pTranscoder->ring[pTranscoder->head];
        p_sample = Mp4Demux_GetSample(pTranscoder->demux, pTranscoder->sampleIndex);
        bytes    = Mp4Demux_ReadSample(pTranscoder->demux,
                                      pTranscoder->sampleIndex,
                                      pTranscoder->sampleBuffer,
                                      pTranscoder->sampleCapacity);
        if (pTranscoder->sampleIndex >= pTranscoder->sampleCount)
        {
            p_slot->eof       = 1;
            pTranscoder->head = (pTranscoder->head + 1U) % PIPELINE_SLOTS;
            ReleaseSemaphore(pTranscoder->filledSlots, 1, NULL);
            break;
        }
        ++pTranscoder->sampleIndex;
        if (0 == bytes || !GpuDecode_DecodeFrame(pTranscoder->decoder, pTranscoder->sampleBuffer, bytes))
        {
            p_slot->eof       = 1;
            pTranscoder->head = (pTranscoder->head + 1U) % PIPELINE_SLOTS;
            ReleaseSemaphore(pTranscoder->filledSlots, 1, NULL);
            break;
        }
        GpuDecode_GetFrame(pTranscoder->decoder, &df);
        if (!CopyDecodedFramePlanes(p_slot, &df, pTranscoder->frameCapacity))
        {
            p_slot->eof       = 1;
            pTranscoder->head = (pTranscoder->head + 1U) % PIPELINE_SLOTS;
            ReleaseSemaphore(pTranscoder->filledSlots, 1, NULL);
            break;
        }
        p_slot->width             = df.width;
        p_slot->height            = df.height;
        p_slot->lumaStride        = df.lumaStride;
        p_slot->chromaStride      = df.chromaStride;
        p_slot->chromaWidth       = df.chromaWidth;
        p_slot->chromaHeight      = df.chromaHeight;
        p_slot->chromaInterleaved = df.chromaInterleaved;
        p_slot->sourceKeyframe    = (NULL != p_sample && p_sample->keyframe) ? 1 : 0;
        p_slot->eof               = 0;
        pTranscoder->head         = (pTranscoder->head + 1U) % PIPELINE_SLOTS;
        ReleaseSemaphore(pTranscoder->filledSlots, 1, NULL);
    }
    return 0;
}

/* ---- One-call transcode of a source MP4 to a proxy MP4 (video re-encode + audio passthrough). -- */

#include "mp4_mux.h"

/* Never replace a source with a proxy that silently drops its audio. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Permits no-audio input or a usable AAC track with configuration, timing and
 *  sample metadata, preventing a proxy from silently discarding source audio.
 */
static FORCE_INLINE int CanPreserveAudio(const Mp4Demux* pDemux)
{
    uint32_t config_size;

    if (MP4_AUDIO_NONE == Mp4Demux_GetAudioCodec(pDemux))
    {
        return 1;
    }
    (void)Mp4Demux_GetAudioConfig(pDemux, &config_size);
    return MP4_AUDIO_AAC == Mp4Demux_GetAudioCodec(pDemux) && config_size > 0 &&
           Mp4Demux_GetAudioSampleCount(pDemux) > 0 && Mp4Demux_GetAudioTimescale(pDemux) > 0 &&
           Mp4Demux_GetAudioSampleRate(pDemux) > 0 && Mp4Demux_GetAudioChannels(pDemux) > 0;
}

/* Copy every audio sample starting before this video's end, including the last frame.
** A failed read/write must fail the proxy, so the session keeps the original stream. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Copies audio samples beginning before the requested video boundary. A read
 *  or mux failure aborts the proxy path instead of omitting that sample.
 */
static FORCE_INLINE int CopyAudioUntil(Mp4Demux* pDemux, Mp4Mux* pMux, unsigned* pIndex, byte_t* pBuffer, double endSeconds)
{
    const unsigned timescale = Mp4Demux_GetAudioTimescale(pDemux);
    const unsigned count     = Mp4Demux_GetAudioSampleCount(pDemux);
    unsigned       end_index = *pIndex;

    while (end_index < count)
    {
        const Mp4Sample* pSample = Mp4Demux_GetAudioSample(pDemux, end_index);

        if ((double)pSample->decodeTime / timescale >= endSeconds)
        {
            break;
        }
        ++end_index;
    }
    while (*pIndex < end_index)
    {
        const Mp4Sample* pSample;
        uint32_t         bytes;

        pSample = Mp4Demux_GetAudioSample(pDemux, *pIndex);
        bytes = Mp4Demux_ReadAudioSample(pDemux, *pIndex, pBuffer, GPU_AUDIO_BUFFER_CAPACITY);
        if (0 == bytes || 0 == pSample->duration || !Mp4Mux_AddAudio(pMux, pBuffer, bytes, pSample->duration))
        {
            return 0;
        }
        ++*pIndex;
    }
    return 1;
}

/* Points pp/pLen at the first NAL of the given type inside an Annex-B buffer (start code stripped). */
static FORCE_INLINE uint32_t TrimNalLength(const byte_t* pData, uint32_t start, uint32_t end)
{
    uint32_t length = end - start;

    while (length > 0 && pData[start + length - 1] == 0)
    {
        --length;
    }
    return length;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Scans Annex B start codes and borrows the first matching NAL span without
 *  its start code; clears the result when none matches.
 */
static FORCE_INLINE void FindNalOfType(const byte_t* pData, uint32_t size, uint32_t nalType, const byte_t** pp, uint32_t* pLen)
{
    uint32_t i;
    uint32_t nal_start;

    *pp       = NULL;
    *pLen     = 0;
    i         = 0;
    nal_start = 0;
    for (;;)
    {
        if (3 > (size - i))
        {
            break;
        }
        if (0 == pData[i] && 0 == pData[i + 1] && 1 == pData[i + 2])
        {
            if (nal_start > 0 && i > nal_start)
            {
                uint32_t nal_length;

                nal_length = TrimNalLength(pData, nal_start, i);
                if (nal_length > 0 && (pData[nal_start] & H264_NAL_TYPE_MASK) == nalType)
                {
                    *pp   = pData + nal_start;
                    *pLen = nal_length;
                    return;
                }
            }
            i         += 3;
            nal_start  = i;
        }
        else
        {
            ++i;
        }
    }
    if (nal_start > 0 && nal_start < size && (pData[nal_start] & H264_NAL_TYPE_MASK) == nalType)
    {
        *pp   = pData + nal_start;
        *pLen = size - nal_start;
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Sets the default output width, QP, short GOP and motion-adaptation strength.
 */
extern FORCE_INLINE void GpuTranscode_InitializeConfig(GpuTranscodeConfig* pConfig)
{
    /* The proxy is the primary stream once the session switches to it, so it has to be worth
    ** watching: HD-wide with a low quantiser, and a short GOP so seeks are still cheap. */
    pConfig->width          = GPU_TRANSCODE_DEFAULT_WIDTH;
    pConfig->bitrateKbps    = GPU_TRANSCODE_DEFAULT_BITRATE;
    pConfig->gopLength      = GPU_TRANSCODE_DEFAULT_GOP; /* ~0.5 s between keyframes at 24 fps, so a seek decodes little */
    pConfig->qpIntra        = GPU_TRANSCODE_DEFAULT_QP_INTRA;
    pConfig->qpInter        = GPU_TRANSCODE_DEFAULT_QP_INTER;
    pConfig->motionAdaptive = 1;
    pConfig->motionStrength = GPU_TRANSCODE_DEFAULT_MOTION; /* a gentle bit-saver on still frames, not a quality cliff */
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Opens the source and creates its shared GPU device, decoder, encoder and
 *  frame-ring storage. Failed resource setup unwinds through Close.
 */
GpuTranscoder* GpuTranscode_Open(GpuDevice* deviceStorage, const TCHAR* pFile, const GpuTranscodeConfig* pConfig, char* pReport, size_t reportCapacity)
{
    Mp4Demux*                        p_demux;
    uint32_t                         sps_size;
    StdVideoH264SequenceParameterSet sps;
    unsigned                         source_width;
    unsigned                         source_height;
    uint32_t                         pps_size;
    StdVideoH264PictureParameterSet  pps;
    unsigned                         out_width;
    unsigned                         out_height;
    uint64_t                         output_height;
    uint64_t                         source_pixels;
    uint64_t                         proxy_pixels;
    uint64_t                         frame_bytes;
    uint64_t                         luma_bytes;
    uint64_t                         chroma_bytes;
    uint64_t                         workspace_bytes;
    uint64_t                         offset;
    GpuTranscoder*                   pTranscoder;
    GpuDevice*                       device;
    unsigned                         sampleCount;
    char                             device_report[GPU_TRANSCODE_DEVICE_REPORT_CAPACITY];
    char                             decode_report[GPU_TRANSCODE_CODEC_REPORT_CAPACITY];
    char                             encode_report[GPU_TRANSCODE_CODEC_REPORT_CAPACITY];
    GpuTranscodeConfig               config;

    config = *pConfig;
    if (reportCapacity > 0)
    {
        pReport[0] = 0;
    }
    p_demux = Mp4Demux_Open(pFile);
    if (NULL == p_demux)
    {
        Report(pReport, reportCapacity, "not an AVC MP4\n");
        return NULL;
    }
    sampleCount = Mp4Demux_GetSampleCount(p_demux);
    if (!H264_ParseSps(Mp4Demux_GetSps(p_demux, &sps_size), sps_size, &sps, &source_width, &source_height) ||
        !H264_ParsePps(Mp4Demux_GetPps(p_demux, &pps_size), pps_size, &pps))
    {
        Report(pReport, reportCapacity, "SPS/PPS parse failed\n");
        Mp4Demux_Close(p_demux);
        return NULL;
    }

    if ((0 == source_width) || (UINT32_MAX == config.width))
    {
        Mp4Demux_Close(p_demux);
        return NULL;
    }
    out_width = (config.width + 1U) & ~1U;
    if (out_width < GPU_TRANSCODE_MIN_WIDTH)
    {
        out_width = GPU_TRANSCODE_MIN_WIDTH;
    }
    output_height = (((uint64_t)out_width * source_height) + (source_width / 2U)) / source_width;
    if (output_height >= UINT32_MAX)
    {
        Mp4Demux_Close(p_demux);
        return NULL;
    }
    out_height = (unsigned)output_height;
    out_height = (out_height + 1U) & ~1U;
    if (out_height < 2U)
    {
        out_height = 2U;
    }

    source_pixels = (uint64_t)source_width * source_height;
    proxy_pixels  = (uint64_t)out_width * out_height;
    if ((source_pixels > GPU_FRAME_STORAGE_CAPACITY / ((uint64_t)PIPELINE_SLOTS * 2U)) ||
        (proxy_pixels > GPU_FRAME_STORAGE_CAPACITY / 2U))
    {
        Report(pReport, reportCapacity, "frame dimensions exceed resident workspace\n");
        Mp4Demux_Close(p_demux);
        return NULL;
    }
    frame_bytes     = source_pixels * 2U;
    luma_bytes      = proxy_pixels;
    chroma_bytes    = (((uint64_t)out_width + 1U) / 2U) * 2U * (((uint64_t)out_height + 1U) / 2U);
    workspace_bytes = (PIPELINE_SLOTS * frame_bytes) + (2U * luma_bytes) + chroma_bytes;
    if (GPU_FRAME_STORAGE_CAPACITY < workspace_bytes)
    {
        Report(pReport, reportCapacity, "frame dimensions exceed resident workspace\n");
        Mp4Demux_Close(p_demux);
        return NULL;
    }

    if ((NULL == deviceStorage) || deviceStorage->transcoderBusy)
    {
        Mp4Demux_Close(p_demux);
        return NULL;
    }
    pTranscoder = (GpuTranscoder*)&deviceStorage->transcoderStorage;
    CRT_Memory_Set(pTranscoder, 0, sizeof(*pTranscoder));
    pTranscoder->owner = deviceStorage;
    deviceStorage->transcoderBusy = TRUE;
    pTranscoder->config         = config;
    pTranscoder->demux          = p_demux;
    pTranscoder->outWidth       = out_width;
    pTranscoder->outHeight      = out_height;
    pTranscoder->sourceWidth    = source_width;
    pTranscoder->sourceHeight   = source_height;
    pTranscoder->sampleCount    = sampleCount;
    pTranscoder->sampleCapacity = GPU_SAMPLE_BUFFER_CAPACITY;
    pTranscoder->frameCapacity  = frame_bytes;
    pTranscoder->device         = GpuDevice_Create(deviceStorage, TRUE, TRUE, device_report, sizeof(device_report));
    device                      = pTranscoder->device;
    Report(pReport, reportCapacity, "device: %s", device_report);
    if (NULL == device)
    {
        GpuTranscode_Close(pTranscoder);
        return NULL;
    }
    pTranscoder->decoder       = GpuDecode_Create(device,
                                                  &sps,
                                                  &pps,
                                                  source_width,
                                                  source_height,
                                                  sps.max_num_ref_frames + 2U,
                                                  Mp4Demux_GetNalLengthSize(p_demux),
                                                  decode_report,
                                                  sizeof(decode_report));
    pTranscoder->encoder = GpuEncode_Create(device,
                                            out_width,
                                            out_height,
                                             config.bitrateKbps,
                                             config.gopLength,
                                             config.qpIntra,
                                             config.qpInter,
                                            GPU_TRANSCODE_DEFAULT_MOTION,
                                            1,
                                            encode_report,
                                            sizeof(encode_report));
    pTranscoder->sampleBuffer = deviceStorage->sampleBuffer;
    offset                 = 0;
    pTranscoder->dstLuma   = &deviceStorage->frameStorage[offset];
    offset                += luma_bytes;
    pTranscoder->dstChroma = &deviceStorage->frameStorage[offset];
    offset                += chroma_bytes;
    pTranscoder->prevLuma  = &deviceStorage->frameStorage[offset];
    offset                += luma_bytes;
    {
        unsigned slot;

        for (slot = 0; slot < PIPELINE_SLOTS; ++slot)
        {
            pTranscoder->ring[slot].planes = &deviceStorage->frameStorage[offset];
            offset += frame_bytes;
        }
    }
    pTranscoder->emptySlots  = CreateSemaphore(NULL, PIPELINE_SLOTS, PIPELINE_SLOTS, NULL);
    pTranscoder->filledSlots = CreateSemaphore(NULL, 0, PIPELINE_SLOTS, NULL);
    Report(pReport, reportCapacity, "decode: %s", decode_report);
    Report(pReport, reportCapacity, "encode: %s", encode_report);
    if (NULL == pTranscoder->decoder || NULL == pTranscoder->encoder || NULL == pTranscoder->emptySlots ||
        NULL == pTranscoder->filledSlots)
    {
        Report(pReport, reportCapacity, "transcoder resource allocation failed\n");
        GpuTranscode_Close(pTranscoder);
        return NULL;
    }
    Report(pReport,
           reportCapacity,
           "proxy: %ux%u from %ux%u, %u samples, qp %d/%d, gop %u, motion %s\n",
           out_width,
           out_height,
           source_width,
           source_height,
            sampleCount,
            config.qpIntra,
            config.qpInter,
            config.gopLength,
            config.motionAdaptive ? "on" : "off");
    return pTranscoder;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE double GpuTranscode_GetLastMotion(const GpuTranscoder* pTranscoder)
{
    return pTranscoder->lastMotion;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE const byte_t* GpuTranscode_GetHeader(GpuTranscoder* pTranscoder, uint32_t* pSize)
{
    return GpuEncode_GetHeader(pTranscoder->encoder, pSize);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE unsigned GpuTranscode_GetOutputWidth(const GpuTranscoder* pTranscoder)
{
    return pTranscoder->outWidth;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE unsigned GpuTranscode_GetOutputHeight(const GpuTranscoder* pTranscoder)
{
    return pTranscoder->outHeight;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
unsigned GpuTranscode_GetFrameCount(const GpuTranscoder* pTranscoder)
{
    return pTranscoder->sampleCount;
}

/* Advances the decoder to the keyframe at or before approxFrame, so a preview clip starts on a
** clean IDR. Returns the sample index it settled on. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Searches backward from the clamped requested sample and resets temporal
 *  motion history at the selected keyframe.
 */
extern FORCE_INLINE unsigned GpuTranscode_SeekKeyframe(GpuTranscoder* pTranscoder, unsigned approxFrame)
{
    unsigned index;

    if (approxFrame >= pTranscoder->sampleCount)
    {
        approxFrame = pTranscoder->sampleCount ? pTranscoder->sampleCount - 1U : 0U;
    }
    for (index = approxFrame; index > 0; --index)
    {
        const Mp4Sample* p_sample;

        p_sample = Mp4Demux_GetSample(pTranscoder->demux, index);
        if (NULL != p_sample && p_sample->keyframe)
        {
            break;
        }
    }
    pTranscoder->sampleIndex = index;
    pTranscoder->havePrev    = 0;
    return index;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Starts the producer on first use, consumes a decoded ring slot, downscales
 *  and selects adaptive encode parameters, then returns the encoded frame.
 */
const byte_t* GpuTranscode_ReadNextFrame(GpuTranscoder* pTranscoder, uint32_t* pSize, int* pKeyframe)
{
    FrameSlot*    p_slot;
    int           force_idr;
    int           qp_override;
    const byte_t* coded;

    if (pTranscoder->sawEof)
    {
        *pSize = 0;
        return NULL;
    }
    if (!pTranscoder->producerStarted)
    {
        pTranscoder->producerStarted = 1;
        pTranscoder->producerThread  = CreateThread(NULL, 0, ProducerThread, pTranscoder, 0, NULL);
        if (NULL == pTranscoder->producerThread)
        {
            pTranscoder->sawEof = 1;
            *pSize              = 0;
            return NULL;
        }
    }
    WaitForSingleObject(pTranscoder->filledSlots, INFINITE);
    p_slot = &pTranscoder->ring[pTranscoder->tail];
    if (p_slot->eof)
    {
        pTranscoder->sawEof = 1;
        *pSize              = 0;
        return NULL;
    }

    /* Downscale the decoded frame this slot carries, then optical-flow-adapt QP and keyframes. */
    DownscaleFrame(p_slot, pTranscoder->dstLuma, pTranscoder->dstChroma, pTranscoder->outWidth, pTranscoder->outHeight);
    force_idr   = p_slot->sourceKeyframe;
    qp_override = 0;
    if (pTranscoder->config.motionAdaptive && pTranscoder->havePrev)
    {
        double residual;
        double motion;
        int    bump;

        motion = EstimateMotion(pTranscoder->prevLuma,
                                                 pTranscoder->dstLuma,
                                                 pTranscoder->outWidth,
                                                 pTranscoder->outHeight,
                                                 &residual);
        pTranscoder->lastMotion = motion;
        bump                    = (int)(((1.0 - motion) * (pTranscoder->config.motionStrength / 100.0) * GPU_TRANSCODE_MOTION_QP_RANGE) + GPU_TRANSCODE_ROUND_HALF);
        qp_override             = pTranscoder->config.qpInter + bump;
        if (qp_override > GPU_TRANSCODE_MAX_QP)
        {
            qp_override = GPU_TRANSCODE_MAX_QP;
        }
        if (!force_idr && residual > GPU_TRANSCODE_SCENE_RESIDUAL && motion > GPU_TRANSCODE_SCENE_MOTION)
        {
            force_idr = 1; /* a sudden, poorly-matching change of content: a new shot */
        }
    }
    coded = GpuEncode_EncodeFrame(pTranscoder->encoder,
                                  pTranscoder->dstLuma,
                                  pTranscoder->outWidth,
                                  pTranscoder->dstChroma,
                                  ((pTranscoder->outWidth + 1U) / 2U) * 2U,
                                  force_idr,
                                  qp_override,
                                  pSize);

    CRT_Memory_Copy(pTranscoder->prevLuma, pTranscoder->dstLuma, (uint64_t)pTranscoder->outWidth * pTranscoder->outHeight);
    pTranscoder->havePrev = 1;
    if (NULL != pKeyframe)
    {
        *pKeyframe = GpuEncode_IsLastFrameKeyframe(pTranscoder->encoder); /* actual IDR, incl. GOP boundaries */
    }
    pTranscoder->tail = (pTranscoder->tail + 1U) % PIPELINE_SLOTS;
    ReleaseSemaphore(pTranscoder->emptySlots, 1, NULL); /* slot free only after the downscale read it */
    return coded;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Signals and joins the producer before destroying resources it uses, then
 *  releases synchronization objects, codec/device owners and CPU frame storage.
 */
void GpuTranscode_Close(GpuTranscoder* pTranscoder)
{
    GpuDevice* owner;

    if (NULL == pTranscoder)
    {
        return;
    }
    owner = pTranscoder->owner;
    if (NULL == owner)
    {
        return;
    }
    /* Stop the decode-ahead thread and join it before tearing down the GPU objects it uses. */
    if (NULL != pTranscoder->producerThread)
    {
        InterlockedExchange(&pTranscoder->stopProducer, 1);
        if (NULL != pTranscoder->emptySlots)
        {
            ReleaseSemaphore(pTranscoder->emptySlots, PIPELINE_SLOTS, NULL); /* unblock it if waiting */
        }
        WaitForSingleObject(pTranscoder->producerThread, INFINITE);
        CloseHandle(pTranscoder->producerThread);
    }
    if (NULL != pTranscoder->emptySlots)
    {
        CloseHandle(pTranscoder->emptySlots);
    }
    if (NULL != pTranscoder->filledSlots)
    {
        CloseHandle(pTranscoder->filledSlots);
    }
    if (NULL != pTranscoder->encoder)
    {
        GpuEncode_Destroy(pTranscoder->encoder);
    }
    if (NULL != pTranscoder->decoder)
    {
        GpuDecode_Destroy(pTranscoder->decoder);
    }
    if (NULL != pTranscoder->device)
    {
        GpuDevice_Destroy(pTranscoder->device);
    }
    if (NULL != pTranscoder->demux)
    {
        Mp4Demux_Close(pTranscoder->demux);
    }
    CRT_Memory_Set(pTranscoder, 0, sizeof(*pTranscoder));
    owner->transcoderBusy = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Starts at a source keyframe, preserves video timing and interleaves retained
 *  AAC up to each encoded frame's end before finalizing the clip.
 */
static FORCE_INLINE unsigned SkipPrecedingAudio(Mp4Demux* pTiming,
                                   unsigned start_index,
                                   unsigned audio_samples,
                                   unsigned video_timescale,
                                   unsigned audio_timescale,
                                   int has_audio)
{
    const Mp4Sample* p_start;
    double start_seconds;
    unsigned audio_index;

    if (!has_audio || 0 == video_timescale || 0 == audio_timescale || 0 == start_index)
    {
        return 0;
    }
    p_start = Mp4Demux_GetSample(pTiming, start_index);
    start_seconds = (NULL != p_start) ? (double)p_start->decodeTime / video_timescale : 0.0;
    for (audio_index = 0; audio_index < audio_samples; ++audio_index)
    {
        const Mp4Sample* p_audio = Mp4Demux_GetAudioSample(pTiming, audio_index);

        if (NULL == p_audio || (double)p_audio->decodeTime / audio_timescale >= start_seconds)
        {
            break;
        }
    }
    return audio_index;
}

int GpuTranscode_WriteMp4(GpuDevice*                deviceStorage,
                          const TCHAR*              pSource,
                          const TCHAR*              pDest,
                          const GpuTranscodeConfig* pConfig,
                          unsigned                  startFrame,
                          unsigned                  maxFrames,
                          const volatile long*      pCancel,
                          char*                     pReport,
                          size_t                    reportCapacity)
{
    GpuTranscoder* pTranscoder;
    char           open_report[GPU_TRANSCODE_REPORT_CAPACITY];
    Mp4Demux*      pTiming;
    const byte_t*  header;
    uint32_t       header_size;
    const byte_t*  sps;
    uint32_t       sps_size;
    const byte_t*  pps;
    uint32_t       pps_size;
    unsigned       video_timescale;
    unsigned       total_samples;
    unsigned       start_index;
    unsigned       end_index;
    unsigned       audio_samples;
    unsigned       audio_timescale;
    const byte_t*  asc;
    uint32_t       asc_size;
    int            has_audio;
    Mp4Mux*        pMux;
    byte_t*        audio_buffer;
    unsigned       audio_index;
    unsigned       produced;
    double         motion_accum;
    int            ok;
    unsigned       frame;

    pTranscoder = GpuTranscode_Open(deviceStorage, pSource, pConfig, open_report, sizeof(open_report));
    if (reportCapacity > 0)
    {
        pReport[0] = 0;
    }
    Report(pReport, reportCapacity, "%s", open_report);
    if (NULL == pTranscoder)
    {
        return 0;
    }
    pTiming = Mp4Demux_Open(pSource);
    if (NULL == pTiming)
    {
        GpuTranscode_Close(pTranscoder);
        return 0;
    }

    header = GpuTranscode_GetHeader(pTranscoder, &header_size);
    FindNalOfType(header, header_size, H264_NAL_TYPE_SPS, &sps, &sps_size);
    FindNalOfType(header, header_size, H264_NAL_TYPE_PPS, &pps, &pps_size);
    video_timescale = Mp4Demux_GetTimescale(pTiming);
    total_samples   = Mp4Demux_GetSampleCount(pTiming);
    start_index     = (startFrame > 0) ? GpuTranscode_SeekKeyframe(pTranscoder, startFrame) : 0U;
    end_index       = (maxFrames > 0 && start_index + maxFrames < total_samples) ? start_index + maxFrames : total_samples;
    audio_samples   = Mp4Demux_GetAudioSampleCount(pTiming);
    audio_timescale = Mp4Demux_GetAudioTimescale(pTiming);
    asc             = Mp4Demux_GetAudioConfig(pTiming, &asc_size);
    has_audio       = (MP4_AUDIO_AAC == Mp4Demux_GetAudioCodec(pTiming) && audio_samples > 0 && asc_size > 0);

    if (!CanPreserveAudio(pTiming))
    {
        Report(pReport, reportCapacity, "source audio cannot be preserved; keeping the original stream\n");
        Mp4Demux_Close(pTiming);
        GpuTranscode_Close(pTranscoder);
        return 0;
    }

    pMux = Mp4Mux_Create(pDest,
                         GpuTranscode_GetOutputWidth(pTranscoder),
                         GpuTranscode_GetOutputHeight(pTranscoder),
                         sps,
                         sps_size,
                         pps,
                         pps_size,
                         video_timescale,
                         has_audio,
                         audio_timescale,
                         Mp4Demux_GetAudioSampleRate(pTiming),
                         Mp4Demux_GetAudioChannels(pTiming),
                         asc,
                         asc_size);
    if (NULL == pMux)
    {
        Report(pReport, reportCapacity, "could not create the proxy MP4\n");
        Mp4Demux_Close(pTiming);
        GpuTranscode_Close(pTranscoder);
        return 0;
    }

    audio_buffer = deviceStorage->audioBuffer;
    audio_index  = SkipPrecedingAudio(pTiming, start_index, audio_samples, video_timescale, audio_timescale, has_audio);
    produced     = 0;
    motion_accum = 0.0;
    ok           = TRUE;

    for (frame = start_index; ok && frame < end_index; ++frame)
    {
        const byte_t*    coded;
        uint32_t         coded_size;
        int              keyframe;
        const Mp4Sample* p_video;
        uint32_t         duration;

        if (NULL != pCancel && 0 != *pCancel)
        {
            ok = 0;
            break;
        }
        coded = GpuTranscode_ReadNextFrame(pTranscoder, &coded_size, &keyframe);
        if (NULL == coded)
        {
            ok = 0;
            break;
        }
        p_video  = Mp4Demux_GetSample(pTiming, frame);
        duration = p_video->duration;
        if (!Mp4Mux_AddVideo(pMux, coded, coded_size, duration, keyframe))
        {
            ok = 0;
            break;
        }
        ++produced;
        motion_accum += GpuTranscode_GetLastMotion(pTranscoder);

        /* Interleave audio up to this frame's end time (absolute), so the mdat stays in play order. */
        if (has_audio)
        {
            ok = CopyAudioUntil(pTiming,
                                pMux,
                                &audio_index,
                                audio_buffer,
                                (double)(p_video->decodeTime + duration) / video_timescale);
        }
    }

    /* Audio is interleaved up to the last video frame's time inside the loop above; audio beyond the
    ** transcoded video is intentionally dropped so the proxy's tracks end together. */
    if (ok)
    {
        ok = Mp4Mux_Finish(pMux);
    }
    Report(pReport,
           reportCapacity,
           "proxy mp4: %s, avg motion %d%% (%u video frames from %u)\n",
           ok ? "ok" : "failed",
           (int)(produced ? (motion_accum / produced * 100.0) + GPU_TRANSCODE_ROUND_HALF : 0.0),
           produced,
           start_index);
    Mp4Mux_Destroy(pMux);
    Mp4Demux_Close(pTiming);
    GpuTranscode_Close(pTranscoder);
    return ok;
}

/* ---- Progressive (fragmented-MP4) transcode: playable within a fraction of a second. ---------- */

/*************************************************************************************************/
/**
 * \par Details:
 *  Flushes at keyframe boundaries and interleaves audio into those fragments.
 *  Invokes the optional ready callback after the configured head start or the
 *  successful final flush of a short clip.
 */
static FORCE_INLINE double GetSourceDuration(Mp4Demux* pTiming, unsigned total_samples, unsigned video_timescale)
{
    const Mp4Sample* p_last;

    if (0 == total_samples || 0 == video_timescale)
    {
        return 0.0;
    }
    p_last = Mp4Demux_GetSample(pTiming, total_samples - 1U);
    return (NULL != p_last) ? (double)(p_last->decodeTime + p_last->duration) / video_timescale : 0.0;
}

static FORCE_INLINE void FlushBeforeKeyframe(Mp4Mux* pMux,
                                int keyframe,
                                unsigned produced,
                                unsigned* pFragments,
                                int* pFirstFragmentSent,
                                uint64_t head_ticks,
                                unsigned video_timescale,
                                GpuTranscodeReadyFn onReady,
                                void* pUser)
{
    if (keyframe && produced > 0 && Mp4Mux_FlushFragment(pMux))
    {
        ++(*pFragments);
        if (!*pFirstFragmentSent && NULL != onReady &&
            head_ticks >= (uint64_t)GPU_TRANSCODE_HEAD_START * video_timescale)
        {
            *pFirstFragmentSent = 1;
            onReady(pUser);
        }
    }
}

int GpuTranscode_WriteFragmentedMp4(GpuDevice*                deviceStorage,
                                    const uint16_t*           pSource,
                                    const uint16_t*           pDest,
                                    const GpuTranscodeConfig* pConfig,
                                    uint32_t                  maxFrames,
                                    GpuTranscodeReadyFn       onReady,
                                    void*                     pUser,
                                    const OS_Atomic32_t*      pCancel,
                                    char*                     pReport,
                                    uint32_t                  reportCapacity)
{
    GpuTranscoder*   pTranscoder;
    char             open_report[GPU_TRANSCODE_REPORT_CAPACITY];
    Mp4Demux*        pTiming;
    const byte_t*    header;
    uint32_t         header_size;
    const byte_t*    sps;
    uint32_t         sps_size;
    const byte_t*    pps;
    uint32_t         pps_size;
    unsigned         video_timescale;
    unsigned         total_samples;
    unsigned         audio_samples;
    unsigned         audio_timescale;
    const byte_t*    asc;
    uint32_t         asc_size;
    int              has_audio;
    double           full_duration;
    Mp4Mux*          pMux;
    byte_t*          audio_buffer;
    unsigned         audio_index;
    unsigned         produced;
    unsigned         fragments;
    int              first_fragment_sent;
    uint64_t         head_ticks; /* video content on disk, for the pre-switch head start */
    int              ok;
    unsigned         frame;

    pTranscoder = GpuTranscode_Open(deviceStorage, pSource, pConfig, open_report, sizeof(open_report));
    if (reportCapacity > 0)
    {
        pReport[0] = 0;
    }
    Report(pReport, reportCapacity, "%s", open_report);
    if (NULL == pTranscoder)
    {
        return 0;
    }
    pTiming = Mp4Demux_Open(pSource);
    if (NULL == pTiming)
    {
        GpuTranscode_Close(pTranscoder);
        return 0;
    }

    header = GpuTranscode_GetHeader(pTranscoder, &header_size);
    FindNalOfType(header, header_size, H264_NAL_TYPE_SPS, &sps, &sps_size);
    FindNalOfType(header, header_size, H264_NAL_TYPE_PPS, &pps, &pps_size);
    video_timescale = Mp4Demux_GetTimescale(pTiming);
    total_samples   = Mp4Demux_GetSampleCount(pTiming);
    audio_samples   = Mp4Demux_GetAudioSampleCount(pTiming);
    audio_timescale = Mp4Demux_GetAudioTimescale(pTiming);
    asc             = Mp4Demux_GetAudioConfig(pTiming, &asc_size);
    has_audio       = (MP4_AUDIO_AAC == Mp4Demux_GetAudioCodec(pTiming) && audio_samples > 0 && asc_size > 0);
    if (!CanPreserveAudio(pTiming))
    {
        Report(pReport, reportCapacity, "source audio cannot be preserved; keeping the original stream\n");
        Mp4Demux_Close(pTiming);
        GpuTranscode_Close(pTranscoder);
        return 0;
    }
    full_duration = GetSourceDuration(pTiming, total_samples, video_timescale);

    pMux = Mp4Mux_CreateFragmented(pDest,
                                   GpuTranscode_GetOutputWidth(pTranscoder),
                                   GpuTranscode_GetOutputHeight(pTranscoder),
                                   sps,
                                   sps_size,
                                   pps,
                                   pps_size,
                                   video_timescale,
                                   has_audio,
                                   audio_timescale,
                                   Mp4Demux_GetAudioSampleRate(pTiming),
                                   Mp4Demux_GetAudioChannels(pTiming),
                                   asc,
                                   asc_size,
                                   full_duration);
    if (NULL == pMux)
    {
        Report(pReport, reportCapacity, "could not create the fragmented proxy MP4\n");
        Mp4Demux_Close(pTiming);
        GpuTranscode_Close(pTranscoder);
        return 0;
    }

    audio_buffer        = deviceStorage->audioBuffer;
    audio_index         = 0;
    produced            = 0;
    fragments           = 0;
    first_fragment_sent = 0;
    head_ticks          = 0;
    ok                  = TRUE;
    for (frame = 0; ok && frame < total_samples && (0 == maxFrames || frame < maxFrames); ++frame)
    {
        const byte_t*    coded;
        uint32_t         coded_size;
        int              keyframe;
        const Mp4Sample* p_video;
        uint32_t         duration;
        bool_t           cancelled;

        cancelled = FALSE;
        if (NULL != pCancel)
        {
            cancelled = !!OS_Atomic_Load32(pCancel);
        }
        if (cancelled)
        {
            ok = 0;
            break;
        }
        coded = GpuTranscode_ReadNextFrame(pTranscoder, &coded_size, &keyframe);
        if (NULL == coded)
        {
            ok = 0;
            break;
        }
        FlushBeforeKeyframe(pMux, keyframe, produced, &fragments, &first_fragment_sent,
                            head_ticks, video_timescale, onReady, pUser);
        p_video  = Mp4Demux_GetSample(pTiming, frame);
        duration = p_video->duration;
        if (!Mp4Mux_AddVideo(pMux, coded, coded_size, duration, keyframe))
        {
            ok = 0;
            break;
        }
        ++produced;
        head_ticks += duration;

        if (has_audio)
        {
            ok = CopyAudioUntil(pTiming, pMux, &audio_index, audio_buffer, (double)(p_video->decodeTime + duration) / video_timescale);
        }
    }

    if (ok)
    {
        ok = Mp4Mux_Finish(pMux); /* flushes the trailing fragment */
    }
    if (!first_fragment_sent && ok && NULL != onReady)
    {
        onReady(pUser); /* short clip: it all landed in the final fragment */
    }
    Report(pReport,
           reportCapacity,
           "fragmented proxy: %s (%u frames, %u fragments)\n",
           ok ? "ok" : "failed",
           produced,
           fragments);
    Mp4Mux_Destroy(pMux);
    Mp4Demux_Close(pTiming);
    GpuTranscode_Close(pTranscoder);
    return ok;
}
