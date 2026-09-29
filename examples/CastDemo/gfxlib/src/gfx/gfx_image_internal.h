#ifndef GFX_IMAGE_INTERNAL_H
#define GFX_IMAGE_INTERNAL_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Private image construction boundary; not part of the frozen public gfxlib interface. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_image.h"

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
 * \brief Initializes an image owner with its selected platform sharing representation.
 *
 * \par Requirements:
 *  - 001 Initialization shall occur only during application startup, before interaction or playback.
 *  - 002 Image, configuration and platform configuration shall be non-NULL and dimensions nonzero.
 *  - 003 The requested kind shall be IMPORT or EXPORT with a valid VA or Vulkan backend.
 *  - 004 Devices and borrowed native sharing objects shall outlive the initialized image.
 *  - 005 Construction shall initialize all image state before publishing the owner to consumers.
 *  - 006 Windows VA export shall replace and close the input handle only after obtaining its export handle.
 *  - 007 Linux Vulkan imports shall retain caller descriptor ownership by duplicating imported descriptors.
 *
 * \par Details:
 *  Windows supports VA import/export and Vulkan import; Vulkan export is unsupported.
 *  Linux supports both backends' import/export paths. Runtime driver failures still follow
 *  the pinned dependencies' failure contracts; this declaration does not establish recovery
 *  or no-allocation evidence for those providers.
 *
 * \param[out] image   Unpublished image owner with no live resources
 * \param[in] createInfo   Validated startup configuration and native sharing handles
 *
 * \retval None   No result value.
 */
void Gfx_Image_InitializeShared(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

#endif /* GFX_IMAGE_INTERNAL_H */
