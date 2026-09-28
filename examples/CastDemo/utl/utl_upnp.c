/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * UPnP Device Architecture, sections 2.3 and 2.8.
 * https://openconnectivity.org/upnp-specs/UPnP-arch-DeviceArchitecture-v2.0-20200417.pdf
 * Root-device display identity; complete profile validation remains unfinished.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_upnp.h"
#include "utl/utl_xml.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
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

/*************************************************************************************************/
/**
 * \brief Constructs an expanded UPnP device-schema name.
 *
 * \par Requirements:
 *  - 001 Local name shall be a complete stable literal with byteCount readable bytes.
 *
 * \param[in] local       Literal local name
 * \param[in] byteCount   Exact local name extent
 *
 * \retval    name   Expanded schema name.
 */
static FORCE_INLINE Utl_Xml_Name_t GetName(const char_t* local, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Selects at most one direct field with a specified expanded name.
 *
 * \par Requirements:
 *  - 001 Document and parent shall belong to a completed tree.
 *  - 002 Duplicate direct fields shall fail; an absent field shall publish NULL.
 *
 * \param[in]  document   Completed document
 * \param[in]  parent     Field owner
 * \param[in]  name       Expanded schema name
 * \param[out] field      Disjoint selected node result
 *
 * \retval     TRUE    Zero or one field was found.
 * \retval     FALSE   Duplicate fields make the selection ambiguous.
 */
static FORCE_INLINE bool_t FindUnique(const Utl_Xml_Document_t* document,
                                      const Utl_Xml_Node_t*     parent,
                                      Utl_Xml_Name_t            name,
                                      const Utl_Xml_Node_t**    field);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Prefix spellings are resolved by the XML module before this schema adapter runs.
 */
static FORCE_INLINE Utl_Xml_Name_t GetName(const char_t* local, uint32_t byteCount)
{
    Utl_Xml_Name_t name;
    name.space.data      = (const byte_t*)UTL_UPNP_DEVICE_NAMESPACE;
    name.space.byteCount = sizeof(UTL_UPNP_DEVICE_NAMESPACE) - 1;
    name.space.kind      = UTL_XML_TEXT_LITERAL;
    name.local.data      = (const byte_t*)local;
    name.local.byteCount = byteCount;
    name.local.kind      = UTL_XML_TEXT_LITERAL;
    return name;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Resuming at next exposes duplicates without entering descendants or vendor namespaces.
 */
static FORCE_INLINE bool_t FindUnique(const Utl_Xml_Document_t* document,
                                      const Utl_Xml_Node_t*     parent,
                                      Utl_Xml_Name_t            name,
                                      const Utl_Xml_Node_t**    field)
{
    const Utl_Xml_Node_t* found;
    const Utl_Xml_Node_t* duplicate;
    bool_t                valid;
    found     = Utl_Xml_FindSibling(document, parent->firstChild, name);
    duplicate = NULL;
    if (NULL != found)
    {
        duplicate = Utl_Xml_FindSibling(document, found->next, name);
    }
    valid = (NULL == duplicate);
    if (valid)
    {
        *field = found;
    }
    return valid;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Utl_Upnp_InitializeDescription(Utl_Upnp_Description_t* description)
{
    LOGASSERT(NULL != description);
    description->device       = NULL;
    description->friendlyName = NULL;
    description->modelName    = NULL;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Displaying a receiver does not require a model label. Missing model text remains
 *  distinguishable from invalid or duplicated model fields; full profile validation is separate.
 */
bool_t Utl_Upnp_ReadDescription(const Utl_Xml_Document_t* document, Utl_Upnp_Description_t* description)
{
    Utl_Upnp_Description_t candidate;
    bool_t                 valid;
    uint32_t               count;
    LOGASSERT(NULL != document);
    LOGASSERT(NULL != description);
    LOGASSERT(UTL_XML_COMPLETE == document->result);
    LOGASSERT(NULL != document->root);
    Utl_Upnp_InitializeDescription(&candidate);
    valid = Utl_Xml_IsElement(document->root, GetName(UTL_UPNP_ROOT_NAME, sizeof(UTL_UPNP_ROOT_NAME) - 1));
    if (valid)
    {
        valid = FindUnique(document,
                           document->root,
                           GetName(UTL_UPNP_DEVICE_NAME, sizeof(UTL_UPNP_DEVICE_NAME) - 1),
                           &candidate.device);
        valid = valid && (NULL != candidate.device);
    }
    if (valid)
    {
        valid = FindUnique(document,
                           candidate.device,
                           GetName(UTL_UPNP_FRIENDLY_NAME, sizeof(UTL_UPNP_FRIENDLY_NAME) - 1),
                           &candidate.friendlyName);
        valid = valid && (NULL != candidate.friendlyName);
    }
    if (valid)
    {
        valid = Utl_Xml_GetElementTextByteCount(candidate.friendlyName, &count);
        if (valid)
        {
            valid = (0 < count);
        }
    }
    if (valid)
    {
        valid = FindUnique(document,
                           candidate.device,
                           GetName(UTL_UPNP_MODEL_NAME, sizeof(UTL_UPNP_MODEL_NAME) - 1),
                           &candidate.modelName);
    }
    if (valid && (NULL != candidate.modelName))
    {
        valid = Utl_Xml_GetElementTextByteCount(candidate.modelName, &count);
    }
    if (valid)
    {
        *description = candidate;
    }
    return valid;
}
