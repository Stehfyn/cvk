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
#include "os/os_assert.h"
#include "os/os_types.h"
#include "utl/utl_protobuf.h"

#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define PROTOBUF_CONTINUATION_BIT    (0x80)
#define PROTOBUF_PAYLOAD_MASK        (0x7F)
#define PROTOBUF_PAYLOAD_BITS        (7)
#define PROTOBUF_TAG_WIRE_BITS       (3)
#define PROTOBUF_TAG_WIRE_MASK       (7)
#define PROTOBUF_BYTE_BIT_COUNT      (8)
#define PROTOBUF_FIXED32_BYTES       (4)
#define PROTOBUF_FIXED64_BYTES       (8)
#define PROTOBUF_LAST_VARINT_INDEX   (UTL_PROTOBUF_MAX_VARINT_BYTES - 1)
#define PROTOBUF_LAST_VARINT_MAXIMUM (1)

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
 * \brief Derives the payload width of either fixed-width wire type.
 * \param[in] wireType   Validated wire type
 * \retval    4,8   Fixed-width payload size in bytes.
 * \retval    0     The wire type does not have a fixed-width payload.
 */
static FORCE_INLINE uint32_t GetFixedByteCount(Utl_Protobuf_WireType_t wireType);

/*************************************************************************************************/
/**
 * \brief Reads the payload following a validated field tag.
 *
 * \par Requirements:
 *  - 001 The caller shall supply a cleared result with a validated wire type.
 *  - 002 The tag width shall not exceed the readable input span.
 *  - 003 Each payload access shall remain inside the counted input.
 *
 * \param[in,out] field      Unpublished field result; discarded by the caller on failure
 * \param[in]     buffer     Counted input including its tag
 * \param[in]     byteCount  Readable input bytes
 * \param[in]     tagWidth   Width of the validated tag
 * \retval        TRUE       A complete payload and record width were decoded.
 * \retval        FALSE      The payload is truncated or invalid.
 */
static FORCE_INLINE bool_t ReadPayload(Utl_Protobuf_Field_t* field,
                                       const byte_t*         buffer,
                                       uint32_t              byteCount,
                                       uint32_t              tagWidth);

/*************************************************************************************************/
/**
 * \brief Writes a shortest varint after capacity validation by its caller.
 * \param[out] buffer   Writable output with GetVarintByteCount(value) bytes
 * \param[in]  value    Unsigned integer
 * \retval     1..UTL_PROTOBUF_MAX_VARINT_BYTES   Number of bytes written into the preflighted output span.
 */
static FORCE_INLINE uint32_t WriteVarint(byte_t* buffer, uint64_t value);

/*************************************************************************************************/
/**
 * \brief Skips the content of an already decoded group-start record.
 *
 * \par Requirements:
 *  - 001 The caller shall supply the cursor after a valid group-start tag.
 *  - 002 A bounded stack shall validate matching group numbers without recursion.
 *  - 003 Failure shall preserve the caller's cursor.
 *
 * \param[in,out] cursor     Cursor immediately after the opening tag
 * \param[in]     number     Opening field number
 * \param[in]     buffer     Counted input including the opening tag
 * \param[in]     byteCount  Readable input bytes
 * \retval        TRUE       The matching group end was reached.
 * \retval        FALSE      Syntax, input bounds or local nesting capacity failed.
 */
static FORCE_INLINE bool_t SkipGroup(uint32_t* cursor, uint32_t number, const byte_t* buffer, uint32_t byteCount);

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
 *  Group markers have no payload; varints and counted bytes have variable widths.
 */
static FORCE_INLINE uint32_t GetFixedByteCount(Utl_Protobuf_WireType_t wireType)
{
    bool_t   is_fixed32;
    bool_t   is_fixed64;
    uint32_t width;

    is_fixed32 = (UTL_PROTOBUF_FIXED32 == wireType);
    is_fixed64 = (UTL_PROTOBUF_FIXED64 == wireType);
    width      = 0;
    if (is_fixed32)
    {
        width = PROTOBUF_FIXED32_BYTES;
    }
    else if (is_fixed64)
    {
        width = PROTOBUF_FIXED64_BYTES;
    }
    else
    {
        /* Other wire types do not carry a fixed-width integer. */
    }
    return width;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All widths are compared with the remaining input before cursor arithmetic.
 *  The enclosing field reader publishes this result only after success.
 */
static FORCE_INLINE bool_t ReadPayload(Utl_Protobuf_Field_t* field,
                                       const byte_t*         buffer,
                                       uint32_t              byteCount,
                                       uint32_t              tagWidth)
{
    uint32_t cursor;
    uint32_t fixed_bytes;
    bool_t   is_varint;
    bool_t   is_bytes;
    bool_t   is_fixed;
    bool_t   valid;
    uint64_t length;
    uint32_t width;
    bool_t   payload_fits;
    uint32_t index;

    LOGASSERT(NULL != field);
    LOGASSERT(NULL != buffer);
    LOGASSERT(tagWidth <= byteCount);

    cursor      = tagWidth;
    fixed_bytes = GetFixedByteCount(field->wireType);
    is_varint   = (UTL_PROTOBUF_VARINT == field->wireType);
    is_bytes    = (UTL_PROTOBUF_BYTES == field->wireType);
    is_fixed    = !!fixed_bytes;
    valid       = TRUE;
    length      = 0;
    width       = 0;
    if (is_varint)
    {
        valid   = Utl_Protobuf_ReadVarint(&field->integerValue, &width, buffer + cursor, byteCount - cursor);
        cursor += width;
    }
    else if (is_bytes)
    {
        valid   = Utl_Protobuf_ReadVarint(&length, &width, buffer + cursor, byteCount - cursor);
        cursor += width;
        if (valid)
        {
            payload_fits = (length <= (byteCount - cursor));
            valid        = payload_fits;
            if (payload_fits)
            {
                field->bytes      = buffer + cursor;
                field->byteCount  = (uint32_t)length;
                cursor           += field->byteCount;
            }
        }
    }
    else if (is_fixed)
    {
        payload_fits = (fixed_bytes <= (byteCount - cursor));
        valid        = payload_fits;
        if (payload_fits)
        {
            for (index = 0; index < fixed_bytes; ++index)
            {
                field->integerValue |= (uint64_t)buffer[cursor + index] << (PROTOBUF_BYTE_BIT_COUNT * index);
            }
            cursor += fixed_bytes;
        }
    }
    else
    {
        /* A validated group marker consists only of its tag. */
    }
    if (valid)
    {
        field->encodedByteCount = cursor;
    }
    return valid;
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
        if (!has_more)
        {
            break;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The stack contains exactly the unmatched opening field numbers. A successful
 *  end marker removes one entry; the outermost end publishes the completed cursor.
 */
static FORCE_INLINE bool_t SkipGroup(uint32_t* cursor, uint32_t number, const byte_t* buffer, uint32_t byteCount)
{
    uint32_t             group_numbers[UTL_PROTOBUF_MAX_GROUP_DEPTH];
    uint32_t             depth;
    uint32_t             offset;
    Utl_Protobuf_Field_t field;
    bool_t               valid;
    bool_t               starts_group;
    bool_t               ends_group;
    bool_t               has_capacity;
    bool_t               matches_start;
    bool_t               has_groups;
    bool_t               read_next;

    LOGASSERT(NULL != cursor);
    LOGASSERT(NULL != buffer);
    LOGASSERT(*cursor <= byteCount);
    LOGASSERT(0 < number);

    group_numbers[0] = number;
    depth            = 1;
    offset           = *cursor;
    for (;;)
    {
        valid = Utl_Protobuf_ReadField(&field, buffer + offset, byteCount - offset);
        if (!valid)
        {
            break;
        }
        offset       += field.encodedByteCount;
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
        has_groups = !!depth;
        read_next  = valid && has_groups;
        if (!read_next)
        {
            break;
        }
    }
    if (valid)
    {
        *cursor = offset;
    }
    return valid;
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
    bool_t   wire_valid;
    bool_t   valid;
    uint64_t tag;
    uint64_t total;
    bool_t   has_payload;
    bool_t   empty_payload;
    bool_t   has_bytes;
    bool_t   is_varint;
    bool_t   is_bytes;
    bool_t   is_fixed32;
    bool_t   size_fits;
    bool_t   can_publish;

    count     = 0;
    has_field = (NULL != field);
    if (has_field)
    {
        number_present = !!field->number;
        number_fits    = (UTL_PROTOBUF_MAX_FIELD_NUMBER >= field->number);
        wire_valid     = (UTL_PROTOBUF_FIXED32 >= (uint32_t)field->wireType);
        valid          = number_present && number_fits && wire_valid;
        if (valid)
        {
            tag         = ((uint64_t)field->number << PROTOBUF_TAG_WIRE_BITS) | (uint32_t)field->wireType;
            total       = GetVarintByteCount(tag);
            total      += GetFixedByteCount(field->wireType);
            is_varint   = (UTL_PROTOBUF_VARINT == field->wireType);
            is_bytes    = (UTL_PROTOBUF_BYTES == field->wireType);
            is_fixed32  = (UTL_PROTOBUF_FIXED32 == field->wireType);
            if (is_varint)
            {
                total += GetVarintByteCount(field->integerValue);
            }
            else if (is_bytes)
            {
                empty_payload  = !field->byteCount;
                has_bytes      = (NULL != field->bytes);
                has_payload    = empty_payload || has_bytes;
                valid          = has_payload;
                total         += GetVarintByteCount(field->byteCount);
                total         += field->byteCount;
            }
            else if (is_fixed32)
            {
                valid = (UINT32_MAX >= field->integerValue);
            }
            else
            {
                /* Fixed64 values and group markers need no additional validation. */
            }
            size_fits   = (UINT32_MAX >= total);
            can_publish = valid && size_fits;
            if (can_publish)
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
    bool_t   precedes_last_byte;
    bool_t   fits_last_byte;
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
            encoded            = buffer[index];
            precedes_last_byte = (PROTOBUF_LAST_VARINT_INDEX > index);
            fits_last_byte     = (PROTOBUF_LAST_VARINT_MAXIMUM >= encoded);
            fits_value         = precedes_last_byte || fits_last_byte;
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
            valid                   = ReadPayload(&result, buffer, byteCount, cursor);
            if (valid)
            {
                (*field) = result;
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
    bool_t   is_varint;
    bool_t   is_bytes;
    bool_t   is_fixed;
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
        fixed_bytes = GetFixedByteCount(field->wireType);
        is_varint   = (UTL_PROTOBUF_VARINT == field->wireType);
        is_bytes    = (UTL_PROTOBUF_BYTES == field->wireType);
        is_fixed    = !!fixed_bytes;
        if (is_varint)
        {
            (void)WriteVarint(buffer + cursor, field->integerValue);
        }
        else if (is_bytes)
        {
            cursor += WriteVarint(buffer + cursor, field->byteCount);
            for (index = 0; index < field->byteCount; ++index)
            {
                buffer[cursor + index] = field->bytes[index];
            }
        }
        else if (is_fixed)
        {
            value = field->integerValue;
            index = 0;
            for (;;)
            {
                buffer[cursor + index]   = (byte_t)value;
                value                  >>= PROTOBUF_BYTE_BIT_COUNT;
                ++index;
                if (!(index < fixed_bytes))
                {
                    break;
                }
            }
        }
        else
        {
            /* A group marker has no payload bytes. */
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
            valid = SkipGroup(&cursor, field.number, buffer, byteCount);
        }
        if (valid)
        {
            (*bytesRead) = cursor;
        }
    }
    return valid;
}
