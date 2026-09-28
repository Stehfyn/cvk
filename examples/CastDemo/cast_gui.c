/* cast_gui.c - CastDemo: a small Win32 window that lists the Cast receivers on the network and
** plays a video on the selected one, with a Play/Pause, Stop and seek toolbar.
**
** The window thread owns the controls. Discovery and the Cast session each run on a worker
** thread and report back with posted messages carrying heap copies of their results.
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
#include "media_server.h"
#include "mp4_index.h"
#include "proxy.h"
#include "ssdp.h"
#include "subtitles.h"
#include "audio_gain.h"

#if defined(_MSC_VER)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#define WM_APP_DISCOVERED   (WM_APP + 1) /* lParam: DiscoveryResult* on the heap */
#define WM_APP_LOG          (WM_APP + 2) /* lParam: WCHAR* on the heap */
#define WM_APP_STATUS       (WM_APP + 3) /* lParam: CastStatus* on the heap */
#define WM_APP_ENDED        (WM_APP + 4) /* lParam: WCHAR* on the heap, or NULL */
#define WM_APP_PREVIEW_DONE (WM_APP + 5) /* lParam: PreviewResult* on the heap */
#define WM_APP_GAIN_DONE    (WM_APP + 6)

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
    unsigned   count;
    CastDevice devices[CAST_MAX_DEVICES];
} DiscoveryResult;

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
    HWND      hSubtitleLabel, hSubtitle, hSubtitleBrowse;
    HWND      hGainLabel, hGain, hGainState;
    HBRUSH    backgroundBrush, fieldBrush;
    SubtitleTrack subtitles[SUBTITLE_MAX_TRACKS];
    unsigned subtitleCount;
    WCHAR subtitleSource[1024];
    WCHAR originalSource[MAX_PATH];
    char originalUrl[1200], originalType[64];
    HANDLE hGainThread;
    volatile LONG gainCancel, gainProgress;
    unsigned gainPercent, gainSerial;
    int gainOk;
    char gainError[256];
    WCHAR gainOutput[MAX_PATH];
    struct GainVariant* gainVariants;
    HWND      hCast;
    HWND      hHint;
    HWND      hPlayPause;
    HWND      hStop;
    HWND      hSeek;
    HWND      hTime;
    HWND      hStatus;
    HWND      hCfgLabel;
    HWND      hResSlider;
    HWND      hResValue;
    HWND      hQpSlider;
    HWND      hQpValue;
    HWND      hGopSlider;
    HWND      hGopValue;
    HWND      hMotionCheck;
    HWND      hMotionSlider;
    HWND      hMotionValue;
    HWND      hPreview;
    HWND      hCfgReadout;
    HFONT     hFont;
    UINT      dpi;
    /* receivers */
    DiscoveryResult devices;
    int             discovering;
    /* the active session */
    CastSession*   pSession;
    HANDLE         hSessionThread;
    MediaServer*   pServer;
    Proxy*         pProxy; /* background low-latency scrub proxy */
    WCHAR          proxySource[MAX_PATH];
    char           proxyLocal[64];
    unsigned short proxyPort;
    GpuTranscodeConfig transcode; /* live transcoder settings from the config sliders */
    double*        pKeyframes; /* the local file's keyframe times, for scrub snapping */
    CastStatus     status;
    ULONGLONG      statusTick; /* when status.currentTime was reported, for interpolation */
    int            seeking;    /* the user is dragging the slider */
    int            stopping;
    HANDLE         hPreviewThread;  /* the quality-preview transcode worker */
    volatile LONG  previewCancel;
    int            previewBusy;
    PreviewJob     previewJob;    /* fixed storage for the running preview, no per-click alloc */
    PreviewResult  previewResult;
} App;

#define POSITION_TIMER   (1)
#define POSITION_TICK_MS (50)
#define SUBTITLE_TIMER (2)
#define GAIN_TIMER (3)

#define DARK_BACKGROUND RGB(24, 26, 31)
#define DARK_FIELD RGB(36, 39, 46)
#define DARK_TEXT RGB(232, 235, 240)
#define DARK_MUTED RGB(151, 160, 174)
#define DARK_ACCENT RGB(82, 156, 245)

typedef struct GainVariant
{
    struct GainVariant* next;
    MediaServer* server;
    WCHAR path[MAX_PATH];
} GainVariant;

static App g_app;

static LRESULT CALLBACK SeekBarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData);
static void RefreshSubtitles(App* pApp);
static void ScheduleGain(App* pApp);

static int Scale(const App* pApp, int dips)
{
    return MulDiv(dips, (int)pApp->dpi, 96);
}

static WCHAR* DupUtf8ToWide(const char* pText)
{
    const int length = MultiByteToWideChar(CP_UTF8, 0, pText, -1, NULL, 0);
    WCHAR*    pWide  = (WCHAR*)malloc((size_t)(length > 0 ? length : 1) * sizeof(WCHAR));

    if (NULL != pWide)
    {
        if (length <= 0 || 0 == MultiByteToWideChar(CP_UTF8, 0, pText, -1, pWide, length))
        {
            pWide[0] = 0;
        }
    }
    return pWide;
}

static void ToUtf8(const wchar_t* pText, char* pOut, size_t capacity)
{
    if (0 == WideCharToMultiByte(CP_UTF8, 0, pText, -1, pOut, (int)capacity, NULL, NULL))
    {
        pOut[0] = 0;
    }
}

static void SetStatus(App* pApp, const WCHAR* pText)
{
    SetWindowTextW(pApp->hStatus, pText);
}

static int ContainsNoCase(const char* pHaystack, const char* pNeedle)
{
    const size_t needleLength = strlen(pNeedle);
    size_t       index;

    for (index = 0; pHaystack[index]; ++index)
    {
        if (0 == _strnicmp(pHaystack + index, pNeedle, needleLength))
        {
            return 1;
        }
    }
    return 0;
}

static void FormatTime(double seconds, WCHAR* pOut, size_t capacity)
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

static HWND CreateChild(App* pApp, const WCHAR* pClass, const WCHAR* pText, DWORD style, DWORD exStyle, int id)
{
    HWND child;
    if (!wcscmp(pClass, L"BUTTON") && (style & BS_TYPEMASK) != BS_AUTOCHECKBOX)
        style = (style & ~BS_TYPEMASK) | BS_OWNERDRAW;
    child = CreateWindowExW(exStyle, pClass, pText, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, pApp->hWnd, (HMENU)(INT_PTR)id, pApp->hInstance, NULL);
    SetWindowTheme(child, L"DarkMode_Explorer", NULL);
    return child;
}

/* The list-view owns its header, so header NM_CUSTOMDRAW never reaches WindowProc.
** Paint the header itself, including the unused area after the last column. Native
** header processing still handles column resizing and hit testing. */
static void PaintDarkHeader(HWND hWnd, HDC dc, const App* pApp)
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
        WCHAR text[128] = {0};
        HDITEMW item = {0};
        RECT rect, divider;
        item.mask = HDI_TEXT | HDI_FORMAT;
        item.pszText = text;
        item.cchTextMax = 128;
        if (!Header_GetItem(hWnd, i, &item) || !Header_GetItemRect(hWnd, i, &rect)) continue;
        divider = rect;
        divider.left = divider.right - 1;
        FillRect(dc, &divider, pApp->backgroundBrush);
        InflateRect(&rect, -Scale(pApp, 6), 0);
        DrawTextW(dc, text, -1, &rect, DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX |
                  ((item.fmt & HDF_RIGHT) ? DT_RIGHT : (item.fmt & HDF_CENTER) ? DT_CENTER : DT_LEFT));
    }
    RestoreDC(dc, saved);
}

static LRESULT CALLBACK DarkHeaderProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData)
{
    const App* pApp = (const App*)refData;
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hWnd, &paint);
        PaintDarkHeader(hWnd, dc, pApp);
        EndPaint(hWnd, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT) { PaintDarkHeader(hWnd, (HDC)wParam, pApp); return 0; }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hWnd, DarkHeaderProc, idSubclass);
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

static void PaintSubtitleItem(HWND combo, HDC dc, RECT rect, int index, UINT state, const App* pApp)
{
    WCHAR text[512] = {0};
    const int saved = SaveDC(dc);
    const int selected = (state & ODS_SELECTED) && !(state & ODS_COMBOBOXEDIT);
    HBRUSH brush = selected ? CreateSolidBrush(RGB(48, 91, 146)) : pApp->fieldBrush;
    FillRect(dc, &rect, brush);
    if (selected) DeleteObject(brush);
    SelectObject(dc, pApp->hFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, (state & ODS_DISABLED) ? DARK_MUTED : DARK_TEXT);
    if (index >= 0)
    {
        LRESULT length = SendMessageW(combo, CB_GETLBTEXTLEN, (WPARAM)index, 0);
        if (length >= 0 && length < 512) SendMessageW(combo, CB_GETLBTEXT, (WPARAM)index, (LPARAM)text);
    }
    InflateRect(&rect, -Scale(pApp, 6), 0);
    DrawTextW(dc, text, -1, &rect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    if ((state & ODS_FOCUS) && !(state & ODS_NOFOCUSRECT)) DrawFocusRect(dc, &rect);
    RestoreDC(dc, saved);
}

/* Owner-draw covers the list items; draw the closed field, border and arrow too,
** because visual styles otherwise retain a white dropdown button. */
static void PaintDarkCombo(HWND hWnd, HDC dc, const App* pApp)
{
    RECT client, text;
    const int saved = SaveDC(dc);
    const int enabled = IsWindowEnabled(hWnd);
    const int focused = GetFocus() == hWnd;
    const int arrowWidth = Scale(pApp, 24);
    HBRUSH border;
    POINT arrow[3];
    int x, y;
    GetClientRect(hWnd, &client);
    FillRect(dc, &client, pApp->fieldBrush);
    text = client;
    InflateRect(&text, -1, -1);
    text.right -= arrowWidth;
    PaintSubtitleItem(hWnd, dc, text, (int)SendMessageW(hWnd, CB_GETCURSEL, 0, 0),
                      ODS_COMBOBOXEDIT | (enabled ? 0 : ODS_DISABLED), pApp);
    border = CreateSolidBrush(focused && enabled ? DARK_ACCENT : RGB(62, 68, 78));
    FrameRect(dc, &client, border);
    DeleteObject(border);
    x = client.right - arrowWidth / 2;
    y = (client.top + client.bottom) / 2;
    arrow[0].x = x - Scale(pApp, 4); arrow[0].y = y - Scale(pApp, 2);
    arrow[1].x = x + Scale(pApp, 4); arrow[1].y = y - Scale(pApp, 2);
    arrow[2].x = x; arrow[2].y = y + Scale(pApp, 2);
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCBrushColor(dc, enabled ? DARK_TEXT : DARK_MUTED);
    SelectObject(dc, GetStockObject(NULL_PEN));
    Polygon(dc, arrow, 3);
    RestoreDC(dc, saved);
}

static LRESULT CALLBACK DarkComboProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData)
{
    const App* pApp = (const App*)refData;
    LRESULT result;
    if (message == WM_PAINT)
    {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(hWnd, &paint);
        PaintDarkCombo(hWnd, dc, pApp);
        EndPaint(hWnd, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT) { PaintDarkCombo(hWnd, (HDC)wParam, pApp); return 0; }
    if (message == WM_ERASEBKGND) return 1;
    if (message == WM_NCDESTROY) RemoveWindowSubclass(hWnd, DarkComboProc, idSubclass);
    result = DefSubclassProc(hWnd, message, wParam, lParam);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE ||
        message == CB_SETCURSEL || message == CB_SHOWDROPDOWN || message == WM_THEMECHANGED)
        InvalidateRect(hWnd, NULL, FALSE);
    return result;
}

static void ApplyFont(App* pApp)
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
        SendMessageW(hChild, WM_SETFONT, (WPARAM)pApp->hFont, TRUE);
    }
    SendMessageW(pApp->hSubtitle, CB_SETITEMHEIGHT, 0, Scale(pApp, 22));
    SendMessageW(pApp->hSubtitle, CB_SETITEMHEIGHT, (WPARAM)-1, Scale(pApp, 20));
}

static void Layout(App* pApp)
{
    RECT      client;
    const int margin  = Scale(pApp, 12);
    const int gap     = Scale(pApp, 8);
    const int rowH    = Scale(pApp, 24);
    const int buttonW = Scale(pApp, 96);
    const int smallW  = Scale(pApp, 72);
    const int labelH  = Scale(pApp, 18);
    const int timeW   = Scale(pApp, 150);
    const int valueW  = Scale(pApp, 108);
    const int cfgH    = labelH + gap + 5 * (rowH + gap); /* label + res + qp + gop + motion + actions */
    int       width;
    int       sliderX;
    int       y;
    int       listH;

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
    MoveWindow(pApp->hSubtitle, margin + Scale(pApp, 68), y, width - Scale(pApp, 68) - gap - buttonW, Scale(pApp, 220), TRUE);
    MoveWindow(pApp->hSubtitleBrowse, client.right - margin - buttonW, y, buttonW, rowH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hGainLabel, margin, y + Scale(pApp, 4), Scale(pApp, 108), labelH, TRUE);
    MoveWindow(pApp->hGain, margin + Scale(pApp, 112), y, width - Scale(pApp, 300), rowH, TRUE);
    MoveWindow(pApp->hGainState, client.right - margin - Scale(pApp, 180), y + Scale(pApp, 4), Scale(pApp, 180), labelH, TRUE);
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
    MoveWindow(pApp->hMotionValue, client.right - margin - Scale(pApp, 48), y + Scale(pApp, 4), Scale(pApp, 48), labelH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hPreview, margin, y, buttonW, rowH, TRUE);
    MoveWindow(pApp->hCfgReadout, margin + buttonW + gap, y + Scale(pApp, 4), width - buttonW - gap, labelH, TRUE);
    ListView_SetColumnWidth(pApp->hList, 0, width - Scale(pApp, 120) - Scale(pApp, 90) - Scale(pApp, 24));
    ListView_SetColumnWidth(pApp->hList, 1, Scale(pApp, 120));
    ListView_SetColumnWidth(pApp->hList, 2, Scale(pApp, 90));
}

static void UpdateControls(App* pApp)
{
    const int haveSession = (NULL != pApp->pSession);
    const int haveDevice  = (-1 != ListView_GetNextItem(pApp->hList, -1, LVNI_SELECTED));
    const int haveFile    = (GetWindowTextLengthW(pApp->hFile) > 0);
    const int playing     = (CAST_STATE_PLAYING == pApp->status.state || CAST_STATE_BUFFERING == pApp->status.state);

    EnableWindow(pApp->hCast, !haveSession && haveDevice && haveFile);
    EnableWindow(pApp->hRefresh, !pApp->discovering);
    EnableWindow(pApp->hStop, haveSession && !pApp->stopping);
    EnableWindow(pApp->hPlayPause, haveSession && pApp->status.hasMedia);
    EnableWindow(pApp->hSeek, haveSession && pApp->status.hasMedia && pApp->status.duration > 0);
    EnableWindow(pApp->hSubtitle, !haveSession);
    EnableWindow(pApp->hSubtitleBrowse, !haveSession);
    EnableWindow(pApp->hGain, !haveSession || pApp->originalSource[0]);
    SetWindowTextW(pApp->hPlayPause, playing ? L"Pause" : L"Play");
}

/* Rewrites the slider value captions from the current transcoder config. */
static void UpdateConfigLabels(App* pApp)
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
static void ReadConfigFromSliders(App* pApp)
{
    int width;
    int qp;

    width = (int)SendMessageW(pApp->hResSlider, TBM_GETPOS, 0, 0);
    width = ((width + 8) / 16) * 16; /* proxy width is a multiple of 16 */
    if (width < 240)
    {
        width = 240;
    }
    pApp->transcode.width       = (unsigned)width;
    qp                          = (int)SendMessageW(pApp->hQpSlider, TBM_GETPOS, 0, 0);
    pApp->transcode.qpInter     = qp;
    pApp->transcode.qpIntra     = (qp - 4 > 1) ? qp - 4 : 1; /* keyframes a little sharper */
    pApp->transcode.gopLength   = (unsigned)SendMessageW(pApp->hGopSlider, TBM_GETPOS, 0, 0);
    pApp->transcode.motionStrength = (int)SendMessageW(pApp->hMotionSlider, TBM_GETPOS, 0, 0);
    pApp->transcode.motionAdaptive = (BST_CHECKED == SendMessageW(pApp->hMotionCheck, BM_GETCHECK, 0, 0)) ? 1 : 0;
    EnableWindow(pApp->hMotionSlider, pApp->transcode.motionAdaptive);
    UpdateConfigLabels(pApp);
}

static void CreateControls(App* pApp)
{
    LVCOLUMNW column;

    pApp->hListLabel   = CreateChild(pApp, L"STATIC", L"Cast receivers on this network", SS_LEFT, 0, ID_LIST_LABEL);
    pApp->hRefresh     = CreateChild(pApp, L"BUTTON", L"Refresh", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_REFRESH);
    pApp->hWindowsCast = CreateChild(pApp, L"BUTTON", L"Windows Cast...", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_WINDOWS_CAST);
    pApp->hList        = CreateChild(pApp, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_LIST);
    pApp->hFileLabel   = CreateChild(pApp, L"STATIC", L"Video", SS_LEFT, 0, ID_FILE_LABEL);
    pApp->hFile        = CreateChild(pApp, L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_FILE);
    pApp->hBrowse      = CreateChild(pApp, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_BROWSE);
    pApp->hSubtitleLabel = CreateChild(pApp, L"STATIC", L"Subtitles", SS_LEFT, 0, ID_SUBTITLE_LABEL);
    pApp->hSubtitle = CreateChild(pApp, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP, 0, ID_SUBTITLE);
    SetWindowSubclass(pApp->hSubtitle, DarkComboProc, 1, (DWORD_PTR)pApp);
    pApp->hSubtitleBrowse = CreateChild(pApp, L"BUTTON", L"Browse...", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_SUBTITLE_BROWSE);
    SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)L"Automatic");
    SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)L"Off");
    SendMessageW(pApp->hSubtitle, CB_SETCURSEL, 0, 0);
    pApp->hGainLabel = CreateChild(pApp, L"STATIC", L"Audio gain 100%", SS_LEFT, 0, ID_GAIN_LABEL);
    pApp->hGain = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_GAIN);
    SendMessageW(pApp->hGain, TBM_SETRANGE, TRUE, MAKELPARAM(0, 400));
    SendMessageW(pApp->hGain, TBM_SETPOS, TRUE, 100);
    SendMessageW(pApp->hGain, TBM_SETPAGESIZE, 0, 25);
    pApp->hGainState = CreateChild(pApp, L"STATIC", L"Original audio", SS_RIGHT | SS_ENDELLIPSIS, 0, ID_GAIN_STATE);
    pApp->hCast        = CreateChild(pApp, L"BUTTON", L"Cast", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, ID_CAST);
    pApp->hHint        = CreateChild(pApp, L"STATIC", L"A file is served to the receiver over HTTP from this PC; an http(s) URL is sent as is.", SS_LEFT | SS_ENDELLIPSIS, 0, ID_HINT);
    pApp->hPlayPause   = CreateChild(pApp, L"BUTTON", L"Play", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_PLAY_PAUSE);
    pApp->hStop        = CreateChild(pApp, L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_STOP);
    pApp->hSeek        = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | TBS_BOTH | WS_TABSTOP, 0, ID_SEEK);
    pApp->hTime        = CreateChild(pApp, L"STATIC", L"", SS_RIGHT, 0, ID_TIME);
    pApp->hStatus      = CreateChild(pApp, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, ID_STATUS);
    ListView_SetBkColor(pApp->hList, DARK_FIELD);
    ListView_SetTextBkColor(pApp->hList, DARK_FIELD);
    ListView_SetTextColor(pApp->hList, DARK_TEXT);

    /* Transcoder (proxy) configuration panel: sliders whose settings drive the GPU proxy encode. */
    GpuTranscode_DefaultConfig(&pApp->transcode);
    pApp->hCfgLabel     = CreateChild(pApp, L"STATIC", L"Proxy transcoder \x2014 adjust, then Preview to test quality on the receiver", SS_LEFT | SS_ENDELLIPSIS, 0, ID_CFG_LABEL);
    pApp->hResValue     = CreateChild(pApp, L"STATIC", L"", SS_LEFT, 0, ID_RES_VALUE);
    pApp->hResSlider    = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_RES_SLIDER);
    pApp->hQpValue      = CreateChild(pApp, L"STATIC", L"", SS_LEFT, 0, ID_QP_VALUE);
    pApp->hQpSlider     = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_QP_SLIDER);
    pApp->hGopValue     = CreateChild(pApp, L"STATIC", L"", SS_LEFT, 0, ID_GOP_VALUE);
    pApp->hGopSlider    = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_GOP_SLIDER);
    pApp->hMotionCheck  = CreateChild(pApp, L"BUTTON", L"Optical flow", BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_MOTION_CHECK);
    pApp->hMotionSlider = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_MOTION_SLIDER);
    pApp->hMotionValue  = CreateChild(pApp, L"STATIC", L"", SS_LEFT, 0, ID_MOTION_VALUE);
    pApp->hPreview      = CreateChild(pApp, L"BUTTON", L"Preview", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_PREVIEW);
    pApp->hCfgReadout   = CreateChild(pApp, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, ID_CFG_READOUT);

    SendMessageW(pApp->hResSlider, TBM_SETRANGE, TRUE, MAKELPARAM(320, 1920));
    SendMessageW(pApp->hResSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.width);
    SendMessageW(pApp->hQpSlider, TBM_SETRANGE, TRUE, MAKELPARAM(18, 40));
    SendMessageW(pApp->hQpSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.qpInter);
    SendMessageW(pApp->hGopSlider, TBM_SETRANGE, TRUE, MAKELPARAM(6, 90));
    SendMessageW(pApp->hGopSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.gopLength);
    SendMessageW(pApp->hMotionSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessageW(pApp->hMotionSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.motionStrength);
    SendMessageW(pApp->hMotionCheck, BM_SETCHECK, pApp->transcode.motionAdaptive ? BST_CHECKED : BST_UNCHECKED, 0);

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
    SendMessageW(pApp->hSeek, TBM_SETRANGE, TRUE, MAKELPARAM(0, SEEK_RANGE));
    /* A click on the channel should jump to the cursor and scrub, not page toward it. */
    SetWindowSubclass(pApp->hSeek, SeekBarProc, 1, (DWORD_PTR)pApp);
    ApplyFont(pApp);
    Layout(pApp);
    UpdateConfigLabels(pApp);
    EnableWindow(pApp->hMotionSlider, pApp->transcode.motionAdaptive);
    UpdateControls(pApp);
}

/* ------------------------------------------------------------------ discovery */

static DWORD WINAPI DiscoveryThread(LPVOID pParameter)
{
    HWND             hWnd    = (HWND)pParameter;
    DiscoveryResult* pResult = (DiscoveryResult*)calloc(1, sizeof(*pResult));

    if (NULL != pResult)
    {
        pResult->count = Ssdp_DiscoverCastDevices(pResult->devices, CAST_MAX_DEVICES, DISCOVERY_MS);
    }
    PostMessageW(hWnd, WM_APP_DISCOVERED, 0, (LPARAM)pResult);
    return 0;
}

static void StartDiscovery(App* pApp)
{
    HANDLE hThread;

    if (pApp->discovering)
    {
        return;
    }
    pApp->discovering = 1;
    SetStatus(pApp, L"Searching for Cast receivers...");
    UpdateControls(pApp);
    hThread = CreateThread(NULL, 0, DiscoveryThread, pApp->hWnd, 0, NULL);
    if (NULL == hThread)
    {
        pApp->discovering = 0;
        SetStatus(pApp, L"Cannot start discovery.");
        UpdateControls(pApp);
        return;
    }
    CloseHandle(hThread);
}

static void OnDiscovered(App* pApp, DiscoveryResult* pResult)
{
    unsigned index;
    int      preferred = -1;
    WCHAR    text[160];

    pApp->discovering = 0;
    if (NULL != pResult)
    {
        pApp->devices = *pResult;
        free(pResult);
    }
    else
    {
        pApp->devices.count = 0;
    }
    ListView_DeleteAllItems(pApp->hList);
    for (index = 0; index < pApp->devices.count; ++index)
    {
        const CastDevice* pDevice = &pApp->devices.devices[index];
        LVITEMW           item;
        WCHAR*            pName    = DupUtf8ToWide(pDevice->friendlyName);
        WCHAR*            pAddress = DupUtf8ToWide(pDevice->host);
        WCHAR*            pModel   = DupUtf8ToWide(pDevice->model);

        ZeroMemory(&item, sizeof(item));
        item.mask    = LVIF_TEXT;
        item.iItem   = (int)index;
        item.pszText = pName ? pName : L"?";
        ListView_InsertItem(pApp->hList, &item);
        ListView_SetItemText(pApp->hList, (int)index, 1, pAddress ? pAddress : L"?");
        ListView_SetItemText(pApp->hList, (int)index, 2, pModel ? pModel : L"");
        if (preferred < 0 && ContainsNoCase(pDevice->friendlyName, PREFERRED_DEVICE))
        {
            preferred = (int)index;
        }
        free(pName);
        free(pAddress);
        free(pModel);
    }
    if (pApp->devices.count > 0)
    {
        const int select = (preferred >= 0) ? preferred : 0;

        ListView_SetItemState(pApp->hList, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"%u receiver%s found.", pApp->devices.count, 1 == pApp->devices.count ? L"" : L"s");
    }
    else
    {
        (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"No Cast receiver answered. Is the projector on and on this network?");
    }
    SetStatus(pApp, text);
    UpdateControls(pApp);
}

/* ------------------------------------------------------------------ the session */

/* Runs on the session thread: hand heap copies to the window thread. */
static void OnSessionEvent(void* pUser, CastEvent event, const CastStatus* pStatus, const char* pText)
{
    HWND hWnd = (HWND)pUser;

    switch (event)
    {
    case CAST_EVENT_LOG:
        PostMessageW(hWnd, WM_APP_LOG, 0, (LPARAM)DupUtf8ToWide(pText));
        break;
    case CAST_EVENT_STATUS:
    {
        CastStatus* pCopy = (CastStatus*)malloc(sizeof(*pCopy));

        if (NULL != pCopy)
        {
            *pCopy = *pStatus;
            PostMessageW(hWnd, WM_APP_STATUS, 0, (LPARAM)pCopy);
        }
        break;
    }
    case CAST_EVENT_ENDED:
        PostMessageW(hWnd, WM_APP_ENDED, 0, (LPARAM)(pText ? DupUtf8ToWide(pText) : NULL));
        break;
    }
}

static DWORD WINAPI SessionThread(LPVOID pParameter)
{
    (void)CastSession_Run((CastSession*)pParameter);
    return 0;
}

/* Starts a session that plays pSourceIn on the selected receiver. buildProxy adds the background
** scrub proxy (for normal casting); the quality preview passes 0 because the clip it plays is
** already the transcoded output. */
static void RefreshSubtitles(App* pApp)
{
    WCHAR source[1024], automatic[380];
    GetWindowTextW(pApp->hFile, source, 1024);
    if (!wcscmp(source, pApp->subtitleSource)) return;
    wcscpy_s(pApp->subtitleSource, 1024, source);
    pApp->subtitleCount = _wcsnicmp(source, L"http", 4) ? Subtitles_Find(source, pApp->subtitles) : 0;
    SendMessageW(pApp->hSubtitle, CB_RESETCONTENT, 0, 0);
    _snwprintf_s(automatic, 380, _TRUNCATE, L"Automatic - %ls", pApp->subtitleCount ? pApp->subtitles[0].label : L"no supported subtitles found");
    SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)automatic);
    SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)L"Off");
    for (unsigned i = 0; i < pApp->subtitleCount; ++i)
        SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)pApp->subtitles[i].label);
    SendMessageW(pApp->hSubtitle, CB_SETCURSEL, 0, 0);
}

static void BrowseSubtitles(App* pApp)
{
    OPENFILENAMEW dialog = {0};
    WCHAR path[MAX_PATH] = {0};
    RefreshSubtitles(pApp);
    dialog.lStructSize = sizeof(dialog); dialog.hwndOwner = pApp->hWnd;
    dialog.lpstrFilter = L"Subtitles\0*.srt;*.vtt\0";
    dialog.lpstrFile = path; dialog.nMaxFile = MAX_PATH;
    dialog.lpstrTitle = L"Choose subtitles";
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&dialog) && pApp->subtitleCount < SUBTITLE_MAX_TRACKS)
    {
        Subtitles_FileTrack(path, &pApp->subtitles[pApp->subtitleCount]);
        SendMessageW(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)pApp->subtitles[pApp->subtitleCount++].label);
        SendMessageW(pApp->hSubtitle, CB_SETCURSEL, pApp->subtitleCount + 1, 0);
    }
}

static DWORD WINAPI GainThread(LPVOID parameter)
{
    App* app = (App*)parameter;
    unsigned serial = app->gainSerial;
    app->gainOk = AudioGain_Convert(app->originalSource, app->gainOutput, app->gainPercent,
                                    &app->gainCancel, &app->gainProgress, app->gainError, sizeof(app->gainError));
    PostMessageW(app->hWnd, WM_APP_GAIN_DONE, serial, 0);
    return 0;
}

static void CancelGain(App* app)
{
    KillTimer(app->hWnd, GAIN_TIMER);
    ++app->gainSerial;
    if (app->hGainThread)
    {
        InterlockedExchange(&app->gainCancel, 1);
        WaitForSingleObject(app->hGainThread, INFINITE);
        CloseHandle(app->hGainThread); app->hGainThread = NULL;
    }
    if (app->gainOutput[0]) { DeleteFileW(app->gainOutput); app->gainOutput[0] = 0; }
}

static void ScheduleGain(App* app)
{
    unsigned percent = (unsigned)SendMessageW(app->hGain, TBM_GETPOS, 0, 0);
    WCHAR directory[MAX_PATH];
    DWORD length;
    CancelGain(app);
    if (!app->pSession || !app->originalSource[0]) return;
    if (percent == 100)
    {
        CastSession_ReplaceMedia(app->pSession, app->originalUrl, app->originalType);
        SetWindowTextW(app->hGainState, L"Original audio");
        return;
    }
    length = GetTempPathW(MAX_PATH, directory);
    if (!length || length >= MAX_PATH || _snwprintf_s(app->gainOutput, MAX_PATH, _TRUNCATE,
        L"%lscvkcast_gain_%lu_%u.mp4", directory, GetCurrentProcessId(), app->gainSerial) < 0)
    {
        SetWindowTextW(app->hGainState, L"Cannot create gain file"); return;
    }
    app->gainPercent = percent;
    InterlockedExchange(&app->gainCancel, 0);
    InterlockedExchange(&app->gainProgress, 0);
    app->hGainThread = CreateThread(NULL, 0, GainThread, app, 0, NULL);
    SetWindowTextW(app->hGainState, app->hGainThread ? L"Preparing gain..." : L"Cannot start audio worker");
}

static void FinishGain(App* app, unsigned serial)
{
    GainVariant* variant;
    char local[64], error[256], url[1200];
    if (serial != app->gainSerial || !app->hGainThread) return;
    WaitForSingleObject(app->hGainThread, INFINITE);
    CloseHandle(app->hGainThread); app->hGainThread = NULL;
    if (!app->pSession || !app->gainOk)
    {
        WCHAR* message = DupUtf8ToWide(app->gainError);
        SetWindowTextW(app->hGainState, L"Gain failed - audio unchanged");
        if (message) { SetStatus(app, message); free(message); }
        CancelGain(app); return;
    }
    variant = (GainVariant*)calloc(1, sizeof(*variant));
    /* Use the same reachable local address selected for the original file. */
    if (!variant || sscanf(app->originalUrl, "http://%63[^:]", local) != 1)
    { free(variant); CancelGain(app); SetWindowTextW(app->hGainState, L"Cannot serve audio gain"); return; }
    variant->server = MediaServer_Start(app->gainOutput, NULL, 0, "video/mp4", error, sizeof(error));
    if (!variant->server)
    { free(variant); CancelGain(app); SetWindowTextW(app->hGainState, L"Cannot serve audio gain"); return; }
    wcscpy_s(variant->path, MAX_PATH, app->gainOutput); app->gainOutput[0] = 0;
    variant->next = app->gainVariants; app->gainVariants = variant;
    snprintf(url, sizeof(url), "http://%s:%u/gain.mp4", local, (unsigned)MediaServer_GetPort(variant->server));
    CastSession_ReplaceMedia(app->pSession, url, "video/mp4");
    SetWindowTextW(app->hGainState, L"Gain ready - switching");
}

static void StartCastSource(App* pApp, const WCHAR* pSourceIn, int buildProxy)
{
    const int         selected = ListView_GetNextItem(pApp->hList, -1, LVNI_SELECTED);
    const CastDevice* pDevice;
    CastSessionConfig config;
    WCHAR             source[1024];
    char              error[256];
    char              localAddress[64];
    char*             vtt = NULL;
    size_t            vttSize = 0;
    const SubtitleTrack* subtitle = NULL;

    if (NULL != pApp->pSession || selected < 0 || (unsigned)selected >= pApp->devices.count)
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
    config.pUser    = pApp->hWnd;
    if (buildProxy)
    {
        int choice;
        RefreshSubtitles(pApp);
        choice = (int)SendMessageW(pApp->hSubtitle, CB_GETCURSEL, 0, 0);
        if (choice == 0 && pApp->subtitleCount) subtitle = &pApp->subtitles[0];
        else if (choice >= 2 && (unsigned)(choice - 2) < pApp->subtitleCount) subtitle = &pApp->subtitles[choice - 2];
        if (subtitle)
        {
            vtt = Subtitles_Load(subtitle, &vttSize, error, sizeof(error));
            if (!vtt)
            {
                WCHAR* message = DupUtf8ToWide(error);
                SetStatus(pApp, message ? message : L"Cannot read subtitles."); free(message); return;
            }
        }
    }
    pApp->originalSource[0] = 0;
    if (0 == _wcsnicmp(source, L"http://", 7) || 0 == _wcsnicmp(source, L"https://", 8))
    {
        const wchar_t* pExtension = wcsrchr(source, L'.');

        ToUtf8(source, config.contentUrl, sizeof(config.contentUrl));
        ToUtf8(source, config.title, sizeof(config.title));
        (void)snprintf(config.contentType, sizeof(config.contentType), "%s", (NULL != pExtension && NULL == wcschr(pExtension, L'/')) ? MediaServer_ContentTypeForExtension(pExtension) : "video/mp4");
    }
    else
    {
        const wchar_t* pName      = wcsrchr(source, L'\\');
        const wchar_t* pExtension = wcsrchr(source, L'.');
        char           extension[16];

        pName = (NULL != pName) ? pName + 1 : source;
        if (NULL != pExtension && pExtension < pName)
        {
            pExtension = NULL;
        }
        ToUtf8(pName, config.title, sizeof(config.title));
        ToUtf8(NULL != pExtension ? pExtension : L"", extension, sizeof(extension));
        (void)snprintf(config.contentType, sizeof(config.contentType), "%s", MediaServer_ContentTypeForExtension(pExtension));
        config.servingLocally = 1;
        if (!MediaServer_LocalAddressToward(pDevice->host, localAddress, sizeof(localAddress)))
        {
            SetStatus(pApp, L"Cannot determine the local address facing the receiver.");
            free(vtt);
            return;
        }
        pApp->pServer = MediaServer_StartWithSubtitles(source, NULL, DEFAULT_SERVER_PORT, config.contentType, vtt, vttSize, error, sizeof(error));
        if (NULL == pApp->pServer)
        {
            WCHAR* pError = DupUtf8ToWide(error);

            SetStatus(pApp, pError ? pError : L"Cannot start the media server.");
            free(pError);
            free(vtt);
            return;
        }
        (void)snprintf(config.contentUrl, sizeof(config.contentUrl), "http://%s:%u/media%s", localAddress, (unsigned)MediaServer_GetPort(pApp->pServer), extension);
        /* Scrub previews snap to keyframes so the receiver paints them after a one-frame decode. */
        free(pApp->pKeyframes);
        pApp->pKeyframes = (double*)malloc(MP4_INDEX_MAX_KEYFRAMES * sizeof(double));
        if (NULL != pApp->pKeyframes)
        {
            config.keyframeCount = Mp4Index_ReadKeyframes(source, pApp->pKeyframes, MP4_INDEX_MAX_KEYFRAMES);
            config.pKeyframes    = pApp->pKeyframes;
        }
        wcscpy_s(pApp->proxySource, MAX_PATH, source);
        (void)snprintf(pApp->proxyLocal, sizeof(pApp->proxyLocal), "%s", localAddress);
        pApp->proxyPort = (unsigned short)(MediaServer_GetPort(pApp->pServer) + 1);
        wcscpy_s(pApp->originalSource, MAX_PATH, source);
    }
    if (vtt && !pApp->pServer)
    {
        if (MediaServer_LocalAddressToward(pDevice->host, localAddress, sizeof(localAddress)))
            pApp->pServer = MediaServer_StartWithSubtitles(NULL, NULL, DEFAULT_SERVER_PORT, "video/mp4", vtt, vttSize, error, sizeof(error));
        if (!pApp->pServer) { free(vtt); SetStatus(pApp, L"Cannot serve subtitles to the receiver."); return; }
    }
    if (vtt)
    {
        snprintf(config.subtitleUrl, sizeof(config.subtitleUrl), "http://%s:%u/subtitles.vtt", localAddress, (unsigned)MediaServer_GetPort(pApp->pServer));
        snprintf(config.subtitleLanguage, sizeof(config.subtitleLanguage), "%s", subtitle->language);
    }
    free(vtt);
    snprintf(pApp->originalUrl, sizeof(pApp->originalUrl), "%s", config.contentUrl);
    snprintf(pApp->originalType, sizeof(pApp->originalType), "%s", config.contentType);
    pApp->pSession = CastSession_Create(&config);
    if (NULL != pApp->pSession)
    {
        pApp->hSessionThread = CreateThread(NULL, 0, SessionThread, pApp->pSession, 0, NULL);
    }
    if (NULL == pApp->hSessionThread)
    {
        CastSession_Destroy(pApp->pSession);
        pApp->pSession = NULL;
        MediaServer_Stop(pApp->pServer);
        pApp->pServer = NULL;
        SetStatus(pApp, L"Cannot start the Cast session.");
        return;
    }
    if (buildProxy && pApp->proxyLocal[0])
    {
        pApp->pProxy = Proxy_Start(pApp->proxySource, &pApp->transcode, pApp->proxyLocal, pApp->proxyPort, pApp->pSession);
    }
    else
    {
        pApp->proxyLocal[0] = 0;
    }
    ZeroMemory(&pApp->status, sizeof(pApp->status));
    pApp->stopping = 0;
    UpdateControls(pApp);
    if (buildProxy && SendMessageW(pApp->hGain, TBM_GETPOS, 0, 0) != 100) ScheduleGain(pApp);
}

/* The Cast button: play the chosen file with the background scrub proxy. */
static void StartCast(App* pApp)
{
    WCHAR source[1024];

    GetWindowTextW(pApp->hFile, source, (int)(sizeof(source) / sizeof(source[0])));
    StartCastSource(pApp, source, 1);
}

/* A short transcode of the source at the current settings, run on a worker thread so the sliders
** stay responsive. On completion it posts the App's PreviewResult to the window thread. */
static DWORD WINAPI PreviewThread(LPVOID pParameter)
{
    PreviewJob*    pJob    = (PreviewJob*)pParameter;
    PreviewResult* pResult = pJob->pResult;
    char           report[1800];
    const char*    pMarker;

    ZeroMemory(pResult, sizeof(*pResult));
    wcscpy_s(pResult->path, MAX_PATH, pJob->dest);
    pResult->ok = GpuTranscode_ToMp4(pJob->source, pJob->dest, &pJob->config, pJob->startFrame, pJob->frames, pJob->pCancel, report, sizeof(report));
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
            const char* pDigits = pMarker;

            while (pDigits > report && (pDigits[-1] < '0' || pDigits[-1] > '9'))
            {
                --pDigits;
            }
            while (pDigits > report && pDigits[-1] >= '0' && pDigits[-1] <= '9')
            {
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
        hFile                  = CreateFileW(pJob->dest, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (INVALID_HANDLE_VALUE != hFile)
        {
            double seconds;

            GetFileSizeEx(hFile, &size);
            CloseHandle(hFile);
            seconds       = (produced > 0) ? (double)produced / 24.0 : 1.0; /* nominal frame rate for the readout */
            pResult->kbps = (unsigned)((double)size.QuadPart * 8.0 / 1000.0 / (seconds > 0.1 ? seconds : 0.1));
        }
    }
    PostMessageW(pJob->hWnd, WM_APP_PREVIEW_DONE, 0, (LPARAM)pResult);
    return 0;
}

static void StartPreview(App* pApp)
{
    PreviewJob* pJob;
    WCHAR       source[MAX_PATH];
    WCHAR       temp[MAX_PATH];
    DWORD       length;
    double      ratio;
    int         position;

    if (pApp->previewBusy)
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
    position         = (int)SendMessageW(pApp->hSeek, TBM_GETPOS, 0, 0);
    ratio            = (pApp->status.duration > 0 && position > 0) ? (double)position / SEEK_RANGE : 0.25;
    pJob->startFrame = (unsigned)(ratio * 24.0 * (pApp->status.duration > 0 ? pApp->status.duration : 600.0));
    pJob->frames     = 8u * 24u; /* about eight seconds at ~24 fps */

    pApp->previewBusy = 1;
    EnableWindow(pApp->hPreview, FALSE);
    SetWindowTextW(pApp->hCfgReadout, L"Building preview\x2026");
    if (NULL != pApp->hPreviewThread)
    {
        CloseHandle(pApp->hPreviewThread);
        pApp->hPreviewThread = NULL;
    }
    pApp->hPreviewThread = CreateThread(NULL, 0, PreviewThread, pJob, 0, NULL);
    if (NULL == pApp->hPreviewThread)
    {
        pApp->previewBusy = 0;
        EnableWindow(pApp->hPreview, TRUE);
    }
}

static void StopCast(App* pApp)
{
    if (NULL != pApp->pSession && !pApp->stopping)
    {
        pApp->stopping = 1;
        CastSession_Post(pApp->pSession, CAST_COMMAND_STOP, 0);
        UpdateControls(pApp);
    }
}

/* Moves the slider and the readout to a position, unless the user holds the slider. */
static void ShowPosition(App* pApp, double seconds)
{
    WCHAR position[32];
    WCHAR duration[32];
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
        SendMessageW(pApp->hSeek, TBM_SETPOS, TRUE, (LPARAM)(LONG)(seconds / pApp->status.duration * SEEK_RANGE + 0.5));
    }
    else
    {
        SendMessageW(pApp->hSeek, TBM_SETPOS, TRUE, 0);
    }
    if (pApp->status.hasMedia)
    {
        FormatTime(seconds, position, sizeof(position) / sizeof(position[0]));
        FormatTime(pApp->status.duration, duration, sizeof(duration) / sizeof(duration[0]));
        (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"%hs  %ls / %ls", pApp->status.stateName, position, duration);
    }
    else
    {
        (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"%hs", pApp->status.stateName);
    }
    SetWindowTextW(pApp->hTime, text);
}

static void OnStatus(App* pApp, CastStatus* pStatus)
{
    if (NULL == pStatus)
    {
        return;
    }
    pApp->status     = *pStatus;
    pApp->statusTick = GetTickCount64();
    free(pStatus);
    ShowPosition(pApp, pApp->status.currentTime);
    UpdateControls(pApp);
}

/* Between the receiver's reports, advance the display at playback rate. */
static void OnPositionTimer(App* pApp)
{
    if (NULL != pApp->pSession && CAST_STATE_PLAYING == pApp->status.state && pApp->status.hasMedia && !pApp->seeking)
    {
        ShowPosition(pApp, pApp->status.currentTime + (double)(GetTickCount64() - pApp->statusTick) / 1000.0);
    }
}

/* Reaps the session thread; safe to call when no session exists. */
static void FinishSession(App* pApp, const WCHAR* pReason)
{
    CancelGain(pApp);
    if (NULL != pApp->hSessionThread)
    {
        WaitForSingleObject(pApp->hSessionThread, 3000);
        CloseHandle(pApp->hSessionThread);
        pApp->hSessionThread = NULL;
    }
    Proxy_Stop(pApp->pProxy);
    pApp->pProxy        = NULL;
    pApp->proxyLocal[0] = 0;
    CastSession_Destroy(pApp->pSession);
    pApp->pSession = NULL;
    MediaServer_Stop(pApp->pServer);
    pApp->pServer = NULL;
    while (pApp->gainVariants)
    {
        GainVariant* variant = pApp->gainVariants;
        pApp->gainVariants = variant->next;
        MediaServer_Stop(variant->server);
        DeleteFileW(variant->path);
        free(variant);
    }
    pApp->originalSource[0] = 0;
    free(pApp->pKeyframes);
    pApp->pKeyframes = NULL;
    pApp->stopping   = 0;
    ZeroMemory(&pApp->status, sizeof(pApp->status));
    SendMessageW(pApp->hSeek, TBM_SETPOS, TRUE, 0);
    SetWindowTextW(pApp->hTime, L"");
    if (NULL != pReason)
    {
        SetStatus(pApp, pReason);
    }
    UpdateControls(pApp);
}

/* Moves to a fraction of the duration. final: the drag ended, so seek exactly and resume;
** otherwise a live scrub preview (paused, keyframe-snapped in the session). */
static void SeekToRatio(App* pApp, double ratio, int final)
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
    SendMessageW(pApp->hSeek, TBM_SETPOS, TRUE, (LPARAM)(LONG)(ratio * SEEK_RANGE + 0.5));
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
static double RatioFromClick(HWND hSeek, LPARAM lParam)
{
    RECT      channel;
    const int x = GET_X_LPARAM(lParam);
    int       span;

    SendMessageW(hSeek, TBM_GETCHANNELRECT, 0, (LPARAM)&channel);
    span = channel.right - channel.left;
    return (span > 0) ? (double)(x - channel.left) / span : 0.0;
}

/* Subclass: a channel click jumps to the cursor and scrubs; release seeks there exactly. */
static LRESULT CALLBACK SeekBarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData)
{
    App* pApp = (App*)refData;

    switch (message)
    {
    case WM_LBUTTONDOWN:
        SetFocus(hWnd);
        SetCapture(hWnd);
        SeekToRatio(pApp, RatioFromClick(hWnd, lParam), 0);
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == hWnd && (MK_LBUTTON & wParam))
        {
            SeekToRatio(pApp, RatioFromClick(hWnd, lParam), 0);
        }
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == hWnd)
        {
            ReleaseCapture();
            SeekToRatio(pApp, RatioFromClick(hWnd, lParam), 1);
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
static void OnSeekScroll(App* pApp, int code)
{
    const int position = (int)SendMessageW(pApp->hSeek, TBM_GETPOS, 0, 0);

    if (NULL == pApp->pSession || pApp->status.duration <= 0 || TB_THUMBTRACK == code)
    {
        return; /* mouse tracking is handled by the subclass */
    }
    CastSession_Post(pApp->pSession, CAST_COMMAND_SEEK, pApp->status.duration * position / SEEK_RANGE);
}

static void Browse(App* pApp)
{
    OPENFILENAMEW dialog;
    WCHAR         file[MAX_PATH];

    file[0] = 0;
    GetWindowTextW(pApp->hFile, file, MAX_PATH);
    if (0 == _wcsnicmp(file, L"http", 4))
    {
        file[0] = 0;
    }
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner   = pApp->hWnd;
    dialog.lpstrFilter = L"Video files\0*.mp4;*.m4v;*.mov;*.webm;*.mkv\0Audio files\0*.mp3;*.m4a;*.aac;*.flac;*.wav;*.ogg\0All files\0*.*\0";
    dialog.lpstrFile   = file;
    dialog.nMaxFile    = MAX_PATH;
    dialog.lpstrTitle  = L"Choose a video to cast";
    dialog.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&dialog))
    {
        SetWindowTextW(pApp->hFile, file);
        UpdateControls(pApp);
    }
}

/* Windows' own flyout for wireless (Miracast) displays: the path for a projector that is a
** Windows display rather than a Cast receiver. */
static void OpenWindowsCast(App* pApp)
{
    ShellExecuteW(pApp->hWnd, L"open", L"ms-settings-connectabledevices:devicediscovery", NULL, NULL, SW_SHOWNORMAL);
}

/* ------------------------------------------------------------------ the window */

static LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    App* pApp = &g_app;

    switch (message)
    {
    case WM_CREATE:
    {
        const BOOL dark = TRUE;
        pApp->hWnd = hWnd;
        pApp->dpi  = GetDpiForWindow(hWnd);
        pApp->backgroundBrush = CreateSolidBrush(DARK_BACKGROUND);
        pApp->fieldBrush = CreateSolidBrush(DARK_FIELD);
        DwmSetWindowAttribute(hWnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        CreateControls(pApp);
        StartDiscovery(pApp);
        SetTimer(hWnd, POSITION_TIMER, POSITION_TICK_MS, NULL);
        return 0;
    }
    case WM_ERASEBKGND:
    {
        RECT rect; GetClientRect(hWnd, &rect);
        FillRect((HDC)wParam, &rect, pApp->backgroundBrush);
        return 1;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
        int field = message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX || (HWND)lParam == pApp->hSubtitle;
        SetTextColor((HDC)wParam, IsWindowEnabled((HWND)lParam) ? DARK_TEXT : DARK_MUTED);
        SetBkColor((HDC)wParam, field ? DARK_FIELD : DARK_BACKGROUND);
        return (LRESULT)(field ? pApp->fieldBrush : pApp->backgroundBrush);
    }
    case WM_MEASUREITEM:
    {
        MEASUREITEMSTRUCT* item = (MEASUREITEMSTRUCT*)lParam;
        if (item->CtlType == ODT_COMBOBOX && item->CtlID == ID_SUBTITLE)
        {
            item->itemHeight = (UINT)Scale(pApp, 22);
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM:
    {
        const DRAWITEMSTRUCT* item = (const DRAWITEMSTRUCT*)lParam;
        WCHAR label[128];
        HBRUSH fill;
        if (item->CtlType == ODT_COMBOBOX && item->CtlID == ID_SUBTITLE)
        {
            PaintSubtitleItem(item->hwndItem, item->hDC, item->rcItem,
                              item->itemID == (UINT)-1 ? -1 : (int)item->itemID, item->itemState, pApp);
            return TRUE;
        }
        if (item->CtlType != ODT_BUTTON) break;
        fill = CreateSolidBrush((item->itemState & ODS_SELECTED) ? RGB(48, 91, 146) : DARK_FIELD);
        FillRect(item->hDC, &item->rcItem, fill); DeleteObject(fill);
        SetBkMode(item->hDC, TRANSPARENT);
        SetTextColor(item->hDC, (item->itemState & ODS_DISABLED) ? DARK_MUTED : DARK_TEXT);
        GetWindowTextW(item->hwndItem, label, 128);
        {
            RECT rect = item->rcItem;
            DrawTextW(item->hDC, label, -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            if (item->itemState & ODS_FOCUS) { InflateRect(&rect, -3, -3); DrawFocusRect(item->hDC, &rect); }
        }
        return TRUE;
    }
    case WM_TIMER:
        if (POSITION_TIMER == wParam)
        {
            OnPositionTimer(pApp);
            if (pApp->hGainThread)
            {
                WCHAR progress[100];
                _snwprintf_s(progress, 100, _TRUNCATE, L"Preparing %u%% gain: %ld%%", pApp->gainPercent, InterlockedCompareExchange(&pApp->gainProgress, 0, 0));
                SetWindowTextW(pApp->hGainState, progress);
            }
        }
        else if (wParam == SUBTITLE_TIMER) { KillTimer(hWnd, SUBTITLE_TIMER); if (!pApp->pSession) RefreshSubtitles(pApp); }
        else if (wParam == GAIN_TIMER) { KillTimer(hWnd, GAIN_TIMER); ScheduleGain(pApp); }
        return 0;
    case WM_SIZE:
        Layout(pApp);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = Scale(pApp, 620);
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = Scale(pApp, 584);
        return 0;
    case WM_DPICHANGED:
    {
        const RECT* pSuggested = (const RECT*)lParam;

        pApp->dpi = HIWORD(wParam);
        SetWindowPos(hWnd, NULL, pSuggested->left, pSuggested->top, pSuggested->right - pSuggested->left, pSuggested->bottom - pSuggested->top, SWP_NOZORDER | SWP_NOACTIVATE);
        ApplyFont(pApp);
        Layout(pApp);
        return 0;
    }
    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case ID_REFRESH:
            StartDiscovery(pApp);
            return 0;
        case ID_WINDOWS_CAST:
            OpenWindowsCast(pApp);
            return 0;
        case ID_BROWSE:
            Browse(pApp);
            return 0;
        case ID_SUBTITLE_BROWSE:
            BrowseSubtitles(pApp);
            return 0;
        case ID_SUBTITLE:
            InvalidateRect(pApp->hSubtitle, NULL, FALSE);
            return 0;
        case ID_CAST:
            StartCast(pApp);
            return 0;
        case ID_PLAY_PAUSE:
            if (NULL != pApp->pSession)
            {
                CastSession_Post(pApp->pSession, CAST_COMMAND_TOGGLE, 0);
            }
            return 0;
        case ID_STOP:
            StopCast(pApp);
            return 0;
        case ID_FILE:
            if (EN_CHANGE == HIWORD(wParam))
            {
                UpdateControls(pApp);
                SetTimer(hWnd, SUBTITLE_TIMER, 350, NULL);
            }
            return 0;
        case ID_MOTION_CHECK:
            ReadConfigFromSliders(pApp);
            return 0;
        case ID_PREVIEW:
            StartPreview(pApp);
            return 0;
        default:
            break;
        }
        break;
    case WM_NOTIFY:
    {
        const NMHDR* pHeader = (const NMHDR*)lParam;

        if (NM_CUSTOMDRAW == pHeader->code)
        {
            NMCUSTOMDRAW* draw = (NMCUSTOMDRAW*)lParam;
            int slider = pHeader->hwndFrom == pApp->hGain || pHeader->hwndFrom == pApp->hSeek || pHeader->hwndFrom == pApp->hResSlider ||
                         pHeader->hwndFrom == pApp->hQpSlider || pHeader->hwndFrom == pApp->hGopSlider || pHeader->hwndFrom == pApp->hMotionSlider;
            if (slider)
            {
                if (draw->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
                if (draw->dwDrawStage == CDDS_ITEMPREPAINT)
                {
                    HBRUSH brush = CreateSolidBrush(draw->dwItemSpec == TBCD_THUMB ? (IsWindowEnabled(pHeader->hwndFrom) ? DARK_ACCENT : DARK_MUTED) : DARK_FIELD);
                    FillRect(draw->hdc, &draw->rc, brush); DeleteObject(brush);
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
        return 0;
    }
    case WM_HSCROLL:
        if ((HWND)lParam == pApp->hGain)
        {
            WCHAR text[64];
            unsigned percent = (unsigned)SendMessageW(pApp->hGain, TBM_GETPOS, 0, 0);
            _snwprintf_s(text, 64, _TRUNCATE, L"Audio gain %u%%", percent);
            SetWindowTextW(pApp->hGainLabel, text);
            if (pApp->pSession) SetTimer(hWnd, GAIN_TIMER, 500, NULL);
            else SetWindowTextW(pApp->hGainState, percent == 100 ? L"Original audio" : L"Applies during playback");
        }
        else if ((HWND)lParam == pApp->hSeek)
        {
            OnSeekScroll(pApp, LOWORD(wParam));
        }
        else if ((HWND)lParam == pApp->hResSlider || (HWND)lParam == pApp->hQpSlider || (HWND)lParam == pApp->hGopSlider || (HWND)lParam == pApp->hMotionSlider)
        {
            ReadConfigFromSliders(pApp);
        }
        return 0;
    case WM_APP_DISCOVERED:
        OnDiscovered(pApp, (DiscoveryResult*)lParam);
        return 0;
    case WM_APP_LOG:
    {
        WCHAR* pText = (WCHAR*)lParam;

        if (NULL != pText)
        {
            SetStatus(pApp, pText);
            free(pText);
        }
        return 0;
    }
    case WM_APP_STATUS:
        OnStatus(pApp, (CastStatus*)lParam);
        return 0;
    case WM_APP_ENDED:
    {
        WCHAR* pReason = (WCHAR*)lParam;

        FinishSession(pApp, pReason ? pReason : L"Session ended.");
        free(pReason);
        return 0;
    }
    case WM_APP_PREVIEW_DONE:
    {
        PreviewResult* pResult = (PreviewResult*)lParam;

        pApp->previewBusy = 0;
        EnableWindow(pApp->hPreview, TRUE);
        if (NULL != pResult && pResult->ok && pResult->frames > 0)
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
        return 0;
    }
    case WM_APP_GAIN_DONE:
        FinishGain(pApp, (unsigned)wParam);
        return 0;
    case WM_CLOSE:
        CancelGain(pApp);
        if (NULL != pApp->hPreviewThread)
        {
            InterlockedExchange(&pApp->previewCancel, 1);
            WaitForSingleObject(pApp->hPreviewThread, INFINITE);
            CloseHandle(pApp->hPreviewThread);
            pApp->hPreviewThread = NULL;
        }
        if (NULL != pApp->pSession)
        {
            CastSession_Post(pApp->pSession, CAST_COMMAND_STOP, 0);
            FinishSession(pApp, NULL);
        }
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        DeleteObject(pApp->backgroundBrush);
        DeleteObject(pApp->fieldBrush);
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hWnd, message, wParam, lParam);
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    static const WCHAR   kClass[] = L"cvkCastDemo";
    WNDCLASSEXW          wc;
    INITCOMMONCONTROLSEX controls;
    WSADATA              wsaData;
    HWND                 hWnd;
    MSG                  msg;
    int                  argc = 0;
    LPWSTR*              argv;

    (void)hPrevInstance;
    (void)lpCmdLine;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    /* The common file dialog is shell-backed and needs this thread in a single-threaded OLE
    ** apartment; without it GetOpenFileName fails. This does not depend on how the app started. */
    OleInitialize(NULL);
    if (0 != WSAStartup(MAKEWORD(2, 2), &wsaData))
    {
        MessageBoxW(NULL, L"WSAStartup failed.", L"CastDemo", MB_ICONERROR);
        return 1;
    }
    controls.dwSize = sizeof(controls);
    controls.dwICC  = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WindowProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = kClass;
    wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
    if (0 == RegisterClassExW(&wc))
    {
        WSACleanup();
        return 1;
    }
    g_app.hInstance = hInstance;
    g_app.dpi       = GetDpiForSystem();
    hWnd            = CreateWindowExW(0, kClass, L"CastDemo", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, Scale(&g_app, 720), Scale(&g_app, 700), NULL, NULL, hInstance, NULL);
    if (NULL == hWnd)
    {
        WSACleanup();
        return 1;
    }
    argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (NULL != argv)
    {
        if (argc > 1)
        {
            SetWindowTextW(g_app.hFile, argv[1]);
            UpdateControls(&g_app);
        }
        LocalFree(argv);
    }
    ShowWindow(hWnd, nShowCmd);
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (!IsDialogMessageW(hWnd, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (NULL != g_app.hFont)
    {
        DeleteObject(g_app.hFont);
    }
    WSACleanup();
    OleUninitialize();
    return (int)msg.wParam;
}
