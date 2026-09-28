/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Protocol Buffers encoding guide: varints, field tags, wire types and groups.
 * https://protobuf.dev/programming-guides/encoding/
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_compiler.h"
#include "utl/utl_protobuf.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define PROTOBUF_CONTINUATION_BIT (0x80)
#define PROTOBUF_PAYLOAD_MASK     (0x7F)
#define PROTOBUF_PAYLOAD_BITS     (7)
#define PROTOBUF_TAG_WIRE_BITS    (3)
#define PROTOBUF_TAG_WIRE_MASK    (7)

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
 * \brief Counts base-128 digits without writing or reading external storage.
 * \param[in] value   Unsigned integer
 * \retval    1..UTL_PROTOBUF_MAX_VARINT_BYTES   Number of bytes in the shortest encoding of the value.
 */
static FORCE_INLINE uint32_t GetVarintByteCount(uint64_t value);

/*************************************************************************************************/
/**
 * \brief Writes a shortest varint after capacity validation by its caller.
 * \param[out] buffer   Writable output with GetVarintByteCount(value) bytes
 * \param[in]  value    Unsigned integer
 * \retval     1..UTL_PROTOBUF_MAX_VARINT_BYTES   Number of bytes written into the preflighted output span.
 */
static FORCE_INLINE uint32_t WriteVarint(byte_t* buffer, uint64_t value);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Right shifts terminate in at most nine iterations.
 */
static FORCE_INLINE uint32_t GetVarintByteCount(uint64_t value)
{
    uint32_t count;

    count = 1;
    for (;;)
    {
        if (PROTOBUF_PAYLOAD_MASK >= value)
        {
            break;
        }
        value >>= PROTOBUF_PAYLOAD_BITS;
        ++count;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  At least one byte is written, even for zero.
 */
static FORCE_INLINE uint32_t WriteVarint(byte_t* buffer, uint64_t value)
{
    uint32_t count;
    byte_t   encoded;
    bool_t   has_more;

    count = 0;
    for (;;)
    {
        encoded    = (byte_t)(value & PROTOBUF_PAYLOAD_MASK);
        value    >>= PROTOBUF_PAYLOAD_BITS;
        has_more   = !!value;
        if (has_more)
        {
            encoded |= PROTOBUF_CONTINUATION_BIT;
        }
        buffer[count] = encoded;
        ++count;
        if (!(has_more))
        {
            break;
        }
    }
    return count;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Intermediate length arithmetic is 64-bit; the public count is narrowed only
 *  after the complete record has been proven representable.
 */
uint32_t Utl_Protobuf_GetFieldByteCount(const Utl_Protobuf_Field_t* field)
{
    uint32_t count;
    bool_t   has_field;
    bool_t   number_present;
    bool_t   number_fits;
    bool_t   valid;
    uint64_t tag;
    uint64_t total;
    bool_t   has_payload;
    bool_t   size_fits;

    count     = 0;
    has_field = (NULL != field);
    if (has_field)
    {
        number_present = !!field->number;
        number_fits    = (UTL_PROTOBUF_MAX_FIELD_NUMBER >= field->number);
        valid          = number_present && number_fits;
        if (valid)
        {
            tag   = ((uint64_t)field->number << PROTOBUF_TAG_WIRE_BITS) | (uint32_t)field->wireType;
            total = GetVarintByteCount(tag);
            switch (field->wireType)
            {
            case UTL_PROTOBUF_VARINT:
                total += GetVarintByteCount(field->integerValue);
                break;
            case UTL_PROTOBUF_FIXED64:
                total += 8;
                break;
            case UTL_PROTOBUF_BYTES:
                has_payload  = (!field->byteCount) || (NULL != field->bytes);
                valid        = has_payload;
                total       += GetVarintByteCount(field->byteCount);
                total       += field->byteCount;
                break;
            case UTL_PROTOBUF_GROUP_START:
            case UTL_PROTOBUF_GROUP_END:
                break;
            case UTL_PROTOBUF_FIXED32:
                valid  = (UINT32_MAX >= field->integerValue);
                total += 4;
                break;
            default:
                valid = FALSE;
                break;
            }
            size_fits = (UINT32_MAX >= total);
            if (valid && size_fits)
            {
                count = (uint32_t)total;
            }
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The tenth-byte check precedes the shift into bit 63. No failed parse publishes
 *  either the accumulated integer or its partial width.
 */
bool_t Utl_Protobuf_ReadVarint(uint64_t* value, uint32_t* bytesRead, const byte_t* buffer, uint32_t byteCount)
{
    bool_t   has_value;
    bool_t   has_count;
    bool_t   has_buffer;
    bool_t   has_input;
    bool_t   can_read;
    bool_t   decoded_value;
    uint64_t decoded;
    uint32_t limit;
    uint32_t index;
    byte_t   encoded;
    bool_t   fits_value;
    bool_t   has_more;

    has_value     = (NULL != value);
    has_count     = (NULL != bytesRead);
    has_buffer    = (NULL != buffer);
    has_input     = !!byteCount;
    can_read      = has_value && has_count && has_buffer && has_input;
    decoded_value = FALSE;
    if (can_read)
    {
        decoded = 0;
        limit   = byteCount;
        if (UTL_PROTOBUF_MAX_VARINT_BYTES < limit)
        {
            limit = UTL_PROTOBUF_MAX_VARINT_BYTES;
        }
        index = 0;
        for (;;)
        {
            encoded    = buffer[index];
            fits_value = (9 > index) || (2 > encoded);
            if (!fits_value)
            {
                break;
            }
            decoded |= (uint64_t)(encoded & PROTOBUF_PAYLOAD_MASK) << (PROTOBUF_PAYLOAD_BITS * index);
            ++index;
            has_more = !!(PROTOBUF_CONTINUATION_BIT & encoded);
            if (!has_more)
            {
                (*value)      = decoded;
                (*bytesRead)  = index;
                decoded_value = TRUE;
                break;
            }
            if (!(index < limit))
            {
                break;
            }
        }
    }
    return decoded_value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Validation is independent from the subsequent bounded write loop.
 */
uint32_t Utl_Protobuf_WriteVarint(uint64_t value, byte_t* buffer, uint32_t capacity)
{
    uint32_t width;
    uint32_t written;
    bool_t   has_buffer;
    bool_t   has_capacity;
    bool_t   can_write;

    width        = GetVarintByteCount(value);
    written      = 0;
    has_buffer   = (NULL != buffer);
    has_capacity = (width <= capacity);
    can_write    = has_buffer && has_capacity;
    if (can_write)
    {
        written = WriteVarint(buffer, value);
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Counts are subtracted from the available span before each payload access.
 *  A local field records borrowed data only after all bounds have been checked.
 */
bool_t Utl_Protobuf_ReadField(Utl_Protobuf_Field_t* field, const byte_t* buffer, uint32_t byteCount)
{
    bool_t               has_field;
    bool_t               valid;
    uint64_t             tag;
    uint32_t             cursor;
    bool_t               tag_fits;
    bool_t               number_valid;
    uint32_t             wire;
    bool_t               wire_valid;
    Utl_Protobuf_Field_t result;
    uint64_t             length;
    uint32_t             width;
    uint32_t             fixed_bytes;
    bool_t               payload_fits;
    uint32_t             index;

    has_field = (NULL != field);
    valid     = has_field;
    tag       = 0;
    cursor    = 0;
    if (valid)
    {
        valid = Utl_Protobuf_ReadVarint(&tag, &cursor, buffer, byteCount);
    }
    if (valid)
    {
        tag_fits     = (UINT32_MAX >= tag);
        number_valid = !!(tag >> PROTOBUF_TAG_WIRE_BITS);
        wire         = (uint32_t)(tag & PROTOBUF_TAG_WIRE_MASK);
        wire_valid   = (UTL_PROTOBUF_FIXED32 >= wire);
        valid        = tag_fits && number_valid && wire_valid;
        if (valid)
        {
            result.bytes            = NULL;
            result.integerValue     = 0;
            result.number           = (uint32_t)(tag >> PROTOBUF_TAG_WIRE_BITS);
            result.wireType         = (Utl_Protobuf_WireType_t)wire;
            result.byteCount        = 0;
            result.encodedByteCount = 0;
            length                  = 0;
            width                   = 0;
            fixed_bytes             = 0;
            switch (result.wireType)
            {
            case UTL_PROTOBUF_VARINT:
                valid   = Utl_Protobuf_ReadVarint(&result.integerValue, &width, buffer + cursor, byteCount - cursor);
                cursor += width;
                break;
            case UTL_PROTOBUF_FIXED64:
                fixed_bytes = 8;
                break;
            case UTL_PROTOBUF_BYTES:
                valid   = Utl_Protobuf_ReadVarint(&length, &width, buffer + cursor, byteCount - cursor);
                cursor += width;
                if (valid)
                {
                    payload_fits = (length <= (byteCount - cursor));
                    valid        = payload_fits;
                    if (payload_fits)
                    {
                        result.bytes      = buffer + cursor;
                        result.byteCount  = (uint32_t)length;
                        cursor           += result.byteCount;
                    }
                }
                break;
            case UTL_PROTOBUF_GROUP_START:
            case UTL_PROTOBUF_GROUP_END:
                break;
            case UTL_PROTOBUF_FIXED32:
                fixed_bytes = 4;
                break;
            default:
                valid = FALSE;
                break;
            }
            if (fixed_bytes)
            {
                payload_fits = (fixed_bytes <= (byteCount - cursor));
                valid        = payload_fits;
                if (payload_fits)
                {
                    index = 0;
                    for (;;)
                    {
                        result.integerValue |= (uint64_t)buffer[cursor + index] << (8 * index);
                        ++index;
                        if (!(index < fixed_bytes))
                        {
                            break;
                        }
                    }
                    cursor += fixed_bytes;
                }
            }
            if (valid)
            {
                result.encodedByteCount = cursor;
                (*field)                = result;
            }
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  FieldByteCount validates all forms before the writer changes output.
 */
uint32_t Utl_Protobuf_WriteField(const Utl_Protobuf_Field_t* field, byte_t* buffer, uint32_t capacity)
{
    uint32_t required;
    uint32_t written;
    bool_t   has_field;
    bool_t   has_output;
    bool_t   has_capacity;
    bool_t   can_write;
    uint64_t tag;
    uint32_t cursor;
    uint32_t fixed_bytes;
    uint32_t index;
    uint64_t value;

    required     = Utl_Protobuf_GetFieldByteCount(field);
    written      = 0;
    has_field    = !!required;
    has_output   = (NULL != buffer);
    has_capacity = (required <= capacity);
    can_write    = has_field && has_output && has_capacity;
    if (can_write)
    {
        tag         = ((uint64_t)field->number << PROTOBUF_TAG_WIRE_BITS) | (uint32_t)field->wireType;
        cursor      = WriteVarint(buffer, tag);
        fixed_bytes = 0;
        switch (field->wireType)
        {
        case UTL_PROTOBUF_VARINT:
            cursor += WriteVarint(buffer + cursor, field->integerValue);
            break;
        case UTL_PROTOBUF_FIXED64:
            fixed_bytes = 8;
            break;
        case UTL_PROTOBUF_BYTES:
            cursor += WriteVarint(buffer + cursor, field->byteCount);
            for (index = 0; index < field->byteCount; ++index)
            {
                buffer[cursor + index] = field->bytes[index];
            }
            cursor += field->byteCount;
            break;
        case UTL_PROTOBUF_GROUP_START:
        case UTL_PROTOBUF_GROUP_END:
            break;
        case UTL_PROTOBUF_FIXED32:
            fixed_bytes = 4;
            break;
        default:
            break;
        }
        if (fixed_bytes)
        {
            value = field->integerValue;
            index = 0;
            for (;;)
            {
                buffer[cursor + index]   = (byte_t)value;
                value                  >>= 8;
                ++index;
                if (!(index < fixed_bytes))
                {
                    break;
                }
            }
        }
        written = required;
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A fixed stack validates nested group numbers without recursive calls.
 *  The source cursor advances only by a complete validated record.
 */
bool_t Utl_Protobuf_SkipField(uint32_t* bytesRead, const byte_t* buffer, uint32_t byteCount)
{
    bool_t               has_output;
    bool_t               valid;
    Utl_Protobuf_Field_t field;
    uint32_t             cursor;
    bool_t               starts_group;
    bool_t               ends_group;
    uint32_t             group_numbers[UTL_PROTOBUF_MAX_GROUP_DEPTH];
    uint32_t             depth;
    bool_t               has_capacity;
    bool_t               matches_start;

    has_output             = (NULL != bytesRead);
    valid                  = has_output;
    field.number           = 0;
    field.wireType         = UTL_PROTOBUF_VARINT;
    field.encodedByteCount = 0;
    if (valid)
    {
        valid = Utl_Protobuf_ReadField(&field, buffer, byteCount);
    }
    if (valid)
    {
        cursor       = field.encodedByteCount;
        starts_group = (UTL_PROTOBUF_GROUP_START == field.wireType);
        ends_group   = (UTL_PROTOBUF_GROUP_END == field.wireType);
        valid        = !ends_group;
        if (starts_group)
        {
            group_numbers[0] = field.number;
            depth            = 1;
            for (;;)
            {
                valid = Utl_Protobuf_ReadField(&field, buffer + cursor, byteCount - cursor);
                if (!valid)
                {
                    break;
                }
                cursor       += field.encodedByteCount;
                starts_group  = (UTL_PROTOBUF_GROUP_START == field.wireType);
                ends_group    = (UTL_PROTOBUF_GROUP_END == field.wireType);
                if (starts_group)
                {
                    has_capacity = (UTL_PROTOBUF_MAX_GROUP_DEPTH > depth);
                    valid        = has_capacity;
                    if (has_capacity)
                    {
                        group_numbers[depth] = field.number;
                        ++depth;
                    }
                }
                else if (ends_group)
                {
                    --depth;
                    matches_start = (group_numbers[depth] == field.number);
                    valid         = matches_start;
                }
                else
                {
                    /* A scalar or counted record does not change group nesting. */
                }
                if (!(valid && depth))
                {
                    break;
                }
            }
        }
        if (valid)
        {
            (*bytesRead) = cursor;
        }
    }
    return valid;
}
