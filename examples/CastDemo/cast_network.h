#ifndef CAST_NETWORK_H
#define CAST_NETWORK_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/** CastDemo route-address presentation over the retained OS network owner and RFC 3986 IPv4 syntax. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_network.h"
#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

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
 * \brief Reads the current local IPv4 address toward a receiver into a bounded string.
 *
 * \par Requirements:
 *  - 001 The ready startup owner shall be exclusively held; missing required storage shall assert.
 *  - 002 Host shall be terminated within hostCapacity and match the complete RFC 3986 IPv4address grammar.
 *  - 003 Output shall be disjoint from the owner and input; insufficient capacity or failure shall preserve it.
 *  - 004 Successful output shall include a terminator; native queries shall use the retained startup socket.
 *  - 005 No native resource shall be acquired or released; native latency and provider allocation remain unproved.
 *
 * \param[in,out] network          Ready exclusive startup owner
 * \param[in]     host             Receiver IPv4 text
 * \param[in]     hostCapacity     Complete readable source extent, including the terminator
 * \param[out]    output           Disjoint output storage
 * \param[in]     outputCapacity   Writable output extent
 *
 * \retval        TRUE    A complete terminated address is published.
 * \retval        FALSE   Invalid syntax, native failure or capacity exhaustion; output is unchanged.
 */
bool_t CastNetwork_ReadLocalAddress(OS_Network_t* network,
                                    const char_t* host,
                                    uint32_t      hostCapacity,
                                    char_t*       output,
                                    uint32_t      outputCapacity);

#endif /* CAST_NETWORK_H */
