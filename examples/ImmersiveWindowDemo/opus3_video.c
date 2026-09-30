#include "opus3_video.h"
#include "video.h"
#include "nt_assert.h"
#include "cd3d11.h"
#include "cdxgi.h"
#include "cd2d.h"
#include <d3d11.h>
#include <dxgi.h>
#include <dxgiformat.h>
#include <dcommon.h>
#include <minwindef.h>
#include <windef.h>
#include <winnt.h>
#include <winerror.h>
#include <Unknwnbase.h>
#include <profileapi.h>

#define VIDEO_100NS_PER_SECOND 10000000
#define VIDEO_DPI              96.0F
#define VIDEO_CENTER           0.5F
#define VIDEO_MAX_CATCHUP      4

void InitVideo(VIDEO* video, LPCWSTR path, ID3D11Device* device)
{
    BOOL             frameReady;
    BOOL             frequencyAvailable;
    BOOL             startTimeAvailable;

    *video = (VIDEO){0};
    cd3d11DeviceGetImmediateContext(device, &video->immediateContext);
    ASSERT(NULL != video->immediateContext);
    video->stream = VideoStream_Open(device, video->immediateContext, path);
    ASSERT(NULL != video->stream);
    VideoStream_GetInfo(video->stream, &video->info);
    ASSERT((0 < video->info.Width) && (0 < video->info.Height));
    ASSERT((0 < video->info.FrameRateNumerator) && (0 < video->info.FrameRateDenominator));
    cd3d11SetupShaderResourceTexture2D(device, video->info.Width, video->info.Height, 1, DXGI_FORMAT_B8G8R8A8_UNORM, NULL, &video->texture);
    ASSERT(NULL != video->texture);
    frameReady = VideoStream_NextFrame(video->stream);
    ASSERT(frameReady);
    VideoStream_CopyFrame(video->stream, video->texture, 0, 0, 0);
    frequencyAvailable = QueryPerformanceFrequency(&video->frequency);
    ASSERT(frequencyAvailable);
    startTimeAvailable = QueryPerformanceCounter(&video->startTime);
    ASSERT(startTimeAvailable);
}

void PrepareVideoFrame(VIDEO* video, ID2D1DeviceContext* context, LONGLONG leadTime)
{
    D2D1_BITMAP_PROPERTIES1 properties;
    D2D1_PIXEL_FORMAT       format;
    IDXGISurface*           surface;
    LARGE_INTEGER           now;
    LONGLONG                elapsed;
    LONGLONG                due;
    BOOL                    counterAvailable;
    BOOL                    frameReady;
    BOOL                    frameAdvanced;
    UINT                    catchup;
    HRESULT                 result;

    if (NULL == video->bitmap)
    {
        result = cdxgiUnknownQueryInterfaceAsSurface((IUnknown*)video->texture, &surface);
        ASSERT(SUCCEEDED(result));
        format.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
        format.alphaMode = D2D1_ALPHA_MODE_IGNORE;
        cd2dInitBitmapProperties1(&properties, format, VIDEO_DPI, VIDEO_DPI, D2D1_BITMAP_OPTIONS_NONE, NULL);
        cd2dDeviceContextCreateBitmapFromDxgiSurface(context, surface, &properties, &video->bitmap);
        cdxgiUnknownRelease((IUnknown*)surface);
        ASSERT(NULL != video->bitmap);
    }
    counterAvailable = QueryPerformanceCounter(&now);
    ASSERT(counterAvailable);
    elapsed       = ((now.QuadPart - video->startTime.QuadPart) * VIDEO_100NS_PER_SECOND) / video->frequency.QuadPart;
    due           = ((elapsed + leadTime) * video->info.FrameRateNumerator) / ((LONGLONG)VIDEO_100NS_PER_SECOND * video->info.FrameRateDenominator);
    frameAdvanced = FALSE;
    for (catchup = 0; (due > video->shown) && (catchup < VIDEO_MAX_CATCHUP); ++catchup)
    {
        frameReady = VideoStream_NextFrame(video->stream);
        if (frameReady)
        {
            ++video->shown;
            frameAdvanced = TRUE;
        }
        else
        {
            break;
        }
    }
    if (frameAdvanced)
    {
        VideoStream_CopyFrame(video->stream, video->texture, 0, 0, 0);
        VideoStream_Prefetch(video->stream, 0);
    }
}

extern FORCEINLINE void DrawVideoFrame(VIDEO* video, ID2D1DeviceContext* context, const RECT* client, LONG captionHeight)
{
    D2D1_RECT_F destination;
    FLOAT       width;
    FLOAT       height;
    FLOAT       widthScale;
    FLOAT       heightScale;
    FLOAT       scale;
    BOOL        hasArea;

    ASSERT(NULL != video->bitmap);
    width   = (FLOAT)(client->right - client->left);
    height  = (FLOAT)(client->bottom - client->top - captionHeight);
    hasArea = (0.0F < width) && (0.0F < height);
    if (hasArea)
    {
        widthScale         = width / (FLOAT)video->info.Width;
        heightScale        = height / (FLOAT)video->info.Height;
        scale              = (widthScale < heightScale) ? widthScale : heightScale;
        destination.left   = VIDEO_CENTER * (width - ((FLOAT)video->info.Width * scale));
        destination.top    = (FLOAT)captionHeight + (VIDEO_CENTER * (height - ((FLOAT)video->info.Height * scale)));
        destination.right  = destination.left + ((FLOAT)video->info.Width * scale);
        destination.bottom = destination.top + ((FLOAT)video->info.Height * scale);
        cd2dDeviceContextDrawBitmap(context, (ID2D1Bitmap*)video->bitmap, &destination, 1.0F, D2D1_INTERPOLATION_MODE_LINEAR, NULL, NULL);
    }
}
