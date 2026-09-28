#ifndef OS_THREAD_H
#define OS_THREAD_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Persistent execution resources established at application startup.
 * The owner serializes lifecycle operations. The callback exclusively owns its
 * job data between successful Submit and completion acquired by TakeResult.
 * Idle waiting belongs to the execution thread; owner operations never wait.
 * Backend resource creation and callback allocations remain separate audit scopes.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_atomic.h"
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
 * \brief Execution state, separate from the shutdown latch.
 */
typedef enum OS_Thread_State_e
{
    OS_THREAD_CLOSED,   ///< No native resources or callback activity remain.
    OS_THREAD_READY,    ///< Owner may prepare and submit one context.
    OS_THREAD_RUNNING,  ///< Accepted job owns its context.
    OS_THREAD_COMPLETE, ///< Callback returned; result awaits owner acquisition.
    OS_THREAD_FAILED    ///< Native failure prevents further execution.
} OS_Thread_State_t;

/*************************************************************************************************/
/**
 * \brief One job invocation on the persistent execution thread.
 *
 * \par Requirements:
 *  - 001 The callback shall eventually return, observing its own cancellation contract.
 *  - 002 Context storage shall remain owned by the caller and live until thread closure.
 *
 * \param[in,out] context   Fixed caller-owned job context
 *
 * \retval        None   No result value.
 */
typedef void (*OS_Thread_Proc_t)(void* context);

/*************************************************************************************************/
/**
 * \brief Caller-owned persistent thread and publication state.
 *
 * \par Details:
 *  Do not copy, move, pack or directly access fields after Initialize.
 *  Native resource identities are opaque to callers; only the selected backend
 *  interprets them. All synchronization words use OS_Atomic operations.
 */
typedef struct OS_Thread_s
{
    void*            nativeThread; ///< Private native execution resource.
    void*            nativeWake;   ///< Private native wake resource.
    OS_Thread_Proc_t run;          ///< Callback fixed for this lifetime.
    void*            context;      ///< Caller-owned context fixed for this lifetime.
    OS_Atomic32_t    state;        ///< Published OS_Thread_State_t.
    OS_Atomic32_t    stopping;     ///< Latched shutdown request.
    OS_Atomic32_t    error;        ///< First native failure, or zero.
} OS_Thread_t;

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
 * \brief Initializes a persistent execution thread during application startup.
 *
 * \par Requirements:
 *  - 001 Required storage and callback shall assert when missing.
 *  - 002 The caller shall provide fresh storage or an object whose TryClose previously succeeded.
 *  - 003 The thread shall remain idle until a successful Submit; its callback and context shall remain fixed.
 *  - 004 Failure shall leave an initialized object; TryClose shall release any retained native resources.
 *
 * \param[out]    thread    Caller-owned storage, valid until TryClose succeeds
 * \param[in]     run       Required callback invoked once per accepted submission
 * \param[in,out] context   Caller-owned callback context, optionally NULL
 *
 * \retval        TRUE    The idle execution thread is available.
 * \retval        FALSE   Native initialization failed; call TryClose before releasing storage.
 */
bool_t OS_Thread_Initialize(OS_Thread_t* thread, OS_Thread_Proc_t run, void* context);

/*************************************************************************************************/
/**
 * \brief Observes the execution state with acquire ordering.
 *
 * \par Requirements:
 *  - 001 The object shall have been initialized and remain alive.
 *  - 002 Observing COMPLETE shall acquire the callback's writes but shall not rearm submission.
 *  - 003 Observing FAILED shall establish that no callback is accessing the context; TryClose still owns native release.
 *
 * \param[in] thread   Required initialized object
 *
 * \retval    state   Current execution state; stopping is queried separately.
 */
OS_Thread_State_t OS_Thread_GetState(const OS_Thread_t* thread);

/*************************************************************************************************/
/**
 * \brief Gets the first platform error recorded by this lifetime.
 *
 * \par Requirements:
 *  - 001 Zero shall denote no recorded native failure.
 *  - 002 The native implementation defines the nonzero error domain; the code remains until reinitialization.
 *
 * \param[in] thread   Required initialized object
 *
 * \retval    0         No recorded platform error.
 * \retval    nonzero   First platform error from initialization, wake, wait or release.
 */
uint32_t OS_Thread_GetError(const OS_Thread_t* thread);

/*************************************************************************************************/
/**
 * \brief Queries the latched shutdown request.
 *
 * \par Requirements:
 *  - 001 Once set, the request shall remain set until the closed object is reinitialized.
 *
 * \param[in] thread   Required initialized object
 *
 * \retval    TRUE    Shutdown has been requested.
 * \retval    FALSE   Shutdown has not been requested.
 */
bool_t OS_Thread_IsStopping(const OS_Thread_t* thread);

/*************************************************************************************************/
/**
 * \brief Publishes one job to an idle persistent thread without creating resources.
 *
 * \par Requirements:
 *  - 001 The owner shall serialize Submit, TakeResult, Stop and TryClose.
 *  - 002 The owner shall finish writing the context before Submit and shall not access callback-owned fields until completion.
 *  - 003 Only READY shall accept a submission; stopping shall reject all later submissions.
 *  - 004 Accepted work shall execute exactly once even after Stop, unless a native failure publishes FAILED.
 *  - 005 No resource creation, allocation, retry loop or blocking wait shall occur.
 *
 * \param[in,out] thread   Required initialized object
 *
 * \retval        TRUE    The callback owns the submitted context.
 * \retval        FALSE   Busy, stopped, closed, failed, or native wake failure; no new job was accepted.
 */
bool_t OS_Thread_Submit(OS_Thread_t* thread);

/*************************************************************************************************/
/**
 * \brief Acquires one completed job and rearms its context for owner use.
 *
 * \par Requirements:
 *  - 001 Only COMPLETE shall be changed to READY, exactly once.
 *  - 002 A stopped object may yield its last accepted result but shall not accept another job.
 *  - 003 No blocking wait or resource creation shall occur.
 *
 * \param[in,out] thread   Required initialized object
 *
 * \retval        TRUE    The completed context is now owned by the caller.
 * \retval        FALSE   No unconsumed callback result is available.
 */
bool_t OS_Thread_TakeResult(OS_Thread_t* thread);

/*************************************************************************************************/
/**
 * \brief Latches shutdown and wakes an idle execution thread.
 *
 * \par Requirements:
 *  - 001 Accepted work shall retain its context until the callback returns; the caller shall request job cancellation separately.
 *  - 002 Repeated calls shall remain valid and retry a failed wake.
 *  - 003 The function shall never wait for callback completion or thread exit.
 *
 * \param[in,out] thread   Required initialized object
 *
 * \retval        TRUE    Shutdown is latched and the wake succeeded, or no execution thread remains.
 * \retval        FALSE   Shutdown is latched but waking the thread failed; retry Stop or TryClose.
 */
bool_t OS_Thread_Stop(OS_Thread_t* thread);

/*************************************************************************************************/
/**
 * \brief Requests shutdown and releases native resources after confirmed thread exit.
 *
 * \par Requirements:
 *  - 001 The caller shall retain the object and context until TRUE.
 *  - 002 Only a zero-time thread wait shall be used; incomplete shutdown shall preserve resources.
 *  - 003 Each successful native release shall clear its identity; failed releases shall remain retryable.
 *  - 004 Repeated calls after success shall return TRUE.
 *  - 005 Unconsumed results shall be discarded when resources are completely closed.
 *
 * \param[in,out] thread   Required initialized object, including after failed Initialize
 *
 * \retval        TRUE    No native resource remains and the callback cannot access the context.
 * \retval        FALSE   Work is still active or a native operation failed; storage must stay alive.
 */
bool_t OS_Thread_TryClose(OS_Thread_t* thread);

#endif /* OS_THREAD_H */
