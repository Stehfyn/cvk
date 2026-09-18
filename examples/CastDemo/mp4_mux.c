#define _CRT_SECURE_NO_WARNINGS
/* mp4_mux.c - a minimal progressive MP4 muxer for the GPU proxy: ftyp, a streamed mdat, and a moov
** written on finish with avc1/avcC video and optional mp4a/esds AAC audio. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mp4_mux.h"

#define MAX_PARAM_BYTES       512u
#define MAX_FRAGMENT_SAMPLES  256u

typedef struct MuxSample
{
    uint64_t offset;
    uint32_t size;
    uint32_t duration;
    int      keyframe;
} MuxSample;

/* A growable byte buffer for assembling the moov in memory. */
typedef struct MuxBuffer
{
    uint8_t* data;
    size_t   size;
    size_t   capacity;
} MuxBuffer;

struct Mp4Mux
{
    FILE*      file;
    unsigned   width;
    unsigned   height;
    uint8_t    sps[MAX_PARAM_BYTES];
    uint32_t   spsSize;
    uint8_t    pps[MAX_PARAM_BYTES];
    uint32_t   ppsSize;
    unsigned   videoTimescale;
    int        hasAudio;
    unsigned   audioTimescale;
    unsigned   audioSampleRate;
    unsigned   audioChannels;
    uint8_t    asc[64];
    uint32_t   ascSize;
    int64_t    mdatStart;
    MuxSample* video;
    uint32_t   videoCount;
    uint32_t   videoCapacity;
    MuxSample* audio;
    uint32_t   audioCount;
    uint32_t   audioCapacity;
    int        failed;

    /* Fragmented (fMP4) mode: an init segment up front, then moof+mdat fragments appended as the
    ** transcode produces them, so the file is playable within seconds instead of after the end. */
    int       fragmented;
    double    fullDurationSeconds; /* the whole timeline, so the init segment shows the full seek bar */
    uint32_t  sequenceNumber;
    uint64_t  videoBaseTime;   /* baseMediaDecodeTime of the next video fragment */
    uint64_t  audioBaseTime;
    MuxBuffer fragVideoData;    /* AVCC sample bytes buffered for the current fragment */
    MuxBuffer fragAudioData;
    MuxBuffer fragMoof;         /* reused each flush so fragments allocate nothing on the hot path */
    MuxSample fragVideo[MAX_FRAGMENT_SAMPLES];
    uint32_t  fragVideoCount;
    MuxSample fragAudio[MAX_FRAGMENT_SAMPLES];
    uint32_t  fragAudioCount;
};

static void BufferBytes(MuxBuffer* pBuffer, const void* pData, size_t count)
{
    if (pBuffer->size + count > pBuffer->capacity)
    {
        size_t   new_capacity;
        uint8_t* grown;

        new_capacity = pBuffer->capacity ? pBuffer->capacity * 2u : 4096u;
        while (new_capacity < pBuffer->size + count)
        {
            new_capacity *= 2u;
        }
        grown = (uint8_t*)realloc(pBuffer->data, new_capacity);
        if (NULL == grown)
        {
            return;
        }
        pBuffer->data     = grown;
        pBuffer->capacity = new_capacity;
    }
    memcpy(pBuffer->data + pBuffer->size, pData, count);
    pBuffer->size += count;
}

static void BufferU8(MuxBuffer* pBuffer, uint8_t value)
{
    BufferBytes(pBuffer, &value, 1);
}

static void BufferU16(MuxBuffer* pBuffer, uint16_t value)
{
    uint8_t bytes[2];

    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
    BufferBytes(pBuffer, bytes, 2);
}

static void BufferU32(MuxBuffer* pBuffer, uint32_t value)
{
    uint8_t bytes[4];

    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
    BufferBytes(pBuffer, bytes, 4);
}

static void BufferU64(MuxBuffer* pBuffer, uint64_t value)
{
    BufferU32(pBuffer, (uint32_t)(value >> 32));
    BufferU32(pBuffer, (uint32_t)value);
}

/* Opens a box with a placeholder size, returning its start offset for BoxClose. */
static size_t BoxOpen(MuxBuffer* pBuffer, const char* pType)
{
    size_t position;

    position = pBuffer->size;
    BufferU32(pBuffer, 0);
    BufferBytes(pBuffer, pType, 4);
    return position;
}

/* Backpatches a box's size field from its start offset to the current end. */
static void BoxClose(MuxBuffer* pBuffer, size_t position)
{
    uint32_t box_size;

    box_size                     = (uint32_t)(pBuffer->size - position);
    pBuffer->data[position]      = (uint8_t)(box_size >> 24);
    pBuffer->data[position + 1u] = (uint8_t)(box_size >> 16);
    pBuffer->data[position + 2u] = (uint8_t)(box_size >> 8);
    pBuffer->data[position + 3u] = (uint8_t)box_size;
}

/* The 3x3 video transform matrix (identity in 16.16 / 2.30 fixed point). */
static void BufferMatrix(MuxBuffer* pBuffer)
{
    BufferU32(pBuffer, 0x00010000);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0x00010000);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0x40000000);
}

static int WriteFileBytes(Mp4Mux* pMux, const void* pData, size_t count)
{
    if (0 == count)
    {
        return 1;
    }
    if (1 != fwrite(pData, count, 1, pMux->file))
    {
        pMux->failed = 1;
        return 0;
    }
    return 1;
}

/* Drops a leading Annex-B start code (00 00 01 or 00 00 00 01) from a parameter set, copying the
** raw NAL bytes into a fixed buffer. */
static void StoreParameterSet(const uint8_t* pData, uint32_t size, uint8_t* pOut, uint32_t* pOutSize)
{
    uint32_t start;

    start = 0;
    if (size >= 4 && 0 == pData[0] && 0 == pData[1] && 0 == pData[2] && 1 == pData[3])
    {
        start = 4;
    }
    else if (size >= 3 && 0 == pData[0] && 0 == pData[1] && 1 == pData[2])
    {
        start = 3;
    }
    if (size - start > MAX_PARAM_BYTES)
    {
        *pOutSize = 0;
        return;
    }
    memcpy(pOut, pData + start, size - start);
    *pOutSize = size - start;
}

Mp4Mux* Mp4Mux_Create(const wchar_t* pPath,
                      unsigned       width,
                      unsigned       height,
                      const uint8_t* pSps,
                      uint32_t       spsSize,
                      const uint8_t* pPps,
                      uint32_t       ppsSize,
                      unsigned       videoTimescale,
                      int            hasAudio,
                      unsigned       audioTimescale,
                      unsigned       audioSampleRate,
                      unsigned       audioChannels,
                      const uint8_t* pAsc,
                      uint32_t       ascSize)
{
    Mp4Mux* pMux;
    uint8_t ftyp[24];
    uint8_t mdat_header[8];

    pMux = (Mp4Mux*)calloc(1, sizeof(*pMux));
    if (NULL == pMux)
    {
        return NULL;
    }
    pMux->file = _wfopen(pPath, L"wb");
    if (NULL == pMux->file)
    {
        free(pMux);
        return NULL;
    }
    pMux->width           = width;
    pMux->height          = height;
    pMux->videoTimescale  = videoTimescale ? videoTimescale : 90000u;
    pMux->hasAudio        = hasAudio;
    pMux->audioTimescale  = audioTimescale;
    pMux->audioSampleRate = audioSampleRate;
    pMux->audioChannels   = audioChannels;
    StoreParameterSet(pSps, spsSize, pMux->sps, &pMux->spsSize);
    StoreParameterSet(pPps, ppsSize, pMux->pps, &pMux->ppsSize);
    if (hasAudio && NULL != pAsc && ascSize > 0 && ascSize <= sizeof(pMux->asc))
    {
        memcpy(pMux->asc, pAsc, ascSize);
        pMux->ascSize = ascSize;
    }

    /* ftyp: major 'isom', minor 0x200, compatible isom/iso2/avc1/mp41. */
    memcpy(ftyp, "\0\0\0\30ftypisom", 12);
    ftyp[8]  = 'i'; ftyp[9] = 's'; ftyp[10] = 'o'; ftyp[11] = 'm';
    ftyp[12] = 0; ftyp[13] = 0; ftyp[14] = 2; ftyp[15] = 0;
    memcpy(ftyp + 16, "iso2", 4);
    memcpy(ftyp + 20, "avc1", 4);
    if (!WriteFileBytes(pMux, ftyp, sizeof(ftyp)))
    {
        Mp4Mux_Destroy(pMux);
        return NULL;
    }

    /* mdat: size patched on finish; samples stream in after this header. */
    pMux->mdatStart = _ftelli64(pMux->file);
    memset(mdat_header, 0, sizeof(mdat_header));
    memcpy(mdat_header + 4, "mdat", 4);
    if (!WriteFileBytes(pMux, mdat_header, sizeof(mdat_header)))
    {
        Mp4Mux_Destroy(pMux);
        return NULL;
    }
    return pMux;
}

static MuxSample* AppendSample(MuxSample** ppArray, uint32_t* pCount, uint32_t* pCapacity)
{
    if (*pCount == *pCapacity)
    {
        uint32_t   new_capacity;
        MuxSample* grown;

        new_capacity = *pCapacity ? *pCapacity * 2u : 4096u;
        grown        = (MuxSample*)realloc(*ppArray, (size_t)new_capacity * sizeof(MuxSample));
        if (NULL == grown)
        {
            return NULL;
        }
        *ppArray   = grown;
        *pCapacity = new_capacity;
    }
    return &(*ppArray)[(*pCount)++];
}

/* Rewrites Annex-B NAL units to 4-byte length-prefixed form, appended to a growable buffer.
** Returns the number of bytes appended. */
static uint32_t AppendAvcc(MuxBuffer* pBuffer, const uint8_t* pData, uint32_t size)
{
    uint32_t total;
    uint32_t i;
    uint32_t nal_start;

    total     = 0;
    i         = 0;
    nal_start = 0;
    while (i + 3 <= size)
    {
        if (0 == pData[i] && 0 == pData[i + 1] && 1 == pData[i + 2])
        {
            if (nal_start > 0 && i > nal_start)
            {
                uint32_t nal_length;

                nal_length = i - nal_start;
                while (nal_length > 0 && 0 == pData[nal_start + nal_length - 1])
                {
                    --nal_length;
                }
                BufferU32(pBuffer, nal_length);
                BufferBytes(pBuffer, pData + nal_start, nal_length);
                total += 4u + nal_length;
            }
            i += 3;
            nal_start = i;
        }
        else
        {
            ++i;
        }
    }
    if (nal_start > 0 && nal_start < size)
    {
        BufferU32(pBuffer, size - nal_start);
        BufferBytes(pBuffer, pData + nal_start, size - nal_start);
        total += 4u + (size - nal_start);
    }
    return total;
}

int Mp4Mux_AddVideo(Mp4Mux* pMux, const uint8_t* pData, uint32_t size, uint32_t duration, int keyframe)
{
    MuxSample* pSample;
    uint64_t   offset;
    uint32_t   total;
    uint32_t   i;
    uint32_t   nal_start;

    if (pMux->failed)
    {
        return 0;
    }
    if (pMux->fragmented)
    {
        MuxSample* p_frag;

        if (pMux->fragVideoCount >= MAX_FRAGMENT_SAMPLES)
        {
            return 0; /* the driver must flush the fragment before it overflows */
        }
        p_frag           = &pMux->fragVideo[pMux->fragVideoCount++];
        p_frag->offset   = pMux->fragVideoData.size;
        p_frag->size     = AppendAvcc(&pMux->fragVideoData, pData, size);
        p_frag->duration = duration;
        p_frag->keyframe = keyframe;
        return 1;
    }
    offset = (uint64_t)_ftelli64(pMux->file);
    total  = 0;

    /* Walk the Annex-B NAL units and re-emit each as a 4-byte length prefix + payload. */
    i         = 0;
    nal_start = 0;
    while (i + 3 <= size)
    {
        int is_start;

        is_start = (0 == pData[i] && 0 == pData[i + 1] && 1 == pData[i + 2]);
        if (is_start)
        {
            /* Emit the NAL that ended here (trailing zero bytes belong to this start code). */
            if (nal_start > 0 && i > nal_start)
            {
                uint32_t nal_length;
                uint8_t  prefix[4];

                nal_length = i - nal_start;
                while (nal_length > 0 && 0 == pData[nal_start + nal_length - 1])
                {
                    --nal_length;
                }
                prefix[0] = (uint8_t)(nal_length >> 24);
                prefix[1] = (uint8_t)(nal_length >> 16);
                prefix[2] = (uint8_t)(nal_length >> 8);
                prefix[3] = (uint8_t)nal_length;
                if (!WriteFileBytes(pMux, prefix, 4) || !WriteFileBytes(pMux, pData + nal_start, nal_length))
                {
                    return 0;
                }
                total += 4u + nal_length;
            }
            i += 3;
            nal_start = i;
        }
        else
        {
            ++i;
        }
    }
    if (nal_start > 0 && nal_start < size)
    {
        uint32_t nal_length;
        uint8_t  prefix[4];

        nal_length = size - nal_start;
        prefix[0]  = (uint8_t)(nal_length >> 24);
        prefix[1]  = (uint8_t)(nal_length >> 16);
        prefix[2]  = (uint8_t)(nal_length >> 8);
        prefix[3]  = (uint8_t)nal_length;
        if (!WriteFileBytes(pMux, prefix, 4) || !WriteFileBytes(pMux, pData + nal_start, nal_length))
        {
            return 0;
        }
        total += 4u + nal_length;
    }

    pSample = AppendSample(&pMux->video, &pMux->videoCount, &pMux->videoCapacity);
    if (NULL == pSample)
    {
        pMux->failed = 1;
        return 0;
    }
    pSample->offset   = offset;
    pSample->size     = total;
    pSample->duration = duration;
    pSample->keyframe = keyframe;
    return 1;
}

int Mp4Mux_AddAudio(Mp4Mux* pMux, const uint8_t* pData, uint32_t size, uint32_t duration)
{
    MuxSample* pSample;
    uint64_t   offset;

    if (pMux->failed || !pMux->hasAudio)
    {
        return 0;
    }
    if (pMux->fragmented)
    {
        MuxSample* p_frag;

        if (pMux->fragAudioCount >= MAX_FRAGMENT_SAMPLES)
        {
            return 0;
        }
        p_frag           = &pMux->fragAudio[pMux->fragAudioCount++];
        p_frag->offset   = pMux->fragAudioData.size;
        p_frag->size     = size;
        p_frag->duration = duration;
        p_frag->keyframe = 1;
        BufferBytes(&pMux->fragAudioData, pData, size);
        return 1;
    }
    offset = (uint64_t)_ftelli64(pMux->file);
    if (!WriteFileBytes(pMux, pData, size))
    {
        return 0;
    }
    pSample = AppendSample(&pMux->audio, &pMux->audioCount, &pMux->audioCapacity);
    if (NULL == pSample)
    {
        pMux->failed = 1;
        return 0;
    }
    pSample->offset   = offset;
    pSample->size     = size;
    pSample->duration = duration;
    pSample->keyframe = 1;
    return 1;
}

/* Emits stts/stsc/stsz/co64 (and stss for video) for one track's sample list. */
static void WriteSampleTables(MuxBuffer* pBuffer, const MuxSample* pSamples, uint32_t count, int isVideo)
{
    size_t   box;
    uint32_t i;
    uint32_t run_count;
    size_t   entry_count_position;
    uint32_t entries;

    /* stts: run-length of equal durations. */
    box = BoxOpen(pBuffer, "stts");
    BufferU32(pBuffer, 0);
    entry_count_position = pBuffer->size;
    BufferU32(pBuffer, 0);
    entries = 0;
    i       = 0;
    while (i < count)
    {
        run_count = 1;
        while (i + run_count < count && pSamples[i + run_count].duration == pSamples[i].duration)
        {
            ++run_count;
        }
        BufferU32(pBuffer, run_count);
        BufferU32(pBuffer, pSamples[i].duration);
        ++entries;
        i += run_count;
    }
    pBuffer->data[entry_count_position]      = (uint8_t)(entries >> 24);
    pBuffer->data[entry_count_position + 1u] = (uint8_t)(entries >> 16);
    pBuffer->data[entry_count_position + 2u] = (uint8_t)(entries >> 8);
    pBuffer->data[entry_count_position + 3u] = (uint8_t)entries;
    BoxClose(pBuffer, box);

    /* stsc: one sample per chunk. */
    box = BoxOpen(pBuffer, "stsc");
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 1);
    BufferU32(pBuffer, 1);
    BufferU32(pBuffer, 1);
    BufferU32(pBuffer, 1);
    BoxClose(pBuffer, box);

    /* stsz: explicit per-sample sizes. */
    box = BoxOpen(pBuffer, "stsz");
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, count);
    for (i = 0; i < count; ++i)
    {
        BufferU32(pBuffer, pSamples[i].size);
    }
    BoxClose(pBuffer, box);

    /* co64: one 64-bit chunk offset per sample. */
    box = BoxOpen(pBuffer, "co64");
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, count);
    for (i = 0; i < count; ++i)
    {
        BufferU64(pBuffer, pSamples[i].offset);
    }
    BoxClose(pBuffer, box);

    /* stss: sync-sample list for video (1-based sample numbers). */
    if (isVideo)
    {
        size_t   sync_count_position;
        uint32_t sync_count;

        box = BoxOpen(pBuffer, "stss");
        BufferU32(pBuffer, 0);
        sync_count_position = pBuffer->size;
        BufferU32(pBuffer, 0);
        sync_count = 0;
        for (i = 0; i < count; ++i)
        {
            if (pSamples[i].keyframe)
            {
                BufferU32(pBuffer, i + 1u);
                ++sync_count;
            }
        }
        pBuffer->data[sync_count_position]      = (uint8_t)(sync_count >> 24);
        pBuffer->data[sync_count_position + 1u] = (uint8_t)(sync_count >> 16);
        pBuffer->data[sync_count_position + 2u] = (uint8_t)(sync_count >> 8);
        pBuffer->data[sync_count_position + 3u] = (uint8_t)sync_count;
        BoxClose(pBuffer, box);
    }
}

/* avc1 sample entry with an avcC configuration record built from the stored SPS/PPS. */
static void WriteVideoSampleEntry(Mp4Mux* pMux, MuxBuffer* pBuffer)
{
    size_t box;
    size_t avcc;

    box = BoxOpen(pBuffer, "stsd");
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 1);
    {
        size_t avc1;

        avc1 = BoxOpen(pBuffer, "avc1");
        BufferU8(pBuffer, 0); BufferU8(pBuffer, 0); BufferU8(pBuffer, 0);
        BufferU8(pBuffer, 0); BufferU8(pBuffer, 0); BufferU8(pBuffer, 0);
        BufferU16(pBuffer, 1);  /* data reference index */
        BufferU16(pBuffer, 0);
        BufferU16(pBuffer, 0);
        BufferU32(pBuffer, 0); BufferU32(pBuffer, 0); BufferU32(pBuffer, 0);
        BufferU16(pBuffer, (uint16_t)pMux->width);
        BufferU16(pBuffer, (uint16_t)pMux->height);
        BufferU32(pBuffer, 0x00480000); /* 72 dpi */
        BufferU32(pBuffer, 0x00480000);
        BufferU32(pBuffer, 0);
        BufferU16(pBuffer, 1);          /* frame count */
        {
            uint8_t compressor[32];

            memset(compressor, 0, sizeof(compressor));
            BufferBytes(pBuffer, compressor, sizeof(compressor));
        }
        BufferU16(pBuffer, 0x0018);     /* depth */
        BufferU16(pBuffer, 0xFFFF);     /* predefined */

        avcc = BoxOpen(pBuffer, "avcC");
        BufferU8(pBuffer, 1);                              /* configuration version */
        BufferU8(pBuffer, pMux->spsSize > 1 ? pMux->sps[1] : 0x4D); /* AVCProfileIndication */
        BufferU8(pBuffer, pMux->spsSize > 2 ? pMux->sps[2] : 0x00); /* profile compatibility */
        BufferU8(pBuffer, pMux->spsSize > 3 ? pMux->sps[3] : 0x1F); /* AVCLevelIndication */
        BufferU8(pBuffer, 0xFF);                           /* 6 bits reserved | lengthSizeMinusOne = 3 */
        BufferU8(pBuffer, 0xE1);                           /* 3 bits reserved | numSPS = 1 */
        BufferU16(pBuffer, (uint16_t)pMux->spsSize);
        BufferBytes(pBuffer, pMux->sps, pMux->spsSize);
        BufferU8(pBuffer, 1);                              /* numPPS */
        BufferU16(pBuffer, (uint16_t)pMux->ppsSize);
        BufferBytes(pBuffer, pMux->pps, pMux->ppsSize);
        BoxClose(pBuffer, avcc);
        BoxClose(pBuffer, avc1);
    }
    BoxClose(pBuffer, box);
}

/* mp4a sample entry with an esds descriptor carrying the AudioSpecificConfig. */
static void WriteAudioSampleEntry(Mp4Mux* pMux, MuxBuffer* pBuffer)
{
    size_t  box;
    uint8_t dsi_len;
    uint8_t dcd_len;
    uint8_t es_len;

    box = BoxOpen(pBuffer, "stsd");
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 1);
    {
        size_t mp4a;
        size_t esds;

        mp4a = BoxOpen(pBuffer, "mp4a");
        BufferU8(pBuffer, 0); BufferU8(pBuffer, 0); BufferU8(pBuffer, 0);
        BufferU8(pBuffer, 0); BufferU8(pBuffer, 0); BufferU8(pBuffer, 0);
        BufferU16(pBuffer, 1);  /* data reference index */
        BufferU32(pBuffer, 0);
        BufferU32(pBuffer, 0);
        BufferU16(pBuffer, (uint16_t)pMux->audioChannels);
        BufferU16(pBuffer, 16); /* sample size */
        BufferU16(pBuffer, 0);
        BufferU16(pBuffer, 0);
        BufferU32(pBuffer, pMux->audioSampleRate << 16);

        esds    = BoxOpen(pBuffer, "esds");
        BufferU32(pBuffer, 0);
        dsi_len = (uint8_t)pMux->ascSize;
        dcd_len = (uint8_t)(13u + 2u + dsi_len);
        es_len  = (uint8_t)(3u + 2u + dcd_len + 3u);
        BufferU8(pBuffer, 0x03);              /* ES_Descriptor */
        BufferU8(pBuffer, es_len);
        BufferU16(pBuffer, 0);                /* ES_ID */
        BufferU8(pBuffer, 0);                 /* flags */
        BufferU8(pBuffer, 0x04);              /* DecoderConfigDescriptor */
        BufferU8(pBuffer, dcd_len);
        BufferU8(pBuffer, 0x40);              /* objectTypeIndication: MPEG-4 AAC */
        BufferU8(pBuffer, 0x15);              /* streamType audio | !upstream | reserved */
        BufferU8(pBuffer, 0); BufferU8(pBuffer, 0); BufferU8(pBuffer, 0); /* bufferSizeDB */
        BufferU32(pBuffer, 0);                /* max bitrate */
        BufferU32(pBuffer, 0);                /* avg bitrate */
        BufferU8(pBuffer, 0x05);              /* DecoderSpecificInfo */
        BufferU8(pBuffer, dsi_len);
        BufferBytes(pBuffer, pMux->asc, pMux->ascSize);
        BufferU8(pBuffer, 0x06);              /* SLConfigDescriptor */
        BufferU8(pBuffer, 1);
        BufferU8(pBuffer, 2);
        BoxClose(pBuffer, esds);
        BoxClose(pBuffer, mp4a);
    }
    BoxClose(pBuffer, box);
}

static uint64_t TrackDuration(const MuxSample* pSamples, uint32_t count)
{
    uint64_t total;
    uint32_t i;

    total = 0;
    for (i = 0; i < count; ++i)
    {
        total += pSamples[i].duration;
    }
    return total;
}

static void WriteTrack(Mp4Mux* pMux, MuxBuffer* pBuffer, int isVideo, uint32_t movieTimescale)
{
    size_t   trak;
    size_t   mdia;
    size_t   minf;
    size_t   box;
    const MuxSample* p_samples;
    uint32_t count;
    uint32_t timescale;
    uint64_t track_duration;
    uint32_t movie_duration;

    if (isVideo)
    {
        p_samples = pMux->video;
        count     = pMux->videoCount;
        timescale = pMux->videoTimescale;
    }
    else
    {
        p_samples = pMux->audio;
        count     = pMux->audioCount;
        timescale = pMux->audioTimescale ? pMux->audioTimescale : pMux->audioSampleRate;
    }
    track_duration = TrackDuration(p_samples, count);
    movie_duration = timescale ? (uint32_t)(track_duration * movieTimescale / timescale) : 0;

    trak = BoxOpen(pBuffer, "trak");

    box = BoxOpen(pBuffer, "tkhd");
    BufferU32(pBuffer, 0x00000007); /* enabled | in movie | in preview */
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, isVideo ? 1u : 2u);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, movie_duration);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU16(pBuffer, 0);                       /* layer */
    BufferU16(pBuffer, 0);                       /* alternate group */
    BufferU16(pBuffer, isVideo ? 0u : 0x0100u);  /* volume */
    BufferU16(pBuffer, 0);
    BufferMatrix(pBuffer);
    BufferU32(pBuffer, isVideo ? (pMux->width << 16) : 0u);
    BufferU32(pBuffer, isVideo ? (pMux->height << 16) : 0u);
    BoxClose(pBuffer, box);

    mdia = BoxOpen(pBuffer, "mdia");
    box  = BoxOpen(pBuffer, "mdhd");
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, timescale);
    BufferU32(pBuffer, (uint32_t)track_duration);
    BufferU16(pBuffer, 0x55C4); /* language 'und' */
    BufferU16(pBuffer, 0);
    BoxClose(pBuffer, box);

    box = BoxOpen(pBuffer, "hdlr");
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferBytes(pBuffer, isVideo ? "vide" : "soun", 4);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferBytes(pBuffer, isVideo ? "VideoHandler" : "SoundHandler", isVideo ? 13u : 13u);
    BoxClose(pBuffer, box);

    minf = BoxOpen(pBuffer, "minf");
    if (isVideo)
    {
        box = BoxOpen(pBuffer, "vmhd");
        BufferU32(pBuffer, 0x00000001);
        BufferU32(pBuffer, 0);
        BufferU16(pBuffer, 0);
        BoxClose(pBuffer, box);
    }
    else
    {
        box = BoxOpen(pBuffer, "smhd");
        BufferU32(pBuffer, 0);
        BufferU16(pBuffer, 0);
        BufferU16(pBuffer, 0);
        BoxClose(pBuffer, box);
    }

    box = BoxOpen(pBuffer, "dinf");
    {
        size_t dref;
        size_t url;

        dref = BoxOpen(pBuffer, "dref");
        BufferU32(pBuffer, 0);
        BufferU32(pBuffer, 1);
        url = BoxOpen(pBuffer, "url ");
        BufferU32(pBuffer, 0x00000001); /* self-contained */
        BoxClose(pBuffer, url);
        BoxClose(pBuffer, dref);
    }
    BoxClose(pBuffer, box);

    box = BoxOpen(pBuffer, "stbl");
    if (isVideo)
    {
        WriteVideoSampleEntry(pMux, pBuffer);
    }
    else
    {
        WriteAudioSampleEntry(pMux, pBuffer);
    }
    WriteSampleTables(pBuffer, p_samples, count, isVideo);
    BoxClose(pBuffer, box);

    BoxClose(pBuffer, minf);
    BoxClose(pBuffer, mdia);
    BoxClose(pBuffer, trak);
}

/* trex (track extends) inside mvex: per-track defaults for the fragments that follow. */
static void WriteTrex(MuxBuffer* pBuffer, uint32_t trackId, uint32_t defaultSampleFlags)
{
    size_t box;

    box = BoxOpen(pBuffer, "trex");
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, trackId);
    BufferU32(pBuffer, 1); /* default sample description index */
    BufferU32(pBuffer, 0); /* default sample duration */
    BufferU32(pBuffer, 0); /* default sample size */
    BufferU32(pBuffer, defaultSampleFlags);
    BoxClose(pBuffer, box);
}

/* Writes the fragmented-MP4 init segment: ftyp then a moov whose tracks carry empty sample tables
** and an mvex declaring the movie extends into the fragments that follow. */
static int WriteFragmentedInit(Mp4Mux* pMux)
{
    uint8_t  ftyp[24];
    size_t   moov;
    uint32_t movie_timescale;
    uint32_t movie_duration;

    memcpy(ftyp, "\0\0\0\30ftypiso5", 12);
    ftyp[8] = 'i'; ftyp[9] = 's'; ftyp[10] = 'o'; ftyp[11] = '5';
    ftyp[12] = 0; ftyp[13] = 0; ftyp[14] = 2; ftyp[15] = 0;
    memcpy(ftyp + 16, "iso6", 4);
    memcpy(ftyp + 20, "mp41", 4);
    if (!WriteFileBytes(pMux, ftyp, sizeof(ftyp)))
    {
        return 0;
    }

    movie_timescale        = 1000;
    movie_duration         = (uint32_t)(pMux->fullDurationSeconds * movie_timescale + 0.5);
    pMux->fragMoof.size    = 0;
    moov                   = BoxOpen(&pMux->fragMoof, "moov");
    {
        size_t mvhd;

        mvhd = BoxOpen(&pMux->fragMoof, "mvhd");
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, movie_timescale);
        BufferU32(&pMux->fragMoof, movie_duration);
        BufferU32(&pMux->fragMoof, 0x00010000);
        BufferU16(&pMux->fragMoof, 0x0100);
        BufferU16(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferMatrix(&pMux->fragMoof);
        BufferU32(&pMux->fragMoof, 0); BufferU32(&pMux->fragMoof, 0); BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0); BufferU32(&pMux->fragMoof, 0); BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, (pMux->hasAudio ? 3u : 2u));
        BoxClose(&pMux->fragMoof, mvhd);
    }
    WriteTrack(pMux, &pMux->fragMoof, 1, movie_timescale); /* empty tables: videoCount is 0 */
    if (pMux->hasAudio)
    {
        WriteTrack(pMux, &pMux->fragMoof, 0, movie_timescale);
    }
    {
        size_t mvex;

        mvex = BoxOpen(&pMux->fragMoof, "mvex");
        {
            size_t mehd;

            mehd = BoxOpen(&pMux->fragMoof, "mehd");
            BufferU32(&pMux->fragMoof, 0);
            BufferU32(&pMux->fragMoof, movie_duration);
            BoxClose(&pMux->fragMoof, mehd);
        }
        WriteTrex(&pMux->fragMoof, 1, 0x01010000); /* video defaults to a non-sync sample */
        if (pMux->hasAudio)
        {
            WriteTrex(&pMux->fragMoof, 2, 0x02000000);
        }
        BoxClose(&pMux->fragMoof, mvex);
    }
    BoxClose(&pMux->fragMoof, moov);
    return (NULL != pMux->fragMoof.data) && WriteFileBytes(pMux, pMux->fragMoof.data, pMux->fragMoof.size);
}

/* Writes one movie fragment (moof + mdat) for the samples buffered since the last flush, appended to
** the file and flushed to disk so a progressive server can hand it out immediately. */
int Mp4Mux_FlushFragment(Mp4Mux* pMux)
{
    size_t   moof;
    size_t   video_data_offset_pos;
    size_t   audio_data_offset_pos;
    uint32_t moof_size;
    uint32_t video_bytes;
    uint32_t audio_bytes;
    uint32_t mdat_size;
    uint8_t  mdat_header[8];
    uint32_t i;
    uint64_t video_ticks;
    uint64_t audio_ticks;

    if (pMux->failed || !pMux->fragmented || (0 == pMux->fragVideoCount && 0 == pMux->fragAudioCount))
    {
        return 0;
    }
    video_bytes           = (uint32_t)pMux->fragVideoData.size;
    audio_bytes           = (uint32_t)pMux->fragAudioData.size;
    video_data_offset_pos = 0;
    audio_data_offset_pos = 0;
    pMux->fragMoof.size   = 0;

    moof = BoxOpen(&pMux->fragMoof, "moof");
    {
        size_t mfhd;

        mfhd = BoxOpen(&pMux->fragMoof, "mfhd");
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, ++pMux->sequenceNumber);
        BoxClose(&pMux->fragMoof, mfhd);
    }
    if (pMux->fragVideoCount > 0)
    {
        size_t traf;
        size_t trun;

        traf = BoxOpen(&pMux->fragMoof, "traf");
        {
            size_t tfhd;
            size_t tfdt;

            tfhd = BoxOpen(&pMux->fragMoof, "tfhd");
            BufferU32(&pMux->fragMoof, 0x00020000); /* default-base-is-moof */
            BufferU32(&pMux->fragMoof, 1);          /* track ID */
            BoxClose(&pMux->fragMoof, tfhd);
            tfdt = BoxOpen(&pMux->fragMoof, "tfdt");
            BufferU32(&pMux->fragMoof, 0x01000000); /* version 1 */
            BufferU64(&pMux->fragMoof, pMux->videoBaseTime);
            BoxClose(&pMux->fragMoof, tfdt);
        }
        trun = BoxOpen(&pMux->fragMoof, "trun");
        BufferU32(&pMux->fragMoof, 0x00000701); /* data-offset, sample-duration, sample-size, sample-flags */
        BufferU32(&pMux->fragMoof, pMux->fragVideoCount);
        video_data_offset_pos = pMux->fragMoof.size;
        BufferU32(&pMux->fragMoof, 0); /* data offset, patched below */
        for (i = 0; i < pMux->fragVideoCount; ++i)
        {
            BufferU32(&pMux->fragMoof, pMux->fragVideo[i].duration);
            BufferU32(&pMux->fragMoof, pMux->fragVideo[i].size);
            BufferU32(&pMux->fragMoof, pMux->fragVideo[i].keyframe ? 0x02000000u : 0x01010000u);
        }
        BoxClose(&pMux->fragMoof, trun);
        BoxClose(&pMux->fragMoof, traf);
    }
    if (pMux->fragAudioCount > 0)
    {
        size_t traf;
        size_t trun;

        traf = BoxOpen(&pMux->fragMoof, "traf");
        {
            size_t tfhd;
            size_t tfdt;

            tfhd = BoxOpen(&pMux->fragMoof, "tfhd");
            BufferU32(&pMux->fragMoof, 0x00020000);
            BufferU32(&pMux->fragMoof, 2);
            BoxClose(&pMux->fragMoof, tfhd);
            tfdt = BoxOpen(&pMux->fragMoof, "tfdt");
            BufferU32(&pMux->fragMoof, 0x01000000);
            BufferU64(&pMux->fragMoof, pMux->audioBaseTime);
            BoxClose(&pMux->fragMoof, tfdt);
        }
        trun = BoxOpen(&pMux->fragMoof, "trun");
        BufferU32(&pMux->fragMoof, 0x00000301); /* data-offset, sample-duration, sample-size */
        BufferU32(&pMux->fragMoof, pMux->fragAudioCount);
        audio_data_offset_pos = pMux->fragMoof.size;
        BufferU32(&pMux->fragMoof, 0);
        for (i = 0; i < pMux->fragAudioCount; ++i)
        {
            BufferU32(&pMux->fragMoof, pMux->fragAudio[i].duration);
            BufferU32(&pMux->fragMoof, pMux->fragAudio[i].size);
        }
        BoxClose(&pMux->fragMoof, trun);
        BoxClose(&pMux->fragMoof, traf);
    }
    BoxClose(&pMux->fragMoof, moof);
    moof_size = (uint32_t)pMux->fragMoof.size;

    /* Sample data offsets are relative to the moof start (default-base-is-moof). */
    if (pMux->fragVideoCount > 0)
    {
        uint32_t off = moof_size + 8u;

        pMux->fragMoof.data[video_data_offset_pos]      = (uint8_t)(off >> 24);
        pMux->fragMoof.data[video_data_offset_pos + 1u] = (uint8_t)(off >> 16);
        pMux->fragMoof.data[video_data_offset_pos + 2u] = (uint8_t)(off >> 8);
        pMux->fragMoof.data[video_data_offset_pos + 3u] = (uint8_t)off;
    }
    if (pMux->fragAudioCount > 0)
    {
        uint32_t off = moof_size + 8u + video_bytes;

        pMux->fragMoof.data[audio_data_offset_pos]      = (uint8_t)(off >> 24);
        pMux->fragMoof.data[audio_data_offset_pos + 1u] = (uint8_t)(off >> 16);
        pMux->fragMoof.data[audio_data_offset_pos + 2u] = (uint8_t)(off >> 8);
        pMux->fragMoof.data[audio_data_offset_pos + 3u] = (uint8_t)off;
    }

    mdat_size    = 8u + video_bytes + audio_bytes;
    mdat_header[0] = (uint8_t)(mdat_size >> 24);
    mdat_header[1] = (uint8_t)(mdat_size >> 16);
    mdat_header[2] = (uint8_t)(mdat_size >> 8);
    mdat_header[3] = (uint8_t)mdat_size;
    memcpy(mdat_header + 4, "mdat", 4);
    if (!WriteFileBytes(pMux, pMux->fragMoof.data, moof_size) || !WriteFileBytes(pMux, mdat_header, sizeof(mdat_header)) ||
        !WriteFileBytes(pMux, pMux->fragVideoData.data, video_bytes) || (audio_bytes > 0 && !WriteFileBytes(pMux, pMux->fragAudioData.data, audio_bytes)))
    {
        return 0;
    }
    (void)fflush(pMux->file);

    video_ticks = 0;
    for (i = 0; i < pMux->fragVideoCount; ++i)
    {
        video_ticks += pMux->fragVideo[i].duration;
    }
    audio_ticks = 0;
    for (i = 0; i < pMux->fragAudioCount; ++i)
    {
        audio_ticks += pMux->fragAudio[i].duration;
    }
    pMux->videoBaseTime += video_ticks;
    pMux->audioBaseTime += audio_ticks;
    pMux->fragVideoCount   = 0;
    pMux->fragAudioCount   = 0;
    pMux->fragVideoData.size = 0;
    pMux->fragAudioData.size = 0;
    return 1;
}

int Mp4Mux_Finish(Mp4Mux* pMux)
{
    MuxBuffer buffer;
    int64_t   mdat_end;
    uint64_t  mdat_size;
    uint8_t   patch[4];
    size_t    moov;
    uint32_t  movie_timescale;
    uint64_t  video_seconds_scaled;
    uint64_t  audio_seconds_scaled;
    uint32_t  movie_duration;
    int       ok;

    if (pMux->fragmented)
    {
        (void)Mp4Mux_FlushFragment(pMux); /* emit any trailing samples */
        (void)fflush(pMux->file);
        return !pMux->failed;
    }
    if (pMux->failed || 0 == pMux->videoCount)
    {
        return 0;
    }

    /* Patch the streamed mdat's size field. */
    mdat_end  = _ftelli64(pMux->file);
    mdat_size = (uint64_t)(mdat_end - pMux->mdatStart);
    if (mdat_size > 0xFFFFFFFFu)
    {
        return 0; /* the proxy is not expected to exceed 4 GiB */
    }
    if (0 != _fseeki64(pMux->file, pMux->mdatStart, SEEK_SET))
    {
        return 0;
    }
    patch[0] = (uint8_t)(mdat_size >> 24);
    patch[1] = (uint8_t)(mdat_size >> 16);
    patch[2] = (uint8_t)(mdat_size >> 8);
    patch[3] = (uint8_t)mdat_size;
    if (!WriteFileBytes(pMux, patch, 4) || 0 != _fseeki64(pMux->file, mdat_end, SEEK_SET))
    {
        return 0;
    }

    memset(&buffer, 0, sizeof(buffer));
    movie_timescale = 1000;
    video_seconds_scaled = pMux->videoTimescale ? TrackDuration(pMux->video, pMux->videoCount) * movie_timescale / pMux->videoTimescale : 0;
    audio_seconds_scaled = (pMux->hasAudio && pMux->audioTimescale) ? TrackDuration(pMux->audio, pMux->audioCount) * movie_timescale / pMux->audioTimescale : 0;
    movie_duration       = (uint32_t)(video_seconds_scaled > audio_seconds_scaled ? video_seconds_scaled : audio_seconds_scaled);

    moov = BoxOpen(&buffer, "moov");
    {
        size_t mvhd;

        mvhd = BoxOpen(&buffer, "mvhd");
        BufferU32(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, movie_timescale);
        BufferU32(&buffer, movie_duration);
        BufferU32(&buffer, 0x00010000); /* rate 1.0 */
        BufferU16(&buffer, 0x0100);     /* volume 1.0 */
        BufferU16(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferMatrix(&buffer);
        BufferU32(&buffer, 0); BufferU32(&buffer, 0); BufferU32(&buffer, 0);
        BufferU32(&buffer, 0); BufferU32(&buffer, 0); BufferU32(&buffer, 0);
        BufferU32(&buffer, (pMux->hasAudio && pMux->audioCount > 0) ? 3u : 2u); /* next track ID */
        BoxClose(&buffer, mvhd);
    }
    WriteTrack(pMux, &buffer, 1, movie_timescale);
    if (pMux->hasAudio && pMux->audioCount > 0)
    {
        WriteTrack(pMux, &buffer, 0, movie_timescale);
    }
    BoxClose(&buffer, moov);

    ok = (NULL != buffer.data) && WriteFileBytes(pMux, buffer.data, buffer.size);
    free(buffer.data);
    return ok && !pMux->failed;
}

Mp4Mux* Mp4Mux_CreateFragmented(const wchar_t* pPath,
                                unsigned       width,
                                unsigned       height,
                                const uint8_t* pSps,
                                uint32_t       spsSize,
                                const uint8_t* pPps,
                                uint32_t       ppsSize,
                                unsigned       videoTimescale,
                                int            hasAudio,
                                unsigned       audioTimescale,
                                unsigned       audioSampleRate,
                                unsigned       audioChannels,
                                const uint8_t* pAsc,
                                uint32_t       ascSize,
                                double         fullDurationSeconds)
{
    Mp4Mux* pMux;

    pMux = (Mp4Mux*)calloc(1, sizeof(*pMux));
    if (NULL == pMux)
    {
        return NULL;
    }
    pMux->file = _wfopen(pPath, L"wb");
    if (NULL == pMux->file)
    {
        free(pMux);
        return NULL;
    }
    pMux->fragmented          = 1;
    pMux->fullDurationSeconds = fullDurationSeconds;
    pMux->width               = width;
    pMux->height              = height;
    pMux->videoTimescale      = videoTimescale ? videoTimescale : 90000u;
    pMux->hasAudio            = hasAudio;
    pMux->audioTimescale      = audioTimescale;
    pMux->audioSampleRate     = audioSampleRate;
    pMux->audioChannels       = audioChannels;
    StoreParameterSet(pSps, spsSize, pMux->sps, &pMux->spsSize);
    StoreParameterSet(pPps, ppsSize, pMux->pps, &pMux->ppsSize);
    if (hasAudio && NULL != pAsc && ascSize > 0 && ascSize <= sizeof(pMux->asc))
    {
        memcpy(pMux->asc, pAsc, ascSize);
        pMux->ascSize = ascSize;
    }
    if (!WriteFragmentedInit(pMux))
    {
        Mp4Mux_Destroy(pMux);
        return NULL;
    }
    (void)fflush(pMux->file);
    return pMux;
}

void Mp4Mux_Destroy(Mp4Mux* pMux)
{
    if (NULL == pMux)
    {
        return;
    }
    if (NULL != pMux->file)
    {
        (void)fclose(pMux->file);
    }
    free(pMux->video);
    free(pMux->audio);
    free(pMux->fragVideoData.data);
    free(pMux->fragAudioData.data);
    free(pMux->fragMoof.data);
    free(pMux);
}
