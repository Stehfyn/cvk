/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * ISO/IEC 14496-12 Box syntax; see utl_mp4.h for the reference source.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_compiler.h"
#include "utl/utl_mp4.h"

/*=================================================================================================
** 3. DECLARATIONS
** 
** 3.1 Macros
**===============================================================================================*/

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
 * \brief Reads a big-endian integer from a validated one-to-eight-byte span.
 * \param[in] buffer      Readable bytes
 * \param[in] byteCount   Integer width, between one and eight
 * \retval    0..UINT64_MAX   Unsigned integer decoded from the validated big-endian span.
 */
static FORCE_INLINE uint64_t ReadUnsigned(const byte_t* buffer, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Writes the low byteCount bytes in big-endian order.
 * \param[out] buffer      Writable bytes
 * \param[in]  byteCount   Integer width, between one and eight
 * \param[in]  value       Value to encode
 *
 * \retval     None   No result value.
 */
static FORCE_INLINE void WriteUnsigned(byte_t* buffer, uint32_t byteCount, uint64_t value);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The callers establish the fixed integer widths before entering this loop.
 */
static FORCE_INLINE uint64_t ReadUnsigned(const byte_t* buffer, uint32_t byteCount)
{
    uint64_t value;
    uint32_t byte_index;

    value      = 0;
    byte_index = 0;
    for (;;)
    {
        value = (value << 8) | buffer[byte_index];
        ++byte_index;
        if (!(byte_index < byteCount))
        {
            break;
        }
    }
    return value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Writes from the last byte toward the first without a shift of 64 bits.
 */
static FORCE_INLINE void WriteUnsigned(byte_t* buffer, uint32_t byteCount, uint64_t value)
{
    uint32_t byte_index;

    byte_index = byteCount;
    for (;;)
    {
        --byte_index;
        buffer[byte_index]   = (byte_t)value;
        value              >>= 8;
        if (!(0 != byte_index))
        {
            break;
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  All size arithmetic uses enclosing-boundary subtraction before addition.
 *  A local result prevents malformed data from partially changing the output.
 */
bool_t Utl_Mp4_ReadBoxHeader(Utl_Mp4_BoxHeader_t* header, const byte_t* buffer, uint64_t byteCount, uint64_t remaining)
{
    Utl_Mp4_BoxHeader_t result;
    uint32_t            size_field;
    uint32_t            uuid_index;
    uint32_t            uuid_offset;
    bool_t              has_output;
    bool_t              has_input;
    bool_t              has_base_header;
    bool_t              is_uuid;
    bool_t              header_fits;
    bool_t              size_includes_header;
    bool_t              size_fits_parent;
    bool_t              is_valid;

    has_output      = (NULL != header);
    has_input       = (NULL != buffer);
    has_base_header = (UTL_MP4_BOX_HEADER_BYTES <= byteCount);
    is_valid        = has_output && has_input && has_base_header;
    if (is_valid)
    {
        size_field             = (uint32_t)ReadUnsigned(buffer, 4);
        result.type            = (uint32_t)ReadUnsigned(buffer + 4, 4);
        result.extendsToEnd    = (0 == size_field);
        result.hasLargeSize    = (1 == size_field);
        result.headerByteCount = UTL_MP4_BOX_HEADER_BYTES;
        result.byteCount       = size_field;
        if (result.hasLargeSize)
        {
            result.headerByteCount = UTL_MP4_LARGE_BOX_HEADER_BYTES;
            header_fits            = (result.headerByteCount <= byteCount);
            is_valid               = header_fits;
            if (header_fits)
            {
                result.byteCount = ReadUnsigned(buffer + UTL_MP4_BOX_HEADER_BYTES, 8);
            }
        }
        else if (result.extendsToEnd)
        {
            result.byteCount = remaining;
        }
        else
        {
            /* The 32-bit size field already supplies the complete box size. */
        }
        uuid_index = 0;
        for (;;)
        {
            result.userType[uuid_index] = 0;
            ++uuid_index;
            if (!(UTL_MP4_USER_TYPE_BYTES > uuid_index))
            {
                break;
            }
        }
        is_uuid = (UTL_MP4_BOX_UUID == result.type);
        if (is_uuid)
        {
            uuid_offset             = result.headerByteCount;
            result.headerByteCount += UTL_MP4_USER_TYPE_BYTES;
            header_fits             = (result.headerByteCount <= byteCount);
            is_valid                = is_valid && header_fits;
            if (header_fits)
            {
                uuid_index = 0;
                for (;;)
                {
                    result.userType[uuid_index] = buffer[uuid_offset + uuid_index];
                    ++uuid_index;
                    if (!(UTL_MP4_USER_TYPE_BYTES > uuid_index))
                    {
                        break;
                    }
                }
            }
        }
        size_includes_header = (result.headerByteCount <= result.byteCount);
        size_fits_parent     = (result.byteCount <= remaining);
        is_valid             = is_valid && size_includes_header && size_fits_parent;
        if (is_valid)
        {
            (*header) = result;
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Validation is complete before output begins. The output loop has a fixed
 *  upper bound of sixteen UUID bytes; the other fields have fixed widths.
 */
uint32_t Utl_Mp4_WriteBoxHeader(const Utl_Mp4_BoxHeader_t* header, byte_t* buffer, uint64_t capacity)
{
    uint32_t written;
    uint32_t header_bytes;
    uint32_t size_field;
    uint32_t uuid_offset;
    uint32_t uuid_index;
    bool_t   has_header;
    bool_t   has_output;
    bool_t   is_uuid;
    bool_t   conflicting_sizes;
    bool_t   size_fits_field;
    bool_t   size_includes_header;
    bool_t   has_capacity;
    bool_t   is_valid;

    written    = 0;
    has_header = (NULL != header);
    has_output = (NULL != buffer);
    is_valid   = has_header && has_output;
    if (is_valid)
    {
        header_bytes = UTL_MP4_BOX_HEADER_BYTES;
        size_field   = (uint32_t)header->byteCount;
        if (header->hasLargeSize)
        {
            header_bytes = UTL_MP4_LARGE_BOX_HEADER_BYTES;
            size_field   = 1;
        }
        if (header->extendsToEnd)
        {
            size_field = 0;
        }
        uuid_offset = header_bytes;
        is_uuid     = (UTL_MP4_BOX_UUID == header->type);
        if (is_uuid)
        {
            header_bytes += UTL_MP4_USER_TYPE_BYTES;
        }
        conflicting_sizes    = header->hasLargeSize && header->extendsToEnd;
        size_fits_field      = header->hasLargeSize || header->extendsToEnd || (UINT32_MAX >= header->byteCount);
        size_includes_header = (header_bytes <= header->byteCount);
        has_capacity         = (header_bytes <= capacity);
        is_valid             = (!conflicting_sizes) && size_fits_field && size_includes_header && has_capacity;
        if (is_valid)
        {
            WriteUnsigned(buffer, 4, size_field);
            WriteUnsigned(buffer + 4, 4, header->type);
            if (header->hasLargeSize)
            {
                WriteUnsigned(buffer + UTL_MP4_BOX_HEADER_BYTES, 8, header->byteCount);
            }
            if (is_uuid)
            {
                uuid_index = 0;
                for (;;)
                {
                    buffer[uuid_offset + uuid_index] = header->userType[uuid_index];
                    ++uuid_index;
                    if (!(UTL_MP4_USER_TYPE_BYTES > uuid_index))
                    {
                        break;
                    }
                }
            }
            written = header_bytes;
        }
    }
    return written;
}
