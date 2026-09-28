/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Route queries use startup-owned OS resources; IPv4 syntax belongs to utl_url. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cast_network.h"
#include "utl/utl_url.h"
#include "os/os_network.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Input is bounded by the longest IPv4address, not an unbounded string scan.
 *  Native failure cannot partially replace a previously published URL address.
 */
bool_t CastNetwork_ReadLocalAddress(OS_Network_t* network,
                                    const char_t* host,
                                    uint32_t      hostCapacity,
                                    char_t*       output,
                                    uint32_t      outputCapacity)
{
    uint32_t           count;
    bool_t             terminated;
    Utl_Url_Text_t     text;
    bool_t             valid;
    OS_Network_Route_t route;
    uint32_t           needed;
    uint32_t           written;
    LOGASSERT(NULL != network);
    LOGASSERT(OS_Network_IsReady(network));
    LOGASSERT((NULL != host) || (0 == hostCapacity));
    LOGASSERT((NULL != output) || (0 == outputCapacity));
    count      = 0;
    terminated = FALSE;
    for (; (count < hostCapacity) && (count <= UTL_URL_IPV4_MAX_BYTES); ++count)
    {
        if (UTL_URL_CHAR_NUL == host[count])
        {
            terminated = TRUE;
            break;
        }
    }
    text.data      = (const byte_t*)host;
    text.byteCount = count;
    valid          = terminated && (UTL_URL_IPV4_MIN_BYTES < outputCapacity);
    if (valid)
    {
        valid = Utl_Url_ReadIpv4(text, &route.destination);
    }
    if (valid)
    {
        valid = OS_Network_ReadRoute(network, &route);
    }
    if (valid)
    {
        needed = Utl_Url_GetIpv4ByteCount(route.local);
        valid  = (needed < outputCapacity);
        if (valid)
        {
            written = Utl_Url_WriteIpv4(route.local, (byte_t*)output, outputCapacity - 1);
            LOGASSERT(needed == written);
            output[written] = UTL_URL_CHAR_NUL;
        }
    }
    return valid;
}
