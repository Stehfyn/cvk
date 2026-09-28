/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Explicit-width atomics for normally cacheable process memory.
 * The selected backend owns CPU/compiler ordering; callers never access word directly.
 * Load is acquire, Store is release, and read-modify-write operations are acquire/release.
 * Initialize requires quiescent ownership and does not publish the containing object.
 * No operation creates resources, allocates, waits, retries or calls the CRT.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_atomic.h"
#include "os/os_atomic_backend.h"
#include "os/os_assert.h"

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

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Storage is quiescent; the native store binds no external resources.
 */
void OS_Atomic_Initialize32(OS_Atomic32_t* atomic, uint32_t value)
{
    LOGASSERT(NULL != atomic);
    LOGASSERT(!((uintptr_t)atomic & (sizeof(uint32_t) - 1)));

    OS_Atomic_Store32Core(&atomic->word, value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Storage is quiescent; the native store binds no external resources.
 */
void OS_Atomic_Initialize64(OS_Atomic64_t* atomic, uint64_t value)
{
    LOGASSERT(NULL != atomic);
    LOGASSERT(!((uintptr_t)atomic & (sizeof(uint64_t) - 1)));

    OS_Atomic_Store64Core(&atomic->word, value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The external native call preserves the compiler boundary; the selected backend
 *  provides the documented indivisibility and hardware ordering.
 */
uint32_t OS_Atomic_Load32(const OS_Atomic32_t* atomic)
{
    LOGASSERT(NULL != atomic);

    return OS_Atomic_Load32Core(&atomic->word);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The external native call preserves the compiler boundary; the selected backend
 *  provides the documented indivisibility and hardware ordering.
 */
uint64_t OS_Atomic_Load64(const OS_Atomic64_t* atomic)
{
    LOGASSERT(NULL != atomic);

    return OS_Atomic_Load64Core(&atomic->word);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The external native call preserves the compiler boundary; the selected backend
 *  provides the documented indivisibility and hardware ordering.
 */
void OS_Atomic_Store32(OS_Atomic32_t* atomic, uint32_t value)
{
    LOGASSERT(NULL != atomic);

    OS_Atomic_Store32Core(&atomic->word, value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The external native call preserves the compiler boundary; the selected backend
 *  provides the documented indivisibility and hardware ordering.
 */
void OS_Atomic_Store64(OS_Atomic64_t* atomic, uint64_t value)
{
    LOGASSERT(NULL != atomic);

    OS_Atomic_Store64Core(&atomic->word, value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The external native call preserves the compiler boundary; the selected backend
 *  provides the documented indivisibility and hardware ordering.
 */
uint32_t OS_Atomic_Exchange32(OS_Atomic32_t* atomic, uint32_t value)
{
    LOGASSERT(NULL != atomic);

    return OS_Atomic_Exchange32Core(&atomic->word, value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The external native call preserves the compiler boundary; the selected backend
 *  provides the documented indivisibility and hardware ordering.
 */
uint64_t OS_Atomic_Exchange64(OS_Atomic64_t* atomic, uint64_t value)
{
    LOGASSERT(NULL != atomic);

    return OS_Atomic_Exchange64Core(&atomic->word, value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The external native call preserves the compiler boundary; the selected backend
 *  provides the documented indivisibility and hardware ordering.
 */
uint32_t OS_Atomic_CompareExchange32(OS_Atomic32_t* atomic, uint32_t expected, uint32_t replacement)
{
    LOGASSERT(NULL != atomic);

    return OS_Atomic_CompareExchange32Core(&atomic->word, expected, replacement);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The external native call preserves the compiler boundary; the selected backend
 *  provides the documented indivisibility and hardware ordering.
 */
uint64_t OS_Atomic_CompareExchange64(OS_Atomic64_t* atomic, uint64_t expected, uint64_t replacement)
{
    LOGASSERT(NULL != atomic);

    return OS_Atomic_CompareExchange64Core(&atomic->word, expected, replacement);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The external native call preserves the compiler boundary; the selected backend
 *  provides the documented indivisibility and hardware ordering.
 */
uint32_t OS_Atomic_FetchAdd32(OS_Atomic32_t* atomic, uint32_t value)
{
    LOGASSERT(NULL != atomic);

    return OS_Atomic_FetchAdd32Core(&atomic->word, value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The external native call preserves the compiler boundary; the selected backend
 *  provides the documented indivisibility and hardware ordering.
 */
uint64_t OS_Atomic_FetchAdd64(OS_Atomic64_t* atomic, uint64_t value)
{
    LOGASSERT(NULL != atomic);

    return OS_Atomic_FetchAdd64Core(&atomic->word, value);
}
