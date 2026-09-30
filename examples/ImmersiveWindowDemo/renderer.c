/* The original demo's two video passes, expressed through cvk. D3D11 is only
** the Media Foundation / DirectComposition bridge. Shared textures stay on the
** same adapter; keyed mutexes and external queue ownership protect both APIs. */
#include "vk_diagnostic.h"
#include "os/os_types.h"
#include <vulkan/vk_platform.h>
#include "renderer.h"
#include "os/os_compiler.h"
#include "cdx11.h"
#include "cdxgi.h"
#include <d3dcompiler.h>
#include <volk.h>
#include "cvk/win32/cvk_win32.h"
#include "cvk/cvk.h"
#include "cvk/cvk_json.h"
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_win32.h>
#include <stdint.h>
#include <limits.h>
#include <d3d11.h>
#include <d3dcommon.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgiformat.h>
#include <dxgicommon.h>
#include <minwindef.h>
#include <windef.h>
#include <minwinbase.h>
#include <winnt.h>
#include <Unknwnbase.h>
#include <winerror.h>
#include <debugapi.h>
#include <WinUser.h>
#include <wingdi.h>
#include <processthreadsapi.h>
#include <heapapi.h>
#include <handleapi.h>
#include <WinBase.h>
#ifdef CVK_DEMO_VALIDATION
#include "os/win32/os_text.h"
#include <stringapiset.h>
#include <WinNls.h>
#endif

#include "fullscreen_vert.h"
#include "nv12_frag.h"
#include "nv12_pipeline_json.h"
#include "plane_copy_hlsl.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

typedef enum VideoPass
{
    VIDEO_PASS_NV12,
    VIDEO_PASS_BICUBIC,
    VIDEO_PASS_COUNT
} VideoPass;

typedef struct Image
{
    VkImage          image;
    VkDeviceMemory   memory;
    VkImageView      views[2];
    VkFramebuffer    framebuffer;
    ID3D11Texture2D* texture;
    IDXGIKeyedMutex* mutex;
    UINT             width, height;
} Image;

typedef struct Frame
{
    ID3D11Texture2D*          texture;
    ID3D11ShaderResourceView* views[2];
    ID3D11RenderTargetView*   targets[2];
    Image                     planes[2];
} Frame;

struct VideoRenderer
{
    ID3D11Device*         d3d;
    ID3D11DeviceContext*  context;
    VkInstance            instance;
    VkPhysicalDevice      physical;
    VkDevice              device;
    VkQueue               queue;
    uint32_t              family;
    VkCommandPool         pool;
    VkCommandBuffer       command;
    VkCommandBuffer       drawCommands[VIDEO_RENDERER_FRAMES];
    VkCommandBuffer       scaleCommand;
    VkCommandBuffer       copyCommands[VIDEO_RENDERER_FRAMES];
    VkFence               fence;
    VkRenderPass          renderPass;
    VkDescriptorPool      descriptors;
    VkDescriptorSetLayout setLayouts[VIDEO_PASS_COUNT];
    VkPipelineLayout      layouts[VIDEO_PASS_COUNT];
    VkPipeline            pipelines[VIDEO_PASS_COUNT];
    VkSampler             samplers[VIDEO_PASS_COUNT];
    VkDescriptorSet       frames[VIDEO_RENDERER_FRAMES];
    VkDescriptorSet       scale;
    VkExtensionProperties extensionProperties[CVK_MAX_PROPERTIES];
    ID3D11VertexShader*   copyVertex;
    ID3D11PixelShader*    copyPixel;
    Frame                 ring[VIDEO_RENDERER_FRAMES];
    Image                 converted;
    Image                 output;
    UINT                  outputWidth, outputHeight;
    UINT                  recordedWidth, recordedHeight;
#ifdef CVK_DEMO_VALIDATION
    VkDebugUtilsMessengerEXT messenger;
#endif
};

static const VkImageSubresourceRange colorRange = {(VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

static CVK_NORETURN void Fail(LPCTSTR message)
{
    OutputDebugString(message);
    MessageBox(NULL, message, TEXT("ImmersiveWindowDemo"), MB_ICONERROR);
    ExitProcess(1);
}

#ifdef CVK_DEMO_VALIDATION
static VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    enum
    {
        VALIDATION_TEXT_CAPACITY = 1024
    };
    uint16_t unicode[VALIDATION_TEXT_CAPACITY];
    TCHAR    native[VALIDATION_TEXT_CAPACITY];
    int required;
    int written;
    bool_t fits;
    bool_t converted;
    (void)severity;
    (void)type;
    (void)user;
    cvkDiagnosticWrite(data->pMessage);
    cvkDiagnosticWrite("\n");
    required  = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data->pMessage, -1, NULL, 0);
    fits      = 0 < required;
    converted = fits && (required <= (int)(sizeof(unicode) / sizeof(unicode[0])));
    if (converted)
    {
        written   = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, data->pMessage, -1,
                                        (LPWSTR)unicode, (int)(sizeof(unicode) / sizeof(unicode[0])));
        converted = written == required;
        if (converted)
        {
            converted = OS_Text_WriteNative(native, sizeof(native) / sizeof(native[0]), unicode);
        }
    }
    OutputDebugString(converted ? native : TEXT("Vulkan validation message could not be converted"));
    return VK_FALSE;
}
#endif

static void CreateDevice(VideoRenderer* r)
{
    VkApplicationInfo           app;
    VkPhysicalDeviceFeatures    features = {0};
    VkPhysicalDeviceProperties2 properties;
    VkDeviceQueueCreateInfo     queue;
    const char*                 required[] = {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME};
    IDXGIDevice*                dxgi;
    IDXGIAdapter*               adapter;
    DXGI_ADAPTER_DESC           desc;
    VkBool32                    extensions_supported;
    float                       priority = 1.0F;
#ifdef CVK_DEMO_VALIDATION
    const char* layers[]             = {"VK_LAYER_KHRONOS_validation"};
    const char* instanceExtensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
#endif

    if (VK_SUCCESS != volkInitialize())
    {
        Fail(TEXT("A Vulkan 1.1 driver is required."));
    }
    cvkInitApplicationInfo(&app, NULL, "ImmersiveWindowDemo", 1, "cvk", 1, VK_API_VERSION_1_1);
#ifdef CVK_DEMO_VALIDATION
    cvkCreateInstance(NULL, (VkInstanceCreateFlags)0, &app, 1, layers, 1, instanceExtensions, NULL, &r->instance);
#else
    cvkCreateInstance(NULL, (VkInstanceCreateFlags)0, &app, 0, NULL, 0, NULL, NULL, &r->instance);
#endif
    volkLoadInstance(r->instance);
#ifdef CVK_DEMO_VALIDATION
    cvkCreateDebugUtilsMessenger(r->instance,
                                 NULL,
                                 (VkDebugUtilsMessengerCreateFlagsEXT)0,
                                 (VkDebugUtilsMessageSeverityFlagsEXT)(
                                     (VkFlags)VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                     (VkFlags)VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT),
                                 (VkDebugUtilsMessageTypeFlagsEXT)(
                                     (VkFlags)VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                     (VkFlags)VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                     (VkFlags)VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT),
                                 Validation,
                                 NULL,
                                 NULL,
                                 &r->messenger);
#endif
    cdxgiUnknownQueryInterfaceAsDevice((IUnknown*)r->d3d, &dxgi);
    cdxgiDeviceGetAdapter(dxgi, &adapter);
    cdxgiAdapterGetDesc(adapter, &desc);
    cdxgiUnknownRelease((IUnknown*)adapter);
    cdxgiUnknownRelease((IUnknown*)dxgi);
    r->physical = cvkGetPhysicalDeviceByLuid(r->instance, (const uint8_t*)&desc.AdapterLuid);
    if (VK_NULL_HANDLE == r->physical)
    {
        Fail(TEXT("The window's graphics adapter must support Vulkan 1.1."));
    }
    cvkGetPhysicalDeviceProperties2(r->physical, NULL, &properties);
    if (properties.properties.apiVersion < VK_API_VERSION_1_1)
    {
        Fail(TEXT("The window's graphics adapter must support Vulkan 1.1."));
    }
#ifdef CVK_DEMO_VALIDATION
    cvkDiagnosticWrite("video: Vulkan adapter ");
    cvkDiagnosticWrite(properties.properties.deviceName);
    cvkDiagnosticWrite(" (vendor 0x");
    cvkDiagnosticWriteHex(properties.properties.vendorID);
    cvkDiagnosticWrite("), matched to D3D11 LUID\n");
#endif
    extensions_supported = cvkGetPhysicalDeviceExtensionSupport(r->physical, NULL, VK_COUNT_OF(required), required, r->extensionProperties);
    if (!extensions_supported)
    {
        Fail(TEXT("The window's adapter must support Win32 external memory and keyed mutexes."));
    }
    r->family = cvkGetPhysicalDeviceGraphicsQueueFamilyIndex(r->physical);
    if (VK_INVALID == r->family)
    {
        Fail(TEXT("No Vulkan graphics queue on the window's adapter."));
    }
    cvkInitDeviceQueueCreateInfo(&queue, NULL, (VkDeviceQueueCreateFlags)0, r->family, 1, &priority);
    cvkCreateDevice(r->physical, NULL, (VkDeviceCreateFlags)0, 1, &queue, 0, NULL, VK_COUNT_OF(required), required, &features, NULL, &r->device);
    volkLoadDevice(r->device);
    cvkGetDeviceQueue(r->device, r->family, 0, &r->queue);
    cvkSetupPrimaryCommandPool(r->device, NULL, (VkCommandPoolCreateFlags)VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, r->family, NULL, 1, NULL, &r->pool, &r->command);
    cvkAllocatePrimaryCommandBuffers(r->device, NULL, r->pool, VIDEO_RENDERER_FRAMES, r->drawCommands);
    cvkAllocatePrimaryCommandBuffer(r->device, NULL, r->pool, &r->scaleCommand);
    cvkAllocatePrimaryCommandBuffers(r->device, NULL, r->pool, VIDEO_RENDERER_FRAMES, r->copyCommands);
    cvkCreateFence(r->device, NULL, (VkFenceCreateFlags)0, NULL, &r->fence);
}

/* Share single-plane textures. Importing the decoder's multi-planar NV12 layout
** can corrupt chroma or lose the device on the native NVIDIA driver. */
static void CreateSharedImage(VideoRenderer* r, Image* image, UINT width, UINT height, VkFormat format, DXGI_FORMAT d3dFormat, BOOL output)
{
    VkImageUsageFlags usage  = output ? (VkImageUsageFlags)VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : (VkImageUsageFlags)VK_IMAGE_USAGE_SAMPLED_BIT;
    VkExtent2D        extent;
    IDXGIResource1*   resource;
    HANDLE            handle;
    DXGI_SAMPLE_DESC  sample = {1, 0};

    cvkInitExtent2D(&extent, width, height);
    if (!cvkGetPhysicalDeviceD3D11Texture2DImportSupport(r->physical, format, usage))
    {
        Fail(TEXT("The Vulkan driver cannot import the shared video plane or output texture."));
    }
    image->width  = width;
    image->height = height;
    cdx11DeviceCreateTexture2D(r->d3d,
                               width,
                               height,
                               1,
                               1,
                               d3dFormat,
                               sample,
                               D3D11_USAGE_DEFAULT,
                               D3D11_BIND_RENDER_TARGET,
                               0,
                               D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX,
                               NULL,
                               &image->texture);
    cdxgiUnknownQueryInterfaceAsKeyedMutex((IUnknown*)image->texture, &image->mutex);
    cdxgiUnknownQueryInterfaceAsResource1((IUnknown*)image->texture, &resource);
    cdxgiResourceCreateReadWriteSharedHandle(resource, &handle);
    cdxgiUnknownRelease((IUnknown*)resource);
    cvkImportD3D11Texture2D(r->physical, r->device, format, extent, usage, handle, &image->image, &image->memory);
    CloseHandle(handle);
    cvkCreateWholeImageView2D(r->device, (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, NULL, (VkImageViewCreateFlags)0, image->image, format, NULL, &image->views[0]);
    if (output)
    {
        cvkCreateSingleAttachmentFramebuffer(r->device, NULL, (VkFramebufferCreateFlags)0, r->renderPass, image->width, image->height, 1, image->views[0], NULL, &image->framebuffer);
    }
}

static FORCE_INLINE void DestroyImage(VideoRenderer* r, Image* image)
{
    /* Vulkan idle alone does not wait for D3D's queued copies. */
    cdxgiKeyedMutexAcquireReleaseSyncZeroSafe(image->mutex);
    cvkDestroyFramebufferSafe(r->device, image->framebuffer, NULL);
    cvkDestroyImageViewSafe(r->device, image->views[0], NULL);
    cvkDestroyImageViewSafe(r->device, image->views[1], NULL);
    cvkDestroyImageSafe(r->device, image->image, NULL);
    cvkFreeMemorySafe(r->device, image->memory, NULL);
    cdxgiUnknownReleaseSafe((IUnknown*)image->mutex);
    cdxgiUnknownReleaseSafe((IUnknown*)image->texture);
    ZeroMemory(image, sizeof(*image));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Releases a previously selected upload plane without a runtime index.
 */
static FORCE_INLINE void DestroyFramePlane(VideoRenderer*              renderer,
                                           ID3D11RenderTargetView*     target,
                                           ID3D11ShaderResourceView*   view,
                                           Image*                      plane)
{
    cdxgiUnknownReleaseSafe((IUnknown*)target);
    cdxgiUnknownReleaseSafe((IUnknown*)view);
    DestroyImage(renderer, plane);
}

static void DestroyFrame(VideoRenderer* r, Frame* frame)
{
    DestroyFramePlane(r, frame->targets[0], frame->views[0], &frame->planes[0]);
    DestroyFramePlane(r, frame->targets[1], frame->views[1], &frame->planes[1]);
    cdxgiUnknownReleaseSafe((IUnknown*)frame->texture);
    ZeroMemory(frame, sizeof(*frame));
}

/* Copy each NV12 plane without filtering or color conversion. D3D resolves its
** native plane layout; Vulkan receives ordinary R8/RG8 images. No CPU readback. */
static ID3DBlob* CompilePlaneCopyShader(const char* entry, const char* profile, LPCTSTR message)
{
    ID3DBlob* code;

    if (FAILED(D3DCompile(plane_copy_hlsl,
                          sizeof(plane_copy_hlsl) - 1,
                          NULL,
                          NULL,
                          NULL,
                          entry,
                          profile,
                          D3DCOMPILE_OPTIMIZATION_LEVEL3,
                          0,
                          &code,
                          NULL)))
    {
        Fail(message);
    }
    return code;
}

static FORCE_INLINE void CreatePlaneCopyShaders(VideoRenderer* r)
{
    ID3DBlob* code;

    code = CompilePlaneCopyShader("VS", "vs_5_0", TEXT("Cannot compile the video plane copy vertex shader."));
    cdx11SetupVertexShaderFromBlob(r->d3d, code, &r->copyVertex);
    cdxgiUnknownReleaseSafe((IUnknown*)code);
    code = CompilePlaneCopyShader("PS", "ps_5_0", TEXT("Cannot compile the video plane copy pixel shader."));
    cdx11SetupPixelShaderFromBlob(r->d3d, code, &r->copyPixel);
    cdxgiUnknownReleaseSafe((IUnknown*)code);
}

enum
{
    PIPELINE_JSON_VALUE_CAPACITY = 512,
    PIPELINE_JSON_ARENA_CAPACITY = 32768
};

typedef struct PipelineScratch
{
    cvkJsonValue           values[PIPELINE_JSON_VALUE_CAPACITY];
    uint8_t                storage[PIPELINE_JSON_ARENA_CAPACITY];
    cvkPipelineJsonObjects objects;
} PipelineScratch;

static void CreatePipeline(VideoRenderer* r, VideoPass pass)
{
    PipelineScratch* scratch = (PipelineScratch*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*scratch));
    cvkArena         arena;

    if (!scratch)
    {
        Fail(TEXT("Cannot allocate pipeline JSON scratch space."));
    }
    arena = (cvkArena){scratch->storage, sizeof(scratch->storage), 0};

    if (VIDEO_PASS_NV12 == pass)
    {
        cvkCreateGraphicsPipelineFromJson(r->device,
                                          VK_NULL_HANDLE,
                                          nv12_pipeline_json,
                                          sizeof(nv12_pipeline_json) - 1,
                                          scratch->values,
                                          VK_COUNT_OF(scratch->values),
                                          &arena,
                                          sizeof(fullscreen_vert),
                                          fullscreen_vert,
                                          sizeof(nv12_frag),
                                          nv12_frag,
                                          VK_NULL_HANDLE,
                                          NULL,
                                          &scratch->objects);
        r->renderPass = scratch->objects.renderPass;
    }
    else
    {
        cvkCreateBicubicScalingPipeline(r->device,
                                        VK_NULL_HANDLE,
                                        scratch->values,
                                        VK_COUNT_OF(scratch->values),
                                        &arena,
                                        r->renderPass,
                                        NULL,
                                        &scratch->objects);
    }
    r->setLayouts[pass] = scratch->objects.descriptorSetLayouts[0];
    r->samplers[pass]   = scratch->objects.samplers[0];
    r->layouts[pass]    = scratch->objects.pipelineLayout;
    r->pipelines[pass]  = scratch->objects.pipeline;
    HeapFree(GetProcessHeap(), 0, scratch);
}

static void CreatePasses(VideoRenderer* r)
{
    int index;

    CreatePipeline(r, VIDEO_PASS_NV12);
    CreatePipeline(r, VIDEO_PASS_BICUBIC);
    cvkCreateSingleTypeDescriptorPool(r->device,
                                      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                      (VIDEO_RENDERER_FRAMES * 2) + 1,
                                      NULL,
                                      (VkDescriptorPoolCreateFlags)0,
                                      VIDEO_RENDERER_FRAMES + 1,
                                      NULL,
                                      &r->descriptors);
    for (index = 0; index < VIDEO_RENDERER_FRAMES; ++index)
    {
        cvkAllocateDescriptorSet(r->device, NULL, r->descriptors, r->setLayouts[VIDEO_PASS_NV12], &r->frames[index]);
    }
    cvkAllocateDescriptorSet(r->device, NULL, r->descriptors, r->setLayouts[VIDEO_PASS_BICUBIC], &r->scale);
}

extern FORCE_INLINE VideoRenderer* VideoRenderer_Create(ID3D11Device* device, ID3D11DeviceContext* context)
{
    VideoRenderer* renderer = (VideoRenderer*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*renderer));
    HDC            desktop;
    int            desktopWidth;
    int            desktopHeight;
    bool_t         desktopAvailable;
    bool_t         desktopReleased;
    bool_t         desktopSizeValid;

    if (!renderer)
    {
        Fail(TEXT("Cannot allocate the video renderer."));
    }
    renderer->d3d     = device;
    renderer->context = context;
    CreatePlaneCopyShaders(renderer);
    CreateDevice(renderer);
    CreatePasses(renderer);
    desktop = GetDC(NULL);
    desktopAvailable = (NULL != desktop);
    if (!desktopAvailable)
    {
        Fail(TEXT("Cannot query the primary desktop size."));
    }
    desktopWidth = GetDeviceCaps(desktop, DESKTOPHORZRES);
    desktopHeight = GetDeviceCaps(desktop, DESKTOPVERTRES);
    desktopReleased = (0 != ReleaseDC(NULL, desktop));
    if (!desktopReleased)
    {
        Fail(TEXT("Cannot release the primary desktop DC."));
    }
    desktopSizeValid = (0 < desktopWidth) && (0 < desktopHeight);
    if (!desktopSizeValid)
    {
        Fail(TEXT("Cannot query the primary desktop size."));
    }
    CreateSharedImage(renderer, &renderer->output, (UINT)desktopWidth, (UINT)desktopHeight, VK_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, TRUE);
    return renderer;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Creates one explicitly selected NV12 plane and binds its Vulkan image.
 */
static FORCE_INLINE void CreateFramePlane(VideoRenderer*                renderer,
                                          Frame*                        frame,
                                          Image*                        plane,
                                          ID3D11ShaderResourceView**    view,
                                          ID3D11RenderTargetView**      target,
                                          VkDescriptorSet               descriptor,
                                          uint32_t                      binding,
                                          UINT                          width,
                                          UINT                          height,
                                          VkFormat                      vk_format,
                                          DXGI_FORMAT                   dxgi_format)
{
    CreateSharedImage(renderer, plane, width, height, vk_format, dxgi_format, FALSE);
    cdx11DeviceCreateWholeTexture2DShaderResourceView(renderer->d3d, (ID3D11Resource*)frame->texture, dxgi_format, view);
    cdx11DeviceCreateTexture2DRenderTargetView(renderer->d3d, (ID3D11Resource*)plane->texture, 0, dxgi_format, target);
    cvkUpdateCombinedImageSamplerDescriptorSet(renderer->device, renderer->samplers[VIDEO_PASS_NV12], plane->views[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, NULL, descriptor, binding, 0);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Recreates the two fixed planes of one frame slot.
 */
static FORCE_INLINE void RecreateFrame(VideoRenderer* renderer,
                                       Frame*         frame,
                                       VkDescriptorSet descriptor,
                                       UINT           width,
                                       UINT           height)
{
    DXGI_SAMPLE_DESC sample = {1, 0};

    DestroyFrame(renderer, frame);
    cdx11DeviceCreateTexture2D(renderer->d3d, width, height, 1, 1, DXGI_FORMAT_NV12, sample, D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, NULL, &frame->texture);
    CreateFramePlane(renderer, frame, &frame->planes[0], &frame->views[0], &frame->targets[0], descriptor, 0, width, height, VK_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM);
    CreateFramePlane(renderer, frame, &frame->planes[1], &frame->views[1], &frame->targets[1], descriptor, 1, width >> 1, height >> 1, VK_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM);
}

static void RecordCopyCommands(VideoRenderer* r)
{
    VkExtent2D      extent;
    VkImage         sampledImages[2];
    VkCommandBuffer command;
    uint32_t        frameIndex;

    cvkInitExtent2D(&extent, r->converted.width, r->converted.height);
    for (frameIndex = 0; frameIndex < VIDEO_RENDERER_FRAMES; ++frameIndex)
    {
        command          = r->copyCommands[frameIndex];
        sampledImages[0] = r->ring[frameIndex].planes[0].image;
        sampledImages[1] = r->ring[frameIndex].planes[1].image;
        cvkResetCommandBuffer(command, (VkCommandBufferResetFlags)0);
        cvkBeginCommandBuffer(command, NULL, (VkCommandBufferUsageFlags)0, NULL);
        cvkCmdAcquireSampledImagesFromExternal(command,
                                               VK_IMAGE_LAYOUT_GENERAL,
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                               r->family,
                                               VK_COUNT_OF(sampledImages),
                                               sampledImages,
                                               colorRange,
                                               (VkPipelineStageFlags)VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        cvkCmdExecuteFullscreenQuadRenderPassOnExternalImage(command,
                                                             VK_IMAGE_LAYOUT_GENERAL,
                                                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                             r->family,
                                                             r->output.image,
                                                             colorRange,
                                                             r->renderPass,
                                                             r->output.framebuffer,
                                                             extent,
                                                             r->pipelines[VIDEO_PASS_NV12],
                                                             r->layouts[VIDEO_PASS_NV12],
                                                             r->frames[frameIndex]);
        cvkCmdReleaseSampledImagesToExternal(command,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                             VK_IMAGE_LAYOUT_GENERAL,
                                             r->family,
                                             VK_COUNT_OF(sampledImages),
                                             sampledImages,
                                             colorRange,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        cvkEndCommandBuffer(command);
    }
}

void VideoRenderer_SetSize(VideoRenderer* r, UINT width, UINT height)
{
    VkExtent2D extent;
    VkResult   result;
    CVK_ASSERT(width && height && !(width & 1) && !(height & 1));
    cvkInitExtent2D(&extent, width, height);
    cvkDeviceWaitIdle(r->device);
    DestroyImage(r, &r->converted);
    RecreateFrame(r, &r->ring[0], r->frames[0], width, height);
    RecreateFrame(r, &r->ring[1], r->frames[1], width, height);
    r->converted.width  = width;
    r->converted.height = height;
    cvkSetupDeviceLocalImage2D(r->physical,
                               r->device,
                               NULL,
                               (VkImageCreateFlags)0,
                               VK_FORMAT_B8G8R8A8_UNORM,
                               1,
                               1,
                               VK_SAMPLE_COUNT_1_BIT,
                               VK_IMAGE_TILING_OPTIMAL,
                               (VkImageUsageFlags)(
                                   (VkFlags)VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                                   (VkFlags)VK_IMAGE_USAGE_SAMPLED_BIT),
                               VK_SHARING_MODE_EXCLUSIVE,
                               0,
                               NULL,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               extent,
                               NULL,
                               NULL,
                               &r->converted.image,
                               &r->converted.memory);
    cvkCreateWholeImageView2D(r->device, (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT, NULL, (VkImageViewCreateFlags)0, r->converted.image, VK_FORMAT_B8G8R8A8_UNORM, NULL, &r->converted.views[0]);
    cvkCreateSingleAttachmentFramebuffer(r->device,
                                         NULL,
                                         (VkFramebufferCreateFlags)0,
                                         r->renderPass,
                                         r->converted.width,
                                         r->converted.height,
                                         1,
                                         r->converted.views[0],
                                         NULL,
                                         &r->converted.framebuffer);
    cvkUpdateCombinedImageSamplerDescriptorSet(r->device,
                                               r->samplers[VIDEO_PASS_BICUBIC],
                                               r->converted.views[0],
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                               NULL,
                                               r->scale,
                                               0,
                                               0);
    cvkResetCommandBuffer(r->command, (VkCommandBufferResetFlags)0);
    cvkBeginCommandBuffer(r->command, NULL, (VkCommandBufferUsageFlags)0, NULL);
    cvkCmdTransitionUndefinedImageToSampledImage(r->command,
                                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                 r->converted.image,
                                                 colorRange,
                                                 (VkPipelineStageFlags)VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    cvkEndCommandBuffer(r->command);
    cvkResetFence(r->device, r->fence);
    cvkQueueSubmitInfo(r->queue, NULL, 0, NULL, NULL, 1, &r->command, 0, NULL, r->fence);
    result = cvkWaitForFenceIndefinitely(r->device, r->fence);
    CVK_ASSERT(VK_SUCCESS == result);
    if ((width <= r->output.width) && (height <= r->output.height))
    {
        RecordCopyCommands(r);
    }
    r->outputWidth = r->outputHeight = 0;
    r->recordedWidth = r->recordedHeight = 0;
}

/** The ring count must remain a power of two for the bounded index mask. */
#if (VIDEO_RENDERER_FRAMES & (VIDEO_RENDERER_FRAMES - 1)) != 0
#error The video frame ring count must be a power of two.
#endif

/*************************************************************************************************/
/**
 * \par Details:
 *  Resolves the frame before validating the caller's slot so every load uses a bounded index.
 */
static FORCE_INLINE uint32_t FrameIndex(int slot)
{
    uint32_t index;

    index = (uint32_t)slot & (VIDEO_RENDERER_FRAMES - 1);
    CVK_ASSERT(slot == (int)index);
    return index;
}

extern FORCE_INLINE ID3D11Texture2D* VideoRenderer_GetFrame(VideoRenderer* r, int slot)
{
    uint32_t frame_index;

    frame_index = FrameIndex(slot);
    return r->ring[frame_index].texture;
}

extern FORCE_INLINE void VideoRenderer_BeginUpload(VideoRenderer* r, int slot)
{
    uint32_t frame_index;

    frame_index = FrameIndex(slot);
    cdxgiKeyedMutexAcquireSync(r->ring[frame_index].planes[0].mutex, 0, INFINITE);
    cdxgiKeyedMutexAcquireSync(r->ring[frame_index].planes[1].mutex, 0, INFINITE);
}

void VideoRenderer_EndUpload(VideoRenderer* r, int slot)
{
    Frame* frame = &r->ring[FrameIndex(slot)];
    cdx11DeviceContextTransferMultiplanarTexture(r->context,
                                                 r->copyVertex,
                                                 r->copyPixel,
                                                 frame->views[0],
                                                 frame->targets[0],
                                                 frame->views[1],
                                                 frame->targets[1],
                                                 frame->planes[0].width,
                                                 frame->planes[0].height);
    cdxgiKeyedMutexReleaseSync(frame->planes[0].mutex, 0);
    cdxgiKeyedMutexReleaseSync(frame->planes[1].mutex, 0);
    cdx11DeviceContextFlush(r->context);
}

static void RecordDrawCommands(VideoRenderer* r, UINT width, UINT height)
{
    VkExtent2D      sourceExtent;
    VkExtent2D      contentExtent;
    VkExtent2D      backingExtent;
    VkImage         sampledImages[2];
    VkCommandBuffer command;
    uint32_t        frameIndex;

    cvkInitExtent2D(&sourceExtent, r->converted.width, r->converted.height);
    cvkInitExtent2D(&contentExtent, width, height);
    cvkInitExtent2D(&backingExtent, r->output.width, r->output.height);
    for (frameIndex = 0; frameIndex < VIDEO_RENDERER_FRAMES; ++frameIndex)
    {
        command          = r->drawCommands[frameIndex];
        sampledImages[0] = r->ring[frameIndex].planes[0].image;
        sampledImages[1] = r->ring[frameIndex].planes[1].image;
        cvkResetCommandBuffer(command, (VkCommandBufferResetFlags)0);
        cvkBeginCommandBuffer(command, NULL, (VkCommandBufferUsageFlags)0, NULL);
        cvkCmdAcquireSampledImagesFromExternal(command,
                                               VK_IMAGE_LAYOUT_GENERAL,
                                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                               r->family,
                                               VK_COUNT_OF(sampledImages),
                                               sampledImages,
                                               colorRange,
                                               (VkPipelineStageFlags)VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        cvkCmdTransitionSampledImageToColorAttachment(command,
                                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                      VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                      r->converted.image,
                                                      colorRange,
                                                      (VkPipelineStageFlags)VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        cvkCmdExecuteFullscreenQuadRenderPassToSampledImage(command,
                                                            r->converted.image,
                                                            colorRange,
                                                            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                                            (VkPipelineStageFlags)VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                                            r->renderPass,
                                                            r->converted.framebuffer,
                                                            sourceExtent,
                                                            r->pipelines[VIDEO_PASS_NV12],
                                                            r->layouts[VIDEO_PASS_NV12],
                                                            r->frames[frameIndex]);
        cvkCmdReleaseSampledImagesToExternal(command,
                                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                             VK_IMAGE_LAYOUT_GENERAL,
                                             r->family,
                                             VK_COUNT_OF(sampledImages),
                                             sampledImages,
                                             colorRange,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        cvkCmdExecuteBicubicScalingRenderPassOnExternalImage(command,
                                                             VK_IMAGE_LAYOUT_GENERAL,
                                                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                             r->family,
                                                             r->output.image,
                                                             colorRange,
                                                             r->renderPass,
                                                             r->output.framebuffer,
                                                             backingExtent,
                                                             r->pipelines[VIDEO_PASS_BICUBIC],
                                                             r->layouts[VIDEO_PASS_BICUBIC],
                                                             r->scale,
                                                             sourceExtent,
                                                             contentExtent,
                                                             0.0F);
        cvkEndCommandBuffer(command);
    }
    command = r->scaleCommand;
    cvkResetCommandBuffer(command, (VkCommandBufferResetFlags)0);
    cvkBeginCommandBuffer(command, NULL, (VkCommandBufferUsageFlags)0, NULL);
    cvkCmdExecuteBicubicScalingRenderPassOnExternalImage(command,
                                                         VK_IMAGE_LAYOUT_GENERAL,
                                                         VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                         r->family,
                                                         r->output.image,
                                                         colorRange,
                                                         r->renderPass,
                                                         r->output.framebuffer,
                                                         backingExtent,
                                                         r->pipelines[VIDEO_PASS_BICUBIC],
                                                         r->layouts[VIDEO_PASS_BICUBIC],
                                                         r->scale,
                                                         sourceExtent,
                                                         contentExtent,
                                                         0.0F);
    cvkEndCommandBuffer(command);
    r->recordedWidth  = width;
    r->recordedHeight = height;
}

void VideoRenderer_Draw(VideoRenderer* r, ID3D11Texture2D* target, int slot, BOOL convert, UINT width, UINT height)
{
    uint32_t             frame_index;
    VkResult             result;
    D3D11_TEXTURE2D_DESC desc;
    VkDeviceMemory       syncs[3];
    VkCommandBuffer      command;
    uint64_t             acquireKeys[3] = {0, 0, 0};
    uint64_t             releaseKeys[3] = {1, 0, 0};
    uint32_t             timeouts[3]    = {INFINITE, INFINITE, INFINITE};
    uint32_t             count          = convert ? 3 : 1;
    BOOL                 redraw;
    bool_t               contentFitsBackBuffer;
    bool_t               outputMatchesBackBuffer;
    bool_t               contentWidthChanged;
    bool_t               contentHeightChanged;
    bool_t               drawCommandsOutdated;

    frame_index = FrameIndex(slot);
    cdx11Texture2DGetDesc(target, &desc);
    contentFitsBackBuffer   = (width <= desc.Width) && (height <= desc.Height);
    outputMatchesBackBuffer = (r->output.width == desc.Width) && (r->output.height == desc.Height);
    CVK_ASSERT(contentFitsBackBuffer);
    CVK_ASSERT(outputMatchesBackBuffer);
    /* Hover, activation and the old-geometry resize latch often repaint the
    ** same video at the same size. Keep the scaled pixels as well as the NV12
    ** conversion, so these paints only copy and draw the caption. */
    contentWidthChanged  = (width != r->outputWidth);
    contentHeightChanged = (height != r->outputHeight);
    redraw               = convert || contentWidthChanged || contentHeightChanged;
    if (redraw)
    {
        drawCommandsOutdated = (width != r->recordedWidth) || (height != r->recordedHeight);
        if (drawCommandsOutdated)
        {
            RecordDrawCommands(r, width, height);
        }
        command  = convert ? r->drawCommands[frame_index] : r->scaleCommand;
        syncs[0] = r->output.memory;
        syncs[1] = r->ring[frame_index].planes[0].memory;
        syncs[2] = r->ring[frame_index].planes[1].memory;
        cvkResetFence(r->device, r->fence);
        cvkQueueSubmitInfoWithWin32KeyedMutexAcquireReleaseInfo(r->queue, NULL, count, syncs, acquireKeys, timeouts, count, syncs, releaseKeys, 0, NULL, NULL, 1, &command, 0, NULL, r->fence);
        r->outputWidth  = width;
        r->outputHeight = height;
    }
    /* The copy precedes the original Direct2D caption and DXGI presentation. */
    /* Cover the entire composition buffer, including pixels a growing window
    ** can expose before its geometry paint reaches DWM. The shader preserves
    ** the exact client-area mapping and clamps the extra area to video edges.
    ** A cropped copy leaves undefined pixels in a flip-discard back buffer. */
    cdx11DeviceContextCopyResourceWithKeyedMutex(r->context, (ID3D11Resource*)target, (ID3D11Resource*)r->output.texture, r->output.mutex, redraw ? 1 : 0, 0);
    if (redraw)
    {
        result = cvkWaitForFenceIndefinitely(r->device, r->fence);
        CVK_ASSERT(VK_SUCCESS == result);
    }
}

void VideoRenderer_CopyFrame(VideoRenderer* r, ID3D11Texture2D* target, UINT subresource, UINT x, UINT y, int slot)
{
    uint32_t        frame_index;
    VkResult        result;
    VkDeviceMemory  syncs[3];
    uint64_t        acquireKeys[3] = {0, 0, 0};
    uint64_t        releaseKeys[3] = {1, 0, 0};
    uint32_t        timeouts[3]    = {INFINITE, INFINITE, INFINITE};
    UINT            width          = r->converted.width;
    UINT            height         = r->converted.height;
    D3D11_BOX       source         = {0, 0, 0, width, height, 1};
    VkCommandBuffer command;
    bool_t          frameFitsOutput;

    frame_index     = FrameIndex(slot);
    frameFitsOutput = (width <= r->output.width) && (height <= r->output.height);
    CVK_ASSERT(frameFitsOutput);
    command  = r->copyCommands[frame_index];
    syncs[0] = r->output.memory;
    syncs[1] = r->ring[frame_index].planes[0].memory;
    syncs[2] = r->ring[frame_index].planes[1].memory;
    cvkResetFence(r->device, r->fence);
    cvkQueueSubmitInfoWithWin32KeyedMutexAcquireReleaseInfo(r->queue, NULL, 3, syncs, acquireKeys, timeouts, 3, syncs, releaseKeys, 0, NULL, NULL, 1, &command, 0, NULL, r->fence);
    cdx11DeviceContextCopySubresourceRegionWithKeyedMutex(r->context, (ID3D11Resource*)target, subresource, x, y, 0, (ID3D11Resource*)r->output.texture, 0, &source, r->output.mutex, 1, 0);
    result = cvkWaitForFenceIndefinitely(r->device, r->fence);
    CVK_ASSERT(VK_SUCCESS == result);
    r->outputWidth = r->outputHeight = 0;
}

void VideoRenderer_Destroy(VideoRenderer* r)
{
    int index;
    if (!r)
    {
        return;
    }
    cvkDeviceWaitIdle(r->device);
    cvkDestroyDescriptorPool(r->device, r->descriptors, NULL);
    DestroyImage(r, &r->output);
    DestroyImage(r, &r->converted);
    for (index = 0; index < VIDEO_RENDERER_FRAMES; ++index)
    {
        DestroyFrame(r, &r->ring[index]);
    }
    cdxgiUnknownRelease((IUnknown*)r->copyPixel);
    cdxgiUnknownRelease((IUnknown*)r->copyVertex);
    for (index = 0; index < VIDEO_PASS_COUNT; ++index)
    {
        cvkDestroyPipeline(r->device, r->pipelines[index], NULL);
        cvkDestroyPipelineLayout(r->device, r->layouts[index], NULL);
        cvkDestroyDescriptorSetLayout(r->device, r->setLayouts[index], NULL);
        cvkDestroySampler(r->device, r->samplers[index], NULL);
    }
    cvkDestroyRenderPass(r->device, r->renderPass, NULL);
    cvkDestroyFence(r->device, r->fence, NULL);
    cvkDestroyCommandPool(r->device, r->pool, NULL);
    cvkDestroyDevice(r->device, NULL);
#ifdef CVK_DEMO_VALIDATION
    cvkDestroyDebugUtilsMessenger(r->instance, r->messenger, NULL);
#endif
    cvkDestroyInstance(r->instance, NULL);
    volkFinalize();
    HeapFree(GetProcessHeap(), 0, r);
}
