# Reference-module findings

> The taxonomy itself is derived from Vulkan in `study/TAXONOMY.md`; this file only records
> how one hand-written module measures against mechanical rules and is secondary.

This file summarizes what `scripts/study_*.py` reveal about the gap between the
hand-written reference module and a fully generatable `cvk`. Each finding ends in a
**Decision** line: either a rule we adopt, or an open choice that must be made before
generation can start. Regenerate the underlying reports with:

```
python scripts/study_commands.py                       # -> study/commands.md
python scripts/study_structs.py --ext <EXT ...>        # -> study/structs.md
python scripts/study_reference.py --ref <path>         # -> study/reference.md
```

## 0. Headline numbers (reference module, Vulkan 1.2 profile)

| Metric | Value |
|---|---|
| Wrapper functions parsed | 275 |
| Tier 0 (one direct `vk*` call) | 87 |
| Tier 0 with the canonical name | 83 / 87 |
| Tier 0 with the exact mechanical parameter list | 63 / 87 |
| Tier 1 (one callee + builds an sType struct) | 37 |
| Tier 1' (one callee, constants only) | 61 |
| Tier 2 (Setup-named, straight-line composition) | 29 |
| Tier 3 (branching or looping composition) | 41 |
| Functions that mix a raw `vk*` call with wrapper calls | 0 |
| Prototypes with a complete Doxygen block | 274 / 275 |
| Registry commands in the profile that are wrapped | 88 / 225 |

The zero mixed functions and the near-complete documentation are the strongest
evidence that the pattern is already disciplined enough to generate. The 24 tier-0
parameter deviations are where the remaining decisions live.

## 1. Tier 0 deviates from mechanical flattening in five classes

From `study/reference.md`, section "Tier 0: name and parameter fidelity".

1. **Member renames.** `pfnUserCallback` -> `pfnCallback`, `minImageCount` -> `minimumImageCount`,
   `memoryRangeCount/pMemoryRanges` -> `rangeCount/pRanges`, `pSurfaceCapabilities` -> `pCapabilities`,
   `pMemoryFdProperties` -> `pProperties`, `pSubresource` -> `subresource` (pointer became by-value).
   **Decision:** forbid renames. Tier 0 uses Vulkan member and parameter names verbatim; by-value
   convenience belongs in tier 1'.
2. **The "Info flip" is applied inconsistently.** `Create*` flattens the info struct and has no
   pointer-taking sibling. `Get*`/`Import*` keep the pointer as tier 0 and add an `Info`-suffixed
   flattened sibling (`GetMemoryFd` + `GetMemoryInfoFd`, `GetImageMemoryRequirements2` + `…Info2`,
   `ImportSemaphoreFd` + `ImportSemaphoreInfoFd`, `CreateSurfaceWin32` + `CreateSurfaceInfoWin32`).
   **Decision (proposed):** the flattened form is always tier 0 and owns the vk name. A single
   `I` parameter is always flattened; an `I[]` parameter is never flattened (count + pointer stay).
   No pointer-taking sibling is generated; callers holding a struct call Vulkan directly.
3. **Output sType structs get a `pNext` parameter, named inconsistently.** `pNext` in
   `GetPhysicalDeviceProperties2`, `pPropertiesNext` in `GetPhysicalDeviceImageFormatProperties2`,
   `pMemoryNext` in `GetImageMemoryRequirements2`.
   **Decision (proposed):** every sType struct the wrapper fills, input or output, gets a
   `pNext` parameter. When one struct is filled it is plain `pNext`. When more than one is filled
   (two inputs, or input plus output) each is `p<Stem>Next` where `<Stem>` is the Vulkan parameter
   name without its `p` prefix (`pProperties` -> `pPropertiesNext`, `pCreateInfo` -> `pCreateInfoNext`).
4. **Two-call enumerations use three conventions.** Count kept as out-pointer
   (`GetPhysicalDevices`, `GetPhysicalDeviceSurfaceFormats`); count returned as `uint32_t`
   (`GetPhysicalDeviceQueueFamilyProperties`, `GetDisplayModeProperties`,
   `GetPhysicalDeviceDisplayProperties`, `GetDisplayPlaneSupportedDisplays`).
   **Decision (proposed):** always return the count as `uint32_t`, always take a fixed-capacity
   array parameter `T p[CVK_MAX_<NOUN>]`, always assert the count against the cap, treat
   `VK_INCOMPLETE` as fatal (it cannot occur once the cap is asserted).
5. **Tier 0 renamed to free the vk name for a convenience.** `CmdCopyImageRegions` /
   `CmdBlitImageRegions` / `QueueSubmitInfos` hold the raw call while `CmdCopyImage` /
   `CmdBlitImage` / `QueueSubmit` are tier 1'. `GetPhysicalDeviceBufferProperties` holds
   `vkGetPhysicalDeviceExternalBufferProperties` for the same reason.
   **Decision:** tier 0 always owns the canonical vk name. Conveniences take a suffix.

The canonical name rule that fits 83 of 87 tier-0 functions is implemented in
`study_reference.canonical_name`: drop `vk`, drop the vendor suffix, `Enumerate` -> `Get`,
move a platform or handle-kind token (`Win32`, `Xlib`, `Fd`) to the end. The 4 misses are all
class 5 above.

## 2. Registry facts the generator must handle (1.2 profile, 178 commands)

From `study/commands.md`.

- **Shapes are role-driven, not template-driven.** 104 distinct shape strings, but every one is a
  sequence of 20 parameter roles. The generator is a per-role emitter plus a per-verb epilogue
  (VKCHECK, output asserts), not 104 templates.
- **14 commands have informational success codes** (`VK_INCOMPLETE`, `VK_TIMEOUT`, `VK_NOT_READY`,
  `VK_EVENT_SET/RESET`, `VK_SUBOPTIMAL_KHR`, `VK_PIPELINE_COMPILE_REQUIRED`). The reference makes
  all of them fatal via `VKCHECK` (`WaitForFences`, `GetQueryPoolResults`, `AcquireNextImage`) and
  is silent on `VK_SUBOPTIMAL_KHR`.
  **Decision (open):** (a) keep fatal and require callers to use infinite timeouts and
  `VK_QUERY_RESULT_WAIT_BIT`, or (b) these commands alone return `VkResult`, still fatal on
  `VK_ERROR_*`. The registry's `successcodes` attribute selects the set mechanically either way.
- **2 commands swallow one error as "unsupported".** `vkGetPhysicalDeviceImageFormatProperties(2)`
  returns `VK_ERROR_FORMAT_NOT_SUPPORTED` and the reference zeroes the output instead of aborting.
  **Decision:** keep as an explicit policy-table entry (command -> tolerated error code -> zeroed
  output); nothing else in the profile needs it.
- **4 commands take two or more single sType inputs** (`vkCmdBeginRenderPass2`, `vkCmdNextSubpass2`)
  or several sType arrays (`vkCmdPipelineBarrier`, `vkCmdWaitEvents`, `vkUpdateDescriptorSets`).
  Two singles flatten with the `p<Stem>Next` rule from finding 1.3; arrays stay count + pointer.
- **5 commands return a value other than `VkResult`/`void`** (`*ProcAddr`, `GetBufferDeviceAddress`,
  two opaque-capture-address getters). These pass the return value through and assert non-zero.
- **12 commands are two-call enumerations**, 6 of which return `void` (no `VK_INCOMPLETE` possible).
- **`vkUpdateDescriptorSetWithTemplate` takes `const void* pData`.** Passthrough.
- **26 Destroy/Free commands are unwrapped in the reference** (process-lifetime policy).
  **Decision:** generate them anyway; their shape is trivial and omitting them is an application choice.

## 3. Struct facts (74 sType input structs, 76 in-profile extender pairs)

From `study/structs.md`.

- **Nested sType pointers are not flattenable at tier 0.** `VkDeviceCreateInfo.pQueueCreateInfos`
  (array), `VkGraphicsPipelineCreateInfo` (10 nested state structs), `VkRenderPassCreateInfo2`
  attachments/subpasses. **Decision:** a member that is a pointer to an sType struct stays a
  pointer at tier 0. A tier 1' or tier 2 function may build exactly one of them from flattened
  arguments (the reference's `SetupDeviceQueue` builds one `VkDeviceQueueCreateInfo`).
- **Qualifier naming is currently by taste.** `CreateExternalImage` (from
  `VkExternalMemoryImageCreateInfo`) vs `CreateExternalMemoryBuffer` (from
  `VkExternalMemoryBufferCreateInfo`); `AllocateExportableBufferMemory` from
  `VkExportMemoryAllocateInfo`; `AllocateImportedMemoryFd` from `VkImportMemoryFdInfoKHR`.
  **Decision (proposed):** qualifier = extender struct name minus the base struct's words minus
  the vendor suffix, so `ExternalMemory`, `Export`, `Dedicated`, `ImportFd`, `ImportWin32Handle`,
  `DrmFormatModifierExplicit`, `DrmFormatModifierList`. Name shape is
  `<Verb><Qualifier...><Noun>` with qualifiers in pNext-chain order, outermost first.
- **Qualifier stacking order needs one rule.** The reference chains ad hoc
  (`SetupExportableDedicatedMemoryBuffer`). **Decision (open):** alphabetical qualifier order in
  the name, chain built in that same order.
- **`VkDeviceCreateInfo` has 23 in-profile extenders, almost all `VkPhysicalDevice*Features`.**
  Generating one qualifier per feature struct is noise. **Decision:** extenders whose name matches
  `VkPhysicalDevice*Features*` are excluded from tier-1 generation; device creation takes a
  caller-built feature chain through `pNext`.
- **5 extenders extend several bases** (`VkImageFormatListCreateInfo` extends image, image-format
  query, and swapchain infos). One qualifier word, several tier-1 targets; no conflict.
- **Fixed arrays and computed lengths** appear only in returned-only structs and
  `VkPhysicalDeviceFeatures`, never in an input the profile flattens.

## 4. Tier structure findings

- **`Setup` is used for two different things.** `SetupImage`, `SetupBuffer`, `SetupCommandPool`
  are straight-line create/allocate/bind. `SetupDevice` (filters requested extensions in a loop)
  and `SetupSwapchain` (selects format, transform, alpha) branch and loop; the study classifies
  them tier 3. **Decision:** `Setup` is reserved for straight-line composition and is generated
  from a small template table. Policy-bearing composites get a different verb and stay hand-written.
- **Tier 1' is the largest hand-written tier (61).** Every one is a constant substitution:
  `VK_QUEUE_FAMILY_IGNORED`, `VK_QUEUE_FAMILY_EXTERNAL`, `TOP/BOTTOM_OF_PIPE`, `count = 1`,
  `depth = 1` for 2D, compute bind point, `VK_NO_OFFSET`. **Decision:** tier 1' is generated from
  a substitution table keyed by (target function, fixed argument set, suffix).
- **Utility helpers (16) and tier 3 (41) are the hand-written residue**, roughly one fifth of the
  module. Format-plane arithmetic, adapter matching, display-plane selection, and the transfer
  sequences live here and are the consumers the generated surface must satisfy.

## 5. Coverage

88 of 225 profile commands are wrapped. The unwrapped set is dominated by graphics-only
`Cmd*` (43), `Destroy*` (26), and physical-device queries (31). None of the unwrapped commands
introduces a parameter role that the wrapped set does not already exercise, which is the
precondition for generating them without new hand-written templates.

## 6. Open decisions, in the order they block generation

1. Info flip: flattened form owns the vk name, no pointer sibling (finding 1.2).
2. `pNext` naming for multiple filled structs: `p<Stem>Next` (finding 1.3).
3. Enumeration convention: return count, fixed-capacity array (finding 1.4).
4. Informational success codes: fatal vs `VkResult` return (finding 2).
5. Qualifier derivation and stacking order (finding 3).
6. `Setup` reserved for straight-line templates (finding 4).

Once 1 through 6 are fixed, `study_reference.py` can be re-pointed at generated output and
must report 100% canonical names and 100% mechanical parameter lists for tiers 0 and 1.
