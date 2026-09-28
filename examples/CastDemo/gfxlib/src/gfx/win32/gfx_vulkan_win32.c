/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_assert.h"
#include "gfx/win32/gfx_vulkan_win32.h"

#include <string.h>

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
void Gfx_Vulkan_AllocateImportedMemoryHandleWin32(VkDevice                           device,
                                                  const void*                        pNext,
                                                  VkDeviceSize                       allocationSize,
                                                  uint32_t                           memoryTypeIndex,
                                                  VkExternalMemoryHandleTypeFlagBits handleType,
                                                  HANDLE                             handle,
                                                  LPCWSTR                            name,
                                                  const VkAllocationCallbacks*       pAllocator,
                                                  VkDeviceMemory*                    pMemory)
{
    VkImportMemoryWin32HandleInfoKHR import_info;

    LOGASSERT((NULL != handle) != (NULL != name));
    import_info.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    import_info.pNext      = pNext;
    import_info.handleType = handleType;
    import_info.handle     = handle;
    import_info.name       = name;
    Gfx_Vulkan_AllocateMemory(device, &import_info, allocationSize, memoryTypeIndex, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetMemoryHandlePropertiesWin32(VkDevice                           device,
                                               void*                              pNext,
                                               VkExternalMemoryHandleTypeFlagBits handleType,
                                               HANDLE                             handle,
                                               VkMemoryWin32HandlePropertiesKHR*  pProperties)
{
    VkMemoryWin32HandlePropertiesKHR properties;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != handle);
    LOGASSERT(NULL != pProperties);

    properties.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
    properties.pNext = pNext;
    LOGASSERT(NULL != vkGetMemoryWin32HandlePropertiesKHR);
    VKCHECK(vkGetMemoryWin32HandlePropertiesKHR(device, handleType, handle, &properties));

    (*pProperties) = properties;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupImportedImagePlaneMemoryHandleWin32(VkDevice                           device,
                                                         const void*                        pNext,
                                                         VkImage                            image,
                                                         VkImageAspectFlagBits              planeAspect,
                                                         VkDeviceSize                       allocationSize,
                                                         uint32_t                           memoryTypeIndex,
                                                         VkExternalMemoryHandleTypeFlagBits handleType,
                                                         HANDLE                             handle,
                                                         LPCWSTR                            name,
                                                         const void*                        pBindNext,
                                                         VkDeviceSize                       memoryOffset,
                                                         const VkAllocationCallbacks*       pAllocator,
                                                         VkDeviceMemory*                    pMemory)
{
    Gfx_Vulkan_AllocateImportedMemoryHandleWin32(device, pNext, allocationSize, memoryTypeIndex, handleType, handle, name, pAllocator, pMemory);
    Gfx_Vulkan_BindImagePlaneMemories(device, &pBindNext, image, 1, &planeAspect, pMemory, &memoryOffset);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetMemoryHandleWin32(VkDevice device, const VkMemoryGetWin32HandleInfoKHR* pInfo, HANDLE* pHandle)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != pInfo);
    LOGASSERT(NULL != pHandle);

    LOGASSERT(NULL != vkGetMemoryWin32HandleKHR);
    VKCHECK(vkGetMemoryWin32HandleKHR(device, pInfo, pHandle));
    LOGASSERT(NULL != (*pHandle));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetMemoryHandleInfoWin32(VkDevice device, const void* pNext, VkDeviceMemory memory, VkExternalMemoryHandleTypeFlagBits handleType, HANDLE* pHandle)
{
    VkMemoryGetWin32HandleInfoKHR info;

    LOGASSERT(VK_NULL_HANDLE != memory);

    info.sType      = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR;
    info.pNext      = pNext;
    info.memory     = memory;
    info.handleType = handleType;
    Gfx_Vulkan_GetMemoryHandleWin32(device, &info, pHandle);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupImportedImageMemoryHandleWin32(VkDevice                           device,
                                                    const void*                        pNext,
                                                    VkImage                            image,
                                                    VkDeviceSize                       allocationSize,
                                                    uint32_t                           memoryTypeIndex,
                                                    VkExternalMemoryHandleTypeFlagBits handleType,
                                                    HANDLE                             handle,
                                                    LPCWSTR                            name,
                                                    VkDeviceSize                       memoryOffset,
                                                    const VkAllocationCallbacks*       pAllocator,
                                                    VkDeviceMemory*                    pMemory)
{
    Gfx_Vulkan_AllocateImportedMemoryHandleWin32(device, pNext, allocationSize, memoryTypeIndex, handleType, handle, name, pAllocator, pMemory);
    Gfx_Vulkan_BindImageMemory(device, image, (*pMemory), memoryOffset);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupImportedMemoryImageHandleWin32(VkPhysicalDevice                   physicalDevice,
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
                                                    HANDLE                             handle,
                                                    VkMemoryPropertyFlags              properties,
                                                    const VkAllocationCallbacks*       pAllocator,
                                                    VkImage*                           pImage,
                                                    VkDeviceMemory*                    pMemory)
{
    VkMemoryWin32HandlePropertiesKHR memory_properties;
    VkDeviceSize                     allocation_size;
    uint32_t                         memory_type_index;

    Gfx_Vulkan_GetMemoryHandlePropertiesWin32(device, NULL, handleType, handle, &memory_properties);
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
    Gfx_Vulkan_GetImageMemoryAllocationInfo(physicalDevice, device, (*pImage), memory_properties.memoryTypeBits, properties, &allocation_size, &memory_type_index);
    Gfx_Vulkan_SetupImportedImageMemoryHandleWin32(device, pMemoryNext, (*pImage), allocation_size, memory_type_index, handleType, handle, NULL, VK_NO_OFFSET, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupExportedMemoryBufferHandleWin32(VkPhysicalDevice                   physicalDevice,
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
                                                     const void*                        pInfoNext,
                                                     VkMemoryPropertyFlags              properties,
                                                     const VkAllocationCallbacks*       pAllocator,
                                                     VkBuffer*                          pBuffer,
                                                     VkDeviceMemory*                    pMemory,
                                                     HANDLE*                            pHandle)
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
    Gfx_Vulkan_GetMemoryHandleInfoWin32(device, pInfoNext, (*pMemory), handleType, pHandle);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupExportedDedicatedMemoryBufferHandleWin32(VkPhysicalDevice                   physicalDevice,
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
                                                              const void*                        pInfoNext,
                                                              VkMemoryPropertyFlags              properties,
                                                              const VkAllocationCallbacks*       pAllocator,
                                                              VkBuffer*                          pBuffer,
                                                              VkDeviceMemory*                    pMemory,
                                                              HANDLE*                            pHandle)
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
    Gfx_Vulkan_GetMemoryHandleInfoWin32(device, pInfoNext, (*pMemory), handleType, pHandle);
}
