# The Vulkan-derived wrapper taxonomy

This document derives the hierarchical-compositional wrapper taxonomy from Vulkan's own
structure as encoded in `vk.xml`, not from any existing wrapper. Data comes from
`scripts/study_taxonomy.py` (object model, command classes, struct classes, X/X2 families,
handle flow) and `scripts/study_commands.py` (parameter roles). A hand-written module is
audited against the result in section 4 as evidence, and it is found wrong in several places.

## 1. What Vulkan itself declares

### 1.1 Object model

Vulkan is an object hierarchy with six *dispatchable* levels and a tree of non-dispatchable
children. The registry records the parent of every handle:

```
VkInstance
└─ VkPhysicalDevice           (retrieved by enumeration, never destroyed)
   └─ VkDevice
      ├─ VkQueue              (retrieved, never destroyed)
      ├─ VkCommandPool ─ VkCommandBuffer      (allocated/freed from pool)
      ├─ VkDescriptorPool ─ VkDescriptorSet   (allocated/freed from pool)
      ├─ VkDeviceMemory, VkBuffer, VkImage, VkImageView, VkBufferView, VkSampler,
      │  VkShaderModule, VkPipelineLayout, VkPipelineCache, VkPipeline, VkRenderPass,
      │  VkFramebuffer, VkDescriptorSetLayout, VkQueryPool, VkFence, VkSemaphore, VkEvent
      └─ VkSwapchainKHR
   VkSurfaceKHR (instance child), VkDisplayKHR / VkDisplayModeKHR (physical-device children)
```

Every non-root, non-retrieved handle has exactly one constructor verb (`Create` or `Allocate`)
and one destructor verb (`Destroy` or `Free`), and the pairing is by handle type. Retrieved
handles (`VkPhysicalDevice`, `VkQueue`, `VkDisplayKHR`, swapchain `VkImage`s) are owned by the
parent and have no destructor. These are Vulkan's lifecycle rules; the wrapper inherits them.

### 1.2 Command classes

Commands are indexed by the first parameter's dispatch level. Across the level axis the
registry and the command verbs give nine lifecycle classes:

| Class | Verbs | Registry signal |
|---|---|---|
| constructor | Create, Allocate | output param of handle type |
| destructor | Destroy, Free | handle param + pAllocator, `void` return |
| retrieval | Get*Queue, Enumerate*PhysicalDevices, GetSwapchainImages | output handle without pAllocator |
| enumeration | Enumerate*, Get*Properties with count | `n*` count in/out, `VK_INCOMPLETE` success code |
| query | Get* | non-handle output, no count |
| state-transition | Bind, Begin, End, Map, Unmap, Reset, Update, Import, Set, Signal | mutates an object's state machine |
| record | Cmd* | `VkCommandBuffer` dispatch, registry `tasks` |
| submit | Queue* | `VkQueue` dispatch |
| host-sync | Wait*, DeviceWaitIdle, QueueWaitIdle | blocks the host, `VK_TIMEOUT` |

Recorded commands carry Vulkan's own sub-taxonomy in the registry: `tasks` is one or more of
`action`, `state`, `synchronization`, `indirection`; `renderpass` is `inside`, `outside`, or
`both`; `cmdbufferlevel` is `primary` and/or `secondary`; `queues` lists the queue capabilities
that may record it. No wrapper needs to invent a classification for `vkCmd*`; it is given.

### 1.3 Result semantics

Every `VkResult`-returning command declares `successcodes` and `errorcodes`. Across the whole
registry there are 13 informational success codes; in the 1.2 profile there are 6:
`VK_INCOMPLETE` (enumeration capacity), `VK_TIMEOUT` and `VK_NOT_READY` (host-sync polling),
`VK_EVENT_SET`/`VK_EVENT_RESET` (event status), `VK_SUBOPTIMAL_KHR` (presentation),
`VK_PIPELINE_COMPILE_REQUIRED` (opt-in). They are not failures. A wrapper that aborts on any
non-`VK_SUCCESS` is wrong for exactly these commands and correct for all others.

### 1.4 Struct classes

The registry distinguishes structs by three attributes: presence of `sType`, `returnedonly`,
and `structextends`. In the 1.2 profile plus WSI and external-memory extensions:

| Class | Count | Definition |
|---|---|---|
| direct input | 72 | sType struct passed directly to a command |
| nested input | 18 | sType struct reachable only through another struct's pointer member (pipeline state, queue create, subpass) |
| extender | 91 | sType struct that only appears in a `pNext` chain |
| output | 37 | `returnedonly` sType struct |
| POD carrier | 74 | no sType; passed by value or nested (extents, offsets, ranges, layouts) |
| region | 12 | POD struct passed as an array to a `vkCmd*` (copies, blits, clears, viewports) |
| union | 2 | `VkClearColorValue`, `VkClearValue` |

`structextends` is Vulkan's composition mechanism: a base struct plus zero or more extension
structs chained through `pNext`. The set of legal chains is fully enumerated by the registry.

### 1.5 Vulkan's own flattening direction

Every `vkX2` command replaces `vkX`'s positional parameters with one sType struct whose members
correspond one-to-one by name (`vkBindImageMemory` ↔ `VkBindImageMemoryInfo`,
`vkCmdCopyImage` ↔ `VkCopyImageInfo2`, `vkGetPhysicalDeviceImageFormatProperties` ↔
`VkPhysicalDeviceImageFormatInfo2`). Where members differ the X2 form is strictly more general
(`pipelineBindPoint` becomes `stageFlags`; barrier stage masks move into each barrier). So a
struct-flattened signature is not a wrapper invention: it is the positional signature Vulkan
itself used before it added `pNext` extensibility. Flattening an input struct recovers Vulkan's
original calling convention plus a `pNext` slot.

### 1.6 Composition chains implied by handle flow

For each handle, the commands that consume it without an `action` task are its preparatory
steps. The registry yields these chains directly:

| Handle | Chain to usable state |
|---|---|
| VkBuffer, VkImage | Create → GetMemoryRequirements → (select memory type) → AllocateMemory → BindMemory |
| VkCommandBuffer | CreateCommandPool → AllocateCommandBuffers → BeginCommandBuffer |
| VkDescriptorSet | CreateDescriptorSetLayout → CreateDescriptorPool → AllocateDescriptorSets → UpdateDescriptorSets |
| VkPipeline (compute) | CreateShaderModule → CreatePipelineLayout → CreateComputePipelines |
| VkSwapchainKHR | GetPhysicalDeviceSurface{Support,Capabilities,Formats,PresentModes} → CreateSwapchain → GetSwapchainImages |
| VkSemaphore (external) | CreateSemaphore → ImportSemaphore{Fd,Win32Handle} |
| VkDeviceMemory (host) | AllocateMemory → MapMemory → Flush/Invalidate → UnmapMemory |

The memory chain is the archetype: a query produces `memoryTypeBits`, a selection intersects it
with requested property flags against `VkPhysicalDeviceMemoryProperties`, and the result feeds
the constructor. Dedicated and exportable allocation are `pNext` extenders on the same chain.

### 1.7 Protocols defined by the specification, not the registry

Some vocabulary exists only in the spec text: queue family ownership transfer is a *release*
operation on the source queue paired with an *acquire* operation on the destination queue, both
expressed as the same `VkImageMemoryBarrier` with `srcQueueFamilyIndex`/`dstQueueFamilyIndex`
set, and `VK_QUEUE_FAMILY_EXTERNAL` denotes a non-Vulkan owner. Image layout transitions,
timeline semaphore waits, and external handle import/export are similar protocol-level
sequences. The registry gives them no names; the spec does.

## 2. The taxonomy that follows

Each tier below is defined by a registry relation, so its membership is computable.

| Tier | Name | One function per | Body |
|---|---|---|---|
| 0 | command | registry command in profile | asserts, fill one sType struct from flattened arguments, one `vk*` call, result policy from `successcodes`, assert outputs |
| 1 | extension | (direct-input struct, in-profile extender) pair | one extender struct in front of the caller's `pNext`, call tier 0 |
| 1' | specialization | row of a constant-substitution table | forward to tier 0/1 with fixed arguments |
| 2 | chain | handle-flow chain from 1.6 | straight-line sequence of tier 0/1 calls, every produced handle returned |
| 3 | protocol | spec protocol from 1.7 | hand-written sequence using tier 0–2 vocabulary |
| P | platform | registry `platform` attribute | tier 0–2 restricted to that platform's extensions |

### 2.1 Tier 0 rules per parameter role

| Role | Wrapper parameter |
|---|---|
| dispatch handle, in-handle, scalar, enum, flags | verbatim |
| single sType input (`I`) | members in registry order, `sType` dropped, `pNext` kept |
| nested sType pointer inside an `I` struct | kept as pointer (not flattened; see 1.4) |
| sType input array (`I[]`) | count + pointer verbatim |
| region / POD arrays | count + pointer verbatim |
| `pAllocator` | verbatim, always last input |
| created handle (`H*out`) | out-pointer, asserted non-null after the call |
| output sType struct (`P`) | `pNext` for the struct plus the out-pointer; wrapper fills `sType`/`pNext` |
| two-call enumeration (`n*` + array) | returns `uint32_t` count, takes `T p[CVK_MAX_<NOUN>]`, asserts count ≤ cap |
| non-`VkResult` return | passed through |

Result policy: `void` return and abort on any code not in `successcodes`; for commands whose
`successcodes` contain more than `VK_SUCCESS`, return `VkResult` and abort only on `errorcodes`.
This is derived per command from the registry, never chosen by hand.

`pNext` naming: one filled struct → `pNext`. Several filled structs (two `I`, or `I` plus `P`)
→ `p<Stem>Next` where `<Stem>` is the Vulkan parameter name without its `p`.

### 2.2 Naming

Names are the Vulkan command name with the prefix swapped and the vendor suffix dropped;
`Enumerate` stays `Enumerate` because Vulkan uses it to mark the two-call protocol. Promoted
extension commands are emitted under the core name and the alias is not emitted. Tier-1
qualifiers are the extender struct name minus the base struct's words and vendor suffix
(`VkExternalMemoryImageCreateInfo` on `VkImageCreateInfo` → `ExternalMemory`;
`VkMemoryDedicatedAllocateInfo` → `Dedicated`; `VkImportMemoryFdInfoKHR` → `ImportFd`), placed
between verb and noun in chain order. Platform tokens stay where Vulkan puts them
(`GetMemoryWin32Handle`, not `GetMemoryHandleWin32`).

### 2.3 Exclusions that follow from the classes

- Extenders matching `VkPhysicalDevice*Features*` are excluded from tier 1; device creation takes a
  caller-built feature chain.
- Nested input structs (1.4) get no tier-0 flattening; tier 2 may build at most one of them.
- Destructors are generated for every constructor because Vulkan pairs them; omitting
  destruction is an application policy, not a wrapper property.

## 3. What is hand-written and why

Tier 3 and the selection helpers in tier 2 chains (memory type selection, surface format
selection, adapter matching) encode either spec protocol or application policy. Everything
else has a registry relation behind it and is generated.

## 4. Audit of the hand-written reference against this taxonomy

Evidence from `scripts/study_reference.py` and direct reading. Items marked **wrong** violate
Vulkan semantics or the taxonomy above; items marked **divergent** are consistent but chosen
differently from what the registry implies.

1. **Wrong: informational success codes are fatal.** `vkAcquireNextImageKHR`, `vkQueuePresentKHR`,
   `vkWaitForFences`, `vkGetQueryPoolResults` are all wrapped with an unconditional
   `VK_SUCCESS` assert. `VK_SUBOPTIMAL_KHR`, `VK_TIMEOUT`, `VK_NOT_READY` are success codes in
   the registry. The wrapper turns legal outcomes into aborts.
2. **Wrong: deprecated extension preferred.** Instance creation prefers `VK_EXT_debug_report`
   and falls back to `VK_EXT_debug_utils`. The registry marks `VK_EXT_debug_report` as
   deprecated by `VK_EXT_debug_utils`; the preference order is inverted.
3. **Wrong: error swallowed too broadly.** `vkGetPhysicalDeviceImageFormatProperties2` zeroes
   its output on *any* non-success, including out-of-memory. Only
   `VK_ERROR_FORMAT_NOT_SUPPORTED` means "unsupported"; the rest are failures.
4. **Wrong: hidden mutable state.** Device setup writes requested extension names into
   file-scope static arrays. Vulkan's object model has no process-global state below the
   loader; a wrapper that adds some is not reentrant and cannot be called for two devices
   concurrently.
5. **Wrong: silent degradation.** Device setup drops requested extensions that are absent
   instead of failing. Vulkan reports `VK_ERROR_EXTENSION_NOT_PRESENT`; a fatal-on-error
   wrapper that silently narrows the request contradicts its own policy.
6. **Wrong: destructors omitted.** No `vkDestroy*`/`vkFree*` is wrapped. Every constructor in
   1.1 has a paired destructor; the wrapper covers half of each object's lifecycle.
7. **Divergent: the flattening direction is applied inconsistently.** `Create*` flattens the
   info struct; `Get*2`/`Import*` keep the pointer and add an `Info`-suffixed flattened sibling.
   Section 1.5 shows flattening is Vulkan's own positional form, so the flattened function
   should own the name in both cases.
8. **Divergent: `Enumerate` renamed to `Get`.** This erases Vulkan's marker for the two-call
   protocol and for `VK_INCOMPLETE`.
9. **Divergent: three enumeration conventions** coexist (count returned, count out-pointer,
   count asserted non-zero). The registry has one enumeration protocol.
10. **Divergent: qualifier words differ from the extender struct names** (`Exportable` for
    `VkExportMemoryAllocateInfo`, `External` for `VkExternalMemoryImageCreateInfo` but
    `ExternalMemory` for the buffer form). The registry supplies the words.
11. **Divergent: platform tokens moved** (`GetMemoryHandleWin32` for `vkGetMemoryWin32HandleKHR`).
12. **Divergent: `pNext` position and naming for output structs** vary
    (`pNext` after the handle, `pNext` after `format`, `pPropertiesNext`).
13. **Divergent: `Setup` covers both straight-line chains and policy-bearing selection**
    (extension filtering, surface format choice). Section 1.6 chains are straight-line; the
    selections are the hand-written helpers of section 3.

The reference's ownership-transfer helpers (`TOP_OF_PIPE`/access 0 on acquire,
`BOTTOM_OF_PIPE`/access 0 on release, `VK_QUEUE_FAMILY_EXTERNAL`) match the spec's protocol
in 1.7 and are the right vocabulary for tier 3.
