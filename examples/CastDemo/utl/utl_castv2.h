#ifndef UTL_CASTV2_H
#define UTL_CASTV2_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * CastV2 1.0 CastMessage schema, fields 1 through 7:
 * https://chromium.googlesource.com/chromium/chromium/+/HEAD/chrome/browser/extensions/api/cast_channel/cast_channel.proto
 * Schema blob fd226541d5abdf22730b462ec9de946aa9b021d4, reviewed 2026-09-27.
 * Chromium MessageFramer, network-order length and 65536-byte implementation limit:
 * https://chromium.googlesource.com/chromium/src/+/73.0.3673.0/components/cast_channel/cast_framer.cc
 *
 * This module implements the version-1.0 envelope used by CastDemo, not the
 * authentication schema, JSON namespaces or later chunked-message protocols.
 * Payload and routing spans are borrowed. No allocation, CRT, I/O or retained state.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UTL_CASTV2_HEADER_BYTES (4)     ///< Network-order body length prefix.
#define UTL_CASTV2_MAX_BODY     (65536) ///< Chromium-compatible implementation capacity.
#define UTL_CASTV2_MAX_FRAME    (UTL_CASTV2_HEADER_BYTES + UTL_CASTV2_MAX_BODY)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Version-1.0 payload discriminator, matching the schema enum. */
typedef enum Utl_CastV2_PayloadType_e
{
    UTL_CASTV2_STRING = 0,
    UTL_CASTV2_BINARY = 1
} Utl_CastV2_PayloadType_t;

/*************************************************************************************************/
/** \brief Counted borrowed bytes, without a terminator or ownership transfer. */
typedef struct Utl_CastV2_Span_s
{
    const byte_t* data;      ///< Readable bytes; NULL is allowed only for an empty span.
    uint32_t      byteCount; ///< Span size in bytes.
} Utl_CastV2_Span_t;

/*************************************************************************************************/
/**
 * \brief Typed CastMessage version-1.0 envelope.
 *
 * \par Details:
 *  protocol_version is fixed at CASTV2_1_0 (zero). All routing fields and the
 *  payload_type field are required on input. payloadType selects exactly one
 *  payload field; text uses valid UTF-8, binary is uninterpreted.
 *  Routing strings may be empty at the schema layer. The session owns endpoint
 *  and namespace policy. Embedded U+0000 is counted data, not a terminator.
 */
typedef struct Utl_CastV2_Message_s
{
    Utl_CastV2_Span_t        source;      ///< UTF-8 source_id, field 2.
    Utl_CastV2_Span_t        destination; ///< UTF-8 destination_id, field 3.
    Utl_CastV2_Span_t        nameSpace;   ///< UTF-8 namespace, field 4.
    Utl_CastV2_PayloadType_t payloadType; ///< STRING or BINARY, field 5.
    Utl_CastV2_Span_t        payload;     ///< UTF-8 field 6 or binary field 7.
} Utl_CastV2_Message_t;

/*************************************************************************************************/
/** \brief Result of inspecting the first frame in a counted byte stream. */
typedef enum Utl_CastV2_FrameState_e
{
    UTL_CASTV2_FRAME_INVALID    = -1,
    UTL_CASTV2_FRAME_INCOMPLETE = 0,
    UTL_CASTV2_FRAME_COMPLETE   = 1
} Utl_CastV2_FrameState_t;

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
 * \brief Derives a validated envelope's shortest protobuf size.
 *
 * \par Requirements:
 *  - 001 Invalid spans, invalid UTF-8, unsupported payload types or excessive size shall yield zero.
 *  - 002 The query shall allocate nothing and perform no I/O.
 *
 * \param[in] message   Non-NULL counted message; readable spans
 *
 * \retval    1..UTL_CASTV2_MAX_BODY   Encoded envelope byte count.
 * \retval    0                        The model is invalid or exceeds the body limit.
 */
uint32_t Utl_CastV2_GetMessageByteCount(const Utl_CastV2_Message_t* message);

/*************************************************************************************************/
/**
 * \brief Derives a validated envelope's framed size.
 *
 * \par Requirements:
 *  - 001 An invalid body shall yield zero.
 *  - 002 A valid result shall include the four-byte length prefix.
 *
 * \param[in] message   Non-NULL counted message
 *
 * \retval    5..UTL_CASTV2_MAX_FRAME   Encoded frame byte count including its length prefix.
 * \retval    0                         The model is invalid or exceeds the body limit.
 */
uint32_t Utl_CastV2_GetFrameByteCount(const Utl_CastV2_Message_t* message);

/*************************************************************************************************/
/**
 * \brief Classifies a possibly fragmented or coalesced frame without consuming it.
 *
 * \par Requirements:
 *  - 001 A partial header or body shall return INCOMPLETE without changing frameBytes.
 *  - 002 Zero or oversized body lengths shall return INVALID.
 *  - 003 A complete frame shall report only its first frame's width; trailing frames remain unconsumed.
 *  - 004 The body shall not be parsed or copied.
 *
 * \param[out] frameBytes   Non-NULL first-frame width; preserved unless complete
 * \param[in]  buffer       Readable bytes; NULL allowed only when byteCount is zero
 * \param[in]  byteCount    Available stream bytes
 *
 * \retval     UTL_CASTV2_FRAME_INVALID      The arguments or declared frame length are invalid.
 * \retval     UTL_CASTV2_FRAME_INCOMPLETE   More bytes are required for the first frame.
 * \retval     UTL_CASTV2_FRAME_COMPLETE     The first frame is available; frameBytes contains its complete size.
 */
Utl_CastV2_FrameState_t Utl_CastV2_InspectFrame(uint32_t* frameBytes, const byte_t* buffer, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Reads a complete version-1.0 protobuf envelope into a borrowed typed model.
 *
 * \par Requirements:
 *  - 001 All five required schema fields and exactly one matching payload field shall be present.
 *  - 002 Known singular fields shall use the last correctly typed value; unsupported enum values shall be skipped as unknown proto2 values.
 *  - 003 Unknown fields, including balanced groups within the protobuf depth bound, shall be skipped.
 *  - 004 Strings shall contain valid UTF-8; malformed wire data shall fail without publishing partial output.
 *  - 005 Returned spans shall borrow buffer until its contents are changed or its storage expires.
 *
 * \param[out] message     Non-NULL output, disjoint from input; unchanged on failure
 * \param[in]  buffer      Non-NULL readable body, without the length prefix
 * \param[in]  byteCount   Complete body size, at most UTL_CASTV2_MAX_BODY
 *
 * \retval     TRUE    For a complete supported envelope.
 * \retval     FALSE   Otherwise.
 */
bool_t Utl_CastV2_ReadMessage(Utl_CastV2_Message_t* message, const byte_t* buffer, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Serializes a validated version-1.0 protobuf envelope.
 *
 * \par Requirements:
 *  - 001 Validation and complete capacity checks shall precede any output write.
 *  - 002 Output shall use ascending field numbers and shortest varints.
 *  - 003 Failure shall return zero and preserve the complete output buffer.
 *
 * \param[in]  message    Non-NULL model with readable spans disjoint from output
 * \param[out] buffer     Non-NULL writable body storage
 * \param[in]  capacity   Writable byte count
 *
 * \retval     1..UTL_CASTV2_MAX_BODY   Number of encoded envelope bytes written.
 * \retval     0                        The model, output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t Utl_CastV2_WriteMessage(const Utl_CastV2_Message_t* message, byte_t* buffer, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Serializes one complete CastV2 frame.
 *
 * \par Requirements:
 *  - 001 A four-byte network-order body length shall precede the protobuf body.
 *  - 002 Validation and capacity failures shall preserve the complete output buffer.
 *  - 003 No allocation, CRT or I/O shall occur.
 *
 * \param[in]  message    Non-NULL model with readable spans disjoint from output
 * \param[out] buffer     Non-NULL writable frame storage
 * \param[in]  capacity   Writable byte count
 *
 * \retval     5..UTL_CASTV2_MAX_FRAME   Number of frame bytes written including the length prefix.
 * \retval     0                         The model, output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t Utl_CastV2_WriteFrame(const Utl_CastV2_Message_t* message, byte_t* buffer, uint32_t capacity);

#endif /* UTL_CASTV2_H */
