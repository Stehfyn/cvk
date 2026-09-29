/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cvk/cvk.h"
#include "os/os_assert.h"
#include "gfx/gfx_image.h"
#include "os/os_malloc.h"
#include "gfx/gfx_surface.h"
#include "gfx/gfx_swapchain.h"
#include "gfx/gfx_swapchain_platform.h"
#include "utl/utl_defines.h"

#include <stdint.h>
#include <string.h>

#include "c_bgraToNv12Spirv.h"
#include "c_rgbaToNv12Spirv.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define NV12_CONVERT_PIXELS_PER_INVOCATION_X (4U) ///< Horizontal output pixels produced per compute invocation.
#define NV12_CONVERT_PIXELS_PER_INVOCATION_Y (2U) ///< Vertical output pixels produced per compute invocation.
#define NV12_CONVERT_PIXELS_PER_INVOCATION_Z (1U) ///< Depth slices produced per compute invocation.
#define NV12_CONVERT_LOCAL_SIZE_X            (8U) ///< Compute workgroup width in invocations.
#define NV12_CONVERT_LOCAL_SIZE_Y            (8U) ///< Compute workgroup height in invocations.
#define NV12_CONVERT_LOCAL_SIZE_Z            (1U) ///< Compute workgroup depth in invocations.
#define MAX_CONVERT_DESCRIPTOR_BINDING_COUNT (2U) ///< Maximum descriptor bindings used by a conversion route.
#define NV12_CONVERT_INPUT_IMAGE_BINDING     (0U) ///< Descriptor binding for the four-channel source image.
#define NV12_CONVERT_OUTPUT_BUFFER_BINDING   (1U) ///< Descriptor binding for the NV12 destination buffer.
#define INITIAL_IMAGE_INDEX                  (0U) ///< Initial round-robin logical image index.

#define NV12_Y_PLANE_INDEX  (0) ///< Luma plane in Vulkan's NV12 plane layout.
#define NV12_UV_PLANE_INDEX (1) ///< Interleaved chroma plane in Vulkan's NV12 plane layout.
#define NV12_PLANE_COUNT    (2) ///< Plane count required by the conversion shader.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Tracks exclusive ownership of each logical swapchain image slot.
 */
typedef enum SlotState_e
{
    SLOT_STATE_AVAILABLE, ///< Slot may be returned by AcquireNextImage().
    SLOT_STATE_SUBMITTED, ///< GPU work retains the slot until its fence completes.
    SLOT_STATE_ACQUIRED,  ///< Slot is caller-owned until SubmitImage().
} SlotState_t;

/*************************************************************************************************/
/**
 * \brief Identifies the conversion selected from the logical source and destination formats.
 */
typedef enum ConvertRoute_e
{
    CONVERT_ROUTE_RGBA8_TO_NV12, ///< Converts an RGBA8 source into an NV12 surface.
    CONVERT_ROUTE_BGRA8_TO_NV12, ///< Converts a BGRA8 source into an NV12 surface.
} ConvertRoute_t;

/*************************************************************************************************/
/**
 * \brief Addressable word of packed output samples in a shader storage buffer.
 */
typedef uint32_t Nv12OutputWord_t;

/*************************************************************************************************/
/**
 * \brief Destination dimensions, row strides, and plane offset passed to the shader.
 */
typedef struct Nv12PushConstants_s
{
    uint32_t width;         ///< Visible output width in pixels.
    uint32_t height;        ///< Visible output height in pixels.
    uint32_t yStrideWords;  ///< Luma row stride in 32-bit output words.
    uint32_t uvOffsetWords; ///< Word offset from buffer start to the interleaved UV plane.
    uint32_t uvStrideWords; ///< Chroma row stride in 32-bit output words.
} Nv12PushConstants_t;

/*************************************************************************************************/
/**
 * \brief Vulkan resources used to convert one backing image.
 */
typedef struct VulkanImageResources_s
{
    VkCommandPool         commandPool;         ///< Pool from which commandBuffer is allocated.
    VkCommandBuffer       commandBuffer;       ///< Commands recorded to convert the backing image.
    VkFence               completionFence;     ///< Signals completion of the submitted conversion commands.
    VkBuffer              destinationBuffer;   ///< Buffer receiving converted image samples.
    VkDeviceMemory        destinationMemory;   ///< Memory bound to destinationBuffer.
    VkDescriptorSetLayout descriptorSetLayout; ///< Resource binding interface used by the conversion shader.
    VkDescriptorPool      descriptorPool;      ///< Storage for descriptorSet.
    VkDescriptorSet       descriptorSet;       ///< Source image and destination buffer bindings.
    VkShaderModule        shaderModule;        ///< Shader code used to create pipeline.
    VkPipelineLayout      pipelineLayout;      ///< Descriptor-set and push-constant interface for pipeline.
    VkPipeline            pipeline;            ///< Pipeline bound when recording conversion commands.
    uint32_t              yStride;             ///< Luma row stride in destinationBuffer, in bytes.
    uint32_t              uvStride;            ///< Chroma row stride in destinationBuffer, in bytes.
    uint32_t              uvOffset;            ///< Byte offset of the chroma plane in destinationBuffer.
} VulkanImageResources_t;

/*************************************************************************************************/
/**
 * \brief Owns the backing-image ring and tracks acquisition and submission state.
 */
struct Gfx_Swapchain_s
{
    Gfx_Swapchain_Kind_t kind;               ///< Operation performed on each submitted image.
    Gfx_Swapchain_Hint_t hint;               ///< Backend used to process image submissions.
    Gfx_Image_Format_t   imageFormat;        ///< Logical caller-visible image format.
    ConvertRoute_t       convertRoute;       ///< Route selected when the swapchain is created.
    uint32_t             outputWordSize;     ///< Size of an output-buffer word, in bytes.
    uint32_t             imageCount;         ///< Number of slots in images and states.
    uint32_t             nextImageIndex;     ///< Next round-robin slot considered for acquisition.
    uint32_t             acquiredImageIndex; ///< Caller-owned slot, or VK_INVALID when no image is acquired.
    Gfx_Surface_t*       surface;            ///< Target surface owned by the caller.
    Gfx_Image_t*         images[CVK_MAX_SWAPCHAIN_IMAGES]; ///< Caller-facing images owned by this swapchain.
    VulkanImageResources_t
                imageResources[CVK_MAX_SWAPCHAIN_IMAGES]; ///< Conversion resources indexed by backing-image slot.
    SlotState_t states[CVK_MAX_SWAPCHAIN_IMAGES];         ///< Ownership states parallel to images.
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
 * \brief Resolves a conversion route from the source image and destination surface formats.
 *
 * \param[in] imageFormat      Logical source image format
 * \param[in] surfaceFormat    Destination surface pixel format
 *
 * \return Matching conversion route; unsupported format pairs are fatal.
 */
static ConvertRoute_t GetConvertRoute(Gfx_Image_Format_t imageFormat, Gfx_Surface_PixelFormat_t surfaceFormat);

/*************************************************************************************************/
/**
 * \brief Maps an image's pixel layout to its Vulkan format.
 *
 * \param[in] imageFormat    Image pixel layout
 *
 * \return Corresponding Vulkan format; unsupported values are fatal.
 */
static VkFormat GetVulkanImageFormat(Gfx_Image_Format_t imageFormat);

/*************************************************************************************************/
/**
 * \brief Returns the storage-buffer word size used by a conversion route.
 *
 * \param[in] convertRoute    Conversion route whose output representation is queried
 *
 * \return Output storage word size in bytes; unsupported routes are fatal.
 */
static uint32_t GetVulkanConvertOutputWordSize(ConvertRoute_t convertRoute);

/*************************************************************************************************/
/**
 * \brief Selects the descriptor schema used by a conversion route.
 *
 * \param[in]  convertRoute         Conversion route whose descriptor schema is queried
 * \par Requirements:
 *  - 001 The route shall be RGBA8-to-NV12 or BGRA8-to-NV12; other routes shall assert.
 *  - 002 Both output arrays shall provide MAX_CONVERT_DESCRIPTOR_BINDING_COUNT entries.
 *  - 003 Each layout shall contain one sampled image and one writable storage buffer.
 *
 * \param[out] descriptorBindings  Receives the two complete descriptor layout bindings
 * \param[out] descriptorPoolSizes Receives the corresponding distinct pool resource counts
 *
 * \retval    None   No result value.
 */
static void GetVulkanConvertDescriptorSchema(
    ConvertRoute_t               convertRoute,
    VkDescriptorSetLayoutBinding descriptorBindings[MAX_CONVERT_DESCRIPTOR_BINDING_COUNT],
    VkDescriptorPoolSize         descriptorPoolSizes[MAX_CONVERT_DESCRIPTOR_BINDING_COUNT]);

/*************************************************************************************************/
/**
 * \brief Selects the SPIR-V conversion shader for a conversion route.
 *
 * \param[in]  convertRoute     Conversion route selecting the shader
 * \param[out] shaderCodeSize   Receives the shader byte count
 *
 * \return Selected SPIR-V shader code; unsupported routes are fatal.
 */
static const uint32_t* GetVulkanConvertShaderCode(ConvertRoute_t convertRoute, size_t* shaderCodeSize);

/*************************************************************************************************/
/**
 * \brief Creates all Vulkan resources required by a conversion swapchain.
 *
 * \param[in]  physicalDevice             Physical device owning the resources
 * \param[in]  device                     Logical device owning the resources
 * \param[in]  queue                      Queue used by logical image memory operations
 * \param[in]  queueFamilyIndex           Command queue family
 * \param[in]  width                      Image width in pixels
 * \param[in]  height                     Image height in pixels
 * \param[in]  imageCount                 Number of backing-image slots to create
 * \param[in]  sourceFormat               Vulkan format of each source image view
 * \param[in]  convertRoute               Operation selected from the source and destination formats
 * \param[in]  outputWordSize             Size of an output-buffer word, in bytes
 * \param[in]  pDestinationImage          Surface-owned Vulkan destination image
 * \param[in]  pAllocator                 Optional Vulkan allocator
 * \param[out] pCommandPool               Receives imageCount reusable command pools
 * \param[out] pCommandBuffer             Receives imageCount primary command buffers
 * \param[out] pCompletionFence           Receives imageCount submission completion fences
 * \param[out] pDestinationBuffer         Receives imageCount output buffers
 * \param[out] pDestinationMemory         Receives imageCount output-buffer memory handles
 * \param[out] pYStride                   Receives imageCount luma row strides in bytes
 * \param[out] pUvOffset                  Receives imageCount chroma plane offsets in bytes
 * \param[out] pUvStride                  Receives imageCount chroma row strides in bytes
 * \param[out] pDescriptorSetLayout       Receives imageCount descriptor-set layouts
 * \param[out] pDescriptorPool            Receives imageCount descriptor pools
 * \param[out] pDescriptorSet             Receives imageCount initialized descriptor sets
 * \param[out] pShaderModule              Receives imageCount shader modules
 * \param[out] pPipelineLayout            Receives imageCount compute pipeline layouts
 * \param[out] pPipeline                  Receives imageCount compute pipelines
 * \param[out] ppBackingImages            Receives imageCount swapchain-owned backing images
 *
 * \retval    None   No result value.
 */
static void CreateVulkanConvertSwapchainResources(VkPhysicalDevice             physicalDevice,
                                                  VkDevice                     device,
                                                  VkQueue                      queue,
                                                  uint32_t                     queueFamilyIndex,
                                                  uint32_t                     width,
                                                  uint32_t                     height,
                                                  uint32_t                     imageCount,
                                                  VkFormat                     sourceFormat,
                                                  ConvertRoute_t               convertRoute,
                                                  uint32_t                     outputWordSize,
                                                  Gfx_Image_t*                 pDestinationImage,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkCommandPool*               pCommandPool,
                                                  VkCommandBuffer*             pCommandBuffer,
                                                  VkFence*                     pCompletionFence,
                                                  VkBuffer*                    pDestinationBuffer,
                                                  VkDeviceMemory*              pDestinationMemory,
                                                  uint32_t*                    pYStride,
                                                  uint32_t*                    pUvOffset,
                                                  uint32_t*                    pUvStride,
                                                  VkDescriptorSetLayout*       pDescriptorSetLayout,
                                                  VkDescriptorPool*            pDescriptorPool,
                                                  VkDescriptorSet*             pDescriptorSet,
                                                  VkShaderModule*              pShaderModule,
                                                  VkPipelineLayout*            pPipelineLayout,
                                                  VkPipeline*                  pPipeline,
                                                  Gfx_Image_t**                ppBackingImages);

/*************************************************************************************************/
/**
 * \brief Validates the operation, backing-image configuration, and target surface.
 *
 * \param[in] pCreateInfo    Swapchain configuration to validate
 *
 * \retval    None   No result value.
 */
static void ValidateCreateInfo(const Gfx_Swapchain_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Initializes the backing-image ring and its per-image Vulkan conversion resources.
 *
 * \param[out] swapchain     Swapchain receiving backing images and per-image conversion resources
 * \param[in]  pCreateInfo    Validated swapchain configuration
 *
 * \retval    None   No result value.
 */
static void CreateVulkanConvertSwapchain(Gfx_Swapchain_t* swapchain, const Gfx_Swapchain_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Creates the conversion image ring and backend resources.
 *
 * \param[out] swapchain     Destination swapchain
 * \param[in]  pCreateInfo    Conversion request whose backend hint selects the implementation
 *
 * \retval    None   No result value.
 */
static void CreateConvertSwapchain(Gfx_Swapchain_t* swapchain, const Gfx_Swapchain_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Validates the configuration and creates backing images and conversion resources.
 *
 * \param[out] swapchain     Zero-initialized destination storage
 * \param[in]  pCreateInfo    Kind-specific creation request
 *
 * \retval    None   No result value.
 */
static void CreateSwapchain(Gfx_Swapchain_t* swapchain, const Gfx_Swapchain_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Reserves the next backing image and synchronizes the shared conversion target.
 *
 * \param[in,out] swapchain    Swapchain whose round-robin slot and surface state advance
 * \param[in]     timeout       Nanosecond Vulkan slot-completion timeout
 * \param[out]    ppImage       Receives the acquired backing image; timeout or invalid acquisition state is fatal
 *
 * \retval    None   No result value.
 */
static void AcquireNextImage(Gfx_Swapchain_t* swapchain, uint64_t timeout, Gfx_Image_t** ppImage);

/*************************************************************************************************/
/**
 * \brief Records and dispatches the Vulkan commands for an acquired image's conversion.
 *
 * \param[in]        device                       Device owning the conversion command resources
 * \param[in]        queue                        Queue receiving the conversion commands
 * \param[in]        queueFamilyIndex             Queue family used for external ownership transfers
 * \param[in]        width                        Visible conversion width in pixels
 * \param[in]        height                       Visible conversion height in pixels
 * \param[in]        convertRoute                 Conversion selected from the source and destination formats
 * \param[in]        outputWordSize               Size of an output-buffer word in bytes
 * \param[in]        commandPool                  Pool containing the reusable conversion command buffer
 * \param[in]        commandBuffer                Command buffer receiving conversion commands
 * \param[in]        completionFence              Fence signaling conversion completion
 * \param[in]        destinationBuffer            Buffer receiving converted samples
 * \param[in]        yStride                      Luma row stride in bytes
 * \param[in]        uvOffset                     Chroma plane offset in bytes
 * \param[in]        uvStride                     Chroma row stride in bytes
 * \param[in]        pipeline                     Compute conversion pipeline
 * \param[in]        pipelineLayout               Descriptor and push-constant interface
 * \param[in]        descriptorSet                Source image and destination buffer bindings
 * \param[in]        uploadDevice                 Device owning the source upload fence
 * \param[in]        uploadQueue                  Queue receiving any pending source upload
 * \param[in]        uploadCommandBuffer          Source upload commands
 * \param[in]        uploadFence                  Fence signaling source upload completion
 * \param[in]        uploadSemaphore              Semaphore waited on before reading the source image
 * \param[in]        uploadSubmitted              Whether the source upload has already been submitted
 * \param[in]        destinationImage             External image receiving output when no external buffer is used
 * \param[in]        destinationFormat            Destination image format
 * \param[in]        destinationWidth             Destination image width in pixels
 * \param[in]        destinationHeight            Destination image height in pixels
 * \param[in]        destinationLayoutInitialized Whether the destination has been used by a previous submission
 * \param[in]        destinationHasExternalBuffer Whether output is written through an external buffer
 * \param[in]        destinationRequiresHostCopy  Whether output must be made available for a host copy
 * \param[in]        externalSemaphore            Optional destination timeline semaphore
 * \param[in,out]    externalSemaphoreValue       Current destination timeline value, advanced for this submission
 *
 * \retval    None   No result value.
 */
static void DispatchVulkanConvertImage(VkDevice         device,
                                       VkQueue          queue,
                                       uint32_t         queueFamilyIndex,
                                       uint32_t         width,
                                       uint32_t         height,
                                       ConvertRoute_t   convertRoute,
                                       uint32_t         outputWordSize,
                                       VkCommandPool    commandPool,
                                       VkCommandBuffer  commandBuffer,
                                       VkFence          completionFence,
                                       VkBuffer         destinationBuffer,
                                       uint32_t         yStride,
                                       uint32_t         uvOffset,
                                       uint32_t         uvStride,
                                       VkPipeline       pipeline,
                                       VkPipelineLayout pipelineLayout,
                                       VkDescriptorSet  descriptorSet,
                                       VkDevice         uploadDevice,
                                       VkQueue          uploadQueue,
                                       VkCommandBuffer  uploadCommandBuffer,
                                       VkFence          uploadFence,
                                       VkSemaphore      uploadSemaphore,
                                       bool_t           uploadSubmitted,
                                       VkImage          destinationImage,
                                       VkFormat         destinationFormat,
                                       uint32_t         destinationWidth,
                                       uint32_t         destinationHeight,
                                       bool_t           destinationLayoutInitialized,
                                       bool_t           destinationHasExternalBuffer,
                                       bool_t           destinationRequiresHostCopy,
                                       VkSemaphore      externalSemaphore,
                                       uint64_t*        externalSemaphoreValue);

/*************************************************************************************************/
/**
 * \brief Validates the acquired image, dispatches its conversion, and publishes the target image.
 *
 * \param[in,out] swapchain Swapchain containing the conversion route, conversion resources, and target surface
 * \param[in,out] image     Acquired backing image whose pending writes are consumed by this submission
 *
 * \retval    None   No result value.
 */
static void SubmitVulkanConvertImage(Gfx_Swapchain_t* swapchain, Gfx_Image_t* image);

/*************************************************************************************************/
/**
 * \brief Submits an acquired image for conversion into the target surface.
 *
 * \param[in,out] swapchain    Conversion swapchain owning the acquired image
 * \param[in,out] image        Acquired image submitted for conversion
 *
 * \retval    None   No result value.
 */
static void SubmitConvertImage(Gfx_Swapchain_t* swapchain, Gfx_Image_t* image);

/*************************************************************************************************/
/**
 * \brief Submits an acquired image for the selected swapchain operation.
 *
 * \param[in,out] swapchain    Owning swapchain whose slot becomes available after backend work
 * \param[in,out] image        Acquired member image; foreign or already released images are fatal
 *
 * \retval    None   No result value.
 */
static void SubmitImage(Gfx_Swapchain_t* swapchain, Gfx_Image_t* image);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static ConvertRoute_t GetConvertRoute(Gfx_Image_Format_t imageFormat, Gfx_Surface_PixelFormat_t surfaceFormat)
{
    if (GFX_SURFACE_PIXELFORMAT_NV12 == surfaceFormat)
    {
        switch (imageFormat)
        {
        case GFX_IMAGE_FORMAT_RGBA8:
            return CONVERT_ROUTE_RGBA8_TO_NV12;
        case GFX_IMAGE_FORMAT_BGRA8:
            return CONVERT_ROUTE_BGRA8_TO_NV12;
        default:
            break;
        }
    }
    LOGFATAL("Unsupported swapchain conversion route!");
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static VkFormat GetVulkanImageFormat(Gfx_Image_Format_t imageFormat)
{
    switch (imageFormat)
    {
    case GFX_IMAGE_FORMAT_RGBA8:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case GFX_IMAGE_FORMAT_BGRA8:
        return VK_FORMAT_B8G8R8A8_UNORM;

    default:
        LOGFATAL("Unsupported image format!");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint32_t GetVulkanConvertOutputWordSize(ConvertRoute_t convertRoute)
{
    switch (convertRoute)
    {
    case CONVERT_ROUTE_RGBA8_TO_NV12:
    case CONVERT_ROUTE_BGRA8_TO_NV12:
        return (uint32_t)sizeof(Nv12OutputWord_t);

    default:
        LOGFATAL("Unsupported Vulkan conversion route!");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void GetVulkanConvertDescriptorSchema(
    ConvertRoute_t               convertRoute,
    VkDescriptorSetLayoutBinding descriptorBindings[MAX_CONVERT_DESCRIPTOR_BINDING_COUNT],
    VkDescriptorPoolSize         descriptorPoolSizes[MAX_CONVERT_DESCRIPTOR_BINDING_COUNT])
{
    bool_t is_rgba;
    bool_t is_bgra;
    bool_t is_supported;

    is_rgba      = (CONVERT_ROUTE_RGBA8_TO_NV12 == convertRoute);
    is_bgra      = (CONVERT_ROUTE_BGRA8_TO_NV12 == convertRoute);
    is_supported = is_rgba || is_bgra;
    LOGASSERT(is_supported);
    LOGASSERT(NULL != descriptorBindings);
    LOGASSERT(NULL != descriptorPoolSizes);

    cvkInitDescriptorSetLayoutBinding(&descriptorBindings[NV12_CONVERT_INPUT_IMAGE_BINDING],
                                      NV12_CONVERT_INPUT_IMAGE_BINDING,
                                      VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                      1,
                                      (VkShaderStageFlags)VK_SHADER_STAGE_COMPUTE_BIT,
                                      NULL);
    cvkInitDescriptorSetLayoutBinding(&descriptorBindings[NV12_CONVERT_OUTPUT_BUFFER_BINDING],
                                      NV12_CONVERT_OUTPUT_BUFFER_BINDING,
                                      VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                      1,
                                      (VkShaderStageFlags)VK_SHADER_STAGE_COMPUTE_BIT,
                                      NULL);
    cvkInitDescriptorPoolSize(&descriptorPoolSizes[NV12_CONVERT_INPUT_IMAGE_BINDING],
                              VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                              1);
    cvkInitDescriptorPoolSize(&descriptorPoolSizes[NV12_CONVERT_OUTPUT_BUFFER_BINDING],
                              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                              1);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static const uint32_t* GetVulkanConvertShaderCode(ConvertRoute_t convertRoute, size_t* shaderCodeSize)
{
    switch (convertRoute)
    {
    case CONVERT_ROUTE_RGBA8_TO_NV12:
        (*shaderCodeSize) = sizeof(c_rgbaToNv12Spirv);
        return c_rgbaToNv12Spirv;

    case CONVERT_ROUTE_BGRA8_TO_NV12:
        (*shaderCodeSize) = sizeof(c_bgraToNv12Spirv);
        return c_bgraToNv12Spirv;

    default:
        LOGFATAL("Unsupported Vulkan conversion route!");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateVulkanConvertSwapchainResources(VkPhysicalDevice             physicalDevice,
                                                  VkDevice                     device,
                                                  VkQueue                      queue,
                                                  uint32_t                     queueFamilyIndex,
                                                  uint32_t                     width,
                                                  uint32_t                     height,
                                                  uint32_t                     imageCount,
                                                  VkFormat                     sourceFormat,
                                                  ConvertRoute_t               convertRoute,
                                                  uint32_t                     outputWordSize,
                                                  Gfx_Image_t*                 pDestinationImage,
                                                  const VkAllocationCallbacks* pAllocator,
                                                  VkCommandPool*               pCommandPool,
                                                  VkCommandBuffer*             pCommandBuffer,
                                                  VkFence*                     pCompletionFence,
                                                  VkBuffer*                    pDestinationBuffer,
                                                  VkDeviceMemory*              pDestinationMemory,
                                                  uint32_t*                    pYStride,
                                                  uint32_t*                    pUvOffset,
                                                  uint32_t*                    pUvStride,
                                                  VkDescriptorSetLayout*       pDescriptorSetLayout,
                                                  VkDescriptorPool*            pDescriptorPool,
                                                  VkDescriptorSet*             pDescriptorSet,
                                                  VkShaderModule*              pShaderModule,
                                                  VkPipelineLayout*            pPipelineLayout,
                                                  VkPipeline*                  pPipeline,
                                                  Gfx_Image_t**                ppBackingImages)
{
    Gfx_Image_CreateInfo_t       image_create_info;
    VkDescriptorSetLayoutBinding descriptor_bindings[MAX_CONVERT_DESCRIPTOR_BINDING_COUNT];
    VkDescriptorPoolSize         descriptor_pool_sizes[MAX_CONVERT_DESCRIPTOR_BINDING_COUNT];
    VkDescriptorSetLayoutSupport descriptor_support;
    const uint32_t*              shader_code;
    VkDeviceSize                 destination_buffer_size;
    size_t                       shader_code_size;
    VkDeviceSize                 plane_offsets[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                     plane_strides[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkExtent3D                   plane_extents[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                     plane_count;
    bool_t                       needs_buffer;
    uint32_t                     image_index;
    VkCommandPool                upload_command_pool;
    VkCommandBuffer              upload_command_buffer;

    GetVulkanConvertDescriptorSchema(convertRoute, descriptor_bindings, descriptor_pool_sizes);
    cvkGetDescriptorSetLayoutSupport(device,
                                     NULL,
                                     (VkDescriptorSetLayoutCreateFlags)0,
                                     MAX_CONVERT_DESCRIPTOR_BINDING_COUNT,
                                     descriptor_bindings,
                                     NULL,
                                     &descriptor_support);
    LOGASSERT(VK_TRUE == descriptor_support.supported);
    shader_code  = GetVulkanConvertShaderCode(convertRoute, &shader_code_size);
    needs_buffer = (VK_NULL_HANDLE == pDestinationImage->vk.externalBuffer);
    if (needs_buffer)
    {
        LOGASSERT(VK_FORMAT_G8_B8R8_2PLANE_420_UNORM == pDestinationImage->vk.format);
        plane_count = cvkCalculateFormatPlaneLayouts(pDestinationImage->vk.format,
                                                     width,
                                                     height,
                                                     1,
                                                     1,
                                                     outputWordSize,
                                                     plane_offsets,
                                                     plane_strides,
                                                     plane_extents,
                                                     &destination_buffer_size);
        LOGASSERT(NV12_PLANE_COUNT == plane_count);
        LOGASSERT(UINT32_MAX >= plane_offsets[NV12_UV_PLANE_INDEX]);
    }
    for (image_index = 0; image_index < imageCount; ++image_index)
    {
        cvkSetupCommandPool(device,
                            NULL,
                            (VkCommandPoolCreateFlags)0,
                            queueFamilyIndex,
                            NULL,
                            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                            1,
                            pAllocator,
                            &pCommandPool[image_index],
                            &pCommandBuffer[image_index]);
        cvkSetupCommandPool(device,
                            NULL,
                            (VkCommandPoolCreateFlags)0,
                            queueFamilyIndex,
                            NULL,
                            VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                            1,
                            pAllocator,
                            &upload_command_pool,
                            &upload_command_buffer);
        cvkCreateFence(device,
                       NULL,
                       (VkFenceCreateFlags)VK_FENCE_CREATE_SIGNALED_BIT,
                       pAllocator,
                       &pCompletionFence[image_index]);
        if (FALSE == needs_buffer)
        {
            pDestinationBuffer[image_index] = pDestinationImage->vk.externalBuffer;
            pDestinationMemory[image_index] = pDestinationImage->vk.externalBufferMemory;
            pYStride[image_index]           = pDestinationImage->vk.externalYStride;
            pUvOffset[image_index]          = pDestinationImage->vk.externalUvOffset;
            pUvStride[image_index]          = pDestinationImage->vk.externalUvStride;
            LOGASSERT(0 == (pYStride[image_index] % outputWordSize));
            LOGASSERT(0 == (pUvOffset[image_index] % outputWordSize));
            LOGASSERT(0 == (pUvStride[image_index] % outputWordSize));
        }
        else
        {
            cvkSetupBuffer(physicalDevice,
                           device,
                           NULL,
                           (VkBufferCreateFlags)0,
                           destination_buffer_size,
                           (VkBufferUsageFlags)((VkFlags)VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                (VkFlags)VK_BUFFER_USAGE_TRANSFER_SRC_BIT),
                           VK_SHARING_MODE_EXCLUSIVE,
                           0,
                           NULL,
                           (VkMemoryPropertyFlags)VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                           NULL,
                           pAllocator,
                           &pDestinationBuffer[image_index],
                           &pDestinationMemory[image_index]);
            pYStride[image_index]  = plane_strides[NV12_Y_PLANE_INDEX];
            pUvOffset[image_index] = (uint32_t)plane_offsets[NV12_UV_PLANE_INDEX];
            pUvStride[image_index] = plane_strides[NV12_UV_PLANE_INDEX];
        }
        image_create_info.hint               = GFX_IMAGE_HINT_VULKAN;
        image_create_info.kind               = GFX_IMAGE_KIND_STORAGE;
        image_create_info.width              = width;
        image_create_info.height             = height;
        image_create_info.platformCreateInfo = NULL;
        image_create_info.va.dpy             = NULL;
        image_create_info.va.format          = 0;
        image_create_info.va.pixelFormat     = 0;
        image_create_info.vk.physicalDevice  = physicalDevice;
        image_create_info.vk.device          = device;
        image_create_info.vk.queue           = queue;
        image_create_info.vk.pAllocator      = pAllocator;
        image_create_info.vk.format          = sourceFormat;
        image_create_info.vk.usage =
            (VkImageUsageFlags)((VkFlags)VK_IMAGE_USAGE_SAMPLED_BIT | (VkFlags)VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        image_create_info.vk.commandPool   = upload_command_pool;
        image_create_info.vk.commandBuffer = upload_command_buffer;
        Gfx_Image_Create(&ppBackingImages[image_index], &image_create_info);
        cvkCreateFence(device, NULL, (VkFenceCreateFlags)0, pAllocator, &ppBackingImages[image_index]->vk.uploadFence);
        cvkCreateSemaphore(device,
                           NULL,
                           (VkSemaphoreCreateFlags)0,
                           pAllocator,
                           &ppBackingImages[image_index]->vk.uploadSemaphore);
        cvkCreateDescriptorSetLayout(device,
                                     NULL,
                                     (VkDescriptorSetLayoutCreateFlags)0,
                                     MAX_CONVERT_DESCRIPTOR_BINDING_COUNT,
                                     descriptor_bindings,
                                     pAllocator,
                                     &pDescriptorSetLayout[image_index]);
        cvkCreateDescriptorPool(device,
                                NULL,
                                (VkDescriptorPoolCreateFlags)0,
                                1,
                                MAX_CONVERT_DESCRIPTOR_BINDING_COUNT,
                                descriptor_pool_sizes,
                                pAllocator,
                                &pDescriptorPool[image_index]);
        cvkAllocateDescriptorSets(device,
                                  NULL,
                                  pDescriptorPool[image_index],
                                  1,
                                  &pDescriptorSetLayout[image_index],
                                  &pDescriptorSet[image_index]);
        cvkUpdateImageDescriptorSet(device,
                                    VK_NULL_HANDLE,
                                    ppBackingImages[image_index]->vk.view,
                                    VK_IMAGE_LAYOUT_GENERAL,
                                    NULL,
                                    pDescriptorSet[image_index],
                                    descriptor_bindings[NV12_CONVERT_INPUT_IMAGE_BINDING].binding,
                                    0,
                                    descriptor_bindings[NV12_CONVERT_INPUT_IMAGE_BINDING].descriptorType);
        cvkUpdateBufferDescriptorSet(device,
                                     pDestinationBuffer[image_index],
                                     VK_NO_OFFSET,
                                     VK_WHOLE_SIZE,
                                     NULL,
                                     pDescriptorSet[image_index],
                                     descriptor_bindings[NV12_CONVERT_OUTPUT_BUFFER_BINDING].binding,
                                     0,
                                     descriptor_bindings[NV12_CONVERT_OUTPUT_BUFFER_BINDING].descriptorType);
        CVK_CHECK(cvkSetupComputePipeline(device,
                                          NULL,
                                          shader_code_size,
                                          shader_code,
                                          NULL,
                                          NULL,
                                          1,
                                          &pDescriptorSetLayout[image_index],
                                          VK_NO_OFFSET,
                                          sizeof(Nv12PushConstants_t),
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
                                          pAllocator,
                                          &pShaderModule[image_index],
                                          &pPipelineLayout[image_index],
                                          &pPipeline[image_index]));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ValidateCreateInfo(const Gfx_Swapchain_CreateInfo_t* pCreateInfo)
{
    LOGASSERT(GFX_SWAPCHAIN_KIND_CONVERT == pCreateInfo->kind);
    LOGASSERT(GFX_SWAPCHAIN_HINT_VULKAN == pCreateInfo->hint);
    LOGASSERT((GFX_IMAGE_FORMAT_RGBA8 == pCreateInfo->imageFormat) ||
              (GFX_IMAGE_FORMAT_BGRA8 == pCreateInfo->imageFormat));
    LOGASSERT(NULL != pCreateInfo->surface);
    LOGASSERT((0 != pCreateInfo->width) && (0 != pCreateInfo->height));
    LOGASSERT(0 != pCreateInfo->imageCount);
    LOGASSERT(CVK_MAX_SWAPCHAIN_IMAGES >= pCreateInfo->imageCount);

    LOGASSERT(GFX_SURFACE_KIND_STORAGE == pCreateInfo->surface->kind);
    LOGASSERT(GFX_SURFACE_PIXELFORMAT_NV12 == pCreateInfo->surface->pixelFormat);
    LOGASSERT((pCreateInfo->width == pCreateInfo->surface->width) &&
              (pCreateInfo->height == pCreateInfo->surface->height));
    LOGASSERT(0 == (pCreateInfo->surface->width & 1u));
    LOGASSERT(0 == (pCreateInfo->surface->height & 1u));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each input image has coherent staging storage matching the requested RGBA8 or BGRA8 format.
 *  Conversion writes directly to external buffer storage when available; image-backed targets use
 *  a separate output buffer followed by an image copy.
 */
static void CreateVulkanConvertSwapchain(Gfx_Swapchain_t* swapchain, const Gfx_Swapchain_CreateInfo_t* pCreateInfo)
{
    VkFormat              source_format;
    ConvertRoute_t        convert_route;
    uint32_t              output_word_size;
    Gfx_Image_t*          images[CVK_MAX_SWAPCHAIN_IMAGES];
    VkCommandPool         command_pool[CVK_MAX_SWAPCHAIN_IMAGES];
    VkCommandBuffer       command_buffer[CVK_MAX_SWAPCHAIN_IMAGES];
    VkFence               completion_fence[CVK_MAX_SWAPCHAIN_IMAGES];
    VkBuffer              destination_buffer[CVK_MAX_SWAPCHAIN_IMAGES];
    VkDeviceMemory        destination_memory[CVK_MAX_SWAPCHAIN_IMAGES];
    uint32_t              y_stride[CVK_MAX_SWAPCHAIN_IMAGES];
    uint32_t              uv_offset[CVK_MAX_SWAPCHAIN_IMAGES];
    uint32_t              uv_stride[CVK_MAX_SWAPCHAIN_IMAGES];
    VkDescriptorSetLayout descriptor_set_layout[CVK_MAX_SWAPCHAIN_IMAGES];
    VkDescriptorPool      descriptor_pool[CVK_MAX_SWAPCHAIN_IMAGES];
    VkDescriptorSet       descriptor_set[CVK_MAX_SWAPCHAIN_IMAGES];
    VkShaderModule        shader_module[CVK_MAX_SWAPCHAIN_IMAGES];
    VkPipelineLayout      pipeline_layout[CVK_MAX_SWAPCHAIN_IMAGES];
    VkPipeline            pipeline[CVK_MAX_SWAPCHAIN_IMAGES];
    uint32_t              image_index;

    source_format    = GetVulkanImageFormat(pCreateInfo->imageFormat);
    convert_route    = GetConvertRoute(pCreateInfo->imageFormat, pCreateInfo->surface->pixelFormat);
    output_word_size = GetVulkanConvertOutputWordSize(convert_route);

    CreateVulkanConvertSwapchainResources(pCreateInfo->surface->vk.physicalDevice,
                                          pCreateInfo->surface->vk.device,
                                          pCreateInfo->surface->vk.queue,
                                          pCreateInfo->surface->vk.queueFamilyIndex,
                                          pCreateInfo->width,
                                          pCreateInfo->height,
                                          pCreateInfo->imageCount,
                                          source_format,
                                          convert_route,
                                          output_word_size,
                                          pCreateInfo->surface->target.writeImage,
                                          pCreateInfo->surface->vk.pAllocator,
                                          command_pool,
                                          command_buffer,
                                          completion_fence,
                                          destination_buffer,
                                          destination_memory,
                                          y_stride,
                                          uv_offset,
                                          uv_stride,
                                          descriptor_set_layout,
                                          descriptor_pool,
                                          descriptor_set,
                                          shader_module,
                                          pipeline_layout,
                                          pipeline,
                                          images);

    for (image_index = 0; image_index < pCreateInfo->imageCount; ++image_index)
    {
        swapchain->images[image_index]                             = images[image_index];
        swapchain->states[image_index]                             = SLOT_STATE_AVAILABLE;
        swapchain->imageResources[image_index].commandPool         = command_pool[image_index];
        swapchain->imageResources[image_index].commandBuffer       = command_buffer[image_index];
        swapchain->imageResources[image_index].completionFence     = completion_fence[image_index];
        swapchain->imageResources[image_index].destinationBuffer   = destination_buffer[image_index];
        swapchain->imageResources[image_index].destinationMemory   = destination_memory[image_index];
        swapchain->imageResources[image_index].descriptorSetLayout = descriptor_set_layout[image_index];
        swapchain->imageResources[image_index].descriptorPool      = descriptor_pool[image_index];
        swapchain->imageResources[image_index].descriptorSet       = descriptor_set[image_index];
        swapchain->imageResources[image_index].shaderModule        = shader_module[image_index];
        swapchain->imageResources[image_index].pipelineLayout      = pipeline_layout[image_index];
        swapchain->imageResources[image_index].pipeline            = pipeline[image_index];
        swapchain->imageResources[image_index].yStride             = y_stride[image_index];
        swapchain->imageResources[image_index].uvStride            = uv_stride[image_index];
        swapchain->imageResources[image_index].uvOffset            = uv_offset[image_index];
    }
    swapchain->kind               = pCreateInfo->kind;
    swapchain->hint               = pCreateInfo->hint;
    swapchain->imageFormat        = pCreateInfo->imageFormat;
    swapchain->convertRoute       = convert_route;
    swapchain->outputWordSize     = output_word_size;
    swapchain->imageCount         = pCreateInfo->imageCount;
    swapchain->nextImageIndex     = INITIAL_IMAGE_INDEX;
    swapchain->acquiredImageIndex = VK_INVALID;
    swapchain->surface            = pCreateInfo->surface;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateConvertSwapchain(Gfx_Swapchain_t* swapchain, const Gfx_Swapchain_CreateInfo_t* pCreateInfo)
{
    switch (pCreateInfo->hint)
    {
    case GFX_SWAPCHAIN_HINT_VULKAN:
        CreateVulkanConvertSwapchain(swapchain, pCreateInfo);
        break;

    default:
        LOGFATAL("Unsupported convert-swapchain backend!");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateSwapchain(Gfx_Swapchain_t* swapchain, const Gfx_Swapchain_CreateInfo_t* pCreateInfo)
{
    ValidateCreateInfo(pCreateInfo);

    switch (pCreateInfo->kind)
    {
    case GFX_SWAPCHAIN_KIND_CONVERT:
        CreateConvertSwapchain(swapchain, pCreateInfo);
        break;

    default:
        LOGFATAL("Unsupported swapchain kind!");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Slots are selected in round-robin order. Reusing a submitted slot waits for its conversion
 *  fence. The shared target's previous reader must release ownership before acquisition.
 */
static void AcquireNextImage(Gfx_Swapchain_t* swapchain, uint64_t timeout, Gfx_Image_t** image)
{
    uint32_t search_offset;
    uint32_t image_index;

    LOGASSERT(VK_INVALID == swapchain->acquiredImageIndex);

    for (search_offset = 0; search_offset < swapchain->imageCount; ++search_offset)
    {
        image_index = (swapchain->nextImageIndex + search_offset) % swapchain->imageCount;

        if (SLOT_STATE_ACQUIRED != swapchain->states[image_index])
        {
            if (SLOT_STATE_SUBMITTED == swapchain->states[image_index])
            {
                CVK_CHECK(cvkWaitForFence(swapchain->surface->vk.device,
                                          swapchain->imageResources[image_index].completionFence,
                                          VK_TRUE,
                                          timeout));
            }
            swapchain->states[image_index]           = SLOT_STATE_ACQUIRED;
            swapchain->images[image_index]->acquired = TRUE;
            swapchain->acquiredImageIndex            = image_index;
            swapchain->nextImageIndex                = (image_index + 1) % swapchain->imageCount;

            LOGASSERT(FALSE == swapchain->surface->readPending);
            if (swapchain->surface->writePending)
            {
                Gfx_Surface_Acquire(swapchain->surface);
            }

            (*image) = swapchain->images[image_index];
            return;
        }
    }
    LOGFATAL("No swapchain image is available!");
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The completion fence protects the slot's command and conversion resources until reuse.
 *  External image ownership and the optional shared timeline synchronize access by the paired API.
 */
static void DispatchVulkanConvertImage(VkDevice         device,
                                       VkQueue          queue,
                                       uint32_t         queueFamilyIndex,
                                       uint32_t         width,
                                       uint32_t         height,
                                       ConvertRoute_t   convertRoute,
                                       uint32_t         outputWordSize,
                                       VkCommandPool    commandPool,
                                       VkCommandBuffer  commandBuffer,
                                       VkFence          completionFence,
                                       VkBuffer         destinationBuffer,
                                       uint32_t         yStride,
                                       uint32_t         uvOffset,
                                       uint32_t         uvStride,
                                       VkPipeline       pipeline,
                                       VkPipelineLayout pipelineLayout,
                                       VkDescriptorSet  descriptorSet,
                                       VkDevice         uploadDevice,
                                       VkQueue          uploadQueue,
                                       VkCommandBuffer  uploadCommandBuffer,
                                       VkFence          uploadFence,
                                       VkSemaphore      uploadSemaphore,
                                       bool_t           uploadSubmitted,
                                       VkImage          destinationImage,
                                       VkFormat         destinationFormat,
                                       uint32_t         destinationWidth,
                                       uint32_t         destinationHeight,
                                       bool_t           destinationLayoutInitialized,
                                       bool_t           destinationHasExternalBuffer,
                                       bool_t           destinationRequiresHostCopy,
                                       VkSemaphore      externalSemaphore,
                                       uint64_t*        externalSemaphoreValue)
{
    VkExtent2D               destination_image_extent;
    VkImageSubresourceLayers destination_image_subresource;
    VkOffset2D               destination_image_offset;
    VkSemaphore              wait_semaphores[2];
    VkPipelineStageFlags     wait_stage_masks[2];
    uint64_t                 wait_semaphore_values[2];
    VkSemaphore              signal_semaphores[1];
    uint64_t                 signal_semaphore_values[1];
    uint32_t                 wait_semaphore_count;
    uint32_t                 signal_semaphore_count;
    VkImageLayout            destination_image_initial_layout;

    if (FALSE == uploadSubmitted)
    {
        cvkEndCommandBuffer(uploadCommandBuffer);
        cvkResetFence(uploadDevice, uploadFence);
        cvkQueueSubmitCommandBuffer(uploadQueue,
                                    NULL,
                                    0,
                                    NULL,
                                    NULL,
                                    1,
                                    &uploadSemaphore,
                                    uploadFence,
                                    uploadCommandBuffer);
    }
    cvkResetCommandPool(device, commandPool, (VkCommandPoolResetFlags)0);
    cvkBeginCommandBuffer(commandBuffer,
                          NULL,
                          (VkCommandBufferUsageFlags)VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
                          NULL);

    if ((destinationHasExternalBuffer) && (FALSE == destinationRequiresHostCopy))
    {
        cvkCmdAcquireBufferFromExternal(commandBuffer,
                                        NULL,
                                        VK_ACCESS_SHADER_WRITE_BIT,
                                        queueFamilyIndex,
                                        destinationBuffer,
                                        VK_NO_OFFSET,
                                        VK_WHOLE_SIZE,
                                        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                        (VkDependencyFlags)0);
    }
    else if (destinationLayoutInitialized)
    {
        if (destinationRequiresHostCopy)
        {
            cvkCmdBufferBarrier(commandBuffer,
                                NULL,
                                VK_ACCESS_HOST_READ_BIT,
                                VK_ACCESS_SHADER_WRITE_BIT,
                                destinationBuffer,
                                VK_NO_OFFSET,
                                VK_WHOLE_SIZE,
                                VK_PIPELINE_STAGE_HOST_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                (VkDependencyFlags)0);
        }
        else
        {
            cvkCmdBufferBarrier(commandBuffer,
                                NULL,
                                VK_ACCESS_TRANSFER_READ_BIT,
                                VK_ACCESS_SHADER_WRITE_BIT,
                                destinationBuffer,
                                VK_NO_OFFSET,
                                VK_WHOLE_SIZE,
                                VK_PIPELINE_STAGE_TRANSFER_BIT,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                (VkDependencyFlags)0);
        }
    }
    switch (convertRoute)
    {
    case CONVERT_ROUTE_RGBA8_TO_NV12:
    case CONVERT_ROUTE_BGRA8_TO_NV12:
    {
        Nv12PushConstants_t push_constants;
        VkExtent2D          item_count;
        VkExtent2D          items_per_invocation;
        VkExtent2D          local_size;

        push_constants.width         = width;
        push_constants.height        = height;
        push_constants.yStrideWords  = yStride / outputWordSize;
        push_constants.uvOffsetWords = uvOffset / outputWordSize;
        push_constants.uvStrideWords = uvStride / outputWordSize;
        item_count.width             = width;
        item_count.height            = height;
        items_per_invocation.width   = NV12_CONVERT_PIXELS_PER_INVOCATION_X;
        items_per_invocation.height  = NV12_CONVERT_PIXELS_PER_INVOCATION_Y;
        local_size.width             = NV12_CONVERT_LOCAL_SIZE_X;
        local_size.height            = NV12_CONVERT_LOCAL_SIZE_Y;
        cvkCmdDispatchComputeItems2D(commandBuffer,
                                     pipeline,
                                     pipelineLayout,
                                     0,
                                     1,
                                     &descriptorSet,
                                     0,
                                     NULL,
                                     0,
                                     (uint32_t)sizeof(push_constants),
                                     &push_constants,
                                     item_count,
                                     items_per_invocation,
                                     local_size);
        break;
    }

    default:
        LOGFATAL("Unsupported Vulkan conversion route!");
    }
    if (FALSE == destinationHasExternalBuffer)
    {
        destination_image_offset.x       = 0;
        destination_image_offset.y       = 0;
        destination_image_extent.width   = destinationWidth;
        destination_image_extent.height  = destinationHeight;
        destination_image_subresource    = cvkGetFormatBaseSubresourceLayers(destinationFormat);
        destination_image_initial_layout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (destinationLayoutInitialized)
        {
            destination_image_initial_layout = VK_IMAGE_LAYOUT_GENERAL;
        }
        cvkCmdTransferComputeBufferToExternalImage2D(commandBuffer,
                                                     (VkDependencyFlags)0,
                                                     NULL,
                                                     destinationBuffer,
                                                     VK_NO_OFFSET,
                                                     outputWordSize,
                                                     destinationImage,
                                                     destinationFormat,
                                                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                                     destination_image_subresource,
                                                     NULL,
                                                     destination_image_initial_layout,
                                                     NULL,
                                                     VK_IMAGE_LAYOUT_GENERAL,
                                                     queueFamilyIndex,
                                                     destination_image_offset,
                                                     destination_image_extent);
    }
    else
    {
        if (destinationRequiresHostCopy)
        {
            cvkCmdBufferBarrier(commandBuffer,
                                NULL,
                                VK_ACCESS_SHADER_WRITE_BIT,
                                VK_ACCESS_HOST_READ_BIT,
                                destinationBuffer,
                                VK_NO_OFFSET,
                                VK_WHOLE_SIZE,
                                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                VK_PIPELINE_STAGE_HOST_BIT,
                                (VkDependencyFlags)0);
        }
        else
        {
            cvkCmdReleaseBufferToExternal(commandBuffer,
                                          NULL,
                                          VK_ACCESS_SHADER_WRITE_BIT,
                                          queueFamilyIndex,
                                          destinationBuffer,
                                          VK_NO_OFFSET,
                                          VK_WHOLE_SIZE,
                                          VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                          (VkDependencyFlags)0);
        }
    }
    cvkEndCommandBuffer(commandBuffer);
    cvkResetFence(device, completionFence);
    wait_semaphore_count       = 1;
    wait_semaphores[0]         = uploadSemaphore;
    wait_stage_masks[0]        = (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    wait_semaphore_values[0]   = 0;
    signal_semaphore_count     = 0;
    signal_semaphores[0]       = VK_NULL_HANDLE;
    signal_semaphore_values[0] = 0;
    if (VK_NULL_HANDLE != externalSemaphore)
    {
        if (destinationLayoutInitialized)
        {
            wait_semaphores[wait_semaphore_count]       = externalSemaphore;
            wait_stage_masks[wait_semaphore_count]      = (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            wait_semaphore_values[wait_semaphore_count] = (*externalSemaphoreValue);
            wait_semaphore_count++;
        }
        signal_semaphore_count = 1;
        signal_semaphores[0]   = externalSemaphore;

        (*externalSemaphoreValue)++;

        signal_semaphore_values[0] = (*externalSemaphoreValue);
    }
    Gfx_Swapchain_Platform_QueueSubmit(queue,
                                       NULL,
                                       wait_semaphore_count,
                                       wait_semaphores,
                                       wait_stage_masks,
                                       wait_semaphore_values,
                                       commandBuffer,
                                       signal_semaphore_count,
                                       signal_semaphores,
                                       signal_semaphore_values,
                                       completionFence);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SubmitVulkanConvertImage(Gfx_Swapchain_t* swapchain, Gfx_Image_t* image)
{
    uint32_t     image_index;
    Gfx_Image_t* target_write_image;
    bool_t       target_has_external_buffer;

    image_index = swapchain->acquiredImageIndex;

    LOGASSERT(image_index < swapchain->imageCount);
    LOGASSERT(image == swapchain->images[image_index]);
    LOGASSERT(image->uploadPending);

    target_write_image         = swapchain->surface->target.writeImage;
    target_has_external_buffer = VK_NULL_HANDLE != target_write_image->vk.externalBuffer;

    DispatchVulkanConvertImage(swapchain->surface->vk.device,
                               swapchain->surface->vk.queue,
                               swapchain->surface->vk.queueFamilyIndex,
                               swapchain->surface->width,
                               swapchain->surface->height,
                               swapchain->convertRoute,
                               swapchain->outputWordSize,
                               swapchain->imageResources[image_index].commandPool,
                               swapchain->imageResources[image_index].commandBuffer,
                               swapchain->imageResources[image_index].completionFence,
                               swapchain->imageResources[image_index].destinationBuffer,
                               swapchain->imageResources[image_index].yStride,
                               swapchain->imageResources[image_index].uvOffset,
                               swapchain->imageResources[image_index].uvStride,
                               swapchain->imageResources[image_index].pipeline,
                               swapchain->imageResources[image_index].pipelineLayout,
                               swapchain->imageResources[image_index].descriptorSet,
                               image->vk.device,
                               image->vk.queue,
                               image->vk.commandBuffer,
                               image->vk.uploadFence,
                               image->vk.uploadSemaphore,
                               image->vk.uploadSubmitted,
                               target_write_image->vk.image,
                               target_write_image->vk.format,
                               target_write_image->vk.extent.width,
                               target_write_image->vk.extent.height,
                               target_write_image->vk.layoutInitialized,
                               target_has_external_buffer,
                               target_write_image->vk.externalBufferHostCopy,
                               target_write_image->vk.externalSemaphore,
                               &target_write_image->vk.externalSemaphoreValue);

    target_write_image->vk.layoutInitialized = TRUE;
    target_write_image->uploadPending        = TRUE;
    image->vk.layoutInitialized              = TRUE;
    image->vk.uploadSubmitted                = FALSE;
    image->uploadPending                     = FALSE;
    swapchain->surface->vk.completionFence   = swapchain->imageResources[image_index].completionFence;
    swapchain->surface->writePending         = TRUE;
    Gfx_Surface_Release(swapchain->surface);
    image->acquired                = FALSE;
    swapchain->states[image_index] = SLOT_STATE_SUBMITTED;
    swapchain->acquiredImageIndex  = VK_INVALID;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SubmitConvertImage(Gfx_Swapchain_t* swapchain, Gfx_Image_t* image)
{
    switch (swapchain->hint)
    {
    case GFX_SWAPCHAIN_HINT_VULKAN:
        SubmitVulkanConvertImage(swapchain, image);
        break;

    default:
        LOGFATAL("Unsupported convert-swapchain backend!");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SubmitImage(Gfx_Swapchain_t* swapchain, Gfx_Image_t* image)
{
    switch (swapchain->kind)
    {
    case GFX_SWAPCHAIN_KIND_CONVERT:
        SubmitConvertImage(swapchain, image);
        break;

    default:
        LOGFATAL("Unsupported swapchain submission kind!");
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Swapchain_Create(Gfx_Swapchain_t** swapchain, const Gfx_Swapchain_CreateInfo_t* createInfo)
{
    LOGASSERT(NULL != swapchain);
    LOGASSERT(NULL != createInfo);

    (*swapchain) = (Gfx_Swapchain_t*)OS_CALLOC(sizeof(Gfx_Swapchain_t));
    CreateSwapchain((*swapchain), createInfo);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The acquired image must be submitted before acquiring another image from this swapchain.
 *  The timeout applies to the slot fence in nanoseconds; shared-surface waits use an infinite timeout.
 *  An encoder holding the conversion target releases read ownership when its Status() call completes.
 */
void Gfx_Swapchain_AcquireNextImage(Gfx_Swapchain_t* swapchain, uint64_t timeout, Gfx_Image_t** image)
{
    LOGASSERT(NULL != swapchain);
    LOGASSERT(NULL != image);

    AcquireNextImage(swapchain, timeout, image);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Swapchain_SubmitImage(Gfx_Swapchain_t* swapchain, Gfx_Image_t* image)
{
    LOGASSERT(NULL != swapchain);
    LOGASSERT(NULL != image);

    SubmitImage(swapchain, image);
}
