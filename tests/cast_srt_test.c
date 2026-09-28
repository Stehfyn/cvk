/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Independent SRT cue/timestamp vectors for the documented compatibility profile.
 * No receiver, filesystem, codec or allocation is involved in this fixture.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_srt.h"
#include "utl/utl_unicode.h"
#include "crt/crt_memory.h"
#include "os/os_process.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define SRT_TEST_SENTINEL       (17)
#define SRT_TEST_WIDTH_SENTINEL (19)
#define SRT_TEST_COUNT_SENTINEL (23)
#define SRT_TEST_STRING_BOUND   (1024)
#define SRT_TEST_MAX_TIME       ("5124095576030:25:51,615")

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief One independent timestamp wire vector and expected model. */
typedef struct TimeVector_s
{
    const char_t* input;
    uint64_t      milliseconds;
    uint32_t      hourDigits;
    uint32_t      bytesRead;
} TimeVector_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Independent integer-boundary and compatibility timestamps.
 *
 * \par Details:
 *  Expected values are literal test data rather than values derived by the parser.
 */
static const TimeVector_t  c_times[] = {{"00:00:00,000", 0, 2, 12},
                                        {"01:02:03,004 suffix", 3723004, 2, 12},
                                        {"10000:00:00.001", 36000000001, 5, 15},
                                        {"000000000000:00:00,000", 0, 12, 22},
                                        {SRT_TEST_MAX_TIME, UINT64_MAX, 13, sizeof(SRT_TEST_MAX_TIME) - 1}};

/*************************************************************************************************/
/** \brief Invalid timestamp widths, delimiters, components and arithmetic bounds. */
static const char_t* const c_invalidTimes[] = {"",
                                               "0:00:00,000",
                                               "00:00,000",
                                               "00:0:00,000",
                                               "00:00:0,000",
                                               "00:00:00,00",
                                               "00:00:00,0000",
                                               "00:60:00,000",
                                               "00:00:60,000",
                                               "00:00:00;000",
                                               "-01:00:00,000",
                                               "18446744073709551616:00:00,000",
                                               "5124095576030:25:51,616",
                                               "5124095576031:00:00,000"};

/*************************************************************************************************/
/** \brief Invalid whole cues, including identifiers, encoding and ordering. */
static const char_t* const c_invalidCues[] = {"orphan",
                                              "orphan\n\n",
                                              "first\nsecond\n00:00:00,000 --> 00:00:01,000\nx\n",
                                              "label-->x\n00:00:00,000 --> 00:00:01,000\nx\n",
                                              "00:00:00,000-->00:00:01,000\nx\n",
                                              "00:00:00,000 --> 00:00:00,000\nx\n",
                                              "00:00:02,000 --> 00:00:01,000\nx\n",
                                              "00:00:00,000 --> 00:00:01,000\n\xC0\x80\n",
                                              "\xED\xA0\x80\n00:00:00,000 --> 00:00:01,000\nx\n",
                                              "\xFF\xFE"};

/*************************************************************************************************/
/** \brief Two cues with mixed line endings, optional identifier and retained suffix. */
static const char_t        c_cues[] = "\xEF\xBB\xBF"
                                      "\r\n0001\r\n00:00:01,234 --> 00:00:05.678 X1:1 X2:2 Y1:3 Y2:4\r\n"
                                      "<i>First</i>\r\nsecond\r\n\r\n\r\n"
                                      "00:00:06.000 --> 00:00:07,000\nlast";

/** Stable readable storage for every sentinel span. */
static const char_t        c_marker[] = "retained field storage for cue rollback";

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Establishes defined recognizable cue fields for preservation checks.
 *
 * \par Requirements:
 *  - 001 Every field shall have a defined value or valid borrowed pointer.
 *
 * \param[out] cue   Required staging model
 *
 * \retval     None   The sentinel model is ready.
 */
static FORCE_INLINE void InitializeMarker(Utl_Srt_Cue_t* cue);

/*************************************************************************************************/
/**
 * \brief Verifies every public cue field was preserved.
 *
 * \par Requirements:
 *  - 001 Input shall have been initialized by InitializeMarker before the tested operation.
 *  - 002 Padding shall not participate in the comparison.
 *
 * \param[in] cue   Required model
 *
 * \retval    None   A changed field asserts.
 */
static FORCE_INLINE void CheckMarker(const Utl_Srt_Cue_t* cue);

/*************************************************************************************************/
/**
 * \brief Compares a borrowed result span against independent bytes.
 *
 * \par Requirements:
 *  - 001 Both counted extents shall match before memory comparison.
 *
 * \param[in] span        Required parsed span
 * \param[in] expected    Required expected bytes
 * \param[in] byteCount   Expected extent
 *
 * \retval    None   A mismatch asserts.
 */
static FORCE_INLINE void CheckSpan(const Utl_Srt_Span_t* span, const char_t* expected, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Checks failed timestamp parsing preserves every result field.
 *
 * \par Requirements:
 *  - 001 Input shall be an independently known malformed or incomplete timestamp.
 *  - 002 Time, lexical width and prefix count shall all remain unchanged.
 *
 * \param[in] input       Stable counted input
 * \param[in] byteCount   Readable extent
 *
 * \retval    None   Any publication after failure asserts.
 */
static FORCE_INLINE void CheckInvalidTimestamp(const char_t* input, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Checks timestamp values, prefix counts, invalid forms and truncation.
 *
 * \par Requirements:
 *  - 001 Every invalid/truncated timestamp shall preserve both outputs.
 *  - 002 Maximum representable time shall succeed and its immediate overflow shall fail.
 *
 * \retval None   Any failed requirement asserts.
 */
static FORCE_INLINE void CheckTimestamps(void);

/*************************************************************************************************/
/**
 * \brief Checks complete cues, line endings and repeated END.
 *
 * \par Requirements:
 *  - 001 Parsed values and all borrowed spans shall match independent expected data.
 *  - 002 END shall preserve cue output and consume trailing separators once.
 *
 * \retval None   Any failed requirement asserts.
 */
static FORCE_INLINE void CheckCues(void);

/*************************************************************************************************/
/**
 * \brief Checks malformed cue rollback and source-prefix boundaries.
 *
 * \par Requirements:
 *  - 001 INVALID shall preserve cursor and every cue field on repeated calls.
 *  - 002 Incomplete timing shall not publish an empty-looking valid cue.
 *  - 003 Embedded NUL shall be rejected using the counted extent.
 *
 * \retval None   Any failed requirement asserts.
 */
static FORCE_INLINE void CheckFailures(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Every pointer refers to a stable literal. Comparisons never inspect structure padding.
 */
static FORCE_INLINE void InitializeMarker(Utl_Srt_Cue_t* cue)
{
    cue->identifier.data           = c_marker;
    cue->identifier.byteCount      = SRT_TEST_COUNT_SENTINEL;
    cue->start.milliseconds        = SRT_TEST_SENTINEL;
    cue->start.hourDigits          = SRT_TEST_WIDTH_SENTINEL;
    cue->end.milliseconds          = SRT_TEST_SENTINEL + 1;
    cue->end.hourDigits            = SRT_TEST_WIDTH_SENTINEL;
    cue->timingExtension.data      = cue->identifier.data;
    cue->timingExtension.byteCount = SRT_TEST_COUNT_SENTINEL;
    cue->payload.data              = cue->identifier.data;
    cue->payload.byteCount         = SRT_TEST_COUNT_SENTINEL;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The marker is not consumed as source; only defined field values are inspected.
 */
static FORCE_INLINE void CheckMarker(const Utl_Srt_Cue_t* cue)
{
    LOGASSERT(SRT_TEST_COUNT_SENTINEL == cue->identifier.byteCount);
    LOGASSERT(c_marker == cue->identifier.data);
    LOGASSERT(SRT_TEST_SENTINEL == cue->start.milliseconds);
    LOGASSERT(SRT_TEST_WIDTH_SENTINEL == cue->start.hourDigits);
    LOGASSERT(SRT_TEST_SENTINEL + 1 == cue->end.milliseconds);
    LOGASSERT(SRT_TEST_WIDTH_SENTINEL == cue->end.hourDigits);
    LOGASSERT(cue->identifier.data == cue->timingExtension.data);
    LOGASSERT(SRT_TEST_COUNT_SENTINEL == cue->timingExtension.byteCount);
    LOGASSERT(cue->identifier.data == cue->payload.data);
    LOGASSERT(SRT_TEST_COUNT_SENTINEL == cue->payload.byteCount);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No source mutation or ownership transfer occurs.
 */
static FORCE_INLINE void CheckSpan(const Utl_Srt_Span_t* span, const char_t* expected, uint32_t byteCount)
{
    LOGASSERT(byteCount == span->byteCount);
    LOGASSERT(0 == CRT_Memory_Compare(span->data, expected, byteCount));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Sentinel values are disjoint from the expected failure state and retained verbatim.
 */
static FORCE_INLINE void CheckInvalidTimestamp(const char_t* input, uint32_t byteCount)
{
    Utl_Srt_Time_t time;
    uint32_t       consumed;
    bool_t         valid;

    time.milliseconds = SRT_TEST_SENTINEL;
    time.hourDigits   = SRT_TEST_WIDTH_SENTINEL;
    consumed          = SRT_TEST_COUNT_SENTINEL;
    valid             = Utl_Srt_ReadTimestamp(&time, &consumed, input, byteCount);
    LOGASSERT(!valid);
    LOGASSERT(SRT_TEST_SENTINEL == time.milliseconds);
    LOGASSERT(SRT_TEST_WIDTH_SENTINEL == time.hourDigits);
    LOGASSERT(SRT_TEST_COUNT_SENTINEL == consumed);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every mandatory prefix of a maximum-domain timestamp is independently rejected.
 */
static FORCE_INLINE void CheckTimestamps(void)
{
    uint32_t       index;
    uint32_t       byte_count;
    bool_t         valid;
    Utl_Srt_Time_t time;
    uint32_t       consumed;
    uint32_t       prefix;

    for (index = 0; index < sizeof(c_times) / sizeof(TimeVector_t); ++index)
    {
        byte_count = Utl_Unicode_GetUtf8StringByteCount(c_times[index].input, SRT_TEST_STRING_BOUND);
        valid      = Utl_Srt_ReadTimestamp(&time, &consumed, c_times[index].input, byte_count);
        LOGASSERT(valid);
        LOGASSERT(c_times[index].milliseconds == time.milliseconds);
        LOGASSERT(c_times[index].hourDigits == time.hourDigits);
        LOGASSERT(c_times[index].bytesRead == consumed);
    }
    for (index = 0; index < sizeof(c_invalidTimes) / sizeof(const char_t*); ++index)
    {
        byte_count = Utl_Unicode_GetUtf8StringByteCount(c_invalidTimes[index], SRT_TEST_STRING_BOUND);
        CheckInvalidTimestamp(c_invalidTimes[index], byte_count);
    }
    for (prefix = 0; prefix < sizeof(SRT_TEST_MAX_TIME) - 1; ++prefix)
    {
        CheckInvalidTimestamp(SRT_TEST_MAX_TIME, prefix);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Suffixes and payload line endings are compared as borrowed wire bytes.
 */
static FORCE_INLINE void CheckCues(void)
{
    Utl_Srt_Reader_t     reader;
    Utl_Srt_ReadResult_t result;
    Utl_Srt_Cue_t        cue;
    const char_t         cr_only[] = "custom id\r00:00:00,000 --> 00:00:01,000\rA\rB\r\r";
    const char_t         empty[]   = "00:00:00,000 --> 00:00:01,000";
    const char_t         blanks[]  = "\n\r\n\r";

    Utl_Srt_InitializeReader(&reader, c_cues, sizeof(c_cues) - 1);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_CUE == result);
    LOGASSERT(1234 == cue.start.milliseconds);
    LOGASSERT(5678 == cue.end.milliseconds);
    CheckSpan(&cue.identifier, "0001", sizeof("0001") - 1);
    CheckSpan(&cue.timingExtension, " X1:1 X2:2 Y1:3 Y2:4", sizeof(" X1:1 X2:2 Y1:3 Y2:4") - 1);
    CheckSpan(&cue.payload, "<i>First</i>\r\nsecond", sizeof("<i>First</i>\r\nsecond") - 1);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_CUE == result);
    LOGASSERT(6000 == cue.start.milliseconds);
    LOGASSERT(7000 == cue.end.milliseconds);
    LOGASSERT(0 == cue.identifier.byteCount);
    LOGASSERT(0 == cue.timingExtension.byteCount);
    CheckSpan(&cue.payload, "last", sizeof("last") - 1);
    InitializeMarker(&cue);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_END == result);
    LOGASSERT(0 == Utl_Srt_GetRemainingByteCount(&reader));
    CheckMarker(&cue);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_END == result);
    CheckMarker(&cue);

    Utl_Srt_InitializeReader(&reader, cr_only, sizeof(cr_only) - 1);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_CUE == result);
    CheckSpan(&cue.identifier, "custom id", sizeof("custom id") - 1);
    CheckSpan(&cue.payload, "A\rB", sizeof("A\rB") - 1);

    Utl_Srt_InitializeReader(&reader, empty, sizeof(empty) - 1);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_CUE == result);
    LOGASSERT(0 == cue.payload.byteCount);

    Utl_Srt_InitializeReader(&reader, blanks, sizeof(blanks) - 1);
    InitializeMarker(&cue);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_END == result);
    LOGASSERT(0 == Utl_Srt_GetRemainingByteCount(&reader));
    CheckMarker(&cue);
    Utl_Srt_InitializeReader(&reader, NULL, 0);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_END == result);
    CheckMarker(&cue);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A second malformed cue must not erase a previously committed reader position.
 */
static FORCE_INLINE void CheckFailures(void)
{
    uint32_t             index;
    uint32_t             byte_count;
    Utl_Srt_Reader_t     reader;
    Utl_Srt_Cue_t        cue;
    Utl_Srt_ReadResult_t result;
    const char_t         timing[] = "00:00:01,000 --> 00:00:02,000";
    uint32_t prefix;
    const char_t         nul_payload[]  = "00:00:01,000 --> 00:00:02,000\nA\0B";
    const char_t         late_invalid[] = "00:00:01,000 --> 00:00:02,000\nfirst\n\norphan\n\n";
    uint32_t offset;

    for (index = 0; index < sizeof(c_invalidCues) / sizeof(const char_t*); ++index)
    {
        byte_count = Utl_Unicode_GetUtf8StringByteCount(c_invalidCues[index], SRT_TEST_STRING_BOUND);
        Utl_Srt_InitializeReader(&reader, c_invalidCues[index], byte_count);
        InitializeMarker(&cue);
        result = Utl_Srt_ReadCue(&reader, &cue);
        LOGASSERT(UTL_SRT_READ_INVALID == result);
        LOGASSERT(0 == reader.offset);
        CheckMarker(&cue);
        result = Utl_Srt_ReadCue(&reader, &cue);
        LOGASSERT(UTL_SRT_READ_INVALID == result);
        LOGASSERT(0 == reader.offset);
        CheckMarker(&cue);
    }
    for (prefix = 1; prefix < sizeof(timing) - 1; ++prefix)
    {
        Utl_Srt_InitializeReader(&reader, timing, prefix);
        InitializeMarker(&cue);
        result = Utl_Srt_ReadCue(&reader, &cue);
        LOGASSERT(UTL_SRT_READ_INVALID == result);
        LOGASSERT(0 == reader.offset);
        CheckMarker(&cue);
    }
    Utl_Srt_InitializeReader(&reader, nul_payload, sizeof(nul_payload) - 1);
    InitializeMarker(&cue);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_INVALID == result);
    CheckMarker(&cue);

    Utl_Srt_InitializeReader(&reader, late_invalid, sizeof(late_invalid) - 1);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_CUE == result);
    offset = reader.offset;
    InitializeMarker(&cue);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_INVALID == result);
    LOGASSERT(offset == reader.offset);
    CheckMarker(&cue);
    result = Utl_Srt_ReadCue(&reader, &cue);
    LOGASSERT(UTL_SRT_READ_INVALID == result);
    LOGASSERT(offset == reader.offset);
    CheckMarker(&cue);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The CRT-free OS entry executes all pure SRT reader fixtures.
 */
uint32_t OS_Application_Main(void)
{
    CheckTimestamps();
    CheckCues();
    CheckFailures();

    return 0;
}
