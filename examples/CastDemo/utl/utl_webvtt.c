/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * WebVTT Candidate Recommendation Draft, 20 May 2026, sections 4.1 and 4.2.2:
 * https://www.w3.org/TR/2026/CRD-webvtt1-20260520/
 * This module owns timestamp grammar and the plain-text cue serialization profile.
 * The file owner separately supplies header, ordering, settings and styled text.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_webvtt.h"
#include "utl/utl_unicode.h"
#include "utl/utl_decimal.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define WEBVTT_CHAR_COLON              (':')
#define WEBVTT_CHAR_DOT                ('.')
#define WEBVTT_CHAR_LF                 ('\n')
#define WEBVTT_CHAR_CR                 ('\r')
#define WEBVTT_CHAR_SPACE              (' ')
#define WEBVTT_CHAR_AMP                ('&')
#define WEBVTT_CHAR_LT                 ('<')
#define WEBVTT_CHAR_GT                 ('>')
#define WEBVTT_CHAR_ZERO               ('0')
#define WEBVTT_TIMING_ARROW            " --> "
#define WEBVTT_ESCAPE_AMP              "&amp;"
#define WEBVTT_ESCAPE_LT               "&lt;"
#define WEBVTT_ESCAPE_GT               "&gt;"
#define WEBVTT_PAIR_DIGITS             (2)
#define WEBVTT_FRACTION_DIGITS         (3)
#define WEBVTT_TIMESTAMP_SUFFIX        (10) ///< Two colon-prefixed pairs, dot and three fractional digits.
#define WEBVTT_UINT64_DIGITS           (20)
#define WEBVTT_UTF8_SCALAR_BYTES       (4)
#define WEBVTT_ASCII_MAX               (0x7F)
#define WEBVTT_MILLISECONDS_PER_MINUTE ((uint64_t)UTL_WEBVTT_MILLISECONDS_PER_SECOND * UTL_WEBVTT_SECONDS_PER_MINUTE)
#define WEBVTT_MILLISECONDS_PER_HOUR   (WEBVTT_MILLISECONDS_PER_MINUTE * UTL_WEBVTT_MINUTES_PER_HOUR)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Bounded writer shared by exact preflight and committed output. */
typedef struct Writer_s
{
    byte_t*  output;   ///< NULL only during preflight.
    uint32_t capacity; ///< Maximum representable or writable count.
    uint32_t used;     ///< Count never beyond capacity.
    bool_t   valid;    ///< Sticky success state.
} Writer_t;

/*************************************************************************************************/
/** \brief Private transactional timestamp cursor. */
typedef struct Reader_s
{
    const byte_t* input;     ///< Stable counted source.
    uint32_t      byteCount; ///< Readable extent.
    uint32_t      offset;    ///< Consumed prefix within byteCount.
    bool_t        valid;     ///< Sticky parse validity.
} Reader_t;

/*************************************************************************************************/
/** \brief A decimal field with its grammar-defined minimum width. */
typedef struct Number_s
{
    uint64_t value; ///< Unsigned decimal value.
    uint32_t width; ///< Minimum emitted digits, at most three.
} Number_t;

/*************************************************************************************************/
/** \brief Line-normalization state retained across decoded scalars. */
typedef struct Line_s
{
    bool_t previousCr; ///< The preceding source scalar was CR.
    bool_t hasText;    ///< The current emitted payload line contains text.
} Line_t;

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
 * \brief Establishes a private output cursor.
 *
 * \par Details:
 *  The empty cursor is valid.
 *
 * \par Requirements:
 *  - 001 The required cursor shall be exclusively owned; a missing cursor shall assert.
 *  - 002 A supplied output shall expose capacity writable bytes; NULL shall select measurement.
 *  - 003 Every initialization shall reset the logical count and sticky validity.
 *
 * \param[out] writer     Required exclusively owned cursor
 * \param[out] output     Writable storage or NULL for measurement
 * \param[in]  capacity   Permitted byte count
 *
 * \retval     None
 */
static FORCE_INLINE void InitializeWriter(Writer_t* writer, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Counts or appends a bounded fragment.
 *
 * \par Details:
 *  Count/output advances on success; validity records capacity failure.
 *
 * \par Requirements:
 *  - 001 Input shall remain readable and disjoint from output throughout the call.
 *  - 002 Failure shall latch without changing the prior count or output.
 *
 * \param[in,out] writer      Initialized cursor
 * \param[in]     input       Required readable fragment
 * \param[in]     byteCount   Fragment extent
 *
 * \retval        None
 */
static FORCE_INLINE void AppendBytes(Writer_t* writer, const byte_t* input, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Appends one ASCII grammar byte.
 *
 * \par Details:
 *  Cursor validity records failure.
 *
 * \par Requirements:
 *  - 001 The cursor shall retain its count-within-capacity invariant.
 *  - 002 Insufficient capacity shall latch failure without changing bytes or count.
 *  - 003 Previously failed cursors shall remain unchanged.
 *
 * \param[in,out] writer   Initialized cursor
 * \param[in]     value    Grammar byte
 *
 * \retval        None
 */
static FORCE_INLINE void AppendByte(Writer_t* writer, byte_t value);

/*************************************************************************************************/
/**
 * \brief Appends a decimal field with leading-zero padding.
 *
 * \par Details:
 *  Cursor validity records failure.
 *
 * \par Requirements:
 *  - 001 The required field shall use a minimum width no greater than three.
 *  - 002 The entire uint64_t domain shall be encoded without truncation or allocation.
 *  - 003 Padding and digit copies shall obey the output cursor's capacity and sticky failure.
 *
 * \param[in,out] writer   Initialized cursor
 * \param[in]     number   Required decimal field and minimum width
 *
 * \retval        None
 */
static FORCE_INLINE void AppendNumber(Writer_t* writer, const Number_t* number);

/*************************************************************************************************/
/**
 * \brief Serializes one canonical timestamp.
 *
 * \par Details:
 *  Cursor validity records failure.
 *
 * \par Requirements:
 *  - 001 Hours shall use at least two digits; minutes and seconds exactly two, fractions exactly three.
 *  - 002 Derived components shall not overflow for any uint64_t millisecond input.
 *  - 003 Every emitted byte shall obey the initialized cursor's capacity and sticky failure.
 *
 * \param[in,out] writer         Initialized cursor
 * \param[in]     milliseconds   Timestamp offset
 *
 * \retval        None
 */
static FORCE_INLINE void AppendTimestamp(Writer_t* writer, uint64_t milliseconds);

/*************************************************************************************************/
/**
 * \brief Serializes a validated timing line.
 *
 * \par Details:
 *  Invalid intervals or insufficient output latch failure.
 *
 * \par Requirements:
 *  - 001 End shall be strictly after start; invalid intervals shall latch failure without output.
 *  - 002 Canonical endpoints shall be separated by the cue arrow and followed by one LF.
 *  - 003 A previously failed cursor shall remain unchanged.
 *
 * \param[in,out] writer   Initialized cursor
 * \param[in]     start    Start milliseconds
 * \param[in]     end      End milliseconds
 *
 * \retval        None
 */
static FORCE_INLINE void AppendTiming(Writer_t* writer, uint64_t start, uint64_t end);

/*************************************************************************************************/
/**
 * \brief Escapes a literal scalar for WebVTT cue text.
 *
 * \par Details:
 *  Cursor validity records failure.
 *
 * \par Requirements:
 *  - 001 The scalar shall be nonzero and valid; newline handling belongs to AppendText.
 *
 * \param[in,out] writer   Initialized cursor
 * \param[in]     scalar   Valid non-newline scalar
 *
 * \retval        None
 */
static FORCE_INLINE void AppendScalar(Writer_t* writer, uint32_t scalar);

/*************************************************************************************************/
/**
 * \brief Normalizes one source scalar into an escaped, nonempty payload line.
 *
 * \par Details:
 *  Line state advances and writer validity records failure.
 *
 * \par Requirements:
 *  - 001 Source scalars shall be valid and nonzero.
 *  - 002 CRLF shall emit one LF; CR and LF alone shall each emit one LF.
 *  - 003 An empty payload line shall contain a space before its LF.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in,out] line     Initialized line state
 * \param[in]     scalar   Decoded source scalar
 *
 * \retval        None
 */
static FORCE_INLINE void AppendLineScalar(Writer_t* writer, Line_t* line, uint32_t scalar);

/*************************************************************************************************/
/**
 * \brief Decodes and serializes complete literal text with cue-safe line boundaries.
 *
 * \par Details:
 *  Text and final cue separator are appended or failure is latched.
 *
 * \par Requirements:
 *  - 001 Every iteration shall consume one scalar or stop on end/failure.
 *  - 002 Invalid encoding and embedded NUL shall fail; callers preflight before committed output.
 *
 * \param[in,out] writer      Initialized cursor
 * \param[in]     input       Stable borrowed text; NULL only for zero byteCount
 * \param[in]     byteCount   Readable bytes
 *
 * \retval        None
 */
static FORCE_INLINE void AppendText(Writer_t* writer, const byte_t* input, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Reads a bounded decimal field into private staging storage.
 *
 * \par Requirements:
 *  - 001 The caller shall supply private value storage and a cursor within its readable extent.
 *  - 002 Nonzero width shall reject both shorter and longer numeric fields.
 *  - 003 Arithmetic overflow and missing digits shall latch failure before advancing the cursor.
 *  - 004 Failed private field values shall never be published by the enclosing parser.
 *
 * \param[in,out] reader   Initialized private cursor
 * \param[out]    value    Required private field staging, ignored after failure
 * \param[in]     width    Exact width, or zero for variable-width hours/minutes
 *
 * \retval        count   Parsed digits; validity separately records width or value failure.
 */
static FORCE_INLINE uint32_t ReadNumber(Reader_t* reader, uint64_t* value, uint32_t width);

/*************************************************************************************************/
/**
 * \brief Consumes one required timestamp delimiter.
 *
 * \par Details:
 *  The delimiter is consumed or failure is latched.
 *
 * \par Requirements:
 *  - 001 The cursor shall remain within the counted readable extent.
 *  - 002 Missing or mismatched delimiters shall latch failure; failed cursors shall remain unchanged.
 *
 * \param[in,out] reader      Initialized private cursor
 * \param[in]     delimiter   Required grammar byte
 *
 * \retval        None
 */
static FORCE_INLINE void ReadDelimiter(Reader_t* reader, byte_t delimiter);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Required private storage is established by each public entry point.
 */
static FORCE_INLINE void InitializeWriter(Writer_t* writer, byte_t* output, uint32_t capacity)
{
    LOGASSERT(NULL != writer);

    writer->output   = output;
    writer->capacity = capacity;
    writer->used     = 0;
    writer->valid    = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The copy receives a zero extent when the fragment does not fit. The retained
 *  count advances by that same bounded extent; failure leaves all output intact.
 */
static FORCE_INLINE void AppendBytes(Writer_t* writer, const byte_t* input, uint32_t byteCount)
{
    bool_t   fits;
    uint32_t copied;
    bool_t   writing;

    if (writer->valid)
    {
        fits    = (byteCount <= writer->capacity - writer->used);
        copied  = fits ? byteCount : 0;
        writing = (NULL != writer->output);
        if (writing)
        {
            CRT_Memory_Copy(writer->output + writer->used, input, copied);
        }
        writer->used  += copied;
        writer->valid  = fits;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A direct bounded byte store avoids a general memory-copy call for grammar bytes.
 */
static FORCE_INLINE void AppendByte(Writer_t* writer, byte_t value)
{
    bool_t fits;
    bool_t writing;

    if (writer->valid)
    {
        fits          = (writer->used < writer->capacity);
        writer->valid = fits;
        if (fits)
        {
            writing = (NULL != writer->output);
            if (writing)
            {
                writer->output[writer->used] = value;
            }
            ++writer->used;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Decimal conversion uses a fixed twenty-byte local. Padding is bounded by the
 *  timestamp grammar, independently of the value's decimal length.
 */
static FORCE_INLINE void AppendNumber(Writer_t* writer, const Number_t* number)
{
    uint32_t count;
    byte_t   digits[WEBVTT_UINT64_DIGITS];
    uint32_t padding;

    LOGASSERT(NULL != number);
    LOGASSERT(WEBVTT_FRACTION_DIGITS >= number->width);

    count = Utl_Decimal_WriteUint64(number->value, digits, sizeof(digits));
    for (padding = count; padding < number->width; ++padding)
    {
        AppendByte(writer, WEBVTT_CHAR_ZERO);
    }
    AppendBytes(writer, digits, count);
    LOGASSERT(0 < count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Division and remainder derive each field without expanding the input value.
 */
static FORCE_INLINE void AppendTimestamp(Writer_t* writer, uint64_t milliseconds)
{
    Number_t number;

    number.value = milliseconds / WEBVTT_MILLISECONDS_PER_HOUR;
    number.width = WEBVTT_PAIR_DIGITS;
    AppendNumber(writer, &number);
    AppendByte(writer, WEBVTT_CHAR_COLON);
    number.value = milliseconds / WEBVTT_MILLISECONDS_PER_MINUTE % UTL_WEBVTT_MINUTES_PER_HOUR;
    AppendNumber(writer, &number);
    AppendByte(writer, WEBVTT_CHAR_COLON);
    number.value = milliseconds / UTL_WEBVTT_MILLISECONDS_PER_SECOND % UTL_WEBVTT_SECONDS_PER_MINUTE;
    AppendNumber(writer, &number);
    AppendByte(writer, WEBVTT_CHAR_DOT);
    number.value = milliseconds % UTL_WEBVTT_MILLISECONDS_PER_SECOND;
    number.width = WEBVTT_FRACTION_DIGITS;
    AppendNumber(writer, &number);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A timing line includes its LF so callers cannot accidentally merge it with text.
 */
static FORCE_INLINE void AppendTiming(Writer_t* writer, uint64_t start, uint64_t end)
{
    bool_t ordered;

    ordered       = (start < end);
    writer->valid = writer->valid && ordered;
    if (writer->valid)
    {
        AppendTimestamp(writer, start);
        AppendBytes(writer, (const byte_t*)WEBVTT_TIMING_ARROW, sizeof(WEBVTT_TIMING_ARROW) - 1);
        AppendTimestamp(writer, end);
        AppendByte(writer, WEBVTT_CHAR_LF);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Escaping greater-than also prevents a literal cue arrow in text. Non-ASCII
 *  scalars are re-encoded directly into a four-byte local, without temporary strings.
 */
static FORCE_INLINE void AppendScalar(Writer_t* writer, uint32_t scalar)
{
    uint32_t count;
    byte_t   encoded[WEBVTT_UTF8_SCALAR_BYTES];

    switch (scalar)
    {
    case WEBVTT_CHAR_AMP:
        AppendBytes(writer, (const byte_t*)WEBVTT_ESCAPE_AMP, sizeof(WEBVTT_ESCAPE_AMP) - 1);
        break;
    case WEBVTT_CHAR_LT:
        AppendBytes(writer, (const byte_t*)WEBVTT_ESCAPE_LT, sizeof(WEBVTT_ESCAPE_LT) - 1);
        break;
    case WEBVTT_CHAR_GT:
        AppendBytes(writer, (const byte_t*)WEBVTT_ESCAPE_GT, sizeof(WEBVTT_ESCAPE_GT) - 1);
        break;
    default:
        if (WEBVTT_ASCII_MAX >= scalar)
        {
            AppendByte(writer, (byte_t)scalar);
        }
        else
        {
            count = Utl_Unicode_WriteUtf8(scalar, encoded, sizeof(encoded));
            AppendBytes(writer, (const byte_t*)encoded, count);
            LOGASSERT(0 < count);
        }
        break;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Source CRLF is folded independently of output escaping. The line state also
 *  distinguishes payload boundaries from empty lines within a cue.
 */
static FORCE_INLINE void AppendLineScalar(Writer_t* writer, Line_t* line, uint32_t scalar)
{
    bool_t is_cr;
    bool_t is_lf;
    bool_t skip_lf;
    bool_t newline;

    is_cr            = (WEBVTT_CHAR_CR == scalar);
    is_lf            = (WEBVTT_CHAR_LF == scalar);
    skip_lf          = line->previousCr && is_lf;
    line->previousCr = is_cr;
    newline          = is_cr || is_lf;
    if (!skip_lf)
    {
        if (newline)
        {
            if (!line->hasText)
            {
                AppendByte(writer, WEBVTT_CHAR_SPACE);
            }
            AppendByte(writer, WEBVTT_CHAR_LF);
            line->hasText = FALSE;
        }
        else
        {
            AppendScalar(writer, scalar);
            line->hasText = TRUE;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A space protects each empty line, including leading and trailing empty lines.
 *  Empty text has no payload and needs only the final separator after the timing LF.
 */
static FORCE_INLINE void AppendText(Writer_t* writer, const byte_t* input, uint32_t byteCount)
{
    Utl_Unicode_Reader_t     reader;
    Line_t                   line;
    bool_t                   has_text;
    Utl_Unicode_ReadResult_t result;
    uint32_t                 scalar;

    Utl_Unicode_InitializeReader(&reader, input, byteCount);
    line.previousCr = FALSE;
    line.hasText    = FALSE;
    has_text        = FALSE;
    for (;;)
    {
        if (!writer->valid)
        {
            break;
        }
        result = Utl_Unicode_ReadScalar(&reader, &scalar);
        if (UTL_UNICODE_READ_END == result)
        {
            break;
        }
        writer->valid = (UTL_UNICODE_READ_SCALAR == result);
        if (writer->valid)
        {
            writer->valid = (0 != scalar);
        }
        if (writer->valid)
        {
            has_text = TRUE;
            AppendLineScalar(writer, &line, scalar);
        }
    }
    if (has_text)
    {
        if (!line.hasText)
        {
            AppendByte(writer, WEBVTT_CHAR_SPACE);
        }
        AppendByte(writer, WEBVTT_CHAR_LF);
    }
    AppendByte(writer, WEBVTT_CHAR_LF);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Fixed-width fields admit one extra digit for inspection so oversized fields
 *  fail instead of being silently truncated into a valid-looking prefix.
 */
static FORCE_INLINE uint32_t ReadNumber(Reader_t* reader, uint64_t* value, uint32_t width)
{
    uint32_t count;
    uint32_t available;
    bool_t   fixed_width;
    bool_t   width_valid;

    count = 0;
    if (reader->valid)
    {
        available   = reader->byteCount - reader->offset;
        fixed_width = (0 < width);
        if (fixed_width && (width < available))
        {
            available = width + 1;
        }
        reader->valid = Utl_Decimal_ReadUint64(value, &count, reader->input + reader->offset, available);
        if (reader->valid)
        {
            width_valid     = (!fixed_width) || (count == width);
            reader->valid   = width_valid;
            reader->offset += count;
        }
    }

    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  End-of-input is checked before loading the required delimiter.
 */
static FORCE_INLINE void ReadDelimiter(Reader_t* reader, byte_t delimiter)
{
    if (reader->valid)
    {
        reader->valid = (reader->offset < reader->byteCount);
        if (reader->valid)
        {
            reader->valid = (delimiter == reader->input[reader->offset]);
            ++reader->offset;
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The fixed suffix accounts for every field after the hours component.
 */
extern FORCE_INLINE uint32_t Utl_WebVtt_GetTimestampByteCount(uint64_t milliseconds)
{
    uint32_t hours;

    hours = Utl_Decimal_GetUint64ByteCount(milliseconds / WEBVTT_MILLISECONDS_PER_HOUR);
    if (WEBVTT_PAIR_DIGITS > hours)
    {
        hours = WEBVTT_PAIR_DIGITS;
    }

    return hours + WEBVTT_TIMESTAMP_SUFFIX;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each timestamp is bounded by the uint64_t time domain; the fixed sum fits uint32_t.
 */
uint32_t Utl_WebVtt_GetTimingByteCount(uint64_t start, uint64_t end)
{
    uint32_t count;

    count = 0;
    if (start < end)
    {
        count = Utl_WebVtt_GetTimestampByteCount(start) + Utl_WebVtt_GetTimestampByteCount(end) +
                (uint32_t)(sizeof(WEBVTT_TIMING_ARROW) - 1) + 1;
    }

    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Preflight and writing use the same decoder and escaping rules, without output mutation.
 */
uint32_t Utl_WebVtt_GetCueByteCount(const Utl_WebVtt_Cue_t* cue)
{
    bool_t   has_text;
    Writer_t writer;
    uint32_t count;

    LOGASSERT(NULL != cue);

    has_text = (NULL != cue->text) || (0 == cue->textByteCount);
    LOGASSERT(has_text);
    InitializeWriter(&writer, NULL, UINT32_MAX);
    AppendTiming(&writer, cue->start, cue->end);
    AppendText(&writer, cue->text, cue->textByteCount);
    count = writer.valid ? writer.used : 0;

    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The first field is hours only when a second colon follows. Arithmetic is
 *  staged after width/range validation; overflow never wraps into a valid time.
 */
extern FORCE_INLINE bool_t Utl_WebVtt_ReadTimestamp(uint64_t*     milliseconds,
                                                    uint32_t*     bytesRead,
                                                    const byte_t* input,
                                                    uint32_t      byteCount)
{
    Reader_t reader;
    uint64_t first;
    uint64_t middle;
    uint64_t seconds;
    uint64_t fraction;
    uint32_t first_digits;
    bool_t   has_hours;
    uint64_t hours;
    uint64_t minutes;
    bool_t   valid_minutes;
    bool_t   valid_seconds;
    bool_t   hours_fit;
    uint64_t base;
    uint64_t remainder;
    bool_t   total_fits;

    LOGASSERT(NULL != milliseconds);
    LOGASSERT(NULL != bytesRead);
    LOGASSERT(NULL != input);

    reader.input     = input;
    reader.byteCount = byteCount;
    reader.offset    = 0;
    reader.valid     = TRUE;
    first            = 0;
    middle           = 0;
    seconds          = 0;
    fraction         = 0;
    first_digits     = ReadNumber(&reader, &first, 0);
    reader.valid     = reader.valid && (WEBVTT_PAIR_DIGITS <= first_digits);
    ReadDelimiter(&reader, WEBVTT_CHAR_COLON);
    (void)ReadNumber(&reader, &middle, WEBVTT_PAIR_DIGITS);
    has_hours = FALSE;
    if (reader.valid && (reader.offset < byteCount))
    {
        has_hours = (WEBVTT_CHAR_COLON == input[reader.offset]);
    }
    if (has_hours)
    {
        hours   = first;
        minutes = middle;
        ReadDelimiter(&reader, WEBVTT_CHAR_COLON);
        (void)ReadNumber(&reader, &seconds, WEBVTT_PAIR_DIGITS);
    }
    else
    {
        hours        = 0;
        minutes      = first;
        seconds      = middle;
        reader.valid = reader.valid && (WEBVTT_PAIR_DIGITS == first_digits);
    }
    ReadDelimiter(&reader, WEBVTT_CHAR_DOT);
    (void)ReadNumber(&reader, &fraction, WEBVTT_FRACTION_DIGITS);
    valid_minutes = (UTL_WEBVTT_MINUTES_PER_HOUR > minutes);
    valid_seconds = (UTL_WEBVTT_SECONDS_PER_MINUTE > seconds);
    hours_fit     = (UINT64_MAX / WEBVTT_MILLISECONDS_PER_HOUR >= hours);
    reader.valid  = reader.valid && valid_minutes && valid_seconds && hours_fit;
    if (reader.valid)
    {
        base = hours * WEBVTT_MILLISECONDS_PER_HOUR;
        remainder =
            (minutes * WEBVTT_MILLISECONDS_PER_MINUTE) + (seconds * UTL_WEBVTT_MILLISECONDS_PER_SECOND) + fraction;
        total_fits   = (UINT64_MAX - remainder >= base);
        reader.valid = total_fits;
        if (total_fits)
        {
            (*milliseconds) = base + remainder;
            (*bytesRead)    = reader.offset;
        }
    }

    return reader.valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Preflight establishes sufficient capacity before any writer operation.
 */
uint32_t Utl_WebVtt_WriteTimestamp(uint64_t milliseconds, byte_t* output, uint32_t capacity)
{
    uint32_t required;
    uint32_t written;
    Writer_t writer;

    LOGASSERT(NULL != output);

    required = Utl_WebVtt_GetTimestampByteCount(milliseconds);
    written  = 0;
    if (required <= capacity)
    {
        InitializeWriter(&writer, output, capacity);
        AppendTimestamp(&writer, milliseconds);
        LOGASSERT(writer.valid);
        LOGASSERT(required == writer.used);
        written = writer.used;
    }

    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The timing query rejects invalid intervals before output is made available.
 */
uint32_t Utl_WebVtt_WriteTiming(uint64_t start, uint64_t end, byte_t* output, uint32_t capacity)
{
    uint32_t required;
    bool_t   fits;
    uint32_t written;
    Writer_t writer;

    LOGASSERT(NULL != output);

    required = Utl_WebVtt_GetTimingByteCount(start, end);
    fits     = (0 < required) && (required <= capacity);
    written  = 0;
    if (fits)
    {
        InitializeWriter(&writer, output, capacity);
        AppendTiming(&writer, start, end);
        LOGASSERT(writer.valid);
        LOGASSERT(required == writer.used);
        written = writer.used;
    }

    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Stable disjoint source storage makes the committed pass identical to preflight.
 */
uint32_t Utl_WebVtt_WriteCue(const Utl_WebVtt_Cue_t* cue, byte_t* output, uint32_t capacity)
{
    uint32_t required;
    bool_t   fits;
    uint32_t written;
    Writer_t writer;

    LOGASSERT(NULL != cue);
    LOGASSERT(NULL != output);

    required = Utl_WebVtt_GetCueByteCount(cue);
    fits     = (0 < required) && (required <= capacity);
    written  = 0;
    if (fits)
    {
        InitializeWriter(&writer, output, capacity);
        AppendTiming(&writer, cue->start, cue->end);
        AppendText(&writer, cue->text, cue->textByteCount);
        LOGASSERT(writer.valid);
        LOGASSERT(required == writer.used);
        written = writer.used;
    }

    return written;
}
