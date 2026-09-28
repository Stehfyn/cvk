/* MKV subtitle regression fixtures and an opt-in local-file extraction check. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "subtitles.h"
#include "mkv_subtitles.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "crt/crt_memory.h"
#include "utl/utl_matroska.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_LATE_TRACK_NUMBER (65)
#define TEST_TEXT_CAPACITY     (128)
#define TEST_TEXT_GUARD        (0xA7)
#define TEST_REUSE_COUNT       (1000)
#define CHECK(x)                                                                                                       \
    for (;;)                                                                                                           \
    {                                                                                                                  \
        if (!(x))                                                                                                      \
        {                                                                                                              \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x);                                                            \
            return 1;                                                                                                  \
        }                                                                                                              \
        break;                                                                                                         \
    }
/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

typedef struct Buffer
{
    uint8_t data[8192];
    size_t  size;
} Buffer;

/* Fixed startup storage for the serial fixture builder and extraction checks. */
typedef struct FixtureWorkspace_s
{
    Buffer                file;
    Buffer                segment;
    Buffer                tracks;
    Buffer                info;
    Buffer                cluster;
    Buffer                entry;
    Buffer                cueBlock;
    Buffer                cueGroup;
    SubtitleTrack         found[SUBTITLE_MAX_TRACKS];
    Subtitles_Workspace_t subtitles;
    byte_t                subtitleScratch[SUBTITLE_TEXT_CAPACITY];
    byte_t                subtitleOutput[SUBTITLE_TEXT_CAPACITY];
} FixtureWorkspace_t;

static FixtureWorkspace_t g_fixture;

/*************************************************************************************************/
/**
 * \brief Checks bounded conversion, in-place source staging and reuse after failure.
 *
 * \par Requirements:
 *  - 001 Every insufficient capacity shall publish an empty result without touching either guard.
 *  - 002 Successful conversion shall match independent expected wire bytes.
 *  - 003 A malformed conversion shall not retain an earlier result or prevent subsequent reuse.
 *
 * \retval None   Assertion failure identifies a contract violation.
 */
static FORCE_INLINE void CheckWorkspace(void);

static void Element(Buffer* out, uint32_t id, const void* data, size_t size)
{
    int shift;
    for (shift = 24; shift > 0; shift -= 8)
    {
        if (id >> shift)
        {
            break;
        }
    }
    for (; shift >= 0; shift -= 8)
        out->data[out->size++] = (uint8_t)(id >> shift);
    out->data[out->size++] = (uint8_t)(0x40 | (size >> 8));
    out->data[out->size++] = (uint8_t)size;
    if (size)
        memcpy(out->data + out->size, data, size);
    out->size += size;
}

static void Uint(Buffer* out, uint32_t id, uint64_t value)
{
    uint8_t bytes[8];
    for (int i = 0; i < 8; ++i)
        bytes[i] = (uint8_t)(value >> (56 - i * 8));
    Element(out, id, bytes, 8);
}

static void Track(Buffer* tracks, unsigned number, const char* codec, const char* language, int ietf)
{
    Buffer* entry;

    entry       = &g_fixture.entry;
    entry->size = 0;
    Uint(entry, 0xd7, number);
    Uint(entry, 0x83, 0x11);
    Element(entry, 0x86, codec, strlen(codec));
    Element(entry, 0x22b59c, language, strlen(language));
    if (ietf)
        Element(entry, 0x22b59d, "da-DK", 5);
    Element(entry, 0x536e, "Full subtitles", 14);
    Uint(entry, 0x56aa, 100000000); /* 100 ms codec delay */
    Element(tracks, 0xae, entry->data, entry->size);
}

static void Cue(Buffer* cluster, unsigned track, int relative, unsigned duration, const char* text)
{
    g_fixture.cueBlock.size = 0;
    g_fixture.cueGroup.size = 0;
    if (track >= 127)
        g_fixture.cueBlock.data[g_fixture.cueBlock.size++] = (uint8_t)(0x40 | (track >> 8));
    g_fixture.cueBlock.data[g_fixture.cueBlock.size++] = (uint8_t)(track < 127 ? 0x80 | track : track);
    g_fixture.cueBlock.data[g_fixture.cueBlock.size++] = (uint8_t)((uint16_t)relative >> 8);
    g_fixture.cueBlock.data[g_fixture.cueBlock.size++] = (uint8_t)relative;
    g_fixture.cueBlock.data[g_fixture.cueBlock.size++] = 0;
    memcpy(g_fixture.cueBlock.data + g_fixture.cueBlock.size, text, strlen(text));
    g_fixture.cueBlock.size += strlen(text);
    /* Duration before Block exercises order-independent BlockGroup parsing. */
    Uint(&g_fixture.cueGroup, 0x9b, duration);
    Element(&g_fixture.cueGroup, 0xa1, g_fixture.cueBlock.data, g_fixture.cueBlock.size);
    Element(cluster, 0xa0, g_fixture.cueGroup.data, g_fixture.cueGroup.size);
}

static int WriteFileBytes(const wchar_t* path, const void* data, size_t size)
{
    FILE* file = NULL;
    int ok;
    if (_wfopen_s(&file, path, L"wb") || !file)
        return 0;
    ok = fwrite(data, 1, size, file) == size;
    return fclose(file) == 0 && ok;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Exact-fit output, all shorter capacities and source/output aliasing exercise
 *  the ownership changes independently of file and container dependencies.
 */
static FORCE_INLINE void CheckWorkspace(void)
{
    const char_t          input[]    = "1\r\n00:00:00,000 --> 00:00:01,001\r\nx\r\n";
    const char_t          expected[] = "WEBVTT\n\n1\n00:00:00.000 --> 00:00:01.001\nx\n\n";
    const byte_t          invalid[]  = {0xC0, 0x80};
    uint32_t              capacity;
    byte_t                source[TEST_TEXT_CAPACITY + 2];
    byte_t                output[TEST_TEXT_CAPACITY + 2];
    Subtitles_Workspace_t workspace;
    bool_t                accepted;
    bool_t                fits;
    uint32_t              count;
    const byte_t*         result;
    uint32_t              iteration;

    LOGASSERT(sizeof(expected) <= TEST_TEXT_CAPACITY);
    for (capacity = 1; capacity <= sizeof(expected); ++capacity)
    {
        CRT_Memory_Set(source, TEST_TEXT_GUARD, sizeof(source));
        CRT_Memory_Set(output, TEST_TEXT_GUARD, sizeof(output));
        Subtitles_Initialize(&workspace, source + 1, output + 1, capacity);
        accepted = Subtitles_Convert(&workspace, input, sizeof(input) - 1);
        fits     = (sizeof(expected) == capacity);
        LOGASSERT(accepted == fits);
        LOGASSERT(TEST_TEXT_GUARD == (byte_t)source[0]);
        LOGASSERT(TEST_TEXT_GUARD == (byte_t)source[capacity + 1]);
        LOGASSERT(TEST_TEXT_GUARD == (byte_t)output[0]);
        LOGASSERT(TEST_TEXT_GUARD == (byte_t)output[capacity + 1]);
        count  = Subtitles_GetByteCount(&workspace);
        result = Subtitles_GetText(&workspace);
        if (fits)
        {
            LOGASSERT(sizeof(expected) - 1 == count);
            LOGASSERT(0 == CRT_Memory_Compare(result, expected, sizeof(expected)));
        }
        else
        {
            LOGASSERT(0 == count);
            LOGASSERT(0 == result[0]);
        }
    }
    Subtitles_Initialize(&workspace, source + 1, output + 1, TEST_TEXT_CAPACITY);
    for (iteration = 0; iteration < TEST_REUSE_COUNT; ++iteration)
    {
        CRT_Memory_Copy(workspace.output, input, sizeof(input));
        accepted = Subtitles_Convert(&workspace, workspace.output, sizeof(input) - 1);
        LOGASSERT(accepted);
        LOGASSERT(output + 1 == Subtitles_GetText(&workspace));
        LOGASSERT(source + 1 == workspace.source);
        LOGASSERT(0 == CRT_Memory_Compare(workspace.output, expected, sizeof(expected)));
        accepted = Subtitles_Convert(&workspace, invalid, sizeof(invalid));
        LOGASSERT(!accepted);
        LOGASSERT(0 == Subtitles_GetByteCount(&workspace));
        LOGASSERT(0 == workspace.output[0]);
    }
}

/*************************************************************************************************/
/**
 * \brief Checks timestamp boundaries and malformed SubRip input.
 *
 * \par Requirements:
 *  - 001 Accepted inputs shall publish complete WebVTT and rejected inputs an empty result.
 *  - 002 Every conversion shall reuse the initialized workspace and invalidate failed output.
 *
 * \retval None   No result value; failed checks assert.
 */
static FORCE_INLINE void CheckTimestamps(void);

/*************************************************************************************************/
/**
 * \par Details:
 *  Exercises counted timestamp parsing through the production converter,
 *  including oversized components and truncation at each separator.
 */
static FORCE_INLINE void CheckTimestamps(void)
{
    const char_t* invalid[] = {"1\n00:60:00,000 --> 00:61:00,000\nx\n",
                               "1\n00:00:60,000 --> 00:02:00,000\nx\n",
                               "1\n10000:00:00,000 --> 10001:00:00,000\nx\n",
                               "1\n999999999999999999999999999999:00:00,000 --> 01:00:00,000\nx\n",
                               "1\n00:00:00,0000 --> 00:00:01,000\nx\n",
                               "1\n00:00:00,00 --> 00:00:01,000\nx\n",
                               "1\n00:0:00,000 --> 00:00:01,000\nx\n",
                               "1\n00:00:0,000 --> 00:00:01,000\nx\n",
                               "1\n00: --> 00:00:01,000\nx\n",
                               "1\n00:00: --> 00:00:01,000\nx\n",
                               "1\n00:00:00, --> 00:00:01,000\nx\n",
                               "1\n00:00:00,000 --> \nx\n",
                               "1\n00:00:00,000 --> 00:00:00,000\nx\n"};
    const char_t* valid[]   = {"1\n00:00:00,000 --> 00:00:01,001\nx\n",
                               "1\n00:00:00.000 --> 00:00:01.001\nx\n",
                               "1\n9999:59:58,999 --> 9999:59:59,999\nx\n",
                               "1\n00:00:00,000 --> 00:00:01,001 X1:1 X2:2 Y1:3 Y2:4\nx\n"};
    uint32_t index;
    uint64_t size;
    bool_t   accepted;

    for (index = 0; index < sizeof(invalid) / sizeof(const char_t*); ++index)
    {
        accepted = Subtitles_Convert(&g_fixture.subtitles, invalid[index], strlen(invalid[index]));
        size     = Subtitles_GetByteCount(&g_fixture.subtitles);
        LOGASSERT(!accepted);
        LOGASSERT(0 == size);
    }
    for (index = 0; index < sizeof(valid) / sizeof(const char_t*); ++index)
    {
        accepted = Subtitles_Convert(&g_fixture.subtitles, valid[index], strlen(valid[index]));
        size     = Subtitles_GetByteCount(&g_fixture.subtitles);
        LOGASSERT(accepted);
        LOGASSERT(0 < size);
    }
}

/*************************************************************************************************/
/**
 * \brief Rejects the first valid cue to exercise consumer cancellation.
 *
 * \param[in,out] user    Writable callback count
 * \param[in]     start   Nonnegative cue start
 * \param[in]     end     Cue end after start
 * \param[in]     text    Readable cue payload
 * \param[in]     size    Nonzero payload count
 *
 * \retval        FALSE   Stop traversal immediately.
 */
static FORCE_INLINE bool_t RejectCue(void* user, double start, double end, const byte_t* text, uint64_t size);

/*************************************************************************************************/
/**
 * \par Details:
 *  The next full read must still work after the consumer rejects this cue.
 */
static FORCE_INLINE bool_t RejectCue(void* user, double start, double end, const byte_t* text, uint64_t size)
{
    uint32_t* count;

    LOGASSERT(NULL != user);
    LOGASSERT(0.0 <= start);
    LOGASSERT(start < end);
    LOGASSERT(NULL != text);
    LOGASSERT(0 < size);

    count = (uint32_t*)user;
    ++(*count);

    return FALSE;
}

/*************************************************************************************************/
/**
 * \brief Accepts and verifies the cue selected beyond the former track-array limit.
 *
 * \par Requirements:
 *
 * \param[in,out] user    Required callback count
 * \param[in]     start   Nonnegative cue start
 * \param[in]     end     End after start
 * \param[in]     text    Borrowed cue bytes
 * \param[in]     size    Cue byte count
 *
 * \retval        TRUE   The independently checked cue is accepted.
 */
static FORCE_INLINE bool_t AcceptLateCue(void* user, double start, double end, const byte_t* text, uint64_t size);

/*************************************************************************************************/
/**
 * \brief Checks extraction of the sixty-fifth supported subtitle track.
 *
 * \par Requirements:
 *  - 001 The fixture shall reuse startup storage and the existing temporary path.
 *
 * \param[in] path   Required temporary file path owned by the fixture
 *
 * \retval    None   No result value; a fixture mismatch asserts.
 */
static FORCE_INLINE void CheckLateTrack(const TCHAR* path);

/*************************************************************************************************/
/**
 * \par Details:
 *  Both payload and timing are checked independently of track metadata traversal.
 */
static FORCE_INLINE bool_t AcceptLateCue(void* user, double start, double end, const byte_t* text, uint64_t size)
{
    uint32_t* count;

    LOGASSERT(NULL != user);
    LOGASSERT(0.0 <= start);
    LOGASSERT(start < end);
    LOGASSERT(sizeof("late") - 1 == size);
    LOGASSERT(0 == CRT_Memory_Compare(text, "late", size));

    count = (uint32_t*)user;
    ++(*count);

    return TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The previous extraction path collected at most 64 records before selecting a track.
 *  This fixture requires the next record and then checks rejection of a missing number.
 */
static FORCE_INLINE void CheckLateTrack(const TCHAR* path)
{
    uint32_t index;
    bool_t   written;
    uint32_t callbacks;
    bool_t   read;

    LOGASSERT(NULL != path);

    g_fixture.file.size    = 0;
    g_fixture.segment.size = 0;
    g_fixture.tracks.size  = 0;
    g_fixture.cluster.size = 0;
    for (index = 1; index <= TEST_LATE_TRACK_NUMBER; ++index)
    {
        Track(&g_fixture.tracks, index, "S_TEXT/UTF8", "eng", 0);
    }
    Element(&g_fixture.segment, UTL_MATROSKA_ID_TRACKS, g_fixture.tracks.data, g_fixture.tracks.size);
    Uint(&g_fixture.cluster, UTL_MATROSKA_ID_CLUSTER_TIMESTAMP, 0);
    Cue(&g_fixture.cluster, TEST_LATE_TRACK_NUMBER, 1, 1, "late");
    Element(&g_fixture.segment, UTL_MATROSKA_ID_CLUSTER, g_fixture.cluster.data, g_fixture.cluster.size);
    Element(&g_fixture.file, UTL_MATROSKA_ID_EBML, NULL, 0);
    Element(&g_fixture.file, UTL_MATROSKA_ID_SEGMENT, g_fixture.segment.data, g_fixture.segment.size);
    written = WriteFileBytes(path, g_fixture.file.data, g_fixture.file.size);
    LOGASSERT(written);
    callbacks = 0;
    read      = MkvSubtitles_Read(path, TEST_LATE_TRACK_NUMBER, AcceptLateCue, &callbacks);
    LOGASSERT(read);
    LOGASSERT(1 == callbacks);
    callbacks = 0;
    read      = MkvSubtitles_Read(path, TEST_LATE_TRACK_NUMBER + 1, AcceptLateCue, &callbacks);
    LOGASSERT(!read);
    LOGASSERT(0 == callbacks);
}

static int FixtureTests(void)
{
    Buffer*        file;
    Buffer*        segment;
    Buffer*        tracks;
    Buffer*        info;
    Buffer*        cluster;
    SubtitleTrack* found;
    wchar_t          directory[MAX_PATH], path[MAX_PATH];
    char             error[256];
    const byte_t*    vtt;
    uint64_t         size;
    unsigned         count;
    MkvSubtitleTrack single;
    uint32_t         callbacks;
    SubtitleTrack    sidecar;
    const char_t     sidecar_text[]     = "WEBVTT\r\n\r\n00:00:00.000 --> 00:00:01.000\r\nsidecar\r\n";
    const char_t     sidecar_expected[] = "WEBVTT\n\n00:00:00.000 --> 00:00:01.000\nsidecar\n";

    file          = &g_fixture.file;
    file->size    = 0;
    segment       = &g_fixture.segment;
    segment->size = 0;
    tracks        = &g_fixture.tracks;
    tracks->size  = 0;
    info          = &g_fixture.info;
    info->size    = 0;
    cluster       = &g_fixture.cluster;
    cluster->size = 0;
    found         = g_fixture.found;

    CheckTimestamps();
    CheckWorkspace();
    CHECK(GetTempPathW(MAX_PATH, directory));
    CHECK(GetTempFileNameW(directory, L"cvs", 0, path));
    Uint(info, 0x2ad7b1, 2000000); /* non-default 2 ms Segment ticks */
    Element(segment, 0x1549a966, info->data, info->size);
    Track(tracks, 4, "S_TEXT/UTF8", "eng", 0);
    Track(tracks, 5, "S_HDMV/PGS", "eng", 0);
    Track(tracks, 130, "S_TEXT/UTF8", "dan", 1);
    Element(segment, 0x1654ae6b, tracks->data, tracks->size);
    Uint(cluster, 0xe7, 1000);
    Cue(cluster, 4, -250, 500, "<i>Hello</i>\r\nSecond line");
    Cue(cluster, 130, -250, 500, "Bl\xc3\xa5");
    /* Unknown-sized clusters terminate at the next Segment-level element. */
    memcpy(segment->data + segment->size, "\x1f\x43\xb6\x75\xff", 5);
    segment->size += 5;
    memcpy(segment->data + segment->size, cluster->data, cluster->size);
    segment->size += cluster->size;
    cluster->size  = 0;
    Uint(cluster, 0xe7, 3000);
    Cue(cluster, 4, 0, 1000, "Later");
    Element(segment, 0x1f43b675, cluster->data, cluster->size);
    Element(file, 0x1a45dfa3, NULL, 0);
    Element(file, 0x18538067, segment->data, segment->size);
    CHECK(WriteFileBytes(path, file->data, file->size));
    count = Subtitles_Find(path, found);
    CHECK(count == 2);
    CHECK(found[0].matroskaTrack == 4 && !strcmp(found[0].language, "eng"));
    CHECK(found[1].matroskaTrack == 130 && !strcmp(found[1].language, "da-DK"));
    CHECK(MkvSubtitles_Find(path, &single, 1) == 1);
    CHECK(single.number == 4);
    CHECK(MkvSubtitles_Find(path, NULL, 0) == 0);
    callbacks = 0;
    CHECK(!MkvSubtitles_Read(path, 4, RejectCue, &callbacks));
    CHECK(callbacks == 1);
    CHECK(Subtitles_Load(&g_fixture.subtitles, &found[0], error, sizeof(error)));
    vtt  = Subtitles_GetText(&g_fixture.subtitles);
    size = Subtitles_GetByteCount(&g_fixture.subtitles);
    CHECK(vtt && size == strlen((const char_t*)vtt));
    CHECK(!strcmp((const char_t*)vtt,
                  "WEBVTT\n\n00:00:01.400 --> 00:00:02.400\n<i>Hello</i>\nSecond line\n\n00:00:05.900 --> "
                  "00:00:07.900\nLater\n\n"));
    CHECK(Subtitles_Load(&g_fixture.subtitles, &found[1], error, sizeof(error)));
    vtt = Subtitles_GetText(&g_fixture.subtitles);
    CHECK(vtt && strstr((const char_t*)vtt, "Bl\xc3\xa5"));
    found[1].matroskaTrack = 999;
    CHECK(!Subtitles_Load(&g_fixture.subtitles, &found[1], error, sizeof(error)));
    CHECK(0 == Subtitles_GetByteCount(&g_fixture.subtitles));
    CHECK(WriteFileBytes(path, file->data, file->size - 1));
    CHECK(!Subtitles_Load(&g_fixture.subtitles, &found[0], error, sizeof(error)));
    CHECK(0 == Subtitles_GetByteCount(&g_fixture.subtitles));
    file->data[0] = 0;
    CHECK(WriteFileBytes(path, file->data, file->size));
    CHECK(Subtitles_Find(path, found) == 0);
    CheckLateTrack(path);
    Subtitles_InitializeFileTrack(path, &sidecar);
    CHECK(WriteFileBytes(path, sidecar_text, sizeof(sidecar_text) - 1));
    CHECK(Subtitles_Load(&g_fixture.subtitles, &sidecar, error, sizeof(error)));
    CHECK(sizeof(sidecar_expected) - 1 == Subtitles_GetByteCount(&g_fixture.subtitles));
    CHECK(0 == CRT_Memory_Compare(Subtitles_GetText(&g_fixture.subtitles), sidecar_expected, sizeof(sidecar_expected)));
    CHECK(WriteFileBytes(path, "", 0));
    CHECK(!Subtitles_Load(&g_fixture.subtitles, &sidecar, error, 1));
    CHECK(0 == error[0]);
    CHECK(0 == Subtitles_GetByteCount(&g_fixture.subtitles));
    CHECK(0 == Subtitles_GetText(&g_fixture.subtitles)[0]);
    CHECK(WriteFileBytes(path, sidecar_text, sizeof(sidecar_text) - 1));
    CHECK(Subtitles_Load(&g_fixture.subtitles, &sidecar, error, sizeof(error)));
    CHECK(DeleteFileW(path));
    puts("subtitle fixture checks passed");
    return 0;
}

int wmain(int argc, wchar_t** argv)
{
    SubtitleTrack* tracks = g_fixture.found;
    unsigned count;
    Subtitles_Initialize(&g_fixture.subtitles,
                         g_fixture.subtitleScratch,
                         g_fixture.subtitleOutput,
                         SUBTITLE_TEXT_CAPACITY);
    if (argc == 1)
        return FixtureTests();
    if (argc != 3)
    {
        fprintf(stderr, "usage: cvk-cast-subtitle-test [video output-directory]\n");
        return 1;
    }
    count = Subtitles_Find(argv[1], tracks);
    CHECK(count > 0);
    CHECK(CreateDirectoryW(argv[2], NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
    printf("detected %u subtitle tracks\n", count);
    for (unsigned i = 0; i < count; ++i)
    {
        char          error[256];
        const byte_t* vtt;
        const char_t* cursor;
        wchar_t       output[MAX_PATH];
        uint64_t      size;
        unsigned      cues  = 0;
        ULONGLONG     begin = GetTickCount64();
        if (!Subtitles_Load(&g_fixture.subtitles, &tracks[i], error, sizeof(error)))
        {
            fprintf(stderr, "track %u: %s\n", i, error);
            return 1;
        }
        vtt  = Subtitles_GetText(&g_fixture.subtitles);
        size = Subtitles_GetByteCount(&g_fixture.subtitles);
        for (cursor = (const char_t*)vtt; (cursor = strstr(cursor, " --> ")) != NULL; cursor += 5)
            ++cues;
        CHECK(cues > 0);
        CHECK(_snwprintf_s(output,
                           MAX_PATH,
                           _TRUNCATE,
                           L"%ls\\track-%llu.vtt",
                           argv[2],
                           (unsigned long long)tracks[i].matroskaTrack) >= 0);
        CHECK(WriteFileBytes(output, vtt, size));
        printf("track %llu (%s): %u cues, %llu bytes, %llu ms\n",
               (unsigned long long)tracks[i].matroskaTrack,
               tracks[i].language,
               cues,
               (unsigned long long)size,
               GetTickCount64() - begin);
    }
    return 0;
}
