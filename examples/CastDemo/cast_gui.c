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
#include <shellapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cast_channel.h"
#include "cast_session.h"
#include "media_server.h"
#include "ssdp.h"

#if defined(_MSC_VER)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#define WM_APP_DISCOVERED (WM_APP + 1) /* lParam: DiscoveryResult* on the heap */
#define WM_APP_LOG        (WM_APP + 2) /* lParam: WCHAR* on the heap */
#define WM_APP_STATUS     (WM_APP + 3) /* lParam: CastStatus* on the heap */
#define WM_APP_ENDED      (WM_APP + 4) /* lParam: WCHAR* on the heap, or NULL */

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
    ID_STATUS
};

typedef struct DiscoveryResult
{
    unsigned   count;
    CastDevice devices[CAST_MAX_DEVICES];
} DiscoveryResult;

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
    HFONT     hFont;
    UINT      dpi;
    /* receivers */
    DiscoveryResult devices;
    int             discovering;
    /* the active session */
    CastSession* pSession;
    HANDLE       hSessionThread;
    MediaServer* pServer;
    CastStatus   status;
    int          seeking; /* the user is dragging the slider */
    int          stopping;
} App;

static App g_app;

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
    const int margin   = Scale(pApp, 12);
    const int gap      = Scale(pApp, 8);
    const int rowH     = Scale(pApp, 24);
    const int buttonW  = Scale(pApp, 96);
    const int smallW   = Scale(pApp, 72);
    const int labelH   = Scale(pApp, 18);
    const int timeW    = Scale(pApp, 150);
    int       width;
    int       y;
    int       listH;

    GetClientRect(pApp->hWnd, &client);
    width = client.right - 2 * margin;
    /* bottom-up: status, toolbar, hint, cast, file; the list takes the rest */
    listH = client.bottom - (margin + labelH + gap) - (gap + rowH) - (gap + rowH) - (gap + rowH) - (gap + labelH) - margin;
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
    ApplyFont(pApp);
    Layout(pApp);
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

static void StartCast(App* pApp)
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
    GetWindowTextW(pApp->hFile, source, (int)(sizeof(source) / sizeof(source[0])));
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
        (void)snprintf(config.contentType, sizeof(config.contentType), "%s",
                       (NULL != pExtension && NULL == wcschr(pExtension, L'/')) ? MediaServer_ContentTypeForExtension(pExtension) : "video/mp4");
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
    ZeroMemory(&pApp->status, sizeof(pApp->status));
    pApp->stopping = 0;
    UpdateControls(pApp);
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

static void OnStatus(App* pApp, CastStatus* pStatus)
{
    WCHAR position[32];
    WCHAR duration[32];
    WCHAR text[128];

    if (NULL == pStatus)
    {
        return;
    }
    pApp->status = *pStatus;
    free(pStatus);
    if (!pApp->seeking && pApp->status.duration > 0)
    {
        SendMessageW(pApp->hSeek, TBM_SETPOS, TRUE, (LPARAM)(LONG)(pApp->status.currentTime / pApp->status.duration * SEEK_RANGE + 0.5));
    }
    else if (!pApp->seeking)
    {
        SendMessageW(pApp->hSeek, TBM_SETPOS, TRUE, 0);
    }
    if (pApp->status.hasMedia)
    {
        FormatTime(pApp->status.currentTime, position, sizeof(position) / sizeof(position[0]));
        FormatTime(pApp->status.duration, duration, sizeof(duration) / sizeof(duration[0]));
        (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"%hs  %ls / %ls", pApp->status.stateName, position, duration);
    }
    else
    {
        (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"%hs", pApp->status.stateName);
    }
    SetWindowTextW(pApp->hTime, text);
    UpdateControls(pApp);
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
    CastSession_Destroy(pApp->pSession);
    pApp->pSession = NULL;
    MediaServer_Stop(pApp->pServer);
    pApp->pServer  = NULL;
    pApp->stopping = 0;
    ZeroMemory(&pApp->status, sizeof(pApp->status));
    SendMessageW(pApp->hSeek, TBM_SETPOS, TRUE, 0);
    SetWindowTextW(pApp->hTime, L"");
    if (NULL != pReason)
    {
        SetStatus(pApp, pReason);
    }
    UpdateControls(pApp);
}

static void OnSeekScroll(App* pApp, int code)
{
    const int position = (int)SendMessageW(pApp->hSeek, TBM_GETPOS, 0, 0);
    double    seconds;

    if (NULL == pApp->pSession || pApp->status.duration <= 0)
    {
        return;
    }
    seconds = pApp->status.duration * position / SEEK_RANGE;
    switch (code)
    {
    case TB_THUMBTRACK:
    {
        WCHAR preview[32];
        WCHAR duration[32];
        WCHAR text[96];

        pApp->seeking = 1;
        FormatTime(seconds, preview, sizeof(preview) / sizeof(preview[0]));
        FormatTime(pApp->status.duration, duration, sizeof(duration) / sizeof(duration[0]));
        (void)swprintf(text, sizeof(text) / sizeof(text[0]), L"SEEK  %ls / %ls", preview, duration);
        SetWindowTextW(pApp->hTime, text);
        break;
    }
    case TB_THUMBPOSITION:
    case TB_ENDTRACK:
        if (pApp->seeking)
        {
            pApp->seeking = 0;
            CastSession_Post(pApp->pSession, CAST_COMMAND_SEEK, seconds);
        }
        break;
    case TB_LINEUP:
    case TB_LINEDOWN:
    case TB_PAGEUP:
    case TB_PAGEDOWN:
    case TB_TOP:
    case TB_BOTTOM:
        CastSession_Post(pApp->pSession, CAST_COMMAND_SEEK, seconds);
        break;
    default:
        break;
    }
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
        return 0;
    case WM_SIZE:
        Layout(pApp);
        return 0;
    case WM_GETMINMAXINFO:
        ((MINMAXINFO*)lParam)->ptMinTrackSize.x = Scale(pApp, 520);
        ((MINMAXINFO*)lParam)->ptMinTrackSize.y = Scale(pApp, 320);
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
    case WM_CLOSE:
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
    hWnd            = CreateWindowExW(0, kClass, L"CastDemo", WS_OVERLAPPEDWINDOW & ~WS_MAXIMIZEBOX, CW_USEDEFAULT, CW_USEDEFAULT, Scale(&g_app, 640), Scale(&g_app, 400), NULL, NULL,
                                      hInstance, NULL);
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
    return (int)msg.wParam;
}
