/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * SSDP wire handling uses utl_ssdp/utl_http and startup-owned native storage.
 * https://upnp.org/specs/arch/UPnP-arch-DeviceArchitecture-v1.1.pdf
 * https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options
 * URI components and HTTP origin-form targets use utl_url/utl_http.
 * Counted device XML and root-device display identity use utl_xml/utl_upnp.
 * WinHTTP connection/request creation and synchronous calls remain open lifetime/timing defects.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h> // IWYU pragma: keep
#include <winhttp.h>
#include <winerror.h>
#include <minwindef.h>
#include <winnt.h>
#include <errhandlingapi.h>
#include <sysinfoapi.h>
#include <inaddr.h>
#include <ws2ipdef.h>
#include <stdint.h>
#include "ssdp.h"
#include "utl/utl_ssdp.h"
#include "utl/utl_http.h"
#include "utl/utl_url.h"
#include "utl/utl_unicode.h"
#include "utl/utl_xml.h"
#include "utl/utl_upnp.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_network.h"
#include "os/os_memory.h"
#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define LOCATION_CAPACITY           (256)
#define DISCOVERY_IDENTITY_CAPACITY (256)
#define DISCOVERY_FIELD_CAPACITY    (32)
#define DISCOVERY_SEARCH_CAPACITY   (256)
#define DISCOVERY_RESPONSE_CAPACITY (2048)
#define DISCOVERY_DOCUMENT_CAPACITY (16384)
/* Each retained node needs at least two source bytes on average; attributes need at least five.
** These conservative bounds cover every document admitted by the byte buffer. */
#define DISCOVERY_XML_NODE_CAPACITY      (DISCOVERY_DOCUMENT_CAPACITY / 2)
#define DISCOVERY_XML_ATTRIBUTE_CAPACITY (DISCOVERY_DOCUMENT_CAPACITY / 4)
#define DISCOVERY_HOST_CAPACITY          (256)
#define DISCOVERY_PATH_CAPACITY          (512)
#define DISCOVERY_RESPONSE_SECONDS       (2)
#define DISCOVERY_SEARCH_ATTEMPTS        (2)
#define DISCOVERY_DRAIN_LIMIT            (256)
#define DISCOVERY_MS_PER_SECOND          (1000)
#define DISCOVERY_US_PER_MS              (1000)
#define DISCOVERY_HTTP_TIMEOUT_MS        (3000)
#define DISCOVERY_DIAL_TARGET            ("urn:dial-multiscreen-org:service:dial:1")
#define DISCOVERY_DIRECT_PREFIX          ("http://")
#define DISCOVERY_DIRECT_SUFFIX          (":8008/ssdp/device-desc.xml")
#define DISCOVERY_TERMINATOR             ('\0')

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Bounded discovery identity and its first reachable description URL.
 */
typedef struct CastDiscoveryLocation_s
{
    char_t   location[LOCATION_CAPACITY];           ///< Terminated description URL.
    byte_t   identity[DISCOVERY_IDENTITY_CAPACITY]; ///< Counted USN used across interfaces.
    uint32_t identityBytes;                         ///< Exact USN byte count.
} CastDiscoveryLocation_t;

/*************************************************************************************************/
/**
 * \brief Resident discovery buffers and retained native identities.
 *
 * \par Details:
 *  One wildcard-bound UDP socket receives unicast replies from every interface.
 *  IP_MULTICAST_IF selects the interface for each outbound search. The socket is
 *  nonblocking; refresh drains only a bounded number of stale queued datagrams.
 */
typedef struct Ssdp_State_s
{
    OS_Network_t            network;                                         ///< Startup-owned complete interface snapshot reader.
    SOCKET                  socket;                                          ///< Startup-owned wildcard-bound nonblocking search socket.
    struct sockaddr_in      group;                                           ///< SSDP multicast destination, prepared at startup.
    HINTERNET               session;                                         ///< HTTP session created once at startup.
    HINTERNET               connection;                                      ///< Retained until request/connection closure succeeds.
    HINTERNET               requestHandle;                                   ///< Current description request, including failed closure.
    uint32_t                requestBytes;                                    ///< Serialized M-SEARCH size.
    byte_t                  request[DISCOVERY_SEARCH_CAPACITY];              ///< Immutable search bytes after startup.
    byte_t                  response[DISCOVERY_RESPONSE_CAPACITY];           ///< Reused datagram input.
    Utl_Http_Field_t        fields[DISCOVERY_FIELD_CAPACITY];                ///< Reused HTTP field descriptors.
    CastDiscoveryLocation_t locations[CAST_MAX_DEVICES];                     ///< Deduplicated reply locations.
    uint32_t                locationCount;                                   ///< Number of initialized location records.
    byte_t                  document[DISCOVERY_DOCUMENT_CAPACITY];           ///< Reused bounded XML body storage.
    char_t                  directLocation[LOCATION_CAPACITY];               ///< Staged direct-host description URL.
    byte_t                  target[DISCOVERY_PATH_CAPACITY];                 ///< Encoded origin-form request bytes.
    WCHAR                   nativeHost[DISCOVERY_HOST_CAPACITY];             ///< WinHTTP fixed UTF-16 host boundary.
    WCHAR                   nativeTarget[DISCOVERY_PATH_CAPACITY];           ///< WinHTTP fixed UTF-16 target boundary.
    Utl_Xml_Node_t          xmlNodes[DISCOVERY_XML_NODE_CAPACITY];           ///< Retained document records.
    Utl_Xml_Attribute_t     xmlAttributes[DISCOVERY_XML_ATTRIBUTE_CAPACITY]; ///< Retained attributes.
    Utl_Xml_Document_t      xmlDocument;                                     ///< Reused counted XML document owner.
    uint32_t                documentBytes;                                   ///< Complete accepted body extent, excluding terminator.
    uint16_t                port;                                            ///< Validated explicit transport port.
    bool_t                  secure;                                          ///< HTTPS request flag.
} Ssdp_State_t;

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
 * \brief Retains the first failure in the current operation.
 *
 * \par Details:
 *  Failure evidence is retained.
 *
 * \par Requirements:
 *  - 001 The caller shall capture native error state before any subsequent native operation.
 *
 * \param[in,out] discovery   Initialized owner
 * \param[in]     code        Captured native or local failure code
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void RecordError(Ssdp_t* discovery, uint32_t code);

/*************************************************************************************************/
/**
 * \brief Captures a failed synchronous WinHTTP BOOL result immediately.
 *
 * \par Requirements:
 *  - 001 No native call shall intervene between the operation and this helper.
 *  - 002 Success shall not inspect stale thread error state.
 *
 * \param[in,out] discovery   Initialized owner
 * \param[in]     result      Native BOOL result
 *
 * \retval        TRUE    The native operation succeeded.
 * \retval        FALSE   The failure code has been retained.
 */
static FORCE_INLINE bool_t CheckHttpResult(Ssdp_t* discovery, BOOL result);

/*************************************************************************************************/
/**
 * \brief Releases the current HTTP request and connection without losing failed identities.
 *
 * \par Requirements:
 *  - 001 Connection release shall follow successful request release; session ownership shall remain unchanged.
 *
 * \param[in,out] discovery   Initialized owner with native state
 *
 * \retval        TRUE    Both request and connection are gone.
 * \retval        FALSE   A failed release remains retained for retry.
 */
static FORCE_INLINE bool_t CloseRequest(Ssdp_t* discovery);

/*************************************************************************************************/
/**
 * \brief Copies one complete simple-content field into terminated application storage.
 *
 * \par Requirements:
 *  - 001 Source shall be a validated simple-content element; output shall be disjoint with positive capacity.
 *  - 002 Insufficient capacity shall preserve all destination bytes.
 *  - 003 Normalized UTF-8 shall be complete before a final terminator is appended.
 *
 * \param[in]  element    Completed field node
 * \param[out] output     Application field storage
 * \param[in]  capacity   Bytes including the terminator
 *
 * \retval     TRUE    Complete text and terminator are published.
 * \retval     FALSE   Output capacity is insufficient.
 */
static FORCE_INLINE bool_t WriteDisplayText(const Utl_Xml_Node_t* element, char_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Validates the complete description before selecting root-device display fields.
 *
 * \par Requirements:
 *  - 001 A successful HTTP read shall have published documentBytes in the retained byte buffer.
 *  - 002 XML records shall use only startup-owned storage; malformed or ambiguous descriptions shall fail.
 *  - 003 Device shall remain private until Describe publishes it; failure may modify this scratch candidate.
 *
 * \param[in,out] discovery   Exclusive ready owner
 * \param[in,out] device      Private candidate with a validated host
 *
 * \retval        TRUE    Complete display fields are available.
 * \retval        FALSE   Candidate must not be published.
 */
static FORCE_INLINE bool_t ReadDescription(Ssdp_t* discovery, CastDevice* device);

/*************************************************************************************************/
/**
 * \brief Checks the decoded host supported by the native discovery transport.
 *
 * \par Requirements:
 *  - 001 Names shall contain ASCII letters, digits, hyphens and periods only.
 *  - 002 Address literals shall already have passed the URL grammar; IPvFuture is unsupported.
 *  - 003 Embedded zero, decoded delimiters and international names shall fail explicitly.
 *
 * \param[in] host        Counted decoded bytes
 * \param[in] byteCount   Complete readable extent
 * \param[in] kind        Validated generic host grammar
 *
 * \retval    TRUE    The host can be passed without reinterpretation of delimiters.
 * \retval    FALSE   Native transport support is missing for this host.
 */
static FORCE_INLINE bool_t IsNativeHost(const char_t* host, uint32_t byteCount, Utl_Url_HostKind_t kind);

/*************************************************************************************************/
/**
 * \brief Prepares a validated location in startup-owned native transport buffers.
 *
 * \par Requirements:
 *  - 001 LOCATION_CAPACITY shall bound readable terminated input.
 *  - 002 Invalid, unsupported or oversized locations shall fail without starting native I/O.
 *  - 003 Request queries and escapes shall be preserved; fragments shall be excluded.
 *  - 004 Explicit port zero shall fail instead of selecting WinHTTP's default-port sentinel.
 *  - 005 Output host and native buffers shall be scratch until TRUE is returned.
 *
 * \param[in,out] discovery   Ready exclusive owner
 * \param[in]     location    Bounded terminated location
 * \param[out]    device      Private unpublished candidate
 *
 * \retval        TRUE    Complete host, port and request target are ready.
 * \retval        FALSE   No native request may consume the prepared buffers.
 */
static FORCE_INLINE bool_t ReadLocation(Ssdp_t* discovery, const char_t* location, CastDevice* device);

/*************************************************************************************************/
/**
 * \brief Reads a complete bounded body from the current successful HTTP response.
 *
 * \par Requirements:
 *  - 001 A ready owner shall hold a request whose response headers were accepted.
 *  - 002 Truncation and failed reads shall clear the published document.
 *  - 003 Request lifetime shall remain owned by the caller.
 *
 * \param[in,out] discovery   Exclusive ready owner with an active response
 *
 * \retval        TRUE    A complete nonempty body is terminated in document storage.
 * \retval        FALSE   The document must not be consumed.
 */
static FORCE_INLINE bool_t ReadDocument(Ssdp_t* discovery);

/*************************************************************************************************/
/**
 * \brief Fetches a complete bounded description body using the retained HTTP session.
 *
 * \par Requirements:
 *  - 001 Truncation, non-success HTTP status, read failure or release failure shall not publish a usable body.
 *  - 002 The native host, port and target shall come from a successful ReadLocation.
 *
 * \param[in,out] discovery   Ready discovery owner
 *
 * \retval        TRUE    A complete nonempty body is terminated in document storage.
 * \retval        FALSE   The document must not be consumed.
 */
static FORCE_INLINE bool_t HttpGet(Ssdp_t* discovery);

/*************************************************************************************************/
/**
 * \brief Stages one description before publishing a receiver value.
 *
 * \par Requirements:
 *  - 001 Failure shall preserve the output; all inputs and output shall remain disjoint from workspace buffers.
 *
 * \param[in,out] discovery   Ready discovery owner
 * \param[in]     location    Terminated description URL
 * \param[out]    device      Caller receiver slot
 *
 * \retval        TRUE    A described receiver was published.
 * \retval        FALSE   Output is unchanged.
 */
static FORCE_INLINE bool_t Describe(Ssdp_t* discovery, const char_t* location, CastDevice* device);

/*************************************************************************************************/
/**
 * \brief Drains queued datagrams before a new discovery exchange.
 *
 * \par Requirements:
 *  - 001 At most DISCOVERY_DRAIN_LIMIT nonblocking reads shall occur; a continuously full queue shall fail.
 *
 * \param[in,out] discovery   Ready owner with a nonblocking socket
 *
 * \retval        TRUE    The queue was observed empty.
 * \retval        FALSE   Native failure or bounded drain exhaustion occurred.
 */
static FORCE_INLINE bool_t DrainSocket(Ssdp_t* discovery);

/*************************************************************************************************/
/**
 * \brief Sends the retained search on each usable address in the complete snapshot.
 *
 * \par Requirements:
 *  - 001 The function shall create no socket or buffer and shall retain native failure evidence.
 *
 * \param[in,out] discovery   Ready owner after draining old replies
 *
 * \retval        TRUE    At least one search datagram was sent.
 * \retval        FALSE   No search could be issued.
 */
static FORCE_INLINE bool_t SendSearch(Ssdp_t* discovery);

/*************************************************************************************************/
/**
 * \brief Validates one received SSDP reply and records a new bounded USN/location pair.
 *
 * \par Details:
 *  A valid new identity is retained when present.
 *
 * \par Requirements:
 *  - 001 Malformed, expired, oversized, duplicate and nonmatching replies shall leave location count unchanged.
 *
 * \param[in,out] state       Exclusive native workspace with room for one more location
 * \param[in]     byteCount   Positive datagram byte count within response storage
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void ReadResponse(Ssdp_State_t* state, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Collects replies within the requested reply window and output capacity.
 *
 * \par Details:
 *  Valid locations and native failure evidence are retained.
 *
 * \par Requirements:
 *  - 001 Socket reads shall be nonblocking; timeout shall be recomputed before each select.
 *
 * \param[in,out] discovery   Ready owner after a successful send
 * \param[in]     capacity    Positive location limit no greater than CAST_MAX_DEVICES
 * \param[in]     timeoutMs   Reply window in milliseconds
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void CollectResponses(Ssdp_t* discovery, uint32_t capacity, uint32_t timeoutMs);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  A failed native operation reporting zero receives the generic-failure code.
 */
static FORCE_INLINE void RecordError(Ssdp_t* discovery, uint32_t code)
{
    uint32_t error;

    error = code;
    if (0 == error)
    {
        error = ERROR_GEN_FAILURE;
    }
    if (0 == discovery->error)
    {
        discovery->error = error;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Called only for synchronous BOOL-returning operations that set GetLastError.
 */
static FORCE_INLINE bool_t CheckHttpResult(Ssdp_t* discovery, BOOL result)
{
    bool_t   valid;
    uint32_t error;

    valid = !!result;
    if (!valid)
    {
        error = GetLastError();
        RecordError(discovery, error);
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A failed child close prevents closing its parent. Native identities remain
 *  in the retained workspace until each close succeeds.
 */
static FORCE_INLINE bool_t CloseRequest(Ssdp_t* discovery)
{
    Ssdp_State_t* state;
    bool_t        closed;
    uint32_t      error;

    state  = discovery->state;
    closed = TRUE;
    if (NULL != state->requestHandle)
    {
        closed = !!WinHttpCloseHandle(state->requestHandle);
        if (closed)
        {
            state->requestHandle = NULL;
        }
        else
        {
            error = GetLastError();
            RecordError(discovery, error);
        }
    }
    if (closed && (NULL != state->connection))
    {
        closed = !!WinHttpCloseHandle(state->connection);
        if (closed)
        {
            state->connection = NULL;
        }
        else
        {
            error = GetLastError();
            RecordError(discovery, error);
        }
    }

    return closed;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Entity decoding and CDATA concatenation preserve complete UTF-8 scalar boundaries.
 */
static FORCE_INLINE bool_t WriteDisplayText(const Utl_Xml_Node_t* element, char_t* output, uint32_t capacity)
{
    uint32_t count;
    bool_t   valid;
    LOGASSERT(NULL != output);
    LOGASSERT(0 < capacity);
    count = 0;
    valid = Utl_Xml_WriteElementText(element, (byte_t*)output, capacity - 1, &count);
    if (valid)
    {
        output[count] = DISCOVERY_TERMINATOR;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Prefix spelling and attribute order cannot redirect field selection. Nested
 *  devices and vendor namespaces do not supply the root device's display name.
 */
static FORCE_INLINE bool_t ReadDescription(Ssdp_t* discovery, CastDevice* device)
{
    Ssdp_State_t*          state;
    Utl_Xml_Result_t       result;
    bool_t                 valid;
    uint32_t               error;
    Utl_Upnp_Description_t description;
    state  = discovery->state;
    result = Utl_Xml_ReadDocument(&state->xmlDocument, state->document, state->documentBytes);
    valid  = (UTL_XML_COMPLETE == result);
    error  = ERROR_INVALID_DATA;
    if (UTL_XML_CAPACITY == result)
    {
        error = ERROR_INSUFFICIENT_BUFFER;
    }
    if (valid)
    {
        valid = Utl_Upnp_ReadDescription(&state->xmlDocument, &description);
    }
    if (valid)
    {
        error = ERROR_INSUFFICIENT_BUFFER;
        valid = WriteDisplayText(description.friendlyName, device->friendlyName, CAST_DEVICE_NAME_BYTES);
        if (valid && (NULL != description.modelName))
        {
            valid = WriteDisplayText(description.modelName, device->model, CAST_DEVICE_MODEL_BYTES);
        }
    }
    if (!valid)
    {
        RecordError(discovery, error);
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Generic URI registered-name syntax is broader than native DNS name support.
 *  IDNA conversion remains a separate open responsibility; no byte is discarded.
 */
static FORCE_INLINE bool_t IsNativeHost(const char_t* host, uint32_t byteCount, Utl_Url_HostKind_t kind)
{
    bool_t   valid;
    bool_t   ipv6;
    uint32_t index;
    char_t   value;
    bool_t   upper;
    bool_t   lower;
    bool_t   digit;
    bool_t   separator;

    valid = (0 < byteCount) && (UTL_URL_HOST_FUTURE != kind);
    ipv6  = (UTL_URL_HOST_IPV6 == kind);
    for (index = 0; valid && (index < byteCount); ++index)
    {
        value     = host[index];
        upper     = (UTL_URL_CHAR_UPPER_FIRST <= value) && (UTL_URL_CHAR_UPPER_LAST >= value);
        lower     = (UTL_URL_CHAR_LOWER_FIRST <= value) && (UTL_URL_CHAR_LOWER_LAST >= value);
        digit     = (UTL_URL_CHAR_DIGIT_FIRST <= value) && (UTL_URL_CHAR_DIGIT_LAST >= value);
        separator = (UTL_URL_CHAR_HYPHEN == value) || (UTL_URL_CHAR_DOT == value);
        valid     = upper || lower || digit || separator || (ipv6 && (UTL_URL_CHAR_COLON == value));
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Decode the host only after separating URI components. A zero byte, escaped
 *  delimiter or unsupported host must never change WinHTTP's interpretation.
 *  WinHTTP requires UTF-16 and preconverted IDN; this adapter currently accepts
 *  ASCII hosts only. All accepted target bytes are validated ASCII URI syntax.
 *  Validate each count against its destination before bounding the retained
 *  index. Invalid counts remain failures and cannot publish an endpoint.
 */
static FORCE_INLINE bool_t ReadLocation(Ssdp_t* discovery, const char_t* location, CastDevice* device)
{
    uint32_t            host_bytes;
    uint32_t            target_bytes;
    Ssdp_State_t*       state;
    uint32_t            count;
    bool_t              valid;
    Utl_Url_t           url;
    Utl_Http_Endpoint_t endpoint;
    bool_t              fits_device_host;
    bool_t              fits_native_host;
    bool_t              fits_host;
    bool_t              fits_target;
    uint32_t            index;

    host_bytes   = 0;
    target_bytes = 0;
    state        = discovery->state;
    count        = Utl_Unicode_GetUtf8StringByteCount(location, LOCATION_CAPACITY);
    valid        = (UTL_UNICODE_NO_TERMINATOR != count);
    if (valid)
    {
        valid = Utl_Url_Read(&url, (const byte_t*)location, count);
    }
    if (valid)
    {
        valid = Utl_Http_GetUrlEndpoint(&url, &endpoint);
    }
    if (valid)
    {
        valid = (0 != endpoint.port);
    }
    if (valid)
    {
        valid = Utl_Url_WriteDecoded(endpoint.host, (byte_t*)device->host, CAST_DEVICE_HOST_BYTES - 1, &host_bytes);
    }
    if (valid)
    {
        fits_device_host = (host_bytes < CAST_DEVICE_HOST_BYTES);
        fits_native_host = (host_bytes < DISCOVERY_HOST_CAPACITY);
        fits_host        = fits_device_host && fits_native_host;
        valid            = fits_host;
        host_bytes      %= DISCOVERY_HOST_CAPACITY;
        host_bytes      %= CAST_DEVICE_HOST_BYTES;
    }
    if (valid)
    {
        valid = IsNativeHost(device->host, host_bytes, url.hostKind);
    }
    if (valid)
    {
        valid = Utl_Http_WriteOriginTarget(&url, state->target, DISCOVERY_PATH_CAPACITY - 1, &target_bytes);
    }
    if (valid)
    {
        fits_target   = (target_bytes < DISCOVERY_PATH_CAPACITY);
        valid         = fits_target;
        target_bytes %= DISCOVERY_PATH_CAPACITY;
    }
    if (valid)
    {
        for (index = 0; index < host_bytes; ++index)
        {
            state->nativeHost[index] = (WCHAR)(byte_t)device->host[index];
        }
        for (index = 0; index < target_bytes; ++index)
        {
            state->nativeTarget[index] = (WCHAR)state->target[index];
        }
        device->host[host_bytes]          = DISCOVERY_TERMINATOR;
        state->nativeHost[host_bytes]     = 0;
        state->nativeTarget[target_bytes] = 0;
        state->port                       = endpoint.port;
        state->secure                     = endpoint.secure;
    }
    else
    {
        RecordError(discovery, ERROR_INVALID_DATA);
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  One-byte overflow probing distinguishes an exactly full document from a
 *  truncated body. Only successful EOF publishes a terminated nonempty document.
 */
static FORCE_INLINE bool_t ReadDocument(Ssdp_t* discovery)
{
    Ssdp_State_t* state;
    bool_t        valid;
    uint32_t      total;
    bool_t        ended;
    DWORD         received;
    byte_t        overflow;
    uint32_t      error;
    bool_t        fits;

    state = discovery->state;
    valid = TRUE;
    total = 0;
    ended = FALSE;
    for (;;)
    {
        if ((!valid) || ended)
        {
            break;
        }
        received = 0;
        if (total < (DISCOVERY_DOCUMENT_CAPACITY - 1))
        {
            valid = !!WinHttpReadData(state->requestHandle,
                                      state->document + total,
                                      DISCOVERY_DOCUMENT_CAPACITY - 1 - total,
                                      &received);
        }
        else
        {
            valid = !!WinHttpReadData(state->requestHandle, &overflow, 1, &received);
        }
        if (!valid)
        {
            error = GetLastError();
            RecordError(discovery, error);
        }
        else
        {
            fits = (received < (DISCOVERY_DOCUMENT_CAPACITY - total));
            if (fits)
            {
                total += received;
            }
            else
            {
                valid = FALSE;
                RecordError(discovery, ERROR_INSUFFICIENT_BUFFER);
            }
        }
        ended = (0 == received);
    }
    valid = valid && ended && (0 < total);
    if (valid)
    {
        state->document[total] = DISCOVERY_TERMINATOR;
        state->documentBytes   = total;
    }
    else
    {
        state->document[0]   = DISCOVERY_TERMINATOR;
        state->documentBytes = 0;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  WinHTTP has a fixed UTF-16 API boundary. Request and connection identities
 *  remain owned across failure. A one-byte overflow probe distinguishes an
 *  exactly-full body from truncation, and only a successful EOF accepts input.
 */
static FORCE_INLINE bool_t HttpGet(Ssdp_t* discovery)
{
    Ssdp_State_t* state;
    bool_t        valid;
    uint32_t      error;
    DWORD         flags;
    DWORD         status;
    DWORD         status_bytes;
    bool_t        closed;

    state                = discovery->state;
    state->documentBytes = 0;
    state->document[0]   = DISCOVERY_TERMINATOR;
    valid                = CloseRequest(discovery);
    if (valid)
    {
        state->connection = WinHttpConnect(state->session, state->nativeHost, state->port, 0);
        valid             = (NULL != state->connection);
        if (!valid)
        {
            error = GetLastError();
            RecordError(discovery, error);
        }
    }
    if (valid)
    {
        flags = 0;
        if (state->secure)
        {
            flags = WINHTTP_FLAG_SECURE;
        }
        state->requestHandle = WinHttpOpenRequest(state->connection,
                                                  L"GET",
                                                  state->nativeTarget,
                                                  NULL,
                                                  WINHTTP_NO_REFERER,
                                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                  flags);
        valid                = (NULL != state->requestHandle);
        if (!valid)
        {
            error = GetLastError();
            RecordError(discovery, error);
        }
    }
    if (valid)
    {
        valid = CheckHttpResult(discovery,
                                WinHttpSendRequest(state->requestHandle,
                                                   WINHTTP_NO_ADDITIONAL_HEADERS,
                                                   0,
                                                   WINHTTP_NO_REQUEST_DATA,
                                                   0,
                                                   0,
                                                   0));
    }
    if (valid)
    {
        valid = CheckHttpResult(discovery, WinHttpReceiveResponse(state->requestHandle, NULL));
    }
    if (valid)
    {
        status       = 0;
        status_bytes = sizeof(DWORD);
        valid        = CheckHttpResult(discovery,
                                       WinHttpQueryHeaders(state->requestHandle,
                                                           WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                                           WINHTTP_HEADER_NAME_BY_INDEX,
                                                           &status,
                                                           &status_bytes,
                                                           WINHTTP_NO_HEADER_INDEX));
        if (valid && (HTTP_STATUS_OK != status))
        {
            valid = FALSE;
            RecordError(discovery, ERROR_INVALID_DATA);
        }
    }
    if (valid)
    {
        valid = ReadDocument(discovery);
    }
    closed = CloseRequest(discovery);
    valid  = valid && closed;

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A failed fetch or missing display name leaves the complete caller value
 *  unchanged, including when a direct host aliases the destination host field.
 */
static FORCE_INLINE bool_t Describe(Ssdp_t* discovery, const char_t* location, CastDevice* device)
{
    CastDevice candidate;
    bool_t     valid;

    CRT_Memory_Set(&candidate, 0, sizeof(CastDevice));
    valid = ReadLocation(discovery, location, &candidate);
    if (valid)
    {
        valid = HttpGet(discovery);
    }
    if (valid)
    {
        valid = ReadDescription(discovery, &candidate);
    }
    if (valid)
    {
        *device = candidate;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Oversized datagrams are consumed and discarded. An empty UDP datagram is
 *  also consumed; only WSAEWOULDBLOCK proves that this drain reached the end.
 */
static FORCE_INLINE bool_t DrainSocket(Ssdp_t* discovery)
{
    bool_t   empty;
    uint32_t index;
    int32_t  received;
    uint32_t error;

    empty = FALSE;
    for (index = 0; index < DISCOVERY_DRAIN_LIMIT; ++index)
    {
        received = recvfrom(discovery->state->socket,
                            (char_t*)discovery->state->response,
                            DISCOVERY_RESPONSE_CAPACITY,
                            0,
                            NULL,
                            NULL);
        if (SOCKET_ERROR == received)
        {
            error = (uint32_t)WSAGetLastError();
            empty = (WSAEWOULDBLOCK == error);
            if (empty)
            {
                break;
            }
            if (WSAEMSGSIZE != error)
            {
                RecordError(discovery, error);
                break;
            }
        }
    }
    if ((!empty) && (0 == discovery->error))
    {
        RecordError(discovery, ERROR_MORE_DATA);
    }

    return empty;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Interface selection affects outbound multicast only. The wildcard-bound
 *  socket continues to receive unicast replies to its retained local port.
 */
static FORCE_INLINE bool_t SendSearch(Ssdp_t* discovery)
{
    Ssdp_State_t*          state;
    bool_t                 snapshot_ready;
    bool_t                 sent;
    uint32_t               count;
    uint32_t               index;
    OS_Network_Interface_t value;
    bool_t                 usable;
    struct in_addr         address;
    int32_t                status;
    uint32_t               error;
    uint32_t               attempt;

    state          = discovery->state;
    snapshot_ready = OS_Network_ReadInterfaces(&state->network);
    sent           = FALSE;
    if (snapshot_ready)
    {
        count = OS_Network_GetInterfaceCount(&state->network);
        for (index = 0; index < count; ++index)
        {
            OS_Network_GetInterface(&state->network, index, &value);
            usable = value.running && (!value.loopback) && value.multicast;
            if (usable)
            {
                CRT_Memory_Copy(&address, value.address, OS_NETWORK_IPV4_BYTE_COUNT);
                status = setsockopt(state->socket,
                                    IPPROTO_IP,
                                    IP_MULTICAST_IF,
                                    (const char_t*)&address,
                                    sizeof(struct in_addr));
                usable = (0 == status);
                if (!usable)
                {
                    error = (uint32_t)WSAGetLastError();
                    RecordError(discovery, error);
                }
            }
            if (usable)
            {
                for (attempt = 0; attempt < DISCOVERY_SEARCH_ATTEMPTS; ++attempt)
                {
                    status = sendto(state->socket,
                                    (const char_t*)state->request,
                                    (int32_t)state->requestBytes,
                                    0,
                                    (const struct sockaddr*)&state->group,
                                    sizeof(struct sockaddr_in));
                    if ((int32_t)state->requestBytes == status)
                    {
                        sent = TRUE;
                    }
                    else
                    {
                        error = (uint32_t)WSAGetLastError();
                        RecordError(discovery, error);
                    }
                }
            }
        }
    }
    else
    {
        RecordError(discovery, OS_Network_GetError(&state->network));
    }

    return sent;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Counted HTTP/SSDP parsing precedes every copy. Publication of a new location
 *  follows both complete identity and location copies, without truncation.
 */
static FORCE_INLINE void ReadResponse(Ssdp_State_t* state, uint32_t byteCount)
{
    Utl_Http_Head_t          head;
    Utl_Http_Result_t        result;
    bool_t                   valid;
    Utl_Ssdp_Response_t      candidate;
    Utl_Http_Text_t          target;
    bool_t                   matches_target;
    bool_t                   fits_location;
    CastDiscoveryLocation_t* location;
    bool_t                   fits_identity;
    bool_t                   current;
    bool_t                   duplicate;
    uint32_t                 index;
    bool_t                   same_identity;

    LOGASSERT(CAST_MAX_DEVICES > state->locationCount);
    LOGASSERT(DISCOVERY_RESPONSE_CAPACITY >= byteCount);

    head.fields        = state->fields;
    head.fieldCapacity = DISCOVERY_FIELD_CAPACITY;
    result             = Utl_Http_ReadHead(&head, UTL_HTTP_RESPONSE, state->response, byteCount);
    valid              = (UTL_HTTP_COMPLETE == result);
    if (valid)
    {
        valid = Utl_Ssdp_ReadResponse(&candidate, &head);
    }
    if (valid)
    {
        target.data      = (const byte_t*)DISCOVERY_DIAL_TARGET;
        target.byteCount = sizeof(DISCOVERY_DIAL_TARGET) - 1;
        matches_target   = Utl_Http_IsTextEqual(candidate.target, target, FALSE);
        fits_location    = (candidate.location.byteCount < LOCATION_CAPACITY);
        fits_identity    = (candidate.usn.byteCount <= DISCOVERY_IDENTITY_CAPACITY);
        current          = (0 < candidate.maximumAge);
        valid            = matches_target && fits_location && fits_identity && current;
    }
    if (valid)
    {
        duplicate = FALSE;
        for (index = 0; index < state->locationCount; ++index)
        {
            same_identity = (state->locations[index].identityBytes == candidate.usn.byteCount);
            if (same_identity)
            {
                same_identity =
                    (0 ==
                     CRT_Memory_Compare(state->locations[index].identity, candidate.usn.data, candidate.usn.byteCount));
            }
            if (same_identity)
            {
                duplicate = TRUE;
                break;
            }
        }
        if (!duplicate)
        {
            location = &state->locations[state->locationCount];
            CRT_Memory_Copy(location->location, candidate.location.data, candidate.location.byteCount);
            location->location[candidate.location.byteCount] = DISCOVERY_TERMINATOR;
            CRT_Memory_Copy(location->identity, candidate.usn.data, candidate.usn.byteCount);
            location->identityBytes = candidate.usn.byteCount;
            ++state->locationCount;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Nonblocking recvfrom prevents a stale readiness indication from blocking.
 *  Recomputing the remaining interval bounds the loop even under unrelated traffic.
 */
static FORCE_INLINE void CollectResponses(Ssdp_t* discovery, uint32_t capacity, uint32_t timeoutMs)
{
    Ssdp_State_t*  state;
    uint64_t       started;
    uint64_t       elapsed;
    uint32_t       remaining;
    struct timeval timeout;
    fd_set         readable;
    int32_t        status;
    uint32_t       error;

    state   = discovery->state;
    started = GetTickCount64();
    for (;;)
    {
        elapsed = GetTickCount64() - started;
        if ((elapsed >= timeoutMs) || (state->locationCount >= capacity))
        {
            break;
        }
        remaining            = timeoutMs - (uint32_t)elapsed;
        timeout.tv_sec       = (long)(remaining / DISCOVERY_MS_PER_SECOND);
        timeout.tv_usec      = (long)(remaining % DISCOVERY_MS_PER_SECOND) * DISCOVERY_US_PER_MS;
        readable.fd_count    = 1;
        readable.fd_array[0] = state->socket;
        status               = select(0, &readable, NULL, NULL, &timeout);
        if (SOCKET_ERROR == status)
        {
            error = (uint32_t)WSAGetLastError();
            RecordError(discovery, error);
            break;
        }
        if (0 == status)
        {
            break;
        }
        status = recvfrom(state->socket, (char_t*)state->response, DISCOVERY_RESPONSE_CAPACITY, 0, NULL, NULL);
        if (0 < status)
        {
            ReadResponse(state, (uint32_t)status);
        }
        else if (SOCKET_ERROR == status)
        {
            error = (uint32_t)WSAGetLastError();
            if ((WSAEWOULDBLOCK != error) && (WSAEMSGSIZE != error))
            {
                RecordError(discovery, error);
                break;
            }
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Native identities are initialized before any acquisition may fail. The fixed
 *  search bytes, multicast endpoint and page-touched buffers survive every refresh.
 */
bool_t Ssdp_Initialize(Ssdp_t* discovery)
{
    Ssdp_State_t*      state;
    bool_t             ready;
    uint32_t           error;
    Utl_Xml_Storage_t  xml_storage;
    struct sockaddr_in local;
    int32_t            status;
    u_long             nonblocking;
    DWORD              control_bytes;
    Utl_Http_Text_t    target;
    Utl_Ssdp_Search_t  search;

    LOGASSERT(NULL != discovery);

    discovery->state = NULL;
    discovery->ready = FALSE;
    discovery->error = 0;
    state            = NULL;
    ready            = OS_Memory_Initialize(&discovery->memory, sizeof(Ssdp_State_t));
    if (ready)
    {
        state                         = (Ssdp_State_t*)OS_Memory_GetAddress(&discovery->memory);
        discovery->state              = state;
        state->socket                 = INVALID_SOCKET;
        xml_storage.nodes             = state->xmlNodes;
        xml_storage.attributes        = state->xmlAttributes;
        xml_storage.nodeCapacity      = DISCOVERY_XML_NODE_CAPACITY;
        xml_storage.attributeCapacity = DISCOVERY_XML_ATTRIBUTE_CAPACITY;
        Utl_Xml_InitializeDocument(&state->xmlDocument, xml_storage);
        ready = OS_Network_Initialize(&state->network);
        if (!ready)
        {
            RecordError(discovery, OS_Network_GetError(&state->network));
        }
    }
    else
    {
        RecordError(discovery, OS_Memory_GetError(&discovery->memory));
    }
    if (ready)
    {
        state->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        ready         = (INVALID_SOCKET != state->socket);
        if (!ready)
        {
            error = (uint32_t)WSAGetLastError();
            RecordError(discovery, error);
        }
    }
    if (ready)
    {
        CRT_Memory_Set(&local, 0, sizeof(struct sockaddr_in));
        local.sin_family = AF_INET;
        status           = bind(state->socket, (const struct sockaddr*)&local, sizeof(struct sockaddr_in));
        ready            = (0 == status);
        if (!ready)
        {
            error = (uint32_t)WSAGetLastError();
            RecordError(discovery, error);
        }
    }
    if (ready)
    {
        nonblocking   = 1;
        control_bytes = 0;
        status = WSAIoctl(state->socket, FIONBIO, &nonblocking, sizeof(u_long), NULL, 0, &control_bytes, NULL, NULL);
        ready  = (0 == status);
        if (!ready)
        {
            error = (uint32_t)WSAGetLastError();
            RecordError(discovery, error);
        }
    }
    if (ready)
    {
        state->session = WinHttpOpen(L"cvk CastDemo/1.0",
                                     WINHTTP_ACCESS_TYPE_NO_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS,
                                     0);
        ready          = (NULL != state->session);
        if (!ready)
        {
            error = GetLastError();
            RecordError(discovery, error);
        }
    }
    if (ready)
    {
        ready = !!WinHttpSetTimeouts(state->session,
                                     DISCOVERY_HTTP_TIMEOUT_MS,
                                     DISCOVERY_HTTP_TIMEOUT_MS,
                                     DISCOVERY_HTTP_TIMEOUT_MS,
                                     DISCOVERY_HTTP_TIMEOUT_MS);
        if (!ready)
        {
            error = GetLastError();
            RecordError(discovery, error);
        }
    }
    if (ready)
    {
        state->group.sin_family = AF_INET;
        state->group.sin_port   = htons(UTL_SSDP_MULTICAST_PORT);
        status                  = inet_pton(AF_INET, UTL_SSDP_MULTICAST_ADDRESS, &state->group.sin_addr);
        LOGASSERT(1 == status);
        target.data      = (const byte_t*)DISCOVERY_DIAL_TARGET;
        target.byteCount = sizeof(DISCOVERY_DIAL_TARGET) - 1;
        Utl_Ssdp_InitializeSearch(&search, target, DISCOVERY_RESPONSE_SECONDS);
        state->requestBytes = Utl_Ssdp_WriteSearch(&search, state->request, DISCOVERY_SEARCH_CAPACITY);
        LOGASSERT(0 < state->requestBytes);
        discovery->ready = TRUE;
    }

    return discovery->ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Worker completion must be acquired before inspecting worker-written errors.
 */
uint32_t Ssdp_GetError(const Ssdp_t* discovery)
{
    LOGASSERT(NULL != discovery);

    return discovery->error;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Interface snapshots, the UDP endpoint and response/description buffers are
 *  reused. Only described receiver values reach caller storage. WinHTTP request
 *  allocations and synchronous I/O remain explicit uncompleted goal requirements.
 */
uint32_t Ssdp_DiscoverCastDevices(Ssdp_t* discovery, CastDevice* devices, uint32_t capacity, uint32_t timeoutMs)
{
    uint32_t count;
    bool_t   can_search;
    bool_t   sent;
    uint32_t index;

    LOGASSERT(NULL != discovery);
    LOGASSERT(discovery->ready);
    LOGASSERT((NULL != devices) || (0 == capacity));

    discovery->error                = 0;
    discovery->state->locationCount = 0;
    count                           = 0;
    can_search                      = (0 < capacity) && (0 < timeoutMs);
    if (can_search)
    {
        if (CAST_MAX_DEVICES < capacity)
        {
            capacity = CAST_MAX_DEVICES;
        }
        can_search = DrainSocket(discovery);
        if (can_search)
        {
            sent = SendSearch(discovery);
            if (sent)
            {
                CollectResponses(discovery, capacity, timeoutMs);
            }
            for (index = 0; index < discovery->state->locationCount; ++index)
            {
                if (Describe(discovery, discovery->state->locations[index].location, &devices[count]))
                {
                    ++count;
                }
            }
        }
    }

    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The location is staged with bounded counted copies before Describe may
 *  publish a value, preserving the documented host/output alias case.
 */
bool_t Ssdp_DescribeHost(Ssdp_t* discovery, const char_t* host, CastDevice* device)
{
    uint32_t host_bytes;
    bool_t   valid;
    char_t*  location;
    uint32_t offset;

    LOGASSERT(NULL != discovery);
    LOGASSERT(NULL != host);
    LOGASSERT(NULL != device);
    LOGASSERT(discovery->ready);

    discovery->error = 0;
    for (host_bytes = 0; host_bytes < CAST_DEVICE_HOST_BYTES; ++host_bytes)
    {
        if (DISCOVERY_TERMINATOR == host[host_bytes])
        {
            break;
        }
    }
    valid = (0 < host_bytes) && (host_bytes < CAST_DEVICE_HOST_BYTES);
    if (valid)
    {
        location = discovery->state->directLocation;
        offset   = sizeof(DISCOVERY_DIRECT_PREFIX) - 1;
        CRT_Memory_Copy(location, DISCOVERY_DIRECT_PREFIX, offset);
        CRT_Memory_Copy(location + offset, host, host_bytes);
        offset += host_bytes;
        LOGASSERT(LOCATION_CAPACITY >= (offset + sizeof(DISCOVERY_DIRECT_SUFFIX)));
        CRT_Memory_Copy(location + offset, DISCOVERY_DIRECT_SUFFIX, sizeof(DISCOVERY_DIRECT_SUFFIX));
        valid = Describe(discovery, location, device);
    }
    else
    {
        RecordError(discovery, ERROR_INVALID_PARAMETER);
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Independently releases HTTP, datagram and interface-query resources. A failed
 *  release retains both its identity and the storage holding it for the next poll.
 */
bool_t Ssdp_TryClose(Ssdp_t* discovery)
{
    bool_t        children_closed;
    Ssdp_State_t* state;
    bool_t        http_closed;
    uint32_t      error;
    bool_t        socket_closed;
    int32_t       status;
    bool_t        network_closed;
    bool_t        closed;

    LOGASSERT(NULL != discovery);

    discovery->ready = FALSE;
    children_closed  = TRUE;
    state            = discovery->state;
    if (NULL != state)
    {
        http_closed = CloseRequest(discovery);
        if (http_closed && (NULL != state->session))
        {
            http_closed = !!WinHttpCloseHandle(state->session);
            if (http_closed)
            {
                state->session = NULL;
            }
            else
            {
                error = GetLastError();
                RecordError(discovery, error);
            }
        }
        socket_closed = TRUE;
        if (INVALID_SOCKET != state->socket)
        {
            status        = closesocket(state->socket);
            socket_closed = (0 == status);
            if (socket_closed)
            {
                state->socket = INVALID_SOCKET;
            }
            else
            {
                error = (uint32_t)WSAGetLastError();
                RecordError(discovery, error);
            }
        }
        network_closed = OS_Network_TryClose(&state->network);
        if (!network_closed)
        {
            RecordError(discovery, OS_Network_GetError(&state->network));
        }
        children_closed = http_closed && socket_closed && network_closed;
    }
    closed = FALSE;
    if (children_closed)
    {
        closed = OS_Memory_TryClose(&discovery->memory);
        if (closed)
        {
            discovery->state = NULL;
        }
        else
        {
            RecordError(discovery, OS_Memory_GetError(&discovery->memory));
        }
    }

    return closed;
}
