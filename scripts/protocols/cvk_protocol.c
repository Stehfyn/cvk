/* Tier 3 protocols (SPEC.md section 8b): source of truth kept with the generator and folded
** into the generated cvk.h/.c. Composed from the generated surface only. */

/**
 * \brief Acquires an externally owned image and begins a render pass using it.
 * \param[in] commandBuffer Command buffer recording the pass.
 * \param[in] pBarrierNext Optional image barrier extension chain.
 * \param[in] dstAccessMask Access required by the render pass.
 * \param[in] externalLayout Layout held by the external owner.
 * \param[in] renderLayout Layout required when the render pass begins.
 * \param[in] queueFamilyIndex Queue family recording the pass.
 * \param[in] image Externally owned attachment image.
 * \param[in] subresourceRange Attachment subresources to acquire.
 * \param[in] dstStageMask Stage requiring the acquired image.
 * \param[in] dependencyFlags Image barrier dependency flags.
 * \param[in] pRenderPassNext Optional render pass begin extension chain.
 * \param[in] renderPass Render pass to begin.
 * \param[in] framebuffer Target framebuffer.
 * \param[in] renderArea Render area.
 * \param[in] clearValueCount Number of clear values.
 * \param[in] pClearValues Clear values, or NULL when the count is zero.
 * \param[in] contents Subpass contents mode.
 * \retval None
 */
void cvkCmdBeginRenderPassOnExternalImage(VkCommandBuffer         commandBuffer,
                                          const void*             pBarrierNext,
                                          VkAccessFlags           dstAccessMask,
                                          VkImageLayout           externalLayout,
                                          VkImageLayout           renderLayout,
                                          uint32_t                queueFamilyIndex,
                                          VkImage                 image,
                                          VkImageSubresourceRange subresourceRange,
                                          VkPipelineStageFlags    dstStageMask,
                                          VkDependencyFlags       dependencyFlags,
                                          const void*             pRenderPassNext,
                                          VkRenderPass            renderPass,
                                          VkFramebuffer           framebuffer,
                                          VkRect2D                renderArea,
                                          uint32_t                clearValueCount,
                                          const VkClearValue*     pClearValues,
                                          VkSubpassContents       contents)
{
    cvkCmdAcquireImageFromExternal(commandBuffer,
                                   pBarrierNext,
                                   dstAccessMask,
                                   externalLayout,
                                   renderLayout,
                                   queueFamilyIndex,
                                   image,
                                   subresourceRange,
                                   dstStageMask,
                                   dependencyFlags);
    cvkCmdBeginRenderPass(commandBuffer,
                          pRenderPassNext,
                          renderPass,
                          framebuffer,
                          renderArea,
                          clearValueCount,
                          pClearValues,
                          contents);
}

/**
 * \brief Ends a render pass and releases its attachment image to an external owner.
 * \param[in] commandBuffer Command buffer recording the pass.
 * \param[in] pBarrierNext Optional image barrier extension chain.
 * \param[in] srcAccessMask Access performed by the render pass.
 * \param[in] renderLayout Layout left by the render pass.
 * \param[in] externalLayout Layout required by the external owner.
 * \param[in] queueFamilyIndex Queue family recording the pass.
 * \param[in] image Externally owned attachment image.
 * \param[in] subresourceRange Attachment subresources to release.
 * \param[in] srcStageMask Stage that accessed the image.
 * \param[in] dependencyFlags Image barrier dependency flags.
 * \retval None
 */
void cvkCmdEndRenderPassOnExternalImage(VkCommandBuffer         commandBuffer,
                                        const void*             pBarrierNext,
                                        VkAccessFlags           srcAccessMask,
                                        VkImageLayout           renderLayout,
                                        VkImageLayout           externalLayout,
                                        uint32_t                queueFamilyIndex,
                                        VkImage                 image,
                                        VkImageSubresourceRange subresourceRange,
                                        VkPipelineStageFlags    srcStageMask,
                                        VkDependencyFlags       dependencyFlags)
{
    cvkCmdEndRenderPass(commandBuffer);
    cvkCmdReleaseImageToExternal(commandBuffer,
                                 pBarrierNext,
                                 srcAccessMask,
                                 renderLayout,
                                 externalLayout,
                                 queueFamilyIndex,
                                 image,
                                 subresourceRange,
                                 srcStageMask,
                                 dependencyFlags);
}

/**
 * \brief Acquires an external color image, draws a fullscreen quad, and releases it.
 * \param[in] commandBuffer Command buffer recording the pass.
 * \param[in] externalLayout Layout held by the external owner before and after the pass.
 * \param[in] renderLayout Color attachment layout during the pass.
 * \param[in] queueFamilyIndex Queue family recording the pass.
 * \param[in] image Externally owned color attachment image.
 * \param[in] subresourceRange Color attachment subresources.
 * \param[in] renderPass Render pass to execute.
 * \param[in] framebuffer Target framebuffer.
 * \param[in] extent Render area, viewport, and scissor extent.
 * \param[in] pipeline Fullscreen triangle-strip pipeline.
 * \param[in] layout Pipeline layout for the descriptor set.
 * \param[in] descriptorSet Descriptor set bound at set zero.
 * \retval None
 */
void cvkCmdExecuteFullscreenQuadRenderPassOnExternalImage(VkCommandBuffer         commandBuffer,
                                                          VkImageLayout           externalLayout,
                                                          VkImageLayout           renderLayout,
                                                          uint32_t                queueFamilyIndex,
                                                          VkImage                 image,
                                                          VkImageSubresourceRange subresourceRange,
                                                          VkRenderPass            renderPass,
                                                          VkFramebuffer           framebuffer,
                                                          VkExtent2D              extent,
                                                          VkPipeline              pipeline,
                                                          VkPipelineLayout        layout,
                                                          VkDescriptorSet         descriptorSet)
{
    VkOffset2D offset;
    VkRect2D   area;

    cvkInitOffset2D(&offset, 0, 0);
    cvkInitRect2D(&area, offset, extent);

    cvkCmdBeginRenderPassOnExternalImage(commandBuffer,
                                         NULL,
                                         (VkAccessFlags)VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                         externalLayout,
                                         renderLayout,
                                         queueFamilyIndex,
                                         image,
                                         subresourceRange,
                                         (VkPipelineStageFlags)VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                         (VkDependencyFlags)0,
                                         NULL,
                                         renderPass,
                                         framebuffer,
                                         area,
                                         0,
                                         NULL,
                                         VK_SUBPASS_CONTENTS_INLINE);
    cvkCmdSetViewportAndScissor2D(commandBuffer, extent);
    cvkCmdBindGraphicsPipeline(commandBuffer, pipeline);
    cvkCmdBindGraphicsDescriptorSets(commandBuffer,
                                     layout,
                                     0,
                                     1,
                                     &descriptorSet,
                                     0,
                                     NULL);
    cvkCmdDrawFullscreenQuad(commandBuffer);
    cvkCmdEndRenderPassOnExternalImage(commandBuffer,
                                       NULL,
                                       (VkAccessFlags)VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                                       renderLayout,
                                       externalLayout,
                                       queueFamilyIndex,
                                       image,
                                       subresourceRange,
                                       (VkPipelineStageFlags)VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                       (VkDependencyFlags)0);
}

/**
 * \brief Records a graphics render pass that draws non-indexed vertices without push constants.
 * \param[in] commandBuffer Command buffer recording the pass.
 * \param[in] renderPass Render pass to begin.
 * \param[in] framebuffer Target framebuffer.
 * \param[in] extent Render area, viewport and scissor extent.
 * \param[in] pipeline Graphics pipeline to bind.
 * \param[in] layout Pipeline layout for the descriptor set.
 * \param[in] descriptorSet Descriptor set bound at set zero.
 * \param[in] vertexCount Number of vertices to draw.
 * \retval None
 */
void cvkCmdExecuteGraphicsRenderPass(VkCommandBuffer  commandBuffer,
                                  VkRenderPass     renderPass,
                                  VkFramebuffer    framebuffer,
                                  VkExtent2D       extent,
                                  VkPipeline       pipeline,
                                  VkPipelineLayout layout,
                                  VkDescriptorSet  descriptorSet,
                                  uint32_t         vertexCount)
{
    VkOffset2D offset;
    VkRect2D   area;

    cvkInitOffset2D(&offset, 0, 0);
    cvkInitRect2D(&area, offset, extent);

    cvkCmdBeginRenderPass(commandBuffer,
                          NULL,
                          renderPass,
                          framebuffer,
                          area,
                          0,
                          NULL,
                          VK_SUBPASS_CONTENTS_INLINE);
    cvkCmdSetViewportAndScissor2D(commandBuffer, extent);
    cvkCmdBindGraphicsPipeline(commandBuffer, pipeline);
    cvkCmdBindGraphicsDescriptorSets(commandBuffer,
                                     layout,
                                     0,
                                     1,
                                     &descriptorSet,
                                     0,
                                     NULL);
    cvkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
    cvkCmdEndRenderPass(commandBuffer);
}

/**
 * \brief Records a graphics render pass that pushes fragment constants and draws non-indexed vertices.
 * \param[in] commandBuffer Command buffer recording the pass.
 * \param[in] renderPass Render pass to begin.
 * \param[in] framebuffer Target framebuffer.
 * \param[in] extent Render area, viewport and scissor extent.
 * \param[in] pipeline Graphics pipeline to bind.
 * \param[in] layout Pipeline layout for the descriptor set and push constants.
 * \param[in] descriptorSet Descriptor set bound at set zero.
 * \param[in] vertexCount Number of vertices to draw.
 * \param[in] pushConstantSize Size of the fragment push constants.
 * \param[in] pPushConstants Fragment push constants.
 * \retval None
 */
void cvkCmdExecuteGraphicsRenderPassWithPushConstants(VkCommandBuffer  commandBuffer,
                                                   VkRenderPass     renderPass,
                                                   VkFramebuffer    framebuffer,
                                                   VkExtent2D       extent,
                                                   VkPipeline       pipeline,
                                                   VkPipelineLayout layout,
                                                   VkDescriptorSet  descriptorSet,
                                                   uint32_t         vertexCount,
                                                   uint32_t         pushConstantSize,
                                                   const void*      pPushConstants)
{
    VkOffset2D offset;
    VkRect2D   area;

    cvkInitOffset2D(&offset, 0, 0);
    cvkInitRect2D(&area, offset, extent);

    cvkCmdBeginRenderPass(commandBuffer,
                          NULL,
                          renderPass,
                          framebuffer,
                          area,
                          0,
                          NULL,
                          VK_SUBPASS_CONTENTS_INLINE);
    cvkCmdSetViewportAndScissor2D(commandBuffer, extent);
    cvkCmdDrawWithPushConstants(commandBuffer,
                                pipeline,
                                layout,
                                0,
                                1,
                                &descriptorSet,
                                0,
                                NULL,
                                vertexCount,
                                1,
                                0,
                                0,
                                0,
                                pushConstantSize,
                                pPushConstants);
    cvkCmdEndRenderPass(commandBuffer);
}


/**
 * \brief VK_EXT_debug_utils callback that prints one message to stderr and never aborts the call.
 *
 * \par Details:
 *  Prints the severity and type masks in hexadecimal, the message identifier name when present,
 *  and the message text. Suitable as pfnUserCallback for cvkCreateDebugInstance.
 *
 * \param[in] messageSeverity  Severity bit of the message
 * \param[in] messageTypes     General, validation, and performance type mask
 * \param[in] pCallbackData    Message identifier and text supplied by the loader, or NULL
 * \param[in] pUserData        Unused application context
 *
 * \retval VK_FALSE The triggering Vulkan call is never aborted.
 */
VKAPI_ATTR VkBool32 VKAPI_CALL cvkWriteDebugUtilsMessage(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                         VkDebugUtilsMessageTypeFlagsEXT             messageTypes,
                                                         const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                         void*                                       pUserData)
{
    const char* message_id_name;
    const char* message;

    (void)pUserData;
    message_id_name = ((NULL != pCallbackData) && (NULL != pCallbackData->pMessageIdName)) ? pCallbackData->pMessageIdName : "";
    message         = ((NULL != pCallbackData) && (NULL != pCallbackData->pMessage)) ? pCallbackData->pMessage : "";
    cvkDiagnosticWrite("vulkan severity 0x");
    cvkDiagnosticWriteHex((uint32_t)messageSeverity);
    cvkDiagnosticWrite(" type 0x");
    cvkDiagnosticWriteHex((uint32_t)messageTypes);
    cvkDiagnosticWrite(" ");
    cvkDiagnosticWrite(message_id_name);
    cvkDiagnosticWrite(": ");
    cvkDiagnosticWrite(message);
    cvkDiagnosticWrite("\n");
    return VK_FALSE;
}

/**
 * \brief Creates an instance with VK_EXT_debug_utils enabled and a messenger receiving every message, creation included.
 *
 * \par Details:
 *  Appends VK_EXT_debug_utils to the requested extensions, asserts the instance supports every
 *  resulting extension, creates the instance with the messenger create info chained so that
 *  messages from instance creation itself are delivered, then creates the messenger that
 *  receives all later messages. The instance and messenger are the caller's to destroy.
 *
 * \param[in]  pNext                    Optional extension chain for VkInstanceCreateInfo, or NULL
 * \param[in]  flags                    VkInstanceCreateInfo::flags
 * \param[in]  pApplicationInfo         Optional application and engine metadata, or NULL
 * \param[in]  enabledLayerCount        Number of names in ppEnabledLayerNames
 * \param[in]  ppEnabledLayerNames      Layer names to enable, or NULL when the count is zero
 * \param[in]  enabledExtensionCount    Number of names in ppEnabledExtensionNames, below CVK_MAX_INSTANCE_EXTENSIONS
 * \param[in]  ppEnabledExtensionNames  Extension names to enable, or NULL when the count is zero
 * \param[in]  messageSeverity          Severities delivered to the callback
 * \param[in]  messageType              Types delivered to the callback
 * \param[in]  pfnUserCallback          Callback receiving the messages, for example cvkWriteDebugUtilsMessage
 * \param[in]  pUserData                Context passed to the callback
 * \param[in]  pAllocator               Optional allocation callbacks, or NULL
 * \param[out] pProperties              Caller-owned CVK_MAX_PROPERTIES enumeration workspace
 * \param[out] pInstance                Receives the created VkInstance
 * \param[out] pMessenger               Receives the created VkDebugUtilsMessengerEXT
 *
 * \retval None
 */
void cvkCreateDebugInstance(const void*                          pNext,
                            VkInstanceCreateFlags                flags,
                            const VkApplicationInfo*             pApplicationInfo,
                            uint32_t                             enabledLayerCount,
                            const char* const*                   ppEnabledLayerNames,
                            uint32_t                             enabledExtensionCount,
                            const char* const*                   ppEnabledExtensionNames,
                            VkDebugUtilsMessageSeverityFlagsEXT  messageSeverity,
                            VkDebugUtilsMessageTypeFlagsEXT      messageType,
                            PFN_vkDebugUtilsMessengerCallbackEXT pfnUserCallback,
                            void*                                pUserData,
                            const VkAllocationCallbacks*         pAllocator,
                            VkExtensionProperties                pProperties[CVK_MAX_PROPERTIES],
                            VkInstance*                          pInstance,
                            VkDebugUtilsMessengerEXT*            pMessenger)
{
    const char* extensions[CVK_MAX_INSTANCE_EXTENSIONS];
    uint32_t    extension_count;
    uint32_t    extension_index;
    VkBool32    supported;

    /* the debug utilities extension joins the requested set */
    CVK_ASSERT(CVK_MAX_INSTANCE_EXTENSIONS > enabledExtensionCount);
    for (extension_index = 0; extension_index < enabledExtensionCount; ++extension_index)
    {
        extensions[extension_index] = ppEnabledExtensionNames[extension_index];
    }
    extension_count               = enabledExtensionCount;
    extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    /* capability: every extension must be supported; nothing is dropped silently */
    supported = cvkGetInstanceExtensionSupport(NULL, extension_count, extensions, pProperties);
    CVK_ASSERT(supported);

    /* create with the messenger info chained, so creation itself is reported */
    cvkCreateInstanceWithDebugUtilsMessengerInfo(pNext,
                                                 (VkDebugUtilsMessengerCreateFlagsEXT)0,
                                                 messageSeverity,
                                                 messageType,
                                                 pfnUserCallback,
                                                 pUserData,
                                                 flags,
                                                 pApplicationInfo,
                                                 enabledLayerCount,
                                                 ppEnabledLayerNames,
                                                 extension_count,
                                                 extensions,
                                                 pAllocator,
                                                 pInstance);

    /* the messenger for every later message */
    cvkCreateDebugUtilsMessenger((*pInstance), NULL, (VkDebugUtilsMessengerCreateFlagsEXT)0, messageSeverity, messageType, pfnUserCallback, pUserData, pAllocator, pMessenger);
}

/**
 * \brief Finds the physical device whose locally unique identifier matches.
 *
 * \par Details:
 *  Enumerates the instance's physical devices and compares each device's LUID from
 *  VkPhysicalDeviceIDProperties when the device reports one as valid.
 *
 * \param[in] instance  Instance whose physical devices are searched
 * \param[in] pLuid     Locally unique identifier of VK_LUID_SIZE bytes
 *
 * \return The matching physical device, or VK_NULL_HANDLE when none matches.
 */
VkPhysicalDevice cvkGetPhysicalDeviceByLuid(VkInstance instance, const uint8_t pLuid[VK_LUID_SIZE])
{
    VkPhysicalDevice             physical_devices[CVK_MAX_PHYSICAL_DEVICES];
    VkPhysicalDeviceIDProperties physical_device_id_properties;
    uint32_t                     physical_device_count;
    uint32_t                     physical_device_index;

    cvkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices);
    for (physical_device_index = 0; physical_device_index < physical_device_count; ++physical_device_index)
    {
        cvkGetPhysicalDeviceIDProperties(physical_devices[physical_device_index], NULL, &physical_device_id_properties);
        if (physical_device_id_properties.deviceLUIDValid && (0 == memcmp(physical_device_id_properties.deviceLUID, pLuid, VK_LUID_SIZE)))
        {
            return physical_devices[physical_device_index];
        }
    }
    return VK_NULL_HANDLE;
}

/**
 * \brief Finds the physical device at a PCI address.
 *
 * \par Details:
 *  Enumerates the instance's physical devices and, for each device supporting
 *  VK_EXT_pci_bus_info, compares its domain, bus, device, and function.
 *
 * \param[in] instance     Instance whose physical devices are searched
 * \param[in] pciDomain    PCI domain of the adapter
 * \param[in] pciBus       PCI bus of the adapter
 * \param[in] pciDevice    PCI device of the adapter
 * \param[in] pciFunction  PCI function of the adapter
 * \param[out] pProperties Caller-owned CVK_MAX_PROPERTIES enumeration workspace, reused per device
 *
 * \return The matching physical device, or VK_NULL_HANDLE when none matches.
 */
VkPhysicalDevice cvkGetPhysicalDeviceByPciBusInfo(VkInstance instance,
                                               uint32_t pciDomain,
                                               uint32_t pciBus,
                                               uint32_t pciDevice,
                                               uint32_t pciFunction,
                                               VkExtensionProperties pProperties[CVK_MAX_PROPERTIES])
{
    VkPhysicalDevice                        physical_devices[CVK_MAX_PHYSICAL_DEVICES];
    VkPhysicalDevicePCIBusInfoPropertiesEXT physical_device_pci_bus_info_properties;
    uint32_t                                physical_device_count;
    uint32_t                                physical_device_index;

    cvkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices);
    for (physical_device_index = 0; physical_device_index < physical_device_count; ++physical_device_index)
    {
        if (cvkGetPhysicalDeviceExtensionSupport(physical_devices[physical_device_index], NULL, 1, (const char* const[]){ VK_EXT_PCI_BUS_INFO_EXTENSION_NAME }, pProperties))
        {
            cvkGetPhysicalDevicePCIBusInfoProperties(physical_devices[physical_device_index], NULL, &physical_device_pci_bus_info_properties);
            if ((pciDomain == physical_device_pci_bus_info_properties.pciDomain) && (pciBus == physical_device_pci_bus_info_properties.pciBus) &&
                (pciDevice == physical_device_pci_bus_info_properties.pciDevice) && (pciFunction == physical_device_pci_bus_info_properties.pciFunction))
            {
                return physical_devices[physical_device_index];
            }
        }
    }
    return VK_NULL_HANDLE;
}

/**
 * \brief Allocates and binds one memory object per memory requirement of a video session.
 *
 * \par Details:
 *  The session form of cvkAllocateImageMemory: the session reports one requirement per
 *  memory bind index; each gets a compatible memory type, its own allocation covering the
 *  whole requirement, and a bind at offset zero. All binds are submitted in one call.
 *
 * \param[in]  physicalDevice          Physical device the memory is selected on
 * \param[in]  device                  Logical device owning the session and memory
 * \param[in]  videoSession            Session to back
 * \param[in]  requiredProperties      Memory property flags every selected type must cover
 * \param[in]  pMemoryNext             Optional extension chain for every VkMemoryAllocateInfo, or NULL
 * \param[in]  pAllocator              Optional allocation callbacks, or NULL
 * \param[out] pMemories               Receives one VkDeviceMemory per requirement; up to CVK_MAX_MEMORY_REQUIREMENTS entries
 *
 * \retval Number of memory objects allocated and bound
 */
uint32_t cvkAllocateVideoSessionMemory(VkPhysicalDevice             physicalDevice,
                                       VkDevice                     device,
                                       VkVideoSessionKHR            videoSession,
                                       VkMemoryPropertyFlags        requiredProperties,
                                       const void*                  pMemoryNext,
                                       const VkAllocationCallbacks* pAllocator,
                                       VkDeviceMemory*              pMemories)
{
    VkVideoSessionMemoryRequirementsKHR memory_requirements[CVK_MAX_MEMORY_REQUIREMENTS];
    VkBindVideoSessionMemoryInfoKHR     bind_session_memory_infos[CVK_MAX_MEMORY_REQUIREMENTS];
    uint32_t                            memory_requirements_count;
    uint32_t                            requirement_index;
    uint32_t                            memory_type_index;

    cvkGetVideoSessionMemoryRequirements(device, videoSession, &memory_requirements_count, memory_requirements);
    for (requirement_index = 0; requirement_index < memory_requirements_count; ++requirement_index)
    {
        memory_type_index = cvkGetPhysicalDeviceCompatibleMemoryTypeIndex(physicalDevice, memory_requirements[requirement_index].memoryRequirements.memoryTypeBits, requiredProperties);
        CVK_ASSERT(VK_INVALID != memory_type_index);
        cvkAllocateMemory(device, pMemoryNext, memory_requirements[requirement_index].memoryRequirements.size, memory_type_index, pAllocator, &pMemories[requirement_index]);
        cvkInitBindVideoSessionMemoryInfo(&bind_session_memory_infos[requirement_index],
                                          NULL,
                                          memory_requirements[requirement_index].memoryBindIndex,
                                          pMemories[requirement_index],
                                          0,
                                          memory_requirements[requirement_index].memoryRequirements.size);
    }
    cvkBindVideoSessionMemory(device, videoSession, memory_requirements_count, bind_session_memory_infos);
    return memory_requirements_count;
}

/**
 * \brief Creates an acceleration structure sized for a build description, with its storage and an aligned scratch buffer.
 *
 * \par Details:
 *  Queries the build sizes for a device build of the geometries, creates the storage buffer
 *  and the structure over it, and a scratch buffer with room to align its device address to
 *  the scratch offset alignment. The build itself is recorded with
 *  cvkCmdBuildDeviceAccelerationStructure using the same geometries and the scratch address.
 *
 * \param[in]  physicalDevice          Physical device the memory is selected on
 * \param[in]  device                  Logical device owning the objects
 * \param[in]  type                    Bottom or top level
 * \param[in]  buildFlags              VkAccelerationStructureBuildGeometryInfoKHR::flags of the build the structure is sized for
 * \param[in]  geometryCount           Number of geometries
 * \param[in]  pGeometries             The geometries; the same array is given to the build
 * \param[in]  pMaxPrimitiveCounts     Maximum primitive count per geometry
 * \param[in]  pAllocator              Optional allocation callbacks, or NULL
 * \param[out] pBuffer                 Receives the storage buffer
 * \param[out] pMemory                 Receives the storage memory
 * \param[out] pAccelerationStructure  Receives the acceleration structure
 * \param[out] pScratchBuffer          Receives the scratch buffer
 * \param[out] pScratchMemory          Receives the scratch memory
 * \param[out] pScratchDeviceAddress   Receives the aligned scratch device address for the build
 *
 * \retval None
 */
void cvkSetupAccelerationStructure(VkPhysicalDevice                         physicalDevice,
                                   VkDevice                                 device,
                                   VkAccelerationStructureTypeKHR           type,
                                   VkBuildAccelerationStructureFlagsKHR     buildFlags,
                                   uint32_t                                 geometryCount,
                                   const VkAccelerationStructureGeometryKHR* pGeometries,
                                   const uint32_t*                          pMaxPrimitiveCounts,
                                   const VkAllocationCallbacks*             pAllocator,
                                   VkBuffer*                                pBuffer,
                                   VkDeviceMemory*                          pMemory,
                                   VkAccelerationStructureKHR*              pAccelerationStructure,
                                   VkBuffer*                                pScratchBuffer,
                                   VkDeviceMemory*                          pScratchMemory,
                                   VkDeviceAddress*                         pScratchDeviceAddress)
{
    VkPhysicalDeviceAccelerationStructurePropertiesKHR acceleration_structure_properties;
    VkDeviceOrHostAddressKHR                           scratch_data;
    VkAccelerationStructureBuildGeometryInfoKHR        build_geometry_info;
    VkAccelerationStructureBuildSizesInfoKHR           build_sizes_info;
    VkDeviceSize                                       scratch_alignment;

    cvkGetPhysicalDeviceAccelerationStructureProperties(physicalDevice, NULL, &acceleration_structure_properties);
    scratch_alignment = acceleration_structure_properties.minAccelerationStructureScratchOffsetAlignment;

    /* sizes: the build description with no destination and no scratch yet */
    cvkInitDeviceOrHostAddressDeviceAddress(&scratch_data, 0);
    cvkInitAccelerationStructureBuildGeometryInfo(&build_geometry_info, NULL, type, buildFlags, VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR, VK_NULL_HANDLE, VK_NULL_HANDLE, geometryCount, pGeometries, NULL, scratch_data);
    cvkGetAccelerationStructureBuildSizes(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_geometry_info, pMaxPrimitiveCounts, NULL, &build_sizes_info);

    /* storage and structure */
    cvkSetupAccelerationStructureBuffer(physicalDevice, device, NULL, (VkBufferCreateFlags)0, build_sizes_info.accelerationStructureSize, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, NULL, pAllocator, pBuffer, pMemory);
    cvkCreateAccelerationStructure(device, NULL, (VkAccelerationStructureCreateFlagsKHR)0, (*pBuffer), 0, build_sizes_info.accelerationStructureSize, type, 0, pAllocator, pAccelerationStructure);

    /* scratch, with room to align its address */
    cvkSetupScratchBuffer(physicalDevice, device, NULL, (VkBufferCreateFlags)0, build_sizes_info.buildScratchSize + scratch_alignment, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, NULL, pAllocator, pScratchBuffer, pScratchMemory);
    (*pScratchDeviceAddress) = cvkAlignDeviceSize(cvkGetBufferDeviceAddress(device, NULL, (*pScratchBuffer)), scratch_alignment);
}

/**
 * \brief Creates a shader binding table for a ray tracing pipeline whose groups are ordered raygen, miss, hit, callable.
 *
 * \par Details:
 *  Group zero is the raygen group; the miss, hit, and callable groups follow in that order.
 *  Every handle is placed at a stride of the handle size rounded up to the handle alignment,
 *  and every region starts at the base alignment, in a host-visible buffer with a device
 *  address. The handles are written straight into the mapped buffer. The raygen region's size
 *  equals its stride as the specification requires; an empty region has a zero address.
 *
 * \param[in]  physicalDevice          Physical device whose ray tracing properties give the alignments
 * \param[in]  device                  Logical device owning the pipeline and the table
 * \param[in]  pipeline                Ray tracing pipeline the handles come from
 * \param[in]  missGroupCount          Number of miss groups following the raygen group
 * \param[in]  hitGroupCount           Number of hit groups following the miss groups
 * \param[in]  callableGroupCount      Number of callable groups following the hit groups
 * \param[in]  pAllocator              Optional allocation callbacks, or NULL
 * \param[out] pBuffer                 Receives the table buffer
 * \param[out] pMemory                 Receives the table memory
 * \param[out] pRaygenRegion           Receives the raygen region
 * \param[out] pMissRegion             Receives the miss region
 * \param[out] pHitRegion              Receives the hit region
 * \param[out] pCallableRegion         Receives the callable region
 *
 * \retval None
 */
void cvkSetupRayTracingShaderBindingTable(VkPhysicalDevice                 physicalDevice,
                                          VkDevice                         device,
                                          VkPipeline                       pipeline,
                                          uint32_t                         missGroupCount,
                                          uint32_t                         hitGroupCount,
                                          uint32_t                         callableGroupCount,
                                          const VkAllocationCallbacks*     pAllocator,
                                          VkBuffer*                        pBuffer,
                                          VkDeviceMemory*                  pMemory,
                                          VkStridedDeviceAddressRegionKHR* pRaygenRegion,
                                          VkStridedDeviceAddressRegionKHR* pMissRegion,
                                          VkStridedDeviceAddressRegionKHR* pHitRegion,
                                          VkStridedDeviceAddressRegionKHR* pCallableRegion)
{
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR ray_tracing_pipeline_properties;
    VkDeviceSize                                    handle_size;
    VkDeviceSize                                    handle_stride;
    VkDeviceSize                                    raygen_size;
    VkDeviceSize                                    miss_size;
    VkDeviceSize                                    hit_size;
    VkDeviceSize                                    callable_size;
    VkDeviceAddress                                 base_address;
    uint8_t*                                        mapped;
    uint32_t                                        group_index;
    uint32_t                                        region_first_group;
    VkDeviceSize                                    region_offset;

    cvkGetPhysicalDeviceRayTracingPipelineProperties(physicalDevice, NULL, &ray_tracing_pipeline_properties);
    handle_size   = ray_tracing_pipeline_properties.shaderGroupHandleSize;
    handle_stride = cvkAlignDeviceSize(handle_size, ray_tracing_pipeline_properties.shaderGroupHandleAlignment);
    raygen_size   = cvkAlignDeviceSize(handle_stride, ray_tracing_pipeline_properties.shaderGroupBaseAlignment);
    miss_size     = cvkAlignDeviceSize(missGroupCount * handle_stride, ray_tracing_pipeline_properties.shaderGroupBaseAlignment);
    hit_size      = cvkAlignDeviceSize(hitGroupCount * handle_stride, ray_tracing_pipeline_properties.shaderGroupBaseAlignment);
    callable_size = cvkAlignDeviceSize(callableGroupCount * handle_stride, ray_tracing_pipeline_properties.shaderGroupBaseAlignment);

    cvkSetupShaderBindingTableBuffer(physicalDevice, device, NULL, (VkBufferCreateFlags)0, raygen_size + miss_size + hit_size + callable_size, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, NULL, pAllocator, pBuffer, pMemory);
    base_address = cvkGetBufferDeviceAddress(device, NULL, (*pBuffer));

    /* handles straight into the mapped table: region by region, group by group */
    cvkMapWholeMemory(device, (*pMemory), (void**)&mapped);
    cvkGetRayTracingShaderGroupHandles(device, pipeline, 0, 1, (size_t)handle_size, mapped);
    region_first_group = 1;
    region_offset      = raygen_size;
    for (group_index = 0; group_index < missGroupCount; ++group_index)
    {
        cvkGetRayTracingShaderGroupHandles(device, pipeline, region_first_group + group_index, 1, (size_t)handle_size, mapped + region_offset + (group_index * handle_stride));
    }
    region_first_group += missGroupCount;
    region_offset += miss_size;
    for (group_index = 0; group_index < hitGroupCount; ++group_index)
    {
        cvkGetRayTracingShaderGroupHandles(device, pipeline, region_first_group + group_index, 1, (size_t)handle_size, mapped + region_offset + (group_index * handle_stride));
    }
    region_first_group += hitGroupCount;
    region_offset += hit_size;
    for (group_index = 0; group_index < callableGroupCount; ++group_index)
    {
        cvkGetRayTracingShaderGroupHandles(device, pipeline, region_first_group + group_index, 1, (size_t)handle_size, mapped + region_offset + (group_index * handle_stride));
    }
    cvkUnmapMemory(device, (*pMemory));

    cvkInitStridedDeviceAddressRegion(pRaygenRegion, base_address, handle_stride, handle_stride);
    cvkInitStridedDeviceAddressRegion(pMissRegion, (0 != missGroupCount) ? base_address + raygen_size : 0, handle_stride, miss_size);
    cvkInitStridedDeviceAddressRegion(pHitRegion, (0 != hitGroupCount) ? base_address + raygen_size + miss_size : 0, handle_stride, hit_size);
    cvkInitStridedDeviceAddressRegion(pCallableRegion, (0 != callableGroupCount) ? base_address + raygen_size + miss_size + hit_size : 0, handle_stride, callable_size);
}

/**
 * \brief Writes bytes into a memory object through a mapping: maps the range, copies, flushes the range, unmaps.
 *
 * \par Details:
 *  Every parameter of the map and of the flushed VkMappedMemoryRange is exposed. For
 *  host-coherent memory the flush is unnecessary: use cvkWriteCoherentMemory.
 *
 * \param[in]  device                  Logical device owning the memory
 * \param[in]  memory                  Host-visible memory object
 * \param[in]  offset                  Byte offset of the range; must observe nonCoherentAtomSize with size
 * \param[in]  size                    Byte count to write; not VK_WHOLE_SIZE
 * \param[in]  flags                   vkMapMemory flags
 * \param[in]  pRangeNext              Optional extension chain for the VkMappedMemoryRange, or NULL
 * \param[in]  pData                   Bytes to write; size bytes
 *
 * \retval None
 */
void cvkWriteMemory(VkDevice         device,
                    VkDeviceMemory   memory,
                    VkDeviceSize     offset,
                    VkDeviceSize     size,
                    VkMemoryMapFlags flags,
                    const void*      pRangeNext,
                    const void*      pData)
{
    void* mapped;

    CVK_ASSERT(VK_WHOLE_SIZE != size);
    cvkMapMemory(device, memory, offset, size, flags, &mapped);
    (void)memcpy(mapped, pData, (size_t)size);
    cvkFlushMappedMemoryRange(device, pRangeNext, memory, offset, size);
    cvkUnmapMemory(device, memory);
}

/**
 * \brief Reads bytes out of a memory object through a mapping: maps the range, invalidates the range, copies, unmaps.
 *
 * \par Details:
 *  Every parameter of the map and of the invalidated VkMappedMemoryRange is exposed. For
 *  host-coherent memory the invalidation is unnecessary: use cvkReadCoherentMemory.
 *
 * \param[in]  device                  Logical device owning the memory
 * \param[in]  memory                  Host-visible memory object
 * \param[in]  offset                  Byte offset of the range; must observe nonCoherentAtomSize with size
 * \param[in]  size                    Byte count to read; not VK_WHOLE_SIZE
 * \param[in]  flags                   vkMapMemory flags
 * \param[in]  pRangeNext              Optional extension chain for the VkMappedMemoryRange, or NULL
 * \param[out] pData                   Receives size bytes
 *
 * \retval None
 */
void cvkReadMemory(VkDevice         device,
                   VkDeviceMemory   memory,
                   VkDeviceSize     offset,
                   VkDeviceSize     size,
                   VkMemoryMapFlags flags,
                   const void*      pRangeNext,
                   void*            pData)
{
    void* mapped;

    CVK_ASSERT(VK_WHOLE_SIZE != size);
    cvkMapMemory(device, memory, offset, size, flags, &mapped);
    cvkInvalidateMappedMemoryRange(device, pRangeNext, memory, offset, size);
    (void)memcpy(pData, mapped, (size_t)size);
    cvkUnmapMemory(device, memory);
}

/**
 * \brief Writes bytes into a host-coherent memory object through a mapping: maps the range, copies, unmaps.
 *
 * \param[in]  device                  Logical device owning the memory
 * \param[in]  memory                  Host-visible, host-coherent memory object
 * \param[in]  offset                  Byte offset of the range
 * \param[in]  size                    Byte count to write; not VK_WHOLE_SIZE
 * \param[in]  flags                   vkMapMemory flags
 * \param[in]  pData                   Bytes to write; size bytes
 *
 * \retval None
 */
void cvkWriteCoherentMemory(VkDevice         device,
                            VkDeviceMemory   memory,
                            VkDeviceSize     offset,
                            VkDeviceSize     size,
                            VkMemoryMapFlags flags,
                            const void*      pData)
{
    void* mapped;

    CVK_ASSERT(VK_WHOLE_SIZE != size);
    cvkMapMemory(device, memory, offset, size, flags, &mapped);
    (void)memcpy(mapped, pData, (size_t)size);
    cvkUnmapMemory(device, memory);
}

/**
 * \brief Reads bytes out of a host-coherent memory object through a mapping: maps the range, copies, unmaps.
 *
 * \param[in]  device                  Logical device owning the memory
 * \param[in]  memory                  Host-visible, host-coherent memory object
 * \param[in]  offset                  Byte offset of the range
 * \param[in]  size                    Byte count to read; not VK_WHOLE_SIZE
 * \param[in]  flags                   vkMapMemory flags
 * \param[out] pData                   Receives size bytes
 *
 * \retval None
 */
void cvkReadCoherentMemory(VkDevice         device,
                           VkDeviceMemory   memory,
                           VkDeviceSize     offset,
                           VkDeviceSize     size,
                           VkMemoryMapFlags flags,
                           void*            pData)
{
    void* mapped;

    CVK_ASSERT(VK_WHOLE_SIZE != size);
    cvkMapMemory(device, memory, offset, size, flags, &mapped);
    (void)memcpy(pData, mapped, (size_t)size);
    cvkUnmapMemory(device, memory);
}
