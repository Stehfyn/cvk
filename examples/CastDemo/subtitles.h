#ifndef CAST_SUBTITLES_H
#define CAST_SUBTITLES_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

#define SUBTITLE_MAX_TRACKS 32
typedef struct SubtitleTrack
{
    wchar_t path[260];
    wchar_t label[320];
    char language[16];
    int embedded; /* -1: sidecar; otherwise zero-based MP4 text track */
    uint64_t matroskaTrack; /* nonzero: embedded Matroska UTF-8 track number */
} SubtitleTrack;

/* Sidecars beside the video, in Subs/, then embedded MP4 tx3g / Matroska UTF-8 tracks. */
unsigned Subtitles_Find(const wchar_t* media, SubtitleTrack tracks[SUBTITLE_MAX_TRACKS]);
void Subtitles_FileTrack(const wchar_t* path, SubtitleTrack* track);
/* Returns allocated UTF-8 WebVTT; caller frees it. NULL means invalid/unsupported input. */
char* Subtitles_Load(const SubtitleTrack* track, size_t* size, char* error, size_t errorCapacity);
char* Subtitles_Convert(const void* data, size_t size, size_t* outputSize);
#endif
