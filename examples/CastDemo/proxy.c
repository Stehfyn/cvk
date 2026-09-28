/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Reusable job ownership; native storage/thread/file operations belong to OS modules.
 * Complete-file proxy serving preserves fixed Content-Length and seekable VOD behavior.
 * URL grammar/serialization belongs to utl_url; cancellation publication uses OS_Atomic.
 * GPU/container/native HTTP allocation and blocking behavior remain separate defects.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <stdint.h>
#include "proxy.h"
#include "cast_session.h"
#include "gpu_transcode.h"
#include "media_server.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_atomic.h"
#include "os/os_memory.h"
#include "os/os_thread.h"
#include "os/os_temporaryfile.h"
#include "crt/crt_memory.h"
#include "utl/utl_unicode.h"
#include "utl/utl_decimal.h"
#include "utl/utl_url.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define PROXY_REPORT_CAPACITY (1800)
#define PROXY_ERROR_CAPACITY  (256)
#define PROXY_URL_CAPACITY    (64) ///< More than the maximum IPv4/port/resource URL plus termination.
#define PROXY_PORT_CAPACITY   (5)  ///< Maximum uint16_t decimal width.
#define PROXY_SCHEME          ("http")
#define PROXY_RESOURCE        ("/proxy.mp4")
#define PROXY_CONTENT_TYPE    ("video/mp4")

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief One retained context; the callback owns non-atomic fields until completion acquisition.
 */
typedef struct Proxy_State_s
{
    MediaServer_Workspace_t* serving;                                  ///< Borrowed startup pool for this job.
    OS_Atomic32_t            cancelled;                                ///< Independent per-job cancellation latch.
    MediaServer*             server;                                   ///< Retained serving owner after callback completion.
    CastSession_t*           session;                                  ///< Borrowed until successful job stop.
    GpuTranscodeConfig       config;                                   ///< Copied transcode settings.
    uint16_t                 source[OS_TEMPORARYFILE_PATH_CAPACITY];   ///< Copied, validated native-order UTF-16 source.
    const uint16_t*          output;                                   ///< Stable startup-owned scratch path.
    char_t                   localAddress[UTL_URL_IPV4_MAX_BYTES + 1]; ///< Canonical address and terminator.
    uint32_t                 addressBytes;                             ///< Exact address syntax bytes.
    uint16_t                 port;                                     ///< Requested listener port.
    char_t                   report[PROXY_REPORT_CAPACITY];            ///< Retained conversion diagnostics.
    char_t                   error[PROXY_ERROR_CAPACITY];              ///< Retained serving diagnostics.
    char_t                   url[PROXY_URL_CAPACITY];                  ///< Complete URL published to the session.
} Proxy_State_t;

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
 * \brief Constructs empty private state before the worker is made available.
 *
 * \par Requirements:
 *  - 001 Required state shall assert; construction shall occur only under exclusive startup ownership.
 *  - 002 Native storage is already resident; no allocation or file operation shall occur here.
 *
 * \param[out] state   Required fresh private storage
 *
 * \retval     None   Private lifetime fields and cancellation are initialized.
 */
static FORCE_INLINE void InitializeState(Proxy_State_t* state);

/*************************************************************************************************/
/**
 * \brief Validates and copies one request under exclusive owner control.
 *
 * \par Requirements:
 *  - 001 No callback or previous serving owner shall remain active.
 *  - 002 All input validation shall precede writes to the retained job fields.
 *  - 003 UTF-16 validity and complete IPv4 grammar shall be checked within bounded source extents.
 *
 * \param[in,out] state     Exclusive retained job storage
 * \param[in]     request   Caller-provided inputs, disjoint from state
 *
 * \retval        TRUE    The complete request was copied.
 * \retval        FALSE   Invalid input; job fields are unchanged.
 */
static FORCE_INLINE bool_t PrepareJob(Proxy_State_t* state, const Proxy_Request_t* request);

/*************************************************************************************************/
/**
 * \brief Serializes the complete served proxy URL through the specification writer.
 *
 * \par Requirements:
 *  - 001 The listener shall have selected its final port and address syntax shall be validated.
 *  - 002 Output publication shall require complete capacity and syntax validation.
 *
 * \param[in,out] state   Callback-owned complete serving state
 *
 * \retval        TRUE    A terminated URL is available.
 * \retval        FALSE   Serialization failed; no URL may be published.
 */
static FORCE_INLINE bool_t WriteUrl(Proxy_State_t* state);

/*************************************************************************************************/
/**
 * \brief Builds and publishes one complete proxy on the persistent worker.
 *
 * \par Requirements:
 *  - 001 The accepted context, scratch file and prepared session shall remain alive until callback completion.
 *  - 002 Cancellation shall be observed atomically before conversion, serving and publication.
 *  - 003 Failed conversion or serving shall preserve original-stream selection.
 *
 * \param[in,out] context   Stable Proxy_t owner fixed at worker initialization
 *
 * \retval        None   Conversion/publication ended; owner must acquire completion before context reuse.
 */
static FORCE_INLINE void RunJob(void* context);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Only lifetime fields need initial values; successful preparation writes every
 *  byte later consumed by conversion or publication.
 */
static FORCE_INLINE void InitializeState(Proxy_State_t* state)
{
    LOGASSERT(NULL != state);
    OS_Atomic_Initialize32(&state->cancelled, FALSE);
    state->serving      = NULL;
    state->server       = NULL;
    state->session      = NULL;
    state->output       = NULL;
    state->source[0]    = UTL_UNICODE_TERMINATOR;
    state->addressBytes = 0;
    state->port         = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The maximum source scan is the retained path capacity. Address scanning is
 *  independently bounded by the canonical IPv4 grammar and its terminator.
 */
static FORCE_INLINE bool_t PrepareJob(Proxy_State_t* state, const Proxy_Request_t* request)
{
    uint32_t       source_bound;
    uint32_t       source_units;
    bool_t         has_source;
    bool_t         source_fits;
    bool_t         source_valid;
    uint32_t       address_bound;
    uint32_t       address_bytes;
    bool_t         address_valid;
    bool_t         valid;
    Utl_Url_Text_t address_text;
    uint32_t       address;

    source_bound = request->sourceCapacity;
    if (OS_TEMPORARYFILE_PATH_CAPACITY < source_bound)
    {
        source_bound = OS_TEMPORARYFILE_PATH_CAPACITY;
    }
    source_units = Utl_Unicode_GetUtf16StringUnitCount(request->source, source_bound);
    has_source   = (0 < source_units);
    source_fits  = (source_units < OS_TEMPORARYFILE_PATH_CAPACITY);
    source_valid = FALSE;
    if (has_source && source_fits)
    {
        source_valid = Utl_Unicode_IsValidUtf16(request->source, source_units);
    }
    address_bound = request->localAddressCapacity;
    if ((UTL_URL_IPV4_MAX_BYTES + 1) < address_bound)
    {
        address_bound = UTL_URL_IPV4_MAX_BYTES + 1;
    }
    address_bytes = Utl_Unicode_GetUtf8StringByteCount(request->localAddress, address_bound);
    address_valid = (UTL_URL_IPV4_MAX_BYTES >= address_bytes);
    valid         = source_valid && address_valid;
    if (valid)
    {
        address_text.data      = request->localAddress;
        address_text.byteCount = address_bytes;
        valid                  = Utl_Url_ReadIpv4(address_text, &address);
        if (valid)
        {
            CRT_Memory_Copy(state->source, request->source, ((uint64_t)source_units + 1) * sizeof(uint16_t));
            state->addressBytes = Utl_Url_WriteIpv4(address, state->localAddress, UTL_URL_IPV4_MAX_BYTES);
            LOGASSERT(address_bytes == state->addressBytes);
            state->localAddress[state->addressBytes] = UTL_URL_CHAR_NUL;
            if (NULL != request->config)
            {
                state->config = (*request->config);
            }
            else
            {
                GpuTranscode_InitializeConfig(&state->config);
            }
            state->serving   = request->serving;
            state->session   = request->session;
            state->port      = request->port;
            state->report[0] = UTL_URL_CHAR_NUL;
            state->error[0]  = UTL_URL_CHAR_NUL;
            state->url[0]    = UTL_URL_CHAR_NUL;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every component is typed and counted; the URL writer supplies all delimiters.
 *  The bounded decimal port buffer has capacity for every native listener port.
 */
static FORCE_INLINE bool_t WriteUrl(Proxy_State_t* state)
{
    Utl_Url_t url;
    uint16_t  selected_port;
    uint32_t  port_bytes;
    char_t    port[PROXY_PORT_CAPACITY];
    bool_t    written;
    uint32_t  url_bytes;

    Utl_Url_Initialize(&url);
    selected_port = MediaServer_GetPort(state->server);
    port_bytes    = Utl_Decimal_WriteUint64(selected_port, port, PROXY_PORT_CAPACITY);
    LOGASSERT(0 < port_bytes);
    url.scheme.data      = PROXY_SCHEME;
    url.scheme.byteCount = sizeof(PROXY_SCHEME) - 1;
    url.host.data        = state->localAddress;
    url.host.byteCount   = state->addressBytes;
    url.port.data        = port;
    url.port.byteCount   = port_bytes;
    url.path.data        = PROXY_RESOURCE;
    url.path.byteCount   = sizeof(PROXY_RESOURCE) - 1;
    url.hostKind         = UTL_URL_HOST_IPV4;
    url.hasScheme        = TRUE;
    url.hasAuthority     = TRUE;
    url.hasPort          = TRUE;
    written              = Utl_Url_Write(&url, state->url, PROXY_URL_CAPACITY - 1, &url_bytes);
    if (written)
    {
        LOGASSERT(PROXY_URL_CAPACITY > url_bytes);
        state->url[url_bytes] = UTL_URL_CHAR_NUL;
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Complete conversion precedes serving so fixed Content-Length VOD behavior is
 *  preserved. A failed stage leaves the original stream selected.
 */
static FORCE_INLINE void RunJob(void* context)
{
    Proxy_t*       proxy;
    Proxy_State_t* state;
    bool_t         cancelled;
    bool_t         converted;
    bool_t         can_serve;
    bool_t         serving;
    bool_t         can_publish;
    bool_t         has_url;

    proxy = (Proxy_t*)context;
    state = proxy->state;
    LOGASSERT(NULL != state);
    cancelled = !!OS_Atomic_Load32(&state->cancelled);
    converted = FALSE;
    if (!cancelled)
    {
        converted = !!GpuTranscode_WriteFragmentedMp4(state->source,
                                                      state->output,
                                                      &state->config,
                                                      0,
                                                      NULL,
                                                      NULL,
                                                      &state->cancelled,
                                                      state->report,
                                                      PROXY_REPORT_CAPACITY);
    }
    cancelled = !!OS_Atomic_Load32(&state->cancelled);
    can_serve = converted && (!cancelled);
    if (can_serve)
    {
        state->server = MediaServer_Start(state->serving,
                                          state->output,
                                          state->localAddress,
                                          state->port,
                                          PROXY_CONTENT_TYPE,
                                          state->error,
                                          PROXY_ERROR_CAPACITY);
    }
    serving     = (NULL != state->server);
    cancelled   = !!OS_Atomic_Load32(&state->cancelled);
    can_publish = serving && (!cancelled);
    if (can_publish)
    {
        has_url = WriteUrl(state);
        if (has_url)
        {
            CastSession_SetProxy(state->session, state->url, PROXY_CONTENT_TYPE);
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Each subowner receives initialization even after a sibling failure, leaving
 *  one consistent closure path. No callback is submitted during initialization.
 */
bool_t Proxy_Initialize(Proxy_t* proxy)
{
    bool_t file_ready;
    bool_t memory_ready;
    bool_t worker_ready;

    LOGASSERT(NULL != proxy);
    proxy->state  = NULL;
    proxy->ready  = FALSE;
    proxy->busy   = FALSE;
    proxy->active = FALSE;
    file_ready    = OS_TemporaryFile_Initialize(&proxy->output);
    memory_ready  = OS_Memory_Initialize(&proxy->memory, sizeof(Proxy_State_t));
    if (memory_ready)
    {
        proxy->state = (Proxy_State_t*)OS_Memory_GetAddress(&proxy->memory);
        InitializeState(proxy->state);
        proxy->state->output = OS_TemporaryFile_GetPath(&proxy->output);
    }
    worker_ready = OS_Thread_Initialize(&proxy->worker, RunJob, proxy);
    proxy->ready = file_ready && memory_ready && worker_ready;
    return proxy->ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Callback-owned fields are never inspected; worker state is acquired atomically.
 */
bool_t Proxy_IsReady(const Proxy_t* proxy)
{
    OS_Thread_State_t state;
    bool_t            ready;

    LOGASSERT(NULL != proxy);
    state = OS_Thread_GetState(&proxy->worker);
    ready = proxy->ready && (!proxy->active) && (OS_THREAD_READY == state);
    return ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Native errors remain available after closure; conversion diagnostics have a
 *  different lifetime and are not conflated with owner-resource failures.
 */
uint32_t Proxy_GetError(const Proxy_t* proxy)
{
    uint32_t error;

    LOGASSERT(NULL != proxy);
    error = OS_Thread_GetError(&proxy->worker);
    if (0 == error)
    {
        error = OS_Memory_GetError(&proxy->memory);
    }
    if (0 == error)
    {
        error = OS_TemporaryFile_GetError(&proxy->output);
    }
    return error;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The submitted callback owns only persistent storage. A failed submission
 *  leaves the context with the owner and releases its unaccepted session borrow.
 */
bool_t Proxy_Start(Proxy_t* proxy, const Proxy_Request_t* request)
{
    bool_t ready;
    bool_t accepted;
    bool_t prepared;
    bool_t valid;

    LOGASSERT(NULL != proxy);
    LOGASSERT(NULL != request);
    LOGASSERT(NULL != request->serving);
    LOGASSERT(NULL != request->source);
    LOGASSERT(NULL != request->localAddress);
    LOGASSERT(NULL != request->session);
    ready    = Proxy_IsReady(proxy) && MediaServer_IsReady(request->serving);
    accepted = FALSE;
    if (ready)
    {
        LOGASSERT(NULL != proxy->state);
        LOGASSERT(NULL == proxy->state->server);
        prepared = CastSession_IsPrepared(request->session);
        LOGASSERT(prepared);
        valid = PrepareJob(proxy->state, request);
        if (valid)
        {
            OS_Atomic_Store32(&proxy->state->cancelled, FALSE);
            accepted      = OS_Thread_Submit(&proxy->worker);
            proxy->busy   = accepted;
            proxy->active = accepted;
            if (!accepted)
            {
                proxy->state->serving = NULL;
                proxy->state->session = NULL;
            }
        }
    }
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Per-job cancellation does not stop the persistent execution lifetime.
 *  Acquired completion or FAILED grants exclusive access to callback fields.
 */
bool_t Proxy_TryStop(Proxy_t* proxy)
{
    bool_t completed;
    bool_t failed;
    bool_t stopped;

    LOGASSERT(NULL != proxy);
    if (NULL != proxy->state)
    {
        OS_Atomic_Store32(&proxy->state->cancelled, TRUE);
    }
    if (proxy->busy)
    {
        completed = OS_Thread_TakeResult(&proxy->worker);
        failed    = (OS_THREAD_FAILED == OS_Thread_GetState(&proxy->worker));
        if (completed || failed)
        {
            proxy->busy = FALSE;
        }
    }
    stopped = !proxy->busy;
    if (stopped && (NULL != proxy->state))
    {
        stopped = MediaServer_TryStop(&proxy->state->server);
        if (stopped)
        {
            proxy->state->serving   = NULL;
            proxy->state->session   = NULL;
            proxy->state->source[0] = UTL_UNICODE_TERMINATOR;
            proxy->active           = FALSE;
        }
    }
    return stopped;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Final worker exit precedes scratch removal. Private-state borrowing ends
 *  before memory release is attempted, including a failed-release retry.
 */
bool_t Proxy_TryClose(Proxy_t* proxy)
{
    bool_t stopped;
    bool_t closed;
    bool_t worker_closed;
    bool_t file_closed;

    LOGASSERT(NULL != proxy);
    proxy->ready = FALSE;
    stopped      = Proxy_TryStop(proxy);
    closed       = FALSE;
    if (stopped)
    {
        worker_closed = OS_Thread_TryClose(&proxy->worker);
        if (worker_closed)
        {
            file_closed = OS_TemporaryFile_TryClose(&proxy->output);
            if (file_closed)
            {
                proxy->state = NULL;
                closed       = OS_Memory_TryClose(&proxy->memory);
            }
        }
    }
    return closed;
}
