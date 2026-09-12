# cvk

`cvk` is a procedural generator for a **hierarchical-compositional C wrapper** around the
Vulkan core and extension APIs. The wrapper it emits is a flat, fatal-on-error, assertion-heavy
C surface in which every raw `vkFoo` becomes `cvkFoo`, and progressively higher functions compose
lower ones without ever hiding Vulkan. This brief describes that pattern for first-time readers
so that generated output can be judged against a fixed set of rules rather than taste.

## 1. What the wrapper is

Every Vulkan entry point `vkFoo` becomes a `cvkFoo` function with the **same information
content, but flattened**: struct fields become positional parameters, `VkResult` is consumed
internally, and preconditions are asserted rather than documented away. On top of that flat base
sit progressively higher functions that *compose* lower ones. Each tier only removes a fixed
amount of boilerplate and fixes a fixed set of choices; nothing in the hierarchy is opaque.

A full surface covers instance, physical device, device, memory, image, buffer, descriptor,
pipeline, command buffer, barrier, transfer, dispatch, query, sync, surface, display, and
swapchain paths, with roughly two hundred functions in the core header and a few dozen per
platform header. Only `cvk` sources call `vk*` directly; everything above consumes the wrapper.

## 2. The tiers

Tiers are recognizable by name and by body shape. Each tier's body is *only* composition of
lower-tier calls plus local scratch, so any function can be read top-down to the raw call.

| Tier | Name shape | Body shape | Example |
|---|---|---|---|
| 0 | `cvk<VerbNoun>` | asserts, fill one `Vk*Info` from positional args, one `vk*` call under `CVK_CHECK`, assert outputs | `cvkCreateImage`, `cvkCreateBuffer`, `cvkCmdPipelineBarrier`, `cvkAllocateMemory` |
| 1 | tier 0 name + qualifier (`External`, `Dedicated`, `Exportable`, `Imported…Fd/…Win32`) | build one `pNext` extension struct, chain caller's `pNext` behind it, call tier 0 | `cvkCreateExternalImage`, `cvkAllocateDedicatedMemory`, `cvkAllocateExportableBufferMemory` |
| 1' | tier 0 name with specialization suffix or dropped args | forward to the general form with constants filled in | `cvkCmdImageBarrier` (queue families = `IGNORED`), `cvkCmdAcquireImageFromExternal` (src = `EXTERNAL`, top-of-pipe), `*2D` variants, `cvkWaitForFence` (count = 1) |
| 2 | `cvkSetup<Noun>` | create + allocate + bind, or create + query + select, in one call; returns every handle produced | `cvkSetupImage`, `cvkSetupBuffer`, `cvkSetupDevice`, `cvkSetupDeviceQueue`, `cvkSetupComputePipeline`, `cvkSetupSwapchain` |
| 3 | domain verbs: `Transfer…`, `Acquire/Release…`, `Dispatch…Items` | sequences of tier 0–2 calls that encode a *policy* (barrier before, copy, barrier after) | `cvkCmdTransferBufferToImageQueueOwnership`, `cvkCmdReleaseComputeBufferToExternal`, `cvkCmdDispatchComputeItems2D` |
| P | `cvkPlatform<Verb>` | one fixed signature per platform, implemented separately per OS from that platform's tier 1–3 helpers | `cvkPlatformSetupDeviceQueue`, `cvkPlatformExportImage`, `cvkPlatformImportImage`, `cvkPlatformGetInstanceExtensions` |

Two naming sub-rules matter for generation:

- **`Info` flip.** When Vulkan takes a pointer to an input struct (`vkGetMemoryFdKHR(device, pInfo, pFd)`),
  tier 0 keeps the struct pointer (`cvkGetMemoryFd`) and a sibling *flattens it* (`cvkGetMemoryInfoFd`,
  `cvkGetPhysicalDeviceImageFormatInfo2`, `cvkImportSemaphoreInfoFd`). The flattened one always
  calls the struct one. Both are public.
- **Qualifier stacking.** Qualifiers compose left to right in the name and inner to outer in the
  `pNext` chain: `cvkSetupExportableDedicatedMemoryBuffer` = create-external-buffer, then
  allocate-exportable-dedicated, then bind. The generator treats each qualifier as a
  `(pNext struct, extra parameters)` pair and stacks them mechanically.

### Worked example (condensed from the buffer family)

```c
/* Tier 0: flatten VkBufferCreateInfo, one vk call, fatal on failure. */
void cvkCreateBuffer(VkDevice device, const void* pNext, VkBufferCreateFlags flags,
                     VkDeviceSize size, VkBufferUsageFlags usage, VkSharingMode sharingMode,
                     uint32_t queueFamilyIndexCount, const uint32_t* pQueueFamilyIndices,
                     const VkAllocationCallbacks* pAllocator, VkBuffer* pBuffer)
{
    VkBufferCreateInfo create_info;
    CVK_ASSERT(VK_NULL_HANDLE != device);
    CVK_ASSERT(0 != size);
    CVK_ASSERT(0 == queueFamilyIndexCount || NULL != pQueueFamilyIndices);
    create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.pNext = pNext;
    /* ...remaining fields assigned positionally... */
    CVK_CHECK(vkCreateBuffer(device, &create_info, pAllocator, pBuffer));
    CVK_ASSERT(VK_NULL_HANDLE != (*pBuffer));
}

/* Tier 1: one extension struct in front of the caller's chain, then tier 0. */
void cvkCreateExternalMemoryBuffer(VkDevice device, const void* pNext,
                                   VkExternalMemoryHandleTypeFlags handleTypes, /* ...same tail... */)
{
    VkExternalMemoryBufferCreateInfo external_info;
    external_info.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    external_info.pNext       = pNext;
    external_info.handleTypes = handleTypes;
    cvkCreateBuffer(device, &external_info, /* ...same tail... */);
}

/* Tier 2: create, allocate, bind; every produced handle is an out-param. */
void cvkSetupExportableMemoryBuffer(VkPhysicalDevice physicalDevice, VkDevice device,
                                    const void* pBufferNext, VkExternalMemoryHandleTypeFlags handleTypes,
                                    /* ...buffer args..., */ const void* pMemoryNext,
                                    VkMemoryPropertyFlags properties, const VkAllocationCallbacks* pAllocator,
                                    VkBuffer* pBuffer, VkDeviceMemory* pMemory)
{
    cvkCreateExternalMemoryBuffer(device, pBufferNext, handleTypes, /* ... */, pAllocator, pBuffer);
    cvkAllocateExportableBufferMemory(physicalDevice, device, *pBuffer, pMemoryNext, handleTypes, properties, pAllocator, pMemory);
    cvkBindBufferMemory(device, *pBuffer, *pMemory, CVK_NO_OFFSET);
}
```

## 3. Invariants every function keeps

- **Fatal, not fallible.** `CVK_CHECK` aborts on any non-success `VkResult`. Functions return
  `void` unless the value is a pure query (`VkBool32`, index, count, handle). There is no error
  path for callers; the only exceptions are queries that legitimately answer "unsupported" and
  set a zeroed output or `VK_FALSE`.
- **Assert every precondition.** Handles non-null, counts non-zero, `count == 0 || ptr != NULL`,
  formats not `UNDEFINED`, masks non-zero. Outputs are asserted after the call (`*pImage != NULL`).
- **`pNext` always threads through.** Every tier-0 creator takes `const void* pNext` for its main
  struct. Tier 1 accepts the caller's `pNext` and places it *behind* its own extension struct.
  Composite setups take one `pNext` per struct they build (`pImageNext`, `pMemoryNext`, `pQueueNext`).
- **`pAllocator` always threads through** to the leaf, even when the chosen policy passes `NULL`.
- **Fixed-capacity enumeration.** Two-call `vkEnumerate*` / `vkGet*` patterns write into
  caller-provided arrays sized by `CVK_MAX_*` constants (`VkPhysicalDevice p[CVK_MAX_PHYSICAL_DEVICES]`),
  with the count asserted against the cap. No heap allocation anywhere in the wrapper.
- **Extension commands are resolved per call** through `cvkGetDeviceProcAddr` / `cvkGetInstanceProcAddr`,
  which are themselves fatal on missing symbols. No global function-pointer table.
- **Positional argument order mirrors the Vulkan struct order**, prefixed by the owning handle(s),
  followed by `pAllocator`, then out-params last. Multi-struct setups interleave in
  creation order (image args, then memory args).
- **Doxygen contract on every prototype**: `\brief`, `\par Details`, one `\param[in|out]` per
  argument with fixed wording conventions ("Optional extension chain … or NULL",
  "Receives the created …"), `\retval None`. Definitions carry only `\par Details`.

## 4. Why the shape is generatable

The pattern is regular because none of its authoring rules depend on judgment:

1. Tier 0 is a pure function of `vk.xml`: command name, parameters, the single input struct
   (if any), its `sType`, and which parameters are outputs.
2. Tier 1 is a pure function of `vk.xml` `structextends` relations: for each `Vk*CreateInfo` /
   `Vk*AllocateInfo`, every extending struct yields one qualifier with that struct's fields as
   extra leading parameters.
3. Tier 1' variants are a small, finite table of constant substitutions (`VK_QUEUE_FAMILY_IGNORED`,
   `VK_QUEUE_FAMILY_EXTERNAL`, `TOP/BOTTOM_OF_PIPE`, `count = 1`, 3D→2D with `depth = 1`).
4. Tier 2 `cvkSetup*` functions follow one of a few templates: create→allocate→bind for anything
   with memory, filter-requested-extensions→create for device and instance, create→enumerate
   for swapchain.
5. Tier 3 and platform functions are the only ones that encode *application policy* and are the
   ones `cvk` treats as hand-authored input, not output.

The core header targets Vulkan 1.2 with selected KHR/EXT extensions; platform headers add, for
example, `VK_KHR_external_memory_fd`, `VK_EXT_image_drm_format_modifier`, and `VK_KHR_xlib_surface`
on Linux, and `VK_KHR_external_memory_win32`, `VK_KHR_external_semaphore_win32`, and
`VK_KHR_win32_surface` on Windows. Extension presence in a header is what decides which
tier 0/1 functions exist there.

## 5. What cvk produces

- `cvk.h` / `cvk.c` for tiers 0, 1, 1', and templated tier 2, generated from the Vulkan registry
  plus a small policy file (naming prefix, `CVK_MAX_*` capacities, which extensions and platforms
  to include, constant-substitution table).
- Per-platform headers (`cvk_linux.h`, `cvk_win32.h`) with the same tiers restricted to that
  platform's extensions.
- Doxygen contracts generated from the registry's parameter descriptions with the fixed phrasing
  above, so hand-written tier 3 code reads identically to generated tier 0–2 code.
- Assertion sets derived mechanically: `optional="false"` handles → non-null assert, `len=`
  attributes → `count == 0 || ptr != NULL`, non-`optional` bitmasks → non-zero assert.

Out of scope for generation: tier 3 domain sequences, `cvkPlatform*`, the debug callbacks, and
format/plane arithmetic helpers (`cvkGetFormatPlaneLayout`, `cvkGetBufferImageCopyRegions`).
Those remain hand-written and are the consumers that the generated surface must satisfy.

## 6. Reading a generated surface

Read one family top to bottom in the core header:
`cvkCreateBuffer` → `cvkCreateExternalMemoryBuffer` → `cvkAllocateBufferMemory` →
`cvkAllocateExportableBufferMemory` → `cvkSetupBuffer` → `cvkSetupExportableMemoryBuffer`.
Then read `cvkCmdPipelineBarrier` → `cvkCmdImageQueueOwnershipBarrier` → `cvkCmdImageBarrier` →
`cvkCmdAcquireImageFromExternal` → `cvkCmdTransferBufferToImageQueueOwnership` →
`cvkCmdTransferBufferToImage2D`. Those two chains exhibit every tier and every naming rule here.
