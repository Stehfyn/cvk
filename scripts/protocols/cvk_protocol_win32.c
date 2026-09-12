/* Tier 3 protocol (SPEC.md section 8b): source of truth kept with the generator and folded
** into the generated cvk_win32.h/.c. Composed from the generated surface only. */

/**
 * \brief Imports a Win32 memory handle as a new image.
 *
 * \par Details:
 *  Queries the handle's memory types (not permitted for the opaque Win32 handle types, which
 *  therefore allow every type), and runs the specification's import chain: capability (the handle type must be
 *  importable; DEDICATED_ONLY forces a dedicated allocation), creation with the handle type
 *  declared, requirements with dedicated requirements chained (requiresDedicatedAllocation
 *  also forces dedicated), memory type selection within the handle's memory types, allocation
 *  with the import info chained (dedicated info in front when required), and binding. The
 *  handle stays owned by the caller as Vulkan specifies for Win32 imports. The capability
 *  query is made with handleType alone; image-creation extension structs in pImageNext are
 *  not forwarded to it.
 *
 * \param[in]  physicalDevice          Physical device the image is created for
 * \param[in]  device                  Logical device owning the image and memory
 * \param[in]  pImageNext              Optional extension chain for VkImageCreateInfo, or NULL
 * \param[in]  flags                   VkImageCreateInfo::flags
 * \param[in]  imageType               VkImageCreateInfo::imageType
 * \param[in]  format                  VkImageCreateInfo::format
 * \param[in]  extent                  VkImageCreateInfo::extent
 * \param[in]  mipLevels               VkImageCreateInfo::mipLevels
 * \param[in]  arrayLayers             VkImageCreateInfo::arrayLayers
 * \param[in]  samples                 VkImageCreateInfo::samples
 * \param[in]  tiling                  VkImageCreateInfo::tiling
 * \param[in]  usage                   VkImageCreateInfo::usage
 * \param[in]  sharingMode             VkImageCreateInfo::sharingMode
 * \param[in]  queueFamilyIndexCount   VkImageCreateInfo::queueFamilyIndexCount
 * \param[in]  pQueueFamilyIndices     VkImageCreateInfo::pQueueFamilyIndices
 * \param[in]  initialLayout           VkImageCreateInfo::initialLayout
 * \param[in]  handleType              Win32 external memory handle type being imported
 * \param[in]  handle                  Handle to import, or NULL when name is used
 * \param[in]  name                    Name of the object to import, or NULL when handle is used
 * \param[in]  allocationSize          Size of the external memory object; must cover the image's requirements
 * \param[in]  requiredProperties      Memory property flags the selected type must cover
 * \param[in]  pMemoryNext             Optional extension chain behind the import info, or NULL
 * \param[in]  pAllocator              Optional allocation callbacks, or NULL
 * \param[out] pImage                  Receives the created VkImage
 * \param[out] pMemory                 Receives the imported VkDeviceMemory
 *
 * \retval None
 */
void cvkImportWin32HandleImage(VkPhysicalDevice                   physicalDevice,
                               VkDevice                           device,
                               const void*                        pImageNext,
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
                               VkExternalMemoryHandleTypeFlagBits handleType,
                               HANDLE                             handle,
                               LPCWSTR                            name,
                               VkDeviceSize                       allocationSize,
                               VkMemoryPropertyFlags              requiredProperties,
                               const void*                        pMemoryNext,
                               const VkAllocationCallbacks*       pAllocator,
                               VkImage*                           pImage,
                               VkDeviceMemory*                    pMemory)
{
    VkMemoryWin32HandlePropertiesKHR memory_win32_handle_properties;
    VkMemoryDedicatedRequirements    memory_dedicated_requirements;
    VkMemoryRequirements2            memory_requirements;
    VkImportMemoryWin32HandleInfoKHR import_memory_win32_handle_info;
    VkMemoryDedicatedAllocateInfo    memory_dedicated_allocate_info;
    const void*                      allocate_next;
    VkBool32                         importable;
    VkBool32                         dedicated;
    uint32_t                         external_memory_type_bits;
    uint32_t                         memory_type_index;

    /* capability: the handle type must be importable for this image description */
    importable = cvkGetPhysicalDeviceExternalImageImportSupport(physicalDevice, handleType, format, imageType, tiling, usage, flags, NULL, &dedicated);
    CVK_ASSERT(importable);

    /* create, with the handle type declared */
    cvkCreateImageWithExternalMemoryInfo(device,
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

    /* constraint: the handle's memory types; opaque Win32 handle types may not be queried and allow every type */
    external_memory_type_bits = UINT32_MAX;
    if ((VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT != handleType) && (VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT != handleType) && (NULL != handle))
    {
        cvkGetMemoryWin32HandleProperties(device, handleType, handle, NULL, &memory_win32_handle_properties);
        external_memory_type_bits = memory_win32_handle_properties.memoryTypeBits;
    }

    /* requirements, with dedicated requirements chained */
    cvkInitMemoryDedicatedRequirements(&memory_dedicated_requirements, NULL);
    cvkGetImageMemoryRequirementsInfo2(device, NULL, (*pImage), &memory_dedicated_requirements, &memory_requirements);
    CVK_ASSERT(allocationSize >= memory_requirements.memoryRequirements.size);
    if (memory_dedicated_requirements.requiresDedicatedAllocation)
    {
        dedicated = VK_TRUE;
    }

    /* select within the external object's memory types */
    memory_type_index = cvkGetPhysicalDeviceCompatibleMemoryTypeIndex(physicalDevice, memory_requirements.memoryRequirements.memoryTypeBits & external_memory_type_bits, requiredProperties);
    CVK_ASSERT(VK_INVALID != memory_type_index);

    /* allocate: import info heads the caller's chain, dedicated info in front when required; the handle stays caller-owned */
    cvkInitImportMemoryWin32HandleInfo(&import_memory_win32_handle_info, pMemoryNext, handleType, handle, name);
    allocate_next = &import_memory_win32_handle_info;
    if (dedicated)
    {
        cvkInitMemoryDedicatedAllocateInfo(&memory_dedicated_allocate_info, &import_memory_win32_handle_info, (*pImage), VK_NULL_HANDLE);
        allocate_next = &memory_dedicated_allocate_info;
    }
    cvkAllocateMemory(device, allocate_next, allocationSize, memory_type_index, pAllocator, pMemory);

    /* bind */
    cvkBindImageMemory(device, (*pImage), (*pMemory), 0);
}

/**
 * \brief Imports a Win32 memory handle as a new buffer.
 *
 * \par Details:
 *  Buffer form of cvkImportWin32HandleImage: capability (the handle type must be importable;
 *  DEDICATED_ONLY forces a dedicated allocation), creation with the handle type declared,
 *  the handle's memory types (not permitted for the opaque Win32 handle types, which
 *  therefore allow every type), requirements with dedicated requirements chained, memory
 *  type selection within the handle's memory types, allocation with the import info chained
 *  (dedicated info in front when required), and binding. The handle stays owned by the
 *  caller as Vulkan specifies for Win32 imports.
 *
 * \param[in]  physicalDevice          Physical device the buffer is created for
 * \param[in]  device                  Logical device owning the buffer and memory
 * \param[in]  pBufferNext             Optional extension chain for VkBufferCreateInfo, or NULL
 * \param[in]  flags                   VkBufferCreateInfo::flags
 * \param[in]  size                    VkBufferCreateInfo::size
 * \param[in]  usage                   VkBufferCreateInfo::usage
 * \param[in]  sharingMode             VkBufferCreateInfo::sharingMode
 * \param[in]  queueFamilyIndexCount   VkBufferCreateInfo::queueFamilyIndexCount
 * \param[in]  pQueueFamilyIndices     VkBufferCreateInfo::pQueueFamilyIndices
 * \param[in]  handleType              Win32 external memory handle type being imported
 * \param[in]  handle                  Handle to import, or NULL when name is used
 * \param[in]  name                    Name of the object to import, or NULL when handle is used
 * \param[in]  allocationSize          Size of the external memory object; must cover the buffer's requirements
 * \param[in]  requiredProperties      Memory property flags the selected type must cover
 * \param[in]  pMemoryNext             Optional extension chain behind the import info, or NULL
 * \param[in]  pAllocator              Optional allocation callbacks, or NULL
 * \param[out] pBuffer                 Receives the created VkBuffer
 * \param[out] pMemory                 Receives the imported VkDeviceMemory
 *
 * \retval None
 */
void cvkImportWin32HandleBuffer(VkPhysicalDevice                   physicalDevice,
                                VkDevice                           device,
                                const void*                        pBufferNext,
                                VkBufferCreateFlags                flags,
                                VkDeviceSize                       size,
                                VkBufferUsageFlags                 usage,
                                VkSharingMode                      sharingMode,
                                uint32_t                           queueFamilyIndexCount,
                                const uint32_t*                    pQueueFamilyIndices,
                                VkExternalMemoryHandleTypeFlagBits handleType,
                                HANDLE                             handle,
                                LPCWSTR                            name,
                                VkDeviceSize                       allocationSize,
                                VkMemoryPropertyFlags              requiredProperties,
                                const void*                        pMemoryNext,
                                const VkAllocationCallbacks*       pAllocator,
                                VkBuffer*                          pBuffer,
                                VkDeviceMemory*                    pMemory)
{
    VkMemoryWin32HandlePropertiesKHR memory_win32_handle_properties;
    VkMemoryDedicatedRequirements    memory_dedicated_requirements;
    VkMemoryRequirements2            memory_requirements;
    VkImportMemoryWin32HandleInfoKHR import_memory_win32_handle_info;
    VkMemoryDedicatedAllocateInfo    memory_dedicated_allocate_info;
    const void*                      allocate_next;
    VkBool32                         importable;
    VkBool32                         dedicated;
    uint32_t                         external_memory_type_bits;
    uint32_t                         memory_type_index;

    /* capability: the handle type must be importable for this buffer description */
    importable = cvkGetPhysicalDeviceExternalBufferImportSupport(physicalDevice, NULL, flags, usage, handleType, &dedicated);
    CVK_ASSERT(importable);

    /* create, with the handle type declared */
    cvkCreateBufferWithExternalMemoryInfo(device, pBufferNext, (VkExternalMemoryHandleTypeFlags)handleType, flags, size, usage, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices, pAllocator, pBuffer);

    /* constraint: the handle's memory types; opaque Win32 handle types may not be queried and allow every type */
    external_memory_type_bits = UINT32_MAX;
    if ((VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT != handleType) && (VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_KMT_BIT != handleType) && (NULL != handle))
    {
        cvkGetMemoryWin32HandleProperties(device, handleType, handle, NULL, &memory_win32_handle_properties);
        external_memory_type_bits = memory_win32_handle_properties.memoryTypeBits;
    }

    /* requirements, with dedicated requirements chained */
    cvkInitMemoryDedicatedRequirements(&memory_dedicated_requirements, NULL);
    cvkGetBufferMemoryRequirementsInfo2(device, NULL, (*pBuffer), &memory_dedicated_requirements, &memory_requirements);
    CVK_ASSERT(allocationSize >= memory_requirements.memoryRequirements.size);
    if (memory_dedicated_requirements.requiresDedicatedAllocation)
    {
        dedicated = VK_TRUE;
    }

    /* select within the external object's memory types */
    memory_type_index = cvkGetPhysicalDeviceCompatibleMemoryTypeIndex(physicalDevice, memory_requirements.memoryRequirements.memoryTypeBits & external_memory_type_bits, requiredProperties);
    CVK_ASSERT(VK_INVALID != memory_type_index);

    /* allocate: import info heads the caller's chain, dedicated info in front when required; the handle stays caller-owned */
    cvkInitImportMemoryWin32HandleInfo(&import_memory_win32_handle_info, pMemoryNext, handleType, handle, name);
    allocate_next = &import_memory_win32_handle_info;
    if (dedicated)
    {
        cvkInitMemoryDedicatedAllocateInfo(&memory_dedicated_allocate_info, &import_memory_win32_handle_info, VK_NULL_HANDLE, (*pBuffer));
        allocate_next = &memory_dedicated_allocate_info;
    }
    cvkAllocateMemory(device, allocate_next, allocationSize, memory_type_index, pAllocator, pMemory);

    /* bind */
    cvkBindBufferMemory(device, (*pBuffer), (*pMemory), 0);
}

/**
 * \brief Creates an image whose memory is exportable and exports it as a Win32 handle.
 *
 * \par Details:
 *  Runs the specification's export chain: capability (the handle type must be exportable;
 *  DEDICATED_ONLY forces a dedicated allocation), creation with the handle type declared,
 *  requirements with dedicated requirements chained (requiresDedicatedAllocation also forces
 *  dedicated), memory type selection, allocation with the export info chained (the Win32
 *  handle info behind it for the opaque and D3D handle types, which are the only ones that
 *  permit it, then dedicated info when required), binding, and the handle export. The handle
 *  is owned by the caller, who closes it; the memory stays valid while the image and memory live.
 *
 * \param[in]  physicalDevice          Physical device the image is created for
 * \param[in]  device                  Logical device owning the image and memory
 * \param[in]  pImageNext              Optional extension chain for VkImageCreateInfo, or NULL
 * \param[in]  flags                   VkImageCreateInfo::flags
 * \param[in]  imageType               VkImageCreateInfo::imageType
 * \param[in]  format                  VkImageCreateInfo::format
 * \param[in]  extent                  VkImageCreateInfo::extent
 * \param[in]  mipLevels               VkImageCreateInfo::mipLevels
 * \param[in]  arrayLayers             VkImageCreateInfo::arrayLayers
 * \param[in]  samples                 VkImageCreateInfo::samples
 * \param[in]  tiling                  VkImageCreateInfo::tiling
 * \param[in]  usage                   VkImageCreateInfo::usage
 * \param[in]  sharingMode             VkImageCreateInfo::sharingMode
 * \param[in]  queueFamilyIndexCount   VkImageCreateInfo::queueFamilyIndexCount
 * \param[in]  pQueueFamilyIndices     VkImageCreateInfo::pQueueFamilyIndices
 * \param[in]  initialLayout           VkImageCreateInfo::initialLayout
 * \param[in]  handleType              Win32 external memory handle type to export
 * \param[in]  pAttributes             VkExportMemoryWin32HandleInfoKHR::pAttributes, or NULL for the defaults
 * \param[in]  dwAccess                VkExportMemoryWin32HandleInfoKHR::dwAccess
 * \param[in]  name                    VkExportMemoryWin32HandleInfoKHR::name, or NULL for an unnamed object
 * \param[in]  requiredProperties      Memory property flags the selected type must cover
 * \param[in]  pMemoryNext             Optional extension chain behind the export infos, or NULL
 * \param[in]  pAllocator              Optional allocation callbacks, or NULL
 * \param[out] pImage                  Receives the created VkImage
 * \param[out] pMemory                 Receives the exportable VkDeviceMemory
 * \param[out] pHandle                 Receives the exported handle, owned by the caller
 *
 * \retval None
 */
void cvkExportWin32HandleImage(VkPhysicalDevice                   physicalDevice,
                               VkDevice                           device,
                               const void*                        pImageNext,
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
                               VkExternalMemoryHandleTypeFlagBits handleType,
                               const SECURITY_ATTRIBUTES*         pAttributes,
                               DWORD                              dwAccess,
                               LPCWSTR                            name,
                               VkMemoryPropertyFlags              requiredProperties,
                               const void*                        pMemoryNext,
                               const VkAllocationCallbacks*       pAllocator,
                               VkImage*                           pImage,
                               VkDeviceMemory*                    pMemory,
                               HANDLE*                            pHandle)
{
    VkMemoryDedicatedRequirements    memory_dedicated_requirements;
    VkMemoryRequirements2            memory_requirements;
    VkMemoryDedicatedAllocateInfo    memory_dedicated_allocate_info;
    VkExportMemoryWin32HandleInfoKHR export_memory_win32_handle_info;
    const void*                      allocate_next;
    VkBool32                         exportable;
    VkBool32                         dedicated;
    uint32_t                         memory_type_index;

    /* capability: the handle type must be exportable for this image description */
    exportable = cvkGetPhysicalDeviceExternalImageExportSupport(physicalDevice, handleType, format, imageType, tiling, usage, flags, NULL, &dedicated);
    CVK_ASSERT(exportable);

    /* create, with the handle type declared */
    cvkCreateImageWithExternalMemoryInfo(device,
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

    /* requirements, with dedicated requirements chained */
    cvkInitMemoryDedicatedRequirements(&memory_dedicated_requirements, NULL);
    cvkGetImageMemoryRequirementsInfo2(device, NULL, (*pImage), &memory_dedicated_requirements, &memory_requirements);
    if (memory_dedicated_requirements.requiresDedicatedAllocation)
    {
        dedicated = VK_TRUE;
    }

    /* select */
    memory_type_index = cvkGetPhysicalDeviceCompatibleMemoryTypeIndex(physicalDevice, memory_requirements.memoryRequirements.memoryTypeBits, requiredProperties);
    CVK_ASSERT(VK_INVALID != memory_type_index);

    /* allocate: export info heads the chain, dedicated info when required, then the Win32 handle info where permitted */
    allocate_next = pMemoryNext;
    if (dedicated)
    {
        cvkInitMemoryDedicatedAllocateInfo(&memory_dedicated_allocate_info, pMemoryNext, (*pImage), VK_NULL_HANDLE);
        allocate_next = &memory_dedicated_allocate_info;
    }
    if ((VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT == handleType) || (VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT == handleType) || (VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT == handleType))
    {
        cvkInitExportMemoryWin32HandleInfo(&export_memory_win32_handle_info, allocate_next, pAttributes, dwAccess, name);
        allocate_next = &export_memory_win32_handle_info;
    }
    cvkAllocateExportMemory(device, allocate_next, (VkExternalMemoryHandleTypeFlags)handleType, memory_requirements.memoryRequirements.size, memory_type_index, pAllocator, pMemory);

    /* bind, then export */
    cvkBindImageMemory(device, (*pImage), (*pMemory), 0);
    cvkGetMemoryWin32Handle(device, NULL, (*pMemory), handleType, pHandle);
}

/**
 * \brief Creates a buffer whose memory is exportable and exports it as a Win32 handle.
 *
 * \par Details:
 *  Buffer form of cvkExportWin32HandleImage: capability, creation with the handle type
 *  declared, requirements with dedicated requirements chained, memory type selection,
 *  allocation with the export info chained (the Win32 handle info behind it for the opaque
 *  and D3D handle types, then dedicated info when required), binding, and the handle export.
 *  The handle is owned by the caller, who closes it.
 *
 * \param[in]  physicalDevice          Physical device the buffer is created for
 * \param[in]  device                  Logical device owning the buffer and memory
 * \param[in]  pBufferNext             Optional extension chain for VkBufferCreateInfo, or NULL
 * \param[in]  flags                   VkBufferCreateInfo::flags
 * \param[in]  size                    VkBufferCreateInfo::size
 * \param[in]  usage                   VkBufferCreateInfo::usage
 * \param[in]  sharingMode             VkBufferCreateInfo::sharingMode
 * \param[in]  queueFamilyIndexCount   VkBufferCreateInfo::queueFamilyIndexCount
 * \param[in]  pQueueFamilyIndices     VkBufferCreateInfo::pQueueFamilyIndices
 * \param[in]  handleType              Win32 external memory handle type to export
 * \param[in]  pAttributes             VkExportMemoryWin32HandleInfoKHR::pAttributes, or NULL for the defaults
 * \param[in]  dwAccess                VkExportMemoryWin32HandleInfoKHR::dwAccess
 * \param[in]  name                    VkExportMemoryWin32HandleInfoKHR::name, or NULL for an unnamed object
 * \param[in]  requiredProperties      Memory property flags the selected type must cover
 * \param[in]  pMemoryNext             Optional extension chain behind the export infos, or NULL
 * \param[in]  pAllocator              Optional allocation callbacks, or NULL
 * \param[out] pBuffer                 Receives the created VkBuffer
 * \param[out] pMemory                 Receives the exportable VkDeviceMemory
 * \param[out] pHandle                 Receives the exported handle, owned by the caller
 *
 * \retval None
 */
void cvkExportWin32HandleBuffer(VkPhysicalDevice                   physicalDevice,
                                VkDevice                           device,
                                const void*                        pBufferNext,
                                VkBufferCreateFlags                flags,
                                VkDeviceSize                       size,
                                VkBufferUsageFlags                 usage,
                                VkSharingMode                      sharingMode,
                                uint32_t                           queueFamilyIndexCount,
                                const uint32_t*                    pQueueFamilyIndices,
                                VkExternalMemoryHandleTypeFlagBits handleType,
                                const SECURITY_ATTRIBUTES*         pAttributes,
                                DWORD                              dwAccess,
                                LPCWSTR                            name,
                                VkMemoryPropertyFlags              requiredProperties,
                                const void*                        pMemoryNext,
                                const VkAllocationCallbacks*       pAllocator,
                                VkBuffer*                          pBuffer,
                                VkDeviceMemory*                    pMemory,
                                HANDLE*                            pHandle)
{
    VkMemoryDedicatedRequirements    memory_dedicated_requirements;
    VkMemoryRequirements2            memory_requirements;
    VkMemoryDedicatedAllocateInfo    memory_dedicated_allocate_info;
    VkExportMemoryWin32HandleInfoKHR export_memory_win32_handle_info;
    const void*                      allocate_next;
    VkBool32                         exportable;
    VkBool32                         dedicated;
    uint32_t                         memory_type_index;

    /* capability: the handle type must be exportable for this buffer description */
    exportable = cvkGetPhysicalDeviceExternalBufferExportSupport(physicalDevice, NULL, flags, usage, handleType, &dedicated);
    CVK_ASSERT(exportable);

    /* create, with the handle type declared */
    cvkCreateBufferWithExternalMemoryInfo(device, pBufferNext, (VkExternalMemoryHandleTypeFlags)handleType, flags, size, usage, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices, pAllocator, pBuffer);

    /* requirements, with dedicated requirements chained */
    cvkInitMemoryDedicatedRequirements(&memory_dedicated_requirements, NULL);
    cvkGetBufferMemoryRequirementsInfo2(device, NULL, (*pBuffer), &memory_dedicated_requirements, &memory_requirements);
    if (memory_dedicated_requirements.requiresDedicatedAllocation)
    {
        dedicated = VK_TRUE;
    }

    /* select */
    memory_type_index = cvkGetPhysicalDeviceCompatibleMemoryTypeIndex(physicalDevice, memory_requirements.memoryRequirements.memoryTypeBits, requiredProperties);
    CVK_ASSERT(VK_INVALID != memory_type_index);

    /* allocate: export info heads the chain, dedicated info when required, then the Win32 handle info where permitted */
    allocate_next = pMemoryNext;
    if (dedicated)
    {
        cvkInitMemoryDedicatedAllocateInfo(&memory_dedicated_allocate_info, pMemoryNext, VK_NULL_HANDLE, (*pBuffer));
        allocate_next = &memory_dedicated_allocate_info;
    }
    if ((VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT == handleType) || (VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT == handleType) || (VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D12_HEAP_BIT == handleType))
    {
        cvkInitExportMemoryWin32HandleInfo(&export_memory_win32_handle_info, allocate_next, pAttributes, dwAccess, name);
        allocate_next = &export_memory_win32_handle_info;
    }
    cvkAllocateExportMemory(device, allocate_next, (VkExternalMemoryHandleTypeFlags)handleType, memory_requirements.memoryRequirements.size, memory_type_index, pAllocator, pMemory);

    /* bind, then export */
    cvkBindBufferMemory(device, (*pBuffer), (*pMemory), 0);
    cvkGetMemoryWin32Handle(device, NULL, (*pMemory), handleType, pHandle);
}
