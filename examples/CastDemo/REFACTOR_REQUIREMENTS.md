# CastDemo refactor requirements and evidence

This records the full requested end state. An entry remains open until its evidence
covers the entire applicable implementation. A successful build or a passing subset
of tests does not establish completion.

## Exact active goal

> okay well the castdemo is written very poorly. for starters every operation must be realtime. no dynamic allocation during playback, oor during user interaction. basically no allocation after program initialization. additionally, the code is not very readable and violates MISRA C guidelines everywhere. thirdly the codes you wrote are slow and fat. the code must be hyper-optimized. see win32x, and c:\dev\xml for guidelines on style and dynamic-allocation avoidance as well as readability. fix all issues. code must retain all current features but be as literally as optimized as possible. all state machines must be ultra-robust and idempotent and immune to failure. all MISRA C guidelines must be followed, as well as full-adherence to all coding style and coding convention and coding standard rule precedent set by a full understanding of Win32X and C:\dev\xml must be followed to full completion no exceptions.

The scope is the complete goal. User corrections identify missed requirements;
they do not replace the rest of the goal or reduce the scope to one issue.

Source-count snapshots later in this document predate the gfxlib import and Linux
diagnostic backends. They are historical observations, not a current whole-tree
audit or proof that the new files satisfy the goal. The user removed historical
build artifacts to reclaim disk space; regenerate only evidence needed for current
work and reuse existing build directories.

## Explicit clarifications that must not be lost

The user's final compiler and analysis requirement (2026-09-28) is mandatory:

- Use all available processors for the whole solution build and for clang-tidy.
  Do not impose an arbitrary four-worker MSBuild limit or retain Visual Studio's
  one-process clang-tidy default. Use native parallel build options without
  reducing any required diagnostic coverage. The user explicitly rejected added
  scheduler configuration and `scripts/run_clang_tidy.py`; that script is deleted.
  Do not recreate it or add scheduler/resource-manager overrides.
- The user initially prohibited changing generated CVK functions or their generator.
  The attempted array-copy, output
  initialization and output-publication generator edits were reverted, including
  regenerated output and the associated generator-test edits. The user subsequently
  chose "Preserve generated code; allow narrowly scoped diagnostic exceptions."
  The initial five Clang categories reported in the generated CVK target were:
  `readability-non-const-parameter`, `bugprone-invalid-enum-default-initialization`,
  `clang-analyzer-security.insecureAPI.DeprecatedOrUnsafeBufferHandling`,
  `-Wmissing-noreturn`, and `-Wimplicit-int-enum-cast`. Apply these only to the
  generated CVK target, preserving every application check, native MSVC warning
  requirement, and other Clang check. This exception is not CRT-free acceptance
  or proof that the solution builds.
  Later generated-JSON diagnostics are not covered by that initial approval.
  The user explicitly rejected the added `readability-math-missing-parentheses`
  exclusion. All later inferred Clang exclusions and proposed native suppressions
  have been removed; only the initial five generated-code categories above remain.
  Native `/Wall /WX` permits only C4820, including generated code. The user also
  rejected additional assertion-related C5045 exceptions. Generated-source and
  generator immutability was subsequently superseded by the explicit approvals
  below; unresolved findings remain build failures.
- The user subsequently selected "Allow generator fixes and matching regenerated
  output" for the reviewed generated-JSON fixes. Apply changes to the generator
  and templates, then regenerate matching CVK and SC output. Preserve public
  signatures and parameter names except for the separately approved native-handle
  resolver contract correction. The user selected "Allow the handle contract
  correction": numeric JSON IDs remain separate and the resolver writes native
  handle values. Neither approval permits additional diagnostic exceptions.
- Preserve existing parameter names. Diagnostic findings do not authorize renaming
  parameters in CVK or its clients. Resolve client misuse through correct contracts
  and call sequencing instead of changing library parameter names.
- Public library includes carry the library namespace. CVK uses `cvk/cvk.h` and
  `cvk/win32/cvk_win32.h`, with analogous JSON and other platform headers. The
  standalone SC profile uses `scvk/`. Generators, exported include roots and every
  owned consumer must agree; flat forwarding headers do not satisfy this rule.
- Compile the owned CastDemo build, gfxlib and CVK with MSVC `/Wall /WX`.
- MSVC permits only compiler-introduced padding warnings to be suppressed.
  Clang's equivalent exception covers both `-Wpadded` and the independent
  `clang-analyzer-optin.performance.Padding` check; neither changes type layout.
  The user subsequently approved two Clang exceptions: documentation diagnostics
  conflicting with bare `\retval None`, and parameter-name length checks conflicting
  with existing names. Clang groups the empty-paragraph diagnostic under
  `-Wdocumentation`; use `-Wno-documentation` and set the minimum parameter-name
  length to one. Other identifier-length checks remain enabled. No other owned-code
  warning may be disabled, filtered out, downgraded or circumvented. This later
  authorization supersedes the padding-only Clang policy in earlier checkpoints.
  A further explicit approval permits `-Wno-unsafe-buffer-usage` and disabling only
  `bugprone-easily-swappable-parameters`, preserving C arrays and existing CVK
  interfaces. The installed Clang 22.1.8 rejects even a bounds-checked C11 array
  access under the former check. Bounds/path analysis and sanitizers remain required.
  The user additionally approved `IWYU pragma: keep` solely for required
  `<Windows.h>` setup includes: removing the header breaks SDK architecture setup,
  although include-cleaner reports the umbrella include as unused. This permits
  that individual include annotation, not disabling include-cleaner or annotating
  arbitrary headers. All other missing/unused direct-header checks remain enabled.
  The user also explicitly requires ignoring warnings from external libraries.
  Keep Volk as a pinned external library and mark dependency/SDK headers as external.
  This explicitly includes Vulkan core, utility, video and SC header families.
  Consume the user's pinned `external/cdx` submodule for applicable DirectX work
  inside gfxlib, using the existing GitHub Desktop/Git Credential Manager credentials.
  The user also authorizes external `cva`. Their subsequent instruction is to
  remove the redundant `gfx_vulkan` and `gfx_vaapi` wrapper modules after replacing
  call sites directly with CVK and cva. The higher-level public `Gfx_` signatures
  remain unchanged; the explicitly retired wrapper families are the exception.
  The exception does not include the copied gfxlib, whose internals are now owned
  refactor work, or CVK. Allocation/lifecycle and functional requirements still apply
  at dependency boundaries; external-warning exclusion is not conformance evidence.
- Do not use `/QSpectre` to remove Spectre diagnostics. Resolve C5045 by correcting
  the dataflow and load ordering. Every evaluated address must be valid by
  construction; hoisting an unchecked access before a bounds check is not a fix.
- Never use `NO_INLINE`, `noinline`, or equivalent compiler-specific barriers.
  Use `FORCE_INLINE` for inline helpers and keep optimization/LTCG enabled.
- Fully enable clang-tidy for **C**, as a required build check, with diagnostics
  treated as errors. Do not analyze the C sources as C++ or hide failures through
  disabled checks, diagnostic suppression or success-returning wrappers.
- Apply MISRA C:2025 guidelines throughout. Clang-tidy alone is not evidence of
  complete MISRA coverage: record supported checks, the remaining rule/directive
  inventory, dedicated-checker results and required manual review. The enforcement
  and evidence gaps are recorded in [MISRA_REVIEW.md](MISRA_REVIEW.md).
- Every applicable MISRA guideline remains required even if no installed check
  implements it. Missing analysis support is an open requirement, not an
  exemption. This applies across production code, tests and dependencies; the later
  external-warning exception changes diagnostic reporting, not required behavior.
- Read the XML/ARINC control-flow precedent and remove inappropriate early
  returns throughout the changed tree. Use named conditions, staged work and a
  final result; retain explicit resource cleanup and output-publication semantics.

These are acceptance requirements, not a claim that the current build passes.

The user additionally requires Valgrind checks and fully sanitized validation.
Provide reproducible sanitizer and Valgrind build/run configurations, preserving
the strict compiler and analysis requirements. Cover production code, fixtures,
malformed inputs, repeated lifecycle operations and dependency boundaries. Report
the exact tools, enabled checks, instrumented binaries and executed scenarios;
unsupported platform/tool combinations and unexecuted paths remain open gaps.
Passing portable-library tests alone does not establish coverage of the Windows
GUI, transport, Media Foundation or CVK/Vulkan application.

The user reiterated these two requirements verbatim:

> Allocating after initialization violates the goal, including allocations hidden in playback, discovery, subtitles, conversion, and dependencies. Those paths are still open defects. Moving allocation to a worker thread does not satisfy the requirement.

> CastDemo also needs specification modules for protobuf and CastV2, Cast JSON messages, HTTP and byte ranges, SSDP/DIAL, device-description XML, URLs and Unicode, SRT/WebVTT/tx3g, and the codec configuration formats it reads and writes. MP4 and Matroska alone are insufficientâ€”and their new header readers are only partial implementations

The user's other explicit instructions remain in force:

- CRT-free executable, `WinMainStartup`, no CRT, hand-written functionality.
- The user additionally requires a hand-written `crt` library with `CRT_` public
  function names. This owns required runtime/compiler support; it does not authorize
  linking Microsoft's CRT or allocating after startup. See [crt/README.md](crt/README.md).
- Required shared OS modules: `os_thread`, `os_fiber`, `os_mutex`, `os_localmutex`,
  `os_semaphore`, `os_localsemaphore`, `os_queue`, `os_ringbuffer` and `os_atomic`. All nine
  require implementation, production migration and verification under the full
  goal. Their responsibilities and acceptance criteria are recorded in
  [OS_MODULES.md](OS_MODULES.md); all remain open.
- Target MISRA C:2025, as accepted in the user's response about the edition.
- No globals. Application state belongs to explicit startup-owned contexts and
  reaches workers and callbacks through their context parameters. Do not replace
  global objects with global pointers, local statics, TLS or hidden singleton
  accessors. Keep every context alive until all borrowers have stopped.
- Name each distinct validity, range and capacity question with `bool_t` before
  combining the answers. Naming only the final compound expression is insufficient.
- Use CVK for Vulkan operations. Query loader, version, extension, queue, format
  and profile prerequisites before construction. Do not bypass CVK with raw
  Vulkan calls or replace its construction contracts with application recovery
  branches. Reuse its existing query functions and builders throughout CastDemo.
- JSON syntax belongs in a general `utl_json` specification module. Cast message
  models belong in `utl_chromecast`, which consumes `utl_json`. CastDemo selects
  policy and appearance through typed values. Neither `utl_castjson` nor a
  `cast_json` application adapter is the requested specification-layer boundary.
- Read the actual Win32X and XML references. Do not substitute guessed conventions,
  generic C style, superficial resemblance, or a formatter's defaults.
- Specification libraries follow `C:/dev/xml`'s `utl_xml` and `utl_arinc661`.
  They need substantive typed specification models and implementations, not renamed
  ad hoc parsers, empty module files, or only outer headers.
- CastDemo is an application composing RFC technologies, ISO formats and protocol
  schemas; its implementation must echo that composition. Specification libraries
  must replace bespoke, non-conformant production logic with auditable models,
  contracts and implementations. General grammar belongs below protocol schemas;
  application policy composes their typed interfaces. Record specification-clause,
  implementation, independent-vector and production-consumer traceability. The
  acceptance criterion is in [SPECIFICATION_MODULES.md](SPECIFICATION_MODULES.md).
- Use canonical `windowsx.h` message handling with `HANDLE_MSG`.
- Abstract GUI dependencies into `gfxlib`, using the `Gfx_` prefix. Start from
  `C:/dev/vulkan-va-zero-copy`'s display/native-window, surface and platform
  implementations; preserve application behavior while adapting their ownership,
  event processing and CVK usage to this goal. [GFX_MODULES.md](GFX_MODULES.md)
  records the inspected interfaces and required migration evidence.
- Copy the existing gfx library and refactor its internals. Preserve the existing
  public function signatures exactly; the user states they were heavily thought
  out. Extend the interface through enums instead of redesigning those signatures.
  This explicit API-preservation requirement also applies where an existing public
  signature uses types that would otherwise be changed by the earlier convention
  rules; it does not exempt internal implementation from those rules.
- The copied gfxlib must use CVK internally. Replace raw Vulkan effects and its
  duplicate construction/validation wrappers with CVK prerequisite queries and
  construction APIs. Do not perform an operation first and infer its support from
  its return value. Preserve the `Gfx_` public API while making this migration.
- CastDemo must consume gfxlib canonically: link the library and use its public
  `Gfx_` interfaces for the responsibilities it owns. Move native display/window,
  surface and graphics implementation behind that boundary. Do not retain a
  parallel application implementation, include gfxlib private headers in CastDemo,
  or treat an unused library target as integration. Keep application/media/UI
  policy in CastDemo and native/Vulkan mechanics inside gfxlib's selected backends.
- Use character-set-agnostic Win32 operations. Generic API names, `TCHAR`,
  `LPTSTR`/`LPCTSTR`, generic structures and `TEXT(...)` must agree throughout the
  platform-facing path. A Unicode-only build does not establish this requirement.
  Fixed UTF-8/UTF-16 wire data and APIs with no generic counterpart require explicit,
  documented conversion boundaries, not blanket A/W calls across the application.
- Use named `bool_t` questions aggressively to make logic readable.
- Column-align assignments and declarations.
- Initializers come first in the public API, in both headers and sources. Accessors
  use `Get...` (for example, `OS_RingBuffer_GetFreeByteCount`), not bare nouns such
  as `FreeByteCount`. Apply this throughout the working-tree changes, including
  callers and documentation; predicates retain meaningful `Is`/`Has` names.
- After initialization, query/derive APIs precede all read/write APIs, with matching header/source order;
  `Utl_Protobuf_GetFieldByteCount` is the explicit example.
- Every function definition has the full asterisk separator and a multiline
  Doxygen `\par Details:` block (`None` when there are no additional details).
- GUI modules follow `C:/dev/xml/examples/arinc661_configuration.h` and `.cpp`,
  including explicit initialized models, bounded owned storage, documented helpers,
  staged mutations and pending actions, and separated measurements and views.
- Put macros in their correct section. The complete source organization and
  documentation conventions apply; a few numbered comment banners are insufficient.
- Do not use `size_t` in application-owned declarations or interfaces.
- Use `byte_t` for counted protocol spans and serialized, network, container,
  codec and subtitle storage, including UTF-8 syntax. Carry that type through
  APIs, owners and callers. A cast that conceals incorrectly typed owned storage
  is not a correction. Keep terminated application/native strings distinct,
  with explicit representation views only at their actual boundaries.
- Do not introduce U/u integer-literal suffixes as a substitute for choosing types.
- Do not use `while` or `do ... while` loops, including macro wrappers. The user's
  clarification is: "remember while and do while loops are non-grata".
  Follow `utl_xml`'s `for (;;)` convention with
  explicit termination. This explicit user instruction overrides the PDF's
  post-tested-loop syntax recommendation; bounds and dataflow still need verification.
- A flag presence test uses the mask result directly, without comparison to zero:
  `if (pEvents->pending.present & CAST_EVENTS_LOG)` is the user's explicit example.
  This direct-condition example takes precedence over the PDF's requirement to
  first store every mask test in a local. Complex questions still need named booleans.
- Reassess everything against the complete goal, rather than checking only the
  most recent criticism. Style, documentation, architecture, allocation, protocols,
  state machines, performance and build output all have to satisfy it together.
- Preserve the original features: subtitles from separate files and embedded tracks,
  gain above 100 percent with live updates, dark UI including headers and combo boxes,
  playable audio, and release binaries in the correct build directory.

## Allocation and realtime requirements

Program initialization is a defined application startup phase, before normal
interaction and playback. A per-cast, per-file, per-discovery or per-seek preparation
step must not be relabeled "initialization" to permit allocation.

All application-owned buffers, object slots, queues and worker resources must have
their storage and ownership established at startup. Their capacities, lifetimes,
overflow behavior, cancellation and reuse must be explicit. Avoiding `malloc`
spelling alone does not meet this requirement: a runtime arena allocator, wrapper,
replacement allocator or hidden library allocator still needs the same audit.

The audit covers GUI events and painting, discovery and HTTP description requests,
connection/TLS setup and reconnect, subtitles, container indexes and samples, HTTP
clients, audio gain/conversion, codecs, GPU resources, proxy switching, workers and
failure/teardown paths. OS and third-party boundaries are included in the evidence;
they cannot be assumed exempt because the allocation is outside CastDemo's source.
Read-only file mapping also creates resources and needs lifecycle evidence.

Moving a blocking operation or allocator to another thread does not make that
operation realtime or allocation-free. UI handlers need bounded work, background
processing needs bounded storage and scheduling, and gain must update the running
audio pipeline without regenerating a complete file. Latency, throughput and
resource behavior need measurements under concurrent load and repeated operations.

If an unavoidable external limitation is discovered, document the concrete evidence
and report it. Do not silently waive the requirement or claim that a weaker behavior
satisfies the goal.

## Authoritative inputs

- The active thread goal: realtime operations; no allocation after initialization;
  all current features retained; robust, idempotent failure handling; optimized code;
  full MISRA C and Win32X/XML convention adherence.
- User clarifications: MISRA C:2025; no CRT; `WinMainStartup`; hand-written runtime
  functionality; implement protocol/specification libraries following `utl_xml` and
  `utl_arinc661`; canonical `HANDLE_MSG` dispatch; named `bool_t` questions;
  aligned assignment columns; macros in their prescribed section; no `size_t`
  in application-owned declarations or interfaces; no U/u literal suffixes.

These requirements apply together to every changed module. An isolated correction
does not satisfy the goal. Explicit-width types must reflect the domain: counted
small buffers and slot counts use `uint32_t`; file extents and large workspaces use
`uint64_t`. Narrowing at Win32 or dependency boundaries requires a checked bound.
- `C:/dev/Win32X/docs/convention.pdf`, all 20 sections, including source layout,
  declarations, message crackers, C5045 dataflow, checks, themes and accessibility.
- `C:/dev/Win32X/examples/main.c`, `examples/main_v2.c`, `include/Win32X/windowsx2.h`,
  startup source and shared compiler policy.
- `C:/dev/xml/include/utl/utl_xml.h`, `utl_arinc661.h` and their implementations:
  source sections, typed models, caller-owned storage, specification citations,
  numbered requirements and function contracts.

The user's explicit no-CRT instruction takes precedence over the PDF's default
static CRT recommendation. The user's explicit dark-mode request remains a product
requirement. Neither qualification waives the other requirements.

## Completion evidence

| Requirement | Required evidence | Current state |
| --- | --- | --- |
| Realtime user interaction | Bounded handlers; no file scans, network waits, conversion, joins or new threads in handlers; measured latency under concurrent load | Open: synchronous subtitle scans, preparation and cancellation remain |
| Realtime gain | Gain applied in the running audio pipeline; no full-file regeneration or receiver reload per slider change; seek and subtitle synchronization verified | Open: current conversion still processes complete files |
| Allocation only at initialization | All application and dependency allocation paths identified; fixed storage and persistent workers; runtime traces after initialization including paint, seek, gain, reconnect and failure paths | Open: many media, network, codec, UI and session paths still allocate |
| CRT-free executable | `WinMainStartup` entry; `/NODEFAULTLIB`; linker map and PE imports show no CRT; no hidden compiler runtime dependencies | Open: release still uses CRT startup and functions |
| Hand-written runtime functionality | Bounded string, numeric, byte, I/O and formatting operations; no CRT aliases hiding linked dependencies; edge-case tests | Open |
| Protocol/specification libraries | Specification inventory; typed caller-owned APIs; source references and numbered requirements; readers/writers separated from Win32 I/O; wire-format, malformed-input and capacity tests | Open: existing codecs remain ad hoc |
| XML source organization | All prescribed header/source sections, macros in 3.1, types in 3.2, globals and prototypes before functions; public contracts and implementation details | Open: new and existing modules need a complete audit |
| Readability and declarations | Named `bool_t` conditions; aligned assignments, declarations, comparisons and tables; C89 declaration preambles; no combined declarators or hidden assignment conditions | Open: only part of the new control code has been corrected |
| Canonical Win32 dispatch | Sorted message values; aligned `HANDLE_MSG` rows; class-prefixed `_On*` handlers; distinct direct handlers; shared missing HANDLE/FORWARD crackers; preserved default processing | Open: main dispatch converted; subclass dispatch and shared crackers incomplete |
| MISRA C:2025 | Applicable full rule/directive inventory, analyzer results and manual evidence for undecidable directives across production code and boundaries | Open: no complete rule set or MISRA checker configured |
| Failure robustness and idempotence | Explicit ownership/state transitions; no terminal-event loss; stop priority; stale completion rejection; cancellation and teardown fault injection; no use-after-free | Open: queue/mailbox tests cover part of the control path only |
| Optimization | Baseline and resulting CPU, memory, executable/import footprint, latency and throughput measurements; inspect hot-path generated code | Open: no whole-application performance proof |
| Win32 UI conventions | Retained canonical layout, coherent painting, correct invalidation; theme/DPI/high-contrast behavior; keyboard and UIA verification | Open |
| Strict enforcement | Format and convention guards; CRLF; warning-clean builds with warnings-as-errors; native AllRules and full configured C clang-tidy analysis; only compiler padding and the four explicitly approved Clang exceptions; optimization retained | Open |
| Correct deliverable | GUI and CLI release builds under `build/windows-msvc-2026/examples/CastDemo/Release`, tested from the final current tree | Intermediate builds exist; final acceptance remains open |

## Features that must be retained

- Receiver discovery, name matching, direct host selection, refresh and Windows Cast.
- Local and HTTP(S) media, route-facing bind address, configurable HTTP port,
  byte ranges, concurrent receiver connections and growing fragmented MP4.
- Connect/launch/load, heartbeat, status, pause/play/toggle/stop, loop and timed stop.
- Seek, scrubbing, repeated bounce measurements, proxy switching and quality preview.
- GPU capability probing, AVC decode/encode, scale, GOP, quality and motion settings.
- Sidecar SRT/WebVTT, MP4 tx3g, MKV UTF-8 subtitles, language/track selection,
  automatic/off/browse modes, CORS delivery and subtitle timing across seeks.
- Gain from 0 to 400 percent, clipping saturation, original-channel audio and AAC
  compatibility for the user's EAC3 media.
- Dark headers, combo boxes, buttons and sliders; DPI handling and keyboard navigation.
- CLI discovery/playback flags and diagnostic modes: `--gpuprobe`, `--encodeinit`,
  `--encodeframe`, `--transcode`, `--fragmp4`, `--proxymp4`, `--decodeinit`,
  `--decode`, `--demux`, plus existing tests.

## Protocol and format inventory to implement and verify

CASTV2 framing and its protobuf schema; Cast connection, heartbeat, receiver and
media JSON messages; HTTP request/response/range/CORS semantics; SSDP and DIAL;
device-description XML; URL and Unicode handling; ISO BMFF/MP4 sample tables,
timelines and fragments; EBML/Matroska subtitle traversal; SRT, tx3g and WebVTT;
AVC parameter parsing and container codec configuration; PCM gain and audio
format handling. The OS TLS, socket, codec and Vulkan boundaries also need
explicit lifecycle/allocation evidence. This list is an audit starting point,
not permission to omit additional specifications found in the code.

The detailed implementation map is [SPECIFICATION_MODULES.md](SPECIFICATION_MODULES.md).
The source-backed record of missed conventions is [REFERENCE_RULES.md](REFERENCE_RULES.md).
Every producer, consumer and transitive format dependency must be accounted for.
Wire headers, test fixtures, and isolated functions are progress only; they do not
establish a complete specification library or production integration.

## Whole-codebase convention checklist

Apply the complete references, including details not repeated below. This checklist
is a review aid, not a narrower replacement for the source precedents.

1. Use the XML numbered layout. Sources have references, includes, declarations
   with 3.1 macros, 3.2 types, 3.3 external globals, 3.4 static globals, 3.5 static
   prototypes, then private and public functions. Headers have the corresponding
   constants, variables and public-function sections. Keep declarations in the
   preamble instead of introducing macros, types or globals halfway through code.
2. Document types, members and function contracts as in the XML modules: specification
   references, brief, details, numbered requirements, direction-qualified parameters
   and return values. State ownership, lifetimes, capacities, failure effects and
   supported specification semantics. Match the documentation to the implementation.
   Results use aligned `\retval` entries, with separate values/ranges and failure
   cases. `\return`, `\returns` and their `@` spellings are prohibited. Use
   `\retval    None` for void contracts, with nothing following `None`; do not add
   "No result value." or describe effects on the result line. Put meaningful effects
   in Details or numbered requirements. Retain the full Details block on definitions.
3. Use explicit-width domain types and the XML `bool_t`, `byte_t`, `char_t`,
   `float32_t` and `float64_t` precedent. Use platform types at genuine platform
   boundaries. Check bounds before narrowing. Do not blanket-replace lengths with
   32-bit values where file sizes or addressable spans need 64 bits.
4. Declare stack locals at each block's beginning, in first-use order, with one
   declarator per statement and no declaration initializers. Do not create naked
   scopes merely to hide temporary declarations. Use meaningful domain names.
5. Align assignments, declarations, members, macro values, comparison operators and
   repeated call/dispatch columns. Run formatting and the required post-pass, then
   inspect the resulting code; clang-format alone is not sufficient.
   Do not binpack arguments or parameters. Once a list wraps, each argument or
   parameter starts on its own line. This applies to declarations, definitions,
   calls and macros, throughout the changes.
6. Use constant-left comparisons. Use booleans directly or with `!`, not equality
   against TRUE/FALSE. Do not compare a presence mask to zero. Break complex
   predicates into readable named questions and keep side effects out of conditions.
7. Parenthesize dereferenced values as `(*p)` and `sizeof((*p))`. Separate pointer
   advancement from dereference operations. Select ternary values before calls.
   Use `ARRAYSIZE` instead of `_countof`.
8. Use sorted `HANDLE_MSG` tables in both window and subclass procedures. Each
   distinct message has a class-prefixed direct `_On*` handler. Shared behavior
   belongs in helpers. Preserve default processing with the corresponding
   `FORWARD_WM_*`. Shared missing crackers need callback-signature comments.
9. Use `for (;;)` loops with explicit termination as in `utl_xml`; do not use
   `while` or `do ... while`, including macro wrappers. Conventional bounded `for`
   loops are also permitted. Preserve termination, bounds, side effects and
   `continue` behavior. Follow Win32X's safe-value dataflow rules and repair C5045 findings
   structurally. Do not silence diagnostics or disable optimization to make code pass.
10. Preserve CRLF, strict warnings, warnings-as-errors, native analysis, focused
    clang-tidy checks and effective enforcement. Do not introduce warning suppression,
    guard exceptions or a baseline allowlist to disguise the remaining violations.
11. Verify retained UI ownership, geometry, coherent painting, DPI, theme invalidation,
    high contrast, keyboard use and UI Automation. The requested dark mode remains
    the product default; other applicable UI conventions are still requirements.
12. Verify every state transition, ownership transfer, repeated action, saturation,
    stale completion, cancellation, partial initialization and teardown failure.
    Tests for a command queue alone do not prove the complete application's robustness.
13. Follow `utl_xml.h` section 3.1 for named grammar characters, delimiters,
    keywords and bounds. Function bodies use those names; documented static
    grammar tables may contain literal ranges. HTTP octets retain their wire
    encoding; generic Win32 character operations apply at the OS/UI boundary.
14. Initializers must establish valid objects by definition. Missing required
    storage, callbacks, invalid kinds and violated object invariants are developer
    errors and assert in every build. Apply this to every changed API. Recoverable
    malformed external data, capacity exhaustion and genuine OS resource failures
    remain explicit ordinary results. Do not hide misuse behind a FALSE return.
15. Keep platform, target and compiler details behind OS interfaces or build-selected
    implementation files. Shared utilities and application/test bodies use domain
    types. Native process startup, command-line access, error-dialog policy and
    termination belong to OS backends. Compiler annotations belong to the compiler
    abstraction. This is a cross-cutting requirement, not a one-call wrapper rule.
16. Keep a single public `Utl_Arguments_Read` interface. Select
    `utl/win32/utl_arguments.c` or `utl/posix/utl_arguments.c` in the build; do not
    introduce target suffixes or conditional-compilation selection in that module.
    Preserve native semantics: Win32 command-line quoting versus POSIX argv records.

## Current evidence and limitations

The reproducible lexical audit is:

```powershell
python scripts/audit_cast.py --report build/cast-audit.json --strict
```

Its first full source scan covered 47 C/header files and 15,173 lines. It found
280 `size_t` occurrences, 228 U/u literals, 512 CRT call sites, 353 missing required
section headings, 129 misplaced macro definitions and additional convention defects.
It also identified 134 allocation/release, 111 resource/thread lifecycle, and 44
waiting/I/O sites requiring semantic review. These are a dated baseline from
2026-09-27, not permanent counts or evidence that the list of defects is exhaustive.
The report records individual locations and source hashes. Regenerate it after edits.

The audit deliberately fails while detected violations remain. It is a lexical
check, not a MISRA analyzer or runtime allocation trace. Full documentation,
semantic style, state-machine, dependency, performance and feature reviews remain
required. No complete MISRA C:2025 rule inventory or compliance checker is currently
configured, so no MISRA-compliance claim is justified.

The current GUI still links CRT and has post-initialization allocations and blocking
paths. The format, protocol and HTTP test executables use `/NODEFAULTLIB` and
`WinMainStartup`; that is evidence about those test executables only, not about
the GUI or CLI. The protocol test's imports are only Kernel32 `WriteFile`,
`ExitProcess` and `GetStdHandle`. The production adapter and its CastV2/Unicode/
protobuf dependencies are exercised by that test without CRT linkage. The decimal
and HTTP range modules run in the same CRT-free test executable; they introduce
no additional imports. The HTTP head/framing test also imports only Kernel32
`WriteFile`, `ExitProcess` and `GetStdHandle`. Its Release/LTCG link exposed a
compiler-generated `memcpy` in the forward span-copy loop; a descending counted
copy removed that dependency without disabling optimization or checks.

The last full functional run passed seven test groups: messages, subtitles, audio,
controls, formats, protocols and HTTP (16.06 seconds). Nine audit tests passed,
including prohibited-loop coverage of all 64 production and Cast test C/header
files, query/derive-before-Find ordering and named grammar-character detection.
It does not prove receiver playback, live gain, allocation
absence, realtime behavior, performance superiority or completion of the goal.
Focused `clang-analyzer-*` checks reported no findings in `utl_castv2.c`,
`utl_unicode.c` and `cast_message.c`. This is not full native analysis or MISRA
evidence for those modules or their callers. Focused analyzer checks on
`utl_decimal.c` and the consolidated `utl_http.c` report no findings after the
redundant initial assignment in `ReadFramingField` was removed. Existing C4996 warnings
for `subtitles.c`'s `sscanf` and the audio test's `_wfopen` remain open CRT defects.

Release output belongs in
`C:/dev/cvk/build/windows-msvc-2026/examples/CastDemo/Release/`.
An intermediate build in that directory must not be described as the final fixed
build while any applicable requirement remains unverified.

## Verified progress so far

- Fixed-size session event mailbox replaces heap-owned status/log/end messages.
  Tests cover 10,000 updates, stale generations, terminal-event retention and
  nonblocking reads under lock contention.
- Fixed command storage coalesces scrub positions, preserves final seeks under
  saturation, latches Stop and rejects subsequent work until reset.
- One discovery worker is initialized once and reused; repeated worker submissions
  and idempotent shutdown are tested.
- Paint-time brush creation and GUI keyframe allocation were removed.
- Session error text is copied before channel destruction; session storage is not
  released until the session thread has actually exited.
- The main window uses typed message-cracker handlers. Full convention compliance
  remains open as stated above.
- MP4 box-header and Matroska VINT/element-header readers and writers use counted
  caller-owned storage and are connected to existing readers. These are foundations,
  not complete MP4/Matroska libraries. Header vectors, truncation, boundary arithmetic,
  rejected-write preservation and size forms pass the format tests.
- `utl_protobuf` supplies all six wire forms, bounded varints and balanced unknown
  group skipping. Production Cast messages now use it through `utl_castv2`.
- `utl_castv2` implements the version-1.0 envelope and stateless frame inspection.
  Tests cover independent wire bytes, all required fields, text/binary consistency,
  field order, duplicate and unknown fields, malformed groups/UTF-8, fragmented and
  coalesced frames, full capacity and unchanged outputs after rejection. Later Cast
  versions, authentication and JSON namespace schemas remain open.
- `utl_unicode` supplies UTF-8/UTF-16 scalar readers/writers, width queries and
  counted validators. Independent vectors, every scalar, every isolated UTF-16 unit,
  truncated/invalid encodings and output preservation pass. BOM and byte-stream
  handling and the subtitle/GUI/JSON migrations remain open.
- Channel framing uses the specification module. Incomplete plaintext no longer
  retries forever merely because a header exists; decryption yields at a complete
  frame and rejects a TLS record that makes no progress. Real receiver/TLS stress
  and deadline verification remain open.
- `while` and `do ... while` are absent from all 66 production C/header files
  and ten Cast test sources, including macros. Both forms are audit errors.
  Conversions preserve loop progress and separate side-effecting reads from tests;
  MP4 run counts no longer decrement below zero, NAL scans check remaining bounds,
  and the message pump distinguishes failure from WM_QUIT. This establishes the
  prohibited-loop syntax requirement, not realtime behavior or robust parsers.
- `utl_decimal` and `utl_http` supply bounded decimal conversion, typed
  Range/Content-Range parsing, sizing, writing and range resolution. The production
  Range adapter now validates complete lists before selecting a satisfiable member.
  Overflow, malformed complete input, empty content, all three range forms,
  capacity limits and output preservation have test coverage. Range semantics are
  consolidated into `utl_http`; the separate range module and its build entries
  are removed. Numeric conversion remains in the generic decimal module.
- `utl_http` adds counted head parsing/writing, field lookup and framing derivation.
  Tests cover every incomplete prefix of a representative head, short-output
  preservation, malformed request/status lines, 33 framing vectors, duplicate
  lengths and connection/coding lists. All parser/writer character terminals and
  HTTP keywords use named constants. Production head migration, chunked bodies,
  trailers, method/precondition policy and server lifetime work remain open.
- Function-definition separator/Details guards cover all 410 currently inventoried
  definitions. Query/derive ordering and declaration/definition order are checked
  for public specification APIs. These guards do not certify documentation accuracy
  or full style adherence. The complete ARINC configuration GUI reference has been
  read and its missed architecture rules recorded in `REFERENCE_RULES.md`.
- The incremental HTTP head reader consumes each appended byte once and latches
  terminal results. Date sizing/writing validates civil values and preflights
  output. Prefix fragmentation, malformed input, capacity and independent date
  vectors pass. These additions are not yet connected to production head handling.
- Required construction inputs now assert in the HTTP head initializer, worker,
  events, command reset and message setup APIs. New reader/date/argument/process
  entries enforce their documented required pointers too. Other legacy API
  precondition policies still need review; these checks do not close that requirement.
- OS process and assertion backends isolate Win32 entry, native command-line
  access, error policy, termination and compiler intrinsics. `utl_arguments` uses
  the shared `Utl_Arguments_Read` declaration with separate Win32/POSIX sources.
  POSIX records are not reparsed for quotes or spaces. The new interfaces use
  counted UTF-16 after OS encoding conversion. A POSIX OS startup/conversion
  backend and migration of the remaining application/test Win32 calls remain open.
- The lexical guard now rejects incorrect result tags, wrapped argument packing,
  direct utility dependencies on Windows types/APIs, target-suffixed argument APIs
  and preprocessor selection in argument implementations. Source selection retains
  comparison with the shared header's API order. These are limited lexical guards,
  not proof that every layering, documentation or construction requirement is met.

## Audit loop

Verification snapshot, 2026-09-28:

- Before the final strict compiler/analysis requirement, seventeen Cast suites
  passed in 35.42 seconds, including atomic contention and event-publication
  coherence. The later SSDP suite passed separately. These are historical
  functional results, not acceptance under the new policy.
- Twenty-nine lexical-audit regression tests pass, including nonfinal returns,
  void guard returns, switch exits, inline barriers, startup allocation wrappers,
  CRT warning suppressions and the prior API/doc checks.
- The current lexical scan covers 116 production C/header files, 60,114 lines
  and 925 definitions. It still reports 293 nonfinal returns,
  321 CRT calls, 120 `size_t` uses, 182 unsigned-suffix literals,
  138 missing section headings, 45 macro-placement findings, 88 explicit A/W
  API calls, 18 dereference-sizeof forms and 3 declarations in for headers.
  Lifecycle review includes 137 allocation/release, 151 resource/thread
  and 57 wait/I/O boundaries. Regenerate the report after changes;
  these counts are not a baseline allowlist.
- SSDP specification parsing/writing and discovery adapters now use final
  results; discovery checks capacity for each ready socket in the batch.
  The MP4-index flow has also been revised, including box-bounded metadata reads,
  but its latest source has not passed the new complete build/analysis gate.
- Matroska subtitle traversal now stages results and closes the reader after
  every open attempt. Successful native releases clear their stored identities.
  The adapter uses fixed-width counts, hand-written memory primitives, generic
  native file APIs and named Matroska constants. File mapping per operation and
  persistent ownership after a failed native release remain open defects.
- Extraction now selects its requested Matroska track during metadata traversal
  and retains one record. Removing the former 64-record local array clears a
  newly exposed 19,624-byte stack warning and permits selection beyond that
  artificial limit. A fixture selects track 65 and checks an absent track;
  native compilation/AllRules checks pass, but strict runtime execution is pending.
- Subtitle timestamp parsing now belongs to `utl_srt`, using bounded decimal
  reads and typed integer results. The application retains its two-through-four
  source-hour-digit policy; the module supports wider hours through UINT64_MAX
  milliseconds. Exact minute/second/fraction widths, component ranges, malformed
  delimiters and overflow are validated before publication.
- Media LOAD serialization uses session-owned output and the new `utl_castjson`
  typed writer. The former 25 KB escape/track stack frame and its CRT formatting
  are removed. Every dynamic field uses counted UTF-8 validation and JSON
  escaping, with preflight preserving output on invalid input or short capacity.
  Request IDs/counts use explicit widths, and media time uses integer milliseconds.
  The adapter rejects nonfinite/overflowing positions and missing termination.
  The session now owns startup storage reused through Prepare/Reset; channel and
  playback dependencies still allocate and remain lifecycle defects.
  This is the current LOAD profile, not complete Cast JSON message/schema coverage.
- GUI discovery, gain, session and preview work now reuse four startup-created
  `os_thread` workers. The position timer is established before worker creation;
  timer failure cannot strand workers. Closing acquires pending results before
  releasing execution resources. The former `cast_worker` module and direct GUI
  thread creation/joins are removed. Dependency waits/allocations remain open.
- GUI and CLI cast state now use `CastSession_Initialize` during startup and
  Prepare/Reset for individual casts. `os_memory` reserves, commits, touches and
  locks the private state before publishing it; a failed residency request is
  not accepted as usable storage. Shutdown retains failed native releases for
  retry. The GUI closes storage only after workers and publishers are quiescent.
  CLI casting now uses the same persistent worker abstraction and acquires
  completion before session release. Reset clears the Stop latch and state on
  the next preparation; a second Run requires Reset/Prepare. Proxy and gain
  publication use bounded `CRT_` copies in place of CRT format calls.
  This closes the explicit per-cast session calloc/free path in source, not
  dependency allocations, blocking locks, channel cleanup or full timing evidence.
- `cast_storage_test.c` covers zeroed caller-defined storage, unchanged extent,
  repeated closure, 1,000 prepared casts, fresh command admission after Stop,
  rejection of closure while prepared, media publication and repeated Reset.
  Native compilation/AllRules checks pass. Its remaining clang-tidy findings
  are included-header diagnostics; the fixture has not linked or executed from
  a successful strict build. Native failure injection and receiver execution
  remain separate missing evidence.
- Thread fixtures cover accepted work followed by stop, refusal to close an
  active callback, 1,000 reused jobs, unchanged native handle counts, repeated
  shutdown and immediate rearm followed by stop. JSON fixtures include complete
  independent wire strings, all short capacities, every control escape,
  supplementary UTF-8, invalid encodings, absent required fields and maximum
  integer media time. Their native compilation/AllRules checks pass; clang-tidy
  still rejects source/header findings. These regressions have not executed from
  a successful strict build, and no runtime success is claimed for them.
- New subtitle regressions cover timestamp boundaries/truncation, bounded track
  discovery and callback rejection followed by a fresh read. Their native MSVC
  compilation and AllRules analysis pass; clang-tidy rejects remaining source
  and included-header findings. These new regressions have not executed from a
  successful strict build. See `build/cast-subtitle-test-wall.log`.
- The caller-owned Unicode byte reader now handles optional UTF-8 and UTF-16
  signatures and either UTF-16 byte order without temporary decoded strings.
  `utl_webvtt` provides timestamp parsing, exact output queries and transactional
  plain-text cue serialization. Embedded Matroska timing and MP4 text delivery
  use these modules. The public subtitle counts are explicit-width and required
  pointers assert before use. Native compilation/analysis pass for this change;
  strict clang-tidy still rejects buffer-access, adapter and included-header
  findings. New BOM, malformed encoding, time-boundary, escaping and capacity
  fixtures have not executed from a successful strict build. Full SRT/WebVTT/tx3g
  semantics and dependency lifetimes remain open.
- Subtitle conversion now borrows two fixed buffers through
  `Subtitles_Workspace_t`. The GUI initializes and touches them before workers
  or normal interaction; the original sixteen-MiB payload limit is retained.
  Sidecar input temporarily borrows output until decoding into separate scratch
  completes. Embedded callbacks append within fixed capacity. The adapter no
  longer allocates, grows or frees text; callers query borrowed results. Failed
  operations publish zero bytes and an empty string, and subsequent operations
  reuse the same storage. Staged conversion/publication replaces the converter's
  early-return/goto cleanup. CRT file handles and native/container dependencies
  still allocate. The server now borrows immutable subtitle bytes and keeps request
  state in its startup pool. Startup first-touch does not prove residency;
  conversion/discovery still run synchronously on the GUI.
- Workspace regressions cover exact fit, every shorter capacity, guard bytes,
  1,000 failure/reuse cycles, source/output aliasing, normalized sidecar loading,
  empty-file failure, one-byte error capacity and a fresh load after failure.
  Native compilation/analysis pass for the changed adapter and fixture. Strict
  clang-tidy still rejects buffer-access, legacy-source and included-header
  diagnostics; these fixtures have not executed from a successful strict build.
  Logs: `build/cast-subtitle-workspace-native.log`,
  `build/cast-subtitle-workspace-test.log`,
  `build/cast-subtitle-workspace-tidy.log` and
  `build/cast-gui-subtitle-workspace.log`.
  Targeted session analysis now derives commands from evaluated Release MSBuild
  items because native failure prevents that target's database from being emitted.
  Earlier targeted adapter logs used a missing database and fallback settings;
  they are superseded and are not configuration coverage evidence. The current
  commands preserve C mode, definitions, include paths, optimization and warnings.
- `utl_srt` now reads complete borrowed cues and owns sidecar framing. Malformed
  UTF-8/NUL, incomplete timing and invalid intervals preserve output and cursor.
  Repeated END/INVALID are defined; trailing blank lines are consumed once.
  Named signature predicates, counted line-end offsets and staged fields retain
  explicit dataflow. The private adapter timestamp parser and CRT string scanning
  are removed. Compatibility, styled-text/extension gaps and exact references are
  recorded in `SPECIFICATION_MODULES.md`.
  Native compilation/analysis show no owned SRT findings. The fixture's previous
  complexity finding and parser dead store are corrected; strict clang-tidy still
  fails on buffer-access and included-header diagnostics. No runtime success is
  claimed for the new prefix, overflow, mixed-newline and rollback vectors.
  Logs: `build/cast-formats-srt-review.log`,
  `build/cvk-cast-srt-test-srt-review.log`,
  `build/cast-session-srt-review.log`,
  `build/cvk-cast-subtitle-test-srt-review.log`,
  `build/cast-srt-module-tidy.log` and `build/cast-srt-adapter-tidy.log`.
- `os_mailbox` now implements retained three-slot ownership for one producer and
  one consumer. CastEvents uses it for status/log snapshots; the terminal latch
  remains separate. New `cast_media` channels publish complete proxy/replacement
  URL/MIME pairs from their existing single producers without an SRW-lock wait.
  The runner borrows a stable proxy pair through synchronous serialization and
  consumes replacements only when settled receiver state permits a switch.
  The old temporary proxy URL/type arrays and four CRT format/copy calls are
  removed. Capacity remains 1200/64 bytes; six payload slots are retained in the
  startup session region. The command queue still has a blocking SRW lock.
  This additional shared module does not replace the required mutex, semaphore
  or fiber work, and does not close hidden allocations or realtime guarantees.
- Mailbox regressions cover all 4,096 operation patterns of length twelve,
  unchanged reader borrows during 1,000 publisher cycles, maximum text extents,
  source mutation, independent channels and repeated empty/reset behavior.
  The native contention fixture now checks media-pair coherence alongside events
  for 200,000 publications and final delivery after terminal publication.
  Five additional fail-fast cases cover invalid mailbox use. No owned native
  diagnostics remain in the changed modules/fixtures. The session target still
  fails on NDIS SDK diagnostics; strict clang-tidy also fails. New fixtures have
  not executed or linked through successful required gates.
  The native fixture uses only base threading/event/time APIs; its source now
  selects WIN32_LEAN_AND_MEAN to remove unused optional SDK includes. Required
  headers remain analyzed, and no diagnostic suppression was added.
  Logs: `build/cast-formats-mailbox-review.log`,
  `build/cast-session-mailbox-review.log`,
  `build/cvk-cast-mailbox-test-mailbox-review.log`,
  `build/cvk-cast-events-test-mailbox-review.log`,
  `build/cvk-cast-assert-test-mailbox-review.log`,
  `build/cast-mailbox-module-tidy.log`, `build/cast-media-module-tidy.log`
  and `build/cast-session-mailbox-tidy.log`.
- Discovery now has a startup lifetime owner used by the GUI and CLI. Resident
  request, response, header, identity, location and 16 KiB document storage is
  reused. `os_network` retains a fixed 128-entry interface snapshot buffer and
  query socket; one wildcard-bound nonblocking search socket serves all usable
  IPv4 multicast interfaces. Per-scan adapter allocation, per-description XML
  allocation, per-scan socket creation and repeated WinHTTP session creation
  are removed. `iphlpapi` is no longer a CastDemo dependency.
  Refresh drains at most 256 queued datagrams before sending; overflow fails
  explicitly. Only complete snapshots are published. Description bodies require
  a successful HTTP status and EOF; truncated/read-failed bodies are rejected.
  Direct-host failures preserve caller output, including aliased host storage.
  Native child-resource identities remain retained across failed closure, and
  the GUI closes the owner only after worker exit. Discovery failures are exposed
  to the GUI through the acquired result instead of appearing as an empty network.
  WinHTTP connection/request creation still allocates during discovery. Native
  queries and HTTP calls are synchronous, XML extraction still uses CRT, CLI
  cleanup still polls, and hidden provider allocation/timing remain unverified.
  These are open defects; retained buffers alone do not meet the complete goal.
  The new native network fixture checks partial startup, repeated snapshots on
  the same region and repeated closure. Its source passes native checks but
  included-header clang-tidy findings prevent runtime execution. New module
  buffer-access/include-cleaner findings and SDK WS2tcpip.h C6101 remain; the
  older NDIS failure from adapter enumeration is gone. No Release was linked.
  Logs: `build/cast-network-discovery-review.log`,
  `build/cast-session-discovery-review.log`,
  `build/cvk-cast-network-test-discovery-review.log`,
  `build/CastDemo-discovery-review.log`, `build/CastDemoCli-discovery-review.log`,
  `build/cast-network-module-tidy.log` and `build/cast-discovery-module-tidy.log`.
  The lexical lifetime inventory now includes discovery/network initialization,
  native socket creation/binding/closure, datagram I/O, native interface controls
  and HTTP handle closure. All 29 auditor regressions pass; CRT character-set scanners are included too. The expanded boundary
  counts identify review sites; they are not measured runtime allocation counts.
- `BuildPolicy.cmake` now requires `/Wall /WX`, retaining optimization/LTCG.
  Its sole warning exception is compiler-inserted padding: MSVC C4820 and
  Clang's corresponding `-Wpadded` diagnostic.
  Two remaining `_CRT_SECURE_NO_WARNINGS` definitions were removed from the
  console and MP4 mux sources. The audit now reports such CRT warning switches;
  the remaining console `fopen` call consequently fails native compilation.
  C clang-tidy is integrated into Visual Studio and Ninja/Make builds; Visual
  Studio's emitted analysis commands explicitly use `/TC`. Findings fail
  the build, including header findings. No `/QSpectre` or inline barrier is used.
- The enabled general-purpose C clang-tidy families are clang-analyzer,
  bugprone, CERT, concurrency, misc, performance, portability and readability.
  LLVM-project naming and C++ modernization are not this repository's standard.
  There are no per-diagnostic exclusions. The complete enabled-check list is
  reproducible with `clang-tidy --config-file=examples/CastDemo/.clang-tidy --list-checks`.
- The strict build currently fails. Logs include `build/cast-strict-build.log`,
  `build/cast-session-storage-wall.log`, `build/cast-memory-formats-wall.log`,
  `build/cast-memory-module-tidy.log`, `build/cast-storage-test-wall.log`,
  `build/cast-cli-storage-wall.log`, `build/cast-gui-storage-wall.log`,
  `build/cast-json-session-wall.log`, `build/cast-json-formats-wall.log`,
  `build/cast-json-test-wall.log`, `build/cast-json-module-tidy.log`,
  `build/cast-thread-test-wall.log`, `build/cast-gui-thread-wall.log`,
  `build/cast-control-wall.log`, `build/cast-assert-wall.log`
  and `build/cast-subtitle-test-wall.log`. Correcting unused umbrella-header
  dependencies allowed the format library's native compilation to proceed;
  analysis still rejects owned-source and included SDK-header findings.
  The session poll comparison, MP4-index offset widening, subtitle timestamp
  CRT calls, zero-capacity JSON-copy underflow and LOAD stack warning have been
  corrected; this does not establish a clean full build. Required Windows.h
  architecture setup still conflicts with an include-cleaner finding; dropping
  the umbrella header caused a native architecture error and was not retained.
  The latest full Release attempt also rejects the linked Volk loader's C4191
  function-pointer conversions. Generated policy inspection passes for all 32 C
  targets in every configuration; that establishes enforcement, not clean code.
- The latest subtitle checks are recorded in
  `build/cast-formats-subtitle-review.log`,
  `build/cast-session-subtitle-review.log`,
  `build/cvk-cast-webvtt-test-subtitle-review.log`,
  `build/cvk-cast-subtitle-test-subtitle-review.log`,
  `build/cast-gui-subtitle-review.log`,
  `build/subtitle-wire-review-tidy.log` and
  `build/subtitles-adapter-review-tidy.log`. The GUI native gate still fails in
  shellapi/uxtheme/dwmapi headers; that session run failed in NDIS SDK headers.
  The later discovery migration removed that dependency; WS2tcpip.h C6101 now
  fails the discovery network/session native gates.
  No replacement Release executable was linked outside these failed gates.
- The counted `utl_url` model now drives discovery location parsing. HTTP
  endpoint and origin-form operations live in `utl_http`, with queries before
  Find/read/write. Query strings are preserved, fragments omitted, and malformed
  or oversized hosts fail rather than being truncated. Native buffers belong to
  the startup discovery owner. The old CRT host slicer and WinHttpCrackUrl call
  are removed. The later XML migration below replaces legacy description
  extraction; native HTTP resource allocation remains.
  Relative resolution, IRI/IDNA, zone identifiers, remaining URL consumers and
  native IPvFuture support are still open.
- URL/HTTP tests compile under the native gate but have not executed because
  strict clang-tidy still fails on buffer-access and included-header diagnostics.
  Review corrected dead stores, optional-output paths, arithmetic grouping,
  unproven count initialization, ambiguous slice arguments and function complexity.
  No padding or other new diagnostic suppression was added. Generated-policy
  inspection covered all 33 then-generated C targets; all 29 lexical-auditor tests pass.
  Current logs: `build/cast-url-formats-review.log`,
  `build/cast-url-test-review.log`, `build/cast-url-session-review.log`,
  `build/cast-utl_url-evaluated-tidy.log`,
  `build/cast-utl_http-evaluated-tidy.log` and
  `build/cast-discovery-url-tidy.log`. Targeted format analysis uses 21 commands
  derived from evaluated Release MSBuild items. Earlier targeted runs against
  MSBuild's partial database could not locate these sources and are not evidence;
  the evaluated-command logs supersede them. No new Release binary was linked.
- `utl_xml` now parses counted UTF-8 documents into caller-owned records,
  validating XML names, attributes, numeric/predefined references, comments,
  processing instructions, CDATA and namespace scope/expanded-name uniqueness.
  Text comparison, sizing and writing share scalar validation and normalization;
  insufficient output capacity preserves every destination byte and count.
  Document failures revoke published nodes and permit reuse of the same storage.
  DTD processing and other XML encodings remain explicitly unsupported and open.
- `utl_upnp` separates root-device display selection from XML grammar. Discovery
  now uses these modules on the complete counted HTTP body; its final CRT
  substring scans and string.h dependency are removed. Node/attribute arrays
  belong to the startup discovery owner. Namespace-aware direct-child selection
  rejects duplicate or nested display fields and excludes embedded-device/vendor
  names. Missing model remains accepted for existing UI compatibility; complete
  UPnP/DIAL profile validation, native allocation and bounded I/O remain open.
- The XML fixture has 64 independent document cases and 11 additional display
  cases, plus prefix, capacity, namespace, text-kind and repeated-reuse checks.
  Review corrected conditional initialization, helper operation order, result
  contracts, processing-instruction NCNames and inconsistent raw closing-delimiter
  acceptance. Changed sources and the fixture pass native compilation/AllRules;
  buffer/header clang-tidy findings still fail strict checks. Discovery also fails
  native WS2tcpip.h C6101. The XML fixture has not executed, and no replacement
  Release binary was linked outside the failed gates.
  Current logs: `build/cast-formats-xml-review.log`,
  `build/cast-session-xml-review.log`, `build/cvk-cast-xml-test-xml-review.log`,
  `build/cast-utl_xml-tidy.log`, `build/cast-utl_upnp-tidy.log` and
  `build/cast-ssdp-xml-tidy.log`. Targeted format commands now contain 23 sources
  derived from evaluated Release MSBuild items. Policy inspection covers all
  34 generated C targets/configurations, and all 29 lexical-auditor tests pass.
- Production HTTP serving now consumes `utl_http` through `cast_http`, which
  separates application method/framing/range policy from native transport.
  Incremental parsing stops at the exact head boundary and preserves pipelined
  bytes for reuse. Invalid framing, unsupported methods/versions and request
  content reject with connection closure; Range is applied only to GET.
  File and subtitle responses share complete range-list validation and counted
  response serialization. The former raw header scans, 128-byte Range truncation
  and response-header CRT formatting are removed.
- Growing-file GET still follows the live file end. A valid range selects a finite
  available snapshot with status 206, a bounded Content-Length and an unknown
  final total in Content-Range. Unsatisfied ranges report the current length;
  HEAD emits no body. Actual receiver seek/retry behavior remains unverified.
  Native synchronous I/O, resource allocation and complete teardown evidence
  remain open. Server metadata and worker startup ownership are recorded below;
  that migration does not establish realtime behavior.
- The HTTP policy fixture contains 37 independent request cases, eight exact
  response forms, short-capacity/guard checks, pipeline-tail reuse, overflow and
  field-injection failures, and 100 repeated request-owner cycles. Changed policy
  and fixture sources pass native compilation/AllRules. Required clang-tidy still
  fails on buffer-access and included-header findings; the session native gate
  also fails on WS2tcpip.h C6101. The fixture has not executed, and no new Release
  executable was linked. Current logs: `build/cast-formats-http-policy-review.log`,
  `build/cast-session-http-policy-review.log`,
  `build/cvk-cast-http-policy-test-review.log`, `build/cast-http-policy-tidy.log`
  and `build/cast-media-server-http-tidy.log`. Targeted analysis uses evaluated
  Release commands for 24 format and 20 session sources. All 35 generated C
  targets retain the required policy, clang-tidy configuration verification passes,
  and all 29 lexical-auditor regressions pass. These checks do not close MISRA,
  specification, runtime allocation or timing requirements.
- Route selection now uses `OS_Network_ReadRoute` and the new `cast_network`
  adapter. GUI and CLI create exclusive route owners at startup, separate from
  discovery worker state. Each lookup reuses its retained query socket and
  validates native output before publishing a local address. The former
  `MediaServer_GetLocalAddressToward` temporary socket/connect/close path is
  removed. Shutdown retains owners through failed release attempts; console
  polling and native query latency/provider allocation remain open.
- `utl_url` now supplies numeric IPv4 size/read/write operations using the same
  grammar as URI host classification. Full input validation and output preflight
  precede publication. Thirty independent syntax/numeric vectors, guarded short
  capacities and native route reuse/loopback fixtures were added. Review corrected
  output-pointer proof, fixture complexity, direct includes and helper order.
  Format/route sources and fixture sources pass native compilation/AllRules;
  required buffer/header clang-tidy diagnostics prevent executing these additions.
  The unused WS2tcpip.h include was removed from the network backend, removing
  its C6101 failure without a diagnostic exemption. Session native analysis still
  fails C6101; GUI native SDK headers and the CLI's legacy fopen/C4996 still fail.
  Logs: `build/cast-route-configure.log`, `build/cast-formats-route-review.log`,
  `build/cast-network-route-review.log`, `build/cvk-cast-url-test-route-review.log`,
  `build/cvk-cast-network-test-route-review.log`, `build/cast-session-route-review.log`,
  `build/CastDemo-route-review.log`, `build/CastDemoCli-route-review.log`,
  `build/cast-network-route-tidy.log` and `build/cast-url-route-tidy.log`.
  Targeted network analysis uses two commands derived from evaluated Release
  MSBuild items. Policy inspection still covers all 35 C targets; all 29 auditor
  regressions pass. No new Release executable has been linked.
- Server/proxy shutdown uses retained pointer owners and zero-time thread
  observations. `MediaServer_TryStop` requests retirement; the retained admission
  worker closes the listener and acquires every submitted client's final completion
  before native release and slot reuse. Admission exclusively owns the listener
  during use; finite read polling lets idle and partial-request clients observe
  cancellation. `Proxy_TryStop` retains the session publisher, serving owner and
  temporary path through conversion completion and failed release attempts.
  GUI completion polling retries these owners and retires one gain variant per
  cleanup attempt; CLI keeps session/Winsock ownership until serving quiesces.
- These changes remove the ignored two-second wait/free race and the proxy's
  infinite owner-thread wait. They do not establish realtime operation or no
  allocation after initialization: native serving operations still allocate,
  conversion still creates storage and execution resources, sends and TransmitFile
  remain blocking, and native provider behavior is unproved. Server/client and
  proxy metadata/execution now belong to startup, as recorded below. Failed native
  releases are retained, but failure injection remains open. Callback completion
  is not a dependency allocation trace or
  proof that native provider-internal resource retirement has ended.
- Server IPv4 bind syntax now uses `utl_url`. Invalid non-NULL input is rejected
  before native startup instead of silently selecting every interface; NULL
  retains the explicit all-interface selection. Subtitle extents and diagnostic
  capacities use explicit widths, with borrowed extent checked for addressability.
  New input-error text uses owned bounded memory operations. Removal
  of the now-unused WS2tcpip.h include eliminates that server translation unit's
  C6101 dependency; discovery still fails the same SDK diagnostic.
- `tests/win32/cast_server_test.c` adds 16 repeated empty-listener and four-client
  shutdown cycles, persistent reuse across read polls, two partial request heads,
  retained identity on pending closure, repeated NULL closure and invalid bind
  rejection through an initialized startup pool. The fixture passes native compilation and
  AllRules analysis; clang-tidy still fails buffer-access and included-header
  diagnostics. It has not executed and no new Release binary has been linked.
  Native failure injection and full application lifetime proof remain open.
- Proxy state, execution and scratch identity now belong to program startup in
  GUI and CLI. `Proxy_Initialize` establishes a resident OS memory owner, persistent
  OS thread and OS temporary file; `Proxy_Start` validates/copies a bounded request
  and submits that worker. Submission contains no proxy allocation, native thread
  or temporary-file creation, or CRT formatting. The callback's GPU/container/HTTP
  dependencies still allocate and block. This is not whole-playback acceptance.
- Per-job cancellation uses `OS_Atomic` through fragmented conversion. Callback
  completion does not permit reuse until the retained serving owner has stopped;
  successful `Proxy_TryStop` ends the session borrow and keeps startup resources
  for the next cast. `Proxy_TryClose` ends execution before removing the scratch
  file and releasing state. Failed cleanup retains the appropriate owner. Scratch
  contents remain until reuse/final closure; a conversion failure preserves the
  original-stream fallback. GUI/CLI retain optional failed startup for final close.
- `os_temporaryfile` owns native filesystem creation/removal in its Win32 backend,
  using generic Win32 text operations with build-selected UTF-16 filesystem
  encoding. Failed removal retains path identity and a sticky nonzero error;
  repeated successful closure performs no native call. Filesystem removal timing
  and provider allocations remain unproved. This owner does not make conversion
  file I/O or native HTTP serving allocation-free.
- Proxy source/address validation uses bounded Unicode and URL specification
  readers. URL publication uses typed `utl_url` components and counted decimal
  serialization. Review corrected request dataflow, release-before-reuse ordering,
  temporary-file failure evidence and first-use declarations. Existing GPU plane
  and motion offsets now widen before arithmetic; padded chroma allocation sizing
  widens before rounding. Shared Vulkan/Win32 declarations now come from the full
  Vulkan SDK header before Volk, removing the duplicate LPCWSTR annotation finding
  without changing Volk or excluding diagnostics. GPU ownership remains unfinished.
- Two new fixtures cover temporary-file identity/sharing-failure retry and 32
  proxy reuse cycles, malformed requests, immediate cancellation, natural empty-file
  conversion rejection, retained storage identity and repeated final close. Native
  compilation/AllRules passes for proxy ownership, temporary-file ownership and
  both fixtures; strict clang-tidy still rejects buffer access and included-header
  findings. The fixtures have not executed. Successful GPU conversion, receiver
  playback, native failure injection and allocation tracing remain unverified.
- Latest logs: `build/cast-proxy-fixtures-configure.log`,
  `build/cast-proxy-selected-review.log`, `build/cast-temporaryfile-selected-review.log`,
  `build/cast-proxy-fixture-review.log`, `build/cast-temporaryfile-fixture-review.log`,
  `build/cast-server-proxy-owner-review.log`, `build/cast-proxy-gpu-review.log`,
  `build/cast-proxy-gpu-tidy.log`, `build/CastDemo-proxy-owner-review.log` and
  `build/CastDemoCli-proxy-owner-review.log`. The GPU native check stops at C6262
  for its existing 137,980-byte probe frame. Discovery SDK C6101, GUI SDK C4668/C4255,
  console fopen C4996 and linked Volk C4191 also remain open. Evaluated Release
  databases cover 25 format and 20 session sources. Policy inspection covers 38
  C targets; all 29 auditor regressions pass. No new Release executable was linked.
- `BuildPolicy.targets` explicitly disables deprecated minimal rebuild, including
  selected-file compilation. The native command records `/Gm-` alongside `/Wall
  /WX`, AllRules, `/O2`, `/GL` and `/Ob3`; the earlier stale-analysis warning is no
  longer present. This corrects compiler configuration without suppressing a
  diagnostic or granting native-only acceptance while clang-tidy fails.
- HTTP serving now establishes a shared pool at program initialization: 32 listener
  slots, eight client workers with fixed HTTP buffers and one admission worker.
  GUI/CLI originals, subtitle hosts, gain variants and proxy output use that pool.
  No heap allocation, subtitle byte copy or native thread creation remains in
  listener/client startup. Native socket/accept/file creation remains a separate
  post-initialization allocation defect.
- Listener starts reserve and publish slots transactionally. Admission owns accept
  and native cleanup; acquired worker completion precedes file/socket release,
  and successful release precedes client/slot reuse. Failed release retains the
  native identity. A failed admission worker has an exclusive cleanup recovery
  path. Final shutdown requires quiescent slots before closing workers and storage;
  pending cleanup with no native error is not recorded as a failure.
- WebVTT data is borrowed unchanged through successful listener stop. GUI source
  preparation rejects changes while a session or its cleanup is active; final
  cleanup retires original, proxy and gain listeners before buffer reuse. The
  pool is retained through proxy shutdown, and Winsock through pool shutdown.
  This source lifetime review still needs runtime failure and concurrency evidence.
- The admission scan rotates listener priority. Occupied client workers leave new
  connections in the native backlog; idle/partial request heads have a five-second
  absolute deadline. Response sends, TransmitFile and growing-file I/O can still
  block. Admission scheduling latency/CPU and provider-internal allocation and
  retirement are unmeasured. CRT logging, complete native boundaries and realtime
  transport remain open. The 32-listener bound also limits the unresolved legacy
  accumulation of gain variants; continuous live gain still needs its full migration.
- Server fixtures now include all listener slots, explicit exhaustion, reuse of a
  retired slot, stable pool storage and eight occupied clients followed by a ninth
  queued connection. Final pool cleanup has a bounded test deadline. The proxy
  fixture retains serving through its full lifetime. Server, proxy and fixture
  sources pass native compilation/AllRules; required clang-tidy still fails, so
  these fixtures have not executed and no new Release executable has been linked.
  Current logs: `build/cast-server-pool-final-review.log`,
  `build/cast-proxy-server-pool-final-review.log`,
  `build/cast-server-pool-fixture-review.log`,
  `build/cast-proxy-server-pool-fixture-review.log`,
  `build/CastDemo-server-pool-review.log` and `build/CastDemoCli-server-pool-review.log`.
- The lexical guard detects invisible source/comment control bytes and now records
  pool/proxy startup and closure, native accept, thread closure and temporary-file
  operations as lifecycle boundaries. Boundary counts include declarations and
  definitions; they are not allocation traces. All 30 auditor regressions pass;
  all 38 generated C targets/configurations retain strict analysis settings. No
  warning exemption, new Release build or MISRA acceptance is claimed.
- The installed clang-tidy 22.1.8 has no complete MISRA C:2025 checker. No
  dedicated MISRA checker was found on PATH or in the searched installed-tool
  locations. Full rule/directive coverage, checker execution and manual evidence
  remain open; general C checks and the lexical guard are not a substitute.
  [MISRA_REVIEW.md](MISRA_REVIEW.md) records the whole-program evidence obligation.
- `FORCE_INLINE` is defined by the compiler abstraction and applied to private
  helpers in the new protocol/OS/command/event/message modules. Review of all
  remaining application helpers and final optimized C5045 dataflow remains open.
- Production Cast framing and routing now consume the counted CastV2 model directly;
  the duplicate cast_message adapter is removed. Session/transport IDs use retained
  decoded byte storage and publish only after successful CONNECT. JSON exact-int64
  conversion and namespace/routing fixtures are implemented, but the production
  receiver parser, floating-point conversion, strict analysis and runtime evidence
  remain open. See SPECIFICATION_MODULES.md and MISRA_REVIEW.md for this scoped review.
- MPEG-4 Systems descriptor and AAC AudioSpecificConfig modules now drive the
  production MP4 descriptor paths. The guessed AAC-LC fallback is removed; typed
  configuration supports Main/LC/SSR/LTP, PCE and explicit/synchronized SBR/PS.
  Other codec/profile semantics, strict analysis, unexecuted C fixtures and
  application/dependency runtime allocation remain open. These additions do not
  close MP4, Matroska, tx3g, AVC or the other specification rows. Current evidence
  and exact limitations are in SPECIFICATION_MODULES.md, MISRA_REVIEW.md and
  build/mpeg4-codec-review.json.
- The user requires `utl_mpeg4` as the shared MPEG-4 foundation, consumed by
  `utl_mpeg4systems` and `utl_mpeg4audio`. Those dependencies now use shared spans
  and actual common integer/bitstream operations. The former `utl_mp4` header
  module is retired into this foundation; typed FullBox and file-type operations
  are connected to native readers and mux paths. Container/track/fragment and
  codec semantics, strict analysis, unexecuted fixtures and runtime allocation
  remain open. See build/mpeg4-common-review.json and SPECIFICATION_MODULES.md.
- Shared MPEG-4 `stts`/`stss` models and cursors now drive a two-page keyframe
  indexer without whole-table heap allocation. Full entry validation, overflow,
  missing-table and insufficient-index-capacity behavior are explicit. Other
  tables, native media headers, edit/composition timing, mux entry writers,
  synchronous I/O, OS/resource lifecycle and runtime validation remain open.
  Native compilation passes for this increment; strict analysis still fails.
  See build/mpeg4-tables-review.json and the scoped specification/MISRA ledgers.
- The last pre-policy GUI/CLI builds are in the required Release directory
  (654,336 / 610,304 bytes, 07:18:55 / 07:18:57 UTC). They do not contain all
  subsequent edits and do not satisfy the strict compiler/analysis requirement.
  They must not be presented as the completed build.

Public API ordering starts with initialization, followed by query/derive,
Find/search, then read/write. Accessor names use `Get...`; bare count, capacity,
span, metadata and frame-view names are not accepted. Headers and definitions
must present the same ordering.
The user's exceptional OS namespace is `OS_` for public functions and types,
including existing process/assertion modules and all callers. Filenames remain `os_*`.
The user's concrete correction is: "Utl_Http_FindField should come after
query/derive". Keep declarations and definitions in the same order, including
framing derivation and range-size queries consolidated into `utl_http`.

The user's repeated specification requirement remains open: Cast JSON messages,
SSDP/DIAL, device-description XML, URLs and Unicode, SRT/WebVTT/tx3g, and every codec
configuration format read or written by CastDemo need substantive specification
implementations and production integration. Existing Unicode scalar codecs are
partial progress; their presence does not close Unicode or any dependent format.

Before each change, inspect the current worktree and the relevant source precedent.
After the change, compare the entire affected module with the applicable conventions,
run the necessary checks, and update the evidence. Before completion, re-audit every
row against current source, final binaries and observed runtime behavior. Missing,
indirect or partial evidence leaves the requirement open.

Graphics migration checkpoint: the core `gfx_vulkan.c/.h` layer and standalone
native helper pairs are retired. The API guard preserves the 23 higher-level
public signatures; all 313 low-level declarations and their wrapper modules are retired.
gfxlib itself is retained. Native
platform wrapper modules are removed and sharing construction belongs to the image
owner. Win32 imports now use CVK's existing image/buffer constructors; the corrected
CVK protocol template and generated outputs remain reproducible. The owned
`gfxlib` target is configured with the same strict gates. Selected surface and
Win32 interop units reach clang-tidy, but owned diagnostics and imported support
dependencies still prevent a successful library build. Canonical CastDemo use,
allocation/lifetime evidence and the validated Release binary remain open. Exact
scope and current logs are recorded in GFX_MODULES.md.

VA image mapping now calls cva from the image owner and uses construction-time,
validated plane geometry for packed transfers. The three former image-copy/layout
wrappers are retired. Native compilation and MSVC analysis pass for the new private
layout module; four unsafe-buffer clang-tidy diagnostics keep its strict gate failing.
The consuming image source still stops at the missing imported allocator header.
No runtime fixture, provider timing/allocation proof or validated Release is established.
The image owner now uses cva directly for synchronization and diagnostics. The supported
cva recoverable-error policy returns every specified diagnostic query failure separately
from the primary synchronization result. Configuration verifies that only that function's
result policy changes; upstream checkouts are untouched. The configured Win32 library
build and regeneration check pass, and the DRM policy generates in memory. Diagnostic
record traversal is bounded and reports its limit. Imported logging, provider allocations,
blocking synchronization and native runtime validation remain open; retiring the wrappers
does not resolve those requirements.

The encoder's packed-header, IQ-matrix, rate/frame-control and remaining five
H.264 parameter/sizing wrappers are retired. Native pictures and slices use cva;
encoder policy consumes retained configuration and picture state with shared
H.264 derivations. Complete native reference lists and bounded weight prefixes
replace generic wrapper arguments. Status now polls completion and validates
the complete bounded segment chain before copying, retaining undersized-capacity
submissions for retry. These changes remain unvalidated by native compilation:
the selected encoder build now gets past H.264 and stops at missing os_malloc.h. Structural API, dependency
call and build-policy checks pass; none establish runtime correctness, provider
allocation behavior or completion of the graphics migration.

H.264 and Exp-Golomb support are integrated as owned specification modules over
the shared MPEG-4 bitstream operations. The retained H.264 encoder models and
serialization are partial; original production H.264 readers are still unmigrated.
Exp-Golomb passes selected native compilation, analysis and clang-tidy. H.264
passes native compilation/analysis and fails clang-tidy. No runtime codec fixture,
sanitizer run or graphics release is established by those translation-unit checks.

CVK workspace/count/header checkpoint: generated output enumeration arrays and
large extension-property arrays now use caller storage, retaining existing query
capacities. This changes the affected CVK helper signatures without changing the
frozen gfxlib interface. Every two-call enumeration retains its initialized extent
and checks the returned count against it. The core selected compile clears the
reported native stack warnings and uninitialized queue-family read; clang-tidy
remains failing. Public headers and all owned consumers now use `cvk/` or `scvk/`.
Generation reproducibility, eleven generator-option tests, platform emission and
Python binding generation pass. See GFX_MODULES.md for the exact scope; canonical
application integration, dependency lifetime/allocation behavior and Release remain
open.


## Current native build follow-up (2026-09-28, late evening)

- The whole `build/windows-msvc-2026/cvk.slnx` Release/x64 build with native
  `/m` completed with exit code 1. `build/whole-slnx-native-current.log` records
  24 failing projects at that checkpoint; this is not build acceptance.
- Subsequent complete native builds and executable runs pass for the atomic,
  compiler-memory ABI, temporary-file, and format fixtures. The ABI executable
  imports only Kernel32. The atomic worker relinquishes each retained handle
  before joining/closing it. Temporary-file fixtures bound their memory accesses
  before validating returned lengths. Protobuf private readers now consume spans
  after the validated tag, rather than receiving a second, range-checked input
  cursor. Public record lengths still include that tag.
- The mailbox query-load experiment did not resolve its LTCG Spectre diagnostics
  and was reverted. Mailbox findings remain open.
- The user reproduced a new failed Debug CVK build that continued analyzing
  `cvk_json.c` after `cvk_win32_json.c` failed. The installed native ClangTidy task
  inherits Microsoft's MultiToolTask; there is no repository runner or scheduler
  override. Visual Studio's native Build.Cancel did not promptly terminate the
  remaining analyzer. That exact process was terminated, and the IDE subsequently
  reported BuildState 3 (done), LastBuildInfo 1. Automatic failure cancellation
  remains unresolved; manually stopping this run is not a persistent fix.
- ImmersiveWindowDemo remains blocked by generated CVK JSON diagnostics. No
  generated source or generator was changed. Approval is pending for the exact
  six additional diagnostic categories shown in the user's log, or alternatively
  for generator fixes. The existing compiler-padding exception now also covers
  Clang's separate padding analyzer. No other new diagnostic exception was added.


## Native analysis integration follow-up (2026-09-29)

- The user explicitly retained the prohibition on custom build hooks, including
  cancellation hooks. `scripts/run_clang_tidy.py` remains absent. The remaining
  `BuildPolicy.targets` import and its compiler-metadata rewrite are removed.
- CMake now emits ordinary per-source MSBuild metadata. Source paths are resolved
  against each target's source directory before setting it, including CVK and
  gfxlib targets declared outside CastDemo. A property reference preserves an
  explicitly empty `PREfastAdditionalOptions` value through CMake generation;
  MSBuild evaluation confirms that it removes the inherited `quiet` option.
  `EnablePREFast` stays true and `MinimalRebuild` is false. The native code-analysis
  and Clang-Tidy tasks retain AllRules, warnings as errors, and the existing scope
  of approved exceptions. No toolchain files were modified.
- The native Clang-Tidy processor setting is 0 (all available processors), and
  the Release build preset uses jobs 0. There is no scheduler override. The policy
  guard passes for 45 owned and 31 external C targets and verifies the removed
  runner/hooks remain absent. Evaluated CVK source metadata is retained in
  `build/cvk-native-items.json`.
- The SRT, WebVTT, SSDP, HTTP-policy and compiler-ABI targets build through the
  actual solution with the native configuration, and all five corresponding
  CTest cases pass. Evidence: `build/native-properties-fixtures-current.log` and
  `build/windows-msvc-2026/Testing/Temporary/LastTest.log`. This is a targeted
  solution build, not acceptance of the whole solution or ImmersiveWindowDemo.
- SRT and Unicode readers retain the unconsumed span directly. Failed reads leave
  it unchanged. SRT arrows are compared only within a bounded readable prefix;
  WebVTT copies use their bounded extent before validating the derived count.
  HTTP, WebVTT and SSDP fixtures use independent expected extents for their loads
  and still validate the reported lengths. These changes clear the corresponding
  reported C5045 failures without suppressing that diagnostic.
- The x64 compiler ABI now supplies an owned stack-probe adapter and page-walking
  core, exercised by a volatile multi-page automatic extent. The compiler-ABI map
  resolves `__chkstk` and `CRT_Stack_ProbeCore` to the owned objects; the executable
  imports only Kernel32. Stack-growth behavior during interaction and the broader
  no-allocation-after-initialization goal remain separate open requirements.
- The generator hardcodes `(uint32_t)(sizeof(table) / sizeof(table[0]))` at four
  sites despite providing `VK_COUNT_OF`. The required canonical spelling is
  `VK_COUNT_OF(table)`. This belongs in the generator when generated-code changes
  are authorized; neither the generator nor generated sources were edited here.
- Removing the custom import has not established prompt failure cancellation.
  The native CVK Debug profiling run retains the same configured checks and
  warnings-as-errors policy. It still reports the Win32 JSON failure before core
  JSON analysis completes. Profiling is diagnostic evidence, not a bypass or a
  successful build. The whole solution, ImmersiveWindowDemo, and the overall goal
  remain incomplete.


### Platform analysis dependency and measured failure

- The previous complete native CVK Debug profiling run returned failure after
  421.075 seconds. Its per-check AST timing report accounts for only 5.202 seconds
  in core JSON; that report alone does not identify where the remaining time is
  spent. No check was removed in response to this measurement.
- The generated platform pair now belongs to the ordinary `cvk-platform` OBJECT
  library. `cvk` depends on that project and retains its objects in the same archive.
  Each source compiles once. Both projects retain the identical generated-code
  analysis policy. The normal native dependency prevents starting core analysis
  after platform compilation or analysis has failed. No runner, scheduler, process
  killer, SDK patch or custom build hook implements this dependency.
- Both CVK target declarations specify `/Wall /WX /wd4820` directly. The new
  platform declaration initially copied the previous `/W4` fallback; that line
  and the existing core fallback were corrected after the user's objection.
  Generated project settings verify all warnings and warnings as errors in every
  configuration, with only the approved compiler-padding suppression.
- The actual ImmersiveWindowDemo Debug/x64 project build now returns the existing
  Win32 JSON diagnostic failure in under four seconds. Core JSON analysis does
  not start and no analyzer remains after that run. Evidence:
  `build/immersive-native-platform-current.log`, its `.binlog`, and the associated
  `-result.json`. This fixes the reproduced platform-failure/core-analysis delay;
  it does not claim general cancellation of unrelated already-running tasks.
- A new whole-solution Debug/x64 `/m` build completed with failure; its remaining
  project failures are recorded in `build/whole-slnx-native-platform-report.json`.
  The solution and ImmersiveWindowDemo are not successful builds. Generated JSON
  diagnostics and the remaining owned-source/LTCG failures are still open.
- The policy guard now covers 46 owned targets and verifies the platform project
  dependency, disjoint source lists, retained archive objects and unchanged scope
  of generated diagnostic exceptions.
