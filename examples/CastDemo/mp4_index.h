/* mp4_index.h - the keyframe times of an MP4/MOV file, read from its sample tables.
**
** A seek to an arbitrary time makes the receiver decode every frame since the previous
** keyframe; with a typical 250-frame group of pictures that is seconds of work. Snapping
** scrub previews to the keyframe before the target makes them a one-frame decode. */
#ifndef CAST_MP4_INDEX_H
#define CAST_MP4_INDEX_H

#include <wchar.h>

#define MP4_INDEX_MAX_KEYFRAMES (65536)

/* Fills pTimes (seconds, ascending) with the sync samples of the first video track.
** Returns the count, or 0 when the file is not an MP4 or has no sync sample table. */
unsigned Mp4Index_ReadKeyframes(const wchar_t* pFile, double* pTimes, unsigned capacity);

/* The largest keyframe time not after seconds, or seconds itself when none qualifies. */
double Mp4Index_SnapToKeyframe(const double* pTimes, unsigned count, double seconds);

#endif
