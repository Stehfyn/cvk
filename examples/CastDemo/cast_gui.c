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

#if defined(_MSC_VER)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#define WM_APP_DISCOVERED   (WM_APP + 1) /* lParam: DiscoveryResult* on the heap */
#define WM_APP_LOG          (WM_APP + 2) /* lParam: WCHAR* on the heap */
#define WM_APP_STATUS       (WM_APP + 3) /* lParam: CastStatus* on the heap */
#define WM_APP_ENDED        (WM_APP + 4) /* lParam: WCHAR* on the heap, or NULL */
#define WM_APP_PREVIEW_DONE (WM_APP + 5) /* lParam: PreviewResult* on the heap */

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

static App g_app;

static LRESULT CALLBACK SeekBarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData);

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
    return CreateWindowExW(exStyle, pClass, pText, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, pApp->hWnd, (HMENU)(INT_PTR)id, pApp->hInstance, NULL);
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
    listH = client.bottom - (margin + labelH + gap) - (gap + rowH) - (gap + rowH) - (gap + rowH) - (gap + labelH) - (gap + cfgH) - margin;
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
    pApp->hCast        = CreateChild(pApp, L"BUTTON", L"Cast", BS_DEFPUSHBUTTON | WS_TABSTOP, 0, ID_CAST);
    pApp->hHint        = CreateChild(pApp, L"STATIC", L"A file is served to the receiver over HTTP from this PC; an http(s) URL is sent as is.", SS_LEFT | SS_ENDELLIPSIS, 0, ID_HINT);
    pApp->hPlayPause   = CreateChild(pApp, L"BUTTON", L"Play", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_PLAY_PAUSE);
    pApp->hStop        = CreateChild(pApp, L"BUTTON", L"Stop", BS_PUSHBUTTON | WS_TABSTOP, 0, ID_STOP);
    pApp->hSeek        = CreateChild(pApp, TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | TBS_BOTH | WS_TABSTOP, 0, ID_SEEK);
    pApp->hTime        = CreateChild(pApp, L"STATIC", L"", SS_RIGHT, 0, ID_TIME);
    pApp->hStatus      = CreateChild(pApp, L"STATIC", L"", SS_LEFT | SS_ENDELLIPSIS, 0, ID_STATUS);

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
static void StartCastSource(App* pApp, const WCHAR* pSourceIn, int buildProxy)
{
    const int         selected = ListView_GetNextItem(pApp->hList, -1, LVNI_SELECTED);
    const CastDevice* pDevice;
    CastSessionConfig config;
    WCHAR             source[1024];
    char              error[256];

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
        char           localAddress[64];
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
            return;
        }
        pApp->pServer = MediaServer_Start(source, NULL, DEFAULT_SERVER_PORT, config.contentType, error, sizeof(error));
        if (NULL == pApp->pServer)
        {
            WCHAR* pError = DupUtf8ToWide(error);

            SetStatus(pApp, pError ? pError : L"Cannot start the media server.");
            free(pError);
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
    }
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
        pApp->hWnd = hWnd;
        pApp->dpi  = GetDpiForWindow(hWnd);
        CreateControls(pApp);
        StartDiscovery(pApp);
        SetTimer(hWnd, POSITION_TIMER, POSITION_TICK_MS, NULL);
        return 0;
    case WM_TIMER:
        if (POSITION_TIMER == wParam)
        {
            OnPositionTimer(pApp);
        }
        return 0;
    case WM_SIZE:
        Layout(pApp);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = Scale(pApp, 520);
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = Scale(pApp, 520);
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
        if ((HWND)lParam == pApp->hSeek)
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
    case WM_CLOSE:
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
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClass;
    wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
    if (0 == RegisterClassExW(&wc))
    {
        WSACleanup();
        return 1;
    }
    g_app.hInstance = hInstance;
    g_app.dpi       = GetDpiForSystem();
    hWnd            = CreateWindowExW(0, kClass, L"CastDemo", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, Scale(&g_app, 660), Scale(&g_app, 620), NULL, NULL, hInstance, NULL);
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
