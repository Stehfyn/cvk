/* mp4_demux.h - reads the sample tables of an MP4 file so the GPU transcoder can feed frames to
** the Vulkan H.264 decoder. Extracts the video track's SPS/PPS (from avcC), the NAL length prefix
** size, and every sample's file offset, size, decode and composition time, and keyframe flag. The
** audio track is located too (codec and sample table) for the separate audio path.
**
** Only the boxes the transcoder needs are parsed; unknown boxes are skipped. AVC (H.264) only. */
#ifndef CAST_MP4_DEMUX_H
#define CAST_MP4_DEMUX_H

#include "os/os_types.h"
#include <stdint.h>
#include <wchar.h>

typedef struct Mp4Sample
{
    uint64_t offset;          /* byte offset of the sample in the file */
    uint32_t size;            /* sample size in bytes */
    uint32_t duration;        /* in track timescale units, including the final sample */
    int64_t  decodeTime;      /* in track timescale units */
    int64_t  compositionTime; /* decodeTime + ctts offset */
    int      keyframe;        /* a sync sample (IDR) */
} Mp4Sample;

typedef enum Mp4AudioCodec
{
    MP4_AUDIO_NONE,
    MP4_AUDIO_AAC,
    MP4_AUDIO_OTHER
} Mp4AudioCodec;

typedef struct Mp4Demux Mp4Demux;

/* Reads a local MP4/MOV tx3g (mov_text) track independently of the video codec.
** index is zero-based among supported text tracks. A NULL callback only probes metadata.
** Sample bytes begin with a big-endian text length; modifier boxes follow the text. */
typedef int             (*Mp4TextProc)(void* pUser, double start, double end, const byte_t* pData, uint32_t size);

/* Opens the file and reads the sample tables. Returns NULL if it is not an AVC MP4. */
Mp4Demux* Mp4Demux_Open(const wchar_t* pFile);

/* Video track parameters. */
unsigned Mp4Demux_GetWidth(const Mp4Demux* pDemux);

unsigned Mp4Demux_GetHeight(const Mp4Demux* pDemux);

unsigned Mp4Demux_GetTimescale(const Mp4Demux* pDemux);

/* Returns the NAL length-prefix width: 1, 2 or 4 bytes. */
int Mp4Demux_GetNalLengthSize(const Mp4Demux* pDemux);

const byte_t* Mp4Demux_GetSps(const Mp4Demux* pDemux, uint32_t* pSize);

const byte_t* Mp4Demux_GetPps(const Mp4Demux* pDemux, uint32_t* pSize);

unsigned Mp4Demux_GetSampleCount(const Mp4Demux* pDemux);

const Mp4Sample* Mp4Demux_GetSample(const Mp4Demux* pDemux, unsigned index);

/* Audio track summary for the separate audio path. */
Mp4AudioCodec Mp4Demux_GetAudioCodec(const Mp4Demux* pDemux);

unsigned Mp4Demux_GetAudioSampleRate(const Mp4Demux* pDemux);

unsigned Mp4Demux_GetAudioChannels(const Mp4Demux* pDemux);

unsigned Mp4Demux_GetAudioTimescale(const Mp4Demux* pDemux);

/* The declared AAC AudioSpecificConfig from esds. Bytes live until Close; missing data is not guessed. */
const byte_t* Mp4Demux_GetAudioConfig(const Mp4Demux* pDemux, uint32_t* pSize);

/* Audio (AAC) sample table for passthrough muxing. */
unsigned Mp4Demux_GetAudioSampleCount(const Mp4Demux* pDemux);

const Mp4Sample* Mp4Demux_GetAudioSample(const Mp4Demux* pDemux, unsigned index);

/* Reads one sample's bytes into pBuffer (AVCC length-prefixed NAL units). Returns bytes read. */
uint32_t Mp4Demux_ReadSample(Mp4Demux* pDemux, unsigned index, byte_t* pBuffer, uint32_t capacity);

uint32_t Mp4Demux_ReadAudioSample(Mp4Demux* pDemux, unsigned index, byte_t* pBuffer, uint32_t capacity);

int Mp4Demux_ReadTextTrack(const wchar_t* pFile, unsigned index, char language[4], Mp4TextProc callback, void* pUser);

void Mp4Demux_Close(Mp4Demux* pDemux);

#endif
