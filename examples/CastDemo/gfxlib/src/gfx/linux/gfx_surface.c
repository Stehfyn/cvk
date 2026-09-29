/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cvk/cvk.h"
#include "cvk/xlib/cvk_xlib.h"
#include <X11/Xlib.h>
#include <string.h>

#include "os/os_assert.h"
#include "os/os_malloc.h"
#include "gfx/gfx_display_internal.h"
#include "gfx/gfx_image.h"
#include "gfx/gfx_image_platform.h"
#include "gfx/gfx_surface.h"
#include "gfx/gfx_surface_internal.h"
#include <va/va_drmcommon.h>
#include "utl/utl_defines.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Native allocation descriptor shared by the surface's image views.
 */
typedef struct DMABufSharedSurface_t
{
    VADRMPRIMESurfaceDescriptor drmPrimeDescriptor; ///< Shared allocation's file descriptors, format, and plane layout.
} DMABufSharedSurface_t;

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
 * \brief Maps a public surface format to its Vulkan image format.
 *
 * \param[in] pixelFormat        YUV420P, NV12, RGBA8, or BGRA8 surface representation
 *
 * \return Matching Vulkan 4:2:0 multiplanar or four-channel RGB format; unsupported values are fatal.
 */
static VkFormat GetVulkanFormat(Gfx_Surface_PixelFormat_t pixelFormat);

/*************************************************************************************************/
/**
 * \brief Maps a public surface format to its VA render-target category.
 *
 * \param[in] pixelFormat        YUV420P, NV12, RGBA8, or BGRA8 surface representation
 *
 * \return VA_RT_FORMAT_YUV420 for YUV420P/NV12 or VA_RT_FORMAT_RGB32 for RGBA8/BGRA8.
 */
static uint32_t GetVaapiFormat(Gfx_Surface_PixelFormat_t pixelFormat);

/*************************************************************************************************/
/**
 * \brief Maps a public surface format to its VA fourcc.
 *
 * \param[in] pixelFormat        YUV420P, NV12, RGBA8, or BGRA8 surface representation
 *
 * \return VA_FOURCC_I420, VA_FOURCC_NV12, VA_FOURCC_RGBA, or VA_FOURCC_BGRA.
 */
static uint32_t GetVaapiPixelFormat(Gfx_Surface_PixelFormat_t pixelFormat);

/*************************************************************************************************/
/**
 * \brief Validates the presentation or storage creation requirements.
 *
 * \param[in] pCreateInfo        Non-NULL surface configuration
 *
 * \retval    None
 */
static void ValidateCreateInfo(const Gfx_Surface_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Creates and initializes an Xlib window presentation surface.
 *
 * \param[out] pSurface       Receives the native surface and window dimensions
 * \param[in]  pCreateInfo    Valid windowed presentation configuration
 *
 * \retval    None
 */
static void CreateWindowedPresentSurface(Gfx_Surface_t* pSurface, const Gfx_Surface_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Creates and initializes a headless presentation surface.
 *
 * \param[out] pSurface       Receives the native surface and requested dimensions
 * \param[in]  pCreateInfo    Valid headless presentation configuration
 *
 * \retval    None
 */
static void CreateHeadlessPresentSurface(Gfx_Surface_t* pSurface, const Gfx_Surface_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Creates a native presentation surface for the selected display kind.
 *
 * \param[out] pSurface        Presentation surface receiving the native VkSurfaceKHR and resolved extent
 * \param[in] pCreateInfo      Validated display and Vulkan instance configuration
 *
 * \retval    None
 */
static void CreatePresentSurface(Gfx_Surface_Storage_t* storage, const Gfx_Surface_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Creates shared storage with a VA read view and a Vulkan write view.
 *
 * \param[out] pSurface        Storage surface receiving VA state, PRIME metadata, and logical images
 * \param[in] pCreateInfo      Storage format, dimensions, and matching VA/Vulkan device contexts
 *
 * \retval    None
 */
static void CreateStorageSurface(Gfx_Surface_t* pSurface, const Gfx_Surface_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Creates a presentation or shared-storage surface.
 *
 * \param[out] pSurface        Zero-initialized destination
 * \param[in] pCreateInfo      Kind-specific creation request
 *
 * \retval    None
 */
static void CreateSurface(Gfx_Surface_Storage_t* storage, const Gfx_Surface_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Completes pending Vulkan writes before storage reuse.
 *
 * \param[in,out] pSurface Storage surface with no active reader
 *
 * \retval None
 */
static void AcquireSurface(Gfx_Surface_t* pSurface);

/*************************************************************************************************/
/**
 * \brief Publishes the read view without waiting for pending writes.
 *
 * \param[in,out] pSurface Storage surface with submitted writes and writePending set
 *
 * \retval None
 */
static void ReleaseSurface(Gfx_Surface_t* pSurface);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static VkFormat GetVulkanFormat(Gfx_Surface_PixelFormat_t pixelFormat)
{
    switch (pixelFormat)
    {
    case GFX_SURFACE_PIXELFORMAT_YUV420P:
        return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
    case GFX_SURFACE_PIXELFORMAT_NV12:
        return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
    case GFX_SURFACE_PIXELFORMAT_BGRA8:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case GFX_SURFACE_PIXELFORMAT_RGBA8:
        return VK_FORMAT_R8G8B8A8_UNORM;
    default:
        LOGFATAL("Unsupported Vulkan surface pixel format");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint32_t GetVaapiFormat(Gfx_Surface_PixelFormat_t pixelFormat)
{
    switch (pixelFormat)
    {
    case GFX_SURFACE_PIXELFORMAT_YUV420P:
    case GFX_SURFACE_PIXELFORMAT_NV12:
        return VA_RT_FORMAT_YUV420;
    case GFX_SURFACE_PIXELFORMAT_BGRA8:
    case GFX_SURFACE_PIXELFORMAT_RGBA8:
        return VA_RT_FORMAT_RGB32;
    default:
        LOGFATAL("Unsupported VA surface format");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint32_t GetVaapiPixelFormat(Gfx_Surface_PixelFormat_t pixelFormat)
{
    switch (pixelFormat)
    {
    case GFX_SURFACE_PIXELFORMAT_YUV420P:
        return VA_FOURCC_I420;
    case GFX_SURFACE_PIXELFORMAT_NV12:
        return VA_FOURCC_NV12;
    case GFX_SURFACE_PIXELFORMAT_BGRA8:
        return VA_FOURCC_BGRA;
    case GFX_SURFACE_PIXELFORMAT_RGBA8:
        return VA_FOURCC_RGBA;
    default:
        LOGFATAL("Unsupported VA surface pixel format");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ValidateCreateInfo(const Gfx_Surface_CreateInfo_t* pCreateInfo)
{
    LOGASSERT(NULL != pCreateInfo);
    LOGASSERT(VK_NULL_HANDLE != pCreateInfo->instance);
    LOGASSERT(VK_NULL_HANDLE != pCreateInfo->physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != pCreateInfo->device);
    LOGASSERT(VK_NULL_HANDLE != pCreateInfo->queue);
    LOGASSERT(VK_INVALID != pCreateInfo->queueFamilyIndex);
    LOGASSERT(NULL == pCreateInfo->pAllocator);
    LOGASSERT((GFX_SURFACE_KIND_PRESENT == pCreateInfo->kind) || (GFX_SURFACE_KIND_STORAGE == pCreateInfo->kind));

    if (GFX_SURFACE_KIND_PRESENT == pCreateInfo->kind)
    {
        LOGASSERT(NULL != pCreateInfo->pDisplay);
        LOGASSERT(0 != pCreateInfo->pDisplay->width);
        LOGASSERT(0 != pCreateInfo->pDisplay->height);
        LOGASSERT(0 == pCreateInfo->width);
        LOGASSERT(0 == pCreateInfo->height);
        return;
    }
    LOGASSERT((GFX_SURFACE_PIXELFORMAT_YUV420P == pCreateInfo->pixelFormat) ||
              (GFX_SURFACE_PIXELFORMAT_NV12 == pCreateInfo->pixelFormat) ||
              (GFX_SURFACE_PIXELFORMAT_RGBA8 == pCreateInfo->pixelFormat) ||
              (GFX_SURFACE_PIXELFORMAT_BGRA8 == pCreateInfo->pixelFormat));
    LOGASSERT((GFX_SURFACE_PIXELFORMAT_RGBA8 == pCreateInfo->pixelFormat) ||
              (GFX_SURFACE_PIXELFORMAT_BGRA8 == pCreateInfo->pixelFormat) ||
              ((0 == (pCreateInfo->width & 1u)) && (0 == (pCreateInfo->height & 1u))));
    LOGASSERT(NULL == pCreateInfo->pDisplay);
    LOGASSERT((0 != pCreateInfo->width) && (0 != pCreateInfo->height));
    LOGASSERT(NULL != pCreateInfo->dpy);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateWindowedPresentSurface(Gfx_Surface_t* pSurface, const Gfx_Surface_CreateInfo_t* pCreateInfo)
{
    Display*     dpy;
    VkSurfaceKHR surface;

    dpy = (Display*)pCreateInfo->pDisplay->nativeDisplay;
    LOGASSERT(NULL != dpy);

    cvkCreateXlibSurface(pCreateInfo->instance,
                         NULL,
                         (VkXlibSurfaceCreateFlagsKHR)0,
                         dpy,
                         (Window)pCreateInfo->pDisplay->nativeWindow,
                         pCreateInfo->pAllocator,
                         &surface);

    pSurface->kind                = pCreateInfo->kind;
    pSurface->va.dpy              = NULL;
    pSurface->va.format           = 0;
    pSurface->va.pixelFormat      = 0;
    pSurface->vk.physicalDevice   = pCreateInfo->physicalDevice;
    pSurface->vk.device           = pCreateInfo->device;
    pSurface->vk.queue            = pCreateInfo->queue;
    pSurface->vk.surface          = surface;
    pSurface->vk.queueFamilyIndex = pCreateInfo->queueFamilyIndex;
    pSurface->vk.format           = VK_FORMAT_UNDEFINED;
    pSurface->vk.pAllocator       = pCreateInfo->pAllocator;
    pSurface->width               = pCreateInfo->pDisplay->width;
    pSurface->height              = pCreateInfo->pDisplay->height;
    pSurface->target.readImage    = NULL;
    pSurface->target.writeImage   = NULL;
    pSurface->acquiredImage       = NULL;
    pSurface->platform            = NULL;
    pSurface->vk.completionFence  = VK_NULL_HANDLE;
    pSurface->writePending        = FALSE;
    pSurface->readPending         = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateHeadlessPresentSurface(Gfx_Surface_t* pSurface, const Gfx_Surface_CreateInfo_t* pCreateInfo)
{
    VkSurfaceKHR surface;

    cvkCreateHeadlessSurface(pCreateInfo->instance,
                             NULL,
                             (VkHeadlessSurfaceCreateFlagsEXT)0,
                             pCreateInfo->pAllocator,
                             &surface);

    pSurface->kind                = pCreateInfo->kind;
    pSurface->va.dpy              = NULL;
    pSurface->va.format           = 0;
    pSurface->va.pixelFormat      = 0;
    pSurface->vk.physicalDevice   = pCreateInfo->physicalDevice;
    pSurface->vk.device           = pCreateInfo->device;
    pSurface->vk.queue            = pCreateInfo->queue;
    pSurface->vk.surface          = surface;
    pSurface->vk.queueFamilyIndex = pCreateInfo->queueFamilyIndex;
    pSurface->vk.format           = VK_FORMAT_UNDEFINED;
    pSurface->vk.pAllocator       = pCreateInfo->pAllocator;
    pSurface->width               = pCreateInfo->pDisplay->width;
    pSurface->height              = pCreateInfo->pDisplay->height;
    pSurface->target.readImage    = NULL;
    pSurface->target.writeImage   = NULL;
    pSurface->acquiredImage       = NULL;
    pSurface->platform            = NULL;
    pSurface->vk.completionFence  = VK_NULL_HANDLE;
    pSurface->writePending        = FALSE;
    pSurface->readPending         = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreatePresentSurface(Gfx_Surface_Storage_t* storage, const Gfx_Surface_CreateInfo_t* pCreateInfo)
{
    Gfx_Surface_t* pSurface;

    LOGASSERT(NULL != storage);
    pSurface = &storage->surface;

    switch (pCreateInfo->pDisplay->kind)
    {
    case GFX_DISPLAY_KIND_WINDOWED:
        CreateWindowedPresentSurface(pSurface, pCreateInfo);
        break;

    case GFX_DISPLAY_KIND_DIRECT:
        Gfx_Surface_InitializeDirect(storage, pCreateInfo);
        break;

    case GFX_DISPLAY_KIND_HEADLESS:
        CreateHeadlessPresentSurface(pSurface, pCreateInfo);
        break;

    default:
        LOGFATAL("Unsupported display kind");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateStorageSurface(Gfx_Surface_t* pSurface, const Gfx_Surface_CreateInfo_t* pCreateInfo)
{
    DMABufSharedSurface_t*          shared_surface;
    Gfx_Image_CreateInfo_t          vk_image_create_info;
    Gfx_Image_CreateInfo_t          va_image_create_info;
    Gfx_Image_Platform_CreateInfo_t platform_create_info;
    Gfx_Image_t*                    export_image;
    Gfx_Image_t*                    import_image;
    uint32_t                        va_format;
    uint32_t                        va_pixel_format;
    VkFormat                        vk_format;
    uint32_t                        object_index;

    shared_surface = (DMABufSharedSurface_t*)OS_CALLOC(sizeof(DMABufSharedSurface_t));

    va_format       = GetVaapiFormat(pCreateInfo->pixelFormat);
    va_pixel_format = GetVaapiPixelFormat(pCreateInfo->pixelFormat);
    vk_format       = GetVulkanFormat(pCreateInfo->pixelFormat);

    for (object_index = 0; object_index < COUNT_OF(shared_surface->drmPrimeDescriptor.objects); ++object_index)
    {
        shared_surface->drmPrimeDescriptor.objects[object_index].fd = -1;
    }

    platform_create_info.sharedHandle               = NULL;
    platform_create_info.sharedImageResource        = NULL;
    platform_create_info.sharedImageHandle          = NULL;
    platform_create_info.sharedImageHandleType      = (VkExternalMemoryHandleTypeFlagBits)0;
    platform_create_info.sharedImageAllocationSize  = 0;
    platform_create_info.transferBufferHostCopy     = FALSE;
    platform_create_info.transferBufferSize         = 0;
    platform_create_info.sharedTransferBufferHandle = NULL;
    platform_create_info.sharedFenceHandle          = NULL;
    platform_create_info.yStride                    = 0;
    platform_create_info.uvOffset                   = 0;
    platform_create_info.uvStride                   = 0;
    platform_create_info.ppVulkanTransferBytes      = NULL;
    platform_create_info.drmPrimeDescriptor         = &shared_surface->drmPrimeDescriptor;

    va_image_create_info.hint               = GFX_IMAGE_HINT_VAAPI;
    va_image_create_info.kind               = GFX_IMAGE_KIND_IMPORT;
    va_image_create_info.width              = pCreateInfo->width;
    va_image_create_info.height             = pCreateInfo->height;
    va_image_create_info.platformCreateInfo = &platform_create_info;
    va_image_create_info.va.dpy             = pCreateInfo->dpy;
    va_image_create_info.va.format          = va_format;
    va_image_create_info.va.pixelFormat     = va_pixel_format;
    va_image_create_info.vk.physicalDevice  = pCreateInfo->physicalDevice;
    va_image_create_info.vk.device          = pCreateInfo->device;
    va_image_create_info.vk.queue           = VK_NULL_HANDLE;
    va_image_create_info.vk.pAllocator      = pCreateInfo->pAllocator;
    va_image_create_info.vk.format          = vk_format;
    va_image_create_info.vk.usage           = (VkImageUsageFlags)VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    va_image_create_info.vk.commandPool     = VK_NULL_HANDLE;
    va_image_create_info.vk.commandBuffer   = VK_NULL_HANDLE;

    vk_image_create_info.hint               = GFX_IMAGE_HINT_VULKAN;
    vk_image_create_info.kind               = GFX_IMAGE_KIND_EXPORT;
    vk_image_create_info.width              = pCreateInfo->width;
    vk_image_create_info.height             = pCreateInfo->height;
    vk_image_create_info.platformCreateInfo = &platform_create_info;
    vk_image_create_info.va.dpy             = pCreateInfo->dpy;
    vk_image_create_info.va.format          = va_format;
    vk_image_create_info.va.pixelFormat     = va_pixel_format;
    vk_image_create_info.vk.physicalDevice  = pCreateInfo->physicalDevice;
    vk_image_create_info.vk.device          = pCreateInfo->device;
    vk_image_create_info.vk.queue           = VK_NULL_HANDLE;
    vk_image_create_info.vk.pAllocator      = pCreateInfo->pAllocator;
    vk_image_create_info.vk.format          = vk_format;
    vk_image_create_info.vk.usage           = (VkImageUsageFlags)VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    vk_image_create_info.vk.commandPool     = VK_NULL_HANDLE;
    vk_image_create_info.vk.commandBuffer   = VK_NULL_HANDLE;

    Gfx_Image_Create(&export_image, &vk_image_create_info);
    Gfx_Image_Create(&import_image, &va_image_create_info);

    pSurface->kind                = pCreateInfo->kind;
    pSurface->pixelFormat         = pCreateInfo->pixelFormat;
    pSurface->va.dpy              = pCreateInfo->dpy;
    pSurface->va.format           = va_format;
    pSurface->va.pixelFormat      = va_pixel_format;
    pSurface->vk.physicalDevice   = pCreateInfo->physicalDevice;
    pSurface->vk.device           = pCreateInfo->device;
    pSurface->vk.queue            = pCreateInfo->queue;
    pSurface->vk.surface          = VK_NULL_HANDLE;
    pSurface->vk.queueFamilyIndex = pCreateInfo->queueFamilyIndex;
    pSurface->vk.format           = vk_format;
    pSurface->vk.pAllocator       = pCreateInfo->pAllocator;
    pSurface->width               = pCreateInfo->width;
    pSurface->height              = pCreateInfo->height;
    pSurface->target.readImage    = import_image;
    pSurface->target.writeImage   = export_image;
    pSurface->acquiredImage       = NULL;
    pSurface->platform            = shared_surface;
    pSurface->vk.completionFence  = VK_NULL_HANDLE;
    pSurface->writePending        = FALSE;
    pSurface->readPending         = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateSurface(Gfx_Surface_Storage_t* storage, const Gfx_Surface_CreateInfo_t* pCreateInfo)
{
    Gfx_Surface_t* pSurface;

    LOGASSERT(NULL != storage);
    pSurface = &storage->surface;

    ValidateCreateInfo(pCreateInfo);

    switch (pCreateInfo->kind)
    {
    case GFX_SURFACE_KIND_PRESENT:
        CreatePresentSurface(storage, pCreateInfo);
        break;

    case GFX_SURFACE_KIND_STORAGE:
        CreateStorageSurface(pSurface, pCreateInfo);
        break;

    default:
        LOGFATAL("Unsupported surface kind");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void AcquireSurface(Gfx_Surface_t* pSurface)
{
    LOGASSERT(GFX_SURFACE_KIND_STORAGE == pSurface->kind);
    if (pSurface->writePending)
    {
        CVK_CHECK(cvkWaitForFence(pSurface->vk.device, pSurface->vk.completionFence, VK_TRUE, UINT64_MAX));
        pSurface->target.writeImage->uploadPending = FALSE;
    }
    pSurface->writePending = FALSE;
    LOGASSERT(FALSE == pSurface->readPending);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ReleaseSurface(Gfx_Surface_t* pSurface)
{
    LOGASSERT(GFX_SURFACE_KIND_STORAGE == pSurface->kind);
    LOGASSERT(pSurface->writePending);

    pSurface->acquiredImage = pSurface->target.readImage;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Surface_Create(Gfx_Surface_t** ppSurface, const Gfx_Surface_CreateInfo_t* pCreateInfo)
{
    Gfx_Surface_Storage_t* storage;

    LOGASSERT(NULL != ppSurface);
    LOGASSERT(NULL != pCreateInfo);

    storage = (Gfx_Surface_Storage_t*)OS_CALLOC(sizeof(Gfx_Surface_Storage_t));
    CreateSurface(storage, pCreateInfo);
    (*ppSurface) = &storage->surface;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Surface_Acquire(Gfx_Surface_t* pSurface)
{
    LOGASSERT(NULL != pSurface);
    LOGASSERT(NULL != pSurface->acquiredImage);

    AcquireSurface(pSurface);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Surface_Release(Gfx_Surface_t* pSurface)
{
    LOGASSERT(NULL != pSurface);

    ReleaseSurface(pSurface);
}
