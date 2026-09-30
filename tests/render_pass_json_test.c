#include "cvk/cvk.h"
#include "cvk/cvk_json.h"
#include "json_allocator.h"

#include <volk.h>
#include <vulkan/vk_platform.h>
#include <vulkan/vulkan_core.h>
#include <stdint.h>
#include <stdio.h>

enum RenderPassJsonTestCapacity
{
    RENDER_PASS_TEXT_CAPACITY = 8192
};

struct VkDevice_T
{
    int marker;
};

struct VkRenderPass_T
{
    int marker;
};

static struct VkDevice_T     fixtureDevice;
static struct VkRenderPass_T fixtureRenderPass;
static VkImageLayout         expectedLayout;
static uint32_t              createdCount;
static JsonTestAllocator     fixtureAllocator;

static VKAPI_ATTR VkResult VKAPI_CALL CreateFixtureRenderPass(VkDevice device, const VkRenderPassCreateInfo* info, const VkAllocationCallbacks* allocator, VkRenderPass* renderPass)
{
    const VkAttachmentDescription* attachment;
    const VkSubpassDependency*     acquire;
    const VkSubpassDependency*     release;

    CVK_ASSERT(&fixtureDevice == device);
    CVK_ASSERT(&fixtureAllocator.callbacks == allocator);
    CVK_ASSERT(1 == info->attachmentCount);
    CVK_ASSERT(1 == info->subpassCount);
    CVK_ASSERT(2 == info->dependencyCount);
    attachment = &info->pAttachments[0];
    acquire    = &info->pDependencies[0];
    release    = &info->pDependencies[1];
    CVK_ASSERT(expectedLayout == attachment->initialLayout);
    CVK_ASSERT(expectedLayout == attachment->finalLayout);
    CVK_ASSERT(VK_FORMAT_B8G8R8A8_UNORM == attachment->format);
    CVK_ASSERT(VK_SUBPASS_EXTERNAL == acquire->srcSubpass);
    CVK_ASSERT(0 == acquire->dstSubpass);
    CVK_ASSERT(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT == acquire->srcStageMask);
    CVK_ASSERT(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT == acquire->dstStageMask);
    CVK_ASSERT(VK_ACCESS_SHADER_READ_BIT == acquire->srcAccessMask);
    CVK_ASSERT(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT == acquire->dstAccessMask);
    CVK_ASSERT(0 == release->srcSubpass);
    CVK_ASSERT(VK_SUBPASS_EXTERNAL == release->dstSubpass);
    CVK_ASSERT(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT == release->srcStageMask);
    CVK_ASSERT(VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT == release->dstStageMask);
    CVK_ASSERT(VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT == release->srcAccessMask);
    CVK_ASSERT(VK_ACCESS_SHADER_READ_BIT == release->dstAccessMask);
    (*renderPass) = &fixtureRenderPass;
    ++createdCount;
    return VK_SUCCESS;
}

static void CheckRenderPass(const char* path, VkImageLayout layout)
{
    static char         text[RENDER_PASS_TEXT_CAPACITY];
    VkRenderPass        renderPass;
    FILE*               file;
    size_t              length;
#ifdef _WIN32
    int                 openResult;
#endif

#ifdef _WIN32
    openResult = fopen_s(&file, path, "rb");
    CVK_ASSERT(0 == openResult);
#else
    file = fopen(path, "rb");
#endif
    CVK_ASSERT(NULL != file);
    length = fread(text, 1, sizeof(text), file);
    CVK_ASSERT((0 < length) && (length < sizeof(text)));
    CVK_ASSERT(0 == fclose(file));
    expectedLayout = layout;
    cvkCreateRenderPassFromPipelineJson(&fixtureDevice, text, length, &fixtureAllocator.callbacks, &renderPass);
    CVK_ASSERT(&fixtureRenderPass == renderPass);
}

int main(int argc, char** argv)
{
    static const size_t initialSize      = 17;
    static const size_t grownSize        = 37;
    static const size_t reducedSize      = 8;
    static const size_t initialAlignment = 64;
    static const size_t grownAlignment   = 65536;
    uint8_t* memory;

    CVK_ASSERT(3 == argc);
    JsonTestInitAllocator(&fixtureAllocator);
    memory = (uint8_t*)fixtureAllocator.callbacks.pfnAllocation(fixtureAllocator.callbacks.pUserData, initialSize, initialAlignment, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
    CVK_ASSERT(NULL != memory);
    CVK_ASSERT(0 == ((uintptr_t)memory % initialAlignment));
    memory[0]               = 'a';
    memory[initialSize - 1] = 'b';

    memory = (uint8_t*)fixtureAllocator.callbacks.pfnReallocation(fixtureAllocator.callbacks.pUserData, memory, grownSize, grownAlignment, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
    CVK_ASSERT(NULL != memory);
    CVK_ASSERT(0 == ((uintptr_t)memory % grownAlignment));
    CVK_ASSERT('a' == memory[0]);
    CVK_ASSERT('b' == memory[initialSize - 1]);
    memory[grownSize - 1] = 'c';

    memory = (uint8_t*)fixtureAllocator.callbacks.pfnReallocation(fixtureAllocator.callbacks.pUserData, memory, reducedSize, reducedSize, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
    CVK_ASSERT(NULL != memory);
    CVK_ASSERT(0 == ((uintptr_t)memory % reducedSize));
    CVK_ASSERT('a' == memory[0]);
    fixtureAllocator.callbacks.pfnFree(fixtureAllocator.callbacks.pUserData, memory);
    vkCreateRenderPass = CreateFixtureRenderPass;
    CheckRenderPass(argv[1], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    CheckRenderPass(argv[2], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    CVK_ASSERT(2 == createdCount);
    return 0;
}
