/* The original demo's two video passes, expressed through cvk. D3D11 is only
** the Media Foundation / DirectComposition bridge. Shared textures stay on the
** same adapter; keyed mutexes and external queue ownership protect both APIs. */
#include "renderer.h"
#include "cdx11.h"
#include "cdxgi.h"
#include <d3dcompiler.h>
#include <volk.h>
#include "win32/cvk_win32.h"
#include <stdio.h>
#include <string.h>

#include "fullscreen_vert.h"
#include "nv12_frag.h"
#include "bicubic_frag.h"

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
    Image                    planes[2];
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
    ID3D11VertexShader*    copyVertex;
    ID3D11PixelShader*     copyPixel;
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

static void Fail(const char* message)
{
    fprintf(stderr, "ImmersiveWindowDemo: %s\n", message);
    OutputDebugStringA(message);
    MessageBoxA(NULL, message, "ImmersiveWindowDemo", MB_ICONERROR);
    ExitProcess(1);
}

#ifdef CVK_DEMO_VALIDATION
static VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    (void)severity;
    (void)type;
    (void)user;
    fprintf(stderr, "%s\n", data->pMessage);
    OutputDebugStringA(data->pMessage);
    return VK_FALSE;
}
#endif

static void CreateDevice(VideoRenderer* r)
{
    VkApplicationInfo                              app;
    VkPhysicalDevice                               devices[CVK_MAX_PHYSICAL_DEVICES];
    VkQueueFamilyProperties                        families[CVK_MAX_QUEUE_FAMILY_PROPERTIES];
    VkPhysicalDeviceFeatures                       features = {0};
    VkPhysicalDeviceProperties2                    properties = {0};
    VkDeviceQueueCreateInfo                        queue;
    VkExtensionProperties                          extensions[CVK_MAX_PROPERTIES];
    const char*                                    required[] = {VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME, VK_KHR_WIN32_KEYED_MUTEX_EXTENSION_NAME};
    IDXGIDevice*                                   dxgi;
    IDXGIAdapter*                                  adapter;
    DXGI_ADAPTER_DESC                              desc;
    uint32_t                                       count, i, j, extensionCount;
    float                                          priority = 1.0f;
#ifdef CVK_DEMO_VALIDATION
    const char* layers[]             = {"VK_LAYER_KHRONOS_validation"};
    const char* instanceExtensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};
#endif

    if (VK_SUCCESS != volkInitialize())
        Fail("A Vulkan 1.1 driver is required.");
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
    count = cvkEnumeratePhysicalDevices(r->instance, devices);
    for (i = 0; i < count; ++i)
    {
        VkPhysicalDeviceIDProperties id;
        cvkInitPhysicalDeviceIDProperties(&id, NULL);
        cvkGetPhysicalDeviceProperties2(devices[i], &id, &properties);
        if (id.deviceLUIDValid && 0 == memcmp(id.deviceLUID, &desc.AdapterLuid, VK_LUID_SIZE))
        {
            r->physical = devices[i];
            break;
        }
    }
    if (!r->physical || properties.properties.apiVersion < VK_API_VERSION_1_1)
        Fail("The window's graphics adapter must support Vulkan 1.1.");
    extensionCount = cvkEnumerateDeviceExtensionProperties(r->physical, NULL, extensions);
    for (i = 0; i < VK_COUNT_OF(required); ++i)
    {
        for (j = 0; j < extensionCount; ++j)
            if (0 == strcmp(required[i], extensions[j].extensionName))
                break;
        if (j == extensionCount)
            Fail(required[i]);
    }
    count = cvkGetPhysicalDeviceQueueFamilyProperties(r->physical, families);
    for (i = 0; i < count; ++i)
        if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            break;
    if (i == count)
        Fail("No Vulkan graphics queue on the window's adapter.");
    r->family = i;
    cvkGetPhysicalDeviceFeatures(r->physical, &features);
    if (!features.samplerAnisotropy || properties.properties.limits.maxSamplerAnisotropy < 16.0f)
        Fail("The reference video sampler requires 16x anisotropic filtering.");
    ZeroMemory(&features, sizeof(features));
    features.samplerAnisotropy = VK_TRUE;
    cvkInitDeviceQueueCreateInfo(&queue, NULL, 0, r->family, 1, &priority);
    cvkCreateDevice(r->physical, NULL, 0, 1, &queue, 0, NULL, VK_COUNT_OF(required), required, &features, NULL, &r->device);
    volkLoadDevice(r->device);
    cvkGetDeviceQueue(r->device, r->family, 0, &r->queue);
    cvkCreateCommandPool(r->device, NULL, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, r->family, NULL, &r->pool);
    cvkAllocateCommandBuffers(r->device, NULL, r->pool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1, &r->command);
    cvkCreateFence(r->device, NULL, 0, NULL, &r->fence);
}

static void CreateView(VideoRenderer* r, Image* image, int index, VkFormat format, VkImageAspectFlags aspect)
{
    VkImageSubresourceRange range = {aspect, 0, 1, 0, 1};
    cvkCreateImageView2D(r->device, NULL, 0, image->image, format, range, NULL, &image->views[index]);
}

static void CreateFramebuffer(VideoRenderer* r, Image* image)
{
    cvkCreateFramebuffer(r->device, NULL, 0, r->renderPass, 1, image->views, image->width, image->height, 1, NULL, &image->framebuffer);
}

/* Share single-plane textures. Importing the decoder's multi-planar NV12 layout
** can corrupt chroma or lose the device on the native NVIDIA driver. */
static void CreateSharedImage(VideoRenderer* r, Image* image, UINT width, UINT height, VkFormat format, DXGI_FORMAT d3dFormat, BOOL output)
{
    VkImageUsageFlags                usage  = output ? VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT : VK_IMAGE_USAGE_SAMPLED_BIT;
    VkImageCreateFlags               flags  = 0;
    VkExtent3D                       extent = {width, height, 1};
    VkMemoryRequirements             requirements;
    VkMemoryWin32HandlePropertiesKHR handleProperties;
    VkImportMemoryWin32HandleInfoKHR import;
    VkMemoryDedicatedAllocateInfo    dedicated;
    VkBool32                         dedicatedOnly;
    uint32_t                         memoryType;
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
        Fail("The Vulkan driver cannot import the shared video plane or output texture.");
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
    cvkCreateImageWithExternalMemoryInfo(r->device,
                                         NULL,
                                         VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT,
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
                                         NULL,
                                         &image->image);
    cvkGetImageMemoryRequirements(r->device, image->image, &requirements);
    cvkGetMemoryWin32HandleProperties(r->device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, handle, NULL, &handleProperties);
    memoryType = cvkGetPhysicalDeviceCompatibleMemoryTypeIndex(r->physical, requirements.memoryTypeBits & handleProperties.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    CVK_ASSERT(VK_INVALID != memoryType);
    cvkInitImportMemoryWin32HandleInfo(&import, NULL, VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT, handle, NULL);
    cvkInitMemoryDedicatedAllocateInfo(&dedicated, &import, image->image, VK_NULL_HANDLE);
    cvkAllocateMemory(r->device, &dedicated, requirements.size, memoryType, NULL, &image->memory);
    CloseHandle(handle);
    cvkBindImageMemory(r->device, image->image, image->memory, 0);
    CreateView(r, image, 0, format, VK_IMAGE_ASPECT_COLOR_BIT);
    if (output)
    {
        CreateFramebuffer(r, image);
    }
}

static void DestroyImage(VideoRenderer* r, Image* image)
{
    /* Vulkan idle alone does not wait for D3D's queued copies. */
    if (image->mutex)
    {
        cdxgiKeyedMutexAcquireSync(image->mutex, 0, INFINITE);
        cdxgiKeyedMutexReleaseSync(image->mutex, 0);
    }
    if (image->framebuffer)
        cvkDestroyFramebuffer(r->device, image->framebuffer, NULL);
    if (image->views[0])
        cvkDestroyImageView(r->device, image->views[0], NULL);
    if (image->views[1])
        cvkDestroyImageView(r->device, image->views[1], NULL);
    if (image->image)
        cvkDestroyImage(r->device, image->image, NULL);
    if (image->memory)
        cvkFreeMemory(r->device, image->memory, NULL);
    if (image->mutex)
        cdxgiUnknownRelease((IUnknown*)image->mutex);
    if (image->texture)
        cdxgiUnknownRelease((IUnknown*)image->texture);
    ZeroMemory(image, sizeof(*image));
}

static void DestroyFrame(VideoRenderer* r, Frame* frame)
{
    int i;
    for (i = 0; i < 2; ++i)
    {
        if (frame->targets[i])
            cdxgiUnknownRelease((IUnknown*)frame->targets[i]);
        if (frame->views[i])
            cdxgiUnknownRelease((IUnknown*)frame->views[i]);
        DestroyImage(r, &frame->planes[i]);
    }
    if (frame->texture)
        cdxgiUnknownRelease((IUnknown*)frame->texture);
    ZeroMemory(frame, sizeof(*frame));
}

/* Copy each NV12 plane without filtering or color conversion. D3D resolves its
** native plane layout; Vulkan receives ordinary R8/RG8 images. No CPU readback. */
static void CreatePlaneCopyShaders(VideoRenderer* r)
{
    static const char source[] =
        "float4 VS(uint id : SV_VertexID) : SV_Position {"
        "float2 p = float2((id << 1) & 2, id & 2);"
        "return float4(p * float2(2, -2) + float2(-1, 1), 0, 1); }"
        "Texture2D<float2> plane : register(t0);"
        "float4 PS(float4 p : SV_Position) : SV_Target {"
        "return float4(plane.Load(int3(int2(p.xy), 0)), 0, 1); }";
    ID3DBlob* code;
    if (FAILED(D3DCompile(source, sizeof(source) - 1, NULL, NULL, NULL, "VS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, NULL)))
        Fail("Cannot compile the video plane copy vertex shader.");
    cdx11DeviceCreateVertexShader(r->d3d, code->lpVtbl->GetBufferPointer(code), code->lpVtbl->GetBufferSize(code), NULL, &r->copyVertex);
    cdxgiUnknownRelease((IUnknown*)code);
    if (FAILED(D3DCompile(source, sizeof(source) - 1, NULL, NULL, NULL, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, NULL)))
        Fail("Cannot compile the video plane copy pixel shader.");
    cdx11DeviceCreatePixelShader(r->d3d, code->lpVtbl->GetBufferPointer(code), code->lpVtbl->GetBufferSize(code), NULL, &r->copyPixel);
    cdxgiUnknownRelease((IUnknown*)code);
}

static void CreatePipeline(VideoRenderer* r, int pass, const uint32_t* fragment, size_t size)
{
    VkShaderModule                         vertex, pixel;
    VkPipelineShaderStageCreateInfo        stages[2]       = {{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT, .pName = "main"},
                                                              {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .pName = "main"}};
    VkPipelineVertexInputStateCreateInfo   input           = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo assembly        = {.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO, .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP};
    VkPipelineViewportStateCreateInfo      viewport        = {.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, .viewportCount = 1, .scissorCount = 1};
    VkPipelineRasterizationStateCreateInfo raster          = {.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                              .polygonMode = VK_POLYGON_MODE_FILL,
                                                              .cullMode    = VK_CULL_MODE_NONE,
                                                              .frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                              .lineWidth   = 1.0f};
    VkPipelineMultisampleStateCreateInfo   multisample     = {.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO, .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT};
    VkPipelineColorBlendAttachmentState    attachment      = {.colorWriteMask = 15};
    VkPipelineColorBlendStateCreateInfo    blend           = {.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO, .attachmentCount = 1, .pAttachments = &attachment};
    VkDynamicState                         dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo       dynamic         = {.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO, .dynamicStateCount = 2, .pDynamicStates = dynamicStates};
    VkGraphicsPipelineCreateInfo           pipeline        = {.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
                                                              .stageCount          = 2,
                                                              .pStages             = stages,
                                                              .pVertexInputState   = &input,
                                                              .pInputAssemblyState = &assembly,
                                                              .pViewportState      = &viewport,
                                                              .pRasterizationState = &raster,
                                                              .pMultisampleState   = &multisample,
                                                              .pColorBlendState    = &blend,
                                                              .pDynamicState       = &dynamic,
                                                              .basePipelineIndex   = -1};

    cvkCreateShaderModule(r->device, NULL, 0, sizeof(fullscreen_vert), fullscreen_vert, NULL, &vertex);
    cvkCreateShaderModule(r->device, NULL, 0, size, fragment, NULL, &pixel);
    stages[0].module    = vertex;
    stages[1].module    = pixel;
    pipeline.layout     = r->layouts[pass];
    pipeline.renderPass = r->renderPass;
    CVK_CHECK(cvkCreateGraphicsPipelines(r->device, VK_NULL_HANDLE, 1, &pipeline, NULL, &r->pipelines[pass]));
    cvkDestroyShaderModule(r->device, pixel, NULL);
    cvkDestroyShaderModule(r->device, vertex, NULL);
}

static void CreatePasses(VideoRenderer* r)
{
    VkAttachmentDescription      attachment  = {.format         = VK_FORMAT_B8G8R8A8_UNORM,
                                                .samples        = VK_SAMPLE_COUNT_1_BIT,
                                                .loadOp         = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                                                .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                .initialLayout  = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                                .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference        reference   = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription         subpass     = {.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS, .colorAttachmentCount = 1, .pColorAttachments = &reference};
    VkDescriptorSetLayoutBinding bindings[2] = {{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL},
                                                {1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL}};
    VkPushConstantRange          constants   = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 6 * sizeof(float)};
    VkDescriptorPoolSize         poolSize    = {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VIDEO_RENDERER_FRAMES * 2 + 1};
    int                          i;

    cvkCreateRenderPass(r->device, NULL, 0, 1, &attachment, 1, &subpass, 0, NULL, NULL, &r->renderPass);
    for (i = 0; i < 2; ++i)
    {
        cvkCreateDescriptorSetLayout(r->device, NULL, 0, i ? 1 : 2, bindings, NULL, &r->setLayouts[i]);
        cvkCreatePipelineLayout(r->device, NULL, 0, 1, &r->setLayouts[i], i ? 1 : 0, i ? &constants : NULL, NULL, &r->layouts[i]);
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
                         i ? VK_TRUE : VK_FALSE,
                         i ? 16.0f : 1.0f,
                         VK_FALSE,
                         VK_COMPARE_OP_NEVER,
                         0,
                         VK_LOD_CLAMP_NONE,
                         VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,
                         VK_FALSE,
                         NULL,
                         &r->samplers[i]);
    }
    cvkCreateDescriptorPool(r->device, NULL, 0, VIDEO_RENDERER_FRAMES + 1, 1, &poolSize, NULL, &r->descriptors);
    for (i = 0; i < VIDEO_RENDERER_FRAMES; ++i)
        cvkAllocateDescriptorSet(r->device, NULL, r->descriptors, r->setLayouts[0], &r->frames[i]);
    cvkAllocateDescriptorSet(r->device, NULL, r->descriptors, r->setLayouts[1], &r->scale);
    CreatePipeline(r, 0, nv12_frag, sizeof(nv12_frag));
    CreatePipeline(r, 1, bicubic_frag, sizeof(bicubic_frag));
}

VideoRenderer* VideoRenderer_Create(ID3D11Device* device, ID3D11DeviceContext* context)
{
    VideoRenderer* r = (VideoRenderer*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*r));
    if (!r)
        Fail("Cannot allocate the video renderer.");
    r->d3d     = device;
    r->context = context;
    CreatePlaneCopyShaders(r);
    CreateDevice(r);
    CreatePasses(r);
    return r;
}

static void WriteImage(VideoRenderer* r, VkDescriptorSet set, uint32_t binding, VkImageView view, VkSampler sampler)
{
    VkDescriptorImageInfo info = {sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet  write =
        {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = binding, .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &info};
    cvkUpdateDescriptorSets(r->device, 1, &write, 0, NULL);
}

void VideoRenderer_SetSize(VideoRenderer* r, UINT width, UINT height)
{
    VkExtent3D extent = {width, height, 1};
    int        i;
    CVK_ASSERT(width && height && !(width & 1) && !(height & 1));
    cvkDeviceWaitIdle(r->device);
    DestroyImage(r, &r->converted);
    for (i = 0; i < VIDEO_RENDERER_FRAMES; ++i)
    {
        Frame* frame = &r->ring[i];
        DXGI_SAMPLE_DESC sample = {1, 0};
        int plane;
        DestroyFrame(r, frame);
        cdx11DeviceCreateTexture2D(r->d3d, width, height, 1, 1, DXGI_FORMAT_NV12, sample,
            D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, NULL, &frame->texture);
        for (plane = 0; plane < 2; ++plane)
        {
            DXGI_FORMAT format = plane ? DXGI_FORMAT_R8G8_UNORM : DXGI_FORMAT_R8_UNORM;
            CreateSharedImage(r, &frame->planes[plane], width >> plane, height >> plane,
                plane ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8_UNORM, format, FALSE);
            cdx11DeviceCreateTexture2DShaderResourceView(r->d3d, (ID3D11Resource*)frame->texture, 0, 1, format, &frame->views[plane]);
            cdx11DeviceCreateTexture2DRenderTargetView(r->d3d, (ID3D11Resource*)frame->planes[plane].texture, 0, format, &frame->targets[plane]);
            WriteImage(r, r->frames[i], (uint32_t)plane, frame->planes[plane].views[0], r->samplers[0]);
        }
    }
    r->converted.width  = width;
    r->converted.height = height;
    cvkSetupImage(r->physical,
                  r->device,
                  NULL,
                  0,
                  VK_IMAGE_TYPE_2D,
                  VK_FORMAT_B8G8R8A8_UNORM,
                  extent,
                  1,
                  1,
                  VK_SAMPLE_COUNT_1_BIT,
                  VK_IMAGE_TILING_OPTIMAL,
                  VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
                  VK_SHARING_MODE_EXCLUSIVE,
                  0,
                  NULL,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
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

ID3D11Texture2D* VideoRenderer_GetFrame(VideoRenderer* r, int slot)
{
    CVK_ASSERT(slot >= 0 && slot < VIDEO_RENDERER_FRAMES);
    return r->ring[slot].texture;
}

void VideoRenderer_BeginUpload(VideoRenderer* r, int slot)
{
    CVK_ASSERT(slot >= 0 && slot < VIDEO_RENDERER_FRAMES);
    cdxgiKeyedMutexAcquireSync(r->ring[slot].planes[0].mutex, 0, INFINITE);
    cdxgiKeyedMutexAcquireSync(r->ring[slot].planes[1].mutex, 0, INFINITE);
}

void VideoRenderer_EndUpload(VideoRenderer* r, int slot)
{
    Frame* frame = &r->ring[slot];
    ID3D11ShaderResourceView* none = NULL;
    int plane;
    cdx11DeviceContextIASetInputLayout(r->context, NULL);
    cdx11DeviceContextIASetPrimitiveTopology(r->context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cdx11DeviceContextVSSetShader(r->context, r->copyVertex, NULL, 0);
    cdx11DeviceContextGSSetShader(r->context, NULL, NULL, 0);
    cdx11DeviceContextPSSetShader(r->context, r->copyPixel, NULL, 0);
    cdx11DeviceContextRSSetState(r->context, NULL);
    cdx11DeviceContextOMSetBlendState(r->context, NULL, NULL, UINT_MAX);
    cdx11DeviceContextOMSetDepthStencilState(r->context, NULL, 0);
    for (plane = 0; plane < 2; ++plane)
    {
        D3D11_VIEWPORT viewport = {0, 0, (FLOAT)frame->planes[plane].width, (FLOAT)frame->planes[plane].height, 0, 1};
        cdx11DeviceContextRSSetViewports(r->context, 1, &viewport);
        cdx11DeviceContextPSSetShaderResources(r->context, 0, 1, &frame->views[plane]);
        cdx11DeviceContextOMSetRenderTargets(r->context, 1, &frame->targets[plane], NULL);
        cdx11DeviceContextDraw(r->context, 3, 0);
    }
    cdx11DeviceContextPSSetShaderResources(r->context, 0, 1, &none);
    cdx11DeviceContextOMSetRenderTargets(r->context, 0, NULL, NULL);
    cdxgiKeyedMutexReleaseSync(frame->planes[0].mutex, 0);
    cdxgiKeyedMutexReleaseSync(frame->planes[1].mutex, 0);
    cdx11DeviceContextFlush(r->context);
}

static void AcquireImage(VideoRenderer* r, Image* image, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags stage)
{
    cvkCmdImageQueueOwnershipBarrier(r->command,
                                     NULL,
                                     0,
                                     access,
                                     VK_IMAGE_LAYOUT_GENERAL,
                                     layout,
                                     VK_QUEUE_FAMILY_EXTERNAL,
                                     r->family,
                                     image->image,
                                     colorRange,
                                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     stage,
                                     0);
}

static void ReleaseImage(VideoRenderer* r, Image* image, VkImageLayout layout, VkAccessFlags access, VkPipelineStageFlags stage)
{
    cvkCmdImageQueueOwnershipBarrier(r->command,
                                     NULL,
                                     access,
                                     0,
                                     layout,
                                     VK_IMAGE_LAYOUT_GENERAL,
                                     r->family,
                                     VK_QUEUE_FAMILY_EXTERNAL,
                                     image->image,
                                     colorRange,
                                     stage,
                                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                     0);
}

static void DrawPass(VideoRenderer* r, Image* target, int pass, VkDescriptorSet set, UINT width, UINT height)
{
    VkRect2D   area        = {{0, 0}, {target->width, target->height}};
    VkViewport viewport    = {0, 0, (float)target->width, (float)target->height, 0, 1};
    float      constants[] = {1.0f / (float)width, 1.0f / (float)height, 1.0f, 0.0f, (float)target->width / (float)width, (float)target->height / (float)height};
    cvkCmdBeginRenderPass(r->command, NULL, r->renderPass, target->framebuffer, area, 0, NULL, VK_SUBPASS_CONTENTS_INLINE);
    cvkCmdSetViewport(r->command, viewport);
    cvkCmdSetScissor(r->command, area);
    cvkCmdBindPipeline(r->command, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipelines[pass]);
    cvkCmdBindDescriptorSets(r->command, VK_PIPELINE_BIND_POINT_GRAPHICS, r->layouts[pass], 0, 1, &set, 0, NULL);
    if (pass)
        cvkCmdPushConstants(r->command, r->layouts[pass], VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(constants), constants);
    cvkCmdDraw(r->command, 4, 1, 0, 0);
    cvkCmdEndRenderPass(r->command);
}

void VideoRenderer_Draw(VideoRenderer* r, ID3D11RenderTargetView* target, int slot, BOOL convert, UINT width, UINT height)
{
    ID3D11Resource*      backBuffer;
    ID3D11Texture2D*     texture;
    D3D11_TEXTURE2D_DESC desc;
    VkDeviceMemory       syncs[3];
    uint64_t             acquireKeys[3] = {0, 0, 0};
    uint64_t             releaseKeys[3] = {1, 0, 0};
    uint32_t             timeouts[3]    = {INFINITE, INFINITE, INFINITE};
    uint32_t             count          = convert ? 3 : 1;
    BOOL                 redraw;

    CVK_ASSERT(slot >= 0 && slot < VIDEO_RENDERER_FRAMES);
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
        cvkBeginCommandBuffer(r->command, NULL, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL);
        if (convert)
        {
            AcquireImage(r, &r->ring[slot].planes[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            AcquireImage(r, &r->ring[slot].planes[1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            cvkCmdImageBarrier(r->command,
                               NULL,
                               r->convertedInitialized ? VK_ACCESS_SHADER_READ_BIT : 0,
                               VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                               r->convertedInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED,
                               VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                               r->converted.image,
                               colorRange,
                               r->convertedInitialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                               VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                               0);
            DrawPass(r, &r->converted, 0, r->frames[slot], r->converted.width, r->converted.height);
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
            ReleaseImage(r, &r->ring[slot].planes[0], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            ReleaseImage(r, &r->ring[slot].planes[1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
            r->convertedInitialized = TRUE;
        }
        AcquireImage(r, &r->output, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        DrawPass(r, &r->output, 1, r->scale, width, height);
        ReleaseImage(r, &r->output, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        cvkEndCommandBuffer(r->command);
        syncs[0] = r->output.memory;
        syncs[1] = r->ring[slot].planes[0].memory;
        syncs[2] = r->ring[slot].planes[1].memory;
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
        CVK_CHECK(cvkWaitForFence(r->device, r->fence, VK_TRUE, UINT64_MAX));
}

void VideoRenderer_Destroy(VideoRenderer* r)
{
    int i;
    if (!r)
        return;
    cvkDeviceWaitIdle(r->device);
    cvkDestroyDescriptorPool(r->device, r->descriptors, NULL);
    DestroyImage(r, &r->output);
    DestroyImage(r, &r->converted);
    for (i = 0; i < VIDEO_RENDERER_FRAMES; ++i)
        DestroyFrame(r, &r->ring[i]);
    cdxgiUnknownRelease((IUnknown*)r->copyPixel);
    cdxgiUnknownRelease((IUnknown*)r->copyVertex);
    for (i = 0; i < 2; ++i)
    {
        cvkDestroyPipeline(r->device, r->pipelines[i], NULL);
        cvkDestroyPipelineLayout(r->device, r->layouts[i], NULL);
        cvkDestroyDescriptorSetLayout(r->device, r->setLayouts[i], NULL);
        cvkDestroySampler(r->device, r->samplers[i], NULL);
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
