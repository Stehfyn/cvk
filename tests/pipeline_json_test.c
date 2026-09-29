/* Parses tests/cube.pc.json (a Vulkan SC pipeline JSON) with cvkParsePipelineJson and checks the
** structures it yields; no device is needed. Run by CTest as cvk-pipeline-json-parse. */

#include "cvk/cvk_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                      \
    do                                                                         \
    {                                                                          \
        if (!(expression))                                                     \
        {                                                                      \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                          \
        }                                                                      \
    } while (0)

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
    CHECK((VkDescriptorSetLayout)(uintptr_t)2 == pipeline.pipelineLayoutCreateInfo.pSetLayouts[0]);
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
    CHECK((VkPipelineLayout)(uintptr_t)5 == graphics->layout);
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
