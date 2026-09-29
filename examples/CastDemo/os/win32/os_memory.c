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

#include <Windows.h> // IWYU pragma: keep
#include <basetsd.h>
#include <minwindef.h>
#include <memoryapi.h>
#include <errhandlingapi.h>
#include <winerror.h>
#include <winnt.h>
#include <stdint.h>
#include "os/os_memory.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "crt/crt_memory.h"

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
 * \brief Retains the first native error for this lifetime.
 *
 * \par Requirements:
 *  - 001 The owner shall exclude concurrent lifetime operations.
 *
 * \param[in,out] memory   Initialized lifetime owner
 * \param[in]     code     Native error captured immediately after failure
 *
 * \retval        None
 */
static FORCE_INLINE void RecordError(OS_Memory_t* memory, uint32_t code);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  A native failure reporting zero receives the generic-failure code.
 */
static FORCE_INLINE void RecordError(OS_Memory_t* memory, uint32_t code)
{
    uint32_t error;

    error = code;
    if (ERROR_SUCCESS == error)
    {
        error = ERROR_GEN_FAILURE;
    }
    if (0 == memory->error)
    {
        memory->error = error;
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The backend reserves/commits, touches and locks every requested page before
 *  publishing readiness. A failed residency request does not become a usable region.
 */
extern FORCE_INLINE bool_t OS_Memory_Initialize(OS_Memory_t* memory, uint64_t byteCount)
{
    uint32_t error;
    bool_t   allocated;
    bool_t   locked;

    LOGASSERT(NULL != memory);
    LOGASSERT(0 < byteCount);
    LOGASSERT(UINTPTR_MAX >= byteCount);

    memory->address       = NULL;
    memory->byteCount     = 0;
    memory->ready         = FALSE;
    memory->unlockPending = FALSE;
    memory->error         = 0;
    memory->address       = VirtualAlloc(NULL, (SIZE_T)byteCount, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    allocated             = (NULL != memory->address);
    if (allocated)
    {
        memory->byteCount     = byteCount;
        memory->unlockPending = TRUE;
        CRT_Memory_Set(memory->address, 0, byteCount);
        locked = !!VirtualLock(memory->address, (SIZE_T)byteCount);
        if (locked)
        {
            memory->ready = TRUE;
        }
        else
        {
            error = GetLastError();
            RecordError(memory, error);
        }
    }
    else
    {
        error = GetLastError();
        RecordError(memory, error);
    }

    return memory->ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Getters do not extend lifetime. The owner excludes closure while any borrower is active.
 */
void* OS_Memory_GetAddress(const OS_Memory_t* memory)
{
    void* address;

    LOGASSERT(NULL != memory);

    address = NULL;
    if (memory->ready)
    {
        address = memory->address;
    }

    return address;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Native page rounding does not increase the caller-visible contract.
 */
extern FORCE_INLINE uint64_t OS_Memory_GetByteCount(const OS_Memory_t* memory)
{
    uint64_t byte_count;

    LOGASSERT(NULL != memory);

    byte_count = 0;
    if (memory->ready)
    {
        byte_count = memory->byteCount;
    }

    return byte_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The Win32 backend records GetLastError values.
 */
extern FORCE_INLINE uint32_t OS_Memory_GetError(const OS_Memory_t* memory)
{
    LOGASSERT(NULL != memory);

    return memory->error;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No native query, lock or allocation occurs.
 */
bool_t OS_Memory_IsReady(const OS_Memory_t* memory)
{
    LOGASSERT(NULL != memory);

    return memory->ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  An unlock failure may leave uncertain per-page lock state. The outstanding
 *  unlock attempt stays recorded until successful unlock or whole-region release.
 *  No code may resume borrowing a region after closure starts.
 */
extern FORCE_INLINE bool_t OS_Memory_TryClose(OS_Memory_t* memory)
{
    bool_t   has_region;
    bool_t   unlocked;
    uint32_t error;
    bool_t   released;
    bool_t   closed;

    LOGASSERT(NULL != memory);

    memory->ready = FALSE;
    has_region    = (NULL != memory->address);
    if (has_region)
    {
        if (memory->unlockPending)
        {
            unlocked = !!VirtualUnlock(memory->address, (SIZE_T)memory->byteCount);
            if (unlocked)
            {
                memory->unlockPending = FALSE;
            }
            else
            {
                error = GetLastError();
                RecordError(memory, error);
            }
        }
        released = !!VirtualFree(memory->address, 0, MEM_RELEASE);
        if (released)
        {
            memory->address       = NULL;
            memory->byteCount     = 0;
            memory->unlockPending = FALSE;
        }
        else
        {
            error = GetLastError();
            RecordError(memory, error);
        }
    }
    closed = (NULL == memory->address);

    return closed;
}
