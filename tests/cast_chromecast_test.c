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

#include "utl/utl_chromecast.h"
#include "utl/utl_castv2.h"
#include "utl/utl_json.h"
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
#define TEST_SUBTITLE_URL       "http://host/sub\\\".vtt"

#define TEST_BASELINE                                                                                                  \
    "{\"type\":\"LOAD\",\"requestId\":9,\"sessionId\":\"session\",\"autoplay\":true,\"currentTime\":1.234,\"media\":{" \
    "\"contentId\":\"http://host/"                                                                                     \
    "movie\",\"contentType\":\"video/"                                                                                 \
    "mp4\",\"streamType\":\"BUFFERED\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"}}}"
#define TEST_SUBTITLE                                                                                                  \
    "{\"type\":\"LOAD\",\"requestId\":9,\"sessionId\":\"session\\\"\\\\\",\"autoplay\":false,\"currentTime\":1.234,"   \
    "\"activeTrackIds\":[7],\"media\":{\"contentId\":\"http://host/"                                                   \
    "\\\"file\",\"contentType\":\"video/"                                                                              \
    "\\\"mp4\",\"streamType\":\"LIVE\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"},\"tracks\":[{"            \
    "\"trackId\":7,\"type\":\"TEXT\",\"subtype\":\"SUBTITLES\",\"name\":\"Captions\",\"language\":"                    \
    "\"en\",\"trackContentId\":\"http://host/sub\\\\\\\".vtt\",\"trackContentType\":\"text/"                           \
    "vtt\"}],\"textTrackStyle\":{\"foregroundColor\":\"#FFFFFFFF\",\"backgroundColor\":\"#00000000\",\"edgeType\":"    \
    "\"OUTLINE\",\"edgeColor\":\"#000000FF\",\"fontScale\":1.0}}}"
#define TEST_NUMERIC                                                                                                   \
    "{\"type\":\"LOAD\",\"requestId\":4294967295,\"sessionId\":\"session\",\"autoplay\":true,\"currentTime\":"         \
    "18446744073709551.615,\"media\":{\"contentId\":\"http://"                                                         \
    "host/movie\",\"contentType\":\"video/"                                                                            \
    "mp4\",\"streamType\":\"NONE\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"}}}"
#define TEST_STRING_INPUT                                                                                              \
    "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F\x10\x11\x12\x13\x14\x15\x16\x17\x18\x19\x1A\x1B" \
    "\x1C\x1D\x1E\x1F\x22\x5C\xF0\x9F\x98\x80"
#define TEST_STRING_EXPECTED                                                                                           \
    "\\u0000\\u0001\\u0002\\u0003\\u0004\\u0005\\u0006\\u0007\\u0008\\u0009\\u000A\\u000B\\u000C\\u000D\\u000E\\u000F" \
    "\\u0010\\u0011\\u0012\\u0013\\u0014\\u0015\\u"                                                                    \
    "0016\\u0017\\u0018\\u0019\\u001A\\u001B\\u001C\\u001D\\u001E\\u001F\\\"\\\\"                                      \
    "\xF0\x9F\x98\x80"                                                                                                 \
    "\"}}"
#define TEST_OVERLONG     "\xC0\x80"
#define TEST_SURROGATE    "\xED\xA0\x80"
#define TEST_TRUNCATED    "\xF0\x9F\x98"
#define TEST_OUT_OF_RANGE "\xF4\x90\x80\x80"
#define TEST_STYLE_NONE                                                                                                \
    "{\"type\":\"LOAD\",\"requestId\":9,\"sessionId\":\"session\",\"autoplay\":true,\"currentTime\":1.234,\"media\":{" \
    "\"contentId\":\"http://host/movie\",\"contentType\":\"video/"                                                     \
    "mp4\",\"streamType\":\"BUFFERED\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"},\"textTrackStyle\":{"     \
    "\"foregroundColor\":\"#12345678\",\"backgroundColor\":\"#9ABCDEF0\",\"edgeType\":\"NONE\",\"edgeColor\":\"#"      \
    "01020304\",\"fontScale\":1.25}}}"
#define TEST_STYLE_OUTLINE                                                                                             \
    "{\"type\":\"LOAD\",\"requestId\":9,\"sessionId\":\"session\",\"autoplay\":true,\"currentTime\":1.234,\"media\":{" \
    "\"contentId\":\"http://host/movie\",\"contentType\":\"video/"                                                     \
    "mp4\",\"streamType\":\"BUFFERED\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"},\"textTrackStyle\":{"     \
    "\"foregroundColor\":\"#12345678\",\"backgroundColor\":\"#9ABCDEF0\",\"edgeType\":\"OUTLINE\",\"edgeColor\":\"#"   \
    "01020304\",\"fontScale\":1.25}}}"
#define TEST_STYLE_DROP_SHADOW                                                                                         \
    "{\"type\":\"LOAD\",\"requestId\":9,\"sessionId\":\"session\",\"autoplay\":true,\"currentTime\":1.234,\"media\":{" \
    "\"contentId\":\"http://host/movie\",\"contentType\":\"video/"                                                     \
    "mp4\",\"streamType\":\"BUFFERED\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"},\"textTrackStyle\":{"     \
    "\"foregroundColor\":\"#12345678\",\"backgroundColor\":\"#9ABCDEF0\",\"edgeType\":\"DROP_SHADOW\",\"edgeColor\":"  \
    "\"#01020304\",\"fontScale\":1.25}}}"
#define TEST_STYLE_RAISED                                                                                              \
    "{\"type\":\"LOAD\",\"requestId\":9,\"sessionId\":\"session\",\"autoplay\":true,\"currentTime\":1.234,\"media\":{" \
    "\"contentId\":\"http://host/movie\",\"contentType\":\"video/"                                                     \
    "mp4\",\"streamType\":\"BUFFERED\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"},\"textTrackStyle\":{"     \
    "\"foregroundColor\":\"#12345678\",\"backgroundColor\":\"#9ABCDEF0\",\"edgeType\":\"RAISED\",\"edgeColor\":\"#"    \
    "01020304\",\"fontScale\":1.25}}}"
#define TEST_STYLE_DEPRESSED                                                                                           \
    "{\"type\":\"LOAD\",\"requestId\":9,\"sessionId\":\"session\",\"autoplay\":true,\"currentTime\":1.234,\"media\":{" \
    "\"contentId\":\"http://host/movie\",\"contentType\":\"video/"                                                     \
    "mp4\",\"streamType\":\"BUFFERED\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"},\"textTrackStyle\":{"     \
    "\"foregroundColor\":\"#12345678\",\"backgroundColor\":\"#9ABCDEF0\",\"edgeType\":\"DEPRESSED\",\"edgeColor\":\"#" \
    "01020304\",\"fontScale\":1.25}}}"
#define TEST_SUBTITLE_NO_STYLE                                                                                         \
    "{\"type\":\"LOAD\",\"requestId\":9,\"sessionId\":\"session\\\"\\\\\",\"autoplay\":false,\"currentTime\":1.234,"   \
    "\"activeTrackIds\":[7],\"media\":{\"contentId\":\"http://host/\\\"file\",\"contentType\":\"video/"                \
    "\\\"mp4\",\"streamType\":\"LIVE\",\"metadata\":{\"metadataType\":0,\"title\":\"Title\"},\"tracks\":[{"            \
    "\"trackId\":7,\"type\":\"TEXT\",\"subtype\":\"SUBTITLES\",\"name\":\"Captions\",\"language\":\"en\","             \
    "\"trackContentId\":\"http://host/sub\\\\\\\".vtt\",\"trackContentType\":\"text/vtt\"}]}}"

#define TEST_OPAQUE_WHITE       (0xFFFFFFFF)
#define TEST_OPAQUE_BLACK       (0x000000FF)
#define TEST_DEFAULT_FONT_SCALE (10)
#define TEST_STYLE_FOREGROUND   (0x12345678)
#define TEST_STYLE_BACKGROUND   (0x9ABCDEF0)
#define TEST_STYLE_EDGE_COLOR   (0x01020304)
#define TEST_STYLE_FONT_SCALE   (125)

#define TEST_COMMAND_CONNECT          "{\"type\":\"CONNECT\"}"
#define TEST_COMMAND_PING             "{\"type\":\"PING\"}"
#define TEST_COMMAND_PONG             "{\"type\":\"PONG\"}"
#define TEST_COMMAND_LAUNCH           "{\"type\":\"LAUNCH\",\"requestId\":9,\"appId\":\"app\\\"\\\\\\u0000\"}"
#define TEST_COMMAND_APP_ID           "app\"\\\0"
#define TEST_COMMAND_STOP_APPLICATION "{\"type\":\"STOP\",\"requestId\":9,\"sessionId\":\"session\\\"\\\\\"}"
#define TEST_COMMAND_ALL_STATUS       "{\"type\":\"GET_STATUS\",\"requestId\":9}"
#define TEST_COMMAND_ONE_STATUS "{\"type\":\"GET_STATUS\",\"requestId\":9,\"mediaSessionId\":-9223372036854775808}"
#define TEST_COMMAND_PLAY       "{\"type\":\"PLAY\",\"requestId\":9,\"mediaSessionId\":9223372036854775807}"
#define TEST_COMMAND_PAUSE      "{\"type\":\"PAUSE\",\"requestId\":9,\"mediaSessionId\":0}"
#define TEST_COMMAND_STOP_MEDIA "{\"type\":\"STOP\",\"requestId\":9,\"mediaSessionId\":-1}"
#define TEST_COMMAND_SEEK       "{\"type\":\"SEEK\",\"requestId\":9,\"mediaSessionId\":7,\"currentTime\":1.234}"
#define TEST_COMMAND_SEEK_START                                                                                        \
    "{\"type\":\"SEEK\",\"requestId\":9,\"mediaSessionId\":7,\"currentTime\":1.234,\"resumeState\":\"PLAYBACK_"        \
    "START\"}"
#define TEST_COMMAND_SEEK_PAUSE                                                                                        \
    "{\"type\":\"SEEK\",\"requestId\":9,\"mediaSessionId\":7,\"currentTime\":1.234,\"resumeState\":\"PLAYBACK_"        \
    "PAUSE\"}"
#define TEST_COMMAND_SEEK_LIMIT                                                                                        \
    "{\"type\":\"SEEK\",\"requestId\":4294967295,\"mediaSessionId\":7,\"currentTime\":1.8446744073709551615}"

#define TEST_NAMESPACE_CONNECTION "urn:x-cast:com.google.cast.tp.connection"
#define TEST_NAMESPACE_HEARTBEAT  "urn:x-cast:com.google.cast.tp.heartbeat"
#define TEST_NAMESPACE_RECEIVER   "urn:x-cast:com.google.cast.receiver"
#define TEST_NAMESPACE_MEDIA      "urn:x-cast:com.google.cast.media"
#define TEST_ROUTE_ENCODED        "web\\u0000\\u0035\\uD83D\\uDE80"
#define TEST_ROUTE_DECODED                                                                                             \
    "web\x00"                                                                                                          \
    "5\xF0\x9F\x9A\x80"
#define TEST_ROUTE_FRAME                                                                                               \
    "\x00\x00\x00\x19\x08\x00\x12\x01"                                                                                 \
    "s"                                                                                                                \
    "\x1A\x09" TEST_ROUTE_DECODED "\x22\x01"                                                                           \
    "n"                                                                                                                \
    "\x28\x00\x32\x02"                                                                                                 \
    "{}"

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Entry-owned output and rejection snapshot shared by sequential cases.
 */
typedef struct TestContext_s
{
    byte_t output[TEST_OUTPUT_CAPACITY];   ///< Writable serialization destination.
    byte_t original[TEST_OUTPUT_CAPACITY]; ///< Whole-buffer rejection sentinel.
} TestContext_t;

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
static FORCE_INLINE Utl_Json_Text_t Text(const char_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Initializes the baseline request fixture.
 *
 * \par Details:
 *  A fixture mismatch asserts.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 * \param[out] load   Required fixture model
 *
 * \retval     None
 */
static FORCE_INLINE void InitializeLoad(Utl_Chromecast_Load_t* load);

/*************************************************************************************************/
/**
 * \brief Checks a complete independently specified request and every short capacity.
 *
 * \par Details:
 *  A fixture mismatch asserts.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 *
 * \retval None
 */
static FORCE_INLINE void CheckKnownLoad(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks subtitles, pause state, LIVE semantics and escaping of every dynamic field.
 *
 * \par Details:
 *  A fixture mismatch asserts.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 *
 * \retval None
 */
static FORCE_INLINE void CheckSubtitle(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks every control escape, quote, reverse solidus and supplementary UTF-8.
 *
 * \par Details:
 *  A fixture mismatch asserts.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 *
 * \retval None
 */
static FORCE_INLINE void CheckStrings(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Verifies that an invalid model preserves its entire destination.
 *
 * \par Details:
 *  A fixture mismatch asserts.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 * \param[in] load   Deliberately invalid fixture
 *
 * \retval    None
 */
static FORCE_INLINE void CheckRejected(TestContext_t* context, Utl_Chromecast_Load_t* load);

/*************************************************************************************************/
/**
 * \brief Checks missing fields, unsupported kinds and malformed UTF-8.
 *
 * \par Details:
 *  A fixture mismatch asserts.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 *
 * \retval None
 */
static FORCE_INLINE void CheckInvalidModels(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks exact uint64_t time output and bounded string measurement.
 *
 * \par Details:
 *  A fixture mismatch asserts.
 *
 * \par Requirements:
 *  - 001 Fixture storage shall remain live for the complete call.
 *
 *
 * \retval None
 */
static FORCE_INLINE void CheckNumericLimits(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks optional styles, every edge token, packed colors and fractional font scale.
 *
 * \par Details:
 *  A mismatch asserts.
 *
 * \par Requirements:
 *  - 001 Complete independent wire strings shall match for all five supported edge kinds.
 *  - 002 Every short output and invalid style shall preserve the entire destination.
 *
 * \param[in,out] context   Entry-owned output and sentinel storage
 *
 * \retval        None
 */
static FORCE_INLINE void CheckStyles(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Compares one complete style request and every insufficient output capacity.
 *
 * \par Details:
 *  A mismatch asserts.
 *
 * \param[in,out] context     Entry-owned output and sentinel storage
 * \param[in]     load        Valid complete request
 * \param[in]     expected    Independently specified wire bytes
 * \param[in]     byteCount   Expected complete wire extent
 *
 * \retval        None
 */
static FORCE_INLINE void CheckStyle(TestContext_t*               context,
                                    const Utl_Chromecast_Load_t* load,
                                    const char_t*                expected,
                                    uint32_t                     byteCount);

/*************************************************************************************************/
/**
 * \brief Checks a complete independently specified command and every short capacity.
 *
 * \par Details:
 *  Mismatches assert in every build.
 *
 * \par Requirements:
 *  - 001 Output beyond the exact count shall remain unchanged; no terminator shall be added.
 *  - 002 Every short capacity and missing output shall fail without modifying storage.
 *
 * \param[in,out] context     Required entry-owned fixture buffers
 * \param[in]     command     Valid borrowed command and active fields
 * \param[in]     expected    Independent expected wire bytes
 * \param[in]     byteCount   Expected serialized extent
 *
 * \retval        None
 */
static FORCE_INLINE void CheckCommand(TestContext_t*                  context,
                                      const Utl_Chromecast_Command_t* command,
                                      const char_t*                   expected,
                                      uint32_t                        byteCount);

/*************************************************************************************************/
/**
 * \brief Checks rejected command models preserve the complete output region.
 *
 * \par Details:
 *  Unexpected acceptance or mutation asserts.
 *
 * \par Requirements:
 *  - 001 Counting and serialization shall both reject the selected invalid model.
 *
 * \param[in,out] context   Entry-owned fixture buffers
 * \param[in]     command   Invalid external field model with its selected member initialized
 *
 * \retval        None
 */
static FORCE_INLINE void CheckRejectedCommand(TestContext_t* context, const Utl_Chromecast_Command_t* command);

/*************************************************************************************************/
/**
 * \brief Exercises every supported command schema with independent complete wire strings.
 *
 * \par Details:
 *  Any mismatch asserts.
 *
 * \par Requirements:
 *  - 001 Type-only commands shall work without initialized inactive fields.
 *  - 002 IDs, decimal bounds, optional fields and JSON escaping shall match the independent fixtures.
 *
 * \param[in,out] context   Entry-owned fixture buffers
 *
 * \retval        None
 */
static FORCE_INLINE void CheckCommands(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Exercises invalid kinds, required fields, request IDs and seek domains.
 *
 * \par Details:
 *  Any unexpected acceptance asserts.
 *
 * \par Requirements:
 *  - 001 Invalid active fields shall fail transactionally, without accessing inactive fields.
 *
 * \param[in,out] context   Entry-owned fixture buffers
 *
 * \retval        None
 */
static FORCE_INLINE void CheckInvalidCommands(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks one independently specified namespace string.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Expected text shall remain readable for the complete comparison.
 *  - 002 Both count and every byte shall match the schema query.
 *
 * \param[in] kind       Namespace selector
 * \param[in] expected   Independent wire spelling
 *
 * \retval None
 */
static FORCE_INLINE void CheckNamespace(Utl_Chromecast_Namespace_t kind, Utl_Json_Text_t expected);

/*************************************************************************************************/
/**
 * \brief Checks protocol namespaces and the namespace selected by every command kind.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Unsupported selectors shall return their documented empty or invalid result.
 *  - 002 Every supported command shall select the protocol's expected namespace.
 *
 * \retval None
 */
static FORCE_INLINE void CheckNamespaces(void);

/*************************************************************************************************/
/**
 * \brief Checks escaped receiver routing through exact counted CastV2 fields.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Storage shall belong to the entry-owned context and remain disjoint from the model.
 *  - 002 Escaped NUL and a surrogate pair shall decode completely without a terminator.
 *  - 003 The encoded frame shall match independent bytes and retain its exact route on readback.
 *
 * \param[in,out] context   Required retained output and sentinel storage
 *
 * \retval None
 */
static FORCE_INLINE void CheckCountedRoute(TestContext_t* context);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Fixture counts are independent of any termination scan.
 */
static FORCE_INLINE Utl_Json_Text_t Text(const char_t* data, uint32_t byteCount)
{
    Utl_Json_Text_t text;

    text.data      = (const byte_t*)data;
    text.byteCount = byteCount;

    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All text is static fixture storage; serialization borrows it without allocation.
 */
static FORCE_INLINE void InitializeLoad(Utl_Chromecast_Load_t* load)
{
    LOGASSERT(NULL != load);

    load->requestId         = TEST_REQUEST_ID;
    load->sessionId         = Text("session", sizeof("session") - 1);
    load->autoplay          = TRUE;
    load->startMilliseconds = TEST_START_MILLISECONDS;
    load->contentId         = Text("http://host/movie", sizeof("http://host/movie") - 1);
    load->contentType       = Text("video/mp4", sizeof("video/mp4") - 1);
    load->stream            = UTL_CHROMECAST_STREAM_BUFFERED;
    load->title             = Text("Title", sizeof("Title") - 1);
    load->subtitle          = NULL;
    load->textTrackStyle    = NULL;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Exact-capacity output is not terminated. Every shorter capacity preserves all output bytes.
 */
static FORCE_INLINE void CheckKnownLoad(TestContext_t* context)
{
    Utl_Chromecast_Load_t load;
    uint32_t              expected;
    uint32_t              count;
    uint32_t              capacity;

    InitializeLoad(&load);
    expected = sizeof(TEST_BASELINE) - 1;
    count    = Utl_Chromecast_GetLoadByteCount(&load);
    LOGASSERT(expected == count);
    CRT_Memory_Set(context->output, TEST_SENTINEL, sizeof(context->output));
    count = Utl_Chromecast_WriteLoad(&load, context->output, expected);
    LOGASSERT(expected == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, TEST_BASELINE, expected));
    LOGASSERT(TEST_SENTINEL == (byte_t)context->output[expected]);
    CRT_Memory_Set(context->original, TEST_SENTINEL, sizeof(context->original));
    for (capacity = 0; capacity < expected; ++capacity)
    {
        CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
        count = Utl_Chromecast_WriteLoad(&load, context->output, capacity);
        LOGASSERT(0 == count);
        LOGASSERT(0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
    }
    LOGASSERT(0 == Utl_Chromecast_WriteLoad(&load, NULL, expected));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The independently authored wire fixture includes activeTrackIds and the existing outline style.
 */
static FORCE_INLINE void CheckSubtitle(TestContext_t* context)
{
    Utl_Chromecast_Load_t           load;
    Utl_Chromecast_Subtitle_t       track;
    Utl_Chromecast_TextTrackStyle_t style;
    uint32_t                        count;

    InitializeLoad(&load);
    load.sessionId                 = Text(TEST_SESSION, sizeof(TEST_SESSION) - 1);
    load.contentId                 = Text(TEST_CONTENT, sizeof(TEST_CONTENT) - 1);
    load.contentType               = Text(TEST_TYPE, sizeof(TEST_TYPE) - 1);
    load.autoplay                  = FALSE;
    load.stream                    = UTL_CHROMECAST_STREAM_LIVE;
    track.trackId                  = TEST_TRACK_ID;
    track.contentId                = Text(TEST_SUBTITLE_URL, sizeof(TEST_SUBTITLE_URL) - 1);
    track.name                     = Text("Captions", sizeof("Captions") - 1);
    track.language                 = Text("en", 2);
    style.foregroundColor          = TEST_OPAQUE_WHITE;
    style.backgroundColor          = 0x00000000;
    style.edge                     = UTL_CHROMECAST_TEXT_EDGE_OUTLINE;
    style.edgeColor                = TEST_OPAQUE_BLACK;
    style.fontScale.coefficient    = TEST_DEFAULT_FONT_SCALE;
    style.fontScale.fractionDigits = 1;
    load.textTrackStyle            = &style;
    load.subtitle                  = &track;
    count                          = Utl_Chromecast_WriteLoad(&load, context->output, sizeof(context->output));
    LOGASSERT(sizeof(TEST_SUBTITLE) - 1 == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, TEST_SUBTITLE, count));
    load.textTrackStyle = NULL;
    count               = Utl_Chromecast_WriteLoad(&load, context->output, sizeof(context->output));
    LOGASSERT(sizeof(TEST_SUBTITLE_NO_STYLE) - 1 == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, TEST_SUBTITLE_NO_STYLE, count));
    track.trackId = 0;
    CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
    count = Utl_Chromecast_WriteLoad(&load, context->output, sizeof(context->output));
    LOGASSERT(0 == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The input contains embedded U+0000 and all U+0001 through U+001F characters.
 *  Expected escape bytes are fixed independently of the serializer's digit algorithm.
 */
static FORCE_INLINE void CheckStrings(TestContext_t* context)
{
    Utl_Chromecast_Load_t load;
    uint32_t              count;
    uint32_t              prefix;

    InitializeLoad(&load);
    load.title = Text((const char_t*)TEST_STRING_INPUT, (sizeof(TEST_STRING_INPUT) - 1));
    count      = Utl_Chromecast_WriteLoad(&load, context->output, sizeof(context->output));
    prefix     = sizeof(TEST_BASELINE) - 1 - sizeof("Title\"}}") + 1;
    LOGASSERT(prefix + sizeof(TEST_STRING_EXPECTED) - 1 == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, TEST_BASELINE, prefix));
    LOGASSERT(0 ==
              CRT_Memory_Compare(context->output + prefix, TEST_STRING_EXPECTED, sizeof(TEST_STRING_EXPECTED) - 1));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Both the query and writer must reject the same invalid model.
 */
static FORCE_INLINE void CheckRejected(TestContext_t* context, Utl_Chromecast_Load_t* load)
{
    uint32_t count;

    CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
    count = Utl_Chromecast_GetLoadByteCount(load);
    LOGASSERT(0 == count);
    count = Utl_Chromecast_WriteLoad(load, context->output, sizeof(context->output));
    LOGASSERT(0 == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Invalid byte sequences include overlong NUL, an encoded surrogate,
 *  a truncated supplementary scalar and a scalar beyond Unicode's maximum.
 */
static FORCE_INLINE void CheckInvalidModels(TestContext_t* context)
{
    Utl_Chromecast_Load_t load;

    InitializeLoad(&load);
    load.requestId = 0;
    CheckRejected(context, &load);
    load.requestId = TEST_REQUEST_ID;
    load.title     = Text((const char_t*)TEST_OVERLONG, (sizeof(TEST_OVERLONG) - 1));
    CheckRejected(context, &load);
    load.title = Text((const char_t*)TEST_SURROGATE, (sizeof(TEST_SURROGATE) - 1));
    CheckRejected(context, &load);
    load.title = Text((const char_t*)TEST_TRUNCATED, (sizeof(TEST_TRUNCATED) - 1));
    CheckRejected(context, &load);
    load.title = Text((const char_t*)TEST_OUT_OF_RANGE, (sizeof(TEST_OUT_OF_RANGE) - 1));
    CheckRejected(context, &load);
    load.title = Text(NULL, 1);
    CheckRejected(context, &load);
    load.title     = Text(NULL, 0);
    load.contentId = Text(NULL, 0);
    CheckRejected(context, &load);
    InitializeLoad(&load);
    load.stream = UTL_CHROMECAST_STREAM_INVALID;
    CheckRejected(context, &load);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The maximum integer time must retain its .615 millisecond fraction.
 *  A zero readable bound reports missing termination without inspecting input.
 */
static FORCE_INLINE void CheckNumericLimits(TestContext_t* context)
{
    Utl_Chromecast_Load_t load;
    uint32_t              count;

    InitializeLoad(&load);
    load.requestId         = UINT32_MAX;
    load.startMilliseconds = UINT64_MAX;
    load.stream            = UTL_CHROMECAST_STREAM_NONE;
    count                  = Utl_Chromecast_WriteLoad(&load, context->output, sizeof(context->output));
    LOGASSERT(sizeof(TEST_NUMERIC) - 1 == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, TEST_NUMERIC, count));
    LOGASSERT(0 == Utl_Unicode_GetUtf8StringByteCount("", 1));
    LOGASSERT(UTL_UNICODE_NO_TERMINATOR == Utl_Unicode_GetUtf8StringByteCount("", 0));
    LOGASSERT(2 == Utl_Unicode_GetUtf8StringByteCount("ab", 3));
    LOGASSERT(UTL_UNICODE_NO_TERMINATOR == Utl_Unicode_GetUtf8StringByteCount("ab", 2));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Capacity failures must leave even bytes beyond the writable extent unchanged.
 */
static FORCE_INLINE void CheckStyle(TestContext_t*               context,
                                    const Utl_Chromecast_Load_t* load,
                                    const char_t*                expected,
                                    uint32_t                     byteCount)
{
    uint32_t count;
    uint32_t capacity;

    count = Utl_Chromecast_GetLoadByteCount(load);
    LOGASSERT(byteCount == count);
    count = Utl_Chromecast_WriteLoad(load, context->output, count);
    LOGASSERT(byteCount == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, expected, count));
    for (capacity = 0; capacity < count; ++capacity)
    {
        CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
        LOGASSERT(0 == Utl_Chromecast_WriteLoad(load, context->output, capacity));
        LOGASSERT(0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The same style model is exercised independently of subtitle presence. Explicit
 *  expected tokens ensure the writer cannot silently substitute the demo's theme.
 */
static FORCE_INLINE void CheckStyles(TestContext_t* context)
{
    Utl_Chromecast_Load_t           load;
    Utl_Chromecast_TextTrackStyle_t style;
    uint32_t                        count;

    InitializeLoad(&load);
    style.foregroundColor          = TEST_STYLE_FOREGROUND;
    style.backgroundColor          = TEST_STYLE_BACKGROUND;
    style.edge                     = UTL_CHROMECAST_TEXT_EDGE_NONE;
    style.edgeColor                = TEST_STYLE_EDGE_COLOR;
    style.fontScale.coefficient    = TEST_STYLE_FONT_SCALE;
    style.fontScale.fractionDigits = 2;
    load.textTrackStyle            = &style;
    style.edge                     = UTL_CHROMECAST_TEXT_EDGE_NONE;
    CheckStyle(context, &load, TEST_STYLE_NONE, sizeof(TEST_STYLE_NONE) - 1);
    style.edge = UTL_CHROMECAST_TEXT_EDGE_OUTLINE;
    CheckStyle(context, &load, TEST_STYLE_OUTLINE, sizeof(TEST_STYLE_OUTLINE) - 1);
    style.edge = UTL_CHROMECAST_TEXT_EDGE_DROP_SHADOW;
    CheckStyle(context, &load, TEST_STYLE_DROP_SHADOW, sizeof(TEST_STYLE_DROP_SHADOW) - 1);
    style.edge = UTL_CHROMECAST_TEXT_EDGE_RAISED;
    CheckStyle(context, &load, TEST_STYLE_RAISED, sizeof(TEST_STYLE_RAISED) - 1);
    style.edge = UTL_CHROMECAST_TEXT_EDGE_DEPRESSED;
    CheckStyle(context, &load, TEST_STYLE_DEPRESSED, sizeof(TEST_STYLE_DEPRESSED) - 1);
    style.edge = UTL_CHROMECAST_TEXT_EDGE_INVALID;
    CheckRejected(context, &load);
    style.edge                     = UTL_CHROMECAST_TEXT_EDGE_OUTLINE;
    style.fontScale.fractionDigits = UTL_JSON_MAX_FRACTION_DIGITS + 1;
    CheckRejected(context, &load);
    load.textTrackStyle = NULL;
    count               = Utl_Chromecast_WriteLoad(&load, context->output, sizeof(context->output));
    LOGASSERT(sizeof(TEST_BASELINE) - 1 == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, TEST_BASELINE, count));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Whole-buffer sentinels detect partial writes and unintended terminators.
 */
static FORCE_INLINE void CheckCommand(TestContext_t*                  context,
                                      const Utl_Chromecast_Command_t* command,
                                      const char_t*                   expected,
                                      uint32_t                        byteCount)
{
    uint32_t count;
    uint32_t capacity;

    LOGASSERT(byteCount < TEST_OUTPUT_CAPACITY);
    count = Utl_Chromecast_GetCommandByteCount(command);
    LOGASSERT(byteCount == count);
    CRT_Memory_Set(context->output, TEST_SENTINEL, sizeof(context->output));
    count = Utl_Chromecast_WriteCommand(command, context->output, byteCount);
    LOGASSERT(byteCount == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, expected, count));
    LOGASSERT(TEST_SENTINEL == context->output[byteCount]);
    CRT_Memory_Set(context->original, TEST_SENTINEL, sizeof(context->original));
    for (capacity = 0; capacity < byteCount; ++capacity)
    {
        CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
        count = Utl_Chromecast_WriteCommand(command, context->output, capacity);
        LOGASSERT(0 == count);
        LOGASSERT(0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
    }
    LOGASSERT(0 == Utl_Chromecast_WriteCommand(command, NULL, byteCount));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Failure must preserve every output byte, not just the nominal command extent.
 */
static FORCE_INLINE void CheckRejectedCommand(TestContext_t* context, const Utl_Chromecast_Command_t* command)
{
    uint32_t count;

    CRT_Memory_Set(context->output, TEST_SENTINEL, sizeof(context->output));
    CRT_Memory_Set(context->original, TEST_SENTINEL, sizeof(context->original));
    count = Utl_Chromecast_GetCommandByteCount(command);
    LOGASSERT(0 == count);
    count = Utl_Chromecast_WriteCommand(command, context->output, sizeof(context->output));
    LOGASSERT(0 == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Inactive fields start indeterminate. Commands initialize only their documented
 *  active fields, including a borrowed optional status ID and counted embedded NUL.
 */
static FORCE_INLINE void CheckCommands(TestContext_t* context)
{
    Utl_Chromecast_Command_t command;
    int64_t                  session_id;

    command.kind = UTL_CHROMECAST_COMMAND_CONNECT;
    CheckCommand(context, &command, TEST_COMMAND_CONNECT, sizeof(TEST_COMMAND_CONNECT) - 1);
    command.kind = UTL_CHROMECAST_COMMAND_PING;
    CheckCommand(context, &command, TEST_COMMAND_PING, sizeof(TEST_COMMAND_PING) - 1);
    command.kind = UTL_CHROMECAST_COMMAND_PONG;
    CheckCommand(context, &command, TEST_COMMAND_PONG, sizeof(TEST_COMMAND_PONG) - 1);
    command.kind               = UTL_CHROMECAST_COMMAND_LAUNCH;
    command.requestId          = TEST_REQUEST_ID;
    command.data.applicationId = Text(TEST_COMMAND_APP_ID, sizeof(TEST_COMMAND_APP_ID) - 1);
    CheckCommand(context, &command, TEST_COMMAND_LAUNCH, sizeof(TEST_COMMAND_LAUNCH) - 1);
    command.kind           = UTL_CHROMECAST_COMMAND_STOP_APPLICATION;
    command.data.sessionId = Text(TEST_SESSION, sizeof(TEST_SESSION) - 1);
    CheckCommand(context, &command, TEST_COMMAND_STOP_APPLICATION, sizeof(TEST_COMMAND_STOP_APPLICATION) - 1);
    command.kind                 = UTL_CHROMECAST_COMMAND_GET_MEDIA_STATUS;
    command.data.statusSessionId = NULL;
    CheckCommand(context, &command, TEST_COMMAND_ALL_STATUS, sizeof(TEST_COMMAND_ALL_STATUS) - 1);
    session_id                   = INT64_MIN;
    command.data.statusSessionId = &session_id;
    CheckCommand(context, &command, TEST_COMMAND_ONE_STATUS, sizeof(TEST_COMMAND_ONE_STATUS) - 1);
    command.kind                = UTL_CHROMECAST_COMMAND_PLAY;
    command.data.mediaSessionId = INT64_MAX;
    CheckCommand(context, &command, TEST_COMMAND_PLAY, sizeof(TEST_COMMAND_PLAY) - 1);
    command.kind                = UTL_CHROMECAST_COMMAND_PAUSE;
    command.data.mediaSessionId = 0;
    CheckCommand(context, &command, TEST_COMMAND_PAUSE, sizeof(TEST_COMMAND_PAUSE) - 1);
    command.kind                = UTL_CHROMECAST_COMMAND_STOP_MEDIA;
    command.data.mediaSessionId = -1;
    CheckCommand(context, &command, TEST_COMMAND_STOP_MEDIA, sizeof(TEST_COMMAND_STOP_MEDIA) - 1);
    command.kind                                 = UTL_CHROMECAST_COMMAND_SEEK;
    command.data.seek.mediaSessionId             = TEST_TRACK_ID;
    command.data.seek.currentTime.coefficient    = TEST_START_MILLISECONDS;
    command.data.seek.currentTime.fractionDigits = 3;
    command.data.seek.resume                     = UTL_CHROMECAST_RESUME_UNCHANGED;
    CheckCommand(context, &command, TEST_COMMAND_SEEK, sizeof(TEST_COMMAND_SEEK) - 1);
    command.data.seek.resume = UTL_CHROMECAST_RESUME_START;
    CheckCommand(context, &command, TEST_COMMAND_SEEK_START, sizeof(TEST_COMMAND_SEEK_START) - 1);
    command.data.seek.resume = UTL_CHROMECAST_RESUME_PAUSE;
    CheckCommand(context, &command, TEST_COMMAND_SEEK_PAUSE, sizeof(TEST_COMMAND_SEEK_PAUSE) - 1);
    command.requestId                            = UINT32_MAX;
    command.data.seek.currentTime.coefficient    = UINT64_MAX;
    command.data.seek.currentTime.fractionDigits = UTL_JSON_MAX_FRACTION_DIGITS;
    command.data.seek.resume                     = UTL_CHROMECAST_RESUME_UNCHANGED;
    CheckCommand(context, &command, TEST_COMMAND_SEEK_LIMIT, sizeof(TEST_COMMAND_SEEK_LIMIT) - 1);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Invalid model values remain ordinary external-data errors, never initializer misuse.
 */
static FORCE_INLINE void CheckInvalidCommands(TestContext_t* context)
{
    Utl_Chromecast_Command_t command;

    command.kind = UTL_CHROMECAST_COMMAND_INVALID;
    CheckRejectedCommand(context, &command);
    command.kind               = UTL_CHROMECAST_COMMAND_LAUNCH;
    command.requestId          = 0;
    command.data.applicationId = Text(TEST_COMMAND_APP_ID, sizeof(TEST_COMMAND_APP_ID) - 1);
    CheckRejectedCommand(context, &command);
    command.requestId          = TEST_REQUEST_ID;
    command.data.applicationId = Text(NULL, 0);
    CheckRejectedCommand(context, &command);
    command.data.applicationId = Text(NULL, 1);
    CheckRejectedCommand(context, &command);
    command.data.applicationId = Text(TEST_OVERLONG, sizeof(TEST_OVERLONG) - 1);
    CheckRejectedCommand(context, &command);
    command.kind           = UTL_CHROMECAST_COMMAND_STOP_APPLICATION;
    command.data.sessionId = Text(TEST_TRUNCATED, sizeof(TEST_TRUNCATED) - 1);
    CheckRejectedCommand(context, &command);
    command.data.sessionId = Text(NULL, 0);
    CheckRejectedCommand(context, &command);
    command.kind                                 = UTL_CHROMECAST_COMMAND_SEEK;
    command.data.seek.mediaSessionId             = TEST_TRACK_ID;
    command.data.seek.currentTime.coefficient    = 0;
    command.data.seek.currentTime.fractionDigits = UTL_JSON_MAX_FRACTION_DIGITS + 1;
    command.data.seek.resume                     = UTL_CHROMECAST_RESUME_UNCHANGED;
    CheckRejectedCommand(context, &command);
    command.data.seek.currentTime.fractionDigits = 0;
    command.data.seek.resume                     = UTL_CHROMECAST_RESUME_INVALID;
    CheckRejectedCommand(context, &command);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Expected spelling is independent of the implementation's namespace macros.
 */
static FORCE_INLINE void CheckNamespace(Utl_Chromecast_Namespace_t kind, Utl_Json_Text_t expected)
{
    Utl_Json_Text_t actual;

    actual = Utl_Chromecast_GetNamespace(kind);
    LOGASSERT(expected.byteCount == actual.byteCount);
    LOGASSERT(0 == CRT_Memory_Compare(actual.data, expected.data, expected.byteCount));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckNamespaces(void)
{
    Utl_Json_Text_t expected;
    Utl_Json_Text_t invalid;

    expected.data      = (const byte_t*)TEST_NAMESPACE_CONNECTION;
    expected.byteCount = sizeof(TEST_NAMESPACE_CONNECTION) - 1;
    CheckNamespace(UTL_CHROMECAST_NAMESPACE_CONNECTION, expected);
    expected.data      = (const byte_t*)TEST_NAMESPACE_HEARTBEAT;
    expected.byteCount = sizeof(TEST_NAMESPACE_HEARTBEAT) - 1;
    CheckNamespace(UTL_CHROMECAST_NAMESPACE_HEARTBEAT, expected);
    expected.data      = (const byte_t*)TEST_NAMESPACE_RECEIVER;
    expected.byteCount = sizeof(TEST_NAMESPACE_RECEIVER) - 1;
    CheckNamespace(UTL_CHROMECAST_NAMESPACE_RECEIVER, expected);
    expected.data      = (const byte_t*)TEST_NAMESPACE_MEDIA;
    expected.byteCount = sizeof(TEST_NAMESPACE_MEDIA) - 1;
    CheckNamespace(UTL_CHROMECAST_NAMESPACE_MEDIA, expected);
    invalid = Utl_Chromecast_GetNamespace(UTL_CHROMECAST_NAMESPACE_INVALID);
    LOGASSERT(NULL == invalid.data);
    LOGASSERT(0 == invalid.byteCount);
    LOGASSERT(UTL_CHROMECAST_NAMESPACE_INVALID == Utl_Chromecast_GetCommandNamespace(UTL_CHROMECAST_COMMAND_INVALID));
    LOGASSERT(UTL_CHROMECAST_NAMESPACE_CONNECTION ==
              Utl_Chromecast_GetCommandNamespace(UTL_CHROMECAST_COMMAND_CONNECT));
    LOGASSERT(UTL_CHROMECAST_NAMESPACE_HEARTBEAT == Utl_Chromecast_GetCommandNamespace(UTL_CHROMECAST_COMMAND_PING));
    LOGASSERT(UTL_CHROMECAST_NAMESPACE_HEARTBEAT == Utl_Chromecast_GetCommandNamespace(UTL_CHROMECAST_COMMAND_PONG));
    LOGASSERT(UTL_CHROMECAST_NAMESPACE_RECEIVER == Utl_Chromecast_GetCommandNamespace(UTL_CHROMECAST_COMMAND_LAUNCH));
    LOGASSERT(UTL_CHROMECAST_NAMESPACE_RECEIVER ==
              Utl_Chromecast_GetCommandNamespace(UTL_CHROMECAST_COMMAND_STOP_APPLICATION));
    LOGASSERT(UTL_CHROMECAST_NAMESPACE_MEDIA ==
              Utl_Chromecast_GetCommandNamespace(UTL_CHROMECAST_COMMAND_GET_MEDIA_STATUS));
    LOGASSERT(UTL_CHROMECAST_NAMESPACE_MEDIA == Utl_Chromecast_GetCommandNamespace(UTL_CHROMECAST_COMMAND_PLAY));
    LOGASSERT(UTL_CHROMECAST_NAMESPACE_MEDIA == Utl_Chromecast_GetCommandNamespace(UTL_CHROMECAST_COMMAND_PAUSE));
    LOGASSERT(UTL_CHROMECAST_NAMESPACE_MEDIA == Utl_Chromecast_GetCommandNamespace(UTL_CHROMECAST_COMMAND_STOP_MEDIA));
    LOGASSERT(UTL_CHROMECAST_NAMESPACE_MEDIA == Utl_Chromecast_GetCommandNamespace(UTL_CHROMECAST_COMMAND_SEEK));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Embedded U+0000 is a routing byte. Invalid UTF-8 immediately after the counted
 *  route catches accidental length scans without requiring an out-of-bounds read.
 */
static FORCE_INLINE void CheckCountedRoute(TestContext_t* context)
{
    Utl_Json_Text_t      encoded;
    uint32_t             decoded_count;
    byte_t               route[sizeof(TEST_ROUTE_DECODED)];
    Utl_CastV2_Message_t message;
    uint32_t             frame_count;
    Utl_CastV2_Message_t decoded;

    encoded.data      = (const byte_t*)TEST_ROUTE_ENCODED;
    encoded.byteCount = sizeof(TEST_ROUTE_ENCODED) - 1;
    decoded_count     = 0;
    LOGASSERT(Utl_Json_GetDecodedByteCount(&decoded_count, encoded));
    LOGASSERT(sizeof(TEST_ROUTE_DECODED) - 1 == decoded_count);
    CRT_Memory_Set(route, TEST_SENTINEL, sizeof(route));
    LOGASSERT(Utl_Json_ReadString(&decoded_count, route, sizeof(route) - 1, encoded));
    LOGASSERT(0 == CRT_Memory_Compare(route, TEST_ROUTE_DECODED, decoded_count));
    LOGASSERT(TEST_SENTINEL == route[sizeof(route) - 1]);
    message.source.data           = (const byte_t*)"s";
    message.source.byteCount      = 1;
    message.destination.data      = route;
    message.destination.byteCount = decoded_count;
    message.nameSpace.data        = (const byte_t*)"n";
    message.nameSpace.byteCount   = 1;
    message.payloadType           = UTL_CASTV2_STRING;
    message.payload.data          = (const byte_t*)"{}";
    message.payload.byteCount     = sizeof("{}") - 1;
    frame_count                   = Utl_CastV2_GetFrameByteCount(&message);
    LOGASSERT(sizeof(TEST_ROUTE_FRAME) - 1 == frame_count);
    CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
    LOGASSERT(frame_count == Utl_CastV2_WriteFrame(&message, context->output, frame_count));
    LOGASSERT(0 == CRT_Memory_Compare(context->output, TEST_ROUTE_FRAME, frame_count));
    LOGASSERT(TEST_SENTINEL == context->output[sizeof(TEST_ROUTE_FRAME) - 1]);
    LOGASSERT(Utl_CastV2_ReadMessage(&decoded,
              &context->output[UTL_CASTV2_HEADER_BYTES],
              frame_count - UTL_CASTV2_HEADER_BYTES));
    LOGASSERT(decoded_count == decoded.destination.byteCount);
    LOGASSERT(0 == CRT_Memory_Compare(decoded.destination.data, TEST_ROUTE_DECODED, decoded_count));
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
    TestContext_t context;

    CRT_Memory_Set(context.original, TEST_SENTINEL, sizeof(context.original));
    CheckKnownLoad(&context);
    CheckSubtitle(&context);
    CheckStrings(&context);
    CheckInvalidModels(&context);
    CheckNumericLimits(&context);
    CheckStyles(&context);
    CheckCommands(&context);
    CheckInvalidCommands(&context);
    CheckNamespaces();
    CheckCountedRoute(&context);

    return 0;
}
