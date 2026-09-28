#ifndef UTL_CASTJSON_H
#define UTL_CASTJSON_H

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

#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UTL_CASTJSON_MILLISECONDS_PER_SECOND (1000) ///< Fixed-point time scale of this profile.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Counted UTF-8 text used by Cast JSON fields.
 *
 * \par Details:
 *  Borrowed counted UTF-8, without any implicit terminator or ownership transfer.
 */
typedef struct Utl_CastJson_Text_s
{
    const char_t* data;      ///< Readable UTF-8 storage; NULL only when byteCount is zero.
    uint32_t      byteCount; ///< Readable bytes; embedded U+0000 is encoded as a JSON escape.
} Utl_CastJson_Text_t;

/*************************************************************************************************/
/**
 * \brief Named media stream semantics.
 *
 * \par Details:
 *  Cast media stream kinds. Values select named wire tokens, never arbitrary JSON.
 */
typedef enum Utl_CastJson_Stream_e
{
    UTL_CASTJSON_STREAM_INVALID, ///< Unclassified external input; no corresponding wire token.
    UTL_CASTJSON_STREAM_NONE,
    UTL_CASTJSON_STREAM_BUFFERED,
    UTL_CASTJSON_STREAM_LIVE
} Utl_CastJson_Stream_t;

/*************************************************************************************************/
/**
 * \brief One selected WebVTT subtitle track.
 *
 * \par Details:
 *  The selected WebVTT subtitle is represented as a TEXT/SUBTITLES track.
 *  The name and language are caller-selected; the content ID is its fetch URL.
 */
typedef struct Utl_CastJson_Subtitle_s
{
    uint32_t            trackId;   ///< Nonzero media track identifier.
    Utl_CastJson_Text_t contentId; ///< Required subtitle content URL.
    Utl_CastJson_Text_t name;      ///< Display name, possibly empty.
    Utl_CastJson_Text_t language;  ///< Language tag, possibly empty.
} Utl_CastJson_Subtitle_t;

/*************************************************************************************************/
/**
 * \brief Typed LOAD request for the media profile used by CastDemo.
 *
 * \par Details:
 *  All pointers are borrowed and counted. The subtitle is optional.
 *  Time is encoded as seconds with exactly three fractional digits; uint64_t
 *  milliseconds are this profile's numeric domain, not a Cast protocol limit.
 */
typedef struct Utl_CastJson_Load_s
{
    uint32_t                       requestId;         ///< Request/response correlation identifier.
    Utl_CastJson_Text_t            sessionId;         ///< Required receiver application session.
    bool_t                         autoplay;          ///< Whether playback should start after loading.
    uint64_t                       startMilliseconds; ///< Nonnegative media position.
    Utl_CastJson_Text_t            contentId;         ///< Required media content URL.
    Utl_CastJson_Text_t            contentType;       ///< Required media MIME type.
    Utl_CastJson_Stream_t          stream;            ///< Stream semantics.
    Utl_CastJson_Text_t            title;             ///< Generic metadata title, possibly empty.
    const Utl_CastJson_Subtitle_t* subtitle;          ///< Optional selected subtitle track.
} Utl_CastJson_Load_t;

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
 * \brief Derives the complete UTF-8 JSON byte count for one typed LOAD request.
 *
 * \par Requirements:
 *  - 001 Required model storage shall assert when missing.
 *  - 002 Every counted string shall be valid UTF-8; missing nonempty storage shall fail.
 *  - 003 Empty required IDs/media types, unsupported stream kinds, zero subtitle IDs and count overflow shall fail.
 *  - 004 No allocation, CRT or OS calls shall occur.
 *
 * \param[in] load   Borrowed model and strings, stable for the call
 *
 * \retval    nonzero   Complete serialized byte count without a terminator.
 * \retval    0         Invalid model or a result exceeding uint32_t.
 */
uint32_t Utl_CastJson_GetLoadByteCount(const Utl_CastJson_Load_t* load);

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
uint32_t Utl_CastJson_WriteLoad(const Utl_CastJson_Load_t* load, char_t* buffer, uint32_t capacity);

#endif /* UTL_CASTJSON_H */
