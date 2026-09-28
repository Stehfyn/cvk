#ifndef CAST_SUBTITLES_H
#define CAST_SUBTITLES_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * CastDemo subtitle discovery and caller-owned conversion workspace.
 * Unicode and WebVTT wire operations use utl_unicode and utl_webvtt.
 * Conversion reuses startup-provided storage. Native discovery, file/container
 * dependencies and full SRT/tx3g parsing remain pending migration and may allocate
 * or block after initialization.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <stdint.h>
#include <wchar.h>
#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define SUBTITLE_MAX_TRACKS    (32)                     ///< Maximum discovered tracks in one result list.
#define SUBTITLE_MAX_BYTES     (0x01000000)             ///< Sixteen MiB application payload limit.
#define SUBTITLE_TEXT_CAPACITY (SUBTITLE_MAX_BYTES + 1) ///< Full payload and retained terminator.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Sidecar or embedded text-track selection with owned display fields. */
typedef struct SubtitleTrack
{
    wchar_t  path[260];
    wchar_t  label[320];
    char     language[16];
    int32_t  embedded;      /* -1: sidecar; otherwise zero-based MP4 text track */
    uint64_t matroskaTrack; /* nonzero: embedded Matroska UTF-8 track number */
} SubtitleTrack;

/*************************************************************************************************/
/**
 * \brief Conversion cursor over two startup-provided text buffers.
 *
 * \par Details:
 *  The owner retains both disjoint buffers and serializes all operations.
 *  Source holds decoded UTF-8; output holds file input during loading and the
 *  final WebVTT after conversion. Returned text is borrowed until the next load
 *  or conversion. File/container readers and consumers own their separate resources.
 */
typedef struct Subtitles_Workspace_s
{
    char_t*  source;    ///< Normalization scratch, capacity bytes.
    char_t*  output;    ///< Borrowed result or transient sidecar input, capacity bytes.
    uint32_t capacity;  ///< Bytes in each buffer, including termination space.
    uint32_t byteCount; ///< Published output bytes, zero after failure.
} Subtitles_Workspace_t;

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
 * \brief Establishes and touches conversion storage during application initialization.
 *
 * \par Requirements:
 *  - 001 Required workspace and two disjoint buffers shall be supplied; absent pointers shall assert.
 *  - 002 Capacity shall be between one and SUBTITLE_TEXT_CAPACITY; misuse shall assert.
 *  - 003 The owner shall retain each buffer for the workspace lifetime and serialize access.
 *  - 004 Initialization shall clear both complete buffers and publish empty output without allocation.
 *
 * \param[out] workspace   Required exclusively owned cursor
 * \param[out] source      Required normalization storage, disjoint from output and workspace
 * \param[out] output      Required result storage, disjoint from source and workspace
 * \param[in]  capacity    Readable/writable bytes in each buffer
 *
 * \retval     None   The workspace is ready for reuse.
 */
void Subtitles_Initialize(Subtitles_Workspace_t* workspace, char_t* source, char_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Initializes one file-backed track and derives its display language.
 *
 * \par Requirements:
 *  - 001 Required path and destination shall be supplied; missing pointers shall assert.
 *  - 002 The terminated path shall remain readable and disjoint from the destination.
 *  - 003 Initialization shall clear embedded selection and establish the default language.
 *
 * \param[in]  path    Required terminated native path
 * \param[out] track   Required exclusively owned destination
 *
 * \retval     None   The file-backed track is initialized; legacy field copies truncate long input.
 */
void Subtitles_InitializeFileTrack(const wchar_t* path, SubtitleTrack* track);

/*************************************************************************************************/
/**
 * \brief Gets the borrowed result storage of an initialized workspace.
 *
 * \par Requirements:
 *  - 001 The initialized workspace and storage shall remain owned and stable.
 *  - 002 A load/conversion failure shall expose an empty terminated string.
 *
 * \param[in] workspace   Required initialized cursor
 *
 * \retval    text   Borrowed terminated UTF-8, valid until the next mutating operation.
 */
const char_t* Subtitles_GetText(const Subtitles_Workspace_t* workspace);

/*************************************************************************************************/
/**
 * \brief Gets the published WebVTT byte count, excluding termination.
 *
 * \par Requirements:
 *  - 001 The caller shall provide an initialized workspace; missing storage shall assert.
 *  - 002 Failed loads/conversions shall publish zero bytes.
 *
 * \param[in] workspace   Required initialized cursor
 *
 * \retval    count   Published result bytes, strictly less than capacity.
 */
uint32_t Subtitles_GetByteCount(const Subtitles_Workspace_t* workspace);

/*************************************************************************************************/
/**
 * \brief Finds adjacent sidecars, Subs-directory files and supported embedded tracks.
 *
 * \par Requirements:
 *  - 001 Required media and result storage shall be supplied; missing pointers shall assert.
 *  - 002 The terminated media path shall remain readable and disjoint from result storage.
 *  - 003 Only the returned prefix of the fixed-capacity result array shall be published.
 *  - 004 Native discovery and container failures shall retain any tracks already found.
 *
 * \param[in]  media    Required terminated native media path
 * \param[out] tracks   Required storage for SUBTITLE_MAX_TRACKS entries
 *
 * \retval     count   Number of initialized entries, at most SUBTITLE_MAX_TRACKS.
 * \retval     0       No supported tracks or the media path could not be resolved.
 */
uint32_t Subtitles_Find(const wchar_t* media, SubtitleTrack tracks[SUBTITLE_MAX_TRACKS]);

/*************************************************************************************************/
/**
 * \brief Converts counted subtitle bytes into the workspace's reusable WebVTT output.
 *
 * \par Requirements:
 *  - 001 Required initialized workspace and source shall be supplied; missing pointers shall assert.
 *  - 002 Source shall remain stable during decoding and disjoint from normalization scratch and cursor.
 *  - 003 Source may equal output; decoding shall finish before output is overwritten.
 *  - 004 Invalid encoding, embedded NUL, malformed SubRip and insufficient capacity shall fail.
 *  - 005 Failure shall publish zero bytes and an empty output string; partial scratch is not published.
 *  - 006 No allocation or release shall occur; prefixed WebVTT still lacks full grammar validation.
 *
 * \param[in,out] workspace   Required initialized reusable storage
 * \param[in]     data        Required readable source bytes
 * \param[in]     size        Source byte count
 *
 * \retval        TRUE    Complete WebVTT is available through GetText/GetByteCount.
 * \retval        FALSE   Invalid input or insufficient capacity; prior output is invalidated.
 */
bool_t Subtitles_Convert(Subtitles_Workspace_t* workspace, const void* data, uint64_t size);

/*************************************************************************************************/
/**
 * \brief Loads a selected sidecar, Matroska UTF-8/SubRip track or MP4 tx3g track.
 *
 * \par Requirements:
 *  - 001 Required initialized workspace/selection and nonempty error storage shall assert when absent.
 *  - 002 Inputs and outputs shall be disjoint; selection shall remain stable throughout the call.
 *  - 003 Failure shall invalidate partial text and publish zero bytes plus a terminated error description.
 *  - 004 Successful borrowed output shall remain valid until the next workspace mutation.
 *  - 005 Text buffers shall never grow or allocate; file/container dependencies still may allocate or block.
 *
 * \param[in,out] workspace       Required initialized conversion storage
 * \param[in]     track           Required initialized track selection
 * \param[out]    error           Required writable error description; unchanged on success
 * \param[in]     errorCapacity   Writable bytes including the terminator, at least one
 *
 * \retval        TRUE    Complete output is available through GetText/GetByteCount.
 * \retval        FALSE   Read, conversion or capacity failure.
 */
bool_t Subtitles_Load(Subtitles_Workspace_t* workspace,
                      const SubtitleTrack*   track,
                      char*                  error,
                      uint32_t               errorCapacity);

#endif
