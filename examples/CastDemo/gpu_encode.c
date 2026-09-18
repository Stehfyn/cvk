/* gpu_encode.c - Vulkan Video H.264 encode session on cvk: device, session with generated SPS/PPS
** and rate control, per-frame encode of downscaled frames, and bitstream read-back. The input
** picture format is whatever the driver advertises for encode source images, so the per-frame
** upload is driven by that format's plane layout rather than assuming one pixel format. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <volk.h>
#include "cvk.h"

#include "gpu_device.h"
#include "gpu_encode.h"

#define MAX_SESSION_MEMORY 8
#define MAX_HEADER_BYTES   1024u
#define ENCODE_DPB_SLOTS   2
#define MAX_PLANES         3
#define BITSTREAM_CAPACITY (4u * 1024u * 1024u)

struct GpuEncoder
{
    VkPhysicalDevice physical;
    VkDevice         device;
    uint32_t         encodeFamily;
    uint32_t         transferFamily;
    VkQueue          encodeQueue;
    VkQueue          transferQueue;

    VkVideoEncodeH264ProfileInfoKHR h264Profile;
    VkVideoProfileInfoKHR           profile;

    StdVideoH264SequenceParameterSet sps;
    StdVideoH264PictureParameterSet  pps;

    VkVideoSessionKHR           session;
    VkVideoSessionParametersKHR parameters;
    VkDeviceMemory              sessionMemory[MAX_SESSION_MEMORY];
    uint32_t                    sessionMemoryCount;
    VkFormat                    inputFormat;
    VkFormat                    dpbFormat;
    VkExtent2D                  codedExtent;
    unsigned                    width;
    unsigned                    height;

    /* Encode input picture, the reconstructed-picture DPB, and their views. */
    VkImage        inputImage;
    VkDeviceMemory inputMemory;
    VkImageView    inputView;
    /* One DPB image with a layer per slot: this GPU lacks separate reference images, so every
    ** reference slot must be a view of the same image. */
    VkImage        dpbImage;
    VkDeviceMemory dpbMemory;
    VkImageView    dpbView[ENCODE_DPB_SLOTS];
    int            dpbValid[ENCODE_DPB_SLOTS];

    VkBuffer       staging;
    VkDeviceMemory stagingMemory;
    void*          stagingMapped;
    VkDeviceSize   stagingSize;

    VkBuffer       bitstream;
    VkDeviceMemory bitstreamMemory;
    void*          bitstreamMapped;
    VkDeviceSize   bitstreamAlignment;

    VkQueryPool feedbackPool;

    VkCommandPool   encodePool;
    VkCommandBuffer encodeCmd;
    VkFence         encodeFence;
    VkCommandPool   transferPool;
    VkCommandBuffer transferCmd;
    VkFence         transferFence;
    VkSemaphore     uploadDone;

    uint32_t frameIndex;
    uint32_t framesSinceIdr;
    uint32_t idrPicId;
    uint32_t gopLength;
    int      lastKeyframe;  /* whether the most recent GpuEncode_Frame produced an IDR */
    int      rateControlReset;
    int      qpIntra;
    int      qpInter;

    uint8_t  header[MAX_HEADER_BYTES];
    uint32_t headerSize;
};

static void Report(char* pReport, size_t capacity, const char* pFormat, ...)
{
    va_list arguments;
    size_t  used;

    used = pReport ? strlen(pReport) : 0;
    if (!pReport || used + 1 >= capacity)
    {
        return;
    }
    va_start(arguments, pFormat);
    (void)vsnprintf(pReport + used, capacity - used, pFormat, arguments);
    va_end(arguments);
}

/* Creates a video image (input picture or DPB) with the given number of array layers, bound to
** device-local memory. Views are created separately so the DPB can expose a view per layer. */
static void CreateVideoImage(GpuEncoder* pEncoder, VkFormat format, VkImageUsageFlags usage, uint32_t arrayLayers, VkImage* pImage, VkDeviceMemory* pMemory)
{
    VkExtent3D extent;

    cvkInitExtent3D(&extent, pEncoder->codedExtent.width, pEncoder->codedExtent.height, 1);
    cvkCreateImageWithVideoProfileListInfo(pEncoder->device,
                                           NULL,
                                           1,
                                           &pEncoder->profile,
                                           (VkImageCreateFlags)0,
                                           VK_IMAGE_TYPE_2D,
                                           format,
                                           extent,
                                           1,
                                           arrayLayers,
                                           VK_SAMPLE_COUNT_1_BIT,
                                           VK_IMAGE_TILING_OPTIMAL,
                                           usage,
                                           VK_SHARING_MODE_EXCLUSIVE,
                                           0,
                                           NULL,
                                           VK_IMAGE_LAYOUT_UNDEFINED,
                                           NULL,
                                           pImage);
    cvkAllocateImageMemory(pEncoder->physical, pEncoder->device, *pImage, (VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, NULL, NULL, pMemory);
    cvkBindImageMemory(pEncoder->device, *pImage, *pMemory, VK_NO_OFFSET);
}

/* Creates a single-layer color view of an image at the given base array layer. */
static void CreateLayerView(GpuEncoder* pEncoder, VkImage image, VkFormat format, uint32_t baseArrayLayer, VkImageView* pView)
{
    VkComponentMapping      identity;
    VkImageSubresourceRange range;

    cvkInitComponentMapping(&identity, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY);
    cvkInitImageSubresourceRange(&range, (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, baseArrayLayer, 1);
    cvkCreateImageView(pEncoder->device, NULL, (VkImageViewCreateFlags)0, image, VK_IMAGE_VIEW_TYPE_2D, format, identity, range, NULL, pView);
}

/* Fills a Main-profile SPS/PPS for the proxy: 4:2:0, progressive frames, one short-term reference,
** CAVLC, no scaling matrices. The driver serializes these to the sequence header. */
static void GenerateParameters(GpuEncoder* pEncoder)
{
    StdVideoH264SequenceParameterSet* pSps;
    StdVideoH264PictureParameterSet*  pPps;

    pSps = &pEncoder->sps;
    memset(pSps, 0, sizeof(*pSps));
    pSps->profile_idc                       = STD_VIDEO_H264_PROFILE_IDC_MAIN;
    pSps->level_idc                         = STD_VIDEO_H264_LEVEL_IDC_4_1;
    pSps->chroma_format_idc                 = STD_VIDEO_H264_CHROMA_FORMAT_IDC_420;
    pSps->seq_parameter_set_id              = 0;
    pSps->log2_max_frame_num_minus4         = 4;
    pSps->pic_order_cnt_type                = STD_VIDEO_H264_POC_TYPE_0;
    pSps->log2_max_pic_order_cnt_lsb_minus4 = 4;
    pSps->max_num_ref_frames                = 1;
    pSps->pic_width_in_mbs_minus1           = (pEncoder->codedExtent.width / 16u) - 1u;
    pSps->pic_height_in_map_units_minus1    = (pEncoder->codedExtent.height / 16u) - 1u;
    pSps->flags.frame_mbs_only_flag         = 1;
    pSps->flags.direct_8x8_inference_flag   = 1;
    if ((pEncoder->height & 15u) != 0)
    {
        pSps->flags.frame_cropping_flag = 1;
        pSps->frame_crop_bottom_offset  = (pEncoder->codedExtent.height - pEncoder->height) / 2u;
    }
    if ((pEncoder->width & 15u) != 0)
    {
        pSps->flags.frame_cropping_flag = 1;
        pSps->frame_crop_right_offset   = (pEncoder->codedExtent.width - pEncoder->width) / 2u;
    }

    pPps = &pEncoder->pps;
    memset(pPps, 0, sizeof(*pPps));
    pPps->seq_parameter_set_id                         = 0;
    pPps->pic_parameter_set_id                         = 0;
    pPps->num_ref_idx_l0_default_active_minus1         = 0;
    pPps->pic_init_qp_minus26                          = 0;
    pPps->flags.deblocking_filter_control_present_flag = 1;
    pPps->flags.entropy_coding_mode_flag               = 0;
}

GpuEncoder* GpuEncode_Create(GpuDevice* pGpu, unsigned width, unsigned height, unsigned bitrateKbps, unsigned gopLength, int qpIntra, int qpInter, unsigned frameRateNum, unsigned frameRateDen, char* pReport, size_t reportCapacity)
{
    GpuEncoder*                                       pEncoder;
    VkVideoProfileListInfoKHR                         profile_list;
    VkPhysicalDeviceVideoFormatInfoKHR                format_info;
    VkVideoFormatPropertiesKHR                        formats[CVK_MAX_VIDEO_FORMAT_PROPERTIES];
    VkVideoEncodeH264CapabilitiesKHR                  h264_caps;
    VkVideoEncodeCapabilitiesKHR                      encode_caps;
    VkVideoCapabilitiesKHR                            capabilities;
    VkVideoEncodeH264SessionCreateInfoKHR             session_create;
    VkExtensionProperties                             std_header;
    VkVideoEncodeH264SessionParametersAddInfoKHR      add_info;
    VkVideoEncodeH264SessionParametersGetInfoKHR      get_info;
    VkVideoEncodeH264SessionParametersFeedbackInfoKHR h264_feedback;
    uint32_t                                          format_count;
    uint32_t                                          i;
    size_t                                            header_size;
    VkResult                                          result;

    (void)bitrateKbps;
    (void)frameRateNum;
    (void)frameRateDen;
    pEncoder = (GpuEncoder*)calloc(1, sizeof(*pEncoder));
    if (NULL == pEncoder)
    {
        return NULL;
    }
    if (reportCapacity > 0)
    {
        pReport[0] = 0;
    }
    pEncoder->physical           = pGpu->physical;
    pEncoder->device             = pGpu->device;
    pEncoder->encodeFamily       = pGpu->encodeFamily;
    pEncoder->transferFamily     = pGpu->transferFamily;
    pEncoder->encodeQueue        = pGpu->encodeQueue;
    pEncoder->transferQueue      = pGpu->transferQueue;
    pEncoder->width              = width;
    pEncoder->height             = height;
    pEncoder->codedExtent.width  = (width + 15u) & ~15u;
    pEncoder->codedExtent.height = (height + 15u) & ~15u;
    pEncoder->gopLength          = gopLength ? gopLength : 12u;
    pEncoder->qpIntra            = (qpIntra > 0 && qpIntra < 52) ? qpIntra : 24;
    pEncoder->qpInter            = (qpInter > 0 && qpInter < 52) ? qpInter : 26;
    pEncoder->rateControlReset   = 1;

    cvkInitVideoEncodeH264ProfileInfo(&pEncoder->h264Profile, NULL, STD_VIDEO_H264_PROFILE_IDC_MAIN);
    cvkInitVideoProfileInfo(&pEncoder->profile,
                            &pEncoder->h264Profile,
                            VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR,
                            (VkVideoChromaSubsamplingFlagsKHR)VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
                            (VkVideoComponentBitDepthFlagsKHR)VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
                            (VkVideoComponentBitDepthFlagsKHR)VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR);
    cvkInitVideoProfileListInfo(&profile_list, NULL, 1, &pEncoder->profile);

    cvkInitVideoEncodeH264Capabilities(&h264_caps, NULL);
    cvkInitVideoEncodeCapabilities(&encode_caps, &h264_caps);
    if (VK_SUCCESS != cvkGetPhysicalDeviceVideoCapabilities(pEncoder->physical, &pEncoder->profile, &encode_caps, &capabilities))
    {
        Report(pReport, reportCapacity, "encode not supported for this profile\n");
        GpuEncode_Destroy(pEncoder);
        return NULL;
    }
    pEncoder->bitstreamAlignment = capabilities.minBitstreamBufferSizeAlignment ? capabilities.minBitstreamBufferSizeAlignment : 1;

    /* The encode input picture and the DPB are separate images with their own advertised formats. */
    cvkInitPhysicalDeviceVideoFormatInfo(&format_info, &profile_list, (VkImageUsageFlags)((VkFlags)VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | (VkFlags)VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    cvkGetPhysicalDeviceVideoFormatProperties(pEncoder->physical, &format_info, &format_count, formats);
    if (0 == format_count)
    {
        Report(pReport, reportCapacity, "no encode input format\n");
        GpuEncode_Destroy(pEncoder);
        return NULL;
    }
    pEncoder->inputFormat = formats[0].format;
    cvkInitPhysicalDeviceVideoFormatInfo(&format_info, &profile_list, (VkImageUsageFlags)VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR);
    cvkGetPhysicalDeviceVideoFormatProperties(pEncoder->physical, &format_info, &format_count, formats);
    if (0 == format_count)
    {
        Report(pReport, reportCapacity, "no encode DPB format\n");
        GpuEncode_Destroy(pEncoder);
        return NULL;
    }
    pEncoder->dpbFormat = formats[0].format;

    memset(&std_header, 0, sizeof(std_header));
    (void)snprintf(std_header.extensionName, sizeof(std_header.extensionName), "%s", VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME);
    std_header.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_SPEC_VERSION;

    cvkInitVideoEncodeH264SessionCreateInfo(&session_create, NULL, VK_TRUE, STD_VIDEO_H264_LEVEL_IDC_4_1);
    pEncoder->sessionMemoryCount = MAX_SESSION_MEMORY;
    cvkSetupVideoSession(pEncoder->physical,
                         pEncoder->device,
                         &session_create,
                         pEncoder->encodeFamily,
                         (VkVideoSessionCreateFlagsKHR)0,
                         &pEncoder->profile,
                         pEncoder->inputFormat,
                         pEncoder->codedExtent,
                         pEncoder->dpbFormat,
                         ENCODE_DPB_SLOTS,
                         1,
                         &std_header,
                         (VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                         NULL,
                         NULL,
                         &pEncoder->session,
                         pEncoder->sessionMemory,
                         &pEncoder->sessionMemoryCount);

    GenerateParameters(pEncoder);
    cvkInitVideoEncodeH264SessionParametersAddInfo(&add_info, NULL, 1, &pEncoder->sps, 1, &pEncoder->pps);
    cvkCreateVideoSessionParametersWithEncodeH264Info(pEncoder->device,
                                                      NULL,
                                                      1,
                                                      1,
                                                      &add_info,
                                                      (VkVideoSessionParametersCreateFlagsKHR)0,
                                                      VK_NULL_HANDLE,
                                                      pEncoder->session,
                                                      NULL,
                                                      &pEncoder->parameters);

    /* Ask the driver for the serialized SPS + PPS: the sequence header the muxer prepends. */
    cvkInitVideoEncodeH264SessionParametersGetInfo(&get_info, NULL, VK_TRUE, VK_TRUE, 0, 0);
    header_size = 0;
    result      = cvkGetEncodedVideoSessionParametersWithH264Info(pEncoder->device, &get_info, pEncoder->parameters, NULL, &header_size, NULL, &h264_feedback);
    if (VK_SUCCESS == result && header_size > 0 && header_size <= MAX_HEADER_BYTES)
    {
        result = cvkGetEncodedVideoSessionParametersWithH264Info(pEncoder->device, &get_info, pEncoder->parameters, NULL, &header_size, pEncoder->header, &h264_feedback);
    }
    if (VK_SUCCESS != result || 0 == header_size)
    {
        Report(pReport, reportCapacity, "could not extract the encoded SPS/PPS (0x%08X)\n", (unsigned)result);
        GpuEncode_Destroy(pEncoder);
        return NULL;
    }
    pEncoder->headerSize = (uint32_t)header_size;

    /* Input picture (upload target), the reconstructed-picture DPB, staging, bitstream, feedback. */
    CreateVideoImage(pEncoder,
                     pEncoder->inputFormat,
                     (VkImageUsageFlags)((VkFlags)VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | (VkFlags)VK_IMAGE_USAGE_TRANSFER_DST_BIT),
                     1,
                     &pEncoder->inputImage,
                     &pEncoder->inputMemory);
    CreateLayerView(pEncoder, pEncoder->inputImage, pEncoder->inputFormat, 0, &pEncoder->inputView);
    CreateVideoImage(pEncoder, pEncoder->dpbFormat, (VkImageUsageFlags)VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR, ENCODE_DPB_SLOTS, &pEncoder->dpbImage, &pEncoder->dpbMemory);
    for (i = 0; i < ENCODE_DPB_SLOTS; ++i)
    {
        CreateLayerView(pEncoder, pEncoder->dpbImage, pEncoder->dpbFormat, i, &pEncoder->dpbView[i]);
    }

    /* Size the upload staging buffer straight from the input pixel format (tightly packed): vku
    ** knows every plane's subsampled extent and per-texel bytes, so no bit depth is assumed. */
    pEncoder->stagingSize = cvkGetFormatSize(pEncoder->inputFormat, pEncoder->codedExtent.width, pEncoder->codedExtent.height);
    cvkCreateBuffer(pEncoder->device, NULL, (VkBufferCreateFlags)0, pEncoder->stagingSize, (VkBufferUsageFlags)VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, NULL, &pEncoder->staging);
    cvkAllocateBufferMemory(pEncoder->physical,
                            pEncoder->device,
                            pEncoder->staging,
                            (VkMemoryPropertyFlags)((VkFlags)VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | (VkFlags)VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                            NULL,
                            NULL,
                            &pEncoder->stagingMemory);
    cvkBindBufferMemory(pEncoder->device, pEncoder->staging, pEncoder->stagingMemory, VK_NO_OFFSET);
    cvkMapMemory(pEncoder->device, pEncoder->stagingMemory, VK_NO_OFFSET, VK_WHOLE_SIZE, (VkMemoryMapFlags)0, &pEncoder->stagingMapped);

    cvkCreateBufferWithVideoProfileListInfo(pEncoder->device,
                                            NULL,
                                            1,
                                            &pEncoder->profile,
                                            (VkBufferCreateFlags)0,
                                            BITSTREAM_CAPACITY,
                                            (VkBufferUsageFlags)VK_BUFFER_USAGE_VIDEO_ENCODE_DST_BIT_KHR,
                                            VK_SHARING_MODE_EXCLUSIVE,
                                            0,
                                            NULL,
                                            NULL,
                                            &pEncoder->bitstream);
    cvkAllocateBufferMemory(pEncoder->physical,
                            pEncoder->device,
                            pEncoder->bitstream,
                            (VkMemoryPropertyFlags)((VkFlags)VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | (VkFlags)VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                            NULL,
                            NULL,
                            &pEncoder->bitstreamMemory);
    cvkBindBufferMemory(pEncoder->device, pEncoder->bitstream, pEncoder->bitstreamMemory, VK_NO_OFFSET);
    cvkMapMemory(pEncoder->device, pEncoder->bitstreamMemory, VK_NO_OFFSET, VK_WHOLE_SIZE, (VkMemoryMapFlags)0, &pEncoder->bitstreamMapped);

    cvkCreateVideoEncodeFeedbackQueryPool(pEncoder->device,
                                          &pEncoder->profile,
                                          (VkVideoEncodeFeedbackFlagsKHR)((VkFlags)VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BUFFER_OFFSET_BIT_KHR | (VkFlags)VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR),
                                          (VkQueryPoolCreateFlags)0,
                                          1,
                                          NULL,
                                          &pEncoder->feedbackPool);

    cvkCreateCommandPool(pEncoder->device, NULL, (VkCommandPoolCreateFlags)VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, pEncoder->encodeFamily, NULL, &pEncoder->encodePool);
    cvkAllocateCommandBuffers(pEncoder->device, NULL, pEncoder->encodePool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &pEncoder->encodeCmd);
    cvkCreateCommandPool(pEncoder->device, NULL, (VkCommandPoolCreateFlags)VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, pEncoder->transferFamily, NULL, &pEncoder->transferPool);
    cvkAllocateCommandBuffers(pEncoder->device, NULL, pEncoder->transferPool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &pEncoder->transferCmd);
    cvkCreateFence(pEncoder->device, NULL, (VkFenceCreateFlags)0, NULL, &pEncoder->encodeFence);
    cvkCreateFence(pEncoder->device, NULL, (VkFenceCreateFlags)0, NULL, &pEncoder->transferFence);
    cvkCreateSemaphore(pEncoder->device, NULL, (VkSemaphoreCreateFlags)0, NULL, &pEncoder->uploadDone);

    Report(pReport,
           reportCapacity,
           "encode session ready: %ux%u (coded %ux%u), input fmt %d, %u planes, %u B SPS/PPS header, %u memory binds\n",
           width,
           height,
           pEncoder->codedExtent.width,
           pEncoder->codedExtent.height,
           (int)pEncoder->inputFormat,
           cvkGetFormatPlaneCount(pEncoder->inputFormat),
           pEncoder->headerSize,
           pEncoder->sessionMemoryCount);
    return pEncoder;
}

/* Fills a picture-resource descriptor for an encode image view at the coded extent. */
static VkVideoPictureResourceInfoKHR PictureResource(const GpuEncoder* pEncoder, VkImageView view)
{
    VkVideoPictureResourceInfoKHR resource;

    memset(&resource, 0, sizeof(resource));
    resource.sType            = VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR;
    resource.codedExtent      = pEncoder->codedExtent;
    resource.baseArrayLayer   = 0;
    resource.imageViewBinding = view;
    return resource;
}

/* Packs one downscaled frame (a luma plane plus interleaved chroma) into the staging buffer using
** the input pixel format's own plane layout, replicating the last row and column into the
** macroblock-aligned border so the coded picture has no undefined samples. Every plane's offset,
** row stride, subsampling divisor and per-texel byte count come from vku, so nothing about the
** bit depth or chroma shape of the encode input format is assumed here. */
static void PackFrame(GpuEncoder* pEncoder, const uint8_t* pLuma, unsigned lumaStride, const uint8_t* pChroma, unsigned chromaStride)
{
    uint8_t*     p_dst;
    VkDeviceSize plane_offsets[MAX_PLANES];
    uint32_t     plane_strides[MAX_PLANES];
    VkExtent3D   plane_extents[MAX_PLANES];
    VkDeviceSize staging_size;
    uint32_t     plane_count;
    uint32_t     plane;

    p_dst       = (uint8_t*)pEncoder->stagingMapped;
    plane_count = cvkCalculateFormatPlaneLayouts(pEncoder->inputFormat, pEncoder->codedExtent.width, pEncoder->codedExtent.height, 1, 1, 1, plane_offsets, plane_strides, plane_extents, &staging_size);
    for (plane = 0; plane < plane_count; ++plane)
    {
        const uint8_t* p_src;
        unsigned       src_stride;
        unsigned       texel;
        unsigned       div_w;
        unsigned       div_h;
        unsigned       src_valid_w;
        unsigned       src_valid_h;
        unsigned       plane_width;
        unsigned       plane_height;
        unsigned       y;

        plane_width  = plane_extents[plane].width;
        plane_height = plane_extents[plane].height;
        texel        = plane_strides[plane] / plane_width; /* bytes per texel, from the stride */
        div_w        = pEncoder->codedExtent.width / plane_width;
        div_h        = pEncoder->codedExtent.height / plane_height;
        src_valid_w  = (pEncoder->width + div_w - 1u) / div_w;
        src_valid_h  = (pEncoder->height + div_h - 1u) / div_h;
        p_src        = (0 == plane) ? pLuma : pChroma;
        src_stride   = (0 == plane) ? lumaStride : chromaStride;
        for (y = 0; y < plane_height; ++y)
        {
            unsigned src_y;
            unsigned x;

            src_y = (y < src_valid_h) ? y : src_valid_h - 1u;
            for (x = 0; x < plane_width; ++x)
            {
                unsigned src_x;
                unsigned b;

                src_x = (x < src_valid_w) ? x : src_valid_w - 1u;
                for (b = 0; b < texel; ++b)
                {
                    p_dst[plane_offsets[plane] + (VkDeviceSize)y * plane_strides[plane] + (VkDeviceSize)x * texel + b] = p_src[(size_t)src_y * src_stride + (size_t)src_x * texel + b];
                }
            }
        }
    }
}

const uint8_t* GpuEncode_Frame(GpuEncoder* pEncoder, const uint8_t* pLuma, unsigned lumaStride, const uint8_t* pChroma, unsigned chromaStride, int forceIdr, int qpOverride, uint32_t* pSize)
{
    int                                  is_idr;
    uint32_t                             current_slot;
    uint32_t                             ref_slot;
    int                                  has_ref;
    int                                  qp;
    VkImageSubresourceRange              color;
    VkBufferImageCopy                    regions[MAX_PLANES];
    VkImageSubresourceLayers             base_layers;
    VkOffset3D                           zero_offset;
    VkExtent3D                           coded_extent;
    VkDeviceSize                         transfer_bytes;
    VkImageAspectFlags                   aspect_mask;
    uint32_t                             region_count;
    uint32_t                             slot;
    VkVideoEncodeRateControlInfoKHR      rate_control;
    VkVideoPictureResourceInfoKHR        input_resource;
    VkVideoPictureResourceInfoKHR        setup_resource;
    VkVideoPictureResourceInfoKHR        ref_resource;
    StdVideoEncodeH264ReferenceInfo      setup_ref_info;
    StdVideoEncodeH264ReferenceInfo      ref_ref_info;
    VkVideoEncodeH264DpbSlotInfoKHR      setup_dpb_info;
    VkVideoEncodeH264DpbSlotInfoKHR      ref_dpb_info;
    VkVideoReferenceSlotInfoKHR          setup_slot_begin;
    VkVideoReferenceSlotInfoKHR          setup_slot_encode;
    VkVideoReferenceSlotInfoKHR          ref_slot_info;
    VkVideoReferenceSlotInfoKHR          begin_slots[ENCODE_DPB_SLOTS + 1];
    uint32_t                             begin_count;
    StdVideoEncodeH264SliceHeader        slice_header;
    StdVideoEncodeH264ReferenceListsInfo ref_lists;
    StdVideoEncodeH264PictureInfo        std_picture;
    VkVideoEncodeH264NaluSliceInfoKHR    nalu_slice;
    VkSubmitInfo                         submit;
    VkPipelineStageFlags                 wait_stage;
    VkDeviceSize                         aligned_range;
    uint32_t                             feedback[3];

    is_idr = (0 == pEncoder->framesSinceIdr) || forceIdr;
    if (is_idr)
    {
        pEncoder->framesSinceIdr = 0;
    }
    current_slot = pEncoder->frameIndex % ENCODE_DPB_SLOTS;
    ref_slot     = (pEncoder->frameIndex + ENCODE_DPB_SLOTS - 1u) % ENCODE_DPB_SLOTS;
    has_ref      = (!is_idr && pEncoder->frameIndex > 0) ? 1 : 0;
    qp           = (qpOverride > 0 && qpOverride < 52) ? qpOverride : (is_idr ? pEncoder->qpIntra : pEncoder->qpInter);

    /* Upload the frame into the input picture on the transfer queue, then hand it to the encoder. */
    cvkInitImageSubresourceRange(&color, (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1);
    PackFrame(pEncoder, pLuma, lumaStride, pChroma, chromaStride);
    /* One buffer->image copy per plane, built from the input pixel format: vku gives each plane's
    ** aspect, subsampled extent and tightly-packed offset matching how PackFrame laid the staging
    ** buffer out, so the upload is correct for any input format without a hand-coded plane table. */
    cvkInitOffset3D(&zero_offset, 0, 0, 0);
    cvkInitExtent3D(&coded_extent, pEncoder->codedExtent.width, pEncoder->codedExtent.height, 1);
    base_layers  = cvkGetFormatBaseSubresourceLayers(pEncoder->inputFormat);
    region_count = cvkGetBufferImageCopyRegions(0, 1, pEncoder->inputFormat, zero_offset, coded_extent, base_layers, regions, &transfer_bytes, &aspect_mask);

    cvkBeginOneTimeCommandBuffer(pEncoder->transferCmd, NULL);
    cvkCmdImageBarrier2(pEncoder->transferCmd,
                        NULL,
                        VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                        (VkAccessFlags2)0,
                        VK_PIPELINE_STAGE_2_COPY_BIT,
                        (VkAccessFlags2)VK_ACCESS_2_TRANSFER_WRITE_BIT,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        pEncoder->inputImage,
                        color,
                        (VkDependencyFlags)0,
                        NULL);
    cvkCmdCopyBufferToImage(pEncoder->transferCmd, pEncoder->staging, pEncoder->inputImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, region_count, regions);
    cvkCmdImageQueueOwnershipBarrier2(pEncoder->transferCmd,
                                      NULL,
                                      VK_PIPELINE_STAGE_2_COPY_BIT,
                                      (VkAccessFlags2)VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                      VK_PIPELINE_STAGE_2_NONE,
                                      (VkAccessFlags2)0,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
                                      pEncoder->transferFamily,
                                      pEncoder->encodeFamily,
                                      pEncoder->inputImage,
                                      color,
                                      (VkDependencyFlags)0,
                                      NULL);
    cvkEndCommandBuffer(pEncoder->transferCmd);
    cvkInitSubmitInfo(&submit, NULL, 0, NULL, NULL, 1, &pEncoder->transferCmd, 1, &pEncoder->uploadDone);
    cvkResetFences(pEncoder->device, 1, &pEncoder->transferFence);
    cvkQueueSubmit(pEncoder->transferQueue, 1, &submit, pEncoder->transferFence);

    /* Rate control: constant QP (rate control disabled), matched on begin and set on the first frame. */
    cvkInitVideoEncodeRateControlInfo(&rate_control, NULL, (VkVideoEncodeRateControlFlagsKHR)0, VK_VIDEO_ENCODE_RATE_CONTROL_MODE_DISABLED_BIT_KHR, 0, NULL, 0, 0);

    input_resource = PictureResource(pEncoder, pEncoder->inputView);
    setup_resource = PictureResource(pEncoder, pEncoder->dpbView[current_slot]);

    memset(&setup_ref_info, 0, sizeof(setup_ref_info));
    setup_ref_info.primary_pic_type = is_idr ? STD_VIDEO_H264_PICTURE_TYPE_IDR : STD_VIDEO_H264_PICTURE_TYPE_P;
    setup_ref_info.FrameNum         = is_idr ? 0u : pEncoder->framesSinceIdr;
    setup_ref_info.PicOrderCnt      = (int32_t)(2u * pEncoder->framesSinceIdr);
    cvkInitVideoEncodeH264DpbSlotInfo(&setup_dpb_info, NULL, &setup_ref_info);
    cvkInitVideoReferenceSlotInfo(&setup_slot_begin, NULL, -1, &setup_resource);
    cvkInitVideoReferenceSlotInfo(&setup_slot_encode, &setup_dpb_info, (int32_t)current_slot, &setup_resource);

    begin_count = 0;
    if (has_ref)
    {
        ref_resource = PictureResource(pEncoder, pEncoder->dpbView[ref_slot]);
        memset(&ref_ref_info, 0, sizeof(ref_ref_info));
        ref_ref_info.primary_pic_type = STD_VIDEO_H264_PICTURE_TYPE_P;
        ref_ref_info.FrameNum         = pEncoder->framesSinceIdr - 1u;
        ref_ref_info.PicOrderCnt      = (int32_t)(2u * (pEncoder->framesSinceIdr - 1u));
        cvkInitVideoEncodeH264DpbSlotInfo(&ref_dpb_info, NULL, &ref_ref_info);
        cvkInitVideoReferenceSlotInfo(&ref_slot_info, &ref_dpb_info, (int32_t)ref_slot, &ref_resource);
        begin_slots[begin_count++] = ref_slot_info;
    }
    begin_slots[begin_count++] = setup_slot_begin;

    /* Record the encode: transition the DPB and input into their encode layouts, encode one slice
    ** with the reconstructed picture as the setup slot, and read back the byte count. */
    cvkBeginOneTimeCommandBuffer(pEncoder->encodeCmd, NULL);
    cvkCmdResetQueryPool(pEncoder->encodeCmd, pEncoder->feedbackPool, 0, 1);
    cvkCmdImageQueueOwnershipBarrier2(pEncoder->encodeCmd,
                                      NULL,
                                      VK_PIPELINE_STAGE_2_NONE,
                                      (VkAccessFlags2)0,
                                      VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR,
                                      (VkAccessFlags2)VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR,
                                      pEncoder->transferFamily,
                                      pEncoder->encodeFamily,
                                      pEncoder->inputImage,
                                      color,
                                      (VkDependencyFlags)0,
                                      NULL);
    for (slot = 0; slot < ENCODE_DPB_SLOTS; ++slot)
    {
        VkImageSubresourceRange layer;

        cvkInitImageSubresourceRange(&layer, (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, slot, 1);
        cvkCmdImageBarrier2(pEncoder->encodeCmd,
                            NULL,
                            VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT,
                            (VkAccessFlags2)0,
                            VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR,
                            (VkAccessFlags2)((VkFlags64)VK_ACCESS_2_VIDEO_ENCODE_WRITE_BIT_KHR | (VkFlags64)VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR),
                            pEncoder->dpbValid[slot] ? VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR : VK_IMAGE_LAYOUT_UNDEFINED,
                            VK_IMAGE_LAYOUT_VIDEO_ENCODE_DPB_KHR,
                            pEncoder->dpbImage,
                            layer,
                            (VkDependencyFlags)0,
                            NULL);
    }

    /* The first begin must match the session's initial (default) rate control state; only after the
    ** control command below has set constant-QP mode does begin carry the disabled rate control. */
    cvkCmdBeginVideoCoding(pEncoder->encodeCmd, pEncoder->rateControlReset ? NULL : (const void*)&rate_control, (VkVideoBeginCodingFlagsKHR)0, pEncoder->session, pEncoder->parameters, begin_count, begin_slots);
    if (pEncoder->rateControlReset)
    {
        cvkCmdControlVideoCoding(pEncoder->encodeCmd, &rate_control, (VkVideoCodingControlFlagsKHR)((VkFlags)VK_VIDEO_CODING_CONTROL_RESET_BIT_KHR | (VkFlags)VK_VIDEO_CODING_CONTROL_ENCODE_RATE_CONTROL_BIT_KHR));
        pEncoder->rateControlReset = 0;
    }

    memset(&slice_header, 0, sizeof(slice_header));
    slice_header.first_mb_in_slice          = 0;
    slice_header.slice_type                 = is_idr ? STD_VIDEO_H264_SLICE_TYPE_I : STD_VIDEO_H264_SLICE_TYPE_P;
    slice_header.slice_qp_delta             = (int8_t)(qp - 26);
    slice_header.cabac_init_idc             = STD_VIDEO_H264_CABAC_INIT_IDC_0;
    slice_header.disable_deblocking_filter_idc = STD_VIDEO_H264_DISABLE_DEBLOCKING_FILTER_IDC_DISABLED;

    memset(&ref_lists, 0, sizeof(ref_lists));
    memset(ref_lists.RefPicList0, STD_VIDEO_H264_NO_REFERENCE_PICTURE, sizeof(ref_lists.RefPicList0));
    memset(ref_lists.RefPicList1, STD_VIDEO_H264_NO_REFERENCE_PICTURE, sizeof(ref_lists.RefPicList1));
    ref_lists.num_ref_idx_l0_active_minus1 = 0;
    ref_lists.num_ref_idx_l1_active_minus1 = 0;
    if (has_ref)
    {
        ref_lists.RefPicList0[0] = (uint8_t)ref_slot;
    }

    memset(&std_picture, 0, sizeof(std_picture));
    std_picture.flags.IdrPicFlag    = is_idr ? 1u : 0u;
    std_picture.flags.is_reference  = 1u;
    std_picture.seq_parameter_set_id = 0;
    std_picture.pic_parameter_set_id = 0;
    std_picture.idr_pic_id          = (uint16_t)pEncoder->idrPicId;
    std_picture.primary_pic_type    = is_idr ? STD_VIDEO_H264_PICTURE_TYPE_IDR : STD_VIDEO_H264_PICTURE_TYPE_P;
    std_picture.frame_num           = is_idr ? 0u : pEncoder->framesSinceIdr;
    std_picture.PicOrderCnt         = (int32_t)(2u * pEncoder->framesSinceIdr);
    std_picture.pRefLists           = &ref_lists;

    cvkInitVideoEncodeH264NaluSliceInfo(&nalu_slice, NULL, qp, &slice_header);

    aligned_range = (BITSTREAM_CAPACITY / pEncoder->bitstreamAlignment) * pEncoder->bitstreamAlignment;

    cvkCmdBeginQuery(pEncoder->encodeCmd, pEncoder->feedbackPool, 0, (VkQueryControlFlags)0);
    cvkCmdEncodeVideoWithH264PictureInfo(pEncoder->encodeCmd,
                                         NULL,
                                         1,
                                         &nalu_slice,
                                         &std_picture,
                                         VK_FALSE,
                                         (VkVideoEncodeFlagsKHR)0,
                                         pEncoder->bitstream,
                                         VK_NO_OFFSET,
                                         aligned_range,
                                         input_resource,
                                         &setup_slot_encode,
                                         has_ref ? 1u : 0u,
                                         has_ref ? &ref_slot_info : NULL,
                                         0);
    cvkCmdEndQuery(pEncoder->encodeCmd, pEncoder->feedbackPool, 0);
    cvkCmdEndVideoCoding(pEncoder->encodeCmd, NULL, (VkVideoEndCodingFlagsKHR)0);
    cvkEndCommandBuffer(pEncoder->encodeCmd);

    wait_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    cvkInitSubmitInfo(&submit, NULL, 1, &pEncoder->uploadDone, &wait_stage, 1, &pEncoder->encodeCmd, 0, NULL);
    cvkResetFences(pEncoder->device, 1, &pEncoder->encodeFence);
    cvkQueueSubmit(pEncoder->encodeQueue, 1, &submit, pEncoder->encodeFence);
    cvkWaitForFence(pEncoder->device, pEncoder->encodeFence, VK_TRUE, UINT64_MAX);

    feedback[0] = 0;
    feedback[1] = 0;
    feedback[2] = 0;
    if (VK_SUCCESS != cvkGetQueryPoolResults(pEncoder->device, pEncoder->feedbackPool, 0, 1, sizeof(feedback), feedback, sizeof(feedback), (VkQueryResultFlags)VK_QUERY_RESULT_WAIT_BIT))
    {
        *pSize = 0;
        return NULL;
    }

    pEncoder->dpbValid[current_slot] = 1;
    pEncoder->lastKeyframe           = is_idr;
    if (is_idr)
    {
        pEncoder->idrPicId = (pEncoder->idrPicId + 1u) & 0xFFFFu;
    }
    ++pEncoder->frameIndex;
    ++pEncoder->framesSinceIdr;
    if (pEncoder->framesSinceIdr >= pEncoder->gopLength)
    {
        pEncoder->framesSinceIdr = 0;
    }

    *pSize = feedback[1];
    return (const uint8_t*)pEncoder->bitstreamMapped + feedback[0];
}

const uint8_t* GpuEncode_Header(GpuEncoder* pEncoder, uint32_t* pSize)
{
    *pSize = pEncoder->headerSize;
    return pEncoder->header;
}

int GpuEncode_LastKeyframe(const GpuEncoder* pEncoder)
{
    return pEncoder->lastKeyframe;
}

void GpuEncode_Destroy(GpuEncoder* pEncoder)
{
    uint32_t i;

    if (NULL == pEncoder)
    {
        return;
    }
    if (VK_NULL_HANDLE != pEncoder->device)
    {
        cvkDeviceWaitIdle(pEncoder->device);
        if (VK_NULL_HANDLE != pEncoder->uploadDone)
        {
            cvkDestroySemaphore(pEncoder->device, pEncoder->uploadDone, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->encodeFence)
        {
            cvkDestroyFence(pEncoder->device, pEncoder->encodeFence, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->transferFence)
        {
            cvkDestroyFence(pEncoder->device, pEncoder->transferFence, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->encodePool)
        {
            cvkDestroyCommandPool(pEncoder->device, pEncoder->encodePool, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->transferPool)
        {
            cvkDestroyCommandPool(pEncoder->device, pEncoder->transferPool, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->feedbackPool)
        {
            cvkDestroyQueryPool(pEncoder->device, pEncoder->feedbackPool, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->bitstream)
        {
            cvkDestroyBuffer(pEncoder->device, pEncoder->bitstream, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->bitstreamMemory)
        {
            cvkFreeMemory(pEncoder->device, pEncoder->bitstreamMemory, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->staging)
        {
            cvkDestroyBuffer(pEncoder->device, pEncoder->staging, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->stagingMemory)
        {
            cvkFreeMemory(pEncoder->device, pEncoder->stagingMemory, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->inputView)
        {
            cvkDestroyImageView(pEncoder->device, pEncoder->inputView, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->inputImage)
        {
            cvkDestroyImage(pEncoder->device, pEncoder->inputImage, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->inputMemory)
        {
            cvkFreeMemory(pEncoder->device, pEncoder->inputMemory, NULL);
        }
        for (i = 0; i < ENCODE_DPB_SLOTS; ++i)
        {
            if (VK_NULL_HANDLE != pEncoder->dpbView[i])
            {
                cvkDestroyImageView(pEncoder->device, pEncoder->dpbView[i], NULL);
            }
        }
        if (VK_NULL_HANDLE != pEncoder->dpbImage)
        {
            cvkDestroyImage(pEncoder->device, pEncoder->dpbImage, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->dpbMemory)
        {
            cvkFreeMemory(pEncoder->device, pEncoder->dpbMemory, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->parameters)
        {
            cvkDestroyVideoSessionParameters(pEncoder->device, pEncoder->parameters, NULL);
        }
        if (VK_NULL_HANDLE != pEncoder->session)
        {
            cvkDestroyVideoSession(pEncoder->device, pEncoder->session, NULL);
        }
        for (i = 0; i < pEncoder->sessionMemoryCount; ++i)
        {
            if (VK_NULL_HANDLE != pEncoder->sessionMemory[i])
            {
                cvkFreeMemory(pEncoder->device, pEncoder->sessionMemory[i], NULL);
            }
        }
        /* The device and instance belong to the shared GpuDevice, not to the encoder. */
    }
    free(pEncoder);
}
