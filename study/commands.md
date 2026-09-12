# Command shapes (VK_VERSION_1_2)

Commands: 178. Distinct shapes: 104.

## Shape legend

`H` in-handle, `s` scalar, `n` count for an in-array, `flags`, `enum`, `S` struct by value, `I` single sType info struct (flattenable), `I[]` array of sType info structs, `S*` plain struct pointer, `S[]` array of plain structs, `H[]` handle array, `s[]` scalar array, `str` C string, `alloc` pAllocator, `H*out` created handle, `P` single sType output struct, `P[]` output sType array, `n*` count in/out for two-call enumeration, `*out` other outputs. Return: `R` VkResult, `v` void, else the type.

## Verb histogram

| Verb | Count |
|---|---|
| Cmd | 51 |
| Get | 40 |
| Create | 24 |
| Destroy | 22 |
| Enumerate | 7 |
| Reset | 6 |
| Bind | 4 |
| Allocate | 3 |
| Free | 3 |
| Queue | 3 |
| Update | 2 |
| Wait | 2 |
| Begin | 1 |
| Device | 1 |
| End | 1 |
| Flush | 1 |
| Invalidate | 1 |
| Map | 1 |
| Merge | 1 |
| Set | 1 |
| Signal | 1 |
| Trim | 1 |
| Unmap | 1 |

## Shape histogram (templates a generator must implement)

| # | Shape | Members |
|---|---|---|
| 21 | `Create:R(H,I,alloc,H*out)` | vkCreateBuffer, vkCreateBufferView, vkCreateCommandPool, vkCreateDescriptorPool, vkCreateDescriptorSetLayout, vkCreateDescriptorUpdateTemplate, vkCreateDevice, vkCreateEvent, vkCreateFence, vkCreateFramebuffer, vkCreateImage, vkCreateImageView, vkCreatePipelineCache, vkCreatePipelineLayout, vkCreateQueryPool, vkCreateRenderPass, vkCreateRenderPass2, vkCreateSampler, vkCreateSamplerYcbcrConversion, vkCreateSemaphore, vkCreateShaderModule |
| 20 | `Destroy:v(H,H,alloc)` | vkDestroyBuffer, vkDestroyBufferView, vkDestroyCommandPool, vkDestroyDescriptorPool, vkDestroyDescriptorSetLayout, vkDestroyDescriptorUpdateTemplate, vkDestroyEvent, vkDestroyFence, vkDestroyFramebuffer, vkDestroyImage, vkDestroyImageView, vkDestroyPipeline, vkDestroyPipelineCache, vkDestroyPipelineLayout, vkDestroyQueryPool, vkDestroyRenderPass, vkDestroySampler, vkDestroySamplerYcbcrConversion, vkDestroySemaphore, vkDestroyShaderModule |
| 6 | `Get:v(H,I,P)` | vkGetBufferMemoryRequirements2, vkGetDescriptorSetLayoutSupport, vkGetImageMemoryRequirements2, vkGetPhysicalDeviceExternalBufferProperties, vkGetPhysicalDeviceExternalFenceProperties, vkGetPhysicalDeviceExternalSemaphoreProperties |
| 3 | `Cmd:v(H,H,s,s,s)` | vkCmdDrawIndexedIndirect, vkCmdDrawIndirect, vkCmdFillBuffer |
| 3 | `Cmd:v(H,flags,s)` | vkCmdSetStencilCompareMask, vkCmdSetStencilReference, vkCmdSetStencilWriteMask |
| 3 | `Get:v(H,H,S*out)` | vkGetBufferMemoryRequirements, vkGetImageMemoryRequirements, vkGetRenderAreaGranularity |
| 3 | `Get:v(H,P)` | vkGetPhysicalDeviceFeatures2, vkGetPhysicalDeviceMemoryProperties2, vkGetPhysicalDeviceProperties2 |
| 3 | `Get:v(H,S*out)` | vkGetPhysicalDeviceFeatures, vkGetPhysicalDeviceMemoryProperties, vkGetPhysicalDeviceProperties |
| 2 | `Allocate:R(H,I,H[]out)` | vkAllocateCommandBuffers, vkAllocateDescriptorSets |
| 2 | `Bind:R(H,H,H,s)` | vkBindBufferMemory, vkBindImageMemory |
| 2 | `Bind:R(H,n,I[])` | vkBindBufferMemory2, vkBindImageMemory2 |
| 2 | `Cmd:v(H,H,enum,H,enum,n,S[])` | vkCmdCopyImage, vkCmdResolveImage |
| 2 | `Cmd:v(H,H,enum,S*,n,S[])` | vkCmdClearColorImage, vkCmdClearDepthStencilImage |
| 2 | `Cmd:v(H,H,flags)` | vkCmdResetEvent, vkCmdSetEvent |
| 2 | `Cmd:v(H,H,s)` | vkCmdDispatchIndirect, vkCmdEndQuery |
| 2 | `Cmd:v(H,H,s,H,s,s,s)` | vkCmdDrawIndexedIndirectCount, vkCmdDrawIndirectCount |
| 2 | `Cmd:v(H,I,I)` | vkCmdBeginRenderPass2, vkCmdNextSubpass2 |
| 2 | `Cmd:v(H,s)` | vkCmdSetDeviceMask, vkCmdSetLineWidth |
| 2 | `Cmd:v(H,s,n,S[])` | vkCmdSetScissor, vkCmdSetViewport |
| 2 | `Cmd:v(H,s,s,s)` | vkCmdDispatch, vkCmdSetDepthBias |
| 2 | `Create:R(H,H,n,I[],alloc,H[]out)` | vkCreateComputePipelines, vkCreateGraphicsPipelines |
| 2 | `Destroy:v(H,alloc)` | vkDestroyDevice, vkDestroyInstance |
| 2 | `Get:PFN_vkVoidFunction(H,str)` | vkGetDeviceProcAddr, vkGetInstanceProcAddr |
| 2 | `Get:R(H,H)` | vkGetEventStatus, vkGetFenceStatus |
| 2 | `Get:uint64_t(H,I)` | vkGetBufferOpaqueCaptureAddress, vkGetDeviceMemoryOpaqueCaptureAddress |
| 2 | `Get:v(H,I,n*,P[])` | vkGetImageSparseMemoryRequirements2, vkGetPhysicalDeviceSparseImageFormatProperties2 |
| 2 | `Queue:R(H,n,I[],H)` | vkQueueBindSparse, vkQueueSubmit |
| 2 | `Reset:R(H,H,flags)` | vkResetCommandPool, vkResetDescriptorPool |
| 1 | `Allocate:R(H,I,alloc,H*out)` | vkAllocateMemory |
| 1 | `Begin:R(H,I)` | vkBeginCommandBuffer |
| 1 | `Cmd:v(H)` | vkCmdEndRenderPass |
| 1 | `Cmd:v(H,H,H,enum,n,S[])` | vkCmdCopyBufferToImage |
| 1 | `Cmd:v(H,H,H,n,S[])` | vkCmdCopyBuffer |
| 1 | `Cmd:v(H,H,enum,H,enum,n,S[],enum)` | vkCmdBlitImage |
| 1 | `Cmd:v(H,H,enum,H,n,S[])` | vkCmdCopyImageToBuffer |
| 1 | `Cmd:v(H,H,flags,s,n,void*in)` | vkCmdPushConstants |
| 1 | `Cmd:v(H,H,s,enum)` | vkCmdBindIndexBuffer |
| 1 | `Cmd:v(H,H,s,flags)` | vkCmdBeginQuery |
| 1 | `Cmd:v(H,H,s,n,void*in)` | vkCmdUpdateBuffer |
| 1 | `Cmd:v(H,H,s,s)` | vkCmdResetQueryPool |
| 1 | `Cmd:v(H,H,s,s,H,s,s,flags)` | vkCmdCopyQueryPoolResults |
| 1 | `Cmd:v(H,I)` | vkCmdEndRenderPass2 |
| 1 | `Cmd:v(H,I,enum)` | vkCmdBeginRenderPass |
| 1 | `Cmd:v(H,enum)` | vkCmdNextSubpass |
| 1 | `Cmd:v(H,enum,H)` | vkCmdBindPipeline |
| 1 | `Cmd:v(H,enum,H,s,n,H[],n,s[])` | vkCmdBindDescriptorSets |
| 1 | `Cmd:v(H,flags,H,s)` | vkCmdWriteTimestamp |
| 1 | `Cmd:v(H,flags,flags,flags,n,I[],n,I[],n,I[])` | vkCmdPipelineBarrier |
| 1 | `Cmd:v(H,n,H[])` | vkCmdExecuteCommands |
| 1 | `Cmd:v(H,n,H[],flags,flags,n,I[],n,I[],n,I[])` | vkCmdWaitEvents |
| 1 | `Cmd:v(H,n,S[],n,S[])` | vkCmdClearAttachments |
| 1 | `Cmd:v(H,s*in)` | vkCmdSetBlendConstants |
| 1 | `Cmd:v(H,s,n,H[],s[])` | vkCmdBindVertexBuffers |
| 1 | `Cmd:v(H,s,s)` | vkCmdSetDepthBounds |
| 1 | `Cmd:v(H,s,s,s,s)` | vkCmdDraw |
| 1 | `Cmd:v(H,s,s,s,s,s)` | vkCmdDrawIndexed |
| 1 | `Cmd:v(H,s,s,s,s,s,s)` | vkCmdDispatchBase |
| 1 | `Create:R(I,alloc,H*out)` | vkCreateInstance |
| 1 | `Device:R(H)` | vkDeviceWaitIdle |
| 1 | `End:R(H)` | vkEndCommandBuffer |
| 1 | `Enumerate:R(H,n*,H[]out)` | vkEnumeratePhysicalDevices |
| 1 | `Enumerate:R(H,n*,P[])` | vkEnumeratePhysicalDeviceGroups |
| 1 | `Enumerate:R(H,n*,S[]out)` | vkEnumerateDeviceLayerProperties |
| 1 | `Enumerate:R(H,str,n*,S[]out)` | vkEnumerateDeviceExtensionProperties |
| 1 | `Enumerate:R(n*,S[]out)` | vkEnumerateInstanceLayerProperties |
| 1 | `Enumerate:R(s*out)` | vkEnumerateInstanceVersion |
| 1 | `Enumerate:R(str,n*,S[]out)` | vkEnumerateInstanceExtensionProperties |
| 1 | `Flush:R(H,n,I[])` | vkFlushMappedMemoryRanges |
| 1 | `Free:R(H,H,n,H[])` | vkFreeDescriptorSets |
| 1 | `Free:v(H,H,alloc)` | vkFreeMemory |
| 1 | `Free:v(H,H,n,H[])` | vkFreeCommandBuffers |
| 1 | `Get:R(H,H,n*,void*out)` | vkGetPipelineCacheData |
| 1 | `Get:R(H,H,s*out)` | vkGetSemaphoreCounterValue |
| 1 | `Get:R(H,H,s,s,n,void*out,s,flags)` | vkGetQueryPoolResults |
| 1 | `Get:R(H,I,P)` | vkGetPhysicalDeviceImageFormatProperties2 |
| 1 | `Get:R(H,enum,enum,enum,flags,flags,S*out)` | vkGetPhysicalDeviceImageFormatProperties |
| 1 | `Get:VkDeviceAddress(H,I)` | vkGetBufferDeviceAddress |
| 1 | `Get:v(H,H,S*,S*out)` | vkGetImageSubresourceLayout |
| 1 | `Get:v(H,H,n*,S[]out)` | vkGetImageSparseMemoryRequirements |
| 1 | `Get:v(H,H,s*out)` | vkGetDeviceMemoryCommitment |
| 1 | `Get:v(H,I,H*out)` | vkGetDeviceQueue2 |
| 1 | `Get:v(H,enum,P)` | vkGetPhysicalDeviceFormatProperties2 |
| 1 | `Get:v(H,enum,S*out)` | vkGetPhysicalDeviceFormatProperties |
| 1 | `Get:v(H,enum,enum,flags,flags,enum,n*,S[]out)` | vkGetPhysicalDeviceSparseImageFormatProperties |
| 1 | `Get:v(H,n*,P[])` | vkGetPhysicalDeviceQueueFamilyProperties2 |
| 1 | `Get:v(H,n*,S[]out)` | vkGetPhysicalDeviceQueueFamilyProperties |
| 1 | `Get:v(H,s,s,H*out)` | vkGetDeviceQueue |
| 1 | `Get:v(H,s,s,s,s*out)` | vkGetDeviceGroupPeerMemoryFeatures |
| 1 | `Invalidate:R(H,n,I[])` | vkInvalidateMappedMemoryRanges |
| 1 | `Map:R(H,H,s,s,flags,void*out)` | vkMapMemory |
| 1 | `Merge:R(H,H,n,H[])` | vkMergePipelineCaches |
| 1 | `Queue:R(H)` | vkQueueWaitIdle |
| 1 | `Reset:R(H,H)` | vkResetEvent |
| 1 | `Reset:R(H,flags)` | vkResetCommandBuffer |
| 1 | `Reset:R(H,n,H[])` | vkResetFences |
| 1 | `Reset:v(H,H,s,s)` | vkResetQueryPool |
| 1 | `Set:R(H,H)` | vkSetEvent |
| 1 | `Signal:R(H,I)` | vkSignalSemaphore |
| 1 | `Trim:v(H,H,flags)` | vkTrimCommandPool |
| 1 | `Unmap:v(H,H)` | vkUnmapMemory |
| 1 | `Update:v(H,H,H,?constvoid)` | vkUpdateDescriptorSetWithTemplate |
| 1 | `Update:v(H,n,I[],n,I[])` | vkUpdateDescriptorSets |
| 1 | `Wait:R(H,I,s)` | vkWaitSemaphores |
| 1 | `Wait:R(H,n,H[],s,s)` | vkWaitForFences |

## Two-call enumerations (need CVK_MAX_* capacity + VK_INCOMPLETE handling)

- `vkEnumerateDeviceExtensionProperties` succ=VK_SUCCESS,VK_INCOMPLETE
- `vkEnumerateDeviceLayerProperties` succ=VK_SUCCESS,VK_INCOMPLETE
- `vkEnumerateInstanceExtensionProperties` succ=VK_SUCCESS,VK_INCOMPLETE
- `vkEnumerateInstanceLayerProperties` succ=VK_SUCCESS,VK_INCOMPLETE
- `vkEnumeratePhysicalDeviceGroups` succ=VK_SUCCESS,VK_INCOMPLETE
- `vkEnumeratePhysicalDevices` succ=VK_SUCCESS,VK_INCOMPLETE
- `vkGetImageSparseMemoryRequirements` succ=
- `vkGetImageSparseMemoryRequirements2` succ=
- `vkGetPhysicalDeviceQueueFamilyProperties` succ=
- `vkGetPhysicalDeviceQueueFamilyProperties2` succ=
- `vkGetPhysicalDeviceSparseImageFormatProperties` succ=
- `vkGetPhysicalDeviceSparseImageFormatProperties2` succ=

## Commands with non-error success codes other than VK_SUCCESS (conflict with fatal-on-non-success)

- `vkCreateComputePipelines`: VK_PIPELINE_COMPILE_REQUIRED_EXT
- `vkCreateGraphicsPipelines`: VK_PIPELINE_COMPILE_REQUIRED_EXT
- `vkEnumerateDeviceExtensionProperties`: VK_INCOMPLETE
- `vkEnumerateDeviceLayerProperties`: VK_INCOMPLETE
- `vkEnumerateInstanceExtensionProperties`: VK_INCOMPLETE
- `vkEnumerateInstanceLayerProperties`: VK_INCOMPLETE
- `vkEnumeratePhysicalDeviceGroups`: VK_INCOMPLETE
- `vkEnumeratePhysicalDevices`: VK_INCOMPLETE
- `vkGetEventStatus`: VK_EVENT_SET, VK_EVENT_RESET
- `vkGetFenceStatus`: VK_NOT_READY
- `vkGetPipelineCacheData`: VK_INCOMPLETE
- `vkGetQueryPoolResults`: VK_NOT_READY
- `vkWaitForFences`: VK_TIMEOUT
- `vkWaitSemaphores`: VK_TIMEOUT

## Irregular commands (break a simple template)

- `vkCmdBeginRenderPass2` — more than one sType input struct — `Cmd:v(H,I,I)`
- `vkCmdNextSubpass2` — more than one sType input struct — `Cmd:v(H,I,I)`
- `vkCmdPipelineBarrier` — more than one sType input struct — `Cmd:v(H,flags,flags,flags,n,I[],n,I[],n,I[])`
- `vkCmdWaitEvents` — more than one sType input struct — `Cmd:v(H,n,H[],flags,flags,n,I[],n,I[],n,I[])`
- `vkGetBufferDeviceAddress` — returns VkDeviceAddress — `Get:VkDeviceAddress(H,I)`
- `vkGetBufferOpaqueCaptureAddress` — returns uint64_t — `Get:uint64_t(H,I)`
- `vkGetDeviceMemoryOpaqueCaptureAddress` — returns uint64_t — `Get:uint64_t(H,I)`
- `vkGetDeviceProcAddr` — returns PFN_vkVoidFunction — `Get:PFN_vkVoidFunction(H,str)`
- `vkGetInstanceProcAddr` — returns PFN_vkVoidFunction — `Get:PFN_vkVoidFunction(H,str)`
- `vkUpdateDescriptorSetWithTemplate` — unclassified parameter — `Update:v(H,H,H,?constvoid)`
- `vkUpdateDescriptorSets` — more than one sType input struct — `Update:v(H,n,I[],n,I[])`

## Per-command table

| Command | Since | Ext | Return | Shape | Success | Queues |
|---|---|---|---|---|---|---|
| `vkAllocateCommandBuffers` | VK_VERSION_1_0 |  | VkResult | `Allocate:R(H,I,H[]out)` | VK_SUCCESS |  |
| `vkAllocateDescriptorSets` | VK_VERSION_1_0 |  | VkResult | `Allocate:R(H,I,H[]out)` | VK_SUCCESS |  |
| `vkAllocateMemory` | VK_VERSION_1_0 |  | VkResult | `Allocate:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkBeginCommandBuffer` | VK_VERSION_1_0 |  | VkResult | `Begin:R(H,I)` | VK_SUCCESS |  |
| `vkBindBufferMemory` | VK_VERSION_1_0 |  | VkResult | `Bind:R(H,H,H,s)` | VK_SUCCESS |  |
| `vkBindBufferMemory2` | VK_VERSION_1_1 |  | VkResult | `Bind:R(H,n,I[])` | VK_SUCCESS |  |
| `vkBindImageMemory` | VK_VERSION_1_0 |  | VkResult | `Bind:R(H,H,H,s)` | VK_SUCCESS |  |
| `vkBindImageMemory2` | VK_VERSION_1_1 |  | VkResult | `Bind:R(H,n,I[])` | VK_SUCCESS |  |
| `vkCmdBeginQuery` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,s,flags)` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_VIDEO_DECODE_BIT_KHR, VK_QUEUE_VIDEO_ENCODE_BIT_KHR |
| `vkCmdBeginRenderPass` | VK_VERSION_1_0 |  | void | `Cmd:v(H,I,enum)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdBeginRenderPass2` | VK_VERSION_1_2 |  | void | `Cmd:v(H,I,I)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdBindDescriptorSets` | VK_VERSION_1_0 |  | void | `Cmd:v(H,enum,H,s,n,H[],n,s[])` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_DATA_GRAPH_BIT_ARM |
| `vkCmdBindIndexBuffer` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,s,enum)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdBindPipeline` | VK_VERSION_1_0 |  | void | `Cmd:v(H,enum,H)` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_DATA_GRAPH_BIT_ARM |
| `vkCmdBindVertexBuffers` | VK_VERSION_1_0 |  | void | `Cmd:v(H,s,n,H[],s[])` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdBlitImage` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,enum,H,enum,n,S[],enum)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdClearAttachments` | VK_VERSION_1_0 |  | void | `Cmd:v(H,n,S[],n,S[])` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdClearColorImage` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,enum,S*,n,S[])` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT |
| `vkCmdClearDepthStencilImage` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,enum,S*,n,S[])` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdCopyBuffer` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,H,n,S[])` |  | VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT |
| `vkCmdCopyBufferToImage` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,H,enum,n,S[])` |  | VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT |
| `vkCmdCopyImage` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,enum,H,enum,n,S[])` |  | VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT |
| `vkCmdCopyImageToBuffer` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,enum,H,n,S[])` |  | VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT |
| `vkCmdCopyQueryPoolResults` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,s,s,H,s,s,flags)` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT |
| `vkCmdDispatch` | VK_VERSION_1_0 |  | void | `Cmd:v(H,s,s,s)` |  | VK_QUEUE_COMPUTE_BIT |
| `vkCmdDispatchBase` | VK_VERSION_1_1 |  | void | `Cmd:v(H,s,s,s,s,s,s)` |  | VK_QUEUE_COMPUTE_BIT |
| `vkCmdDispatchIndirect` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,s)` |  | VK_QUEUE_COMPUTE_BIT |
| `vkCmdDraw` | VK_VERSION_1_0 |  | void | `Cmd:v(H,s,s,s,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdDrawIndexed` | VK_VERSION_1_0 |  | void | `Cmd:v(H,s,s,s,s,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdDrawIndexedIndirect` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,s,s,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdDrawIndexedIndirectCount` | VK_VERSION_1_2 |  | void | `Cmd:v(H,H,s,H,s,s,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdDrawIndirect` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,s,s,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdDrawIndirectCount` | VK_VERSION_1_2 |  | void | `Cmd:v(H,H,s,H,s,s,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdEndQuery` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,s)` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_VIDEO_DECODE_BIT_KHR, VK_QUEUE_VIDEO_ENCODE_BIT_KHR |
| `vkCmdEndRenderPass` | VK_VERSION_1_0 |  | void | `Cmd:v(H)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdEndRenderPass2` | VK_VERSION_1_2 |  | void | `Cmd:v(H,I)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdExecuteCommands` | VK_VERSION_1_0 |  | void | `Cmd:v(H,n,H[])` |  | VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT |
| `vkCmdFillBuffer` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,s,s,s)` |  | VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT |
| `vkCmdNextSubpass` | VK_VERSION_1_0 |  | void | `Cmd:v(H,enum)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdNextSubpass2` | VK_VERSION_1_2 |  | void | `Cmd:v(H,I,I)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdPipelineBarrier` | VK_VERSION_1_0 |  | void | `Cmd:v(H,flags,flags,flags,n,I[],n,I[],n,I[])` |  | VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_VIDEO_DECODE_BIT_KHR, VK_QUEUE_VIDEO_ENCODE_BIT_KHR |
| `vkCmdPushConstants` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,flags,s,n,void*in)` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT |
| `vkCmdResetEvent` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,flags)` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_VIDEO_DECODE_BIT_KHR, VK_QUEUE_VIDEO_ENCODE_BIT_KHR |
| `vkCmdResetQueryPool` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,s,s)` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_VIDEO_DECODE_BIT_KHR, VK_QUEUE_VIDEO_ENCODE_BIT_KHR, VK_QUEUE_OPTICAL_FLOW_BIT_NV |
| `vkCmdResolveImage` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,enum,H,enum,n,S[])` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdSetBlendConstants` | VK_VERSION_1_0 |  | void | `Cmd:v(H,s*in)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdSetDepthBias` | VK_VERSION_1_0 |  | void | `Cmd:v(H,s,s,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdSetDepthBounds` | VK_VERSION_1_0 |  | void | `Cmd:v(H,s,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdSetDeviceMask` | VK_VERSION_1_1 |  | void | `Cmd:v(H,s)` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_TRANSFER_BIT |
| `vkCmdSetEvent` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,flags)` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_VIDEO_DECODE_BIT_KHR, VK_QUEUE_VIDEO_ENCODE_BIT_KHR |
| `vkCmdSetLineWidth` | VK_VERSION_1_0 |  | void | `Cmd:v(H,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdSetScissor` | VK_VERSION_1_0 |  | void | `Cmd:v(H,s,n,S[])` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdSetStencilCompareMask` | VK_VERSION_1_0 |  | void | `Cmd:v(H,flags,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdSetStencilReference` | VK_VERSION_1_0 |  | void | `Cmd:v(H,flags,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdSetStencilWriteMask` | VK_VERSION_1_0 |  | void | `Cmd:v(H,flags,s)` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdSetViewport` | VK_VERSION_1_0 |  | void | `Cmd:v(H,s,n,S[])` |  | VK_QUEUE_GRAPHICS_BIT |
| `vkCmdUpdateBuffer` | VK_VERSION_1_0 |  | void | `Cmd:v(H,H,s,n,void*in)` |  | VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT |
| `vkCmdWaitEvents` | VK_VERSION_1_0 |  | void | `Cmd:v(H,n,H[],flags,flags,n,I[],n,I[],n,I[])` |  | VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_VIDEO_DECODE_BIT_KHR, VK_QUEUE_VIDEO_ENCODE_BIT_KHR |
| `vkCmdWriteTimestamp` | VK_VERSION_1_0 |  | void | `Cmd:v(H,flags,H,s)` |  | VK_QUEUE_TRANSFER_BIT, VK_QUEUE_GRAPHICS_BIT, VK_QUEUE_COMPUTE_BIT, VK_QUEUE_VIDEO_DECODE_BIT_KHR, VK_QUEUE_VIDEO_ENCODE_BIT_KHR, VK_QUEUE_OPTICAL_FLOW_BIT_NV |
| `vkCreateBuffer` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateBufferView` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateCommandPool` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateComputePipelines` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,H,n,I[],alloc,H[]out)` | VK_SUCCESS, VK_PIPELINE_COMPILE_REQUIRED_EXT |  |
| `vkCreateDescriptorPool` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateDescriptorSetLayout` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateDescriptorUpdateTemplate` | VK_VERSION_1_1 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateDevice` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateEvent` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateFence` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateFramebuffer` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateGraphicsPipelines` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,H,n,I[],alloc,H[]out)` | VK_SUCCESS, VK_PIPELINE_COMPILE_REQUIRED_EXT |  |
| `vkCreateImage` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateImageView` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateInstance` | VK_VERSION_1_0 |  | VkResult | `Create:R(I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreatePipelineCache` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreatePipelineLayout` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateQueryPool` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateRenderPass` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateRenderPass2` | VK_VERSION_1_2 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateSampler` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateSamplerYcbcrConversion` | VK_VERSION_1_1 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateSemaphore` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkCreateShaderModule` | VK_VERSION_1_0 |  | VkResult | `Create:R(H,I,alloc,H*out)` | VK_SUCCESS |  |
| `vkDestroyBuffer` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyBufferView` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyCommandPool` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyDescriptorPool` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyDescriptorSetLayout` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyDescriptorUpdateTemplate` | VK_VERSION_1_1 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyDevice` | VK_VERSION_1_0 |  | void | `Destroy:v(H,alloc)` |  |  |
| `vkDestroyEvent` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyFence` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyFramebuffer` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyImage` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyImageView` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyInstance` | VK_VERSION_1_0 |  | void | `Destroy:v(H,alloc)` |  |  |
| `vkDestroyPipeline` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyPipelineCache` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyPipelineLayout` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyQueryPool` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyRenderPass` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroySampler` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroySamplerYcbcrConversion` | VK_VERSION_1_1 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroySemaphore` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDestroyShaderModule` | VK_VERSION_1_0 |  | void | `Destroy:v(H,H,alloc)` |  |  |
| `vkDeviceWaitIdle` | VK_VERSION_1_0 |  | VkResult | `Device:R(H)` | VK_SUCCESS |  |
| `vkEndCommandBuffer` | VK_VERSION_1_0 |  | VkResult | `End:R(H)` | VK_SUCCESS |  |
| `vkEnumerateDeviceExtensionProperties` | VK_VERSION_1_0 |  | VkResult | `Enumerate:R(H,str,n*,S[]out)` | VK_SUCCESS, VK_INCOMPLETE |  |
| `vkEnumerateDeviceLayerProperties` | VK_VERSION_1_0 |  | VkResult | `Enumerate:R(H,n*,S[]out)` | VK_SUCCESS, VK_INCOMPLETE |  |
| `vkEnumerateInstanceExtensionProperties` | VK_VERSION_1_0 |  | VkResult | `Enumerate:R(str,n*,S[]out)` | VK_SUCCESS, VK_INCOMPLETE |  |
| `vkEnumerateInstanceLayerProperties` | VK_VERSION_1_0 |  | VkResult | `Enumerate:R(n*,S[]out)` | VK_SUCCESS, VK_INCOMPLETE |  |
| `vkEnumerateInstanceVersion` | VK_VERSION_1_1 |  | VkResult | `Enumerate:R(s*out)` | VK_SUCCESS |  |
| `vkEnumeratePhysicalDeviceGroups` | VK_VERSION_1_1 |  | VkResult | `Enumerate:R(H,n*,P[])` | VK_SUCCESS, VK_INCOMPLETE |  |
| `vkEnumeratePhysicalDevices` | VK_VERSION_1_0 |  | VkResult | `Enumerate:R(H,n*,H[]out)` | VK_SUCCESS, VK_INCOMPLETE |  |
| `vkFlushMappedMemoryRanges` | VK_VERSION_1_0 |  | VkResult | `Flush:R(H,n,I[])` | VK_SUCCESS |  |
| `vkFreeCommandBuffers` | VK_VERSION_1_0 |  | void | `Free:v(H,H,n,H[])` |  |  |
| `vkFreeDescriptorSets` | VK_VERSION_1_0 |  | VkResult | `Free:R(H,H,n,H[])` | VK_SUCCESS |  |
| `vkFreeMemory` | VK_VERSION_1_0 |  | void | `Free:v(H,H,alloc)` |  |  |
| `vkGetBufferDeviceAddress` | VK_VERSION_1_2 |  | VkDeviceAddress | `Get:VkDeviceAddress(H,I)` |  |  |
| `vkGetBufferMemoryRequirements` | VK_VERSION_1_0 |  | void | `Get:v(H,H,S*out)` |  |  |
| `vkGetBufferMemoryRequirements2` | VK_VERSION_1_1 |  | void | `Get:v(H,I,P)` |  |  |
| `vkGetBufferOpaqueCaptureAddress` | VK_VERSION_1_2 |  | uint64_t | `Get:uint64_t(H,I)` |  |  |
| `vkGetDescriptorSetLayoutSupport` | VK_VERSION_1_1 |  | void | `Get:v(H,I,P)` |  |  |
| `vkGetDeviceGroupPeerMemoryFeatures` | VK_VERSION_1_1 |  | void | `Get:v(H,s,s,s,s*out)` |  |  |
| `vkGetDeviceMemoryCommitment` | VK_VERSION_1_0 |  | void | `Get:v(H,H,s*out)` |  |  |
| `vkGetDeviceMemoryOpaqueCaptureAddress` | VK_VERSION_1_2 |  | uint64_t | `Get:uint64_t(H,I)` |  |  |
| `vkGetDeviceProcAddr` | VK_VERSION_1_0 |  | PFN_vkVoidFunction | `Get:PFN_vkVoidFunction(H,str)` |  |  |
| `vkGetDeviceQueue` | VK_VERSION_1_0 |  | void | `Get:v(H,s,s,H*out)` |  |  |
| `vkGetDeviceQueue2` | VK_VERSION_1_1 |  | void | `Get:v(H,I,H*out)` |  |  |
| `vkGetEventStatus` | VK_VERSION_1_0 |  | VkResult | `Get:R(H,H)` | VK_EVENT_SET, VK_EVENT_RESET |  |
| `vkGetFenceStatus` | VK_VERSION_1_0 |  | VkResult | `Get:R(H,H)` | VK_SUCCESS, VK_NOT_READY |  |
| `vkGetImageMemoryRequirements` | VK_VERSION_1_0 |  | void | `Get:v(H,H,S*out)` |  |  |
| `vkGetImageMemoryRequirements2` | VK_VERSION_1_1 |  | void | `Get:v(H,I,P)` |  |  |
| `vkGetImageSparseMemoryRequirements` | VK_VERSION_1_0 |  | void | `Get:v(H,H,n*,S[]out)` |  |  |
| `vkGetImageSparseMemoryRequirements2` | VK_VERSION_1_1 |  | void | `Get:v(H,I,n*,P[])` |  |  |
| `vkGetImageSubresourceLayout` | VK_VERSION_1_0 |  | void | `Get:v(H,H,S*,S*out)` |  |  |
| `vkGetInstanceProcAddr` | VK_VERSION_1_0 |  | PFN_vkVoidFunction | `Get:PFN_vkVoidFunction(H,str)` |  |  |
| `vkGetPhysicalDeviceExternalBufferProperties` | VK_VERSION_1_1 |  | void | `Get:v(H,I,P)` |  |  |
| `vkGetPhysicalDeviceExternalFenceProperties` | VK_VERSION_1_1 |  | void | `Get:v(H,I,P)` |  |  |
| `vkGetPhysicalDeviceExternalSemaphoreProperties` | VK_VERSION_1_1 |  | void | `Get:v(H,I,P)` |  |  |
| `vkGetPhysicalDeviceFeatures` | VK_VERSION_1_0 |  | void | `Get:v(H,S*out)` |  |  |
| `vkGetPhysicalDeviceFeatures2` | VK_VERSION_1_1 |  | void | `Get:v(H,P)` |  |  |
| `vkGetPhysicalDeviceFormatProperties` | VK_VERSION_1_0 |  | void | `Get:v(H,enum,S*out)` |  |  |
| `vkGetPhysicalDeviceFormatProperties2` | VK_VERSION_1_1 |  | void | `Get:v(H,enum,P)` |  |  |
| `vkGetPhysicalDeviceImageFormatProperties` | VK_VERSION_1_0 |  | VkResult | `Get:R(H,enum,enum,enum,flags,flags,S*out)` | VK_SUCCESS |  |
| `vkGetPhysicalDeviceImageFormatProperties2` | VK_VERSION_1_1 |  | VkResult | `Get:R(H,I,P)` | VK_SUCCESS |  |
| `vkGetPhysicalDeviceMemoryProperties` | VK_VERSION_1_0 |  | void | `Get:v(H,S*out)` |  |  |
| `vkGetPhysicalDeviceMemoryProperties2` | VK_VERSION_1_1 |  | void | `Get:v(H,P)` |  |  |
| `vkGetPhysicalDeviceProperties` | VK_VERSION_1_0 |  | void | `Get:v(H,S*out)` |  |  |
| `vkGetPhysicalDeviceProperties2` | VK_VERSION_1_1 |  | void | `Get:v(H,P)` |  |  |
| `vkGetPhysicalDeviceQueueFamilyProperties` | VK_VERSION_1_0 |  | void | `Get:v(H,n*,S[]out)` |  |  |
| `vkGetPhysicalDeviceQueueFamilyProperties2` | VK_VERSION_1_1 |  | void | `Get:v(H,n*,P[])` |  |  |
| `vkGetPhysicalDeviceSparseImageFormatProperties` | VK_VERSION_1_0 |  | void | `Get:v(H,enum,enum,flags,flags,enum,n*,S[]out)` |  |  |
| `vkGetPhysicalDeviceSparseImageFormatProperties2` | VK_VERSION_1_1 |  | void | `Get:v(H,I,n*,P[])` |  |  |
| `vkGetPipelineCacheData` | VK_VERSION_1_0 |  | VkResult | `Get:R(H,H,n*,void*out)` | VK_SUCCESS, VK_INCOMPLETE |  |
| `vkGetQueryPoolResults` | VK_VERSION_1_0 |  | VkResult | `Get:R(H,H,s,s,n,void*out,s,flags)` | VK_SUCCESS, VK_NOT_READY |  |
| `vkGetRenderAreaGranularity` | VK_VERSION_1_0 |  | void | `Get:v(H,H,S*out)` |  |  |
| `vkGetSemaphoreCounterValue` | VK_VERSION_1_2 |  | VkResult | `Get:R(H,H,s*out)` | VK_SUCCESS |  |
| `vkInvalidateMappedMemoryRanges` | VK_VERSION_1_0 |  | VkResult | `Invalidate:R(H,n,I[])` | VK_SUCCESS |  |
| `vkMapMemory` | VK_VERSION_1_0 |  | VkResult | `Map:R(H,H,s,s,flags,void*out)` | VK_SUCCESS |  |
| `vkMergePipelineCaches` | VK_VERSION_1_0 |  | VkResult | `Merge:R(H,H,n,H[])` | VK_SUCCESS |  |
| `vkQueueBindSparse` | VK_VERSION_1_0 |  | VkResult | `Queue:R(H,n,I[],H)` | VK_SUCCESS | VK_QUEUE_SPARSE_BINDING_BIT |
| `vkQueueSubmit` | VK_VERSION_1_0 |  | VkResult | `Queue:R(H,n,I[],H)` | VK_SUCCESS |  |
| `vkQueueWaitIdle` | VK_VERSION_1_0 |  | VkResult | `Queue:R(H)` | VK_SUCCESS |  |
| `vkResetCommandBuffer` | VK_VERSION_1_0 |  | VkResult | `Reset:R(H,flags)` | VK_SUCCESS |  |
| `vkResetCommandPool` | VK_VERSION_1_0 |  | VkResult | `Reset:R(H,H,flags)` | VK_SUCCESS |  |
| `vkResetDescriptorPool` | VK_VERSION_1_0 |  | VkResult | `Reset:R(H,H,flags)` | VK_SUCCESS |  |
| `vkResetEvent` | VK_VERSION_1_0 |  | VkResult | `Reset:R(H,H)` | VK_SUCCESS |  |
| `vkResetFences` | VK_VERSION_1_0 |  | VkResult | `Reset:R(H,n,H[])` | VK_SUCCESS |  |
| `vkResetQueryPool` | VK_VERSION_1_2 |  | void | `Reset:v(H,H,s,s)` |  |  |
| `vkSetEvent` | VK_VERSION_1_0 |  | VkResult | `Set:R(H,H)` | VK_SUCCESS |  |
| `vkSignalSemaphore` | VK_VERSION_1_2 |  | VkResult | `Signal:R(H,I)` | VK_SUCCESS |  |
| `vkTrimCommandPool` | VK_VERSION_1_1 |  | void | `Trim:v(H,H,flags)` |  |  |
| `vkUnmapMemory` | VK_VERSION_1_0 |  | void | `Unmap:v(H,H)` |  |  |
| `vkUpdateDescriptorSetWithTemplate` | VK_VERSION_1_1 |  | void | `Update:v(H,H,H,?constvoid)` |  |  |
| `vkUpdateDescriptorSets` | VK_VERSION_1_0 |  | void | `Update:v(H,n,I[],n,I[])` |  |  |
| `vkWaitForFences` | VK_VERSION_1_0 |  | VkResult | `Wait:R(H,n,H[],s,s)` | VK_SUCCESS, VK_TIMEOUT |  |
| `vkWaitSemaphores` | VK_VERSION_1_2 |  | VkResult | `Wait:R(H,I,s)` | VK_SUCCESS, VK_TIMEOUT |  |
