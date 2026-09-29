#ifndef UTL_UPNP_H
#define UTL_UPNP_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * UPnP Device Architecture, section 2.3 device descriptions and section 2.8 schema.
 * https://openconnectivity.org/upnp-specs/UPnP-arch-DeviceArchitecture-v2.0-20200417.pdf
 * This model selects the root-device display fields consumed by CastDemo.
 * Complete device/service/profile validation remains an unfinished responsibility.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#include "utl/utl_xml.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UTL_UPNP_DEVICE_NAMESPACE ("urn:schemas-upnp-org:device-1-0")
#define UTL_UPNP_ROOT_NAME        ("root")
#define UTL_UPNP_DEVICE_NAME      ("device")
#define UTL_UPNP_FRIENDLY_NAME    ("friendlyName")
#define UTL_UPNP_MODEL_NAME       ("modelName")

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Borrowed root-device identity from a completed description document.
 */
typedef struct Utl_Upnp_Description_s
{
    const Utl_Xml_Node_t* device;       ///< Unique direct root device.
    const Utl_Xml_Node_t* friendlyName; ///< Unique nonempty simple-content display name.
    const Utl_Xml_Node_t* modelName;    ///< Unique simple-content model, or NULL if omitted.
} Utl_Upnp_Description_t;

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
 * \brief Establishes an empty description identity without acquiring storage.
 *
 * \par Details:
 *  Empty identity is established.
 *
 * \par Requirements:
 *  - 001 Missing output shall assert as a development error.
 *  - 002 Every borrowed field shall become NULL.
 *
 * \param[out] description   Required exclusive result
 *
 * \retval     None
 */
void Utl_Upnp_InitializeDescription(Utl_Upnp_Description_t* description);

/*************************************************************************************************/
/**
 * \brief Selects the root-device display identity by namespace and hierarchy.
 *
 * \par Requirements:
 *  - 001 Required pointers and a completed immutable XML document shall assert.
 *  - 002 Only the UPnP root and its unique direct device child shall be selected.
 *  - 003 Friendly name shall be unique, nonempty simple content; model shall be unique simple content if present.
 *  - 004 Unknown elements and namespaces shall be ignored; embedded devices shall not provide root fields.
 *  - 005 Failure shall preserve the caller result; successful borrows remain valid only while document/source are retained.
 *  - 006 No allocation, OS or CRT operation shall occur.
 *
 * \param[in]  document      Successfully parsed immutable XML document
 * \param[out] description   Required disjoint identity result
 *
 * \retval     TRUE    A complete usable display identity is published.
 * \retval     FALSE   Root-device display identity is missing, ambiguous or structurally invalid.
 */
bool_t Utl_Upnp_ReadDescription(const Utl_Xml_Document_t* document, Utl_Upnp_Description_t* description);

#endif /* UTL_UPNP_H */
