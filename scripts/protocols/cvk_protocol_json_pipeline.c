/* Tier 3 protocol (SPEC.md section 8c): a Vulkan SC pipeline JSON (the pipeline cache
** compiler's input, in memory) to the objects and handles it describes. Source of truth kept
** with the generator and folded into the generated cvk_json.h/cvk_json.c. Composed from the generated
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
    (*ppElements) = cvkAllocateArena(pContext->pArena, count * elementSize, CVK_JSON_ARENA_ALIGNMENT);
    CVK_ASSERT((0 == count) || ((NULL != *ppIds) && (NULL != *ppElements)));
    for (index = 0; index < count; ++index)
    {
        const cvkJsonValue* entry  = cvkGetJsonElement(&pContext->document, pList, index);
        const cvkJsonValue* member = cvkGetJsonElement(&pContext->document, entry, 0); /* the one member: id -> structure */
        char                buffer[CVK_JSON_INTEGER_TEXT_CAPACITY];

        CVK_ASSERT((NULL != member) && (member->keyLength < sizeof(buffer)));
        (void)memcpy(buffer, member->pKey, member->keyLength);
        buffer[member->keyLength] = '\0';
        (*ppIds)[index]           = cvkParseUint64Text(buffer);
    }
    return count;
}

/**
 * \brief Reads the pipeline state and its render-pass version.
 * \param[in] pContext Parsing context.
 * \param[in] root Document root.
 * \param[out] pPipeline Parsed pipeline and render pass.
 * \retval Other The graphics or compute state object.
 */
static CVK_FORCE_INLINE const cvkJsonValue* cvkReadPipelineJsonState(const cvkJsonContext* pContext,
                                                                   const cvkJsonValue* root,
                                                                   cvkPipelineJson* pPipeline)
{
    const cvkJsonValue* state;
    const cvkJsonValue* value;

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

    return state;
}

/**
 * \brief Parses a pipeline JSON document into its create infos; object IDs are retained separately from native handles.
 *
 * \par Details:
 *  Reads "GraphicsPipelineState" or "ComputePipelineState" with its "Renderpass" (version
 *  from the sType), "DescriptorSetLayouts", "PipelineLayout", "GraphicsPipeline" or
 *  "ComputePipeline", "ShaderFileNames", "Samplers" or "ImmutableSamplers", and
 *  "YcbcrSamplers"; then "EnabledExtensions" and "PipelineUUID". Handles are parsed through
 *  the context's resolver; with none, native handles stay null and pHandleReferences retains their IDs.
 *
 * \param[in]  pContext   Parsing context whose document is the pipeline JSON
 * \param[out] pPipeline  Receives the parsed pipeline description
 *
 * \retval None
 */
void cvkParsePipelineJson(const cvkJsonContext* pContext, cvkPipelineJson* pPipeline)
{
    cvkJsonContext      context;
    const cvkJsonContext* parsing;
    const cvkJsonValue* root;
    const cvkJsonValue* state;
    const cvkJsonValue* value;
    const cvkJsonValue* list;
    void*               elements;
    uint32_t            index;

    (void)memset(pPipeline, 0, sizeof(*pPipeline));
    context                    = *pContext;
    context.ppHandleReferences = &pPipeline->pHandleReferences;
    parsing                    = &context;
    root                       = &parsing->document.pValues[0];
    state = cvkReadPipelineJsonState(parsing, root, pPipeline);

    /* objects by id */
    list                                 = cvkGetJsonMember(&parsing->document, state, "DescriptorSetLayouts");
    pPipeline->descriptorSetLayoutCount  = cvkParsePipelineJsonObjectList(parsing, list, sizeof(VkDescriptorSetLayoutCreateInfo), &pPipeline->pDescriptorSetLayoutIds, &elements);
    pPipeline->pDescriptorSetLayoutCreateInfos = (VkDescriptorSetLayoutCreateInfo*)elements;
    for (index = 0; index < pPipeline->descriptorSetLayoutCount; ++index)
    {
        cvkParseJsonDescriptorSetLayoutCreateInfo(parsing, cvkGetJsonElement(&parsing->document, cvkGetJsonElement(&parsing->document, list, index), 0), &pPipeline->pDescriptorSetLayoutCreateInfos[index]);
    }
    list = cvkGetJsonMember(&parsing->document, state, "Samplers");
    if (NULL == list)
    {
        list = cvkGetJsonMember(&parsing->document, state, "ImmutableSamplers");
    }
    pPipeline->samplerCount        = cvkParsePipelineJsonObjectList(parsing, list, sizeof(VkSamplerCreateInfo), &pPipeline->pSamplerIds, &elements);
    pPipeline->pSamplerCreateInfos = (VkSamplerCreateInfo*)elements;
    for (index = 0; index < pPipeline->samplerCount; ++index)
    {
        cvkParseJsonSamplerCreateInfo(parsing, cvkGetJsonElement(&parsing->document, cvkGetJsonElement(&parsing->document, list, index), 0), &pPipeline->pSamplerCreateInfos[index]);
    }
    list                                          = cvkGetJsonMember(&parsing->document, state, "YcbcrSamplers");
    pPipeline->samplerYcbcrConversionCount        = cvkParsePipelineJsonObjectList(parsing, list, sizeof(VkSamplerYcbcrConversionCreateInfo), &pPipeline->pSamplerYcbcrConversionIds, &elements);
    pPipeline->pSamplerYcbcrConversionCreateInfos = (VkSamplerYcbcrConversionCreateInfo*)elements;
    for (index = 0; index < pPipeline->samplerYcbcrConversionCount; ++index)
    {
        cvkParseJsonSamplerYcbcrConversionCreateInfo(parsing, cvkGetJsonElement(&parsing->document, cvkGetJsonElement(&parsing->document, list, index), 0), &pPipeline->pSamplerYcbcrConversionCreateInfos[index]);
    }

    /* the pipeline layout */
    value = cvkGetJsonMember(&parsing->document, state, "PipelineLayout");
    if (!cvkIsJsonNull(value))
    {
        pPipeline->hasPipelineLayout = VK_TRUE;
        cvkParseJsonPipelineLayoutCreateInfo(parsing, value, &pPipeline->pipelineLayoutCreateInfo);
    }

    /* shaders: stage and file name */
    list                       = cvkGetJsonMember(&parsing->document, state, "ShaderFileNames");
    pPipeline->shaderCount     = ((NULL != list) && (CVK_JSON_ARRAY == list->kind)) ? list->childCount : 0;
    pPipeline->pShaderStages   = (VkShaderStageFlagBits*)cvkAllocateArena(parsing->pArena, pPipeline->shaderCount * sizeof(VkShaderStageFlagBits), sizeof(VkShaderStageFlagBits));
    pPipeline->ppShaderFileNames = (const char**)cvkAllocateArena(parsing->pArena, pPipeline->shaderCount * sizeof(const char*), sizeof(const char*));
    for (index = 0; index < pPipeline->shaderCount; ++index)
    {
        const cvkJsonValue* shader = cvkGetJsonElement(&parsing->document, list, index);

        pPipeline->pShaderStages[index]     = cvkParseJsonShaderStageFlagBits(cvkGetJsonMember(&parsing->document, shader, "stage"));
        pPipeline->ppShaderFileNames[index] = cvkCopyJsonString(parsing->pArena, cvkGetJsonMember(&parsing->document, shader, "filename"));
    }

    /* document-level: extensions and the pipeline identifier */
    list                              = cvkGetJsonMember(&parsing->document, root, "EnabledExtensions");
    pPipeline->enabledExtensionCount  = ((NULL != list) && (CVK_JSON_ARRAY == list->kind)) ? list->childCount : 0;
    pPipeline->ppEnabledExtensionNames = (const char**)cvkAllocateArena(parsing->pArena, pPipeline->enabledExtensionCount * sizeof(const char*), sizeof(const char*));
    for (index = 0; index < pPipeline->enabledExtensionCount; ++index)
    {
        pPipeline->ppEnabledExtensionNames[index] = cvkCopyJsonString(parsing->pArena, cvkGetJsonElement(&parsing->document, list, index));
    }
    list = cvkGetJsonMember(&parsing->document, root, "PipelineUUID");
    if ((NULL != list) && (CVK_JSON_ARRAY == list->kind) && (VK_UUID_SIZE == list->childCount))
    {
        pPipeline->hasPipelineUuid = VK_TRUE;
        for (index = 0; index < VK_UUID_SIZE; ++index)
        {
            pPipeline->pipelineUuid[index] = (uint8_t)cvkGetJsonUint64(cvkGetJsonElement(&parsing->document, list, index));
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
 * \brief Finds an object ID in a pipeline object's ID table.
 * \param[in] count Number of entries.
 * \param[in] pIds Object IDs.
 * \param[in] id Requested ID.
 * \retval VK_INVALID No matching ID.
 * \retval Other The matching index into a typed handle table.
 */
uint32_t cvkGetPipelineJsonHandleIndex(uint32_t        count,
                                     const uint64_t* pIds,
                                     uint64_t        id)
{
    uint32_t index;
    uint32_t found;

    found = VK_INVALID;
    for (index = 0; index < count; ++index)
    {
        if (pIds[index] == id)
        {
            found = index;
            break;
        }
    }
    return found;
}

/**
 * \brief Copies descriptor bindings and resolves immutable sampler references.
 * \param[in] pPipeline Parsed description and retained object IDs.
 * \param[in] pObjects Previously created native samplers.
 * \param[in] pInfo Descriptor set layout description.
 * \param[in,out] pArena Caller-owned output storage.
 * \retval Other Resolved bindings, or NULL for an empty layout.
 */
static CVK_FORCE_INLINE VkDescriptorSetLayoutBinding* cvkResolvePipelineJsonBindings(const cvkPipelineJson* pPipeline,
                                                                                    const cvkPipelineJsonObjects* pObjects,
                                                                                    const VkDescriptorSetLayoutCreateInfo* pInfo,
                                                                                    cvkArena* pArena)
{
    VkDescriptorSetLayoutBinding* bindings;
    VkSampler* samplers;
    uint32_t inner;
    uint32_t sampler_index;
    uint32_t found;
    uint64_t object_id;

    bindings = (VkDescriptorSetLayoutBinding*)cvkAllocateArena(pArena,
                                                             pInfo->bindingCount * sizeof(*bindings),
                                                             CVK_JSON_ARENA_ALIGNMENT);
    for (inner = 0; inner < pInfo->bindingCount; ++inner)
    {
        bindings[inner] = pInfo->pBindings[inner];
        if (NULL != bindings[inner].pImmutableSamplers)
        {
            samplers = (VkSampler*)cvkAllocateArena(pArena, bindings[inner].descriptorCount * sizeof(VkSampler), CVK_JSON_ARENA_ALIGNMENT);

            for (sampler_index = 0; sampler_index < bindings[inner].descriptorCount; ++sampler_index)
            {
                object_id          = cvkGetJsonHandleId(pPipeline->pHandleReferences, (const void*)&pInfo->pBindings[inner].pImmutableSamplers[sampler_index]);
                found       = cvkGetPipelineJsonHandleIndex(pObjects->samplerCount, pObjects->samplerIds, object_id);
                samplers[sampler_index] = pInfo->pBindings[inner].pImmutableSamplers[sampler_index];
                if (VK_INVALID != found)
                {
                    samplers[sampler_index] = pObjects->samplers[found];
                }
            }
            bindings[inner].pImmutableSamplers = samplers;
        }
    }
    return bindings;
}

/**
 * \brief Copies one shader stage and resolves its retained module ID.
 * \param[in] pPipeline Parsed description and retained object IDs.
 * \param[in] resolveExternal Optional resolver for external native handles.
 * \param[in,out] pUser Resolver state.
 * \param[in] pSource Stage with its parse-time address intact.
 * \param[out] pStage Resolved stage copy.
 * \retval None
 */
static CVK_FORCE_INLINE void cvkResolvePipelineJsonShader(const cvkPipelineJson* pPipeline,
                                                          PFN_cvkResolveJsonHandle resolveExternal,
                                                          void* pUser,
                                                          const VkPipelineShaderStageCreateInfo* pSource,
                                                          VkPipelineShaderStageCreateInfo* pStage)
{
    uint64_t object_id;
    VkBool32 has_reference;
    VkBool32 has_resolver;
    VkBool32 can_resolve;

    *pStage       = *pSource;
    object_id     = cvkGetJsonHandleId(pPipeline->pHandleReferences, (const void*)&pSource->module);
    has_reference = 0 != object_id;
    has_resolver  = NULL != resolveExternal;
    can_resolve   = has_reference && has_resolver;
    if (can_resolve)
    {
        (void)resolveExternal(pUser,
                              "VkShaderModule",
                              object_id,
                              sizeof(VkShaderModule),
                              (void*)&pStage->module);
    }
}

static CVK_FORCE_INLINE VkRenderPass cvkResolvePipelineJsonRenderPass(const cvkPipelineJson* pPipeline,
                                                                     PFN_cvkResolveJsonHandle resolveExternal,
                                                                     void* pUser)
{
    uint64_t object_id;
    VkBool32 resolved;
    VkRenderPass render_pass = VK_NULL_HANDLE;

    if (NULL == resolveExternal)
    {
        return VK_NULL_HANDLE;
    }
    object_id = cvkGetJsonHandleId(pPipeline->pHandleReferences, (const void*)&pPipeline->graphicsPipelineCreateInfo.renderPass);
    if (0 == object_id)
    {
        return VK_NULL_HANDLE;
    }
    resolved = resolveExternal(pUser, "VkRenderPass", object_id, sizeof(VkRenderPass), (void*)&render_pass);
    CVK_ASSERT(resolved);
    CVK_ASSERT(VK_NULL_HANDLE != render_pass);
    return render_pass;
}

/**
 * \brief Creates the objects a parsed pipeline JSON describes, in dependency order, then the pipeline.
 *
 * \par Details:
 *  Ycbcr conversions, samplers, descriptor set layouts (immutable sampler ids resolved),
 *  the pipeline layout (set layout ids resolved), the render pass (version 1 or 2), and the
 *  pipeline with its layout and render pass resolved (an external render pass stays owned
 *  by the caller), its base pipeline cleared, and its
 *  shader modules resolved through the caller's resolver ("VkShaderModule", id), which
 *  returns VK_NULL_HANDLE where modules do not exist (Vulkan SC). Ids in extension chains
 *  are not resolved. The pipeline's pNext is pPipelineNext when given (Vulkan SC chains its
 *  VkPipelineOfflineCreateInfo there), else the file's.
 *
 * \param[in]  device           Logical device creating the objects
 * \param[in]  pipelineCache    Pipeline cache the pipeline is created from, or VK_NULL_HANDLE
 * \param[in]  pPipeline        The parsed pipeline JSON
 * \param[in]  pPipelineNext    Extension chain for the pipeline create info replacing the file's, or NULL
 * \param[in]  resolveExternal  Resolver for handles the file does not define (shader modules and render pass), or NULL
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
    uint32_t index;
    uint32_t inner;
    uint32_t found;
    uint64_t object_id;
    VkRenderPass render_pass;

    (void)memset(pObjects, 0, sizeof(*pObjects));
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
    }

    /* descriptor set layouts, immutable sampler ids resolved */
    pObjects->descriptorSetLayoutCount = pPipeline->descriptorSetLayoutCount;
    for (index = 0; index < pPipeline->descriptorSetLayoutCount; ++index)
    {
        const VkDescriptorSetLayoutCreateInfo* info     = &pPipeline->pDescriptorSetLayoutCreateInfos[index];
        VkDescriptorSetLayoutBinding* bindings;

        bindings = cvkResolvePipelineJsonBindings(pPipeline, pObjects, info, pArena);
        pObjects->descriptorSetLayoutIds[index] = pPipeline->pDescriptorSetLayoutIds[index];
        cvkCreateDescriptorSetLayout(device, info->pNext, info->flags, info->bindingCount, bindings, pAllocator, &pObjects->descriptorSetLayouts[index]);
    }

    /* pipeline layout, set layout ids resolved */
    if (pPipeline->hasPipelineLayout)
    {
        const VkPipelineLayoutCreateInfo* info    = &pPipeline->pipelineLayoutCreateInfo;
        VkDescriptorSetLayout*            layouts = (VkDescriptorSetLayout*)cvkAllocateArena(pArena, info->setLayoutCount * sizeof(VkDescriptorSetLayout), CVK_JSON_ARENA_ALIGNMENT);

        for (inner = 0; inner < info->setLayoutCount; ++inner)
        {
            object_id             = cvkGetJsonHandleId(pPipeline->pHandleReferences, (const void*)&info->pSetLayouts[inner]);
            found          = cvkGetPipelineJsonHandleIndex(pObjects->descriptorSetLayoutCount, pObjects->descriptorSetLayoutIds, object_id);
            layouts[inner] = info->pSetLayouts[inner];
            if (VK_INVALID != found)
            {
                layouts[inner] = pObjects->descriptorSetLayouts[found];
            }
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
        VkPipelineShaderStageCreateInfo*    stages = (VkPipelineShaderStageCreateInfo*)cvkAllocateArena(pArena, info->stageCount * sizeof(VkPipelineShaderStageCreateInfo), CVK_JSON_ARENA_ALIGNMENT);

        render_pass = pObjects->renderPass;
        if (VK_NULL_HANDLE == render_pass)
        {
            render_pass = cvkResolvePipelineJsonRenderPass(pPipeline, resolveExternal, pUser);
        }

        for (inner = 0; inner < info->stageCount; ++inner)
        {
            cvkResolvePipelineJsonShader(pPipeline,
                                         resolveExternal,
                                         pUser,
                                         &info->pStages[inner],
                                         &stages[inner]);
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
                                  render_pass,
                                  info->subpass,
                                  VK_NULL_HANDLE,
                                  -1,
                                  pAllocator,
                                  &pObjects->pipeline);
    }
    else
    {
        const VkComputePipelineCreateInfo* info  = &pPipeline->computePipelineCreateInfo;
        VkPipelineShaderStageCreateInfo    stage;

        cvkResolvePipelineJsonShader(pPipeline,
                                     resolveExternal,
                                     pUser,
                                     &info->stage,
                                     &stage);
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
 * \param[in]  resolveExternal    Resolver for shader modules and an external render pass, or NULL for VK_NULL_HANDLE modules
 * \param[in]  pUser              Passed to the resolver
 * \param[in]  pAllocator         Optional allocation callbacks, or NULL
 * \param[out] pPipeline          Receives the parsed description; numeric IDs are retained in pHandleReferences
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
    context.ppHandleReferences  = NULL;
    cvkParsePipelineJson(&context, pPipeline);
    cvkCreatePipelineJsonObjects(device, pipelineCache, pPipeline, NULL, resolveExternal, pUser, pArena, pAllocator, pObjects);
}

/**
 * \brief Handles supplied to a graphics pipeline JSON by the caller.
 * \par Profile: vulkan
 */
typedef struct cvkGraphicsPipelineJsonHandles
{
    uint64_t       vertexId;
    uint64_t       fragmentId;
    uint64_t       renderPassId;
    VkShaderModule vertex;
    VkShaderModule fragment;
    VkRenderPass   renderPass;
} cvkGraphicsPipelineJsonHandles;

/**
 * \brief Resolves caller-owned shader modules and an optional caller-owned render pass.
 * \par Profile: vulkan
 */
static CVK_FORCE_INLINE VkBool32 cvkResolveGraphicsPipelineJsonHandle(void* pUser,
                                                                     const char* pHandleType,
                                                                     uint64_t id,
                                                                     uint32_t handleByteCount,
                                                                     void* pHandle)
{
    cvkGraphicsPipelineJsonHandles* handles = (cvkGraphicsPipelineJsonHandles*)pUser;
    const char* shader_type = "VkShaderModule";
    const char* render_pass_type = "VkRenderPass";
    const char* left;
    const char* right;

    left = pHandleType;
    right = shader_type;
    while (*left && *left == *right)
    {
        ++left;
        ++right;
    }
    if (*left == *right && sizeof(VkShaderModule) == handleByteCount)
    {
        if (handles->vertexId == id)
        {
            *(VkShaderModule*)pHandle = handles->vertex;
            return VK_TRUE;
        }
        if (handles->fragmentId == id)
        {
            *(VkShaderModule*)pHandle = handles->fragment;
            return VK_TRUE;
        }
    }
    left = pHandleType;
    right = render_pass_type;
    while (*left && *left == *right)
    {
        ++left;
        ++right;
    }
    if (*left == *right && sizeof(VkRenderPass) == handleByteCount && handles->renderPassId == id && VK_NULL_HANDLE != handles->renderPass)
    {
        *(VkRenderPass*)pHandle = handles->renderPass;
        return VK_TRUE;
    }
    return VK_FALSE;
}

/**
 * \brief Creates a graphics pipeline from JSON using caller-owned vertex and fragment modules.
 * \par Profile: vulkan
 * \par Details:
 *  The module IDs are taken from the vertex and fragment stages in the JSON. The supplied
 *  modules and external render pass remain owned by the caller.
 * \param[in] device Logical device creating the objects.
 * \param[in] pipelineCache Pipeline cache, or VK_NULL_HANDLE.
 * \param[in] pJsonText Pipeline JSON text.
 * \param[in] jsonLength Byte length of the text.
 * \param[in] pJsonValues Storage for the document's values.
 * \param[in] jsonValueCapacity Capacity of pJsonValues.
 * \param[in] pArena Arena for parsed structures and resolved arrays.
 * \param[in] vertex Caller-owned vertex shader module.
 * \param[in] fragment Caller-owned fragment shader module.
 * \param[in] externalRenderPass Caller-owned render pass if the JSON omits one, or VK_NULL_HANDLE.
 * \param[in] pAllocator Optional allocation callbacks, or NULL.
 * \param[out] pObjects Receives the created JSON objects.
 * \retval None
 */
void cvkCreateGraphicsPipelineFromJsonWithModules(VkDevice                     device,
                                                   VkPipelineCache              pipelineCache,
                                                   const char*                  pJsonText,
                                                   size_t                       jsonLength,
                                                   cvkJsonValue*                pJsonValues,
                                                   uint32_t                     jsonValueCapacity,
                                                   cvkArena*                    pArena,
                                                   VkShaderModule               vertex,
                                                   VkShaderModule               fragment,
                                                   VkRenderPass                 externalRenderPass,
                                                   const VkAllocationCallbacks* pAllocator,
                                                   cvkPipelineJsonObjects*      pObjects)
{
    cvkJsonContext context;
    cvkPipelineJson pipeline;
    cvkGraphicsPipelineJsonHandles handles = {0};
    uint32_t index;

    context.document.pValues = pJsonValues;
    context.document.valueCount = cvkParseJsonDocument(pJsonText, jsonLength, pJsonValues, jsonValueCapacity);
    context.pArena = pArena;
    context.resolveHandle = NULL;
    context.pUser = NULL;
    context.ppHandleReferences = NULL;
    cvkParsePipelineJson(&context, &pipeline);
    CVK_ASSERT(VK_PIPELINE_BIND_POINT_GRAPHICS == pipeline.bindPoint);
    CVK_ASSERT(2 == pipeline.graphicsPipelineCreateInfo.stageCount);
    for (index = 0; index < pipeline.graphicsPipelineCreateInfo.stageCount; ++index)
    {
        const VkPipelineShaderStageCreateInfo* stage = &pipeline.graphicsPipelineCreateInfo.pStages[index];
        uint64_t module_id = cvkGetJsonHandleId(pipeline.pHandleReferences, (const void*)&stage->module);

        CVK_ASSERT(0 != module_id);
        if (VK_SHADER_STAGE_VERTEX_BIT == stage->stage)
        {
            CVK_ASSERT(0 == handles.vertexId);
            handles.vertexId = module_id;
        }
        else if (VK_SHADER_STAGE_FRAGMENT_BIT == stage->stage)
        {
            CVK_ASSERT(0 == handles.fragmentId);
            handles.fragmentId = module_id;
        }
        else
        {
            CVK_ASSERT(VK_FALSE);
        }
    }
    CVK_ASSERT(0 != handles.vertexId);
    CVK_ASSERT(0 != handles.fragmentId);
    CVK_ASSERT(handles.vertexId != handles.fragmentId);
    handles.renderPassId = cvkGetJsonHandleId(pipeline.pHandleReferences, (const void*)&pipeline.graphicsPipelineCreateInfo.renderPass);
    handles.vertex = vertex;
    handles.fragment = fragment;
    handles.renderPass = externalRenderPass;
    cvkCreatePipelineJsonObjects(device, pipelineCache, &pipeline, NULL, cvkResolveGraphicsPipelineJsonHandle, &handles, pArena, pAllocator, pObjects);
}

/**
 * \brief Creates a graphics pipeline from JSON and temporary vertex and fragment SPIR-V modules.
 * \par Profile: vulkan
 * \par Details:
 *  The shader modules are destroyed after pipeline creation. Use
 *  cvkCreateGraphicsPipelineFromJsonWithModules to retain or reuse caller-owned modules.
 * \param[in] device Logical device creating the objects.
 * \param[in] pipelineCache Pipeline cache, or VK_NULL_HANDLE.
 * \param[in] pJsonText Pipeline JSON text.
 * \param[in] jsonLength Byte length of the text.
 * \param[in] pJsonValues Storage for the document's values.
 * \param[in] jsonValueCapacity Capacity of pJsonValues.
 * \param[in] pArena Arena for parsed structures and resolved arrays.
 * \param[in] vertexCodeSize Vertex SPIR-V byte length.
 * \param[in] pVertexCode Vertex SPIR-V words.
 * \param[in] fragmentCodeSize Fragment SPIR-V byte length.
 * \param[in] pFragmentCode Fragment SPIR-V words.
 * \param[in] externalRenderPass Caller-owned render pass if the JSON omits one, or VK_NULL_HANDLE.
 * \param[in] pAllocator Optional allocation callbacks, or NULL.
 * \param[out] pObjects Receives the created JSON objects.
 * \retval None
 */
void cvkCreateGraphicsPipelineFromJson(VkDevice                     device,
                                       VkPipelineCache              pipelineCache,
                                       const char*                  pJsonText,
                                       size_t                       jsonLength,
                                       cvkJsonValue*                pJsonValues,
                                       uint32_t                     jsonValueCapacity,
                                       cvkArena*                    pArena,
                                       size_t                       vertexCodeSize,
                                       const uint32_t*              pVertexCode,
                                       size_t                       fragmentCodeSize,
                                       const uint32_t*              pFragmentCode,
                                       VkRenderPass                 externalRenderPass,
                                       const VkAllocationCallbacks* pAllocator,
                                       cvkPipelineJsonObjects*      pObjects)
{
    VkShaderModule vertex;
    VkShaderModule fragment;

    cvkCreateShaderModule(device, NULL, 0, vertexCodeSize, pVertexCode, pAllocator, &vertex);
    cvkCreateShaderModule(device, NULL, 0, fragmentCodeSize, pFragmentCode, pAllocator, &fragment);
    cvkCreateGraphicsPipelineFromJsonWithModules(device,
                                                 pipelineCache,
                                                 pJsonText,
                                                 jsonLength,
                                                 pJsonValues,
                                                 jsonValueCapacity,
                                                 pArena,
                                                 vertex,
                                                 fragment,
                                                 externalRenderPass,
                                                 pAllocator,
                                                 pObjects);
    cvkDestroyShaderModule(device, fragment, pAllocator);
    cvkDestroyShaderModule(device, vertex, pAllocator);
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
 * \param[out] pPipeline          Receives the parsed description; numeric IDs are retained in pHandleReferences
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
    context.ppHandleReferences  = NULL;
    cvkParsePipelineJson(&context, pPipeline);
    CVK_ASSERT(pPipeline->hasPipelineUuid);
    file_next = (VK_PIPELINE_BIND_POINT_GRAPHICS == pPipeline->bindPoint) ? pPipeline->graphicsPipelineCreateInfo.pNext : pPipeline->computePipelineCreateInfo.pNext;
    cvkInitPipelineOfflineCreateInfo(&pipeline_offline_create_info, file_next, pPipeline->pipelineUuid, matchControl, poolEntrySize);
    cvkCreatePipelineJsonObjects(device, pipelineCache, pPipeline, &pipeline_offline_create_info, NULL, NULL, pArena, pAllocator, pObjects);
}
