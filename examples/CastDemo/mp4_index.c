/* mp4_index.c - walks moov/trak/mdia/minf/stbl and converts the stss sync-sample numbers to
** decode times through stts. Composition offsets (ctts) and edit lists are ignored: they shift
** a preview by a frame or two at most. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mp4_index.h"

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define MAX_TABLE_BYTES    (64u * 1024u * 1024u)

typedef struct Box
{
    uint64_t start;
    uint64_t payload;
    uint64_t end;
    uint32_t type;
} Box;

static uint32_t Be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

static uint64_t Be64(const uint8_t* p)
{
    return ((uint64_t)Be32(p) << 32) | Be32(p + 4);
}

static int ReadAt(HANDLE hFile, uint64_t offset, void* pOut, DWORD size)
{
    LARGE_INTEGER position;
    DWORD         read = 0;

    position.QuadPart = (LONGLONG)offset;
    return SetFilePointerEx(hFile, position, NULL, FILE_BEGIN) && ReadFile(hFile, pOut, size, &read, NULL) && read == size;
}

/* Reads the box header at offset. Returns 0 at the end of the parent or on a malformed size. */
static int ReadBox(HANDLE hFile, uint64_t offset, uint64_t parentEnd, Box* pBox)
{
    uint8_t  header[16];
    uint64_t size;

    if (offset + 8 > parentEnd || !ReadAt(hFile, offset, header, 8))
    {
        return 0;
    }
    size          = Be32(header);
    pBox->type    = Be32(header + 4);
    pBox->start   = offset;
    pBox->payload = offset + 8;
    if (1 == size)
    {
        if (!ReadAt(hFile, offset + 8, header + 8, 8))
        {
            return 0;
        }
        size          = Be64(header + 8);
        pBox->payload = offset + 16;
    }
    else if (0 == size)
    {
        size = parentEnd - offset;
    }
    if (size < pBox->payload - offset || offset + size > parentEnd)
    {
        return 0;
    }
    pBox->end = offset + size;
    return 1;
}

static int FindChild(HANDLE hFile, const Box* pParent, uint32_t type, Box* pChild)
{
    uint64_t offset = pParent->payload;

    while (ReadBox(hFile, offset, pParent->end, pChild))
    {
        if (pChild->type == type)
        {
            return 1;
        }
        offset = pChild->end;
    }
    return 0;
}

static uint8_t* ReadPayload(HANDLE hFile, const Box* pBox, size_t* pSize)
{
    const uint64_t size = pBox->end - pBox->payload;
    uint8_t*       pData;

    if (size < 8 || size > MAX_TABLE_BYTES)
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

static unsigned KeyframesFromTrack(HANDLE hFile, const Box* pTrack, double* pTimes, unsigned capacity)
{
    Box      mdia, hdlr, mdhd, minf, stbl, stts, stss;
    uint8_t  buffer[32];
    uint32_t timescale;
    uint8_t* pStts    = NULL;
    uint8_t* pStss    = NULL;
    size_t   sttsSize = 0;
    size_t   stssSize = 0;
    unsigned count    = 0;

    if (!FindChild(hFile, pTrack, FOURCC('m', 'd', 'i', 'a'), &mdia) || !FindChild(hFile, &mdia, FOURCC('h', 'd', 'l', 'r'), &hdlr) || !ReadAt(hFile, hdlr.payload, buffer, 12) ||
        FOURCC('v', 'i', 'd', 'e') != Be32(buffer + 8))
    {
        return 0; /* not the video track */
    }
    if (!FindChild(hFile, &mdia, FOURCC('m', 'd', 'h', 'd'), &mdhd) || !ReadAt(hFile, mdhd.payload, buffer, 32))
    {
        return 0;
    }
    timescale = (1 == buffer[0]) ? Be32(buffer + 20) : Be32(buffer + 12);
    if (0 == timescale || !FindChild(hFile, &mdia, FOURCC('m', 'i', 'n', 'f'), &minf) || !FindChild(hFile, &minf, FOURCC('s', 't', 'b', 'l'), &stbl) ||
        !FindChild(hFile, &stbl, FOURCC('s', 't', 't', 's'), &stts) || !FindChild(hFile, &stbl, FOURCC('s', 't', 's', 's'), &stss))
    {
        return 0; /* no sync sample table: every sample is a keyframe, nothing to snap */
    }
    pStts = ReadPayload(hFile, &stts, &sttsSize);
    pStss = ReadPayload(hFile, &stss, &stssSize);
    if (NULL != pStts && NULL != pStss)
    {
        const uint32_t sttsCount = Be32(pStts + 4);
        const uint32_t stssCount = Be32(pStss + 4);

        if ((uint64_t)sttsCount * 8 + 8 <= sttsSize && (uint64_t)stssCount * 4 + 8 <= stssSize && sttsCount > 0)
        {
            uint32_t entry        = 0;
            uint64_t sampleBase   = 1; /* first sample number covered by the current stts entry */
            uint64_t timeBase     = 0; /* its decode time */
            uint32_t entrySamples = Be32(pStts + 8);
            uint32_t entryDelta   = Be32(pStts + 12);
            uint32_t index;

            for (index = 0; index < stssCount && count < capacity; ++index)
            {
                const uint32_t sample = Be32(pStss + 8 + index * 4);

                while (entry < sttsCount && sample >= sampleBase + entrySamples)
                {
                    timeBase += (uint64_t)entrySamples * entryDelta;
                    sampleBase += entrySamples;
                    ++entry;
                    if (entry < sttsCount)
                    {
                        entrySamples = Be32(pStts + 8 + entry * 8);
                        entryDelta   = Be32(pStts + 12 + entry * 8);
                    }
                }
                if (entry >= sttsCount || sample < sampleBase)
                {
                    break;
                }
                pTimes[count++] = (double)(timeBase + (sample - sampleBase) * (uint64_t)entryDelta) / (double)timescale;
            }
        }
    }
    free(pStts);
    free(pStss);
    return count;
}

unsigned Mp4Index_ReadKeyframes(const wchar_t* pFile, double* pTimes, unsigned capacity)
{
    HANDLE        hFile = CreateFileW(pFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
    LARGE_INTEGER size;
    Box           root, moov, track;
    uint64_t      offset;
    unsigned      count = 0;

    if (INVALID_HANDLE_VALUE == hFile)
    {
        return 0;
    }
    if (GetFileSizeEx(hFile, &size))
    {
        root.start   = 0;
        root.payload = 0;
        root.end     = (uint64_t)size.QuadPart;
        root.type    = 0;
        if (FindChild(hFile, &root, FOURCC('m', 'o', 'o', 'v'), &moov))
        {
            offset = moov.payload;
            while (0 == count && ReadBox(hFile, offset, moov.end, &track))
            {
                if (FOURCC('t', 'r', 'a', 'k') == track.type)
                {
                    count = KeyframesFromTrack(hFile, &track, pTimes, capacity);
                }
                offset = track.end;
            }
        }
    }
    CloseHandle(hFile);
    return count;
}

double Mp4Index_SnapToKeyframe(const double* pTimes, unsigned count, double seconds)
{
    unsigned low  = 0;
    unsigned high = count;

    if (0 == count || seconds < pTimes[0])
    {
        return seconds;
    }
    while (high - low > 1) /* largest index with pTimes[index] <= seconds */
    {
        const unsigned middle = low + (high - low) / 2;

        if (pTimes[middle] <= seconds)
        {
            low = middle;
        }
        else
        {
            high = middle;
        }
    }
    return pTimes[low];
}
