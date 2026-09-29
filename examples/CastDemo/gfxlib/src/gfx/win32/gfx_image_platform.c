/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Native image construction composes the pinned CVK and cva interfaces.
 * Resource ownership follows gfx_image.h; platform selection belongs to the build.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_image_internal.h"
#include "gfx/gfx_image.h"
#include "gfx/gfx_image_platform.h"
#include "cvk/cvk.h"
#include "cva.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_types.h"

#include <stdint.h>
#include <stddef.h>
#include <vulkan/vulkan_core.h>

#include "cvk/win32/cvk_win32.h"
#include "win32/cva_win32.h"
#include <minwindef.h>
#include <winnt.h>
#include <handleapi.h>
#include <va/va.h>
#include <va/va_win32.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define IMPORT_MEMORY_TYPE_INDEX  (0) ///< Native import memory type.
#define IMPORT_DESCRIPTOR_INDEX   (1) ///< Native import resource descriptor.
#define IMPORT_PIXEL_FORMAT_INDEX (2) ///< Native import pixel format.
#define IMPORT_USAGE_INDEX        (3) ///< Native encoder usage.
#define IMPORT_ATTRIBUTE_COUNT    (4) ///< Complete native import schema.
#define EXPORT_PIXEL_FORMAT_INDEX (0) ///< Export pixel format.
#define EXPORT_USAGE_INDEX        (1) ///< Encoder and export usage.
#define EXPORT_MEMORY_TYPE_INDEX  (2) ///< Win32 handle memory type.
#define EXPORT_DESCRIPTOR_INDEX   (3) ///< Borrowed handle being re-exported by VA.
#define EXPORT_ATTRIBUTE_COUNT    (4) ///< Complete Windows export schema.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

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
 * \brief Imports the configured D3D resource or NT handle into the image's VA surface.
 *
 * \par Requirements:
 *  - 001 Native surface attributes shall use the selected D3D resource or NT-handle representation.
 *  - 002 Pixel-format narrowing and required resource handles shall be validated before creation.
 *  - 004 Construction shall occur during startup using an unpublished initialized owner.
 *
 * \param[in,out] image   Image owner initialized with no native resources
 * \param[in] createInfo   Validated image and native sharing configuration
 *
 * \retval None
 */
static void InitializeVaImport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Constructs the image's exported VA surface and replaces its input handle after export.
 *
 * \par Requirements:
 *  - 001 The input handle shall remain published until a non-NULL export handle is available.
 *  - 002 After export, the old handle shall be closed and replaced exactly once.
 *  - 004 Construction shall occur during startup using an unpublished initialized owner.
 *
 * \param[in,out] image   Image owner initialized with no native resources
 * \param[in] createInfo   Validated image and native sharing configuration
 *
 * \retval None
 */
static void InitializeVaExport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Constructs the selected shared or host-copy transfer representation with CVK.
 *
 * \par Requirements:
 *  - 001 Transfer extent and required handles shall be valid before construction.
 *  - 002 Native fence import support shall be queried before constructing the shared GPU buffer.
 *  - 003 Shared resources shall use CVK import; host-copy storage shall remain mapped for its owner.
 *  - 004 Construction shall occur during startup using an unpublished initialized owner.
 *
 * \param[in,out] image   Image owner initialized with no native resources
 * \param[in] createInfo   Validated image and native sharing configuration
 *
 * \retval None
 */
static void InitializeVulkanTransfer(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Imports image storage through CVK or selects its transfer-buffer representation.
 *
 * \par Requirements:
 *  - 001 Image handles shall identify D3D12 resources or heaps; heap imports require their complete extent.
 *  - 002 CVK shall query import support and dedicated-allocation requirements before allocating memory.
 *  - 003 The imported image allocation shall be stored directly in its owning image.
 *  - 004 Construction shall occur during startup using an unpublished initialized owner.
 *
 * \param[in,out] image   Image owner initialized with no native resources
 * \param[in] createInfo   Validated image and native sharing configuration
 *
 * \retval None
 */
static void InitializeVulkanImport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InitializeVaImport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    const Gfx_Image_Platform_CreateInfo_t* platform;
    VASurfaceAttrib                        attributes[IMPORT_ATTRIBUTE_COUNT];
    void*                                  resource;
    uint32_t                               memory_type;
    bool_t                                 has_resource;

    platform = createInfo->platformCreateInfo;
    LOGASSERT(NULL != platform);
    LOGASSERT(INT32_MAX >= createInfo->va.pixelFormat);
    has_resource = NULL != platform->sharedImageResource;
    if (has_resource)
    {
        resource    = platform->sharedImageResource;
        memory_type = VA_SURFACE_ATTRIB_MEM_TYPE_D3D12_RESOURCE;
    }
    else
    {
        LOGASSERT(NULL != platform->sharedHandle);
        LOGASSERT(NULL != (*platform->sharedHandle));
        resource    = (*platform->sharedHandle);
        memory_type = VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE;
    }
    cvaInitSurfaceAttribInteger(&attributes[IMPORT_MEMORY_TYPE_INDEX],
                                VASurfaceAttribMemoryType,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                (int32_t)memory_type);
    cvaInitSurfaceAttribPointer(&attributes[IMPORT_DESCRIPTOR_INDEX],
                                VASurfaceAttribExternalBufferDescriptor,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                (void*)&resource);
    cvaInitSurfaceAttribInteger(&attributes[IMPORT_PIXEL_FORMAT_INDEX],
                                VASurfaceAttribPixelFormat,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                (int32_t)createInfo->va.pixelFormat);
    cvaInitSurfaceAttribInteger(&attributes[IMPORT_USAGE_INDEX],
                                VASurfaceAttribUsageHint,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER);
    cvaCreateSurface(createInfo->va.dpy,
                     createInfo->va.format,
                     createInfo->width,
                     createInfo->height,
                     &image->va.surfaceId,
                     attributes,
                     IMPORT_ATTRIBUTE_COUNT);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InitializeVaExport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    const Gfx_Image_Platform_CreateInfo_t* platform;
    VASurfaceAttrib                        attributes[EXPORT_ATTRIBUTE_COUNT];
    HANDLE                                 import_handle;
    HANDLE                                 export_handle;
    BOOL                                   handle_closed;

    platform = createInfo->platformCreateInfo;
    LOGASSERT(NULL != platform);
    LOGASSERT(NULL != platform->sharedHandle);
    LOGASSERT(NULL != (*platform->sharedHandle));
    LOGASSERT(INT32_MAX >= createInfo->va.pixelFormat);
    import_handle = (*platform->sharedHandle);
    export_handle = NULL;

    cvaInitSurfaceAttribInteger(&attributes[EXPORT_PIXEL_FORMAT_INDEX],
                                VASurfaceAttribPixelFormat,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                (int32_t)createInfo->va.pixelFormat);
    cvaInitSurfaceAttribInteger(&attributes[EXPORT_USAGE_INDEX],
                                VASurfaceAttribUsageHint,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER | VA_SURFACE_ATTRIB_USAGE_HINT_EXPORT);
    cvaInitSurfaceAttribInteger(&attributes[EXPORT_MEMORY_TYPE_INDEX],
                                VASurfaceAttribMemoryType,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE);
    cvaInitSurfaceAttribPointer(&attributes[EXPORT_DESCRIPTOR_INDEX],
                                VASurfaceAttribExternalBufferDescriptor,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                (void*)&import_handle);
    cvaCreateSurface(createInfo->va.dpy,
                     createInfo->va.format,
                     createInfo->width,
                     createInfo->height,
                     &image->va.surfaceId,
                     attributes,
                     EXPORT_ATTRIBUTE_COUNT);
    cvaExportSurfaceWin32Handle(createInfo->va.dpy,
                                image->va.surfaceId,
                                VA_EXPORT_SURFACE_WRITE_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS,
                                &export_handle);
    LOGASSERT(NULL != export_handle);
    handle_closed = CloseHandle(import_handle);
    LOGASSERT(handle_closed);
    (*platform->sharedHandle) = export_handle;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InitializeVulkanTransfer(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    const Gfx_Image_Platform_CreateInfo_t* platform;
    VkExternalSemaphoreProperties          semaphore_properties;
    bool_t                                 uses_host_copy;

    platform       = createInfo->platformCreateInfo;
    uses_host_copy = platform->transferBufferHostCopy;
    LOGASSERT(0 < platform->transferBufferSize);
    LOGASSERT(UINT32_MAX >= platform->uvOffset);
    LOGASSERT(NULL != platform->ppVulkanTransferBytes);
    LOGASSERT(NULL == (*platform->ppVulkanTransferBytes));

    if (uses_host_copy)
    {
        cvkSetupBuffer(createInfo->vk.physicalDevice,
                       createInfo->vk.device,
                       NULL,
                       (VkBufferCreateFlags)0,
                       platform->transferBufferSize,
                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                       VK_SHARING_MODE_EXCLUSIVE,
                       0,
                       NULL,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       NULL,
                       createInfo->vk.pAllocator,
                       &image->vk.externalBuffer,
                       &image->vk.externalBufferMemory);
        cvkMapMemory(createInfo->vk.device,
                     image->vk.externalBufferMemory,
                     VK_NO_OFFSET,
                     VK_WHOLE_SIZE,
                     (VkMemoryMapFlags)0,
                     platform->ppVulkanTransferBytes);
    }
    else
    {
        LOGASSERT(NULL != platform->sharedTransferBufferHandle);
        LOGASSERT(NULL != platform->sharedFenceHandle);
        cvkGetPhysicalDeviceExternalSemaphoreInfo(createInfo->vk.physicalDevice,
                                                  NULL,
                                                  VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT,
                                                  NULL,
                                                  &semaphore_properties);
        LOGASSERT(semaphore_properties.externalSemaphoreFeatures & VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT);
        cvkImportWin32HandleBuffer(createInfo->vk.physicalDevice,
                                   createInfo->vk.device,
                                   NULL,
                                   (VkBufferCreateFlags)0,
                                   platform->transferBufferSize,
                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   VK_SHARING_MODE_EXCLUSIVE,
                                   0,
                                   NULL,
                                   VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
                                   platform->sharedTransferBufferHandle,
                                   NULL,
                                   0,
                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                   NULL,
                                   createInfo->vk.pAllocator,
                                   &image->vk.externalBuffer,
                                   &image->vk.externalBufferMemory);
        cvkCreateSemaphore(createInfo->vk.device,
                           NULL,
                           (VkSemaphoreCreateFlags)0,
                           createInfo->vk.pAllocator,
                           &image->vk.externalSemaphore);
        cvkImportSemaphoreWin32Handle(createInfo->vk.device,
                                      NULL,
                                      image->vk.externalSemaphore,
                                      (VkSemaphoreImportFlags)0,
                                      VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT,
                                      platform->sharedFenceHandle,
                                      NULL);
    }
    image->vk.externalBufferSize     = platform->transferBufferSize;
    image->vk.externalBufferHostCopy = uses_host_copy;
    image->vk.externalYStride        = platform->yStride;
    image->vk.externalUvOffset       = (uint32_t)platform->uvOffset;
    image->vk.externalUvStride       = platform->uvStride;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InitializeVulkanImport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    const Gfx_Image_Platform_CreateInfo_t* platform;
    bool_t                                 has_image;
    bool_t                                 is_resource;
    bool_t                                 is_heap;
    bool_t                                 valid_handle_type;

    platform  = createInfo->platformCreateInfo;
    has_image = NULL != platform->sharedImageHandle;
    if (has_image)
    {
        is_resource       = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT == platform->sharedImageHandleType;
        is_heap           = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT == platform->sharedImageHandleType;
        valid_handle_type = is_resource || is_heap;
        LOGASSERT(valid_handle_type);
        if (is_heap)
        {
            LOGASSERT(0 < platform->sharedImageAllocationSize);
        }
        cvkImportWin32HandleImage(createInfo->vk.physicalDevice,
                                  createInfo->vk.device,
                                  NULL,
                                  (VkImageCreateFlags)0,
                                  VK_IMAGE_TYPE_2D,
                                  createInfo->vk.format,
                                  image->vk.extent,
                                  1,
                                  1,
                                  VK_SAMPLE_COUNT_1_BIT,
                                  VK_IMAGE_TILING_OPTIMAL,
                                  createInfo->vk.usage,
                                  VK_SHARING_MODE_EXCLUSIVE,
                                  0,
                                  NULL,
                                  VK_IMAGE_LAYOUT_UNDEFINED,
                                  platform->sharedImageHandleType,
                                  platform->sharedImageHandle,
                                  NULL,
                                  platform->sharedImageAllocationSize,
                                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                  NULL,
                                  createInfo->vk.pAllocator,
                                  &image->vk.image,
                                  &image->vk.memory);
        image->vk.memoryCount = 1;
    }
    else
    {
        InitializeVulkanTransfer(image, createInfo);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  No separate VA or Vulkan wrapper outputs escape the image owner.
 */
void Gfx_Image_InitializeShared(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    bool_t is_import;
    bool_t is_export;
    bool_t is_shared;
    bool_t is_vulkan;
    bool_t is_va;
    bool_t valid_backend;

    LOGASSERT(NULL != image);
    LOGASSERT(NULL != createInfo);
    LOGASSERT(NULL != createInfo->platformCreateInfo);
    LOGASSERT(0 < createInfo->width);
    LOGASSERT(0 < createInfo->height);
    is_import     = GFX_IMAGE_KIND_IMPORT == createInfo->kind;
    is_export     = GFX_IMAGE_KIND_EXPORT == createInfo->kind;
    is_shared     = is_import || is_export;
    is_vulkan     = GFX_IMAGE_HINT_VULKAN == createInfo->hint;
    is_va         = GFX_IMAGE_HINT_VAAPI == createInfo->hint;
    valid_backend = is_vulkan || is_va;
    LOGASSERT(is_shared);
    LOGASSERT(valid_backend);

    CRT_Memory_Set(image, 0, sizeof(*image));
    image->hint         = createInfo->hint;
    image->kind         = createInfo->kind;
    image->va.surfaceId = VA_INVALID_SURFACE;
    if (is_va)
    {
        LOGASSERT(NULL != createInfo->va.dpy);
        image->va.dpy = createInfo->va.dpy;
        if (is_import)
        {
            InitializeVaImport(image, createInfo);
        }
        else
        {
            InitializeVaExport(image, createInfo);
        }
    }
    else
    {
        LOGASSERT(VK_NULL_HANDLE != createInfo->vk.physicalDevice);
        LOGASSERT(VK_NULL_HANDLE != createInfo->vk.device);
        LOGASSERT(VK_FORMAT_UNDEFINED != createInfo->vk.format);
        image->vk.device        = createInfo->vk.device;
        image->vk.format        = createInfo->vk.format;
        image->vk.extent.width  = createInfo->width;
        image->vk.extent.height = createInfo->height;
        image->vk.extent.depth  = 1;
        if (is_import)
        {
            InitializeVulkanImport(image, createInfo);
        }
        else
        {
            OS_AssertFailure("Vulkan image export is unsupported on Windows", __FILE__, __LINE__);
        }
    }
}
