# Graphics-library extraction

The user requires a `gfxlib` with `Gfx_` public names, using
`C:/dev/vulkan-va-zero-copy` as the starting implementation. Displays, native
windows, presentation/storage surfaces and their platform mechanics belong below
CastDemo's UI policy. This is part of the complete goal, not an exemption for GUI
dependencies or a claim that a Linux application already exists.

The user's initial direction fixes the migration boundary: copy the
existing gfx library and refactor the internals; **do not modify retained public
gfxlib function signatures**. The subsequently authorized wrapper retirement is
recorded below. Extend behavior through enums. Existing signature types,
including the adapter enumeration's `size_t` parameters, therefore remain at that
public boundary; internal code still follows the complete goal conventions.

The copied implementation **must use CVK internally**. Prerequisite queries and
CVK construction contracts must replace raw Vulkan effects and duplicate Vulkan
construction/checking wrappers. A post-call result check is not a substitute for
the required preflight. This requirement does not authorize changing public
`Gfx_` function signatures.

The user also authorizes their `Stehfyn/cdx` library as an external submodule for
replacing raw DirectX calls inside gfxlib. The repository already pins it at
`external/cdx` (`d10ee0af2a7412062f2260d3a53c8df28351d66d`). Use its existing
queries and construction contracts; retain the pinned upstream implementation.
The external-warning exception covers cdx and Vulkan core/utility/video/SC headers,
but does not exempt owned gfxlib or CVK code. Dependency allocation and lifetime
requirements still require evidence.

The later instruction explicitly authorizes removal of `gfx_vulkan` and
`gfx_vaapi`: replace their call sites directly with CVK and cva, retaining cdx for
DirectX operations. This does **not** authorize removing gfxlib. It supersedes
signature preservation for those low-level wrapper families, including their
platform variants. The higher-level `Gfx_` display, adapter, image, renderer,
surface, swapchain and encoder signatures remain frozen. Retain graphics policy
and interop composition inside those higher-level modules rather than reproducing
another Vulkan or VA-API wrapper layer.

`external/cva` pins `8560e4db994354a500523f9fded09e8f81fe725b`. Its upstream
checkout declares libva in `.gitmodules` but omits the gitlink. The root therefore
pins `external/libva` at cva's declared registry revision
`9b1db46a3a11b6152a4fa2c3b3f1e93da2cb5edf`; `cmake/Cva.cmake` prepares the required
header layout and runs the unchanged upstream generator against that revision.
`cmake/cva-status-policy.json` uses cva's supported recoverable-error policy for
the optional surface diagnostic query. `scripts/configure_cva.py` verifies that
this changes exactly one function: `cvaQuerySurfaceError` retains its arguments
and returns all 32 specified libva error statuses to its caller. Synchronization,
construction and other function bodies retain upstream behavior. Generated headers
and sources are kept together in the existing build directory; neither external
checkout is modified. Status values outside the pinned specification still follow
cva's unexpected-error behavior. The policy is not evidence of CRT-free provider
execution or allocation-free diagnostic queries.

CastDemo must consume this library canonically through its public `Gfx_` API and
an actual link dependency. Native display/window, surface and graphics mechanisms
move behind that boundary; application/media/UI policy remains in CastDemo.
Parallel application implementations, private-header access, include aliases and
an unconsumed target do not meet the migration requirement.

## Reference implementation inspected

Reference checkout HEAD: `f14fec46dbdd9f63330719526b00a84083d0cc8b`.
The working files, including local changes, are the implementation reference.

| Boundary | Existing evidence | Transfer into CastDemo |
| --- | --- | --- |
| Display model | `include/gfx/gfx_display.h`: `Gfx_Display_Kind_t`, `Gfx_Display_CreateInfo_t`, `Gfx_Display_ProcessEvents` | Preserve windowed/direct/headless distinctions and explicit extent/event state |
| Native window ownership | `src/gfx/gfx_display_internal.h`: `nativeDisplay`, `nativeWindow`, `nativeEvent`, `running` | Native handles belong to the backend and must have explicit lifetime and owner relationships |
| Windows desktop | `src/gfx/win32/gfx_display.c`: `CreateWindowedDisplay`, `ProcessDisplayEvents` | Reuse window/class/event mechanics, adapting to generic Win32 names, `HANDLE_MSG`, caller-owned state and bounded dispatch |
| Linux desktop | `src/gfx/linux/gfx_display.c`: Xlib display/window and close-event handling | Provide a build-selected Linux backend for the same public interface, including native dependency lifecycle evidence |
| Surface roles | `include/gfx/gfx_surface.h`: `GFX_SURFACE_KIND_PRESENT`, `GFX_SURFACE_KIND_STORAGE` | Keep presentation targets distinct from shared image storage and its read/write ownership |
| Surface ownership | `Gfx_Surface_t`: `writePending`, `readPending`, `acquiredImage`, read/write views | Retain explicit handoff semantics; turn waits and retries into bounded progress consistent with the goal |
| Platform selection | `src/CMakeLists.txt` and `cmake/GfxTargets.cmake` select `gfx_display.c`, `gfx_surface.c`, adapter and Vulkan platform sources | Select implementations in the build; avoid target-selection branches in application logic |

For example, the reference display's state is explicit:

```c
bool_t             running;
uintptr_t          nativeDisplay;
uintptr_t          nativeWindow;
uintptr_t          nativeEvent;
```

Those fields are in `src/gfx/gfx_display_internal.h:38`. The reference embeds its
native window in the display implementation; the inspected public interface does
not yet define a separate `Gfx_Window_` module.

## Required adaptation and evidence

- Preserve all CastDemo controls, dark styling, input, DPI/accessibility behavior,
  media actions and shutdown semantics while moving native mechanics below the
  graphics interface. A headless smoke test does not cover the interactive GUI.
- Initialize retained display/window/surface owners and capacities at application
  startup. The reference's `Gfx_Display_Create` calls `OS_CALLOC`; copying it does
  not prove the no-post-initialization-allocation requirement for native providers.
- The reference drains native event queues and its Linux window mapping waits for
  `MapNotify`. CastDemo needs bounded dispatch/progress and explicit cancellation;
  those loops cannot simply be copied as realtime evidence.
- Retain CVK queries and construction contracts for Vulkan effects. The reference's
  raw `vkCreateXlibSurfaceKHR` wrapper is not authorization to bypass CVK here.
- Adapt the reference to the explicit goal conventions: `OS_`, `CRT_`, `Gfx_`, no
  globals, generic Win32 character handling, named predicates, initialization-first
  APIs, single final returns, full contracts and all strict analysis requirements.
- Instrument the library and each executable backend with the applicable sanitizer
  matrix. Valgrind/MSan/TSan evidence from a Linux backend does not establish the
  same result for Win32, external drivers or GPU execution. Track each boundary.

The original import contained 39 graphics headers, implementations and shaders,
including both platform backends. `gfxlib/UPSTREAM.json` retains their original
hashes and all 336 original public declarations across 15 headers as provenance.
`python scripts/check_gfx_api.py` protects the 23 retained higher-level signatures
across nine headers and now rejects reintroduction of either retired wrapper family
or direct Vulkan/libva calls in owned gfxlib source and headers. Enum extensions
do not alter the signature comparison. The reference checkout is unchanged.

Current migration evidence (not completion):

- All 313 original low-level declarations have been retired through direct
  dependency calls, integration into their owning module, or removal of unreachable
  code. `gfx_vulkan.c/.h` and `gfx_vaapi.c/.h` are deleted, together with the standalone
  Win32/Linux Vulkan helper source/header pairs. The platform Vulkan and VA wrapper
  headers and their four native implementation files are also deleted. Native sharing
  construction now belongs to the selected `gfx_image_platform.c` implementation,
  reached through one private image-owner initialization interface. Its output is
  the existing image owner, rather than a generic collection of native output pointers.
- VA synchronization, optional diagnostic queries and image memory mapping now
  call cva directly from the image owner. Diagnostic failures in the specified
  domain remain separate from the primary synchronization result. Traversal ends
  at the native sentinel or after 256 records, with an explicit limit message.
  The imported logging dependency remains unresolved; moving this existing policy
  into the owner does not establish a working logging backend or a bounded native
  synchronization operation. Private image
  layout code caches validated active rows during construction, preserving the
  reference format set and native plane ordering. Packed transfers exclude native
  padding, combine contiguous rows, and use a real `void*` mapping variable instead
  of casting typed pointers to `void**`. Capacity checks precede synchronization and
  mapping. The cached geometry is retained in the private startup owner, with no
  change to a public function signature.
- The configured Win32 cva library builds in Release (`build/cva-configured-build.log`).
  Its seven generated files pass byte-for-byte regeneration checks. Both Win32 and
  DRM configurations pass the function-change/status-domain checks; the DRM output
  was generated in memory without a native Linux compilation. These are generation
  and library-build checks, not native diagnostic runtime tests. Required runtime
  cases include successful and failed synchronization combined with each specified
  query failure, NULL outputs, empty/short/maximum diagnostic chains, record-limit
  reporting and preservation of the primary result. Driver-owned diagnostic records
  and the cva runtime still need allocation, CRT and lifetime review.
- `build/gfx-image-layout-analysis.log` records native compilation and MSVC analysis
  passing for the private layout module, followed by four clang-tidy unsafe-buffer
  diagnostics at calculated row pointers. The strict gate remains failing with no
  warning suppression. The consuming image unit still stops at missing `os/os_malloc.h`
  (`build/gfx-image-analysis.log`), so its integration is not compile validated.
  No C fixture bypassed these failures. Required native cases remain packed and pitched
  copies, each supported format, byte-rounded packed rows, nonzero plane offsets,
  exact final-row extents, padding sentinels, maximum dimensions/overflow, invalid
  plane counts/subsampling/pitches/extents, and undersized read/write capacities.
  Existing blocking synchronization, native mapping behavior and provider allocations
  remain unresolved; the bounded copy loops alone do not establish real-time playback.
- The encoder now queries surface pixel formats with cva and maps its picture/slice
  buffers directly, copying their bounded payloads through `CRT_Memory_Copy`. Native
  export surfaces compose fixed cva attribute schemas instead of the generic 128-entry
  export wrapper. Win32 and DRM export operations use cva's platform interfaces.
- Encoder IQ matrices and packed SPS/PPS buffers now use cva directly. Rate-control
  and frame-rate parameters are typed native packets assembled by the encoder;
  compile-time extent/offset checks express their required native ABI layout.
  Their former byte-array casts, helper interfaces and CRT copies were removed.
- The remaining five encoder wrappers are now retired: picture, sequence, picture-
  parameter and slice-parameter initialization, plus coded-surface sizing. Native
  pictures and slices use cva directly; encoder-specific parameter policy consumes
  its retained configuration, request and decoded-picture state. Sequence and
  picture bitfield records remain owned encoder construction because pinned cva
  does not provide those initializers. No replacement generic VA wrapper API was
  introduced. Native frame numbers use retained decoding-order values, while field
  counts use display-order indices and are checked before signed conversion.
- Complete native reference lists are initialized once per submission. Caller
  weight spans are copied only for active references into retained slice arrays;
  disabled weights and unused native entries remain initialized. The pinned cva
  slice initializer copies every input array before publishing its result, allowing
  those retained arrays to supply its inputs. Shared H.264 derivation supplies PPS
  default reference counts and slice override comparisons. Coded-buffer sizing
  includes the existing SPS/PPS reserve before native encoder construction; it is
  still an estimate, with oversized coded output handled as a completion failure.
  These are reviewed source changes, not executed encoder validation.
- Win32 image and buffer imports now call `cvkImportWin32HandleImage` and
  `cvkImportWin32HandleBuffer` directly. Their former nine private Vulkan helper
  functions are removed. CVK handles import capability, memory requirements,
  compatible memory types, dedicated allocation and binding. The shared-buffer
  path queries native fence import support before creating its resources. The
  host-copy fallback is retained, as are caller-owned Win32 handles and persistent
  transfer mappings. Windows VA imports use typed cva attribute constructors;
  re-export replaces the caller's input handle only after obtaining an export.
- CVK's authoritative Win32 protocol template now accounts for Vulkan ignoring
  allocationSize on D3D texture/resource imports. Those imports use queried memory
  requirements; heap and opaque imports retain the supplied allocation extent and
  its existing minimum-size assertion. This follows
  [VkMemoryAllocateInfo](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryAllocateInfo.html).
  The generator emits the required core-header includes and correct Windows header
  casing. Generated-file reproducibility and all eleven generator-option tests pass
  (`build/generated-current-check.log`, `build/gfx-generator-tests.log`). These checks do
  not execute native memory imports. Native regression cases still required include
  zero/small ignored resource sizes, insufficient heap extents, dedicated requirements,
  unsupported image/buffer/fence imports and retained handle ownership.
- The Win32 image implementation and updated CVK Win32 source pass native compilation
  and MSVC analysis, then fail clang-tidy (`build/gfx-win32-image-analysis.log`,
  `build/cvk-win32-import-analysis.log`). The current image invocation reports 45
  short-identifier diagnostics and one array-bound constant diagnostic in owned CVK
  headers, with no diagnostics in the image implementation itself. The CVK source
  additionally retains include and parameter-interface diagnostics. The gates remain
  failing; no diagnostic was suppressed and no native fixture bypassed them.
  Linux sharing construction was migrated with the existing import behavior, including
  descriptor duplication and modifier-specific memory planes; its native build and
  remaining private CVK composition helpers still require review.
- Encoder status uses a zero-timeout cva buffer poll, with a surface-readiness
  query for drivers returning UNIMPLEMENTED. Busy submissions remain pending.
  The complete mapped chain is inspected before destination writes: termination,
  nonempty payloads, zero bit offsets, native error flags and the startup-allocated
  coded-buffer extent are checked within a 256-segment bound. Undersized destinations
  receive the required byte count and retain the submission for retry. Failed
  output is discarded, reports UINT64_MAX through codedSize and requires an IDR.
  This distinguishes failure from a pending poll without changing a function
  signature; callers must handle the documented output states. These source changes remove silent
  truncation and an explicit indefinite wait; provider timing, allocations and
  failure recovery are not yet verified.
- Renderer and conversion descriptor schemas now use typed CVK bindings and pool
  records, with layout-support queries before resource creation. Timestamp support
  is queried before renderer construction. Native memory interop uses CVK allocation
  chains, memory-type queries and batched plane binding. This is not evidence that
  every construction prerequisite or failure path has been completed.
- CVK enumeration destinations now use its declared capacities. VA surface-attribute
  queries provide cva's 128-entry destination. Native boolean outputs use `VkBool32`.
  The fixed NV12 conversion layout is derived once before allocating ring resources.
- Shared direct-display policy belongs to `gfx_surface.c`. Its private owner reuses
  bounded display/mode/plane query storage instead of a 45 KB stack frame. The native
  surface constructors retain that workspace in their existing owner allocation;
  adapting the imported allocator and proving startup-only ownership remain open.
- The Win32 adapter uses cdx's DXGI/D3D12 interfaces. cdxgi, cdx12 and cva compile
  successfully in the existing Release build directory with external warnings
  disabled. This does not establish CRT-free linking or dependency lifecycle safety.
- `scripts/check_gfx_calls.py` verifies declaration presence and argument counts
  for 239 calls against the pinned and configured headers. It is a structural check, not C type checking,
  runtime validation, prerequisite-query evidence or proof of correct argument order.
- `scripts/check_cast_build_policy.py` verifies the generated owned/external warning
  boundary, including external Vulkan core/video/utility/SC headers. Owned checks
  remain enabled: 45 owned and 31 external C targets are checked. Third-party warning
  exclusion does not hide owned-code failures.
- `gfxlib/CMakeLists.txt` now defines the owned `gfxlib` target and `gfx::gfx` alias,
  explicitly selecting the native sources and linking CVK/cva/Volk/cdx. Configuration
  succeeds in `build/windows-msvc-2026`. Array counts and the used low-bit masks follow
  the `C:/dev/xml` utility header. The H.264 support module is now wired through
  `cast-formats`; compilation still fails on the imported allocator, while H.264
  strict analysis, shader/resource and provider-runtime integration
  also remain open. See `build/gfx-configure.log` and `build/gfx-native-compile.log`.
- Selected direct-display and Win32 Vulkan/VA interop translation units reach clang-tidy after
  native compilation/analysis. Their strict checks still fail on owned buffer-access,
  include and naming diagnostics, including CVK header diagnostics. No warning was
  suppressed to obtain this progress. Logs: `build/gfx-surface-analysis.log` and
  `build/gfx-win32-interop-analysis.log`, `build/gfx-win32-va-analysis.log`.
  The imported encoder's C4200 suppression was removed; external-header policy
  handles its libva includes. These are not passing library builds or
  runtime test results.
- The current selected encoder compile gets past H.264 and stops at missing
  `os/os_malloc.h` before analysis of its implementation (`build/gfx-encoder-analysis.log`).
  The owned H.264 module uses the MPEG-4 bitstream primitives and the new Exp-Golomb
  reader/writer. NAL output has an explicit sizing pass and correct next-byte escape
  conditions; global lookup tables and packed wire structs have been removed.
  H.264 native compilation/analysis reaches clang-tidy, which still fails on owned
  interface, complexity, constant and buffer-access diagnostics (`build/h264-native.log`).
  Exp-Golomb passes its selected native compilation/analysis/tidy gate
  (`build/expgolomb-native.log`); this is not linked-runtime or whole-library evidence.
  H.264 model/convention completion and startup allocator adaptation remain required.
  No encoder fixture was executed
  around the failed gate. Pending checks include 256/257-segment boundaries,
  cyclic chains, a malformed late segment, native-capacity overflow, undersized
  destination retry, completion timeout, unsupported timed polling, encode failure,
  repeated collection, and preservation of output bytes on every failed collection.
  The new parameter-construction changes additionally require native checks of
  IDR/reference reset, reordered B pictures, low-delay list fallback, field flags,
  POC/frame-number boundaries, slice partition remainders, zero/maximum active
  references, every weight-enable combination, minimum-length caller weight spans,
  and reserved-byte initialization. The missing allocator header prevents native
  compilation of these changes; no fixture bypasses that failed gate.

Supporting-dependency integration, successful gfxlib compilation,
canonical CastDemo consumption, GUI migration and validation remain open. gfxlib
has build wiring but is not yet successfully built or used by CastDemo. Its imported allocator/logging and H.264
dependencies still need adaptation. No fixed CastDemo Release binary is established
by the successful external-library builds or structural checks.

The allocator audit currently finds twelve imported `OS_CALLOC` call sites across
the shared constructors and both native backends (eight in either selected backend).
They allocate image, renderer, swapchain, encoder, adapter, display, surface and native
shared-surface owners. The reference allocator delegates to process-lifetime dynamic
allocation and provides no startup-phase enforcement or recovery evidence. It has not
been copied in as a compatibility shim. Explicit startup ownership, canonical application
construction order and native-provider allocation evidence remain required before those
missing dependencies can be treated as resolved.

CVK's generated enumeration helpers now keep native array workspace in caller-owned
storage. Output-array extender wrappers retain their native capacity and accept the
base workspace before the extender destination. Instance/device extension queries
and their compositions likewise receive the 512-entry extension-property workspace
from their callers. This removes the reported 20 KB and 133 KB local stack frames
without shrinking query capacities or adding heap/global storage. These are CVK
source-interface changes; gfxlib's 23 retained public signatures are unchanged.

Two-call enumeration now retains the queried capacity separately from the in/out
result count. Output preparation uses that retained extent, and the count returned
by the fill must remain within it before consumers can read entries. The selected
core build passes native compilation and MSVC analysis. This fixes the reported
queue-family uninitialized-data analyzer path; the full clang-tidy gate still fails.
The current check is recorded in `build/cvk-core-analysis.log`. No runtime or full
Release-build result follows from this selected compilation.

The generated public layout is now `include/cvk/` and `include/scvk/`. Core, JSON,
and platform sources, applications, tests and Python's binding generator use those
include namespaces through CMake's existing `include/` root. All 137 CVK/SCVK
include directives resolve; generated-file reproducibility, eleven generator-option
tests, platform-header emission and generation of 5,280 Python bindings pass. The
configured cva regeneration CTest also passes. These checks do not close the owned
strict-build or production-integration requirements.

The registry model now retains imported-type provider headers, and the generator
uses those dependencies in implementations. Core and platform sources directly
include the owned/native declarations they consume; unused platform string/SDK
includes are not emitted. The current core run has zero native compiler/analysis
diagnostics and 816 clang-tidy errors (previously 856). The Win32 import source
has zero native diagnostics and 52 clang-tidy errors: 45 short parameter names in
the core header, six parameter-interface diagnostics and one array-bound constant
diagnostic. The include-cleaner errors are cleared in both selected sources. No
warning policy was weakened. The complete library/application builds still fail.
