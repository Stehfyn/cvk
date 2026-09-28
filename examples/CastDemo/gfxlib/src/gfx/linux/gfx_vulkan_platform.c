/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_vulkan_platform.h"
#include "gfx/gfx_vaapi.h"
#include "gfx/gfx_vulkan.h"
#include "os/os_assert.h"
#include "utl/utl_defines.h"

#include <drm/drm_fourcc.h>
#include <string.h>
#include <unistd.h>
#include <va/va_drmcommon.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define PCI_DOMAIN_SHIFT (24U) ///< Bit offset of the PCI domain in a packed adapter identifier.
#define PCI_BUS_SHIFT    (16U) ///< Bit offset of the PCI bus in a packed adapter identifier.
#define PCI_DEVICE_SHIFT (8U)  ///< Bit offset of the PCI device in a packed adapter identifier.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/**
 * \brief Vulkan instance extensions used by the Linux platform implementation.
 */
static const char* const c_vkPlatformInstanceExtensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME, VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

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
 * \retval    None
 */
static void GetSurfaceMemoryObjectsDrm(const VADRMPRIMESurfaceDescriptor* descriptor, uint32_t* objectCount, uint64_t* objectSizes, int32_t* objectFds, uint64_t* drmFormatModifier);

/*************************************************************************************************/
/**
 * \brief Queries Vulkan memory-type compatibility for an external allocation's file descriptor.
 *
 * \param[in]  device      Logical device
 * \param[in]  handleType  External-memory handle type
 * \param[in]  fd          External allocation descriptor
 * \param[out] pProperties Initialized properties structure receiving the result
 *
 * \retval    None
 */
static void GetMemoryPropertiesFd(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pProperties);

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
 * \retval    None
 */
static void GetMemoryPropertiesInfoFd(VkDevice device, void* pNext, VkExternalMemoryHandleTypeFlagBits handleType, int fd, uint32_t* pMemoryTypeBits);

/*************************************************************************************************/
/**
 * \brief Allocates memory by importing the supplied file descriptor.
 *
 * \param[in]  device          Logical device importing the descriptor
 * \param[in]  pNext           Optional allocation extension chain
 * \param[in]  allocationSize  Imported memory-object size in bytes
 * \param[in]  memoryTypeIndex Compatible physical-device memory-type index selected by the caller
 * \param[in]  handleType      External-memory FD handle type
 * \param[in]  fd              Descriptor transferred to Vulkan on successful allocation; not duplicated here
 * \param[in]  pAllocator      Optional host allocation callbacks
 * \param[out] pMemory         Imported memory handle
 *
 * \retval    None
 */
static void AllocateImportedMemoryFd(VkDevice                           device,
                                     const void*                        pNext,
                                     VkDeviceSize                       allocationSize,
                                     uint32_t                           memoryTypeIndex,
                                     VkExternalMemoryHandleTypeFlagBits handleType,
                                     int                                fd,
                                     const VkAllocationCallbacks*       pAllocator,
                                     VkDeviceMemory*                    pMemory);

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
 * \retval    None
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
 * \retval    None
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
 * \brief Exports a device-memory allocation as a file descriptor.
 *
 * \param[in]  device Logical device
 * \param[in]  pInfo  Memory export parameters
 * \param[out] pFd    Receives an owned file descriptor
 *
 * \retval    None
 */
static void GetMemoryFd(VkDevice device, const VkMemoryGetFdInfoKHR* pInfo, int* pFd);

/*************************************************************************************************/
/**
 * \brief Exports Vulkan device memory as a POSIX file descriptor.
 *
 * \param[in]  device     Logical device owning memory
 * \param[in]  pNext      Export extension chain, or NULL
 * \param[in]  memory     Exportable Vulkan allocation
 * \param[in]  handleType Requested external-memory descriptor type
 * \param[out] pFd        Receives an owned file descriptor
 *
 * \retval    None
 */
static void GetMemoryInfoFd(VkDevice device, const void* pNext, VkDeviceMemory memory, VkExternalMemoryHandleTypeFlagBits handleType, int* pFd);

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
 * \retval    None
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
 * \retval    None
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
 * \retval    None
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
 * \retval    None
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
 * \retval    None
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
 * \retval    None
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
 * \brief Packs Vulkan PCI bus properties into the repository adapter identifier.
 *
 * \param[in] domain   PCI domain
 * \param[in] bus      PCI bus
 * \param[in] device   PCI device
 * \param[in] function PCI function
 *
 * \return domain:bus:device:function packed from most to least significant.
 */
static uint64_t PackPciAddress(uint32_t domain, uint32_t bus, uint32_t device, uint32_t function);

/*************************************************************************************************/
/**
 * \brief Converts Vulkan format to VA and DRM codes without changing channel or plane order.
 *
 * \param[in]  format            Requested Vulkan format
 * \param[in]  preferredVaFourcc Preferred VA representation; zero selects the canonical format
 * \param[out] pVaFourcc         Supported VA representation, falling back to the canonical format
 * \param[out] pDrmFormat        DRM format of its composed layer
 *
 * \retval None
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
static void GetSurfaceMemoryObjectsDrm(const VADRMPRIMESurfaceDescriptor* descriptor, uint32_t* numObjects, uint64_t* objectSizes, int32_t* objectFds, uint64_t* drmFormatModifier)
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
static void GetMemoryPropertiesFd(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pProperties)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != pProperties);

    LOGASSERT(NULL != vkGetMemoryFdPropertiesKHR);
    VKCHECK(vkGetMemoryFdPropertiesKHR(device, handleType, fd, pProperties));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void GetMemoryPropertiesInfoFd(VkDevice device, void* pNext, VkExternalMemoryHandleTypeFlagBits handleType, int fd, uint32_t* pMemoryTypeBits)
{
    VkMemoryFdPropertiesKHR properties;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 <= fd);
    LOGASSERT(NULL != pMemoryTypeBits);

    (void)memset(&properties, 0, sizeof(properties));
    properties.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
    properties.pNext = pNext;
    GetMemoryPropertiesFd(device, handleType, fd, &properties);

    (*pMemoryTypeBits) = properties.memoryTypeBits;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void AllocateImportedMemoryFd(VkDevice                           device,
                                     const void*                        pNext,
                                     VkDeviceSize                       allocationSize,
                                     uint32_t                           memoryTypeIndex,
                                     VkExternalMemoryHandleTypeFlagBits handleType,
                                     int                                fd,
                                     const VkAllocationCallbacks*       pAllocator,
                                     VkDeviceMemory*                    pMemory)
{
    VkImportMemoryFdInfoKHR import_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != allocationSize);
    LOGASSERT(0 <= fd);
    LOGASSERT(NULL != pMemory);

    import_info.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    import_info.pNext      = pNext;
    import_info.handleType = handleType;
    import_info.fd         = fd;
    Gfx_Vulkan_AllocateMemory(device, &import_info, allocationSize, memoryTypeIndex, pAllocator, pMemory);
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
    Gfx_Vulkan_AllocateDedicatedImageMemory(device, &import_info, image, allocationSize, memoryTypeIndex, pAllocator, pMemory);
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
    AllocateImportedDedicatedImageMemoryFd(device, pNext, image, allocationSize, memoryTypeIndex, handleType, fd, pAllocator, pMemory);
    Gfx_Vulkan_BindImageMemory(device, image, (*pMemory), VK_NO_OFFSET);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void GetMemoryFd(VkDevice device, const VkMemoryGetFdInfoKHR* pInfo, int* pFd)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != pInfo);
    LOGASSERT(NULL != pFd);

    LOGASSERT(NULL != vkGetMemoryFdKHR);
    VKCHECK(vkGetMemoryFdKHR(device, pInfo, pFd));
    LOGASSERT(0 <= (*pFd));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void GetMemoryInfoFd(VkDevice device, const void* pNext, VkDeviceMemory memory, VkExternalMemoryHandleTypeFlagBits handleType, int* pFd)
{
    VkMemoryGetFdInfoKHR info;

    LOGASSERT(VK_NULL_HANDLE != memory);

    info.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR;
    info.pNext      = pNext;
    info.memory     = memory;
    info.handleType = handleType;
    GetMemoryFd(device, &info, pFd);
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
    LOGASSERT(VK_MAX_DRM_FORMAT_MODIFIER_PLANES >= planeCount);
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
    Gfx_Vulkan_CreateExternalImage(device,
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
    VkImageAspectFlagBits                         memory_plane_aspects[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkDeviceMemory                                plane_memories[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                                      image_memory_type_bits[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkBool32                                      memory_object_used[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkPhysicalDeviceImageDrmFormatModifierInfoEXT modifier_info;
    VkImageCreateFlags                            flags;
    bool_t                                        dedicated_allocation;
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
    LOGASSERT(VK_MAX_DRM_FORMAT_MODIFIER_PLANES >= modifierPlaneCount);
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

    (void)memset(memory_plane_aspects, 0, sizeof(memory_plane_aspects));
    (void)memset(plane_memories, 0, sizeof(plane_memories));
    (void)memset(memory_object_used, 0, sizeof(memory_object_used));
    for (memory_object_index = 0; memory_object_index < memoryObjectCount; ++memory_object_index)
    {
        LOGASSERT(0 != pMemoryObjectSizes[memory_object_index]);
        LOGASSERT(0 <= pMemoryObjectFds[memory_object_index]);

        GetMemoryPropertiesInfoFd(device, NULL, handleType, pMemoryObjectFds[memory_object_index], &image_memory_type_bits[memory_object_index]);
    }
    flags = (VkImageCreateFlags)0;
    if (1 < memoryObjectCount)
    {
        flags = (VkImageCreateFlags)VK_IMAGE_CREATE_DISJOINT_BIT;
    }
    (void)memset(&modifier_info, 0, sizeof(modifier_info));
    modifier_info.sType                 = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT;
    modifier_info.drmFormatModifier     = drmFormatModifier;
    modifier_info.sharingMode           = sharingMode;
    modifier_info.queueFamilyIndexCount = queueFamilyIndexCount;
    modifier_info.pQueueFamilyIndices   = pQueueFamilyIndices;
    import_supported                    = Gfx_Vulkan_GetPhysicalDeviceExternalImageImportSupport(physicalDevice,
                                                                                                 &modifier_info,
                                                                                                 flags,
                                                                                                 VK_IMAGE_TYPE_2D,
                                                                                                 format,
                                                                                                 VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
                                                                                                 usage,
                                                                                                 handleType,
                                                                                                 &dedicated_allocation);
    LOGASSERT(TRUE == import_supported);
    if (1u < memoryObjectCount)
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

        Gfx_Vulkan_GetImageMemoryAllocationInfo(physicalDevice, device, (*pImage), image_memory_type_bits[memory_object_index], properties, &allocation_size, &memory_type_index);
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

        (*pMemoryCount) = memoryObjectCount;
        return;
    }
    for (memory_plane_index = 0; memory_plane_index < modifierPlaneCount; ++memory_plane_index)
    {
        VkMemoryRequirements2 requirements;

        memory_object_index = pPlaneMemoryObjectIndices[memory_plane_index];
        LOGASSERT(memoryObjectCount > memory_object_index);

        memory_plane_aspects[memory_plane_index] = Gfx_Vulkan_GetMemoryPlaneAspect(memory_plane_index);
        Gfx_Vulkan_GetImagePlaneMemoryRequirements2(device, NULL, (*pImage), memory_plane_aspects[memory_plane_index], NULL, &requirements);
        LOGASSERT(pMemoryObjectSizes[memory_object_index] >= requirements.memoryRequirements.size);

        image_memory_type_bits[memory_object_index] &= requirements.memoryRequirements.memoryTypeBits;
        memory_object_used[memory_object_index] = VK_TRUE;
    }
    for (memory_object_index = 0; memory_object_index < memoryObjectCount; ++memory_object_index)
    {
        LOGASSERT(VK_TRUE == memory_object_used[memory_object_index]);
        LOGASSERT(0 != image_memory_type_bits[memory_object_index]);

        memory_type_index = Gfx_Vulkan_GetPhysicalDeviceCompatibleMemoryTypeIndex(physicalDevice, image_memory_type_bits[memory_object_index], properties);
        LOGASSERT(VK_INVALID != memory_type_index);
        imported_fd = dup(pMemoryObjectFds[memory_object_index]);
        LOGASSERT(0 <= imported_fd);
        AllocateImportedMemoryFd(device, pMemoryNext, pMemoryObjectSizes[memory_object_index], memory_type_index, handleType, imported_fd, pAllocator, &pMemories[memory_object_index]);
    }
    for (memory_plane_index = 0; memory_plane_index < modifierPlaneCount; ++memory_plane_index)
    {
        memory_object_index                = pPlaneMemoryObjectIndices[memory_plane_index];
        plane_memories[memory_plane_index] = pMemories[memory_object_index];
    }

    Gfx_Vulkan_BindImagePlaneMemories(device, NULL, (*pImage), modifierPlaneCount, memory_plane_aspects, plane_memories, NULL);

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
    AllocateImportedMemoryFd(device, pNext, allocationSize, memoryTypeIndex, handleType, fd, pAllocator, pMemory);
    Gfx_Vulkan_BindImageMemory(device, image, (*pMemory), memoryOffset);
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
    Gfx_Vulkan_SetupExportableMemoryBuffer(physicalDevice,
                                           device,
                                           pBufferNext,
                                           (VkExternalMemoryHandleTypeFlags)handleType,
                                           flags,
                                           size,
                                           usage,
                                           sharingMode,
                                           queueFamilyIndexCount,
                                           pQueueFamilyIndices,
                                           pMemoryNext,
                                           properties,
                                           pAllocator,
                                           pBuffer,
                                           pMemory);
    GetMemoryInfoFd(device, pGetNext, (*pMemory), handleType, pFd);
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
    Gfx_Vulkan_SetupExportedDedicatedMemoryBuffer(physicalDevice,
                                                    device,
                                                    pBufferNext,
                                                    (VkExternalMemoryHandleTypeFlags)handleType,
                                                    flags,
                                                    size,
                                                    usage,
                                                    sharingMode,
                                                    queueFamilyIndexCount,
                                                    pQueueFamilyIndices,
                                                    pMemoryNext,
                                                    properties,
                                                    pAllocator,
                                                    pBuffer,
                                                    pMemory);
    GetMemoryInfoFd(device, pGetNext, (*pMemory), handleType, pFd);
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
    uint32_t                         format_plane_count;
    VkDrmFormatModifierPropertiesEXT modifiers[VK_MAX_DRM_FORMAT_MODIFIERS];
    VkDrmFormatModifierPropertiesEXT modifier_properties;
    VkSubresourceLayout              plane_layouts[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                         modifier_count;
    bool_t                           modifier_supported;
    uint32_t                         plane_index;

    LOGASSERT(NULL != pPlaneOffsets);
    LOGASSERT(NULL != pPlaneRowPitches);
    LOGASSERT((0 != planeCount) && (VK_MAX_DRM_FORMAT_MODIFIER_PLANES >= planeCount));
    format_plane_count = Gfx_Vulkan_GetFormatPlaneCount(format);
    LOGASSERT(format_plane_count <= planeCount);

    Gfx_Vulkan_GetPhysicalDeviceDrmFormatModifierProperties(physicalDevice, NULL, format, &modifier_count, modifiers);
    modifier_supported = Gfx_Vulkan_GetDrmFormatModifierProperties(modifier_count, modifiers, drmFormatModifier, &modifier_properties);
    if (!modifier_supported)
    {
        LOGFATAL("Vulkan format does not support the exported DRM modifier");
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
        LOGFATAL("Vulkan format has no matching VA/DRM surface representation");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint64_t PackPciAddress(uint32_t domain, uint32_t bus, uint32_t device, uint32_t function)
{
    LOGASSERT(UINT8_MAX >= bus);
    LOGASSERT(UINT8_MAX >= device);
    LOGASSERT(UINT8_MAX >= function);

    return ((uint64_t)domain << PCI_DOMAIN_SHIFT) | ((uint64_t)bus << PCI_BUS_SHIFT) | ((uint64_t)device << PCI_DEVICE_SHIFT) | function;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_Platform_SetupDeviceQueue(uint64_t                        adapterId,
                                          VkPhysicalDevice                physicalDevice,
                                          const void*                     pNext,
                                          VkDeviceCreateFlags             flags,
                                          const void*                     pQueueNext,
                                          VkDeviceQueueCreateFlags        queueFlags,
                                          uint32_t                        queueFamilyIndex,
                                          float                           queuePriority,
                                          uint32_t                        enabledLayerCount,
                                          const char* const*              ppEnabledLayerNames,
                                          uint32_t                        requestedExtensionCount,
                                          const char* const*              ppRequestedExtensionNames,
                                          const VkPhysicalDeviceFeatures* pEnabledFeatures,
                                          const VkAllocationCallbacks*    pAllocator,
                                          VkDevice*                       pDevice,
                                          VkQueue*                        pQueue)
{
    VkPhysicalDevicePCIBusInfoPropertiesEXT properties;
    uint64_t                                physical_device_id;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pDevice);
    LOGASSERT(NULL != pQueue);

    Gfx_Vulkan_GetPhysicalDevicePciBusInfoProperties(physicalDevice, NULL, &properties);

    physical_device_id = PackPciAddress(properties.pciDomain, properties.pciBus, properties.pciDevice, properties.pciFunction);
    LOGASSERT(adapterId == physical_device_id);

    Gfx_Vulkan_SetupDeviceQueue(physicalDevice,
                                pNext,
                                flags,
                                pQueueNext,
                                queueFlags,
                                queueFamilyIndex,
                                queuePriority,
                                enabledLayerCount,
                                ppEnabledLayerNames,
                                requestedExtensionCount,
                                ppRequestedExtensionNames,
                                pEnabledFeatures,
                                pAllocator,
                                pDevice,
                                pQueue);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_Platform_ExportImage(VkPhysicalDevice             physicalDevice,
                                     VkDevice                     device,
                                     VkFormat                     format,
                                     VkExtent3D                   extent,
                                     VkImageUsageFlags            usage,
                                     void*                        drmPrimeDescriptor,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkImage*                     pImage,
                                     VkDeviceMemory*              pMemories,
                                     uint32_t*                    pMemoryCount,
                                     VkBuffer*                    pExternalBuffer,
                                     VkDeviceMemory*              pExternalBufferMemory,
                                     VkDeviceSize*                pExternalBufferSize,
                                     bool_t*                      pExternalBufferHostCopy,
                                     uint32_t*                    pExternalYStride,
                                     uint32_t*                    pExternalUvOffset,
                                     uint32_t*                    pExternalUvStride,
                                     VkSemaphore*                 pExternalSemaphore)
{
    const VkExternalMemoryHandleTypeFlagBits handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
    VkMemoryRequirements                     memory_requirements;
    VADRMPRIMESurfaceDescriptor*             descriptor;
    VkDeviceSize                             plane_offsets[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                                 plane_strides[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkExtent3D                               plane_extents[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkDeviceSize                             buffer_size;
    bool_t                                   dedicated_only;
    bool_t                                   export_supported;
    uint32_t                                 plane_count;
    uint32_t                                 plane_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != extent.width && 0 != extent.height && 1 == extent.depth);
    LOGASSERT((VkImageUsageFlags)0 != usage);
    LOGASSERT(NULL != drmPrimeDescriptor);
    LOGASSERT(NULL != pImage);
    LOGASSERT(NULL != pMemories);
    LOGASSERT(NULL != pMemoryCount);
    LOGASSERT(NULL != pExternalBuffer);
    LOGASSERT(NULL != pExternalBufferMemory);
    LOGASSERT(NULL != pExternalBufferSize);
    LOGASSERT(NULL != pExternalBufferHostCopy);
    LOGASSERT(NULL != pExternalYStride);
    LOGASSERT(NULL != pExternalUvOffset);
    LOGASSERT(NULL != pExternalUvStride);
    LOGASSERT(NULL != pExternalSemaphore);

    descriptor = (VADRMPRIMESurfaceDescriptor*)drmPrimeDescriptor;
    (void)memset(descriptor, 0, sizeof(*descriptor));
    (*pImage) = VK_NULL_HANDLE;
    (void)memset(pMemories, 0, sizeof((*pMemories)) * VK_MAX_DRM_FORMAT_MODIFIER_PLANES);
    (*pMemoryCount)            = 0;
    (*pExternalBuffer)         = VK_NULL_HANDLE;
    (*pExternalBufferMemory)   = VK_NULL_HANDLE;
    (*pExternalBufferSize)     = 0;
    (*pExternalBufferHostCopy) = FALSE;
    (*pExternalYStride)        = 0;
    (*pExternalUvOffset)       = 0;
    (*pExternalUvStride)       = 0;
    (*pExternalSemaphore)      = VK_NULL_HANDLE;
    GetImageFormats(format, 0, &descriptor->fourcc, &descriptor->layers[0].drm_format);
    plane_count = Gfx_Vulkan_CalculateFormatPlaneLayouts(format, extent.width, extent.height, extent.depth, 1, VA_LINEAR_ROW_ALIGNMENT, plane_offsets, plane_strides, plane_extents, &buffer_size);
    LOGASSERT(COUNT_OF(descriptor->layers[0].offset) >= plane_count);
    LOGASSERT(UINT32_MAX >= buffer_size);
    export_supported = Gfx_Vulkan_GetPhysicalDeviceExternalBufferExportSupport(physicalDevice, NULL, (VkBufferCreateFlags)0, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, handle_type, &dedicated_only);
    LOGASSERT(export_supported);
    if (dedicated_only)
    {
        SetupExportedDedicatedMemoryBufferFd(physicalDevice,
                                             device,
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
                                             pAllocator,
                                             pExternalBuffer,
                                             pExternalBufferMemory,
                                             &descriptor->objects[0].fd);
    }
    else
    {
        SetupExportedMemoryBufferFd(physicalDevice,
                                    device,
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
                                    pAllocator,
                                    pExternalBuffer,
                                    pExternalBufferMemory,
                                    &descriptor->objects[0].fd);
    }

    Gfx_Vulkan_GetBufferMemoryRequirements(device, (*pExternalBuffer), &memory_requirements);
    LOGASSERT(UINT32_MAX >= memory_requirements.size);
    descriptor->width                          = extent.width;
    descriptor->height                         = extent.height;
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
    (*pExternalBufferSize)     = buffer_size;
    (*pExternalBufferHostCopy) = FALSE;
    (*pExternalYStride)        = plane_strides[0];
    if (1u < plane_count)
    {
        (*pExternalUvOffset) = (uint32_t)plane_offsets[1];
        (*pExternalUvStride) = plane_strides[1];
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_Platform_ImportImage(VkPhysicalDevice                   physicalDevice,
                                     VkDevice                           device,
                                     VkFormat                           format,
                                     VkExtent3D                         extent,
                                     VkImageUsageFlags                  usage,
                                     const void*                        drmPrimeDescriptor,
                                     void*                              sharedImageHandle,
                                     VkExternalMemoryHandleTypeFlagBits sharedImageHandleType,
                                     VkDeviceSize                       sharedImageAllocationSize,
                                     bool_t                             transferBufferHostCopy,
                                     VkDeviceSize                       transferBufferSize,
                                     void*                              sharedTransferBufferHandle,
                                     void*                              sharedFenceHandle,
                                     uint32_t                           yStride,
                                     uint64_t                           uvOffset,
                                     uint32_t                           uvStride,
                                     void**                             ppVulkanTransferBytes,
                                     const VkAllocationCallbacks*       pAllocator,
                                     VkImage*                           pImage,
                                     VkDeviceMemory*                    pMemories,
                                     uint32_t*                          pMemoryCount,
                                     VkBuffer*                          pExternalBuffer,
                                     VkDeviceMemory*                    pExternalBufferMemory,
                                     VkDeviceSize*                      pExternalBufferSize,
                                     bool_t*                            pExternalBufferHostCopy,
                                     uint32_t*                          pExternalYStride,
                                     uint32_t*                          pExternalUvOffset,
                                     uint32_t*                          pExternalUvStride,
                                     VkSemaphore*                       pExternalSemaphore)
{
    const VADRMPRIMESurfaceDescriptor* descriptor;
    VkDeviceSize                       memory_object_sizes[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    int                                memory_object_fds[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                           memory_object_count;
    uint64_t                           drm_format_modifier;
    uint32_t                           plane_index;
    uint32_t                           format_plane_count;
    uint32_t                           va_fourcc;
    uint32_t                           drm_format;

    (void)sharedImageHandle;
    (void)sharedImageHandleType;
    (void)sharedImageAllocationSize;
    (void)transferBufferHostCopy;
    (void)transferBufferSize;
    (void)sharedTransferBufferHandle;
    (void)sharedFenceHandle;
    (void)yStride;
    (void)uvOffset;
    (void)uvStride;
    (void)ppVulkanTransferBytes;
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != extent.width && 0 != extent.height && 1 == extent.depth);
    LOGASSERT((VkImageUsageFlags)0 != usage);
    LOGASSERT(NULL != pImage);
    LOGASSERT(NULL != pMemories);
    LOGASSERT(NULL != pMemoryCount);
    LOGASSERT(NULL != pExternalBuffer);
    LOGASSERT(NULL != pExternalBufferMemory);
    LOGASSERT(NULL != pExternalBufferSize);
    LOGASSERT(NULL != pExternalBufferHostCopy);
    LOGASSERT(NULL != pExternalYStride);
    LOGASSERT(NULL != pExternalUvOffset);
    LOGASSERT(NULL != pExternalUvStride);
    LOGASSERT(NULL != pExternalSemaphore);

    descriptor = (const VADRMPRIMESurfaceDescriptor*)drmPrimeDescriptor;
    LOGASSERT(NULL != descriptor);
    GetImageFormats(format, descriptor->fourcc, &va_fourcc, &drm_format);
    LOGASSERT(va_fourcc == descriptor->fourcc);
    LOGASSERT(1 == descriptor->num_layers);
    LOGASSERT(drm_format == descriptor->layers[0].drm_format);
    LOGASSERT((0 != descriptor->layers[0].num_planes) && (COUNT_OF(descriptor->layers[0].object_index) >= descriptor->layers[0].num_planes));
    LOGASSERT((extent.width == descriptor->width) && (extent.height == descriptor->height));

    GetSurfaceMemoryObjectsDrm(descriptor, &memory_object_count, memory_object_sizes, memory_object_fds, &drm_format_modifier);

    format_plane_count = Gfx_Vulkan_GetFormatPlaneCount(format);
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

    (*pImage) = VK_NULL_HANDLE;
    (void)memset(pMemories, 0, sizeof((*pMemories)) * VK_MAX_DRM_FORMAT_MODIFIER_PLANES);
    (*pMemoryCount)            = 0;
    (*pExternalBuffer)         = VK_NULL_HANDLE;
    (*pExternalBufferMemory)   = VK_NULL_HANDLE;
    (*pExternalBufferSize)     = 0;
    (*pExternalBufferHostCopy) = FALSE;
    (*pExternalYStride)        = 0;
    (*pExternalUvOffset)       = 0;
    (*pExternalUvStride)       = 0;
    (*pExternalSemaphore)      = VK_NULL_HANDLE;
    SetupImportedDmaBufImage(physicalDevice,
                             device,
                             NULL,
                             format,
                             extent,
                             usage,
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
                             pAllocator,
                             pImage,
                             VK_MAX_DRM_FORMAT_MODIFIER_PLANES,
                             pMemories,
                             pMemoryCount);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_Platform_GetInstanceExtensions(uint32_t* pExtensionCount, const char* const** pppExtensions)
{
    LOGASSERT(NULL != pExtensionCount);
    LOGASSERT(NULL != pppExtensions);

    (*pppExtensions)   = c_vkPlatformInstanceExtensions;
    (*pExtensionCount) = VK_COUNT_OF(c_vkPlatformInstanceExtensions);
}
