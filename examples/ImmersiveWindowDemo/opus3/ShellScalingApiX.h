#ifndef SHELLSCALINGAPIX_H
#define SHELLSCALINGAPIX_H

#include <windows.h>
#include <shellscalingapi.h>

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * GetDpiForMonitorEx -- delay-bound wrapper for shcore.dll!GetDpiForMonitor (ShellScalingApi.h origin,
 * Windows 8.1+). shcore is not normally resident, so it is LoadLibrary-bound on first use. Returns
 * E_NOTIMPL when shcore or the export is unavailable; the SDK prototype stays authoritative. The
 * wrapper name is distinct from the SDK export so the real prototype (included above) is not redefined.
 */
/* No SAL: body is macro-generated (DELAYLOAD), so annotating the declaration would trip C28251. */
HRESULT WINAPI GetDpiForMonitorEx(HMONITOR         hmonitor,
                                  MONITOR_DPI_TYPE dpiType,
                                  UINT*            dpiX,
                                  UINT*            dpiY);

#ifdef __cplusplus
}
#endif

#endif /* SHELLSCALINGAPIX_H */
