/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * HTTP/1.1 syntax, framing and chunked transfer coding: RFC 9112 sections 2 through 9.
 * https://www.rfc-editor.org/rfc/rfc9112.html
 * HTTP ranges: RFC 9110 sections 14.1, 14.2 and 14.4.
 * HTTP fields, tokens and connection options: RFC 9110 sections 5, 7.6.1 and 8.6.
 * https://www.rfc-editor.org/rfc/rfc9110.html
 *
 * Octet-oriented, counted models use borrowed or caller-owned storage. No CRT,
 * allocation, OS calls, sockets or retained global state occur here. CRLF is required;
 * obsolete folding and whitespace before a field colon are rejected. HTTP URI
 * endpoints and origin-form targets follow RFC 9110 section 4.2 and RFC 9112 section 3.2.1.
 * Authentication, caching, content codings and application method policy are separate.
 * uint64_t body counts and caller capacities are local bounds, not HTTP grammar limits.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_compiler.h"
#include "utl/utl_http.h"
#include "utl/utl_decimal.h"
#include "os/os_assert.h"
#include "os/os_types.h"
#include "utl/utl_url.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define RANGE_UNIT_BYTES   (sizeof(UTL_HTTP_RANGE_UNIT) - 1)
#define RANGE_PREFIX_BYTES (RANGE_UNIT_BYTES + 1)

#define HTTP_FIELD_LENGTH     (0)
#define HTTP_FIELD_TRANSFER   (1)
#define HTTP_FIELD_CONNECTION (2)

#define DATE_YEAR_MAX      (9999)
#define DATE_MONTH_COUNT   (12)
#define DATE_WEEKDAY_COUNT (7)
#define DATE_FEBRUARY      (2)
#define DATE_HOUR_MAX      (23)
#define DATE_MINUTE_MAX    (59)
#define DATE_SECOND_MAX    (60)
#define DATE_NAME_BYTES    (3)
#define DATE_TIME_DIGITS   (2)
#define DATE_YEAR_DIGITS   (4)
#define DATE_RADIX         (10)
#define DATE_DAY_OFFSET    (5)
#define DATE_MONTH_OFFSET  (8)
#define DATE_YEAR_OFFSET   (12)
#define DATE_HOUR_OFFSET   (17)
#define DATE_MINUTE_OFFSET (20)
#define DATE_SECOND_OFFSET (23)
#define DATE_ZONE_OFFSET   (26)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Accumulated framing fields; only a validated result is published. */
typedef struct HttpFramingFields_s
{
    bool_t   hasLength;
    uint64_t length;
    bool_t   hasTransfer;
    bool_t   hasChunked;
    bool_t   hasOtherCoding;
    bool_t   mustClose;
    bool_t   keepAlive;
    uint32_t hostCount;
} HttpFramingFields_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/** Canonical case-insensitive byte-range unit. */
static const char_t c_rangeUnit[] = UTL_HTTP_RANGE_UNIT;

/** RFC 9110 section 5.6.7 day-name and month terminals, in calendar order. */
static const char_t c_weekdays[DATE_WEEKDAY_COUNT][DATE_NAME_BYTES + 1] =
    {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static const char_t c_months[DATE_MONTH_COUNT][DATE_NAME_BYTES + 1] =
    {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
/** Gregorian month lengths before the leap-day adjustment. */
static const uint8_t c_monthDays[DATE_MONTH_COUNT] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
/** Month offsets for the Gregorian weekday congruence, Sunday numbered zero. */
static const uint8_t c_weekdayOffsets[DATE_MONTH_COUNT] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Recognizes one RFC token byte.
 *
 * \param[in] value   HTTP octet
 *
 * \retval    TRUE    For tchar.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t IsTokenByte(byte_t value);

/*************************************************************************************************/
/**
 * \brief Recognizes HTTP optional whitespace.
 *
 * \param[in] value   HTTP octet
 *
 * \retval    TRUE    For SP or HTAB.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t IsWhitespace(char_t value);

/*************************************************************************************************/
/**
 * \brief Folds ASCII uppercase to lowercase.
 *
 * \param[in] value   HTTP octet
 *
 * \retval    0..UINT8_MAX   ASCII lowercase for an uppercase input; otherwise the unchanged octet.
 */
static FORCE_INLINE byte_t FoldAscii(byte_t value);

/*************************************************************************************************/
/**
 * \brief Finds the next non-whitespace byte.
 *
 * \param[in] buffer      Readable input
 * \param[in] byteCount   Readable bytes
 * \param[in] cursor      Offset at most byteCount
 *
 * \retval    cursor..byteCount   Offset of the first non-whitespace byte, or byteCount when none remains.
 */
static FORCE_INLINE uint32_t GetWhitespaceEnd(const char_t* buffer, uint32_t byteCount, uint32_t cursor);

/*************************************************************************************************/
/**
 * \brief Removes outer optional whitespace from a borrowed span.
 *
 * \param[in] text   Readable input
 *
 * \retval    text   Borrowed subspan with leading and trailing optional whitespace removed.
 */
static FORCE_INLINE Utl_Http_Text_t Trim(Utl_Http_Text_t text);

/*************************************************************************************************/
/**
 * \brief Compares a span with a terminated internal ASCII constant.
 *
 * \param[in] text         Readable counted bytes
 * \param[in] literal      Non-NULL internal terminated constant
 * \param[in] ignoreCase   TRUE for ASCII folding
 *
 * \retval    TRUE    For complete equality.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t IsEqualLiteral(Utl_Http_Text_t text, const char_t* literal, bool_t ignoreCase);

/*************************************************************************************************/
/**
 * \brief Validates octet boundaries of a nonempty request target.
 *
 * \param[in] target   Borrowed request target
 *
 * \retval    TRUE    For visible ASCII without a fragment delimiter.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t IsTarget(Utl_Http_Text_t target);

/*************************************************************************************************/
/**
 * \brief Matches the case-insensitive bytes range unit and its exact separator.
 *
 * \param[in]     buffer      Readable counted input
 * \param[in]     byteCount   Readable bytes
 * \param[in,out] cursor      Current input offset; changed only on a match
 * \param[in]     separator   Equals for Range or SP for Content-Range
 *
 * \retval        TRUE    When the prefix matches.
 * \retval        FALSE   Otherwise.
 */
static FORCE_INLINE bool_t HasBytesUnit(const char_t* buffer, uint32_t byteCount, uint32_t* cursor, char_t separator);

/*************************************************************************************************/
/**
 * \brief Validates the kind and ordered endpoints of a typed range.
 *
 * \param[in] range   Non-NULL specifier
 *
 * \retval    TRUE    For a supported syntactically valid kind.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t IsValidRange(const Utl_Http_Range_t* range);

/*************************************************************************************************/
/**
 * \brief Counts a validated range specifier's canonical bytes.
 *
 * \param[in] range   Non-NULL valid specifier
 *
 * \retval    2..41   Decimal digits and the separating dash for the validated range.
 */
static FORCE_INLINE uint32_t GetRangeMemberByteCount(const Utl_Http_Range_t* range);

/*************************************************************************************************/
/**
 * \brief Reads a single non-folded field line.
 *
 * \param[out] field   Result on success
 * \param[in]  line    Complete line excluding CRLF
 *
 * \retval     TRUE    For valid name and field-value syntax.
 * \retval     FALSE   Otherwise.
 */
static FORCE_INLINE bool_t ReadField(Utl_Http_Field_t* field, Utl_Http_Text_t line);

/*************************************************************************************************/
/**
 * \brief Reads the exact HTTP-version grammar.
 *
 * \param[out] major   Single major digit on success
 * \param[out] minor   Single minor digit on success
 * \param[in]  text    Eight version bytes
 *
 * \retval     TRUE    For HTTP/DIGIT.DIGIT.
 * \retval     FALSE   Otherwise.
 */
static FORCE_INLINE bool_t ReadVersion(uint8_t* major, uint8_t* minor, Utl_Http_Text_t text);

/*************************************************************************************************/
/**
 * \brief Reads a request-line or status-line into a private candidate.
 *
 * \param[in,out] head   Candidate with its expected kind set
 * \param[in]     line   Complete counted start line
 *
 * \retval        TRUE    For valid start-line syntax.
 * \retval        FALSE   Otherwise.
 */
static FORCE_INLINE bool_t ReadStartLine(Utl_Http_Head_t* head, Utl_Http_Text_t line);

/*************************************************************************************************/
/**
 * \brief Parses a newly completed line into the incremental reader candidate.
 *
 * \param[in,out] reader   Initialized scan state
 * \param[in]     line     Borrowed line excluding its CRLF
 *
 * \retval        None   No result value.
 */
static FORCE_INLINE void ReadCompletedLine(Utl_Http_HeadReader_t* reader, Utl_Http_Text_t line);

/*************************************************************************************************/
/**
 * \brief Reads the next nonempty HTTP list member, respecting quoted commas.
 *
 * \param[out]    member         Borrowed item on success when hasMember is TRUE
 * \param[out]    hasMember      Whether a member was found
 * \param[in]     list           Validated readable field value
 * \param[in,out] cursor         Next offset at most list.byteCount
 * \param[in,out] emptyMembers   Accumulated tolerance count
 *
 * \retval        TRUE    The next member or the end of the list was found.
 * \retval        FALSE   Quotes are unmatched or the empty-member limit was exceeded.
 */
static FORCE_INLINE bool_t ReadListMember(Utl_Http_Text_t* member,
                                          bool_t*          hasMember,
                                          Utl_Http_Text_t  list,
                                          uint32_t*        cursor,
                                          uint32_t*        emptyMembers);

/*************************************************************************************************/
/**
 * \brief Validates semicolon parameters or chunk extensions.
 *
 * \param[in] text            Complete counted value
 * \param[in] cursor          Offset after the primary token or chunk size
 * \param[in] valueRequired   TRUE for transfer parameters; FALSE for chunk extensions
 *
 * \retval    TRUE    When the remainder matches parameter syntax.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t ReadParameters(Utl_Http_Text_t text, uint32_t cursor, bool_t valueRequired);

/*************************************************************************************************/
/**
 * \brief Accumulates Content-Length, Transfer-Encoding or Connection list semantics.
 *
 * \param[in,out] fields   Private aggregate, discarded on failure
 * \param[in]     value    Validated counted field value
 * \param[in]     kind     HTTP_FIELD_LENGTH, HTTP_FIELD_TRANSFER or HTTP_FIELD_CONNECTION
 *
 * \retval        TRUE    When every member is valid and consistent.
 * \retval        FALSE   Otherwise.
 */
static FORCE_INLINE bool_t ReadFramingField(HttpFramingFields_t* fields, Utl_Http_Text_t value, uint32_t kind);

/*************************************************************************************************/
/**
 * \brief Reads a uint64_t decimal prefix at the current input offset.
 *
 * \param[out]    value       Non-NULL numeric result
 * \param[in]     buffer      Readable counted input
 * \param[in]     byteCount   Input span size
 * \param[in,out] cursor      Offset within input, advanced only on success
 *
 * \retval        TRUE    For a representable numeric prefix.
 * \retval        FALSE   Otherwise.
 */
static FORCE_INLINE bool_t ReadNumber(uint64_t* value, const char_t* buffer, uint32_t byteCount, uint32_t* cursor);

/*************************************************************************************************/
/**
 * \brief Reads one range member up to a delimiter or the counted end.
 *
 * \param[out]    range       Non-NULL candidate, not published on failure by its caller
 * \param[in]     buffer      Readable counted input
 * \param[in]     byteCount   Input span size
 * \param[in,out] cursor      Offset strictly before the input end on entry
 *
 * \retval        TRUE    For valid syntax and representable numbers.
 * \retval        FALSE   Otherwise.
 */
static FORCE_INLINE bool_t ReadRangeMember(Utl_Http_Range_t* range,
                                           const char_t*     buffer,
                                           uint32_t          byteCount,
                                           uint32_t*         cursor);

/*************************************************************************************************/
/**
 * \brief Appends a prevalidated span inside preflighted capacity.
 *
 * \param[out]    buffer   Writable output
 * \param[in,out] cursor   Next output offset
 * \param[in]     text     Readable disjoint span
 *
 * \retval        None   No result value.
 */
static FORCE_INLINE void WriteSpan(char_t* buffer, uint32_t* cursor, Utl_Http_Text_t text);

/*************************************************************************************************/
/**
 * \brief Appends a terminated internal constant without its terminator.
 *
 * \param[out]    buffer    Writable output
 * \param[in,out] cursor    Next output offset
 * \param[in]     literal   Internal constant covered by writer size preflight
 *
 * \retval        None   No result value.
 */
static FORCE_INLINE void WriteLiteral(char_t* buffer, uint32_t* cursor, const char_t* literal);

/*************************************************************************************************/
/**
 * \brief Writes a prevalidated date number with leading zeroes.
 *
 * \param[out] output   Writable fixed-width span
 * \param[in]  value    Number already bounded by its calendar component
 * \param[in]  width    Two or four decimal digits
 *
 * \retval     None   No result value.
 */
static FORCE_INLINE void WriteDateNumber(char_t* output, uint32_t value, uint32_t width);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  ASCII ranges are separate named questions; token punctuation is handled explicitly.
 */
static FORCE_INLINE bool_t IsTokenByte(byte_t value)
{
    bool_t is_digit;
    bool_t is_upper;
    bool_t is_lower;
    bool_t is_token;

    is_digit = (UTL_HTTP_CHAR_DIGIT_FIRST <= value) && (UTL_HTTP_CHAR_DIGIT_LAST >= value);
    is_upper = (UTL_HTTP_CHAR_UPPER_FIRST <= value) && (UTL_HTTP_CHAR_UPPER_LAST >= value);
    is_lower = (UTL_HTTP_CHAR_LOWER_FIRST <= value) && (UTL_HTTP_CHAR_LOWER_LAST >= value);
    is_token = is_digit || is_upper || is_lower;
    if (!is_token)
    {
        switch (value)
        {
        case UTL_HTTP_CHAR_EXCLAMATION:
        case UTL_HTTP_CHAR_HASH:
        case UTL_HTTP_CHAR_DOLLAR:
        case UTL_HTTP_CHAR_PERCENT:
        case UTL_HTTP_CHAR_AMP:
        case UTL_HTTP_CHAR_APOSTROPHE:
        case UTL_HTTP_CHAR_ASTERISK:
        case UTL_HTTP_CHAR_PLUS:
        case UTL_HTTP_CHAR_HYPHEN:
        case UTL_HTTP_CHAR_DOT:
        case UTL_HTTP_CHAR_CARET:
        case UTL_HTTP_CHAR_UNDERSCORE:
        case UTL_HTTP_CHAR_BACKTICK:
        case UTL_HTTP_CHAR_PIPE:
        case UTL_HTTP_CHAR_TILDE:
            is_token = TRUE;
            break;
        default:
            break;
        }
    }
    return is_token;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsWhitespace(char_t value)
{
    return (UTL_HTTP_CHAR_SPACE == value) || (UTL_HTTP_CHAR_TAB == value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  HTTP name comparison is independent of process locale.
 */
static FORCE_INLINE byte_t FoldAscii(byte_t value)
{
    if ((UTL_HTTP_CHAR_UPPER_FIRST <= value) && (UTL_HTTP_CHAR_UPPER_LAST >= value))
    {
        value = (byte_t)(value + (UTL_HTTP_CHAR_LOWER_FIRST - UTL_HTTP_CHAR_UPPER_FIRST));
    }
    return value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetWhitespaceEnd(const char_t* buffer, uint32_t byteCount, uint32_t cursor)
{
    uint32_t index;

    for (index = cursor; index < byteCount; ++index)
    {
        if (!IsWhitespace(buffer[index]))
        {
            break;
        }
    }
    return index;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  An empty input does not require a data pointer.
 */
static FORCE_INLINE Utl_Http_Text_t Trim(Utl_Http_Text_t text)
{
    uint32_t first;
    uint32_t end;

    first = GetWhitespaceEnd(text.data, text.byteCount, 0);
    end   = text.byteCount;
    for (;;)
    {
        if (first == end)
        {
            break;
        }
        if (!IsWhitespace(text.data[end - 1]))
        {
            break;
        }
        --end;
    }
    if (0 < first)
    {
        text.data += first;
    }
    text.byteCount = end - first;
    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The literal terminator is checked before reading its next byte.
 */
static FORCE_INLINE bool_t IsEqualLiteral(Utl_Http_Text_t text, const char_t* literal, bool_t ignoreCase)
{
    bool_t   equal;
    uint32_t index;
    byte_t   left;
    byte_t   right;

    equal = (0 == text.byteCount) || (NULL != text.data);
    if (equal)
    {
        for (index = 0; index < text.byteCount; ++index)
        {
            if (UTL_HTTP_CHAR_NUL == literal[index])
            {
                equal = FALSE;
                break;
            }
            left  = (byte_t)text.data[index];
            right = (byte_t)literal[index];
            if (ignoreCase)
            {
                left  = FoldAscii(left);
                right = FoldAscii(right);
            }
            if (left != right)
            {
                equal = FALSE;
                break;
            }
        }
        if (equal)
        {
            equal = (UTL_HTTP_CHAR_NUL == literal[text.byteCount]);
        }
    }
    return equal;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  URI forms, percent-encoding and authority semantics are validated by the URI layer.
 */
static FORCE_INLINE bool_t IsTarget(Utl_Http_Text_t target)
{
    bool_t   is_valid;
    uint32_t index;
    byte_t   value;
    bool_t   is_visible;
    bool_t   is_fragment;

    is_valid = (NULL != target.data) && (0 < target.byteCount);
    if (is_valid)
    {
        for (index = 0; index < target.byteCount; ++index)
        {
            value       = (byte_t)target.data[index];
            is_visible  = (UTL_HTTP_CHAR_EXCLAMATION <= value) && (UTL_HTTP_CHAR_TILDE >= value);
            is_fragment = (UTL_HTTP_CHAR_HASH == value);
            is_valid    = is_visible && !is_fragment;
            if (!is_valid)
            {
                break;
            }
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The comparison uses ASCII folding only; whitespace around '=' is not introduced.
 */
static FORCE_INLINE bool_t HasBytesUnit(const char_t* buffer, uint32_t byteCount, uint32_t* cursor, char_t separator)
{
    bool_t          has_prefix;
    Utl_Http_Text_t unit;

    has_prefix = (RANGE_PREFIX_BYTES <= (byteCount - (*cursor)));
    if (has_prefix)
    {
        unit.data      = &buffer[(*cursor)];
        unit.byteCount = RANGE_UNIT_BYTES;
        has_prefix     = IsEqualLiteral(unit, c_rangeUnit, TRUE);
        has_prefix     = has_prefix && (separator == buffer[(*cursor) + RANGE_UNIT_BYTES]);
        if (has_prefix)
        {
            (*cursor) += RANGE_PREFIX_BYTES;
        }
    }
    return has_prefix;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A zero-length suffix is valid syntax but is not satisfiable.
 */
static FORCE_INLINE bool_t IsValidRange(const Utl_Http_Range_t* range)
{
    bool_t is_valid;

    switch (range->kind)
    {
    case UTL_HTTP_RANGE_CLOSED:
        is_valid = (range->first <= range->last);
        break;
    case UTL_HTTP_RANGE_OPEN:
    case UTL_HTTP_RANGE_SUFFIX:
        is_valid = TRUE;
        break;
    default:
        is_valid = FALSE;
        break;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetRangeMemberByteCount(const Utl_Http_Range_t* range)
{
    uint32_t count;

    count = 1;
    if (UTL_HTTP_RANGE_SUFFIX == range->kind)
    {
        count += Utl_Decimal_GetUint64ByteCount(range->suffixLength);
    }
    else
    {
        count += Utl_Decimal_GetUint64ByteCount(range->first);
        if (UTL_HTTP_RANGE_CLOSED == range->kind)
        {
            count += Utl_Decimal_GetUint64ByteCount(range->last);
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The colon must immediately follow a nonempty token; outer value OWS is removed.
 */
static FORCE_INLINE bool_t ReadField(Utl_Http_Field_t* field, Utl_Http_Text_t line)
{
    bool_t           is_valid;
    uint32_t         index;
    Utl_Http_Field_t result;

    is_valid = FALSE;
    for (index = 0; index < line.byteCount; ++index)
    {
        if (!IsTokenByte((byte_t)line.data[index]))
        {
            break;
        }
    }
    if ((0 < index) && (index < line.byteCount))
    {
        is_valid = (UTL_HTTP_CHAR_COLON == line.data[index]);
    }
    if (is_valid)
    {
        result.name.data       = line.data;
        result.name.byteCount  = index;
        result.value.data      = line.data + index + 1;
        result.value.byteCount = line.byteCount - index - 1;
        result.value           = Trim(result.value);
        is_valid               = Utl_Http_IsFieldValue(result.value);
        if (is_valid)
        {
            (*field) = result;
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Version support policy is separate from recognizing its syntax.
 */
static FORCE_INLINE bool_t ReadVersion(uint8_t* major, uint8_t* minor, Utl_Http_Text_t text)
{
    bool_t          has_width;
    bool_t          is_valid;
    Utl_Http_Text_t prefix;
    bool_t          has_prefix;
    bool_t          has_major;
    bool_t          has_minor;

    has_width = (UTL_HTTP_VERSION_BYTE_COUNT == text.byteCount);
    is_valid  = has_width;
    if (has_width)
    {
        prefix.data      = text.data;
        prefix.byteCount = sizeof(UTL_HTTP_VERSION_PREFIX) - 1;
        has_prefix       = IsEqualLiteral(prefix, UTL_HTTP_VERSION_PREFIX, FALSE);
        has_major        = (UTL_HTTP_CHAR_DIGIT_FIRST <= text.data[UTL_HTTP_VERSION_MAJOR_OFFSET]) &&
                           (UTL_HTTP_CHAR_DIGIT_LAST >= text.data[UTL_HTTP_VERSION_MAJOR_OFFSET]);
        has_minor = (UTL_HTTP_CHAR_DIGIT_FIRST <= text.data[UTL_HTTP_VERSION_MINOR_OFFSET]) &&
                           (UTL_HTTP_CHAR_DIGIT_LAST >= text.data[UTL_HTTP_VERSION_MINOR_OFFSET]);
        is_valid =
            has_prefix && has_major && has_minor && (UTL_HTTP_CHAR_DOT == text.data[UTL_HTTP_VERSION_DOT_OFFSET]);
        if (is_valid)
        {
            (*major) = (uint8_t)(text.data[UTL_HTTP_VERSION_MAJOR_OFFSET] - UTL_HTTP_CHAR_DIGIT_FIRST);
            (*minor) = (uint8_t)(text.data[UTL_HTTP_VERSION_MINOR_OFFSET] - UTL_HTTP_CHAR_DIGIT_FIRST);
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Exactly one SP separates request-line components. Response reason bytes are preserved.
 */
static FORCE_INLINE bool_t ReadStartLine(Utl_Http_Head_t* head, Utl_Http_Text_t line)
{
    bool_t          is_valid;
    uint32_t        first;
    uint32_t        second;
    Utl_Http_Text_t version;
    uint64_t        status;
    uint32_t        status_bytes;
    bool_t          has_status;
    bool_t          has_status_separator;
    bool_t          has_reason_separator;
    bool_t          status_in_range;

    is_valid = FALSE;
    if (UTL_HTTP_REQUEST == head->kind)
    {
        for (first = 0; first < line.byteCount; ++first)
        {
            if (UTL_HTTP_CHAR_SPACE == line.data[first])
            {
                break;
            }
        }
        if ((0 < first) && (first < line.byteCount))
        {
            head->method.data      = line.data;
            head->method.byteCount = first;
            is_valid               = Utl_Http_IsToken(head->method);
            for (second = first + 1; second < line.byteCount; ++second)
            {
                if (UTL_HTTP_CHAR_SPACE == line.data[second])
                {
                    break;
                }
            }
            is_valid = is_valid && (second < line.byteCount);
            if (is_valid)
            {
                head->target.data      = line.data + first + 1;
                head->target.byteCount = second - first - 1;
                version.data           = line.data + second + 1;
                version.byteCount      = line.byteCount - second - 1;
                is_valid               = IsTarget(head->target) && ReadVersion(&head->major, &head->minor, version);
            }
        }
    }
    else if ((UTL_HTTP_RESPONSE == head->kind) && (UTL_HTTP_REASON_OFFSET <= line.byteCount))
    {
        version.data      = line.data;
        version.byteCount = UTL_HTTP_VERSION_BYTE_COUNT;
        is_valid          = ReadVersion(&head->major, &head->minor, version);
        status            = 0;
        status_bytes      = 0;
        has_status        = Utl_Decimal_ReadUint64(&status,
                                                      &status_bytes,
                                                      line.data + UTL_HTTP_STATUS_OFFSET,
                                                      UTL_HTTP_STATUS_DIGIT_COUNT);
        has_status           = has_status && (UTL_HTTP_STATUS_DIGIT_COUNT == status_bytes);
        has_status_separator = (UTL_HTTP_CHAR_SPACE == line.data[UTL_HTTP_VERSION_BYTE_COUNT]);
        has_reason_separator = (UTL_HTTP_CHAR_SPACE == line.data[UTL_HTTP_REASON_OFFSET - 1]);
        status_in_range      = (UTL_HTTP_STATUS_FIRST <= status) && (UTL_HTTP_STATUS_LAST >= status);
        is_valid             = is_valid && has_status && has_status_separator && has_reason_separator && status_in_range;
        if (is_valid)
        {
            head->status           = (uint16_t)status;
            head->reason.data      = line.data + UTL_HTTP_REASON_OFFSET;
            head->reason.byteCount = line.byteCount - UTL_HTTP_REASON_OFFSET;
            is_valid               = Utl_Http_IsFieldValue(head->reason);
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The scanner has consumed CRLF. Leading empty request lines are bounded before the start-line.
 */
static FORCE_INLINE void ReadCompletedLine(Utl_Http_HeadReader_t* reader, Utl_Http_Text_t line)
{
    bool_t           can_ignore;
    Utl_Http_Field_t field;

    if (!reader->inFields)
    {
        can_ignore = (0 == line.byteCount) && (UTL_HTTP_REQUEST == reader->head.kind);
        if (can_ignore)
        {
            ++reader->leadingEmptyLines;
            if (UTL_HTTP_MAX_EMPTY_MEMBERS < reader->leadingEmptyLines)
            {
                reader->result = UTL_HTTP_CAPACITY;
            }
        }
        else if (ReadStartLine(&reader->head, line))
        {
            reader->inFields = TRUE;
        }
        else
        {
            reader->result = UTL_HTTP_INVALID;
        }
    }
    else if (0 == line.byteCount)
    {
        reader->head.byteCount = reader->scan;
        reader->result         = UTL_HTTP_COMPLETE;
    }
    else if (!ReadField(&field, line))
    {
        reader->result = UTL_HTTP_INVALID;
    }
    else if (reader->head.fieldCount == reader->head.fieldCapacity)
    {
        reader->result = UTL_HTTP_CAPACITY;
    }
    else
    {
        reader->head.fields[reader->head.fieldCount] = field;
        ++reader->head.fieldCount;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Quoted pairs suppress quote/comma interpretation for their following octet. Empty items use a shared local bound.
 */
static FORCE_INLINE bool_t ReadListMember(Utl_Http_Text_t* member,
                                          bool_t*          hasMember,
                                          Utl_Http_Text_t  list,
                                          uint32_t*        cursor,
                                          uint32_t*        emptyMembers)
{
    bool_t          is_valid;
    uint32_t        start;
    bool_t          in_quotes;
    bool_t          escaped;
    uint32_t        end;
    Utl_Http_Text_t item;

    is_valid     = TRUE;
    (*hasMember) = FALSE;
    for (;;)
    {
        if ((*cursor) == list.byteCount)
        {
            break;
        }
        start     = (*cursor);
        in_quotes = FALSE;
        escaped   = FALSE;
        for (end = start; end < list.byteCount; ++end)
        {
            if (escaped)
            {
                escaped = FALSE;
            }
            else if (in_quotes && (UTL_HTTP_CHAR_BACKSLASH == list.data[end]))
            {
                escaped = TRUE;
            }
            else if (UTL_HTTP_CHAR_QUOTE == list.data[end])
            {
                in_quotes = !in_quotes;
            }
            else if (!in_quotes && (UTL_HTTP_CHAR_COMMA == list.data[end]))
            {
                break;
            }
        }
        if (in_quotes || escaped)
        {
            is_valid = FALSE;
            break;
        }
        item.data      = list.data + start;
        item.byteCount = end - start;
        item           = Trim(item);
        (*cursor)      = end;
        if (end < list.byteCount)
        {
            ++(*cursor);
            if ((*cursor) == list.byteCount)
            {
                ++(*emptyMembers);
            }
        }
        if (0 == item.byteCount)
        {
            ++(*emptyMembers);
        }
        if (UTL_HTTP_MAX_EMPTY_MEMBERS < (*emptyMembers))
        {
            is_valid = FALSE;
            break;
        }
        if (0 < item.byteCount)
        {
            (*member)    = item;
            (*hasMember) = TRUE;
            break;
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Quoted values accept quoted-pair escaping and obs-text; controls are rejected before parameter parsing.
 */
static FORCE_INLINE bool_t ReadParameters(Utl_Http_Text_t text, uint32_t cursor, bool_t valueRequired)
{
    bool_t   is_valid;
    uint32_t start;
    bool_t   has_equals;
    bool_t   has_quote_end;

    is_valid = TRUE;
    for (;;)
    {
        cursor = GetWhitespaceEnd(text.data, text.byteCount, cursor);
        if (cursor == text.byteCount)
        {
            break;
        }
        if (UTL_HTTP_CHAR_SEMICOLON != text.data[cursor])
        {
            is_valid = FALSE;
            break;
        }
        cursor = GetWhitespaceEnd(text.data, text.byteCount, cursor + 1);
        start  = cursor;
        for (;;)
        {
            if (cursor == text.byteCount)
            {
                break;
            }
            if (!IsTokenByte((byte_t)text.data[cursor]))
            {
                break;
            }
            ++cursor;
        }
        if (start == cursor)
        {
            is_valid = FALSE;
            break;
        }
        cursor     = GetWhitespaceEnd(text.data, text.byteCount, cursor);
        has_equals = (cursor < text.byteCount) && (UTL_HTTP_CHAR_EQUALS == text.data[cursor]);
        if (!has_equals)
        {
            if (valueRequired)
            {
                is_valid = FALSE;
                break;
            }
            continue;
        }
        cursor = GetWhitespaceEnd(text.data, text.byteCount, cursor + 1);
        if (cursor == text.byteCount)
        {
            is_valid = FALSE;
            break;
        }
        if (UTL_HTTP_CHAR_QUOTE == text.data[cursor])
        {
            ++cursor;
            has_quote_end = FALSE;
            for (;;)
            {
                if (cursor == text.byteCount)
                {
                    break;
                }
                if (UTL_HTTP_CHAR_QUOTE == text.data[cursor])
                {
                    ++cursor;
                    has_quote_end = TRUE;
                    break;
                }
                if (UTL_HTTP_CHAR_BACKSLASH == text.data[cursor])
                {
                    ++cursor;
                    if (cursor == text.byteCount)
                    {
                        break;
                    }
                }
                ++cursor;
            }
            if (!has_quote_end)
            {
                is_valid = FALSE;
                break;
            }
        }
        else
        {
            start = cursor;
            for (;;)
            {
                if (cursor == text.byteCount)
                {
                    break;
                }
                if (!IsTokenByte((byte_t)text.data[cursor]))
                {
                    break;
                }
                ++cursor;
            }
            if (start == cursor)
            {
                is_valid = FALSE;
                break;
            }
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Duplicate lengths must agree; chunked cannot be repeated or followed by another coding.
 */
static FORCE_INLINE bool_t ReadFramingField(HttpFramingFields_t* fields, Utl_Http_Text_t value, uint32_t kind)
{
    uint32_t        cursor;
    uint32_t        empty_members;
    bool_t          has_any;
    bool_t          is_valid;
    Utl_Http_Text_t member;
    bool_t          has_member;
    uint64_t        length;
    uint32_t        consumed;
    Utl_Http_Text_t coding;
    bool_t          is_chunked;

    cursor        = 0;
    empty_members = 0;
    has_any       = FALSE;
    for (;;)
    {
        is_valid = ReadListMember(&member, &has_member, value, &cursor, &empty_members);
        if (!is_valid || !has_member)
        {
            break;
        }
        has_any = TRUE;
        if (HTTP_FIELD_LENGTH == kind)
        {
            is_valid = Utl_Decimal_ReadUint64(&length, &consumed, member.data, member.byteCount);
            if (is_valid)
            {
                is_valid = (consumed == member.byteCount);
                if (fields->hasLength)
                {
                    is_valid = is_valid && (length == fields->length);
                }
                fields->length    = length;
                fields->hasLength = TRUE;
            }
        }
        else if (HTTP_FIELD_CONNECTION == kind)
        {
            is_valid = Utl_Http_IsToken(member);
            if (is_valid)
            {
                fields->mustClose = fields->mustClose || IsEqualLiteral(member, UTL_HTTP_CONNECTION_CLOSE, TRUE);
                fields->keepAlive = fields->keepAlive || IsEqualLiteral(member, UTL_HTTP_CONNECTION_KEEP_ALIVE, TRUE);
            }
        }
        else
        {
            for (consumed = 0; consumed < member.byteCount; ++consumed)
            {
                if (!IsTokenByte((byte_t)member.data[consumed]))
                {
                    break;
                }
            }
            coding.data      = member.data;
            coding.byteCount = consumed;
            is_chunked       = IsEqualLiteral(coding, UTL_HTTP_CODING_CHUNKED, TRUE);
            is_valid         = (0 < consumed) && !fields->hasChunked;
            if (is_valid)
            {
                is_valid = ReadParameters(member, consumed, TRUE);
                if (is_chunked)
                {
                    is_valid = is_valid && (consumed == member.byteCount);
                }
                fields->hasTransfer    = TRUE;
                fields->hasChunked     = is_chunked;
                fields->hasOtherCoding = fields->hasOtherCoding || !is_chunked;
            }
        }
        if (!is_valid)
        {
            break;
        }
    }
    return is_valid && has_any;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The remaining counted span is supplied directly to the shared decimal reader.
 */
static FORCE_INLINE bool_t ReadNumber(uint64_t* value, const char_t* buffer, uint32_t byteCount, uint32_t* cursor)
{
    bool_t   is_valid;
    uint32_t consumed;

    is_valid = Utl_Decimal_ReadUint64(value, &consumed, &buffer[(*cursor)], byteCount - (*cursor));
    if (is_valid)
    {
        (*cursor) += consumed;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The enclosing list reader checks trailing delimiters and capacity.
 */
static FORCE_INLINE bool_t ReadRangeMember(Utl_Http_Range_t* range,
                                           const char_t*     buffer,
                                           uint32_t          byteCount,
                                           uint32_t*         cursor)
{
    bool_t is_valid;
    bool_t has_last;

    range->kind         = UTL_HTTP_RANGE_OPEN;
    range->first        = 0;
    range->last         = 0;
    range->suffixLength = 0;
    if (UTL_HTTP_CHAR_HYPHEN == buffer[(*cursor)])
    {
        range->kind = UTL_HTTP_RANGE_SUFFIX;
        ++(*cursor);
        is_valid = ReadNumber(&range->suffixLength, buffer, byteCount, cursor);
    }
    else
    {
        is_valid = ReadNumber(&range->first, buffer, byteCount, cursor);
        if (is_valid)
        {
            is_valid = ((*cursor) < byteCount);
        }
        if (is_valid)
        {
            is_valid = (UTL_HTTP_CHAR_HYPHEN == buffer[(*cursor)]);
        }
        if (is_valid)
        {
            ++(*cursor);
            has_last = FALSE;
            if ((*cursor) < byteCount)
            {
                has_last =
                    (UTL_HTTP_CHAR_DIGIT_FIRST <= buffer[(*cursor)]) && (UTL_HTTP_CHAR_DIGIT_LAST >= buffer[(*cursor)]);
            }
            if (has_last)
            {
                range->kind = UTL_HTTP_RANGE_CLOSED;
                is_valid    = ReadNumber(&range->last, buffer, byteCount, cursor);
                is_valid    = is_valid && (range->first <= range->last);
            }
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The enclosing writer has validated every source and the complete output size.
 *  A descending counted copy keeps the Release/LTCG build independent of memcpy.
 */
static FORCE_INLINE void WriteSpan(char_t* buffer, uint32_t* cursor, Utl_Http_Text_t text)
{
    uint32_t index;

    for (index = text.byteCount; 0 < index;)
    {
        --index;
        buffer[(*cursor) + index] = text.data[index];
    }
    (*cursor) += text.byteCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteLiteral(char_t* buffer, uint32_t* cursor, const char_t* literal)
{
    uint32_t index;

    for (index = 0; UTL_HTTP_CHAR_NUL != literal[index]; ++index)
    {
        buffer[(*cursor) + index] = literal[index];
    }
    (*cursor) += index;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The enclosing date writer has checked the fixed width and complete output capacity.
 */
static FORCE_INLINE void WriteDateNumber(char_t* output, uint32_t value, uint32_t width)
{
    uint32_t cursor;

    for (cursor = width; 0 < cursor;)
    {
        --cursor;
        output[cursor]  = (char_t)(UTL_HTTP_CHAR_DIGIT_FIRST + (value % DATE_RADIX));
        value          /= DATE_RADIX;
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  All scan and candidate members reset. Field bytes remain caller-owned scratch.
 */
void Utl_Http_InitializeHeadReader(Utl_Http_HeadReader_t* reader,
                                   Utl_Http_Kind_t        kind,
                                   Utl_Http_Field_t*      fields,
                                   uint32_t               fieldCapacity)
{
    bool_t has_slots;
    bool_t valid_kind;

    LOGASSERT(NULL != reader);

    has_slots  = (NULL != fields) || (0 == fieldCapacity);
    valid_kind = (UTL_HTTP_REQUEST == kind) || (UTL_HTTP_RESPONSE == kind);
    LOGASSERT(has_slots);
    LOGASSERT(valid_kind);
    reader->head.fields           = fields;
    reader->head.fieldCapacity    = fieldCapacity;
    reader->head.fieldCount       = 0;
    reader->head.kind             = kind;
    reader->head.major            = 0;
    reader->head.minor            = 0;
    reader->head.method.data      = NULL;
    reader->head.method.byteCount = 0;
    reader->head.target.data      = NULL;
    reader->head.target.byteCount = 0;
    reader->head.status           = 0;
    reader->head.reason.data      = NULL;
    reader->head.reason.byteCount = 0;
    reader->head.byteCount        = 0;
    reader->scan                  = 0;
    reader->lineStart             = 0;
    reader->leadingEmptyLines     = 0;
    reader->inFields              = FALSE;
    reader->pendingCr             = FALSE;
    reader->input                 = NULL;
    reader->result                = UTL_HTTP_INCOMPLETE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Http_IsToken(Utl_Http_Text_t text)
{
    bool_t   is_valid;
    uint32_t index;

    is_valid = (NULL != text.data) && (0 < text.byteCount);
    if (is_valid)
    {
        for (index = 0; index < text.byteCount; ++index)
        {
            if (!IsTokenByte((byte_t)text.data[index]))
            {
                is_valid = FALSE;
                break;
            }
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Validation treats bytes above ASCII as opaque octets, not Unicode.
 */
bool_t Utl_Http_IsFieldValue(Utl_Http_Text_t text)
{
    bool_t   is_valid;
    uint32_t index;
    byte_t   value;
    bool_t   is_visible;
    bool_t   is_tab;

    is_valid = (0 == text.byteCount) || (NULL != text.data);
    if (is_valid)
    {
        for (index = 0; index < text.byteCount; ++index)
        {
            value      = (byte_t)text.data[index];
            is_visible = (UTL_HTTP_CHAR_SPACE <= value) && (UTL_HTTP_CHAR_DEL != value);
            is_tab     = (UTL_HTTP_CHAR_TAB == value);
            is_valid   = is_visible || is_tab;
            if (!is_valid)
            {
                break;
            }
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Case folding changes ASCII uppercase only; methods normally use exact comparison.
 */
bool_t Utl_Http_IsTextEqual(Utl_Http_Text_t left, Utl_Http_Text_t right, bool_t ignoreCase)
{
    bool_t   equal;
    uint32_t index;
    byte_t   a;
    byte_t   b;

    equal = (left.byteCount == right.byteCount);
    if (equal && (0 < left.byteCount))
    {
        equal = (NULL != left.data) && (NULL != right.data);
        if (equal)
        {
            for (index = 0; index < left.byteCount; ++index)
            {
                a = (byte_t)left.data[index];
                b = (byte_t)right.data[index];
                if (ignoreCase)
                {
                    a = FoldAscii(a);
                    b = FoldAscii(b);
                }
                if (a != b)
                {
                    equal = FALSE;
                    break;
                }
            }
        }
    }
    return equal;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Writers use this query to preflight all data and capacity before modifying output.
 */
uint32_t Utl_Http_GetHeadByteCount(const Utl_Http_Head_t* head)
{
    bool_t   is_valid;
    uint64_t total;
    uint32_t count;
    uint32_t index;

    is_valid = (NULL != head);
    total    = 0;
    count    = 0;
    if (is_valid)
    {
        is_valid = (UTL_HTTP_VERSION_MAX_DIGIT >= head->major) && (UTL_HTTP_VERSION_MAX_DIGIT >= head->minor) &&
                   (head->fieldCount <= head->fieldCapacity);
        if (0 < head->fieldCount)
        {
            is_valid = is_valid && (NULL != head->fields);
        }
    }
    if (is_valid)
    {
        if (UTL_HTTP_REQUEST == head->kind)
        {
            is_valid = Utl_Http_IsToken(head->method) && IsTarget(head->target);
            total    = (uint64_t)head->method.byteCount + head->target.byteCount + UTL_HTTP_VERSION_BYTE_COUNT + 2 +
                       (2 * UTL_HTTP_LINE_END_BYTE_COUNT);
        }
        else if (UTL_HTTP_RESPONSE == head->kind)
        {
            is_valid = (UTL_HTTP_STATUS_FIRST <= head->status) && (UTL_HTTP_STATUS_LAST >= head->status) &&
                       Utl_Http_IsFieldValue(head->reason);
            total = (uint64_t)head->reason.byteCount + UTL_HTTP_REASON_OFFSET + (2 * UTL_HTTP_LINE_END_BYTE_COUNT);
        }
        else
        {
            is_valid = FALSE;
        }
    }
    if (is_valid)
    {
        for (index = 0; index < head->fieldCount; ++index)
        {
            is_valid = Utl_Http_IsToken(head->fields[index].name) && Utl_Http_IsFieldValue(head->fields[index].value);
            if (!is_valid)
            {
                break;
            }
            total += (uint64_t)head->fields[index].name.byteCount + head->fields[index].value.byteCount +
                     UTL_HTTP_FIELD_SEPARATOR_BYTE_COUNT + UTL_HTTP_LINE_END_BYTE_COUNT;
        }
        if (is_valid && (UINT32_MAX >= total))
        {
            count = (uint32_t)total;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Calendar bounds precede table access. Gregorian century exceptions distinguish 1900 from 2000.
 */
uint32_t Utl_Http_GetDateByteCount(const Utl_Http_Date_t* date)
{
    bool_t   has_year;
    bool_t   has_month;
    bool_t   has_hour;
    bool_t   has_minute;
    bool_t   has_second;
    bool_t   has_calendar;
    bool_t   has_clock;
    bool_t   is_valid;
    uint32_t count;
    uint32_t days;
    bool_t   is_leap;

    LOGASSERT(NULL != date);

    has_year     = (0 < date->year) && (DATE_YEAR_MAX >= date->year);
    has_month    = (0 < date->month) && (DATE_MONTH_COUNT >= date->month);
    has_hour     = (DATE_HOUR_MAX >= date->hour);
    has_minute   = (DATE_MINUTE_MAX >= date->minute);
    has_second   = (DATE_SECOND_MAX >= date->second);
    has_calendar = has_year && has_month;
    has_clock    = has_hour && has_minute && has_second;
    is_valid     = has_calendar && has_clock;
    count        = 0;
    if (is_valid)
    {
        days    = c_monthDays[date->month - 1];
        is_leap = (0 == (date->year % 4)) && ((0 != (date->year % 100)) || (0 == (date->year % 400)));
        if ((DATE_FEBRUARY == date->month) && is_leap)
        {
            ++days;
        }
        is_valid = (0 < date->day) && (days >= date->day);
        if (is_valid)
        {
            count = UTL_HTTP_DATE_BYTE_COUNT;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Bodyless and tunnel responses apply before length/coding fields. Other transfer codings remain visible so callers can select a decoder.
 */
bool_t Utl_Http_DeriveFraming(Utl_Http_Framing_t* framing, const Utl_Http_Head_t* head, Utl_Http_Text_t responseTo)
{
    bool_t                  is_valid;
    bool_t                  is_response;
    bool_t                  is_informational;
    bool_t                  has_no_body;
    bool_t                  is_tunnel;
    HttpFramingFields_t     fields;
    uint32_t                index;
    const Utl_Http_Field_t* field;
    Utl_Http_Framing_t      result;

    is_valid = (NULL != framing) && (0 < Utl_Http_GetHeadByteCount(head));
    if (is_valid)
    {
        is_valid = (1 == head->major);
    }
    if (is_valid)
    {
        is_response = (UTL_HTTP_RESPONSE == head->kind);
        if (is_response)
        {
            is_valid = Utl_Http_IsToken(responseTo);
        }
        is_informational =
            (UTL_HTTP_STATUS_FIRST <= head->status) && (UTL_HTTP_STATUS_INFORMATIONAL_LAST >= head->status);
        has_no_body = is_response &&
                      (IsEqualLiteral(responseTo, UTL_HTTP_METHOD_HEAD, FALSE) || is_informational ||
                       (UTL_HTTP_STATUS_NO_CONTENT == head->status) || (UTL_HTTP_STATUS_NOT_MODIFIED == head->status));
        is_tunnel = is_response && IsEqualLiteral(responseTo, UTL_HTTP_METHOD_CONNECT, FALSE) &&
                      (UTL_HTTP_STATUS_SUCCESS_FIRST <= head->status) && (UTL_HTTP_STATUS_SUCCESS_LAST >= head->status);
        fields.hasLength      = FALSE;
        fields.length         = 0;
        fields.hasTransfer    = FALSE;
        fields.hasChunked     = FALSE;
        fields.hasOtherCoding = FALSE;
        fields.mustClose      = FALSE;
        fields.keepAlive      = FALSE;
        fields.hostCount      = 0;
        for (index = 0; index < head->fieldCount; ++index)
        {
            if (!is_valid)
            {
                break;
            }
            field = &head->fields[index];
            if (IsEqualLiteral(field->name, UTL_HTTP_FIELD_CONNECTION, TRUE))
            {
                is_valid = ReadFramingField(&fields, field->value, HTTP_FIELD_CONNECTION);
            }
            else if (IsEqualLiteral(field->name, UTL_HTTP_FIELD_HOST, TRUE))
            {
                ++fields.hostCount;
            }
            else if (!has_no_body && !is_tunnel)
            {
                if (IsEqualLiteral(field->name, UTL_HTTP_FIELD_CONTENT_LENGTH, TRUE))
                {
                    is_valid = ReadFramingField(&fields, field->value, HTTP_FIELD_LENGTH);
                }
                else if (IsEqualLiteral(field->name, UTL_HTTP_FIELD_TRANSFER_ENCODING, TRUE))
                {
                    is_valid = ReadFramingField(&fields, field->value, HTTP_FIELD_TRANSFER);
                }
            }
        }
        if (!is_response && (1 <= head->minor))
        {
            is_valid = is_valid && (1 == fields.hostCount);
        }
        if (fields.hasTransfer)
        {
            is_valid = is_valid && !fields.hasLength && (1 <= head->minor);
            if (!is_response)
            {
                is_valid = is_valid && fields.hasChunked;
            }
        }
        if (is_valid)
        {
            result.kind                   = UTL_HTTP_BODY_NONE;
            result.contentLength          = 0;
            result.keepAlive              = ((1 <= head->minor) || fields.keepAlive) && !fields.mustClose;
            result.hasOtherTransferCoding = fields.hasOtherCoding;
            if (has_no_body)
            {
                result.kind = UTL_HTTP_BODY_NONE;
            }
            else if (is_tunnel)
            {
                result.kind      = UTL_HTTP_BODY_TUNNEL;
                result.keepAlive = FALSE;
            }
            else if (fields.hasTransfer)
            {
                result.kind = fields.hasChunked ? UTL_HTTP_BODY_CHUNKED : UTL_HTTP_BODY_CLOSE;
            }
            else if (fields.hasLength)
            {
                result.kind          = UTL_HTTP_BODY_LENGTH;
                result.contentLength = fields.length;
            }
            else if (is_response)
            {
                result.kind = UTL_HTTP_BODY_CLOSE;
            }
            if (UTL_HTTP_BODY_CLOSE == result.kind)
            {
                result.keepAlive = FALSE;
            }
            (*framing) = result;
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Offset/count representation avoids underflow for the RFC's satisfiable empty suffix case.
 */
bool_t Utl_Http_ResolveRange(Utl_Http_RangeInterval_t* interval,
                             const Utl_Http_Range_t*   range,
                             uint64_t                  representationBytes)
{
    bool_t                   has_arguments;
    bool_t                   is_satisfiable;
    Utl_Http_RangeInterval_t result;
    uint64_t                 last;

    has_arguments    = (NULL != interval) && (NULL != range);
    is_satisfiable   = has_arguments;
    result.offset    = 0;
    result.byteCount = 0;
    if (is_satisfiable)
    {
        is_satisfiable = IsValidRange(range);
    }
    if (is_satisfiable)
    {
        if (UTL_HTTP_RANGE_SUFFIX == range->kind)
        {
            is_satisfiable   = (0 < range->suffixLength);
            result.byteCount = range->suffixLength;
            if (representationBytes < result.byteCount)
            {
                result.byteCount = representationBytes;
            }
            result.offset = representationBytes - result.byteCount;
        }
        else
        {
            is_satisfiable = (range->first < representationBytes);
            if (is_satisfiable)
            {
                last = representationBytes - 1;
                if (UTL_HTTP_RANGE_CLOSED == range->kind)
                {
                    if (range->last < last)
                    {
                        last = range->last;
                    }
                }
                result.offset    = range->first;
                result.byteCount = (last - range->first) + 1;
            }
        }
    }
    if (is_satisfiable)
    {
        (*interval) = result;
    }
    return is_satisfiable;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A uint64_t accumulator includes the prefix and comma separators before narrowing.
 */
uint32_t Utl_Http_GetRangeByteCount(const Utl_Http_RangeSet_t* request)
{
    bool_t   is_valid;
    uint64_t total;
    uint32_t count;
    uint32_t index;

    is_valid = (NULL != request);
    total    = RANGE_PREFIX_BYTES;
    count    = 0;
    if (is_valid)
    {
        is_valid = (NULL != request->ranges) && (0 < request->count) && (request->count <= request->capacity);
    }
    if (is_valid)
    {
        total += request->count - 1;
        for (index = 0; index < request->count; ++index)
        {
            is_valid = IsValidRange(&request->ranges[index]);
            if (!is_valid)
            {
                break;
            }
            total += GetRangeMemberByteCount(&request->ranges[index]);
        }
        if (is_valid && (UINT32_MAX >= total))
        {
            count = (uint32_t)total;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each integer occupies at most twenty decimal bytes.
 */
uint32_t Utl_Http_GetContentRangeByteCount(const Utl_Http_ContentRange_t* content)
{
    bool_t   is_valid;
    uint32_t count;

    is_valid = (NULL != content);
    count    = 0;
    if (is_valid)
    {
        count = RANGE_PREFIX_BYTES + 1;
        if (content->hasRange)
        {
            is_valid  = (content->first <= content->last);
            count    += Utl_Decimal_GetUint64ByteCount(content->first);
            count    += Utl_Decimal_GetUint64ByteCount(content->last);
            ++count;
            if (content->hasCompleteLength)
            {
                is_valid = is_valid && (content->last < content->completeLength);
            }
        }
        else
        {
            is_valid = content->hasCompleteLength;
            ++count;
        }
        if (content->hasCompleteLength)
        {
            count += Utl_Decimal_GetUint64ByteCount(content->completeLength);
        }
        else
        {
            ++count;
        }
    }
    if (!is_valid)
    {
        count = 0;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  RFC 9110 sections 4.2.1 through 4.2.4 define default ports and reject received
 *  userinfo. A native consumer must not reinterpret explicit port zero as default.
 */
bool_t Utl_Http_GetUrlEndpoint(const Utl_Url_t* url, Utl_Http_Endpoint_t* endpoint)
{
    bool_t              valid;
    bool_t              has_host;
    bool_t              has_authority;
    Utl_Http_Text_t     scheme;
    Utl_Http_Text_t     expected;
    bool_t              plain;
    bool_t              secure;
    Utl_Http_Endpoint_t candidate;
    bool_t              has_number;

    LOGASSERT(NULL != url);
    LOGASSERT(NULL != endpoint);

    valid         = Utl_Url_IsValid(url);
    has_host      = (0 < url->host.byteCount);
    has_authority = url->hasScheme && url->hasAuthority && !url->hasUserInfo && has_host;
    valid         = valid && has_authority;
    if (valid)
    {
        scheme.data        = url->scheme.data;
        scheme.byteCount   = url->scheme.byteCount;
        expected.data      = UTL_HTTP_SCHEME;
        expected.byteCount = sizeof(UTL_HTTP_SCHEME) - 1;
        plain              = Utl_Http_IsTextEqual(scheme, expected, TRUE);
        expected.data      = UTL_HTTP_SECURE_SCHEME;
        expected.byteCount = sizeof(UTL_HTTP_SECURE_SCHEME) - 1;
        secure             = Utl_Http_IsTextEqual(scheme, expected, TRUE);
        valid              = plain || secure;
        if (valid)
        {
            candidate.host   = url->host;
            candidate.secure = secure;
            candidate.port   = UTL_HTTP_DEFAULT_PORT;
            if (secure)
            {
                candidate.port = UTL_HTTP_SECURE_PORT;
            }
            has_number = url->hasPort && (0 < url->port.byteCount);
            if (has_number)
            {
                valid = Utl_Url_GetPort(url, &candidate.port);
            }
            if (valid)
            {
                *endpoint = candidate;
            }
        }
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Wide accumulation includes delimiters without wrapping at UINT32_MAX.
 */
bool_t Utl_Http_GetOriginTargetByteCount(const Utl_Url_t* url, uint32_t* byteCount)
{
    bool_t              valid;
    Utl_Http_Endpoint_t endpoint;
    uint64_t            count;

    LOGASSERT(NULL != url);
    LOGASSERT(NULL != byteCount);

    valid = Utl_Http_GetUrlEndpoint(url, &endpoint);
    if (valid)
    {
        count = url->path.byteCount;
        if (0 == count)
        {
            count = 1;
        }
        if (url->hasQuery)
        {
            ++count;
            count += url->query.byteCount;
        }
        valid = (UINT32_MAX >= count);
        if (valid)
        {
            *byteCount = (uint32_t)count;
        }
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A caller enumerates duplicates by resuming at one past the returned index.
 */
uint32_t Utl_Http_FindField(const Utl_Http_Head_t* head, Utl_Http_Text_t name, uint32_t startIndex)
{
    uint32_t found;
    bool_t   has_fields;
    uint32_t index;

    found      = UTL_HTTP_NO_FIELD;
    has_fields = (NULL != head);
    if (has_fields)
    {
        has_fields = (NULL != head->fields) && (head->fieldCount <= head->fieldCapacity);
    }
    if (has_fields)
    {
        for (index = startIndex; index < head->fieldCount; ++index)
        {
            if (Utl_Http_IsTextEqual(head->fields[index].name, name, TRUE))
            {
                found = index;
                break;
            }
        }
    }
    return found;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only new bytes are scanned. Caller misuse asserts; malformed wire syntax remains a latched read result.
 */
Utl_Http_Result_t Utl_Http_ReadHeadIncremental(Utl_Http_HeadReader_t* reader, const char_t* buffer, uint32_t byteCount)
{
    bool_t          has_input;
    bool_t          same_input;
    bool_t          has_bounds;
    bool_t          at_end;
    bool_t          is_terminal;
    char_t          value;
    Utl_Http_Text_t line;
    bool_t          failed;

    LOGASSERT(NULL != reader);

    if (UTL_HTTP_INCOMPLETE == reader->result)
    {
        has_input  = (NULL != buffer) || (0 == byteCount);
        same_input = (0 == reader->scan) || (reader->input == buffer);
        has_bounds = (reader->scan <= byteCount) && (reader->lineStart <= reader->scan);
        LOGASSERT(has_input);
        LOGASSERT(same_input);
        LOGASSERT(has_bounds);

        reader->input = buffer;
        for (;;)
        {
            at_end      = (reader->scan == byteCount);
            is_terminal = (UTL_HTTP_INCOMPLETE != reader->result);
            if (at_end || is_terminal)
            {
                break;
            }
            value = buffer[reader->scan];
            ++reader->scan;
            if (reader->pendingCr)
            {
                reader->pendingCr = FALSE;
                if (UTL_HTTP_CHAR_LF != value)
                {
                    reader->result = UTL_HTTP_INVALID;
                }
                else
                {
                    LOGASSERT(UTL_HTTP_LINE_END_BYTE_COUNT <= (reader->scan - reader->lineStart));
                    line.data         = buffer + reader->lineStart;
                    line.byteCount    = reader->scan - reader->lineStart - UTL_HTTP_LINE_END_BYTE_COUNT;
                    reader->lineStart = reader->scan;
                    ReadCompletedLine(reader, line);
                }
            }
            else if (UTL_HTTP_CHAR_CR == value)
            {
                reader->pendingCr = TRUE;
            }
            else if (UTL_HTTP_CHAR_LF == value)
            {
                reader->result = UTL_HTTP_INVALID;
            }
        }
    }
    failed = (UTL_HTTP_INVALID == reader->result) || (UTL_HTTP_CAPACITY == reader->result);
    if (failed)
    {
        reader->head.fieldCount = 0;
        reader->head.byteCount  = 0;
    }
    return reader->result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The one-shot entry point uses the same incremental reader and publishes a complete head only.
 */
Utl_Http_Result_t Utl_Http_ReadHead(Utl_Http_Head_t* head,
                                    Utl_Http_Kind_t  kind,
                                    const char_t*    buffer,
                                    uint32_t         byteCount)
{
    Utl_Http_HeadReader_t reader;
    Utl_Http_Result_t     result;

    LOGASSERT(NULL != head);
    LOGASSERT((NULL != buffer) || (0 == byteCount));

    Utl_Http_InitializeHeadReader(&reader, kind, head->fields, head->fieldCapacity);
    result           = Utl_Http_ReadHeadIncremental(&reader, buffer, byteCount);
    head->fieldCount = 0;
    head->byteCount  = 0;
    if (UTL_HTTP_COMPLETE == result)
    {
        (*head) = reader.head;
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The count is published only after all members have valid syntax and fit the supplied capacity.
 */
bool_t Utl_Http_ReadRange(Utl_Http_RangeSet_t* request, const char_t* buffer, uint32_t byteCount)
{
    bool_t           is_valid;
    uint32_t         cursor;
    uint32_t         count;
    uint32_t         empty_members;
    Utl_Http_Range_t candidate;

    is_valid      = (NULL != request) && (NULL != buffer);
    cursor        = 0;
    count         = 0;
    empty_members = 0;
    if (NULL != request)
    {
        request->count = 0;
        is_valid       = is_valid && (NULL != request->ranges);
    }
    if (is_valid)
    {
        cursor   = GetWhitespaceEnd(buffer, byteCount, cursor);
        is_valid = HasBytesUnit(buffer, byteCount, &cursor, UTL_HTTP_CHAR_EQUALS);
    }
    for (;;)
    {
        if (!is_valid)
        {
            break;
        }
        cursor = GetWhitespaceEnd(buffer, byteCount, cursor);
        if (byteCount == cursor)
        {
            break;
        }
        if (UTL_HTTP_CHAR_COMMA == buffer[cursor])
        {
            ++empty_members;
            ++cursor;
            is_valid = (UTL_HTTP_MAX_EMPTY_MEMBERS >= empty_members);
            continue;
        }
        is_valid = ReadRangeMember(&candidate, buffer, byteCount, &cursor);
        if (is_valid)
        {
            is_valid = (count < request->capacity);
        }
        if (is_valid)
        {
            request->ranges[count] = candidate;
            ++count;
            cursor = GetWhitespaceEnd(buffer, byteCount, cursor);
            if (cursor < byteCount)
            {
                is_valid = (UTL_HTTP_CHAR_COMMA == buffer[cursor]);
                ++cursor;
            }
        }
    }
    is_valid = is_valid && (0 < count);
    if (is_valid)
    {
        request->count = count;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Syntax is read into a local model and the same model query validates its semantic bounds.
 */
bool_t Utl_Http_ReadContentRange(Utl_Http_ContentRange_t* content, const char_t* buffer, uint32_t byteCount)
{
    bool_t                  is_valid;
    uint32_t                cursor;
    Utl_Http_ContentRange_t result;

    is_valid                 = (NULL != content) && (NULL != buffer);
    cursor                   = 0;
    result.hasRange          = TRUE;
    result.hasCompleteLength = TRUE;
    result.first             = 0;
    result.last              = 0;
    result.completeLength    = 0;
    if (is_valid)
    {
        cursor   = GetWhitespaceEnd(buffer, byteCount, cursor);
        is_valid = HasBytesUnit(buffer, byteCount, &cursor, UTL_HTTP_CHAR_SPACE);
    }
    if (is_valid)
    {
        is_valid = (cursor < byteCount);
    }
    if (is_valid)
    {
        if (UTL_HTTP_CHAR_ASTERISK == buffer[cursor])
        {
            result.hasRange = FALSE;
            ++cursor;
        }
        else
        {
            is_valid = ReadNumber(&result.first, buffer, byteCount, &cursor);
            if (is_valid)
            {
                is_valid = (cursor < byteCount);
            }
            if (is_valid)
            {
                is_valid = (UTL_HTTP_CHAR_HYPHEN == buffer[cursor]);
                ++cursor;
            }
            if (is_valid)
            {
                is_valid = ReadNumber(&result.last, buffer, byteCount, &cursor);
            }
        }
    }
    if (is_valid)
    {
        is_valid = (cursor < byteCount);
    }
    if (is_valid)
    {
        is_valid = (UTL_HTTP_CHAR_SLASH == buffer[cursor]);
        ++cursor;
    }
    if (is_valid)
    {
        is_valid = (cursor < byteCount);
    }
    if (is_valid)
    {
        if (UTL_HTTP_CHAR_ASTERISK == buffer[cursor])
        {
            result.hasCompleteLength = FALSE;
            ++cursor;
        }
        else
        {
            is_valid = ReadNumber(&result.completeLength, buffer, byteCount, &cursor);
        }
    }
    if (is_valid)
    {
        cursor   = GetWhitespaceEnd(buffer, byteCount, cursor);
        is_valid = (byteCount == cursor) && (0 < Utl_Http_GetContentRangeByteCount(&result));
    }
    if (is_valid)
    {
        (*content) = result;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The size query excludes integer overflow; exact status digits need no formatter or CRT.
 */
uint32_t Utl_Http_WriteHead(const Utl_Http_Head_t* head, char_t* buffer, uint32_t capacity)
{
    uint32_t count;
    bool_t   can_write;
    uint32_t cursor;
    uint32_t index;

    count     = Utl_Http_GetHeadByteCount(head);
    can_write = (0 < count) && (count <= capacity) && (NULL != buffer);
    cursor    = 0;
    if (can_write)
    {
        if (UTL_HTTP_REQUEST == head->kind)
        {
            WriteSpan(buffer, &cursor, head->method);
            buffer[cursor] = UTL_HTTP_CHAR_SPACE;
            ++cursor;
            WriteSpan(buffer, &cursor, head->target);
            buffer[cursor] = UTL_HTTP_CHAR_SPACE;
            ++cursor;
        }
        WriteLiteral(buffer, &cursor, UTL_HTTP_VERSION_PREFIX);
        buffer[cursor]      = (char_t)(UTL_HTTP_CHAR_DIGIT_FIRST + head->major);
        buffer[cursor + 1]  = UTL_HTTP_CHAR_DOT;
        buffer[cursor + 2]  = (char_t)(UTL_HTTP_CHAR_DIGIT_FIRST + head->minor);
        cursor             += 3;
        if (UTL_HTTP_RESPONSE == head->kind)
        {
            buffer[cursor]      = UTL_HTTP_CHAR_SPACE;
            buffer[cursor + 1]  = (char_t)(UTL_HTTP_CHAR_DIGIT_FIRST + (head->status / 100));
            buffer[cursor + 2]  = (char_t)(UTL_HTTP_CHAR_DIGIT_FIRST + ((head->status / 10) % 10));
            buffer[cursor + 3]  = (char_t)(UTL_HTTP_CHAR_DIGIT_FIRST + (head->status % 10));
            buffer[cursor + 4]  = UTL_HTTP_CHAR_SPACE;
            cursor             += 5;
            WriteSpan(buffer, &cursor, head->reason);
        }
        WriteLiteral(buffer, &cursor, UTL_HTTP_LINE_END);
        for (index = 0; index < head->fieldCount; ++index)
        {
            WriteSpan(buffer, &cursor, head->fields[index].name);
            WriteLiteral(buffer, &cursor, UTL_HTTP_FIELD_SEPARATOR);
            WriteSpan(buffer, &cursor, head->fields[index].value);
            WriteLiteral(buffer, &cursor, UTL_HTTP_LINE_END);
        }
        WriteLiteral(buffer, &cursor, UTL_HTTP_LINE_END);
    }
    return cursor;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Weekday uses Gregorian arithmetic with January and February counted in the preceding year.
 */
uint32_t Utl_Http_WriteDate(const Utl_Http_Date_t* date, char_t* buffer, uint32_t capacity)
{
    uint32_t count;
    bool_t   can_write;
    uint32_t year;
    uint32_t weekday;
    uint32_t index;

    LOGASSERT(NULL != date);
    LOGASSERT(NULL != buffer);

    count     = Utl_Http_GetDateByteCount(date);
    can_write = (0 < count) && (count <= capacity);
    if (can_write)
    {
        year = date->year;
        if (DATE_FEBRUARY >= date->month)
        {
            --year;
        }
        weekday = (year + (year / 4) - (year / 100) + (year / 400) + c_weekdayOffsets[date->month - 1] + date->day) %
                  DATE_WEEKDAY_COUNT;
        for (index = 0; index < DATE_NAME_BYTES; ++index)
        {
            buffer[index]                     = c_weekdays[weekday][index];
            buffer[DATE_MONTH_OFFSET + index] = c_months[date->month - 1][index];
            buffer[DATE_ZONE_OFFSET + index]  = UTL_HTTP_DATE_GMT[index];
        }
        buffer[DATE_NAME_BYTES]        = UTL_HTTP_CHAR_COMMA;
        buffer[DATE_DAY_OFFSET - 1]    = UTL_HTTP_CHAR_SPACE;
        buffer[DATE_MONTH_OFFSET - 1]  = UTL_HTTP_CHAR_SPACE;
        buffer[DATE_YEAR_OFFSET - 1]   = UTL_HTTP_CHAR_SPACE;
        buffer[DATE_HOUR_OFFSET - 1]   = UTL_HTTP_CHAR_SPACE;
        buffer[DATE_MINUTE_OFFSET - 1] = UTL_HTTP_CHAR_COLON;
        buffer[DATE_SECOND_OFFSET - 1] = UTL_HTTP_CHAR_COLON;
        buffer[DATE_ZONE_OFFSET - 1]   = UTL_HTTP_CHAR_SPACE;
        WriteDateNumber(buffer + DATE_DAY_OFFSET, date->day, DATE_TIME_DIGITS);
        WriteDateNumber(buffer + DATE_YEAR_OFFSET, date->year, DATE_YEAR_DIGITS);
        WriteDateNumber(buffer + DATE_HOUR_OFFSET, date->hour, DATE_TIME_DIGITS);
        WriteDateNumber(buffer + DATE_MINUTE_OFFSET, date->minute, DATE_TIME_DIGITS);
        WriteDateNumber(buffer + DATE_SECOND_OFFSET, date->second, DATE_TIME_DIGITS);
    }
    else
    {
        count = 0;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every append is bounded by the precomputed total and uses caller-owned storage.
 */
uint32_t Utl_Http_WriteRange(const Utl_Http_RangeSet_t* request, char_t* buffer, uint32_t capacity)
{
    uint32_t                count;
    bool_t                  can_write;
    uint32_t                cursor;
    uint32_t                index;
    const Utl_Http_Range_t* range;

    count     = Utl_Http_GetRangeByteCount(request);
    can_write = (NULL != buffer) && (0 < count) && (count <= capacity);
    if (can_write)
    {
        for (cursor = 0; cursor < RANGE_UNIT_BYTES; ++cursor)
        {
            buffer[cursor] = c_rangeUnit[cursor];
        }
        buffer[cursor] = UTL_HTTP_CHAR_EQUALS;
        ++cursor;
        for (index = 0; index < request->count; ++index)
        {
            range = &request->ranges[index];
            if (0 < index)
            {
                buffer[cursor] = UTL_HTTP_CHAR_COMMA;
                ++cursor;
            }
            if (UTL_HTTP_RANGE_SUFFIX != range->kind)
            {
                cursor += Utl_Decimal_WriteUint64(range->first, &buffer[cursor], count - cursor);
            }
            buffer[cursor] = UTL_HTTP_CHAR_HYPHEN;
            ++cursor;
            if (UTL_HTTP_RANGE_SUFFIX == range->kind)
            {
                cursor += Utl_Decimal_WriteUint64(range->suffixLength, &buffer[cursor], count - cursor);
            }
            else if (UTL_HTTP_RANGE_CLOSED == range->kind)
            {
                cursor += Utl_Decimal_WriteUint64(range->last, &buffer[cursor], count - cursor);
            }
        }
    }
    else
    {
        count = 0;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The same semantic count query is used by standalone sizing and writing.
 */
uint32_t Utl_Http_WriteContentRange(const Utl_Http_ContentRange_t* content, char_t* buffer, uint32_t capacity)
{
    uint32_t count;
    bool_t   can_write;
    uint32_t cursor;

    count     = Utl_Http_GetContentRangeByteCount(content);
    can_write = (NULL != buffer) && (0 < count) && (count <= capacity);
    if (can_write)
    {
        for (cursor = 0; cursor < RANGE_UNIT_BYTES; ++cursor)
        {
            buffer[cursor] = c_rangeUnit[cursor];
        }
        buffer[cursor] = UTL_HTTP_CHAR_SPACE;
        ++cursor;
        if (content->hasRange)
        {
            cursor         += Utl_Decimal_WriteUint64(content->first, &buffer[cursor], count - cursor);
            buffer[cursor]  = UTL_HTTP_CHAR_HYPHEN;
            ++cursor;
            cursor += Utl_Decimal_WriteUint64(content->last, &buffer[cursor], count - cursor);
        }
        else
        {
            buffer[cursor] = UTL_HTTP_CHAR_ASTERISK;
            ++cursor;
        }
        buffer[cursor] = UTL_HTTP_CHAR_SLASH;
        ++cursor;
        if (content->hasCompleteLength)
        {
            (void)Utl_Decimal_WriteUint64(content->completeLength, &buffer[cursor], count - cursor);
        }
        else
        {
            buffer[cursor] = UTL_HTTP_CHAR_ASTERISK;
        }
    }
    else
    {
        count = 0;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  RFC 9112 section 3.2.1 retains the encoded path and query. Capacity and syntax
 *  are checked before any output write; fragments never enter the request target.
 */
bool_t Utl_Http_WriteOriginTarget(const Utl_Url_t* url, char_t* output, uint32_t capacity, uint32_t* byteCount)
{
    bool_t   valid;
    uint32_t count;
    uint32_t offset;
    uint32_t index;

    LOGASSERT(NULL != url);
    LOGASSERT(NULL != byteCount);
    LOGASSERT((NULL != output) || (0 == capacity));

    valid = Utl_Http_GetOriginTargetByteCount(url, &count);
    if (valid)
    {
        valid = (NULL != output) && (count <= capacity);
    }
    if (valid)
    {
        offset = 0;
        if (0 == url->path.byteCount)
        {
            output[offset] = UTL_URL_CHAR_SLASH;
            ++offset;
        }
        else
        {
            for (index = 0; index < url->path.byteCount; ++index)
            {
                output[offset] = url->path.data[index];
                ++offset;
            }
        }
        if (url->hasQuery)
        {
            output[offset] = UTL_URL_CHAR_QUERY;
            ++offset;
            for (index = 0; index < url->query.byteCount; ++index)
            {
                output[offset] = url->query.data[index];
                ++offset;
            }
        }
        LOGASSERT(offset == count);
        *byteCount = count;
    }

    return valid;
}
