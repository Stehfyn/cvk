#ifndef VULKAN_VA_ZERO_COPY_GFX_DISPLAY_H
#define VULKAN_VA_ZERO_COPY_GFX_DISPLAY_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"

#include <stdint.h>

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
 * \brief Owns native display handles and tracks the display extent and event state.
 */
typedef struct Gfx_Display_t Gfx_Display_t;

/*************************************************************************************************/
/**
 * \brief Selects the presentation environment created for a display.
 */
typedef enum Gfx_Display_Kind_t
{
    GFX_DISPLAY_KIND_WINDOWED = 0, ///< Native desktop window with an event queue.
    GFX_DISPLAY_KIND_DIRECT,       ///< Vulkan direct-display presentation without a desktop window.
    GFX_DISPLAY_KIND_HEADLESS,     ///< Display extent for an offscreen surface, without a native window.
} Gfx_Display_Kind_t;

/*************************************************************************************************/
/**
 * \brief Selects a display environment and its requested dimensions.
 *
 * \par Details:
 *  Windowed and headless presentation use the requested dimensions. A direct presentation
 *  surface uses its selected native mode's dimensions; creating that surface does not resize the display object.
 */
typedef struct Gfx_Display_CreateInfo_t
{
    Gfx_Display_Kind_t kind;   ///< Presentation environment to initialize.
    uint32_t           width;  ///< Nonzero requested width; direct surfaces use their selected mode width.
    uint32_t           height; ///< Nonzero requested height; direct surfaces use their selected mode height.
} Gfx_Display_CreateInfo_t;

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
 * \brief Creates persistent display state for the selected presentation environment.
 *
 * \param[out] ppDisplay          Receives a non-NULL display object retained for process lifetime
 * \param[in]  pCreateInfo        Non-NULL configuration containing a supported display kind
 *
 * \retval    None No value is returned.
 */
void Gfx_Display_Create(Gfx_Display_t** ppDisplay, const Gfx_Display_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Processes pending native events and reports whether rendering should continue.
 *
 * \param[in,out] pDisplay        Non-NULL display whose native event queue is drained
 *
 * \retval TRUE                   The display remains active
 * \retval FALSE                  A native close or quit event has stopped the display
 */
bool_t Gfx_Display_ProcessEvents(Gfx_Display_t* pDisplay);

#endif /* VULKAN_VA_ZERO_COPY_GFX_DISPLAY_H */
