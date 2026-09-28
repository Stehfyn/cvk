/* gpu_transcode.c - Stage 1: Vulkan Video H.264 encode/decode capability detection through cvk. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <volk.h>
#include "cvk.h"

#include "gpu_transcode.h"

/* One appended line of the human-readable report. */
static void Report(char* pReport, size_t capacity, const char* pFormat, ...)
{
    va_list arguments;
    size_t  used = strlen(pReport);

    if (used + 1 >= capacity)
    {
        return;
    }
    va_start(arguments, pFormat);
    (void)vsnprintf(pReport + used, capacity - used, pFormat, arguments);
    va_end(arguments);
}

static int HasExtension(const VkExtensionProperties* pExtensions, uint32_t count, const char* pName)
{
    uint32_t i;

    for (i = 0; i < count; ++i)
    {
        if (0 == strcmp(pExtensions[i].extensionName, pName))
        {
            return 1;
        }
    }
    return 0;
}

int GpuTranscode_Probe(GpuTranscodeCaps* pCaps, char* pReport, size_t reportCapacity)
{
    VkApplicationInfo       app;
    VkInstance              instance = VK_NULL_HANDLE;
    VkPhysicalDevice        devices[CVK_MAX_PHYSICAL_DEVICES];
    VkQueueFamilyProperties families[CVK_MAX_QUEUE_FAMILY_PROPERTIES];
    VkExtensionProperties   extensions[CVK_MAX_PROPERTIES];
    uint32_t                deviceCount;
    uint32_t                index;

    memset(pCaps, 0, sizeof(*pCaps));
    if (reportCapacity > 0)
    {
        pReport[0] = 0;
    }
    if (VK_SUCCESS != volkInitialize())
    {
        Report(pReport, reportCapacity, "no Vulkan loader present\n");
        return 0;
    }
    /* Vulkan Video encode was finalized against 1.3-era drivers. */
    cvkInitApplicationInfo(&app, NULL, "CastDemo GPU transcode", 1, "cvk", 1, VK_API_VERSION_1_3);
    cvkCreateInstance(NULL, 0, &app, 0, NULL, 0, NULL, NULL, &instance);
    volkLoadInstance(instance);

    cvkEnumeratePhysicalDevices(instance, &deviceCount, devices);
    for (index = 0; index < deviceCount; ++index)
    {
        VkPhysicalDeviceProperties2 properties = {0};
        uint32_t                    familyCount;
        uint32_t                    extensionCount;
        uint32_t                    family;
        int                         encodeFamily = -1;
        int                         decodeFamily = -1;
        int                         haveEncodeH264;
        int                         haveDecodeH264;

        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        cvkGetPhysicalDeviceProperties2(devices[index], NULL, &properties);
        if (properties.properties.apiVersion < VK_API_VERSION_1_3)
        {
            continue; /* video encode needs a 1.3 driver */
        }
        cvkGetPhysicalDeviceQueueFamilyProperties(devices[index], &familyCount, families);
        for (family = 0; family < familyCount; ++family)
        {
            if ((families[family].queueFlags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) && encodeFamily < 0)
            {
                encodeFamily = (int)family;
            }
            if ((families[family].queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) && decodeFamily < 0)
            {
                decodeFamily = (int)family;
            }
        }
        cvkEnumerateDeviceExtensionProperties(devices[index], NULL, &extensionCount, extensions);
        haveEncodeH264 = HasExtension(extensions, extensionCount, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME) && HasExtension(extensions, extensionCount, VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME) &&
                         HasExtension(extensions, extensionCount, VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME) && encodeFamily >= 0;
        haveDecodeH264 = HasExtension(extensions, extensionCount, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME) && HasExtension(extensions, extensionCount, VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME) &&
                         HasExtension(extensions, extensionCount, VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME) && decodeFamily >= 0;
        Report(pReport, reportCapacity, "%s: encode-h264 %s, decode-h264 %s\n", properties.properties.deviceName, haveEncodeH264 ? "yes" : "no", haveDecodeH264 ? "yes" : "no");
        if (!haveEncodeH264)
        {
            continue;
        }

        /* Query the H.264 encode capabilities for an 8-bit 4:2:0 Main-profile stream. */
        {
            VkVideoEncodeH264ProfileInfoKHR  h264Profile  = {0};
            VkVideoProfileInfoKHR            profile      = {0};
            VkVideoEncodeH264CapabilitiesKHR h264Caps     = {0};
            VkVideoEncodeCapabilitiesKHR     encodeCaps   = {0};
            VkVideoCapabilitiesKHR           capabilities = {0};
            VkResult                         result;

            h264Profile.sType           = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR;
            h264Profile.stdProfileIdc   = STD_VIDEO_H264_PROFILE_IDC_MAIN;
            profile.sType               = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
            profile.pNext               = &h264Profile;
            profile.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR;
            profile.chromaSubsampling   = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
            profile.lumaBitDepth        = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
            profile.chromaBitDepth      = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
            h264Caps.sType              = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_KHR;
            encodeCaps.sType            = VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR;
            encodeCaps.pNext            = &h264Caps;
            capabilities.sType          = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;

            result = cvkGetPhysicalDeviceVideoCapabilities(devices[index], &profile, &encodeCaps, &capabilities);
            if (VK_SUCCESS != result)
            {
                Report(pReport, reportCapacity, "  encode capability query failed (0x%08X)\n", (unsigned)result);
                continue;
            }
            pCaps->available           = 1;
            pCaps->canDecodeH264       = haveDecodeH264;
            pCaps->encodeQueueFamily   = (unsigned)encodeFamily;
            pCaps->decodeQueueFamily   = (unsigned)(decodeFamily >= 0 ? decodeFamily : 0);
            pCaps->minCodedWidth       = capabilities.minCodedExtent.width;
            pCaps->minCodedHeight      = capabilities.minCodedExtent.height;
            pCaps->maxCodedWidth       = capabilities.maxCodedExtent.width;
            pCaps->maxCodedHeight      = capabilities.maxCodedExtent.height;
            pCaps->maxDpbSlots         = capabilities.maxDpbSlots;
            pCaps->maxActiveReferences = capabilities.maxActiveReferencePictures;
            (void)snprintf(pCaps->deviceName, sizeof(pCaps->deviceName), "%s", properties.properties.deviceName);
            Report(pReport,
                   reportCapacity,
                   "  coded extent %ux%u..%ux%u, dpb slots %u, active refs %u\n",
                   pCaps->minCodedWidth,
                   pCaps->minCodedHeight,
                   pCaps->maxCodedWidth,
                   pCaps->maxCodedHeight,
                   pCaps->maxDpbSlots,
                   pCaps->maxActiveReferences);
            break;
        }
    }
    if (deviceCount > 0)
    {
        VkQueueFamilyProperties queueFamilies[CVK_MAX_QUEUE_FAMILY_PROPERTIES];
        uint32_t                queueFamilyCount;
        uint32_t                f;

        cvkGetPhysicalDeviceQueueFamilyProperties(devices[0], &queueFamilyCount, queueFamilies);
        for (f = 0; f < queueFamilyCount; ++f)
        {
            const VkQueueFlags q = queueFamilies[f].queueFlags;

            Report(pReport,
                   reportCapacity,
                   "  queue family %u: %u queues%s%s%s%s%s\n",
                   f,
                   queueFamilies[f].queueCount,
                   (q & VK_QUEUE_GRAPHICS_BIT) ? " graphics" : "",
                   (q & VK_QUEUE_COMPUTE_BIT) ? " compute" : "",
                   (q & VK_QUEUE_TRANSFER_BIT) ? " transfer" : "",
                   (q & VK_QUEUE_VIDEO_DECODE_BIT_KHR) ? " decode" : "",
                   (q & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) ? " encode" : "");
        }
    }
    if (pCaps->available && deviceCount > 0)
    {
        /* Report whether the NV12 video format and its planes support GPU blit (for the downscale). */
        VkFormatProperties nv12;
        VkFormatProperties r8;
        VkFormatProperties r8g8;
        const VkFlags      blit = (VkFlags)VK_FORMAT_FEATURE_BLIT_SRC_BIT | (VkFlags)VK_FORMAT_FEATURE_BLIT_DST_BIT;

        vkGetPhysicalDeviceFormatProperties(devices[0], VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, &nv12);
        vkGetPhysicalDeviceFormatProperties(devices[0], VK_FORMAT_R8_UNORM, &r8);
        vkGetPhysicalDeviceFormatProperties(devices[0], VK_FORMAT_R8G8_UNORM, &r8g8);
        Report(pReport,
               reportCapacity,
               "  blit: nv12 %s, R8 %s, R8G8 %s\n",
               (nv12.optimalTilingFeatures & blit) == blit ? "yes" : "no",
               (r8.optimalTilingFeatures & blit) == blit ? "yes" : "no",
               (r8g8.optimalTilingFeatures & blit) == blit ? "yes" : "no");
    }
    if (VK_NULL_HANDLE != instance)
    {
        cvkDestroyInstance(instance, NULL);
    }
    if (!pCaps->available)
    {
        Report(pReport, reportCapacity, "no Vulkan Video H.264 encoder on this system\n");
    }
    return pCaps->available;
}

/* ---- The full decode -> downscale -> encode proxy pipeline. ---------------------------------- */

#include <stdlib.h>

#include "mp4_demux.h"
#include "h264_params.h"
#include "gpu_device.h"
#include "gpu_decode.h"
#include "gpu_encode.h"

#define PIPELINE_SLOTS 3

/* One decoded full-resolution frame handed from the decode thread to the encode thread, in whatever
** YCbCr layout the decoder produced (interleaved 2-plane or planar 3-plane). */
typedef struct FrameSlot
{
    uint8_t* planes;       /* luma, then the chroma plane(s), allocated once */
    unsigned width;
    unsigned height;
    unsigned lumaStride;
    unsigned chromaStride;
    unsigned chromaWidth;
    unsigned chromaHeight;
    int      chromaInterleaved;
    int      sourceKeyframe;
    int      eof;          /* the producer reached the end (or failed) */
} FrameSlot;

struct GpuTranscoder
{
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
    uint8_t*           sampleBuffer;
    uint32_t           sampleCapacity;
    uint8_t*           dstLuma;
    uint8_t*           dstChroma;
    GpuTranscodeConfig config;
    uint8_t*           prevLuma;    /* previous frame's downscaled luma, for optical flow */
    int                havePrev;
    double             lastMotion;  /* 0..1 average inter-frame motion of the last frame */

    /* Decode-ahead pipeline: the producer thread decodes into a bounded ring while the caller's
    ** thread downscales and encodes, so the decode and encode engines run in parallel. */
    FrameSlot     ring[PIPELINE_SLOTS];
    HANDLE        emptySlots;  /* counts ring slots the producer may fill */
    HANDLE        filledSlots; /* counts ring slots the consumer may drain */
    HANDLE        producerThread;
    volatile LONG stopProducer;
    int           producerStarted;
    int           sawEof;
    unsigned      head;        /* producer writes here */
    unsigned      tail;        /* consumer reads here */
};

/* Area-average downscale of a decoded frame (in whatever YCbCr layout the decoder produced) into the
** encoder's NV12 input: luma to ow x oh, chroma to (ow/2) x (oh/2) interleaved. Averaging rather than
** point sampling keeps the shrunk proxy free of aliasing, and reading chroma through the frame's
** interleaved/planar flag makes the pipeline format-agnostic. */
static void DownscaleFrame(const FrameSlot* pSrc, uint8_t* pDstLuma, uint8_t* pDstChroma, unsigned ow, unsigned oh)
{
    const uint8_t* p_luma;
    const uint8_t* p_cb;
    const uint8_t* p_cr;
    unsigned       sw;
    unsigned       sh;
    unsigned       luma_stride;
    unsigned       chroma_stride;
    unsigned       src_chroma_w;
    unsigned       src_chroma_h;
    unsigned       dst_chroma_w;
    unsigned       dst_chroma_h;
    unsigned       cb_step;
    unsigned       oy;

    p_luma        = pSrc->planes;
    luma_stride   = pSrc->lumaStride;
    sw            = pSrc->width;
    sh            = pSrc->height;
    chroma_stride = pSrc->chromaStride;
    src_chroma_w  = pSrc->chromaWidth;
    src_chroma_h  = pSrc->chromaHeight;
    p_cb          = pSrc->planes + (size_t)luma_stride * sh;
    p_cr          = pSrc->chromaInterleaved ? p_cb : (p_cb + (size_t)chroma_stride * src_chroma_h);
    cb_step       = pSrc->chromaInterleaved ? 2u : 1u; /* interleaved Cb/Cr vs a planar Cb plane */

    for (oy = 0; oy < oh; ++oy)
    {
        unsigned sy0;
        unsigned sy1;
        unsigned ox;

        sy0 = oy * sh / oh;
        sy1 = (oy + 1u) * sh / oh;
        if (sy1 <= sy0)
        {
            sy1 = sy0 + 1u;
        }
        for (ox = 0; ox < ow; ++ox)
        {
            unsigned sx0;
            unsigned sx1;
            unsigned sy;
            uint32_t sum;
            uint32_t count;

            sx0   = ox * sw / ow;
            sx1   = (ox + 1u) * sw / ow;
            if (sx1 <= sx0)
            {
                sx1 = sx0 + 1u;
            }
            sum   = 0;
            count = 0;
            for (sy = sy0; sy < sy1 && sy < sh; ++sy)
            {
                unsigned sx;

                for (sx = sx0; sx < sx1 && sx < sw; ++sx)
                {
                    sum += p_luma[(size_t)sy * luma_stride + sx];
                    ++count;
                }
            }
            pDstLuma[(size_t)oy * ow + ox] = (uint8_t)(sum / (count ? count : 1u));
        }
    }

    dst_chroma_w = (ow + 1u) / 2u;
    dst_chroma_h = (oh + 1u) / 2u;
    for (oy = 0; oy < dst_chroma_h; ++oy)
    {
        unsigned cy0;
        unsigned cy1;
        unsigned ox;

        cy0 = oy * src_chroma_h / dst_chroma_h;
        cy1 = (oy + 1u) * src_chroma_h / dst_chroma_h;
        if (cy1 <= cy0)
        {
            cy1 = cy0 + 1u;
        }
        for (ox = 0; ox < dst_chroma_w; ++ox)
        {
            unsigned cx0;
            unsigned cx1;
            unsigned cy;
            uint32_t sum_cb;
            uint32_t sum_cr;
            uint32_t count;

            cx0    = ox * src_chroma_w / dst_chroma_w;
            cx1    = (ox + 1u) * src_chroma_w / dst_chroma_w;
            if (cx1 <= cx0)
            {
                cx1 = cx0 + 1u;
            }
            sum_cb = 0;
            sum_cr = 0;
            count  = 0;
            for (cy = cy0; cy < cy1 && cy < src_chroma_h; ++cy)
            {
                unsigned cx;

                for (cx = cx0; cx < cx1 && cx < src_chroma_w; ++cx)
                {
                    sum_cb += p_cb[(size_t)cy * chroma_stride + cx * cb_step];
                    sum_cr += p_cr[(size_t)cy * chroma_stride + cx * cb_step + (pSrc->chromaInterleaved ? 1u : 0u)];
                    ++count;
                }
            }
            pDstChroma[((size_t)oy * dst_chroma_w + ox) * 2u]      = (uint8_t)(sum_cb / (count ? count : 1u));
            pDstChroma[((size_t)oy * dst_chroma_w + ox) * 2u + 1u] = (uint8_t)(sum_cr / (count ? count : 1u));
        }
    }
}

void GpuTranscode_DefaultConfig(GpuTranscodeConfig* pConfig)
{
    /* The proxy is the primary stream once the session switches to it, so it has to be worth
    ** watching: HD-wide with a low quantiser, and a short GOP so seeks are still cheap. */
    pConfig->width          = 1280;
    pConfig->bitrateKbps    = 4000;
    pConfig->gopLength      = 12; /* ~0.5 s between keyframes at 24 fps, so a seek decodes little */
    pConfig->qpIntra        = 20;
    pConfig->qpInter        = 23;
    pConfig->motionAdaptive = 1;
    pConfig->motionStrength = 30; /* a gentle bit-saver on still frames, not a quality cliff */
}

/* A coarse optical-flow estimate: for a grid of blocks, search a small window for the best match in
** the previous frame and accumulate the motion magnitude, normalised to 0..1. This is the motion
** signal that drives adaptive QP (spend fewer bits when the scene barely moves) and scene-cut
** keyframes (a sudden jump in residual means a new shot, which deserves a fresh IDR). */
static double EstimateMotion(const uint8_t* pPrev, const uint8_t* pCurr, unsigned width, unsigned height, double* pResidual)
{
    const unsigned block  = 16;
    const int      search = 4;
    unsigned       by;
    uint64_t       motion_sum;
    uint64_t       residual_sum;
    unsigned       block_count;

    motion_sum   = 0;
    residual_sum = 0;
    block_count  = 0;
    for (by = 0; by + block <= height; by += block)
    {
        unsigned bx;

        for (bx = 0; bx + block <= width; bx += block)
        {
            int      best_dx;
            int      best_dy;
            uint32_t best_sad;
            unsigned yz;
            int      dy;

            /* Seed the search with the zero-motion match so flat, ambiguous blocks (which match
            ** equally at any offset) report no motion instead of the first candidate tried. */
            best_dx  = 0;
            best_dy  = 0;
            best_sad = 0;
            for (yz = 0; yz < block; yz += 2)
            {
                unsigned xz;

                for (xz = 0; xz < block; xz += 2)
                {
                    int a;
                    int b;

                    a = pCurr[(size_t)(by + yz) * width + (bx + xz)];
                    b = pPrev[(size_t)(by + yz) * width + (bx + xz)];
                    best_sad += (uint32_t)(a > b ? a - b : b - a);
                }
            }
            for (dy = -search; dy <= search; ++dy)
            {
                int dx;

                if ((int)by + dy < 0 || by + block + (unsigned)dy > height)
                {
                    continue;
                }
                for (dx = -search; dx <= search; ++dx)
                {
                    uint32_t sad;
                    unsigned y;

                    if ((int)bx + dx < 0 || bx + block + (unsigned)dx > width)
                    {
                        continue;
                    }
                    sad = 0;
                    for (y = 0; y < block; y += 2)
                    {
                        unsigned x;

                        for (x = 0; x < block; x += 2)
                        {
                            int a;
                            int b;

                            a = pCurr[(size_t)(by + y) * width + (bx + x)];
                            b = pPrev[(size_t)((int)(by + y) + dy) * width + ((int)(bx + x) + dx)];
                            sad += (uint32_t)(a > b ? a - b : b - a);
                        }
                    }
                    if (sad < best_sad)
                    {
                        best_sad = sad;
                        best_dx  = dx;
                        best_dy  = dy;
                    }
                }
            }
            motion_sum   += (uint64_t)(best_dx < 0 ? -best_dx : best_dx) + (uint64_t)(best_dy < 0 ? -best_dy : best_dy);
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
    /* Motion is the mean per-block displacement over the search radius; residual is the mean best
    ** match error per sampled pixel (0..255), used to spot scene cuts. */
    if (NULL != pResidual)
    {
        *pResidual = (double)residual_sum / block_count / (double)((block / 2) * (block / 2)) / 255.0;
    }
    return (double)motion_sum / block_count / (2.0 * search);
}

GpuTranscoder* GpuTranscode_Open(const wchar_t* pFile, const GpuTranscodeConfig* pConfig, char* pReport, size_t reportCapacity)
{
    GpuTranscoder*                   pTranscoder;
    Mp4Demux*                        p_demux;
    StdVideoH264SequenceParameterSet sps;
    StdVideoH264PictureParameterSet  pps;
    unsigned                         out_width;
    unsigned                         source_width;
    unsigned                         source_height;
    unsigned                         out_height;
    uint32_t                         sps_size;
    uint32_t                         pps_size;
    char                             device_report[256];
    char                             decode_report[512];
    char                             encode_report[512];

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
    if (!H264_ParseSps(Mp4Demux_Sps(p_demux, &sps_size), sps_size, &sps, &source_width, &source_height) || !H264_ParsePps(Mp4Demux_Pps(p_demux, &pps_size), pps_size, &pps))
    {
        Report(pReport, reportCapacity, "SPS/PPS parse failed\n");
        Mp4Demux_Close(p_demux);
        return NULL;
    }

    out_width = (pConfig->width + 1u) & ~1u;
    if (out_width < 16u)
    {
        out_width = 16u;
    }
    out_height = (unsigned)(((uint64_t)out_width * source_height + source_width / 2u) / source_width);
    out_height = (out_height + 1u) & ~1u;
    if (out_height < 2u)
    {
        out_height = 2u;
    }

    pTranscoder = (GpuTranscoder*)calloc(1, sizeof(*pTranscoder));
    if (NULL == pTranscoder)
    {
        Mp4Demux_Close(p_demux);
        return NULL;
    }
    pTranscoder->config         = *pConfig;
    pTranscoder->demux          = p_demux;
    pTranscoder->outWidth       = out_width;
    pTranscoder->outHeight      = out_height;
    pTranscoder->sourceWidth    = source_width;
    pTranscoder->sourceHeight   = source_height;
    pTranscoder->sampleCount    = Mp4Demux_SampleCount(p_demux);
    pTranscoder->sampleCapacity = 8u * 1024u * 1024u;
    pTranscoder->device         = GpuDevice_Create(1, 1, device_report, sizeof(device_report));
    Report(pReport, reportCapacity, "device: %s", device_report);
    if (NULL == pTranscoder->device)
    {
        GpuTranscode_Close(pTranscoder);
        return NULL;
    }
    pTranscoder->decoder        = GpuDecode_Create(pTranscoder->device, &sps, &pps, source_width, source_height, sps.max_num_ref_frames + 2u, Mp4Demux_NalLengthSize(p_demux), decode_report, sizeof(decode_report));
    pTranscoder->encoder        = GpuEncode_Create(pTranscoder->device, out_width, out_height, pConfig->bitrateKbps, pConfig->gopLength, pConfig->qpIntra, pConfig->qpInter, 30, 1, encode_report, sizeof(encode_report));
    pTranscoder->sampleBuffer   = (uint8_t*)malloc(pTranscoder->sampleCapacity);
    pTranscoder->dstLuma        = (uint8_t*)malloc((size_t)out_width * out_height);
    pTranscoder->dstChroma      = (uint8_t*)malloc((size_t)(((out_width + 1u) / 2u) * 2u) * ((out_height + 1u) / 2u));
    pTranscoder->prevLuma       = (uint8_t*)malloc((size_t)out_width * out_height);
    {
        unsigned slot;

        for (slot = 0; slot < PIPELINE_SLOTS; ++slot)
        {
            pTranscoder->ring[slot].planes = (uint8_t*)malloc((size_t)source_width * source_height * 2u); /* luma + chroma, 4:2:0 or 4:2:2 */
            if (NULL == pTranscoder->ring[slot].planes)
            {
                Report(pReport, reportCapacity, "transcoder ring allocation failed\n");
                GpuTranscode_Close(pTranscoder);
                return NULL;
            }
        }
    }
    pTranscoder->emptySlots  = CreateSemaphoreW(NULL, PIPELINE_SLOTS, PIPELINE_SLOTS, NULL);
    pTranscoder->filledSlots = CreateSemaphoreW(NULL, 0, PIPELINE_SLOTS, NULL);
    Report(pReport, reportCapacity, "decode: %s", decode_report);
    Report(pReport, reportCapacity, "encode: %s", encode_report);
    if (NULL == pTranscoder->decoder || NULL == pTranscoder->encoder || NULL == pTranscoder->sampleBuffer || NULL == pTranscoder->dstLuma || NULL == pTranscoder->dstChroma || NULL == pTranscoder->prevLuma ||
        NULL == pTranscoder->emptySlots || NULL == pTranscoder->filledSlots)
    {
        Report(pReport, reportCapacity, "transcoder resource allocation failed\n");
        GpuTranscode_Close(pTranscoder);
        return NULL;
    }
    Report(pReport, reportCapacity, "proxy: %ux%u from %ux%u, %u samples, qp %d/%d, gop %u, motion %s\n", out_width, out_height, source_width, source_height, pTranscoder->sampleCount, pConfig->qpIntra, pConfig->qpInter, pConfig->gopLength, pConfig->motionAdaptive ? "on" : "off");
    return pTranscoder;
}

double GpuTranscode_LastMotion(const GpuTranscoder* pTranscoder)
{
    return pTranscoder->lastMotion;
}

/* Advances the decoder to the keyframe at or before approxFrame, so a preview clip starts on a
** clean IDR. Returns the sample index it settled on. */
unsigned GpuTranscode_SeekKeyframe(GpuTranscoder* pTranscoder, unsigned approxFrame)
{
    unsigned index;

    if (approxFrame >= pTranscoder->sampleCount)
    {
        approxFrame = pTranscoder->sampleCount ? pTranscoder->sampleCount - 1u : 0u;
    }
    index = approxFrame;
    while (index > 0)
    {
        const Mp4Sample* p_sample;

        p_sample = Mp4Demux_Sample(pTranscoder->demux, index);
        if (NULL != p_sample && p_sample->keyframe)
        {
            break;
        }
        --index;
    }
    pTranscoder->sampleIndex = index;
    pTranscoder->havePrev    = 0;
    return index;
}

const uint8_t* GpuTranscode_Header(GpuTranscoder* pTranscoder, uint32_t* pSize)
{
    return GpuEncode_Header(pTranscoder->encoder, pSize);
}

/* The decode-ahead producer: reads a source sample, decodes it, and copies the full-resolution NV12
** into the next ring slot for the encode thread. Runs until the source ends or a stop is signalled.
** Decode runs on its own engine and transfer queue, so it overlaps the encode side. */
static DWORD WINAPI ProducerThread(LPVOID pParameter)
{
    GpuTranscoder* pTranscoder = (GpuTranscoder*)pParameter;

    for (;;)
    {
        FrameSlot*       p_slot;
        const Mp4Sample* p_sample;
        DecodedFrame     df;
        size_t           luma_bytes;
        size_t           chroma_bytes;
        uint32_t         bytes;

        WaitForSingleObject(pTranscoder->emptySlots, INFINITE);
        if (0 != pTranscoder->stopProducer)
        {
            break;
        }
        p_slot = &pTranscoder->ring[pTranscoder->head];
        if (pTranscoder->sampleIndex >= pTranscoder->sampleCount)
        {
            p_slot->eof       = 1;
            pTranscoder->head = (pTranscoder->head + 1u) % PIPELINE_SLOTS;
            ReleaseSemaphore(pTranscoder->filledSlots, 1, NULL);
            break;
        }
        p_sample = Mp4Demux_Sample(pTranscoder->demux, pTranscoder->sampleIndex);
        bytes    = Mp4Demux_ReadSample(pTranscoder->demux, pTranscoder->sampleIndex, pTranscoder->sampleBuffer, pTranscoder->sampleCapacity);
        ++pTranscoder->sampleIndex;
        if (0 == bytes || !GpuDecode_DecodeFrame(pTranscoder->decoder, pTranscoder->sampleBuffer, bytes))
        {
            p_slot->eof       = 1;
            pTranscoder->head = (pTranscoder->head + 1u) % PIPELINE_SLOTS;
            ReleaseSemaphore(pTranscoder->filledSlots, 1, NULL);
            break;
        }
        GpuDecode_Frame(pTranscoder->decoder, &df);
        luma_bytes   = (size_t)df.lumaStride * df.height;
        chroma_bytes = (size_t)df.chromaStride * df.chromaHeight;
        memcpy(p_slot->planes, df.pLuma, luma_bytes + (df.chromaInterleaved ? chroma_bytes : chroma_bytes * 2u));
        p_slot->width             = df.width;
        p_slot->height            = df.height;
        p_slot->lumaStride        = df.lumaStride;
        p_slot->chromaStride      = df.chromaStride;
        p_slot->chromaWidth       = df.chromaWidth;
        p_slot->chromaHeight      = df.chromaHeight;
        p_slot->chromaInterleaved = df.chromaInterleaved;
        p_slot->sourceKeyframe    = (NULL != p_sample && p_sample->keyframe) ? 1 : 0;
        p_slot->eof               = 0;
        pTranscoder->head      = (pTranscoder->head + 1u) % PIPELINE_SLOTS;
        ReleaseSemaphore(pTranscoder->filledSlots, 1, NULL);
    }
    return 0;
}

const uint8_t* GpuTranscode_NextFrame(GpuTranscoder* pTranscoder, uint32_t* pSize, int* pKeyframe)
{
    FrameSlot*     p_slot;
    int            force_idr;
    int            qp_override;
    const uint8_t* coded;

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
        *pSize             = 0;
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

        motion                  = EstimateMotion(pTranscoder->prevLuma, pTranscoder->dstLuma, pTranscoder->outWidth, pTranscoder->outHeight, &residual);
        pTranscoder->lastMotion = motion;
        bump                    = (int)((1.0 - motion) * (pTranscoder->config.motionStrength / 100.0) * 8.0 + 0.5);
        qp_override             = pTranscoder->config.qpInter + bump;
        if (qp_override > 50)
        {
            qp_override = 50;
        }
        if (!force_idr && residual > 0.18 && motion > 0.35)
        {
            force_idr = 1; /* a sudden, poorly-matching change of content: a new shot */
        }
    }
    coded = GpuEncode_Frame(pTranscoder->encoder, pTranscoder->dstLuma, pTranscoder->outWidth, pTranscoder->dstChroma, ((pTranscoder->outWidth + 1u) / 2u) * 2u, force_idr, qp_override, pSize);

    memcpy(pTranscoder->prevLuma, pTranscoder->dstLuma, (size_t)pTranscoder->outWidth * pTranscoder->outHeight);
    pTranscoder->havePrev = 1;
    if (NULL != pKeyframe)
    {
        *pKeyframe = GpuEncode_LastKeyframe(pTranscoder->encoder); /* actual IDR, incl. GOP boundaries */
    }
    pTranscoder->tail = (pTranscoder->tail + 1u) % PIPELINE_SLOTS;
    ReleaseSemaphore(pTranscoder->emptySlots, 1, NULL); /* slot free only after the downscale read it */
    return coded;
}

unsigned GpuTranscode_OutWidth(const GpuTranscoder* pTranscoder)
{
    return pTranscoder->outWidth;
}

unsigned GpuTranscode_OutHeight(const GpuTranscoder* pTranscoder)
{
    return pTranscoder->outHeight;
}

unsigned GpuTranscode_FrameCount(const GpuTranscoder* pTranscoder)
{
    return pTranscoder->sampleCount;
}

void GpuTranscode_Close(GpuTranscoder* pTranscoder)
{
    unsigned slot;

    if (NULL == pTranscoder)
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
    for (slot = 0; slot < PIPELINE_SLOTS; ++slot)
    {
        free(pTranscoder->ring[slot].planes);
    }
    free(pTranscoder->sampleBuffer);
    free(pTranscoder->dstLuma);
    free(pTranscoder->dstChroma);
    free(pTranscoder->prevLuma);
    free(pTranscoder);
}

/* ---- One-call transcode of a source MP4 to a proxy MP4 (video re-encode + audio passthrough). -- */

#include "mp4_mux.h"

/* Never replace a source with a proxy that silently drops its audio. */
static int CanPreserveAudio(const Mp4Demux* pDemux)
{
    uint32_t config_size;

    if (MP4_AUDIO_NONE == Mp4Demux_AudioCodec(pDemux))
    {
        return 1;
    }
    (void)Mp4Demux_AudioConfig(pDemux, &config_size);
    return MP4_AUDIO_AAC == Mp4Demux_AudioCodec(pDemux) && config_size > 0 &&
           Mp4Demux_AudioSampleCount(pDemux) > 0 && Mp4Demux_AudioTimescale(pDemux) > 0 &&
           Mp4Demux_AudioSampleRate(pDemux) > 0 && Mp4Demux_AudioChannels(pDemux) > 0;
}

/* Copy every audio sample starting before this video's end, including the last frame.
** A failed read/write must fail the proxy, so the session keeps the original stream. */
static int CopyAudioUntil(Mp4Demux* pDemux, Mp4Mux* pMux, unsigned* pIndex,
                          uint8_t* pBuffer, double endSeconds)
{
    const unsigned timescale = Mp4Demux_AudioTimescale(pDemux);
    const unsigned count = Mp4Demux_AudioSampleCount(pDemux);

    while (*pIndex < count)
    {
        const Mp4Sample* pSample = Mp4Demux_AudioSample(pDemux, *pIndex);
        uint32_t bytes;

        if ((double)pSample->decodeTime / timescale >= endSeconds)
        {
            break;
        }
        bytes = Mp4Demux_ReadAudioSample(pDemux, *pIndex, pBuffer, 64u * 1024u);
        if (0 == bytes || 0 == pSample->duration || !Mp4Mux_AddAudio(pMux, pBuffer, bytes, pSample->duration))
        {
            return 0;
        }
        ++*pIndex;
    }
    return 1;
}

/* Points pp/pLen at the first NAL of the given type inside an Annex-B buffer (start code stripped). */
static void FindNalOfType(const uint8_t* pData, uint32_t size, int nalType, const uint8_t** pp, uint32_t* pLen)
{
    uint32_t i;
    uint32_t nal_start;

    *pp       = NULL;
    *pLen     = 0;
    i         = 0;
    nal_start = 0;
    while (i + 3 <= size)
    {
        if (0 == pData[i] && 0 == pData[i + 1] && 1 == pData[i + 2])
        {
            if (nal_start > 0 && i > nal_start)
            {
                uint32_t nal_length;

                nal_length = i - nal_start;
                while (nal_length > 0 && 0 == pData[nal_start + nal_length - 1])
                {
                    --nal_length;
                }
                if (nal_length > 0 && (pData[nal_start] & 0x1F) == nalType)
                {
                    *pp   = pData + nal_start;
                    *pLen = nal_length;
                    return;
                }
            }
            i += 3;
            nal_start = i;
        }
        else
        {
            ++i;
        }
    }
    if (nal_start > 0 && nal_start < size && (pData[nal_start] & 0x1F) == nalType)
    {
        *pp   = pData + nal_start;
        *pLen = size - nal_start;
    }
}

int GpuTranscode_ToMp4(const wchar_t* pSource, const wchar_t* pDest, const GpuTranscodeConfig* pConfig, unsigned startFrame, unsigned maxFrames, volatile long* pCancel, char* pReport, size_t reportCapacity)
{
    GpuTranscoder* pTranscoder;
    Mp4Demux*      pTiming;
    Mp4Mux*        pMux;
    const uint8_t* header;
    const uint8_t* sps;
    const uint8_t* pps;
    const uint8_t* asc;
    uint32_t       header_size;
    uint32_t       sps_size;
    uint32_t       pps_size;
    uint32_t       asc_size;
    unsigned       video_timescale;
    unsigned       audio_timescale;
    unsigned       total_samples;
    unsigned       start_index;
    unsigned       end_index;
    unsigned       audio_samples;
    int            has_audio;
    uint8_t*       audio_buffer;
    unsigned       audio_index;
    unsigned       frame;
    unsigned       produced;
    double         motion_accum;
    char           open_report[1600];
    int            ok;

    pTranscoder = GpuTranscode_Open(pSource, pConfig, open_report, sizeof(open_report));
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

    header          = GpuTranscode_Header(pTranscoder, &header_size);
    FindNalOfType(header, header_size, 7, &sps, &sps_size);
    FindNalOfType(header, header_size, 8, &pps, &pps_size);
    video_timescale = Mp4Demux_Timescale(pTiming);
    total_samples   = Mp4Demux_SampleCount(pTiming);
    start_index     = (startFrame > 0) ? GpuTranscode_SeekKeyframe(pTranscoder, startFrame) : 0u;
    end_index       = (maxFrames > 0 && start_index + maxFrames < total_samples) ? start_index + maxFrames : total_samples;
    audio_samples   = Mp4Demux_AudioSampleCount(pTiming);
    audio_timescale = Mp4Demux_AudioTimescale(pTiming);
    asc             = Mp4Demux_AudioConfig(pTiming, &asc_size);
    has_audio       = (MP4_AUDIO_AAC == Mp4Demux_AudioCodec(pTiming) && audio_samples > 0 && asc_size > 0);

    if (!CanPreserveAudio(pTiming))
    {
        Report(pReport, reportCapacity, "source audio cannot be preserved; keeping the original stream\n");
        Mp4Demux_Close(pTiming);
        GpuTranscode_Close(pTranscoder);
        return 0;
    }

    pMux = Mp4Mux_Create(pDest,
                         GpuTranscode_OutWidth(pTranscoder),
                         GpuTranscode_OutHeight(pTranscoder),
                         sps,
                         sps_size,
                         pps,
                         pps_size,
                         video_timescale,
                         has_audio,
                         audio_timescale,
                         Mp4Demux_AudioSampleRate(pTiming),
                         Mp4Demux_AudioChannels(pTiming),
                         asc,
                         asc_size);
    if (NULL == pMux)
    {
        Report(pReport, reportCapacity, "could not create the proxy MP4\n");
        Mp4Demux_Close(pTiming);
        GpuTranscode_Close(pTranscoder);
        return 0;
    }

    audio_buffer = has_audio ? (uint8_t*)malloc(64u * 1024u) : NULL;
    audio_index  = 0;
    produced     = 0;
    motion_accum = 0.0;
    ok           = !has_audio || NULL != audio_buffer;

    /* Skip audio that precedes the (possibly seeked) start so the clip's tracks begin together. */
    if (has_audio && video_timescale > 0 && audio_timescale > 0 && start_index > 0)
    {
        const Mp4Sample* p_start;
        double           start_seconds;

        p_start       = Mp4Demux_Sample(pTiming, start_index);
        start_seconds = (NULL != p_start) ? (double)p_start->decodeTime / video_timescale : 0.0;
        while (audio_index < audio_samples)
        {
            const Mp4Sample* p_audio;

            p_audio = Mp4Demux_AudioSample(pTiming, audio_index);
            if (NULL == p_audio || (double)p_audio->decodeTime / audio_timescale >= start_seconds)
            {
                break;
            }
            ++audio_index;
        }
    }

    for (frame = start_index; ok && frame < end_index; ++frame)
    {
        const uint8_t*   coded;
        uint32_t         coded_size;
        int              keyframe;
        const Mp4Sample* p_video;
        uint32_t         duration;

        if (NULL != pCancel && 0 != *pCancel)
        {
            ok = 0;
            break;
        }
        coded = GpuTranscode_NextFrame(pTranscoder, &coded_size, &keyframe);
        if (NULL == coded)
        {
            ok = 0;
            break;
        }
        p_video  = Mp4Demux_Sample(pTiming, frame);
        duration = p_video->duration;
        if (!Mp4Mux_AddVideo(pMux, coded, coded_size, duration, keyframe))
        {
            ok = 0;
            break;
        }
        ++produced;
        motion_accum += GpuTranscode_LastMotion(pTranscoder);

        /* Interleave audio up to this frame's end time (absolute), so the mdat stays in play order. */
        if (has_audio)
        {
            ok = CopyAudioUntil(pTiming, pMux, &audio_index, audio_buffer,
                                (double)(p_video->decodeTime + duration) / video_timescale);
        }
    }

    /* Audio is interleaved up to the last video frame's time inside the loop above; audio beyond the
    ** transcoded video is intentionally dropped so the proxy's tracks end together. */
    if (ok)
    {
        ok = Mp4Mux_Finish(pMux);
    }
    Report(pReport, reportCapacity, "proxy mp4: %s, avg motion %d%% (%u video frames from %u)\n", ok ? "ok" : "failed", (int)(produced ? motion_accum / produced * 100.0 + 0.5 : 0.0), produced, start_index);
    free(audio_buffer);
    Mp4Mux_Destroy(pMux);
    Mp4Demux_Close(pTiming);
    GpuTranscode_Close(pTranscoder);
    return ok;
}

/* ---- Progressive (fragmented-MP4) transcode: playable within a fraction of a second. ---------- */

int GpuTranscode_ToFragmentedMp4(const wchar_t* pSource, const wchar_t* pDest, const GpuTranscodeConfig* pConfig, unsigned maxFrames, GpuTranscodeReadyFn onReady, void* pUser, volatile long* pCancel, char* pReport, size_t reportCapacity)
{
    GpuTranscoder*   pTranscoder;
    Mp4Demux*        pTiming;
    Mp4Mux*          pMux;
    const uint8_t*   header;
    const uint8_t*   sps;
    const uint8_t*   pps;
    const uint8_t*   asc;
    const Mp4Sample* p_last;
    uint32_t         header_size;
    uint32_t         sps_size;
    uint32_t         pps_size;
    uint32_t         asc_size;
    unsigned         video_timescale;
    unsigned         audio_timescale;
    unsigned         total_samples;
    unsigned         audio_samples;
    unsigned         audio_index;
    unsigned         frame;
    unsigned         produced;
    unsigned         fragments;
    int              has_audio;
    int              first_fragment_sent;
    uint8_t*         audio_buffer;
    double           full_duration;
    uint64_t         head_ticks;   /* video content on disk, for the pre-switch head start */
    char             open_report[1600];
    int              ok;

    pTranscoder = GpuTranscode_Open(pSource, pConfig, open_report, sizeof(open_report));
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

    header          = GpuTranscode_Header(pTranscoder, &header_size);
    FindNalOfType(header, header_size, 7, &sps, &sps_size);
    FindNalOfType(header, header_size, 8, &pps, &pps_size);
    video_timescale = Mp4Demux_Timescale(pTiming);
    total_samples   = Mp4Demux_SampleCount(pTiming);
    audio_samples   = Mp4Demux_AudioSampleCount(pTiming);
    audio_timescale = Mp4Demux_AudioTimescale(pTiming);
    asc             = Mp4Demux_AudioConfig(pTiming, &asc_size);
    has_audio       = (MP4_AUDIO_AAC == Mp4Demux_AudioCodec(pTiming) && audio_samples > 0 && asc_size > 0);
    if (!CanPreserveAudio(pTiming))
    {
        Report(pReport, reportCapacity, "source audio cannot be preserved; keeping the original stream\n");
        Mp4Demux_Close(pTiming);
        GpuTranscode_Close(pTranscoder);
        return 0;
    }
    p_last          = (total_samples > 0) ? Mp4Demux_Sample(pTiming, total_samples - 1u) : NULL;
    full_duration   = (NULL != p_last && video_timescale > 0) ? (double)(p_last->decodeTime + p_last->duration) / video_timescale : 0.0;

    pMux = Mp4Mux_CreateFragmented(pDest,
                                   GpuTranscode_OutWidth(pTranscoder),
                                   GpuTranscode_OutHeight(pTranscoder),
                                   sps,
                                   sps_size,
                                   pps,
                                   pps_size,
                                   video_timescale,
                                   has_audio,
                                   audio_timescale,
                                   Mp4Demux_AudioSampleRate(pTiming),
                                   Mp4Demux_AudioChannels(pTiming),
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

    audio_buffer        = has_audio ? (uint8_t*)malloc(64u * 1024u) : NULL;
    audio_index         = 0;
    produced            = 0;
    fragments           = 0;
    first_fragment_sent = 0;
    head_ticks          = 0;
    ok                  = !has_audio || NULL != audio_buffer;
    for (frame = 0; ok && frame < total_samples && (0 == maxFrames || frame < maxFrames); ++frame)
    {
        const uint8_t*   coded;
        uint32_t         coded_size;
        int              keyframe;
        const Mp4Sample* p_video;
        uint32_t         duration;

        if (NULL != pCancel && 0 != *pCancel)
        {
            ok = 0;
            break;
        }
        coded = GpuTranscode_NextFrame(pTranscoder, &coded_size, &keyframe);
        if (NULL == coded)
        {
            ok = 0;
            break;
        }
        /* Each fragment starts on a keyframe: close the previous one first so it is self-seekable. */
        if (keyframe && produced > 0)
        {
            if (Mp4Mux_FlushFragment(pMux))
            {
                ++fragments;
                /* Start serving only once several seconds of content are on disk: a receiver that
                ** loads a barely-started file rejects it. The transcode runs well ahead of playback,
                ** so it keeps growing past the viewer from here. */
                if (!first_fragment_sent && NULL != onReady && head_ticks >= (uint64_t)8u * video_timescale)
                {
                    first_fragment_sent = 1;
                    onReady(pUser);
                }
            }
        }
        p_video  = Mp4Demux_Sample(pTiming, frame);
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
            ok = CopyAudioUntil(pTiming, pMux, &audio_index, audio_buffer,
                                (double)(p_video->decodeTime + duration) / video_timescale);
        }
    }

    if (ok)
    {
        ok = Mp4Mux_Finish(pMux); /* flushes the trailing fragment */
        if (!first_fragment_sent && ok && NULL != onReady)
        {
            onReady(pUser); /* short clip: it all landed in the final fragment */
        }
    }
    Report(pReport, reportCapacity, "fragmented proxy: %s (%u frames, %u fragments)\n", ok ? "ok" : "failed", produced, fragments);
    free(audio_buffer);
    Mp4Mux_Destroy(pMux);
    Mp4Demux_Close(pTiming);
    GpuTranscode_Close(pTranscoder);
    return ok;
}
