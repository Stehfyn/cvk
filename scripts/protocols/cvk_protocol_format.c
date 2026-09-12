/* Tier 3 protocols (SPEC.md section 8b): source of truth kept with the generator and folded
** into the generated cvk.h/cvk.c. Composed from the generated surface only.
** Profile: vulkan. The format tables of the Vulkan Utility Libraries describe formats the
** Vulkan SC headers do not define, so this file is absent from the SC profile. */

#include <vulkan/utility/vk_format_utils.h>

/**
 * \brief Returns the number of format planes of a format (one for single-plane formats).
 *
 * \param[in] format  Vulkan format
 *
 * \return Plane count from the format tables.
 */
uint32_t cvkGetFormatPlaneCount(VkFormat format)
{
    return vkuFormatPlaneCount(format);
}

/**
 * \brief Returns the aspect addressing one format plane: color for single-plane formats, else the plane bit.
 *
 * \param[in] format      Vulkan format
 * \param[in] planeIndex  Plane index below the format's plane count
 *
 * \return The aspect bit for the plane.
 */
VkImageAspectFlagBits cvkGetFormatPlaneAspect(VkFormat format, uint32_t planeIndex)
{
    if (1u == vkuFormatPlaneCount(format))
    {
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
    /* format plane aspects are consecutive bits from VK_IMAGE_ASPECT_PLANE_0_BIT */
    return (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_PLANE_0_BIT << planeIndex);
}

/**
 * \brief Returns the aspect addressing one memory plane of a DRM format modifier image.
 *
 * \param[in] planeIndex  Memory plane index below CVK_MAX_DRM_FORMAT_MODIFIER_PLANES
 *
 * \return The memory plane aspect bit.
 */
VkImageAspectFlagBits cvkGetMemoryPlaneAspect(uint32_t planeIndex)
{
    /* memory plane aspects are consecutive bits from VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT */
    return (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT << planeIndex);
}

/**
 * \brief Returns the aspect mask covering every aspect of a format: depth and stencil, color, or all planes.
 *
 * \param[in] format  Vulkan format
 *
 * \return The complete aspect mask of the format.
 */
VkImageAspectFlags cvkGetFormatAspectMask(VkFormat format)
{
    VkImageAspectFlags aspect_mask;
    uint32_t           plane_count;
    uint32_t           plane_index;

    aspect_mask = 0;
    if (vkuFormatHasDepth(format))
    {
        aspect_mask |= VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (vkuFormatHasStencil(format))
    {
        aspect_mask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    if (0 != aspect_mask)
    {
        return aspect_mask;
    }
    plane_count = vkuFormatPlaneCount(format);
    for (plane_index = 0; plane_index < plane_count; ++plane_index)
    {
        aspect_mask |= cvkGetFormatPlaneAspect(format, plane_index);
    }
    return aspect_mask;
}

/**
 * \brief Builds subresource layers covering every aspect of a format.
 *
 * \param[in] format          Vulkan format
 * \param[in] mipLevel        Mip level
 * \param[in] baseArrayLayer  First array layer
 * \param[in] layerCount      Number of array layers
 *
 * \return The subresource layers.
 */
VkImageSubresourceLayers cvkGetFormatSubresourceLayers(VkFormat format, uint32_t mipLevel, uint32_t baseArrayLayer, uint32_t layerCount)
{
    VkImageSubresourceLayers image_subresource_layers;

    cvkInitImageSubresourceLayers(&image_subresource_layers, cvkGetFormatAspectMask(format), mipLevel, baseArrayLayer, layerCount);
    return image_subresource_layers;
}

/**
 * \brief Builds subresource layers for mip level zero and array layer zero of a format.
 *
 * \param[in] format  Vulkan format
 *
 * \return The subresource layers.
 */
VkImageSubresourceLayers cvkGetFormatBaseSubresourceLayers(VkFormat format)
{
    return cvkGetFormatSubresourceLayers(format, 0, 0, 1);
}

/**
 * \brief Builds a subresource range covering every aspect of a format.
 *
 * \param[in] format          Vulkan format
 * \param[in] baseMipLevel    First mip level
 * \param[in] levelCount      Number of mip levels
 * \param[in] baseArrayLayer  First array layer
 * \param[in] layerCount      Number of array layers
 *
 * \return The subresource range.
 */
VkImageSubresourceRange cvkGetFormatSubresourceRange(VkFormat format, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount)
{
    VkImageSubresourceRange image_subresource_range;

    cvkInitImageSubresourceRange(&image_subresource_range, cvkGetFormatAspectMask(format), baseMipLevel, levelCount, baseArrayLayer, layerCount);
    return image_subresource_range;
}

/**
 * \brief Builds a subresource range for mip level zero and array layer zero of a format.
 *
 * \param[in] format  Vulkan format
 *
 * \return The subresource range.
 */
VkImageSubresourceRange cvkGetFormatBaseSubresourceRange(VkFormat format)
{
    return cvkGetFormatSubresourceRange(format, 0, 1, 0, 1);
}

/**
 * \brief Lays out every plane of an image linearly in one buffer with aligned rows.
 *
 * \par Details:
 *  Planes follow one another; each plane's rows are rounded up to rowAlignment bytes. Plane
 *  extents apply the format's chroma divisors; block-compressed planes are measured in
 *  texel blocks. Outputs have VKU_FORMAT_MAX_PLANES entries and unused entries are zero.
 *
 * \param[in]  format         Vulkan format
 * \param[in]  width          Image width in texels
 * \param[in]  height         Image height in texels
 * \param[in]  depth          Image depth in texels
 * \param[in]  layerCount     Number of array layers laid out consecutively per plane
 * \param[in]  rowAlignment   Row alignment in bytes, at least one
 * \param[out] pPlaneOffsets  Receives each plane's byte offset
 * \param[out] pPlaneStrides  Receives each plane's row pitch in bytes
 * \param[out] pPlaneExtents  Receives each plane's extent in texels
 * \param[out] pSize          Receives the total byte count
 *
 * \return The format's plane count.
 */
uint32_t cvkCalculateFormatPlaneLayouts(VkFormat      format,
                                        uint32_t      width,
                                        uint32_t      height,
                                        uint32_t      depth,
                                        uint32_t      layerCount,
                                        VkDeviceSize  rowAlignment,
                                        VkDeviceSize* pPlaneOffsets,
                                        uint32_t*     pPlaneStrides,
                                        VkExtent3D*   pPlaneExtents,
                                        VkDeviceSize* pSize)
{
    VkDeviceSize plane_offset;
    VkDeviceSize row_byte_count;
    VkDeviceSize aligned_row_byte_count;
    VkDeviceSize plane_byte_count;
    VkExtent3D   block_extent;
    VkExtent2D   divisors;
    VkFormat     plane_format;
    uint32_t     block_columns;
    uint32_t     block_rows;
    uint32_t     block_slices;
    uint32_t     plane_count;
    uint32_t     plane_index;

    plane_count  = vkuFormatPlaneCount(format);
    plane_offset = 0;
    (void)memset(pPlaneOffsets, 0, VKU_FORMAT_MAX_PLANES * sizeof(pPlaneOffsets[0]));
    (void)memset(pPlaneStrides, 0, VKU_FORMAT_MAX_PLANES * sizeof(pPlaneStrides[0]));
    (void)memset(pPlaneExtents, 0, VKU_FORMAT_MAX_PLANES * sizeof(pPlaneExtents[0]));
    for (plane_index = 0; plane_index < plane_count; ++plane_index)
    {
        if (1u == plane_count)
        {
            plane_format                      = format;
            pPlaneExtents[plane_index].width  = width;
            pPlaneExtents[plane_index].height = height;
        }
        else
        {
            plane_format                      = vkuFindMultiplaneCompatibleFormat(format, cvkGetFormatPlaneAspect(format, plane_index));
            divisors                          = vkuFindMultiplaneExtentDivisors(format, cvkGetFormatPlaneAspect(format, plane_index));
            pPlaneExtents[plane_index].width  = (width + divisors.width - 1u) / divisors.width;
            pPlaneExtents[plane_index].height = (height + divisors.height - 1u) / divisors.height;
        }
        pPlaneExtents[plane_index].depth = depth;
        block_extent                     = vkuFormatTexelBlockExtent(plane_format);
        block_columns                    = (pPlaneExtents[plane_index].width + block_extent.width - 1u) / block_extent.width;
        block_rows                       = (pPlaneExtents[plane_index].height + block_extent.height - 1u) / block_extent.height;
        block_slices                     = (pPlaneExtents[plane_index].depth + block_extent.depth - 1u) / block_extent.depth;
        row_byte_count                   = (VkDeviceSize)block_columns * vkuFormatTexelBlockSize(plane_format);
        aligned_row_byte_count           = ((row_byte_count + rowAlignment - 1u) / rowAlignment) * rowAlignment;
        plane_byte_count                 = aligned_row_byte_count * block_rows * block_slices * layerCount;
        pPlaneOffsets[plane_index]       = plane_offset;
        pPlaneStrides[plane_index]       = (uint32_t)aligned_row_byte_count;
        plane_offset += plane_byte_count;
    }
    (*pSize) = plane_offset;
    return plane_count;
}

/**
 * \brief Returns the tightly packed byte count of a 2D single-layer image of a format.
 *
 * \param[in] format  Vulkan format
 * \param[in] width   Image width in texels
 * \param[in] height  Image height in texels
 *
 * \return Total byte count of every plane laid out with row alignment one.
 */
VkDeviceSize cvkGetFormatSize(VkFormat format, uint32_t width, uint32_t height)
{
    VkDeviceSize plane_offsets[VKU_FORMAT_MAX_PLANES];
    uint32_t     plane_strides[VKU_FORMAT_MAX_PLANES];
    VkExtent3D   plane_extents[VKU_FORMAT_MAX_PLANES];
    VkDeviceSize size;

    (void)cvkCalculateFormatPlaneLayouts(format, width, height, 1, 1, 1, plane_offsets, plane_strides, plane_extents, &size);
    return size;
}

/**
 * \brief Builds the buffer-image copy regions transferring every plane of a subresource through a linear buffer.
 *
 * \par Details:
 *  One region per format plane, laid out by cvkCalculateFormatPlaneLayouts from bufferOffset.
 *  A region's bufferRowLength is zero when its rows are tightly packed, else the padded row
 *  length in texels. Image offsets are divided by the plane's chroma divisors.
 *
 * \param[in]  bufferOffset        Byte offset of the first plane in the buffer
 * \param[in]  bufferRowAlignment  Row alignment in bytes, at least one
 * \param[in]  imageFormat         Format of the image
 * \param[in]  imageOffset         Texel offset in the image
 * \param[in]  imageExtent         Texel extent in the image
 * \param[in]  imageSubresource    Mip level and array layers; the aspect is set per plane
 * \param[out] pRegions            Receives one region per plane; VKU_FORMAT_MAX_PLANES entries
 * \param[out] pTransferByteCount  Receives the byte count covered in the buffer
 * \param[out] pAspectMask         Receives the union of the plane aspects
 *
 * \return The number of regions written.
 */
uint32_t cvkGetBufferImageCopyRegions(VkDeviceSize             bufferOffset,
                                      VkDeviceSize             bufferRowAlignment,
                                      VkFormat                 imageFormat,
                                      VkOffset3D               imageOffset,
                                      VkExtent3D               imageExtent,
                                      VkImageSubresourceLayers imageSubresource,
                                      VkBufferImageCopy*       pRegions,
                                      VkDeviceSize*            pTransferByteCount,
                                      VkImageAspectFlags*      pAspectMask)
{
    VkDeviceSize             plane_offsets[VKU_FORMAT_MAX_PLANES];
    uint32_t                 plane_strides[VKU_FORMAT_MAX_PLANES];
    VkExtent3D               plane_extents[VKU_FORMAT_MAX_PLANES];
    VkImageSubresourceLayers plane_subresource;
    VkOffset3D               plane_offset;
    VkExtent2D               divisors;
    VkExtent3D               block_extent;
    VkDeviceSize             tight_row_byte_count;
    VkFormat                 plane_format;
    uint32_t                 plane_texel_size;
    uint32_t                 buffer_row_length;
    uint32_t                 plane_count;
    uint32_t                 plane_index;

    plane_count    = cvkCalculateFormatPlaneLayouts(imageFormat,
                                                    imageExtent.width,
                                                    imageExtent.height,
                                                    imageExtent.depth,
                                                    imageSubresource.layerCount,
                                                    bufferRowAlignment,
                                                    plane_offsets,
                                                    plane_strides,
                                                    plane_extents,
                                                    pTransferByteCount);
    (*pAspectMask) = 0;
    for (plane_index = 0; plane_index < plane_count; ++plane_index)
    {
        plane_offset = imageOffset;
        if (1u == plane_count)
        {
            plane_format = imageFormat;
        }
        else
        {
            plane_format   = vkuFindMultiplaneCompatibleFormat(imageFormat, cvkGetFormatPlaneAspect(imageFormat, plane_index));
            divisors       = vkuFindMultiplaneExtentDivisors(imageFormat, cvkGetFormatPlaneAspect(imageFormat, plane_index));
            plane_offset.x = imageOffset.x / (int32_t)divisors.width;
            plane_offset.y = imageOffset.y / (int32_t)divisors.height;
        }
        plane_texel_size     = vkuFormatTexelBlockSize(plane_format);
        block_extent         = vkuFormatTexelBlockExtent(plane_format);
        tight_row_byte_count = (VkDeviceSize)((plane_extents[plane_index].width + block_extent.width - 1u) / block_extent.width) * plane_texel_size;
        buffer_row_length    = (tight_row_byte_count == plane_strides[plane_index]) ? 0 : (plane_strides[plane_index] / plane_texel_size) * block_extent.width;
        cvkInitImageSubresourceLayers(&plane_subresource, cvkGetFormatPlaneAspect(imageFormat, plane_index), imageSubresource.mipLevel, imageSubresource.baseArrayLayer, imageSubresource.layerCount);
        cvkInitBufferImageCopy(&pRegions[plane_index], bufferOffset + plane_offsets[plane_index], buffer_row_length, 0, plane_subresource, plane_offset, plane_extents[plane_index]);
        (*pAspectMask) |= plane_subresource.aspectMask;
    }
    return plane_count;
}

/**
 * \brief Copies every plane of a subresource from a linear buffer into an image, with barriers and optional ownership transfers.
 *
 * \par Details:
 *  Records: an image barrier into the transfer layout (acquire, from the caller's stage and
 *  access), a buffer barrier to transfer read, one copy per plane laid out by
 *  cvkGetBufferImageCopyRegions, then an image barrier into the final layout (release, to
 *  the caller's stage and access). Queue family pairs are VK_QUEUE_FAMILY_IGNORED for no
 *  ownership transfer.
 *
 * \param[in] commandBuffer                    Command buffer recording the transfer
 * \param[in] dependencyFlags                  Dependency flags of every barrier
 * \param[in] pBufferBarrierNext               Optional extension chain of the buffer barrier, or NULL
 * \param[in] bufferSrcStageMask               Stage that last wrote the buffer
 * \param[in] bufferSrcAccessMask              Access that last wrote the buffer
 * \param[in] bufferSrcQueueFamilyIndex        Buffer ownership source
 * \param[in] bufferDstQueueFamilyIndex        Buffer ownership destination
 * \param[in] buffer                           Source buffer
 * \param[in] bufferOffset                     Byte offset of the first plane in the buffer
 * \param[in] bufferRowAlignment               Row alignment of the buffer layout in bytes
 * \param[in] image                            Destination image
 * \param[in] imageFormat                      Format of the image
 * \param[in] transferImageLayout              Layout of the image during the copy
 * \param[in] imageOffset                      Texel offset in the image
 * \param[in] imageExtent                      Texel extent in the image
 * \param[in] imageSubresource                 Mip level and array layers
 * \param[in] pImageAcquireBarrierNext         Optional extension chain of the acquiring image barrier, or NULL
 * \param[in] imageSrcStageMask                Stage that last used the image
 * \param[in] imageSrcAccessMask               Access that last used the image
 * \param[in] initialImageLayout               Layout of the image before the transfer
 * \param[in] imageAcquireSrcQueueFamilyIndex  Image ownership source before the transfer
 * \param[in] imageAcquireDstQueueFamilyIndex  Image ownership destination before the transfer
 * \param[in] pImageBarrierNext                Optional extension chain of the releasing image barrier, or NULL
 * \param[in] imageDstStageMask                Stage that next uses the image
 * \param[in] imageDstAccessMask               Access that next uses the image
 * \param[in] finalImageLayout                 Layout of the image after the transfer
 * \param[in] imageSrcQueueFamilyIndex         Image ownership source after the transfer
 * \param[in] imageDstQueueFamilyIndex         Image ownership destination after the transfer
 *
 * \retval None
 */
void cvkCmdTransferBufferToImageQueueOwnership(VkCommandBuffer          commandBuffer,
                                               VkDependencyFlags        dependencyFlags,
                                               const void*              pBufferBarrierNext,
                                               VkPipelineStageFlags     bufferSrcStageMask,
                                               VkAccessFlags            bufferSrcAccessMask,
                                               uint32_t                 bufferSrcQueueFamilyIndex,
                                               uint32_t                 bufferDstQueueFamilyIndex,
                                               VkBuffer                 buffer,
                                               VkDeviceSize             bufferOffset,
                                               VkDeviceSize             bufferRowAlignment,
                                               VkImage                  image,
                                               VkFormat                 imageFormat,
                                               VkImageLayout            transferImageLayout,
                                               VkOffset3D               imageOffset,
                                               VkExtent3D               imageExtent,
                                               VkImageSubresourceLayers imageSubresource,
                                               const void*              pImageAcquireBarrierNext,
                                               VkPipelineStageFlags     imageSrcStageMask,
                                               VkAccessFlags            imageSrcAccessMask,
                                               VkImageLayout            initialImageLayout,
                                               uint32_t                 imageAcquireSrcQueueFamilyIndex,
                                               uint32_t                 imageAcquireDstQueueFamilyIndex,
                                               const void*              pImageBarrierNext,
                                               VkPipelineStageFlags     imageDstStageMask,
                                               VkAccessFlags            imageDstAccessMask,
                                               VkImageLayout            finalImageLayout,
                                               uint32_t                 imageSrcQueueFamilyIndex,
                                               uint32_t                 imageDstQueueFamilyIndex)
{
    VkBufferImageCopy       regions[VKU_FORMAT_MAX_PLANES];
    VkImageSubresourceRange subresource_range;
    VkDeviceSize            transfer_byte_count;
    VkImageAspectFlags      aspect_mask;
    uint32_t                region_count;

    region_count = cvkGetBufferImageCopyRegions(bufferOffset, bufferRowAlignment, imageFormat, imageOffset, imageExtent, imageSubresource, regions, &transfer_byte_count, &aspect_mask);
    cvkInitImageSubresourceRange(&subresource_range, aspect_mask, imageSubresource.mipLevel, 1, imageSubresource.baseArrayLayer, imageSubresource.layerCount);

    cvkCmdImageQueueOwnershipBarrier(commandBuffer,
                                     pImageAcquireBarrierNext,
                                     imageSrcAccessMask,
                                     VK_ACCESS_TRANSFER_WRITE_BIT,
                                     initialImageLayout,
                                     transferImageLayout,
                                     imageAcquireSrcQueueFamilyIndex,
                                     imageAcquireDstQueueFamilyIndex,
                                     image,
                                     subresource_range,
                                     imageSrcStageMask,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     dependencyFlags);
    cvkCmdBufferQueueOwnershipBarrier(commandBuffer,
                                      pBufferBarrierNext,
                                      bufferSrcAccessMask,
                                      VK_ACCESS_TRANSFER_READ_BIT,
                                      bufferSrcQueueFamilyIndex,
                                      bufferDstQueueFamilyIndex,
                                      buffer,
                                      bufferOffset,
                                      transfer_byte_count,
                                      bufferSrcStageMask,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      dependencyFlags);
    cvkCmdCopyBufferToImage(commandBuffer, buffer, image, transferImageLayout, region_count, regions);
    cvkCmdImageQueueOwnershipBarrier(commandBuffer,
                                     pImageBarrierNext,
                                     VK_ACCESS_TRANSFER_WRITE_BIT,
                                     imageDstAccessMask,
                                     transferImageLayout,
                                     finalImageLayout,
                                     imageSrcQueueFamilyIndex,
                                     imageDstQueueFamilyIndex,
                                     image,
                                     subresource_range,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     imageDstStageMask,
                                     dependencyFlags);
}

/**
 * \brief Copies every plane of a subresource from an image into a linear buffer, with barriers and optional ownership transfers.
 *
 * \par Details:
 *  Records: an image barrier into the transfer layout (acquire), a buffer barrier to transfer
 *  write (acquire), one copy per plane laid out by cvkGetBufferImageCopyRegions, then an image
 *  barrier into the final layout and a buffer barrier to the caller's stage and access
 *  (release). Queue family pairs are VK_QUEUE_FAMILY_IGNORED for no ownership transfer.
 *
 * \param[in] commandBuffer                     Command buffer recording the transfer
 * \param[in] dependencyFlags                   Dependency flags of every barrier
 * \param[in] pImageAcquireBarrierNext          Optional extension chain of the acquiring image barrier, or NULL
 * \param[in] imageSrcStageMask                 Stage that last wrote the image
 * \param[in] imageSrcAccessMask                Access that last wrote the image
 * \param[in] initialImageLayout                Layout of the image before the transfer
 * \param[in] imageAcquireSrcQueueFamilyIndex   Image ownership source before the transfer
 * \param[in] imageAcquireDstQueueFamilyIndex   Image ownership destination before the transfer
 * \param[in] image                             Source image
 * \param[in] imageFormat                       Format of the image
 * \param[in] transferImageLayout               Layout of the image during the copy
 * \param[in] imageOffset                       Texel offset in the image
 * \param[in] imageExtent                       Texel extent in the image
 * \param[in] imageSubresource                  Mip level and array layers
 * \param[in] pBufferAcquireBarrierNext         Optional extension chain of the acquiring buffer barrier, or NULL
 * \param[in] bufferSrcStageMask                Stage that last used the buffer
 * \param[in] bufferSrcAccessMask               Access that last used the buffer
 * \param[in] bufferAcquireSrcQueueFamilyIndex  Buffer ownership source before the transfer
 * \param[in] bufferAcquireDstQueueFamilyIndex  Buffer ownership destination before the transfer
 * \param[in] buffer                            Destination buffer
 * \param[in] bufferOffset                      Byte offset of the first plane in the buffer
 * \param[in] bufferRowAlignment                Row alignment of the buffer layout in bytes
 * \param[in] pImageBarrierNext                 Optional extension chain of the releasing image barrier, or NULL
 * \param[in] imageDstStageMask                 Stage that next uses the image
 * \param[in] imageDstAccessMask                Access that next uses the image
 * \param[in] finalImageLayout                  Layout of the image after the transfer
 * \param[in] imageSrcQueueFamilyIndex          Image ownership source after the transfer
 * \param[in] imageDstQueueFamilyIndex          Image ownership destination after the transfer
 * \param[in] pBufferBarrierNext                Optional extension chain of the releasing buffer barrier, or NULL
 * \param[in] bufferDstStageMask                Stage that next uses the buffer
 * \param[in] bufferDstAccessMask               Access that next uses the buffer
 * \param[in] bufferSrcQueueFamilyIndex         Buffer ownership source after the transfer
 * \param[in] bufferDstQueueFamilyIndex         Buffer ownership destination after the transfer
 *
 * \retval None
 */
void cvkCmdTransferImageToBufferQueueOwnership(VkCommandBuffer          commandBuffer,
                                               VkDependencyFlags        dependencyFlags,
                                               const void*              pImageAcquireBarrierNext,
                                               VkPipelineStageFlags     imageSrcStageMask,
                                               VkAccessFlags            imageSrcAccessMask,
                                               VkImageLayout            initialImageLayout,
                                               uint32_t                 imageAcquireSrcQueueFamilyIndex,
                                               uint32_t                 imageAcquireDstQueueFamilyIndex,
                                               VkImage                  image,
                                               VkFormat                 imageFormat,
                                               VkImageLayout            transferImageLayout,
                                               VkOffset3D               imageOffset,
                                               VkExtent3D               imageExtent,
                                               VkImageSubresourceLayers imageSubresource,
                                               const void*              pBufferAcquireBarrierNext,
                                               VkPipelineStageFlags     bufferSrcStageMask,
                                               VkAccessFlags            bufferSrcAccessMask,
                                               uint32_t                 bufferAcquireSrcQueueFamilyIndex,
                                               uint32_t                 bufferAcquireDstQueueFamilyIndex,
                                               VkBuffer                 buffer,
                                               VkDeviceSize             bufferOffset,
                                               VkDeviceSize             bufferRowAlignment,
                                               const void*              pImageBarrierNext,
                                               VkPipelineStageFlags     imageDstStageMask,
                                               VkAccessFlags            imageDstAccessMask,
                                               VkImageLayout            finalImageLayout,
                                               uint32_t                 imageSrcQueueFamilyIndex,
                                               uint32_t                 imageDstQueueFamilyIndex,
                                               const void*              pBufferBarrierNext,
                                               VkPipelineStageFlags     bufferDstStageMask,
                                               VkAccessFlags            bufferDstAccessMask,
                                               uint32_t                 bufferSrcQueueFamilyIndex,
                                               uint32_t                 bufferDstQueueFamilyIndex)
{
    VkBufferImageCopy       regions[VKU_FORMAT_MAX_PLANES];
    VkImageSubresourceRange subresource_range;
    VkDeviceSize            transfer_byte_count;
    VkImageAspectFlags      aspect_mask;
    uint32_t                region_count;

    region_count = cvkGetBufferImageCopyRegions(bufferOffset, bufferRowAlignment, imageFormat, imageOffset, imageExtent, imageSubresource, regions, &transfer_byte_count, &aspect_mask);
    cvkInitImageSubresourceRange(&subresource_range, aspect_mask, imageSubresource.mipLevel, 1, imageSubresource.baseArrayLayer, imageSubresource.layerCount);

    cvkCmdImageQueueOwnershipBarrier(commandBuffer,
                                     pImageAcquireBarrierNext,
                                     imageSrcAccessMask,
                                     VK_ACCESS_TRANSFER_READ_BIT,
                                     initialImageLayout,
                                     transferImageLayout,
                                     imageAcquireSrcQueueFamilyIndex,
                                     imageAcquireDstQueueFamilyIndex,
                                     image,
                                     subresource_range,
                                     imageSrcStageMask,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     dependencyFlags);
    cvkCmdBufferQueueOwnershipBarrier(commandBuffer,
                                      pBufferAcquireBarrierNext,
                                      bufferSrcAccessMask,
                                      VK_ACCESS_TRANSFER_WRITE_BIT,
                                      bufferAcquireSrcQueueFamilyIndex,
                                      bufferAcquireDstQueueFamilyIndex,
                                      buffer,
                                      bufferOffset,
                                      transfer_byte_count,
                                      bufferSrcStageMask,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      dependencyFlags);
    cvkCmdCopyImageToBuffer(commandBuffer, image, transferImageLayout, buffer, region_count, regions);
    cvkCmdImageQueueOwnershipBarrier(commandBuffer,
                                     pImageBarrierNext,
                                     VK_ACCESS_TRANSFER_READ_BIT,
                                     imageDstAccessMask,
                                     transferImageLayout,
                                     finalImageLayout,
                                     imageSrcQueueFamilyIndex,
                                     imageDstQueueFamilyIndex,
                                     image,
                                     subresource_range,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     imageDstStageMask,
                                     dependencyFlags);
    cvkCmdBufferQueueOwnershipBarrier(commandBuffer,
                                      pBufferBarrierNext,
                                      VK_ACCESS_TRANSFER_WRITE_BIT,
                                      bufferDstAccessMask,
                                      bufferSrcQueueFamilyIndex,
                                      bufferDstQueueFamilyIndex,
                                      buffer,
                                      bufferOffset,
                                      transfer_byte_count,
                                      VK_PIPELINE_STAGE_TRANSFER_BIT,
                                      bufferDstStageMask,
                                      dependencyFlags);
}

/**
 * \brief Copies one region between two images, with barriers and optional ownership transfers.
 *
 * \par Details:
 *  Records: both images into their transfer layouts (acquire), one copy of the region, then
 *  both images into their final layouts (release). Queue family pairs are
 *  VK_QUEUE_FAMILY_IGNORED for no ownership transfer.
 *
 * \param[in] commandBuffer                Command buffer recording the transfer
 * \param[in] dependencyFlags              Dependency flags of every barrier
 * \param[in] pSrcAcquireBarrierNext       Optional extension chain of the source acquire barrier, or NULL
 * \param[in] srcStageMask                 Stage that last used the source
 * \param[in] srcAccessMask                Access that last used the source
 * \param[in] srcInitialLayout             Layout of the source before the transfer
 * \param[in] srcTransferLayout            Layout of the source during the copy
 * \param[in] srcAcquireQueueFamilyIndex   Source ownership before the transfer
 * \param[in] srcTransferQueueFamilyIndex  Source ownership during the transfer
 * \param[in] srcImage                     Source image
 * \param[in] srcSubresource               Source subresource layers
 * \param[in] srcOffset                    Source texel offset
 * \param[in] pDstAcquireBarrierNext       Optional extension chain of the destination acquire barrier, or NULL
 * \param[in] dstStageMask                 Stage that last used the destination
 * \param[in] dstAccessMask                Access that last used the destination
 * \param[in] dstInitialLayout             Layout of the destination before the transfer
 * \param[in] dstTransferLayout            Layout of the destination during the copy
 * \param[in] dstAcquireQueueFamilyIndex   Destination ownership before the transfer
 * \param[in] dstTransferQueueFamilyIndex  Destination ownership during the transfer
 * \param[in] dstImage                     Destination image
 * \param[in] dstSubresource               Destination subresource layers
 * \param[in] dstOffset                    Destination texel offset
 * \param[in] extent                       Texel extent copied
 * \param[in] pSrcBarrierNext              Optional extension chain of the source release barrier, or NULL
 * \param[in] srcDstStageMask              Stage that next uses the source
 * \param[in] srcDstAccessMask             Access that next uses the source
 * \param[in] srcFinalLayout               Layout of the source after the transfer
 * \param[in] srcReleaseQueueFamilyIndex   Source ownership during the release
 * \param[in] srcDstQueueFamilyIndex       Source ownership after the transfer
 * \param[in] pDstBarrierNext              Optional extension chain of the destination release barrier, or NULL
 * \param[in] dstDstStageMask              Stage that next uses the destination
 * \param[in] dstDstAccessMask             Access that next uses the destination
 * \param[in] dstFinalLayout               Layout of the destination after the transfer
 * \param[in] dstReleaseQueueFamilyIndex   Destination ownership during the release
 * \param[in] dstDstQueueFamilyIndex       Destination ownership after the transfer
 *
 * \retval None
 */
void cvkCmdTransferImageToImageQueueOwnership(VkCommandBuffer          commandBuffer,
                                              VkDependencyFlags        dependencyFlags,
                                              const void*              pSrcAcquireBarrierNext,
                                              VkPipelineStageFlags     srcStageMask,
                                              VkAccessFlags            srcAccessMask,
                                              VkImageLayout            srcInitialLayout,
                                              VkImageLayout            srcTransferLayout,
                                              uint32_t                 srcAcquireQueueFamilyIndex,
                                              uint32_t                 srcTransferQueueFamilyIndex,
                                              VkImage                  srcImage,
                                              VkImageSubresourceLayers srcSubresource,
                                              VkOffset3D               srcOffset,
                                              const void*              pDstAcquireBarrierNext,
                                              VkPipelineStageFlags     dstStageMask,
                                              VkAccessFlags            dstAccessMask,
                                              VkImageLayout            dstInitialLayout,
                                              VkImageLayout            dstTransferLayout,
                                              uint32_t                 dstAcquireQueueFamilyIndex,
                                              uint32_t                 dstTransferQueueFamilyIndex,
                                              VkImage                  dstImage,
                                              VkImageSubresourceLayers dstSubresource,
                                              VkOffset3D               dstOffset,
                                              VkExtent3D               extent,
                                              const void*              pSrcBarrierNext,
                                              VkPipelineStageFlags     srcDstStageMask,
                                              VkAccessFlags            srcDstAccessMask,
                                              VkImageLayout            srcFinalLayout,
                                              uint32_t                 srcReleaseQueueFamilyIndex,
                                              uint32_t                 srcDstQueueFamilyIndex,
                                              const void*              pDstBarrierNext,
                                              VkPipelineStageFlags     dstDstStageMask,
                                              VkAccessFlags            dstDstAccessMask,
                                              VkImageLayout            dstFinalLayout,
                                              uint32_t                 dstReleaseQueueFamilyIndex,
                                              uint32_t                 dstDstQueueFamilyIndex)
{
    VkImageSubresourceRange src_range;
    VkImageSubresourceRange dst_range;
    VkImageCopy             region;

    cvkInitImageSubresourceRange(&src_range, srcSubresource.aspectMask, srcSubresource.mipLevel, 1, srcSubresource.baseArrayLayer, srcSubresource.layerCount);
    cvkInitImageSubresourceRange(&dst_range, dstSubresource.aspectMask, dstSubresource.mipLevel, 1, dstSubresource.baseArrayLayer, dstSubresource.layerCount);
    cvkInitImageCopy(&region, srcSubresource, srcOffset, dstSubresource, dstOffset, extent);

    cvkCmdImageQueueOwnershipBarrier(commandBuffer,
                                     pSrcAcquireBarrierNext,
                                     srcAccessMask,
                                     VK_ACCESS_TRANSFER_READ_BIT,
                                     srcInitialLayout,
                                     srcTransferLayout,
                                     srcAcquireQueueFamilyIndex,
                                     srcTransferQueueFamilyIndex,
                                     srcImage,
                                     src_range,
                                     srcStageMask,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     dependencyFlags);
    cvkCmdImageQueueOwnershipBarrier(commandBuffer,
                                     pDstAcquireBarrierNext,
                                     dstAccessMask,
                                     VK_ACCESS_TRANSFER_WRITE_BIT,
                                     dstInitialLayout,
                                     dstTransferLayout,
                                     dstAcquireQueueFamilyIndex,
                                     dstTransferQueueFamilyIndex,
                                     dstImage,
                                     dst_range,
                                     dstStageMask,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     dependencyFlags);
    cvkCmdCopyImage(commandBuffer, srcImage, srcTransferLayout, dstImage, dstTransferLayout, 1, &region);
    cvkCmdImageQueueOwnershipBarrier(commandBuffer,
                                     pSrcBarrierNext,
                                     VK_ACCESS_TRANSFER_READ_BIT,
                                     srcDstAccessMask,
                                     srcTransferLayout,
                                     srcFinalLayout,
                                     srcReleaseQueueFamilyIndex,
                                     srcDstQueueFamilyIndex,
                                     srcImage,
                                     src_range,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     srcDstStageMask,
                                     dependencyFlags);
    cvkCmdImageQueueOwnershipBarrier(commandBuffer,
                                     pDstBarrierNext,
                                     VK_ACCESS_TRANSFER_WRITE_BIT,
                                     dstDstAccessMask,
                                     dstTransferLayout,
                                     dstFinalLayout,
                                     dstReleaseQueueFamilyIndex,
                                     dstDstQueueFamilyIndex,
                                     dstImage,
                                     dst_range,
                                     VK_PIPELINE_STAGE_TRANSFER_BIT,
                                     dstDstStageMask,
                                     dependencyFlags);
}

/**
 * \brief Blits one region between two images with a filter, with barriers (no ownership transfer).
 *
 * \par Details:
 *  Records: both images into their transfer layouts, one blit whose destination corner is the
 *  offset plus the extent, then both images into their final layouts.
 *
 * \param[in] commandBuffer           Command buffer recording the transfer
 * \param[in] dependencyFlags         Dependency flags of every barrier
 * \param[in] pSrcAcquireBarrierNext  Optional extension chain of the source acquire barrier, or NULL
 * \param[in] srcStageMask            Stage that last used the source
 * \param[in] srcAccessMask           Access that last used the source
 * \param[in] srcInitialLayout        Layout of the source before the transfer
 * \param[in] srcTransferLayout       Layout of the source during the blit
 * \param[in] srcImage                Source image
 * \param[in] srcSubresource          Source subresource layers
 * \param[in] srcOffset               Source texel offset
 * \param[in] srcExtent               Source texel extent
 * \param[in] pDstAcquireBarrierNext  Optional extension chain of the destination acquire barrier, or NULL
 * \param[in] dstStageMask            Stage that last used the destination
 * \param[in] dstAccessMask           Access that last used the destination
 * \param[in] dstInitialLayout        Layout of the destination before the transfer
 * \param[in] dstTransferLayout       Layout of the destination during the blit
 * \param[in] dstImage                Destination image
 * \param[in] dstSubresource          Destination subresource layers
 * \param[in] dstOffset               Destination texel offset
 * \param[in] dstExtent               Destination texel extent
 * \param[in] filter                  Filter applied by the blit
 * \param[in] pSrcBarrierNext         Optional extension chain of the source release barrier, or NULL
 * \param[in] srcDstStageMask         Stage that next uses the source
 * \param[in] srcDstAccessMask        Access that next uses the source
 * \param[in] srcFinalLayout          Layout of the source after the transfer
 * \param[in] pDstBarrierNext         Optional extension chain of the destination release barrier, or NULL
 * \param[in] dstDstStageMask         Stage that next uses the destination
 * \param[in] dstDstAccessMask        Access that next uses the destination
 * \param[in] dstFinalLayout          Layout of the destination after the transfer
 *
 * \retval None
 */
void cvkCmdTransferBlitImage(VkCommandBuffer          commandBuffer,
                             VkDependencyFlags        dependencyFlags,
                             const void*              pSrcAcquireBarrierNext,
                             VkPipelineStageFlags     srcStageMask,
                             VkAccessFlags            srcAccessMask,
                             VkImageLayout            srcInitialLayout,
                             VkImageLayout            srcTransferLayout,
                             VkImage                  srcImage,
                             VkImageSubresourceLayers srcSubresource,
                             VkOffset3D               srcOffset,
                             VkExtent3D               srcExtent,
                             const void*              pDstAcquireBarrierNext,
                             VkPipelineStageFlags     dstStageMask,
                             VkAccessFlags            dstAccessMask,
                             VkImageLayout            dstInitialLayout,
                             VkImageLayout            dstTransferLayout,
                             VkImage                  dstImage,
                             VkImageSubresourceLayers dstSubresource,
                             VkOffset3D               dstOffset,
                             VkExtent3D               dstExtent,
                             VkFilter                 filter,
                             const void*              pSrcBarrierNext,
                             VkPipelineStageFlags     srcDstStageMask,
                             VkAccessFlags            srcDstAccessMask,
                             VkImageLayout            srcFinalLayout,
                             const void*              pDstBarrierNext,
                             VkPipelineStageFlags     dstDstStageMask,
                             VkAccessFlags            dstDstAccessMask,
                             VkImageLayout            dstFinalLayout)
{
    VkImageSubresourceRange src_range;
    VkImageSubresourceRange dst_range;
    VkOffset3D              src_offsets[2];
    VkOffset3D              dst_offsets[2];
    VkImageBlit             region;

    cvkInitImageSubresourceRange(&src_range, srcSubresource.aspectMask, srcSubresource.mipLevel, 1, srcSubresource.baseArrayLayer, srcSubresource.layerCount);
    cvkInitImageSubresourceRange(&dst_range, dstSubresource.aspectMask, dstSubresource.mipLevel, 1, dstSubresource.baseArrayLayer, dstSubresource.layerCount);
    src_offsets[0] = srcOffset;
    cvkInitOffset3D(&src_offsets[1], srcOffset.x + (int32_t)srcExtent.width, srcOffset.y + (int32_t)srcExtent.height, srcOffset.z + (int32_t)srcExtent.depth);
    dst_offsets[0] = dstOffset;
    cvkInitOffset3D(&dst_offsets[1], dstOffset.x + (int32_t)dstExtent.width, dstOffset.y + (int32_t)dstExtent.height, dstOffset.z + (int32_t)dstExtent.depth);
    cvkInitImageBlit(&region, srcSubresource, src_offsets, dstSubresource, dst_offsets);

    cvkCmdImageBarrier(commandBuffer,
                       pSrcAcquireBarrierNext,
                       srcAccessMask,
                       VK_ACCESS_TRANSFER_READ_BIT,
                       srcInitialLayout,
                       srcTransferLayout,
                       srcImage,
                       src_range,
                       srcStageMask,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       dependencyFlags);
    cvkCmdImageBarrier(commandBuffer,
                       pDstAcquireBarrierNext,
                       dstAccessMask,
                       VK_ACCESS_TRANSFER_WRITE_BIT,
                       dstInitialLayout,
                       dstTransferLayout,
                       dstImage,
                       dst_range,
                       dstStageMask,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       dependencyFlags);
    cvkCmdBlitImage(commandBuffer, srcImage, srcTransferLayout, dstImage, dstTransferLayout, 1, &region, filter);
    cvkCmdImageBarrier(commandBuffer,
                       pSrcBarrierNext,
                       VK_ACCESS_TRANSFER_READ_BIT,
                       srcDstAccessMask,
                       srcTransferLayout,
                       srcFinalLayout,
                       srcImage,
                       src_range,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       srcDstStageMask,
                       dependencyFlags);
    cvkCmdImageBarrier(commandBuffer,
                       pDstBarrierNext,
                       VK_ACCESS_TRANSFER_WRITE_BIT,
                       dstDstAccessMask,
                       dstTransferLayout,
                       dstFinalLayout,
                       dstImage,
                       dst_range,
                       VK_PIPELINE_STAGE_TRANSFER_BIT,
                       dstDstStageMask,
                       dependencyFlags);
}
