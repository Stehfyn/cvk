#ifndef CAST_AUDIO_GAIN_H
#define CAST_AUDIO_GAIN_H
#include <stddef.h>
#include <stdint.h>
#include <tchar.h>

/* Saturating PCM gain, percent 0..400. */
void AudioGain_Apply(int16_t* samples, size_t count, unsigned percent);
/* 1 for audio requiring AAC conversion, 0 for AAC/MP3 or no audio, -1 on probe failure. */
int AudioGain_NeedsConversion(const TCHAR* source, char* error, size_t errorCapacity);
/* Copies compressed video and re-encodes audio using Windows Media Foundation.
** Runs on a worker; progress and cancel are interlocked LONGs. The caller owns dest.
** Preserves mono/stereo/5.1; source must have video and audio supported by Windows. */
int AudioGain_Convert(const TCHAR* source, const TCHAR* dest, unsigned percent, volatile long* cancel, volatile long* progress, char* error, size_t errorCapacity);
#endif
