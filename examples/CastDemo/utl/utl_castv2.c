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

#include "os/os_compiler.h"
#include "utl/utl_castv2.h"
#include "utl/utl_protobuf.h"
#include "utl/utl_unicode.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define CASTV2_VERSION              (0)
#define CASTV2_FIELD_COUNT          (6)
#define CASTV2_FIELD_VERSION        (1)
#define CASTV2_FIELD_SOURCE         (2)
#define CASTV2_FIELD_DESTINATION    (3)
#define CASTV2_FIELD_NAMESPACE      (4)
#define CASTV2_FIELD_PAYLOAD_TYPE   (5)
#define CASTV2_FIELD_TEXT           (6)
#define CASTV2_FIELD_BINARY         (7)
#define CASTV2_PRESENT_VERSION      (0x01)
#define CASTV2_PRESENT_SOURCE       (0x02)
#define CASTV2_PRESENT_DESTINATION  (0x04)
#define CASTV2_PRESENT_NAMESPACE    (0x08)
#define CASTV2_PRESENT_PAYLOAD_TYPE (0x10)
#define CASTV2_PRESENT_TEXT         (0x20)
#define CASTV2_PRESENT_BINARY       (0x40)
#define CASTV2_REQUIRED_FIELDS      (0x1F)

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
 * \brief Validates a bounded span and, when requested, its UTF-8 syntax.
 *
 * \param[in] span     Non-NULL span
 * \param[in] isText   TRUE for a schema string
 *
 * \retval    TRUE    When the span is readable within the message limit and has valid syntax.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t IsValidSpan(const Utl_CastV2_Span_t* span, bool_t isText);

/*************************************************************************************************/
/**
 * \brief Validates every span and the payload discriminator.
 *
 * \param[in] message   Non-NULL model
 *
 * \retval    TRUE    For valid routing strings, payload syntax and discriminator.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t IsValidMessage(const Utl_CastV2_Message_t* message);

/*************************************************************************************************/
/**
 * \brief Derives one of the six emitted wire records from the typed envelope.
 *
 * \param[out] field     Non-NULL record
 * \param[in]  message   Validated message
 * \param[in]  index     Zero through CASTV2_FIELD_COUNT minus one
 *
 * \retval     None
 */
static FORCE_INLINE void DeriveField(Utl_Protobuf_Field_t* field, const Utl_CastV2_Message_t* message, uint32_t index);

/*************************************************************************************************/
/**
 * \brief Writes a prevalidated message into an exact-size body span.
 *
 * \param[in]  message     Stable validated message with disjoint spans
 * \param[out] buffer      Non-NULL output
 * \param[in]  byteCount   Exact MessageByteCount result
 *
 * \retval     None
 */
static FORCE_INLINE void WriteBody(const Utl_CastV2_Message_t* message, byte_t* buffer, uint32_t byteCount);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The size bound is tested before any payload byte is read.
 */
static FORCE_INLINE bool_t IsValidSpan(const Utl_CastV2_Span_t* span, bool_t isText)
{
    bool_t has_storage;
    bool_t fits_message;
    bool_t is_valid;

    has_storage  = (0 == span->byteCount) || (NULL != span->data);
    fits_message = (UTL_CASTV2_MAX_BODY >= span->byteCount);
    is_valid     = has_storage && fits_message;
    if (is_valid && isText)
    {
        is_valid = Utl_Unicode_IsValidUtf8(span->data, span->byteCount);
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Binary bytes remain opaque. Endpoint and namespace policy belongs to the session layer.
 */
static FORCE_INLINE bool_t IsValidMessage(const Utl_CastV2_Message_t* message)
{
    bool_t is_text;
    bool_t is_binary;
    bool_t is_valid;

    is_text   = (UTL_CASTV2_STRING == message->payloadType);
    is_binary = (UTL_CASTV2_BINARY == message->payloadType);
    is_valid  = is_text || is_binary;
    if (is_valid)
    {
        is_valid = IsValidSpan(&message->source, TRUE);
    }
    if (is_valid)
    {
        is_valid = IsValidSpan(&message->destination, TRUE);
    }
    if (is_valid)
    {
        is_valid = IsValidSpan(&message->nameSpace, TRUE);
    }
    if (is_valid)
    {
        is_valid = IsValidSpan(&message->payload, is_text);
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All record members are assigned; no previous field state is retained.
 */
static FORCE_INLINE void DeriveField(Utl_Protobuf_Field_t* field, const Utl_CastV2_Message_t* message, uint32_t index)
{
    const Utl_CastV2_Span_t* span;

    span                    = NULL;
    field->bytes            = NULL;
    field->integerValue     = 0;
    field->number           = index + 1;
    field->wireType         = UTL_PROTOBUF_VARINT;
    field->byteCount        = 0;
    field->encodedByteCount = 0;
    switch (field->number)
    {
    case CASTV2_FIELD_VERSION:
        field->integerValue = CASTV2_VERSION;
        break;
    case CASTV2_FIELD_SOURCE:
        span = &message->source;
        break;
    case CASTV2_FIELD_DESTINATION:
        span = &message->destination;
        break;
    case CASTV2_FIELD_NAMESPACE:
        span = &message->nameSpace;
        break;
    case CASTV2_FIELD_PAYLOAD_TYPE:
        field->integerValue = (uint32_t)message->payloadType;
        break;
    case CASTV2_FIELD_TEXT:
        span = &message->payload;
        if (UTL_CASTV2_BINARY == message->payloadType)
        {
            field->number = CASTV2_FIELD_BINARY;
        }
        break;
    default:
        break;
    }
    if (NULL != span)
    {
        field->bytes     = span->data;
        field->wireType  = UTL_PROTOBUF_BYTES;
        field->byteCount = span->byteCount;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Six records are emitted; each record's storage was included by the preceding size query.
 */
static FORCE_INLINE void WriteBody(const Utl_CastV2_Message_t* message, byte_t* buffer, uint32_t byteCount)
{
    uint32_t             cursor;
    uint32_t             index;
    Utl_Protobuf_Field_t field;

    cursor = 0;
    for (index = 0; index < CASTV2_FIELD_COUNT; ++index)
    {
        DeriveField(&field, message, index);
        cursor += Utl_Protobuf_WriteField(&field, &buffer[cursor], byteCount - cursor);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Individual spans are bounded before UTF-8 validation; total arithmetic uses uint64_t.
 */
uint32_t Utl_CastV2_GetMessageByteCount(const Utl_CastV2_Message_t* message)
{
    uint64_t             total;
    uint32_t             count;
    bool_t               is_valid;
    uint32_t             index;
    Utl_Protobuf_Field_t field;

    total    = 0;
    count    = 0;
    is_valid = (NULL != message);
    if (is_valid)
    {
        is_valid = IsValidMessage(message);
    }
    if (is_valid)
    {
        for (index = 0; index < CASTV2_FIELD_COUNT; ++index)
        {
            DeriveField(&field, message, index);
            total += Utl_Protobuf_GetFieldByteCount(&field);
        }
        if (UTL_CASTV2_MAX_BODY >= total)
        {
            count = (uint32_t)total;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_CastV2_GetFrameByteCount(const Utl_CastV2_Message_t* message)
{
    uint32_t count;

    count = Utl_CastV2_GetMessageByteCount(message);
    if (0 < count)
    {
        count += UTL_CASTV2_HEADER_BYTES;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only the four-byte prefix is inspected. Subtraction avoids overflowing frame-length arithmetic.
 */
Utl_CastV2_FrameState_t Utl_CastV2_InspectFrame(uint32_t* frameBytes, const byte_t* buffer, uint32_t byteCount)
{
    bool_t                  has_output;
    bool_t                  has_input;
    bool_t                  has_header;
    Utl_CastV2_FrameState_t state;
    uint32_t                body_bytes;
    bool_t                  is_size_valid;
    bool_t                  has_body;

    has_output = (NULL != frameBytes);
    has_input  = (0 == byteCount) || (NULL != buffer);
    has_header = (UTL_CASTV2_HEADER_BYTES <= byteCount);
    state      = UTL_CASTV2_FRAME_INVALID;
    if (has_output && has_input)
    {
        state = UTL_CASTV2_FRAME_INCOMPLETE;
        if (has_header)
        {
            body_bytes     = ((uint32_t)buffer[0] << 24) | ((uint32_t)buffer[1] << 16);
            body_bytes    |= ((uint32_t)buffer[2] << 8) | (uint32_t)buffer[3];
            is_size_valid  = (0 < body_bytes) && (UTL_CASTV2_MAX_BODY >= body_bytes);
            if (is_size_valid)
            {
                has_body = (body_bytes <= (byteCount - UTL_CASTV2_HEADER_BYTES));
                if (has_body)
                {
                    (*frameBytes) = UTL_CASTV2_HEADER_BYTES + body_bytes;
                    state         = UTL_CASTV2_FRAME_COMPLETE;
                }
            }
            else
            {
                state = UTL_CASTV2_FRAME_INVALID;
            }
        }
    }
    return state;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A local result and presence mask distinguish absent fields from empty strings or zero-valued enums.
 *  Every known string is validated while it is read, including overwritten duplicates;
 *  the selected spans need no second validation pass before publication.
 */
bool_t Utl_CastV2_ReadMessage(Utl_CastV2_Message_t* message, const byte_t* buffer, uint32_t byteCount)
{
    bool_t               has_output;
    bool_t               has_input;
    bool_t               has_size;
    bool_t               is_valid;
    uint32_t             cursor;
    uint32_t             present;
    Utl_CastV2_Message_t result;
    Utl_CastV2_Span_t    text_payload;
    Utl_CastV2_Span_t    binary_payload;
    Utl_Protobuf_Field_t field;
    uint32_t             consumed;
    bool_t               is_routing_field;
    bool_t               is_string_field;
    bool_t               has_required;
    bool_t               has_text;
    bool_t               has_binary;
    bool_t               is_text;
    bool_t               has_single_payload;
    bool_t               payload_matches_type;

    has_output                   = (NULL != message);
    has_input                    = (NULL != buffer);
    has_size                     = (0 < byteCount) && (UTL_CASTV2_MAX_BODY >= byteCount);
    is_valid                     = has_output && has_input && has_size;
    cursor                       = 0;
    present                      = 0;
    result.source.data           = NULL;
    result.source.byteCount      = 0;
    result.destination.data      = NULL;
    result.destination.byteCount = 0;
    result.nameSpace.data        = NULL;
    result.nameSpace.byteCount   = 0;
    result.payloadType           = UTL_CASTV2_STRING;
    result.payload.data          = NULL;
    result.payload.byteCount     = 0;
    text_payload.data            = NULL;
    text_payload.byteCount       = 0;
    binary_payload.data          = NULL;
    binary_payload.byteCount     = 0;
    for (;;)
    {
        if (!is_valid || (cursor == byteCount))
        {
            break;
        }
        is_valid = Utl_Protobuf_ReadField(&field, &buffer[cursor], byteCount - cursor);
        if (is_valid)
        {
            consumed = field.encodedByteCount;
            if (UTL_PROTOBUF_GROUP_START == field.wireType)
            {
                is_valid = Utl_Protobuf_SkipField(&consumed, &buffer[cursor], byteCount - cursor);
            }
            else if (UTL_PROTOBUF_GROUP_END == field.wireType)
            {
                is_valid = FALSE;
            }
            else
            {
                is_routing_field = (CASTV2_FIELD_SOURCE <= field.number) && (CASTV2_FIELD_NAMESPACE >= field.number);
                is_string_field  = is_routing_field || (CASTV2_FIELD_TEXT == field.number);
                if (is_string_field && (UTL_PROTOBUF_BYTES == field.wireType))
                {
                    is_valid = Utl_Unicode_IsValidUtf8(field.bytes, field.byteCount);
                }
                switch (field.number)
                {
                case CASTV2_FIELD_VERSION:
                    if ((UTL_PROTOBUF_VARINT == field.wireType) && (CASTV2_VERSION == field.integerValue))
                    {
                        present |= CASTV2_PRESENT_VERSION;
                    }
                    break;
                case CASTV2_FIELD_SOURCE:
                    if (UTL_PROTOBUF_BYTES == field.wireType)
                    {
                        result.source.data       = field.bytes;
                        result.source.byteCount  = field.byteCount;
                        present                 |= CASTV2_PRESENT_SOURCE;
                    }
                    break;
                case CASTV2_FIELD_DESTINATION:
                    if (UTL_PROTOBUF_BYTES == field.wireType)
                    {
                        result.destination.data       = field.bytes;
                        result.destination.byteCount  = field.byteCount;
                        present                      |= CASTV2_PRESENT_DESTINATION;
                    }
                    break;
                case CASTV2_FIELD_NAMESPACE:
                    if (UTL_PROTOBUF_BYTES == field.wireType)
                    {
                        result.nameSpace.data       = field.bytes;
                        result.nameSpace.byteCount  = field.byteCount;
                        present                    |= CASTV2_PRESENT_NAMESPACE;
                    }
                    break;
                case CASTV2_FIELD_PAYLOAD_TYPE:
                    if (UTL_PROTOBUF_VARINT == field.wireType)
                    {
                        if (UTL_CASTV2_BINARY >= field.integerValue)
                        {
                            result.payloadType  = (Utl_CastV2_PayloadType_t)field.integerValue;
                            present            |= CASTV2_PRESENT_PAYLOAD_TYPE;
                        }
                    }
                    break;
                case CASTV2_FIELD_TEXT:
                    if (UTL_PROTOBUF_BYTES == field.wireType)
                    {
                        text_payload.data       = field.bytes;
                        text_payload.byteCount  = field.byteCount;
                        present                |= CASTV2_PRESENT_TEXT;
                    }
                    break;
                case CASTV2_FIELD_BINARY:
                    if (UTL_PROTOBUF_BYTES == field.wireType)
                    {
                        binary_payload.data       = field.bytes;
                        binary_payload.byteCount  = field.byteCount;
                        present                  |= CASTV2_PRESENT_BINARY;
                    }
                    break;
                default:
                    break;
                }
            }
            cursor += consumed;
        }
    }
    has_required         = (CASTV2_REQUIRED_FIELDS == (present & CASTV2_REQUIRED_FIELDS));
    has_text             = !!(present & CASTV2_PRESENT_TEXT);
    has_binary           = !!(present & CASTV2_PRESENT_BINARY);
    is_text              = (UTL_CASTV2_STRING == result.payloadType);
    has_single_payload   = (has_text != has_binary);
    payload_matches_type = (is_text == has_text);
    is_valid             = is_valid && has_required && has_single_payload && payload_matches_type;
    if (is_valid)
    {
        result.payload = binary_payload;
        if (is_text)
        {
            result.payload = text_payload;
        }
        (*message) = result;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The same typed field derivation drives sizing and writing; the validated source must remain stable.
 */
uint32_t Utl_CastV2_WriteMessage(const Utl_CastV2_Message_t* message, byte_t* buffer, uint32_t capacity)
{
    uint32_t body_bytes;
    bool_t   has_buffer;
    bool_t   has_body;
    bool_t   has_capacity;

    body_bytes   = Utl_CastV2_GetMessageByteCount(message);
    has_buffer   = (NULL != buffer);
    has_body     = (0 < body_bytes);
    has_capacity = (body_bytes <= capacity);
    if (has_buffer && has_body && has_capacity)
    {
        WriteBody(message, buffer, body_bytes);
    }
    else
    {
        body_bytes = 0;
    }
    return body_bytes;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The bounded body count makes adding the four-byte header safe before the capacity check.
 */
uint32_t Utl_CastV2_WriteFrame(const Utl_CastV2_Message_t* message, byte_t* buffer, uint32_t capacity)
{
    uint32_t body_bytes;
    uint32_t frame_bytes;
    bool_t   has_buffer;
    bool_t   has_body;
    bool_t   has_capacity;

    body_bytes   = Utl_CastV2_GetMessageByteCount(message);
    frame_bytes  = UTL_CASTV2_HEADER_BYTES + body_bytes;
    has_buffer   = (NULL != buffer);
    has_body     = (0 < body_bytes);
    has_capacity = (frame_bytes <= capacity);
    if (has_buffer && has_body && has_capacity)
    {
        buffer[0] = (byte_t)(body_bytes >> 24);
        buffer[1] = (byte_t)(body_bytes >> 16);
        buffer[2] = (byte_t)(body_bytes >> 8);
        buffer[3] = (byte_t)body_bytes;
        WriteBody(message, &buffer[UTL_CASTV2_HEADER_BYTES], body_bytes);
    }
    else
    {
        frame_bytes = 0;
    }
    return frame_bytes;
}
