/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Scratch-file startup, unique identity, sharing-failure retention and retry.
 * Native filesystem calls are exercised; this is not a provider allocation trace.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h> // IWYU pragma: keep
#include <minwindef.h>
#include <winnt.h>
#include <fileapi.h>
#include <handleapi.h>
#include <errhandlingapi.h>
#include <winerror.h>
#include <stdint.h>
#include "os/os_temporaryfile.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include "crt/crt_memory.h"
#include "utl/utl_unicode.h"

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

static OS_TemporaryFile_t g_first;
static OS_TemporaryFile_t g_second;

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Checks that two startup owners receive distinct valid native paths.
 *
 * \par Details:
 *  All assertions pass or the fixture fails.
 *
 * \par Requirements:
 *  - 001 Both owners shall be ready and their path borrows shall remain stable.
 *  - 002 Comparison shall use only initialized units through termination.
 *
 * \retval None
 */
static FORCE_INLINE void CheckDistinctPaths(void);

/*************************************************************************************************/
/**
 * \brief Forces a real sharing violation, then verifies retained removal and retry.
 *
 * \par Details:
 *  All assertions pass or the fixture fails.
 *
 * \par Requirements:
 *  - 001 The fixture shall copy the path before revoking its borrow.
 *  - 002 Failed closure shall revoke readiness while retaining first-error evidence.
 *  - 003 Releasing the conflicting handle shall permit removal and idempotent closure.
 *
 * \retval None
 */
static FORCE_INLINE void CheckRemovalRetry(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Independent owners must not overwrite the same file during their ready lifetimes.
 */
static FORCE_INLINE void CheckDistinctPaths(void)
{
    const uint16_t* first;
    const uint16_t* second;
    uint32_t        first_units;
    uint32_t        second_units;
    uint32_t        compared_units;
    bool_t          different_lengths;
    bool_t          different_units;
    bool_t          distinct;
    int32_t         compared;

    first           = OS_TemporaryFile_GetPath(&g_first);
    second          = OS_TemporaryFile_GetPath(&g_second);
    first_units     = Utl_Unicode_GetUtf16StringUnitCount(first, OS_TEMPORARYFILE_PATH_CAPACITY);
    second_units    = Utl_Unicode_GetUtf16StringUnitCount(second, OS_TEMPORARYFILE_PATH_CAPACITY);
    compared_units  = (first_units < second_units) ? first_units : second_units;
    compared_units %= OS_TEMPORARYFILE_PATH_CAPACITY;
    compared        = CRT_Memory_Compare(first, second, (uint64_t)compared_units * sizeof(uint16_t));
    LOGASSERT(0 < first_units);
    LOGASSERT(OS_TEMPORARYFILE_PATH_CAPACITY > first_units);
    LOGASSERT(0 < second_units);
    LOGASSERT(OS_TEMPORARYFILE_PATH_CAPACITY > second_units);
    different_lengths = (first_units != second_units);
    different_units   = (0 != compared);
    distinct          = different_lengths || different_units;
    LOGASSERT(distinct);
    LOGASSERT(first == OS_TemporaryFile_GetPath(&g_first));
    LOGASSERT(second == OS_TemporaryFile_GetPath(&g_second));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A handle without FILE_SHARE_DELETE deliberately prevents the first removal.
 *  Only the copied path is used after TryClose revokes new borrows.
 */
static FORCE_INLINE void CheckRemovalRetry(void)
{
    const uint16_t* borrowed;
    uint32_t        units;
    uint32_t        copied_units;
    uint16_t        path[OS_TEMPORARYFILE_PATH_CAPACITY];
    HANDLE          blocker;
    bool_t          closed;
    uint32_t        first_error;
    BOOL            released;
    DWORD           attributes;
    uint32_t        error;
    bool_t          missing;

    borrowed     = OS_TemporaryFile_GetPath(&g_first);
    units        = Utl_Unicode_GetUtf16StringUnitCount(borrowed, OS_TEMPORARYFILE_PATH_CAPACITY);
    copied_units = units % OS_TEMPORARYFILE_PATH_CAPACITY;
    CRT_Memory_Copy(path, borrowed, (uint64_t)copied_units * sizeof(uint16_t));
    path[copied_units] = UTL_UNICODE_TERMINATOR;
    LOGASSERT(OS_TEMPORARYFILE_PATH_CAPACITY > units);
    blocker =
        CreateFile((LPCTSTR)path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    LOGASSERT(INVALID_HANDLE_VALUE != blocker);
    closed = OS_TemporaryFile_TryClose(&g_first);
    LOGASSERT(!closed);
    LOGASSERT(!OS_TemporaryFile_IsReady(&g_first));
    LOGASSERT(NULL == OS_TemporaryFile_GetPath(&g_first));
    first_error = OS_TemporaryFile_GetError(&g_first);
    LOGASSERT(ERROR_SHARING_VIOLATION == first_error);
    closed = OS_TemporaryFile_TryClose(&g_first);
    LOGASSERT(!closed);
    LOGASSERT(first_error == OS_TemporaryFile_GetError(&g_first));
    released = CloseHandle(blocker);
    LOGASSERT(released);
    closed = OS_TemporaryFile_TryClose(&g_first);
    LOGASSERT(closed);
    closed = OS_TemporaryFile_TryClose(&g_first);
    LOGASSERT(closed);
    LOGASSERT(first_error == OS_TemporaryFile_GetError(&g_first));
    attributes = GetFileAttributes((LPCTSTR)path);
    error      = GetLastError();
    missing    = (ERROR_FILE_NOT_FOUND == error) || (ERROR_PATH_NOT_FOUND == error);
    LOGASSERT(INVALID_FILE_ATTRIBUTES == attributes);
    LOGASSERT(missing);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Both native file creations occur before any exercise begins.
 */
uint32_t OS_Application_Main(void)
{
    bool_t first_ready;
    bool_t second_ready;
    bool_t closed;

    first_ready  = OS_TemporaryFile_Initialize(&g_first);
    second_ready = OS_TemporaryFile_Initialize(&g_second);
    LOGASSERT(first_ready);
    LOGASSERT(second_ready);
    LOGASSERT(OS_TemporaryFile_IsReady(&g_first));
    LOGASSERT(OS_TemporaryFile_IsReady(&g_second));
    LOGASSERT(0 == OS_TemporaryFile_GetError(&g_first));
    LOGASSERT(0 == OS_TemporaryFile_GetError(&g_second));
    CheckDistinctPaths();
    CheckRemovalRetry();
    closed = OS_TemporaryFile_TryClose(&g_second);
    LOGASSERT(closed);
    closed = OS_TemporaryFile_TryClose(&g_second);
    LOGASSERT(closed);
    LOGASSERT(NULL == OS_TemporaryFile_GetPath(&g_second));
    return 0;
}
