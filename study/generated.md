# Reference module study: `cvk*` in `.`

Functions: 470. Vulkan commands reached: 224. Reference extension set: 0.

## Tier histogram (derived mechanically from body shape)

| Tier | Meaning | Count |
|---|---|---|
| 0 | calls one vk* directly, no wrapper callees | 222 |
| 1 | one wrapper callee + builds an sType struct | 44 |
| 1' | one wrapper callee, constants only | 0 |
| 2 | several callees, Setup-named | 0 |
| 3 | several callees, domain verb | 0 |
| 0+mixed | calls vk* AND wrappers (should be split) | 0 |
| util | pure C helper | 202 |
| infra | check/procaddr | 2 |

## Tier 0: name and parameter fidelity against mechanical flattening

Expected = Vulkan params in order, with each single sType input struct replaced by its members (minus sType). A tier-0 function that differs is a *decision* the pattern must either canonicalize or forbid.

| Function | vk command | Name = vk name? | Params match? | Difference |
|---|---|---|---|---|
| `AcquireNextImage` | `vkAcquireNextImageKHR` | yes | yes |  |
| `AcquireNextImage2` | `vkAcquireNextImage2KHR` | yes | yes |  |
| `AllocateCommandBuffers` | `vkAllocateCommandBuffers` | yes | yes |  |
| `AllocateDescriptorSets` | `vkAllocateDescriptorSets` | yes | yes |  |
| `AllocateMemory` | `vkAllocateMemory` | yes | yes |  |
| `BeginCommandBuffer` | `vkBeginCommandBuffer` | yes | yes |  |
| `BindBufferMemory` | `vkBindBufferMemory` | yes | yes |  |
| `BindBufferMemory2` | `vkBindBufferMemory2` | yes | yes |  |
| `BindImageMemory` | `vkBindImageMemory` | yes | yes |  |
| `BindImageMemory2` | `vkBindImageMemory2` | yes | yes |  |
| `CmdBeginDebugUtilsLabel` | `vkCmdBeginDebugUtilsLabelEXT` | yes | yes |  |
| `CmdBeginQuery` | `vkCmdBeginQuery` | yes | yes |  |
| `CmdBeginRenderPass` | `vkCmdBeginRenderPass` | yes | yes |  |
| `CmdBeginRenderPass2` | `vkCmdBeginRenderPass2` | yes | yes |  |
| `CmdBindDescriptorSets` | `vkCmdBindDescriptorSets` | yes | yes |  |
| `CmdBindIndexBuffer` | `vkCmdBindIndexBuffer` | yes | yes |  |
| `CmdBindPipeline` | `vkCmdBindPipeline` | yes | yes |  |
| `CmdBindVertexBuffers` | `vkCmdBindVertexBuffers` | yes | yes |  |
| `CmdBlitImage` | `vkCmdBlitImage` | yes | yes |  |
| `CmdClearAttachments` | `vkCmdClearAttachments` | yes | yes |  |
| `CmdClearColorImage` | `vkCmdClearColorImage` | yes | yes |  |
| `CmdClearDepthStencilImage` | `vkCmdClearDepthStencilImage` | yes | yes |  |
| `CmdCopyBuffer` | `vkCmdCopyBuffer` | yes | yes |  |
| `CmdCopyBufferToImage` | `vkCmdCopyBufferToImage` | yes | yes |  |
| `CmdCopyImage` | `vkCmdCopyImage` | yes | yes |  |
| `CmdCopyImageToBuffer` | `vkCmdCopyImageToBuffer` | yes | yes |  |
| `CmdCopyQueryPoolResults` | `vkCmdCopyQueryPoolResults` | yes | yes |  |
| `CmdDispatch` | `vkCmdDispatch` | yes | yes |  |
| `CmdDispatchBase` | `vkCmdDispatchBase` | yes | yes |  |
| `CmdDispatchIndirect` | `vkCmdDispatchIndirect` | yes | yes |  |
| `CmdDraw` | `vkCmdDraw` | yes | yes |  |
| `CmdDrawIndexed` | `vkCmdDrawIndexed` | yes | yes |  |
| `CmdDrawIndexedIndirect` | `vkCmdDrawIndexedIndirect` | yes | yes |  |
| `CmdDrawIndexedIndirectCount` | `vkCmdDrawIndexedIndirectCount` | yes | yes |  |
| `CmdDrawIndirect` | `vkCmdDrawIndirect` | yes | yes |  |
| `CmdDrawIndirectCount` | `vkCmdDrawIndirectCount` | yes | yes |  |
| `CmdEndDebugUtilsLabel` | `vkCmdEndDebugUtilsLabelEXT` | yes | yes |  |
| `CmdEndQuery` | `vkCmdEndQuery` | yes | yes |  |
| `CmdEndRenderPass` | `vkCmdEndRenderPass` | yes | yes |  |
| `CmdEndRenderPass2` | `vkCmdEndRenderPass2` | yes | yes |  |
| `CmdExecuteCommands` | `vkCmdExecuteCommands` | yes | yes |  |
| `CmdFillBuffer` | `vkCmdFillBuffer` | yes | yes |  |
| `CmdInsertDebugUtilsLabel` | `vkCmdInsertDebugUtilsLabelEXT` | yes | yes |  |
| `CmdNextSubpass` | `vkCmdNextSubpass` | yes | yes |  |
| `CmdNextSubpass2` | `vkCmdNextSubpass2` | yes | yes |  |
| `CmdPipelineBarrier` | `vkCmdPipelineBarrier` | yes | yes |  |
| `CmdPushConstants` | `vkCmdPushConstants` | yes | yes |  |
| `CmdResetEvent` | `vkCmdResetEvent` | yes | yes |  |
| `CmdResetQueryPool` | `vkCmdResetQueryPool` | yes | yes |  |
| `CmdResolveImage` | `vkCmdResolveImage` | yes | yes |  |
| `CmdSetBlendConstants` | `vkCmdSetBlendConstants` | yes | yes |  |
| `CmdSetDepthBias` | `vkCmdSetDepthBias` | yes | yes |  |
| `CmdSetDepthBounds` | `vkCmdSetDepthBounds` | yes | yes |  |
| `CmdSetDeviceMask` | `vkCmdSetDeviceMask` | yes | yes |  |
| `CmdSetEvent` | `vkCmdSetEvent` | yes | yes |  |
| `CmdSetLineWidth` | `vkCmdSetLineWidth` | yes | yes |  |
| `CmdSetScissor` | `vkCmdSetScissor` | yes | yes |  |
| `CmdSetStencilCompareMask` | `vkCmdSetStencilCompareMask` | yes | yes |  |
| `CmdSetStencilReference` | `vkCmdSetStencilReference` | yes | yes |  |
| `CmdSetStencilWriteMask` | `vkCmdSetStencilWriteMask` | yes | yes |  |
| `CmdSetViewport` | `vkCmdSetViewport` | yes | yes |  |
| `CmdUpdateBuffer` | `vkCmdUpdateBuffer` | yes | yes |  |
| `CmdWaitEvents` | `vkCmdWaitEvents` | yes | yes |  |
| `CmdWriteTimestamp` | `vkCmdWriteTimestamp` | yes | yes |  |
| `CreateBuffer` | `vkCreateBuffer` | yes | yes |  |
| `CreateBufferView` | `vkCreateBufferView` | yes | yes |  |
| `CreateCommandPool` | `vkCreateCommandPool` | yes | yes |  |
| `CreateComputePipelines` | `vkCreateComputePipelines` | yes | yes |  |
| `CreateDebugUtilsMessenger` | `vkCreateDebugUtilsMessengerEXT` | yes | yes |  |
| `CreateDescriptorPool` | `vkCreateDescriptorPool` | yes | yes |  |
| `CreateDescriptorSetLayout` | `vkCreateDescriptorSetLayout` | yes | yes |  |
| `CreateDescriptorUpdateTemplate` | `vkCreateDescriptorUpdateTemplate` | yes | yes |  |
| `CreateDevice` | `vkCreateDevice` | yes | yes |  |
| `CreateDisplayMode` | `vkCreateDisplayModeKHR` | yes | yes |  |
| `CreateDisplayPlaneSurface` | `vkCreateDisplayPlaneSurfaceKHR` | yes | yes |  |
| `CreateEvent` | `vkCreateEvent` | yes | yes |  |
| `CreateFence` | `vkCreateFence` | yes | yes |  |
| `CreateFramebuffer` | `vkCreateFramebuffer` | yes | yes |  |
| `CreateGraphicsPipelines` | `vkCreateGraphicsPipelines` | yes | yes |  |
| `CreateHeadlessSurface` | `vkCreateHeadlessSurfaceEXT` | yes | yes |  |
| `CreateImage` | `vkCreateImage` | yes | yes |  |
| `CreateImageView` | `vkCreateImageView` | yes | yes |  |
| `CreateInstance` | `vkCreateInstance` | yes | yes |  |
| `CreatePipelineCache` | `vkCreatePipelineCache` | yes | yes |  |
| `CreatePipelineLayout` | `vkCreatePipelineLayout` | yes | yes |  |
| `CreateQueryPool` | `vkCreateQueryPool` | yes | yes |  |
| `CreateRenderPass` | `vkCreateRenderPass` | yes | yes |  |
| `CreateRenderPass2` | `vkCreateRenderPass2` | yes | yes |  |
| `CreateSampler` | `vkCreateSampler` | yes | yes |  |
| `CreateSamplerYcbcrConversion` | `vkCreateSamplerYcbcrConversion` | yes | yes |  |
| `CreateSemaphore` | `vkCreateSemaphore` | yes | yes |  |
| `CreateShaderModule` | `vkCreateShaderModule` | yes | yes |  |
| `CreateSwapchain` | `vkCreateSwapchainKHR` | yes | yes |  |
| `CreateWin32Surface` | `vkCreateWin32SurfaceKHR` | yes | yes |  |
| `CreateXlibSurface` | `vkCreateXlibSurfaceKHR` | yes | yes |  |
| `DestroyBuffer` | `vkDestroyBuffer` | yes | yes |  |
| `DestroyBufferView` | `vkDestroyBufferView` | yes | yes |  |
| `DestroyCommandPool` | `vkDestroyCommandPool` | yes | yes |  |
| `DestroyDebugUtilsMessenger` | `vkDestroyDebugUtilsMessengerEXT` | yes | yes |  |
| `DestroyDescriptorPool` | `vkDestroyDescriptorPool` | yes | yes |  |
| `DestroyDescriptorSetLayout` | `vkDestroyDescriptorSetLayout` | yes | yes |  |
| `DestroyDescriptorUpdateTemplate` | `vkDestroyDescriptorUpdateTemplate` | yes | yes |  |
| `DestroyDevice` | `vkDestroyDevice` | yes | yes |  |
| `DestroyEvent` | `vkDestroyEvent` | yes | yes |  |
| `DestroyFence` | `vkDestroyFence` | yes | yes |  |
| `DestroyFramebuffer` | `vkDestroyFramebuffer` | yes | yes |  |
| `DestroyImage` | `vkDestroyImage` | yes | yes |  |
| `DestroyImageView` | `vkDestroyImageView` | yes | yes |  |
| `DestroyInstance` | `vkDestroyInstance` | yes | yes |  |
| `DestroyPipeline` | `vkDestroyPipeline` | yes | yes |  |
| `DestroyPipelineCache` | `vkDestroyPipelineCache` | yes | yes |  |
| `DestroyPipelineLayout` | `vkDestroyPipelineLayout` | yes | yes |  |
| `DestroyQueryPool` | `vkDestroyQueryPool` | yes | yes |  |
| `DestroyRenderPass` | `vkDestroyRenderPass` | yes | yes |  |
| `DestroySampler` | `vkDestroySampler` | yes | yes |  |
| `DestroySamplerYcbcrConversion` | `vkDestroySamplerYcbcrConversion` | yes | yes |  |
| `DestroySemaphore` | `vkDestroySemaphore` | yes | yes |  |
| `DestroyShaderModule` | `vkDestroyShaderModule` | yes | yes |  |
| `DestroySurface` | `vkDestroySurfaceKHR` | yes | yes |  |
| `DestroySwapchain` | `vkDestroySwapchainKHR` | yes | yes |  |
| `DeviceWaitIdle` | `vkDeviceWaitIdle` | yes | yes |  |
| `EndCommandBuffer` | `vkEndCommandBuffer` | yes | yes |  |
| `EnumerateDeviceExtensionProperties` | `vkEnumerateDeviceExtensionProperties` | yes | yes |  |
| `EnumerateDeviceLayerProperties` | `vkEnumerateDeviceLayerProperties` | yes | yes |  |
| `EnumerateInstanceExtensionProperties` | `vkEnumerateInstanceExtensionProperties` | yes | yes |  |
| `EnumerateInstanceLayerProperties` | `vkEnumerateInstanceLayerProperties` | yes | yes |  |
| `EnumerateInstanceVersion` | `vkEnumerateInstanceVersion` | yes | yes |  |
| `EnumeratePhysicalDeviceGroups` | `vkEnumeratePhysicalDeviceGroups` | yes | yes |  |
| `EnumeratePhysicalDevices` | `vkEnumeratePhysicalDevices` | yes | yes |  |
| `FlushMappedMemoryRanges` | `vkFlushMappedMemoryRanges` | yes | yes |  |
| `FreeCommandBuffers` | `vkFreeCommandBuffers` | yes | yes |  |
| `FreeDescriptorSets` | `vkFreeDescriptorSets` | yes | yes |  |
| `FreeMemory` | `vkFreeMemory` | yes | yes |  |
| `GetBufferDeviceAddress` | `vkGetBufferDeviceAddress` | yes | yes |  |
| `GetBufferMemoryRequirements` | `vkGetBufferMemoryRequirements` | yes | yes |  |
| `GetBufferMemoryRequirements2` | `vkGetBufferMemoryRequirements2` | yes | yes |  |
| `GetBufferOpaqueCaptureAddress` | `vkGetBufferOpaqueCaptureAddress` | yes | yes |  |
| `GetDescriptorSetLayoutSupport` | `vkGetDescriptorSetLayoutSupport` | yes | yes |  |
| `GetDeviceGroupPeerMemoryFeatures` | `vkGetDeviceGroupPeerMemoryFeatures` | yes | yes |  |
| `GetDeviceGroupPresentCapabilities` | `vkGetDeviceGroupPresentCapabilitiesKHR` | yes | yes |  |
| `GetDeviceGroupSurfacePresentModes` | `vkGetDeviceGroupSurfacePresentModesKHR` | yes | yes |  |
| `GetDeviceMemoryCommitment` | `vkGetDeviceMemoryCommitment` | yes | yes |  |
| `GetDeviceMemoryOpaqueCaptureAddress` | `vkGetDeviceMemoryOpaqueCaptureAddress` | yes | yes |  |
| `GetDeviceQueue` | `vkGetDeviceQueue` | yes | yes |  |
| `GetDeviceQueue2` | `vkGetDeviceQueue2` | yes | yes |  |
| `GetDisplayModeProperties` | `vkGetDisplayModePropertiesKHR` | yes | yes |  |
| `GetDisplayPlaneCapabilities` | `vkGetDisplayPlaneCapabilitiesKHR` | yes | yes |  |
| `GetDisplayPlaneSupportedDisplays` | `vkGetDisplayPlaneSupportedDisplaysKHR` | yes | yes |  |
| `GetEventStatus` | `vkGetEventStatus` | yes | yes |  |
| `GetFenceStatus` | `vkGetFenceStatus` | yes | yes |  |
| `GetImageDrmFormatModifierProperties` | `vkGetImageDrmFormatModifierPropertiesEXT` | yes | yes |  |
| `GetImageMemoryRequirements` | `vkGetImageMemoryRequirements` | yes | yes |  |
| `GetImageMemoryRequirements2` | `vkGetImageMemoryRequirements2` | yes | yes |  |
| `GetImageSparseMemoryRequirements` | `vkGetImageSparseMemoryRequirements` | yes | yes |  |
| `GetImageSparseMemoryRequirements2` | `vkGetImageSparseMemoryRequirements2` | yes | yes |  |
| `GetImageSubresourceLayout` | `vkGetImageSubresourceLayout` | yes | yes |  |
| `GetMemoryFd` | `vkGetMemoryFdKHR` | yes | yes |  |
| `GetMemoryFdProperties` | `vkGetMemoryFdPropertiesKHR` | yes | yes |  |
| `GetMemoryWin32Handle` | `vkGetMemoryWin32HandleKHR` | yes | yes |  |
| `GetMemoryWin32HandleProperties` | `vkGetMemoryWin32HandlePropertiesKHR` | yes | yes |  |
| `GetPhysicalDeviceDisplayPlaneProperties` | `vkGetPhysicalDeviceDisplayPlanePropertiesKHR` | yes | yes |  |
| `GetPhysicalDeviceDisplayProperties` | `vkGetPhysicalDeviceDisplayPropertiesKHR` | yes | yes |  |
| `GetPhysicalDeviceExternalBufferProperties` | `vkGetPhysicalDeviceExternalBufferProperties` | yes | yes |  |
| `GetPhysicalDeviceExternalFenceProperties` | `vkGetPhysicalDeviceExternalFenceProperties` | yes | yes |  |
| `GetPhysicalDeviceExternalSemaphoreProperties` | `vkGetPhysicalDeviceExternalSemaphoreProperties` | yes | yes |  |
| `GetPhysicalDeviceFeatures` | `vkGetPhysicalDeviceFeatures` | yes | yes |  |
| `GetPhysicalDeviceFeatures2` | `vkGetPhysicalDeviceFeatures2` | yes | yes |  |
| `GetPhysicalDeviceFormatProperties` | `vkGetPhysicalDeviceFormatProperties` | yes | yes |  |
| `GetPhysicalDeviceFormatProperties2` | `vkGetPhysicalDeviceFormatProperties2` | yes | yes |  |
| `GetPhysicalDeviceImageFormatProperties` | `vkGetPhysicalDeviceImageFormatProperties` | yes | yes |  |
| `GetPhysicalDeviceImageFormatProperties2` | `vkGetPhysicalDeviceImageFormatProperties2` | yes | yes |  |
| `GetPhysicalDeviceMemoryProperties` | `vkGetPhysicalDeviceMemoryProperties` | yes | yes |  |
| `GetPhysicalDeviceMemoryProperties2` | `vkGetPhysicalDeviceMemoryProperties2` | yes | yes |  |
| `GetPhysicalDevicePresentRectangles` | `vkGetPhysicalDevicePresentRectanglesKHR` | yes | yes |  |
| `GetPhysicalDeviceProperties` | `vkGetPhysicalDeviceProperties` | yes | yes |  |
| `GetPhysicalDeviceProperties2` | `vkGetPhysicalDeviceProperties2` | yes | yes |  |
| `GetPhysicalDeviceQueueFamilyProperties` | `vkGetPhysicalDeviceQueueFamilyProperties` | yes | yes |  |
| `GetPhysicalDeviceQueueFamilyProperties2` | `vkGetPhysicalDeviceQueueFamilyProperties2` | yes | yes |  |
| `GetPhysicalDeviceSparseImageFormatProperties` | `vkGetPhysicalDeviceSparseImageFormatProperties` | yes | yes |  |
| `GetPhysicalDeviceSparseImageFormatProperties2` | `vkGetPhysicalDeviceSparseImageFormatProperties2` | yes | yes |  |
| `GetPhysicalDeviceSurfaceCapabilities` | `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` | yes | yes |  |
| `GetPhysicalDeviceSurfaceFormats` | `vkGetPhysicalDeviceSurfaceFormatsKHR` | yes | yes |  |
| `GetPhysicalDeviceSurfacePresentModes` | `vkGetPhysicalDeviceSurfacePresentModesKHR` | yes | yes |  |
| `GetPhysicalDeviceSurfaceSupport` | `vkGetPhysicalDeviceSurfaceSupportKHR` | yes | yes |  |
| `GetPhysicalDeviceWin32PresentationSupport` | `vkGetPhysicalDeviceWin32PresentationSupportKHR` | yes | yes |  |
| `GetPhysicalDeviceXlibPresentationSupport` | `vkGetPhysicalDeviceXlibPresentationSupportKHR` | yes | yes |  |
| `GetPipelineCacheData` | `vkGetPipelineCacheData` | yes | yes |  |
| `GetQueryPoolResults` | `vkGetQueryPoolResults` | yes | yes |  |
| `GetRenderAreaGranularity` | `vkGetRenderAreaGranularity` | yes | yes |  |
| `GetSemaphoreCounterValue` | `vkGetSemaphoreCounterValue` | yes | yes |  |
| `GetSemaphoreFd` | `vkGetSemaphoreFdKHR` | yes | yes |  |
| `GetSemaphoreWin32Handle` | `vkGetSemaphoreWin32HandleKHR` | yes | yes |  |
| `GetSwapchainImages` | `vkGetSwapchainImagesKHR` | yes | yes |  |
| `ImportSemaphoreFd` | `vkImportSemaphoreFdKHR` | yes | yes |  |
| `ImportSemaphoreWin32Handle` | `vkImportSemaphoreWin32HandleKHR` | yes | yes |  |
| `InvalidateMappedMemoryRanges` | `vkInvalidateMappedMemoryRanges` | yes | yes |  |
| `MapMemory` | `vkMapMemory` | yes | yes |  |
| `MergePipelineCaches` | `vkMergePipelineCaches` | yes | yes |  |
| `QueueBeginDebugUtilsLabel` | `vkQueueBeginDebugUtilsLabelEXT` | yes | yes |  |
| `QueueBindSparse` | `vkQueueBindSparse` | yes | yes |  |
| `QueueEndDebugUtilsLabel` | `vkQueueEndDebugUtilsLabelEXT` | yes | yes |  |
| `QueueInsertDebugUtilsLabel` | `vkQueueInsertDebugUtilsLabelEXT` | yes | yes |  |
| `QueuePresent` | `vkQueuePresentKHR` | yes | yes |  |
| `QueueSubmit` | `vkQueueSubmit` | yes | yes |  |
| `QueueWaitIdle` | `vkQueueWaitIdle` | yes | yes |  |
| `ResetCommandBuffer` | `vkResetCommandBuffer` | yes | yes |  |
| `ResetCommandPool` | `vkResetCommandPool` | yes | yes |  |
| `ResetDescriptorPool` | `vkResetDescriptorPool` | yes | yes |  |
| `ResetEvent` | `vkResetEvent` | yes | yes |  |
| `ResetFences` | `vkResetFences` | yes | yes |  |
| `ResetQueryPool` | `vkResetQueryPool` | yes | yes |  |
| `SetDebugUtilsObjectName` | `vkSetDebugUtilsObjectNameEXT` | yes | yes |  |
| `SetDebugUtilsObjectTag` | `vkSetDebugUtilsObjectTagEXT` | yes | yes |  |
| `SetEvent` | `vkSetEvent` | yes | yes |  |
| `SignalSemaphore` | `vkSignalSemaphore` | yes | yes |  |
| `SubmitDebugUtilsMessage` | `vkSubmitDebugUtilsMessageEXT` | yes | yes |  |
| `TrimCommandPool` | `vkTrimCommandPool` | yes | yes |  |
| `UnmapMemory` | `vkUnmapMemory` | yes | yes |  |
| `UpdateDescriptorSetWithTemplate` | `vkUpdateDescriptorSetWithTemplate` | yes | yes |  |
| `UpdateDescriptorSets` | `vkUpdateDescriptorSets` | yes | yes |  |
| `WaitForFences` | `vkWaitForFences` | yes | yes |  |
| `WaitSemaphores` | `vkWaitSemaphores` | yes | yes |  |

Tier-0 functions: 222; exact vk name: 222; exact mechanical params: 222.

## Mixed functions (call vk* directly and also call wrappers)


## Coverage of the reference profile

Profile = VK_VERSION_1_2 + 

Wrapped: 178 / 178. Missing: 0. Used outside profile: 46.


Used but outside the computed profile (extension not named in sources, or platform-only):
- `vkAcquireNextImage2KHR` ['VK_KHR_swapchain', 'VK_KHR_device_group']
- `vkAcquireNextImageKHR` ['VK_KHR_swapchain']
- `vkCmdBeginDebugUtilsLabelEXT` ['VK_EXT_debug_utils']
- `vkCmdEndDebugUtilsLabelEXT` ['VK_EXT_debug_utils']
- `vkCmdInsertDebugUtilsLabelEXT` ['VK_EXT_debug_utils']
- `vkCreateDebugUtilsMessengerEXT` ['VK_EXT_debug_utils']
- `vkCreateDisplayModeKHR` ['VK_KHR_display']
- `vkCreateDisplayPlaneSurfaceKHR` ['VK_KHR_display']
- `vkCreateHeadlessSurfaceEXT` ['VK_EXT_headless_surface']
- `vkCreateSwapchainKHR` ['VK_KHR_swapchain']
- `vkCreateWin32SurfaceKHR` ['VK_KHR_win32_surface']
- `vkCreateXlibSurfaceKHR` ['VK_KHR_xlib_surface']
- `vkDestroyDebugUtilsMessengerEXT` ['VK_EXT_debug_utils']
- `vkDestroySurfaceKHR` ['VK_KHR_surface']
- `vkDestroySwapchainKHR` ['VK_KHR_swapchain']
- `vkGetDeviceGroupPresentCapabilitiesKHR` ['VK_KHR_swapchain', 'VK_KHR_device_group']
- `vkGetDeviceGroupSurfacePresentModesKHR` ['VK_KHR_swapchain', 'VK_KHR_device_group']
- `vkGetDisplayModePropertiesKHR` ['VK_KHR_display']
- `vkGetDisplayPlaneCapabilitiesKHR` ['VK_KHR_display']
- `vkGetDisplayPlaneSupportedDisplaysKHR` ['VK_KHR_display']
- `vkGetImageDrmFormatModifierPropertiesEXT` ['VK_EXT_image_drm_format_modifier']
- `vkGetMemoryFdKHR` ['VK_KHR_external_memory_fd']
- `vkGetMemoryFdPropertiesKHR` ['VK_KHR_external_memory_fd']
- `vkGetMemoryWin32HandleKHR` ['VK_KHR_external_memory_win32']
- `vkGetMemoryWin32HandlePropertiesKHR` ['VK_KHR_external_memory_win32']
- `vkGetPhysicalDeviceDisplayPlanePropertiesKHR` ['VK_KHR_display']
- `vkGetPhysicalDeviceDisplayPropertiesKHR` ['VK_KHR_display']
- `vkGetPhysicalDevicePresentRectanglesKHR` ['VK_KHR_swapchain', 'VK_KHR_device_group']
- `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` ['VK_KHR_surface']
- `vkGetPhysicalDeviceSurfaceFormatsKHR` ['VK_KHR_surface']
- `vkGetPhysicalDeviceSurfacePresentModesKHR` ['VK_KHR_surface']
- `vkGetPhysicalDeviceSurfaceSupportKHR` ['VK_KHR_surface']
- `vkGetPhysicalDeviceWin32PresentationSupportKHR` ['VK_KHR_win32_surface']
- `vkGetPhysicalDeviceXlibPresentationSupportKHR` ['VK_KHR_xlib_surface']
- `vkGetSemaphoreFdKHR` ['VK_KHR_external_semaphore_fd']
- `vkGetSemaphoreWin32HandleKHR` ['VK_KHR_external_semaphore_win32']
- `vkGetSwapchainImagesKHR` ['VK_KHR_swapchain']
- `vkImportSemaphoreFdKHR` ['VK_KHR_external_semaphore_fd']
- `vkImportSemaphoreWin32HandleKHR` ['VK_KHR_external_semaphore_win32']
- `vkQueueBeginDebugUtilsLabelEXT` ['VK_EXT_debug_utils']
- `vkQueueEndDebugUtilsLabelEXT` ['VK_EXT_debug_utils']
- `vkQueueInsertDebugUtilsLabelEXT` ['VK_EXT_debug_utils']
- `vkQueuePresentKHR` ['VK_KHR_swapchain']
- `vkSetDebugUtilsObjectNameEXT` ['VK_EXT_debug_utils']
- `vkSetDebugUtilsObjectTagEXT` ['VK_EXT_debug_utils']
- `vkSubmitDebugUtilsMessageEXT` ['VK_EXT_debug_utils']

## Documentation completeness (header prototypes)

Prototypes with a doc block: 468; param-count mismatches or missing brief: 0; no doc block found: 2.
- `CheckResult`: no doc block (cvk.c)
- `Fatal`: no doc block (cvk.c)

## Name-shape conventions observed

| Leading verb | Count |
|---|---|
| Init | 200 |
| Get | 69 |
| Cmd | 56 |
| Create | 56 |
| (other) | 43 |
| Allocate | 11 |
| Queue | 8 |
| Reset | 6 |
| Bind | 4 |
| Acquire | 2 |
| Begin | 2 |
| Import | 2 |
| Update | 2 |
| Wait | 2 |
| Check | 1 |
| Device | 1 |
| End | 1 |
| Flush | 1 |
| Invalidate | 1 |
| Map | 1 |
| Unmap | 1 |

## Per-function table

| Function | Group | Tier | Depth | vk | Wrapper callees | sType | Asserts | Params |
|---|---|---|---|---|---|---|---|---|
| `AcquireNextImage` | generated | 0 | 1 | vkAcquireNextImageKHR |  | 0 | 0 | 6 |
| `AcquireNextImage2` | generated | 0 | 1 | vkAcquireNextImage2KHR |  | 1 | 0 | 8 |
| `AllocateCommandBuffers` | generated | 0 | 1 | vkAllocateCommandBuffers |  | 1 | 0 | 6 |
| `AllocateDedicatedMemory` | generated | 1 | 2 |  | AllocateMemory | 1 | 0 | 8 |
| `AllocateDescriptorSets` | generated | 0 | 1 | vkAllocateDescriptorSets |  | 1 | 0 | 6 |
| `AllocateExportMemory` | generated | 1 | 2 |  | AllocateMemory | 1 | 0 | 7 |
| `AllocateExportWin32HandleMemory` | generated | 1 | 2 |  | AllocateMemory | 1 | 0 | 9 |
| `AllocateFlagsMemory` | generated | 1 | 2 |  | AllocateMemory | 1 | 0 | 8 |
| `AllocateImportFdMemory` | generated | 1 | 2 |  | AllocateMemory | 1 | 0 | 8 |
| `AllocateImportWin32HandleMemory` | generated | 1 | 2 |  | AllocateMemory | 1 | 0 | 9 |
| `AllocateMemory` | generated | 0 | 1 | vkAllocateMemory |  | 1 | 0 | 6 |
| `AllocateOpaqueCaptureAddressMemory` | generated | 1 | 2 |  | AllocateMemory | 1 | 0 | 7 |
| `AllocateVariableDescriptorCountDescriptorSets` | generated | 1 | 2 |  | AllocateDescriptorSets | 1 | 0 | 8 |
| `BeginCommandBuffer` | generated | 0 | 1 | vkBeginCommandBuffer |  | 1 | 0 | 4 |
| `BeginDeviceGroupCommandBuffer` | generated | 1 | 2 |  | BeginCommandBuffer | 1 | 0 | 5 |
| `BindBufferMemory` | generated | 0 | 1 | vkBindBufferMemory |  | 0 | 0 | 4 |
| `BindBufferMemory2` | generated | 0 | 1 | vkBindBufferMemory2 |  | 0 | 0 | 3 |
| `BindImageMemory` | generated | 0 | 1 | vkBindImageMemory |  | 0 | 0 | 4 |
| `BindImageMemory2` | generated | 0 | 1 | vkBindImageMemory2 |  | 0 | 0 | 3 |
| `CheckResult` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `CmdAttachmentBeginRenderPass` | generated | 1 | 2 |  | CmdBeginRenderPass | 1 | 0 | 10 |
| `CmdBeginDebugUtilsLabel` | generated | 0 | 1 | vkCmdBeginDebugUtilsLabelEXT |  | 1 | 0 | 4 |
| `CmdBeginQuery` | generated | 0 | 1 | vkCmdBeginQuery |  | 0 | 0 | 4 |
| `CmdBeginRenderPass` | generated | 0 | 1 | vkCmdBeginRenderPass |  | 1 | 0 | 8 |
| `CmdBeginRenderPass2` | generated | 0 | 1 | vkCmdBeginRenderPass2 |  | 2 | 0 | 9 |
| `CmdBindDescriptorSets` | generated | 0 | 1 | vkCmdBindDescriptorSets |  | 0 | 0 | 8 |
| `CmdBindIndexBuffer` | generated | 0 | 1 | vkCmdBindIndexBuffer |  | 0 | 0 | 4 |
| `CmdBindPipeline` | generated | 0 | 1 | vkCmdBindPipeline |  | 0 | 0 | 3 |
| `CmdBindVertexBuffers` | generated | 0 | 1 | vkCmdBindVertexBuffers |  | 0 | 0 | 5 |
| `CmdBlitImage` | generated | 0 | 1 | vkCmdBlitImage |  | 0 | 0 | 8 |
| `CmdClearAttachments` | generated | 0 | 1 | vkCmdClearAttachments |  | 0 | 0 | 5 |
| `CmdClearColorImage` | generated | 0 | 1 | vkCmdClearColorImage |  | 0 | 0 | 6 |
| `CmdClearDepthStencilImage` | generated | 0 | 1 | vkCmdClearDepthStencilImage |  | 0 | 0 | 6 |
| `CmdCopyBuffer` | generated | 0 | 1 | vkCmdCopyBuffer |  | 0 | 0 | 5 |
| `CmdCopyBufferToImage` | generated | 0 | 1 | vkCmdCopyBufferToImage |  | 0 | 0 | 6 |
| `CmdCopyImage` | generated | 0 | 1 | vkCmdCopyImage |  | 0 | 0 | 7 |
| `CmdCopyImageToBuffer` | generated | 0 | 1 | vkCmdCopyImageToBuffer |  | 0 | 0 | 6 |
| `CmdCopyQueryPoolResults` | generated | 0 | 1 | vkCmdCopyQueryPoolResults |  | 0 | 0 | 8 |
| `CmdDeviceGroupBeginRenderPass` | generated | 1 | 2 |  | CmdBeginRenderPass | 1 | 0 | 11 |
| `CmdDispatch` | generated | 0 | 1 | vkCmdDispatch |  | 0 | 0 | 4 |
| `CmdDispatchBase` | generated | 0 | 1 | vkCmdDispatchBase |  | 0 | 0 | 7 |
| `CmdDispatchIndirect` | generated | 0 | 1 | vkCmdDispatchIndirect |  | 0 | 0 | 3 |
| `CmdDraw` | generated | 0 | 1 | vkCmdDraw |  | 0 | 0 | 5 |
| `CmdDrawIndexed` | generated | 0 | 1 | vkCmdDrawIndexed |  | 0 | 0 | 6 |
| `CmdDrawIndexedIndirect` | generated | 0 | 1 | vkCmdDrawIndexedIndirect |  | 0 | 0 | 5 |
| `CmdDrawIndexedIndirectCount` | generated | 0 | 1 | vkCmdDrawIndexedIndirectCount |  | 0 | 0 | 7 |
| `CmdDrawIndirect` | generated | 0 | 1 | vkCmdDrawIndirect |  | 0 | 0 | 5 |
| `CmdDrawIndirectCount` | generated | 0 | 1 | vkCmdDrawIndirectCount |  | 0 | 0 | 7 |
| `CmdEndDebugUtilsLabel` | generated | 0 | 1 | vkCmdEndDebugUtilsLabelEXT |  | 0 | 0 | 1 |
| `CmdEndQuery` | generated | 0 | 1 | vkCmdEndQuery |  | 0 | 0 | 3 |
| `CmdEndRenderPass` | generated | 0 | 1 | vkCmdEndRenderPass |  | 0 | 0 | 1 |
| `CmdEndRenderPass2` | generated | 0 | 1 | vkCmdEndRenderPass2 |  | 1 | 0 | 2 |
| `CmdExecuteCommands` | generated | 0 | 1 | vkCmdExecuteCommands |  | 0 | 0 | 3 |
| `CmdFillBuffer` | generated | 0 | 1 | vkCmdFillBuffer |  | 0 | 0 | 5 |
| `CmdInsertDebugUtilsLabel` | generated | 0 | 1 | vkCmdInsertDebugUtilsLabelEXT |  | 1 | 0 | 4 |
| `CmdNextSubpass` | generated | 0 | 1 | vkCmdNextSubpass |  | 0 | 0 | 2 |
| `CmdNextSubpass2` | generated | 0 | 1 | vkCmdNextSubpass2 |  | 2 | 0 | 4 |
| `CmdPipelineBarrier` | generated | 0 | 1 | vkCmdPipelineBarrier |  | 0 | 0 | 10 |
| `CmdPushConstants` | generated | 0 | 1 | vkCmdPushConstants |  | 0 | 0 | 6 |
| `CmdResetEvent` | generated | 0 | 1 | vkCmdResetEvent |  | 0 | 0 | 3 |
| `CmdResetQueryPool` | generated | 0 | 1 | vkCmdResetQueryPool |  | 0 | 0 | 4 |
| `CmdResolveImage` | generated | 0 | 1 | vkCmdResolveImage |  | 0 | 0 | 7 |
| `CmdSetBlendConstants` | generated | 0 | 1 | vkCmdSetBlendConstants |  | 0 | 0 | 2 |
| `CmdSetDepthBias` | generated | 0 | 1 | vkCmdSetDepthBias |  | 0 | 0 | 4 |
| `CmdSetDepthBounds` | generated | 0 | 1 | vkCmdSetDepthBounds |  | 0 | 0 | 3 |
| `CmdSetDeviceMask` | generated | 0 | 1 | vkCmdSetDeviceMask |  | 0 | 0 | 2 |
| `CmdSetEvent` | generated | 0 | 1 | vkCmdSetEvent |  | 0 | 0 | 3 |
| `CmdSetLineWidth` | generated | 0 | 1 | vkCmdSetLineWidth |  | 0 | 0 | 2 |
| `CmdSetScissor` | generated | 0 | 1 | vkCmdSetScissor |  | 0 | 0 | 4 |
| `CmdSetStencilCompareMask` | generated | 0 | 1 | vkCmdSetStencilCompareMask |  | 0 | 0 | 3 |
| `CmdSetStencilReference` | generated | 0 | 1 | vkCmdSetStencilReference |  | 0 | 0 | 3 |
| `CmdSetStencilWriteMask` | generated | 0 | 1 | vkCmdSetStencilWriteMask |  | 0 | 0 | 3 |
| `CmdSetViewport` | generated | 0 | 1 | vkCmdSetViewport |  | 0 | 0 | 4 |
| `CmdUpdateBuffer` | generated | 0 | 1 | vkCmdUpdateBuffer |  | 0 | 0 | 5 |
| `CmdWaitEvents` | generated | 0 | 1 | vkCmdWaitEvents |  | 0 | 0 | 11 |
| `CmdWriteTimestamp` | generated | 0 | 1 | vkCmdWriteTimestamp |  | 0 | 0 | 4 |
| `CreateAttachmentsFramebuffer` | generated | 1 | 2 |  | CreateFramebuffer | 1 | 0 | 13 |
| `CreateBindingFlagsDescriptorSetLayout` | generated | 1 | 2 |  | CreateDescriptorSetLayout | 1 | 0 | 9 |
| `CreateBuffer` | generated | 0 | 1 | vkCreateBuffer |  | 1 | 0 | 10 |
| `CreateBufferView` | generated | 0 | 1 | vkCreateBufferView |  | 1 | 0 | 9 |
| `CreateCommandPool` | generated | 0 | 1 | vkCreateCommandPool |  | 1 | 0 | 6 |
| `CreateComputePipelines` | generated | 0 | 1 | vkCreateComputePipelines |  | 0 | 0 | 6 |
| `CreateDebugUtilsMessenger` | generated | 0 | 1 | vkCreateDebugUtilsMessengerEXT |  | 1 | 0 | 9 |
| `CreateDebugUtilsMessengerInstance` | generated | 1 | 2 |  | CreateInstance | 1 | 0 | 14 |
| `CreateDebugUtilsObjectNameSampler` | generated | 1 | 2 |  | CreateSampler | 1 | 0 | 23 |
| `CreateDescriptorPool` | generated | 0 | 1 | vkCreateDescriptorPool |  | 1 | 0 | 8 |
| `CreateDescriptorSetLayout` | generated | 0 | 1 | vkCreateDescriptorSetLayout |  | 1 | 0 | 7 |
| `CreateDescriptorUpdateTemplate` | generated | 0 | 1 | vkCreateDescriptorUpdateTemplate |  | 1 | 0 | 12 |
| `CreateDevice` | generated | 0 | 1 | vkCreateDevice |  | 1 | 0 | 12 |
| `CreateDeviceGroupSwapchain` | generated | 1 | 2 |  | CreateSwapchain | 1 | 0 | 21 |
| `CreateDisplayMode` | generated | 0 | 1 | vkCreateDisplayModeKHR |  | 1 | 0 | 7 |
| `CreateDisplayPlaneSurface` | generated | 0 | 1 | vkCreateDisplayPlaneSurfaceKHR |  | 1 | 0 | 12 |
| `CreateDrmFormatModifierExplicitImage` | generated | 1 | 2 |  | CreateImage | 1 | 0 | 20 |
| `CreateDrmFormatModifierListImage` | generated | 1 | 2 |  | CreateImage | 1 | 0 | 19 |
| `CreateEvent` | generated | 0 | 1 | vkCreateEvent |  | 1 | 0 | 5 |
| `CreateExportFence` | generated | 1 | 2 |  | CreateFence | 1 | 0 | 6 |
| `CreateExportSemaphore` | generated | 1 | 2 |  | CreateSemaphore | 1 | 0 | 6 |
| `CreateExportWin32HandleSemaphore` | generated | 1 | 2 |  | CreateSemaphore | 1 | 0 | 8 |
| `CreateExternalMemoryBuffer` | generated | 1 | 2 |  | CreateBuffer | 1 | 0 | 11 |
| `CreateExternalMemoryImage` | generated | 1 | 2 |  | CreateImage | 1 | 0 | 18 |
| `CreateFence` | generated | 0 | 1 | vkCreateFence |  | 1 | 0 | 5 |
| `CreateFormatListImage` | generated | 1 | 2 |  | CreateImage | 1 | 0 | 19 |
| `CreateFramebuffer` | generated | 0 | 1 | vkCreateFramebuffer |  | 1 | 0 | 11 |
| `CreateGraphicsPipelines` | generated | 0 | 1 | vkCreateGraphicsPipelines |  | 0 | 0 | 6 |
| `CreateGroupDeviceDevice` | generated | 1 | 2 |  | CreateDevice | 1 | 0 | 14 |
| `CreateHeadlessSurface` | generated | 0 | 1 | vkCreateHeadlessSurfaceEXT |  | 1 | 0 | 5 |
| `CreateImage` | generated | 0 | 1 | vkCreateImage |  | 1 | 0 | 17 |
| `CreateImageFormatListSwapchain` | generated | 1 | 2 |  | CreateSwapchain | 1 | 0 | 22 |
| `CreateImageView` | generated | 0 | 1 | vkCreateImageView |  | 1 | 0 | 10 |
| `CreateInputAttachmentAspectRenderPass` | generated | 1 | 2 |  | CreateRenderPass | 1 | 0 | 13 |
| `CreateInstance` | generated | 0 | 1 | vkCreateInstance |  | 1 | 0 | 9 |
| `CreateMultiviewRenderPass` | generated | 1 | 2 |  | CreateRenderPass | 1 | 0 | 17 |
| `CreateOpaqueCaptureAddressBuffer` | generated | 1 | 2 |  | CreateBuffer | 1 | 0 | 11 |
| `CreatePipelineCache` | generated | 0 | 1 | vkCreatePipelineCache |  | 1 | 0 | 7 |
| `CreatePipelineLayout` | generated | 0 | 1 | vkCreatePipelineLayout |  | 1 | 0 | 9 |
| `CreateQueryPool` | generated | 0 | 1 | vkCreateQueryPool |  | 1 | 0 | 8 |
| `CreateReductionModeSampler` | generated | 1 | 2 |  | CreateSampler | 1 | 0 | 21 |
| `CreateRenderPass` | generated | 0 | 1 | vkCreateRenderPass |  | 1 | 0 | 11 |
| `CreateRenderPass2` | generated | 0 | 1 | vkCreateRenderPass2 |  | 1 | 0 | 13 |
| `CreateSampler` | generated | 0 | 1 | vkCreateSampler |  | 1 | 0 | 20 |
| `CreateSamplerYcbcrConversion` | generated | 0 | 1 | vkCreateSamplerYcbcrConversion |  | 1 | 0 | 12 |
| `CreateSamplerYcbcrConversionImageView` | generated | 1 | 2 |  | CreateImageView | 1 | 0 | 11 |
| `CreateSemaphore` | generated | 0 | 1 | vkCreateSemaphore |  | 1 | 0 | 5 |
| `CreateShaderModule` | generated | 0 | 1 | vkCreateShaderModule |  | 1 | 0 | 7 |
| `CreateStencilUsageImage` | generated | 1 | 2 |  | CreateImage | 1 | 0 | 18 |
| `CreateSwapchain` | generated | 0 | 1 | vkCreateSwapchainKHR |  | 1 | 0 | 20 |
| `CreateSwapchainImage` | generated | 1 | 2 |  | CreateImage | 1 | 0 | 18 |
| `CreateTypeSemaphore` | generated | 1 | 2 |  | CreateSemaphore | 1 | 0 | 7 |
| `CreateUsageImageView` | generated | 1 | 2 |  | CreateImageView | 1 | 0 | 11 |
| `CreateWin32Surface` | generated | 0 | 1 | vkCreateWin32SurfaceKHR |  | 1 | 0 | 7 |
| `CreateXlibSurface` | generated | 0 | 1 | vkCreateXlibSurfaceKHR |  | 1 | 0 | 7 |
| `CreateYcbcrConversionSampler` | generated | 1 | 2 |  | CreateSampler | 1 | 0 | 21 |
| `DestroyBuffer` | generated | 0 | 1 | vkDestroyBuffer |  | 0 | 0 | 3 |
| `DestroyBufferView` | generated | 0 | 1 | vkDestroyBufferView |  | 0 | 0 | 3 |
| `DestroyCommandPool` | generated | 0 | 1 | vkDestroyCommandPool |  | 0 | 0 | 3 |
| `DestroyDebugUtilsMessenger` | generated | 0 | 1 | vkDestroyDebugUtilsMessengerEXT |  | 0 | 0 | 3 |
| `DestroyDescriptorPool` | generated | 0 | 1 | vkDestroyDescriptorPool |  | 0 | 0 | 3 |
| `DestroyDescriptorSetLayout` | generated | 0 | 1 | vkDestroyDescriptorSetLayout |  | 0 | 0 | 3 |
| `DestroyDescriptorUpdateTemplate` | generated | 0 | 1 | vkDestroyDescriptorUpdateTemplate |  | 0 | 0 | 3 |
| `DestroyDevice` | generated | 0 | 1 | vkDestroyDevice |  | 0 | 0 | 2 |
| `DestroyEvent` | generated | 0 | 1 | vkDestroyEvent |  | 0 | 0 | 3 |
| `DestroyFence` | generated | 0 | 1 | vkDestroyFence |  | 0 | 0 | 3 |
| `DestroyFramebuffer` | generated | 0 | 1 | vkDestroyFramebuffer |  | 0 | 0 | 3 |
| `DestroyImage` | generated | 0 | 1 | vkDestroyImage |  | 0 | 0 | 3 |
| `DestroyImageView` | generated | 0 | 1 | vkDestroyImageView |  | 0 | 0 | 3 |
| `DestroyInstance` | generated | 0 | 1 | vkDestroyInstance |  | 0 | 0 | 2 |
| `DestroyPipeline` | generated | 0 | 1 | vkDestroyPipeline |  | 0 | 0 | 3 |
| `DestroyPipelineCache` | generated | 0 | 1 | vkDestroyPipelineCache |  | 0 | 0 | 3 |
| `DestroyPipelineLayout` | generated | 0 | 1 | vkDestroyPipelineLayout |  | 0 | 0 | 3 |
| `DestroyQueryPool` | generated | 0 | 1 | vkDestroyQueryPool |  | 0 | 0 | 3 |
| `DestroyRenderPass` | generated | 0 | 1 | vkDestroyRenderPass |  | 0 | 0 | 3 |
| `DestroySampler` | generated | 0 | 1 | vkDestroySampler |  | 0 | 0 | 3 |
| `DestroySamplerYcbcrConversion` | generated | 0 | 1 | vkDestroySamplerYcbcrConversion |  | 0 | 0 | 3 |
| `DestroySemaphore` | generated | 0 | 1 | vkDestroySemaphore |  | 0 | 0 | 3 |
| `DestroyShaderModule` | generated | 0 | 1 | vkDestroyShaderModule |  | 0 | 0 | 3 |
| `DestroySurface` | generated | 0 | 1 | vkDestroySurfaceKHR |  | 0 | 0 | 3 |
| `DestroySwapchain` | generated | 0 | 1 | vkDestroySwapchainKHR |  | 0 | 0 | 3 |
| `DeviceWaitIdle` | generated | 0 | 1 | vkDeviceWaitIdle |  | 0 | 0 | 1 |
| `EndCommandBuffer` | generated | 0 | 1 | vkEndCommandBuffer |  | 0 | 0 | 1 |
| `EnumerateDeviceExtensionProperties` | generated | 0 | 1 | vkEnumerateDeviceExtensionProperties |  | 0 | 0 | 3 |
| `EnumerateDeviceLayerProperties` | generated | 0 | 1 | vkEnumerateDeviceLayerProperties |  | 0 | 0 | 2 |
| `EnumerateInstanceExtensionProperties` | generated | 0 | 1 | vkEnumerateInstanceExtensionProperties |  | 0 | 0 | 2 |
| `EnumerateInstanceLayerProperties` | generated | 0 | 1 | vkEnumerateInstanceLayerProperties |  | 0 | 0 | 1 |
| `EnumerateInstanceVersion` | generated | 0 | 1 | vkEnumerateInstanceVersion |  | 0 | 0 | 1 |
| `EnumeratePhysicalDeviceGroups` | generated | 0 | 1 | vkEnumeratePhysicalDeviceGroups |  | 1 | 0 | 2 |
| `EnumeratePhysicalDevices` | generated | 0 | 1 | vkEnumeratePhysicalDevices |  | 0 | 0 | 2 |
| `Fatal` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `FlushMappedMemoryRanges` | generated | 0 | 1 | vkFlushMappedMemoryRanges |  | 0 | 0 | 3 |
| `FreeCommandBuffers` | generated | 0 | 1 | vkFreeCommandBuffers |  | 0 | 0 | 4 |
| `FreeDescriptorSets` | generated | 0 | 1 | vkFreeDescriptorSets |  | 0 | 0 | 4 |
| `FreeMemory` | generated | 0 | 1 | vkFreeMemory |  | 0 | 0 | 3 |
| `GetBindingFlagsDescriptorSetLayoutSupport` | generated | 1 | 2 |  | GetDescriptorSetLayoutSupport | 1 | 0 | 9 |
| `GetBufferDeviceAddress` | generated | 0 | 1 | vkGetBufferDeviceAddress |  | 1 | 0 | 3 |
| `GetBufferMemoryRequirements` | generated | 0 | 1 | vkGetBufferMemoryRequirements |  | 0 | 0 | 3 |
| `GetBufferMemoryRequirements2` | generated | 0 | 1 | vkGetBufferMemoryRequirements2 |  | 1 | 0 | 5 |
| `GetBufferOpaqueCaptureAddress` | generated | 0 | 1 | vkGetBufferOpaqueCaptureAddress |  | 1 | 0 | 3 |
| `GetDescriptorSetLayoutSupport` | generated | 0 | 1 | vkGetDescriptorSetLayoutSupport |  | 1 | 0 | 7 |
| `GetDeviceGroupPeerMemoryFeatures` | generated | 0 | 1 | vkGetDeviceGroupPeerMemoryFeatures |  | 0 | 0 | 5 |
| `GetDeviceGroupPresentCapabilities` | generated | 0 | 1 | vkGetDeviceGroupPresentCapabilitiesKHR |  | 0 | 0 | 3 |
| `GetDeviceGroupSurfacePresentModes` | generated | 0 | 1 | vkGetDeviceGroupSurfacePresentModesKHR |  | 0 | 0 | 3 |
| `GetDeviceMemoryCommitment` | generated | 0 | 1 | vkGetDeviceMemoryCommitment |  | 0 | 0 | 3 |
| `GetDeviceMemoryOpaqueCaptureAddress` | generated | 0 | 1 | vkGetDeviceMemoryOpaqueCaptureAddress |  | 1 | 0 | 3 |
| `GetDeviceProcAddr` | generated | infra | 1 | vkGetDeviceProcAddr |  | 0 | 0 | 2 |
| `GetDeviceQueue` | generated | 0 | 1 | vkGetDeviceQueue |  | 0 | 0 | 4 |
| `GetDeviceQueue2` | generated | 0 | 1 | vkGetDeviceQueue2 |  | 1 | 0 | 6 |
| `GetDisplayModeProperties` | generated | 0 | 1 | vkGetDisplayModePropertiesKHR |  | 0 | 0 | 3 |
| `GetDisplayPlaneCapabilities` | generated | 0 | 1 | vkGetDisplayPlaneCapabilitiesKHR |  | 0 | 0 | 4 |
| `GetDisplayPlaneSupportedDisplays` | generated | 0 | 1 | vkGetDisplayPlaneSupportedDisplaysKHR |  | 0 | 0 | 3 |
| `GetDrmModifierInfoPhysicalDeviceImageFormatProperties2` | generated | 1 | 2 |  | GetPhysicalDeviceImageFormatProperties2 | 1 | 0 | 13 |
| `GetEventStatus` | generated | 0 | 1 | vkGetEventStatus |  | 0 | 0 | 2 |
| `GetExternalInfoPhysicalDeviceImageFormatProperties2` | generated | 1 | 2 |  | GetPhysicalDeviceImageFormatProperties2 | 1 | 0 | 10 |
| `GetFenceStatus` | generated | 0 | 1 | vkGetFenceStatus |  | 0 | 0 | 2 |
| `GetImageDrmFormatModifierProperties` | generated | 0 | 1 | vkGetImageDrmFormatModifierPropertiesEXT |  | 0 | 0 | 4 |
| `GetImageMemoryRequirements` | generated | 0 | 1 | vkGetImageMemoryRequirements |  | 0 | 0 | 3 |
| `GetImageMemoryRequirements2` | generated | 0 | 1 | vkGetImageMemoryRequirements2 |  | 1 | 0 | 5 |
| `GetImageSparseMemoryRequirements` | generated | 0 | 1 | vkGetImageSparseMemoryRequirements |  | 0 | 0 | 3 |
| `GetImageSparseMemoryRequirements2` | generated | 0 | 1 | vkGetImageSparseMemoryRequirements2 |  | 2 | 0 | 4 |
| `GetImageSubresourceLayout` | generated | 0 | 1 | vkGetImageSubresourceLayout |  | 0 | 0 | 4 |
| `GetInstanceProcAddr` | generated | infra | 1 | vkGetInstanceProcAddr |  | 0 | 0 | 2 |
| `GetListCreateInfoPhysicalDeviceImageFormatProperties2` | generated | 1 | 2 |  | GetPhysicalDeviceImageFormatProperties2 | 1 | 0 | 11 |
| `GetMemoryFd` | generated | 0 | 1 | vkGetMemoryFdKHR |  | 1 | 0 | 5 |
| `GetMemoryFdProperties` | generated | 0 | 1 | vkGetMemoryFdPropertiesKHR |  | 0 | 0 | 5 |
| `GetMemoryWin32Handle` | generated | 0 | 1 | vkGetMemoryWin32HandleKHR |  | 1 | 0 | 5 |
| `GetMemoryWin32HandleProperties` | generated | 0 | 1 | vkGetMemoryWin32HandlePropertiesKHR |  | 0 | 0 | 5 |
| `GetPhysicalDeviceDisplayPlaneProperties` | generated | 0 | 1 | vkGetPhysicalDeviceDisplayPlanePropertiesKHR |  | 0 | 0 | 2 |
| `GetPhysicalDeviceDisplayProperties` | generated | 0 | 1 | vkGetPhysicalDeviceDisplayPropertiesKHR |  | 0 | 0 | 2 |
| `GetPhysicalDeviceExternalBufferProperties` | generated | 0 | 1 | vkGetPhysicalDeviceExternalBufferProperties |  | 1 | 0 | 7 |
| `GetPhysicalDeviceExternalFenceProperties` | generated | 0 | 1 | vkGetPhysicalDeviceExternalFenceProperties |  | 1 | 0 | 5 |
| `GetPhysicalDeviceExternalSemaphoreProperties` | generated | 0 | 1 | vkGetPhysicalDeviceExternalSemaphoreProperties |  | 1 | 0 | 5 |
| `GetPhysicalDeviceFeatures` | generated | 0 | 1 | vkGetPhysicalDeviceFeatures |  | 0 | 0 | 2 |
| `GetPhysicalDeviceFeatures2` | generated | 0 | 1 | vkGetPhysicalDeviceFeatures2 |  | 0 | 0 | 3 |
| `GetPhysicalDeviceFormatProperties` | generated | 0 | 1 | vkGetPhysicalDeviceFormatProperties |  | 0 | 0 | 3 |
| `GetPhysicalDeviceFormatProperties2` | generated | 0 | 1 | vkGetPhysicalDeviceFormatProperties2 |  | 0 | 0 | 4 |
| `GetPhysicalDeviceImageFormatProperties` | generated | 0 | 1 | vkGetPhysicalDeviceImageFormatProperties |  | 0 | 0 | 7 |
| `GetPhysicalDeviceImageFormatProperties2` | generated | 0 | 1 | vkGetPhysicalDeviceImageFormatProperties2 |  | 1 | 0 | 9 |
| `GetPhysicalDeviceMemoryProperties` | generated | 0 | 1 | vkGetPhysicalDeviceMemoryProperties |  | 0 | 0 | 2 |
| `GetPhysicalDeviceMemoryProperties2` | generated | 0 | 1 | vkGetPhysicalDeviceMemoryProperties2 |  | 0 | 0 | 3 |
| `GetPhysicalDevicePresentRectangles` | generated | 0 | 1 | vkGetPhysicalDevicePresentRectanglesKHR |  | 0 | 0 | 3 |
| `GetPhysicalDeviceProperties` | generated | 0 | 1 | vkGetPhysicalDeviceProperties |  | 0 | 0 | 2 |
| `GetPhysicalDeviceProperties2` | generated | 0 | 1 | vkGetPhysicalDeviceProperties2 |  | 0 | 0 | 3 |
| `GetPhysicalDeviceQueueFamilyProperties` | generated | 0 | 1 | vkGetPhysicalDeviceQueueFamilyProperties |  | 0 | 0 | 2 |
| `GetPhysicalDeviceQueueFamilyProperties2` | generated | 0 | 1 | vkGetPhysicalDeviceQueueFamilyProperties2 |  | 1 | 0 | 2 |
| `GetPhysicalDeviceSparseImageFormatProperties` | generated | 0 | 1 | vkGetPhysicalDeviceSparseImageFormatProperties |  | 0 | 0 | 7 |
| `GetPhysicalDeviceSparseImageFormatProperties2` | generated | 0 | 1 | vkGetPhysicalDeviceSparseImageFormatProperties2 |  | 2 | 0 | 8 |
| `GetPhysicalDeviceSurfaceCapabilities` | generated | 0 | 1 | vkGetPhysicalDeviceSurfaceCapabilitiesKHR |  | 0 | 0 | 3 |
| `GetPhysicalDeviceSurfaceFormats` | generated | 0 | 1 | vkGetPhysicalDeviceSurfaceFormatsKHR |  | 0 | 0 | 3 |
| `GetPhysicalDeviceSurfacePresentModes` | generated | 0 | 1 | vkGetPhysicalDeviceSurfacePresentModesKHR |  | 0 | 0 | 3 |
| `GetPhysicalDeviceSurfaceSupport` | generated | 0 | 1 | vkGetPhysicalDeviceSurfaceSupportKHR |  | 0 | 0 | 4 |
| `GetPhysicalDeviceWin32PresentationSupport` | generated | 0 | 1 | vkGetPhysicalDeviceWin32PresentationSupportKHR |  | 0 | 0 | 2 |
| `GetPhysicalDeviceXlibPresentationSupport` | generated | 0 | 1 | vkGetPhysicalDeviceXlibPresentationSupportKHR |  | 0 | 0 | 4 |
| `GetPipelineCacheData` | generated | 0 | 1 | vkGetPipelineCacheData |  | 0 | 0 | 4 |
| `GetPlaneInfoImageMemoryRequirements2` | generated | 1 | 2 |  | GetImageMemoryRequirements2 | 1 | 0 | 6 |
| `GetQueryPoolResults` | generated | 0 | 1 | vkGetQueryPoolResults |  | 0 | 0 | 8 |
| `GetRenderAreaGranularity` | generated | 0 | 1 | vkGetRenderAreaGranularity |  | 0 | 0 | 3 |
| `GetSemaphoreCounterValue` | generated | 0 | 1 | vkGetSemaphoreCounterValue |  | 0 | 0 | 3 |
| `GetSemaphoreFd` | generated | 0 | 1 | vkGetSemaphoreFdKHR |  | 1 | 0 | 5 |
| `GetSemaphoreWin32Handle` | generated | 0 | 1 | vkGetSemaphoreWin32HandleKHR |  | 1 | 0 | 5 |
| `GetStencilUsageCreateInfoPhysicalDeviceImageFormatProperties2` | generated | 1 | 2 |  | GetPhysicalDeviceImageFormatProperties2 | 1 | 0 | 10 |
| `GetSwapchainImages` | generated | 0 | 1 | vkGetSwapchainImagesKHR |  | 0 | 0 | 3 |
| `GetTypeCreatePhysicalDeviceExternalSemaphoreProperties` | generated | 1 | 2 |  | GetPhysicalDeviceExternalSemaphoreProperties | 1 | 0 | 7 |
| `ImportSemaphoreFd` | generated | 0 | 1 | vkImportSemaphoreFdKHR |  | 1 | 0 | 6 |
| `ImportSemaphoreWin32Handle` | generated | 0 | 1 | vkImportSemaphoreWin32HandleKHR |  | 1 | 0 | 7 |
| `InitAcquireNextImageInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitApplicationInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitAttachmentDescription2` | generated | util | 1 |  |  | 0 | 0 | 11 |
| `InitAttachmentDescriptionStencilLayout` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitAttachmentReference2` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitAttachmentReferenceStencilLayout` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitBindBufferMemoryDeviceGroupInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitBindBufferMemoryInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitBindImageMemoryDeviceGroupInfo` | generated | util | 1 |  |  | 0 | 0 | 6 |
| `InitBindImageMemoryInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitBindImageMemorySwapchainInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitBindImagePlaneMemoryInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitBindSparseInfo` | generated | util | 1 |  |  | 0 | 0 | 12 |
| `InitBufferCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 8 |
| `InitBufferDeviceAddressInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitBufferMemoryBarrier` | generated | util | 1 |  |  | 0 | 0 | 9 |
| `InitBufferMemoryRequirementsInfo2` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitBufferOpaqueCaptureAddressCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitBufferViewCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitCommandBufferAllocateInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitCommandBufferBeginInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitCommandBufferInheritanceInfo` | generated | util | 1 |  |  | 0 | 0 | 8 |
| `InitCommandPoolCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitComputePipelineCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitCopyDescriptorSet` | generated | util | 1 |  |  | 0 | 0 | 9 |
| `InitD3D12FenceSubmitInfo` | generated | util | 1 |  |  | 0 | 0 | 6 |
| `InitDebugUtilsLabel` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitDebugUtilsMessengerCallbackData` | generated | util | 1 |  |  | 0 | 0 | 12 |
| `InitDebugUtilsMessengerCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitDebugUtilsObjectNameInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitDebugUtilsObjectTagInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitDescriptorPoolCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 6 |
| `InitDescriptorSetAllocateInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitDescriptorSetLayoutBindingFlagsCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitDescriptorSetLayoutCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitDescriptorSetLayoutSupport` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitDescriptorSetVariableDescriptorCountAllocateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitDescriptorSetVariableDescriptorCountLayoutSupport` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitDescriptorUpdateTemplateCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 10 |
| `InitDeviceCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 10 |
| `InitDeviceGroupBindSparseInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitDeviceGroupCommandBufferBeginInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitDeviceGroupDeviceCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitDeviceGroupPresentCapabilities` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitDeviceGroupPresentInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitDeviceGroupRenderPassBeginInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitDeviceGroupSubmitInfo` | generated | util | 1 |  |  | 0 | 0 | 8 |
| `InitDeviceGroupSwapchainCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitDeviceMemoryOpaqueCaptureAddressInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitDeviceQueueCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 6 |
| `InitDeviceQueueInfo2` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitDisplayModeCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitDisplaySurfaceCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 10 |
| `InitDrmFormatModifierPropertiesList` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitDrmFormatModifierPropertiesList2` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitEventCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitExportFenceCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitExportMemoryAllocateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitExportMemoryWin32HandleInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitExportSemaphoreCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitExportSemaphoreWin32HandleInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitExternalBufferProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitExternalFenceProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitExternalImageFormatProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitExternalMemoryBufferCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitExternalMemoryImageCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitExternalSemaphoreProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitFenceCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitFormatProperties2` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitFramebufferAttachmentImageInfo` | generated | util | 1 |  |  | 0 | 0 | 9 |
| `InitFramebufferAttachmentsCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitFramebufferCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 9 |
| `InitGraphicsPipelineCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 19 |
| `InitHeadlessSurfaceCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitImageCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 15 |
| `InitImageDrmFormatModifierExplicitCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitImageDrmFormatModifierListCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitImageDrmFormatModifierProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitImageFormatListCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitImageFormatProperties2` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitImageMemoryBarrier` | generated | util | 1 |  |  | 0 | 0 | 10 |
| `InitImageMemoryRequirementsInfo2` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitImagePlaneMemoryRequirementsInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitImageSparseMemoryRequirementsInfo2` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitImageStencilUsageCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitImageSwapchainCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitImageViewCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 8 |
| `InitImageViewUsageCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitImportMemoryFdInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitImportMemoryWin32HandleInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitImportSemaphoreFdInfo` | generated | util | 1 |  |  | 0 | 0 | 6 |
| `InitImportSemaphoreWin32HandleInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitInstanceCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 8 |
| `InitMappedMemoryRange` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitMemoryAllocateFlagsInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitMemoryAllocateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitMemoryBarrier` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitMemoryDedicatedAllocateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitMemoryDedicatedRequirements` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitMemoryFdProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitMemoryGetFdInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitMemoryGetWin32HandleInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitMemoryOpaqueCaptureAddressAllocateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitMemoryRequirements2` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitMemoryWin32HandleProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDevice16BitStorageFeatures` | generated | util | 1 |  |  | 0 | 0 | 6 |
| `InitPhysicalDevice8BitStorageFeatures` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitPhysicalDeviceBufferDeviceAddressFeatures` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitPhysicalDeviceDepthStencilResolveProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceDescriptorIndexingFeatures` | generated | util | 1 |  |  | 0 | 0 | 22 |
| `InitPhysicalDeviceDescriptorIndexingProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceDriverProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceExternalBufferInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitPhysicalDeviceExternalFenceInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceExternalImageFormatInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceExternalSemaphoreInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceFeatures2` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceFloatControlsProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceGroupProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceHostQueryResetFeatures` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceIDProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceImageDrmFormatModifierInfo` | generated | util | 1 |  |  | 0 | 0 | 6 |
| `InitPhysicalDeviceImageFormatInfo2` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitPhysicalDeviceImagelessFramebufferFeatures` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceMaintenance3Properties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceMemoryProperties2` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceMultiviewFeatures` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitPhysicalDeviceMultiviewProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDevicePCIBusInfoProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDevicePointClippingProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceProperties2` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceProtectedMemoryFeatures` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceProtectedMemoryProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceSamplerFilterMinmaxProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceSamplerYcbcrConversionFeatures` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceScalarBlockLayoutFeatures` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceSeparateDepthStencilLayoutsFeatures` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceShaderAtomicInt64Features` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitPhysicalDeviceShaderDrawParametersFeatures` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceShaderFloat16Int8Features` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitPhysicalDeviceShaderSubgroupExtendedTypesFeatures` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceSparseImageFormatInfo2` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitPhysicalDeviceSubgroupProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceTimelineSemaphoreFeatures` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceTimelineSemaphoreProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceUniformBufferStandardLayoutFeatures` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPhysicalDeviceVariablePointersFeatures` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitPhysicalDeviceVulkan11Features` | generated | util | 1 |  |  | 0 | 0 | 14 |
| `InitPhysicalDeviceVulkan11Properties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceVulkan12Features` | generated | util | 1 |  |  | 0 | 0 | 49 |
| `InitPhysicalDeviceVulkan12Properties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitPhysicalDeviceVulkanMemoryModelFeatures` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitPipelineCacheCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitPipelineColorBlendStateCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 8 |
| `InitPipelineDepthStencilStateCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 12 |
| `InitPipelineDynamicStateCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitPipelineInputAssemblyStateCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitPipelineLayoutCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitPipelineMultisampleStateCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 9 |
| `InitPipelineRasterizationStateCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 13 |
| `InitPipelineShaderStageCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitPipelineTessellationDomainOriginStateCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitPipelineTessellationStateCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitPipelineVertexInputStateCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitPipelineViewportStateCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitPresentInfo` | generated | util | 1 |  |  | 0 | 0 | 8 |
| `InitProtectedSubmitInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitQueryPoolCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 6 |
| `InitQueueFamilyProperties2` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitRenderPassAttachmentBeginInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitRenderPassBeginInfo` | generated | util | 1 |  |  | 0 | 0 | 7 |
| `InitRenderPassCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 9 |
| `InitRenderPassCreateInfo2` | generated | util | 1 |  |  | 0 | 0 | 11 |
| `InitRenderPassInputAttachmentAspectCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitRenderPassMultiviewCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 8 |
| `InitSamplerCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 18 |
| `InitSamplerReductionModeCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitSamplerYcbcrConversionCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 10 |
| `InitSamplerYcbcrConversionImageFormatProperties` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitSamplerYcbcrConversionInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitSemaphoreCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitSemaphoreGetFdInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitSemaphoreGetWin32HandleInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitSemaphoreSignalInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitSemaphoreTypeCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 4 |
| `InitSemaphoreWaitInfo` | generated | util | 1 |  |  | 0 | 0 | 6 |
| `InitShaderModuleCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitSparseImageFormatProperties2` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitSparseImageMemoryRequirements2` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitSubmitInfo` | generated | util | 1 |  |  | 0 | 0 | 9 |
| `InitSubpassBeginInfo` | generated | util | 1 |  |  | 0 | 0 | 3 |
| `InitSubpassDependency2` | generated | util | 1 |  |  | 0 | 0 | 10 |
| `InitSubpassDescription2` | generated | util | 1 |  |  | 0 | 0 | 13 |
| `InitSubpassDescriptionDepthStencilResolve` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitSubpassEndInfo` | generated | util | 1 |  |  | 0 | 0 | 2 |
| `InitSwapchainCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 18 |
| `InitTimelineSemaphoreSubmitInfo` | generated | util | 1 |  |  | 0 | 0 | 6 |
| `InitWin32SurfaceCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InitWriteDescriptorSet` | generated | util | 1 |  |  | 0 | 0 | 10 |
| `InitXlibSurfaceCreateInfo` | generated | util | 1 |  |  | 0 | 0 | 5 |
| `InvalidateMappedMemoryRanges` | generated | 0 | 1 | vkInvalidateMappedMemoryRanges |  | 0 | 0 | 3 |
| `MapMemory` | generated | 0 | 1 | vkMapMemory |  | 0 | 0 | 6 |
| `MergePipelineCaches` | generated | 0 | 1 | vkMergePipelineCaches |  | 0 | 0 | 4 |
| `QueueBeginDebugUtilsLabel` | generated | 0 | 1 | vkQueueBeginDebugUtilsLabelEXT |  | 1 | 0 | 4 |
| `QueueBindSparse` | generated | 0 | 1 | vkQueueBindSparse |  | 0 | 0 | 4 |
| `QueueDeviceGroupPresent` | generated | 1 | 2 |  | QueuePresent | 1 | 0 | 11 |
| `QueueEndDebugUtilsLabel` | generated | 0 | 1 | vkQueueEndDebugUtilsLabelEXT |  | 0 | 0 | 1 |
| `QueueInsertDebugUtilsLabel` | generated | 0 | 1 | vkQueueInsertDebugUtilsLabelEXT |  | 1 | 0 | 4 |
| `QueuePresent` | generated | 0 | 1 | vkQueuePresentKHR |  | 1 | 0 | 8 |
| `QueueSubmit` | generated | 0 | 1 | vkQueueSubmit |  | 0 | 0 | 4 |
| `QueueWaitIdle` | generated | 0 | 1 | vkQueueWaitIdle |  | 0 | 0 | 1 |
| `ResetCommandBuffer` | generated | 0 | 1 | vkResetCommandBuffer |  | 0 | 0 | 2 |
| `ResetCommandPool` | generated | 0 | 1 | vkResetCommandPool |  | 0 | 0 | 3 |
| `ResetDescriptorPool` | generated | 0 | 1 | vkResetDescriptorPool |  | 0 | 0 | 3 |
| `ResetEvent` | generated | 0 | 1 | vkResetEvent |  | 0 | 0 | 2 |
| `ResetFences` | generated | 0 | 1 | vkResetFences |  | 0 | 0 | 3 |
| `ResetQueryPool` | generated | 0 | 1 | vkResetQueryPool |  | 0 | 0 | 4 |
| `SetDebugUtilsObjectName` | generated | 0 | 1 | vkSetDebugUtilsObjectNameEXT |  | 1 | 0 | 5 |
| `SetDebugUtilsObjectTag` | generated | 0 | 1 | vkSetDebugUtilsObjectTagEXT |  | 1 | 0 | 7 |
| `SetEvent` | generated | 0 | 1 | vkSetEvent |  | 0 | 0 | 2 |
| `SignalSemaphore` | generated | 0 | 1 | vkSignalSemaphore |  | 1 | 0 | 4 |
| `SubmitDebugUtilsMessage` | generated | 0 | 1 | vkSubmitDebugUtilsMessageEXT |  | 1 | 0 | 14 |
| `TrimCommandPool` | generated | 0 | 1 | vkTrimCommandPool |  | 0 | 0 | 3 |
| `UnmapMemory` | generated | 0 | 1 | vkUnmapMemory |  | 0 | 0 | 2 |
| `UpdateDescriptorSetWithTemplate` | generated | 0 | 1 | vkUpdateDescriptorSetWithTemplate |  | 0 | 0 | 4 |
| `UpdateDescriptorSets` | generated | 0 | 1 | vkUpdateDescriptorSets |  | 0 | 0 | 5 |
| `WaitForFences` | generated | 0 | 1 | vkWaitForFences |  | 0 | 0 | 5 |
| `WaitSemaphores` | generated | 0 | 1 | vkWaitSemaphores |  | 1 | 0 | 7 |
