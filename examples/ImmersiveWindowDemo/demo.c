/* demo.c - ImmersiveWindowDemo: a borderless DirectComposition window playing demo.mp4.
**
** The window owns a retained DirectComposition video surface and a separate caption.
** Media Foundation decodes on the same D3D11 device; CVK converts new video frames.
** DWM owns display refresh and window composition independently of video frame rate.
** No C runtime beyond the entry point: Win32 only.
**
**   ImmersiveWindowDemo [file.mp4]       default: demo.mp4 beside the executable */

#include "immersive_window.h"
#include "presentation.h"
#include "video.h"

#include <commctrl.h>
#include <shellapi.h>

#include "cdx11.h"
#include "cdxgi.h"

static ID3D11Device*           g_pDevice;
static ID3D11DeviceContext*    g_pContext;
static VideoStream*            g_pVideo;
static VideoInfo               g_info;
static BOOL                    g_fPaused;

/* Opt into the same CPU policy as Task Manager's Efficiency mode (green leaf). */
static void EnableEfficiencyMode(void)
{
    PROCESS_POWER_THROTTLING_STATE power = {0};
    HANDLE process = GetCurrentProcess();

    power.Version = PROCESS_POWER_THROTTLING_CURRENT_VERSION;
    power.ControlMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    power.StateMask = PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
    if (!SetProcessInformation(process, ProcessPowerThrottling, &power, sizeof(power)))
    {
        OutputDebugStringW(L"ImmersiveWindowDemo: Windows did not enable EcoQoS.\n");
        return;
    }
    if (!SetPriorityClass(process, IDLE_PRIORITY_CLASS))
    {
        OutputDebugStringW(L"ImmersiveWindowDemo: Windows did not lower the process priority.\n");
    }
}

static LARGE_INTEGER g_frequency;
static LARGE_INTEGER g_start;
static LONGLONG      g_shown;

#define VIDEO_TIMER 1
static LARGE_INTEGER g_pauseStart;
static BOOL g_clockPaused;

static void ArmVideoTimer(HWND hWnd)
{
    LARGE_INTEGER now;
    LONGLONG next, remaining;
    UINT delay;
    if (NULL == g_pVideo || g_clockPaused || !g_info.FrameRateNumerator || !g_info.FrameRateDenominator)
        return;
    QueryPerformanceCounter(&now);
    next = g_start.QuadPart + (g_shown + 1) * g_frequency.QuadPart * g_info.FrameRateDenominator / g_info.FrameRateNumerator;
    remaining = max(0, next - now.QuadPart);
    delay = (UINT)((remaining * 1000 + g_frequency.QuadPart - 1) / g_frequency.QuadPart);
    /* This timer advances media time, including inside native modal loops.
    ** It does not schedule resizing or the compositor's display refresh. */
    SetTimer(hWnd, VIDEO_TIMER, max(USER_TIMER_MINIMUM, delay), NULL);
}

static void SyncPlaybackClock(HWND hWnd)
{
    BOOL paused = g_fPaused || IsIconic(hWnd);
    LARGE_INTEGER now;
    if (NULL == g_pVideo || paused == g_clockPaused)
        return;
    QueryPerformanceCounter(&now);
    if (paused)
    {
        g_pauseStart = now;
        KillTimer(hWnd, VIDEO_TIMER);
    }
    else
    {
        g_start.QuadPart += now.QuadPart - g_pauseStart.QuadPart;
    }
    g_clockPaused = paused;
    ArmVideoTimer(hWnd);
}

static void DrawVideo(HWND hWnd, ID3D11Texture2D* texture, UINT subresource, UINT x, UINT y)
{
    (void)hWnd;
    VideoStream_CopyFrame(g_pVideo, texture, subresource, x, y);
}

static void AdvanceVideo(HWND hWnd)
{
    LARGE_INTEGER now;
    LONGLONG due;
    KillTimer(hWnd, VIDEO_TIMER);
    if (NULL == g_pVideo || g_clockPaused)
        return;
    QueryPerformanceCounter(&now);
    due = (now.QuadPart - g_start.QuadPart) * g_info.FrameRateNumerator / (g_frequency.QuadPart * g_info.FrameRateDenominator);
    if (due > g_shown && VideoStream_NextFrame(g_pVideo))
    {
        g_shown = due;
        UpdateImmersiveVideo(hWnd);
        VideoStream_Prefetch(g_pVideo, 0);
    }
    ArmVideoTimer(hWnd);
}

static void SetupVideo(HWND hWnd, LPCWSTR pszFile)
{
    if (!GetImmersiveDevice(hWnd, &g_pDevice, &g_pContext))
        return;
    g_pVideo = VideoStream_Open(g_pDevice, g_pContext, pszFile);
    if (NULL == g_pVideo)
    {
        WCHAR message[MAX_PATH + 32];
        lstrcpyW(message, L"Cannot open ");
        lstrcpynW(message + lstrlenW(message), pszFile, MAX_PATH);
        MessageBoxW(hWnd, message, L"ImmersiveWindowDemo", MB_ICONWARNING);
        return;
    }
    VideoStream_GetInfo(g_pVideo, &g_info);
    SetImmersiveVideo(hWnd, g_info.Width, g_info.Height, DrawVideo);
    if (VideoStream_NextFrame(g_pVideo))
        UpdateImmersiveVideo(hWnd);
    QueryPerformanceFrequency(&g_frequency);
    QueryPerformanceCounter(&g_start);
    ArmVideoTimer(hWnd);
}

/* the application's subclass of the immersive window: pause on Space, video teardown */
static LRESULT CALLBACK DemoProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData)
{
    (void)refData;
    if (WM_KEYUP == uMsg && VK_SPACE == wParam)
    {
        g_fPaused = !g_fPaused;
        SyncPlaybackClock(hWnd);
        return 0;
    }
    if (WM_TIMER == uMsg && VIDEO_TIMER == wParam)
    {
        AdvanceVideo(hWnd);
        return 0;
    }
    if (WM_SIZE == uMsg)
        SyncPlaybackClock(hWnd);
    if (WM_DESTROY == uMsg)
    {
        KillTimer(hWnd, VIDEO_TIMER);
        if (NULL != g_pVideo)
        {
            VideoStream_Close(g_pVideo);
            g_pVideo = NULL;
        }
    }
    if (WM_NCDESTROY == uMsg)
    {
        RemoveWindowSubclass(hWnd, DemoProc, idSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

/* The file: the first command line argument, else demo.mp4 beside the executable. */
static void ResolveFile(WCHAR* pszFile, int capacity)
{
    int     argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (NULL != argv && argc > 1)
    {
        lstrcpynW(pszFile, argv[1], capacity);
    }
    else
    {
        int i;
        int lastSlash = -1;
        GetModuleFileNameW(NULL, pszFile, (DWORD)capacity);
        for (i = 0; 0 != pszFile[i]; ++i)
        {
            if (L'\\' == pszFile[i])
            {
                lastSlash = i;
            }
        }
        lstrcpynW(pszFile + lastSlash + 1, L"demo.mp4", capacity - lastSlash - 1);
    }
    if (NULL != argv)
    {
        LocalFree(argv);
    }
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPWSTR lpCmdLine, int nShowCmd)
{
    WCHAR   file[MAX_PATH];
    HWND    hWnd;
    HRESULT hr;
    MSG     msg;
    BOOL    result;

    (void)hPrevInstance;
    (void)lpCmdLine;

    EnableEfficiencyMode();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(hr))
    {
        return 1;
    }
    InitImmersiveControls();
    ResolveFile(file, MAX_PATH);

    hWnd = CreateWindowExW(WS_EX_NOREDIRECTIONBITMAP,
                           IMMERSIVE_CLASSW,
                           L"ImmersiveWindow32 Demo",
                           BCS_DEBUG | BCS_BLURBEHIND | BCS_CENTERED | BCS_WINDOW,
                           0,
                           0,
                           1280,
                           720,
                           NULL,
                           NULL,
                           hInstance,
                           NULL);
    if (NULL == hWnd)
    {
        return 1;
    }
    SetWindowSubclass(hWnd, DemoProc, 1, 0);
    SetupVideo(hWnd, file);

    ShowWindow(hWnd, nShowCmd);
    UpdateWindow(hWnd);
    while ((result = GetMessageW(&msg, NULL, 0, 0)) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return result < 0 ? 1 : (int)msg.wParam;
}
