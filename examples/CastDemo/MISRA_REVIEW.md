# MISRA C:2025 enforcement and evidence

MISRA C:2025 is mandatory for the complete CastDemo goal, alongside the explicit
Win32X/XML conventions and the user's later corrections. It is not satisfied by
enabling CERT checks, passing a lexical audit, or reviewing only new modules.
This document records enforcement work; it is not a compliance declaration.

Every applicable guideline is an acceptance requirement, including guidelines
that need manual analysis. A guideline absent from clang-tidy's installed checks
remains open; it must not be classified as inapplicable merely because the tool
cannot check it. Production code, tests, platform boundaries and linked
dependencies remain in scope. There is no warning-clean or new-code-only
substitute for reviewing the complete program.

## Build enforcement

- Production C, tests and linked C dependencies receive the shared policy in
  [BuildPolicy.cmake](BuildPolicy.cmake).
- MSVC uses `/Wall /WX` and native `AllRules.ruleset` analysis. C clang-tidy uses
  the complete configured general-purpose C analysis families, with findings
  treated as errors and included-header diagnostics visible.
- The sole permitted diagnostic exception is compiler-inserted padding:
  MSVC C4820 and Clang's corresponding `-Wpadded` diagnostic. No other diagnostic
  exclusions, `/QSpectre`, `NO_INLINE` barriers or analysis bypasses are permitted.
- [`.clang-tidy`](.clang-tidy) is the effective check configuration. Generated
  compiler commands must select C, and every target/configuration must preserve
  the policy. A failed analysis run does not authorize linking a replacement
  binary outside the required gates.
- CMake now runs the installed clang-tidy's `--verify-config` during configuration
  and repeats validation when `.clang-tidy` changes. An unrecognized setting or
  check stops configuration. This validates the configuration only; it does not
  run source analysis or establish MISRA coverage. See the
  [clang-tidy option documentation](https://clang.llvm.org/extra/clang-tidy/#using-clang-tidy).
- Generated-policy inspection currently covers 38 C targets in every generated
  configuration. `build/cast-enabled-clang-tidy-checks.log` records the installed
  check list. Enabled C++-only checks select their language support; production
  analysis commands use C mode. Neither the list nor policy inspection proves
  MISRA guideline coverage or a clean analysis run.
- CRT warning-disabling definitions are prohibited too. The two remaining
  `_CRT_SECURE_NO_WARNINGS` definitions in CastDemo sources were removed; the
  console's legacy `fopen` now fails the native gate and remains an open defect.

## Guideline coverage still required

The complete licensed MISRA C:2025 rule/directive text has not been located in
the inspected workspace/reference/download locations. No dedicated MISRA C:2025
checker has been found in the inspected installed tools. The existing checks
are useful defect detectors but provide no complete MISRA coverage claim.

For every applicable guideline, record its verified identifier and edition,
classification, applicability, affected translation units and configurations,
automated checker/version/settings, checker coverage limits, manual review
method, findings and closure evidence. Populate that inventory from the
authoritative edition and checker documentation; do not invent rule numbers,
carry classifications forward from another edition, or label absent evidence
as a pass. Applicable guidelines remain open until their evidence is complete.
The user's no-exceptions requirement does not permit local deviations or
reclassification to make the result appear compliant.

## Concrete review work

These are review areas and current defects, not a substitute guideline inventory.

| Area | Evidence needed across the program |
| --- | --- |
| Types and expressions | Essential-type and conversion review, range proofs before narrowing, signed-overflow prevention, finite floating-point boundaries, named boolean questions and explicit evaluation order |
| Storage and lifetime | No allocations after initialization, including dependencies; valid ownership through cancellation/failure; no stale pointers, invalid aliasing or unbounded stack objects |
| Control flow | Bounded progress, justified loop termination, staged final results, no uninitialized/dead stores, complete state transitions and repeated-operation behavior |
| Interfaces | Required-input assertions, explicit runtime/input failures, accurate output-on-failure contracts, separate platform/compiler implementations and complete function documentation |
| External data | Specification-backed parsing/serialization, bounds before accesses, complete counted consumption, encoding validation, transactional publication and independent invalid-input vectors |
| Concurrency | Publication/acquisition ordering, exclusive job-context ownership, accepted-work preservation, shutdown quiescence and resource-release retry behavior |
| Build and dependencies | C compiler/analysis results, all linked dependency findings, no-CRT maps/imports, required optimizations and final Release artifacts |

The strict build currently fails on owned-source, dependency and included
SDK/header diagnostics, including C4191 conversions in the linked Volk loader.
Recent native analysis no longer reports the LOAD formatter's
25 KB stack frame after the earlier typed LOAD migration (now `utl_chromecast` over `utl_json`), or the subtitle reader's
19 KB track-array frame after selecting during traversal. These are corrected
defects, not proof of MISRA compliance. The new thread and JSON fixtures have
not executed from a successful strict build. Runtime allocation tracing,
complete specification coverage and the full GUI/CLI migration remain open.

Session state now has an explicit startup owner, preparation/reset lifecycle and
retryable shutdown. Per-cast state allocation is removed from GUI and CLI source;
the console also acquires persistent-worker completion before release. Bounded
media publication no longer uses CRT formatting, and Run cannot be reused without
a new preparation. The storage fixture passes native compilation and analysis;
its remaining clang-tidy findings are in included headers. It has not executed
from a successful strict build. Dependency allocation, blocking synchronization,
failed native operation injection and whole-program lifetime proofs remain open.

The subtitle byte reader and WebVTT serializer now use counted, borrowed input,
explicit-width arithmetic and preflight before output publication. Subtitle API
counts no longer use size_t. Embedded cue callbacks no longer allocate temporary
decoded strings or format timestamps through the CRT; finite/range checks precede
floating-point-to-integer conversion. The workspace now borrows two startup-provided
buffers and reuses them for decoding, sidecar input and final output. No text
allocation, growth or release remains in the adapter. CRT file handles, container
dependencies still allocate and violate the goal. The server now borrows immutable
subtitle bytes and reuses request storage established at startup; native transport
allocation and timing remain open.

The new wire fixture and changed conversion code pass native compilation and
analysis. Targeted clang-tidy analysis prompted typed helper arguments, simpler
conversion stages and corrected arithmetic. Buffer-access findings remain in
the new modules; legacy adapter and included-header diagnostics also remain.
The fixtures have not executed from a successful strict build. These are concrete
review results, not evidence that every MISRA guideline has been checked or met.

Workspace tests additionally cover all short output capacities, guard bytes,
source/output aliasing, 1,000 failure/reuse cycles and sidecar failure followed by
a new load. Native compilation/analysis pass; strict analysis remains failing.
The current targeted session analysis uses commands derived from evaluated
Release MSBuild items. Earlier targeted adapter runs pointed at a database that
native failure had prevented from being generated; those fallback runs are not
configuration coverage evidence and are superseded by the workspace analysis log.

SubRip cue/timestamp parsing now uses `utl_srt` with explicit-width counts,
checked decimal arithmetic and borrowed, typed spans. A complete cue is staged
before publication; invalid input preserves both the cursor and all result
fields, and repeated end/failure is defined. Review removed a dead store and
replaced pointer-difference arithmetic with counted source offsets. The sidecar
adapter no longer owns timestamp parsing or CRT string-based cue framing.
Native compilation/analysis show no owned SRT findings; required clang-tidy
still fails on buffer-access and included-header diagnostics. The new fixture
has not executed. These are specific corrections and open findings, not a
MISRA guideline coverage claim.

The new OS mailbox separates producer, middle and consumer ownership with
acquire/release exchanges. Media publishers now write disjoint retained slots;
the runner's borrow remains stable until its own next successful acquisition.
Independent channels preserve URL/MIME coherence without sharing the command
queue's blocking lock. This ownership argument depends on the documented single
producer/consumer roles and quiescent reset; it does not permit extra concurrent
producers or reset during use. Current production call sites meet that topology.

Native review corrected an unproven test index and widened arithmetic before
multiplication in the concurrent fixture. The metadata module now has no owned
source clang-tidy finding in its targeted run; included-header findings remain.
Typed media slot access, test buffer accesses and broader session/header findings
still fail strict analysis. New model, contention and misuse fixtures have not
executed. No complete race-freedom, timing or MISRA coverage claim follows from
native compilation or the isolated metadata result.

Current progress and reproducible logs are recorded in
[REFACTOR_REQUIREMENTS.md](REFACTOR_REQUIREMENTS.md),
[REFERENCE_RULES.md](REFERENCE_RULES.md),
[OS_MODULES.md](OS_MODULES.md) and
[SPECIFICATION_MODULES.md](SPECIFICATION_MODULES.md).

Discovery review now covers startup-owned adapter snapshots, datagram resources,
HTTP session ownership and retained description buffers. Partial initialization
and failed releases preserve resource identities; GUI closure follows worker
quiescence. Response collection uses nonblocking receives, and body acceptance
requires successful EOF without truncation. This still leaves synchronous query
and HTTP operations, runtime HTTP connection/request allocation, CLI shutdown
polling and provider-internal allocation unproved. The later XML migration below
replaces the legacy description scanner.

Targeted analysis corrected an unproven initialization path, implicit native
control-code conversion, missing direct dependencies and excessive HTTP/test
function complexity. Buffer-access and required umbrella-header diagnostics
remain. Native analysis fails in WS2tcpip.h C6101; the new fixture passes native
checks but remains behind header clang-tidy errors. No current runtime pass or
MISRA compliance follows from these changes.

The user's explicit MISRA C clarification remains an acceptance requirement for
the complete program. There is still no complete dedicated-checker result or
guideline-by-guideline manual inventory; no MISRA compliance is claimed.

The URL/HTTP boundary review removed ambiguous slice arguments, redundant stores,
unproven optional-output paths and excessive parsing-function complexity. Named
slice fields separate offset from extent, and host, authority, path and output
operations have individual contracts. Invalid syntax and capacity failures stage
results before publication. The new fixture also removed unnecessary structure
padding and separated failure-publication checks from case iteration.

Native compile/analysis pass for the changed format sources and fixture. The
URL source's remaining targeted clang-tidy findings concern buffer accesses;
included-header findings also remain. Discovery retains buffer/header findings
and the WS2tcpip.h C6101 native failure. These results neither establish MISRA
coverage nor authorize running/linking outside the failed strict gates.

The XML/device-description migration now uses counted immutable UTF-8 input,
startup-owned node/attribute records and a separate UPnP display-identity model.
Complete syntax and namespace validation precede root publication; rejection
revokes every published record count. Iterative traversal, exact input extents,
checked character-reference arithmetic and preflighted text output replace CRT
substring scanning. Caller representation errors assert separately from malformed
network input. This does not close full XML/UPnP profile coverage or the native
network allocation and timing requirements.

Targeted review corrected conditional-initialization findings and an inconsistency
where text comparison accepted a raw CDATA closing delimiter that sizing rejected.
Literal-scalar normalization is now separate from markup/reference interpretation.
Private declarations and definitions have matching operation order and explicit
failure-result contracts. The fixture includes 64 independent document vectors,
11 additional display-identity vectors, namespace normalization, output guards,
all short capacities, incomplete prefixes and repeated owner reuse.

The changed XML/UPnP sources and fixture pass native compilation/AllRules analysis.
Targeted XML clang-tidy still reports buffer-access diagnostics; its included
headers also fail. UPnP has no owned-source finding in its targeted run, with
included-header findings still failing the command. Discovery retains the SDK
WS2tcpip.h C6101 native failure and buffer/header analysis findings. The XML fixture
has not executed from a successful strict build. These results are scoped defect
review evidence, not a MISRA C:2025 guideline inventory or compliance finding.

The C/MISRA requirement was rechecked against the active goal and the user's
latest clarification. The installed clang-tidy accepts the complete configured
policy; all 35 generated C targets retain required compiler/native/clang-tidy
settings, and all 29 lexical-auditor regression tests pass. Neither result is a
MISRA compliance finding. There is no complete guideline inventory or dedicated
MISRA C:2025 checker result, and no applicable guideline has been waived.

The HTTP serving migration now separates counted grammar from application policy
and native transport. Complete framing validation precedes request dispatch;
terminal request results latch until explicit reuse. Responses are preflighted
before publication, including partial interval arithmetic and unknown-total
snapshot ranges. Review replaced ambiguous adjacent header-name/value arguments
with named field members and separated error closure from OPTIONS persistence.
The fixture covers 37 independent request cases, exact response bytes, every
short output capacity, pipeline retention and 100 reuse cycles.

Changed HTTP policy and fixture sources pass native compilation/AllRules, but
required clang-tidy still reports buffer-access and included-header findings.
The discovery/session check still fails in WS2tcpip.h with C6101; the server's
unused include has been removed. Native transport allocation, blocking I/O,
CRT operations and complete lifecycle evidence remain
open. The HTTP policy fixture has not executed and no replacement Release binary
has been linked outside the failed gates. Complete logs and remaining protocol
coverage are recorded in the requirements and specification ledgers.

Route-address acquisition now has explicit startup and shutdown ownership in GUI
and CLI. Counted IPv4 parsing/serialization shares the URL module's grammar with
URI host validation. Numeric representation is independent of native byte order;
native route output is staged and checked before publication. Thirty independent
syntax/value vectors and native loopback/reuse/capacity fixtures were added.
Review corrected an unproven nullable output path, excessive fixture complexity,
direct-header dependencies and declaration/definition ordering. These are scoped
corrections, not a complete essential-type, lifetime or MISRA guideline review.

The modified format/route sources and fixtures pass native compilation/AllRules.
Removing the route backend's unused WS2tcpip.h include removed its C6101 failure;
discovery still fails that SDK diagnostic. Clang-tidy still
rejects buffer/header findings, GUI native headers fail, and the console's legacy
fopen still fails C4996. No new fixture execution or Release link is claimed.
Native socket/file allocation, complete gain-variant lifetime, native shutdown timing
and dependency allocation remain open across the full goal.

Shutdown review now establishes distinct ownership stages for cancellation,
callback quiescence, native handle release and temporary-file removal. Pending
polls retain the owner pointer; completed polls clear it and are idempotent.
Admission acquires each submitted client's completion before native release and
slot reuse. It also closes listeners without concurrent accept, consistent with
the [Winsock close contract](https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-closesocket).
Client and partial-startup cleanup now retain failed native releases for retry;
completed callback accounting alone does not prove provider-internal resource
retirement. Native transport and conversion allocation, blocking transport,
whole-program startup ownership and native failure injection remain open. Fragmented proxy
conversion now observes the OS atomic per-job cancellation latch; legacy preview
conversion and producer shutdown still require separate migration.

New native fixtures compile and pass AllRules analysis; required clang-tidy
still fails their buffer accesses and included headers. Analysis also corrected
shutdown function complexity, required direct includes and native control-code
conversion. No fixture execution, complete guideline review, compliant Release
or MISRA C:2025 acceptance is claimed. The current policy covers 38 C targets;
full applicable guideline evidence remains mandatory beyond these defect checks.

Proxy review now covers startup-owned storage/thread/file composition, failed
partial startup, transactional request validation, callback acquisition before
server-field inspection, job lifetime through serving shutdown, scratch borrow
expiry and retryable final release. The two new native fixtures pass compilation
and AllRules but have not executed because required clang-tidy checks fail.
No allocation trace or successful end-to-end conversion/playback result is claimed.

Selected-source analysis corrected proxy conditional-initialization dataflow,
native string termination evidence, zero-valued failure recording and GPU arithmetic
widening. Full GPU native analysis still stops on a 137,980-byte probe stack frame;
owned-source complexity, buffer access and included-header clang findings remain.
General checks do not prove essential-type rules, complete lifetime safety or
MISRA C:2025 coverage. The missing authoritative inventory/dedicated checker is
still an open evidence requirement, not an exception.

MSBuild minimal rebuild is explicitly disabled so selected-file native analysis
does not inherit ambiguous/stale rebuild state. The actual native command retains
`/Gm-`, `/Wall /WX`, AllRules, `/O2`, `/GL` and `/Ob3`. No warning exemption was added;
the previous stale-analysis warning no longer appears. Microsoft's documented
[minimal-rebuild behavior](https://learn.microsoft.com/en-us/cpp/build/reference/gm-enable-minimal-rebuild?view=msvc-170)
does not replace checking the evaluated compiler command or full build results.

The HTTP pool review covers transactional slot reservation, immutable subtitle
borrows, completion acquisition before metadata reuse, cancellation, partial
startup, admission failure recovery and retryable file/socket/worker release.
Review corrected a pending-cleanup result incorrectly being recorded as an error,
a missing failed-admission cleanup path, unused declarations and a misplaced API
contract. Native compilation/AllRules pass for the changed server, proxy and pool
fixtures; required clang-tidy still fails. Capacity and saturation fixtures have
not executed. These are concrete corrections, not full MISRA guideline evidence.

The lexical guard now rejects invisible control bytes in source and comments and
records startup-owner closure plus native accept as lifecycle boundaries. All 30
auditor regressions pass, and generated policy inspection covers 38 C targets.
These checks neither trace allocation nor establish MISRA C:2025 coverage. The
authoritative guideline inventory, dedicated analysis and manual evidence remain
required for the complete program; no diagnostic exception was added.

## JSON/Chromecast separation review

The typed Chromecast schema now consumes the generic JSON specification writer.
Caller-selected style replaces fixed demo appearance in the protocol library.
Exact decimals group coefficient and scale in a typed value; numeric grammar,
name-state preparation and emission are separately staged. Invalid post-root
operations consistently latch a writer error. Every distinct range/storage
question in the changed validation code is named before composition.

Native `/Wall /WX` and AllRules compile the two modules and both protocol
fixtures. Strict clang-tidy remains failing, with full logs under
`build/cast-{chromecast,json}{,-fixture}-native.log`. Remaining owned diagnostics
include Clang indexed-buffer/pointer usage in the generic writer and Chromecast
fixture. The latest Chromecast module and generic JSON fixture logs contain no
owned-file findings, but included SDK findings still prevent success. No warning
policy was weakened, and no C fixture was executed outside the failed checks.
The audit-script regression suite passes 30 tests and generated project policy
covers 39 C targets. These checks do not certify MISRA compliance or a Release.

The GUI/CLI context migration remains unfinished. The CLI placeholder has now
been replaced by a startup-owned keyboard backend and bounded application input
policy. Native control callbacks must not be connected by hidden global state.
The executable integration must be completed before any full-build claim.

## Console integration and front-end compilation

`os_console` duplicates its input handle at startup and retains the original
mode through retryable restoration and handle closure. Keyboard reads consume
at most one record with the documented native NOWAIT operation. Empty, ignored
and failed reads preserve caller output. The CLI consumes at most 32 records per
poll, owns the key-to-command policy and checks retained input errors only after
the session worker is quiescent. Repeated key-down records cannot be canceled by
parity: a toggle uses asynchronously reported receiver state.

Native `/Wall /WX` and AllRules pass for both ANSI/Unicode console objects and
the isolated-console fixture. Required clang-tidy still fails on included
headers; the fixture additionally reports its unused `Windows.h` umbrella
include. Neither console object has an owned-file finding in the latest logs.
The fixture has not linked or executed. Its launcher creates a hidden isolated
console for keyboard tests and a detached process for absent-input tests. It
never detaches or injects events into the caller's console. Native restoration
failure, input failure after successful startup and concurrent signal/teardown
injection remain uncovered.

Logs are `build/cast-console-{ansi,unicode}-native.log` and
`build/cvk-cast-console-test-native.log`. The generated import archive contains
both exact native read exports, verified against this host's Kernel32 exports.
The latest `cast_session.c` and `gpu_probe.c` selected compilations also pass
native compilation/AllRules and fail clang-tidy; this is not whole-target success.

The GUI's misplaced WM_NCCREATE HANDLE_MSG case is corrected inside a switch.
Native GUI compilation still fails SDK C4668/C4255 diagnostics. Native CLI
compilation still fails C4996 at its legacy transcode-output `fopen`; substituting
another CRT call or suppressing the warning would not satisfy the goal. Logs are
`build/cast-gui-context-native.log` and `build/CastDemoCli-native.log`.
All 31 lexical-auditor regressions pass and generated policy inspection covers
42 C targets. No warning exemption, fixture execution or new Release is claimed.

Native Ctrl+Break, generated control events, console close and shutdown remain
open. Keyboard Ctrl+C input does not replace these lifecycle obligations.
The native signal mechanism creates a handler thread per event and provides
no caller context, so its lifetime and allocation behavior require an explicit
solution under the unchanged goal. See Microsoft's
[control-signal contract](https://learn.microsoft.com/en-us/windows/console/ctrl-c-and-ctrl-break-signals).
All broader CRT, allocation, realtime, protocol and MISRA obligations remain open.


## Counted-byte and JSON reader review

The latest user correction is recorded in REFERENCE_RULES.md and
REFACTOR_REQUIREMENTS.md: serialized and counted wire data uses `byte_t` through
storage, interfaces and callers. The correction reaches the JSON/Chromecast,
CastV2 adapter, HTTP/range, SSDP, URL, XML, decimal, SRT and WebVTT chains; retained
application buffers and binary media/codec/image-plane storage were reviewed too.
Terminated native/application text remains text, with explicit boundary views.
This does not discharge CRT use, allocation lifetime or missing format semantics.

The new iterative JSON reader has fixed caller-owned records, no recursive C
calls and no allocation. External syntax/capacity failures publish no root;
Unicode decoding preflights output and preserves count/storage on rejection.
Native `/Wall /WX` and AllRules compilation of the reader passes. Clang-tidy
identified excessive nesting in escape parsing and record admission; separating
hexadecimal decoding, escape selection and record publication cleared those
complexity findings. Its buffer-access diagnostics and SDK diagnostics still
fail the strict target. No checks or warnings were disabled. Evidence is in
`build/cast-json-reader-native.log`.

Native reader/Chromecast and affected protocol fixtures compile, but the strict
analysis gate has not passed and these binaries have not been linked or run.
The protocol adapter fixture uses a payload with invalid bytes beyond its
explicit count to check that framing excludes them. Reader cases cover complete
and malformed grammar, every truncated container prefix, record exhaustion,
maximum admitted nesting, decoded name lookup, surrogate pairs and output
preservation. Fixture evidence is in `build/cvk-cast-*-byte-native.log`.

Application checks are deliberately selected per source in
`build/*-byte-selected-native.log`; incremental project checks are insufficient
proof that every changed body was compiled. Full application compilation also
still encounters native SDK macro/prototype diagnostics and the CLI's existing
`fopen` warning. The production receiver parser and its CRT numeric conversion
remain unmigrated. There is no new validated Release and no MISRA completion claim.


The selected per-source pass also exposed native C6001 in transcoder partial
startup cleanup and C26451 in MP4 sample-table/edit-list offsets. The transcoder
now records successful plane acquisitions before cleanup and releases exactly
that retained prefix in reverse order. MP4 offset multiplication now widens the
operand before arithmetic, matching the existing 64-bit table-extent preflight.
The transcoder passes native compilation/AllRules after this correction; strict
clang-tidy still fails. The demux also passes native compilation/AllRules after the last edit-list
offset correction; its strict clang-tidy check remains rejected in
`build/mp4_demux-byte-selected-native.log`. These are scoped corrections, not
completion of allocator removal, teardown-failure handling or MP4 semantics.
Version-specific composition offsets and complete sample/run/extent validation
remain part of the required MP4 specification migration.

The selected socket/discovery checks still fail native SDK C6101 in
`WS2tcpip.h` for the SDK's output-MTU path. This is distinct from passing native
compilation of the message, session, subtitle, proxy and probe sources. No SDK
warning is suppressed and full-target acceptance is still withheld.


## Typed command serialization review

Outgoing commands now use the generic JSON writer through typed Chromecast
models. Their serializers use counted byte_t output, fixed automatic writer
frames, borrowed input and two-pass validation. There is no vendor CRT call,
allocation, global state or recursive traversal in the new command serialization.
The session reuses its startup payload storage and has no remaining text-formatted
wire commands. Its diagnostic formatting and incoming legacy JSON/numeric parser
are still CRT/reader defects.

The signed JSON writer derives negative magnitudes by incrementing before
negation, so INT64_MIN never overflows. The request sequence checks exhaustion
before incrementing. Complete time-derivation results replaced a conditional
output path that clang diagnosed as potentially uninitialized. Seek requests use
named model fields instead of interchangeable floating-point/boolean parameters.
The parser publication helper derives its record index from its owner, eliminating
a redundant index argument; decoded comparison no longer performs a dead store.

Native /Wall /WX and AllRules compilation passes for the affected JSON and
Chromecast implementation units, session unit and command fixtures. Strict
clang-tidy still rejects included SDK diagnostics, generic JSON buffer accesses,
fixture buffer accesses and existing application findings. This is not a strict
build pass and no new C fixture was linked or executed. Compiler and analysis
settings, including warning treatment, remain unchanged. Evidence is recorded in
`build/utl_chromecast-command-native.log`, `build/utl_json-command-native.log`,
`build/cast-session-command-native.log` and
`build/cast-chromecast-command-fixture-native.log`.

The full specification, initialization-only allocation, no-CRT application,
reference-style, MISRA and Release acceptance obligations remain open. Neither
selected native compilation nor authored fixtures establish runtime correctness,
complete compliance or a fixed release binary.


## Exact integer and counted routing review

The exact JSON integer query uses bounded integer arithmetic throughout. Its
decimal prefix cannot exceed nineteen digits, the exponent summary is bounded
against the uint32_t source extent, and signed magnitude is checked before
narrowing. INT64_MIN is published separately. A rejected value leaves the
output unchanged. Native compilation and AllRules pass for the implementation
and integer fixtures (`build/cast-json-integer-native.log` and
`build/cast-json-integer-fixture-native.log`). Full clang-tidy still fails on
generic JSON buffer diagnostics, existing fixture findings and included SDK
diagnostics; these C fixtures were not linked or executed.

The counted routing migration removes the redundant Cast message structure and
all CastMessage_* application/fixture calls. The channel now serializes and reads
the specification model directly. Namespace mapping queries read no command
payload fields. Receiver IDs are decoded into startup-owned storage only after
both pass preflight; logical route state is published after CONNECT succeeds.
The two-pass decode asserts the stable-source preflight guarantee rather than
treating a programmer invariant as malformed external input. Diagnostics still
use the legacy terminated logging interface; incoming numeric/media semantics,
allocation dependencies and native transport behavior remain open.

Selected native /Wall /WX and AllRules compilation passes for the updated
Chromecast module, session, routing fixture, legacy message fixture and assertion
fixture. The channel still fails native SDK C6101 in WS2tcpip.h. The protocol
fixture still fails native SDK C4668 in shellapi.h/winioctl.h. No warning is
suppressed, excluded or weakened. The Chromecast module has no owned clang-tidy
diagnostic in its selected log; the session and fixtures still have owned
findings, and included dependencies remain rejected. Full targets therefore do
not pass. Current evidence is `build/cast-*-routes-native.log` and
`build/cast-chromecast-routes-fixture-native.log`; compact diagnostic counts are
in `build/cast-routes-review.json`.

The auditor's 31 regressions pass, and all 42 generated C targets retain the
required compiler/analysis settings. These are infrastructure checks, not
execution evidence for the changed C. No new Release or full MISRA acceptance
is claimed. The current binary in the Release directory predates these changes.


## MPEG-4 Systems and Audio configuration review

The new descriptor and AudioSpecificConfig modules have no mutable global state,
heap operation, platform I/O or external CRT dependency. Their models use counted
byte spans and explicit-width fields. Missing required owner/storage pointers
assert; malformed external bytes, unsupported syntax and output exhaustion remain
ordinary outcomes. Public output publication follows complete validation. Writers
measure the entire active model and output capacity before mutation. Optional
fields are accessed only when selected; fixed program arrays are checked before
traversal and bit shifts occur only after field-width checks.

Expandable descriptor arithmetic is bounded by the 28-bit body limit. Audio read
positions use uint64_t so a uint32_t byte extent cannot overflow conversion to
bits. Serialization traverses bounded program arrays and at most 255 comment
bytes. Invalid scalar widths, duplicate element selectors and inconsistent
program/tool fields fail before any caller output write. Terminal transport
alignment is not interpreted as a codec field. No unmodeled audio object is
silently treated as LC.

Native /Wall /WX and AllRules compilation passes for the two new modules, their
fixtures and the updated MP4 sources. The strict clang-tidy gate still rejects
buffer-access diagnostics in these modules, included dependency diagnostics and
legacy application findings. These failures remain unsuppressed; authored C
fixtures have not executed. The generated 44 C targets retain all required
compiler and analyzer settings. Logs and per-check counts are captured in
`build/mpeg4-codec-review.json`.

No full MISRA acceptance, realtime timing bound, successful end-to-end playback,
complete specification coverage or Release build is established. The demux/mux
still allocate at runtime, use CRT and retain unresolved state/resource behavior.
MF, Schannel, Vulkan and other dependency allocations also remain open; moving
these operations to workers does not satisfy initialization-only allocation.

## Shared MPEG-4 foundation review

`utl_mpeg4` now owns the shared span, explicit-width integer, bit-field,
bit-reader and bit-writer models consumed by the Systems and Audio modules.
Owners initialize explicitly; the reader/writer failure state is sticky and
reinitialization resets it. Field capacity is checked by subtraction before
cursor advancement. A 64-bit bit position represents the entire uint32_t byte
domain. Shift widths are bounded before use, including full-width integers;
bit operations move at most one byte portion per iteration. Counter mode never
dereferences source byte sequences, and write mode initializes partial bytes.
There is no runtime allocation, external CRT operation or mutable global in
these common wire operations. They do not establish those properties for the
application or dependencies.

The Box reader now receives distinct parent and file extents; native consumers
propagate the file boundary instead of silently resolving size zero to a nested
parent end. The common FileType and FullBox field codecs replace additional
inline output syntax. Unknown types and supported opaque data remain preserved;
complete document and codec semantics remain explicitly open.

The three modules, their three focused fixtures and three MP4 consumers pass
selected native /Wall /WX and AllRules compilation. Fully enabled clang-tidy
still rejects owned unsafe-buffer usage and included dependency findings.
The legacy combined format fixture additionally has SDK C4668 failures. No
suppression or policy relaxation was added. All 45 generated C targets retain
the required policy. The fixtures are authored and compiled, not executed;
strict target success, full MISRA acceptance and a verified Release remain open.
See `build/mpeg4-common-review.json` for source hashes, log paths and diagnostics.

## MPEG-4 sample-table and bounded-index review

The shared library adds typed sample-table prefixes and cursors with caller-owned
state, no heap/native/CRT operations, explicit widths, named decision predicates,
initialization assertions, one final return and transactional field publication.
The native indexer replaces whole-table malloc/free with two fixed pages and
validates complete entry sequences before returning a nonzero index count.
Page size bounds resident bytes; it does not bound file latency or the total
number of operations. Native file opening, synchronous reads, GUI scheduling,
platform-specific interfaces and composition/edit timing remain unresolved.

Native /Wall /WX compilation reports no diagnostics for the shared MPEG-4 unit,
indexer and expanded fixture. Full clang-tidy still fails, including owned
buffer-usage findings and included SDK/compiler-header findings. The fixture
has no owned-source analysis findings, but its included-header gate still fails.
The existing media-header adapter also retains style/complexity findings.
No warning was disabled and no gate was bypassed to link or execute the fixtures.
No new Release or full MISRA acceptance is claimed.

All 45 generated C targets retain the required compiler and analysis settings;
the auditor's 31 regression cases pass. Exact source hashes and unit-specific
diagnostics are retained in `build/mpeg4-tables-review.json`. The full lexical
inventory is regenerated separately; its absence of a finding does not prove
semantic, allocation, timing, concurrency or guideline compliance.
