#ifndef OS_WIN32_WINDOWSX_H
#define OS_WIN32_WINDOWSX_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Additional typed Win32 message crackers for the CastDemo window and subclasses. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <windowsx.h>
#include <CommCtrl.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/** Callback: void fn(HWND, UINT dpiX, UINT dpiY, const RECT* suggested). */
#ifndef HANDLE_WM_DPICHANGED
#define HANDLE_WM_DPICHANGED(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (UINT)LOWORD(wParam), (UINT)HIWORD(wParam), (const RECT*)(lParam)), (LRESULT)0)
#endif
#ifndef FORWARD_WM_DPICHANGED
#define FORWARD_WM_DPICHANGED(hwnd, dpiX, dpiY, suggested, fn) \
    (void)(fn)((hwnd), WM_DPICHANGED, MAKEWPARAM((dpiX), (dpiY)), (LPARAM)(const RECT*)(suggested))
#endif

/** Callback: void fn(HWND, HDC, UINT flags). */
#ifndef HANDLE_WM_PRINTCLIENT
#define HANDLE_WM_PRINTCLIENT(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (HDC)(wParam), (UINT)(lParam)), (LRESULT)0)
#endif
#ifndef FORWARD_WM_PRINTCLIENT
#define FORWARD_WM_PRINTCLIENT(hwnd, dc, flags, fn) \
    (void)(fn)((hwnd), WM_PRINTCLIENT, (WPARAM)(HDC)(dc), (LPARAM)(UINT)(flags))
#endif

/** Callback: LRESULT fn(HWND). */
#ifndef HANDLE_WM_THEMECHANGED
#define HANDLE_WM_THEMECHANGED(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd)))
#endif
#ifndef FORWARD_WM_THEMECHANGED
#define FORWARD_WM_THEMECHANGED(hwnd, fn) \
    (fn)((hwnd), WM_THEMECHANGED, 0, 0)
#endif

/** Callback: LRESULT fn(HWND, int index). */
#ifndef HANDLE_CB_SETCURSEL
#define HANDLE_CB_SETCURSEL(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (int)(wParam)))
#endif
#ifndef FORWARD_CB_SETCURSEL
#define FORWARD_CB_SETCURSEL(hwnd, index, fn) \
    (fn)((hwnd), CB_SETCURSEL, (WPARAM)(int)(index), 0)
#endif

/** Callback: LRESULT fn(HWND, BOOL show). */
#ifndef HANDLE_CB_SHOWDROPDOWN
#define HANDLE_CB_SHOWDROPDOWN(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (BOOL)(wParam)))
#endif
#ifndef FORWARD_CB_SHOWDROPDOWN
#define FORWARD_CB_SHOWDROPDOWN(hwnd, show, fn) \
    (fn)((hwnd), CB_SHOWDROPDOWN, (WPARAM)(BOOL)(show), 0)
#endif

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

#endif /* OS_WIN32_WINDOWSX_H */
