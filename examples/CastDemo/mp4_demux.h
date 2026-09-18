/* mp4_demux.h - reads the sample tables of an MP4 file so the GPU transcoder can feed frames to
** the Vulkan H.264 decoder. Extracts the video track's SPS/PPS (from avcC), the NAL length prefix
** size, and every sample's file offset, size, decode and composition time, and keyframe flag. The
** audio track is located too (codec and sample table) for the separate audio path.
**
** Only the boxes the transcoder needs are parsed; unknown boxes are skipped. AVC (H.264) only. */
#ifndef CAST_MP4_DEMUX_H
#define CAST_MP4_DEMUX_H

#include <stdint.h>
#include <wchar.h>

typedef struct Mp4Sample
{
    uint64_t offset;          /* byte offset of the sample in the file */
    uint32_t size;            /* sample size in bytes */
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

/* Opens the file and reads the sample tables. Returns NULL if it is not an AVC MP4. */
Mp4Demux* Mp4Demux_Open(const wchar_t* pFile);

/* Video track parameters. */
unsigned Mp4Demux_Width(const Mp4Demux* pDemux);
unsigned Mp4Demux_Height(const Mp4Demux* pDemux);
unsigned Mp4Demux_Timescale(const Mp4Demux* pDemux);
int Mp4Demux_NalLengthSize(const Mp4Demux* pDemux); /* 1, 2 or 4 */
const uint8_t* Mp4Demux_Sps(const Mp4Demux* pDemux, uint32_t* pSize);
const uint8_t* Mp4Demux_Pps(const Mp4Demux* pDemux, uint32_t* pSize);
unsigned Mp4Demux_SampleCount(const Mp4Demux* pDemux);
const Mp4Sample* Mp4Demux_Sample(const Mp4Demux* pDemux, unsigned index);

/* Reads one sample's bytes into pBuffer (AVCC length-prefixed NAL units). Returns bytes read. */
uint32_t Mp4Demux_ReadSample(Mp4Demux* pDemux, unsigned index, uint8_t* pBuffer, uint32_t capacity);

/* Audio track summary for the separate audio path. */
Mp4AudioCodec Mp4Demux_AudioCodec(const Mp4Demux* pDemux);
unsigned Mp4Demux_AudioSampleRate(const Mp4Demux* pDemux);
unsigned Mp4Demux_AudioChannels(const Mp4Demux* pDemux);
unsigned Mp4Demux_AudioTimescale(const Mp4Demux* pDemux);

/* The AAC AudioSpecificConfig (from esds, or synthesised for AAC-LC). Bytes live until Close. */
const uint8_t* Mp4Demux_AudioConfig(const Mp4Demux* pDemux, uint32_t* pSize);

/* Audio (AAC) sample table for passthrough muxing. */
unsigned Mp4Demux_AudioSampleCount(const Mp4Demux* pDemux);
const Mp4Sample* Mp4Demux_AudioSample(const Mp4Demux* pDemux, unsigned index);
uint32_t Mp4Demux_ReadAudioSample(Mp4Demux* pDemux, unsigned index, uint8_t* pBuffer, uint32_t capacity);

void Mp4Demux_Close(Mp4Demux* pDemux);

#endif
