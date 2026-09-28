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
 * The separate caller-owned byte reader interprets one optional leading signature
 * and decodes either UTF-16 byte order without alignment assumptions. No operation
 * allocates, calls CRT/OS routines or retains global mutable state.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_unicode.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UNICODE_MAX_SCALAR            (0x10FFFF)
#define UNICODE_BMP_MAX               (0xFFFF)
#define UNICODE_SUPPLEMENTARY_FIRST   (0x10000)
#define UNICODE_HIGH_SURROGATE_FIRST  (0xD800)
#define UNICODE_HIGH_SURROGATE_LAST   (0xDBFF)
#define UNICODE_LOW_SURROGATE_FIRST   (0xDC00)
#define UNICODE_LOW_SURROGATE_LAST    (0xDFFF)
#define UTF8_ONE_BYTE_MAX             (0x7F)
#define UTF8_TWO_BYTE_MAX             (0x7FF)
#define UTF8_THREE_BYTE_MAX           (0xFFFF)
#define UTF8_TWO_BYTE_FIRST           (0xC2)
#define UTF8_TWO_BYTE_LAST            (0xDF)
#define UTF8_THREE_BYTE_FIRST         (0xE0)
#define UTF8_THREE_BYTE_LAST          (0xEF)
#define UTF8_FOUR_BYTE_FIRST          (0xF0)
#define UTF8_FOUR_BYTE_LAST           (0xF4)
#define UTF8_TWO_BYTE_MASK            (0x1F)
#define UTF8_THREE_BYTE_MASK          (0x0F)
#define UTF8_FOUR_BYTE_MASK           (0x07)
#define UTF8_CONTINUATION_FIRST       (0x80)
#define UTF8_CONTINUATION_LAST        (0xBF)
#define UTF8_CONTINUATION_MASK        (0x3F)
#define UTF8_CONTINUATION_BITS        (6)
#define UTF16_PAIR_BITS               (10)
#define UTF16_PAIR_MASK               (0x3FF)
#define UNICODE_UTF8_SIGNATURE_FIRST  (0xEF)
#define UNICODE_UTF8_SIGNATURE_SECOND (0xBB)
#define UNICODE_UTF8_SIGNATURE_LAST   (0xBF)
#define UNICODE_UTF16_SIGNATURE_LOW   (0xFF)
#define UNICODE_UTF16_SIGNATURE_HIGH  (0xFE)
#define UNICODE_BYTE_BITS             (8)
#define UNICODE_UTF16_UNIT_BYTES      (2)
#define UNICODE_UTF16_MAX_UNITS       (2)
#define UNICODE_UTF8_SIGNATURE_BYTES  (3)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief One complete decoded scalar and its source extent. */
typedef struct Scalar_s
{
    uint32_t value;     ///< Decoded scalar value.
    uint32_t byteCount; ///< Source bytes consumed by that scalar.
} Scalar_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/** Leading bits for one-, two-, three- and four-byte UTF-8 encodings. */
static const byte_t c_utf8LeadBits[] = {0, 0xC0, 0xE0, 0xF0};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Reads an unaligned UTF-16 scalar using explicit byte order.
 *
 * \par Requirements:
 *  - 001 The caller shall supply an initialized UTF-16 reader and disjoint outputs.
 *  - 002 Input and outputs shall be preserved on truncated or malformed input.
 *
 * \param[in]  reader    Required initialized reader
 * \param[out] decoded   Required complete scalar result
 *
 * \retval     TRUE    A complete scalar was read.
 * \retval     FALSE   No complete valid scalar is available.
 */
static FORCE_INLINE bool_t ReadByteOrderedUtf16(const Utl_Unicode_Reader_t* reader, Scalar_t* decoded);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The temporary holds at most two native units. All byte offsets are bounded by
 *  the supplied extent before loading; the scalar codec owns surrogate validation.
 */
static FORCE_INLINE bool_t ReadByteOrderedUtf16(const Utl_Unicode_Reader_t* reader, Scalar_t* decoded)
{
    uint32_t remaining;
    uint32_t unit_count;
    bool_t   little_endian;
    uint32_t index;
    uint32_t offset;
    uint16_t first;
    uint16_t second;
    uint16_t units[UNICODE_UTF16_MAX_UNITS];
    bool_t   valid;
    uint32_t consumed;

    remaining     = Utl_Unicode_GetRemainingByteCount(reader);
    unit_count    = remaining / UNICODE_UTF16_UNIT_BYTES;
    little_endian = (UTL_UNICODE_UTF16_LE == reader->encoding);
    if (UNICODE_UTF16_MAX_UNITS < unit_count)
    {
        unit_count = UNICODE_UTF16_MAX_UNITS;
    }
    for (index = 0; index < unit_count; ++index)
    {
        offset = reader->offset + (index * UNICODE_UTF16_UNIT_BYTES);
        first  = reader->data[offset];
        second = reader->data[offset + 1];
        if (little_endian)
        {
            units[index] = (uint16_t)(first | (second << UNICODE_BYTE_BITS));
        }
        else
        {
            units[index] = (uint16_t)((first << UNICODE_BYTE_BITS) | second);
        }
    }
    valid = Utl_Unicode_ReadUtf16(&decoded->value, &consumed, units, unit_count);
    if (valid)
    {
        decoded->byteCount = consumed * UNICODE_UTF16_UNIT_BYTES;
    }

    return valid;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Signature interpretation follows Unicode 17.0, chapter 23.8. UTF-32 is not
 *  selected by this API. The byte-order choice is independent of the host target.
 *  https://www.unicode.org/versions/Unicode17.0.0/core-spec/chapter-23/
 */
void Utl_Unicode_InitializeReader(Utl_Unicode_Reader_t* reader, const byte_t* data, uint32_t byteCount)
{
    bool_t has_source;
    bool_t has_pair;
    byte_t first;
    byte_t second;
    bool_t little_endian;
    bool_t big_endian;
    bool_t utf8_prefix;
    bool_t has_utf8_signature;

    LOGASSERT(NULL != reader);

    has_source = (NULL != data) || (0 == byteCount);
    LOGASSERT(has_source);
    reader->data      = data;
    reader->byteCount = byteCount;
    reader->offset    = 0;
    reader->encoding  = UTL_UNICODE_UTF8;
    has_pair          = (UNICODE_UTF16_UNIT_BYTES <= byteCount);
    if (has_pair)
    {
        first         = data[0];
        second        = data[1];
        little_endian = (UNICODE_UTF16_SIGNATURE_LOW == first) && (UNICODE_UTF16_SIGNATURE_HIGH == second);
        big_endian    = (UNICODE_UTF16_SIGNATURE_HIGH == first) && (UNICODE_UTF16_SIGNATURE_LOW == second);
        utf8_prefix   = (UNICODE_UTF8_SIGNATURE_FIRST == first) && (UNICODE_UTF8_SIGNATURE_SECOND == second);
        if (little_endian || big_endian)
        {
            reader->encoding = little_endian ? UTL_UNICODE_UTF16_LE : UTL_UNICODE_UTF16_BE;
            reader->offset   = UNICODE_UTF16_UNIT_BYTES;
        }
        else if (utf8_prefix && (UNICODE_UTF8_SIGNATURE_BYTES <= byteCount))
        {
            has_utf8_signature = (UNICODE_UTF8_SIGNATURE_LAST == data[UNICODE_UTF8_SIGNATURE_BYTES - 1]);
            if (has_utf8_signature)
            {
                reader->offset = UNICODE_UTF8_SIGNATURE_BYTES;
            }
        }
        else
        {
            /* Unmarked payload remains UTF-8; ReadScalar diagnoses invalid bytes. */
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Cursor bounds are initialization/read invariants and programmer obligations.
 */
uint32_t Utl_Unicode_GetRemainingByteCount(const Utl_Unicode_Reader_t* reader)
{
    LOGASSERT(NULL != reader);
    LOGASSERT(reader->offset <= reader->byteCount);

    return reader->byteCount - reader->offset;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Unicode noncharacters are scalar values; their use is a higher-level policy.
 */
bool_t Utl_Unicode_IsScalar(uint32_t value)
{
    bool_t is_in_range;
    bool_t is_surrogate;

    is_in_range  = (UNICODE_MAX_SCALAR >= value);
    is_surrogate = (UNICODE_HIGH_SURROGATE_FIRST <= value) && (UNICODE_LOW_SURROGATE_LAST >= value);

    return is_in_range && (!is_surrogate);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The first zero unit terminates the scan. A full unterminated span has an explicit
 *  sentinel, so callers cannot mistake truncation for a complete command line.
 */
uint32_t Utl_Unicode_GetUtf8StringByteCount(const char_t* text, uint32_t capacity)
{
    uint32_t count;
    uint32_t index;
    bool_t   terminated;

    LOGASSERT(NULL != text);

    count = UTL_UNICODE_NO_TERMINATOR;
    for (index = 0; index < capacity; ++index)
    {
        terminated = (0 == text[index]);
        if (terminated)
        {
            count = index;
            break;
        }
    }

    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The result distinguishes an empty terminated string from missing termination.
 */
uint32_t Utl_Unicode_GetUtf16StringUnitCount(const uint16_t* units, uint32_t capacity)
{
    uint32_t count;
    uint32_t index;

    LOGASSERT(NULL != units);
    count = UTL_UNICODE_NO_TERMINATOR;
    for (index = 0; index < capacity; ++index)
    {
        if (UTL_UNICODE_TERMINATOR == units[index])
        {
            count = index;
            break;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Unicode_GetUtf8ByteCount(uint32_t value)
{
    uint32_t count;

    count = 0;
    if (Utl_Unicode_IsScalar(value))
    {
        if (UTF8_ONE_BYTE_MAX >= value)
        {
            count = 1;
        }
        else if (UTF8_TWO_BYTE_MAX >= value)
        {
            count = 2;
        }
        else if (UTF8_THREE_BYTE_MAX >= value)
        {
            count = 3;
        }
        else
        {
            count = 4;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Unicode_GetUtf16UnitCount(uint32_t value)
{
    uint32_t count;

    count = 0;
    if (Utl_Unicode_IsScalar(value))
    {
        count = 1;
        if (UNICODE_BMP_MAX < value)
        {
            count = 2;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  U+0000 and U+FEFF are ordinary counted scalars, not terminators or removed BOMs.
 */
bool_t Utl_Unicode_IsValidUtf8(const byte_t* buffer, uint32_t byteCount)
{
    uint32_t cursor;
    bool_t   is_valid;
    uint32_t scalar;
    uint32_t consumed;

    cursor   = 0;
    is_valid = (0 == byteCount) || (NULL != buffer);
    for (;;)
    {
        if (!is_valid || (cursor == byteCount))
        {
            break;
        }
        is_valid = Utl_Unicode_ReadUtf8(&scalar, &consumed, &buffer[cursor], byteCount - cursor);
        if (is_valid)
        {
            cursor += consumed;
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Byte order and BOM interpretation belong to the enclosing byte-stream format.
 */
bool_t Utl_Unicode_IsValidUtf16(const uint16_t* units, uint32_t unitCount)
{
    uint32_t cursor;
    bool_t   is_valid;
    uint32_t scalar;
    uint32_t consumed;

    cursor   = 0;
    is_valid = (0 == unitCount) || (NULL != units);
    for (;;)
    {
        if (!is_valid || (cursor == unitCount))
        {
            break;
        }
        is_valid = Utl_Unicode_ReadUtf16(&scalar, &consumed, &units[cursor], unitCount - cursor);
        if (is_valid)
        {
            cursor += consumed;
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Lead-byte classification sets the width and minimum scalar before any continuation byte is read.
 */
bool_t Utl_Unicode_ReadUtf8(uint32_t* scalar, uint32_t* bytesRead, const byte_t* buffer, uint32_t byteCount)
{
    bool_t   has_outputs;
    bool_t   has_input;
    bool_t   is_valid;
    uint32_t value;
    uint32_t width;
    uint32_t minimum;
    byte_t   lead;
    uint32_t index;
    byte_t   next;
    bool_t   is_continuation;
    bool_t   is_shortest;
    bool_t   is_scalar;

    has_outputs = (NULL != scalar) && (NULL != bytesRead);
    has_input   = (NULL != buffer) && (0 < byteCount);
    is_valid    = has_outputs && has_input;
    value       = 0;
    width       = 0;
    minimum     = 0;
    if (is_valid)
    {
        lead = buffer[0];
        if (UTF8_ONE_BYTE_MAX >= lead)
        {
            value = lead;
            width = 1;
        }
        else if ((UTF8_TWO_BYTE_FIRST <= lead) && (UTF8_TWO_BYTE_LAST >= lead))
        {
            value   = lead & UTF8_TWO_BYTE_MASK;
            width   = 2;
            minimum = UTF8_ONE_BYTE_MAX + 1;
        }
        else if ((UTF8_THREE_BYTE_FIRST <= lead) && (UTF8_THREE_BYTE_LAST >= lead))
        {
            value   = lead & UTF8_THREE_BYTE_MASK;
            width   = 3;
            minimum = UTF8_TWO_BYTE_MAX + 1;
        }
        else if ((UTF8_FOUR_BYTE_FIRST <= lead) && (UTF8_FOUR_BYTE_LAST >= lead))
        {
            value   = lead & UTF8_FOUR_BYTE_MASK;
            width   = 4;
            minimum = UTF8_THREE_BYTE_MAX + 1;
        }
        else
        {
            is_valid = FALSE;
        }
        is_valid = is_valid && (width <= byteCount);
    }
    if (is_valid)
    {
        for (index = 1; index < width; ++index)
        {
            next            = buffer[index];
            is_continuation = (UTF8_CONTINUATION_FIRST <= next) && (UTF8_CONTINUATION_LAST >= next);
            if (!is_continuation)
            {
                is_valid = FALSE;
                break;
            }
            value = (value << UTF8_CONTINUATION_BITS) | (next & UTF8_CONTINUATION_MASK);
        }
        is_shortest = (minimum <= value);
        is_scalar   = Utl_Unicode_IsScalar(value);
        is_valid    = is_valid && is_shortest && is_scalar;
    }
    if (is_valid)
    {
        (*scalar)    = value;
        (*bytesRead) = width;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A surrogate pair contributes ten bits from each unit above the supplementary-plane base.
 */
bool_t Utl_Unicode_ReadUtf16(uint32_t* scalar, uint32_t* unitsRead, const uint16_t* units, uint32_t unitCount)
{
    bool_t   has_outputs;
    bool_t   has_input;
    bool_t   is_valid;
    uint32_t value;
    uint32_t width;
    uint32_t lead;
    bool_t   is_high;
    bool_t   is_low;
    uint32_t next;

    has_outputs = (NULL != scalar) && (NULL != unitsRead);
    has_input   = (NULL != units) && (0 < unitCount);
    is_valid    = has_outputs && has_input;
    value       = 0;
    width       = 1;
    if (is_valid)
    {
        lead    = units[0];
        is_high = (UNICODE_HIGH_SURROGATE_FIRST <= lead) && (UNICODE_HIGH_SURROGATE_LAST >= lead);
        is_low  = (UNICODE_LOW_SURROGATE_FIRST <= lead) && (UNICODE_LOW_SURROGATE_LAST >= lead);
        value   = lead;
        if (is_high)
        {
            is_valid = (2 <= unitCount);
            if (is_valid)
            {
                next     = units[1];
                is_valid = (UNICODE_LOW_SURROGATE_FIRST <= next) && (UNICODE_LOW_SURROGATE_LAST >= next);
                if (is_valid)
                {
                    value  = UNICODE_SUPPLEMENTARY_FIRST + ((lead - UNICODE_HIGH_SURROGATE_FIRST) << UTF16_PAIR_BITS);
                    value += next - UNICODE_LOW_SURROGATE_FIRST;
                    width  = 2;
                }
            }
        }
        else if (is_low)
        {
            is_valid = FALSE;
        }
    }
    if (is_valid)
    {
        (*scalar)    = value;
        (*unitsRead) = width;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Output and offset are published together only after a complete scalar read.
 */
Utl_Unicode_ReadResult_t Utl_Unicode_ReadScalar(Utl_Unicode_Reader_t* reader, uint32_t* scalar)
{
    uint32_t                 remaining;
    Utl_Unicode_ReadResult_t result;
    bool_t                   is_utf8;
    bool_t                   valid;
    Scalar_t                 decoded;
    bool_t                   is_utf16;

    LOGASSERT(NULL != reader);
    LOGASSERT(NULL != scalar);

    remaining = Utl_Unicode_GetRemainingByteCount(reader);
    result    = UTL_UNICODE_READ_END;
    if (0 < remaining)
    {
        is_utf8 = (UTL_UNICODE_UTF8 == reader->encoding);
        if (is_utf8)
        {
            valid = Utl_Unicode_ReadUtf8(&decoded.value, &decoded.byteCount, reader->data + reader->offset, remaining);
        }
        else
        {
            is_utf16 = (UTL_UNICODE_UTF16_LE == reader->encoding) || (UTL_UNICODE_UTF16_BE == reader->encoding);
            LOGASSERT(is_utf16);
            valid = ReadByteOrderedUtf16(reader, &decoded);
        }
        result = UTL_UNICODE_READ_INVALID;
        if (valid)
        {
            reader->offset += decoded.byteCount;
            (*scalar)       = decoded.value;
            result          = UTL_UNICODE_READ_SCALAR;
        }
    }

    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Unicode_WriteUtf8(uint32_t scalar, byte_t* buffer, uint32_t capacity)
{
    uint32_t width;
    bool_t   has_buffer;
    bool_t   has_scalar;
    bool_t   has_capacity;
    bool_t   can_write;
    uint32_t value;
    uint32_t index;

    width        = Utl_Unicode_GetUtf8ByteCount(scalar);
    has_buffer   = (NULL != buffer);
    has_scalar   = (0 < width);
    has_capacity = (width <= capacity);
    can_write    = has_buffer && has_scalar && has_capacity;
    if (can_write)
    {
        value = scalar;
        for (index = width - 1; 0 < index; --index)
        {
            buffer[index] = (byte_t)(UTF8_CONTINUATION_FIRST | (value & UTF8_CONTINUATION_MASK));
            value         >>= UTF8_CONTINUATION_BITS;
        }
        buffer[0] = (byte_t)(c_utf8LeadBits[width - 1] | value);
    }
    else
    {
        width = 0;
    }
    return width;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Supplementary scalars are reduced by U+10000 before splitting the surrogate payload.
 */
uint32_t Utl_Unicode_WriteUtf16(uint32_t scalar, uint16_t* units, uint32_t capacity)
{
    uint32_t width;
    bool_t   has_buffer;
    bool_t   has_scalar;
    bool_t   has_capacity;
    bool_t   can_write;
    uint32_t value;

    width        = Utl_Unicode_GetUtf16UnitCount(scalar);
    has_buffer   = (NULL != units);
    has_scalar   = (0 < width);
    has_capacity = (width <= capacity);
    can_write    = has_buffer && has_scalar && has_capacity;
    if (can_write)
    {
        if (1 == width)
        {
            units[0] = (uint16_t)scalar;
        }
        else
        {
            value    = scalar - UNICODE_SUPPLEMENTARY_FIRST;
            units[0] = (uint16_t)(UNICODE_HIGH_SURROGATE_FIRST + (value >> UTF16_PAIR_BITS));
            units[1] = (uint16_t)(UNICODE_LOW_SURROGATE_FIRST + (value & UTF16_PAIR_MASK));
        }
    }
    else
    {
        width = 0;
    }
    return width;
}
