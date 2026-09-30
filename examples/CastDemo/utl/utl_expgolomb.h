#ifndef UTL_EXPGOLOMB_H
#define UTL_EXPGOLOMB_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * ITU-T H.264 (08/2024), 9.1 and 9.1.1: order-zero ue(v) and se(v) coding.
 * https://www.itu.int/rec/T-REC-H.264-202408-S/en
 * Shared MPEG-4 readers/writers retain storage ownership and sticky error state.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_mpeg4.h"
#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

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
 * \brief Derives the complete ue(v) codeword length.
 *
 * \par Requirements:
 *  - 001 Every uint32_t input shall be representable, including UINT32_MAX.
 *  - 002 Counting shall not access storage or allocate memory.
 *
 * \param[in] value  Unsigned syntax value
 *
 * \retval 1..65  Complete codeword length in bits.
 */
uint32_t Utl_ExpGolomb_GetUnsignedBitCount(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Derives the complete se(v) codeword length.
 *
 * \par Requirements:
 *  - 001 Every int32_t input shall be representable, including INT32_MIN.
 *  - 002 Zero maps to zero; positive values map to odd code numbers, negative values to even ones.
 *
 * \param[in] value  Signed syntax value
 *
 * \retval 1..65  Complete codeword length in bits.
 */
uint32_t Utl_ExpGolomb_GetSignedBitCount(int32_t value);

/*************************************************************************************************/
/**
 * \brief Reads one ue(v) value from the shared bit reader.
 *
 * \par Requirements:
 *  - 001 The reader shall be initialized, non-NULL and exclusively owned; misuse shall assert.
 *  - 002 Truncation or a value exceeding UINT32_MAX shall clear the reader's valid flag.
 *  - 003 An invalid reader shall remain invalid until reinitialized.
 *  - 004 A codeword shall consume at most 65 bits; no allocation shall occur.
 *
 * \param[in,out] reader  Initialized reader advanced through the codeword or detected error
 *
 * \retval 0..UINT32_MAX  Decoded value when reader.valid remains TRUE; zero otherwise.
 */
uint32_t Utl_ExpGolomb_ReadUnsigned(Utl_Mpeg4_BitReader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads one se(v) value from the shared bit reader.
 *
 * \par Requirements:
 *  - 001 The reader shall be initialized, non-NULL and exclusively owned; misuse shall assert.
 *  - 002 Truncation or a value outside the int32_t domain shall clear the reader's valid flag.
 *  - 003 The signed mapping shall handle INT32_MIN without negating it in int32_t arithmetic.
 *  - 004 An invalid reader shall remain invalid; at most 65 bits shall be consumed.
 *
 * \param[in,out] reader  Initialized reader advanced through the codeword or detected error
 *
 * \retval INT32_MIN..INT32_MAX  Decoded value when reader.valid remains TRUE; zero otherwise.
 */
int32_t Utl_ExpGolomb_ReadSigned(Utl_Mpeg4_BitReader_t* reader);

/*************************************************************************************************/
/**
 * \brief Writes one ue(v) codeword or counts its bits.
 *
 * \par Requirements:
 *  - 001 The writer shall be initialized, non-NULL and exclusively owned; misuse shall assert.
 *  - 002 Every uint32_t value shall be supported at any current bit position.
 *  - 003 Capacity shall be checked for the whole codeword before any output store.
 *  - 004 Insufficient capacity shall clear valid without changing position or destination bytes.
 *  - 005 An invalid writer shall remain invalid; a counter shall not dereference output storage.
 *
 * \param[in,out] writer  Initialized writer or bit counter
 * \param[in]     value   Unsigned syntax value
 *
 * \retval None No value is returned.
 */
void Utl_ExpGolomb_WriteUnsigned(Utl_Mpeg4_BitWriter_t* writer, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Writes one se(v) codeword or counts its bits.
 *
 * \par Requirements:
 *  - 001 The writer shall be initialized, non-NULL and exclusively owned; misuse shall assert.
 *  - 002 Every int32_t value shall be supported, including INT32_MIN.
 *  - 003 Capacity shall be checked for the whole codeword before any output store.
 *  - 004 Insufficient capacity shall clear valid without changing position or destination bytes.
 *  - 005 An invalid writer shall remain invalid; no allocation shall occur.
 *
 * \param[in,out] writer  Initialized writer or bit counter
 * \param[in]     value   Signed syntax value
 *
 * \retval None No value is returned.
 */
void Utl_ExpGolomb_WriteSigned(Utl_Mpeg4_BitWriter_t* writer, int32_t value);

#endif /* UTL_EXPGOLOMB_H */
