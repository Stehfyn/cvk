#ifndef CRT_MEMORY_BACKEND_H
#define CRT_MEMORY_BACKEND_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Private build-selected implementation contract for CRT memory and compiler helpers.
 * Counts remain uint64_t here; each native ABI adapter must preserve its target extent.
 * Callers have already established the required pointers, bounds and ownership.
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
 * \brief Implements the corresponding public CRT memory operation.
 *
 * \par Details:
 *  This is a private implementation interface. The matching public operation specifies
 *  all extent, ownership, aliasing and result contracts.
 *
 * \par Requirements:
 *  - 001 The backend shall implement the public operation without external runtime helpers.
 *  - 002 No allocation, global mutable state, waits or I/O shall occur.
 *
 * \param[in] first       First readable extent; required for nonzero count
 * \param[in] second      Second readable extent; required for nonzero count
 * \param[in] byteCount   Exact readable extent in bytes
 *
 * \retval    -255..-1   First sequence precedes second.
 * \retval    0          Sequences are equal.
 * \retval    1..255     First sequence follows second.
 */
int32_t CRT_Memory_CompareCore(const void* first, const void* second, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Implements the corresponding public CRT memory operation.
 *
 * \par Details:
 *  This is a private implementation interface. The matching public operation specifies
 *  all extent, ownership, aliasing and result contracts.
 *
 * \par Requirements:
 *  - 001 The backend shall implement the public operation without external runtime helpers.
 *  - 002 No allocation, global mutable state, waits or I/O shall occur.
 *
 * \param[out] output      Writable destination extent
 * \param[in]  input       Readable source extent, disjoint from output
 * \param[in]  byteCount   Exact byte count to copy
 *
 * \retval     output   Original destination pointer.
 */
void* CRT_Memory_CopyCore(void* output, const void* input, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Implements the corresponding public CRT memory operation.
 *
 * \par Details:
 *  This is a private implementation interface. The matching public operation specifies
 *  all extent, ownership, aliasing and result contracts.
 *
 * \par Requirements:
 *  - 001 The backend shall implement the public operation without external runtime helpers.
 *  - 002 No allocation, global mutable state, waits or I/O shall occur.
 *
 * \param[out] output      Writable destination extent
 * \param[in]  input       Readable source extent, overlap permitted
 * \param[in]  byteCount   Exact byte count to move
 *
 * \retval     output   Original destination pointer.
 */
void* CRT_Memory_MoveCore(void* output, const void* input, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Implements the corresponding public CRT memory operation.
 *
 * \par Details:
 *  This is a private implementation interface. The matching public operation specifies
 *  all extent, ownership, aliasing and result contracts.
 *
 * \par Requirements:
 *  - 001 The backend shall implement the public operation without external runtime helpers.
 *  - 002 No allocation, global mutable state, waits or I/O shall occur.
 *
 * \param[out] output      Writable destination extent
 * \param[in]  value       Byte value to repeat
 * \param[in]  byteCount   Exact byte count to fill
 *
 * \retval     output   Original destination pointer.
 */
void* CRT_Memory_SetCore(void* output, byte_t value, uint64_t byteCount);

#endif /* CRT_MEMORY_BACKEND_H */
