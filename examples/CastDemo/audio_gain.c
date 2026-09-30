/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Windows Media Foundation source-reader and sink-writer audio conversion. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <stdint.h>
#include <stddef.h>
#include <mfobjects.h>
#include <combaseapi.h>
#include <objbase.h>
#include <minwindef.h>
#include <basetsd.h>
#include <PropIdl.h>
#include <fileapi.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <Mferror.h>
#include <winerror.h>
#include <winnt.h>
#include <wtypes.h>
#include "crt/crt_format.h"
#include "audio_gain.h"
#include "os/os_compiler.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define AUDIO_GAIN_MAX_PERCENT          (400U)
#define AUDIO_GAIN_PERCENT_BASE         (100U)
#define AUDIO_GAIN_SURROUND_CHANNELS    (6U)
#define AUDIO_GAIN_RATE_44100           (44100U)
#define AUDIO_GAIN_RATE_48000           (48000U)
#define AUDIO_GAIN_MAX_SAMPLE_100NS     (100000000)
#define AUDIO_GAIN_PROGRESS_INCOMPLETE  (99)
#define AUDIO_GAIN_ERROR_STEP_LENGTH    (140)

#define TRY(call)                                                                                                      \
    {                                                                                                                  \
        step = #call;                                                                                                  \
        hr = (call);                                                                                                   \
        if (FAILED(hr))                                                                                                \
        {                                                                                                              \
            goto done;                                                                                                 \
        }                                                                                                              \
    }
#define RELEASE(type, value)                                                                                           \
    if (value)                                                                                                         \
    {                                                                                                                  \
        type##_Release(value);                                                                                         \
        (value) = NULL;                                                                                                \
    }

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

static FORCE_INLINE HRESULT FindAudioStreams(IMFSourceReader* reader,
                                IMFMediaType** ppVideo,
                                IMFMediaType** ppNativeAudio,
                                DWORD* pVideoIn,
                                DWORD* pAudioIn)
{
    DWORD i;

    for (i = 0;; ++i)
    {
        IMFMediaType* type = NULL;
        GUID major;
        HRESULT hr = IMFSourceReader_GetNativeMediaType(reader, i, 0, &type);

        if (MF_E_INVALIDSTREAMNUMBER == hr)
        {
            return S_OK;
        }
        if (FAILED(hr))
        {
            return hr;
        }
        hr = IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major);
        if (SUCCEEDED(hr) && IsEqualGUID(&major, &MFMediaType_Video) && NULL == *ppVideo)
        {
            *ppVideo = type;
            *pVideoIn = i;
        }
        else if (SUCCEEDED(hr) && IsEqualGUID(&major, &MFMediaType_Audio) && NULL == *ppNativeAudio)
        {
            *ppNativeAudio = type;
            *pAudioIn = i;
        }
        else
        {
            IMFMediaType_Release(type);
        }
    }
}

static FORCE_INLINE HRESULT CreatePcmType(IMFSourceReader* reader, DWORD audioIn,
                              UINT32 channels, UINT32 rate, IMFMediaType** ppPcm)
{
    IMFMediaType* pcm;
    HRESULT hr;
    HRESULT majorStatus;
    HRESULT subtypeStatus;
    HRESULT channelsStatus;
    HRESULT rateStatus;
    HRESULT bitsStatus;
    HRESULT alignmentStatus;
    HRESULT averageStatus;
    HRESULT status;

    *ppPcm = NULL;
    hr = MFCreateMediaType(ppPcm);
    pcm = *ppPcm;
    if (FAILED(hr))
    {
        return hr;
    }
    majorStatus = IMFMediaType_SetGUID(pcm, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    subtypeStatus = IMFMediaType_SetGUID(pcm, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
    channelsStatus = IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    rateStatus = IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    bitsStatus = IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    alignmentStatus = IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
    averageStatus = IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 2);
    status = majorStatus;
    status = FAILED(status) ? status : subtypeStatus;
    status = FAILED(status) ? status : channelsStatus;
    status = FAILED(status) ? status : rateStatus;
    status = FAILED(status) ? status : bitsStatus;
    status = FAILED(status) ? status : alignmentStatus;
    status = FAILED(status) ? status : averageStatus;
    if (FAILED(status))
    {
        return status;
    }
    hr = IMFSourceReader_SetCurrentMediaType(reader, audioIn, NULL, pcm);
    if (FAILED(hr))
    {
        return hr;
    }
    IMFMediaType_Release(pcm);
    *ppPcm = NULL;
    return IMFSourceReader_GetCurrentMediaType(reader, audioIn, ppPcm);
}

static FORCE_INLINE HRESULT CreateAacType(UINT32 channels, UINT32 rate, IMFMediaType** ppAac)
{
    IMFMediaType* aac;
    HRESULT hr;
    HRESULT majorStatus;
    HRESULT subtypeStatus;
    HRESULT channelsStatus;
    HRESULT rateStatus;
    HRESULT bitsStatus;
    HRESULT averageStatus;
    HRESULT profileStatus = S_OK;
    HRESULT payloadStatus;
    HRESULT status;

    *ppAac = NULL;
    hr = MFCreateMediaType(ppAac);
    aac = *ppAac;
    if (FAILED(hr))
    {
        return hr;
    }
    majorStatus = IMFMediaType_SetGUID(aac, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
    subtypeStatus = IMFMediaType_SetGUID(aac, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
    channelsStatus = IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_NUM_CHANNELS, channels);
    rateStatus = IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
    bitsStatus = IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    averageStatus = IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                             channels == AUDIO_GAIN_SURROUND_CHANNELS ? 72000 : 24000);
    if (channels == AUDIO_GAIN_SURROUND_CHANNELS)
    {
        profileStatus = IMFMediaType_SetUINT32(aac, &MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x2a);
    }
    payloadStatus = IMFMediaType_SetUINT32(aac, &MF_MT_AAC_PAYLOAD_TYPE, 0);
    status = majorStatus;
    status = FAILED(status) ? status : subtypeStatus;
    status = FAILED(status) ? status : channelsStatus;
    status = FAILED(status) ? status : rateStatus;
    status = FAILED(status) ? status : bitsStatus;
    status = FAILED(status) ? status : averageStatus;
    status = FAILED(status) ? status : profileStatus;
    return FAILED(status) ? status : payloadStatus;
}

static FORCE_INLINE HRESULT CreateConversionWriter(const TCHAR* dest,
                                      IMFMediaType* video,
                                      IMFMediaType* pcm,
                                      IMFMediaType* aac,
                                      IMFAttributes** ppAttributes,
                                      IMFSinkWriter** ppWriter,
                                      DWORD* pVideoOut,
                                      DWORD* pAudioOut)
{
    IMFAttributes* attributes;
    IMFSinkWriter* writer;
    HRESULT hr;
    HRESULT videoAddStatus;
    HRESULT audioAddStatus;
    HRESULT videoTypeStatus;
    HRESULT audioTypeStatus;
    HRESULT status;

    *ppAttributes = NULL;
    hr = MFCreateAttributes(ppAttributes, 2);
    attributes = *ppAttributes;
    if (FAILED(hr))
    {
        return hr;
    }
    hr = IMFAttributes_SetUINT32(attributes, &MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
    if (FAILED(hr))
    {
        return hr;
    }
    *ppWriter = NULL;
    hr = MFCreateSinkWriterFromURL(dest, NULL, attributes, ppWriter);
    writer = *ppWriter;
    if (FAILED(hr))
    {
        return hr;
    }
    videoAddStatus = IMFSinkWriter_AddStream(writer, video, pVideoOut);
    audioAddStatus = IMFSinkWriter_AddStream(writer, aac, pAudioOut);
    status = FAILED(videoAddStatus) ? videoAddStatus : audioAddStatus;
    if (FAILED(status))
    {
        return status;
    }
    videoTypeStatus = IMFSinkWriter_SetInputMediaType(writer, *pVideoOut, video, NULL);
    audioTypeStatus = IMFSinkWriter_SetInputMediaType(writer, *pAudioOut, pcm, NULL);
    status = FAILED(videoTypeStatus) ? videoTypeStatus : audioTypeStatus;
    if (FAILED(status))
    {
        return status;
    }
    return IMFSinkWriter_BeginWriting(writer);
}

static FORCE_INLINE HRESULT EnsureVideoDecodeTime(IMFSample* sample, LONGLONG time,
                                      LONGLONG* pNextVideoDecodeTime,
                                      int* pHaveVideoDecodeTime)
{
    UINT64 decode = 0;
    LONGLONG sampleDuration = 0;
    HRESULT hr;
    HRESULT decodeStatus;

    decodeStatus = IMFSample_GetUINT64(sample, &MFSampleExtension_DecodeTimestamp, &decode);
    hr = IMFSample_GetSampleDuration(sample, &sampleDuration);
    if (SUCCEEDED(decodeStatus))
    {
        return S_OK;
    }
    if (FAILED(hr))
    {
        return hr;
    }
    if (sampleDuration <= 0 || sampleDuration > AUDIO_GAIN_MAX_SAMPLE_100NS)
    {
        return MF_E_INVALIDMEDIATYPE;
    }
    if (!*pHaveVideoDecodeTime)
    {
        *pNextVideoDecodeTime = time;
    }
    hr = IMFSample_SetUINT64(sample, &MFSampleExtension_DecodeTimestamp, (UINT64)*pNextVideoDecodeTime);
    if (SUCCEEDED(hr))
    {
        *pNextVideoDecodeTime += sampleDuration;
        *pHaveVideoDecodeTime = 1;
    }
    return hr;
}

static HRESULT ApplyGainToSample(IMFSample* sample, unsigned percent)
{
    IMFMediaBuffer* buffer = NULL;
    BYTE* bytes;
    DWORD length;
    HRESULT hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer);

    if (FAILED(hr))
    {
        return hr;
    }
    hr = IMFMediaBuffer_Lock(buffer, &bytes, NULL, &length);
    if (SUCCEEDED(hr))
    {
        if (((uintptr_t)bytes % sizeof(int16_t)) != 0)
        {
            (void)IMFMediaBuffer_Unlock(buffer);
            hr = MF_E_INVALIDMEDIATYPE;
        }
        else
        {
#ifdef __clang__
            AudioGain_Apply((int16_t*)__builtin_assume_aligned(bytes, sizeof(int16_t)), length / sizeof(int16_t), percent);
#else
            AudioGain_Apply((int16_t*)bytes, length / sizeof(int16_t), percent);
#endif
            hr = IMFMediaBuffer_Unlock(buffer);
        }
    }
    IMFMediaBuffer_Release(buffer);
    return hr;
}

static FORCE_INLINE HRESULT WriteConversionSample(IMFSinkWriter* writer,
                                     IMFSample* sample,
                                     DWORD stream,
                                     DWORD videoIn,
                                     DWORD audioIn,
                                     DWORD videoOut,
                                     DWORD audioOut,
                                     LONGLONG time,
                                     unsigned percent,
                                     LONGLONG* pNextVideoDecodeTime,
                                     int* pHaveVideoDecodeTime,
                                     const char** ppStep)
{
    HRESULT hr = S_OK;

    if (stream == videoIn)
    {
        *ppStep = "EnsureVideoDecodeTime";
        hr = EnsureVideoDecodeTime(sample, time, pNextVideoDecodeTime, pHaveVideoDecodeTime);
    }
    if (SUCCEEDED(hr) && stream == audioIn)
    {
        *ppStep = "ApplyGainToSample";
        hr = ApplyGainToSample(sample, percent);
    }
    if (SUCCEEDED(hr))
    {
        *ppStep = "IMFSinkWriter_WriteSample";
        hr = IMFSinkWriter_WriteSample(writer, stream == audioIn ? audioOut : videoOut, sample);
    }
    return hr;
}

static FORCE_INLINE HRESULT MarkConversionStreamEnd(IMFSourceReader* reader, DWORD stream,
                                       DWORD videoIn, DWORD audioIn,
                                       int* pVideoEnd, int* pAudioEnd)
{
    if (stream == videoIn)
    {
        *pVideoEnd = 1;
    }
    if (stream == audioIn)
    {
        *pAudioEnd = 1;
    }
    return IMFSourceReader_SetStreamSelection(reader, stream, FALSE);
}

static FORCE_INLINE void UpdateConversionProgress(volatile long* progress, LONGLONG duration, LONGLONG time)
{
    if (progress && duration > 0 && time >= 0)
    {
        InterlockedExchange(progress,
                            (LONG)(time >= duration ? AUDIO_GAIN_PROGRESS_INCOMPLETE :
                                   time * AUDIO_GAIN_PERCENT_BASE / duration));
    }
}

static FORCE_INLINE HRESULT ReadConversionSample(IMFSourceReader* reader,
                                    DWORD* pStream,
                                    DWORD* pFlags,
                                    LONGLONG* pTime,
                                    IMFSample** ppSample)
{
    HRESULT hr;
    IMFSample* sample;

    *ppSample = NULL;
    hr = IMFSourceReader_ReadSample(reader, (DWORD)MF_SOURCE_READER_ANY_STREAM,
                                    0, pStream, pFlags, pTime, ppSample);
    sample = *ppSample;
    if (SUCCEEDED(hr) && (*pFlags & MF_SOURCE_READERF_ERROR))
    {
        hr = E_FAIL;
    }
    if (SUCCEEDED(hr) && (*pFlags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))
    {
        hr = MF_E_INVALIDMEDIATYPE;
    }
    if (FAILED(hr) && NULL != sample)
    {
        IMFSample_Release(sample);
        *ppSample = NULL;
    }
    return hr;
}

static FORCE_INLINE int ConversionCancelled(volatile long* cancel)
{
    return cancel && InterlockedCompareExchange(cancel, 0, 0);
}

static HRESULT ProcessConversion(IMFSourceReader* reader,
                                 IMFSinkWriter* writer,
                                 DWORD videoIn,
                                 DWORD audioIn,
                                 DWORD videoOut,
                                 DWORD audioOut,
                                 unsigned percent,
                                 volatile long* cancel,
                                 volatile long* progress,
                                 LONGLONG duration,
                                 const char** ppStep)
{
    int videoEnd = 0;
    int audioEnd = 0;
    LONGLONG nextVideoDecodeTime = 0;
    int haveVideoDecodeTime = 0;
    HRESULT hr = S_OK;

    for (;;)
    {
        DWORD stream;
        DWORD flags;
        LONGLONG time;
        IMFSample* sample = NULL;

        if (videoEnd && audioEnd)
        {
            break;
        }
        if (ConversionCancelled(cancel))
        {
            hr = E_ABORT;
            break;
        }
        *ppStep = "IMFSourceReader_ReadSample";
        hr = ReadConversionSample(reader, &stream, &flags, &time, &sample);
        if (FAILED(hr))
        {
            break;
        }
        if (NULL != sample)
        {
            hr = WriteConversionSample(writer, sample, stream, videoIn, audioIn,
                                       videoOut, audioOut, time, percent,
                                       &nextVideoDecodeTime, &haveVideoDecodeTime, ppStep);
            IMFSample_Release(sample);
            if (FAILED(hr))
            {
                break;
            }
        }
        else if (flags & MF_SOURCE_READERF_STREAMTICK)
        {
            *ppStep = "IMFSinkWriter_SendStreamTick";
            hr = IMFSinkWriter_SendStreamTick(writer,
                                              stream == audioIn ? audioOut : videoOut, time);
            if (FAILED(hr))
            {
                break;
            }
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            *ppStep = "IMFSourceReader_SetStreamSelection";
            hr = MarkConversionStreamEnd(reader, stream, videoIn, audioIn, &videoEnd, &audioEnd);
            if (FAILED(hr))
            {
                break;
            }
        }
        UpdateConversionProgress(progress, duration, time);
    }
    return hr;
}

static HRESULT PrepareAudioInput(const TCHAR* source,
                                 IMFSourceReader** ppReader,
                                 IMFMediaType** ppVideo,
                                 IMFMediaType** ppNativeAudio,
                                 IMFMediaType** ppPcm,
                                 DWORD* pVideoIn,
                                 DWORD* pAudioIn,
                                 UINT32* pChannels,
                                 UINT32* pRate,
                                 const char** ppStep)
{
    IMFSourceReader* reader;
    IMFMediaType* video;
    IMFMediaType* nativeAudio;
    HRESULT hr;
    HRESULT channels_status;
    HRESULT rate_status;
    HRESULT video_selection_status;
    HRESULT audio_selection_status;

    *ppStep = "MFCreateSourceReaderFromURL";
    *ppReader = NULL;
    *ppVideo = NULL;
    *ppNativeAudio = NULL;
    hr = MFCreateSourceReaderFromURL(source, NULL, ppReader);
    reader = *ppReader;
    if (FAILED(hr))
    {
        return hr;
    }
    *ppStep = "IMFSourceReader_SetStreamSelection";
    hr = IMFSourceReader_SetStreamSelection(reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
    if (FAILED(hr))
    {
        return hr;
    }
    *ppStep = "FindAudioStreams";
    hr = FindAudioStreams(reader, ppVideo, ppNativeAudio, pVideoIn, pAudioIn);
    video = *ppVideo;
    nativeAudio = *ppNativeAudio;
    if (FAILED(hr))
    {
        return hr;
    }
    if (NULL == video || NULL == nativeAudio)
    {
        return MF_E_INVALIDMEDIATYPE;
    }
    channels_status = IMFMediaType_GetUINT32(nativeAudio, &MF_MT_AUDIO_NUM_CHANNELS, pChannels);
    rate_status = IMFMediaType_GetUINT32(nativeAudio, &MF_MT_AUDIO_SAMPLES_PER_SECOND, pRate);
    if (FAILED(channels_status))
    {
        *ppStep = "IMFMediaType_GetUINT32(channels)";
        return channels_status;
    }
    if (FAILED(rate_status))
    {
        *ppStep = "IMFMediaType_GetUINT32(rate)";
        return rate_status;
    }
    if ((*pChannels != 1 && *pChannels != 2 && *pChannels != AUDIO_GAIN_SURROUND_CHANNELS) ||
        (*pRate != AUDIO_GAIN_RATE_44100 && *pRate != AUDIO_GAIN_RATE_48000))
    {
        return MF_E_INVALIDMEDIATYPE;
    }
    *ppStep = "IMFSourceReader_SetCurrentMediaType(video)";
    hr = IMFSourceReader_SetCurrentMediaType(reader, *pVideoIn, NULL, video);
    if (FAILED(hr))
    {
        return hr;
    }
    video_selection_status = IMFSourceReader_SetStreamSelection(reader, *pVideoIn, TRUE);
    audio_selection_status = IMFSourceReader_SetStreamSelection(reader, *pAudioIn, TRUE);
    if (FAILED(video_selection_status))
    {
        *ppStep = "IMFSourceReader_SetStreamSelection(video)";
        return video_selection_status;
    }
    if (FAILED(audio_selection_status))
    {
        *ppStep = "IMFSourceReader_SetStreamSelection(audio)";
        return audio_selection_status;
    }
    *ppStep = "CreatePcmType";
    return CreatePcmType(reader, *pAudioIn, *pChannels, *pRate, ppPcm);
}

static FORCE_INLINE LONGLONG ReadPresentationDuration(IMFSourceReader* reader)
{
    PROPVARIANT value;
    LONGLONG duration = 0;

    PropVariantInit(&value);
    if (SUCCEEDED(IMFSourceReader_GetPresentationAttribute(reader,
                                                            (DWORD)MF_SOURCE_READER_MEDIASOURCE,
                                                            &MF_PD_DURATION, &value)) &&
        value.vt == VT_UI8)
    {
        duration = (LONGLONG)value.uhVal.QuadPart;
    }
    PropVariantClear(&value);
    return duration;
}

static FORCE_INLINE void ReportConversionFailure(char* error, size_t errorCapacity, HRESULT hr, const char* step)
{
    if (hr == E_ABORT)
    {
        (void)CRT_Format_Copy(error, (uint32_t)errorCapacity, "Audio conversion cancelled.");
    }
    else
    {
        (void)CRT_Format_Write(error, (uint32_t)errorCapacity,
                               "Audio conversion failed (0x%08lX) at %.*s.",
                               (unsigned long)hr, AUDIO_GAIN_ERROR_STEP_LENGTH, step);
    }
}

static FORCE_INLINE void ReleaseAudioTypes(IMFMediaType** ppVideo,
                              IMFMediaType** ppNativeAudio,
                              IMFMediaType** ppPcm,
                              IMFMediaType** ppAac)
{
    if (NULL != *ppVideo)
    {
        IMFMediaType_Release(*ppVideo);
        *ppVideo = NULL;
    }
    if (NULL != *ppNativeAudio)
    {
        IMFMediaType_Release(*ppNativeAudio);
        *ppNativeAudio = NULL;
    }
    if (NULL != *ppPcm)
    {
        IMFMediaType_Release(*ppPcm);
        *ppPcm = NULL;
    }
    if (NULL != *ppAac)
    {
        IMFMediaType_Release(*ppAac);
        *ppAac = NULL;
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Scales signed 16-bit samples in place, caps the requested gain at 400 percent,
 *  then saturates each result to the signed 16-bit range.
 */
extern FORCE_INLINE void AudioGain_Apply(int16_t* samples, size_t count, unsigned percent)
{
    size_t i;
    if (percent > AUDIO_GAIN_MAX_PERCENT)
    {
        percent = AUDIO_GAIN_MAX_PERCENT;
    }
    for (i = 0; i < count; ++i)
    {
        int value = (int)samples[i] * (int)percent / (int)AUDIO_GAIN_PERCENT_BASE;
        if (value > INT16_MAX)
        {
            value = INT16_MAX;
        }
        else if (value < INT16_MIN)
        {
            value = INT16_MIN;
        }
        samples[i] = (int16_t)value;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Opens the first audio stream through Media Foundation and checks its native
 *  subtype. AAC and MP3 bypass conversion; inspection failure returns an error.
 */
int AudioGain_NeedsConversion(const TCHAR* source, char* error, size_t errorCapacity)
{
    IMFSourceReader* reader = NULL;
    IMFMediaType*    type   = NULL;
    HRESULT          com     = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    HRESULT          hr      = com;
    const char*      step    = "CoInitializeEx";
    int              started = 0;
    int              result  = -1;
    if (errorCapacity)
    {
        error[0] = 0;
    }
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
    {
        goto done;
    }
    TRY(MFStartup(MF_VERSION, MFSTARTUP_FULL))
    started = 1;
    TRY(MFCreateSourceReaderFromURL(source, NULL, &reader))
    hr = IMFSourceReader_GetNativeMediaType(reader, (DWORD)MF_SOURCE_READER_FIRST_AUDIO_STREAM, 0, &type);
    if (hr == MF_E_INVALIDSTREAMNUMBER)
    {
        result = 0;
        goto done;
    }
    if (FAILED(hr))
    {
        step = "GetNativeMediaType(audio)";
        goto done;
    }
    {
        GUID subtype;
        TRY(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype))
        result = !IsEqualGUID(&subtype, &MFAudioFormat_AAC) && !IsEqualGUID(&subtype, &MFAudioFormat_MP3);
    }
done:
    if (result < 0)
    {
        (void)CRT_Format_Write(error,
                               (uint32_t)errorCapacity,
                               "Cannot inspect audio (0x%08lX) at %s.",
                               (unsigned long)hr,
                               step);
    }
    RELEASE(IMFMediaType, type)
    RELEASE(IMFSourceReader, reader)
    if (started)
    {
        MFShutdown();
    }
    if (SUCCEEDED(com))
    {
        CoUninitialize();
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Copies compressed video and converts decoded PCM to AAC through Media Foundation.
 *  For video samples lacking DTS, advances decode time by sample duration while
 *  preserving presentation timestamps. Cancellation and errors release the COM
 *  objects and delete the incomplete output file.
 */
int AudioGain_Convert(const TCHAR* source,
                      const TCHAR* dest,
                      unsigned       percent,
                      volatile long* cancel,
                      volatile long* progress,
                      char*          error,
                      size_t         errorCapacity)
{
    IMFSourceReader* reader = NULL;
    IMFSinkWriter*   writer = NULL;
    IMFMediaType*    video       = NULL;
    IMFMediaType*    nativeAudio = NULL;
    IMFMediaType*    pcm         = NULL;
    IMFMediaType*    aac         = NULL;
    IMFAttributes*   attributes = NULL;
    HRESULT          hr;
    HRESULT          com                 = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const char*      step    = "CoInitializeEx";
    int              started             = 0;
    int              ok                  = 0;
    DWORD            videoIn             = 0;
    DWORD            audioIn             = 0;
    DWORD            videoOut            = 0;
    DWORD            audioOut            = 0;
    UINT32           channels            = 0;
    UINT32           rate                = 0;
    LONGLONG         duration = 0;
    if (errorCapacity)
    {
        error[0] = 0;
    }
    if (FAILED(com))
    {
        hr = com;
        goto done;
    }
    TRY(MFStartup(MF_VERSION, MFSTARTUP_FULL))
    started = 1;
    TRY(PrepareAudioInput(source, &reader, &video, &nativeAudio, &pcm,
                          &videoIn, &audioIn, &channels, &rate, &step))
    TRY(CreateAacType(channels, rate, &aac))
    TRY(CreateConversionWriter(dest, video, pcm, aac, &attributes, &writer, &videoOut, &audioOut))
    duration = ReadPresentationDuration(reader);
    TRY(ProcessConversion(reader, writer, videoIn, audioIn, videoOut, audioOut,
                          percent, cancel, progress, duration, &step))
    TRY(IMFSinkWriter_Finalize(writer))
    if (ConversionCancelled(cancel))
    {
        hr = E_ABORT;
        goto done;
    }
    if (progress)
    {
        InterlockedExchange(progress, AUDIO_GAIN_PERCENT_BASE);
    }
    ok = 1;
done:
    if (!ok)
    {
        ReportConversionFailure(error, errorCapacity, hr, step);
    }
    RELEASE(IMFSinkWriter, writer)
    RELEASE(IMFSourceReader, reader)
    ReleaseAudioTypes(&video, &nativeAudio, &pcm, &aac);
    RELEASE(IMFAttributes, attributes)
    if (started)
    {
        MFShutdown();
    }
    if (SUCCEEDED(com))
    {
        CoUninitialize();
    }
    if (!ok)
    {
        DeleteFileW(dest);
    }
    return ok;
}
