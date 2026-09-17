/* Format layouts and copy regions need no Vulkan loader or GPU. */
#include "cvk.h"
#include <stdio.h>

#define CHECK(expression)                                                                                                                                                                              \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        if (!(expression))                                                                                                                                                                             \
        {                                                                                                                                                                                              \
            (void)fprintf(stderr, "line %d: %s\n", __LINE__, #expression);                                                                                                                             \
            return 1;                                                                                                                                                                                  \
        }                                                                                                                                                                                              \
    } while (0)

int main(void)
{
    /* Expected byte layouts include row padding, partial compressed blocks,
     * and rounded-up chroma planes for odd image dimensions. */
    const struct
    {
        VkFormat     format;
        uint32_t     width, height, alignment, count;
        VkDeviceSize size;
        VkDeviceSize offsets[3];
        uint32_t     strides[3];
        VkExtent3D   extents[3];
    } cases[] = {
        {VK_FORMAT_R8G8B8A8_UNORM, 5, 3, 8, 1, 72, {0}, {24}, {{5, 3, 1}}},
        {VK_FORMAT_BC1_RGB_UNORM_BLOCK, 7, 5, 8, 1, 32, {0}, {16}, {{7, 5, 1}}},
        {VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, 5, 3, 4, 2, 40, {0, 24}, {8, 8}, {{5, 3, 1}, {3, 2, 1}}},
        {VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM, 5, 3, 4, 3, 40, {0, 24, 32}, {8, 4, 4}, {{5, 3, 1}, {3, 2, 1}, {3, 2, 1}}},
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        VkDeviceSize offsets[3], size;
        uint32_t     strides[3];
        VkExtent3D   extents[3];
        CHECK(cvkGetFormatPlaneCount(cases[i].format) == cases[i].count);
        CHECK(cvkCalculateFormatPlaneLayouts(cases[i].format, cases[i].width, cases[i].height, 1, 1, cases[i].alignment, offsets, strides, extents, &size) == cases[i].count);
        CHECK(size == cases[i].size);
        for (uint32_t plane = 0; plane < 3; ++plane)
        {
            CHECK(offsets[plane] == cases[i].offsets[plane]);
            CHECK(strides[plane] == cases[i].strides[plane]);
            CHECK(extents[plane].width == cases[i].extents[plane].width);
            CHECK(extents[plane].height == cases[i].extents[plane].height);
            CHECK(extents[plane].depth == cases[i].extents[plane].depth);
        }
    }

    CHECK(cvkGetFormatAspectMask(VK_FORMAT_R8G8B8A8_UNORM) == VK_IMAGE_ASPECT_COLOR_BIT);
    CHECK(cvkGetFormatAspectMask(VK_FORMAT_D32_SFLOAT) == VK_IMAGE_ASPECT_DEPTH_BIT);
    CHECK(cvkGetFormatAspectMask(VK_FORMAT_S8_UINT) == VK_IMAGE_ASPECT_STENCIL_BIT);
    CHECK(cvkGetFormatAspectMask(VK_FORMAT_D24_UNORM_S8_UINT) == (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT));

    VkBufferImageCopy              regions[3];
    VkDeviceSize                   transfer_size;
    VkImageAspectFlags             aspects;
    const VkOffset3D               offset      = {4, 2, 0};
    const VkExtent3D               extent      = {5, 3, 1};
    const VkImageSubresourceLayers subresource = {0, 2, 3, 2};
    CHECK(cvkGetBufferImageCopyRegions(16, 4, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, offset, extent, subresource, regions, &transfer_size, &aspects) == 2);
    CHECK(transfer_size == 80);
    CHECK(aspects == (VK_IMAGE_ASPECT_PLANE_0_BIT | VK_IMAGE_ASPECT_PLANE_1_BIT));
    CHECK(regions[0].bufferOffset == 16 && regions[1].bufferOffset == 64);
    CHECK(regions[0].bufferRowLength == 8 && regions[1].bufferRowLength == 4);
    CHECK(regions[1].imageOffset.x == 2 && regions[1].imageOffset.y == 1);
    CHECK(regions[1].imageExtent.width == 3 && regions[1].imageExtent.height == 2);
    CHECK(regions[1].imageSubresource.mipLevel == 2 && regions[1].imageSubresource.baseArrayLayer == 3);
    CHECK(regions[1].imageSubresource.layerCount == 2);
    return 0;
}
