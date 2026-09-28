/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Persistent execution lifecycle, context publication and fixed native resources.
 * Test callbacks deliberately block at a gate to make owner-side refusal observable.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h>
#include <minwindef.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#include <sysinfoapi.h>
#include <stdint.h>
#include "os/os_thread.h"
#include "os/os_atomic.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define THREAD_TEST_REPETITIONS (1000)       ///< Repeated jobs on the same native resources.
#define THREAD_TEST_TIMEOUT     (5000)       ///< Test failure deadline in milliseconds.
#define THREAD_TEST_RESULT_MASK (0x5A3C17E9) ///< Independently checked result transformation.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Context transferred between the owner and its execution callback.
 */
typedef struct Job_s
{
    OS_Atomic32_t gate;    ///< Test-controlled permission to finish the callback.
    OS_Atomic32_t entered; ///< Callback entry publication.
    uint32_t      input;   ///< Owner-written job input.
    uint32_t      result;  ///< Callback-written result.
    uint32_t      count;   ///< Callback-written invocation count.
} Job_t;

/*************************************************************************************************/
/**
 * \brief One persistent execution object and its independently owned job context.
 */
typedef struct Fixture_s
{
    OS_Thread_t thread; ///< Startup execution resource, closed after its scenario.
    Job_t       job;    ///< Context used only by this fixture's callback and owner.
} Fixture_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static Fixture_t g_stopped;   ///< Shutdown while the callback is held at its gate.
static Fixture_t g_repeated;  ///< Repeated result publication on unchanged resources.
static Fixture_t g_restarted; ///< Immediate rearm followed by shutdown.

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Publishes entry, waits for the test gate and calculates a result.
 *
 * \param[in,out] context   Initialized Job_t whose owner waits for completion
 *
 * \retval        None   No result value.
 */
static FORCE_INLINE void RunJob(void* context);

/*************************************************************************************************/
/**
 * \brief Waits for one callback result under the test deadline.
 *
 * \param[in,out] thread   Initialized execution object with an accepted job
 *
 * \retval        None   No result value; a deadline or worker failure asserts.
 */
static FORCE_INLINE void WaitForResult(OS_Thread_t* thread);

/*************************************************************************************************/
/**
 * \brief Closes an idle or completing thread under the test deadline.
 *
 * \param[in,out] thread   Initialized execution object
 *
 * \retval        None   No result value; an exceeded deadline asserts.
 */
static FORCE_INLINE void CloseThread(OS_Thread_t* thread);

/*************************************************************************************************/
/**
 * \brief Establishes one startup worker with a closed callback gate.
 *
 * \param[out] fixture   Fresh static storage retained until thread closure
 *
 * \retval     None   No result value; initialization failure asserts.
 */
static FORCE_INLINE void InitializeFixture(Fixture_t* fixture);

/*************************************************************************************************/
/**
 * \brief Verifies that stopping preserves an accepted job and its context.
 *
 * \param[in,out] fixture   Initialized idle fixture with its gate closed
 *
 * \retval        None   No result value; incorrect state or output asserts.
 */
static FORCE_INLINE void CheckAcceptedStop(Fixture_t* fixture);

/*************************************************************************************************/
/**
 * \brief Verifies repeated results and unchanged native resource count.
 *
 * \param[in,out] fixture   Initialized idle fixture
 *
 * \retval        None   No result value; incorrect state or output asserts.
 */
static FORCE_INLINE void CheckRepeatedJobs(Fixture_t* fixture);

/*************************************************************************************************/
/**
 * \brief Verifies an immediate rearm followed by a stop request.
 *
 * \param[in,out] fixture   Initialized idle fixture
 *
 * \retval        None   No result value; a missing second result asserts.
 */
static FORCE_INLINE void CheckRearmStop(Fixture_t* fixture);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Result fields are written only after the test releases the gate. The owner
 *  reads them only after TakeResult acquires completion.
 */
static FORCE_INLINE void RunJob(void* context)
{
    Job_t*   job;
    uint64_t deadline;
    bool_t   released;

    job      = (Job_t*)context;
    deadline = GetTickCount64() + THREAD_TEST_TIMEOUT;
    OS_Atomic_Store32(&job->entered, TRUE);
    for (;;)
    {
        released = !!OS_Atomic_Load32(&job->gate);
        if (released)
        {
            break;
        }
        LOGASSERT(GetTickCount64() < deadline);
        Sleep(1);
    }
    job->result = job->input ^ THREAD_TEST_RESULT_MASK;
    ++job->count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The wait belongs to the test harness; the API under test remains nonblocking.
 */
static FORCE_INLINE void WaitForResult(OS_Thread_t* thread)
{
    uint64_t deadline;
    bool_t   completed;

    deadline = GetTickCount64() + THREAD_TEST_TIMEOUT;
    for (;;)
    {
        completed = OS_Thread_TakeResult(thread);
        if (completed)
        {
            break;
        }
        LOGASSERT(OS_THREAD_FAILED != OS_Thread_GetState(thread));
        LOGASSERT(GetTickCount64() < deadline);
        Sleep(1);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every attempt uses the API's zero-time exit observation and retains storage.
 */
static FORCE_INLINE void CloseThread(OS_Thread_t* thread)
{
    uint64_t deadline;
    bool_t   closed;

    deadline = GetTickCount64() + THREAD_TEST_TIMEOUT;
    for (;;)
    {
        closed = OS_Thread_TryClose(thread);
        if (closed)
        {
            break;
        }
        LOGASSERT(GetTickCount64() < deadline);
        Sleep(1);
    }
    LOGASSERT(OS_THREAD_CLOSED == OS_Thread_GetState(thread));
    LOGASSERT(OS_Thread_TryClose(thread));
    LOGASSERT(OS_Thread_Stop(thread));
    LOGASSERT(!OS_Thread_Submit(thread));
    LOGASSERT(!OS_Thread_TakeResult(thread));
    LOGASSERT(0 == OS_Thread_GetError(thread));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Distinct fixtures keep every scenario's context and resource ownership explicit.
 */
static FORCE_INLINE void InitializeFixture(Fixture_t* fixture)
{
    bool_t initialized;

    OS_Atomic_Initialize32(&fixture->job.gate, FALSE);
    OS_Atomic_Initialize32(&fixture->job.entered, FALSE);
    fixture->job.input  = 0;
    fixture->job.result = 0;
    fixture->job.count  = 0;
    initialized         = OS_Thread_Initialize(&fixture->thread, RunJob, &fixture->job);
    LOGASSERT(initialized);
    LOGASSERT(OS_THREAD_READY == OS_Thread_GetState(&fixture->thread));
    LOGASSERT(!OS_Thread_IsStopping(&fixture->thread));
    LOGASSERT(0 == OS_Thread_GetError(&fixture->thread));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The callback cannot finish before the owner confirms closure is refused.
 *  Stop must preserve the accepted job even if the native thread has not run yet.
 */
static FORCE_INLINE void CheckAcceptedStop(Fixture_t* fixture)
{
    uint64_t deadline;
    bool_t   entered;

    LOGASSERT(OS_Thread_Submit(&fixture->thread));
    LOGASSERT(OS_Thread_Stop(&fixture->thread));
    LOGASSERT(OS_Thread_IsStopping(&fixture->thread));
    deadline = GetTickCount64() + THREAD_TEST_TIMEOUT;
    for (;;)
    {
        entered = !!OS_Atomic_Load32(&fixture->job.entered);
        if (entered)
        {
            break;
        }
        LOGASSERT(GetTickCount64() < deadline);
        Sleep(1);
    }
    LOGASSERT(!OS_Thread_Submit(&fixture->thread));
    LOGASSERT(!OS_Thread_TakeResult(&fixture->thread));
    LOGASSERT(!OS_Thread_TryClose(&fixture->thread));
    LOGASSERT(OS_Thread_Stop(&fixture->thread));
    OS_Atomic_Store32(&fixture->job.gate, TRUE);
    WaitForResult(&fixture->thread);
    LOGASSERT(1 == fixture->job.count);
    LOGASSERT(THREAD_TEST_RESULT_MASK == fixture->job.result);
    CloseThread(&fixture->thread);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Context reads occur only after completion acquisition. The result equation
 *  and invocation count detect stale output, lost work and duplicate execution.
 */
static FORCE_INLINE void CheckRepeatedJobs(Fixture_t* fixture)
{
    DWORD    handles_before;
    uint32_t index;
    DWORD    handles_after;

    LOGASSERT(GetProcessHandleCount(GetCurrentProcess(), &handles_before));
    OS_Atomic_Store32(&fixture->job.gate, TRUE);
    for (index = 0; index < THREAD_TEST_REPETITIONS; ++index)
    {
        fixture->job.input = index;
        LOGASSERT(OS_Thread_Submit(&fixture->thread));
        WaitForResult(&fixture->thread);
        LOGASSERT((index + 1) == fixture->job.count);
        LOGASSERT((index ^ THREAD_TEST_RESULT_MASK) == fixture->job.result);
        LOGASSERT(!OS_Thread_TakeResult(&fixture->thread));
    }
    LOGASSERT(GetProcessHandleCount(GetCurrentProcess(), &handles_after));
    LOGASSERT(handles_before == handles_after);
    CloseThread(&fixture->thread);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The worker may still be leaving its first completion publication when the
 *  second submission and stop arrive. Both accepted callbacks must run.
 */
static FORCE_INLINE void CheckRearmStop(Fixture_t* fixture)
{
    OS_Atomic_Store32(&fixture->job.gate, TRUE);
    LOGASSERT(OS_Thread_Submit(&fixture->thread));
    WaitForResult(&fixture->thread);
    fixture->job.input = THREAD_TEST_RESULT_MASK;
    LOGASSERT(OS_Thread_Submit(&fixture->thread));
    LOGASSERT(OS_Thread_Stop(&fixture->thread));
    WaitForResult(&fixture->thread);
    LOGASSERT(2 == fixture->job.count);
    LOGASSERT(0 == fixture->job.result);
    CloseThread(&fixture->thread);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  All execution resources are initialized before any callback is submitted.
 *  Each scenario consumes its results and closes its own persistent worker.
 */
uint32_t OS_Application_Main(void)
{
    InitializeFixture(&g_stopped);
    InitializeFixture(&g_repeated);
    InitializeFixture(&g_restarted);
    CheckAcceptedStop(&g_stopped);
    CheckRepeatedJobs(&g_repeated);
    CheckRearmStop(&g_restarted);

    return 0;
}
