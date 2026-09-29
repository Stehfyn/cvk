# cvk generation specification (working document)

Status: draft under experiment. Every rule here is either **derived** from a registry relation
(no choice involved) or **policy** (a choice, recorded with its reason). `scripts/generate.py`
implements the rules; `study/*.md` holds the evidence. When a rule changes, change it here
first, then the generator, then regenerate and re-run the studies.

## 1. Inputs

- `external/Vulkan-Headers/registry/vk.xml` (pinned by the submodule), and for the Vulkan SC
  profile `external/VulkanSC-Headers/registry/vk.xml` read with `api="vulkansc"`: the API
  variant's own commands and members, its feature's `depends` closure and `<remove>` blocks,
  and require blocks whose `depends` the variant satisfies.
- `cvk.policy.json`: profile (core version, extension list or `"all"`, platforms), capacity
  constants, `role_overrides` for registry quirks, the tier-1' substitution table, and the
  tier-2 chain table. The profile is `VK_VERSION_1_4` with every extension whose platform is
  none or one of the profile's platforms. `platforms: "all"` selects every platform
  category in that profile's registry, including provisional extensions. A list selects
  only the named categories. Output is independent of the generation host; categories
  with no supported entities in an API profile do not produce empty files.
  CMake's `CVK_PLATFORMS` list independently selects which generated pairs to compile
  for the target SDK and derives their protect macros from the pinned registry.

## 2. Outputs

Generated headers and sources target the C99 language dialect. They have no C++
linkage guards. Fixed-width integer types come from the Vulkan SDK and `<stdint.h>`.
Integer literals omit unsigned suffixes when the surrounding expression or destination
already supplies the required type. Keep suffixes where they affect correctness, such as
`1u << memory_type_index`, which must support bit 31 without a signed left shift.
Zero-valued flag arguments explicitly name the destination mask type, for example
`(VkAccessFlags)0`, in generated compositions and authored protocols.
Zero byte offsets in memory binding, mapping, mapped ranges, and whole-buffer descriptors
use `VK_NO_OFFSET`, defined by the generated core headers as `((VkDeviceSize)0)`.
Shader-object binding helpers derive the stage count with `VK_COUNT_OF(stages)`.
When parallel shader storage is needed, declare a sized stages array and the shader
array first, then assign their elements. A lone stages array may use an initializer.
`VK_COUNT_OF` returns a `uint32_t` element count for an array; it is not for pointer
arguments. Parallel shader arrays declare the same explicit numeric size as the stages array.
Pipeline setup helpers likewise pass `VK_COUNT_OF(shader_stages)` and
`VK_COUNT_OF(shader_groups)` when consuming those complete local arrays.
Calls to `memcpy`, `memset`, and `memmove` that discard the return value explicitly
cast it to `(void)`, in generated code and authored protocols alike.
The JSON reader parses 64-bit integers without `strtoull`/`strtoll`. Vulkan format
helpers use the pinned Vulkan-Utility-Libraries `vk_format_utils.h` as a private
implementation dependency. The SC profile omits these Vulkan-only helpers.
Core JSON numeric readers accept platform API constant names using their typed registry
values, without including native SDK headers. Platform sources expose their Vulkan
extension declarations before including Volk so its dispatch declarations are complete.

**Rule zero: the outputs are never edited by hand.** Any defect in `include/cvk/cvk.h` or
`src/cvk.c` is a defect in a rule here or in `scripts/generate.py`. Fix the rule, regenerate,
re-verify. A commit that changes the outputs without changing the generator or the policy is
wrong by definition.

- `include/cvk/cvk.h`, `src/cvk.c`: core profile, free of platform conditionals.
- `include/cvk/cvk_json.h`, `src/cvk_json.c`: JSON types, reader, generated parsers, and
  pipeline helpers, separate from the core pair. The JSON header includes `cvk/cvk.h`.
  Platform parsers use `include/cvk/<platform>/cvk_<platform>_json.h` and matching sources;
  those headers include `cvk/cvk_json.h` and the platform headers. The same layout applies
  to `scvk`. CMake builds these sources into the corresponding profile library.
- `include/scvk/scvk.h`, `src/scvk.c` (and platform pairs): the Vulkan SC profile, a standalone
  library from `scvk.policy.json` (which extends `cvk.policy.json`: api `vulkansc`, its
  registry, core `VKSC_VERSION_1_0`, every SC-supported extension, prefix `scvk` applied to
  every identifier). The same rules over another registry: a composition naming a command,
  constant, or type the profile lacks is dropped there (`missing: drop`), and a protocol file
  may declare the variant it belongs to (`Profile: vulkan` on the format protocols, whose
  utility-library tables describe formats the SC headers lack).
- `include/cvk/<platform>/cvk_<platform>.h`, `src/<platform>/cvk_<platform>.c` per registry
  platform, each including `cvk/cvk.h` and its native and Vulkan platform headers. The same
  layout applies to `scvk` and the `posix` protocol pair; sources include their headers
  as `<prefix>/<platform>/<prefix>_<platform>.h` relative to the public `include/` directory.
The core header is `<prefix>/<prefix>.h`; consumers never add a profile subdirectory
  as an include root. All generated and application includes use this namespace,
  including Python's generated C adapters. No flat forwarding headers are emitted.
- Generated implementations directly include the provider headers for their used
  imported types. The registry's `requires` relationships select codec and native
  SDK headers; fixed-width C types select `<stdint.h>`. Platform sources include
  their native Vulkan declarations and the selected profile's core header.
  These remain external headers; the owned implementation remains fully analyzed.

- Tier 3 protocols are folded into the platform pairs from `scripts/protocols/` (section 8b).
- `doc_comments` controls emitted Doxygen blocks (section 9); the shared policy disables them.

Python bindings are generated separately by `scripts/generate_python.py` from the
emitted C headers with libclang. CMake's optional `CVK_BUILD_PYTHON` target emits
`_generated.py` and C adapters into the build tree; Python outputs are never edited
by hand. The generator preserves every core and JSON function and the selected
platform declarations, verifies native record layouts at import, and passes
by-value records through pointer-taking C adapters. CVK's parameter metadata drives
Python output returns, argument defaults, and array lengths. The initial Python target requires
a native 64-bit build and excludes the separate Vulkan SC profile.

## 3. Names (derived)

| Thing | Name |
|---|---|
| tier 0 for `vkXyz[Vendor]` | `cvkXyz`; a promotable tag (`KHR`, `EXT`) is dropped, a vendor tag (`NV`, `AMD`, `QCOM`, ...) always stays (`cvkGetDisplayModeStereoPropertiesNV`); alias commands are not emitted, only their promoted target. **Canonical suffix rule:** when several in-profile commands (or structs) share a stem after suffix stripping, only the canonical one drops its suffix: lowest vendor rank (core, then `KHR`, then `EXT`, then any other vendor) when that rank is held by exactly one of them; the rest keep their suffix (`cvkCmdDrawMeshTasks` for `EXT`, `cvkCmdDrawMeshTasksNV`; `cvkInitExportMemoryAllocateInfo`, `cvkInitExportMemoryAllocateInfoNV`). Same rank held by several of the same kind: all keep the suffix. An output extender's replacement name (section 6) competes in the command group of that name, so `vkGetPhysicalDeviceExternalImageFormatPropertiesNV` keeps its suffix beside the core `VkExternalImageFormatProperties` convenience; a tie between a command and a struct at the same rank is settled by tier-0 ownership: the command keeps the plain name and the convenience takes its `With` form (`cvkGetPhysicalDeviceCooperativeMatrixProperties` is the `KHR` enumeration, `cvkGetPhysicalDeviceProperties2WithCooperativeMatrixProperties` the limits query). A plural command reserves its singular stem in the same groups (`vkCmdBuildAccelerationStructuresKHR` owns `cvkCmdBuildAccelerationStructure`, so `vkCmdBuildAccelerationStructureNV` keeps its suffix). A kept suffix always trails the whole derived name, every suffix in order of appearance (`cvkCreateImageWithExternalMemoryInfoNV`, `cvkGetGeneratedCommandsMemoryRequirementsWithTileRequirementsNVQCOM`) |
| builder for `VkAbcInfo` | `cvkInitAbcInfo` |
| single-qualifier convenience | Distinct part `D` = the extender's tokens outside the blocks that match its **base struct** (block alignment; a one-token block strictly inside the extender is kept, so `DrmFormatModifier` stays whole), minus the trailing struct-kind word (`Create`, `Allocate`, `Begin`, `Bind`, `Submit`) and `Info`/version tokens; vendor suffix dropped. Placement by verb. **Operation extenders** (`Import*`, `Export*`): the extender names the operation, so the name is `cvk<Verb><ExtenderStruct minus kind/Info>`, the `vkImportSemaphoreFdKHR` idiom: `cvkAllocateImportMemoryFd`, `cvkAllocateExportMemory`, `cvkCreateExportSemaphoreWin32Handle`, and on any platform `cvkAllocateImportMemoryZirconHandle`, `cvkAllocateImportAndroidHardwareBuffer`; when one such extender serves several bases with the same verb (`VkExportMetalObjectCreateInfoEXT`) it falls back to the next form. **Constructors and everything but queries**: `<tier-0>With<D>Info`, where the tier-0 name drops its trailing version digit because a chained extender already implies the extensible form (`cvkBindImageMemoryWithPlaneInfo`, not `â€¦Memory2Withâ€¦`; a flattened sibling named after a struct keeps the struct's digit), unless the digit-less command is in the profile and is itself extensible, that is, takes an sType input of its own: then the digit is part of the name (`cvkCreateRenderPassWithFragmentDensityMapInfo` from `vkCreateRenderPass`, `cvkCreateRenderPass2WithFragmentDensityMapInfo` from `vkCreateRenderPass2`; `vkBindImageMemory` has no info struct, so `vkBindImageMemory2` drops its digit) (`cvkCreateImageViewWithUsageInfo`, `cvkAllocateMemoryWithDedicatedInfo`). **Queries** (`Get`, `Enumerate`): if the extender is a plain `Info` struct (not `CreateInfo`/`AllocateInfo`/`BeginInfo`) whose name covers the base struct's noun run, the extender is the subject of the query and the name is `cvk<Verb><ExtenderStruct>` with no command version digit (`VkImagePlaneMemoryRequirementsInfo` â†’ `cvkGetImagePlaneMemoryRequirementsInfo`, `VkPhysicalDeviceExternalImageFormatInfo` â†’ `cvkGetPhysicalDeviceExternalImageFormatInfo`); otherwise `D` is spliced into the base struct's noun run so that it wraps the run's unmatched remainder: first word of `D`, remaining run words, rest of `D`, then the command's tail, with the command's version digit dropped (`VkSemaphoreTypeCreateInfo` â†’ `cvkGetPhysicalDeviceExternalSemaphoreTypeProperties`, `VkImageStencilUsageCreateInfo` â†’ `cvkGetPhysicalDeviceImageStencilFormatUsageProperties`). The command may name the run in full or, when the run starts with the handle the base struct is about and the command is dispatched elsewhere, only its tail: `VkSurfaceFullScreenExclusiveInfoEXT` on `VkPhysicalDeviceSurfaceInfo2KHR` gives `cvkGetPhysicalDeviceSurfaceFullScreenExclusivePresentModes` from `vkGetPhysicalDeviceSurfacePresentModes2EXT` and `cvkGetDeviceGroupSurfaceFullScreenExclusivePresentModes` from `vkGetDeviceGroupSurfacePresentModes2EXT`; any other missing qualifier (`Compute` of `VkComputePipelineCreateInfo` in `vkGetPipelineIndirectMemoryRequirementsNV`) means the run is absent and the name is the `With` form (`cvkGetPipelineIndirectMemoryRequirementsWithBinaryInfo`). A collision is a generation error |
| extender member sharing a base member's name | The tokens of its type absent from the base member's type distinguish it (`VkPipelineCreateFlags2 flags` beside `VkPipelineCreateFlags flags` â†’ `flags2`, `VkBufferUsageFlags2 usage` â†’ `usage2`); with the same type, the extender's distinct part qualifies it (`VkImageViewUsageCreateInfo::usage` on `VkPhysicalDeviceImageFormatInfo2` â†’ `imageViewUsage`). Parallel-array names keep a name that is already plural (`flags` â†’ `pFlags`) |
| capacity constant for enumeration array `pFooBars` | `CVK_MAX_FOO_BARS` |

Collisions after suffix stripping are a generation error, never silently resolved.

**Declared exceptions.** `name_overrides` in the policy file maps a rule-derived name to the
name to emit instead. The generator applies it at name registration, so callers and bodies
use the final name, and it fails if an entry never matches a generated name (stale entries
are errors). Every entry is a recorded decision, never an edit. The table is currently empty.
`Enumerate` is kept: it is Vulkan's marker for the two-call protocol.

## 4. Tier 0: one function per profile command

The plain cvk command name makes the nominal operation first-class. The policy's
`command_forms` table can bind Vulkan parameters to constants and replace an input
pointer with a named value parameter (`by_value`). These forms call Vulkan directly.
`cvkCmdSetScissor(commandBuffer, scissor)` and `cvkCmdSetViewport(commandBuffer, viewport)`
take one value and set slot zero, passing `0, 1, &value` to Vulkan. Their `2D` and combined
helpers use the same names without `First`. Call Vulkan directly for arbitrary array
ranges; cvk does not add a passthrough sibling for these commands.

Other parameters remain in registry order, transformed by role:

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
| two-call enumeration | returns `void`, writing the result through its count pointer; array becomes `T p<Name>[CVK_MAX_<NAME>]`. The count is assigned zero immediately before the count query. A separate local retains the queried capacity, bounds preparation, and is checked against the returned count after the fill |
| non-`VkResult` return | passed through |

When more than one struct is filled (two `I`, or `I` and `P`), each `pNext` is `p<Stem>Next`
with `<Stem>` = the Vulkan parameter name minus its leading `p`. With exactly one filled struct
the parameter is plain `pNext`.

Result policy (derived from `successcodes`): if the only success code is `VK_SUCCESS` the
function returns `void` and `CVK_CHECK` aborts on any other value. Otherwise the function
returns `VkResult`, aborts if the value is negative (all `VK_ERROR_*` are negative), and hands
the informational code back. Enumerations abort on `VK_INCOMPLETE` because the capacity is
asserted first.

Enumeration capacity and result count remain distinct even when the native API uses
one in/out pointer for both. The queried capacity bounds every prepared output-chain
element. The fill may reduce the count; its final count must not exceed that saved
capacity before any caller consumes it. This follows the input-capacity/output-count
contract of [vkGetPhysicalDeviceQueueFamilyProperties](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceQueueFamilyProperties.html)
and applies to the generated enumeration family, including extender arrays.

Dispatch (policy): every wrapper calls its `vkCommand` entry point directly. The Vulkan
profile includes `<volk.h>` privately in its implementation files and uses Volk's global
function pointers. Public headers include `<vulkan/vulkan_core.h>` or
`<vulkan/vulkan_sc_core.h>` and expose no Volk declarations. The application calls
`volkInitialize()` before instance creation and `volkLoadInstance(instance)` afterwards;
it may then use `volkLoadDevice(device)` to load device-specific pointers. Required
entry points must be loaded before their wrappers are called. cvk performs no loading,
caching, invalidation, or implicit device selection. It adds no parent device/instance
parameters for dispatch. Reloading Volk's globals is the application's responsibility;
`volkLoadDevice` selects one device and is not a per-device table interface.
The CMake target links Volk and uses the pinned Vulkan headers, with the platform defines
matching the generated platform sources. Do not link the Vulkan loader library alongside
Volk's global symbols. The separate Vulkan SC profile retains its native headers and calls
SC entry points directly; its application supplies the SC implementation, not standard Volk.
The explicit `cvkGet*ProcAddr` wrappers remain available and check their returned pointer.

Block-scope variables are `snake_case` and follow the Vulkan parameter they stand for: the
flattened input struct is named after its parameter minus the `p` (`pCreateInfo` â†’
`create_info`, `pInfo` â†’ `info`), the enumeration count after the removed count parameter
(`pPhysicalDeviceCount` â†’ `physical_device_count`), a chained extender after its role
(`VkPipelineCreationFeedbackCreateInfo` â†’ `create_info`, `VkMemoryDedicatedAllocateInfo`
â†’ `allocate_info`, output properties â†’ `properties`). Extender arrays use plural names;
collisions with parameters or other locals use `ext_` and, if needed, a numeric suffix. Explicit procedure
queries and command results use `function` and `result`. Declarations are ordered by first access in the
statements that follow.

**Recoverable errors (policy).** Vulkan reports some conditions the caller is expected to
handle as error codes: a support query's `VK_ERROR_FORMAT_NOT_SUPPORTED`,
`VK_ERROR_IMAGE_USAGE_NOT_SUPPORTED_KHR` and `VK_ERROR_VIDEO_PROFILE_*_NOT_SUPPORTED_KHR`,
a swapchain's `VK_ERROR_OUT_OF_DATE_KHR`, `VK_ERROR_SURFACE_LOST_KHR` and
`VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT`. The policy's `recoverable_errors` lists them.
A command whose error codes include one returns `VkResult` under its own name, like a command
with informational success codes: the wrapper asserts the result is a success code or one of
the command's recoverable errors, copies its outputs only on success, and returns it. A chain
reads it (`into: result`) and folds it into a `VkBool32`
(`cvkGetPhysicalDeviceVideoProfileSupport`, `cvkGetPhysicalDeviceExternalImage{Import,Export}Support`
report VK_FALSE for an unsupported format instead of aborting). Enumerations keep aborting: a
count has no room for a result. A chain step returning `VkResult` must route it (`return`,
`into`, `store`): a discarded result is a generation error, which is how the pipeline setups
came to return the compile result.

**Registry quirks (policy).** `role_overrides` in the policy file maps `vkCommand::param` to
a role when the registry's declaration misleads the derivation: an input struct passed through
a non-const pointer is declared `I` (`vkGetExternalComputeQueueDataNV::params`,
`vkSetGpaDeviceClockModeAMD::pInfo`). Every entry is a recorded decision.

## 5. Assertions (derived from `optional` and `len`)

Input assertions are a policy switch (`input_asserts` in the policy file), currently **off**:
no precondition on parameters or flattened members is emitted. What is always emitted are
the post-call checks, which are the wrapper's own contract: a created handle is
`!= VK_NULL_HANDLE`, an explicit procedure-query result is `!= NULL`, and an enumeration count is
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

**Union builders.** A union has one builder per member, `cvkInit<Union><Member>`, each
setting that member: `cvkInitClearValueColor`, `cvkInitClearColorValueFloat32`,
`cvkInitDeviceOrHostAddressDeviceAddress`, `cvkInitAccelerationStructureGeometryDataTriangles`.
Chains build union values through them, never through a raw assignment.

**Initialization style.** The boolean policy option `designated_initializers` (default
true) selects C99 designated initialization in struct and union builders and generated
wrappers, including flattened inputs, extenders, and elements of local arrays. Scalar and
pointer members use `(*pInfo) = (VkType)` followed by an Allman-braced initializer.
The generator restores initializer braces after clang-format, without formatter directives.
A blank line separates function calls from adjacent assignments or initialized declarations,
including checked, void-cast, returned and assigned calls. Calls whose results are assigned
stand apart from output copies, so their assignment spacing does not depend on nearby names
or the line limit. This applies to all generated sources and both initialization styles;
consecutive assignments without calls stay together. `cvkAlignDeviceSize` (and its SC
counterpart) is treated as arithmetic within an assignment run, so related size and stride
calculations remain contiguous and aligned.
A return statement is separated from a preceding statement or completed block by a blank
line. A return that is the first statement in its block does not gain a leading blank line.
Assertion groups are followed by a blank line before the next statement, including loops
and calls. Consecutive assertions stay together; no blank line is added before a closing brace.
Do not insert a blank line before an assertion checking the preceding operation's result.
Enumeration loop indices are declared with the function's other locals, without an extra
scope around the loop.
An adjacent local declaration and designated assignment are combined into one initialized
declaration. Intervening statements or declarations are not reordered.
Generated initializers never include a trailing comma; clang-format is configured not to add one.
Fixed arrays are copied
into a designated-initialized temporary before assigning it to `*pInfo`, so an array
argument may refer to the destination's existing storage. Returned-only builders and
updates to caller-provided output storage keep their `sType`/`pNext` assignments and leave
output members untouched. Wrappers take caller-provided counts and pointers as explicit parameters when
preparing output extension structures; the destination struct is output-only. Signatures and
wrapper composition are unchanged. The SC profile inherits the shared setting.

**Commands with several sType inputs** (`vkCmdBeginRenderPass2` with a render pass begin info
and a subpass begin info) get a convenience per extender of each input, on that input's own
`p<Stem>Next` (`cvkCmdBeginRenderPass2WithAttachmentInfo`, `cvkCmdBeginRenderPass2WithSampleLocationsInfo`).

**Extenders on the singular of an array-input command** (`VkPipelineRenderingCreateInfo` on
`VkGraphicsPipelineCreateInfo`, whose command takes an array): `<singular>With<D>Info` on the
singular (`cvkCreateGraphicsPipelineWithRenderingInfo`, `cvkCreateComputePipelineWithFlags2Info`,
`cvkCreateRayTracingPipelineWithBinaryInfo`). Two extenders of one base whose distinct parts
coincide (`VkPipelineLibraryCreateInfoKHR` and `VkGraphicsPipelineLibraryCreateInfoEXT` both
leave `Library`) each take their whole struct name instead
(`cvkCreateGraphicsPipelineWithPipelineLibraryInfo`, `cvkCreateGraphicsPipelineWithGraphicsPipelineLibraryInfo`).
`shared_extender_members` reuses a base scalar parameter when policy records a required
equality. `VkPresentTimesInfoGOOGLE::swapchainCount` shares `VkPresentInfoKHR`'s count
as required by `VUID-VkPresentTimesInfoGOOGLE-swapchainCount-01247`.
Retained vendor tags are set aside while removing trailing struct-kind, `Info`, and
version tokens, then restored at the end of the derived name. Thus `VkPresentTimesInfoGOOGLE`
produces `cvkQueuePresentWithTimesInfoGOOGLE`, with exactly one `Info`.

An extender member colliding with a base member is disambiguated as in section 3, vendor tags
excluded (`VkGraphicsPipelineLibraryFlagsEXT flags` beside `VkPipelineCreateFlags flags` â†’
`flagsGraphicsLibrary`).

**Per-element extenders on array inputs** (`VkBindImagePlaneMemoryInfo` on
`VkBindImageMemoryInfo`) are emitted as the parallel-array form
`(handle, count, ppNexts, <extender member arrays>, <base member arrays>)`, named as the
**plural of the singular** (last word pluralized):
`cvkBindPlaneImageMemories(device, bindInfoCount, ppNexts, pPlaneAspects, pImages, pMemories, pMemoryOffsets)`.
Array parameter names pluralize the member (`memory` â†’ `pMemories`, `imageIndex` â†’ `pImageIndices`).
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
The function takes `pNext` in place of the output chain when it is the only chain parameter,
or `p<ExtenderStruct>Next` when other chain parameters need to be distinguished. It fills the extender's `sType`/`pNext`,
chains it into the base output struct, which stays a local, and returns exactly the struct it is
named after through the out-parameter `p<ExtenderStruct>`. An output extender with pointer
members carries caller-provided storage (`VkDrmFormatModifierPropertiesListEXT`: a count and an
array). Its members are explicit input parameters immediately after the extension chain,
using the same collision rules as input extenders. The wrapper builds a local from those
parameters and copies the result to the output struct after the call; it never reads the
output destination. For example, the shader-abort-message wrapper takes `messageDataSize`
and `pMessageData` explicitly and retains `pDeviceFaultShaderAbortMessageInfo` to receive
the returned size. A caller who also wants the base struct calls the base query.

**Extenders on the element struct of an enumeration** (`VkQueueFamilyVideoPropertiesKHR` on
`VkQueueFamilyProperties2`) follow the same replacement rule on the enumeration's name
(`cvkGetPhysicalDeviceQueueFamilyVideoProperties`, `cvkGetPhysicalDeviceQueueFamilyGlobalPriorityProperties`)
and retain the enumeration's count and base-array parameters. The caller supplies the
base array as contiguous native workspace, followed by the extender output array
`p<ExtenderStruct>[CVK_MAX_<Base>]`. Both arrays must hold the declared capacity and be
disjoint. Every active base element is initialized and chained to its corresponding
extender before the fill call; the count is written through the count output. Base
storage is never a hidden local array, heap allocation, global or TLS object. Callers
with a persistent query owner may retain both arrays there. This is a source/API change
for output-array conveniences; callers must supply the added base-array argument.
The video queue-family selector's policy chain explicitly owns its smaller bounded
base and video arrays rather than hiding storage in the called query. The run must match the enumeration's
name exactly: an enumeration naming its subject in the plural (`SurfaceFormats2` for
`VkSurfaceFormat2KHR`) takes the With form on the tier-0 name
(`cvkGetPhysicalDeviceSurfaceFormats2WithImageCompressionProperties`), since replacing the run
would drop the subject.

Instance and device extension-support compositions likewise require the caller's
`VkExtensionProperties pProperties[CVK_MAX_PROPERTIES]` workspace. Their debug-instance,
PCI-device lookup and device-queue construction compositions pass that workspace through;
they must not hide another fixed-capacity extension array in a deeper call frame.
The parameter is scratch output, reused between native enumeration calls. This retains
the full configured enumeration capacity without heap allocation, globals or large
implicit stack frames. Public callers of these CVK compositions must supply the new
workspace argument; gfxlib's retained public signatures are unaffected.

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
- **Control operations** (policy): `vkCmdControlVideoCodingKHR` is one command for several
  operations selected by a flag; a substitution binding one flag is named by the operation,
  not by `Control`: `cvkCmdResetVideoCoding`, `cvkCmdSetVideoEncodeRateControl`,
  `cvkCmdSetVideoEncodeQualityLevel` (the last two on the tier-1 forms carrying the info).
- **Constant substitutions** (policy): a one-step chain (section 8) whose `bind` fixes
  arguments: `cvkCmdBindComputePipeline`, `cvkCmdImageBarrier` (queue families ignored),
  `cvkCmdAcquireImageFromExternal` and `cvkCmdReleaseImageToExternal` (top or bottom of pipe,
  no access, `VK_QUEUE_FAMILY_EXTERNAL`), `cvkCreateTimestampQueryPool`, `cvkQueuePresentImage`.

Builders (`cvkInitâ€¦`) exist for every struct the caller fills, plain structs included
(`cvkInitImageSubresourceRange`, `cvkInitPushConstantRange`, `cvkInitDescriptorImageInfo`), so
chains can build the plain structs the sequences need.

## 8. Tier 2: chains (policy table over derived handle flow)

A chain is a straight-line sequence of already-emitted functions, listed in `chains` in the
policy file. **The chain's signature is computed from its steps' signatures**, never written:

- an input of a step is satisfied by an earlier output whose stem matches (`image` from
  `pImage`, `allocationSize` from `pAllocationSize`, `memoryTypeIndex` from `pMemoryTypeIndex`);
- a step's plain `pNext` is renamed after that step's product (`pImageNext`, `pMemoryNext`),
  under the chain's own name for it when the step's out-parameter is bound to a chain output
  (`pCommandBuffers` bound to `pCommandBuffer` gives `pCommandBufferNext`); an already
  qualified `p<X>Next` keeps its name, so `pDeviceNext` and `pQueueNext` never merge;
- every unsatisfied input becomes a chain parameter; every produced handle becomes a chain
  out-parameter; a step output routed to a `local` stays internal;
- `bind` fixes a step argument to an expression (`memoryOffset: 0`, a mask intersection);
  `set` assigns a chain output; `store`/`return` route a step's return value; `declare` adds a
  local with an initializer (`float blend_constants[4] = { 0.0f, ... }`); a `local` of the form
  `name[i]` is an element of a chain-local array sized by its highest index (`shader_stages[1]`);
- `check: true` wraps a `VkResult`-returning call in `CVK_CHECK` when only `VK_SUCCESS`
  is accepted. It replaces a result temporary followed by a success assertion and cannot
  be combined with another return-value route;
- two steps wanting one name for different types (`VkShaderModuleCreateFlags flags` and
  `VkPipelineCreateFlags flags`) is a generation error: the policy binds one of them to a
  declared input (`shaderModuleFlags`, `pipelineLayoutFlags`), so the unqualified name stays
  with the chain's product;
- `assign` writes a statement on a local (`shaders[0] = vertexShader`), for arrays whose
  initializers are parameters;
- declared outputs join the parameter list where a step first binds, stores, or sets them;
  a chain over a builder is a builder, so its `pInfo` leads and its `pNext` follows;
- parameter order: dispatch handles by level, then inputs in order of first appearance, then
  `pAllocator`, then outputs.

Flag constants in bound mask arguments are cast to the registry's underlying `VkFlags` or
`VkFlags64` type before bitwise operations; the result is cast to the parameter's flags
typedef. A single flag constant gets only the destination typedef cast, for example
`(VkPipelineStageFlags)VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT`. Aliases are preserved in the
destination cast. Ordinary enum arguments and already-typed mask variables pass through unchanged.
Compound mask expressions break after the destination cast's opening parenthesis, with
one operand per line, indented one level from the cast. Binary operators end each
continuation line; the closing parenthesis follows the final operand. This layout is
restored after clang-format without formatter-disable comments.
Authored protocol arguments, mask assignments and bit tests also cast flag-bit enums and
enum-valued helper results explicitly to the corresponding flags typedef. This includes
compound assignments and conditional expressions; enum-to-enum uses keep their enum type.

The one piece of logic the registry cannot supply, memory type selection
(`cvkGetCompatibleMemoryTypeIndex`: first type in the mask whose property flags **cover** the
required flags; `cvkGetMemoryTypeIndex`: first type in the mask whose property flags **equal**
the given flags; both return `VK_INVALID` if none, and `cvk.h` defines `VK_INVALID` as
`UINT32_MAX`), is fixed text in the generator and composes like any other function. Chains that go on to allocate carry an
`assert` step on the index, so an impossible selection is fatal at the chain, not in the helper.

Seeded chains: `cvkGetPhysicalDevice{,Compatible}MemoryTypeIndex` (memory properties, then the
exact or the covering selector), `cvkGetPhysicalDeviceExternalImage{Import,Export}Support`
(external format query â†’ `VkBool32` for the feature bit, `pDedicatedOnly` out),
`cvkGet{Instance,PhysicalDevice}ExtensionSupport` (enumeration into caller-owned
workspace, then the fixed selector) and `cvkGetPhysicalDeviceQueueFamilyIndex`
(enumeration into a bounded queue-family local, then the fixed selector), `cvkSetupDeviceQueue`
(extension support asserted, one queue create info built, device created, queue retrieved),
`cvkCmd{Image,Buffer}QueueOwnershipBarrier` (builder, then `cvkCmdPipelineBarrier` with one
barrier), `cvkQueueSubmitCommandBuffer`, `cvkSetupCommandPool`, `cvkCreateComputePipelineLayout`,
`cvkSetupComputePipeline`, `cvkUpdate{,Image,Buffer}DescriptorSet`, `cvkGetQueryPoolResults64`,
`cvkCmdDispatchItems` (workgroup arithmetic in `bind` expressions), `cvkCmdDispatchComputeItems`,
`cvkAllocate{Image,Buffer}Memory`
(requirements â†’ type index â†’ assert â†’ allocate, all in one body; no import mask, that
constraint belongs to the import protocol). Plain chains use the 1.0 requirements query: the
extensible `2` form is used only where something is chained (dedicated requirements, plane),
`cvkSetup{Image,Buffer}` (create â†’ allocate â†’ bind). The second sweep over the widened
profile added, by family: synchronization (`cvkCreateSignaledFence`, `cvkWaitFor{Fence,AllFences,AnyFence}Indefinitely`
forms, `cvkCreateTimelineSemaphore`, `cvkWaitSemaphore[Indefinitely]`); command buffers
(`cvkBeginOneTimeCommandBuffer`, `cvkAllocate{,Primary,Secondary}CommandBuffer[s]`,
`cvkCreate{Resettable,Transient}CommandPool`, `cvkSetupPrimaryCommandPool`,
`cvkQueueSubmitCommandBufferAndWaitIdle`); memory (`cvkMapWholeMemory`,
`cvk{Flush,Invalidate}WholeMappedMemory`, `cvkAllocate{DeviceLocal,HostVisible}{Image,Buffer}Memory`,
`cvkSetup{DeviceLocal,HostVisible}{Image,Buffer}`); images (`cvkCreateImage2D`, `cvkSetupImage2D`,
`cvkSetupDeviceLocalImage2D`, `cvkCreateImageView2D` with identity components,
`cvkCreateWholeImageView2D` over the remaining levels and layers); physical device
(`cvkGetPhysicalDeviceLimits`, `cvkGetPhysicalDeviceTimestampPeriod`, `cvkGetPhysicalDeviceApiVersion`,
`cvkGetPhysicalDeviceByIndex`, `cvkGetPhysicalDevice{Compute,Graphics,Transfer}QueueFamilyIndex`,
`cvkSetup{Compute,Graphics,Transfer}DeviceQueue`); descriptors (`cvkCreateSingleBindingDescriptorSetLayout`,
`cvkCreateSingleTypeDescriptorPool`, `cvkAllocateDescriptorSet`, `cvkUpdate{StorageImage,SampledImage,CombinedImageSampler,StorageBuffer,UniformBuffer}DescriptorSet`
and the `Whole` buffer forms); queries (`cvkCmdWrite{TopOfPipe,BottomOfPipe}Timestamp`); barriers
(`cvkCmdMemoryBarrier`, and the synchronization2 mirror `cvkCmd{Memory,Image,Buffer}Barrier2`,
`cvkCmd{Image,Buffer}QueueOwnershipBarrier2`, `cvkCmd{Acquire,Release}{Image,Buffer}{From,To}External2`,
each taking `pDependencyNext` for the `VkDependencyInfo` chain beside the barrier's `pNext`);
rendering (`cvkCmdBeginColorRendering`, `cvkCreateSingleAttachmentFramebuffer`,
`cvkCreateColorRenderPass`: one attachment, one graphics subpass, no dependencies);
swapchain (`cvkAcquireNextImageIndefinitely`); external buffers
(`cvkGetPhysicalDeviceExternalBuffer{Import,Export}Support`, the buffer mirror of the image
support chains). A `Whole` form binds offset 0 and `VK_WHOLE_SIZE` or the remaining levels
and layers; an `Indefinitely` form binds `UINT64_MAX`; a `Single<Noun>` form binds a count of
one over a local built by a builder step. Step forms: `bind`, `local`, `into`
(return value to a local), `store` (return value to a chain output), `return` (on a call, or
an expression with the chain's `ret` type), `set`, `assert`. An import constraint on memory types is a
parameter of the import protocol, never a name and never part of a plain allocation.

## 8a. Families: compute, graphics, video

One vocabulary across the three families, so that each level of one family has its
counterpart in the others. `<Kind>` is `Compute`, `Graphics`, or `Video`.

| Level | Compute | Graphics | Video |
|---|---|---|---|
| queue family | `cvkGetPhysicalDeviceComputeQueueFamilyIndex` (flags) | `cvkGetPhysicalDeviceGraphicsQueueFamilyIndex` | `cvkGetPhysicalDeviceVideo{Decode,Encode}QueueFamilyIndex` (flags); `cvkGetPhysicalDeviceVideoQueueFamilyIndex(requiredOperations)` on the video properties of every family (output-array extender + `cvkGetVideoQueueFamilyIndex`, policy selector) |
| device | `cvkSetupComputeDeviceQueue` | `cvkSetupGraphicsDeviceQueue` | `cvkSetupVideoDeviceQueue(requiredOperations)` |
| capability | features/properties output extenders | same | `cvkGetPhysicalDeviceVideoCapabilities` (+ `cvkGetPhysicalDeviceVideo{Decode,Encode,DecodeH264,...}Capabilities` output extenders), `cvkGetPhysicalDeviceVideoProfileSupport` (recoverable result â†’ `VkBool32`), `cvkGetPhysicalDeviceVideoFormatPropertiesByIndex` |
| program | `cvkCreateShaderModule`; shader object `cvkCreateComputeShader` | `cvkCreateShaderModule`; `cvkCreate{Vertex,Fragment}Shader` | session parameters: `cvkCreateVideoSessionParameters[With<Codec>Info]`, `cvkUpdateVideoSessionParameters[With<Codec>AddInfo]` |
| layout | `cvkCreateComputePipelineLayout` (one range, `COMPUTE_BIT`) | `cvkCreateGraphicsPipelineLayout` (one range, `ALL_GRAPHICS`) | â€” |
| fixed-function state | â€” | `cvkInit<Adjective>Pipeline<State>CreateInfo`: the builder with the canonical constants bound (`Empty` vertex input, `TriangleList`/`TriangleStrip`/`PointList`/`LineList` input assembly, `Single` viewport, `Fill` rasterization, `SampleCount` multisample, `Disabled`/`DepthTest` depth stencil, `Opaque`/`AlphaBlend` attachment, `SingleAttachment` color blend). Arrays stay caller-owned: a builder never points into chain storage | picture resources and reference slots: builders only, storage is the caller's |
| object | `cvkSetupComputePipeline` (module, stage, layout, pipeline) | `cvkSetupGraphicsPipeline` (two modules, two stages, layout, pipeline over caller-provided state) | `cvkSetupVideoSession` (session, then `cvkAllocateVideoSessionMemory`: one allocation and bind per requirement, a protocol), `cvkSetup{Buffer,Image}WithVideoProfileListInfo` |
| bind | `cvkCmdBindComputePipeline`, `cvkCmdBindComputeDescriptorSets`, `cvkCmdPushComputeConstants`, `cvkCmdBindComputeShader` | `cvkCmdBindGraphicsPipeline`, `cvkCmdBindGraphicsDescriptorSets`, `cvkCmdPushGraphicsConstants`, `cvkCmdSet{Viewport,Scissor}[2D]`, `cvkCmdSetViewportAndScissor2D` | `cvkCmdBeginVideoCoding[With<RateControl>Info]`, `cvkCmdControlVideoCoding`, `cvkCmdControlResetVideoCoding`, `cvkCmdEndVideoCoding` |
| work | `cvkCmdDispatchItems`, `cvkCmdDispatchComputeItems` (bind, bind, push, dispatch) and 2D forms | `cvkCmdDrawGraphicsVertices`, `cvkCmdDrawGraphicsIndices` (bind, bind, push, draw) | `cvkCmdDecodeVideo[With<Codec>PictureInfo]`, `cvkCmdEncodeVideo[With<Codec>PictureInfo]` |
| queries | `cvkCreateTimestampQueryPool` | same | `cvkCreateResultStatusQueryPool`, `cvkGetQueryResultStatus`; `cvkCreateVideoEncode[Bitstream]FeedbackQueryPool`, `cvkGetVideoEncodeBitstreamFeedback` (offset and bytes written, status returned) |
| resources | `cvkSetup{DeviceLocal,HostVisible}Buffer` | attachments: `cvkCreateWholeImageView2D`, `cvkCreateColorRenderPass`, `cvkCreateSingleAttachmentFramebuffer` | `cvkSetupVideoImage` (2D, one level, one sample, optimal, undefined; the array layers are the pictures) and `cvkSetupVideo{DecodeDpb,DecodeDst,EncodeSrc,EncodeDpb}Image`; `cvkSetupVideo{DecodeSrc,EncodeDst}Buffer`; `cvkCreateLayerImageView2D` (one picture of the DPB array); `cvkAlignDeviceSize` for the bitstream alignments |
| DPB | â€” | â€” | `cvkInitOriginVideoPictureResourceInfo`, `cvkInitInactiveVideoReferenceSlotInfo` (slot -1: a resource in scope that is not yet a reference), `cvkGetInactiveVideoReferenceSlotIndex` over the caller's activity flags. The DPB itself is caller state: which slot a picture takes and when it is retired is the codec's decision, encoded in the codec picture infos the tier-1 forms carry |
| loop | `cvkQueueSubmitCommandBufferAndWaitIdle` | `cvkCmdBeginColorRendering` â€¦ `cvkCmdEndRendering` | one picture per coding scope: `cvkCmd{Decode,Encode}VideoPicture` (begin with the bound slots, decode or encode, end) and `cvkCmd{Decode,Encode}InitialVideoPicture` (reset first; encode also sets the rate control); `cvkCmdSetVideoEncode{RateControl,QualityLevel}` between scopes |
| libraries | â€” | `cvkCreateGraphicsPipelineLibrary` (`LIBRARY_BIT` added), one per subset taking only the state that subset consumes: `cvkCreate{VertexInputInterface,PreRasterizationShaders,FragmentShader,FragmentOutputInterface}GraphicsPipelineLibrary`, and `cvkCreateLinkedGraphicsPipeline` (no state of its own); `cvkSetupGraphicsPipelineWithRenderingInfo` for dynamic rendering | â€” |

The `<Kind>` word binds the bind point (`VK_PIPELINE_BIND_POINT_<KIND>`), the stage set
(`COMPUTE_BIT`, `ALL_GRAPHICS`), or the queue flag; a codec is never a `<Kind>`, it is the
extender's distinct part (`H264`, `AV1`), which tier 1 already names. The video family has no
layout or fixed-function level: its state lives in the session and its parameters objects,
which the object level creates. Left to the caller: the codec picture infos (tier 1 carries
them) and the DPB activity flags.

The same levels for ray tracing and mesh shading. Mesh shading is the graphics family with
other stages: its bind point is graphics, so it shares the graphics bind forms and only its
stage set, layout, setup, work, and shader-object forms are its own.

| Level | Ray tracing | Mesh shading |
|---|---|---|
| capability | `cvkGetPhysicalDeviceRayTracingPipelineProperties`, `cvkGetPhysicalDeviceAccelerationStructureProperties` (output extenders); `cvkGetPhysicalDeviceRayTracingShaderGroupHandleStride` | `cvkGetPhysicalDeviceMeshShaderProperties` |
| program | shader modules; groups `cvkInit{General,TrianglesHit,ProceduralHit}RayTracingShaderGroupCreateInfo` | shader modules; shader objects `cvkCreate{Task,Mesh}Shader` |
| layout | `cvkCreateRayTracingPipelineLayout` (every ray tracing stage) | `cvkCreateMeshShadingPipelineLayout` (task, mesh, fragment) |
| resources | `cvkSetup{DeviceAddress,HostVisibleDeviceAddress}Buffer` (allocate flags chained), `cvkSetup{AccelerationStructure,Scratch,ShaderBindingTable,AccelerationStructureBuildInputBuffer}Buffer`; geometries `cvkInit{Triangles,Aabbs,Instances}AccelerationStructureGeometry` over the union builders, `cvkInitDeviceâ€¦Data` forms from device addresses | â€” |
| object | `cvkCreate{BottomLevel,TopLevel}AccelerationStructure`; `cvkSetup[{BottomLevel,TopLevel}]AccelerationStructure` (sizes, storage, structure, aligned scratch: a protocol); `cvkSetupRayTracingPipeline` (raygen, miss, closest hit; groups in table order); `cvkSetupRayTracingShaderBindingTable` (handles written into a mapped table, regions at the base alignment: a protocol) | `cvkSetup[Task]MeshShadingPipeline` (no vertex input or input assembly state) |
| bind | `cvkCmdBindRayTracingPipeline`, `cvkCmdBindRayTracingDescriptorSets`, `cvkCmdPushRayTracingConstants`, `cvkUpdateAccelerationStructureDescriptorSet` | graphics bind forms; `cvkCmdPushMeshShadingConstants`; `cvkCmdBind[Task]MeshShadingShaders` |
| work | `cvkCmdBuild[Device[{BottomLevel,TopLevel}]]AccelerationStructure`, `cvkCmdAccelerationStructureBuildBarrier`; `cvkCmdTraceRays2D`, `cvkCmdTraceRayTracingRays[2D]` (bind, bind, push, trace) | `cvkCmdDrawMeshTaskItems[2D]` (the dispatch arithmetic), `cvkCmdDrawMeshShadingTasks[2D]` (bind, bind, push, draw) |

Filled gaps, all families: shader-object binding (`cvkCmdBindGraphicsShaders`), union builders,
conveniences on commands with several sType inputs.

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

- `cvkImportWin32HandleImage` (`cvk_protocol_win32.c` â†’ `cvk_win32.h/.c`) and `cvkImportFdImage`
  (`cvk_protocol_posix.c` â†’ `cvk_posix.h/.c`): each the complete import chain of
  `study/MEMORY.md` Â§1 in one place: capability â†’ create with the handle type declared â†’ the
  external object's memory types (except the opaque types the specification forbids querying)
  â†’ requirements with dedicated requirements chained â†’ select within the external object's
  memory types â†’ allocate with the import info (dedicated info in front when `DEDICATED_ONLY`
  or `requiresDedicatedAllocation` demands it) â†’ bind. The external object's size is a
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

- Acceleration structures (`cvkSetupAccelerationStructure`, core): build sizes for a device
  build, storage buffer and structure, scratch buffer with room to align its address to the
  scratch offset alignment; the build is a chain over the same geometries.
- Shader binding table (`cvkSetupRayTracingShaderBindingTable`, core): raygen group zero,
  then miss, hit, callable; handle stride from the handle alignment, regions from the base
  alignment, handles fetched one group at a time straight into the mapped table; the raygen
  region's size equals its stride.
- Memory access (`cvkWriteMemory`, `cvkReadMemory`, `cvkWriteCoherentMemory`,
  `cvkReadCoherentMemory`, core): map, copy, flush or invalidate the range, unmap, with every
  parameter of the map and of the mapped range exposed; the coherent forms skip the flush.
- Video session memory (`cvkAllocateVideoSessionMemory`, core): the session form of
  `cvkAllocateImageMemory`; one compatible type, allocation, and bind per reported
  requirement, all binds in one call, count returned.
- Buffer import (`cvkImportFdBuffer`, `cvkImportWin32HandleBuffer`): the buffer form of the
  image import protocols, on `cvkGetPhysicalDeviceExternalBufferImportSupport`,
  `cvkCreateBufferWithExternalMemoryInfo`, `cvkGetBufferMemoryRequirementsInfo2` with
  dedicated requirements, and `cvkBindBufferMemory`.
- Export (`cvkExportFd{Image,Buffer}`, `cvkExportWin32Handle{Image,Buffer}`): capability
  (exportable; DEDICATED_ONLY or requiresDedicatedAllocation forces dedicated), creation with
  the handle type declared, requirements, selection, `cvkAllocateExportMemory` with the
  dedicated info chained when required, binding, then `cvkGetMemoryFd` or
  `cvkGetMemoryWin32Handle`. The Win32 forms take the export handle info's attributes, access
  and name, chained only for the opaque Win32 and D3D handle types, the only ones the
  specification permits it for. The exported descriptor or handle is owned by the caller.

## 8c. JSON (derived parsers over an authored reader)

The Vulkan SC pipeline JSON (the pipeline cache compiler's input) is the reference format:
enumerants and bits by name, flags as names joined by `|` or `"0"`, `VkBool32` as `VK_TRUE`
and `VK_FALSE`, pointers as arrays or objects, `"NULL"` or `null` for none, extension chains
as objects with an `sType`, handles as the file's object ids, strings and fixed arrays as
themselves. Three pieces:

- **Reader** (`cvk_protocol_json.c`, with the types of `cvk_protocol_json.h` folded into
  `cvk_json.h`): `cvkParseJsonDocument` reads a document into a caller-provided value array with no
  allocation; members and elements are found with `cvkGetJsonMember` and `cvkGetJsonElement`;
  numbers, booleans, and strings are read by kind; `cvkArena` holds what the parsers build;
  `cvkResolveJsonHandle` maps an id through the context's resolver, or keeps it.
- **Parsers** (derived): `cvkParseJson<Type>` for every enumeration and flag type the
  profile's headers define (name tables from the registry, gated by API variant, version
  closure, extension, and `protect`), and for every structure and union of the profile
  (`memset`, `sType`, then each member by kind; arrays and pointed-to structures allocated
  from the arena; data and function pointers and platform types stay zero);
  `cvkParseJsonStructureChain` dispatches an extension chain object on its `sType`.
- **Pipeline** (`cvk_protocol_json_pipeline.c`): `cvkParsePipelineJson` reads a pipeline file
  (render pass by version, id-keyed descriptor set layouts, samplers, Ycbcr conversions, the
  pipeline layout, the pipeline, shader file names, enabled extensions, the pipeline UUID);
  `cvkCreatePipelineJsonObjects` creates them in dependency order, resolving the ids in
  immutable samplers, set layouts, layout and render pass, and shader modules through the
  caller's resolver; `cvkCreatePipelineFromJson` does both from the text. The SC form
  `scvkCreateOfflinePipelineFromJson` chains the file's UUID in a `VkPipelineOfflineCreateInfo`
  with the caller's match control and pool entry size, modules `VK_NULL_HANDLE`.
  `tests/pipeline_json_test.c` parses `tests/cube.pc.json` (the Khronos SC cube sample's
  pipeline) under CTest.

## 9. Documentation phrasing (derived)

`doc_comments` is a boolean policy option (default true, currently false in
`cvk.policy.json`, inherited by Vulkan SC). When false, generated declarations and
definitions omit Doxygen blocks, including copied protocol documentation. Ordinary
implementation comments and generated-file banners remain. When true, the rules below apply.

`single_line_prototypes` is a boolean policy option (default false, currently true in
the shared policy). When true, each function declaration occupies exactly one physical
line. The generator brackets declaration sections with clang-format off/on directives,
so the column limit cannot split prototypes during generation or later formatting.
Function definitions keep their ordinary layout. Both options are
generation-policy settings and require no CMake configuration.

- `\brief` from the verb: Create â†’ "Creates a <noun>.", Allocate â†’ "Allocates â€¦",
  Get â†’ "Retrieves â€¦", Enumerate â†’ "Enumerates â€¦ into a fixed-capacity array.",
  Cmd â†’ "Records â€¦", Destroy/Free â†’ "Destroys/Frees â€¦", else "Calls vk<Name>."
- `\param[in]`/`\param[out]` per parameter; `pNext` always "Optional extension chain attached
  to <struct>, or NULL"; `pAllocator` always "Optional allocation callbacks, or NULL"; output
  handles "Receives the created <type>"; enumeration arrays "Receives up to CVK_MAX_â€¦ entries".
- `\retval None` for `void`; `\return` text for values.

## 10. Exclusions

- Alias commands, `vulkansc`-only entities, commands of extensions not in the profile.
- Features structs as tier-1 qualifiers.
- Nothing else is excluded: destructors, `Cmd*` graphics commands, and every query are emitted.

## 11. Verification loop

0. `python scripts/check_generated.py` (also the CTest test `cvk-generated-files-match`)
   regenerates into a temporary directory and fails if any file under `include/` or `src/`
   differs. This is rule zero made mechanical; it runs before anything else.
1. `python scripts/generate.py` â†’ `include/`, `src/`.
2. `clang -fsyntax-only -std=c99 -Wall -Wextra -Iinclude -Iexternal/volk -isystem external/Vulkan-Headers/include -isystem external/Vulkan-Utility-Libraries/include src/cvk.c`
   with and without each platform define.
3. `python scripts/study_reference.py --ref generated --prefix cvk` must report tier 0 name
   fidelity and mechanical parameter fidelity of 100%.
4. Hand-written tier 3 (`study/MEMORY.md` Â§4.4) is compiled against the generated surface as
   the acceptance test of the taxonomy.

## 12. Open decisions

- **Adjective-first convenience names.** `cvkCreateImageWithDrmFormatModifierExplicitInfo`
  follows the registry order of `VkImageDrmFormatModifierExplicitCreateInfoEXT`;
  `â€¦WithExplicitDrmFormatModifierInfo` reads better as English. Tabled: no rule can tell an
  adjective (`Explicit`) from a noun (`List`), and the registry order keeps the pair on one
  stem and greppable against the struct and enum names. If adopted, it goes in the
  `name_overrides` table in `cvk.policy.json` (section 3), never as an edit.
  Related idea, same table: conceptually `cvkCreateImageWithExternalMemoryInfo` could be
  encoded as `cvkCreateExternalImage`, the reference module's name, where the qualifier
  becomes an adjective on the created object instead of a chained-struct suffix. Same
  objection applies (which word is the adjective, and `Memory` is dropped), so it would be a
  declared override, not a rule.

- Whether enumerations with a `void` return should also assert a non-zero count (the
  reference does for surface formats; the spec guarantees â‰¥ 1 there, not in general).
- Whether `pAllocator` should be threaded at all given the policy always passes NULL.
- Builder naming for aliased structs (`VkPhysicalDeviceProperties2KHR`): emit only the core name.
- **Thin distinct parts from the `Flags2` structs.** `VkPipelineCreateFlags2CreateInfo` against
  `VkComputePipelineCreateInfo` leaves `Flags2`, so the rules give
  `cvkCreateComputePipelinesWithFlags2Info` and the per-element singular
  `cvkCreateFlags2ComputePipeline`. Rule-consistent, reads oddly; a candidate for the
  adjective-first discussion above, not an edit.
- **Handle singulars with no plural noun** (`vkGetImageOpaqueCaptureDataEXT` takes `pImages`
  but is not named in the plural) fall back to `<tier-0>Single`. Two commands today.
