/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cvk/cvk.h"
#include "cvk/win32/cvk_win32.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_malloc.h"
#include "gfx/gfx_display_internal.h"
#include "gfx/gfx_image.h"
#include "gfx/gfx_image_platform.h"
#include "gfx/gfx_surface.h"
#include "gfx/gfx_surface_internal.h"
#include "utl/utl_defines.h"

#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define HRCHECK(expression)                                                                                            \
    (CheckHresult((expression),                                                                                        \
                  #expression,                                                                                         \
                  __FILE__,                                                                                            \
                  __LINE__)) ///< Makes a failed D3D12 operation fatal at its call site.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Identifies how the surface's image views share or transfer their pixel storage.
 */
typedef enum D3D12InteropKind_e
{
    D3D12_INTEROP_IMAGE,           ///< Vulkan imports the D3D12 texture directly.
    D3D12_INTEROP_TRANSFER_BUFFER, ///< Vulkan writes a shared buffer copied by D3D12.
    D3D12_INTEROP_HOST_BUFFER,     ///< Vulkan and D3D12 buffers are bridged by a CPU copy.
} D3D12InteropKind_t;

_Static_assert(sizeof(LUID) == VK_LUID_SIZE, "DXGI and Vulkan LUID sizes must match");

/*************************************************************************************************/
/**
 * \brief Shared native storage, transfer resources, and synchronization state owned by a surface.
 */
typedef struct D3D12SharedSurface_s
{
    D3D12InteropKind_t interopKind;   ///< Selected shared-image, transfer-buffer, or host-buffer path.
    ID3D12Device*      d3d12Device;   ///< D3D12 device on the matched adapter.
    ID3D12Heap*        d3d12Heap;     ///< Shared heap backing a directly imported placed texture.
    ID3D12Resource*    d3d12Resource; ///< Texture backing the surface's native image storage.
    ID3D12Resource*
        d3d12TransferBuffer; ///< Linear copy source; shared with Vulkan or filled by the host-copy fallback.
    ID3D12CommandQueue*        d3d12CopyQueue;     ///< Queue copying the linear buffer into the texture.
    ID3D12CommandAllocator*    d3d12CopyAllocator; ///< Reusable allocator for d3d12CopyList.
    ID3D12GraphicsCommandList* d3d12CopyList;      ///< Re-recorded buffer-to-texture copy commands.
    ID3D12Fence* d3d12Fence; ///< Fence shared by Vulkan writes and D3D12 copies, or used only for host-copy completion.
    HANDLE       sharedHandle;               ///< NT handle used by VA to import a committed texture.
    HANDLE       sharedImageHandle;          ///< D3D12 heap or resource handle imported as a Vulkan image.
    HANDLE       sharedTransferBufferHandle; ///< NT handle exporting d3d12TransferBuffer to Vulkan.
    HANDLE       sharedFenceHandle;          ///< NT handle exporting d3d12Fence to Vulkan.
    VkExternalMemoryHandleTypeFlagBits sharedImageHandleType; ///< Handle type matching sharedImageHandle.
    VkDeviceSize sharedImageAllocationSize;                   ///< Complete shared heap size; zero for resource handles.
    uint64_t     copyFenceValue;                              ///< Pending native copy completion, or zero.
    HANDLE       fenceEvent;                                  ///< Auto-reset event used for synchronous fence waits.
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT copyFootprints[2];     ///< Linear luma and chroma copy layouts.
    UINT64                             transferBufferSize;    ///< Required transfer-buffer allocation size in bytes.
    void* d3d12TransferBytes; ///< Persistently mapped D3D12 upload memory used by the host-copy fallback.
    void* vkTransferBytes;    ///< Persistently mapped Vulkan memory used by the host-copy fallback.
} D3D12SharedSurface_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Default GPU-local D3D12 heap properties.
 */
static const D3D12_HEAP_PROPERTIES c_d3d12DefaultHeapProps = {
    D3D12_HEAP_TYPE_DEFAULT,
    D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    D3D12_MEMORY_POOL_UNKNOWN,
    1,
    1,
};

/*************************************************************************************************/
/**
 * \brief D3D12 copy-command queue configuration used by surface transfer paths.
 */
static const D3D12_COMMAND_QUEUE_DESC c_d3d12CopyQueueDesc = {
    D3D12_COMMAND_LIST_TYPE_COPY,
    D3D12_COMMAND_QUEUE_PRIORITY_NORMAL,
    D3D12_COMMAND_QUEUE_FLAG_NONE,
    0,
};

/*************************************************************************************************/
/**
 * \brief CPU-visible D3D12 upload-heap properties.
 */
static const D3D12_HEAP_PROPERTIES c_d3d12UploadHeapProps = {
    D3D12_HEAP_TYPE_UPLOAD,
    D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
    D3D12_MEMORY_POOL_UNKNOWN,
    1,
    1,
};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Converts a failed D3D12 HRESULT into a source-located fatal diagnostic.
 *
 * \param[in] hr                 Result to test; successful values return without side effects
 * \param[in] expression        Text of the failed expression
 * \param[in] file              Source file containing the check
 * \param[in] line               One-based source line containing the check
 *
 * \retval    None   No result value.
 */
static void CheckHresult(HRESULT hr, const char* expression, const char* file, int line);

/*************************************************************************************************/
/**
 * \brief Writes one D3D12 debug-layer message to standard error.
 *
 * \param[in] category            Debug-layer message category
 * \param[in] severity            Debug-layer message severity
 * \param[in] id                  Debug-layer message identifier
 * \param[in] description        Null-terminated diagnostic text
 * \param[in] context            Unused callback context
 *
 * \retval None   No result value.
 */
static void __stdcall MessageCallbackD3D12(D3D12_MESSAGE_CATEGORY category,
                                           D3D12_MESSAGE_SEVERITY severity,
                                           D3D12_MESSAGE_ID       id,
                                           const char*            description,
                                           void*                  context);

/*************************************************************************************************/
/**
 * \brief Converts a D3D12 diagnostic severity to a stable lowercase label.
 *
 * \param[in] severity        D3D12 severity enumerant
 *
 * \return Static severity name; unsupported enumerants terminate through LOGFATAL().
 */
static const char* D3D12MessageSeverityNameToString(D3D12_MESSAGE_SEVERITY severity);

/*************************************************************************************************/
/**
 * \brief Maps a surface pixel format to its D3D12 resource format.
 *
 * \param[in] pixelFormat        Supported surface pixel format
 *
 * \return Corresponding DXGI format; unsupported formats terminate through LOGFATAL().
 */
static DXGI_FORMAT GetDxgiFormat(Gfx_Surface_PixelFormat_t pixelFormat);

/*************************************************************************************************/
/**
 * \brief Maps a surface pixel format to its Vulkan image format.
 *
 * \param[in] pixelFormat        Supported surface pixel format
 *
 * \return Corresponding VkFormat; unsupported formats terminate through LOGFATAL().
 */
static VkFormat GetVulkanFormat(Gfx_Surface_PixelFormat_t pixelFormat);

/*************************************************************************************************/
/**
 * \brief Maps a surface pixel format to its VA render-target format.
 *
 * \param[in] pixelFormat        Supported surface pixel format
 *
 * \return VA_RT_FORMAT_YUV420 for YUV420P/NV12 or VA_RT_FORMAT_RGB32 for RGBA8/BGRA8.
 */
static uint32_t GetVaapiFormat(Gfx_Surface_PixelFormat_t pixelFormat);

/*************************************************************************************************/
/**
 * \brief Maps a surface pixel format to its VA four-character code.
 *
 * \param[in] pixelFormat        Supported surface pixel format
 *
 * \return VA_FOURCC_I420, VA_FOURCC_NV12, VA_FOURCC_RGBA, or VA_FOURCC_BGRA.
 */
static uint32_t GetVaapiPixelFormat(Gfx_Surface_PixelFormat_t pixelFormat);

/*************************************************************************************************/
/**
 * \brief Creates a D3D12 two-dimensional texture resource description.
 *
 * \param[in] dxgiFormat       Texture format
 * \param[in] width            Texture width in pixels
 * \param[in] height           Texture height in pixels
 * \param[in] flags            Texture resource flags
 *
 * \return Initialized D3D12 texture resource description.
 */
static D3D12_RESOURCE_DESC GetTextureDescD3D12(DXGI_FORMAT          dxgiFormat,
                                               uint32_t             width,
                                               uint32_t             height,
                                               D3D12_RESOURCE_FLAGS flags);

/*************************************************************************************************/
/**
 * \brief Creates a D3D12 buffer resource description.
 *
 * \param[in] size       Buffer size in bytes
 *
 * \return Initialized D3D12 buffer resource description.
 */
static D3D12_RESOURCE_DESC GetTransferBufferDescD3D12(uint64_t size);

/*************************************************************************************************/
/**
 * \brief Gets the aligned D3D12 heap size required for a resource.
 *
 * \param[in]  d3d12Device       Device queried for the resource allocation requirements
 * \param[in]  resourceDesc      Resource description used for the allocation query
 * \param[in]  minimumSize       Vulkan image memory requirement to include in the heap
 * \param[out] heapAlignment     Receives the required heap alignment
 *
 * \return Maximum of the resource requirement and minimumSize, rounded up to the D3D12 heap alignment.
 */
static uint64_t GetAlignedHeapSizeD3D12(ID3D12Device*              d3d12Device,
                                        const D3D12_RESOURCE_DESC* resourceDesc,
                                        uint64_t                   minimumSize,
                                        uint64_t*                  heapAlignment);

/*************************************************************************************************/
/**
 * \brief Selects direct image import, transfer-buffer copy, or host-copy fallback.
 *
 * \param[in]  physicalDevice                  Physical device queried for external-memory support
 * \param[in]  format                          Vulkan image format
 * \param[in]  usage                           Required Vulkan image usage
 * \param[out] interopKind                    Receives the selected interop path
 * \param[out] externalMemoryHandleType       Receives heap or resource type for direct import
 *
 * \retval    None   No result value.
 */
static void SelectVulkanSurfaceInteropD3D12(VkPhysicalDevice                    physicalDevice,
                                            VkFormat                            format,
                                            VkImageUsageFlags                   usage,
                                            D3D12InteropKind_t*                 interopKind,
                                            VkExternalMemoryHandleTypeFlagBits* externalMemoryHandleType);

/*************************************************************************************************/
/**
 * \brief Creates all D3D12 resources required by a shared storage surface.
 *
 * \param[in]  dxgiFormat                    Shared texture format
 * \param[in]  vkDeviceLuid                  Vulkan device LUID used for direct DXGI lookup
 * \param[in]  width                         Surface width in pixels
 * \param[in]  height                        Surface height in pixels
 * \param[in]  physicalDevice                Vulkan physical device
 * \param[in]  device                        Vulkan device queried for image memory requirements
 * \param[in]  vkFormat                      Vulkan image format
 * \param[out] d3d12Device                 Receives the D3D12 device
 * \param[out] d3d12Heap                   Receives the optional shared heap
 * \param[out] d3d12Resource               Receives the read-image texture
 * \param[out] d3d12TransferBuffer         Receives the optional transfer buffer
 * \param[out] d3d12CopyQueue              Receives the optional copy queue
 * \param[out] d3d12CopyAllocator          Receives the optional copy allocator
 * \param[out] d3d12CopyList               Receives the optional copy command list
 * \param[out] d3d12Fence                  Receives the optional copy fence
 * \param[out] sharedHandle                 Receives the optional VA texture handle
 * \param[out] sharedImageHandle            Receives the optional Vulkan image handle
 * \param[out] sharedTransferBufferHandle   Receives the optional Vulkan transfer-buffer handle
 * \param[out] sharedFenceHandle            Receives the optional Vulkan fence handle
 * \param[out] sharedImageHandleType        Receives the selected Vulkan image handle type
 * \param[out] sharedImageAllocationSize    Receives the shared-image allocation size
 * \param[out] fenceEvent                   Receives the optional copy-completion event
 * \param[out] copyFootprints               Receives the luma and chroma transfer layouts
 * \param[out] transferBufferSize           Receives the transfer-buffer size
 * \param[out] d3d12TransferBytes          Receives the optional mapped D3D12 transfer buffer
 * \param[out] interopKind                  Receives the selected interop path
 *
 * \retval    None   No result value.
 */
static void CreateSharedSurfaceResourcesD3D12(DXGI_FORMAT                         dxgiFormat,
                                              LUID                                vkDeviceLuid,
                                              uint32_t                            width,
                                              uint32_t                            height,
                                              VkPhysicalDevice                    physicalDevice,
                                              VkDevice                            device,
                                              VkFormat                            vkFormat,
                                              ID3D12Device**                      d3d12Device,
                                              ID3D12Heap**                        d3d12Heap,
                                              ID3D12Resource**                    d3d12Resource,
                                              ID3D12Resource**                    d3d12TransferBuffer,
                                              ID3D12CommandQueue**                d3d12CopyQueue,
                                              ID3D12CommandAllocator**            d3d12CopyAllocator,
                                              ID3D12GraphicsCommandList**         d3d12CopyList,
                                              ID3D12Fence**                       d3d12Fence,
                                              HANDLE*                             sharedHandle,
                                              HANDLE*                             sharedImageHandle,
                                              HANDLE*                             sharedTransferBufferHandle,
                                              HANDLE*                             sharedFenceHandle,
                                              VkExternalMemoryHandleTypeFlagBits* sharedImageHandleType,
                                              VkDeviceSize*                       sharedImageAllocationSize,
                                              HANDLE*                             fenceEvent,
                                              D3D12_PLACED_SUBRESOURCE_FOOTPRINT  copyFootprints[2],
                                              UINT64*                             transferBufferSize,
                                              void**                              d3d12TransferBytes,
                                              D3D12InteropKind_t*                 interopKind);

/*************************************************************************************************/
/**
 * \brief Queues a copy from the D3D12 transfer buffer into the read image.
 *
 * \param[in] d3d12CopyQueue          Queue used to submit the copy
 * \param[in] d3d12CopyAllocator      Allocator used to record the copy
 * \param[in] d3d12CopyList           Command list used to record the copy
 * \param[in] d3d12TransferBuffer     Source transfer buffer
 * \param[in] d3d12Resource           Destination image resource
 * \param[in] copyFootprints          Source layouts for the image planes
 * \param[in] copyFootprintCount       Number of source layouts
 * \param[in] d3d12Fence              Fence signaled after the queued copy
 * \param[in] completionFenceValue     Fence value signaled after the queued copy
 *
 * \retval    None   No result value.
 */
static void CopySurfaceTransferBufferToReadImageD3D12(ID3D12CommandQueue*                       d3d12CopyQueue,
                                                      ID3D12CommandAllocator*                   d3d12CopyAllocator,
                                                      ID3D12GraphicsCommandList*                d3d12CopyList,
                                                      ID3D12Resource*                           d3d12TransferBuffer,
                                                      ID3D12Resource*                           d3d12Resource,
                                                      const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* copyFootprints,
                                                      uint32_t                                  copyFootprintCount,
                                                      ID3D12Fence*                              d3d12Fence,
                                                      uint64_t                                  completionFenceValue);

/*************************************************************************************************/
/**
 * \brief Validates the presentation or storage creation requirements.
 *
 * \param[in] createInfo        Non-NULL surface configuration
 *
 * \retval    None   No result value.
 */
static void ValidateCreateInfo(const Gfx_Surface_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Creates and initializes a Win32 window presentation surface.
 *
 * \param[out] surface       Receives the native surface and window dimensions
 * \param[in]  createInfo    Valid windowed presentation configuration
 *
 * \retval    None   No result value.
 */
static void CreateWindowedPresentSurface(Gfx_Surface_t* surface, const Gfx_Surface_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Creates and initializes a headless presentation surface.
 *
 * \param[out] surface       Receives the native surface and requested dimensions
 * \param[in]  createInfo    Valid headless presentation configuration
 *
 * \retval    None   No result value.
 */
static void CreateHeadlessPresentSurface(Gfx_Surface_t* surface, const Gfx_Surface_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Creates a native presentation surface for the selected display kind.
 *
 * \param[out] surface           Receives common state and the windowed, direct, or headless VkSurfaceKHR
 * \param[in]  createInfo        Valid present-surface configuration
 *
 * \retval    None   No result value.
 */
static void CreatePresentSurface(Gfx_Surface_Storage_t* storage, const Gfx_Surface_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Creates shared storage with a VA read view and a Vulkan write view.
 *
 * \param[out] surface           Receives initialized VA state, D3D12 allocation, and backend image handles
 * \param[in]  createInfo        Storage format, dimensions, and matching VA/Vulkan device contexts
 *
 * \retval    None   No result value.
 */
static void CreateStorageSurface(Gfx_Surface_t* surface, const Gfx_Surface_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Creates a presentation or shared-storage surface.
 *
 * \param[out] surface           Zero-initialized surface storage to construct
 * \param[in]  createInfo        Presentation or storage creation parameters
 *
 * \retval    None   No result value.
 */
static void CreateSurface(Gfx_Surface_Storage_t* storage, const Gfx_Surface_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Completes pending writes and native copies before storage reuse.
 *
 * \param[in,out] surface        Storage surface with no active reader
 *
 * \retval    None   No result value.
 */
static void AcquireSurface(Gfx_Surface_t* surface);

/*************************************************************************************************/
/**
 * \brief Publishes the read view and queues any shared-buffer copy.
 *
 * \param[in,out] surface        Storage surface with submitted writes and writePending set
 *
 * \retval    None   No result value.
 */
static void ReleaseSurface(Gfx_Surface_t* surface);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CheckHresult(HRESULT hr, const char* expression, const char* file, int line)
{
    if (FAILED(hr))
    {
        char  fatal_message[768];
        char  system_message[256];
        DWORD length;

        system_message[0] = '\0';
        length            = FormatMessageA(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                           NULL,
                                           (DWORD)hr,
                                           MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                           system_message,
                                           (DWORD)sizeof(system_message),
                                           NULL);
        while (0 != length && ('\r' == system_message[length - 1] || '\n' == system_message[length - 1]))
        {
            system_message[--length] = '\0';
        }
        if (0 == length)
        {
            (void)strcpy_s(system_message, sizeof(system_message), "no system description available");
        }
        LOGSTARTBUFFER(fatal_message, sizeof(fatal_message));
        LOGPRINTS(expression);
        LOGPRINTS(" failed with HRESULT 0x");
        LOGPRINTX((uint64_t)(uint32_t)hr, 8, TRUE);
        LOGPRINTS(": ");
        LOGPRINTS(system_message);
        LOGEND();
        Os_LogFatal(fatal_message, file, line);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void __stdcall MessageCallbackD3D12(D3D12_MESSAGE_CATEGORY category,
                                           D3D12_MESSAGE_SEVERITY severity,
                                           D3D12_MESSAGE_ID       id,
                                           const char*            description,
                                           void*                  context)
{
    (void)context;
    LOGSTART(stderr);
    LOGPRINTS("D3D12 ");
    LOGPRINTS(D3D12MessageSeverityNameToString(severity));
    LOGPRINTS(" [category ");
    LOGPRINTI((int64_t)((int)category));
    LOGPRINTS(", id ");
    LOGPRINTI((int64_t)((int)id));
    LOGPRINTS("]: ");
    LOGPRINTS(description);
    LOGPRINTS("\n");
    LOGEND();
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static const char* D3D12MessageSeverityNameToString(D3D12_MESSAGE_SEVERITY severity)
{
    switch (severity)
    {
    case D3D12_MESSAGE_SEVERITY_CORRUPTION:
        return "corruption";
    case D3D12_MESSAGE_SEVERITY_ERROR:
        return "error";
    case D3D12_MESSAGE_SEVERITY_WARNING:
        return "warning";
    case D3D12_MESSAGE_SEVERITY_INFO:
        return "info";
    case D3D12_MESSAGE_SEVERITY_MESSAGE:
        return "message";
    default:
        LOGFATAL("Unsupported D3D12 message severity");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static DXGI_FORMAT GetDxgiFormat(Gfx_Surface_PixelFormat_t pixelFormat)
{
    switch (pixelFormat)
    {
    case GFX_SURFACE_PIXELFORMAT_YUV420P:
        return DXGI_FORMAT_420_OPAQUE;
    case GFX_SURFACE_PIXELFORMAT_NV12:
        return DXGI_FORMAT_NV12;
    case GFX_SURFACE_PIXELFORMAT_BGRA8:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case GFX_SURFACE_PIXELFORMAT_RGBA8:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
        LOGFATAL("Unsupported D3D12 surface pixel format");
    }
}

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
static D3D12_RESOURCE_DESC GetTextureDescD3D12(DXGI_FORMAT          dxgiFormat,
                                               uint32_t             width,
                                               uint32_t             height,
                                               D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC texture_desc;

    texture_desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Alignment          = 0;
    texture_desc.Width              = width;
    texture_desc.Height             = height;
    texture_desc.DepthOrArraySize   = 1;
    texture_desc.MipLevels          = 1;
    texture_desc.Format             = dxgiFormat;
    texture_desc.SampleDesc.Count   = 1;
    texture_desc.SampleDesc.Quality = 0;
    texture_desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    texture_desc.Flags              = flags;

    return texture_desc;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static D3D12_RESOURCE_DESC GetTransferBufferDescD3D12(uint64_t size)
{
    D3D12_RESOURCE_DESC buffer_desc;

    buffer_desc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer_desc.Alignment          = 0;
    buffer_desc.Width              = size;
    buffer_desc.Height             = 1;
    buffer_desc.DepthOrArraySize   = 1;
    buffer_desc.MipLevels          = 1;
    buffer_desc.Format             = DXGI_FORMAT_UNKNOWN;
    buffer_desc.SampleDesc.Count   = 1;
    buffer_desc.SampleDesc.Quality = 0;
    buffer_desc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    buffer_desc.Flags              = D3D12_RESOURCE_FLAG_NONE;

    return buffer_desc;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint64_t GetAlignedHeapSizeD3D12(ID3D12Device*              d3d12Device,
                                        const D3D12_RESOURCE_DESC* resourceDesc,
                                        uint64_t                   minimumSize,
                                        uint64_t*                  heapAlignment)
{
    D3D12_RESOURCE_ALLOCATION_INFO allocation_info;
    uint64_t                       aligned_heap_size;
    bool_t                         heap_size_aligned;
    uint64_t                       remainder;

    LOGASSERT(NULL != d3d12Device);
    LOGASSERT(NULL != resourceDesc);
    LOGASSERT(NULL != heapAlignment);

    allocation_info.SizeInBytes = 0;
    allocation_info.Alignment   = 0;
    (void)ID3D12Device_GetResourceAllocationInfo(d3d12Device, &allocation_info, 0, 1, resourceDesc);
    LOGASSERT((0 != allocation_info.SizeInBytes) && (UINT64_MAX != allocation_info.SizeInBytes));
    if (allocation_info.SizeInBytes < minimumSize)
    {
        allocation_info.SizeInBytes = minimumSize;
    }

    aligned_heap_size = 0;
    heap_size_aligned = FALSE;
    if (0 != allocation_info.Alignment)
    {
        remainder = allocation_info.SizeInBytes % allocation_info.Alignment;
        if (0 == remainder)
        {
            aligned_heap_size = allocation_info.SizeInBytes;
            heap_size_aligned = TRUE;
        }
        else if (allocation_info.SizeInBytes <= UINT64_MAX - (allocation_info.Alignment - remainder))
        {
            aligned_heap_size = allocation_info.SizeInBytes + allocation_info.Alignment - remainder;
            heap_size_aligned = TRUE;
        }
    }
    LOGASSERT(heap_size_aligned);

    (*heapAlignment) = allocation_info.Alignment;

    return aligned_heap_size;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Preference order is non-dedicated heap image import, resource image import, shared GPU
 *  buffer, then a host-copy buffer. The heap path requires support without DEDICATED_ONLY.
 */
static void SelectVulkanSurfaceInteropD3D12(VkPhysicalDevice                    physicalDevice,
                                            VkFormat                            format,
                                            VkImageUsageFlags                   usage,
                                            D3D12InteropKind_t*                 interopKind,
                                            VkExternalMemoryHandleTypeFlagBits* externalMemoryHandleType)
{
    VkBool32 buffer_dedicated_only;
    bool_t   buffer_import_supported;
    VkBool32 heap_dedicated_only;
    bool_t   heap_import_supported;
    VkBool32 resource_dedicated_only;
    bool_t   resource_import_supported;

    LOGASSERT(NULL != interopKind);
    LOGASSERT(NULL != externalMemoryHandleType);

    heap_import_supported =
        (VK_TRUE == cvkGetPhysicalDeviceExternalImageImportSupport(physicalDevice,
                                                                   VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT,
                                                                   format,
                                                                   VK_IMAGE_TYPE_2D,
                                                                   VK_IMAGE_TILING_OPTIMAL,
                                                                   usage,
                                                                   (VkImageCreateFlags)0,
                                                                   NULL,
                                                                   &heap_dedicated_only));
    resource_import_supported =
        (VK_TRUE == cvkGetPhysicalDeviceExternalImageImportSupport(physicalDevice,
                                                                   VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
                                                                   format,
                                                                   VK_IMAGE_TYPE_2D,
                                                                   VK_IMAGE_TILING_OPTIMAL,
                                                                   usage,
                                                                   (VkImageCreateFlags)0,
                                                                   NULL,
                                                                   &resource_dedicated_only));
    buffer_import_supported = (VK_TRUE == cvkGetPhysicalDeviceExternalBufferImportSupport(
                                              physicalDevice,
                                              NULL,
                                              (VkBufferCreateFlags)0,
                                              (VkBufferUsageFlags)VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                              VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
                                              &buffer_dedicated_only));

    if (heap_import_supported && (!heap_dedicated_only))
    {
        (*interopKind)              = D3D12_INTEROP_IMAGE;
        (*externalMemoryHandleType) = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT;
    }
    else if (resource_import_supported)
    {
        (*interopKind)              = D3D12_INTEROP_IMAGE;
        (*externalMemoryHandleType) = VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT;
    }
    else if (buffer_import_supported)
    {
        (*interopKind)              = D3D12_INTEROP_TRANSFER_BUFFER;
        (*externalMemoryHandleType) = (VkExternalMemoryHandleTypeFlagBits)0;
    }
    else
    {
        (*interopKind)              = D3D12_INTEROP_HOST_BUFFER;
        (*externalMemoryHandleType) = (VkExternalMemoryHandleTypeFlagBits)0;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateSharedSurfaceResourcesD3D12(DXGI_FORMAT                         dxgiFormat,
                                              LUID                                vkDeviceLuid,
                                              uint32_t                            width,
                                              uint32_t                            height,
                                              VkPhysicalDevice                    physicalDevice,
                                              VkDevice                            device,
                                              VkFormat                            vkFormat,
                                              ID3D12Device**                      d3d12Device,
                                              ID3D12Heap**                        d3d12Heap,
                                              ID3D12Resource**                    d3d12Resource,
                                              ID3D12Resource**                    d3d12TransferBuffer,
                                              ID3D12CommandQueue**                d3d12CopyQueue,
                                              ID3D12CommandAllocator**            d3d12CopyAllocator,
                                              ID3D12GraphicsCommandList**         d3d12CopyList,
                                              ID3D12Fence**                       d3d12Fence,
                                              HANDLE*                             sharedHandle,
                                              HANDLE*                             sharedImageHandle,
                                              HANDLE*                             sharedTransferBufferHandle,
                                              HANDLE*                             sharedFenceHandle,
                                              VkExternalMemoryHandleTypeFlagBits* sharedImageHandleType,
                                              VkDeviceSize*                       sharedImageAllocationSize,
                                              HANDLE*                             fenceEvent,
                                              D3D12_PLACED_SUBRESOURCE_FOOTPRINT  copyFootprints[2],
                                              UINT64*                             transferBufferSize,
                                              void**                              d3d12TransferBytes,
                                              D3D12InteropKind_t*                 interopKind)
{
    IDXGIFactory6*                    factory;
    IDXGIAdapter1*                    adapter;
    ID3D12InfoQueue1*                 info_queue;
    DWORD                             callback_cookie;
    VkImageUsageFlags                 image_usage;
    D3D12_FEATURE_DATA_FORMAT_SUPPORT format_support;
    D3D12_RESOURCE_DESC               texture_desc;
    UINT                              row_counts[2];
    UINT64                            row_sizes[2];
    D3D12_RESOURCE_DESC               transfer_buffer_desc;

    factory = NULL;
    HRCHECK(CreateDXGIFactory2(0, &IID_IDXGIFactory6, (void**)&factory));
    LOGASSERT(NULL != factory);

    adapter = NULL;
    HRCHECK(IDXGIFactory6_EnumAdapterByLuid(factory, vkDeviceLuid, &IID_IDXGIAdapter1, (void**)&adapter));
    LOGASSERT(NULL != adapter);
    IDXGIFactory6_Release(factory);

    (*d3d12Device) = NULL;
    HRCHECK(D3D12CreateDevice((IUnknown*)adapter, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)d3d12Device));
    LOGASSERT(NULL != (*d3d12Device));
    IDXGIAdapter1_Release(adapter);

    info_queue = NULL;
    if (SUCCEEDED(ID3D12Device_QueryInterface((*d3d12Device), &IID_ID3D12InfoQueue1, (void**)&info_queue)))
    {
        LOGASSERT(NULL != info_queue);
        HRCHECK(ID3D12InfoQueue1_RegisterMessageCallback(info_queue,
                                                         MessageCallbackD3D12,
                                                         D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS,
                                                         NULL,
                                                         &callback_cookie));
        ID3D12InfoQueue1_Release(info_queue);
    }

    format_support.Format   = dxgiFormat;
    format_support.Support1 = D3D12_FORMAT_SUPPORT1_NONE;
    format_support.Support2 = D3D12_FORMAT_SUPPORT2_NONE;
    HRCHECK(ID3D12Device_CheckFeatureSupport((*d3d12Device),
                                             D3D12_FEATURE_FORMAT_SUPPORT,
                                             &format_support,
                                             sizeof(format_support)));
    LOGASSERT(format_support.Support1 & D3D12_FORMAT_SUPPORT1_TEXTURE2D);

    image_usage = (VkImageUsageFlags)VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    SelectVulkanSurfaceInteropD3D12(physicalDevice, vkFormat, image_usage, interopKind, sharedImageHandleType);

    (*d3d12Heap)                  = NULL;
    (*d3d12Resource)              = NULL;
    (*d3d12TransferBuffer)        = NULL;
    (*d3d12CopyQueue)             = NULL;
    (*d3d12CopyAllocator)         = NULL;
    (*d3d12CopyList)              = NULL;
    (*d3d12Fence)                 = NULL;
    (*sharedHandle)               = NULL;
    (*sharedImageHandle)          = NULL;
    (*sharedTransferBufferHandle) = NULL;
    (*sharedFenceHandle)          = NULL;
    (*fenceEvent)                 = NULL;
    (*d3d12TransferBytes)         = NULL;

    switch ((*interopKind))
    {
    case D3D12_INTEROP_IMAGE:
    {
        texture_desc = GetTextureDescD3D12(dxgiFormat, width, height, D3D12_RESOURCE_FLAG_NONE);

        if (VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT == (*sharedImageHandleType))
        {
            VkExtent3D                      extent;
            uint64_t                        size_in_bytes;
            uint64_t                        heap_alignment;
            D3D12_HEAP_DESC                 shared_heap_desc;
            VkExternalMemoryImageCreateInfo external_info;
            VkImageCreateInfo               image_info;
            VkMemoryRequirements2           requirements;

            extent.width  = width;
            extent.height = height;
            extent.depth  = 1;
            cvkInitExternalMemoryImageCreateInfo(
                &external_info,
                NULL,
                (VkExternalMemoryHandleTypeFlags)VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT);
            cvkInitImageCreateInfo(&image_info,
                                   &external_info,
                                   (VkImageCreateFlags)0,
                                   VK_IMAGE_TYPE_2D,
                                   vkFormat,
                                   extent,
                                   1,
                                   1,
                                   VK_SAMPLE_COUNT_1_BIT,
                                   VK_IMAGE_TILING_OPTIMAL,
                                   image_usage,
                                   VK_SHARING_MODE_EXCLUSIVE,
                                   0,
                                   NULL,
                                   VK_IMAGE_LAYOUT_UNDEFINED);
            cvkGetDeviceImageMemoryRequirements(device, NULL, &image_info, VK_IMAGE_ASPECT_NONE, NULL, &requirements);
            size_in_bytes = GetAlignedHeapSizeD3D12((*d3d12Device),
                                                    &texture_desc,
                                                    requirements.memoryRequirements.size,
                                                    &heap_alignment);

            shared_heap_desc.SizeInBytes = size_in_bytes;
            shared_heap_desc.Properties  = c_d3d12DefaultHeapProps;
            shared_heap_desc.Alignment   = heap_alignment;
            shared_heap_desc.Flags       = D3D12_HEAP_FLAG_SHARED | D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;

            HRCHECK(ID3D12Device_CreateHeap((*d3d12Device), &shared_heap_desc, &IID_ID3D12Heap, (void**)d3d12Heap));
            LOGASSERT(NULL != (*d3d12Heap));
            HRCHECK(ID3D12Device_CreatePlacedResource((*d3d12Device),
                                                      (*d3d12Heap),
                                                      0,
                                                      &texture_desc,
                                                      D3D12_RESOURCE_STATE_COMMON,
                                                      NULL,
                                                      &IID_ID3D12Resource,
                                                      (void**)d3d12Resource));
            LOGASSERT(NULL != (*d3d12Resource));
            HRCHECK(ID3D12Device_CreateSharedHandle((*d3d12Device),
                                                    (ID3D12DeviceChild*)(*d3d12Heap),
                                                    NULL,
                                                    GENERIC_ALL,
                                                    NULL,
                                                    sharedImageHandle));
            LOGASSERT(NULL != (*sharedImageHandle));

            (*sharedImageAllocationSize) = size_in_bytes;

            LOGSTART(stdout);
            LOGPRINTS("Vulkan -> VA transfer: zero-copy shared D3D12 heap image.\n");
            LOGEND();
        }
        else
        {
            (*sharedImageAllocationSize) = 0;

            HRCHECK(ID3D12Device_CreateCommittedResource((*d3d12Device),
                                                         &c_d3d12DefaultHeapProps,
                                                         D3D12_HEAP_FLAG_SHARED,
                                                         &texture_desc,
                                                         D3D12_RESOURCE_STATE_COMMON,
                                                         NULL,
                                                         &IID_ID3D12Resource,
                                                         (void**)d3d12Resource));
            LOGASSERT(NULL != (*d3d12Resource));
            HRCHECK(ID3D12Device_CreateSharedHandle((*d3d12Device),
                                                    (ID3D12DeviceChild*)(*d3d12Resource),
                                                    NULL,
                                                    GENERIC_ALL,
                                                    NULL,
                                                    sharedHandle));
            LOGASSERT(NULL != (*sharedHandle));
            HRCHECK(ID3D12Device_CreateSharedHandle((*d3d12Device),
                                                    (ID3D12DeviceChild*)(*d3d12Resource),
                                                    NULL,
                                                    GENERIC_ALL,
                                                    NULL,
                                                    sharedImageHandle));
            LOGASSERT(NULL != (*sharedImageHandle));

            LOGSTART(stdout);
            LOGPRINTS("Vulkan -> VA transfer: zero-copy shared D3D12 resource image.\n");
            LOGEND();
        }
        break;
    }
    case D3D12_INTEROP_TRANSFER_BUFFER:
    {
        texture_desc = GetTextureDescD3D12(dxgiFormat,
                                           width,
                                           height,
                                           D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                                               D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);

        ID3D12Device_GetCopyableFootprints((*d3d12Device),
                                           &texture_desc,
                                           0,
                                           (UINT)COUNT_OF(row_counts),
                                           0,
                                           copyFootprints,
                                           row_counts,
                                           row_sizes,
                                           transferBufferSize);

        HRCHECK(ID3D12Device_CreateCommittedResource((*d3d12Device),
                                                     &c_d3d12DefaultHeapProps,
                                                     D3D12_HEAP_FLAG_SHARED,
                                                     &texture_desc,
                                                     D3D12_RESOURCE_STATE_COMMON,
                                                     NULL,
                                                     &IID_ID3D12Resource,
                                                     (void**)d3d12Resource));
        LOGASSERT(NULL != (*d3d12Resource));

        transfer_buffer_desc = GetTransferBufferDescD3D12((*transferBufferSize));

        HRCHECK(ID3D12Device_CreateCommittedResource((*d3d12Device),
                                                     &c_d3d12DefaultHeapProps,
                                                     D3D12_HEAP_FLAG_SHARED,
                                                     &transfer_buffer_desc,
                                                     D3D12_RESOURCE_STATE_COMMON,
                                                     NULL,
                                                     &IID_ID3D12Resource,
                                                     (void**)d3d12TransferBuffer));
        LOGASSERT(NULL != (*d3d12TransferBuffer));

        HRCHECK(ID3D12Device_CreateCommandQueue((*d3d12Device),
                                                &c_d3d12CopyQueueDesc,
                                                &IID_ID3D12CommandQueue,
                                                (void**)d3d12CopyQueue));
        LOGASSERT(NULL != (*d3d12CopyQueue));
        HRCHECK(ID3D12Device_CreateCommandAllocator((*d3d12Device),
                                                    D3D12_COMMAND_LIST_TYPE_COPY,
                                                    &IID_ID3D12CommandAllocator,
                                                    (void**)d3d12CopyAllocator));
        LOGASSERT(NULL != (*d3d12CopyAllocator));
        HRCHECK(ID3D12Device_CreateCommandList((*d3d12Device),
                                               0,
                                               D3D12_COMMAND_LIST_TYPE_COPY,
                                               (*d3d12CopyAllocator),
                                               NULL,
                                               &IID_ID3D12GraphicsCommandList,
                                               (void**)d3d12CopyList));
        LOGASSERT(NULL != (*d3d12CopyList));

        HRCHECK(ID3D12GraphicsCommandList_Close((*d3d12CopyList)));

        HRCHECK(
            ID3D12Device_CreateFence((*d3d12Device), 0, D3D12_FENCE_FLAG_SHARED, &IID_ID3D12Fence, (void**)d3d12Fence));
        LOGASSERT(NULL != (*d3d12Fence));
        HRCHECK(ID3D12Device_CreateSharedHandle((*d3d12Device),
                                                (ID3D12DeviceChild*)(*d3d12Resource),
                                                NULL,
                                                GENERIC_ALL,
                                                NULL,
                                                sharedHandle));
        LOGASSERT(NULL != (*sharedHandle));
        HRCHECK(ID3D12Device_CreateSharedHandle((*d3d12Device),
                                                (ID3D12DeviceChild*)(*d3d12TransferBuffer),
                                                NULL,
                                                GENERIC_ALL,
                                                NULL,
                                                sharedTransferBufferHandle));
        LOGASSERT(NULL != (*sharedTransferBufferHandle));
        HRCHECK(ID3D12Device_CreateSharedHandle((*d3d12Device),
                                                (ID3D12DeviceChild*)(*d3d12Fence),
                                                NULL,
                                                GENERIC_ALL,
                                                NULL,
                                                sharedFenceHandle));
        LOGASSERT(NULL != (*sharedFenceHandle));

        (*fenceEvent) = CreateEventA(NULL, FALSE, FALSE, NULL);
        LOGASSERT(NULL != (*fenceEvent));

        LOGSTART(stdout);
        LOGPRINTS("Vulkan -> D3D12 transfer: shared GPU transfer buffer; one D3D12 buffer-to-texture copy remains.\n");
        LOGEND();
        break;
    }
    case D3D12_INTEROP_HOST_BUFFER:
    {
        texture_desc = GetTextureDescD3D12(dxgiFormat,
                                           width,
                                           height,
                                           D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                                               D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS);

        ID3D12Device_GetCopyableFootprints((*d3d12Device),
                                           &texture_desc,
                                           0,
                                           (UINT)COUNT_OF(row_counts),
                                           0,
                                           copyFootprints,
                                           row_counts,
                                           row_sizes,
                                           transferBufferSize);

        transfer_buffer_desc = GetTransferBufferDescD3D12((*transferBufferSize));

        HRCHECK(ID3D12Device_CreateCommittedResource((*d3d12Device),
                                                     &c_d3d12DefaultHeapProps,
                                                     D3D12_HEAP_FLAG_SHARED,
                                                     &texture_desc,
                                                     D3D12_RESOURCE_STATE_COMMON,
                                                     NULL,
                                                     &IID_ID3D12Resource,
                                                     (void**)d3d12Resource));
        LOGASSERT(NULL != (*d3d12Resource));
        HRCHECK(ID3D12Device_CreateCommittedResource((*d3d12Device),
                                                     &c_d3d12UploadHeapProps,
                                                     D3D12_HEAP_FLAG_NONE,
                                                     &transfer_buffer_desc,
                                                     D3D12_RESOURCE_STATE_GENERIC_READ,
                                                     NULL,
                                                     &IID_ID3D12Resource,
                                                     (void**)d3d12TransferBuffer));
        LOGASSERT(NULL != (*d3d12TransferBuffer));
        HRCHECK(ID3D12Device_CreateCommandQueue((*d3d12Device),
                                                &c_d3d12CopyQueueDesc,
                                                &IID_ID3D12CommandQueue,
                                                (void**)d3d12CopyQueue));
        LOGASSERT(NULL != (*d3d12CopyQueue));
        HRCHECK(ID3D12Device_CreateCommandAllocator((*d3d12Device),
                                                    D3D12_COMMAND_LIST_TYPE_COPY,
                                                    &IID_ID3D12CommandAllocator,
                                                    (void**)d3d12CopyAllocator));
        LOGASSERT(NULL != (*d3d12CopyAllocator));
        HRCHECK(ID3D12Device_CreateCommandList((*d3d12Device),
                                               0,
                                               D3D12_COMMAND_LIST_TYPE_COPY,
                                               (*d3d12CopyAllocator),
                                               NULL,
                                               &IID_ID3D12GraphicsCommandList,
                                               (void**)d3d12CopyList));
        LOGASSERT(NULL != (*d3d12CopyList));

        HRCHECK(ID3D12GraphicsCommandList_Close((*d3d12CopyList)));

        HRCHECK(
            ID3D12Device_CreateFence((*d3d12Device), 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)d3d12Fence));
        LOGASSERT(NULL != (*d3d12Fence));
        HRCHECK(ID3D12Device_CreateSharedHandle((*d3d12Device),
                                                (ID3D12DeviceChild*)(*d3d12Resource),
                                                NULL,
                                                GENERIC_ALL,
                                                NULL,
                                                sharedHandle));
        LOGASSERT(NULL != (*sharedHandle));
        HRCHECK(ID3D12Resource_Map((*d3d12TransferBuffer), 0, NULL, d3d12TransferBytes));
        LOGASSERT(NULL != (*d3d12TransferBytes));

        (*fenceEvent) = CreateEvent(NULL, FALSE, FALSE, NULL);
        LOGASSERT(NULL != (*fenceEvent));

        LOGSTART(stdout);
        LOGPRINTS("Vulkan -> D3D12 transfer: CPU host-copy fallback (external image and buffer import unsupported).\n");
        LOGEND();
        break;
    }
    default:
        LOGFATAL("Unsupported D3D12 surface interop kind");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CopySurfaceTransferBufferToReadImageD3D12(ID3D12CommandQueue*                       d3d12CopyQueue,
                                                      ID3D12CommandAllocator*                   d3d12CopyAllocator,
                                                      ID3D12GraphicsCommandList*                d3d12CopyList,
                                                      ID3D12Resource*                           d3d12TransferBuffer,
                                                      ID3D12Resource*                           d3d12Resource,
                                                      const D3D12_PLACED_SUBRESOURCE_FOOTPRINT* copyFootprints,
                                                      uint32_t                                  copyFootprintCount,
                                                      ID3D12Fence*                              d3d12Fence,
                                                      uint64_t                                  completionFenceValue)
{
    uint32_t                    plane_index;
    D3D12_TEXTURE_COPY_LOCATION src;
    D3D12_TEXTURE_COPY_LOCATION dst;

    HRCHECK(ID3D12CommandAllocator_Reset(d3d12CopyAllocator));
    HRCHECK(ID3D12GraphicsCommandList_Reset(d3d12CopyList, d3d12CopyAllocator, NULL));

    for (plane_index = 0; plane_index < copyFootprintCount; ++plane_index)
    {
        src.pResource        = d3d12TransferBuffer;
        src.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint  = copyFootprints[plane_index];
        dst.pResource        = d3d12Resource;
        dst.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.SubresourceIndex = plane_index;
        ID3D12GraphicsCommandList_CopyTextureRegion(d3d12CopyList, &dst, 0, 0, 0, &src, NULL);
    }

    HRCHECK(ID3D12GraphicsCommandList_Close(d3d12CopyList));
    ID3D12CommandQueue_ExecuteCommandLists(d3d12CopyQueue, 1, (ID3D12CommandList**)&d3d12CopyList);
    HRCHECK(ID3D12CommandQueue_Signal(d3d12CopyQueue, d3d12Fence, completionFenceValue));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ValidateCreateInfo(const Gfx_Surface_CreateInfo_t* createInfo)
{
    LOGASSERT(NULL != createInfo);
    LOGASSERT(VK_NULL_HANDLE != createInfo->instance);
    LOGASSERT(VK_NULL_HANDLE != createInfo->physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != createInfo->device);
    LOGASSERT(VK_NULL_HANDLE != createInfo->queue);
    LOGASSERT(VK_INVALID != createInfo->queueFamilyIndex);
    LOGASSERT(NULL == createInfo->pAllocator);
    LOGASSERT((GFX_SURFACE_KIND_PRESENT == createInfo->kind) || (GFX_SURFACE_KIND_STORAGE == createInfo->kind));

    if (GFX_SURFACE_KIND_PRESENT == createInfo->kind)
    {
        LOGASSERT(NULL != createInfo->pDisplay);
        LOGASSERT(0 != createInfo->pDisplay->width);
        LOGASSERT(0 != createInfo->pDisplay->height);
        LOGASSERT(0 == createInfo->width);
        LOGASSERT(0 == createInfo->height);
        return;
    }
    LOGASSERT((GFX_SURFACE_PIXELFORMAT_YUV420P == createInfo->pixelFormat) ||
              (GFX_SURFACE_PIXELFORMAT_NV12 == createInfo->pixelFormat) ||
              (GFX_SURFACE_PIXELFORMAT_RGBA8 == createInfo->pixelFormat) ||
              (GFX_SURFACE_PIXELFORMAT_BGRA8 == createInfo->pixelFormat));
    LOGASSERT((GFX_SURFACE_PIXELFORMAT_RGBA8 == createInfo->pixelFormat) ||
              (GFX_SURFACE_PIXELFORMAT_BGRA8 == createInfo->pixelFormat) ||
              ((0 == (createInfo->width & 1u)) && (0 == (createInfo->height & 1u))));
    LOGASSERT(NULL == createInfo->pDisplay);
    LOGASSERT((0 != createInfo->width) && (0 != createInfo->height));
    LOGASSERT(NULL != createInfo->dpy);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateWindowedPresentSurface(Gfx_Surface_t* surface, const Gfx_Surface_CreateInfo_t* createInfo)
{
    VkSurfaceKHR vk_surface;

    cvkCreateWin32Surface(createInfo->instance,
                          NULL,
                          (VkWin32SurfaceCreateFlagsKHR)0,
                          (HINSTANCE)createInfo->pDisplay->nativeDisplay,
                          (HWND)createInfo->pDisplay->nativeWindow,
                          createInfo->pAllocator,
                          &vk_surface);

    surface->kind                = createInfo->kind;
    surface->va.dpy              = NULL;
    surface->va.format           = 0;
    surface->va.pixelFormat      = 0;
    surface->vk.physicalDevice   = createInfo->physicalDevice;
    surface->vk.device           = createInfo->device;
    surface->vk.queue            = createInfo->queue;
    surface->vk.surface          = vk_surface;
    surface->vk.queueFamilyIndex = createInfo->queueFamilyIndex;
    surface->vk.format           = VK_FORMAT_UNDEFINED;
    surface->vk.pAllocator       = createInfo->pAllocator;
    surface->width               = createInfo->pDisplay->width;
    surface->height              = createInfo->pDisplay->height;
    surface->target.readImage    = NULL;
    surface->target.writeImage   = NULL;
    surface->acquiredImage       = NULL;
    surface->platform            = NULL;
    surface->vk.completionFence  = VK_NULL_HANDLE;
    surface->writePending        = FALSE;
    surface->readPending         = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateHeadlessPresentSurface(Gfx_Surface_t* surface, const Gfx_Surface_CreateInfo_t* createInfo)
{
    VkSurfaceKHR vk_surface;

    cvkCreateHeadlessSurface(createInfo->instance,
                             NULL,
                             (VkHeadlessSurfaceCreateFlagsEXT)0,
                             createInfo->pAllocator,
                             &vk_surface);

    surface->kind                = createInfo->kind;
    surface->va.dpy              = NULL;
    surface->va.format           = 0;
    surface->va.pixelFormat      = 0;
    surface->vk.physicalDevice   = createInfo->physicalDevice;
    surface->vk.device           = createInfo->device;
    surface->vk.queue            = createInfo->queue;
    surface->vk.surface          = vk_surface;
    surface->vk.queueFamilyIndex = createInfo->queueFamilyIndex;
    surface->vk.format           = VK_FORMAT_UNDEFINED;
    surface->vk.pAllocator       = createInfo->pAllocator;
    surface->width               = createInfo->pDisplay->width;
    surface->height              = createInfo->pDisplay->height;
    surface->target.readImage    = NULL;
    surface->target.writeImage   = NULL;
    surface->acquiredImage       = NULL;
    surface->platform            = NULL;
    surface->vk.completionFence  = VK_NULL_HANDLE;
    surface->writePending        = FALSE;
    surface->readPending         = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreatePresentSurface(Gfx_Surface_Storage_t* storage, const Gfx_Surface_CreateInfo_t* createInfo)
{
    Gfx_Surface_t* surface;

    LOGASSERT(NULL != storage);
    surface = &storage->surface;

    switch (createInfo->pDisplay->kind)
    {
    case GFX_DISPLAY_KIND_WINDOWED:
        CreateWindowedPresentSurface(surface, createInfo);
        break;

    case GFX_DISPLAY_KIND_DIRECT:
        Gfx_Surface_InitializeDirect(storage, createInfo);
        break;

    case GFX_DISPLAY_KIND_HEADLESS:
        CreateHeadlessPresentSurface(surface, createInfo);
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
static void CreateStorageSurface(Gfx_Surface_t* surface, const Gfx_Surface_CreateInfo_t* createInfo)
{
    D3D12SharedSurface_t*           shared_surface;
    Gfx_Image_CreateInfo_t          va_image_create_info;
    Gfx_Image_CreateInfo_t          vk_image_create_info;
    Gfx_Image_Platform_CreateInfo_t platform_create_info;
    VkPhysicalDeviceIDProperties    device_identity;
    LUID                            vk_device_luid;
    DXGI_FORMAT                     dxgi_format;
    uint32_t                        va_format;
    uint32_t                        va_pixel_format;
    VkFormat                        vk_format;
    Gfx_Image_t*                    read_image;
    Gfx_Image_t*                    write_image;

    cvkGetPhysicalDeviceIDProperties(createInfo->physicalDevice, NULL, &device_identity);
    LOGASSERT(VK_TRUE == device_identity.deviceLUIDValid);
    CRT_Memory_Copy(&vk_device_luid, device_identity.deviceLUID, VK_LUID_SIZE);

    shared_surface = (D3D12SharedSurface_t*)OS_CALLOC(sizeof(D3D12SharedSurface_t));

    dxgi_format     = GetDxgiFormat(createInfo->pixelFormat);
    va_format       = GetVaapiFormat(createInfo->pixelFormat);
    va_pixel_format = GetVaapiPixelFormat(createInfo->pixelFormat);
    vk_format       = GetVulkanFormat(createInfo->pixelFormat);

    CreateSharedSurfaceResourcesD3D12(dxgi_format,
                                      vk_device_luid,
                                      createInfo->width,
                                      createInfo->height,
                                      createInfo->physicalDevice,
                                      createInfo->device,
                                      vk_format,
                                      &shared_surface->d3d12Device,
                                      &shared_surface->d3d12Heap,
                                      &shared_surface->d3d12Resource,
                                      &shared_surface->d3d12TransferBuffer,
                                      &shared_surface->d3d12CopyQueue,
                                      &shared_surface->d3d12CopyAllocator,
                                      &shared_surface->d3d12CopyList,
                                      &shared_surface->d3d12Fence,
                                      &shared_surface->sharedHandle,
                                      &shared_surface->sharedImageHandle,
                                      &shared_surface->sharedTransferBufferHandle,
                                      &shared_surface->sharedFenceHandle,
                                      &shared_surface->sharedImageHandleType,
                                      &shared_surface->sharedImageAllocationSize,
                                      &shared_surface->fenceEvent,
                                      shared_surface->copyFootprints,
                                      &shared_surface->transferBufferSize,
                                      &shared_surface->d3d12TransferBytes,
                                      &shared_surface->interopKind);

    platform_create_info.sharedHandle = &shared_surface->sharedHandle;
    if (VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT == shared_surface->sharedImageHandleType)
    {
        platform_create_info.sharedImageResource = shared_surface->d3d12Resource;
    }
    else
    {
        platform_create_info.sharedImageResource = NULL;
    }
    platform_create_info.sharedImageHandle         = shared_surface->sharedImageHandle;
    platform_create_info.sharedImageHandleType     = shared_surface->sharedImageHandleType;
    platform_create_info.sharedImageAllocationSize = shared_surface->sharedImageAllocationSize;
    if (D3D12_INTEROP_HOST_BUFFER == shared_surface->interopKind)
    {
        platform_create_info.transferBufferHostCopy = TRUE;
    }
    else
    {
        platform_create_info.transferBufferHostCopy = FALSE;
    }
    platform_create_info.transferBufferSize         = shared_surface->transferBufferSize;
    platform_create_info.sharedTransferBufferHandle = shared_surface->sharedTransferBufferHandle;
    platform_create_info.sharedFenceHandle          = shared_surface->sharedFenceHandle;
    platform_create_info.yStride                    = shared_surface->copyFootprints[0].Footprint.RowPitch;
    platform_create_info.uvOffset                   = shared_surface->copyFootprints[1].Offset;
    platform_create_info.uvStride                   = shared_surface->copyFootprints[1].Footprint.RowPitch;
    platform_create_info.ppVulkanTransferBytes      = &shared_surface->vkTransferBytes;
    platform_create_info.drmPrimeDescriptor         = NULL;

    va_image_create_info.hint               = GFX_IMAGE_HINT_VAAPI;
    va_image_create_info.kind               = GFX_IMAGE_KIND_IMPORT;
    va_image_create_info.width              = createInfo->width;
    va_image_create_info.height             = createInfo->height;
    va_image_create_info.platformCreateInfo = &platform_create_info;
    va_image_create_info.va.dpy             = createInfo->dpy;
    va_image_create_info.va.format          = va_format;
    va_image_create_info.va.pixelFormat     = va_pixel_format;
    va_image_create_info.vk.physicalDevice  = createInfo->physicalDevice;
    va_image_create_info.vk.device          = createInfo->device;
    va_image_create_info.vk.queue           = VK_NULL_HANDLE;
    va_image_create_info.vk.pAllocator      = createInfo->pAllocator;
    va_image_create_info.vk.format          = vk_format;
    va_image_create_info.vk.usage           = (VkImageUsageFlags)VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    va_image_create_info.vk.commandPool     = VK_NULL_HANDLE;
    va_image_create_info.vk.commandBuffer   = VK_NULL_HANDLE;

    vk_image_create_info.hint               = GFX_IMAGE_HINT_VULKAN;
    vk_image_create_info.kind               = GFX_IMAGE_KIND_IMPORT;
    vk_image_create_info.width              = createInfo->width;
    vk_image_create_info.height             = createInfo->height;
    vk_image_create_info.platformCreateInfo = &platform_create_info;
    vk_image_create_info.va.dpy             = createInfo->dpy;
    vk_image_create_info.va.format          = va_format;
    vk_image_create_info.va.pixelFormat     = va_pixel_format;
    vk_image_create_info.vk.physicalDevice  = createInfo->physicalDevice;
    vk_image_create_info.vk.device          = createInfo->device;
    vk_image_create_info.vk.queue           = VK_NULL_HANDLE;
    vk_image_create_info.vk.pAllocator      = createInfo->pAllocator;
    vk_image_create_info.vk.format          = vk_format;
    vk_image_create_info.vk.usage           = (VkImageUsageFlags)VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    vk_image_create_info.vk.commandPool     = VK_NULL_HANDLE;
    vk_image_create_info.vk.commandBuffer   = VK_NULL_HANDLE;

    Gfx_Image_Create(&read_image, &va_image_create_info);
    Gfx_Image_Create(&write_image, &vk_image_create_info);

    surface->kind                = createInfo->kind;
    surface->pixelFormat         = createInfo->pixelFormat;
    surface->va.dpy              = createInfo->dpy;
    surface->va.format           = va_format;
    surface->va.pixelFormat      = va_pixel_format;
    surface->vk.physicalDevice   = createInfo->physicalDevice;
    surface->vk.device           = createInfo->device;
    surface->vk.queue            = createInfo->queue;
    surface->vk.surface          = VK_NULL_HANDLE;
    surface->vk.queueFamilyIndex = createInfo->queueFamilyIndex;
    surface->vk.format           = vk_format;
    surface->vk.pAllocator       = createInfo->pAllocator;
    surface->width               = createInfo->width;
    surface->height              = createInfo->height;
    surface->acquiredImage       = NULL;
    surface->platform            = shared_surface;
    surface->target.readImage    = read_image;
    surface->target.writeImage   = write_image;
    surface->vk.completionFence  = VK_NULL_HANDLE;
    surface->writePending        = FALSE;
    surface->readPending         = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateSurface(Gfx_Surface_Storage_t* storage, const Gfx_Surface_CreateInfo_t* createInfo)
{
    Gfx_Surface_t* surface;

    LOGASSERT(NULL != storage);
    surface = &storage->surface;

    ValidateCreateInfo(createInfo);

    switch (createInfo->kind)
    {
    case GFX_SURFACE_KIND_PRESENT:
        CreatePresentSurface(storage, createInfo);
        break;

    case GFX_SURFACE_KIND_STORAGE:
        CreateStorageSurface(surface, createInfo);
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
static void AcquireSurface(Gfx_Surface_t* surface)
{
    D3D12SharedSurface_t* shared_surface;

    LOGASSERT(GFX_SURFACE_KIND_STORAGE == surface->kind);

    shared_surface = (D3D12SharedSurface_t*)surface->platform;
    LOGASSERT(NULL != shared_surface);

    if (surface->writePending)
    {
        switch (shared_surface->interopKind)
        {
        case D3D12_INTEROP_IMAGE:
        {
            CVK_CHECK(cvkWaitForFence(surface->vk.device, surface->vk.completionFence, VK_TRUE, UINT64_MAX));

            surface->target.writeImage->uploadPending = FALSE;
            break;
        }
        case D3D12_INTEROP_HOST_BUFFER:
        {
            CVK_CHECK(cvkWaitForFence(surface->vk.device, surface->vk.completionFence, VK_TRUE, UINT64_MAX));

            LOGASSERT(NULL != shared_surface->vkTransferBytes);
            LOGASSERT(NULL != shared_surface->d3d12TransferBytes);

            (void)memcpy(shared_surface->d3d12TransferBytes,
                         shared_surface->vkTransferBytes,
                         (size_t)shared_surface->transferBufferSize);

            surface->target.writeImage->vk.externalSemaphoreValue += 1;

            CopySurfaceTransferBufferToReadImageD3D12(shared_surface->d3d12CopyQueue,
                                                      shared_surface->d3d12CopyAllocator,
                                                      shared_surface->d3d12CopyList,
                                                      shared_surface->d3d12TransferBuffer,
                                                      shared_surface->d3d12Resource,
                                                      shared_surface->copyFootprints,
                                                      (uint32_t)COUNT_OF(shared_surface->copyFootprints),
                                                      shared_surface->d3d12Fence,
                                                      surface->target.writeImage->vk.externalSemaphoreValue);
            shared_surface->copyFenceValue = surface->target.writeImage->vk.externalSemaphoreValue;

            surface->target.writeImage->uploadPending = FALSE;
            break;
        }
        case D3D12_INTEROP_TRANSFER_BUFFER:
        default:
            break;
        }
    }
    if (0 != shared_surface->copyFenceValue)
    {
        bool_t signaled;

        HRCHECK(ID3D12Fence_SetEventOnCompletion(shared_surface->d3d12Fence,
                                                 shared_surface->copyFenceValue,
                                                 shared_surface->fenceEvent));
        signaled = (WAIT_OBJECT_0 == WaitForSingleObject(shared_surface->fenceEvent, INFINITE));
        LOGASSERT(signaled);

        shared_surface->copyFenceValue = 0;
    }

    surface->writePending = FALSE;

    LOGASSERT(FALSE == surface->readPending);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ReleaseSurface(Gfx_Surface_t* surface)
{
    D3D12SharedSurface_t* shared_surface;

    shared_surface = (D3D12SharedSurface_t*)surface->platform;
    LOGASSERT(NULL != shared_surface);

    if (surface->target.writeImage->uploadPending)
    {
        switch (shared_surface->interopKind)
        {
        case D3D12_INTEROP_TRANSFER_BUFFER:
        {
            LOGASSERT(0 != surface->target.writeImage->vk.externalSemaphoreValue);

            HRCHECK(ID3D12CommandQueue_Wait(shared_surface->d3d12CopyQueue,
                                            shared_surface->d3d12Fence,
                                            surface->target.writeImage->vk.externalSemaphoreValue));

            surface->target.writeImage->vk.externalSemaphoreValue += 1;

            CopySurfaceTransferBufferToReadImageD3D12(shared_surface->d3d12CopyQueue,
                                                      shared_surface->d3d12CopyAllocator,
                                                      shared_surface->d3d12CopyList,
                                                      shared_surface->d3d12TransferBuffer,
                                                      shared_surface->d3d12Resource,
                                                      shared_surface->copyFootprints,
                                                      (uint32_t)COUNT_OF(shared_surface->copyFootprints),
                                                      shared_surface->d3d12Fence,
                                                      surface->target.writeImage->vk.externalSemaphoreValue);
            shared_surface->copyFenceValue = surface->target.writeImage->vk.externalSemaphoreValue;

            surface->target.writeImage->uploadPending = FALSE;
            break;
        }
        case D3D12_INTEROP_HOST_BUFFER:
        case D3D12_INTEROP_IMAGE:
        default:
            break;
        }
    }

    surface->acquiredImage = surface->target.readImage;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Surface_Create(Gfx_Surface_t** surface, const Gfx_Surface_CreateInfo_t* createInfo)
{
    Gfx_Surface_Storage_t* storage;

    LOGASSERT(NULL != surface);
    LOGASSERT(NULL != createInfo);

    storage = (Gfx_Surface_Storage_t*)OS_CALLOC(sizeof(Gfx_Surface_Storage_t));
    CreateSurface(storage, createInfo);
    (*surface) = &storage->surface;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Surface_Acquire(Gfx_Surface_t* surface)
{
    LOGASSERT(NULL != surface);
    LOGASSERT(NULL != surface->acquiredImage);

    AcquireSurface(surface);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Surface_Release(Gfx_Surface_t* surface)
{
    LOGASSERT(NULL != surface);

    ReleaseSurface(surface);
}
