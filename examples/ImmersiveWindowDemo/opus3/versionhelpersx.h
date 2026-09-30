#ifndef VERSIONHELPERSX_H
#define VERSIONHELPERSX_H

#include <windows.h>
#include "delayimpx.h"

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * versionhelpersx.h -- extended-library counterparts to the SDK's inline VersionHelpers (versionhelpers.h
 * origin). The SDK helpers are inline wrappers over kernel32!VerifyVersionInfoW, which since Windows 8.1
 * is subject to the application-manifest compatibility shim: without the right supportedOS GUIDs in the
 * manifest, VerifyVersionInfo caps the reported version at 6.2, so IsWindows10OrGreater() returns FALSE
 * on a real Windows 10. It also cannot distinguish Windows 11 from Windows 10 (both report 10.0).
 *
 * The *Ex predicates below resolve the version from ntdll!RtlGetVersion instead -- the kernel's real
 * major/minor/build, immune to the manifest shim -- so they answer truthfully regardless of manifest, and
 * the build number lets IsWindows11OrGreaterEx exist at all. The Ex suffix also keeps these clear of the
 * SDK inline names if a translation unit includes both headers. No A/W split: these read no strings.
 */

/*
 * RtlGetVersionEx -- delay-bound wrapper for ntdll.dll!RtlGetVersion. ntdll is always resident, so it is
 * GetModuleHandle-resolved on first use (DECLARE_DLL_THUNK). lpVersionInformation is filled per its
 * dwOSVersionInfoSize field (set it to sizeof(OSVERSIONINFOEXW) to receive wProductType). Returns an
 * NTSTATUS (LONG): 0 (STATUS_SUCCESS) on success; a negative value if ntdll or the export is somehow
 * absent -- not possible on any NT since Windows 2000, but the predicates fail closed to FALSE if so.
 * OSVERSIONINFOEXW is layout-compatible with RTL_OSVERSIONINFOEXW, so it is used directly to avoid
 * winternl.h. The wrapper name is distinct from the export so the real prototype is not redefined.
 */
/* Body generated as static FORCEINLINE here -- each consumer inlines its own copy; no out-of-line
   symbol, no C4711. SAL omitted: the macro-generated body cannot carry matching annotations (C28251). */
#include "versionhelpersxThunks.inl"

/*
 * Core predicates. IsWindowsBuildOrGreaterEx is the full primitive (adds the build-number axis the SDK's
 * IsWindowsVersionOrGreater lacks); IsWindowsVersionOrGreaterEx mirrors the SDK's 3-argument signature
 * (build treated as 0). Both compare (major, minor, build, servicePackMajor) lexicographically against
 * the running OS and return TRUE when the OS is at least the requested version.
 */
BOOL WINAPI IsWindowsBuildOrGreaterEx(WORD  wMajorVersion,
                                      WORD  wMinorVersion,
                                      DWORD dwBuildNumber,
                                      WORD  wServicePackMajor);
BOOL WINAPI IsWindowsVersionOrGreaterEx(WORD wMajorVersion,
                                        WORD wMinorVersion,
                                        WORD wServicePackMajor);

/* Named release predicates -- shim-immune supersets of versionhelpers.h's inline family. */
BOOL WINAPI IsWindowsXPOrGreaterEx(void);
BOOL WINAPI IsWindowsXPSP1OrGreaterEx(void);
BOOL WINAPI IsWindowsXPSP2OrGreaterEx(void);
BOOL WINAPI IsWindowsXPSP3OrGreaterEx(void);
BOOL WINAPI IsWindowsVistaOrGreaterEx(void);
BOOL WINAPI IsWindowsVistaSP1OrGreaterEx(void);
BOOL WINAPI IsWindowsVistaSP2OrGreaterEx(void);
BOOL WINAPI IsWindows7OrGreaterEx(void);
BOOL WINAPI IsWindows7SP1OrGreaterEx(void);
BOOL WINAPI IsWindows8OrGreaterEx(void);
BOOL WINAPI IsWindows8Point1OrGreaterEx(void);
BOOL WINAPI IsWindowsThresholdOrGreaterEx(void);
BOOL WINAPI IsWindows10OrGreaterEx(void);

/* Extension beyond the SDK family: Windows 11 (10.0.22000+), expressible only via the build number. */
BOOL WINAPI IsWindows11OrGreaterEx(void);

/* TRUE when the running edition is any Server SKU (wProductType != VER_NT_WORKSTATION). */
BOOL WINAPI IsWindowsServerEx(void);

#ifdef __cplusplus
}
#endif

#endif /* VERSIONHELPERSX_H */
