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
#include "os/os_assert.h"
#include "os/os_types.h"
#include "utl/utl_castv2.h"
#include "utl/utl_protobuf.h"
#include "utl/utl_unicode.h"

#include <stdint.h>

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
#define CASTV2_LENGTH_HIGH_SHIFT    (24)
#define CASTV2_LENGTH_MIDDLE_SHIFT  (16)
#define CASTV2_LENGTH_LOW_SHIFT     (8)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Unpublished envelope with independent presence tracking for both payload forms. */
typedef struct MessageReader_s
{
    Utl_CastV2_Message_t message;       ///< Routing fields and payload discriminator.
    Utl_CastV2_Span_t    textPayload;   ///< Last text payload, including an empty value.
    Utl_CastV2_Span_t    binaryPayload; ///< Last binary payload, including an empty value.
    uint32_t             present;       ///< Accepted known field numbers.
} MessageReader_t;

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
 * \brief Initializes the private candidate for a single envelope read.
 * \par Requirements:
 *  - 001 The caller shall supply writable reader storage.
 *  - 002 No previous field or presence state shall be retained.
 * \param[out] reader   Candidate reader
 * \retval     None No value is returned.
 */
static FORCE_INLINE void InitializeReader(MessageReader_t* reader);

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
 * \retval     None No value is returned.
 */
static FORCE_INLINE void DeriveField(Utl_Protobuf_Field_t* field, const Utl_CastV2_Message_t* message, uint32_t index);

/*************************************************************************************************/
/**
 * \brief Reads a length-delimited field according to its envelope schema number.
 * \par Requirements:
 *  - 001 Every recognized string shall be valid UTF-8, including overwritten duplicates.
 *  - 002 Unknown field numbers shall be ignored without interpreting their payload.
 *  - 003 A recognized field shall be published to the candidate only after validation.
 * \param[in,out] reader   Initialized unpublished candidate
 * \param[in]     field    Decoded protobuf BYTES record
 * \retval        TRUE     A recognized field is valid, or the field number is unknown.
 * \retval        FALSE    A recognized string has invalid UTF-8.
 */
static FORCE_INLINE bool_t ReadBytesField(MessageReader_t* reader, const Utl_Protobuf_Field_t* field);

/*************************************************************************************************/
/**
 * \brief Reads a version or payload-discriminator varint.
 * \par Requirements:
 *  - 001 Unrecognized numbers and enum values shall leave candidate fields unchanged.
 *  - 002 Accepted values shall update their corresponding presence bits.
 * \param[in,out] reader   Initialized unpublished candidate
 * \param[in]     field    Decoded protobuf VARINT record
 * \retval        None No value is returned.
 */
static FORCE_INLINE void ReadIntegerField(MessageReader_t* reader, const Utl_Protobuf_Field_t* field);

/*************************************************************************************************/
/**
 * \brief Writes a prevalidated message into an exact-size body span.
 *
 * \param[in]  message     Stable validated message with disjoint spans
 * \param[out] buffer      Non-NULL output
 * \param[in]  byteCount   Exact MessageByteCount result
 *
 * \retval     None No value is returned.
 */
static FORCE_INLINE void WriteBody(const Utl_CastV2_Message_t* message, byte_t* buffer, uint32_t byteCount);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Presence is independent of zero-valued enums and empty borrowed spans.
 */
static FORCE_INLINE void InitializeReader(MessageReader_t* reader)
{
    Utl_CastV2_Span_t empty;

    LOGASSERT(NULL != reader);
    empty.data                  = NULL;
    empty.byteCount             = 0;
    reader->message.source      = empty;
    reader->message.destination = empty;
    reader->message.nameSpace   = empty;
    reader->message.payload     = empty;
    reader->message.payloadType = UTL_CASTV2_STRING;
    reader->textPayload         = empty;
    reader->binaryPayload       = empty;
    reader->present             = 0;
}

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
 *  Only schema string fields undergo Unicode validation. The binary payload and
 *  unknown fields remain opaque; duplicate recognized fields replace prior spans.
 */
static FORCE_INLINE bool_t ReadBytesField(MessageReader_t* reader, const Utl_Protobuf_Field_t* field)
{
    Utl_CastV2_Span_t* span;
    uint32_t           presence;
    bool_t             is_text;
    bool_t             is_known;
    bool_t             needs_validation;
    bool_t             is_valid;
    bool_t             can_publish;

    LOGASSERT(NULL != reader);
    LOGASSERT(NULL != field);
    LOGASSERT(UTL_PROTOBUF_BYTES == field->wireType);

    span     = NULL;
    presence = 0;
    is_text  = TRUE;
    switch (field->number)
    {
    case CASTV2_FIELD_SOURCE:
        span     = &reader->message.source;
        presence = CASTV2_PRESENT_SOURCE;
        break;
    case CASTV2_FIELD_DESTINATION:
        span     = &reader->message.destination;
        presence = CASTV2_PRESENT_DESTINATION;
        break;
    case CASTV2_FIELD_NAMESPACE:
        span     = &reader->message.nameSpace;
        presence = CASTV2_PRESENT_NAMESPACE;
        break;
    case CASTV2_FIELD_TEXT:
        span     = &reader->textPayload;
        presence = CASTV2_PRESENT_TEXT;
        break;
    case CASTV2_FIELD_BINARY:
        span     = &reader->binaryPayload;
        presence = CASTV2_PRESENT_BINARY;
        is_text  = FALSE;
        break;
    default:
        break;
    }
    is_known         = (NULL != span);
    needs_validation = is_known && is_text;
    is_valid         = TRUE;
    if (needs_validation)
    {
        is_valid = Utl_Unicode_IsValidUtf8(field->bytes, field->byteCount);
    }
    can_publish = is_known && is_valid;
    if (can_publish)
    {
        span->data       = field->bytes;
        span->byteCount  = field->byteCount;
        reader->present |= presence;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Unknown enum values follow protobuf unknown-field behavior and do not erase
 *  a previously accepted value of the same field.
 */
static FORCE_INLINE void ReadIntegerField(MessageReader_t* reader, const Utl_Protobuf_Field_t* field)
{
    bool_t is_version;
    bool_t valid_version;
    bool_t accepts_version;
    bool_t is_payload_type;
    bool_t valid_payload_type;
    bool_t accepts_payload_type;

    LOGASSERT(NULL != reader);
    LOGASSERT(NULL != field);
    LOGASSERT(UTL_PROTOBUF_VARINT == field->wireType);

    is_version           = (CASTV2_FIELD_VERSION == field->number);
    valid_version        = (CASTV2_VERSION == field->integerValue);
    accepts_version      = is_version && valid_version;
    is_payload_type      = (CASTV2_FIELD_PAYLOAD_TYPE == field->number);
    valid_payload_type   = (UTL_CASTV2_BINARY >= field->integerValue);
    accepts_payload_type = is_payload_type && valid_payload_type;
    if (accepts_version)
    {
        reader->present |= CASTV2_PRESENT_VERSION;
    }
    if (accepts_payload_type)
    {
        reader->message.payloadType  = (Utl_CastV2_PayloadType_t)field->integerValue;
        reader->present             |= CASTV2_PRESENT_PAYLOAD_TYPE;
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
extern FORCE_INLINE uint32_t Utl_CastV2_GetFrameByteCount(const Utl_CastV2_Message_t* message)
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
            body_bytes =
                ((uint32_t)buffer[0] << CASTV2_LENGTH_HIGH_SHIFT) | ((uint32_t)buffer[1] << CASTV2_LENGTH_MIDDLE_SHIFT);
            body_bytes    |= ((uint32_t)buffer[2] << CASTV2_LENGTH_LOW_SHIFT) | (uint32_t)buffer[3];
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
    bool_t               has_body;
    bool_t               fits_body;
    bool_t               is_valid;
    uint32_t             cursor;
    MessageReader_t      reader;
    Utl_Protobuf_Field_t field;
    uint32_t             consumed;
    bool_t               at_end;
    bool_t               stop_reading;
    bool_t               starts_group;
    bool_t               ends_group;
    bool_t               is_bytes;
    bool_t               is_varint;
    bool_t               has_required;
    bool_t               has_text;
    bool_t               has_binary;
    bool_t               is_text;
    bool_t               has_single_payload;
    bool_t               payload_matches_type;

    has_output = (NULL != message);
    has_input  = (NULL != buffer);
    has_body   = (0 < byteCount);
    fits_body  = (UTL_CASTV2_MAX_BODY >= byteCount);
    is_valid   = has_output && has_input && has_body && fits_body;
    cursor     = 0;
    InitializeReader(&reader);
    for (;;)
    {
        at_end       = (cursor == byteCount);
        stop_reading = !is_valid || at_end;
        if (stop_reading)
        {
            break;
        }
        is_valid = Utl_Protobuf_ReadField(&field, &buffer[cursor], byteCount - cursor);
        if (is_valid)
        {
            consumed     = field.encodedByteCount;
            starts_group = (UTL_PROTOBUF_GROUP_START == field.wireType);
            ends_group   = (UTL_PROTOBUF_GROUP_END == field.wireType);
            is_bytes     = (UTL_PROTOBUF_BYTES == field.wireType);
            is_varint    = (UTL_PROTOBUF_VARINT == field.wireType);
            if (starts_group)
            {
                is_valid = Utl_Protobuf_SkipField(&consumed, &buffer[cursor], byteCount - cursor);
            }
            else if (ends_group)
            {
                is_valid = FALSE;
            }
            else if (is_bytes)
            {
                is_valid = ReadBytesField(&reader, &field);
            }
            else if (is_varint)
            {
                ReadIntegerField(&reader, &field);
            }
            else
            {
                /* Fixed-width fields are unknown to the retained CastV2 schema. */
            }
            cursor += consumed;
        }
    }
    has_required         = (CASTV2_REQUIRED_FIELDS == (reader.present & CASTV2_REQUIRED_FIELDS));
    has_text             = !!(reader.present & CASTV2_PRESENT_TEXT);
    has_binary           = !!(reader.present & CASTV2_PRESENT_BINARY);
    is_text              = (UTL_CASTV2_STRING == reader.message.payloadType);
    has_single_payload   = (has_text != has_binary);
    payload_matches_type = (is_text == has_text);
    is_valid             = is_valid && has_required && has_single_payload && payload_matches_type;
    if (is_valid)
    {
        reader.message.payload = reader.binaryPayload;
        if (is_text)
        {
            reader.message.payload = reader.textPayload;
        }
        (*message) = reader.message;
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
extern FORCE_INLINE uint32_t Utl_CastV2_WriteFrame(const Utl_CastV2_Message_t* message, byte_t* buffer, uint32_t capacity)
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
        buffer[0] = (byte_t)(body_bytes >> CASTV2_LENGTH_HIGH_SHIFT);
        buffer[1] = (byte_t)(body_bytes >> CASTV2_LENGTH_MIDDLE_SHIFT);
        buffer[2] = (byte_t)(body_bytes >> CASTV2_LENGTH_LOW_SHIFT);
        buffer[3] = (byte_t)body_bytes;
        WriteBody(message, &buffer[UTL_CASTV2_HEADER_BYTES], body_bytes);
    }
    else
    {
        frame_bytes = 0;
    }
    return frame_bytes;
}
