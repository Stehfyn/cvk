#ifndef CVK_IMMERSIVE_PRESENTATION_H
#define CVK_IMMERSIVE_PRESENTATION_H

#include "immersive_window.h"
#include <d3d11.h>

/* The callback fills the complete native-size video texture at the supplied offset.
** The texture is borrowed only for the duration of the callback. */
typedef void (*IMMERSIVEVIDEOPROC)(HWND hWnd, ID3D11Texture2D* texture, UINT subresource, UINT x, UINT y);

BOOL GetImmersiveDevice(HWND hWnd, ID3D11Device** device, ID3D11DeviceContext** context);
void SetImmersiveVideo(HWND hWnd, UINT width, UINT height, IMMERSIVEVIDEOPROC draw);
void UpdateImmersiveVideo(HWND hWnd);

#endif
