/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Cast LOAD profile boundary fixtures, independent complete wire strings,
 * transactional capacity failures and scalar UTF-8 rejection.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_castjson.h"
#include "utl/utl_unicode.h"
#include "crt/crt_memory.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_OUTPUT_CAPACITY    (4096)
#define TEST_SENTINEL           (0xA5)
#define TEST_REQUEST_ID         (9)
#define TEST_START_MILLISECONDS (1234)
#define TEST_TRACK_ID           (7)
#define TEST_SESSION            "session\"\\"
#define TEST_CONTENT            "http://host/\"file"
#define TEST_TYPE               "video/\"mp4"
#define TEST_SUBTITLE           "http://host/sub\\\".vtt"

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static const char_t c_baseline[] = "{\"type\":\"LOAD\",\"requestId\":9,\"sessionId\":\"session\",\"autoplay\":true,\"currentTime\":1.234,\"media\":{\"contentId\":\"http://host/"
                                   "movie\",\"contentType\":\"video/mp4\",\"streamType\":\"BUFFERED\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"}}}";
static const char_t c_subtitle[] =
    "{\"type\":\"LOAD\",\"requestId\":9,\"sessionId\":\"session\\\"\\\\\",\"autoplay\":false,\"currentTime\":1.234,\"activeTrackIds\":[7],\"media\":{\"contentId\":\"http://host/"
    "\\\"file\",\"contentType\":\"video/"
    "\\\"mp4\",\"streamType\":\"LIVE\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"},\"tracks\":[{\"trackId\":7,\"type\":\"TEXT\",\"subtype\":\"SUBTITLES\",\"name\":\"Captions\",\"language\":"
    "\"en\",\"trackContentId\":\"http://host/sub\\\\\\\".vtt\",\"trackContentType\":\"text/"
    "vtt\"}],\"textTrackStyle\":{\"foregroundColor\":\"#FFFFFFFF\",\"backgroundColor\":\"#00000000\",\"edgeType\":\"OUTLINE\",\"edgeColor\":\"#000000FF\",\"fontScale\":1.0}}}";
static const char_t c_numeric[]     = "{\"type\":\"LOAD\",\"requestId\":4294967295,\"sessionId\":\"session\",\"autoplay\":true,\"currentTime\":18446744073709551.615,\"media\":{\"contentId\":\"http://"
                                      "host/movie\",\"contentType\":\"video/mp4\",\"streamType\":\"NONE\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"}}}";
static const byte_t c_stringInput[] = {0x00,
                                       0x01,
                                       0x02,
                                       0x03,
                                       0x04,
                                       0x05,
                                       0x06,
                                       0x07,
                                       0x08,
                                       0x09,
                                       0x0A,
                                       0x0B,
                                       0x0C,
                                       0x0D,
                                       0x0E,
                                       0x0F,
                                       0x10,
                                       0x11,
                                       0x12,
                                       0x13,
                                       0x14,
                                       0x15,
                                       0x16,
                                       0x17,
                                       0x18,
                                       0x19,
                                       0x1A,
                                       0x1B,
                                       0x1C,
                                       0x1D,
                                       0x1E,
                                       0x1F,
                                       0x22,
                                       0x5C,
                                       0xF0,
                                       0x9F,
                                       0x98,
                                       0x80};
static const char_t c_stringExpected[] = "\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\u0008\\u0009\\u000A\\u000B\\u000C\\u000D\\u000E\\u000F\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u"
                                         "0016\\u0017\\u0018\\u0019\\u001A\\u001B\\u001C\\u001D\\u001E\\u001F\\\"\\\\"
                                         "\xF0\x9F\x98\x80"
                                         "\"}}";
static const byte_t c_overlong[]       = {0xC0, 0x80};
static const byte_t c_surrogate[]      = {0xED, 0xA0, 0x80};
static const byte_t c_truncated[]      = {0xF0, 0x9F, 0x98};
static const byte_t c_outOfRange[]     = {0xF4, 0x90, 0x80, 0x80};
static char_t       g_output[TEST_OUTPUT_CAPACITY];   ///< Fixed output, reused by every case.
static char_t       g_original[TEST_OUTPUT_CAPACITY]; ///< Sentinel image for whole-buffer comparisons.

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Constructs a counted fixture view.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 * \param[in] data        Fixture bytes
 * \param[in] byteCount   Readable bytes
 *
 * \retval    text   Borrowed fixture view.
 */
static FORCE_INLINE Utl_CastJson_Text_t Text(const char_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Initializes the baseline request fixture.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 * \param[out] load   Required fixture model
 *
 * \retval     None   No result value; a fixture mismatch asserts.
 */
static FORCE_INLINE void InitializeLoad(Utl_CastJson_Load_t* load);

/*************************************************************************************************/
/**
 * \brief Checks a complete independently specified request and every short capacity.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 *
 * \retval None   No result value; a fixture mismatch asserts.
 */
static FORCE_INLINE void CheckKnownLoad(void);

/*************************************************************************************************/
/**
 * \brief Checks subtitles, pause state, LIVE semantics and escaping of every dynamic field.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 *
 * \retval None   No result value; a fixture mismatch asserts.
 */
static FORCE_INLINE void CheckSubtitle(void);

/*************************************************************************************************/
/**
 * \brief Checks every control escape, quote, reverse solidus and supplementary UTF-8.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 *
 * \retval None   No result value; a fixture mismatch asserts.
 */
static FORCE_INLINE void CheckStrings(void);

/*************************************************************************************************/
/**
 * \brief Verifies that an invalid model preserves its entire destination.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 * \param[in] load   Deliberately invalid fixture
 *
 * \retval    None   No result value; a fixture mismatch asserts.
 */
static FORCE_INLINE void CheckRejected(Utl_CastJson_Load_t* load);

/*************************************************************************************************/
/**
 * \brief Checks missing fields, unsupported kinds and malformed UTF-8.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 *
 * \retval None   No result value; a fixture mismatch asserts.
 */
static FORCE_INLINE void CheckInvalidModels(void);

/*************************************************************************************************/
/**
 * \brief Checks exact uint64_t time output and bounded string measurement.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 *
 * \retval None   No result value; a fixture mismatch asserts.
 */
static FORCE_INLINE void CheckNumericLimits(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Fixture counts are independent of any termination scan.
 */
static FORCE_INLINE Utl_CastJson_Text_t Text(const char_t* data, uint32_t byteCount)
{
    Utl_CastJson_Text_t text;

    text.data      = data;
    text.byteCount = byteCount;

    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All text is static fixture storage; serialization borrows it without allocation.
 */
static FORCE_INLINE void InitializeLoad(Utl_CastJson_Load_t* load)
{
    LOGASSERT(NULL != load);

    load->requestId         = TEST_REQUEST_ID;
    load->sessionId         = Text("session", sizeof("session") - 1);
    load->autoplay          = TRUE;
    load->startMilliseconds = TEST_START_MILLISECONDS;
    load->contentId         = Text("http://host/movie", sizeof("http://host/movie") - 1);
    load->contentType       = Text("video/mp4", sizeof("video/mp4") - 1);
    load->stream            = UTL_CASTJSON_STREAM_BUFFERED;
    load->title             = Text("Title", sizeof("Title") - 1);
    load->subtitle          = NULL;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Exact-capacity output is not terminated. Every shorter capacity preserves all output bytes.
 */
static FORCE_INLINE void CheckKnownLoad(void)
{
    Utl_CastJson_Load_t load;
    uint32_t            expected;
    uint32_t            count;
    uint32_t            capacity;

    InitializeLoad(&load);
    expected = sizeof(c_baseline) - 1;
    count    = Utl_CastJson_GetLoadByteCount(&load);
    LOGASSERT(expected == count);
    CRT_Memory_Set(g_output, TEST_SENTINEL, sizeof(g_output));
    count = Utl_CastJson_WriteLoad(&load, g_output, expected);
    LOGASSERT(expected == count);
    LOGASSERT(0 == CRT_Memory_Compare(g_output, c_baseline, expected));
    LOGASSERT(TEST_SENTINEL == (byte_t)g_output[expected]);
    CRT_Memory_Set(g_original, TEST_SENTINEL, sizeof(g_original));
    for (capacity = 0; capacity < expected; ++capacity)
    {
        CRT_Memory_Copy(g_output, g_original, sizeof(g_output));
        count = Utl_CastJson_WriteLoad(&load, g_output, capacity);
        LOGASSERT(0 == count);
        LOGASSERT(0 == CRT_Memory_Compare(g_output, g_original, sizeof(g_output)));
    }
    LOGASSERT(0 == Utl_CastJson_WriteLoad(&load, NULL, expected));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The independently authored wire fixture includes activeTrackIds and the existing outline style.
 */
static FORCE_INLINE void CheckSubtitle(void)
{
    Utl_CastJson_Load_t     load;
    Utl_CastJson_Subtitle_t track;
    uint32_t                count;

    InitializeLoad(&load);
    load.sessionId   = Text(TEST_SESSION, sizeof(TEST_SESSION) - 1);
    load.contentId   = Text(TEST_CONTENT, sizeof(TEST_CONTENT) - 1);
    load.contentType = Text(TEST_TYPE, sizeof(TEST_TYPE) - 1);
    load.autoplay    = FALSE;
    load.stream      = UTL_CASTJSON_STREAM_LIVE;
    track.trackId    = TEST_TRACK_ID;
    track.contentId  = Text(TEST_SUBTITLE, sizeof(TEST_SUBTITLE) - 1);
    track.name       = Text("Captions", sizeof("Captions") - 1);
    track.language   = Text("en", 2);
    load.subtitle    = &track;
    count            = Utl_CastJson_WriteLoad(&load, g_output, sizeof(g_output));
    LOGASSERT(sizeof(c_subtitle) - 1 == count);
    LOGASSERT(0 == CRT_Memory_Compare(g_output, c_subtitle, count));
    track.trackId = 0;
    CRT_Memory_Copy(g_output, g_original, sizeof(g_output));
    count = Utl_CastJson_WriteLoad(&load, g_output, sizeof(g_output));
    LOGASSERT(0 == count);
    LOGASSERT(0 == CRT_Memory_Compare(g_output, g_original, sizeof(g_output)));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The input contains embedded U+0000 and all U+0001 through U+001F characters.
 *  Expected escape bytes are fixed independently of the serializer's digit algorithm.
 */
static FORCE_INLINE void CheckStrings(void)
{
    Utl_CastJson_Load_t load;
    uint32_t            count;
    uint32_t            prefix;

    InitializeLoad(&load);
    load.title = Text((const char_t*)c_stringInput, sizeof(c_stringInput));
    count      = Utl_CastJson_WriteLoad(&load, g_output, sizeof(g_output));
    prefix     = sizeof(c_baseline) - 1 - sizeof("Title\"}}") + 1;
    LOGASSERT(prefix + sizeof(c_stringExpected) - 1 == count);
    LOGASSERT(0 == CRT_Memory_Compare(g_output, c_baseline, prefix));
    LOGASSERT(0 == CRT_Memory_Compare(g_output + prefix, c_stringExpected, sizeof(c_stringExpected) - 1));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Both the query and writer must reject the same invalid model.
 */
static FORCE_INLINE void CheckRejected(Utl_CastJson_Load_t* load)
{
    uint32_t count;

    CRT_Memory_Copy(g_output, g_original, sizeof(g_output));
    count = Utl_CastJson_GetLoadByteCount(load);
    LOGASSERT(0 == count);
    count = Utl_CastJson_WriteLoad(load, g_output, sizeof(g_output));
    LOGASSERT(0 == count);
    LOGASSERT(0 == CRT_Memory_Compare(g_output, g_original, sizeof(g_output)));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Invalid byte sequences include overlong NUL, an encoded surrogate,
 *  a truncated supplementary scalar and a scalar beyond Unicode's maximum.
 */
static FORCE_INLINE void CheckInvalidModels(void)
{
    Utl_CastJson_Load_t load;

    InitializeLoad(&load);
    load.title = Text((const char_t*)c_overlong, sizeof(c_overlong));
    CheckRejected(&load);
    load.title = Text((const char_t*)c_surrogate, sizeof(c_surrogate));
    CheckRejected(&load);
    load.title = Text((const char_t*)c_truncated, sizeof(c_truncated));
    CheckRejected(&load);
    load.title = Text((const char_t*)c_outOfRange, sizeof(c_outOfRange));
    CheckRejected(&load);
    load.title = Text(NULL, 1);
    CheckRejected(&load);
    load.title     = Text(NULL, 0);
    load.contentId = Text(NULL, 0);
    CheckRejected(&load);
    InitializeLoad(&load);
    load.stream = UTL_CASTJSON_STREAM_INVALID;
    CheckRejected(&load);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The maximum integer time must retain its .615 millisecond fraction.
 *  A zero readable bound reports missing termination without inspecting input.
 */
static FORCE_INLINE void CheckNumericLimits(void)
{
    Utl_CastJson_Load_t load;
    uint32_t            count;

    InitializeLoad(&load);
    load.requestId         = UINT32_MAX;
    load.startMilliseconds = UINT64_MAX;
    load.stream            = UTL_CASTJSON_STREAM_NONE;
    count                  = Utl_CastJson_WriteLoad(&load, g_output, sizeof(g_output));
    LOGASSERT(sizeof(c_numeric) - 1 == count);
    LOGASSERT(0 == CRT_Memory_Compare(g_output, c_numeric, count));
    LOGASSERT(0 == Utl_Unicode_GetUtf8StringByteCount("", 1));
    LOGASSERT(UTL_UNICODE_NO_TERMINATOR == Utl_Unicode_GetUtf8StringByteCount("", 0));
    LOGASSERT(2 == Utl_Unicode_GetUtf8StringByteCount("ab", 3));
    LOGASSERT(UTL_UNICODE_NO_TERMINATOR == Utl_Unicode_GetUtf8StringByteCount("ab", 2));
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Every fixture uses startup storage and the CRT-free process entry.
 *  No network, receiver or media codec is needed for these wire checks.
 */
uint32_t OS_Application_Main(void)
{
    CheckKnownLoad();
    CheckSubtitle();
    CheckStrings();
    CheckInvalidModels();
    CheckNumericLimits();

    return 0;
}
