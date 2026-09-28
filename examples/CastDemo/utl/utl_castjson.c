/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Cast sender LOAD profile used by CastDemo:
 * https://developers.google.com/cast/docs/media/messages#load
 * https://developers.google.com/cast/docs/reference/web_sender/chrome.cast.media.LoadRequest
 * JSON string and number productions:
 * https://www.rfc-editor.org/rfc/rfc8259.html#section-6
 * https://www.rfc-editor.org/rfc/rfc8259.html#section-7
 * This profile carries one optional active WebVTT subtitle with the demo's
 * outline style. Other message kinds, response parsing, media metadata kinds,
 * track kinds and arbitrary styling remain separate implementation work.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_castjson.h"
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

#define JSON_QUOTE                     ('"')
#define JSON_ESCAPE                    ('\\')
#define JSON_UNICODE_ESCAPE            ('u')
#define JSON_DECIMAL_POINT             ('.')
#define JSON_OBJECT_END                ('}')
#define JSON_ARRAY_END                 (']')
#define JSON_FIRST_UNESCAPED           (0x20)
#define JSON_HEX_BITS                  (4)
#define JSON_HEX_MASK                  (0x0F)
#define JSON_HEX_ALPHA_FIRST           ('A')
#define JSON_DECIMAL_BASE              (10)
#define JSON_FRACTION_HUNDREDS         (100)
#define JSON_UINT64_BYTE_COUNT         (20)
#define JSON_CONTROL_ESCAPE_BYTE_COUNT (6)
#define JSON_CONTROL_ESCAPE_LAST       (JSON_CONTROL_ESCAPE_BYTE_COUNT - 1)
#define JSON_TIME_FRACTION_BYTE_COUNT  (4)

#define JSON_LOAD_PREFIX     "{\"type\":\"LOAD\",\"requestId\":"
#define JSON_SESSION         ",\"sessionId\":"
#define JSON_AUTOPLAY        ",\"autoplay\":"
#define JSON_TRUE            "true"
#define JSON_FALSE           "false"
#define JSON_TIME            ",\"currentTime\":"
#define JSON_ACTIVE_TRACK    ",\"activeTrackIds\":["
#define JSON_MEDIA           ",\"media\":{\"contentId\":"
#define JSON_CONTENT_TYPE    ",\"contentType\":"
#define JSON_STREAM          ",\"streamType\":"
#define JSON_STREAM_NONE     "\"NONE\""
#define JSON_STREAM_BUFFERED "\"BUFFERED\""
#define JSON_STREAM_LIVE     "\"LIVE\""
#define JSON_TITLE           ",\"metadata\":{\"metadataType\":0,\"title\":"
#define JSON_TRACK_PREFIX    ",\"tracks\":[{\"trackId\":"
#define JSON_TRACK_NAME      ",\"type\":\"TEXT\",\"subtype\":\"SUBTITLES\",\"name\":"
#define JSON_TRACK_LANGUAGE  ",\"language\":"
#define JSON_TRACK_CONTENT   ",\"trackContentId\":"
#define JSON_TRACK_SUFFIX                                                                                              \
    ",\"trackContentType\":\"text/"                                                                                    \
    "vtt\"}],\"textTrackStyle\":{\"foregroundColor\":\"#FFFFFFFF\",\"backgroundColor\":\"#00000000\",\"edgeType\":"    \
    "\"OUTLINE\",\"edgeColor\":\"#000000FF\",\"fontScale\":1.0}"

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief A bounded output cursor, also used without output for exact preflight.
 */
typedef struct Writer_s
{
    char_t*  buffer;   ///< NULL during preflight; otherwise caller-owned output.
    uint32_t capacity; ///< Maximum permitted byte count.
    uint32_t used;     ///< Committed byte count, never greater than capacity.
    bool_t   valid;    ///< Sticky success flag for bounded append operations.
} Writer_t;

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
 * \brief Derives the uppercase JSON hexadecimal digit for one nibble.
 *
 * \par Requirements:
 *  - 001 A value outside the four-bit nibble domain shall assert.
 *  - 002 No lookup table, allocation or CRT operation shall be used.
 *
 * \param[in] value   Four-bit unsigned digit
 *
 * \retval    digit   ASCII 0 through 9 or A through F.
 */
static FORCE_INLINE char_t GetHexDigit(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Validates the complete counted UTF-8 representation.
 *
 * \par Requirements:
 *  - 001 Inputs shall obey the private serializer ownership and count invariants.
 *
 * \param[in] text   Borrowed text; NULL is allowed only for zero length
 *
 * \retval    TRUE    Valid scalar UTF-8.
 * \retval    FALSE   Invalid encoding or missing counted storage.
 */
static FORCE_INLINE bool_t IsValidText(Utl_CastJson_Text_t text);

/*************************************************************************************************/
/**
 * \brief Validates the supported LOAD model and optional subtitle.
 *
 * \par Requirements:
 *  - 001 Inputs shall obey the private serializer ownership and count invariants.
 *
 * \param[in] load   Required readable model
 *
 * \retval    TRUE    All modeled fields are serializable.
 * \retval    FALSE   Invalid text, missing required fields or unsupported stream kind.
 */
static FORCE_INLINE bool_t IsValidLoad(const Utl_CastJson_Load_t* load);

/*************************************************************************************************/
/**
 * \brief Appends a bounded fragment or counts it during preflight.
 *
 * \par Requirements:
 *  - 001 Inputs shall obey the private serializer ownership and count invariants.
 *
 * \param[in,out] writer      Initialized writer with a valid count invariant
 * \param[in]     data        Readable fragment, disjoint from non-NULL output
 * \param[in]     byteCount   Fragment bytes
 *
 * \retval        None   Failure is retained in the writer.
 */
static FORCE_INLINE void AppendBytes(Writer_t* writer, const char_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Appends one JSON syntax or string byte.
 *
 * \par Requirements:
 *  - 001 Inputs shall obey the private serializer ownership and count invariants.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     value    Byte to append
 *
 * \retval        None   Failure remains in the writer.
 */
static FORCE_INLINE void AppendByte(Writer_t* writer, char_t value);

/*************************************************************************************************/
/**
 * \brief Appends an unsigned JSON integer.
 *
 * \par Requirements:
 *  - 001 Inputs shall obey the private serializer ownership and count invariants.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     value    Nonnegative integer
 *
 * \retval        None   Failure remains in the writer.
 */
static FORCE_INLINE void AppendUint64(Writer_t* writer, uint64_t value);

/*************************************************************************************************/
/**
 * \brief Appends a quoted counted string with every mandatory JSON escape.
 *
 * \par Requirements:
 *  - 001 Inputs shall obey the private serializer ownership and count invariants.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     text     Already validated UTF-8, stable and disjoint from output
 *
 * \retval        None   Failure remains in the writer.
 */
static FORCE_INLINE void AppendText(Writer_t* writer, Utl_CastJson_Text_t text);

/*************************************************************************************************/
/**
 * \brief Appends fixed-point media time in JSON seconds.
 *
 * \par Requirements:
 *  - 001 Inputs shall obey the private serializer ownership and count invariants.
 *
 * \param[in,out] writer         Initialized writer
 * \param[in]     milliseconds   Nonnegative position
 *
 * \retval        None   Failure remains in the writer.
 */
static FORCE_INLINE void AppendTime(Writer_t* writer, uint64_t milliseconds);

/*************************************************************************************************/
/**
 * \brief Appends the selected WebVTT track and the existing demo text style.
 *
 * \par Requirements:
 *  - 001 Inputs shall obey the private serializer ownership and count invariants.
 *
 * \param[in,out] writer     Initialized writer
 * \param[in]     subtitle   Validated subtitle profile
 *
 * \retval        None   Failure remains in the writer.
 */
static FORCE_INLINE void AppendSubtitle(Writer_t* writer, const Utl_CastJson_Subtitle_t* subtitle);

/*************************************************************************************************/
/**
 * \brief Serializes the complete supported LOAD profile in deterministic field order.
 *
 * \par Requirements:
 *  - 001 Inputs shall obey the private serializer ownership and count invariants.
 *
 * \param[in,out] writer   Initialized counting or writing state
 * \param[in]     load     Validated stable model
 *
 * \retval        None   Complete count or sticky capacity failure remains in writer.
 */
static FORCE_INLINE void EncodeLoad(Writer_t* writer, const Utl_CastJson_Load_t* load);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Scalar arithmetic replaces a table lookup with an explicitly bounded domain.
 */
static FORCE_INLINE char_t GetHexDigit(uint32_t value)
{
    bool_t   decimal;
    uint32_t digit;

    LOGASSERT(JSON_HEX_MASK >= value);

    decimal = (JSON_DECIMAL_BASE > value);
    digit   = UTL_DECIMAL_CHAR_FIRST + value;
    if (!decimal)
    {
        digit = JSON_HEX_ALPHA_FIRST + (value - JSON_DECIMAL_BASE);
    }

    return (char_t)digit;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The Unicode boundary rejects overlong encodings, truncated sequences and surrogates.
 */
static FORCE_INLINE bool_t IsValidText(Utl_CastJson_Text_t text)
{
    return Utl_Unicode_IsValidUtf8((const byte_t*)text.data, text.byteCount);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The caller asserts object existence; field validation treats untrusted text as data.
 *  Counted empty optional text is valid. A present subtitle must have content and an ID.
 */
static FORCE_INLINE bool_t IsValidLoad(const Utl_CastJson_Load_t* load)
{
    bool_t valid_session;
    bool_t valid_content;
    bool_t valid_type;
    bool_t valid_title;
    bool_t valid_stream;
    bool_t has_session;
    bool_t has_content;
    bool_t has_type;
    bool_t valid_subtitle;
    bool_t valid_subtitle_id;
    bool_t valid_subtitle_name;
    bool_t valid_language;
    bool_t has_subtitle_content;
    bool_t has_track_id;
    bool_t is_valid;

    valid_session  = IsValidText(load->sessionId);
    valid_content  = IsValidText(load->contentId);
    valid_type     = IsValidText(load->contentType);
    valid_title    = IsValidText(load->title);
    valid_stream   = (UTL_CASTJSON_STREAM_NONE == load->stream) || (UTL_CASTJSON_STREAM_BUFFERED == load->stream) ||
                     (UTL_CASTJSON_STREAM_LIVE == load->stream);
    has_session    = (0 < load->sessionId.byteCount);
    has_content    = (0 < load->contentId.byteCount);
    has_type       = (0 < load->contentType.byteCount);
    valid_subtitle = TRUE;
    if (NULL != load->subtitle)
    {
        valid_subtitle_id    = IsValidText(load->subtitle->contentId);
        valid_subtitle_name  = IsValidText(load->subtitle->name);
        valid_language       = IsValidText(load->subtitle->language);
        has_subtitle_content = (0 < load->subtitle->contentId.byteCount);
        has_track_id         = (0 < load->subtitle->trackId);
        valid_subtitle =
            valid_subtitle_id && valid_subtitle_name && valid_language && has_subtitle_content && has_track_id;
    }
    is_valid = valid_session && valid_content && valid_type && valid_title && valid_stream && has_session &&
               has_content && has_type && valid_subtitle;

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Subtraction establishes capacity without overflowing an addition.
 *  A NULL output counts bytes without dereferencing or forming an output pointer.
 */
static FORCE_INLINE void AppendBytes(Writer_t* writer, const char_t* data, uint32_t byteCount)
{
    bool_t has_space;
    bool_t can_append;
    bool_t writing;

    LOGASSERT(writer->used <= writer->capacity);

    has_space  = (byteCount <= writer->capacity - writer->used);
    can_append = writer->valid && has_space;
    writing    = (NULL != writer->buffer);
    if (can_append)
    {
        if (writing)
        {
            CRT_Memory_Copy(writer->buffer + writer->used, data, byteCount);
        }
        writer->used += byteCount;
    }
    writer->valid = can_append;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The one-byte source remains alive through the synchronous copy.
 */
static FORCE_INLINE void AppendByte(Writer_t* writer, char_t value)
{
    AppendBytes(writer, &value, 1);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The fixed decimal buffer covers the entire uint64_t domain and is reused per value.
 */
static FORCE_INLINE void AppendUint64(Writer_t* writer, uint64_t value)
{
    uint32_t count;
    char_t   digits[JSON_UINT64_BYTE_COUNT];

    count = Utl_Decimal_WriteUint64(value, digits, sizeof(digits));
    LOGASSERT(0 < count);
    AppendBytes(writer, digits, count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Controls use six-byte Unicode escapes; quote and reverse solidus use two bytes.
 *  Valid non-ASCII UTF-8 passes through unchanged, including supplementary scalars.
 */
static FORCE_INLINE void AppendText(Writer_t* writer, Utl_CastJson_Text_t text)
{
    uint32_t index;
    byte_t   current;
    bool_t   short_escape;
    bool_t   control;
    char_t   escape[JSON_CONTROL_ESCAPE_BYTE_COUNT];

    AppendByte(writer, JSON_QUOTE);
    for (index = 0; (index < text.byteCount) && writer->valid; ++index)
    {
        current      = (byte_t)text.data[index];
        short_escape = (JSON_QUOTE == current) || (JSON_ESCAPE == current);
        control      = (current < JSON_FIRST_UNESCAPED);
        if (short_escape)
        {
            AppendByte(writer, JSON_ESCAPE);
            AppendByte(writer, (char_t)current);
        }
        else if (control)
        {
            escape[0]                        = JSON_ESCAPE;
            escape[1]                        = JSON_UNICODE_ESCAPE;
            escape[2]                        = UTL_DECIMAL_CHAR_FIRST;
            escape[3]                        = UTL_DECIMAL_CHAR_FIRST;
            escape[4]                        = GetHexDigit(current >> JSON_HEX_BITS);
            escape[JSON_CONTROL_ESCAPE_LAST] = GetHexDigit(current & JSON_HEX_MASK);
            AppendBytes(writer, escape, sizeof(escape));
        }
        else
        {
            AppendByte(writer, (char_t)current);
        }
    }
    AppendByte(writer, JSON_QUOTE);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Integer division preserves all millisecond digits without locale, rounding or CRT.
 */
static FORCE_INLINE void AppendTime(Writer_t* writer, uint64_t milliseconds)
{
    uint64_t seconds;
    uint32_t fraction;
    uint32_t hundreds;
    uint32_t tens;
    uint32_t units;
    char_t   digits[JSON_TIME_FRACTION_BYTE_COUNT];

    seconds   = milliseconds / UTL_CASTJSON_MILLISECONDS_PER_SECOND;
    fraction  = (uint32_t)(milliseconds % UTL_CASTJSON_MILLISECONDS_PER_SECOND);
    hundreds  = fraction / JSON_FRACTION_HUNDREDS;
    tens      = (fraction / JSON_DECIMAL_BASE) % JSON_DECIMAL_BASE;
    units     = fraction % JSON_DECIMAL_BASE;
    digits[0] = JSON_DECIMAL_POINT;
    digits[1] = (char_t)(UTL_DECIMAL_CHAR_FIRST + hundreds);
    digits[2] = (char_t)(UTL_DECIMAL_CHAR_FIRST + tens);
    digits[3] = (char_t)(UTL_DECIMAL_CHAR_FIRST + units);
    AppendUint64(writer, seconds);
    AppendBytes(writer, digits, sizeof(digits));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  This explicitly documented application profile preserves the previous outline style.
 *  It does not claim arbitrary track-kind or TextTrackStyle coverage.
 */
static FORCE_INLINE void AppendSubtitle(Writer_t* writer, const Utl_CastJson_Subtitle_t* subtitle)
{
    AppendBytes(writer, JSON_TRACK_PREFIX, sizeof(JSON_TRACK_PREFIX) - 1);
    AppendUint64(writer, subtitle->trackId);
    AppendBytes(writer, JSON_TRACK_NAME, sizeof(JSON_TRACK_NAME) - 1);
    AppendText(writer, subtitle->name);
    AppendBytes(writer, JSON_TRACK_LANGUAGE, sizeof(JSON_TRACK_LANGUAGE) - 1);
    AppendText(writer, subtitle->language);
    AppendBytes(writer, JSON_TRACK_CONTENT, sizeof(JSON_TRACK_CONTENT) - 1);
    AppendText(writer, subtitle->contentId);
    AppendBytes(writer, JSON_TRACK_SUFFIX, sizeof(JSON_TRACK_SUFFIX) - 1);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Both passes execute the same serializer; output space is checked before the writing pass.
 *  Every caller-controlled text field goes through the same UTF-8 string encoder.
 */
static FORCE_INLINE void EncodeLoad(Writer_t* writer, const Utl_CastJson_Load_t* load)
{
    bool_t has_subtitle;
    bool_t is_buffered;
    bool_t is_live;

    has_subtitle = (NULL != load->subtitle);
    AppendBytes(writer, JSON_LOAD_PREFIX, sizeof(JSON_LOAD_PREFIX) - 1);
    AppendUint64(writer, load->requestId);
    AppendBytes(writer, JSON_SESSION, sizeof(JSON_SESSION) - 1);
    AppendText(writer, load->sessionId);
    AppendBytes(writer, JSON_AUTOPLAY, sizeof(JSON_AUTOPLAY) - 1);
    if (load->autoplay)
    {
        AppendBytes(writer, JSON_TRUE, sizeof(JSON_TRUE) - 1);
    }
    else
    {
        AppendBytes(writer, JSON_FALSE, sizeof(JSON_FALSE) - 1);
    }
    AppendBytes(writer, JSON_TIME, sizeof(JSON_TIME) - 1);
    AppendTime(writer, load->startMilliseconds);
    if (has_subtitle)
    {
        AppendBytes(writer, JSON_ACTIVE_TRACK, sizeof(JSON_ACTIVE_TRACK) - 1);
        AppendUint64(writer, load->subtitle->trackId);
        AppendByte(writer, JSON_ARRAY_END);
    }
    AppendBytes(writer, JSON_MEDIA, sizeof(JSON_MEDIA) - 1);
    AppendText(writer, load->contentId);
    AppendBytes(writer, JSON_CONTENT_TYPE, sizeof(JSON_CONTENT_TYPE) - 1);
    AppendText(writer, load->contentType);
    AppendBytes(writer, JSON_STREAM, sizeof(JSON_STREAM) - 1);
    is_buffered = (UTL_CASTJSON_STREAM_BUFFERED == load->stream);
    is_live     = (UTL_CASTJSON_STREAM_LIVE == load->stream);
    if (is_buffered)
    {
        AppendBytes(writer, JSON_STREAM_BUFFERED, sizeof(JSON_STREAM_BUFFERED) - 1);
    }
    else if (is_live)
    {
        AppendBytes(writer, JSON_STREAM_LIVE, sizeof(JSON_STREAM_LIVE) - 1);
    }
    else
    {
        LOGASSERT(UTL_CASTJSON_STREAM_NONE == load->stream);
        AppendBytes(writer, JSON_STREAM_NONE, sizeof(JSON_STREAM_NONE) - 1);
    }
    AppendBytes(writer, JSON_TITLE, sizeof(JSON_TITLE) - 1);
    AppendText(writer, load->title);
    AppendByte(writer, JSON_OBJECT_END);
    if (has_subtitle)
    {
        AppendSubtitle(writer, load->subtitle);
    }
    AppendByte(writer, JSON_OBJECT_END);
    AppendByte(writer, JSON_OBJECT_END);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Validation precedes all counting. A missing model is programmer misuse.
 *  The same bounded serializer is used for counting and for writing.
 */
uint32_t Utl_CastJson_GetLoadByteCount(const Utl_CastJson_Load_t* load)
{
    bool_t   is_valid;
    uint32_t count;
    Writer_t writer;

    LOGASSERT(NULL != load);

    is_valid = IsValidLoad(load);
    count    = 0;
    if (is_valid)
    {
        writer.buffer   = NULL;
        writer.capacity = UINT32_MAX;
        writer.used     = 0;
        writer.valid    = TRUE;
        EncodeLoad(&writer, load);
        if (writer.valid)
        {
            count = writer.used;
        }
    }

    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A failed preflight or capacity check preserves every byte of caller output.
 *  Successful writing cannot fail while the immutable, disjoint input contract holds.
 */
uint32_t Utl_CastJson_WriteLoad(const Utl_CastJson_Load_t* load, char_t* buffer, uint32_t capacity)
{
    uint32_t count;
    bool_t   has_output;
    bool_t   has_count;
    bool_t   has_capacity;
    bool_t   can_write;
    uint32_t written;
    Writer_t writer;

    count        = Utl_CastJson_GetLoadByteCount(load);
    has_output   = (NULL != buffer);
    has_count    = (0 < count);
    has_capacity = (count <= capacity);
    can_write    = has_output && has_count && has_capacity;
    written      = 0;
    if (can_write)
    {
        writer.buffer   = buffer;
        writer.capacity = capacity;
        writer.used     = 0;
        writer.valid    = TRUE;
        EncodeLoad(&writer, load);
        LOGASSERT(writer.valid);
        LOGASSERT(count == writer.used);
        written = count;
    }

    return written;
}
