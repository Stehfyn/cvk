/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Windows Media Foundation source-reader and sink-writer audio conversion. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <propvarutil.h>
#include "crt/crt_format.h"
#include "audio_gain.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TRY(call)                                                                                                      \
    for (;;)                                                                                                           \
    {                                                                                                                  \
        step = #call;                                                                                                  \
        hr   = (call);                                                                                                 \
        if (FAILED(hr))                                                                                                \
        {                                                                                                              \
            goto done;                                                                                                 \
        }                                                                                                              \
        break;                                                                                                         \
    }
#define RELEASE(type, value)                                                                                           \
    for (;;)                                                                                                           \
    {                                                                                                                  \
        if (value)                                                                                                     \
        {                                                                                                              \
            type##_Release(value);                                                                                     \
            value = NULL;                                                                                              \
        }                                                                                                              \
        break;                                                                                                         \
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

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Scales signed 16-bit samples in place, caps the requested gain at 400 percent,
 *  then saturates each result to the signed 16-bit range.
 */
void AudioGain_Apply(int16_t* samples, size_t count, unsigned percent)
{
    size_t i;
    if (percent > 400)
        percent = 400;
    for (i = 0; i < count; ++i)
    {
        int value  = (int)samples[i] * (int)percent / 100;
        samples[i] = (int16_t)(value > 32767 ? 32767 : value < -32768 ? -32768 : value);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Opens the first audio stream through Media Foundation and checks its native
 *  subtype. AAC and MP3 bypass conversion; inspection failure returns an error.
 */
int AudioGain_NeedsConversion(const wchar_t* source, char* error, size_t errorCapacity)
{
    IMFSourceReader* reader = NULL;
    IMFMediaType*    type   = NULL;
    HRESULT          com = CoInitializeEx(NULL, COINIT_MULTITHREADED), hr = com;
    const char*      step    = "CoInitializeEx";
    int              started = 0, result = -1;
    if (errorCapacity)
        error[0] = 0;
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
        goto done;
    TRY(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    started = 1;
    TRY(MFCreateSourceReaderFromURL(source, NULL, &reader));
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
        TRY(IMFMediaType_GetGUID(type, &MF_MT_SUBTYPE, &subtype));
        result = !IsEqualGUID(&subtype, &MFAudioFormat_AAC) && !IsEqualGUID(&subtype, &MFAudioFormat_MP3);
    }
done:
    if (result < 0)
        (void)CRT_Format_Write(error, (uint32_t)errorCapacity, "Cannot inspect audio (0x%08lX) at %s.",
                               (unsigned long)hr, step);
    RELEASE(IMFMediaType, type);
    RELEASE(IMFSourceReader, reader);
    if (started)
        MFShutdown();
    if (SUCCEEDED(com))
        CoUninitialize();
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
int AudioGain_Convert(const wchar_t* source,
                      const wchar_t* dest,
                      unsigned       percent,
                      volatile long* cancel,
                      volatile long* progress,
                      char*          error,
                      size_t         errorCapacity)
{
    IMFSourceReader* reader = NULL;
    IMFSinkWriter*   writer = NULL;
    IMFMediaType *   video = NULL, *nativeAudio = NULL, *pcm = NULL, *aac = NULL;
    IMFAttributes*   attributes = NULL;
    IMFSample*       sample     = NULL;
    IMFMediaBuffer*  buffer     = NULL;
    HRESULT          hr, com = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    const char*      step    = "CoInitializeEx";
    int              started = 0, ok = 0;
    DWORD            videoIn = 0, audioIn = 0, videoOut = 0, audioOut = 0;
    UINT32           channels = 0, rate = 0;
    LONGLONG         duration = 0;
    int              videoEnd = 0, audioEnd = 0;
    LONGLONG         nextVideoDecodeTime = 0;
    int              haveVideoDecodeTime = 0;
    if (errorCapacity)
        error[0] = 0;
    if (FAILED(com))
    {
        hr = com;
        goto done;
    }
    TRY(MFStartup(MF_VERSION, MFSTARTUP_FULL));
    started = 1;
    TRY(MFCreateSourceReaderFromURL(source, NULL, &reader));
    TRY(IMFSourceReader_SetStreamSelection(reader, (DWORD)MF_SOURCE_READER_ALL_STREAMS, FALSE));
    /* Find the actual stream indexes; ANY_STREAM returns these rather than the sentinel IDs. */
    for (DWORD i = 0;; ++i)
    {
        IMFMediaType* type = NULL;
        GUID major;
        hr = IMFSourceReader_GetNativeMediaType(reader, i, 0, &type);
        if (hr == MF_E_INVALIDSTREAMNUMBER)
            break;
        if (FAILED(hr))
            goto done;
        hr = IMFMediaType_GetGUID(type, &MF_MT_MAJOR_TYPE, &major);
        if (SUCCEEDED(hr) && IsEqualGUID(&major, &MFMediaType_Video) && !video)
        {
            video   = type;
            videoIn = i;
        }
        else if (SUCCEEDED(hr) && IsEqualGUID(&major, &MFMediaType_Audio) && !nativeAudio)
        {
            nativeAudio = type;
            audioIn     = i;
        }
        else
            IMFMediaType_Release(type);
    }
    if (!video || !nativeAudio)
    {
        hr = MF_E_INVALIDMEDIATYPE;
        goto done;
    }
    TRY(IMFMediaType_GetUINT32(nativeAudio, &MF_MT_AUDIO_NUM_CHANNELS, &channels));
    TRY(IMFMediaType_GetUINT32(nativeAudio, &MF_MT_AUDIO_SAMPLES_PER_SECOND, &rate));
    if ((channels != 1 && channels != 2 && channels != 6) || (rate != 44100 && rate != 48000))
    {
        hr = MF_E_INVALIDMEDIATYPE;
        goto done;
    }
    TRY(IMFSourceReader_SetCurrentMediaType(reader, videoIn, NULL, video));
    TRY(IMFSourceReader_SetStreamSelection(reader, videoIn, TRUE));
    TRY(IMFSourceReader_SetStreamSelection(reader, audioIn, TRUE));
    TRY(MFCreateMediaType(&pcm));
    TRY(IMFMediaType_SetGUID(pcm, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio));
    TRY(IMFMediaType_SetGUID(pcm, &MF_MT_SUBTYPE, &MFAudioFormat_PCM));
    TRY(IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_NUM_CHANNELS, channels));
    TRY(IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate));
    TRY(IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
    TRY(IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_BLOCK_ALIGNMENT, channels * 2));
    TRY(IMFMediaType_SetUINT32(pcm, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, rate * channels * 2));
    TRY(IMFSourceReader_SetCurrentMediaType(reader, audioIn, NULL, pcm));
    RELEASE(IMFMediaType, pcm);
    TRY(IMFSourceReader_GetCurrentMediaType(reader, audioIn, &pcm));
    TRY(MFCreateMediaType(&aac));
    TRY(IMFMediaType_SetGUID(aac, &MF_MT_MAJOR_TYPE, &MFMediaType_Audio));
    TRY(IMFMediaType_SetGUID(aac, &MF_MT_SUBTYPE, &MFAudioFormat_AAC));
    TRY(IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_NUM_CHANNELS, channels));
    TRY(IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_SAMPLES_PER_SECOND, rate));
    TRY(IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_BITS_PER_SAMPLE, 16));
    TRY(IMFMediaType_SetUINT32(aac, &MF_MT_AUDIO_AVG_BYTES_PER_SECOND, channels == 6 ? 72000 : 24000));
    if (channels == 6)
        TRY(IMFMediaType_SetUINT32(aac, &MF_MT_AAC_AUDIO_PROFILE_LEVEL_INDICATION, 0x2a));
    TRY(IMFMediaType_SetUINT32(aac, &MF_MT_AAC_PAYLOAD_TYPE, 0));
    TRY(MFCreateAttributes(&attributes, 2));
    TRY(IMFAttributes_SetUINT32(attributes, &MF_SINK_WRITER_DISABLE_THROTTLING, TRUE));
    TRY(MFCreateSinkWriterFromURL(dest, NULL, attributes, &writer));
    TRY(IMFSinkWriter_AddStream(writer, video, &videoOut));
    TRY(IMFSinkWriter_SetInputMediaType(writer, videoOut, video, NULL));
    TRY(IMFSinkWriter_AddStream(writer, aac, &audioOut));
    TRY(IMFSinkWriter_SetInputMediaType(writer, audioOut, pcm, NULL));
    TRY(IMFSinkWriter_BeginWriting(writer));
    {
        PROPVARIANT value;
        PropVariantInit(&value);
        if (SUCCEEDED(IMFSourceReader_GetPresentationAttribute(reader,
                                                               (DWORD)MF_SOURCE_READER_MEDIASOURCE,
                                                               &MF_PD_DURATION,
                                                               &value)) &&
            value.vt == VT_UI8)
            duration = (LONGLONG)value.uhVal.QuadPart;
        PropVariantClear(&value);
    }
    for (;;)
    {
        DWORD    stream, flags;
        LONGLONG time;
        if (videoEnd && audioEnd)
        {
            break;
        }
        if (cancel && InterlockedCompareExchange(cancel, 0, 0))
        {
            hr = E_ABORT;
            goto done;
        }
        TRY(IMFSourceReader_ReadSample(reader, (DWORD)MF_SOURCE_READER_ANY_STREAM, 0, &stream, &flags, &time, &sample));
        if (flags & MF_SOURCE_READERF_ERROR)
        {
            hr = E_FAIL;
            goto done;
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
        {
            hr = MF_E_INVALIDMEDIATYPE;
            goto done;
        }
        if (sample)
        {
            if (stream == videoIn)
            {
                UINT64   decode         = 0;
                LONGLONG sampleDuration = 0;
                /* MKV exposes presentation timestamps but often omits DTS. The MP4
                ** sink drops B-frames if timestamps go backwards without DTS.
                ** Samples arrive in decode order, so advance DTS by frame duration;
                ** keep presentation times intact for audio/subtitle sync. */
                if (FAILED(IMFSample_GetUINT64(sample, &MFSampleExtension_DecodeTimestamp, &decode)))
                {
                    TRY(IMFSample_GetSampleDuration(sample, &sampleDuration));
                    if (sampleDuration <= 0 || sampleDuration > 100000000)
                    {
                        hr = MF_E_INVALIDMEDIATYPE;
                        goto done;
                    }
                    if (!haveVideoDecodeTime)
                        nextVideoDecodeTime = time;
                    TRY(IMFSample_SetUINT64(sample, &MFSampleExtension_DecodeTimestamp, (UINT64)nextVideoDecodeTime));
                    nextVideoDecodeTime += sampleDuration;
                    haveVideoDecodeTime  = 1;
                }
            }
            if (stream == audioIn)
            {
                BYTE* bytes;
                DWORD length;
                TRY(IMFSample_ConvertToContiguousBuffer(sample, &buffer));
                TRY(IMFMediaBuffer_Lock(buffer, &bytes, NULL, &length));
                AudioGain_Apply((int16_t*)bytes, length / sizeof(int16_t), percent);
                hr = IMFMediaBuffer_Unlock(buffer);
                RELEASE(IMFMediaBuffer, buffer);
                if (FAILED(hr))
                    goto done;
            }
            TRY(IMFSinkWriter_WriteSample(writer, stream == audioIn ? audioOut : videoOut, sample));
            RELEASE(IMFSample, sample);
        }
        else if (flags & MF_SOURCE_READERF_STREAMTICK)
            TRY(IMFSinkWriter_SendStreamTick(writer, stream == audioIn ? audioOut : videoOut, time));
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            if (stream == videoIn)
                videoEnd = 1;
            if (stream == audioIn)
                audioEnd = 1;
            TRY(IMFSourceReader_SetStreamSelection(reader, stream, FALSE));
        }
        if (progress && duration > 0 && time >= 0)
            InterlockedExchange(progress, (LONG)(time >= duration ? 99 : time * 100 / duration));
    }
    TRY(IMFSinkWriter_Finalize(writer));
    if (cancel && InterlockedCompareExchange(cancel, 0, 0))
    {
        hr = E_ABORT;
        goto done;
    }
    if (progress)
        InterlockedExchange(progress, 100);
    ok = 1;
done:
    if (!ok)
    {
        if (hr == E_ABORT)
            (void)CRT_Format_Copy(error, (uint32_t)errorCapacity, "Audio conversion cancelled.");
        else
            (void)CRT_Format_Write(error, (uint32_t)errorCapacity,
                                   "Audio conversion failed (0x%08lX) at %.*s.", (unsigned long)hr, 140, step);
    }
    RELEASE(IMFMediaBuffer, buffer);
    RELEASE(IMFSample, sample);
    RELEASE(IMFSinkWriter, writer);
    RELEASE(IMFSourceReader, reader);
    RELEASE(IMFMediaType, video);
    RELEASE(IMFMediaType, nativeAudio);
    RELEASE(IMFMediaType, pcm);
    RELEASE(IMFMediaType, aac);
    RELEASE(IMFAttributes, attributes);
    if (started)
        MFShutdown();
    if (SUCCEEDED(com))
        CoUninitialize();
    if (!ok)
        DeleteFileW(dest);
    return ok;
}
