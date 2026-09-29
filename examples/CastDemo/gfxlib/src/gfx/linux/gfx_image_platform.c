/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Native image construction composes the pinned CVK and cva interfaces.
 * Resource ownership follows gfx_image.h; platform selection belongs to the build.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_image_internal.h"
#include "gfx/gfx_image_platform.h"
#include "cvk/cvk.h"
#include "cva.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "utl/utl_defines.h"

#include "drm/cva_drm.h"
#include <drm/drm_fourcc.h>
#include <unistd.h>
#include <va/va_drmcommon.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define IMPORT_MEMORY_TYPE_INDEX  (0)   ///< Native import memory type.
#define IMPORT_DESCRIPTOR_INDEX   (1)   ///< Native import resource descriptor.
#define IMPORT_PIXEL_FORMAT_INDEX (2)   ///< Native import pixel format.
#define IMPORT_USAGE_INDEX        (3)   ///< Native encoder usage.
#define IMPORT_ATTRIBUTE_COUNT    (4)   ///< Complete native import schema.
#define EXPORT_PIXEL_FORMAT_INDEX (0)   ///< Export pixel format.
#define EXPORT_USAGE_INDEX        (1)   ///< Encoder and export usage.
#define EXPORT_ATTRIBUTE_COUNT    (2)   ///< Complete DMA-BUF export schema.
#define LINEAR_ROW_ALIGNMENT      (128) ///< Row alignment for VA-importable DMA-BUF storage.

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
 * \brief Imports the configured native representation into the image's VA surface.
 *
 * \par Requirements:
 *  - 001 The image shall be initialized with invalid native resources before construction.
 *  - 002 Native resources shall be created only during application startup.
 *  - 003 The configuration and sharing handles shall satisfy the selected native representation.
 *  - 004 Native resources shall remain associated with their owning image and borrowed device.
 *
 * \param[in,out] image   Initialized owner receiving native resources
 * \param[in] createInfo   Validated image configuration and native sharing parameters
 *
 * \retval None   No result value.
 */
static void InitializeVaImport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Constructs the image's exportable VA surface and publishes its native sharing descriptor.
 *
 * \par Requirements:
 *  - 001 The image shall be initialized with invalid native resources before construction.
 *  - 002 Native resources shall be created only during application startup.
 *  - 003 The configuration and sharing handles shall satisfy the selected native representation.
 *  - 004 Native resources shall remain associated with their owning image and borrowed device.
 *
 * \param[in,out] image   Initialized owner receiving native resources
 * \param[in] createInfo   Validated image configuration and native sharing parameters
 *
 * \retval None   No result value.
 */
static void InitializeVaExport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Imports native image or buffer storage directly into the image owner.
 *
 * \par Requirements:
 *  - 001 The image shall be initialized with invalid native resources before construction.
 *  - 002 Native resources shall be created only during application startup.
 *  - 003 The configuration and sharing handles shall satisfy the selected native representation.
 *  - 004 Native resources shall remain associated with their owning image and borrowed device.
 *
 * \param[in,out] image   Initialized owner receiving native resources
 * \param[in] createInfo   Validated image configuration and native sharing parameters
 *
 * \retval None   No result value.
 */
static void InitializeVulkanImport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Constructs the image's native export representation on supported platforms.
 *
 * \par Requirements:
 *  - 001 The image shall be initialized with invalid native resources before construction.
 *  - 002 Native resources shall be created only during application startup.
 *  - 003 The configuration and sharing handles shall satisfy the selected native representation.
 *  - 004 Native resources shall remain associated with their owning image and borrowed device.
 *
 * \param[in,out] image   Initialized owner receiving native resources
 * \param[in] createInfo   Validated image configuration and native sharing parameters
 *
 * \retval None   No result value.
 */
static void InitializeVulkanExport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Unpacks the memory objects of a DRM PRIME 2 surface with a common modifier.
 *
 * \param[in]  descriptor        Surface descriptor containing the memory objects
 * \param[out] objectCount       Receives the number of memory objects
 * \param[out] objectSizes       Receives each object's allocation size in bytes
 * \param[out] objectFds         Receives each object's borrowed file descriptor
 * \param[out] drmFormatModifier Receives the modifier shared by every object
 *
 * \retval    None   No result value.
 */
static void GetSurfaceMemoryObjectsDrm(const VADRMPRIMESurfaceDescriptor* descriptor,
                                       uint32_t*                          objectCount,
                                       uint64_t*                          objectSizes,
                                       int32_t*                           objectFds,
                                       uint64_t*                          drmFormatModifier);

/*************************************************************************************************/
/**
 * \brief Queries memory-type compatibility for an imported POSIX file descriptor.
 *
 * \param[in]     device          Vulkan 1.2 logical device
 * \param[in,out] pNext           Extension chain for VkMemoryFdPropertiesKHR, or NULL
 * \param[in]     handleType      External-memory handle type matching fd
 * \param[in]     fd              Importable file descriptor
 * \param[out]    pMemoryTypeBits Receives compatible physical-device memory-type bits
 *
 * \retval    None   No result value.
 */
static void GetMemoryPropertiesInfoFd(VkDevice                           device,
                                      void*                              pNext,
                                      VkExternalMemoryHandleTypeFlagBits handleType,
                                      int                                fd,
                                      uint32_t*                          pMemoryTypeBits);

/*************************************************************************************************/
/**
 * \brief Imports a file descriptor as dedicated Vulkan image memory.
 *
 * \param[in]  device          Logical device importing the descriptor
 * \param[in]  pNext           Optional tail chained after dedicated-allocation metadata
 * \param[in]  image           Image receiving the dedicated allocation
 * \param[in]  allocationSize  Imported memory-object size in bytes
 * \param[in]  memoryTypeIndex Compatible physical-device memory-type index selected by the caller
 * \param[in]  handleType      External-memory FD handle type
 * \param[in]  fd              Descriptor transferred to Vulkan on successful allocation; not duplicated here
 * \param[in]  pAllocator      Optional host allocation callbacks
 * \param[out] pMemory         Imported dedicated memory handle
 *
 * \retval    None   No result value.
 */
static void AllocateImportedDedicatedImageMemoryFd(VkDevice                           device,
                                                   const void*                        pNext,
                                                   VkImage                            image,
                                                   VkDeviceSize                       allocationSize,
                                                   uint32_t                           memoryTypeIndex,
                                                   VkExternalMemoryHandleTypeFlagBits handleType,
                                                   int                                fd,
                                                   const VkAllocationCallbacks*       pAllocator,
                                                   VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Allocates imported dedicated memory and binds it to an existing image.
 *
 * \param[in]  device          Logical device owning the resources
 * \param[in]  pNext           Optional extension chain, or NULL
 * \param[in]  image           Image receiving the dedicated allocation
 * \param[in]  allocationSize  Allocation size supplied by the caller
 * \param[in]  memoryTypeIndex Compatible memory-type index selected by the caller
 * \param[in]  handleType      External-memory handle type
 * \param[in]  fd              Descriptor transferred to Vulkan on success; not duplicated here
 * \param[in]  pAllocator      Optional Vulkan allocation callbacks, or NULL
 * \param[out] pMemory         Receives allocated memory
 *
 * \retval    None   No result value.
 */
static void SetupImportedDedicatedImageMemoryFd(VkDevice                           device,
                                                const void*                        pNext,
                                                VkImage                            image,
                                                VkDeviceSize                       allocationSize,
                                                uint32_t                           memoryTypeIndex,
                                                VkExternalMemoryHandleTypeFlagBits handleType,
                                                int                                fd,
                                                const VkAllocationCallbacks*       pAllocator,
                                                VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Creates an unbound explicit-DRM-modifier image with caller-supplied plane layouts.
 *
 * \param[in]  device                Vulkan 1.2 logical device
 * \param[in]  pNext                 Additional image creation extension chain, or NULL
 * \param[in]  handleTypes           Exportable/importable external-memory handle mask
 * \param[in]  flags                 Vulkan image creation flags
 * \param[in]  imageType             One-, two-, or three-dimensional image type
 * \param[in]  format                Vulkan pixel format
 * \param[in]  extent                Nonzero image extent
 * \param[in]  mipLevels             Nonzero mip-level count
 * \param[in]  arrayLayers           Nonzero array-layer count
 * \param[in]  samples               Image sample count
 * \param[in]  usage                 Image usage mask
 * \param[in]  sharingMode           Exclusive or concurrent queue-family sharing
 * \param[in]  queueFamilyIndexCount Number of queue family indices
 * \param[in]  pQueueFamilyIndices   Queue family indices for concurrent sharing
 * \param[in]  initialLayout         Initial image layout permitted by Vulkan
 * \param[in]  drmFormatModifier     Explicit DRM modifier
 * \param[in]  planeCount            Number of modifier plane layouts
 * \param[in]  pPlaneLayouts         Array of planeCount explicit layouts
 * \param[in]  pAllocator            Vulkan allocator, or NULL
 * \param[out] pImage                Receives the created image
 *
 * \retval    None   No result value.
 */
static void CreateExternalDrmFormatModifierImage(VkDevice                        device,
                                                 const void*                     pNext,
                                                 VkExternalMemoryHandleTypeFlags handleTypes,
                                                 VkImageCreateFlags              flags,
                                                 VkImageType                     imageType,
                                                 VkFormat                        format,
                                                 VkExtent3D                      extent,
                                                 uint32_t                        mipLevels,
                                                 uint32_t                        arrayLayers,
                                                 VkSampleCountFlagBits           samples,
                                                 VkImageUsageFlags               usage,
                                                 VkSharingMode                   sharingMode,
                                                 uint32_t                        queueFamilyIndexCount,
                                                 const uint32_t*                 pQueueFamilyIndices,
                                                 VkImageLayout                   initialLayout,
                                                 uint64_t                        drmFormatModifier,
                                                 uint32_t                        planeCount,
                                                 const VkSubresourceLayout*      pPlaneLayouts,
                                                 const VkAllocationCallbacks*    pAllocator,
                                                 VkImage*                        pImage);

/*************************************************************************************************/
/**
 * \brief Creates an explicit-modifier image and imports/binds all external memory objects.
 *
 * \param[in]  physicalDevice            Physical device used for memory-type selection
 * \param[in]  device                    Logical device owning the resources
 * \param[in]  pImageNext                Additional image creation extension chain, or NULL
 * \param[in]  handleType                External-memory file-descriptor type
 * \param[in]  format                    Vulkan pixel format matching the native descriptor
 * \param[in]  extent                    Nonzero image extent
 * \param[in]  usage                     Required image usage flags
 * \param[in]  sharingMode               Exclusive or concurrent queue-family sharing
 * \param[in]  queueFamilyIndexCount     Number of queue family indices
 * \param[in]  pQueueFamilyIndices       Queue family indices for concurrent sharing
 * \param[in]  initialLayout             Permitted initial image layout
 * \param[in]  drmFormatModifier         Explicit DRM modifier
 * \param[in]  modifierPlaneCount        Number of explicit plane layouts
 * \param[in]  pPlaneLayouts             Array of modifierPlaneCount layouts
 * \param[in]  pPlaneMemoryObjectIndices Memory-object index for each modifier plane
 * \param[in]  memoryObjectCount         Number of object sizes and file descriptors
 * \param[in]  pMemoryObjectSizes        External allocation size per object
 * \param[in]  pMemoryObjectFds          Importable descriptor per object
 * \param[in]  pMemoryNext               Additional memory allocation extension chain, or NULL
 * \param[in]  properties                Required Vulkan memory properties
 * \param[in]  pAllocator                Vulkan allocator, or NULL
 * \param[out] pImage                    Receives the created and bound image
 * \param[in]  memoryCapacity            Element capacity of pMemories
 * \param[out] pMemories                 Receives imported memory allocations
 * \param[out] pMemoryCount              Receives the initialized allocation count
 *
 * \retval    None   No result value.
 */
static void SetupImportedDrmFormatModifierImage(VkPhysicalDevice                   physicalDevice,
                                                VkDevice                           device,
                                                const void*                        pImageNext,
                                                VkExternalMemoryHandleTypeFlagBits handleType,
                                                VkFormat                           format,
                                                VkExtent3D                         extent,
                                                VkImageUsageFlags                  usage,
                                                VkSharingMode                      sharingMode,
                                                uint32_t                           queueFamilyIndexCount,
                                                const uint32_t*                    pQueueFamilyIndices,
                                                VkImageLayout                      initialLayout,
                                                uint64_t                           drmFormatModifier,
                                                uint32_t                           modifierPlaneCount,
                                                const VkSubresourceLayout*         pPlaneLayouts,
                                                const uint32_t*                    pPlaneMemoryObjectIndices,
                                                uint32_t                           memoryObjectCount,
                                                const VkDeviceSize*                pMemoryObjectSizes,
                                                const int*                         pMemoryObjectFds,
                                                const void*                        pMemoryNext,
                                                VkMemoryPropertyFlags              properties,
                                                const VkAllocationCallbacks*       pAllocator,
                                                VkImage*                           pImage,
                                                uint32_t                           memoryCapacity,
                                                VkDeviceMemory*                    pMemories,
                                                uint32_t*                          pMemoryCount);

/*************************************************************************************************/
/**
 * \brief Allocates imported image memory and binds it.
 *
 * \param[in]  device          Logical device
 * \param[in]  pNext           Optional allocation extension chain
 * \param[in]  image           Resource receiving imported storage
 * \param[in]  allocationSize  Imported allocation size
 * \param[in]  memoryTypeIndex Caller-selected compatible memory-type index
 * \param[in]  handleType      External-memory handle type
 * \param[in]  fd              Descriptor consumed by Vulkan on successful import
 * \param[in]  memoryOffset    Aligned binding offset in the imported allocation
 * \param[in]  pAllocator      Optional allocation callbacks
 * \param[out] pMemory         Receives imported and bound memory
 *
 * \retval    None   No result value.
 */
static void SetupImportedImageMemoryFd(VkDevice                           device,
                                       const void*                        pNext,
                                       VkImage                            image,
                                       VkDeviceSize                       allocationSize,
                                       uint32_t                           memoryTypeIndex,
                                       VkExternalMemoryHandleTypeFlagBits handleType,
                                       int                                fd,
                                       VkDeviceSize                       memoryOffset,
                                       const VkAllocationCallbacks*       pAllocator,
                                       VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Creates and binds non-dedicated exportable buffer storage, then exports its file descriptor.
 *
 * \param[in]  physicalDevice        Physical device supplying memory properties
 * \param[in]  device                Logical device
 * \param[in]  pBufferNext           Buffer creation extension chain
 * \param[in]  handleType            Native external-memory type
 * \param[in]  flags                 Buffer creation flags
 * \param[in]  size                  Buffer size
 * \param[in]  usage                 Buffer usage
 * \param[in]  sharingMode           Queue-family sharing mode
 * \param[in]  queueFamilyIndexCount Queue-family index count
 * \param[in]  pQueueFamilyIndices   Queue-family indices
 * \param[in]  pMemoryNext           Allocation extension chain
 * \param[in]  pGetNext              Native memory export extension chain
 * \param[in]  properties            Required memory properties
 * \param[in]  pAllocator            Optional allocation callbacks
 * \param[out] pBuffer               Receives the created and bound buffer
 * \param[out] pMemory               Receives exportable memory
 * \param[out] pFd                   Receives the native exported handle
 *
 * \retval    None   No result value.
 */
static void SetupExportedMemoryBufferFd(VkPhysicalDevice                   physicalDevice,
                                        VkDevice                           device,
                                        const void*                        pBufferNext,
                                        VkExternalMemoryHandleTypeFlagBits handleType,
                                        VkBufferCreateFlags                flags,
                                        VkDeviceSize                       size,
                                        VkBufferUsageFlags                 usage,
                                        VkSharingMode                      sharingMode,
                                        uint32_t                           queueFamilyIndexCount,
                                        const uint32_t*                    pQueueFamilyIndices,
                                        const void*                        pMemoryNext,
                                        const void*                        pGetNext,
                                        VkMemoryPropertyFlags              properties,
                                        const VkAllocationCallbacks*       pAllocator,
                                        VkBuffer*                          pBuffer,
                                        VkDeviceMemory*                    pMemory,
                                        int*                               pFd);

/*************************************************************************************************/
/**
 * \brief Creates and binds dedicated exportable buffer storage, then exports its file descriptor.
 *
 * \param[in]  physicalDevice        Physical device supplying memory properties
 * \param[in]  device                Logical device
 * \param[in]  pBufferNext           Buffer creation extension chain
 * \param[in]  handleType            Native external-memory type
 * \param[in]  flags                 Buffer creation flags
 * \param[in]  size                  Buffer size
 * \param[in]  usage                 Buffer usage
 * \param[in]  sharingMode           Queue-family sharing mode
 * \param[in]  queueFamilyIndexCount Queue-family index count
 * \param[in]  pQueueFamilyIndices   Queue-family indices
 * \param[in]  pMemoryNext           Allocation extension chain
 * \param[in]  pGetNext              Native memory export extension chain
 * \param[in]  properties            Required memory properties
 * \param[in]  pAllocator            Optional allocation callbacks
 * \param[out] pBuffer               Receives the created and bound buffer
 * \param[out] pMemory               Receives exportable dedicated memory
 * \param[out] pFd                   Receives the native exported handle
 *
 * \retval    None   No result value.
 */
static void SetupExportedDedicatedMemoryBufferFd(VkPhysicalDevice                   physicalDevice,
                                                 VkDevice                           device,
                                                 const void*                        pBufferNext,
                                                 VkExternalMemoryHandleTypeFlagBits handleType,
                                                 VkBufferCreateFlags                flags,
                                                 VkDeviceSize                       size,
                                                 VkBufferUsageFlags                 usage,
                                                 VkSharingMode                      sharingMode,
                                                 uint32_t                           queueFamilyIndexCount,
                                                 const uint32_t*                    pQueueFamilyIndices,
                                                 const void*                        pMemoryNext,
                                                 const void*                        pGetNext,
                                                 VkMemoryPropertyFlags              properties,
                                                 const VkAllocationCallbacks*       pAllocator,
                                                 VkBuffer*                          pBuffer,
                                                 VkDeviceMemory*                    pMemory,
                                                 int*                               pFd);

/*************************************************************************************************/
/**
 * \brief Queries DRM modifier planes, constructs their Vulkan layouts, and sets up an imported DMA-BUF image.
 *
 * \param[in]  physicalDevice            Physical device used for memory-type selection
 * \param[in]  device                    Logical device owning the resources
 * \param[in]  pImageNext                Additional image creation extension chain, or NULL
 * \param[in]  format                    Vulkan multi-planar pixel format
 * \param[in]  extent                    Nonzero image extent
 * \param[in]  usage                     Required image usage flags
 * \param[in]  sharingMode               Exclusive or concurrent queue-family sharing
 * \param[in]  queueFamilyIndexCount     Number of queue family indices
 * \param[in]  pQueueFamilyIndices       Queue family indices for concurrent sharing
 * \param[in]  initialLayout             Permitted initial image layout
 * \param[in]  drmFormatModifier         Explicit DRM modifier
 * \param[in]  planeCount                Number of supplied plane offsets, pitches, and object indices
 * \param[in]  pPlaneOffsets             Byte offset for each supplied plane
 * \param[in]  pPlaneRowPitches          Row pitch in bytes for each supplied plane
 * \param[in]  pPlaneMemoryObjectIndices Memory-object index for each modifier plane
 * \param[in]  memoryObjectCount         Number of object sizes and file descriptors
 * \param[in]  pMemoryObjectSizes        External allocation size per object
 * \param[in]  pMemoryObjectFds          Importable descriptor per object
 * \param[in]  pMemoryNext               Additional memory allocation extension chain, or NULL
 * \param[in]  properties                Required Vulkan memory properties
 * \param[in]  pAllocator                Vulkan allocator, or NULL
 * \param[out] pImage                    Receives the created and bound image
 * \param[in]  memoryCapacity            Element capacity of pMemories
 * \param[out] pMemories                 Receives imported memory allocations
 * \param[out] pMemoryCount              Receives the initialized allocation count
 *
 * \retval    None   No result value.
 */
static void SetupImportedDmaBufImage(VkPhysicalDevice             physicalDevice,
                                     VkDevice                     device,
                                     const void*                  pImageNext,
                                     VkFormat                     format,
                                     VkExtent3D                   extent,
                                     VkImageUsageFlags            usage,
                                     VkSharingMode                sharingMode,
                                     uint32_t                     queueFamilyIndexCount,
                                     const uint32_t*              pQueueFamilyIndices,
                                     VkImageLayout                initialLayout,
                                     uint64_t                     drmFormatModifier,
                                     uint32_t                     planeCount,
                                     const uint32_t*              pPlaneOffsets,
                                     const uint32_t*              pPlaneRowPitches,
                                     const uint32_t*              pPlaneMemoryObjectIndices,
                                     uint32_t                     memoryObjectCount,
                                     const VkDeviceSize*          pMemoryObjectSizes,
                                     const int*                   pMemoryObjectFds,
                                     const void*                  pMemoryNext,
                                     VkMemoryPropertyFlags        properties,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkImage*                     pImage,
                                     uint32_t                     memoryCapacity,
                                     VkDeviceMemory*              pMemories,
                                     uint32_t*                    pMemoryCount);

/*************************************************************************************************/
/**
 * \brief Converts Vulkan format to VA and DRM codes without changing channel or plane order.
 *
 * \param[in]  format            Requested Vulkan format
 * \param[in]  preferredVaFourcc Preferred VA representation; zero selects the canonical format
 * \param[out] pVaFourcc         Supported VA representation, falling back to the canonical format
 * \param[out] pDrmFormat        DRM format of its composed layer
 *
 * \retval None   No result value.
 */
static void GetImageFormats(VkFormat format, uint32_t preferredVaFourcc, uint32_t* pVaFourcc, uint32_t* pDrmFormat);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InitializeVaImport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    VADRMPRIMESurfaceDescriptor* descriptor;
    VASurfaceAttrib              attributes[IMPORT_ATTRIBUTE_COUNT];

    descriptor = createInfo->platformCreateInfo->drmPrimeDescriptor;
    LOGASSERT(NULL != descriptor);
    LOGASSERT(INT32_MAX >= createInfo->va.pixelFormat);
    LOGASSERT(createInfo->width == descriptor->width);
    LOGASSERT(createInfo->height == descriptor->height);
    LOGASSERT(createInfo->va.pixelFormat == descriptor->fourcc);

    cvaInitSurfaceAttribInteger(&attributes[IMPORT_MEMORY_TYPE_INDEX],
                                VASurfaceAttribMemoryType,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2);
    cvaInitSurfaceAttribPointer(&attributes[IMPORT_DESCRIPTOR_INDEX],
                                VASurfaceAttribExternalBufferDescriptor,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                descriptor);
    cvaInitSurfaceAttribInteger(&attributes[IMPORT_PIXEL_FORMAT_INDEX],
                                VASurfaceAttribPixelFormat,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                (int32_t)createInfo->va.pixelFormat);
    cvaInitSurfaceAttribInteger(&attributes[IMPORT_USAGE_INDEX],
                                VASurfaceAttribUsageHint,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER);
    cvaCreateSurface(createInfo->va.dpy,
                     createInfo->va.format,
                     createInfo->width,
                     createInfo->height,
                     &image->va.surfaceId,
                     attributes,
                     IMPORT_ATTRIBUTE_COUNT);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InitializeVaExport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    VADRMPRIMESurfaceDescriptor* descriptor;
    VASurfaceAttrib              attributes[EXPORT_ATTRIBUTE_COUNT];

    descriptor = createInfo->platformCreateInfo->drmPrimeDescriptor;
    LOGASSERT(NULL != descriptor);
    LOGASSERT(INT32_MAX >= createInfo->va.pixelFormat);

    cvaInitSurfaceAttribInteger(&attributes[EXPORT_PIXEL_FORMAT_INDEX],
                                VASurfaceAttribPixelFormat,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                (int32_t)createInfo->va.pixelFormat);
    cvaInitSurfaceAttribInteger(&attributes[EXPORT_USAGE_INDEX],
                                VASurfaceAttribUsageHint,
                                VA_SURFACE_ATTRIB_SETTABLE,
                                VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER | VA_SURFACE_ATTRIB_USAGE_HINT_EXPORT);
    cvaCreateSurface(createInfo->va.dpy,
                     createInfo->va.format,
                     createInfo->width,
                     createInfo->height,
                     &image->va.surfaceId,
                     attributes,
                     EXPORT_ATTRIBUTE_COUNT);
    CRT_Memory_Set(descriptor, 0, sizeof(*descriptor));
    cvaExportSurfaceDRMPrime2(createInfo->va.dpy,
                              image->va.surfaceId,
                              VA_EXPORT_SURFACE_WRITE_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS,
                              descriptor);
    LOGASSERT(createInfo->va.pixelFormat == descriptor->fourcc);
    LOGASSERT(createInfo->width == descriptor->width);
    LOGASSERT(createInfo->height == descriptor->height);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InitializeVulkanImport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    VkDeviceMemory                     memories[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                           memory_index;
    const VADRMPRIMESurfaceDescriptor* descriptor;
    VkDeviceSize                       memory_object_sizes[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    int                                memory_object_fds[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                           memory_object_count;
    uint64_t                           drm_format_modifier;
    uint32_t                           plane_index;
    uint32_t                           format_plane_count;
    uint32_t                           va_fourcc;
    uint32_t                           drm_format;

    CRT_Memory_Set(memories, 0, sizeof(memories));
    LOGASSERT(VK_NULL_HANDLE != createInfo->vk.physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != createInfo->vk.device);
    LOGASSERT(0 != image->vk.extent.width && 0 != image->vk.extent.height && 1 == image->vk.extent.depth);
    LOGASSERT((VkImageUsageFlags)0 != createInfo->vk.usage);

    descriptor = (const VADRMPRIMESurfaceDescriptor*)createInfo->platformCreateInfo->drmPrimeDescriptor;
    LOGASSERT(NULL != descriptor);
    GetImageFormats(createInfo->vk.format, descriptor->fourcc, &va_fourcc, &drm_format);
    LOGASSERT(va_fourcc == descriptor->fourcc);
    LOGASSERT(1 == descriptor->num_layers);
    LOGASSERT(drm_format == descriptor->layers[0].drm_format);
    LOGASSERT((0 != descriptor->layers[0].num_planes) &&
              (COUNT_OF(descriptor->layers[0].object_index) >= descriptor->layers[0].num_planes));
    LOGASSERT((image->vk.extent.width == descriptor->width) && (image->vk.extent.height == descriptor->height));

    GetSurfaceMemoryObjectsDrm(descriptor,
                               &memory_object_count,
                               memory_object_sizes,
                               memory_object_fds,
                               &drm_format_modifier);

    format_plane_count = cvkGetFormatPlaneCount(createInfo->vk.format);
    LOGASSERT(format_plane_count <= descriptor->layers[0].num_planes);
    if (DRM_FORMAT_MOD_LINEAR == drm_format_modifier)
    {
        LOGASSERT(format_plane_count == descriptor->layers[0].num_planes);
    }
    for (plane_index = 0; plane_index < descriptor->layers[0].num_planes; ++plane_index)
    {
        uint32_t object_index;

        object_index = descriptor->layers[0].object_index[plane_index];
        LOGASSERT(memory_object_count > object_index);
        LOGASSERT(memory_object_sizes[object_index] > descriptor->layers[0].offset[plane_index]);
        LOGASSERT(0 != descriptor->layers[0].pitch[plane_index]);
    }

    SetupImportedDmaBufImage(createInfo->vk.physicalDevice,
                             createInfo->vk.device,
                             NULL,
                             createInfo->vk.format,
                             image->vk.extent,
                             createInfo->vk.usage,
                             VK_SHARING_MODE_EXCLUSIVE,
                             0,
                             NULL,
                             VK_IMAGE_LAYOUT_UNDEFINED,
                             drm_format_modifier,
                             descriptor->layers[0].num_planes,
                             descriptor->layers[0].offset,
                             descriptor->layers[0].pitch,
                             descriptor->layers[0].object_index,
                             memory_object_count,
                             memory_object_sizes,
                             memory_object_fds,
                             NULL,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             createInfo->vk.pAllocator,
                             &image->vk.image,
                             CVK_MAX_DRM_FORMAT_MODIFIER_PLANES,
                             memories,
                             &image->vk.memoryCount);
    LOGASSERT(CVK_MAX_DRM_FORMAT_MODIFIER_PLANES >= image->vk.memoryCount);
    image->vk.memory = memories[0];
    for (memory_index = 1; memory_index < CVK_MAX_DRM_FORMAT_MODIFIER_PLANES; ++memory_index)
    {
        image->vk.additionalMemories[memory_index - 1] = memories[memory_index];
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InitializeVulkanExport(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    VkExternalMemoryHandleTypeFlagBits handle_type;
    VkMemoryRequirements               memory_requirements;
    VADRMPRIMESurfaceDescriptor*       descriptor;
    VkDeviceSize                       plane_offsets[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                           plane_strides[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkExtent3D                         plane_extents[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkDeviceSize                       buffer_size;
    VkBool32                           dedicated_only;
    bool_t                             export_supported;
    uint32_t                           plane_count;
    uint32_t                           plane_index;

    handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    LOGASSERT(VK_NULL_HANDLE != createInfo->vk.physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != createInfo->vk.device);
    LOGASSERT(0 != image->vk.extent.width && 0 != image->vk.extent.height && 1 == image->vk.extent.depth);
    LOGASSERT((VkImageUsageFlags)0 != createInfo->vk.usage);
    LOGASSERT(NULL != createInfo->platformCreateInfo->drmPrimeDescriptor);

    descriptor = (VADRMPRIMESurfaceDescriptor*)createInfo->platformCreateInfo->drmPrimeDescriptor;
    CRT_Memory_Set(descriptor, 0, sizeof(*descriptor));
    GetImageFormats(createInfo->vk.format, 0, &descriptor->fourcc, &descriptor->layers[0].drm_format);
    plane_count = cvkCalculateFormatPlaneLayouts(createInfo->vk.format,
                                                 image->vk.extent.width,
                                                 image->vk.extent.height,
                                                 image->vk.extent.depth,
                                                 1,
                                                 LINEAR_ROW_ALIGNMENT,
                                                 plane_offsets,
                                                 plane_strides,
                                                 plane_extents,
                                                 &buffer_size);
    LOGASSERT(COUNT_OF(descriptor->layers[0].offset) >= plane_count);
    LOGASSERT(UINT32_MAX >= buffer_size);
    export_supported = (VK_TRUE == cvkGetPhysicalDeviceExternalBufferExportSupport(createInfo->vk.physicalDevice,
                                                                                   NULL,
                                                                                   (VkBufferCreateFlags)0,
                                                                                   VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                                                   handle_type,
                                                                                   &dedicated_only));
    LOGASSERT(export_supported);
    if (dedicated_only)
    {
        SetupExportedDedicatedMemoryBufferFd(createInfo->vk.physicalDevice,
                                             createInfo->vk.device,
                                             NULL,
                                             handle_type,
                                             (VkBufferCreateFlags)0,
                                             buffer_size,
                                             VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                             VK_SHARING_MODE_EXCLUSIVE,
                                             0,
                                             NULL,
                                             NULL,
                                             NULL,
                                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                             createInfo->vk.pAllocator,
                                             &image->vk.externalBuffer,
                                             &image->vk.externalBufferMemory,
                                             &descriptor->objects[0].fd);
    }
    else
    {
        SetupExportedMemoryBufferFd(createInfo->vk.physicalDevice,
                                    createInfo->vk.device,
                                    NULL,
                                    handle_type,
                                    (VkBufferCreateFlags)0,
                                    buffer_size,
                                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                    VK_SHARING_MODE_EXCLUSIVE,
                                    0,
                                    NULL,
                                    NULL,
                                    NULL,
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                    createInfo->vk.pAllocator,
                                    &image->vk.externalBuffer,
                                    &image->vk.externalBufferMemory,
                                    &descriptor->objects[0].fd);
    }

    cvkGetBufferMemoryRequirements(createInfo->vk.device, image->vk.externalBuffer, &memory_requirements);
    LOGASSERT(UINT32_MAX >= memory_requirements.size);
    descriptor->width                          = image->vk.extent.width;
    descriptor->height                         = image->vk.extent.height;
    descriptor->num_objects                    = 1;
    descriptor->objects[0].size                = (uint32_t)memory_requirements.size;
    descriptor->objects[0].drm_format_modifier = DRM_FORMAT_MOD_LINEAR;
    descriptor->num_layers                     = 1;
    descriptor->layers[0].num_planes           = plane_count;
    for (plane_index = 0; plane_index < plane_count; ++plane_index)
    {
        descriptor->layers[0].object_index[plane_index] = 0;
        descriptor->layers[0].offset[plane_index]       = (uint32_t)plane_offsets[plane_index];
        descriptor->layers[0].pitch[plane_index]        = plane_strides[plane_index];
    }
    image->vk.externalBufferSize = buffer_size;
    image->vk.externalYStride    = plane_strides[0];
    if (1 < plane_count)
    {
        image->vk.externalUvOffset = (uint32_t)plane_offsets[1];
        image->vk.externalUvStride = plane_strides[1];
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void GetSurfaceMemoryObjectsDrm(const VADRMPRIMESurfaceDescriptor* descriptor,
                                       uint32_t*                          numObjects,
                                       uint64_t*                          objectSizes,
                                       int32_t*                           objectFds,
                                       uint64_t*                          drmFormatModifier)
{
    uint32_t object_index;

    LOGASSERT(NULL != descriptor);
    LOGASSERT((0 != descriptor->num_objects) && (COUNT_OF(descriptor->objects) >= descriptor->num_objects));
    LOGASSERT(NULL != numObjects);
    LOGASSERT(NULL != objectSizes);
    LOGASSERT(NULL != objectFds);
    LOGASSERT(NULL != drmFormatModifier);

    (*drmFormatModifier) = descriptor->objects[0].drm_format_modifier;
    for (object_index = 0; object_index < descriptor->num_objects; ++object_index)
    {
        LOGASSERT((*drmFormatModifier) == descriptor->objects[object_index].drm_format_modifier);

        objectSizes[object_index] = descriptor->objects[object_index].size;
        objectFds[object_index]   = descriptor->objects[object_index].fd;
    }
    (*numObjects) = descriptor->num_objects;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void GetMemoryPropertiesInfoFd(VkDevice                           device,
                                      void*                              pNext,
                                      VkExternalMemoryHandleTypeFlagBits handleType,
                                      int                                fd,
                                      uint32_t*                          pMemoryTypeBits)
{
    VkMemoryFdPropertiesKHR properties;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 <= fd);
    LOGASSERT(NULL != pMemoryTypeBits);

    cvkGetMemoryFdProperties(device, handleType, fd, pNext, &properties);

    (*pMemoryTypeBits) = properties.memoryTypeBits;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void AllocateImportedDedicatedImageMemoryFd(VkDevice                           device,
                                                   const void*                        pNext,
                                                   VkImage                            image,
                                                   VkDeviceSize                       allocationSize,
                                                   uint32_t                           memoryTypeIndex,
                                                   VkExternalMemoryHandleTypeFlagBits handleType,
                                                   int                                fd,
                                                   const VkAllocationCallbacks*       pAllocator,
                                                   VkDeviceMemory*                    pMemory)
{
    VkImportMemoryFdInfoKHR import_info;

    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT(0 <= fd);

    import_info.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    import_info.pNext      = pNext;
    import_info.handleType = handleType;
    import_info.fd         = fd;
    cvkAllocateMemoryWithDedicatedInfo(device,
                                       &import_info,
                                       image,
                                       VK_NULL_HANDLE,
                                       allocationSize,
                                       memoryTypeIndex,
                                       pAllocator,
                                       pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupImportedDedicatedImageMemoryFd(VkDevice                           device,
                                                const void*                        pNext,
                                                VkImage                            image,
                                                VkDeviceSize                       allocationSize,
                                                uint32_t                           memoryTypeIndex,
                                                VkExternalMemoryHandleTypeFlagBits handleType,
                                                int                                fd,
                                                const VkAllocationCallbacks*       pAllocator,
                                                VkDeviceMemory*                    pMemory)
{
    AllocateImportedDedicatedImageMemoryFd(device,
                                           pNext,
                                           image,
                                           allocationSize,
                                           memoryTypeIndex,
                                           handleType,
                                           fd,
                                           pAllocator,
                                           pMemory);
    cvkBindImageMemory(device, image, (*pMemory), VK_NO_OFFSET);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateExternalDrmFormatModifierImage(VkDevice                        device,
                                                 const void*                     pNext,
                                                 VkExternalMemoryHandleTypeFlags handleTypes,
                                                 VkImageCreateFlags              flags,
                                                 VkImageType                     imageType,
                                                 VkFormat                        format,
                                                 VkExtent3D                      extent,
                                                 uint32_t                        mipLevels,
                                                 uint32_t                        arrayLayers,
                                                 VkSampleCountFlagBits           samples,
                                                 VkImageUsageFlags               usage,
                                                 VkSharingMode                   sharingMode,
                                                 uint32_t                        queueFamilyIndexCount,
                                                 const uint32_t*                 pQueueFamilyIndices,
                                                 VkImageLayout                   initialLayout,
                                                 uint64_t                        drmFormatModifier,
                                                 uint32_t                        planeCount,
                                                 const VkSubresourceLayout*      pPlaneLayouts,
                                                 const VkAllocationCallbacks*    pAllocator,
                                                 VkImage*                        pImage)
{
    uint32_t                                      plane_index;
    VkImageDrmFormatModifierExplicitCreateInfoEXT modifier_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(0 != extent.width);
    LOGASSERT(0 != extent.height);
    LOGASSERT(0 != extent.depth);
    LOGASSERT(0 != mipLevels);
    LOGASSERT(0 != arrayLayers);
    LOGASSERT((VkImageUsageFlags)0 != usage);
    LOGASSERT(0 != planeCount);
    LOGASSERT(CVK_MAX_DRM_FORMAT_MODIFIER_PLANES >= planeCount);
    LOGASSERT(NULL != pPlaneLayouts);
    LOGASSERT(NULL != pImage);

    for (plane_index = 0; plane_index < planeCount; ++plane_index)
    {
        LOGASSERT(0 != pPlaneLayouts[plane_index].rowPitch);
    }

    modifier_info.sType                       = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT;
    modifier_info.pNext                       = pNext;
    modifier_info.drmFormatModifier           = drmFormatModifier;
    modifier_info.drmFormatModifierPlaneCount = planeCount;
    modifier_info.pPlaneLayouts               = pPlaneLayouts;
    cvkCreateImageWithExternalMemoryInfo(device,
                                         &modifier_info,
                                         handleTypes,
                                         flags,
                                         imageType,
                                         format,
                                         extent,
                                         mipLevels,
                                         arrayLayers,
                                         samples,
                                         VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
                                         usage,
                                         sharingMode,
                                         queueFamilyIndexCount,
                                         pQueueFamilyIndices,
                                         initialLayout,
                                         pAllocator,
                                         pImage);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupImportedDrmFormatModifierImage(VkPhysicalDevice                   physicalDevice,
                                                VkDevice                           device,
                                                const void*                        pImageNext,
                                                VkExternalMemoryHandleTypeFlagBits handleType,
                                                VkFormat                           format,
                                                VkExtent3D                         extent,
                                                VkImageUsageFlags                  usage,
                                                VkSharingMode                      sharingMode,
                                                uint32_t                           queueFamilyIndexCount,
                                                const uint32_t*                    pQueueFamilyIndices,
                                                VkImageLayout                      initialLayout,
                                                uint64_t                           drmFormatModifier,
                                                uint32_t                           modifierPlaneCount,
                                                const VkSubresourceLayout*         pPlaneLayouts,
                                                const uint32_t*                    pPlaneMemoryObjectIndices,
                                                uint32_t                           memoryObjectCount,
                                                const VkDeviceSize*                pMemoryObjectSizes,
                                                const int*                         pMemoryObjectFds,
                                                const void*                        pMemoryNext,
                                                VkMemoryPropertyFlags              properties,
                                                const VkAllocationCallbacks*       pAllocator,
                                                VkImage*                           pImage,
                                                uint32_t                           memoryCapacity,
                                                VkDeviceMemory*                    pMemories,
                                                uint32_t*                          pMemoryCount)
{
    VkBindImageMemoryInfo                         bind_infos[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkBindImagePlaneMemoryInfo                    plane_infos[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkMemoryRequirements                          image_requirements;
    VkImageAspectFlagBits                         memory_plane_aspects[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                                      image_memory_type_bits[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkBool32                                      memory_object_used[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkPhysicalDeviceImageDrmFormatModifierInfoEXT modifier_info;
    VkImageCreateFlags                            flags;
    VkBool32                                      dedicated_allocation;
    bool_t                                        import_supported;
    uint32_t                                      memory_object_index;
    uint32_t                                      memory_plane_index;
    uint32_t                                      memory_type_index;
    int                                           imported_fd;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT == handleType);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(0 != extent.width);
    LOGASSERT(0 != extent.height);
    LOGASSERT(0 != extent.depth);
    LOGASSERT((VkImageUsageFlags)0 != usage);
    LOGASSERT(0 != modifierPlaneCount);
    LOGASSERT(CVK_MAX_DRM_FORMAT_MODIFIER_PLANES >= modifierPlaneCount);
    LOGASSERT(NULL != pPlaneLayouts);
    LOGASSERT(NULL != pPlaneMemoryObjectIndices);
    LOGASSERT(0 != memoryObjectCount);
    LOGASSERT(modifierPlaneCount >= memoryObjectCount);
    LOGASSERT(memoryCapacity >= memoryObjectCount);
    LOGASSERT(NULL != pMemoryObjectSizes);
    LOGASSERT(NULL != pMemoryObjectFds);
    LOGASSERT(NULL != pImage);
    LOGASSERT(NULL != pMemories);
    LOGASSERT(NULL != pMemoryCount);

    CRT_Memory_Set(memory_plane_aspects, 0, sizeof(memory_plane_aspects));
    CRT_Memory_Set(memory_object_used, 0, sizeof(memory_object_used));
    for (memory_object_index = 0; memory_object_index < memoryObjectCount; ++memory_object_index)
    {
        LOGASSERT(0 != pMemoryObjectSizes[memory_object_index]);
        LOGASSERT(0 <= pMemoryObjectFds[memory_object_index]);

        GetMemoryPropertiesInfoFd(device,
                                  NULL,
                                  handleType,
                                  pMemoryObjectFds[memory_object_index],
                                  &image_memory_type_bits[memory_object_index]);
    }
    flags = (VkImageCreateFlags)0;
    if (1 < memoryObjectCount)
    {
        flags = (VkImageCreateFlags)VK_IMAGE_CREATE_DISJOINT_BIT;
    }
    CRT_Memory_Set(&modifier_info, 0, sizeof(modifier_info));
    modifier_info.sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
    modifier_info.drmFormatModifier     = drmFormatModifier;
    modifier_info.sharingMode           = sharingMode;
    modifier_info.queueFamilyIndexCount = queueFamilyIndexCount;
    modifier_info.pQueueFamilyIndices   = pQueueFamilyIndices;
    import_supported =
        (VK_TRUE == cvkGetPhysicalDeviceExternalImageImportSupport(physicalDevice,
                                                                   handleType,
                                                                   format,
                                                                   VK_IMAGE_TYPE_2D,
                                                                   VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
                                                                   usage,
                                                                   flags,
                                                                   &modifier_info,
                                                                   &dedicated_allocation));
    LOGASSERT(TRUE == import_supported);
    if (1 < memoryObjectCount)
    {
        LOGASSERT(FALSE == dedicated_allocation);
    }

    CreateExternalDrmFormatModifierImage(device,
                                         pImageNext,
                                         (VkExternalMemoryHandleTypeFlags)handleType,
                                         flags,
                                         VK_IMAGE_TYPE_2D,
                                         format,
                                         extent,
                                         1,
                                         1,
                                         VK_SAMPLE_COUNT_1_BIT,
                                         usage,
                                         sharingMode,
                                         queueFamilyIndexCount,
                                         pQueueFamilyIndices,
                                         initialLayout,
                                         drmFormatModifier,
                                         modifierPlaneCount,
                                         pPlaneLayouts,
                                         pAllocator,
                                         pImage);
    if (1 == memoryObjectCount)
    {
        VkDeviceSize allocation_size;

        memory_object_index = 0;
        for (memory_plane_index = 0; memory_plane_index < modifierPlaneCount; ++memory_plane_index)
        {
            LOGASSERT(memory_object_index == pPlaneMemoryObjectIndices[memory_plane_index]);
        }

        cvkGetImageMemoryRequirements(device, (*pImage), &image_requirements);
        memory_type_index = cvkGetPhysicalDeviceCompatibleMemoryTypeIndex(
            physicalDevice,
            image_requirements.memoryTypeBits & image_memory_type_bits[memory_object_index],
            properties);
        LOGASSERT(VK_INVALID != memory_type_index);
        allocation_size = image_requirements.size;
        LOGASSERT(pMemoryObjectSizes[memory_object_index] >= allocation_size);
        imported_fd = dup(pMemoryObjectFds[memory_object_index]);
        LOGASSERT(0 <= imported_fd);
        if (TRUE == dedicated_allocation)
        {
            SetupImportedDedicatedImageMemoryFd(device,
                                                pMemoryNext,
                                                (*pImage),
                                                pMemoryObjectSizes[memory_object_index],
                                                memory_type_index,
                                                handleType,
                                                imported_fd,
                                                pAllocator,
                                                &pMemories[memory_object_index]);
        }
        else
        {
            SetupImportedImageMemoryFd(device,
                                       pMemoryNext,
                                       (*pImage),
                                       pMemoryObjectSizes[memory_object_index],
                                       memory_type_index,
                                       handleType,
                                       imported_fd,
                                       VK_NO_OFFSET,
                                       pAllocator,
                                       &pMemories[memory_object_index]);
        }
    }
    else
    {
        for (memory_plane_index = 0; memory_plane_index < modifierPlaneCount; ++memory_plane_index)
        {
            VkMemoryRequirements2 requirements;

            memory_object_index = pPlaneMemoryObjectIndices[memory_plane_index];
            LOGASSERT(memoryObjectCount > memory_object_index);

            memory_plane_aspects[memory_plane_index] = cvkGetMemoryPlaneAspect(memory_plane_index);
            cvkGetImagePlaneMemoryRequirementsInfo(device,
                                                   NULL,
                                                   memory_plane_aspects[memory_plane_index],
                                                   (*pImage),
                                                   NULL,
                                                   &requirements);
            LOGASSERT(pMemoryObjectSizes[memory_object_index] >= requirements.memoryRequirements.size);

            image_memory_type_bits[memory_object_index] &= requirements.memoryRequirements.memoryTypeBits;
            memory_object_used[memory_object_index]      = VK_TRUE;
        }
        for (memory_object_index = 0; memory_object_index < memoryObjectCount; ++memory_object_index)
        {
            LOGASSERT(VK_TRUE == memory_object_used[memory_object_index]);
            LOGASSERT(0 != image_memory_type_bits[memory_object_index]);

            memory_type_index =
                cvkGetPhysicalDeviceCompatibleMemoryTypeIndex(physicalDevice,
                                                              image_memory_type_bits[memory_object_index],
                                                              properties);
            LOGASSERT(VK_INVALID != memory_type_index);
            imported_fd = dup(pMemoryObjectFds[memory_object_index]);
            LOGASSERT(0 <= imported_fd);
            cvkAllocateImportMemoryFd(device,
                                      pMemoryNext,
                                      handleType,
                                      imported_fd,
                                      pMemoryObjectSizes[memory_object_index],
                                      memory_type_index,
                                      pAllocator,
                                      &pMemories[memory_object_index]);
        }
        for (memory_plane_index = 0; memory_plane_index < modifierPlaneCount; ++memory_plane_index)
        {
            memory_object_index = pPlaneMemoryObjectIndices[memory_plane_index];
            cvkInitBindImagePlaneMemoryInfo(&plane_infos[memory_plane_index],
                                            NULL,
                                            memory_plane_aspects[memory_plane_index]);
            cvkInitBindImageMemoryInfo(&bind_infos[memory_plane_index],
                                       &plane_infos[memory_plane_index],
                                       (*pImage),
                                       pMemories[memory_object_index],
                                       0);
        }

        cvkBindImageMemory2(device, modifierPlaneCount, bind_infos);
    }
    (*pMemoryCount) = memoryObjectCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupImportedImageMemoryFd(VkDevice                           device,
                                       const void*                        pNext,
                                       VkImage                            image,
                                       VkDeviceSize                       allocationSize,
                                       uint32_t                           memoryTypeIndex,
                                       VkExternalMemoryHandleTypeFlagBits handleType,
                                       int                                fd,
                                       VkDeviceSize                       memoryOffset,
                                       const VkAllocationCallbacks*       pAllocator,
                                       VkDeviceMemory*                    pMemory)
{
    cvkAllocateImportMemoryFd(device, pNext, handleType, fd, allocationSize, memoryTypeIndex, pAllocator, pMemory);
    cvkBindImageMemory(device, image, (*pMemory), memoryOffset);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupExportedMemoryBufferFd(VkPhysicalDevice                   physicalDevice,
                                        VkDevice                           device,
                                        const void*                        pBufferNext,
                                        VkExternalMemoryHandleTypeFlagBits handleType,
                                        VkBufferCreateFlags                flags,
                                        VkDeviceSize                       size,
                                        VkBufferUsageFlags                 usage,
                                        VkSharingMode                      sharingMode,
                                        uint32_t                           queueFamilyIndexCount,
                                        const uint32_t*                    pQueueFamilyIndices,
                                        const void*                        pMemoryNext,
                                        const void*                        pGetNext,
                                        VkMemoryPropertyFlags              properties,
                                        const VkAllocationCallbacks*       pAllocator,
                                        VkBuffer*                          pBuffer,
                                        VkDeviceMemory*                    pMemory,
                                        int*                               pFd)
{
    VkExportMemoryAllocateInfo export_info;
    cvkInitExportMemoryAllocateInfo(&export_info, pMemoryNext, (VkExternalMemoryHandleTypeFlags)handleType);
    cvkCreateBufferWithExternalMemoryInfo(device,
                                          pBufferNext,
                                          (VkExternalMemoryHandleTypeFlags)handleType,
                                          flags,
                                          size,
                                          usage,
                                          sharingMode,
                                          queueFamilyIndexCount,
                                          pQueueFamilyIndices,
                                          pAllocator,
                                          pBuffer);
    cvkAllocateBufferMemory(physicalDevice, device, (*pBuffer), properties, &export_info, pAllocator, pMemory);
    cvkBindBufferMemory(device, (*pBuffer), (*pMemory), 0);
    cvkGetMemoryFd(device, pGetNext, (*pMemory), handleType, pFd);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupExportedDedicatedMemoryBufferFd(VkPhysicalDevice                   physicalDevice,
                                                 VkDevice                           device,
                                                 const void*                        pBufferNext,
                                                 VkExternalMemoryHandleTypeFlagBits handleType,
                                                 VkBufferCreateFlags                flags,
                                                 VkDeviceSize                       size,
                                                 VkBufferUsageFlags                 usage,
                                                 VkSharingMode                      sharingMode,
                                                 uint32_t                           queueFamilyIndexCount,
                                                 const uint32_t*                    pQueueFamilyIndices,
                                                 const void*                        pMemoryNext,
                                                 const void*                        pGetNext,
                                                 VkMemoryPropertyFlags              properties,
                                                 const VkAllocationCallbacks*       pAllocator,
                                                 VkBuffer*                          pBuffer,
                                                 VkDeviceMemory*                    pMemory,
                                                 int*                               pFd)
{
    VkExportMemoryAllocateInfo    export_info;
    VkMemoryDedicatedAllocateInfo dedicated_info;
    cvkInitExportMemoryAllocateInfo(&export_info, pMemoryNext, (VkExternalMemoryHandleTypeFlags)handleType);
    cvkCreateBufferWithExternalMemoryInfo(device,
                                          pBufferNext,
                                          (VkExternalMemoryHandleTypeFlags)handleType,
                                          flags,
                                          size,
                                          usage,
                                          sharingMode,
                                          queueFamilyIndexCount,
                                          pQueueFamilyIndices,
                                          pAllocator,
                                          pBuffer);
    cvkInitMemoryDedicatedAllocateInfo(&dedicated_info, &export_info, VK_NULL_HANDLE, (*pBuffer));
    cvkAllocateBufferMemory(physicalDevice, device, (*pBuffer), properties, &dedicated_info, pAllocator, pMemory);
    cvkBindBufferMemory(device, (*pBuffer), (*pMemory), 0);
    cvkGetMemoryFd(device, pGetNext, (*pMemory), handleType, pFd);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupImportedDmaBufImage(VkPhysicalDevice             physicalDevice,
                                     VkDevice                     device,
                                     const void*                  pImageNext,
                                     VkFormat                     format,
                                     VkExtent3D                   extent,
                                     VkImageUsageFlags            usage,
                                     VkSharingMode                sharingMode,
                                     uint32_t                     queueFamilyIndexCount,
                                     const uint32_t*              pQueueFamilyIndices,
                                     VkImageLayout                initialLayout,
                                     uint64_t                     drmFormatModifier,
                                     uint32_t                     planeCount,
                                     const uint32_t*              pPlaneOffsets,
                                     const uint32_t*              pPlaneRowPitches,
                                     const uint32_t*              pPlaneMemoryObjectIndices,
                                     uint32_t                     memoryObjectCount,
                                     const VkDeviceSize*          pMemoryObjectSizes,
                                     const int*                   pMemoryObjectFds,
                                     const void*                  pMemoryNext,
                                     VkMemoryPropertyFlags        properties,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkImage*                     pImage,
                                     uint32_t                     memoryCapacity,
                                     VkDeviceMemory*              pMemories,
                                     uint32_t*                    pMemoryCount)
{
    uint32_t                             format_plane_count;
    VkDrmFormatModifierPropertiesEXT     modifiers[CVK_MAX_DRM_FORMAT_MODIFIERS];
    VkDrmFormatModifierPropertiesEXT     modifier_properties;
    VkSubresourceLayout                  plane_layouts[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkDrmFormatModifierPropertiesListEXT modifier_list;
    uint32_t                             modifier_index;
    bool_t                               modifier_supported;
    uint32_t                             plane_index;

    LOGASSERT(NULL != pPlaneOffsets);
    LOGASSERT(NULL != pPlaneRowPitches);
    LOGASSERT((0 != planeCount) && (CVK_MAX_DRM_FORMAT_MODIFIER_PLANES >= planeCount));
    format_plane_count = cvkGetFormatPlaneCount(format);
    LOGASSERT(format_plane_count <= planeCount);

    cvkGetPhysicalDeviceDrmFormatModifierPropertiesList(physicalDevice, format, NULL, 0, NULL, &modifier_list);
    LOGASSERT(CVK_MAX_DRM_FORMAT_MODIFIERS >= modifier_list.drmFormatModifierCount);
    cvkGetPhysicalDeviceDrmFormatModifierPropertiesList(physicalDevice,
                                                        format,
                                                        NULL,
                                                        modifier_list.drmFormatModifierCount,
                                                        modifiers,
                                                        &modifier_list);
    modifier_supported = FALSE;
    for (modifier_index = 0; modifier_index < modifier_list.drmFormatModifierCount; ++modifier_index)
    {
        if (drmFormatModifier == modifiers[modifier_index].drmFormatModifier)
        {
            modifier_properties = modifiers[modifier_index];
            modifier_supported  = TRUE;
            break;
        }
    }
    if (!modifier_supported)
    {
        OS_AssertFailure("Vulkan format does not support the exported DRM modifier", __FILE__, __LINE__);
    }
    LOGASSERT(0 != modifier_properties.drmFormatModifierPlaneCount);
    LOGASSERT(planeCount >= modifier_properties.drmFormatModifierPlaneCount);

    for (plane_index = 0; plane_index < modifier_properties.drmFormatModifierPlaneCount; ++plane_index)
    {
        plane_layouts[plane_index].offset     = pPlaneOffsets[plane_index];
        plane_layouts[plane_index].size       = 0;
        plane_layouts[plane_index].rowPitch   = pPlaneRowPitches[plane_index];
        plane_layouts[plane_index].arrayPitch = 0;
        plane_layouts[plane_index].depthPitch = 0;
    }

    SetupImportedDrmFormatModifierImage(physicalDevice,
                                        device,
                                        pImageNext,
                                        VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                                        format,
                                        extent,
                                        usage,
                                        sharingMode,
                                        queueFamilyIndexCount,
                                        pQueueFamilyIndices,
                                        initialLayout,
                                        drmFormatModifier,
                                        modifier_properties.drmFormatModifierPlaneCount,
                                        plane_layouts,
                                        pPlaneMemoryObjectIndices,
                                        memoryObjectCount,
                                        pMemoryObjectSizes,
                                        pMemoryObjectFds,
                                        pMemoryNext,
                                        properties,
                                        pAllocator,
                                        pImage,
                                        memoryCapacity,
                                        pMemories,
                                        pMemoryCount);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void GetImageFormats(VkFormat format, uint32_t preferredVaFourcc, uint32_t* pVaFourcc, uint32_t* pDrmFormat)
{
    LOGASSERT(NULL != pVaFourcc);
    LOGASSERT(NULL != pDrmFormat);

    /* DRM packed channel names describe a little-endian word; VA names describe byte order. */
    switch (format)
    {
    case VK_FORMAT_R8_UNORM:
        (*pVaFourcc)  = VA_FOURCC_Y800;
        (*pDrmFormat) = DRM_FORMAT_R8;
        break;
    case VK_FORMAT_R16_UNORM:
        (*pVaFourcc)  = VA_FOURCC_Y16;
        (*pDrmFormat) = DRM_FORMAT_R16;
        break;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_A8B8G8R8_UNORM_PACK32:
        (*pVaFourcc)  = VA_FOURCC_RGBA;
        (*pDrmFormat) = DRM_FORMAT_ABGR8888;
        break;
    case VK_FORMAT_B8G8R8A8_UNORM:
        (*pVaFourcc)  = VA_FOURCC_BGRA;
        (*pDrmFormat) = DRM_FORMAT_ARGB8888;
        break;
    case VK_FORMAT_G8B8G8R8_422_UNORM:
        (*pVaFourcc)  = VA_FOURCC_YUY2;
        (*pDrmFormat) = DRM_FORMAT_YUYV;
        break;
    case VK_FORMAT_B8G8R8G8_422_UNORM:
        (*pVaFourcc)  = VA_FOURCC_UYVY;
        (*pDrmFormat) = DRM_FORMAT_UYVY;
        break;
    case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
        (*pVaFourcc)  = VA_FOURCC_NV12;
        (*pDrmFormat) = DRM_FORMAT_NV12;
        break;
    case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
        (*pVaFourcc)  = VA_FOURCC_P208;
        (*pDrmFormat) = DRM_FORMAT_NV16;
        break;
    case VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16:
        (*pVaFourcc)  = VA_FOURCC_P010;
        (*pDrmFormat) = DRM_FORMAT_P010;
        break;
    case VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16:
        (*pVaFourcc)  = VA_FOURCC_P012;
        (*pDrmFormat) = DRM_FORMAT_P012;
        break;
    case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
        (*pVaFourcc)  = VA_FOURCC_P016;
        (*pDrmFormat) = DRM_FORMAT_P016;
        break;
    case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
        (*pVaFourcc)  = (VA_FOURCC_IYUV == preferredVaFourcc) ? VA_FOURCC_IYUV : VA_FOURCC_I420;
        (*pDrmFormat) = DRM_FORMAT_YUV420;
        break;
    case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
        (*pVaFourcc)  = VA_FOURCC_422H;
        (*pDrmFormat) = DRM_FORMAT_YUV422;
        break;
    case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
        (*pVaFourcc)  = VA_FOURCC_444P;
        (*pDrmFormat) = DRM_FORMAT_YUV444;
        break;
    default:
        OS_AssertFailure("Vulkan format has no matching VA/DRM surface representation", __FILE__, __LINE__);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  No separate VA or Vulkan wrapper outputs escape the image owner.
 */
void Gfx_Image_InitializeShared(Gfx_Image_t* image, const Gfx_Image_CreateInfo_t* createInfo)
{
    bool_t is_import;
    bool_t is_export;
    bool_t is_shared;
    bool_t is_vulkan;
    bool_t is_va;
    bool_t valid_backend;

    LOGASSERT(NULL != image);
    LOGASSERT(NULL != createInfo);
    LOGASSERT(NULL != createInfo->platformCreateInfo);
    LOGASSERT(0 < createInfo->width);
    LOGASSERT(0 < createInfo->height);
    is_import     = GFX_IMAGE_KIND_IMPORT == createInfo->kind;
    is_export     = GFX_IMAGE_KIND_EXPORT == createInfo->kind;
    is_shared     = is_import || is_export;
    is_vulkan     = GFX_IMAGE_HINT_VULKAN == createInfo->hint;
    is_va         = GFX_IMAGE_HINT_VAAPI == createInfo->hint;
    valid_backend = is_vulkan || is_va;
    LOGASSERT(is_shared);
    LOGASSERT(valid_backend);

    CRT_Memory_Set(image, 0, sizeof(*image));
    image->hint         = createInfo->hint;
    image->kind         = createInfo->kind;
    image->va.surfaceId = VA_INVALID_SURFACE;
    if (is_va)
    {
        LOGASSERT(NULL != createInfo->va.dpy);
        image->va.dpy = createInfo->va.dpy;
        if (is_import)
        {
            InitializeVaImport(image, createInfo);
        }
        else
        {
            InitializeVaExport(image, createInfo);
        }
    }
    else
    {
        LOGASSERT(VK_NULL_HANDLE != createInfo->vk.physicalDevice);
        LOGASSERT(VK_NULL_HANDLE != createInfo->vk.device);
        LOGASSERT(VK_FORMAT_UNDEFINED != createInfo->vk.format);
        image->vk.device        = createInfo->vk.device;
        image->vk.format        = createInfo->vk.format;
        image->vk.extent.width  = createInfo->width;
        image->vk.extent.height = createInfo->height;
        image->vk.extent.depth  = 1;
        if (is_import)
        {
            InitializeVulkanImport(image, createInfo);
        }
        else
        {
            InitializeVulkanExport(image, createInfo);
        }
    }
}
