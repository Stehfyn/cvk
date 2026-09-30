/*=================================================================================================
** 1. References
**===============================================================================================*/

/* mp4_demux.c - MP4 sample-table reader for the GPU transcoder. Walks moov/trak/mdia/minf/stbl,
** reads avcC (SPS/PPS), stsd, stsz, stsc, stco/co64, stts, ctts and stss, and builds a flat sample
** table with file offsets. AVC only; unknown boxes are skipped. */
/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h> // IWYU pragma: keep
#include <minwindef.h>
#include <fileapi.h>
#include <WinBase.h>
#include <handleapi.h>

#include <stdint.h>
#include <stddef.h>
#include <heapapi.h>
#include <winnt.h>

#include "mp4_demux.h"
#include "utl/utl_mpeg4.h"
#include "utl/utl_mpeg4systems.h"
#include "utl/utl_mpeg4audio.h"
#include "crt/crt_memory.h"
#include "os/os_compiler.h"
#include "os/os_assert.h"
#include "os/os_types.h"

static FORCE_INLINE void* AllocateTableBytes(size_t bytes)
{
    return HeapAlloc(GetProcessHeap(), 0, bytes);
}

static FORCE_INLINE void* AllocateZeroedTable(size_t count, size_t elementSize)
{
    size_t bytes = count * elementSize;

    if (0 != elementSize && count > SIZE_MAX / elementSize)
    {
        return NULL;
    }
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
}

static FORCE_INLINE void ReleaseTableBytes(void* address)
{
    BOOL released;

    if (NULL == address)
    {
        return;
    }
    released = HeapFree(GetProcessHeap(), 0, address);
    LOGASSERT(released);
}

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define MAX_TABLE_BYTES              (UINT64_C(128) * 1024U * 1024U)
#define ESDS_FULLBOX_BYTES           (UTL_MPEG4_FULLBOX_FIELDS_BYTES)
#define AUDIO_CONFIGURATION_CAPACITY (64)
#define MAX_PARAM_SET                (4096)
#define MP4_BITS_PER_BYTE            8
#define MP4_BITS_PER_U16             16
#define MP4_BITS_PER_U32             32
#define MP4_BYTES_PER_U32            4
#define MP4_BYTES_PER_U64            8
#define MP4_FULL_BOX_PREFIX_BYTES    8
#define MP4_STSZ_PREFIX_BYTES        12
#define MP4_STSC_ENTRY_BYTES         12
#define MP4_TIME_TABLE_ENTRY_BYTES   8
#define MP4_AVCC_MIN_BYTES           7
#define MP4_AVCC_SPS_COUNT_OFFSET    5
#define MP4_AVCC_SPS_START_OFFSET    6
#define MP4_AVCC_SPS_COUNT_MASK      0x1F
#define MP4_VISUAL_DIMENSIONS_OFFSET 24
#define MP4_VISUAL_ENTRY_BYTES       78
#define MP4_AUDIO_ENTRY_BYTES        28
#define MP4_AUDIO_CHANNELS_OFFSET    16
#define MP4_AUDIO_RATE_OFFSET        24
#define MP4_MEDIA_HEADER_READ_BYTES  32
#define MP4_MEDIA_HEADER_V0_BYTES    24
#define MP4_MEDIA_HEADER_V1_BYTES    36
#define MP4_MOVIE_HEADER_READ_BYTES  24
#define MP4_MEDIA_TIMESCALE_V0_OFFSET 12
#define MP4_MEDIA_TIMESCALE_V1_OFFSET 20
#define MP4_MEDIA_LANGUAGE_V0_OFFSET 20
#define MP4_MEDIA_LANGUAGE_V1_OFFSET 32
#define MP4_HANDLER_HEADER_BYTES     12
#define MP4_HANDLER_TYPE_OFFSET      8
#define MP4_EDIT_ENTRY_V0_BYTES      12
#define MP4_EDIT_ENTRY_V1_BYTES      20
#define MP4_EDIT_RATE_NORMAL         0x00010000
#define MP4_LANGUAGE_FIRST_SHIFT     10
#define MP4_LANGUAGE_BITS_PER_CHAR   5
#define MP4_LANGUAGE_CHAR_MASK       31
#define MP4_LANGUAGE_CHAR_BASE       0x60
#define MP4_SUBTITLE_SAMPLE_CAPACITY ((size_t)1024U * 1024U)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

typedef struct Box
{
    uint64_t payload;
    uint64_t end;
    uint64_t fileEnd;
    uint32_t type;
} Box;

struct Mp4Demux
{
    HANDLE hFile;
    unsigned      width, height;
    unsigned      timescale;
    int           nalLengthSize;
    byte_t        sps[MAX_PARAM_SET];
    uint32_t      spsSize;
    byte_t        pps[MAX_PARAM_SET];
    uint32_t      ppsSize;
    Mp4Sample*    pSamples;
    unsigned      sampleCount;
    Mp4AudioCodec audioCodec;
    unsigned      audioSampleRate;
    unsigned      audioChannels;
    unsigned      audioTimescale;
    Mp4Sample*    pAudioSamples;
    unsigned      audioSampleCount;
    byte_t        audioConfig[AUDIO_CONFIGURATION_CAPACITY]; /* AudioSpecificConfig (esds DecoderSpecificInfo) */
    uint32_t      audioConfigSize;
};

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Publishes the complete codec configuration from one bounded esds payload.
 *
 * \par Requirements:
 *  - 001 The demux owner and counted box payload shall remain valid and disjoint.
 *  - 002 FullBox version/flags, complete descriptor lengths and unique ES/decoder/configuration components shall be validated.
 *  - 003 Only completely modeled AAC configuration fitting retained storage shall publish; malformed or unsupported syntax shall not be guessed or truncated.
 *
 * \param[in,out] demux       Required demux owner
 * \param[in]     data        Required counted FullBox payload
 * \param[in]     byteCount   Readable payload bytes
 *
 * \retval TRUE Complete configuration published.
 * \retval FALSE Invalid, unsupported or oversized configuration; prior configuration retained.
 */
static FORCE_INLINE bool_t ParseEsds(Mp4Demux* demux, const byte_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Reads a bounded native prefix and publishes validated MPEG-4 box coordinates.
 *
 * \par Requirements:
 *  - 001 The caller shall supply a valid file handle and exclusively writable result.
 *  - 002 Offset and parent boundaries shall be checked against file end before native I/O.
 *  - 003 The common specification reader shall validate all size forms; output shall remain unchanged on failure.
 *  - 004 File extent shall propagate to descendants without querying the file again.
 *
 * \param[in]  hFile       Required open readable file
 * \param[in]  offset      Absolute candidate box start
 * \param[in]  parentEnd   Exclusive parent end
 * \param[in]  fileEnd     Exclusive file end
 * \param[out] pBox        Required result
 *
 * \retval TRUE Complete coordinates published.
 * \retval FALSE Invalid extent, malformed header or native read failure.
 */
static FORCE_INLINE bool_t ReadBox(HANDLE hFile, uint64_t offset, uint64_t parentEnd, uint64_t fileEnd, Box* pBox);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Combines four big-endian bytes into an unsigned 32-bit value.
 */
static FORCE_INLINE uint32_t Be32(const byte_t* p)
{
    return ((uint32_t)p[0] << (MP4_BITS_PER_U32 - MP4_BITS_PER_BYTE)) | ((uint32_t)p[1] << MP4_BITS_PER_U16) |
           ((uint32_t)p[2] << MP4_BITS_PER_BYTE) | (uint32_t)p[3];
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Combines two big-endian bytes into an unsigned 16-bit value.
 */
static FORCE_INLINE uint16_t Be16(const byte_t* p)
{
    return (uint16_t)(((uint32_t)p[0] << MP4_BITS_PER_BYTE) | (uint32_t)p[1]);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Combines two big-endian 32-bit words into an unsigned 64-bit value.
 */
static FORCE_INLINE uint64_t Be64(const byte_t* p)
{
    return ((uint64_t)Be32(p) << MP4_BITS_PER_U32) | Be32(p + MP4_BYTES_PER_U32);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Positions the file at an absolute offset and requires the exact requested
 *  byte count.
 */
static FORCE_INLINE int ReadAt(HANDLE hFile, uint64_t offset, void* pOut, DWORD size)
{
    LARGE_INTEGER position;
    DWORD         read;
    bool_t        moved;
    bool_t        readOk;
    bool_t        complete;
    bool_t        valid;
    DWORD         requested;
    byte_t        discard = 0;
    void*         destination;

    if (0 == size)
    {
        return 1;
    }
    position.QuadPart = (LONGLONG)offset;
    read              = 0;
    moved             = !!SetFilePointerEx(hFile, position, NULL, FILE_BEGIN);
    requested         = moved ? size : 0;
    destination       = moved ? pOut : &discard;
    readOk            = !!ReadFile(hFile, destination, requested, &read, NULL);
    complete = (size == read);
    valid = readOk && complete;
    return moved && valid;
}

static FORCE_INLINE uint32_t ReadSelectedSample(Mp4Demux* pDemux, const Mp4Sample* pSample, byte_t* pBuffer, uint32_t capacity)
{
    Mp4Sample       empty;
    const Mp4Sample* selected;
    uint32_t        size;
    bool_t          valid;
    bool_t          read;

    empty.offset = 0;
    empty.size   = 0;
    selected     = (NULL != pSample) ? pSample : &empty;
    size         = selected->size;
    valid        = (NULL != pSample) && (size <= capacity);
    size         = valid ? size : 0;
    read         = ReadAt(pDemux->hFile, selected->offset, pBuffer, size);
    return (valid && read) ? size : 0;
}

/* Reads the box header at offset within parentEnd. Returns 0 at the end or on a bad size. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Reads a bounded header prefix and delegates size validation to utl_mpeg4 before
 *  publishing absolute payload and end offsets.
 */
static FORCE_INLINE bool_t ReadBox(HANDLE hFile, uint64_t offset, uint64_t parentEnd, uint64_t fileEnd, Box* pBox)
{
    bool_t                offset_in_parent;
    bool_t                parent_in_file;
    bool_t                is_valid;
    uint64_t              remaining;
    bool_t                has_header;
    bool_t                read;
    DWORD                 read_bytes;
    byte_t                bytes[UTL_MPEG4_MAX_BOX_HEADER_BYTES];
    Utl_Mpeg4_Span_t      prefix;
    Utl_Mpeg4_BoxHeader_t header;
    uint32_t              header_bytes;

    LOGASSERT(NULL != pBox);
    LOGASSERT(NULL != hFile);
    LOGASSERT(INVALID_HANDLE_VALUE != hFile);
    offset_in_parent = (offset <= parentEnd);
    parent_in_file   = (parentEnd <= fileEnd);
    is_valid         = offset_in_parent && parent_in_file;
    remaining        = offset_in_parent ? parentEnd - offset : 0;
    has_header       = is_valid && (UTL_MPEG4_BOX_HEADER_BYTES <= remaining);
    read_bytes       = (DWORD)((remaining < UTL_MPEG4_MAX_BOX_HEADER_BYTES) ? remaining : UTL_MPEG4_MAX_BOX_HEADER_BYTES);
    read_bytes       = has_header ? read_bytes : 0;
    read             = ReadAt(hFile, offset, bytes, read_bytes);
    is_valid         = has_header && read;
    if (is_valid)
    {
        prefix.data      = bytes;
        prefix.byteCount = read_bytes;
        is_valid         = Utl_Mpeg4_ReadBoxHeader(&header, prefix, remaining, fileEnd - offset);
    }
    if (is_valid)
    {
        header_bytes  = Utl_Mpeg4_GetBoxHeaderByteCount(&header);
        pBox->fileEnd = fileEnd;
        pBox->type    = header.type;
        pBox->payload = offset + header_bytes;
        pBox->end     = offset + header.byteCount;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Advances through immediate children until a box of the requested type is found.
 */
static FORCE_INLINE int FindChild(HANDLE hFile, const Box* pParent, uint32_t type, Box* pChild)
{
    uint64_t offset = pParent->payload;

    for (;;)
    {
        if (!ReadBox(hFile, offset, pParent->end, pParent->fileEnd, pChild))
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
static FORCE_INLINE byte_t* ReadPayload(HANDLE hFile, const Box* pBox, size_t* pSize)
{
    const uint64_t size = pBox->end - pBox->payload;
    byte_t* pData;

    if (size < 4 || size > MAX_TABLE_BYTES)
    {
        return NULL;
    }
    pData = (byte_t*)AllocateTableBytes((size_t)size);
    if (NULL != pData && !ReadAt(hFile, pBox->payload, pData, (DWORD)size))
    {
        ReleaseTableBytes(pData);
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
static int ParseAvcC(Mp4Demux* pDemux, const byte_t* pData, size_t size)
{
    unsigned count;
    size_t   position;
    unsigned i;

    if (size < MP4_AVCC_MIN_BYTES)
    {
        return 0;
    }
    pDemux->nalLengthSize = (pData[4] & 0x03) + 1;
    count                 = pData[MP4_AVCC_SPS_COUNT_OFFSET] & MP4_AVCC_SPS_COUNT_MASK;
    position              = MP4_AVCC_SPS_START_OFFSET;
    for (i = 0; i < count; ++i)
    {
        uint16_t length;
        uint16_t remaining;
        byte_t* destination;
        const byte_t* source;

        if (position + 2 > size)
        {
            return 0;
        }
        length   = Be16(pData + position);
        position += 2;
        if (position + length > size)
        {
            return 0;
        }
        destination = pDemux->sps;
        source      = pData + position;
        if (0 == pDemux->spsSize && length <= MAX_PARAM_SET)
        {
            remaining = length;
            for (; 0 < remaining; --remaining)
            {
                *destination = *source;
                ++destination;
                ++source;
            }
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
        uint16_t remaining;
        byte_t* destination;
        const byte_t* source;

        if (position + 2 > size)
        {
            return 0;
        }
        length   = Be16(pData + position);
        position += 2;
        if (position + length > size)
        {
            return 0;
        }
        destination = pDemux->pps;
        source      = pData + position;
        if (0 == pDemux->ppsSize && length <= MAX_PARAM_SET)
        {
            remaining = length;
            for (; 0 < remaining; --remaining)
            {
                *destination = *source;
                ++destination;
                ++source;
            }
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
    Box      stsd;
    Box      entry;
    Box      avcC;
    uint64_t entryStart;
    byte_t   dims[MP4_BYTES_PER_U32];
    size_t   avcCSize;
    byte_t*  pAvcC;
    int      ok;

    if (!FindChild(hFile, pStbl, FOURCC('s', 't', 's', 'd'), &stsd))
    {
        return 0;
    }
    /* stsd: version+flags(4) entryCount(4), then the first sample entry box. */
    entryStart = stsd.payload + MP4_FULL_BOX_PREFIX_BYTES;
    if (!ReadBox(hFile, entryStart, stsd.end, stsd.fileEnd, &entry))
    {
        return 0;
    }
    if (entry.type != FOURCC('a', 'v', 'c', '1') && entry.type != FOURCC('a', 'v', 'c', '3'))
    {
        return 0; /* not H.264 */
    }
    /* VisualSampleEntry: 6 reserved + 2 dataRefIndex + 16 predefined/reserved + 2 width + 2 height. */
    if (!ReadAt(hFile, entry.payload + MP4_VISUAL_DIMENSIONS_OFFSET, dims, MP4_BYTES_PER_U32))
    {
        return 0;
    }
    pDemux->width  = Be16(dims);
    pDemux->height = Be16(dims + 2);
    /* The VisualSampleEntry has 78 fixed bytes after the box header (which ReadBox already
    ** consumed), then its child boxes; avcC is among them. */
    {
        uint64_t offset = entry.payload + MP4_VISUAL_ENTRY_BYTES;
        Box scan;

        ok = 0;
        for (;;)
        {
            if (!ReadBox(hFile, offset, entry.end, entry.fileEnd, &scan))
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
    ReleaseTableBytes(pAvcC);
    return ok;
}

/* Builds a flat sample table (offsets, sizes, times, keyframes) from stsz, stsc, stco/co64, stts and
** stss into a freshly allocated array returned through ppSamples/pOutCount. */
static int ExpandSampleOffsets(const byte_t* pStsz, const byte_t* pStsc,
                               const byte_t* pStco, uint32_t sampleSizeFixed,
                               uint32_t sampleCount, uint32_t chunkCount,
                               uint32_t stscCount, int is64, Mp4Sample* samples)
{

        uint32_t sample = 0;
        uint32_t entry;

        for (entry = 0; entry < stscCount && sample < sampleCount; ++entry)
        {
            const uint32_t firstChunk      = Be32(pStsc + MP4_FULL_BOX_PREFIX_BYTES + ((uint64_t)entry * MP4_STSC_ENTRY_BYTES));
            const uint32_t samplesPerChunk = Be32(pStsc + MP4_FULL_BOX_PREFIX_BYTES + ((uint64_t)entry * MP4_STSC_ENTRY_BYTES) + MP4_BYTES_PER_U32);
            const uint32_t nextFirstChunk =
                (entry + 1 < stscCount) ? Be32(pStsc + MP4_FULL_BOX_PREFIX_BYTES + (((uint64_t)entry + 1) * MP4_STSC_ENTRY_BYTES))
                                        : chunkCount + 1;
            uint32_t chunk;

            if (firstChunk == 0 || samplesPerChunk == 0)
            {
                return 0;
            }
            for (chunk = firstChunk; chunk < nextFirstChunk && chunk <= chunkCount && sample < sampleCount; ++chunk)
            {
                uint64_t offset = is64 ? Be64(pStco + MP4_FULL_BOX_PREFIX_BYTES + ((uint64_t)(chunk - 1) * MP4_BYTES_PER_U64))
                                       : Be32(pStco + MP4_FULL_BOX_PREFIX_BYTES + ((uint64_t)(chunk - 1) * MP4_BYTES_PER_U32));
                uint32_t inChunk;

                for (inChunk = 0; inChunk < samplesPerChunk && sample < sampleCount; ++inChunk)
                {
                    const uint32_t sz = sampleSizeFixed ? sampleSizeFixed
                                                        : Be32(pStsz + MP4_STSZ_PREFIX_BYTES + ((uint64_t)sample * MP4_BYTES_PER_U32));

                    samples[sample].offset = offset;
                    samples[sample].size   = sz;
                    offset                 += sz;
                    ++sample;
                }
            }
        }
        if (sample != sampleCount)
        {
            return 0; /* table inconsistent */
        }
        return 1;
}

static FORCE_INLINE int ExpandDecodeTimes(const byte_t* pStts, size_t sttsSize,
                             uint32_t sampleCount, Mp4Sample* samples)
{

        const uint32_t entries = Be32(pStts + 4);
        uint32_t entry;
        uint32_t       sample = 0;
        int64_t        time   = 0;

        if (((uint64_t)entries * MP4_TIME_TABLE_ENTRY_BYTES) + MP4_FULL_BOX_PREFIX_BYTES > sttsSize)
        {
            return 0;
        }
        for (entry = 0; entry < entries && sample < sampleCount; ++entry)
        {
            uint32_t       runCount = Be32(pStts + MP4_FULL_BOX_PREFIX_BYTES + ((uint64_t)entry * MP4_TIME_TABLE_ENTRY_BYTES));
            const uint32_t delta    = Be32(pStts + MP4_FULL_BOX_PREFIX_BYTES + ((uint64_t)entry * MP4_TIME_TABLE_ENTRY_BYTES) + MP4_BYTES_PER_U32);

            for (;;)
            {
                if ((0 == runCount) || (sample == sampleCount))
                {
                    break;
                }
                --runCount;
                samples[sample].decodeTime      = time;
                samples[sample].compositionTime = time;
                samples[sample].duration        = delta;
                time                            += delta;
                ++sample;
            }
        }
        if (sample != sampleCount)
        {
            return 0;
        }
        return 1;
}

static FORCE_INLINE void ExpandCompositionTimes(const byte_t* pCtts, size_t cttsSize,
                                   uint32_t sampleCount, Mp4Sample* samples)
{
    /* Composition offsets (ctts), if present. */
    if (NULL != pCtts)
    {
        const uint32_t entries = Be32(pCtts + 4);
        uint32_t entry;
        uint32_t       sample = 0;

        if (((uint64_t)entries * MP4_TIME_TABLE_ENTRY_BYTES) + MP4_FULL_BOX_PREFIX_BYTES <= cttsSize)
        {
            for (entry = 0; entry < entries && sample < sampleCount; ++entry)
            {
                uint32_t      runCount = Be32(pCtts + MP4_FULL_BOX_PREFIX_BYTES + ((uint64_t)entry * MP4_TIME_TABLE_ENTRY_BYTES));
                const int32_t offset   = (int32_t)Be32(pCtts + MP4_FULL_BOX_PREFIX_BYTES + ((uint64_t)entry * MP4_TIME_TABLE_ENTRY_BYTES) + MP4_BYTES_PER_U32);

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

}

static FORCE_INLINE void MarkSyncSamples(const byte_t* pStss, size_t stssSize,
                            uint32_t sampleCount, Mp4Sample* samples)
{
    uint32_t i;
    /* Keyframes: stss lists sync sample numbers (1-based). No stss => every sample is a keyframe. */
    if (NULL != pStss)
    {
        const uint32_t entries = Be32(pStss + 4);
        uint32_t i2;

        if (((uint64_t)entries * MP4_BYTES_PER_U32) + MP4_FULL_BOX_PREFIX_BYTES <= stssSize)
        {
            for (i2 = 0; i2 < entries; ++i2)
            {
                const uint32_t number = Be32(pStss + MP4_FULL_BOX_PREFIX_BYTES + ((uint64_t)i2 * MP4_BYTES_PER_U32));

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
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Expands chunk/sample tables into offsets, sizes, decode times, composition
 *  offsets and sync flags. Releases temporary table payloads on every exit.
 */
static int BuildSampleTable(HANDLE hFile, const Box* pStbl, Mp4Sample** ppSamples, uint32_t* pOutCount)
{
    Box        stsz;
    Box        stsc;
    Box        stco;
    Box        stts;
    Box        stss;
    Box        ctts;
    Mp4Sample* samples  = NULL;
    byte_t*   pStsz    = NULL;
    byte_t*   pStsc    = NULL;
    byte_t*   pStco    = NULL;
    byte_t*   pStts    = NULL;
    byte_t*   pStss    = NULL;
    byte_t*   pCtts    = NULL;
    size_t     stszSize = 0;
    size_t     stscSize = 0;
    size_t     stcoSize = 0;
    size_t     sttsSize = 0;
    size_t     stssSize = 0;
    size_t     cttsSize = 0;
    int        is64 = 0;
    uint32_t sampleSizeFixed;
    uint32_t sampleCount;
    uint32_t chunkCount;
    uint32_t stscCount;
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
    if (NULL == pStsz || NULL == pStsc || NULL == pStco || NULL == pStts || stszSize < MP4_STSZ_PREFIX_BYTES ||
        stscSize < MP4_FULL_BOX_PREFIX_BYTES || stcoSize < MP4_FULL_BOX_PREFIX_BYTES ||
        sttsSize < MP4_FULL_BOX_PREFIX_BYTES || (NULL != pCtts && cttsSize < MP4_FULL_BOX_PREFIX_BYTES) ||
        (NULL != pStss && stssSize < MP4_FULL_BOX_PREFIX_BYTES))
    {
        goto cleanup;
    }
    sampleSizeFixed = Be32(pStsz + 4);
    sampleCount     = Be32(pStsz + MP4_FULL_BOX_PREFIX_BYTES);
    chunkCount      = Be32(pStco + 4);
    stscCount       = Be32(pStsc + 4);
    if (0 == sampleCount || sampleCount > MAX_TABLE_BYTES / sizeof(Mp4Sample) ||
        ((uint64_t)stscCount * MP4_STSC_ENTRY_BYTES) + MP4_FULL_BOX_PREFIX_BYTES > stscSize ||
        ((uint64_t)chunkCount * (is64 ? MP4_BYTES_PER_U64 : MP4_BYTES_PER_U32)) + MP4_FULL_BOX_PREFIX_BYTES > stcoSize)
    {
        goto cleanup;
    }
    if (0 == sampleSizeFixed && ((uint64_t)sampleCount * MP4_BYTES_PER_U32) + MP4_STSZ_PREFIX_BYTES > stszSize)
    {
        goto cleanup;
    }
    samples = (Mp4Sample*)AllocateZeroedTable(sampleCount, sizeof(Mp4Sample));
    if (NULL == samples)
    {
        goto cleanup;
    }

    if (!ExpandSampleOffsets(pStsz, pStsc, pStco, sampleSizeFixed,
                             sampleCount, chunkCount, stscCount, is64, samples) ||
        !ExpandDecodeTimes(pStts, sttsSize, sampleCount, samples))
    {
        goto cleanup;
    }
    ExpandCompositionTimes(pCtts, cttsSize, sampleCount, samples);
    MarkSyncSamples(pStss, stssSize, sampleCount, samples);
    *ppSamples = samples;
    *pOutCount = sampleCount;
    samples    = NULL;
    ok         = 1;

cleanup:
    ReleaseTableBytes(samples);
    ReleaseTableBytes(pStsz);
    ReleaseTableBytes(pStsc);
    ReleaseTableBytes(pStco);
    ReleaseTableBytes(pStts);
    ReleaseTableBytes(pStss);
    ReleaseTableBytes(pCtts);
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
    Box    mdhd;
    byte_t buffer[MP4_MEDIA_HEADER_READ_BYTES];

    if (!FindChild(hFile, pMdia, FOURCC('m', 'd', 'h', 'd'), &mdhd) || !ReadAt(hFile, mdhd.payload, buffer, MP4_MEDIA_HEADER_READ_BYTES))
    {
        return 0;
    }
    return (1 == buffer[0]) ? Be32(buffer + MP4_MEDIA_TIMESCALE_V1_OFFSET) : Be32(buffer + MP4_MEDIA_TIMESCALE_V0_OFFSET);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Compares the hdlr handler type after locating and reading the child payload.
 */
static int TrackIsHandler(HANDLE hFile, const Box* pMdia, uint32_t handler)
{
    Box    hdlr;
    byte_t buffer[MP4_HANDLER_HEADER_BYTES];

    return FindChild(hFile, pMdia, FOURCC('h', 'd', 'l', 'r'), &hdlr) && ReadAt(hFile, hdlr.payload, buffer, MP4_HANDLER_HEADER_BYTES) &&
           Be32(buffer + MP4_HANDLER_TYPE_OFFSET) == handler;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every nested reader receives only its parent's payload extent. Optional ES
 *  fields and unknown component descriptors are handled by the Systems module.
 */
static FORCE_INLINE bool_t ParseEsds(Mp4Demux* demux, const byte_t* data, uint32_t byteCount)
{
    bool_t                              has_header;
    bool_t                              valid;
    Utl_Mpeg4_Span_t                    fullbox_bytes;
    Utl_Mpeg4_FullBoxFields_t           fullbox;
    bool_t                              supported_version;
    bool_t                              valid_flags;
    Utl_Mpeg4_Span_t                    bytes;
    Utl_Mpeg4Systems_Descriptor_t       descriptor = {0};
    Utl_Mpeg4Systems_ElementaryStream_t stream = {0};
    Utl_Mpeg4Systems_DecoderConfig_t    decoder = {0};
    bool_t                              audio_object;
    bool_t                              audio_stream;
    bool_t                              supported;
    bool_t                              nonempty;
    bool_t                              fits;
    Utl_Mpeg4_Span_t                    audio_bytes;
    Utl_Mpeg4Audio_Status_t             audio_status;
    Utl_Mpeg4Audio_Config_t             configuration;

    LOGASSERT(NULL != demux);
    LOGASSERT(NULL != data);
    has_header = (ESDS_FULLBOX_BYTES <= byteCount);
    valid      = FALSE;
    if (has_header)
    {
        fullbox_bytes.data      = data;
        fullbox_bytes.byteCount = byteCount;
        valid                   = Utl_Mpeg4_ReadFullBoxFields(&fullbox, fullbox_bytes);
        LOGASSERT(valid);
        supported_version = (0 == fullbox.version);
        valid_flags       = (0 == fullbox.flags);
        valid             = supported_version && valid_flags;
        if (valid)
        {
            bytes.data      = &data[ESDS_FULLBOX_BYTES];
            bytes.byteCount = byteCount - ESDS_FULLBOX_BYTES;
            valid           = Utl_Mpeg4Systems_FindDescriptor(&descriptor, bytes, UTL_MPEG4SYSTEMS_TAG_ES);
        }
    }
    if (valid)
    {
        valid = Utl_Mpeg4Systems_ReadElementaryStream(&stream, &descriptor);
    }
    if (valid)
    {
        valid = Utl_Mpeg4Systems_FindDescriptor(&descriptor, stream.children, UTL_MPEG4SYSTEMS_TAG_DECODER_CONFIG);
    }
    if (valid)
    {
        valid = Utl_Mpeg4Systems_ReadDecoderConfig(&decoder, &descriptor);
    }
    if (valid)
    {
        audio_object = (UTL_MPEG4SYSTEMS_OBJECT_AUDIO == decoder.objectType);
        audio_stream = (UTL_MPEG4SYSTEMS_STREAM_AUDIO == decoder.streamType);
        supported    = audio_object && audio_stream;
        valid        = supported;
        if (supported)
        {
            valid = Utl_Mpeg4Systems_FindDescriptor(&descriptor, decoder.children, UTL_MPEG4SYSTEMS_TAG_DECODER_INFO);
        }
    }
    if (valid)
    {
        nonempty = (0 < descriptor.payload.byteCount);
        fits     = (descriptor.payload.byteCount <= sizeof(demux->audioConfig));
        valid    = nonempty && fits;
        if (valid)
        {
            audio_bytes.data      = descriptor.payload.data;
            audio_bytes.byteCount = descriptor.payload.byteCount;
            audio_status          = Utl_Mpeg4Audio_ReadConfig(&configuration, audio_bytes);
            valid                 = (UTL_MPEG4AUDIO_COMPLETE == audio_status);
        }
        if (valid)
        {
            CRT_Memory_Copy(demux->audioConfig, descriptor.payload.data, descriptor.payload.byteCount);
            demux->audioConfigSize = descriptor.payload.byteCount;
            demux->audioCodec      = MP4_AUDIO_AAC;
        }
    }
    return valid;
}

/* Locates the AAC audio track: rate, channels, timescale, AudioSpecificConfig and sample table. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Selects AAC metadata, reads its declared AudioSpecificConfig and builds
 *  the audio sample table.
 */
static void ParseAudioTrack(Mp4Demux* pDemux, HANDLE hFile, const Box* pMdia)
{
    Box      minf;
    Box      stbl;
    Box      stsd;
    Box      entry;
    Box      esds;
    byte_t   record[MP4_AUDIO_ENTRY_BYTES];
    uint64_t offset;
    Box      scan;

    pDemux->audioCodec = MP4_AUDIO_OTHER;
    if (!FindChild(hFile, pMdia, FOURCC('m', 'i', 'n', 'f'), &minf) ||
        !FindChild(hFile, &minf, FOURCC('s', 't', 'b', 'l'), &stbl) ||
        !FindChild(hFile, &stbl, FOURCC('s', 't', 's', 'd'), &stsd))
    {
        return;
    }
    if (!ReadBox(hFile, stsd.payload + MP4_FULL_BOX_PREFIX_BYTES, stsd.end, stsd.fileEnd, &entry))
    {
        return;
    }
    if (entry.type != FOURCC('m', 'p', '4', 'a'))
    {
        return; /* only AAC is passed through */
    }
    /* AudioSampleEntry: 6 reserved + 2 dataRef + 8 reserved + 2 channels + 2 sampleSize + 4 predefined + 2 rate.hi */
    if (ReadAt(hFile, entry.payload, record, MP4_AUDIO_ENTRY_BYTES))
    {
        pDemux->audioChannels   = Be16(record + MP4_AUDIO_CHANNELS_OFFSET);
        pDemux->audioSampleRate = Be16(record + MP4_AUDIO_RATE_OFFSET); /* 16.16 fixed; high word is the integer rate */
    }
    pDemux->audioTimescale = ReadTimescale(hFile, pMdia);

    /* esds is a child of the mp4a entry, after the 28-byte AudioSampleEntry. */
    offset = entry.payload + MP4_AUDIO_ENTRY_BYTES;
    for (;;)
    {
        if (!ReadBox(hFile, offset, entry.end, entry.fileEnd, &scan))
        {
            break;
        }
        if (scan.type == FOURCC('e', 's', 'd', 's'))
        {
            size_t  esdsSize;
            byte_t* pEsds;

            esds  = scan;
            pEsds = ReadPayload(hFile, &esds, &esdsSize);
            if (NULL != pEsds)
            {
                if (esdsSize <= UINT32_MAX)
                {
                    (void)ParseEsds(pDemux, pEsds, (uint32_t)esdsSize);
                }
                ReleaseTableBytes(pEsds);
            }
            break;
        }
        offset = scan.end;
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
static void ParseMovieTrack(Mp4Demux* pDemux, const Box* pTrack, int* pHaveVideo)
{
    Box mdia;
    Box minf;
    Box stbl;

    if (pTrack->type != FOURCC('t', 'r', 'a', 'k') ||
        !FindChild(pDemux->hFile, pTrack, FOURCC('m', 'd', 'i', 'a'), &mdia))
    {
        return;
    }
    if (!*pHaveVideo && TrackIsHandler(pDemux->hFile, &mdia, FOURCC('v', 'i', 'd', 'e')) &&
        FindChild(pDemux->hFile, &mdia, FOURCC('m', 'i', 'n', 'f'), &minf) &&
        FindChild(pDemux->hFile, &minf, FOURCC('s', 't', 'b', 'l'), &stbl))
    {
        pDemux->timescale = ReadTimescale(pDemux->hFile, &mdia);
        if (ParseVideoSampleDescription(pDemux, pDemux->hFile, &stbl) &&
            BuildSampleTable(pDemux->hFile, &stbl, &pDemux->pSamples, &pDemux->sampleCount))
        {
            *pHaveVideo = 1;
        }
    }
    else if (MP4_AUDIO_NONE == pDemux->audioCodec &&
             TrackIsHandler(pDemux->hFile, &mdia, FOURCC('s', 'o', 'u', 'n')))
    {
        ParseAudioTrack(pDemux, pDemux->hFile, &mdia);
    }
}

Mp4Demux* Mp4Demux_Open(const TCHAR* pFile)
{
    Mp4Demux*     pDemux = (Mp4Demux*)AllocateZeroedTable(1, sizeof(*pDemux));
    LARGE_INTEGER size;
    Box           root;
    Box           moov;
    Box           trak;
    uint64_t offset;
    int           haveVideo = 0;

    if (NULL == pDemux)
    {
        return NULL;
    }
    pDemux->hFile =
        CreateFile(pFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
    if (INVALID_HANDLE_VALUE == pDemux->hFile || !GetFileSizeEx(pDemux->hFile, &size))
    {
        Mp4Demux_Close(pDemux);
        return NULL;
    }
    root.payload = 0;
    root.end     = (uint64_t)size.QuadPart;
    root.fileEnd = root.end;
    root.type    = 0;
    if (!FindChild(pDemux->hFile, &root, FOURCC('m', 'o', 'o', 'v'), &moov))
    {
        Mp4Demux_Close(pDemux);
        return NULL;
    }
    offset = moov.payload;
    for (;;)
    {
        if (!ReadBox(pDemux->hFile, offset, moov.end, moov.fileEnd, &trak))
        {
            break;
        }
        ParseMovieTrack(pDemux, &trak, &haveVideo);
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
extern FORCE_INLINE unsigned Mp4Demux_GetTimescale(const Mp4Demux* pDemux)
{
    return pDemux->timescale;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE int Mp4Demux_GetNalLengthSize(const Mp4Demux* pDemux)
{
    return pDemux->nalLengthSize;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE const byte_t* Mp4Demux_GetSps(const Mp4Demux* pDemux, uint32_t* pSize)
{
    *pSize = pDemux->spsSize;
    return pDemux->sps;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE const byte_t* Mp4Demux_GetPps(const Mp4Demux* pDemux, uint32_t* pSize)
{
    *pSize = pDemux->ppsSize;
    return pDemux->pps;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE unsigned Mp4Demux_GetSampleCount(const Mp4Demux* pDemux)
{
    return pDemux->sampleCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE const Mp4Sample* Mp4Demux_GetSample(const Mp4Demux* pDemux, unsigned index)
{
    return (index < pDemux->sampleCount) ? &pDemux->pSamples[index] : NULL;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE Mp4AudioCodec Mp4Demux_GetAudioCodec(const Mp4Demux* pDemux)
{
    return pDemux->audioCodec;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE unsigned Mp4Demux_GetAudioSampleRate(const Mp4Demux* pDemux)
{
    return pDemux->audioSampleRate;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE unsigned Mp4Demux_GetAudioChannels(const Mp4Demux* pDemux)
{
    return pDemux->audioChannels;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE unsigned Mp4Demux_GetAudioTimescale(const Mp4Demux* pDemux)
{
    return pDemux->audioTimescale;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE const byte_t* Mp4Demux_GetAudioConfig(const Mp4Demux* pDemux, uint32_t* pSize)
{
    *pSize = pDemux->audioConfigSize;
    return pDemux->audioConfig;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE unsigned Mp4Demux_GetAudioSampleCount(const Mp4Demux* pDemux)
{
    return pDemux->audioSampleCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE const Mp4Sample* Mp4Demux_GetAudioSample(const Mp4Demux* pDemux, unsigned index)
{
    return (index < pDemux->audioSampleCount) ? &pDemux->pAudioSamples[index] : NULL;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Checks the selected sample and caller capacity before reading its file span.
 */
extern FORCE_INLINE uint32_t Mp4Demux_ReadSample(Mp4Demux* pDemux, unsigned index, byte_t* pBuffer, uint32_t capacity)
{
    const Mp4Sample* pSample = Mp4Demux_GetSample(pDemux, index);

    return ReadSelectedSample(pDemux, pSample, pBuffer, capacity);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Checks the selected audio sample and caller capacity before reading its span.
 */
extern FORCE_INLINE uint32_t Mp4Demux_ReadAudioSample(Mp4Demux* pDemux, unsigned index, byte_t* pBuffer, uint32_t capacity)
{
    const Mp4Sample* pSample = Mp4Demux_GetAudioSample(pDemux, index);

    return ReadSelectedSample(pDemux, pSample, pBuffer, capacity);
}

static int FindTextTrackBoxes(HANDLE file, const Box* pTrack, Box* pMdia, Box* pStbl)
{
    Box minf;
    Box stsd;
    Box entry;

    return pTrack->type == FOURCC('t', 'r', 'a', 'k') &&
           FindChild(file, pTrack, FOURCC('m', 'd', 'i', 'a'), pMdia) &&
           FindChild(file, pMdia, FOURCC('m', 'i', 'n', 'f'), &minf) &&
           FindChild(file, &minf, FOURCC('s', 't', 'b', 'l'), pStbl) &&
           FindChild(file, pStbl, FOURCC('s', 't', 's', 'd'), &stsd) &&
           ReadBox(file, stsd.payload + MP4_FULL_BOX_PREFIX_BYTES, stsd.end, stsd.fileEnd, &entry) &&
           entry.type == FOURCC('t', 'x', '3', 'g');
}

static int ReadTextMediaHeader(HANDLE file, const Box* pMdia, unsigned* pTimescale, char language[4])
{
    Box mdhd;
    byte_t header[MP4_MEDIA_HEADER_V1_BYTES];
    unsigned lang;
    unsigned i;

    if (!FindChild(file, pMdia, FOURCC('m', 'd', 'h', 'd'), &mdhd) ||
        mdhd.end - mdhd.payload < MP4_MEDIA_HEADER_V0_BYTES ||
        !ReadAt(file, mdhd.payload, header, MP4_MEDIA_HEADER_V0_BYTES))
    {
        return 0;
    }
    if (header[0] == 1 && (mdhd.end - mdhd.payload < MP4_MEDIA_HEADER_V1_BYTES ||
                           !ReadAt(file, mdhd.payload, header, MP4_MEDIA_HEADER_V1_BYTES)))
    {
        return 0;
    }
    *pTimescale = Be32(header + (header[0] == 1 ? MP4_MEDIA_TIMESCALE_V1_OFFSET : MP4_MEDIA_TIMESCALE_V0_OFFSET));
    if (0 == *pTimescale)
    {
        return 0;
    }
    lang = Be16(header + (header[0] == 1 ? MP4_MEDIA_LANGUAGE_V1_OFFSET : MP4_MEDIA_LANGUAGE_V0_OFFSET));
    for (i = 0; i < 3; ++i)
    {
        language[i] = (char)(((lang >> (MP4_LANGUAGE_FIRST_SHIFT - (MP4_LANGUAGE_BITS_PER_CHAR * i))) & MP4_LANGUAGE_CHAR_MASK) +
                             MP4_LANGUAGE_CHAR_BASE);
    }
    language[3] = 0;
    if (language[0] < 'a' || language[0] > 'z' || language[1] < 'a' || language[1] > 'z' ||
        language[2] < 'a' || language[2] > 'z')
    {
        CRT_Memory_Copy(language, "und", sizeof("und"));
    }
    return 1;
}

static int ReadTextEditList(HANDLE file, const Box* pMoov, const Box* pTrack,
                            unsigned timescale, double* pShift, double* pLimit)
{
    Box edts;
    Box elst;
    Box mvhd;
    byte_t header[MP4_MEDIA_HEADER_V1_BYTES];
    size_t editSize = 0;
    byte_t* edits;
    unsigned movieScale = 0;
    unsigned n;
    unsigned stride;
    unsigned i;
    double lead = 0;
    int haveMedia = 0;
    int valid = 1;

    *pShift = 0;
    *pLimit = -1;
    if (!FindChild(file, pTrack, FOURCC('e', 'd', 't', 's'), &edts) ||
        !FindChild(file, &edts, FOURCC('e', 'l', 's', 't'), &elst))
    {
        return 1;
    }
    edits = ReadPayload(file, &elst, &editSize);
    if (FindChild(file, pMoov, FOURCC('m', 'v', 'h', 'd'), &mvhd) &&
        ReadAt(file, mvhd.payload, header, MP4_MOVIE_HEADER_READ_BYTES))
    {
        movieScale = Be32(header + (header[0] == 1 ? MP4_MEDIA_TIMESCALE_V1_OFFSET : MP4_MEDIA_TIMESCALE_V0_OFFSET));
    }
    if (NULL == edits || editSize < MP4_FULL_BOX_PREFIX_BYTES || 0 == movieScale)
    {
        ReleaseTableBytes(edits);
        return 0;
    }
    n = Be32(edits + 4);
    stride = edits[0] == 1 ? MP4_EDIT_ENTRY_V1_BYTES : MP4_EDIT_ENTRY_V0_BYTES;
    if (n > 2 || ((uint64_t)n * stride) + MP4_FULL_BOX_PREFIX_BYTES > editSize)
    {
        ReleaseTableBytes(edits);
        return 0;
    }
    for (i = 0; i < n; ++i)
    {
        const byte_t* e = edits + MP4_FULL_BOX_PREFIX_BYTES + ((uint64_t)i * stride);
        uint64_t duration = stride == MP4_EDIT_ENTRY_V1_BYTES ? Be64(e) : Be32(e);
        int64_t mediaTime = stride == MP4_EDIT_ENTRY_V1_BYTES ? (int64_t)Be64(e + MP4_BYTES_PER_U64)
                                                              : (int32_t)Be32(e + MP4_BYTES_PER_U32);

        if (Be32(e + stride - MP4_BYTES_PER_U32) != MP4_EDIT_RATE_NORMAL || haveMedia || mediaTime < -1)
        {
            valid = 0;
            break;
        }
        if (mediaTime == -1)
        {
            lead += (double)duration / movieScale;
        }
        else
        {
            *pShift = lead - ((double)mediaTime / timescale);
            *pLimit = lead + ((double)duration / movieScale);
            haveMedia = 1;
        }
    }
    ReleaseTableBytes(edits);
    return valid;
}

static int EmitTextSamples(HANDLE file, const Box* pStbl, uint64_t fileEnd,
                           unsigned timescale, double shift, double limit,
                           Mp4TextProc callback, void* pUser)
{
    Mp4Sample* samples = NULL;
    uint32_t count = 0;
    byte_t* data = NULL;
    uint32_t i;
    int ok = 0;

    if (!BuildSampleTable(file, pStbl, &samples, &count))
    {
        goto done;
    }
    data = (byte_t*)AllocateTableBytes(MP4_SUBTITLE_SAMPLE_CAPACITY);
    if (NULL == data)
    {
        goto done;
    }
    for (i = 0; i < count; ++i)
    {
        Mp4Sample sample;
        double start;
        double end;
        bool_t hasInterval;
        bool_t accepted;

        sample = samples[i];
        start = ((double)sample.compositionTime / timescale) + shift;
        end = start + ((double)sample.duration / timescale);

        if (MP4_SUBTITLE_SAMPLE_CAPACITY < sample.size || fileEnd < sample.offset ||
            fileEnd - sample.offset < sample.size ||
            !ReadAt(file, sample.offset, data, sample.size))
        {
            goto done;
        }
        if (limit >= 0 && end > limit)
        {
            end = limit;
        }
        if (start < 0)
        {
            start = 0;
        }
        hasInterval = (start < end);
        accepted = TRUE;
        if (hasInterval)
        {
            accepted = callback(pUser, start, end, data, sample.size);
        }
        if (!accepted)
        {
            goto done;
        }
    }
    ok = 1;
done:
    ReleaseTableBytes(samples);
    ReleaseTableBytes(data);
    return ok;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Locates the requested timed-text track, applies the supported edit-list timing
 *  and dispatches nonempty sample intervals while temporary sample storage is valid.
 */
int Mp4Demux_ReadTextTrack(const TCHAR* pFile, unsigned index, char language[4], Mp4TextProc callback, void* pUser)
{
    HANDLE file = CreateFile(pFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
    LARGE_INTEGER size;
    Box root;
    Box moov;
    Box trak;
    Box mdia;
    Box stbl;
    uint64_t offset;
    unsigned timescale;
    double shift;
    double limit;
    int ok = 0;

    if (INVALID_HANDLE_VALUE == file)
    {
        return 0;
    }
    if (!GetFileSizeEx(file, &size))
    {
        goto done;
    }
    root.payload = 0;
    root.end = (uint64_t)size.QuadPart;
    root.fileEnd = root.end;
    if (!FindChild(file, &root, FOURCC('m', 'o', 'o', 'v'), &moov))
    {
        goto done;
    }
    for (offset = moov.payload; ReadBox(file, offset, moov.end, moov.fileEnd, &trak); offset = trak.end)
    {
                if (!FindTextTrackBoxes(file, &trak, &mdia, &stbl))
                {
                    continue;
                }
                if (index-- > 0)
                {
                    continue;
                }
                if (!ReadTextMediaHeader(file, &mdia, &timescale, language))
                {
                    break;
                }
                if (NULL == callback)
                {
                    ok = 1;
                    break;
                }
                if (ReadTextEditList(file, &moov, &trak, timescale, &shift, &limit))
                {
                    ok = EmitTextSamples(file, &stbl, root.end, timescale, shift, limit, callback, pUser);
                }
                break;
    }
done:
    CloseHandle(file);
    return ok;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Closes the source file and frees video/audio tables and the demux owner.
 *  NULL is accepted.
 */
extern FORCE_INLINE void Mp4Demux_Close(Mp4Demux* pDemux)
{
    if (NULL == pDemux)
    {
        return;
    }
    if (INVALID_HANDLE_VALUE != pDemux->hFile && NULL != pDemux->hFile)
    {
        CloseHandle(pDemux->hFile);
    }
    ReleaseTableBytes(pDemux->pSamples);
    ReleaseTableBytes(pDemux->pAudioSamples);
    ReleaseTableBytes(pDemux);
}
