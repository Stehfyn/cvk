/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_assert.h"
#include "gfx/linux/gfx_vulkan_linux.h"

#include <string.h>
#include <unistd.h>

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

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetMemoryPropertiesFd(VkDevice device, VkExternalMemoryHandleTypeFlagBits handleType, int fd, VkMemoryFdPropertiesKHR* pProperties)
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
void Gfx_Vulkan_GetMemoryPropertiesInfoFd(VkDevice device, void* pNext, VkExternalMemoryHandleTypeFlagBits handleType, int fd, uint32_t* pMemoryTypeBits)
{
    VkMemoryFdPropertiesKHR properties;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 <= fd);
    LOGASSERT(NULL != pMemoryTypeBits);

    (void)memset(&properties, 0, sizeof(properties));
    properties.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR;
    properties.pNext = pNext;
    Gfx_Vulkan_GetMemoryPropertiesFd(device, handleType, fd, &properties);

    (*pMemoryTypeBits) = properties.memoryTypeBits;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateImportedMemoryFd(VkDevice                           device,
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
void Gfx_Vulkan_AllocateImportedDedicatedImageMemoryFd(VkDevice                           device,
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
 *  Binds at offset zero. allocationSize and memoryTypeIndex must be compatible with the image and imported allocation.
 */
void Gfx_Vulkan_SetupImportedDedicatedImageMemoryFd(VkDevice                           device,
                                                    const void*                        pNext,
                                                    VkImage                            image,
                                                    VkDeviceSize                       allocationSize,
                                                    uint32_t                           memoryTypeIndex,
                                                    VkExternalMemoryHandleTypeFlagBits handleType,
                                                    int                                fd,
                                                    const VkAllocationCallbacks*       pAllocator,
                                                    VkDeviceMemory*                    pMemory)
{
    Gfx_Vulkan_AllocateImportedDedicatedImageMemoryFd(device, pNext, image, allocationSize, memoryTypeIndex, handleType, fd, pAllocator, pMemory);
    Gfx_Vulkan_BindImageMemory(device, image, (*pMemory), VK_NO_OFFSET);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
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
                                                VkDeviceMemory*                    pMemory)
{
    Gfx_Vulkan_AllocateImportedMemoryFd(device, pNext, allocationSize, memoryTypeIndex, handleType, fd, pAllocator, pMemory);
    Gfx_Vulkan_BindImagePlaneMemories(device, &pBindNext, image, 1, &planeAspect, pMemory, &memoryOffset);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetImageDrmFormatModifierProperties(VkDevice device, VkImage image, VkImageDrmFormatModifierPropertiesEXT* pProperties)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != pProperties);

    LOGASSERT(NULL != vkGetImageDrmFormatModifierPropertiesEXT);
    VKCHECK(vkGetImageDrmFormatModifierPropertiesEXT(device, image, pProperties));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetImageDrmFormatModifierPropertiesInfo(VkDevice device, void* pNext, VkImage image, uint64_t* pDrmFormatModifier)
{
    VkImageDrmFormatModifierPropertiesEXT properties;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT(NULL != pDrmFormatModifier);

    (void)memset(&properties, 0, sizeof(properties));
    properties.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_PROPERTIES_EXT;
    properties.pNext = pNext;
    Gfx_Vulkan_GetImageDrmFormatModifierProperties(device, image, &properties);

    (*pDrmFormatModifier) = properties.drmFormatModifier;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
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
                                                        VkDeviceMemory*                    pMemory)
{
    VkImportMemoryFdInfoKHR import_info;

    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT(0 <= fd);

    import_info.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR;
    import_info.pNext      = pNext;
    import_info.handleType = handleType;
    import_info.fd         = fd;
    Gfx_Vulkan_AllocateDedicatedExternalBufferMemory(physicalDevice, device, &import_info, buffer, memoryTypeBits, properties, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
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
                                            VkDeviceMemory*                    pMemory)
{
    uint32_t memory_type_bits;

    Gfx_Vulkan_GetMemoryPropertiesInfoFd(device, NULL, handleType, fd, &memory_type_bits);
    Gfx_Vulkan_CreateExternalMemoryBuffer(device,
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
    Gfx_Vulkan_SetupImportedDedicatedBufferMemoryFd(physicalDevice, device, pMemoryNext, (*pBuffer), memory_type_bits, handleType, fd, properties, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_ImportSemaphoreFd(VkDevice device, const VkImportSemaphoreFdInfoKHR* pInfo)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != pInfo);

    LOGASSERT(NULL != vkImportSemaphoreFdKHR);
    VKCHECK(vkImportSemaphoreFdKHR(device, pInfo));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Requires VK_KHR_external_semaphore_fd. Forwards the descriptor without duplicating it.
 */
void Gfx_Vulkan_ImportSemaphoreInfoFd(VkDevice device, const void* pNext, VkSemaphore semaphore, VkSemaphoreImportFlags flags, VkExternalSemaphoreHandleTypeFlagBits handleType, int fd)
{
    VkImportSemaphoreFdInfoKHR import_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != semaphore);
    LOGASSERT((0 <= fd) || ((-1 == fd) && (VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT == handleType)));

    import_info.sType      = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR;
    import_info.pNext      = pNext;
    import_info.semaphore  = semaphore;
    import_info.flags      = flags;
    import_info.handleType = handleType;
    import_info.fd         = fd;
    Gfx_Vulkan_ImportSemaphoreFd(device, &import_info);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupImportedSemaphoreFd(VkDevice                              device,
                                         const void*                           pSemaphoreNext,
                                         VkSemaphoreCreateFlags                semaphoreFlags,
                                         const void*                           pImportNext,
                                         VkSemaphoreImportFlags                importFlags,
                                         VkExternalSemaphoreHandleTypeFlagBits handleType,
                                         int                                   fd,
                                         const VkAllocationCallbacks*          pAllocator,
                                         VkSemaphore*                          pSemaphore)
{
    Gfx_Vulkan_CreateSemaphore(device, pSemaphoreNext, semaphoreFlags, pAllocator, pSemaphore);
    Gfx_Vulkan_ImportSemaphoreInfoFd(device, pImportNext, (*pSemaphore), importFlags, handleType, fd);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
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
                                           VkDeviceMemory*                    pMemory)
{
    Gfx_Vulkan_AllocateImportedMemoryFd(device, pNext, allocationSize, memoryTypeIndex, handleType, fd, pAllocator, pMemory);
    Gfx_Vulkan_BindImageMemory(device, image, (*pMemory), memoryOffset);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
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
                                                     VkDeviceMemory*                    pMemory)
{
    Gfx_Vulkan_AllocateImportedDedicatedBufferMemoryFd(physicalDevice, device, pNext, buffer, memoryTypeBits, handleType, fd, properties, pAllocator, pMemory);
    Gfx_Vulkan_BindBufferMemory(device, buffer, (*pMemory), VK_NO_OFFSET);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
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
                                           VkDeviceMemory*                    pMemory)
{
    uint32_t     memory_type_bits;
    uint32_t     memory_type_index;
    VkDeviceSize allocation_size;

    Gfx_Vulkan_GetMemoryPropertiesInfoFd(device, NULL, handleType, fd, &memory_type_bits);
    Gfx_Vulkan_CreateExternalImage(device,
                                   pImageNext,
                                   (VkExternalMemoryHandleTypeFlags)handleType,
                                   flags,
                                   imageType,
                                   format,
                                   extent,
                                   mipLevels,
                                   arrayLayers,
                                   samples,
                                   tiling,
                                   usage,
                                   sharingMode,
                                   queueFamilyIndexCount,
                                   pQueueFamilyIndices,
                                   initialLayout,
                                   pAllocator,
                                   pImage);
    Gfx_Vulkan_GetImageMemoryAllocationInfo(physicalDevice, device, (*pImage), memory_type_bits, properties, &allocation_size, &memory_type_index);
    Gfx_Vulkan_SetupImportedImageMemoryFd(device, pMemoryNext, (*pImage), allocation_size, memory_type_index, handleType, fd, VK_NO_OFFSET, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
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
                                                    VkDeviceMemory*                    pMemory)
{
    uint32_t     memory_type_bits;
    uint32_t     memory_type_index;
    VkDeviceSize allocation_size;

    Gfx_Vulkan_GetMemoryPropertiesInfoFd(device, NULL, handleType, fd, &memory_type_bits);
    Gfx_Vulkan_CreateExternalImage(device,
                                   pImageNext,
                                   (VkExternalMemoryHandleTypeFlags)handleType,
                                   flags,
                                   imageType,
                                   format,
                                   extent,
                                   mipLevels,
                                   arrayLayers,
                                   samples,
                                   tiling,
                                   usage,
                                   sharingMode,
                                   queueFamilyIndexCount,
                                   pQueueFamilyIndices,
                                   initialLayout,
                                   pAllocator,
                                   pImage);
    Gfx_Vulkan_GetImageMemoryAllocationInfo(physicalDevice, device, (*pImage), memory_type_bits, properties, &allocation_size, &memory_type_index);
    Gfx_Vulkan_SetupImportedDedicatedImageMemoryFd(device, pMemoryNext, (*pImage), allocation_size, memory_type_index, handleType, fd, pAllocator, pMemory);
}
