# cvk generation specification (working document)

Status: draft under experiment. Every rule here is either **derived** from a registry relation
(no choice involved) or **policy** (a choice, recorded with its reason). `scripts/generate.py`
implements the rules; `study/*.md` holds the evidence. When a rule changes, change it here
first, then the generator, then regenerate and re-run the studies.

## 1. Inputs

- `external/Vulkan-Headers/registry/vk.xml` (pinned by the submodule).
- `cvk.policy.json`: profile (core version, extension list or `"all"`, platforms), capacity
  constants, `role_overrides` for registry quirks, the tier-1' substitution table, and the
  tier-2 chain table. The profile is `VK_VERSION_1_4` with every extension whose platform is
  none or one of the profile's platforms (`win32`, `xlib`).

## 2. Outputs

**Rule zero: the outputs are never edited by hand.** Any defect in `include/cvk.h` or
`src/cvk.c` is a defect in a rule here or in `scripts/generate.py`. Fix the rule, regenerate,
re-verify. A commit that changes the outputs without changing the generator or the policy is
wrong by definition.

- `include/cvk.h`, `src/cvk.c`: core profile, free of platform conditionals.
- `include/cvk_<platform>.h`, `src/cvk_<platform>.c` per registry platform (`win32`, `xlib`),
  each including `cvk.h` and its platform headers.
- Tier 3 protocols are folded into the platform pairs from `scripts/protocols/` (section 8b).
- Every emitted function has a full Doxygen block with fixed phrasing (section 9).

## 3. Names (derived)

| Thing | Name |
|---|---|
| tier 0 for `vkXyz[Vendor]` | `cvkXyz`; vendor suffix dropped; alias commands are not emitted, only their promoted target. **Canonical suffix rule:** when several in-profile commands (or structs) share a stem after suffix stripping, only the canonical one drops its suffix: lowest vendor rank (core, then `KHR`, then `EXT`, then any other vendor) when that rank is held by exactly one of them; the rest keep their suffix (`cvkCmdDrawMeshTasks` for `EXT`, `cvkCmdDrawMeshTasksNV`; `cvkInitExportMemoryAllocateInfo`, `cvkInitExportMemoryAllocateInfoNV`). Same rank held by several of the same kind: all keep the suffix. An output extender's replacement name (section 6) competes in the command group of that name, so `vkGetPhysicalDeviceExternalImageFormatPropertiesNV` keeps its suffix beside the core `VkExternalImageFormatProperties` convenience; a tie between a command and a struct at the same rank is settled by tier-0 ownership: the command keeps the plain name and the convenience takes its `With` form (`cvkGetPhysicalDeviceCooperativeMatrixProperties` is the `KHR` enumeration, `cvkGetPhysicalDeviceProperties2WithCooperativeMatrixProperties` the limits query). A kept suffix always trails the whole derived name, every suffix in order of appearance (`cvkCreateImageWithExternalMemoryInfoNV`, `cvkGetGeneratedCommandsMemoryRequirementsWithTileRequirementsNVQCOM`) |
| builder for `VkAbcInfo` | `cvkInitAbcInfo` |
| single-qualifier convenience | Distinct part `D` = the extender's tokens outside the blocks that match its **base struct** (block alignment; a one-token block strictly inside the extender is kept, so `DrmFormatModifier` stays whole), minus the trailing struct-kind word (`Create`, `Allocate`, `Begin`, `Bind`, `Submit`) and `Info`/version tokens; vendor suffix dropped. Placement by verb. **Operation extenders** (`Import*`, `Export*`): the extender names the operation, so the name is `cvk<Verb><ExtenderStruct minus kind/Info>`, the `vkImportSemaphoreFdKHR` idiom: `cvkAllocateImportMemoryFd`, `cvkAllocateExportMemory`, `cvkCreateExportSemaphoreWin32Handle`, and on any platform `cvkAllocateImportMemoryZirconHandle`, `cvkAllocateImportAndroidHardwareBuffer`; when one such extender serves several bases with the same verb (`VkExportMetalObjectCreateInfoEXT`) it falls back to the next form. **Constructors and everything but queries**: `<tier-0>With<D>Info`, where the tier-0 name drops its trailing version digit because a chained extender already implies the extensible form (`cvkBindImageMemoryWithPlaneInfo`, not `…Memory2With…`; a flattened sibling named after a struct keeps the struct's digit), unless the digit-less command is in the profile and is itself extensible, that is, takes an sType input of its own: then the digit is part of the name (`cvkCreateRenderPassWithFragmentDensityMapInfo` from `vkCreateRenderPass`, `cvkCreateRenderPass2WithFragmentDensityMapInfo` from `vkCreateRenderPass2`; `vkBindImageMemory` has no info struct, so `vkBindImageMemory2` drops its digit) (`cvkCreateImageViewWithUsageInfo`, `cvkAllocateMemoryWithDedicatedInfo`). **Queries** (`Get`, `Enumerate`): if the extender is a plain `Info` struct (not `CreateInfo`/`AllocateInfo`/`BeginInfo`) whose name covers the base struct's noun run, the extender is the subject of the query and the name is `cvk<Verb><ExtenderStruct>` with no command version digit (`VkImagePlaneMemoryRequirementsInfo` → `cvkGetImagePlaneMemoryRequirementsInfo`, `VkPhysicalDeviceExternalImageFormatInfo` → `cvkGetPhysicalDeviceExternalImageFormatInfo`); otherwise `D` is spliced into the base struct's noun run so that it wraps the run's unmatched remainder: first word of `D`, remaining run words, rest of `D`, then the command's tail, with the command's version digit dropped (`VkSemaphoreTypeCreateInfo` → `cvkGetPhysicalDeviceExternalSemaphoreTypeProperties`, `VkImageStencilUsageCreateInfo` → `cvkGetPhysicalDeviceImageStencilFormatUsageProperties`). The command may name the run in full or, when the run starts with the handle the base struct is about and the command is dispatched elsewhere, only its tail: `VkSurfaceFullScreenExclusiveInfoEXT` on `VkPhysicalDeviceSurfaceInfo2KHR` gives `cvkGetPhysicalDeviceSurfaceFullScreenExclusivePresentModes` from `vkGetPhysicalDeviceSurfacePresentModes2EXT` and `cvkGetDeviceGroupSurfaceFullScreenExclusivePresentModes` from `vkGetDeviceGroupSurfacePresentModes2EXT`; any other missing qualifier (`Compute` of `VkComputePipelineCreateInfo` in `vkGetPipelineIndirectMemoryRequirementsNV`) means the run is absent and the name is the `With` form (`cvkGetPipelineIndirectMemoryRequirementsWithBinaryInfo`). A collision is a generation error |
| extender member sharing a base member's name | The tokens of its type absent from the base member's type distinguish it (`VkPipelineCreateFlags2 flags` beside `VkPipelineCreateFlags flags` → `flags2`, `VkBufferUsageFlags2 usage` → `usage2`); with the same type, the extender's distinct part qualifies it (`VkImageViewUsageCreateInfo::usage` on `VkPhysicalDeviceImageFormatInfo2` → `imageViewUsage`). Parallel-array names keep a name that is already plural (`flags` → `pFlags`) |
| capacity constant for enumeration array `pFooBars` | `CVK_MAX_FOO_BARS` |

Collisions after suffix stripping are a generation error, never silently resolved.

**Declared exceptions.** `name_overrides` in the policy file maps a rule-derived name to the
name to emit instead. The generator applies it at name registration, so callers and bodies
use the final name, and it fails if an entry never matches a generated name (stale entries
are errors). Every entry is a recorded decision, never an edit. The table is currently empty.
`Enumerate` is kept: it is Vulkan's marker for the two-call protocol.

## 4. Tier 0: one function per profile command (derived)

Parameters in registry order, transformed by role:

| Role | Emitted |
|---|---|
| handle, scalar, enum, flags, POD by value | verbatim |
| single sType input struct (`I`) | **construction/recording kinds** (`CreateInfo`, `AllocateInfo`, `BeginInfo`, `BindInfo`, `SubmitInfo`, `EndInfo`): flattened, members in order, `sType` dropped, `pNext` kept with the struct member's own type (`const void* pNext`, or `void* pNext` for the registry's few inputs declared without const). **Plain `Info` structs when the command also fills an output sType struct** (`VkBufferMemoryRequirementsInfo2` with `VkMemoryRequirements2`, `VkPhysicalDeviceImageFormatInfo2` with `VkImageFormatProperties2`): tier 0 keeps the input pointer as Vulkan declares it and does only the output, and a flattened sibling `cvk<Verb><InfoStruct>` (verb token removed once from the struct name: `cvkGetBufferMemoryRequirementsInfo2`, `cvkGetPhysicalDeviceImageFormatInfo2`) builds the input and calls tier 0, so each function does one thing. A plain-Info input with no output struct (`vkGetMemoryFdKHR`, `vkGetBufferOpaqueCaptureAddress`, `vkImportSemaphoreFdKHR`) flattens at tier 0 like everything else; a pointer-keeping wrapper there would do nothing. Conveniences chain onto the flattened sibling where one exists |
| pointer to sType struct inside an `I` struct | kept as pointer (builders exist for it) |
| sType input array (`I[]`), POD array, handle array, scalar array | count + pointer verbatim |
| `pAllocator` | verbatim |
| output handle | out-pointer; asserted non-null before, `!= VK_NULL_HANDLE` after |
| output sType struct (`P`) | `void* p<Stem>Next` immediately before the out-pointer. The wrapper fills a local named after the parameter (`support`, `memory_requirements`) with `sType`/`pNext`, passes it, and copies it to the out-pointer after the call; the caller's struct is never written before the call. Each struct fill is its own block |
| output POD struct / scalar | out-pointer verbatim |
| two-call enumeration | count pointer removed; function returns `uint32_t`; array becomes `T p<Name>[CVK_MAX_<NAME>]` |
| non-`VkResult` return | passed through |

When more than one struct is filled (two `I`, or `I` and `P`), each `pNext` is `p<Stem>Next`
with `<Stem>` = the Vulkan parameter name minus its leading `p`. With exactly one filled struct
the parameter is plain `pNext`.

Result policy (derived from `successcodes`): if the only success code is `VK_SUCCESS` the
function returns `void` and `CVK_CHECK` aborts on any other value. Otherwise the function
returns `VkResult`, aborts if the value is negative (all `VK_ERROR_*` are negative), and hands
the informational code back. Enumerations abort on `VK_INCOMPLETE` because the capacity is
asserted first.

Dispatch (policy): commands in the profile's core version, and commands of extensions the
loader exports as trampolines (surface, swapchain, display, platform surfaces, debug
utilities; the list is `LOADER_LINKED` in the generator), are called directly. Every other
extension command is resolved per call: `cvkGetDeviceProcAddr` for device-level dispatch,
`cvkGetInstanceProcAddr` otherwise, both fatal if absent. The generated wrappers of
`vkGet*ProcAddr` are that infrastructure (they assert a non-null result). The function pointer
is declared with the other locals and loaded immediately before the first call, never in its
declaration. The level of a command is read from its first handle's parent chain in the
registry: device level when the chain reaches `VkDevice` (`VkQueue`, `VkCommandBuffer` via
`VkCommandPool`, `VkExternalComputeQueueNV`), instance level when it reaches only `VkInstance`
(`VkPhysicalDevice`). A resolved command whose first handle is not the level handle gains that
handle as its first parameter (`VkDevice device`, `VkInstance instance`). Reason: no global
table, no init order, and the parent is the only stateless way to resolve. The pointer local
is `function`, or `function_pointer` when a parameter is named `function`
(`vkDestroyCuFunctionNVX`).

Block-scope variables are `snake_case` and follow the Vulkan parameter they stand for: the
flattened input struct is named after its parameter minus the `p` (`pCreateInfo` →
`create_info`, `pInfo` → `info`), the enumeration count after the removed count parameter
(`pPhysicalDeviceCount` → `physical_device_count`), a chained extender after its struct
(`VkMemoryDedicatedAllocateInfo` → `memory_dedicated_allocate_info`), and resolved commands
and results are `function` and `result`. Declarations are ordered by first access in the
statements that follow.

**Registry quirks (policy).** `role_overrides` in the policy file maps `vkCommand::param` to
a role when the registry's declaration misleads the derivation: an input struct passed through
a non-const pointer is declared `I` (`vkGetExternalComputeQueueDataNV::params`,
`vkSetGpaDeviceClockModeAMD::pInfo`). Every entry is a recorded decision.

## 5. Assertions (derived from `optional` and `len`)

Input assertions are a policy switch (`input_asserts` in the policy file), currently **off**:
no precondition on parameters or flattened members is emitted. What is always emitted are
the post-call checks, which are the wrapper's own contract: a created handle is
`!= VK_NULL_HANDLE`, a resolved command pointer is `!= NULL`, and an enumeration count is
`<= CVK_MAX_*`. When the switch is on, the rules below apply.

- Non-optional handle or pointer parameter/member: `!= VK_NULL_HANDLE` / `!= NULL`.
- Array with `len=<count>`: `0 == <count> || NULL != <array>` when the array is optional,
  `NULL != <array>` otherwise.
- Non-optional enum-typed parameter with a `_MAX_ENUM` sentinel: no assertion (registry gives
  no valid range); flags are not asserted non-zero unless the registry marks them non-optional
  and the type has a `FlagBits` enum (policy: reference asserted usage flags; keep only when
  derived).
- Output pointers: `!= NULL` before the call.

## 6. Tier 1: extenders (derived)

Emission order in every file: builders first, then tier 0, flattened siblings, conveniences,
policy functions, chains, protocols.

For every sType struct in the profile (direct input, nested input, extender, output):

- **builder** `void cvkInit<Struct>(<Struct>* p, const void* pNext, <members>...)` fills
  `sType`, `pNext`, and every member. For output (returned-only) structs the builder takes
  only `pNext`.

For every (direct-input struct, in-profile extender) pair where the extender is not a
`VkPhysicalDevice*Features*` struct and the base is a single `I` parameter:

- **convenience** `cvk<Verb><Qualifier><Rest>(<tier-0 params with the extender's members
  inserted right after pNext>)` builds the extender in front of the caller's `pNext` and calls
  tier 0.

An extender member whose name collides with a tier-0 parameter name (for example
`descriptorSetCount` in `VkDescriptorSetVariableDescriptorCountAllocateInfo`) is emitted as
`<qualifier><Member>` in lower camel case (`variableDescriptorCountDescriptorSetCount`).

Stacking more than one extender is done by the caller with builders in one frame. No
multi-qualifier function is generated (policy: 2^n explosion; see `study/MEMORY.md`).

**Per-element extenders on array inputs** (`VkBindImagePlaneMemoryInfo` on
`VkBindImageMemoryInfo`) are emitted as the parallel-array form
`(handle, count, ppNexts, <extender member arrays>, <base member arrays>)`, named as the
**plural of the singular** (last word pluralized):
`cvkBindPlaneImageMemories(device, bindInfoCount, ppNexts, pPlaneAspects, pImages, pMemories, pMemoryOffsets)`.
Array parameter names pluralize the member (`memory` → `pMemories`, `imageIndex` → `pImageIndices`).
Only generated when neither the base nor the extender has pointer or array members, since
parallel arrays would otherwise nest; the caller builds such arrays with the builders. The
element array is sized by `CVK_MAX_<ArrayParam>` and the count is asserted against it. The
**singular** (count = 1) flattens one element and forwards to the plural; it is named
`cvk<Verb><D><noun run>` where the noun run is the command's own noun, depluralized and without
its version digit, and a `Cmd` verb carries its action word: `cvkBindPlaneImageMemory(device, pNext, planeAspect, image, memory, memoryOffset)`,
`cvkBindSwapchainImageMemory`, `cvkCmdBindPushDescriptorBufferHandleDescriptorBuffer`. The plural
pluralizes the last word: `cvkBindPlaneImageMemories`.

**Extenders on the output sType struct** (`VkMemoryDedicatedRequirements` on
`VkMemoryRequirements2`, `VkExternalImageFormatProperties` on `VkImageFormatProperties2`,
`VkPhysicalDeviceIDProperties` on `VkPhysicalDeviceProperties2`) are named by
**replacement** on the command's name (version digit dropped): its tail is the output
struct's noun run, and the extender's full name replaces that run, which reproduces Vulkan's
struct names with `Get` in front: `cvkGetPhysicalDeviceHostQueryResetFeatures`,
`cvkGetPhysicalDeviceIDProperties`, `cvkGetPhysicalDeviceExternalImageFormatProperties`,
`cvkGetImageMemoryDedicatedRequirements`, `cvkGetPhysicalDeviceDrmFormatModifierPropertiesList2`,
`cvkGetDescriptorSetVariableDescriptorCountLayoutSupport`. The call goes to the flattened
sibling when one exists, so the input stays flattened. Two fallbacks: `<target>With<D><Kind>`
when the command's tail is not the run, or when the replacement name is owned by a tier-0
command (section 3: `cvkGetPhysicalDeviceProperties2WithCooperativeMatrixProperties`); and
`<base>With<D>[Info]` when the extender carries no kind word at all (`Properties`,
`Requirements`, `Features`, `Support`, `Capabilities`), since an `Info`-named or bare extender
cannot stand in for the run: `cvkGetPhysicalDeviceSurfaceCapabilitiesWithImageUsageFlags2Info`,
`cvkGetPhysicalDeviceSurfaceCapabilitiesWithPresentModeCompatibility`.
The function takes `p<ExtenderStruct>Next` in place of the output chain, fills the extender's `sType`/`pNext`,
chains it into the base output struct, which stays a local, and returns exactly the struct it is
named after through the out-parameter `p<ExtenderStruct>`. An output extender with pointer
members carries caller-provided storage (`VkDrmFormatModifierPropertiesListEXT`: a count and an
array), so that parameter is in/out and seeds the local before the call (`pExternalImageFormatProperties`,
`pMemoryDedicatedRequirements`). A caller who also wants the base struct calls the base query.

## 7. Tier 1': specializations

Two derived forms and one policy form.

- **Singulars** (derived): a command with exactly one array of sType structs or handles
  (and no other array) gets a count-of-one form. A plural command name is depluralized
  (`cvkCreateComputePipeline`, `cvkFlushMappedMemoryRange`, `cvkWaitForFence`, `cvkResetFence`,
  `cvkFreeCommandBuffer`); otherwise the element struct names it with the verb removed once
  (`cvkQueueSubmitInfo`, `cvkBindImageMemoryInfo`, `cvkQueueBindSparseInfo`); when one element
  struct serves several commands of the same verb, the singular is named after the command,
  `<tier-0>Info` (`cvkCmdBeginTransformFeedback2Info`, `cvkCmdEndTransformFeedback2Info`, the
  flattened-sibling rule of section 4). An sType element is
  flattened; a handle element is passed by value and its out-array counterpart, when present,
  becomes a single out-pointer. A handle singular whose element name is taken by another
  parameter is not generated (`vkCmdExecuteCommands`).
- **Per-element extender singulars** (derived): section 6.
- **Constant substitutions** (policy): a one-step chain (section 8) whose `bind` fixes
  arguments: `cvkCmdBindComputePipeline`, `cvkCmdImageBarrier` (queue families ignored),
  `cvkCmdAcquireImageFromExternal` and `cvkCmdReleaseImageToExternal` (top or bottom of pipe,
  no access, `VK_QUEUE_FAMILY_EXTERNAL`), `cvkCreateTimestampQueryPool`, `cvkQueuePresentImage`.

Builders (`cvkInit…`) exist for every struct the caller fills, plain structs included
(`cvkInitImageSubresourceRange`, `cvkInitPushConstantRange`, `cvkInitDescriptorImageInfo`), so
chains can build the plain structs the sequences need.

## 8. Tier 2: chains (policy table over derived handle flow)

A chain is a straight-line sequence of already-emitted functions, listed in `chains` in the
policy file. **The chain's signature is computed from its steps' signatures**, never written:

- an input of a step is satisfied by an earlier output whose stem matches (`image` from
  `pImage`, `allocationSize` from `pAllocationSize`, `memoryTypeIndex` from `pMemoryTypeIndex`);
- a step's `pNext` is renamed after that step's product (`pImageNext`, `pMemoryNext`);
- every unsatisfied input becomes a chain parameter; every produced handle becomes a chain
  out-parameter; a step output routed to a `local` stays internal;
- `bind` fixes a step argument to an expression (`memoryOffset: 0`, a mask intersection);
  `set` assigns a chain output; `store`/`return` route a step's return value;
- parameter order: dispatch handles by level, then inputs in order of first appearance, then
  `pAllocator`, then outputs.

The one piece of logic the registry cannot supply, memory type selection
(`cvkGetCompatibleMemoryTypeIndex`: first type in the mask whose property flags **cover** the
required flags; `cvkGetMemoryTypeIndex`: first type in the mask whose property flags **equal**
the given flags; both return `VK_INVALID` if none, and `cvk.h` defines `VK_INVALID` as
`UINT32_MAX`), is fixed text in the generator and composes like any other function. Chains that go on to allocate carry an
`assert` step on the index, so an impossible selection is fatal at the chain, not in the helper.

Seeded chains: `cvkGetPhysicalDevice{,Compatible}MemoryTypeIndex` (memory properties, then the
exact or the covering selector), `cvkGetPhysicalDeviceExternalImage{Import,Export}Support`
(external format query → `VkBool32` for the feature bit, `pDedicatedOnly` out),
`cvkGet{Instance,PhysicalDevice}ExtensionSupport` and `cvkGetPhysicalDeviceQueueFamilyIndex`
(enumeration into a fixed-capacity local, then the fixed selector), `cvkSetupDeviceQueue`
(extension support asserted, one queue create info built, device created, queue retrieved),
`cvkCmd{Image,Buffer}QueueOwnershipBarrier` (builder, then `cvkCmdPipelineBarrier` with one
barrier), `cvkQueueSubmitCommandBuffer`, `cvkSetupCommandPool`, `cvkCreateComputePipelineLayout`,
`cvkSetupComputePipeline`, `cvkUpdate{,Image,Buffer}DescriptorSet`, `cvkGetQueryPoolResults64`,
`cvkCmdDispatchItems` (workgroup arithmetic in `bind` expressions), `cvkCmdDispatchComputeItems`,
`cvkAllocate{Image,Buffer}Memory`
(requirements → type index → assert → allocate, all in one body; no import mask, that
constraint belongs to the import protocol). Plain chains use the 1.0 requirements query: the
extensible `2` form is used only where something is chained (dedicated requirements, plane),
`cvkSetup{Image,Buffer}` (create → allocate → bind). Step forms: `bind`, `local`, `into`
(return value to a local), `store` (return value to a chain output), `return` (on a call, or
an expression with the chain's `ret` type), `set`, `assert`. An import constraint on memory types is a
parameter of the import protocol, never a name and never part of a plain allocation.

## 8b. Tier 3: protocols (authored C, folded in by the generator)

Protocols branch on runtime facts the registry does not know (`DEDICATED_ONLY`,
`requiresDedicatedAllocation`, memory object count), so their bodies are authored as real C,
but **never under `include/` or `src/`**: they live with the generator as
`scripts/protocols/cvk_protocol_<platform>.c`, and the generator folds each file into that
platform's generated pair (Doxygen block and signature into the header, definition and system
includes into the source). `posix` is a pair the registry does not name:
`VK_KHR_external_memory_fd` is platform-neutral, `dup` is not. The generator enforces that a
protocol calls only functions it produces and never a `vk*` command, so the protocols are the
acceptance test of the surface: anything they cannot express is a generator defect.

- `cvkImportWin32HandleImage` (`cvk_protocol_win32.c` → `cvk_win32.h/.c`) and `cvkImportFdImage`
  (`cvk_protocol_posix.c` → `cvk_posix.h/.c`): each the complete import chain of
  `study/MEMORY.md` §1 in one place: capability → create with the handle type declared → the
  external object's memory types (except the opaque types the specification forbids querying)
  → requirements with dedicated requirements chained → select within the external object's
  memory types → allocate with the import info (dedicated info in front when `DEDICATED_ONLY`
  or `requiresDedicatedAllocation` demands it) → bind. The external object's size is a
  parameter, never derived. The Win32 handle stays caller-owned; the descriptor is imported as
  a `dup` so the caller's stays valid. A handle-agnostic core was tried and removed: an import
  is per handle kind, and a protocol should read as the specification's list in one place.

- `cvkImportDmaBufImage` (`cvk_protocol_posix.c`): the disjoint DRM modifier import. Modifier
  properties list (in/out) -> the modifier's memory plane count -> capability with the modifier
  info chained (`DISJOINT` when several memory objects) -> create with explicit plane layouts ->
  per memory object: descriptor memory types; single object: requirements with dedicated,
  select, `dup`, allocate (dedicated when required), bind; several objects: per-plane
  requirements intersected per object, select, `dup`, allocate, bind all planes at once.

- Core protocols (`cvk_protocol.c` -> `cvk.h/.c`): `cvkWriteDebugUtilsMessage`, a
  `VK_EXT_debug_utils` callback printing to stderr; `cvkCreateDebugInstance`, which appends
  `VK_EXT_debug_utils` to the requested extensions, asserts instance support, creates the
  instance with the messenger info chained so creation itself is reported, then creates the
  messenger (the reference preferred the deprecated `VK_EXT_debug_report`; this does not);
  `cvkGetPhysicalDeviceByLuid` and `cvkGetPhysicalDeviceByPciBusInfo`, adapter identity
  matching over the enumerated physical devices (`VK_NULL_HANDLE` when none matches).

- Format and transfer protocols (`cvk_protocol.c`): the plane arithmetic on the Vulkan
  Utility Libraries format tables (`cvkGetFormatPlaneCount`, `cvkGetFormatPlaneAspect`,
  `cvkGetMemoryPlaneAspect`, `cvkGetFormatAspectMask`, subresource layers and ranges,
  `cvkCalculateFormatPlaneLayouts`, `cvkGetFormatSize`, `cvkGetBufferImageCopyRegions`) and
  the transfer sequences with barriers around the copy (`cvkCmdTransferBufferToImageQueueOwnership`,
  `cvkCmdTransferImageToBufferQueueOwnership`, `cvkCmdTransferImageToImageQueueOwnership`,
  `cvkCmdTransferBlitImage`). Their no-ownership, external-owner, compute-source, and 2D forms
  are chains binding constants or building 3D offsets and extents from 2D ones. Chains and
  protocols may compose each other: the generator emits in dependency order.

Not yet written: buffer import and export.

## 9. Documentation phrasing (derived)

- `\brief` from the verb: Create → "Creates a <noun>.", Allocate → "Allocates …",
  Get → "Retrieves …", Enumerate → "Enumerates … into a fixed-capacity array.",
  Cmd → "Records …", Destroy/Free → "Destroys/Frees …", else "Calls vk<Name>."
- `\param[in]`/`\param[out]` per parameter; `pNext` always "Optional extension chain attached
  to <struct>, or NULL"; `pAllocator` always "Optional allocation callbacks, or NULL"; output
  handles "Receives the created <type>"; enumeration arrays "Receives up to CVK_MAX_… entries".
- `\retval None` for `void`; `\return` text for values.

## 10. Exclusions

- Alias commands, `vulkansc`-only entities, commands of extensions not in the profile.
- Features structs as tier-1 qualifiers.
- Nothing else is excluded: destructors, `Cmd*` graphics commands, and every query are emitted.

## 11. Verification loop

0. `python scripts/check_generated.py` (also the CTest test `cvk-generated-files-match`)
   regenerates into a temporary directory and fails if any file under `include/` or `src/`
   differs. This is rule zero made mechanical; it runs before anything else.
1. `python scripts/generate.py` → `include/`, `src/`.
2. `clang -fsyntax-only -std=c17 -Wall -Wextra -Iexternal/Vulkan-Headers/include generated/cvk.c`
   with and without each platform define.
3. `python scripts/study_reference.py --ref generated --prefix cvk` must report tier 0 name
   fidelity and mechanical parameter fidelity of 100%.
4. Hand-written tier 3 (`study/MEMORY.md` §4.4) is compiled against the generated surface as
   the acceptance test of the taxonomy.

## 12. Open decisions

- **Adjective-first convenience names.** `cvkCreateImageWithDrmFormatModifierExplicitInfo`
  follows the registry order of `VkImageDrmFormatModifierExplicitCreateInfoEXT`;
  `…WithExplicitDrmFormatModifierInfo` reads better as English. Tabled: no rule can tell an
  adjective (`Explicit`) from a noun (`List`), and the registry order keeps the pair on one
  stem and greppable against the struct and enum names. If adopted, it goes in the
  `name_overrides` table in `cvk.policy.json` (section 3), never as an edit.
  Related idea, same table: conceptually `cvkCreateImageWithExternalMemoryInfo` could be
  encoded as `cvkCreateExternalImage`, the reference module's name, where the qualifier
  becomes an adjective on the created object instead of a chained-struct suffix. Same
  objection applies (which word is the adjective, and `Memory` is dropped), so it would be a
  declared override, not a rule.

- Whether enumerations with a `void` return should also assert a non-zero count (the
  reference does for surface formats; the spec guarantees ≥ 1 there, not in general).
- Whether `pAllocator` should be threaded at all given the policy always passes NULL.
- Builder naming for aliased structs (`VkPhysicalDeviceProperties2KHR`): emit only the core name.
- **Thin distinct parts from the `Flags2` structs.** `VkPipelineCreateFlags2CreateInfo` against
  `VkComputePipelineCreateInfo` leaves `Flags2`, so the rules give
  `cvkCreateComputePipelinesWithFlags2Info` and the per-element singular
  `cvkCreateFlags2ComputePipeline`. Rule-consistent, reads oddly; a candidate for the
  adjective-first discussion above, not an edit.
- **Handle singulars with no plural noun** (`vkGetImageOpaqueCaptureDataEXT` takes `pImages`
  but is not named in the plural) fall back to `<tier-0>Single`. Two commands today.
