#ifndef OS_ATOMIC_H
#define OS_ATOMIC_H

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

#include "os/os_types.h"

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
 * \brief Caller-owned naturally aligned 32-bit atomic word.
 *
 * \par Details:
 *  Initialize before publication. Do not copy, pack, move or access word directly
 *  while another thread can reach the object. All users must stop before reuse.
 *  The containing storage must preserve the uint32_t alignment and ordinary
 *  cacheable-memory semantics. MMIO and non-temporal access are outside this contract.
 */
typedef struct OS_Atomic32_s
{
    uint32_t word; ///< Private representation; use only OS_Atomic functions.
} OS_Atomic32_t;

/*************************************************************************************************/
/**
 * \brief Caller-owned naturally aligned 64-bit atomic word.
 *
 * \par Details:
 *  Initialize before publication. Do not copy, pack, move or access word directly
 *  while another thread can reach the object. All users must stop before reuse.
 *  The containing storage must preserve the uint64_t alignment and ordinary
 *  cacheable-memory semantics. MMIO and non-temporal access are outside this contract.
 */
typedef struct OS_Atomic64_s
{
    uint64_t word; ///< Private representation; use only OS_Atomic functions.
} OS_Atomic64_t;

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
 * \brief Constructs a 32-bit atomic object before concurrent use.
 *
 * \par Details:
 *  The caller exclusively owns the complete object and its containing storage.
 *  Required pointers and natural alignment are programming invariants, not runtime failures.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 Misaligned storage shall assert before initialization.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *  - 004 Every bit of word shall be initialized before the object is published.
 *
 * \param[in,out] atomic   Non-NULL quiescent caller-owned object
 * \param[in]     value    Word value to establish
 *
 * \retval        None   No result value.
 */
void OS_Atomic_Initialize32(OS_Atomic32_t* atomic, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Constructs a 64-bit atomic object before concurrent use.
 *
 * \par Details:
 *  The caller exclusively owns the complete object and its containing storage.
 *  Required pointers and natural alignment are programming invariants, not runtime failures.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 Misaligned storage shall assert before initialization.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *  - 004 Every bit of word shall be initialized before the object is published.
 *
 * \param[in,out] atomic   Non-NULL quiescent caller-owned object
 * \param[in]     value    Word value to establish
 *
 * \retval        None   No result value.
 */
void OS_Atomic_Initialize64(OS_Atomic64_t* atomic, uint64_t value);

/*************************************************************************************************/
/**
 * \brief Acquires the current 32-bit word.
 *
 * \par Details:
 *  An acquired publication makes preceding ordinary writes visible when matched
 *  with the releasing thread's synchronization protocol.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 The caller shall provide an initialized, naturally aligned object.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *
 * \param[in] atomic   Non-NULL initialized naturally aligned object
 *
 * \retval    0..UINT32_MAX   Current word.
 */
uint32_t OS_Atomic_Load32(const OS_Atomic32_t* atomic);

/*************************************************************************************************/
/**
 * \brief Acquires the current 64-bit word.
 *
 * \par Details:
 *  An acquired publication makes preceding ordinary writes visible when matched
 *  with the releasing thread's synchronization protocol.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 The caller shall provide an initialized, naturally aligned object.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *
 * \param[in] atomic   Non-NULL initialized naturally aligned object
 *
 * \retval    0..UINT64_MAX   Current word.
 */
uint64_t OS_Atomic_Load64(const OS_Atomic64_t* atomic);

/*************************************************************************************************/
/**
 * \brief Releases a new 32-bit word value.
 *
 * \par Details:
 *  Ordinary writes sequenced before this store become visible to a matching acquire.
 *  This operation does not promise a global sequentially consistent order.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 The caller shall provide an initialized, naturally aligned object.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *
 * \param[in,out] atomic   Non-NULL initialized naturally aligned object
 * \param[in]     value    Word value to establish
 *
 * \retval        None   No result value.
 */
void OS_Atomic_Store32(OS_Atomic32_t* atomic, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Releases a new 64-bit word value.
 *
 * \par Details:
 *  Ordinary writes sequenced before this store become visible to a matching acquire.
 *  This operation does not promise a global sequentially consistent order.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 The caller shall provide an initialized, naturally aligned object.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *
 * \param[in,out] atomic   Non-NULL initialized naturally aligned object
 * \param[in]     value    Word value to establish
 *
 * \retval        None   No result value.
 */
void OS_Atomic_Store64(OS_Atomic64_t* atomic, uint64_t value);

/*************************************************************************************************/
/**
 * \brief Exchanges a 32-bit word in one indivisible operation.
 *
 * \par Details:
 *  The exchange acquires the prior publication and releases preceding ordinary writes.
 *  This is one operation; there is no retry loop under contention.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 The caller shall provide an initialized, naturally aligned object.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *
 * \param[in,out] atomic   Non-NULL initialized naturally aligned object
 * \param[in]     value    Word value to establish
 *
 * \retval        0..UINT32_MAX   Word observed immediately before the operation.
 */
uint32_t OS_Atomic_Exchange32(OS_Atomic32_t* atomic, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Exchanges a 64-bit word in one indivisible operation.
 *
 * \par Details:
 *  The exchange acquires the prior publication and releases preceding ordinary writes.
 *  This is one operation; there is no retry loop under contention.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 The caller shall provide an initialized, naturally aligned object.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *
 * \param[in,out] atomic   Non-NULL initialized naturally aligned object
 * \param[in]     value    Word value to establish
 *
 * \retval        0..UINT64_MAX   Word observed immediately before the operation.
 */
uint64_t OS_Atomic_Exchange64(OS_Atomic64_t* atomic, uint64_t value);

/*************************************************************************************************/
/**
 * \brief Attempts one strong 32-bit compare-and-exchange.
 *
 * \par Details:
 *  The observed value is always returned. Equality with expected means replacement
 *  succeeded. Failure acquires the observed publication and never fails spuriously.
 *  No retry occurs inside this function; the caller owns retry/deadline policy.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 The caller shall provide an initialized, naturally aligned object.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *
 * \param[in,out] atomic        Non-NULL initialized naturally aligned object
 * \param[in]     expected      Word value required for replacement
 * \param[in]     replacement   Value to store on an exact match
 *
 * \retval        0..UINT32_MAX   Word observed immediately before the operation.
 */
uint32_t OS_Atomic_CompareExchange32(OS_Atomic32_t* atomic, uint32_t expected, uint32_t replacement);

/*************************************************************************************************/
/**
 * \brief Attempts one strong 64-bit compare-and-exchange.
 *
 * \par Details:
 *  The observed value is always returned. Equality with expected means replacement
 *  succeeded. Failure acquires the observed publication and never fails spuriously.
 *  No retry occurs inside this function; the caller owns retry/deadline policy.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 The caller shall provide an initialized, naturally aligned object.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *
 * \param[in,out] atomic        Non-NULL initialized naturally aligned object
 * \param[in]     expected      Word value required for replacement
 * \param[in]     replacement   Value to store on an exact match
 *
 * \retval        0..UINT64_MAX   Word observed immediately before the operation.
 */
uint64_t OS_Atomic_CompareExchange64(OS_Atomic64_t* atomic, uint64_t expected, uint64_t replacement);

/*************************************************************************************************/
/**
 * \brief Adds to a 32-bit word and returns its previous value.
 *
 * \par Details:
 *  Arithmetic is modulo 2^32; callers that prohibit wrapping must enforce their
 *  own value bounds. The indivisible update acquires and releases preceding state.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 The caller shall provide an initialized, naturally aligned object.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *
 * \param[in,out] atomic   Non-NULL initialized naturally aligned object
 * \param[in]     value    Unsigned increment, with modulo arithmetic
 *
 * \retval        0..UINT32_MAX   Word observed immediately before the operation.
 */
uint32_t OS_Atomic_FetchAdd32(OS_Atomic32_t* atomic, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Adds to a 64-bit word and returns its previous value.
 *
 * \par Details:
 *  Arithmetic is modulo 2^64; callers that prohibit wrapping must enforce their
 *  own value bounds. The indivisible update acquires and releases preceding state.
 *
 * \par Requirements:
 *  - 001 Missing object storage shall assert in every build.
 *  - 002 The caller shall provide an initialized, naturally aligned object.
 *  - 003 The operation shall allocate nothing and use no locks, waits, I/O or hidden retry loop.
 *
 * \param[in,out] atomic   Non-NULL initialized naturally aligned object
 * \param[in]     value    Unsigned increment, with modulo arithmetic
 *
 * \retval        0..UINT64_MAX   Word observed immediately before the operation.
 */
uint64_t OS_Atomic_FetchAdd64(OS_Atomic64_t* atomic, uint64_t value);

#endif /* OS_ATOMIC_H */
