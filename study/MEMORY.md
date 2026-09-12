# The memory hierarchy: Vulkan's model, the reference's encoding, and the honed form

This is the non-trivial case the wrapper has to get exactly right: allocation with dedicated,
export, and import qualifiers, image-plane binding, DRM-modifier images, and the platform
handle types. Section 1 is what Vulkan defines. Section 2 is how the reference encodes it
(tree from `scripts/study_family.py`). Section 3 is what is wrong or divergent. Section 4 is
the derived hierarchy and the naming grammar that makes it generatable.

## 1. Vulkan's memory model as the registry states it

Memory is a set of **independent axes**. Each axis is one registry relation; none of them
knows about the others.

| Axis | Registry relation | Values |
|---|---|---|
| resource | commands consuming `VkImage`/`VkBuffer` | none (raw), buffer, image, image plane (`VkImagePlaneMemoryRequirementsInfo`, `VkBindImagePlaneMemoryInfo`) |
| requirements | `vkGet{Buffer,Image}MemoryRequirements2` | `size`, `alignment`, `memoryTypeBits`; output extender `VkMemoryDedicatedRequirements` (`requires`/`prefers`) |
| import constraint | `vkGetMemory{Fd,Win32Handle}PropertiesKHR` | `memoryTypeBits` of the external object; the final mask is the AND of this and the resource's |
| memory type selection | `vkGetPhysicalDeviceMemoryProperties` | index whose `propertyFlags` ⊇ required flags and whose bit is set in the mask |
| allocation qualifiers | `structextends="VkMemoryAllocateInfo"` (8 in profile) | `Dedicated(image, buffer)`, `Export(handleTypes)`, `ImportFd(handleType, fd)`, `ImportWin32Handle(handleType, handle, name)`, `ExportWin32HandleInfo(pAttributes, dwAccess, name)`, `AllocateFlags(flags, deviceMask)`, `ImportHostPointer(handleType, pHostPointer)`, `OpaqueCaptureAddress(address)` |
| bind | `vkBind{Image,Buffer}Memory`, `vkBind{Image,Buffer}Memory2` | per-element extenders `Plane(planeAspect)`, `DeviceGroup`, `Swapchain` |
| export retrieval | `vkGetMemory{Fd,Win32Handle}KHR` | after allocation with `Export` |
| capability | `VkPhysicalDeviceExternalImageFormatInfo` → `VkExternalImageFormatProperties`; `vkGetPhysicalDeviceExternalBufferProperties` | `IMPORTABLE`, `EXPORTABLE`, `DEDICATED_ONLY`, `compatibleHandleTypes` |
| creation declaration | `VkExternalMemory{Image,Buffer}CreateInfo` | `handleTypes` the resource may be bound to |
| modifier tiling | `VkImageDrmFormatModifier{Explicit,List}CreateInfoEXT`, `VkPhysicalDeviceImageDrmFormatModifierInfoEXT` | plane layouts, per-plane memory objects |

Protocol facts the spec adds on top:

- Importing a file descriptor **transfers ownership** to the implementation on success; a caller
  that needs the fd afterwards must `dup` it first.
- `DEDICATED_ONLY` or `requiresDedicatedAllocation` forces the `Dedicated` qualifier; some
  handle types (D3D12 resource, AHardwareBuffer) require it unconditionally.
- The relation between `allocationSize` and the external object's size is **per handle type**:
  for opaque handles it must match the exporting allocation; for dma-buf it is the caller's
  size and must cover the requirements. This is why "size from requirements" is not always right.
- A disjoint (multi-plane) image binds each memory plane separately with `Plane`, and the
  requirements query is per plane.

The complete import chain, in Vulkan's own order:

```
capability   GetPhysicalDeviceImageFormatProperties2 + ExternalImageFormatInfo  -> importable, dedicatedOnly
create       CreateImage + ExternalMemoryImageCreateInfo(handleTypes) [+ DrmFormatModifierExplicit]
constraint   GetMemoryFdProperties(handleType, fd)                              -> handleTypeBits
requirements GetImageMemoryRequirements2 [+ PlaneMemoryRequirementsInfo] [+ DedicatedRequirements out]
select       memoryTypeIndex = select(handleTypeBits & requirements.memoryTypeBits, properties)
ownership    fd' = dup(fd)
allocate     AllocateMemory + ImportFd(handleType, fd') [+ Dedicated(image)]
bind         BindImageMemory | BindImageMemory2 + Plane(aspect) per memory plane
```

The export chain is the mirror: capability (`EXPORTABLE`) → create with `handleTypes` →
requirements → select → allocate with `Export(handleTypes)` [+ `Dedicated`] → bind →
`GetMemoryFd(memory, handleType)`.

## 2. How the reference encodes it

Tree (abridged; full output from `python scripts/study_family.py --match 'Memory|Import|Export'`):

```
AllocateMemory                                  tier 0   VkMemoryAllocateInfo
├ AllocateDedicatedMemory(image, buffer)        tier 1   + MemoryDedicatedAllocateInfo
│ ├ AllocateDedicatedImageMemory                tier 1'  buffer = NULL
│ │ ├ AllocateImportedDedicatedImageMemoryFd    tier 1   + ImportMemoryFdInfoKHR
│ │ └ AllocateImportedDedicatedImageMemoryHandleWin32          + ImportMemoryWin32HandleInfoKHR
│ ├ AllocateDedicatedBufferMemory               tier 3   requirements → select → dedicated   (chain, not qualifier)
│ │ └ AllocateExportableDedicatedBufferMemory   tier 1   + ExportMemoryAllocateInfo
│ └ AllocateDedicatedExternalBufferMemory       tier 3   same chain, mask param, asserts pNext != NULL
│   ├ AllocateImportedDedicatedBufferMemoryFd   tier 1   + ImportMemoryFdInfoKHR
│   └ AllocateImportedDedicatedBufferMemoryHandleWin32
├ AllocateImageMemory                           tier 3   requirements → select → allocate     (mask = UINT32_MAX)
├ AllocateBufferMemory                          tier 3   requirements → select → allocate
│ └ AllocateExportableBufferMemory              tier 1   + ExportMemoryAllocateInfo
├ AllocateExternalBufferMemory                  tier 3   chain with mask param                (unused)
├ AllocateImportedMemoryFd                      tier 1   + ImportMemoryFdInfoKHR
└ AllocateImportedMemoryHandleWin32             tier 1   + ImportMemoryWin32HandleInfoKHR

GetImageMemoryAllocationInfo(mask, properties) -> (size, index)     requirements ∧ mask → select
GetBufferMemoryAllocationInfo                                       same, via 1.0 requirements
BindImageMemories(ppNext[], memories[], offsets[]) -> BindImageMemory2
BindImagePlaneMemories(aspects[]) -> BindImageMemories                per-element Plane extender

SetupImportedImageMemoryFd            = AllocateImportedMemoryFd + BindImageMemory
SetupImportedDedicatedImageMemoryFd   = AllocateImportedDedicatedImageMemoryFd + BindImageMemory
SetupImportedImagePlaneMemoryFd       = AllocateImportedMemoryFd + BindImagePlaneMemories(1)
SetupImportedMemoryImageFd            = GetFdProps + CreateExternalImage + AllocationInfo + SetupImportedImageMemoryFd
SetupImportedDedicatedMemoryImageFd   = GetFdProps + CreateExternalImage + AllocationInfo + SetupImportedDedicatedImageMemoryFd
SetupImportedMemoryBufferFd           = GetFdProps + CreateExternalMemoryBuffer + SetupImportedDedicatedBufferMemoryFd
SetupExportedMemoryBufferFd           = SetupExportableMemoryBuffer + GetMemoryInfoFd
SetupImportedDrmFormatModifierImage   = full import chain incl. capability, dup, per-plane requirements and bind
(Win32 mirrors each Fd function; adds SetupImportedMemoryImageWithSizeHandleWin32)
```

Mapping each name onto the axes of section 1:

| Word in name | Axis it encodes | Registry word |
|---|---|---|
| `Dedicated` | allocation qualifier | `Dedicated` ✔ |
| `Exportable` | allocation qualifier | `Export` ✘ |
| `Imported…Fd` / `Imported…HandleWin32` | allocation qualifier | `ImportFd` / `ImportWin32Handle` ✘ |
| `External` (on Allocate) | import constraint mask present | none: it is a parameter, not a struct ✘ |
| `External` (on Create) | creation declaration | `ExternalMemory` ✔/✘ (buffer form says `ExternalMemory`) |
| `Image` / `Buffer` before `Memory` | resource axis, memory only | scope: memory for an existing resource |
| `Memory` before `Image` / `Buffer` | resource axis, whole chain | scope: creates the resource too |
| `Exported` | export retrieval performed | `GetMemoryFd` step |
| `WithSize` | allocationSize supplied by caller | per-handle-type size rule |

## 3. What is wrong or divergent against Vulkan

1. **Wrong: dedicated requirements are never queried.** No function chains
   `VkMemoryDedicatedRequirements` onto `VkMemoryRequirements2`. The dedicated decision is made
   by the caller's choice of function name, or by `DEDICATED_ONLY` in the DRM path only.
   Vulkan allows `requiresDedicatedAllocation` on any resource; the chain has a hole.
2. **Wrong: the simple import chains skip the capability step.** `SetupImportedMemoryImageFd`
   and the Win32 mirror create and import without `GetPhysicalDeviceExternalImageImportSupport`.
   Only `SetupImportedDrmFormatModifierImage` runs the full protocol of section 1.
3. **Wrong: fd import size is taken from image requirements.** `SetupImportedMemoryImageFd`
   allocates with `allocation_size` from the image's requirements. For opaque-fd imports of a
   Vulkan-exported allocation, `allocationSize` must equal the exporter's size, which may be
   larger. The Win32 side recognized this and added `…WithSize…`; the fd side did not.
4. **Wrong: simple import chains do not `dup` the fd.** `SetupImportedMemoryImageFd` queries
   properties with the caller's fd and then imports the same fd, transferring ownership. Only
   the DRM path duplicates. Either both must, or the contract "fd is consumed" must be stated.
5. **Divergent: one axis, two encodings.** The import-constraint mask is a positional parameter
   on the image side (`memoryTypeBits`, `UINT32_MAX` for none) and a name qualifier plus a
   `pNext != NULL` assertion on the buffer side (`AllocateExternalBufferMemory`). The
   assertion couples a parameter to an unrelated struct that the function cannot see.
6. **Divergent: word order carries scope.** `SetupImportedDedicatedMemoryImageFd` (creates the
   image) and `SetupImportedDedicatedImageMemoryFd` (memory for an existing image) differ only
   in the order of `Memory` and `Image`. Scope must be spelled, not implied by permutation.
7. **Divergent: same-level names span three tiers.** `AllocateDedicatedImageMemory` is a
   constant substitution, `AllocateDedicatedBufferMemory` is a requirements-select-allocate
   chain, `AllocateImportedMemoryFd` is a single extender. A reader cannot tell tier from name.
8. **Divergent: `SetupImportedMemoryBufferFd` is always dedicated** but does not say so;
   the image forms distinguish dedicated and non-dedicated by name.
9. **Divergent: the two platforms flatten the same registry shape differently.**
   `GetMemoryPropertiesInfoFd` returns only `memoryTypeBits`; `GetMemoryHandlePropertiesWin32`
   returns the whole `VkMemoryWin32HandlePropertiesKHR`. The structs are isomorphic.
10. **Divergent: image requirements use the 1.1 command, buffer requirements the 1.0 command.**
    `GetImageMemoryRequirements2` vs `GetBufferMemoryRequirements`. The buffer side therefore
    cannot receive the dedicated-requirements extender at all.
11. **Divergent: a DRM-specific capacity is used on a core function.** `BindImageMemories` sizes
    its scratch by `VK_MAX_DRM_FORMAT_MODIFIER_PLANES`.
12. **Divergent: combinatorial stacking is hand-picked.** Eight allocation extenders admit 256
    chains; the reference hand-writes 7 of them, each as a new function, because a tier-1
    function builds its struct on its own stack and cannot be composed with another tier-1
    function by a caller.
13. **Divergent: `AllocationInfo` names the selection step with Vulkan's allocation vocabulary.**
    `GetImageMemoryAllocationInfo` returns `(size, memoryTypeIndex)`, which is the selection
    result, not a `VkMemoryAllocateInfo`.

What the reference gets right and the honed form keeps: `Export` (allocation capability) vs
`Exported` (handle retrieved) is a real Vulkan distinction; `Plane` binding through
per-element extenders is the correct use of `vkBindImageMemory2`; the DRM path's ownership
`dup`, per-plane requirements, `DISJOINT` derivation from memory-object count, and the
`DEDICATED_ONLY` ⇒ single-object assertion are all faithful to the spec.

## 4. The honed hierarchy

### 4.1 Tier 0 (per command, flattened)

```
cvkAllocateMemory(device, pNext, allocationSize, memoryTypeIndex, pAllocator, pMemory)
cvkGetImageMemoryRequirements2(device, pNext, image, pMemoryRequirementsNext, pMemoryRequirements)
cvkGetBufferMemoryRequirements2(device, pNext, buffer, pMemoryRequirementsNext, pMemoryRequirements)
cvkBindImageMemory(device, image, memory, memoryOffset)
cvkBindImageMemory2(device, bindInfoCount, pBindInfos)
cvkGetMemoryFdProperties(device, handleType, fd, pNext, pMemoryFdProperties)
cvkGetMemoryWin32HandleProperties(device, handleType, handle, pNext, pMemoryWin32HandleProperties)
cvkGetMemoryFd(device, pNext, memory, handleType, pFd)
cvkGetMemoryWin32Handle(device, pNext, memory, handleType, pHandle)
cvkGetPhysicalDeviceImageFormatProperties2(physicalDevice, pNext, format, type, tiling, usage, flags,
                                           pImageFormatPropertiesNext, pImageFormatProperties)
```

Both platforms flatten identically because the registry shapes are identical.

### 4.2 Tier 1 (per extender): builders plus single-qualifier conveniences

Vulkan's composition mechanism is the `pNext` chain, and a chain of N structs must live in one
stack frame. So tier 1 has two generated forms per extender:

- a **builder** that only fills the struct: `cvkInitExportMemoryAllocateInfo(&info, pNext, handleTypes)`,
  `cvkInitMemoryDedicatedAllocateInfo(&info, pNext, image, buffer)`, `cvkInitImportMemoryFdInfo(&info, pNext, handleType, fd)`;
- a **single-qualifier convenience** that builds one struct and calls tier 0:
  `cvkAllocateExportMemory`, `cvkAllocateDedicatedMemory`, `cvkAllocateImportFdMemory`,
  `cvkAllocateImportWin32HandleMemory`, `cvkAllocateFlagsMemory`, …

Stacking is then the caller's, in one frame, with no combinatorial generation:

```c
VkMemoryDedicatedAllocateInfo dedicated; VkImportMemoryFdInfoKHR import;
cvkInitMemoryDedicatedAllocateInfo(&dedicated, pNext, image, VK_NULL_HANDLE);
cvkInitImportMemoryFdInfo(&import, &dedicated, handleType, fd);
cvkAllocateMemory(device, &import, size, index, pAllocator, &memory);
```

Output-side extenders are tier 1 too: `cvkGetImageMemoryRequirements2Dedicated(…, pRequires, pPrefers)`
chains `VkMemoryDedicatedRequirements` and returns its fields. Per-element extenders on
`I[]` inputs (`Plane` on `VkBindImageMemoryInfo`) get the parallel-array form the reference
already uses: `cvkBindImagePlaneMemory2(device, count, pPlaneAspects, pMemories, pOffsets)`.

Qualifier words are the extender struct name minus base words and vendor suffix, verbatim:
`Dedicated`, `Export`, `ImportFd`, `ImportWin32Handle`, `ExportWin32HandleInfo`, `Flags`,
`ImportHostPointer`, `OpaqueCaptureAddress`, `Plane`, `ExternalMemory`,
`DrmFormatModifierExplicit`, `DrmFormatModifierList`.

### 4.3 Tier 2 (chains from handle flow), with the mask as a parameter

```
cvkSelectMemoryType(physicalDevice, memoryTypeBits, requiredProperties) -> uint32_t        (policy helper)
cvkGetImageMemorySelection(physicalDevice, device, image, memoryTypeBitsMask, properties,
                           pSize, pMemoryTypeIndex, pRequiresDedicated)                   requirements+dedicated ∧ mask → select
cvkGetImagePlaneMemorySelection(…, planeAspect, …)
cvkGetBufferMemorySelection(…)
cvkAllocateImageMemory(physicalDevice, device, image, memoryTypeBitsMask, properties, pNext, pAllocator, pMemory)
cvkAllocateBufferMemory(…)                                                                  selection → AllocateMemory(pNext)
cvkSetupImage(...create args..., mask, properties, pImageNext, pMemoryNext, pAllocator, pImage, pMemory)
```

The import-constraint mask is always a parameter, never a name. Qualifiers on the allocation
are supplied through `pMemoryNext` built with tier-1 builders, so tier 2 needs no qualifier
permutations at all. `Setup<Noun>` produces the nouns it names, and only those.

### 4.4 Tier 3 (protocols), named by the spec's operation

```
cvkImportFdImage(...)        capability → create(ExternalMemory[+DrmFormatModifierExplicit]) → GetMemoryFdProperties
                             → selection(∧ handle bits, dedicated) → dup → allocate(ImportFd[+Dedicated]) → bind[Plane]
cvkImportWin32HandleImage(...)
cvkExportFdBuffer(...)       capability → create(ExternalMemory) → selection → allocate(Export[+Dedicated]) → bind → GetMemoryFd
cvkExportWin32HandleBuffer(...)
```

These are the only hand-written functions in the family. Each runs the complete chain of
section 1, including the capability query, the dedicated-requirements query, the
per-handle-type size rule, and fd ownership. The reference's `SetupImportedDrmFormatModifierImage`
is already this function for the dma-buf case; the honed form makes it the *only* shape an
import takes, so the incomplete simple chains (findings 1–4) cannot exist.

### 4.5 Naming grammar

```
cvk <Verb> <Qualifier>* <Noun>                     tier 0/1   (Verb from the command; Qualifier from extenders)
cvk Init <ExtenderStruct>                          tier 1     (builder)
cvk Get <Resource> [Plane] MemorySelection         tier 2
cvk Allocate <Resource> Memory                     tier 2
cvk Setup <Noun>                                   tier 2     (produces exactly <Noun> and its memory)
cvk <Import|Export> <HandleKind> <Resource>        tier 3     (spec operation, handle kind, resource)
```

No word ever encodes scope by position; no qualifier word exists that is not a registry
struct name; no parameter is implied by a name.
