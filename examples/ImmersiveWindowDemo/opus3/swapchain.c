// CDX supplies D2D, D3D and DXGI declarations that conflict with the SDK headers.
// NOLINTBEGIN(misc-include-cleaner)
/*
 * swapchain.c -- the composition swap chain: the D3D11 device, the DXGI flip-model swap chain sized to
 * the desktop, the Direct2D context that draws into its back buffer, and the DirectComposition visual
 * that shows it in the window. The presenter renders through the context, presents, then commits.
 *
 * Every Win32/COM failure is fatal here (nt_assert.h ASSERT): the program has no fallback path, so each
 * HRESULT is captured and asserted rather than gated on.
 */

#pragma runtime_checks("", off)
#if defined(_MSC_VER) && !defined(__clang__)
#pragma check_stack(off)
#endif
#pragma strict_gs_check(off)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif

#include <Windows.h> // IWYU pragma: keep
#include <initguid.h> // IWYU pragma: keep
#include <windef.h>
#include <minwindef.h>
#include <winerror.h>
#include <Unknwnbase.h>
#include "swapchain.h"
#include "nt_assert.h"
#include "cd3d11.h"
#include "cdxgi.h"

/* Swap chain: FLIP_DISCARD, presented immediately (tearing + mode switch allowed) with the frame-latency
   waitable object. It is sized to the desktop once and clipped by the window, so it is never resized. */
#define SWAPCHAIN_FLAGS             (DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
#define SWAPCHAIN_BUFFERS           5U
#define SWAPCHAIN_MAX_FRAME_LATENCY 1U
#define SWAPCHAIN_BITMAP_DPI        96.0F

/* Stand up the GPU stack: the D3D11 device, the composition swap chain with one frame of latency, and a
   Direct2D device context targeting its back buffer. */
extern FORCEINLINE void InitSwapChain(SWAPCHAIN* pSwapChain, IDXGIAdapter* adapter)
{
    IDXGIFactory2*          pFactory;
    IDXGISwapChain2*        pSwapChain2;
    IDXGISurface*           pBackBuffer;
    ID2D1Device*            pD2DDevice;
    ID2D1Bitmap1*           pTarget;
    D2D1_PIXEL_FORMAT       pixelFormat;
    D2D1_BITMAP_PROPERTIES1 bp;
    D3D_DRIVER_TYPE         driverType;
    UINT                    desktopWidth;
    UINT                    desktopHeight;

    if (NULL != adapter)
    {
        driverType = D3D_DRIVER_TYPE_UNKNOWN;
    }
    else
    {
        driverType = D3D_DRIVER_TYPE_HARDWARE;
    }
    cd3d11SetupDevice1(adapter, driverType, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT, &pSwapChain->device, &pSwapChain->dxgiDevice, NULL, NULL);
    cdxgiCreateFactory2(0U, &pFactory);
    cdxgiFactoryGetPrimaryOutputSize((IDXGIFactory1*)pFactory, &desktopWidth, &desktopHeight);
    cdxgiSetupCompositionSwapChain2(pFactory,
                                    (IUnknown*)pSwapChain->dxgiDevice,
                                    desktopWidth,
                                    desktopHeight,
                                    DXGI_FORMAT_B8G8R8A8_UNORM,
                                    SWAPCHAIN_BUFFERS,
                                    DXGI_SWAP_EFFECT_FLIP_DISCARD,
                                    SWAPCHAIN_FLAGS,
                                    &pSwapChain->swapChain,
                                    &pSwapChain2);
    cdxgiSwapChainSetMaximumFrameLatency(pSwapChain2, SWAPCHAIN_MAX_FRAME_LATENCY);
    cdxgiUnknownReleaseSafe((IUnknown*)pSwapChain2);
    cdxgiUnknownReleaseSafe((IUnknown*)pFactory);

    cd2dCreateDevice((IDXGIDevice*)pSwapChain->dxgiDevice, NULL, &pD2DDevice);
    cd2dSetupDefaultDeviceContext(pD2DDevice, &pSwapChain->context);
    cdxgiUnknownReleaseSafe((IUnknown*)pD2DDevice);

    cdxgiSwapChainGetBufferAsSurface((IDXGISwapChain*)pSwapChain->swapChain, 0U, &pBackBuffer);
    cd2dInitPixelFormat(&pixelFormat, DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    cd2dInitBitmapProperties1(&bp, pixelFormat, SWAPCHAIN_BITMAP_DPI, SWAPCHAIN_BITMAP_DPI, (D2D1_BITMAP_OPTIONS)(D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW), NULL); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange): bitmap options are flags.
    cd2dDeviceContextCreateBitmapFromDxgiSurface(pSwapChain->context, pBackBuffer, &bp, &pTarget);
    cdxgiUnknownReleaseSafe((IUnknown*)pBackBuffer);
    cd2dDeviceContextSetTargetBitmap(pSwapChain->context, pTarget);
    cdxgiUnknownReleaseSafe((IUnknown*)pTarget);
}

/* Two presents per frame: an immediate (tearing, non-blocking) present of the new frame, then either a
   sequenced vsync present (fRestart / fVSync) or a second immediate one. DXGI_ERROR_WAS_STILL_DRAWING
   is DO_NOT_WAIT's "not yet" and is tolerated; TRUE only when both presents fully succeeded. */
extern FORCEINLINE BOOL PresentSwapChain(SWAPCHAIN* pSwapChain, BOOL fRestart, BOOL fVSync)
{
    HRESULT hrFirst;
    HRESULT hrSecond;
    UINT    uFlags;

    uFlags = DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_DO_NOT_WAIT;
    if (fRestart)
    {
        uFlags |= DXGI_PRESENT_RESTART;
    }
    hrFirst = IDXGISwapChain1_Present(pSwapChain->swapChain, 0U, uFlags);
    ++pSwapChain->presentCount;

    if (fRestart || fVSync)
    {
        hrSecond = IDXGISwapChain1_Present(pSwapChain->swapChain, 1U, DXGI_PRESENT_DO_NOT_SEQUENCE);
    }
    else
    {
        hrSecond = IDXGISwapChain1_Present(pSwapChain->swapChain, 0U, DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_DO_NOT_WAIT);
    }
    ++pSwapChain->presentCount;

    ASSERT(SUCCEEDED(hrFirst) || (DXGI_ERROR_WAS_STILL_DRAWING == hrFirst));
    ASSERT(SUCCEEDED(hrSecond) || (DXGI_ERROR_WAS_STILL_DRAWING == hrSecond));
    return (S_OK == hrFirst) && (S_OK == hrSecond);
}

void DestroySwapChain(SWAPCHAIN* pSwapChain)
{
    cdxgiUnknownReleaseSafe((IUnknown*)pSwapChain->visual);
    cdxgiUnknownReleaseSafe((IUnknown*)pSwapChain->compositionTarget);
    cdxgiUnknownReleaseSafe((IUnknown*)pSwapChain->compositionDevice);
    cdxgiUnknownReleaseSafe((IUnknown*)pSwapChain->context);
    cdxgiUnknownReleaseSafe((IUnknown*)pSwapChain->swapChain);
    cdxgiUnknownReleaseSafe((IUnknown*)pSwapChain->dxgiDevice);
    cdxgiUnknownReleaseSafe((IUnknown*)pSwapChain->device);
}
// NOLINTEND(misc-include-cleaner)
