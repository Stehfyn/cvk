#ifndef IMMERSIVE_RENDERER_H
#define IMMERSIVE_RENDERER_H

#include "video.h"

#define VIDEO_RENDERER_FRAMES 2

typedef struct VideoRenderer VideoRenderer;

VideoRenderer* VideoRenderer_Create(ID3D11Device* pDevice, ID3D11DeviceContext* pContext);
void VideoRenderer_SetSize(VideoRenderer* pRenderer, UINT width, UINT height);
ID3D11Texture2D* VideoRenderer_GetFrame(VideoRenderer* pRenderer, int slot);
void VideoRenderer_BeginUpload(VideoRenderer* pRenderer, int slot);
void VideoRenderer_EndUpload(VideoRenderer* pRenderer, int slot);
void VideoRenderer_Draw(VideoRenderer* pRenderer, ID3D11Texture2D* pTarget, int slot, BOOL convert, UINT width, UINT height);
void VideoRenderer_CopyFrame(VideoRenderer* r, ID3D11Texture2D* target, UINT subresource, UINT x, UINT y, int slot);
void VideoRenderer_Destroy(VideoRenderer* pRenderer);

#endif
