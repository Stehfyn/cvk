#ifndef UTL_UNICODE_H
#define UTL_UNICODE_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * RFC 3629 sections 3 and 4: shortest-form UTF-8 and scalar restrictions.
 * https://www.rfc-editor.org/rfc/rfc3629.html
 * Unicode Standard, chapter 3, UTF-16 surrogate mapping (D91).
 * https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-3/
 *
 * Scalar codecs use native-order UTF-16 units and preserve U+FEFF as data.
 * The caller-owned byte reader additionally interprets one leading UTF-8/UTF-16
 * signature and handles either UTF-16 byte order. No operation allocates, calls
 * CRT/OS routines or retains global mutable state.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UTL_UNICODE_TERMINATOR    (0)          ///< Zero code unit terminating a string.
#define UTL_UNICODE_NO_TERMINATOR (UINT32_MAX) ///< No zero unit in the supplied readable bound.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Encoding selected by the optional leading signature of a byte stream. */
typedef enum Utl_Unicode_Encoding_e
{
    UTL_UNICODE_UTF8,
    UTL_UNICODE_UTF16_LE,
    UTL_UNICODE_UTF16_BE
} Utl_Unicode_Encoding_t;

/*************************************************************************************************/
/** \brief Result of one bounded scalar read. */
typedef enum Utl_Unicode_ReadResult_e
{
    UTL_UNICODE_READ_END,
    UTL_UNICODE_READ_SCALAR,
    UTL_UNICODE_READ_INVALID
} Utl_Unicode_ReadResult_t;

/*************************************************************************************************/
/**
 * \brief Caller-owned cursor over stable UTF-8 or signature-marked UTF-16 bytes.
 *
 * \par Details:
 *  The initializer consumes at most one leading signature. Later U+FEFF and
 *  U+0000 are ordinary scalars. No native-endian or alignment assumption applies
 *  to source storage. The caller excludes concurrent access to this cursor.
 */
typedef struct Utl_Unicode_Reader_s
{
    const byte_t*          next;               ///< First unconsumed borrowed byte, NULL only for empty input.
    uint32_t               remainingByteCount; ///< Readable extent starting at next.
    Utl_Unicode_Encoding_t encoding;           ///< Encoding selected during initialization.
} Utl_Unicode_Reader_t;

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
 * \brief Initializes a cursor over UTF-8 or signature-marked UTF-16 byte storage.
 *
 * \par Details:
 *  A cursor is ready for queries and reads.
 *
 * \par Requirements:
 *  - 001 Missing cursor or nonempty source storage shall assert as programmer misuse.
 *  - 002 UTF-8 shall be selected when no supported leading signature is present.
 *  - 003 Exactly one UTF-8, UTF-16LE or UTF-16BE signature shall be consumed when present.
 *  - 004 Invalid or truncated payload encodings shall be reported by ReadScalar, not by initialization assertions.
 *  - 005 Initialization shall inspect at most three bytes and allocate nothing.
 *
 * \param[out] reader      Required cursor, disjoint from input
 * \param[in]  data        Stable input, NULL only for an empty span
 * \param[in]  byteCount   Readable bytes retained until the cursor is no longer used
 *
 * \retval     None No value is returned.
 */
void Utl_Unicode_InitializeReader(Utl_Unicode_Reader_t* reader, const byte_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Gets bytes not yet consumed by a text cursor.
 *
 * \par Requirements:
 *  - 001 The caller shall retain an initialized cursor and its borrowed source.
 *  - 002 No input bytes shall be inspected.
 *
 * \param[in] reader   Required initialized cursor
 *
 * \retval    count   Unconsumed bytes, including malformed or incomplete trailing bytes.
 */
uint32_t Utl_Unicode_GetRemainingByteCount(const Utl_Unicode_Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Checks whether a code point is a Unicode scalar value.
 *
 * \par Requirements:
 *  - 001 Surrogates and values above U+10FFFF shall be rejected.
 *  - 002 No allocation, CRT or OS calls shall occur.
 *
 * \param[in] value   Candidate code point
 *
 * \retval    TRUE    For a scalar, including noncharacters and U+0000.
 * \retval    FALSE   Otherwise.
 */
bool_t Utl_Unicode_IsScalar(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Measures a terminated UTF-8 string within a readable bound.
 *
 * \par Requirements:
 *  - 001 Missing required source storage shall assert as programmer misuse.
 *  - 002 At most capacity bytes shall be inspected; the first zero byte shall end the scan.
 *  - 003 Scalar validation shall remain separate; the result shall exclude the terminator.
 *  - 004 No allocation, CRT or OS calls shall occur.
 *
 * \param[in] text       Required string, readable until its terminator or capacity
 * \param[in] capacity   Maximum bytes to inspect, including a possible terminator
 *
 * \retval    0..capacity-1               Bytes before the first zero.
 * \retval    UTL_UNICODE_NO_TERMINATOR   No zero byte within the supplied bound.
 */
uint32_t Utl_Unicode_GetUtf8StringByteCount(const char_t* text, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Measures a terminated UTF-16 string within a readable bound.
 *
 * \par Details:
 *  This query counts native-order units and stops at the first zero unit. It does not
 *  validate scalar syntax; use IsValidUtf16 when that is required.
 *
 * \par Requirements:
 *  - 001 A missing source pointer shall assert as programmer misuse.
 *  - 002 At most capacity units shall be inspected; the terminator shall not count toward the result.
 *  - 003 Missing termination shall yield UTL_UNICODE_NO_TERMINATOR without reading past capacity.
 *
 * \param[in] units      Non-NULL readable native-order units
 * \param[in] capacity   Maximum readable units, including a possible terminator
 *
 * \retval    0..capacity-1               Number of units before the first zero.
 * \retval    UTL_UNICODE_NO_TERMINATOR   No zero unit occurs within capacity.
 */
uint32_t Utl_Unicode_GetUtf16StringUnitCount(const uint16_t* units, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Derives the shortest UTF-8 width of one scalar.
 *
 * \par Requirements:
 *  - 001 Invalid scalars shall yield zero.
 *  - 002 No allocation, CRT or OS calls shall occur.
 *
 * \param[in] value   Candidate scalar
 *
 * \retval    1..4   Number of UTF-8 bytes required for the scalar.
 * \retval    0      The value is not a Unicode scalar.
 */
uint32_t Utl_Unicode_GetUtf8ByteCount(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Derives the UTF-16 code-unit count of one scalar.
 *
 * \par Requirements:
 *  - 001 Invalid scalars shall yield zero.
 *  - 002 No allocation, CRT or OS calls shall occur.
 *
 * \param[in] value   Candidate scalar
 *
 * \retval    1..2   Number of UTF-16 code units required for the scalar.
 * \retval    0      The value is not a Unicode scalar.
 */
uint32_t Utl_Unicode_GetUtf16UnitCount(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Validates an entire counted UTF-8 span.
 *
 * \par Requirements:
 *  - 001 Every scalar shall have shortest-form UTF-8 syntax.
 *  - 002 Truncation, surrogates, stray continuation bytes and out-of-range values shall fail.
 *  - 003 No allocation, CRT or OS calls shall occur.
 *
 * \param[in] buffer      Readable bytes; NULL is permitted only for an empty span
 * \param[in] byteCount   Number of readable bytes
 *
 * \retval    TRUE    For a fully valid span, including an empty span.
 * \retval    FALSE   Otherwise.
 */
bool_t Utl_Unicode_IsValidUtf8(const byte_t* buffer, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Validates an entire counted UTF-16 code-unit span.
 *
 * \par Requirements:
 *  - 001 Every high surrogate shall be followed by a low surrogate.
 *  - 002 Unpaired low surrogates and truncated pairs shall fail.
 *  - 003 No allocation, CRT or OS calls shall occur.
 *
 * \param[in] units       Readable native-order units; NULL is permitted for an empty span
 * \param[in] unitCount   Number of readable uint16_t units
 *
 * \retval    TRUE    For a fully valid span, including an empty span.
 * \retval    FALSE   Otherwise.
 */
bool_t Utl_Unicode_IsValidUtf16(const uint16_t* units, uint32_t unitCount);

/*************************************************************************************************/
/**
 * \brief Reads one scalar from counted UTF-8 input.
 *
 * \par Requirements:
 *  - 001 At most four bytes shall be inspected.
 *  - 002 Malformed or truncated input shall preserve both outputs.
 *  - 003 Outputs shall be disjoint from the input.
 *  - 004 No allocation, CRT or OS calls shall occur.
 *
 * \param[out] scalar      Non-NULL scalar result
 * \param[out] bytesRead   Non-NULL consumed-byte count, disjoint from scalar
 * \param[in]  buffer      Non-NULL readable input
 * \param[in]  byteCount   Readable byte count
 *
 * \retval     TRUE    When one scalar was published.
 * \retval     FALSE   Otherwise.
 */
bool_t Utl_Unicode_ReadUtf8(uint32_t* scalar, uint32_t* bytesRead, const byte_t* buffer, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Reads one scalar from native-order UTF-16 units.
 *
 * \par Requirements:
 *  - 001 At most two units shall be inspected.
 *  - 002 Unpaired surrogates shall preserve both outputs.
 *  - 003 Outputs shall be disjoint from input.
 *  - 004 No allocation, CRT or OS calls shall occur.
 *
 * \param[out] scalar      Non-NULL scalar result
 * \param[out] unitsRead   Non-NULL consumed-unit count, disjoint from scalar
 * \param[in]  units       Non-NULL readable native-order units
 * \param[in]  unitCount   Readable unit count
 *
 * \retval     TRUE    When one scalar was published.
 * \retval     FALSE   Otherwise.
 */
bool_t Utl_Unicode_ReadUtf16(uint32_t* scalar, uint32_t* unitsRead, const uint16_t* units, uint32_t unitCount);

/*************************************************************************************************/
/**
 * \brief Reads one scalar from a signature-aware byte cursor.
 *
 * \par Requirements:
 *  - 001 Missing reader or result storage shall assert; output shall be disjoint from reader and source.
 *  - 002 At most four source bytes shall be inspected, without aligned native-unit loads.
 *  - 003 Invalid input or end of input shall preserve both the cursor and scalar output.
 *  - 004 Success shall advance by one complete scalar; NUL and noninitial U+FEFF shall be preserved.
 *  - 005 No allocation, CRT, OS call or retained global state shall occur.
 *
 * \param[in,out] reader   Required initialized cursor with stable borrowed bytes
 * \param[out]    scalar   Required disjoint output, written only for READ_SCALAR
 *
 * \retval        UTL_UNICODE_READ_END       No bytes remain; repeated reads remain at end.
 * \retval        UTL_UNICODE_READ_SCALAR    One complete scalar was published.
 * \retval        UTL_UNICODE_READ_INVALID   Malformed/truncated encoding; repeated reads fail at the same position.
 */
Utl_Unicode_ReadResult_t Utl_Unicode_ReadScalar(Utl_Unicode_Reader_t* reader, uint32_t* scalar);

/*************************************************************************************************/
/**
 * \brief Writes one scalar using shortest-form UTF-8.
 *
 * \par Requirements:
 *  - 001 Invalid scalars and insufficient capacity shall leave the output unchanged.
 *  - 002 No terminator or BOM shall be appended.
 *  - 003 No allocation, CRT or OS calls shall occur.
 *
 * \param[in]  scalar     Scalar to encode
 * \param[out] buffer     Non-NULL writable bytes
 * \param[in]  capacity   Available byte count
 *
 * \retval     1..4   Number of UTF-8 bytes written.
 * \retval     0      The scalar, output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t Utl_Unicode_WriteUtf8(uint32_t scalar, byte_t* buffer, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes one scalar using native-order UTF-16 units.
 *
 * \par Requirements:
 *  - 001 Invalid scalars and insufficient capacity shall leave the output unchanged.
 *  - 002 No terminator or BOM shall be appended.
 *  - 003 No allocation, CRT or OS calls shall occur.
 *
 * \param[in]  scalar     Scalar to encode
 * \param[out] units      Non-NULL writable native-order units
 * \param[in]  capacity   Available uint16_t unit count
 *
 * \retval     1..2   Number of UTF-16 code units written.
 * \retval     0      The scalar, output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t Utl_Unicode_WriteUtf16(uint32_t scalar, uint16_t* units, uint32_t capacity);

#endif /* UTL_UNICODE_H */
