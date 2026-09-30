#ifndef IMMERSIVE_OPUS3_SWAPCHAIN_H
#define IMMERSIVE_OPUS3_SWAPCHAIN_H

#include <Windows.h>
#include <d3d11.h>
#include <dxgi1_3.h>
#include <cd2d.h>
#include <cdcomp.h>

/* The presenter's GPU stack: a BGRA/video-capable D3D11 device, a desktop-sized premultiplied
   FLIP_DISCARD composition swap chain with one frame of latency, a Direct2D device context targeting its
   back buffer, and the DirectComposition device / target / visual that bind the swap chain to the
   window. Zero-initialize, then call InitSwapChain before binding the DirectComposition visual. */
typedef struct SWAPCHAIN
{
    ID3D11Device*        device;
    IDXGIDevice1*        dxgiDevice;
    IDXGISwapChain1*     swapChain;
    ID2D1DeviceContext*  context;           /* targets the swap chain's back buffer */
    IDCompositionDevice* compositionDevice;
    IDCompositionTarget* compositionTarget;
    IDCompositionVisual* visual;            /* content: the swap chain                */
    /* Frame accounting, for a debugger (volatile so the stores survive). */
    volatile ULONGLONG   presentCount;
    volatile ULONGLONG   commitCount;
} SWAPCHAIN;

void
InitSwapChain(
    SWAPCHAIN*     swapChain,
    IDXGIAdapter*  adapter);

void
DestroySwapChain(
    SWAPCHAIN* swapChain);

BOOL
PresentSwapChain(
    SWAPCHAIN* swapChain,
    BOOL       restart,
    BOOL       vsync);

#endif /* IMMERSIVE_OPUS3_SWAPCHAIN_H */
