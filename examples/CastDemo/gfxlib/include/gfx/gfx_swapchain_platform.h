#ifndef VULKAN_VA_ZERO_COPY_GFX_SWAPCHAIN_PLATFORM_H
#define VULKAN_VA_ZERO_COPY_GFX_SWAPCHAIN_PLATFORM_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cvk/cvk.h"

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
 * \brief Submits one command buffer with the platform's external semaphore values.
 *
 * \param[in] queue                 Queue receiving the submission
 * \param[in] pNext                 Optional submit extension chain
 * \param[in] waitSemaphoreCount    Number of wait semaphores
 * \param[in] pWaitSemaphores       Wait semaphores, or NULL when the count is zero
 * \param[in] pWaitDstStageMasks    Destination stage masks parallel to the wait semaphores
 * \param[in] pWaitSemaphoreValues  D3D12 fence or timeline values parallel to the wait semaphores
 * \param[in] commandBuffer         Command buffer to submit
 * \param[in] signalSemaphoreCount  Number of signal semaphores
 * \param[in] pSignalSemaphores     Signal semaphores, or NULL when the count is zero
 * \param[in] pSignalSemaphoreValues D3D12 fence or timeline values parallel to the signal semaphores
 * \param[in] fence                 Submission completion fence, or VK_NULL_HANDLE
 *
 * \retval    None
 */
void Gfx_Swapchain_Platform_QueueSubmit(VkQueue                     queue,
                                        const void*                 pNext,
                                        uint32_t                    waitSemaphoreCount,
                                        const VkSemaphore*          pWaitSemaphores,
                                        const VkPipelineStageFlags* pWaitDstStageMasks,
                                        const uint64_t*             pWaitSemaphoreValues,
                                        VkCommandBuffer             commandBuffer,
                                        uint32_t                    signalSemaphoreCount,
                                        const VkSemaphore*          pSignalSemaphores,
                                        const uint64_t*             pSignalSemaphoreValues,
                                        VkFence                     fence);

#endif /* VULKAN_VA_ZERO_COPY_GFX_SWAPCHAIN_PLATFORM_H */
