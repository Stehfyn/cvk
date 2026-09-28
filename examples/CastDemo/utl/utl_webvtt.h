#ifndef UTL_WEBVTT_H
#define UTL_WEBVTT_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * WebVTT Candidate Recommendation Draft, 20 May 2026, sections 4.1 and 4.2.2:
 * https://www.w3.org/TR/2026/CRD-webvtt1-20260520/
 * Counted timestamps, cue timing lines and safe plain-text cue serialization.
 * Regions, settings, styled cue trees and full-file parsing remain separate work.
 * Milliseconds are represented by uint64_t; this is a model limit, not a wire limit.
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

#define UTL_WEBVTT_HEADER                  "WEBVTT\n\n" ///< UTF-8 file signature and required separation.
#define UTL_WEBVTT_MILLISECONDS_PER_SECOND (1000)       ///< Timestamp fractional scale.
#define UTL_WEBVTT_SECONDS_PER_MINUTE      (60)         ///< Timestamp seconds domain.
#define UTL_WEBVTT_MINUTES_PER_HOUR        (60)         ///< Timestamp minutes domain.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief A timed plain-text cue with borrowed source bytes.
 *
 * \par Details:
 *  Text may be UTF-8, optionally signature-marked, or signature-marked UTF-16.
 *  It is literal text: markup delimiters are escaped. CRLF/CR normalize to LF;
 *  empty payload lines become a space so they cannot terminate the cue.
 *  No pointer is retained after a query or write.
 */
typedef struct Utl_WebVtt_Cue_s
{
    uint64_t      start;         ///< Start milliseconds, including zero.
    uint64_t      end;           ///< End milliseconds, strictly after start.
    const byte_t* text;          ///< Stable borrowed source, NULL only for zero textByteCount.
    uint32_t      textByteCount; ///< Readable source bytes, with no implicit termination.
} Utl_WebVtt_Cue_t;

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
 * \brief Derives the canonical timestamp byte count.
 *
 * \par Requirements:
 *  - 001 Hours shall always be emitted with at least two digits; minutes/seconds with two and milliseconds with three.
 *  - 002 The entire uint64_t millisecond domain shall be representable without arithmetic overflow.
 *
 * \param[in] milliseconds   Timestamp offset
 *
 * \retval    count   Exact bytes, excluding any terminator.
 */
uint32_t Utl_WebVtt_GetTimestampByteCount(uint64_t milliseconds);

/*************************************************************************************************/
/**
 * \brief Derives the complete timing-line byte count.
 *
 * \par Requirements:
 *  - 001 End shall be strictly greater than start; invalid timing shall report zero.
 *  - 002 The line shall include one LF and the space-delimited cue arrow.
 *
 * \param[in] start   Start milliseconds
 * \param[in] end     End milliseconds
 *
 * \retval    nonzero   Exact timing-line byte count.
 * \retval    0         Invalid interval.
 */
uint32_t Utl_WebVtt_GetTimingByteCount(uint64_t start, uint64_t end);

/*************************************************************************************************/
/**
 * \brief Validates and derives the exact plain-text cue byte count.
 *
 * \par Requirements:
 *  - 001 Missing model or nonempty text storage shall assert as programmer misuse.
 *  - 002 Invalid encoding, embedded NUL or invalid timing shall report zero.
 *  - 003 Blank lines and markup delimiters shall be encoded without changing cue boundaries.
 *  - 004 Output count overflow shall fail without allocation, CRT or OS operations.
 *
 * \param[in] cue   Required stable model and borrowed text
 *
 * \retval    nonzero   Exact bytes including the final cue-separating blank line.
 * \retval    0         The cue cannot be serialized.
 */
uint32_t Utl_WebVtt_GetCueByteCount(const Utl_WebVtt_Cue_t* cue);

/*************************************************************************************************/
/**
 * \brief Reads a timestamp prefix with optional hours.
 *
 * \par Requirements:
 *  - 001 Required input/output pointers shall assert; all outputs shall be disjoint from input and each other.
 *  - 002 Hours, when present, shall have at least two digits; minute and second fields shall have exactly two.
 *  - 003 Minutes/seconds shall be below sixty and the fractional field shall contain exactly three digits.
 *  - 004 Malformed, truncated or uint64_t-overflowing values shall preserve both outputs.
 *  - 005 The enclosing grammar shall validate any bytes following the timestamp prefix.
 *
 * \param[out] milliseconds   Required timestamp output
 * \param[out] bytesRead      Required prefix byte-count output
 * \param[in]  input          Required counted input
 * \param[in]  byteCount      Readable bytes
 *
 * \retval     TRUE    A complete representable timestamp prefix was published.
 * \retval     FALSE   Invalid, incomplete or overflowing timestamp; outputs are unchanged.
 */
bool_t Utl_WebVtt_ReadTimestamp(uint64_t* milliseconds, uint32_t* bytesRead, const byte_t* input, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Writes one canonical timestamp into caller-owned storage.
 *
 * \par Requirements:
 *  - 001 Missing output storage shall assert; capacity shall be checked before writing.
 *  - 002 Insufficient capacity shall preserve the output; no terminator shall be appended.
 *  - 003 No allocation, CRT or OS operation shall occur.
 *
 * \param[in]  milliseconds   Timestamp offset
 * \param[out] output         Required writable storage
 * \param[in]  capacity       Available bytes
 *
 * \retval     nonzero   Exact bytes written.
 * \retval     0         Insufficient capacity; output is unchanged.
 */
uint32_t Utl_WebVtt_WriteTimestamp(uint64_t milliseconds, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes a complete cue timing line.
 *
 * \par Requirements:
 *  - 001 Required output storage shall assert; invalid intervals and insufficient capacity shall preserve it.
 *  - 002 One LF shall follow the end timestamp; no terminator shall be appended.
 *
 * \param[in]  start      Start milliseconds
 * \param[in]  end        End milliseconds
 * \param[out] output     Required writable storage
 * \param[in]  capacity   Available bytes
 *
 * \retval     nonzero   Exact bytes written.
 * \retval     0         Invalid timing or insufficient capacity; output is unchanged.
 */
uint32_t Utl_WebVtt_WriteTiming(uint64_t start, uint64_t end, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes one complete plain-text cue transactionally.
 *
 * \par Requirements:
 *  - 001 Required model/output pointers shall assert; the model and source shall remain stable and disjoint from output.
 *  - 002 Complete preflight shall precede writing, preserving output on invalid input or insufficient capacity.
 *  - 003 UTF-8 shall be emitted without BOM or NUL termination; the final blank line shall terminate this cue.
 *  - 004 A file owner shall separately enforce nondecreasing cue starts and write its WebVTT header.
 *  - 005 No allocation, CRT, OS call or retained global state shall occur.
 *
 * \param[in]  cue        Required stable model and borrowed text
 * \param[out] output     Required writable disjoint storage
 * \param[in]  capacity   Available bytes
 *
 * \retval     nonzero   Exact cue bytes written.
 * \retval     0         Invalid cue or insufficient capacity; output is unchanged.
 */
uint32_t Utl_WebVtt_WriteCue(const Utl_WebVtt_Cue_t* cue, byte_t* output, uint32_t capacity);

#endif /* UTL_WEBVTT_H */
