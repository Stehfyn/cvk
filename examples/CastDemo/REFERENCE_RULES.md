# Missed Win32X and XML rules

Recorded after rereading the references on 2026-09-27. This is an implementation
defect ledger, not a compliance certificate. Read it with
[REFACTOR_REQUIREMENTS.md](REFACTOR_REQUIREMENTS.md) and
[SPECIFICATION_MODULES.md](SPECIFICATION_MODULES.md). All applicable rules must be
checked together across the full application, not only against the last changed function.

## Sources and precedence

| Reference | Material reviewed |
| --- | --- |
| [Win32X convention.pdf](C:/dev/Win32X/docs/convention.pdf) | All twenty sections: enforcement, text policy, build/analysis, no-CRT code, formatting/tidy, layout, macros, declarations, names, pointers, calls, conditions, switches, crackers, C5045, UI, accessibility and guards |
| [Win32XPolicy.cmake](C:/dev/Win32X/cmake/Win32XPolicy.cmake) | Shared warning and native-analysis policy |
| [Win32X root CMake](C:/dev/Win32X/CMakeLists.txt), [library CMake](C:/dev/Win32X/src/CMakeLists.txt), [example CMake](C:/dev/Win32X/examples/CMakeLists.txt) | Actual compiler/linker configuration, CRT-free entry, optimization, manifest and dependency linkage |
| [WinBaseXText.inl](C:/dev/Win32X/src/WinBaseXText.inl), [WinBaseX.h](C:/dev/Win32X/include/Win32X/WinBaseX.h), [processenvxText.inl](C:/dev/Win32X/src/processenvxText.inl), [processenvx.h](C:/dev/Win32X/include/Win32X/processenvx.h) | Generic-text implementations, A/W selection, startup and command-line contracts |
| [windowsx2.h](C:/dev/Win32X/include/Win32X/windowsx2.h), [windefx.h](C:/dev/Win32X/include/Win32X/windefx.h), [WinUserX.h](C:/dev/Win32X/include/Win32X/WinUserX.h) | Message cracker/forwarder contracts, named predicates, platform types and startup/DPI behavior |
| [main.c](C:/dev/Win32X/examples/main.c), [main_v2.c](C:/dev/Win32X/examples/main_v2.c), [app.manifest](C:/dev/Win32X/examples/app.manifest) | Generic Win32 pump/calls, forwarding, initial theme, DPI, rendering ownership and activation context |
| [theme-paint-message-contract.md](C:/dev/Win32X/docs/theme-paint-message-contract.md) | Conditional dispatch/drawing behavior, themed text, common-control styles, activation context and animation ownership |
| [XML design](C:/dev/xml/docs/xml-design.md), [XML README](C:/dev/xml/README.md), [XML CMake](C:/dev/xml/CMakeLists.txt) | Layering, public namespace, ownership, error distinctions and source/build organization |
| [utl_xml.h](C:/dev/xml/include/utl/utl_xml.h), [utl_xml.c](C:/dev/xml/src/utl/utl_xml.c) | Complete public contracts and source structure; lexical helpers, bounded traversal, named conditions, caller-owned models, parsing/writing and error behavior |
| [utl_arinc661.h](C:/dev/xml/include/utl/utl_arinc661.h), [utl_arinc661.c](C:/dev/xml/src/utl/utl_arinc661.c) | Typed schema models and public contracts; private prototype documentation, aligned workspace partitioning, schema conversion and reset-on-failure |
| [os_types.h](C:/dev/xml/include/os/os_types.h), [utl_defines.h](C:/dev/xml/include/utl/utl_defines.h), [utl_scan.h](C:/dev/xml/include/utl/utl_scan.h), [utl_scan.c](C:/dev/xml/src/utl/utl_scan.c) | Domain types, constants, explicit-width scanning, finite/range checks and explicit loop termination |
| [os_assert.h](C:/dev/xml/include/os/os_assert.h), [os_message.h](C:/dev/xml/include/os/os_message.h), [os_malloc.h](C:/dev/xml/include/os/os_malloc.h), [os_malloc.c](C:/dev/xml/src/os/os_malloc.c) | Invariants versus input errors, bounded diagnostic construction and process-lifetime storage |
| [utl_tests.c](C:/dev/xml/tests/utl_tests.c), [utl_arinc661_tests.c](C:/dev/xml/tests/utl_arinc661_tests.c) | Boundary-oriented specification tests and typed read/write/read checks |
| [arinc661_configuration.h](C:/dev/xml/examples/arinc661_configuration.h), [arinc661_configuration.cpp](C:/dev/xml/examples/arinc661_configuration.cpp) | Complete GUI header and implementation: fixed document/editor models, initialization, typed library boundaries, staged edits, status and pending-action state, drawing and measurement helpers |

The user explicitly requires no CRT, `WinMainStartup`, no allocation after
initialization, no `size_t`, no U/u suffixes, no `while` or `do ... while`, direct mask tests,
character-set-agnostic Win32 operations, and complete convention adherence.
These requirements prevail over contrary individual examples in either repository.

The references are not internally uniform. For example, the PDF recommends static
CRT by default while Win32X's application links without CRT; the PDF forbids local
warning suppression while the current library CMake contains toolchain workarounds.
Some XML support files still use CRT, `size_t` or macro `do ... while` wrappers,
and some example code differs from the PDF's declaration rules. These observations
are not exemptions for CastDemo. Follow the explicit user instructions and the
documented strict rules; do not copy a conflicting artifact as permission to relax them.
The XML design document also describes an earlier implementation stage, so verify
current behavior against the current public contracts and implementation.

## Win32X rules missed or incompletely applied

Every row below remains open across the complete application unless its evidence
explicitly establishes otherwise. A corrected occurrence does not close the rule.

| ID / source | Rule to apply | CastDemo gap or evidence required |
| --- | --- | --- |
| W01 / PDF 1, 19 | Fix reported causes; no local warning disables, weakened warnings-as-errors, disabled analysis/formatting/hooks, optimization removal or guard exemptions | Main targets, tests and linked C dependencies now require `/Wall /WX` and C clang-tidy; the strict build fails and acceptance remains open |
| W02 / PDF 2 | CRLF and final newline for repository text, with editor/Git policy and guards; exclude genuine binary/generated artifacts appropriately | Audit found bare LF in production files; new documentation must also comply |
| W03 / PDF 3; policy CMake | Centralize compiler, warning, runtime and analysis policy instead of independently weakening targets | `BuildPolicy.cmake` applies the shared compiler/analysis policy; full pass and MISRA coverage remain open |
| W04 / PDF 3, 6; explicit full C analysis correction | Enable native AllRules and the full configured C clang-tidy families; analyze actual C translation units with included-header diagnostics visible; findings fail the build | Configuration verified for all 38 C targets; source and included-header findings still fail analysis. Complete MISRA guideline coverage and a successful complete build remain open |
| W05 / PDF 4, 20 | Repair C5045-sensitive dataflow structurally; keep optimization and warning checks active | No whole-program C5045 review; early bounds checks alone are not evidence |
| W06 / PDF 4; Win32X startup/link sources | No CRT startup or explicit/implicit CRT helper dependencies; check link map and imports, including x86 integer helpers and math/compiler-generated calls | GUI still enters `wWinMain` and uses CRT. Format, protocol and HTTP tests link with `/NODEFAULTLIB` and `WinMainStartup`. The HTTP test exposed an LTCG-generated `memcpy` in the span writer; a descending counted copy removed it without weakening optimization or checks. This is evidence for those executables only |
| W07 / PDF 5; explicit argument-layout correction | Run clang-format plus a deterministic post-pass for declaration/member/operator/call/dispatch alignment; inspect final output. Wrapped parameter/argument lists use one item per line, without binpacking | Wrapped lists across CastDemo and its tests were corrected; a lexical guard detects shared item-start lines. Broader custom-type and repeated-row alignment still needs full review |
| W08 / PDF 7 | Includes, macros, source types, prototypes and global/static data all precede function definitions | GUI crackers near `WindowProc` and other mid-file declarations still violate this; the gain macros have been moved into section 3.1 |
| W09 / PDF 8 | Align contiguous object-macro replacement columns; place macros in the XML 3.1 section | Incomplete sectioning and misplaced definitions remain |
| W10 / PDF 9 | Stack declarations belong at each block's beginning; one declarator, no initializer, no declaration in `for`; no naked temporary scope | `audio_gain.c` and many GUI/container routines still violate these requirements |
| W11 / PDF 9 | Declare locals in first-use order; put `UNREFERENCED_PARAMETER` immediately after the declaration preamble | New modules also need this audit; a mechanically aligned list is insufficient |
| W12 / PDF 10 | Names describe the quantity; avoid generic geometry names; extended structures use the extended abbreviation; use the prescribed array-count helper | GUI uses `WNDCLASSEXW wc` and bare dimensions; repeated manual array-size expressions remain |
| W13 / PDF 11 | Write dereferenced values as `(*p)` and `sizeof((*p))`; separate increments from reads/writes | Audit found unparenthesized `sizeof` dereferences; existing wire code increments cursors inside indexing |
| W14 / PDF 12 | Compute selected values before calls; no ternary arguments | MF media setup/write calls, control setup and other paths contain ternary arguments |
| W15 / PDF 12 | Align corresponding columns of repeated calls, comparison assignments, arithmetic assignments, guard returns and dispatch rows | Main message table and repeated GUI calls are not aligned as required |
| W16 / PDF 12; generic-text sources | Use generic Win32 API names with matching `TCHAR`, `LPTSTR`/`LPCTSTR`, generic structures and `TEXT` literals | GUI and filesystem/worker paths hard-code W APIs and types; `UNICODE` being defined does not prove agnostic source |
| W17 / generic-text sources | Share generic implementations across A/W selection; isolate real encoding differences at documented boundaries | Need narrow/wide compile checks and conversion tests; UTF-8 protocol data must retain its explicit wire encoding |
| W18 / PDF 13 | Constants go left in comparisons; use boolean questions directly or negate them, not TRUE/FALSE comparisons | The current scan finds no TRUE/FALSE comparisons after the worker correction. Ordered comparisons and semantic boolean usage still need whole-source review |
| W19 / PDF 13 plus user override | Presence masks are not zero comparisons; the user's direct mask condition is accepted; canonicalize a stored boolean where needed | Three mailbox mask comparisons were corrected; other masks and boolean ternaries still need review |
| W20 / PDF 13 | Break complex conditions into domain questions; parenthesize comparison/negation terms inside compounds; align comparison operators | Named-question conversion is incomplete and some new loops still negate compound bounds mechanically |
| W21 / PDF 14 | Sort all switch cases by resolved numeric value, including message rows | Main `HANDLE_MSG` table is out of order; protocol switches need the same review |
| W22 / PDF 15 | Both window and subclass procedures use canonical crackers | Main window conversion is partial; `SeekBarProc` and subtitle subclass still decode raw messages |
| W23 / PDF 15 | Direct handlers use the window-class prefix plus `_On*`; distinct messages have distinct thin direct handlers, with shared logic elsewhere | Unprefixed `OnCreate` etc.; several WM_CTLCOLOR messages share one direct handler |
| W24 / PDF 15; windowsx2.h | Missing HANDLE/FORWARD pairs belong in a shared header, each with its callback signature and composition guards | Custom crackers remain mid-file and lack corresponding forwarders |
| W25 / PDF 15; main.c OnCommand | Preserve required default processing explicitly through the matching forwarder, including subclass cleanup paths | Must check each handled return against the old behavior; indiscriminate zero returns are not safe |
| W26 / PDF 16, 20 plus user override | No `while` or `do ... while`, including macro wrappers; use conventional bounded `for` or explicit termination in `for (;;)` with safe bounds and progress | Both forms are absent from the 57 production C/header files and seven Cast test sources. The audit rejects both tokens outside comments/strings, including macro bodies; a regression guard scans all 64 files. This closes this syntax finding, not the broader realtime or state-machine requirements |
| W27 / PDF 16, 20 | Runtime indexes need bounded traversal or named safe values; capacity append/reserve paths and returned lengths need safe dataflow, not only a guard branch | Container tables, new field readers and application indexes still require semantic/analysis evidence |
| W28 / PDF 17 | Platform, renderer, layout, controls and visual style have separate responsibilities; canonical retained geometry drives size/DPI changes | GUI currently combines controls, jobs, painting and protocol setup in one module |
| W29 / PDF 17 | Coherent painting, deliberate dirty rectangles and legible text; do not disguise defects with redraw suppression or competing surfaces | Paint/erase/subclass ownership and resize behavior require a complete review |
| W30 / PDF 17 | When a module owns a custom retained surface, follow the owner-draw restrictions on visible standard controls and broad deferred/redraw operations; use an appropriate renderer for high-refresh work | Applicability and retained-surface design must be resolved explicitly; existing hybrid control painting has no completed evidence |
| W31 / PDF 18 plus dark-mode request | Theme invalidation includes settings, theme, system colors, DWM colorization, high contrast and DPI; update resources without losing logical state | Main dispatch lacks the full theme/high-contrast handling; initial dark colors alone are insufficient |
| W32 / PDF 18 | Keyboard and UIA coverage includes names, roles, IDs, bounds, focus/enabled state, patterns and change events for every interactive/status surface | No UIA or complete keyboard test evidence |
| W33 / WinUserX; startup sources | Respect startup show/placement/DPI contracts and consistent structures; do not force foreground or guess startup state | Entry and first-show path need review with the requested `WinMainStartup` |
| W34 / main.c; message pump | Preserve accelerator/dialog translation, normal dispatch, quit and failure handling; do not spin or block the UI on worker completion | Pump is hard-coded W; gain/preview cancellation and close still contain blocking waits |
| W35 / manifest; theme contract 3, 4, 6 | Verify common-controls v6 activation context; handle absent theme handles; request owner-draw styles and honor custom-draw return semantics | New executable/paint paths have no recorded manifest/runtime verification |
| W36 / theme contract 0Ã¢â‚¬â€œ4b | Do not assume unforwarded messages were processed, DWM handles sizing/paint, or conditional owner-draw callbacks occur every frame | Needed for any theme/paint refactor; these are conditional behavior contracts, not a reason to add unused animation |
| W37 / theme contract 3, 4b | A themed surface needs coherent text rendering; if buffered animation is used, continue the active animation first and do not start competing animations; no GUI-thread DwmFlush | Apply if those APIs/features are used; the no-post-init-allocation requirement still applies to animation resources |
| W38 / PDF 19 | Guards must cover line endings, circumvention, layout/style, formatting, analysis and applicable owner-draw rules | `audit_cast.py` is only a partial lexical guard; it cannot certify these requirements |
| W39 / goal; release CMake precedent | Preserve aggressive optimization and measure executable size, memory, CPU and latency; emit final artifacts in the designated build tree | No final performance comparison or fully compliant release exists |

Character-set-agnostic Win32 does not mean treating UTF-8 HTTP/JSON/subtitle bytes as
`TCHAR`. Platform text and specified external encodings are separate contracts.
An API with only a wide form, such as a specific COM interface, needs a documented
conversion boundary. Generic CRT macros such as `_tcslen` are not a no-CRT solution.

## XML rules missed or incompletely applied

| ID / source | Rule/precedent to apply | CastDemo gap or evidence required |
| --- | --- | --- |
| X01 / utl_xml.h and utl_arinc661.h sections 1Ã¢â‚¬â€œ3.5; source sections 1Ã¢â‚¬â€œ5 | Complete numbered source/header layout, including empty prescribed sections; macros/types/globals/prototypes in their respective places | Many old and new control modules only have fragments of the layout |
| X02 / public contracts in both headers | Every public function needs an accurate brief, details where needed, numbered requirements, parameter direction/meaning and specific returns | Most existing APIs have a one-line comment or no contract; new controls remain incomplete |
| X03 / utl_arinc661.c prototype section; implementation Details blocks | Document private prototypes before implementations; implementation comments explain mechanics without replacing public contracts | `cast_commands.c` and the new message/format modules have prototype preambles; `cast_events.c` and other existing modules still need complete contracts and structure |
| X04 / type/member docs and schema citations | Document each public model/member's domain, unit, ownership and meaning, with actual specification productions/clauses | Existing MP4/MKV/codec structs largely expose implementation details instead of documented spec models |
| X05 / XML design Naming; public headers | Public types/functions carry the module prefix; struct/enum tags and typedefs follow the `_s`/`_e`/`_t` pattern; private helper names stay private | New worker/mailbox/command type naming still follows older unqualified names |
| X06 / os_types.h; typed library models | Use `bool_t`, `byte_t`, `char_t`, `float32_t`/`float64_t` and explicit-width domain integers; platform ABI types stay at boundaries | Many booleans are `int`, media times are raw `double` and counts remain `size_t`/unspecified unsigned |
| X07 / counted XML spans and ARINC capacities | Keep pointer, count, capacity and unit separate; use 32-bit small counts and 64-bit large byte spans; check before narrowing | `size_t` removal is unfinished; a bulk rename would hide overflow mistakes |
| X08 / utl_xml.h section 3.1; utl_xml.c lexical helpers and grammar tables; explicit user correction | Parser/writer character terminals, delimiters, keywords and bounds use named module constants in section 3.1. Documented static grammar tables belong in section 3.4 and may contain their literal ranges | Replaced raw characters throughout `utl_http` and its consolidated range code; named HTTP tokens, status/version bounds and derived widths follow the same rule. `utl_decimal` digit terminals are named too. The audit rejects character literals inside specification function bodies and tests escaped characters, comments, strings, macros and tables. Token meaning, numeric grammar bounds and the rest of the application still require semantic review |
| X09 / utl_xml.c InRange/DecodeUtf8; ARINC HasElementName | Readable named questions express separate validity decisions, followed by staged work | Long compound expressions remain; boolean names alone do not excuse opaque expressions |
| X10 / utl_xml.c traversal; utl_scan.c | Conventional bounded `for` and explicit `for (;;)` termination; traversal limits and progress must be visible; user forbids both `while` and `do ... while` | Converted production and Cast test loops, including gain/test macros. Kept I/O calls explicit, preserved cursor progress, removed post-decrement underflow in MP4 runs, and distinguished GetMessage failure from WM_QUIT. Remaining complex conditions and lifecycle behavior still require review |
| X11 / XML design Goal and scope | Generic syntax belongs below protocol/schema interpretation; ARINC rules do not leak into XML | `utl_chromecast` now consumes `utl_json`; `cast_session` supplies application policy. JSON parsing, the remaining Cast messages, socket/session logic and container I/O still need migration |
| X12 / utl_arinc661.h typed Configuration/Definition | A specification module provides substantive typed domain models and readers/writers; string searches or generic nodes alone do not implement a schema | Format inventory remains largely unimplemented; MP4/Matroska header primitives are partial |
| X13 / InitializeXmlWorkspace, utl_arinc661.c:580 | Caller supplies aligned bounded workspace; partitioning checks size/alignment, then establishes fixed typed storage | Heap-backed subtitle/container/mux/GPU paths and per-operation storage remain |
| X14 / ReadConfiguration and ReadDefinition contracts | Temporary parse representation does not escape into long-lived typed results; string and workspace ownership is explicit | Borrowed pointers, file mappings, queued payloads and converted-text ownership need full tracing |
| X15 / XML design Tree model; LOGASSERT contracts; explicit correct-by-definition correction | Initializers establish valid objects. Missing required storage/callbacks, invalid kinds and violated invariants assert in every build; malformed external input, capacity limits and OS resource failure remain ordinary results. Carry this across all changed APIs | Always-active CRT-free assertions now guard the HTTP initializer, worker/events/commands/message construction and new reader/date/argument/process entries. Release misuse tests exercise the fatal path. Legacy nullable-error policies, complete object-state invariants and OS failure paths remain open |
| X16 / AppendChild/AppendNode; bounded ancestor traversal | Preserve order, valid parent/owner relationships and initialized/unlinked preconditions; reject duplicate/cyclic or invalid state | Apply equivalent ownership invariants to queues, workers, tracks and container trees, with tests |
| X17 / ARINC read failure contracts | Specify what remains valid on failure; clear/preserve published counts/results according to that contract, and never expose stale partial state as success | Header tests cover a small transactional case; media/session/error paths lack whole-state proof |
| X18 / Read/WriteDocument contracts | Document mutation of input, terminators, required extra bytes, disjoint buffers and borrowed lifetimes; byte output count is not a string length | Existing subtitle/HTTP/media APIs mix terminated and counted data without sufficient contracts |
| X19 / utl_scan.c numeric checks; typed ARINC conversions | Validate syntax separately from numeric conversion, check full consumption where required, range/finite checks before narrowing, explicit defaults | CRT scanning/formatting and ad hoc timestamp/descriptor parsing remain; hand-written replacements need edge-case proof |
| X20 / XML design Remaining work; current header Details | State precisely what grammar/semantics are implemented; test/model presence is not full processor conformance | Do not describe Box headers as an MP4 library completion or partial Cast fields as complete Cast coverage |
| X21 / source declaration/assignment clusters | Follow local naming and alignment precedent, including snake_case domain locals, documented camelCase model fields, one declaration at a time and separate assignment streams | Formatter defaults and old variable lists do not establish adherence |
| X22 / XML/ARINC tests | Test specification boundaries, invalid gaps, capacities and typed read/write/read behavior; checks remain active in optimized test builds | New protocol work needs independent vectors plus production integration; passing unrelated tests is insufficient |
| X23 / OS helpers and explicit platform/toolchain/target correction | Keep OS services, native startup/types and compiler details behind OS/compiler interfaces. Shared utilities use fixed-width domain types and bounded hand-written operations | Process startup, command-line access, error policy, termination and assertions now have OS interfaces/backends; argument syntax and bounded string measurement have utilities. The two new test bodies use these interfaces. Remaining application/control/test platform coupling and POSIX OS startup/conversion remain open |
| X24 / goal plus caller-owned storage precedent | Storage belongs to the initialized owner; allocation cannot be hidden in a helper, worker, codec, file map or dependency | `malloc`/`calloc`/`realloc` and indirect resource creation after startup remain open defects |
| X25 / explicit user clarification; XML public query/derive precedents | Public API order is initialization, query/derive, Find/search, then read/write; preserve matching declaration/definition order | `Utl_Protobuf_GetFieldByteCount` precedes every reader/writer. `Utl_Http_FindField` follows all queries, including framing derivation, range resolution and size queries. Audit guards and regression cases check these operation categories, not alphabetical order or only the immediately following consumer |
| X26 / XML function-definition preambles; explicit user clarification | Every C function definition has the full asterisk separator and multiline Doxygen `\par Details:` block, with `None` only when there are no additional implementation notes | The inventoried production C definitions have both blocks; current counts are in REFACTOR_REQUIREMENTS.md. The guard checks attachment and layout; accuracy of contracts, native assembly documentation and the broader requirements remain separate review obligations |
| X27 / utl_xml.h and utl_xml.c result contracts; explicit user correction | Use aligned `\retval` entries for each result value/range and separate failure cases; use `None` for void. Never use `\return`, `\returns` or their `@` spellings | Corrected tags throughout CastDemo and Cast C tests. The audit scans comments and distinguishes literal test data. Full contract completeness and semantic accuracy still require review |
| X28 / explicit target-implementation correction | Publish `Utl_Arguments_Read`, with `utl/win32/utl_arguments.c` and `utl/posix/utl_arguments.c` selected by the build. Do not encode target selection in public function names or preprocessor branches | Both implementations share the typed record interface. Win32 parses command-line quoting; POSIX preserves argv record boundaries. Backend API order is checked against the shared header. Other target-specific modules require the same architectural review |

## GUI rules from arinc661_configuration

The OS namespace is an explicit user exception to the reference naming precedent:
public OS functions and types use `OS_`, not `Os_`. Apply this to all existing and
new OS modules and their callers; lowercase `os_` filenames remain unchanged.

The user also requires a hand-written `crt` library with `CRT_` function names.
The latest explicit corrections require public initializers first and `Get...`
accessor names throughout the changed tree. This takes precedence over earlier
summaries that placed every query before initialization. Predicates retain `Is`/`Has`;
headers and definitions must match, with queries before search and read/write.

Compiler-mandated ABI spellings belong only in the selected backend. Owning those
implementations does not close remaining runtime, allocation, portability or performance
findings; see [crt/README.md](crt/README.md).

The user's required OS foundation is recorded in [OS_MODULES.md](OS_MODULES.md):
`os_thread`, `os_fiber`, `os_mutex`, `os_localmutex`, `os_semaphore`,
`os_localsemaphore`, `os_queue`, `os_ringbuffer` and `os_atomic`. Apply X15, X23, X24 and the
build-selected implementation rule to every one. All nine remain open; application
workers/mailboxes and renamed native calls do not complete the shared abstractions.

The user's `arinc66_configuration` reference resolves to the two files above.
Their architecture and conventions apply to CastDemo's Win32 GUI. ImGui, CRT calls,
`size_t`, and synchronous file operations in that example do not override the
explicit Win32X, no-CRT, no-post-initialization-allocation and realtime requirements.

| ID / source | Rule/precedent to apply | CastDemo gap or evidence required |
| --- | --- | --- |
| G01 / header Document_t and Editor_t | Give each owner an explicit typed model with fixed arrays, workspace, counts/capacities, selection and pending-operation state | `cast_gui.c` mixes application, control, conversion and preview state in globals; ownership needs a complete redesign |
| G02 / Initialize, source:3574 | Initialize every slot and bind each library model to its owner's storage at startup; closing/reusing a slot resets logical state | Per-selection, per-cast and dependency resource creation remains allocation debt; resetting a worker does not establish this rule |
| G03 / LoadConfigurationFile; UnloadConfigurationFile | Separate GUI orchestration and OS I/O from pure typed specification readers/writers | GUI/session/container parsing is still interleaved; calling an OS codec does not implement a specification library |
| G04 / CreateDocument; AddScreen through AddUserApplication | Establish capacity and domain validity before mutation; initialize the new slot before publishing count/selection/status | Audit file selection, discovery results, track changes and cast startup for partial publication and stale state |
| G05 / DrawHierarchy, source:2019; DrawDocumentTabs, source:2814 | Record structural changes during traversal and apply them after traversal; repair selection/parent indices after compaction | Control callbacks and worker events need explicit mutation boundaries and generation/lifetime checks |
| G06 / DrawIdentifierField, source:2265 | Edit a candidate, validate it, and only then commit it to the model; failed validation preserves the previous value | URLs, gain, seek targets, selected tracks and device addresses need consistent validation/commit behavior |
| G07 / EditorStatusToString; MarkDocumentDirty; DrawStatusBar | Centralize typed status-to-text mapping; distinguish editor-wide and item-specific status; invalidate stale status on relevant changes | Scattered literal messages and loosely related flags obscure CastDemo's transitions and failures |
| G08 / SaveDocument; SaveAllDocuments; DrawSaveAsDialog | Model pending actions explicitly and clear/continue them only after successful completion or explicit cancellation; failure preserves usable state | Cast preparation, reconnect, stop and shutdown still need complete idempotent transition and cancellation review |
| G09 / UpdatePreviewMeasurements; DrawScreenInspector | Cache derived model data and recompute it when the underlying fields change, not on every render | Avoid repeated container probing, conversion, geometry derivation and blocking work in GUI handlers |
| G10 / MeasurePaneWidths; ClampPaneResize; DrawScenePane | Derive layout from actual text/control metrics, clamp geometry, guard empty/invalid dimensions, and keep coordinate calculations named and staged | Fixed pixel assumptions, DPI/theme changes, seek geometry and preview sizing need review |
| G11 / Draw*Field, Draw*Pane and DrawEditor | Split repeated control behavior and individual views into documented helpers; keep the top-level GUI function as readable orchestration | `cast_gui.c` is still a large mixed-responsibility module; canonical class-prefixed HANDLE_MSG dispatch is additionally required |
| G12 / source sections 3.1, 3.2 and 3.5 | Put IDs, labels, defaults, flags, capacities and colors in prescribed sections; type the state; document all private prototypes and every definition | Function Details blocks alone do not satisfy GUI structure, public/private contracts or alignment |

These are recorded defects and review criteria, not a claim that `cast_gui.c` now
follows them. Query/derive-before-read/write ordering and all explicit user rules
continue to take precedence where an individual reference function differs.

## Acceptance evidence still missing

The user's final compiler/analysis correction requires `/Wall /WX`, with only
compiler-introduced padding warnings suppressible in MSVC. The user later approved
two Clang exceptions to preserve bare `\retval None` and existing parameter names:
`-Wno-documentation` and a minimum parameter-name length of one. A subsequent
explicit approval adds `-Wno-unsafe-buffer-usage` and exclusion of only
`bugprone-easily-swappable-parameters`. Bounds/path analysis, sanitizers and
other checks remain enabled. No `/QSpectre`, other warning suppression or circumvention; no
`NO_INLINE`/equivalent; and `FORCE_INLINE` for
inline helpers. C5045 requires valid-by-construction addresses and corrected load
ordering, not an unchecked speculative read. Clang-tidy must be fully enabled
for C and fail the build on findings. MISRA C:2025 rule/directive coverage,
dedicated analysis and manual evidence remain required beyond clang-tidy.
The whole-program evidence obligation and current checker/inventory gaps are
recorded in [MISRA_REVIEW.md](MISRA_REVIEW.md).
The same correction applies throughout the affected build and its dependencies.

The early-return correction follows the staged validity and single final result
used by `utl_xml` and `utl_arinc661`. Audit failure cleanup and output publication
across the whole changed tree; do not substitute `goto`, one-iteration loops,
deep mechanical nesting, or a smaller allowlist for that review.

The current source and binary do not satisfy this ledger. Required evidence includes
the entire source/style/documentation review; all format-library implementations and
production migrations; both generic-text compile paths; warning/analysis results;
no-CRT final GUI/CLI maps and imports; post-initialization allocation traces including
dependencies; latency/throughput/footprint measurements; UI/theme/accessibility checks;
retained-feature tests; and fault/cancellation/repetition tests for complete state
machines. MISRA C:2025 still needs the complete applicable rule/directive inventory,
qualified analysis and manual evidence.

The allocation requirement is unchanged:

> Allocating after initialization violates the goal, including allocations hidden in playback, discovery, subtitles, conversion, and dependencies. Those paths are still open defects. Moving allocation to a worker thread does not satisfy the requirement.

Do not mark a ledger row complete based on a renamed type, a new header, a local
formatter pass or an intermediate executable. Attach evidence from the final current
implementation and recheck dependent rows.

URL review applies the same reference conventions: initializer first, queries
before reading/writing, matching helper declaration/definition order, named
syntax macros, aligned declarations/assignments, explicit-width counts, final
results, numbered contracts and definition Details blocks. Shared component
storage assertions distinguish caller misuse from malformed wire syntax.
No new CRT call, allocation or diagnostic exemption was introduced by the URL
module. This is scoped source evidence; the complete ledger remains open.

The XML migration applies the same ownership and syntax separation to discovery:
startup-bound records, counted immutable source, distinct malformed-input results,
misuse assertions, an iterative parser and publication only after complete input
validation. Its namespace-aware UPnP display adapter is separate from XML grammar.
Private declaration/definition order, named syntax constants, explicit boolean
results, member documentation, aligned grammar tables and full Details blocks
were reviewed against the actual `C:/dev/xml` sources. These scoped changes leave
full XML/UPnP coverage, strict analysis, MISRA evidence and whole-program rules open.


The HTTP serving migration applies these rules at the production boundary too:
typed request/response metadata, counted source borrows, terminal-result latching,
whole-head validation before dispatch, named response fields and preflight before
serialization. Initialize functions precede queries and reads/writes; header and
source operation order match. Misuse assertions, numbered contracts and definition
Details blocks accompany the application policy. Full reference/MISRA review,
native lifecycle ownership and CRT removal remain open across the complete server.

The retained route reader applies the platform boundary rule to another production
path: numeric addresses enter the OS interface, byte-order and socket operations
remain in `os/win32/os_network.c`, and counted textual grammar stays in `utl_url`.
The application adapter owns bounded string presentation only. GUI/CLI startup
and shutdown now retain the route owner, with independent UI/discovery state.
Native query timing, complete application ownership and all reference/MISRA
requirements remain open; this is scoped integration evidence.

Shutdown changes use named stage results, retained pointer ownership, zero-time
thread observations, numbered public/private contracts and Details blocks.
New private stage helpers use FORCE_INLINE; declarations follow first use and
assignments/wrapped calls are aligned without binpacking. Proxy source and header
now use caller-owned startup lifetimes, explicit-width request fields, matching
initialize/query/work/close API order and numbered contracts/Details blocks.
The temporary-file backend separates native filesystem ownership from proxy
policy, using generic Win32 text APIs and a build-selected filesystem encoding.
Request syntax failure is ordinary; required-storage/session misuse asserts.
All local declaration groups were checked against first non-member use, avoiding
incorrect ordering caused by matching similarly named structure fields. Server
validation uses the same misuse/runtime distinction. No complete GUI, native
boundary, GPU conversion or MISRA acceptance follows from these scoped changes.

The HTTP pool keeps Initialize first, uses OS ownership and atomic publication,
and supplies numbered contracts and definition Details blocks for its lifecycle
stages. Required storage misuse asserts separately from invalid media input,
exhausted slots and native failure. Listener/client storage is established once
at startup; immutable subtitle borrows end only after successful stop. Review
also corrected a misplaced public contract and removed accidental control bytes
from comments; the lexical guard now detects those bytes. Strict diagnostics and
legacy server/application conventions remain open, as do native allocation and
the complete reference/MISRA review. This is not whole-module acceptance.

## Latest ownership and protocol corrections

- The user prohibits globals. Explicit startup contexts must reach borrowers
  through parameters and native callback userdata. Global pointers, local statics,
  TLS and singleton accessors are not substitutes. All borrowers must be quiescent
  before context storage is released. The GUI/CLI migration is in progress and is
  not a whole-tree proof; console cancellation integration is still unfinished.
- Each separate bound, presence and validity question needs its own `bool_t`
  before composition. A single name for a compound comparison is insufficient.
- Counted protocol input, serialized output, packet/container/codec buffers and
  borrowed wire spans use `byte_t`, including UTF-8 syntax. This applies through
  the storage, public interface and every caller; changing only `loadPayload` or
  casting incorrectly typed storage at each call does not satisfy the rule.
  `C:/dev/xml/include/utl/utl_xml.h` demonstrates this in attribute/node names and
  values and in `Utl_Xml_IsString`. Terminated application/native text uses its
  text type; C literals and fixed native text APIs need explicit byte/text views
  at that boundary. Numeric eight-bit values retain their numeric type.
- Chromecast message semantics belong in `utl_chromecast`, consuming `utl_json`.
  `cast_json` misclassified a protocol schema as an application adapter. Demo
  appearance is caller policy, represented by a typed optional style.

The console integration keeps native input representation inside `os/win32`,
with a build-selected generic native read spelling for both ANSI and Unicode.
Application keys and receiver commands stay in CLI policy. Initializer, queries,
read and close appear in matching public order; caller misuse asserts separately
from native failure. The first error and outstanding restoration/handle duties
remain owned until closure. The application consumes bounded input without
writing worker-owned results. The GUI WM_NCCREATE cracker is now inside a switch,
as HANDLE_MSG requires. Complete GUI/CLI reference-style migration, strict
diagnostics, native signals and the whole-program allocation proof remain open.
