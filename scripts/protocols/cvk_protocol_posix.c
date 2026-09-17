/* Tier 3 protocol (SPEC.md section 8b): source of truth kept with the generator and folded
** into the generated cvk_posix.h/.c. Composed from the generated surface only. */

#include <unistd.h>

/**
 * \brief Imports a file descriptor as a new image; the caller's descriptor stays valid.
 *
 * \par Details:
 *  Queries the descriptor's memory types (not permitted for the opaque descriptor type, which
 *  therefore allows every type), duplicates the descriptor because a successful import
 *  transfers ownership of the imported one to Vulkan, and runs the specification's import chain: capability (the handle type must be
 *  importable; DEDICATED_ONLY forces a dedicated allocation), creation with the handle type
 *  declared, requirements with dedicated requirements chained (requiresDedicatedAllocation
 *  also forces dedicated), memory type selection within the descriptor's memory types,
 *  allocation with the import info chained (dedicated info in front when required), and
 *  binding. The capability query is made with handleType alone; image-creation extension
 *  structs in pImageNext are not forwarded to it.
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
 * \param[in]  handleType              File-descriptor external memory handle type being imported
 * \param[in]  fd                      Descriptor to import; remains owned by the caller
 * \param[in]  allocationSize          Size of the external memory object; must cover the image's requirements
 * \param[in]  requiredProperties      Memory property flags the selected type must cover
 * \param[in]  pMemoryNext             Optional extension chain behind the import info, or NULL
 * \param[in]  pAllocator              Optional allocation callbacks, or NULL
 * \param[out] pImage                  Receives the created VkImage
 * \param[out] pMemory                 Receives the imported VkDeviceMemory
 *
 * \retval None
 */
void cvkImportFdImage(VkPhysicalDevice                   physicalDevice,
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
                      int                                fd,
                      VkDeviceSize                       allocationSize,
                      VkMemoryPropertyFlags              requiredProperties,
                      const void*                        pMemoryNext,
                      const VkAllocationCallbacks*       pAllocator,
                      VkImage*                           pImage,
                      VkDeviceMemory*                    pMemory)
{
    VkMemoryFdPropertiesKHR       memory_fd_properties;
    VkMemoryDedicatedRequirements memory_dedicated_requirements;
    VkMemoryRequirements2         memory_requirements;
    VkImportMemoryFdInfoKHR       import_memory_fd_info;
    VkMemoryDedicatedAllocateInfo memory_dedicated_allocate_info;
    const void*                   allocate_next;
    VkBool32                      importable;
    VkBool32                      dedicated;
    uint32_t                      external_memory_type_bits;
    uint32_t                      memory_type_index;
    int                           imported_fd;

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

    /* constraint: the descriptor's memory types; the opaque descriptor type may not be queried and allows every type */
    external_memory_type_bits = UINT32_MAX;
    if (VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT != handleType)
    {
        cvkGetMemoryFdProperties(device, handleType, fd, NULL, &memory_fd_properties);
        external_memory_type_bits = memory_fd_properties.memoryTypeBits;
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

    /* ownership: a successful import consumes the descriptor, so import a duplicate */
    imported_fd = dup(fd);
    CVK_ASSERT(0 <= imported_fd);

    /* allocate: import info heads the caller's chain, dedicated info in front when required */
    cvkInitImportMemoryFdInfo(&import_memory_fd_info, pMemoryNext, handleType, imported_fd);
    allocate_next = &import_memory_fd_info;
    if (dedicated)
    {
        cvkInitMemoryDedicatedAllocateInfo(&memory_dedicated_allocate_info, &import_memory_fd_info, (*pImage), VK_NULL_HANDLE);
        allocate_next = &memory_dedicated_allocate_info;
    }
    cvkAllocateMemory(device, allocate_next, allocationSize, memory_type_index, pAllocator, pMemory);

    /* bind */
    cvkBindImageMemory(device, (*pImage), (*pMemory), 0);
}

/**
 * \brief Imports a DRM PRIME layout (modifier, plane layouts, memory objects) as a new 2D image.
 *
 * \par Details:
 *  Runs the specification's import chain for VK_EXT_image_drm_format_modifier: the modifier's
 *  properties (its memory plane count must match the layout), capability with the modifier
 *  info chained (DISJOINT when several memory objects back the image; DEDICATED_ONLY is then
 *  impossible), creation with the handle type declared and the explicit plane layouts, the
 *  descriptors' memory types, requirements (whole image for one memory object, per memory
 *  plane otherwise, intersected per object), selection within each object's memory types,
 *  duplication of every descriptor because a successful import consumes it, allocation with
 *  the import info chained (dedicated info in front for one object when required), and
 *  binding (whole image, or every memory plane at once). Caller descriptors stay valid.
 *
 * \param[in]  physicalDevice             Physical device the image is created for
 * \param[in]  device                     Logical device owning the image and memory
 * \param[in]  pImageNext                 Optional extension chain for VkImageCreateInfo, or NULL
 * \param[in]  format                     VkImageCreateInfo::format
 * \param[in]  extent                     VkImageCreateInfo::extent; depth must be one
 * \param[in]  usage                      VkImageCreateInfo::usage
 * \param[in]  sharingMode                VkImageCreateInfo::sharingMode
 * \param[in]  queueFamilyIndexCount      VkImageCreateInfo::queueFamilyIndexCount
 * \param[in]  pQueueFamilyIndices        VkImageCreateInfo::pQueueFamilyIndices
 * \param[in]  initialLayout              VkImageCreateInfo::initialLayout
 * \param[in]  drmFormatModifier          DRM format modifier of the layout
 * \param[in]  planeCount                 Number of memory planes in the layout, at most CVK_MAX_DRM_FORMAT_MODIFIER_PLANES
 * \param[in]  pPlaneOffsets              Byte offset of each memory plane within its memory object
 * \param[in]  pPlaneRowPitches           Row pitch in bytes of each memory plane
 * \param[in]  pPlaneMemoryObjectIndices  Memory object backing each memory plane
 * \param[in]  memoryObjectCount          Number of memory objects, at most planeCount
 * \param[in]  pMemoryObjectSizes         Size in bytes of each memory object
 * \param[in]  pMemoryObjectFds           DMA-BUF descriptor of each memory object; remain owned by the caller
 * \param[in]  requiredProperties         Memory property flags every selected type must cover
 * \param[in]  pMemoryNext                Optional extension chain behind each import info, or NULL
 * \param[in]  pAllocator                 Optional allocation callbacks, or NULL
 * \param[out] pImage                     Receives the created VkImage
 * \param[out] pMemories                  Receives one VkDeviceMemory per memory object; memoryObjectCount entries
 *
 * \retval None
 */
void cvkImportDmaBufImage(VkPhysicalDevice             physicalDevice,
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
                          VkMemoryPropertyFlags        requiredProperties,
                          const void*                  pMemoryNext,
                          const VkAllocationCallbacks* pAllocator,
                          VkImage*                     pImage,
                          VkDeviceMemory*              pMemories)
{
    VkDrmFormatModifierPropertiesEXT              drm_format_modifier_properties[CVK_MAX_DRM_FORMAT_MODIFIERS];
    VkDrmFormatModifierPropertiesListEXT          drm_format_modifier_properties_list;
    VkPhysicalDeviceImageDrmFormatModifierInfoEXT physical_device_image_drm_format_modifier_info;
    VkSubresourceLayout                           plane_layouts[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkImageDrmFormatModifierExplicitCreateInfoEXT image_drm_format_modifier_explicit_create_info;
    VkMemoryFdPropertiesKHR                       memory_fd_properties;
    VkMemoryDedicatedRequirements                 memory_dedicated_requirements;
    VkMemoryRequirements2                         memory_requirements;
    VkImportMemoryFdInfoKHR                       import_memory_fd_info;
    VkMemoryDedicatedAllocateInfo                 memory_dedicated_allocate_info;
    VkImageAspectFlagBits                         plane_aspects[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkImage                                       plane_images[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkDeviceMemory                                plane_memories[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    VkDeviceSize                                  plane_offsets[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                                      object_memory_type_bits[CVK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    const void*                                   allocate_next;
    VkImageCreateFlags                            flags;
    VkBool32                                      importable;
    VkBool32                                      dedicated;
    uint32_t                                      modifier_index;
    uint32_t                                      modifier_plane_count;
    uint32_t                                      plane_index;
    uint32_t                                      object_index;
    uint32_t                                      memory_type_index;
    int                                           imported_fd;

    CVK_ASSERT(1 == extent.depth);
    CVK_ASSERT((0 != planeCount) && (CVK_MAX_DRM_FORMAT_MODIFIER_PLANES >= planeCount));
    CVK_ASSERT((0 != memoryObjectCount) && (planeCount >= memoryObjectCount));

    /* modifier: the layout's plane count must be the modifier's memory plane count */
    drm_format_modifier_properties_list.drmFormatModifierCount       = CVK_MAX_DRM_FORMAT_MODIFIERS;
    drm_format_modifier_properties_list.pDrmFormatModifierProperties = drm_format_modifier_properties;
    cvkGetPhysicalDeviceDrmFormatModifierPropertiesList(physicalDevice, format, NULL, &drm_format_modifier_properties_list);
    modifier_plane_count = 0;
    for (modifier_index = 0; modifier_index < drm_format_modifier_properties_list.drmFormatModifierCount; ++modifier_index)
    {
        if (drmFormatModifier == drm_format_modifier_properties[modifier_index].drmFormatModifier)
        {
            modifier_plane_count = drm_format_modifier_properties[modifier_index].drmFormatModifierPlaneCount;
        }
    }
    CVK_ASSERT(planeCount == modifier_plane_count);

    /* capability: importable with this modifier; several memory objects need a disjoint image, which cannot be dedicated-only */
    flags = (1u < memoryObjectCount) ? VK_IMAGE_CREATE_DISJOINT_BIT : 0;
    cvkInitPhysicalDeviceImageDrmFormatModifierInfo(&physical_device_image_drm_format_modifier_info, NULL, drmFormatModifier, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices);
    importable = cvkGetPhysicalDeviceExternalImageImportSupport(physicalDevice,
                                                                VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                                                                format,
                                                                VK_IMAGE_TYPE_2D,
                                                                VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
                                                                usage,
                                                                flags,
                                                                &physical_device_image_drm_format_modifier_info,
                                                                &dedicated);
    CVK_ASSERT(importable);
    CVK_ASSERT((1u == memoryObjectCount) || !dedicated);

    /* create, with the handle type declared and the explicit plane layouts */
    for (plane_index = 0; plane_index < planeCount; ++plane_index)
    {
        plane_layouts[plane_index].offset     = pPlaneOffsets[plane_index];
        plane_layouts[plane_index].size       = 0;
        plane_layouts[plane_index].rowPitch   = pPlaneRowPitches[plane_index];
        plane_layouts[plane_index].arrayPitch = 0;
        plane_layouts[plane_index].depthPitch = 0;
    }
    cvkInitImageDrmFormatModifierExplicitCreateInfo(&image_drm_format_modifier_explicit_create_info, pImageNext, drmFormatModifier, planeCount, plane_layouts);
    cvkCreateImageWithExternalMemoryInfo(device,
                                         &image_drm_format_modifier_explicit_create_info,
                                         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
                                         flags,
                                         VK_IMAGE_TYPE_2D,
                                         format,
                                         extent,
                                         1,
                                         1,
                                         VK_SAMPLE_COUNT_1_BIT,
                                         VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
                                         usage,
                                         sharingMode,
                                         queueFamilyIndexCount,
                                         pQueueFamilyIndices,
                                         initialLayout,
                                         pAllocator,
                                         pImage);

    /* constraint: each memory object's memory types */
    for (object_index = 0; object_index < memoryObjectCount; ++object_index)
    {
        cvkGetMemoryFdProperties(device, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, pMemoryObjectFds[object_index], NULL, &memory_fd_properties);
        object_memory_type_bits[object_index] = memory_fd_properties.memoryTypeBits;
    }

    if (1u == memoryObjectCount)
    {
        /* one memory object: whole-image requirements with dedicated requirements chained */
        cvkInitMemoryDedicatedRequirements(&memory_dedicated_requirements, NULL);
        cvkGetImageMemoryRequirementsInfo2(device, NULL, (*pImage), &memory_dedicated_requirements, &memory_requirements);
        CVK_ASSERT(pMemoryObjectSizes[0] >= memory_requirements.memoryRequirements.size);
        if (memory_dedicated_requirements.requiresDedicatedAllocation)
        {
            dedicated = VK_TRUE;
        }
        memory_type_index = cvkGetPhysicalDeviceCompatibleMemoryTypeIndex(physicalDevice, memory_requirements.memoryRequirements.memoryTypeBits & object_memory_type_bits[0], requiredProperties);
        CVK_ASSERT(VK_INVALID != memory_type_index);

        imported_fd = dup(pMemoryObjectFds[0]);
        CVK_ASSERT(0 <= imported_fd);
        cvkInitImportMemoryFdInfo(&import_memory_fd_info, pMemoryNext, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, imported_fd);
        allocate_next = &import_memory_fd_info;
        if (dedicated)
        {
            cvkInitMemoryDedicatedAllocateInfo(&memory_dedicated_allocate_info, &import_memory_fd_info, (*pImage), VK_NULL_HANDLE);
            allocate_next = &memory_dedicated_allocate_info;
        }
        cvkAllocateMemory(device, allocate_next, pMemoryObjectSizes[0], memory_type_index, pAllocator, &pMemories[0]);
        cvkBindImageMemory(device, (*pImage), pMemories[0], 0);
        return;
    }

    /* several memory objects: per-plane requirements, intersected into the object that backs each plane */
    for (plane_index = 0; plane_index < planeCount; ++plane_index)
    {
        object_index = pPlaneMemoryObjectIndices[plane_index];
        CVK_ASSERT(memoryObjectCount > object_index);
        plane_aspects[plane_index] = (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT << plane_index);
        cvkGetImagePlaneMemoryRequirementsInfo(device, NULL, plane_aspects[plane_index], (*pImage), NULL, &memory_requirements);
        CVK_ASSERT(pMemoryObjectSizes[object_index] >= memory_requirements.memoryRequirements.size);
        object_memory_type_bits[object_index] &= memory_requirements.memoryRequirements.memoryTypeBits;
    }
    for (object_index = 0; object_index < memoryObjectCount; ++object_index)
    {
        memory_type_index = cvkGetPhysicalDeviceCompatibleMemoryTypeIndex(physicalDevice, object_memory_type_bits[object_index], requiredProperties);
        CVK_ASSERT(VK_INVALID != memory_type_index);
        imported_fd = dup(pMemoryObjectFds[object_index]);
        CVK_ASSERT(0 <= imported_fd);
        cvkInitImportMemoryFdInfo(&import_memory_fd_info, pMemoryNext, VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT, imported_fd);
        cvkAllocateMemory(device, &import_memory_fd_info, pMemoryObjectSizes[object_index], memory_type_index, pAllocator, &pMemories[object_index]);
    }
    for (plane_index = 0; plane_index < planeCount; ++plane_index)
    {
        plane_images[plane_index]   = (*pImage);
        plane_memories[plane_index] = pMemories[pPlaneMemoryObjectIndices[plane_index]];
        plane_offsets[plane_index]  = 0;
    }
    cvkBindPlaneImageMemories(device, planeCount, NULL, plane_aspects, plane_images, plane_memories, plane_offsets);
}

/**
 * \brief Imports a file descriptor as a new buffer; the caller's descriptor stays valid.
 *
 * \par Details:
 *  Buffer form of cvkImportFdImage: capability (the handle type must be importable;
 *  DEDICATED_ONLY forces a dedicated allocation), creation with the handle type declared,
 *  the descriptor's memory types (not permitted for the opaque descriptor type, which
 *  therefore allows every type), requirements with dedicated requirements chained,
 *  memory type selection within the descriptor's memory types, duplication of the descriptor
 *  because a successful import consumes it, allocation with the import info chained
 *  (dedicated info in front when required), and binding.
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
 * \param[in]  handleType              File-descriptor external memory handle type being imported
 * \param[in]  fd                      Descriptor to import; remains owned by the caller
 * \param[in]  allocationSize          Size of the external memory object; must cover the buffer's requirements
 * \param[in]  requiredProperties      Memory property flags the selected type must cover
 * \param[in]  pMemoryNext             Optional extension chain behind the import info, or NULL
 * \param[in]  pAllocator              Optional allocation callbacks, or NULL
 * \param[out] pBuffer                 Receives the created VkBuffer
 * \param[out] pMemory                 Receives the imported VkDeviceMemory
 *
 * \retval None
 */
void cvkImportFdBuffer(VkPhysicalDevice                   physicalDevice,
                       VkDevice                           device,
                       const void*                        pBufferNext,
                       VkBufferCreateFlags                flags,
                       VkDeviceSize                       size,
                       VkBufferUsageFlags                 usage,
                       VkSharingMode                      sharingMode,
                       uint32_t                           queueFamilyIndexCount,
                       const uint32_t*                    pQueueFamilyIndices,
                       VkExternalMemoryHandleTypeFlagBits handleType,
                       int                                fd,
                       VkDeviceSize                       allocationSize,
                       VkMemoryPropertyFlags              requiredProperties,
                       const void*                        pMemoryNext,
                       const VkAllocationCallbacks*       pAllocator,
                       VkBuffer*                          pBuffer,
                       VkDeviceMemory*                    pMemory)
{
    VkMemoryFdPropertiesKHR       memory_fd_properties;
    VkMemoryDedicatedRequirements memory_dedicated_requirements;
    VkMemoryRequirements2         memory_requirements;
    VkImportMemoryFdInfoKHR       import_memory_fd_info;
    VkMemoryDedicatedAllocateInfo memory_dedicated_allocate_info;
    const void*                   allocate_next;
    VkBool32                      importable;
    VkBool32                      dedicated;
    uint32_t                      external_memory_type_bits;
    uint32_t                      memory_type_index;
    int                           imported_fd;

    /* capability: the handle type must be importable for this buffer description */
    importable = cvkGetPhysicalDeviceExternalBufferImportSupport(physicalDevice, NULL, flags, usage, handleType, &dedicated);
    CVK_ASSERT(importable);

    /* create, with the handle type declared */
    cvkCreateBufferWithExternalMemoryInfo(device, pBufferNext, (VkExternalMemoryHandleTypeFlags)handleType, flags, size, usage, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices, pAllocator, pBuffer);

    /* constraint: the descriptor's memory types; the opaque descriptor type may not be queried and allows every type */
    external_memory_type_bits = UINT32_MAX;
    if (VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT != handleType)
    {
        cvkGetMemoryFdProperties(device, handleType, fd, NULL, &memory_fd_properties);
        external_memory_type_bits = memory_fd_properties.memoryTypeBits;
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

    /* ownership: a successful import consumes the descriptor, so import a duplicate */
    imported_fd = dup(fd);
    CVK_ASSERT(0 <= imported_fd);

    /* allocate: import info heads the caller's chain, dedicated info in front when required */
    cvkInitImportMemoryFdInfo(&import_memory_fd_info, pMemoryNext, handleType, imported_fd);
    allocate_next = &import_memory_fd_info;
    if (dedicated)
    {
        cvkInitMemoryDedicatedAllocateInfo(&memory_dedicated_allocate_info, &import_memory_fd_info, VK_NULL_HANDLE, (*pBuffer));
        allocate_next = &memory_dedicated_allocate_info;
    }
    cvkAllocateMemory(device, allocate_next, allocationSize, memory_type_index, pAllocator, pMemory);

    /* bind */
    cvkBindBufferMemory(device, (*pBuffer), (*pMemory), 0);
}

/**
 * \brief Creates an image whose memory is exportable and exports it as a file descriptor.
 *
 * \par Details:
 *  Runs the specification's export chain: capability (the handle type must be exportable;
 *  DEDICATED_ONLY forces a dedicated allocation), creation with the handle type declared,
 *  requirements with dedicated requirements chained (requiresDedicatedAllocation also forces
 *  dedicated), memory type selection, allocation with the export info chained (dedicated info
 *  behind it when required), binding, and the descriptor export. The descriptor is owned by
 *  the caller, who closes it; the memory stays valid while the image and memory live.
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
 * \param[in]  handleType              File-descriptor external memory handle type to export
 * \param[in]  requiredProperties      Memory property flags the selected type must cover
 * \param[in]  pMemoryNext             Optional extension chain behind the export info, or NULL
 * \param[in]  pAllocator              Optional allocation callbacks, or NULL
 * \param[out] pImage                  Receives the created VkImage
 * \param[out] pMemory                 Receives the exportable VkDeviceMemory
 * \param[out] pFd                     Receives the exported descriptor, owned by the caller
 *
 * \retval None
 */
void cvkExportFdImage(VkPhysicalDevice                   physicalDevice,
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
                      VkMemoryPropertyFlags              requiredProperties,
                      const void*                        pMemoryNext,
                      const VkAllocationCallbacks*       pAllocator,
                      VkImage*                           pImage,
                      VkDeviceMemory*                    pMemory,
                      int*                               pFd)
{
    VkMemoryDedicatedRequirements memory_dedicated_requirements;
    VkMemoryRequirements2         memory_requirements;
    VkMemoryDedicatedAllocateInfo memory_dedicated_allocate_info;
    const void*                   allocate_next;
    VkBool32                      exportable;
    VkBool32                      dedicated;
    uint32_t                      memory_type_index;

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

    /* allocate: export info heads the chain, dedicated info behind it when required */
    allocate_next = pMemoryNext;
    if (dedicated)
    {
        cvkInitMemoryDedicatedAllocateInfo(&memory_dedicated_allocate_info, pMemoryNext, (*pImage), VK_NULL_HANDLE);
        allocate_next = &memory_dedicated_allocate_info;
    }
    cvkAllocateExportMemory(device, allocate_next, (VkExternalMemoryHandleTypeFlags)handleType, memory_requirements.memoryRequirements.size, memory_type_index, pAllocator, pMemory);

    /* bind, then export */
    cvkBindImageMemory(device, (*pImage), (*pMemory), 0);
    cvkGetMemoryFd(device, NULL, (*pMemory), handleType, pFd);
}

/**
 * \brief Creates a buffer whose memory is exportable and exports it as a file descriptor.
 *
 * \par Details:
 *  Buffer form of cvkExportFdImage: capability, creation with the handle type declared,
 *  requirements with dedicated requirements chained, memory type selection, allocation with
 *  the export info chained (dedicated info behind it when required), binding, and the
 *  descriptor export. The descriptor is owned by the caller, who closes it.
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
 * \param[in]  handleType              File-descriptor external memory handle type to export
 * \param[in]  requiredProperties      Memory property flags the selected type must cover
 * \param[in]  pMemoryNext             Optional extension chain behind the export info, or NULL
 * \param[in]  pAllocator              Optional allocation callbacks, or NULL
 * \param[out] pBuffer                 Receives the created VkBuffer
 * \param[out] pMemory                 Receives the exportable VkDeviceMemory
 * \param[out] pFd                     Receives the exported descriptor, owned by the caller
 *
 * \retval None
 */
void cvkExportFdBuffer(VkPhysicalDevice                   physicalDevice,
                       VkDevice                           device,
                       const void*                        pBufferNext,
                       VkBufferCreateFlags                flags,
                       VkDeviceSize                       size,
                       VkBufferUsageFlags                 usage,
                       VkSharingMode                      sharingMode,
                       uint32_t                           queueFamilyIndexCount,
                       const uint32_t*                    pQueueFamilyIndices,
                       VkExternalMemoryHandleTypeFlagBits handleType,
                       VkMemoryPropertyFlags              requiredProperties,
                       const void*                        pMemoryNext,
                       const VkAllocationCallbacks*       pAllocator,
                       VkBuffer*                          pBuffer,
                       VkDeviceMemory*                    pMemory,
                       int*                               pFd)
{
    VkMemoryDedicatedRequirements memory_dedicated_requirements;
    VkMemoryRequirements2         memory_requirements;
    VkMemoryDedicatedAllocateInfo memory_dedicated_allocate_info;
    const void*                   allocate_next;
    VkBool32                      exportable;
    VkBool32                      dedicated;
    uint32_t                      memory_type_index;

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

    /* allocate: export info heads the chain, dedicated info behind it when required */
    allocate_next = pMemoryNext;
    if (dedicated)
    {
        cvkInitMemoryDedicatedAllocateInfo(&memory_dedicated_allocate_info, pMemoryNext, VK_NULL_HANDLE, (*pBuffer));
        allocate_next = &memory_dedicated_allocate_info;
    }
    cvkAllocateExportMemory(device, allocate_next, (VkExternalMemoryHandleTypeFlags)handleType, memory_requirements.memoryRequirements.size, memory_type_index, pAllocator, pMemory);

    /* bind, then export */
    cvkBindBufferMemory(device, (*pBuffer), (*pMemory), 0);
    cvkGetMemoryFd(device, NULL, (*pMemory), handleType, pFd);
}
