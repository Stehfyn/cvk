/*=================================================================================================
** 1. References
**===============================================================================================*/

/* cast_demo.c - CastDemoCli: play a video on a Google Cast receiver such as the XGIMI Horizon S.
**
** The projector is a Chromecast built-in device, so "connecting" means the CASTV2 protocol:
** find the receiver with SSDP, open a TLS channel to port 8009, launch the default media
** receiver application and send it a LOAD with a URL. The receiver fetches and decodes the
** video itself, so the file is served from this machine by a small HTTP range server.
**
**   CastDemoCli [file.mp4] [options]     default: demo.mp4 beside the executable
**     --device NAME    receiver whose friendly name contains NAME (default "XGIMI")
**     --host ADDRESS   skip discovery and connect to this receiver
**     --url URL        cast this URL instead of serving a local file
**     --port N         HTTP port for the local file (default 8642)
**     --bind ADDRESS   local IPv4 address to serve from (default: the one facing the receiver)
**     --seconds N      stop after N seconds of playback
**     --loop           restart the file when it finishes
**     --list           discover receivers and exit
**     --verbose        print every Cast message sent and received
**
** Keys while playing: Space pauses/resumes, Q or Escape stops and exits. */
/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cast_channel.h"
#include "cast_session.h"
#include "cast_events.h"
#include "os/os_assert.h"
#include "os/os_console.h"
#include "os/os_thread.h"
#include "os/os_compiler.h"
#include "media_server.h"
#include "cast_network.h"
#include "os/os_network.h"
#include "mp4_index.h"
#include "gpu_device.h"
#include "gpu_decode.h"
#include "gpu_encode.h"
#include "gpu_transcode.h"
#include "gpu_probe.h"
#include "h264_params.h"
#include "mp4_demux.h"
#include "proxy.h"
#include "ssdp.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define DEFAULT_DEVICE_NAME  "XGIMI"
#define DEFAULT_SERVER_PORT  (8642)
#define DISCOVERY_MS         (3000)
#define PROGRESS_MS          (5000)
#define SESSION_POLL_MS      (50)
#define CONSOLE_RECORD_LIMIT (32) ///< Maximum native input records consumed by one playback poll.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

typedef struct Options
{
    WCHAR file[MAX_PATH];
    char  device[128];
    char  host[256];
    char  url[1024];
    char  bind[64];
    unsigned short port;
    unsigned seconds;
    double   seek;   /* > 0: seek here three seconds into playback, to time the round trip */
    double   scrub;  /* > 0: preview this position (paused, keyframe-snapped) at three seconds, release at five */
    unsigned bounce; /* > 0: after playback starts, jump to this many varied positions, timing each */
    int      loop;
    int      list;
    int      verbose;
} Options;

/*************************************************************************************************/
/**
 * \brief Startup-owned console state and the resources borrowed by its session worker.
 *
 * \par Details:
 *  The entry point retains this object until every worker and resource is closed.
 *  Status crosses the worker boundary through the mailbox. Playback timing belongs
 *  exclusively to the console thread; progress text belongs to the session worker.
 */
typedef struct CastConsole_s
{
    uint64_t                playingSince;  ///< Console-owned first PLAYING time in milliseconds.
    char_t                  lastState[32]; ///< Worker-owned last printed state.
    uint64_t                lastProgress;  ///< Worker-owned last progress-print time.
    int32_t                 runResult;     ///< Worker result, read only after acquiring completion.
    float64_t               duration;      ///< Console-owned duration acquired from the mailbox.
    CastSession_t           session;       ///< Session storage retained through worker closure.
    OS_Thread_t             sessionWorker; ///< Persistent worker borrowing this owner.
    Ssdp_t                  discovery;     ///< Discovery resource established before interaction.
    OS_Network_t            routeNetwork;  ///< Route-query resource established at startup.
    Proxy_t                 proxy;         ///< Proxy lifetime and persistent execution state.
    MediaServer_Workspace_t serving;       ///< HTTP listener and worker pool.
    GpuProbe_t              gpuProbe;      ///< Startup diagnostic storage.
    CastEvents_t            events;        ///< Worker-to-console status publication.
    CastEvents_Snapshot_t   snapshot;      ///< Consumer-owned acquired status.
    OS_Console_t            console;       ///< Startup-owned keyboard input and mode restoration.
} CastConsole_t;

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
 * \brief Releases startup-owned proxy and route resources before Winsock shutdown.
 *
 * \par Requirements:
 *  - 001 Both initialized owners shall remain live and Winsock shall remain available until closure succeeds.
 *  - 002 Failed releases shall retain ownership and be retried; the console polling wait remains an open timing defect.
 *
 * \retval None   Proxy and route resources are released; failed Winsock cleanup sets the process result.
 */
static FORCE_INLINE void CloseNetworkResources(CastConsole_t* app);

/*************************************************************************************************/
/**
 * \brief Closes startup-owned session resources after work and publishers are quiescent.
 *
 * \par Requirements:
 *  - 001 Both lifetime objects shall have been initialized, including after failed startup.
 *  - 002 The caller shall stop proxy publication and acquire worker completion before entry.
 *  - 003 Failed native releases shall retain owner storage and be retried on later shutdown polls.
 *
 * \retval None   Native session resources are closed; any recorded failure changes the exit result.
 */
static FORCE_INLINE void CloseSessionResources(CastConsole_t* app);

/*************************************************************************************************/
/**
 * \brief Polls proxy and serving shutdown before releasing their dependencies.
 *
 * \par Requirements:
 *  - 001 The caller shall have requested session Stop; both owners shall remain live.
 *  - 002 Winsock and the proxy's session shall remain live until both shutdown operations succeed.
 *  - 003 Failed releases shall preserve ownership; console polling latency remains an open timing defect.
 *
 * \param[in,out] proxy    Required initialized proxy owner
 * \param[in,out] server   Required pointer to a possibly NULL server owner
 *
 * \retval None   No remaining proxy publisher or serving worker can use the dependencies.
 */
static FORCE_INLINE void CloseServingResources(Proxy_t* proxy, MediaServer** server);

/*************************************************************************************************/
/**
 * \brief Determines whether a normalized key names a playback stop gesture.
 *
 * \param[in] key   Required complete input record
 *
 * \retval    TRUE    Q, Escape, Cancel or Control+C was pressed.
 * \retval    FALSE   The key does not name a stop gesture.
 */
static FORCE_INLINE bool_t IsStopKey(const OS_Console_Key_t* key);

/*************************************************************************************************/
/**
 * \brief Applies one keyboard record to the playback command policy.
 *
 * \par Requirements:
 *  - 001 Key releases and zero-repeat input shall not change playback.
 *  - 002 One Space key-down record shall enqueue one toggle against reported playback state.
 *  - 003 Stop gestures shall be returned to the owner for one latched session stop.
 *
 * \param[in,out] session   Initialized active session
 * \param[in]     key       Complete normalized input record
 *
 * \retval        TRUE    Stop was requested.
 * \retval        FALSE   No stop gesture occurred.
 */
static FORCE_INLINE bool_t HandleConsoleKey(CastSession_t* session, const OS_Console_Key_t* key);

/*************************************************************************************************/
/**
 * \brief Consumes a bounded batch of keyboard input without waiting for a record.
 *
 * \par Requirements:
 *  - 001 The owner shall retain the console and session for every input attempt.
 *  - 002 One poll shall consume at most CONSOLE_RECORD_LIMIT native records.
 *  - 003 Native input failure shall request stop and remain recorded in the console.
 *  - 004 This function shall not write the session worker's runResult field.
 *
 * \param[in,out] app   Required active console owner
 *
 * \retval        TRUE    Stop was requested, including after input failure.
 * \retval        FALSE   No stop request occurred in this batch.
 */
static FORCE_INLINE bool_t PollConsoleInput(CastConsole_t* app);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/


/*************************************************************************************************/
/**
 * \par Details:
 *  Converts a terminated Windows string into the caller's UTF-8 buffer. Conversion
 *  failure produces an empty string.
 */
static void ToUtf8(const wchar_t* pText, char* pOut, size_t capacity)
{
    if (0 == WideCharToMultiByte(CP_UTF8, 0, pText, -1, pOut, (int)capacity, NULL, NULL))
    {
        pOut[0] = 0;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Tests each possible start position with a case-insensitive prefix comparison.
 *  An empty search string matches.
 */
static int ContainsNoCase(const char* pHaystack, const char* pNeedle)
{
    const size_t needleLength = strlen(pNeedle);
    size_t index;

    for (index = 0; pHaystack[index]; ++index)
    {
        if (0 == _strnicmp(pHaystack + index, pNeedle, needleLength))
        {
            return 1;
        }
    }
    return 0 == needleLength;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Writes the supported command-line options and default receiver name to stderr.
 */
static void Usage(void)
{
    (void)fprintf(stderr,
     "CastDemoCli [file.mp4] [--device NAME] [--host ADDRESS] [--url URL] [--port N] [--bind ADDRESS]\n"
                  "            [--seconds N] [--seek T] [--scrub T] [--bounce N] [--loop] [--list] [--verbose]\n"
                  "Plays a video on a Google Cast receiver (default device name: %s).\n",
                                                            DEFAULT_DEVICE_NAME);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Applies defaults, scans command-line switches and converts their values.
 *  When no file is supplied, uses demo.mp4 beside the executable.
 */
static int ParseOptions(int argc, wchar_t** argv, Options* pOptions)
{
    int index;

    ZeroMemory(pOptions, sizeof(*pOptions));
    (void)snprintf(pOptions->device, sizeof(pOptions->device), "%s", DEFAULT_DEVICE_NAME);
    pOptions->port = DEFAULT_SERVER_PORT;
    for (index = 1; index < argc; ++index)
    {
        const wchar_t* pArgument = argv[index];
        const int      hasValue  = index + 1 < argc;

        if (0 == wcscmp(pArgument, L"--device") && hasValue)
        {
            ToUtf8(argv[++index], pOptions->device, sizeof(pOptions->device));
        }
        else if (0 == wcscmp(pArgument, L"--host") && hasValue)
        {
            ToUtf8(argv[++index], pOptions->host, sizeof(pOptions->host));
        }
        else if (0 == wcscmp(pArgument, L"--url") && hasValue)
        {
            ToUtf8(argv[++index], pOptions->url, sizeof(pOptions->url));
        }
        else if (0 == wcscmp(pArgument, L"--bind") && hasValue)
        {
            ToUtf8(argv[++index], pOptions->bind, sizeof(pOptions->bind));
        }
        else if (0 == wcscmp(pArgument, L"--port") && hasValue)
        {
            pOptions->port = (unsigned short)wcstoul(argv[++index], NULL, 10);
        }
        else if (0 == wcscmp(pArgument, L"--seconds") && hasValue)
        {
            pOptions->seconds = (unsigned)wcstoul(argv[++index], NULL, 10);
        }
        else if (0 == wcscmp(pArgument, L"--seek") && hasValue)
        {
            pOptions->seek = wcstod(argv[++index], NULL);
        }
        else if (0 == wcscmp(pArgument, L"--scrub") && hasValue)
        {
            pOptions->scrub = wcstod(argv[++index], NULL);
        }
        else if (0 == wcscmp(pArgument, L"--bounce") && hasValue)
        {
            pOptions->bounce = (unsigned)wcstoul(argv[++index], NULL, 10);
        }
        else if (0 == wcscmp(pArgument, L"--loop"))
        {
            pOptions->loop = 1;
        }
        else if (0 == wcscmp(pArgument, L"--list"))
        {
            pOptions->list = 1;
        }
        else if (0 == wcscmp(pArgument, L"--verbose"))
        {
            pOptions->verbose = 1;
        }
        else if (L'-' == pArgument[0])
        {
            return 0;
        }
        else
        {
            wcscpy_s(pOptions->file, MAX_PATH, pArgument);
        }
    }
    if (0 == pOptions->file[0])
    {
        /* demo.mp4 beside the executable, as the other example does */
        wchar_t* pSlash;

        GetModuleFileNameW(NULL, pOptions->file, MAX_PATH);
        pSlash = wcsrchr(pOptions->file, L'\\');
        if (NULL != pSlash)
        {
            pSlash[1] = 0;
        }
        else
        {
            pOptions->file[0] = 0;
        }
        wcscat_s(pOptions->file, MAX_PATH, L"demo.mp4");
    }
    return 1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Resolves the first IPv4 result into numeric address text, then releases the
 *  resolver's result list.
 */
static int ResolveIPv4(const char* pHost, char* pOut, size_t capacity)
{
    struct addrinfo hints;
    struct addrinfo* pResult = NULL;
    int              ok      = 0;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (0 == getaddrinfo(pHost, NULL, &hints, &pResult) && NULL != pResult)
    {
        ok = NULL != inet_ntop(AF_INET, &((struct sockaddr_in*)pResult->ai_addr)->sin_addr, pOut, capacity);
        freeaddrinfo(pResult);
    }
    return ok;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  An explicit host bypasses multicast selection. Otherwise lists discovery
 *  results and selects the first case-insensitive receiver-name match.
 */
static int FindDevice(CastConsole_t* app, const Options* pOptions, CastDevice* pDevice)
{
    unsigned   count;
    CastDevice devices[CAST_MAX_DEVICES];
    unsigned   index;
    int        found = 0;

    if (pOptions->host[0])
    {
        ZeroMemory(pDevice, sizeof(*pDevice));
        if (!ResolveIPv4(pOptions->host, pDevice->host, sizeof(pDevice->host)))
        {
            (void)fprintf(stderr, "cannot resolve %s\n", pOptions->host);
            return 0;
        }
        if (!Ssdp_DescribeHost(&app->discovery, pDevice->host, pDevice))
        {
            (void)snprintf(pDevice->friendlyName, sizeof(pDevice->friendlyName), "%s", pOptions->host);
        }
        return 1;
    }
    (void)printf("searching for Cast receivers...\n");
    count = Ssdp_DiscoverCastDevices(&app->discovery, devices, CAST_MAX_DEVICES, DISCOVERY_MS);
    for (index = 0; index < count; ++index)
    {
        const int match = ContainsNoCase(devices[index].friendlyName, pOptions->device);

        (void)printf("  %s%s  %s  (%s)\n",
         match ? "* " : "  ",
         devices[index].friendlyName,
         devices[index].host,
         devices[index].model);
        if (match && !found)
        {
            *pDevice = devices[index];
            found    = 1;
        }
    }
    if (0 == count)
    {
        (void)printf("  none answered. Is the projector on and on the same network?\n");
    }
    else if (!found && !pOptions->list)
    {
        (void)printf("no receiver name contains \"%s\"; pass --device NAME or --host ADDRESS\n", pOptions->device);
    }
    return found;
}

/* Runs on the session thread: print progress, remember when playback started. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Runs on the session thread. Prints log and terminal events, records the first
 *  PLAYING time and throttles repeated progress lines.
 */
static void OnSessionEvent(void* pUser, CastEvent event, const CastStatus* pStatus, const char* pText)
{
    CastConsole_t* app;
    uint64_t       now;

    LOGASSERT(NULL != pUser);

    app = (CastConsole_t*)pUser;
    now = GetTickCount64();
    switch (event)
    {
    case CAST_EVENT_LOG:
        (void)printf("%s\n", pText);
        break;
    case CAST_EVENT_STATUS:
        (void)CastEvents_Publish(&app->events, 0, event, pStatus, NULL);
        if (pStatus->hasMedia && (0 != strcmp(pStatus->stateName, app->lastState) || now - app->lastProgress >= PROGRESS_MS))
        {
            (void)snprintf(app->lastState, sizeof(app->lastState), "%s", pStatus->stateName);
            app->lastProgress = now;
            (void)printf("%s  %.1f / %.1f s\n", pStatus->stateName, pStatus->currentTime, pStatus->duration);
        }
        break;
    case CAST_EVENT_ENDED:
        (void)printf("%s\n", pText ? pText : "session ended");
        break;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Stores the session runner's result for the console owner to read after the
 *  thread exits.
 */
static FORCE_INLINE void SessionJob(void* context)
{
    CastConsole_t* app;

    LOGASSERT(NULL != context);

    app            = (CastConsole_t*)context;
    app->runResult = CastSession_Run(&app->session);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Shutdown preserves the owner through native release retries.
 */
static FORCE_INLINE void CloseNetworkResources(CastConsole_t* app)
{
    bool_t  proxy_closed;
    bool_t  route_closed;
    bool_t  serving_closed;
    bool_t  closed;
    int32_t status;
    for (;;)
    {
        proxy_closed   = Proxy_TryClose(&app->proxy);
        route_closed   = OS_Network_TryClose(&app->routeNetwork);
        serving_closed = FALSE;
        if (proxy_closed)
        {
            serving_closed = MediaServer_TryClose(&app->serving);
        }
        closed = proxy_closed && route_closed && serving_closed;
        if (closed)
        {
            break;
        }
        Sleep(SESSION_POLL_MS);
    }
    status = WSACleanup();
    if (0 != status)
    {
        app->runResult = 1;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each closure attempt is nonblocking. The console shutdown loop retains both
 *  owners until native release succeeds; no callback can outlive its storage.
 */
static FORCE_INLINE void CloseSessionResources(CastConsole_t* app)
{
    bool_t   worker_closed;
    bool_t   storage_closed;
    uint32_t worker_error;
    uint32_t storage_error;
    bool_t   has_error;

    for (;;)
    {
        worker_closed  = OS_Thread_TryClose(&app->sessionWorker);
        storage_closed = FALSE;
        if (worker_closed)
        {
            CastSession_Reset(&app->session);
            storage_closed = CastSession_TryClose(&app->session);
        }
        if (storage_closed)
        {
            break;
        }
        Sleep(SESSION_POLL_MS);
    }
    worker_error  = OS_Thread_GetError(&app->sessionWorker);
    storage_error = OS_Memory_GetError(&app->session.memory);
    has_error     = (0 != worker_error) || (0 != storage_error);
    if (has_error)
    {
        app->runResult = 1;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Polls both owners on each iteration so either shutdown can make progress.
 *  No timeout is interpreted as successful release.
 */
static FORCE_INLINE void CloseServingResources(Proxy_t* proxy, MediaServer** server)
{
    bool_t proxy_stopped;
    bool_t server_stopped;
    bool_t stopped;

    for (;;)
    {
        proxy_stopped  = Proxy_TryStop(proxy);
        server_stopped = MediaServer_TryStop(server);
        stopped        = proxy_stopped && server_stopped;
        if (stopped)
        {
            break;
        }
        Sleep(SESSION_POLL_MS);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Modifier interpretation is application policy; the OS layer only reports keys.
 */
static FORCE_INLINE bool_t IsStopKey(const OS_Console_Key_t* key)
{
    bool_t is_quit;
    bool_t is_escape;
    bool_t is_cancel;
    bool_t is_c;
    bool_t has_control;
    bool_t is_control_c;
    bool_t is_stop;

    LOGASSERT(NULL != key);

    is_quit      = (OS_CONSOLE_KEY_Q == key->code);
    is_escape    = (OS_CONSOLE_KEY_ESCAPE == key->code);
    is_cancel    = (OS_CONSOLE_KEY_CANCEL == key->code);
    is_c         = (OS_CONSOLE_KEY_C == key->code);
    has_control  = !!(key->modifiers & OS_CONSOLE_MODIFIER_CONTROL);
    is_control_c = is_c && has_control;
    is_stop      = is_quit || is_escape || is_cancel || is_control_c;
    return is_stop;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Native repeat counts coalesce to one request per record. Toggle is resolved
 *  against asynchronously reported receiver state, so parity cannot cancel it.
 */
static FORCE_INLINE bool_t HandleConsoleKey(CastSession_t* session, const OS_Console_Key_t* key)
{
    bool_t has_repeats;
    bool_t is_press;
    bool_t stop;
    bool_t is_space;

    LOGASSERT(NULL != session);
    LOGASSERT(NULL != key);

    has_repeats = (0 < key->repeatCount);
    is_press    = key->pressed && has_repeats;
    stop        = FALSE;
    if (is_press)
    {
        stop     = IsStopKey(key);
        is_space = (OS_CONSOLE_KEY_SPACE == key->code);
        if (is_space)
        {
            CastSession_Post(session, CAST_COMMAND_TOGGLE, 0);
        }
    }
    return stop;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Ignored native records count against the same limit as keyboard records.
 *  Stop ends consumption immediately. The retained OS error is checked after
 *  the worker is quiescent, preserving sole worker ownership of runResult.
 */
static FORCE_INLINE bool_t PollConsoleInput(CastConsole_t* app)
{
    bool_t                  stop;
    uint32_t                index;
    OS_Console_ReadResult_t result;
    OS_Console_Key_t        key;
    bool_t                  is_empty;
    bool_t                  failed;
    bool_t                  is_key;

    LOGASSERT(NULL != app);

    stop = FALSE;
    for (index = 0; index < CONSOLE_RECORD_LIMIT; ++index)
    {
        result   = OS_Console_ReadKey(&app->console, &key);
        is_empty = (OS_CONSOLE_READ_EMPTY == result);
        failed   = (OS_CONSOLE_READ_FAILED == result);
        is_key   = (OS_CONSOLE_READ_KEY == result);
        if (is_empty)
        {
            break;
        }
        if (failed)
        {
            stop = TRUE;
        }
        else if (is_key)
        {
            stop = HandleConsoleKey(&app->session, &key);
        }
        if (stop)
        {
            break;
        }
    }
    return stop;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Parses options, starts local serving when required and runs the Cast session
 *  on a worker thread while processing console and scripted playback commands.
 */
static FORCE_INLINE int RunConsole(CastConsole_t* app, int argc, wchar_t** argv)
{
    Options           options;
    CastDevice        device;
    CastSessionConfig config;
    CastSession_t*    pSession;
    MediaServer*      pServer    = NULL;
    double*           pKeyframes = NULL;
    WSADATA wsaData;
    char    error[256];
    char    localAddress[64];
    int               stopping = 0;
    bool_t storage_ready;
    bool_t worker_ready;
    bool_t session_ready;
    bool_t submitted;
    bool_t serving_ready;
    bool_t route_ready;
    bool_t discovery_ready;
    bool_t network_ready;
    bool_t device_found;
    bool_t discovery_closed;

    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, NULL, _IONBF, 0);
    {
        int argIndex;

        for (argIndex = 1; argIndex < argc; ++argIndex)
        {
            if (0 == wcscmp(argv[argIndex], L"--gpuprobe"))
            {
                bool_t ready;
                bool_t closed;
                bool_t available;

                ready     = GpuProbe_Initialize(&app->gpuProbe);
                available = ready && GpuProbe_GetCaps(&app->gpuProbe)->available;
                (void)printf("%s", GpuProbe_GetReport(&app->gpuProbe));
                if (GpuProbe_IsReportTruncated(&app->gpuProbe))
                {
                    (void)printf("[GPU probe report truncated]\n");
                }
                for (;;)
                {
                    closed = GpuProbe_TryClose(&app->gpuProbe);
                    if (closed)
                    {
                        break;
                    }
                    Sleep(SESSION_POLL_MS);
                }
                return available ? 0 : 1;
            }
            if (0 == wcscmp(argv[argIndex], L"--encodeinit"))
            {
                GpuDevice*  pGpu;
                GpuEncoder* pEncoder;
                char        device_report[256];
                char        report[512];

                pGpu = GpuDevice_Create(0, 1, device_report, sizeof(device_report));
                (void)printf("%s", device_report);
                if (NULL == pGpu)
                {
                    return 1;
                }
                pEncoder = GpuEncode_Create(pGpu, 480, 272, 600, 12, 24, 28, 30, 1, report, sizeof(report));
                (void)printf("%s", report);
                GpuEncode_Destroy(pEncoder);
                GpuDevice_Destroy(pGpu);
                return pEncoder ? 0 : 1;
            }
            if (0 == wcscmp(argv[argIndex], L"--encodeframe"))
            {
                GpuDevice*  pGpu;
                GpuEncoder* pEncoder;
                char        device_report[256];
                char        report[512];
                const unsigned encode_width  = 480;
                const unsigned encode_height = 272;
                unsigned frame_count;
                byte_t* luma;
                byte_t* chroma;
                unsigned frame;
                uint64_t total;

                frame_count = 30;
                if (argIndex + 1 < argc)
                {
                    frame_count = (unsigned)_wtoi(argv[argIndex + 1]);
                }
                pGpu = GpuDevice_Create(0, 1, device_report, sizeof(device_report));
                (void)printf("%s", device_report);
                if (NULL == pGpu)
                {
                    return 1;
                }
                pEncoder =
                    GpuEncode_Create(pGpu, encode_width, encode_height, 600, 12, 24, 28, 30, 1, report, sizeof(report));
                (void)printf("%s", report);
                if (NULL == pEncoder)
                {
                    GpuDevice_Destroy(pGpu);
                    return 1;
                }
                luma   = (byte_t*)malloc((size_t)encode_width * encode_height);
                chroma = (byte_t*)malloc((size_t)encode_width * (encode_height / 2));
                total  = 0;
                for (frame = 0; frame < frame_count; ++frame)
                {
                    const byte_t* coded;
                    uint32_t       coded_size;
                    unsigned       y;

                    for (y = 0; y < encode_height; ++y)
                    {
                        unsigned x;

                        for (x = 0; x < encode_width; ++x)
                        {
                            luma[(size_t)y * encode_width + x] = (uint8_t)(x + y + frame * 4u);
                        }
                    }
                    for (y = 0; y < encode_height / 2u; ++y)
                    {
                        unsigned x;

                        for (x = 0; x < encode_width; ++x)
                        {
                            chroma[(size_t)y * encode_width + x] = (uint8_t)(128 + ((int)frame * 2));
                        }
                    }
                    coded =
                        GpuEncode_EncodeFrame(pEncoder, luma, encode_width, chroma, encode_width, 0, 0, &coded_size);
                    total += coded_size;
                    if (coded && coded_size >= 5)
                    {
                        int start_code;
                        int nal_type;

                        start_code = (coded[0] == 0 && coded[1] == 0 && coded[2] == 0 && coded[3] == 1) ||
                                     (coded[0] == 0 && coded[1] == 0 && coded[2] == 1);
                        nal_type =
                            (coded[0] == 0 && coded[1] == 0 && coded[2] == 1) ? (coded[3] & 0x1F) : (coded[4] & 0x1F);
                        (void)printf("frame %2u: ok %5u bytes, %s, first NAL type %d\n",
                         frame,
                         coded_size,
                         start_code ? "annex-b" : "no start code",
                         nal_type);
                    }
                    else
                    {
                        (void)printf("frame %2u: FAIL %u bytes\n", frame, coded_size);
                    }
                }
                (void)printf("encoded %u frames, %llu bytes total, avg %llu B/frame\n",
                 frame_count,
                 (unsigned long long)total,
                 (unsigned long long)(frame_count ? total / frame_count : 0));
                free(luma);
                free(chroma);
                GpuEncode_Destroy(pEncoder);
                GpuDevice_Destroy(pGpu);
                return 0;
            }
            if (0 == wcscmp(argv[argIndex], L"--transcode") && argIndex + 1 < argc)
            {
                GpuTranscoder*     pTranscoder;
                GpuTranscodeConfig tconfig;
                char               report[1600];
                unsigned           frame_limit;
                unsigned           frame;
                const byte_t*     header;
                uint32_t           header_size;
                FILE*              out;
                uint64_t           total;
                unsigned           keyframes;

                GpuTranscode_InitializeConfig(&tconfig);
                frame_limit = (argIndex + 2 < argc) ? (unsigned)wcstoul(argv[argIndex + 2], NULL, 10) : 60u;
                pTranscoder = GpuTranscode_Open(argv[argIndex + 1], &tconfig, report, sizeof(report));
                (void)printf("%s", report);
                if (NULL == pTranscoder)
                {
                    return 1;
                }
                if (frame_limit > GpuTranscode_GetFrameCount(pTranscoder))
                {
                    frame_limit = GpuTranscode_GetFrameCount(pTranscoder);
                }
                out    = fopen("proxy.264", "wb");
                header = GpuTranscode_GetHeader(pTranscoder, &header_size);
                if (out && header && header_size)
                {
                    (void)fwrite(header, 1, header_size, out);
                }
                total     = 0;
                keyframes = 0;
                for (frame = 0; frame < frame_limit; ++frame)
                {
                    const byte_t* coded;
                    uint32_t       coded_size;
                    int            keyframe;

                    coded = GpuTranscode_ReadNextFrame(pTranscoder, &coded_size, &keyframe);
                    if (NULL == coded)
                    {
                        (void)printf("frame %u: end of stream or failure\n", frame);
                        break;
                    }
                    if (out)
                    {
                        (void)fwrite(coded, 1, coded_size, out);
                    }
                    total     += coded_size;
                    keyframes += (unsigned)(keyframe != 0);
                    if (frame < 8 || (frame % 30) == 0)
                    {
                        (void)printf("frame %3u: %5u bytes%s\n", frame, coded_size, keyframe ? " (keyframe)" : "");
                    }
                }
                if (out)
                {
                    (void)fclose(out);
                }
                (void)printf("transcoded %u frames (%u keyframes), %llu bytes -> proxy.264 (%ux%u)\n",
                 frame,
                 keyframes,
                 (unsigned long long)total,
                 GpuTranscode_GetOutputWidth(pTranscoder),
                 GpuTranscode_GetOutputHeight(pTranscoder));
                GpuTranscode_Close(pTranscoder);
                return 0;
            }
            if (0 == wcscmp(argv[argIndex], L"--fragmp4") && argIndex + 2 < argc)
            {
                char               report[1800];
                GpuTranscodeConfig fragConfig;
                unsigned           max_frames;
                int                ok;

                GpuTranscode_InitializeConfig(&fragConfig);
                max_frames = (argIndex + 3 < argc) ? (unsigned)wcstoul(argv[argIndex + 3], NULL, 10) : 0u;
                ok         = GpuTranscode_WriteFragmentedMp4(argv[argIndex + 1],
                                                             argv[argIndex + 2],
                                                             &fragConfig,
                                                             max_frames,
                                                             NULL,
                                                             NULL,
                                                             NULL,
                                                             report,
                                                             sizeof(report));
                (void)printf("%s", report);
                return ok ? 0 : 1;
            }
            if (0 == wcscmp(argv[argIndex], L"--proxymp4") && argIndex + 2 < argc)
            {
                char               report[1800];
                GpuTranscodeConfig proxyConfig;
                unsigned           max_frames;
                int                ok;

                GpuTranscode_InitializeConfig(&proxyConfig);
                max_frames = (argIndex + 3 < argc) ? (unsigned)wcstoul(argv[argIndex + 3], NULL, 10) : 0u;
                ok         = GpuTranscode_WriteMp4(argv[argIndex + 1],
                                                   argv[argIndex + 2],
                                                   &proxyConfig,
                                                   0,
                                                   max_frames,
                                                   NULL,
                                                   report,
                                                   sizeof(report));
                (void)printf("%s", report);
                return ok ? 0 : 1;
            }
            if (0 == wcscmp(argv[argIndex], L"--decodeinit") && argIndex + 1 < argc)
            {
                Mp4Demux*                        pDemux = Mp4Demux_Open(argv[argIndex + 1]);
                StdVideoH264SequenceParameterSet sps;
                StdVideoH264PictureParameterSet  pps;
                unsigned                         w, h;
                uint32_t                         spsSize, ppsSize;
                char        report[512];
                char        deviceReport[256];
                GpuDevice*  pGpu;
                GpuDecoder* pDecoder;

                if (NULL == pDemux)
                {
                    (void)printf("not an AVC MP4\n");
                    return 1;
                }
                if (!H264_ParseSps(Mp4Demux_GetSps(pDemux, &spsSize), spsSize, &sps, &w, &h) ||
                    !H264_ParsePps(Mp4Demux_GetPps(pDemux, &ppsSize), ppsSize, &pps))
                {
                    (void)printf("SPS/PPS parse failed\n");
                    Mp4Demux_Close(pDemux);
                    return 1;
                }
                pGpu = GpuDevice_Create(1, 0, deviceReport, sizeof(deviceReport));
                (void)printf("%s", deviceReport);
                if (NULL == pGpu)
                {
                    Mp4Demux_Close(pDemux);
                    return 1;
                }
                pDecoder = GpuDecode_Create(pGpu,
                                            &sps,
                                            &pps,
                                            w,
                                            h,
                                            sps.max_num_ref_frames + 2u,
                                            Mp4Demux_GetNalLengthSize(pDemux),
                                            report,
                                            sizeof(report));
                (void)printf("%s", report);
                GpuDecode_Destroy(pDecoder);
                GpuDevice_Destroy(pGpu);
                Mp4Demux_Close(pDemux);
                return pDecoder ? 0 : 1;
            }
            if (0 == wcscmp(argv[argIndex], L"--decode") && argIndex + 1 < argc)
            {
                Mp4Demux*                        pDemux = Mp4Demux_Open(argv[argIndex + 1]);
                StdVideoH264SequenceParameterSet sps;
                StdVideoH264PictureParameterSet  pps;
                unsigned                         w, h;
                uint32_t                         spsSize, ppsSize;
                char        report[512];
                char        deviceReport[256];
                GpuDevice*  pGpu;
                GpuDecoder* pDecoder;
                byte_t*    pSampleBuffer;
                unsigned frameCount = (argIndex + 2 < argc) ? (unsigned)wcstoul(argv[argIndex + 2], NULL, 10) : 8;
                unsigned frame;

                if (NULL == pDemux)
                {
                    (void)printf("not an AVC MP4\n");
                    return 1;
                }
                if (!H264_ParseSps(Mp4Demux_GetSps(pDemux, &spsSize), spsSize, &sps, &w, &h) ||
                    !H264_ParsePps(Mp4Demux_GetPps(pDemux, &ppsSize), ppsSize, &pps))
                {
                    (void)printf("SPS/PPS parse failed\n");
                    Mp4Demux_Close(pDemux);
                    return 1;
                }
                pGpu = GpuDevice_Create(1, 0, deviceReport, sizeof(deviceReport));
                (void)printf("%s", deviceReport);
                if (NULL == pGpu)
                {
                    Mp4Demux_Close(pDemux);
                    return 1;
                }
                pDecoder = GpuDecode_Create(pGpu,
                                            &sps,
                                            &pps,
                                            w,
                                            h,
                                            sps.max_num_ref_frames + 2u,
                                            Mp4Demux_GetNalLengthSize(pDemux),
                                            report,
                                            sizeof(report));
                pSampleBuffer = (byte_t*)malloc(4u * 1024u * 1024u);
                (void)printf("%s", report);
                if (NULL == pDecoder || NULL == pSampleBuffer)
                {
                    (void)printf("decoder or buffer allocation failed\n");
                    GpuDecode_Destroy(pDecoder);
                    GpuDevice_Destroy(pGpu);
                    free(pSampleBuffer);
                    Mp4Demux_Close(pDemux);
                    return 1;
                }
                if (frameCount > Mp4Demux_GetSampleCount(pDemux))
                {
                    frameCount = Mp4Demux_GetSampleCount(pDemux);
                }
                for (frame = 0; frame < frameCount; ++frame)
                {
                    uint32_t bytes = Mp4Demux_ReadSample(pDemux, frame, pSampleBuffer, 4u * 1024u * 1024u);

                    if (0 == bytes || !GpuDecode_DecodeFrame(pDecoder, pSampleBuffer, bytes))
                    {
                        (void)printf("frame %u: decode failed\n", frame);
                        break;
                    }
                    {
                        unsigned       lw, lh, ls, x, y;
                        const byte_t* pLuma   = GpuDecode_GetLuma(pDecoder, &lw, &lh, &ls);
                        uint64_t       sum     = 0;
                        uint64_t       nonzero = 0;

                        for (y = 0; y < lh; ++y)
                        {
                            for (x = 0; x < lw; ++x)
                            {
                                const uint8_t v = pLuma[(size_t)y * ls + x];

                                sum     += v;
                                nonzero += (v != 0);
                            }
                        }
                        (void)printf("frame %u: %ux%u luma mean %.1f, %.0f%% nonzero\n",
                         frame,
                         lw,
                         lh,
                         (double)sum / ((double)lw * lh),
                         100.0 * (double)nonzero / ((double)lw * lh));
                    }
                }
                free(pSampleBuffer);
                GpuDecode_Destroy(pDecoder);
                GpuDevice_Destroy(pGpu);
                Mp4Demux_Close(pDemux);
                return 0;
            }
            if (0 == wcscmp(argv[argIndex], L"--demux") && argIndex + 1 < argc)
            {
                Mp4Demux* pDemux = Mp4Demux_Open(argv[argIndex + 1]);
                uint32_t  spsSize, ppsSize;
                unsigned  keyframes = 0;
                unsigned i;

                if (NULL == pDemux)
                {
                    (void)printf("not an AVC MP4, or unreadable\n");
                    return 1;
                }
                (void)Mp4Demux_GetSps(pDemux, &spsSize);
                (void)Mp4Demux_GetPps(pDemux, &ppsSize);
                for (i = 0; i < Mp4Demux_GetSampleCount(pDemux); ++i)
                {
                    keyframes += Mp4Demux_GetSample(pDemux, i)->keyframe;
                }
                (void)printf("video %ux%u, timescale %u, NAL length %d, SPS %u B, PPS %u B\n",
                 Mp4Demux_GetWidth(pDemux),
                 Mp4Demux_GetHeight(pDemux),
                 Mp4Demux_GetTimescale(pDemux),
                 Mp4Demux_GetNalLengthSize(pDemux),
                 spsSize,
                 ppsSize);
                {
                    StdVideoH264SequenceParameterSet sps;
                    StdVideoH264PictureParameterSet  pps;
                    unsigned                         w, h;

                    if (H264_ParseSps(Mp4Demux_GetSps(pDemux, &spsSize), spsSize, &sps, &w, &h) &&
                        H264_ParsePps(Mp4Demux_GetPps(pDemux, &ppsSize), ppsSize, &pps))
                    {
                        (void)printf("SPS: profile %d level %d, %ux%u, %u ref frames, poc type %d; PPS: id %u, cabac "
                                     "%u, init_qp %d\n",
                                     (int)sps.profile_idc,
                                      (int)sps.level_idc,
                                      w,
                                      h,
                                      sps.max_num_ref_frames,
                                      (int)sps.pic_order_cnt_type,
                                      pps.pic_parameter_set_id,
                                      pps.flags.entropy_coding_mode_flag,
                                      26 + pps.pic_init_qp_minus26);
                    }
                    else
                    {
                        (void)printf("SPS/PPS parse failed\n");
                    }
                }
                (void)printf("%u samples, %u keyframes; audio codec %d, %u Hz, %u ch\n",
                 Mp4Demux_GetSampleCount(pDemux),
                 keyframes,
                 (int)Mp4Demux_GetAudioCodec(pDemux),
                 Mp4Demux_GetAudioSampleRate(pDemux),
                 Mp4Demux_GetAudioChannels(pDemux));
                for (i = 0; i < 3 && i < Mp4Demux_GetSampleCount(pDemux); ++i)
                {
                    const Mp4Sample* s = Mp4Demux_GetSample(pDemux, i);

                    (void)printf("  sample %u: offset %llu size %u dts %lld key %d\n",
                     i,
                     (unsigned long long)s->offset,
                     s->size,
                     (long long)s->decodeTime,
                     s->keyframe);
                }
                Mp4Demux_Close(pDemux);
                return 0;
            }
        }
    }
    if (!ParseOptions(argc, argv, &options))
    {
        Usage();
        return 3;
    }
    if (0 != WSAStartup(MAKEWORD(2, 2), &wsaData))
    {
        (void)fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
    /* Failed optional proxy startup leaves original-stream casting available. */
    (void)Proxy_Initialize(&app->proxy);
    serving_ready   = MediaServer_Initialize(&app->serving);
    route_ready     = OS_Network_Initialize(&app->routeNetwork);
    discovery_ready = Ssdp_Initialize(&app->discovery);
    network_ready   = serving_ready && route_ready && discovery_ready;
    device_found    = FALSE;
    if (network_ready)
    {
        device_found = !!FindDevice(app, &options, &device);
    }
    else
    {
        (void)fprintf(stderr,
         "network initialization failed: serving %u, discovery %u, route %u\n",
         MediaServer_GetError(&app->serving),
         Ssdp_GetError(&app->discovery),
         OS_Network_GetError(&app->routeNetwork));
    }
    for (;;)
    {
        discovery_closed = Ssdp_TryClose(&app->discovery);
        if (discovery_closed)
        {
            break;
        }
        Sleep(SESSION_POLL_MS);
    }
    if (!device_found)
    {
        CloseNetworkResources(app);
        return (network_ready && options.list) ? app->runResult : 2;
    }
    if (options.list)
    {
        CloseNetworkResources(app);
        return app->runResult;
    }
    (void)printf("using %s at %s\n", device.friendlyName, device.host);

    ZeroMemory(&config, sizeof(config));
    (void)snprintf(config.host, sizeof(config.host), "%s", device.host);
    config.loop     = options.loop;
    config.verbose  = options.verbose;
    config.callback = OnSessionEvent;
    config.pUser    = app;
    if (options.url[0])
    {
        const char* pExtension = strrchr(options.url, '.');
        wchar_t extension[16];

        (void)snprintf(config.contentUrl, sizeof(config.contentUrl), "%s", options.url);
        (void)snprintf(config.title, sizeof(config.title), "%s", options.url);
        if (NULL != pExtension && NULL == strchr(pExtension, '/') &&
            0 != MultiByteToWideChar(CP_UTF8, 0, pExtension, -1, extension, 16))
        {
            (void)snprintf(config.contentType,
             sizeof(config.contentType),
             "%s",
             MediaServer_GetContentTypeForExtension(extension));
        }
        else
        {
            (void)snprintf(config.contentType, sizeof(config.contentType), "video/mp4");
        }
    }
    else
    {
        const wchar_t* pName      = wcsrchr(options.file, L'\\');
        const wchar_t* pExtension = wcsrchr(options.file, L'.');
        char extension[16];

        pName = (NULL != pName) ? pName + 1 : options.file;
        if (NULL != pExtension && pExtension < pName)
        {
            pExtension = NULL;
        }
        ToUtf8(pName, config.title, sizeof(config.title));
        ToUtf8(NULL != pExtension ? pExtension : L"", extension, sizeof(extension));
        (void)snprintf(config.contentType,
         sizeof(config.contentType),
         "%s",
         MediaServer_GetContentTypeForExtension(pExtension));
        config.servingLocally = 1;
        if (options.bind[0])
        {
            (void)snprintf(localAddress, sizeof(localAddress), "%s", options.bind);
        }
        else if (!CastNetwork_ReadLocalAddress(&app->routeNetwork,
                 device.host,
                 sizeof(device.host),
                 localAddress,
                 sizeof(localAddress)))
        {
            (void)fprintf(stderr, "cannot determine the local address facing %s; pass --bind ADDRESS\n", device.host);
            CloseNetworkResources(app);
            return 1;
        }
        pServer = MediaServer_Start(&app->serving,
                                    options.file,
                                    options.bind[0] ? options.bind : NULL,
                                    options.port,
                                    config.contentType,
                                    error,
                                    sizeof(error));
        if (NULL == pServer)
        {
            (void)fprintf(stderr, "server: %s\n", error);
            CloseNetworkResources(app);
            return 1;
        }
        (void)snprintf(config.contentUrl,
         sizeof(config.contentUrl),
         "http://%s:%u/media%s",
         localAddress,
         (unsigned)MediaServer_GetPort(pServer),
         extension);
        (void)printf("serving %ls as %s (%s)\n", options.file, config.contentUrl, config.contentType);
        pKeyframes = (double*)malloc(MP4_INDEX_MAX_KEYFRAMES * sizeof(double));
        if (NULL != pKeyframes)
        {
            config.keyframeCount = Mp4Index_ReadKeyframes(options.file, pKeyframes, MP4_INDEX_MAX_KEYFRAMES);
            config.pKeyframes    = pKeyframes;
        }
    }

    storage_ready = CastSession_Initialize(&app->session);
    worker_ready  = OS_Thread_Initialize(&app->sessionWorker, SessionJob, app);
    session_ready = storage_ready && worker_ready;
    submitted     = FALSE;
    pSession      = &app->session;
    if (session_ready)
    {
        CastSession_Prepare(pSession, &config);
        submitted = OS_Thread_Submit(&app->sessionWorker);
    }
    if (!submitted)
    {
        (void)fprintf(stderr, "cannot start the session\n");
        CloseServingResources(&app->proxy, &pServer);
        CloseSessionResources(app);
        free(pKeyframes);
        CloseNetworkResources(app);
        return 1;
    }
    /* Build and serve the low-latency scrub proxy in the background (local files only). */
    if (config.servingLocally)
    {
        Proxy_Request_t request;
        bool_t          proxy_started;

        request.serving = &app->serving;
        request.source  = options.file;

        request.sourceCapacity       = MAX_PATH;
        request.localAddress         = localAddress;
        request.localAddressCapacity = sizeof(localAddress);
        request.config               = NULL;
        request.port                 = (uint16_t)(MediaServer_GetPort(pServer) + 1);
        request.session              = pSession;
        proxy_started                = Proxy_Start(&app->proxy, &request);
        if (proxy_started)
        {
            (void)printf("building responsive scrub proxy in the background...\n");
        }
    }
    {
        unsigned       bounceDone = 0;
        ULONGLONG      nextBounce = 0;
        const unsigned dwellMs    = 2800; /* let each resume complete before the next jump */

        for (;;)
        {
            bool_t completed;
            bool_t failed;
            int    stop;

            completed = OS_Thread_TakeResult(&app->sessionWorker);
            failed    = (OS_THREAD_FAILED == OS_Thread_GetState(&app->sessionWorker));
            if (failed)
            {
                app->runResult = 1;
            }
            if (completed || failed)
            {
                break;
            }
            stop = FALSE;
            if (CastEvents_Take(&app->events, &app->snapshot))
            {
                if (app->snapshot.present & CAST_EVENTS_STATUS)
                {
                    bool_t is_playing;
                    bool_t has_started;
                    bool_t first_playing;
                    bool_t has_duration;

                    is_playing    = (CAST_STATE_PLAYING == app->snapshot.status.state);
                    has_started   = (0 != app->playingSince);
                    first_playing = is_playing && !has_started;
                    has_duration  = (0 < app->snapshot.status.duration);
                    if (first_playing)
                    {
                        app->playingSince = GetTickCount64();
                    }
                    if (has_duration)
                    {
                        app->duration = app->snapshot.status.duration;
                    }
                }
            }

            /* Bounce: jump across the timeline many times so the numbers reflect scrubbing, not one
        ** cold seek. Positions follow the golden ratio so successive jumps are far apart. */
            if (options.bounce > 0 && 0 != app->playingSince && app->duration > 0)
            {
                const ULONGLONG t = GetTickCount64();

                if (0 == nextBounce)
                {
                    nextBounce = app->playingSince + 3000;
                }
                if (bounceDone < options.bounce && t >= nextBounce)
                {
                    const double fromFrac = 0.03 + 0.94 * fmod(0.137 + (bounceDone + 0) * 0.6180339887, 1.0);
                    const double toFrac   = 0.03 + 0.94 * fmod(0.137 + (bounceDone + 1) * 0.6180339887, 1.0);
                    const double seconds  = toFrac * app->duration;
                    int step;

                    ++bounceDone;
                    (void)printf("bounce %u/%u -> %.1f s (%.0f%%)\n",
                     bounceDone,
                     options.bounce,
                     seconds,
                     toFrac * 100.0);
                    /* Simulate a real drag: sweep scrub previews toward the target over ~360 ms, then
                ** release. This exercises the proxy switch and hysteresis, not just a lone seek. */
                    for (step = 1; step <= 6; ++step)
                    {
                        const double f = fromFrac + (toFrac - fromFrac) * step / 6.0;

                        CastSession_Post(pSession, CAST_COMMAND_SCRUB, f * app->duration);
                        Sleep(60);
                    }
                    CastSession_Post(pSession, CAST_COMMAND_SEEK, seconds); /* release */
                    nextBounce = t + dwellMs;
                }
                else if (bounceDone >= options.bounce && t >= nextBounce)
                {
                    (void)printf("bounce run complete\n");
                    stop = 1;
                }
            }

            if (options.seek > 0 && 0 != app->playingSince && GetTickCount64() >= app->playingSince + 3000)
            {
                (void)printf("seeking to %.1f s\n", options.seek);
                CastSession_Post(pSession, CAST_COMMAND_SEEK, options.seek);
                options.seek = 0;
            }
            if (options.scrub > 0 && 0 != app->playingSince && GetTickCount64() >= app->playingSince + 3000)
            {
                (void)printf("scrubbing to %.1f s (paused preview at the keyframe before it)\n", options.scrub);
                CastSession_Post(pSession, CAST_COMMAND_SCRUB, options.scrub);
                options.seek      = options.scrub; /* the release: an exact, resuming seek two seconds later */
                options.scrub     = 0;
                app->playingSince += 2000;
            }
            if (options.seconds > 0 && 0 != app->playingSince &&
                GetTickCount64() >= app->playingSince + (ULONGLONG)options.seconds * 1000)
            {
                if (!stopping)
                {
                    (void)printf("stopping after %u seconds\n", options.seconds);
                }
                stop = 1;
            }
            {
                bool_t input_stop;

                input_stop = PollConsoleInput(app);
                if (input_stop)
                {
                    stop = TRUE;
                }
            }
            if (stop && !stopping)
            {
                stopping = 1;
                CastSession_Post(pSession, CAST_COMMAND_STOP, 0);
            }
            Sleep(SESSION_POLL_MS);
        }
    }
    CloseServingResources(&app->proxy, &pServer);
    CloseSessionResources(app);
    free(pKeyframes);
    CloseNetworkResources(app);
    return app->runResult;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Owns the console context from startup through complete resource closure.
 *  Failed startup retains its memory owner for release; no callback borrows the
 *  context after RunConsole returns. Keyboard polling registers no native callback;
 *  native control-signal cancellation remains a separate integration obligation.
 */
int wmain(int argc, wchar_t** argv)
{
    bool_t         ready;
    OS_Memory_t    memory;
    int32_t        result;
    CastConsole_t* app;
    bool_t         closed;
    bool_t         console_failed;

    ready  = OS_Memory_Initialize(&memory, sizeof(CastConsole_t));
    result = 1;
    if (ready)
    {
        app = (CastConsole_t*)OS_Memory_GetAddress(&memory);
        CastEvents_Initialize(&app->events);
        ready = OS_Console_Initialize(&app->console);
        if (ready)
        {
            result = RunConsole(app, argc, argv);
        }
        for (;;)
        {
            closed = OS_Console_TryClose(&app->console);
            if (closed)
            {
                break;
            }
            Sleep(SESSION_POLL_MS);
        }
        console_failed = (0 != OS_Console_GetError(&app->console));
        if (console_failed)
        {
            result = 1;
        }
    }
    for (;;)
    {
        closed = OS_Memory_TryClose(&memory);
        if (closed)
        {
            break;
        }
        Sleep(SESSION_POLL_MS);
    }
    return result;
}
