#ifndef UTL_CHROMECAST_H
#define UTL_CHROMECAST_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Google Cast media LOAD messages and text track style:
 * https://developers.google.com/cast/docs/media/messages#load
 * https://developers.google.com/cast/docs/reference/web_sender/chrome.cast.media.LoadRequest
 * JSON grammar, string escaping, numbers and structural writing are owned by utl_json.
 * https://developers.google.com/cast/docs/reference/web_sender/chrome.cast.media.TextTrackStyle
 * Control-channel reference implementation (heartbeat, connection, receiver):
 * https://chromium.googlesource.com/chromium/src/+/main/components/media_router/common/providers/cast/channel/cast_message_util.cc
 * The LOAD profile carries one optional WebVTT subtitle and selected text style.
 * Typed commands cover connection, heartbeat, launch, application stop, media
 * status, play, pause, media stop and seek. Response parsing, remaining command
 * kinds, custom data, metadata kinds and track collections remain open.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#include "utl/utl_json.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UTL_CHROMECAST_MILLISECONDS_PER_SECOND (1000) ///< Fixed-point time scale of this profile.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Protocol namespace used by a Cast connection, heartbeat, receiver or media message.
 */
typedef enum Utl_Chromecast_Namespace_e
{
    UTL_CHROMECAST_NAMESPACE_INVALID,    ///< No supported namespace.
    UTL_CHROMECAST_NAMESPACE_CONNECTION, ///< Virtual channel connection management.
    UTL_CHROMECAST_NAMESPACE_HEARTBEAT,  ///< Channel keepalive.
    UTL_CHROMECAST_NAMESPACE_RECEIVER,   ///< Receiver application management.
    UTL_CHROMECAST_NAMESPACE_MEDIA       ///< Media playback and status.
} Utl_Chromecast_Namespace_t;

/*************************************************************************************************/
/**
 * \brief Named media stream semantics.
 *
 * \par Details:
 *  Cast media stream kinds. Values select named wire tokens, never arbitrary JSON.
 */
typedef enum Utl_Chromecast_Stream_e
{
    UTL_CHROMECAST_STREAM_INVALID, ///< Unclassified external input; no corresponding wire token.
    UTL_CHROMECAST_STREAM_NONE,
    UTL_CHROMECAST_STREAM_BUFFERED,
    UTL_CHROMECAST_STREAM_LIVE
} Utl_Chromecast_Stream_t;

/*************************************************************************************************/
/**
 * \brief One selected WebVTT subtitle track.
 *
 * \par Details:
 *  The selected WebVTT subtitle is represented as a TEXT/SUBTITLES track.
 *  The name and language are caller-selected; the content ID is its fetch URL.
 */
typedef struct Utl_Chromecast_Subtitle_s
{
    uint32_t        trackId;   ///< Nonzero media track identifier.
    Utl_Json_Text_t contentId; ///< Required subtitle content URL.
    Utl_Json_Text_t name;      ///< Display name, possibly empty.
    Utl_Json_Text_t language;  ///< Language tag, possibly empty.
} Utl_Chromecast_Subtitle_t;

/*************************************************************************************************/
/**
 * \brief Defined text-track edge treatments from the Cast media schema.
 */
typedef enum Utl_Chromecast_TextEdge_e
{
    UTL_CHROMECAST_TEXT_EDGE_INVALID,     ///< Invalid external value; no wire representation.
    UTL_CHROMECAST_TEXT_EDGE_NONE,        ///< No edge treatment.
    UTL_CHROMECAST_TEXT_EDGE_OUTLINE,     ///< Outline around glyphs.
    UTL_CHROMECAST_TEXT_EDGE_DROP_SHADOW, ///< Shadow behind glyphs.
    UTL_CHROMECAST_TEXT_EDGE_RAISED,      ///< Raised glyph edge.
    UTL_CHROMECAST_TEXT_EDGE_DEPRESSED    ///< Depressed glyph edge.
} Utl_Chromecast_TextEdge_t;

/*************************************************************************************************/
/**
 * \brief Caller-selected subset of the Cast textTrackStyle object.
 *
 * \par Details:
 *  Colors are packed RRGGBBAA values serialized as eight hexadecimal digits.
 *  Font scale uses the exact decimal model supplied by utl_json.
 *  The fixed-point representation is this API's domain, not a protocol bound.
 *  The caller selects all values; this module imposes no application theme.
 */
typedef struct Utl_Chromecast_TextTrackStyle_s
{
    uint32_t                  foregroundColor; ///< Packed foreground RRGGBBAA.
    uint32_t                  backgroundColor; ///< Packed background RRGGBBAA.
    Utl_Chromecast_TextEdge_t edge;            ///< Required supported edge treatment.
    uint32_t                  edgeColor;       ///< Packed edge RRGGBBAA.
    Utl_Json_Decimal_t        fontScale;       ///< Nonnegative exact decimal font scaling factor.
} Utl_Chromecast_TextTrackStyle_t;

/*************************************************************************************************/
/**
 * \brief Typed LOAD request for the media profile used by CastDemo.
 *
 * \par Details:
 *  All pointers are borrowed and counted. Subtitle and text style are optional.
 *  Time is encoded as seconds with exactly three fractional digits; uint64_t
 *  milliseconds are this profile's numeric domain, not a Cast protocol limit.
 */
typedef struct Utl_Chromecast_Load_s
{
    uint32_t                               requestId;         ///< Nonzero request/response correlation identifier.
    Utl_Json_Text_t                        sessionId;         ///< Required receiver application session.
    bool_t                                 autoplay;          ///< Whether playback should start after loading.
    uint64_t                               startMilliseconds; ///< Nonnegative media position.
    Utl_Json_Text_t                        contentId;         ///< Required media content URL.
    Utl_Json_Text_t                        contentType;       ///< Required media MIME type.
    Utl_Chromecast_Stream_t                stream;            ///< Stream semantics.
    Utl_Json_Text_t                        title;             ///< Generic metadata title, possibly empty.
    const Utl_Chromecast_TextTrackStyle_t* textTrackStyle;    ///< Optional borrowed style; NULL omits it.
    const Utl_Chromecast_Subtitle_t*       subtitle;          ///< Optional selected subtitle track.
} Utl_Chromecast_Load_t;

/*************************************************************************************************/
/**
 * \brief Supported outgoing command schemas, distinguished by namespace and fields.
 */
typedef enum Utl_Chromecast_CommandKind_e
{
    UTL_CHROMECAST_COMMAND_INVALID,          ///< No wire representation.
    UTL_CHROMECAST_COMMAND_CONNECT,          ///< Connection namespace; type only.
    UTL_CHROMECAST_COMMAND_PING,             ///< Heartbeat namespace; type only.
    UTL_CHROMECAST_COMMAND_PONG,             ///< Heartbeat namespace; type only.
    UTL_CHROMECAST_COMMAND_LAUNCH,           ///< Receiver namespace; applicationId member.
    UTL_CHROMECAST_COMMAND_STOP_APPLICATION, ///< Receiver namespace; sessionId member.
    UTL_CHROMECAST_COMMAND_GET_MEDIA_STATUS, ///< Media namespace; statusSessionId member.
    UTL_CHROMECAST_COMMAND_PLAY,             ///< Media namespace; mediaSessionId member.
    UTL_CHROMECAST_COMMAND_PAUSE,            ///< Media namespace; mediaSessionId member.
    UTL_CHROMECAST_COMMAND_STOP_MEDIA,       ///< Media namespace; mediaSessionId member.
    UTL_CHROMECAST_COMMAND_SEEK              ///< Media namespace; seek member.
} Utl_Chromecast_CommandKind_t;

/*************************************************************************************************/
/**
 * \brief Optional SEEK playback transition, independent of the target position.
 */
typedef enum Utl_Chromecast_Resume_e
{
    UTL_CHROMECAST_RESUME_INVALID,   ///< Invalid input; reject before serialization.
    UTL_CHROMECAST_RESUME_UNCHANGED, ///< Omit resumeState; preserve playback state.
    UTL_CHROMECAST_RESUME_START,     ///< PLAYBACK_START.
    UTL_CHROMECAST_RESUME_PAUSE      ///< PLAYBACK_PAUSE.
} Utl_Chromecast_Resume_t;

/*************************************************************************************************/
/**
 * \brief Seek to an exact nonnegative decimal position in seconds.
 *
 * \par Details:
 *  This profile always supplies currentTime. Its decimal domain is an API bound,
 *  not a Cast protocol bound. The receiver determines the valid playback range.
 */
typedef struct Utl_Chromecast_Seek_s
{
    int64_t                 mediaSessionId; ///< Receiver-assigned identifier, preserved exactly.
    Utl_Json_Decimal_t      currentTime;    ///< Nonnegative seconds; at most 19 fractional digits.
    Utl_Chromecast_Resume_t resume;         ///< Optional transition after positioning.
} Utl_Chromecast_Seek_t;

/*************************************************************************************************/
/**
 * \brief Command-specific borrowed data; only the member selected by kind is read.
 */
typedef struct Utl_Chromecast_CommandData_s
{
    Utl_Json_Text_t       applicationId;   ///< LAUNCH: required nonempty UTF-8 application ID.
    Utl_Json_Text_t       sessionId;       ///< STOP_APPLICATION: required nonempty UTF-8 session ID.
    const int64_t*        statusSessionId; ///< GET_MEDIA_STATUS: optional ID; NULL requests all sessions.
    int64_t               mediaSessionId;  ///< PLAY, PAUSE, STOP_MEDIA: receiver-assigned ID.
    Utl_Chromecast_Seek_t seek;            ///< SEEK: target session, position and resume policy.
} Utl_Chromecast_CommandData_t;

/*************************************************************************************************/
/**
 * \brief Typed outgoing command, excluding the separately modeled LOAD message.
 *
 * \par Details:
 *  CONNECT, PING and PONG read only kind. Every other kind also reads requestId
 *  and its selected data member. The caller supplies a nonzero correlation ID.
 *  Unselected fields are not read. All borrowed data remains stable through
 *  query/write calls and is disjoint from output. No routing or sender policy
 *  is inferred; the caller uses the namespace documented on the command kind.
 */
typedef struct Utl_Chromecast_Command_s
{
    Utl_Chromecast_CommandKind_t kind;      ///< Selects schema and applicable data fields.
    uint32_t                     requestId; ///< Nonzero correlation ID, except type-only commands.
    Utl_Chromecast_CommandData_t data;      ///< Active kind-specific fields; ignored for type-only commands.
} Utl_Chromecast_Command_t;

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Derives the exact namespace URI for a supported namespace kind.
 *
 * \par Requirements:
 *  - 001 Known kinds shall return immutable counted UTF-8 protocol literals.
 *  - 002 Invalid or unsupported kinds shall return an empty span without native calls or allocation.
 *
 * \param[in] nameSpace   Namespace kind
 *
 * \retval    text   Borrowed namespace bytes; empty for an unsupported kind.
 */
Utl_Json_Text_t Utl_Chromecast_GetNamespace(Utl_Chromecast_Namespace_t nameSpace);

/*************************************************************************************************/
/**
 * \brief Derives the namespace required by a typed command schema.
 *
 * \par Requirements:
 *  - 001 Every supported command shall map to its required namespace.
 *  - 002 Unknown kinds shall return INVALID; the separately modeled LOAD uses MEDIA.
 *
 * \param[in] kind   Command schema kind
 *
 * \retval    nameSpace   Required namespace, or INVALID for an unsupported kind.
 */
Utl_Chromecast_Namespace_t Utl_Chromecast_GetCommandNamespace(Utl_Chromecast_CommandKind_t kind);

/*************************************************************************************************/
/**
 * \brief Derives the complete UTF-8 JSON byte count for one typed LOAD request.
 *
 * \par Requirements:
 *  - 001 Required model storage shall assert when missing.
 *  - 002 Every counted string shall be valid UTF-8; missing nonempty storage shall fail.
 *  - 003 Empty required IDs/media types, unsupported kinds, zero request/track IDs and count overflow shall fail.
 *  - 004 A style decimal scale above UTL_JSON_MAX_FRACTION_DIGITS shall fail before writing.
 *  - 005 No allocation, CRT or OS calls shall occur.
 *
 * \param[in] load   Borrowed model and strings, stable for the call
 *
 * \retval    nonzero   Complete serialized byte count without a terminator.
 * \retval    0         Invalid model or a result exceeding uint32_t.
 */
uint32_t Utl_Chromecast_GetLoadByteCount(const Utl_Chromecast_Load_t* load);

/*************************************************************************************************/
/**
 * \brief Derives the complete byte count for a typed control command.
 *
 * \par Requirements:
 *  - 001 Missing required model storage shall assert.
 *  - 002 Unknown kinds, zero request IDs on correlated commands and invalid active fields shall fail.
 *  - 003 Required text shall be nonempty scalar UTF-8; all strings shall be JSON-escaped.
 *  - 004 Only the fields selected by kind shall be read; no allocation, CRT or native operation shall occur.
 *
 * \param[in] command   Stable borrowed model with its selected fields initialized
 *
 * \retval    nonzero   Exact serialized byte count without a terminator.
 * \retval    0         Invalid model or uint32_t byte-count overflow.
 */
uint32_t Utl_Chromecast_GetCommandByteCount(const Utl_Chromecast_Command_t* command);

/*************************************************************************************************/
/**
 * \brief Writes a complete preflighted LOAD request into caller-owned output.
 *
 * \par Requirements:
 *  - 001 Required model storage shall assert when missing.
 *  - 002 The model and borrowed text shall remain unchanged and disjoint from output throughout the call.
 *  - 003 Capacity and all external model data shall be validated before any output byte changes.
 *  - 004 No NUL terminator, allocation, CRT or OS operation shall be introduced.
 *
 * \param[in]  load       Borrowed typed LOAD request
 * \param[out] buffer     Writable output disjoint from the model and its strings
 * \param[in]  capacity   Available output bytes
 *
 * \retval     nonzero   Complete byte count written.
 * \retval     0         Invalid model, missing output or insufficient capacity; output unchanged.
 */
uint32_t Utl_Chromecast_WriteLoad(const Utl_Chromecast_Load_t* load, byte_t* buffer, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes a fully preflighted command to counted byte storage.
 *
 * \par Requirements:
 *  - 001 Missing required model storage shall assert.
 *  - 002 Model, borrowed strings and optional status ID shall remain stable and disjoint from output.
 *  - 003 Every active field and the complete capacity shall be validated before output changes.
 *  - 004 No implicit terminator, allocation, CRT or native operation shall occur.
 *
 * \param[in]  command    Required borrowed model and active fields
 * \param[out] buffer     Writable output; NULL fails without mutation
 * \param[in]  capacity   Writable byte extent
 *
 * \retval     nonzero   Complete byte count written.
 * \retval     0         Invalid model, missing output or insufficient capacity; output unchanged.
 */
uint32_t Utl_Chromecast_WriteCommand(const Utl_Chromecast_Command_t* command, byte_t* buffer, uint32_t capacity);

#endif /* UTL_CHROMECAST_H */
