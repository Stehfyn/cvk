/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Native interface reader lifetime, startup failure and snapshot reuse.
 * Requires no receiver and sends no discovery traffic. The fixture checks explicit
 * owned storage identity; it is not a trace of hidden provider allocations.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <WinSock2.h>
#include <minwindef.h>
#include <stdint.h>
#include "os/os_types.h"
#include "os/os_network.h"
#include "cast_network.h"
#include "os/os_memory.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include "crt/crt_memory.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define NETWORK_TEST_LOOPBACK         (0x7F000001)
#define NETWORK_TEST_SENTINEL         (0xABCDEF01)
#define NETWORK_TEST_GUARD            (0xA5)
#define NETWORK_TEST_ADDRESS_CAPACITY (16)

#define NETWORK_TEST_REPETITIONS (32) ///< Independent snapshots on one retained allocation.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static OS_Network_t g_failed;
static OS_Network_t g_network;

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Checks a failed startup owner before Winsock is available.
 *
 * \par Requirements:
 *  - 001 Winsock shall not have been initialized in this process.
 *  - 002 Startup failure shall expose neither readiness nor snapshot entries.
 *  - 003 Repeated closure shall preserve the first failure and release retained memory.
 *
 * \retval None   Assertions pass or the process fails.
 */
static FORCE_INLINE void CheckFailedStartup(void);

/*************************************************************************************************/
/**
 * \brief Checks stable copied values for one published interface.
 *
 * \par Requirements:
 *  - 001 The index shall belong to the current exclusive reader snapshot.
 *  - 002 Comparisons shall cover fields, excluding unspecified structure padding.
 *
 * \param[in] index   Published interface index
 *
 * \retval    None   Assertions pass or the process fails.
 */
static FORCE_INLINE void CheckInterface(uint32_t index);

/*************************************************************************************************/
/**
 * \brief Checks complete interface snapshots using one startup allocation.
 *
 * \par Requirements:
 *  - 001 The reader shall be ready and exclusively owned.
 *  - 002 Query/getter calls shall preserve the owned region address and extent.
 *  - 003 Repeated getters shall copy identical values without consuming the snapshot.
 *
 * \retval None   Assertions pass or the process fails.
 */
static FORCE_INLINE void CheckSnapshots(void);

/*************************************************************************************************/
/**
 * \brief Checks repeated loopback route queries and transactional text output.
 *
 * \par Requirements:
 *  - 001 Independent values and guards shall be checked without relying on a round trip alone.
 *
 * \retval None   Assertions pass or the process fails.
 */
static FORCE_INLINE void CheckRoutes(void);

/*************************************************************************************************/
/**
 * \brief Checks route text capacity and invalid input preservation.
 *
 * \par Requirements:
 *  - 001 Failure shall preserve guarded output and success shall match independent bytes.
 *
 * \retval None   Assertions pass or the process fails.
 */
static FORCE_INLINE void CheckRouteOutput(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  This exercises real partial startup, without fabricating private native state.
 *  A residency acquisition failure is also required to produce a closable owner.
 */
static FORCE_INLINE void CheckFailedStartup(void)
{
    bool_t   ready;
    uint32_t error;
    bool_t   closed;

    ready = OS_Network_Initialize(&g_failed);
    LOGASSERT(!ready);
    LOGASSERT(!OS_Network_IsReady(&g_failed));
    LOGASSERT(0 == OS_Network_GetInterfaceCount(&g_failed));
    error = OS_Network_GetError(&g_failed);
    LOGASSERT(0 != error);
    closed = OS_Network_TryClose(&g_failed);
    LOGASSERT(closed);
    LOGASSERT(NULL == OS_Memory_GetAddress(&g_failed.memory));
    closed = OS_Network_TryClose(&g_failed);
    LOGASSERT(closed);
    LOGASSERT(error == OS_Network_GetError(&g_failed));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Both reads belong to the same snapshot, independent of live adapter changes.
 */
static FORCE_INLINE void CheckInterface(uint32_t index)
{
    OS_Network_Interface_t first;
    OS_Network_Interface_t second;
    int32_t                compared;

    OS_Network_GetInterface(&g_network, index, &first);
    OS_Network_GetInterface(&g_network, index, &second);
    compared = CRT_Memory_Compare(first.address, second.address, OS_NETWORK_IPV4_BYTE_COUNT);
    LOGASSERT(0 == compared);
    LOGASSERT(first.running == second.running);
    LOGASSERT(first.loopback == second.loopback);
    LOGASSERT(first.multicast == second.multicast);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Network state may change between queries, so counts and addresses are compared
 *  only within one published snapshot. All comparisons avoid structure padding.
 */
static FORCE_INLINE void CheckSnapshots(void)
{
    void*    address;
    uint64_t byte_count;
    uint32_t repetition;
    bool_t   read;
    uint32_t count;
    uint32_t index;

    address    = OS_Memory_GetAddress(&g_network.memory);
    byte_count = OS_Memory_GetByteCount(&g_network.memory);
    LOGASSERT(NULL != address);
    LOGASSERT(0 < byte_count);
    LOGASSERT(0 == OS_Network_GetInterfaceCount(&g_network));
    for (repetition = 0; repetition < NETWORK_TEST_REPETITIONS; ++repetition)
    {
        read = OS_Network_ReadInterfaces(&g_network);
        LOGASSERT(read);
        count = OS_Network_GetInterfaceCount(&g_network);
        LOGASSERT(OS_NETWORK_INTERFACE_CAPACITY >= count);
        LOGASSERT(address == OS_Memory_GetAddress(&g_network.memory));
        LOGASSERT(byte_count == OS_Memory_GetByteCount(&g_network.memory));
        for (index = 0; index < count; ++index)
        {
            CheckInterface(index);
        }
        LOGASSERT(count == OS_Network_GetInterfaceCount(&g_network));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only the local loopback route is queried; no datagram or receiver connection is created.
 */
static FORCE_INLINE void CheckRoutes(void)
{
    void*              storage;
    uint32_t           count;
    uint32_t           repetition;
    OS_Network_Route_t route;
    bool_t             read;
    char_t             output[NETWORK_TEST_ADDRESS_CAPACITY];
    int32_t            compared;
    storage = OS_Memory_GetAddress(&g_network.memory);
    count   = OS_Network_GetInterfaceCount(&g_network);
    for (repetition = 0; repetition < NETWORK_TEST_REPETITIONS; ++repetition)
    {
        route.destination = NETWORK_TEST_LOOPBACK;
        route.local       = NETWORK_TEST_SENTINEL;
        read              = OS_Network_ReadRoute(&g_network, &route);
        LOGASSERT(read);
        LOGASSERT(NETWORK_TEST_LOOPBACK == route.destination);
        LOGASSERT(NETWORK_TEST_LOOPBACK == route.local);
        LOGASSERT(storage == OS_Memory_GetAddress(&g_network.memory));
        LOGASSERT(count == OS_Network_GetInterfaceCount(&g_network));
        read = CastNetwork_ReadLocalAddress(&g_network, "127.0.0.1", sizeof("127.0.0.1"), output, sizeof(output));
        LOGASSERT(read);
        compared = CRT_Memory_Compare(output, "127.0.0.1", sizeof("127.0.0.1"));
        LOGASSERT(0 == compared);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Capacity exhaustion and rejected host syntax leave the previous output bytes untouched.
 */
static FORCE_INLINE void CheckRouteOutput(void)
{
    uint32_t capacity;
    char_t   output[NETWORK_TEST_ADDRESS_CAPACITY];
    bool_t   read;
    uint32_t index;
    for (capacity = 0; capacity < sizeof("127.0.0.1"); ++capacity)
    {
        CRT_Memory_Set(output, NETWORK_TEST_GUARD, sizeof(output));
        read = CastNetwork_ReadLocalAddress(&g_network, "127.0.0.1", sizeof("127.0.0.1"), output, capacity);
        LOGASSERT(!read);
        for (index = 0; index < sizeof(output); ++index)
        {
            LOGASSERT(NETWORK_TEST_GUARD == (byte_t)output[index]);
        }
    }
    read = CastNetwork_ReadLocalAddress(&g_network, "127.0.0.1", sizeof("127.0.0.1") - 1, output, sizeof(output));
    LOGASSERT(!read);
    read = CastNetwork_ReadLocalAddress(&g_network, "127.000.0.1", sizeof("127.000.0.1"), output, sizeof(output));
    LOGASSERT(!read);
    for (index = 0; index < sizeof(output); ++index)
    {
        LOGASSERT(NETWORK_TEST_GUARD == (byte_t)output[index]);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  All native owner initialization precedes snapshot exercises. Winsock shutdown
 *  follows verified closure; a second closure must issue no socket operations.
 */
uint32_t OS_Application_Main(void)
{
    int32_t status;
    WSADATA data;
    bool_t  ready;
    bool_t  closed;

    CheckFailedStartup();
    status = WSAStartup(MAKEWORD(2, 2), &data);
    LOGASSERT(0 == status);
    ready = OS_Network_Initialize(&g_network);
    LOGASSERT(ready);
    CheckSnapshots();
    CheckRoutes();
    CheckRouteOutput();
    closed = OS_Network_TryClose(&g_network);
    LOGASSERT(closed);
    LOGASSERT(!OS_Network_IsReady(&g_network));
    LOGASSERT(0 == OS_Network_GetInterfaceCount(&g_network));
    LOGASSERT(0 == OS_Network_GetError(&g_network));
    status = WSACleanup();
    LOGASSERT(0 == status);
    closed = OS_Network_TryClose(&g_network);
    LOGASSERT(closed);

    return 0;
}
