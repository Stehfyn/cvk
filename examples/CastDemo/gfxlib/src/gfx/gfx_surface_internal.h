#ifndef GFX_SURFACE_INTERNAL_H
#define GFX_SURFACE_INTERNAL_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Private surface policy shared by the build-selected native implementations. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_surface.h"
#include "cvk/cvk.h"

#include <vulkan/vulkan_core.h>

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
 * \brief Surface owner and bounded query storage acquired together during graphics initialization.
 *
 * \par Details:
 *  Display, mode and plane enumeration run in sequence. Only the active union member is read;
 *  selected handles, dimensions and flags are copied before the next query reuses the storage.
 */
typedef struct Gfx_Surface_Storage_s
{
    Gfx_Surface_t surface; ///< Public surface state retained through the owner's lifetime.
    union
    {
        VkDisplayPropertiesKHR      displays[CVK_MAX_PROPERTIES]; ///< Physical-display query output.
        VkDisplayModePropertiesKHR  modes[CVK_MAX_PROPERTIES];    ///< Selected display's mode query output.
        VkDisplayPlanePropertiesKHR planes[CVK_MAX_PROPERTIES];   ///< Physical-device plane query output.
    } queries; ///< Initialization-only enumeration workspace; never exposed to callers.
} Gfx_Surface_Storage_t;

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
 * \brief Initializes a presentation surface for the selected physical display and mode.
 *
 * \par Details:
 *  This is a private implementation entry point, not an addition to the public Gfx_ interface.
 *  The caller creates the native surface during graphics initialization.
 *
 * \par Requirements:
 *  - 001 Both objects and the instance and physical-device handles shall be valid; misuse shall assert.
 *  - 002 Display and mode indices shall designate currently enumerated entries.
 *  - 003 The selected plane shall support the display and be unassigned or already assigned to it.
 *  - 004 Enumeration and capability queries shall precede surface creation.
 *  - 005 Output state shall be published only after the CVK construction operation completes.
 *
 * \param[out] storage     Unpublished startup-owned surface and its enumeration workspace
 * \param[in]  createInfo  Direct-presentation selection and initialized device context
 *
 * \retval     None No value is returned.
 */
void Gfx_Surface_InitializeDirect(Gfx_Surface_Storage_t* storage, const Gfx_Surface_CreateInfo_t* createInfo);

#endif /* GFX_SURFACE_INTERNAL_H */
