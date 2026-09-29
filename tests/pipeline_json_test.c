/* Parses tests/cube.pc.json (a Vulkan SC pipeline JSON) with cvkParsePipelineJson and checks the
** structures it yields; no device is needed. Run by CTest as cvk-pipeline-json-parse. */

#include "vk_diagnostic.h"
#include "cvk/cvk_json.h"
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
    char text[TEXT_CAPACITY];
    cvkJsonValue values[VALUE_CAPACITY];
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
    CHECK(2 == argc);
    file = fopen(argv[1], "rb");
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

    (void)printf("pipeline JSON parsed: %u values, %u bytes of arena\n", context.document.valueCount, (unsigned)arena.used);
    return 0;
}
