/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_swapchain_platform.h"
#include "gfx/gfx_vulkan.h"

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
 * \brief Submits command buffers with D3D12 fence values.
 *
 * \param[in] queue                  Queue receiving the submission
 * \param[in] pNext                  Additional submission extension chain, or NULL
 * \param[in] waitSemaphoreCount     Number of semaphores and fence values to wait on
 * \param[in] pWaitSemaphores        Semaphores waited on before command execution
 * \param[in] pWaitDstStageMask      Pipeline stage masks parallel to the wait semaphores
 * \param[in] pWaitSemaphoreValues   D3D12 fence values parallel to the wait semaphores
 * \param[in] commandBufferCount     Number of command buffers to submit
 * \param[in] pCommandBuffers        Command buffers to submit
 * \param[in] signalSemaphoreCount   Number of semaphores and fence values to signal
 * \param[in] pSignalSemaphores      Semaphores signaled when command execution completes
 * \param[in] pSignalSemaphoreValues D3D12 fence values parallel to the signal semaphores
 * \param[in] fence                  Submission completion fence, or VK_NULL_HANDLE
 *
 * \retval    None
 */
static void QueueSubmitD3D12Fence(VkQueue                     queue,
                                  const void*                 pNext,
                                  uint32_t                    waitSemaphoreCount,
                                  const VkSemaphore*          pWaitSemaphores,
                                  const VkPipelineStageFlags* pWaitDstStageMask,
                                  const uint64_t*             pWaitSemaphoreValues,
                                  uint32_t                    commandBufferCount,
                                  const VkCommandBuffer*      pCommandBuffers,
                                  uint32_t                    signalSemaphoreCount,
                                  const VkSemaphore*          pSignalSemaphores,
                                  const uint64_t*             pSignalSemaphoreValues,
                                  VkFence                     fence);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void QueueSubmitD3D12Fence(VkQueue                     queue,
                                  const void*                 pNext,
                                  uint32_t                    waitSemaphoreCount,
                                  const VkSemaphore*          pWaitSemaphores,
                                  const VkPipelineStageFlags* pWaitDstStageMask,
                                  const uint64_t*             pWaitSemaphoreValues,
                                  uint32_t                    commandBufferCount,
                                  const VkCommandBuffer*      pCommandBuffers,
                                  uint32_t                    signalSemaphoreCount,
                                  const VkSemaphore*          pSignalSemaphores,
                                  const uint64_t*             pSignalSemaphoreValues,
                                  VkFence                     fence)
{
    VkD3D12FenceSubmitInfoKHR fence_submit_info;

    fence_submit_info.sType                      = VK_STRUCTURE_TYPE_D3D12_FENCE_SUBMIT_INFO_KHR;
    fence_submit_info.pNext                      = pNext;
    fence_submit_info.waitSemaphoreValuesCount   = waitSemaphoreCount;
    fence_submit_info.pWaitSemaphoreValues       = pWaitSemaphoreValues;
    fence_submit_info.signalSemaphoreValuesCount = signalSemaphoreCount;
    fence_submit_info.pSignalSemaphoreValues     = pSignalSemaphoreValues;
    Gfx_Vulkan_QueueSubmit(queue, &fence_submit_info, waitSemaphoreCount, pWaitSemaphores, pWaitDstStageMask, commandBufferCount, pCommandBuffers, signalSemaphoreCount, pSignalSemaphores, fence);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
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
                                        VkFence                     fence)
{
    QueueSubmitD3D12Fence(queue,
                          pNext,
                          waitSemaphoreCount,
                          pWaitSemaphores,
                          pWaitDstStageMasks,
                          pWaitSemaphoreValues,
                          1,
                          &commandBuffer,
                          signalSemaphoreCount,
                          pSignalSemaphores,
                          pSignalSemaphoreValues,
                          fence);
}
