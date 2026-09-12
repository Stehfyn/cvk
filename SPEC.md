# cvk generation specification (working document)

Status: draft under experiment. Every rule here is either **derived** from a registry relation
(no choice involved) or **policy** (a choice, recorded with its reason). `scripts/generate.py`
implements the rules; `study/*.md` holds the evidence. When a rule changes, change it here
first, then the generator, then regenerate and re-run the studies.

## 1. Inputs

- `external/Vulkan-Headers/registry/vk.xml` (pinned by the submodule).
- `cvk.policy.json`: profile (core version, extension list, platforms), capacity constants,
  the tier-1' substitution table, and the tier-2 chain table.

## 2. Outputs

**Rule zero: the outputs are never edited by hand.** Any defect in `include/cvk.h` or
`src/cvk.c` is a defect in a rule here or in `scripts/generate.py`. Fix the rule, regenerate,
re-verify. A commit that changes the outputs without changing the generator or the policy is
wrong by definition.

- `generated/cvk.h`, `generated/cvk.c`: core profile. Platform-specific commands are inside
  the same `#ifdef VK_USE_PLATFORM_*` guards `vulkan.h` uses, so the file set is one pair.
- Every emitted function has a full Doxygen block with fixed phrasing (section 9).

## 3. Names (derived)

| Thing | Name |
|---|---|
| tier 0 for `vkXyz[Vendor]` | `cvkXyz`; vendor suffix dropped; alias commands are not emitted, only their promoted target |
| builder for `VkAbcInfo` | `cvkInitAbcInfo` |
| single-qualifier convenience | Distinct part `D` = the extender's tokens outside the blocks that match its **base struct** (block alignment; a one-token block strictly inside the extender is kept, so `DrmFormatModifier` stays whole), minus the trailing struct-kind word (`Create`, `Allocate`, `Begin`, `Bind`, `Submit`) and `Info`/version tokens; vendor suffix dropped. Placement by verb. **Constructors and everything but queries**: `<tier-0>With<D>Info` (`cvkCreateImageViewWithUsageInfo`, `cvkAllocateMemoryWithDedicatedInfo`). **Queries** (`Get`, `Enumerate`): if the extender is a plain `Info` struct (not `CreateInfo`/`AllocateInfo`/`BeginInfo`) whose name covers the base struct's noun run, the extender is the subject of the query and the name is `cvk<Verb><ExtenderStruct>` with no command version digit (`VkImagePlaneMemoryRequirementsInfo` → `cvkGetImagePlaneMemoryRequirementsInfo`, `VkPhysicalDeviceExternalImageFormatInfo` → `cvkGetPhysicalDeviceExternalImageFormatInfo`); otherwise `D` is spliced into the base struct's noun run so that it wraps the run's unmatched remainder: first word of `D`, remaining run words, rest of `D`, then the command's tail, with the command's version digit dropped (`VkSemaphoreTypeCreateInfo` → `cvkGetPhysicalDeviceExternalSemaphoreTypeProperties`, `VkImageStencilUsageCreateInfo` → `cvkGetPhysicalDeviceImageStencilFormatUsageProperties`). A collision is a generation error |
| qualifier for extender `E` of base `B` (used only to disambiguate colliding parameter names) | CamelCase tokens of `E` minus the multiset of tokens of `B`, vendor suffix dropped, in `E`'s order |
| capacity constant for enumeration array `pFooBars` | `CVK_MAX_FOO_BARS` |

Collisions after suffix stripping are a generation error, never silently resolved.
`Enumerate` is kept: it is Vulkan's marker for the two-call protocol.

## 4. Tier 0: one function per profile command (derived)

Parameters in registry order, transformed by role:

| Role | Emitted |
|---|---|
| handle, scalar, enum, flags, POD by value | verbatim |
| single sType input struct (`I`) | **construction/recording kinds** (`CreateInfo`, `AllocateInfo`, `BeginInfo`, `BindInfo`, `SubmitInfo`, `EndInfo`): flattened, members in order, `sType` dropped, `pNext` kept as `const void* pNext`. **Plain `Info` structs** (query subjects such as `VkBufferMemoryRequirementsInfo2`, `VkMemoryGetFdInfoKHR`): tier 0 keeps the pointer as Vulkan declares it, and a flattened sibling `cvk<Verb><InfoStruct>` (verb token removed once from the struct name: `cvkGetBufferMemoryRequirementsInfo2`, `cvkGetMemoryFdInfo`, `cvkImportSemaphoreFdInfo`) builds the struct and calls tier 0. When Vulkan reuses one Info struct for several commands of the same verb (`VkBufferDeviceAddressInfo`), the sibling is named after the command instead: `<tier-0>Info` (`cvkGetBufferOpaqueCaptureAddressInfo`). Conveniences chain onto the flattened sibling |
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
`vkGet*ProcAddr` are that infrastructure (they assert a non-null result). A resolved command
whose dispatch handle is a `VkQueue` or `VkCommandBuffer` gains `VkDevice device` as its first
parameter; one dispatched on a `VkPhysicalDevice` gains `VkInstance instance`. Reason: no
global table, no init order, and the parent is the only stateless way to resolve.

Block-scope variables are `snake_case` and follow the Vulkan parameter they stand for: the
flattened input struct is named after its parameter minus the `p` (`pCreateInfo` →
`create_info`, `pInfo` → `info`), the enumeration count after the removed count parameter
(`pPhysicalDeviceCount` → `physical_device_count`), a chained extender after its struct
(`VkMemoryDedicatedAllocateInfo` → `memory_dedicated_allocate_info`), and resolved commands
and results are `function` and `result`.

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

Per-element extenders on `I[]` inputs (for example `VkBindImagePlaneMemoryInfo` on
`VkBindImageMemoryInfo`) are emitted as the parallel-array form: `cvk<Verb><Qualifier><Rest>2(
handle, count, <extender member arrays>..., <base member arrays>...)`. (Not yet generated.)

## 7. Tier 1': specializations (policy table)

Each row of `substitutions` in the policy file names a target function, a suffix or word
change, and the fixed arguments. Generated as a forwarding call. Examples that the memory
study justified: `count = 1` forms, `VK_QUEUE_FAMILY_IGNORED` barriers, 2D from 3D.
(Not yet generated.)

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

Seeded chains: `cvkGetPhysicalDeviceMemoryTypeIndex`, `cvkAllocate{Image,Buffer}Memory`
(requirements → type index → assert → allocate, all in one body; no import mask, that
constraint belongs to the import protocol). Plain chains use the 1.0 requirements query: the
extensible `2` form is used only where something is chained (dedicated requirements, plane),
`cvkSetup{Image,Buffer}` (create → allocate → bind). Step forms: `bind`, `local`, `into`
(return value to a local), `store` (return value to a chain output), `return` (on a call, or
an expression with the chain's `ret` type), `set`, `assert`. An import constraint on memory types is a
parameter of the import protocol, never a name and never part of a plain allocation.

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
  stem and greppable against the struct and enum names. If adopted, it goes in as a
  `name_overrides` table in `cvk.policy.json` applied by the generator, never as an edit.
  Related idea, same table: conceptually `cvkCreateImageWithExternalMemoryInfo` could be
  encoded as `cvkCreateExternalImage`, the reference module's name, where the qualifier
  becomes an adjective on the created object instead of a chained-struct suffix. Same
  objection applies (which word is the adjective, and `Memory` is dropped), so it would be a
  declared override, not a rule.

- Whether enumerations with a `void` return should also assert a non-zero count (the
  reference does for surface formats; the spec guarantees ≥ 1 there, not in general).
- Whether `pAllocator` should be threaded at all given the policy always passes NULL.
- Builder naming for aliased structs (`VkPhysicalDeviceProperties2KHR`): emit only the core name.
