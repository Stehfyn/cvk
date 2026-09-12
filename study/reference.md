# Reference module study: `Gfx_Vulkan_*` in `C:/dev/vulkan-va-zero-copy`

Functions: 275. Vulkan commands reached: 89. Reference extension set: 14.

## Tier histogram (derived mechanically from body shape)

| Tier | Meaning | Count |
|---|---|---|
| 0 | calls one vk* directly, no wrapper callees | 87 |
| 1 | one wrapper callee + builds an sType struct | 37 |
| 1' | one wrapper callee, constants only | 61 |
| 2 | several callees, Setup-named | 29 |
| 3 | several callees, domain verb | 41 |
| 0+mixed | calls vk* AND wrappers (should be split) | 0 |
| util | pure C helper | 16 |
| infra | check/procaddr | 4 |

## Tier 0: name and parameter fidelity against mechanical flattening

Expected = Vulkan params in order, with each single sType input struct replaced by its members (minus sType). A tier-0 function that differs is a *decision* the pattern must either canonicalize or forbid.

| Function | vk command | Name = vk name? | Params match? | Difference |
|---|---|---|---|---|
| `AcquireNextImage` | `vkAcquireNextImageKHR` | yes | yes |  |
| `AllocateCommandBuffers` | `vkAllocateCommandBuffers` | yes | yes |  |
| `AllocateDescriptorSets` | `vkAllocateDescriptorSets` | yes | yes |  |
| `AllocateMemory` | `vkAllocateMemory` | yes | yes |  |
| `BeginCommandBuffer` | `vkBeginCommandBuffer` | yes | yes |  |
| `BindBufferMemory` | `vkBindBufferMemory` | yes | yes |  |
| `BindImageMemory` | `vkBindImageMemory` | yes | yes |  |
| `BindImageMemory2` | `vkBindImageMemory2` | yes | yes |  |
| `CmdBindDescriptorSets` | `vkCmdBindDescriptorSets` | yes | yes |  |
| `CmdBindPipeline` | `vkCmdBindPipeline` | yes | yes |  |
| `CmdBlitImageRegions` | `vkCmdBlitImage` | no: CmdBlitImage | yes |  |
| `CmdCopyBufferToImage` | `vkCmdCopyBufferToImage` | yes | yes |  |
| `CmdCopyImageRegions` | `vkCmdCopyImage` | no: CmdCopyImage | yes |  |
| `CmdCopyImageToBuffer` | `vkCmdCopyImageToBuffer` | yes | yes |  |
| `CmdDispatch` | `vkCmdDispatch` | yes | yes |  |
| `CmdPipelineBarrier` | `vkCmdPipelineBarrier` | yes | yes |  |
| `CmdPushConstants` | `vkCmdPushConstants` | yes | yes |  |
| `CmdResetQueryPool` | `vkCmdResetQueryPool` | yes | yes |  |
| `CmdWriteTimestamp` | `vkCmdWriteTimestamp` | yes | yes |  |
| `CreateBuffer` | `vkCreateBuffer` | yes | yes |  |
| `CreateCommandPool` | `vkCreateCommandPool` | yes | yes |  |
| `CreateComputePipelines` | `vkCreateComputePipelines` | yes | yes |  |
| `CreateDebugReportCallback` | `vkCreateDebugReportCallbackEXT` | yes | yes |  |
| `CreateDebugUtilsMessenger` | `vkCreateDebugUtilsMessengerEXT` | yes | no | missing ['pfnUserCallback']; extra ['pfnCallback'] |
| `CreateDescriptorPool` | `vkCreateDescriptorPool` | yes | yes |  |
| `CreateDescriptorSetLayout` | `vkCreateDescriptorSetLayout` | yes | yes |  |
| `CreateDevice` | `vkCreateDevice` | yes | yes |  |
| `CreateDisplayPlaneSurface` | `vkCreateDisplayPlaneSurfaceKHR` | yes | yes |  |
| `CreateFence` | `vkCreateFence` | yes | yes |  |
| `CreateHeadlessSurface` | `vkCreateHeadlessSurfaceEXT` | yes | yes |  |
| `CreateImage` | `vkCreateImage` | yes | yes |  |
| `CreateImageView` | `vkCreateImageView` | yes | yes |  |
| `CreateInstance` | `vkCreateInstance` | yes | yes |  |
| `CreatePipelineCache` | `vkCreatePipelineCache` | yes | yes |  |
| `CreatePipelineLayout` | `vkCreatePipelineLayout` | yes | yes |  |
| `CreateQueryPool` | `vkCreateQueryPool` | yes | yes |  |
| `CreateSemaphore` | `vkCreateSemaphore` | yes | yes |  |
| `CreateShaderModule` | `vkCreateShaderModule` | yes | yes |  |
| `CreateSurfaceWin32` | `vkCreateWin32SurfaceKHR` | yes | no | missing ['pNext', 'flags', 'hinstance', 'hwnd']; extra ['pCreateInfo'] |
| `CreateSurfaceXlib` | `vkCreateXlibSurfaceKHR` | yes | no | missing ['pNext', 'flags', 'dpy', 'window']; extra ['pCreateInfo'] |
| `CreateSwapchain` | `vkCreateSwapchainKHR` | yes | no | missing ['minImageCount']; extra ['minimumImageCount'] |
| `DeviceWaitIdle` | `vkDeviceWaitIdle` | yes | yes |  |
| `EndCommandBuffer` | `vkEndCommandBuffer` | yes | yes |  |
| `FlushMappedMemoryRanges` | `vkFlushMappedMemoryRanges` | yes | no | missing ['memoryRangeCount', 'pMemoryRanges']; extra ['rangeCount', 'pRanges'] |
| `GetBufferMemoryRequirements` | `vkGetBufferMemoryRequirements` | yes | yes |  |
| `GetDeviceExtensionProperties` | `vkEnumerateDeviceExtensionProperties` | yes | yes |  |
| `GetDeviceQueue` | `vkGetDeviceQueue` | yes | yes |  |
| `GetDisplayModeProperties` | `vkGetDisplayModePropertiesKHR` | yes | no | missing ['pPropertyCount'] |
| `GetDisplayPlaneCapabilities` | `vkGetDisplayPlaneCapabilitiesKHR` | yes | yes |  |
| `GetDisplayPlaneSupportedDisplays` | `vkGetDisplayPlaneSupportedDisplaysKHR` | yes | no | missing ['pDisplayCount'] |
| `GetImageDrmFormatModifierProperties` | `vkGetImageDrmFormatModifierPropertiesEXT` | yes | yes |  |
| `GetImageMemoryRequirements` | `vkGetImageMemoryRequirements` | yes | yes |  |
| `GetImageMemoryRequirements2` | `vkGetImageMemoryRequirements2` | yes | no | missing ['pNext', 'image']; extra ['pInfo', 'pMemoryNext'] |
| `GetImageSubresourceLayout` | `vkGetImageSubresourceLayout` | yes | no | missing ['pSubresource']; extra ['subresource'] |
| `GetInstanceExtensionProperties` | `vkEnumerateInstanceExtensionProperties` | yes | yes |  |
| `GetMemoryFd` | `vkGetMemoryFdKHR` | yes | no | missing ['pNext', 'memory', 'handleType']; extra ['pInfo'] |
| `GetMemoryHandlePropertiesWin32` | `vkGetMemoryWin32HandlePropertiesKHR` | yes | no | missing ['pMemoryWin32HandleProperties']; extra ['pNext', 'pProperties'] |
| `GetMemoryHandleWin32` | `vkGetMemoryWin32HandleKHR` | yes | no | missing ['pNext', 'memory', 'handleType']; extra ['pInfo'] |
| `GetMemoryPropertiesFd` | `vkGetMemoryFdPropertiesKHR` | yes | no | missing ['pMemoryFdProperties']; extra ['pProperties'] |
| `GetPhysicalDeviceBufferProperties` | `vkGetPhysicalDeviceExternalBufferProperties` | no: GetPhysicalDeviceExternalBufferProperties | no | missing ['pNext', 'flags', 'usage', 'handleType']; extra ['pInfo', 'pPropertiesNext'] |
| `GetPhysicalDeviceDisplayPlaneProperties` | `vkGetPhysicalDeviceDisplayPlanePropertiesKHR` | yes | no | missing ['pPropertyCount'] |
| `GetPhysicalDeviceDisplayProperties` | `vkGetPhysicalDeviceDisplayPropertiesKHR` | yes | no | missing ['pPropertyCount'] |
| `GetPhysicalDeviceFormatProperties` | `vkGetPhysicalDeviceFormatProperties` | yes | yes |  |
| `GetPhysicalDeviceFormatProperties2` | `vkGetPhysicalDeviceFormatProperties2` | yes | no | extra ['pNext'] |
| `GetPhysicalDeviceImageFormatProperties2` | `vkGetPhysicalDeviceImageFormatProperties2` | yes | no | missing ['pNext', 'format', 'type', 'tiling', 'usage', 'flags']; extra ['pInfo', 'pPropertiesNext'] |
| `GetPhysicalDeviceMemoryProperties` | `vkGetPhysicalDeviceMemoryProperties` | yes | yes |  |
| `GetPhysicalDeviceProperties2` | `vkGetPhysicalDeviceProperties2` | yes | no | extra ['pNext'] |
| `GetPhysicalDeviceQueueFamilyProperties` | `vkGetPhysicalDeviceQueueFamilyProperties` | yes | no | missing ['pQueueFamilyPropertyCount'] |
| `GetPhysicalDeviceSurfaceCapabilities` | `vkGetPhysicalDeviceSurfaceCapabilitiesKHR` | yes | no | missing ['pSurfaceCapabilities']; extra ['pCapabilities'] |
| `GetPhysicalDeviceSurfaceFormats` | `vkGetPhysicalDeviceSurfaceFormatsKHR` | yes | yes |  |
| `GetPhysicalDeviceSurfacePresentModes` | `vkGetPhysicalDeviceSurfacePresentModesKHR` | yes | yes |  |
| `GetPhysicalDeviceSurfaceSupport` | `vkGetPhysicalDeviceSurfaceSupportKHR` | yes | yes |  |
| `GetPhysicalDevices` | `vkEnumeratePhysicalDevices` | yes | yes |  |
| `GetQueryPoolResults` | `vkGetQueryPoolResults` | yes | yes |  |
| `GetSwapchainImages` | `vkGetSwapchainImagesKHR` | yes | yes |  |
| `ImportSemaphoreFd` | `vkImportSemaphoreFdKHR` | yes | no | missing ['pNext', 'semaphore', 'flags', 'handleType', 'fd']; extra ['pInfo'] |
| `ImportSemaphoreHandleWin32` | `vkImportSemaphoreWin32HandleKHR` | yes | no | missing ['pNext', 'semaphore', 'flags', 'handleType', 'handle', 'name']; extra ['pInfo'] |
| `InvalidateMappedMemoryRanges` | `vkInvalidateMappedMemoryRanges` | yes | no | missing ['memoryRangeCount', 'pMemoryRanges']; extra ['rangeCount', 'pRanges'] |
| `MapMemory` | `vkMapMemory` | yes | yes |  |
| `QueuePresent` | `vkQueuePresentKHR` | yes | yes |  |
| `QueueSubmitInfos` | `vkQueueSubmit` | no: QueueSubmit | yes |  |
| `QueueWaitIdle` | `vkQueueWaitIdle` | yes | yes |  |
| `ResetCommandPool` | `vkResetCommandPool` | yes | yes |  |
| `ResetFences` | `vkResetFences` | yes | yes |  |
| `UnmapMemory` | `vkUnmapMemory` | yes | yes |  |
| `UpdateDescriptorSets` | `vkUpdateDescriptorSets` | yes | yes |  |
| `WaitForFences` | `vkWaitForFences` | yes | yes |  |

Tier-0 functions: 87; exact vk name: 83; exact mechanical params: 63.

## Mixed functions (call vk* directly and also call wrappers)


## Coverage of the reference profile

Profile = VK_VERSION_1_2 + VK_EXT_debug_report, VK_EXT_debug_utils, VK_EXT_external_memory_dma_buf, VK_EXT_headless_surface, VK_EXT_image_drm_format_modifier, VK_EXT_pci_bus_info, VK_KHR_display, VK_KHR_external_memory_fd, VK_KHR_external_memory_win32, VK_KHR_external_semaphore_win32, VK_KHR_surface, VK_KHR_swapchain, VK_KHR_win32_surface, VK_KHR_xlib_surface

Wrapped: 88 / 225. Missing: 137. Used outside profile: 1.

- **Acquire** (1): vkAcquireNextImage2KHR
- **Bind** (1): vkBindBufferMemory2
- **Cmd** (43): vkCmdBeginDebugUtilsLabelEXT, vkCmdBeginQuery, vkCmdBeginRenderPass, vkCmdBeginRenderPass2, vkCmdBindIndexBuffer, vkCmdBindVertexBuffers, vkCmdClearAttachments, vkCmdClearColorImage, vkCmdClearDepthStencilImage, vkCmdCopyBuffer, vkCmdCopyQueryPoolResults, vkCmdDispatchBase, vkCmdDispatchIndirect, vkCmdDraw, vkCmdDrawIndexed, vkCmdDrawIndexedIndirect, vkCmdDrawIndexedIndirectCount, vkCmdDrawIndirect, vkCmdDrawIndirectCount, vkCmdEndDebugUtilsLabelEXT, vkCmdEndQuery, vkCmdEndRenderPass, vkCmdEndRenderPass2, vkCmdExecuteCommands, vkCmdFillBuffer, vkCmdInsertDebugUtilsLabelEXT, vkCmdNextSubpass, vkCmdNextSubpass2, vkCmdResetEvent, vkCmdResolveImage, vkCmdSetBlendConstants, vkCmdSetDepthBias, vkCmdSetDepthBounds, vkCmdSetDeviceMask, vkCmdSetEvent, vkCmdSetLineWidth, vkCmdSetScissor, vkCmdSetStencilCompareMask, vkCmdSetStencilReference, vkCmdSetStencilWriteMask, vkCmdSetViewport, vkCmdUpdateBuffer, vkCmdWaitEvents
- **Create** (10): vkCreateBufferView, vkCreateDescriptorUpdateTemplate, vkCreateDisplayModeKHR, vkCreateEvent, vkCreateFramebuffer, vkCreateGraphicsPipelines, vkCreateRenderPass, vkCreateRenderPass2, vkCreateSampler, vkCreateSamplerYcbcrConversion
- **Debug** (1): vkDebugReportMessageEXT
- **Destroy** (26): vkDestroyBuffer, vkDestroyBufferView, vkDestroyCommandPool, vkDestroyDebugReportCallbackEXT, vkDestroyDebugUtilsMessengerEXT, vkDestroyDescriptorPool, vkDestroyDescriptorSetLayout, vkDestroyDescriptorUpdateTemplate, vkDestroyDevice, vkDestroyEvent, vkDestroyFence, vkDestroyFramebuffer, vkDestroyImage, vkDestroyImageView, vkDestroyInstance, vkDestroyPipeline, vkDestroyPipelineCache, vkDestroyPipelineLayout, vkDestroyQueryPool, vkDestroyRenderPass, vkDestroySampler, vkDestroySamplerYcbcrConversion, vkDestroySemaphore, vkDestroyShaderModule, vkDestroySurfaceKHR, vkDestroySwapchainKHR
- **Enumerate** (4): vkEnumerateDeviceLayerProperties, vkEnumerateInstanceLayerProperties, vkEnumerateInstanceVersion, vkEnumeratePhysicalDeviceGroups
- **Free** (3): vkFreeCommandBuffers, vkFreeDescriptorSets, vkFreeMemory
- **Get** (31): vkGetBufferDeviceAddress, vkGetBufferMemoryRequirements2, vkGetBufferOpaqueCaptureAddress, vkGetDescriptorSetLayoutSupport, vkGetDeviceGroupPeerMemoryFeatures, vkGetDeviceGroupPresentCapabilitiesKHR, vkGetDeviceGroupSurfacePresentModesKHR, vkGetDeviceMemoryCommitment, vkGetDeviceMemoryOpaqueCaptureAddress, vkGetDeviceQueue2, vkGetEventStatus, vkGetFenceStatus, vkGetImageSparseMemoryRequirements, vkGetImageSparseMemoryRequirements2, vkGetPhysicalDeviceExternalFenceProperties, vkGetPhysicalDeviceExternalSemaphoreProperties, vkGetPhysicalDeviceFeatures, vkGetPhysicalDeviceFeatures2, vkGetPhysicalDeviceImageFormatProperties, vkGetPhysicalDeviceMemoryProperties2, vkGetPhysicalDevicePresentRectanglesKHR, vkGetPhysicalDeviceProperties, vkGetPhysicalDeviceQueueFamilyProperties2, vkGetPhysicalDeviceSparseImageFormatProperties, vkGetPhysicalDeviceSparseImageFormatProperties2, vkGetPhysicalDeviceWin32PresentationSupportKHR, vkGetPhysicalDeviceXlibPresentationSupportKHR, vkGetPipelineCacheData, vkGetRenderAreaGranularity, vkGetSemaphoreCounterValue, vkGetSemaphoreWin32HandleKHR
- **Merge** (1): vkMergePipelineCaches
- **Queue** (4): vkQueueBeginDebugUtilsLabelEXT, vkQueueBindSparse, vkQueueEndDebugUtilsLabelEXT, vkQueueInsertDebugUtilsLabelEXT
- **Reset** (4): vkResetCommandBuffer, vkResetDescriptorPool, vkResetEvent, vkResetQueryPool
- **Set** (3): vkSetDebugUtilsObjectNameEXT, vkSetDebugUtilsObjectTagEXT, vkSetEvent
- **Signal** (1): vkSignalSemaphore
- **Submit** (1): vkSubmitDebugUtilsMessageEXT
- **Trim** (1): vkTrimCommandPool
- **Update** (1): vkUpdateDescriptorSetWithTemplate
- **Wait** (1): vkWaitSemaphores

Used but outside the computed profile (extension not named in sources, or platform-only):
- `vkImportSemaphoreFdKHR` ['VK_KHR_external_semaphore_fd']

## Documentation completeness (header prototypes)

Prototypes with a doc block: 275; param-count mismatches or missing brief: 1; no doc block found: 0.
- `QueueSubmitD3D12Fence`: 0 \param vs 12 params

## Name-shape conventions observed

| Leading verb | Count |
|---|---|
| Get | 84 |
| Cmd | 57 |
| Create | 33 |
| Setup | 33 |
| Allocate | 18 |
| Queue | 8 |
| Bind | 5 |
| Write | 4 |
| Update | 4 |
| Import | 4 |
| Platform | 4 |
| Reset | 3 |
| Invalidate | 2 |
| Flush | 2 |
| Read | 2 |
| Wait | 2 |
| Check | 1 |
| Init | 1 |
| Select | 1 |
| Device | 1 |
| Calculate | 1 |
| Map | 1 |
| Unmap | 1 |
| Begin | 1 |
| End | 1 |
| Acquire | 1 |

## Per-function table

| Function | Group | Tier | Depth | vk | Wrapper callees | sType | Asserts | Params |
|---|---|---|---|---|---|---|---|---|
| `AcquireNextImage` | core | 0 | 1 | vkAcquireNextImageKHR |  | 0 | 4 | 6 |
| `AllocateBufferMemory` | core | 3 | 4 |  | AllocateMemory, GetBufferMemoryAllocationInfo | 0 | 4 | 7 |
| `AllocateCommandBuffers` | core | 0 | 1 | vkAllocateCommandBuffers |  | 1 | 5 | 6 |
| `AllocateDedicatedBufferMemory` | core | 3 | 4 |  | AllocateDedicatedMemory, GetBufferMemoryAllocationInfo | 0 | 4 | 7 |
| `AllocateDedicatedExternalBufferMemory` | core | 3 | 4 |  | AllocateDedicatedMemory, GetBufferMemoryAllocationInfo | 0 | 6 | 8 |
| `AllocateDedicatedImageMemory` | core | 1' | 3 |  | AllocateDedicatedMemory | 0 | 1 | 7 |
| `AllocateDedicatedMemory` | core | 1 | 2 |  | AllocateMemory | 1 | 1 | 8 |
| `AllocateDescriptorSets` | core | 0 | 1 | vkAllocateDescriptorSets |  | 1 | 6 | 6 |
| `AllocateExportableBufferMemory` | core | 1 | 5 |  | AllocateBufferMemory | 1 | 5 | 8 |
| `AllocateExportableDedicatedBufferMemory` | core | 1 | 5 |  | AllocateDedicatedBufferMemory | 1 | 5 | 8 |
| `AllocateExternalBufferMemory` | core | 3 | 4 |  | AllocateMemory, GetBufferMemoryAllocationInfo | 0 | 6 | 8 |
| `AllocateImageMemory` | core | 3 | 4 |  | AllocateMemory, GetImageMemoryAllocationInfo | 0 | 4 | 7 |
| `AllocateMemory` | core | 0 | 1 | vkAllocateMemory |  | 1 | 5 | 6 |
| `BeginCommandBuffer` | core | 0 | 1 | vkBeginCommandBuffer |  | 1 | 1 | 4 |
| `BindBufferMemory` | core | 0 | 1 | vkBindBufferMemory |  | 0 | 3 | 4 |
| `BindImageMemories` | core | 3 | 2 |  | BindImageMemory2 | 1 | 5 | 6 |
| `BindImageMemory` | core | 0 | 1 | vkBindImageMemory |  | 0 | 3 | 4 |
| `BindImageMemory2` | core | 0 | 1 | vkBindImageMemory2 |  | 0 | 3 | 3 |
| `BindImagePlaneMemories` | core | 3 | 3 |  | BindImageMemories | 1 | 4 | 7 |
| `CalculateFormatPlaneLayouts` | core | 3 | 2 |  | GetFormatPlaneAspect | 0 | 23 | 10 |
| `CheckError` | core | infra | 1 |  |  | 0 | 1 | 1 |
| `CmdAcquireBufferFromExternal` | core | 1' | 3 |  | CmdBufferQueueOwnershipBarrier | 0 | 0 | 9 |
| `CmdAcquireComputeBuffer` | core | 1' | 4 |  | CmdBufferBarrier | 0 | 0 | 8 |
| `CmdAcquireComputeBufferFromExternal` | core | 1' | 4 |  | CmdAcquireBufferFromExternal | 0 | 0 | 7 |
| `CmdAcquireComputeBufferFromHostRead` | core | 1' | 5 |  | CmdAcquireComputeBuffer | 0 | 0 | 6 |
| `CmdAcquireComputeBufferFromTransferRead` | core | 1' | 5 |  | CmdAcquireComputeBuffer | 0 | 0 | 6 |
| `CmdAcquireComputeBufferQueueOwnership` | core | 1' | 3 |  | CmdBufferQueueOwnershipBarrier | 0 | 0 | 10 |
| `CmdAcquireImageFromExternal` | core | 1' | 3 |  | CmdImageQueueOwnershipBarrier | 0 | 0 | 10 |
| `CmdBindComputeDescriptorSets` | core | 1' | 2 |  | CmdBindDescriptorSets | 0 | 5 | 7 |
| `CmdBindComputePipeline` | core | 1' | 2 |  | CmdBindPipeline | 0 | 0 | 2 |
| `CmdBindDescriptorSets` | core | 0 | 1 | vkCmdBindDescriptorSets |  | 0 | 5 | 8 |
| `CmdBindPipeline` | core | 0 | 1 | vkCmdBindPipeline |  | 0 | 2 | 3 |
| `CmdBlitImage` | core | 1' | 2 |  | CmdBlitImageRegions | 0 | 6 | 12 |
| `CmdBlitImage2D` | core | 1' | 3 |  | CmdBlitImage | 0 | 0 | 12 |
| `CmdBlitImageRegions` | core | 0 | 1 | vkCmdBlitImage |  | 0 | 5 | 8 |
| `CmdBufferBarrier` | core | 1' | 3 |  | CmdBufferQueueOwnershipBarrier | 0 | 0 | 10 |
| `CmdBufferQueueOwnershipBarrier` | core | 1 | 2 |  | CmdPipelineBarrier | 1 | 5 | 12 |
| `CmdCopyBufferToImage` | core | 0 | 1 | vkCmdCopyBufferToImage |  | 0 | 5 | 6 |
| `CmdCopyImage` | core | 1' | 2 |  | CmdCopyImageRegions | 0 | 10 | 10 |
| `CmdCopyImageRegions` | core | 0 | 1 | vkCmdCopyImage |  | 0 | 5 | 7 |
| `CmdCopyImageToBuffer` | core | 0 | 1 | vkCmdCopyImageToBuffer |  | 0 | 5 | 6 |
| `CmdDispatch` | core | 0 | 1 | vkCmdDispatch |  | 0 | 4 | 4 |
| `CmdDispatchComputeItems` | core | 3 | 3 |  | CmdBindComputeDescriptorSets, CmdBindComputePipeline, CmdDispatchItems, CmdPushComputeConstants | 0 | 6 | 14 |
| `CmdDispatchComputeItems2D` | core | 1' | 4 |  | CmdDispatchComputeItems | 0 | 0 | 14 |
| `CmdDispatchItems` | core | 1' | 2 |  | CmdDispatch | 0 | 16 | 4 |
| `CmdDispatchItems2D` | core | 1' | 3 |  | CmdDispatchItems | 0 | 0 | 4 |
| `CmdImageBarrier` | core | 1' | 3 |  | CmdImageQueueOwnershipBarrier | 0 | 0 | 11 |
| `CmdImageQueueOwnershipBarrier` | core | 1 | 2 |  | CmdPipelineBarrier | 1 | 7 | 13 |
| `CmdPipelineBarrier` | core | 0 | 1 | vkCmdPipelineBarrier |  | 0 | 6 | 10 |
| `CmdPushComputeConstants` | core | 1' | 2 |  | CmdPushConstants | 0 | 6 | 5 |
| `CmdPushConstants` | core | 0 | 1 | vkCmdPushConstants |  | 0 | 7 | 6 |
| `CmdReleaseBufferToExternal` | core | 1' | 3 |  | CmdBufferQueueOwnershipBarrier | 0 | 0 | 9 |
| `CmdReleaseComputeBuffer` | core | 1' | 4 |  | CmdBufferBarrier | 0 | 0 | 8 |
| `CmdReleaseComputeBufferQueueOwnership` | core | 1' | 3 |  | CmdBufferQueueOwnershipBarrier | 0 | 0 | 10 |
| `CmdReleaseComputeBufferToExternal` | core | 1' | 4 |  | CmdReleaseBufferToExternal | 0 | 0 | 7 |
| `CmdReleaseComputeBufferToHostRead` | core | 1' | 5 |  | CmdReleaseComputeBuffer | 0 | 0 | 6 |
| `CmdReleaseImageToExternal` | core | 1' | 3 |  | CmdImageQueueOwnershipBarrier | 0 | 0 | 10 |
| `CmdResetQueryPool` | core | 0 | 1 | vkCmdResetQueryPool |  | 0 | 3 | 4 |
| `CmdTransferBlitImage` | core | 3 | 4 |  | CmdBlitImage, CmdImageBarrier | 0 | 0 | 29 |
| `CmdTransferBlitImage2D` | core | 1' | 5 |  | CmdTransferBlitImage | 0 | 0 | 29 |
| `CmdTransferBufferToExternalImage` | core | 1' | 5 |  | CmdTransferBufferToImageQueueOwnership | 0 | 0 | 19 |
| `CmdTransferBufferToImage` | core | 1' | 5 |  | CmdTransferBufferToImageQueueOwnership | 0 | 0 | 22 |
| `CmdTransferBufferToImage2D` | core | 1' | 6 |  | CmdTransferBufferToImage | 0 | 0 | 22 |
| `CmdTransferBufferToImageQueueOwnership` | core | 3 | 4 |  | CmdBufferQueueOwnershipBarrier, CmdCopyBufferToImage, CmdImageQueueOwnershipBarrier, GetBufferImageCopyRegions | 0 | 6 | 28 |
| `CmdTransferBufferToImageQueueOwnership2D` | core | 1' | 5 |  | CmdTransferBufferToImageQueueOwnership | 0 | 0 | 28 |
| `CmdTransferComputeBufferToExternalImage` | core | 1' | 6 |  | CmdTransferBufferToExternalImage | 0 | 0 | 17 |
| `CmdTransferComputeBufferToExternalImage2D` | core | 1' | 7 |  | CmdTransferComputeBufferToExternalImage | 0 | 0 | 17 |
| `CmdTransferComputeBufferToImageQueueOwnership` | core | 1' | 5 |  | CmdTransferBufferToImageQueueOwnership | 0 | 0 | 26 |
| `CmdTransferComputeBufferToImageQueueOwnership2D` | core | 1' | 6 |  | CmdTransferBufferToImageQueueOwnership2D | 0 | 0 | 26 |
| `CmdTransferImageToBuffer` | core | 1' | 5 |  | CmdTransferImageToBufferQueueOwnership | 0 | 0 | 25 |
| `CmdTransferImageToBuffer2D` | core | 1' | 6 |  | CmdTransferImageToBuffer | 0 | 0 | 25 |
| `CmdTransferImageToBufferQueueOwnership` | core | 3 | 4 |  | CmdBufferQueueOwnershipBarrier, CmdCopyImageToBuffer, CmdImageQueueOwnershipBarrier, GetBufferImageCopyRegions | 0 | 7 | 33 |
| `CmdTransferImageToBufferQueueOwnership2D` | core | 1' | 5 |  | CmdTransferImageToBufferQueueOwnership | 0 | 0 | 33 |
| `CmdTransferImageToImage` | core | 1' | 4 |  | CmdTransferImageToImageQueueOwnership | 0 | 0 | 27 |
| `CmdTransferImageToImage2D` | core | 1' | 5 |  | CmdTransferImageToImage | 0 | 0 | 27 |
| `CmdTransferImageToImageQueueOwnership` | core | 3 | 3 |  | CmdCopyImage, CmdImageQueueOwnershipBarrier | 0 | 11 | 35 |
| `CmdTransferImageToImageQueueOwnership2D` | core | 1' | 4 |  | CmdTransferImageToImageQueueOwnership | 0 | 0 | 35 |
| `CmdWriteTimestamp` | core | 0 | 1 | vkCmdWriteTimestamp |  | 0 | 3 | 4 |
| `CreateBuffer` | core | 0 | 1 | vkCreateBuffer |  | 1 | 6 | 10 |
| `CreateCommandPool` | core | 0 | 1 | vkCreateCommandPool |  | 1 | 4 | 6 |
| `CreateComputePipeline` | core | 1 | 2 |  | CreateComputePipelines | 1 | 4 | 10 |
| `CreateComputePipelineLayout` | core | 3 | 2 |  | CreatePipelineLayout | 0 | 5 | 9 |
| `CreateComputePipelines` | core | 0 | 1 | vkCreateComputePipelines |  | 0 | 5 | 6 |
| `CreateDebugInstance` | core | 3 | 2 |  | CreateDebugReportCallback, CreateDebugUtilsMessenger, CreateInstance, GetInstanceExtensionProperties, GetInstanceExtensionSupport | 2 | 6 | 13 |
| `CreateDebugReportCallback` | core | 0 | 1 | vkCreateDebugReportCallbackEXT |  | 1 | 5 | 7 |
| `CreateDebugUtilsMessenger` | core | 0 | 1 | vkCreateDebugUtilsMessengerEXT |  | 1 | 6 | 9 |
| `CreateDescriptorPool` | core | 0 | 1 | vkCreateDescriptorPool |  | 1 | 5 | 8 |
| `CreateDescriptorSet` | core | 3 | 2 |  | AllocateDescriptorSets, CreateDescriptorPool, CreateDescriptorSetLayout | 0 | 13 | 15 |
| `CreateDescriptorSetLayout` | core | 0 | 1 | vkCreateDescriptorSetLayout |  | 1 | 4 | 7 |
| `CreateDevice` | core | 0 | 1 | vkCreateDevice |  | 1 | 7 | 12 |
| `CreateDirectDisplaySurface` | core | 3 | 3 |  | CreateDisplayPlaneSurface, GetCompatibleDisplayPlaneIndex, GetDisplayModeProperties, GetDisplayPlaneAlphaMode, GetDisplayPlaneCapabilities, GetPhysicalDeviceDisplayPlaneProperties, GetPhysicalDeviceDisplayProperties, GetSurfaceTransform | 0 | 9 | 7 |
| `CreateDisplayPlaneSurface` | core | 0 | 1 | vkCreateDisplayPlaneSurfaceKHR |  | 1 | 9 | 12 |
| `CreateExternalImage` | core | 1 | 2 |  | CreateImage | 1 | 1 | 18 |
| `CreateExternalMemoryBuffer` | core | 1 | 2 |  | CreateBuffer | 1 | 6 | 11 |
| `CreateFence` | core | 0 | 1 | vkCreateFence |  | 1 | 3 | 5 |
| `CreateHeadlessSurface` | core | 0 | 1 | vkCreateHeadlessSurfaceEXT |  | 1 | 3 | 5 |
| `CreateImage` | core | 0 | 1 | vkCreateImage |  | 1 | 12 | 17 |
| `CreateImageView` | core | 0 | 1 | vkCreateImageView |  | 1 | 8 | 10 |
| `CreateInstance` | core | 0 | 1 | vkCreateInstance |  | 1 | 4 | 9 |
| `CreatePipelineCache` | core | 0 | 1 | vkCreatePipelineCache |  | 1 | 4 | 7 |
| `CreatePipelineLayout` | core | 0 | 1 | vkCreatePipelineLayout |  | 1 | 5 | 9 |
| `CreateQueryPool` | core | 0 | 1 | vkCreateQueryPool |  | 1 | 4 | 8 |
| `CreateQueryPool64` | core | 1' | 2 |  | CreateQueryPool | 0 | 0 | 6 |
| `CreateSemaphore` | core | 0 | 1 | vkCreateSemaphore |  | 1 | 3 | 5 |
| `CreateShaderModule` | core | 0 | 1 | vkCreateShaderModule |  | 1 | 6 | 7 |
| `CreateSwapchain` | core | 0 | 1 | vkCreateSwapchainKHR |  | 1 | 13 | 20 |
| `DeviceWaitIdle` | core | 0 | 1 | vkDeviceWaitIdle |  | 0 | 1 | 1 |
| `EndCommandBuffer` | core | 0 | 1 | vkEndCommandBuffer |  | 0 | 1 | 1 |
| `FlushMappedMemoryRange` | core | 1 | 2 |  | FlushMappedMemoryRanges | 1 | 3 | 5 |
| `FlushMappedMemoryRanges` | core | 0 | 1 | vkFlushMappedMemoryRanges |  | 0 | 3 | 3 |
| `GetBufferImageCopyRegions` | core | 3 | 3 |  | CalculateFormatPlaneLayouts, GetFormatPlaneAspect | 0 | 17 | 9 |
| `GetBufferMemoryAllocationInfo` | core | 3 | 3 |  | GetBufferMemoryRequirements, GetPhysicalDeviceCompatibleMemoryTypeIndex | 0 | 3 | 7 |
| `GetBufferMemoryRequirements` | core | 0 | 1 | vkGetBufferMemoryRequirements |  | 0 | 3 | 3 |
| `GetCompatibleDisplayPlaneIndex` | core | 3 | 2 |  | GetDisplayPlaneSupportedDisplays | 0 | 5 | 4 |
| `GetCompatibleMemoryTypeIndex` | core | util | 1 |  |  | 0 | 3 | 4 |
| `GetDeviceExtensionProperties` | core | 0 | 1 | vkEnumerateDeviceExtensionProperties |  | 0 | 4 | 4 |
| `GetDeviceExtensionSupport` | core | util | 1 |  |  | 0 | 1 | 3 |
| `GetDeviceExtensionsSupport` | core | 3 | 2 |  | GetDeviceExtensionSupport | 0 | 4 | 4 |
| `GetDeviceProcAddr` | core | infra | 1 | vkGetDeviceProcAddr |  | 0 | 3 | 2 |
| `GetDeviceQueue` | core | 0 | 1 | vkGetDeviceQueue |  | 0 | 4 | 4 |
| `GetDisplayModeProperties` | core | 0 | 1 | vkGetDisplayModePropertiesKHR |  | 0 | 5 | 3 |
| `GetDisplayPlaneAlphaMode` | core | util | 1 |  |  | 0 | 0 | 1 |
| `GetDisplayPlaneCapabilities` | core | 0 | 1 | vkGetDisplayPlaneCapabilitiesKHR |  | 0 | 4 | 4 |
| `GetDisplayPlaneSupportedDisplays` | core | 0 | 1 | vkGetDisplayPlaneSupportedDisplaysKHR |  | 0 | 5 | 3 |
| `GetDrmFormatModifierProperties` | core | util | 1 |  |  | 0 | 2 | 4 |
| `GetFormatAspectMask` | core | 3 | 2 |  | GetFormatPlaneAspect | 0 | 3 | 1 |
| `GetFormatBaseSubresourceLayers` | core | 1' | 4 |  | GetFormatSubresourceLayers | 0 | 0 | 1 |
| `GetFormatBaseSubresourceRange` | core | 1' | 4 |  | GetFormatSubresourceRange | 0 | 0 | 1 |
| `GetFormatPlaneAspect` | core | util | 1 |  |  | 0 | 3 | 2 |
| `GetFormatPlaneCount` | core | util | 1 |  |  | 0 | 1 | 1 |
| `GetFormatPlaneLayout` | core | 3 | 3 |  | CalculateFormatPlaneLayouts | 0 | 17 | 9 |
| `GetFormatSize` | core | 1' | 3 |  | CalculateFormatPlaneLayouts | 0 | 3 | 3 |
| `GetFormatSubresourceLayers` | core | 1' | 3 |  | GetFormatAspectMask | 0 | 1 | 4 |
| `GetFormatSubresourceRange` | core | 1' | 3 |  | GetFormatAspectMask | 0 | 2 | 5 |
| `GetImageMemoryAllocationInfo` | core | 3 | 3 |  | GetImageMemoryRequirementsInfo2, GetPhysicalDeviceCompatibleMemoryTypeIndex | 0 | 3 | 7 |
| `GetImageMemoryRequirements` | core | 0 | 1 | vkGetImageMemoryRequirements |  | 0 | 3 | 3 |
| `GetImageMemoryRequirements2` | core | 0 | 1 | vkGetImageMemoryRequirements2 |  | 1 | 3 | 4 |
| `GetImageMemoryRequirementsInfo2` | core | 1 | 2 |  | GetImageMemoryRequirements2 | 1 | 3 | 5 |
| `GetImagePlaneMemoryRequirements` | core | 1' | 4 |  | GetImagePlaneMemoryRequirements2 | 0 | 1 | 4 |
| `GetImagePlaneMemoryRequirements2` | core | 1 | 3 |  | GetImageMemoryRequirementsInfo2 | 1 | 2 | 6 |
| `GetImageSubresourceLayout` | core | 0 | 1 | vkGetImageSubresourceLayout |  | 0 | 3 | 4 |
| `GetInstanceExtensionProperties` | core | 0 | 1 | vkEnumerateInstanceExtensionProperties |  | 0 | 3 | 3 |
| `GetInstanceExtensionSupport` | core | util | 1 |  |  | 0 | 1 | 3 |
| `GetInstanceProcAddr` | core | infra | 1 | vkGetInstanceProcAddr |  | 0 | 2 | 2 |
| `GetMemoryPlaneAspect` | core | util | 1 |  |  | 0 | 1 | 1 |
| `GetMemoryTypeIndex` | core | util | 1 |  |  | 0 | 3 | 4 |
| `GetPhysicalDevice` | core | 3 | 4 |  | GetDeviceExtensionProperties, GetDeviceExtensionSupport, GetDeviceExtensionsSupport, GetPhysicalDeviceLuid, GetPhysicalDevicePciBusInfoProperties, GetPhysicalDeviceProperties2, GetPhysicalDevices | 0 | 4 | 5 |
| `GetPhysicalDeviceBufferProperties` | core | 0 | 1 | vkGetPhysicalDeviceExternalBufferProperties |  | 1 | 3 | 4 |
| `GetPhysicalDeviceCompatibleMemoryTypeIndex` | core | 3 | 2 |  | GetCompatibleMemoryTypeIndex, GetPhysicalDeviceMemoryProperties | 0 | 0 | 3 |
| `GetPhysicalDeviceDisplayPlaneProperties` | core | 0 | 1 | vkGetPhysicalDeviceDisplayPlanePropertiesKHR |  | 0 | 3 | 2 |
| `GetPhysicalDeviceDisplayProperties` | core | 0 | 1 | vkGetPhysicalDeviceDisplayPropertiesKHR |  | 0 | 4 | 2 |
| `GetPhysicalDeviceDrmFormatModifierProperties` | core | 1' | 3 |  | GetPhysicalDeviceDrmFormatModifierPropertiesList | 0 | 5 | 5 |
| `GetPhysicalDeviceDrmFormatModifierPropertiesList` | core | 1 | 2 |  | GetPhysicalDeviceFormatProperties2 | 1 | 3 | 6 |
| `GetPhysicalDeviceExternalBufferExportSupport` | core | 1' | 4 |  | GetPhysicalDeviceExternalBufferMemoryProperties | 0 | 1 | 6 |
| `GetPhysicalDeviceExternalBufferImportSupport` | core | 1' | 4 |  | GetPhysicalDeviceExternalBufferMemoryProperties | 0 | 1 | 6 |
| `GetPhysicalDeviceExternalBufferMemoryFeatures` | core | 1' | 4 |  | GetPhysicalDeviceExternalBufferMemoryProperties | 0 | 1 | 6 |
| `GetPhysicalDeviceExternalBufferMemoryProperties` | core | 1' | 3 |  | GetPhysicalDeviceExternalBufferProperties | 0 | 1 | 6 |
| `GetPhysicalDeviceExternalBufferProperties` | core | 1 | 2 |  | GetPhysicalDeviceBufferProperties | 1 | 2 | 7 |
| `GetPhysicalDeviceExternalImageExportSupport` | core | 1' | 5 |  | GetPhysicalDeviceExternalImageMemoryProperties | 0 | 1 | 9 |
| `GetPhysicalDeviceExternalImageImportSupport` | core | 1' | 5 |  | GetPhysicalDeviceExternalImageMemoryProperties | 0 | 1 | 9 |
| `GetPhysicalDeviceExternalImageMemoryProperties` | core | 1 | 4 |  | GetPhysicalDeviceImageMemoryProperties | 1 | 4 | 9 |
| `GetPhysicalDeviceFormatProperties` | core | 0 | 1 | vkGetPhysicalDeviceFormatProperties |  | 0 | 3 | 3 |
| `GetPhysicalDeviceFormatProperties2` | core | 0 | 1 | vkGetPhysicalDeviceFormatProperties2 |  | 1 | 3 | 4 |
| `GetPhysicalDeviceIdProperties` | core | 1 | 2 |  | GetPhysicalDeviceProperties2 | 1 | 2 | 3 |
| `GetPhysicalDeviceImageFormatInfo2` | core | 1 | 2 |  | GetPhysicalDeviceImageFormatProperties2 | 1 | 4 | 9 |
| `GetPhysicalDeviceImageFormatProperties2` | core | 0 | 1 | vkGetPhysicalDeviceImageFormatProperties2 |  | 1 | 3 | 4 |
| `GetPhysicalDeviceImageMemoryProperties` | core | 1 | 3 |  | GetPhysicalDeviceImageFormatInfo2 | 1 | 4 | 8 |
| `GetPhysicalDeviceLuid` | core | 3 | 3 |  | GetPhysicalDeviceIdProperties | 0 | 2 | 2 |
| `GetPhysicalDeviceMemoryProperties` | core | 0 | 1 | vkGetPhysicalDeviceMemoryProperties |  | 0 | 2 | 2 |
| `GetPhysicalDevicePciBusInfoProperties` | core | 1 | 2 |  | GetPhysicalDeviceProperties2 | 1 | 2 | 3 |
| `GetPhysicalDeviceProperties2` | core | 0 | 1 | vkGetPhysicalDeviceProperties2 |  | 1 | 2 | 3 |
| `GetPhysicalDeviceQueueFamilyProperties` | core | 0 | 1 | vkGetPhysicalDeviceQueueFamilyProperties |  | 0 | 3 | 2 |
| `GetPhysicalDeviceSurfaceCapabilities` | core | 0 | 1 | vkGetPhysicalDeviceSurfaceCapabilitiesKHR |  | 0 | 3 | 3 |
| `GetPhysicalDeviceSurfaceFormatProperties` | core | 3 | 2 |  | GetPhysicalDeviceSurfaceFormats | 0 | 2 | 4 |
| `GetPhysicalDeviceSurfaceFormats` | core | 0 | 1 | vkGetPhysicalDeviceSurfaceFormatsKHR |  | 0 | 6 | 4 |
| `GetPhysicalDeviceSurfacePresentModeSupport` | core | 3 | 2 |  | GetPhysicalDeviceSurfacePresentModes | 0 | 1 | 4 |
| `GetPhysicalDeviceSurfacePresentModes` | core | 0 | 1 | vkGetPhysicalDeviceSurfacePresentModesKHR |  | 0 | 6 | 4 |
| `GetPhysicalDeviceSurfaceSupport` | core | 0 | 1 | vkGetPhysicalDeviceSurfaceSupportKHR |  | 0 | 4 | 4 |
| `GetPhysicalDevices` | core | 0 | 1 | vkEnumeratePhysicalDevices |  | 0 | 5 | 3 |
| `GetQueryPoolResults` | core | 0 | 1 | vkGetQueryPoolResults |  | 0 | 4 | 8 |
| `GetQueryPoolResults64` | core | 1' | 2 |  | GetQueryPoolResults | 0 | 2 | 6 |
| `GetQueueFamilyIndex` | core | util | 1 |  |  | 0 | 4 | 3 |
| `GetSurfaceCompositeAlpha` | core | util | 1 |  |  | 0 | 0 | 1 |
| `GetSurfaceTransform` | core | util | 1 |  |  | 0 | 0 | 1 |
| `GetSwapchainImages` | core | 0 | 1 | vkGetSwapchainImagesKHR |  | 0 | 7 | 4 |
| `InitAllocator` | core | infra | 1 |  |  | 0 | 1 | 1 |
| `InvalidateMappedMemoryRange` | core | 1 | 2 |  | InvalidateMappedMemoryRanges | 1 | 3 | 5 |
| `InvalidateMappedMemoryRanges` | core | 0 | 1 | vkInvalidateMappedMemoryRanges |  | 0 | 3 | 3 |
| `MapMemory` | core | 0 | 1 | vkMapMemory |  | 0 | 5 | 6 |
| `QueuePresent` | core | 0 | 1 | vkQueuePresentKHR |  | 1 | 5 | 8 |
| `QueuePresentImage` | core | 1' | 2 |  | QueuePresent | 0 | 0 | 7 |
| `QueueSubmit` | core | 1 | 2 |  | QueueSubmitInfos | 1 | 5 | 10 |
| `QueueSubmitCommandBuffer` | core | 1' | 3 |  | QueueSubmit | 0 | 1 | 9 |
| `QueueSubmitInfos` | core | 0 | 1 | vkQueueSubmit |  | 0 | 2 | 4 |
| `QueueSubmitTimelineSemaphore` | core | 1 | 3 |  | QueueSubmit | 1 | 0 | 12 |
| `QueueWaitIdle` | core | 0 | 1 | vkQueueWaitIdle |  | 0 | 1 | 1 |
| `ReadCoherentMemory` | core | 3 | 2 |  | MapMemory, UnmapMemory | 0 | 6 | 6 |
| `ReadMemory` | core | 3 | 3 |  | InvalidateMappedMemoryRange, MapMemory, UnmapMemory | 0 | 11 | 9 |
| `ResetCommandPool` | core | 0 | 1 | vkResetCommandPool |  | 0 | 2 | 3 |
| `ResetFence` | core | 1' | 2 |  | ResetFences | 0 | 1 | 2 |
| `ResetFences` | core | 0 | 1 | vkResetFences |  | 0 | 3 | 3 |
| `SelectPhysicalDeviceSurfaceFormat` | core | 3 | 2 |  | GetPhysicalDeviceFormatProperties, GetPhysicalDeviceSurfaceFormats | 0 | 5 | 5 |
| `SetupBuffer` | core | 2 | 5 |  | AllocateBufferMemory, BindBufferMemory, CreateBuffer | 0 | 6 | 14 |
| `SetupCommandPool` | core | 2 | 2 |  | AllocateCommandBuffers, CreateCommandPool | 0 | 0 | 10 |
| `SetupComputePipeline` | core | 2 | 3 |  | CreateComputePipeline, CreateComputePipelineLayout, CreateShaderModule | 1 | 2 | 24 |
| `SetupDevice` | core | 3 | 2 |  | CreateDevice, GetDeviceExtensionProperties, GetDeviceExtensionSupport | 0 | 7 | 12 |
| `SetupDeviceQueue` | core | 2 | 3 |  | GetDeviceQueue, SetupDevice | 1 | 6 | 15 |
| `SetupExportableDedicatedMemoryBuffer` | core | 2 | 6 |  | AllocateExportableDedicatedBufferMemory, BindBufferMemory, CreateExternalMemoryBuffer | 0 | 8 | 15 |
| `SetupExportableMemoryBuffer` | core | 2 | 6 |  | AllocateExportableBufferMemory, BindBufferMemory, CreateExternalMemoryBuffer | 0 | 8 | 15 |
| `SetupImage` | core | 2 | 5 |  | AllocateImageMemory, BindImageMemory, CreateImage | 0 | 11 | 21 |
| `SetupPlaneBuffer` | core | 2 | 6 |  | GetFormatPlaneLayout, SetupBuffer | 0 | 15 | 22 |
| `SetupSwapchain` | core | 3 | 3 |  | CreateSwapchain, GetPhysicalDeviceSurfaceCapabilities, GetPhysicalDeviceSurfaceFormatProperties, GetPhysicalDeviceSurfacePresentModeSupport, GetPhysicalDeviceSurfaceSupport, GetSurfaceCompositeAlpha, GetSurfaceTransform | 0 | 21 | 19 |
| `UnmapMemory` | core | 0 | 1 | vkUnmapMemory |  | 0 | 2 | 2 |
| `UpdateBufferDescriptorSet` | core | 1' | 3 |  | UpdateDescriptorSet | 0 | 3 | 9 |
| `UpdateDescriptorSet` | core | 1 | 2 |  | UpdateDescriptorSets | 1 | 3 | 10 |
| `UpdateDescriptorSets` | core | 0 | 1 | vkUpdateDescriptorSets |  | 0 | 3 | 5 |
| `UpdateImageDescriptorSet` | core | 1' | 3 |  | UpdateDescriptorSet | 0 | 2 | 9 |
| `WaitForFence` | core | 1' | 2 |  | WaitForFences | 0 | 1 | 3 |
| `WaitForFences` | core | 0 | 1 | vkWaitForFences |  | 0 | 3 | 5 |
| `WriteCoherentMemory` | core | 3 | 2 |  | MapMemory, UnmapMemory | 0 | 6 | 6 |
| `WriteDebugReportMessage` | core | util | 1 |  |  | 0 | 0 | 8 |
| `WriteDebugUtilsMessage` | core | util | 1 |  |  | 0 | 0 | 4 |
| `WriteMemory` | core | 3 | 3 |  | FlushMappedMemoryRange, MapMemory, UnmapMemory | 0 | 11 | 9 |
| `AllocateImportedDedicatedBufferMemoryFd` | linux | 1 | 5 |  | AllocateDedicatedExternalBufferMemory | 1 | 2 | 10 |
| `AllocateImportedDedicatedImageMemoryFd` | linux | 1 | 4 |  | AllocateDedicatedImageMemory | 1 | 2 | 9 |
| `AllocateImportedMemoryFd` | linux | 1 | 2 |  | AllocateMemory | 1 | 4 | 8 |
| `CreateExternalDrmFormatModifierImage` | linux | 3 | 3 |  | CreateExternalImage | 1 | 13 | 20 |
| `CreateSurfaceInfoXlib` | linux | 1 | 2 |  | CreateSurfaceXlib | 1 | 4 | 7 |
| `CreateSurfaceXlib` | linux | 0 | 1 | vkCreateXlibSurfaceKHR |  | 0 | 4 | 4 |
| `GetImageDrmFormatModifierProperties` | linux | 0 | 1 | vkGetImageDrmFormatModifierPropertiesEXT |  | 0 | 2 | 3 |
| `GetImageDrmFormatModifierPropertiesInfo` | linux | 1 | 2 |  | GetImageDrmFormatModifierProperties | 1 | 3 | 4 |
| `GetMemoryFd` | linux | 0 | 1 | vkGetMemoryFdKHR |  | 0 | 4 | 3 |
| `GetMemoryInfoFd` | linux | 1 | 2 |  | GetMemoryFd | 1 | 1 | 5 |
| `GetMemoryPropertiesFd` | linux | 0 | 1 | vkGetMemoryFdPropertiesKHR |  | 0 | 2 | 4 |
| `GetMemoryPropertiesInfoFd` | linux | 1 | 2 |  | GetMemoryPropertiesFd | 1 | 3 | 5 |
| `ImportSemaphoreFd` | linux | 0 | 1 | vkImportSemaphoreFdKHR |  | 0 | 2 | 2 |
| `ImportSemaphoreInfoFd` | linux | 1 | 2 |  | ImportSemaphoreFd | 1 | 3 | 6 |
| `SetupExportedDedicatedMemoryBufferFd` | linux | 2 | 7 |  | GetMemoryInfoFd, SetupExportableDedicatedMemoryBuffer | 0 | 0 | 17 |
| `SetupExportedMemoryBufferFd` | linux | 2 | 7 |  | GetMemoryInfoFd, SetupExportableMemoryBuffer | 0 | 0 | 17 |
| `SetupImportedDedicatedBufferMemoryFd` | linux | 2 | 6 |  | AllocateImportedDedicatedBufferMemoryFd, BindBufferMemory | 0 | 0 | 10 |
| `SetupImportedDedicatedImageMemoryFd` | linux | 2 | 5 |  | AllocateImportedDedicatedImageMemoryFd, BindImageMemory | 0 | 0 | 9 |
| `SetupImportedDedicatedMemoryImageFd` | linux | 2 | 6 |  | CreateExternalImage, GetImageMemoryAllocationInfo, GetMemoryPropertiesInfoFd, SetupImportedDedicatedImageMemoryFd | 0 | 0 | 23 |
| `SetupImportedDmaBufImage` | linux | 3 | 7 |  | GetDrmFormatModifierProperties, GetFormatPlaneCount, GetPhysicalDeviceDrmFormatModifierProperties, SetupImportedDrmFormatModifierImage | 0 | 6 | 25 |
| `SetupImportedDrmFormatModifierImage` | linux | 3 | 6 |  | AllocateImportedMemoryFd, BindImagePlaneMemories, CreateExternalDrmFormatModifierImage, GetImageMemoryAllocationInfo, GetImagePlaneMemoryRequirements2, GetMemoryPlaneAspect, GetMemoryPropertiesInfoFd, GetPhysicalDeviceCompatibleMemoryTypeIndex, GetPhysicalDeviceExternalImageImportSupport, SetupImportedDedicatedImageMemoryFd, SetupImportedImageMemoryFd | 1 | 33 | 25 |
| `SetupImportedImageMemoryFd` | linux | 2 | 3 |  | AllocateImportedMemoryFd, BindImageMemory | 0 | 0 | 10 |
| `SetupImportedImagePlaneMemoryFd` | linux | 2 | 4 |  | AllocateImportedMemoryFd, BindImagePlaneMemories | 0 | 0 | 12 |
| `SetupImportedMemoryBufferFd` | linux | 2 | 7 |  | CreateExternalMemoryBuffer, GetMemoryPropertiesInfoFd, SetupImportedDedicatedBufferMemoryFd | 0 | 0 | 16 |
| `SetupImportedMemoryImageFd` | linux | 2 | 4 |  | CreateExternalImage, GetImageMemoryAllocationInfo, GetMemoryPropertiesInfoFd, SetupImportedImageMemoryFd | 0 | 0 | 23 |
| `SetupImportedSemaphoreFd` | linux | 2 | 3 |  | CreateSemaphore, ImportSemaphoreInfoFd | 0 | 0 | 9 |
| `AllocateImportedDedicatedBufferMemoryHandleWin32` | win32 | 1 | 5 |  | AllocateDedicatedExternalBufferMemory | 1 | 2 | 11 |
| `AllocateImportedDedicatedImageMemoryHandleWin32` | win32 | 1 | 4 |  | AllocateDedicatedImageMemory | 1 | 1 | 10 |
| `AllocateImportedMemoryHandleWin32` | win32 | 1 | 2 |  | AllocateMemory | 1 | 1 | 9 |
| `CreateSurfaceInfoWin32` | win32 | 1 | 2 |  | CreateSurfaceWin32 | 1 | 4 | 7 |
| `CreateSurfaceWin32` | win32 | 0 | 1 | vkCreateWin32SurfaceKHR |  | 0 | 4 | 4 |
| `GetMemoryHandleInfoWin32` | win32 | 1 | 2 |  | GetMemoryHandleWin32 | 1 | 1 | 5 |
| `GetMemoryHandlePropertiesWin32` | win32 | 0 | 1 | vkGetMemoryWin32HandlePropertiesKHR |  | 1 | 3 | 5 |
| `GetMemoryHandleWin32` | win32 | 0 | 1 | vkGetMemoryWin32HandleKHR |  | 0 | 4 | 3 |
| `ImportSemaphoreHandleInfoWin32` | win32 | 1 | 2 |  | ImportSemaphoreHandleWin32 | 1 | 3 | 7 |
| `ImportSemaphoreHandleWin32` | win32 | 0 | 1 | vkImportSemaphoreWin32HandleKHR |  | 0 | 2 | 2 |
| `QueueSubmitD3D12Fence` | win32 | 1 | 3 |  | QueueSubmit | 1 | 0 | 12 |
| `SetupExportedDedicatedMemoryBufferHandleWin32` | win32 | 2 | 7 |  | GetMemoryHandleInfoWin32, SetupExportableDedicatedMemoryBuffer | 0 | 0 | 17 |
| `SetupExportedMemoryBufferHandleWin32` | win32 | 2 | 7 |  | GetMemoryHandleInfoWin32, SetupExportableMemoryBuffer | 0 | 0 | 17 |
| `SetupImportedDedicatedBufferMemoryHandleWin32` | win32 | 2 | 6 |  | AllocateImportedDedicatedBufferMemoryHandleWin32, BindBufferMemory | 0 | 0 | 11 |
| `SetupImportedDedicatedImageMemoryHandleWin32` | win32 | 2 | 5 |  | AllocateImportedDedicatedImageMemoryHandleWin32, BindImageMemory | 0 | 0 | 10 |
| `SetupImportedDedicatedMemoryImageHandleWin32` | win32 | 2 | 6 |  | CreateExternalImage, GetImageMemoryAllocationInfo, GetMemoryHandlePropertiesWin32, SetupImportedDedicatedImageMemoryHandleWin32 | 0 | 0 | 23 |
| `SetupImportedImageMemoryHandleWin32` | win32 | 2 | 3 |  | AllocateImportedMemoryHandleWin32, BindImageMemory | 0 | 0 | 11 |
| `SetupImportedImagePlaneMemoryHandleWin32` | win32 | 2 | 4 |  | AllocateImportedMemoryHandleWin32, BindImagePlaneMemories | 0 | 0 | 13 |
| `SetupImportedMemoryBufferHandleWin32` | win32 | 2 | 7 |  | CreateExternalMemoryBuffer, GetMemoryHandlePropertiesWin32, SetupImportedDedicatedBufferMemoryHandleWin32 | 0 | 0 | 16 |
| `SetupImportedMemoryImageHandleWin32` | win32 | 2 | 4 |  | CreateExternalImage, GetImageMemoryAllocationInfo, GetMemoryHandlePropertiesWin32, SetupImportedImageMemoryHandleWin32 | 0 | 0 | 23 |
| `SetupImportedMemoryImageWithSizeHandleWin32` | win32 | 2 | 4 |  | CreateExternalImage, GetImageMemoryAllocationInfo, GetMemoryHandlePropertiesWin32, SetupImportedImageMemoryHandleWin32 | 0 | 1 | 24 |
| `SetupImportedSemaphoreHandleWin32` | win32 | 2 | 3 |  | CreateSemaphore, ImportSemaphoreHandleInfoWin32 | 0 | 3 | 10 |
| `Platform_ExportImage` | win32-platform | util | 1 |  |  | 0 | 0 | 18 |
| `Platform_GetInstanceExtensions` | win32-platform | util | 1 |  |  | 0 | 2 | 2 |
| `Platform_ImportImage` | win32-platform | 3 | 8 |  | MapMemory, SetupBuffer, SetupImportedDedicatedMemoryImageHandleWin32, SetupImportedMemoryBufferHandleWin32, SetupImportedMemoryImageWithSizeHandleWin32, SetupImportedSemaphoreHandleWin32 | 0 | 16 | 29 |
| `Platform_SetupDeviceQueue` | win32-platform | 3 | 4 |  | GetPhysicalDeviceLuid, SetupDeviceQueue | 0 | 5 | 16 |
