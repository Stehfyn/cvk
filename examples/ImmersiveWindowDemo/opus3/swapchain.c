/*
 * swapchain.c -- the composition swap chain: the D3D11 device, the DXGI flip-model swap chain sized to
 * the desktop, the Direct2D context that draws into its back buffer, and the DirectComposition visual
 * that shows it in the window. The presenter renders through the context, presents, then commits.
 *
 * Every Win32/COM failure is fatal here (nt_assert.h ASSERT): the program has no fallback path, so each
 * HRESULT is captured and asserted rather than gated on.
 */

#pragma runtime_checks("", off)
#pragma check_stack(off)
#pragma strict_gs_check(off)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif

#include <windows.h>
#include <initguid.h>
#include "swapchain.h"
#include "nt_assert.h"

/* Swap chain: FLIP_DISCARD, presented immediately (tearing + mode switch allowed) with the frame-latency
   waitable object. It is sized to the desktop once and clipped by the window, so it is never resized. */
#define SWAPCHAIN_FLAGS             (DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT)
#define SWAPCHAIN_BUFFERS           5u
#define SWAPCHAIN_MAX_FRAME_LATENCY 1u

static FORCEINLINE void SwapChainRelease(IUnknown** ppUnk)
{
    if (*ppUnk)
    {
        (void)IUnknown_Release(*ppUnk);
        *ppUnk = NULL;
    }
}

/* Stand up the GPU stack: the D3D11 device, the composition swap chain with one frame of latency, and a
   Direct2D device context targeting its back buffer. */
void InitSwapChain(SWAPCHAIN* pSwapChain, IDXGIAdapter* adapter)
{
    IDXGIFactory2*          pFactory;
    IDXGISwapChain2*        pSwapChain2;
    IDXGISurface*           pBackBuffer;
    ID2D1Device*            pD2DDevice;
    ID2D1Bitmap1*           pTarget;
    DXGI_SWAP_CHAIN_DESC1   desc;
    D2D1_BITMAP_PROPERTIES1 bp;
    D3D_DRIVER_TYPE          driverType;
    HDC                     hdc;

    if (NULL != adapter)
    {
        driverType = D3D_DRIVER_TYPE_UNKNOWN;
    }
    else
    {
        driverType = D3D_DRIVER_TYPE_HARDWARE;
    }
    ASSERT(SUCCEEDED(
        D3D11CreateDevice(adapter, driverType, NULL, D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0u, D3D11_SDK_VERSION, &pSwapChain->device, NULL, NULL)));
    ASSERT(SUCCEEDED(ID3D11Device_QueryInterface(pSwapChain->device, &IID_IDXGIDevice1, (void**)&pSwapChain->dxgiDevice)));
    ASSERT(SUCCEEDED(CreateDXGIFactory2(0u, &IID_IDXGIFactory2, (void**)&pFactory)));

    hdc = GetDC(NULL);
    ASSERT(hdc);
    SecureZeroMemory(&desc, sizeof(desc));
    desc.Width            = (UINT)GetDeviceCaps(hdc, DESKTOPHORZRES);
    desc.Height           = (UINT)GetDeviceCaps(hdc, DESKTOPVERTRES);
    desc.Format           = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.Stereo           = FALSE;
    desc.SampleDesc.Count = 1u;
    desc.BufferUsage      = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount      = SWAPCHAIN_BUFFERS;
    desc.Scaling          = DXGI_SCALING_STRETCH;
    desc.SwapEffect       = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.AlphaMode        = DXGI_ALPHA_MODE_PREMULTIPLIED;
    desc.Flags            = SWAPCHAIN_FLAGS;
    ASSERT(ReleaseDC(NULL, hdc));

    ASSERT(SUCCEEDED(IDXGIFactory2_CreateSwapChainForComposition(pFactory, (IUnknown*)pSwapChain->dxgiDevice, &desc, NULL, &pSwapChain->swapChain)));
    ASSERT(SUCCEEDED(IDXGISwapChain1_QueryInterface(pSwapChain->swapChain, &IID_IDXGISwapChain2, (void**)&pSwapChain2)));
    ASSERT(SUCCEEDED(IDXGISwapChain2_SetMaximumFrameLatency(pSwapChain2, SWAPCHAIN_MAX_FRAME_LATENCY)));
    SwapChainRelease((IUnknown**)&pSwapChain2);
    SwapChainRelease((IUnknown**)&pFactory);

    ASSERT(SUCCEEDED(D2D1CreateDevice((IDXGIDevice*)pSwapChain->dxgiDevice, NULL, &pD2DDevice)));
    ASSERT(SUCCEEDED(ID2D1Device_CreateDeviceContext(pD2DDevice, D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &pSwapChain->context)));
    SwapChainRelease((IUnknown**)&pD2DDevice);

    ASSERT(SUCCEEDED(IDXGISwapChain1_GetBuffer(pSwapChain->swapChain, 0u, &IID_IDXGISurface, (void**)&pBackBuffer)));
    SecureZeroMemory(&bp, sizeof(bp));
    bp.pixelFormat.format    = desc.Format;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.dpiX                  = 96.0f;
    bp.dpiY                  = 96.0f;
    bp.bitmapOptions         = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ASSERT(SUCCEEDED(ID2D1DeviceContext_CreateBitmapFromDxgiSurface(pSwapChain->context, pBackBuffer, &bp, &pTarget)));
    SwapChainRelease((IUnknown**)&pBackBuffer);
    ID2D1DeviceContext_SetTarget(pSwapChain->context, (ID2D1Image*)pTarget);
    SwapChainRelease((IUnknown**)&pTarget);
}

/* Bind the swap chain to the window through DirectComposition: a device on the DXGI device, a target
   for the hwnd, and a single visual whose content is the swap chain. */
void BindSwapChain(SWAPCHAIN* pSwapChain, HWND hwnd)
{
    ASSERT(SUCCEEDED(DCompositionCreateDevice((IDXGIDevice*)pSwapChain->dxgiDevice, &IID_IDCompositionDevice, (void**)&pSwapChain->compositionDevice)));
    ASSERT(SUCCEEDED(IDCompositionDevice_CreateTargetForHwnd(pSwapChain->compositionDevice, hwnd, TRUE, &pSwapChain->compositionTarget)));
    ASSERT(SUCCEEDED(IDCompositionDevice_CreateVisual(pSwapChain->compositionDevice, &pSwapChain->visual)));
    ASSERT(SUCCEEDED(IDCompositionVisual_SetContent(pSwapChain->visual, (IUnknown*)pSwapChain->swapChain)));
    ASSERT(SUCCEEDED(IDCompositionTarget_SetRoot(pSwapChain->compositionTarget, pSwapChain->visual)));
}

/* Two presents per frame: an immediate (tearing, non-blocking) present of the new frame, then either a
   sequenced vsync present (fRestart / fVSync) or a second immediate one. DXGI_ERROR_WAS_STILL_DRAWING
   is DO_NOT_WAIT's "not yet" and is tolerated; TRUE only when both presents fully succeeded. */
BOOL PresentSwapChain(SWAPCHAIN* pSwapChain, BOOL fRestart, BOOL fVSync)
{
    HRESULT hrFirst;
    HRESULT hrSecond;
    UINT    uFlags;

    uFlags = DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_DO_NOT_WAIT;
    if (fRestart)
    {
        uFlags |= DXGI_PRESENT_RESTART;
    }
    hrFirst = IDXGISwapChain1_Present(pSwapChain->swapChain, 0u, uFlags);
    ++pSwapChain->presentCount;

    if (fRestart || fVSync)
    {
        hrSecond = IDXGISwapChain1_Present(pSwapChain->swapChain, 1u, DXGI_PRESENT_DO_NOT_SEQUENCE);
    }
    else
    {
        hrSecond = IDXGISwapChain1_Present(pSwapChain->swapChain, 0u, DXGI_PRESENT_ALLOW_TEARING | DXGI_PRESENT_DO_NOT_WAIT);
    }
    ++pSwapChain->presentCount;

    ASSERT(SUCCEEDED(hrFirst) || (DXGI_ERROR_WAS_STILL_DRAWING == hrFirst));
    ASSERT(SUCCEEDED(hrSecond) || (DXGI_ERROR_WAS_STILL_DRAWING == hrSecond));
    return (S_OK == hrFirst) && (S_OK == hrSecond);
}

/* Publish the frame: set the visual's border mode, then commit the DComp transaction. */
void CommitSwapChain(SWAPCHAIN* pSwapChain, DCOMPOSITION_BORDER_MODE borderMode)
{
    ASSERT(SUCCEEDED(IDCompositionVisual_SetBorderMode(pSwapChain->visual, borderMode)));
    ASSERT(SUCCEEDED(IDCompositionDevice_Commit(pSwapChain->compositionDevice)));
    ++pSwapChain->commitCount;
}

/* Block until DWM has taken the last commit. */
void WaitForSwapChainCommit(SWAPCHAIN* pSwapChain)
{
    ASSERT(SUCCEEDED(IDCompositionDevice_WaitForCommitCompletion(pSwapChain->compositionDevice)));
}
