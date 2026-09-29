/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Direct-display selection policy; native construction and queries are provided by CVK. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_surface_internal.h"
#include "gfx/gfx_surface.h"
#include "cvk/cvk.h"
#include "os/os_assert.h"
#include "os/os_types.h"

#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan_core.h>

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
 * \brief Selects the first plane supporting the requested display without replacing another display.
 *
 * \par Requirements:
 *  - 001 The device and display shall be valid enumerated handles.
 *  - 002 Properties shall contain planeCount entries in native enumeration order.
 *  - 003 At least one compatible plane shall exist; the caller checks this startup prerequisite.
 *  - 004 Enumeration output storage shall satisfy CVK's declared capacity contract.
 *
 * \param[in] physicalDevice  Physical device queried for supported displays
 * \param[in] display         Display to associate with the selected plane
 * \param[in] planeCount      Number of initialized plane properties
 * \param[in] properties      Plane properties in native enumeration order
 *
 * \retval    0..planeCount-1   Index of the first compatible plane.
 */
static uint32_t GetCompatiblePlaneIndex(VkPhysicalDevice                   physicalDevice,
                                        VkDisplayKHR                       display,
                                        uint32_t                           planeCount,
                                        const VkDisplayPlanePropertiesKHR* properties);

/*************************************************************************************************/
/**
 * \brief Selects a supported alpha mode in opaque, global, per-pixel, premultiplied order.
 *
 * \par Requirements:
 *  - 001 The mask shall contain at least one defined display-plane alpha mode.
 *  - 002 Selection shall examine the defined bits in increasing order without allocating storage.
 *
 * \param[in] supported  Queried display-plane alpha mask
 *
 * \retval    VkDisplayPlaneAlphaFlagBitsKHR   First supported defined alpha mode.
 */
static VkDisplayPlaneAlphaFlagBitsKHR GetAlphaMode(VkDisplayPlaneAlphaFlagsKHR supported);

/*************************************************************************************************/
/**
 * \brief Selects a surface transform, preferring identity before rotations, mirrors and inheritance.
 *
 * \par Requirements:
 *  - 001 The mask shall contain at least one defined surface transform.
 *  - 002 Selection shall examine the defined bits in increasing order without allocating storage.
 *
 * \param[in] supported  Queried display transform mask
 *
 * \retval    VkSurfaceTransformFlagBitsKHR   First supported defined transform.
 */
static VkSurfaceTransformFlagBitsKHR GetTransform(VkSurfaceTransformFlagsKHR supported);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Plane indices remain stable within the queried snapshot. Selection does not acquire the plane.
 */
static uint32_t GetCompatiblePlaneIndex(VkPhysicalDevice                   physicalDevice,
                                        VkDisplayKHR                       display,
                                        uint32_t                           planeCount,
                                        const VkDisplayPlanePropertiesKHR* properties)
{
    VkDisplayKHR displays[CVK_MAX_DISPLAYS];
    uint32_t     selected;
    uint32_t     plane_index;
    bool_t       is_unassigned;
    bool_t       is_selected_display;
    bool_t       can_select;
    uint32_t     display_count;
    uint32_t     display_index;
    bool_t       found;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != display);
    LOGASSERT(0 < planeCount);
    LOGASSERT(NULL != properties);

    selected = planeCount;
    for (plane_index = 0; plane_index < planeCount; ++plane_index)
    {
        is_unassigned       = (VK_NULL_HANDLE == properties[plane_index].currentDisplay);
        is_selected_display = (display == properties[plane_index].currentDisplay);
        can_select          = is_unassigned || is_selected_display;
        if (can_select)
        {
            cvkGetDisplayPlaneSupportedDisplays(physicalDevice, plane_index, &display_count, displays);
            found = FALSE;
            for (display_index = 0; display_index < display_count; ++display_index)
            {
                if (display == displays[display_index])
                {
                    found = TRUE;
                    break;
                }
            }
            if (found)
            {
                selected = plane_index;
                break;
            }
        }
    }
    LOGASSERT(selected < planeCount);
    return selected;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Defined alpha modes occupy consecutive bits from opaque through premultiplied per-pixel alpha.
 */
static VkDisplayPlaneAlphaFlagBitsKHR GetAlphaMode(VkDisplayPlaneAlphaFlagsKHR supported)
{
    VkDisplayPlaneAlphaFlagsKHR candidate;

    for (candidate = (VkDisplayPlaneAlphaFlagsKHR)VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR;
         candidate <= (VkDisplayPlaneAlphaFlagsKHR)VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR;
         candidate <<= 1)
    {
        if (supported & candidate)
        {
            break;
        }
    }
    LOGASSERT(candidate <= (VkDisplayPlaneAlphaFlagsKHR)VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR);
    return (VkDisplayPlaneAlphaFlagBitsKHR)candidate;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Defined transform bits preserve the reference's identity-first selection order.
 */
static VkSurfaceTransformFlagBitsKHR GetTransform(VkSurfaceTransformFlagsKHR supported)
{
    VkSurfaceTransformFlagsKHR candidate;

    for (candidate = (VkSurfaceTransformFlagsKHR)VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
         candidate <= (VkSurfaceTransformFlagsKHR)VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR;
         candidate <<= 1)
    {
        if (supported & candidate)
        {
            break;
        }
    }
    LOGASSERT(candidate <= (VkSurfaceTransformFlagsKHR)VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR);
    return (VkSurfaceTransformFlagBitsKHR)candidate;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Native platforms share display selection and publish the same surface state after construction.
 */
void Gfx_Surface_InitializeDirect(Gfx_Surface_Storage_t* storage, const Gfx_Surface_CreateInfo_t* createInfo)
{
    Gfx_Surface_t*                 surface;
    uint32_t                       display_count;
    VkDisplayKHR                   display;
    uint32_t                       mode_count;
    VkDisplayModeKHR               mode;
    VkExtent2D                     extent;
    uint32_t                       plane_count;
    uint32_t                       plane_index;
    VkDisplayPlaneCapabilitiesKHR  capabilities;
    VkSurfaceTransformFlagBitsKHR  transform;
    VkDisplayPlaneAlphaFlagBitsKHR alpha;
    VkSurfaceKHR                   native_surface;

    LOGASSERT(NULL != storage);
    LOGASSERT(NULL != createInfo);
    LOGASSERT(GFX_SURFACE_KIND_PRESENT == createInfo->kind);
    LOGASSERT(VK_NULL_HANDLE != createInfo->physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != createInfo->instance);

    surface = &storage->surface;
    cvkGetPhysicalDeviceDisplayProperties(createInfo->physicalDevice, &display_count, storage->queries.displays);
    LOGASSERT(createInfo->directDisplayIndex < display_count);
    display   = storage->queries.displays[createInfo->directDisplayIndex].display;
    transform = GetTransform(storage->queries.displays[createInfo->directDisplayIndex].supportedTransforms);
    cvkGetDisplayModeProperties(createInfo->physicalDevice, display, &mode_count, storage->queries.modes);
    LOGASSERT(createInfo->directDisplayModeIndex < mode_count);
    mode   = storage->queries.modes[createInfo->directDisplayModeIndex].displayMode;
    extent = storage->queries.modes[createInfo->directDisplayModeIndex].parameters.visibleRegion;
    cvkGetPhysicalDeviceDisplayPlaneProperties(createInfo->physicalDevice, &plane_count, storage->queries.planes);
    plane_index = GetCompatiblePlaneIndex(createInfo->physicalDevice, display, plane_count, storage->queries.planes);
    cvkGetDisplayPlaneCapabilities(createInfo->physicalDevice, mode, plane_index, &capabilities);
    alpha = GetAlphaMode(capabilities.supportedAlpha);
    cvkCreateDisplayPlaneSurface(createInfo->instance,
                                 NULL,
                                 (VkDisplaySurfaceCreateFlagsKHR)0,
                                 mode,
                                 plane_index,
                                 storage->queries.planes[plane_index].currentStackIndex,
                                 transform,
                                 1.0F,
                                 alpha,
                                 extent,
                                 createInfo->pAllocator,
                                 &native_surface);

    surface->kind                = createInfo->kind;
    surface->pixelFormat         = GFX_SURFACE_PIXELFORMAT_UNDEFINED;
    surface->va.dpy              = NULL;
    surface->va.format           = 0;
    surface->va.pixelFormat      = 0;
    surface->vk.physicalDevice   = createInfo->physicalDevice;
    surface->vk.device           = createInfo->device;
    surface->vk.queue            = createInfo->queue;
    surface->vk.completionFence  = VK_NULL_HANDLE;
    surface->vk.surface          = native_surface;
    surface->vk.queueFamilyIndex = createInfo->queueFamilyIndex;
    surface->vk.format           = VK_FORMAT_UNDEFINED;
    surface->vk.pAllocator       = createInfo->pAllocator;
    surface->writePending        = FALSE;
    surface->readPending         = FALSE;
    surface->width               = extent.width;
    surface->height              = extent.height;
    surface->target.readImage    = NULL;
    surface->target.writeImage   = NULL;
    surface->acquiredImage       = NULL;
    surface->platform            = NULL;
}
