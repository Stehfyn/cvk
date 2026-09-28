#ifndef CAST_SSDP_H
#define CAST_SSDP_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * SSDP discovery uses utl_ssdp/utl_http; DIAL device descriptions are fetched separately.
 * https://upnp.org/specs/arch/UPnP-arch-DeviceArchitecture-v1.1.pdf
 * Native description requests and XML/URL parsing remain migration boundaries.
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

#define CAST_MAX_DEVICES        (16)  ///< Local discovery output capacity.
#define CAST_DEVICE_NAME_BYTES  (128) ///< Terminated UTF-8 display name capacity.
#define CAST_DEVICE_MODEL_BYTES (64)  ///< Terminated UTF-8 model capacity.
#define CAST_DEVICE_HOST_BYTES  (64)  ///< Terminated host text capacity.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief One described Cast receiver, independent of discovery workspace lifetime.
 */
typedef struct CastDevice
{
    char_t friendlyName[CAST_DEVICE_NAME_BYTES]; ///< Receiver display name.
    char_t model[CAST_DEVICE_MODEL_BYTES];       ///< Optional model name.
    char_t host[CAST_DEVICE_HOST_BYTES];         ///< Receiver host text.
} CastDevice;

/*************************************************************************************************/
/**
 * \brief Startup-owned discovery resources, used by one worker at a time.
 *
 * \par Details:
 *  Do not copy or move a live owner. The Win32 backend retains resident buffers,
 *  interface-query and search sockets, and one HTTP session. HTTP connection and
 *  request creation still occur during description fetches and remain open defects.
 */
typedef struct Ssdp_s
{
    OS_Memory_t          memory; ///< Startup storage owner, valid after failed initialization.
    struct Ssdp_State_s* state;  ///< Native state, absent unless storage became ready.
    bool_t               ready;  ///< Initialization succeeded and closure has not begun.
    uint32_t             error;  ///< First failure in the latest operation or closure attempt.
} Ssdp_t;

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
 * \brief Establishes discovery storage and persistent native resources at startup.
 *
 * \par Requirements:
 *  - 001 A missing owner shall assert; owner storage shall be fresh or successfully closed.
 *  - 002 Winsock shall remain initialized until the owner's TryClose succeeds.
 *  - 003 All explicit discovery buffers and query/search sockets shall be created here only.
 *  - 004 Any acquisition failure shall leave a valid owner requiring TryClose.
 *
 * \param[out] discovery   Required owner, alive through successful closure
 *
 * \retval     TRUE    Discovery operations are available.
 * \retval     FALSE   Startup failed; all retained resources still require closure.
 */
bool_t Ssdp_Initialize(Ssdp_t* discovery);

/*************************************************************************************************/
/**
 * \brief Gets the first recorded failure in the current operation.
 *
 * \par Requirements:
 *  - 001 The caller shall acquire worker completion before reading worker-written state.
 *  - 002 A new discovery/description operation shall clear the prior operation's code.
 *
 * \param[in] discovery   Required initialized owner, exclusively held
 *
 * \retval    0         No native or local-capacity failure was recorded.
 * \retval    nonzero   Native Win32/Winsock or local-capacity failure code.
 */
uint32_t Ssdp_GetError(const Ssdp_t* discovery);

/*************************************************************************************************/
/**
 * \brief Searches operational non-loopback multicast IPv4 interfaces and describes responders.
 *
 * \par Requirements:
 *  - 001 Missing or unready owner storage shall assert.
 *  - 002 A missing output shall assert unless capacity is zero; outputs shall be disjoint from the owner.
 *  - 003 Zero capacity or timeout shall return zero without native discovery operations.
 *  - 004 At most CAST_MAX_DEVICES complete values shall be published, deduplicated by SSDP USN.
 *  - 005 Retained buffers and sockets shall be reused without application allocation or growth.
 *  - 006 The timeout shall bound the reply-collection window; HTTP fetch latency remains separate.
 *  - 007 The caller shall serialize discovery, description, getters and closure.
 *
 * \param[in,out] discovery   Ready startup owner
 * \param[out]    devices     Required output extent unless capacity is zero
 * \param[in]     capacity    Number of writable receiver slots
 * \param[in]     timeoutMs   Reply-collection interval in milliseconds
 *
 * \retval        count   Number of described receivers written to devices.
 */
uint32_t Ssdp_DiscoverCastDevices(Ssdp_t* discovery, CastDevice* devices, uint32_t capacity, uint32_t timeoutMs);

/*************************************************************************************************/
/**
 * \brief Describes a known receiver through its conventional DIAL description endpoint.
 *
 * \par Requirements:
 *  - 001 Required ready owner, host and output pointers shall assert.
 *  - 002 Host text shall terminate within CAST_DEVICE_HOST_BYTES bytes or fail without truncation.
 *  - 003 Host text may alias device.host; the complete location shall be staged before output changes.
 *  - 004 Failure shall preserve the complete caller output.
 *
 * \param[in,out] discovery   Ready startup owner, exclusively held
 * \param[in]     host        Terminated receiver host text
 * \param[out]    device      Required output disjoint from owner storage
 *
 * \retval        TRUE    A complete receiver value was published.
 * \retval        FALSE   The description or native operation failed; output is unchanged.
 */
bool_t Ssdp_DescribeHost(Ssdp_t* discovery, const char_t* host, CastDevice* device);

/*************************************************************************************************/
/**
 * \brief Releases discovery resources after every user and worker is quiescent.
 *
 * \par Requirements:
 *  - 001 Required initialized owner storage shall assert, including after failed startup.
 *  - 002 The first call shall revoke readiness, including when native release fails.
 *  - 003 Failed native resource releases shall retain their identities for retry.
 *  - 004 Workspace release shall follow successful release of every retained native resource.
 *  - 005 Repeated successful closure shall perform no additional native releases.
 *
 * \param[in,out] discovery   Quiescent owner, retained until closure succeeds
 *
 * \retval        TRUE    All owned resources have been released.
 * \retval        FALSE   Retain the owner and retry closure.
 */
bool_t Ssdp_TryClose(Ssdp_t* discovery);

#endif /* CAST_SSDP_H */
