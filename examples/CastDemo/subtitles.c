/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * SubRip input and WebVTT delivery adapters used by CastDemo.
 * Typed SRT cue parsing, WebVTT timing/plain-text serialization and Unicode
 * byte decoding are integrated. Styled text, complete WebVTT and tx3g semantics remain required.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include "subtitles.h"
#include "mp4_demux.h"
#include "mkv_subtitles.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "utl/utl_srt.h"
#include "utl/utl_unicode.h"
#include "utl/utl_webvtt.h"
#include "crt/crt_memory.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define SUBTITLE_MAX_HOUR_DIGITS        (4)                ///< Retained application input-hour width limit.
#define MAX_SUBTITLE_BYTES              SUBTITLE_MAX_BYTES ///< Shared application payload limit.
#define SUBTITLE_TIMING_CAPACITY        (64)               ///< Fits two uint64_t-millisecond timestamps, arrow and LF.
#define SUBTITLE_UTF8_SCALAR_BYTES      (4)                ///< Largest shortest-form UTF-8 scalar.
#define SUBTITLE_MILLISECONDS_LIMIT     (18446744073709551616.0) ///< Exclusive uint64_t conversion boundary.
#define SUBTITLE_TIME_CR                ('\r')                   ///< Source carriage-return line ending.
#define SUBTITLE_TIME_LF                ('\n')                   ///< WebVTT line-feed delimiter.
#define SUBTITLE_TX3G_COUNT_BYTES       (2)                      ///< Timed-text sample's big-endian byte-count prefix.
#define SUBTITLE_BYTE_RADIX             (256)                    ///< Unsigned big-endian byte significance.
#define SUBTITLE_HALF_MILLISECOND       (0.5) ///< Round nonnegative media times to nearest millisecond.
#define SUBTITLE_WEBVTT_SIGNATURE       ("WEBVTT")
#define SUBTITLE_WEBVTT_SIGNATURE_BYTES (sizeof(SUBTITLE_WEBVTT_SIGNATURE) - 1)
#define SUBTITLE_CHAR_SPACE             (' ')
#define SUBTITLE_CHAR_TAB               ('\t')
#define SUBTITLE_NEWLINE                ("\n")
#define SUBTITLE_MP4_LANGUAGE_CAPACITY  (4)
#define SUBTITLE_LOAD_ERROR                                                                                            \
    ("Cannot read subtitles: invalid input or storage capacity exceeded. "                                             \
     "Use UTF-8/UTF-16 SRT or WebVTT, MP4 tx3g, or MKV UTF-8/SubRip text.")

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Cursor over startup-provided conversion storage; capacity includes termination.
 */
typedef struct Text
{
    byte_t*  data;
    uint32_t size;
    uint32_t capacity;
} Text;

/*************************************************************************************************/
/** \brief Source newline state for preserving embedded SubRip markup. */
typedef struct SubtitleLine_s
{
    bool_t previousNewline; ///< Start of the current emitted payload line.
    bool_t previousCr;      ///< CR was the previous source scalar.
} SubtitleLine_t;

/*************************************************************************************************/
/** \brief Paired scratch and destination cursors for one conversion. */
typedef struct Conversion_s
{
    Text source; ///< Decoded normalization scratch.
    Text output; ///< Bounded WebVTT staging cursor.
} Conversion_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Checks output capacity for an entire pending fragment and its terminator.
 *
 * \par Requirements:
 *  - 001 Required initialized storage shall retain size below capacity; misuse shall assert.
 *  - 002 Queries shall preserve all buffer bytes and state.
 *  - 003 Storage shall never grow or allocate during conversion.
 *
 * \param[in] text        Required initialized borrowed output
 * \param[in] byteCount   Pending fragment byte count
 *
 * \retval    TRUE    The fragment and final terminator fit.
 * \retval    FALSE   Insufficient remaining capacity.
 */
static FORCE_INLINE bool_t HasCapacity(const Text* text, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Converts finite nonnegative cue times into checked integer milliseconds.
 *
 * \par Requirements:
 *  - 001 Required outputs shall assert and remain unchanged on invalid input.
 *  - 002 Reversed, nonfinite or unrepresentable intervals shall fail before integer conversion.
 *  - 003 Nearest-millisecond rounding shall use upward ties; a rounded empty interval remains representable.
 *
 * \param[in]  start   Source start seconds
 * \param[in]  end     Source end seconds, at least start
 * \param[out] first   Required start millisecond output
 * \param[out] last    Required end millisecond output
 *
 * \retval     TRUE    Both endpoints are representable.
 * \retval     FALSE   Invalid time domain; outputs are unchanged.
 */
static FORCE_INLINE bool_t GetCueTimes(double start, double end, uint64_t* first, uint64_t* last);

/*************************************************************************************************/
/**
 * \brief Derives the exact normalized UTF-8 byte count for a subtitle text span.
 *
 * \par Requirements:
 *  - 001 The source shall remain stable and readable for the supplied extent.
 *  - 002 Invalid encoding, embedded NUL and an empty decoded span shall fail.
 *  - 003 Output above the application payload limit shall fail before count addition.
 *  - 004 Measurement shall perform no allocation, CRT or OS operation.
 *
 * \param[in] data   Required counted source
 * \param[in] size   Readable bytes within the application limit
 *
 * \retval    count   Exact UTF-8 bytes, excluding termination.
 * \retval    0       Invalid, empty or oversized decoded text.
 */
static FORCE_INLINE uint32_t GetUtf8TextByteCount(const byte_t* data, uint32_t size);

/*************************************************************************************************/
/**
 * \brief Appends bytes to the currently owned conversion buffer.
 *
 * \par Requirements:
 *  - 001 The caller shall retain exclusive access to initialized storage throughout conversion.
 *  - 002 Failure shall preserve the previous buffer and logical byte count.
 *
 * \param[in,out] text   Initialized owned buffer
 * \param[in]     data   Readable input
 * \param[in]     size   Input byte count
 *
 * \retval        1   Bytes appended and terminator retained.
 * \retval        0   Capacity limit exceeded.
 */
static FORCE_INLINE int Append(Text* text, const byte_t* data, uint64_t size);

/*************************************************************************************************/
/**
 * \brief Decodes supported input encodings into caller-owned UTF-8 scratch.
 *
 * \par Requirements:
 *  - 001 Input shall use UTF-8 or BOM-marked UTF-16.
 *  - 002 Embedded NUL code points and invalid encoded sequences shall be rejected.
 *  - 003 The destination shall be initialized and disjoint from stable input.
 *  - 004 Preflight shall reject insufficient capacity before writing destination bytes.
 *
 * \param[in,out] destination   Required initialized scratch cursor
 * \param[in]     data          Readable input bytes
 * \param[in]     size          Input byte count
 *
 * \retval        TRUE    Complete terminated UTF-8 was written.
 * \retval        FALSE   Invalid encoding, embedded NUL, oversized input or insufficient capacity.
 */
static FORCE_INLINE bool_t DecodeUtf8(Text* destination, const byte_t* data, uint64_t size);

/*************************************************************************************************/
/**
 * \brief Appends matching sidecars from one native directory.
 *
 * \par Requirements:
 *  - 001 The incoming count shall not exceed SUBTITLE_MAX_TRACKS.
 *  - 002 Each processed directory entry shall advance enumeration.
 *
 * \param[in]     directory   Terminated native directory path
 * \param[in]     stem        Optional media stem
 * \param[in,out] tracks      SUBTITLE_MAX_TRACKS caller-owned records
 * \param[in]     count       Number of previously initialized records
 *
 * \retval        count   Updated number of initialized track records.
 */
static FORCE_INLINE unsigned FindFiles(const wchar_t* directory,
                                       const wchar_t* stem,
                                       SubtitleTrack* tracks,
                                       unsigned       count);

/*************************************************************************************************/
/**
 * \brief Appends one decoded scalar while preserving embedded SubRip markup.
 *
 * \par Requirements:
 *  - 001 The caller shall supply exclusively owned output and initialized line state.
 *  - 002 The scalar shall be valid and nonzero; CRLF shall emit exactly one LF.
 *  - 003 Empty payload lines shall receive a space so they cannot terminate the cue.
 *  - 004 Capacity failure shall stop conversion; the owner shall invalidate partial text.
 *
 * \param[in,out] out      Initialized conversion buffer
 * \param[in,out] line     Initialized source newline state
 * \param[in]     scalar   Nonzero Unicode scalar
 *
 * \retval        TRUE    Scalar appended or the LF half of CRLF consumed.
 * \retval        FALSE   Output capacity failure.
 */
static FORCE_INLINE bool_t AppendMatroskaScalar(Text* out, SubtitleLine_t* line, uint32_t scalar);

/*************************************************************************************************/
/**
 * \brief Serializes one borrowed Matroska text cue as WebVTT.
 *
 * \par Requirements:
 *  - 001 The borrowed cue shall be consumed synchronously.
 *  - 002 The caller shall discard accumulated output after failure.
 *
 * \param[in,out] user    Initialized Text buffer
 * \param[in]     start   Nonnegative start time in seconds
 * \param[in]     end     End time after start
 * \param[in]     data    Readable UTF-8 cue bytes
 * \param[in]     size    Cue byte count
 *
 * \retval        1   Cue accepted, including a duration rounded to zero milliseconds.
 * \retval        0   Conversion or buffer append failed.
 */
static FORCE_INLINE int MatroskaCue(void* user, double start, double end, const byte_t* data, uint64_t size);

/*************************************************************************************************/
/**
 * \brief Converts a borrowed MP4 tx3g sample into WebVTT.
 *
 * \par Requirements:
 *  - 001 Sample text length shall not exceed the supplied sample extent.
 *  - 002 The borrowed sample shall be consumed synchronously.
 *
 * \param[in,out] user    Initialized Text buffer
 * \param[in]     start   Start time in seconds
 * \param[in]     end     End time in seconds
 * \param[in]     data    Readable sample bytes
 * \param[in]     size    Sample byte count
 *
 * \retval        1   Sample accepted.
 * \retval        0   Malformed sample, conversion or buffer append failure.
 */
static FORCE_INLINE int TextSample(void* user, double start, double end, const byte_t* data, uint32_t size);

/*************************************************************************************************/
/**
 * \brief Normalizes the complete counted UTF-8 scratch buffer in place.
 *
 * \par Requirements:
 *  - 001 Text shall be initialized, valid UTF-8 and contain no embedded NUL.
 *  - 002 CRLF and CR shall become LF without growing the stored extent.
 *
 * \param[in,out] text   Exclusively owned decoded scratch
 *
 * \retval        None   The count and terminator describe normalized text.
 */
static FORCE_INLINE void NormalizeNewlines(Text* text);

/*************************************************************************************************/
/**
 * \brief Serializes a parsed SubRip cue into bounded WebVTT staging.
 *
 * \par Requirements:
 *  - 001 The cue shall have been validated by utl_srt and retain its borrowed source.
 *  - 002 Source hour widths above the existing four-digit application limit shall fail.
 *  - 003 Identifier and payload shall retain their bytes; timing extensions shall not become WebVTT settings.
 *  - 004 Capacity failure shall leave partial staging for the enclosing owner to invalidate.
 *
 * \param[in,out] out   Initialized bounded output
 * \param[in]     cue   Required complete borrowed cue
 *
 * \retval        TRUE    The cue is appended with one separating blank line.
 * \retval        FALSE   Unsupported hour width or insufficient output capacity.
 */
static FORCE_INLINE bool_t AppendSrtCue(Text* out, const Utl_Srt_Cue_t* cue);

/*************************************************************************************************/
/**
 * \brief Converts typed SubRip cues using caller-owned scratch and output.
 *
 * \par Requirements:
 *  - 001 Scratch shall be normalized terminated UTF-8; output shall be disjoint and empty.
 *  - 002 At least one timed cue and a complete final block shall be required.
 *  - 003 Failure shall leave unpublished partial staging for the owner to invalidate.
 *
 * \param[in,out] conversion   Initialized output and disjoint normalized scratch
 *
 * \retval        TRUE    Complete conversion.
 * \retval        FALSE   Malformed SubRip or insufficient output capacity.
 */
static FORCE_INLINE bool_t ConvertSubRip(Conversion_t* conversion);

/*************************************************************************************************/
/**
 * \brief Reads a bounded sidecar and converts it using retained storage.
 *
 * \par Requirements:
 *  - 001 The caller shall supply initialized exclusive storage and a stable file selection.
 *  - 002 Input must fit output capacity with termination space before reading.
 *  - 003 Every opened file shall be closed before conversion; any I/O failure shall fail this attempt.
 *  - 004 The caller shall invalidate partial output after failure; file resources still belong to the CRT.
 *
 * \param[in,out] workspace   Initialized scratch and output
 * \param[in]     track       Required initialized selection
 *
 * \retval        TRUE    Complete converted sidecar.
 * \retval        FALSE   Open, seek, size, read, close or conversion failure.
 */
static FORCE_INLINE bool_t ReadSidecar(Subtitles_Workspace_t* workspace, const SubtitleTrack* track);

/*************************************************************************************************/
/**
 * \brief Writes the bounded terminated load-failure description.
 *
 * \par Requirements:
 *  - 001 Error storage shall be present and capacity shall include at least one byte.
 *  - 002 The description shall truncate only at capacity minus one and retain termination.
 *
 * \param[out] error      Writable description
 * \param[in]  capacity   Writable bytes
 *
 * \retval     None   The description is terminated.
 */
static FORCE_INLINE void WriteLoadError(char_t* error, uint32_t capacity);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Subtraction uses the established size-below-capacity invariant. A strict
 *  comparison reserves one byte for termination without count addition.
 */
static FORCE_INLINE bool_t HasCapacity(const Text* text, uint32_t byteCount)
{
    bool_t fits;

    LOGASSERT(NULL != text);
    LOGASSERT(NULL != text->data);
    LOGASSERT(0 < text->capacity);
    LOGASSERT(SUBTITLE_TEXT_CAPACITY >= text->capacity);
    LOGASSERT(text->size < text->capacity);

    fits = (byteCount < text->capacity - text->size);

    return fits;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Both the original seconds and rounded milliseconds are bounded before casting.
 *  NaN fails the ordered comparisons; infinity exceeds the exclusive upper bound.
 */
static FORCE_INLINE bool_t GetCueTimes(double start, double end, uint64_t* first, uint64_t* last)
{
    bool_t valid_start;
    bool_t valid_end;
    bool_t ordered;
    bool_t valid;
    double first_value;
    double last_value;
    bool_t first_fits;
    bool_t last_fits;

    LOGASSERT(NULL != first);
    LOGASSERT(NULL != last);

    valid_start = (0.0 <= start) && (start < SUBTITLE_MILLISECONDS_LIMIT / UTL_WEBVTT_MILLISECONDS_PER_SECOND);
    valid_end   = (0.0 <= end) && (end < SUBTITLE_MILLISECONDS_LIMIT / UTL_WEBVTT_MILLISECONDS_PER_SECOND);
    ordered     = (start <= end);
    valid       = valid_start && valid_end && ordered;
    if (valid)
    {
        first_value = (start * UTL_WEBVTT_MILLISECONDS_PER_SECOND) + SUBTITLE_HALF_MILLISECOND;
        last_value  = (end * UTL_WEBVTT_MILLISECONDS_PER_SECOND) + SUBTITLE_HALF_MILLISECOND;
        first_fits  = (first_value < SUBTITLE_MILLISECONDS_LIMIT);
        last_fits   = (last_value < SUBTITLE_MILLISECONDS_LIMIT);
        valid       = first_fits && last_fits;
        if (valid)
        {
            (*first) = (uint64_t)first_value;
            (*last)  = (uint64_t)last_value;
        }
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each iteration consumes a scalar or exits; no malformed input can repeat
 *  without progress. Subtraction bounds the accumulated count before addition.
 */
static FORCE_INLINE uint32_t GetUtf8TextByteCount(const byte_t* data, uint32_t size)
{
    Utl_Unicode_Reader_t     reader;
    uint32_t                 count;
    bool_t                   valid;
    Utl_Unicode_ReadResult_t result;
    uint32_t                 scalar;
    uint32_t                 width;

    LOGASSERT(NULL != data);
    LOGASSERT(MAX_SUBTITLE_BYTES >= size);

    Utl_Unicode_InitializeReader(&reader, data, size);
    count = 0;
    valid = TRUE;
    for (;;)
    {
        result = Utl_Unicode_ReadScalar(&reader, &scalar);
        if (UTL_UNICODE_READ_END == result)
        {
            break;
        }
        valid = (UTL_UNICODE_READ_SCALAR == result);
        if (valid)
        {
            valid = (0 != scalar);
        }
        if (!valid)
        {
            break;
        }
        width = Utl_Unicode_GetUtf8ByteCount(scalar);
        valid = (width <= MAX_SUBTITLE_BYTES - count);
        if (!valid)
        {
            break;
        }
        count += width;
    }
    if (!valid)
    {
        count = 0;
    }

    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Checks the retained capacity, appends counted bytes and restores termination.
 */
static FORCE_INLINE int Append(Text* text, const byte_t* data, uint64_t size)
{
    bool_t   fits;
    uint32_t count;

    LOGASSERT(NULL != text);
    LOGASSERT(NULL != data);

    fits = (size <= MAX_SUBTITLE_BYTES);
    if (fits)
    {
        count = (uint32_t)size;
        fits  = HasCapacity(text, count);
        if (fits)
        {
            CRT_Memory_Copy(text->data + text->size, data, count);
            text->size             += count;
            text->data[text->size]  = 0;
        }
    }

    return fits;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The Unicode reader validates/counts complete stable input before writing the
 *  retained scratch. The second pass writes exact UTF-8 without allocation,
 *  native character conversion or temporary UTF-16 storage.
 */
static FORCE_INLINE bool_t DecodeUtf8(Text* destination, const byte_t* data, uint64_t size)
{
    bool_t                   valid;
    uint32_t                 byte_count;
    Utl_Unicode_Reader_t     reader;
    uint32_t                 offset;
    Utl_Unicode_ReadResult_t read_result;
    uint32_t                 scalar;
    uint32_t                 written;

    LOGASSERT(NULL != destination);
    LOGASSERT(NULL != data);

    destination->size = 0;
    valid             = (0 < size) && (size <= MAX_SUBTITLE_BYTES);
    byte_count        = 0;
    if (valid)
    {
        byte_count = GetUtf8TextByteCount(data, (uint32_t)size);
        valid      = (0 < byte_count) && (byte_count < destination->capacity);
    }
    if (valid)
    {
        Utl_Unicode_InitializeReader(&reader, data, (uint32_t)size);
        offset = 0;
        for (;;)
        {
            read_result = Utl_Unicode_ReadScalar(&reader, &scalar);
            if (UTL_UNICODE_READ_END == read_result)
            {
                break;
            }
            LOGASSERT(UTL_UNICODE_READ_SCALAR == read_result);
            written = Utl_Unicode_WriteUtf8(scalar, (byte_t*)destination->data + offset, byte_count - offset);
            LOGASSERT(0 < written);
            offset += written;
        }
        LOGASSERT(byte_count == offset);
        destination->data[offset] = 0;
        destination->size         = offset;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Filters directory entries by subtitle extension and optional media stem.
 *  Every processed entry advances enumeration, including entries skipped by filters.
 */
static FORCE_INLINE unsigned FindFiles(const wchar_t* directory,
                                       const wchar_t* stem,
                                       SubtitleTrack* tracks,
                                       unsigned       count)
{
    WIN32_FIND_DATAW entry;
    wchar_t          pattern[260], path[260];
    HANDLE find;
    if (_snwprintf_s(pattern, 260, _TRUNCATE, L"%ls*", directory) < 0)
        return count;
    find = FindFirstFileW(pattern, &entry);
    if (find == INVALID_HANDLE_VALUE)
        return count;
    for (;;)
    {
        const wchar_t* extension;
        uint32_t       stem_length;
        bool_t         is_directory;
        bool_t         is_subtitle;
        bool_t         matches_stem;
        bool_t         path_fits;

        if (count >= SUBTITLE_MAX_TRACKS)
        {
            break;
        }
        extension    = wcsrchr(entry.cFileName, L'.');
        stem_length  = stem ? (uint32_t)wcslen(stem) : 0;
        is_directory = !!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        is_subtitle  = FALSE;
        matches_stem = TRUE;
        if (extension)
        {
            is_subtitle = !_wcsicmp(extension, L".srt") || !_wcsicmp(extension, L".vtt");
        }
        if (stem)
        {
            matches_stem = !_wcsnicmp(entry.cFileName, stem, stem_length) && (L'.' == entry.cFileName[stem_length]);
        }
        if (!is_directory && is_subtitle && matches_stem)
        {
            path_fits = _snwprintf_s(path, 260, _TRUNCATE, L"%ls%ls", directory, entry.cFileName) >= 0;
            if (path_fits)
            {
                Subtitles_InitializeFileTrack(path, &tracks[count]);
                count += 1;
            }
        }
        if (!FindNextFileW(find, &entry))
        {
            break;
        }
    }
    FindClose(find);
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  This adapter preserves existing inline markup. Styled cue validation and
 *  a specification-backed SubRip text model remain separate required work.
 */
static FORCE_INLINE bool_t AppendMatroskaScalar(Text* out, SubtitleLine_t* line, uint32_t scalar)
{
    bool_t   is_cr;
    bool_t   is_lf;
    bool_t   skip_lf;
    bool_t   valid;
    uint32_t written;
    byte_t   encoded[SUBTITLE_UTF8_SCALAR_BYTES];

    is_cr            = (SUBTITLE_TIME_CR == scalar);
    is_lf            = (SUBTITLE_TIME_LF == scalar);
    skip_lf          = line->previousCr && is_lf;
    line->previousCr = is_cr;
    valid            = TRUE;
    if (!skip_lf)
    {
        if (is_cr || is_lf)
        {
            scalar = SUBTITLE_TIME_LF;
            if (line->previousNewline)
            {
                valid = Append(out, (const byte_t*)" ", 1);
            }
        }
        if (valid)
        {
            written = Utl_Unicode_WriteUtf8(scalar, encoded, sizeof(encoded));
            LOGASSERT(0 < written);
            valid = Append(out, encoded, written);
        }
        line->previousNewline = (SUBTITLE_TIME_LF == scalar);
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Serializes cue times in milliseconds, normalizes newlines and preserves inline
 *  SubRip markup while preventing blank payload lines from ending a WebVTT cue.
 */
static FORCE_INLINE int MatroskaCue(void* user, double start, double end, const byte_t* data, uint64_t size)
{
    Text*                    out;
    bool_t                   valid;
    uint64_t                 first;
    uint64_t                 last;
    bool_t                   valid_size;
    bool_t                   positive;
    Utl_Unicode_Reader_t     reader;
    uint32_t                 written;
    byte_t                   timing[SUBTITLE_TIMING_CAPACITY];
    SubtitleLine_t           line;
    Utl_Unicode_ReadResult_t result;
    uint32_t                 scalar;

    LOGASSERT(NULL != user);
    LOGASSERT(NULL != data);

    out        = (Text*)user;
    valid      = GetCueTimes(start, end, &first, &last);
    valid_size = (0 < size) && (size <= MAX_SUBTITLE_BYTES);
    valid      = valid && valid_size;
    positive   = FALSE;
    if (valid)
    {
        positive = (first < last);
        Utl_Unicode_InitializeReader(&reader, data, (uint32_t)size);
    }
    if (positive)
    {
        written = Utl_WebVtt_WriteTiming(first, last, timing, sizeof(timing));
        LOGASSERT(0 < written);
        valid = Append(out, timing, written);
    }
    line.previousNewline = TRUE;
    line.previousCr      = FALSE;
    for (;;)
    {
        if (!valid)
        {
            break;
        }
        result = Utl_Unicode_ReadScalar(&reader, &scalar);
        if (UTL_UNICODE_READ_END == result)
        {
            break;
        }
        valid = (UTL_UNICODE_READ_SCALAR == result);
        if (valid)
        {
            valid = (0 != scalar);
        }
        if (valid && positive)
        {
            valid = AppendMatroskaScalar(out, &line, scalar);
        }
    }
    if (valid && positive)
    {
        valid = Append(out, line.previousNewline ? (const byte_t*)"\n" : (const byte_t*)"\n\n", line.previousNewline ? 1 : 2);
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Reads the tx3g counted text, escapes markup characters and prevents empty
 *  payload lines from splitting a WebVTT cue.
 */
static FORCE_INLINE int TextSample(void* user, double start, double end, const byte_t* data, uint32_t size)
{
    Text*            out;
    bool_t           valid;
    bool_t           positive;
    bool_t           has_text;
    uint32_t         length;
    Utl_WebVtt_Cue_t cue;
    uint32_t         required;
    uint32_t         written;

    LOGASSERT(NULL != user);
    LOGASSERT(NULL != data);

    out      = (Text*)user;
    valid    = (SUBTITLE_TX3G_COUNT_BYTES <= size);
    positive = FALSE;
    has_text = FALSE;
    length   = 0;
    if (valid)
    {
        length   = ((uint32_t)data[0] * SUBTITLE_BYTE_RADIX) + data[1];
        valid    = (length <= size - SUBTITLE_TX3G_COUNT_BYTES);
        has_text = (0 < length);
    }
    if (valid && has_text)
    {
        cue.text          = data + SUBTITLE_TX3G_COUNT_BYTES;
        cue.textByteCount = length;
        valid             = GetCueTimes(start, end, &cue.start, &cue.end);
        if (valid)
        {
            positive = (cue.start < cue.end);
        }
    }
    if (positive)
    {
        required = Utl_WebVtt_GetCueByteCount(&cue);
        valid    = (0 < required);
        if (valid)
        {
            valid = HasCapacity(out, required);
        }
        if (valid)
        {
            written = Utl_WebVtt_WriteCue(&cue, out->data + out->size, out->capacity - out->size - 1);
            LOGASSERT(required == written);
            out->size            += written;
            out->data[out->size]  = 0;
        }
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Destination never overtakes the counted source cursor; the retained terminator
 *  is written after every original byte has been consumed.
 */
static FORCE_INLINE void NormalizeNewlines(Text* text)
{
    uint32_t read;
    uint32_t written;
    byte_t   value;
    bool_t   has_next;

    read    = 0;
    written = 0;
    for (; read < text->size; ++read)
    {
        value = text->data[read];
        if (SUBTITLE_TIME_CR == value)
        {
            value    = SUBTITLE_TIME_LF;
            has_next = (read + 1 < text->size);
            if (has_next && (SUBTITLE_TIME_LF == text->data[read + 1]))
            {
                ++read;
            }
        }
        text->data[written] = value;
        ++written;
    }
    text->data[written] = 0;
    text->size          = written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Input has already been normalized to LF and validated as UTF-8. The WebVTT
 *  timing writer owns the destination grammar; raw SubRip markup remains preserved
 *  pending the styled-text model. Empty cues need only the timing line and separator.
 */
static FORCE_INLINE bool_t AppendSrtCue(Text* out, const Utl_Srt_Cue_t* cue)
{
    bool_t   start_fits;
    bool_t   end_fits;
    bool_t   valid;
    bool_t   has_identifier;
    uint32_t written;
    byte_t   timing[SUBTITLE_TIMING_CAPACITY];
    bool_t   has_payload;

    start_fits     = (cue->start.hourDigits <= SUBTITLE_MAX_HOUR_DIGITS);
    end_fits       = (cue->end.hourDigits <= SUBTITLE_MAX_HOUR_DIGITS);
    valid          = start_fits && end_fits;
    has_identifier = (0 < cue->identifier.byteCount);
    if (valid && has_identifier)
    {
        valid = Append(out, cue->identifier.data, cue->identifier.byteCount);
        if (valid)
        {
            valid = Append(out, (const byte_t*)SUBTITLE_NEWLINE, sizeof(SUBTITLE_NEWLINE) - 1);
        }
    }
    if (valid)
    {
        written = Utl_WebVtt_WriteTiming(cue->start.milliseconds, cue->end.milliseconds, timing, sizeof(timing));
        LOGASSERT(0 < written);
        valid = Append(out, timing, written);
    }
    has_payload = (0 < cue->payload.byteCount);
    if (valid && has_payload)
    {
        valid = Append(out, cue->payload.data, cue->payload.byteCount);
        if (valid)
        {
            valid = Append(out, (const byte_t*)SUBTITLE_NEWLINE, sizeof(SUBTITLE_NEWLINE) - 1);
        }
    }
    if (valid)
    {
        valid = Append(out, (const byte_t*)SUBTITLE_NEWLINE, sizeof(SUBTITLE_NEWLINE) - 1);
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The SRT reader owns cue framing and timestamp validation. Each iteration
 *  consumes a complete cue or stops; the application owner invalidates partial output.
 */
static FORCE_INLINE bool_t ConvertSubRip(Conversion_t* conversion)
{
    Utl_Srt_Reader_t     reader;
    bool_t               valid;
    bool_t               has_cues;
    Utl_Srt_ReadResult_t result;
    Utl_Srt_Cue_t        cue;

    Utl_Srt_InitializeReader(&reader, conversion->source.data, conversion->source.size);
    valid    = Append(&conversion->output, (const byte_t*)UTL_WEBVTT_HEADER, sizeof(UTL_WEBVTT_HEADER) - 1);
    has_cues = FALSE;
    for (;;)
    {
        if (!valid)
        {
            break;
        }
        result = Utl_Srt_ReadCue(&reader, &cue);
        if (UTL_SRT_READ_END == result)
        {
            break;
        }
        valid = (UTL_SRT_READ_CUE == result);
        if (valid)
        {
            has_cues = TRUE;
            valid    = AppendSrtCue(&conversion->output, &cue);
        }
    }
    valid = valid && has_cues;

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Sidecar bytes borrow the output buffer until decoded into separate scratch.
 *  Every opened FILE is closed before conversion; all I/O failures invalidate
 *  this attempt. CRT file resources remain a separate allocation/lifetime defect.
 */
static FORCE_INLINE bool_t ReadSidecar(Subtitles_Workspace_t* workspace, const SubtitleTrack* track)
{
    FILE*    file;
    int32_t  length;
    uint32_t count;
    int32_t  opened;
    bool_t   valid;
    int32_t  seek_result;
    uint64_t read_count;
    int32_t  closed;

    file   = NULL;
    length = 0;
    count  = 0;
    opened = _wfopen_s(&file, track->path, L"rb");
    valid  = (0 == opened) && (NULL != file);
    if (valid)
    {
        seek_result = fseek(file, 0, SEEK_END);
        valid       = (0 == seek_result);
    }
    if (valid)
    {
        length = ftell(file);
        valid  = (0 < length) && ((uint64_t)length < workspace->capacity);
    }
    if (valid)
    {
        seek_result = fseek(file, 0, SEEK_SET);
        valid       = (0 == seek_result);
    }
    if (valid)
    {
        count      = (uint32_t)length;
        read_count = fread(workspace->output, 1, count, file);
        valid      = (count == read_count);
    }
    if (NULL != file)
    {
        closed = fclose(file);
        valid  = valid && (0 == closed);
    }
    if (valid)
    {
        valid = Subtitles_Convert(workspace, workspace->output, count);
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Copies one bounded application description without a formatting dependency.
 */
static FORCE_INLINE void WriteLoadError(char_t* error, uint32_t capacity)
{
    uint32_t count;

    LOGASSERT(NULL != error);
    LOGASSERT(0 < capacity);

    count = sizeof(SUBTITLE_LOAD_ERROR) - 1;
    if (capacity <= count)
    {
        count = capacity - 1;
    }
    CRT_Memory_Copy(error, SUBTITLE_LOAD_ERROR, count);
    error[count] = 0;
}
/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Clearing both buffers touches their pages during startup. This does not lock
 *  pages or establish residency for file, codec, server or operating-system state.
 */
void Subtitles_Initialize(Subtitles_Workspace_t* workspace, byte_t* source, byte_t* output, uint32_t capacity)
{
    LOGASSERT(NULL != workspace);
    LOGASSERT(NULL != source);
    LOGASSERT(NULL != output);
    LOGASSERT(source != output);
    LOGASSERT(0 < capacity);
    LOGASSERT(SUBTITLE_TEXT_CAPACITY >= capacity);

    workspace->source    = source;
    workspace->output    = output;
    workspace->capacity  = capacity;
    workspace->byteCount = 0;
    CRT_Memory_Set(source, 0, capacity);
    CRT_Memory_Set(output, 0, capacity);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Copies the path and display label and derives a short lowercase language
 *  suffix from the filename when present.
 */
void Subtitles_InitializeFileTrack(const wchar_t* path, SubtitleTrack* track)
{
    const wchar_t* name;
    wchar_t        stem[260];
    const wchar_t* dot;
    LOGASSERT(NULL != path);
    LOGASSERT(NULL != track);

    name = wcsrchr(path, L'\\');
    memset(track, 0, sizeof(*track));
    track->embedded = -1;
    wcsncpy_s(track->path, 260, path, _TRUNCATE);
    name = name ? name + 1 : path;
    wcsncpy_s(track->label, 320, name, _TRUNCATE);
    wcsncpy_s(stem, 260, name, _TRUNCATE);
    dot = wcsrchr(stem, L'.');
    if (dot)
        stem[dot - stem] = 0;
    name = wcsrchr(stem, L'.');
    name = name ? name + 1 : stem;
    strcpy_s(track->language, 16, "und");
    if (wcslen(name) == 2 || wcslen(name) == 3)
    {
        unsigned i;
        for (i = 0; name[i] >= L'a' && name[i] <= L'z'; ++i)
        {
        }
        if (!name[i])
            WideCharToMultiByte(CP_UTF8, 0, name, -1, track->language, 16, NULL, NULL);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No ownership transfer occurs; consumers must finish before workspace reuse.
 */
const byte_t* Subtitles_GetText(const Subtitles_Workspace_t* workspace)
{
    LOGASSERT(NULL != workspace);
    LOGASSERT(NULL != workspace->output);

    return workspace->output;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The output count is published only after complete conversion.
 */
uint32_t Subtitles_GetByteCount(const Subtitles_Workspace_t* workspace)
{
    LOGASSERT(NULL != workspace);
    LOGASSERT(workspace->byteCount < workspace->capacity);

    return workspace->byteCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Searches adjacent sidecar files and the Subs directory, then appends supported
 *  MP4 and Matroska tracks within the fixed track-list capacity.
 */
uint32_t Subtitles_Find(const wchar_t* media, SubtitleTrack tracks[SUBTITLE_MAX_TRACKS])
{
    wchar_t  directory[260], stem[260], subs[260];
    wchar_t* slash;
    wchar_t* dot;
    unsigned count = 0, i;
    DWORD pathLength;
    LOGASSERT(NULL != media);
    LOGASSERT(NULL != tracks);

    if (!*media || wcslen(media) >= 260)
        return 0;
    pathLength = GetFullPathNameW(media, 260, directory, NULL);
    if (!pathLength || pathLength >= 260)
        return 0;
    slash = wcsrchr(directory, L'\\');
    if (!slash)
        return 0;
    wcscpy_s(stem, 260, slash + 1);
    slash[1] = 0;
    dot      = wcsrchr(stem, L'.');
    if (dot)
        *dot = 0;
    count = FindFiles(directory, stem, tracks, count);
    if (_snwprintf_s(subs, 260, _TRUNCATE, L"%lsSubs\\", directory) >= 0)
        count = FindFiles(subs, NULL, tracks, count);
    for (i = 0; count < SUBTITLE_MAX_TRACKS; ++i)
    {
        char language[4];
        if (!Mp4Demux_ReadTextTrack(media, i, language, NULL, NULL))
            break;
        Subtitles_InitializeFileTrack(media, &tracks[count]);
        tracks[count].embedded = (int)i;
        strcpy_s(tracks[count].language, 16, language);
        _snwprintf_s(tracks[count].label, 320, _TRUNCATE, L"Embedded %u (%hs)", i + 1, language);
        ++count;
    }
    {
        MkvSubtitleTrack mkv[SUBTITLE_MAX_TRACKS];
        unsigned         total = MkvSubtitles_Find(media, mkv, SUBTITLE_MAX_TRACKS - count);
        for (i = 0; i < total; ++i)
        {
            wchar_t name[256] = {0};
            Subtitles_InitializeFileTrack(media, &tracks[count]);
            tracks[count].matroskaTrack = mkv[i].number;
            strcpy_s(tracks[count].language, 16, mkv[i].language);
            MultiByteToWideChar(CP_UTF8, 0, mkv[i].name, -1, name, 256);
            _snwprintf_s(tracks[count].label,
                         320,
                         _TRUNCATE,
                         L"MKV track %llu (%hs)%ls%ls",
                         (unsigned long long)mkv[i].number,
                         mkv[i].language,
                         name[0] ? L" - " : L"",
                         name);
            ++count;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Normalizes text encoding and line endings. WebVTT-prefixed input is returned
 *  without full cue validation; SubRip input must contain valid timed cues and
 *  has its timestamp separators converted to WebVTT form.
 */
bool_t Subtitles_Convert(Subtitles_Workspace_t* workspace, const void* bytes, uint64_t size)
{
    Conversion_t conversion;
    bool_t       valid;
    bool_t       has_signature;
    byte_t       separator;
    bool_t       is_line_end;
    bool_t       is_space;
    bool_t       is_terminated;
    bool_t       valid_separator;

    LOGASSERT(NULL != workspace);
    LOGASSERT(NULL != bytes);
    LOGASSERT(NULL != workspace->source);
    LOGASSERT(NULL != workspace->output);
    LOGASSERT(0 < workspace->capacity);

    workspace->byteCount       = 0;
    conversion.source.data     = workspace->source;
    conversion.source.size     = 0;
    conversion.source.capacity = workspace->capacity;
    conversion.output.data     = workspace->output;
    conversion.output.size     = 0;
    conversion.output.capacity = workspace->capacity;
    valid                      = DecodeUtf8(&conversion.source, (const byte_t*)bytes, size);
    if (valid)
    {
        NormalizeNewlines(&conversion.source);
        has_signature = FALSE;
        if (SUBTITLE_WEBVTT_SIGNATURE_BYTES <= conversion.source.size)
        {
            separator     = conversion.source.data[SUBTITLE_WEBVTT_SIGNATURE_BYTES];
            has_signature = (0 == CRT_Memory_Compare(conversion.source.data,
                                                       SUBTITLE_WEBVTT_SIGNATURE,
                                                       SUBTITLE_WEBVTT_SIGNATURE_BYTES));
            is_line_end     = (SUBTITLE_TIME_LF == separator);
            is_space        = (SUBTITLE_CHAR_SPACE == separator) || (SUBTITLE_CHAR_TAB == separator);
            is_terminated   = (0 == separator);
            valid_separator = is_line_end || is_space || is_terminated;
            has_signature   = has_signature && valid_separator;
        }
        if (has_signature)
        {
            valid = Append(&conversion.output, conversion.source.data, conversion.source.size);
        }
        else
        {
            valid = ConvertSubRip(&conversion);
        }
    }
    if (valid)
    {
        workspace->byteCount = conversion.output.size;
    }
    else
    {
        workspace->output[0] = 0;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Dispatches to Matroska, MP4 timed-text or sidecar conversion using retained
 *  buffers. Sidecar input borrows output until decoding completes. File and
 *  container dependencies still create native/CRT resources after startup.
 */
bool_t Subtitles_Load(Subtitles_Workspace_t* workspace, const SubtitleTrack* track, char* error, uint32_t errorCapacity)
{
    Text   out;
    bool_t valid;
    char_t language[SUBTITLE_MP4_LANGUAGE_CAPACITY];

    LOGASSERT(NULL != workspace);
    LOGASSERT(NULL != track);
    LOGASSERT(NULL != error);
    LOGASSERT(0 < errorCapacity);
    LOGASSERT(NULL != workspace->output);
    LOGASSERT(NULL != workspace->source);
    LOGASSERT(0 < workspace->capacity);

    workspace->byteCount = 0;
    workspace->output[0] = 0;
    out.data             = workspace->output;
    out.size             = 0;
    out.capacity         = workspace->capacity;
    if (0 != track->matroskaTrack)
    {
        valid = Append(&out, (const byte_t*)UTL_WEBVTT_HEADER, sizeof(UTL_WEBVTT_HEADER) - 1);
        if (valid)
        {
            valid = !!MkvSubtitles_Read(track->path, track->matroskaTrack, MatroskaCue, &out);
        }
    }
    else if (0 <= track->embedded)
    {
        valid = Append(&out, (const byte_t*)UTL_WEBVTT_HEADER, sizeof(UTL_WEBVTT_HEADER) - 1);
        if (valid)
        {
            valid = !!Mp4Demux_ReadTextTrack(track->path, (uint32_t)track->embedded, language, TextSample, &out);
        }
    }
    else
    {
        valid    = ReadSidecar(workspace, track);
        out.size = workspace->byteCount;
    }
    if (valid)
    {
        workspace->byteCount = out.size;
    }
    else
    {
        workspace->output[0] = 0;
        WriteLoadError(error, errorCapacity);
    }

    return valid;
}
