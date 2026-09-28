#ifndef OS_TEMPORARYFILE_H
#define OS_TEMPORARYFILE_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * A startup-created temporary file and its retained native-order UTF-16 path.
 * The owner and all borrowers serialize access; do not copy or move a live owner.
 * Backends are build-selected. Only Initialize creates a filesystem entry.
 * Filesystem calls and consumer/provider allocations need separate timing evidence.
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

#define OS_TEMPORARYFILE_PATH_CAPACITY (260) ///< Supported native UTF-16 units, including termination.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Caller-owned scratch-file lifetime; fields belong to the selected backend.
 */
typedef struct OS_TemporaryFile_s
{
    uint16_t path[OS_TEMPORARYFILE_PATH_CAPACITY]; ///< Retained native-order UTF-16 identity.
    bool_t   ready;                                ///< The path can be borrowed before shutdown.
    bool_t   owned;                                ///< A file removal attempt remains required.
    uint32_t error;                                ///< First native failure, preserved through closure.
} OS_TemporaryFile_t;

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
 * \brief Creates a unique empty scratch file during program initialization.
 *
 * \par Requirements:
 *  - 001 Missing owner storage shall assert; the caller shall provide fresh or completely closed storage.
 *  - 002 File creation and native temporary-directory discovery shall occur only in this startup operation.
 *  - 003 Failure shall leave an initialized, closable owner; every successfully acquired path shall remain retained.
 *  - 004 Borrowers shall overwrite contents without deleting, renaming or transferring the owned path.
 *  - 005 Platform filesystem calls may block and allocate; they are confined to startup here.
 *
 * \param[out] file   Required stable owner storage through successful TryClose
 *
 * \retval     TRUE    An empty file and terminated path are ready.
 * \retval     FALSE   Native startup failed; TryClose remains required.
 */
bool_t OS_TemporaryFile_Initialize(OS_TemporaryFile_t* file);

/*************************************************************************************************/
/**
 * \brief Queries whether the retained path can be borrowed.
 *
 * \par Requirements:
 *  - 001 The owner shall have been initialized, including after failed startup.
 *  - 002 Any shutdown attempt shall revoke readiness.
 *
 * \param[in] file   Required initialized owner
 *
 * \retval    TRUE    The scratch path is available.
 * \retval    FALSE   Initialization failed or shutdown started.
 */
bool_t OS_TemporaryFile_IsReady(const OS_TemporaryFile_t* file);

/*************************************************************************************************/
/**
 * \brief Gets the first recorded native failure.
 *
 * \par Requirements:
 *  - 001 A successful retry shall preserve prior failure evidence until reinitialization.
 *
 * \param[in] file   Required initialized owner
 *
 * \retval    0         No failure is recorded.
 * \retval    nonzero   Backend-native error code.
 */
uint32_t OS_TemporaryFile_GetError(const OS_TemporaryFile_t* file);

/*************************************************************************************************/
/**
 * \brief Borrows the terminated native-order UTF-16 path without native calls.
 *
 * \par Requirements:
 *  - 001 Borrowers shall end every file/path use before the first TryClose call.
 *  - 002 The address and bytes shall remain stable throughout the ready lifetime.
 *
 * \param[in] file   Required initialized owner
 *
 * \retval    path   A terminated path within OS_TEMPORARYFILE_PATH_CAPACITY units.
 * \retval    NULL   The path is unavailable.
 */
const uint16_t* OS_TemporaryFile_GetPath(const OS_TemporaryFile_t* file);

/*************************************************************************************************/
/**
 * \brief Removes a quiescent scratch file, retaining failed removal for retry.
 *
 * \par Requirements:
 *  - 001 The caller shall end all borrowers and serialize lifecycle operations.
 *  - 002 A failed removal shall retain the exact path and ownership; at most one native removal shall be attempted.
 *  - 003 Already absent files shall complete closure; all other errors shall remain retryable.
 *  - 004 Successful repeated closure shall perform no native operation.
 *  - 005 Native filesystem removal latency remains subject to backend measurement.
 *
 * \param[in,out] file   Required initialized owner, including after failed startup
 *
 * \retval        TRUE    No owned filesystem entry remains at the path.
 * \retval        FALSE   Native removal failed; retain the owner and retry.
 */
bool_t OS_TemporaryFile_TryClose(OS_TemporaryFile_t* file);

#endif /* OS_TEMPORARYFILE_H */
