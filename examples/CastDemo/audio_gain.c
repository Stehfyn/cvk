/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Windows Media Foundation source-reader and sink-writer audio conversion. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <stdint.h>
#include <stddef.h>
#include <Unknwnbase.h>
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
#include <cmf.h>
#include "crt/crt_format.h"
#include "audio_gain.h"
#include "os/os_compiler.h"
#include "os/os_types.h"

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
#define AUDIO_GAIN_DEFAULT_AVERAGE_BITRATE  (24000U)
#define AUDIO_GAIN_SURROUND_AVERAGE_BITRATE (72000U)
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

static FORCE_INLINE HRESULT FindAudioStreams(IMFSourceReader* const* ppReader, IMFMediaType** ppVideo, IMFMediaType** ppNativeAudio, DWORD* pVideoIn, DWORD* pAudioIn)
{
    DWORD i;

    for (i = 0;; ++i)
    {
        IMFSourceReader* reader = *ppReader;
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
            cmfUnknownReleaseSafe((IUnknown*)type);
        }
    }
}

static FORCE_INLINE HRESULT CreatePcmType(IMFSourceReader* reader, DWORD audioIn, UINT32 channels, UINT32 rate, IMFMediaType** ppPcm)
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
    HRESULT setStatus;
    HRESULT currentStatus;
    IMFMediaType* current;
    IMFMediaType* released = NULL;
    bool_t set;

    *ppPcm = NULL;
    hr = MFCreateMediaType(ppPcm);
    pcm = *ppPcm;
    if (NULL != pcm)
    {
        majorStatus = IMFMediaType_SetGUID(pcm, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        subtypeStatus = IMFMediaType_SetGUID(pcm, &MF_MT_SUBTYPE, &MFAudioFormat_PCM);
        channelsStatus = IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_NUM_CHANNELS, channels);
        rateStatus = IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
        bitsStatus = IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        alignmentStatus = IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2);
        averageStatus = IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 2);
        setStatus     = IMFSourceReader_SetCurrentMediaType(reader, audioIn, NULL, pcm);
        current       = NULL;
        currentStatus = IMFSourceReader_GetCurrentMediaType(reader, audioIn, &current);
        status = majorStatus;
        status = FAILED(status) ? status : subtypeStatus;
        status = FAILED(status) ? status : channelsStatus;
        status = FAILED(status) ? status : rateStatus;
        status = FAILED(status) ? status : bitsStatus;
        status = FAILED(status) ? status : alignmentStatus;
        status = FAILED(status) ? status : averageStatus;
        set = SUCCEEDED(setStatus);
        released = set ? pcm : current;
        *ppPcm = set ? current : pcm;
        status = FAILED(status) ? status : setStatus;
        hr = FAILED(status) ? status : currentStatus;
    }
    cmfUnknownReleaseSafe((IUnknown*)released);
    return hr;
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
    HRESULT profileStatus;
    HRESULT payloadStatus;
    HRESULT status;
    UINT32 averageBitrate;

    *ppAac = NULL;
    hr = MFCreateMediaType(ppAac);
    aac = *ppAac;
    profileStatus = S_OK;
    averageBitrate = (AUDIO_GAIN_SURROUND_CHANNELS == channels)
                         ? AUDIO_GAIN_SURROUND_AVERAGE_BITRATE
                         : AUDIO_GAIN_DEFAULT_AVERAGE_BITRATE;
    if (SUCCEEDED(hr))
    {
        majorStatus = IMFMediaType_SetGUID(aac, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio);
        subtypeStatus = IMFMediaType_SetGUID(aac, &MF_MT_SUBTYPE, &MFAudioFormat_AAC);
        channelsStatus = IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_NUM_CHANNELS, channels);
        rateStatus = IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate);
        bitsStatus = IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
        averageStatus = IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, averageBitrate);
        if (AUDIO_GAIN_SURROUND_CHANNELS == channels)
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
        hr = FAILED(status) ? status : payloadStatus;
    }
    return hr;
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
    HRESULT throttleStatus;
    HRESULT createStatus;
    HRESULT videoAddStatus;
    HRESULT audioAddStatus;
    HRESULT videoTypeStatus;
    HRESULT audioTypeStatus;
    HRESULT beginStatus;
    HRESULT status;
    DWORD videoStream;
    DWORD audioStream;

    *ppAttributes = NULL;
    *ppWriter = NULL;
    *pVideoOut = 0;
    *pAudioOut = 0;
    throttleStatus  = S_OK;
    createStatus    = S_OK;
    videoAddStatus  = S_OK;
    audioAddStatus  = S_OK;
    videoTypeStatus = S_OK;
    audioTypeStatus = S_OK;
    beginStatus     = E_POINTER; /* a missing writer never begins writing */
    hr = MFCreateAttributes(ppAttributes, 2);
    attributes = *ppAttributes;
    if (NULL != attributes)
    {
        throttleStatus = IMFAttributes_SetUINT32(attributes, &MF_SINK_WRITER_DISABLE_THROTTLING, TRUE);
        createStatus   = MFCreateSinkWriterFromURL(dest, NULL, attributes, ppWriter);
    }
    writer = *ppWriter;
    if (NULL != writer)
    {
        videoAddStatus  = IMFSinkWriter_AddStream(writer, video, pVideoOut);
        audioAddStatus  = IMFSinkWriter_AddStream(writer, aac, pAudioOut);
        videoStream     = *pVideoOut;
        audioStream     = *pAudioOut;
        videoTypeStatus = IMFSinkWriter_SetInputMediaType(writer, videoStream, video, NULL);
        audioTypeStatus = IMFSinkWriter_SetInputMediaType(writer, audioStream, pcm, NULL);
        beginStatus     = IMFSinkWriter_BeginWriting(writer);
    }
    status = FAILED(hr) ? hr : throttleStatus;
    status = FAILED(status) ? status : createStatus;
    status = FAILED(status) ? status : videoAddStatus;
    status = FAILED(status) ? status : audioAddStatus;
    status = FAILED(status) ? status : videoTypeStatus;
    status = FAILED(status) ? status : audioTypeStatus;
    status = FAILED(status) ? status : beginStatus;
    return status;
}

static FORCE_INLINE HRESULT EnsureVideoDecodeTime(IMFSample* sample, LONGLONG time, LONGLONG* pNextVideoDecodeTime, int* pHaveVideoDecodeTime)
{
    UINT64 decode = 0;
    LONGLONG sampleDuration = 0;
    HRESULT hr;
    HRESULT decodeStatus;
    bool_t needsTimestamp;
    bool_t hasDuration;
    bool_t validDuration;
    bool_t canSet;
    bool_t invalidDuration;

    decodeStatus = IMFSample_GetUINT64(sample, &MFSampleExtension_DecodeTimestamp, &decode);
    hr = IMFSample_GetSampleDuration(sample, &sampleDuration);
    needsTimestamp  = FAILED(decodeStatus);
    hasDuration     = SUCCEEDED(hr);
    validDuration   = (0 < sampleDuration && AUDIO_GAIN_MAX_SAMPLE_100NS >= sampleDuration);
    canSet          = needsTimestamp && hasDuration && validDuration;
    invalidDuration = needsTimestamp && hasDuration && !validDuration;
    if (canSet)
    {
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
    }
    if (invalidDuration)
    {
        hr = MF_E_INVALIDMEDIATYPE;
    }
    return needsTimestamp ? hr : S_OK;
}

static HRESULT ApplyGainToSample(IMFSample* sample, unsigned percent)
{
    IMFMediaBuffer* buffer;
    BYTE* bytes;
    DWORD length;
    HRESULT hr;

    buffer = NULL;
    hr = IMFSample_ConvertToContiguousBuffer(sample, &buffer);
    if (SUCCEEDED(hr))
    {
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
    }
    cmfUnknownReleaseSafe((IUnknown*)buffer);
    return hr;
}

static FORCE_INLINE HRESULT WriteConversionSample(IMFSinkWriter* writer,
                                     IMFSample* const* ppSample,
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
    HRESULT hr;
    bool_t is_video;
    bool_t is_audio;
    DWORD output_stream;
    IMFSample* sample;

    hr = S_OK;
    is_video = (videoIn == stream);
    is_audio = (audioIn == stream);
    output_stream = is_audio ? audioOut : videoOut;
    sample = *ppSample;
    if (is_video)
    {
        *ppStep = "EnsureVideoDecodeTime";
        hr = EnsureVideoDecodeTime(sample, time, pNextVideoDecodeTime, pHaveVideoDecodeTime);
    }
    else if (is_audio)
    {
        *ppStep = "ApplyGainToSample";
        hr = ApplyGainToSample(sample, percent);
    }
    sample = *ppSample;
    if (SUCCEEDED(hr))
    {
        *ppStep = "IMFSinkWriter_WriteSample";
        hr = IMFSinkWriter_WriteSample(writer, output_stream, sample);
    }
    return hr;
}

static FORCE_INLINE HRESULT MarkConversionStreamEnd(IMFSourceReader* reader, DWORD stream, DWORD videoIn, DWORD audioIn, int* pVideoEnd, int* pAudioEnd)
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

static FORCE_INLINE HRESULT ReadConversionSample(IMFSourceReader* reader, DWORD* pStream, DWORD* pFlags, LONGLONG* pTime, IMFSample** ppSample)
{
    HRESULT hr;
    IMFSample* sample;
    IMFSample* released;

    *ppSample = NULL;
    hr = IMFSourceReader_ReadSample(reader, (DWORD)MF_SOURCE_READER_ANY_STREAM, 0, pStream, pFlags, pTime, ppSample);
    sample = *ppSample;
    if (SUCCEEDED(hr) && (*pFlags & MF_SOURCE_READERF_ERROR))
    {
        hr = E_FAIL;
    }
    if (SUCCEEDED(hr) && (*pFlags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED))
    {
        hr = MF_E_INVALIDMEDIATYPE;
    }
    released = FAILED(hr) ? sample : NULL;
    cmfUnknownReleaseSafe((IUnknown*)released);
    if (FAILED(hr))
    {
        *ppSample = NULL;
    }
    return hr;
}

static FORCE_INLINE int ConversionCancelled(volatile long* cancel)
{
    return cancel && InterlockedCompareExchange(cancel, 0, 0);
}

static FORCE_INLINE HRESULT ProcessConversion(IMFSourceReader* const* ppReader,
                                 IMFSinkWriter* const* ppWriter,
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
        IMFSourceReader* reader;
        IMFSinkWriter* writer;
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
        reader = *ppReader;
        hr = ReadConversionSample(reader, &stream, &flags, &time, &sample);
        if (FAILED(hr))
        {
            break;
        }
        writer = *ppWriter;
        if (NULL != sample)
        {
            hr = WriteConversionSample(writer, &sample, stream, videoIn, audioIn, videoOut, audioOut, time, percent, &nextVideoDecodeTime, &haveVideoDecodeTime, ppStep);
            cmfUnknownReleaseSafe((IUnknown*)sample);
            if (FAILED(hr))
            {
                break;
            }
        }
        else if (flags & MF_SOURCE_READERF_STREAMTICK)
        {
            *ppStep = "IMFSinkWriter_SendStreamTick";
            hr = IMFSinkWriter_SendStreamTick(writer, stream == audioIn ? audioOut : videoOut, time);
            if (FAILED(hr))
            {
                break;
            }
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            *ppStep = "IMFSourceReader_SetStreamSelection";
            reader = *ppReader;
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
    HRESULT deselect_status;
    HRESULT find_status;
    HRESULT channels_status;
    HRESULT rate_status;
    HRESULT video_type_status;
    HRESULT video_selection_status;
    HRESULT audio_selection_status;
    HRESULT pcm_status;
    bool_t has_streams;
    bool_t valid_channels;
    bool_t valid_rate;
    bool_t configure;
    DWORD videoStream;
    DWORD audioStream;
    UINT32 channels;
    UINT32 rate;

    *ppStep = "MFCreateSourceReaderFromURL";
    *ppReader = NULL;
    *ppVideo = NULL;
    *ppNativeAudio = NULL;
    *pVideoIn = 0;
    *pAudioIn = 0;
    *pChannels = 0;
    *pRate = 0;
    deselect_status        = S_OK;
    find_status            = S_OK;
    channels_status        = S_OK;
    rate_status            = S_OK;
    video_type_status      = S_OK;
    video_selection_status = S_OK;
    audio_selection_status = S_OK;
    pcm_status             = S_OK;
    hr = MFCreateSourceReaderFromURL(source, NULL, ppReader);
    reader = *ppReader;
    if (NULL != reader)
    {
        deselect_status = IMFSourceReader_SetStreamSelection(reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE);
        find_status     = FindAudioStreams(ppReader, ppVideo, ppNativeAudio, pVideoIn, pAudioIn);
    }
    video       = *ppVideo;
    nativeAudio = *ppNativeAudio;
    videoStream = *pVideoIn;
    audioStream = *pAudioIn;
    has_streams = (NULL != video) && (NULL != nativeAudio);
    if (has_streams)
    {
        channels_status = IMFMediaType_GetUINT32(nativeAudio, &MF_MT_AUDIO_NUM_CHANNELS, pChannels);
        rate_status     = IMFMediaType_GetUINT32(nativeAudio, &MF_MT_AUDIO_SAMPLES_PER_SECOND, pRate);
    }
    channels       = *pChannels;
    rate           = *pRate;
    valid_channels = (1 == channels || 2 == channels || AUDIO_GAIN_SURROUND_CHANNELS == channels);
    valid_rate     = (AUDIO_GAIN_RATE_44100 == rate || AUDIO_GAIN_RATE_48000 == rate);
    configure      = has_streams && valid_channels && valid_rate;
    if (configure)
    {
        reader                 = *ppReader;
        video_type_status      = IMFSourceReader_SetCurrentMediaType(reader, videoStream, NULL, video);
        video_selection_status = IMFSourceReader_SetStreamSelection(reader, videoStream, TRUE);
        audio_selection_status = IMFSourceReader_SetStreamSelection(reader, audioStream, TRUE);
        pcm_status             = CreatePcmType(reader, audioStream, channels, rate, ppPcm);
    }
    if (FAILED(hr))
    {
        /* *ppStep already names MFCreateSourceReaderFromURL. */
    }
    else if (FAILED(deselect_status))
    {
        *ppStep = "IMFSourceReader_SetStreamSelection";
        hr = deselect_status;
    }
    else if (FAILED(find_status))
    {
        *ppStep = "FindAudioStreams";
        hr = find_status;
    }
    else if (FAILED(channels_status))
    {
        *ppStep = "IMFMediaType_GetUINT32(channels)";
        hr = channels_status;
    }
    else if (FAILED(rate_status))
    {
        *ppStep = "IMFMediaType_GetUINT32(rate)";
        hr = rate_status;
    }
    else if (!configure)
    {
        *ppStep = "FindAudioStreams";
        hr = MF_E_INVALIDMEDIATYPE;
    }
    else if (FAILED(video_type_status))
    {
        *ppStep = "IMFSourceReader_SetCurrentMediaType(video)";
        hr = video_type_status;
    }
    else if (FAILED(video_selection_status))
    {
        *ppStep = "IMFSourceReader_SetStreamSelection(video)";
        hr = video_selection_status;
    }
    else if (FAILED(audio_selection_status))
    {
        *ppStep = "IMFSourceReader_SetStreamSelection(audio)";
        hr = audio_selection_status;
    }
    else
    {
        *ppStep = "CreatePcmType";
        hr = pcm_status;
    }
    return hr;
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

static FORCE_INLINE void ReleaseAudioTypes(IMFMediaType** ppVideo, IMFMediaType** ppNativeAudio, IMFMediaType** ppPcm, IMFMediaType** ppAac)
{
    cmfUnknownReleaseSafe((IUnknown*)*ppVideo);
    cmfUnknownReleaseSafe((IUnknown*)*ppNativeAudio);
    cmfUnknownReleaseSafe((IUnknown*)*ppPcm);
    cmfUnknownReleaseSafe((IUnknown*)*ppAac);
    *ppVideo       = NULL;
    *ppNativeAudio = NULL;
    *ppPcm         = NULL;
    *ppAac         = NULL;
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
    cmfUnknownReleaseSafe((IUnknown*)type);
    cmfUnknownReleaseSafe((IUnknown*)reader);
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
int AudioGain_Convert(const TCHAR* source, const TCHAR* dest, unsigned percent, volatile long* cancel, volatile long* progress, char* error, size_t errorCapacity)
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
    TRY(PrepareAudioInput(source, &reader, &video, &nativeAudio, &pcm, &videoIn, &audioIn, &channels, &rate, &step))
    TRY(CreateAacType(channels, rate, &aac))
    TRY(CreateConversionWriter(dest, video, pcm, aac, &attributes, &writer, &videoOut, &audioOut))
    duration = ReadPresentationDuration(reader);
    TRY(ProcessConversion(&reader, &writer, videoIn, audioIn, videoOut, audioOut, percent, cancel, progress, duration, &step))
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
    cmfUnknownReleaseSafe((IUnknown*)writer);
    cmfUnknownReleaseSafe((IUnknown*)reader);
    ReleaseAudioTypes(&video, &nativeAudio, &pcm, &aac);
    cmfUnknownReleaseSafe((IUnknown*)attributes);
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
