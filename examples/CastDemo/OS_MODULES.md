# Required OS modules

The user explicitly requires these modules as part of the active CastDemo goal:

> we need os_thread, os_fiber, os_mutex, os_localmutex, os_semaphore, os_localsemaphore, os_queue, os_ringbuffer

The user additionally requires `os_atomic`.

All nine are mandatory and currently open. Existing application-specific workers,
locks, command storage and event mailboxes do not complete these shared modules.
Adding headers or wrappers alone does not establish implementation or integration.

`os_ringbuffer` and `os_queue` now have caller-owned storage implementations.
CastCommands uses the queue; normal dequeue advances the ring without moving
surviving entries. Both modules require caller-provided exclusive access and make
no atomic or concurrent-queue claim. Wider producer/consumer integration, the remaining
OS modules, and complete performance/allocation evidence remain open.

`os_atomic` now has an MSVC x64 assembly backend for 32/64-bit load, store,
exchange, compare/exchange and fetch-add, with documented memory ordering.
The event mailbox uses those operations for separate status/log publication
and terminal notification. Native contention, coherence, saturation and misuse
tests passed before the new strict compiler/analysis policy. Other targets,
complete application lifecycle migration and whole-goal evidence remain open.

`os_thread` now provides persistent Win32 execution resources, created at startup
and reused through Submit/TakeResult. State and error publication use `os_atomic`.
Stop preserves accepted work; TryClose observes exit with a zero-time wait and
retains failed releases for retry. GUI discovery, gain conversion, casting and
preview each use one of four startup-created workers. The completion timer is
established before worker initialization, and shutdown consumes active results
before closing their resources. No direct thread creation or thread join remains
in `cast_gui.c`. CLI casting also uses the persistent worker and acquires its
completion before releasing session state; dependency operations still contain
waits and allocations.

Proxy execution now also uses one startup-created OS thread, with independent
atomic per-job cancellation and successful job teardown before rearming. Its
callback still calls allocating/blocking GPU and server dependencies.

HTTP serving now creates its admission worker and eight reusable client workers
at program startup. This does not close `os_thread` or the allocation requirement.
Conversion execution resources and target backends, native failure injection,
callback/dependency allocations and measured timing remain open. Thread,
control and assertion fixtures pass native compilation/AllRules checks but
remain rejected by clang-tidy. The new thread regressions have not executed
from a successful strict build.

`os_memory` additionally owns a startup-only resident Win32 region with explicit
readiness and retryable closure. Reserve/commit, first-touch writes and VirtualLock
all precede usable address publication. Failed initialization retains resources
for TryClose. Shutdown revokes readiness, attempts unlock and whole-region release,
and retains failed releases; repeated complete closure performs no native calls.
The interface uses ordinary domain types; native memory operations stay in the
Win32 source. CastSession uses this owner to keep private state across casts.
It does not cover dependency storage, stack residency, other target backends,
native failure injection or a complete allocation trace. The storage fixture
passes native compilation/AllRules checks but remains behind included-header
clang-tidy errors; no current runtime pass is claimed.

`os_mailbox` now owns the shared single-producer/single-consumer latest-value
exchange previously embedded in CastEvents. The caller retains three typed
payload slots. One atomic exchange publishes a complete writer slot; a consumer
uses one load and, when pending, one exchange to acquire a stable slot. There
are no retries, waits, payload copies or resource operations in this metadata
module. Querying a reader slot before the first successful acquisition asserts.
Empty reads retain a prior borrow; producer replacement never touches that borrow.
Reinitialization requires both participants to be quiescent.

CastEvents now uses this module for status/log categories while retaining its
separate terminal latch. CastSession owns independent fixed `cast_media` channels
for its proxy worker and GUI replacement producer. Their URL/MIME pairs coalesce
without taking the runner's SRW lock. The runner retains the acquired proxy slot
through synchronous LOAD serialization. Replacements are consumed only when
receiver state permits switching. All six payload slots reside in the session's
startup-owned memory region. This trades additional fixed startup storage for
nonwaiting publication; no runtime storage growth is introduced.

The command queue still uses an SRW lock. `os_mailbox` is neither a mutex nor a
FIFO, and does not satisfy any of the missing mutex/semaphore/fiber modules.
The required nine modules, dependency allocations, realtime measurements and
whole-program concurrency/lifetime proof remain open.

New fixtures enumerate all 4,096 publish/take sequences of length twelve, check
retained maximum-size media pairs across 1,000 producer cycles, separate channels,
source mutation, repeated empty reads and quiescent reuse. The existing native
event-contention fixture additionally checks 200,000 coherent media publications
and final delivery before the terminal event is consumed. Five additional misuse
cases cover absent metadata and reading before acquisition. Native compilation
and analysis pass for these changed modules/fixtures; strict clang-tidy remains
failing and these new checks have not executed from a successful strict build.

| Required module | Responsibility and integration work | Required contract/evidence |
| --- | --- | --- |
| `os_thread` | Shared thread lifecycle for workers, discovery, sessions, conversion and HTTP serving | Startup resource creation; typed entry/context ownership; start/completion/cancellation states; safe reuse; confirmed quiescence before release; bounded operations |
| `os_fiber` | Shared fiber/context lifecycle and cooperative execution | Startup-established context and stack storage; explicit switch/resume/finish states; stack/context ownership and lifetime; target ABI handling behind selected implementations; no allocation during switching |
| `os_mutex` | Shared mutex abstraction, replacing scattered native mutex operations | Explicit scope and distinction from `os_localmutex`; ownership and acquisition/release rules; contention, timeout, failure and teardown behavior |
| `os_localmutex` | Shared local-mutex abstraction for applicable application synchronization | Explicit local scope and ownership; documented acquisition/release and contention behavior; bounded operations; distinction from `os_mutex` preserved |
| `os_semaphore` | Shared semaphore abstraction | Explicit scope and distinction from `os_localsemaphore`; initial/maximum counts; acquire/release bounds; saturation, cancellation, failure and teardown behavior |
| `os_localsemaphore` | Shared local-semaphore abstraction | Explicit local scope; initialized permit storage/state; count bounds; wake/acquire/release behavior; distinction from `os_semaphore` preserved |
| `os_queue` | Shared bounded queue used by appropriate command, event and work paths | Caller-owned storage; element ownership; supported producer/consumer model; full/empty behavior; publication/consumption ordering; cancellation/reset/reuse rules |
| `os_ringbuffer` | Shared bounded circular-buffer storage for applicable streaming and queue paths | Caller-owned capacity; wraparound and overflow rules; readable/writable spans; reserve/commit/consume ownership; partial operations and reset/reuse behavior |
| `os_atomic` | Shared explicit-width atomic operations with build-selected target/compiler implementations | Initialization and alignment; supported widths; indivisible load/store/exchange/compare-exchange and required arithmetic; explicit memory ordering; publication/observation rules; documented progress guarantees; no hidden locks or allocation; contention and target evidence |

The precise scope of the mutex and semaphore variants must be expressed in their
contracts and implemented consistently with the authoritative references. The names
alone do not establish interchangeable behavior.

`os_network` now exposes a portable IPv4 interface snapshot through a Win32
implementation selected by the build. Its startup owner retains a resident
128-entry native array and one query socket. A query publishes only a complete,
validated snapshot; native overflow/failure exposes zero entries. Address getters
copy network-order octets and capability flags. Closure revokes readiness and
retains failed socket/storage releases for retry. `cast-network` owns this native
dependency separately from the format library's existing source set.

Discovery now uses this reader and a startup-owned wildcard-bound nonblocking
UDP socket. Interface selection precedes each multicast send, so refresh does
not allocate an adapter list or recreate search sockets. This does not establish
allocation-free or realtime native queries: `WSAIoctl` is still synchronous and
provider internals remain untraced. WinHTTP still creates connections/requests
during description fetches. URL and XML description parsing now use counted
specification modules; that migration does not remove native request allocation.

The new native fixture exercises failed initialization before Winsock startup,
complete snapshot reuse on the same retained region and closure repeated after
Winsock shutdown. The fixture's source passes native compilation and analysis;
included-header clang-tidy findings prevent execution. The route-query review
below removes the network backend's unused WS2tcpip.h dependency; its current
native compilation/AllRules pass. Buffer/header clang-tidy findings remain.
No runtime, allocation-trace or timing pass is claimed.

`OS_Network_ReadRoute` now stages a numeric IPv4 destination/source query through
the retained startup socket. It validates the returned extent and address family
before publishing the source, preserves both fields on failure, and leaves an
existing interface snapshot untouched. The native operation is documented under
[SIO_ROUTING_INTERFACE_QUERY](https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls#sio_routing_interface_query-opcode-setting-i-o-t1).
No explicit socket creation, connection, allocation or release occurs in the
query. Native latency and provider-internal allocation remain unverified.

GUI and CLI now establish dedicated route readers during startup and retain them
until shutdown. GUI route access belongs to the window thread and is independent
of the discovery worker's reader. Failed startup remains closable; GUI closure
polls retained resources and CLI shutdown retries before Winsock cleanup. The
console's shutdown polling wait remains an open timing defect. CastNetwork uses
the existing URL module for counted IPv4 syntax and complete guarded output.

Route fixtures add loopback queries, repeated owner reuse, unchanged interface
snapshot/storage identity, exact text output, short capacities and rejected host
syntax. The route backend, adapter and fixtures pass native checks; strict
buffer/header findings still prevent execution. This is not allocation tracing
or proof that every native call is realtime.

The HTTP server now composes one startup OS memory owner, one admission worker,
32 retained listener slots and eight shared client workers with fixed HTTP buffers.
GUI, CLI and proxy use that same retained pool. No listener/client metadata heap
allocation, per-connection thread creation or subtitle byte copy remains in the
adapter. WebVTT bytes are borrowed unchanged until successful listener stop.

Admission owns listener use and native cleanup; acquired callback completion
precedes file/socket release, and successful release precedes slot reuse. Failed
native cleanup retains identities for retry. If admission fails before entering
its callback, an exclusive recovery path retires pending resources. Final closure
requires quiescent slots before closing workers and then releasing storage.

This is incomplete transport ownership: socket/accept/file operations still
create native resources during use, response sends/TransmitFile remain blocking,
and provider allocation/retirement and timing are unproved. Request headers have
a five-second absolute deadline and admission rotates listener priority, but these
do not establish realtime operation. CRT transfer logging remains. The 32-listener
bound also exposes the unresolved accumulation of old gain variants; continuous
live gain and bounded representation reuse still need their complete migration.

## Requirements shared by every module

- Public OS identifiers exceptionally use the `OS_` prefix, including types and
  functions. Apply the user's correction to existing modules and callers too;
  `Os_` is not the accepted prefix. Filenames retain the lowercase `os_` form.
- Put `Initialize` first in both public declarations and definitions. Accessors use
  `Get...`, including count, capacity and borrowed-span accessors. Follow initialization
  with queries/derivations, then search and read/write operations. Keep both orders identical.
- Follow the complete active goal, Win32X and `C:/dev/xml`, including typed models,
  full section layout, aligned declarations/assignments, named boolean questions,
  documented function contracts and definition Details blocks. Use `\retval`.
- Keep public APIs independent of platform, target and toolchain. Select differing
  implementations through source files in the build, following the shared
  `Utl_Arguments_Read` interface with Win32/POSIX implementations. Keep portable
  data-structure logic shared. Native types, intrinsics, entry conventions and ABI
  details belong inside their corresponding implementation boundaries.
- Establish all storage, stacks, synchronization and execution resources during
  program initialization. No later allocation, including hidden dependency or OS
  allocation, may be excused by moving it behind one of these APIs or onto a worker.
- Initializers establish valid objects by definition. Required storage, callbacks,
  capacities and invariant misuse assert in every build. External/runtime failures
  have explicit results and leave a documented valid state.
- Use explicit-width domain types, bounded hand-written operations and no CRT.
  Use neither `while` nor `do ... while`; preserve explicit bounds and progress.
- Define ownership, lifetime, state transitions, memory visibility, cancellation,
  repeated operations, partial initialization and safe shutdown before integration.
- Preserve realtime behavior throughout the caller and implementation. A blocking
  operation is not made realtime by moving it into an OS wrapper or another thread.
- Verify each implementation, then migrate its production callers. Exercise
  contention, saturation, wraparound, failures, cancellation, repetition and teardown
  as applicable. Measure latency and verify allocation/CRT behavior, including
  dependencies. Passing an isolated test does not complete application migration.

Acceptance remains governed by [REFACTOR_REQUIREMENTS.md](REFACTOR_REQUIREMENTS.md)
and [REFERENCE_RULES.md](REFERENCE_RULES.md). Do not close a module until its
implementation, production integration and required evidence are complete.

Server lifetime repair retains native identities in startup-owned pool slots;
admission serializes accept/listener cleanup, and client completion and resource
release precede slot reuse. GUI teardown retires one gain variant per attempt.
Proxy ownership composes startup-owned memory, thread and
temporary-file modules; per-job stop retains these for reuse and final close
releases them only after serving/publication is quiescent. This does not complete
os_thread or a realtime transport abstraction: native transport and conversion
allocation, synchronous sends and failed-operation injection remain open.
The production-loopback and proxy fixtures pass native analysis
but have not executed through the required strict gates.

`os_temporaryfile` additionally owns a startup-created scratch file and stable
UTF-16 filesystem identity. Native creation and temporary-directory discovery
occur only in Initialize. GetPath borrows the identity without native calls;
all consumers must end before TryClose. Shutdown revokes borrowing and attempts
one removal. A failed removal retains the path and first nonzero native error;
already absent files and repeated successful closure complete idempotently.
The Win32 backend uses generic Win32/TCHAR operations with filesystem encoding
selected in CMake, independently of UI text selection. Native creation may
allocate at startup; removal latency/provider behavior still require evidence.
The fixture exercises distinct paths, a live no-delete-sharing handle, retained
failure, successful retry and repeated closure. It passes native compilation
and AllRules but remains blocked by required clang-tidy findings. No OS module
is newly marked complete.

`os_console` now supplies a separate startup-owned keyboard lifetime. The Win32
backend duplicates standard console input once, saves its exact mode and applies
nonprocessed key input. A failed standard-handle acquisition is distinguished
from absent or redirected input. Each read uses the documented NOWAIT native
operation with capacity one and preserves output unless a key record is complete.
There is no readiness-check followed by a potentially blocking read. Portable
key identities and modifiers reach CLI policy without CRT keyboard operations.
Closure revokes reads, restores the original mode, then closes the owned handle;
failed obligations remain retained for retry and repeated successful close does
no native work. State has one explicit owner and no callback or global registry.

The build compiles both native text modes and supplies an import archive for
the SDK's missing ReadConsoleInputEx import. Imports resolve at process startup;
there is no runtime symbol lookup or function-pointer conversion. The fixture
covers intended key, modifier, repeat, ignored-event, empty, redirected, absent,
failed-start and repeated-close behavior in isolated processes. Native compiler
and AllRules checks pass; strict clang-tidy still fails, and fixture execution
has not occurred. Native failure injection, provider allocation/timing evidence,
Ctrl+Break/generated-control/close/shutdown handling and complete CLI integration
remain open. This additional module does not complete any of the nine required
shared OS modules or relax their acceptance requirements.
