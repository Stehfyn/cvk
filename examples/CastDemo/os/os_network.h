#ifndef OS_NETWORK_H
#define OS_NETWORK_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Fixed-capacity IPv4 interface and route snapshots; native implementations are build selected.
 * https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ioctls
 * SIO_GET_INTERFACE_LIST returns one INTERFACE_INFO per unicast address.
 * SIO_ROUTING_INTERFACE_QUERY derives the current local source address for a destination.
 * Native query latency and provider-internal allocation remain external boundaries.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_memory.h"
#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define OS_NETWORK_INTERFACE_CAPACITY (128) ///< Local snapshot limit, not a protocol limit.
#define OS_NETWORK_IPV4_BYTE_COUNT    (4)   ///< IPv4 address octets in network order.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Numeric IPv4 route query; the first octet occupies bits 31 through 24. */
typedef struct OS_Network_Route_s
{
    uint32_t destination; ///< Caller-selected remote address; preserved by queries.
    uint32_t local;       ///< Published source address; unchanged when a query fails.
} OS_Network_Route_t;

/*************************************************************************************************/
/**
 * \brief One IPv4 unicast address and its native interface capabilities.
 */
typedef struct OS_Network_Interface_s
{
    byte_t address[OS_NETWORK_IPV4_BYTE_COUNT]; ///< Network-order address bytes.
    bool_t running;                             ///< Interface is operational.
    bool_t loopback;                            ///< Interface is a loopback path.
    bool_t multicast;                           ///< Interface supports multicast.
} OS_Network_Interface_t;

/*************************************************************************************************/
/**
 * \brief Startup-owned interface reader with private native storage.
 *
 * \par Details:
 *  Do not copy or move a live reader. All calls require exclusive ownership;
 *  returned interface values are copies and do not extend native lifetime.
 */
typedef struct OS_Network_s
{
    OS_Memory_t                memory; ///< Startup allocation, retained after partial failure.
    struct OS_Network_State_s* state;  ///< Backend object, absent unless storage became ready.
    bool_t                     ready;  ///< Startup completed and closure has not begun.
    uint32_t                   error;  ///< First failure since startup or the latest query began.
} OS_Network_t;

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
 * \brief Acquires fixed interface storage and the native query resource at startup.
 *
 * \par Requirements:
 *  - 001 A missing owner shall assert; storage shall be fresh or successfully closed.
 *  - 002 The Win32 caller shall keep Winsock initialized until TryClose succeeds.
 *  - 003 Native acquisition failure shall leave a closable initialized owner.
 *  - 004 No snapshot shall be published before a successful ReadInterfaces.
 *
 * \param[out] network   Required startup owner
 *
 * \retval     TRUE    The reader is ready.
 * \retval     FALSE   Startup failed; TryClose is still required.
 */
bool_t OS_Network_Initialize(OS_Network_t* network);

/*************************************************************************************************/
/**
 * \brief Queries startup readiness without native operations.
 *
 * \par Requirements:
 *  - 001 Failed startup and any closure attempt shall report FALSE.
 *
 * \param[in] network   Required initialized owner
 *
 * \retval    TRUE    Interface queries may be issued.
 * \retval    FALSE   The reader is unavailable.
 */
bool_t OS_Network_IsReady(const OS_Network_t* network);

/*************************************************************************************************/
/**
 * \brief Gets the first native error since startup or the latest query began.
 *
 * \par Requirements:
 *  - 001 Each new query shall clear previous query evidence before issuing native work.
 *  - 002 Getters and closure shall preserve the first recorded failure until the next query or initialization.
 *
 * \param[in] network   Required initialized owner
 *
 * \retval    0         No native failure has been recorded.
 * \retval    nonzero   Backend-specific failure code.
 */
uint32_t OS_Network_GetError(const OS_Network_t* network);

/*************************************************************************************************/
/**
 * \brief Gets the complete published snapshot's address count.
 *
 * \par Requirements:
 *  - 001 Failed queries and unavailable owners shall expose zero addresses.
 *
 * \param[in] network   Required initialized owner
 *
 * \retval    count   Number of accessible interface values, at most the fixed capacity.
 */
uint32_t OS_Network_GetInterfaceCount(const OS_Network_t* network);

/*************************************************************************************************/
/**
 * \brief Copies one published interface into caller storage.
 *
 * \par Details:
 *  One interface value is copied.
 *
 * \par Requirements:
 *  - 001 A missing output, unready owner or unpublished index shall assert.
 *  - 002 Output storage shall be disjoint from the owner and its native storage.
 *  - 003 Native address bytes shall retain network byte order.
 *
 * \param[in]  network   Required ready owner with a published snapshot
 * \param[in]  index     Index less than GetInterfaceCount
 * \param[out] value     Required disjoint output
 *
 * \retval     None
 */
void OS_Network_GetInterface(const OS_Network_t* network, uint32_t index, OS_Network_Interface_t* value);

/*************************************************************************************************/
/**
 * \brief Replaces the snapshot using the retained native query resource.
 *
 * \par Requirements:
 *  - 001 A missing or unready owner shall assert.
 *  - 002 One native query shall use only the fixed startup-owned output buffer.
 *  - 003 Overflow, invalid native output or native failure shall publish no entries.
 *  - 004 All IPv4 unicast entries shall be preserved; no partial list shall be published.
 *
 * \param[in,out] network   Required ready owner, exclusively held
 *
 * \retval        TRUE    A complete snapshot, possibly empty, is available.
 * \retval        FALSE   No snapshot is available; native error evidence is retained.
 */
bool_t OS_Network_ReadInterfaces(OS_Network_t* network);

/*************************************************************************************************/
/**
 * \brief Queries the current local IPv4 route using the retained startup resource.
 *
 * \par Requirements:
 *  - 001 Required initialized ready owner and result storage shall assert on misuse.
 *  - 002 The caller shall exclude other queries and lifetime operations for this owner.
 *  - 003 Success shall publish the current local source address; failure shall preserve both route fields.
 *  - 004 No socket, storage or thread shall be created or released by this operation.
 *  - 005 The snapshot shall not promise a persistent route or a bounded native query latency.
 *
 * \param[in,out] network   Exclusive startup owner
 * \param[in,out] route     Destination and staged source result
 *
 * \retval        TRUE    A current local address is published.
 * \retval        FALSE   Native failure; route is unchanged and error evidence is retained.
 */
bool_t OS_Network_ReadRoute(OS_Network_t* network, OS_Network_Route_t* route);

/*************************************************************************************************/
/**
 * \brief Releases a quiescent owner, retaining failed releases for retry.
 *
 * \par Requirements:
 *  - 001 The caller shall exclude every query and getter before closure begins.
 *  - 002 Readiness shall be revoked on the first attempt.
 *  - 003 Native storage shall outlive every retained native resource.
 *  - 004 Repeated successful closure shall issue no further native releases.
 *
 * \param[in,out] network   Required initialized owner, even after failed startup
 *
 * \retval        TRUE    All owned resources are released.
 * \retval        FALSE   Preserve owner storage and retry closure.
 */
bool_t OS_Network_TryClose(OS_Network_t* network);

#endif /* OS_NETWORK_H */
