/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Win32 persistent execution, event signalling and zero-time thread observation:
 * https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-createthread
 * https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-setevent
 * https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitforsingleobject
 * OS_Atomic defines publication ordering independently of compiler intrinsics.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h>
#include <minwindef.h>
#include <winbase.h>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <winerror.h>
#include <stdint.h>
#include "os/os_thread.h"
#include "os/os_atomic.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define THREAD_STACK_BYTE_COUNT (1024 * 1024) ///< Native committed startup stack size.

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
 * \brief Records the first native error without overwriting an earlier failure.
 *
 * \param[in,out] thread   Initialized thread storage
 * \param[in]     code     Native error captured immediately after failure
 *
 * \retval        None
 */
static FORCE_INLINE void RecordError(OS_Thread_t* thread, uint32_t code);

/*************************************************************************************************/
/**
 * \brief Waits on the startup event and executes each published job once.
 *
 * \par Requirements:
 *  - 001 Only the execution thread shall use an indefinite idle wait.
 *  - 002 Completion or failure shall publish context ownership back to the owner.
 *
 * \param[in,out] parameter   Required initialized OS_Thread_t storage
 *
 * \retval        0   Shutdown or native wait failure ended the execution loop.
 */
static FORCE_INLINE DWORD WINAPI RunThread(LPVOID parameter);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  A failure reporting zero is represented by the native generic-failure code.
 */
static FORCE_INLINE void RecordError(OS_Thread_t* thread, uint32_t code)
{
    uint32_t error;

    error = code;
    if (ERROR_SUCCESS == error)
    {
        error = ERROR_GEN_FAILURE;
    }
    (void)OS_Atomic_CompareExchange32(&thread->error, 0, error);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Accepted work is checked before shutdown, so Stop does not lose a queued job.
 *  An active callback returns before the shutdown latch ends the loop.
 *  The final state load also catches a new submission made after completion was
 *  published but before this iteration observed shutdown.
 */
static FORCE_INLINE DWORD WINAPI RunThread(LPVOID parameter)
{
    OS_Thread_t* thread;
    DWORD        wait_result;
    bool_t       wait_succeeded;
    uint32_t     error;
    uint32_t     state;
    bool_t       has_job;
    bool_t       stopping;

    thread = (OS_Thread_t*)parameter;
    for (;;)
    {
        wait_result    = WaitForSingleObject(thread->nativeWake, INFINITE);
        wait_succeeded = (WAIT_OBJECT_0 == wait_result);
        if (!wait_succeeded)
        {
            error = GetLastError();
            RecordError(thread, error);
            OS_Atomic_Store32(&thread->state, OS_THREAD_FAILED);
            break;
        }
        state   = OS_Atomic_Load32(&thread->state);
        has_job = (OS_THREAD_RUNNING == state);
        if (has_job)
        {
            thread->run(thread->context);
            OS_Atomic_Store32(&thread->state, OS_THREAD_COMPLETE);
        }
        stopping = OS_Thread_IsStopping(thread);
        state    = OS_Atomic_Load32(&thread->state);
        has_job  = (OS_THREAD_RUNNING == state);
        if (stopping && (!has_job))
        {
            break;
        }
    }

    return 0;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The event and a fully committed one-megabyte stack are created only here.
 *  Failure retains any acquired resource so the owner can retry TryClose.
 */
bool_t OS_Thread_Initialize(OS_Thread_t* thread, OS_Thread_Proc_t run, void* context)
{
    uint32_t error;
    bool_t   initialized;

    LOGASSERT(NULL != thread);
    LOGASSERT(NULL != run);

    thread->nativeThread = NULL;
    thread->nativeWake   = NULL;
    thread->run          = run;
    thread->context      = context;
    OS_Atomic_Initialize32(&thread->state, OS_THREAD_READY);
    OS_Atomic_Initialize32(&thread->stopping, FALSE);
    OS_Atomic_Initialize32(&thread->error, 0);
    thread->nativeWake = CreateEvent(NULL, FALSE, FALSE, NULL);
    initialized        = (NULL != thread->nativeWake);
    if (initialized)
    {
        thread->nativeThread = CreateThread(NULL, THREAD_STACK_BYTE_COUNT, RunThread, thread, 0, NULL);
        initialized          = (NULL != thread->nativeThread);
    }
    if (!initialized)
    {
        error = GetLastError();
        RecordError(thread, error);
        OS_Atomic_Store32(&thread->state, OS_THREAD_FAILED);
    }

    return initialized;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The atomic acquire pairs with callback completion or native failure publication.
 */
OS_Thread_State_t OS_Thread_GetState(const OS_Thread_t* thread)
{
    uint32_t state;

    LOGASSERT(NULL != thread);

    state = OS_Atomic_Load32(&thread->state);

    return (OS_Thread_State_t)state;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The error word is sticky until the next initialization of closed storage.
 */
uint32_t OS_Thread_GetError(const OS_Thread_t* thread)
{
    LOGASSERT(NULL != thread);

    return OS_Atomic_Load32(&thread->error);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No native call or blocking observation is needed.
 */
bool_t OS_Thread_IsStopping(const OS_Thread_t* thread)
{
    uint32_t stopping;

    LOGASSERT(NULL != thread);

    stopping = OS_Atomic_Load32(&thread->stopping);

    return !!stopping;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A failed event signal rolls back only RUNNING; concurrent FAILED publication
 *  is preserved. Owner serialization excludes a simultaneous Stop or second Submit.
 */
bool_t OS_Thread_Submit(OS_Thread_t* thread)
{
    bool_t   stopping;
    bool_t   has_thread;
    bool_t   can_submit;
    bool_t   submitted;
    uint32_t previous;
    bool_t   claimed;
    uint32_t error;

    LOGASSERT(NULL != thread);

    stopping   = OS_Thread_IsStopping(thread);
    has_thread = (NULL != thread->nativeThread);
    can_submit = has_thread && (!stopping);
    submitted  = FALSE;
    if (can_submit)
    {
        previous = OS_Atomic_CompareExchange32(&thread->state, OS_THREAD_READY, OS_THREAD_RUNNING);
        claimed  = (OS_THREAD_READY == previous);
        if (claimed)
        {
            submitted = SetEvent(thread->nativeWake);
            if (!submitted)
            {
                error = GetLastError();
                RecordError(thread, error);
                (void)OS_Atomic_CompareExchange32(&thread->state, OS_THREAD_RUNNING, OS_THREAD_READY);
            }
        }
    }

    return submitted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The single exchange acquires callback writes and allows later context reuse.
 */
bool_t OS_Thread_TakeResult(OS_Thread_t* thread)
{
    uint32_t previous;
    bool_t   completed;

    LOGASSERT(NULL != thread);

    previous  = OS_Atomic_CompareExchange32(&thread->state, OS_THREAD_COMPLETE, OS_THREAD_READY);
    completed = (OS_THREAD_COMPLETE == previous);

    return completed;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A failed wake does not clear the shutdown request; later Stop or TryClose
 *  retries it. Running callbacks also observe shutdown after returning.
 */
bool_t OS_Thread_Stop(OS_Thread_t* thread)
{
    bool_t   has_thread;
    bool_t   has_wake;
    bool_t   needs_wake;
    bool_t   signalled;
    uint32_t error;

    LOGASSERT(NULL != thread);

    OS_Atomic_Store32(&thread->stopping, TRUE);
    has_thread = (NULL != thread->nativeThread);
    has_wake   = (NULL != thread->nativeWake);
    needs_wake = has_thread && has_wake;
    signalled  = TRUE;
    if (needs_wake)
    {
        signalled = SetEvent(thread->nativeWake);
        if (!signalled)
        {
            error = GetLastError();
            RecordError(thread, error);
        }
    }

    return signalled;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  An exit observation precedes every release of live-thread resources.
 *  Failed CloseHandle operations retain their identities for a later retry.
 */
bool_t OS_Thread_TryClose(OS_Thread_t* thread)
{
    bool_t   has_thread;
    bool_t   quiescent;
    DWORD    wait_result;
    bool_t   wait_failed;
    uint32_t error;
    bool_t   released;
    bool_t   has_wake;
    bool_t   closed;

    LOGASSERT(NULL != thread);

    (void)OS_Thread_Stop(thread);
    has_thread = (NULL != thread->nativeThread);
    quiescent  = !has_thread;
    if (has_thread)
    {
        wait_result = WaitForSingleObject(thread->nativeThread, 0);
        wait_failed = (WAIT_FAILED == wait_result);
        if (wait_failed)
        {
            error = GetLastError();
            RecordError(thread, error);
        }
        quiescent = (WAIT_OBJECT_0 == wait_result);
    }
    if (quiescent)
    {
        if (has_thread)
        {
            released = CloseHandle(thread->nativeThread);
            if (released)
            {
                thread->nativeThread = NULL;
            }
            else
            {
                error = GetLastError();
                RecordError(thread, error);
            }
        }
        has_wake = (NULL != thread->nativeWake);
        if (has_wake)
        {
            released = CloseHandle(thread->nativeWake);
            if (released)
            {
                thread->nativeWake = NULL;
            }
            else
            {
                error = GetLastError();
                RecordError(thread, error);
            }
        }
    }
    closed = (NULL == thread->nativeThread) && (NULL == thread->nativeWake);
    if (closed)
    {
        OS_Atomic_Store32(&thread->state, OS_THREAD_CLOSED);
    }

    return closed;
}
