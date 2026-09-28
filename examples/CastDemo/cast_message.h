#ifndef CAST_MESSAGE_H
#define CAST_MESSAGE_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Compatibility boundary between the current Cast session and utl_castv2.
 * Wire syntax, required fields, UTF-8 validation and framing belong to utl_castv2.
 * This adapter retains no storage and performs no allocation, CRT calls or I/O.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_castv2.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define CAST_MESSAGE_MAX_BODY  (UTL_CASTV2_MAX_BODY)
#define CAST_MESSAGE_MAX_FRAME (UTL_CASTV2_MAX_FRAME)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Borrowed application view of a decoded or outgoing CastV2 message.
 *
 * \par Details:
 *  Counts are bytes, not character counts. Spans need no terminator after Decode.
 *  All spans remain valid only while the caller retains their original storage.
 */
typedef struct CastMessage_s
{
    const char_t* pSource;           ///< Counted UTF-8 source endpoint.
    uint32_t      sourceLength;      ///< Source bytes.
    const char_t* pDestination;      ///< Counted UTF-8 destination endpoint.
    uint32_t      destinationLength; ///< Destination bytes.
    const char_t* pNamespace;        ///< Counted UTF-8 namespace.
    uint32_t      namespaceLength;   ///< Namespace bytes.
    const char_t* pPayload;          ///< Counted UTF-8 text or opaque binary bytes.
    uint32_t      payloadLength;     ///< Payload bytes.
    bool_t        binary;            ///< TRUE selects payload_binary.
} CastMessage_t;

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
 * \brief Binds a text message to four caller-owned terminated UTF-8 strings.
 *
 * \par Requirements:
 *  - 001 The operation shall allocate nothing, call no CRT and perform no I/O.
 *  - 002 Counted spans shall remain caller-owned and valid for the duration of use.
 *  - 003 Missing caller storage or a missing string shall assert in every build.
 *
 * \param[out] message       Non-NULL application view, disjoint from strings
 * \param[in]  source        Non-NULL terminated source endpoint
 * \param[in]  destination   Non-NULL terminated destination endpoint
 * \param[in]  nameSpace     Non-NULL terminated namespace
 * \param[in]  payload       Non-NULL terminated JSON/text payload
 *
 * \retval     None   No result value.
 */
void CastMessage_Initialize(CastMessage_t* message,
                            const char_t*  source,
                            const char_t*  destination,
                            const char_t*  nameSpace,
                            const char_t*  payload);

/*************************************************************************************************/
/**
 * \brief Compares a counted namespace to a terminated UTF-8 name.
 *
 * \par Requirements:
 *  - 001 The operation shall allocate nothing, call no CRT and perform no I/O.
 *  - 002 Counted spans shall remain caller-owned and valid for the duration of use.
 *
 * \param[in] message     Non-NULL application message
 * \param[in] nameSpace   Non-NULL terminated namespace
 *
 * \retval    TRUE    For identical byte sequences.
 * \retval    FALSE   Otherwise.
 */
bool_t CastMessage_IsNamespace(const CastMessage_t* message, const char_t* nameSpace);

/*************************************************************************************************/
/**
 * \brief Writes a complete CastV2 frame through the specification library.
 *
 * \par Requirements:
 *  - 001 The operation shall allocate nothing, call no CRT and perform no I/O.
 *  - 002 Counted spans shall remain caller-owned and valid for the duration of use.
 *
 * \param[in]  message    Non-NULL counted view with spans disjoint from output
 * \param[out] buffer     Non-NULL writable frame storage
 * \param[in]  capacity   Available bytes
 *
 * \retval     5..UTL_CASTV2_MAX_FRAME   Number of frame bytes written including the length prefix.
 * \retval     0                         The model, output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t CastMessage_Encode(const CastMessage_t* message, byte_t* buffer, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Reads a complete CastV2 body through the specification library.
 *
 * \par Requirements:
 *  - 001 The operation shall allocate nothing, call no CRT and perform no I/O.
 *  - 002 Counted spans shall remain caller-owned and valid for the duration of use.
 *
 * \param[in]  buffer      Non-NULL body bytes without the frame prefix
 * \param[in]  byteCount   Readable body byte count
 * \param[out] message     Non-NULL result, disjoint from input; preserved on failure
 *
 * \retval     TRUE    For a validated supported envelope.
 * \retval     FALSE   Otherwise.
 */
bool_t CastMessage_Decode(const byte_t* buffer, uint32_t byteCount, CastMessage_t* message);

#endif /* CAST_MESSAGE_H */
