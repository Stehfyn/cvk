#ifndef VULKAN_VA_ZERO_COPY_GFX_IMAGE_H
#define VULKAN_VA_ZERO_COPY_GFX_IMAGE_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_vulkan.h"
#include "os/os_types.h"

#include <stddef.h>
#include <stdint.h>
#include <va/va.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

typedef struct Gfx_Image_Platform_CreateInfo_s Gfx_Image_Platform_CreateInfo_t;

/*************************************************************************************************/
/**
 * \brief Selects the API that owns an image's native representation.
 */
typedef enum Gfx_Image_Hint_e
{
    GFX_IMAGE_HINT_VAAPI,  ///< VA surface; STORAGE also owns a derived image for CPU access.
    GFX_IMAGE_HINT_VULKAN, ///< Vulkan image or buffer storage and synchronization state.
} Gfx_Image_Hint_t;

/*************************************************************************************************/
/**
 * \brief Defines an image's role in cross-API memory sharing.
 */
typedef enum Gfx_Image_Kind_e
{
    GFX_IMAGE_KIND_IMPORT,  ///< Access native storage shared by another API.
    GFX_IMAGE_KIND_EXPORT,  ///< Expose native storage for import by another API.
    GFX_IMAGE_KIND_STORAGE, ///< API-local image without external-memory interop.
} Gfx_Image_Kind_t;

/*************************************************************************************************/
/**
 * \brief Describes the order and representation of an image's color channels.
 */
typedef enum Gfx_Image_Format_e
{
    GFX_IMAGE_FORMAT_RGBA8, ///< Interleaved red, green, blue, alpha bytes.
    GFX_IMAGE_FORMAT_BGRA8, ///< Interleaved blue, green, red, alpha bytes.
} Gfx_Image_Format_t;

/*************************************************************************************************/
/**
 * \brief Native image storage and the state needed to transfer pixels and synchronize access.
 */
typedef struct Gfx_Image_s
{
    Gfx_Image_Hint_t hint;          ///< API owning the active native representation.
    Gfx_Image_Kind_t kind;          ///< Image's external-memory sharing role.
    bool_t           acquired;      ///< TRUE while the caller has exclusive access to the image.
    bool_t           uploadPending; ///< TRUE while pixel writes await consumption.
    struct
    {
        VADisplay   dpy;       ///< Initialized VA display that owns the surface.
        VASurfaceID surfaceId; ///< VA surface containing the pixels.
        VAImage     image;     ///< Persistent CPU-accessible view; valid only for VA STORAGE images.
    } va;                      ///< VA-API representation; valid when hint is VA-API.
    struct
    {
        VkDevice             device;                                                    ///< Logical device owning every Vulkan handle below.
        VkQueue              queue;                                                     ///< Queue matching commandPool for uploads and synchronous readback.
        VkCommandPool        commandPool;                                               ///< Reusable pool for upload or interop commands.
        VkCommandBuffer      commandBuffer;                                             ///< Reusable primary command buffer allocated from commandPool.
        VkFence              uploadFence;                                               ///< Signals completion of submitted writes to the image.
        VkSemaphore          uploadSemaphore;                                           ///< Signals submitted image writes to a subsequent GPU consumer.
        VkFence              readbackFence;                                             ///< Completion of CPU readback submissions.
        VkImage              image;                                                     ///< Native image; VK_NULL_HANDLE for buffer-backed interop.
        VkDeviceMemory       memory;                                                    ///< First allocation bound to image; VK_NULL_HANDLE for buffer-backed interop.
        VkBuffer             externalBuffer;                                            ///< Linear pixel storage for interop, including the host-copy fallback.
        VkDeviceMemory       externalBufferMemory;                                      ///< Memory bound to externalBuffer.
        VkDeviceSize         externalBufferSize;                                        ///< Size of externalBuffer in bytes.
        bool_t               externalBufferHostCopy;                                    ///< TRUE when externalBuffer is a host-visible interop fallback.
        uint32_t             externalYStride;                                           ///< Luma or packed-pixel row pitch in externalBuffer, in bytes.
        uint32_t             externalUvOffset;                                          ///< Second plane byte offset in externalBuffer; zero for single-plane storage.
        uint32_t             externalUvStride;                                          ///< Second plane row pitch in externalBuffer; zero for single-plane storage.
        VkDeviceMemory       additionalMemories[VK_MAX_DRM_FORMAT_MODIFIER_PLANES - 1]; ///< Non-primary DRM plane allocations.
        uint32_t             memoryCount;                                               ///< Number of image memory allocations, including memory.
        VkBuffer             uploadBuffer;                                              ///< Staging buffer for transfers between host memory and image.
        VkDeviceMemory       uploadMemory;                                              ///< Coherent host-visible memory bound to uploadBuffer.
        void*                pUploadData;                                               ///< Persistently mapped address of uploadMemory.
        VkImageView          view;                                                      ///< Shader-visible view of image.
        VkSemaphore          externalSemaphore;                                         ///< Timeline semaphore shared with the paired API.
        uint64_t             externalSemaphoreValue;                                    ///< Most recently queued signal value on the shared timeline.
        VkFormat             format;                                                    ///< Pixel format of the Vulkan image or external buffer.
        VkExtent3D           extent;                                                    ///< Pixel extent of the Vulkan image or external buffer.
        size_t               uploadByteCount;                                           ///< Packed pixel byte count required for Vulkan STORAGE reads and writes.
        VkPipelineStageFlags readyStageMask;                                            ///< Stage mask describing the image's current GENERAL state.
        VkAccessFlags        readyAccessMask;                                           ///< Access mask describing the image's current GENERAL state.
        bool_t               layoutInitialized;                                         ///< TRUE after image first transitions out of UNDEFINED.
        bool_t               uploadSubmitted;                                           ///< TRUE when submitted writes require a wait on uploadSemaphore before the image is consumed.
    } vk;                                                                               ///< Vulkan representation; valid when hint is Vulkan.
} Gfx_Image_t;

/*************************************************************************************************/
/**
 * \brief Specifies an image's backend, memory-sharing role, dimensions, and native resources.
 */
typedef struct Gfx_Image_CreateInfo_s
{
    Gfx_Image_Hint_t hint;   ///< API that owns the image representation.
    Gfx_Image_Kind_t kind;   ///< Import, export, or API-local storage role.
    uint32_t         width;  ///< Image width in pixels.
    uint32_t         height; ///< Image height in pixels.
    struct
    {
        VADisplay dpy;         ///< Initialized VA display.
        uint32_t  format;      ///< VA render-target format.
        uint32_t  pixelFormat; ///< VA fourcc.
    } va;                      ///< VA-API creation parameters.
    struct
    {
        VkPhysicalDevice             physicalDevice;           ///< Device used for memory-type selection.
        VkDevice                     device;                   ///< Logical device owning the image.
        VkQueue                      queue;                    ///< Queue matching commandPool; required for local-storage readback.
        const VkAllocationCallbacks* pAllocator;               ///< Vulkan allocator, or NULL.
        VkFormat                     format;                   ///< Native Vulkan image format.
        VkImageUsageFlags            usage;                    ///< STORAGE requires TRANSFER_DST_BIT; STORAGE_BIT or SAMPLED_BIT also creates a view.
        VkCommandPool                commandPool;              ///< Borrowed upload command pool for local storage.
        VkCommandBuffer              commandBuffer;            ///< Borrowed upload command buffer for local storage.
    } vk;                                                      ///< Vulkan creation parameters.
    const Gfx_Image_Platform_CreateInfo_t* platformCreateInfo; ///< Required for IMPORT/EXPORT; ignored for STORAGE.
} Gfx_Image_CreateInfo_t;

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Allocates an image and creates its native representation.
 *
 * \par Details:
 *  IMPORT/EXPORT requires platformCreateInfo and platform-supported formats and handles.
 *  Linux supports VA and Vulkan imports and exports. Windows supports VA imports and exports and Vulkan imports.
 *  Shared storage must exist before importing it; Vulkan EXPORT is unsupported on Windows.
 *  VA STORAGE retains a derived image for CPU access. VA IMPORT/EXPORT does not derive an image.
 *  Vulkan STORAGE requires transfer-destination usage and adds transfer-source usage for readback.
 *  Its queue, command pool, and command buffer are borrowed from the caller.
 *  Devices, shared resources, and borrowed command objects must outlive the image.
 *  There is no image destruction API; created resources are retained for process lifetime.
 *
 * \param[out] image      Receives the allocated image; initialized by this function
 * \param[in]  createInfo Backend, sharing role, extent, and native creation parameters
 *
 * \retval None
 */
void Gfx_Image_Create(Gfx_Image_t** image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Copies current image pixels to host memory and waits for completion.
 *
 * \par Details:
 *  Supports STORAGE images on both backends. Vulkan reads return tightly packed pixels and require
 *  dstSize to equal uploadByteCount. VA reads remove native row padding; dstSize must hold all packed
 *  planes and fit uint32_t. Vulkan contents must have been initialized or have a pending write.
 *  Readback preserves uploadPending. Unsubmitted Vulkan writes are submitted with the readback,
 *  and their command buffer is reopened afterward. Already submitted uploads are waited on.
 *  Other work using the image or its borrowed command pool must be complete before this call.
 *  The caller must exclusively own the acquired image and the Vulkan queue and command objects it uses.
 *
 * \param[in,out] image   Non-NULL acquired image
 * \param[out]    dst     Non-NULL destination storage filled during this call
 * \param[in]     dstSize Destination size in bytes
 *
 * \retval    None
 */
void Gfx_Image_ReadMemory(Gfx_Image_t* image, void* dst, uint64_t dstSize);

/*************************************************************************************************/
/**
 * \brief Copies host pixels into an acquired image and marks the write pending.
 *
 * \par Details:
 *  Supports STORAGE images on both backends. Vulkan writes require srcSize to equal uploadByteCount
 *  and record a transfer for later submission by the image consumer. VA writes copy all packed planes
 *  into the persistent derived image; srcSize must hold those planes and fit uint32_t.
 *  The image must be acquired with no pending write. Previous GPU use of the Vulkan image and its
 *  borrowed command pool must be complete; the caller must exclusively own those command objects.
 *
 * \param[in,out] image   Non-NULL acquired image with no pending upload
 * \param[in]     src     Non-NULL source storage copied during this call
 * \param[in]     srcSize Number of source bytes
 *
 * \retval    None
 */
void Gfx_Image_WriteMemory(Gfx_Image_t* image, const void* src, uint64_t srcSize);

#endif /* VULKAN_VA_ZERO_COPY_GFX_IMAGE_H */
