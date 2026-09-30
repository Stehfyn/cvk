/* Parses tests/cube.pc.json (a Vulkan SC pipeline JSON) with cvkParsePipelineJson and checks the
** structures it yields; no device is needed. Run by CTest as cvk-pipeline-json-parse. */

#include "vk_diagnostic.h"
#include <volk.h>
#include "cvk/cvk_json.h"
#include "json_allocator.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                                                                                                                                              \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        if (!(expression))                                                                                                                                                                             \
        {                                                                                                                                                                                              \
            cvkDiagnosticWrite(__FILE__ ":");                                                                                                                                                          \
            cvkDiagnosticWriteDecimal(__LINE__);                                                                                                                                                       \
            cvkDiagnosticWrite(": check failed: " #expression "\n");                                                                                                                                   \
            return 1;                                                                                                                                                                                  \
        }                                                                                                                                                                                              \
    } while (0)

/* A real pointer-valued test handle; no integer-to-pointer encoding is used. */
struct VkCommandBuffer_T
{
    uint32_t marker;
};

typedef struct JsonHandleFixture
{
    VkCommandBuffer commandBuffer;
    uint32_t calls;
} JsonHandleFixture;

/**
 * \brief Resolves the fixture's command-buffer ID into its native handle type.
 * \param[in,out] pUser Fixture handle and callback count.
 * \param[in] pHandleType Requested Vulkan handle type.
 * \param[in] id JSON object ID.
 * \param[in] handleByteCount Extent of the native output type.
 * \param[out] pHandle Receives the native handle on success.
 * \retval VK_TRUE The fixture ID was resolved.
 * \retval VK_FALSE The ID is unknown; output remains unchanged.
 */
static CVK_FORCE_INLINE VkBool32 ResolveFixtureHandle(void* pUser,
                                                       const char* pHandleType,
                                                       uint64_t id,
                                                       uint32_t handleByteCount,
                                                       void* pHandle);

/**
 * \brief Checks native handle identity, repeated IDs, nulls and deferred references.
 * \retval VK_TRUE All handle and arena checks passed.
 * \retval VK_FALSE A contract check failed.
 */
static CVK_FORCE_INLINE VkBool32 CheckHandleReferences(void);

/**
 * \brief Checks iterative parsing well beyond ordinary native call-stack depth.
 * \retval VK_TRUE Every container link and scalar value is correct.
 * \retval VK_FALSE A parsed link or value differs from the fixture.
 */
static CVK_FORCE_INLINE VkBool32 CheckDeepDocument(void);

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static CVK_FORCE_INLINE VkBool32 ResolveFixtureHandle(void* pUser,
                                                       const char* pHandleType,
                                                       uint64_t id,
                                                       uint32_t handleByteCount,
                                                       void* pHandle)
{
    JsonHandleFixture* fixture;
    VkBool32 resolved;

    fixture = (JsonHandleFixture*)pUser;
    ++fixture->calls;
    resolved = 37 == id;
    if (resolved)
    {
        CVK_ASSERT(0 == strcmp(pHandleType, "VkCommandBuffer"));
        CVK_ASSERT(sizeof(VkCommandBuffer) == handleByteCount);
        *(VkCommandBuffer*)pHandle = fixture->commandBuffer;
    }
    return resolved;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The arena deliberately begins one byte into its backing storage.
 */
static CVK_FORCE_INLINE VkBool32 CheckHandleReferences(void)
{
    const char text[] = "{\"commandBufferCount\":4,\"pCommandBuffers\":[37,37,null,99]}";
    uint8_t storage[4097];
    cvkJsonValue values[16];
    cvkJsonHandleReference* references;
    cvkJsonContext context;
    cvkArena arena;
    VkSubmitInfo info;
    struct VkCommandBuffer_T command_buffer;
    JsonHandleFixture fixture;
    VkBool32 passed;
    uint64_t initial_size;

    command_buffer.marker       = 37;
    fixture.commandBuffer       = &command_buffer;
    fixture.calls               = 0;
    references                  = NULL;
    arena.pBase                 = storage + 1;
    arena.used                  = 0;
    arena.size                  = sizeof(storage) - 1;
    initial_size                = arena.size;
    context.document.pValues    = values;
    context.document.valueCount = cvkParseJsonDocument(text, sizeof(text) - 1, values, VK_COUNT_OF(values));
    context.pArena              = &arena;
    context.resolveHandle       = ResolveFixtureHandle;
    context.pUser               = &fixture;
    context.ppHandleReferences  = &references;
    cvkParseJsonSubmitInfo(&context, &values[0], &info);
    passed = initial_size == arena.size;
    passed = passed && (3 == fixture.calls);
    passed = passed && (4 == info.commandBufferCount);
    passed = passed && (fixture.commandBuffer == info.pCommandBuffers[0]);
    passed = passed && (fixture.commandBuffer == info.pCommandBuffers[1]);
    passed = passed && (VK_NULL_HANDLE == info.pCommandBuffers[2]);
    passed = passed && (VK_NULL_HANDLE == info.pCommandBuffers[3]);
    passed = passed && (37 == cvkGetJsonHandleId(references, (const void*)&info.pCommandBuffers[0]));
    passed = passed && (37 == cvkGetJsonHandleId(references, (const void*)&info.pCommandBuffers[1]));
    passed = passed && (0 == cvkGetJsonHandleId(references, (const void*)&info.pCommandBuffers[2]));
    passed = passed && (99 == cvkGetJsonHandleId(references, (const void*)&info.pCommandBuffers[3]));
    passed = passed && (0 == ((uintptr_t)info.pCommandBuffers % CVK_JSON_ARENA_ALIGNMENT));
    return passed;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Nodes must retain sibling and parent relationships after each container closes.
 */
static CVK_FORCE_INLINE VkBool32 CheckDeepDocument(void)
{
    enum { DEPTH = 4096, TEXT_CAPACITY = (DEPTH * 2) + 1, VALUE_CAPACITY = DEPTH + 1 };
    static char text[TEXT_CAPACITY];
    static cvkJsonValue values[VALUE_CAPACITY];
    uint32_t index;
    uint32_t count;
    VkBool32 passed;

    for (index = 0; index < DEPTH; ++index)
    {
        text[index] = '[';
        text[TEXT_CAPACITY - index - 1] = ']';
    }
    text[DEPTH] = '0';
    count = cvkParseJsonDocument(text, sizeof(text), values, VK_COUNT_OF(values));
    passed = VALUE_CAPACITY == count;
    for (index = 0; index < DEPTH; ++index)
    {
        passed = passed && (CVK_JSON_ARRAY == values[index].kind);
        passed = passed && (1 == values[index].childCount);
        passed = passed && (index + 1 == values[index].firstChild);
        passed = passed && (CVK_JSON_NONE == values[index].nextSibling);
    }
    passed = passed && (CVK_JSON_NUMBER == values[DEPTH].kind);
    passed = passed && (0 == cvkGetJsonUint64(&values[DEPTH]));
    return passed;
}

static FILE* OpenPipelineFile(const char* path)
{
#ifdef _MSC_VER
    FILE* file = NULL;
    (void)fopen_s(&file, path, "rb");
    return file;
#else
    return fopen(path, "rb");
#endif
}

static VkBool32 CheckRealNumbers(void)
{
    cvkJsonValue value = {0};
    double number;

    value.kind = CVK_JSON_NUMBER;
    value.pText = "-0.125";
    value.textLength = (uint32_t)strlen(value.pText);
    if (-0.125 != cvkGetJsonDouble(&value))
    {
        return VK_FALSE;
    }
    value.pText = "1.25e2";
    value.textLength = (uint32_t)strlen(value.pText);
    if (125.0 != cvkGetJsonDouble(&value))
    {
        return VK_FALSE;
    }
    value.pText = "2.5e-2";
    value.textLength = (uint32_t)strlen(value.pText);
    number = cvkGetJsonDouble(&value);
    return (0.024999 < number) && (number < 0.025001);
}

static int CheckDemoPipeline(const char* path, VkBool32 scaled)
{
    static char         text[8192];
    static cvkJsonValue values[512];
    static uint8_t      storage[32768];
    cvkArena            arena = {storage, sizeof(storage), 0};
    cvkJsonContext      context;
    cvkPipelineJson     pipeline;
    FILE*               file;
    size_t              length;

    file = OpenPipelineFile(path);
    CHECK(NULL != file);
    length = fread(text, 1, sizeof(text), file);
    (void)fclose(file);
    CHECK((0 < length) && (length < sizeof(text)));
    context.document.pValues    = values;
    context.document.valueCount = cvkParseJsonDocument(text, length, values, VK_COUNT_OF(values));
    context.pArena              = &arena;
    context.resolveHandle       = NULL;
    context.pUser               = NULL;
    context.ppHandleReferences  = NULL;
    cvkParsePipelineJson(&context, &pipeline);

    CHECK(VK_PIPELINE_BIND_POINT_GRAPHICS == pipeline.bindPoint);
    CHECK(scaled ? 0 == pipeline.renderPassVersion : 1 == pipeline.renderPassVersion);
    if (!scaled)
    {
        CHECK(VK_FORMAT_B8G8R8A8_UNORM == pipeline.renderPassCreateInfo.pAttachments[0].format);
        CHECK(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL == pipeline.renderPassCreateInfo.pAttachments[0].initialLayout);
        CHECK(VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL == pipeline.renderPassCreateInfo.pAttachments[0].finalLayout);
        CHECK(2 == pipeline.renderPassCreateInfo.dependencyCount);
    }
    CHECK(1 == pipeline.descriptorSetLayoutCount);
    CHECK((scaled ? 1U : 2U) == pipeline.pDescriptorSetLayoutCreateInfos[0].bindingCount);
    CHECK(1 == pipeline.samplerCount);
    CHECK(VK_FALSE == pipeline.pSamplerCreateInfos[0].anisotropyEnable);
    CHECK(1.0F == pipeline.pSamplerCreateInfos[0].maxAnisotropy);
    CHECK(1000.0F == pipeline.pSamplerCreateInfos[0].maxLod);
    CHECK((scaled ? 1U : 0U) == pipeline.pipelineLayoutCreateInfo.pushConstantRangeCount);
    if (scaled)
    {
        CHECK(24 == pipeline.pipelineLayoutCreateInfo.pPushConstantRanges[0].size);
    }
    CHECK(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP == pipeline.graphicsPipelineCreateInfo.pInputAssemblyState->topology);
    CHECK(VK_CULL_MODE_NONE == pipeline.graphicsPipelineCreateInfo.pRasterizationState->cullMode);
    CHECK(VK_SAMPLE_COUNT_1_BIT == pipeline.graphicsPipelineCreateInfo.pMultisampleState->rasterizationSamples);
    CHECK(VK_BLEND_FACTOR_ONE == pipeline.graphicsPipelineCreateInfo.pColorBlendState->pAttachments[0].srcColorBlendFactor);
    CHECK(VK_BLEND_FACTOR_ONE == pipeline.graphicsPipelineCreateInfo.pColorBlendState->pAttachments[0].srcAlphaBlendFactor);
    CHECK(2 == pipeline.graphicsPipelineCreateInfo.pDynamicState->dynamicStateCount);
    CHECK(1 == cvkGetJsonHandleId(pipeline.pHandleReferences, &pipeline.graphicsPipelineCreateInfo.renderPass));
    CHECK(1 == cvkGetJsonHandleId(pipeline.pHandleReferences, &pipeline.graphicsPipelineCreateInfo.pStages[0].module));
    CHECK(2 == cvkGetJsonHandleId(pipeline.pHandleReferences, &pipeline.graphicsPipelineCreateInfo.pStages[1].module));
    return 0;
}

static uint32_t shaderCreateCount;
static uint32_t shaderDestroyCount;
static uint32_t graphicsPipelineCount;
static VkShaderModule expectedVertex;
static VkShaderModule expectedFragment;
static VkRenderPass expectedRenderPass;
static const VkAllocationCallbacks* expectedAllocator;

static VKAPI_ATTR VkResult VKAPI_CALL CreateFixtureShaderModule(VkDevice device,
                                                                const VkShaderModuleCreateInfo* info,
                                                                const VkAllocationCallbacks* allocator,
                                                                VkShaderModule* module)
{
    CVK_ASSERT((VkDevice)(uintptr_t)1 == device);
    CVK_ASSERT(expectedAllocator == allocator);
    CVK_ASSERT(sizeof(uint32_t) == info->codeSize);
    CVK_ASSERT(NULL != info->pCode);
    ++shaderCreateCount;
    *module = (VkShaderModule)((uintptr_t)shaderCreateCount + 10);
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL DestroyFixtureShaderModule(VkDevice device,
                                                              VkShaderModule module,
                                                              const VkAllocationCallbacks* allocator)
{
    CVK_ASSERT((VkDevice)(uintptr_t)1 == device);
    CVK_ASSERT(expectedAllocator == allocator);
    CVK_ASSERT((VkShaderModule)(uintptr_t)(12 - shaderDestroyCount) == module);
    ++shaderDestroyCount;
}

static VKAPI_ATTR VkResult VKAPI_CALL CreateFixtureGraphicsPipeline(VkDevice device,
                                                                     VkPipelineCache cache,
                                                                     uint32_t count,
                                                                     const VkGraphicsPipelineCreateInfo* infos,
                                                                     const VkAllocationCallbacks* allocator,
                                                                     VkPipeline* pipelines)
{
    CVK_ASSERT((VkDevice)(uintptr_t)1 == device);
    CVK_ASSERT(VK_NULL_HANDLE == cache);
    CVK_ASSERT(1 == count);
    CVK_ASSERT(expectedAllocator == allocator);
    CVK_ASSERT(2 == infos[0].stageCount);
    CVK_ASSERT(expectedVertex == infos[0].pStages[0].module);
    CVK_ASSERT(expectedFragment == infos[0].pStages[1].module);
    CVK_ASSERT(expectedRenderPass == infos[0].renderPass);
    ++graphicsPipelineCount;
    pipelines[0] = (VkPipeline)((uintptr_t)graphicsPipelineCount + 20);
    return VK_SUCCESS;
}

static int CheckGraphicsPipelineModuleOwnership(void)
{
    static const char json[] = "{\"GraphicsPipelineState\":{\"GraphicsPipeline\":{\"stageCount\":2,\"pStages\":[{\"stage\":\"VK_SHADER_STAGE_VERTEX_BIT\",\"module\":7,\"pName\":\"main\"},{\"stage\":\"VK_SHADER_STAGE_FRAGMENT_BIT\",\"module\":9,\"pName\":\"main\"}],\"renderPass\":5}}}";
    const uint32_t code[] = {0x07230203};
    const VkDevice device = (VkDevice)(uintptr_t)1;
    cvkPipelineJsonObjects objects;
    JsonTestAllocator allocator;

    JsonTestInitAllocator(&allocator);
    expectedAllocator = &allocator.callbacks;
    vkCreateShaderModule = CreateFixtureShaderModule;
    vkDestroyShaderModule = DestroyFixtureShaderModule;
    vkCreateGraphicsPipelines = CreateFixtureGraphicsPipeline;
    expectedVertex = (VkShaderModule)(uintptr_t)31;
    expectedFragment = (VkShaderModule)(uintptr_t)32;
    expectedRenderPass = (VkRenderPass)(uintptr_t)33;
    cvkCreateGraphicsPipelineFromJsonWithModules(device,
                                                 VK_NULL_HANDLE,
                                                 json,
                                                 sizeof(json) - 1,
                                                 expectedVertex,
                                                 expectedFragment,
                                                 expectedRenderPass,
                                                 expectedAllocator,
                                                 &objects);
    CHECK(0 == shaderCreateCount);
    CHECK(0 == shaderDestroyCount);
    CHECK(1 == graphicsPipelineCount);
    CHECK(VK_NULL_HANDLE == objects.renderPass);
    expectedVertex = (VkShaderModule)(uintptr_t)11;
    expectedFragment = (VkShaderModule)(uintptr_t)12;
    cvkCreateGraphicsPipelineFromJson(device,
                                      VK_NULL_HANDLE,
                                      json,
                                      sizeof(json) - 1,
                                      sizeof(code),
                                      code,
                                      sizeof(code),
                                      code,
                                      expectedRenderPass,
                                      expectedAllocator,
                                      &objects);
    CHECK(2 == shaderCreateCount);
    CHECK(2 == shaderDestroyCount);
    CHECK(2 == graphicsPipelineCount);
    CHECK(VK_NULL_HANDLE == objects.renderPass);
    return 0;
}

int main(int argc, char** argv)
{
    static char            text[65536];
    static cvkJsonValue    values[4096];
    static uint8_t         storage[262144];
    cvkArena               arena;
    cvkJsonContext         context;
    cvkPipelineJson        pipeline;
    FILE*                  file;
    size_t                 length;
    const VkGraphicsPipelineCreateInfo* graphics;

    CHECK(CheckHandleReferences());
    CHECK(CheckDeepDocument());
    CHECK(CheckRealNumbers());
    CHECK(0 == CheckGraphicsPipelineModuleOwnership());
    CHECK(4 == argc);
    file = OpenPipelineFile(argv[1]);
    CHECK(NULL != file);
    length = fread(text, 1, sizeof(text), file);
    (void)fclose(file);
    CHECK((0 < length) && (length < sizeof(text)));

    arena.pBase                 = storage;
    arena.size                  = sizeof(storage);
    arena.used                  = 0;
    context.document.pValues    = values;
    context.document.valueCount = cvkParseJsonDocument(text, length, values, (uint32_t)(sizeof(values) / sizeof(values[0])));
    context.pArena              = &arena;
    context.resolveHandle       = NULL;
    context.pUser               = NULL;
    context.ppHandleReferences  = NULL;
    cvkParsePipelineJson(&context, &pipeline);

    CHECK(VK_PIPELINE_BIND_POINT_GRAPHICS == pipeline.bindPoint);
    CHECK(1 == pipeline.renderPassVersion);
    CHECK(2 == pipeline.renderPassCreateInfo.attachmentCount);
    CHECK(VK_FORMAT_R8G8B8A8_UNORM == pipeline.renderPassCreateInfo.pAttachments[0].format);
    CHECK(VK_FORMAT_D16_UNORM == pipeline.renderPassCreateInfo.pAttachments[1].format);
    CHECK(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR == pipeline.renderPassCreateInfo.pAttachments[0].finalLayout);
    CHECK(1 == pipeline.renderPassCreateInfo.subpassCount);
    CHECK(1 == pipeline.renderPassCreateInfo.pSubpasses[0].colorAttachmentCount);
    CHECK(NULL != pipeline.renderPassCreateInfo.pSubpasses[0].pDepthStencilAttachment);
    CHECK(1 == pipeline.renderPassCreateInfo.pSubpasses[0].pDepthStencilAttachment->attachment);
    CHECK(2 == pipeline.renderPassCreateInfo.dependencyCount);
    CHECK(VK_SUBPASS_EXTERNAL == pipeline.renderPassCreateInfo.pDependencies[0].srcSubpass);
    CHECK((VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT) == pipeline.renderPassCreateInfo.pDependencies[0].srcStageMask);
    CHECK(0 == pipeline.renderPassCreateInfo.pDependencies[1].srcAccessMask);

    CHECK(1 == pipeline.descriptorSetLayoutCount);
    CHECK(2 == pipeline.pDescriptorSetLayoutIds[0]);
    CHECK(2 == pipeline.pDescriptorSetLayoutCreateInfos[0].bindingCount);
    CHECK(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER == pipeline.pDescriptorSetLayoutCreateInfos[0].pBindings[1].descriptorType);
    CHECK(VK_SHADER_STAGE_FRAGMENT_BIT == pipeline.pDescriptorSetLayoutCreateInfos[0].pBindings[1].stageFlags);
    CHECK(NULL == pipeline.pDescriptorSetLayoutCreateInfos[0].pBindings[1].pImmutableSamplers);

    CHECK(pipeline.hasPipelineLayout);
    CHECK(1 == pipeline.pipelineLayoutCreateInfo.setLayoutCount);
    CHECK(VK_NULL_HANDLE == pipeline.pipelineLayoutCreateInfo.pSetLayouts[0]);
    CHECK(2 == cvkGetJsonHandleId(pipeline.pHandleReferences, (const void*)&pipeline.pipelineLayoutCreateInfo.pSetLayouts[0]));
    CHECK(0 == pipeline.pipelineLayoutCreateInfo.pushConstantRangeCount);

    graphics = &pipeline.graphicsPipelineCreateInfo;
    CHECK(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO == graphics->sType);
    CHECK(2 == graphics->stageCount);
    CHECK(VK_SHADER_STAGE_VERTEX_BIT == graphics->pStages[0].stage);
    CHECK(VK_SHADER_STAGE_FRAGMENT_BIT == graphics->pStages[1].stage);
    CHECK(0 == strcmp("main", graphics->pStages[1].pName));
    CHECK(NULL == graphics->pStages[1].pSpecializationInfo);
    CHECK(NULL == graphics->pTessellationState);
    CHECK(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST == graphics->pInputAssemblyState->topology);
    CHECK(VK_FALSE == graphics->pInputAssemblyState->primitiveRestartEnable);
    CHECK(1 == graphics->pViewportState->viewportCount);
    CHECK(NULL == graphics->pViewportState->pViewports);
    CHECK(VK_CULL_MODE_BACK_BIT == graphics->pRasterizationState->cullMode);
    CHECK(1.0f == graphics->pRasterizationState->lineWidth);
    CHECK(VK_TRUE == graphics->pDepthStencilState->depthTestEnable);
    CHECK(VK_COMPARE_OP_LESS_OR_EQUAL == graphics->pDepthStencilState->depthCompareOp);
    CHECK(VK_STENCIL_OP_KEEP == graphics->pDepthStencilState->back.failOp);
    CHECK(1 == graphics->pColorBlendState->attachmentCount);
    CHECK((VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT) == graphics->pColorBlendState->pAttachments[0].colorWriteMask);
    CHECK(0.0f == graphics->pColorBlendState->blendConstants[3]);
    CHECK(2 == graphics->pDynamicState->dynamicStateCount);
    CHECK(VK_DYNAMIC_STATE_SCISSOR == graphics->pDynamicState->pDynamicStates[1]);
    CHECK(VK_NULL_HANDLE == graphics->layout);
    CHECK(5 == cvkGetJsonHandleId(pipeline.pHandleReferences, (const void*)&graphics->layout));
    CHECK(VK_NULL_HANDLE == graphics->basePipelineHandle);

    CHECK(2 == pipeline.shaderCount);
    CHECK(VK_SHADER_STAGE_FRAGMENT_BIT == pipeline.pShaderStages[1]);
    CHECK(0 == strcmp("cube.frag.spv", pipeline.ppShaderFileNames[1]));
    CHECK(0 == pipeline.enabledExtensionCount);
    CHECK(pipeline.hasPipelineUuid);
    CHECK(245 == pipeline.pipelineUuid[0]);
    CHECK(123 == pipeline.pipelineUuid[7]);
    CHECK(0 == CheckDemoPipeline(argv[2], VK_FALSE));
    CHECK(0 == CheckDemoPipeline(argv[3], VK_TRUE));

    (void)printf("pipeline JSON parsed: %u values, %u bytes of arena\n", context.document.valueCount, (unsigned)arena.used);
    return 0;
}
