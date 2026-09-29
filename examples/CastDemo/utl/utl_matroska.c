/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * RFC 8794, sections 4 through 7: EBML variable integers, IDs and element sizes.
 * https://www.rfc-editor.org/rfc/rfc8794.html
 * RFC 9559, sections 4.2 and 4.3: Matroska ID 0x80 and ID/size width constraints.
 * https://www.rfc-editor.org/rfc/rfc9559.html
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_compiler.h"
#include "utl/utl_matroska.h"

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

/* RFC 8794 shortest valid ID ranges, updated by RFC 9559 to include 0x80. */
static const uint32_t c_minimumIds[UTL_MATROSKA_MAX_ID_BYTES] = {0x80, 0x407F, 0x203FFF, 0x101FFFFF};
static const uint32_t c_maximumIds[UTL_MATROSKA_MAX_ID_BYTES] = {0xFE, 0x7FFE, 0x3FFFFE, 0x1FFFFFFE};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Determines the canonical encoded width of a Matroska element ID.
 * \param[in] id   Encoded element ID including its width marker
 * \retval    1..UTL_MATROSKA_MAX_ID_BYTES   Encoded width of the valid Matroska element ID.
 * \retval    0                              The element ID is invalid.
 */
static FORCE_INLINE uint32_t GetIdByteCount(uint32_t id);

/*************************************************************************************************/
/**
 * \brief Writes a validated encoded integer in big-endian byte order.
 * \param[out] buffer      At least byteCount writable bytes
 * \param[in]  byteCount   Width from one through eight
 * \param[in]  value       Encoded integer
 *
 * \retval     None
 */
static FORCE_INLINE void WriteEncoded(byte_t* buffer, uint32_t byteCount, uint64_t value);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Four disjoint ranges enforce both reserved-value and shortest-encoding rules.
 */
static FORCE_INLINE uint32_t GetIdByteCount(uint32_t id)
{
    uint32_t width_index;
    uint32_t result;
    bool_t   above_minimum;
    bool_t   below_maximum;
    bool_t   in_range;

    width_index = 0;
    result      = 0;
    for (;;)
    {
        above_minimum = (c_minimumIds[width_index] <= id);
        below_maximum = (c_maximumIds[width_index] >= id);
        in_range      = above_minimum && below_maximum;
        if (in_range)
        {
            result = width_index + 1;
            break;
        }
        ++width_index;
        if (!(UTL_MATROSKA_MAX_ID_BYTES > width_index))
        {
            break;
        }
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The caller has already validated both the width and the output capacity.
 */
static FORCE_INLINE void WriteEncoded(byte_t* buffer, uint32_t byteCount, uint64_t value)
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
 *  Width detection and decoding are bounded by eight bytes. The marker is kept
 *  separately from its data mask so ID readers can validate the original encoding.
 */
bool_t Utl_Matroska_ReadVint(Utl_Matroska_Vint_t* value, const byte_t* buffer, uint64_t byteCount)
{
    bool_t              has_output;
    bool_t              has_input;
    bool_t              has_first_byte;
    bool_t              is_valid;
    bool_t              has_marker;
    uint32_t            marker;
    uint32_t            width;
    bool_t              marker_found;
    bool_t              width_fits;
    Utl_Matroska_Vint_t result;
    uint32_t            byte_index;
    uint64_t            data_mask;

    has_output     = (NULL != value);
    has_input      = (NULL != buffer);
    has_first_byte = (0 < byteCount);
    is_valid       = has_output && has_input && has_first_byte;
    if (is_valid)
    {
        has_marker = (0 != buffer[0]);
        is_valid   = has_marker;
        if (has_marker)
        {
            marker = 0x80;
            width  = 1;
            for (;;)
            {
                marker_found = !!(marker & buffer[0]);
                if (marker_found)
                {
                    break;
                }
                marker >>= 1;
                ++width;
            }
            width_fits = (width <= byteCount);
            is_valid   = width_fits;
            if (width_fits)
            {
                result.encodedValue = 0;
                byte_index          = 0;
                for (;;)
                {
                    result.encodedValue = (result.encodedValue << 8) | buffer[byte_index];
                    ++byte_index;
                    if (!(byte_index < width))
                    {
                        break;
                    }
                }
                data_mask             = (((uint64_t)1 << (7 * width)) - 1);
                result.value          = result.encodedValue & data_mask;
                result.byteCount      = width;
                result.allDataBitsSet = (data_mask == result.value);
                (*value)              = result;
            }
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Size parsing starts only after the ID width is known to fit. Known payloads
 *  are checked using subtraction so their declared size cannot wrap an end offset.
 */
bool_t Utl_Matroska_ReadElementHeader(Utl_Matroska_ElementHeader_t* header,
                                      const byte_t*                 buffer,
                                      uint64_t                      byteCount,
                                      uint64_t                      remaining)
{
    Utl_Matroska_Vint_t          id;
    Utl_Matroska_Vint_t          size;
    bool_t                       has_output;
    bool_t                       is_valid;
    bool_t                       id_width_fits;
    Utl_Matroska_ElementHeader_t result;
    uint32_t                     expected_id_bytes;
    bool_t                       id_is_canonical;
    bool_t                       header_fits;
    bool_t                       payload_fits;

    id.encodedValue     = 0;
    id.value            = 0;
    id.byteCount        = 0;
    id.allDataBitsSet   = FALSE;
    size.encodedValue   = 0;
    size.value          = 0;
    size.byteCount      = 0;
    size.allDataBitsSet = FALSE;
    has_output          = (NULL != header);
    is_valid            = has_output;
    if (is_valid)
    {
        is_valid = Utl_Matroska_ReadVint(&id, buffer, byteCount);
    }
    if (is_valid)
    {
        id_width_fits = (UTL_MATROSKA_MAX_ID_BYTES >= id.byteCount);
        is_valid      = id_width_fits;
    }
    if (is_valid)
    {
        result.id         = (uint32_t)id.encodedValue;
        expected_id_bytes = GetIdByteCount(result.id);
        id_is_canonical   = (id.byteCount == expected_id_bytes);
        is_valid          = id_is_canonical;
    }
    if (is_valid)
    {
        is_valid = Utl_Matroska_ReadVint(&size, buffer + id.byteCount, byteCount - id.byteCount);
    }
    if (is_valid)
    {
        result.headerByteCount  = id.byteCount + size.byteCount;
        result.sizeByteCount    = size.byteCount;
        result.unknownSize      = size.allDataBitsSet;
        result.payloadByteCount = size.value;
        header_fits             = (result.headerByteCount <= remaining);
        is_valid                = header_fits;
        if (result.unknownSize)
        {
            result.payloadByteCount = 0;
        }
        else if (header_fits)
        {
            payload_fits = (result.payloadByteCount <= (remaining - result.headerByteCount));
            is_valid     = payload_fits;
        }
        else
        {
            /* No payload bound can be established for a header outside its parent. */
        }
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
 *  The selected size width is preserved for round trips and streaming headers.
 *  The unknown sentinel is generated independently of the caller's payload size.
 */
uint32_t Utl_Matroska_WriteElementHeader(const Utl_Matroska_ElementHeader_t* header, byte_t* buffer, uint64_t capacity)
{
    uint32_t written;
    bool_t   has_header;
    bool_t   has_output;
    bool_t   is_valid;
    uint32_t id_bytes;
    bool_t   id_is_valid;
    bool_t   size_width_valid;
    uint32_t header_bytes;
    uint64_t marker;
    uint64_t maximum_size;
    bool_t   payload_fits;
    bool_t   has_capacity;
    uint64_t encoded_size;

    written    = 0;
    has_header = (NULL != header);
    has_output = (NULL != buffer);
    is_valid   = has_header && has_output;
    if (is_valid)
    {
        id_bytes         = GetIdByteCount(header->id);
        id_is_valid      = (0 != id_bytes);
        size_width_valid = (0 < header->sizeByteCount) && (UTL_MATROSKA_MAX_SIZE_BYTES >= header->sizeByteCount);
        is_valid         = id_is_valid && size_width_valid;
        if (is_valid)
        {
            header_bytes = id_bytes + header->sizeByteCount;
            marker       = ((uint64_t)1 << (7 * header->sizeByteCount));
            maximum_size = marker - 1;
            payload_fits = header->unknownSize || (header->payloadByteCount < maximum_size);
            has_capacity = (header_bytes <= capacity);
            is_valid     = payload_fits && has_capacity;
            if (is_valid)
            {
                encoded_size = marker | header->payloadByteCount;
                if (header->unknownSize)
                {
                    encoded_size = marker | maximum_size;
                }
                WriteEncoded(buffer, id_bytes, header->id);
                WriteEncoded(buffer + id_bytes, header->sizeByteCount, encoded_size);
                written = header_bytes;
            }
        }
    }
    return written;
}
