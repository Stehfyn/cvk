/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Startup memory ownership and repeated per-cast preparation without new storage.
 * No receiver is needed. This fixture does not exercise channel or codec allocation.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <stdint.h>
#include "cast_session.h"
#include "os/os_memory.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include "crt/crt_memory.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define STORAGE_TEST_BYTE_COUNT  (8193) ///< Covers complete and partial native pages.
#define STORAGE_TEST_LAST_VALUE  (0xA5) ///< Distinguishable retained final byte.
#define STORAGE_TEST_REPETITIONS (1000) ///< Cast lifetimes on one startup allocation.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief A caller-defined object spanning complete and partial native pages.
 */
typedef struct Storage_s
{
    uint8_t bytes[STORAGE_TEST_BYTE_COUNT]; ///< Fixed object constructed in startup storage.
} Storage_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static OS_Memory_t       g_memory;
static CastSession_t     g_session;
static CastSessionConfig g_config;
static const Storage_t   g_zero = {{0}};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Rejects callback invocation by preparation and reset operations.
 *
 * \par Details:
 *  Any invocation asserts.
 *
 * \par Requirements:
 *  - 001 None of the exercised owner operations shall dispatch an event.
 *
 * \param[in,out] context   Unused callback context
 * \param[in]     event     Unused event kind
 * \param[in]     status    Unused borrowed status
 * \param[in]     text      Unused borrowed diagnostic
 *
 * \retval        None No value is returned.
 */
static OS_NORETURN FORCE_INLINE void RejectEvent(void* context, CastEvent event, const CastStatus* status, const char* text);

/*************************************************************************************************/
/**
 * \brief Checks zeroed writable storage and idempotent complete closure.
 *
 * \par Details:
 *  All assertions pass or the test process fails.
 *
 * \par Requirements:
 *  - 001 The fixture shall have initialized successfully and remain exclusively owned.
 *  - 002 The last byte of the declared extent shall retain a written value across getter calls.
 *  - 003 Closed storage shall publish no address or usable byte count.
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckMemory(void);

/*************************************************************************************************/
/**
 * \brief Exercises command and storage ownership for one prepared cast.
 *
 * \par Details:
 *  The fixture is reset for the next cast.
 *
 * \par Requirements:
 *  - 001 Startup storage shall remain unchanged across rejected closure and Reset.
 *  - 002 Each preparation shall admit Play before Stop and reject Play after Stop.
 *
 * \param[in] address     Expected startup region address
 * \param[in] byteCount   Expected startup usable extent
 *
 * \retval    None No value is returned.
 */
static FORCE_INLINE void CheckPreparedCast(const void* address, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Checks repeated command admission and storage lifetime across prepared casts.
 *
 * \par Details:
 *  All assertions pass or the test process fails.
 *
 * \par Requirements:
 *  - 001 Every cast shall reuse the same startup address and extent.
 *  - 002 Stop shall reject ordinary commands in that cast; the next Prepare shall admit them again.
 *  - 003 Closure while prepared shall reject without revoking usable storage.
 *  - 004 Repeated Reset and successful closure shall be idempotent.
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckSession(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Run is deliberately never called. Owner-only lifecycle operations cannot emit events.
 */
static OS_NORETURN FORCE_INLINE void RejectEvent(void* context, CastEvent event, const CastStatus* status, const char* text)
{
    (void)context;
    (void)event;
    (void)status;
    (void)text;

    LOGASSERT(FALSE);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The zeroing check covers the entire promised extent, including the final partial page.
 */
static FORCE_INLINE void CheckMemory(void)
{
    Storage_t* storage;
    int32_t    comparison;
    bool_t     closed;

    LOGASSERT(OS_Memory_IsReady(&g_memory));
    LOGASSERT(sizeof(Storage_t) == OS_Memory_GetByteCount(&g_memory));
    LOGASSERT(0 == OS_Memory_GetError(&g_memory));
    storage = (Storage_t*)OS_Memory_GetAddress(&g_memory);
    LOGASSERT(NULL != storage);
    comparison = CRT_Memory_Compare(storage, &g_zero, sizeof(Storage_t));
    LOGASSERT(0 == comparison);
    storage->bytes[0]                           = 1;
    storage->bytes[STORAGE_TEST_BYTE_COUNT - 1] = STORAGE_TEST_LAST_VALUE;
    LOGASSERT(storage == OS_Memory_GetAddress(&g_memory));
    LOGASSERT(1 == storage->bytes[0]);
    LOGASSERT(STORAGE_TEST_LAST_VALUE == storage->bytes[STORAGE_TEST_BYTE_COUNT - 1]);
    closed = OS_Memory_TryClose(&g_memory);
    LOGASSERT(closed);
    LOGASSERT(!OS_Memory_IsReady(&g_memory));
    LOGASSERT(NULL == OS_Memory_GetAddress(&g_memory));
    LOGASSERT(0 == OS_Memory_GetByteCount(&g_memory));
    closed = OS_Memory_TryClose(&g_memory);
    LOGASSERT(closed);
    LOGASSERT(0 == OS_Memory_GetError(&g_memory));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Both media publishers copy bounded text during the prepared lifetime. Stop
 *  remains a queue policy; it does not permit premature storage closure.
 */
static FORCE_INLINE void CheckPreparedCast(const void* address, uint64_t byteCount)
{
    bool_t accepted;
    bool_t closed;

    CastSession_Prepare(&g_session, &g_config);
    LOGASSERT(CastSession_IsPrepared(&g_session));
    LOGASSERT(!CastSession_IsRunning(&g_session));
    CastSession_SetProxy(&g_session, "http://localhost/proxy.mp4", "video/mp4");
    CastSession_ReplaceMedia(&g_session, "http://localhost/gain.mp4", "video/mp4");
    accepted = CastSession_Post(&g_session, CAST_COMMAND_PLAY, 0.0);
    LOGASSERT(accepted);
    accepted = CastSession_Post(&g_session, CAST_COMMAND_STOP, 0.0);
    LOGASSERT(accepted);
    accepted = CastSession_Post(&g_session, CAST_COMMAND_PLAY, 0.0);
    LOGASSERT(!accepted);
    closed = CastSession_TryClose(&g_session);
    LOGASSERT(!closed);
    LOGASSERT(CastSession_IsPrepared(&g_session));
    LOGASSERT(address == OS_Memory_GetAddress(&g_session.memory));
    LOGASSERT(byteCount == OS_Memory_GetByteCount(&g_session.memory));
    CastSession_Reset(&g_session);
    CastSession_Reset(&g_session);
    LOGASSERT(!CastSession_IsPrepared(&g_session));
    LOGASSERT(address == OS_Memory_GetAddress(&g_session.memory));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Independent admission outcomes detect a stale Stop latch after reuse. No private
 *  implementation state is fabricated and no network failure is used as a test oracle.
 */
static FORCE_INLINE void CheckSession(void)
{
    void*    address;
    uint64_t byte_count;
    uint32_t iteration;
    bool_t   closed;

    address    = OS_Memory_GetAddress(&g_session.memory);
    byte_count = OS_Memory_GetByteCount(&g_session.memory);
    LOGASSERT(NULL != address);
    LOGASSERT(0 < byte_count);
    LOGASSERT(!CastSession_IsPrepared(&g_session));
    LOGASSERT(!CastSession_IsRunning(&g_session));
    for (iteration = 0; STORAGE_TEST_REPETITIONS > iteration; ++iteration)
    {
        CheckPreparedCast(address, byte_count);
    }
    closed = CastSession_TryClose(&g_session);
    LOGASSERT(closed);
    closed = CastSession_TryClose(&g_session);
    LOGASSERT(closed);
    LOGASSERT(NULL == OS_Memory_GetAddress(&g_session.memory));
    LOGASSERT(0 == OS_Memory_GetByteCount(&g_session.memory));
    LOGASSERT(0 == OS_Memory_GetError(&g_session.memory));
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  All fixture storage is acquired before any exercised cast lifecycle begins.
 */
uint32_t OS_Application_Main(void)
{
    bool_t memory_ready;
    bool_t session_ready;

    memory_ready  = OS_Memory_Initialize(&g_memory, sizeof(Storage_t));
    session_ready = CastSession_Initialize(&g_session);
    LOGASSERT(memory_ready);
    LOGASSERT(session_ready);
    CRT_Memory_Set(&g_config, 0, sizeof(CastSessionConfig));
    g_config.callback = RejectEvent;
    CheckMemory();
    CheckSession();

    return 0;
}
