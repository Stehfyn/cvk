#ifndef UTL_SSDP_H
#define UTL_SSDP_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * UPnP Device Architecture 1.0/1.1, sections 1.1 and 1.3:
 * https://upnp.org/specs/arch/UPnP-arch-DeviceArchitecture-v1.1.pdf
 * This module implements the IPv4 multicast M-SEARCH exchange used by CastDemo.
 * HTTP octet syntax belongs to utl_http. URL resolution, device XML, datagram
 * transport, expiry scheduling and advertisement subscriptions are separate layers.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_http.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/* SSDP discovery grammar and IPv4 multicast endpoint, UDA section 1.3. */
#define UTL_SSDP_MULTICAST_ADDRESS  ("239.255.255.250")
#define UTL_SSDP_MULTICAST_PORT     (1900)
#define UTL_SSDP_MULTICAST_HOST     ("239.255.255.250:1900")
#define UTL_SSDP_METHOD_SEARCH      ("M-SEARCH")
#define UTL_SSDP_REQUEST_TARGET     ("*")
#define UTL_SSDP_DISCOVER           ("\"ssdp:discover\"")
#define UTL_SSDP_ALL                ("ssdp:all")
#define UTL_SSDP_ROOT_DEVICE        ("upnp:rootdevice")
#define UTL_SSDP_UUID_PREFIX        ("uuid:")
#define UTL_SSDP_URN_PREFIX         ("urn:")
#define UTL_SSDP_DEVICE_KIND        ("device")
#define UTL_SSDP_SERVICE_KIND       ("service")
#define UTL_SSDP_FIELD_MAN          ("MAN")
#define UTL_SSDP_FIELD_MX           ("MX")
#define UTL_SSDP_FIELD_ST           ("ST")
#define UTL_SSDP_FIELD_USN          ("USN")
#define UTL_SSDP_FIELD_LOCATION     ("LOCATION")
#define UTL_SSDP_FIELD_CACHE        ("CACHE-CONTROL")
#define UTL_SSDP_FIELD_SERVER       ("SERVER")
#define UTL_SSDP_FIELD_EXT          ("EXT")
#define UTL_SSDP_MAX_AGE            ("max-age")
#define UTL_SSDP_REASON_OK          ("OK")
#define UTL_SSDP_SEARCH_FIELD_COUNT (4)
#define UTL_SSDP_WAIT_DIGITS        (10) ///< uint32_t decimal storage; application type bound.
#define UTL_SSDP_VISIBLE_FIRST      (0x21)
#define UTL_SSDP_VISIBLE_LAST       (0x7E)

#define UTL_SSDP_URN_COMPONENT_COUNT (3) ///< Domain, kind and type precede the version.
#define UTL_SSDP_URN_KIND_INDEX      (1) ///< Kind follows the domain component.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Borrowed IPv4 multicast search with a caller-selected response window.
 *
 * \par Details:
 *  maximumWait is at least one second. Values above five are permitted by UDA;
 *  devices may use a shorter response window. No socket or timer belongs here.
 */
typedef struct Utl_Ssdp_Search_s
{
    Utl_Http_Text_t target;      ///< Search target URI; ssdp:all is allowed.
    uint32_t        maximumWait; ///< Requested maximum response delay in seconds.
} Utl_Ssdp_Search_t;

/*************************************************************************************************/
/**
 * \brief Borrowed UPnP 1.0-compatible search response used by a control point.
 *
 * \par Details:
 *  Every span borrows the input retained by its parsed HTTP head. Unknown fields
 *  remain in that head and can be read by a higher-version or DIAL adapter.
 *  URI semantics of location and product semantics of server are separate from
 *  this search envelope; receiving COMPLETE is not URL/DNS validation.
 */
typedef struct Utl_Ssdp_Response_s
{
    Utl_Http_Text_t target;     ///< Echoed target; never ssdp:all.
    Utl_Http_Text_t usn;        ///< Full identity, suitable for per-service deduplication.
    Utl_Http_Text_t uuid;       ///< UUID including uuid: prefix, before the optional suffix.
    Utl_Http_Text_t location;   ///< Root device-description URL, not normalized or resolved.
    Utl_Http_Text_t server;     ///< Server product field preserved verbatim.
    uint64_t        maximumAge; ///< Advertisement lifetime; local uint64_t arithmetic bound.
} Utl_Ssdp_Response_t;

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
 * \brief Constructs an IPv4 multicast search from caller-owned target bytes.
 *
 * \par Details:
 *  Target storage is borrowed until every size/write call is complete.
 *
 * \par Requirements:
 *  - 001 Missing output, invalid target or a zero wait shall assert as development errors.
 *  - 002 The operation shall create no resource, allocate nothing and perform no I/O.
 *
 * \param[out] search        Non-NULL caller-owned model
 * \param[in]  target        Valid counted target URI
 * \param[in]  maximumWait   Positive response window in seconds
 *
 * \retval     None   No result value.
 */
void Utl_Ssdp_InitializeSearch(Utl_Ssdp_Search_t* search, Utl_Http_Text_t target, uint32_t maximumWait);

/*************************************************************************************************/
/**
 * \brief Recognizes the SSDP search-target alternatives.
 *
 * \par Details:
 *  UUID identifiers accept the noncanonical visible strings required for UPnP 1.0
 *  compatibility. Device/service URNs require nonempty domain/type and positive version.
 *
 * \par Requirements:
 *  - 001 ssdp:all, upnp:rootdevice, UUID and device/service URNs shall be recognized.
 *  - 002 Comparisons shall be case-sensitive; missing, empty or whitespace-bearing values shall fail.
 *
 * \param[in] target   Borrowed counted search target
 *
 * \retval    TRUE    Supported search-target syntax.
 * \retval    FALSE   Invalid target syntax.
 */
bool_t Utl_Ssdp_IsTarget(Utl_Http_Text_t target);

/*************************************************************************************************/
/**
 * \brief Derives the complete M-SEARCH datagram size.
 *
 * \par Details:
 *  The count includes the final blank line and excludes any C string terminator.
 *
 * \par Requirements:
 *  - 001 Missing model storage shall assert.
 *  - 002 Invalid target or zero wait shall return zero.
 *  - 003 No output storage, allocation, CRT or OS operation shall be needed.
 *
 * \param[in] search   Non-NULL borrowed search model
 *
 * \retval    1..UINT32_MAX   Complete wire byte count.
 * \retval    0               Invalid or unrepresentable model.
 */
uint32_t Utl_Ssdp_GetSearchByteCount(const Utl_Ssdp_Search_t* search);

/*************************************************************************************************/
/**
 * \brief Reads an IPv4 multicast M-SEARCH head into a typed search.
 *
 * \par Details:
 *  The input head is already parsed by utl_http. Datagram completion and transport
 *  address selection belong to the caller. Unrecognized extension fields are retained
 *  in the original head and ignored by this version.
 *
 * \par Requirements:
 *  - 001 Missing pointers shall assert; model/head/field storage shall be disjoint.
 *  - 002 Required singleton fields shall reject duplicates, including identical duplicates.
 *  - 003 Invalid wire values shall leave the complete output unchanged.
 *  - 004 The uint32_t MX bound is a local arithmetic limit, not a normative five-second maximum.
 *
 * \param[out] search   Non-NULL candidate published only on success
 * \param[in]  head     Parsed immutable HTTP head
 *
 * \retval     TRUE    A valid supported multicast search was read.
 * \retval     FALSE   Wrong start line, invalid or missing fields, or unsupported MX width.
 */
bool_t Utl_Ssdp_ReadSearch(Utl_Ssdp_Search_t* search, const Utl_Http_Head_t* head);

/*************************************************************************************************/
/**
 * \brief Reads the control-point search response envelope and identity.
 *
 * \par Details:
 *  The model covers the UPnP 1.0-compatible required search fields. UDA 1.1 boot/config
 *  and search-port extensions remain available in the original head. URL and server
 *  product semantics require their own modules; this function does not resolve URLs.
 *
 * \par Requirements:
 *  - 001 Missing pointers shall assert; input and output storage shall be disjoint.
 *  - 002 The HTTP/1.1 200 OK start line and all six required singleton fields shall be checked.
 *  - 003 ST and USN shall agree; ssdp:all shall not be accepted as a response target.
 *  - 004 Duplicate max-age and overflowing lifetimes shall fail; unknown directives shall be skipped.
 *  - 005 Failure shall preserve the complete output; no allocation, CRT, OS resource or I/O shall occur.
 *
 * \param[out] response   Non-NULL borrowed result, published only on success
 * \param[in]  head       Parsed immutable HTTP response head
 *
 * \retval     TRUE    Required envelope fields and identity were read.
 * \retval     FALSE   Malformed, missing, duplicate or inconsistent required envelope data.
 */
bool_t Utl_Ssdp_ReadResponse(Utl_Ssdp_Response_t* response, const Utl_Http_Head_t* head);

/*************************************************************************************************/
/**
 * \brief Serializes the complete IPv4 multicast M-SEARCH datagram.
 *
 * \par Details:
 *  The writer first validates and measures the complete head. It never appends a
 *  C terminator or partially publishes a datagram on capacity failure.
 *
 * \par Requirements:
 *  - 001 Required pointers shall assert as programmer misuse.
 *  - 002 Invalid models and insufficient capacity shall preserve every output byte.
 *  - 003 Wire fields shall use CRLF and the final empty line; no body or Content-Length shall be emitted.
 *
 * \param[in]  search     Non-NULL initialized search model
 * \param[out] buffer     Non-NULL output disjoint from model and target
 * \param[in]  capacity   Writable output bytes
 *
 * \retval     1..capacity   Complete datagram bytes written.
 * \retval     0             Invalid model or insufficient output capacity; output unchanged.
 */
uint32_t Utl_Ssdp_WriteSearch(const Utl_Ssdp_Search_t* search, char_t* buffer, uint32_t capacity);

#endif /* UTL_SSDP_H */
