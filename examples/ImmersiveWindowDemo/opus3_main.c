/*
 * main.c -- opus3: a CRT-free video player composited into a borderless, custom-caption window.
 *
 * The window lives on its own thread (window.c) so the message pump never stalls the presenter. This
 * thread owns the GPU stack (swapchain.c: D3D11 -> Direct2D -> a DXGI flip-model composition swap chain
 * -> DirectComposition) and paces itself off the display's vertical blank (D3DKMT) to publish exactly
 * one composed frame per refresh: the decoded video frame (video.c), then the caption DWM would have
 * drawn (caption.c: system icon, DWrite title, the caption-button sprites straight out of the DWMWindow
 * theme atlas, the light/dark toggle glyph, and a 1px top border on Windows 10).
 *
 * Every Win32/COM failure is fatal here (nt_assert.h ASSERT): the program has no fallback path, so each
 * HRESULT is captured and asserted rather than gated on.
 */

#pragma runtime_checks("", off)
#pragma strict_gs_check(off)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif

#include <Windows.h> // IWYU pragma: keep
#include <minwindef.h>
#include <windef.h>
#include <winnt.h>
#include <winerror.h>
#include <WinBase.h>
#include <WinUser.h>
#include <wingdi.h>
#include <processthreadsapi.h>
#include <processenv.h>
#include <libloaderapi.h>
#include <handleapi.h>
#include <heapapi.h>
#include <synchapi.h>
#include <combaseapi.h>
#include <objbase.h>
#include <cd2d.h>
#include <dxgi.h>
#include <dxgi1_6.h>
#include <Unknwn.h>
#include <winternl.h>
#include <d3dkmthk.h>
#include <shellapi.h>
#include "nt_assert.h"
#include "window.h"
#include "swapchain.h" /* before video.h: brings the D3D11 / Direct2D types it names */
#include "opus3_video.h"
#include "caption.h"
#include "opus3_main.h"
#include "cdxgi.h"
#include "cdcomp.h"

NTSYSCALLAPI NTSTATUS NTAPI NtDelayExecution(BOOLEAN Alertable, PLARGE_INTEGER DelayInterval);

/* ---- constants --------------------------------------------------------------------------------- */

/* Frame pacing. Each iteration waits for the vertical blank, arms a short one-shot timer, renders and
   commits, then reads the timer to record which side of the window the commit landed on. */
#define APP_COMMIT_WINDOW_100NS   (-3LL * 10000LL) /* relative due time: 3 ms after the blank */
#define APP_100NS_PER_SECOND      10000000         /* refresh period = this / VREFRESH */
#define APP_100NS_PER_MILLISECOND 10000LL
#define APP_WINDOW_WIDTH          1280
#define APP_WINDOW_HEIGHT         720

/* ---- types ------------------------------------------------------------------------------------- */

/* The window thread's start-up handshake: the presenter waits on hAck until the window exists, then
   takes hwnd and the IMMERSIVEWINDOW, which lives on the window thread's stack for the process
   lifetime (its WM_QUIT calls ExitProcess). */
typedef struct APP_WINDOW_LAUNCH
{
    HANDLE           hAck;
    HWND             hwnd;
    IMMERSIVEWINDOW* pWindow;
} APP_WINDOW_LAUNCH;

/* One window, one pipeline: everything the presenter thread owns. Heap-allocated once at entry (the
   caption's atlas tables run to several KB) and handed to every helper -- no globals. */
typedef struct APP_STATE
{
    HWND             hwnd;
    IMMERSIVEWINDOW* pWindow;   /* owned by the window thread (see APP_WINDOW_LAUNCH) */
    SWAPCHAIN        swapChain; /* the GPU stack; its context draws every frame        */
    CAPTION          caption;   /* the window draws it (IMMERSIVEWINDOW.caption)       */
    VIDEO            video;
    LONGLONG         llRefreshPeriod; /* one display refresh, in 100ns units */
    /* Frame accounting, for a debugger (volatile so the stores survive). */
    volatile ULONGLONG cVblanks;
    volatile ULONGLONG cCommitsBeforeTimer; /* commit landed inside the post-blank window */
    volatile ULONGLONG cCommitsAfterTimer;  /* commit landed after the timer had fired    */
} APP_STATE;

/* ---- helpers ----------------------------------------------------------------------------------- */

/* The primary display's refresh period in 100ns units (the decoder's frame lead time). */
static FORCEINLINE LONGLONG AppRefreshPeriod(void)
{
    HDC      hdc;
    LONGLONG llPeriod;

    hdc = GetDC(NULL);
    ASSERT(hdc);
    llPeriod = APP_100NS_PER_SECOND / GetDeviceCaps(hdc, VREFRESH);
    ASSERT(ReleaseDC(NULL, hdc));
    return llPeriod;
}

/* Resolve the adapter + VidPn source driving hwnd's display (NULL: the primary) for
   D3DKMTWaitForVerticalBlankEvent. The adapter handle is held for the process lifetime. */
static FORCEINLINE void AppOpenVerticalBlankEvent(HWND hwnd, D3DKMT_WAITFORVERTICALBLANKEVENT* pvbe)
{
    D3DKMT_OPENADAPTERFROMHDC adapterOpen;

    SecureZeroMemory(&adapterOpen, sizeof(adapterOpen));
    adapterOpen.hDc = GetDC(hwnd);
    ASSERT(adapterOpen.hDc);
    ASSERT(NT_SUCCESS(D3DKMTOpenAdapterFromHdc(&adapterOpen)));
    ASSERT(ReleaseDC(hwnd, adapterOpen.hDc));

    SecureZeroMemory(pvbe, sizeof(*pvbe));
    pvbe->hAdapter      = adapterOpen.hAdapter;
    pvbe->VidPnSourceId = adapterOpen.VidPnSourceId;
}

/* The video to play: the first command-line argument, else demo.mp4 beside the executable. */
static FORCEINLINE void AppResolveVideoPath(WCHAR* pszPath, DWORD cchPath)
{
    static const WCHAR szDefault[] = L"demo.mp4";
    LPWSTR*            rgpszArgs;
    int                cArgs;
    DWORD              cch;
    BOOL               fAtFileName;

    rgpszArgs = CommandLineToArgvW(GetCommandLineW(), &cArgs);
    ASSERT(rgpszArgs);
    if (cArgs > 1)
    {
        ASSERT((DWORD)lstrlenW(rgpszArgs[1]) < cchPath);
        (void)lstrcpyW(pszPath, rgpszArgs[1]);
    }
    else
    {
        cch = GetModuleFileNameW(NULL, pszPath, cchPath);
        ASSERT(cch && (cch < cchPath));
        fAtFileName = (cch == 0U);
        while (!fAtFileName)
        {
            fAtFileName = (L'\\' == pszPath[cch - 1U]);
            if (!fAtFileName)
            {
                --cch;
            }
        }
        ASSERT((cch + ARRAYSIZE(szDefault)) <= cchPath);
        (void)lstrcpyW(pszPath + cch, szDefault);
    }
    (void)LocalFree((HLOCAL)rgpszArgs);
}

/* ---- window thread ----------------------------------------------------------------------------- */

/* Creates the immersive window on this thread's stack (for the process lifetime), hands it to the
   presenter, and pumps messages until WM_QUIT, which ends the process. */
static DWORD WINAPI AppWindowThread(LPVOID pvParameter)
{
    APP_WINDOW_LAUNCH* pLaunch;
    IMMERSIVEWINDOW    window;
    IMMERSIVEWINDOWCREATEINFO createInfo;
    MSG                msg;
    BOOL               fHasMessage;
    LARGE_INTEGER      liDelay;
    NTSTATUS           status;

    pLaunch = (APP_WINDOW_LAUNCH*)pvParameter;
    createInfo.clientProc = NULL;
    createInfo.clientGeometryProc = NULL;
    createInfo.clientData = NULL;
    createInfo.title = TEXT("Opus3");
    createInfo.width = APP_WINDOW_WIDTH;
    createInfo.height = APP_WINDOW_HEIGHT;
    createInfo.showCommand = SW_SHOW;
    CreateImmersiveWindow(&window, &createInfo);
    pLaunch->hwnd    = window.hwnd;
    pLaunch->pWindow = &window;
    ASSERT(SetEvent(pLaunch->hAck));

    liDelay.QuadPart = -(LONGLONG)USER_TIMER_MINIMUM * APP_100NS_PER_MILLISECOND;
    for (;;)
    {
        for (;;)
        {
            fHasMessage = PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE);
            if (fHasMessage)
            {
                if (WM_QUIT == msg.message)
                {
                    ExitProcess((UINT)msg.wParam);
                }
                TranslateMessage(&msg);
                DispatchMessage(&msg);
            }
            else
            {
                break;
            }
        }
        status = NtDelayExecution(FALSE, &liDelay);
        ASSERT(NT_SUCCESS(status));
    }
}

/* Start the window thread and wait until its window exists. */
static FORCEINLINE void AppStartWindowThread(APP_STATE* pApp)
{
    APP_WINDOW_LAUNCH launch;
    HANDLE            hThread;
    BOOL              fClosed;

    SecureZeroMemory(&launch, sizeof(launch));

    launch.hAck = CreateEvent(NULL, FALSE, FALSE, NULL);
    ASSERT(launch.hAck);

    hThread = CreateThread(NULL, 0U, AppWindowThread, &launch, 0U, NULL);
    ASSERT(NULL != hThread);
    fClosed = CloseHandle(hThread);
    ASSERT(fClosed);

    ASSERT(WAIT_OBJECT_0 == WaitForSingleObject(launch.hAck, INFINITE));
    ASSERT(CloseHandle(launch.hAck));

    pApp->hwnd    = launch.hwnd;
    pApp->pWindow = launch.pWindow;
}

/* ---- frame ------------------------------------------------------------------------------------- */

/* The client content of one frame, inside Begin/EndImmersivePaint (which clear the client area and draw
   the caption over it): the video frame, inset below the caption band. */
static FORCEINLINE void AppRenderFrame(APP_STATE* pApp, ID2D1DeviceContext* pDC, const IMMERSIVEPAINTSTRUCT* pps)
{
    DrawVideoFrame(&pApp->video, pDC, &pps->rcClient, pps->layout.cyCaption);
}

/* ---- entry ------------------------------------------------------------------------------------- */

/* Process entry (no CRT: /ENTRY:WinMainCRTStartup). This thread is the presenter; the window runs on
   its own thread. Never returns -- the window thread's WM_QUIT ends the process. */
void WINAPI WinMainCRTStartup(void)
{
    APP_STATE*                       pApp;
    IMMERSIVEPAINTSTRUCT             paint;
    ID2D1DeviceContext*              pDC;
    D3DKMT_WAITFORVERTICALBLANKEVENT vbe;
    LARGE_INTEGER                    liDue;
    HANDLE                           hTimer;
    IDXGIFactory6*                   factory;
    IDXGIAdapter1*                   adapter;
    WCHAR                            szVideoPath[MAX_PATH];
    DWORD                            dwTimerState;
    HRESULT                          result;

    pApp = (APP_STATE*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pApp));
    ASSERT(pApp);
    ASSERT(SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2));

    AppStartWindowThread(pApp);

    hTimer = CreateWaitableTimerEx(NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    ASSERT(hTimer);

    result = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    ASSERT(0 <= result);
    cdxgiSetupDefaultFactory(&factory);
    cdxgiFactoryGetMinimumPowerAdapter(factory, &adapter);
    InitSwapChain(&pApp->swapChain, (IDXGIAdapter*)adapter);
    cdxgiUnknownRelease((IUnknown*)adapter);
    cdxgiUnknownRelease((IUnknown*)factory);
    AppResolveVideoPath(szVideoPath, ARRAYSIZE(szVideoPath));
    InitVideo(&pApp->video, szVideoPath, pApp->swapChain.device);
    InitCaption(&pApp->caption, pApp->pWindow, pApp->swapChain.context);
    pApp->pWindow->caption = &pApp->caption;
    PrepareVideoFrame(&pApp->video, pApp->swapChain.context, 0);
    pApp->llRefreshPeriod = AppRefreshPeriod();
    cdcompSetupDevice((IDXGIDevice*)pApp->swapChain.dxgiDevice, pApp->hwnd, TRUE, &pApp->swapChain.compositionDevice, &pApp->swapChain.compositionTarget, &pApp->swapChain.visual);
    cdcompVisualSetContent(pApp->swapChain.visual, (IUnknown*)pApp->swapChain.swapChain);
    pApp->pWindow->swapChain = &pApp->swapChain;

    /* First frame. */
    pDC = BeginImmersivePaint(pApp->pWindow, &paint);
    AppRenderFrame(pApp, pDC, &paint);
    EndImmersivePaint(pApp->pWindow, &paint);

    AppOpenVerticalBlankEvent(NULL, &vbe);
    liDue.QuadPart = APP_COMMIT_WINDOW_100NS;

    /* Steady state: one frame per vertical blank. Decode one refresh ahead, wait for the blank, arm the
       commit-window timer, paint, then record which side of the window the commit landed on. */
    for (;;)
    {
        PrepareVideoFrame(&pApp->video, pApp->swapChain.context, pApp->llRefreshPeriod);
        ASSERT(NT_SUCCESS(D3DKMTWaitForVerticalBlankEvent(&vbe)));
        ASSERT(SetWaitableTimerEx(hTimer, &liDue, 0, NULL, NULL, NULL, 0U));

        pDC = BeginImmersivePaint(pApp->pWindow, &paint);
        AppRenderFrame(pApp, pDC, &paint);
        EndImmersivePaint(pApp->pWindow, &paint);

        dwTimerState = WaitForSingleObject(hTimer, 0U);
        ASSERT((WAIT_OBJECT_0 == dwTimerState) || (WAIT_TIMEOUT == dwTimerState));
        if (WAIT_TIMEOUT == dwTimerState)
        {
            ++pApp->cCommitsBeforeTimer;
        }
        else
        {
            ++pApp->cCommitsAfterTimer;
        }
        ++pApp->cVblanks;
    }
}
