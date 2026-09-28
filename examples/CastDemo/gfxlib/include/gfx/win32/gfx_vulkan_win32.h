#ifndef VULKAN_VA_ZERO_COPY_GFX_VULKAN_WIN32_H
#define VULKAN_VA_ZERO_COPY_GFX_VULKAN_WIN32_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <windows.h>

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
 * \brief Allocates memory by importing a Win32 handle or name.
 *
 * \param[in]  device           Logical device importing the handle
 * \param[in]  pNext            Additional allocation extension chain, or NULL
 * \param[in]  allocationSize   Imported allocation size in bytes
 * \param[in]  memoryTypeIndex  Compatible physical-device memory-type index selected by the caller
 * \param[in]  handleType       External-memory type matching handle or name
 * \param[in]  handle           Borrowed native allocation handle, or NULL when importing by name
 * \param[in]  name             Named allocation identifier, or NULL when importing a handle
 * \param[in]  pAllocator       Optional host allocation callbacks
 * \param[out] pMemory          Receives allocated memory; binding is performed by the caller
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateImportedMemoryHandleWin32(VkDevice                           device,
                                                  const void*                        pNext,
                                                  VkDeviceSize                       allocationSize,
                                                  uint32_t                           memoryTypeIndex,
                                                  VkExternalMemoryHandleTypeFlagBits handleType,
                                                  HANDLE                             handle,
                                                  LPCWSTR                            name,
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
 * \param[in]  handle           Native handle, or NULL when importing by name
 * \param[in]  name             UTF-16 resource name, or NULL when importing a handle
 * \param[in]  pBindNext        Optional plane-binding extension chain, or NULL
 * \param[in]  memoryOffset     Aligned byte offset at which to bind the plane
 * \param[in]  pAllocator       Optional Vulkan allocation callbacks, or NULL
 * \param[out] pMemory          Receives allocated memory
 *
 * \retval    None
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
                                                         VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Exports device memory as a native Win32 handle.
 *
 * \par Details:
 *  Requires VK_KHR_external_memory_win32.
 *
 * \param[in]  device   Logical device owning the resources
 * \param[in]  pInfo    Native memory-handle export parameters
 * \param[out] pHandle  Receives the exported handle; caller owns NT handles
 *
 * \retval    None
 */
void Gfx_Vulkan_GetMemoryHandleWin32(VkDevice device, const VkMemoryGetWin32HandleInfoKHR* pInfo, HANDLE* pHandle);

/*************************************************************************************************/
/**
 * \brief Exports a Win32 handle for device memory.
 *
 * \param[in]  device      Logical device owning the resources
 * \param[in]  pNext       Optional extension chain, or NULL
 * \param[in]  memory      Exportable device-memory allocation
 * \param[in]  handleType  External-memory handle type
 * \param[out] pHandle     Receives the exported handle; caller owns NT handles
 *
 * \retval    None
 */
void Gfx_Vulkan_GetMemoryHandleInfoWin32(VkDevice device, const void* pNext, VkDeviceMemory memory, VkExternalMemoryHandleTypeFlagBits handleType, HANDLE* pHandle);

/*************************************************************************************************/
/**
 * \brief Queries memory-type compatibility for a Win32 allocation handle.
 *
 * \param[in]     device       Logical device
 * \param[in,out] pNext        Optional writable output extension chain, or NULL
 * \param[in]     handleType   External-memory handle type
 * \param[in]     handle       External allocation handle
 * \param[out]    pProperties  Receives the initialized and queried properties structure
 *
 * \retval    None
 */
void Gfx_Vulkan_GetMemoryHandlePropertiesWin32(VkDevice                           device,
                                               void*                              pNext,
                                               VkExternalMemoryHandleTypeFlagBits handleType,
                                               HANDLE                             handle,
                                               VkMemoryWin32HandlePropertiesKHR*  pProperties);

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
 * \param[in]  handle           Native allocation handle, or NULL when importing by name
 * \param[in]  name             UTF-16 allocation name, or NULL when importing a handle
 * \param[in]  memoryOffset     Aligned binding offset in the imported allocation
 * \param[in]  pAllocator       Optional allocation callbacks
 * \param[out] pMemory          Receives imported and bound memory
 *
 * \retval    None
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
                                                    VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Creates an image and imports and binds non-dedicated native storage.
 *
 * \par Details:
 *  The imported allocation must have the image's queried memory-requirement size.
 *  This helper has no explicit allocation-size input.
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
 * \param[in]  handle                 Native allocation handle whose properties can be queried
 * \param[in]  properties             Required memory properties
 * \param[in]  pAllocator             Optional allocation callbacks
 * \param[out] pImage                 Receives the created and bound image
 * \param[out] pMemory                Receives imported memory
 *
 * \retval    None
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
                                                    VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Creates and binds non-dedicated exportable buffer storage, then exports its native handle.
 *
 * \par Details:
 *  The caller must verify export support and select the dedicated variant when required.
 *
 * \param[in]  physicalDevice         Physical device supplying memory properties
 * \param[in]  device                 Logical device
 * \param[in]  pBufferNext            Buffer creation extension chain
 * \param[in]  handleType             Native external-memory type
 * \param[in]  flags                  Buffer creation flags
 * \param[in]  size                   Buffer size
 * \param[in]  usage                  Buffer usage
 * \param[in]  sharingMode            Queue-family sharing mode
 * \param[in]  queueFamilyIndexCount  Queue-family index count
 * \param[in]  pQueueFamilyIndices    Queue-family indices
 * \param[in]  pMemoryNext            Allocation extension chain
 * \param[in]  pInfoNext              Native memory export extension chain
 * \param[in]  properties             Required memory properties
 * \param[in]  pAllocator             Optional allocation callbacks
 * \param[out] pBuffer                Receives the created and bound buffer
 * \param[out] pMemory                Receives exportable memory
 * \param[out] pHandle                Receives the native exported handle
 *
 * \retval    None
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
                                                     HANDLE*                            pHandle);

/*************************************************************************************************/
/**
 * \brief Creates and binds dedicated exportable buffer storage, then exports its native handle.
 *
 * \par Details:
 *  The caller must verify export support before requesting a dedicated allocation.
 *
 * \param[in]  physicalDevice         Physical device supplying memory properties
 * \param[in]  device                 Logical device
 * \param[in]  pBufferNext            Buffer creation extension chain
 * \param[in]  handleType             Native external-memory type
 * \param[in]  flags                  Buffer creation flags
 * \param[in]  size                   Buffer size
 * \param[in]  usage                  Buffer usage
 * \param[in]  sharingMode            Queue-family sharing mode
 * \param[in]  queueFamilyIndexCount  Queue-family index count
 * \param[in]  pQueueFamilyIndices    Queue-family indices
 * \param[in]  pMemoryNext            Allocation extension chain
 * \param[in]  pInfoNext              Native memory export extension chain
 * \param[in]  properties             Required memory properties
 * \param[in]  pAllocator             Optional allocation callbacks
 * \param[out] pBuffer                Receives the created and bound buffer
 * \param[out] pMemory                Receives exportable dedicated memory
 * \param[out] pHandle                Receives the native exported handle
 *
 * \retval    None
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
                                                              HANDLE*                            pHandle);

#endif /* VULKAN_VA_ZERO_COPY_GFX_VULKAN_WIN32_H */
