#ifndef VULKAN_VA_ZERO_COPY_GFX_RENDERER_H
#define VULKAN_VA_ZERO_COPY_GFX_RENDERER_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_image.h"

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
 * \brief Renders into an owned offscreen image and copies completed frames to acquired images.
 */
typedef struct Gfx_Renderer_t Gfx_Renderer_t;

/*************************************************************************************************/
/**
 * \brief Selects the rendering backend.
 */
typedef enum Gfx_Renderer_Hint_t
{
    GFX_RENDERER_HINT_VULKAN = 0, ///< Vulkan renderer.
} Gfx_Renderer_Hint_t;

/*************************************************************************************************/
/**
 * \brief Specifies the rendering backend, device context, and target dimensions.
 */
typedef struct Gfx_Renderer_CreateInfo_t
{
    Gfx_Renderer_Hint_t hint;   ///< Backend used to record and submit rendering work.
    uint32_t            width;  ///< Nonzero offscreen render-target width in pixels.
    uint32_t            height; ///< Nonzero offscreen render-target height in pixels.
    struct
    {
        VkPhysicalDevice             physicalDevice;   ///< Device used for render-target memory selection.
        VkDevice                     device;           ///< Logical device owning renderer resources.
        VkQueue                      queue;            ///< Queue used to submit completed frames.
        uint32_t                     queueFamilyIndex; ///< Queue family used for timestamp validation.
        const VkAllocationCallbacks* pAllocator;       ///< Optional Vulkan allocation callbacks.
    } vk;                                              ///< Vulkan creation parameters.
} Gfx_Renderer_CreateInfo_t;

/*************************************************************************************************/
/**
 * \brief Cumulative renderer execution statistics.
 */
typedef struct Gfx_Renderer_Statistics_t
{
    uint64_t frameCount;         ///< Number of completed frames included in gpuTimeNanoseconds.
    uint64_t gpuTimeNanoseconds; ///< GPU time spent rendering and blitting to acquired images.
} Gfx_Renderer_Statistics_t;

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
 * \brief Creates a renderer and its persistent offscreen target.
 *
 * \par Details:
 *  Renderer resources are retained for process lifetime. Images supplied to BeginFrame()
 *  remain owned by the caller.
 *
 * \param[out] ppRenderer         Receives the initialized renderer
 * \param[in]  pCreateInfo        Backend, Vulkan context, and render-target extent
 *
 * \retval    None No value is returned.
 */
void Gfx_Renderer_Create(Gfx_Renderer_t** ppRenderer, const Gfx_Renderer_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Begins one frame on the renderer-owned offscreen target.
 *
 * \par Details:
 *  The supplied acquired image is retained only as the destination of the frame-ending blit.
 *
 * \param[in,out] pRenderer       Idle renderer beginning a frame
 * \param[in,out] pTargetImage    Acquired swapchain image receiving the completed target
 *
 * \retval    None No value is returned.
 */
void Gfx_Renderer_BeginFrame(Gfx_Renderer_t* pRenderer, Gfx_Image_t* pTargetImage);

/*************************************************************************************************/
/**
 * \brief Records the deterministic test scene into the active offscreen target.
 *
 * \param[in,out] pRenderer       Renderer between BeginFrame() and EndFrame()
 * \param[in]     frameIndex      Animation frame used by the test scene
 *
 * \retval    None No value is returned.
 */
void Gfx_Renderer_Render(Gfx_Renderer_t* pRenderer, uint32_t frameIndex);

/*************************************************************************************************/
/**
 * \brief Copies the rendered frame to the acquired image and submits the recorded commands.
 *
 * \par Details:
 *  Ends recording after the target-to-backing-image blit and submits the complete frame. The
 *  submission signals the backing image semaphore consumed by Gfx_Swapchain_SubmitImage().
 *
 * \param[in,out] pRenderer       Active renderer containing recorded render work
 *
 * \retval    None No value is returned.
 */
void Gfx_Renderer_EndFrame(Gfx_Renderer_t* pRenderer);

/*************************************************************************************************/
/**
 * \brief Reads cumulative GPU execution statistics for every submitted renderer frame.
 *
 * \par Details:
 *  Completes outstanding timestamp queries before returning. The counters are cumulative for the
 *  renderer lifetime, allowing callers to subtract two snapshots around a measured interval.
 *
 * \param[in,out] pRenderer       Renderer whose timestamp queries are collected
 * \param[out]    pStatistics     Receives completed frame count and GPU time
 *
 * \retval    None No value is returned.
 */
void Gfx_Renderer_GetStatistics(Gfx_Renderer_t* pRenderer, Gfx_Renderer_Statistics_t* pStatistics);

#endif /* VULKAN_VA_ZERO_COPY_GFX_RENDERER_H */
