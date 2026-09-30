#ifndef CRT_MEMORY_H
#define CRT_MEMORY_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Hand-written counted memory operations. No allocation, initialization resource,
 * system runtime library, locale or hidden payload ownership is involved.
 * Public interfaces use explicit-width counts; the build selects the native ABI.
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
 * \brief Compares two byte sequences in unsigned lexicographic order.
 *
 * \par Details:
 *  This operation is data-dependent and is not a constant-time secret comparison.
 *
 * \par Requirements:
 *  - 001 Both pointers shall be required even at zero length; missing pointers shall assert.
 *  - 002 Both inputs shall provide byteCount readable bytes; overlap is permitted.
 *  - 003 The first differing byte shall determine the result; empty sequences compare equal.
 *
 * \param[in] first       Non-NULL first readable sequence
 * \param[in] second      Non-NULL second readable sequence
 * \param[in] byteCount   Readable extent of each sequence in bytes
 *
 * \retval    -255..-1   First sequence precedes second.
 * \retval    0          Sequences are equal.
 * \retval    1..255     First sequence follows second.
 */
int32_t CRT_Memory_Compare(const void* first, const void* second, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Copies a disjoint counted byte sequence.
 *
 * \par Details:
 *  The caller owns both extents and excludes concurrent mutation. A zero count is a
 *  no-op; it does not relax the required pointer contract.
 *
 * \par Requirements:
 *  - 001 Required pointers shall assert when missing.
 *  - 002 Input and output shall provide byteCount accessible bytes and shall not overlap.
 *  - 003 No allocation, external runtime, synchronization or I/O shall occur.
 *
 * \param[out] output      Non-NULL writable extent
 * \param[in]  input       Non-NULL disjoint readable extent
 * \param[in]  byteCount   Exact byte count to copy
 *
 * \retval     None No value is returned.
 */
void CRT_Memory_Copy(void* output, const void* input, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Copies a counted byte sequence with overlap allowed.
 *
 * \par Details:
 *  The result is as if the original input were copied before any output store, without
 *  allocating temporary storage. Equal pointers and a zero count are no-ops.
 *
 * \par Requirements:
 *  - 001 Required pointers shall assert when missing.
 *  - 002 Input and output shall each provide byteCount accessible bytes.
 *  - 003 No access shall leave either supplied extent; no temporary buffer shall be allocated.
 *
 * \param[out] output      Non-NULL writable extent
 * \param[in]  input       Non-NULL readable extent, possibly overlapping output
 * \param[in]  byteCount   Exact byte count to move
 *
 * \retval     None No value is returned.
 */
void CRT_Memory_Move(void* output, const void* input, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Fills a counted byte extent with one repeated value.
 *
 * \par Details:
 *  This ordinary fill is not a secure-erasure guarantee. The caller excludes concurrent
 *  access and supplies all output storage.
 *
 * \par Requirements:
 *  - 001 A missing output pointer shall assert even at zero length.
 *  - 002 Exactly byteCount accessible output bytes shall receive value.
 *  - 003 No allocation, runtime dependency, synchronization or I/O shall occur.
 *
 * \param[out] output      Non-NULL writable extent
 * \param[in]  value       Byte value to repeat
 * \param[in]  byteCount   Exact output extent in bytes
 *
 * \retval     None No value is returned.
 */
void CRT_Memory_Set(void* output, byte_t value, uint64_t byteCount);

#endif /* CRT_MEMORY_H */
