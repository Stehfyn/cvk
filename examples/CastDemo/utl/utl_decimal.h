#ifndef UTL_DECIMAL_H
#define UTL_DECIMAL_H

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

#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/* ASCII DIGIT terminals used by the counted decimal reader and writer. */
#define UTL_DECIMAL_CHAR_FIRST ('0') ///< First decimal digit.
#define UTL_DECIMAL_CHAR_LAST  ('9') ///< Last decimal digit.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

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
 * \brief Derives the decimal width of an unsigned 64-bit integer.
 *
 * \par Requirements:
 *  - 001 Zero shall occupy one digit.
 *  - 002 The operation shall allocate nothing and call no CRT or OS functions.
 *
 * \param[in] value   Unsigned integer
 *
 * \retval    1..20   Number of ASCII decimal digits required to encode the value.
 */
uint32_t Utl_Decimal_GetUint64ByteCount(uint64_t value);

/*************************************************************************************************/
/**
 * \brief Reads a nonempty unsigned decimal prefix from counted ASCII input.
 *
 * \par Requirements:
 *  - 001 Scanning shall stop at the first non-digit or the counted end.
 *  - 002 Signs, leading whitespace, missing digits and overflow shall fail.
 *  - 003 Leading zeros shall be accepted without imposing a digit-count limit.
 *  - 004 Failure shall preserve both outputs; no allocation, CRT or I/O shall occur.
 *
 * \param[out] value       Non-NULL integer output
 * \param[out] bytesRead   Non-NULL consumed count, disjoint from value and input
 * \param[in]  buffer      Non-NULL readable counted input, disjoint from outputs
 * \param[in]  byteCount   Readable bytes
 *
 * \retval     TRUE    For a representable prefix.
 * \retval     FALSE   Otherwise.
 */
bool_t Utl_Decimal_ReadUint64(uint64_t* value, uint32_t* bytesRead, const byte_t* buffer, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Writes the shortest unsigned decimal representation.
 *
 * \par Requirements:
 *  - 001 Capacity shall be validated before the first write.
 *  - 002 The function shall append no sign, whitespace or NUL terminator.
 *  - 003 Failure shall preserve the buffer; no allocation, CRT or I/O shall occur.
 *
 * \param[in]  value      Unsigned integer
 * \param[out] buffer     Non-NULL writable output
 * \param[in]  capacity   Available bytes; no terminator is written
 *
 * \retval     1..20   Number of ASCII decimal digits written.
 * \retval     0       Output is missing or capacity is insufficient; output remains unchanged.
 */
uint32_t Utl_Decimal_WriteUint64(uint64_t value, byte_t* buffer, uint32_t capacity);

#endif /* UTL_DECIMAL_H */
