#ifndef CVK_IMMERSIVE_OPUS3_VIDEO_H
#define CVK_IMMERSIVE_OPUS3_VIDEO_H

#include "video.h"
#include "cd2d.h"

typedef struct VIDEO
{
    VideoStream*         stream;
    ID3D11DeviceContext* immediateContext;
    ID3D11Texture2D*     texture;
    ID2D1Bitmap1*        bitmap;
    VideoInfo            info;
    LARGE_INTEGER        frequency;
    LARGE_INTEGER        startTime;
    LONGLONG             shown;
} VIDEO;

void InitVideo(VIDEO* video, LPCWSTR path, ID3D11Device* device);
void PrepareVideoFrame(VIDEO* video, ID2D1DeviceContext* context, LONGLONG leadTime);
void DrawVideoFrame(VIDEO* video, ID2D1DeviceContext* context, const RECT* client, LONG captionHeight);

#endif
