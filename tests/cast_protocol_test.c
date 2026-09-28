/*=================================================================================================
** 1. References
**===============================================================================================*/

/* RFC 3629, UTF-16 scalar mapping, protobuf wire rules and CastV2 1.0 schema vectors. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_unicode.h"
#include "utl/utl_protobuf.h"
#include "utl/utl_castv2.h"
#include "utl/utl_decimal.h"
#include "utl/utl_http.h"

#include "os/os_assert.h"
#include "os/os_process.h"
#include "os/os_compiler.h"
#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_SENTINEL (0xA5)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Independent scalar/UTF-8/UTF-16 encoding vector. */
typedef struct UnicodeVector_s
{
    uint32_t scalar;
    byte_t   utf8[4];
    uint32_t byteCount;
    uint16_t utf16[2];
    uint32_t unitCount;
} UnicodeVector_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static const UnicodeVector_t c_unicode[]    = {{0, {0, 0, 0, 0}, 1, {0, 0}, 1},
                                               {0x7F, {0x7F, 0, 0, 0}, 1, {0x7F, 0}, 1},
                                               {0x80, {0xC2, 0x80, 0, 0}, 2, {0x80, 0}, 1},
                                               {0x7FF, {0xDF, 0xBF, 0, 0}, 2, {0x7FF, 0}, 1},
                                               {0x800, {0xE0, 0xA0, 0x80, 0}, 3, {0x800, 0}, 1},
                                               {0xD7FF, {0xED, 0x9F, 0xBF, 0}, 3, {0xD7FF, 0}, 1},
                                               {0xE000, {0xEE, 0x80, 0x80, 0}, 3, {0xE000, 0}, 1},
                                               {0xFEFF, {0xEF, 0xBB, 0xBF, 0}, 3, {0xFEFF, 0}, 1},
                                               {0xFFFF, {0xEF, 0xBF, 0xBF, 0}, 3, {0xFFFF, 0}, 1},
                                               {0x10000, {0xF0, 0x90, 0x80, 0x80}, 4, {0xD800, 0xDC00}, 2},
                                               {0x1F600, {0xF0, 0x9F, 0x98, 0x80}, 4, {0xD83D, 0xDE00}, 2},
                                               {0x10FFFF, {0xF4, 0x8F, 0xBF, 0xBF}, 4, {0xDBFF, 0xDFFF}, 2}};
static const byte_t          c_badUtf8[][4] = {{0x80, 0, 0, 0},
                                               {0xBF, 0, 0, 0},
                                               {0xC0, 0x80, 0, 0},
                                               {0xC1, 0xBF, 0, 0},
                                               {0xC2, 0x7F, 0, 0},
                                               {0xDF, 0xC0, 0, 0},
                                               {0xE0, 0x9F, 0xBF, 0},
                                               {0xED, 0xA0, 0x80, 0},
                                               {0xED, 0xBF, 0xBF, 0},
                                               {0xF0, 0x8F, 0xBF, 0xBF},
                                               {0xF4, 0x90, 0x80, 0x80},
                                               {0xF5, 0x80, 0x80, 0x80},
                                               {0xFF, 0, 0, 0}};
static const byte_t          c_frame[] =
    {0, 0, 0, 17, 0x08, 0, 0x12, 1, 's', 0x1A, 1, 'd', 0x22, 1, 'n', 0x28, 0, 0x32, 2, '{', '}'};
static const byte_t c_requiredOffsets[] = {0, 2, 5, 8, 11};
static const byte_t c_requiredWidths[]  = {2, 3, 3, 3, 2};
static const byte_t c_unknownFields[] =
    {0x40, 1, 0x49, 1, 2, 3, 4, 5, 6, 7, 8, 0x52, 2, 0xFF, 0xFE, 0x5D, 1, 2, 3, 4, 0x63, 0x68, 1, 0x64};
static byte_t         s_payload[UTL_CASTV2_MAX_BODY];
static byte_t         s_frame[UTL_CASTV2_MAX_FRAME];

static const char_t   c_decimalMax[]       = "18446744073709551615";
static const char_t   c_decimalOverflow[]  = "18446744073709551616";
static const char_t   c_decimalPrefix[]    = "00000000000000000000000000000042-rest";
static const char_t   c_rangeInput[]       = " \tBYTES=, 000-009, 20-, -005,\t";
static const char_t   c_rangeCanonical[]   = "bytes=0-9,20-,-5";
static const char_t   c_contentInput[]     = "\tBYTES 000-009/010 \t";
static const char_t   c_contentCanonical[] = "bytes 0-9/10";
static const char_t   c_unknownLength[]    = "bytes 1-9/*";
static const char_t   c_unsatisfied[]      = "bytes */0";
static const char_t   c_badRanges[][64]    = {"",
                                              "bytes=",
                                              "bytes=,,,",
                                              "bytes =0-1",
                                              "bytes= 0 -1",
                                              "bytes=0- 1",
                                              "items=0-1",
                                              "bytes=5-2",
                                              "bytes=-",
                                              "bytes=0-9suffix",
                                              "bytes=0-9,5-2",
                                              "bytes=0-9\r\n",
                                              "bytes=0-9\0,10-20",
                                              "bytes=18446744073709551616-",
                                              "bytes=-18446744073709551616",
                                              "bytes=0-18446744073709551616"};
static const uint32_t c_badRangeBytes[]    = {0, 6, 9, 10, 11, 10, 9, 9, 7, 15, 13, 11, 16, 27, 27, 28};
static const char_t   c_badContents[][64]  = {"bytes */*",
                                              "bytes 0-10/10",
                                              "bytes 5-2/10",
                                              "bytes 0-0/0",
                                              "bytes 0-1/2junk",
                                              "bytes\t0-1/2",
                                              "bytes  0-1/2",
                                              "bytes 0 -1/2",
                                              "bytes 0-1 /2",
                                              "bytes 0-1/18446744073709551616",
                                              "bytes 18446744073709551616-1/*"};
static const uint32_t c_badContentBytes[]  = {9, 13, 12, 11, 15, 11, 12, 12, 12, 30, 30};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Terminates with a source-line code when a check fails.
 *  \param[in] passed   Check outcome
 *  \param[in] line     Source line reported as process status
 *  \retval None
 */
static FORCE_INLINE void Check(bool_t passed, uint32_t line);

/*************************************************************************************************/
/** \brief Compares two counted byte spans.
 *  \param[in] left    First readable span
 *  \param[in] right   Second readable span
 *  \param[in] count   Span byte count
 *  \retval TRUE    Identical counted bytes.
 *  \retval FALSE   At least one byte differs.
 */
static bool_t Equal(const byte_t* left, const byte_t* right, uint32_t count);

/*************************************************************************************************/
/** \brief Exercises independent Unicode vectors and malformed input.
 *  \retval None
 */
static void CheckUnicodeVectors(void);

/*************************************************************************************************/
/** \brief Checks all scalar values and all isolated UTF-16 units.
 *  \retval None
 */
static void CheckUnicodeDomain(void);

/*************************************************************************************************/
/** \brief Checks independent Cast bytes, capacity and incremental framing.
 *  \retval None
 */
static void CheckCastFrames(void);

/*************************************************************************************************/
/** \brief Checks schema presence, duplicates, unknown fields and invalid encodings.
 *  \retval None
 */
static void CheckCastSchema(void);

/*************************************************************************************************/
/** \brief Checks decimal limits, prefix consumption and bounded output.
 *  \retval None
 */
static void CheckDecimal(void);

/*************************************************************************************************/
/** \brief Checks byte-range grammar, resolution, canonical output and failure preservation.
 *  \retval None
 */
static void CheckHttpRanges(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Checks remain active in optimized builds and need no CRT assertion facility.
 */
static FORCE_INLINE void Check(bool_t passed, uint32_t line)
{

    if (!passed)
    {
        OS_AssertFailure("fixture check", __FILE__, line);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static bool_t Equal(const byte_t* left, const byte_t* right, uint32_t count)
{
    bool_t   is_equal;
    uint32_t index;

    is_equal = TRUE;
    for (index = 0; index < count; ++index)
    {
        if (left[index] != right[index])
        {
            is_equal = FALSE;
            break;
        }
    }
    return is_equal;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every truncated vector must preserve both scalar outputs. Short-capacity writes
 *  must preserve all destination bytes or units, not merely the counted prefix.
 */
static void CheckUnicodeVectors(void)
{
    uint32_t               index;
    const UnicodeVector_t* vector;
    uint32_t               count;
    uint32_t               scalar;
    byte_t                 encoded[4];
    uint32_t               consumed;
    uint32_t               prefix;
    uint16_t               units[2];

    Check(Utl_Unicode_IsValidUtf8(NULL, 0), __LINE__);
    Check(Utl_Unicode_IsValidUtf16(NULL, 0), __LINE__);
    Check(!Utl_Unicode_IsValidUtf8(NULL, 1), __LINE__);
    Check(!Utl_Unicode_IsValidUtf16(NULL, 1), __LINE__);
    for (index = 0; index < (sizeof(c_unicode) / sizeof(c_unicode[0])); ++index)
    {
        vector = &c_unicode[index];
        count  = Utl_Unicode_WriteUtf8(vector->scalar, encoded, sizeof(encoded));
        Check(count == vector->byteCount, __LINE__);
        Check(Equal(encoded, vector->utf8, count), __LINE__);
        Check(Utl_Unicode_ReadUtf8(&scalar, &consumed, vector->utf8, vector->byteCount), __LINE__);
        Check((scalar == vector->scalar) && (consumed == vector->byteCount), __LINE__);
        Check(Utl_Unicode_IsValidUtf8(vector->utf8, vector->byteCount), __LINE__);
        for (prefix = 0; prefix < vector->byteCount; ++prefix)
        {
            scalar   = UINT32_MAX;
            consumed = UINT32_MAX;
            Check(!Utl_Unicode_ReadUtf8(&scalar, &consumed, vector->utf8, prefix), __LINE__);
            Check((UINT32_MAX == scalar) && (UINT32_MAX == consumed), __LINE__);
            encoded[0] = TEST_SENTINEL;
            encoded[1] = TEST_SENTINEL;
            encoded[2] = TEST_SENTINEL;
            encoded[3] = TEST_SENTINEL;
            Check(0 == Utl_Unicode_WriteUtf8(vector->scalar, encoded, prefix), __LINE__);
            Check((TEST_SENTINEL == encoded[0]) && (TEST_SENTINEL == encoded[1]), __LINE__);
            Check((TEST_SENTINEL == encoded[2]) && (TEST_SENTINEL == encoded[3]), __LINE__);
        }
        count = Utl_Unicode_WriteUtf16(vector->scalar, units, 2);
        Check(count == vector->unitCount, __LINE__);
        Check(units[0] == vector->utf16[0], __LINE__);
        if (2 == count)
        {
            Check(units[1] == vector->utf16[1], __LINE__);
        }
        Check(Utl_Unicode_ReadUtf16(&scalar, &consumed, vector->utf16, vector->unitCount), __LINE__);
        Check((scalar == vector->scalar) && (consumed == vector->unitCount), __LINE__);
        for (prefix = 0; prefix < vector->unitCount; ++prefix)
        {
            scalar   = UINT32_MAX;
            consumed = UINT32_MAX;
            Check(!Utl_Unicode_ReadUtf16(&scalar, &consumed, vector->utf16, prefix), __LINE__);
            Check((UINT32_MAX == scalar) && (UINT32_MAX == consumed), __LINE__);
            units[0] = TEST_SENTINEL;
            units[1] = TEST_SENTINEL;
            Check(0 == Utl_Unicode_WriteUtf16(vector->scalar, units, prefix), __LINE__);
            Check((TEST_SENTINEL == units[0]) && (TEST_SENTINEL == units[1]), __LINE__);
        }
    }
    for (index = 0; index < (sizeof(c_badUtf8) / sizeof(c_badUtf8[0])); ++index)
    {
        scalar   = UINT32_MAX;
        consumed = UINT32_MAX;
        Check(!Utl_Unicode_ReadUtf8(&scalar, &consumed, c_badUtf8[index], 4), __LINE__);
        Check((UINT32_MAX == scalar) && (UINT32_MAX == consumed), __LINE__);
        Check(!Utl_Unicode_IsValidUtf8(c_badUtf8[index], 4), __LINE__);
    }
    units[0] = 0xD800;
    units[1] = 0xDBFF;
    Check(!Utl_Unicode_IsValidUtf16(units, 2), __LINE__);
    units[1] = 0xE000;
    Check(!Utl_Unicode_IsValidUtf16(units, 2), __LINE__);
    units[1] = 0x41;
    Check(!Utl_Unicode_IsValidUtf16(units, 2), __LINE__);
    Check(!Utl_Unicode_IsScalar(0x110000), __LINE__);
    Check(!Utl_Unicode_IsScalar(UINT32_MAX), __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Exhaustive scalar checks complement independent vectors; the expected scalar
 *  domain is calculated directly from the Unicode code-point intervals.
 */
static void CheckUnicodeDomain(void)
{
    uint32_t scalar;
    bool_t   is_scalar;
    uint32_t count;
    byte_t   encoded[4];
    uint32_t decoded;
    uint32_t consumed;
    uint16_t units[2];

    for (scalar = 0; scalar <= 0x10FFFF; ++scalar)
    {
        is_scalar = (scalar < 0xD800) || (scalar > 0xDFFF);
        Check(is_scalar == Utl_Unicode_IsScalar(scalar), __LINE__);
        count = Utl_Unicode_WriteUtf8(scalar, encoded, sizeof(encoded));
        if (is_scalar)
        {
            Check(Utl_Unicode_GetUtf8ByteCount(scalar) == count, __LINE__);
            Check(Utl_Unicode_ReadUtf8(&decoded, &consumed, encoded, count), __LINE__);
            Check((decoded == scalar) && (consumed == count), __LINE__);
            count = Utl_Unicode_WriteUtf16(scalar, units, 2);
            Check(Utl_Unicode_GetUtf16UnitCount(scalar) == count, __LINE__);
            Check(Utl_Unicode_ReadUtf16(&decoded, &consumed, units, count), __LINE__);
            Check((decoded == scalar) && (consumed == count), __LINE__);
        }
        else
        {
            Check(0 == count, __LINE__);
            Check(0 == Utl_Unicode_WriteUtf16(scalar, units, 2), __LINE__);
        }
        if (0xFFFF >= scalar)
        {
            units[0] = (uint16_t)scalar;
            Check(is_scalar == Utl_Unicode_IsValidUtf16(units, 1), __LINE__);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The fixed wire vector is independent of the writer. A maximum-sized body,
 *  coalesced frames and every short prefix exercise the framing boundary.
 */
static void CheckCastFrames(void)
{
    Utl_CastV2_Message_t    message;
    uint32_t                count;
    byte_t                  buffer[64];
    byte_t                  payload[4];
    uint32_t                prefix;
    Utl_CastV2_FrameState_t state;
    uint32_t                index;
    Utl_CastV2_Message_t    decoded;

    Check(Utl_CastV2_ReadMessage(&message, &c_frame[4], sizeof(c_frame) - 4), __LINE__);
    Check(17 == Utl_CastV2_GetMessageByteCount(&message), __LINE__);
    Check(sizeof(c_frame) == Utl_CastV2_GetFrameByteCount(&message), __LINE__);
    count = Utl_CastV2_WriteFrame(&message, buffer, sizeof(buffer));
    Check(sizeof(c_frame) == count, __LINE__);
    Check(Equal(buffer, c_frame, count), __LINE__);
    payload[0]                = c_frame[sizeof(c_frame) - 2];
    payload[1]                = c_frame[sizeof(c_frame) - 1];
    payload[2]                = 0xFF;
    payload[3]                = 0;
    message.payload.data      = payload;
    message.payload.byteCount = 2;
    count                     = Utl_CastV2_WriteFrame(&message, buffer, sizeof(buffer));
    Check(sizeof(c_frame) == count, __LINE__);
    Check(Equal(buffer, c_frame, count), __LINE__);
    Check(Utl_CastV2_ReadMessage(&decoded, &buffer[4], count - 4), __LINE__);
    Check(1 == decoded.nameSpace.byteCount, __LINE__);
    Check(Equal(decoded.nameSpace.data, (const byte_t*)"n", 1), __LINE__);
    for (prefix = 0; prefix < sizeof(c_frame); ++prefix)
    {
        count = UINT32_MAX;
        state = Utl_CastV2_InspectFrame(&count, c_frame, prefix);
        Check(UTL_CASTV2_FRAME_INCOMPLETE == state, __LINE__);
        Check(UINT32_MAX == count, __LINE__);
        for (index = 0; index < sizeof(buffer); ++index)
        {
            buffer[index] = TEST_SENTINEL;
        }
        Check(0 == Utl_CastV2_WriteFrame(&message, buffer, prefix), __LINE__);
        for (index = 0; index < sizeof(buffer); ++index)
        {
            Check(TEST_SENTINEL == buffer[index], __LINE__);
        }
    }
    for (index = 0; index < sizeof(c_frame); ++index)
    {
        buffer[index]                   = c_frame[index];
        buffer[sizeof(c_frame) + index] = c_frame[index];
    }
    state = Utl_CastV2_InspectFrame(&count, buffer, 2 * sizeof(c_frame));
    Check((UTL_CASTV2_FRAME_COMPLETE == state) && (sizeof(c_frame) == count), __LINE__);
    buffer[3] = 0;
    Check(UTL_CASTV2_FRAME_INVALID == Utl_CastV2_InspectFrame(&count, buffer, 4), __LINE__);
    buffer[1] = 1;
    buffer[3] = 1;
    Check(UTL_CASTV2_FRAME_INVALID == Utl_CastV2_InspectFrame(&count, buffer, 4), __LINE__);
    Check(UTL_CASTV2_FRAME_INVALID == Utl_CastV2_InspectFrame(&count, NULL, 1), __LINE__);
    for (index = 0; index < sizeof(s_payload); ++index)
    {
        s_payload[index] = (byte_t)('a' + (index % 26));
    }
    message.payload.data      = s_payload;
    message.payload.byteCount = UTL_CASTV2_MAX_BODY - 17;
    Check(UTL_CASTV2_MAX_BODY == Utl_CastV2_GetMessageByteCount(&message), __LINE__);
    count = Utl_CastV2_WriteFrame(&message, s_frame, sizeof(s_frame));
    Check(UTL_CASTV2_MAX_FRAME == count, __LINE__);
    Check(Utl_CastV2_ReadMessage(&decoded, &s_frame[4], count - 4), __LINE__);
    Check(decoded.payload.byteCount == message.payload.byteCount, __LINE__);
    ++message.payload.byteCount;
    Check(0 == Utl_CastV2_GetMessageByteCount(&message), __LINE__);
    message.payload.byteCount = UINT32_MAX;
    Check(0 == Utl_CastV2_GetMessageByteCount(&message), __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Mutation vectors remove each required field independently, reorder records,
 *  append unknown wire forms and check last-value semantics and failure atomicity.
 */
static void CheckCastSchema(void)
{
    Utl_CastV2_Message_t message;
    uint32_t             index;
    uint32_t             required;
    uint32_t             start;
    uint32_t             end;
    uint32_t             count;
    byte_t               body[128];
    uint32_t             cursor;
    Utl_Protobuf_Field_t field;

    Check(Utl_CastV2_ReadMessage(&message, &c_frame[4], 17), __LINE__);
    for (index = 0; index < 17; ++index)
    {
        Check(!Utl_CastV2_ReadMessage(&message, &c_frame[4], index), __LINE__);
        Check(message.source.data == &c_frame[8], __LINE__);
        Check(2 == message.payload.byteCount, __LINE__);
    }
    for (required = 0; required < sizeof(c_requiredOffsets); ++required)
    {
        start = c_requiredOffsets[required];
        end   = start + c_requiredWidths[required];
        count = 0;
        for (index = 0; index < 17; ++index)
        {
            if ((index < start) || (end <= index))
            {
                body[count] = c_frame[4 + index];
                ++count;
            }
        }
        Check(!Utl_CastV2_ReadMessage(&message, body, count), __LINE__);
    }
    cursor = 0;
    count  = 17;
    for (;;)
    {
        if (17 == cursor)
        {
            break;
        }
        Check(Utl_Protobuf_ReadField(&field, &c_frame[4 + cursor], 17 - cursor), __LINE__);
        count -= field.encodedByteCount;
        for (index = 0; index < field.encodedByteCount; ++index)
        {
            body[count + index] = c_frame[4 + cursor + index];
        }
        cursor += field.encodedByteCount;
    }
    Check(Utl_CastV2_ReadMessage(&message, body, 17), __LINE__);
    for (index = 0; index < 17; ++index)
    {
        body[index] = c_frame[4 + index];
    }
    for (index = 0; index < sizeof(c_unknownFields); ++index)
    {
        body[17 + index] = c_unknownFields[index];
    }
    Check(Utl_CastV2_ReadMessage(&message, body, 17 + sizeof(c_unknownFields)), __LINE__);
    body[17] = 0x12;
    body[18] = 1;
    body[19] = 'x';
    Check(Utl_CastV2_ReadMessage(&message, body, 20), __LINE__);
    Check('x' == message.source.data[0], __LINE__);
    body[4] = 0xFF;
    Check(!Utl_CastV2_ReadMessage(&message, body, 20), __LINE__);
    body[4]  = 's';
    body[17] = 0x28;
    body[18] = 7;
    Check(Utl_CastV2_ReadMessage(&message, body, 19), __LINE__);
    Check(UTL_CASTV2_STRING == message.payloadType, __LINE__);
    body[12] = 7;
    Check(!Utl_CastV2_ReadMessage(&message, body, 17), __LINE__);
    body[12] = 1;
    Check(!Utl_CastV2_ReadMessage(&message, body, 17), __LINE__);
    body[13] = 0x3A;
    body[15] = 0xFF;
    body[16] = 0;
    Check(Utl_CastV2_ReadMessage(&message, body, 17), __LINE__);
    Check(UTL_CASTV2_BINARY == message.payloadType, __LINE__);
    count = Utl_CastV2_WriteFrame(&message, s_frame, sizeof(s_frame));
    Check((21 == count) && (0xFF == s_frame[19]), __LINE__);
    body[17] = 0x32;
    body[18] = 0;
    Check(!Utl_CastV2_ReadMessage(&message, body, 19), __LINE__);
    body[1] = 1;
    Check(!Utl_CastV2_ReadMessage(&message, body, 17), __LINE__);
    body[1]  = 0;
    body[17] = 0x64;
    Check(!Utl_CastV2_ReadMessage(&message, body, 18), __LINE__);
    body[17] = 0x63;
    Check(!Utl_CastV2_ReadMessage(&message, body, 18), __LINE__);
    body[17] = 0x63;
    body[18] = 0x6C;
    Check(!Utl_CastV2_ReadMessage(&message, body, 19), __LINE__);
    body[17] = 0;
    Check(!Utl_CastV2_ReadMessage(&message, body, 18), __LINE__);
    Check((UTL_CASTV2_BINARY == message.payloadType) && (2 == message.payload.byteCount), __LINE__);
    Check(!Utl_CastV2_ReadMessage(&message, body, 18), __LINE__);
    Check((UTL_CASTV2_BINARY == message.payloadType) && (2 == message.payload.byteCount), __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Independent maximum and overflow vectors also verify unchanged outputs on failure.
 */
static void CheckDecimal(void)
{
    uint64_t value;
    uint32_t consumed;
    uint32_t index;
    byte_t   output[24];
    uint32_t capacity;

    value    = 7;
    consumed = 7;
    Check(!Utl_Decimal_ReadUint64(&value, &consumed, (const byte_t*)c_decimalOverflow, sizeof(c_decimalOverflow) - 1),
          __LINE__);
    Check((7 == value) && (7 == consumed), __LINE__);
    Check(!Utl_Decimal_ReadUint64(&value, &consumed, (const byte_t*)"-1", 2), __LINE__);
    Check(!Utl_Decimal_ReadUint64(&value, &consumed, (const byte_t*)"+1", 2), __LINE__);
    Check(!Utl_Decimal_ReadUint64(&value, &consumed, (const byte_t*)" 1", 2), __LINE__);
    Check(!Utl_Decimal_ReadUint64(&value, &consumed, NULL, 1), __LINE__);
    Check(!Utl_Decimal_ReadUint64(&value, &consumed, (const byte_t*)"1", 0), __LINE__);
    Check(!Utl_Decimal_ReadUint64(NULL, &consumed, (const byte_t*)"1", 1), __LINE__);
    Check(!Utl_Decimal_ReadUint64(&value, NULL, (const byte_t*)"1", 1), __LINE__);
    Check((7 == value) && (7 == consumed), __LINE__);
    Check(Utl_Decimal_ReadUint64(&value, &consumed, (const byte_t*)c_decimalMax, sizeof(c_decimalMax) - 1), __LINE__);
    Check((UINT64_MAX == value) && (20 == consumed), __LINE__);
    Check(20 == Utl_Decimal_GetUint64ByteCount(value), __LINE__);
    for (index = 0; index < sizeof(output); ++index)
    {
        output[index] = (char_t)('a' + index);
    }
    for (capacity = 0; capacity < 20; ++capacity)
    {
        Check(0 == Utl_Decimal_WriteUint64(value, output, capacity), __LINE__);
        for (index = 0; index < sizeof(output); ++index)
        {
            Check((char_t)('a' + index) == output[index], __LINE__);
        }
    }
    Check(20 == Utl_Decimal_WriteUint64(value, output, 20), __LINE__);
    Check(Equal((const byte_t*)c_decimalMax, (const byte_t*)output, 20), __LINE__);
    Check('u' == output[20], __LINE__);
    Check(Utl_Decimal_ReadUint64(&value, &consumed, (const byte_t*)c_decimalPrefix, sizeof(c_decimalPrefix) - 1),
          __LINE__);
    Check((42 == value) && (32 == consumed), __LINE__);
    Check(Utl_Decimal_ReadUint64(&value, &consumed, (const byte_t*)"0rest", 5), __LINE__);
    Check((0 == value) && (1 == consumed), __LINE__);
    Check(1 == Utl_Decimal_GetUint64ByteCount(0), __LINE__);
    Check(1 == Utl_Decimal_WriteUint64(0, output, 1), __LINE__);
    Check('0' == output[0], __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Vectors cover all three forms, complete-list validation, overflow, caller capacity,
 *  empty content, unknown lengths and preflight preservation of short output buffers.
 */
static void CheckHttpRanges(void)
{
    Utl_Http_RangeSet_t      request;
    Utl_Http_Range_t         slots[3];
    uint32_t                 capacity;
    Utl_Http_RangeInterval_t interval;
    uint32_t                 index;
    byte_t                   output[64];
    byte_t                   empty_members[48];
    Utl_Http_ContentRange_t  content;

    request.ranges   = slots;
    request.capacity = 3;
    request.count    = 0;
    Check(Utl_Http_ReadRange(&request, (const byte_t*)c_rangeInput, sizeof(c_rangeInput) - 1), __LINE__);
    Check(3 == request.count, __LINE__);
    Check((UTL_HTTP_RANGE_CLOSED == slots[0].kind) && (0 == slots[0].first) && (9 == slots[0].last), __LINE__);
    Check((UTL_HTTP_RANGE_OPEN == slots[1].kind) && (20 == slots[1].first), __LINE__);
    Check((UTL_HTTP_RANGE_SUFFIX == slots[2].kind) && (5 == slots[2].suffixLength), __LINE__);
    Check(Utl_Http_ResolveRange(&interval, &slots[0], 5), __LINE__);
    Check((0 == interval.offset) && (5 == interval.byteCount), __LINE__);
    Check(!Utl_Http_ResolveRange(&interval, &slots[1], 20), __LINE__);
    Check((0 == interval.offset) && (5 == interval.byteCount), __LINE__);
    Check(Utl_Http_ResolveRange(&interval, &slots[1], 21), __LINE__);
    Check((20 == interval.offset) && (1 == interval.byteCount), __LINE__);
    Check(Utl_Http_ResolveRange(&interval, &slots[2], 3), __LINE__);
    Check((0 == interval.offset) && (3 == interval.byteCount), __LINE__);
    Check(Utl_Http_ResolveRange(&interval, &slots[2], 7), __LINE__);
    Check((2 == interval.offset) && (5 == interval.byteCount), __LINE__);
    Check(Utl_Http_ResolveRange(&interval, &slots[2], 0), __LINE__);
    Check((0 == interval.offset) && (0 == interval.byteCount), __LINE__);
    for (index = 0; index < sizeof(output); ++index)
    {
        output[index] = (char_t)('A' + (index % 26));
    }
    Check((sizeof(c_rangeCanonical) - 1) == Utl_Http_GetRangeByteCount(&request), __LINE__);
    for (capacity = 0; capacity < (sizeof(c_rangeCanonical) - 1); ++capacity)
    {
        Check(0 == Utl_Http_WriteRange(&request, output, capacity), __LINE__);
        for (index = 0; index < sizeof(output); ++index)
        {
            Check((char_t)('A' + (index % 26)) == output[index], __LINE__);
        }
    }
    Check((sizeof(c_rangeCanonical) - 1) == Utl_Http_WriteRange(&request, output, sizeof(c_rangeCanonical) - 1),
          __LINE__);
    Check(Equal((const byte_t*)c_rangeCanonical, (const byte_t*)output, sizeof(c_rangeCanonical) - 1), __LINE__);
    request.capacity = 2;
    Check(!Utl_Http_ReadRange(&request, (const byte_t*)c_rangeInput, sizeof(c_rangeInput) - 1), __LINE__);
    Check(0 == request.count, __LINE__);
    request.capacity = 3;
    for (index = 0; index < (sizeof(c_badRanges) / sizeof(c_badRanges[0])); ++index)
    {
        request.count = 3;
        Check(!Utl_Http_ReadRange(&request, (const byte_t*)c_badRanges[index], c_badRangeBytes[index]), __LINE__);
        Check(0 == request.count, __LINE__);
    }
    Check(Utl_Http_ReadRange(&request, (const byte_t*)"bytes=-0", 8), __LINE__);
    Check(!Utl_Http_ResolveRange(&interval, &slots[0], UINT64_MAX), __LINE__);
    Check(!Utl_Http_ResolveRange(&interval, &slots[0], 0), __LINE__);
    slots[0].suffixLength = UINT64_MAX;
    Check(Utl_Http_ResolveRange(&interval, &slots[0], UINT64_MAX), __LINE__);
    Check((0 == interval.offset) && (UINT64_MAX == interval.byteCount), __LINE__);
    slots[0].kind  = UTL_HTTP_RANGE_CLOSED;
    slots[0].first = 0;
    slots[0].last  = UINT64_MAX;
    Check(Utl_Http_ResolveRange(&interval, &slots[0], UINT64_MAX), __LINE__);
    Check((0 == interval.offset) && (UINT64_MAX == interval.byteCount), __LINE__);
    slots[0].first = UINT64_MAX;
    Check(!Utl_Http_ResolveRange(&interval, &slots[0], UINT64_MAX), __LINE__);
    for (index = 0; index < 6; ++index)
    {
        empty_members[index] = c_rangeCanonical[index];
    }
    for (index = 6; index < 38; ++index)
    {
        empty_members[index] = ',';
    }
    empty_members[38] = '0';
    empty_members[39] = '-';
    Check(Utl_Http_ReadRange(&request, empty_members, 40), __LINE__);
    empty_members[38] = ',';
    empty_members[39] = '0';
    empty_members[40] = '-';
    Check(!Utl_Http_ReadRange(&request, empty_members, 41), __LINE__);
    Check(Utl_Http_ReadContentRange(&content, (const byte_t*)c_contentInput, sizeof(c_contentInput) - 1), __LINE__);
    Check(content.hasRange && content.hasCompleteLength, __LINE__);
    Check((0 == content.first) && (9 == content.last) && (10 == content.completeLength), __LINE__);
    Check((sizeof(c_contentCanonical) - 1) == Utl_Http_GetContentRangeByteCount(&content), __LINE__);
    for (index = 0; index < sizeof(output); ++index)
    {
        output[index] = (char_t)('A' + (index % 26));
    }
    for (capacity = 0; capacity < (sizeof(c_contentCanonical) - 1); ++capacity)
    {
        Check(0 == Utl_Http_WriteContentRange(&content, output, capacity), __LINE__);
        for (index = 0; index < sizeof(output); ++index)
        {
            Check((char_t)('A' + (index % 26)) == output[index], __LINE__);
        }
    }
    Check((sizeof(c_contentCanonical) - 1) ==
              Utl_Http_WriteContentRange(&content, output, sizeof(c_contentCanonical) - 1),
          __LINE__);
    Check(Equal((const byte_t*)c_contentCanonical, (const byte_t*)output, sizeof(c_contentCanonical) - 1), __LINE__);
    for (index = 0; index < (sizeof(c_badContents) / sizeof(c_badContents[0])); ++index)
    {
        Check(!Utl_Http_ReadContentRange(&content, (const byte_t*)c_badContents[index], c_badContentBytes[index]),
              __LINE__);
        Check(content.hasRange && content.hasCompleteLength && (9 == content.last) && (10 == content.completeLength),
              __LINE__);
    }
    Check(Utl_Http_ReadContentRange(&content, (const byte_t*)c_unknownLength, sizeof(c_unknownLength) - 1), __LINE__);
    Check(content.hasRange && !content.hasCompleteLength && (1 == content.first) && (9 == content.last), __LINE__);
    Check((sizeof(c_unknownLength) - 1) == Utl_Http_WriteContentRange(&content, output, sizeof(output)), __LINE__);
    Check(Equal((const byte_t*)c_unknownLength, (const byte_t*)output, sizeof(c_unknownLength) - 1), __LINE__);
    Check(Utl_Http_ReadContentRange(&content, (const byte_t*)c_unsatisfied, sizeof(c_unsatisfied) - 1), __LINE__);
    Check(!content.hasRange && content.hasCompleteLength && (0 == content.completeLength), __LINE__);
    Check((sizeof(c_unsatisfied) - 1) == Utl_Http_WriteContentRange(&content, output, sizeof(output)), __LINE__);
    Check(Equal((const byte_t*)c_unsatisfied, (const byte_t*)output, sizeof(c_unsatisfied) - 1), __LINE__);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Runs protocol checks with a CRT-free executable entry point.
 * \par Details:
 *  The selected platform entry owns startup; the fixture itself never allocates.
 */
uint32_t OS_Application_Main(void)
{

    CheckUnicodeVectors();
    CheckUnicodeDomain();
    CheckCastFrames();
    CheckCastSchema();
    CheckDecimal();
    CheckHttpRanges();
    return 0;
}
