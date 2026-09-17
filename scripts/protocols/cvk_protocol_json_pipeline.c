/* Tier 3 protocol (SPEC.md section 8c): a Vulkan SC pipeline JSON (the pipeline cache
** compiler's input, in memory) to the objects and handles it describes. Source of truth kept
** with the generator and folded into the generated cvk.h/cvk.c. Composed from the generated
** surface only: the reader (cvk_protocol_json.c) and the generated cvkParseJson* parsers. */

/**
 * \brief Parses an id-keyed object list ("DescriptorSetLayouts": [{"2": {...}}, ...]) into ids and structures of one type.
 *
 * \param[in]  pContext     Parsing context
 * \param[in]  pList        The JSON array, or NULL for none
 * \param[in]  elementSize  sizeof the parsed structure
 * \param[out] ppIds        Receives the arena array of ids, one per entry
 * \param[out] ppElements   Receives the arena array of structures, zeroed; the caller parses each
 *
 * \retval Number of entries
 */
uint32_t cvkParsePipelineJsonObjectList(const cvkJsonContext* pContext, const cvkJsonValue* pList, size_t elementSize, uint64_t** ppIds, void** ppElements)
{
    uint32_t count;
    uint32_t index;

    count = ((NULL != pList) && (CVK_JSON_ARRAY == pList->kind)) ? pList->childCount : 0;
    (*ppIds)      = (uint64_t*)cvkAllocateArena(pContext->pArena, count * sizeof(uint64_t), sizeof(uint64_t));
    (*ppElements) = cvkAllocateArena(pContext->pArena, count * elementSize, 16);
    for (index = 0; index < count; ++index)
    {
        const cvkJsonValue* entry  = cvkGetJsonElement(&pContext->document, pList, index);
        const cvkJsonValue* member = cvkGetJsonElement(&pContext->document, entry, 0); /* the one member: id -> structure */
        char                buffer[32];

        CVK_ASSERT((NULL != member) && (member->keyLength < sizeof(buffer)));
        memcpy(buffer, member->pKey, member->keyLength);
        buffer[member->keyLength] = '\0';
        (*ppIds)[index]           = cvkParseUint64Text(buffer);
    }
    return count;
}

/**
 * \brief Parses a pipeline JSON document into its create infos; handle fields keep the file's object ids.
 *
 * \par Details:
 *  Reads "GraphicsPipelineState" or "ComputePipelineState" with its "Renderpass" (version
 *  from the sType), "DescriptorSetLayouts", "PipelineLayout", "GraphicsPipeline" or
 *  "ComputePipeline", "ShaderFileNames", "Samplers" or "ImmutableSamplers", and
 *  "YcbcrSamplers"; then "EnabledExtensions" and "PipelineUUID". Handles are parsed through
 *  the context's resolver; with none, they hold the ids for cvkCreatePipelineJsonObjects.
 *
 * \param[in]  pContext   Parsing context whose document is the pipeline JSON
 * \param[out] pPipeline  Receives the parsed pipeline description
 *
 * \retval None
 */
void cvkParsePipelineJson(const cvkJsonContext* pContext, cvkPipelineJson* pPipeline)
{
    const cvkJsonValue* root = &pContext->document.pValues[0];
    const cvkJsonValue* state;
    const cvkJsonValue* value;
    const cvkJsonValue* list;
    void*               elements;
    uint32_t            index;

    memset(pPipeline, 0, sizeof(*pPipeline));
    state = cvkGetJsonMember(&pContext->document, root, "GraphicsPipelineState");
    if (NULL != state)
    {
        pPipeline->bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        value                = cvkGetJsonMember(&pContext->document, state, "GraphicsPipeline");
        if (NULL != value)
        {
            cvkParseJsonGraphicsPipelineCreateInfo(pContext, value, &pPipeline->graphicsPipelineCreateInfo);
        }
    }
    else
    {
        state = cvkGetJsonMember(&pContext->document, root, "ComputePipelineState");
        CVK_ASSERT(NULL != state);
        pPipeline->bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
        value                = cvkGetJsonMember(&pContext->document, state, "ComputePipeline");
        if (NULL != value)
        {
            cvkParseJsonComputePipelineCreateInfo(pContext, value, &pPipeline->computePipelineCreateInfo);
        }
    }

    /* render pass: the sType decides the version */
    value = cvkGetJsonMember(&pContext->document, state, "Renderpass");
    if (!cvkIsJsonNull(value))
    {
        if (cvkJsonStringEquals(cvkGetJsonMember(&pContext->document, value, "sType"), "VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2"))
        {
            pPipeline->renderPassVersion = 2;
            cvkParseJsonRenderPassCreateInfo2(pContext, value, &pPipeline->renderPassCreateInfo2);
        }
        else
        {
            pPipeline->renderPassVersion = 1;
            cvkParseJsonRenderPassCreateInfo(pContext, value, &pPipeline->renderPassCreateInfo);
        }
    }

    /* objects by id */
    list                                 = cvkGetJsonMember(&pContext->document, state, "DescriptorSetLayouts");
    pPipeline->descriptorSetLayoutCount  = cvkParsePipelineJsonObjectList(pContext, list, sizeof(VkDescriptorSetLayoutCreateInfo), &pPipeline->pDescriptorSetLayoutIds, &elements);
    pPipeline->pDescriptorSetLayoutCreateInfos = (VkDescriptorSetLayoutCreateInfo*)elements;
    for (index = 0; index < pPipeline->descriptorSetLayoutCount; ++index)
    {
        cvkParseJsonDescriptorSetLayoutCreateInfo(pContext, cvkGetJsonElement(&pContext->document, cvkGetJsonElement(&pContext->document, list, index), 0), &pPipeline->pDescriptorSetLayoutCreateInfos[index]);
    }
    list = cvkGetJsonMember(&pContext->document, state, "Samplers");
    if (NULL == list)
    {
        list = cvkGetJsonMember(&pContext->document, state, "ImmutableSamplers");
    }
    pPipeline->samplerCount        = cvkParsePipelineJsonObjectList(pContext, list, sizeof(VkSamplerCreateInfo), &pPipeline->pSamplerIds, &elements);
    pPipeline->pSamplerCreateInfos = (VkSamplerCreateInfo*)elements;
    for (index = 0; index < pPipeline->samplerCount; ++index)
    {
        cvkParseJsonSamplerCreateInfo(pContext, cvkGetJsonElement(&pContext->document, cvkGetJsonElement(&pContext->document, list, index), 0), &pPipeline->pSamplerCreateInfos[index]);
    }
    list                                          = cvkGetJsonMember(&pContext->document, state, "YcbcrSamplers");
    pPipeline->samplerYcbcrConversionCount        = cvkParsePipelineJsonObjectList(pContext, list, sizeof(VkSamplerYcbcrConversionCreateInfo), &pPipeline->pSamplerYcbcrConversionIds, &elements);
    pPipeline->pSamplerYcbcrConversionCreateInfos = (VkSamplerYcbcrConversionCreateInfo*)elements;
    for (index = 0; index < pPipeline->samplerYcbcrConversionCount; ++index)
    {
        cvkParseJsonSamplerYcbcrConversionCreateInfo(pContext, cvkGetJsonElement(&pContext->document, cvkGetJsonElement(&pContext->document, list, index), 0), &pPipeline->pSamplerYcbcrConversionCreateInfos[index]);
    }

    /* the pipeline layout */
    value = cvkGetJsonMember(&pContext->document, state, "PipelineLayout");
    if (!cvkIsJsonNull(value))
    {
        pPipeline->hasPipelineLayout = VK_TRUE;
        cvkParseJsonPipelineLayoutCreateInfo(pContext, value, &pPipeline->pipelineLayoutCreateInfo);
    }

    /* shaders: stage and file name */
    list                       = cvkGetJsonMember(&pContext->document, state, "ShaderFileNames");
    pPipeline->shaderCount     = ((NULL != list) && (CVK_JSON_ARRAY == list->kind)) ? list->childCount : 0;
    pPipeline->pShaderStages   = (VkShaderStageFlagBits*)cvkAllocateArena(pContext->pArena, pPipeline->shaderCount * sizeof(VkShaderStageFlagBits), sizeof(VkShaderStageFlagBits));
    pPipeline->ppShaderFileNames = (const char**)cvkAllocateArena(pContext->pArena, pPipeline->shaderCount * sizeof(const char*), sizeof(const char*));
    for (index = 0; index < pPipeline->shaderCount; ++index)
    {
        const cvkJsonValue* shader = cvkGetJsonElement(&pContext->document, list, index);

        pPipeline->pShaderStages[index]     = cvkParseJsonShaderStageFlagBits(cvkGetJsonMember(&pContext->document, shader, "stage"));
        pPipeline->ppShaderFileNames[index] = cvkCopyJsonString(pContext->pArena, cvkGetJsonMember(&pContext->document, shader, "filename"));
    }

    /* document-level: extensions and the pipeline identifier */
    list                              = cvkGetJsonMember(&pContext->document, root, "EnabledExtensions");
    pPipeline->enabledExtensionCount  = ((NULL != list) && (CVK_JSON_ARRAY == list->kind)) ? list->childCount : 0;
    pPipeline->ppEnabledExtensionNames = (const char**)cvkAllocateArena(pContext->pArena, pPipeline->enabledExtensionCount * sizeof(const char*), sizeof(const char*));
    for (index = 0; index < pPipeline->enabledExtensionCount; ++index)
    {
        pPipeline->ppEnabledExtensionNames[index] = cvkCopyJsonString(pContext->pArena, cvkGetJsonElement(&pContext->document, list, index));
    }
    list = cvkGetJsonMember(&pContext->document, root, "PipelineUUID");
    if ((NULL != list) && (CVK_JSON_ARRAY == list->kind) && (VK_UUID_SIZE == list->childCount))
    {
        pPipeline->hasPipelineUuid = VK_TRUE;
        for (index = 0; index < VK_UUID_SIZE; ++index)
        {
            pPipeline->pipelineUuid[index] = (uint8_t)cvkGetJsonUint64(cvkGetJsonElement(&pContext->document, list, index));
        }
    }
}

/**
 * \brief Returns the handle recorded for an id in an id and handle table, or 0.
 *
 * \param[in]  count     Number of entries
 * \param[in]  pIds      The ids
 * \param[in]  pHandles  The handles, as integers, one per id
 * \param[in]  id        The id to find
 *
 * \retval The handle as an integer, or 0
 */
uint64_t cvkGetPipelineJsonHandle(uint32_t count, const uint64_t* pIds, const uint64_t* pHandles, uint64_t id)
{
    uint32_t index;

    for (index = 0; index < count; ++index)
    {
        if (pIds[index] == id)
        {
            return pHandles[index];
        }
    }
    return 0;
}

/**
 * \brief Creates the objects a parsed pipeline JSON describes, in dependency order, then the pipeline.
 *
 * \par Details:
 *  Ycbcr conversions, samplers, descriptor set layouts (immutable sampler ids resolved),
 *  the pipeline layout (set layout ids resolved), the render pass (version 1 or 2), and the
 *  pipeline with its layout and render pass resolved, its base pipeline cleared, and its
 *  shader modules resolved through the caller's resolver ("VkShaderModule", id), which
 *  returns VK_NULL_HANDLE where modules do not exist (Vulkan SC). Ids in extension chains
 *  are not resolved. The pipeline's pNext is pPipelineNext when given (Vulkan SC chains its
 *  VkPipelineOfflineCreateInfo there), else the file's.
 *
 * \param[in]  device           Logical device creating the objects
 * \param[in]  pipelineCache    Pipeline cache the pipeline is created from, or VK_NULL_HANDLE
 * \param[in]  pPipeline        The parsed pipeline JSON
 * \param[in]  pPipelineNext    Extension chain for the pipeline create info replacing the file's, or NULL
 * \param[in]  resolveExternal  Resolver for handles the file does not define (shader modules), or NULL
 * \param[in]  pUser            Passed to the resolver
 * \param[in]  pArena           Arena for the resolved arrays
 * \param[in]  pAllocator       Optional allocation callbacks, or NULL
 * \param[out] pObjects         Receives every created handle beside its id
 *
 * \retval None
 */
void cvkCreatePipelineJsonObjects(VkDevice                     device,
                                  VkPipelineCache              pipelineCache,
                                  const cvkPipelineJson*       pPipeline,
                                  const void*                  pPipelineNext,
                                  PFN_cvkResolveJsonHandle     resolveExternal,
                                  void*                        pUser,
                                  cvkArena*                    pArena,
                                  const VkAllocationCallbacks* pAllocator,
                                  cvkPipelineJsonObjects*      pObjects)
{
    uint64_t sampler_handles[CVK_MAX_PIPELINE_JSON_OBJECTS];
    uint64_t descriptor_set_layout_handles[CVK_MAX_PIPELINE_JSON_OBJECTS];
    uint32_t index;
    uint32_t inner;

    memset(pObjects, 0, sizeof(*pObjects));
    CVK_ASSERT(CVK_MAX_PIPELINE_JSON_OBJECTS >= pPipeline->samplerYcbcrConversionCount);
    CVK_ASSERT(CVK_MAX_PIPELINE_JSON_OBJECTS >= pPipeline->samplerCount);
    CVK_ASSERT(CVK_MAX_PIPELINE_JSON_OBJECTS >= pPipeline->descriptorSetLayoutCount);

    /* Ycbcr conversions */
    pObjects->samplerYcbcrConversionCount = pPipeline->samplerYcbcrConversionCount;
    for (index = 0; index < pPipeline->samplerYcbcrConversionCount; ++index)
    {
        const VkSamplerYcbcrConversionCreateInfo* info = &pPipeline->pSamplerYcbcrConversionCreateInfos[index];

        pObjects->samplerYcbcrConversionIds[index] = pPipeline->pSamplerYcbcrConversionIds[index];
        cvkCreateSamplerYcbcrConversion(device, info->pNext, info->format, info->ycbcrModel, info->ycbcrRange, info->components, info->xChromaOffset, info->yChromaOffset, info->chromaFilter, info->forceExplicitReconstruction, pAllocator, &pObjects->samplerYcbcrConversions[index]);
    }

    /* samplers */
    pObjects->samplerCount = pPipeline->samplerCount;
    for (index = 0; index < pPipeline->samplerCount; ++index)
    {
        const VkSamplerCreateInfo* info = &pPipeline->pSamplerCreateInfos[index];

        pObjects->samplerIds[index] = pPipeline->pSamplerIds[index];
        cvkCreateSampler(device, info->pNext, info->flags, info->magFilter, info->minFilter, info->mipmapMode, info->addressModeU, info->addressModeV, info->addressModeW, info->mipLodBias, info->anisotropyEnable, info->maxAnisotropy, info->compareEnable, info->compareOp, info->minLod, info->maxLod, info->borderColor, info->unnormalizedCoordinates, pAllocator, &pObjects->samplers[index]);
        sampler_handles[index] = (uint64_t)(uintptr_t)pObjects->samplers[index];
    }

    /* descriptor set layouts, immutable sampler ids resolved */
    pObjects->descriptorSetLayoutCount = pPipeline->descriptorSetLayoutCount;
    for (index = 0; index < pPipeline->descriptorSetLayoutCount; ++index)
    {
        const VkDescriptorSetLayoutCreateInfo* info     = &pPipeline->pDescriptorSetLayoutCreateInfos[index];
        VkDescriptorSetLayoutBinding*          bindings = (VkDescriptorSetLayoutBinding*)cvkAllocateArena(pArena, info->bindingCount * sizeof(VkDescriptorSetLayoutBinding), 8);

        for (inner = 0; inner < info->bindingCount; ++inner)
        {
            bindings[inner] = info->pBindings[inner];
            if (NULL != bindings[inner].pImmutableSamplers)
            {
                VkSampler* samplers = (VkSampler*)cvkAllocateArena(pArena, bindings[inner].descriptorCount * sizeof(VkSampler), 8);
                uint32_t   s;

                for (s = 0; s < bindings[inner].descriptorCount; ++s)
                {
                    samplers[s] = (VkSampler)(uintptr_t)cvkGetPipelineJsonHandle(pObjects->samplerCount, pObjects->samplerIds, sampler_handles, (uint64_t)(uintptr_t)info->pBindings[inner].pImmutableSamplers[s]);
                }
                bindings[inner].pImmutableSamplers = samplers;
            }
        }
        pObjects->descriptorSetLayoutIds[index] = pPipeline->pDescriptorSetLayoutIds[index];
        cvkCreateDescriptorSetLayout(device, info->pNext, info->flags, info->bindingCount, bindings, pAllocator, &pObjects->descriptorSetLayouts[index]);
        descriptor_set_layout_handles[index] = (uint64_t)(uintptr_t)pObjects->descriptorSetLayouts[index];
    }

    /* pipeline layout, set layout ids resolved */
    if (pPipeline->hasPipelineLayout)
    {
        const VkPipelineLayoutCreateInfo* info    = &pPipeline->pipelineLayoutCreateInfo;
        VkDescriptorSetLayout*            layouts = (VkDescriptorSetLayout*)cvkAllocateArena(pArena, info->setLayoutCount * sizeof(VkDescriptorSetLayout), 8);

        for (inner = 0; inner < info->setLayoutCount; ++inner)
        {
            layouts[inner] = (VkDescriptorSetLayout)(uintptr_t)cvkGetPipelineJsonHandle(pObjects->descriptorSetLayoutCount, pObjects->descriptorSetLayoutIds, descriptor_set_layout_handles, (uint64_t)(uintptr_t)info->pSetLayouts[inner]);
        }
        cvkCreatePipelineLayout(device, info->pNext, info->flags, info->setLayoutCount, layouts, info->pushConstantRangeCount, info->pPushConstantRanges, pAllocator, &pObjects->pipelineLayout);
    }

    /* render pass */
    if (1 == pPipeline->renderPassVersion)
    {
        const VkRenderPassCreateInfo* info = &pPipeline->renderPassCreateInfo;

        cvkCreateRenderPass(device, info->pNext, info->flags, info->attachmentCount, info->pAttachments, info->subpassCount, info->pSubpasses, info->dependencyCount, info->pDependencies, pAllocator, &pObjects->renderPass);
    }
    else if (2 == pPipeline->renderPassVersion)
    {
        const VkRenderPassCreateInfo2* info = &pPipeline->renderPassCreateInfo2;

        cvkCreateRenderPass2(device, info->pNext, info->flags, info->attachmentCount, info->pAttachments, info->subpassCount, info->pSubpasses, info->dependencyCount, info->pDependencies, info->correlatedViewMaskCount, info->pCorrelatedViewMasks, pAllocator, &pObjects->renderPass);
    }

    /* the pipeline */
    if (VK_PIPELINE_BIND_POINT_GRAPHICS == pPipeline->bindPoint)
    {
        const VkGraphicsPipelineCreateInfo* info   = &pPipeline->graphicsPipelineCreateInfo;
        VkPipelineShaderStageCreateInfo*    stages = (VkPipelineShaderStageCreateInfo*)cvkAllocateArena(pArena, info->stageCount * sizeof(VkPipelineShaderStageCreateInfo), 8);

        for (inner = 0; inner < info->stageCount; ++inner)
        {
            stages[inner]        = info->pStages[inner];
            stages[inner].module = (NULL != resolveExternal) ? (VkShaderModule)(uintptr_t)resolveExternal(pUser, "VkShaderModule", (uint64_t)(uintptr_t)info->pStages[inner].module) : VK_NULL_HANDLE;
        }
        cvkCreateGraphicsPipeline(device,
                                  pipelineCache,
                                  (NULL != pPipelineNext) ? pPipelineNext : info->pNext,
                                  info->flags,
                                  info->stageCount,
                                  stages,
                                  info->pVertexInputState,
                                  info->pInputAssemblyState,
                                  info->pTessellationState,
                                  info->pViewportState,
                                  info->pRasterizationState,
                                  info->pMultisampleState,
                                  info->pDepthStencilState,
                                  info->pColorBlendState,
                                  info->pDynamicState,
                                  pObjects->pipelineLayout,
                                  pObjects->renderPass,
                                  info->subpass,
                                  VK_NULL_HANDLE,
                                  -1,
                                  pAllocator,
                                  &pObjects->pipeline);
    }
    else
    {
        const VkComputePipelineCreateInfo* info  = &pPipeline->computePipelineCreateInfo;
        VkPipelineShaderStageCreateInfo    stage = info->stage;

        stage.module = (NULL != resolveExternal) ? (VkShaderModule)(uintptr_t)resolveExternal(pUser, "VkShaderModule", (uint64_t)(uintptr_t)info->stage.module) : VK_NULL_HANDLE;
        cvkCreateComputePipeline(device, pipelineCache, (NULL != pPipelineNext) ? pPipelineNext : info->pNext, info->flags, stage, pObjects->pipelineLayout, VK_NULL_HANDLE, -1, pAllocator, &pObjects->pipeline);
    }
}

/**
 * \brief Turns an in-memory pipeline JSON into its objects and pipeline: reads the document, parses it, creates everything.
 *
 * \param[in]  device             Logical device creating the objects
 * \param[in]  pipelineCache      Pipeline cache the pipeline is created from, or VK_NULL_HANDLE
 * \param[in]  pJsonText          The pipeline JSON text
 * \param[in]  jsonLength         Byte length of the text
 * \param[in]  pJsonValues        Storage for the document's values
 * \param[in]  jsonValueCapacity  Capacity of pJsonValues
 * \param[in]  pArena             Arena for the parsed structures and resolved arrays
 * \param[in]  resolveExternal    Resolver for shader modules, or NULL for VK_NULL_HANDLE modules
 * \param[in]  pUser              Passed to the resolver
 * \param[in]  pAllocator         Optional allocation callbacks, or NULL
 * \param[out] pPipeline          Receives the parsed description; its handle fields hold the file's ids
 * \param[out] pObjects           Receives every created handle beside its id
 *
 * \retval None
 */
void cvkCreatePipelineFromJson(VkDevice                     device,
                               VkPipelineCache              pipelineCache,
                               const char*                  pJsonText,
                               size_t                       jsonLength,
                               cvkJsonValue*                pJsonValues,
                               uint32_t                     jsonValueCapacity,
                               cvkArena*                    pArena,
                               PFN_cvkResolveJsonHandle     resolveExternal,
                               void*                        pUser,
                               const VkAllocationCallbacks* pAllocator,
                               cvkPipelineJson*             pPipeline,
                               cvkPipelineJsonObjects*      pObjects)
{
    cvkJsonContext context;

    context.document.pValues    = pJsonValues;
    context.document.valueCount = cvkParseJsonDocument(pJsonText, jsonLength, pJsonValues, jsonValueCapacity);
    context.pArena              = pArena;
    context.resolveHandle       = NULL;
    context.pUser               = NULL;
    cvkParsePipelineJson(&context, pPipeline);
    cvkCreatePipelineJsonObjects(device, pipelineCache, pPipeline, NULL, resolveExternal, pUser, pArena, pAllocator, pObjects);
}

/**
 * \brief Turns an in-memory pipeline JSON into its objects and a pipeline created offline from the pipeline cache by its identifier.
 *
 * \par Profile: vulkansc
 *
 * \par Details:
 *  Vulkan SC creates pipelines from a cache by identifier: the file's PipelineUUID goes into
 *  a VkPipelineOfflineCreateInfo chained ahead of the file's own extension chain, with the
 *  match control and pool entry size the application knows from its cache. Shader modules do
 *  not exist in Vulkan SC: the stages' modules are VK_NULL_HANDLE.
 *
 * \param[in]  device             Logical device creating the objects
 * \param[in]  pipelineCache      Pipeline cache holding the pipeline
 * \param[in]  pJsonText          The pipeline JSON text
 * \param[in]  jsonLength         Byte length of the text
 * \param[in]  pJsonValues        Storage for the document's values
 * \param[in]  jsonValueCapacity  Capacity of pJsonValues
 * \param[in]  pArena             Arena for the parsed structures and resolved arrays
 * \param[in]  matchControl       VkPipelineOfflineCreateInfo::matchControl
 * \param[in]  poolEntrySize      VkPipelineOfflineCreateInfo::poolEntrySize
 * \param[in]  pAllocator         Optional allocation callbacks, or NULL
 * \param[out] pPipeline          Receives the parsed description; its handle fields hold the file's ids
 * \param[out] pObjects           Receives every created handle beside its id
 *
 * \retval None
 */
void cvkCreateOfflinePipelineFromJson(VkDevice                     device,
                                      VkPipelineCache              pipelineCache,
                                      const char*                  pJsonText,
                                      size_t                       jsonLength,
                                      cvkJsonValue*                pJsonValues,
                                      uint32_t                     jsonValueCapacity,
                                      cvkArena*                    pArena,
                                      VkPipelineMatchControl       matchControl,
                                      VkDeviceSize                 poolEntrySize,
                                      const VkAllocationCallbacks* pAllocator,
                                      cvkPipelineJson*             pPipeline,
                                      cvkPipelineJsonObjects*      pObjects)
{
    cvkJsonContext              context;
    VkPipelineOfflineCreateInfo pipeline_offline_create_info;
    const void*                 file_next;

    context.document.pValues    = pJsonValues;
    context.document.valueCount = cvkParseJsonDocument(pJsonText, jsonLength, pJsonValues, jsonValueCapacity);
    context.pArena              = pArena;
    context.resolveHandle       = NULL;
    context.pUser               = NULL;
    cvkParsePipelineJson(&context, pPipeline);
    CVK_ASSERT(pPipeline->hasPipelineUuid);
    file_next = (VK_PIPELINE_BIND_POINT_GRAPHICS == pPipeline->bindPoint) ? pPipeline->graphicsPipelineCreateInfo.pNext : pPipeline->computePipelineCreateInfo.pNext;
    cvkInitPipelineOfflineCreateInfo(&pipeline_offline_create_info, file_next, pPipeline->pipelineUuid, matchControl, poolEntrySize);
    cvkCreatePipelineJsonObjects(device, pipelineCache, pPipeline, &pipeline_offline_create_info, NULL, NULL, pArena, pAllocator, pObjects);
}
