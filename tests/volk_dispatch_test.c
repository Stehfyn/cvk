/* Exercise real Volk loading with mock entry points; no installed loader or GPU. */
#include <volk.h>
#include "cvk/cvk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression)                                                                                                                                                                              \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        if (!(expression))                                                                                                                                                                             \
        {                                                                                                                                                                                              \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);                                                                                                       \
            exit(1);                                                                                                                                                                                   \
        }                                                                                                                                                                                              \
    } while (0)

static unsigned lookups;
static unsigned observed;
static unsigned version = 1;
static unsigned destroyed;
static VkDevice expected_device;
static VkRect2D expected_scissor;
static VkViewport expected_viewport;
static unsigned scissor_calls;
static unsigned viewport_calls;

static VKAPI_ATTR void VKAPI_CALL set_scissor(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* scissors)
{
    CHECK(commandBuffer == (VkCommandBuffer)(uintptr_t)4);
    CHECK(firstScissor == 0 && scissorCount == 1 && scissors != NULL);
    CHECK(scissors[0].offset.x == expected_scissor.offset.x && scissors[0].offset.y == expected_scissor.offset.y);
    CHECK(scissors[0].extent.width == expected_scissor.extent.width && scissors[0].extent.height == expected_scissor.extent.height);
    ++scissor_calls;
}

static VKAPI_ATTR void VKAPI_CALL set_viewport(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* viewports)
{
    CHECK(commandBuffer == (VkCommandBuffer)(uintptr_t)4);
    CHECK(firstViewport == 0 && viewportCount == 1 && viewports != NULL);
    CHECK(viewports[0].x == expected_viewport.x && viewports[0].y == expected_viewport.y);
    CHECK(viewports[0].width == expected_viewport.width && viewports[0].height == expected_viewport.height);
    CHECK(viewports[0].minDepth == expected_viewport.minDepth && viewports[0].maxDepth == expected_viewport.maxDepth);
    ++viewport_calls;
}

static VKAPI_ATTR void VKAPI_CALL release_first(VkDevice device)
{
    CHECK(device == expected_device);
    observed = 1;
}

static VKAPI_ATTR void VKAPI_CALL release_second(VkDevice device)
{
    CHECK(device == expected_device);
    observed = 2;
}

static VKAPI_ATTR VkResult VKAPI_CALL wait_idle(VkDevice device)
{
    CHECK(device == expected_device);
    observed = 3;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL draw_mesh(VkCommandBuffer commandBuffer, uint32_t x, uint32_t y, uint32_t z)
{
    CHECK(commandBuffer == (VkCommandBuffer)(uintptr_t)4);
    CHECK(x == 2 && y == 3 && z == 1);
    observed = 4;
}

static VKAPI_ATTR VkResult VKAPI_CALL acquire_display(VkPhysicalDevice physicalDevice, int32_t drmFd, VkDisplayKHR display)
{
    CHECK(physicalDevice == (VkPhysicalDevice)(uintptr_t)3);
    CHECK(drmFd == 4 && display == (VkDisplayKHR)(uintptr_t)5);
    observed = 5;
    return VK_SUCCESS;
}

static VKAPI_ATTR VkResult VKAPI_CALL create_buffer(VkDevice device, const VkBufferCreateInfo* info, const VkAllocationCallbacks* allocator, VkBuffer* buffer)
{
    CHECK(device == expected_device && allocator == NULL);
    CHECK(info->sType == VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO && info->pNext == NULL);
    CHECK(info->size == 1024 && info->usage == VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    CHECK(info->sharingMode == VK_SHARING_MODE_EXCLUSIVE);
    CHECK(info->queueFamilyIndexCount == 0 && info->pQueueFamilyIndices == NULL);
    *buffer = (VkBuffer)(uintptr_t)6;
    return VK_SUCCESS;
}

static VKAPI_ATTR void VKAPI_CALL destroy_device(VkDevice device, const VkAllocationCallbacks* allocator)
{
    CHECK(device == expected_device && allocator == NULL);
    ++destroyed;
}

static VKAPI_ATTR void VKAPI_CALL destroy_instance(VkInstance instance, const VkAllocationCallbacks* allocator)
{
    CHECK(instance == (VkInstance)(uintptr_t)1 && allocator == NULL);
    ++destroyed;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL device_proc(VkDevice device, const char* name)
{
    CHECK(device == expected_device);
    ++lookups;
    if (strcmp(name, "vkReleaseProfilingLockKHR") == 0)
        return (PFN_vkVoidFunction)(version == 1 ? release_first : release_second);
    if (strcmp(name, "vkDeviceWaitIdle") == 0)
        return (PFN_vkVoidFunction)wait_idle;
    if (strcmp(name, "vkCmdDrawMeshTasksEXT") == 0)
        return (PFN_vkVoidFunction)draw_mesh;
    if (strcmp(name, "vkCmdSetScissor") == 0)
        return (PFN_vkVoidFunction)set_scissor;
    if (strcmp(name, "vkCmdSetViewport") == 0)
        return (PFN_vkVoidFunction)set_viewport;
    if (strcmp(name, "vkCreateBuffer") == 0)
        return (PFN_vkVoidFunction)create_buffer;
    if (strcmp(name, "vkDestroyDevice") == 0)
        return (PFN_vkVoidFunction)destroy_device;
    return NULL;
}

static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL instance_proc(VkInstance instance, const char* name)
{
    (void)instance;
    ++lookups;
    if (strcmp(name, "vkGetDeviceProcAddr") == 0)
        return (PFN_vkVoidFunction)device_proc;
    if (strcmp(name, "vkAcquireDrmDisplayEXT") == 0)
        return (PFN_vkVoidFunction)acquire_display;
    if (strcmp(name, "vkDestroyInstance") == 0)
        return (PFN_vkVoidFunction)destroy_instance;
    return NULL;
}

int main(void)
{
    VkInstance instance = (VkInstance)(uintptr_t)1;
    VkBuffer   buffer   = VK_NULL_HANDLE;
    unsigned   loaded_lookups;

    expected_device = (VkDevice)(uintptr_t)2;
    volkInitializeCustom(instance_proc);
    volkLoadInstanceOnly(instance);
    volkLoadDevice(expected_device);
    loaded_lookups = lookups;
    CHECK(loaded_lookups > 0);

    cvkReleaseProfilingLock(expected_device);
    CHECK(observed == 1);
    cvkDeviceWaitIdle(expected_device);
    CHECK(observed == 3);
    cvkCmdDrawMeshTasks((VkCommandBuffer)(uintptr_t)4, 2, 3, 1);
    CHECK(observed == 4);
    cvkAcquireDrmDisplay((VkPhysicalDevice)(uintptr_t)3, 4, (VkDisplayKHR)(uintptr_t)5);
    CHECK(observed == 5);
    cvkCreateBuffer(expected_device, NULL, 0, 1024, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, NULL, NULL, &buffer);
    CHECK(buffer == (VkBuffer)(uintptr_t)6);
    expected_scissor = (VkRect2D)
    {
        .offset = { 13, 17 },
        .extent = { 640, 480 }
    };
    expected_viewport = (VkViewport)
    {
        .x = 7.0f,
        .y = 9.0f,
        .width = 640.0f,
        .height = 480.0f,
        .minDepth = 0.25f,
        .maxDepth = 0.75f
    };
    cvkCmdSetScissor((VkCommandBuffer)(uintptr_t)4, expected_scissor);
    cvkCmdSetViewport((VkCommandBuffer)(uintptr_t)4, expected_viewport);
    CHECK(scissor_calls == 1 && viewport_calls == 1);
    expected_scissor.offset.x = 0;
    expected_scissor.offset.y = 0;
    expected_viewport.x = 0.0f;
    expected_viewport.y = 0.0f;
    expected_viewport.minDepth = 0.0f;
    expected_viewport.maxDepth = 1.0f;
    cvkCmdSetViewportAndScissor2D((VkCommandBuffer)(uintptr_t)4, expected_scissor.extent);
    CHECK(scissor_calls == 2 && viewport_calls == 2);
    cvkDestroyDevice(expected_device, NULL);
    CHECK(destroyed == 1 && lookups == loaded_lookups);

    /* Application reloads Volk after changing devices; wrappers see the new pointer. */
    expected_device = (VkDevice)(uintptr_t)7;
    version         = 2;
    volkLoadDevice(expected_device);
    CHECK(lookups > loaded_lookups);
    loaded_lookups = lookups;
    cvkReleaseProfilingLock(expected_device);
    CHECK(observed == 2);
    cvkDestroyDevice(expected_device, NULL);
    cvkDestroyInstance(instance, NULL);
    CHECK(destroyed == 3 && lookups == loaded_lookups);
    volkFinalize();
    CHECK(vkReleaseProfilingLockKHR == NULL);
    return 0;
}
