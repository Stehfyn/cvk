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
#include <minwinbase.h>
#include <winnt.h>
#include <Unknwnbase.h>
#include <winerror.h>
#include <debugapi.h>
#include <WinUser.h>
#include <processthreadsapi.h>
#include <heapapi.h>
#include <handleapi.h>
#include <WinBase.h>
#ifdef CVK_DEMO_VALIDATION
#include "os/win32/os_text.h"
#include <stringapiset.h>
#include <WinNls.h>
#include <stdio.h>
#endif
#include <string.h>

#include "fullscreen_vert.h"
#include "nv12_frag.h"
#include "bicubic_frag.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define VIDEO_SAMPLER_ANISOTROPY (16.0F) ///< Reference bicubic sampler's required anisotropy.
#define VIDEO_SCALE_FLOAT_COUNT  (6)     ///< Two inverse sizes, two biases and two scale factors.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

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
    VkFence               fence;
    VkRenderPass          renderPass;
    VkDescriptorPool      descriptors;
    VkDescriptorSetLayout setLayouts[2];
    VkPipelineLayout      layouts[2];
    VkPipeline            pipelines[2];
    VkSampler             samplers[2];
    VkDescriptorSet       frames[VIDEO_RENDERER_FRAMES];
    VkDescriptorSet       scale;
    VkExtensionProperties extensionProperties[CVK_MAX_PROPERTIES];
    ID3D11VertexShader*   copyVertex;
    ID3D11PixelShader*    copyPixel;
    Frame                 ring[VIDEO_RENDERER_FRAMES];
    Image                 converted;
    Image                 output;
    BOOL                  convertedInitialized;
    UINT                  outputWidth, outputHeight;
#ifdef CVK_DEMO_VALIDATION
    VkDebugUtilsMessengerEXT messenger;
#endif
};

static const VkImageSubresourceRange colorRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

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
    cvkCreateInstance(NULL, 0, &app, 1, layers, 1, instanceExtensions, NULL, &r->instance);
#else
    cvkCreateInstance(NULL, 0, &app, 0, NULL, 0, NULL, NULL, &r->instance);
#endif
    volkLoadInstance(r->instance);
#ifdef CVK_DEMO_VALIDATION
    cvkCreateDebugUtilsMessenger(r->instance,
                                 NULL,
                                 0,
                                 VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
                                 VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
                                 Validation,
                                 NULL,
                                 NULL,
                                 &r->messenger);
#endif
    cdxgiUnknownQueryInterface((IUnknown*)r->d3d, &IID_IDXGIDevice, (void**)&dxgi);
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
    cvkGetPhysicalDeviceFeatures(r->physical, &features);
    if (!features.samplerAnisotropy || properties.properties.limits.maxSamplerAnisotropy < VIDEO_SAMPLER_ANISOTROPY)
    {
        Fail(TEXT("The reference video sampler requires 16x anisotropic filtering."));
    }
    features = (VkPhysicalDeviceFeatures){.samplerAnisotropy = VK_TRUE};
    cvkInitDeviceQueueCreateInfo(&queue, NULL, 0, r->family, 1, &priority);
    cvkCreateDevice(r->physical, NULL, 0, 1, &queue, 0, NULL, VK_COUNT_OF(required), required, &features, NULL, &r->device);
    volkLoadDevice(r->device);
    cvkGetDeviceQueue(r->device, r->family, 0, &r->queue);
    cvkSetupPrimaryCommandPool(r->device, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, r->family, NULL, 1, NULL, &r->pool, &r->command);
    cvkCreateFence(r->device, NULL, 0, NULL, &r->fence);
}

static FORCE_INLINE void CreateView(VideoRenderer* r, Image* image, int index, VkFormat format, VkImageAspectFlags aspect)
{
    cvkCreateWholeImageView2D(r->device, aspect, NULL, 0, image->image, format, NULL, &image->views[index]);
}

static FORCE_INLINE void CreateFramebuffer(VideoRenderer* r, Image* image)
{
    cvkCreateSingleAttachmentFramebuffer(r->device, NULL, 0, r->renderPass, image->width, image->height, 1, image->views[0], NULL, &image->framebuffer);
}

/* Share single-plane textures. Importing the decoder's multi-planar NV12 layout
** can corrupt chroma or lose the device on the native NVIDIA driver. */
static void CreateSharedImage(VideoRenderer* r, Image* image, UINT width, UINT height, VkFormat format, DXGI_FORMAT d3dFormat, BOOL output)
{
    VkImageUsageFlags                usage  = output ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageCreateFlags               flags  = 0;
    VkExtent3D                       extent = {width, height, 1};
    VkBool32                         dedicatedOnly;
    IDXGIResource1*                  resource;
    HANDLE                           handle;
    DXGI_SAMPLE_DESC                 sample = {1, 0};

    if (!cvkGetPhysicalDeviceExternalImageImportSupport(r->physical,
                                                        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
                                                        format,
                                                        VK_IMAGE_TYPE_2D,
                                                        VK_IMAGE_TILING_OPTIMAL,
                                                        usage,
                                                        flags,
                                                        NULL,
                                                        &dedicatedOnly))
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
    cdxgiUnknownQueryInterface((IUnknown*)image->texture, &IID_IDXGIKeyedMutex, (void**)&image->mutex);
    cdxgiUnknownQueryInterface((IUnknown*)image->texture, &IID_IDXGIResource1, (void**)&resource);
    cdxgiResourceCreateReadWriteSharedHandle(resource, &handle);
    cdxgiUnknownRelease((IUnknown*)resource);
    cvkImportWin32HandleImage(r->physical,
                              r->device,
                              NULL,
                              flags,
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
                              VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
                              handle,
                              NULL,
                              0,
                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                              NULL,
                              NULL,
                              &image->image,
                              &image->memory);
    CloseHandle(handle);
    CreateView(r, image, 0, format, VK_IMAGE_ASPECT_COLOR_BIT);
    if (output)
    {
        CreateFramebuffer(r, image);
    }
}

static FORCE_INLINE void DestroyImage(VideoRenderer* r, Image* image)
{
    /* Vulkan idle alone does not wait for D3D's queued copies. */
    if (image->mutex)
    {
        cdxgiKeyedMutexAcquireSync(image->mutex, 0, INFINITE);
        cdxgiKeyedMutexReleaseSync(image->mutex, 0);
    }
    if (image->framebuffer)
    {
        cvkDestroyFramebuffer(r->device, image->framebuffer, NULL);
    }
    if (image->views[0])
    {
        cvkDestroyImageView(r->device, image->views[0], NULL);
    }
    if (image->views[1])
    {
        cvkDestroyImageView(r->device, image->views[1], NULL);
    }
    if (image->image)
    {
        cvkDestroyImage(r->device, image->image, NULL);
    }
    if (image->memory)
    {
        cvkFreeMemory(r->device, image->memory, NULL);
    }
    if (image->mutex)
    {
        cdxgiUnknownRelease((IUnknown*)image->mutex);
    }
    if (image->texture)
    {
        cdxgiUnknownRelease((IUnknown*)image->texture);
    }
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
    if (target)
    {
        cdxgiUnknownRelease((IUnknown*)target);
    }
    if (view)
    {
        cdxgiUnknownRelease((IUnknown*)view);
    }
    DestroyImage(renderer, plane);
}

static void DestroyFrame(VideoRenderer* r, Frame* frame)
{
    DestroyFramePlane(r, frame->targets[0], frame->views[0], &frame->planes[0]);
    DestroyFramePlane(r, frame->targets[1], frame->views[1], &frame->planes[1]);
    if (frame->texture)
    {
        cdxgiUnknownRelease((IUnknown*)frame->texture);
    }
    ZeroMemory(frame, sizeof(*frame));
}

/* Copy each NV12 plane without filtering or color conversion. D3D resolves its
** native plane layout; Vulkan receives ordinary R8/RG8 images. No CPU readback. */
static FORCE_INLINE void CreatePlaneCopyShaders(VideoRenderer* r)
{
    static const char source[] = "float4 VS(uint id : SV_VertexID) : SV_Position {"
                                 "float2 p = float2((id << 1) & 2, id & 2);"
                                 "return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }"
                                 "Texture2D<float2> plane : register(t0);"
                                 "float4 PS(float4 p : SV_Position) : SV_Target {"
                                 "return float4(plane.Load(int3(int2(p.xy), 0)), 0, 1); }";
    ID3DBlob*         code;
    if (FAILED(D3DCompile(source, sizeof(source) - 1, NULL, NULL, NULL, "VS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, NULL)))
    {
        Fail(TEXT("Cannot compile the video plane copy vertex shader."));
    }
    cdx11DeviceCreateVertexShader(r->d3d, code->lpVtbl->GetBufferPointer(code), code->lpVtbl->GetBufferSize(code), NULL, &r->copyVertex);
    cdxgiUnknownRelease((IUnknown*)code);
    if (FAILED(D3DCompile(source, sizeof(source) - 1, NULL, NULL, NULL, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, NULL)))
    {
        Fail(TEXT("Cannot compile the video plane copy pixel shader."));
    }
    cdx11DeviceCreatePixelShader(r->d3d, code->lpVtbl->GetBufferPointer(code), code->lpVtbl->GetBufferSize(code), NULL, &r->copyPixel);
    cdxgiUnknownRelease((IUnknown*)code);
}

static void CreatePipeline(VideoRenderer* r, int pass, const uint32_t* fragment, size_t size)
{
    VkResult                               result;
    VkShaderModule                         vertex;
    VkShaderModule                         pixel;
    VkPipelineVertexInputStateCreateInfo   input;
    VkPipelineInputAssemblyStateCreateInfo assembly;
    VkPipelineViewportStateCreateInfo      viewport;
    VkPipelineRasterizationStateCreateInfo raster;
    VkPipelineMultisampleStateCreateInfo   multisample;
    VkPipelineColorBlendAttachmentState    attachment;
    VkPipelineColorBlendStateCreateInfo    blend;
    VkDynamicState                         dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo       dynamic         = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamicStates};

    cvkInitEmptyPipelineVertexInputStateCreateInfo(&input, NULL, 0);
    cvkInitTriangleStripPipelineInputAssemblyStateCreateInfo(&assembly, NULL, 0);
    cvkInitSinglePipelineViewportStateCreateInfo(&viewport, NULL, 0, NULL, NULL);
    cvkInitFillPipelineRasterizationStateCreateInfo(&raster, NULL, 0, VK_CULL_MODE_NONE, VK_FRONT_FACE_COUNTER_CLOCKWISE);
    cvkInitSampleCountPipelineMultisampleStateCreateInfo(&multisample, NULL, 0, VK_SAMPLE_COUNT_1_BIT);
    cvkInitOpaquePipelineColorBlendAttachmentState(&attachment);
    cvkInitSingleAttachmentPipelineColorBlendStateCreateInfo(&blend, NULL, 0, &attachment);
    result = cvkSetupGraphicsPipeline(r->device,
                                      NULL,
                                      1,
                                      &r->setLayouts[pass],
                                      0,
                                      pass ? VIDEO_SCALE_FLOAT_COUNT * sizeof(float) : 0,
                                      VK_NULL_HANDLE,
                                      NULL,
                                      0,
                                      &input,
                                      &assembly,
                                      NULL,
                                      &viewport,
                                      &raster,
                                      &multisample,
                                      NULL,
                                      &blend,
                                      &dynamic,
                                      r->renderPass,
                                      0,
                                      0,
                                      0,
                                      NULL,
                                      sizeof(fullscreen_vert),
                                      fullscreen_vert,
                                      NULL,
                                      size,
                                      fragment,
                                      NULL,
                                      NULL,
                                      0,
                                      "main",
                                      NULL,
                                      "main",
                                      NULL,
                                      NULL,
                                      &vertex,
                                      &pixel,
                                      &r->layouts[pass],
                                      &r->pipelines[pass]);
    CVK_ASSERT(VK_SUCCESS == result);
    cvkDestroyShaderModule(r->device, pixel, NULL);
    cvkDestroyShaderModule(r->device, vertex, NULL);
}

static void CreatePasses(VideoRenderer* r)
{
    VkDescriptorSetLayoutBinding bindings[2] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
                                                {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};
    int                          index;

    cvkCreateColorRenderPass(r->device,
                             VK_FORMAT_B8G8R8A8_UNORM,
                             VK_SAMPLE_COUNT_1_BIT,
                             VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                             VK_ATTACHMENT_STORE_OP_STORE,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                             NULL,
                             0,
                             NULL,
                             &r->renderPass);
    for (index = 0; index < 2; ++index)
    {
        cvkCreateDescriptorSetLayout(r->device, NULL, 0, index ? 1 : 2, bindings, NULL, &r->setLayouts[index]);
        cvkCreateSampler(r->device,
                         NULL,
                         0,
                         VK_FILTER_LINEAR,
                         VK_FILTER_LINEAR,
                         VK_SAMPLER_MIPMAP_MODE_LINEAR,
                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                         VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                         0,
                         index ? VK_TRUE : VK_FALSE,
                         index ? VIDEO_SAMPLER_ANISOTROPY : 1.0F,
                         VK_FALSE,
                         VK_COMPARE_OP_NEVER,
                         0,
                         VK_LOD_CLAMP_NONE,
                         VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
                         VK_FALSE,
                         NULL,
                         &r->samplers[index]);
    }
    cvkCreateSingleTypeDescriptorPool(r->device, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, (VIDEO_RENDERER_FRAMES * 2) + 1, NULL, 0, VIDEO_RENDERER_FRAMES + 1, NULL, &r->descriptors);
    for (index = 0; index < VIDEO_RENDERER_FRAMES; ++index)
    {
        cvkAllocateDescriptorSet(r->device, NULL, r->descriptors, r->setLayouts[0], &r->frames[index]);
    }
    cvkAllocateDescriptorSet(r->device, NULL, r->descriptors, r->setLayouts[1], &r->scale);
    CreatePipeline(r, 0, nv12_frag, sizeof(nv12_frag));
    CreatePipeline(r, 1, bicubic_frag, sizeof(bicubic_frag));
}

extern FORCE_INLINE VideoRenderer* VideoRenderer_Create(ID3D11Device* device, ID3D11DeviceContext* context)
{
    VideoRenderer* renderer = (VideoRenderer*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*renderer));
    if (!renderer)
    {
        Fail(TEXT("Cannot allocate the video renderer."));
    }
    renderer->d3d     = device;
    renderer->context = context;
    CreatePlaneCopyShaders(renderer);
    CreateDevice(renderer);
    CreatePasses(renderer);
    return renderer;
}

static FORCE_INLINE void WriteImage(VideoRenderer* r, VkDescriptorSet set, uint32_t binding, VkImageView view, VkSampler sampler)
{
    cvkUpdateCombinedImageSamplerDescriptorSet(r->device, sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, NULL, set, binding, 0);
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
    cdx11DeviceCreateTexture2DShaderResourceView(renderer->d3d,
                                                  (ID3D11Resource*)frame->texture,
                                                  0,
                                                  1,
                                                  dxgi_format,
                                                  view);
    cdx11DeviceCreateTexture2DRenderTargetView(renderer->d3d,
                                                (ID3D11Resource*)plane->texture,
                                                0,
                                                dxgi_format,
                                                target);
    WriteImage(renderer, descriptor, binding, plane->views[0], renderer->samplers[0]);
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
    cdx11DeviceCreateTexture2D(renderer->d3d,
                               width,
                               height,
                               1,
                               1,
                               DXGI_FORMAT_NV12,
                               sample,
                               D3D11_USAGE_DEFAULT,
                               D3D11_BIND_SHADER_RESOURCE,
                               0,
                               0,
                               NULL,
                               &frame->texture);
    CreateFramePlane(renderer, frame, &frame->planes[0], &frame->views[0], &frame->targets[0],
                     descriptor, 0, width, height, VK_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UNORM);
    CreateFramePlane(renderer, frame, &frame->planes[1], &frame->views[1], &frame->targets[1],
                     descriptor, 1, width >> 1, height >> 1, VK_FORMAT_R8G8_UNORM, DXGI_FORMAT_R8G8_UNORM);
}

void VideoRenderer_SetSize(VideoRenderer* r, UINT width, UINT height)
{
    VkExtent2D extent = {width, height};
    CVK_ASSERT(width && height && !(width & 1) && !(height & 1));
    cvkDeviceWaitIdle(r->device);
    DestroyImage(r, &r->converted);
    RecreateFrame(r, &r->ring[0], r->frames[0], width, height);
    RecreateFrame(r, &r->ring[1], r->frames[1], width, height);
    r->converted.width  = width;
    r->converted.height = height;
    cvkSetupDeviceLocalImage2D(r->physical,
                               r->device,
                               NULL,
                               0,
                               VK_FORMAT_B8G8R8A8_UNORM,
                               1,
                               1,
                               VK_SAMPLE_COUNT_1_BIT,
                               VK_IMAGE_TILING_OPTIMAL,
                               VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                               VK_SHARING_MODE_EXCLUSIVE,
                               0,
                               NULL,
                               VK_IMAGE_LAYOUT_UNDEFINED,
                               extent,
                               NULL,
                               NULL,
                               &r->converted.image,
                               &r->converted.memory);
    CreateView(r, &r->converted, 0, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_ASPECT_COLOR_BIT);
    CreateFramebuffer(r, &r->converted);
    WriteImage(r, r->scale, 0, r->converted.views[0], r->samplers[1]);
    r->convertedInitialized = FALSE;
    r->outputWidth = r->outputHeight = 0;
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

/*************************************************************************************************/
/**
 * \par Details:
 *  Draws one selected upload plane through its already resolved views.
 */
static FORCE_INLINE void DrawUploadPlane(VideoRenderer*               renderer,
                                         const Image*                  image,
                                         ID3D11ShaderResourceView**   view,
                                         ID3D11RenderTargetView**     target)
{
    D3D11_VIEWPORT viewport = {0, 0, (FLOAT)image->width, (FLOAT)image->height, 0, 1};

    cdx11DeviceContextRSSetViewports(renderer->context, 1, &viewport);
    cdx11DeviceContextPSSetShaderResources(renderer->context, 0, 1, view);
    cdx11DeviceContextOMSetRenderTargets(renderer->context, 1, target, NULL);
    cdx11DeviceContextDraw(renderer->context, 3, 0);
}

void VideoRenderer_EndUpload(VideoRenderer* r, int slot)
{
    Frame*                    frame = &r->ring[FrameIndex(slot)];
    ID3D11ShaderResourceView* none  = NULL;
    cdx11DeviceContextIASetInputLayout(r->context, NULL);
    cdx11DeviceContextIASetPrimitiveTopology(r->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cdx11DeviceContextVSSetShader(r->context, r->copyVertex, NULL, 0);
    cdx11DeviceContextGSSetShader(r->context, NULL, NULL, 0);
    cdx11DeviceContextPSSetShader(r->context, r->copyPixel, NULL, 0);
    cdx11DeviceContextRSSetState(r->context, NULL);
    cdx11DeviceContextOMSetBlendState(r->context, NULL, NULL, UINT_MAX);
    cdx11DeviceContextOMSetDepthStencilState(r->context, NULL, 0);
    DrawUploadPlane(r, &frame->planes[0], &frame->views[0], &frame->targets[0]);
    DrawUploadPlane(r, &frame->planes[1], &frame->views[1], &frame->targets[1]);
    cdx11DeviceContextPSSetShaderResources(r->context, 0, 1, &none);
    cdx11DeviceContextOMSetRenderTargets(r->context, 0, NULL, NULL);
    cdxgiKeyedMutexReleaseSync(frame->planes[0].mutex, 0);
    cdxgiKeyedMutexReleaseSync(frame->planes[1].mutex, 0);
    cdx11DeviceContextFlush(r->context);
}

static void RenderPass(VideoRenderer* r, Image* target, int pass, VkDescriptorSet set, UINT width, UINT height)
{
    VkRect2D area        = {{0, 0}, {target->width, target->height}};
    float    constants[] = {1.0F / (float)width, 1.0F / (float)height, 1.0F, 0.0F, (float)target->width / (float)width, (float)target->height / (float)height};
    cvkCmdBeginRenderPass(r->command, NULL, r->renderPass, target->framebuffer, area, 0, NULL, VK_SUBPASS_CONTENTS_INLINE);
    cvkCmdSetViewportAndScissor2D(r->command, area.extent);
    if (pass)
    {
        cvkCmdDrawGraphicsVertices(r->command, r->pipelines[pass], r->layouts[pass], 0, 1, &set, 0, NULL, 4, 1, 0, 0, 0, sizeof(constants), constants);
    }
    else
    {
        cvkCmdBindGraphicsPipeline(r->command, r->pipelines[pass]);
        cvkCmdBindGraphicsDescriptorSets(r->command, r->layouts[pass], 0, 1, &set, 0, NULL);
        cvkCmdDraw(r->command, 4, 1, 0, 0);
    }
    cvkCmdEndRenderPass(r->command);
}

void VideoRenderer_Draw(VideoRenderer* r, ID3D11RenderTargetView* target, int slot, BOOL convert, UINT width, UINT height)
{
    uint32_t             frame_index;
    VkResult             result;
    ID3D11Resource*      backBuffer;
    ID3D11Texture2D*     texture;
    D3D11_TEXTURE2D_DESC desc;
    VkDeviceMemory       syncs[3];
    uint64_t             acquireKeys[3] = {0, 0, 0};
    uint64_t             releaseKeys[3] = {1, 0, 0};
    uint32_t             timeouts[3]    = {INFINITE, INFINITE, INFINITE};
    uint32_t             count          = convert ? 3 : 1;
    BOOL                 redraw;

    frame_index = FrameIndex(slot);
    cdx11ViewGetResource((ID3D11View*)target, &backBuffer);
    cdxgiUnknownQueryInterface((IUnknown*)backBuffer, &IID_ID3D11Texture2D, (void**)&texture);
    cdx11Texture2DGetDesc(texture, &desc);
    cdxgiUnknownRelease((IUnknown*)texture);
    CVK_ASSERT(width <= desc.Width && height <= desc.Height);
    if (r->output.width != desc.Width || r->output.height != desc.Height)
    {
        cvkDeviceWaitIdle(r->device);
        DestroyImage(r, &r->output);
        CreateSharedImage(r, &r->output, desc.Width, desc.Height, VK_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, TRUE);
        r->outputWidth = r->outputHeight = 0;
    }
    /* Hover, activation and the old-geometry resize latch often repaint the
    ** same video at the same size. Keep the scaled pixels as well as the NV12
    ** conversion, so these paints only copy and draw the caption. */
    redraw = convert || width != r->outputWidth || height != r->outputHeight;
    if (redraw)
    {
        cvkResetCommandBuffer(r->command, 0);
        cvkBeginOneTimeCommandBuffer(r->command, NULL);
        if (convert)
        {
            cvkCmdAcquireImageFromExternal(r->command,
                                           NULL,
                                           VK_ACCESS_SHADER_READ_BIT,
                                           VK_IMAGE_LAYOUT_GENERAL,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                           r->family,
                                           r->ring[frame_index].planes[0].image,
                                           colorRange,
                                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                           0);
            cvkCmdAcquireImageFromExternal(r->command,
                                           NULL,
                                           VK_ACCESS_SHADER_READ_BIT,
                                           VK_IMAGE_LAYOUT_GENERAL,
                                           VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                           r->family,
                                           r->ring[frame_index].planes[1].image,
                                           colorRange,
                                           VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                           0);
            if (r->convertedInitialized)
            {
                cvkCmdImageBarrier(r->command,
                                   NULL,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   r->converted.image,
                                   colorRange,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   0);
            }
            else
            {
                cvkCmdImageBarrier(r->command,
                                   NULL,
                                   0,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_IMAGE_LAYOUT_UNDEFINED,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   r->converted.image,
                                   colorRange,
                                   VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   0);
            }
            RenderPass(r, &r->converted, 0, r->frames[frame_index], r->converted.width, r->converted.height);
            cvkCmdImageBarrier(r->command,
                               NULL,
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                               VK_ACCESS_SHADER_READ_BIT,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                               r->converted.image,
                               colorRange,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                               0);
            cvkCmdReleaseImageToExternal(r->command,
                                         NULL,
                                         VK_ACCESS_SHADER_READ_BIT,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                         VK_IMAGE_LAYOUT_GENERAL,
                                         r->family,
                                         r->ring[frame_index].planes[0].image,
                                         colorRange,
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                         0);
            cvkCmdReleaseImageToExternal(r->command,
                                         NULL,
                                         VK_ACCESS_SHADER_READ_BIT,
                                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                         VK_IMAGE_LAYOUT_GENERAL,
                                         r->family,
                                         r->ring[frame_index].planes[1].image,
                                         colorRange,
                                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                         0);
            r->convertedInitialized = TRUE;
        }
        cvkCmdAcquireImageFromExternal(r->command,
                                       NULL,
                                       VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       VK_IMAGE_LAYOUT_GENERAL,
                                       VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                       r->family,
                                       r->output.image,
                                       colorRange,
                                       VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                       0);
        RenderPass(r, &r->output, 1, r->scale, width, height);
        cvkCmdReleaseImageToExternal(r->command,
                                     NULL,
                                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                     VK_IMAGE_LAYOUT_GENERAL,
                                     r->family,
                                     r->output.image,
                                     colorRange,
                                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                     0);
        cvkEndCommandBuffer(r->command);
        syncs[0] = r->output.memory;
        syncs[1] = r->ring[frame_index].planes[0].memory;
        syncs[2] = r->ring[frame_index].planes[1].memory;
        cvkResetFence(r->device, r->fence);
        cvkQueueSubmitInfoWithWin32KeyedMutexAcquireReleaseInfo(r->queue, NULL, count, syncs, acquireKeys, timeouts, count, syncs, releaseKeys, 0, NULL, NULL, 1, &r->command, 0, NULL, r->fence);
        r->outputWidth  = width;
        r->outputHeight = height;
    }
    /* The copy precedes the original Direct2D caption and DXGI presentation. */
    cdxgiKeyedMutexAcquireSync(r->output.mutex, redraw ? 1 : 0, INFINITE);
    /* Cover the entire composition buffer, including pixels a growing window
    ** can expose before its geometry paint reaches DWM. The shader preserves
    ** the exact client-area mapping and clamps the extra area to video edges.
    ** A cropped copy leaves undefined pixels in a flip-discard back buffer. */
    cdx11DeviceContextCopyResource(r->context, backBuffer, (ID3D11Resource*)r->output.texture);
    cdxgiKeyedMutexReleaseSync(r->output.mutex, 0);
    cdx11DeviceContextFlush(r->context);
    cdxgiUnknownRelease((IUnknown*)backBuffer);
    if (redraw)
    {
        result = cvkWaitForFenceIndefinitely(r->device, r->fence);
        CVK_ASSERT(VK_SUCCESS == result);
    }
}

void VideoRenderer_CopyFrame(VideoRenderer* r, ID3D11Texture2D* target, UINT subresource, UINT x, UINT y, int slot)
{
    uint32_t       frame_index;
    VkResult       result;
    VkDeviceMemory syncs[3];
    uint64_t       acquireKeys[3] = {0, 0, 0};
    uint64_t       releaseKeys[3] = {1, 0, 0};
    uint32_t       timeouts[3]    = {INFINITE, INFINITE, INFINITE};
    UINT           width          = r->converted.width;
    UINT           height         = r->converted.height;
    D3D11_BOX      source         = {0, 0, 0, width, height, 1};

    frame_index = FrameIndex(slot);
    if (r->output.width != width || r->output.height != height)
    {
        cvkDeviceWaitIdle(r->device);
        DestroyImage(r, &r->output);
        CreateSharedImage(r, &r->output, width, height, VK_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM, TRUE);
    }
    cvkResetCommandBuffer(r->command, 0);
    cvkBeginOneTimeCommandBuffer(r->command, NULL);
    cvkCmdAcquireImageFromExternal(r->command,
                                   NULL,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_IMAGE_LAYOUT_GENERAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   r->family,
                                   r->ring[frame_index].planes[0].image,
                                   colorRange,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   0);
    cvkCmdAcquireImageFromExternal(r->command,
                                   NULL,
                                   VK_ACCESS_SHADER_READ_BIT,
                                   VK_IMAGE_LAYOUT_GENERAL,
                                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                   r->family,
                                   r->ring[frame_index].planes[1].image,
                                   colorRange,
                                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                   0);
    cvkCmdAcquireImageFromExternal(r->command,
                                   NULL,
                                   VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                   VK_IMAGE_LAYOUT_GENERAL,
                                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                   r->family,
                                   r->output.image,
                                   colorRange,
                                   VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                   0);
    RenderPass(r, &r->output, 0, r->frames[frame_index], width, height);
    cvkCmdReleaseImageToExternal(r->command,
                                 NULL,
                                 VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                 VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                 VK_IMAGE_LAYOUT_GENERAL,
                                 r->family,
                                 r->output.image,
                                 colorRange,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 0);
    cvkCmdReleaseImageToExternal(r->command,
                                 NULL,
                                 VK_ACCESS_SHADER_READ_BIT,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                 VK_IMAGE_LAYOUT_GENERAL,
                                 r->family,
                                 r->ring[frame_index].planes[0].image,
                                 colorRange,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0);
    cvkCmdReleaseImageToExternal(r->command,
                                 NULL,
                                 VK_ACCESS_SHADER_READ_BIT,
                                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                 VK_IMAGE_LAYOUT_GENERAL,
                                 r->family,
                                 r->ring[frame_index].planes[1].image,
                                 colorRange,
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                                 0);
    cvkEndCommandBuffer(r->command);
    syncs[0] = r->output.memory;
    syncs[1] = r->ring[frame_index].planes[0].memory;
    syncs[2] = r->ring[frame_index].planes[1].memory;
    cvkResetFence(r->device, r->fence);
    cvkQueueSubmitInfoWithWin32KeyedMutexAcquireReleaseInfo(r->queue, NULL, 3, syncs, acquireKeys, timeouts, 3, syncs, releaseKeys, 0, NULL, NULL, 1, &r->command, 0, NULL, r->fence);
    cdxgiKeyedMutexAcquireSync(r->output.mutex, 1, INFINITE);
    cdx11DeviceContextCopySubresourceRegion(r->context, (ID3D11Resource*)target, subresource, x, y, 0, (ID3D11Resource*)r->output.texture, 0, &source);
    cdxgiKeyedMutexReleaseSync(r->output.mutex, 0);
    cdx11DeviceContextFlush(r->context);
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
    for (index = 0; index < 2; ++index)
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
