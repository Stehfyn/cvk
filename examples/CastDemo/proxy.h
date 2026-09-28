#ifndef CAST_PROXY_H
#define CAST_PROXY_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Startup-owned proxy state, scratch identity and persistent execution.
 * Accepted work borrows its prepared Cast session through successful TryStop.
 * Conversion and native HTTP serving still require separate allocation/timing repair.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#include "os/os_memory.h"
#include "os/os_thread.h"
#include "os/os_temporaryfile.h"
#include "cast_session.h"
#include "media_server.h"
#include "gpu_transcode.h"

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
 * \brief Borrowed submission inputs copied before the persistent worker is signalled.
 */
typedef struct Proxy_Request_s
{
    MediaServer_Workspace_t*  serving;              ///< Startup serving pool retained through successful TryStop.
    const uint16_t*           source;               ///< Native-order UTF-16 path, terminated within its bound.
    uint32_t                  sourceCapacity;       ///< Readable path units, including possible termination.
    const char_t*             localAddress;         ///< Terminated canonical dotted-decimal IPv4 address.
    uint32_t                  localAddressCapacity; ///< Readable address bytes, including possible termination.
    const GpuTranscodeConfig* config;               ///< Optional settings; NULL selects existing defaults.
    uint16_t                  port;                 ///< Requested proxy listener port; zero selects one.
    CastSession_t*            session;              ///< Prepared session retained through successful TryStop.
} Proxy_Request_t;

/*************************************************************************************************/
/**
 * \brief Caller-owned lifetime; do not copy, move or directly modify initialized fields.
 */
typedef struct Proxy_s
{
    OS_Memory_t           memory; ///< Private resident state acquired at program initialization.
    OS_Thread_t           worker; ///< Fixed callback/context acquired at program initialization.
    OS_TemporaryFile_t    output; ///< Stable scratch path retained across casts.
    struct Proxy_State_s* state;  ///< Private implementation state, never exposed to borrowers.
    bool_t                ready;  ///< Complete startup succeeded and final shutdown has not begun.
    bool_t                active; ///< A job/session/server borrow remains until successful TryStop.
    bool_t                busy;   ///< An accepted callback still owns its context.
} Proxy_t;

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
 * \brief Establishes proxy storage, one scratch file and one idle worker at program startup.
 *
 * \par Requirements:
 *  - 001 Required owner storage shall assert; the caller shall provide fresh or completely closed storage.
 *  - 002 Every subowner shall be initialized even if another startup operation fails.
 *  - 003 Failure shall retain acquired resources for TryClose; no submission shall become available.
 *  - 004 Only this operation shall acquire the proxy's state, execution resource and scratch identity.
 *
 * \param[out] proxy   Required stable caller-owned lifetime record
 *
 * \retval     TRUE    Persistent resources are ready for a submission.
 * \retval     FALSE   Startup failed; retain the owner and call TryClose.
 */
bool_t Proxy_Initialize(Proxy_t* proxy);

/*************************************************************************************************/
/**
 * \brief Queries whether a new request can be submitted.
 *
 * \par Requirements:
 *  - 001 All owner API calls shall be serialized; callback-owned fields shall not be inspected while busy.
 *  - 002 An accepted request shall require successful TryStop before another submission.
 *
 * \param[in] proxy   Required initialized owner
 *
 * \retval    TRUE    Startup remains ready and the worker has no retained job.
 * \retval    FALSE   Busy, failed startup/worker, or final shutdown.
 */
bool_t Proxy_IsReady(const Proxy_t* proxy);

/*************************************************************************************************/
/**
 * \brief Gets retained native failure evidence from the constituent owners.
 *
 * \par Requirements:
 *  - 001 Worker, memory and scratch-file errors shall be queried in that priority order.
 *  - 002 A conversion rejection shall remain separate from native owner failure.
 *
 * \param[in] proxy   Required initialized owner, including after closure
 *
 * \retval    0         No constituent owner recorded a native failure.
 * \retval    nonzero   A retained backend error code.
 */
uint32_t Proxy_GetError(const Proxy_t* proxy);

/*************************************************************************************************/
/**
 * \brief Copies one validated request into retained storage and submits the existing worker.
 *
 * \par Requirements:
 *  - 001 Missing owner, request, serving pool, source, address or session storage shall assert as caller misuse.
 *  - 002 The session shall be prepared; session and serving pool shall remain alive until successful TryStop; request strings shall be disjoint from owner storage.
 *  - 003 Invalid, unterminated, empty or unsupported-capacity paths/addresses shall reject without submitting work.
 *  - 004 Caller inputs shall be copied before publication; the callback shall never borrow request storage.
 *  - 005 No proxy allocation, thread/file creation or formatting through the CRT shall occur in submission.
 *  - 006 Conversion/HTTP dependencies still allocate and block in the accepted callback; they remain goal defects.
 *
 * \param[in,out] proxy     Required initialized owner, serialized by the caller
 * \param[in]     request   Complete borrowed job inputs
 *
 * \retval        TRUE    The persistent callback owns the copied request.
 * \retval        FALSE   Invalid input, unavailable owner or failed submission; no new work was accepted.
 */
bool_t Proxy_Start(Proxy_t* proxy, const Proxy_Request_t* request);

/*************************************************************************************************/
/**
 * \brief Cancels the current job and polls until publication and serving are quiescent.
 *
 * \par Requirements:
 *  - 001 The caller shall retain the session and Winsock until TRUE.
 *  - 002 Cancellation shall use OS_Atomic; callback fields shall be acquired only after completion or worker failure.
 *  - 003 Pending server teardown shall retain its identity for another attempt.
 *  - 004 Success shall release session borrowing and rearm reuse without destroying the worker or scratch file.
 *  - 005 The last scratch contents shall remain owned until reuse or final TryClose.
 *  - 006 No blocking thread wait, owner allocation or native resource creation shall occur.
 *
 * \param[in,out] proxy   Required initialized owner, including after failed startup
 *
 * \retval        TRUE    No callback or serving worker can use the session or source/output file.
 * \retval        FALSE   Work or native release remains pending; retain all dependencies.
 */
bool_t Proxy_TryStop(Proxy_t* proxy);

/*************************************************************************************************/
/**
 * \brief Ends the proxy lifetime after job, worker, file and storage teardown complete.
 *
 * \par Requirements:
 *  - 001 The first attempt shall revoke readiness and request job cancellation.
 *  - 002 Worker closure shall precede scratch removal and private-storage release.
 *  - 003 Each failed native release shall retain its constituent owner for retry.
 *  - 004 Repeated success shall remain valid; no caller storage may be released before TRUE.
 *
 * \param[in,out] proxy   Required initialized owner, including after failed startup
 *
 * \retval        TRUE    All proxy-owned startup resources are released.
 * \retval        FALSE   Shutdown remains pending; retain the owner and dependencies.
 */
bool_t Proxy_TryClose(Proxy_t* proxy);

#endif /* CAST_PROXY_H */
