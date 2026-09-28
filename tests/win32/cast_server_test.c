/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Loopback server shutdown with idle and partially framed persistent clients.
 * Exercises the production listener and client workers; no receiver or GPU is used.
 * Legacy runtime allocation remains in scope and is not certified by this fixture.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <WinSock2.h>
#include <minwindef.h>
#include <inaddr.h>
#include <winerror.h>
#include <stdint.h>
#include <synchapi.h>
#include "media_server.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include "os/os_memory.h"
#include "crt/crt_memory.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define SERVER_TEST_RESPONSE_CAPACITY (1024)
#define SERVER_TEST_ERROR_CAPACITY    (256)
#define SERVER_TEST_LOOPBACK          (0x7F000001)
#define SERVER_TEST_REPETITIONS       (16)
#define SERVER_TEST_CLIENTS           (4)
#define SERVER_TEST_POLL_LIMIT        (1000)
#define SERVER_TEST_POLL_MS           (5)
#define SERVER_TEST_IO_LIMIT_MS       (5000)
#define SERVER_TEST_IDLE_MS           (125)
#define SERVER_TEST_HEAD              ("HEAD /subtitles.vtt HTTP/1.1\r\nHost: 127.0.0.1\r\n\r\n")
#define SERVER_TEST_PARTIAL           ("GET /subtitles.vtt HTTP/1.1\r\nHost:")
#define SERVER_TEST_OK                ("HTTP/1.1 200 ")
#define SERVER_TEST_END               ("\r\n\r\n")
#define SERVER_TEST_VTT               ("WEBVTT\n")

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static MediaServer_Workspace_t g_serving;

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Checks that rejected bind syntax cannot silently select every interface.
 *
 * \par Requirements:
 *  - 001 Rejection shall precede native socket creation and work without Winsock startup.
 *  - 002 Error text shall distinguish invalid input from a later native startup failure.
 *
 * \retval None   All rejection assertions pass or the fixture fails.
 */
static FORCE_INLINE void CheckRejectedBind(void);

/*************************************************************************************************/
/**
 * \brief Connects one test client with finite blocking-I/O deadlines.
 *
 * \par Requirements:
 *  - 001 The server shall be listening exclusively on loopback.
 *  - 002 A native failure shall terminate the fixture through an assertion.
 *
 * \param[in] port   Server-selected listener port
 *
 * \retval    socket   Client owned by the fixture until explicit closure.
 */
static FORCE_INLINE SOCKET ConnectClient(uint16_t port);

/*************************************************************************************************/
/**
 * \brief Sends the complete counted fixture bytes.
 *
 * \par Requirements:
 *  - 001 Partial native sends shall advance exactly by their reported extent.
 *  - 002 The finite native deadline shall fail the fixture rather than permit an indefinite wait.
 *
 * \param[in] client   Exclusive fixture socket
 * \param[in] text     Complete readable request bytes
 * \param[in] count    Exact readable extent
 *
 * \retval    None   The request is queued or the fixture fails.
 */
static FORCE_INLINE void SendText(SOCKET client, const char_t* text, uint32_t count);

/*************************************************************************************************/
/**
 * \brief Establishes that the server accepted and answered a persistent client.
 *
 * \par Requirements:
 *  - 001 A bodyless HEAD response shall have the independently specified status prefix and complete delimiter.
 *  - 002 The fixture shall consume the exact header without waiting for connection closure.
 *
 * \param[in] client   Exclusive connected fixture socket
 *
 * \retval    None   A complete successful response was consumed or the fixture fails.
 */
static FORCE_INLINE void ReadHead(SOCKET client);

/*************************************************************************************************/
/**
 * \brief Checks retained owner identity and eventual shutdown of all admitted clients.
 *
 * \par Requirements:
 *  - 001 Every unsuccessful poll shall preserve the original pointer value.
 *  - 002 Shutdown shall finish with peers still open, including an incomplete request.
 *  - 003 Repeated successful shutdown shall keep the owner NULL.
 *
 * \param[in,out] owner   Required live server pointer
 *
 * \retval        None   Closure and ownership assertions pass or the fixture fails.
 */
static FORCE_INLINE void StopServer(MediaServer** owner);

/*************************************************************************************************/
/**
 * \brief Exercises persistent idle and incomplete requests during server cancellation.
 *
 * \par Requirements:
 *  - 001 Every client shall complete a HEAD before shutdown, proving admission.
 *  - 002 A client shall remain reusable across more than one server read-poll interval.
 *  - 003 Clients shall remain open until server shutdown proves that their workers ended.
 *
 * \retval None   All assertions pass or the fixture fails.
 */
static FORCE_INLINE void CheckClients(void);

/*************************************************************************************************/
/**
 * \brief Exercises complete listener saturation and slot reuse against stable storage.
 *
 * \par Requirements:
 *  - 001 An excess listener shall fail without changing existing identities or pool storage.
 *  - 002 A successfully retired slot shall accept another representation.
 *
 * \retval None   Every bounded admission and reuse assertion passes.
 */
static FORCE_INLINE void CheckListenerCapacity(void);

/*************************************************************************************************/
/**
 * \brief Exercises a client queued beyond worker capacity and subsequent reuse.
 *
 * \par Requirements:
 *  - 001 Every startup worker shall first hold one persistent client.
 *  - 002 Ending one client shall permit the queued peer to obtain a valid response.
 *
 * \retval None   Capacity handoff and cleanup complete within fixture deadlines.
 */
static FORCE_INLINE void CheckClientCapacity(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Malformed syntax has its own diagnostic and does not reserve a listener slot.
 */
static FORCE_INLINE void CheckRejectedBind(void)
{
    MediaServer* server;
    char_t       error[SERVER_TEST_ERROR_CAPACITY];
    int32_t      compared;

    server = MediaServer_Start(&g_serving, NULL, "127.0.0.999", 0, "video/mp4", error, sizeof(error));
    LOGASSERT(NULL == server);
    compared = CRT_Memory_Compare(error, "invalid IPv4 bind address", sizeof("invalid IPv4 bind address"));
    LOGASSERT(0 == compared);
    server = MediaServer_Start(&g_serving, NULL, "", 0, "video/mp4", error, sizeof(error));
    LOGASSERT(NULL == server);
    compared = CRT_Memory_Compare(error, "invalid IPv4 bind address", sizeof("invalid IPv4 bind address"));
    LOGASSERT(0 == compared);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The peer is local; deadline failures close the test through LOGASSERT.
 */
static FORCE_INLINE SOCKET ConnectClient(uint16_t port)
{
    SOCKET             client;
    DWORD              timeout;
    int32_t            status;
    struct sockaddr_in address;

    client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    LOGASSERT(INVALID_SOCKET != client);
    timeout = SERVER_TEST_IO_LIMIT_MS;
    status  = setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char_t*)&timeout, sizeof(timeout));
    LOGASSERT(0 == status);
    status = setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, (const char_t*)&timeout, sizeof(timeout));
    LOGASSERT(0 == status);
    CRT_Memory_Set(&address, 0, sizeof(address));
    address.sin_family      = AF_INET;
    address.sin_port        = htons(port);
    address.sin_addr.s_addr = htonl(SERVER_TEST_LOOPBACK);
    status                  = connect(client, (const struct sockaddr*)&address, sizeof(address));
    LOGASSERT(0 == status);
    return client;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only positive, bounded send counts advance the input position.
 */
static FORCE_INLINE void SendText(SOCKET client, const char_t* text, uint32_t count)
{
    uint32_t offset;
    uint32_t remaining;
    int32_t  sent;

    LOGASSERT(NULL != text);
    LOGASSERT(INT32_MAX >= count);
    offset = 0;
    for (; offset < count;)
    {
        remaining = count - offset;
        sent      = send(client, text + offset, (int32_t)remaining, 0);
        LOGASSERT(0 < sent);
        LOGASSERT(remaining >= (uint32_t)sent);
        offset += (uint32_t)sent;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The small HEAD fixture consumes bytes through CRLF CRLF with no body read.
 */
static FORCE_INLINE void ReadHead(SOCKET client)
{
    uint32_t count;
    bool_t   complete;
    char_t   response[SERVER_TEST_RESPONSE_CAPACITY];
    int32_t  received;
    int32_t  compared;

    SendText(client, SERVER_TEST_HEAD, sizeof(SERVER_TEST_HEAD) - 1);
    count    = 0;
    complete = FALSE;
    for (; (count < sizeof(response)) && (!complete);)
    {
        received = recv(client, response + count, 1, 0);
        LOGASSERT(1 == received);
        ++count;
        if ((sizeof(SERVER_TEST_END) - 1) <= count)
        {
            compared = CRT_Memory_Compare(response + count - (sizeof(SERVER_TEST_END) - 1),
                                          SERVER_TEST_END,
                                          sizeof(SERVER_TEST_END) - 1);
            complete = (0 == compared);
        }
    }
    LOGASSERT(complete);
    LOGASSERT((sizeof(SERVER_TEST_OK) - 1) <= count);
    compared = CRT_Memory_Compare(response, SERVER_TEST_OK, sizeof(SERVER_TEST_OK) - 1);
    LOGASSERT(0 == compared);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A finite fixture deadline detects stuck idle or partial-request workers.
 */
static FORCE_INLINE void StopServer(MediaServer** owner)
{
    MediaServer* original;
    bool_t       stopped;
    uint32_t     attempt;

    original = (*owner);
    stopped  = FALSE;
    for (attempt = 0; (attempt < SERVER_TEST_POLL_LIMIT) && (!stopped); ++attempt)
    {
        stopped = MediaServer_TryStop(owner);
        if (!stopped)
        {
            LOGASSERT(original == (*owner));
            Sleep(SERVER_TEST_POLL_MS);
        }
    }
    LOGASSERT(stopped);
    LOGASSERT(NULL == (*owner));
    stopped = MediaServer_TryStop(owner);
    LOGASSERT(stopped);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All clients first exchange a complete response; two then leave partial heads.
 *  Peer sockets are deliberately kept open until the server confirms shutdown.
 */
static FORCE_INLINE void CheckClients(void)
{
    MediaServer* server;
    char_t       error[SERVER_TEST_ERROR_CAPACITY];
    uint16_t     port;
    uint32_t     index;
    SOCKET       clients[SERVER_TEST_CLIENTS];
    int32_t      received;
    char_t       last;
    bool_t       ended;
    int32_t      native_error;
    int32_t      status;

    server = MediaServer_StartWithSubtitles(&g_serving,
                                            NULL,
                                            "127.0.0.1",
                                            0,
                                            "video/mp4",
                                            (const byte_t*)SERVER_TEST_VTT,
                                            sizeof(SERVER_TEST_VTT) - 1,
                                            error,
                                            sizeof(error));
    LOGASSERT(NULL != server);
    port = MediaServer_GetPort(server);
    LOGASSERT(0 < port);
    for (index = 0; index < SERVER_TEST_CLIENTS; ++index)
    {
        clients[index] = ConnectClient(port);
        ReadHead(clients[index]);
    }
    Sleep(SERVER_TEST_IDLE_MS);
    ReadHead(clients[0]);
    SendText(clients[1], SERVER_TEST_PARTIAL, sizeof(SERVER_TEST_PARTIAL) - 1);
    SendText(clients[3], SERVER_TEST_PARTIAL, sizeof(SERVER_TEST_PARTIAL) - 1);
    StopServer(&server);
    for (index = 0; index < SERVER_TEST_CLIENTS; ++index)
    {
        received = recv(clients[index], &last, 1, 0);
        ended    = (0 == received);
        if (SOCKET_ERROR == received)
        {
            native_error = WSAGetLastError();
            ended        = (WSAECONNRESET == native_error);
        }
        LOGASSERT(ended);
        status = closesocket(clients[index]);
        LOGASSERT(0 == status);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Listener objects remain at fixed addresses until the entire pool closes.
 */
static FORCE_INLINE void CheckListenerCapacity(void)
{
    void*        storage;
    uint64_t     bytes;
    uint32_t     index;
    MediaServer* listeners[MEDIA_SERVER_CAPACITY];
    char_t       error[SERVER_TEST_ERROR_CAPACITY];
    MediaServer* excess;
    int32_t      compared;
    MediaServer* original;

    storage = OS_Memory_GetAddress(&g_serving.memory);
    bytes   = OS_Memory_GetByteCount(&g_serving.memory);
    for (index = 0; index < MEDIA_SERVER_CAPACITY; ++index)
    {
        listeners[index] = MediaServer_Start(&g_serving, NULL, "127.0.0.1", 0, "video/mp4", error, sizeof(error));
        LOGASSERT(NULL != listeners[index]);
    }
    excess = MediaServer_Start(&g_serving, NULL, "127.0.0.1", 0, "video/mp4", error, sizeof(error));
    LOGASSERT(NULL == excess);
    compared = CRT_Memory_Compare(error,
                                  "media listener capacity is exhausted",
                                  sizeof("media listener capacity is exhausted"));
    LOGASSERT(0 == compared);
    original = listeners[0];
    StopServer(&listeners[0]);
    listeners[0] = MediaServer_Start(&g_serving, NULL, "127.0.0.1", 0, "video/mp4", error, sizeof(error));
    LOGASSERT(original == listeners[0]);
    for (index = 0; index < MEDIA_SERVER_CAPACITY; ++index)
    {
        StopServer(&listeners[index]);
    }
    LOGASSERT(storage == OS_Memory_GetAddress(&g_serving.memory));
    LOGASSERT(bytes == OS_Memory_GetByteCount(&g_serving.memory));
    LOGASSERT(0 == MediaServer_GetError(&g_serving));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The extra peer may complete its native handshake while waiting in the backlog.
 *  Its first HTTP response requires a previously occupied startup worker to retire.
 */
static FORCE_INLINE void CheckClientCapacity(void)
{
    MediaServer* server;
    char_t       error[SERVER_TEST_ERROR_CAPACITY];
    uint16_t     port;
    uint32_t     index;
    SOCKET       clients[MEDIA_SERVER_CLIENT_CAPACITY];
    SOCKET       queued;
    int32_t      status;

    server = MediaServer_StartWithSubtitles(&g_serving,
                                            NULL,
                                            "127.0.0.1",
                                            0,
                                            "video/mp4",
                                            (const byte_t*)SERVER_TEST_VTT,
                                            sizeof(SERVER_TEST_VTT) - 1,
                                            error,
                                            sizeof(error));
    LOGASSERT(NULL != server);
    port = MediaServer_GetPort(server);
    for (index = 0; index < MEDIA_SERVER_CLIENT_CAPACITY; ++index)
    {
        clients[index] = ConnectClient(port);
        ReadHead(clients[index]);
    }
    queued = ConnectClient(port);
    status = closesocket(clients[0]);
    LOGASSERT(0 == status);
    ReadHead(queued);
    status = closesocket(queued);
    LOGASSERT(0 == status);
    for (index = 1; index < MEDIA_SERVER_CLIENT_CAPACITY; ++index)
    {
        status = closesocket(clients[index]);
        LOGASSERT(0 == status);
    }
    StopServer(&server);
    LOGASSERT(0 == MediaServer_GetError(&g_serving));
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Null-owner idempotence needs no native startup. Live iterations exercise
 *  actual server resources; they do not establish allocation-free operation.
 */
uint32_t OS_Application_Main(void)
{
    MediaServer* server;
    bool_t       stopped;
    int32_t      status;
    WSADATA      data;
    uint32_t     repetition;
    char_t       error[SERVER_TEST_ERROR_CAPACITY];

    server  = NULL;
    stopped = MediaServer_TryStop(&server);
    LOGASSERT(stopped);
    status = WSAStartup(MAKEWORD(2, 2), &data);
    LOGASSERT(0 == status);
    stopped = MediaServer_Initialize(&g_serving);
    LOGASSERT(stopped);
    CheckRejectedBind();
    CheckListenerCapacity();
    CheckClientCapacity();
    for (repetition = 0; repetition < SERVER_TEST_REPETITIONS; ++repetition)
    {
        server = MediaServer_Start(&g_serving, NULL, "127.0.0.1", 0, "video/mp4", error, sizeof(error));
        LOGASSERT(NULL != server);
        StopServer(&server);
        CheckClients();
    }
    stopped = FALSE;
    for (repetition = 0; (repetition < SERVER_TEST_POLL_LIMIT) && (!stopped); ++repetition)
    {
        stopped = MediaServer_TryClose(&g_serving);
        if (stopped)
        {
            break;
        }
        Sleep(SERVER_TEST_POLL_MS);
    }
    LOGASSERT(stopped);
    stopped = MediaServer_TryClose(&g_serving);
    LOGASSERT(stopped);
    LOGASSERT(!MediaServer_IsReady(&g_serving));
    LOGASSERT(NULL == OS_Memory_GetAddress(&g_serving.memory));
    LOGASSERT(0 == MediaServer_GetError(&g_serving));
    status = WSACleanup();
    LOGASSERT(0 == status);
    stopped = MediaServer_TryStop(&server);
    LOGASSERT(stopped);
    return 0;
}
