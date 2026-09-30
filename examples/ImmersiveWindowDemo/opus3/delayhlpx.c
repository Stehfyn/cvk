/*
 * delayhlpx.c -- minimal, CRT-free delay-load helper (__delayLoadHelper2). The linker's /DELAYLOAD
 * stubs call this once per delay-imported function: it loads the owning DLL on demand, resolves the
 * import (by name or ordinal) from the V2 (RVA-based) ImgDelayDescr, patches the IAT slot, and returns
 * the resolved address. Replaces delayimp.lib so we keep /NODEFAULTLIB (delayimp.lib's delayhlp.obj
 * drags _load_config_used, a CRT object). Failure (DLL absent) returns NULL -- acceptable because every
 * delay-loaded DLL here (user32/ole32/advapi32/shell32) is always present on a live system; no
 * notification or failure hooks are implemented.
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

#include <Windows.h> // IWYU pragma: keep
#include <delayimp.h>
#include <winnt.h>
#include <basetsd.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <stddef.h>

/* Linker-provided image base; RVAs in the descriptor are relative to it. */
// NOLINTBEGIN(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,clang-diagnostic-reserved-identifier): linker-defined names.
extern IMAGE_DOS_HEADER __ImageBase;
extern FARPROC WINAPI __delayLoadHelper2(PCImgDelayDescr pidd, FARPROC* ppfnIATEntry);

static FORCEINLINE PVOID RvaToPtr(RVA rva)
{
    return (PBYTE)&__ImageBase + rva;
}

FARPROC WINAPI __delayLoadHelper2(PCImgDelayDescr pidd, FARPROC* ppfnIATEntry)
{
    LPCSTR                pszDll;
    HMODULE*              phmod;
    HMODULE               hmod;
    FARPROC*              pIAT;
    PIMAGE_THUNK_DATA     pINT;
    size_t                index;
    PIMAGE_IMPORT_BY_NAME pName;
    FARPROC               pfn;

    pszDll = (LPCSTR)RvaToPtr(pidd->rvaDLLName);
    phmod  = (HMODULE*)RvaToPtr(pidd->rvaHmod);

    hmod = (*phmod);
    if (!hmod)
    {
        hmod = LoadLibraryA(pszDll);
        if (!hmod)
        {
            return NULL;
        }
        (*phmod) = hmod;
    }

    pIAT  = (FARPROC*)RvaToPtr(pidd->rvaIAT);
    pINT  = (PIMAGE_THUNK_DATA)RvaToPtr(pidd->rvaINT);
    index = (size_t)(ppfnIATEntry - pIAT);

    if (IMAGE_SNAP_BY_ORDINAL(pINT[index].u1.Ordinal))
    {
        pfn = GetProcAddress(hmod, (LPCSTR)(ULONG_PTR)IMAGE_ORDINAL(pINT[index].u1.Ordinal)); // NOLINT(performance-no-int-to-ptr): Win32 encodes ordinal imports as pointers.
    }
    else
    {
        pName = (PIMAGE_IMPORT_BY_NAME)RvaToPtr((RVA)pINT[index].u1.AddressOfData);
        pfn   = GetProcAddress(hmod, (LPCSTR)pName->Name);
    }

    (*ppfnIATEntry) = pfn;
    return pfn;
}
// NOLINTEND(bugprone-reserved-identifier,cert-dcl37-c,cert-dcl51-cpp,clang-diagnostic-reserved-identifier)
