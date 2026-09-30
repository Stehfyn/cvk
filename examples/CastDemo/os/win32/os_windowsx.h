#ifndef OS_WIN32_WINDOWSX_H
#define OS_WIN32_WINDOWSX_H

#include <windowsx.h>
#include <commctrl.h>

#ifndef HANDLE_WM_DPICHANGED
#define HANDLE_WM_DPICHANGED(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (UINT)LOWORD(wParam), (UINT)HIWORD(wParam), (const RECT*)(lParam)), (LRESULT)0)
#define FORWARD_WM_DPICHANGED(hwnd, dpiX, dpiY, suggested, fn) \
    (void)(fn)((hwnd), WM_DPICHANGED, MAKEWPARAM((dpiX), (dpiY)), (LPARAM)(const RECT*)(suggested))
#endif

#ifndef HANDLE_WM_PRINTCLIENT
#define HANDLE_WM_PRINTCLIENT(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (HDC)(wParam), (UINT)(lParam)), (LRESULT)0)
#define FORWARD_WM_PRINTCLIENT(hwnd, dc, flags, fn) \
    (void)(fn)((hwnd), WM_PRINTCLIENT, (WPARAM)(HDC)(dc), (LPARAM)(UINT)(flags))
#endif

#ifndef HANDLE_WM_THEMECHANGED
#define HANDLE_WM_THEMECHANGED(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd)))
#define FORWARD_WM_THEMECHANGED(hwnd, fn) \
    (fn)((hwnd), WM_THEMECHANGED, 0, 0)
#endif

#ifndef HANDLE_CB_SETCURSEL
#define HANDLE_CB_SETCURSEL(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (int)(wParam)))
#define FORWARD_CB_SETCURSEL(hwnd, index, fn) \
    (fn)((hwnd), CB_SETCURSEL, (WPARAM)(int)(index), 0)
#endif

#ifndef HANDLE_CB_SHOWDROPDOWN
#define HANDLE_CB_SHOWDROPDOWN(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (BOOL)(wParam)))
#define FORWARD_CB_SHOWDROPDOWN(hwnd, show, fn) \
    (fn)((hwnd), CB_SHOWDROPDOWN, (WPARAM)(BOOL)(show), 0)
#endif

#endif
