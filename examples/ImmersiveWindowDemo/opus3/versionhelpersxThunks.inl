/*
 * versionhelpersxThunks.inl -- ntdll.dll thunk body (versionhelpers.h-family origin). Included exactly
 * once, by versionhelpersx.c, so the wrapper definition lives in one translation unit. ntdll is always
 * resident, so GetModuleHandle resolution (DECLARE_DLL_THUNK) suffices. RtlGetVersion has shipped since
 * Windows 2000, so the fallback is effectively unreachable; a negative NTSTATUS (-1) signals "version
 * undeterminable" and every predicate then fails closed to FALSE.
 *
 * RtlGetVersion -- not kernel32!VerifyVersionInfoW -- is the target precisely so the result reports the
 * kernel's real version, immune to the application-manifest compatibility shim that makes the SDK
 * VersionHelpers under-report on Windows 8.1+. (A VerifyVersionInfoW thunk would also be pointless: that
 * export is statically importable from the always-resident, already-linked kernel32.)
 *
 * DECLARE_DLL_THUNK argument order: (_DllName, _RetType, _WrapperName, _ExportName, _Args1, _Args2,
 * _Fallback) -- one argument per line.
 */

DECLARE_DLL_THUNK(TEXT("ntdll.dll"),
                  LONG,
                  RtlGetVersionEx,
                  RtlGetVersion,
                  (OSVERSIONINFOEXW* lpVersionInformation),
                  (lpVersionInformation),
                  -1)
