/* Matroska UTF-8/SubRip subtitle tracks; no video or audio decoding is required. */
#ifndef CAST_MKV_SUBTITLES_H
#define CAST_MKV_SUBTITLES_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef struct MkvSubtitleTrack
{
    uint64_t number;
    char language[16];
    char name[256];
    double timestampScale;
    uint64_t codecDelay;
    uint64_t defaultDuration;
} MkvSubtitleTrack;

typedef int (*MkvSubtitleProc)(void* user, double start, double end, const uint8_t* text, size_t size);

/* Reads track metadata without scanning the movie's clusters. Unsupported image,
** compressed and encrypted subtitle codecs are excluded from the returned list. */
unsigned MkvSubtitles_Find(const wchar_t* path, MkvSubtitleTrack* tracks, unsigned capacity);
/* Visits every cue in the selected track. Returns 0 on malformed/unsupported input;
** callers must discard partial output. Times are seconds on the movie timeline. */
int MkvSubtitles_Read(const wchar_t* path, uint64_t trackNumber, MkvSubtitleProc callback, void* user);
#endif
