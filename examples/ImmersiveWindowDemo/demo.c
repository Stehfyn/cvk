/* demo.c - ImmersiveWindowDemo: a borderless DirectComposition window playing demo.mp4.
**
** The window (immersive_window.c) owns the Direct3D 11 device, the composition swap chain and
** the Direct2D caption. The video (video.c) decodes the file with Media Foundation on the same
** device and draws each frame into the window's offscreen target before the caption goes on top.
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
static ID3D11RenderTargetView* g_pBackBufferTarget;
static VideoStream*            g_pVideo;
static VideoInfo               g_info;
static BOOL                    g_fPaused;
static BOOL                    g_fDirty = TRUE; /* something other than the video changed: repaint */
static BOOL                    g_fNewFrame;     /* the last PreDraw adopted a video frame */

static LARGE_INTEGER g_frequency;
static LARGE_INTEGER g_start;
static LONGLONG      g_shown;

/* Frames of the video elapsed on the wall clock: seconds * numerator / denominator, in integers. */
static LONGLONG FramesElapsed(void)
{
    LARGE_INTEGER now;
    if (0 == g_frequency.QuadPart)
    {
        QueryPerformanceFrequency(&g_frequency);
        QueryPerformanceCounter(&g_start);
    }
    QueryPerformanceCounter(&now);
    return ((now.QuadPart - g_start.QuadPart) * g_info.FrameRateNumerator) / (g_frequency.QuadPart * g_info.FrameRateDenominator);
}

/* Milliseconds until the next video frame is due; a long wait when there is nothing to pace. */
static DWORD MillisecondsToNextFrame(void)
{
    LARGE_INTEGER now;
    LONGLONG      nextTicks;
    if (NULL == g_pVideo || g_fPaused || 0 == g_info.FrameRateNumerator)
    {
        return 1000;
    }
    (void)FramesElapsed();
    QueryPerformanceCounter(&now);
    nextTicks = g_start.QuadPart + ((g_shown + 1) * g_frequency.QuadPart * g_info.FrameRateDenominator) / g_info.FrameRateNumerator;
    if (nextTicks <= now.QuadPart)
    {
        return 0;
    }
    return (DWORD)((nextTicks - now.QuadPart) * 1000 / g_frequency.QuadPart);
}

/* Advances the video when the wall clock reaches the next frame, as the original PreDraw did. */
static void PreDraw(HWND hWnd)
{
    LONGLONG due;

    (void)hWnd;
    if (NULL == g_pVideo || g_fPaused || 0 == g_info.FrameRateNumerator)
    {
        return;
    }
    due = FramesElapsed();
    if (due > g_shown)
    {
        g_shown     = due;
        g_fNewFrame = VideoStream_NextFrame(g_pVideo) || g_fNewFrame;
    }
}

/* Draws the current frame under the caption: the video fills the window's client area. */
static void Draw(HWND hWnd)
{
    SIZE size;
    if (NULL == g_pVideo)
    {
        return;
    }
    if (GetImmersivePaintSize(hWnd, &size) && size.cx > 0 && size.cy > 0)
    {
        VideoStream_Draw(g_pVideo, g_pBackBufferTarget, (UINT)size.cx, (UINT)size.cy);
    }
}

static void SetupVideo(HWND hWnd, LPCWSTR pszFile)
{
    void* pDevice;
    void* pContext;
    void* pSwapChain;
    void* pD2DContext;
    void* pSwapChain2;

    if (!GetDeviceAndSwapchain(hWnd, &pDevice, &pContext, &pSwapChain, &pD2DContext, &pSwapChain2))
    {
        return;
    }
    g_pDevice    = (ID3D11Device*)pDevice;
    g_pContext   = (ID3D11DeviceContext*)pContext;
    g_pBackBufferTarget = GetImmersiveFrameTarget(hWnd);

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
    SetDeviceDrawCallback(hWnd, Draw);
    SetDevicePreDrawCallback(hWnd, PreDraw);
}

/* the application's subclass of the immersive window: pause on Space, video teardown */
static LRESULT CALLBACK DemoProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData)
{
    (void)refData;
    if (WM_KEYUP == uMsg && VK_SPACE == wParam)
    {
        g_fPaused = !g_fPaused;
        return 0;
    }
    /* anything the caption or the frame reflects: hover and clicks on the caption, activation,
    ** geometry, colours, DPI */
    switch (uMsg)
    {
    case WM_NCMOUSEMOVE:
    case WM_NCMOUSELEAVE:
    case WM_MOUSEMOVE:
    case WM_MOUSELEAVE:
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONUP:
    case WM_NCLBUTTONDBLCLK:
    case WM_NCRBUTTONUP:
    case WM_ACTIVATE:
    case WM_NCACTIVATE:
    case WM_SIZE:
    case WM_WINDOWPOSCHANGED:
    case WM_DISPLAYCHANGE:
    case WM_DPICHANGED:
    case WM_SYSCOLORCHANGE:
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case WM_SYSCOMMAND:
    case WM_EXITSIZEMOVE:
    case WM_EXITMENULOOP:
    case WM_INITD2D:
        g_fDirty = TRUE;
        break;
    default:
        break;
    }
    if (WM_DESTROY == uMsg)
    {
        if (NULL != g_pVideo)
        {
            VideoStream_Close(g_pVideo);
            g_pVideo = NULL;
        }
        if (NULL != g_pBackBufferTarget)
        {
            cdxgiUnknownRelease((IUnknown*)g_pBackBufferTarget);
            g_pBackBufferTarget = NULL;
        }
    }
    if (WM_NCDESTROY == uMsg)
    {
        RemoveWindowSubclass(hWnd, DemoProc, idSubclass);
    }
    return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

static BOOL PumpMessageQueue(void)
{
    MSG  msg;
    BOOL fQuit = FALSE;
    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
        fQuit |= (WM_QUIT == msg.message);
    }
    return !fQuit;
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

    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nShowCmd;

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
                           BCS_DEBUG | BCS_BLURBEHIND | BCS_CENTERED | BCS_WINDOW | WS_VISIBLE,
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

    /* Paint when there is something new: a video frame that came due, or a change the caption
    ** reflects. Otherwise sleep until the next frame is due or a message arrives, so an idle or
    ** paused window costs nothing. The window's own resize path paints its geometry frames. */
    while (PumpMessageQueue())
    {
        if (IsIconic(hWnd))
        {
            WaitMessage();
            continue;
        }
        PreDraw(hWnd);
        if (g_fNewFrame || g_fDirty)
        {
            g_fNewFrame = FALSE;
            g_fDirty    = FALSE;
            /* BeginImmersivePaint waits on this swap chain's frame-latency
            ** handle. A separate scanline/vblank wait delays the Vulkan work
            ** until after the blank and uses the wrong clock across monitors. */
            BeginImmersivePaint(hWnd);
            EndImmersivePaint(hWnd, IMMERSIVE_PAINT_RESTART, IMMERSIVE_PAINT_SYNC_0);
        }
        else
        {
            DWORD wait = MillisecondsToNextFrame();
            if (NULL != g_pVideo && !g_fPaused && wait > 4)
            {
                VideoStream_Prefetch(g_pVideo, wait - 4); /* decode ahead in the time before the next frame */
                wait = MillisecondsToNextFrame();
            }
            MsgWaitForMultipleObjects(0, NULL, FALSE, wait, QS_ALLINPUT);
        }
    }
    CoUninitialize();
    return 0;
}
