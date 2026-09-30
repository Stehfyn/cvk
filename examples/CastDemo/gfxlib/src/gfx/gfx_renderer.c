/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cvk/cvk.h"
#include "gfx/gfx_renderer.h"

#include "gfx/gfx_image.h"
#include "os/os_assert.h"
#include "os/os_malloc.h"

#include "c_renderPatternSpirv.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define RENDER_TARGET_BINDING (0U) ///< Storage-image binding written by the renderer program.
#define RENDER_LOCAL_SIZE_X   (8U) ///< Renderer-program workgroup width.
#define RENDER_LOCAL_SIZE_Y   (8U) ///< Renderer-program workgroup height.
#define RENDER_QUERY_BEGIN    (0U) ///< Timestamp recorded before renderer commands.
#define RENDER_QUERY_END      (1U) ///< Timestamp recorded after the target blit.
#define RENDER_QUERY_COUNT    (2U) ///< Timestamp queries assigned to each in-flight image.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Shader parameters that vary for each rendered frame. */
typedef struct RenderPushConstants_t
{
    uint32_t frameIndex; ///< Animation frame rendered into the offscreen target.
} RenderPushConstants_t;

/*************************************************************************************************/
/** \brief Associates a destination image with the collection state of its timestamp queries. */
typedef struct RenderQuerySlot_t
{
    Gfx_Image_t* pImage;  ///< Destination image associated with this query pair.
    bool_t       pending; ///< TRUE while the query pair contains an uncollected frame.
} RenderQuerySlot_t;

/*************************************************************************************************/
/**
 * \brief Rendering resources, active-frame state, and accumulated execution statistics.
 */
struct Gfx_Renderer_t
{
    Gfx_Renderer_Hint_t          hint;                ///< Active rendering backend.
    Gfx_Image_t*                 pTargetImage;        ///< Acquired destination retained only for the active frame.
    uint32_t                     width;               ///< Offscreen target width.
    uint32_t                     height;              ///< Offscreen target height.
    VkPhysicalDevice             physicalDevice;      ///< Device used for target memory selection.
    VkDevice                     device;              ///< Logical device owning renderer resources.
    VkQueue                      queue;               ///< Queue receiving completed render frames.
    uint32_t                     queueFamilyIndex;    ///< Queue family supporting timestamp queries.
    const VkAllocationCallbacks* pAllocator;          ///< Optional Vulkan allocation callbacks.
    VkImage                      targetImage;         ///< Offscreen image written by render operations.
    VkDeviceMemory               targetMemory;        ///< Device-local storage bound to targetImage.
    VkImageView                  targetView;          ///< Storage view bound to the renderer program.
    VkDescriptorSetLayout        descriptorSetLayout; ///< Renderer-program descriptor layout.
    VkDescriptorPool             descriptorPool;      ///< Pool retaining descriptorSet.
    VkDescriptorSet              descriptorSet;       ///< Immutable descriptor of targetView.
    VkShaderModule               shaderModule;        ///< Procedural renderer program.
    VkPipelineLayout             pipelineLayout;      ///< Descriptor and push-constant interface.
    VkPipeline                   pipeline;            ///< Compute pipeline recording render operations.
    VkQueryPool                  queryPool;           ///< Timestamp pairs measuring renderer GPU work.
    RenderQuerySlot_t            querySlots[CVK_MAX_SWAPCHAIN_IMAGES]; ///< Per-image query ownership.
    uint32_t                     activeQuerySlot;                      ///< Query slot recording the active frame.
    uint64_t                     timestampMask;     ///< Mask for the queue family's valid timestamp bits.
    float                        timestampPeriod;   ///< Nanoseconds represented by one timestamp tick.
    Gfx_Renderer_Statistics_t    statistics;        ///< Cumulative collected GPU execution time.
    bool_t                       targetInitialized; ///< TRUE after targetImage leaves UNDEFINED.
    bool_t                       renderRecorded;    ///< TRUE after the active frame records its render operation.
    bool_t                       frameActive;       ///< TRUE between BeginFrame() and EndFrame().
};

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
 * \brief Validates the rendering backend, device context, and target dimensions.
 *
 * \param[in] pCreateInfo        Complete renderer specification
 *
 * \retval    None No value is returned.
 */
static void ValidateCreateInfo(const Gfx_Renderer_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Creates the renderer-owned image, view, descriptor, and render program.
 *
 * \param[out] pRenderer         Zero-initialized renderer storage
 * \param[in]  pCreateInfo       Validated renderer specification
 *
 * \retval    None No value is returned.
 */
static void CreateRenderer(Gfx_Renderer_t* pRenderer, const Gfx_Renderer_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Finds or assigns the timestamp slot associated with an acquired image.
 *
 * \param[in,out] pRenderer      Initialized renderer
 * \param[in]     pImage         Acquired image requiring a timestamp pair
 *
 * \return Index of the image's stable timestamp slot.
 */
static uint32_t GetQuerySlot(Gfx_Renderer_t* pRenderer, Gfx_Image_t* pImage);

/*************************************************************************************************/
/**
 * \brief Collects one completed timestamp pair into cumulative statistics.
 *
 * \param[in,out] pRenderer      Initialized renderer
 * \param[in]     slotIndex      Timestamp slot to collect when pending
 *
 * \retval    None No value is returned.
 */
static void CollectQuerySlot(Gfx_Renderer_t* pRenderer, uint32_t slotIndex);

/*************************************************************************************************/
/**
 * \brief Begins commands that target the private offscreen image.
 *
 * \param[in,out] pRenderer      Idle renderer receiving the acquired destination image
 *
 * \retval    None No value is returned.
 */
static void BeginFrame(Gfx_Renderer_t* pRenderer, Gfx_Image_t* pTargetImage);

/*************************************************************************************************/
/**
 * \brief Dispatches the test-scene render program into the offscreen image.
 *
 * \param[in,out] pRenderer      Renderer recording an active frame
 * \param[in]     frameIndex     Animation frame supplied to the render program
 *
 * \retval    None No value is returned.
 */
static void RenderFrame(Gfx_Renderer_t* pRenderer, uint32_t frameIndex);

/*************************************************************************************************/
/**
 * \brief Blits the target into the acquired image and submits the complete frame.
 *
 * \param[in,out] pRenderer      Active renderer containing recorded render work
 *
 * \retval    None No value is returned.
 */
static void EndFrame(Gfx_Renderer_t* pRenderer);

/*************************************************************************************************/
/**
 * \brief Collects outstanding timestamps and copies cumulative statistics.
 *
 * \param[in,out] pRenderer      Idle renderer whose query results are collected
 * \param[out]    pStatistics    Receives a cumulative renderer-lifetime snapshot
 *
 * \retval    None No value is returned.
 */
static void GetStatistics(Gfx_Renderer_t* pRenderer, Gfx_Renderer_Statistics_t* pStatistics);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  EndFrame() scales the rendered image to the destination image's extent when copying the frame.
 */
static void ValidateCreateInfo(const Gfx_Renderer_CreateInfo_t* pCreateInfo)
{
    LOGASSERT(NULL != pCreateInfo);
    LOGASSERT(GFX_RENDERER_HINT_VULKAN == pCreateInfo->hint);
    LOGASSERT((0 != pCreateInfo->width) && (0 != pCreateInfo->height));
    LOGASSERT(VK_NULL_HANDLE != pCreateInfo->vk.physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != pCreateInfo->vk.device);
    LOGASSERT(VK_NULL_HANDLE != pCreateInfo->vk.queue);
    LOGASSERT(VK_INVALID != pCreateInfo->vk.queueFamilyIndex);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  An acquired backing image retains its query pair while capacity remains. An idle pair may be
 *  reassigned when the renderer moves between image rings.
 */
static uint32_t GetQuerySlot(Gfx_Renderer_t* pRenderer, Gfx_Image_t* pImage)
{
    uint32_t slot_index;

    for (slot_index = 0; slot_index < CVK_MAX_SWAPCHAIN_IMAGES; ++slot_index)
    {
        if (pImage == pRenderer->querySlots[slot_index].pImage)
        {
            return slot_index;
        }
    }
    for (slot_index = 0; slot_index < CVK_MAX_SWAPCHAIN_IMAGES; ++slot_index)
    {
        if (NULL == pRenderer->querySlots[slot_index].pImage)
        {
            pRenderer->querySlots[slot_index].pImage = pImage;
            return slot_index;
        }
    }
    for (slot_index = 0; slot_index < CVK_MAX_SWAPCHAIN_IMAGES; ++slot_index)
    {
        if (FALSE == pRenderer->querySlots[slot_index].pending)
        {
            pRenderer->querySlots[slot_index].pImage = pImage;
            return slot_index;
        }
    }
    LOGFATAL("renderer timestamp slot capacity exceeded");
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Timestamp subtraction is masked for queue families that expose fewer than 64 valid bits.
 */
static void CollectQuerySlot(Gfx_Renderer_t* pRenderer, uint32_t slotIndex)
{
    uint64_t timestamps[RENDER_QUERY_COUNT];
    uint64_t elapsed_ticks;

    if (FALSE == pRenderer->querySlots[slotIndex].pending)
    {
        return;
    }
    CVK_CHECK(cvkGetQueryPoolResults64(pRenderer->device,
                                       pRenderer->queryPool,
                                       slotIndex * RENDER_QUERY_COUNT,
                                       RENDER_QUERY_COUNT,
                                       (VkQueryResultFlags)VK_QUERY_RESULT_WAIT_BIT,
                                       timestamps));
    elapsed_ticks = (timestamps[RENDER_QUERY_END] - timestamps[RENDER_QUERY_BEGIN]) & pRenderer->timestampMask;
    pRenderer->statistics.gpuTimeNanoseconds += (uint64_t)((double)elapsed_ticks * (double)pRenderer->timestampPeriod);
    ++pRenderer->statistics.frameCount;
    pRenderer->querySlots[slotIndex].pending = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The target has no host-visible staging allocation. Its immutable storage descriptor is shared
 *  by every frame because every submission writes the same image on the same ordered queue.
 */
static void CreateRenderer(Gfx_Renderer_t* pRenderer, const Gfx_Renderer_CreateInfo_t* pCreateInfo)
{
    VkExtent3D                   target_extent;
    VkComponentMapping           components;
    VkImageSubresourceRange      target_range;
    VkDescriptorSetLayoutBinding descriptor_binding;
    VkDescriptorPoolSize         descriptor_pool_size;
    VkDescriptorSetLayoutSupport descriptor_support;
    VkQueueFamilyProperties      queue_families[CVK_MAX_QUEUE_FAMILY_PROPERTIES];
    VkPhysicalDeviceProperties2  properties;
    uint32_t                     queue_family_count;
    uint32_t                     timestamp_valid_bits;

    ValidateCreateInfo(pCreateInfo);
    cvkGetPhysicalDeviceQueueFamilyProperties(pCreateInfo->vk.physicalDevice, &queue_family_count, queue_families);
    LOGASSERT(pCreateInfo->vk.queueFamilyIndex < queue_family_count);
    timestamp_valid_bits = queue_families[pCreateInfo->vk.queueFamilyIndex].timestampValidBits;
    LOGASSERT(0 != timestamp_valid_bits);
    cvkGetPhysicalDeviceProperties2(pCreateInfo->vk.physicalDevice, NULL, &properties);
    cvkInitDescriptorSetLayoutBinding(&descriptor_binding,
                                      RENDER_TARGET_BINDING,
                                      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                      1,
                                      (VkShaderStageFlags)VK_SHADER_STAGE_COMPUTE_BIT,
                                      NULL);
    cvkInitDescriptorPoolSize(&descriptor_pool_size, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1);
    cvkGetDescriptorSetLayoutSupport(pCreateInfo->vk.device,
                                     NULL,
                                     (VkDescriptorSetLayoutCreateFlags)0,
                                     1,
                                     &descriptor_binding,
                                     NULL,
                                     &descriptor_support);
    LOGASSERT(VK_TRUE == descriptor_support.supported);
    target_extent.width  = pCreateInfo->width;
    target_extent.height = pCreateInfo->height;
    target_extent.depth  = 1;
    cvkSetupImage(pCreateInfo->vk.physicalDevice,
                  pCreateInfo->vk.device,
                  NULL,
                  (VkImageCreateFlags)0,
                  VK_IMAGE_TYPE_2D,
                  VK_FORMAT_R8G8B8A8_UNORM,
                  target_extent,
                  1,
                  1,
                  VK_SAMPLE_COUNT_1_BIT,
                  VK_IMAGE_TILING_OPTIMAL,
                  (VkImageUsageFlags)((VkFlags)VK_IMAGE_USAGE_STORAGE_BIT | (VkFlags)VK_IMAGE_USAGE_TRANSFER_SRC_BIT),
                  VK_SHARING_MODE_EXCLUSIVE,
                  0,
                  NULL,
                  VK_IMAGE_LAYOUT_UNDEFINED,
                  (VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                  NULL,
                  pCreateInfo->vk.pAllocator,
                  &pRenderer->targetImage,
                  &pRenderer->targetMemory);
    components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    target_range = cvkGetFormatBaseSubresourceRange(VK_FORMAT_R8G8B8A8_UNORM);
    cvkCreateImageView(pCreateInfo->vk.device,
                       NULL,
                       (VkImageViewCreateFlags)0,
                       pRenderer->targetImage,
                       VK_IMAGE_VIEW_TYPE_2D,
                       VK_FORMAT_R8G8B8A8_UNORM,
                       components,
                       target_range,
                       pCreateInfo->vk.pAllocator,
                       &pRenderer->targetView);

    cvkCreateDescriptorSetLayout(pCreateInfo->vk.device,
                                 NULL,
                                 (VkDescriptorSetLayoutCreateFlags)0,
                                 1,
                                 &descriptor_binding,
                                 pCreateInfo->vk.pAllocator,
                                 &pRenderer->descriptorSetLayout);
    cvkCreateDescriptorPool(pCreateInfo->vk.device,
                            NULL,
                            (VkDescriptorPoolCreateFlags)0,
                            1,
                            1,
                            &descriptor_pool_size,
                            pCreateInfo->vk.pAllocator,
                            &pRenderer->descriptorPool);
    cvkAllocateDescriptorSets(pCreateInfo->vk.device,
                              NULL,
                              pRenderer->descriptorPool,
                              1,
                              &pRenderer->descriptorSetLayout,
                              &pRenderer->descriptorSet);
    cvkUpdateImageDescriptorSet(pCreateInfo->vk.device,
                                VK_NULL_HANDLE,
                                pRenderer->targetView,
                                VK_IMAGE_LAYOUT_GENERAL,
                                NULL,
                                pRenderer->descriptorSet,
                                descriptor_binding.binding,
                                0,
                                descriptor_binding.descriptorType);
    CVK_CHECK(cvkSetupComputePipeline(pCreateInfo->vk.device,
                                      NULL,
                                      sizeof(c_renderPatternSpirv),
                                      c_renderPatternSpirv,
                                      NULL,
                                      NULL,
                                      1,
                                      &pRenderer->descriptorSetLayout,
                                      0,
                                      sizeof(RenderPushConstants_t),
                                      VK_NULL_HANDLE,
                                      NULL,
                                      (VkPipelineCreateFlags)0,
                                      VK_NULL_HANDLE,
                                      0,
                                      (VkShaderModuleCreateFlags)0,
                                      (VkPipelineLayoutCreateFlags)0,
                                      NULL,
                                      (VkPipelineShaderStageCreateFlags)0,
                                      "main",
                                      pCreateInfo->vk.pAllocator,
                                      &pRenderer->shaderModule,
                                      &pRenderer->pipelineLayout,
                                      &pRenderer->pipeline));
    cvkCreateTimestampQueryPool(pCreateInfo->vk.device,
                                NULL,
                                (VkQueryPoolCreateFlags)0,
                                CVK_MAX_SWAPCHAIN_IMAGES * RENDER_QUERY_COUNT,
                                pCreateInfo->vk.pAllocator,
                                &pRenderer->queryPool);
    pRenderer->hint             = pCreateInfo->hint;
    pRenderer->width            = pCreateInfo->width;
    pRenderer->height           = pCreateInfo->height;
    pRenderer->physicalDevice   = pCreateInfo->vk.physicalDevice;
    pRenderer->device           = pCreateInfo->vk.device;
    pRenderer->queue            = pCreateInfo->vk.queue;
    pRenderer->queueFamilyIndex = pCreateInfo->vk.queueFamilyIndex;
    pRenderer->pAllocator       = pCreateInfo->vk.pAllocator;
    pRenderer->timestampPeriod  = properties.properties.limits.timestampPeriod;
    pRenderer->timestampMask    = UINT64_MAX;
    if (64U > timestamp_valid_bits)
    {
        pRenderer->timestampMask = (UINT64_C(1) << timestamp_valid_bits) - UINT64_C(1);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The acquired backing image contributes command storage but is not bound as a render target. The
 *  first frame transitions only the private target; later frames inherit its GENERAL layout from
 *  the preceding frame's release barrier.
 */
static void BeginFrame(Gfx_Renderer_t* pRenderer, Gfx_Image_t* pTargetImage)
{
    VkImageSubresourceRange target_range;
    uint32_t                first_query;

    LOGASSERT(NULL != pRenderer);
    LOGASSERT(FALSE == pRenderer->frameActive);
    LOGASSERT(NULL != pTargetImage);
    LOGASSERT(FALSE != pTargetImage->acquired);
    LOGASSERT(FALSE == pTargetImage->uploadPending);
    LOGASSERT(FALSE == pTargetImage->vk.uploadSubmitted);
    LOGASSERT(pRenderer->device == pTargetImage->vk.device);
    LOGASSERT(pRenderer->queue == pTargetImage->vk.queue);

    pRenderer->activeQuerySlot = GetQuerySlot(pRenderer, pTargetImage);
    CollectQuerySlot(pRenderer, pRenderer->activeQuerySlot);
    cvkResetCommandPool(pTargetImage->vk.device, pTargetImage->vk.commandPool, (VkCommandPoolResetFlags)0);
    cvkBeginCommandBuffer(pTargetImage->vk.commandBuffer,
                          NULL,
                          (VkCommandBufferUsageFlags)VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                          NULL);
    first_query = pRenderer->activeQuerySlot * RENDER_QUERY_COUNT;
    cvkCmdResetQueryPool(pTargetImage->vk.commandBuffer, pRenderer->queryPool, first_query, RENDER_QUERY_COUNT);
    cvkCmdWriteTimestamp(pTargetImage->vk.commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         pRenderer->queryPool,
                         first_query + RENDER_QUERY_BEGIN);
    if (FALSE == pRenderer->targetInitialized)
    {
        target_range = cvkGetFormatBaseSubresourceRange(VK_FORMAT_R8G8B8A8_UNORM);
        cvkCmdImageBarrier(pTargetImage->vk.commandBuffer,
                           NULL,
                           (VkAccessFlags)0,
                           (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                           VK_IMAGE_LAYOUT_UNDEFINED,
                           VK_IMAGE_LAYOUT_GENERAL,
                           pRenderer->targetImage,
                           target_range,
                           (VkPipelineStageFlags)VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                           (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           (VkDependencyFlags)0);
    }
    pRenderer->pTargetImage   = pTargetImage;
    pRenderer->renderRecorded = FALSE;
    pRenderer->frameActive    = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A single shader invocation writes one target pixel. The shader derives the complete animated
 *  image from the pixel coordinate and frame index, so rendering performs no host upload.
 */
static void RenderFrame(Gfx_Renderer_t* pRenderer, uint32_t frameIndex)
{
    RenderPushConstants_t push_constants;
    VkExtent2D            item_count;
    VkExtent2D            items_per_invocation;
    VkExtent2D            local_size;

    LOGASSERT(NULL != pRenderer);
    LOGASSERT(FALSE != pRenderer->frameActive);
    LOGASSERT(FALSE == pRenderer->renderRecorded);

    push_constants.frameIndex   = frameIndex;
    item_count.width            = pRenderer->width;
    item_count.height           = pRenderer->height;
    items_per_invocation.width  = 1;
    items_per_invocation.height = 1;
    local_size.width            = RENDER_LOCAL_SIZE_X;
    local_size.height           = RENDER_LOCAL_SIZE_Y;
    cvkCmdDispatchComputeItems2D(pRenderer->pTargetImage->vk.commandBuffer,
                                 pRenderer->pipeline,
                                 pRenderer->pipelineLayout,
                                 0,
                                 1,
                                 &pRenderer->descriptorSet,
                                 0,
                                 NULL,
                                 0,
                                 sizeof(push_constants),
                                 &push_constants,
                                 item_count,
                                 items_per_invocation,
                                 local_size);
    pRenderer->renderRecorded = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The render and target-to-backing-image blit occupy one command buffer and one submission. The
 *  backing image semaphore therefore represents completion of both operations and becomes the
 *  dependency that subsequent GPU work must wait on before reading the image.
 */
static void EndFrame(Gfx_Renderer_t* pRenderer)
{
    Gfx_Image_t*             target_image;
    VkPipelineStageFlags     destination_stage_mask;
    VkAccessFlags            destination_access_mask;
    VkImageLayout            destination_layout;
    VkImageSubresourceLayers source_subresource;
    VkImageSubresourceLayers destination_subresource;
    VkOffset2D               image_offset;
    VkExtent2D               source_extent;
    VkExtent2D               destination_extent;

    LOGASSERT(NULL != pRenderer);
    LOGASSERT(FALSE != pRenderer->frameActive);
    LOGASSERT(FALSE != pRenderer->renderRecorded);
    target_image = pRenderer->pTargetImage;

    if (target_image->vk.layoutInitialized)
    {
        destination_stage_mask  = target_image->vk.readyStageMask;
        destination_access_mask = target_image->vk.readyAccessMask;
        destination_layout      = VK_IMAGE_LAYOUT_GENERAL;
    }
    else
    {
        destination_stage_mask  = (VkPipelineStageFlags)VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destination_access_mask = (VkAccessFlags)0;
        destination_layout      = VK_IMAGE_LAYOUT_UNDEFINED;
    }
    source_subresource        = cvkGetFormatBaseSubresourceLayers(VK_FORMAT_R8G8B8A8_UNORM);
    destination_subresource   = cvkGetFormatBaseSubresourceLayers(target_image->vk.format);
    image_offset.x            = 0;
    image_offset.y            = 0;
    source_extent.width       = pRenderer->width;
    source_extent.height      = pRenderer->height;
    destination_extent.width  = target_image->vk.extent.width;
    destination_extent.height = target_image->vk.extent.height;
    cvkCmdTransferBlitImage2D(target_image->vk.commandBuffer,
                              (VkDependencyFlags)0,
                              NULL,
                              (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                              VK_IMAGE_LAYOUT_GENERAL,
                              VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                              pRenderer->targetImage,
                              source_subresource,
                              NULL,
                              destination_stage_mask,
                              destination_access_mask,
                              destination_layout,
                              VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                              target_image->vk.image,
                              destination_subresource,
                              VK_FILTER_NEAREST,
                              NULL,
                              (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                              (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                              VK_IMAGE_LAYOUT_GENERAL,
                              NULL,
                              (VkPipelineStageFlags)VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                              (VkAccessFlags)0,
                              VK_IMAGE_LAYOUT_GENERAL,
                              image_offset,
                              source_extent,
                              image_offset,
                              destination_extent);
    cvkCmdWriteTimestamp(target_image->vk.commandBuffer,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         pRenderer->queryPool,
                         (pRenderer->activeQuerySlot * RENDER_QUERY_COUNT) + RENDER_QUERY_END);
    cvkEndCommandBuffer(target_image->vk.commandBuffer);
    cvkResetFence(target_image->vk.device, target_image->vk.uploadFence);
    cvkQueueSubmitCommandBuffer(target_image->vk.queue,
                                NULL,
                                0,
                                NULL,
                                NULL,
                                1,
                                &target_image->vk.uploadSemaphore,
                                target_image->vk.uploadFence,
                                target_image->vk.commandBuffer);
    pRenderer->targetInitialized       = TRUE;
    target_image->vk.layoutInitialized = TRUE;
    target_image->vk.readyStageMask    = (VkPipelineStageFlags)VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT;
    target_image->vk.readyAccessMask   = (VkAccessFlags)0;
    target_image->vk.uploadSubmitted   = TRUE;
    target_image->uploadPending        = TRUE;
    pRenderer->querySlots[pRenderer->activeQuerySlot].pending = TRUE;
    pRenderer->pTargetImage                                   = NULL;
    pRenderer->frameActive                                    = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void GetStatistics(Gfx_Renderer_t* pRenderer, Gfx_Renderer_Statistics_t* pStatistics)
{
    uint32_t slot_index;

    LOGASSERT(NULL != pRenderer);
    LOGASSERT(NULL != pStatistics);
    LOGASSERT(FALSE == pRenderer->frameActive);

    for (slot_index = 0; slot_index < CVK_MAX_SWAPCHAIN_IMAGES; ++slot_index)
    {
        CollectQuerySlot(pRenderer, slot_index);
    }
    (*pStatistics) = pRenderer->statistics;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Renderer_Create(Gfx_Renderer_t** ppRenderer, const Gfx_Renderer_CreateInfo_t* pCreateInfo)
{
    LOGASSERT(NULL != ppRenderer);
    LOGASSERT(NULL != pCreateInfo);

    (*ppRenderer) = (Gfx_Renderer_t*)OS_CALLOC(sizeof((**ppRenderer)));
    CreateRenderer((*ppRenderer), pCreateInfo);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Renderer_BeginFrame(Gfx_Renderer_t* pRenderer, Gfx_Image_t* pTargetImage)
{
    BeginFrame(pRenderer, pTargetImage);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Renderer_Render(Gfx_Renderer_t* pRenderer, uint32_t frameIndex)
{
    RenderFrame(pRenderer, frameIndex);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Renderer_EndFrame(Gfx_Renderer_t* pRenderer)
{
    EndFrame(pRenderer);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Renderer_GetStatistics(Gfx_Renderer_t* pRenderer, Gfx_Renderer_Statistics_t* pStatistics)
{
    GetStatistics(pRenderer, pStatistics);
}
