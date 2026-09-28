/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Google Cast media message schemas:
 * https://developers.google.com/cast/docs/media/messages
 * Control-channel fields follow Chromium's cast_message_util reference:
 * https://chromium.googlesource.com/chromium/src/+/main/components/media_router/common/providers/cast/channel/cast_message_util.cc
 * Typed protocol fields are composed through the general utl_json writer.
 * Sender policy and chosen subtitle appearance belong to the caller.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_chromecast.h"
#include "utl/utl_json.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define LOAD_FRAME_CAPACITY         (4)
#define LOAD_TIME_FRACTION_DIGITS   (3)
#define COMMAND_FRAME_CAPACITY      (1)
#define CHROMECAST_METADATA_GENERIC (0) ///< Media GenericMediaMetadata.metadataType.

/* Cast TextTrackStyle colors use CSS eight-digit RRGGBBAA notation. */
#define COLOR_PREFIX         ('#')
#define COLOR_DIGIT_ZERO     ('0')
#define COLOR_DIGIT_A        ('A')
#define COLOR_HEX_DIGITS     (8)
#define COLOR_TEXT_BYTES     (COLOR_HEX_DIGITS + 1)
#define COLOR_BITS_PER_DIGIT (4)
#define COLOR_DIGIT_MASK     (0x0F)
#define COLOR_DECIMAL_DIGITS (10)

/* Fixed RRGGBBAA positions, from the low alpha nibble to the high red nibble. */
#define COLOR_ALPHA_HIGH_SHIFT (COLOR_BITS_PER_DIGIT)
#define COLOR_BLUE_LOW_SHIFT   (COLOR_ALPHA_HIGH_SHIFT + COLOR_BITS_PER_DIGIT)
#define COLOR_BLUE_HIGH_SHIFT  (COLOR_BLUE_LOW_SHIFT + COLOR_BITS_PER_DIGIT)
#define COLOR_GREEN_LOW_SHIFT  (COLOR_BLUE_HIGH_SHIFT + COLOR_BITS_PER_DIGIT)
#define COLOR_GREEN_HIGH_SHIFT (COLOR_GREEN_LOW_SHIFT + COLOR_BITS_PER_DIGIT)
#define COLOR_RED_LOW_SHIFT    (COLOR_GREEN_HIGH_SHIFT + COLOR_BITS_PER_DIGIT)
#define COLOR_RED_HIGH_SHIFT   (COLOR_RED_LOW_SHIFT + COLOR_BITS_PER_DIGIT)

#define COLOR_PREFIX_OFFSET     (0)
#define COLOR_RED_HIGH_OFFSET   (COLOR_PREFIX_OFFSET + 1)
#define COLOR_RED_LOW_OFFSET    (COLOR_RED_HIGH_OFFSET + 1)
#define COLOR_GREEN_HIGH_OFFSET (COLOR_RED_LOW_OFFSET + 1)
#define COLOR_GREEN_LOW_OFFSET  (COLOR_GREEN_HIGH_OFFSET + 1)
#define COLOR_BLUE_HIGH_OFFSET  (COLOR_GREEN_LOW_OFFSET + 1)
#define COLOR_BLUE_LOW_OFFSET   (COLOR_BLUE_HIGH_OFFSET + 1)
#define COLOR_ALPHA_HIGH_OFFSET (COLOR_BLUE_LOW_OFFSET + 1)
#define COLOR_ALPHA_LOW_OFFSET  (COLOR_ALPHA_HIGH_OFFSET + 1)

#define CHROMECAST_TOKEN_NONE               ("NONE")
#define CHROMECAST_TOKEN_DROP_SHADOW        ("DROP_SHADOW")
#define CHROMECAST_TOKEN_OUTLINE            ("OUTLINE")
#define CHROMECAST_TOKEN_RAISED             ("RAISED")
#define CHROMECAST_TOKEN_DEPRESSED          ("DEPRESSED")
#define CHROMECAST_TOKEN_TRACKS             ("tracks")
#define CHROMECAST_TOKEN_TRACK_ID           ("trackId")
#define CHROMECAST_TOKEN_TYPE               ("type")
#define CHROMECAST_TOKEN_TEXT               ("TEXT")
#define CHROMECAST_TOKEN_SUBTYPE            ("subtype")
#define CHROMECAST_TOKEN_SUBTITLES          ("SUBTITLES")
#define CHROMECAST_TOKEN_NAME               ("name")
#define CHROMECAST_TOKEN_LANGUAGE           ("language")
#define CHROMECAST_TOKEN_TRACK_CONTENT_ID   ("trackContentId")
#define CHROMECAST_TOKEN_TRACK_CONTENT_TYPE ("trackContentType")
#define CHROMECAST_TOKEN_TEXT_VTT           ("text/vtt")
#define CHROMECAST_TOKEN_TEXT_TRACK_STYLE   ("textTrackStyle")
#define CHROMECAST_TOKEN_FOREGROUND_COLOR   ("foregroundColor")
#define CHROMECAST_TOKEN_BACKGROUND_COLOR   ("backgroundColor")
#define CHROMECAST_TOKEN_EDGE_TYPE          ("edgeType")
#define CHROMECAST_TOKEN_EDGE_COLOR         ("edgeColor")
#define CHROMECAST_TOKEN_FONT_SCALE         ("fontScale")
#define CHROMECAST_TOKEN_LOAD               ("LOAD")
#define CHROMECAST_TOKEN_REQUEST_ID         ("requestId")
#define CHROMECAST_TOKEN_SESSION_ID         ("sessionId")
#define CHROMECAST_TOKEN_AUTOPLAY           ("autoplay")
#define CHROMECAST_TOKEN_CURRENT_TIME       ("currentTime")
#define CHROMECAST_TOKEN_ACTIVE_TRACK_IDS   ("activeTrackIds")
#define CHROMECAST_TOKEN_MEDIA              ("media")
#define CHROMECAST_TOKEN_CONTENT_ID         ("contentId")
#define CHROMECAST_TOKEN_CONTENT_TYPE       ("contentType")
#define CHROMECAST_TOKEN_STREAM_TYPE        ("streamType")
#define CHROMECAST_TOKEN_BUFFERED           ("BUFFERED")
#define CHROMECAST_TOKEN_LIVE               ("LIVE")
#define CHROMECAST_TOKEN_METADATA           ("metadata")
#define CHROMECAST_TOKEN_METADATA_TYPE      ("metadataType")
#define CHROMECAST_TOKEN_TITLE              ("title")
#define CHROMECAST_TOKEN_CONNECT            ("CONNECT")
#define CHROMECAST_TOKEN_PING               ("PING")
#define CHROMECAST_TOKEN_PONG               ("PONG")
#define CHROMECAST_TOKEN_LAUNCH             ("LAUNCH")
#define CHROMECAST_TOKEN_STOP               ("STOP")
#define CHROMECAST_TOKEN_GET_STATUS         ("GET_STATUS")
#define CHROMECAST_TOKEN_PLAY               ("PLAY")
#define CHROMECAST_TOKEN_PAUSE              ("PAUSE")
#define CHROMECAST_TOKEN_SEEK               ("SEEK")
#define CHROMECAST_TOKEN_APP_ID             ("appId")
#define CHROMECAST_TOKEN_MEDIA_SESSION_ID   ("mediaSessionId")
#define CHROMECAST_TOKEN_RESUME_STATE       ("resumeState")
#define CHROMECAST_TOKEN_PLAYBACK_START     ("PLAYBACK_START")
#define CHROMECAST_TOKEN_PLAYBACK_PAUSE     ("PLAYBACK_PAUSE")
#define CHROMECAST_NAMESPACE_CONNECTION     ("urn:x-cast:com.google.cast.tp.connection")
#define CHROMECAST_NAMESPACE_HEARTBEAT      ("urn:x-cast:com.google.cast.tp.heartbeat")
#define CHROMECAST_NAMESPACE_RECEIVER       ("urn:x-cast:com.google.cast.receiver")
#define CHROMECAST_NAMESPACE_MEDIA          ("urn:x-cast:com.google.cast.media")

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

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
 * \brief Builds a counted view of an application token.
 *
 * \par Requirements:
 *  - 001 Cast fields shall remain borrowed, immutable and disjoint from output during both passes.
 *
 * \param[in] data    Application token
 * \param[in] bytes   Token bytes
 *
 * \retval text   Borrowed view.
 */
static FORCE_INLINE Utl_Json_Text_t Text(const char_t* data, uint32_t bytes);

/*************************************************************************************************/
/**
 * \brief Derives the protocol token for a text edge treatment.
 *
 * \param[in] edge   External edge value to classify
 *
 * \retval    text    Borrowed protocol token, or an empty span for an unknown value.
 */
static FORCE_INLINE Utl_Json_Text_t GetEdgeToken(Utl_Chromecast_TextEdge_t edge);

/*************************************************************************************************/
/**
 * \brief Derives one hexadecimal color digit from the low four bits.
 *
 * \param[in] value   Packed color bits shifted to the desired digit
 *
 * \retval    digit   Uppercase hexadecimal character.
 */
static FORCE_INLINE byte_t GetColorDigit(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Checks required storage without interpreting JSON syntax.
 *
 * \par Requirements:
 *  - 001 Cast fields shall remain borrowed, immutable and disjoint from output during both passes.
 *
 * \param[in] text   Application field
 *
 * \retval TRUE   Counted storage is present.
 * \retval FALSE   Nonempty field lacks storage.
 */
static FORCE_INLINE bool_t HasText(Utl_Json_Text_t text);

/*************************************************************************************************/
/**
 * \brief Checks Cast LOAD field semantics before JSON serialization.
 *
 * \par Requirements:
 *  - 001 Cast fields shall remain borrowed, immutable and disjoint from output during both passes.
 *
 * \param[in] load   Required model
 *
 * \retval TRUE   Cast field requirements are met.
 * \retval FALSE   Invalid Cast model.
 */
static FORCE_INLINE bool_t IsValidLoad(const Utl_Chromecast_Load_t* load);

/*************************************************************************************************/
/**
 * \brief Derives the wire type for a command schema.
 *
 * \par Requirements:
 *  - 001 Unknown kinds shall yield an empty view without indexing a token table.
 *
 * \param[in] kind   Command schema to classify
 *
 * \retval    text   Borrowed wire token; empty for an unsupported kind.
 */
static FORCE_INLINE Utl_Json_Text_t GetCommandToken(Utl_Chromecast_CommandKind_t kind);

/*************************************************************************************************/
/**
 * \brief Determines whether a command contains only its type field.
 *
 * \par Requirements:
 *  - 001 Only CONNECT, PING and PONG shall have no correlation or data fields.
 *
 * \param[in] kind   Command schema
 *
 * \retval    TRUE    A type-only schema.
 * \retval    FALSE   Correlated or unsupported schema.
 */
static FORCE_INLINE bool_t IsTypeOnly(Utl_Chromecast_CommandKind_t kind);

/*************************************************************************************************/
/**
 * \brief Validates active command fields before a counting pass.
 *
 * \par Requirements:
 *  - 001 Required model storage and every field selected by kind shall be initialized.
 *  - 002 Only active fields shall be read; JSON text validation belongs to the counting writer.
 *
 * \param[in] command   Stable required model
 *
 * \retval    TRUE    Selected schema and domain fields are valid.
 * \retval    FALSE   Unsupported schema, missing fields or invalid decimal/resume domain.
 */
static FORCE_INLINE bool_t IsValidCommand(const Utl_Chromecast_Command_t* command);

/*************************************************************************************************/
/**
 * \brief Supplies an application field name to the JSON writer.
 *
 * \par Requirements:
 *  - 001 Cast fields shall remain borrowed, immutable and disjoint from output during both passes.
 *
 * \param[in,out] writer   Shared writer
 * \param[in]     name     Cast field name
 * \param[in]     bytes    Counted name bytes
 *
 * \retval None   JSON writer owns grammar and status.
 */
static FORCE_INLINE void WriteName(Utl_Json_Writer_t* writer, const char_t* name, uint32_t bytes);

/*************************************************************************************************/
/**
 * \brief Supplies an application string token to the JSON writer.
 *
 * \par Requirements:
 *  - 001 Cast fields shall remain borrowed, immutable and disjoint from output during both passes.
 *
 * \param[in,out] writer   Shared writer
 * \param[in]     value    Cast token
 * \param[in]     bytes    Counted token bytes
 *
 * \retval None   JSON writer owns escaping and status.
 */
static FORCE_INLINE void WriteToken(Utl_Json_Writer_t* writer, const char_t* value, uint32_t bytes);

/*************************************************************************************************/
/**
 * \brief Emits the protocol's eight-digit RGBA color notation through the JSON writer.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     rgba     Packed RRGGBBAA value
 *
 * \retval        None   No result value.
 */
static FORCE_INLINE void WriteColor(Utl_Json_Writer_t* writer, uint32_t rgba);

/*************************************************************************************************/
/**
 * \brief Composes the selected Cast subtitle and application outline style.
 *
 * \par Requirements:
 *  - 001 Cast fields shall remain borrowed, immutable and disjoint from output during both passes.
 *
 * \param[in,out] writer     Shared JSON writer
 * \param[in]     subtitle   Valid selected track
 *
 * \retval None   JSON writer retains final status.
 */
static FORCE_INLINE void WriteSubtitle(Utl_Json_Writer_t* writer, const Utl_Chromecast_Subtitle_t* subtitle);

/*************************************************************************************************/
/**
 * \brief Emits caller-selected text track styling.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     style    Preflighted borrowed style model
 *
 * \retval        None   No result value.
 */
static FORCE_INLINE void WriteTextTrackStyle(Utl_Json_Writer_t* writer, const Utl_Chromecast_TextTrackStyle_t* style);

/*************************************************************************************************/
/**
 * \brief Composes only the Cast LOAD schema through general JSON operations.
 *
 * \par Requirements:
 *  - 001 Cast fields shall remain borrowed, immutable and disjoint from output during both passes.
 *
 * \param[in,out] writer   Counting or output writer
 * \param[in]     load     Stable Cast model
 *
 * \retval None   Completion is queried from the JSON writer.
 */
static FORCE_INLINE void EncodeLoad(Utl_Json_Writer_t* writer, const Utl_Chromecast_Load_t* load);

/*************************************************************************************************/
/**
 * \brief Composes the active SEEK fields after the common request fields.
 *
 * \par Requirements:
 *  - 001 The seek model shall have a valid decimal scale and resume value.
 *  - 002 UNCHANGED shall omit resumeState; START and PAUSE shall emit their respective tokens.
 *
 * \param[in,out] writer   Initialized writer inside a command object
 * \param[in]     seek     Stable validated model
 *
 * \retval        None   Writer records completion or failure.
 */
static FORCE_INLINE void WriteSeek(Utl_Json_Writer_t* writer, const Utl_Chromecast_Seek_t* seek);

/*************************************************************************************************/
/**
 * \brief Composes the fields selected by a preflighted command model.
 *
 * \par Requirements:
 *  - 001 The model shall satisfy IsValidCommand and remain unchanged through serialization.
 *  - 002 Only selected fields shall be read; writer failures shall remain latched.
 *
 * \param[in,out] writer    Initialized exclusive writer
 * \param[in]     command   Stable validated command model
 *
 * \retval        None   Writer records completion or failure.
 */
static FORCE_INLINE void EncodeCommand(Utl_Json_Writer_t* writer, const Utl_Chromecast_Command_t* command);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Json_Text_t Text(const char_t* data, uint32_t bytes)
{
    Utl_Json_Text_t text;
    text.data      = (const byte_t*)data;
    text.byteCount = bytes;
    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  An empty span represents an unknown edge kind and is rejected before writing.
 */
static FORCE_INLINE Utl_Json_Text_t GetEdgeToken(Utl_Chromecast_TextEdge_t edge)
{
    bool_t          is_none;
    bool_t          is_outline;
    bool_t          is_shadow;
    bool_t          is_raised;
    bool_t          is_depressed;
    Utl_Json_Text_t token;

    is_none         = (UTL_CHROMECAST_TEXT_EDGE_NONE == edge);
    is_outline      = (UTL_CHROMECAST_TEXT_EDGE_OUTLINE == edge);
    is_shadow       = (UTL_CHROMECAST_TEXT_EDGE_DROP_SHADOW == edge);
    is_raised       = (UTL_CHROMECAST_TEXT_EDGE_RAISED == edge);
    is_depressed    = (UTL_CHROMECAST_TEXT_EDGE_DEPRESSED == edge);
    token.data      = NULL;
    token.byteCount = 0;
    if (is_none)
    {
        token = Text(CHROMECAST_TOKEN_NONE, sizeof(CHROMECAST_TOKEN_NONE) - 1);
    }
    else if (is_outline)
    {
        token = Text(CHROMECAST_TOKEN_OUTLINE, sizeof(CHROMECAST_TOKEN_OUTLINE) - 1);
    }
    else if (is_shadow)
    {
        token = Text(CHROMECAST_TOKEN_DROP_SHADOW, sizeof(CHROMECAST_TOKEN_DROP_SHADOW) - 1);
    }
    else if (is_raised)
    {
        token = Text(CHROMECAST_TOKEN_RAISED, sizeof(CHROMECAST_TOKEN_RAISED) - 1);
    }
    else if (is_depressed)
    {
        token = Text(CHROMECAST_TOKEN_DEPRESSED, sizeof(CHROMECAST_TOKEN_DEPRESSED) - 1);
    }
    else
    {
        /* Unknown external enum values retain the invalid empty token. */
    }
    return token;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Masking establishes the digit domain before either character derivation.
 */
static FORCE_INLINE byte_t GetColorDigit(uint32_t value)
{
    uint32_t digit;
    bool_t   is_decimal;
    byte_t   character;

    digit      = value & COLOR_DIGIT_MASK;
    is_decimal = (digit < COLOR_DECIMAL_DIGITS);
    if (is_decimal)
    {
        character = (byte_t)(COLOR_DIGIT_ZERO + digit);
    }
    else
    {
        character = (byte_t)(COLOR_DIGIT_A + digit - COLOR_DECIMAL_DIGITS);
    }
    return character;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t HasText(Utl_Json_Text_t text)
{
    bool_t is_empty;
    bool_t has_storage;
    bool_t has_text;

    is_empty    = (0 == text.byteCount);
    has_storage = (NULL != text.data);
    has_text    = is_empty || has_storage;
    return has_text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsValidLoad(const Utl_Chromecast_Load_t* load)
{
    bool_t          has_request;
    bool_t          valid_session;
    bool_t          valid_content;
    bool_t          valid_type;
    bool_t          valid_title;
    bool_t          is_none;
    bool_t          is_buffered;
    bool_t          is_live;
    bool_t          valid_stream;
    bool_t          has_style;
    bool_t          valid_style;
    Utl_Json_Text_t edge;
    bool_t          valid_edge;
    bool_t          valid_scale;
    bool_t          has_session;
    bool_t          has_content;
    bool_t          has_type;
    bool_t          valid_subtitle;
    bool_t          valid_subtitle_id;
    bool_t          valid_subtitle_name;
    bool_t          valid_language;
    bool_t          has_subtitle_content;
    bool_t          has_track_id;
    bool_t          is_valid;

    has_request   = (0 < load->requestId);
    valid_session = HasText(load->sessionId);
    valid_content = HasText(load->contentId);
    valid_type    = HasText(load->contentType);
    valid_title   = HasText(load->title);
    is_none       = (UTL_CHROMECAST_STREAM_NONE == load->stream);
    is_buffered   = (UTL_CHROMECAST_STREAM_BUFFERED == load->stream);
    is_live       = (UTL_CHROMECAST_STREAM_LIVE == load->stream);
    valid_stream  = is_none || is_buffered || is_live;
    has_style     = (NULL != load->textTrackStyle);
    valid_style   = TRUE;
    if (has_style)
    {
        edge        = GetEdgeToken(load->textTrackStyle->edge);
        valid_edge  = (0 < edge.byteCount);
        valid_scale = (load->textTrackStyle->fontScale.fractionDigits <= UTL_JSON_MAX_FRACTION_DIGITS);
        valid_style = valid_edge && valid_scale;
    }
    has_session    = (0 < load->sessionId.byteCount);
    has_content    = (0 < load->contentId.byteCount);
    has_type       = (0 < load->contentType.byteCount);
    valid_subtitle = TRUE;
    if (NULL != load->subtitle)
    {
        valid_subtitle_id    = HasText(load->subtitle->contentId);
        valid_subtitle_name  = HasText(load->subtitle->name);
        valid_language       = HasText(load->subtitle->language);
        has_subtitle_content = (0 < load->subtitle->contentId.byteCount);
        has_track_id         = (0 < load->subtitle->trackId);
        valid_subtitle =
            valid_subtitle_id && valid_subtitle_name && valid_language && has_subtitle_content && has_track_id;
    }
    is_valid = has_request && valid_session && valid_content && valid_type && valid_title && valid_stream &&
               has_session && has_content && has_type && valid_subtitle && valid_style;

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Json_Text_t GetCommandToken(Utl_Chromecast_CommandKind_t kind)
{
    bool_t          is_connect;
    bool_t          is_ping;
    bool_t          is_pong;
    bool_t          is_launch;
    bool_t          is_app_stop;
    bool_t          is_media_stop;
    bool_t          is_stop;
    bool_t          is_status;
    bool_t          is_play;
    bool_t          is_pause;
    bool_t          is_seek;
    Utl_Json_Text_t token;

    is_connect      = (UTL_CHROMECAST_COMMAND_CONNECT == kind);
    is_ping         = (UTL_CHROMECAST_COMMAND_PING == kind);
    is_pong         = (UTL_CHROMECAST_COMMAND_PONG == kind);
    is_launch       = (UTL_CHROMECAST_COMMAND_LAUNCH == kind);
    is_app_stop     = (UTL_CHROMECAST_COMMAND_STOP_APPLICATION == kind);
    is_media_stop   = (UTL_CHROMECAST_COMMAND_STOP_MEDIA == kind);
    is_stop         = is_app_stop || is_media_stop;
    is_status       = (UTL_CHROMECAST_COMMAND_GET_MEDIA_STATUS == kind);
    is_play         = (UTL_CHROMECAST_COMMAND_PLAY == kind);
    is_pause        = (UTL_CHROMECAST_COMMAND_PAUSE == kind);
    is_seek         = (UTL_CHROMECAST_COMMAND_SEEK == kind);
    token.data      = NULL;
    token.byteCount = 0;
    if (is_connect)
    {
        token = Text(CHROMECAST_TOKEN_CONNECT, sizeof(CHROMECAST_TOKEN_CONNECT) - 1);
    }
    else if (is_ping)
    {
        token = Text(CHROMECAST_TOKEN_PING, sizeof(CHROMECAST_TOKEN_PING) - 1);
    }
    else if (is_pong)
    {
        token = Text(CHROMECAST_TOKEN_PONG, sizeof(CHROMECAST_TOKEN_PONG) - 1);
    }
    else if (is_launch)
    {
        token = Text(CHROMECAST_TOKEN_LAUNCH, sizeof(CHROMECAST_TOKEN_LAUNCH) - 1);
    }
    else if (is_stop)
    {
        token = Text(CHROMECAST_TOKEN_STOP, sizeof(CHROMECAST_TOKEN_STOP) - 1);
    }
    else if (is_status)
    {
        token = Text(CHROMECAST_TOKEN_GET_STATUS, sizeof(CHROMECAST_TOKEN_GET_STATUS) - 1);
    }
    else if (is_play)
    {
        token = Text(CHROMECAST_TOKEN_PLAY, sizeof(CHROMECAST_TOKEN_PLAY) - 1);
    }
    else if (is_pause)
    {
        token = Text(CHROMECAST_TOKEN_PAUSE, sizeof(CHROMECAST_TOKEN_PAUSE) - 1);
    }
    else if (is_seek)
    {
        token = Text(CHROMECAST_TOKEN_SEEK, sizeof(CHROMECAST_TOKEN_SEEK) - 1);
    }
    else
    {
        /* Unsupported external enum values have no wire representation. */
    }
    return token;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsTypeOnly(Utl_Chromecast_CommandKind_t kind)
{
    bool_t is_connect;
    bool_t is_ping;
    bool_t is_pong;
    bool_t is_type_only;

    is_connect   = (UTL_CHROMECAST_COMMAND_CONNECT == kind);
    is_ping      = (UTL_CHROMECAST_COMMAND_PING == kind);
    is_pong      = (UTL_CHROMECAST_COMMAND_PONG == kind);
    is_type_only = is_connect || is_ping || is_pong;
    return is_type_only;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A type-only command reads only kind. Unknown kinds fail before requestId or
 *  data is inspected. Text is validated by the JSON counting pass.
 */
static FORCE_INLINE bool_t IsValidCommand(const Utl_Chromecast_Command_t* command)
{
    Utl_Json_Text_t token;
    bool_t          valid;
    bool_t          type_only;
    bool_t          has_request;
    bool_t          is_launch;
    bool_t          is_app_stop;
    bool_t          has_identifier;
    bool_t          is_seek;
    Utl_Json_Text_t identifier;
    bool_t          has_storage;
    bool_t          has_text;
    bool_t          valid_scale;
    bool_t          keep_state;
    bool_t          start;
    bool_t          pause;
    bool_t          valid_resume;

    token     = GetCommandToken(command->kind);
    valid     = (0 < token.byteCount);
    type_only = IsTypeOnly(command->kind);
    if (valid && !type_only)
    {
        has_request    = (0 < command->requestId);
        valid          = has_request;
        is_launch      = (UTL_CHROMECAST_COMMAND_LAUNCH == command->kind);
        is_app_stop    = (UTL_CHROMECAST_COMMAND_STOP_APPLICATION == command->kind);
        has_identifier = is_launch || is_app_stop;
        is_seek        = (UTL_CHROMECAST_COMMAND_SEEK == command->kind);
        if (has_identifier)
        {
            if (is_launch)
            {
                identifier = command->data.applicationId;
            }
            else
            {
                identifier = command->data.sessionId;
            }
            has_storage = HasText(identifier);
            has_text    = (0 < identifier.byteCount);
            valid       = has_request && has_storage && has_text;
        }
        else if (is_seek)
        {
            valid_scale  = (command->data.seek.currentTime.fractionDigits <= UTL_JSON_MAX_FRACTION_DIGITS);
            keep_state   = (UTL_CHROMECAST_RESUME_UNCHANGED == command->data.seek.resume);
            start        = (UTL_CHROMECAST_RESUME_START == command->data.seek.resume);
            pause        = (UTL_CHROMECAST_RESUME_PAUSE == command->data.seek.resume);
            valid_resume = keep_state || start || pause;
            valid        = has_request && valid_scale && valid_resume;
        }
        else
        {
            /* Other active command fields accept the full signed ID domain. */
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteName(Utl_Json_Writer_t* writer, const char_t* name, uint32_t bytes)
{
    Utl_Json_Text_t text;
    text = Text(name, bytes);
    (void)Utl_Json_WriteName(writer, text);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteToken(Utl_Json_Writer_t* writer, const char_t* value, uint32_t bytes)
{
    Utl_Json_Text_t text;
    text = Text(value, bytes);
    (void)Utl_Json_WriteString(writer, text);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The packed RGBA value always yields exactly eight hexadecimal digits.
 */
static FORCE_INLINE void WriteColor(Utl_Json_Writer_t* writer, uint32_t rgba)
{
    byte_t          bytes[COLOR_TEXT_BYTES];
    Utl_Json_Text_t text;

    bytes[COLOR_PREFIX_OFFSET]     = COLOR_PREFIX;
    bytes[COLOR_RED_HIGH_OFFSET]   = GetColorDigit(rgba >> COLOR_RED_HIGH_SHIFT);
    bytes[COLOR_RED_LOW_OFFSET]    = GetColorDigit(rgba >> COLOR_RED_LOW_SHIFT);
    bytes[COLOR_GREEN_HIGH_OFFSET] = GetColorDigit(rgba >> COLOR_GREEN_HIGH_SHIFT);
    bytes[COLOR_GREEN_LOW_OFFSET]  = GetColorDigit(rgba >> COLOR_GREEN_LOW_SHIFT);
    bytes[COLOR_BLUE_HIGH_OFFSET]  = GetColorDigit(rgba >> COLOR_BLUE_HIGH_SHIFT);
    bytes[COLOR_BLUE_LOW_OFFSET]   = GetColorDigit(rgba >> COLOR_BLUE_LOW_SHIFT);
    bytes[COLOR_ALPHA_HIGH_OFFSET] = GetColorDigit(rgba >> COLOR_ALPHA_HIGH_SHIFT);
    bytes[COLOR_ALPHA_LOW_OFFSET]  = GetColorDigit(rgba);
    text.data                      = bytes;
    text.byteCount                 = sizeof(bytes);
    (void)Utl_Json_WriteString(writer, text);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteSubtitle(Utl_Json_Writer_t* writer, const Utl_Chromecast_Subtitle_t* subtitle)
{
    WriteName(writer, CHROMECAST_TOKEN_TRACKS, sizeof(CHROMECAST_TOKEN_TRACKS) - 1);
    (void)Utl_Json_BeginArray(writer);
    (void)Utl_Json_BeginObject(writer);
    WriteName(writer, CHROMECAST_TOKEN_TRACK_ID, sizeof(CHROMECAST_TOKEN_TRACK_ID) - 1);
    (void)Utl_Json_WriteUint64(writer, subtitle->trackId);
    WriteName(writer, CHROMECAST_TOKEN_TYPE, sizeof(CHROMECAST_TOKEN_TYPE) - 1);
    WriteToken(writer, CHROMECAST_TOKEN_TEXT, sizeof(CHROMECAST_TOKEN_TEXT) - 1);
    WriteName(writer, CHROMECAST_TOKEN_SUBTYPE, sizeof(CHROMECAST_TOKEN_SUBTYPE) - 1);
    WriteToken(writer, CHROMECAST_TOKEN_SUBTITLES, sizeof(CHROMECAST_TOKEN_SUBTITLES) - 1);
    WriteName(writer, CHROMECAST_TOKEN_NAME, sizeof(CHROMECAST_TOKEN_NAME) - 1);
    (void)Utl_Json_WriteString(writer, subtitle->name);
    WriteName(writer, CHROMECAST_TOKEN_LANGUAGE, sizeof(CHROMECAST_TOKEN_LANGUAGE) - 1);
    (void)Utl_Json_WriteString(writer, subtitle->language);
    WriteName(writer, CHROMECAST_TOKEN_TRACK_CONTENT_ID, sizeof(CHROMECAST_TOKEN_TRACK_CONTENT_ID) - 1);
    (void)Utl_Json_WriteString(writer, subtitle->contentId);
    WriteName(writer, CHROMECAST_TOKEN_TRACK_CONTENT_TYPE, sizeof(CHROMECAST_TOKEN_TRACK_CONTENT_TYPE) - 1);
    WriteToken(writer, CHROMECAST_TOKEN_TEXT_VTT, sizeof(CHROMECAST_TOKEN_TEXT_VTT) - 1);
    (void)Utl_Json_EndObject(writer);
    (void)Utl_Json_EndArray(writer);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every style value comes from the caller; NULL style is omitted by EncodeLoad.
 */
static FORCE_INLINE void WriteTextTrackStyle(Utl_Json_Writer_t* writer, const Utl_Chromecast_TextTrackStyle_t* style)
{
    Utl_Json_Text_t edge;

    WriteName(writer, CHROMECAST_TOKEN_TEXT_TRACK_STYLE, sizeof(CHROMECAST_TOKEN_TEXT_TRACK_STYLE) - 1);
    (void)Utl_Json_BeginObject(writer);
    WriteName(writer, CHROMECAST_TOKEN_FOREGROUND_COLOR, sizeof(CHROMECAST_TOKEN_FOREGROUND_COLOR) - 1);
    WriteColor(writer, style->foregroundColor);
    WriteName(writer, CHROMECAST_TOKEN_BACKGROUND_COLOR, sizeof(CHROMECAST_TOKEN_BACKGROUND_COLOR) - 1);
    WriteColor(writer, style->backgroundColor);
    WriteName(writer, CHROMECAST_TOKEN_EDGE_TYPE, sizeof(CHROMECAST_TOKEN_EDGE_TYPE) - 1);
    edge = GetEdgeToken(style->edge);
    (void)Utl_Json_WriteString(writer, edge);
    WriteName(writer, CHROMECAST_TOKEN_EDGE_COLOR, sizeof(CHROMECAST_TOKEN_EDGE_COLOR) - 1);
    WriteColor(writer, style->edgeColor);
    WriteName(writer, CHROMECAST_TOKEN_FONT_SCALE, sizeof(CHROMECAST_TOKEN_FONT_SCALE) - 1);
    (void)Utl_Json_WriteDecimal(writer, style->fontScale);
    (void)Utl_Json_EndObject(writer);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void EncodeLoad(Utl_Json_Writer_t* writer, const Utl_Chromecast_Load_t* load)
{
    bool_t             has_subtitle;
    bool_t             is_buffered;
    bool_t             is_live;
    Utl_Json_Decimal_t position;
    has_subtitle = (NULL != load->subtitle);
    is_buffered  = (UTL_CHROMECAST_STREAM_BUFFERED == load->stream);
    is_live      = (UTL_CHROMECAST_STREAM_LIVE == load->stream);
    (void)Utl_Json_BeginObject(writer);
    WriteName(writer, CHROMECAST_TOKEN_TYPE, sizeof(CHROMECAST_TOKEN_TYPE) - 1);
    WriteToken(writer, CHROMECAST_TOKEN_LOAD, sizeof(CHROMECAST_TOKEN_LOAD) - 1);
    WriteName(writer, CHROMECAST_TOKEN_REQUEST_ID, sizeof(CHROMECAST_TOKEN_REQUEST_ID) - 1);
    (void)Utl_Json_WriteUint64(writer, load->requestId);
    WriteName(writer, CHROMECAST_TOKEN_SESSION_ID, sizeof(CHROMECAST_TOKEN_SESSION_ID) - 1);
    (void)Utl_Json_WriteString(writer, load->sessionId);
    WriteName(writer, CHROMECAST_TOKEN_AUTOPLAY, sizeof(CHROMECAST_TOKEN_AUTOPLAY) - 1);
    (void)Utl_Json_WriteBoolean(writer, load->autoplay);
    WriteName(writer, CHROMECAST_TOKEN_CURRENT_TIME, sizeof(CHROMECAST_TOKEN_CURRENT_TIME) - 1);
    position.coefficient    = load->startMilliseconds;
    position.fractionDigits = LOAD_TIME_FRACTION_DIGITS;
    (void)Utl_Json_WriteDecimal(writer, position);
    if (has_subtitle)
    {
        WriteName(writer, CHROMECAST_TOKEN_ACTIVE_TRACK_IDS, sizeof(CHROMECAST_TOKEN_ACTIVE_TRACK_IDS) - 1);
        (void)Utl_Json_BeginArray(writer);
        (void)Utl_Json_WriteUint64(writer, load->subtitle->trackId);
        (void)Utl_Json_EndArray(writer);
    }
    WriteName(writer, CHROMECAST_TOKEN_MEDIA, sizeof(CHROMECAST_TOKEN_MEDIA) - 1);
    (void)Utl_Json_BeginObject(writer);
    WriteName(writer, CHROMECAST_TOKEN_CONTENT_ID, sizeof(CHROMECAST_TOKEN_CONTENT_ID) - 1);
    (void)Utl_Json_WriteString(writer, load->contentId);
    WriteName(writer, CHROMECAST_TOKEN_CONTENT_TYPE, sizeof(CHROMECAST_TOKEN_CONTENT_TYPE) - 1);
    (void)Utl_Json_WriteString(writer, load->contentType);
    WriteName(writer, CHROMECAST_TOKEN_STREAM_TYPE, sizeof(CHROMECAST_TOKEN_STREAM_TYPE) - 1);
    if (is_buffered)
    {
        WriteToken(writer, CHROMECAST_TOKEN_BUFFERED, sizeof(CHROMECAST_TOKEN_BUFFERED) - 1);
    }
    else if (is_live)
    {
        WriteToken(writer, CHROMECAST_TOKEN_LIVE, sizeof(CHROMECAST_TOKEN_LIVE) - 1);
    }
    else
    {
        WriteToken(writer, CHROMECAST_TOKEN_NONE, sizeof(CHROMECAST_TOKEN_NONE) - 1);
    }
    WriteName(writer, CHROMECAST_TOKEN_METADATA, sizeof(CHROMECAST_TOKEN_METADATA) - 1);
    (void)Utl_Json_BeginObject(writer);
    WriteName(writer, CHROMECAST_TOKEN_METADATA_TYPE, sizeof(CHROMECAST_TOKEN_METADATA_TYPE) - 1);
    (void)Utl_Json_WriteUint64(writer, CHROMECAST_METADATA_GENERIC);
    WriteName(writer, CHROMECAST_TOKEN_TITLE, sizeof(CHROMECAST_TOKEN_TITLE) - 1);
    (void)Utl_Json_WriteString(writer, load->title);
    (void)Utl_Json_EndObject(writer);
    if (has_subtitle)
    {
        WriteSubtitle(writer, load->subtitle);
    }
    if (NULL != load->textTrackStyle)
    {
        WriteTextTrackStyle(writer, load->textTrackStyle);
    }
    (void)Utl_Json_EndObject(writer);
    (void)Utl_Json_EndObject(writer);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Resume state is omitted unless the caller explicitly selects a transition.
 */
static FORCE_INLINE void WriteSeek(Utl_Json_Writer_t* writer, const Utl_Chromecast_Seek_t* seek)
{
    bool_t start;
    bool_t pause;

    WriteName(writer, CHROMECAST_TOKEN_MEDIA_SESSION_ID, sizeof(CHROMECAST_TOKEN_MEDIA_SESSION_ID) - 1);
    (void)Utl_Json_WriteInt64(writer, seek->mediaSessionId);
    WriteName(writer, CHROMECAST_TOKEN_CURRENT_TIME, sizeof(CHROMECAST_TOKEN_CURRENT_TIME) - 1);
    (void)Utl_Json_WriteDecimal(writer, seek->currentTime);
    start = (UTL_CHROMECAST_RESUME_START == seek->resume);
    pause = (UTL_CHROMECAST_RESUME_PAUSE == seek->resume);
    if (start || pause)
    {
        WriteName(writer, CHROMECAST_TOKEN_RESUME_STATE, sizeof(CHROMECAST_TOKEN_RESUME_STATE) - 1);
        if (start)
        {
            WriteToken(writer, CHROMECAST_TOKEN_PLAYBACK_START, sizeof(CHROMECAST_TOKEN_PLAYBACK_START) - 1);
        }
        else
        {
            WriteToken(writer, CHROMECAST_TOKEN_PLAYBACK_PAUSE, sizeof(CHROMECAST_TOKEN_PLAYBACK_PAUSE) - 1);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Type, correlation and schema-specific fields are written in a stable order.
 *  Only the selected data member is accessed, in both counting and output passes.
 */
static FORCE_INLINE void EncodeCommand(Utl_Json_Writer_t* writer, const Utl_Chromecast_Command_t* command)
{
    Utl_Json_Text_t token;
    bool_t          type_only;
    bool_t          is_launch;
    bool_t          is_app_stop;
    bool_t          is_status;
    bool_t          is_play;
    bool_t          is_pause;
    bool_t          is_media_stop;
    bool_t          has_media_id;
    bool_t          is_seek;
    const int64_t*  status_session;

    token         = GetCommandToken(command->kind);
    type_only     = IsTypeOnly(command->kind);
    is_launch     = (UTL_CHROMECAST_COMMAND_LAUNCH == command->kind);
    is_app_stop   = (UTL_CHROMECAST_COMMAND_STOP_APPLICATION == command->kind);
    is_status     = (UTL_CHROMECAST_COMMAND_GET_MEDIA_STATUS == command->kind);
    is_play       = (UTL_CHROMECAST_COMMAND_PLAY == command->kind);
    is_pause      = (UTL_CHROMECAST_COMMAND_PAUSE == command->kind);
    is_media_stop = (UTL_CHROMECAST_COMMAND_STOP_MEDIA == command->kind);
    has_media_id  = is_play || is_pause || is_media_stop;
    is_seek       = (UTL_CHROMECAST_COMMAND_SEEK == command->kind);
    (void)Utl_Json_BeginObject(writer);
    WriteName(writer, CHROMECAST_TOKEN_TYPE, sizeof(CHROMECAST_TOKEN_TYPE) - 1);
    (void)Utl_Json_WriteString(writer, token);
    if (!type_only)
    {
        WriteName(writer, CHROMECAST_TOKEN_REQUEST_ID, sizeof(CHROMECAST_TOKEN_REQUEST_ID) - 1);
        (void)Utl_Json_WriteUint64(writer, command->requestId);
    }
    if (is_launch)
    {
        WriteName(writer, CHROMECAST_TOKEN_APP_ID, sizeof(CHROMECAST_TOKEN_APP_ID) - 1);
        (void)Utl_Json_WriteString(writer, command->data.applicationId);
    }
    else if (is_app_stop)
    {
        WriteName(writer, CHROMECAST_TOKEN_SESSION_ID, sizeof(CHROMECAST_TOKEN_SESSION_ID) - 1);
        (void)Utl_Json_WriteString(writer, command->data.sessionId);
    }
    else if (is_status)
    {
        status_session = command->data.statusSessionId;
        if (NULL != status_session)
        {
            WriteName(writer, CHROMECAST_TOKEN_MEDIA_SESSION_ID, sizeof(CHROMECAST_TOKEN_MEDIA_SESSION_ID) - 1);
            (void)Utl_Json_WriteInt64(writer, *status_session);
        }
    }
    else if (has_media_id)
    {
        WriteName(writer, CHROMECAST_TOKEN_MEDIA_SESSION_ID, sizeof(CHROMECAST_TOKEN_MEDIA_SESSION_ID) - 1);
        (void)Utl_Json_WriteInt64(writer, command->data.mediaSessionId);
    }
    else if (is_seek)
    {
        WriteSeek(writer, &command->data.seek);
    }
    else
    {
        LOGASSERT(type_only);
    }
    (void)Utl_Json_EndObject(writer);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Namespace literals are counted at compile time; no termination scan is needed.
 */
Utl_Json_Text_t Utl_Chromecast_GetNamespace(Utl_Chromecast_Namespace_t nameSpace)
{
    bool_t          is_connection;
    bool_t          is_heartbeat;
    bool_t          is_receiver;
    bool_t          is_media;
    Utl_Json_Text_t text;

    is_connection  = (UTL_CHROMECAST_NAMESPACE_CONNECTION == nameSpace);
    is_heartbeat   = (UTL_CHROMECAST_NAMESPACE_HEARTBEAT == nameSpace);
    is_receiver    = (UTL_CHROMECAST_NAMESPACE_RECEIVER == nameSpace);
    is_media       = (UTL_CHROMECAST_NAMESPACE_MEDIA == nameSpace);
    text.data      = NULL;
    text.byteCount = 0;
    if (is_connection)
    {
        text = Text(CHROMECAST_NAMESPACE_CONNECTION, sizeof(CHROMECAST_NAMESPACE_CONNECTION) - 1);
    }
    else if (is_heartbeat)
    {
        text = Text(CHROMECAST_NAMESPACE_HEARTBEAT, sizeof(CHROMECAST_NAMESPACE_HEARTBEAT) - 1);
    }
    else if (is_receiver)
    {
        text = Text(CHROMECAST_NAMESPACE_RECEIVER, sizeof(CHROMECAST_NAMESPACE_RECEIVER) - 1);
    }
    else if (is_media)
    {
        text = Text(CHROMECAST_NAMESPACE_MEDIA, sizeof(CHROMECAST_NAMESPACE_MEDIA) - 1);
    }
    else
    {
        /* Unsupported kinds retain the empty result. */
    }
    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The command discriminator determines routing without examining payload fields.
 */
Utl_Chromecast_Namespace_t Utl_Chromecast_GetCommandNamespace(Utl_Chromecast_CommandKind_t kind)
{
    bool_t                     is_connection;
    bool_t                     is_ping;
    bool_t                     is_pong;
    bool_t                     is_heartbeat;
    bool_t                     is_launch;
    bool_t                     is_application_stop;
    bool_t                     is_receiver;
    bool_t                     is_status;
    bool_t                     is_play;
    bool_t                     is_pause;
    bool_t                     is_media_stop;
    bool_t                     is_seek;
    bool_t                     is_media;
    Utl_Chromecast_Namespace_t name_space;

    is_connection       = (UTL_CHROMECAST_COMMAND_CONNECT == kind);
    is_ping             = (UTL_CHROMECAST_COMMAND_PING == kind);
    is_pong             = (UTL_CHROMECAST_COMMAND_PONG == kind);
    is_heartbeat        = is_ping || is_pong;
    is_launch           = (UTL_CHROMECAST_COMMAND_LAUNCH == kind);
    is_application_stop = (UTL_CHROMECAST_COMMAND_STOP_APPLICATION == kind);
    is_receiver         = is_launch || is_application_stop;
    is_status           = (UTL_CHROMECAST_COMMAND_GET_MEDIA_STATUS == kind);
    is_play             = (UTL_CHROMECAST_COMMAND_PLAY == kind);
    is_pause            = (UTL_CHROMECAST_COMMAND_PAUSE == kind);
    is_media_stop       = (UTL_CHROMECAST_COMMAND_STOP_MEDIA == kind);
    is_seek             = (UTL_CHROMECAST_COMMAND_SEEK == kind);
    is_media            = is_status || is_play || is_pause || is_media_stop || is_seek;
    name_space          = UTL_CHROMECAST_NAMESPACE_INVALID;
    if (is_connection)
    {
        name_space = UTL_CHROMECAST_NAMESPACE_CONNECTION;
    }
    else if (is_heartbeat)
    {
        name_space = UTL_CHROMECAST_NAMESPACE_HEARTBEAT;
    }
    else if (is_receiver)
    {
        name_space = UTL_CHROMECAST_NAMESPACE_RECEIVER;
    }
    else if (is_media)
    {
        name_space = UTL_CHROMECAST_NAMESPACE_MEDIA;
    }
    else
    {
        /* Unsupported commands retain INVALID. */
    }
    return name_space;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Cast semantics and JSON syntax are both validated by the counting pass before any output is touched.
 */
uint32_t Utl_Chromecast_GetLoadByteCount(const Utl_Chromecast_Load_t* load)
{
    bool_t            valid;
    uint32_t          count;
    Utl_Json_Writer_t writer;
    Utl_Json_Frame_t  frames[LOAD_FRAME_CAPACITY];
    LOGASSERT(NULL != load);
    valid = IsValidLoad(load);
    count = 0;
    if (valid)
    {
        Utl_Json_InitializeWriter(&writer, frames, LOAD_FRAME_CAPACITY, NULL, UINT32_MAX);
        EncodeLoad(&writer, load);
        if (Utl_Json_IsComplete(&writer))
        {
            count = Utl_Json_GetByteCount(&writer);
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The count pass validates the entire active schema without writing output bytes.
 */
uint32_t Utl_Chromecast_GetCommandByteCount(const Utl_Chromecast_Command_t* command)
{
    bool_t            valid;
    uint32_t          count;
    Utl_Json_Writer_t writer;
    Utl_Json_Frame_t  frames[COMMAND_FRAME_CAPACITY];

    LOGASSERT(NULL != command);

    valid = IsValidCommand(command);
    count = 0;
    if (valid)
    {
        Utl_Json_InitializeWriter(&writer, frames, COMMAND_FRAME_CAPACITY, NULL, UINT32_MAX);
        EncodeCommand(&writer, command);
        if (Utl_Json_IsComplete(&writer))
        {
            count = Utl_Json_GetByteCount(&writer);
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Two passes preserve output on invalid fields, UTF-8, count overflow or short capacity.
 */
uint32_t Utl_Chromecast_WriteLoad(const Utl_Chromecast_Load_t* load, byte_t* buffer, uint32_t capacity)
{
    uint32_t          count;
    bool_t            has_output;
    bool_t            has_message;
    bool_t            has_capacity;
    bool_t            can_write;
    uint32_t          written;
    Utl_Json_Writer_t writer;
    Utl_Json_Frame_t  frames[LOAD_FRAME_CAPACITY];
    count        = Utl_Chromecast_GetLoadByteCount(load);
    has_output   = (NULL != buffer);
    has_message  = (0 < count);
    has_capacity = (count <= capacity);
    can_write    = has_output && has_message && has_capacity;
    written      = 0;
    if (can_write)
    {
        Utl_Json_InitializeWriter(&writer, frames, LOAD_FRAME_CAPACITY, buffer, capacity);
        EncodeLoad(&writer, load);
        LOGASSERT(Utl_Json_IsComplete(&writer));
        LOGASSERT(count == Utl_Json_GetByteCount(&writer));
        written = count;
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Counting validates the complete model before any output byte is modified.
 *  A stable disjoint model guarantees the second pass writes the derived extent.
 */
uint32_t Utl_Chromecast_WriteCommand(const Utl_Chromecast_Command_t* command, byte_t* buffer, uint32_t capacity)
{
    uint32_t          count;
    bool_t            has_output;
    bool_t            has_message;
    bool_t            has_capacity;
    bool_t            can_write;
    uint32_t          written;
    Utl_Json_Writer_t writer;
    Utl_Json_Frame_t  frames[COMMAND_FRAME_CAPACITY];

    count        = Utl_Chromecast_GetCommandByteCount(command);
    has_output   = (NULL != buffer);
    has_message  = (0 < count);
    has_capacity = (count <= capacity);
    can_write    = has_output && has_message && has_capacity;
    written      = 0;
    if (can_write)
    {
        Utl_Json_InitializeWriter(&writer, frames, COMMAND_FRAME_CAPACITY, buffer, capacity);
        EncodeCommand(&writer, command);
        LOGASSERT(Utl_Json_IsComplete(&writer));
        LOGASSERT(count == Utl_Json_GetByteCount(&writer));
        written = count;
    }
    return written;
}
