#ifndef UTL_SRT_H
#define UTL_SRT_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Matroska technical documentation, SRT Subtitles:
 * https://www.matroska.org/technical/subtitles.html#srt-subtitles
 * Library of Congress fdd000569, last significant update 27 May 2025:
 * https://www.loc.gov/preservation/digital/formats/fdd/fdd000569.shtml
 *
 * This counted UTF-8 reader implements the cue/timestamp envelope used by CastDemo.
 * Compatibility accepts optional/non-numeric identifiers, comma or period fractions,
 * CRLF/CR/LF, optional final separation, empty payloads and opaque timing suffixes.
 * Identifiers containing the cue arrow are invalid. Inline markup and timing
 * suffix interpretation are separate responsibilities; borrowed bytes are preserved.
 * UTF-16 decoding belongs to utl_unicode before this parser. No line-length or
 * cue-order restriction is invented from the descriptive references.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Borrowed, counted UTF-8 bytes with no implicit terminator. */
typedef struct Utl_Srt_Span_s
{
    const byte_t* data;      ///< Borrowed source, NULL for an absent span.
    uint32_t      byteCount; ///< Readable extent.
} Utl_Srt_Span_t;

/*************************************************************************************************/
/** \brief Parsed integer timestamp and its source hour-field width. */
typedef struct Utl_Srt_Time_s
{
    uint64_t milliseconds; ///< Nonnegative offset; uint64_t is the model limit.
    uint32_t hourDigits;   ///< At least two; allows application-specific width policies.
} Utl_Srt_Time_t;

/*************************************************************************************************/
/**
 * \brief One complete cue with borrowed text and uninterpreted extension spans.
 *
 * \par Details:
 *  Payload excludes its final line ending but preserves internal line endings.
 *  Empty payload is permitted. The timing suffix begins immediately after the end
 *  timestamp and is not interpreted as WebVTT settings. No source is modified.
 */
typedef struct Utl_Srt_Cue_s
{
    Utl_Srt_Span_t identifier;      ///< Optional identifier line, excluding its terminator.
    Utl_Srt_Time_t start;           ///< Inclusive media start.
    Utl_Srt_Time_t end;             ///< End strictly after start.
    Utl_Srt_Span_t timingExtension; ///< Uninterpreted remainder of the timing line.
    Utl_Srt_Span_t payload;         ///< Counted text, including any inline markup.
} Utl_Srt_Cue_t;

/*************************************************************************************************/
/** \brief Result of an atomic whole-cue read. */
typedef enum Utl_Srt_ReadResult_e
{
    UTL_SRT_READ_END,
    UTL_SRT_READ_CUE,
    UTL_SRT_READ_INVALID
} Utl_Srt_ReadResult_t;

/*************************************************************************************************/
/** \brief Caller-owned cursor over stable complete UTF-8 input. */
typedef struct Utl_Srt_Reader_s
{
    const byte_t* input;     ///< Borrowed storage, NULL only for empty input.
    uint32_t      byteCount; ///< Complete readable extent.
    uint32_t      offset;    ///< Next unconsumed byte, within byteCount.
} Utl_Srt_Reader_t;

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
 * \brief Initializes a reader and consumes one optional UTF-8 signature.
 *
 * \par Requirements:
 *  - 001 Missing reader or nonempty input storage shall assert as programmer misuse.
 *  - 002 Input shall remain stable and disjoint from the cursor and all cue outputs.
 *  - 003 Only an initial complete UTF-8 signature shall be skipped.
 *  - 004 Invalid payload shall be reported by ReadCue, not asserted during initialization.
 *  - 005 No allocation, file, platform or system-CRT operation shall occur.
 *
 * \param[out] reader      Required exclusively owned cursor
 * \param[in]  input       Stable complete source; NULL only for zero byteCount
 * \param[in]  byteCount   Readable source extent
 *
 * \retval     None   The reader is ready for queries and cue reads.
 */
void Utl_Srt_InitializeReader(Utl_Srt_Reader_t* reader, const byte_t* input, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Gets the unconsumed source byte count.
 *
 * \par Requirements:
 *  - 001 The caller shall supply an initialized reader and retain its borrowed source.
 *  - 002 Cursor invariants shall assert before subtraction; no source byte shall be read.
 *
 * \param[in] reader   Required initialized cursor
 *
 * \retval    count   Remaining bytes, including trailing blank lines or invalid data.
 */
uint32_t Utl_Srt_GetRemainingByteCount(const Utl_Srt_Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads a complete timestamp prefix without changing outputs on failure.
 *
 * \par Requirements:
 *  - 001 All pointers shall be supplied and disjoint; missing pointers shall assert.
 *  - 002 Hours shall use at least two digits; minutes/seconds exactly two below sixty.
 *  - 003 Fraction shall use exactly three digits after comma or period.
 *  - 004 The entire millisecond result shall fit uint64_t without overflow.
 *  - 005 A following non-digit belongs to the enclosing grammar; additional fractional digits shall fail.
 *  - 006 Malformed/truncated input shall preserve both outputs; no allocation shall occur.
 *
 * \param[out] time        Required parsed timestamp
 * \param[out] bytesRead   Required consumed prefix count
 * \param[in]  input       Required counted source
 * \param[in]  byteCount   Readable extent
 *
 * \retval     TRUE    A complete timestamp was parsed.
 * \retval     FALSE   Invalid width, component, delimiter, truncation or overflow.
 */
bool_t Utl_Srt_ReadTimestamp(Utl_Srt_Time_t* time, uint32_t* bytesRead, const byte_t* input, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Reads the next complete cue using private staging.
 *
 * \par Requirements:
 *  - 001 Required initialized reader and disjoint cue output shall assert when absent.
 *  - 002 Blank separators shall be skipped; each iteration shall consume input or terminate.
 *  - 003 Timing shall have a space-delimited arrow and strictly increasing endpoints.
 *  - 004 Every consumed nonempty line shall be valid UTF-8 without embedded NUL.
 *  - 005 Success shall publish a complete cue and cursor together; failure shall preserve both.
 *  - 006 End shall consume trailing blank lines, preserve cue output and be repeatable.
 *  - 007 No allocation or mutable global state shall occur; work is bounded by the remaining input extent.
 *
 * \param[in,out] reader   Required exclusively owned cursor
 * \param[out]    cue      Required destination; preserved on END or INVALID
 *
 * \retval        UTL_SRT_READ_CUE       A complete borrowed cue is available.
 * \retval        UTL_SRT_READ_END       No cue remains; subsequent reads repeat END.
 * \retval        UTL_SRT_READ_INVALID   Invalid input; cursor and cue are unchanged.
 */
Utl_Srt_ReadResult_t Utl_Srt_ReadCue(Utl_Srt_Reader_t* reader, Utl_Srt_Cue_t* cue);

#endif /* UTL_SRT_H */
