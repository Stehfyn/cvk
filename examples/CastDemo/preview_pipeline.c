#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <Windows.h> // IWYU pragma: keep
#include <winnt.h>
#include <minwindef.h>
#include <basetsd.h>
#include <minwinbase.h>
#include <WinBase.h>
#include <memoryapi.h>
#include <sysinfoapi.h>
#include <synchapi.h>
#include <combaseapi.h>
#include <objbase.h>
#include <Unknwn.h>
#include <wtypes.h>
#include <WTypesbase.h>
#include <oaidl.h>
#include <guiddef.h>
#include <coguid.h>
#include <winerror.h>
#include <strmif.h>
#include <mfobjects.h>
#include <mfapi.h>
#include <mfidl.h> // IWYU pragma: keep
#include <mfreadwrite.h>
#include <mftransform.h>
#include <Mferror.h>
#include <wmcodecdsp.h>
#include <codecapi.h>
#include <stddef.h>
#include <stdint.h>
#include <cmf.h>
#include "preview_pipeline.h"
#include "crt/crt_memory.h"
#include "gpu_transcode.h"
#include "os/os_thread.h"
#include "os/os_types.h"

#define PREVIEW_FPS                  (24U)
#define PREVIEW_FRAME_TIME          (10000000ULL / PREVIEW_FPS)
#define PREVIEW_FRAME_DELAY_MS      (1000U / PREVIEW_FPS)
#define PREVIEW_MAX_DIMENSION       (4096U)
#define PREVIEW_MAX_FRAME_BYTES     (67108864ULL)
#define PREVIEW_HEADER_CAPACITY     (2048U)
#define PREVIEW_MIN_BITRATE         (1000000U)
#define PREVIEW_MAX_QP              (51U)
#define PREVIEW_MIN_QP              (16U)
#define PREVIEW_MOTION_QP_RANGE     (8U)
#define PREVIEW_IDLE_DELAY_MS       (20U)
#define PREVIEW_SIZE_SHIFT          (32U)
#define PREVIEW_QP_I_SHIFT          (16U)
#define PREVIEW_QP_P_SHIFT          (32U)
#define PREVIEW_QP_B_SHIFT          (48U)
#define PREVIEW_KBPS_TO_BPS         (1000U)
#define PREVIEW_PIXEL_MAX           (255)
#define PREVIEW_PERCENT            (100U)
#define PREVIEW_MOTION_ROUND        (5000U)
#define PREVIEW_MOTION_DENOMINATOR  (10000U)
#define PREVIEW_Y_OFFSET           (16)
#define PREVIEW_CHROMA_OFFSET      (128)
#define PREVIEW_COLOR_ROUND        (128)
#define PREVIEW_COLOR_SHIFT        (8)
#define PREVIEW_CHROMA_ROUND       (512)
#define PREVIEW_CHROMA_SHIFT       (10)
#define PREVIEW_SAMPLE_MASK        (15U)
#define PREVIEW_RGB_Y_R            (66)
#define PREVIEW_RGB_Y_G            (129)
#define PREVIEW_RGB_Y_B            (25)
#define PREVIEW_RGB_U_R            (38)
#define PREVIEW_RGB_U_G            (74)
#define PREVIEW_RGB_U_B            (112)
#define PREVIEW_RGB_V_R            (112)
#define PREVIEW_RGB_V_G            (94)
#define PREVIEW_RGB_V_B            (18)
#define PREVIEW_YUV_RGB_Y          (298)
#define PREVIEW_YUV_RGB_BU         (516)
#define PREVIEW_YUV_RGB_GU         (100)
#define PREVIEW_YUV_RGB_GV         (208)
#define PREVIEW_YUV_RGB_RV         (409)

typedef struct PreviewCodec
{
    IMFSourceReader* reader;
    IMFTransform*    encoder;
    IMFTransform*    decoder;
    ICodecAPI*       codecApi;
    BYTE             header[PREVIEW_HEADER_CAPACITY];
    UINT32           headerLength;
    UINT32           sourceWidth;
    UINT32           sourceHeight;
    LONG             sourceStride;
    UINT32           width;
    UINT32           height;
    UINT32           decoderStride;
    UINT32           encodedCapacity;
    UINT32           decodedCapacity;
    BYTE*            previousLuma;
    BYTE*            bgra;
    GpuTranscodeConfig config;
    ULONGLONG        frameIndex;
    LONGLONG         playbackPosition;
    bool_t           decoderStarted;
} PreviewCodec;

static void PreviewCodec_Close(PreviewCodec* codec)
{
    if (NULL != codec->previousLuma)
    {
        (void)VirtualFree(codec->previousLuma, 0, MEM_RELEASE);
    }
    if (NULL != codec->bgra)
    {
        (void)VirtualFree(codec->bgra, 0, MEM_RELEASE);
    }
    if (NULL != codec->codecApi)
    {
        cmfUnknownRelease((IUnknown*)codec->codecApi);
    }
    if (NULL != codec->decoder)
    {
        cmfUnknownRelease((IUnknown*)codec->decoder);
    }
    if (NULL != codec->encoder)
    {
        cmfUnknownRelease((IUnknown*)codec->encoder);
    }
    if (NULL != codec->reader)
    {
        cmfUnknownRelease((IUnknown*)codec->reader);
    }
    ZeroMemory(codec, sizeof(*codec));
}

static HRESULT PreviewCodec_MakeType(
    const GUID*    subtype,
    UINT32         width,
    UINT32         height,
    IMFMediaType** result)
{
    IMFMediaType* type;
    UINT64        size;
    UINT64        rate;
    HRESULT       hr;

    type = NULL;
    size = ((UINT64)width << PREVIEW_SIZE_SHIFT) | height;
    rate = ((UINT64)PREVIEW_FPS << PREVIEW_SIZE_SHIFT) | 1U;
    hr   = MFCreateMediaType(&type);
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaType_SetGUID(type, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaType_SetGUID(type, &MF_MT_SUBTYPE, subtype);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaType_SetUINT64(type, &MF_MT_FRAME_SIZE, size);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaType_SetUINT64(type, &MF_MT_FRAME_RATE, rate);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaType_SetUINT32(type, &MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaType_SetUINT64(type, &MF_MT_PIXEL_ASPECT_RATIO, ((UINT64)1U << 32U) | 1U);
    }
    if (FAILED(hr))
    {
        if (NULL != type)
        {
            cmfUnknownRelease((IUnknown*)type);
        }
        type = NULL;
    }
    *result = type;
    return hr;
}

static HRESULT PreviewCodec_OpenReader(PreviewCodec* codec, const WCHAR* path)
{
    IMFAttributes* attributes;
    IMFMediaType*  requested;
    IMFMediaType*  current;
    UINT64         size;
    UINT32         stride;
    HRESULT        hr;

    size       = 0;
    attributes = NULL;
    requested  = NULL;
    current    = NULL;
    hr         = MFCreateAttributes(&attributes, 1);
    if (SUCCEEDED(hr))
    {
        hr = IMFAttributes_SetUINT32(attributes, &MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    }
    if (SUCCEEDED(hr))
    {
        hr = MFCreateSourceReaderFromURL(path, attributes, &codec->reader);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFSourceReader_SetStreamSelection(codec->reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFSourceReader_SetStreamSelection(codec->reader, (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
    }
    if (SUCCEEDED(hr))
    {
        hr = MFCreateMediaType(&requested);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaType_SetGUID(requested, &MF_MT_MAJOR_TYPE, &MFMediaType_Video);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaType_SetGUID(requested, &MF_MT_SUBTYPE, &MFVideoFormat_RGB32);
    }
    if (SUCCEEDED(hr))
    {
        hr = cmfSourceReaderSetCurrentMediaType(codec->reader,
                                                  (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                  NULL,
                                                  requested);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFSourceReader_GetCurrentMediaType(codec->reader,
                                                  (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                  &current);
    }
    if (SUCCEEDED(hr))
    {
        hr = cmfAttributesGetUINT64((IMFAttributes*)current, &MF_MT_FRAME_SIZE, &size);
    }
    if (SUCCEEDED(hr))
    {
            codec->sourceWidth  = (UINT32)(size >> PREVIEW_SIZE_SHIFT);
        codec->sourceHeight = (UINT32)size;
        codec->sourceStride = (LONG)((UINT64)codec->sourceWidth * 4U);
        if (SUCCEEDED(cmfAttributesGetUINT32((IMFAttributes*)current, &MF_MT_DEFAULT_STRIDE, &stride)))
        {
            codec->sourceStride = (LONG)stride;
        }
        if ((0 == codec->sourceWidth) || (0 == codec->sourceHeight) ||
            (PREVIEW_MAX_DIMENSION < codec->sourceWidth) ||
            (PREVIEW_MAX_DIMENSION < codec->sourceHeight) ||
            (0 == codec->sourceStride))
        {
            hr = MF_E_INVALIDMEDIATYPE;
        }
    }
    if (NULL != current)
    {
        cmfUnknownRelease((IUnknown*)current);
    }
    if (NULL != requested)
    {
        cmfUnknownRelease((IUnknown*)requested);
    }
    if (NULL != attributes)
    {
        cmfUnknownRelease((IUnknown*)attributes);
    }
    return hr;
}

static HRESULT PreviewCodec_SetProperty(
    ICodecAPI*   api,
    const GUID* property,
    VARTYPE     type,
    UINT64      value)
{
    VARIANT parameter;

    ZeroMemory(&parameter, sizeof(parameter));
    parameter.vt = type;
    if (VT_UI8 == type)
    {
        parameter.ullVal = value;
    }
    else
    {
        parameter.ulVal = (ULONG)value;
    }
    return ICodecAPI_SetValue(api, property, &parameter);
}

/* Each negotiation step preserves its HRESULT so unsupported encoders fail without terminating the UI. */
static HRESULT PreviewCodec_OpenEncoder(PreviewCodec* codec) // NOLINT(readability-function-cognitive-complexity)
{
    IMFMediaType*         output;
    IMFMediaType*         input;
    MFT_OUTPUT_STREAM_INFO stream;
    UINT32                bitrate;
    UINT64                qp;
    UINT32                lumaBytes;
    HRESULT               hr;

    output = NULL;
    input  = NULL;
    ZeroMemory(&stream, sizeof(stream));
    hr = CoCreateInstance(&CLSID_CMSH264EncoderMFT,
                          NULL,
                          CLSCTX_INPROC_SERVER,
                          &IID_IMFTransform,
                          (void**)&codec->encoder);
    if (SUCCEEDED(hr))
    {
        hr = IMFTransform_QueryInterface(codec->encoder, &IID_ICodecAPI, (void**)&codec->codecApi);
    }
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_SetProperty(codec->codecApi,
                                      &CODECAPI_AVEncCommonRateControlMode,
                                      VT_UI4,
                                      eAVEncCommonRateControlMode_Quality);
    }
    qp = (UINT64)(UINT32)codec->config.qpInter;
    qp |= ((UINT64)(UINT32)((PREVIEW_MIN_QP > (UINT32)codec->config.qpIntra) ?
                            PREVIEW_MIN_QP : (UINT32)codec->config.qpIntra) << PREVIEW_QP_I_SHIFT);
    qp |= ((UINT64)(UINT32)codec->config.qpInter << PREVIEW_QP_P_SHIFT);
    qp |= ((UINT64)(UINT32)codec->config.qpInter << PREVIEW_QP_B_SHIFT);
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_SetProperty(codec->codecApi, &CODECAPI_AVEncVideoEncodeQP, VT_UI8, qp);
    }
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_SetProperty(codec->codecApi,
                                      &CODECAPI_AVEncMPVGOPSize,
                                      VT_UI4,
                                      codec->config.gopLength);
    }
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_MakeType(&MFVideoFormat_H264, codec->width, codec->height, &output);
    }
    bitrate = codec->config.bitrateKbps * PREVIEW_KBPS_TO_BPS;
    if (PREVIEW_MIN_BITRATE > bitrate)
    {
        bitrate = PREVIEW_MIN_BITRATE;
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaType_SetUINT32(output, &MF_MT_AVG_BITRATE, bitrate);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFTransform_SetOutputType(codec->encoder, 0, output, 0);
    }
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_MakeType(&MFVideoFormat_NV12, codec->width, codec->height, &input);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFTransform_SetInputType(codec->encoder, 0, input, 0);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFTransform_GetOutputStreamInfo(codec->encoder, 0, &stream);
    }
    if (SUCCEEDED(hr))
    {
        codec->encodedCapacity = stream.cbSize;
        if (0 == codec->encodedCapacity)
        {
            codec->encodedCapacity = codec->width * codec->height;
        }
        if (PREVIEW_MAX_FRAME_BYTES < codec->encodedCapacity)
        {
            hr = MF_E_BUFFERTOOSMALL;
        }
    }
    if (SUCCEEDED(hr))
    {
        hr = cmfAttributesGetBlobSize((IMFAttributes*)output, &MF_MT_MPEG_SEQUENCE_HEADER, &codec->headerLength);
    }
    if (SUCCEEDED(hr))
    {
        if (PREVIEW_HEADER_CAPACITY < codec->headerLength)
        {
            hr = MF_E_BUFFERTOOSMALL;
        }
        else
        {
            hr = cmfAttributesGetBlob((IMFAttributes*)output,
                                      &MF_MT_MPEG_SEQUENCE_HEADER,
                                      codec->header,
                                      PREVIEW_HEADER_CAPACITY,
                                      &codec->headerLength);
        }
    }
    lumaBytes = codec->width * codec->height;
    if (SUCCEEDED(hr))
    {
        codec->previousLuma = (BYTE*)VirtualAlloc(NULL, lumaBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (NULL == codec->previousLuma)
        {
            hr = E_OUTOFMEMORY;
        }
    }
    if (SUCCEEDED(hr))
    {
        codec->bgra = (BYTE*)VirtualAlloc(NULL,
                                          (SIZE_T)codec->width * codec->height * 4U,
                                          MEM_COMMIT | MEM_RESERVE,
                                          PAGE_READWRITE);
        if (NULL == codec->bgra)
        {
            hr = E_OUTOFMEMORY;
        }
    }
    if (NULL != input)
    {
        cmfUnknownRelease((IUnknown*)input);
    }
    if (NULL != output)
    {
        cmfUnknownRelease((IUnknown*)output);
    }
    return hr;
}

static HRESULT PreviewCodec_OpenDecoder(PreviewCodec* codec)
{
    IMFMediaType*          input;
    IMFMediaType*          output;
    MFT_OUTPUT_STREAM_INFO stream;
    UINT32                 stride;
    HRESULT                hr;

    input  = NULL;
    output = NULL;
    ZeroMemory(&stream, sizeof(stream));
    hr = CoCreateInstance(&CLSID_CMSH264DecoderMFT,
                          NULL,
                          CLSCTX_INPROC_SERVER,
                          &IID_IMFTransform,
                          (void**)&codec->decoder);
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_MakeType(&MFVideoFormat_H264, codec->width, codec->height, &input);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFTransform_SetInputType(codec->decoder, 0, input, 0);
    }
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_MakeType(&MFVideoFormat_NV12, codec->width, codec->height, &output);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFTransform_SetOutputType(codec->decoder, 0, output, 0);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFTransform_GetOutputStreamInfo(codec->decoder, 0, &stream);
    }
    if (SUCCEEDED(hr))
    {
        codec->decodedCapacity = stream.cbSize;
        if (0 == codec->decodedCapacity)
        {
            codec->decodedCapacity = codec->width * codec->height * 2U;
        }
        codec->decoderStride = codec->width;
        if (SUCCEEDED(cmfAttributesGetUINT32((IMFAttributes*)output, &MF_MT_DEFAULT_STRIDE, &stride)))
        {
            codec->decoderStride = stride;
        }
        if ((PREVIEW_MAX_FRAME_BYTES < codec->decodedCapacity) ||
            (codec->decoderStride < codec->width))
        {
            hr = MF_E_BUFFERTOOSMALL;
        }
    }
    if (NULL != output)
    {
        cmfUnknownRelease((IUnknown*)output);
    }
    if (NULL != input)
    {
        cmfUnknownRelease((IUnknown*)input);
    }
    return hr;
}

static HRESULT PreviewCodec_Open(
    PreviewCodec*             codec,
    const WCHAR*              path,
    const GpuTranscodeConfig* config)
{
    UINT64 outputHeight;
    HRESULT hr;

    codec->config = *config;
    hr = PreviewCodec_OpenReader(codec, path);
    if (SUCCEEDED(hr))
    {
        codec->width = (config->width + 1U) & ~1U;
        outputHeight = (((UINT64)codec->width * codec->sourceHeight) + (codec->sourceWidth / 2U)) /
                       codec->sourceWidth;
        codec->height = ((UINT32)outputHeight + 1U) & ~1U;
        if ((PREVIEW_MAX_DIMENSION < codec->width) ||
            (PREVIEW_MAX_DIMENSION < codec->height) ||
            (PREVIEW_MAX_FRAME_BYTES < (UINT64)codec->width * codec->height * 4U))
        {
            hr = MF_E_INVALIDMEDIATYPE;
        }
    }
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_OpenEncoder(codec);
    }
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_OpenDecoder(codec);
    }
    return hr;
}

static BYTE Preview_Clamp(int value)
{
    BYTE result;

    if (0 > value)
    {
        result = 0;
    }
    else if (PREVIEW_PIXEL_MAX < value)
    {
        result = PREVIEW_PIXEL_MAX;
    }
    else
    {
        result = (BYTE)value;
    }
    return result;
}

static void PreviewCodec_ReadPixel(
    const PreviewCodec* codec,
    const BYTE*         firstRow,
    UINT32              x,
    UINT32              y,
    int*                red,
    int*                green,
    int*                blue)
{
    const BYTE* pixel;

    pixel  = firstRow + ((int64_t)y * codec->sourceStride) + ((size_t)x * 4U);
    *blue  = pixel[0];
    *green = pixel[1];
    *red   = pixel[2];
}

/* The nested 2x2 loops produce one chroma pair from four scaled RGB pixels. */
static HRESULT PreviewCodec_FillNv12( // NOLINT(readability-function-cognitive-complexity)
    PreviewCodec* codec,
    IMFSample*    source,
    BYTE*         target,
    UINT32*       motion)
{
    IMFMediaBuffer* buffer;
    BYTE*           bytes;
    const BYTE*     firstRow;
    BYTE*           chroma;
    DWORD           length;
    int64_t         strideMagnitude;
    UINT64          required;
    UINT64          difference;
    UINT32          samples;
    UINT32          y;
    HRESULT         hr;

    buffer     = NULL;
    bytes      = NULL;
    length     = 0;
    strideMagnitude = 0;
    difference = 0;
    samples    = 0;
    hr = IMFSample_ConvertToContiguousBuffer(source, &buffer);
    if (SUCCEEDED(hr))
    {
        hr = cmfMediaBufferLock(buffer, &bytes, NULL, &length);
    }
    if (SUCCEEDED(hr))
    {
        strideMagnitude = (0 > codec->sourceStride) ?
                          -(int64_t)codec->sourceStride : (int64_t)codec->sourceStride;
        required = ((UINT64)strideMagnitude * ((UINT64)codec->sourceHeight - 1U)) +
                   ((UINT64)codec->sourceWidth * 4U);
        if (required > length)
        {
            hr = MF_E_BUFFERTOOSMALL;
        }
    }
    if (SUCCEEDED(hr))
    {
        firstRow = bytes;
        if (0 > codec->sourceStride)
        {
            firstRow += (SIZE_T)(strideMagnitude * ((int64_t)codec->sourceHeight - 1));
        }
        chroma = target + ((size_t)codec->width * codec->height);
        for (y = 0; y < codec->height; y += 2U)
        {
            UINT32 x;

            for (x = 0; x < codec->width; x += 2U)
            {
                int redSum;
                int greenSum;
                int blueSum;
                UINT32 dy;

                redSum   = 0;
                greenSum = 0;
                blueSum  = 0;
                for (dy = 0; dy < 2U; ++dy)
                {
                    UINT32 dx;

                    for (dx = 0; dx < 2U; ++dx)
                    {
                        UINT32 sx;
                        UINT32 sy;
                        UINT32 destination;
                        int    red;
                        int    green;
                        int    blue;
                        int    luma;

                        sx = (UINT32)((((UINT64)x + dx) * codec->sourceWidth) / codec->width);
                        sy = (UINT32)((((UINT64)y + dy) * codec->sourceHeight) / codec->height);
                        destination = ((y + dy) * codec->width) + x + dx;
                        PreviewCodec_ReadPixel(codec, firstRow, sx, sy, &red, &green, &blue);
                        luma = PREVIEW_Y_OFFSET +
                               (((PREVIEW_RGB_Y_R * red) + (PREVIEW_RGB_Y_G * green) +
                                 (PREVIEW_RGB_Y_B * blue) + PREVIEW_COLOR_ROUND) >> PREVIEW_COLOR_SHIFT);
                        target[destination] = Preview_Clamp(luma);
                        redSum   += red;
                        greenSum += green;
                        blueSum  += blue;
                        if (0 != codec->frameIndex && (0 == (x & PREVIEW_SAMPLE_MASK)) &&
                            (0 == (y & PREVIEW_SAMPLE_MASK)))
                        {
                            int delta;

                            delta = (int)target[destination] - (int)codec->previousLuma[destination];
                            difference += (UINT64)((0 > delta) ? -delta : delta);
                            ++samples;
                        }
                        codec->previousLuma[destination] = target[destination];
                    }
                }
                chroma[((size_t)y / 2U * codec->width) + x] =
                    Preview_Clamp(PREVIEW_CHROMA_OFFSET +
                                  (((-PREVIEW_RGB_U_R * redSum) - (PREVIEW_RGB_U_G * greenSum) +
                                    (PREVIEW_RGB_U_B * blueSum) + PREVIEW_CHROMA_ROUND) >>
                                   PREVIEW_CHROMA_SHIFT));
                chroma[((size_t)y / 2U * codec->width) + x + 1U] =
                    Preview_Clamp(PREVIEW_CHROMA_OFFSET +
                                  (((PREVIEW_RGB_V_R * redSum) - (PREVIEW_RGB_V_G * greenSum) -
                                    (PREVIEW_RGB_V_B * blueSum) + PREVIEW_CHROMA_ROUND) >>
                                   PREVIEW_CHROMA_SHIFT));
            }
        }
    }
    if (NULL != bytes)
    {
        cmfMediaBufferUnlock(buffer);
    }
    if (NULL != buffer)
    {
        cmfUnknownRelease((IUnknown*)buffer);
    }
    *motion = (0 < samples) ?
              (UINT32)((difference * PREVIEW_PERCENT) / ((UINT64)samples * PREVIEW_PIXEL_MAX)) :
              PREVIEW_PERCENT;
    return hr;
}

static HRESULT PreviewCodec_CreateSample(
    DWORD            capacity,
    IMFSample**      sample,
    IMFMediaBuffer** buffer)
{
    HRESULT hr;

    *sample = NULL;
    *buffer = NULL;
    hr = MFCreateMemoryBuffer(capacity, buffer);
    if (SUCCEEDED(hr))
    {
        hr = MFCreateSample(sample);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFSample_AddBuffer(*sample, *buffer);
    }
    if (FAILED(hr))
    {
        if (NULL != *sample)
        {
            cmfUnknownRelease((IUnknown*)*sample);
            *sample = NULL;
        }
        if (NULL != *buffer)
        {
            cmfUnknownRelease((IUnknown*)*buffer);
            *buffer = NULL;
        }
    }
    return hr;
}

static HRESULT PreviewCodec_EncodeInput(
    PreviewCodec* codec,
    IMFSample*    source)
{
    IMFSample*      input;
    IMFMediaBuffer* buffer;
    BYTE*           target;
    DWORD           frameBytes;
    UINT32          motion;
    UINT32          qpInter;
    UINT32          qpIntra;
    UINT64          qp;
    HRESULT         hr;

    input      = NULL;
    buffer     = NULL;
    target     = NULL;
    motion     = PREVIEW_PERCENT;
    frameBytes = (codec->width * codec->height * 3U) / 2U;
    hr = PreviewCodec_CreateSample(frameBytes, &input, &buffer);
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaBuffer_Lock(buffer, &target, NULL, NULL);
    }
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_FillNv12(codec, source, target, &motion);
        (void)IMFMediaBuffer_Unlock(buffer);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaBuffer_SetCurrentLength(buffer, frameBytes);
    }
    qpInter = (UINT32)codec->config.qpInter;
    qpIntra = (UINT32)codec->config.qpIntra;
    if (PREVIEW_MIN_QP > qpIntra)
    {
        qpIntra = PREVIEW_MIN_QP;
    }
    if (codec->config.motionAdaptive)
    {
        UINT32 strength;
        UINT32 bump;

        strength = (UINT32)codec->config.motionStrength;
        bump = (((PREVIEW_PERCENT - motion) * strength * PREVIEW_MOTION_QP_RANGE) +
                PREVIEW_MOTION_ROUND) / PREVIEW_MOTION_DENOMINATOR;
        qpInter += bump;
        if (PREVIEW_MAX_QP < qpInter)
        {
            qpInter = PREVIEW_MAX_QP;
        }
    }
    qp = qpInter | ((UINT64)qpIntra << PREVIEW_QP_I_SHIFT) |
         ((UINT64)qpInter << PREVIEW_QP_P_SHIFT) | ((UINT64)qpInter << PREVIEW_QP_B_SHIFT);
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_SetProperty(codec->codecApi, &CODECAPI_AVEncVideoEncodeQP, VT_UI8, qp);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFSample_SetSampleTime(input, (LONGLONG)(codec->frameIndex * PREVIEW_FRAME_TIME));
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFSample_SetSampleDuration(input, (LONGLONG)PREVIEW_FRAME_TIME);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFTransform_ProcessInput(codec->encoder, 0, input, 0);
    }
    if (NULL != input)
    {
        cmfUnknownRelease((IUnknown*)input);
    }
    if (NULL != buffer)
    {
        cmfUnknownRelease((IUnknown*)buffer);
    }
    return hr;
}

static HRESULT PreviewPipeline_Publish(
    PreviewPipeline*    preview,
    const PreviewCodec* codec,
    IMFSample*          decoded)
{
    IMFMediaBuffer* buffer;
    BYTE*           source;
    BYTE*           replacement;
    DWORD           length;
    UINT64          required;
    UINT32          y;
    HRESULT         hr;

    buffer = NULL;
    source = NULL;
    length = 0;
    hr = IMFSample_ConvertToContiguousBuffer(decoded, &buffer);
    if (SUCCEEDED(hr))
    {
        hr = cmfMediaBufferLock(buffer, &source, NULL, &length);
    }
    required = (UINT64)codec->decoderStride * codec->height * 3U / 2U;
    if (SUCCEEDED(hr) && (required > length))
    {
        hr = MF_E_BUFFERTOOSMALL;
    }
    replacement = NULL;
    if (SUCCEEDED(hr) && ((codec->width != preview->width) || (codec->height != preview->height)))
    {
        replacement = (BYTE*)VirtualAlloc(NULL,
                                          (SIZE_T)codec->width * codec->height * 4U,
                                          MEM_COMMIT | MEM_RESERVE,
                                          PAGE_READWRITE);
        if (NULL == replacement)
        {
            hr = E_OUTOFMEMORY;
        }
    }
    if (SUCCEEDED(hr))
    {
        for (y = 0; y < codec->height; ++y)
        {
            UINT32 x;

            for (x = 0; x < codec->width; ++x)
            {
                int luma;
                int cb;
                int cr;
                BYTE* pixel;

                luma = (int)source[((size_t)y * codec->decoderStride) + x] - PREVIEW_Y_OFFSET;
                cb = (int)source[((size_t)codec->decoderStride * codec->height) +
                                 ((size_t)y / 2U * codec->decoderStride) + (x & ~1U)] -
                     PREVIEW_CHROMA_OFFSET;
                cr = (int)source[((size_t)codec->decoderStride * codec->height) +
                                 ((size_t)y / 2U * codec->decoderStride) + (x | 1U)] -
                     PREVIEW_CHROMA_OFFSET;
                if (0 > luma)
                {
                    luma = 0;
                }
                pixel = &codec->bgra[((size_t)y * codec->width * 4U) + ((size_t)x * 4U)];
                pixel[0] = Preview_Clamp(((PREVIEW_YUV_RGB_Y * luma) + (PREVIEW_YUV_RGB_BU * cb) +
                                          PREVIEW_COLOR_ROUND) >> PREVIEW_COLOR_SHIFT);
                pixel[1] = Preview_Clamp(((PREVIEW_YUV_RGB_Y * luma) - (PREVIEW_YUV_RGB_GU * cb) -
                                          (PREVIEW_YUV_RGB_GV * cr) + PREVIEW_COLOR_ROUND) >>
                                         PREVIEW_COLOR_SHIFT);
                pixel[2] = Preview_Clamp(((PREVIEW_YUV_RGB_Y * luma) + (PREVIEW_YUV_RGB_RV * cr) +
                                          PREVIEW_COLOR_ROUND) >> PREVIEW_COLOR_SHIFT);
                pixel[3] = PREVIEW_PIXEL_MAX;
            }
        }
        AcquireSRWLockExclusive(&preview->frameLock);
        if (NULL != replacement)
        {
            if (NULL != preview->pixels)
            {
                (void)VirtualFree(preview->pixels, 0, MEM_RELEASE);
            }
            preview->pixels = replacement;
            preview->width  = codec->width;
            preview->height = codec->height;
            preview->pitch  = codec->width * 4U;
        }
        CRT_Memory_Copy(preview->pixels,
                        codec->bgra,
                        (uint64_t)codec->width * codec->height * 4U);
        ++preview->frameGeneration;
        ReleaseSRWLockExclusive(&preview->frameLock);
    }
    if (NULL != source)
    {
        cmfMediaBufferUnlock(buffer);
    }
    if (NULL != buffer)
    {
        cmfUnknownRelease((IUnknown*)buffer);
    }
    return hr;
}

static HRESULT PreviewCodec_TakeOutput(
    IMFTransform* transform,
    DWORD         capacity,
    IMFSample**   result)
{
    MFT_OUTPUT_STREAM_INFO info;
    MFT_OUTPUT_DATA_BUFFER output;
    IMFSample*             supplied;
    IMFMediaBuffer*        buffer;
    DWORD                  status;
    bool_t                 suppliesSample;
    HRESULT                hr;

    *result = NULL;
    supplied = NULL;
    buffer   = NULL;
    ZeroMemory(&info, sizeof(info));
    hr = IMFTransform_GetOutputStreamInfo(transform, 0, &info);
    suppliesSample = SUCCEEDED(hr) &&
                     (0 != (info.dwFlags & (MFT_OUTPUT_STREAM_PROVIDES_SAMPLES | MFT_OUTPUT_STREAM_CAN_PROVIDE_SAMPLES)));
    if (SUCCEEDED(hr) && !suppliesSample)
    {
        if (info.cbSize > capacity)
        {
            capacity = info.cbSize;
        }
        hr = PreviewCodec_CreateSample(capacity, &supplied, &buffer);
    }
    if (SUCCEEDED(hr))
    {
        ZeroMemory(&output, sizeof(output));
        output.dwStreamID = 0;
        output.pSample    = supplied;
        status = 0;
        hr = IMFTransform_ProcessOutput(transform, 0, 1, &output, &status);
        if (SUCCEEDED(hr))
        {
            *result = output.pSample;
            if (output.pSample == supplied)
            {
                supplied = NULL;
            }
            if (NULL == output.pSample)
            {
                hr = MF_E_TRANSFORM_NEED_MORE_INPUT;
            }
        }
        else if ((NULL != output.pSample) && (output.pSample != supplied))
        {
            cmfUnknownRelease((IUnknown*)output.pSample);
        }
        if (NULL != output.pEvents)
        {
            cmfUnknownRelease((IUnknown*)output.pEvents);
        }
    }
    if (NULL != supplied)
    {
        cmfUnknownRelease((IUnknown*)supplied);
    }
    if (NULL != buffer)
    {
        cmfUnknownRelease((IUnknown*)buffer);
    }
    return hr;
}

static HRESULT PreviewCodec_ResetDecoderOutput(PreviewCodec* codec)
{
    IMFMediaType*          output;
    MFT_OUTPUT_STREAM_INFO info;
    UINT32                 stride;
    HRESULT                hr;

    output = NULL;
    ZeroMemory(&info, sizeof(info));
    hr = PreviewCodec_MakeType(&MFVideoFormat_NV12, codec->width, codec->height, &output);
    if (SUCCEEDED(hr))
    {
        hr = IMFTransform_SetOutputType(codec->decoder, 0, output, 0);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFTransform_GetOutputStreamInfo(codec->decoder, 0, &info);
    }
    if (SUCCEEDED(hr))
    {
        codec->decodedCapacity = info.cbSize;
        codec->decoderStride = codec->width;
        if (SUCCEEDED(IMFMediaType_GetUINT32(output, &MF_MT_DEFAULT_STRIDE, &stride)))
        {
            codec->decoderStride = stride;
        }
    }
    if (NULL != output)
    {
        cmfUnknownRelease((IUnknown*)output);
    }
    return hr;
}

static HRESULT PreviewCodec_DrainDecoder(
    PreviewPipeline* preview,
    PreviewCodec*    codec)
{
    IMFSample* decoded;
    HRESULT    hr;
    bool_t     continueDraining;

    continueDraining = TRUE;
    hr = S_OK;
    while (continueDraining)
    {
        decoded = NULL;
        hr = PreviewCodec_TakeOutput(codec->decoder, codec->decodedCapacity, &decoded);
        if (MF_E_TRANSFORM_NEED_MORE_INPUT == hr)
        {
            hr = S_OK;
            break;
        }
        if (MF_E_TRANSFORM_STREAM_CHANGE == hr)
        {
            hr = PreviewCodec_ResetDecoderOutput(codec);
            continueDraining = SUCCEEDED(hr);
            continue;
        }
        if (FAILED(hr))
        {
            break;
        }
        if (NULL != decoded)
        {
            hr = PreviewPipeline_Publish(preview, codec, decoded);
            cmfUnknownRelease((IUnknown*)decoded);
        }
        continueDraining = SUCCEEDED(hr);
    }
    return hr;
}

static HRESULT PreviewCodec_SubmitEncoded(
    PreviewPipeline* preview,
    PreviewCodec*    codec,
    IMFSample*      encoded)
{
    IMFSample*      input;
    IMFMediaBuffer* sourceBuffer;
    IMFMediaBuffer* inputBuffer;
    BYTE*           source;
    BYTE*           target;
    DWORD           length;
    DWORD           prefix;
    HRESULT         hr;

    input        = NULL;
    sourceBuffer = NULL;
    inputBuffer  = NULL;
    source       = NULL;
    target       = NULL;
    length       = 0;
    prefix       = codec->decoderStarted ? 0U : codec->headerLength;
    hr = IMFSample_ConvertToContiguousBuffer(encoded, &sourceBuffer);
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaBuffer_Lock(sourceBuffer, &source, NULL, &length);
    }
    if (SUCCEEDED(hr))
    {
        if ((UINT32_MAX - prefix < length) ||
            (PREVIEW_MAX_FRAME_BYTES < (UINT64)prefix + length))
        {
            hr = MF_E_BUFFERTOOSMALL;
        }
    }
    if (SUCCEEDED(hr))
    {
        hr = PreviewCodec_CreateSample(prefix + length, &input, &inputBuffer);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFMediaBuffer_Lock(inputBuffer, &target, NULL, NULL);
    }
    if (SUCCEEDED(hr))
    {
        if (0 < prefix)
        {
            CRT_Memory_Copy(target, codec->header, prefix);
        }
        CRT_Memory_Copy(target + prefix, source, length);
        (void)IMFMediaBuffer_Unlock(inputBuffer);
        hr = IMFMediaBuffer_SetCurrentLength(inputBuffer, prefix + length);
    }
    if (SUCCEEDED(hr))
    {
        hr = IMFTransform_ProcessInput(codec->decoder, 0, input, 0);
    }
    if (SUCCEEDED(hr))
    {
        codec->decoderStarted = TRUE;
        hr = PreviewCodec_DrainDecoder(preview, codec);
    }
    if (NULL != source)
    {
        (void)IMFMediaBuffer_Unlock(sourceBuffer);
    }
    if (NULL != input)
    {
        cmfUnknownRelease((IUnknown*)input);
    }
    if (NULL != inputBuffer)
    {
        cmfUnknownRelease((IUnknown*)inputBuffer);
    }
    if (NULL != sourceBuffer)
    {
        cmfUnknownRelease((IUnknown*)sourceBuffer);
    }
    return hr;
}

static HRESULT PreviewCodec_DrainEncoder(
    PreviewPipeline* preview,
    PreviewCodec*    codec)
{
    IMFSample* encoded;
    HRESULT    hr;
    bool_t     continueDraining;

    continueDraining = TRUE;
    hr = S_OK;
    while (continueDraining)
    {
        encoded = NULL;
        hr = PreviewCodec_TakeOutput(codec->encoder, codec->encodedCapacity, &encoded);
        if (MF_E_TRANSFORM_NEED_MORE_INPUT == hr)
        {
            hr = S_OK;
            break;
        }
        if (FAILED(hr))
        {
            break;
        }
        if (NULL != encoded)
        {
            hr = PreviewCodec_SubmitEncoded(preview, codec, encoded);
            cmfUnknownRelease((IUnknown*)encoded);
        }
        continueDraining = SUCCEEDED(hr);
    }
    return hr;
}

static HRESULT PreviewCodec_Advance(
    PreviewPipeline* preview,
    PreviewCodec*    codec)
{
    IMFSample*  source;
    PROPVARIANT start;
    DWORD       flags;
    LONGLONG    timestamp;
    HRESULT     hr;

    source    = NULL;
    flags     = 0;
    timestamp = 0;
    hr = IMFSourceReader_ReadSample(codec->reader,
                                    (DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                    0,
                                    NULL,
                                    &flags,
                                    &timestamp,
                                    &source);
    if (SUCCEEDED(hr) && (0 != (flags & MF_SOURCE_READERF_ENDOFSTREAM)))
    {
        ZeroMemory(&start, sizeof(start));
        start.vt = VT_I8;
        hr = IMFSourceReader_SetCurrentPosition(codec->reader, &GUID_NULL, &start);
    }
    if (SUCCEEDED(hr) && (NULL != source))
    {
        codec->playbackPosition = timestamp;
        hr = PreviewCodec_EncodeInput(codec, source);
        if (SUCCEEDED(hr))
        {
            hr = PreviewCodec_DrainEncoder(preview, codec);
        }
        ++codec->frameIndex;
    }
    if (NULL != source)
    {
        cmfUnknownRelease((IUnknown*)source);
    }
    return hr;
}

/* The worker owns source, encoder, and decoder lifetimes across request revisions. */
static void PreviewPipeline_Work(void* context) // NOLINT(readability-function-cognitive-complexity)
{
    PreviewPipeline*   preview;
    PreviewCodec       codec;
    GpuTranscodeConfig config;
    WCHAR              path[MAX_PATH];
    WCHAR              previousPath[MAX_PATH];
    unsigned           generation;
    unsigned           observed;
    LONGLONG           position;
    ULONGLONG          nextTick;
    ULONGLONG          now;
    PROPVARIANT        seek;
    HRESULT            hr;
    bool_t             mediaReady;
    bool_t             sourceChanged;
    bool_t             encoderChanged;

    preview = (PreviewPipeline*)context;
    ZeroMemory(&codec, sizeof(codec));
    ZeroMemory(&config, sizeof(config));
    ZeroMemory(previousPath, sizeof(previousPath));
    previousPath[MAX_PATH - 1U] = L'\0';
    observed = 0;
    nextTick = 0;
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        (void)InterlockedExchange(&preview->failed, TRUE);
        return;
    }
    mediaReady = SUCCEEDED(cmfStartup(MF_VERSION, MFSTARTUP_FULL));
    if (!mediaReady)
    {
        (void)InterlockedExchange(&preview->failed, TRUE);
    }
    while (mediaReady && !OS_Thread_IsStopping(&preview->worker))
    {
        AcquireSRWLockShared(&preview->requestLock);
        generation = preview->requestGeneration;
        if (generation != observed)
        {
            CRT_Memory_Copy(path, preview->path, sizeof(path));
            path[MAX_PATH - 1U] = L'\0';
            config = preview->config;
        }
        ReleaseSRWLockShared(&preview->requestLock);
        if (generation != observed)
        {
            observed = generation;
            sourceChanged  = (0 != lstrcmpW(previousPath, path));
            encoderChanged = (codec.config.width != config.width) ||
                             (codec.config.gopLength != config.gopLength);
            if (sourceChanged || encoderChanged || (NULL == codec.reader))
            {
                position = sourceChanged ? 0 : codec.playbackPosition;
                PreviewCodec_Close(&codec);
                CRT_Memory_Copy(previousPath, path, sizeof(path));
                previousPath[MAX_PATH - 1U] = L'\0';
                hr = PreviewCodec_Open(&codec, path, &config);
                if (SUCCEEDED(hr) && (NULL != codec.reader) && (0 < position))
                {
                    ZeroMemory(&seek, sizeof(seek));
                    seek.vt = VT_I8;
                    seek.hVal.QuadPart = position;
                    hr = IMFSourceReader_SetCurrentPosition(codec.reader, &GUID_NULL, &seek);
                }
                if (FAILED(hr))
                {
                    PreviewCodec_Close(&codec);
                    (void)InterlockedExchange(&preview->failed, TRUE);
                }
            }
            else
            {
                codec.config = config;
            }
            nextTick = 0;
        }
        if (NULL == codec.reader)
        {
            Sleep(PREVIEW_IDLE_DELAY_MS);
            continue;
        }
        now = GetTickCount64();
        if (now < nextTick)
        {
            Sleep((DWORD)(nextTick - now));
            continue;
        }
        nextTick = now + PREVIEW_FRAME_DELAY_MS;
        hr = PreviewCodec_Advance(preview, &codec);
        if (FAILED(hr))
        {
            PreviewCodec_Close(&codec);
            (void)InterlockedExchange(&preview->failed, TRUE);
        }
    }
    PreviewCodec_Close(&codec);
    if (mediaReady)
    {
        cmfShutdown();
    }
    CoUninitialize();
}

bool_t PreviewPipeline_Initialize(PreviewPipeline* preview)
{
    bool_t initialized;

    InitializeSRWLock(&preview->requestLock);
    InitializeSRWLock(&preview->frameLock);
    initialized = OS_Thread_Initialize(&preview->worker, PreviewPipeline_Work, preview);
    preview->initialized = initialized;
    return initialized;
}

bool_t PreviewPipeline_Start(
    PreviewPipeline*         preview,
    const WCHAR*             path,
    const GpuTranscodeConfig* config)
{
    size_t index;
    bool_t terminated;
    bool_t ready;
    OS_Thread_State_t state;

    terminated = FALSE;
    for (index = 0; index < MAX_PATH; ++index)
    {
        if (L'\0' == path[index])
        {
            terminated = TRUE;
            break;
        }
    }
    if (!terminated || !preview->initialized)
    {
        return FALSE;
    }
    AcquireSRWLockExclusive(&preview->requestLock);
    CRT_Memory_Copy(preview->path, path, (index + 1U) * sizeof(WCHAR));
    preview->config = *config;
    ++preview->requestGeneration;
    ReleaseSRWLockExclusive(&preview->requestLock);
    (void)InterlockedExchange(&preview->failed, FALSE);
    state = OS_Thread_GetState(&preview->worker);
    ready = (OS_THREAD_RUNNING == state);
    if (OS_THREAD_READY == state)
    {
        ready = OS_Thread_Submit(&preview->worker);
    }
    return ready;
}

void PreviewPipeline_SetConfig(
    PreviewPipeline*         preview,
    const GpuTranscodeConfig* config)
{
    AcquireSRWLockExclusive(&preview->requestLock);
    if (L'\0' != preview->path[0])
    {
        preview->config = *config;
        ++preview->requestGeneration;
    }
    ReleaseSRWLockExclusive(&preview->requestLock);
}

bool_t PreviewPipeline_TryClose(PreviewPipeline* preview)
{
    bool_t closed;

    (void)OS_Thread_Stop(&preview->worker);
    closed = OS_Thread_TryClose(&preview->worker);
    if (closed)
    {
        AcquireSRWLockExclusive(&preview->frameLock);
        if (NULL != preview->pixels)
        {
            (void)VirtualFree(preview->pixels, 0, MEM_RELEASE);
            preview->pixels = NULL;
        }
        ReleaseSRWLockExclusive(&preview->frameLock);
    }
    return closed;
}
