/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Native contention harness for the build-selected OS atomic implementation. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_atomic.h"
#include "os/os_assert.h"
#include "os/os_process.h"

#include <windows.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define ATOMIC_TEST_THREADS     (4)
#define ATOMIC_TEST_ITERATIONS  (100000)
#define ATOMIC_TEST_TOTAL       (ATOMIC_TEST_THREADS * ATOMIC_TEST_ITERATIONS)
#define ATOMIC_TEST_TIMEOUT     (10000)
#define ATOMIC_TEST_STACK_BYTES (65536)
#define ATOMIC_TEST_STORED32    (0x87654321)
#define ATOMIC_TEST_STORED64    (UINT64_MAX - 127)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Per-thread results with shared atomic counter references.
 */
typedef struct CounterWorker_s
{
    HANDLE         gate;          ///< Initialization gate.
    OS_Atomic32_t* counter32;     ///< Shared ticket source.
    OS_Atomic64_t* counter64;     ///< Shared ticket source.
    OS_Atomic32_t* conditional32; ///< Shared conditional increment target.
    OS_Atomic64_t* conditional64; ///< Shared conditional increment target.
    uint64_t       sum32;         ///< Private ticket sum.
    uint64_t       sum64;         ///< Private ticket sum.
    uint32_t       success32;     ///< Private successful increment count.
    uint32_t       success64;     ///< Private successful increment count.
} CounterWorker_t;

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
 * \brief Checks high bits, wraparound and both strong compare/exchange outcomes.
 *
 * \par Requirements:
 *  - 001 Assertions shall remain active in Release.
 *
 * \retval None
 */
static void CheckValues(void);

/*************************************************************************************************/
/**
 * \brief Collects atomic tickets and successful conditional increments.
 *
 * \par Requirements:
 *  - 001 The test owner shall provide initialized shared words and a live startup gate.
 *  - 002 Only this worker shall write its result fields until it exits.
 *
 * \param[in,out] context   Non-NULL counter-worker fixture
 *
 * \retval        0   Fixed iteration count completed.
 */
static DWORD WINAPI RunCounters(LPVOID context);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Independent expected values exercise each width without a concurrent participant.
 */
static void CheckValues(void)
{
    OS_Atomic32_t word32;
    OS_Atomic64_t word64;

    OS_Atomic_Initialize32(&word32, UINT32_MAX);
    OS_Atomic_Initialize64(&word64, UINT64_MAX);
    LOGASSERT(UINT32_MAX == OS_Atomic_Load32(&word32));
    LOGASSERT(UINT64_MAX == OS_Atomic_Load64(&word64));
    LOGASSERT(UINT32_MAX == OS_Atomic_FetchAdd32(&word32, 1));
    LOGASSERT(UINT64_MAX == OS_Atomic_FetchAdd64(&word64, 1));
    LOGASSERT(0 == OS_Atomic_Load32(&word32));
    LOGASSERT(0 == OS_Atomic_Load64(&word64));
    LOGASSERT(0 == OS_Atomic_CompareExchange32(&word32, 1, 9));
    LOGASSERT(0 == OS_Atomic_CompareExchange64(&word64, 1, 9));
    LOGASSERT(0 == OS_Atomic_Load32(&word32));
    LOGASSERT(0 == OS_Atomic_Load64(&word64));
    LOGASSERT(0 == OS_Atomic_CompareExchange32(&word32, 0, UINT32_MAX - 1));
    LOGASSERT(0 == OS_Atomic_CompareExchange64(&word64, 0, UINT64_MAX - 1));
    LOGASSERT((UINT32_MAX - 1) == OS_Atomic_Exchange32(&word32, 7));
    LOGASSERT((UINT64_MAX - 1) == OS_Atomic_Exchange64(&word64, 7));
    OS_Atomic_Store32(&word32, ATOMIC_TEST_STORED32);
    OS_Atomic_Store64(&word64, ATOMIC_TEST_STORED64);
    LOGASSERT(ATOMIC_TEST_STORED32 == OS_Atomic_Load32(&word32));
    LOGASSERT(ATOMIC_TEST_STORED64 == OS_Atomic_Load64(&word64));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each compare/exchange has one attempt; failures are expected under contention.
 *  Private totals are observed only after the thread has exited.
 */
static DWORD WINAPI RunCounters(LPVOID context)
{
    CounterWorker_t* worker;
    DWORD            wait_result;
    uint32_t         index;
    uint32_t         ticket32;
    uint64_t         ticket64;
    uint32_t         expected32;
    uint64_t         expected64;
    uint32_t         observed32;
    uint64_t         observed64;

    worker = context;
    LOGASSERT(NULL != worker);
    wait_result = WaitForSingleObject(worker->gate, ATOMIC_TEST_TIMEOUT);
    LOGASSERT(WAIT_OBJECT_0 == wait_result);
    for (index = 0; index < ATOMIC_TEST_ITERATIONS; ++index)
    {
        ticket32 = OS_Atomic_FetchAdd32(worker->counter32, 1);
        ticket64 = OS_Atomic_FetchAdd64(worker->counter64, 1);
        LOGASSERT(ticket32 < ATOMIC_TEST_TOTAL);
        LOGASSERT(ticket64 < ATOMIC_TEST_TOTAL);
        worker->sum32 += ticket32;
        worker->sum64 += ticket64;
        expected32 = OS_Atomic_Load32(worker->conditional32);
        expected64 = OS_Atomic_Load64(worker->conditional64);
        observed32 = OS_Atomic_CompareExchange32(worker->conditional32, expected32, expected32 + 1);
        observed64 = OS_Atomic_CompareExchange64(worker->conditional64, expected64, expected64 + 1);
        if (expected32 == observed32)
        {
            ++worker->success32;
        }
        if (expected64 == observed64)
        {
            ++worker->success64;
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
 *  Create all test threads before opening their shared gate. The production atomic
 *  operations create no resources; native waits belong to this bounded test harness.
 */
uint32_t OS_Application_Main(void)
{
    OS_Atomic32_t   counter32;
    OS_Atomic64_t   counter64;
    OS_Atomic32_t   conditional32;
    OS_Atomic64_t   conditional64;
    HANDLE          gate;
    uint32_t        index;
    CounterWorker_t workers[ATOMIC_TEST_THREADS];
    uint64_t        sum32;
    uint64_t        sum64;
    uint32_t        success32;
    uint64_t        success64;
    HANDLE          threads[ATOMIC_TEST_THREADS];
    DWORD           wait_result;
    uint64_t        expected_sum;

    OS_Process_SetInteractive(FALSE);
    CheckValues();
    OS_Atomic_Initialize32(&counter32, 0);
    OS_Atomic_Initialize64(&counter64, 0);
    OS_Atomic_Initialize32(&conditional32, 0);
    OS_Atomic_Initialize64(&conditional64, 0);
    gate = CreateEvent(NULL, TRUE, FALSE, NULL);
    LOGASSERT(NULL != gate);
    for (index = 0; index < ATOMIC_TEST_THREADS; ++index)
    {
        workers[index].gate          = gate;
        workers[index].counter32     = &counter32;
        workers[index].counter64     = &counter64;
        workers[index].conditional32 = &conditional32;
        workers[index].conditional64 = &conditional64;
        workers[index].sum32         = 0;
        workers[index].sum64         = 0;
        workers[index].success32     = 0;
        workers[index].success64     = 0;
        threads[index]               = CreateThread(NULL, ATOMIC_TEST_STACK_BYTES, RunCounters, &workers[index], 0, NULL);
        LOGASSERT(NULL != threads[index]);
    }
    LOGASSERT(SetEvent(gate));
    wait_result = WaitForMultipleObjects(ATOMIC_TEST_THREADS, threads, TRUE, ATOMIC_TEST_TIMEOUT);
    LOGASSERT(WAIT_OBJECT_0 == wait_result);
    sum32     = 0;
    sum64     = 0;
    success32 = 0;
    success64 = 0;
    for (index = 0; index < ATOMIC_TEST_THREADS; ++index)
    {
        sum32 += workers[index].sum32;
        sum64 += workers[index].sum64;
        success32 += workers[index].success32;
        success64 += workers[index].success64;
        LOGASSERT(CloseHandle(threads[index]));
    }
    LOGASSERT(CloseHandle(gate));
    expected_sum = ((uint64_t)ATOMIC_TEST_TOTAL * (ATOMIC_TEST_TOTAL - 1)) / 2;
    LOGASSERT(ATOMIC_TEST_TOTAL == OS_Atomic_Load32(&counter32));
    LOGASSERT(ATOMIC_TEST_TOTAL == OS_Atomic_Load64(&counter64));
    LOGASSERT(expected_sum == sum32);
    LOGASSERT(expected_sum == sum64);
    LOGASSERT(success32 == OS_Atomic_Load32(&conditional32));
    LOGASSERT(success64 == OS_Atomic_Load64(&conditional64));
    LOGASSERT(success32 > 0);
    LOGASSERT(success64 > 0);

    return 0;
}
