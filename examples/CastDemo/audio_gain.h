#ifndef CAST_AUDIO_GAIN_H
#define CAST_AUDIO_GAIN_H
#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

/* Saturating PCM gain, percent 0..400. */
void AudioGain_Apply(int16_t* samples, size_t count, unsigned percent);
/* Copies compressed video and re-encodes audio using Windows Media Foundation.
** Runs on a worker; progress and cancel are interlocked LONGs. The caller owns dest.
** Preserves mono/stereo/5.1; source must have video and audio supported by Windows. */
int AudioGain_Convert(const wchar_t* source, const wchar_t* dest, unsigned percent,
                      volatile long* cancel, volatile long* progress, char* error, size_t errorCapacity);
#endif
