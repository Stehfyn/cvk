/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_image.h"
#include "gfx/gfx_image_platform.h"
#include "gfx/gfx_vaapi.h"
#include "gfx/gfx_vaapi_platform.h"
#include "gfx/gfx_vulkan.h"
#include "gfx/gfx_vulkan_platform.h"
#include "os/os_assert.h"
#include "os/os_malloc.h"

#include <string.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Reads packed pixels from a VA storage image after synchronizing its surface.
 *
 * \par Details:
 *  Reuses the existing derived image and leaves acquired and uploadPending unchanged.
 *
 * \param[in,out] image   Acquired VA STORAGE image with a derived image created during initialization
 * \param[out]    dst     Destination receiving tightly packed image planes without native row padding
 * \param[in]     dstSize Destination capacity in bytes; must fit uint32_t and hold all packed planes
 *
 * \retval None
 */
static void ReadVaapiImageMemory(Gfx_Image_t* image, void* dst, uint64_t dstSize);

/*************************************************************************************************/
/**
 * \brief Reads current Vulkan storage image pixels through coherent staging memory.
 *
 * \par Details:
 *  Waits for the readback fence before copying packed pixels to dst.
 *  Unsubmitted writes share the readback submission; already submitted uploads are waited on.
 *  uploadPending is preserved, and an unsubmitted upload command buffer is reopened for its consumer.
 *
 * \param[in,out] image   Acquired Vulkan STORAGE image with initialized contents or a pending write
 * \param[out]    dst     Destination receiving the current image pixels, including GPU changes
 * \param[in]     dstSize Packed pixel byte count; must equal uploadByteCount
 *
 * \retval None
 */
static void ReadVulkanImageMemory(Gfx_Image_t* image, void* dst, uint64_t dstSize);

/*************************************************************************************************/
/**
 * \brief Enforces the image state required for a new memory write.
 *
 * \param[in] image Image that must be acquired and have no unconsumed write
 * \param[in] src   Non-null source pixel storage
 *
 * \retval    None
 */
static void ValidateWriteMemory(const Gfx_Image_t* image, const void* src);

/*************************************************************************************************/
/**
 * \brief Copies host pixels into a VA surface through its persistent derived image.
 *
 * \param[in,out] image   Acquired VA STORAGE image with no pending write
 * \param[in]     src     Packed source pixels
 * \param[in]     srcSize Source capacity in bytes; must hold all packed planes and fit uint32_t
 *
 * \retval    None
 */
static void WriteVaapiImageMemory(Gfx_Image_t* image, const void* src, uint64_t srcSize);

/*************************************************************************************************/
/**
 * \brief Records a host-buffer-to-Vulkan-image transfer.
 *
 * \param[in,out] image   Acquired Vulkan STORAGE image with an idle command pool and no pending write
 * \param[in]     src     Source pixels copied into coherent staging memory
 * \param[in]     srcSize Packed pixel byte count; must equal uploadByteCount
 *
 * \retval    None
 */
static void WriteVulkanImageMemory(Gfx_Image_t* image, const void* src, uint64_t srcSize);

/*************************************************************************************************/
/**
 * \brief Validates common image creation parameters.
 *
 * \param[in] createInfo Non-NULL creation parameters with nonzero dimensions, a recognized hint and kind, and interop inputs when required
 *
 * \retval None
 */
static void ValidateCreateInfo(const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Initializes VA image storage with no pending work.
 *
 * \param[out] image     Image receiving the VA representation and default state
 * \param[in]  kind      Storage, import, or export role
 * \param[in]  dpy       Initialized VA display owning surfaceId
 * \param[in]  surfaceId Native VA surface containing the pixels
 * \param[in]  imageView Persistent derived view for STORAGE; a zeroed VAImage for IMPORT/EXPORT
 *
 * \retval None
 */
static void InitVaapiImage(Gfx_Image_t* image, Gfx_Image_Kind_t kind, VADisplay dpy, VASurfaceID surfaceId, VAImage imageView);

/*************************************************************************************************/
/**
 * \brief Creates Vulkan storage resources and maps their coherent staging allocation.
 *
 * \param[in]  physicalDevice Physical device supplying compatible memory types
 * \param[in]  device         Logical device owning the resources
 * \param[in]  format         Vulkan image format
 * \param[in]  width          Image width in pixels
 * \param[in]  height         Image height in pixels
 * \param[in]  usage          Requested image usage, including transfer-destination access
 * \param[in]  pAllocator     Optional Vulkan allocation callbacks
 * \param[out] pUploadBuffer  Receives the bidirectional staging buffer
 * \param[out] pUploadMemory  Receives the coherent staging allocation
 * \param[out] ppUploadData   Receives the persistent staging mapping
 * \param[out] image          Receives the storage image
 * \param[out] pMemory        Receives the storage image allocation
 * \param[out] pView          Receives the storage view, or VK_NULL_HANDLE when not requested
 * \param[out] pReadbackFence Receives the unsignaled readback fence
 * \param[out] pStagingSize   Receives the staging buffer size in bytes
 *
 * \retval None
 */
static void CreateVulkanStorageImageResources(VkPhysicalDevice             physicalDevice,
                                              VkDevice                     device,
                                              VkFormat                     format,
                                              uint32_t                     width,
                                              uint32_t                     height,
                                              VkImageUsageFlags            usage,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkBuffer*                    pUploadBuffer,
                                              VkDeviceMemory*              pUploadMemory,
                                              void**                       ppUploadData,
                                              VkImage*                     image,
                                              VkDeviceMemory*              pMemory,
                                              VkImageView*                 pView,
                                              VkFence*                     pReadbackFence,
                                              VkDeviceSize*                pStagingSize);

/*************************************************************************************************/
/**
 * \brief Creates a local VA storage surface and its persistent derived image.
 *
 * \param[out] image      Allocated image receiving its native representation and backend state
 * \param[in]  createInfo STORAGE configuration with an initialized VA display, render-target format, and pixel fourcc
 *
 * \retval None
 */
static void CreateVaapiStorageImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Imports native storage into VA.
 *
 * \param[out] image      Zero-initialized image receiving the imported surface
 * \param[in]  createInfo Validated VA import configuration
 *
 * \retval None
 */
static void CreateVaapiImportImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Creates an exportable VA surface.
 *
 * \param[out] image      Zero-initialized image receiving the exported surface
 * \param[in]  createInfo Validated VA export configuration
 *
 * \retval None
 */
static void CreateVaapiExportImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Initializes Vulkan image storage and synchronization state.
 *
 * \param[out] image                    Image receiving the Vulkan representation and default state
 * \param[in]  kind                     Storage, import, or export role
 * \param[in]  device                   Logical device owning the resources
 * \param[in]  queue                    Borrowed STORAGE queue; VK_NULL_HANDLE for interop
 * \param[in]  commandPool              Borrowed STORAGE command pool; VK_NULL_HANDLE for interop
 * \param[in]  commandBuffer            Borrowed STORAGE command buffer; VK_NULL_HANDLE for interop
 * \param[in]  readbackFence            STORAGE readback fence; VK_NULL_HANDLE for interop
 * \param[in]  vkImage                  Native image; VK_NULL_HANDLE for buffer-backed interop
 * \param[in]  memory                   Primary image memory; VK_NULL_HANDLE for buffer-backed interop
 * \param[in]  externalBuffer           Linear interop storage; VK_NULL_HANDLE when unused
 * \param[in]  externalBufferMemory     Memory bound to externalBuffer; VK_NULL_HANDLE when unused
 * \param[in]  externalBufferSize       External buffer size in bytes
 * \param[in]  externalBufferHostCopy   TRUE for the host-copy interop fallback
 * \param[in]  externalYStride          Luma or packed-pixel row pitch in bytes
 * \param[in]  externalUvOffset         Second plane byte offset; zero for single-plane storage
 * \param[in]  externalUvStride         Second plane row pitch; zero for single-plane storage
 * \param[in]  additionalMemories       Array of VK_MAX_DRM_FORMAT_MODIFIER_PLANES - 1 handles, or NULL for all-null slots
 * \param[in]  memoryCount              Number of image memory allocations, including memory
 * \param[in]  uploadBuffer             STORAGE staging buffer; VK_NULL_HANDLE for interop
 * \param[in]  uploadMemory             Coherent staging memory; VK_NULL_HANDLE for interop
 * \param[in]  pUploadData              Persistent staging mapping; NULL for interop
 * \param[in]  view                     Shader image view; VK_NULL_HANDLE when unused
 * \param[in]  externalSemaphore        Shared timeline semaphore; VK_NULL_HANDLE when unused
 * \param[in]  format                   Pixel format of the image or external buffer
 * \param[in]  width                    Image width in pixels
 * \param[in]  height                   Image height in pixels
 * \param[in]  uploadByteCount          Packed STORAGE staging size in bytes; zero for interop
 *
 * \retval None
 */
static void InitVulkanImage(Gfx_Image_t*          image,
                            Gfx_Image_Kind_t      kind,
                            VkDevice              device,
                            VkQueue               queue,
                            VkCommandPool         commandPool,
                            VkCommandBuffer       commandBuffer,
                            VkFence               readbackFence,
                            VkImage               vkImage,
                            VkDeviceMemory        memory,
                            VkBuffer              externalBuffer,
                            VkDeviceMemory        externalBufferMemory,
                            VkDeviceSize          externalBufferSize,
                            bool_t                externalBufferHostCopy,
                            uint32_t              externalYStride,
                            uint32_t              externalUvOffset,
                            uint32_t              externalUvStride,
                            const VkDeviceMemory* additionalMemories,
                            uint32_t              memoryCount,
                            VkBuffer              uploadBuffer,
                            VkDeviceMemory        uploadMemory,
                            void*                 pUploadData,
                            VkImageView           view,
                            VkSemaphore           externalSemaphore,
                            VkFormat              format,
                            uint32_t              width,
                            uint32_t              height,
                            size_t                uploadByteCount);

/*************************************************************************************************/
/**
 * \brief Creates a local Vulkan image and coherent staging memory for reads and writes.
 *
 * \par Details:
 *  The image also receives transfer-source usage for readback.
 *
 * \param[out] image      Allocated image receiving its native representation and backend state
 * \param[in]  createInfo STORAGE configuration with transfer-destination usage and a queue matching the borrowed command pool and buffer
 *
 * \retval None
 */
static void CreateVulkanStorageImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Imports native memory into Vulkan.
 *
 * \param[out] image      Zero-initialized image receiving the imported resources
 * \param[in]  createInfo Validated Vulkan import configuration
 *
 * \retval None
 */
static void CreateVulkanImportImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Exports Vulkan storage.
 *
 * \param[out] image      Zero-initialized image receiving the exported resources
 * \param[in]  createInfo Validated Vulkan export configuration
 *
 * \retval None
 */
static void CreateVulkanExportImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Creates the native VA representation for the requested image kind.
 *
 * \param[out] image      Allocated image receiving its native representation and backend state
 * \param[in]  createInfo Validated backend, image kind, extent, and native creation inputs
 *
 * \retval None
 */
static void CreateVaapiImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Creates the native Vulkan representation for the requested image kind.
 *
 * \param[out] image      Allocated image receiving its native representation and backend state
 * \param[in]  createInfo Validated backend, image kind, extent, and native creation inputs
 *
 * \retval None
 */
static void CreateVulkanImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Creates an image using the requested backend and sharing role.
 *
 * \param[out] image      Allocated image initialized by the selected concrete creation function
 * \param[in]  createInfo Image backend, sharing kind, dimensions, and native creation parameters
 *
 * \retval None
 */
static void CreateImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Copies packed pixels from an acquired STORAGE image to host memory.
 *
 * \param[in,out] image   Non-NULL acquired STORAGE image whose backend performs the read
 * \param[out]    dst     Non-NULL destination storage filled before the function returns
 * \param[in]     dstSize Destination capacity satisfying the selected backend's size requirements
 *
 * \retval None
 */
static void ReadImageMemory(Gfx_Image_t* image, void* dst, uint64_t dstSize);

/*************************************************************************************************/
/**
 * \brief Copies host pixels to an acquired STORAGE image and marks the write pending.
 *
 * \param[in,out] image   Acquired STORAGE image with no pending write
 * \param[in]     src     Source pixel bytes
 * \param[in]     srcSize Source capacity satisfying the selected backend's size requirements
 *
 * \retval    None
 */
static void WriteImageMemory(Gfx_Image_t* image, const void* src, uint64_t srcSize);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ReadVaapiImageMemory(Gfx_Image_t* image, void* dst, uint64_t dstSize)
{
    LOGASSERT(GFX_IMAGE_KIND_STORAGE == image->kind);
    LOGASSERT(NULL != image->va.dpy);
    LOGASSERT(VA_INVALID_SURFACE != image->va.surfaceId);
    LOGASSERT(dstSize <= (uint64_t)UINT32_MAX);

    Gfx_Vaapi_SyncSurface(image->va.dpy, image->va.surfaceId);
    Gfx_Vaapi_ReadImageMemory(image->va.dpy, &image->va.image, dst, (uint32_t)dstSize);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  An unsubmitted write leaves its command buffer open for the image consumer.
 *  Readback submits that buffer, then reopens it to preserve the consumer's recording contract.
 */
static void ReadVulkanImageMemory(Gfx_Image_t* image, void* dst, uint64_t dstSize)
{
    VkExtent2D               image_extent;
    VkImageSubresourceLayers image_subresource;
    VkOffset2D               image_offset;

    LOGASSERT(GFX_IMAGE_KIND_STORAGE == image->kind);
    LOGASSERT(VK_NULL_HANDLE != image->vk.device);
    LOGASSERT(VK_NULL_HANDLE != image->vk.queue);
    LOGASSERT(VK_NULL_HANDLE != image->vk.commandPool);
    LOGASSERT(VK_NULL_HANDLE != image->vk.commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != image->vk.image);
    LOGASSERT(VK_NULL_HANDLE != image->vk.uploadBuffer);
    LOGASSERT(VK_NULL_HANDLE != image->vk.uploadMemory);
    LOGASSERT(image->vk.layoutInitialized || image->uploadPending);
    LOGASSERT((uint64_t)image->vk.uploadByteCount == dstSize);

    if (image->vk.uploadSubmitted)
    {
        Gfx_Vulkan_WaitForFence(image->vk.device, image->vk.uploadFence, VK_INFINITE);
    }
    if ((FALSE == image->uploadPending) || (image->vk.uploadSubmitted))
    {
        Gfx_Vulkan_ResetCommandPool(image->vk.device, image->vk.commandPool, (VkCommandPoolResetFlags)0);
        Gfx_Vulkan_BeginCommandBuffer(image->vk.commandBuffer, NULL, (VkCommandBufferUsageFlags)VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
    }
    image_offset.x      = 0;
    image_offset.y      = 0;
    image_extent.width  = image->vk.extent.width;
    image_extent.height = image->vk.extent.height;
    image_subresource   = Gfx_Vulkan_GetFormatBaseSubresourceLayers(image->vk.format);
    Gfx_Vulkan_CmdTransferImageToBuffer2D(image->vk.commandBuffer,
                                          (VkDependencyFlags)0,
                                          NULL,
                                          (VkPipelineStageFlags)VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                          (VkAccessFlags)((VkFlags)VK_ACCESS_MEMORY_READ_BIT | (VkFlags)VK_ACCESS_MEMORY_WRITE_BIT),
                                          VK_IMAGE_LAYOUT_GENERAL,
                                          image->vk.image,
                                          image->vk.format,
                                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                          image_offset,
                                          image_extent,
                                          image_subresource,
                                          NULL,
                                          (VkPipelineStageFlags)((VkFlags)VK_PIPELINE_STAGE_ALL_COMMANDS_BIT | (VkFlags)VK_PIPELINE_STAGE_HOST_BIT),
                                          (VkAccessFlags)((VkFlags)VK_ACCESS_MEMORY_READ_BIT | (VkFlags)VK_ACCESS_MEMORY_WRITE_BIT),
                                          image->vk.uploadBuffer,
                                          VK_NO_OFFSET,
                                          1,
                                          NULL,
                                          (VkPipelineStageFlags)VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                                          (VkAccessFlags)((VkFlags)VK_ACCESS_MEMORY_READ_BIT | (VkFlags)VK_ACCESS_MEMORY_WRITE_BIT),
                                          VK_IMAGE_LAYOUT_GENERAL,
                                          NULL,
                                          (VkPipelineStageFlags)VK_PIPELINE_STAGE_HOST_BIT,
                                          (VkAccessFlags)VK_ACCESS_HOST_READ_BIT);
    Gfx_Vulkan_EndCommandBuffer(image->vk.commandBuffer);
    Gfx_Vulkan_ResetFence(image->vk.device, image->vk.readbackFence);
    Gfx_Vulkan_QueueSubmitCommandBuffer(image->vk.queue, NULL, 0, NULL, NULL, image->vk.commandBuffer, 0, NULL, image->vk.readbackFence);
    Gfx_Vulkan_WaitForFence(image->vk.device, image->vk.readbackFence, UINT64_MAX);
    LOGASSERT(NULL != image->vk.pUploadData);
    (void)memcpy(dst, image->vk.pUploadData, (size_t)dstSize);
    image->vk.layoutInitialized = TRUE;
    image->vk.readyStageMask    = (VkPipelineStageFlags)VK_PIPELINE_STAGE_HOST_BIT;
    image->vk.readyAccessMask   = (VkAccessFlags)VK_ACCESS_HOST_READ_BIT;

    if ((image->uploadPending) && (FALSE == image->vk.uploadSubmitted))
    {
        Gfx_Vulkan_ResetCommandPool(image->vk.device, image->vk.commandPool, (VkCommandPoolResetFlags)0);
        Gfx_Vulkan_BeginCommandBuffer(image->vk.commandBuffer, NULL, (VkCommandBufferUsageFlags)VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ValidateWriteMemory(const Gfx_Image_t* image, const void* src)
{
    LOGASSERT(NULL != image);
    LOGASSERT(NULL != src);
    LOGASSERT(FALSE != image->acquired);
    LOGASSERT(FALSE == image->uploadPending);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void WriteVaapiImageMemory(Gfx_Image_t* image, const void* src, uint64_t srcSize)
{
    LOGASSERT(GFX_IMAGE_KIND_STORAGE == image->kind);
    LOGASSERT(NULL != image->va.dpy);
    LOGASSERT(VA_INVALID_SURFACE != image->va.surfaceId);
    LOGASSERT(srcSize <= (uint64_t)UINT32_MAX);

    Gfx_Vaapi_SyncSurface(image->va.dpy, image->va.surfaceId);
    Gfx_Vaapi_WriteImageMemory(image->va.dpy, &image->va.image, src, (uint32_t)srcSize);
    image->uploadPending = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The recorded transfer ends in GENERAL layout with compute-shader read access.
 *  Subsequent writes assume the preceding GPU consumer used compute-shader read access.
 */
static void WriteVulkanImageMemory(Gfx_Image_t* image, const void* src, uint64_t srcSize)
{
    VkAccessFlags            vk_image_src_access_mask;
    VkExtent2D               vk_image_extent;
    VkImageLayout            vk_initial_image_layout;
    VkImageSubresourceLayers vk_image_subresource;
    VkOffset2D               vk_image_offset;
    VkPipelineStageFlags     vk_image_src_stage_mask;

    LOGASSERT(GFX_IMAGE_KIND_STORAGE == image->kind);
    LOGASSERT(VK_NULL_HANDLE != image->vk.device);
    LOGASSERT(VK_NULL_HANDLE != image->vk.commandPool);
    LOGASSERT(VK_NULL_HANDLE != image->vk.commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != image->vk.image);
    LOGASSERT(VK_NULL_HANDLE != image->vk.uploadBuffer);
    LOGASSERT(VK_NULL_HANDLE != image->vk.uploadMemory);
    LOGASSERT(VK_FORMAT_UNDEFINED != image->vk.format);
    LOGASSERT(0 != image->vk.extent.width);
    LOGASSERT(0 != image->vk.extent.height);
    LOGASSERT((uint64_t)image->vk.uploadByteCount == srcSize);

    LOGASSERT(NULL != image->vk.pUploadData);
    (void)memcpy(image->vk.pUploadData, src, (size_t)srcSize);

    Gfx_Vulkan_ResetCommandPool(image->vk.device, image->vk.commandPool, (VkCommandPoolResetFlags)0);
    Gfx_Vulkan_BeginCommandBuffer(image->vk.commandBuffer, NULL, (VkCommandBufferUsageFlags)VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
    vk_image_offset.x      = 0;
    vk_image_offset.y      = 0;
    vk_image_extent.width  = image->vk.extent.width;
    vk_image_extent.height = image->vk.extent.height;
    vk_image_subresource   = Gfx_Vulkan_GetFormatBaseSubresourceLayers(image->vk.format);
    if (image->vk.layoutInitialized)
    {
        vk_image_src_stage_mask  = (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        vk_image_src_access_mask = (VkAccessFlags)VK_ACCESS_SHADER_READ_BIT;
        vk_initial_image_layout  = VK_IMAGE_LAYOUT_GENERAL;
    }
    else
    {
        vk_image_src_stage_mask  = (VkPipelineStageFlags)VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        vk_image_src_access_mask = (VkAccessFlags)0;
        vk_initial_image_layout  = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    Gfx_Vulkan_CmdTransferBufferToImage2D(image->vk.commandBuffer,
                                          (VkDependencyFlags)0,
                                          NULL,
                                          (VkPipelineStageFlags)VK_PIPELINE_STAGE_HOST_BIT,
                                          (VkAccessFlags)VK_ACCESS_HOST_WRITE_BIT,
                                          image->vk.uploadBuffer,
                                          VK_NO_OFFSET,
                                          1,
                                          image->vk.image,
                                          image->vk.format,
                                          VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                          vk_image_offset,
                                          vk_image_extent,
                                          vk_image_subresource,
                                          NULL,
                                          vk_image_src_stage_mask,
                                          vk_image_src_access_mask,
                                          vk_initial_image_layout,
                                          NULL,
                                          (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                          (VkAccessFlags)VK_ACCESS_SHADER_READ_BIT,
                                          VK_IMAGE_LAYOUT_GENERAL);
    image->vk.readyStageMask  = (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    image->vk.readyAccessMask = (VkAccessFlags)VK_ACCESS_SHADER_READ_BIT;
    image->uploadPending      = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Validates common creation inputs before constructing the backend representation.
 */
static void ValidateCreateInfo(const Gfx_Image_CreateInfo_t* createInfo)
{
    LOGASSERT(NULL != createInfo);
    LOGASSERT(0 != createInfo->width && 0 != createInfo->height);
    LOGASSERT(GFX_IMAGE_HINT_VAAPI == createInfo->hint || GFX_IMAGE_HINT_VULKAN == createInfo->hint);
    LOGASSERT(GFX_IMAGE_KIND_IMPORT == createInfo->kind || GFX_IMAGE_KIND_EXPORT == createInfo->kind || GFX_IMAGE_KIND_STORAGE == createInfo->kind);
    if (GFX_IMAGE_KIND_STORAGE != createInfo->kind)
    {
        LOGASSERT(NULL != createInfo->platformCreateInfo);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InitVaapiImage(Gfx_Image_t* image, Gfx_Image_Kind_t kind, VADisplay dpy, VASurfaceID surfaceId, VAImage imageView)
{
    uint32_t memory_index;

    image->hint                      = GFX_IMAGE_HINT_VAAPI;
    image->kind                      = kind;
    image->acquired                  = FALSE;
    image->uploadPending             = FALSE;
    image->va.dpy                    = dpy;
    image->va.surfaceId              = surfaceId;
    image->va.image                  = imageView;
    image->vk.device                 = VK_NULL_HANDLE;
    image->vk.queue                  = VK_NULL_HANDLE;
    image->vk.commandPool            = VK_NULL_HANDLE;
    image->vk.commandBuffer          = VK_NULL_HANDLE;
    image->vk.uploadFence            = VK_NULL_HANDLE;
    image->vk.uploadSemaphore        = VK_NULL_HANDLE;
    image->vk.readbackFence          = VK_NULL_HANDLE;
    image->vk.image                  = VK_NULL_HANDLE;
    image->vk.memory                 = VK_NULL_HANDLE;
    image->vk.externalBuffer         = VK_NULL_HANDLE;
    image->vk.externalBufferMemory   = VK_NULL_HANDLE;
    image->vk.externalBufferSize     = 0;
    image->vk.externalBufferHostCopy = FALSE;
    image->vk.externalYStride        = 0;
    image->vk.externalUvOffset       = 0;
    image->vk.externalUvStride       = 0;
    for (memory_index = 0; memory_index < VK_COUNT_OF(image->vk.additionalMemories); ++memory_index)
    {
        image->vk.additionalMemories[memory_index] = VK_NULL_HANDLE;
    }
    image->vk.memoryCount            = 0;
    image->vk.uploadBuffer           = VK_NULL_HANDLE;
    image->vk.uploadMemory           = VK_NULL_HANDLE;
    image->vk.pUploadData            = NULL;
    image->vk.view                   = VK_NULL_HANDLE;
    image->vk.externalSemaphore      = VK_NULL_HANDLE;
    image->vk.externalSemaphoreValue = 0;
    image->vk.format                 = VK_FORMAT_UNDEFINED;
    image->vk.extent.width           = 0;
    image->vk.extent.height          = 0;
    image->vk.extent.depth           = 0;
    image->vk.uploadByteCount        = 0;
    image->vk.readyStageMask         = (VkPipelineStageFlags)0;
    image->vk.readyAccessMask        = (VkAccessFlags)0;
    image->vk.layoutInitialized      = FALSE;
    image->vk.uploadSubmitted        = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateVaapiStorageImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    VASurfaceID va_surface_id;
    VAImage     va_image_view;

    Gfx_Vaapi_CreateSurface(createInfo->va.dpy, createInfo->va.format, createInfo->va.pixelFormat, createInfo->width, createInfo->height, &va_surface_id);
    Gfx_Vaapi_DeriveImage(createInfo->va.dpy, va_surface_id, &va_image_view);

    InitVaapiImage(image, createInfo->kind, createInfo->va.dpy, va_surface_id, va_image_view);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateVaapiImportImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    VASurfaceID va_surface_id;
    VAImage     va_image_view;

    Gfx_Vaapi_Platform_ImportSurface(createInfo->va.dpy,
                                     createInfo->va.format,
                                     createInfo->va.pixelFormat,
                                     createInfo->width,
                                     createInfo->height,
                                     createInfo->platformCreateInfo->sharedImageResource,
                                     createInfo->platformCreateInfo->sharedHandle,
                                     createInfo->platformCreateInfo->drmPrimeDescriptor,
                                     &va_surface_id);

    (void)memset(&va_image_view, 0, sizeof(va_image_view));

    InitVaapiImage(image, createInfo->kind, createInfo->va.dpy, va_surface_id, va_image_view);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateVaapiExportImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    VASurfaceID va_surface_id;
    VAImage     va_image_view;

    Gfx_Vaapi_Platform_CreateExportedSurface(createInfo->va.dpy,
                                             createInfo->va.format,
                                             createInfo->va.pixelFormat,
                                             createInfo->width,
                                             createInfo->height,
                                             createInfo->platformCreateInfo->sharedHandle,
                                             createInfo->platformCreateInfo->drmPrimeDescriptor,
                                             &va_surface_id);

    (void)memset(&va_image_view, 0, sizeof(va_image_view));

    InitVaapiImage(image, createInfo->kind, createInfo->va.dpy, va_surface_id, va_image_view);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InitVulkanImage(Gfx_Image_t*          image,
                            Gfx_Image_Kind_t      kind,
                            VkDevice              device,
                            VkQueue               queue,
                            VkCommandPool         commandPool,
                            VkCommandBuffer       commandBuffer,
                            VkFence               readbackFence,
                            VkImage               vkImage,
                            VkDeviceMemory        memory,
                            VkBuffer              externalBuffer,
                            VkDeviceMemory        externalBufferMemory,
                            VkDeviceSize          externalBufferSize,
                            bool_t                externalBufferHostCopy,
                            uint32_t              externalYStride,
                            uint32_t              externalUvOffset,
                            uint32_t              externalUvStride,
                            const VkDeviceMemory* additionalMemories,
                            uint32_t              memoryCount,
                            VkBuffer              uploadBuffer,
                            VkDeviceMemory        uploadMemory,
                            void*                 pUploadData,
                            VkImageView           view,
                            VkSemaphore           externalSemaphore,
                            VkFormat              format,
                            uint32_t              width,
                            uint32_t              height,
                            size_t                uploadByteCount)
{
    uint32_t memory_index;

    image->hint                      = GFX_IMAGE_HINT_VULKAN;
    image->kind                      = kind;
    image->acquired                  = FALSE;
    image->uploadPending             = FALSE;
    image->va.dpy                    = NULL;
    image->va.surfaceId              = VA_INVALID_SURFACE;
    image->va.image                  = (VAImage){0};
    image->vk.device                 = device;
    image->vk.queue                  = queue;
    image->vk.commandPool            = commandPool;
    image->vk.commandBuffer          = commandBuffer;
    image->vk.uploadFence            = VK_NULL_HANDLE;
    image->vk.uploadSemaphore        = VK_NULL_HANDLE;
    image->vk.readbackFence          = readbackFence;
    image->vk.image                  = vkImage;
    image->vk.memory                 = memory;
    image->vk.externalBuffer         = externalBuffer;
    image->vk.externalBufferMemory   = externalBufferMemory;
    image->vk.externalBufferSize     = externalBufferSize;
    image->vk.externalBufferHostCopy = externalBufferHostCopy;
    image->vk.externalYStride        = externalYStride;
    image->vk.externalUvOffset       = externalUvOffset;
    image->vk.externalUvStride       = externalUvStride;
    for (memory_index = 0; memory_index < VK_COUNT_OF(image->vk.additionalMemories); ++memory_index)
    {
        if (NULL == additionalMemories)
        {
            image->vk.additionalMemories[memory_index] = VK_NULL_HANDLE;
        }
        else
        {
            image->vk.additionalMemories[memory_index] = additionalMemories[memory_index];
        }
    }
    image->vk.memoryCount            = memoryCount;
    image->vk.uploadBuffer           = uploadBuffer;
    image->vk.uploadMemory           = uploadMemory;
    image->vk.pUploadData            = pUploadData;
    image->vk.view                   = view;
    image->vk.externalSemaphore      = externalSemaphore;
    image->vk.externalSemaphoreValue = 0;
    image->vk.format                 = format;
    image->vk.extent.width           = width;
    image->vk.extent.height          = height;
    image->vk.extent.depth           = 1;
    image->vk.uploadByteCount        = uploadByteCount;
    image->vk.readyStageMask         = (VkPipelineStageFlags)0;
    image->vk.readyAccessMask        = (VkAccessFlags)0;
    image->vk.layoutInitialized      = FALSE;
    image->vk.uploadSubmitted        = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateVulkanStorageImageResources(VkPhysicalDevice             physicalDevice,
                                              VkDevice                     device,
                                              VkFormat                     format,
                                              uint32_t                     width,
                                              uint32_t                     height,
                                              VkImageUsageFlags            usage,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkBuffer*                    pUploadBuffer,
                                              VkDeviceMemory*              pUploadMemory,
                                              void**                       ppUploadData,
                                              VkImage*                     image,
                                              VkDeviceMemory*              pMemory,
                                              VkImageView*                 pView,
                                              VkFence*                     pReadbackFence,
                                              VkDeviceSize*                pStagingSize)
{
    VkExtent3D              image_extent;
    VkComponentMapping      components;
    VkImageSubresourceRange view_range;
    VkDeviceSize            staging_size;
    VkImageUsageFlags       image_usage;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT((VkImageUsageFlags)0 != (usage & (VkImageUsageFlags)VK_IMAGE_USAGE_TRANSFER_DST_BIT));
    LOGASSERT(NULL != pView);
    LOGASSERT(NULL != pStagingSize);

    image_extent.width  = width;
    image_extent.height = height;
    image_extent.depth  = 1;

    staging_size = Gfx_Vulkan_GetFormatSize(image_extent.width, image_extent.height, format);
    image_usage  = usage | (VkImageUsageFlags)VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    components   = c_vkIdentityComponentMapping;
    view_range   = Gfx_Vulkan_GetFormatBaseSubresourceRange(format);
    LOGASSERT((VkDeviceSize)(size_t)staging_size == staging_size);

    Gfx_Vulkan_SetupBuffer(physicalDevice,
                           device,
                           NULL,
                           (VkBufferCreateFlags)0,
                           staging_size,
                           (VkBufferUsageFlags)((VkFlags)VK_BUFFER_USAGE_TRANSFER_SRC_BIT | (VkFlags)VK_BUFFER_USAGE_TRANSFER_DST_BIT),
                           VK_SHARING_MODE_EXCLUSIVE,
                           0,
                           NULL,
                           NULL,
                           (VkMemoryPropertyFlags)((VkFlags)VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | (VkFlags)VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                           pAllocator,
                           pUploadBuffer,
                           pUploadMemory);
    Gfx_Vulkan_MapMemory(device, (*pUploadMemory), VK_NO_OFFSET, staging_size, (VkMemoryMapFlags)0, ppUploadData);
    Gfx_Vulkan_SetupImage(physicalDevice,
                          device,
                          NULL,
                          (VkImageCreateFlags)0,
                          VK_IMAGE_TYPE_2D,
                          format,
                          image_extent,
                          1,
                          1,
                          VK_SAMPLE_COUNT_1_BIT,
                          VK_IMAGE_TILING_OPTIMAL,
                          image_usage,
                          VK_SHARING_MODE_EXCLUSIVE,
                          0,
                          NULL,
                          VK_IMAGE_LAYOUT_UNDEFINED,
                          NULL,
                          (VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                          pAllocator,
                          image,
                          pMemory);
    (*pView) = VK_NULL_HANDLE;
    if (usage & (VkImageUsageFlags)((VkFlags)VK_IMAGE_USAGE_STORAGE_BIT | (VkFlags)VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        Gfx_Vulkan_CreateImageView(device, NULL, (VkImageViewCreateFlags)0, (*image), VK_IMAGE_VIEW_TYPE_2D, format, components, view_range, pAllocator, pView);
    }
    Gfx_Vulkan_CreateFence(device, NULL, (VkFenceCreateFlags)0, pAllocator, pReadbackFence);
    (*pStagingSize) = staging_size;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateVulkanStorageImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    VkBuffer       vk_upload_buffer;
    VkDeviceMemory vk_upload_memory;
    void*          upload_data;
    VkImage        vk_image;
    VkDeviceMemory vk_memory;
    VkImageView    vk_view;
    VkFence        vk_readback_fence;
    VkDeviceSize   vk_staging_size;

    LOGASSERT(VK_NULL_HANDLE != createInfo->vk.queue);
    LOGASSERT(VK_NULL_HANDLE != createInfo->vk.commandPool);
    LOGASSERT(VK_NULL_HANDLE != createInfo->vk.commandBuffer);

    CreateVulkanStorageImageResources(createInfo->vk.physicalDevice,
                                      createInfo->vk.device,
                                      createInfo->vk.format,
                                      createInfo->width,
                                      createInfo->height,
                                      createInfo->vk.usage,
                                      createInfo->vk.pAllocator,
                                      &vk_upload_buffer,
                                      &vk_upload_memory,
                                      &upload_data,
                                      &vk_image,
                                      &vk_memory,
                                      &vk_view,
                                      &vk_readback_fence,
                                      &vk_staging_size);

    InitVulkanImage(image,
                    createInfo->kind,
                    createInfo->vk.device,
                    createInfo->vk.queue,
                    createInfo->vk.commandPool,
                    createInfo->vk.commandBuffer,
                    vk_readback_fence,
                    vk_image,
                    vk_memory,
                    VK_NULL_HANDLE,
                    VK_NULL_HANDLE,
                    0,
                    FALSE,
                    0,
                    0,
                    0,
                    NULL,
                    1,
                    vk_upload_buffer,
                    vk_upload_memory,
                    upload_data,
                    vk_view,
                    VK_NULL_HANDLE,
                    createInfo->vk.format,
                    createInfo->width,
                    createInfo->height,
                    (size_t)vk_staging_size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateVulkanImportImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    VkExtent3D     vk_extent;
    VkImage        vk_image;
    VkDeviceMemory vk_memories[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t       vk_memory_count;
    VkBuffer       vk_external_buffer;
    VkDeviceMemory vk_external_buffer_memory;
    VkDeviceSize   vk_external_buffer_size;
    bool_t         external_buffer_host_copy;
    uint32_t       external_y_stride;
    uint32_t       external_uv_offset;
    uint32_t       external_uv_stride;
    VkSemaphore    vk_external_semaphore;

    vk_extent.width  = createInfo->width;
    vk_extent.height = createInfo->height;
    vk_extent.depth  = 1;
    Gfx_Vulkan_Platform_ImportImage(createInfo->vk.physicalDevice,
                                    createInfo->vk.device,
                                    createInfo->vk.format,
                                    vk_extent,
                                    createInfo->vk.usage,
                                    createInfo->platformCreateInfo->drmPrimeDescriptor,
                                    createInfo->platformCreateInfo->sharedImageHandle,
                                    createInfo->platformCreateInfo->sharedImageHandleType,
                                    createInfo->platformCreateInfo->sharedImageAllocationSize,
                                    createInfo->platformCreateInfo->transferBufferHostCopy,
                                    createInfo->platformCreateInfo->transferBufferSize,
                                    createInfo->platformCreateInfo->sharedTransferBufferHandle,
                                    createInfo->platformCreateInfo->sharedFenceHandle,
                                    createInfo->platformCreateInfo->yStride,
                                    createInfo->platformCreateInfo->uvOffset,
                                    createInfo->platformCreateInfo->uvStride,
                                    createInfo->platformCreateInfo->ppVulkanTransferBytes,
                                    createInfo->vk.pAllocator,
                                    &vk_image,
                                    vk_memories,
                                    &vk_memory_count,
                                    &vk_external_buffer,
                                    &vk_external_buffer_memory,
                                    &vk_external_buffer_size,
                                    &external_buffer_host_copy,
                                    &external_y_stride,
                                    &external_uv_offset,
                                    &external_uv_stride,
                                    &vk_external_semaphore);

    InitVulkanImage(image,
                    createInfo->kind,
                    createInfo->vk.device,
                    VK_NULL_HANDLE,
                    VK_NULL_HANDLE,
                    VK_NULL_HANDLE,
                    VK_NULL_HANDLE,
                    vk_image,
                    vk_memories[0],
                    vk_external_buffer,
                    vk_external_buffer_memory,
                    vk_external_buffer_size,
                    external_buffer_host_copy,
                    external_y_stride,
                    external_uv_offset,
                    external_uv_stride,
                    &vk_memories[1],
                    vk_memory_count,
                    VK_NULL_HANDLE,
                    VK_NULL_HANDLE,
                    NULL,
                    VK_NULL_HANDLE,
                    vk_external_semaphore,
                    createInfo->vk.format,
                    createInfo->width,
                    createInfo->height,
                    0);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateVulkanExportImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    VkExtent3D     vk_extent;
    VkImage        vk_image;
    VkDeviceMemory vk_memories[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t       vk_memory_count;
    VkBuffer       vk_external_buffer;
    VkDeviceMemory vk_external_buffer_memory;
    VkDeviceSize   vk_external_buffer_size;
    bool_t         external_buffer_host_copy;
    uint32_t       external_y_stride;
    uint32_t       external_uv_offset;
    uint32_t       external_uv_stride;
    VkSemaphore    vk_external_semaphore;

    vk_extent.width  = createInfo->width;
    vk_extent.height = createInfo->height;
    vk_extent.depth  = 1;
    Gfx_Vulkan_Platform_ExportImage(createInfo->vk.physicalDevice,
                                    createInfo->vk.device,
                                    createInfo->vk.format,
                                    vk_extent,
                                    createInfo->vk.usage,
                                    createInfo->platformCreateInfo->drmPrimeDescriptor,
                                    createInfo->vk.pAllocator,
                                    &vk_image,
                                    vk_memories,
                                    &vk_memory_count,
                                    &vk_external_buffer,
                                    &vk_external_buffer_memory,
                                    &vk_external_buffer_size,
                                    &external_buffer_host_copy,
                                    &external_y_stride,
                                    &external_uv_offset,
                                    &external_uv_stride,
                                    &vk_external_semaphore);

    InitVulkanImage(image,
                    createInfo->kind,
                    createInfo->vk.device,
                    VK_NULL_HANDLE,
                    VK_NULL_HANDLE,
                    VK_NULL_HANDLE,
                    VK_NULL_HANDLE,
                    vk_image,
                    vk_memories[0],
                    vk_external_buffer,
                    vk_external_buffer_memory,
                    vk_external_buffer_size,
                    external_buffer_host_copy,
                    external_y_stride,
                    external_uv_offset,
                    external_uv_stride,
                    &vk_memories[1],
                    vk_memory_count,
                    VK_NULL_HANDLE,
                    VK_NULL_HANDLE,
                    NULL,
                    VK_NULL_HANDLE,
                    vk_external_semaphore,
                    createInfo->vk.format,
                    createInfo->width,
                    createInfo->height,
                    0);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateVaapiImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    switch (createInfo->kind)
    {
    case GFX_IMAGE_KIND_STORAGE:
        CreateVaapiStorageImage(image, createInfo);
        break;

    case GFX_IMAGE_KIND_IMPORT:
        CreateVaapiImportImage(image, createInfo);
        break;

    case GFX_IMAGE_KIND_EXPORT:
        CreateVaapiExportImage(image, createInfo);
        break;

    default:
        LOGFATAL("Unsupported VA vk_image_view kind");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateVulkanImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    switch (createInfo->kind)
    {
    case GFX_IMAGE_KIND_STORAGE:
        CreateVulkanStorageImage(image, createInfo);
        break;

    case GFX_IMAGE_KIND_IMPORT:
        CreateVulkanImportImage(image, createInfo);
        break;

    case GFX_IMAGE_KIND_EXPORT:
        CreateVulkanExportImage(image, createInfo);
        break;

    default:
        LOGFATAL("Unsupported Vulkan vk_image_view kind");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    ValidateCreateInfo(createInfo);

    switch (createInfo->hint)
    {
    case GFX_IMAGE_HINT_VAAPI:
        CreateVaapiImage(image, createInfo);
        break;

    case GFX_IMAGE_HINT_VULKAN:
        CreateVulkanImage(image, createInfo);
        break;

    default:
        LOGFATAL("Unsupported vk_image_view hint");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ReadImageMemory(Gfx_Image_t* image, void* dst, uint64_t dstSize)
{
    LOGASSERT(NULL != image);
    LOGASSERT(NULL != dst);
    LOGASSERT(FALSE != image->acquired);

    switch (image->hint)
    {
    case GFX_IMAGE_HINT_VULKAN:
        ReadVulkanImageMemory(image, dst, dstSize);
        break;

    case GFX_IMAGE_HINT_VAAPI:
        ReadVaapiImageMemory(image, dst, dstSize);
        break;

    default:
        LOGFATAL("Unsupported vk_image_view vk_memory read hint");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void WriteImageMemory(Gfx_Image_t* image, const void* src, uint64_t srcSize)
{
    ValidateWriteMemory(image, src);

    switch (image->hint)
    {
    case GFX_IMAGE_HINT_VULKAN:
        WriteVulkanImageMemory(image, src, srcSize);
        break;

    case GFX_IMAGE_HINT_VAAPI:
        WriteVaapiImageMemory(image, src, srcSize);
        break;

    default:
        LOGFATAL("Unsupported vk_image_view vk_memory write hint");
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Image_Create(Gfx_Image_t** image, const Gfx_Image_CreateInfo_t* createInfo)
{
    LOGASSERT(NULL != image);
    LOGASSERT(NULL != createInfo);

    (*image) = (Gfx_Image_t*)OS_CALLOC(sizeof(Gfx_Image_t));
    CreateImage((*image), createInfo);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Image_ReadMemory(Gfx_Image_t* image, void* dst, uint64_t dstSize)
{
    LOGASSERT(NULL != image);
    LOGASSERT(NULL != dst);

    ReadImageMemory(image, dst, dstSize);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Image_WriteMemory(Gfx_Image_t* image, const void* src, uint64_t srcSize)
{
    LOGASSERT(NULL != image);
    LOGASSERT(NULL != src);

    WriteImageMemory(image, src, srcSize);
}
