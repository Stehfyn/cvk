/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Matroska SRT Subtitles: https://www.matroska.org/technical/subtitles.html#srt-subtitles
 * Library of Congress fdd000569: https://www.loc.gov/preservation/digital/formats/fdd/fdd000569.shtml
 * The header documents the explicit compatibility profile and remaining text/extension responsibilities.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_srt.h"
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

#define SRT_CHAR_COLON              (':')
#define SRT_CHAR_COMMA              (',')
#define SRT_CHAR_DOT                ('.')
#define SRT_CHAR_CR                 ('\r')
#define SRT_CHAR_LF                 ('\n')
#define SRT_ARROW                   (" --> ")
#define SRT_ARROW_CORE              ("-->")
#define SRT_PAIR_DIGITS             (2)
#define SRT_FRACTION_DIGITS         (3)
#define SRT_COMPONENT_MAX           (59)
#define SRT_FRACTION_MAX            (999)
#define SRT_MILLISECONDS_PER_SECOND (1000)
#define SRT_MILLISECONDS_PER_MINUTE (60000)
#define SRT_MILLISECONDS_PER_HOUR   (3600000)
#define SRT_SIGNATURE_BYTES         (3)
#define SRT_SIGNATURE_FIRST         (0xEF)
#define SRT_SIGNATURE_SECOND        (0xBB)
#define SRT_SIGNATURE_LAST          (0xBF)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Private cursor with sticky validity. */
typedef struct Cursor_s
{
    const byte_t* input;     ///< Stable borrowed source.
    uint32_t      byteCount; ///< Counted readable extent.
    uint32_t      offset;    ///< Next byte, never beyond byteCount.
    bool_t        valid;     ///< Sticky private validity.
} Cursor_t;

/*************************************************************************************************/
/** \brief Width and value constraints for one decimal time field. */
typedef struct NumberFormat_s
{
    uint32_t minimumDigits; ///< Smallest lexical field width.
    uint32_t maximumDigits; ///< Largest inspected valid width.
    uint64_t maximumValue;  ///< Largest permitted field value.
} NumberFormat_t;

/*************************************************************************************************/
/** \brief Parsed time-field value and lexical width. */
typedef struct Number_s
{
    uint64_t value;  ///< Parsed numeric value.
    uint32_t digits; ///< Consumed ASCII digits.
} Number_t;

/*************************************************************************************************/
/** \brief Borrowed line plus its counted source end, independent of pointer-difference width. */
typedef struct Line_s
{
    Utl_Srt_Span_t text; ///< Content excluding the line ending.
    uint32_t       end;  ///< Counted end offset before the line ending.
} Line_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Timestamp decimal productions and representable-hour bound.
 *
 * \par Details:
 *  The hour-width lower bound is lexical; its upper value bound prevents
 *  millisecond multiplication from overflowing. Pair/fraction widths are exact.
 */
static const NumberFormat_t c_hoursFormat    = {SRT_PAIR_DIGITS, UINT32_MAX, UINT64_MAX / SRT_MILLISECONDS_PER_HOUR};
static const NumberFormat_t c_pairFormat     = {SRT_PAIR_DIGITS, SRT_PAIR_DIGITS, SRT_COMPONENT_MAX};
static const NumberFormat_t c_fractionFormat = {SRT_FRACTION_DIGITS, SRT_FRACTION_DIGITS, SRT_FRACTION_MAX};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Establishes a private counted cursor.
 *
 * \par Details:
 *  The cursor is valid and at offset zero.
 *
 * \par Requirements:
 *  - 001 Required cursor and nonempty source shall be supplied; misuse shall assert.
 *  - 002 Initialization shall reset offset and sticky validity without inspecting input.
 *
 * \param[out] cursor      Required private staging cursor
 * \param[in]  input       Stable counted source, NULL only when empty
 * \param[in]  byteCount   Readable extent
 *
 * \retval     None
 */
static FORCE_INLINE void InitializeCursor(Cursor_t* cursor, const byte_t* input, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Establishes empty private cue staging.
 *
 * \par Details:
 *  Every span and timestamp has a defined empty value.
 *
 * \par Requirements:
 *  - 001 The required cue shall be exclusively owned.
 *  - 002 All fields shall be initialized before conditional parsing.
 *
 * \param[out] cue   Required private model
 *
 * \retval     None
 */
static FORCE_INLINE void InitializeCue(Utl_Srt_Cue_t* cue);

/*************************************************************************************************/
/**
 * \brief Checks whether a line contains the cue-arrow token.
 *
 * \par Requirements:
 *  - 001 The counted span shall remain readable and stable.
 *  - 002 Every comparison shall fit entirely inside the supplied extent.
 *
 * \param[in] line   Required counted line
 *
 * \retval    TRUE    An arrow occurs in the line.
 * \retval    FALSE   No complete arrow occurs.
 */
static FORCE_INLINE bool_t HasArrow(const Utl_Srt_Span_t* line);

/*************************************************************************************************/
/**
 * \brief Parses one bounded-width decimal field into private staging.
 *
 * \par Details:
 *  Cursor validity records the result.
 *
 * \par Requirements:
 *  - 001 The initialized cursor shall remain within its readable extent.
 *  - 002 Field width, value and decimal overflow shall be validated before cursor advancement.
 *  - 003 Fixed-width fields shall inspect one extra digit when available to reject oversized numbers.
 *
 * \param[in,out] cursor   Initialized private cursor
 * \param[in]     format   Required field rules
 * \param[out]    number   Required private result, ignored after failure
 *
 * \retval        None
 */
static FORCE_INLINE void ReadNumber(Cursor_t* cursor, const NumberFormat_t* format, Number_t* number);

/*************************************************************************************************/
/**
 * \brief Consumes a timestamp separator.
 *
 * \par Details:
 *  A delimiter is consumed or failure is latched.
 *
 * \par Requirements:
 *  - 001 The cursor shall be initialized and remain within its readable extent.
 *  - 002 Comma shall additionally accept period for the explicit compatibility profile.
 *  - 003 Missing or mismatched delimiters shall latch failure without reading outside input.
 *
 * \param[in,out] cursor     Initialized private cursor
 * \param[in]     expected   Required separator, colon or comma
 *
 * \retval        None
 */
static FORCE_INLINE void ReadDelimiter(Cursor_t* cursor, byte_t expected);

/*************************************************************************************************/
/**
 * \brief Reads one raw line and consumes its optional line ending.
 *
 * \par Requirements:
 *  - 001 The initialized cursor shall retain counted bounds.
 *  - 002 CRLF, CR and LF shall each terminate one line; EOF may terminate the final line.
 *  - 003 Embedded NUL and malformed UTF-8 shall latch failure.
 *  - 004 Output is private staging and shall not be published after failure.
 *
 * \param[in,out] cursor   Initialized private cursor
 * \param[out]    line     Required borrowed line staging
 *
 * \retval        TRUE    A line was encountered; cursor validity determines whether it is usable.
 * \retval        FALSE   End or a previously failed cursor.
 */
static FORCE_INLINE bool_t ReadLine(Cursor_t* cursor, Line_t* line);

/*************************************************************************************************/
/**
 * \brief Skips blank separators before the next cue.
 *
 * \par Requirements:
 *  - 001 Every blank-line iteration shall consume at least one byte.
 *  - 002 End and malformed input shall stop the scan.
 *
 * \param[in,out] cursor   Initialized private cursor
 * \param[out]    line     Required nonempty line staging
 *
 * \retval        TRUE    A nonempty line was encountered; validity remains in the cursor.
 * \retval        FALSE   No nonempty line remains.
 */
static FORCE_INLINE bool_t ReadNonemptyLine(Cursor_t* cursor, Line_t* line);

/*************************************************************************************************/
/**
 * \brief Reads cue endpoints and retains any timing suffix.
 *
 * \par Requirements:
 *  - 001 The line shall have already passed UTF-8/NUL validation.
 *  - 002 The arrow shall immediately follow the start timestamp with one space on each side.
 *  - 003 End shall be strictly greater than start; partial fields remain private staging.
 *  - 004 The timing suffix shall be preserved as opaque bytes, not interpreted as WebVTT settings.
 *
 * \param[in]     line   Required validated counted line
 * \param[in,out] cue    Initialized private cue staging
 *
 * \retval        TRUE    The timing envelope is valid.
 * \retval        FALSE   Malformed, reversed, empty or overflowing interval.
 */
static FORCE_INLINE bool_t ReadTimingLine(const Utl_Srt_Span_t* line, Utl_Srt_Cue_t* cue);

/*************************************************************************************************/
/**
 * \brief Reads an optional identifier and the mandatory timing line.
 *
 * \par Details:
 *  Timing is parsed or cursor validity is cleared.
 *
 * \par Requirements:
 *  - 001 The first line shall be nonempty and validated.
 *  - 002 At most one identifier line shall precede timing; a blank line after an identifier shall fail.
 *  - 003 Identifiers containing an arrow shall never be treated as text identifiers.
 *
 * \param[in,out] cursor   Initialized private cursor, positioned after first
 * \param[in]     first    Required validated first line
 * \param[in,out] cue      Initialized private cue staging
 *
 * \retval        None
 */
static FORCE_INLINE void ReadHeader(Cursor_t* cursor, const Line_t* first, Utl_Srt_Cue_t* cue);

/*************************************************************************************************/
/**
 * \brief Collects validated payload lines up to a blank separator or EOF.
 *
 * \par Details:
 *  The payload is staged or cursor validity records failure.
 *
 * \par Requirements:
 *  - 001 Each successful line shall consume bytes; malformed UTF-8/NUL shall latch failure.
 *  - 002 Internal line endings shall be retained; the final line ending shall be excluded.
 *  - 003 A missing payload shall produce an empty borrowed span.
 *
 * \param[in,out] cursor    Initialized private cursor after timing
 * \param[out]    payload   Required private span staging
 *
 * \retval        None
 */
static FORCE_INLINE void ReadPayload(Cursor_t* cursor, Utl_Srt_Span_t* payload);

/*************************************************************************************************/
/**
 * \brief Publishes every field of a completely parsed cue.
 *
 * \par Details:
 *  All scalar values and borrowed spans are published.
 *
 * \par Requirements:
 *  - 001 Both models shall be present and disjoint.
 *  - 002 The staged model shall be complete; no partial parse shall call this helper.
 *
 * \param[out] output   Required public destination
 * \param[in]  staged   Required complete private model
 *
 * \retval     None
 */
static FORCE_INLINE void PublishCue(Utl_Srt_Cue_t* output, const Utl_Srt_Cue_t* staged);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Public entry points own and retain the borrowed source.
 */
static FORCE_INLINE void InitializeCursor(Cursor_t* cursor, const byte_t* input, uint32_t byteCount)
{
    bool_t has_source;

    LOGASSERT(NULL != cursor);
    has_source = (NULL != input) || (0 == byteCount);
    LOGASSERT(has_source);
    cursor->input     = input;
    cursor->byteCount = byteCount;
    cursor->offset    = 0;
    cursor->valid     = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Empty staging prevents partial parses from exposing indeterminate fields.
 */
static FORCE_INLINE void InitializeCue(Utl_Srt_Cue_t* cue)
{
    LOGASSERT(NULL != cue);
    cue->identifier.data           = NULL;
    cue->identifier.byteCount      = 0;
    cue->start.milliseconds        = 0;
    cue->start.hourDigits          = 0;
    cue->end.milliseconds          = 0;
    cue->end.hourDigits            = 0;
    cue->timingExtension.data      = NULL;
    cue->timingExtension.byteCount = 0;
    cue->payload.data              = NULL;
    cue->payload.byteCount         = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The core token also excludes arrow-bearing identifiers from generated WebVTT.
 */
static FORCE_INLINE bool_t HasArrow(const Utl_Srt_Span_t* line)
{
    bool_t   found;
    uint32_t remaining;
    uint32_t offset;
    int32_t  comparison;

    found     = FALSE;
    remaining = line->byteCount;
    offset    = 0;
    for (;;)
    {
        if (sizeof(SRT_ARROW_CORE) - 1 > remaining)
        {
            break;
        }
        comparison = CRT_Memory_Compare(line->data + offset, SRT_ARROW_CORE, sizeof(SRT_ARROW_CORE) - 1);
        found      = (0 == comparison);
        if (found)
        {
            break;
        }
        ++offset;
        --remaining;
    }

    return found;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Subtraction bounds the candidate span before decimal parsing.
 */
static FORCE_INLINE void ReadNumber(Cursor_t* cursor, const NumberFormat_t* format, Number_t* number)
{
    uint32_t available;
    uint64_t value;
    uint32_t digits;
    bool_t   valid_width;
    bool_t   valid_value;

    if (cursor->valid)
    {
        available = cursor->byteCount - cursor->offset;
        if (format->maximumDigits < available)
        {
            available = format->maximumDigits + 1;
        }
        cursor->valid = Utl_Decimal_ReadUint64(&value, &digits, cursor->input + cursor->offset, available);
        if (cursor->valid)
        {
            valid_width   = (format->minimumDigits <= digits) && (digits <= format->maximumDigits);
            valid_value   = (value <= format->maximumValue);
            cursor->valid = valid_width && valid_value;
            if (cursor->valid)
            {
                number->value  = value;
                number->digits = digits;
                cursor->offset += digits;
            }
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only the fraction separator admits the compatibility alternative.
 */
static FORCE_INLINE void ReadDelimiter(Cursor_t* cursor, byte_t expected)
{
    byte_t value;
    bool_t exact;
    bool_t fraction;
    bool_t compatible;

    if (cursor->valid)
    {
        cursor->valid = (cursor->offset < cursor->byteCount);
        if (cursor->valid)
        {
            value         = cursor->input[cursor->offset];
            exact         = (expected == value);
            fraction      = (SRT_CHAR_COMMA == expected);
            compatible    = fraction && (SRT_CHAR_DOT == value);
            cursor->valid = exact || compatible;
            ++cursor->offset;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The byte scan identifies framing; scalar validation checks the complete nonempty line.
 */
static FORCE_INLINE bool_t ReadLine(Cursor_t* cursor, Line_t* line)
{
    bool_t   present;
    uint32_t first;
    byte_t   value;
    bool_t   is_cr;
    bool_t   is_lf;
    bool_t   newline;
    bool_t   has_next;

    present = cursor->valid && (cursor->offset < cursor->byteCount);
    if (present)
    {
        first = cursor->offset;
        for (;;)
        {
            if (cursor->offset == cursor->byteCount)
            {
                break;
            }
            value         = cursor->input[cursor->offset];
            is_cr         = (SRT_CHAR_CR == value);
            is_lf         = (SRT_CHAR_LF == value);
            newline       = is_cr || is_lf;
            cursor->valid = (0 != value);
            if (newline || !cursor->valid)
            {
                break;
            }
            ++cursor->offset;
        }
        line->end            = cursor->offset;
        line->text.data      = cursor->input + first;
        line->text.byteCount = cursor->offset - first;
        if (cursor->valid)
        {
            cursor->valid = Utl_Unicode_IsValidUtf8((const byte_t*)line->text.data, line->text.byteCount);
        }
        has_next = (cursor->offset < cursor->byteCount);
        if (cursor->valid && has_next)
        {
            is_cr = (SRT_CHAR_CR == cursor->input[cursor->offset]);
            ++cursor->offset;
            has_next = (cursor->offset < cursor->byteCount);
            if (is_cr && has_next)
            {
                is_lf = (SRT_CHAR_LF == cursor->input[cursor->offset]);
                if (is_lf)
                {
                    ++cursor->offset;
                }
            }
        }
    }

    return present;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Trailing blank lines can be consumed once when the public reader reports END.
 */
static FORCE_INLINE bool_t ReadNonemptyLine(Cursor_t* cursor, Line_t* line)
{
    bool_t present;
    bool_t nonempty;

    for (;;)
    {
        present = ReadLine(cursor, line);
        if (!present || !cursor->valid)
        {
            break;
        }
        nonempty = (0 < line->text.byteCount);
        if (nonempty)
        {
            break;
        }
    }

    return present;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every offset addition follows a remaining-byte check or a successful counted parse.
 */
static FORCE_INLINE bool_t ReadTimingLine(const Utl_Srt_Span_t* line, Utl_Srt_Cue_t* cue)
{
    uint32_t first;
    uint32_t offset;
    uint32_t last;
    bool_t   valid;
    uint32_t remaining;
    int32_t  comparison;

    first  = 0;
    offset = 0;
    last   = 0;
    valid  = Utl_Srt_ReadTimestamp(&cue->start, &first, line->data, line->byteCount);
    if (valid)
    {
        remaining = line->byteCount - first;
        valid     = (sizeof(SRT_ARROW) - 1 <= remaining);
        if (valid)
        {
            comparison = CRT_Memory_Compare(line->data + first, SRT_ARROW, sizeof(SRT_ARROW) - 1);
            valid      = (0 == comparison);
            offset     = first + (uint32_t)(sizeof(SRT_ARROW) - 1);
        }
    }
    if (valid)
    {
        valid = Utl_Srt_ReadTimestamp(&cue->end, &last, line->data + offset, line->byteCount - offset);
    }
    if (valid)
    {
        valid = (cue->start.milliseconds < cue->end.milliseconds);
    }
    if (valid)
    {
        offset                         += last;
        cue->timingExtension.data      = line->data + offset;
        cue->timingExtension.byteCount = line->byteCount - offset;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A leading arrow-bearing line must parse as timing; malformed timing cannot become an identifier.
 */
static FORCE_INLINE void ReadHeader(Cursor_t* cursor, const Line_t* first, Utl_Srt_Cue_t* cue)
{
    bool_t is_timing;
    bool_t present;
    Line_t timing;

    is_timing = HasArrow(&first->text);
    if (is_timing)
    {
        cursor->valid = ReadTimingLine(&first->text, cue);
    }
    else
    {
        cue->identifier.data      = first->text.data;
        cue->identifier.byteCount = first->text.byteCount;
        present                   = ReadLine(cursor, &timing);
        cursor->valid             = cursor->valid && present;
        if (cursor->valid)
        {
            cursor->valid = ReadTimingLine(&timing.text, cue);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The span is borrowed from the original storage; no payload copy or allocation occurs.
 */
static FORCE_INLINE void ReadPayload(Cursor_t* cursor, Utl_Srt_Span_t* payload)
{
    uint32_t first;
    uint32_t last;
    bool_t   present;
    Line_t   line;
    bool_t   blank;

    first = cursor->offset;
    last  = first;
    for (;;)
    {
        present = ReadLine(cursor, &line);
        if (!present || !cursor->valid)
        {
            break;
        }
        blank = (0 == line.text.byteCount);
        if (blank)
        {
            break;
        }
        last = line.end;
    }
    if (cursor->valid)
    {
        payload->data      = cursor->input + first;
        payload->byteCount = last - first;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Explicit field publication avoids an implicit large-structure runtime copy.
 */
static FORCE_INLINE void PublishCue(Utl_Srt_Cue_t* output, const Utl_Srt_Cue_t* staged)
{
    output->identifier.data           = staged->identifier.data;
    output->identifier.byteCount      = staged->identifier.byteCount;
    output->start.milliseconds        = staged->start.milliseconds;
    output->start.hourDigits          = staged->start.hourDigits;
    output->end.milliseconds          = staged->end.milliseconds;
    output->end.hourDigits            = staged->end.hourDigits;
    output->timingExtension.data      = staged->timingExtension.data;
    output->timingExtension.byteCount = staged->timingExtension.byteCount;
    output->payload.data              = staged->payload.data;
    output->payload.byteCount         = staged->payload.byteCount;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Only the complete leading UTF-8 signature is skipped. Other encodings fail during cue reading.
 */
void Utl_Srt_InitializeReader(Utl_Srt_Reader_t* reader, const byte_t* input, uint32_t byteCount)
{
    bool_t has_source;
    bool_t matches_first;
    bool_t matches_second;
    bool_t matches_last;
    bool_t has_signature;

    LOGASSERT(NULL != reader);
    has_source = (NULL != input) || (0 == byteCount);
    LOGASSERT(has_source);
    reader->input     = input;
    reader->byteCount = byteCount;
    reader->offset    = 0;
    if (SRT_SIGNATURE_BYTES <= byteCount)
    {
        matches_first  = (SRT_SIGNATURE_FIRST == (byte_t)input[0]);
        matches_second = (SRT_SIGNATURE_SECOND == (byte_t)input[1]);
        matches_last   = (SRT_SIGNATURE_LAST == (byte_t)input[2]);
        has_signature  = matches_first && matches_second && matches_last;
        if (has_signature)
        {
            reader->offset = SRT_SIGNATURE_BYTES;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Subtraction follows the established cursor invariant.
 */
uint32_t Utl_Srt_GetRemainingByteCount(const Utl_Srt_Reader_t* reader)
{
    LOGASSERT(NULL != reader);
    LOGASSERT(reader->offset <= reader->byteCount);

    return reader->byteCount - reader->offset;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The hours bound precedes multiplication; the remaining fields are independently bounded before addition.
 */
bool_t Utl_Srt_ReadTimestamp(Utl_Srt_Time_t* time, uint32_t* bytesRead, const byte_t* input, uint32_t byteCount)
{
    Cursor_t cursor;
    Number_t hours;
    Number_t minutes;
    Number_t seconds;
    Number_t fraction;
    uint64_t base;
    uint64_t remainder;
    bool_t   fits;

    LOGASSERT(NULL != time);
    LOGASSERT(NULL != bytesRead);
    LOGASSERT(NULL != input);

    InitializeCursor(&cursor, input, byteCount);
    hours.value     = 0;
    hours.digits    = 0;
    minutes.value   = 0;
    minutes.digits  = 0;
    seconds.value   = 0;
    seconds.digits  = 0;
    fraction.value  = 0;
    fraction.digits = 0;
    ReadNumber(&cursor, &c_hoursFormat, &hours);
    ReadDelimiter(&cursor, SRT_CHAR_COLON);
    ReadNumber(&cursor, &c_pairFormat, &minutes);
    ReadDelimiter(&cursor, SRT_CHAR_COLON);
    ReadNumber(&cursor, &c_pairFormat, &seconds);
    ReadDelimiter(&cursor, SRT_CHAR_COMMA);
    ReadNumber(&cursor, &c_fractionFormat, &fraction);
    if (cursor.valid)
    {
        base      = hours.value * SRT_MILLISECONDS_PER_HOUR;
        remainder = (minutes.value * SRT_MILLISECONDS_PER_MINUTE) + (seconds.value * SRT_MILLISECONDS_PER_SECOND) +
                       fraction.value;
        fits         = (base <= UINT64_MAX - remainder);
        cursor.valid = fits;
        if (fits)
        {
            time->milliseconds = base + remainder;
            time->hourDigits   = hours.digits;
            *bytesRead         = cursor.offset;
        }
    }

    return cursor.valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only a complete valid cue advances the public reader. END consumes trailing separators once;
 *  INVALID preserves all public state.
 */
Utl_Srt_ReadResult_t Utl_Srt_ReadCue(Utl_Srt_Reader_t* reader, Utl_Srt_Cue_t* cue)
{
    Cursor_t             cursor;
    bool_t               present;
    Line_t               first;
    Utl_Srt_ReadResult_t result;
    Utl_Srt_Cue_t        staged;

    LOGASSERT(NULL != reader);
    LOGASSERT(NULL != cue);

    LOGASSERT(reader->offset <= reader->byteCount);
    InitializeCursor(&cursor, reader->input, reader->byteCount);
    cursor.offset = reader->offset;
    present       = ReadNonemptyLine(&cursor, &first);
    result        = UTL_SRT_READ_INVALID;
    if (cursor.valid)
    {
        if (present)
        {
            InitializeCue(&staged);
            ReadHeader(&cursor, &first, &staged);
            if (cursor.valid)
            {
                ReadPayload(&cursor, &staged.payload);
            }
            if (cursor.valid)
            {
                PublishCue(cue, &staged);
                reader->offset = cursor.offset;
                result         = UTL_SRT_READ_CUE;
            }
        }
        else
        {
            reader->offset = cursor.offset;
            result         = UTL_SRT_READ_END;
        }
    }

    return result;
}
