// C port of https://github.com/melak47/BorderlessWindow (CC0-1.0).
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include "window.h"
#include "caption.h"
#include "dpi.h"
#include "nt_assert.h"
#include "versionhelpersx.h"
#include <windowsx.h>
#include <dwmapi.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

#define HANDLE_WM_MOUSELEAVE(hwnd, wParam, lParam, fn) \
    ((fn)(hwnd), 0L)
#define FORWARD_WM_MOUSELEAVE(hwnd, fn) \
    (void)(fn)((hwnd), WM_MOUSELEAVE, 0L, 0L)

#define HANDLE_WM_NCMOUSELEAVE(hwnd, wParam, lParam, fn) \
    ((fn)(hwnd), 0L)
#define FORWARD_WM_NCMOUSELEAVE(hwnd, fn) \
    (void)(fn)((hwnd), WM_NCMOUSELEAVE, 0L, 0L)

#define HANDLE_WM_CAPTURECHANGED(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (HWND)(lParam)), 0L)
#define FORWARD_WM_CAPTURECHANGED(hwnd, hwndNewCapture, fn) \
    (void)(fn)((hwnd), WM_CAPTURECHANGED, 0L, (LPARAM)(HWND)(hwndNewCapture))

/* The SDK crackers discard lParam, including -1 to suppress nonclient painting. */
#undef HANDLE_WM_NCACTIVATE
#define HANDLE_WM_NCACTIVATE(hwnd, wParam, lParam, fn) \
    (LRESULT)(BOOL)(fn)((hwnd), (BOOL)(wParam), (lParam))
#undef FORWARD_WM_NCACTIVATE
#define FORWARD_WM_NCACTIVATE(hwnd, fActive, lParam, fn) \
    (BOOL)(fn)((hwnd), WM_NCACTIVATE, (WPARAM)(BOOL)(fActive), (lParam))

#define HANDLE_WM_DWMCOMPOSITIONCHANGED(hwnd, wParam, lParam, fn) \
    ((fn)(hwnd), 0L)
#define HANDLE_WM_DPICHANGED(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (UINT)LOWORD(wParam), (UINT)HIWORD(wParam), (RECT*)(lParam)), 0L)

#define HANDLE_WM_THEMECHANGED(hwnd, wParam, lParam, fn) \
    ((fn)(hwnd), 0L)
#define FORWARD_WM_THEMECHANGED(hwnd, fn) \
    (void)(fn)((hwnd), WM_THEMECHANGED, 0L, 0L)

/* The SDK's WM_WININICHANGE cracker (WM_SETTINGCHANGE is its alias) discards wParam, the SPI_ action. */
#define HANDLE_WM_SETTINGCHANGE(hwnd, wParam, lParam, fn) \
    ((fn)((hwnd), (UINT)(wParam), (LPCTSTR)(lParam)), 0L)
#define FORWARD_WM_SETTINGCHANGE(hwnd, uiAction, lpszSection, fn) \
    (void)(fn)((hwnd), WM_SETTINGCHANGE, (WPARAM)(UINT)(uiAction), (LPARAM)(LPCTSTR)(lpszSection))

/* The SDK cracker discards the result, which is what SetWindowText returns. */
#undef HANDLE_WM_SETTEXT
#define HANDLE_WM_SETTEXT(hwnd, wParam, lParam, fn) \
    (LRESULT)(BOOL)(fn)((hwnd), (LPCTSTR)(lParam))
#undef FORWARD_WM_SETTEXT
#define FORWARD_WM_SETTEXT(hwnd, lpszText, fn) \
    (BOOL)(fn)((hwnd), WM_SETTEXT, 0L, (LPARAM)(LPCTSTR)(lpszText))

/* WM_SETICON returns the icon it replaced. */
#define HANDLE_WM_SETICON(hwnd, wParam, lParam, fn) \
    (LRESULT)(fn)((hwnd), (UINT)(wParam), (HICON)(lParam))
#define FORWARD_WM_SETICON(hwnd, uType, hIcon, fn) \
    (HICON)(fn)((hwnd), WM_SETICON, (WPARAM)(UINT)(uType), (LPARAM)(HICON)(hIcon))

#define SetWindowStyle(hwnd, style) (SetWindowLongPtr(hwnd, GWL_STYLE, (LONG_PTR)(style)))
#define SetWindowExStyle(hwnd, style) (SetWindowLongPtr(hwnd, GWL_EXSTYLE, (LONG_PTR)(style)))
#define SetWindowUserData(hwnd, data) (SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR)(data)))
#define GetWindowUserData(hwnd) ((void*)GetWindowLongPtr(hwnd, GWLP_USERDATA))

#define ABS(x)         ((x < 0) ? -x : x)
#define RECTWIDTH(rc)  (ABS(rc.right - rc.left))
#define RECTHEIGHT(rc) (ABS(rc.bottom - rc.top))
#define WVR_NONE       (0x00000000)

/* Private nonclient hit-test code for the light/dark caption button,
   chosen above the system HT* range (HTHELP = 21 is the highest). */
#define HTLIGHTDARK    (0x00000100)
#define STYLE_WINDOWED         (WS_OVERLAPPEDWINDOW | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define STYLE_AERO_BORDERLESS  (WS_POPUP            | WS_THICKFRAME | WS_CAPTION | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX)
#define STYLE_BASIC_BORDERLESS (WS_POPUP            | WS_THICKFRAME              | WS_SYSMENU | WS_MAXIMIZEBOX | WS_MINIMIZEBOX)
#define STYLE_FULLSCREEN       (WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX)
#define STYLE_FRAME_MASK       (STYLE_WINDOWED | STYLE_AERO_BORDERLESS)

static FORCEINLINE BOOL
IsCompositionEnabled(
    void)
{
    BOOL enabled;

    enabled = FALSE;

    if (SUCCEEDED(DwmIsCompositionEnabled(&enabled)))
    {
        return enabled;
    }
    else
    {
        return FALSE;
    }
}

static FORCEINLINE BOOL
IsWindowCaptured(
    HWND hwnd)
{
    return (BOOL)(GetCapture() == hwnd);
}

static FORCEINLINE BOOL
SetCaptureEx(
    HWND hwnd)
{
    SetCapture(hwnd);
    return (BOOL)(GetCapture() == hwnd);
}

static FORCEINLINE BOOL
ReleaseCaptureEx(
  HWND hwnd)
{
  if (IsWindowCaptured(hwnd))
  {
      return ReleaseCapture();
  }
  else
  {
      return TRUE;
  }
}

static FORCEINLINE BOOL
SetWindowRect(
    HWND hWnd,
    HWND hWndInsertAfter,
    const RECT* lpRect,
    UINT uFlags)
{
    POINT pt;
    SIZE  size;

    pt.x = lpRect->left;
    pt.y = lpRect->top;
    size.cx = RECTWIDTH((*lpRect));
    size.cy = RECTHEIGHT((*lpRect));

    return SetWindowPos(hWnd, hWndInsertAfter, pt.x, pt.y, size.cx, size.cy, uFlags);
}

static FORCEINLINE BOOL
GetWindowMonitorInfo(
    HWND         hwnd,
    MONITORINFO* lpmi)
{
    SecureZeroMemory(lpmi, sizeof(MONITORINFO));
    lpmi->cbSize = sizeof(MONITORINFO);
    return GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), lpmi);
}

static FORCEINLINE DWORD
SelectWindowStyle(
    BOOL fBorderless)
{
    if (fBorderless)
    {
        if (IsCompositionEnabled())
        {
            return STYLE_AERO_BORDERLESS;
        }
        else
        {
            return STYLE_BASIC_BORDERLESS;
        }
    }
    else
    {
        return STYLE_WINDOWED;
    }
}

static FORCEINLINE void
SetShadow(
    HWND hwnd,
    BOOL fEnabled)
{
    MARGINS margins;
    int     width;
    BOOL    fCompositionEnabled;
    BOOL    fShadowVisible;
    enum DWMNCRENDERINGPOLICY policy;

    fCompositionEnabled = IsCompositionEnabled();
    fShadowVisible      = fEnabled && IsWindows11OrGreaterEx();
    if (fCompositionEnabled)
    {
        policy = fEnabled ? DWMNCRP_ENABLED : DWMNCRP_DISABLED;
        ASSERT(SUCCEEDED(DwmSetWindowAttribute(hwnd, DWMWA_NCRENDERING_POLICY, &policy, sizeof(policy))));

        if (fShadowVisible)
        {
            width = 1;
        }
        else
        {
            width = 0;
        }

        margins.cxLeftWidth    = width;
        margins.cxRightWidth   = width;
        margins.cyTopHeight    = 0;
        margins.cyBottomHeight = width;

        ASSERT(SUCCEEDED(DwmExtendFrameIntoClientArea(hwnd, &margins)));
    }
}

static FORCEINLINE IMMERSIVEWINDOW*
GetImmersiveWindow(
    HWND hwnd)
{
    return (IMMERSIVEWINDOW*)GetWindowUserData(hwnd);
}

static void
UpdateWindowMetrics(
    IMMERSIVEWINDOW* window);

static void
SetBorderless(
    IMMERSIVEWINDOW* window,
    BOOL             fEnabled)
{
    DWORD dwNewStyle;
    DWORD dwOldStyle;
    BOOL  fShadowVisible;

    dwOldStyle = GetWindowStyle(window->hwnd);
    dwNewStyle = (dwOldStyle & ~STYLE_FRAME_MASK) | SelectWindowStyle(fEnabled);

    AcquireSRWLockExclusive(&window->presentLock);
    window->fBorderless = fEnabled;
    ReleaseSRWLockExclusive(&window->presentLock);

    if (window->fFullscreen)
    {
        return;
    }

    if (dwNewStyle != dwOldStyle)
    {
        AcquireSRWLockExclusive(&window->presentLock);
        window->fBorderless   = fEnabled;
        window->hotButton     = CB_NONE;
        window->pressedButton = CB_NONE;
        ReleaseSRWLockExclusive(&window->presentLock);

        ASSERT(ReleaseCaptureEx(window->hwnd));

        ASSERT(SetWindowStyle(window->hwnd, dwNewStyle));
        fShadowVisible = !fEnabled || window->fShadow;
        SetShadow(window->hwnd, fShadowVisible);
        /* Change only the frame; retain maximized/minimized placement and shell arrangement. */
        ASSERT(SetWindowPos(window->hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
        UpdateWindowMetrics(window); /* the frame changed: DWM's visible border with it */
    }
}

static FORCEINLINE void
SetBorderlessShadow(
    IMMERSIVEWINDOW* window,
    BOOL             fEnabled)
{
    window->fShadow = fEnabled;
    if (window->fBorderless && !window->fFullscreen)
    {
        SetShadow(window->hwnd, fEnabled);
        ASSERT(SetWindowPos(window->hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
        UpdateWindowMetrics(window);
    }
}

static void
SetFullscreen(
    IMMERSIVEWINDOW* window,
    BOOL             fEnabled)
{
    MONITORINFO monitor;
    DWORD       style;

    if (fEnabled == window->fFullscreen)
    {
        return;
    }

    if (fEnabled)
    {
        window->windowedPlacement.length = sizeof(window->windowedPlacement);
        ASSERT(GetWindowPlacement(window->hwnd, &window->windowedPlacement));
        ASSERT(GetWindowRect(window->hwnd, &window->windowedRect));
        ASSERT(GetWindowMonitorInfo(window->hwnd, &monitor));
    }

    ASSERT(ReleaseCaptureEx(window->hwnd));
    AcquireSRWLockExclusive(&window->presentLock);
    window->fFullscreen  = fEnabled;
    window->hotButton     = CB_NONE;
    window->pressedButton = CB_NONE;
    ReleaseSRWLockExclusive(&window->presentLock);

    style = GetWindowStyle(window->hwnd) & ~STYLE_FRAME_MASK;
    if (fEnabled)
    {
        /* Keep the window's show state; fullscreen changes only its frame and bounds. */
        ASSERT(SetWindowStyle(window->hwnd, style | STYLE_FULLSCREEN));
        SetShadow(window->hwnd, FALSE);
        ASSERT(SetWindowRect(window->hwnd, HWND_TOP, &monitor.rcMonitor, SWP_FRAMECHANGED | SWP_NOOWNERZORDER));
    }
    else
    {
        ASSERT(SetWindowStyle(window->hwnd, style | SelectWindowStyle(window->fBorderless)));
        SetShadow(window->hwnd, !window->fBorderless || window->fShadow);
        ASSERT(SetWindowPlacement(window->hwnd, &window->windowedPlacement));
        if (window->windowedPlacement.showCmd == SW_SHOWNORMAL)
        {
            /* A snapped window's current rectangle can differ from rcNormalPosition. */
            ASSERT(SetWindowRect(window->hwnd, NULL, &window->windowedRect, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE));
        }
        else
        {
            ASSERT(SetWindowPos(window->hwnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE));
        }
    }
    UpdateWindowMetrics(window);
}

/* ---- metrics and caption geometry ------------------------------------------------------------- */

/* The atlas button's width, in DIPs, less its two edge columns: DWM stretches this much and adds an
   edge column per side scaled by the whole-number factor the atlas is rasterized at (ceil of the DPI
   scale). Measured against DWM's button rects (OBJID_TITLEBAR) at every scale from 100% to 250%:
   46, 59, 70, 81, 92, 105, 116 px. */
#define BORDERLESS_CAPTION_BUTTON_INNER_DIP 44

/* Sample the window's metrics for its current DPI. Window thread only; published under presentLock
   for the renderer. The caption band is DWM's: the caption plus the top resize border, which DWM's
   glyphs are centered in. The visible top border is DWM's reported thickness, else (Windows 10, which
   lacks the attribute) SM_CYBORDER: hit-testing gives those rows to resizing, and on Windows 10 the
   caption paints them itself. The button cluster's outer buttons run floor(scale) columns past the
   cluster on their outer sides (also measured at every scale). The title's origin is where DWM's title
   text starts, measured at every scale against the same window's native frame: 30, 36, 44, 50, 55,
   61, 69 px at 100% to 250%, which is SM_CYSIZE + 2 * SM_CXFRAME + the visible border - 1. */
static void
UpdateWindowMetrics(
    IMMERSIVEWINDOW* window)
{
    WINDOWMETRICS metrics;
    UINT          thickness;

    metrics.dpi             = GetDpiForWindow(window->hwnd);
    metrics.border.cx       = GetSystemMetricsForDpi(SM_CXFRAME, metrics.dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, metrics.dpi);
    metrics.border.cy       = GetSystemMetricsForDpi(SM_CYFRAME, metrics.dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, metrics.dpi);
    metrics.thinBorder.cx   = GetSystemMetricsForDpi(SM_CXBORDER, metrics.dpi);
    metrics.thinBorder.cy   = GetSystemMetricsForDpi(SM_CYBORDER, metrics.dpi);
    metrics.smallIcon.cx    = GetSystemMetricsForDpi(SM_CXSMICON, metrics.dpi);
    metrics.smallIcon.cy    = GetSystemMetricsForDpi(SM_CYSMICON, metrics.dpi);
    metrics.cyCaption       = GetSystemMetricsForDpi(SM_CYCAPTION, metrics.dpi) + metrics.border.cy;
    metrics.cxCaptionButton = DipsToPixels(BORDERLESS_CAPTION_BUTTON_INNER_DIP, metrics.dpi) + (2 * DpiScaleCeil(metrics.dpi));
    metrics.cxCaptionOuter  = DpiScaleFloor(metrics.dpi);
    if (FAILED(DwmGetWindowAttribute(window->hwnd, DWMWA_VISIBLE_FRAME_BORDER_THICKNESS, &thickness, sizeof(thickness))))
    {
        thickness = (UINT)metrics.thinBorder.cy;
    }
    metrics.cyTopBorder = (LONG)thickness;
    metrics.cySize      = GetSystemMetricsForDpi(SM_CYSIZE, metrics.dpi);
    metrics.xTitle      = metrics.cySize + (2 * GetSystemMetricsForDpi(SM_CXFRAME, metrics.dpi)) + metrics.cyTopBorder - 1;

    AcquireSRWLockExclusive(&window->presentLock);
    window->metrics = metrics;
    ReleaseSRWLockExclusive(&window->presentLock);
}

/* The system icon's slot: the small icon, inset by the resize border and centered in the caption band. */
static FORCEINLINE RECT
BorderlessCaptionIconRect(
    const WINDOWMETRICS* metrics)
{
    RECT rect;

    rect.left   = metrics->border.cx;
    rect.top    = (metrics->cyCaption - metrics->smallIcon.cy) / 2;
    rect.right  = rect.left + metrics->smallIcon.cx;
    rect.bottom = rect.top  + metrics->smallIcon.cy;

    return rect;
}

/* The system menu's hit area, as DWM's (OBJID_SYSMENU, measured at every scale): SM_CYSIZE square,
   against the client's left edge, its bottom one row above the band's. */
static FORCEINLINE RECT
BorderlessCaptionSysMenuRect(
    const WINDOWMETRICS* metrics)
{
    RECT rect;

    rect.left   = 0;
    rect.right  = metrics->cySize;
    rect.bottom = metrics->cyCaption - 1;
    rect.top    = rect.bottom - metrics->cySize;

    return rect;
}

/* A caption button's rect, as DWM lays its cluster out: right to left from one column past the
   client's right edge (into DWM's border, which the client excludes), each button its width, with the
   close and minimize buttons (the atlas cluster's outer pair) running the outer columns past it on
   their outer sides. The light/dark toggle continues the run leftward. */
static FORCEINLINE RECT
BorderlessCaptionButtonRect(
    const WINDOWMETRICS* metrics,
    LONG                 clientWidth,
    CAPTIONBUTTON        button)
{
    RECT rect;
    LONG cxButton;
    LONG cxOuter;

    cxButton = metrics->cxCaptionButton;
    cxOuter  = metrics->cxCaptionOuter;

    rect.top    = 0;
    rect.bottom = metrics->cyCaption;
    rect.right  = clientWidth + 1;
    rect.left   = rect.right - cxButton - cxOuter;
    if (button < CB_CLOSEBUTTON)
    {
        rect.right = rect.left;
        rect.left  = rect.right - cxButton;
    }
    if (button < CB_MAXBUTTON)
    {
        rect.right = rect.left;
        rect.left  = rect.right - cxButton - cxOuter;
    }
    if (button < CB_MINBUTTON)
    {
        rect.right = rect.left;
        rect.left  = rect.right - cxButton;
    }

    return rect;
}

static FORCEINLINE void
GetBorderlessCaptionLayout(
    const IMMERSIVEWINDOW* window,
    LONG                   clientWidth,
    CAPTIONLAYOUT*         layout)
{
    const WINDOWMETRICS* metrics;
    WINDOWMETRICS        adjustedMetrics;
    RECT*                rect;
    CAPTIONBUTTON        button;
    UINT                 count;

    adjustedMetrics = window->metrics;
    if (IsZoomed(window->hwnd))
    {
        /* The maximized resize border lies outside the visible caption. */
        adjustedMetrics.cyCaption -= adjustedMetrics.border.cy;
        adjustedMetrics.xTitle -= adjustedMetrics.border.cx - adjustedMetrics.cyTopBorder;
        adjustedMetrics.border.cx = adjustedMetrics.cyTopBorder;
        clientWidth -= adjustedMetrics.cyTopBorder;
        adjustedMetrics.cyTopBorder = 0;
    }
    metrics             = &adjustedMetrics;
    layout->dpi         = metrics->dpi;
    layout->cyCaption   = metrics->cyCaption;
    layout->cyTopBorder = metrics->cyTopBorder;
    layout->xTitle      = metrics->xTitle;
    rect   = &layout->buttons[CB_LIGHTDARKBUTTON];
    button = CB_LIGHTDARKBUTTON;
    for (count = CB_CLOSEBUTTON - CB_LIGHTDARKBUTTON + 1u; count != 0u; --count, ++button, ++rect)
    {
        *rect = BorderlessCaptionButtonRect(metrics, clientWidth, button);
        if (IsZoomed(window->hwnd))
        {
            /* DWM's maximized button cells end one row above the caption band. */
            --rect->bottom;
        }
    }
    layout->buttons[CB_ICON] = BorderlessCaptionIconRect(metrics);
    layout->sysMenu          = BorderlessCaptionSysMenuRect(metrics);
    if (IsZoomed(window->hwnd))
    {
        /* The close target reaches the screen corner without shifting its glyph. */
        layout->buttons[CB_CLOSEBUTTON].right = clientWidth + window->metrics.cyTopBorder;
    }
}

CAPTIONBUTTONSTATE
GetCaptionButtonState(
    const IMMERSIVEWINDOW* window,
    CAPTIONBUTTON          button)
{
    if (window->hotButton != button)
    {
        return CBST_NORMAL;
    }
    if (window->pressedButton == button)
    {
        return CBST_PRESSED;
    }
    if (window->pressedButton == CB_NONE)
    {
        return CBST_HOT;
    }
    return CBST_NORMAL;
}

/* ---- hit testing ------------------------------------------------------------------------------ */

static FORCEINLINE CAPTIONBUTTON
HitTestBorderlessCaption(
    const CAPTIONLAYOUT* layout,
    POINT                ptCursor)
{
    RECT          rect;
    CAPTIONBUTTON button;

    for (button = (CB_COUNT - 1); button > CB_NONE; --button)
    {
        if (button == CB_ICON)
        {
            rect = layout->sysMenu;
        }
        else
        {
            rect = layout->buttons[button];
        }

        if (PtInRect(&rect, ptCursor))
        {
            break;
        }
    }

    return button;
}

static FORCEINLINE CAPTIONBUTTON
CaptionButtonFromHitTest(
    UINT codeHitTest)
{
    switch (codeHitTest)
    {
        case HTLIGHTDARK: return CB_LIGHTDARKBUTTON;
        case HTMINBUTTON: return CB_MINBUTTON;
        case HTMAXBUTTON: return CB_MAXBUTTON;
        case HTCLOSE:     return CB_CLOSEBUTTON;
        case HTSYSMENU:   return CB_ICON;
        default:          return CB_NONE;
    }
}

static UINT
HitTestBorderlessWindow(
    const IMMERSIVEWINDOW* window,
    POINT                  ptCursor)
{
    HWND          hwnd;
    CAPTIONLAYOUT layout;
    RECT          client;
    RECT          rect;
    SIZE          border;
    POINT         ptClient;
    CAPTIONBUTTON button;
    UINT          result;
    BOOL          left;
    BOOL          right;
    BOOL          top;
    BOOL          bottom;
    BOOL          fResizableButton;
    BOOL          fInTopBorder;

    hwnd     = window->hwnd;
    ptClient = ptCursor;
    ASSERT(ScreenToClient(hwnd, &ptClient));
    ASSERT(GetClientRect(hwnd, &client));
    GetBorderlessCaptionLayout(window, client.right, &layout);
    button = HitTestBorderlessCaption(&layout, ptClient);
    result = (ptClient.y < layout.cyCaption) ? HTCAPTION : HTCLIENT;

    /* DWM gives the visible top border rows to resizing even over the
       caption buttons; buttons only respond from the first row below it. */
    fResizableButton = (button != CB_NONE) && (button != CB_ICON) && !IsZoomed(hwnd);
    fInTopBorder     = ptClient.y < layout.cyTopBorder;
    if (fResizableButton && fInTopBorder)
    {
        button = CB_NONE;
    }

    if (button != CB_NONE)
    {
        switch (button)
        {
            case CB_ICON:            result = HTSYSMENU;   break;
            case CB_LIGHTDARKBUTTON: result = HTLIGHTDARK; break;
            case CB_MINBUTTON:       result = HTMINBUTTON; break;
            case CB_MAXBUTTON:       result = HTMAXBUTTON; break;
            case CB_CLOSEBUTTON:     result = HTCLOSE;     break;

            case CB_NONE:
            case CB_COUNT:
            DEFAULT_UNREACHABLE;
        }
    }
    else if (!IsZoomed(hwnd))
    {
        ASSERT(GetWindowRect(hwnd, &rect));
        border = window->metrics.border;
        left   = ptCursor.x <  (rect.left  + (1.5 * border.cx));
        top    = ptCursor.y <  rect.top    + border.cy;
        right  = ptCursor.x >= rect.right  - (1.5 * border.cx);
        bottom = ptCursor.y >= rect.bottom - border.cy;

        if (top && left)
        {
            result = HTTOPLEFT;
        }
        else if (top && right)
        {
            result = HTTOPRIGHT;
        }
        else if (bottom && left)
        {
            result = HTBOTTOMLEFT;
        }
        else if (bottom && right)
        {
            result = HTBOTTOMRIGHT;
        }
        else if (left)
        {
            result = HTLEFT;
        }
        else if (right)
        {
            result = HTRIGHT;
        }
        else if (top)
        {
            result = HTTOP;
        }
        else if (bottom)
        {
            result = HTBOTTOM;
        }
    }

    return result;
}

/* Resolve the caller's original icon, size it for this DPI, and give both DWM and the custom caption
   one resource-independent HICON. CopyImage alone retains user32's DPI-sensitive resource identity:
   DWM can obtain different pixels from that handle in its own DPI context. CreateIconIndirect fixes
   the pixels, so both captions draw the same image 1:1. Always resize the original, never the previous
   DPI's copy. Window thread only: WM_GETICON may send messages; the renderer holds presentLock while
   decoding the published copy. Retire the previous copy after publishing under that same lock. */
static void
UpdateCaptionIcon(
    IMMERSIVEWINDOW* window)
{
    HICON    hIcon;
    HICON    hIconOwned;
    HICON    hIconOwnedPrev;
    HICON    hIconSized;
    ICONINFO iconInfo;
    UINT     dpi;
    int      cxSmall;
    int      cySmall;

    dpi   = window->metrics.dpi;
    hIcon = window->smallIconSource;
    if (!hIcon)
    {
        hIcon = (HICON)SendMessage(window->hwnd, WM_GETICON, ICON_BIG, (LPARAM)dpi);
    }
    if (!hIcon)
    {
        hIcon = (HICON)GetClassLongPtr(window->hwnd, GCLP_HICON);
    }
    if (!hIcon)
    {
        hIcon = (HICON)GetClassLongPtr(window->hwnd, GCLP_HICONSM);
    }
    if (!hIcon)
    {
        hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    ASSERT(hIcon);

    cxSmall    = (int)window->metrics.smallIcon.cx;
    cySmall    = (int)window->metrics.smallIcon.cy;
    hIconSized = (HICON)CopyImage(hIcon, IMAGE_ICON, cxSmall, cySmall, 0u);
    ASSERT(hIconSized);
    ASSERT(GetIconInfo(hIconSized, &iconInfo));
    hIconOwned = CreateIconIndirect(&iconInfo);
    ASSERT(hIconOwned);
    ASSERT(DeleteObject(iconInfo.hbmMask));
    if (iconInfo.hbmColor)
    {
        ASSERT(DeleteObject(iconInfo.hbmColor));
    }
    ASSERT(DestroyIcon(hIconSized));
    hIcon = hIconOwned;

    /* Give DWM the same DPI-sized pixels as the custom caption. Leaving ICON_SMALL unset lets
       user32/DWM select and resample a different image at different display scales. Calling the
       default procedure directly keeps this internal copy out of OnSetIcon's caller-owned source. */
    (void)FORWARD_WM_SETICON(window->hwnd, ICON_SMALL, hIcon, DefWindowProc);

    AcquireSRWLockExclusive(&window->presentLock);
    hIconOwnedPrev           = window->captionIconOwned;
    window->captionIcon      = hIcon;
    window->captionIconOwned = hIconOwned;
    ++window->captionRevision;
    ReleaseSRWLockExclusive(&window->presentLock);

    if (hIconOwnedPrev)
    {
        ASSERT(DestroyIcon(hIconOwnedPrev));
    }
}

static FORCEINLINE BOOL
OnNCCreate(
    HWND           hwnd,
    LPCREATESTRUCT lpCreateStruct)
{
    IMMERSIVEWINDOW* window;

    window = (IMMERSIVEWINDOW*)lpCreateStruct->lpCreateParams;
    window->hwnd = hwnd;

    /* Keep title access on the window thread. The renderer holds presentLock,
       so sending WM_GETTEXT from there could deadlock with a window message. */
    lstrcpynW(window->captionTitle, lpCreateStruct->lpszName, ARRAYSIZE(window->captionTitle));
    ++window->captionRevision;

    SetWindowUserData(hwnd, window);
    UpdateWindowMetrics(window);

    return FORWARD_WM_NCCREATE(hwnd, lpCreateStruct, DefWindowProc);
}

static FORCEINLINE UINT
OnNCCalcSize(
    HWND               hwnd,
    BOOL               fCalcValidRects,
    NCCALCSIZE_PARAMS* lpcsp)
{
    IMMERSIVEWINDOW* window;
    RECT*            lpRect;
    UINT             result;

    window = GetImmersiveWindow(hwnd);
    lpRect = (RECT*)lpcsp;
    result = WVR_NONE;

    if (window->fFullscreen)
    {
        /* Fullscreen uses the entire monitor rectangle as its client. */
    }
    else if (window->fBorderless)
    {
        if (IsZoomed(hwnd))
        {
            result = FORWARD_WM_NCCALCSIZE(hwnd, fCalcValidRects, lpcsp, DefWindowProc);
            lpRect->top -= window->metrics.cyCaption - window->metrics.border.cy;
        }
        else
        {
            lpRect->left   += window->metrics.border.cx;
            lpRect->right  -= window->metrics.border.cx;
            lpRect->bottom -= window->metrics.border.cy;
        }
    }
    else
    {
        result = FORWARD_WM_NCCALCSIZE(hwnd, fCalcValidRects, lpcsp, DefWindowProc);
    }

    AcquireSRWLockExclusive(&window->presentLock);
    window->rcClient = (*lpRect);
    if (window->fPresentReady)
    {
        window->fGeometryPending = TRUE;
        while (window->fGeometryPending)
        {
            ASSERT(SleepConditionVariableSRW(&window->presentCondition, &window->presentLock, INFINITE, 0));
        }
    }
    ReleaseSRWLockExclusive(&window->presentLock);
    return result;
}

static FORCEINLINE UINT
OnNCHittest(
    HWND hwnd,
    int  x,
    int  y)
{
    IMMERSIVEWINDOW* window;
    UINT             result;
    POINT            ptCursor;

    window = GetImmersiveWindow(hwnd);

    if (window->fFullscreen)
    {
        result = HTCLIENT;
    }
    else if (window->fBorderless)
    {
        ptCursor.x = x;
        ptCursor.y = y;

        result = HitTestBorderlessWindow(window, ptCursor);
    }
    else
    {
        result = FORWARD_WM_NCHITTEST(hwnd, x, y, DefWindowProc);
    }

    return result;
}

static FORCEINLINE void
OnNCMouseMove(
    HWND hwnd,
    int  x,
    int  y,
    UINT codeHitTest)
{
    IMMERSIVEWINDOW* window;
    CAPTIONBUTTON    button;

    window = GetImmersiveWindow(hwnd);

    if (window->fBorderless)
    {
        button = CaptionButtonFromHitTest(codeHitTest);

        AcquireSRWLockExclusive(&window->presentLock);
        window->hotButton = button;
        ReleaseSRWLockExclusive(&window->presentLock);

        if (button != CB_NONE)
        {
            TRACKMOUSEEVENT tme;

            tme.cbSize      = sizeof(tme);
            tme.dwFlags     = TME_LEAVE | TME_NONCLIENT;
            tme.hwndTrack   = hwnd;
            tme.dwHoverTime = 0;

            ASSERT(TrackMouseEvent(&tme));
        }
    }

    FORWARD_WM_NCMOUSEMOVE(hwnd, x, y, codeHitTest, DefWindowProc);
}

static FORCEINLINE void
OnNCMouseLeave(
    HWND hwnd)
{
    IMMERSIVEWINDOW* window;

    window = GetImmersiveWindow(hwnd);

    if (window->fBorderless)
    {
        AcquireSRWLockExclusive(&window->presentLock);
        if (window->pressedButton == CB_NONE)
        {
            window->hotButton = CB_NONE;
        }
        ReleaseSRWLockExclusive(&window->presentLock);
    }

    FORWARD_WM_NCMOUSELEAVE(hwnd, DefWindowProc);
}

static void
OnNCLButtonDown(
    HWND hwnd,
    BOOL fDoubleClick,
    int  x,
    int  y,
    UINT codeHitTest)
{
    IMMERSIVEWINDOW* window;
    CAPTIONBUTTON    button;
    BOOL             fCaptionButtonPressed;

    window = GetImmersiveWindow(hwnd);

    button                = CaptionButtonFromHitTest(codeHitTest);
    fCaptionButtonPressed = window->fBorderless && (button >= CB_LIGHTDARKBUTTON) && (button <= CB_CLOSEBUTTON);

    if (fCaptionButtonPressed)
    {
        AcquireSRWLockExclusive(&window->presentLock);
        window->hotButton     = button;
        window->pressedButton = button;
        ReleaseSRWLockExclusive(&window->presentLock);

        ASSERT(SetCaptureEx(hwnd));
    }
    else
    {
        FORWARD_WM_NCLBUTTONDOWN(hwnd, fDoubleClick, x, y, codeHitTest, DefWindowProc);
    }
}

/* The tracking re-test is a plain rect test against the pressed button,
   matching win32kfull!xxxTrackCaptionButton, which latches the button rect
   at button-down and PtInRects every move against it. The rect inputs
   cannot change while the button is captured, so recomputing is identical
   to caching. */
static BOOL
IsPtInCaptionButton(
    const IMMERSIVEWINDOW* window,
    CAPTIONBUTTON          button,
    POINT                  ptClient)
{
    CAPTIONLAYOUT layout;
    RECT          client;

    ASSERT(GetClientRect(window->hwnd, &client));
    GetBorderlessCaptionLayout(window, client.right, &layout);

    if (button == CB_ICON)
    {
        return PtInRect(&layout.sysMenu, ptClient);
    }
    return PtInRect(&layout.buttons[button], ptClient);
}

static FORCEINLINE void
OnMouseMove(
    HWND hwnd,
    int  x,
    int  y,
    UINT keyFlags)
{
    IMMERSIVEWINDOW* window;
    POINT            ptCursor;
    BOOL             fTrackingPressedButton;

    window                 = GetImmersiveWindow(hwnd);
    fTrackingPressedButton = (window->pressedButton != CB_NONE) && IsWindowCaptured(hwnd);

    if (fTrackingPressedButton)
    {
        ptCursor.x = x;
        ptCursor.y = y;

        AcquireSRWLockExclusive(&window->presentLock);
        if (IsPtInCaptionButton(window, window->pressedButton, ptCursor))
        {
            window->hotButton = window->pressedButton;
        }
        else
        {
            window->hotButton = CB_NONE;
        }
        ReleaseSRWLockExclusive(&window->presentLock);
    }

    FORWARD_WM_MOUSEMOVE(hwnd, x, y, keyFlags, DefWindowProc);
}

static void
OnLButtonUp(
    HWND hwnd,
    int  x,
    int  y,
    UINT keyFlags)
{
    IMMERSIVEWINDOW* window;
    CAPTIONBUTTON    button;
    POINT            ptCursor;
    BOOL             fReleasingPressedButton;

    window                  = GetImmersiveWindow(hwnd);
    button                  = window->pressedButton;
    fReleasingPressedButton = window->fBorderless && (button != CB_NONE) && IsWindowCaptured(hwnd);

    if (fReleasingPressedButton)
    {
        ptCursor.x = x;
        ptCursor.y = y;

        ASSERT(ReleaseCaptureEx(hwnd));

        if (IsPtInCaptionButton(window, button, ptCursor))
        {
            ASSERT(ClientToScreen(hwnd, &ptCursor));

            switch (button)
            {
                case CB_LIGHTDARKBUTTON:
                    AcquireSRWLockExclusive(&window->presentLock);
                    window->fLightMode = !window->fLightMode;
                    ReleaseSRWLockExclusive(&window->presentLock);
                    break;

                case CB_MINBUTTON:
                    FORWARD_WM_SYSCOMMAND(hwnd, SC_MINIMIZE, ptCursor.x, ptCursor.y, SendMessage);
                    break;

                case CB_MAXBUTTON:
                    if (IsZoomed(hwnd))
                    {
                        FORWARD_WM_SYSCOMMAND(hwnd, SC_RESTORE, ptCursor.x, ptCursor.y, SendMessage);
                    }
                    else
                    {
                        FORWARD_WM_SYSCOMMAND(hwnd, SC_MAXIMIZE, ptCursor.x, ptCursor.y, SendMessage);
                    }
                    break;

                case CB_CLOSEBUTTON:
                    FORWARD_WM_SYSCOMMAND(hwnd, SC_CLOSE, ptCursor.x, ptCursor.y, SendMessage);
                    break;

                case CB_ICON:
                case CB_NONE:
                case CB_COUNT:
                default:
                    break;
            }
        }
    }
    else
    {
        FORWARD_WM_LBUTTONUP(hwnd, x, y, keyFlags, DefWindowProc);
    }
}

static FORCEINLINE UINT
ShowSystemMenu(
    HWND hwnd,
    int  x,
    int  y)
{
    HMENU hMenu;
    UINT  selected;
    UINT  flags;

    hMenu = GetSystemMenu(hwnd, FALSE);
    flags = TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD | TPM_RIGHTBUTTON;

    selected = TrackPopupMenuEx(hMenu, flags, x, y, hwnd, NULL);

    if (selected != 0)
    {
        FORWARD_WM_SYSCOMMAND(hwnd, selected, 0, 0, DefWindowProc);
    }

    return selected;
}

static FORCEINLINE void
OnNCRButtonUp(
    HWND hwnd,
    int  x,
    int  y,
    UINT codeHitTest)
{
    IMMERSIVEWINDOW* window;

    window = GetImmersiveWindow(hwnd);

    if (window->fBorderless && ((codeHitTest == HTCAPTION) || (codeHitTest == HTSYSMENU)))
    {
        ShowSystemMenu(hwnd, x, y);
    }
    else
    {
        FORWARD_WM_NCRBUTTONUP(hwnd, x, y, codeHitTest, DefWindowProc);
    }
}

static FORCEINLINE void
OnCancelMode(
    HWND hwnd)
{
    IMMERSIVEWINDOW* window;

    window = GetImmersiveWindow(hwnd);

    if (window->fBorderless)
    {
        ASSERT(ReleaseCaptureEx(hwnd));

        AcquireSRWLockExclusive(&window->presentLock);
        window->pressedButton = CB_NONE;
        ReleaseSRWLockExclusive(&window->presentLock);
    }

    FORWARD_WM_CANCELMODE(hwnd, DefWindowProc);
}

static FORCEINLINE void
OnCaptureChanged(
    HWND hwnd,
    HWND hwndNewCapture)
{
    IMMERSIVEWINDOW* window;

    window = GetImmersiveWindow(hwnd);

    if (window->fBorderless)
    {
        AcquireSRWLockExclusive(&window->presentLock);
        if (window->pressedButton != CB_NONE)
        {
            window->hotButton = CB_NONE;
        }
        window->pressedButton = CB_NONE;
        ReleaseSRWLockExclusive(&window->presentLock);
    }

    FORWARD_WM_CAPTURECHANGED(hwnd, hwndNewCapture, DefWindowProc);
}

static FORCEINLINE BOOL
OnNCActivate(
    HWND   hwnd,
    BOOL   fActive,
    LPARAM lParam)
{
    IMMERSIVEWINDOW* window;

    window = GetImmersiveWindow(hwnd);

    AcquireSRWLockExclusive(&window->presentLock);
    window->fActive = fActive;
    ReleaseSRWLockExclusive(&window->presentLock);

    if (window->fBorderless)
    {
        if (IsCompositionEnabled())
        {
            return FORWARD_WM_NCACTIVATE(hwnd, fActive, lParam, DefWindowProc);
        }
        else
        {
            return TRUE;
        }
    }
    else
    {
       return FORWARD_WM_NCACTIVATE(hwnd, fActive, lParam, DefWindowProc);
    }
}

static FORCEINLINE void
OnDWMCompositionChanged(
    HWND hwnd)
{
    IMMERSIVEWINDOW* window;

    window = GetImmersiveWindow(hwnd);
    SetBorderless(window, window->fBorderless);
    UpdateWindowMetrics(window);
}

/* A DPI change: resample the metrics and re-resolve the icon for the new DPI (its revision bump also has
   the renderer rebuild what it caches per DPI), then take the suggested rect. The caption's geometry,
   title layout and atlas variant follow the metrics every frame. */
static FORCEINLINE void
OnDpiChanged(
    HWND  hwnd,
    UINT  dpiX,
    UINT  dpiY,
    RECT* lpRect)
{
    IMMERSIVEWINDOW* window;
    MONITORINFO      monitor;

    UNREFERENCED_PARAMETER(dpiX);
    UNREFERENCED_PARAMETER(dpiY);

    window = GetImmersiveWindow(hwnd);
    UpdateWindowMetrics(window);
    UpdateCaptionIcon(window);
    if (window->fFullscreen)
    {
        monitor.cbSize = sizeof(monitor);
        ASSERT(GetMonitorInfo(MonitorFromRect(lpRect, MONITOR_DEFAULTTONEAREST), &monitor));
        lpRect = &monitor.rcMonitor;
    }
    ASSERT(SetWindowRect(hwnd, NULL, lpRect, SWP_NOZORDER | SWP_NOACTIVATE));
}

/* Plain and Alt key-downs: F9 borderless, F10 shadow, F11 fullscreen. Holding a key toggles once. */
static FORCEINLINE BOOL
OnShortcutKey(
    HWND hwnd,
    UINT vk,
    UINT flags)
{
    IMMERSIVEWINDOW* window;

    window = GetImmersiveWindow(hwnd);

    switch (vk)
    {
        case VK_F9:
            if (!(flags & KF_REPEAT))
            {
                SetBorderless(window, !window->fBorderless);
            }
            return TRUE;

        case VK_F10:
            if (!(flags & KF_REPEAT))
            {
                SetBorderlessShadow(window, !window->fShadow);
            }
            return TRUE;

        case VK_F11:
            if (!(flags & KF_REPEAT))
            {
                SetFullscreen(window, !window->fFullscreen);
            }
            return TRUE;
    }

    return FALSE;
}

static FORCEINLINE void
OnKeyDown(
    HWND hwnd,
    UINT vk,
    BOOL fDown,
    int  cRepeat,
    UINT flags)
{
    UNREFERENCED_PARAMETER(fDown);

    if (!OnShortcutKey(hwnd, vk, flags))
    {
        FORWARD_WM_KEYDOWN(hwnd, vk, cRepeat, flags, DefWindowProc);
    }
}

static FORCEINLINE void
OnSysKeyDown(
    HWND hwnd,
    UINT vk,
    BOOL fDown,
    int  cRepeat,
    UINT flags)
{
    UNREFERENCED_PARAMETER(fDown);

    if (!OnShortcutKey(hwnd, vk, flags))
    {
        FORWARD_WM_SYSKEYDOWN(hwnd, vk, cRepeat, flags, DefWindowProc);
    }
}

/* Where the keyboard-invoked system menu drops: a thin border in from the client's left edge, a thin
   border above the caption band's bottom. */
static FORCEINLINE POINT
GetCaptionSystemMenuPosition(
    const IMMERSIVEWINDOW* window)
{
    POINT pt;

    pt.x = window->metrics.thinBorder.cx;
    pt.y = window->metrics.cyCaption - window->metrics.thinBorder.cy;

    ASSERT(ClientToScreen(window->hwnd, &pt));

    return pt;
}

static FORCEINLINE UINT
ShowCaptionSystemMenu(
    const IMMERSIVEWINDOW* window)
{
    POINT pt;

    pt = GetCaptionSystemMenuPosition(window);

    return ShowSystemMenu(window->hwnd, pt.x, pt.y);
}

static FORCEINLINE void
OnSCKeyMenu(
    const IMMERSIVEWINDOW* window,
    UINT                   cmd,
    int                    x,
    int                    y)
{
    UNREFERENCED_PARAMETER(cmd);
    UNREFERENCED_PARAMETER(x);
    UNREFERENCED_PARAMETER(y);

    ShowCaptionSystemMenu(window);
}

static FORCEINLINE void
OnSysCommand(
    HWND hwnd,
    UINT cmd,
    int  x,
    int  y)
{
    IMMERSIVEWINDOW* window;

    window = GetImmersiveWindow(hwnd);

    if (window->fBorderless)
    {
        switch (GET_SC_WPARAM(cmd))
        {
            case SC_KEYMENU:
                OnSCKeyMenu(window, cmd, x, y);
                break;

            default:
                FORWARD_WM_SYSCOMMAND(hwnd, cmd, x, y, DefWindowProc);
                break;
        }
    }
    else
    {
        FORWARD_WM_SYSCOMMAND(hwnd, cmd, x, y, DefWindowProc);
    }
}

static FORCEINLINE BOOL
OnEraseBkgnd(
    HWND hwnd,
    HDC  hdc)
{
    UNREFERENCED_PARAMETER(hwnd);
    UNREFERENCED_PARAMETER(hdc);

    return TRUE;
}

static FORCEINLINE void
OnPaint(
    HWND hwnd)
{
    PAINTSTRUCT paint;

    BeginPaint(hwnd, &paint);
    EndPaint(hwnd, &paint);
}

static FORCEINLINE void
OnClose(
    HWND hwnd)
{
    ASSERT(DestroyWindow(hwnd));
}

static FORCEINLINE void
OnDestroy(
    HWND hwnd)
{
    UNREFERENCED_PARAMETER(hwnd);

    PostQuitMessage(EXIT_SUCCESS);
}

static FORCEINLINE void
OnNCDestroy(
    HWND hwnd)
{
    IMMERSIVEWINDOW* window;

    window = GetImmersiveWindow(hwnd);
    window->hwnd = NULL;
    SetWindowUserData(hwnd, NULL);

    FORWARD_WM_NCDESTROY(hwnd, DefWindowProc);
}

/* The title, kept on the window thread for the renderer (see OnNCCreate), once DefWindowProc has taken
   the new text. */
static FORCEINLINE BOOL
OnSetText(
    HWND    hwnd,
    LPCTSTR lpszText)
{
    IMMERSIVEWINDOW* window;
    LPCWSTR          title;
    BOOL             result;

    window = GetImmersiveWindow(hwnd);
    result = FORWARD_WM_SETTEXT(hwnd, lpszText, DefWindowProc);

    if (result && window)
    {
        if (lpszText)
        {
            title = lpszText;
        }
        else
        {
            title = L"";
        }
        AcquireSRWLockExclusive(&window->presentLock);
        lstrcpynW(window->captionTitle, title, ARRAYSIZE(window->captionTitle));
        ++window->captionRevision;
        ReleaseSRWLockExclusive(&window->presentLock);
    }

    return result;
}

/* A new window icon: re-resolve the caption icon from it. Returns the icon it replaced. */
static FORCEINLINE HICON
OnSetIcon(
    HWND  hwnd,
    UINT  uType,
    HICON hIcon)
{
    IMMERSIVEWINDOW* window;
    HICON            hIconPrev;

    window    = GetImmersiveWindow(hwnd);
    if (window && (uType == ICON_SMALL))
    {
        hIconPrev = window->smallIconSource;
        window->smallIconSource = hIcon;
    }
    else
    {
        hIconPrev = FORWARD_WM_SETICON(hwnd, uType, hIcon, DefWindowProc);
    }

    if (window)
    {
        UpdateCaptionIcon(window);
    }

    return hIconPrev;
}

/* A system setting changed (non-client metrics, colors, the light/dark mode set): resample the metrics
   and have the renderer rebuild the caption's title layout and atlas. */
static FORCEINLINE void
OnSettingChange(
    HWND    hwnd,
    UINT    uiAction,
    LPCTSTR lpszSection)
{
    IMMERSIVEWINDOW* window;

    window = GetImmersiveWindow(hwnd);

    if (window)
    {
        UpdateWindowMetrics(window);
        AcquireSRWLockExclusive(&window->presentLock);
        ++window->captionRevision;
        ReleaseSRWLockExclusive(&window->presentLock);
    }

    FORWARD_WM_SETTINGCHANGE(hwnd, uiAction, lpszSection, DefWindowProc);
}

/* The visual style changed: the DWMWindow atlas is decoded from the current .msstyles, so resample the
   metrics and have the renderer rebuild the caption. */
static FORCEINLINE void
OnThemeChanged(
    HWND hwnd)
{
    IMMERSIVEWINDOW* window;

    window = GetImmersiveWindow(hwnd);

    if (window)
    {
        UpdateWindowMetrics(window);
        AcquireSRWLockExclusive(&window->presentLock);
        ++window->captionRevision;
        ReleaseSRWLockExclusive(&window->presentLock);
    }

    FORWARD_WM_THEMECHANGED(hwnd, DefWindowProc);
}

static LRESULT CALLBACK
WndProc(
    HWND   hWnd,
    UINT   Msg,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (Msg)
    {
        HANDLE_MSG(hWnd, WM_SETTEXT,               OnSetText);
        HANDLE_MSG(hWnd, WM_SETICON,               OnSetIcon);
        HANDLE_MSG(hWnd, WM_SETTINGCHANGE,         OnSettingChange);
        HANDLE_MSG(hWnd, WM_THEMECHANGED,          OnThemeChanged);
        HANDLE_MSG(hWnd, WM_NCCREATE,              OnNCCreate);
        HANDLE_MSG(hWnd, WM_NCCALCSIZE,            OnNCCalcSize);
        HANDLE_MSG(hWnd, WM_NCHITTEST,             OnNCHittest);
        HANDLE_MSG(hWnd, WM_NCMOUSEMOVE,           OnNCMouseMove);
        HANDLE_MSG(hWnd, WM_NCMOUSELEAVE,          OnNCMouseLeave);
        HANDLE_MSG(hWnd, WM_NCLBUTTONDOWN,         OnNCLButtonDown);
        HANDLE_MSG(hWnd, WM_NCLBUTTONDBLCLK,       OnNCLButtonDown);
        HANDLE_MSG(hWnd, WM_NCRBUTTONUP,           OnNCRButtonUp);
        HANDLE_MSG(hWnd, WM_MOUSEMOVE,             OnMouseMove);
        HANDLE_MSG(hWnd, WM_LBUTTONUP,             OnLButtonUp);
        HANDLE_MSG(hWnd, WM_CANCELMODE,            OnCancelMode);
        HANDLE_MSG(hWnd, WM_CAPTURECHANGED,        OnCaptureChanged);
        HANDLE_MSG(hWnd, WM_NCACTIVATE,            OnNCActivate);
        HANDLE_MSG(hWnd, WM_DWMCOMPOSITIONCHANGED, OnDWMCompositionChanged);
        HANDLE_MSG(hWnd, WM_DPICHANGED,            OnDpiChanged);
        HANDLE_MSG(hWnd, WM_KEYDOWN,               OnKeyDown);
        HANDLE_MSG(hWnd, WM_SYSKEYDOWN,            OnSysKeyDown);
        HANDLE_MSG(hWnd, WM_SYSCOMMAND,            OnSysCommand);
        HANDLE_MSG(hWnd, WM_ERASEBKGND,            OnEraseBkgnd);
        HANDLE_MSG(hWnd, WM_PAINT,                 OnPaint);
        HANDLE_MSG(hWnd, WM_CLOSE,                 OnClose);
        HANDLE_MSG(hWnd, WM_DESTROY,               OnDestroy);
        HANDLE_MSG(hWnd, WM_NCDESTROY,             OnNCDestroy);
    }

    return DefWindowProc(hWnd, Msg, wParam, lParam);
}

/* Client shades. Dark mode's clear color alternates with the frame parity: both are black; tint one to
   see the cadence. */
static const D2D1_COLOR_F WINDOW_CF_LIGHT_CLIENT     = {0.95f, 0.95f, 0.95f, 1.0f};
static const D2D1_COLOR_F WINDOW_RGCF_DARK_CLIENT[2] = {{0.0f, 0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, 0.0f, 1.0f}};

ID2D1DeviceContext*
BeginImmersivePaint(
    IMMERSIVEWINDOW*      window,
    IMMERSIVEPAINTSTRUCT* ps)
{
    const D2D1_COLOR_F* clientColor;

    AcquireSRWLockExclusive(&window->presentLock);
    ps->context  = window->swapChain->context;
    ps->rcClient = window->rcClient;
    if (window->fBorderless && !window->fFullscreen)
    {
        /* Before the draw opens: the caption's resources follow the window, and rebuilding some of them
           retargets the context. */
        GetBorderlessCaptionLayout(window, ps->rcClient.right - ps->rcClient.left, &ps->layout);
        PrepareWindowCaption(window->caption, ps->context, &ps->layout);
    }
    else
    {
        SecureZeroMemory(&ps->layout, sizeof(ps->layout));
    }
    ID2D1DeviceContext_BeginDraw(ps->context);

    /* Erase the client area in the theme's shade. */
    if (window->fLightMode)
    {
        clientColor = &WINDOW_CF_LIGHT_CLIENT;
    }
    else
    {
        clientColor = &WINDOW_RGCF_DARK_CLIENT[window->paintCount & 1u];
    }
    ID2D1DeviceContext_Clear(ps->context, clientColor);
    return ps->context;
}

void
EndImmersivePaint(
    IMMERSIVEWINDOW*            window,
    const IMMERSIVEPAINTSTRUCT* ps)
{
    DCOMPOSITION_BORDER_MODE borderMode;
    BOOL                     fPresented;

    if (window->fBorderless && !window->fFullscreen)
    {
        DrawWindowCaption(window->caption, ps->context);
    }
    ASSERT(SUCCEEDED(ID2D1DeviceContext_EndDraw(ps->context, NULL, NULL)));
    /* A pending geometry change restarts the present so the frame laid out for the new rcClient is the
       one that lands. */
    fPresented = PresentSwapChain(window->swapChain, window->fGeometryPending, TRUE);

    /* First frame: the visual's border mode is left at its default, and DWM must take the commit before
       the window reports the frame as presented, so the first frame the user sees is complete. After
       that the border mode alternates with the frame parity. */
    if (window->paintCount)
    {
        borderMode = (DCOMPOSITION_BORDER_MODE)(window->paintCount & 1u);
    }
    else
    {
        borderMode = DCOMPOSITION_BORDER_MODE_INHERIT;
    }
    cdcompVisualSetBorderMode(window->swapChain->visual, borderMode);
    cdcompDeviceCommit(window->swapChain->compositionDevice);
    ++window->swapChain->commitCount;
    if (!window->paintCount)
    {
        cdcompDeviceWaitForCommitCompletion(window->swapChain->compositionDevice);
    }
    ++window->paintCount;

    if (fPresented)
    {
        window->fPresentReady    = TRUE;
        window->fGeometryPending = FALSE;
        WakeConditionVariable(&window->presentCondition);
    }
    ReleaseSRWLockExclusive(&window->presentLock);
}

static FORCEINLINE void
InitImmersiveWindow(
    IMMERSIVEWINDOW* window)
{
    window->hwnd             = NULL;
    SecureZeroMemory(&window->metrics, sizeof(window->metrics));
    window->fBorderless      = TRUE;
    window->fShadow          = TRUE;
    window->fFullscreen      = FALSE;
    SecureZeroMemory(&window->windowedPlacement, sizeof(window->windowedPlacement));
    SecureZeroMemory(&window->windowedRect, sizeof(window->windowedRect));
    window->presentLock      = (SRWLOCK)SRWLOCK_INIT;
    window->presentCondition = (CONDITION_VARIABLE)CONDITION_VARIABLE_INIT;
    window->rcClient.left    = 0;
    window->rcClient.top     = 0;
    window->rcClient.right   = 0;
    window->rcClient.bottom  = 0;
    window->fPresentReady    = FALSE;
    window->fGeometryPending = FALSE;
    window->hotButton        = CB_NONE;
    window->pressedButton    = CB_NONE;
    window->fLightMode       = FALSE;
    window->fActive          = FALSE;
    window->captionTitle[0]  = L'\0';
    window->captionIcon      = NULL;
    window->captionIconOwned = NULL;
    window->smallIconSource  = NULL;
    window->captionRevision  = 0;
    window->swapChain        = NULL;
    window->caption          = NULL;
    window->paintCount       = 0;
}

void
CreateImmersiveWindow(
    IMMERSIVEWINDOW* window)
{
    WNDCLASSEX wcx;
    HINSTANCE  instance;

    InitImmersiveWindow(window);

    instance = GetModuleHandle(NULL);

    wcx.cbSize        = sizeof(wcx);
    wcx.style         = CS_BYTEALIGNCLIENT | CS_BYTEALIGNWINDOW;
    wcx.lpfnWndProc   = WndProc;
    wcx.cbClsExtra    = 0;
    wcx.cbWndExtra    = 0;
    wcx.hInstance     = instance;
    wcx.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
    wcx.hCursor       = LoadCursor(NULL, IDC_ARROW);
    wcx.hbrBackground = NULL;
    wcx.lpszMenuName  = NULL;
    wcx.lpszClassName = WC_IMMERSIVEWINDOW;
    wcx.hIconSm       = NULL;
    ASSERT(RegisterClassEx(&wcx));

    window->hwnd = CreateWindowEx(
        WS_EX_NOREDIRECTIONBITMAP,
        wcx.lpszClassName,
        TEXT("Opus3"),
        STYLE_AERO_BORDERLESS,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        1280,
        720,
        NULL,
        NULL,
        instance,
        window);
    ASSERT(window->hwnd);

    SetBorderless(window, window->fBorderless);
    SetBorderlessShadow(window, window->fShadow);
    ShowWindow(window->hwnd, SW_SHOW);
    UpdateWindowMetrics(window); /* now that DWM has framed the window */
    UpdateCaptionIcon(window);

    ASSERT(SetWindowLongPtr(window->hwnd, GWLP_USERDATA, (LONG_PTR)window) == (LONG_PTR)window);
}
