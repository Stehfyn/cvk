#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "subtitles.h"
#include "mp4_demux.h"
#include "mkv_subtitles.h"

#define MAX_SUBTITLE_BYTES (16u * 1024u * 1024u)
typedef struct Text { char* data; size_t size, capacity; } Text;

static int Append(Text* text, const char* data, size_t size)
{
    if (size > MAX_SUBTITLE_BYTES - text->size) return 0;
    if (text->size + size + 1 > text->capacity)
    {
        size_t capacity = (text->size + size + 1) * 2;
        char* next = (char*)realloc(text->data, capacity);
        if (!next) return 0;
        text->data = next;
        text->capacity = capacity;
    }
    memcpy(text->data + text->size, data, size);
    text->size += size;
    text->data[text->size] = 0;
    return 1;
}

/* UTF-8 (optional BOM) and UTF-16 with BOM; never silently corrupt non-ASCII captions. */
static char* Utf8(const uint8_t* data, size_t size)
{
    char* result;
    if (size > MAX_SUBTITLE_BYTES) return NULL;
    if (size >= 2 && ((data[0] == 0xff && data[1] == 0xfe) || (data[0] == 0xfe && data[1] == 0xff)))
    {
        size_t i, count = (size - 2) / 2;
        wchar_t* wide;
        int bytes;
        if (size % 2) return NULL;
        wide = (wchar_t*)malloc((count + 1) * sizeof(wchar_t));
        if (!wide) return NULL;
        for (i = 0; i < count; ++i)
            wide[i] = data[0] == 0xff ? (wchar_t)(data[2+i*2] | data[3+i*2] << 8) : (wchar_t)(data[2+i*2] << 8 | data[3+i*2]);
        bytes = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, (int)count, NULL, 0, NULL, NULL);
        result = bytes > 0 ? (char*)malloc((size_t)bytes + 1) : NULL;
        if (result)
        {
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, (int)count, result, bytes, NULL, NULL);
            result[bytes] = 0;
            if (memchr(result, 0, bytes)) { free(result); result = NULL; }
        }
        free(wide);
        return result;
    }
    if (size >= 3 && !memcmp(data, "\xef\xbb\xbf", 3)) { data += 3; size -= 3; }
    if (!size || memchr(data, 0, size) || !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, (const char*)data, (int)size, NULL, 0)) return NULL;
    result = (char*)malloc(size + 1);
    if (result) { memcpy(result, data, size); result[size] = 0; }
    return result;
}

static int Timestamp(const char* text, uint64_t* millis, int* length)
{
    unsigned h, m, s, ms;
    char separator;
    int n = 0;
    if (sscanf(text, "%u:%u:%u%c%3u%n", &h, &m, &s, &separator, &ms, &n) != 5 ||
        (separator != ',' && separator != '.') || m > 59 || s > 59 || ms > 999 || h > 9999 || n < 12 ||
        text[n-4] != separator || (text[n] >= '0' && text[n] <= '9')) return 0;
    *millis = (((uint64_t)h * 60 + m) * 60 + s) * 1000 + ms;
    *length = n;
    return 1;
}

char* Subtitles_Convert(const void* bytes, size_t size, size_t* outputSize)
{
    char* source = Utf8((const uint8_t*)bytes, size);
    Text out = {0};
    char *read, *write, *line;
    int blockLine = 0, cues = 0, timed = 0;
    *outputSize = 0;
    if (!source) return NULL;
    /* Normalize CRLF and CR. */
    for (read = write = source; *read; ++read)
    {
        if (*read == '\r') { *write++ = '\n'; if (read[1] == '\n') ++read; }
        else *write++ = *read;
    }
    *write = 0;
    if (!strncmp(source, "WEBVTT", 6) && (source[6] == '\n' || source[6] == ' ' || source[6] == '\t' || !source[6]))
    {
        *outputSize = strlen(source);
        return source;
    }
    if (!Append(&out, "WEBVTT\n\n", 8)) goto fail;
    for (line = source; *line; )
    {
        char* end = strchr(line, '\n');
        size_t length = end ? (size_t)(end - line) : strlen(line);
        char* arrow;
        if (end) *end = 0;
        if (!length)
        {
            if (blockLine && !timed) goto fail;
            blockLine = 0; timed = 0;
        }
        else if (!timed && (arrow = strstr(line, " --> ")) != NULL)
        {
            uint64_t start, stop;
            int first, last;
            if (blockLine > 1 || !Timestamp(line, &start, &first) || line + first != arrow ||
                !Timestamp(arrow + 5, &stop, &last) || stop <= start) goto fail;
            /* SRT coordinate extensions are not WebVTT cue settings. */
            length = (size_t)(arrow + 5 + last - line);
            if (line[first-4] == ',') line[first-4] = '.';
            if (arrow[5+last-4] == ',') arrow[5+last-4] = '.';
            timed = 1; ++cues;
        }
        else if (!timed && blockLine > 0) goto fail;
        if (!Append(&out, line, length) || !Append(&out, "\n", 1)) goto fail;
        if (length) ++blockLine;
        line = end ? end + 1 : line + strlen(line);
    }
    if (!cues || (blockLine && !timed)) goto fail;
    if (!Append(&out, "\n", 1)) goto fail;
    free(source);
    *outputSize = out.size;
    return out.data;
fail:
    free(source); free(out.data); return NULL;
}

void Subtitles_FileTrack(const wchar_t* path, SubtitleTrack* track)
{
    const wchar_t* name = wcsrchr(path, L'\\');
    const wchar_t* dot;
    wchar_t stem[260];
    memset(track, 0, sizeof(*track));
    track->embedded = -1;
    wcsncpy_s(track->path, 260, path, _TRUNCATE);
    name = name ? name + 1 : path;
    wcsncpy_s(track->label, 320, name, _TRUNCATE);
    wcsncpy_s(stem, 260, name, _TRUNCATE);
    dot = wcsrchr(stem, L'.');
    if (dot) stem[dot - stem] = 0;
    name = wcsrchr(stem, L'.'); name = name ? name + 1 : stem;
    strcpy_s(track->language, 16, "und");
    if (wcslen(name) == 2 || wcslen(name) == 3)
    {
        unsigned i;
        for (i = 0; name[i] >= L'a' && name[i] <= L'z'; ++i) {}
        if (!name[i]) WideCharToMultiByte(CP_UTF8, 0, name, -1, track->language, 16, NULL, NULL);
    }
}

static unsigned FindFiles(const wchar_t* directory, const wchar_t* stem, SubtitleTrack* tracks, unsigned count)
{
    WIN32_FIND_DATAW entry;
    wchar_t pattern[260], path[260];
    HANDLE find;
    if (_snwprintf_s(pattern, 260, _TRUNCATE, L"%ls*", directory) < 0) return count;
    find = FindFirstFileW(pattern, &entry);
    if (find == INVALID_HANDLE_VALUE) return count;
    do
    {
        const wchar_t* ext = wcsrchr(entry.cFileName, L'.');
        size_t stemLength = stem ? wcslen(stem) : 0;
        if (count >= SUBTITLE_MAX_TRACKS) break;
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !ext || (_wcsicmp(ext,L".srt") && _wcsicmp(ext,L".vtt"))) continue;
        if (stem && (_wcsnicmp(entry.cFileName, stem, stemLength) || entry.cFileName[stemLength] != '.')) continue;
        if (_snwprintf_s(path, 260, _TRUNCATE, L"%ls%ls", directory, entry.cFileName) < 0) continue;
        Subtitles_FileTrack(path, &tracks[count++]);
    } while (FindNextFileW(find, &entry));
    FindClose(find);
    return count;
}

unsigned Subtitles_Find(const wchar_t* media, SubtitleTrack tracks[SUBTITLE_MAX_TRACKS])
{
    wchar_t directory[260], stem[260], subs[260];
    wchar_t* slash;
    wchar_t* dot;
    unsigned count = 0, i;
    DWORD pathLength;
    if (!media || !*media || wcslen(media) >= 260) return 0;
    pathLength = GetFullPathNameW(media, 260, directory, NULL);
    if (!pathLength || pathLength >= 260) return 0;
    slash = wcsrchr(directory, L'\\');
    if (!slash) return 0;
    wcscpy_s(stem, 260, slash + 1); slash[1] = 0;
    dot = wcsrchr(stem, L'.'); if (dot) *dot = 0;
    count = FindFiles(directory, stem, tracks, count);
    if (_snwprintf_s(subs, 260, _TRUNCATE, L"%lsSubs\\", directory) >= 0) count = FindFiles(subs, NULL, tracks, count);
    for (i = 0; count < SUBTITLE_MAX_TRACKS; ++i)
    {
        char language[4];
        if (!Mp4Demux_ReadTextTrack(media, i, language, NULL, NULL)) break;
        Subtitles_FileTrack(media, &tracks[count]);
        tracks[count].embedded = (int)i;
        strcpy_s(tracks[count].language, 16, language);
        _snwprintf_s(tracks[count].label, 320, _TRUNCATE, L"Embedded %u (%hs)", i + 1, language);
        ++count;
    }
    {
        MkvSubtitleTrack mkv[SUBTITLE_MAX_TRACKS];
        unsigned total = MkvSubtitles_Find(media, mkv, SUBTITLE_MAX_TRACKS - count);
        for (i = 0; i < total; ++i)
        {
            wchar_t name[256] = {0};
            Subtitles_FileTrack(media, &tracks[count]);
            tracks[count].matroskaTrack = mkv[i].number;
            strcpy_s(tracks[count].language, 16, mkv[i].language);
            MultiByteToWideChar(CP_UTF8, 0, mkv[i].name, -1, name, 256);
            _snwprintf_s(tracks[count].label, 320, _TRUNCATE, L"MKV track %llu (%hs)%ls%ls", (unsigned long long)mkv[i].number,
                         mkv[i].language, name[0] ? L" - " : L"", name);
            ++count;
        }
    }
    return count;
}

static int MatroskaCue(void* user, double start, double end, const uint8_t* data, size_t size)
{
    Text* out = (Text*)user;
    char timing[100];
    char* text = Utf8(data, size);
    uint64_t a = (uint64_t)(start * 1000 + .5), b = (uint64_t)(end * 1000 + .5);
    int previousNewline = 1;
    if (!text) return 0;
    if (b <= a) { free(text); return 1; }
    snprintf(timing, sizeof(timing), "%02llu:%02llu:%02llu.%03llu --> %02llu:%02llu:%02llu.%03llu\n",
             a/3600000, a/60000%60, a/1000%60, a%1000, b/3600000, b/60000%60, b/1000%60, b%1000);
    if (!Append(out, timing, strlen(timing))) { free(text); return 0; }
    /* Matroska SubRip stores just UTF-8 cue text. Preserve its inline markup and
    ** normalize line endings; blank payload lines must not terminate a WebVTT cue. */
    for (char* p = text; *p; ++p)
    {
        char c = *p;
        if (c == '\r') { if (p[1] == '\n') ++p; c = '\n'; }
        if (c == '\n' && previousNewline && !Append(out, " ", 1)) { free(text); return 0; }
        if (!Append(out, &c, 1)) { free(text); return 0; }
        previousNewline = c == '\n';
    }
    free(text);
    return Append(out, previousNewline ? "\n" : "\n\n", previousNewline ? 1 : 2);
}

static int TextSample(void* user, double start, double end, const uint8_t* data, uint32_t size)
{
    Text* out = (Text*)user;
    char timing[100];
    char* text;
    uint32_t length;
    uint64_t a = (uint64_t)(start * 1000 + .5), b = (uint64_t)(end * 1000 + .5);
    if (size < 2) return 0;
    length = (uint32_t)data[0] * 256 + data[1];
    if (length > size - 2) return 0;
    if (!length) return 1;
    text = Utf8(data + 2, length);
    if (!text) return 0;
    snprintf(timing, sizeof(timing), "%02llu:%02llu:%02llu.%03llu --> %02llu:%02llu:%02llu.%03llu\n",
             a/3600000, a/60000%60, a/1000%60, a%1000, b/3600000, b/60000%60, b/1000%60, b%1000);
    if (!Append(out, timing, strlen(timing))) { free(text); return 0; }
    /* tx3g is plain text: escape markup characters. Empty lines cannot split a cue. */
    for (char* p = text; *p; ++p)
    {
        const char* value = *p == '&' ? "&amp;" : *p == '<' ? "&lt;" : *p == '>' ? "&gt;" : NULL;
        if (*p == '\r') { if (p[1] == '\n') continue; *p = '\n'; }
        if (*p == '\n' && (p == text || p[-1] == '\n' || !p[1])) value = " ";
        if (!Append(out, value ? value : p, value ? strlen(value) : 1)) { free(text); return 0; }
    }
    free(text);
    return Append(out, "\n\n", 2);
}

char* Subtitles_Load(const SubtitleTrack* track, size_t* size, char* error, size_t errorCapacity)
{
    char* result = NULL;
    *size = 0;
    if (track->matroskaTrack)
    {
        Text out = {0};
        if (Append(&out, "WEBVTT\n\n", 8) && MkvSubtitles_Read(track->path, track->matroskaTrack, MatroskaCue, &out))
        { *size = out.size; result = out.data; }
        else free(out.data);
    }
    else if (track->embedded >= 0)
    {
        Text out = {0};
        char language[4];
        if (Append(&out, "WEBVTT\n\n", 8) && Mp4Demux_ReadTextTrack(track->path, (unsigned)track->embedded, language, TextSample, &out))
        { *size = out.size; result = out.data; }
        else free(out.data);
    }
    else
    {
        FILE* file = NULL;
        if (!_wfopen_s(&file, track->path, L"rb") && file)
        {
            long length;
            fseek(file, 0, SEEK_END); length = ftell(file); rewind(file);
            if (length > 0 && length <= MAX_SUBTITLE_BYTES)
            {
                char* data = (char*)malloc((size_t)length);
                if (data && fread(data, 1, (size_t)length, file) == (size_t)length) result = Subtitles_Convert(data, (size_t)length, size);
                free(data);
            }
            fclose(file);
        }
    }
    if (!result) snprintf(error, errorCapacity, "Cannot read subtitles: use UTF-8/UTF-16 SRT or WebVTT, an MP4 tx3g track, or an MKV UTF-8/SubRip text track.");
    return result;
}
