# Graphics-library extraction

The user requires a `gfxlib` with `Gfx_` public names, using
`C:/dev/vulkan-va-zero-copy` as the starting implementation. Displays, native
windows, presentation/storage surfaces and their platform mechanics belong below
CastDemo's UI policy. This is part of the complete goal, not an exemption for GUI
dependencies or a claim that a Linux application already exists.

The user's subsequent explicit direction fixes the migration boundary: copy the
existing gfx library and refactor the internals; **do not modify any public gfxlib
function signature**. Extend behavior through enums. Existing signature types,
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

The 39 existing graphics headers, implementations and shaders have been copied to
`gfxlib/`, including both platform backends. `gfxlib/UPSTREAM.json` records the
original file hashes and all 336 public function declarations across 15 headers.
`python scripts/check_gfx_api.py` verifies that no signature is added, removed or
changed; enum extensions do not alter that comparison. The original checkout is
unchanged.

CVK conversion, supporting-dependency integration, build wiring, GUI production
migration and validation remain open. The imported implementation is not yet
built or used by CastDemo. Copying it does not establish conformance or completion.
