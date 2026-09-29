/* Media Foundation decoding and CVK rendering on the window's graphics adapter. */
#ifndef IMMERSIVE_VIDEO_H
#define IMMERSIVE_VIDEO_H

#define WIN32_LEAN_AND_MEAN
#include <Windows.h> // IWYU pragma: keep
#include <d3d11.h>

typedef struct VideoStream VideoStream;

typedef struct VideoInfo
{
    UINT32   Width;
    UINT32   Height;
    UINT32   FrameRateNumerator;
    UINT32   FrameRateDenominator;
    LONGLONG Duration100ns;
} VideoInfo;

VideoStream* VideoStream_Open(ID3D11Device* device, ID3D11DeviceContext* context, LPCTSTR file);
void VideoStream_GetInfo(const VideoStream* stream, VideoInfo* info);
BOOL VideoStream_NextFrame(VideoStream* stream);
void VideoStream_Prefetch(VideoStream* stream, DWORD budgetMs);
void VideoStream_Draw(VideoStream* stream, ID3D11RenderTargetView* target, UINT width, UINT height);
/* Convert at native resolution and copy into the supplied GPU texture region.
** The destination format is BGRA8 and must contain the complete native-size frame. */
void VideoStream_CopyFrame(VideoStream* stream, ID3D11Texture2D* target, UINT subresource, UINT x, UINT y);
void VideoStream_Close(VideoStream* stream);

#endif
