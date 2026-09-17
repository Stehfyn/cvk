/* GPU integration test: render the same decoded frames through the original
** CDX implementation and the CVK port, then compare their BGRA pixels. */
#include "video.h"
#include "cdx11.h"
#include "cdxgi.h"
#include <stdio.h>
#include <stdlib.h>

VideoStream* ReferenceVideoStream_Open(ID3D11Device*, ID3D11DeviceContext*, LPCWSTR);
BOOL ReferenceVideoStream_NextFrame(VideoStream*);
void ReferenceVideoStream_Draw(VideoStream*, ID3D11RenderTargetView*, UINT, UINT);
void ReferenceVideoStream_Close(VideoStream*);

static int Compare(ID3D11Device* device, ID3D11DeviceContext* context, VideoStream* reference, VideoStream* port, UINT width, UINT height, int frame)
{
    ID3D11Texture2D*         textures[2];
    ID3D11Texture2D*         staging[2];
    ID3D11RenderTargetView*  targets[2];
    D3D11_MAPPED_SUBRESOURCE mapped[2];
    DXGI_SAMPLE_DESC         sample        = {1, 0};
    unsigned                 maxDifference = 0;
    size_t                   different     = 0;
    int                      i;
    UINT                     x, y;
    UINT                     backingWidth = max(width, 1920), backingHeight = max(height, 1080);
    size_t                   transparent = 0;

    for (i = 0; i < 2; ++i)
    {
        cdx11DeviceCreateTexture2D(device, backingWidth, backingHeight, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, sample, D3D11_USAGE_DEFAULT, D3D11_BIND_RENDER_TARGET, 0, 0, NULL, &textures[i]);
        cdx11DeviceCreateRenderTargetView(device, (ID3D11Resource*)textures[i], NULL, &targets[i]);
        cdx11DeviceCreateTexture2D(device, backingWidth, backingHeight, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, sample, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, NULL, &staging[i]);
    }
    /* Poison the destination: every pixel, including the resize margin, must
    ** be replaced by opaque video rather than expose the desktop through it. */
    {
        const FLOAT poison[4] = {1, 0, 1, 0};
        cdx11DeviceContextClearRenderTargetView(context, targets[1], poison);
    }
    ReferenceVideoStream_Draw(reference, targets[0], width, height);
    VideoStream_Draw(port, targets[1], width, height);
    for (i = 0; i < 2; ++i)
    {
        cdx11DeviceContextCopyResource(context, (ID3D11Resource*)staging[i], (ID3D11Resource*)textures[i]);
        if (FAILED(cdx11DeviceContextMap(context, (ID3D11Resource*)staging[i], 0, D3D11_MAP_READ, 0, &mapped[i])))
            return 1;
    }
    for (y = 0; y < height; ++y)
    {
        const BYTE* a = (const BYTE*)mapped[0].pData + y * mapped[0].RowPitch;
        const BYTE* b = (const BYTE*)mapped[1].pData + y * mapped[1].RowPitch;
        for (x = 0; x < width * 4; ++x)
        {
            unsigned difference = (unsigned)abs((int)a[x] - (int)b[x]);
            if (difference > maxDifference)
                maxDifference = difference;
            if (difference > 3)
                ++different;
        }
    }
    for (y = 0; y < backingHeight; ++y)
    {
        const BYTE* row = (const BYTE*)mapped[1].pData + y * mapped[1].RowPitch;
        for (x = 0; x < backingWidth; ++x)
            if (row[x * 4 + 3] != 255)
                ++transparent;
    }
    for (i = 0; i < 2; ++i)
    {
        cdx11DeviceContextUnmap(context, (ID3D11Resource*)staging[i], 0);
        cdxgiUnknownRelease((IUnknown*)targets[i]);
        cdxgiUnknownRelease((IUnknown*)textures[i]);
        cdxgiUnknownRelease((IUnknown*)staging[i]);
    }
    printf("frame %d, %ux%u: max channel difference %u; channels differing by >3: %zu\n", frame, width, height, maxDifference, different);
    /* Independent APIs can round filtering and UNORM conversion differently. */
    if (transparent)
        fprintf(stderr, "Unpainted resize margin: %zu pixels\n", transparent);
    return different != 0 || transparent != 0;
}

int wmain(int argc, wchar_t** argv)
{
    ID3D11Device*        device;
    ID3D11DeviceContext* context;
    D3D_FEATURE_LEVEL    level;
    VideoStream*         reference;
    VideoStream*         port;
    VideoInfo            info;
    int                  frame, failed = 0;

    if (argc != 2)
        return 2;
    if (FAILED(CoInitializeEx(NULL, COINIT_APARTMENTTHREADED)))
        return 2;
    if (FAILED(cdx11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, D3D11_CREATE_DEVICE_BGRA_SUPPORT, NULL, 0, D3D11_SDK_VERSION, &device, &level, &context)))
        return 2;
    reference = ReferenceVideoStream_Open(device, context, argv[1]);
    port      = VideoStream_Open(device, context, argv[1]);
    if (!reference || !port)
        return 2;
    VideoStream_GetInfo(port, &info);
    /* A two-second fixture loops during this run. Repeated draws exercise the
    ** paused-frame cache; changing target dimensions exercises shared output recreation. */
    for (frame = 0; frame < 65 && !failed; ++frame)
    {
        int  retry;
        BOOL haveReference = FALSE, havePort = FALSE;
        /* At EOF a zero-budget read can rewind without delivering a sample. */
        for (retry = 0; retry < 10 && !haveReference; ++retry)
            haveReference = ReferenceVideoStream_NextFrame(reference);
        for (retry = 0; retry < 10 && !havePort; ++retry)
            havePort = VideoStream_NextFrame(port);
        if (!haveReference || !havePort)
        {
            failed = 1;
            break;
        }
        failed |= Compare(device, context, reference, port, info.Width, info.Height, frame);
        if (frame == 0 || frame == 64)
        {
            failed |= Compare(device, context, reference, port, 1280, 720, frame);
            failed |= Compare(device, context, reference, port, 321, 181, frame);
            failed |= Compare(device, context, reference, port, info.Width, info.Height, frame);
        }
    }
    VideoStream_Close(port);
    ReferenceVideoStream_Close(reference);
    cdxgiUnknownRelease((IUnknown*)context);
    cdxgiUnknownRelease((IUnknown*)device);
    CoUninitialize();
    return failed;
}
