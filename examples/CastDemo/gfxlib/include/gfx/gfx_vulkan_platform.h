#ifndef VULKAN_VA_ZERO_COPY_GFX_VULKAN_PLATFORM_H
#define VULKAN_VA_ZERO_COPY_GFX_VULKAN_PLATFORM_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_vulkan.h"
#include "os/os_types.h"

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
 * \brief Validates platform adapter identity, sets up its Vulkan logical device, and returns its queue.
 *
 * \par Details:
 *  adapterId must match the Windows LUID or the packed Linux PCI domain, bus, device, and
 *  function. Creates one queue in the requested family.
 *
 * \param[in]  adapterId                 Platform adapter identity to match
 * \param[in]  physicalDevice            Vulkan physical device expected to represent adapterId
 * \param[in]  pNext                     Optional logical-device extension chain
 * \param[in]  flags                     Logical-device creation flags
 * \param[in]  pQueueNext                Optional queue extension chain
 * \param[in]  queueFlags                Queue creation flags
 * \param[in]  queueFamilyIndex          Queue family selected by the caller
 * \param[in]  queuePriority             Priority assigned to the device queue, from 0.0 through 1.0
 * \param[in]  enabledLayerCount         Number of enabled device layer names
 * \param[in]  ppEnabledLayerNames       Enabled device layer names
 * \param[in]  requestedExtensionCount   Number of requested device extension names
 * \param[in]  ppRequestedExtensionNames Requested device extension names
 * \param[in]  pEnabledFeatures          Optional core physical-device features
 * \param[in]  pAllocator                Optional Vulkan allocation callbacks
 * \param[out] pDevice                   Receives the created logical device
 * \param[out] pQueue                    Receives queue zero from queueFamilyIndex
 *
 * \retval    None
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
                                          VkQueue*                        pQueue);

/*************************************************************************************************/
/**
 * \brief Allocates image storage and exports its native sharing descriptor and plane layout.
 *
 * \par Details:
 *  Linux creates a device-local storage buffer with a format-derived linear plane layout and
 *  exports one DMA-BUF file descriptor in a single-layer VADRMPRIMESurfaceDescriptor. Supported
 *  representations include R8/R16, RGBA/BGRA8, packed YUY2/UYVY, NV12/P208, P010/P012/P016,
 *  and planar 8-bit YUV 420/422/444. Formats without a matching VA/DRM representation are fatal.
 *  All plane offsets and pitches are returned in the descriptor; the buffer-layout outputs
 *  describe its first two planes (if present). The result uses externalBuffer; image and memoryCount remain zero. The caller owns both the exported file
 *  descriptor and the returned Vulkan resources. Windows export is unsupported and terminates
 *  through LOGFATAL.
 *
 * \param[in]  physicalDevice          Physical device used to select exportable memory
 * \param[in]  device                  Logical device owning the exported buffer and memory
 * \param[in]  format                  Vulkan format with a supported VA/DRM representation
 * \param[in]  extent                  Nonzero width and height in pixels; depth must be one
 * \param[in]  usage                   Required nonzero image usage flags; the buffer uses VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
 * \param[out] drmPrimeDescriptor      Non-NULL VADRMPRIMESurfaceDescriptor storage receiving the DMA-BUF layout
 * \param[in]  pAllocator              Optional Vulkan allocation callbacks, or NULL
 * \param[out] pImage                  Receives the native image, or VK_NULL_HANDLE for buffer storage
 * \param[out] pMemories               Receives image allocations; requires VK_MAX_DRM_FORMAT_MODIFIER_PLANES entries
 * \param[out] pMemoryCount            Receives the number of image memory allocations
 * \param[out] pExternalBuffer         Receives the external transfer buffer
 * \param[out] pExternalBufferMemory   Receives the external transfer buffer allocation
 * \param[out] pExternalBufferSize     Receives the external transfer buffer size in bytes
 * \param[out] pExternalBufferHostCopy Receives whether the transfer buffer uses a host copy
 * \param[out] pExternalYStride        Receives the luma row stride in bytes
 * \param[out] pExternalUvOffset       Receives the chroma plane offset in bytes
 * \param[out] pExternalUvStride       Receives the chroma row stride in bytes
 * \param[out] pExternalSemaphore      Receives the external synchronization semaphore
 *
 * \retval    None
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
                                     VkSemaphore*                 pExternalSemaphore);

/*************************************************************************************************/
/**
 * \brief Imports native image or transfer-buffer resources into Vulkan.
 *
 * \par Details:
 *  Linux validates the descriptor's VA/DRM formats against format and imports its composed layer
 *  as an image, preserving modifier-specific memory planes and duplicating its file descriptors
 *  so the originals remain caller-owned. Supported representations match ExportImage, including
 *  the IYUV alias for I420. Windows imports sharedImageHandle as an image when it
 *  is non-NULL. Otherwise, Windows creates a mapped host buffer when transferBufferHostCopy is
 *  TRUE, or imports a shared D3D12 buffer and fence when it is FALSE. The buffer paths return
 *  externalBuffer with plane-layout metadata and leave image and memoryCount zero.
 *  The caller owns the returned Vulkan resources. The host-buffer mapping remains valid until
 *  that memory is unmapped or freed. Parameters specific to the other platform are ignored.
 *
 * \param[in]     physicalDevice             Physical device used to select compatible memory
 * \param[in]     device                     Logical device owning the imported resources
 * \param[in]     format                     Vulkan format matching the native image; unused for Windows buffers
 * \param[in]     extent                     Image extent with depth one; Linux width and height must match the descriptor
 * \param[in]     usage                      Vulkan image usage flags; Windows buffers use VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
 * \param[in]     drmPrimeDescriptor         Linux single-layer VADRMPRIMESurfaceDescriptor; ignored on Windows
 * \param[in]     sharedImageHandle          Windows shared D3D12 image-allocation HANDLE; NULL selects a buffer path
 * \param[in]     sharedImageHandleType      VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT or VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT for sharedImageHandle
 * \param[in]     sharedImageAllocationSize  Windows shared-heap size in bytes; ignored for resource-handle imports
 * \param[in]     transferBufferHostCopy     TRUE selects the Windows mapped host-buffer path when no image handle is supplied
 * \param[in]     transferBufferSize         Windows transfer-buffer size in bytes
 * \param[in]     sharedTransferBufferHandle Windows D3D12 buffer HANDLE required for the shared-buffer path
 * \param[in]     sharedFenceHandle          Windows D3D12 fence HANDLE required for the shared-buffer path
 * \param[in]     yStride                    Windows buffer luma row pitch in bytes
 * \param[in]     uvOffset                   Windows buffer interleaved chroma byte offset; must fit uint32_t
 * \param[in]     uvStride                   Windows buffer interleaved chroma row pitch in bytes
 * \param[in,out] ppVulkanTransferBytes      Windows buffer paths require an initially NULL pointer; receives the host mapping, stays NULL for shared buffers
 * \param[in]     pAllocator                 Optional Vulkan allocation callbacks, or NULL
 * \param[out]    pImage                     Receives the native image, or VK_NULL_HANDLE for buffer storage
 * \param[out]    pMemories                  Receives image allocations; requires VK_MAX_DRM_FORMAT_MODIFIER_PLANES entries
 * \param[out]    pMemoryCount               Receives the number of image memory allocations
 * \param[out]    pExternalBuffer            Receives the external transfer buffer
 * \param[out]    pExternalBufferMemory      Receives the external transfer buffer allocation
 * \param[out]    pExternalBufferSize        Receives the external transfer buffer size in bytes
 * \param[out]    pExternalBufferHostCopy    Receives whether the transfer buffer uses a host copy
 * \param[out]    pExternalYStride           Receives the luma row stride in bytes
 * \param[out]    pExternalUvOffset          Receives the chroma plane offset in bytes
 * \param[out]    pExternalUvStride          Receives the chroma row stride in bytes
 * \param[out]    pExternalSemaphore         Receives the external synchronization semaphore
 *
 * \retval    None
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
                                     VkSemaphore*                       pExternalSemaphore);

/*************************************************************************************************/
/**
 * \brief Returns the Vulkan instance extensions required by the platform.
 *
 * \par Details:
 *  Returns a borrowed read-only array with static lifetime. The caller must not free it.
 *  Extension names are not filtered by runtime support.
 *
 * \param[out] pExtensionCount Non-NULL output receiving the number of extension names
 * \param[out] pppExtensions   Non-NULL output receiving the file-local extension-name array
 *
 * \retval    None
 */
void Gfx_Vulkan_Platform_GetInstanceExtensions(uint32_t* pExtensionCount, const char* const** pppExtensions);

#endif /* VULKAN_VA_ZERO_COPY_GFX_VULKAN_PLATFORM_H */
