/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Counted ASCII unsigned-decimal representation used by HTTP field values.
 * RFC 9110 section 8.6: https://www.rfc-editor.org/rfc/rfc9110.html#section-8.6
 * The uint64_t result is an application-domain limit, not an HTTP grammar limit.
 * Callers decide whether the parsed prefix must consume their entire input.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_decimal.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define DECIMAL_RADIX (10)

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

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Repeated division terminates after at most nineteen iterations.
 */
uint32_t Utl_Decimal_GetUint64ByteCount(uint64_t value)
{
    uint32_t count;

    count = 1;
    for (;;)
    {
        if (DECIMAL_RADIX > value)
        {
            break;
        }
        value /= DECIMAL_RADIX;
        ++count;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each digit is checked against the remaining uint64_t range before multiplication.
 */
bool_t Utl_Decimal_ReadUint64(uint64_t* value, uint32_t* bytesRead, const char_t* buffer, uint32_t byteCount)
{
    bool_t   has_outputs;
    bool_t   has_input;
    bool_t   is_valid;
    uint32_t cursor;
    uint64_t number;
    char_t   current;
    bool_t   is_digit;
    uint64_t digit;
    bool_t   fits_value;

    has_outputs = (NULL != value) && (NULL != bytesRead);
    has_input   = (NULL != buffer) && (0 < byteCount);
    is_valid    = has_outputs && has_input;
    cursor      = 0;
    number      = 0;
    if (is_valid)
    {
        for (;;)
        {
            current  = buffer[cursor];
            is_digit = (UTL_DECIMAL_CHAR_FIRST <= current) && (UTL_DECIMAL_CHAR_LAST >= current);
            if (!is_digit)
            {
                break;
            }
            digit      = (uint64_t)current - (uint64_t)UTL_DECIMAL_CHAR_FIRST;
            fits_value = (number <= ((UINT64_MAX - digit) / DECIMAL_RADIX));
            if (!fits_value)
            {
                is_valid = FALSE;
                break;
            }
            number = (number * DECIMAL_RADIX) + digit;
            ++cursor;
            if (byteCount == cursor)
            {
                break;
            }
        }
        is_valid = is_valid && (0 < cursor);
    }
    if (is_valid)
    {
        (*value)     = number;
        (*bytesRead) = cursor;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Digits are written from right to left within the exact precomputed width.
 */
uint32_t Utl_Decimal_WriteUint64(uint64_t value, char_t* buffer, uint32_t capacity)
{
    uint32_t count;
    bool_t   has_buffer;
    bool_t   has_capacity;
    uint32_t cursor;

    count        = Utl_Decimal_GetUint64ByteCount(value);
    has_buffer   = (NULL != buffer);
    has_capacity = (count <= capacity);
    if (has_buffer && has_capacity)
    {
        cursor = count;
        for (;;)
        {
            --cursor;
            buffer[cursor]  = (char_t)(UTL_DECIMAL_CHAR_FIRST + (value % DECIMAL_RADIX));
            value          /= DECIMAL_RADIX;
            if (0 == cursor)
            {
                break;
            }
        }
    }
    else
    {
        count = 0;
    }
    return count;
}
