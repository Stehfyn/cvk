/* mp4_demux.c - MP4 sample-table reader for the GPU transcoder. Walks moov/trak/mdia/minf/stbl,
** reads avcC (SPS/PPS), stsd, stsz, stsc, stco/co64, stts, ctts and stss, and builds a flat sample
** table with file offsets. AVC only; unknown boxes are skipped. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mp4_demux.h"
#include "utl/utl_mp4.h"

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define MAX_TABLE_BYTES    (128u * 1024u * 1024u)
#define MAX_PARAM_SET      (4096u)

typedef struct Box
{
    uint64_t payload;
    uint64_t end;
    uint32_t type;
} Box;

struct Mp4Demux
{
    HANDLE hFile;
    unsigned      width, height;
    unsigned      timescale;
    int           nalLengthSize;
    uint8_t       sps[MAX_PARAM_SET];
    uint32_t      spsSize;
    uint8_t       pps[MAX_PARAM_SET];
    uint32_t      ppsSize;
    Mp4Sample*    pSamples;
    unsigned      sampleCount;
    Mp4AudioCodec audioCodec;
    unsigned      audioSampleRate;
    unsigned      audioChannels;
    unsigned      audioTimescale;
    Mp4Sample*    pAudioSamples;
    unsigned      audioSampleCount;
    uint8_t       audioConfig[64]; /* AudioSpecificConfig (esds DecoderSpecificInfo) */
    uint32_t      audioConfigSize;
};

/*************************************************************************************************/
/**
 * \par Details:
 *  Combines four big-endian bytes into an unsigned 32-bit value.
 */
static uint32_t Be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Combines two big-endian bytes into an unsigned 16-bit value.
 */
static uint16_t Be16(const uint8_t* p)
{
    return (uint16_t)(((uint32_t)p[0] << 8) | (uint32_t)p[1]);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Combines two big-endian 32-bit words into an unsigned 64-bit value.
 */
static uint64_t Be64(const uint8_t* p)
{
    return ((uint64_t)Be32(p) << 32) | Be32(p + 4);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Positions the file at an absolute offset and requires the exact requested
 *  byte count.
 */
static int ReadAt(HANDLE hFile, uint64_t offset, void* pOut, DWORD size)
{
    LARGE_INTEGER position;
    DWORD         read = 0;

    position.QuadPart = (LONGLONG)offset;
    return SetFilePointerEx(hFile, position, NULL, FILE_BEGIN) && ReadFile(hFile, pOut, size, &read, NULL) &&
           read == size;
}

/* Reads the box header at offset within parentEnd. Returns 0 at the end or on a bad size. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Reads a bounded header prefix and delegates size validation to utl_mp4 before
 *  publishing absolute payload and end offsets.
 */
static int ReadBox(HANDLE hFile, uint64_t offset, uint64_t parentEnd, Box* pBox)
{
    Utl_Mp4_BoxHeader_t header;
    bool_t              offset_in_parent;
    bool_t              is_valid;
    uint64_t            remaining;
    bool_t              has_header;
    DWORD               read_bytes;
    byte_t              bytes[UTL_MP4_MAX_BOX_HEADER_BYTES];

    header.type            = 0;
    header.headerByteCount = 0;
    header.byteCount       = 0;
    offset_in_parent       = (offset <= parentEnd);
    is_valid               = offset_in_parent;
    if (is_valid)
    {
        remaining  = parentEnd - offset;
        has_header = (UTL_MP4_BOX_HEADER_BYTES <= remaining);
        is_valid   = has_header;
        if (has_header)
        {
            read_bytes = UTL_MP4_MAX_BOX_HEADER_BYTES;
            if (remaining < read_bytes)
            {
                read_bytes = (DWORD)remaining;
            }
            is_valid = ReadAt(hFile, offset, bytes, read_bytes);
            if (is_valid)
            {
                is_valid = Utl_Mp4_ReadBoxHeader(&header, bytes, read_bytes, remaining);
            }
        }
    }
    if (is_valid)
    {
        pBox->type    = header.type;
        pBox->payload = offset + header.headerByteCount;
        pBox->end     = offset + header.byteCount;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Advances through immediate children until a box of the requested type is found.
 */
static int FindChild(HANDLE hFile, const Box* pParent, uint32_t type, Box* pChild)
{
    uint64_t offset = pParent->payload;

    for (;;)
    {
        if (!ReadBox(hFile, offset, pParent->end, pChild))
        {
            break;
        }
        if (pChild->type == type)
        {
            return 1;
        }
        offset = pChild->end;
    }
    return 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Allocates a table payload within MAX_TABLE_BYTES and frees it after a failed
 *  exact read.
 */
static uint8_t* ReadPayload(HANDLE hFile, const Box* pBox, size_t* pSize)
{
    const uint64_t size = pBox->end - pBox->payload;
    uint8_t* pData;

    if (size < 4 || size > MAX_TABLE_BYTES)
    {
        return NULL;
    }
    pData = (uint8_t*)malloc((size_t)size);
    if (NULL != pData && !ReadAt(hFile, pBox->payload, pData, (DWORD)size))
    {
        free(pData);
        return NULL;
    }
    *pSize = (size_t)size;
    return pData;
}

/* avcC: configurationVersion(1) profile(1) compat(1) level(1) [6 bits reserved | 2 bits lenSize-1]
** [3 bits reserved | 5 bits numSPS] then SPS entries, then numPPS and PPS entries. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Reads the NAL length width and parameter-set arrays, retaining the first SPS
 *  and PPS that fit the fixed parameter buffers.
 */
static int ParseAvcC(Mp4Demux* pDemux, const uint8_t* pData, size_t size)
{
    unsigned count;
    size_t   position;
    unsigned i;

    if (size < 7)
    {
        return 0;
    }
    pDemux->nalLengthSize = (pData[4] & 0x03) + 1;
    count                 = pData[5] & 0x1F;
    position              = 6;
    for (i = 0; i < count; ++i)
    {
        uint16_t length;

        if (position + 2 > size)
        {
            return 0;
        }
        length    = Be16(pData + position);
        position += 2;
        if (position + length > size)
        {
            return 0;
        }
        if (0 == pDemux->spsSize && length <= MAX_PARAM_SET)
        {
            memcpy(pDemux->sps, pData + position, length);
            pDemux->spsSize = length;
        }
        position += length;
    }
    if (position >= size)
    {
        return 0;
    }
    count = pData[position++];
    for (i = 0; i < count; ++i)
    {
        uint16_t length;

        if (position + 2 > size)
        {
            return 0;
        }
        length    = Be16(pData + position);
        position += 2;
        if (position + length > size)
        {
            return 0;
        }
        if (0 == pDemux->ppsSize && length <= MAX_PARAM_SET)
        {
            memcpy(pDemux->pps, pData + position, length);
            pDemux->ppsSize = length;
        }
        position += length;
    }
    return pDemux->spsSize > 0 && pDemux->ppsSize > 0;
}

/* stsd -> avc1/avc3 sample entry -> avcC. The visual sample entry is 78 bytes before child boxes. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Selects an AVC visual sample entry, reads its dimensions and searches beyond
 *  its fixed header for the avcC child.
 */
static int ParseVideoSampleDescription(Mp4Demux* pDemux, HANDLE hFile, const Box* pStbl)
{
    Box      stsd, entry, avcC;
    uint8_t  header[16];
    uint64_t entryStart;
    uint8_t  dims[16];
    size_t   avcCSize;
    uint8_t* pAvcC;
    int      ok;

    if (!FindChild(hFile, pStbl, FOURCC('s', 't', 's', 'd'), &stsd))
    {
        return 0;
    }
    /* stsd: version+flags(4) entryCount(4), then the first sample entry box. */
    entryStart = stsd.payload + 8;
    if (!ReadBox(hFile, entryStart, stsd.end, &entry))
    {
        return 0;
    }
    if (entry.type != FOURCC('a', 'v', 'c', '1') && entry.type != FOURCC('a', 'v', 'c', '3'))
    {
        return 0; /* not H.264 */
    }
    /* VisualSampleEntry: 6 reserved + 2 dataRefIndex + 16 predefined/reserved + 2 width + 2 height. */
    if (!ReadAt(hFile, entry.payload + 24, dims, 4))
    {
        return 0;
    }
    pDemux->width  = Be16(dims);
    pDemux->height = Be16(dims + 2);
    (void)header;
    /* The VisualSampleEntry has 78 fixed bytes after the box header (which ReadBox already
    ** consumed), then its child boxes; avcC is among them. */
    {
        uint64_t offset = entry.payload + 78;
        Box scan;

        ok = 0;
        for (;;)
        {
            if (!ReadBox(hFile, offset, entry.end, &scan))
            {
                break;
            }
            if (scan.type == FOURCC('a', 'v', 'c', 'C'))
            {
                avcC = scan;
                ok   = 1;
                break;
            }
            offset = scan.end;
        }
        if (!ok)
        {
            return 0;
        }
    }
    pAvcC = ReadPayload(hFile, &avcC, &avcCSize);
    if (NULL == pAvcC)
    {
        return 0;
    }
    ok = ParseAvcC(pDemux, pAvcC, avcCSize);
    free(pAvcC);
    return ok;
}

/* Builds a flat sample table (offsets, sizes, times, keyframes) from stsz, stsc, stco/co64, stts and
** stss into a freshly allocated array returned through ppSamples/pOutCount. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Expands chunk/sample tables into offsets, sizes, decode times, composition
 *  offsets and sync flags. Releases temporary table payloads on every exit.
 */
static int BuildSampleTable(HANDLE hFile, const Box* pStbl, Mp4Sample** ppSamples, uint32_t* pOutCount)
{
    Box        stsz, stsc, stco, stts, stss, ctts;
    Mp4Sample* samples  = NULL;
    uint8_t*   pStsz    = NULL;
    uint8_t*   pStsc    = NULL;
    uint8_t*   pStco    = NULL;
    uint8_t*   pStts    = NULL;
    uint8_t*   pStss    = NULL;
    uint8_t*   pCtts    = NULL;
    size_t     stszSize = 0, stscSize = 0, stcoSize = 0, sttsSize = 0, stssSize = 0, cttsSize = 0;
    int        is64 = 0;
    uint32_t sampleSizeFixed;
    uint32_t sampleCount;
    uint32_t chunkCount;
    uint32_t stscCount;
    uint32_t i;
    int        ok = 0;

    if (!FindChild(hFile, pStbl, FOURCC('s', 't', 's', 'z'), &stsz) ||
        !FindChild(hFile, pStbl, FOURCC('s', 't', 's', 'c'), &stsc) ||
        !FindChild(hFile, pStbl, FOURCC('s', 't', 't', 's'), &stts))
    {
        return 0;
    }
    if (!FindChild(hFile, pStbl, FOURCC('s', 't', 'c', 'o'), &stco))
    {
        if (!FindChild(hFile, pStbl, FOURCC('c', 'o', '6', '4'), &stco))
        {
            return 0;
        }
        is64 = 1;
    }
    pStsz = ReadPayload(hFile, &stsz, &stszSize);
    pStsc = ReadPayload(hFile, &stsc, &stscSize);
    pStco = ReadPayload(hFile, &stco, &stcoSize);
    pStts = ReadPayload(hFile, &stts, &sttsSize);
    if (FindChild(hFile, pStbl, FOURCC('s', 't', 's', 's'), &stss))
    {
        pStss = ReadPayload(hFile, &stss, &stssSize);
    }
    if (FindChild(hFile, pStbl, FOURCC('c', 't', 't', 's'), &ctts))
    {
        pCtts = ReadPayload(hFile, &ctts, &cttsSize);
    }
    if (NULL == pStsz || NULL == pStsc || NULL == pStco || NULL == pStts || stszSize < 12 || stscSize < 8 ||
        stcoSize < 8 || sttsSize < 8 || (NULL != pCtts && cttsSize < 8) || (NULL != pStss && stssSize < 8))
    {
        goto cleanup;
    }
    sampleSizeFixed = Be32(pStsz + 4);
    sampleCount     = Be32(pStsz + 8);
    chunkCount      = Be32(pStco + 4);
    stscCount       = Be32(pStsc + 4);
    if (0 == sampleCount || sampleCount > MAX_TABLE_BYTES / sizeof(Mp4Sample) ||
        (uint64_t)stscCount * 12 + 8 > stscSize || (uint64_t)chunkCount * (is64 ? 8u : 4u) + 8 > stcoSize)
    {
        goto cleanup;
    }
    if (0 == sampleSizeFixed && (uint64_t)sampleCount * 4 + 12 > stszSize)
    {
        goto cleanup;
    }
    samples = (Mp4Sample*)calloc(sampleCount, sizeof(Mp4Sample));
    if (NULL == samples)
    {
        goto cleanup;
    }

    /* Offsets: expand stsc runs, place each sample at chunkOffset + sum of prior sample sizes. */
    {
        uint32_t sample = 0;
        uint32_t entry;

        for (entry = 0; entry < stscCount && sample < sampleCount; ++entry)
        {
            const uint32_t firstChunk      = Be32(pStsc + 8 + entry * 12);
            const uint32_t samplesPerChunk = Be32(pStsc + 8 + entry * 12 + 4);
            const uint32_t nextFirstChunk =
                (entry + 1 < stscCount) ? Be32(pStsc + 8 + (entry + 1) * 12) : chunkCount + 1;
            uint32_t chunk;

            if (firstChunk == 0 || samplesPerChunk == 0)
            {
                goto cleanup;
            }
            for (chunk = firstChunk; chunk < nextFirstChunk && chunk <= chunkCount && sample < sampleCount; ++chunk)
            {
                uint64_t offset = is64 ? Be64(pStco + 8 + (chunk - 1) * 8) : Be32(pStco + 8 + (chunk - 1) * 4);
                uint32_t inChunk;

                for (inChunk = 0; inChunk < samplesPerChunk && sample < sampleCount; ++inChunk)
                {
                    const uint32_t sz = sampleSizeFixed ? sampleSizeFixed : Be32(pStsz + 12 + sample * 4);

                    samples[sample].offset  = offset;
                    samples[sample].size    = sz;
                    offset                 += sz;
                    ++sample;
                }
            }
        }
        if (sample != sampleCount)
        {
            goto cleanup; /* table inconsistent */
        }
    }

    /* Decode times from stts run-length entries. */
    {
        const uint32_t entries = Be32(pStts + 4);
        uint32_t entry;
        uint32_t       sample = 0;
        int64_t        time   = 0;

        if ((uint64_t)entries * 8 + 8 > sttsSize)
        {
            goto cleanup;
        }
        for (entry = 0; entry < entries && sample < sampleCount; ++entry)
        {
            uint32_t       runCount = Be32(pStts + 8 + entry * 8);
            const uint32_t delta    = Be32(pStts + 8 + entry * 8 + 4);

            for (;;)
            {
                if ((0 == runCount) || (sample == sampleCount))
                {
                    break;
                }
                --runCount;
                samples[sample].decodeTime       = time;
                samples[sample].compositionTime  = time;
                samples[sample].duration         = delta;
                time                            += delta;
                ++sample;
            }
        }
        if (sample != sampleCount)
        {
            goto cleanup;
        }
    }

    /* Composition offsets (ctts), if present. */
    if (NULL != pCtts)
    {
        const uint32_t entries = Be32(pCtts + 4);
        uint32_t entry;
        uint32_t       sample = 0;

        if ((uint64_t)entries * 8 + 8 <= cttsSize)
        {
            for (entry = 0; entry < entries && sample < sampleCount; ++entry)
            {
                uint32_t      runCount = Be32(pCtts + 8 + entry * 8);
                const int32_t offset   = (int32_t)Be32(pCtts + 8 + entry * 8 + 4);

                for (;;)
                {
                    if ((0 == runCount) || (sample == sampleCount))
                    {
                        break;
                    }
                    --runCount;
                    samples[sample].compositionTime = samples[sample].decodeTime + offset;
                    ++sample;
                }
            }
        }
    }

    /* Keyframes: stss lists sync sample numbers (1-based). No stss => every sample is a keyframe. */
    if (NULL != pStss)
    {
        const uint32_t entries = Be32(pStss + 4);
        uint32_t i2;

        if ((uint64_t)entries * 4 + 8 <= stssSize)
        {
            for (i2 = 0; i2 < entries; ++i2)
            {
                const uint32_t number = Be32(pStss + 8 + i2 * 4);

                if (number >= 1 && number <= sampleCount)
                {
                    samples[number - 1].keyframe = 1;
                }
            }
        }
    }
    else
    {
        for (i = 0; i < sampleCount; ++i)
        {
            samples[i].keyframe = 1;
        }
    }
    *ppSamples = samples;
    *pOutCount = sampleCount;
    samples    = NULL;
    ok         = 1;

cleanup:
    free(samples);
    free(pStsz);
    free(pStsc);
    free(pStco);
    free(pStts);
    free(pStss);
    free(pCtts);
    return ok;
}

/* mdhd gives the track timescale (needed to map times to seconds). */
/*************************************************************************************************/
/**
 * \par Details:
 *  Reads the mdhd timescale at the version-zero or version-one offset.
 */
static unsigned ReadTimescale(HANDLE hFile, const Box* pMdia)
{
    Box     mdhd;
    uint8_t buffer[32];

    if (!FindChild(hFile, pMdia, FOURCC('m', 'd', 'h', 'd'), &mdhd) || !ReadAt(hFile, mdhd.payload, buffer, 32))
    {
        return 0;
    }
    return (1 == buffer[0]) ? Be32(buffer + 20) : Be32(buffer + 12);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Compares the hdlr handler type after locating and reading the child payload.
 */
static int TrackIsHandler(HANDLE hFile, const Box* pMdia, uint32_t handler)
{
    Box     hdlr;
    uint8_t buffer[12];

    return FindChild(hFile, pMdia, FOURCC('h', 'd', 'l', 'r'), &hdlr) && ReadAt(hFile, hdlr.payload, buffer, 12) &&
           Be32(buffer + 8) == handler;
}

/* Reads one MPEG-4 descriptor expandable length (1-4 bytes, 7 bits each, high bit continues). */
/*************************************************************************************************/
/**
 * \par Details:
 *  Accumulates up to four seven-bit descriptor-length groups while advancing
 *  the supplied cursor.
 */
static uint32_t ReadDescriptorLength(const uint8_t* p, size_t size, size_t* pPosition)
{
    uint32_t length;
    int      i;

    length = 0;
    for (i = 0; i < 4 && *pPosition < size; ++i)
    {
        uint8_t b;

        b      = p[(*pPosition)++];
        length = (length << 7) | (uint32_t)(b & 0x7F);
        if (0 == (b & 0x80))
        {
            break;
        }
    }
    return length;
}

/* esds: version+flags(4), ES_Descriptor(0x03){ES_ID(2) flags(1) DecoderConfigDescriptor(0x04){
** objectType(1) streamType+buffer(4) maxBitrate(4) avgBitrate(4) DecoderSpecificInfo(0x05){ASC}}}.
** Extracts the AudioSpecificConfig bytes. Returns 0 if the layout is not the common (flags 0) one. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Extracts DecoderSpecificInfo using the common ES_Descriptor layout with
 *  stream-dependence, URL and OCR fields assumed absent.
 */
static int ParseEsds(Mp4Demux* pDemux, const uint8_t* p, size_t size)
{
    size_t   position;
    uint32_t length;

    position = 4;
    if (position >= size || p[position++] != 0x03)
    {
        return 0;
    }
    (void)ReadDescriptorLength(p, size, &position);
    position += 3; /* ES_ID(2) + flags(1); assume the stream-dependence/URL/OCR flags are clear */
    if (position >= size || p[position++] != 0x04)
    {
        return 0;
    }
    (void)ReadDescriptorLength(p, size, &position);
    position += 13; /* objectTypeIndication(1) + streamType/bufferSize(4) + maxBitrate(4) + avgBitrate(4) */
    if (position >= size || p[position++] != 0x05)
    {
        return 0;
    }
    length = ReadDescriptorLength(p, size, &position);
    if (0 == length || length > sizeof(pDemux->audioConfig) || position + length > size)
    {
        return 0;
    }
    memcpy(pDemux->audioConfig, p + position, length);
    pDemux->audioConfigSize = length;
    return 1;
}

/* AudioSpecificConfig for AAC-LC synthesised from the sample rate and channel count, used when the
** esds cannot be parsed. objectType 2 (AAC-LC), then the sampling-frequency index and channels. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Builds the two-byte AAC-LC configuration from the sample-rate table index
 *  and channel configuration used by the existing fallback.
 */
static void SynthesizeAudioConfig(Mp4Demux* pDemux)
{
    static const unsigned rates[] =
        {96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
    unsigned index;
    unsigned i;

    index = 15;
    for (i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i)
    {
        if (rates[i] == pDemux->audioSampleRate)
        {
            index = i;
            break;
        }
    }
    pDemux->audioConfig[0]  = (uint8_t)((2u << 3) | (index >> 1));
    pDemux->audioConfig[1]  = (uint8_t)(((index & 1u) << 7) | ((pDemux->audioChannels & 0x0Fu) << 3));
    pDemux->audioConfigSize = 2;
}

/* Locates the AAC audio track: rate, channels, timescale, AudioSpecificConfig and sample table. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Selects AAC metadata, reads or synthesizes its AudioSpecificConfig and builds
 *  the audio sample table.
 */
static void ParseAudioTrack(Mp4Demux* pDemux, HANDLE hFile, const Box* pMdia)
{
    Box      minf, stbl, stsd, entry, esds;
    uint8_t  record[28];
    uint64_t offset;
    Box      scan;

    pDemux->audioCodec = MP4_AUDIO_OTHER;
    if (!FindChild(hFile, pMdia, FOURCC('m', 'i', 'n', 'f'), &minf) ||
        !FindChild(hFile, &minf, FOURCC('s', 't', 'b', 'l'), &stbl) ||
        !FindChild(hFile, &stbl, FOURCC('s', 't', 's', 'd'), &stsd))
    {
        return;
    }
    if (!ReadBox(hFile, stsd.payload + 8, stsd.end, &entry))
    {
        return;
    }
    if (entry.type != FOURCC('m', 'p', '4', 'a'))
    {
        return; /* only AAC is passed through */
    }
    pDemux->audioCodec = MP4_AUDIO_AAC;
    /* AudioSampleEntry: 6 reserved + 2 dataRef + 8 reserved + 2 channels + 2 sampleSize + 4 predefined + 2 rate.hi */
    if (ReadAt(hFile, entry.payload, record, 28))
    {
        pDemux->audioChannels   = Be16(record + 16);
        pDemux->audioSampleRate = Be16(record + 24); /* 16.16 fixed; high word is the integer rate */
    }
    pDemux->audioTimescale = ReadTimescale(hFile, pMdia);

    /* esds is a child of the mp4a entry, after the 28-byte AudioSampleEntry. */
    offset = entry.payload + 28;
    for (;;)
    {
        if (!ReadBox(hFile, offset, entry.end, &scan))
        {
            break;
        }
        if (scan.type == FOURCC('e', 's', 'd', 's'))
        {
            size_t   esdsSize;
            uint8_t* pEsds;

            esds  = scan;
            pEsds = ReadPayload(hFile, &esds, &esdsSize);
            if (NULL != pEsds)
            {
                (void)ParseEsds(pDemux, pEsds, esdsSize);
                free(pEsds);
            }
            break;
        }
        offset = scan.end;
    }
    if (0 == pDemux->audioConfigSize && pDemux->audioSampleRate > 0 && pDemux->audioChannels > 0)
    {
        SynthesizeAudioConfig(pDemux);
    }
    (void)BuildSampleTable(hFile, &stbl, &pDemux->pAudioSamples, &pDemux->audioSampleCount);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Allocates the demux owner and reads movie tracks, requiring a usable video
 *  track and timescale before returning the open source.
 */
Mp4Demux* Mp4Demux_Open(const wchar_t* pFile)
{
    Mp4Demux*     pDemux = (Mp4Demux*)calloc(1, sizeof(*pDemux));
    LARGE_INTEGER size;
    Box           root, moov, trak;
    uint64_t offset;
    int           haveVideo = 0;

    if (NULL == pDemux)
    {
        return NULL;
    }
    pDemux->hFile =
        CreateFileW(pFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
    if (INVALID_HANDLE_VALUE == pDemux->hFile || !GetFileSizeEx(pDemux->hFile, &size))
    {
        Mp4Demux_Close(pDemux);
        return NULL;
    }
    root.payload = 0;
    root.end     = (uint64_t)size.QuadPart;
    root.type    = 0;
    if (!FindChild(pDemux->hFile, &root, FOURCC('m', 'o', 'o', 'v'), &moov))
    {
        Mp4Demux_Close(pDemux);
        return NULL;
    }
    offset = moov.payload;
    for (;;)
    {
        if (!ReadBox(pDemux->hFile, offset, moov.end, &trak))
        {
            break;
        }
        if (trak.type == FOURCC('t', 'r', 'a', 'k'))
        {
            Box mdia, minf, stbl;

            if (FindChild(pDemux->hFile, &trak, FOURCC('m', 'd', 'i', 'a'), &mdia))
            {
                if (!haveVideo && TrackIsHandler(pDemux->hFile, &mdia, FOURCC('v', 'i', 'd', 'e')) &&
                    FindChild(pDemux->hFile, &mdia, FOURCC('m', 'i', 'n', 'f'), &minf) &&
                    FindChild(pDemux->hFile, &minf, FOURCC('s', 't', 'b', 'l'), &stbl))
                {
                    pDemux->timescale = ReadTimescale(pDemux->hFile, &mdia);
                    if (ParseVideoSampleDescription(pDemux, pDemux->hFile, &stbl) &&
                        BuildSampleTable(pDemux->hFile, &stbl, &pDemux->pSamples, &pDemux->sampleCount))
                    {
                        haveVideo = 1;
                    }
                }
                else if (MP4_AUDIO_NONE == pDemux->audioCodec &&
                         TrackIsHandler(pDemux->hFile, &mdia, FOURCC('s', 'o', 'u', 'n')))
                {
                    ParseAudioTrack(pDemux, pDemux->hFile, &mdia);
                }
            }
        }
        offset = trak.end;
    }
    if (!haveVideo || 0 == pDemux->timescale)
    {
        Mp4Demux_Close(pDemux);
        return NULL;
    }
    return pDemux;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
unsigned Mp4Demux_GetWidth(const Mp4Demux* pDemux)
{
    return pDemux->width;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
unsigned Mp4Demux_GetHeight(const Mp4Demux* pDemux)
{
    return pDemux->height;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
unsigned Mp4Demux_GetTimescale(const Mp4Demux* pDemux)
{
    return pDemux->timescale;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
int Mp4Demux_GetNalLengthSize(const Mp4Demux* pDemux)
{
    return pDemux->nalLengthSize;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
const uint8_t* Mp4Demux_GetSps(const Mp4Demux* pDemux, uint32_t* pSize)
{
    *pSize = pDemux->spsSize;
    return pDemux->sps;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
const uint8_t* Mp4Demux_GetPps(const Mp4Demux* pDemux, uint32_t* pSize)
{
    *pSize = pDemux->ppsSize;
    return pDemux->pps;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
unsigned Mp4Demux_GetSampleCount(const Mp4Demux* pDemux)
{
    return pDemux->sampleCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
const Mp4Sample* Mp4Demux_GetSample(const Mp4Demux* pDemux, unsigned index)
{
    return (index < pDemux->sampleCount) ? &pDemux->pSamples[index] : NULL;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
Mp4AudioCodec Mp4Demux_GetAudioCodec(const Mp4Demux* pDemux)
{
    return pDemux->audioCodec;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
unsigned Mp4Demux_GetAudioSampleRate(const Mp4Demux* pDemux)
{
    return pDemux->audioSampleRate;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
unsigned Mp4Demux_GetAudioChannels(const Mp4Demux* pDemux)
{
    return pDemux->audioChannels;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
unsigned Mp4Demux_GetAudioTimescale(const Mp4Demux* pDemux)
{
    return pDemux->audioTimescale;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
const uint8_t* Mp4Demux_GetAudioConfig(const Mp4Demux* pDemux, uint32_t* pSize)
{
    *pSize = pDemux->audioConfigSize;
    return pDemux->audioConfig;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
unsigned Mp4Demux_GetAudioSampleCount(const Mp4Demux* pDemux)
{
    return pDemux->audioSampleCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
const Mp4Sample* Mp4Demux_GetAudioSample(const Mp4Demux* pDemux, unsigned index)
{
    return (index < pDemux->audioSampleCount) ? &pDemux->pAudioSamples[index] : NULL;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Checks the selected sample and caller capacity before reading its file span.
 */
uint32_t Mp4Demux_ReadSample(Mp4Demux* pDemux, unsigned index, uint8_t* pBuffer, uint32_t capacity)
{
    const Mp4Sample* pSample = Mp4Demux_GetSample(pDemux, index);

    if (NULL == pSample || pSample->size > capacity)
    {
        return 0;
    }
    return ReadAt(pDemux->hFile, pSample->offset, pBuffer, pSample->size) ? pSample->size : 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Checks the selected audio sample and caller capacity before reading its span.
 */
uint32_t Mp4Demux_ReadAudioSample(Mp4Demux* pDemux, unsigned index, uint8_t* pBuffer, uint32_t capacity)
{
    const Mp4Sample* pSample = Mp4Demux_GetAudioSample(pDemux, index);

    if (NULL == pSample || pSample->size > capacity)
    {
        return 0;
    }
    return ReadAt(pDemux->hFile, pSample->offset, pBuffer, pSample->size) ? pSample->size : 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Locates the requested timed-text track, applies the supported edit-list timing
 *  and dispatches nonempty sample intervals while temporary sample storage is valid.
 */
int Mp4Demux_ReadTextTrack(const wchar_t* pFile, unsigned index, char language[4], Mp4TextProc callback, void* pUser)
{
    HANDLE file = CreateFileW(pFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
    LARGE_INTEGER size;
    Box           root, moov, trak, mvhd;
    uint64_t offset;
    Mp4Sample*    samples = NULL;
    uint32_t      count   = 0, i;
    uint8_t*      data    = NULL;
    int           ok      = 0;
    if (INVALID_HANDLE_VALUE == file)
        return 0;
    if (!GetFileSizeEx(file, &size))
        goto done;
    root.payload = 0;
    root.end     = (uint64_t)size.QuadPart;
    if (!FindChild(file, &root, FOURCC('m', 'o', 'o', 'v'), &moov))
        goto done;
    for (offset = moov.payload; ReadBox(file, offset, moov.end, &trak); offset = trak.end)
    {
        Box      mdia, minf, stbl, stsd, entry, mdhd, edts, elst;
        uint8_t  header[36];
        unsigned timescale;
        double   shift = 0, limit = -1;
        if (trak.type != FOURCC('t', 'r', 'a', 'k') || !FindChild(file, &trak, FOURCC('m', 'd', 'i', 'a'), &mdia) ||
            !FindChild(file, &mdia, FOURCC('m', 'i', 'n', 'f'), &minf) ||
            !FindChild(file, &minf, FOURCC('s', 't', 'b', 'l'), &stbl) ||
            !FindChild(file, &stbl, FOURCC('s', 't', 's', 'd'), &stsd) ||
            !ReadBox(file, stsd.payload + 8, stsd.end, &entry) || entry.type != FOURCC('t', 'x', '3', 'g'))
            continue;
        if (index-- > 0)
            continue;
        if (!FindChild(file, &mdia, FOURCC('m', 'd', 'h', 'd'), &mdhd) || mdhd.end - mdhd.payload < 24 ||
            !ReadAt(file, mdhd.payload, header, 24))
            goto done;
        if (header[0] == 1 && (mdhd.end - mdhd.payload < 36 || !ReadAt(file, mdhd.payload, header, 36)))
            goto done;
        timescale = Be32(header + (header[0] == 1 ? 20 : 12));
        if (0 == timescale)
            goto done;
        {
            unsigned lang = Be16(header + (header[0] == 1 ? 32 : 20));
            for (i = 0; i < 3; ++i)
                language[i] = (char)(((lang >> (10 - 5 * i)) & 31) + 0x60);
            language[3] = 0;
            if (language[0] < 'a' || language[0] > 'z' || language[1] < 'a' || language[1] > 'z' || language[2] < 'a' ||
                language[2] > 'z')
                memcpy(language, "und", 4);
        }
        if (NULL == callback)
        {
            ok = 1;
            goto done;
        }
        /* Common edit list: optional empty lead-in followed by one rate-1 media edit. */
        if (FindChild(file, &trak, FOURCC('e', 'd', 't', 's'), &edts) &&
            FindChild(file, &edts, FOURCC('e', 'l', 's', 't'), &elst))
        {
            size_t   editSize   = 0;
            uint8_t* edits      = ReadPayload(file, &elst, &editSize);
            unsigned movieScale = 0, n, stride;
            double   lead       = 0;
            int      haveMedia  = 0;
            if (FindChild(file, &moov, FOURCC('m', 'v', 'h', 'd'), &mvhd) && ReadAt(file, mvhd.payload, header, 24))
                movieScale = Be32(header + (header[0] == 1 ? 20 : 12));
            if (!edits || editSize < 8 || !movieScale)
            {
                free(edits);
                goto done;
            }
            n      = Be32(edits + 4);
            stride = edits[0] == 1 ? 20 : 12;
            if (n > 2 || (uint64_t)n * stride + 8 > editSize)
            {
                free(edits);
                goto done;
            }
            for (i = 0; i < n; ++i)
            {
                const uint8_t* e         = edits + 8 + i * stride;
                uint64_t       duration  = stride == 20 ? Be64(e) : Be32(e);
                int64_t        mediaTime = stride == 20 ? (int64_t)Be64(e + 8) : (int32_t)Be32(e + 4);
                if (Be32(e + stride - 4) != 0x00010000 || haveMedia || mediaTime < -1)
                {
                    free(edits);
                    goto done;
                }
                if (mediaTime == -1)
                    lead += (double)duration / movieScale;
                else
                {
                    shift     = lead - (double)mediaTime / timescale;
                    limit     = lead + (double)duration / movieScale;
                    haveMedia = 1;
                }
            }
            free(edits);
        }
        if (!BuildSampleTable(file, &stbl, &samples, &count))
            goto done;
        data = (uint8_t*)malloc(1024 * 1024);
        if (!data)
            goto done;
        for (i = 0; i < count; ++i)
        {
            double start = (double)samples[i].compositionTime / timescale + shift;
            double end   = start + (double)samples[i].duration / timescale;
            if (samples[i].size > 1024 * 1024 || samples[i].offset > root.end ||
                samples[i].size > root.end - samples[i].offset ||
                !ReadAt(file, samples[i].offset, data, samples[i].size))
                goto done;
            if (limit >= 0 && end > limit)
                end = limit;
            if (start < 0)
                start = 0;
            if (end > start && !callback(pUser, start, end, data, samples[i].size))
                goto done;
        }
        ok = 1;
        break;
    }
done:
    free(samples);
    free(data);
    CloseHandle(file);
    return ok;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Closes the source file and frees video/audio tables and the demux owner.
 *  NULL is accepted.
 */
void Mp4Demux_Close(Mp4Demux* pDemux)
{
    if (NULL == pDemux)
    {
        return;
    }
    if (INVALID_HANDLE_VALUE != pDemux->hFile && NULL != pDemux->hFile)
    {
        CloseHandle(pDemux->hFile);
    }
    free(pDemux->pSamples);
    free(pDemux->pAudioSamples);
    free(pDemux);
}
