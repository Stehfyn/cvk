#ifndef CAST_MKV_SUBTITLES_H
#define CAST_MKV_SUBTITLES_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * RFC 9559, Matroska UTF-8 subtitle tracks.
 * This application adapter still maps a file per operation; it does not yet meet
 * the initialization-only resource requirement.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <tchar.h>
#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define MKV_SUBTITLE_LANGUAGE_CAPACITY (16)  ///< Terminated language metadata bytes.
#define MKV_SUBTITLE_NAME_CAPACITY     (256) ///< Terminated track-name metadata bytes.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Supported text-track metadata copied out of the input file.
 */
typedef struct MkvSubtitleTrack
{
    uint64_t  number;                                   ///< Nonzero TrackNumber.
    char_t    language[MKV_SUBTITLE_LANGUAGE_CAPACITY]; ///< BCP 47 language when present, otherwise Language.
    char_t    name[MKV_SUBTITLE_NAME_CAPACITY];         ///< Bounded UTF-8 track name.
    float64_t timestampScale;                           ///< Positive TrackTimestampScale.
    uint64_t  codecDelay;                               ///< CodecDelay in nanoseconds.
    uint64_t  defaultDuration;                          ///< DefaultDuration in nanoseconds; zero when absent.
} MkvSubtitleTrack;

/*************************************************************************************************/
/**
 * \brief Receives a borrowed UTF-8 cue span on the movie timeline.
 *
 * \par Requirements:
 *  - 001 The callback shall consume the span synchronously and retain no input pointer.
 *  - 002 Returning FALSE shall stop traversal and invalidate all output for this operation.
 *
 * \param[in,out] user    Caller-owned context
 * \param[in]     start   Nonnegative start time in seconds
 * \param[in]     end     End time in seconds, greater than start
 * \param[in]     text    Borrowed readable UTF-8 bytes
 * \param[in]     size    Number of readable bytes
 *
 * \retval        TRUE    Cue accepted.
 * \retval        FALSE   Stop traversal and discard partial output.
 */
typedef bool_t (*MkvSubtitleProc)(void* user, float64_t start, float64_t end, const byte_t* text, uint64_t size);

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Finds supported UTF-8 subtitle tracks without traversing media clusters.
 *
 * \par Requirements:
 *  - 001 Missing path or nonzero capacity with missing tracks shall assert.
 *  - 002 At most capacity records shall be published; malformed input shall return zero.
 *  - 003 Compressed, encrypted and non-UTF-8 subtitle codecs shall be excluded.
 *  - 004 The caller shall ignore all output records when the returned count is zero.
 *
 * \param[in]  path       Required terminated native file path
 * \param[out] tracks     Caller-owned metadata slots; may be NULL at zero capacity
 * \param[in]  capacity   Number of writable slots
 *
 * \retval     0             No supported tracks, input failure or resource-release failure.
 * \retval     1..capacity   Number of initialized output records.
 */
uint32_t MkvSubtitles_Find(const TCHAR* path, MkvSubtitleTrack* tracks, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Visits each cue in the selected UTF-8 subtitle track.
 *
 * \par Requirements:
 *  - 001 Missing path or callback shall assert.
 *  - 002 All partially delivered output shall be discarded when this function returns FALSE.
 *  - 003 Unsupported lacing, malformed elements and invalid timing shall fail traversal.
 *  - 004 Each borrowed cue span shall remain readable only for the duration of its callback.
 *
 * \param[in]     path          Required terminated native file path
 * \param[in]     trackNumber   Selected nonzero TrackNumber
 * \param[in]     callback      Required synchronous consumer
 * \param[in,out] user          Caller context passed unchanged to the callback
 *
 * \retval        TRUE    Traversal and resource release succeeded.
 * \retval        FALSE   Missing track, malformed input, callback rejection or resource failure.
 */
bool_t MkvSubtitles_Read(const TCHAR* path, uint64_t trackNumber, MkvSubtitleProc callback, void* user);

#endif /* CAST_MKV_SUBTITLES_H */
