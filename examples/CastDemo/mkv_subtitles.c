#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "mkv_subtitles.h"

#define ID_EBML    0x1a45dfa3u
#define ID_SEGMENT 0x18538067u
#define ID_INFO    0x1549a966u
#define ID_TRACKS  0x1654ae6bu
#define ID_CLUSTER 0x1f43b675u
#define MAX_CUE_BYTES (1024u * 1024u)

typedef struct Reader
{
    HANDLE file, mapping;
    const uint8_t* data;
    size_t size;
} Reader;

typedef struct Element
{
    uint32_t id;
    size_t data, end;
    int unknown;
} Element;

static void CloseReader(Reader* reader)
{
    if (reader->data) UnmapViewOfFile(reader->data);
    if (reader->mapping) CloseHandle(reader->mapping);
    if (reader->file != INVALID_HANDLE_VALUE) CloseHandle(reader->file);
}

static int OpenReader(const wchar_t* path, Reader* reader)
{
    LARGE_INTEGER size;
    memset(reader, 0, sizeof(*reader));
    reader->file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
    if (reader->file == INVALID_HANDLE_VALUE) return 0;
    if (!GetFileSizeEx(reader->file, &size) || size.QuadPart <= 0 || (uint64_t)size.QuadPart > SIZE_MAX) goto fail;
    reader->size = (size_t)size.QuadPart;
    reader->mapping = CreateFileMappingW(reader->file, NULL, PAGE_READONLY, 0, 0, NULL);
    if (!reader->mapping) goto fail;
    /* Mapping does not read the movie into RAM. Only the small headers and selected
    ** subtitle payloads are touched; video/audio payloads are skipped by length. */
    reader->data = (const uint8_t*)MapViewOfFile(reader->mapping, FILE_MAP_READ, 0, 0, 0);
    if (!reader->data) goto fail;
    return 1;
fail:
    CloseReader(reader);
    return 0;
}

static int Vint(const uint8_t* data, size_t limit, size_t* cursor, uint64_t* value, int id, int* unknown)
{
    unsigned length = 1, mask = 0x80;
    uint64_t result;
    if (*cursor >= limit || !data[*cursor]) return 0;
    while (!(data[*cursor] & mask)) { ++length; mask >>= 1; }
    if (length > (id ? 4u : 8u) || length > limit - *cursor) return 0;
    result = data[(*cursor)++] & (id ? 0xffu : mask - 1);
    for (unsigned i = 1; i < length; ++i) result = (result << 8) | data[(*cursor)++];
    if (unknown) *unknown = !id && result == ((UINT64_C(1) << (7 * length)) - 1);
    *value = result;
    return 1;
}

static int ReadElement(const Reader* reader, size_t offset, size_t limit, Element* element)
{
    uint64_t id, size;
    if (limit > reader->size || !Vint(reader->data, limit, &offset, &id, 1, NULL) ||
        !Vint(reader->data, limit, &offset, &size, 0, &element->unknown)) return 0;
    element->id = (uint32_t)id;
    element->data = offset;
    if (element->unknown)
    {
        if (id != ID_SEGMENT && id != ID_CLUSTER) return 0;
        element->end = limit;
    }
    else
    {
        if (size > limit - offset) return 0;
        element->end = offset + (size_t)size;
    }
    return 1;
}

static int Uint(const Reader* reader, const Element* element, uint64_t* value)
{
    size_t length = element->end - element->data;
    uint64_t result = 0;
    if (length > 8) return 0;
    for (size_t i = element->data; i < element->end; ++i) result = (result << 8) | reader->data[i];
    *value = result;
    return 1;
}

static int String(const Reader* reader, const Element* element, char* out, size_t capacity)
{
    size_t length = element->end - element->data;
    if (length >= capacity) length = capacity - 1;
    memcpy(out, reader->data + element->data, length);
    out[length] = 0;
    return 1;
}

static int Segment(const Reader* reader, Element* segment)
{
    Element element;
    size_t offset = 0;
    if (!ReadElement(reader, offset, reader->size, &element) || element.id != ID_EBML) return 0;
    for (offset = element.end; offset < reader->size; offset = element.end)
    {
        if (!ReadElement(reader, offset, reader->size, &element)) return 0;
        if (element.id == ID_SEGMENT) { *segment = element; return 1; }
    }
    return 0;
}

/* Returns 1 for a supported track, 0 for a codec we cannot render, -1 for bad EBML. */
static int Track(const Reader* reader, const Element* entry, MkvSubtitleTrack* track)
{
    Element element;
    uint64_t type = 0;
    char codec[64] = {0}, ietf[16] = {0};
    int encoded = 0;
    memset(track, 0, sizeof(*track));
    memcpy(track->language, "eng", 4); /* Matroska's Language default */
    track->timestampScale = 1;
    for (size_t offset = entry->data; offset < entry->end; offset = element.end)
    {
        if (!ReadElement(reader, offset, entry->end, &element)) return -1;
        switch (element.id)
        {
        case 0xd7: if (!Uint(reader, &element, &track->number)) return -1; break;
        case 0x83: if (!Uint(reader, &element, &type)) return -1; break;
        case 0x86: String(reader, &element, codec, sizeof(codec)); break;
        case 0x22b59c: String(reader, &element, track->language, sizeof(track->language)); break;
        case 0x22b59d: String(reader, &element, ietf, sizeof(ietf)); break;
        case 0x536e: String(reader, &element, track->name, sizeof(track->name)); break;
        case 0x23e383: if (!Uint(reader, &element, &track->defaultDuration)) return -1; break;
        case 0x56aa: if (!Uint(reader, &element, &track->codecDelay)) return -1; break;
        case 0x6d80: encoded = 1; break;
        case 0x23314f:
        {
            uint64_t bits;
            size_t length = element.end - element.data;
            if (!Uint(reader, &element, &bits)) return -1;
            if (length == 4) { uint32_t b = (uint32_t)bits; float f; memcpy(&f, &b, 4); track->timestampScale = f; }
            else if (length == 8) memcpy(&track->timestampScale, &bits, 8);
            else return -1;
            break;
        }
        default: break;
        }
    }
    if (ietf[0]) memcpy(track->language, ietf, sizeof(ietf));
    if (!isfinite(track->timestampScale) || track->timestampScale <= 0) return -1;
    return type == 0x11 && track->number && !encoded && !strcmp(codec, "S_TEXT/UTF8");
}

static int Metadata(const Reader* reader, const Element* segment, MkvSubtitleTrack* tracks,
                    unsigned capacity, unsigned* count, uint64_t* timestampScale)
{
    Element element;
    int haveTracks = 0, haveInfo = 0;
    *count = 0;
    *timestampScale = 1000000;
    for (size_t offset = segment->data; offset < segment->end; offset = element.end)
    {
        if (!ReadElement(reader, offset, segment->end, &element)) return 0;
        if (element.id == ID_TRACKS)
        {
            Element entry;
            for (size_t p = element.data; p < element.end; p = entry.end)
            {
                MkvSubtitleTrack track;
                int result;
                if (!ReadElement(reader, p, element.end, &entry)) return 0;
                if (entry.id != 0xae) continue;
                result = Track(reader, &entry, &track);
                if (result < 0) return 0;
                if (result && *count < capacity) tracks[(*count)++] = track;
            }
            haveTracks = 1;
        }
        else if (element.id == ID_INFO)
        {
            Element entry;
            for (size_t p = element.data; p < element.end; p = entry.end)
            {
                if (!ReadElement(reader, p, element.end, &entry)) return 0;
                if (entry.id == 0x2ad7b1 && (!Uint(reader, &entry, timestampScale) || !*timestampScale)) return 0;
            }
            haveInfo = 1;
        }
        if (haveTracks && haveInfo) return 1;
        if (element.id == ID_CLUSTER && haveTracks) return 1;
    }
    return haveTracks;
}

unsigned MkvSubtitles_Find(const wchar_t* path, MkvSubtitleTrack* tracks, unsigned capacity)
{
    Reader reader;
    Element segment;
    unsigned count = 0;
    uint64_t scale;
    if (!OpenReader(path, &reader)) return 0;
    if (!Segment(&reader, &segment) || !Metadata(&reader, &segment, tracks, capacity, &count, &scale)) count = 0;
    CloseReader(&reader);
    return count;
}

static int EmitBlock(const Reader* reader, const Element* block, const MkvSubtitleTrack* track,
                     uint64_t clusterTime, uint64_t scale, uint64_t duration, MkvSubtitleProc callback, void* user)
{
    size_t offset = block->data;
    uint64_t number;
    int16_t relative;
    double start, end;
    if (!Vint(reader->data, block->end, &offset, &number, 0, NULL)) return 0;
    if (number != track->number) return 1;
    if (block->end - offset < 3 || (reader->data[offset + 2] & 0x06)) return 0; /* no subtitle lacing */
    relative = (int16_t)((unsigned)reader->data[offset] * 256 + reader->data[offset + 1]);
    offset += 3;
    if (block->end - offset > MAX_CUE_BYTES) return 0;
    start = ((double)clusterTime + (double)relative * track->timestampScale) * (double)scale / 1e9 - (double)track->codecDelay / 1e9;
    end = start + (duration ? (double)duration * (double)scale * track->timestampScale : (double)track->defaultDuration) / 1e9;
    if (!isfinite(start) || !isfinite(end) || end <= start || end > 36000000) return 0;
    if (end <= 0 || block->end == offset) return 1;
    return callback(user, start < 0 ? 0 : start, end, reader->data + offset, block->end - offset);
}

static int TopLevel(uint32_t id)
{
    return id == ID_CLUSTER || id == ID_INFO || id == ID_TRACKS || id == 0x114d9b74 ||
           id == 0x1c53bb6b || id == 0x1941a469 || id == 0x1043a770 || id == 0x1254c367;
}

static int Cluster(const Reader* reader, const Element* cluster, const MkvSubtitleTrack* track,
                   uint64_t scale, MkvSubtitleProc callback, void* user, size_t* next)
{
    Element element;
    uint64_t time = 0;
    int haveTime = 0;
    *next = cluster->end;
    for (size_t offset = cluster->data; offset < cluster->end; offset = element.end)
    {
        if (!ReadElement(reader, offset, cluster->end, &element)) return 0;
        if (cluster->unknown && TopLevel(element.id)) { *next = offset; break; }
        if (element.id == 0xe7)
        {
            if (!Uint(reader, &element, &time)) return 0;
            haveTime = 1;
        }
        else if (element.id == 0xa0)
        {
            Element child, block = {0};
            uint64_t duration = 0;
            if (!haveTime) return 0;
            for (size_t p = element.data; p < element.end; p = child.end)
            {
                if (!ReadElement(reader, p, element.end, &child)) return 0;
                if (child.id == 0xa1) block = child;
                else if (child.id == 0x9b && !Uint(reader, &child, &duration)) return 0;
            }
            if (block.id && !EmitBlock(reader, &block, track, time, scale, duration, callback, user)) return 0;
        }
        else if (element.id == 0xa3)
        {
            if (!haveTime || !EmitBlock(reader, &element, track, time, scale, 0, callback, user)) return 0;
        }
    }
    return 1;
}

int MkvSubtitles_Read(const wchar_t* path, uint64_t trackNumber, MkvSubtitleProc callback, void* user)
{
    Reader reader;
    Element segment, element;
    MkvSubtitleTrack tracks[64], *track = NULL;
    unsigned count;
    uint64_t scale;
    int ok = 0;
    if (!callback || !OpenReader(path, &reader)) return 0;
    if (!Segment(&reader, &segment) || !Metadata(&reader, &segment, tracks, 64, &count, &scale)) goto done;
    for (unsigned i = 0; i < count; ++i) if (tracks[i].number == trackNumber) { track = &tracks[i]; break; }
    if (!track) goto done;
    for (size_t offset = segment.data; offset < segment.end; )
    {
        if (!ReadElement(&reader, offset, segment.end, &element)) goto done;
        offset = element.end;
        if (element.id == ID_CLUSTER && !Cluster(&reader, &element, track, scale, callback, user, &offset)) goto done;
    }
    ok = 1;
done:
    CloseReader(&reader);
    return ok;
}
