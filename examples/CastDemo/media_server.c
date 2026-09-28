/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Native media transport; counted request/framing/response policy is in cast_http.
 * https://www.rfc-editor.org/rfc/rfc9110.html
 * https://www.rfc-editor.org/rfc/rfc9112.html
 * Storage and workers are startup-owned. Native socket/file creation and blocking I/O remain open defects.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <WinSock2.h>
#include <inaddr.h>
#include <MSWSock.h>
#include <minwindef.h>
#include <winnt.h>
#include <fileapi.h>
#include <minwinbase.h>
#include <handleapi.h>
#include <synchapi.h>
#include <sysinfoapi.h>
#include <profileapi.h>
#include <WinBase.h>

#include <stdio.h>
#include <wchar.h>

#include "media_server.h"
#include "cast_http.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_atomic.h"
#include "os/os_memory.h"
#include "os/os_thread.h"
#include "utl/utl_unicode.h"
#include <errhandlingapi.h>
#include <winerror.h>
#include "crt/crt_memory.h"
#include "utl/utl_http.h"
#include "utl/utl_url.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define READ_POLL_MICROSECONDS (50000)
#define ACCEPT_IDLE_MS         (1)
#define FILE_SIZE_HIGH_BITS    (32)
#define RANGE_MEMBER_CAPACITY  (32)
#define REQUEST_IDLE_LIMIT_MS  (5000)
#define ERROR_SERVER_START     ("cannot start media listener")
#define ERROR_SERVER_CAPACITY  ("media listener capacity is exhausted")
#define ERROR_SERVER_INPUT     ("invalid media path or content type")
#define ERROR_SERVER_UNREADY   ("media serving workspace is unavailable")

#define ERROR_BIND_ADDRESS ("invalid IPv4 bind address")
#define ERROR_TEXT_NUL     ('\0')

#define REQUEST_CAPACITY       (8192)
#define REQUEST_FIELD_CAPACITY (REQUEST_CAPACITY / 4)
#define RESPONSE_CAPACITY      (1024)
#define CONTENT_TYPE_CAPACITY  (64)
#define SUBTITLE_CONTENT_TYPE  ("text/vtt; charset=utf-8")
#define TRANSMIT_MAX           (62914560)    /* 60 MiB per TransmitFile call; larger ranges loop */
#define SEND_BUFFER_SIZE       (1024 * 1024) /* keeps a Wi-Fi link full after a seek despite its round trips */
#define GROW_POLL_MS           (8)
#define GROW_IDLE_MAX          (60000)
#define TRANSFER_MEGABYTE      (1048576.0)
#define TRANSFER_MILLISECONDS  (1000.0)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Published listener lifetime; only FREE may be reserved by a start caller.
 */
typedef enum ServerState_e
{
    SERVER_FREE,
    SERVER_RESERVED,
    SERVER_ACTIVE,
    SERVER_STOPPING,
    SERVER_STOPPED
} ServerState_t;

/*************************************************************************************************/
/**
 * \brief Growing-file completion belongs to the retained listener slot.
 */
typedef struct LiveState_s
{
    OS_Atomic32_t complete; ///< Atomic producer completion latch.
} LiveState_t;

/*************************************************************************************************/
/**
 * \brief Immutable caller-owned subtitle bytes borrowed through successful stop.
 */
typedef struct SubtitleData_s
{
    uint64_t      size; ///< Exact published representation size.
    const byte_t* data; ///< Immutable caller-owned bytes retained through successful stop.
} SubtitleData_t;

/*************************************************************************************************/
/**
 * \brief Fixed listener slot; admission owns native cleanup after publication.
 */
struct MediaServer
{
    struct MediaServer_Storage_s* storage;                            ///< Retained private pool lifetime.
    OS_Atomic32_t                 state;                              ///< Published listener lifecycle; FREE alone permits reservation.
    OS_Atomic32_t                 stopping;                           ///< Atomic request observed by client callbacks.
    SOCKET                        listener;                           ///< Admission-owned socket after ACTIVE publication.
    uint32_t                      clients;                            ///< Admission-owned references retained through native cleanup.
    bool_t                        abandoned;                          ///< Failed start has no public owner; admission returns its slot to FREE.
    TCHAR                         file[MAX_PATH];                     ///< Immutable copied UTF-16 path through successful stop.
    char_t                        contentType[CONTENT_TYPE_CAPACITY]; ///< Immutable terminated response media type.
    uint16_t                      port;                               ///< Final bound native port.
    uint64_t                      size;                               ///< Exact published representation size.
    LiveState_t                   liveState;                          ///< Retained growing-file completion metadata.
    SubtitleData_t                subtitleData;                       ///< Retained description of the immutable external subtitle borrow.
    LiveState_t*                  live;                               ///< Optional borrowed growing-file completion.
    const SubtitleData_t*         subtitles;                          ///< Immutable subtitle bytes or metadata retained through client retirement.
};

/*************************************************************************************************/
/**
 * \brief One reusable client worker and its exclusively owned native resources.
 */
typedef struct Connection_s
{
    OS_Thread_t        worker; ///< Startup execution owner; admission serializes submission/acquisition.
    bool_t             busy;   ///< An accepted callback still owns connection fields.
    CastHttp_Request_t http;   ///< Counted per-connection HTTP parser and policy.
    Utl_Http_Field_t
                  fields[REQUEST_FIELD_CAPACITY]; ///< Fixed request-field slots bounded by the admitted request extent.
    byte_t                request[REQUEST_CAPACITY];          ///< Retained request prefix and pipelined bytes.
    byte_t                response[RESPONSE_CAPACITY];        ///< Complete response header serialized before sending.
    uint32_t              requestUsed;                        ///< Initialized request bytes, bounded by REQUEST_CAPACITY.
    LARGE_INTEGER         responseStart;                      ///< Counter acquired before response dispatch.
    bool_t                responseTimed;                      ///< Counter query succeeded.
    MediaServer*          server;                             ///< Listener reference retained through native resource release.
    SOCKET                socket;                             ///< Accepted native identity retained until successful close.
    HANDLE                hFile;                              ///< Reusable request file handle retained until successful close.
    TCHAR                 file[MAX_PATH];                     ///< Immutable copied UTF-16 path through successful stop.
    char_t                contentType[CONTENT_TYPE_CAPACITY]; ///< Immutable terminated response media type.
    uint64_t              size;                               ///< Exact published representation size.
    LiveState_t*          live;                               ///< Optional borrowed growing-file completion.
    const SubtitleData_t* subtitles;                          ///< Immutable subtitle bytes or metadata retained through client retirement.
} Connection_t;

/*************************************************************************************************/
/**
 * \brief Resident startup storage; one admission worker serializes all client slots.
 */
typedef struct MediaServer_Storage_s
{
    OS_Thread_t*  admission;                      ///< Startup admission worker or its retained reference.
    OS_Atomic32_t recovery;                       ///< Exclusive nonwaiting cleanup claim after admission failure.
    OS_Atomic32_t closing;                        ///< Atomic final pool shutdown request.
    OS_Atomic32_t error;                          ///< Sticky first native failure.
    MediaServer   servers[MEDIA_SERVER_CAPACITY]; ///< Fixed listener slots shared by all start callers.
    Connection_t
        connections[MEDIA_SERVER_CLIENT_CAPACITY]; ///< Fixed clients and HTTP workspaces shared by all listeners.
} MediaServer_Storage_t;

/*************************************************************************************************/
/**
 * \brief Fully validated borrowed representation; copying never performs native I/O.
 */
typedef struct ServerRepresentation_s
{
    const uint16_t* file;             ///< Immutable copied UTF-16 path through successful stop.
    uint32_t        fileUnits;        ///< Validated UTF-16 path units excluding termination.
    const char_t*   contentType;      ///< Immutable terminated response media type.
    uint32_t        contentTypeBytes; ///< Validated media-type bytes excluding termination.
    bool_t          growing;          ///< The producer may append bytes until completion publication.
    const byte_t*   subtitles;        ///< Immutable subtitle bytes or metadata retained through client retirement.
    uint64_t        subtitleBytes;    ///< Readable immutable subtitle extent.
    uint64_t        fileBytes;        ///< Validated native file size.
} ServerRepresentation_t;

/*************************************************************************************************/
/**
 * \brief Validated numeric endpoint passed as one typed value.
 */
typedef struct ServerEndpoint_s
{
    uint32_t address; ///< Validated numeric IPv4 address independent of byte order.
    uint16_t port;    ///< Final bound native port.
} ServerEndpoint_t;

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
 * \brief Borrows the initialized connection media type.
 *
 * \par Requirements:
 *  - 001 The connection shall contain a terminated type within its fixed array.
 *
 * \param[in] connection   Initialized connection metadata
 *
 * \retval    text   Counted borrowed media type.
 */
static FORCE_INLINE Utl_Http_Text_t GetContentType(const Connection_t* connection);

/*************************************************************************************************/
/**
 * \brief Polls readability for one exclusively owned native socket.
 *
 * \par Requirements:
 *  - 001 The caller shall retain the socket and exclude concurrent reads and closure.
 *  - 002 Each poll shall use a finite timeout; timeout shall preserve the socket and queued bytes.
 *  - 003 Native provider timing and allocation remain separate review obligations.
 *
 * \param[in] socket   Exclusive listener or connection
 *
 * \retval    1              Read or accept can proceed.
 * \retval    0              No readability was observed before timeout.
 * \retval    SOCKET_ERROR   The native query failed.
 */
static FORCE_INLINE int32_t PollRead(SOCKET socket);

/*************************************************************************************************/
/**
 * \brief Sends one complete counted span through bounded native send counts.
 *
 * \par Requirements:
 *  - 001 The source shall retain byteCount readable bytes until the operation completes.
 *  - 002 Partial writes shall advance only by the successfully sent count.
 *  - 003 This native path is still blocking; realtime transport remains unfinished.
 *
 * \param[in] socket      Connected native socket
 * \param[in] data        Stable readable bytes
 * \param[in] byteCount   Exact extent
 *
 * \retval    TRUE    Every byte was sent.
 * \retval    FALSE   A native send failed or made no valid progress.
 */
static FORCE_INLINE bool_t SendAll(SOCKET socket, const void* data, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Transmits a fixed file interval using native zero-copy sends.
 *
 * \par Requirements:
 *  - 001 The file handle and interval shall describe readable retained content.
 *  - 002 pComplete shall become true only after every selected byte is transmitted.
 *
 * \param[in]  pConnection   Retained native resources
 * \param[in]  first         Validated starting offset
 * \param[in]  remaining     Validated selected byte count
 * \param[out] pComplete     Required completion result
 *
 * \retval     sent   Number of bytes transmitted before completion or failure.
 */
static FORCE_INLINE uint64_t StreamFile(const Connection_t* pConnection,
                                        uint64_t            first,
                                        uint64_t            remaining,
                                        int*                pComplete);

/*************************************************************************************************/
/**
 * \brief Transmits available growing-file bytes until completion or failure.
 *
 * \par Requirements:
 *  - 001 The file and shared completion state shall remain live through the operation.
 *  - 002 The polling limit shall terminate a stalled producer.
 *
 * \param[in] pConnection   Retained native resources
 * \param[in] first         Starting byte offset
 *
 * \retval    sent   Bytes transmitted before completion, disconnect or stall.
 */
static FORCE_INLINE uint64_t StreamGrowingFile(const Connection_t* pConnection, uint64_t first);

/*************************************************************************************************/
/**
 * \brief Serializes and sends one complete typed response head.
 *
 * \par Requirements:
 *  - 001 The response and its borrowed media type shall remain unchanged through serialization.
 *  - 002 Invalid metadata or insufficient fixed output shall send no header bytes.
 *
 * \param[in,out] connection   Exclusive output workspace and native socket
 * \param[in]     response     Validated serving metadata
 *
 * \retval        TRUE    All header bytes were sent.
 * \retval        FALSE   Serialization or native sending failed.
 */
static FORCE_INLINE bool_t SendResponse(Connection_t* connection, const CastHttp_Response_t* response);

/*************************************************************************************************/
/**
 * \brief Sends a typed bodyless error response before connection closure.
 *
 * \par Requirements:
 *  - 001 Status shall be an error supported by the application response model.
 *  - 002 Error responses shall close without interpreting any remaining request bytes.
 *
 * \param[in,out] connection   Exclusive native connection
 * \param[in]     status       Named response status
 *
 * \retval        TRUE    The complete header was sent.
 * \retval        FALSE   No complete header could be sent.
 */
static FORCE_INLINE bool_t SendErrorResponse(Connection_t* connection, uint16_t status);

/*************************************************************************************************/
/**
 * \brief Records a counted request and completed transfer summary.
 *
 * \par Requirements:
 *  - 001 Request shall be READY and retain its bounded source bytes.
 *  - 002 The legacy CRT logging dependency remains an open migration defect.
 *  - 003 Failed native timing or invalid counter intervals shall report zero duration and rate.
 *
 * \param[in] connection   Response timing owner
 * \param[in] request      Completed request policy
 * \param[in] response     Selected response metadata
 * \param[in] sent         Transmitted body bytes
 *
 * \retval    None   The diagnostic is attempted.
 */
static FORCE_INLINE void LogResponse(const Connection_t*        connection,
                                     const CastHttp_Request_t*  request,
                                     const CastHttp_Response_t* response,
                                     uint64_t                   sent);

/*************************************************************************************************/
/**
 * \brief Derives a progressive response or a bounded snapshot selected by Range.
 *
 * \par Requirements:
 *  - 001 Caller shall retain a ready request and an open growing file.
 *  - 002 Eligible ranges shall use one nonnegative native file-size snapshot.
 *  - 003 Partial responses shall have a finite body and an unknown final total.
 *  - 004 Failed size queries shall select an empty 500 response; all responses shall close.
 *
 * \param[in]  connection   Retained native file and media type
 * \param[in]  request      Ready request policy
 * \param[out] response     Exclusive complete response metadata
 *
 * \retval     None   All response members are initialized.
 */
static FORCE_INLINE void ReadGrowingResponse(const Connection_t*       connection,
                                             const CastHttp_Request_t* request,
                                             CastHttp_Response_t*      response);

/*************************************************************************************************/
/**
 * \brief Serves a growing representation without claiming a fixed final length.
 *
 * \par Requirements:
 *  - 001 The ready request and native file shall remain alive.
 *  - 002 A satisfiable range shall serve a finite snapshot; an ordinary GET shall stream from byte zero as the file grows.
 *  - 003 HEAD shall emit no body and growing responses shall close.
 *
 * \param[in,out] connection   Exclusive native connection
 * \param[in]     request      Ready request policy
 *
 * \retval        FALSE   This response always closes the connection.
 */
static FORCE_INLINE bool_t HandleGrowingRequest(Connection_t* connection, const CastHttp_Request_t* request);

/*************************************************************************************************/
/**
 * \brief Serves one fixed file representation or selected range.
 *
 * \par Requirements:
 *  - 001 Only a ready request shall reach this handler.
 *  - 002 HEAD shall omit content; unsatisfiable ranges shall emit a typed 416.
 *  - 003 Persistence shall require complete successful sending and request permission.
 *
 * \param[in,out] connection   Exclusive native connection
 * \param[in]     request      Ready request policy
 *
 * \retval        TRUE    Another request may be read.
 * \retval        FALSE   The connection shall close.
 */
static FORCE_INLINE bool_t HandleRequest(Connection_t* connection, const CastHttp_Request_t* request);

/*************************************************************************************************/
/**
 * \brief Serves retained WebVTT bytes using the shared response and range model.
 *
 * \par Requirements:
 *  - 001 Subtitle bytes and ownership shall remain live through completion.
 *  - 002 HEAD and Connection options shall follow the same policy as fixed files.
 *
 * \param[in,out] connection   Exclusive native connection
 * \param[in]     request      Ready request policy
 *
 * \retval        TRUE    Another request may be read.
 * \retval        FALSE   The connection shall close.
 */
static FORCE_INLINE bool_t HandleSubtitleRequest(Connection_t* connection, const CastHttp_Request_t* request);

/*************************************************************************************************/
/**
 * \brief Receives a complete counted request into retained connection storage.
 *
 * \par Requirements:
 *  - 001 requestUsed shall not exceed the fixed buffer.
 *  - 002 Malformed, oversized or unsupported requests shall receive a closing error response.
 *  - 003 No body byte shall be reinterpreted as a pipelined request.
 *
 * \param[in,out] connection   Exclusive input and parser state
 *
 * \retval        TRUE    Exactly one READY request is available.
 * \retval        FALSE   The connection failed or its request was rejected.
 */
static FORCE_INLINE bool_t ReadRequest(Connection_t* connection);

/*************************************************************************************************/
/**
 * \brief Dispatches a READY request without rescanning raw syntax.
 *
 * \par Requirements:
 *  - 001 The request model shall retain its complete immutable source.
 *  - 002 Missing media and native file failures shall produce closing status responses.
 *
 * \param[in,out] connection   Exclusive READY request and resources
 *
 * \retval        TRUE    The response permits another request.
 * \retval        FALSE   The connection shall close.
 */
static FORCE_INLINE bool_t ServeRequest(Connection_t* connection);

/*************************************************************************************************/
/**
 * \brief Owns one legacy connection worker until cleanup.
 *
 * \par Requirements:
 *  - 001 The accepted connection shall be exclusively owned by this worker; immutable metadata remains borrowed.
 *  - 002 Pipeline storage shall advance only after a complete persistent response.
 *  - 003 Native file creation and blocking operations remain open goal defects; admission retains native-release failures.
 *
 * \param[in,out] parameter   Connection_t supplied by AcceptLoop
 *
 * \retval        None   The callback returned native-resource ownership to admission.
 */
static FORCE_INLINE void ServeConnection(void* parameter);

/*************************************************************************************************/
/**
 * \brief Reads the optional native listener address through the IPv4 specification model.
 *
 * \par Requirements:
 *  - 001 The required output shall assert when missing; a non-NULL input shall be terminated.
 *  - 002 NULL alone shall select all interfaces; malformed or noncanonical IPv4 syntax shall fail.
 *  - 003 Rejection shall preserve output; success shall publish the first octet in bits 31 through 24.
 *
 * \param[in]  text      Optional terminated IPv4 string
 * \param[out] address   Required disjoint numeric result
 *
 * \retval     TRUE    The explicit address or all-interface selection is published.
 * \retval     FALSE   The supplied address is invalid; output is unchanged.
 */
static FORCE_INLINE bool_t ReadBindAddress(const char_t* text, uint32_t* address);

/*************************************************************************************************/
/**
 * \brief Retains the first native pool failure.
 *
 * \par Requirements:
 *  - 001 Error publication shall be atomic; zero on failure shall become ERROR_GEN_FAILURE.
 *
 * \param[in,out] storage   Retained startup storage
 * \param[in] code   Captured native failure
 *
 * \retval None   Existing failure evidence remains unchanged.
 */
static FORCE_INLINE void RecordError(MediaServer_Storage_t* storage, uint32_t code);

/*************************************************************************************************/
/**
 * \brief Copies a bounded diagnostic without a formatting runtime.
 *
 * \par Requirements:
 *  - 001 Nonzero capacity shall provide writable output; source and output shall be disjoint.
 *
 * \param[out] output   Optional when capacity is zero
 * \param[in] capacity   Writable bytes
 * \param[in] text   Complete constant diagnostic
 * \param[in] count   Diagnostic byte count
 *
 * \retval None   Nonempty output capacity contains a terminated diagnostic.
 */
static FORCE_INLINE void WriteError(char_t* output, uint32_t capacity, const char_t* text, uint32_t count);

/*************************************************************************************************/
/**
 * \brief Queries complete listener/client retirement through published slot states.
 *
 * \par Requirements:
 *  - 001 STOPPED/FREE shall publish only after native handles and client references retire.
 *  - 002 Final shutdown shall exclude new reservations before using this query.
 *
 * \param[in] storage   Retained startup pool
 *
 * \retval TRUE    Every slot is quiescent.
 * \retval FALSE   A reservation, listener or client still retains resources.
 */
static FORCE_INLINE bool_t IsPoolQuiescent(const MediaServer_Storage_t* storage);

/*************************************************************************************************/
/**
 * \brief Finds an available startup-owned client slot.
 *
 * \par Requirements:
 *  - 001 Only the admission worker shall inspect or reuse connection ownership.
 *  - 002 A failed worker or retained native resource shall never be reused.
 *
 * \param[in,out] storage   Admission-owned slot metadata
 *
 * \retval connection   A quiescent reusable slot.
 * \retval NULL   Capacity is occupied or unavailable.
 */
static FORCE_INLINE Connection_t* FindConnection(MediaServer_Storage_t* storage);

/*************************************************************************************************/
/**
 * \brief Reserves one fixed listener slot before writing its metadata.
 *
 * \par Requirements:
 *  - 001 Reservation shall use one compare/exchange per candidate and a bounded scan.
 *  - 002 Concurrent start callers shall never obtain the same slot.
 *
 * \param[in,out] storage   Ready startup pool
 *
 * \retval server   Exclusive unpublished slot.
 * \retval NULL   No listener slot is available.
 */
static FORCE_INLINE MediaServer* ReserveServer(MediaServer_Storage_t* storage);

/*************************************************************************************************/
/**
 * \brief Retries one quiescent client's native file and socket releases.
 *
 * \par Requirements:
 *  - 001 Callback ownership shall have been acquired before native identities are inspected.
 *  - 002 Successful releases shall clear only their own identities; failed releases shall remain retained.
 *  - 003 The server reference shall remain live throughout this operation, including after failure.
 *
 * \param[in,out] storage   Exclusive admission failure evidence
 * \param[in,out] connection   Quiescent retained native resources
 *
 * \retval TRUE    Both native identities are released.
 * \retval FALSE   At least one release remains pending.
 */
static FORCE_INLINE bool_t TryCloseConnectionResources(MediaServer_Storage_t* storage, Connection_t* connection);

/*************************************************************************************************/
/**
 * \brief Acquires and retires one client without losing failed native releases.
 *
 * \par Requirements:
 *  - 001 Only admission or the exclusive quiescent recovery owner shall call this operation.
 *  - 002 Busy callback fields shall remain untouched until completion acquisition.
 *  - 003 Both native resources shall be released before the server reference ends.
 *
 * \param[in,out] storage   Exclusive admission metadata
 * \param[in,out] connection   Retained slot
 *
 * \retval None   Completion/cleanup is acquired or remains pending.
 */
static FORCE_INLINE void RetireConnection(MediaServer_Storage_t* storage, Connection_t* connection);

/*************************************************************************************************/
/**
 * \brief Acquires completed callbacks and retries their native cleanup.
 *
 * \par Requirements:
 *  - 001 Only admission shall invoke this operation; busy callback fields shall remain untouched.
 *  - 002 Failed file/socket release shall retain identity and the server reference.
 *  - 003 The final reference shall end only after both native resources are released.
 *
 * \param[in,out] storage   Admission-owned connection lifetimes
 *
 * \retval None   Each slot receives at most one completion and cleanup attempt.
 */
static FORCE_INLINE void RetireConnections(MediaServer_Storage_t* storage);

/*************************************************************************************************/
/**
 * \brief Publishes one accepted socket to a reusable startup worker.
 *
 * \par Requirements:
 *  - 001 The admission worker shall own a free connection slot and the accepted socket.
 *  - 002 Socket identity and server borrowing shall be retained before any fallible operation.
 *  - 003 Submission failure shall leave cleanup to the normal retirement path.
 *
 * \param[in,out] server   Active retained listener
 * \param[in,out] connection   Exclusive free slot
 * \param[in] client   Accepted native socket
 *
 * \retval None   Work is submitted or retained for native cleanup.
 */
static FORCE_INLINE void StartClient(MediaServer* server, Connection_t* connection, SOCKET client);

/*************************************************************************************************/
/**
 * \brief Attempts one accept using a nonblocking listener and an available client slot.
 *
 * \par Requirements:
 *  - 001 Only admission shall use a published listener socket.
 *  - 002 Saturation shall retain the native backlog without accepting an unowned socket.
 *
 * \param[in,out] server   Active listener metadata
 *
 * \retval None   At most one native accept and client submission occur.
 */
static FORCE_INLINE void PollListener(MediaServer* server);

/*************************************************************************************************/
/**
 * \brief Retires a stopped listener only after native cleanup and every client reference.
 *
 * \par Requirements:
 *  - 001 Admission shall no longer accept on this listener.
 *  - 002 Failed close shall retain identity; borrowed data shall remain live until STOPPED.
 *
 * \param[in,out] server   Stopping listener slot
 *
 * \retval None   Successful retirement publishes STOPPED or releases an abandoned start.
 */
static FORCE_INLINE void RetireListener(MediaServer* server);

/*************************************************************************************************/
/**
 * \brief Advances retained cleanup when the admission thread fails before callback execution.
 *
 * \par Requirements:
 *  - 001 FAILED shall acquire callback quiescence before any admission-owned field is inspected.
 *  - 002 Competing stop callers shall attempt one atomic recovery claim without waiting.
 *  - 003 New admission shall remain unavailable; every active listener shall be cancelled.
 *  - 004 Failed native releases shall remain retained for a subsequent recovery attempt.
 *
 * \param[in,out] storage   Retained pool with a failed admission worker
 *
 * \retval None   This caller advanced one bounded cleanup pass or another caller owns recovery.
 */
static FORCE_INLINE void RecoverAdmission(MediaServer_Storage_t* storage);

/*************************************************************************************************/
/**
 * \brief Serializes listener admission and client retirement on one startup worker.
 *
 * \par Requirements:
 *  - 001 Each pass shall visit fixed listener/client capacities once.
 *  - 002 Shutdown shall cancel every published listener and retain all pending cleanup.
 *  - 003 The callback shall return only when no listener/client can issue native I/O.
 *
 * \param[in,out] context   Stable initialized MediaServer_Workspace_t owner
 *
 * \retval None   Global shutdown and all native retirement completed.
 */
static FORCE_INLINE void AcceptLoop(void* context);

/*************************************************************************************************/
/**
 * \brief Configures one reserved native listener before publishing it.
 *
 * \par Requirements:
 *  - 001 Every acquired native identity shall remain in the reserved slot on failure.
 *  - 002 Each native operation shall succeed before the dependent next operation.
 *
 * \param[in,out] server   Exclusive reserved slot
 * \param[in] endpoint   Validated address and requested port
 *
 * \retval TRUE   Listener and final port are available.
 * \retval FALSE   Failure is recorded; slot retains cleanup.
 */
static FORCE_INLINE bool_t OpenListener(MediaServer* server, const ServerEndpoint_t* endpoint);

/*************************************************************************************************/
/**
 * \brief Copies validated immutable inputs into a reserved listener slot.
 *
 * \par Requirements:
 *  - 001 The slot shall be reserved and free of native/client ownership.
 *  - 002 The caller shall have proved path/media-type termination and every source extent.
 *  - 003 Subtitle bytes shall remain borrowed until the returned listener is stopped.
 *
 * \param[in,out] server   Exclusive reserved slot
 * \param[in] representation   Validated source model
 *
 * \retval None   All client-visible metadata is initialized before publication.
 */
static FORCE_INLINE void PrepareRepresentation(MediaServer* server, const ServerRepresentation_t* representation);

/*************************************************************************************************/
/**
 * \brief Reads native file size after complete path syntax validation.
 *
 * \par Requirements:
 *  - 001 A NULL source shall select an empty video representation without native I/O.
 *  - 002 A failed native query or directory shall preserve the model file-size output.
 *
 * \param[in,out] representation   Validated path and destination size
 * \param[in,out] storage   Retained native failure evidence
 *
 * \retval TRUE   The optional file size is available.
 * \retval FALSE   The file cannot be served.
 */
static FORCE_INLINE bool_t ReadRepresentationFile(ServerRepresentation_t* representation,
                                                  MediaServer_Storage_t*  storage);

/*************************************************************************************************/
/**
 * \brief Validates one representation and publishes a reserved listener slot.
 *
 * \par Requirements:
 *  - 001 Required workspace/type/error and nonempty subtitle storage shall assert.
 *  - 002 All external syntax/extents shall validate before reservation and native startup.
 *  - 003 Subtitle bytes shall be immutable and retained until successful TryStop.
 *  - 004 Failed native startup shall preserve resources for admission-owned cleanup.
 *
 * \param[in,out] workspace   Ready startup lifetime
 * \param[in] file   Optional terminated UTF-16 path
 * \param[in] bindAddress   Optional terminated IPv4 address
 * \param[in] port   Requested port
 * \param[in] contentType   Terminated bounded media type
 * \param[in] growing   Growing representation selection
 * \param[in] vtt   Optional immutable borrowed WebVTT
 * \param[in] vttSize   Exact subtitle bytes
 * \param[out] error   Optional bounded diagnostic
 * \param[in] errorCapacity   Writable error bytes
 *
 * \retval server   Active retained listener.
 * \retval NULL   No representation was published.
 */
static FORCE_INLINE MediaServer* StartInternal(MediaServer_Workspace_t* workspace,
                                               const uint16_t*          file,
                                               const char_t*            bindAddress,
                                               uint16_t                 port,
                                               const char_t*            contentType,
                                               bool_t                   growing,
                                               const byte_t*            vtt,
                                               uint64_t                 vttSize,
                                               char_t*                  error,
                                               uint32_t                 errorCapacity);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The fixed connection metadata must retain its initialization terminator.
 */
static FORCE_INLINE Utl_Http_Text_t GetContentType(const Connection_t* connection)
{
    uint32_t        count;
    Utl_Http_Text_t text;
    for (count = 0; count < sizeof(connection->contentType); ++count)
    {
        if (0 == connection->contentType[count])
        {
            break;
        }
    }
    LOGASSERT(count < sizeof(connection->contentType));
    text.data      = (const byte_t*)connection->contentType;
    text.byteCount = count;
    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Winsock select read readiness permits an immediate receive under exclusive
 *  socket ownership. The finite poll lets the owning worker observe cancellation.
 *  https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-select
 */
static FORCE_INLINE int32_t PollRead(SOCKET socket)
{
    fd_set         readable;
    struct timeval timeout;
    int32_t        selected;

    FD_ZERO(&readable);
    FD_SET(socket, &readable);
    timeout.tv_sec  = 0;
    timeout.tv_usec = READ_POLL_MICROSECONDS;
    selected        = select(0, &readable, NULL, NULL, &timeout);
    return selected;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each native send is limited to its signed count domain; partial writes advance only after success.
 */
static FORCE_INLINE bool_t SendAll(SOCKET socket, const void* data, uint64_t byteCount)
{
    const char_t* cursor;
    uint64_t      remaining;
    bool_t        valid;
    int32_t       wanted;
    int32_t       sent;
    LOGASSERT((NULL != data) || (0 == byteCount));
    cursor    = (const char_t*)data;
    remaining = byteCount;
    valid     = TRUE;
    for (;;)
    {
        if ((!valid) || (0 == remaining))
        {
            break;
        }
        wanted = INT32_MAX;
        if (remaining < INT32_MAX)
        {
            wanted = (int32_t)remaining;
        }
        sent  = send(socket, cursor, wanted, 0);
        valid = (0 < sent) && (sent <= wanted);
        if (valid)
        {
            remaining -= (uint32_t)sent;
            cursor    += (uint32_t)sent;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Uses bounded TransmitFile requests from the file cache to the socket.
 *  Reports a partial byte count if positioning or transmission fails.
 */
static FORCE_INLINE uint64_t StreamFile(const Connection_t* pConnection,
                                        uint64_t            first,
                                        uint64_t            remaining,
                                        int*                pComplete)
{
    uint64_t sent = 0;

    *pComplete = 0;
    if (INVALID_HANDLE_VALUE == pConnection->hFile)
    {
        return 0;
    }
    for (;;)
    {
        LARGE_INTEGER offset;
        DWORD         want;

        if (0 == remaining)
        {
            break;
        }
        want            = (remaining < TRANSMIT_MAX) ? (DWORD)remaining : TRANSMIT_MAX;
        offset.QuadPart = (LONGLONG)(first + sent);
        if (!SetFilePointerEx(pConnection->hFile, offset, NULL, FILE_BEGIN) ||
            !TransmitFile(pConnection->socket, pConnection->hFile, want, 0, NULL, NULL, 0))
        {
            return sent;
        }
        sent      += want;
        remaining -= want;
    }
    *pComplete = 1;
    return sent;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Streams currently available bytes and polls when caught up. Stops on writer
 *  completion, transmission failure or the configured idle-poll limit.
 */
static FORCE_INLINE uint64_t StreamGrowingFile(const Connection_t* pConnection, uint64_t first)
{
    uint64_t sent      = 0;
    unsigned idleWaits = 0;

    if (INVALID_HANDLE_VALUE == pConnection->hFile)
    {
        return 0;
    }
    for (;;)
    {
        LARGE_INTEGER fileSize;
        uint64_t      available;

        if (!GetFileSizeEx(pConnection->hFile, &fileSize))
        {
            return sent;
        }
        available = (uint64_t)fileSize.QuadPart;
        if (first + sent < available)
        {
            LARGE_INTEGER offset;
            const uint64_t remaining = available - (first + sent);
            const DWORD    want      = (remaining < TRANSMIT_MAX) ? (DWORD)remaining : TRANSMIT_MAX;

            offset.QuadPart = (LONGLONG)(first + sent);
            if (!SetFilePointerEx(pConnection->hFile, offset, NULL, FILE_BEGIN) ||
                !TransmitFile(pConnection->socket, pConnection->hFile, want, 0, NULL, NULL, 0))
            {
                return sent; /* the receiver closed the connection */
            }
            sent      += want;
            idleWaits = 0;
            continue;
        }
        /* Caught up with what has been written so far. */
        if (NULL == pConnection->live || OS_Atomic_Load32(&pConnection->live->complete))
        {
            return sent; /* the file is done growing and fully sent */
        }
        if (idleWaits * GROW_POLL_MS >= GROW_IDLE_MAX)
        {
            return sent; /* the writer stalled without completing; do not hang forever */
        }
        ++idleWaits;
        Sleep(GROW_POLL_MS);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No header byte is sent before complete model and capacity validation.
 */
static FORCE_INLINE bool_t SendResponse(Connection_t* connection, const CastHttp_Response_t* response)
{
    uint32_t count;
    bool_t   sent;
    count = CastHttp_WriteResponse(response, connection->response, RESPONSE_CAPACITY);
    sent  = (0 < count);
    if (sent)
    {
        sent = SendAll(connection->socket, connection->response, count);
    }
    return sent;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Bodyless status and persistence semantics are owned by the typed response serializer.
 */
static FORCE_INLINE bool_t SendErrorResponse(Connection_t* connection, uint16_t status)
{
    CastHttp_Response_t response;
    bool_t              sent;
    LOGASSERT(UTL_HTTP_STATUS_BAD_REQUEST <= status);
    CastHttp_InitializeResponse(&response, status);
    sent = SendResponse(connection, &response);
    return sent;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Counted method and target spans are bounded by the admitted request buffer.
 */
static FORCE_INLINE void LogResponse(const Connection_t*        connection,
                                     const CastHttp_Request_t*  request,
                                     const CastHttp_Response_t* response,
                                     uint64_t                   sent)
{
    const Utl_Http_Head_t* head;
    LARGE_INTEGER          frequency;
    LARGE_INTEGER          end;
    bool_t                 has_frequency;
    bool_t                 has_end;
    bool_t                 valid_frequency;
    bool_t                 ordered;
    bool_t                 measured;
    double                 seconds;
    double                 megabytes;
    double                 megabytes_per_second;
    head = &request->reader.head;
    LOGASSERT(head->method.byteCount <= REQUEST_CAPACITY);
    LOGASSERT(head->target.byteCount <= REQUEST_CAPACITY);
    frequency.QuadPart   = 0;
    end.QuadPart         = 0;
    has_frequency        = QueryPerformanceFrequency(&frequency);
    has_end              = QueryPerformanceCounter(&end);
    valid_frequency      = has_frequency && (0 < frequency.QuadPart);
    ordered              = (0 <= connection->responseStart.QuadPart) && (connection->responseStart.QuadPart <= end.QuadPart);
    measured             = connection->responseTimed && has_end && valid_frequency && ordered;
    seconds              = 0.0;
    megabytes            = (double)sent / TRANSFER_MEGABYTE;
    megabytes_per_second = 0.0;
    if (measured)
    {
        seconds = (double)(end.QuadPart - connection->responseStart.QuadPart) / (double)frequency.QuadPart;
        if (0.0 < seconds)
        {
            megabytes_per_second = megabytes / seconds;
        }
    }
    (void)fprintf(stderr,
     "server: %.*s %.*s -> %u, sent %llu bytes from offset %llu in %.0f ms = %.1f MB/s%s\n",
     (int32_t)head->method.byteCount,
     head->method.data,
     (int32_t)head->target.byteCount,
     head->target.data,
     (uint32_t)response->status,
     (unsigned long long)sent,
     (unsigned long long)response->interval.offset,
     seconds * TRANSFER_MILLISECONDS,
     megabytes_per_second,
     response->growing ? " growing" : "");
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The current file extent bounds partial output without claiming that conversion has finished.
 */
static FORCE_INLINE void ReadGrowingResponse(const Connection_t*       connection,
                                             const CastHttp_Request_t* request,
                                             CastHttp_Response_t*      response)
{
    LARGE_INTEGER          size;
    bool_t                 sized;
    bool_t                 valid_size;
    CastHttp_RangeResult_t range;
    CastHttp_InitializeResponse(response, UTL_HTTP_STATUS_OK);
    response->contentType = GetContentType(connection);
    response->growing     = TRUE;
    if (UTL_HTTP_NO_FIELD != request->rangeField)
    {
        size.QuadPart = 0;
        sized         = GetFileSizeEx(connection->hFile, &size);
        valid_size    = sized && (0 <= size.QuadPart);
        if (valid_size)
        {
            response->representationBytes = (uint64_t)size.QuadPart;
            range                         = CastHttp_GetRange(request, response->representationBytes, &response->interval);
            if (CAST_HTTP_RANGE_PARTIAL == range)
            {
                response->status                   = UTL_HTTP_STATUS_PARTIAL_CONTENT;
                response->growing                  = FALSE;
                response->rangeCompleteLengthKnown = FALSE;
            }
            else if (CAST_HTTP_RANGE_UNSATISFIABLE == range)
            {
                response->status  = UTL_HTTP_STATUS_RANGE_NOT_SATISFIABLE;
                response->growing = FALSE;
            }
        }
        else
        {
            CastHttp_InitializeResponse(response, UTL_HTTP_STATUS_SERVER_ERROR);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A partial snapshot ends at its declared Content-Length; unbounded output follows the live file end.
 */
static FORCE_INLINE bool_t HandleGrowingRequest(Connection_t* connection, const CastHttp_Request_t* request)
{
    CastHttp_Response_t response;
    uint64_t            body_bytes;
    bool_t              sent;
    bool_t              has_body;
    int32_t             complete;
    ReadGrowingResponse(connection, request, &response);
    body_bytes = 0;
    sent       = SendResponse(connection, &response);
    has_body   = sent && (!request->headOnly);
    if (has_body && response.growing)
    {
        body_bytes = StreamGrowingFile(connection, 0);
    }
    else if (has_body && (UTL_HTTP_STATUS_PARTIAL_CONTENT == response.status))
    {
        body_bytes = StreamFile(connection, response.interval.offset, response.interval.byteCount, &complete);
    }
    if (sent)
    {
        LogResponse(connection, request, &response, body_bytes);
    }
    return FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only a completely sent bodyless or fixed-length response permits another request on this connection.
 */
static FORCE_INLINE bool_t HandleRequest(Connection_t* connection, const CastHttp_Request_t* request)
{
    CastHttp_Response_t    response;
    CastHttp_RangeResult_t range;
    bool_t                 sent;
    uint64_t               body_bytes;
    bool_t                 has_body;
    int32_t                complete;
    bool_t                 keep_alive;
    CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_OK);
    response.contentType         = GetContentType(connection);
    response.representationBytes = connection->size;
    response.keepAlive           = request->keepAlive;
    range                        = CastHttp_GetRange(request, connection->size, &response.interval);
    if (CAST_HTTP_RANGE_UNSATISFIABLE == range)
    {
        response.status = UTL_HTTP_STATUS_RANGE_NOT_SATISFIABLE;
    }
    else if (CAST_HTTP_RANGE_PARTIAL == range)
    {
        response.status = UTL_HTTP_STATUS_PARTIAL_CONTENT;
    }
    sent       = SendResponse(connection, &response);
    body_bytes = 0;
    has_body   = (!request->headOnly) && (CAST_HTTP_RANGE_UNSATISFIABLE != range) && (0 < response.interval.byteCount);
    if (sent && has_body)
    {
        body_bytes = StreamFile(connection, response.interval.offset, response.interval.byteCount, &complete);
        sent       = (0 != complete);
    }
    LogResponse(connection, request, &response, body_bytes);
    keep_alive = sent && request->keepAlive;
    return keep_alive;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Subtitle and file responses share counted range and persistence policy, including HEAD ignoring Range.
 */
static FORCE_INLINE bool_t HandleSubtitleRequest(Connection_t* connection, const CastHttp_Request_t* request)
{
    const SubtitleData_t*  subtitles;
    bool_t                 keep_alive;
    CastHttp_Response_t    response;
    CastHttp_RangeResult_t range;
    bool_t                 sent;
    uint64_t               body_bytes;
    bool_t                 has_body;
    subtitles  = connection->subtitles;
    keep_alive = FALSE;
    if (NULL == subtitles)
    {
        (void)SendErrorResponse(connection, UTL_HTTP_STATUS_NOT_FOUND);
    }
    else
    {
        CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_OK);
        response.contentType.data      = (const byte_t*)SUBTITLE_CONTENT_TYPE;
        response.contentType.byteCount = sizeof(SUBTITLE_CONTENT_TYPE) - 1;
        response.representationBytes   = subtitles->size;
        response.keepAlive             = request->keepAlive;
        range                          = CastHttp_GetRange(request, subtitles->size, &response.interval);
        if (CAST_HTTP_RANGE_UNSATISFIABLE == range)
        {
            response.status = UTL_HTTP_STATUS_RANGE_NOT_SATISFIABLE;
        }
        else if (CAST_HTTP_RANGE_PARTIAL == range)
        {
            response.status = UTL_HTTP_STATUS_PARTIAL_CONTENT;
        }
        sent       = SendResponse(connection, &response);
        body_bytes = 0;
        has_body =
            (!request->headOnly) && (CAST_HTTP_RANGE_UNSATISFIABLE != range) && (0 < response.interval.byteCount);
        if (sent && has_body)
        {
            sent = SendAll(connection->socket, subtitles->data + response.interval.offset, response.interval.byteCount);
            if (sent)
            {
                body_bytes = response.interval.byteCount;
            }
        }
        LogResponse(connection, request, &response, body_bytes);
        keep_alive = sent && request->keepAlive;
    }
    return keep_alive;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only newly received bytes are scanned; body-bearing requests are rejected before pipelined bytes can be interpreted.
 */
static FORCE_INLINE bool_t ReadRequest(Connection_t* connection)
{
    uint64_t          started;
    CastHttp_Result_t result;
    bool_t            active;
    uint64_t          now;
    bool_t            within_deadline;
    uint32_t          available;
    int32_t           selected;
    bool_t            stopping;
    int32_t           received;
    started = GetTickCount64();
    result  = CastHttp_ReadRequest(&connection->http, connection->request, connection->requestUsed);
    active  = !OS_Atomic_Load32(&connection->server->stopping);
    for (; active && (CAST_HTTP_INCOMPLETE == result);)
    {
        now             = GetTickCount64();
        within_deadline = ((now - started) < REQUEST_IDLE_LIMIT_MS);
        if (!within_deadline)
        {
            active = FALSE;
            break;
        }
        available = REQUEST_CAPACITY - connection->requestUsed;
        if (0 == available)
        {
            (void)SendErrorResponse(connection, UTL_HTTP_STATUS_HEADERS_TOO_LARGE);
            active = FALSE;
        }
        else
        {
            selected = PollRead(connection->socket);
            stopping = !!OS_Atomic_Load32(&connection->server->stopping);
            active   = (SOCKET_ERROR != selected) && (!stopping);
            if (active && (0 < selected))
            {
                received =
                    recv(connection->socket, (char_t*)connection->request + connection->requestUsed, (int32_t)available, 0);
                active = (0 < received) && ((uint32_t)received <= available);
                if (active)
                {
                    connection->requestUsed += (uint32_t)received;
                    result                  = CastHttp_ReadRequest(&connection->http, connection->request, connection->requestUsed);
                }
            }
        }
    }
    if (CAST_HTTP_REJECTED == result)
    {
        (void)SendErrorResponse(connection, connection->http.errorStatus);
        active = FALSE;
    }
    return active;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No dispatch inspects raw method prefixes or fields outside the validated head.
 */
static FORCE_INLINE bool_t ServeRequest(Connection_t* connection)
{
    const CastHttp_Request_t* request;
    bool_t                    keep_alive;
    CastHttp_Response_t       response;
    request = &connection->http;
    LOGASSERT(CAST_HTTP_READY == request->result);
    connection->responseStart.QuadPart = 0;
    connection->responseTimed          = QueryPerformanceCounter(&connection->responseStart);
    keep_alive                         = FALSE;
    if (request->options)
    {
        CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_NO_CONTENT);
        response.keepAlive = request->keepAlive;
        keep_alive         = SendResponse(connection, &response);
        keep_alive         = keep_alive && request->keepAlive;
    }
    else if (request->subtitles)
    {
        keep_alive = HandleSubtitleRequest(connection, request);
    }
    else if (TEXT('\0') == connection->file[0])
    {
        (void)SendErrorResponse(connection, UTL_HTTP_STATUS_NOT_FOUND);
    }
    else if (INVALID_HANDLE_VALUE == connection->hFile)
    {
        (void)SendErrorResponse(connection, UTL_HTTP_STATUS_SERVER_ERROR);
    }
    else if (NULL != connection->live)
    {
        keep_alive = HandleGrowingRequest(connection, request);
    }
    else
    {
        keep_alive = HandleRequest(connection, request);
    }
    return keep_alive;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Persistent input storage is retired only after response completion; unread pipelined bytes remain intact.
 */
static FORCE_INLINE void ServeConnection(void* parameter)
{
    Connection_t* connection;
    BOOL          no_delay;
    int32_t       send_buffer;
    bool_t        active;
    uint32_t      consumed;
    connection              = (Connection_t*)parameter;
    no_delay                = TRUE;
    send_buffer             = SEND_BUFFER_SIZE;
    connection->hFile       = INVALID_HANDLE_VALUE;
    connection->requestUsed = 0;
    CastHttp_InitializeRequest(&connection->http, connection->fields, REQUEST_FIELD_CAPACITY);
    active = (0 == setsockopt(connection->socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&no_delay, sizeof(no_delay)));
    if (active)
    {
        active =
            (0 ==
             setsockopt(connection->socket, SOL_SOCKET, SO_SNDBUF, (const char*)&send_buffer, sizeof(send_buffer)));
    }
    if (active && (TEXT('\0') != connection->file[0]))
    {
        connection->hFile = CreateFile(connection->file,
                                       GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                       NULL,
                                       OPEN_EXISTING,
                                       FILE_FLAG_SEQUENTIAL_SCAN,
                                       NULL);
    }
    for (; active;)
    {
        active = ReadRequest(connection);
        if (active)
        {
            active = ServeRequest(connection);
        }
        if (active)
        {
            consumed = connection->http.reader.head.byteCount;
            LOGASSERT(consumed <= connection->requestUsed);
            connection->requestUsed -= consumed;
            CRT_Memory_Move(connection->request, connection->request + consumed, connection->requestUsed);
            CastHttp_InitializeRequest(&connection->http, connection->fields, REQUEST_FIELD_CAPACITY);
        }
    }
    /* OS_Thread publishes completion; admission retains and releases native identities. */
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only the bounded adapter reads a terminated string. The protocol module
 *  validates all counted syntax before the native socket address is constructed.
 */
static FORCE_INLINE bool_t ReadBindAddress(const char_t* text, uint32_t* address)
{
    bool_t         valid;
    uint32_t       count;
    Utl_Url_Text_t source;

    LOGASSERT(NULL != address);
    valid = (NULL == text);
    if (valid)
    {
        (*address) = INADDR_ANY;
    }
    else
    {
        count = 0;
        for (; count <= UTL_URL_IPV4_MAX_BYTES; ++count)
        {
            if (UTL_URL_CHAR_NUL == text[count])
            {
                break;
            }
        }
        source.data      = (const byte_t*)text;
        source.byteCount = count;
        valid            = Utl_Url_ReadIpv4(source, address);
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The pool error survives successful cleanup and slot reuse.
 */
static FORCE_INLINE void RecordError(MediaServer_Storage_t* storage, uint32_t code)
{
    uint32_t error;
    error = code;
    if (ERROR_SUCCESS == error)
    {
        error = ERROR_GEN_FAILURE;
    }
    (void)OS_Atomic_CompareExchange32(&storage->error, 0, error);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Capacity includes the final terminator.
 */
static FORCE_INLINE void WriteError(char_t* output, uint32_t capacity, const char_t* text, uint32_t count)
{
    uint32_t bytes;
    if (0 < capacity)
    {
        bytes = capacity - 1;
        if (count < bytes)
        {
            bytes = count;
        }
        CRT_Memory_Copy(output, text, bytes);
        output[bytes] = ERROR_TEXT_NUL;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Published slot retirement also acquires completion of all native client cleanup.
 */
static FORCE_INLINE bool_t IsPoolQuiescent(const MediaServer_Storage_t* storage)
{
    bool_t   quiescent;
    uint32_t index;
    uint32_t state;

    quiescent = TRUE;
    for (index = 0; index < MEDIA_SERVER_CAPACITY; ++index)
    {
        state     = OS_Atomic_Load32(&storage->servers[index].state);
        quiescent = quiescent && ((SERVER_FREE == state) || (SERVER_STOPPED == state));
    }
    return quiescent;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No socket is accepted until one fixed slot can retain it.
 */
static FORCE_INLINE Connection_t* FindConnection(MediaServer_Storage_t* storage)
{
    Connection_t* result;
    uint32_t      index;
    Connection_t* connection;
    bool_t        ready;
    result = NULL;
    for (index = 0; index < MEDIA_SERVER_CLIENT_CAPACITY; ++index)
    {
        connection = &storage->connections[index];
        ready      = (!connection->busy) && (NULL == connection->server) &&
                     (OS_THREAD_READY == OS_Thread_GetState(&connection->worker));
        if (ready)
        {
            result = connection;
            break;
        }
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  FREE publication follows successful listener/client retirement.
 */
static FORCE_INLINE MediaServer* ReserveServer(MediaServer_Storage_t* storage)
{
    MediaServer* result;
    uint32_t     index;
    MediaServer* server;
    uint32_t     previous;
    result = NULL;
    for (index = 0; index < MEDIA_SERVER_CAPACITY; ++index)
    {
        server   = &storage->servers[index];
        previous = OS_Atomic_CompareExchange32(&server->state, SERVER_FREE, SERVER_RESERVED);
        if (SERVER_FREE == previous)
        {
            result = server;
            break;
        }
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each resource receives at most one release attempt per invocation.
 */
static FORCE_INLINE bool_t TryCloseConnectionResources(MediaServer_Storage_t* storage, Connection_t* connection)
{
    bool_t   released;
    uint32_t error;
    bool_t   closed;

    if (INVALID_HANDLE_VALUE != connection->hFile)
    {
        released = CloseHandle(connection->hFile);
        if (released)
        {
            connection->hFile = INVALID_HANDLE_VALUE;
        }
        else
        {
            error = GetLastError();
            RecordError(storage, error);
        }
    }
    if (INVALID_SOCKET != connection->socket)
    {
        released = (0 == closesocket(connection->socket));
        if (released)
        {
            connection->socket = INVALID_SOCKET;
        }
        else
        {
            error = (uint32_t)WSAGetLastError();
            RecordError(storage, error);
        }
    }
    closed = (INVALID_HANDLE_VALUE == connection->hFile) && (INVALID_SOCKET == connection->socket);
    return closed;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Callback acquisition precedes native release; both precede ending the server borrow.
 */
static FORCE_INLINE void RetireConnection(MediaServer_Storage_t* storage, Connection_t* connection)
{
    bool_t complete;
    bool_t failed;
    bool_t can_retire;
    bool_t closed;
    if (connection->busy)
    {
        complete = OS_Thread_TakeResult(&connection->worker);
        failed   = (OS_THREAD_FAILED == OS_Thread_GetState(&connection->worker));
        if (failed)
        {
            RecordError(storage, OS_Thread_GetError(&connection->worker));
        }
        if (complete || failed)
        {
            connection->busy = FALSE;
        }
    }
    can_retire = (!connection->busy) && (NULL != connection->server);
    if (can_retire)
    {
        closed = TryCloseConnectionResources(storage, connection);
        if (closed)
        {
            LOGASSERT(0 < connection->server->clients);
            --connection->server->clients;
            connection->server    = NULL;
            connection->live      = NULL;
            connection->subtitles = NULL;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The fixed scan advances each client lifetime once.
 */
static FORCE_INLINE void RetireConnections(MediaServer_Storage_t* storage)
{
    uint32_t index;
    for (index = 0; index < MEDIA_SERVER_CLIENT_CAPACITY; ++index)
    {
        RetireConnection(storage, &storage->connections[index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No client allocation or thread creation occurs during admission.
 */
static FORCE_INLINE void StartClient(MediaServer* server, Connection_t* connection, SOCKET client)
{
    u_long   blocking;
    bool_t   configured;
    uint32_t error;
    connection->server    = server;
    connection->socket    = client;
    connection->hFile     = INVALID_HANDLE_VALUE;
    connection->size      = server->size;
    connection->live      = server->live;
    connection->subtitles = server->subtitles;
    connection->busy      = FALSE;
    ++server->clients;
    CRT_Memory_Copy(connection->file, server->file, sizeof(connection->file));
    CRT_Memory_Copy(connection->contentType, server->contentType, sizeof(connection->contentType));
    blocking   = 0;
    configured = (0 == ioctlsocket(client, (int32_t)FIONBIO, &blocking));
    if (configured)
    {
        connection->busy = OS_Thread_Submit(&connection->worker);
        if (!connection->busy)
        {
            error = OS_Thread_GetError(&connection->worker);
            RecordError(server->storage, error);
        }
    }
    else
    {
        error = (uint32_t)WSAGetLastError();
        RecordError(server->storage, error);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  WSAEWOULDBLOCK is ordinary absence of work, not a retained failure.
 */
static FORCE_INLINE void PollListener(MediaServer* server)
{
    Connection_t* connection;
    SOCKET        client;
    uint32_t      error;
    connection = FindConnection(server->storage);
    if (NULL != connection)
    {
        client = accept(server->listener, NULL, NULL);
        if (INVALID_SOCKET != client)
        {
            StartClient(server, connection, client);
        }
        else
        {
            error = (uint32_t)WSAGetLastError();
            if (WSAEWOULDBLOCK != error)
            {
                RecordError(server->storage, error);
                OS_Atomic_Store32(&server->stopping, TRUE);
                OS_Atomic_Store32(&server->state, SERVER_STOPPING);
            }
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Failed starts have no returned owner and may become FREE after native cleanup.
 */
static FORCE_INLINE void RetireListener(MediaServer* server)
{
    bool_t   released;
    uint32_t error;
    bool_t   closed;
    if (INVALID_SOCKET != server->listener)
    {
        released = (0 == closesocket(server->listener));
        if (released)
        {
            server->listener = INVALID_SOCKET;
        }
        else
        {
            error = (uint32_t)WSAGetLastError();
            RecordError(server->storage, error);
        }
    }
    closed = (INVALID_SOCKET == server->listener) && (0 == server->clients);
    if (closed)
    {
        if (server->abandoned)
        {
            OS_Atomic_Store32(&server->state, SERVER_FREE);
        }
        else
        {
            OS_Atomic_Store32(&server->state, SERVER_STOPPED);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Recovery never races admission. Native cleanup on this exceptional owner path
 *  remains a timing boundary; normal stop polls perform no native cleanup.
 */
static FORCE_INLINE void RecoverAdmission(MediaServer_Storage_t* storage)
{
    bool_t       failed;
    uint32_t     previous;
    bool_t       acquired;
    uint32_t     error;
    uint32_t     index;
    MediaServer* server;
    uint32_t     state;
    failed = (OS_THREAD_FAILED == OS_Thread_GetState(storage->admission));
    if (failed)
    {
        previous = OS_Atomic_CompareExchange32(&storage->recovery, FALSE, TRUE);
        acquired = (0 == previous);
        if (acquired)
        {
            error = OS_Thread_GetError(storage->admission);
            RecordError(storage, error);
            OS_Atomic_Store32(&storage->closing, TRUE);
            RetireConnections(storage);
            for (index = 0; index < MEDIA_SERVER_CAPACITY; ++index)
            {
                server = &storage->servers[index];
                state  = OS_Atomic_Load32(&server->state);
                if (SERVER_ACTIVE == state)
                {
                    OS_Atomic_Store32(&server->stopping, TRUE);
                    OS_Atomic_Store32(&server->liveState.complete, TRUE);
                    OS_Atomic_Store32(&server->state, SERVER_STOPPING);
                    state = SERVER_STOPPING;
                }
                if (SERVER_STOPPING == state)
                {
                    RetireListener(server);
                }
            }
            OS_Atomic_Store32(&storage->recovery, FALSE);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Native accept is nonblocking; response/file I/O on client workers remains blocking.
 *  The idle sleep is a scheduling boundary requiring later latency measurement.
 */
static FORCE_INLINE void AcceptLoop(void* context)
{
    MediaServer_Workspace_t* workspace;
    MediaServer_Storage_t*   storage;
    uint32_t                 first;
    bool_t                   closing;
    bool_t                   quiescent;
    uint32_t                 index;
    uint32_t                 slot;
    MediaServer*             server;
    uint32_t                 state;
    workspace = (MediaServer_Workspace_t*)context;
    storage   = workspace->storage;
    first     = 0;
    for (;;)
    {
        RetireConnections(storage);
        closing   = !!OS_Atomic_Load32(&storage->closing);
        quiescent = TRUE;
        for (index = 0; index < MEDIA_SERVER_CAPACITY; ++index)
        {
            slot   = (first + index) % MEDIA_SERVER_CAPACITY;
            server = &storage->servers[slot];
            state  = OS_Atomic_Load32(&server->state);
            if (closing && (SERVER_ACTIVE == state))
            {
                OS_Atomic_Store32(&server->stopping, TRUE);
                OS_Atomic_Store32(&server->liveState.complete, TRUE);
                OS_Atomic_Store32(&server->state, SERVER_STOPPING);
                state = SERVER_STOPPING;
            }
            if (SERVER_ACTIVE == state)
            {
                PollListener(server);
            }
            else if (SERVER_STOPPING == state)
            {
                RetireListener(server);
            }
            state     = OS_Atomic_Load32(&server->state);
            quiescent = quiescent && ((SERVER_FREE == state) || (SERVER_STOPPED == state));
        }
        first = (first + 1) % MEDIA_SERVER_CAPACITY;
        if (closing && quiescent)
        {
            break;
        }
        Sleep(ACCEPT_IDLE_MS);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Native socket creation still occurs per start and remains an allocation-boundary defect.
 */
static FORCE_INLINE bool_t OpenListener(MediaServer* server, const ServerEndpoint_t* endpoint)
{
    bool_t             opened;
    BOOL               exclusive;
    u_long             nonblocking;
    struct sockaddr_in address;
    int32_t            address_length;
    uint32_t           error;
    server->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    opened           = (INVALID_SOCKET != server->listener);
    if (opened)
    {
        exclusive = TRUE;
        opened    = (0 == setsockopt(server->listener,
                     SOL_SOCKET,
                     SO_EXCLUSIVEADDRUSE,
                     (const char_t*)&exclusive,
                     sizeof(exclusive)));
    }
    if (opened)
    {
        nonblocking = 1;
        opened      = (0 == ioctlsocket(server->listener, (int32_t)FIONBIO, &nonblocking));
    }
    if (opened)
    {
        CRT_Memory_Set(&address, 0, sizeof(address));
        address.sin_family      = AF_INET;
        address.sin_port        = htons(endpoint->port);
        address.sin_addr.s_addr = htonl(endpoint->address);
        opened                  = (0 == bind(server->listener, (struct sockaddr*)&address, sizeof(address)));
    }
    if (opened)
    {
        opened = (0 == listen(server->listener, SOMAXCONN));
    }
    if (opened)
    {
        address_length = sizeof(address);
        opened         = (0 == getsockname(server->listener, (struct sockaddr*)&address, &address_length));
    }
    if (opened)
    {
        server->port = ntohs(address.sin_port);
    }
    else
    {
        error = (uint32_t)WSAGetLastError();
        RecordError(server->storage, error);
    }
    return opened;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Growing completion and subtitle metadata belong to the retained listener.
 */
static FORCE_INLINE void PrepareRepresentation(MediaServer* server, const ServerRepresentation_t* representation)
{
    LOGASSERT(INVALID_SOCKET == server->listener);
    LOGASSERT(0 == server->clients);
    server->abandoned = FALSE;
    server->file[0]   = TEXT('\0');
    if (NULL != representation->file)
    {
        CRT_Memory_Copy(server->file, representation->file, (uint64_t)representation->fileUnits * sizeof(uint16_t));
        server->file[representation->fileUnits] = TEXT('\0');
    }
    CRT_Memory_Copy(server->contentType, representation->contentType, representation->contentTypeBytes);
    server->contentType[representation->contentTypeBytes] = ERROR_TEXT_NUL;
    server->size                                          = representation->fileBytes;
    server->live                                          = NULL;
    server->subtitles                                     = NULL;
    OS_Atomic_Store32(&server->stopping, FALSE);
    OS_Atomic_Store32(&server->liveState.complete, FALSE);
    if (representation->growing)
    {
        server->live = &server->liveState;
    }
    if (0 < representation->subtitleBytes)
    {
        server->subtitleData.data = representation->subtitles;
        server->subtitleData.size = representation->subtitleBytes;
        server->subtitles         = &server->subtitleData;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Native file queries remain a runtime latency/provider boundary.
 */
static FORCE_INLINE bool_t ReadRepresentationFile(ServerRepresentation_t* representation,
                                                  MediaServer_Storage_t*  storage)
{
    bool_t                    accessible;
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    uint32_t                  error;
    accessible = TRUE;
    if (NULL == representation->file)
    {
        representation->fileBytes = 0;
    }
    else
    {
        accessible = GetFileAttributesEx((LPCTSTR)representation->file, GetFileExInfoStandard, &attributes);
        if (accessible)
        {
            if (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            {
                accessible = FALSE;
                RecordError(storage, ERROR_DIRECTORY);
            }
            else
            {
                representation->fileBytes =
                    ((uint64_t)attributes.nFileSizeHigh << FILE_SIZE_HIGH_BITS) | attributes.nFileSizeLow;
            }
        }
        else
        {
            error = GetLastError();
            RecordError(storage, error);
        }
    }
    return accessible;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only a completely configured listener publishes its representation to admission.
 */
static FORCE_INLINE MediaServer* StartInternal(MediaServer_Workspace_t* workspace,
                                               const uint16_t*          file,
                                               const char_t*            bindAddress,
                                               uint16_t                 port,
                                               const char_t*            contentType,
                                               bool_t                   growing,
                                               const byte_t*            vtt,
                                               uint64_t                 vttSize,
                                               char_t*                  error,
                                               uint32_t                 errorCapacity)
{
    MediaServer*           result;
    bool_t                 ready;
    bool_t                 valid_bind;
    uint32_t               bind_address;
    uint32_t               type_bytes;
    uint32_t               file_units;
    bool_t                 valid_file;
    bool_t                 fits_subtitles;
    bool_t                 valid;
    ServerRepresentation_t representation;
    ServerEndpoint_t       endpoint;
    bool_t                 accessible;
    MediaServer*           server;
    bool_t                 opened;
    LOGASSERT(NULL != workspace);
    LOGASSERT(NULL != contentType);
    LOGASSERT((0 == errorCapacity) || (NULL != error));
    LOGASSERT((0 == vttSize) || (NULL != vtt));
    result     = NULL;
    ready      = MediaServer_IsReady(workspace);
    valid_bind = ReadBindAddress(bindAddress, &bind_address);
    type_bytes = Utl_Unicode_GetUtf8StringByteCount(contentType, CONTENT_TYPE_CAPACITY);
    file_units = 0;
    valid_file = TRUE;
    if (NULL != file)
    {
        file_units = Utl_Unicode_GetUtf16StringUnitCount(file, MAX_PATH);
        valid_file = (0 < file_units) && (MAX_PATH > file_units);
        if (valid_file)
        {
            valid_file = Utl_Unicode_IsValidUtf16(file, file_units);
        }
    }
    fits_subtitles = (UINTPTR_MAX >= vttSize);
    valid          = valid_bind && valid_file && fits_subtitles && (0 < type_bytes) && (CONTENT_TYPE_CAPACITY > type_bytes);
    if (!ready)
    {
        WriteError(error, errorCapacity, ERROR_SERVER_UNREADY, sizeof(ERROR_SERVER_UNREADY) - 1);
    }
    else if (!valid_bind)
    {
        WriteError(error, errorCapacity, ERROR_BIND_ADDRESS, sizeof(ERROR_BIND_ADDRESS) - 1);
    }
    else if (!valid)
    {
        WriteError(error, errorCapacity, ERROR_SERVER_INPUT, sizeof(ERROR_SERVER_INPUT) - 1);
    }
    else
    {
        representation.file             = file;
        representation.fileUnits        = file_units;
        representation.contentType      = contentType;
        representation.contentTypeBytes = type_bytes;
        representation.growing          = growing;
        representation.subtitles        = vtt;
        representation.subtitleBytes    = vttSize;
        endpoint.address                = bind_address;
        endpoint.port                   = port;
        accessible                      = ReadRepresentationFile(&representation, workspace->storage);
        server                          = NULL;
        if (accessible)
        {
            server = ReserveServer(workspace->storage);
        }
        if (!accessible)
        {
            WriteError(error, errorCapacity, ERROR_SERVER_INPUT, sizeof(ERROR_SERVER_INPUT) - 1);
        }
        else if (NULL == server)
        {
            WriteError(error, errorCapacity, ERROR_SERVER_CAPACITY, sizeof(ERROR_SERVER_CAPACITY) - 1);
        }
        else
        {
            PrepareRepresentation(server, &representation);
            opened = OpenListener(server, &endpoint);
            if (opened)
            {
                result = server;
                OS_Atomic_Store32(&server->state, SERVER_ACTIVE);
            }
            else
            {
                server->abandoned         = TRUE;
                server->subtitles         = NULL;
                server->subtitleData.data = NULL;
                OS_Atomic_Store32(&server->state, SERVER_STOPPING);
                WriteError(error, errorCapacity, ERROR_SERVER_START, sizeof(ERROR_SERVER_START) - 1);
            }
        }
    }
    return result;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Every client worker is initialized even after a sibling failure. Admission
 *  begins only after the complete pool is ready; partial owners remain closable.
 */
bool_t MediaServer_Initialize(MediaServer_Workspace_t* workspace)
{
    bool_t        memory_ready;
    bool_t        clients_ready;
    uint32_t      index;
    MediaServer*  server;
    Connection_t* connection;
    bool_t        worker_ready;
    bool_t        admission_ready;
    LOGASSERT(NULL != workspace);
    workspace->storage = NULL;
    workspace->ready   = FALSE;
    workspace->error   = 0;
    memory_ready       = OS_Memory_Initialize(&workspace->memory, sizeof(MediaServer_Storage_t));
    clients_ready      = memory_ready;
    if (memory_ready)
    {
        workspace->storage            = (MediaServer_Storage_t*)OS_Memory_GetAddress(&workspace->memory);
        workspace->storage->admission = &workspace->admission;
        OS_Atomic_Initialize32(&workspace->storage->recovery, FALSE);
        OS_Atomic_Initialize32(&workspace->storage->closing, FALSE);
        OS_Atomic_Initialize32(&workspace->storage->error, 0);
        for (index = 0; index < MEDIA_SERVER_CAPACITY; ++index)
        {
            server           = &workspace->storage->servers[index];
            server->storage  = workspace->storage;
            server->listener = INVALID_SOCKET;
            server->clients  = 0;
            OS_Atomic_Initialize32(&server->state, SERVER_FREE);
            OS_Atomic_Initialize32(&server->stopping, FALSE);
            OS_Atomic_Initialize32(&server->liveState.complete, FALSE);
        }
        for (index = 0; index < MEDIA_SERVER_CLIENT_CAPACITY; ++index)
        {
            connection         = &workspace->storage->connections[index];
            connection->server = NULL;
            connection->socket = INVALID_SOCKET;
            connection->hFile  = INVALID_HANDLE_VALUE;
            connection->busy   = FALSE;
            worker_ready       = OS_Thread_Initialize(&connection->worker, ServeConnection, connection);
            if (!worker_ready)
            {
                RecordError(workspace->storage, OS_Thread_GetError(&connection->worker));
            }
            clients_ready = clients_ready && worker_ready;
        }
    }
    admission_ready  = OS_Thread_Initialize(&workspace->admission, AcceptLoop, workspace);
    workspace->ready = memory_ready && clients_ready && admission_ready;
    if (workspace->ready)
    {
        workspace->ready = OS_Thread_Submit(&workspace->admission);
    }
    return workspace->ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Native availability is acquired without inspecting callback-owned metadata.
 */
bool_t MediaServer_IsReady(const MediaServer_Workspace_t* workspace)
{
    bool_t ready;
    LOGASSERT(NULL != workspace);
    ready = workspace->ready && (OS_THREAD_RUNNING == OS_Thread_GetState(&workspace->admission));
    return ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Resource errors survive closure; pool-native evidence is retained separately.
 */
uint32_t MediaServer_GetError(const MediaServer_Workspace_t* workspace)
{
    uint32_t error;
    LOGASSERT(NULL != workspace);
    error = workspace->error;
    if (0 == error)
    {
        error = OS_Memory_GetError(&workspace->memory);
    }
    if (0 == error)
    {
        error = OS_Thread_GetError(&workspace->admission);
    }
    if ((0 == error) && (NULL != workspace->storage))
    {
        error = OS_Atomic_Load32(&workspace->storage->error);
    }
    return error;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Matches known extensions without case sensitivity and falls back to
 *  application/octet-stream.
 */
const char* MediaServer_GetContentTypeForExtension(const uint16_t* pExtension)
{
    static const struct
    {
        const wchar_t* pExtension;
        const char*    pType;
    } kTypes[] = {
        {L".mp4", "video/mp4"},
        {L".m4v", "video/mp4"},
        {L".mov", "video/quicktime"},
        {L".webm", "video/webm"},
        {L".mkv", "video/x-matroska"},
        {L".mp3", "audio/mpeg"},
        {L".m4a", "audio/mp4"},
        {L".aac", "audio/aac"},
        {L".flac", "audio/flac"},
        {L".wav", "audio/wav"},
        {L".ogg", "audio/ogg"},
        {L".jpg", "image/jpeg"},
        {L".jpeg", "image/jpeg"},
        {L".png", "image/png"},
        {L".gif", "image/gif"},
    };
    uint32_t index;

    if (NULL != pExtension)
    {
        for (index = 0; index < sizeof(kTypes) / sizeof(kTypes[0]); ++index)
        {
            if (0 == _wcsicmp(pExtension, kTypes[index].pExtension))
            {
                return kTypes[index].pType;
            }
        }
    }
    return "application/octet-stream";
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint16_t MediaServer_GetPort(const MediaServer* pServer)
{
    LOGASSERT(NULL != pServer);
    return pServer->port;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Validates the complete range set in fixed caller-owned storage, then selects
 *  its first satisfiable member. RFC 9110 permits ignoring invalid or excessive
 *  sets and Range on empty representations; those cases return zero.
 */
int MediaServer_ParseRange(const char* pValue, uint64_t size, uint64_t* pFirst, uint64_t* pLast)
{
    Utl_Http_RangeSet_t      request;
    Utl_Http_Range_t         ranges[RANGE_MEMBER_CAPACITY];
    bool_t                   has_arguments;
    int                      result;
    uint32_t                 byte_count;
    bool_t                   is_valid;
    uint32_t                 index;
    Utl_Http_RangeInterval_t interval;

    LOGASSERT(NULL != pFirst);
    LOGASSERT(NULL != pLast);
    request.ranges   = ranges;
    request.capacity = sizeof(ranges) / sizeof(ranges[0]);
    request.count    = 0;
    has_arguments    = (NULL != pValue);
    result           = 0;
    if (has_arguments && (0 < size))
    {
        for (byte_count = 0; byte_count < REQUEST_CAPACITY; ++byte_count)
        {
            if ('\0' == pValue[byte_count])
            {
                break;
            }
        }
        is_valid = (byte_count < REQUEST_CAPACITY);
        if (is_valid)
        {
            is_valid = Utl_Http_ReadRange(&request, (const byte_t*)pValue, byte_count);
        }
        if (is_valid)
        {
            result = -1;
            for (index = 0; index < request.count; ++index)
            {
                if (Utl_Http_ResolveRange(&interval, &ranges[index], size))
                {
                    (*pFirst) = interval.offset;
                    (*pLast)  = interval.offset + interval.byteCount - 1;
                    result    = 1;
                    break;
                }
            }
        }
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
MediaServer* MediaServer_Start(MediaServer_Workspace_t* workspace,
                               const uint16_t*          pFile,
                               const char*              pBindAddress,
                               uint16_t                 port,
                               const char*              pContentType,
                               char*                    pError,
                               uint32_t                 errorCapacity)
{
    return StartInternal(workspace, pFile, pBindAddress, port, pContentType, 0, NULL, 0, pError, errorCapacity);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
MediaServer* MediaServer_StartWithSubtitles(MediaServer_Workspace_t* workspace,
                                            const uint16_t*          pFile,
                                            const char*              pBindAddress,
                                            uint16_t                 port,
                                            const char*              pContentType,
                                            const byte_t*              pVtt,
                                            uint64_t                 vttSize,
                                            char*                    pError,
                                            uint32_t                 errorCapacity)
{
    return StartInternal(workspace, pFile, pBindAddress, port, pContentType, 0, pVtt, vttSize, pError, errorCapacity);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
MediaServer* MediaServer_StartGrowing(MediaServer_Workspace_t* workspace,
                                      const uint16_t*          pFile,
                                      const char*              pBindAddress,
                                      uint16_t                 port,
                                      const char*              pContentType,
                                      char*                    pError,
                                      uint32_t                 errorCapacity)
{
    return StartInternal(workspace, pFile, pBindAddress, port, pContentType, 1, NULL, 0, pError, errorCapacity);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Atomically marks the live file complete so readers can finish after consuming
 *  its currently available bytes.
 */
void MediaServer_MarkComplete(MediaServer* pServer)
{
    if (NULL != pServer && NULL != pServer->live)
    {
        OS_Atomic_Store32(&pServer->live->complete, TRUE);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Admission closes native resources and acquires every client before STOPPED.
 *  Owner release publishes FREE only after all caller borrows have ended.
 */
bool_t MediaServer_TryStop(MediaServer** owner)
{
    MediaServer* server;
    bool_t       stopped;
    uint32_t     state;
    LOGASSERT(owner);
    server  = (*owner);
    stopped = (NULL == server);
    if (!stopped)
    {
        OS_Atomic_Store32(&server->stopping, TRUE);
        OS_Atomic_Store32(&server->liveState.complete, TRUE);
        (void)OS_Atomic_CompareExchange32(&server->state, SERVER_ACTIVE, SERVER_STOPPING);
        RecoverAdmission(server->storage);
        state   = OS_Atomic_Load32(&server->state);
        stopped = (SERVER_STOPPED == state);
        if (stopped)
        {
            server->subtitles         = NULL;
            server->live              = NULL;
            server->subtitleData.data = NULL;
            (*owner)                  = NULL;
            OS_Atomic_Store32(&server->state, SERVER_FREE);
        }
    }
    return stopped;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All public listener owners and concurrent start callers must be retired first.
 *  Admission exits after native cleanup; client threads close before storage.
 */
bool_t MediaServer_TryClose(MediaServer_Workspace_t* workspace)
{
    bool_t   quiescent;
    bool_t   admission_closed;
    bool_t   clients_closed;
    uint32_t index;
    bool_t   client_closed;
    uint32_t client_error;
    bool_t   closed;
    LOGASSERT(NULL != workspace);
    workspace->ready = FALSE;
    workspace->error = MediaServer_GetError(workspace);
    if (NULL != workspace->storage)
    {
        OS_Atomic_Store32(&workspace->storage->closing, TRUE);
    }
    if (NULL != workspace->storage)
    {
        RecoverAdmission(workspace->storage);
    }
    quiescent = TRUE;
    if (NULL != workspace->storage)
    {
        quiescent = IsPoolQuiescent(workspace->storage);
    }
    admission_closed = FALSE;
    if (quiescent)
    {
        admission_closed = OS_Thread_TryClose(&workspace->admission);
    }
    clients_closed = admission_closed;
    if (admission_closed && (NULL != workspace->storage))
    {
        for (index = 0; index < MEDIA_SERVER_CLIENT_CAPACITY; ++index)
        {
            client_closed = OS_Thread_TryClose(&workspace->storage->connections[index].worker);
            client_error  = OS_Thread_GetError(&workspace->storage->connections[index].worker);
            if (0 != client_error)
            {
                RecordError(workspace->storage, client_error);
            }
            clients_closed = clients_closed && client_closed;
        }
    }
    closed = FALSE;
    if (admission_closed && clients_closed)
    {
        workspace->error   = MediaServer_GetError(workspace);
        workspace->storage = NULL;
        closed             = OS_Memory_TryClose(&workspace->memory);
    }
    return closed;
}
