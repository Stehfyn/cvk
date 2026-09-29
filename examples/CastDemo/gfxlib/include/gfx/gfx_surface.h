#ifndef VULKAN_VA_ZERO_COPY_GFX_SURFACE_H
#define VULKAN_VA_ZERO_COPY_GFX_SURFACE_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_display.h"
#include "gfx/gfx_image.h"
#include "os/os_types.h"

#include <va/va.h>
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
 * \brief Selects whether a surface targets presentation or shared storage.
 */
typedef enum Gfx_Surface_Kind_e
{
    GFX_SURFACE_KIND_PRESENT, ///< Vulkan presentation surface associated with a display.
    GFX_SURFACE_KIND_STORAGE, ///< Shared storage with separate image views for writing and reading.
} Gfx_Surface_Kind_t;

/*************************************************************************************************/
/**
 * \brief Selects the logical pixel layout shared by a surface's images.
 */
typedef enum Gfx_Surface_PixelFormat_e
{
    GFX_SURFACE_PIXELFORMAT_YUV420P,   ///< Three-plane 4:2:0 Y, U, and V layout.
    GFX_SURFACE_PIXELFORMAT_NV12,      ///< 4:2:0 Y plane followed by interleaved UV samples.
    GFX_SURFACE_PIXELFORMAT_RGBA8,     ///< Interleaved red, green, blue, alpha bytes.
    GFX_SURFACE_PIXELFORMAT_BGRA8,     ///< Interleaved blue, green, red, alpha bytes.
    GFX_SURFACE_PIXELFORMAT_UNDEFINED, ///< Presentation targets have no shared-storage pixel layout.
} Gfx_Surface_PixelFormat_t;

/*************************************************************************************************/
/**
 * \brief Specifies a surface's target, pixel layout, device context, and dimensions.
 *
 * \par Details:
 *  Both kinds require a Vulkan instance, physical device, device, queue, and queue-family index.
 *  pAllocator must be NULL. PRESENT requires pDisplay and zero width and height; its extent comes
 *  from the display or selected direct-display mode. pixelFormat and dpy are ignored for PRESENT.
 *  STORAGE requires a matching VA display, NULL pDisplay, and nonzero dimensions. YUV dimensions
 *  must be even. The surface creates its own VA read view and Vulkan write view; the platform
 *  selects shared memory or a transfer path supported by the adapter.
 */
typedef struct Gfx_Surface_CreateInfo_s
{
    Gfx_Surface_Kind_t           kind;               ///< Presentation or storage role.
    Gfx_Surface_PixelFormat_t    pixelFormat;        ///< Storage pixel layout; ignored for presentation.
    const Gfx_Display_t*         pDisplay;           ///< Display used only by presentation surfaces; otherwise NULL.
    VADisplay                    dpy;                ///< Initialized VA display required by storage surfaces.
    VkInstance                   instance;           ///< Instance used to create the native surface.
    VkPhysicalDevice             physicalDevice;     ///< Physical device supporting the selected queue and interop.
    VkDevice                     device;             ///< Logical device owning surface resources.
    VkQueue                      queue;              ///< Queue used for image transfers and synchronization.
    uint32_t                     queueFamilyIndex;   ///< Family index of queue; must not be VK_INVALID.
    uint32_t                     directDisplayIndex; ///< Vulkan display enumeration index for direct presentation.
    uint32_t                     directDisplayModeIndex; ///< Mode enumeration index within the selected direct display.
    const VkAllocationCallbacks* pAllocator;             ///< Must be NULL; custom Vulkan allocation is unsupported.
    uint32_t                     width;                  ///< Storage width in pixels; zero for presentation surfaces.
    uint32_t                     height;                 ///< Storage height in pixels; zero for presentation surfaces.
} Gfx_Surface_CreateInfo_t;

/*************************************************************************************************/
/**
 * \brief Owns a native target and coordinates access to its image storage.
 */
typedef struct Gfx_Surface_s
{
    Gfx_Surface_Kind_t        kind;        ///< Presentation or shared-storage role.
    Gfx_Surface_PixelFormat_t pixelFormat; ///< Storage pixel layout; unused for presentation.
    struct
    {
        VADisplay    dpy;         ///< Initialized VA display owning storage surfaces.
        unsigned int format;      ///< VA render-target format such as VA_RT_FORMAT_YUV420.
        uint32_t     pixelFormat; ///< VA four-character pixel-format code.
    } va;                         ///< VA representation used by shared-storage surfaces.
    struct
    {
        VkPhysicalDevice physicalDevice;   ///< Physical device backing the surface's resources.
        VkDevice         device;           ///< Logical device owning Vulkan resources.
        VkQueue          queue;            ///< Queue used for operations on the surface's images.
        VkFence          completionFence;  ///< Borrowed fence for the latest Vulkan writes to this storage target.
        VkSurfaceKHR     surface;          ///< Native presentation surface, or VK_NULL_HANDLE for storage.
        uint32_t         queueFamilyIndex; ///< Family index of queue.
        VkFormat         format;           ///< Storage format; VK_FORMAT_UNDEFINED for presentation.
        const VkAllocationCallbacks* pAllocator; ///< NULL; surfaces use the default Vulkan allocator.
    } vk;                                        ///< Vulkan representation common to both surface kinds.
    bool_t   writePending; ///< Submitted writes still require synchronization before the target is read or reused.
    bool_t   readPending;  ///< A consumer still holds the target for reading.
    uint32_t width;        ///< Surface width in pixels.
    uint32_t height;       ///< Surface height in pixels.
    struct
    {
        Gfx_Image_t* readImage;  ///< Image view through which consumers read the target.
        Gfx_Image_t* writeImage; ///< Image view through which producers write the target.
    } target;                    ///< Storage image views; both pointers are NULL for presentation.
    Gfx_Image_t* acquiredImage;  ///< Read view published by Release; NULL before the first release.
    void*        platform;       ///< Platform-specific persistent interop state.
} Gfx_Surface_t;

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
 * \brief Creates persistent presentation or shared-storage resources.
 *
 * \par Details:
 *  The supplied display and device contexts must outlive the surface.
 *  Created resources are retained for process lifetime.
 *
 * \param[out] ppSurface          Receives a non-NULL surface retained for process lifetime
 * \param[in]  pCreateInfo        Non-NULL configuration satisfying the selected surface contract
 *
 * \retval    None   No result value.
 */
void Gfx_Surface_Create(Gfx_Surface_t** ppSurface, const Gfx_Surface_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Waits for pending storage writes and native transfers to complete.
 *
 * \par Details:
 *  Clears writePending after synchronization. readPending must be FALSE;
 *  the previous consumer must release its read ownership before the surface is reused.
 *  Asserts that acquiredImage is non-NULL; Release must publish the read image before acquisition.
 *
 * \param[in,out] pSurface        Storage surface with initialized target images and no active reader
 *
 * \retval    None   No result value.
 */
void Gfx_Surface_Acquire(Gfx_Surface_t* pSurface);

/*************************************************************************************************/
/**
 * \brief Publishes the storage target's read view after writes have been submitted.
 *
 * \par Details:
 *  Sets acquiredImage to target.readImage without waiting for completion.
 *  Windows shared-buffer transfers are queued here; host-copy transfers are deferred until Acquire.
 *  The consumer must synchronize pending writes before reading the published image.
 *
 * \param[in,out] pSurface        Storage surface with submitted writes and writePending set
 *
 * \retval    None   No result value.
 */
void Gfx_Surface_Release(Gfx_Surface_t* pSurface);

#endif /* VULKAN_VA_ZERO_COPY_GFX_SURFACE_H */
