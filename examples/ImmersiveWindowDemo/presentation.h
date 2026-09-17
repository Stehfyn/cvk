#ifndef CVK_IMMERSIVE_PRESENTATION_H
#define CVK_IMMERSIVE_PRESENTATION_H

#include "immersive_window.h"
struct ID3D11RenderTargetView;

/* All drawing uses one size snapshot, including a proposed resize size. */
BOOL GetImmersivePaintSize(HWND hWnd, SIZE* size);
/* Returns an owned reference to the offscreen video + caption render target. */
struct ID3D11RenderTargetView* GetImmersiveFrameTarget(HWND hWnd);

#endif
