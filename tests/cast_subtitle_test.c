/* MKV subtitle regression fixtures and an opt-in local-file extraction check. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "subtitles.h"
#include "mkv_subtitles.h"

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "line %d: %s\n", __LINE__, #x); return 1; } } while (0)
typedef struct Buffer { uint8_t data[8192]; size_t size; } Buffer;

static void Element(Buffer* out, uint32_t id, const void* data, size_t size)
{
    int shift = 24;
    while (shift && !(id >> shift)) shift -= 8;
    for (; shift >= 0; shift -= 8) out->data[out->size++] = (uint8_t)(id >> shift);
    out->data[out->size++] = (uint8_t)(0x40 | (size >> 8));
    out->data[out->size++] = (uint8_t)size;
    if (size) memcpy(out->data + out->size, data, size);
    out->size += size;
}

static void Uint(Buffer* out, uint32_t id, uint64_t value)
{
    uint8_t bytes[8];
    for (int i = 0; i < 8; ++i) bytes[i] = (uint8_t)(value >> (56 - i * 8));
    Element(out, id, bytes, 8);
}

static void Track(Buffer* tracks, unsigned number, const char* codec, const char* language, int ietf)
{
    Buffer entry = {0};
    Uint(&entry, 0xd7, number);
    Uint(&entry, 0x83, 0x11);
    Element(&entry, 0x86, codec, strlen(codec));
    Element(&entry, 0x22b59c, language, strlen(language));
    if (ietf) Element(&entry, 0x22b59d, "da-DK", 5);
    Element(&entry, 0x536e, "Full subtitles", 14);
    Uint(&entry, 0x56aa, 100000000); /* 100 ms codec delay */
    Element(tracks, 0xae, entry.data, entry.size);
}

static void Cue(Buffer* cluster, unsigned track, int relative, unsigned duration, const char* text)
{
    Buffer block = {0}, group = {0};
    if (track >= 127) block.data[block.size++] = (uint8_t)(0x40 | (track >> 8));
    block.data[block.size++] = (uint8_t)(track < 127 ? 0x80 | track : track);
    block.data[block.size++] = (uint8_t)((uint16_t)relative >> 8);
    block.data[block.size++] = (uint8_t)relative;
    block.data[block.size++] = 0;
    memcpy(block.data + block.size, text, strlen(text)); block.size += strlen(text);
    /* Duration before Block exercises order-independent BlockGroup parsing. */
    Uint(&group, 0x9b, duration);
    Element(&group, 0xa1, block.data, block.size);
    Element(cluster, 0xa0, group.data, group.size);
}

static int WriteFileBytes(const wchar_t* path, const void* data, size_t size)
{
    FILE* file = NULL;
    int ok;
    if (_wfopen_s(&file, path, L"wb") || !file) return 0;
    ok = fwrite(data, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}

static int FixtureTests(void)
{
    Buffer file = {0}, segment = {0}, tracks = {0}, info = {0}, cluster = {0};
    SubtitleTrack found[SUBTITLE_MAX_TRACKS];
    wchar_t directory[MAX_PATH], path[MAX_PATH];
    char error[256], *vtt;
    size_t size;
    unsigned count;
    CHECK(GetTempPathW(MAX_PATH, directory));
    CHECK(GetTempFileNameW(directory, L"cvs", 0, path));
    Uint(&info, 0x2ad7b1, 2000000); /* non-default 2 ms Segment ticks */
    Element(&segment, 0x1549a966, info.data, info.size);
    Track(&tracks, 4, "S_TEXT/UTF8", "eng", 0);
    Track(&tracks, 5, "S_HDMV/PGS", "eng", 0);
    Track(&tracks, 130, "S_TEXT/UTF8", "dan", 1);
    Element(&segment, 0x1654ae6b, tracks.data, tracks.size);
    Uint(&cluster, 0xe7, 1000);
    Cue(&cluster, 4, -250, 500, "<i>Hello</i>\r\nSecond line");
    Cue(&cluster, 130, -250, 500, "Bl\xc3\xa5");
    /* Unknown-sized clusters terminate at the next Segment-level element. */
    memcpy(segment.data + segment.size, "\x1f\x43\xb6\x75\xff", 5); segment.size += 5;
    memcpy(segment.data + segment.size, cluster.data, cluster.size); segment.size += cluster.size;
    cluster.size = 0;
    Uint(&cluster, 0xe7, 3000);
    Cue(&cluster, 4, 0, 1000, "Later");
    Element(&segment, 0x1f43b675, cluster.data, cluster.size);
    Element(&file, 0x1a45dfa3, NULL, 0);
    Element(&file, 0x18538067, segment.data, segment.size);
    CHECK(WriteFileBytes(path, file.data, file.size));
    count = Subtitles_Find(path, found);
    CHECK(count == 2);
    CHECK(found[0].matroskaTrack == 4 && !strcmp(found[0].language, "eng"));
    CHECK(found[1].matroskaTrack == 130 && !strcmp(found[1].language, "da-DK"));
    vtt = Subtitles_Load(&found[0], &size, error, sizeof(error));
    CHECK(vtt && size == strlen(vtt));
    CHECK(!strcmp(vtt, "WEBVTT\n\n00:00:01.400 --> 00:00:02.400\n<i>Hello</i>\nSecond line\n\n00:00:05.900 --> 00:00:07.900\nLater\n\n"));
    free(vtt);
    vtt = Subtitles_Load(&found[1], &size, error, sizeof(error));
    CHECK(vtt && strstr(vtt, "Bl\xc3\xa5")); free(vtt);
    found[1].matroskaTrack = 999;
    CHECK(!Subtitles_Load(&found[1], &size, error, sizeof(error)));
    CHECK(WriteFileBytes(path, file.data, file.size - 1));
    CHECK(!Subtitles_Load(&found[0], &size, error, sizeof(error)));
    file.data[0] = 0;
    CHECK(WriteFileBytes(path, file.data, file.size));
    CHECK(Subtitles_Find(path, found) == 0);
    CHECK(DeleteFileW(path));
    puts("subtitle fixture checks passed");
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    SubtitleTrack tracks[SUBTITLE_MAX_TRACKS];
    unsigned count;
    if (argc == 1) return FixtureTests();
    if (argc != 3) { fprintf(stderr, "usage: cvk-cast-subtitle-test [video output-directory]\n"); return 1; }
    count = Subtitles_Find(argv[1], tracks);
    CHECK(count > 0);
    CHECK(CreateDirectoryW(argv[2], NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
    printf("detected %u subtitle tracks\n", count);
    for (unsigned i = 0; i < count; ++i)
    {
        char error[256], *vtt, *cursor;
        wchar_t output[MAX_PATH];
        size_t size;
        unsigned cues = 0;
        ULONGLONG begin = GetTickCount64();
        vtt = Subtitles_Load(&tracks[i], &size, error, sizeof(error));
        if (!vtt) { fprintf(stderr, "track %u: %s\n", i, error); return 1; }
        for (cursor = vtt; (cursor = strstr(cursor, " --> ")) != NULL; cursor += 5) ++cues;
        CHECK(cues > 0);
        CHECK(_snwprintf_s(output, MAX_PATH, _TRUNCATE, L"%ls\\track-%llu.vtt", argv[2], (unsigned long long)tracks[i].matroskaTrack) >= 0);
        CHECK(WriteFileBytes(output, vtt, size));
        printf("track %llu (%s): %u cues, %zu bytes, %llu ms\n", (unsigned long long)tracks[i].matroskaTrack,
               tracks[i].language, cues, size, GetTickCount64() - begin);
        free(vtt);
    }
    return 0;
}
