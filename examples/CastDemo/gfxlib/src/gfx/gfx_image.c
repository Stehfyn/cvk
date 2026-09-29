/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cvk/cvk.h"
#include "cva.h"
#include "gfx/gfx_image.h"
#include "gfx/gfx_image_platform.h"
#include "gfx/gfx_image_internal.h"
#include "gfx/gfx_image_layout.h"
#include "os/os_assert.h"
#include "os/os_malloc.h"

#include <stdio.h>
#include <string.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define DIAGNOSTIC_RECORD_LIMIT   (256) ///< Maximum native diagnostic records examined per synchronization.
#define DIAGNOSTIC_RECORD_END     (-1)  ///< libva va.h: terminating VASurfaceDecodeMBErrors status.
#define DIAGNOSTIC_RECORD_PRESENT (1)   ///< libva va.h: hardware supplied detailed record fields.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Startup image owner retaining validated VA transfer geometry alongside its public state.
 *
 * \par Details:
 *  image is the first member so the public pointer identifies its owning allocation.
 *  Only Gfx_Image_Create constructs storage images; callers shall not copy or fabricate owners.
 */
typedef struct Gfx_Image_Storage_s
{
    Gfx_Image_t        image;  ///< Public image state at the start of its owning allocation.
    Gfx_Image_Layout_t layout; ///< Immutable geometry for VA STORAGE images; unused by other kinds.
} Gfx_Image_Storage_t;

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
 * \brief Reports a bounded sequence of optional VA surface diagnostics for an image.
 *
 * \par Requirements:
 *  - 001 Image shall retain a valid display and surface during the native query and reporting.
 *  - 002 The configured cva query shall return every specified diagnostic failure to this caller.
 *  - 003 Failed queries shall not expose or inspect native output pointers.
 *  - 004 At most DIAGNOSTIC_RECORD_LIMIT records shall be examined; reaching the limit shall be reported.
 *  - 005 Optional diagnostics shall not replace the original synchronization result.
 *
 * \par Details:
 *  The driver owns the returned sentinel-terminated records. Its allocation behavior and the
 *  imported logging backend still require adaptation to the complete initialization-only goal.
 *
 * \param[in] image   VA image whose pending work was synchronized
 * \param[in] status  Original success, decode-error or hardware-busy result
 *
 * \retval None
 */
static void ReportVaImageDiagnostics(const Gfx_Image_t* image, VAStatus status);

/*************************************************************************************************/
/**
 * \brief Synchronizes a VA image while preserving its primary completion result.
 *
 * \par Requirements:
 *  - 001 Image shall contain a valid display and surface; invalid construction shall assert.
 *  - 002 Native operations shall use cva; optional diagnostics shall use its configured result policy.
 *  - 003 Final status checking shall use the synchronization result, not the diagnostic query result.
 *
 * \par Details:
 *  This retains the existing synchronous image-transfer contract. Converting the owning
 *  image/surface workflow to bounded completion remains required by the complete goal.
 *
 * \param[in] image   VA image exclusively owned by its caller
 *
 * \retval None
 */
static void SynchronizeVaImage(const Gfx_Image_t* image);

/*************************************************************************************************/
/**
 * \brief Reads packed pixels from a VA storage image after synchronizing its surface.
 *
 * \par Requirements:
 *  - 001 Image shall be an acquired STORAGE owner returned by Gfx_Image_Create.
 *  - 002 Destination shall be disjoint from native storage and cover the cached packed extent.
 *  - 003 Capacity shall be checked before native synchronization or mapping.
 *  - 004 Each successful mapping shall be followed by exactly one unmap after copying active rows.
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
static void InitVaapiImage(Gfx_Image_t*     image,
                           Gfx_Image_Kind_t kind,
                           VADisplay        dpy,
                           VASurfaceID      surfaceId,
                           VAImage          imageView);

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
 * \param[in]  additionalMemories       Array of CVK_MAX_DRM_FORMAT_MODIFIER_PLANES - 1 handles, or NULL for all-null slots
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
 *  cva's configured optional-query policy returns native failure statuses. Failed output is
 *  discarded before inspection. A native sentinel ends traversal before any further record load.
 */
static void ReportVaImageDiagnostics(const Gfx_Image_t* image, VAStatus status)
{
    void*                          error_info;
    VAStatus                       query_status;
    bool_t                         query_succeeded;
    bool_t                         has_records;
    bool_t                         can_report;
    const VASurfaceDecodeMBErrors* surface_error;
    uint32_t                       record_index;
    bool_t                         is_terminal;
    bool_t                         has_details;
    bool_t                         reached_limit;
    bool_t                         synchronization_failed;

    error_info      = NULL;
    query_status    = cvaQuerySurfaceError(image->va.dpy, image->va.surfaceId, status, &error_info);
    query_succeeded = (VA_STATUS_SUCCESS == query_status);
    if (FALSE == query_succeeded)
    {
        error_info = NULL;
    }
    has_records = (NULL != error_info);
    can_report  = query_succeeded && has_records;
    if (can_report)
    {
        surface_error = (const VASurfaceDecodeMBErrors*)error_info;
        for (record_index = 0; record_index < DIAGNOSTIC_RECORD_LIMIT; ++record_index)
        {
            is_terminal = (DIAGNOSTIC_RECORD_END == surface_error->status);
            has_details = (DIAGNOSTIC_RECORD_PRESENT == surface_error->status);
            if (is_terminal)
            {
                break;
            }
            if (has_details)
            {
                LOGSTART(stderr);
                LOGPRINTS("VA-API surface ");
                LOGPRINTU((uint64_t)image->va.surfaceId);
                LOGPRINTS(" diagnostic: type=");
                LOGPRINTI((int64_t)surface_error->decode_error_type);
                LOGPRINTS(" start_mb=");
                LOGPRINTU((uint64_t)surface_error->start_mb);
                LOGPRINTS(" end_mb=");
                LOGPRINTU((uint64_t)surface_error->end_mb);
                LOGPRINTS(" num_mb=");
                LOGPRINTU((uint64_t)surface_error->num_mb);
                LOGPRINTS("\n");
                LOGEND();
            }
            ++surface_error;
        }
        reached_limit = (DIAGNOSTIC_RECORD_LIMIT == record_index);
        if (reached_limit)
        {
            LOGSTART(stderr);
            LOGPRINTS("VA-API surface ");
            LOGPRINTU((uint64_t)image->va.surfaceId);
            LOGPRINTS(" diagnostic record limit reached\n");
            LOGEND();
        }
    }
    else if (FALSE == query_succeeded)
    {
        synchronization_failed = (VA_STATUS_SUCCESS != status);
        if (synchronization_failed)
        {
            LOGSTART(stderr);
            LOGPRINTS("VA-API surface ");
            LOGPRINTU((uint64_t)image->va.surfaceId);
            LOGPRINTS(" diagnostics unavailable: ");
            LOGPRINTS(cvaErrorStr(query_status));
            LOGPRINTS("\n");
            LOGEND();
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only the three libva-defined synchronization outcomes permit the diagnostic query.
 *  The primary result remains live until the final cva status check.
 */
static void SynchronizeVaImage(const Gfx_Image_t* image)
{
    VAStatus status;
    bool_t   is_complete;
    bool_t   decoding_failed;
    bool_t   hardware_busy;
    bool_t   can_query;

    LOGASSERT(NULL != image);
    LOGASSERT(NULL != image->va.dpy);
    LOGASSERT(VA_INVALID_SURFACE != image->va.surfaceId);

    status          = cvaSyncSurface(image->va.dpy, image->va.surfaceId);
    is_complete     = (VA_STATUS_SUCCESS == status);
    decoding_failed = (VA_STATUS_ERROR_DECODING_ERROR == status);
    hardware_busy   = (VA_STATUS_ERROR_HW_BUSY == status);
    can_query       = is_complete || decoding_failed || hardware_busy;
    if (can_query)
    {
        ReportVaImageDiagnostics(image, status);
    }
    cvaCheckStatus(status, "cvaSyncSurface(image->va.dpy, image->va.surfaceId)", __FILE__, __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ReadVaapiImageMemory(Gfx_Image_t* image, void* dst, uint64_t dstSize)
{
    const Gfx_Image_Storage_t* storage;
    void*                      mapped;

    LOGASSERT(GFX_IMAGE_KIND_STORAGE == image->kind);
    LOGASSERT(NULL != image->va.dpy);
    LOGASSERT(VA_INVALID_SURFACE != image->va.surfaceId);
    LOGASSERT(dstSize <= (uint64_t)UINT32_MAX);

    storage = (const Gfx_Image_Storage_t*)image;
    LOGASSERT(storage->layout.packedByteCount <= dstSize);

    SynchronizeVaImage(image);
    mapped = NULL;
    cvaMapBuffer2(image->va.dpy, image->va.image.buf, &mapped, VA_MAPBUFFER_FLAG_READ);
    LOGASSERT(NULL != mapped);
    Gfx_Image_ReadLayout(&storage->layout, dst, dstSize, mapped);
    cvaUnmapBuffer(image->va.dpy, image->va.image.buf);
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
        CVK_CHECK(cvkWaitForFence(image->vk.device, image->vk.uploadFence, VK_TRUE, UINT64_MAX));
    }
    if ((FALSE == image->uploadPending) || (image->vk.uploadSubmitted))
    {
        cvkResetCommandPool(image->vk.device, image->vk.commandPool, (VkCommandPoolResetFlags)0);
        cvkBeginCommandBuffer(image->vk.commandBuffer,
                              NULL,
                              (VkCommandBufferUsageFlags)VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                              NULL);
    }
    image_offset.x      = 0;
    image_offset.y      = 0;
    image_extent.width  = image->vk.extent.width;
    image_extent.height = image->vk.extent.height;
    image_subresource   = cvkGetFormatBaseSubresourceLayers(image->vk.format);
    cvkCmdTransferImageToBuffer2D(
        image->vk.commandBuffer,
        (VkDependencyFlags)0,
        NULL,
        (VkPipelineStageFlags)VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        (VkAccessFlags)((VkFlags)VK_ACCESS_MEMORY_READ_BIT | (VkFlags)VK_ACCESS_MEMORY_WRITE_BIT),
        VK_IMAGE_LAYOUT_GENERAL,
        image->vk.image,
        image->vk.format,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
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
        (VkAccessFlags)VK_ACCESS_HOST_READ_BIT,
        image_offset,
        image_extent);
    cvkEndCommandBuffer(image->vk.commandBuffer);
    cvkResetFence(image->vk.device, image->vk.readbackFence);
    cvkQueueSubmitCommandBuffer(image->vk.queue,
                                NULL,
                                0,
                                NULL,
                                NULL,
                                0,
                                NULL,
                                image->vk.readbackFence,
                                image->vk.commandBuffer);
    CVK_CHECK(cvkWaitForFence(image->vk.device, image->vk.readbackFence, VK_TRUE, UINT64_MAX));
    LOGASSERT(NULL != image->vk.pUploadData);
    (void)memcpy(dst, image->vk.pUploadData, (size_t)dstSize);
    image->vk.layoutInitialized = TRUE;
    image->vk.readyStageMask    = (VkPipelineStageFlags)VK_PIPELINE_STAGE_HOST_BIT;
    image->vk.readyAccessMask   = (VkAccessFlags)VK_ACCESS_HOST_READ_BIT;

    if ((image->uploadPending) && (FALSE == image->vk.uploadSubmitted))
    {
        cvkResetCommandPool(image->vk.device, image->vk.commandPool, (VkCommandPoolResetFlags)0);
        cvkBeginCommandBuffer(image->vk.commandBuffer,
                              NULL,
                              (VkCommandBufferUsageFlags)VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                              NULL);
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
    const Gfx_Image_Storage_t* storage;
    void*                      mapped;

    LOGASSERT(GFX_IMAGE_KIND_STORAGE == image->kind);
    LOGASSERT(NULL != image->va.dpy);
    LOGASSERT(VA_INVALID_SURFACE != image->va.surfaceId);
    LOGASSERT(srcSize <= (uint64_t)UINT32_MAX);

    storage = (const Gfx_Image_Storage_t*)image;
    LOGASSERT(storage->layout.packedByteCount <= srcSize);

    SynchronizeVaImage(image);
    mapped = NULL;
    cvaMapBuffer2(image->va.dpy, image->va.image.buf, &mapped, VA_MAPBUFFER_FLAG_WRITE);
    LOGASSERT(NULL != mapped);
    Gfx_Image_WriteLayout(&storage->layout, src, srcSize, mapped);
    cvaUnmapBuffer(image->va.dpy, image->va.image.buf);
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

    cvkResetCommandPool(image->vk.device, image->vk.commandPool, (VkCommandPoolResetFlags)0);
    cvkBeginCommandBuffer(image->vk.commandBuffer,
                          NULL,
                          (VkCommandBufferUsageFlags)VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                          NULL);
    vk_image_offset.x      = 0;
    vk_image_offset.y      = 0;
    vk_image_extent.width  = image->vk.extent.width;
    vk_image_extent.height = image->vk.extent.height;
    vk_image_subresource   = cvkGetFormatBaseSubresourceLayers(image->vk.format);
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
    cvkCmdTransferBufferToImage2D(image->vk.commandBuffer,
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
                                  vk_image_subresource,
                                  NULL,
                                  vk_image_src_stage_mask,
                                  vk_image_src_access_mask,
                                  vk_initial_image_layout,
                                  NULL,
                                  (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                  (VkAccessFlags)VK_ACCESS_SHADER_READ_BIT,
                                  VK_IMAGE_LAYOUT_GENERAL,
                                  vk_image_offset,
                                  vk_image_extent);
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
    LOGASSERT(GFX_IMAGE_KIND_IMPORT == createInfo->kind || GFX_IMAGE_KIND_EXPORT == createInfo->kind ||
              GFX_IMAGE_KIND_STORAGE == createInfo->kind);
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
static void InitVaapiImage(Gfx_Image_t*     image,
                           Gfx_Image_Kind_t kind,
                           VADisplay        dpy,
                           VASurfaceID      surfaceId,
                           VAImage          imageView)
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
    VASurfaceID          va_surface_id;
    VAImage              va_image_view;
    Gfx_Image_Storage_t* storage;

    cvaCreateSurfacesWithPixelFormat(createInfo->va.dpy,
                                     createInfo->va.format,
                                     createInfo->width,
                                     createInfo->height,
                                     &va_surface_id,
                                     1,
                                     createInfo->va.pixelFormat);
    cvaDeriveImage(createInfo->va.dpy, va_surface_id, &va_image_view);

    storage = (Gfx_Image_Storage_t*)image;
    Gfx_Image_InitializeLayout(&storage->layout, &va_image_view);
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

    staging_size = cvkGetFormatSize(format, image_extent.width, image_extent.height);
    image_usage  = usage | (VkImageUsageFlags)VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    cvkInitComponentMapping(&components,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY,
                            VK_COMPONENT_SWIZZLE_IDENTITY);
    view_range = cvkGetFormatBaseSubresourceRange(format);
    LOGASSERT((VkDeviceSize)(size_t)staging_size == staging_size);

    cvkSetupBuffer(
        physicalDevice,
        device,
        NULL,
        (VkBufferCreateFlags)0,
        staging_size,
        (VkBufferUsageFlags)((VkFlags)VK_BUFFER_USAGE_TRANSFER_SRC_BIT | (VkFlags)VK_BUFFER_USAGE_TRANSFER_DST_BIT),
        VK_SHARING_MODE_EXCLUSIVE,
        0,
        NULL,
        (VkMemoryPropertyFlags)((VkFlags)VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                (VkFlags)VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
        NULL,
        pAllocator,
        pUploadBuffer,
        pUploadMemory);
    cvkMapMemory(device, (*pUploadMemory), VK_NO_OFFSET, staging_size, (VkMemoryMapFlags)0, ppUploadData);
    cvkSetupImage(physicalDevice,
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
                  (VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                  NULL,
                  pAllocator,
                  image,
                  pMemory);
    (*pView) = VK_NULL_HANDLE;
    if (usage & (VkImageUsageFlags)((VkFlags)VK_IMAGE_USAGE_STORAGE_BIT | (VkFlags)VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        cvkCreateImageView(device,
                           NULL,
                           (VkImageViewCreateFlags)0,
                           (*image),
                           VK_IMAGE_VIEW_TYPE_2D,
                           format,
                           components,
                           view_range,
                           pAllocator,
                           pView);
    }
    cvkCreateFence(device, NULL, (VkFenceCreateFlags)0, pAllocator, pReadbackFence);
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
static void CreateVaapiImage(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    switch (createInfo->kind)
    {
    case GFX_IMAGE_KIND_STORAGE:
        CreateVaapiStorageImage(image, createInfo);
        break;

    case GFX_IMAGE_KIND_IMPORT:
        Gfx_Image_InitializeShared(image, createInfo);
        break;

    case GFX_IMAGE_KIND_EXPORT:
        Gfx_Image_InitializeShared(image, createInfo);
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
        Gfx_Image_InitializeShared(image, createInfo);
        break;

    case GFX_IMAGE_KIND_EXPORT:
        Gfx_Image_InitializeShared(image, createInfo);
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
    Gfx_Image_Storage_t* storage;

    LOGASSERT(NULL != image);
    LOGASSERT(NULL != createInfo);

    storage = (Gfx_Image_Storage_t*)OS_CALLOC(sizeof(Gfx_Image_Storage_t));
    CreateImage(&storage->image, createInfo);
    (*image) = &storage->image;
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
