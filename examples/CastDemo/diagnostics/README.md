# Dynamic validation

Run from Linux, or from PowerShell using the installed Ubuntu WSL2 distribution:

```powershell
wsl -d Ubuntu -- python3 /mnt/c/dev/cvk/scripts/check_cast_dynamic.py
```

The runner configures and builds each mode, then executes its fixtures only if
that mode's strict build succeeds. Failed compilation never falls through to an
old executable. Logs and the non-success report are retained under
`build/cast-diagnostics/<mode>/` and `build/cast-diagnostics/report.json`.

| Mode | Enabled checks |
| --- | --- |
| `memcheck` | Valgrind memory accesses, undefined-value origins, all leak kinds and open file descriptors; no default suppressions; errors fail the process |
| `address` | ASan, LeakSanitizer on Linux, UBSan, integer/implicit-conversion/local-bounds/float-divide-by-zero checks, stack use after scope/return; no recovery |
| `memory` | MemorySanitizer with origin tracking; no recovery |
| `thread` | ThreadSanitizer; no recovery |

The sanitizer modes are separate because Clang cannot combine AddressSanitizer,
MemorySanitizer and ThreadSanitizer in one executable.
[Clang's sanitizer combinations](https://clang.llvm.org/docs/UsersManual.html#controlling-code-generation)
and [Valgrind Memcheck options](https://valgrind.org/docs/manual/mc-manual.html)
define the checks used here.

All modes retain optimization and LTO, `FORCE_INLINE`, warnings as errors, and the
shared C clang-tidy policy. The user's later exception excludes external-library
headers; owned code retains all checks except compiler-padding warnings. This may report optimizer-sensitive Memcheck findings;
they require investigation, not blanket suppression or an optimization downgrade.

The diagnostic executables use hosted startup so their sanitizer runtimes can
initialize. Their hand-written memory backend is instrumentable C. These binaries
are test artifacts, not CRT-free Release deliverables or allocation-lifecycle
evidence for the production executable. Production startup/link policy is unchanged.
MSan additionally requires compatible instrumentation of dependent code; the
current configuration is not evidence that external libraries are instrumented.

## Current observed scope and result

Valgrind 3.22.0, Clang/clang-tidy 18.1.3, LLVM, LLD and compiler sanitizer runtimes
were installed in Ubuntu 24.04 WSL2. A Valgrind run of `/usr/bin/true` completed
with zero errors and no suppressions. That checks tool startup only.

The diagnostic project compiles all top-level `utl/*.c` sources, the POSIX argument
reader, the HTTP policy adapter, and the hand-written memory layer. It selects 16
existing fixture bodies. The older format/protocol/HTTP fixtures now use
`OS_Application_Main` on both platforms; native startup and failure handling belong
to the selected backend.

All four configurations were attempted. Their strict builds fail on source and
included-header diagnostics, including unsafe buffer access, missing direct
includes and reserved identifiers in compiler headers. No application fixture has
executed under these new modes, and no sanitizer or Valgrind clean claim is made.

Full Windows application instrumentation/runtime startup, GUI/display backends,
network/TLS, native concurrency, Media Foundation, CVK/Vulkan and driver boundaries
remain uncovered. The imported `gfxlib` has not yet joined this build. The thread
mode currently checks single-threaded fixtures and cannot establish race coverage
for the application. Portability work and production scenarios must extend this
matrix without reducing the complete goal.
