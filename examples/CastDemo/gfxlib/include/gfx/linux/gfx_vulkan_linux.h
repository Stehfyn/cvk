#ifndef VULKAN_VA_ZERO_COPY_GFX_VULKAN_LINUX_H
#define VULKAN_VA_ZERO_COPY_GFX_VULKAN_LINUX_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <X11/Xlib.h>

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
void Gfx_Vulkan_GetMemoryPropertiesInfoFd(VkDevice device, void* pNext, VkExternalMemoryHandleTypeFlagBits handleType, int fd, uint32_t* pMemoryTypeBits);

/*************************************************************************************************/
/**
 * \brief Allocates memory by importing the supplied file descriptor.
 *
 * \param[in]  device           Logical device importing the descriptor
 * \param[in]  pNext            Optional allocation extension chain
 * \param[in]  allocationSize   Imported memory-object size in bytes
 * \param[in]  memoryTypeIndex  Compatible physical-device memory-type index selected by the caller
 * \param[in]  handleType       External-memory FD handle type
 * \param[in]  fd               Descriptor transferred to Vulkan on successful allocation; not duplicated here
 * \param[in]  pAllocator       Optional host allocation callbacks
 * \param[out] pMemory          Imported memory handle
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateImportedMemoryFd(VkDevice                           device,
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
 * \par Details:
 *  Composes import metadata and delegates to Gfx_Vulkan_AllocateDedicatedImageMemory.
 *
 * \param[in]  device           Logical device importing the descriptor
 * \param[in]  pNext            Optional tail chained after dedicated-allocation metadata
 * \param[in]  image            Image receiving the dedicated allocation
 * \param[in]  allocationSize   Imported memory-object size in bytes
 * \param[in]  memoryTypeIndex  Compatible physical-device memory-type index selected by the caller
 * \param[in]  handleType       External-memory FD handle type
 * \param[in]  fd               Descriptor transferred to Vulkan on successful allocation; not duplicated here
 * \param[in]  pAllocator       Optional host allocation callbacks
 * \param[out] pMemory          Imported dedicated memory handle
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateImportedDedicatedImageMemoryFd(VkDevice                           device,
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
 * \par Details:
 *  Binds at offset zero. allocationSize and memoryTypeIndex must be compatible with the image and imported allocation.
 *
 * \param[in]  device           Logical device owning the resources
 * \param[in]  pNext            Optional extension chain, or NULL
 * \param[in]  image            Image receiving the dedicated allocation
 * \param[in]  allocationSize   Allocation size supplied by the caller
 * \param[in]  memoryTypeIndex  Compatible memory-type index selected by the caller
 * \param[in]  handleType       External-memory handle type
 * \param[in]  fd               Descriptor transferred to Vulkan on success; not duplicated here
 * \param[in]  pAllocator       Optional Vulkan allocation callbacks, or NULL
 * \param[out] pMemory          Receives allocated memory
 *
 * \retval    None
 */
void Gfx_Vulkan_SetupImportedDedicatedImageMemoryFd(VkDevice                           device,
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
 * \brief Allocates imported memory and binds one image plane using explicit allocation inputs.
 *
 * \param[in]  device           Logical device owning the resources
 * \param[in]  pNext            Optional extension chain, or NULL
 * \param[in]  image            Disjoint image receiving the plane binding
 * \param[in]  planeAspect      Image or DRM memory-plane aspect to bind
 * \param[in]  allocationSize   Allocation size supplied by the caller
 * \param[in]  memoryTypeIndex  Compatible memory-type index selected by the caller
 * \param[in]  handleType       External-memory handle type
 * \param[in]  fd               Descriptor transferred to Vulkan on success; not duplicated here
 * \param[in]  pBindNext        Optional plane-binding extension chain, or NULL
 * \param[in]  memoryOffset     Aligned byte offset at which to bind the plane
 * \param[in]  pAllocator       Optional Vulkan allocation callbacks, or NULL
 * \param[out] pMemory          Receives allocated memory
 *
 * \retval    None
 */
void Gfx_Vulkan_SetupImportedImagePlaneMemoryFd(VkDevice                           device,
                                                const void*                        pNext,
                                                VkImage                            image,
                                                VkImageAspectFlagBits              planeAspect,
                                                VkDeviceSize                       allocationSize,
                                                uint32_t                           memoryTypeIndex,
                                                VkExternalMemoryHandleTypeFlagBits handleType,
                                                int                                fd,
                                                const void*                        pBindNext,
                                                VkDeviceSize                       memoryOffset,
                                                const VkAllocationCallbacks*       pAllocator,
                                                VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Returns the DRM format modifier selected for a created Vulkan image.
 *
 * \param[in]     device             Logical device owning image
 * \param[in,out] pNext              Query extension chain, or NULL
 * \param[in]     image              Image created with DRM format-modifier tiling
 * \param[out]    pDrmFormatModifier Receives the selected modifier value
 *
 * \retval    None
 */
void Gfx_Vulkan_GetImageDrmFormatModifierPropertiesInfo(VkDevice device, void* pNext, VkImage image, uint64_t* pDrmFormatModifier);

/*************************************************************************************************/
/**
 * \brief Allocates dedicated buffer memory by importing a native allocation.
 *
 * \param[in]  physicalDevice  Physical device supplying memory properties
 * \param[in]  device          Logical device owning the resources
 * \param[in]  pNext           Optional extension chain, or NULL
 * \param[in]  buffer          Buffer receiving the dedicated allocation
 * \param[in]  memoryTypeBits  Compatible native allocation memory-type mask
 * \param[in]  handleType      External-memory handle type
 * \param[in]  fd              Descriptor transferred to Vulkan on success; not duplicated here
 * \param[in]  properties      Required memory properties
 * \param[in]  pAllocator      Optional Vulkan allocation callbacks, or NULL
 * \param[out] pMemory         Receives allocated memory
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateImportedDedicatedBufferMemoryFd(VkPhysicalDevice                   physicalDevice,
                                                        VkDevice                           device,
                                                        const void*                        pNext,
                                                        VkBuffer                           buffer,
                                                        uint32_t                           memoryTypeBits,
                                                        VkExternalMemoryHandleTypeFlagBits handleType,
                                                        int                                fd,
                                                        VkMemoryPropertyFlags              properties,
                                                        const VkAllocationCallbacks*       pAllocator,
                                                        VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Creates a buffer, imports dedicated storage, and binds it using native allocation properties.
 *
 * \param[in]  physicalDevice         Physical device supplying memory properties
 * \param[in]  device                 Logical device owning the resources
 * \param[in]  pBufferNext            Optional buffer creation extension chain, or NULL
 * \param[in]  handleType             External-memory handle type
 * \param[in]  flags                  Buffer creation flags
 * \param[in]  size                   Buffer size in bytes
 * \param[in]  usage                  Buffer usage flags
 * \param[in]  sharingMode            Buffer queue-family sharing mode
 * \param[in]  queueFamilyIndexCount  Number of queue-family indices
 * \param[in]  pQueueFamilyIndices    Queue-family indices for concurrent sharing
 * \param[in]  pMemoryNext            Optional allocation extension chain, or NULL
 * \param[in]  fd                     Descriptor transferred to Vulkan on success; not duplicated here
 * \param[in]  properties             Required memory properties
 * \param[in]  pAllocator             Optional Vulkan allocation callbacks, or NULL
 * \param[out] pBuffer                Receives the created and bound buffer
 * \param[out] pMemory                Receives allocated memory
 *
 * \retval    None
 */
void Gfx_Vulkan_SetupImportedMemoryBufferFd(VkPhysicalDevice                   physicalDevice,
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
                                            int                                fd,
                                            VkMemoryPropertyFlags              properties,
                                            const VkAllocationCallbacks*       pAllocator,
                                            VkBuffer*                          pBuffer,
                                            VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Imports a file descriptor into an existing semaphore.
 *
 * \par Details:
 *  Requires VK_KHR_external_semaphore_fd. Forwards the descriptor without duplicating it.
 *
 * \param[in]  device      Logical device owning the resources
 * \param[in]  pNext       Optional extension chain, or NULL
 * \param[in]  semaphore   Existing semaphore receiving the payload
 * \param[in]  flags       Semaphore import flags
 * \param[in]  handleType  External-semaphore handle type
 * \param[in]  fd          Descriptor consumed on success; -1 is permitted for SYNC_FD
 *
 * \retval    None
 */
void Gfx_Vulkan_ImportSemaphoreInfoFd(VkDevice device, const void* pNext, VkSemaphore semaphore, VkSemaphoreImportFlags flags, VkExternalSemaphoreHandleTypeFlagBits handleType, int fd);

/*************************************************************************************************/
/**
 * \brief Creates a semaphore and imports a file-descriptor payload into it.
 *
 * \param[in]  device          Logical device owning the resources
 * \param[in]  pSemaphoreNext  Optional semaphore creation extension chain, or NULL
 * \param[in]  semaphoreFlags  Semaphore creation flags
 * \param[in]  pImportNext     Optional semaphore import extension chain, or NULL
 * \param[in]  importFlags     Semaphore import flags
 * \param[in]  handleType      External-semaphore handle type
 * \param[in]  fd              Descriptor consumed on success; -1 is permitted for SYNC_FD
 * \param[in]  pAllocator      Optional Vulkan allocation callbacks, or NULL
 * \param[out] pSemaphore      Receives the created semaphore with imported payload
 *
 * \retval    None
 */
void Gfx_Vulkan_SetupImportedSemaphoreFd(VkDevice                              device,
                                         const void*                           pSemaphoreNext,
                                         VkSemaphoreCreateFlags                semaphoreFlags,
                                         const void*                           pImportNext,
                                         VkSemaphoreImportFlags                importFlags,
                                         VkExternalSemaphoreHandleTypeFlagBits handleType,
                                         int                                   fd,
                                         const VkAllocationCallbacks*          pAllocator,
                                         VkSemaphore*                          pSemaphore);

/*************************************************************************************************/
/**
 * \brief Queries Vulkan memory-type compatibility for an external allocation's file descriptor.
 *
 * \param[in]  device       Logical device
 * \param[in]  handleType   External-memory handle type
 * \param[in]  fd           External allocation descriptor
 * \param[out] pProperties  Initialized properties structure receiving the result
 *
 * \retval    None
 */
void Gfx_Vulkan_GetMemoryPropertiesFd(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pProperties);

/*************************************************************************************************/
/**
 * \brief Returns the DRM format modifier describing an image's memory layout.
 *
 * \param[in]  device       Logical device
 * \param[in]  image        Image whose modifier is queried
 * \param[out] pProperties  Initialized properties structure receiving the result
 *
 * \retval    None
 */
void Gfx_Vulkan_GetImageDrmFormatModifierProperties(VkDevice device, VkImage image, VkImageDrmFormatModifierPropertiesEXT* pProperties);

/*************************************************************************************************/
/**
 * \brief Imports a file descriptor's synchronization payload into a semaphore.
 *
 * \param[in]  device  Logical device
 * \param[in]  pInfo   Semaphore import parameters
 *
 * \retval    None
 */
void Gfx_Vulkan_ImportSemaphoreFd(VkDevice device, const VkImportSemaphoreFdInfoKHR* pInfo);

/*************************************************************************************************/
/**
 * \brief Allocates imported image memory and binds it.
 *
 * \param[in]  device           Logical device
 * \param[in]  pNext            Optional allocation extension chain
 * \param[in]  image            Resource receiving imported storage
 * \param[in]  allocationSize   Imported allocation size
 * \param[in]  memoryTypeIndex  Caller-selected compatible memory-type index
 * \param[in]  handleType       External-memory handle type
 * \param[in]  fd               Descriptor consumed by Vulkan on successful import
 * \param[in]  memoryOffset     Aligned binding offset in the imported allocation
 * \param[in]  pAllocator       Optional allocation callbacks
 * \param[out] pMemory          Receives imported and bound memory
 *
 * \retval    None
 */
void Gfx_Vulkan_SetupImportedImageMemoryFd(VkDevice                           device,
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
 * \brief Allocates imported dedicated buffer memory and binds it.
 *
 * \param[in]  physicalDevice  Physical device supplying memory properties
 * \param[in]  device          Logical device
 * \param[in]  pNext           Optional allocation extension chain
 * \param[in]  buffer          Resource receiving imported storage
 * \param[in]  memoryTypeBits  Compatible native allocation memory-type mask
 * \param[in]  handleType      External-memory handle type
 * \param[in]  fd              Descriptor consumed by Vulkan on successful import
 * \param[in]  properties      Required memory properties
 * \param[in]  pAllocator      Optional allocation callbacks
 * \param[out] pMemory         Receives imported and bound memory
 *
 * \retval    None
 */
void Gfx_Vulkan_SetupImportedDedicatedBufferMemoryFd(VkPhysicalDevice                   physicalDevice,
                                                     VkDevice                           device,
                                                     const void*                        pNext,
                                                     VkBuffer                           buffer,
                                                     uint32_t                           memoryTypeBits,
                                                     VkExternalMemoryHandleTypeFlagBits handleType,
                                                     int                                fd,
                                                     VkMemoryPropertyFlags              properties,
                                                     const VkAllocationCallbacks*       pAllocator,
                                                     VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Creates an image and imports and binds non-dedicated native storage.
 *
 * \par Details:
 *  Allocation size and memory type are derived from the image and file-descriptor properties.
 *
 * \param[in]  physicalDevice         Physical device supplying memory properties
 * \param[in]  device                 Logical device
 * \param[in]  pImageNext             Image creation extension chain
 * \param[in]  handleType             Native external-memory type
 * \param[in]  flags                  Image creation flags
 * \param[in]  imageType              Image dimensionality
 * \param[in]  format                 Image format
 * \param[in]  extent                 Image dimensions
 * \param[in]  mipLevels              Mip-level count
 * \param[in]  arrayLayers            Array-layer count
 * \param[in]  samples                Image sample count
 * \param[in]  tiling                 Image tiling
 * \param[in]  usage                  Image usage
 * \param[in]  sharingMode            Queue-family sharing mode
 * \param[in]  queueFamilyIndexCount  Queue-family index count
 * \param[in]  pQueueFamilyIndices    Queue-family indices
 * \param[in]  initialLayout          Initial image layout
 * \param[in]  pMemoryNext            Allocation extension chain
 * \param[in]  fd                     Descriptor consumed on successful import; duplicate beforehand if borrowed
 * \param[in]  properties             Required memory properties
 * \param[in]  pAllocator             Optional allocation callbacks
 * \param[out] pImage                 Receives the created and bound image
 * \param[out] pMemory                Receives imported memory
 *
 * \retval    None
 */
void Gfx_Vulkan_SetupImportedMemoryImageFd(VkPhysicalDevice                   physicalDevice,
                                           VkDevice                           device,
                                           const void*                        pImageNext,
                                           VkExternalMemoryHandleTypeFlagBits handleType,
                                           VkImageCreateFlags                 flags,
                                           VkImageType                        imageType,
                                           VkFormat                           format,
                                           VkExtent3D                         extent,
                                           uint32_t                           mipLevels,
                                           uint32_t                           arrayLayers,
                                           VkSampleCountFlagBits              samples,
                                           VkImageTiling                      tiling,
                                           VkImageUsageFlags                  usage,
                                           VkSharingMode                      sharingMode,
                                           uint32_t                           queueFamilyIndexCount,
                                           const uint32_t*                    pQueueFamilyIndices,
                                           VkImageLayout                      initialLayout,
                                           const void*                        pMemoryNext,
                                           int                                fd,
                                           VkMemoryPropertyFlags              properties,
                                           const VkAllocationCallbacks*       pAllocator,
                                           VkImage*                           pImage,
                                           VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Creates an image and imports and binds dedicated native storage.
 *
 * \par Details:
 *  Allocation size and memory type are derived from the image and file-descriptor properties.
 *
 * \param[in]  physicalDevice         Physical device supplying memory properties
 * \param[in]  device                 Logical device
 * \param[in]  pImageNext             Image creation extension chain
 * \param[in]  handleType             Native external-memory type
 * \param[in]  flags                  Image creation flags
 * \param[in]  imageType              Image dimensionality
 * \param[in]  format                 Image format
 * \param[in]  extent                 Image dimensions
 * \param[in]  mipLevels              Mip-level count
 * \param[in]  arrayLayers            Array-layer count
 * \param[in]  samples                Image sample count
 * \param[in]  tiling                 Image tiling
 * \param[in]  usage                  Image usage
 * \param[in]  sharingMode            Queue-family sharing mode
 * \param[in]  queueFamilyIndexCount  Queue-family index count
 * \param[in]  pQueueFamilyIndices    Queue-family indices
 * \param[in]  initialLayout          Initial image layout
 * \param[in]  pMemoryNext            Allocation extension chain
 * \param[in]  fd                     Descriptor consumed on successful import; duplicate beforehand if borrowed
 * \param[in]  properties             Required memory properties
 * \param[in]  pAllocator             Optional allocation callbacks
 * \param[out] pImage                 Receives the created and bound image
 * \param[out] pMemory                Receives imported memory
 *
 * \retval    None
 */
void Gfx_Vulkan_SetupImportedDedicatedMemoryImageFd(VkPhysicalDevice                   physicalDevice,
                                                    VkDevice                           device,
                                                    const void*                        pImageNext,
                                                    VkExternalMemoryHandleTypeFlagBits handleType,
                                                    VkImageCreateFlags                 flags,
                                                    VkImageType                        imageType,
                                                    VkFormat                           format,
                                                    VkExtent3D                         extent,
                                                    uint32_t                           mipLevels,
                                                    uint32_t                           arrayLayers,
                                                    VkSampleCountFlagBits              samples,
                                                    VkImageTiling                      tiling,
                                                    VkImageUsageFlags                  usage,
                                                    VkSharingMode                      sharingMode,
                                                    uint32_t                           queueFamilyIndexCount,
                                                    const uint32_t*                    pQueueFamilyIndices,
                                                    VkImageLayout                      initialLayout,
                                                    const void*                        pMemoryNext,
                                                    int                                fd,
                                                    VkMemoryPropertyFlags              properties,
                                                    const VkAllocationCallbacks*       pAllocator,
                                                    VkImage*                           pImage,
                                                    VkDeviceMemory*                    pMemory);

#endif /* VULKAN_VA_ZERO_COPY_GFX_VULKAN_LINUX_H */
