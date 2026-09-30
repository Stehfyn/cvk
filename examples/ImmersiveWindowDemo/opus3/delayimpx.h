#ifndef DELAYIMPX_H
#define DELAYIMPX_H

#include <windows.h>

/*
 * delayimpx.h -- internal delay-load thunk generators (spiritual successor to the toolset's
 * delayimp.h). Each macro emits a forwarding function that resolves its target export once, caches
 * it, and calls through it; a miss returns a caller-supplied value rather than faulting.
 * Self-contained: only LoadLibrary / GetModuleHandle / GetProcAddress, no CRT.
 */

/*
 * The FARPROC-to-typed-pointer transfer below uses a union, not a cast. GetProcAddress returns FARPROC
 * (a function pointer with unspecified parameters); a cast to the concrete signature is a
 * function-pointer reinterpret that warns on every toolchain at full strength -- MSVC C4191 (under
 * /Wall), Clang -Wcast-function-type[-strict], GCC -Wcast-function-type. These are .c translation units,
 * so C permits reading a union member other than the one written (C11 6.5.2.3p3, "type punning"); with
 * no cast operator present, none of those warnings can fire and no #pragma suppression is needed. All
 * function pointers share one representation/size on Win32/x64/ARM64, so the pun is sound here.
 */

/*
 * DELAYLOAD -- define a delay-bound wrapper _WrapperName (signature _Args1, calling convention
 * _CallConv, return _RetType) that resolves _DllName!_ExportName on first use and forwards to it.
 * _hInst is a caller-owned HMODULE lvalue (the cached library handle, shared across wrappers for the
 * same DLL); the DLL is loaded with LoadLibrary on demand. _Args2 is the forwarded argument list;
 * _ErrVal (already of type _RetType) is returned if the library or the export is missing. Use this
 * (rather than DECLARE_DLL_THUNK) for a DLL that may not already be resident, e.g. shcore.
 */
#define DELAYLOAD(_hInst, _DllName, _CallConv, _WrapperName, _ExportName, _Args1, _Args2, _RetType, _ErrVal) \
    typedef _RetType(_CallConv* PFN_##_WrapperName) _Args1;                                                  \
    _RetType _CallConv _WrapperName _Args1                                                                   \
    {                                                                                                        \
        static PFN_##_WrapperName s_pfn;                                                                     \
        union                                                                                                \
        {                                                                                                    \
            FARPROC            fp;                                                                           \
            PFN_##_WrapperName pfn;                                                                          \
        } u;                                                                                           \
                                                                                                             \
        if (!s_pfn)                                                                                   \
        {                                                                                                    \
            if (!(_hInst))                                                                            \
            {                                                                                                \
                (_hInst) = LoadLibrary(_DllName);                                                            \
            }                                                                                                \
            if (!(_hInst))                                                                            \
            {                                                                                                \
                return (_ErrVal);                                                                            \
            }                                                                                                \
            u.fp = GetProcAddress((_hInst), #_ExportName);                                                   \
            if (!u.fp)                                                                                \
            {                                                                                                \
                return (_ErrVal);                                                                            \
            }                                                                                                \
            s_pfn = u.pfn;                                                                                   \
        }                                                                                                    \
        return s_pfn _Args2;                                                                                 \
    }

/*
 * DECLARE_DLL_THUNK -- define a delay-bound wrapper _WrapperName (signature _Args1, return _RetType,
 * WINAPI) forwarding to _DllName!_ExportName, resolved via GetModuleHandle (i.e. for a DLL already
 * resident -- a statically-imported or otherwise-loaded module). _Args2 is the forwarded argument list;
 * _Fallback is the expression returned when the module or export is absent -- typically a call to the
 * legacy SDK function, so callers never branch on OS version. _ExportName is kept distinct from
 * _WrapperName so the wrapper (e.g. GetDpiForMonitorEx) does not collide with the real SDK prototype it
 * looks up (#_ExportName, ANSI for GetProcAddress). The wrapper has external linkage to match its
 * declaration in the paired X-header; instantiate each thunk in exactly one translation unit.
 */
#define DECLARE_DLL_THUNK(_DllName, _RetType, _WrapperName, _ExportName, _Args1, _Args2, _Fallback) \
    typedef _RetType(WINAPI* PFN_##_WrapperName) _Args1;                                            \
    static FORCEINLINE _RetType WINAPI _WrapperName _Args1                                          \
    {                                                                                               \
        static PFN_##_WrapperName s_pfn;                                                            \
        HMODULE                   hMod;                                                             \
        union                                                                                       \
        {                                                                                           \
            FARPROC            fp;                                                                  \
            PFN_##_WrapperName pfn;                                                                 \
        } u;                                                                                  \
                                                                                                    \
        if (!s_pfn)                                                                          \
        {                                                                                           \
            hMod = GetModuleHandle(_DllName);                                                       \
            if (!hMod)                                                                       \
            {                                                                                       \
                return (_Fallback);                                                                 \
            }                                                                                       \
            u.fp = GetProcAddress(hMod, #_ExportName);                                              \
            if (!u.fp)                                                                       \
            {                                                                                       \
                return (_Fallback);                                                                 \
            }                                                                                       \
            s_pfn = u.pfn;                                                                          \
        }                                                                                           \
        return s_pfn _Args2;                                                                        \
    }

#endif /* DELAYIMPX_H */
