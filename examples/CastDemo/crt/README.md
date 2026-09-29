# Hand-written CRT library

The user explicitly requires a `crt` library and the `CRT_` function prefix.
This library supplies owned runtime functionality. It does not link Microsoft's CRT,
provide an allocator, or replace the requested `WinMainStartup` entry contract.

The current implementation provides counted `CRT_Memory_Compare`,
`CRT_Memory_Copy`, `CRT_Memory_Move` and `CRT_Memory_Set` operations with
explicit-width types, required-pointer assertions and documented extent/aliasing
contracts. The queue uses this library for fixed-size entry copies.

The build currently selects the MSVC x64 backend. Its leaf assembly supplies the
memory operations. A separate `crt-abi` archive supplies compiler-required `memcmp`,
`memcpy`, `memmove` and `memset` adapters to the same implementations; the CRT-free
targets link it. Keeping those adapters separate lets existing application callers
migrate without colliding with their remaining system runtime imports. Those
mandated spellings stay inside the backend. The assembly uses only volatile
registers and SSE2, preserves the stack and direction flag,
and accesses exact bounded extents. `_fltused` is supplied by a separate compiler
data module in the MSVC x64 assembly backend. It exports the exact compiler ABI
name and four-byte value in read-only storage; it has no C namespace declaration
or executable code. Forced symbol retention resolves the marker before LTCG; C optimization
and whole-program optimization remain enabled.

The x64 backend also supplies `CRT_Stack_ProbeCore` and the compiler-mandated
`__chkstk` adapter. It touches each pending stack page in descending order,
preserves the compiler's argument registers and frame size, and leaves the
stack adjustment to the caller. It does not disable compiler stack probing or
import a system runtime. Stack reservation and commitment still belong to
process/thread initialization; this helper is not proof that application thread
stacks are fully committed before interaction.

No heap, startup initializer, locks, waits, I/O, external runtime helpers, or mutable
global operational state are used by these memory operations. Ordinary compare and
fill do not promise constant-time secret comparison or guaranteed secure erasure.

Verification covers independent expected bytes, lengths and alignments, both overlap
directions, equal pointers, first unsigned-byte differences, zero length, protected
page boundaries and caller-misuse assertions. A separate native ABI test calls all
four adapter symbols through volatile function pointers, checking their results
and the unsigned-byte conversion of negative and wider `memset` values.
Its volatile multi-page local exercises compiler-emitted stack probing and
checks every byte through the final partial page.
Release tests link with `/NODEFAULTLIB`;
maps/imports must continue to show owned helper definitions and no system CRT.

This is not a complete C standard library or a claim that the full application is
free of system runtime dependencies. Other compiler/target backends, remaining
string/numeric/formatting operations, application call-site migration and comparative
performance measurements remain open. Unsupported targets fail configuration until
their ABI backend is implemented and verified. Existing GUI/CLI CRT startup and other
runtime dependencies remain defects under the full CastDemo goal.
