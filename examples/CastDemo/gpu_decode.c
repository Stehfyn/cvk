/* gpu_decode.c - Vulkan Video H.264 decoder on cvk: device, session, DPB, and the per-frame
** decode loop with reference management. Decoded luma is read back through cvk's queue-ownership
** transfer helper. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <volk.h>
#include "cvk.h"

#include "gpu_device.h"
#include "gpu_decode.h"
#include "os/os_types.h"

#define MAX_SESSION_MEMORY 8
#define MAX_DPB_SLOTS      17
#define BITSTREAM_CAPACITY (4u * 1024u * 1024u)
#define MAX_IMAGE_PLANES   3

typedef struct DpbSlot
{
    int                             active;
    int                             frameNum;
    int                             frameNumWrap;
    int                             poc;
    unsigned                        age; /* decode-order stamp: smallest = oldest reference */
    StdVideoDecodeH264ReferenceInfo refInfo;
} DpbSlot;

struct GpuDecoder
{
    VkPhysicalDevice physical;
    VkDevice         device;
    uint32_t         decodeFamily;
    uint32_t         transferFamily;
    VkQueue          decodeQueue;
    VkQueue          transferQueue;

    /* the video profile, kept alive so image/buffer creation can chain it */
    VkVideoDecodeH264ProfileInfoKHR h264Profile;
    VkVideoProfileInfoKHR           profile;

    StdVideoH264SequenceParameterSet sps;
    StdVideoH264PictureParameterSet  pps;
    int                              nalLengthSize;

    VkVideoSessionKHR           session;
    VkVideoSessionParametersKHR parameters;
    VkDeviceMemory              sessionMemory[MAX_SESSION_MEMORY];
    uint32_t                    sessionMemoryCount;
    VkFormat                    pictureFormat;
    VkFormat                    dpbFormat;
    VkExtent2D                  codedExtent;
    unsigned                    displayWidth;
    unsigned                    displayHeight;
    unsigned                    maxDpbSlots;

    /* DPB images (reference store) and one output image (distinct mode) */
    VkImage        dpbImage[MAX_DPB_SLOTS];
    VkDeviceMemory dpbMemory[MAX_DPB_SLOTS];
    VkImageView    dpbView[MAX_DPB_SLOTS];
    DpbSlot        dpb[MAX_DPB_SLOTS];
    VkImage        outputImage;
    VkDeviceMemory outputMemory;
    VkImageView    outputView;

    VkBuffer       bitstream;
    VkDeviceMemory bitstreamMemory;
    void*          bitstreamMapped;

    VkBuffer       readback;
    VkDeviceMemory readbackMemory;
    void*          readbackMapped;

    VkCommandPool   decodePool;
    VkCommandBuffer decodeCmd;
    VkFence         decodeFence;
    VkCommandPool   transferPool;
    VkCommandBuffer transferCmd;
    VkFence         transferFence;
    VkSemaphore     decodeDone;

    VkDeviceSize bitstreamAlignment;
    int          prevPocMsb;
    int          prevPocLsb;
    int          firstFrame;
    int          lumaStride;
    unsigned     decodeOrder; /* increments per stored reference, to stamp DpbSlot.age */
};

/*************************************************************************************************/
/**
 * \par Details:
 *  Appends formatted diagnostic text within the unused portion of the report buffer.
 */
static void Report(char* pReport, size_t capacity, const char* pFormat, ...)
{
    va_list arguments;
    size_t  used = pReport ? strlen(pReport) : 0;

    if (!pReport || used + 1 >= capacity)
    {
        return;
    }
    va_start(arguments, pFormat);
    (void)vsnprintf(pReport + used, capacity - used, pFormat, arguments);
    va_end(arguments);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Creates a profile-compatible image, allocates and binds device-local memory,
 *  then creates the image view used by the decoder.
 */
static void CreateVideoImage(GpuDecoder*       pDecoder,
                             VkFormat          format,
                             VkImageUsageFlags usage,
                             VkImage*          pImage,
                             VkDeviceMemory*   pMemory,
                             VkImageView*      pView)
{
    VkExtent3D              extent;
    VkComponentMapping      identity;
    VkImageSubresourceRange range;

    cvkInitExtent3D(&extent, pDecoder->codedExtent.width, pDecoder->codedExtent.height, 1);
    cvkInitComponentMapping(&identity,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY);
    cvkInitImageSubresourceRange(&range, (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);

    cvkCreateImageWithVideoProfileListInfo(pDecoder->device,
                                           NULL,
                                           1,
                                           &pDecoder->profile,
                                           (VkImageCreateFlags)0,
                                           VK_IMAGE_TYPE_2D,
                                           format,
                                           extent,
                                           1,
                                           1,
                                           VK_SAMPLE_COUNT_1_BIT,
                                           VK_IMAGE_TILING_OPTIMAL,
                                           usage,
                                           VK_SHARING_MODE_EXCLUSIVE,
                                           0,
                                           NULL,
                                           VK_IMAGE_LAYOUT_UNDEFINED,
                                           NULL,
                                           pImage);
    cvkAllocateImageMemory(pDecoder->physical,
                           pDecoder->device,
                           *pImage,
                           (VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           NULL,
                           NULL,
                           pMemory);
    cvkBindImageMemory(pDecoder->device, *pImage, *pMemory, VK_NO_OFFSET);
    cvkCreateImageView(pDecoder->device,
                       NULL,
                       (VkImageViewCreateFlags)0,
                       *pImage,
                       VK_IMAGE_VIEW_TYPE_2D,
                       format,
                       identity,
                       range,
                       NULL,
                       pView);
}

/* Fills a picture-resource descriptor for a DPB image view. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Builds a picture-resource descriptor from the decoder's coded extent and view.
 */
static VkVideoPictureResourceInfoKHR GetPictureResource(const GpuDecoder* pDecoder, VkImageView view)
{
    VkVideoPictureResourceInfoKHR resource;

    memset(&resource, 0, sizeof(resource));
    resource.sType            = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
    resource.codedExtent      = pDecoder->codedExtent;
    resource.baseArrayLayer   = 0;
    resource.imageViewBinding = view;
    return resource;
}

/* Picture order count for pic_order_cnt_type 0, updating the decoder's running state. */
/* Picture order count for pic_order_cnt_type 0, updating the decoder running state. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Applies picture-order-count type-zero wrap handling. Updates the previous
 *  POC state only for reference pictures.
 */
static int ComputePoc(GpuDecoder* pDecoder, const H264SliceHeader* pHeader, int maxPocLsb)
{
    int lsb;
    int poc_msb;

    lsb = (int)pHeader->picOrderCntLsb;
    if (lsb < pDecoder->prevPocLsb && (pDecoder->prevPocLsb - lsb) >= maxPocLsb / 2)
    {
        poc_msb = pDecoder->prevPocMsb + maxPocLsb;
    }
    else if (lsb > pDecoder->prevPocLsb && (lsb - pDecoder->prevPocLsb) > maxPocLsb / 2)
    {
        poc_msb = pDecoder->prevPocMsb - maxPocLsb;
    }
    else
    {
        poc_msb = pDecoder->prevPocMsb;
    }
    if (pHeader->nalRefIdc != 0)
    {
        pDecoder->prevPocMsb = poc_msb;
        pDecoder->prevPocLsb = lsb;
    }
    return poc_msb + lsb;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Queries decode capabilities and formats, creates the video session and DPB,
 *  and sizes cached host readback from the selected pixel format's plane layout.
 */
GpuDecoder* GpuDecode_Create(GpuDevice*                              pGpu,
                             const StdVideoH264SequenceParameterSet* pSps,
                             const StdVideoH264PictureParameterSet*  pPps,
                             unsigned                                width,
                             unsigned                                height,
                             unsigned                                maxDpbSlots,
                             int                                     nalLengthSize,
                             char*                                   pReport,
                             size_t                                  reportCapacity)
{
    GpuDecoder*                                  pDecoder;
    VkVideoProfileListInfoKHR                    profile_list;
    VkVideoDecodeH264CapabilitiesKHR             h264_caps;
    VkVideoDecodeCapabilitiesKHR                 decode_caps;
    VkVideoCapabilitiesKHR                       capabilities;
    int                                          coincide;
    VkPhysicalDeviceVideoFormatInfoKHR           format_info;
    uint32_t                                     format_count;
    VkVideoFormatPropertiesKHR                   formats[CVK_MAX_VIDEO_FORMAT_PROPERTIES];
    VkExtensionProperties                        std_header;
    VkVideoDecodeH264SessionParametersAddInfoKHR add_info;
    uint32_t                                     i;
    VkDeviceSize                                 readback_size;

    pDecoder = (GpuDecoder*)calloc(1, sizeof(*pDecoder));
    if (NULL == pDecoder)
    {
        return NULL;
    }
    if (reportCapacity > 0)
    {
        pReport[0] = 0;
    }
    pDecoder->physical       = pGpu->physical;
    pDecoder->device         = pGpu->device;
    pDecoder->decodeFamily   = pGpu->decodeFamily;
    pDecoder->transferFamily = pGpu->transferFamily2; /* the second transfer queue: parallel to encode upload */
    pDecoder->decodeQueue    = pGpu->decodeQueue;
    pDecoder->transferQueue  = pGpu->transferQueue2;
    pDecoder->sps            = *pSps;
    pDecoder->pps            = *pPps;
    pDecoder->nalLengthSize  = nalLengthSize;
    pDecoder->displayWidth   = width;
    pDecoder->displayHeight  = height;
    pDecoder->maxDpbSlots    = maxDpbSlots < MAX_DPB_SLOTS ? maxDpbSlots : MAX_DPB_SLOTS;
    pDecoder->firstFrame     = 1;

    cvkInitVideoDecodeH264ProfileInfo(&pDecoder->h264Profile,
                                      NULL,
                                      pSps->profile_idc,
                                      VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR);
    cvkInitVideoProfileInfo(&pDecoder->profile,
                            &pDecoder->h264Profile,
                            VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
                            (VkVideoChromaSubsamplingFlagsKHR)VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
                            (VkVideoComponentBitDepthFlagsKHR)VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
                            (VkVideoComponentBitDepthFlagsKHR)VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR);
    cvkInitVideoProfileListInfo(&profile_list, NULL, 1, &pDecoder->profile);

    cvkInitVideoDecodeH264Capabilities(&h264_caps, NULL);
    memset(&decode_caps, 0, sizeof(decode_caps));
    memset(&capabilities, 0, sizeof(capabilities));
    decode_caps.sType  = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR;
    decode_caps.pNext  = &h264_caps;
    capabilities.sType = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
    if (VK_SUCCESS !=
        cvkGetPhysicalDeviceVideoCapabilities(pDecoder->physical, &pDecoder->profile, &decode_caps, &capabilities))
    {
        Report(pReport, reportCapacity, "decode not supported for this profile\n");
        GpuDecode_Destroy(pDecoder);
        return NULL;
    }
    coincide = (decode_caps.flags &
                (VkVideoDecodeCapabilityFlagsKHR)VK_VIDEO_DECODE_CAPABILITY_DPB_AND_OUTPUT_COINCIDE_BIT_KHR) != 0;
    pDecoder->bitstreamAlignment =
        capabilities.minBitstreamBufferSizeAlignment ? capabilities.minBitstreamBufferSizeAlignment : 1;

    cvkInitPhysicalDeviceVideoFormatInfo(&format_info,
                                         &profile_list,
                                         coincide
                                             ? (VkImageUsageFlags)((VkFlags)VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
                                                                   (VkFlags)VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR)
                                             : (VkImageUsageFlags)VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR);
    cvkGetPhysicalDeviceVideoFormatProperties(pDecoder->physical, &format_info, &format_count, formats);
    if (0 == format_count)
    {
        Report(pReport, reportCapacity, "no decode output format\n");
        GpuDecode_Destroy(pDecoder);
        return NULL;
    }
    pDecoder->pictureFormat = formats[0].format;
    pDecoder->dpbFormat     = formats[0].format;
    if (!coincide)
    {
        cvkInitPhysicalDeviceVideoFormatInfo(&format_info,
                                             &profile_list,
                                             (VkImageUsageFlags)VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR);
        cvkGetPhysicalDeviceVideoFormatProperties(pDecoder->physical, &format_info, &format_count, formats);
        if (0 == format_count)
        {
            Report(pReport, reportCapacity, "no decode DPB format\n");
            GpuDecode_Destroy(pDecoder);
            return NULL;
        }
        pDecoder->dpbFormat = formats[0].format;
    }
    pDecoder->codedExtent.width  = (width + 15u) & ~15u;
    pDecoder->codedExtent.height = (height + 15u) & ~15u;

    memset(&std_header, 0, sizeof(std_header));
    (void)snprintf(std_header.extensionName,
                   sizeof(std_header.extensionName),
                   "%s",
                   VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME);
    std_header.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION;

    pDecoder->sessionMemoryCount = MAX_SESSION_MEMORY;
    cvkSetupVideoSession(pDecoder->physical,
                         pDecoder->device,
                         NULL,
                         pDecoder->decodeFamily,
                         (VkVideoSessionCreateFlagsKHR)0,
                         &pDecoder->profile,
                         pDecoder->pictureFormat,
                         pDecoder->codedExtent,
                         pDecoder->dpbFormat,
                         pDecoder->maxDpbSlots,
                         pDecoder->maxDpbSlots > 1 ? pDecoder->maxDpbSlots - 1 : 1,
                         &std_header,
                         (VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         NULL,
                         NULL,
                         &pDecoder->session,
                         pDecoder->sessionMemory,
                         &pDecoder->sessionMemoryCount);

    cvkInitVideoDecodeH264SessionParametersAddInfo(&add_info, NULL, 1, &pDecoder->sps, 1, &pDecoder->pps);
    cvkCreateVideoSessionParametersWithDecodeH264Info(pDecoder->device,
                                                      NULL,
                                                      1,
                                                      1,
                                                      &add_info,
                                                      (VkVideoSessionParametersCreateFlagsKHR)0,
                                                      VK_NULL_HANDLE,
                                                      pDecoder->session,
                                                      NULL,
                                                      &pDecoder->parameters);

    /* DPB reference images, the distinct output image, and the two staging buffers. */
    for (i = 0; i < pDecoder->maxDpbSlots; ++i)
    {
        CreateVideoImage(pDecoder,
                         pDecoder->dpbFormat,
                         (VkImageUsageFlags)VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR,
                         &pDecoder->dpbImage[i],
                         &pDecoder->dpbMemory[i],
                         &pDecoder->dpbView[i]);
    }
    CreateVideoImage(pDecoder,
                     pDecoder->pictureFormat,
                     (VkImageUsageFlags)((VkFlags)VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR |
                                         (VkFlags)VK_IMAGE_USAGE_TRANSFER_SRC_BIT) |
                         (coincide ? (VkImageUsageFlags)VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR : (VkImageUsageFlags)0),
                     &pDecoder->outputImage,
                     &pDecoder->outputMemory,
                     &pDecoder->outputView);

    cvkCreateBufferWithVideoProfileListInfo(pDecoder->device,
                                            NULL,
                                            1,
                                            &pDecoder->profile,
                                            (VkBufferCreateFlags)0,
                                            BITSTREAM_CAPACITY,
                                            (VkBufferUsageFlags)VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR,
                                            VK_SHARING_MODE_EXCLUSIVE,
                                            0,
                                            NULL,
                                            NULL,
                                            &pDecoder->bitstream);
    cvkAllocateBufferMemory(pDecoder->physical,
                            pDecoder->device,
                            pDecoder->bitstream,
                            (VkMemoryPropertyFlags)((VkFlags)VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                    (VkFlags)VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                            NULL,
                            NULL,
                            &pDecoder->bitstreamMemory);
    cvkBindBufferMemory(pDecoder->device, pDecoder->bitstream, pDecoder->bitstreamMemory, VK_NO_OFFSET);
    cvkMapMemory(pDecoder->device,
                 pDecoder->bitstreamMemory,
                 VK_NO_OFFSET,
                 VK_WHOLE_SIZE,
                 (VkMemoryMapFlags)0,
                 &pDecoder->bitstreamMapped);

    pDecoder->lumaStride = (int)pDecoder->codedExtent.width;
    /* Size the readback buffer from the output pixel format itself (plane count, subsampling and
    ** per-texel bytes all derived by vku), so no bit depth or chroma layout is assumed here. */
    readback_size =
        cvkGetFormatSize(pDecoder->pictureFormat, pDecoder->codedExtent.width, pDecoder->codedExtent.height);
    cvkCreateBuffer(pDecoder->device,
                    NULL,
                    (VkBufferCreateFlags)0,
                    readback_size,
                    (VkBufferUsageFlags)VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_SHARING_MODE_EXCLUSIVE,
                    0,
                    NULL,
                    NULL,
                    &pDecoder->readback);
    /* HOST_CACHED, not just HOST_COHERENT: the CPU reads this buffer back (downscale, luma stats),
    ** and reads from write-combined coherent memory run at a few MB/s, which dominated the frame. */
    cvkAllocateBufferMemory(pDecoder->physical,
                            pDecoder->device,
                            pDecoder->readback,
                            (VkMemoryPropertyFlags)((VkFlags)VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                    (VkFlags)VK_MEMORY_PROPERTY_HOST_CACHED_BIT |
                                                    (VkFlags)VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                            NULL,
                            NULL,
                            &pDecoder->readbackMemory);
    cvkBindBufferMemory(pDecoder->device, pDecoder->readback, pDecoder->readbackMemory, VK_NO_OFFSET);
    cvkMapMemory(pDecoder->device,
                 pDecoder->readbackMemory,
                 VK_NO_OFFSET,
                 VK_WHOLE_SIZE,
                 (VkMemoryMapFlags)0,
                 &pDecoder->readbackMapped);

    cvkCreateCommandPool(pDecoder->device,
                         NULL,
                         (VkCommandPoolCreateFlags)VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                         pDecoder->decodeFamily,
                         NULL,
                         &pDecoder->decodePool);
    cvkAllocateCommandBuffers(pDecoder->device,
                              NULL,
                              pDecoder->decodePool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1,
                              &pDecoder->decodeCmd);
    cvkCreateCommandPool(pDecoder->device,
                         NULL,
                         (VkCommandPoolCreateFlags)VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
                         pDecoder->transferFamily,
                         NULL,
                         &pDecoder->transferPool);
    cvkAllocateCommandBuffers(pDecoder->device,
                              NULL,
                              pDecoder->transferPool,
                              VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                              1,
                              &pDecoder->transferCmd);
    cvkCreateFence(pDecoder->device, NULL, (VkFenceCreateFlags)0, NULL, &pDecoder->decodeFence);
    cvkCreateFence(pDecoder->device, NULL, (VkFenceCreateFlags)0, NULL, &pDecoder->transferFence);
    cvkCreateSemaphore(pDecoder->device, NULL, (VkSemaphoreCreateFlags)0, NULL, &pDecoder->decodeDone);

    Report(pReport,
           reportCapacity,
           "decode session ready: %ux%u, %s mode, fmt %d, %u dpb slots, decode qf %u, transfer qf %u\n",
           pDecoder->codedExtent.width,
           pDecoder->codedExtent.height,
           coincide ? "coincide" : "distinct",
           (int)pDecoder->pictureFormat,
           pDecoder->maxDpbSlots,
           pDecoder->decodeFamily,
           pDecoder->transferFamily);
    return pDecoder;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Derives the first plane's offset and stride from the selected output format
 *  and returns a borrowed pointer into mapped readback storage.
 */
const byte_t* GpuDecode_GetLuma(const GpuDecoder* pDecoder, unsigned* pWidth, unsigned* pHeight, unsigned* pStride)
{
    VkDeviceSize plane_offsets[MAX_IMAGE_PLANES];
    uint32_t     plane_strides[MAX_IMAGE_PLANES];
    VkExtent3D   plane_extents[MAX_IMAGE_PLANES];
    VkDeviceSize size;

    (void)cvkCalculateFormatPlaneLayouts(pDecoder->pictureFormat,
                                         pDecoder->displayWidth,
                                         pDecoder->displayHeight,
                                         1,
                                         1,
                                         1,
                                         plane_offsets,
                                         plane_strides,
                                         plane_extents,
                                         &size);
    *pWidth  = pDecoder->displayWidth;
    *pHeight = pDecoder->displayHeight;
    *pStride = plane_strides[0]; /* row bytes of the luma plane as the format defines it */
    return (const byte_t*)pDecoder->readbackMapped + plane_offsets[0];
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Derives luma/chroma plane offsets and strides from the output format and
 *  returns borrowed pointers into mapped readback storage.
 */
const byte_t* GpuDecode_GetNv12(const GpuDecoder* pDecoder,
                                 unsigned*         pWidth,
                                 unsigned*         pHeight,
                                 unsigned*         pLumaStride,
                                 const byte_t**   ppChroma,
                                 unsigned*         pChromaStride)
{
    VkDeviceSize plane_offsets[MAX_IMAGE_PLANES];
    uint32_t     plane_strides[MAX_IMAGE_PLANES];
    VkExtent3D   plane_extents[MAX_IMAGE_PLANES];
    VkDeviceSize size;

    (void)cvkCalculateFormatPlaneLayouts(pDecoder->pictureFormat,
                                         pDecoder->displayWidth,
                                         pDecoder->displayHeight,
                                         1,
                                         1,
                                         1,
                                         plane_offsets,
                                         plane_strides,
                                         plane_extents,
                                         &size);
    *pWidth        = pDecoder->displayWidth;
    *pHeight       = pDecoder->displayHeight;
    *pLumaStride   = plane_strides[0];
    *pChromaStride = plane_strides[1];
    *ppChroma      = (const byte_t*)pDecoder->readbackMapped + plane_offsets[1];
    return (const byte_t*)pDecoder->readbackMapped + plane_offsets[0];
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Exposes mapped plane pointers with format-derived strides, extents and
 *  interleaved/planar chroma metadata.
 */
void GpuDecode_GetFrame(const GpuDecoder* pDecoder, DecodedFrame* pFrame)
{
    const byte_t* base;
    uint32_t      plane_count;
    VkDeviceSize  plane_offsets[MAX_IMAGE_PLANES];
    uint32_t      plane_strides[MAX_IMAGE_PLANES];
    VkExtent3D    plane_extents[MAX_IMAGE_PLANES];
    VkDeviceSize  size;

    /* Read back the frame in whatever layout the pixel format defines: vku gives each plane's
    ** offset, row stride and subsampled extent, so no bit depth or chroma shape is hard-coded. */
    base                      = (const byte_t*)pDecoder->readbackMapped;
    plane_count               = cvkCalculateFormatPlaneLayouts(pDecoder->pictureFormat,
                                                               pDecoder->displayWidth,
                                                               pDecoder->displayHeight,
                                                               1,
                                                               1,
                                                               1,
                                                               plane_offsets,
                                                               plane_strides,
                                                               plane_extents,
                                                               &size);
    pFrame->width             = pDecoder->displayWidth;
    pFrame->height            = pDecoder->displayHeight;
    pFrame->lumaStride        = plane_strides[0];
    pFrame->chromaWidth       = plane_extents[1].width;
    pFrame->chromaHeight      = plane_extents[1].height;
    pFrame->chromaStride      = plane_strides[1];
    pFrame->chromaInterleaved = (plane_count == 2) ? 1 : 0;
    pFrame->pLuma             = base + plane_offsets[0];
    pFrame->pCb               = base + plane_offsets[1];
    pFrame->pCr               = (plane_count == 3) ? (base + plane_offsets[2]) : NULL;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Collects coded slices from length-prefixed NAL units, updates reference state,
 *  records decode and readback work, then applies sliding-window reference marking.
 */
int GpuDecode_DecodeFrame(GpuDecoder* pDecoder, const byte_t* pSample, uint32_t size)
{
    uint32_t                        offset;
    byte_t*                         p_stream;
    uint32_t                        stream_length;
    uint32_t                        slice_count;
    const byte_t*                   p_first_slice;
    uint32_t                        first_slice_size;
    uint32_t                        slice_offsets[64];
    H264SliceHeader                 header;
    int                             max_poc_lsb;
    uint32_t                        i;
    int                             poc;
    int                             current_slot;
    uint32_t                        begin_count;
    uint32_t                        decode_ref_count;
    VkVideoPictureResourceInfoKHR   ref_resource[MAX_DPB_SLOTS];
    VkVideoDecodeH264DpbSlotInfoKHR dpb_slot_info[MAX_DPB_SLOTS];
    VkVideoReferenceSlotInfoKHR     decode_refs[MAX_DPB_SLOTS];
    VkVideoReferenceSlotInfoKHR     begin_slots[MAX_DPB_SLOTS + 1];
    VkVideoPictureResourceInfoKHR   setup_resource;
    StdVideoDecodeH264ReferenceInfo setup_ref_info;
    VkVideoDecodeH264DpbSlotInfoKHR setup_dpb_info;
    VkVideoReferenceSlotInfoKHR     setup_slot_begin;
    VkVideoReferenceSlotInfoKHR     setup_slot_decode;
    VkImageSubresourceRange         color;
    StdVideoDecodeH264PictureInfo   std_picture;
    VkVideoPictureResourceInfoKHR   dst_resource;
    VkDeviceSize                    aligned_range;
    VkSubmitInfo                    submit;
    VkOffset3D                      zero_offset;
    VkExtent3D                      display_extent;
    VkImageSubresourceLayers        base_layers;
    uint32_t                        region_count;
    VkBufferImageCopy               copy_regions[MAX_IMAGE_PLANES];
    VkDeviceSize                    transfer_bytes;
    VkImageAspectFlags              aspect_mask;
    VkPipelineStageFlags            wait_stage;
    DpbSlot*                        p_slot;
    unsigned                        max_refs;
    unsigned                        active_refs;
    int                             oldest_slot;

    offset           = 0;
    p_stream         = (byte_t*)pDecoder->bitstreamMapped;
    stream_length    = 0;
    slice_count      = 0;
    p_first_slice    = NULL;
    first_slice_size = 0;

    /* Walk the AVCC NAL units: keep coded slices, and note the first slice header. */
    for (;;)
    {
        uint32_t nal_size;
        int      b;
        int      nal_type;
        bool_t   has_nal;
        bool_t   has_prefix_space;
        bool_t   has_payload_space;
        bool_t   has_slice_slot;

        if ((uint32_t)pDecoder->nalLengthSize > (size - offset))
        {
            break;
        }
        nal_size = 0;
        for (b = 0; b < pDecoder->nalLengthSize; ++b)
        {
            nal_size = (nal_size << 8) | pSample[offset + b];
        }
        offset  += pDecoder->nalLengthSize;
        has_nal  = (0 < nal_size) && (nal_size <= (size - offset));
        if (!has_nal)
        {
            break;
        }
        nal_type = pSample[offset] & 0x1F;
        if (nal_type == 1 || nal_type == 5)
        {
            has_prefix_space  = (4 <= (BITSTREAM_CAPACITY - stream_length));
            has_payload_space = has_prefix_space && (nal_size <= (BITSTREAM_CAPACITY - stream_length - 4));
            has_slice_slot    = (slice_count < 64);
            if (!has_payload_space || !has_slice_slot)
            {
                break;
            }
            if (NULL == p_first_slice)
            {
                p_first_slice    = pSample + offset;
                first_slice_size = nal_size;
            }
            slice_offsets[slice_count++] = stream_length;
            p_stream[stream_length++]    = 0;
            p_stream[stream_length++]    = 0;
            p_stream[stream_length++]    = 0;
            p_stream[stream_length++]    = 1;
            memcpy(p_stream + stream_length, pSample + offset, nal_size);
            stream_length += nal_size;
        }
        offset += nal_size;
    }
    if (NULL == p_first_slice || 0 == slice_count)
    {
        return 0;
    }
    if (!H264_ParseSliceHeader(p_first_slice, first_slice_size, &pDecoder->sps, &pDecoder->pps, &header))
    {
        return 0;
    }

    /* Picture order count (type 0), resetting the DPB at an IDR. */
    max_poc_lsb = 1 << (pDecoder->sps.log2_max_pic_order_cnt_lsb_minus4 + 4);
    if (header.isIdr)
    {
        pDecoder->prevPocMsb = 0;
        pDecoder->prevPocLsb = 0;
        for (i = 0; i < pDecoder->maxDpbSlots; ++i)
        {
            pDecoder->dpb[i].active = 0;
        }
    }
    poc = ComputePoc(pDecoder, &header, max_poc_lsb);

    /* Pick a free DPB slot for the reconstructed picture. Sliding-window marking below keeps the
    ** active reference count at most max_num_ref_frames, and the DPB has max_num_ref_frames + 2
    ** slots, so a free slot for the current picture always exists. */
    current_slot = -1;
    for (i = 0; i < pDecoder->maxDpbSlots; ++i)
    {
        if (!pDecoder->dpb[i].active)
        {
            current_slot = (int)i;
            break;
        }
    }
    if (current_slot < 0)
    {
        current_slot = 0; /* the sliding window freed none; overwrite slot 0 */
    }

    /* Reference slots for the coding scope: active references, then the reconstructed slot. */
    begin_count      = 0;
    decode_ref_count = 0;
    for (i = 0; i < pDecoder->maxDpbSlots; ++i)
    {
        if (!pDecoder->dpb[i].active || (int)i == current_slot)
        {
            continue;
        }
        ref_resource[decode_ref_count] = GetPictureResource(pDecoder, pDecoder->dpbView[i]);
        cvkInitVideoDecodeH264DpbSlotInfo(&dpb_slot_info[decode_ref_count], NULL, &pDecoder->dpb[i].refInfo);
        cvkInitVideoReferenceSlotInfo(&decode_refs[decode_ref_count],
                                      &dpb_slot_info[decode_ref_count],
                                      (int32_t)i,
                                      &ref_resource[decode_ref_count]);
        begin_slots[begin_count++] = decode_refs[decode_ref_count];
        ++decode_ref_count;
    }

    /* The reconstructed slot: begin is given it inactive (slotIndex -1); the decode setup slot
    ** carries its H.264 reference info and the target slot index. */
    setup_resource = GetPictureResource(pDecoder, pDecoder->dpbView[current_slot]);
    memset(&setup_ref_info, 0, sizeof(setup_ref_info));
    setup_ref_info.FrameNum       = (uint16_t)header.frameNum;
    setup_ref_info.PicOrderCnt[0] = poc;
    setup_ref_info.PicOrderCnt[1] = poc;
    cvkInitVideoDecodeH264DpbSlotInfo(&setup_dpb_info, NULL, &setup_ref_info);
    cvkInitVideoReferenceSlotInfo(&setup_slot_begin, NULL, -1, &setup_resource);
    cvkInitVideoReferenceSlotInfo(&setup_slot_decode, &setup_dpb_info, current_slot, &setup_resource);
    begin_slots[begin_count++] = setup_slot_begin;

    /* Record the decode: bring the DPB and output images into their decode layouts, decode into
    ** the output image, then release it to the transfer queue. */
    cvkInitImageSubresourceRange(&color, (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
    cvkBeginOneTimeCommandBuffer(pDecoder->decodeCmd, NULL);
    cvkCmdImageBarrier2(pDecoder->decodeCmd,
                        NULL,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                        (VkAccessFlags2)0,
                        VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
                        (VkAccessFlags2)VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR,
                        pDecoder->outputImage,
                        color,
                        (VkDependencyFlags)0,
                        NULL);
    for (i = 0; i < pDecoder->maxDpbSlots; ++i)
    {
        cvkCmdImageBarrier2(pDecoder->decodeCmd,
                            NULL,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                            (VkAccessFlags2)0,
                            VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
                            (VkAccessFlags2)((VkFlags64)VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR |
                                             (VkFlags64)VK_ACCESS_2_VIDEO_DECODE_READ_BIT_KHR),
                            pDecoder->dpb[i].active ? VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_VIDEO_DECODE_DPB_KHR,
                            pDecoder->dpbImage[i],
                            color,
                            (VkDependencyFlags)0,
                            NULL);
    }

    cvkCmdBeginVideoCoding(pDecoder->decodeCmd,
                           NULL,
                           (VkVideoBeginCodingFlagsKHR)0,
                           pDecoder->session,
                           pDecoder->parameters,
                           begin_count,
                           begin_slots);
    if (pDecoder->firstFrame)
    {
        cvkCmdControlVideoCoding(pDecoder->decodeCmd,
                                 NULL,
                                 (VkVideoCodingControlFlagsKHR)VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR);
        pDecoder->firstFrame = 0;
    }

    memset(&std_picture, 0, sizeof(std_picture));
    std_picture.seq_parameter_set_id = pDecoder->sps.seq_parameter_set_id;
    std_picture.pic_parameter_set_id = pDecoder->pps.pic_parameter_set_id;
    std_picture.frame_num            = (uint16_t)header.frameNum;
    std_picture.idr_pic_id           = (uint16_t)header.idrPicId;
    std_picture.PicOrderCnt[0]       = poc;
    std_picture.PicOrderCnt[1]       = poc;
    std_picture.flags.IdrPicFlag     = header.isIdr ? 1u : 0u;
    std_picture.flags.is_reference   = header.nalRefIdc ? 1u : 0u;
    std_picture.flags.is_intra       = (header.sliceType % 5 == 2) ? 1u : 0u;

    dst_resource = GetPictureResource(pDecoder, pDecoder->outputView);
    /* The bitstream range must be a multiple of the codec alignment; pad within the buffer. */
    aligned_range = (stream_length + pDecoder->bitstreamAlignment - 1) & ~(pDecoder->bitstreamAlignment - 1);
    if (aligned_range > BITSTREAM_CAPACITY)
    {
        aligned_range = BITSTREAM_CAPACITY;
    }
    memset((byte_t*)pDecoder->bitstreamMapped + stream_length, 0, (size_t)(aligned_range - stream_length));

    cvkCmdDecodeVideoWithH264PictureInfo(pDecoder->decodeCmd,
                                         NULL,
                                         &std_picture,
                                         slice_count,
                                         slice_offsets,
                                         (VkVideoDecodeFlagsKHR)0,
                                         pDecoder->bitstream,
                                         VK_NO_OFFSET,
                                         aligned_range,
                                         dst_resource,
                                         &setup_slot_decode,
                                         decode_ref_count,
                                         decode_refs);
    cvkCmdEndVideoCoding(pDecoder->decodeCmd, NULL, (VkVideoEndCodingFlagsKHR)0);

    cvkCmdImageQueueOwnershipBarrier2(pDecoder->decodeCmd,
                                      NULL,
                                      VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR,
                                      (VkAccessFlags2)VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR,
                                      VK_PIPELINE_STAGE_2_NONE,
                                      (VkAccessFlags2)0,
                                      VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR,
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                      pDecoder->decodeFamily,
                                      pDecoder->transferFamily,
                                      pDecoder->outputImage,
                                      color,
                                      (VkDependencyFlags)0,
                                      NULL);
    cvkEndCommandBuffer(pDecoder->decodeCmd);

    cvkInitSubmitInfo(&submit, NULL, 0, NULL, NULL, 1, &pDecoder->decodeCmd, 1, &pDecoder->decodeDone);
    cvkResetFences(pDecoder->device, 1, &pDecoder->decodeFence);
    cvkQueueSubmit(pDecoder->decodeQueue, 1, &submit, pDecoder->decodeFence);

    /* Transfer queue: acquire the output image and copy every plane into the readback buffer. */
    cvkInitOffset3D(&zero_offset, 0, 0, 0);
    cvkInitExtent3D(&display_extent, pDecoder->displayWidth, pDecoder->displayHeight, 1);
    cvkBeginOneTimeCommandBuffer(pDecoder->transferCmd, NULL);
    cvkCmdImageQueueOwnershipBarrier2(pDecoder->transferCmd,
                                      NULL,
                                      VK_PIPELINE_STAGE_2_NONE,
                                      (VkAccessFlags2)0,
                                      VK_PIPELINE_STAGE_2_COPY_BIT,
                                      (VkAccessFlags2)VK_ACCESS_2_TRANSFER_READ_BIT,
                                      VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR,
                                      VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                      pDecoder->decodeFamily,
                                      pDecoder->transferFamily,
                                      pDecoder->outputImage,
                                      color,
                                      (VkDependencyFlags)0,
                                      NULL);
    /* Build one buffer->image copy per plane straight from the pixel format: vku derives each
    ** plane's aspect, subsampled extent, row stride and per-texel bytes, so a tightly packed
    ** readback works for any decoder output format without assuming NV12 or a bit depth. */
    base_layers  = cvkGetFormatBaseSubresourceLayers(pDecoder->pictureFormat);
    region_count = cvkGetBufferImageCopyRegions(VK_NO_OFFSET,
                                                1,
                                                pDecoder->pictureFormat,
                                                zero_offset,
                                                display_extent,
                                                base_layers,
                                                copy_regions,
                                                &transfer_bytes,
                                                &aspect_mask);
    cvkCmdCopyImageToBuffer(pDecoder->transferCmd,
                            pDecoder->outputImage,
                            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                            pDecoder->readback,
                            region_count,
                            copy_regions);
    cvkEndCommandBuffer(pDecoder->transferCmd);

    wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    cvkInitSubmitInfo(&submit, NULL, 1, &pDecoder->decodeDone, &wait_stage, 1, &pDecoder->transferCmd, 0, NULL);
    cvkResetFences(pDecoder->device, 1, &pDecoder->transferFence);
    cvkQueueSubmit(pDecoder->transferQueue, 1, &submit, pDecoder->transferFence);
    cvkWaitForFence(pDecoder->device, pDecoder->transferFence, VK_TRUE, UINT64_MAX);

    /* Update the DPB: activate the reconstructed slot when it is a reference. */
    if (header.nalRefIdc != 0)
    {
        p_slot           = &pDecoder->dpb[current_slot];
        p_slot->active   = 1;
        p_slot->frameNum = (int)header.frameNum;
        p_slot->poc      = poc;
        p_slot->age      = ++pDecoder->decodeOrder;

        /* Sliding-window reference marking (H.264 8.2.5.3), applied after the picture is stored:
        ** a stream keeps at most max_num_ref_frames short-term references, so retire the oldest
        ** (smallest decode-order age) until the count is within that bound. Without this the DPB
        ** filled and later frames referenced stale pictures instead of the most recent one, which
        ** corrupted decode a few frames into every GOP. */
        max_refs = pDecoder->sps.max_num_ref_frames ? pDecoder->sps.max_num_ref_frames : 1u;
        for (;;)
        {
            active_refs = 0;
            for (i = 0; i < pDecoder->maxDpbSlots; ++i)
            {
                active_refs += pDecoder->dpb[i].active ? 1u : 0u;
            }
            if (active_refs <= max_refs)
            {
                break;
            }
            oldest_slot = -1;
            for (i = 0; i < pDecoder->maxDpbSlots; ++i)
            {
                if (pDecoder->dpb[i].active &&
                    (oldest_slot < 0 || pDecoder->dpb[i].age < pDecoder->dpb[oldest_slot].age))
                {
                    oldest_slot = (int)i;
                }
            }
            if (oldest_slot < 0)
            {
                break;
            }
            pDecoder->dpb[oldest_slot].active = 0;
        }
        memset(&p_slot->refInfo, 0, sizeof(p_slot->refInfo));
        p_slot->refInfo.FrameNum       = (uint16_t)header.frameNum;
        p_slot->refInfo.PicOrderCnt[0] = poc;
        p_slot->refInfo.PicOrderCnt[1] = poc;
    }
    return 1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Waits for device work before releasing decoder-owned video, image, buffer
 *  and synchronization resources. The shared GPU device remains owned elsewhere.
 */
void GpuDecode_Destroy(GpuDecoder* pDecoder)
{
    uint32_t i;

    if (NULL == pDecoder)
    {
        return;
    }
    if (VK_NULL_HANDLE != pDecoder->device)
    {
        cvkDeviceWaitIdle(pDecoder->device);
        if (VK_NULL_HANDLE != pDecoder->decodeDone)
        {
            cvkDestroySemaphore(pDecoder->device, pDecoder->decodeDone, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->decodeFence)
        {
            cvkDestroyFence(pDecoder->device, pDecoder->decodeFence, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->transferFence)
        {
            cvkDestroyFence(pDecoder->device, pDecoder->transferFence, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->decodePool)
        {
            cvkDestroyCommandPool(pDecoder->device, pDecoder->decodePool, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->transferPool)
        {
            cvkDestroyCommandPool(pDecoder->device, pDecoder->transferPool, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->readback)
        {
            cvkDestroyBuffer(pDecoder->device, pDecoder->readback, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->readbackMemory)
        {
            cvkFreeMemory(pDecoder->device, pDecoder->readbackMemory, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->bitstream)
        {
            cvkDestroyBuffer(pDecoder->device, pDecoder->bitstream, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->bitstreamMemory)
        {
            cvkFreeMemory(pDecoder->device, pDecoder->bitstreamMemory, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->outputView)
        {
            cvkDestroyImageView(pDecoder->device, pDecoder->outputView, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->outputImage)
        {
            cvkDestroyImage(pDecoder->device, pDecoder->outputImage, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->outputMemory)
        {
            cvkFreeMemory(pDecoder->device, pDecoder->outputMemory, NULL);
        }
        for (i = 0; i < pDecoder->maxDpbSlots; ++i)
        {
            if (VK_NULL_HANDLE != pDecoder->dpbView[i])
            {
                cvkDestroyImageView(pDecoder->device, pDecoder->dpbView[i], NULL);
            }
            if (VK_NULL_HANDLE != pDecoder->dpbImage[i])
            {
                cvkDestroyImage(pDecoder->device, pDecoder->dpbImage[i], NULL);
            }
            if (VK_NULL_HANDLE != pDecoder->dpbMemory[i])
            {
                cvkFreeMemory(pDecoder->device, pDecoder->dpbMemory[i], NULL);
            }
        }
        if (VK_NULL_HANDLE != pDecoder->parameters)
        {
            cvkDestroyVideoSessionParameters(pDecoder->device, pDecoder->parameters, NULL);
        }
        if (VK_NULL_HANDLE != pDecoder->session)
        {
            cvkDestroyVideoSession(pDecoder->device, pDecoder->session, NULL);
        }
        for (i = 0; i < pDecoder->sessionMemoryCount; ++i)
        {
            if (VK_NULL_HANDLE != pDecoder->sessionMemory[i])
            {
                cvkFreeMemory(pDecoder->device, pDecoder->sessionMemory[i], NULL);
            }
        }
        /* The device and instance belong to the shared GpuDevice, not to the decoder. */
    }
    free(pDecoder);
}
