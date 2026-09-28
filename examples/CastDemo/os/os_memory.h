#ifndef OS_MEMORY_H
#define OS_MEMORY_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Startup-owned resident storage with retryable shutdown.
 * https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc
 * https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtuallock
 * https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualunlock
 * https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree
 * These guarantees cover this region, not callback, dependency or stack storage.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Caller-owned lifetime record for one startup region.
 *
 * \par Details:
 *  Fields belong to the selected backend. Do not alter, copy, pack or move a live
 *  owner. The native allocation is aligned for ordinary target objects; callers
 *  requiring stronger alignment must verify it before constructing their model.
 */
typedef struct OS_Memory_s
{
    void*    address;       ///< Native region identity retained until release succeeds.
    uint64_t byteCount;     ///< Requested region extent, retained through partial closure.
    bool_t   ready;         ///< Residency acquired and no shutdown attempt made.
    bool_t   unlockPending; ///< An unlock attempt remains required or uncertain.
    uint32_t error;         ///< First native failure; zero initially.
} OS_Memory_t;

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
 * \brief Establishes zeroed resident storage during program initialization.
 *
 * \par Requirements:
 *  - 001 Required owner storage and a positive native-representable byte count shall assert.
 *  - 002 The owner shall provide fresh storage or an object whose TryClose previously succeeded.
 *  - 003 Allocation, first-touch writes and residency acquisition shall occur only in this startup operation.
 *  - 004 Failure shall leave an initialized owner retaining all acquired resources for TryClose.
 *  - 005 The caller shall serialize lifetime operations; copying or moving a live owner is prohibited.
 *
 * \param[out] memory      Required owner, alive through successful closure
 * \param[in]  byteCount   Required readable/writable byte extent
 *
 * \retval     TRUE    Zeroed storage is ready and resident.
 * \retval     FALSE   Native startup failed; the owner still requires TryClose.
 */
bool_t OS_Memory_Initialize(OS_Memory_t* memory, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Borrows the initialized region without acquiring resources.
 *
 * \par Requirements:
 *  - 001 The owner shall remain alive and ready throughout every borrowed access.
 *  - 002 A failed Initialize or any TryClose attempt shall expose no new borrowed address.
 *
 * \param[in] memory   Required initialized owner
 *
 * \retval    address   Writable region of GetByteCount bytes while ready.
 * \retval    NULL      Initialization failed or shutdown began.
 */
void* OS_Memory_GetAddress(const OS_Memory_t* memory);

/*************************************************************************************************/
/**
 * \brief Gets the usable extent of a ready region.
 *
 * \par Requirements:
 *  - 001 Unready or closing storage shall report zero usable bytes.
 *
 * \param[in] memory   Required initialized owner
 *
 * \retval    nonzero   Requested usable byte count.
 * \retval    0         No ready region is available.
 */
uint64_t OS_Memory_GetByteCount(const OS_Memory_t* memory);

/*************************************************************************************************/
/**
 * \brief Gets the first native lifetime failure.
 *
 * \par Requirements:
 *  - 001 Failure evidence shall remain until reinitialization of closed owner storage.
 *
 * \param[in] memory   Required initialized owner
 *
 * \retval    0         No native failure was recorded.
 * \retval    nonzero   First native allocation, residency or release error.
 */
uint32_t OS_Memory_GetError(const OS_Memory_t* memory);

/*************************************************************************************************/
/**
 * \brief Queries whether storage can be borrowed.
 *
 * \par Requirements:
 *  - 001 Readiness shall be published only after complete startup initialization.
 *
 * \param[in] memory   Required initialized owner
 *
 * \retval    TRUE    The resident region is available.
 * \retval    FALSE   Startup failed or shutdown has begun.
 */
bool_t OS_Memory_IsReady(const OS_Memory_t* memory);

/*************************************************************************************************/
/**
 * \brief Releases a quiescent region while retaining failed releases for retry.
 *
 * \par Requirements:
 *  - 001 The caller shall end all borrowed accesses before the first attempt.
 *  - 002 The first attempt shall revoke readiness even if a native release fails.
 *  - 003 Successful releases shall clear stored identities; failed releases shall retain them.
 *  - 004 A whole-region release shall still be attempted after an unlock failure.
 *  - 005 Repeated calls after complete release shall return TRUE without native calls.
 *
 * \param[in,out] memory   Required initialized owner, including after failed startup
 *
 * \retval        TRUE    The region is completely released.
 * \retval        FALSE   A native region remains; keep owner storage and retry.
 */
bool_t OS_Memory_TryClose(OS_Memory_t* memory);

#endif /* OS_MEMORY_H */
