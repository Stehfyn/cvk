/* mp4_index.c - walks moov/trak/mdia/minf/stbl and converts the stss sync-sample numbers to
** decode times through stts. Composition offsets (ctts) and edit lists are ignored: they shift
** a preview by a frame or two at most. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mp4_index.h"
#include "utl/utl_mp4.h"

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))
#define MAX_TABLE_BYTES    (64u * 1024u * 1024u)

typedef struct Box
{
    uint64_t start;
    uint64_t payload;
    uint64_t end;
    uint32_t type;
} Box;

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
 *  Moves the file pointer to an absolute byte offset and requires an exact read.
 */
static int ReadAt(HANDLE hFile, uint64_t offset, void* pOut, DWORD size)
{
    LARGE_INTEGER position;
    DWORD         read = 0;

    position.QuadPart = (LONGLONG)offset;
    return SetFilePointerEx(hFile, position, NULL, FILE_BEGIN) && ReadFile(hFile, pOut, size, &read, NULL) &&
           read == size;
}

/* Reads the box header at offset. Returns 0 at the end of the parent or on a malformed size. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Reads a bounded header prefix and delegates box-size validation to utl_mp4
 *  before publishing absolute payload and end offsets.
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
        pBox->start   = offset;
        pBox->type    = header.type;
        pBox->payload = offset + header.headerByteCount;
        pBox->end     = offset + header.byteCount;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Advances through immediate child boxes until the requested type is found.
 */
static int FindChild(HANDLE hFile, const Box* pParent, uint32_t type, Box* pChild)
{
    uint64_t offset;
    bool_t   found;
    bool_t   is_valid;
    Box      candidate;

    offset = pParent->payload;
    found  = FALSE;
    for (;;)
    {
        is_valid = ReadBox(hFile, offset, pParent->end, &candidate);
        if (!is_valid)
        {
            break;
        }
        found = (candidate.type == type);
        if (found)
        {
            (*pChild) = candidate;
            break;
        }
        offset = candidate.end;
    }

    return found;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Allocates a bounded table payload and frees it if the exact file read fails.
 */
static uint8_t* ReadPayload(HANDLE hFile, const Box* pBox, uint64_t* pSize)
{
    uint64_t size;
    bool_t   is_valid;
    uint8_t* data;

    size     = pBox->end - pBox->payload;
    is_valid = (8 <= size) && (size <= MAX_TABLE_BYTES);
    data     = NULL;
    if (is_valid)
    {
        data     = (uint8_t*)malloc(size);
        is_valid = (NULL != data);
    }
    if (is_valid)
    {
        is_valid = ReadAt(hFile, pBox->payload, data, (DWORD)size);
        if (!is_valid)
        {
            free(data);
            data = NULL;
        }
    }
    if (is_valid)
    {
        (*pSize) = size;
    }

    return data;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Selects a video track and maps sync-sample numbers through decode-time runs
 *  to seconds, bounded by the caller's output capacity.
 */
static unsigned KeyframesFromTrack(HANDLE hFile, const Box* pTrack, double* pTimes, unsigned capacity)
{
    uint32_t timescale;
    uint8_t* stts_data;
    uint8_t* stss_data;
    uint64_t stts_size;
    uint64_t stss_size;
    uint32_t count;
    bool_t   is_valid;
    Box      mdia;
    Box      hdlr;
    uint8_t  buffer[32];
    Box      mdhd;
    Box      minf;
    Box      stbl;
    Box      stts;
    Box      stss;
    uint32_t stts_count;
    uint32_t stss_count;
    bool_t   has_timing;
    bool_t   has_samples;
    uint32_t entry;
    uint64_t sample_base;
    uint64_t time_base;
    uint32_t entry_samples;
    uint32_t entry_delta;
    uint32_t index;
    uint32_t sample;
    bool_t   has_entry;
    bool_t   in_entry;

    timescale = 0;
    stts_data = NULL;
    stss_data = NULL;
    stts_size = 0;
    stss_size = 0;
    count     = 0;
    is_valid  = FindChild(hFile, pTrack, FOURCC('m', 'd', 'i', 'a'), &mdia);
    if (is_valid)
    {
        is_valid = FindChild(hFile, &mdia, FOURCC('h', 'd', 'l', 'r'), &hdlr);
    }
    if (is_valid)
    {
        is_valid = (12 <= (hdlr.end - hdlr.payload)) && ReadAt(hFile, hdlr.payload, buffer, 12);
        if (is_valid)
        {
            is_valid = (FOURCC('v', 'i', 'd', 'e') == Be32(buffer + 8));
        }
    }
    if (is_valid)
    {
        is_valid = FindChild(hFile, &mdia, FOURCC('m', 'd', 'h', 'd'), &mdhd);
    }
    if (is_valid)
    {
        is_valid = (16 <= (mdhd.end - mdhd.payload)) && ReadAt(hFile, mdhd.payload, buffer, 16);
        if (is_valid)
        {
            if (1 == buffer[0])
            {
                is_valid = (24 <= (mdhd.end - mdhd.payload)) && ReadAt(hFile, mdhd.payload + 16, buffer + 16, 8);
                if (is_valid)
                {
                    timescale = Be32(buffer + 20);
                }
            }
            else
            {
                is_valid = (0 == buffer[0]);
                if (is_valid)
                {
                    timescale = Be32(buffer + 12);
                }
            }
            is_valid = is_valid && (0 < timescale);
        }
    }
    if (is_valid)
    {
        is_valid = FindChild(hFile, &mdia, FOURCC('m', 'i', 'n', 'f'), &minf);
    }
    if (is_valid)
    {
        is_valid = FindChild(hFile, &minf, FOURCC('s', 't', 'b', 'l'), &stbl);
    }
    if (is_valid)
    {
        is_valid = FindChild(hFile, &stbl, FOURCC('s', 't', 't', 's'), &stts);
    }
    if (is_valid)
    {
        is_valid = FindChild(hFile, &stbl, FOURCC('s', 't', 's', 's'), &stss);
    }
    if (is_valid)
    {
        stts_data = ReadPayload(hFile, &stts, &stts_size);
        stss_data = ReadPayload(hFile, &stss, &stss_size);
        is_valid  = (NULL != stts_data) && (NULL != stss_data);
    }
    if (is_valid)
    {
        stts_count  = Be32(stts_data + 4);
        stss_count  = Be32(stss_data + 4);
        has_timing  = (0 < stts_count) && ((uint64_t)stts_count * 8 + 8 <= stts_size);
        has_samples = ((uint64_t)stss_count * 4 + 8 <= stss_size);
        is_valid    = has_timing && has_samples;
        if (is_valid)
        {
            entry         = 0;
            sample_base   = 1;
            time_base     = 0;
            entry_samples = Be32(stts_data + 8);
            entry_delta   = Be32(stts_data + 12);
            for (index = 0; (index < stss_count) && (count < capacity); ++index)
            {
                sample = Be32(stss_data + 8 + (uint64_t)index * 4);
                for (;;)
                {
                    has_entry = (entry < stts_count);
                    in_entry  = (sample < (sample_base + entry_samples));
                    if ((!has_entry) || in_entry)
                    {
                        break;
                    }
                    time_base   += (uint64_t)entry_samples * entry_delta;
                    sample_base += entry_samples;
                    ++entry;
                    if (entry < stts_count)
                    {
                        entry_samples = Be32(stts_data + 8 + (uint64_t)entry * 8);
                        entry_delta   = Be32(stts_data + 12 + (uint64_t)entry * 8);
                    }
                }
                has_entry = (entry < stts_count);
                in_entry  = (sample_base <= sample);
                if (!(has_entry && in_entry))
                {
                    break;
                }
                pTimes[count] =
                    (double)(time_base + (sample - sample_base) * (uint64_t)entry_delta) / (double)timescale;
                ++count;
            }
        }
    }
    free(stts_data);
    free(stss_data);

    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Opens the file, locates movie tracks and collects the first available video
 *  sync-sample index before closing the handle.
 */
unsigned Mp4Index_ReadKeyframes(const wchar_t* pFile, double* pTimes, unsigned capacity)
{
    HANDLE        file;
    uint32_t      count;
    bool_t        is_valid;
    LARGE_INTEGER size;
    Box           root;
    Box           moov;
    uint64_t      offset;
    Box           track;

    file     = CreateFileW(pFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
    count    = 0;
    is_valid = (INVALID_HANDLE_VALUE != file);
    if (is_valid)
    {
        is_valid = GetFileSizeEx(file, &size);
        if (is_valid)
        {
            is_valid = (0 <= size.QuadPart);
        }
        if (is_valid)
        {
            root.start   = 0;
            root.payload = 0;
            root.end     = (uint64_t)size.QuadPart;
            root.type    = 0;
            is_valid     = FindChild(file, &root, FOURCC('m', 'o', 'o', 'v'), &moov);
            if (is_valid)
            {
                offset = moov.payload;
                for (;;)
                {
                    is_valid = ReadBox(file, offset, moov.end, &track);
                    if (!is_valid)
                    {
                        break;
                    }
                    if (FOURCC('t', 'r', 'a', 'k') == track.type)
                    {
                        count = KeyframesFromTrack(file, &track, pTimes, capacity);
                    }
                    if (0 < count)
                    {
                        break;
                    }
                    offset = track.end;
                }
            }
        }
        CloseHandle(file);
    }

    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Uses binary search for the last timestamp at or before the request. Empty
 *  indexes and times before the first indexed sample leave the request unchanged.
 */
double Mp4Index_SnapToKeyframe(const double* pTimes, unsigned count, double seconds)
{
    double   result;
    bool_t   has_index;
    bool_t   can_snap;
    uint32_t low;
    uint32_t high;
    uint32_t middle;

    result    = seconds;
    has_index = (0 < count);
    can_snap  = has_index && (pTimes[0] <= seconds);
    if (can_snap)
    {
        low  = 0;
        high = count;
        for (;;)
        {
            if (1 >= (high - low))
            {
                break;
            }
            middle = low + (high - low) / 2;
            if (pTimes[middle] <= seconds)
            {
                low = middle;
            }
            else
            {
                high = middle;
            }
        }
        result = pTimes[low];
    }

    return result;
}
