/* mp4_mux.h - a minimal progressive MP4 muxer for the GPU proxy.
**
** Writes a plain MP4 with one H.264 video track (avc1 + avcC from the encoder's SPS/PPS) and,
** optionally, one AAC audio track (mp4a + esds) passed straight through from the source. Samples
** stream into the mdat as they are added; the moov (with the sample tables) is written on finish.
** Video is fed as Annex-B and rewritten to the length-prefixed form MP4 uses. */
#ifndef CAST_MP4_MUX_H
#define CAST_MP4_MUX_H

#include "os/os_types.h"
#include <stdint.h>
#include <tchar.h>

typedef struct Mp4Mux Mp4Mux;

/* Opens pPath and begins the file. sps/pps are the encoder's parameter sets (Annex-B or raw NAL,
** without start codes). hasAudio adds an AAC track described by asc (AudioSpecificConfig). Returns
** NULL on failure. */
Mp4Mux* Mp4Mux_Create(const TCHAR* pPath,
                      unsigned       width,
                      unsigned       height,
                      const byte_t* pSps,
                      uint32_t       spsSize,
                      const byte_t* pPps,
                      uint32_t       ppsSize,
                      unsigned       videoTimescale,
                      int            hasAudio,
                      unsigned       audioTimescale,
                      unsigned       audioSampleRate,
                      unsigned       audioChannels,
                      const byte_t* pAsc,
                      uint32_t       ascSize);

/* Opens pPath as a fragmented MP4: an init segment (moov with mvex) is written now, and moof+mdat
** fragments are appended by Mp4Mux_FlushFragment as they are produced, so the file is playable and
** seekable within its produced range almost immediately. fullDurationSeconds is the whole timeline
** (from the source) so the receiver shows the complete seek bar. Returns NULL on failure. */
Mp4Mux* Mp4Mux_CreateFragmented(const TCHAR* pPath,
                                unsigned       width,
                                unsigned       height,
                                const byte_t* pSps,
                                uint32_t       spsSize,
                                const byte_t* pPps,
                                uint32_t       ppsSize,
                                unsigned       videoTimescale,
                                int            hasAudio,
                                unsigned       audioTimescale,
                                unsigned       audioSampleRate,
                                unsigned       audioChannels,
                                const byte_t* pAsc,
                                uint32_t       ascSize,
                                double         fullDurationSeconds);

/* Writes a movie fragment for the samples added since the last flush and flushes it to disk.
** Returns 1 if a fragment was written, 0 if there was nothing pending. */
int Mp4Mux_FlushFragment(Mp4Mux* pMux);

/* Appends one coded video frame (Annex-B) with its duration in the video timescale. Returns 1 on
** success. */
int Mp4Mux_AddVideo(Mp4Mux* pMux, const byte_t* pData, uint32_t size, uint32_t duration, int keyframe);

/* Appends one AAC access unit with its duration in the audio timescale. Returns 1 on success. */
int Mp4Mux_AddAudio(Mp4Mux* pMux, const byte_t* pData, uint32_t size, uint32_t duration);

/* Writes the moov and closes the file. Returns 1 on success. */
int Mp4Mux_Finish(Mp4Mux* pMux);

/* Closes and frees without requiring a successful finish (for the error path). */
void Mp4Mux_Destroy(Mp4Mux* pMux);

#endif
