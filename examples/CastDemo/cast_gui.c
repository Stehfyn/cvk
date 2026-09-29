/* cast_gui.c - CastDemo: a small Win32 window that lists the Cast receivers on the network and
** plays a video on the selected one, with a Play/Pause, Stop and seek toolbar.
**
** The window thread owns the controls. Discovery and the Cast session each run on a worker
** thread and publish into startup-owned context or event-mailbox storage.
**
**   CastDemo [file.mp4]                 preselects the file */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>
#include <windowsx.h>
#include <uxtheme.h>
#include <dwmapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cast_channel.h"
#include "cast_session.h"
#include "os/os_assert.h"
#include "os/os_memory.h"
#include "media_server.h"
#include "cast_network.h"
#include "os/os_network.h"
#include "mp4_index.h"
#include "proxy.h"
#include "ssdp.h"
#include "subtitles.h"
#include "audio_gain.h"
#include "cast_events.h"
#include "os/os_thread.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"

#if defined(_MSC_VER)
#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#define PREFERRED_DEVICE    "XGIMI"
#define DEFAULT_SERVER_PORT (8642)
#define DISCOVERY_MS        (3000)
#define SEEK_RANGE          (1000)

enum ControlId
{
    ID_LIST = 100,
    ID_LIST_LABEL,
    ID_REFRESH,
    ID_WINDOWS_CAST,
    ID_FILE_LABEL,
    ID_FILE,
    ID_BROWSE,
    ID_SUBTITLE_LABEL,
    ID_SUBTITLE,
    ID_SUBTITLE_BROWSE,
    ID_GAIN_LABEL,
    ID_GAIN,
    ID_GAIN_STATE,
    ID_CAST,
    ID_HINT,
    ID_PLAY_PAUSE,
    ID_STOP,
    ID_SEEK,
    ID_TIME,
    ID_STATUS,
    ID_CFG_LABEL,
    ID_RES_SLIDER,
    ID_RES_VALUE,
    ID_QP_SLIDER,
    ID_QP_VALUE,
    ID_GOP_SLIDER,
    ID_GOP_VALUE,
    ID_MOTION_CHECK,
    ID_MOTION_SLIDER,
    ID_MOTION_VALUE,
    ID_PREVIEW,
    ID_CFG_READOUT
};

typedef struct DiscoveryResult
{
    uint32_t   count;
    uint32_t   error;
    CastDevice devices[CAST_MAX_DEVICES];
} DiscoveryResult;

typedef struct SessionEvents
{
    CastEvents_t mailbox;
    unsigned     generation;
} SessionEvents;

/* A quality-preview transcode job and its result. One preview runs at a time, so both live in the
** App with fixed capacity rather than being allocated per click. */
typedef struct PreviewResult
{
    int      ok;
    unsigned width;
    unsigned height;
    unsigned frames;
    unsigned kbps;
    int      motionPercent;
    WCHAR    path[MAX_PATH];
} PreviewResult;

typedef struct PreviewJob
{
    GpuDevice          gpuDevice; ///< Startup-owned device and query workspace.
    HWND               hWnd;
    PreviewResult*     pResult;
    volatile LONG*     pCancel;
    GpuTranscodeConfig config;
    unsigned           startFrame;
    unsigned           frames;
    WCHAR              source[MAX_PATH];
    WCHAR              dest[MAX_PATH];
} PreviewJob;

typedef struct App
{
    HINSTANCE hInstance;
    HWND      hWnd;
    HWND      hListLabel;
    HWND      hList;
    HWND      hRefresh;
    HWND      hWindowsCast;
    HWND      hFileLabel;
    HWND      hFile;
    HWND      hBrowse;
    HWND                  hSubtitleLabel, hSubtitle, hSubtitleBrowse;
    HWND                  hGainLabel, hGain, hGainState;
    HBRUSH                backgroundBrush, fieldBrush;
    HBRUSH                selectedBrush, accentBrush, mutedBrush, borderBrush;
    SubtitleTrack         subtitles[SUBTITLE_MAX_TRACKS];
    Subtitles_Workspace_t subtitleWorkspace;
    byte_t                subtitleScratch[SUBTITLE_TEXT_CAPACITY];
    byte_t                subtitleOutput[SUBTITLE_TEXT_CAPACITY];
    unsigned              subtitleCount;
    WCHAR                 subtitleSource[1024];
    WCHAR                 originalSource[MAX_PATH];
    char                  originalUrl[1200], originalType[64];
    OS_Thread_t gainWorker;
    bool_t      gainBusy;
    bool_t      gainRestartPending;
    bool_t      gainDiscard;
    unsigned    gainActiveSerial;
    volatile LONG         gainCancel, gainProgress;
    unsigned              gainPercent, gainSerial;
    int gainOk;
    int                   preparingAudio, audioNeedsConversion;
    CastSessionConfig   pendingConfig;
    char                gainError[256];
    WCHAR               gainOutput[MAX_PATH];
    struct GainVariant* gainVariants;
    HWND                hCast;
    HWND                hHint;
    HWND                hPlayPause;
    HWND                hStop;
    HWND                hSeek;
    HWND                hTime;
    HWND                hStatus;
    HWND                hCfgLabel;
    HWND                hResSlider;
    HWND                hResValue;
    HWND                hQpSlider;
    HWND                hQpValue;
    HWND                hGopSlider;
    HWND                hGopValue;
    HWND                hMotionCheck;
    HWND                hMotionSlider;
    HWND                hMotionValue;
    HWND                hPreview;
    HWND                hCfgReadout;
    HFONT               hFont;
    UINT                dpi;
    /* receivers */
    DiscoveryResult devices;
    DiscoveryResult discovered;
    Ssdp_t          discovery;
    OS_Network_t    routeNetwork; ///< UI-owned startup query resource, separate from discovery worker state.
    OS_Thread_t     discoveryWorker;
    SessionEvents   events;
    int             closing;
    int             finishPending;
    WCHAR           finishReason[CAST_EVENTS_TEXT_CAPACITY];
    int             discovering;
    /* the active session */
    CastSession_t  session;  ///< Storage established at startup and reused by every cast.
    CastSession_t* pSession; ///< Active alias; NULL between casts.
    OS_Thread_t    sessionWorker;
    bool_t         sessionBusy;
    MediaServer*   pServer;
    MediaServer_Workspace_t serving;
    Proxy_t        proxy; ///< Startup-owned proxy state, scratch file and persistent worker.
    WCHAR          proxySource[MAX_PATH];
    char           proxyLocal[64];
    unsigned short        proxyPort;
    GpuTranscodeConfig transcode; /* live transcoder settings from the config sliders */
    double             keyframes[MP4_INDEX_MAX_KEYFRAMES];
    CastStatus         status;
    ULONGLONG          statusTick; /* when status.currentTime was reported, for interpolation */
    int                seeking;    /* the user is dragging the slider */
    int                stopping;
    OS_Thread_t        previewWorker; ///< Startup-owned preview execution resource.
    volatile LONG      previewCancel;
    int                previewBusy;
    PreviewJob         previewJob; /* fixed storage for the running preview, no per-click alloc */
    PreviewResult      previewResult;
    bool_t windowDestroyed; ///< UI-owned completion of the native window lifetime.
} App;

#define POSITION_TIMER   (1)
#define POSITION_TICK_MS (50)
#define SUBTITLE_TIMER   (2)
#define GAIN_TIMER       (3)

#define DARK_BACKGROUND RGB(24, 26, 31)
#define DARK_FIELD      RGB(36, 39, 46)
#define DARK_TEXT       RGB(232, 235, 240)
#define DARK_MUTED      RGB(151, 160, 174)
#define DARK_ACCENT     RGB(82, 156, 245)

typedef struct GainVariant
{
    struct GainVariant* next;
    MediaServer*        server;
    WCHAR               path[MAX_PATH];
} GainVariant;



/*************************************************************************************************/
/**
 * \brief Borrows the startup context already attached to a live window.
 *
 * \par Requirements:
 *  - 001 WM_NCCREATE shall have attached the context before any caller uses this query.
 *  - 002 The entry point shall retain the context until native destruction and worker closure.
 *
 * \param[in] window   Native application window
 *
 * \retval    non-NULL   Borrowed startup context; a missing binding asserts.
 */
static FORCE_INLINE App* GetApp(HWND window);

/*************************************************************************************************/
/**
 * \par Details:
 *  The native window owns only a borrowed pointer, never the context allocation.
 */
static FORCE_INLINE App* GetApp(HWND window)
{
    App* app;

    app = (App*)GetWindowLongPtr(window, GWLP_USERDATA);
    LOGASSERT(NULL != app);
    return app;
}

/*************************************************************************************************/
/**
 * \brief Acquires completed job contexts and dispatches their results on the UI thread.
 *
 * \par Requirements:
 *  - 001 Running job storage shall remain untouched until completion or FAILED is acquired.
 *  - 002 Pending gain restarts shall consume only the latest slider value.
 *
 * \param[in,out] app   Initialized application and its persistent workers
 *
 * \retval        None
 */
static FORCE_INLINE void PollJobs(App* app);

static FORCE_INLINE LRESULT CALLBACK
SeekBarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData);
static FORCE_INLINE void RefreshSubtitles(App* pApp);
static FORCE_INLINE void ScheduleGain(App* app);
static FORCE_INLINE void OnPreviewDone(HWND hWnd, const PreviewResult* pResult);
static FORCE_INLINE void FinishSession(App* pApp, const WCHAR* pReason);

/*************************************************************************************************/
/**
 * \par Details:
 *  Converts 96-DPI logical units using the window's current DPI.
 */
static FORCE_INLINE int Scale(const App* pApp, int dips)
{
    return MulDiv(dips, (int)pApp->dpi, 96);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Writes an empty Windows string when the source is NULL or conversion fails.
 */
static FORCE_INLINE void Utf8ToWide(const char* pText, WCHAR* pWide, int capacity)
{
    if ((NULL == pText) || (0 == MultiByteToWideChar(CP_UTF8, 0, pText, -1, pWide, capacity)))
    {
        pWide[0] = L'\0';
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Converts a terminated Windows string into UTF-8; failure clears the output.
 */
static FORCE_INLINE void ToUtf8(const wchar_t* pText, char* pOut, size_t capacity)
{
    if (0 == WideCharToMultiByte(CP_UTF8, 0, pText, -1, pOut, (int)capacity, NULL, NULL))
    {
        pOut[0] = 0;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void SetStatus(App* pApp, const WCHAR* pText)
{
    SetWindowTextW(pApp->hStatus, pText);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Searches successive byte positions using a case-insensitive prefix comparison.
 */
static FORCE_INLINE int ContainsNoCase(const char* pHaystack, const char* pNeedle)
{
    const size_t needleLength = strlen(pNeedle);
    size_t index;

    for (index = 0; pHaystack[index]; ++index)
    {
        if (0 == _strnicmp(pHaystack + index, pNeedle, needleLength))
        {
            return 1;
        }
    }
    return 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Rounds to the nearest second and includes an hours field only for durations
 *  of at least one hour.
 */
static FORCE_INLINE void FormatTime(double seconds, WCHAR* pOut, size_t capacity)
{
    const int total = (int)(seconds + 0.5);

    if (total >= 3600)
    {
        (void)swprintf(pOut, capacity, L"%d:%02d:%02d", total / 3600, (total / 60) % 60, total % 60);
    }
    else
    {
        (void)swprintf(pOut, capacity, L"%d:%02d", total / 60, total % 60);
    }
}

/* ------------------------------------------------------------------ controls and layout */

/*************************************************************************************************/
/**
 * \par Details:
 *  Changes ordinary buttons to owner-draw controls and requests the dark Explorer
 *  theme after creating the child window.
 */
static FORCE_INLINE HWND
CreateChild(App* pApp, const WCHAR* pClass, const WCHAR* pText, DWORD style, DWORD exStyle, int id)
{
    HWND child;
    if (!wcscmp(pClass, L"BUTTON") && (style & BS_TYPEMASK) != BS_AUTOCHECKBOX)
        style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    child = CreateWindowExW(exStyle,
                            pClass,
                            pText,
                            WS_CHILD | WS_VISIBLE | style,
                            0,
                            0,
                            10,
                            10,
                            pApp->hWnd,
                            (HMENU)(INT_PTR)id,
                            pApp->hInstance,
                            NULL);
    SetWindowTheme(child, L"DarkMode_Explorer", NULL);
    return child;
}

/* The list-view owns its header, so header NM_CUSTOMDRAW never reaches WindowProc.
** Paint the header itself, including the unused area after the last column. Native
** header processing still handles column resizing and hit testing. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Paints the header cells and trailing unused area directly because the parent
 *  list-view owns header notifications. Native processing retains hit testing
 *  and column resizing.
 */
static FORCE_INLINE void PaintDarkHeader(HWND hWnd, HDC dc, const App* pApp)
{
    RECT client;
    const int saved = SaveDC(dc);
    GetClientRect(hWnd, &client);
    FillRect(dc, &client, pApp->fieldBrush);
    SelectObject(dc, pApp->hFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, DARK_MUTED);
    for (int i = 0; i < Header_GetItemCount(hWnd); ++i)
    {
        WCHAR   text[128] = {0};
        HDITEMW item      = {0};
        RECT    rect, divider;
        item.mask       = HDI_TEXT | HDI_FORMAT;
        item.pszText    = text;
        item.cchTextMax = 128;
        if (!Header_GetItem(hWnd, i, &item) || !Header_GetItemRect(hWnd, i, &rect))
            continue;
        divider      = rect;
        divider.left = divider.right - 1;
        FillRect(dc, &divider, pApp->backgroundBrush);
        InflateRect(&rect, -Scale(pApp, 6), 0);
        DrawTextW(dc,
                  text,
                  -1,
                  &rect,
                  DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX |
                      ((item.fmt & HDF_RIGHT)    ? DT_RIGHT
                       : (item.fmt & HDF_CENTER) ? DT_CENTER
                                                 : DT_LEFT));
    }
    RestoreDC(dc, saved);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Handles paint and print-client requests with the dark header renderer and
 *  removes the subclass during nonclient destruction.
 */
static FORCE_INLINE LRESULT CALLBACK
DarkHeaderProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData)
{
    const App* pApp = (const App*)refData;
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        HDC         dc = BeginPaint(hWnd, &paint);
        PaintDarkHeader(hWnd, dc, pApp);
        EndPaint(hWnd, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT)
    {
        PaintDarkHeader(hWnd, (HDC)wParam, pApp);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hWnd, DarkHeaderProc, idSubclass);
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Chooses selection and disabled colors, draws the counted combo item text and
 *  adds the focus rectangle when requested by the item state.
 */
static FORCE_INLINE void PaintSubtitleItem(HWND combo, HDC dc, RECT rect, int index, UINT state, const App* pApp)
{
    WCHAR     text[512] = {0};
    const int saved     = SaveDC(dc);
    const int selected  = (state & ODS_SELECTED) && !(state & ODS_COMBOBOXEDIT);
    HBRUSH    brush     = selected ? pApp->selectedBrush : pApp->fieldBrush;
    FillRect(dc, &rect, brush);
    SelectObject(dc, pApp->hFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, (state & ODS_DISABLED) ? DARK_MUTED : DARK_TEXT);
    if (index >= 0)
    {
        LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)index, 0);
        if (length >= 0 && length < 512)
            SendMessageW(combo, CB_GETLBTEXT, (WPARAM)index, (LPARAM)text);
    }
    InflateRect(&rect, -Scale(pApp, 6), 0);
    DrawTextW(dc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if ((state & ODS_FOCUS) && !(state & ODS_NOFOCUSRECT))
        DrawFocusRect(dc, &rect);
    RestoreDC(dc, saved);
}

/* Owner-draw covers the list items; draw the closed field, border and arrow too,
** because visual styles otherwise retain a white dropdown button. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Draws the closed selection field, border and arrow in addition to owner-drawn
 *  list items, preventing the dropdown button from retaining its light theme.
 */
static FORCE_INLINE void PaintDarkCombo(HWND hWnd, HDC dc, const App* pApp)
{
    RECT      client, text;
    const int saved      = SaveDC(dc);
    const int enabled    = IsWindowEnabled(hWnd);
    const int focused    = GetFocus() == hWnd;
    const int arrowWidth = Scale(pApp, 24);
    HBRUSH border;
    POINT  arrow[3];
    int       x, y;
    GetClientRect(hWnd, &client);
    FillRect(dc, &client, pApp->fieldBrush);
    text = client;
    InflateRect(&text, -1, -1);
    text.right -= arrowWidth;
    PaintSubtitleItem(hWnd,
                      dc,
                      text,
                      (int)SendMessage(hWnd, CB_GETCURSEL, 0, 0),
                      ODS_COMBOBOXEDIT | (enabled ? 0 : ODS_DISABLED),
                      pApp);
    border = focused && enabled ? pApp->accentBrush : pApp->borderBrush;
    FrameRect(dc, &client, border);
    x          = client.right - arrowWidth / 2;
    y          = (client.top + client.bottom) / 2;
    arrow[0].x = x - Scale(pApp, 4);
    arrow[0].y = y - Scale(pApp, 2);
    arrow[1].x = x + Scale(pApp, 4);
    arrow[1].y = y - Scale(pApp, 2);
    arrow[2].x = x;
    arrow[2].y = y + Scale(pApp, 2);
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, enabled ? DARK_TEXT : DARK_MUTED);
    SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, arrow, 3);
    RestoreDC(dc, saved);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Supplies dark painting and requests repaint after focus, enable, selection,
 *  dropdown and theme changes handled by the default subclass procedure.
 */
static FORCE_INLINE LRESULT CALLBACK
DarkComboProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData)
{
    const App* pApp = (const App*)refData;
    LRESULT result;
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        HDC         dc = BeginPaint(hWnd, &paint);
        PaintDarkCombo(hWnd, dc, pApp);
        EndPaint(hWnd, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT)
    {
        PaintDarkCombo(hWnd, (HDC)wParam, pApp);
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hWnd, DarkComboProc, idSubclass);
    result = DefSubclassProc(hWnd, message, wParam, lParam);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE || message == CB_SETCURSEL ||
        message == CB_SHOWDROPDOWN || message == WM_THEMECHANGED)
        InvalidateRect(hWnd, NULL, FALSE);
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Creates the current DPI's message font, replaces the old font and sends it to
 *  all child controls before updating combo item heights.
 */
static FORCE_INLINE void ApplyFont(App* pApp)
{
    NONCLIENTMETRICSW metrics;
    HWND              hChild;

    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, pApp->dpi))
    {
        HFONT hFont = CreateFontIndirectW(&metrics.lfMessageFont);

        if (NULL != hFont)
        {
            if (NULL != pApp->hFont)
            {
                DeleteObject(pApp->hFont);
            }
            pApp->hFont = hFont;
        }
    }
    for (hChild = GetWindow(pApp->hWnd, GW_CHILD); NULL != hChild; hChild = GetWindow(hChild, GW_HWNDNEXT))
    {
        SendMessage(hChild, WM_SETFONT, (WPARAM)pApp->hFont, TRUE);
    }
    SendMessage(pApp->hSubtitle, CB_SETITEMHEIGHT, 0, Scale(pApp, 22));
    SendMessage(pApp->hSubtitle, CB_SETITEMHEIGHT, (WPARAM)-1, Scale(pApp, 20));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Computes DPI-scaled rows from the client rectangle and reserves the remaining
 *  height for the receiver list, then moves controls and adjusts column widths.
 */
static FORCE_INLINE void Layout(App* pApp)
{
    RECT client;
    const int margin  = Scale(pApp, 12);
    const int gap     = Scale(pApp, 8);
    const int rowH    = Scale(pApp, 24);
    const int buttonW = Scale(pApp, 96);
    const int smallW  = Scale(pApp, 72);
    const int labelH  = Scale(pApp, 18);
    const int timeW   = Scale(pApp, 150);
    const int valueW  = Scale(pApp, 108);
    const int cfgH    = labelH + gap + 5 * (rowH + gap); /* label + res + qp + gop + motion + actions */
    int width;
    int sliderX;
    int y;
    int listH;

    GetClientRect(pApp->hWnd, &client);
    width   = client.right - 2 * margin;
    sliderX = margin + valueW + gap;
    /* bottom-up: config panel, status, toolbar, hint, cast, file; the list takes the rest */
    listH = client.bottom - (margin + labelH + gap) - 5 * (gap + rowH) - (gap + labelH) - (gap + cfgH) - margin;
    if (listH < Scale(pApp, 60))
    {
        listH = Scale(pApp, 60);
    }
    y = margin;
    MoveWindow(pApp->hListLabel, margin, y + Scale(pApp, 3), width - 2 * buttonW - 2 * gap, labelH, TRUE);
    MoveWindow(pApp->hRefresh, client.right - margin - 2 * buttonW - gap, y - Scale(pApp, 2), buttonW, rowH, TRUE);
    MoveWindow(pApp->hWindowsCast, client.right - margin - buttonW, y - Scale(pApp, 2), buttonW, rowH, TRUE);
    y += labelH + gap;
    MoveWindow(pApp->hList, margin, y, width, listH, TRUE);
    y += listH + gap;
    MoveWindow(pApp->hFileLabel, margin, y + Scale(pApp, 4), Scale(pApp, 40), labelH, TRUE);
    MoveWindow(pApp->hFile, margin + Scale(pApp, 44), y, width - Scale(pApp, 44) - gap - buttonW, rowH, TRUE);
    MoveWindow(pApp->hBrowse, client.right - margin - buttonW, y, buttonW, rowH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hSubtitleLabel, margin, y + Scale(pApp, 4), Scale(pApp, 64), labelH, TRUE);
    MoveWindow(pApp->hSubtitle,
               margin + Scale(pApp, 68),
               y,
               width - Scale(pApp, 68) - gap - buttonW,
               Scale(pApp, 220),
               TRUE);
    MoveWindow(pApp->hSubtitleBrowse, client.right - margin - buttonW, y, buttonW, rowH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hGainLabel, margin, y + Scale(pApp, 4), Scale(pApp, 108), labelH, TRUE);
    MoveWindow(pApp->hGain, margin + Scale(pApp, 112), y, width - Scale(pApp, 300), rowH, TRUE);
    MoveWindow(pApp->hGainState,
               client.right - margin - Scale(pApp, 180),
               y + Scale(pApp, 4),
               Scale(pApp, 180),
               labelH,
               TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hCast, margin, y, buttonW, rowH, TRUE);
    MoveWindow(pApp->hHint, margin + buttonW + gap, y + Scale(pApp, 4), width - buttonW - gap, labelH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hPlayPause, margin, y, smallW, rowH, TRUE);
    MoveWindow(pApp->hStop, margin + smallW + gap, y, smallW, rowH, TRUE);
    MoveWindow(pApp->hSeek, margin + 2 * (smallW + gap), y, width - 2 * (smallW + gap) - gap - timeW, rowH, TRUE);
    MoveWindow(pApp->hTime, client.right - margin - timeW, y + Scale(pApp, 4), timeW, labelH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hStatus, margin, y, width, labelH, TRUE);
    y += labelH + gap;
    /* The transcoder configuration panel. */
    MoveWindow(pApp->hCfgLabel, margin, y, width, labelH, TRUE);
    y += labelH + gap;
    MoveWindow(pApp->hResValue, margin, y + Scale(pApp, 4), valueW, labelH, TRUE);
    MoveWindow(pApp->hResSlider, sliderX, y, width - valueW - gap, rowH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hQpValue, margin, y + Scale(pApp, 4), valueW, labelH, TRUE);
    MoveWindow(pApp->hQpSlider, sliderX, y, width - valueW - gap, rowH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hGopValue, margin, y + Scale(pApp, 4), valueW, labelH, TRUE);
    MoveWindow(pApp->hGopSlider, sliderX, y, width - valueW - gap, rowH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hMotionCheck, margin, y + Scale(pApp, 2), valueW, rowH, TRUE);
    MoveWindow(pApp->hMotionSlider, sliderX, y, width - valueW - gap - Scale(pApp, 52), rowH, TRUE);
    MoveWindow(pApp->hMotionValue,
               client.right - margin - Scale(pApp, 48),
               y + Scale(pApp, 4),
               Scale(pApp, 48),
               labelH,
               TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hPreview, margin, y, buttonW, rowH, TRUE);
    MoveWindow(pApp->hCfgReadout, margin + buttonW + gap, y + Scale(pApp, 4), width - buttonW - gap, labelH, TRUE);
    ListView_SetColumnWidth(pApp->hList, 0, width - Scale(pApp, 120) - Scale(pApp, 90) - Scale(pApp, 24));
    ListView_SetColumnWidth(pApp->hList, 1, Scale(pApp, 120));
    ListView_SetColumnWidth(pApp->hList, 2, Scale(pApp, 90));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Derives control availability from selection, session, preparation and preview
 *  state, then updates the Play/Pause caption.
 */
static FORCE_INLINE void UpdateControls(App* pApp)
{
    const int haveSession = (NULL != pApp->pSession);
    const int busy        = haveSession || pApp->preparingAudio;
    const int haveDevice  = (-1 != ListView_GetNextItem(pApp->hList, -1, LVNI_SELECTED));
    const int haveFile    = (GetWindowTextLengthW(pApp->hFile) > 0);
    const int playing     = (CAST_STATE_PLAYING == pApp->status.state || CAST_STATE_BUFFERING == pApp->status.state);

    EnableWindow(pApp->hCast, !busy && haveDevice && haveFile);
    EnableWindow(pApp->hRefresh, !pApp->discovering);
    EnableWindow(pApp->hStop, busy && !pApp->stopping);
    EnableWindow(pApp->hPlayPause, haveSession && pApp->status.hasMedia);
    EnableWindow(pApp->hSeek, haveSession && pApp->status.hasMedia && pApp->status.duration > 0);
    EnableWindow(pApp->hSubtitle, !busy);
    EnableWindow(pApp->hSubtitleBrowse, !busy);
    EnableWindow(pApp->hFile, !busy);
    EnableWindow(pApp->hBrowse, !busy);
    EnableWindow(pApp->hPreview, !pApp->preparingAudio && !pApp->previewBusy);
    EnableWindow(pApp->hGain, !haveSession || pApp->originalSource[0]);
    SetWindowTextW(pApp->hPlayPause, playing ? L"Pause" : L"Play");
}

/* Rewrites the slider value captions from the current transcoder config. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Formats slider captions from the stored transcoder configuration.
 */
static FORCE_INLINE void UpdateConfigLabels(App* pApp)
{
    WCHAR text[96];

    (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"Width  %u px", pApp->transcode.width);
    SetWindowTextW(pApp->hResValue, text);
    (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"Quality  QP %d", pApp->transcode.qpInter);
    SetWindowTextW(pApp->hQpValue, text);
    (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"Keyframe  %uf", pApp->transcode.gopLength);
    SetWindowTextW(pApp->hGopValue, text);
    (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"%d%%", pApp->transcode.motionStrength);
    SetWindowTextW(pApp->hMotionValue, text);
}

/* Reads the sliders and checkbox into the transcoder config, snapping where needed, then refreshes
** the captions. Called whenever a control changes so pApp->transcode always mirrors the panel. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Snaps width to a multiple of sixteen, derives intra QP from inter QP and
 *  updates motion controls before refreshing the captions.
 */
static FORCE_INLINE void ReadConfigFromSliders(App* pApp)
{
    int width;
    int qp;

    width = (int)SendMessage(pApp->hResSlider, TBM_GETPOS, 0, 0);
    width = ((width + 8) / 16) * 16; /* proxy width is a multiple of 16 */
    if (width < 240)
    {
        width = 240;
    }
    pApp->transcode.width          = (unsigned)width;
    qp                             = (int)SendMessage(pApp->hQpSlider, TBM_GETPOS, 0, 0);
    pApp->transcode.qpInter        = qp;
    pApp->transcode.qpIntra        = (qp - 4 > 1) ? qp - 4 : 1; /* keyframes a little sharper */
    pApp->transcode.gopLength      = (unsigned)SendMessage(pApp->hGopSlider, TBM_GETPOS, 0, 0);
    pApp->transcode.motionStrength = (int)SendMessage(pApp->hMotionSlider, TBM_GETPOS, 0, 0);
    pApp->transcode.motionAdaptive = (BST_CHECKED == SendMessage(pApp->hMotionCheck, BM_GETCHECK, 0, 0)) ? 1 : 0;
    EnableWindow(pApp->hMotionSlider, pApp->transcode.motionAdaptive);
    UpdateConfigLabels(pApp);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Creates the receiver list, media controls and transcoder settings, initializes
 *  their ranges/defaults and installs the painting and seek subclasses.
 */
static FORCE_INLINE void CreateControls(App* pApp)
{
    LVCOLUMNW column;

    pApp->hListLabel = CreateChild(pApp, L"STATIC", L"Cast receivers on this network", SS_LEFT, 0, ID_LIST_LABEL);
    pApp->hRefresh   = CreateChild(pApp, L"BUTTON", L"Refresh", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_REFRESH);
    pApp->hWindowsCast =
        CreateChild(pApp, L"BUTTON", L"Windows Cast...", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_WINDOWS_CAST);
    pApp->hList = CreateChild(pApp,
                                       WC_LISTVIEWW,
                                       L"",
                                       LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_TABSTOP,
                                       WS_EX_CLIENTEDGE,
                                       ID_LIST);
    pApp->hFileLabel     = CreateChild(pApp, L"STATIC", L"Video", SS_LEFT, 0, ID_FILE_LABEL);
    pApp->hFile          = CreateChild(pApp, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_FILE);
    pApp->hBrowse        = CreateChild(pApp, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_BROWSE);
    pApp->hSubtitleLabel = CreateChild(pApp, L"STATIC", L"Subtitles", SS_LEFT, 0, ID_SUBTITLE_LABEL);
    pApp->hSubtitle      = CreateChild(pApp,
                                       L"COMBOBOX",
                                       L"",
                                       CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
                                       0,
                                       ID_SUBTITLE);
    SetWindowSubclass(pApp->hSubtitle, DarkComboProc, 1, (DWORD_PTR)pApp);
    pApp->hSubtitleBrowse =
        CreateChild(pApp, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_SUBTITLE_BROWSE);
    SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)L"Automatic");
    SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)L"Off");
    SendMessage(pApp->hSubtitle, CB_SETCURSEL, 0, 0);
    pApp->hGainLabel = CreateChild(pApp, L"STATIC", L"Audio gain 100%", SS_LEFT, 0, ID_GAIN_LABEL);
    pApp->hGain      = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_GAIN);
    SendMessage(pApp->hGain, TBM_SETRANGE, TRUE, MAKELPARAM(0, 400));
    SendMessage(pApp->hGain, TBM_SETPOS, TRUE, 100);
    SendMessage(pApp->hGain, TBM_SETPAGESIZE, 0, 25);
    pApp->hGainState = CreateChild(pApp, L"STATIC", L"Original audio", SS_RIGHT | SS_ENDELLIPSIS, 0, ID_GAIN_STATE);
    pApp->hCast      = CreateChild(pApp, L"BUTTON", L"Cast", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, ID_CAST);
    pApp->hHint      = CreateChild(pApp,
                              L"STATIC",
                              L"A file is served to the receiver over HTTP from this PC; an http(s) URL is sent as is.",
                              SS_LEFT | SS_ENDELLIPSIS,
                              0,
                              ID_HINT);
    pApp->hPlayPause = CreateChild(pApp, L"BUTTON", L"Play", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_PLAY_PAUSE);
    pApp->hStop      = CreateChild(pApp, L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_STOP);
    pApp->hSeek      = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | TBS_BOTH | WS_TABSTOP, 0, ID_SEEK);
    pApp->hTime      = CreateChild(pApp, L"STATIC", L"", SS_RIGHT, 0, ID_TIME);
    pApp->hStatus    = CreateChild(pApp, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, ID_STATUS);
    ListView_SetBkColor(pApp->hList, DARK_FIELD);
    ListView_SetTextBkColor(pApp->hList, DARK_FIELD);
    ListView_SetTextColor(pApp->hList, DARK_TEXT);

    /* Transcoder (proxy) configuration panel: sliders whose settings drive the GPU proxy encode. */
    GpuTranscode_InitializeConfig(&pApp->transcode);
    pApp->hCfgLabel = CreateChild(pApp,
                                   L"STATIC",
                                   L"Proxy transcoder \x2014 adjust, then Preview to test quality on the receiver",
                                   SS_LEFT | SS_ENDELLIPSIS,
                                   0,
                                   ID_CFG_LABEL);
    pApp->hResValue  = CreateChild(pApp, L"STATIC", L"", SS_LEFT, 0, ID_RES_VALUE);
    pApp->hResSlider = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_RES_SLIDER);
    pApp->hQpValue   = CreateChild(pApp, L"STATIC", L"", SS_LEFT, 0, ID_QP_VALUE);
    pApp->hQpSlider  = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_QP_SLIDER);
    pApp->hGopValue  = CreateChild(pApp, L"STATIC", L"", SS_LEFT, 0, ID_GOP_VALUE);
    pApp->hGopSlider = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_GOP_SLIDER);
    pApp->hMotionCheck =
        CreateChild(pApp, L"BUTTON", L"Optical flow", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_MOTION_CHECK);
    pApp->hMotionSlider =
        CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_MOTION_SLIDER);
    pApp->hMotionValue = CreateChild(pApp, L"STATIC", L"", SS_LEFT, 0, ID_MOTION_VALUE);
    pApp->hPreview     = CreateChild(pApp, L"BUTTON", L"Preview", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_PREVIEW);
    pApp->hCfgReadout  = CreateChild(pApp, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, ID_CFG_READOUT);

    SendMessage(pApp->hResSlider, TBM_SETRANGE, TRUE, MAKELPARAM(320, 1920));
    SendMessage(pApp->hResSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.width);
    SendMessage(pApp->hQpSlider, TBM_SETRANGE, TRUE, MAKELPARAM(18, 40));
    SendMessage(pApp->hQpSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.qpInter);
    SendMessage(pApp->hGopSlider, TBM_SETRANGE, TRUE, MAKELPARAM(6, 90));
    SendMessage(pApp->hGopSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.gopLength);
    SendMessage(pApp->hMotionSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(pApp->hMotionSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.motionStrength);
    SendMessage(pApp->hMotionCheck, BM_SETCHECK, pApp->transcode.motionAdaptive ? BST_CHECKED : BST_UNCHECKED, 0);

    ListView_SetExtendedListViewStyle(pApp->hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ZeroMemory(&column, sizeof(column));
    column.mask    = LVCF_TEXT | LVCF_WIDTH;
    column.cx      = 200;
    column.pszText = L"Receiver";
    ListView_InsertColumn(pApp->hList, 0, &column);
    column.pszText = L"Address";
    ListView_InsertColumn(pApp->hList, 1, &column);
    column.pszText = L"Model";
    ListView_InsertColumn(pApp->hList, 2, &column);
    SetWindowSubclass(ListView_GetHeader(pApp->hList), DarkHeaderProc, 1, (DWORD_PTR)pApp);
    SendMessage(pApp->hSeek, TBM_SETRANGE, TRUE, MAKELPARAM(0, SEEK_RANGE));
    /* A click on the channel should jump to the cursor and scrub, not page toward it. */
    SetWindowSubclass(pApp->hSeek, SeekBarProc, 1, (DWORD_PTR)pApp);
    ApplyFont(pApp);
    Layout(pApp);
    UpdateConfigLabels(pApp);
    EnableWindow(pApp->hMotionSlider, pApp->transcode.motionAdaptive);
    UpdateControls(pApp);
}

/* ------------------------------------------------------------------ discovery */

/*************************************************************************************************/
/**
 * \par Details:
 *  Writes discovery results into App-owned storage used by the persistent worker.
 */
static FORCE_INLINE void DiscoveryJob(void* pParameter)
{
    App* pApp = (App*)pParameter;

    pApp->discovered.count =
        Ssdp_DiscoverCastDevices(&pApp->discovery, pApp->discovered.devices, CAST_MAX_DEVICES, DISCOVERY_MS);
    pApp->discovered.error = Ssdp_GetError(&pApp->discovery);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Suppresses duplicate or closing-time requests and restores UI state if the
 *  persistent worker rejects submission.
 */
static FORCE_INLINE void StartDiscovery(App* pApp)
{
    if (pApp->discovering || pApp->closing)
    {
        return;
    }
    pApp->discovering = 1;
    SetStatus(pApp, L"Searching for Cast receivers...");
    UpdateControls(pApp);
    if (0 == OS_Thread_Submit(&pApp->discoveryWorker))
    {
        pApp->discovering = 0;
        SetStatus(pApp, L"Cannot start discovery.");
        UpdateControls(pApp);
        return;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Replaces receiver rows from the completed result, prefers the configured name
 *  match and refreshes status and control availability.
 */
static FORCE_INLINE void OnDiscovered(App* pApp, const DiscoveryResult* pResult)
{
    unsigned index;
    int      preferred = -1;
    WCHAR  text[160];
    bool_t failed;

    pApp->discovering = 0;
    if (NULL != pResult)
    {
        pApp->devices = *pResult;
    }
    else
    {
        pApp->devices.count = 0;
        pApp->devices.error = 0;
    }
    ListView_DeleteAllItems(pApp->hList);
    for (index = 0; index < pApp->devices.count; ++index)
    {
        const CastDevice* pDevice = &pApp->devices.devices[index];
        LVITEMW item;
        WCHAR   name[256];
        WCHAR   address[64];
        WCHAR   model[256];

        Utf8ToWide(pDevice->friendlyName, name, 256);
        Utf8ToWide(pDevice->host, address, 64);
        Utf8ToWide(pDevice->model, model, 256);

        ZeroMemory(&item, sizeof(item));
        item.mask    = LVIF_TEXT;
        item.iItem   = (int)index;
        item.pszText = name;
        ListView_InsertItem(pApp->hList, &item);
        ListView_SetItemText(pApp->hList, (int)index, 1, address);
        ListView_SetItemText(pApp->hList, (int)index, 2, model);
        if (preferred < 0 && ContainsNoCase(pDevice->friendlyName, PREFERRED_DEVICE))
        {
            preferred = (int)index;
        }
    }
    if (pApp->devices.count > 0)
    {
        const int select = (preferred >= 0) ? preferred : 0;

        ListView_SetItemState(pApp->hList, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        (void)swprintf(text,
                       sizeof(text) / sizeof(text[0]),
                       L"%u receiver%s found.",
                       pApp->devices.count,
                       1 == pApp->devices.count ? L"" : L"s");
    }
    else
    {
        (void)swprintf(text,
                       sizeof(text) / sizeof(text[0]),
                       L"No Cast receiver answered. Is the projector on and on this network?");
    }
    failed = (0 == pApp->devices.count) && (0 != pApp->devices.error);
    if (failed)
    {
        SetStatus(pApp, TEXT("Discovery failed. Check the network connection and retry."));
    }
    else
    {
        SetStatus(pApp, text);
    }
    UpdateControls(pApp);
}

/* ------------------------------------------------------------------ the session */

/* The session publishes bounded snapshots; the UI consumes them on its timer. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Copies session events into the generation-tagged mailbox for timer-driven
 *  consumption by the UI thread.
 */
static FORCE_INLINE void OnSessionEvent(void* pUser, CastEvent event, const CastStatus* pStatus, const char* pText)
{
    SessionEvents* pEvents = (SessionEvents*)pUser;

    (void)CastEvents_Publish(&pEvents->mailbox, pEvents->generation, event, pStatus, pText);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Runs the Cast session until its conversation ends.
 */
static FORCE_INLINE void SessionJob(void* context)
{
    App* app;

    app = (App*)context;
    LOGASSERT(NULL != app->pSession);
    (void)CastSession_Run(app->pSession);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Skips unchanged source text and remote URLs, enumerates local tracks and
 *  rebuilds the combo with Automatic and Off entries before explicit tracks.
 */
static FORCE_INLINE void RefreshSubtitles(App* pApp)
{
    WCHAR source[1024], automatic[380];
    GetWindowTextW(pApp->hFile, source, 1024);
    if (!wcscmp(source, pApp->subtitleSource))
        return;
    wcscpy_s(pApp->subtitleSource, 1024, source);
    pApp->subtitleCount = _wcsnicmp(source, L"http", 4) ? Subtitles_Find(source, pApp->subtitles) : 0;
    SendMessage(pApp->hSubtitle, CB_RESETCONTENT, 0, 0);
    _snwprintf_s(automatic,
                 380,
                 _TRUNCATE,
                 L"Automatic - %ls",
                 pApp->subtitleCount ? pApp->subtitles[0].label : L"no supported subtitles found");
    SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)automatic);
    SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)L"Off");
    for (unsigned i = 0; i < pApp->subtitleCount; ++i)
        SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)pApp->subtitles[i].label);
    SendMessage(pApp->hSubtitle, CB_SETCURSEL, 0, 0);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Appends a selected SRT/WebVTT file to the bounded track list and selects it
 *  in the subtitle combo.
 */
static FORCE_INLINE void BrowseSubtitles(App* pApp)
{
    OPENFILENAMEW dialog         = {0};
    WCHAR         path[MAX_PATH] = {0};
    RefreshSubtitles(pApp);
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner   = pApp->hWnd;
    dialog.lpstrFilter = L"Subtitles\0*.srt;*.vtt\0";
    dialog.lpstrFile   = path;
    dialog.nMaxFile    = MAX_PATH;
    dialog.lpstrTitle  = L"Choose subtitles";
    dialog.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&dialog) && pApp->subtitleCount < SUBTITLE_MAX_TRACKS)
    {
        Subtitles_InitializeFileTrack(path, &pApp->subtitles[pApp->subtitleCount]);
        SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)pApp->subtitles[pApp->subtitleCount++].label);
        SendMessage(pApp->hSubtitle, CB_SETCURSEL, (WPARAM)pApp->subtitleCount + 1, 0);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Runs conversion against the submitted context. OS_Thread publishes completion
 *  only after this callback returns.
 */
static FORCE_INLINE void GainJob(void* context)
{
    App* app;

    app         = (App*)context;
    app->gainOk = AudioGain_Convert(app->originalSource,
                                    app->gainOutput,
                                    app->gainPercent,
                                    &app->gainCancel,
                                    &app->gainProgress,
                                    app->gainError,
                                    sizeof(app->gainError));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Latches cancellation without waiting. A running callback retains its output
 *  path until PollJobs acquires completion and deletes unclaimed output.
 */
static FORCE_INLINE void CancelGain(App* app)
{
    KillTimer(app->hWnd, GAIN_TIMER);
    if (!app->gainDiscard)
    {
        ++app->gainSerial;
    }
    app->gainRestartPending = FALSE;
    app->gainDiscard        = TRUE;
    if (app->gainBusy)
    {
        (void)InterlockedExchange(&app->gainCancel, TRUE);
    }
    else if (app->gainOutput[0])
    {
        (void)DeleteFile(app->gainOutput);
        app->gainOutput[0] = TEXT('\0');
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Uses owner access to the completed gain context to restore original audio
 *  or submit conversion of the latest slider value to the startup worker.
 */
static FORCE_INLINE void StartGain(App* app)
{
    uint32_t percent;
    bool_t   has_session;
    bool_t   has_source;
    bool_t   can_prepare;
    bool_t   use_original;
    DWORD    length;
    WCHAR    directory[MAX_PATH];
    bool_t   valid_path;
    int32_t  written;

    LOGASSERT(!app->gainBusy);
    LOGASSERT(!app->closing);
    LOGASSERT(!app->stopping);
    LOGASSERT(!app->finishPending);

    percent                 = (uint32_t)SendMessage(app->hGain, TBM_GETPOS, 0, 0);
    app->gainRestartPending = FALSE;
    app->gainDiscard        = FALSE;
    has_session             = (NULL != app->pSession) || app->preparingAudio;
    has_source              = !!app->originalSource[0];
    can_prepare             = has_session && has_source;
    if (can_prepare)
    {
        use_original = (100 == percent) && (!app->audioNeedsConversion) && (!app->preparingAudio);
        if (use_original)
        {
            CastSession_ReplaceMedia(app->pSession, app->originalUrl, app->originalType);
            SetWindowText(app->hGainState, TEXT("Original audio"));
        }
        else
        {
            length     = GetTempPath(MAX_PATH, directory);
            valid_path = (0 < length) && (length < MAX_PATH);
            if (valid_path)
            {
                written = _snwprintf_s(app->gainOutput,
                                          MAX_PATH,
                                          _TRUNCATE,
                                          L"%lscvkcast_gain_%lu_%u.mp4",
                                          directory,
                                          GetCurrentProcessId(),
                                          app->gainSerial);
                valid_path = (0 <= written);
            }
            if (valid_path)
            {
                app->gainPercent = percent;
                (void)InterlockedExchange(&app->gainCancel, 0);
                (void)InterlockedExchange(&app->gainProgress, 0);
                app->gainActiveSerial = app->gainSerial;
                app->gainBusy         = OS_Thread_Submit(&app->gainWorker);
                SetWindowText(app->hGainState,
                              app->gainBusy ? TEXT("Preparing gain...") : TEXT("Cannot start audio worker"));
                if ((!app->gainBusy) && app->preparingAudio)
                {
                    FinishSession(app, TEXT("Cannot start audio preparation."));
                }
            }
            else
            {
                SetWindowText(app->hGainState, TEXT("Cannot create gain file"));
                if (app->preparingAudio)
                {
                    FinishSession(app, TEXT("Cannot create the audio preparation file."));
                }
            }
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Coalesces gain changes while cancellation is pending. The timer starts the
 *  latest slider value only after the previous callback releases its context.
 */
static FORCE_INLINE void ScheduleGain(App* app)
{
    bool_t can_schedule;

    CancelGain(app);
    can_schedule            = (!app->closing) && (!app->stopping) && (!app->finishPending);
    app->gainRestartPending = can_schedule;
    if (can_schedule && (!app->gainBusy))
    {
        StartGain(app);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Consumes an acquired completion, serves successful converted output and
 *  either starts the prepared session or requests a media replacement.
 */
static FORCE_INLINE void FinishGain(App* app, unsigned serial)
{
    GainVariant* variant;
    char         local[64], error[256], url[1200];
    LOGASSERT(!app->gainBusy);
    LOGASSERT(serial == app->gainSerial);
    if ((!app->pSession && !app->preparingAudio) || !app->gainOk)
    {
        WCHAR message[256];

        Utf8ToWide(app->gainError, message, 256);
        SetWindowTextW(app->hGainState, L"Audio preparation failed");
        if (app->preparingAudio)
            FinishSession(app, message);
        else
        {
            CancelGain(app);
            SetStatus(app, message);
        }
        return;
    }
    variant = (GainVariant*)calloc(1, sizeof(*variant));
    /* Use the same reachable local address selected for the original file. */
    if (!variant || sscanf_s(app->originalUrl, "http://%63[^:]", local, (unsigned)sizeof(local)) != 1)
    {
        free(variant);
        CancelGain(app);
        if (app->preparingAudio)
            FinishSession(app, L"Cannot serve prepared audio.");
        SetWindowTextW(app->hGainState, L"Cannot serve audio gain");
        return;
    }
    variant->server = MediaServer_Start(&app->serving, app->gainOutput, NULL, 0, "video/mp4", error, sizeof(error));
    if (!variant->server)
    {
        free(variant);
        CancelGain(app);
        if (app->preparingAudio)
            FinishSession(app, L"Cannot serve prepared audio.");
        SetWindowTextW(app->hGainState, L"Cannot serve audio gain");
        return;
    }
    wcscpy_s(variant->path, MAX_PATH, app->gainOutput);
    app->gainOutput[0] = 0;
    variant->next      = app->gainVariants;
    app->gainVariants  = variant;
    snprintf(url, sizeof(url), "http://%s:%u/gain.mp4", local, (unsigned)MediaServer_GetPort(variant->server));
    if (app->preparingAudio)
    {
        snprintf(app->pendingConfig.contentUrl, sizeof(app->pendingConfig.contentUrl), "%s", url);
        snprintf(app->pendingConfig.contentType, sizeof(app->pendingConfig.contentType), "video/mp4");
        ++app->events.generation;
        CastEvents_Begin(&app->events.mailbox, app->events.generation);
        CastSession_Prepare(&app->session, &app->pendingConfig);
        app->pSession    = &app->session;
        app->sessionBusy = OS_Thread_Submit(&app->sessionWorker);
        if (!app->sessionBusy)
        {
            FinishSession(app, L"Cannot start the Cast session.");
            return;
        }
        app->preparingAudio = 0;
        ZeroMemory(&app->status, sizeof(app->status));
        UpdateControls(app);
        SetWindowTextW(app->hGainState, L"AAC audio ready");
    }
    else
    {
        CastSession_ReplaceMedia(app->pSession, url, "video/mp4");
        SetWindowTextW(app->hGainState, L"Gain ready - switching");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Prepares the selected receiver and source, optional subtitle serving and
 *  audio conversion. Normal casting may also start a scrub proxy; previews use
 *  the supplied transcoded file directly.
 */
static FORCE_INLINE void StartCastSource(App* pApp, const WCHAR* pSourceIn, int buildProxy)
{
    const int            selected = ListView_GetNextItem(pApp->hList, -1, LVNI_SELECTED);
    const CastDevice* pDevice;
    CastSessionConfig config;
    WCHAR             source[1024];
    char              error[256];
    char              localAddress[64];
    const byte_t*        vtt      = NULL;
    uint64_t             vttSize  = 0;
    const SubtitleTrack* subtitle = NULL;

    if (pApp->closing || pApp->finishPending || pApp->gainBusy || NULL != pApp->pSession || pApp->preparingAudio ||
        selected < 0 || (unsigned)selected >= pApp->devices.count)
    {
        return;
    }
    pDevice = &pApp->devices.devices[selected];
    wcscpy_s(source, sizeof(source) / sizeof(source[0]), pSourceIn);
    if (0 == source[0])
    {
        return;
    }
    ZeroMemory(&config, sizeof(config));
    (void)snprintf(config.host, sizeof(config.host), "%s", pDevice->host);
    config.callback = OnSessionEvent;
    config.pUser    = &pApp->events;
    if (buildProxy)
    {
        int choice;
        RefreshSubtitles(pApp);
        choice = (int)SendMessage(pApp->hSubtitle, CB_GETCURSEL, 0, 0);
        if (choice == 0 && pApp->subtitleCount)
            subtitle = &pApp->subtitles[0];
        else if (choice >= 2 && (unsigned)(choice - 2) < pApp->subtitleCount)
            subtitle = &pApp->subtitles[choice - 2];
        if (subtitle)
        {
            if (!Subtitles_Load(&pApp->subtitleWorkspace, subtitle, error, sizeof(error)))
            {
                WCHAR message[256];
                Utf8ToWide(error, message, 256);
                SetStatus(pApp, message);
                return;
            }
            vtt     = Subtitles_GetText(&pApp->subtitleWorkspace);
            vttSize = Subtitles_GetByteCount(&pApp->subtitleWorkspace);
        }
    }
    pApp->originalSource[0] = 0;
    if (0 == _wcsnicmp(source, L"http://", 7) || 0 == _wcsnicmp(source, L"https://", 8))
    {
        const wchar_t* pExtension = wcsrchr(source, L'.');

        ToUtf8(source, config.contentUrl, sizeof(config.contentUrl));
        ToUtf8(source, config.title, sizeof(config.title));
        (void)snprintf(config.contentType,
                       sizeof(config.contentType),
                       "%s",
                       (NULL != pExtension && NULL == wcschr(pExtension, L'/'))
                           ? MediaServer_GetContentTypeForExtension(pExtension)
                           : "video/mp4");
    }
    else
    {
        const wchar_t* pName      = wcsrchr(source, L'\\');
        const wchar_t* pExtension = wcsrchr(source, L'.');
        char extension[16];

        pName = (NULL != pName) ? pName + 1 : source;
        if (NULL != pExtension && pExtension < pName)
        {
            pExtension = NULL;
        }
        ToUtf8(pName, config.title, sizeof(config.title));
        ToUtf8(NULL != pExtension ? pExtension : L"", extension, sizeof(extension));
        (void)snprintf(config.contentType,
                       sizeof(config.contentType),
                       "%s",
                       MediaServer_GetContentTypeForExtension(pExtension));
        config.servingLocally = 1;
        if (!CastNetwork_ReadLocalAddress(&pApp->routeNetwork,
                                          pDevice->host,
                                          sizeof(pDevice->host),
                                          localAddress,
                                          sizeof(localAddress)))
        {
            SetStatus(pApp, L"Cannot determine the local address facing the receiver.");
            return;
        }
        pApp->pServer = MediaServer_StartWithSubtitles(&pApp->serving,
                                                       source,
                                                       NULL,
                                                       DEFAULT_SERVER_PORT,
                                                       config.contentType,
                                                       vtt,
                                                       vttSize,
                                                       error,
                                                       sizeof(error));
        if (NULL == pApp->pServer)
        {
            WCHAR message[256];

            Utf8ToWide(error, message, 256);
            SetStatus(pApp, message);
            return;
        }
        (void)snprintf(config.contentUrl,
                       sizeof(config.contentUrl),
                       "http://%s:%u/media%s",
                       localAddress,
                       (unsigned)MediaServer_GetPort(pApp->pServer),
                       extension);
        /* Scrub previews snap to keyframes so the receiver paints them after a one-frame decode. */
        config.keyframeCount = Mp4Index_ReadKeyframes(source, pApp->keyframes, MP4_INDEX_MAX_KEYFRAMES);
        config.pKeyframes    = pApp->keyframes;
        wcscpy_s(pApp->proxySource, MAX_PATH, source);
        (void)snprintf(pApp->proxyLocal, sizeof(pApp->proxyLocal), "%s", localAddress);
        pApp->proxyPort = (unsigned short)(MediaServer_GetPort(pApp->pServer) + 1);
        wcscpy_s(pApp->originalSource, MAX_PATH, source);
    }
    if (vtt && !pApp->pServer)
    {
        if (CastNetwork_ReadLocalAddress(&pApp->routeNetwork,
                                         pDevice->host,
                                         sizeof(pDevice->host),
                                         localAddress,
                                         sizeof(localAddress)))
            pApp->pServer = MediaServer_StartWithSubtitles(&pApp->serving,
                                                           NULL,
                                                           NULL,
                                                           DEFAULT_SERVER_PORT,
                                                           "video/mp4",
                                                           vtt,
                                                           vttSize,
                                                           error,
                                                           sizeof(error));
        if (!pApp->pServer)
        {
            SetStatus(pApp, L"Cannot serve subtitles to the receiver.");
            return;
        }
    }
    if (vtt)
    {
        snprintf(config.subtitleUrl,
                 sizeof(config.subtitleUrl),
                 "http://%s:%u/subtitles.vtt",
                 localAddress,
                 (unsigned)MediaServer_GetPort(pApp->pServer));
        snprintf(config.subtitleLanguage, sizeof(config.subtitleLanguage), "%s", subtitle->language);
    }
    snprintf(pApp->originalUrl, sizeof(pApp->originalUrl), "%s", config.contentUrl);
    snprintf(pApp->originalType, sizeof(pApp->originalType), "%s", config.contentType);
    pApp->audioNeedsConversion = 0;
    if (buildProxy && pApp->originalSource[0])
    {
        pApp->audioNeedsConversion = AudioGain_NeedsConversion(source, error, sizeof(error));
        if (pApp->audioNeedsConversion < 0)
        {
            WCHAR message[256];
            Utf8ToWide(error, message, 256);
            FinishSession(pApp, message);
            return;
        }
        if (pApp->audioNeedsConversion || SendMessage(pApp->hGain, TBM_GETPOS, 0, 0) != 100)
        {
            pApp->pendingConfig  = config;
            pApp->preparingAudio = 1;
            pApp->proxyLocal[0]  = 0;
            pApp->stopping       = 0;
            SetStatus(pApp, L"Preparing AAC audio before playback; video quality is preserved. Stop cancels.");
            UpdateControls(pApp);
            ScheduleGain(pApp);
            return;
        }
    }
    ++pApp->events.generation;
    CastEvents_Begin(&pApp->events.mailbox, pApp->events.generation);
    CastSession_Prepare(&pApp->session, &config);
    pApp->pSession    = &pApp->session;
    pApp->sessionBusy = OS_Thread_Submit(&pApp->sessionWorker);
    if (!pApp->sessionBusy)
    {
        FinishSession(pApp, TEXT("Cannot start the Cast session."));
        return;
    }
    if (buildProxy && pApp->proxyLocal[0])
    {
        Proxy_Request_t request;

        request.serving = &pApp->serving;
        request.source  = pApp->proxySource;

        request.sourceCapacity       = MAX_PATH;
        request.localAddress         = pApp->proxyLocal;
        request.localAddressCapacity = sizeof(pApp->proxyLocal);
        request.config               = &pApp->transcode;
        request.port                 = pApp->proxyPort;
        request.session              = pApp->pSession;
        (void)Proxy_Start(&pApp->proxy, &request);
    }
    else
    {
        pApp->proxyLocal[0] = 0;
    }
    ZeroMemory(&pApp->status, sizeof(pApp->status));
    pApp->stopping = 0;
    UpdateControls(pApp);
    if (buildProxy && SendMessage(pApp->hGain, TBM_GETPOS, 0, 0) != 100)
        ScheduleGain(pApp);
}

/* The Cast button: play the chosen file with the background scrub proxy. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Reads the source edit control and starts normal casting with proxy generation.
 */
static FORCE_INLINE void StartCast(App* pApp)
{
    WCHAR source[1024];

    GetWindowTextW(pApp->hFile, source, (int)(sizeof(source) / sizeof(source[0])));
    StartCastSource(pApp, source, 1);
}

/* A short transcode of the source at the current settings, run on a worker thread so the sliders
** stay responsive. OS_Thread publishes the App-owned result after callback return. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Transcodes the requested clip into App-owned result storage, derives the
 *  preview readout from its report/file size. The owner acquires it through OS_Thread.
 */
static FORCE_INLINE void PreviewJobRun(void* context)
{
    PreviewJob*    pJob    = (PreviewJob*)context;
    PreviewResult* pResult = pJob->pResult;
    char        report[1800];
    const char* pMarker;

    ZeroMemory(pResult, sizeof(*pResult));
    wcscpy_s(pResult->path, MAX_PATH, pJob->dest);
    pResult->ok = GpuTranscode_WriteMp4(&pJob->gpuDevice,
                                        pJob->source,
                                        pJob->dest,
                                        &pJob->config,
                                        pJob->startFrame,
                                        pJob->frames,
                                        pJob->pCancel,
                                        report,
                                        sizeof(report));
    if (pResult->ok)
    {
        HANDLE        hFile;
        unsigned      produced;
        double        motion;
        LARGE_INTEGER size;

        produced = 0;
        motion   = 0.0;
        /* The report ends with "... (N video frames from S)"; pull the frame count and motion. */
        pMarker = strstr(report, "video frames from");
        if (NULL != pMarker)
        {
            const char* pDigits;
            bool_t      is_digit;

            for (pDigits = pMarker; pDigits > report; --pDigits)
            {
                is_digit = ('0' <= pDigits[-1]) && ('9' >= pDigits[-1]);
                if (is_digit)
                {
                    break;
                }
            }
            for (;;)
            {
                if (pDigits == report)
                {
                    break;
                }
                is_digit = ('0' <= pDigits[-1]) && ('9' >= pDigits[-1]);
                if (!is_digit)
                {
                    break;
                }
                --pDigits;
            }
            produced = (unsigned)atoi(pDigits);
        }
        pMarker = strstr(report, "avg motion ");
        if (NULL != pMarker)
        {
            motion = atof(pMarker + 11) / 100.0;
        }
        pResult->width         = pJob->config.width;
        pResult->frames        = produced;
        pResult->motionPercent = (int)(motion * 100.0 + 0.5);
        size.QuadPart          = 0;
        hFile =
            CreateFileW(pJob->dest, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (INVALID_HANDLE_VALUE != hFile)
        {
            double seconds;

            GetFileSizeEx(hFile, &size);
            CloseHandle(hFile);
            seconds       = (produced > 0) ? (double)produced / 24.0 : 1.0; /* nominal frame rate for the readout */
            pResult->kbps = (unsigned)((double)size.QuadPart * 8.0 / 1000.0 / (seconds > 0.1 ? seconds : 0.1));
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Captures the current settings and estimates a clip start from playback position,
 *  then starts a worker using App-owned job and result storage.
 */
static FORCE_INLINE void StartPreview(App* pApp)
{
    WCHAR       source[MAX_PATH];
    DWORD       length;
    WCHAR       temp[MAX_PATH];
    PreviewJob* pJob;
    int         position;
    double      ratio;

    if (pApp->closing || pApp->previewBusy || pApp->preparingAudio)
    {
        return;
    }
    GetWindowTextW(pApp->hFile, source, MAX_PATH);
    if (0 == source[0] || 0 == _wcsnicmp(source, L"http", 4))
    {
        SetWindowTextW(pApp->hCfgReadout, L"Preview needs a local video file.");
        return;
    }
    ReadConfigFromSliders(pApp);
    length = GetTempPathW(MAX_PATH, temp);
    if (0 == length || length > MAX_PATH - 40)
    {
        return;
    }
    pJob = &pApp->previewJob;
    wcscpy_s(pJob->source, MAX_PATH, source);
    (void)_snwprintf_s(temp + length, MAX_PATH - length, _TRUNCATE, L"cvkcast_preview_%lu.mp4", GetCurrentProcessId());
    wcscpy_s(pJob->dest, MAX_PATH, temp);
    pJob->config        = pApp->transcode;
    pJob->hWnd          = pApp->hWnd;
    pJob->pResult       = &pApp->previewResult;
    pApp->previewCancel = 0;
    pJob->pCancel       = &pApp->previewCancel;
    /* Start the clip near the current playback position (or a quarter in), snapped to a keyframe. */
    position         = (int)SendMessage(pApp->hSeek, TBM_GETPOS, 0, 0);
    ratio            = (pApp->status.duration > 0 && position > 0) ? (double)position / SEEK_RANGE : 0.25;
    pJob->startFrame = (unsigned)(ratio * 24.0 * (pApp->status.duration > 0 ? pApp->status.duration : 600.0));
    pJob->frames     = 8u * 24u; /* about eight seconds at ~24 fps */

    pApp->previewBusy = 1;
    EnableWindow(pApp->hPreview, FALSE);
    SetWindowTextW(pApp->hCfgReadout, L"Building preview\x2026");
    if (!OS_Thread_Submit(&pApp->previewWorker))
    {
        pApp->previewBusy = 0;
        EnableWindow(pApp->hPreview, TRUE);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Cancels preparation or posts a single Stop request for the active session.
 */
static FORCE_INLINE void StopCast(App* pApp)
{
    if (pApp->preparingAudio)
    {
        FinishSession(pApp, L"Audio preparation cancelled.");
        return;
    }
    if (NULL != pApp->pSession && !pApp->stopping)
    {
        pApp->stopping = 1;
        CastSession_Post(pApp->pSession, CAST_COMMAND_STOP, 0);
        UpdateControls(pApp);
    }
}

/* Moves the slider and the readout to a position, unless the user holds the slider. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Leaves the slider under user control during dragging; otherwise updates its
 *  position and formatted state/time readout.
 */
static FORCE_INLINE void ShowPosition(App* pApp, double seconds)
{
    WCHAR duration[32];
    WCHAR position[32];
    WCHAR text[128];

    if (pApp->seeking)
    {
        return;
    }
    if (pApp->status.duration > 0)
    {
        if (seconds > pApp->status.duration)
        {
            seconds = pApp->status.duration;
        }
        SendMessage(pApp->hSeek, TBM_SETPOS, TRUE, (LPARAM)(LONG)(seconds / pApp->status.duration * SEEK_RANGE + 0.5));
    }
    else
    {
        SendMessage(pApp->hSeek, TBM_SETPOS, TRUE, 0);
    }
    if (pApp->status.hasMedia)
    {
        FormatTime(seconds, position, sizeof(position) / sizeof(position[0]));
        FormatTime(pApp->status.duration, duration, sizeof(duration) / sizeof(duration[0]));
        (void)swprintf(text,
                       sizeof(text) / sizeof(text[0]),
                       L"%hs  %ls / %ls",
                       pApp->status.stateName,
                       position,
                       duration);
    }
    else
    {
        (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"%hs", pApp->status.stateName);
    }
    SetWindowTextW(pApp->hTime, text);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Copies the latest status and records its receipt tick before refreshing the
 *  position display and controls.
 */
static FORCE_INLINE void OnStatus(App* pApp, const CastStatus* pStatus)
{
    if (NULL == pStatus)
    {
        return;
    }
    pApp->status     = *pStatus;
    pApp->statusTick = GetTickCount64();
    ShowPosition(pApp, pApp->status.currentTime);
    UpdateControls(pApp);
}

/* Between the receiver's reports, advance the display at playback rate. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Extrapolates the displayed position between receiver reports while playback
 *  is active and the user is not dragging.
 */
static FORCE_INLINE void OnPositionTimer(App* pApp)
{
    if (NULL != pApp->pSession && CAST_STATE_PLAYING == pApp->status.state && pApp->status.hasMedia && !pApp->seeking)
    {
        ShowPosition(pApp, pApp->status.currentTime + (double)(GetTickCount64() - pApp->statusTick) / 1000.0);
    }
}

/* Reaps the session thread; safe to call when no session exists. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Defers session destruction until its callback has returned, then stops serving
 *  and conversion resources and clears session UI state.
 */
static FORCE_INLINE void FinishSession(App* pApp, const WCHAR* pReason)
{
    bool_t       session_complete;
    bool_t       session_failed;
    bool_t       proxy_stopped;
    bool_t       server_stopped;
    bool_t       variants_stopped;
    bool_t       workers_complete;
    GainVariant* variant;
    bool_t       variant_stopped;
    bool_t       file_removed;
    uint32_t     error;
    bool_t       servers_complete;
    bool_t       can_release;
    bool_t       has_reason;
    bool_t       saved_reason;

    if (pApp->sessionBusy)
    {
        session_complete = OS_Thread_TakeResult(&pApp->sessionWorker);
        session_failed   = (OS_THREAD_FAILED == OS_Thread_GetState(&pApp->sessionWorker));
        if (session_complete || session_failed)
        {
            pApp->sessionBusy = FALSE;
        }
    }
    CancelGain(pApp);
    proxy_stopped    = Proxy_TryStop(&pApp->proxy);
    server_stopped   = MediaServer_TryStop(&pApp->pServer);
    variants_stopped = (NULL == pApp->gainVariants);
    workers_complete = (!pApp->sessionBusy) && (!pApp->gainBusy);
    if (workers_complete && (!variants_stopped))
    {
        /* Retire at most one gain variant per cleanup attempt. */
        variant         = pApp->gainVariants;
        variant_stopped = MediaServer_TryStop(&variant->server);
        if (variant_stopped)
        {
            file_removed = DeleteFile(variant->path);
            if (!file_removed)
            {
                error        = GetLastError();
                file_removed = (ERROR_FILE_NOT_FOUND == error) || (ERROR_PATH_NOT_FOUND == error);
            }
            if (file_removed)
            {
                pApp->gainVariants = variant->next;
                free(variant);
            }
        }
        variants_stopped = (NULL == pApp->gainVariants);
    }
    servers_complete = proxy_stopped && server_stopped && variants_stopped;
    can_release      = workers_complete && servers_complete;
    if (!can_release)
    {
        pApp->finishPending = TRUE;
        has_reason          = (NULL != pReason);
        saved_reason        = (pReason == pApp->finishReason);
        if (has_reason && (!saved_reason))
        {
            wcscpy_s(pApp->finishReason, CAST_EVENTS_TEXT_CAPACITY, pReason);
        }
    }
    else
    {
        pApp->proxyLocal[0] = 0;
        if (NULL != pApp->pSession)
        {
            CastSession_Reset(pApp->pSession);
        }
        pApp->pSession             = NULL;
        pApp->originalSource[0]    = 0;
        pApp->preparingAudio       = 0;
        pApp->audioNeedsConversion = 0;
        ZeroMemory(&pApp->pendingConfig, sizeof(pApp->pendingConfig));
        SetWindowTextW(pApp->hGainState, L"");
        pApp->stopping      = 0;
        pApp->finishPending = 0;
        ZeroMemory(&pApp->status, sizeof(pApp->status));
        SendMessage(pApp->hSeek, TBM_SETPOS, TRUE, 0);
        SetWindowTextW(pApp->hTime, L"");
        if (NULL != pReason)
        {
            SetStatus(pApp, pReason);
        }
        UpdateControls(pApp);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Consumes one mailbox snapshot on the UI thread and retries deferred session
 *  cleanup after handling status, log and terminal events.
 */
static FORCE_INLINE void PollSessionEvents(App* pApp)
{
    CastEvents_Snapshot_t snapshot;
    WCHAR                 text[CAST_EVENTS_TEXT_CAPACITY];

    if (CastEvents_Take(&pApp->events.mailbox, &snapshot))
    {
        if (snapshot.present & CAST_EVENTS_STATUS)
        {
            OnStatus(pApp, &snapshot.status);
        }
        if (snapshot.present & CAST_EVENTS_LOG)
        {
            Utf8ToWide(snapshot.log, text, CAST_EVENTS_TEXT_CAPACITY);
            SetStatus(pApp, text);
        }
        if (snapshot.present & CAST_EVENTS_ENDED)
        {
            Utf8ToWide(snapshot.reason, text, CAST_EVENTS_TEXT_CAPACITY);
            FinishSession(pApp, text);
        }
    }
    if (pApp->finishPending)
    {
        FinishSession(pApp, pApp->finishReason);
    }
}

/* Moves to a fraction of the duration. final: the drag ended, so seek exactly and resume;
** otherwise a live scrub preview (paused, keyframe-snapped in the session). */
/*************************************************************************************************/
/**
 * \par Details:
 *  Clamps a timeline fraction and posts either a scrub update or a final seek.
 *  The local slider/readout follows the requested target immediately.
 */
static FORCE_INLINE void SeekToRatio(App* pApp, double ratio, int final)
{
    double seconds;

    if (NULL == pApp->pSession || pApp->status.duration <= 0)
    {
        return;
    }
    if (ratio < 0.0)
    {
        ratio = 0.0;
    }
    if (ratio > 1.0)
    {
        ratio = 1.0;
    }
    seconds = pApp->status.duration * ratio;
    SendMessage(pApp->hSeek, TBM_SETPOS, TRUE, (LPARAM)(LONG)(ratio * SEEK_RANGE + 0.5));
    if (final)
    {
        pApp->seeking = 0;
        CastSession_Post(pApp->pSession, CAST_COMMAND_SEEK, seconds);
        pApp->status.currentTime = seconds;
        pApp->statusTick         = GetTickCount64();
        ShowPosition(pApp, seconds);
    }
    else
    {
        WCHAR preview[32];
        WCHAR duration[32];
        WCHAR text[96];

        pApp->seeking = 1;
        CastSession_Post(pApp->pSession, CAST_COMMAND_SCRUB, seconds);
        FormatTime(seconds, preview, sizeof(preview) / sizeof(preview[0]));
        FormatTime(pApp->status.duration, duration, sizeof(duration) / sizeof(duration[0]));
        (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"SEEK  %ls / %ls", preview, duration);
        SetWindowTextW(pApp->hTime, text);
    }
}

/* The click position as a fraction of the channel: how far along the media the cursor points. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Maps the mouse coordinate into the trackbar channel's horizontal span.
 *  A nonpositive span produces zero.
 */
static FORCE_INLINE double GetRatioFromClick(HWND hSeek, LPARAM lParam)
{
    RECT channel;
    const int x = GET_X_LPARAM(lParam);
    int span;

    SendMessage(hSeek, TBM_GETCHANNELRECT, 0, (LPARAM)&channel);
    span = channel.right - channel.left;
    return (span > 0) ? (double)(x - channel.left) / span : 0.0;
}

/* Subclass: a channel click jumps to the cursor and scrubs; release seeks there exactly. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Captures mouse dragging for scrub updates, sends a final seek on release and
 *  removes its subclass during nonclient destruction.
 */
static FORCE_INLINE LRESULT CALLBACK
SeekBarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData)
{
    App* pApp = (App*)refData;

    switch (message)
    {
    case WM_LBUTTONDOWN:
        SetFocus(hWnd);
        SetCapture(hWnd);
        SeekToRatio(pApp, GetRatioFromClick(hWnd, lParam), 0);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == hWnd && (MK_LBUTTON & wParam))
        {
            SeekToRatio(pApp, GetRatioFromClick(hWnd, lParam), 0);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hWnd)
        {
            ReleaseCapture();
            SeekToRatio(pApp, GetRatioFromClick(hWnd, lParam), 1);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(hWnd, SeekBarProc, idSubclass);
        break;
    default:
        break;
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

/* Keyboard (arrows, page keys) still scrolls the slider through the default proc; seek to
** wherever it lands, exactly. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Translates keyboard-driven slider movement into a final seek; mouse thumb
 *  tracking is handled by the subclass.
 */
static FORCE_INLINE void OnSeekScroll(App* pApp, int code)
{
    const int position = (int)SendMessage(pApp->hSeek, TBM_GETPOS, 0, 0);

    if (NULL == pApp->pSession || pApp->status.duration <= 0 || TB_THUMBTRACK == code)
    {
        return; /* mouse tracking is handled by the subclass */
    }
    CastSession_Post(pApp->pSession, CAST_COMMAND_SEEK, pApp->status.duration * position / SEEK_RANGE);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Opens the media file dialog and copies a successful selection to the source
 *  control. An existing URL is not used as the initial local filename.
 */
static FORCE_INLINE void Browse(App* pApp)
{
    WCHAR         file[MAX_PATH];
    OPENFILENAMEW dialog;

    file[0] = 0;
    GetWindowTextW(pApp->hFile, file, MAX_PATH);
    if (0 == _wcsnicmp(file, L"http", 4))
    {
        file[0] = 0;
    }
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner   = pApp->hWnd;
    dialog.lpstrFilter = L"Video files\0*.mp4;*.m4v;*.mov;*.webm;*.mkv\0Audio "
                         L"files\0*.mp3;*.m4a;*.aac;*.flac;*.wav;*.ogg\0All files\0*.*\0";
    dialog.lpstrFile  = file;
    dialog.nMaxFile   = MAX_PATH;
    dialog.lpstrTitle = L"Choose a video to cast";
    dialog.Flags      = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&dialog))
    {
        SetWindowTextW(pApp->hFile, file);
        UpdateControls(pApp);
    }
}

/* Windows' own flyout for wireless (Miracast) displays: the path for a projector that is a
** Windows display rather than a Cast receiver. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Opens the Windows wireless-display discovery URI through the shell.
 */
static FORCE_INLINE void OpenWindowsCast(App* pApp)
{
    ShellExecuteW(pApp->hWnd, L"open", L"ms-settings-connectabledevices:devicediscovery", NULL, NULL, SW_SHOWNORMAL);
}

/* ------------------------------------------------------------------ the window */

/*************************************************************************************************/
/**
 * \par Details:
 *  Establishes completion polling before acquiring worker resources. Failure to
 *  create the timer rejects window creation without starting any worker.
 *  A partial worker initialization is closed by the established timer.
 */
static FORCE_INLINE BOOL OnCreate(HWND hWnd, LPCREATESTRUCT pCreate)
{
    App*     pApp;
    UINT_PTR timer;
    bool_t   timer_ready;
    BOOL     dark;
    bool_t   serving_ready;
    bool_t   storage_ready;
    bool_t   discovery_storage_ready;
    bool_t   route_ready;
    bool_t   discovery_ready;
    bool_t   gain_ready;
    bool_t   session_ready;
    bool_t   preview_ready;
    bool_t   state_ready;
    bool_t   execution_ready;
    bool_t   workers_ready;

    (void)pCreate;

    pApp        = GetApp(hWnd);
    pApp->hWnd  = hWnd;
    timer       = SetTimer(hWnd, POSITION_TIMER, POSITION_TICK_MS, NULL);
    timer_ready = (0 != timer);
    if (timer_ready)
    {
        dark                  = TRUE;
        pApp->dpi             = GetDpiForWindow(hWnd);
        pApp->backgroundBrush = CreateSolidBrush(DARK_BACKGROUND);
        pApp->fieldBrush      = CreateSolidBrush(DARK_FIELD);
        pApp->selectedBrush   = CreateSolidBrush(RGB(48, 91, 146));
        pApp->accentBrush     = CreateSolidBrush(DARK_ACCENT);
        pApp->mutedBrush      = CreateSolidBrush(DARK_MUTED);
        pApp->borderBrush     = CreateSolidBrush(RGB(62, 68, 78));
        CastEvents_Initialize(&pApp->events.mailbox);
        Subtitles_Initialize(&pApp->subtitleWorkspace,
                             pApp->subtitleScratch,
                             pApp->subtitleOutput,
                             SUBTITLE_TEXT_CAPACITY);
        /* Optional proxy startup failure keeps original-stream casting available. */
        (void)Proxy_Initialize(&pApp->proxy);
        serving_ready           = MediaServer_Initialize(&pApp->serving);
        storage_ready           = CastSession_Initialize(&pApp->session);
        discovery_storage_ready = Ssdp_Initialize(&pApp->discovery);
        route_ready             = OS_Network_Initialize(&pApp->routeNetwork);
        discovery_ready         = OS_Thread_Initialize(&pApp->discoveryWorker, DiscoveryJob, pApp);
        gain_ready              = OS_Thread_Initialize(&pApp->gainWorker, GainJob, pApp);
        session_ready           = OS_Thread_Initialize(&pApp->sessionWorker, SessionJob, pApp);
        preview_ready           = OS_Thread_Initialize(&pApp->previewWorker, PreviewJobRun, &pApp->previewJob);
        state_ready             = serving_ready && storage_ready && discovery_storage_ready && route_ready;
        execution_ready         = discovery_ready && gain_ready && session_ready && preview_ready;
        workers_ready           = state_ready && execution_ready;
        DwmSetWindowAttribute(hWnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        CreateControls(pApp);
        if (workers_ready)
        {
            StartDiscovery(pApp);
        }
        else
        {
            pApp->closing = TRUE;
            EnableWindow(hWnd, FALSE);
        }
    }

    return timer_ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Fills the client area with the retained background brush.
 */
static FORCE_INLINE BOOL OnEraseBackground(HWND hWnd, HDC dc)
{
    App* pApp;

    pApp = GetApp(hWnd);
    RECT rect;
    GetClientRect(hWnd, &rect);
    FillRect(dc, &rect, pApp->backgroundBrush);
    return TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Selects field or background colors and returns the corresponding retained
 *  brush, using muted text for disabled controls.
 */
static FORCE_INLINE HBRUSH OnControlColor(HWND hWnd, HDC dc, HWND hControl, int type)
{
    App* pApp;

    pApp = GetApp(hWnd);
    (void)hWnd;

    int field = type == CTLCOLOR_EDIT || type == CTLCOLOR_LISTBOX || hControl == pApp->hSubtitle;
    SetTextColor(dc, IsWindowEnabled(hControl) ? DARK_TEXT : DARK_MUTED);
    SetBkColor(dc, field ? DARK_FIELD : DARK_BACKGROUND);
    return field ? pApp->fieldBrush : pApp->backgroundBrush;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Sets the subtitle combo's DPI-scaled item height and forwards other requests.
 */
static FORCE_INLINE void OnMeasureItem(HWND hWnd, MEASUREITEMSTRUCT* item)
{
    App* pApp;

    pApp = GetApp(hWnd);
    if (item->CtlType == ODT_COMBOBOX && item->CtlID == ID_SUBTITLE)
    {
        item->itemHeight = (UINT)Scale(pApp, 22);
        return;
    }
    FORWARD_WM_MEASUREITEM(hWnd, item, DefWindowProcW);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Draws subtitle entries or dark buttons with selection/focus state; forwards
 *  unhandled control types to default window processing.
 */
static FORCE_INLINE void OnDrawItem(HWND hWnd, const DRAWITEMSTRUCT* item)
{
    App* pApp;

    pApp = GetApp(hWnd);
    WCHAR  label[128];
    HBRUSH fill;
    if (item->CtlType == ODT_COMBOBOX && item->CtlID == ID_SUBTITLE)
    {
        PaintSubtitleItem(item->hwndItem,
                          item->hDC,
                          item->rcItem,
                          item->itemID == (UINT)-1 ? -1 : (int)item->itemID,
                          item->itemState,
                          pApp);
        return;
    }
    if (item->CtlType != ODT_BUTTON)
    {
        FORWARD_WM_DRAWITEM(hWnd, item, DefWindowProcW);
        return;
    }
    fill = (item->itemState & ODS_SELECTED) ? pApp->selectedBrush : pApp->fieldBrush;
    FillRect(item->hDC, &item->rcItem, fill);
    SetBkMode(item->hDC, TRANSPARENT);
    SetTextColor(item->hDC, (item->itemState & ODS_DISABLED) ? DARK_MUTED : DARK_TEXT);
    GetWindowTextW(item->hwndItem, label, 128);
    {
        RECT rect = item->rcItem;
        DrawTextW(item->hDC, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (item->itemState & ODS_FOCUS)
        {
            InflateRect(&rect, -3, -3);
            DrawFocusRect(item->hDC, &rect);
        }
    }
    return;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Callback publication replaces posted completion pointers. Cancellation keeps
 *  the context and output path alive until the callback has returned.
 */
static FORCE_INLINE void PollJobs(App* app)
{
    bool_t failed;
    bool_t completed;
    bool_t discard;
    bool_t restart;

    failed = (OS_THREAD_FAILED == OS_Thread_GetState(&app->sessionWorker));
    if (app->sessionBusy && failed)
    {
        FinishSession(app, TEXT("Session worker stopped."));
    }

    if (app->gainBusy)
    {
        completed = OS_Thread_TakeResult(&app->gainWorker);
        failed    = (OS_THREAD_FAILED == OS_Thread_GetState(&app->gainWorker));
        if (completed || failed)
        {
            app->gainBusy = FALSE;
            discard       = app->gainDiscard || app->closing || app->finishPending || app->stopping;
            if (discard)
            {
                if (app->gainOutput[0])
                {
                    (void)DeleteFile(app->gainOutput);
                    app->gainOutput[0] = TEXT('\0');
                }
            }
            else
            {
                if (failed)
                {
                    app->gainOk = FALSE;
                }
                FinishGain(app, app->gainActiveSerial);
            }
        }
    }
    if (app->previewBusy)
    {
        completed = OS_Thread_TakeResult(&app->previewWorker);
        failed    = (OS_THREAD_FAILED == OS_Thread_GetState(&app->previewWorker));
        if (completed || failed)
        {
            if (failed)
            {
                app->previewResult.ok = FALSE;
            }
            OnPreviewDone(app->hWnd, &app->previewResult);
        }
    }
    restart =
        app->gainRestartPending && (!app->gainBusy) && (!app->closing) && (!app->finishPending) && (!app->stopping);
    if (restart)
    {
        StartGain(app);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Polls worker/session progress, advances the position readout and handles
 *  debounced subtitle/gain jobs. Closing polls all worker exits without waiting.
 */
static FORCE_INLINE void OnTimer(HWND hWnd, UINT id)
{
    App*   pApp = GetApp(hWnd);
    bool_t discovery_closed;
    bool_t gain_closed;
    bool_t session_closed;
    bool_t preview_closed;
    bool_t jobs_closed;
    bool_t can_release;
    bool_t can_destroy;
    bool_t session_storage_closed;
    bool_t discovery_storage_closed;
    bool_t route_closed;
    bool_t proxy_closed;
    bool_t serving_closed;
    bool_t discovery_failed;

    if (POSITION_TIMER == id)
    {
        PollSessionEvents(pApp);
        PollJobs(pApp);
        if (pApp->finishPending)
        {
            FinishSession(pApp, pApp->finishReason);
        }
        if (pApp->closing)
        {
            discovery_closed = OS_Thread_TryClose(&pApp->discoveryWorker);
            gain_closed      = FALSE;
            session_closed   = FALSE;
            preview_closed   = FALSE;
            if (!pApp->gainBusy)
            {
                gain_closed = OS_Thread_TryClose(&pApp->gainWorker);
            }
            if (!pApp->sessionBusy)
            {
                session_closed = OS_Thread_TryClose(&pApp->sessionWorker);
            }
            if (!pApp->previewBusy)
            {
                preview_closed = OS_Thread_TryClose(&pApp->previewWorker);
            }
            jobs_closed = discovery_closed && gain_closed && session_closed && preview_closed;
            can_release = jobs_closed && (!pApp->pSession) && (!pApp->finishPending);
            can_destroy = FALSE;
            if (can_release)
            {
                session_storage_closed   = CastSession_TryClose(&pApp->session);
                discovery_storage_closed = Ssdp_TryClose(&pApp->discovery);
                route_closed             = OS_Network_TryClose(&pApp->routeNetwork);
                proxy_closed             = Proxy_TryClose(&pApp->proxy);
                serving_closed           = FALSE;
                if (proxy_closed)
                {
                    serving_closed = MediaServer_TryClose(&pApp->serving);
                }
                can_destroy = session_storage_closed && discovery_storage_closed && route_closed && proxy_closed && serving_closed;
            }
            if (can_destroy)
            {
                DestroyWindow(hWnd);
            }
            return;
        }
        if (OS_Thread_TakeResult(&pApp->discoveryWorker))
        {
            OnDiscovered(pApp, &pApp->discovered);
        }
        discovery_failed = (OS_THREAD_FAILED == OS_Thread_GetState(&pApp->discoveryWorker));
        if (pApp->discovering && discovery_failed)
        {
            pApp->discovering = FALSE;
            SetStatus(pApp, TEXT("Discovery worker stopped."));
            UpdateControls(pApp);
        }
        OnPositionTimer(pApp);
        if (pApp->gainBusy)
        {
            WCHAR progress[100];
            _snwprintf_s(progress,
                         100,
                         _TRUNCATE,
                         L"Preparing AAC (%u%%): %ld%%",
                         pApp->gainPercent,
                         InterlockedCompareExchange(&pApp->gainProgress, 0, 0));
            SetWindowTextW(pApp->hGainState, progress);
        }
    }
    else if (id == SUBTITLE_TIMER)
    {
        KillTimer(hWnd, SUBTITLE_TIMER);
        if (!pApp->closing && !pApp->finishPending && !pApp->gainBusy && !pApp->pSession && !pApp->preparingAudio)
            RefreshSubtitles(pApp);
    }
    else if (id == GAIN_TIMER)
    {
        KillTimer(hWnd, GAIN_TIMER);
        if (!pApp->closing)
        {
            ScheduleGain(pApp);
        }
    }
    return;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void OnSize(HWND hWnd, UINT state, int width, int height)
{
    App* pApp;

    pApp = GetApp(hWnd);
    (void)hWnd;
    (void)state;
    (void)width;
    (void)height;

    Layout(pApp);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Applies the DPI-scaled minimum tracking size.
 */
static FORCE_INLINE void OnGetMinMaxInfo(HWND hWnd, MINMAXINFO* pInfo)
{
    App* pApp;

    pApp = GetApp(hWnd);
    (void)hWnd;

    pInfo->ptMinTrackSize.x = Scale(pApp, 620);
    pInfo->ptMinTrackSize.y = Scale(pApp, 584);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Accepts the suggested window rectangle and refreshes fonts and layout using
 *  the new vertical DPI.
 */
static FORCE_INLINE void OnDpiChanged(HWND hWnd, UINT dpiX, UINT dpiY, const RECT* pSuggested)
{
    App* pApp;

    pApp = GetApp(hWnd);
    (void)dpiX;

    pApp->dpi = dpiY;
    SetWindowPos(hWnd,
                 NULL,
                 pSuggested->left,
                 pSuggested->top,
                 pSuggested->right - pSuggested->left,
                 pSuggested->bottom - pSuggested->top,
                 SWP_NOZORDER | SWP_NOACTIVATE);
    ApplyFont(pApp);
    Layout(pApp);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Routes control commands and file-edit notifications to their application
 *  operations; forwards unhandled commands.
 */
static FORCE_INLINE void OnCommand(HWND hWnd, int id, HWND hControl, UINT codeNotify)
{
    App* pApp;

    pApp = GetApp(hWnd);
    switch (id)
    {
    case ID_REFRESH:
        StartDiscovery(pApp);
        return;
    case ID_WINDOWS_CAST:
        OpenWindowsCast(pApp);
        return;
    case ID_BROWSE:
        Browse(pApp);
        return;
    case ID_SUBTITLE_BROWSE:
        BrowseSubtitles(pApp);
        return;
    case ID_SUBTITLE:
        InvalidateRect(pApp->hSubtitle, NULL, FALSE);
        return;
    case ID_CAST:
        StartCast(pApp);
        return;
    case ID_PLAY_PAUSE:
        if (NULL != pApp->pSession)
        {
            CastSession_Post(pApp->pSession, CAST_COMMAND_TOGGLE, 0);
        }
        return;
    case ID_STOP:
        StopCast(pApp);
        return;
    case ID_FILE:
        if (EN_CHANGE == codeNotify)
        {
            UpdateControls(pApp);
            SetTimer(hWnd, SUBTITLE_TIMER, 350, NULL);
        }
        return;
    case ID_MOTION_CHECK:
        ReadConfigFromSliders(pApp);
        return;
    case ID_PREVIEW:
        StartPreview(pApp);
        return;
    default:
        break;
    }
    FORWARD_WM_COMMAND(hWnd, id, hControl, codeNotify, DefWindowProcW);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Supplies slider custom drawing and receiver-list selection/double-click
 *  behavior, then forwards notifications requiring default processing.
 */
static FORCE_INLINE LRESULT OnNotify(HWND hWnd, int idFrom, NMHDR* pHeader)
{
    App* pApp;

    pApp = GetApp(hWnd);
    if (NM_CUSTOMDRAW == pHeader->code)
    {
        NMCUSTOMDRAW* draw   = (NMCUSTOMDRAW*)pHeader;
        int           slider = pHeader->hwndFrom == pApp->hGain || pHeader->hwndFrom == pApp->hSeek ||
                               pHeader->hwndFrom == pApp->hResSlider || pHeader->hwndFrom == pApp->hQpSlider ||
                               pHeader->hwndFrom == pApp->hGopSlider || pHeader->hwndFrom == pApp->hMotionSlider;
        if (slider)
        {
            if (draw->dwDrawStage == CDDS_PREPAINT)
                return CDRF_NOTIFYITEMDRAW;
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT)
            {
                HBRUSH brush = draw->dwItemSpec == TBCD_THUMB
                                   ? (IsWindowEnabled(pHeader->hwndFrom) ? pApp->accentBrush : pApp->mutedBrush)
                                   : pApp->fieldBrush;
                FillRect(draw->hdc, &draw->rc, brush);
                return CDRF_SKIPDEFAULT;
            }
        }
    }

    if (pHeader->hwndFrom == pApp->hList)
    {
        if (LVN_ITEMCHANGED == pHeader->code)
        {
            UpdateControls(pApp);
        }
        else if (NM_DBLCLK == pHeader->code && IsWindowEnabled(pApp->hCast))
        {
            StartCast(pApp);
        }
    }
    return FORWARD_WM_NOTIFY(hWnd, idFrom, pHeader, DefWindowProcW);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Updates gain captions and its debounce timer, routes seeking, or copies
 *  transcoder slider values into configuration.
 */
static FORCE_INLINE void OnHScroll(HWND hWnd, HWND hControl, UINT code, int position)
{
    App* pApp;

    pApp = GetApp(hWnd);
    (void)position;

    if (hControl == pApp->hGain)
    {
        WCHAR text[64];
        unsigned percent = (unsigned)SendMessage(pApp->hGain, TBM_GETPOS, 0, 0);
        _snwprintf_s(text, 64, _TRUNCATE, L"Audio gain %u%%", percent);
        SetWindowTextW(pApp->hGainLabel, text);
        if (pApp->pSession || pApp->preparingAudio)
            SetTimer(hWnd, GAIN_TIMER, 500, NULL);
        else
            SetWindowTextW(pApp->hGainState, percent == 100 ? L"Original audio" : L"Applies during playback");
    }
    else if (hControl == pApp->hSeek)
    {
        OnSeekScroll(pApp, code);
    }
    else if (hControl == pApp->hResSlider || hControl == pApp->hQpSlider || hControl == pApp->hGopSlider ||
             hControl == pApp->hMotionSlider)
    {
        ReadConfigFromSliders(pApp);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Publishes a successful preview's readout and starts it only when no Cast
 *  session is active. Closing suppresses preview playback.
 */
static FORCE_INLINE void OnPreviewDone(HWND hWnd, const PreviewResult* pResult)
{
    App* pApp;

    pApp = GetApp(hWnd);
    (void)hWnd;

    pApp->previewBusy = 0;
    EnableWindow(pApp->hPreview, !pApp->preparingAudio);
    if (!pApp->closing && NULL != pResult && pResult->ok && pResult->frames > 0)
    {
        WCHAR text[192];

        (void)swprintf(text,
                       sizeof(text) / sizeof(text[0]),
                       L"%u px, QP %d, ~%u kbps, motion %d%% \x2014 playing on the receiver",
                       pResult->width,
                       pApp->transcode.qpInter,
                       pResult->kbps,
                       pResult->motionPercent);
        SetWindowTextW(pApp->hCfgReadout, text);
        /* Show the transcoded clip on the receiver: no proxy, since the clip is the output. */
        if (NULL == pApp->pSession)
        {
            StartCastSource(pApp, pResult->path, 0);
        }
    }
    else
    {
        SetWindowTextW(pApp->hCfgReadout, L"Preview failed (need a local AVC MP4 with an H.264 track).");
    }
    (void)pResult; /* App-owned storage; nothing to free */
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Latches closing, requests worker cancellation and session Stop, and starts
 *  cleanup. The position timer completes deferred window destruction.
 */
static FORCE_INLINE void OnClose(HWND hWnd)
{
    App* app;

    app = GetApp(hWnd);
    if (!app->closing)
    {
        app->closing = TRUE;
        EnableWindow(hWnd, FALSE);
        CancelGain(app);
        (void)InterlockedExchange(&app->previewCancel, TRUE);
        if (NULL != app->pSession)
        {
            CastSession_Post(app->pSession, CAST_COMMAND_STOP, 0);
        }
        (void)OS_Thread_Stop(&app->discoveryWorker);
        (void)OS_Thread_Stop(&app->gainWorker);
        (void)OS_Thread_Stop(&app->sessionWorker);
        (void)OS_Thread_Stop(&app->previewWorker);
        FinishSession(app, NULL);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Deletes the retained theme brushes and posts the thread's quit message.
 */
static FORCE_INLINE void OnDestroy(HWND hWnd)
{
    App* pApp;

    pApp = GetApp(hWnd);
    (void)hWnd;

    DeleteObject(pApp->backgroundBrush);
    DeleteObject(pApp->fieldBrush);
    DeleteObject(pApp->selectedBrush);
    DeleteObject(pApp->accentBrush);
    DeleteObject(pApp->mutedBrush);
    DeleteObject(pApp->borderBrush);
    pApp->windowDestroyed = TRUE;
    PostQuitMessage(0);
}

/* Message crackers for messages absent from windowsx.h; signatures match the handlers. */
#ifndef HANDLE_WM_DPICHANGED
/* void OnDpiChanged(HWND, UINT dpiX, UINT dpiY, const RECT* suggested) */
#define HANDLE_WM_DPICHANGED(hwnd, wParam, lParam, fn)                                                                 \
    ((fn)((hwnd), (UINT)LOWORD(wParam), (UINT)HIWORD(wParam), (const RECT*)(lParam)), (LRESULT)0)
#endif

/*************************************************************************************************/
/**
 * \brief Binds the caller-owned context passed to CreateWindowEx.
 *
 * \param[in] window   Native window being constructed
 * \param[in] create   Native creation record containing the required context
 *
 * \retval    TRUE    Context binding succeeded.
 * \retval    FALSE   Native binding failed; creation must stop.
 */
static FORCE_INLINE BOOL OnNcCreate(HWND window, LPCREATESTRUCT create);

/*************************************************************************************************/
/**
 * \par Details:
 *  A zero previous value is expected for a new window. Last-error distinguishes
 *  that successful result from native failure before WM_CREATE may use the owner.
 */
static FORCE_INLINE BOOL OnNcCreate(HWND window, LPCREATESTRUCT create)
{
    App*     app;
    LONG_PTR previous;
    DWORD    error;
    bool_t   has_previous;
    bool_t   no_error;
    bool_t   attached;

    LOGASSERT(NULL != create);
    LOGASSERT(NULL != create->lpCreateParams);

    app          = (App*)create->lpCreateParams;
    app->hWnd    = window;
    SetLastError(ERROR_SUCCESS);
    previous     = SetWindowLongPtr(window, GWLP_USERDATA, (LONG_PTR)app);
    error        = GetLastError();
    has_previous = (0 != previous);
    no_error     = (ERROR_SUCCESS == error);
    attached     = has_previous || no_error;
    return attached;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Uses the main-window message crackers and forwards unhandled messages to
 *  default window processing.
 */
static FORCE_INLINE LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    App* app;
    LRESULT result;

    switch (message)
    {
        HANDLE_MSG(hWnd, WM_NCCREATE, OnNcCreate);
    default:
        break;
    }
    app = (App*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
    if (NULL != app)
    {
        switch (message)
        {
            HANDLE_MSG(hWnd, WM_CREATE, OnCreate);
            HANDLE_MSG(hWnd, WM_ERASEBKGND, OnEraseBackground);
            HANDLE_MSG(hWnd, WM_CTLCOLORSTATIC, OnControlColor);
            HANDLE_MSG(hWnd, WM_CTLCOLORBTN, OnControlColor);
            HANDLE_MSG(hWnd, WM_CTLCOLOREDIT, OnControlColor);
            HANDLE_MSG(hWnd, WM_CTLCOLORLISTBOX, OnControlColor);
            HANDLE_MSG(hWnd, WM_MEASUREITEM, OnMeasureItem);
            HANDLE_MSG(hWnd, WM_DRAWITEM, OnDrawItem);
            HANDLE_MSG(hWnd, WM_TIMER, OnTimer);
            HANDLE_MSG(hWnd, WM_SIZE, OnSize);
            HANDLE_MSG(hWnd, WM_GETMINMAXINFO, OnGetMinMaxInfo);
            HANDLE_MSG(hWnd, WM_DPICHANGED, OnDpiChanged);
            HANDLE_MSG(hWnd, WM_COMMAND, OnCommand);
            HANDLE_MSG(hWnd, WM_NOTIFY, OnNotify);
            HANDLE_MSG(hWnd, WM_HSCROLL, OnHScroll);
            HANDLE_MSG(hWnd, WM_CLOSE, OnClose);
            HANDLE_MSG(hWnd, WM_DESTROY, OnDestroy);
        default:
            break;
        }
    }
    result = DefWindowProc(hWnd, message, wParam, lParam);
    if (WM_NCDESTROY == message)
    {
        (void)SetWindowLongPtr(hWnd, GWLP_USERDATA, 0);
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Initializes DPI, OLE, Winsock and common controls, creates the application
 *  window, then runs dialog-key translation and the window message loop.
 */
static FORCE_INLINE int RunWindow(App* app, HINSTANCE instance, int showCommand)
{
    HRESULT              ole_result;
    bool_t               ole_ready;
    WSADATA              wsa_data;
    int32_t              network_result;
    bool_t               network_ready;
    int32_t              result;
    INITCOMMONCONTROLSEX controls;
    bool_t               controls_ready;
    WNDCLASSEX           window_class;
    ATOM                 class_atom;
    bool_t               class_ready;
    HWND                 window;
    bool_t               window_ready;
    int32_t              argument_count;
    LPWSTR*              arguments;
    MSG                  message;
    BOOL                 message_result;
    bool_t               pump_failed;
    bool_t               has_message;
    bool_t               is_quit;

    LOGASSERT(NULL != app);

    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ole_result    = OleInitialize(NULL);
    ole_ready     = SUCCEEDED(ole_result);
    network_ready = FALSE;
    class_ready   = FALSE;
    result        = 1;
    if (ole_ready)
    {
        network_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        network_ready  = (0 == network_result);
    }
    if (network_ready)
    {
        controls.dwSize = sizeof(controls);
        controls.dwICC  = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
        controls_ready  = !!InitCommonControlsEx(&controls);
        if (controls_ready)
        {
            ZeroMemory(&window_class, sizeof(window_class));
            window_class.cbSize        = sizeof(window_class);
            window_class.lpfnWndProc   = WindowProc;
            window_class.hInstance     = instance;
            window_class.hCursor       = LoadCursor(NULL, IDC_ARROW);
            window_class.hbrBackground = NULL;
            window_class.lpszClassName = TEXT("cvkCastDemo");
            window_class.hIcon         = LoadIcon(NULL, IDI_APPLICATION);
            class_atom                 = RegisterClassEx(&window_class);
            class_ready                = (0 != class_atom);
        }
    }
    if (class_ready)
    {
        app->hInstance = instance;
        app->dpi       = GetDpiForSystem();
        window         = CreateWindowEx(0,
                                        TEXT("cvkCastDemo"),
                                        TEXT("CastDemo"),
                                        WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX,
                                        CW_USEDEFAULT,
                                        CW_USEDEFAULT,
                                        Scale(app, 720),
                                        Scale(app, 700),
                                        NULL,
                                        NULL,
                                        instance,
                                        app);
        window_ready = (NULL != window);
        if (window_ready)
        {
            argument_count = 0;
            arguments      = CommandLineToArgvW(GetCommandLineW(), &argument_count);
            if (NULL != arguments)
            {
                if (1 < argument_count)
                {
                    SetWindowTextW(app->hFile, arguments[1]);
                    UpdateControls(app);
                }
                (void)LocalFree(arguments);
            }
            ShowWindow(window, showCommand);
            pump_failed = FALSE;
            for (;;)
            {
                if (app->windowDestroyed)
                {
                    break;
                }
                if (pump_failed)
                {
                    has_message = !!PeekMessage(&message, NULL, 0, 0, PM_REMOVE);
                    if (!has_message)
                    {
                        OnTimer(window, POSITION_TIMER);
                        (void)MsgWaitForMultipleObjectsEx(0, NULL, POSITION_TICK_MS, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
                    }
                }
                else
                {
                    message_result = GetMessage(&message, NULL, 0, 0);
                    has_message    = (0 < message_result);
                    is_quit        = (0 == message_result);
                    if (!has_message)
                    {
                        if (is_quit)
                        {
                            result = (int32_t)message.wParam;
                        }
                        pump_failed = TRUE;
                        OnClose(window);
                    }
                }
                if (has_message)
                {
                    if (!IsDialogMessage(window, &message))
                    {
                        TranslateMessage(&message);
                        DispatchMessage(&message);
                    }
                }
            }
            if (!pump_failed)
            {
                result = 0;
            }
        }
        if (NULL != app->hFont)
        {
            (void)DeleteObject(app->hFont);
        }
        (void)UnregisterClass(TEXT("cvkCastDemo"), instance);
    }
    if (network_ready)
    {
        (void)WSACleanup();
    }
    if (ole_ready)
    {
        OleUninitialize();
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Retains the context outside the window procedure until all workers and the
 *  native window have stopped using it. Native memory-release failure is retried.
 */
int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE previousInstance, LPWSTR commandLine, int showCommand)
{
    OS_Memory_t memory;
    bool_t      ready;
    int32_t     result;
    App*        app;
    bool_t      closed;

    (void)previousInstance;
    (void)commandLine;

    ready  = OS_Memory_Initialize(&memory, sizeof(App));
    result = 1;
    if (ready)
    {
        app    = (App*)OS_Memory_GetAddress(&memory);
        GpuDevice_Initialize(&app->previewJob.gpuDevice);
        result = RunWindow(app, instance, showCommand);
    }
    for (;;)
    {
        closed = OS_Memory_TryClose(&memory);
        if (closed)
        {
            break;
        }
        Sleep(POSITION_TICK_MS);
    }
    return result;
}
