# CastDemo specification implementation map

This implements the inventory requirement in [REFACTOR_REQUIREMENTS.md](REFACTOR_REQUIREMENTS.md).
The complete goal remains authoritative. All rows are open until the specification
semantics used by every production reader and writer are implemented and verified.
Names below describe intended responsibilities; they do not imply those libraries
already exist. Splitting or combining modules must preserve every responsibility.

## Required library properties

Follow `C:/dev/xml/include/utl/utl_xml.h`, `utl_arinc661.h` and their source files:
typed specification models, explicit-width counts, borrowed or caller-owned storage,
documented capacity/lifetime/error contracts, numbered requirements and precise
specification references. No CRT, heap, Win32 I/O, sockets, codec creation or GUI
operations belong in the pure wire-format implementation.

Readers and writers must validate arithmetic, truncation, enum values, lengths,
nesting and capacity without partially publishing invalid results. Unknown data
needs the specification's skip/preservation rules, not accidental acceptance.
Application restrictions must be distinguished from normative limits; a local
buffer capacity must not be described as a protocol-imposed maximum.

Each library needs known external wire vectors, malformed/truncated inputs, boundary
and capacity cases, round trips where useful, and integration at the actual CastDemo
call sites. A test-only library or a replacement outer header parser is incomplete.

## Producers, consumers and missing coverage

| Specification/responsibility | Existing production locations | Required coverage | Current state |
| --- | --- | --- | --- |
| Protocol Buffers wire format | `utl_protobuf`, `utl_castv2`, `cast_message.c` | Typed fields; bounded varints including the tenth byte; tags/wire types; fixed fields; counted data; unknown fields/groups; overflow and truncation; deterministic writer | Wire reader/writer and bounded group skipping now drive production Cast encoding/decoding. Boundary tests pass in CRT-free executables; whole-program convention/MISRA and lifecycle evidence remains open |
| CastV2 envelope and framing | `utl_castv2`, `cast_message.c`, `cast_channel.c` | Chromium CastMessage schema; version and payload enums; required/optional fields; UTF-8/binary distinction; duplicate/unknown-field behavior; big-endian frame lengths and incremental framing | Version-1.0 typed envelope and stateless frame inspection are connected to production. Required-field, duplicate, unknown/group, binary, UTF-8, fragmentation and capacity vectors pass. Authentication, later-version chunking and end-to-end receiver/TLS verification are not completed |
| JSON | `cast_session.c`, `utl_castjson`, `cvk_json` dependency | Typed caller-owned model; string escapes/Unicode; numbers and overflow; arrays/objects; nesting and capacity; reader/writer errors | The LOAD writer validates/escapes counted UTF-8 and writes bounded integer/fixed-point numbers; a complete generic JSON module and reader migration remain open |
| Cast connection, heartbeat, receiver and media messages | `cast_session.c`, `utl_castjson` | Typed CONNECT, PING/PONG, LAUNCH, receiver/media status, LOAD, PLAY/PAUSE/STOP, SEEK and errors; IDs, session changes, track selection, live/buffered media and unknown extensions | The production LOAD profile uses a typed writer; other messages and incoming status/errors still use hand-built strings and ad hoc JSON lookups |
| HTTP message syntax and semantics | `utl_http`, `cast_http`, `media_server.c`, `ssdp.c` | Counted request/status lines and headers; framing; GET/HEAD/OPTIONS handling; limits; complete/partial input; response serialization; connection lifecycle; malformed requests | Caller-owned head reader/writer and field lookup distinguish complete, incomplete, invalid and capacity outcomes. Framing derivation checks length/coding precedence, duplicate lengths, Connection tokens and Host count. Independent wire, prefix, malformed response, framing and capacity vectors pass the CRT-free HTTP test. SSDP response heads use the counted reader. HTTP URI endpoint and origin-form operations now serve discovery. The server now uses counted heads and typed response output through cast_http; its new fixture has not executed under the strict gates. Chunked body/trailer processing, complete target/Host semantics and connection ownership remain open |
| HTTP byte ranges and CORS | `utl_http`, `utl_decimal`, `media_server.c` | RFC range grammar, satisfiability, suffix/open-ended ranges, overflow, multiple ranges policy, Content-Range/Accept-Ranges and CORS request/response behavior | Range/Content-Range models, queries, readers, writers and resolution are consolidated into `utl_http`; the separate range module is removed. Production callers use the new API and validate complete sets before selecting the first satisfiable member. Numeric and range vectors pass in the CRT-free protocol test. File/subtitle/growing response fields and GET/HEAD range policy now use the typed models. New strict-gated fixtures remain unexecuted. Complete preconditions, multipart and CORS semantics remain open |
| SSDP/UPnP discovery | `utl_ssdp`, `utl_http`, `ssdp.c` | M-SEARCH serialization; HTTPU response parsing; search target, status, location and device identity; duplicate/timeout behavior | Typed IPv4 M-SEARCH drives production discovery. Interface/query/search resources and description buffers now have retained startup ownership; runtime verification is pending. Advertisements, IPv6, expiry scheduling, extensions, HTTP request allocation and provider lifetime/timing evidence remain open |
| DIAL discovery/device description | `utl_upnp`, `ssdp.c` | DIAL device/service fields and description semantics used by discovery; typed results and protocol errors | Root-device display identity now uses namespace-aware direct-child selection and duplicate/simple-content validation. Full UPnP/DIAL device/service/profile semantics and strict runtime verification remain open |
| XML device documents | `utl_xml`, `utl_upnp`, `ssdp.c` | XML names, attributes, text, entities, encoding and nesting required by device descriptions; schema-specific traversal separate from XML syntax | Counted UTF-8 document parsing, namespace scope, normalized text and transactional output now replace CRT substring extraction. Fixed records are owned at application startup. DTD processing, other XML encodings, complete conformance evidence and strict runtime verification remain open |
| URI/URL | `utl_url`, `utl_http`, `ssdp.c`, `cast_gui.c`, `cast_demo.c`, `cast_session.c` | Scheme, authority/host/port, IPv4/IPv6 representation, path/query/fragment, escaping and validated HTTP(S) URLs | Counted RFC 3986 reference model, grammar reader, recomposition, port and percent-decoding queries now drive discovery. HTTP endpoint and origin-form derivation preserve queries and reject invalid input without truncation. Relative resolution, IRI/IDNA, zone identifiers, other application migrations and strict runtime verification remain open |
| Unicode | `utl_unicode`, `utl_castv2`, `subtitles.c`, GUI/CLI conversion helpers, JSON/text handling | UTF-8 and UTF-16 decoding/encoding, BOMs, surrogate/overlong/out-of-range rejection, counted output capacity | Scalar codecs, width queries and validators have historical functional coverage. A new caller-owned BOM-aware byte reader supports either UTF-16 byte order and is integrated into subtitle conversion. New reader fixtures compile under native analysis but have not run from a successful strict build. Remaining GUI/CLI/JSON migration and full lifecycle evidence are open |
| ISO BMFF/MP4 structure and timelines | `mp4_demux.c`, `mp4_index.c`, `mp4_mux.c` | Box/FullBox models; hierarchy; sizes/UUIDs; movie/track/media headers; sample descriptions/tables; offsets/timescales; edit/composition timing; sync samples; overflow | `utl_mp4` only implements Box headers; most semantics remain ad hoc |
| Fragmented MP4 | `mp4_mux.c`, `gpu_transcode.c`, `media_server.c` | Initialization metadata and brands; mvex/trex; moof/mfhd/traf/tfhd/tfdt/trun; defaults/flags; offsets, decode/composition timestamps and fragment boundaries | Ad hoc mux logic and growing-file transport |
| EBML | `mkv_subtitles.c` | Header/schema limits; VINT context; IDs and sizes; typed primitive data; master traversal; unknown-size boundaries; permitted/default values | `utl_matroska` contains VINT/element-header primitives and named element constants; the semantic model remains partial |
| Matroska | `mkv_subtitles.c` | Segment/Info/Tracks/Cluster models; language and codec metadata; timestamp scale/offsets; blocks/groups, lacing, durations, track selection and content encodings | Extraction now selects during metadata traversal with one retained record; most traversal, timing and block logic remain outside the new library |
| SubRip/SRT | `utl_srt`, `subtitles.c` | Counted cue/timestamp/text model; accepted de facto dialect documented; BOM/newline variants; invalid/overflow times and cue boundaries | Counted cue/timestamp reader now drives sidecar conversion; inline markup, timing-extension semantics and Matroska styled-text integration remain open |
| WebVTT | `utl_webvtt`, `subtitles.c`, `media_server.c`, `cast_session.c` | Header/metadata and cues; timestamps/settings/text escaping; safe serialization, timing and delivery; parsed input validation | Typed timestamp reader, exact size queries, timing writer and transactional plain-text cue writer are integrated for embedded subtitle delivery. Full-file reading, ordered cues, settings, regions and styled text remain open; sidecar WebVTT still receives only a prefix check |
| 3GPP timed text / tx3g | `mp4_demux.c`, `subtitles.c` | Text sample entry/configuration, counted text encoding and modifiers used by the app; MP4 presentation/edit timing; safe WebVTT conversion | Counted text payload now uses shared Unicode decoding and plain WebVTT serialization without temporary decoded strings. Sample entry, modifier and presentation/edit models remain missing |
| H.264/AVC bitstream syntax | `h264_params.c`, `gpu_decode.c`, `gpu_encode.c` | NAL/RBSP and emulation prevention; bit reader; unsigned/signed Exp-Golomb; SPS/PPS/scaling/cropping; slice/POC/reference data used by Vulkan | Ad hoc reader and Vulkan-specific structures |
| AVC decoder configuration / NAL framing | `mp4_demux.c`, `mp4_mux.c`, `gpu_transcode.c` | avcC parameter arrays and profile data; NAL length widths; Annex B versus length-prefixed representation; capacity and truncation | Embedded demux/mux/transcode routines |
| MPEG-4 descriptors and AAC configuration | `mp4_demux.c`, `mp4_mux.c`, `audio_gain.c` | Descriptor tags/lengths, esds/DecoderSpecificInfo, AudioSpecificConfig, object types, sample rates and channel configuration used by the app | Embedded descriptor/configuration byte sequences and MF types |
| PCM/audio representation | `audio_gain.c`, `gpu_transcode.c`, Media Foundation boundary | Sample width/signedness, channels/layout/rate, block alignment, timestamps, gain scaling and saturation with a live pipeline | MF preparation path; whole-file conversion still present |
| Codec/container compatibility | Demux, mux, subtitle and MF integration | Account for every accepted input, including the user's HEVC/EAC3 Matroska source, AAC/MP3 output compatibility and subtitle formats; distinguish parsed formats from opaque pass-through | Existing behavior partly tested; full specification/dependency inventory remains open |

## Dependency boundaries are also in scope

The required shared execution, synchronization and storage modules are `os_thread`,
`os_fiber`, `os_mutex`, `os_localmutex`, `os_semaphore`, `os_localsemaphore`,
`os_queue`, `os_ringbuffer` and `os_atomic`. [OS_MODULES.md](OS_MODULES.md) records their mandatory
implementation and integration requirements. They remain open alongside the
specification libraries; OS wrappers do not exempt dependency allocations or waits.

Argument transport is also represented by a shared `Utl_Arguments_Read` interface.
The build selects `utl/win32/utl_arguments.c` for Microsoft command-line quoting or
`utl/posix/utl_arguments.c` for already-separated argv records. Neither implementation
uses target-selection preprocessor branches. Caller-owned records contain counted
native-order UTF-16 after OS encoding conversion; the POSIX reader preserves spaces,
quotes, empty arguments and record boundaries without applying shell parsing.
Win32 process access and native entry are in `os/win32`. POSIX process startup and
encoding conversion, and migration of the remaining application calls, remain open.

| Boundary | Current use | Evidence still required |
| --- | --- | --- |
| Winsock/DNS/network adapters | Discovery, channel setup and HTTP serving | Initialization/runtime allocations, persistent resources, bounded progress, cancellation, retries and failures |
| Schannel/TLS | Cast control channel | Credential/context/token allocation lifetimes, handshake/reconnect behavior, framing, error handling and no-CRT linkage |
| WinHTTP | Device description retrieval | Connection/request allocation, internal buffers, timeout/cancel behavior and replacement needed to satisfy the goal |
| Media Foundation/COM/codecs | Audio inspection, decoding and AAC preparation | All created objects/buffers and internal allocations, streaming gain, format changes, timestamp fidelity and teardown |
| CVK/Volk/Vulkan/driver | Device/session creation, GPU decode/encode and transfers | Transitive CRT/allocator imports, CPU/GPU allocation timing, persistent capacity, synchronization and error recovery |
| Win32 GUI/common controls/dialogs/GDI | Window, controls, painting, browsing and DPI/theme changes | Application and dependency allocation timing, retained resources, bounded handlers, canonical message flow and UI behavior |
| Windows file/mapping APIs | Container/subtitle reads and output files | Handle/view/buffer lifetime, bounded access, cancellation and reuse without per-interaction allocation |

Using an OS implementation does not close a specification row or exempt its
allocation behavior. These boundaries need explicit evidence; unsupported behavior
must be reported rather than silently narrowed out of the goal.

## Completion review

The current CastV2 module cites the version-1.0 schema blob
`fd226541d5abdf22730b462ec9de946aa9b021d4` and Chromium's four-byte framing and
65536-byte implementation limit. It implements the envelope currently used by
CastDemo. The [newer Open Screen schema](https://chromium.googlesource.com/openscreen/+/refs/heads/main/cast/common/channel/proto/cast_channel.proto)
also defines versions 1.1 through 1.3, chunk metadata and authentication messages.
Those are explicit remaining coverage items, not silently implemented by skipping
unknown protobuf fields. Cast JSON namespaces remain a separate open row; the
new LOAD writer is limited to the explicitly described profile below.

The Unicode module implements counted scalar encodings, not normalization or a
locale library. Its UTF-16 interface uses native-order units and preserves U+FEFF.
The subtitle/URL/JSON/platform layers still need their own byte-order, BOM and
encoding contracts and production migrations.

The byte-range module follows RFC 9110 sections 5.6.1 and 14. The decimal model is
bounded to uint64_t and caller-provided slot counts; these are application limits,
not limits imposed by HTTP. Empty-list-member tolerance is bounded to 32. The
server adapter uses 32 range slots, ignores invalid/excessive sets, and ignores
Range on empty content. The pure resolver separately preserves the RFC's
satisfiable nonzero suffix of a zero-byte representation. Full HTTP remains open:
request-body framing, duplicate/oversized field handling, Range on HEAD, growing
file response semantics, typed response serialization and persistent server workers
are not completed by replacing the Range value parser.

For each row, record the exact source specification/revision, model/API files,
production callers, retained features, supported productions/fields, capacity and
error policy, test evidence, dependency lifecycle evidence and unresolved gaps.
Re-scan the complete program after each migration: additional encountered formats
belong in this inventory. Do not declare the specification work complete merely
because two or several module pairs exist.


## Cast LOAD implementation evidence and remaining scope

`utl_castjson.h/.c` models the request ID, counted UTF-8 session/content/type/title,
autoplay, stream kind, integer millisecond position and optional selected WebVTT
subtitle. It preserves the demo's existing subtitle outline style. Query and write
share one bounded serializer; malformed strings or insufficient capacity leave
output unchanged. Every caller-supplied string, including session ID and MIME
type, follows the same escape rules. No escaped-string/track scratch arrays,
allocation, CRT, native calls or global mutable state occur in this module.

Production `CastSession_FormatLoad` now feeds that model instead of using
`snprintf`. Its bounded termination scan uses the shared Unicode utility; invalid
times, unsupported stream tokens and missing terminators fail before publication.
The previous 25 KB stack diagnostic no longer appears in native session analysis.
The GUI and CLI now retain startup-initialized session storage across preparations
and resets. Native channels, playback and other dependencies still allocate, so
the application allocation requirement remains open.

The exact source references are Google's
[media LOAD message](https://developers.google.com/cast/docs/media/messages#load),
[sender LoadRequest fields](https://developers.google.com/cast/docs/reference/web_sender/chrome.cast.media.LoadRequest),
and [RFC 8259 JSON syntax](https://www.rfc-editor.org/rfc/rfc8259.html).
The single subtitle, generic metadata, fixed style, unsigned request IDs and
uint64_t millisecond domain are explicit application-profile limits, not claims
about the complete protocol grammar.

Independent wire fixtures, all short output capacities, every control escape,
supplementary UTF-8, invalid encodings, invalid/missing model fields and maximum
integer media time are in `tests/cast_json_test.c`. Native compilation/AllRules
analysis pass, but strict clang-tidy still fails; the new fixtures have not run
from a successful strict build. Other Cast message kinds, response/error models,
general metadata/tracks/style, generic JSON parsing and complete receiver/runtime
verification remain open.

## SSDP implementation evidence and remaining scope

`utl_ssdp.h/.c` models counted search requests and responses, validates target
and USN relationships, reads max-age with quoted-extension handling, and
preflights complete M-SEARCH serialization. Production `ssdp.c` uses it with
`utl_http`, keeps exact counted locations, deduplicates USNs across interfaces
and checks capacity on each received response. Independent request bytes,
all short output capacities, prefix truncation, duplicate/missing fields,
identity mismatches and numeric boundaries pass in the SSDP fixture.

The specification basis is UPnP Device Architecture 1.1 sections 1.1/1.3,
including required compatibility with UPnP 1.0 UUIDs:
https://upnp.org/specs/arch/UPnP-arch-DeviceArchitecture-v1.1.pdf

This is the IPv4 M-SEARCH exchange subset. Advertisements, subscriptions,
full UPnP 1.1 extension semantics, IPv6, URL resolution/validation, expiry
scheduling, DIAL Application-URL/REST and device-description XML remain open.
Native discovery/HTTP resources, mapped media and codec paths still allocate
after initialization. The new parser does not close those lifetime defects.

## Unicode byte reader and WebVTT implementation evidence

`Utl_Unicode_InitializeReader` borrows counted storage and consumes one optional
leading UTF-8 or UTF-16 byte-order signature. `ReadScalar` handles unaligned UTF-16
in either byte order and reports scalar, end or invalid input. Failed and repeated
end reads preserve the output value and cursor. Embedded NUL and noninitial FEFF
are preserved by the codec; subtitle adapters separately reject NUL. The reader
has no allocation, CRT or native I/O operations.

`utl_webvtt.h/.c` implements counted timestamp grammar and canonical timestamp,
timing-line and literal cue writers. Query and write share a bounded serializer;
invalid timing, encoding or output capacity leave caller output unchanged. The
plain-text profile escapes markup delimiters, normalizes CRLF/CR and protects
empty payload lines from becoming cue separators. Milliseconds use uint64_t;
this representation limit is distinct from the wire grammar. Complete file
reading, ordering, settings, regions and styled cue semantics remain open.

The exact source references are
[WebVTT CRD, 20 May 2026, sections 4.1 and 4.2.2](https://www.w3.org/TR/2026/CRD-webvtt1-20260520/)
and [Unicode 17.0 chapter 23.8](https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-23/).
Embedded Matroska timing and MP4 plain tx3g output now use the new writer.
Matroska inline markup remains in an adapter awaiting its full SubRip/styled-text
model. Sidecar input, normalization and output now reuse two startup-owned buffers;
the input temporarily borrows output until decoding into disjoint scratch is done.
The GUI keeps the original sixteen-MiB payload limit. Capacity failures invalidate
published output, and the next conversion reuses the same storage. CRT file and
container readers, discovery and the media server's copied text/request ownership
still have allocation/lifetime defects. First-touch does not prove residency or
realtime behavior; synchronous conversion and full specification coverage remain open.

`tests/cast_webvtt_test.c` contains independent BOM/endian and supplementary
scalar vectors, repeated invalid/end reads, invalid encodings, timestamp widths
and range boundaries through UINT64_MAX, prefix truncation, literal escaping,
newline conversion and all insufficient output capacities. Native compilation
and analysis pass. Strict clang-tidy still rejects buffer-access and included
header findings. These new fixtures have not linked or executed from a successful
strict build; no runtime success or complete specification compliance is claimed.

## SubRip cue reader implementation evidence

`utl_srt.h/.c` now owns the counted UTF-8 cue envelope and integer timestamp
parser used by sidecar conversion. Its typed result borrows identifier, payload
and timing-suffix spans and records integer endpoints plus source hour widths.
Invalid input preserves every result field and the reader cursor. End consumes
trailing blank separators once and preserves the last output on repeated reads.
Line boundaries use counted offsets rather than pointer differences. Parsing
performs no allocation, native I/O or system-CRT calls.

The descriptive sources are
[Matroska SRT subtitles](https://www.matroska.org/technical/subtitles.html#srt-subtitles)
and [Library of Congress fdd000569, updated 27 May 2025](https://www.loc.gov/preservation/digital/formats/fdd/fdd000569.shtml).
The explicit compatibility profile accepts optional/nonnumeric identifiers,
comma or period fractions, CRLF/CR/LF, optional final separation, empty payloads
and opaque timing suffixes. Arrow-bearing identifiers, embedded NUL, malformed
UTF-8 and reversed/empty intervals fail. No 32-character line limit or cue-order
restriction is imposed. Timestamp arithmetic supports the entire uint64_t
millisecond range; the adapter separately retains its four-source-hour-digit
application limit.

`subtitles.c` now consumes complete typed cues and writes canonical WebVTT timing.
The old CRT string-scanning/framing and private timestamp parser are removed.
Payload markup remains borrowed/raw, and timing extensions are retained by the
reader but omitted from converted WebVTT. Full styled-text conversion, extension
interpretation, Matroska integration and full WebVTT file validation remain open.

`tests/cast_srt_test.c` covers independent timestamps through UINT64_MAX and its
immediate overflow, every incomplete timestamp/timing-line prefix, mixed line
endings, optional identifiers, retained suffixes, empty payloads, invalid encoding
and repeated INVALID/END with field-by-field publication checks. Native compile
and analysis runs show no owned SRT diagnostics. Strict clang-tidy still reports
buffer-access and included-header findings; the fixture has not executed from a
successful strict build. See the SRT logs in `REFACTOR_REQUIREMENTS.md`.

## Counted URL and HTTP endpoint progress

`utl_url` models URI-reference components and explicit delimiter presence. It
validates registered names, IPv4, IPv6 and IPvFuture grammar, leaves escapes
encoded during splitting, and provides a separate counted percent decoder.
Recomposition preserves empty components, case and escape spelling. Invalid
syntax and insufficient output capacity preserve caller outputs. The grammar
and recomposition references are [RFC 3986 sections 2-4 and 5.3](https://www.rfc-editor.org/rfc/rfc3986.html).

`utl_http` derives HTTP(S) host/port/security values, applies scheme defaults,
rejects received userinfo and writes origin-form targets. An empty path becomes
one slash, queries remain encoded and fragments are omitted, following
[RFC 9110 section 4.2](https://www.rfc-editor.org/rfc/rfc9110.html#section-4.2)
and [RFC 9112 section 3.2.1](https://www.rfc-editor.org/rfc/rfc9112.html#section-3.2.1).

Discovery now removes its CRT host slicer and WinHttpCrackUrl dependency. Its
native host/target buffers are retained in startup storage. Complete parsing and
capacity validation precede a request; device publication remains transactional.
Explicit port zero is rejected at this boundary because WinHttpConnect uses zero
as its default-port sentinel. Native host support currently excludes IPvFuture,
international names requiring IDNA, and decoded non-DNS characters. These are
reported failures, not silent normalization or truncation. WinHTTP's fixed UTF-16
and preconverted-IDN requirements are documented by
[WinHttpConnect](https://learn.microsoft.com/en-us/windows/win32/api/winhttp/nf-winhttp-winhttpconnect).

The URL fixture includes RFC examples, malformed syntax and literal hosts,
percent-decoded octets, all insufficient output capacities, preservation after
failure, default/explicit ports and independent HTTP target bytes. Native compile
and analysis pass for the changed format sources and fixture. Strict clang-tidy
still fails on buffer-access and included-header findings; the fixture has not
executed. Full URL integration, complete XML coverage, WinHTTP runtime allocation and
bounded native I/O remain open. No specification row is closed by this work.

## Counted XML and device-description progress

`utl_xml` retains an iterative tree of elements, attributes and counted text in
caller-owned records. It validates UTF-8, XML characters/names, balanced tags,
one document root, attribute separation/uniqueness, declarations, comments,
processing instructions, CDATA and numeric/predefined references. Namespace
resolution handles scoped/default bindings, implicit xml, reserved xml/xmlns
rules, undeclaration and duplicate expanded attribute names. Normalized text
comparison, sizing and output share one scalar interpretation. Complete parsing
precedes root publication; failed reads revoke the root and counts, and failed
text writes preserve output. Source borrows expire on the next document read.

The normative references are [XML 1.0 Fifth Edition](https://www.w3.org/TR/2008/REC-xml-20081126/)
and [Namespaces in XML 1.0 Third Edition](https://www.w3.org/TR/xml-names/).
This implementation currently consumes UTF-8 only; DTD processing and other
encodings return UNSUPPORTED. It is not a complete general XML processor.

`utl_upnp` selects the unique direct device under the UPnP root namespace, then
its unique nonempty simple friendlyName and optional simple modelName. Embedded
devices and vendor-namespace fields cannot supply those values. The reference
is [UPnP Device Architecture 2.0, sections 2.3 and 2.8](https://openconnectivity.org/upnp-specs/UPnP-arch-DeviceArchitecture-v2.0-20200417.pdf).
The standard requires UTF-8 device descriptions and requires modelName; this
display adapter deliberately retains the prior application's missing-model
behavior and therefore does not constitute complete schema/profile validation.
Complete device/service semantics and the DIAL application profile remain open.

Discovery now invokes these modules using its exact accepted HTTP-body count.
XML records are bound in the startup owner, with conservative capacities derived
from its fixed admitted body extent. No parser or display-adapter operation
allocates or performs native I/O. WinHTTP still creates connection/request
resources during discovery; that is an unchanged allocation defect.

The new fixture covers independent XML and display-selection vectors, malformed
namespaces/references/encoding, every incomplete prefix of a nested document,
short output capacities, record exhaustion, guarded writes and repeated reuse.
Native compilation/AllRules pass. Strict buffer/header analysis findings remain,
so the fixture has not executed and no conformance or runtime pass is claimed.

## Counted HTTP serving progress

`media_server.c` now dispatches only a ready `cast_http` request model backed by
`utl_http`'s incremental counted reader. The application rejects ambiguous
framing, nonempty/transfer-coded request bodies and unsupported versions/methods
before interpreting pipeline bytes. The admitted head has an exact boundary;
remaining bytes are retained for the next request. Fixed field storage covers
the server's entire admitted head extent, and parser terminal states latch until
explicit initialization.

File and WebVTT responses share method, persistence and range policy. HEAD ignores
Range and sends no content. GET validates the complete range set before choosing
a satisfiable interval; malformed, duplicate, excessive and unknown-unit ranges
are ignored. If-Range is ignored by choosing the whole representation because
the application supplies no validator. Response metadata uses the specification
module's counted head/Content-Range writers; no raw header substring scanning or
CRT response-header formatting remains in these handlers.

An ordinary growing-file GET streams to the live end with connection closure.
A satisfiable range serves a finite snapshot with Content-Length and a 206
Content-Range whose final total is unknown. A 416 reports the current snapshot
length. These semantics follow [RFC 9110 sections 14.1.2, 14.2 and 14.4](https://www.rfc-editor.org/rfc/rfc9110.html#section-14).
Receiver behavior during conversion and subsequent seeks still requires runtime
verification. The existing CORS fields are preserved; complete origin/preflight
policy, conditional requests, target/Host URI semantics, general chunked bodies
and trailers remain open.

The new fixture has 37 independent request vectors and eight exact response
forms, including unknown-total partial content, forbidden Content-Length on 204,
all short capacities, guarded output, pipeline-tail retention and repeated reuse.
It passes native compilation/AllRules but has not executed because required
clang-tidy buffer/header diagnostics remain. The later startup-pool migration
removes connection metadata allocation, per-connection thread creation and copied
subtitle bytes. Native socket/file allocation, synchronous transport and complete
shutdown evidence remain open. No specification row is closed.

## IPv4 presentation and route integration

The URL module now exposes numeric IPv4 sizing, reading and writing, sharing one
grammar with its URI host classifier. It implements the four-octet dotted-decimal
production in [RFC 3986 section 3.2.2](https://www.rfc-editor.org/rfc/rfc3986.html#section-3.2.2).
Numeric values place the first octet in the most significant byte independent of
the target's byte order. Readers preserve results on rejection; writers preflight
the complete extent before output and append no implicit terminator.

`cast_network` uses those operations to present the retained OS route reader's
result. GUI/CLI route lookup no longer creates and connects a temporary socket or
uses native IPv4 text conversion. Source scans have an explicit extent and the
IPv4 grammar's maximum; output, including the application string terminator, is
published only after complete validation and capacity checks. Discovery and route
readers have distinct ownership to avoid sharing state across UI/worker access.

Thirty independent IPv4 vectors cover exact numeric byte order, octet boundaries,
leading zeroes, excess/missing components, signs, whitespace, appended ports and
embedded zero bytes. All short writer capacities retain guard bytes. Native route
fixtures cover loopback output and resource reuse, but strict buffer/header
analysis still prevents executing these additions. Full URI/Unicode coverage and
native allocation/latency evidence remain open.

The native media listener now consumes `Utl_Url_ReadIpv4` too. Its bounded string
adapter accepts NULL as the explicit all-interface option and rejects invalid
non-NULL syntax before native startup, instead of falling back to all interfaces.
Native byte order is applied only after successful numeric parsing. Production
fixtures include invalid-bind rejection and real persistent HTTP clients during
shutdown; they remain unexecuted behind strict analysis errors. No URL, HTTP or
native-transport specification row is closed by this integration.

The startup-owned proxy now validates copied native-order UTF-16 source paths
through the Unicode reader and parses the complete bounded local IPv4 address
through `utl_url`. Its served URL uses typed/countable scheme, host, port and path
components; `utl_decimal` writes the port and `utl_url` supplies delimiters.
All validation precedes retained job mutation and complete serialization precedes
session publication. No native/CRT URL formatting remains in `proxy.c`.
This integration does not close URI, Unicode, MP4, Matroska or codec coverage;
the conversion/serving dependencies still allocate and strict checks still fail.

HTTP policy now operates inside startup-owned request, field and response buffers.
Each admitted connection has an exclusive retained worker; parser state is reset
only after completion acquisition and native resource retirement. A listener's
WebVTT representation is a counted immutable borrow, retained through all range
responses and successful listener stop. No protocol grammar moved back into the
transport adapter. Native resource creation, full protocol coverage and receiver
behavior remain open; bounded metadata alone closes no specification requirement.
