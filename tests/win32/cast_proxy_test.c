/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Persistent proxy context reuse, rejection, completion acquisition and shutdown.
 * An empty startup-created source is rejected by the production container reader
 * before GPU creation. No receiver or active Cast session runner is used.
 * Dependency allocation remains a goal defect and is not certified by this fixture.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <WinSock2.h>
#include <Windows.h>
#include <minwindef.h>
#include <synchapi.h>
#include <stdint.h>
#include "proxy.h"
#include "media_server.h"
#include "cast_session.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include "os/os_memory.h"
#include "os/os_thread.h"
#include "os/os_temporaryfile.h"
#include "utl/utl_unicode.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define PROXY_TEST_REPETITIONS (32)
#define PROXY_TEST_POLL_LIMIT  (1000)
#define PROXY_TEST_POLL_MS     (5)
#define PROXY_TEST_ADDRESS     ("127.0.0.1")
#define PROXY_TEST_BAD_ADDRESS ("127.0.0.999")
#define PROXY_TEST_SURROGATE   (0xD800)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static Proxy_t                 g_proxy;
static OS_TemporaryFile_t      g_source;
static CastSession_t           g_session;
static CastSessionConfig       g_config;
static Proxy_Request_t         g_request;
static MediaServer_Workspace_t g_serving;

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Rejects events from the deliberately inactive session runner.
 *
 * \par Details:
 *  Any invocation terminates the fixture.
 *
 * \par Requirements:
 *  - 001 No exercised owner operation shall invoke a session callback.
 *
 * \param[in,out] context   Unused context
 * \param[in]     event     Unused event kind
 * \param[in]     status    Unused status
 * \param[in]     text      Unused diagnostic
 *
 * \retval        None
 */
static OS_NORETURN FORCE_INLINE void RejectEvent(void*             context,
                                                 CastEvent         event,
                                                 const CastStatus* status,
                                                 const char_t*     text);

/*************************************************************************************************/
/**
 * \brief Checks input rejection without submitting or changing startup ownership.
 *
 * \par Details:
 *  All rejection assertions pass or the fixture fails.
 *
 * \par Requirements:
 *  - 001 The session shall be prepared; the proxy shall initially be ready.
 *  - 002 Invalid UTF-16, empty/unterminated source and invalid/unterminated IPv4 shall leave the proxy ready.
 *
 * \retval None
 */
static FORCE_INLINE void CheckRejectedInput(void);

/*************************************************************************************************/
/**
 * \brief Acquires per-job teardown or final closure under a finite fixture deadline.
 *
 * \par Details:
 *  Teardown completed or the fixture fails.
 *
 * \par Requirements:
 *  - 001 The prepared session and startup source shall stay alive until completion.
 *  - 002 No polling timeout shall be accepted as completed teardown.
 *
 * \param[in] final   TRUE requests final closure; FALSE requests reusable per-job stop
 *
 * \retval    None
 */
static FORCE_INLINE void AwaitStop(bool_t final);

/*************************************************************************************************/
/**
 * \brief Waits for natural conversion rejection while retaining the job lifetime.
 *
 * \par Details:
 *  Completion was observed or the fixture fails.
 *
 * \par Requirements:
 *  - 001 The supplied empty source shall fail container parsing before any GPU is created.
 *  - 002 Callback completion alone shall not rearm the proxy for another request.
 *
 * \retval None
 */
static FORCE_INLINE void AwaitCompletion(void);

/*************************************************************************************************/
/**
 * \brief Checks repeated submission, cancellation and context reuse against stable startup storage.
 *
 * \par Details:
 *  All assertions pass or the fixture fails.
 *
 * \par Requirements:
 *  - 001 The memory address, extent and scratch-path address shall remain stable across jobs.
 *  - 002 A second submission shall fail before per-job stop, even if the callback already completed.
 *
 * \retval None
 */
static FORCE_INLINE void CheckReuse(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The session runner is never submitted by this fixture.
 */
static OS_NORETURN FORCE_INLINE void RejectEvent(void*             context,
                                                 CastEvent         event,
                                                 const CastStatus* status,
                                                 const char_t*     text)
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
 *  Each rejected request is independent and is followed by a readiness check.
 */
static FORCE_INLINE void CheckRejectedInput(void)
{
    Proxy_Request_t request;
    uint16_t        malformed[2];
    uint16_t        empty[1];
    bool_t          accepted;

    request                = g_request;
    malformed[0]           = PROXY_TEST_SURROGATE;
    malformed[1]           = UTL_UNICODE_TERMINATOR;
    empty[0]               = UTL_UNICODE_TERMINATOR;
    request.source         = malformed;
    request.sourceCapacity = 2;
    accepted               = Proxy_Start(&g_proxy, &request);
    LOGASSERT(!accepted);
    LOGASSERT(Proxy_IsReady(&g_proxy));
    request.source         = empty;
    request.sourceCapacity = 1;
    accepted               = Proxy_Start(&g_proxy, &request);
    LOGASSERT(!accepted);
    LOGASSERT(Proxy_IsReady(&g_proxy));
    request                = g_request;
    request.sourceCapacity = 1;
    accepted               = Proxy_Start(&g_proxy, &request);
    LOGASSERT(!accepted);
    LOGASSERT(Proxy_IsReady(&g_proxy));
    request                      = g_request;
    request.localAddress         = PROXY_TEST_BAD_ADDRESS;
    request.localAddressCapacity = sizeof(PROXY_TEST_BAD_ADDRESS);
    accepted                     = Proxy_Start(&g_proxy, &request);
    LOGASSERT(!accepted);
    LOGASSERT(Proxy_IsReady(&g_proxy));
    request                      = g_request;
    request.localAddressCapacity = sizeof(PROXY_TEST_ADDRESS) - 1;
    accepted                     = Proxy_Start(&g_proxy, &request);
    LOGASSERT(!accepted);
    LOGASSERT(Proxy_IsReady(&g_proxy));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Sleeps belong to the fixture deadline, not to proxy owner operations.
 */
static FORCE_INLINE void AwaitStop(bool_t final)
{
    bool_t   stopped;
    uint32_t attempt;

    stopped = FALSE;
    for (attempt = 0; (attempt < PROXY_TEST_POLL_LIMIT) && (!stopped); ++attempt)
    {
        if (final)
        {
            stopped = Proxy_TryClose(&g_proxy);
        }
        else
        {
            stopped = Proxy_TryStop(&g_proxy);
        }
        if (!stopped)
        {
            Sleep(PROXY_TEST_POLL_MS);
        }
    }
    LOGASSERT(stopped);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Observing COMPLETE is deliberately separate from acquiring/rearming the job.
 */
static FORCE_INLINE void AwaitCompletion(void)
{
    bool_t            complete;
    uint32_t          attempt;
    OS_Thread_State_t state;

    complete = FALSE;
    for (attempt = 0; (attempt < PROXY_TEST_POLL_LIMIT) && (!complete); ++attempt)
    {
        state    = OS_Thread_GetState(&g_proxy.worker);
        complete = (OS_THREAD_COMPLETE == state);
        LOGASSERT(OS_THREAD_FAILED != state);
        if (!complete)
        {
            Sleep(PROXY_TEST_POLL_MS);
        }
    }
    LOGASSERT(complete);
    LOGASSERT(!Proxy_IsReady(&g_proxy));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Alternates immediate cancellation with natural malformed-source completion.
 */
static FORCE_INLINE void CheckReuse(void)
{
    void*           memory;
    uint64_t        bytes;
    const uint16_t* output;
    uint32_t        repetition;
    bool_t          accepted;
    bool_t          stopped;

    memory = OS_Memory_GetAddress(&g_proxy.memory);
    bytes  = OS_Memory_GetByteCount(&g_proxy.memory);
    output = OS_TemporaryFile_GetPath(&g_proxy.output);
    LOGASSERT(NULL != memory);
    LOGASSERT(0 < bytes);
    LOGASSERT(NULL != output);
    for (repetition = 0; repetition < PROXY_TEST_REPETITIONS; ++repetition)
    {
        CastSession_Prepare(&g_session, &g_config);
        accepted = Proxy_Start(&g_proxy, &g_request);
        LOGASSERT(accepted);
        LOGASSERT(!Proxy_IsReady(&g_proxy));
        accepted = Proxy_Start(&g_proxy, &g_request);
        LOGASSERT(!accepted);
        if (repetition & 1)
        {
            AwaitCompletion();
        }
        AwaitStop(FALSE);
        stopped = Proxy_TryStop(&g_proxy);
        LOGASSERT(stopped);
        LOGASSERT(Proxy_IsReady(&g_proxy));
        LOGASSERT(memory == OS_Memory_GetAddress(&g_proxy.memory));
        LOGASSERT(bytes == OS_Memory_GetByteCount(&g_proxy.memory));
        LOGASSERT(output == OS_TemporaryFile_GetPath(&g_proxy.output));
        LOGASSERT(OS_THREAD_READY == OS_Thread_GetState(&g_proxy.worker));
        CastSession_Reset(&g_session);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Every owned storage, file and worker is established before request exercises.
 *  Final proxy closure precedes session reset and release of the source file.
 */
uint32_t OS_Application_Main(void)
{
    int32_t  status;
    WSADATA  winsock;
    bool_t   ready;
    bool_t   accepted;
    bool_t   closed;
    uint32_t attempt;

    status = WSAStartup(MAKEWORD(2, 2), &winsock);
    LOGASSERT(0 == status);
    ready = MediaServer_Initialize(&g_serving);
    LOGASSERT(ready);
    ready = OS_TemporaryFile_Initialize(&g_source);
    LOGASSERT(ready);
    ready = CastSession_Initialize(&g_session);
    LOGASSERT(ready);
    ready = Proxy_Initialize(&g_proxy);
    LOGASSERT(ready);
    g_config.callback              = RejectEvent;
    g_request.serving              = &g_serving;
    g_request.source               = OS_TemporaryFile_GetPath(&g_source);
    g_request.sourceCapacity       = OS_TEMPORARYFILE_PATH_CAPACITY;
    g_request.localAddress         = PROXY_TEST_ADDRESS;
    g_request.localAddressCapacity = sizeof(PROXY_TEST_ADDRESS);
    g_request.config               = NULL;
    g_request.port                 = 0;
    g_request.session              = &g_session;
    CastSession_Prepare(&g_session, &g_config);
    CheckRejectedInput();
    CastSession_Reset(&g_session);
    CheckReuse();
    CastSession_Prepare(&g_session, &g_config);
    accepted = Proxy_Start(&g_proxy, &g_request);
    LOGASSERT(accepted);
    AwaitStop(TRUE);
    closed = Proxy_TryClose(&g_proxy);
    LOGASSERT(closed);
    LOGASSERT(!Proxy_IsReady(&g_proxy));
    LOGASSERT(NULL == OS_Memory_GetAddress(&g_proxy.memory));
    LOGASSERT(NULL == OS_TemporaryFile_GetPath(&g_proxy.output));
    LOGASSERT(0 == Proxy_GetError(&g_proxy));
    accepted = Proxy_Start(&g_proxy, &g_request);
    LOGASSERT(!accepted);
    CastSession_Reset(&g_session);
    closed = CastSession_TryClose(&g_session);
    LOGASSERT(closed);
    closed = OS_TemporaryFile_TryClose(&g_source);
    LOGASSERT(closed);
    closed = FALSE;
    for (attempt = 0; (attempt < PROXY_TEST_POLL_LIMIT) && (!closed); ++attempt)
    {
        closed = MediaServer_TryClose(&g_serving);
        if (closed)
        {
            break;
        }
        Sleep(PROXY_TEST_POLL_MS);
    }
    LOGASSERT(closed);
    status = WSACleanup();
    LOGASSERT(0 == status);
    return 0;
}
