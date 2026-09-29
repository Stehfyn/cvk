/* demo.c - ImmersiveWindowDemo: a borderless DirectComposition window playing demo.mp4.
**
** The window owns a composition swap chain and a cached native-resolution video bitmap.
** Media Foundation decodes on the same D3D11 device; CVK converts new video frames.
** D2D scales the cached video and draws the caption into the same presented frame.
** No C runtime beyond the entry point: Win32 only.
**
**   ImmersiveWindowDemo [file.mp4]       default: demo.mp4 beside the executable */

#include "immersive_window.h"
#include "presentation.h"
#include "video.h"
#include "os/os_process.h"
#include "os/os_types.h"
#include "os/win32/os_text.h"
#include <Windows.h> // IWYU pragma: keep
#include <basetsd.h>
#include <windef.h>
#include <stdint.h>
#include <d3d11.h>
#include <CommCtrl.h>
#include <combaseapi.h>
#include <debugapi.h>
#include <libloaderapi.h>
#include <minwindef.h>
#include <objbase.h>
#include <processthreadsapi.h>
#include <profileapi.h>
#include <WinBase.h>
#include <WinUser.h>
#include <winerror.h>
#include <winnt.h>

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
        OutputDebugString(TEXT("ImmersiveWindowDemo: Windows did not enable EcoQoS.\n"));
        return;
    }
    if (!SetPriorityClass(process, IDLE_PRIORITY_CLASS))
    {
        OutputDebugString(TEXT("ImmersiveWindowDemo: Windows did not lower the process priority.\n"));
    }
}

static LARGE_INTEGER g_frequency;
static LARGE_INTEGER g_start;
static LONGLONG      g_shown;

#define VIDEO_TIMER                  (1)
#define VIDEO_MILLISECONDS_PER_SECOND (1000)
#define VIDEO_MESSAGE_EXTRA_UNITS   (32)
#define VIDEO_WINDOW_WIDTH           (1280)
#define VIDEO_WINDOW_HEIGHT          (720)
static LARGE_INTEGER g_pauseStart;
static BOOL g_clockPaused;

static void ArmVideoTimer(HWND hWnd)
{
    LARGE_INTEGER now;
    LONGLONG next;
    LONGLONG remaining;
    UINT delay;
    UINT bounded_delay;
    bool_t below_minimum;
    if (NULL == g_pVideo || g_clockPaused || !g_info.FrameRateNumerator || !g_info.FrameRateDenominator)
    {
        return;
    }
    QueryPerformanceCounter(&now);
    next = g_start.QuadPart + (((g_shown + 1) * g_frequency.QuadPart * g_info.FrameRateDenominator) / g_info.FrameRateNumerator);
    remaining = next > now.QuadPart ? next - now.QuadPart : 0;
    delay = (UINT)(((remaining * VIDEO_MILLISECONDS_PER_SECOND) + g_frequency.QuadPart - 1) / g_frequency.QuadPart);
    below_minimum = delay < USER_TIMER_MINIMUM;
    bounded_delay = below_minimum ? USER_TIMER_MINIMUM : delay;
    /* This timer advances media time, including inside native modal loops.
    ** It does not schedule resizing or the compositor's display refresh. */
    if (!SetTimer(hWnd, VIDEO_TIMER, bounded_delay, NULL))
    {
        PostMessage(hWnd, WM_CLOSE, 0, 0);
    }
}

static void SyncPlaybackClock(HWND hWnd)
{
    BOOL paused = g_fPaused || IsIconic(hWnd);
    LARGE_INTEGER now;
    if (NULL == g_pVideo || paused == g_clockPaused)
    {
        return;
    }
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
    {
        return;
    }
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

static void SetupVideo(HWND hWnd, LPCTSTR pszFile)
{
    if (!GetImmersiveDevice(hWnd, &g_pDevice, &g_pContext))
    {
        return;
    }
    g_pVideo = VideoStream_Open(g_pDevice, g_pContext, pszFile);
    if (NULL == g_pVideo)
    {
        TCHAR message[MAX_PATH + VIDEO_MESSAGE_EXTRA_UNITS];
        LPTSTR copied;
        lstrcpy(message, TEXT("Cannot open "));
        copied = lstrcpyn(message + lstrlen(message), pszFile, MAX_PATH);
        if (NULL != copied)
        {
            MessageBox(hWnd, message, TEXT("ImmersiveWindowDemo"), MB_ICONWARNING);
        }
        else
        {
            MessageBox(hWnd, pszFile, TEXT("Cannot open video"), MB_ICONWARNING);
        }
        return;
    }
    VideoStream_GetInfo(g_pVideo, &g_info);
    SetImmersiveVideo(hWnd, g_info.Width, g_info.Height, DrawVideo);
    if (VideoStream_NextFrame(g_pVideo))
    {
        UpdateImmersiveVideo(hWnd);
    }
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
    {
        SyncPlaybackClock(hWnd);
    }
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
static bool_t ResolveFile(TCHAR* pszFile, int capacity)
{
    uint16_t argument[MAX_PATH];
    uint32_t unit_count;
    bool_t  found;
    bool_t  converted;
    LPTSTR  copied;
    DWORD   length;
    DWORD   index;
    int     lastSlash;

    pszFile[0]  = TEXT('\0');
    copied      = NULL;
    unit_count  = 0;
    found       = OS_Process_ReadArgument(&unit_count, argument, MAX_PATH - 1, 1);
    argument[unit_count] = 0;
    if (found)
    {
        converted = OS_Text_WriteNative(pszFile, (uint32_t)capacity, argument);
        copied    = converted ? pszFile : NULL;
    }
    else
    {
        lastSlash = -1;
        length    = GetModuleFileName(NULL, pszFile, (DWORD)capacity);
        if (0 < length && length < (DWORD)capacity)
        {
            for (index = 0; index < length; ++index)
            {
                if (TEXT('\\') == pszFile[index])
                {
                    lastSlash = (int)index;
                }
            }
            copied = lstrcpyn(pszFile + lastSlash + 1, TEXT("demo.mp4"), capacity - lastSlash - 1);
        }
    }
    return NULL != copied;
}

static int RunDemo(HINSTANCE hInstance, int nShowCmd)
{
    TCHAR   file[MAX_PATH];
    HWND    hWnd;
    HRESULT result_code;
    MSG     msg;
    BOOL    result;

    EnableEfficiencyMode();
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    result_code = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(result_code))
    {
        return 1;
    }
    InitImmersiveControls();
    if (!ResolveFile(file, MAX_PATH))
    {
        MessageBox(NULL, TEXT("The media path cannot be represented or is too long."),
                   TEXT("ImmersiveWindowDemo"), MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    hWnd = CreateWindowEx(WS_EX_NOREDIRECTIONBITMAP,
                           IMMERSIVE_CLASS,
                           TEXT("ImmersiveWindow32 Demo"),
                           BCS_DEBUG | BCS_BLURBEHIND | BCS_CENTERED | BCS_WINDOW,
                           0,
                           0,
                           VIDEO_WINDOW_WIDTH,
                           VIDEO_WINDOW_HEIGHT,
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
    for (;;)
    {
        result = GetMessage(&msg, NULL, 0, 0);
        if (0 >= result)
        {
            break;
        }
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    CoUninitialize();
    return result < 0 ? 1 : (int)msg.wParam;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The native GUI entry obtains generic Win32 launch state without a C runtime startup.
 */
uint32_t OS_Application_Main(void)
{
    STARTUPINFO startup;
    HINSTANCE   instance;
    bool_t      has_show_command;
    int         show_command;
    int         result;

    startup.cb      = sizeof(startup);
    instance        = GetModuleHandle(NULL);
    show_command    = SW_SHOWDEFAULT;
    GetStartupInfo(&startup);
    has_show_command = 0 != (startup.dwFlags & STARTF_USESHOWWINDOW);
    if (has_show_command)
    {
        show_command = (int)startup.wShowWindow;
    }
    result = 1;
    if (NULL != instance)
    {
        result = RunDemo(instance, show_command);
    }
    return (uint32_t)result;
}
