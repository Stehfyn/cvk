/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Independent WebVTT wire strings and UTF-8/UTF-16 signature/byte-order vectors.
 * These fixtures exercise pure conversion without receiver, file or codec resources.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_webvtt.h"
#include "utl/utl_unicode.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEXT_TEST_CAPACITY      (256)
#define TEXT_TEST_INVALID_BYTES (6)
#define TEXT_TEST_SENTINEL      (0xA5)
#define TEXT_TEST_END           (1001)
#define TEXT_TEST_MAX_TIMESTAMP "5124095576030:25:51.615"

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Independent expected timestamp serialization. */
typedef struct Time_s
{
    uint64_t      value;
    const char_t* text;
    uint32_t      count;
} Time_t;

/*************************************************************************************************/
/** \brief A bounded malformed scalar sequence. */
typedef struct InvalidText_s
{
    byte_t   bytes[TEXT_TEST_INVALID_BYTES];
    uint32_t count;
} InvalidText_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static const byte_t        c_utf8[]        = {0xEF, 0xBB, 0xBF, 0x41, 0x00, 0xEF, 0xBB, 0xBF, 0xF0, 0x9F, 0x99, 0x82};
static const byte_t        c_utf16le[]     = {0xFF, 0xFE, 0x41, 0x00, 0x00, 0x00, 0xFF, 0xFE, 0x3D, 0xD8, 0x42, 0xDE};
static const byte_t        c_utf16be[]     = {0xFE, 0xFF, 0x00, 0x41, 0x00, 0x00, 0xFE, 0xFF, 0xD8, 0x3D, 0xDE, 0x42};
static const uint32_t      c_scalars[]     = {0x41, 0, 0xFEFF, 0x1F642};
static const InvalidText_t c_invalidText[] = {{{0xEF, 0xBB}, 2},
                                              {{0xC0, 0xAF}, 2},
                                              {{0xED, 0xA0, 0x80}, 3},
                                              {{0xF4, 0x90, 0x80, 0x80}, 4},
                                              {{0xFF, 0xFE, 0x41}, 3},
                                              {{0xFF, 0xFE, 0x00, 0xD8}, 4},
                                              {{0xFE, 0xFF, 0xDC, 0x00}, 4},
                                              {{0xFE, 0xFF, 0xD8, 0x00, 0x00, 0x41}, 6}};
static const Time_t        c_times[] = {{0, "00:00:00.000", sizeof("00:00:00.000") - 1},
                                        {1, "00:00:00.001", sizeof("00:00:00.001") - 1},
                                        {59999, "00:00:59.999", sizeof("00:00:59.999") - 1},
                                        {60000, "00:01:00.000", sizeof("00:01:00.000") - 1},
                                        {3600000, "01:00:00.000", sizeof("01:00:00.000") - 1},
                                        {360000000, "100:00:00.000", sizeof("100:00:00.000") - 1},
                                        {UINT64_MAX, TEXT_TEST_MAX_TIMESTAMP, sizeof(TEXT_TEST_MAX_TIMESTAMP) - 1}};
static const char_t* const c_invalidTimes[] = {"",
                                               "0:00.000",
                                               "00:0.000",
                                               "00:00.00",
                                               "00:00.0000",
                                               "00:00,000",
                                               "60:00.000",
                                               "00:60.000",
                                               "1:00:00.000",
                                               "01:60:00.000",
                                               "01:00:60.000",
                                               "000:00.000",
                                               "-01:00:00.000",
                                               "5124095576030:25:51.616",
                                               "18446744073709551616:00:00.000"};
static const char_t        c_cueText[]      = "<x>& -->\r\n\r\nb\n";
static const char_t        c_cueWire[]      = "00:00:00.000 --> 00:00:01.001\n&lt;x&gt;&amp; --&gt;\n \nb\n \n\n";
static const byte_t        c_cueUtf16[]     = {0xFF, 0xFE, 0x3D, 0xD8, 0x42, 0xDE};
static const char_t        c_cueUtf16Wire[] = "00:00:00.000 --> 00:00:01.001\n\xF0\x9F\x99\x82\n\n";
static const char_t        c_emptyCueWire[] = "00:00:00.000 --> 00:00:01.001\n\n";

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Checks one complete signature-marked byte sequence against independent scalars.
 *
 * \par Details:
 *  Scalar values and idempotent end-of-input assertions pass.
 *
 * \param[in] data        Stable source vector
 * \param[in] byteCount   Readable bytes
 *
 * \retval    None
 */
static FORCE_INLINE void CheckReader(const byte_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Checks repeated malformed reads preserve output and cursor position.
 *
 * \par Details:
 *  All malformed scalar assertions pass.
 *
 * \retval None
 */
static FORCE_INLINE void CheckInvalidReaders(void);

/*************************************************************************************************/
/**
 * \brief Checks one independent timestamp wire vector and every shorter output capacity.
 *
 * \par Details:
 *  Count, write, parse and output-preservation assertions pass.
 *
 * \param[in] item   Independent expected value, bytes and count
 *
 * \retval    None
 */
static FORCE_INLINE void CheckTimestamp(const Time_t* item);

/*************************************************************************************************/
/**
 * \brief Checks invalid timestamp grammar and every truncated maximum-value prefix.
 *
 * \par Details:
 *  Invalid-input output-preservation assertions pass.
 *
 * \retval None
 */
static FORCE_INLINE void CheckInvalidTimestamps(void);

/*************************************************************************************************/
/**
 * \brief Checks a complete cue against independently written WebVTT bytes.
 *
 * \par Details:
 *  Wire and all-short-capacity assertions pass.
 *
 * \param[in] cue         Stable plain-text model
 * \param[in] expected    Independent expected serialization
 * \param[in] byteCount   Expected serialization length
 *
 * \retval    None
 */
static FORCE_INLINE void CheckCue(const Utl_WebVtt_Cue_t* cue, const char_t* expected, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Checks encoding, newline, escaping, timing and invalid-cue boundaries.
 *
 * \par Details:
 *  Every cue fixture passes.
 *
 * \retval None
 */
static FORCE_INLINE void CheckCues(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Only the initial BOM disappears; a later BOM and NUL are returned as scalars.
 */
static FORCE_INLINE void CheckReader(const byte_t* data, uint32_t byteCount)
{
    Utl_Unicode_Reader_t     reader;
    uint32_t                 index;
    Utl_Unicode_ReadResult_t result;
    uint32_t                 scalar;

    Utl_Unicode_InitializeReader(&reader, data, byteCount);
    for (index = 0; index < sizeof(c_scalars) / sizeof(uint32_t); ++index)
    {
        result = Utl_Unicode_ReadScalar(&reader, &scalar);
        LOGASSERT(UTL_UNICODE_READ_SCALAR == result);
        LOGASSERT(c_scalars[index] == scalar);
    }
    LOGASSERT(0 == Utl_Unicode_GetRemainingByteCount(&reader));
    scalar = UINT32_MAX;
    result = Utl_Unicode_ReadScalar(&reader, &scalar);
    LOGASSERT(UTL_UNICODE_READ_END == result);
    LOGASSERT(UINT32_MAX == scalar);
    result = Utl_Unicode_ReadScalar(&reader, &scalar);
    LOGASSERT(UTL_UNICODE_READ_END == result);
    LOGASSERT(UINT32_MAX == scalar);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Cases include partial signatures, overlong UTF-8, surrogate UTF-8, out-of-range
 *  scalars, odd UTF-16 extents, isolated surrogates and a mismatched pair.
 */
static FORCE_INLINE void CheckInvalidReaders(void)
{
    uint32_t                 index;
    Utl_Unicode_Reader_t     reader;
    uint32_t                 remaining;
    const byte_t*            next;
    uint32_t                 scalar;
    Utl_Unicode_ReadResult_t result;

    for (index = 0; index < sizeof(c_invalidText) / sizeof(InvalidText_t); ++index)
    {
        Utl_Unicode_InitializeReader(&reader, c_invalidText[index].bytes, c_invalidText[index].count);
        remaining = Utl_Unicode_GetRemainingByteCount(&reader);
        next      = reader.next;
        scalar    = UINT32_MAX;
        result    = Utl_Unicode_ReadScalar(&reader, &scalar);
        LOGASSERT(UTL_UNICODE_READ_INVALID == result);
        LOGASSERT(UINT32_MAX == scalar);
        LOGASSERT(remaining == Utl_Unicode_GetRemainingByteCount(&reader));
        LOGASSERT(next == reader.next);
        result = Utl_Unicode_ReadScalar(&reader, &scalar);
        LOGASSERT(UTL_UNICODE_READ_INVALID == result);
        LOGASSERT(UINT32_MAX == scalar);
        LOGASSERT(remaining == Utl_Unicode_GetRemainingByteCount(&reader));
        LOGASSERT(next == reader.next);
    }
    Utl_Unicode_InitializeReader(&reader, NULL, 0);
    scalar = UINT32_MAX;
    result = Utl_Unicode_ReadScalar(&reader, &scalar);
    LOGASSERT(UTL_UNICODE_READ_END == result);
    LOGASSERT(UINT32_MAX == scalar);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Query/write agreement is checked against explicit wire bytes, not only a round trip.
 */
static FORCE_INLINE void CheckTimestamp(const Time_t* item)
{
    uint32_t count;
    uint64_t value;
    byte_t   before[TEXT_TEST_CAPACITY];
    uint32_t capacity;
    byte_t   output[TEXT_TEST_CAPACITY];
    uint32_t consumed;
    bool_t   valid;

    count = Utl_WebVtt_GetTimestampByteCount(item->value);
    LOGASSERT(item->count == count);
    CRT_Memory_Set(before, TEXT_TEST_SENTINEL, sizeof(before));
    for (capacity = 0; capacity < count; ++capacity)
    {
        CRT_Memory_Copy(output, before, sizeof(output));
        consumed = Utl_WebVtt_WriteTimestamp(item->value, output, capacity);
        LOGASSERT(0 == consumed);
        LOGASSERT(0 == CRT_Memory_Compare(output, before, sizeof(output)));
    }
    consumed = Utl_WebVtt_WriteTimestamp(item->value, output, item->count);
    LOGASSERT(item->count == consumed);
    LOGASSERT(0 == CRT_Memory_Compare(output, item->text, item->count));
    valid = Utl_WebVtt_ReadTimestamp(&value, &consumed, (const byte_t*)item->text, item->count);
    LOGASSERT(valid);
    LOGASSERT(item->value == value);
    LOGASSERT(item->count == consumed);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The maximum supported time tests overflow at a single millisecond beyond it.
 */
static FORCE_INLINE void CheckInvalidTimestamps(void)
{
    uint32_t index;
    uint32_t count;
    uint64_t value;
    uint32_t consumed;
    bool_t   valid;

    for (index = 0; index < sizeof(c_invalidTimes) / sizeof(const char_t*); ++index)
    {
        count    = Utl_Unicode_GetUtf8StringByteCount(c_invalidTimes[index], TEXT_TEST_CAPACITY);
        value    = UINT64_MAX;
        consumed = UINT32_MAX;
        valid    = Utl_WebVtt_ReadTimestamp(&value, &consumed, (const byte_t*)c_invalidTimes[index], count);
        LOGASSERT(!valid);
        LOGASSERT(UINT64_MAX == value);
        LOGASSERT(UINT32_MAX == consumed);
    }
    for (count = 0; count < sizeof(TEXT_TEST_MAX_TIMESTAMP) - 1; ++count)
    {
        value    = UINT64_MAX;
        consumed = UINT32_MAX;
        valid    = Utl_WebVtt_ReadTimestamp(&value, &consumed, (const byte_t*)TEXT_TEST_MAX_TIMESTAMP, count);
        LOGASSERT(!valid);
        LOGASSERT(UINT64_MAX == value);
        LOGASSERT(UINT32_MAX == consumed);
    }
    valid = Utl_WebVtt_ReadTimestamp(&value,
                                    &consumed,
                                    (const byte_t*)"00:01.001 trailing",
                                    sizeof("00:01.001 trailing") - 1);
    LOGASSERT(valid);
    LOGASSERT(TEXT_TEST_END == value);
    LOGASSERT(sizeof("00:01.001") - 1 == consumed);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every smaller capacity preserves the full output object, including bytes beyond capacity.
 */
static FORCE_INLINE void CheckCue(const Utl_WebVtt_Cue_t* cue, const char_t* expected, uint32_t byteCount)
{
    uint32_t count;
    byte_t   before[TEXT_TEST_CAPACITY];
    uint32_t capacity;
    byte_t   output[TEXT_TEST_CAPACITY];
    uint32_t written;

    count = Utl_WebVtt_GetCueByteCount(cue);
    CRT_Memory_Set(before, TEXT_TEST_SENTINEL, sizeof(before));
    for (capacity = 0; capacity < byteCount; ++capacity)
    {
        CRT_Memory_Copy(output, before, sizeof(output));
        written = Utl_WebVtt_WriteCue(cue, output, capacity);
        LOGASSERT(0 == written);
        LOGASSERT(0 == CRT_Memory_Compare(output, before, sizeof(output)));
    }
    written = Utl_WebVtt_WriteCue(cue, output, byteCount);
    LOGASSERT(byteCount == written);
    LOGASSERT(0 == CRT_Memory_Compare(output, expected, byteCount));
    LOGASSERT(byteCount == count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Plain-text escaping is distinct from the application's existing styled-SubRip path.
 */
static FORCE_INLINE void CheckCues(void)
{
    Utl_WebVtt_Cue_t cue;
    byte_t           before[TEXT_TEST_CAPACITY];
    uint32_t         index;
    byte_t           output[TEXT_TEST_CAPACITY];
    uint32_t         written;

    cue.start         = 0;
    cue.end           = TEXT_TEST_END;
    cue.text          = (const byte_t*)c_cueText;
    cue.textByteCount = sizeof(c_cueText) - 1;
    CheckCue(&cue, c_cueWire, sizeof(c_cueWire) - 1);
    cue.text          = c_cueUtf16;
    cue.textByteCount = sizeof(c_cueUtf16);
    CheckCue(&cue, c_cueUtf16Wire, sizeof(c_cueUtf16Wire) - 1);
    cue.text          = NULL;
    cue.textByteCount = 0;
    CheckCue(&cue, c_emptyCueWire, sizeof(c_emptyCueWire) - 1);
    CRT_Memory_Set(before, TEXT_TEST_SENTINEL, sizeof(before));
    for (index = 0; index < sizeof(c_invalidText) / sizeof(InvalidText_t); ++index)
    {
        cue.text          = c_invalidText[index].bytes;
        cue.textByteCount = c_invalidText[index].count;
        CRT_Memory_Copy(output, before, sizeof(output));
        written = Utl_WebVtt_WriteCue(&cue, output, sizeof(output));
        LOGASSERT(0 == written);
        LOGASSERT(0 == CRT_Memory_Compare(output, before, sizeof(output)));
    }
    cue.text          = c_utf8;
    cue.textByteCount = sizeof(c_utf8);
    written           = Utl_WebVtt_WriteCue(&cue, output, sizeof(output));
    LOGASSERT(0 == written);
    LOGASSERT(0 == CRT_Memory_Compare(output, before, sizeof(output)));
    cue.text          = NULL;
    cue.textByteCount = 0;
    cue.end           = cue.start;
    written           = Utl_WebVtt_WriteCue(&cue, output, sizeof(output));
    LOGASSERT(0 == written);
    LOGASSERT(0 == CRT_Memory_Compare(output, before, sizeof(output)));
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Fixtures run against production Unicode and WebVTT modules through CRT-free startup.
 */
uint32_t OS_Application_Main(void)
{
    uint32_t index;

    CheckReader(c_utf8, sizeof(c_utf8));
    CheckReader(c_utf16le, sizeof(c_utf16le));
    CheckReader(c_utf16be, sizeof(c_utf16be));
    CheckInvalidReaders();
    for (index = 0; index < sizeof(c_times) / sizeof(Time_t); ++index)
    {
        CheckTimestamp(&c_times[index]);
    }
    CheckInvalidTimestamps();
    CheckCues();

    return 0;
}
