# Input structs and pNext extenders (VK_VERSION_1_2 + VK_EXT_debug_report, VK_EXT_debug_utils, VK_EXT_external_memory_dma_buf, VK_EXT_headless_surface, VK_EXT_image_drm_format_modifier, VK_EXT_pci_bus_info, VK_KHR_display, VK_KHR_external_memory_fd, VK_KHR_external_memory_win32, VK_KHR_external_semaphore_fd, VK_KHR_external_semaphore_win32, VK_KHR_surface, VK_KHR_swapchain, VK_KHR_win32_surface, VK_KHR_xlib_surface)

sType input structs consumed by profile commands: 74.

## `VkAcquireNextImageInfoKHR`

Consumed by: vkAcquireNextImage2KHR

Flattened parameters (6): `const void* pNext`, `VkSwapchainKHR swapchain`, `uint64_t timeout`, `VkSemaphore semaphore`, `VkFence fence`, `uint32_t deviceMask`

Extenders in profile: 0 (registry total 0)

## `VkBindBufferMemoryInfo`

Consumed by: vkBindBufferMemory2[]

Flattened parameters (4): `const void* pNext`, `VkBuffer buffer`, `VkDeviceMemory memory`, `VkDeviceSize memoryOffset`

Extenders in profile: 1 (registry total 2)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkBindBufferMemoryDeviceGroupInfo` | VK_VERSION_1_1 | `uint32_t deviceIndexCount`, `const uint32_t* pDeviceIndices` |  |

## `VkBindImageMemoryInfo`

Consumed by: vkBindImageMemory2[]

Flattened parameters (4): `const void* pNext`, `VkImage image`, `VkDeviceMemory memory`, `VkDeviceSize memoryOffset`

Extenders in profile: 3 (registry total 6)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkBindImageMemoryDeviceGroupInfo` | VK_VERSION_1_1 | `uint32_t deviceIndexCount`, `const uint32_t* pDeviceIndices`, `uint32_t splitInstanceBindRegionCount`, `const VkRect2D* pSplitInstanceBindRegions` | pSplitInstanceBindRegions: array of plain struct VkRect2D |
| `VkBindImageMemorySwapchainInfoKHR` | VK_KHR_swapchain, VK_KHR_device_group | `VkSwapchainKHR swapchain`, `uint32_t imageIndex` |  |
| `VkBindImagePlaneMemoryInfo` | VK_VERSION_1_1 | `VkImageAspectFlagBits planeAspect` |  |

## `VkBindSparseInfo`

Consumed by: vkQueueBindSparse[]

Flattened parameters (11): `const void* pNext`, `uint32_t waitSemaphoreCount`, `const VkSemaphore* pWaitSemaphores`, `uint32_t bufferBindCount`, `const VkSparseBufferMemoryBindInfo* pBufferBinds`, `uint32_t imageOpaqueBindCount`, `const VkSparseImageOpaqueMemoryBindInfo* pImageOpaqueBinds`, `uint32_t imageBindCount`, `const VkSparseImageMemoryBindInfo* pImageBinds`, `uint32_t signalSemaphoreCount`, `const VkSemaphore* pSignalSemaphores`

Hazards:
- pBufferBinds: array of plain struct VkSparseBufferMemoryBindInfo
- pImageOpaqueBinds: array of plain struct VkSparseImageOpaqueMemoryBindInfo
- pImageBinds: array of plain struct VkSparseImageMemoryBindInfo

Extenders in profile: 2 (registry total 4)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkDeviceGroupBindSparseInfo` | VK_VERSION_1_1 | `uint32_t resourceDeviceIndex`, `uint32_t memoryDeviceIndex` |  |
| `VkTimelineSemaphoreSubmitInfo` | VK_VERSION_1_2 | `uint32_t waitSemaphoreValueCount`, `const uint64_t* pWaitSemaphoreValues`, `uint32_t signalSemaphoreValueCount`, `const uint64_t* pSignalSemaphoreValues` |  |

## `VkBufferCreateInfo`

Consumed by: vkCreateBuffer

Flattened parameters (7): `const void* pNext`, `VkBufferCreateFlags flags`, `VkDeviceSize size`, `VkBufferUsageFlags usage`, `VkSharingMode sharingMode`, `uint32_t queueFamilyIndexCount`, `const uint32_t* pQueueFamilyIndices`

Extenders in profile: 2 (registry total 9)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkBufferOpaqueCaptureAddressCreateInfo` | VK_VERSION_1_2 | `uint64_t opaqueCaptureAddress` |  |
| `VkExternalMemoryBufferCreateInfo` | VK_VERSION_1_1 | `VkExternalMemoryHandleTypeFlags handleTypes` |  |

## `VkBufferDeviceAddressInfo`

Consumed by: vkGetBufferDeviceAddress, vkGetBufferOpaqueCaptureAddress

Flattened parameters (2): `const void* pNext`, `VkBuffer buffer`

Extenders in profile: 0 (registry total 0)

## `VkBufferMemoryBarrier`

Consumed by: vkCmdPipelineBarrier[], vkCmdWaitEvents[]

Flattened parameters (8): `const void* pNext`, `VkAccessFlags srcAccessMask`, `VkAccessFlags dstAccessMask`, `uint32_t srcQueueFamilyIndex`, `uint32_t dstQueueFamilyIndex`, `VkBuffer buffer`, `VkDeviceSize offset`, `VkDeviceSize size`

Extenders in profile: 0 (registry total 1)

## `VkBufferMemoryRequirementsInfo2`

Consumed by: vkGetBufferMemoryRequirements2

Flattened parameters (2): `const void* pNext`, `VkBuffer buffer`

Extenders in profile: 0 (registry total 0)

## `VkBufferViewCreateInfo`

Consumed by: vkCreateBufferView

Flattened parameters (6): `const void* pNext`, `VkBufferViewCreateFlags flags`, `VkBuffer buffer`, `VkFormat format`, `VkDeviceSize offset`, `VkDeviceSize range`

Extenders in profile: 0 (registry total 2)

## `VkCommandBufferAllocateInfo`

Consumed by: vkAllocateCommandBuffers

Flattened parameters (4): `const void* pNext`, `VkCommandPool commandPool`, `VkCommandBufferLevel level`, `uint32_t commandBufferCount`

Extenders in profile: 0 (registry total 0)

## `VkCommandBufferBeginInfo`

Consumed by: vkBeginCommandBuffer

Flattened parameters (3): `const void* pNext`, `VkCommandBufferUsageFlags flags`, `const VkCommandBufferInheritanceInfo* pInheritanceInfo`

Hazards:
- pInheritanceInfo: pointer to sType struct VkCommandBufferInheritanceInfo

Extenders in profile: 1 (registry total 1)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkDeviceGroupCommandBufferBeginInfo` | VK_VERSION_1_1 | `uint32_t deviceMask` |  |

## `VkCommandPoolCreateInfo`

Consumed by: vkCreateCommandPool

Flattened parameters (3): `const void* pNext`, `VkCommandPoolCreateFlags flags`, `uint32_t queueFamilyIndex`

Extenders in profile: 0 (registry total 2)

## `VkComputePipelineCreateInfo`

Consumed by: vkCreateComputePipelines[]

Flattened parameters (6): `const void* pNext`, `VkPipelineCreateFlags flags`, `VkPipelineShaderStageCreateInfo stage`, `VkPipelineLayout layout`, `VkPipeline basePipelineHandle`, `int32_t basePipelineIndex`

Extenders in profile: 0 (registry total 9)

## `VkCopyDescriptorSet`

Consumed by: vkUpdateDescriptorSets[]

Flattened parameters (8): `const void* pNext`, `VkDescriptorSet srcSet`, `uint32_t srcBinding`, `uint32_t srcArrayElement`, `VkDescriptorSet dstSet`, `uint32_t dstBinding`, `uint32_t dstArrayElement`, `uint32_t descriptorCount`

Extenders in profile: 0 (registry total 0)

## `VkDebugReportCallbackCreateInfoEXT`

Consumed by: vkCreateDebugReportCallbackEXT

Flattened parameters (4): `const void* pNext`, `VkDebugReportFlagsEXT flags`, `PFN_vkDebugReportCallbackEXT pfnCallback`, `void* pUserData`

Extenders in profile: 0 (registry total 0)

## `VkDebugUtilsLabelEXT`

Consumed by: vkCmdBeginDebugUtilsLabelEXT, vkCmdInsertDebugUtilsLabelEXT, vkQueueBeginDebugUtilsLabelEXT, vkQueueInsertDebugUtilsLabelEXT

Flattened parameters (3): `const void* pNext`, `const char* pLabelName`, `float color`

Hazards:
- color: fixed array [4]

Extenders in profile: 0 (registry total 0)

## `VkDebugUtilsMessengerCallbackDataEXT`

Consumed by: vkSubmitDebugUtilsMessageEXT

Flattened parameters (11): `const void* pNext`, `VkDebugUtilsMessengerCallbackDataFlagsEXT flags`, `const char* pMessageIdName`, `int32_t messageIdNumber`, `const char* pMessage`, `uint32_t queueLabelCount`, `const VkDebugUtilsLabelEXT* pQueueLabels`, `uint32_t cmdBufLabelCount`, `const VkDebugUtilsLabelEXT* pCmdBufLabels`, `uint32_t objectCount`, `const VkDebugUtilsObjectNameInfoEXT* pObjects`

Hazards:
- pQueueLabels: pointer to sType struct VkDebugUtilsLabelEXT (array)
- pCmdBufLabels: pointer to sType struct VkDebugUtilsLabelEXT (array)
- pObjects: pointer to sType struct VkDebugUtilsObjectNameInfoEXT (array)

Extenders in profile: 0 (registry total 1)

## `VkDebugUtilsMessengerCreateInfoEXT`

Consumed by: vkCreateDebugUtilsMessengerEXT

Flattened parameters (6): `const void* pNext`, `VkDebugUtilsMessengerCreateFlagsEXT flags`, `VkDebugUtilsMessageSeverityFlagsEXT messageSeverity`, `VkDebugUtilsMessageTypeFlagsEXT messageType`, `PFN_vkDebugUtilsMessengerCallbackEXT pfnUserCallback`, `void* pUserData`

Extenders in profile: 0 (registry total 0)

## `VkDebugUtilsObjectNameInfoEXT`

Consumed by: vkSetDebugUtilsObjectNameEXT

Flattened parameters (4): `const void* pNext`, `VkObjectType objectType`, `uint64_t objectHandle`, `const char* pObjectName`

Extenders in profile: 0 (registry total 0)

## `VkDebugUtilsObjectTagInfoEXT`

Consumed by: vkSetDebugUtilsObjectTagEXT

Flattened parameters (6): `const void* pNext`, `VkObjectType objectType`, `uint64_t objectHandle`, `uint64_t tagName`, `size_t tagSize`, `const void* pTag`

Hazards:
- pTag: void payload len=tagSize

Extenders in profile: 0 (registry total 0)

## `VkDescriptorPoolCreateInfo`

Consumed by: vkCreateDescriptorPool

Flattened parameters (5): `const void* pNext`, `VkDescriptorPoolCreateFlags flags`, `uint32_t maxSets`, `uint32_t poolSizeCount`, `const VkDescriptorPoolSize* pPoolSizes`

Hazards:
- pPoolSizes: array of plain struct VkDescriptorPoolSize

Extenders in profile: 0 (registry total 3)

## `VkDescriptorSetAllocateInfo`

Consumed by: vkAllocateDescriptorSets

Flattened parameters (4): `const void* pNext`, `VkDescriptorPool descriptorPool`, `uint32_t descriptorSetCount`, `const VkDescriptorSetLayout* pSetLayouts`

Extenders in profile: 1 (registry total 1)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkDescriptorSetVariableDescriptorCountAllocateInfo` | VK_VERSION_1_2 | `uint32_t descriptorSetCount`, `const uint32_t* pDescriptorCounts` |  |

## `VkDescriptorSetLayoutCreateInfo`

Consumed by: vkCreateDescriptorSetLayout, vkGetDescriptorSetLayoutSupport

Flattened parameters (4): `const void* pNext`, `VkDescriptorSetLayoutCreateFlags flags`, `uint32_t bindingCount`, `const VkDescriptorSetLayoutBinding* pBindings`

Hazards:
- pBindings: array of plain struct VkDescriptorSetLayoutBinding

Extenders in profile: 1 (registry total 2)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkDescriptorSetLayoutBindingFlagsCreateInfo` | VK_VERSION_1_2 | `uint32_t bindingCount`, `const VkDescriptorBindingFlags* pBindingFlags` |  |

## `VkDescriptorUpdateTemplateCreateInfo`

Consumed by: vkCreateDescriptorUpdateTemplate

Flattened parameters (9): `const void* pNext`, `VkDescriptorUpdateTemplateCreateFlags flags`, `uint32_t descriptorUpdateEntryCount`, `const VkDescriptorUpdateTemplateEntry* pDescriptorUpdateEntries`, `VkDescriptorUpdateTemplateType templateType`, `VkDescriptorSetLayout descriptorSetLayout`, `VkPipelineBindPoint pipelineBindPoint`, `VkPipelineLayout pipelineLayout`, `uint32_t set`

Hazards:
- pDescriptorUpdateEntries: array of plain struct VkDescriptorUpdateTemplateEntry

Extenders in profile: 0 (registry total 0)

## `VkDeviceCreateInfo`

Consumed by: vkCreateDevice

Flattened parameters (9): `const void* pNext`, `VkDeviceCreateFlags flags`, `uint32_t queueCreateInfoCount`, `const VkDeviceQueueCreateInfo* pQueueCreateInfos`, `uint32_t enabledLayerCount`, `const char* const* ppEnabledLayerNames`, `uint32_t enabledExtensionCount`, `const char* const* ppEnabledExtensionNames`, `const VkPhysicalDeviceFeatures* pEnabledFeatures`

Hazards:
- pQueueCreateInfos: pointer to sType struct VkDeviceQueueCreateInfo (array)
- ppEnabledLayerNames: double pointer const char* const*
- ppEnabledExtensionNames: double pointer const char* const*

Extenders in profile: 23 (registry total 295)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkDeviceGroupDeviceCreateInfo` | VK_VERSION_1_1 | `uint32_t physicalDeviceCount`, `const VkPhysicalDevice* pPhysicalDevices` |  |
| `VkPhysicalDevice16BitStorageFeatures` | VK_VERSION_1_1 | `VkBool32 storageBuffer16BitAccess`, `VkBool32 uniformAndStorageBuffer16BitAccess`, `VkBool32 storagePushConstant16`, `VkBool32 storageInputOutput16` |  |
| `VkPhysicalDevice8BitStorageFeatures` | VK_VERSION_1_2 | `VkBool32 storageBuffer8BitAccess`, `VkBool32 uniformAndStorageBuffer8BitAccess`, `VkBool32 storagePushConstant8` |  |
| `VkPhysicalDeviceBufferDeviceAddressFeatures` | VK_VERSION_1_2 | `VkBool32 bufferDeviceAddress`, `VkBool32 bufferDeviceAddressCaptureReplay`, `VkBool32 bufferDeviceAddressMultiDevice` |  |
| `VkPhysicalDeviceDescriptorIndexingFeatures` | VK_VERSION_1_2 | `VkBool32 shaderInputAttachmentArrayDynamicIndexing`, `VkBool32 shaderUniformTexelBufferArrayDynamicIndexing`, `VkBool32 shaderStorageTexelBufferArrayDynamicIndexing`, `VkBool32 shaderUniformBufferArrayNonUniformIndexing`, `VkBool32 shaderSampledImageArrayNonUniformIndexing`, `VkBool32 shaderStorageBufferArrayNonUniformIndexing`, `VkBool32 shaderStorageImageArrayNonUniformIndexing`, `VkBool32 shaderInputAttachmentArrayNonUniformIndexing`, `VkBool32 shaderUniformTexelBufferArrayNonUniformIndexing`, `VkBool32 shaderStorageTexelBufferArrayNonUniformIndexing`, `VkBool32 descriptorBindingUniformBufferUpdateAfterBind`, `VkBool32 descriptorBindingSampledImageUpdateAfterBind`, `VkBool32 descriptorBindingStorageImageUpdateAfterBind`, `VkBool32 descriptorBindingStorageBufferUpdateAfterBind`, `VkBool32 descriptorBindingUniformTexelBufferUpdateAfterBind`, `VkBool32 descriptorBindingStorageTexelBufferUpdateAfterBind`, `VkBool32 descriptorBindingUpdateUnusedWhilePending`, `VkBool32 descriptorBindingPartiallyBound`, `VkBool32 descriptorBindingVariableDescriptorCount`, `VkBool32 runtimeDescriptorArray` |  |
| `VkPhysicalDeviceFeatures2` | VK_VERSION_1_1 | `VkPhysicalDeviceFeatures features` |  |
| `VkPhysicalDeviceHostQueryResetFeatures` | VK_VERSION_1_2 | `VkBool32 hostQueryReset` |  |
| `VkPhysicalDeviceImagelessFramebufferFeatures` | VK_VERSION_1_2 | `VkBool32 imagelessFramebuffer` |  |
| `VkPhysicalDeviceMultiviewFeatures` | VK_VERSION_1_1 | `VkBool32 multiview`, `VkBool32 multiviewGeometryShader`, `VkBool32 multiviewTessellationShader` |  |
| `VkPhysicalDeviceProtectedMemoryFeatures` | VK_VERSION_1_1 | `VkBool32 protectedMemory` |  |
| `VkPhysicalDeviceSamplerYcbcrConversionFeatures` | VK_VERSION_1_1 | `VkBool32 samplerYcbcrConversion` |  |
| `VkPhysicalDeviceScalarBlockLayoutFeatures` | VK_VERSION_1_2 | `VkBool32 scalarBlockLayout` |  |
| `VkPhysicalDeviceSeparateDepthStencilLayoutsFeatures` | VK_VERSION_1_2 | `VkBool32 separateDepthStencilLayouts` |  |
| `VkPhysicalDeviceShaderAtomicInt64Features` | VK_VERSION_1_2 | `VkBool32 shaderBufferInt64Atomics`, `VkBool32 shaderSharedInt64Atomics` |  |
| `VkPhysicalDeviceShaderDrawParametersFeatures` | VK_VERSION_1_1 | `VkBool32 shaderDrawParameters` |  |
| `VkPhysicalDeviceShaderFloat16Int8Features` | VK_VERSION_1_2 | `VkBool32 shaderFloat16`, `VkBool32 shaderInt8` |  |
| `VkPhysicalDeviceShaderSubgroupExtendedTypesFeatures` | VK_VERSION_1_2 | `VkBool32 shaderSubgroupExtendedTypes` |  |
| `VkPhysicalDeviceTimelineSemaphoreFeatures` | VK_VERSION_1_2 | `VkBool32 timelineSemaphore` |  |
| `VkPhysicalDeviceUniformBufferStandardLayoutFeatures` | VK_VERSION_1_2 | `VkBool32 uniformBufferStandardLayout` |  |
| `VkPhysicalDeviceVariablePointersFeatures` | VK_VERSION_1_1 | `VkBool32 variablePointersStorageBuffer`, `VkBool32 variablePointers` |  |
| `VkPhysicalDeviceVulkan11Features` | VK_VERSION_1_2 | `VkBool32 storageBuffer16BitAccess`, `VkBool32 uniformAndStorageBuffer16BitAccess`, `VkBool32 storagePushConstant16`, `VkBool32 storageInputOutput16`, `VkBool32 multiview`, `VkBool32 multiviewGeometryShader`, `VkBool32 multiviewTessellationShader`, `VkBool32 variablePointersStorageBuffer`, `VkBool32 variablePointers`, `VkBool32 protectedMemory`, `VkBool32 samplerYcbcrConversion`, `VkBool32 shaderDrawParameters` |  |
| `VkPhysicalDeviceVulkan12Features` | VK_VERSION_1_2 | `VkBool32 samplerMirrorClampToEdge`, `VkBool32 drawIndirectCount`, `VkBool32 storageBuffer8BitAccess`, `VkBool32 uniformAndStorageBuffer8BitAccess`, `VkBool32 storagePushConstant8`, `VkBool32 shaderBufferInt64Atomics`, `VkBool32 shaderSharedInt64Atomics`, `VkBool32 shaderFloat16`, `VkBool32 shaderInt8`, `VkBool32 descriptorIndexing`, `VkBool32 shaderInputAttachmentArrayDynamicIndexing`, `VkBool32 shaderUniformTexelBufferArrayDynamicIndexing`, `VkBool32 shaderStorageTexelBufferArrayDynamicIndexing`, `VkBool32 shaderUniformBufferArrayNonUniformIndexing`, `VkBool32 shaderSampledImageArrayNonUniformIndexing`, `VkBool32 shaderStorageBufferArrayNonUniformIndexing`, `VkBool32 shaderStorageImageArrayNonUniformIndexing`, `VkBool32 shaderInputAttachmentArrayNonUniformIndexing`, `VkBool32 shaderUniformTexelBufferArrayNonUniformIndexing`, `VkBool32 shaderStorageTexelBufferArrayNonUniformIndexing`, `VkBool32 descriptorBindingUniformBufferUpdateAfterBind`, `VkBool32 descriptorBindingSampledImageUpdateAfterBind`, `VkBool32 descriptorBindingStorageImageUpdateAfterBind`, `VkBool32 descriptorBindingStorageBufferUpdateAfterBind`, `VkBool32 descriptorBindingUniformTexelBufferUpdateAfterBind`, `VkBool32 descriptorBindingStorageTexelBufferUpdateAfterBind`, `VkBool32 descriptorBindingUpdateUnusedWhilePending`, `VkBool32 descriptorBindingPartiallyBound`, `VkBool32 descriptorBindingVariableDescriptorCount`, `VkBool32 runtimeDescriptorArray`, `VkBool32 samplerFilterMinmax`, `VkBool32 scalarBlockLayout`, `VkBool32 imagelessFramebuffer`, `VkBool32 uniformBufferStandardLayout`, `VkBool32 shaderSubgroupExtendedTypes`, `VkBool32 separateDepthStencilLayouts`, `VkBool32 hostQueryReset`, `VkBool32 timelineSemaphore`, `VkBool32 bufferDeviceAddress`, `VkBool32 bufferDeviceAddressCaptureReplay`, `VkBool32 bufferDeviceAddressMultiDevice`, `VkBool32 vulkanMemoryModel`, `VkBool32 vulkanMemoryModelDeviceScope`, `VkBool32 vulkanMemoryModelAvailabilityVisibilityChains`, `VkBool32 shaderOutputViewportIndex`, `VkBool32 shaderOutputLayer`, `VkBool32 subgroupBroadcastDynamicId` |  |
| `VkPhysicalDeviceVulkanMemoryModelFeatures` | VK_VERSION_1_2 | `VkBool32 vulkanMemoryModel`, `VkBool32 vulkanMemoryModelDeviceScope`, `VkBool32 vulkanMemoryModelAvailabilityVisibilityChains` |  |

## `VkDeviceMemoryOpaqueCaptureAddressInfo`

Consumed by: vkGetDeviceMemoryOpaqueCaptureAddress

Flattened parameters (2): `const void* pNext`, `VkDeviceMemory memory`

Extenders in profile: 0 (registry total 0)

## `VkDeviceQueueInfo2`

Consumed by: vkGetDeviceQueue2

Flattened parameters (4): `const void* pNext`, `VkDeviceQueueCreateFlags flags`, `uint32_t queueFamilyIndex`, `uint32_t queueIndex`

Extenders in profile: 0 (registry total 0)

## `VkDisplayModeCreateInfoKHR`

Consumed by: vkCreateDisplayModeKHR

Flattened parameters (3): `const void* pNext`, `VkDisplayModeCreateFlagsKHR flags`, `VkDisplayModeParametersKHR parameters`

Extenders in profile: 0 (registry total 0)

## `VkDisplaySurfaceCreateInfoKHR`

Consumed by: vkCreateDisplayPlaneSurfaceKHR

Flattened parameters (9): `const void* pNext`, `VkDisplaySurfaceCreateFlagsKHR flags`, `VkDisplayModeKHR displayMode`, `uint32_t planeIndex`, `uint32_t planeStackIndex`, `VkSurfaceTransformFlagBitsKHR transform`, `float globalAlpha`, `VkDisplayPlaneAlphaFlagBitsKHR alphaMode`, `VkExtent2D imageExtent`

Extenders in profile: 0 (registry total 1)

## `VkEventCreateInfo`

Consumed by: vkCreateEvent

Flattened parameters (2): `const void* pNext`, `VkEventCreateFlags flags`

Extenders in profile: 0 (registry total 2)

## `VkFenceCreateInfo`

Consumed by: vkCreateFence

Flattened parameters (2): `const void* pNext`, `VkFenceCreateFlags flags`

Extenders in profile: 1 (registry total 3)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkExportFenceCreateInfo` | VK_VERSION_1_1 | `VkExternalFenceHandleTypeFlags handleTypes` |  |

## `VkFramebufferCreateInfo`

Consumed by: vkCreateFramebuffer

Flattened parameters (8): `const void* pNext`, `VkFramebufferCreateFlags flags`, `VkRenderPass renderPass`, `uint32_t attachmentCount`, `const VkImageView* pAttachments`, `uint32_t width`, `uint32_t height`, `uint32_t layers`

Extenders in profile: 1 (registry total 1)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkFramebufferAttachmentsCreateInfo` | VK_VERSION_1_2 | `uint32_t attachmentImageInfoCount`, `const VkFramebufferAttachmentImageInfo* pAttachmentImageInfos` | pAttachmentImageInfos: pointer to sType struct VkFramebufferAttachmentImageInfo (array) |

## `VkGraphicsPipelineCreateInfo`

Consumed by: vkCreateGraphicsPipelines[]

Flattened parameters (18): `const void* pNext`, `VkPipelineCreateFlags flags`, `uint32_t stageCount`, `const VkPipelineShaderStageCreateInfo* pStages`, `const VkPipelineVertexInputStateCreateInfo* pVertexInputState`, `const VkPipelineInputAssemblyStateCreateInfo* pInputAssemblyState`, `const VkPipelineTessellationStateCreateInfo* pTessellationState`, `const VkPipelineViewportStateCreateInfo* pViewportState`, `const VkPipelineRasterizationStateCreateInfo* pRasterizationState`, `const VkPipelineMultisampleStateCreateInfo* pMultisampleState`, `const VkPipelineDepthStencilStateCreateInfo* pDepthStencilState`, `const VkPipelineColorBlendStateCreateInfo* pColorBlendState`, `const VkPipelineDynamicStateCreateInfo* pDynamicState`, `VkPipelineLayout layout`, `VkRenderPass renderPass`, `uint32_t subpass`, `VkPipeline basePipelineHandle`, `int32_t basePipelineIndex`

Hazards:
- pStages: pointer to sType struct VkPipelineShaderStageCreateInfo (array)
- pVertexInputState: pointer to sType struct VkPipelineVertexInputStateCreateInfo
- pInputAssemblyState: pointer to sType struct VkPipelineInputAssemblyStateCreateInfo
- pTessellationState: pointer to sType struct VkPipelineTessellationStateCreateInfo
- pViewportState: pointer to sType struct VkPipelineViewportStateCreateInfo
- pRasterizationState: pointer to sType struct VkPipelineRasterizationStateCreateInfo
- pMultisampleState: pointer to sType struct VkPipelineMultisampleStateCreateInfo
- pDepthStencilState: pointer to sType struct VkPipelineDepthStencilStateCreateInfo
- pColorBlendState: pointer to sType struct VkPipelineColorBlendStateCreateInfo
- pDynamicState: pointer to sType struct VkPipelineDynamicStateCreateInfo

Extenders in profile: 0 (registry total 23)

## `VkHeadlessSurfaceCreateInfoEXT`

Consumed by: vkCreateHeadlessSurfaceEXT

Flattened parameters (2): `const void* pNext`, `VkHeadlessSurfaceCreateFlagsEXT flags`

Extenders in profile: 0 (registry total 0)

## `VkImageCreateInfo`

Consumed by: vkCreateImage

Flattened parameters (14): `const void* pNext`, `VkImageCreateFlags flags`, `VkImageType imageType`, `VkFormat format`, `VkExtent3D extent`, `uint32_t mipLevels`, `uint32_t arrayLayers`, `VkSampleCountFlagBits samples`, `VkImageTiling tiling`, `VkImageUsageFlags usage`, `VkSharingMode sharingMode`, `uint32_t queueFamilyIndexCount`, `const uint32_t* pQueueFamilyIndices`, `VkImageLayout initialLayout`

Extenders in profile: 6 (registry total 30)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkExternalMemoryImageCreateInfo` | VK_VERSION_1_1 | `VkExternalMemoryHandleTypeFlags handleTypes` |  |
| `VkImageDrmFormatModifierExplicitCreateInfoEXT` | VK_EXT_image_drm_format_modifier | `uint64_t drmFormatModifier`, `uint32_t drmFormatModifierPlaneCount`, `const VkSubresourceLayout* pPlaneLayouts` | pPlaneLayouts: array of plain struct VkSubresourceLayout |
| `VkImageDrmFormatModifierListCreateInfoEXT` | VK_EXT_image_drm_format_modifier | `uint32_t drmFormatModifierCount`, `const uint64_t* pDrmFormatModifiers` |  |
| `VkImageFormatListCreateInfo` | VK_VERSION_1_2 | `uint32_t viewFormatCount`, `const VkFormat* pViewFormats` |  |
| `VkImageStencilUsageCreateInfo` | VK_VERSION_1_2 | `VkImageUsageFlags stencilUsage` |  |
| `VkImageSwapchainCreateInfoKHR` | VK_KHR_swapchain, VK_KHR_device_group | `VkSwapchainKHR swapchain` |  |

## `VkImageMemoryBarrier`

Consumed by: vkCmdPipelineBarrier[], vkCmdWaitEvents[]

Flattened parameters (9): `const void* pNext`, `VkAccessFlags srcAccessMask`, `VkAccessFlags dstAccessMask`, `VkImageLayout oldLayout`, `VkImageLayout newLayout`, `uint32_t srcQueueFamilyIndex`, `uint32_t dstQueueFamilyIndex`, `VkImage image`, `VkImageSubresourceRange subresourceRange`

Extenders in profile: 0 (registry total 2)

## `VkImageMemoryRequirementsInfo2`

Consumed by: vkGetImageMemoryRequirements2

Flattened parameters (2): `const void* pNext`, `VkImage image`

Extenders in profile: 1 (registry total 1)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkImagePlaneMemoryRequirementsInfo` | VK_VERSION_1_1 | `VkImageAspectFlagBits planeAspect` |  |

## `VkImageSparseMemoryRequirementsInfo2`

Consumed by: vkGetImageSparseMemoryRequirements2

Flattened parameters (2): `const void* pNext`, `VkImage image`

Extenders in profile: 0 (registry total 0)

## `VkImageViewCreateInfo`

Consumed by: vkCreateImageView

Flattened parameters (7): `const void* pNext`, `VkImageViewCreateFlags flags`, `VkImage image`, `VkImageViewType viewType`, `VkFormat format`, `VkComponentMapping components`, `VkImageSubresourceRange subresourceRange`

Extenders in profile: 2 (registry total 9)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkImageViewUsageCreateInfo` | VK_VERSION_1_1 | `VkImageUsageFlags usage` |  |
| `VkSamplerYcbcrConversionInfo` | VK_VERSION_1_1 | `VkSamplerYcbcrConversion conversion` |  |

## `VkImportSemaphoreFdInfoKHR`

Consumed by: vkImportSemaphoreFdKHR

Flattened parameters (5): `const void* pNext`, `VkSemaphore semaphore`, `VkSemaphoreImportFlags flags`, `VkExternalSemaphoreHandleTypeFlagBits handleType`, `int fd`

Extenders in profile: 0 (registry total 0)

## `VkImportSemaphoreWin32HandleInfoKHR`

Consumed by: vkImportSemaphoreWin32HandleKHR

Flattened parameters (6): `const void* pNext`, `VkSemaphore semaphore`, `VkSemaphoreImportFlags flags`, `VkExternalSemaphoreHandleTypeFlagBits handleType`, `HANDLE handle`, `LPCWSTR name`

Extenders in profile: 0 (registry total 0)

## `VkInstanceCreateInfo`

Consumed by: vkCreateInstance

Flattened parameters (7): `const void* pNext`, `VkInstanceCreateFlags flags`, `const VkApplicationInfo* pApplicationInfo`, `uint32_t enabledLayerCount`, `const char* const* ppEnabledLayerNames`, `uint32_t enabledExtensionCount`, `const char* const* ppEnabledExtensionNames`

Hazards:
- pApplicationInfo: pointer to sType struct VkApplicationInfo
- ppEnabledLayerNames: double pointer const char* const*
- ppEnabledExtensionNames: double pointer const char* const*

Extenders in profile: 2 (registry total 7)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkDebugReportCallbackCreateInfoEXT` | VK_EXT_debug_report | `VkDebugReportFlagsEXT flags`, `PFN_vkDebugReportCallbackEXT pfnCallback`, `void* pUserData` |  |
| `VkDebugUtilsMessengerCreateInfoEXT` | VK_EXT_debug_utils | `VkDebugUtilsMessengerCreateFlagsEXT flags`, `VkDebugUtilsMessageSeverityFlagsEXT messageSeverity`, `VkDebugUtilsMessageTypeFlagsEXT messageType`, `PFN_vkDebugUtilsMessengerCallbackEXT pfnUserCallback`, `void* pUserData` |  |

## `VkMappedMemoryRange`

Consumed by: vkFlushMappedMemoryRanges[], vkInvalidateMappedMemoryRanges[]

Flattened parameters (4): `const void* pNext`, `VkDeviceMemory memory`, `VkDeviceSize offset`, `VkDeviceSize size`

Extenders in profile: 0 (registry total 0)

## `VkMemoryAllocateInfo`

Consumed by: vkAllocateMemory

Flattened parameters (3): `const void* pNext`, `VkDeviceSize allocationSize`, `uint32_t memoryTypeIndex`

Extenders in profile: 7 (registry total 25)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkExportMemoryAllocateInfo` | VK_VERSION_1_1 | `VkExternalMemoryHandleTypeFlags handleTypes` |  |
| `VkExportMemoryWin32HandleInfoKHR` | VK_KHR_external_memory_win32 | `const SECURITY_ATTRIBUTES* pAttributes`, `DWORD dwAccess`, `LPCWSTR name` |  |
| `VkImportMemoryFdInfoKHR` | VK_KHR_external_memory_fd | `VkExternalMemoryHandleTypeFlagBits handleType`, `int fd` |  |
| `VkImportMemoryWin32HandleInfoKHR` | VK_KHR_external_memory_win32 | `VkExternalMemoryHandleTypeFlagBits handleType`, `HANDLE handle`, `LPCWSTR name` |  |
| `VkMemoryAllocateFlagsInfo` | VK_VERSION_1_1 | `VkMemoryAllocateFlags flags`, `uint32_t deviceMask` |  |
| `VkMemoryDedicatedAllocateInfo` | VK_VERSION_1_1 | `VkImage image`, `VkBuffer buffer` |  |
| `VkMemoryOpaqueCaptureAddressAllocateInfo` | VK_VERSION_1_2 | `uint64_t opaqueCaptureAddress` |  |

## `VkMemoryBarrier`

Consumed by: vkCmdPipelineBarrier[], vkCmdWaitEvents[]

Flattened parameters (3): `const void* pNext`, `VkAccessFlags srcAccessMask`, `VkAccessFlags dstAccessMask`

Extenders in profile: 0 (registry total 0)

## `VkMemoryGetFdInfoKHR`

Consumed by: vkGetMemoryFdKHR

Flattened parameters (3): `const void* pNext`, `VkDeviceMemory memory`, `VkExternalMemoryHandleTypeFlagBits handleType`

Extenders in profile: 0 (registry total 0)

## `VkMemoryGetWin32HandleInfoKHR`

Consumed by: vkGetMemoryWin32HandleKHR

Flattened parameters (3): `const void* pNext`, `VkDeviceMemory memory`, `VkExternalMemoryHandleTypeFlagBits handleType`

Extenders in profile: 0 (registry total 0)

## `VkPhysicalDeviceExternalBufferInfo`

Consumed by: vkGetPhysicalDeviceExternalBufferProperties

Flattened parameters (4): `const void* pNext`, `VkBufferCreateFlags flags`, `VkBufferUsageFlags usage`, `VkExternalMemoryHandleTypeFlagBits handleType`

Extenders in profile: 0 (registry total 1)

## `VkPhysicalDeviceExternalFenceInfo`

Consumed by: vkGetPhysicalDeviceExternalFenceProperties

Flattened parameters (2): `const void* pNext`, `VkExternalFenceHandleTypeFlagBits handleType`

Extenders in profile: 0 (registry total 0)

## `VkPhysicalDeviceExternalSemaphoreInfo`

Consumed by: vkGetPhysicalDeviceExternalSemaphoreProperties

Flattened parameters (2): `const void* pNext`, `VkExternalSemaphoreHandleTypeFlagBits handleType`

Extenders in profile: 1 (registry total 1)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkSemaphoreTypeCreateInfo` | VK_VERSION_1_2 | `VkSemaphoreType semaphoreType`, `uint64_t initialValue` |  |

## `VkPhysicalDeviceImageFormatInfo2`

Consumed by: vkGetPhysicalDeviceImageFormatProperties2

Flattened parameters (6): `const void* pNext`, `VkFormat format`, `VkImageType type`, `VkImageTiling tiling`, `VkImageUsageFlags usage`, `VkImageCreateFlags flags`

Extenders in profile: 4 (registry total 12)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkImageFormatListCreateInfo` | VK_VERSION_1_2 | `uint32_t viewFormatCount`, `const VkFormat* pViewFormats` |  |
| `VkImageStencilUsageCreateInfo` | VK_VERSION_1_2 | `VkImageUsageFlags stencilUsage` |  |
| `VkPhysicalDeviceExternalImageFormatInfo` | VK_VERSION_1_1 | `VkExternalMemoryHandleTypeFlagBits handleType` |  |
| `VkPhysicalDeviceImageDrmFormatModifierInfoEXT` | VK_EXT_image_drm_format_modifier | `uint64_t drmFormatModifier`, `VkSharingMode sharingMode`, `uint32_t queueFamilyIndexCount`, `const uint32_t* pQueueFamilyIndices` |  |

## `VkPhysicalDeviceSparseImageFormatInfo2`

Consumed by: vkGetPhysicalDeviceSparseImageFormatProperties2

Flattened parameters (6): `const void* pNext`, `VkFormat format`, `VkImageType type`, `VkSampleCountFlagBits samples`, `VkImageUsageFlags usage`, `VkImageTiling tiling`

Extenders in profile: 0 (registry total 1)

## `VkPipelineCacheCreateInfo`

Consumed by: vkCreatePipelineCache

Flattened parameters (4): `const void* pNext`, `VkPipelineCacheCreateFlags flags`, `size_t initialDataSize`, `const void* pInitialData`

Hazards:
- pInitialData: void payload len=initialDataSize

Extenders in profile: 0 (registry total 0)

## `VkPipelineLayoutCreateInfo`

Consumed by: vkCreatePipelineLayout

Flattened parameters (6): `const void* pNext`, `VkPipelineLayoutCreateFlags flags`, `uint32_t setLayoutCount`, `const VkDescriptorSetLayout* pSetLayouts`, `uint32_t pushConstantRangeCount`, `const VkPushConstantRange* pPushConstantRanges`

Hazards:
- pPushConstantRanges: array of plain struct VkPushConstantRange

Extenders in profile: 0 (registry total 0)

## `VkPresentInfoKHR`

Consumed by: vkQueuePresentKHR

Flattened parameters (7): `const void* pNext`, `uint32_t waitSemaphoreCount`, `const VkSemaphore* pWaitSemaphores`, `uint32_t swapchainCount`, `const VkSwapchainKHR* pSwapchains`, `const uint32_t* pImageIndices`, `VkResult* pResults`

Extenders in profile: 1 (registry total 13)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkDeviceGroupPresentInfoKHR` | VK_KHR_swapchain, VK_KHR_device_group | `uint32_t swapchainCount`, `const uint32_t* pDeviceMasks`, `VkDeviceGroupPresentModeFlagBitsKHR mode` |  |

## `VkQueryPoolCreateInfo`

Consumed by: vkCreateQueryPool

Flattened parameters (5): `const void* pNext`, `VkQueryPoolCreateFlags flags`, `VkQueryType queryType`, `uint32_t queryCount`, `VkQueryPipelineStatisticFlags pipelineStatistics`

Extenders in profile: 0 (registry total 14)

## `VkRenderPassBeginInfo`

Consumed by: vkCmdBeginRenderPass, vkCmdBeginRenderPass2

Flattened parameters (6): `const void* pNext`, `VkRenderPass renderPass`, `VkFramebuffer framebuffer`, `VkRect2D renderArea`, `uint32_t clearValueCount`, `const VkClearValue* pClearValues`

Hazards:
- pClearValues: pointer to union VkClearValue

Extenders in profile: 2 (registry total 7)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkDeviceGroupRenderPassBeginInfo` | VK_VERSION_1_1 | `uint32_t deviceMask`, `uint32_t deviceRenderAreaCount`, `const VkRect2D* pDeviceRenderAreas` | pDeviceRenderAreas: array of plain struct VkRect2D |
| `VkRenderPassAttachmentBeginInfo` | VK_VERSION_1_2 | `uint32_t attachmentCount`, `const VkImageView* pAttachments` |  |

## `VkRenderPassCreateInfo`

Consumed by: vkCreateRenderPass

Flattened parameters (8): `const void* pNext`, `VkRenderPassCreateFlags flags`, `uint32_t attachmentCount`, `const VkAttachmentDescription* pAttachments`, `uint32_t subpassCount`, `const VkSubpassDescription* pSubpasses`, `uint32_t dependencyCount`, `const VkSubpassDependency* pDependencies`

Hazards:
- pAttachments: array of plain struct VkAttachmentDescription
- pSubpasses: array of plain struct VkSubpassDescription
- pDependencies: array of plain struct VkSubpassDependency

Extenders in profile: 2 (registry total 5)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkRenderPassInputAttachmentAspectCreateInfo` | VK_VERSION_1_1 | `uint32_t aspectReferenceCount`, `const VkInputAttachmentAspectReference* pAspectReferences` | pAspectReferences: array of plain struct VkInputAttachmentAspectReference |
| `VkRenderPassMultiviewCreateInfo` | VK_VERSION_1_1 | `uint32_t subpassCount`, `const uint32_t* pViewMasks`, `uint32_t dependencyCount`, `const int32_t* pViewOffsets`, `uint32_t correlationMaskCount`, `const uint32_t* pCorrelationMasks` |  |

## `VkRenderPassCreateInfo2`

Consumed by: vkCreateRenderPass2

Flattened parameters (10): `const void* pNext`, `VkRenderPassCreateFlags flags`, `uint32_t attachmentCount`, `const VkAttachmentDescription2* pAttachments`, `uint32_t subpassCount`, `const VkSubpassDescription2* pSubpasses`, `uint32_t dependencyCount`, `const VkSubpassDependency2* pDependencies`, `uint32_t correlatedViewMaskCount`, `const uint32_t* pCorrelatedViewMasks`

Hazards:
- pAttachments: pointer to sType struct VkAttachmentDescription2 (array)
- pSubpasses: pointer to sType struct VkSubpassDescription2 (array)
- pDependencies: pointer to sType struct VkSubpassDependency2 (array)

Extenders in profile: 0 (registry total 5)

## `VkSamplerCreateInfo`

Consumed by: vkCreateSampler

Flattened parameters (17): `const void* pNext`, `VkSamplerCreateFlags flags`, `VkFilter magFilter`, `VkFilter minFilter`, `VkSamplerMipmapMode mipmapMode`, `VkSamplerAddressMode addressModeU`, `VkSamplerAddressMode addressModeV`, `VkSamplerAddressMode addressModeW`, `float mipLodBias`, `VkBool32 anisotropyEnable`, `float maxAnisotropy`, `VkBool32 compareEnable`, `VkCompareOp compareOp`, `float minLod`, `float maxLod`, `VkBorderColor borderColor`, `VkBool32 unnormalizedCoordinates`

Extenders in profile: 3 (registry total 9)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkDebugUtilsObjectNameInfoEXT` | VK_EXT_debug_utils | `VkObjectType objectType`, `uint64_t objectHandle`, `const char* pObjectName` |  |
| `VkSamplerReductionModeCreateInfo` | VK_VERSION_1_2 | `VkSamplerReductionMode reductionMode` |  |
| `VkSamplerYcbcrConversionInfo` | VK_VERSION_1_1 | `VkSamplerYcbcrConversion conversion` |  |

## `VkSamplerYcbcrConversionCreateInfo`

Consumed by: vkCreateSamplerYcbcrConversion

Flattened parameters (9): `const void* pNext`, `VkFormat format`, `VkSamplerYcbcrModelConversion ycbcrModel`, `VkSamplerYcbcrRange ycbcrRange`, `VkComponentMapping components`, `VkChromaLocation xChromaOffset`, `VkChromaLocation yChromaOffset`, `VkFilter chromaFilter`, `VkBool32 forceExplicitReconstruction`

Extenders in profile: 0 (registry total 4)

## `VkSemaphoreCreateInfo`

Consumed by: vkCreateSemaphore

Flattened parameters (2): `const void* pNext`, `VkSemaphoreCreateFlags flags`

Extenders in profile: 3 (registry total 8)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkExportSemaphoreCreateInfo` | VK_VERSION_1_1 | `VkExternalSemaphoreHandleTypeFlags handleTypes` |  |
| `VkExportSemaphoreWin32HandleInfoKHR` | VK_KHR_external_semaphore_win32 | `const SECURITY_ATTRIBUTES* pAttributes`, `DWORD dwAccess`, `LPCWSTR name` |  |
| `VkSemaphoreTypeCreateInfo` | VK_VERSION_1_2 | `VkSemaphoreType semaphoreType`, `uint64_t initialValue` |  |

## `VkSemaphoreGetFdInfoKHR`

Consumed by: vkGetSemaphoreFdKHR

Flattened parameters (3): `const void* pNext`, `VkSemaphore semaphore`, `VkExternalSemaphoreHandleTypeFlagBits handleType`

Extenders in profile: 0 (registry total 0)

## `VkSemaphoreGetWin32HandleInfoKHR`

Consumed by: vkGetSemaphoreWin32HandleKHR

Flattened parameters (3): `const void* pNext`, `VkSemaphore semaphore`, `VkExternalSemaphoreHandleTypeFlagBits handleType`

Extenders in profile: 0 (registry total 0)

## `VkSemaphoreSignalInfo`

Consumed by: vkSignalSemaphore

Flattened parameters (3): `const void* pNext`, `VkSemaphore semaphore`, `uint64_t value`

Extenders in profile: 0 (registry total 0)

## `VkSemaphoreWaitInfo`

Consumed by: vkWaitSemaphores

Flattened parameters (5): `const void* pNext`, `VkSemaphoreWaitFlags flags`, `uint32_t semaphoreCount`, `const VkSemaphore* pSemaphores`, `const uint64_t* pValues`

Extenders in profile: 0 (registry total 0)

## `VkShaderModuleCreateInfo`

Consumed by: vkCreateShaderModule

Flattened parameters (4): `const void* pNext`, `VkShaderModuleCreateFlags flags`, `size_t codeSize`, `const uint32_t* pCode`

Hazards:
- pCode: computed len codeSize / 4

Extenders in profile: 0 (registry total 2)

## `VkSubmitInfo`

Consumed by: vkQueueSubmit[]

Flattened parameters (8): `const void* pNext`, `uint32_t waitSemaphoreCount`, `const VkSemaphore* pWaitSemaphores`, `const VkPipelineStageFlags* pWaitDstStageMask`, `uint32_t commandBufferCount`, `const VkCommandBuffer* pCommandBuffers`, `uint32_t signalSemaphoreCount`, `const VkSemaphore* pSignalSemaphores`

Extenders in profile: 4 (registry total 12)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkD3D12FenceSubmitInfoKHR` | VK_KHR_external_semaphore_win32 | `uint32_t waitSemaphoreValuesCount`, `const uint64_t* pWaitSemaphoreValues`, `uint32_t signalSemaphoreValuesCount`, `const uint64_t* pSignalSemaphoreValues` |  |
| `VkDeviceGroupSubmitInfo` | VK_VERSION_1_1 | `uint32_t waitSemaphoreCount`, `const uint32_t* pWaitSemaphoreDeviceIndices`, `uint32_t commandBufferCount`, `const uint32_t* pCommandBufferDeviceMasks`, `uint32_t signalSemaphoreCount`, `const uint32_t* pSignalSemaphoreDeviceIndices` |  |
| `VkProtectedSubmitInfo` | VK_VERSION_1_1 | `VkBool32 protectedSubmit` |  |
| `VkTimelineSemaphoreSubmitInfo` | VK_VERSION_1_2 | `uint32_t waitSemaphoreValueCount`, `const uint64_t* pWaitSemaphoreValues`, `uint32_t signalSemaphoreValueCount`, `const uint64_t* pSignalSemaphoreValues` |  |

## `VkSubpassBeginInfo`

Consumed by: vkCmdBeginRenderPass2, vkCmdNextSubpass2

Flattened parameters (2): `const void* pNext`, `VkSubpassContents contents`

Extenders in profile: 0 (registry total 0)

## `VkSubpassEndInfo`

Consumed by: vkCmdEndRenderPass2, vkCmdNextSubpass2

Flattened parameters (1): `const void* pNext`

Extenders in profile: 0 (registry total 1)

## `VkSwapchainCreateInfoKHR`

Consumed by: vkCreateSwapchainKHR

Flattened parameters (17): `const void* pNext`, `VkSwapchainCreateFlagsKHR flags`, `VkSurfaceKHR surface`, `uint32_t minImageCount`, `VkFormat imageFormat`, `VkColorSpaceKHR imageColorSpace`, `VkExtent2D imageExtent`, `uint32_t imageArrayLayers`, `VkImageUsageFlags imageUsage`, `VkSharingMode imageSharingMode`, `uint32_t queueFamilyIndexCount`, `const uint32_t* pQueueFamilyIndices`, `VkSurfaceTransformFlagBitsKHR preTransform`, `VkCompositeAlphaFlagBitsKHR compositeAlpha`, `VkPresentModeKHR presentMode`, `VkBool32 clipped`, `VkSwapchainKHR oldSwapchain`

Extenders in profile: 2 (registry total 12)

| Extender | Origin | Extra parameters | Hazards |
|---|---|---|---|
| `VkDeviceGroupSwapchainCreateInfoKHR` | VK_KHR_swapchain, VK_KHR_device_group | `VkDeviceGroupPresentModeFlagsKHR modes` |  |
| `VkImageFormatListCreateInfo` | VK_VERSION_1_2 | `uint32_t viewFormatCount`, `const VkFormat* pViewFormats` |  |

## `VkWin32SurfaceCreateInfoKHR`

Consumed by: vkCreateWin32SurfaceKHR

Flattened parameters (4): `const void* pNext`, `VkWin32SurfaceCreateFlagsKHR flags`, `HINSTANCE hinstance`, `HWND hwnd`

Extenders in profile: 0 (registry total 0)

## `VkWriteDescriptorSet`

Consumed by: vkUpdateDescriptorSets[]

Flattened parameters (9): `const void* pNext`, `VkDescriptorSet dstSet`, `uint32_t dstBinding`, `uint32_t dstArrayElement`, `uint32_t descriptorCount`, `VkDescriptorType descriptorType`, `const VkDescriptorImageInfo* pImageInfo`, `const VkDescriptorBufferInfo* pBufferInfo`, `const VkBufferView* pTexelBufferView`

Hazards:
- pImageInfo: array of plain struct VkDescriptorImageInfo
- pBufferInfo: array of plain struct VkDescriptorBufferInfo

Extenders in profile: 0 (registry total 5)

## `VkXlibSurfaceCreateInfoKHR`

Consumed by: vkCreateXlibSurfaceKHR

Flattened parameters (4): `const void* pNext`, `VkXlibSurfaceCreateFlagsKHR flags`, `Display* dpy`, `Window window`

Extenders in profile: 0 (registry total 0)

## Extenders that extend several bases (one qualifier, several tier-0 targets)

- `VkImageFormatListCreateInfo` -> VkImageCreateInfo, VkPhysicalDeviceImageFormatInfo2, VkSwapchainCreateInfoKHR
- `VkImageStencilUsageCreateInfo` -> VkImageCreateInfo, VkPhysicalDeviceImageFormatInfo2
- `VkSamplerYcbcrConversionInfo` -> VkImageViewCreateInfo, VkSamplerCreateInfo
- `VkSemaphoreTypeCreateInfo` -> VkPhysicalDeviceExternalSemaphoreInfo, VkSemaphoreCreateInfo
- `VkTimelineSemaphoreSubmitInfo` -> VkBindSparseInfo, VkSubmitInfo

Total in-profile (base, extender) pairs: 76. Each pair is one candidate tier-1 function.
