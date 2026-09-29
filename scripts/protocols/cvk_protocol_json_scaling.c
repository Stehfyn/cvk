/* Profile: vulkan. CVK-owned bicubic scaling pipeline assets and draw command. */
#include <cvk_fullscreen_vert.inc>
#include <cvk_bicubic_frag.inc>
#include <cvk_bicubic_pipeline_json.inc>

typedef struct cvkBicubicScalingConstants
{
    float texelSize[2];
    float blend;
    float padding;
    float sampleScale[2];
} cvkBicubicScalingConstants;

/**
 * \brief Creates CVK's bicubic scaling graphics pipeline and its descriptor objects.
 * \par Profile: vulkan
 * \par Details:
 *  CVK owns the pipeline JSON and SPIR-V. The shader modules are temporary. The returned
 *  pipeline, pipeline layout, descriptor set layout, and sampler belong to the caller.
 *  The render pass must be compatible with the pipeline's single color attachment.
 * \param[in] device Logical device creating the objects.
 * \param[in] pipelineCache Pipeline cache, or VK_NULL_HANDLE.
 * \param[in] pJsonValues Storage for the document's values.
 * \param[in] jsonValueCapacity Capacity of pJsonValues.
 * \param[in] pArena Arena for parsed structures and resolved arrays.
 * \param[in] renderPass Caller-owned compatible render pass.
 * \param[in] pAllocator Optional allocation callbacks, or NULL.
 * \param[out] pObjects Receives the created JSON objects.
 * \retval None
 */
void cvkCreateBicubicScalingPipeline(VkDevice                     device,
                                     VkPipelineCache              pipelineCache,
                                     cvkJsonValue*                pJsonValues,
                                     uint32_t                     jsonValueCapacity,
                                     cvkArena*                    pArena,
                                     VkRenderPass                 renderPass,
                                     const VkAllocationCallbacks* pAllocator,
                                     cvkPipelineJsonObjects*      pObjects)
{
    CVK_ASSERT(VK_NULL_HANDLE != renderPass);
    cvkCreateGraphicsPipelineFromJson(device,
                                      pipelineCache,
                                      cvk_bicubic_pipeline_json,
                                      sizeof(cvk_bicubic_pipeline_json) - 1,
                                      pJsonValues,
                                      jsonValueCapacity,
                                      pArena,
                                      sizeof(cvk_fullscreen_vert),
                                      cvk_fullscreen_vert,
                                      sizeof(cvk_bicubic_frag),
                                      cvk_bicubic_frag,
                                      renderPass,
                                      pAllocator,
                                      pObjects);
}

/**
 * \brief Draws a fullscreen quad with CVK's bicubic scaling pipeline.
 * \par Profile: vulkan
 * \par Details:
 *  The descriptor set must bind the sampled image at set 0, binding 0. A blend value of
 *  zero selects bicubic filtering; one selects the original linearly filtered sample.
 * \param[in] commandBuffer Recording command buffer within a compatible render pass.
 * \param[in] pipeline Pipeline created by cvkCreateBicubicScalingPipeline.
 * \param[in] layout Layout returned with the pipeline.
 * \param[in] descriptorSet Descriptor set for the sampled image.
 * \param[in] sourceExtent Dimensions of the sampled image.
 * \param[in] contentExtent Dimensions of the destination content.
 * \param[in] backingExtent Dimensions of the backing image.
 * \param[in] blend Bicubic-to-original blend in the range zero to one.
 * \retval None
 */
void cvkCmdDrawFullscreenQuadWithBicubicScaling(VkCommandBuffer  commandBuffer,
                                                VkPipeline       pipeline,
                                                VkPipelineLayout layout,
                                                VkDescriptorSet  descriptorSet,
                                                VkExtent2D       sourceExtent,
                                                VkExtent2D       contentExtent,
                                                VkExtent2D       backingExtent,
                                                float            blend)
{
    cvkBicubicScalingConstants constants;

    CVK_ASSERT((0 != sourceExtent.width) && (0 != sourceExtent.height));
    CVK_ASSERT((0 != contentExtent.width) && (0 != contentExtent.height));
    CVK_ASSERT((0.0F <= blend) && (1.0F >= blend));
    constants.texelSize[0]   = 1.0F / (float)sourceExtent.width;
    constants.texelSize[1]   = 1.0F / (float)sourceExtent.height;
    constants.blend          = blend;
    constants.padding        = 0.0F;
    constants.sampleScale[0] = (float)backingExtent.width / (float)contentExtent.width;
    constants.sampleScale[1] = (float)backingExtent.height / (float)contentExtent.height;
    cvkCmdDrawFullscreenQuadWithPushConstants(commandBuffer,
                                              pipeline,
                                              layout,
                                              0,
                                              1,
                                              &descriptorSet,
                                              0,
                                              NULL,
                                              0,
                                              sizeof(constants),
                                              &constants);
}
