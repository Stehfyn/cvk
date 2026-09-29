#ifndef VULKAN_VA_ZERO_COPY_GFX_IMAGE_PLATFORM_H
#define VULKAN_VA_ZERO_COPY_GFX_IMAGE_PLATFORM_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_image.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Native handles, memory layout, and synchronization parameters for sharing an image.
 */
typedef struct Gfx_Image_Platform_CreateInfo_s
{
    void** sharedHandle;        ///< Native texture handle storage used by VA.
    void*  sharedImageResource; ///< Native image resource imported directly by VA.
    void*  sharedImageHandle;   ///< Native image allocation handle imported by Vulkan.
    VkExternalMemoryHandleTypeFlagBits
        sharedImageHandleType; ///< External-memory handle type identifying sharedImageHandle.
    VkDeviceSize
           sharedImageAllocationSize; ///< Complete heap allocation size in bytes; ignored for D3D12 resource handles.
    bool_t transferBufferHostCopy;    ///< TRUE to use the mapped host-copy fallback.
    VkDeviceSize transferBufferSize;  ///< Transfer-buffer allocation size.
    void*        sharedTransferBufferHandle; ///< Native transfer-buffer handle imported by Vulkan.
    void*        sharedFenceHandle;          ///< Native fence handle imported by Vulkan.
    uint32_t     yStride;                    ///< Luma or packed-pixel row pitch in bytes.
    uint64_t     uvOffset;                   ///< Second plane byte offset; zero for single-plane storage.
    uint32_t     uvStride;                   ///< Second plane row pitch; zero for single-plane storage.
    void**       ppVulkanTransferBytes;      ///< Receives the persistent Vulkan fallback mapping.
    void*        drmPrimeDescriptor;         ///< DMA-BUF descriptor used for export and import.
} Gfx_Image_Platform_CreateInfo_t;

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

#endif /* VULKAN_VA_ZERO_COPY_GFX_IMAGE_PLATFORM_H */
