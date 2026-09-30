/*
 * ShellScalingApiXThunks.inl -- shcore.dll thunk bodies (ShellScalingApi.h origin). Included exactly
 * once, by ShellScalingApiX.c. shcore is loaded on demand (LoadLibrary) via the shared g_hShcore cache,
 * since it is not statically imported.
 *
 * DELAYLOAD argument order: (_hInst, _DllName, _CallConv, _WrapperName, _ExportName, _Args1, _Args2,
 * _RetType, _ErrVal) -- one argument per line.
 */

DELAYLOAD(g_hShcore,
          TEXT("shcore.dll"),
          WINAPI,
          GetDpiForMonitorEx,
          GetDpiForMonitor,
          (HMONITOR hmonitor, MONITOR_DPI_TYPE dpiType, UINT* dpiX, UINT* dpiY),
          (hmonitor, dpiType, dpiX, dpiY),
          HRESULT,
          E_NOTIMPL)
