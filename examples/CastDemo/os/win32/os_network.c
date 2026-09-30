/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * SIO_GET_INTERFACE_LIST uses caller-provided storage; insufficient capacity fails.
 * SIO_ROUTING_INTERFACE_QUERY uses the same retained socket with staged sockaddr output.
 * https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls
 * https://learn.microsoft.com/en-us/windows/win32/api/ws2ipdef/ns-ws2ipdef-interface_info
 * https://learn.microsoft.com/en-us/windows/win32/api/winsock2/nf-winsock2-closesocket
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <WinSock2.h>
#include <ws2ipdef.h>
#include <inaddr.h>
#include <winerror.h>
#include <minwindef.h>
#include <stdint.h>
#include "os/os_network.h"
#include "os/os_memory.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "crt/crt_memory.h"

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
 * \brief One retained query socket and its fixed native output array.
 */
typedef struct OS_Network_State_s
{
    SOCKET         socket;                                 ///< Created only during initialization.
    uint32_t       count;                                  ///< Published only after validating the whole output.
    INTERFACE_INFO entries[OS_NETWORK_INTERFACE_CAPACITY]; ///< Native ABI query storage.
} OS_Network_State_t;

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
 * \brief Retains the first captured native error.
 *
 * \par Details:
 *  Failure evidence is retained.
 *
 * \par Requirements:
 *  - 001 The caller shall capture the code immediately after the failed native operation.
 *
 * \param[in,out] network   Initialized owner
 * \param[in]     code      Captured native failure code
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void RecordError(OS_Network_t* network, uint32_t code);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  A zero code from a failed operation receives the generic-failure code.
 */
static FORCE_INLINE void RecordError(OS_Network_t* network, uint32_t code)
{
    uint32_t error;
    uint32_t previous;

    error = (0 == code) ? ERROR_GEN_FAILURE : code;
    previous = network->error;
    network->error = (0 == previous) ? error : previous;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Resident storage is acquired before constructing the native socket identity.
 *  The query uses an IPv4 datagram provider and does not bind a local endpoint.
 */
extern FORCE_INLINE bool_t OS_Network_Initialize(OS_Network_t* network)
{
    uint32_t error;
    bool_t   storage_ready;

    LOGASSERT(NULL != network);

    network->state = NULL;
    network->ready = FALSE;
    network->error = 0;
    storage_ready  = OS_Memory_Initialize(&network->memory, sizeof(OS_Network_State_t));
    if (storage_ready)
    {
        network->state         = (OS_Network_State_t*)OS_Memory_GetAddress(&network->memory);
        network->state->socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        network->ready         = (INVALID_SOCKET != network->state->socket);
        if (!network->ready)
        {
            error = (uint32_t)WSAGetLastError();
            RecordError(network, error);
        }
    }
    else
    {
        error = OS_Memory_GetError(&network->memory);
        RecordError(network, error);
    }

    return network->ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE bool_t OS_Network_IsReady(const OS_Network_t* network)
{
    LOGASSERT(NULL != network);

    return network->ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Native Winsock and storage error codes share the Win32 error domain.
 */
extern FORCE_INLINE uint32_t OS_Network_GetError(const OS_Network_t* network)
{
    LOGASSERT(NULL != network);

    return network->error;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  An unpublished or closing reader exposes no native array elements.
 */
extern FORCE_INLINE uint32_t OS_Network_GetInterfaceCount(const OS_Network_t* network)
{
    uint32_t count;

    LOGASSERT(NULL != network);

    count = 0;
    if (network->ready)
    {
        count = network->state->count;
    }

    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  ReadInterfaces has validated AF_INET for every published native address.
 *  Snapshot a bounded native entry before asserting publication. The mask retains
 *  a valid storage index even when the caller violates the index contract.
 *  Named native octets preserve network byte order without an indexed copy call.
 */
extern FORCE_INLINE void OS_Network_GetInterface(const OS_Network_t*     network,
                                                 uint32_t                index,
                                                 OS_Network_Interface_t* value)
{
    bool_t                fits_storage;
    bool_t                is_published;
    bool_t                valid_index;
    uint32_t              index_mask;
    uint32_t              safe_index;
    const INTERFACE_INFO* entry;
    IN_ADDR               address;
    ULONG                 flags;

    LOGASSERT(NULL != network);
    LOGASSERT(NULL != value);
    LOGASSERT(network->ready);

    fits_storage = (OS_NETWORK_INTERFACE_CAPACITY > index);
    is_published = (network->state->count > index);
    valid_index  = fits_storage && is_published;
    index_mask   = 0 - (uint32_t)valid_index;
    safe_index   = index & index_mask;
    entry        = &network->state->entries[safe_index];
    address      = entry->iiAddress.AddressIn.sin_addr;
    flags        = entry->iiFlags;

    LOGASSERT(fits_storage);
    LOGASSERT(is_published);

    value->address[0] = address.S_un.S_un_b.s_b1;
    value->address[1] = address.S_un.S_un_b.s_b2;
    value->address[2] = address.S_un.S_un_b.s_b3;
    value->address[3] = address.S_un.S_un_b.s_b4;
    value->running    = !!(flags & IFF_UP);
    value->loopback   = !!(flags & IFF_LOOPBACK);
    value->multicast  = !!(flags & IFF_MULTICAST);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The native call is synchronous and bounded in output size, not proven latency.
 *  Count, extent and address-family validation precede publishing any entry.
 */
extern FORCE_INLINE bool_t OS_Network_ReadInterfaces(OS_Network_t* network)
{
    uint32_t error;
    uint32_t count;
    DWORD    bytes;
    int32_t  status;
    bool_t   valid;
    uint32_t index;

    LOGASSERT(NULL != network);
    LOGASSERT(network->ready);

    network->error        = 0;
    network->state->count = 0;
    bytes                 = 0;
    status                = WSAIoctl(network->state->socket,
                                     SIO_GET_INTERFACE_LIST,
                                     NULL,
                                     0,
                                     network->state->entries,
                                     sizeof(network->state->entries),
                                     &bytes,
                                     NULL,
                                     NULL);
    valid                 = (0 == status);
    if (!valid)
    {
        error = (uint32_t)WSAGetLastError();
        RecordError(network, error);
    }
    else
    {
        valid = (sizeof(network->state->entries) >= bytes);
        valid = valid && (0 == (bytes % sizeof(INTERFACE_INFO)));
        count = bytes / sizeof(INTERFACE_INFO);
        for (index = 0; valid && (index < count); ++index)
        {
            valid = (AF_INET == network->state->entries[index].iiAddress.AddressIn.sin_family);
        }
        if (valid)
        {
            network->state->count = count;
        }
        else
        {
            RecordError(network, ERROR_INVALID_DATA);
        }
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  SIO_ROUTING_INTERFACE_QUERY neither connects nor recreates the retained query socket.
 */
extern FORCE_INLINE bool_t OS_Network_ReadRoute(OS_Network_t* network, OS_Network_Route_t* route)
{
    uint32_t           error;
    struct sockaddr_in destination;
    struct sockaddr_in local;
    DWORD              bytes;
    int32_t            status;
    bool_t             valid;
    bool_t             valid_extent;
    bool_t             valid_family;
    LOGASSERT(NULL != network);
    LOGASSERT(NULL != route);
    LOGASSERT(network->ready);
    network->error = 0;
    CRT_Memory_Set(&destination, 0, sizeof(struct sockaddr_in));
    CRT_Memory_Set(&local, 0, sizeof(struct sockaddr_in));
    destination.sin_family      = AF_INET;
    destination.sin_addr.s_addr = htonl(route->destination);
    bytes                       = 0;
    status                      = WSAIoctl(network->state->socket,
                                           (uint32_t)SIO_ROUTING_INTERFACE_QUERY,
                                           &destination,
                                           sizeof(struct sockaddr_in),
                                           &local,
                                           sizeof(struct sockaddr_in),
                                           &bytes,
                                           NULL,
                                           NULL);
    valid                       = (0 == status);
    if (!valid)
    {
        error = (uint32_t)WSAGetLastError();
        RecordError(network, error);
    }
    else
    {
        valid_extent = (sizeof(struct sockaddr_in) == bytes);
        valid_family = (AF_INET == local.sin_family);
        valid        = valid_extent && valid_family;
        if (valid)
        {
            route->local = ntohl(local.sin_addr.s_addr);
        }
        else
        {
            RecordError(network, ERROR_INVALID_DATA);
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The socket identity remains retained if closesocket fails. Storage release
 *  begins only after the socket is gone, and failed storage release is retryable.
 */
extern FORCE_INLINE bool_t OS_Network_TryClose(OS_Network_t* network)
{
    bool_t   socket_closed;
    int32_t  status;
    uint32_t error;
    bool_t   closed;

    LOGASSERT(NULL != network);

    network->ready = FALSE;
    socket_closed  = TRUE;
    if (NULL != network->state)
    {
        network->state->count = 0;
        if (INVALID_SOCKET != network->state->socket)
        {
            status        = closesocket(network->state->socket);
            socket_closed = (0 == status);
            if (socket_closed)
            {
                network->state->socket = INVALID_SOCKET;
            }
            else
            {
                error = (uint32_t)WSAGetLastError();
                RecordError(network, error);
            }
        }
    }
    closed = FALSE;
    if (socket_closed)
    {
        closed = OS_Memory_TryClose(&network->memory);
        if (closed)
        {
            network->state = NULL;
        }
        else
        {
            error = OS_Memory_GetError(&network->memory);
            RecordError(network, error);
        }
    }

    return closed;
}
