/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <windows.h>

#include "gfx/gfx_vulkan_platform.h"
#include "gfx/gfx_vulkan.h"
#include "os/os_assert.h"
#include "utl/utl_defines.h"

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

/**
 * \brief Vulkan instance extensions used by the Windows platform implementation.
 */
static const char* const c_vkPlatformInstanceExtensions[] = {
    VK_KHR_SURFACE_EXTENSION_NAME,
    VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    //VK_KHR_DISPLAY_EXTENSION_NAME,
    //VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME
};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Allocates memory by importing a Win32 handle or name.
 *
 * \param[in]  device          Logical device importing the handle
 * \param[in]  pNext           Additional allocation extension chain, or NULL
 * \param[in]  allocationSize  Imported allocation size in bytes
 * \param[in]  memoryTypeIndex Compatible physical-device memory-type index selected by the caller
 * \param[in]  handleType      External-memory type matching handle or name
 * \param[in]  handle          Borrowed native allocation handle, or NULL when importing by name
 * \param[in]  name            Named allocation identifier, or NULL when importing a handle
 * \param[in]  pAllocator      Optional host allocation callbacks
 * \param[out] pMemory         Receives allocated memory; binding is performed by the caller
 *
 * \retval    None
 */
static void AllocateImportedMemoryHandleWin32(VkDevice                           device,
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
 * \brief Imports a Win32 handle or name as dedicated external image memory.
 *
 * \param[in]  device          Logical device importing the handle
 * \param[in]  pNext           Additional allocation extension chain, or NULL
 * \param[in]  image           Image receiving the dedicated allocation
 * \param[in]  allocationSize  Imported allocation size in bytes
 * \param[in]  memoryTypeIndex Compatible physical-device memory-type index selected by the caller
 * \param[in]  handleType      External-memory type matching handle or name
 * \param[in]  handle          Borrowed native allocation handle, or NULL when importing by name
 * \param[in]  name            Named allocation identifier, or NULL when importing a handle
 * \param[in]  pAllocator      Optional host allocation callbacks
 * \param[out] pMemory         Receives allocated memory; binding is performed by the caller
 *
 * \retval    None
 */
static void AllocateImportedDedicatedImageMemoryHandleWin32(VkDevice                           device,
                                                            const void*                        pNext,
                                                            VkImage                            image,
                                                            VkDeviceSize                       allocationSize,
                                                            uint32_t                           memoryTypeIndex,
                                                            VkExternalMemoryHandleTypeFlagBits handleType,
                                                            HANDLE                             handle,
                                                            LPCWSTR                            name,
                                                            const VkAllocationCallbacks*       pAllocator,
                                                            VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Initializes and queries memory properties for a Win32 allocation handle.
 *
 * \param[in]     device      Logical device
 * \param[in,out] pNext       Optional writable output extension chain, or NULL
 * \param[in]     handleType  External-memory handle type
 * \param[in]     handle      External allocation handle
 * \param[out]    pProperties Receives the initialized and queried properties structure
 *
 * \retval    None
 */
static void GetMemoryHandlePropertiesWin32(VkDevice device, void* pNext, VkExternalMemoryHandleTypeFlagBits handleType, HANDLE handle, VkMemoryWin32HandlePropertiesKHR* pProperties);

/*************************************************************************************************/
/**
 * \brief Allocates dedicated buffer memory by importing a native allocation.
 *
 * \param[in]  physicalDevice Physical device supplying memory properties
 * \param[in]  device         Logical device owning the resources
 * \param[in]  pNext          Optional extension chain, or NULL
 * \param[in]  buffer         Buffer receiving the dedicated allocation
 * \param[in]  memoryTypeBits Compatible native allocation memory-type mask
 * \param[in]  handleType     External-memory handle type
 * \param[in]  handle         Native handle, or NULL when importing by name
 * \param[in]  name           UTF-16 resource name, or NULL when importing a handle
 * \param[in]  properties     Required memory properties
 * \param[in]  pAllocator     Optional Vulkan allocation callbacks, or NULL
 * \param[out] pMemory        Receives allocated memory
 *
 * \retval    None
 */
static void AllocateImportedDedicatedBufferMemoryHandleWin32(VkPhysicalDevice                   physicalDevice,
                                                             VkDevice                           device,
                                                             const void*                        pNext,
                                                             VkBuffer                           buffer,
                                                             uint32_t                           memoryTypeBits,
                                                             VkExternalMemoryHandleTypeFlagBits handleType,
                                                             HANDLE                             handle,
                                                             LPCWSTR                            name,
                                                             VkMemoryPropertyFlags              properties,
                                                             const VkAllocationCallbacks*       pAllocator,
                                                             VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Creates a buffer, imports dedicated storage, and binds it using native allocation properties.
 *
 * \param[in]  physicalDevice        Physical device supplying memory properties
 * \param[in]  device                Logical device owning the resources
 * \param[in]  pBufferNext           Optional buffer creation extension chain, or NULL
 * \param[in]  handleType            External-memory handle type
 * \param[in]  flags                 Buffer creation flags
 * \param[in]  size                  Buffer size in bytes
 * \param[in]  usage                 Buffer usage flags
 * \param[in]  sharingMode           Buffer queue-family sharing mode
 * \param[in]  queueFamilyIndexCount Number of queue-family indices
 * \param[in]  pQueueFamilyIndices   Queue-family indices for concurrent sharing
 * \param[in]  pMemoryNext           Optional allocation extension chain, or NULL
 * \param[in]  handle                Native allocation handle whose properties can be queried
 * \param[in]  properties            Required memory properties
 * \param[in]  pAllocator            Optional Vulkan allocation callbacks, or NULL
 * \param[out] pBuffer               Receives the created and bound buffer
 * \param[out] pMemory               Receives allocated memory
 *
 * \retval    None
 */
static void SetupImportedMemoryBufferHandleWin32(VkPhysicalDevice                   physicalDevice,
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
                                                 HANDLE                             handle,
                                                 VkMemoryPropertyFlags              properties,
                                                 const VkAllocationCallbacks*       pAllocator,
                                                 VkBuffer*                          pBuffer,
                                                 VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Imports a native handle's synchronization payload into a semaphore.
 *
 * \param[in] device Logical device
 * \param[in] pInfo  Semaphore import parameters
 *
 * \retval    None
 */
static void ImportSemaphoreHandleWin32(VkDevice device, const VkImportSemaphoreWin32HandleInfoKHR* pInfo);

/*************************************************************************************************/
/**
 * \brief Imports a Win32 semaphore handle into an existing Vulkan semaphore.
 *
 * \param[in] device     Logical device owning semaphore
 * \param[in] pNext      Import extension chain, or NULL
 * \param[in] semaphore  Existing semaphore receiving the external payload
 * \param[in] flags      Permanent or temporary import flags
 * \param[in] handleType External-semaphore handle type
 * \param[in] handle     Handle to import, or NULL when importing by name
 * \param[in] name       Named handle identifier, or NULL when importing handle
 *
 * \retval    None
 */
static void ImportSemaphoreHandleInfoWin32(VkDevice                              device,
                                           const void*                           pNext,
                                           VkSemaphore                           semaphore,
                                           VkSemaphoreImportFlags                flags,
                                           VkExternalSemaphoreHandleTypeFlagBits handleType,
                                           HANDLE                                handle,
                                           LPCWSTR                               name);

/*************************************************************************************************/
/**
 * \brief Creates a semaphore and imports a Win32 handle payload into it.
 *
 * \param[in]  device         Logical device owning the semaphore
 * \param[in]  pSemaphoreNext Optional semaphore creation extension chain, or NULL
 * \param[in]  semaphoreFlags Semaphore creation flags
 * \param[in]  pImportNext    Optional semaphore import extension chain, or NULL
 * \param[in]  importFlags    Permanent or temporary import flags
 * \param[in]  handleType     External-semaphore handle type
 * \param[in]  handle         Handle to import, or NULL when importing by name
 * \param[in]  name           Named handle identifier, or NULL when importing a handle
 * \param[in]  pAllocator     Optional Vulkan allocation callbacks, or NULL
 * \param[out] pSemaphore     Receives the created semaphore with its imported payload
 *
 * \retval    None
 */
static void SetupImportedSemaphoreHandleWin32(VkDevice                              device,
                                              const void*                           pSemaphoreNext,
                                              VkSemaphoreCreateFlags                semaphoreFlags,
                                              const void*                           pImportNext,
                                              VkSemaphoreImportFlags                importFlags,
                                              VkExternalSemaphoreHandleTypeFlagBits handleType,
                                              HANDLE                                handle,
                                              LPCWSTR                               name,
                                              const VkAllocationCallbacks*          pAllocator,
                                              VkSemaphore*                          pSemaphore);

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
 * \param[in]  handle          Native handle, or NULL when importing by name
 * \param[in]  name            UTF-16 resource name, or NULL when importing a handle
 * \param[in]  pAllocator      Optional Vulkan allocation callbacks, or NULL
 * \param[out] pMemory         Receives allocated memory
 *
 * \retval    None
 */
static void SetupImportedDedicatedImageMemoryHandleWin32(VkDevice                           device,
                                                         const void*                        pNext,
                                                         VkImage                            image,
                                                         VkDeviceSize                       allocationSize,
                                                         uint32_t                           memoryTypeIndex,
                                                         VkExternalMemoryHandleTypeFlagBits handleType,
                                                         HANDLE                             handle,
                                                         LPCWSTR                            name,
                                                         const VkAllocationCallbacks*       pAllocator,
                                                         VkDeviceMemory*                    pMemory);

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
 * \param[in]  handle          Native allocation handle, or NULL when importing by name
 * \param[in]  name            UTF-16 allocation name, or NULL when importing a handle
 * \param[in]  memoryOffset    Aligned binding offset in the imported allocation
 * \param[in]  pAllocator      Optional allocation callbacks
 * \param[out] pMemory         Receives imported and bound memory
 *
 * \retval    None
 */
static void SetupImportedImageMemoryHandleWin32(VkDevice                           device,
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
 * \brief Allocates imported dedicated buffer memory and binds it.
 *
 * \param[in]  physicalDevice Physical device supplying memory properties
 * \param[in]  device         Logical device
 * \param[in]  pNext          Optional allocation extension chain
 * \param[in]  buffer         Resource receiving imported storage
 * \param[in]  memoryTypeBits Compatible native allocation memory-type mask
 * \param[in]  handleType     External-memory handle type
 * \param[in]  handle         Native allocation handle, or NULL when importing by name
 * \param[in]  name           UTF-16 allocation name, or NULL when importing a handle
 * \param[in]  properties     Required memory properties
 * \param[in]  pAllocator     Optional allocation callbacks
 * \param[out] pMemory        Receives imported and bound memory
 *
 * \retval    None
 */
static void SetupImportedDedicatedBufferMemoryHandleWin32(VkPhysicalDevice                   physicalDevice,
                                                          VkDevice                           device,
                                                          const void*                        pNext,
                                                          VkBuffer                           buffer,
                                                          uint32_t                           memoryTypeBits,
                                                          VkExternalMemoryHandleTypeFlagBits handleType,
                                                          HANDLE                             handle,
                                                          LPCWSTR                            name,
                                                          VkMemoryPropertyFlags              properties,
                                                          const VkAllocationCallbacks*       pAllocator,
                                                          VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Creates an image and imports and binds a non-dedicated allocation of a specified size.
 *
 * \param[in]  physicalDevice        Physical device supplying memory properties
 * \param[in]  device                Logical device
 * \param[in]  pImageNext            Image creation extension chain
 * \param[in]  handleType            Native external-memory type supporting non-dedicated imports
 * \param[in]  flags                 Image creation flags
 * \param[in]  imageType             Image dimensionality
 * \param[in]  format                Image format
 * \param[in]  extent                Image dimensions
 * \param[in]  mipLevels             Mip-level count
 * \param[in]  arrayLayers           Array-layer count
 * \param[in]  samples               Image sample count
 * \param[in]  tiling                Image tiling
 * \param[in]  usage                 Image usage
 * \param[in]  sharingMode           Queue-family sharing mode
 * \param[in]  queueFamilyIndexCount Queue-family index count
 * \param[in]  pQueueFamilyIndices   Queue-family indices
 * \param[in]  initialLayout         Initial image layout
 * \param[in]  pMemoryNext           Allocation extension chain
 * \param[in]  allocationSize        Complete native allocation size in bytes
 * \param[in]  handle                Native allocation handle whose properties can be queried
 * \param[in]  properties            Required memory properties
 * \param[in]  pAllocator            Optional allocation callbacks
 * \param[out] pImage                Receives the created and bound image
 * \param[out] pMemory               Receives imported memory
 *
 * \retval    None
 */
static void SetupImportedMemoryImageWithSizeHandleWin32(VkPhysicalDevice                   physicalDevice,
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
                                                        VkDeviceSize                       allocationSize,
                                                        HANDLE                             handle,
                                                        VkMemoryPropertyFlags              properties,
                                                        const VkAllocationCallbacks*       pAllocator,
                                                        VkImage*                           pImage,
                                                        VkDeviceMemory*                    pMemory);

/*************************************************************************************************/
/**
 * \brief Creates an image and imports and binds dedicated native storage.
 *
 * \param[in]  physicalDevice        Physical device supplying memory properties
 * \param[in]  device                Logical device
 * \param[in]  pImageNext            Image creation extension chain
 * \param[in]  handleType            Native external-memory type
 * \param[in]  flags                 Image creation flags
 * \param[in]  imageType             Image dimensionality
 * \param[in]  format                Image format
 * \param[in]  extent                Image dimensions
 * \param[in]  mipLevels             Mip-level count
 * \param[in]  arrayLayers           Array-layer count
 * \param[in]  samples               Image sample count
 * \param[in]  tiling                Image tiling
 * \param[in]  usage                 Image usage
 * \param[in]  sharingMode           Queue-family sharing mode
 * \param[in]  queueFamilyIndexCount Queue-family index count
 * \param[in]  pQueueFamilyIndices   Queue-family indices
 * \param[in]  initialLayout         Initial image layout
 * \param[in]  pMemoryNext           Allocation extension chain
 * \param[in]  handle                Native allocation handle whose properties can be queried
 * \param[in]  properties            Required memory properties
 * \param[in]  pAllocator            Optional allocation callbacks
 * \param[out] pImage                Receives the created and bound image
 * \param[out] pMemory               Receives imported memory
 *
 * \retval    None
 */
static void SetupImportedDedicatedMemoryImageHandleWin32(VkPhysicalDevice                   physicalDevice,
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

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void AllocateImportedMemoryHandleWin32(VkDevice                           device,
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
static void AllocateImportedDedicatedImageMemoryHandleWin32(VkDevice                           device,
                                                            const void*                        pNext,
                                                            VkImage                            image,
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
    Gfx_Vulkan_AllocateDedicatedImageMemory(device, &import_info, image, allocationSize, memoryTypeIndex, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void GetMemoryHandlePropertiesWin32(VkDevice device, void* pNext, VkExternalMemoryHandleTypeFlagBits handleType, HANDLE handle, VkMemoryWin32HandlePropertiesKHR* pProperties)
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
static void AllocateImportedDedicatedBufferMemoryHandleWin32(VkPhysicalDevice                   physicalDevice,
                                                             VkDevice                           device,
                                                             const void*                        pNext,
                                                             VkBuffer                           buffer,
                                                             uint32_t                           memoryTypeBits,
                                                             VkExternalMemoryHandleTypeFlagBits handleType,
                                                             HANDLE                             handle,
                                                             LPCWSTR                            name,
                                                             VkMemoryPropertyFlags              properties,
                                                             const VkAllocationCallbacks*       pAllocator,
                                                             VkDeviceMemory*                    pMemory)
{
    VkImportMemoryWin32HandleInfoKHR import_info;

    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT((NULL != handle) != (NULL != name));

    import_info.sType      = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
    import_info.pNext      = pNext;
    import_info.handleType = handleType;
    import_info.handle     = handle;
    import_info.name       = name;
    Gfx_Vulkan_AllocateDedicatedExternalBufferMemory(physicalDevice, device, &import_info, buffer, memoryTypeBits, properties, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupImportedMemoryBufferHandleWin32(VkPhysicalDevice                   physicalDevice,
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
                                                 HANDLE                             handle,
                                                 VkMemoryPropertyFlags              properties,
                                                 const VkAllocationCallbacks*       pAllocator,
                                                 VkBuffer*                          pBuffer,
                                                 VkDeviceMemory*                    pMemory)
{
    VkMemoryWin32HandlePropertiesKHR memory_properties;

    GetMemoryHandlePropertiesWin32(device, NULL, handleType, handle, &memory_properties);
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
    SetupImportedDedicatedBufferMemoryHandleWin32(physicalDevice, device, pMemoryNext, (*pBuffer), memory_properties.memoryTypeBits, handleType, handle, NULL, properties, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ImportSemaphoreHandleWin32(VkDevice device, const VkImportSemaphoreWin32HandleInfoKHR* pInfo)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != pInfo);

    LOGASSERT(NULL != vkImportSemaphoreWin32HandleKHR);
    VKCHECK(vkImportSemaphoreWin32HandleKHR(device, pInfo));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ImportSemaphoreHandleInfoWin32(VkDevice                              device,
                                           const void*                           pNext,
                                           VkSemaphore                           semaphore,
                                           VkSemaphoreImportFlags                flags,
                                           VkExternalSemaphoreHandleTypeFlagBits handleType,
                                           HANDLE                                handle,
                                           LPCWSTR                               name)
{
    VkImportSemaphoreWin32HandleInfoKHR info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != semaphore);
    LOGASSERT((NULL != handle) != (NULL != name));

    info.sType      = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_WIN32_HANDLE_INFO_KHR;
    info.pNext      = pNext;
    info.semaphore  = semaphore;
    info.flags      = flags;
    info.handleType = handleType;
    info.handle     = handle;
    info.name       = name;
    ImportSemaphoreHandleWin32(device, &info);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupImportedSemaphoreHandleWin32(VkDevice                              device,
                                              const void*                           pSemaphoreNext,
                                              VkSemaphoreCreateFlags                semaphoreFlags,
                                              const void*                           pImportNext,
                                              VkSemaphoreImportFlags                importFlags,
                                              VkExternalSemaphoreHandleTypeFlagBits handleType,
                                              HANDLE                                handle,
                                              LPCWSTR                               name,
                                              const VkAllocationCallbacks*          pAllocator,
                                              VkSemaphore*                          pSemaphore)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT((NULL != handle) != (NULL != name));
    LOGASSERT(NULL != pSemaphore);

    Gfx_Vulkan_CreateSemaphore(device, pSemaphoreNext, semaphoreFlags, pAllocator, pSemaphore);
    ImportSemaphoreHandleInfoWin32(device, pImportNext, (*pSemaphore), importFlags, handleType, handle, name);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupImportedDedicatedImageMemoryHandleWin32(VkDevice                           device,
                                                         const void*                        pNext,
                                                         VkImage                            image,
                                                         VkDeviceSize                       allocationSize,
                                                         uint32_t                           memoryTypeIndex,
                                                         VkExternalMemoryHandleTypeFlagBits handleType,
                                                         HANDLE                             handle,
                                                         LPCWSTR                            name,
                                                         const VkAllocationCallbacks*       pAllocator,
                                                         VkDeviceMemory*                    pMemory)
{
    AllocateImportedDedicatedImageMemoryHandleWin32(device, pNext, image, allocationSize, memoryTypeIndex, handleType, handle, name, pAllocator, pMemory);
    Gfx_Vulkan_BindImageMemory(device, image, (*pMemory), VK_NO_OFFSET);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupImportedImageMemoryHandleWin32(VkDevice                           device,
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
    AllocateImportedMemoryHandleWin32(device, pNext, allocationSize, memoryTypeIndex, handleType, handle, name, pAllocator, pMemory);
    Gfx_Vulkan_BindImageMemory(device, image, (*pMemory), memoryOffset);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupImportedDedicatedBufferMemoryHandleWin32(VkPhysicalDevice                   physicalDevice,
                                                          VkDevice                           device,
                                                          const void*                        pNext,
                                                          VkBuffer                           buffer,
                                                          uint32_t                           memoryTypeBits,
                                                          VkExternalMemoryHandleTypeFlagBits handleType,
                                                          HANDLE                             handle,
                                                          LPCWSTR                            name,
                                                          VkMemoryPropertyFlags              properties,
                                                          const VkAllocationCallbacks*       pAllocator,
                                                          VkDeviceMemory*                    pMemory)
{
    AllocateImportedDedicatedBufferMemoryHandleWin32(physicalDevice, device, pNext, buffer, memoryTypeBits, handleType, handle, name, properties, pAllocator, pMemory);
    Gfx_Vulkan_BindBufferMemory(device, buffer, (*pMemory), VK_NO_OFFSET);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupImportedMemoryImageWithSizeHandleWin32(VkPhysicalDevice                   physicalDevice,
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
                                                        VkDeviceSize                       allocationSize,
                                                        HANDLE                             handle,
                                                        VkMemoryPropertyFlags              properties,
                                                        const VkAllocationCallbacks*       pAllocator,
                                                        VkImage*                           pImage,
                                                        VkDeviceMemory*                    pMemory)
{
    VkMemoryWin32HandlePropertiesKHR memory_properties;
    VkDeviceSize                     required_size;
    uint32_t                         memory_type_index;

    GetMemoryHandlePropertiesWin32(device, NULL, handleType, handle, &memory_properties);
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
    Gfx_Vulkan_GetImageMemoryAllocationInfo(physicalDevice, device, (*pImage), memory_properties.memoryTypeBits, properties, &required_size, &memory_type_index);
    LOGASSERT(allocationSize >= required_size);

    SetupImportedImageMemoryHandleWin32(device, pMemoryNext, (*pImage), allocationSize, memory_type_index, handleType, handle, NULL, VK_NO_OFFSET, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void SetupImportedDedicatedMemoryImageHandleWin32(VkPhysicalDevice                   physicalDevice,
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

    GetMemoryHandlePropertiesWin32(device, NULL, handleType, handle, &memory_properties);
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
    SetupImportedDedicatedImageMemoryHandleWin32(device, pMemoryNext, (*pImage), allocation_size, memory_type_index, handleType, handle, NULL, pAllocator, pMemory);
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
    uint8_t  device_luid[VK_LUID_SIZE];
    VkBool32 luid_supported;
    int      luid_comparison;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pDevice);
    LOGASSERT(NULL != pQueue);

    luid_supported = Gfx_Vulkan_GetPhysicalDeviceLuid(physicalDevice, device_luid);
    LOGASSERT(VK_TRUE == luid_supported);

    luid_comparison = 0 == memcmp(device_luid, &adapterId, VK_LUID_SIZE);
    LOGASSERT(luid_comparison);

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
    (void)physicalDevice;
    (void)device;
    (void)format;
    (void)extent;
    (void)usage;
    (void)drmPrimeDescriptor;
    (void)pAllocator;
    (void)pImage;
    (void)pMemories;
    (void)pMemoryCount;
    (void)pExternalBuffer;
    (void)pExternalBufferMemory;
    (void)pExternalBufferSize;
    (void)pExternalBufferHostCopy;
    (void)pExternalYStride;
    (void)pExternalUvOffset;
    (void)pExternalUvStride;
    (void)pExternalSemaphore;
    LOGFATAL("Vulkan image export is unsupported on Windows");
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
    (void)drmPrimeDescriptor;

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

    if (NULL != sharedImageHandle)
    {
        if (VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT == sharedImageHandleType)
        {
            /* Vulkan requires DEDICATED_ONLY_BIT for D3D12_RESOURCE_BIT (a committed resource):
             * https://docs.vulkan.org/refpages/latest/refpages/source/VkExternalMemoryFeatureFlagBits.html */
            SetupImportedDedicatedMemoryImageHandleWin32(physicalDevice,
                                                         device,
                                                         NULL,
                                                         sharedImageHandleType,
                                                         (VkImageCreateFlags)0,
                                                         VK_IMAGE_TYPE_2D,
                                                         format,
                                                         extent,
                                                         1,
                                                         1,
                                                         VK_SAMPLE_COUNT_1_BIT,
                                                         VK_IMAGE_TILING_OPTIMAL,
                                                         usage,
                                                         VK_SHARING_MODE_EXCLUSIVE,
                                                         0,
                                                         NULL,
                                                         VK_IMAGE_LAYOUT_UNDEFINED,
                                                         NULL,
                                                         (HANDLE)sharedImageHandle,
                                                         VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                         pAllocator,
                                                         pImage,
                                                         &pMemories[0]);
        }
        else
        {
            /* This is a D3D12 heap; SelectVulkanSurfaceInteropD3D12 selects this path only when
             * the capability query does not report DEDICATED_ONLY_BIT, permitting non-dedicated import.
             * WithSize preserves the complete native heap size, including padding: Vulkan requires
             * allocationSize to equal D3D12_HEAP_DESC::SizeInBytes, not the image's memory requirement.
             * VUID-VkMemoryAllocateInfo-allocationSize-00647:
             * https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryAllocateInfo.html */
            SetupImportedMemoryImageWithSizeHandleWin32(physicalDevice,
                                                        device,
                                                        NULL,
                                                        sharedImageHandleType,
                                                        (VkImageCreateFlags)0,
                                                        VK_IMAGE_TYPE_2D,
                                                        format,
                                                        extent,
                                                        1,
                                                        1,
                                                        VK_SAMPLE_COUNT_1_BIT,
                                                        VK_IMAGE_TILING_OPTIMAL,
                                                        usage,
                                                        VK_SHARING_MODE_EXCLUSIVE,
                                                        0,
                                                        NULL,
                                                        VK_IMAGE_LAYOUT_UNDEFINED,
                                                        NULL,
                                                        sharedImageAllocationSize,
                                                        (HANDLE)sharedImageHandle,
                                                        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                        pAllocator,
                                                        pImage,
                                                        &pMemories[0]);
        }
        (*pMemoryCount) = 1;
    }
    else
    {
        LOGASSERT(NULL != ppVulkanTransferBytes);
        LOGASSERT(NULL == (*ppVulkanTransferBytes));

        if (transferBufferHostCopy)
        {
            /* Fallback when shared image and buffer imports are unavailable. Vulkan writes into
             * this mapped, host-coherent buffer; after GPU completion, the CPU copies its bytes
             * into a separate D3D12 upload buffer for transfer into the VA image. */
            Gfx_Vulkan_SetupBuffer(physicalDevice,
                                   device,
                                   NULL,
                                   (VkBufferCreateFlags)0,
                                   transferBufferSize,
                                   (VkBufferUsageFlags)VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                   VK_SHARING_MODE_EXCLUSIVE,
                                   0,
                                   NULL,
                                   NULL,
                                   (VkMemoryPropertyFlags)((VkFlags)VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | (VkFlags)VK_MEMORY_PROPERTY_HOST_COHERENT_BIT),
                                   pAllocator,
                                   pExternalBuffer,
                                   pExternalBufferMemory);
            Gfx_Vulkan_MapMemory(device, (*pExternalBufferMemory), VK_NO_OFFSET, VK_WHOLE_SIZE, (VkMemoryMapFlags)0, ppVulkanTransferBytes);
            (*pExternalBufferHostCopy) = TRUE;
        }
        else
        {
            /* Shared buffer fallback when direct image import is unavailable. Import the D3D12
             * buffer so Vulkan can write it without a CPU copy; D3D12 then copies it into the VA image.
             * Import the D3D12 fence as a Vulkan semaphore to order that copy after Vulkan writes. */
            LOGASSERT(NULL != sharedTransferBufferHandle);
            LOGASSERT(NULL != sharedFenceHandle);

            SetupImportedMemoryBufferHandleWin32(physicalDevice,
                                                 device,
                                                 NULL,
                                                 VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_RESOURCE_BIT,
                                                 (VkBufferCreateFlags)0,
                                                 transferBufferSize,
                                                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
                                                 VK_SHARING_MODE_EXCLUSIVE,
                                                 0,
                                                 NULL,
                                                 NULL,
                                                 (HANDLE)sharedTransferBufferHandle,
                                                 VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                                 pAllocator,
                                                 pExternalBuffer,
                                                 pExternalBufferMemory);
            SetupImportedSemaphoreHandleWin32(device,
                                              NULL,
                                              (VkSemaphoreCreateFlags)0,
                                              NULL,
                                              (VkSemaphoreImportFlags)0,
                                              VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_D3D12_FENCE_BIT,
                                              (HANDLE)sharedFenceHandle,
                                              NULL,
                                              pAllocator,
                                              pExternalSemaphore);
        }
        (*pExternalBufferSize)     = transferBufferSize;
        (*pExternalYStride)        = yStride;
        (*pExternalUvOffset)       = (uint32_t)uvOffset;
        (*pExternalUvStride)       = uvStride;
        (*pExternalBufferHostCopy) = transferBufferHostCopy;
        LOGASSERT((UINT64)(*pExternalUvOffset) == uvOffset);
    }
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
