/*
 * versionhelpersx.c -- single instantiation TU for the versionhelpers.h-family extended predicates. The
 * ntdll!RtlGetVersion thunk is instantiated once here (via the .inl); the IsWindows*OrGreaterEx family is
 * a thin lexicographic comparison over the version it reports. No CRT.
 */

#pragma runtime_checks("", off)
#if defined(_MSC_VER) && !defined(__clang__)
#pragma check_stack(off)
#endif
#pragma strict_gs_check(off)

#pragma comment(lib, "kernel32.lib")

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#include "versionhelpersx.h"
#include <sdkddkver.h>
#include <minwindef.h>
#include <winnt.h>
#include <WinBase.h>

#define WINDOWS_11_INITIAL_BUILD 22000U

/*
 * VersionAtLeast -- TRUE when the running OS is at least (major, minor, build, sp), compared
 * lexicographically in that order of significance. The version is read once from RtlGetVersionEx; a
 * negative NTSTATUS (the unreachable thunk-miss path) reports FALSE so callers never act on a guess.
 * All parameters are DWORD so every call site widens without a signed/unsigned conversion under /Wall.
 */
BOOL VersionAtLeast(DWORD major, DWORD minor, DWORD build, DWORD sp)
{
    OSVERSIONINFOEXW osvi;
    BOOL             fVersionUnavailable;

    SecureZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = (DWORD)sizeof(osvi);
    fVersionUnavailable      = RtlGetVersionEx(&osvi) < 0;
    if (fVersionUnavailable)
    {
        return FALSE;
    }

    if (osvi.dwMajorVersion != major)
    {
        return osvi.dwMajorVersion > major;
    }
    if (osvi.dwMinorVersion != minor)
    {
        return osvi.dwMinorVersion > minor;
    }
    if (osvi.dwBuildNumber != build)
    {
        return osvi.dwBuildNumber > build;
    }
    return (DWORD)osvi.wServicePackMajor >= sp;
}

BOOL WINAPI IsWindowsBuildOrGreaterEx(WORD wMajorVersion, WORD wMinorVersion, DWORD dwBuildNumber, WORD wServicePackMajor)
{
    return VersionAtLeast(wMajorVersion, wMinorVersion, dwBuildNumber, wServicePackMajor);
}

BOOL WINAPI IsWindowsVersionOrGreaterEx(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
{
    return VersionAtLeast(wMajorVersion, wMinorVersion, 0U, wServicePackMajor);
}

/* major/minor are taken from the SDK's _WIN32_WINNT_* constants exactly as versionhelpers.h does
   (HIBYTE = major, LOBYTE = minor); the service-pack and build axes are supplied as literals. */
BOOL WINAPI IsWindowsXPOrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_WINXP), (DWORD)LOBYTE(_WIN32_WINNT_WINXP), 0U, 0U);
}

BOOL WINAPI IsWindowsXPSP1OrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_WINXP), (DWORD)LOBYTE(_WIN32_WINNT_WINXP), 0U, 1U);
}

BOOL WINAPI IsWindowsXPSP2OrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_WINXP), (DWORD)LOBYTE(_WIN32_WINNT_WINXP), 0U, 2U);
}

BOOL WINAPI IsWindowsXPSP3OrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_WINXP), (DWORD)LOBYTE(_WIN32_WINNT_WINXP), 0U, 3U);
}

BOOL WINAPI IsWindowsVistaOrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_VISTA), (DWORD)LOBYTE(_WIN32_WINNT_VISTA), 0U, 0U);
}

BOOL WINAPI IsWindowsVistaSP1OrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_VISTA), (DWORD)LOBYTE(_WIN32_WINNT_VISTA), 0U, 1U);
}

BOOL WINAPI IsWindowsVistaSP2OrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_VISTA), (DWORD)LOBYTE(_WIN32_WINNT_VISTA), 0U, 2U);
}

BOOL WINAPI IsWindows7OrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_WIN7), (DWORD)LOBYTE(_WIN32_WINNT_WIN7), 0U, 0U);
}

BOOL WINAPI IsWindows7SP1OrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_WIN7), (DWORD)LOBYTE(_WIN32_WINNT_WIN7), 0U, 1U);
}

BOOL WINAPI IsWindows8OrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_WIN8), (DWORD)LOBYTE(_WIN32_WINNT_WIN8), 0U, 0U);
}

BOOL WINAPI IsWindows8Point1OrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_WINBLUE), (DWORD)LOBYTE(_WIN32_WINNT_WINBLUE), 0U, 0U);
}

/* IsWindowsThresholdOrGreater is the SDK's alias for the 10.0 check; preserved here verbatim. */
BOOL WINAPI IsWindowsThresholdOrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_WIN10), (DWORD)LOBYTE(_WIN32_WINNT_WIN10), 0U, 0U);
}

BOOL WINAPI IsWindows10OrGreaterEx(void)
{
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_WIN10), (DWORD)LOBYTE(_WIN32_WINNT_WIN10), 0U, 0U);
}

extern FORCEINLINE BOOL WINAPI IsWindows11OrGreaterEx(void)
{
    /* Windows 11 RTM is 10.0.22000; major/minor alone cannot separate it from Windows 10 (both 10.0), so
       the build number is the discriminator -- available only because RtlGetVersion reports the kernel's
       real build, which the SDK's VerifyVersionInfo-based IsWindows10OrGreater cannot. */
    return VersionAtLeast((DWORD)HIBYTE(_WIN32_WINNT_WIN10), (DWORD)LOBYTE(_WIN32_WINNT_WIN10), WINDOWS_11_INITIAL_BUILD, 0U);
}

BOOL WINAPI IsWindowsServerEx(void)
{
    OSVERSIONINFOEXW osvi;
    BOOL             fVersionUnavailable;

    SecureZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = (DWORD)sizeof(osvi);
    fVersionUnavailable      = RtlGetVersionEx(&osvi) < 0;
    if (fVersionUnavailable)
    {
        return FALSE;
    }
    return osvi.wProductType != VER_NT_WORKSTATION;
}
