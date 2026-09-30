#ifndef CVK_TESTS_JSON_ALLOCATOR_H
#define CVK_TESTS_JSON_ALLOCATOR_H

#ifdef _WIN32

#include "cvk/win32/cvk_win32.h"

typedef struct JsonTestAllocator
{
    VkAllocationCallbacks callbacks;
} JsonTestAllocator;

#define JsonTestInitAllocator(allocator) cvkInitWin32AllocationCallbacks(&(allocator)->callbacks)

#else

#include <vulkan/vulkan_core.h>
#include <stdint.h>
#include <stdlib.h>
#ifdef _WIN32
#include <string.h>
#endif

typedef struct JsonTestAllocationHeader
{
    void*  base;
    size_t size;
} JsonTestAllocationHeader;

typedef struct JsonTestAllocator
{
    VkAllocationCallbacks callbacks;
    uint32_t              allocations;
    uint32_t              frees;
} JsonTestAllocator;

static void JsonTestCopy(void* destination, const void* source, size_t size)
{
#ifdef _WIN32
    int result = memcpy_s(destination, size, source, size);

    if (0 != result)
    {
        abort();
    }
#else
    uint8_t* output = (uint8_t*)destination;
    const uint8_t* input = (const uint8_t*)source;
    size_t index;

    for (index = 0; index < size; ++index)
    {
        output[index] = input[index];
    }
#endif
}

static void* VKAPI_CALL JsonTestAllocate(void* user, size_t size, size_t alignment, VkSystemAllocationScope scope)
{
    JsonTestAllocator* allocator = (JsonTestAllocator*)user;
    JsonTestAllocationHeader header;
    uint8_t* base;
    uint8_t* memory;
    size_t padding;

    (void)scope;
    if ((0 == size) || (0 == alignment))
    {
        return NULL;
    }
    if (alignment < sizeof(void*))
    {
        alignment = sizeof(void*);
    }
    if ((alignment > SIZE_MAX - sizeof(JsonTestAllocationHeader)) ||
        (size > SIZE_MAX - alignment - sizeof(JsonTestAllocationHeader)))
    {
        return NULL;
    }
    base = (uint8_t*)malloc(size + alignment + sizeof(JsonTestAllocationHeader));
    if (NULL == base)
    {
        return NULL;
    }
    memory = base + sizeof(JsonTestAllocationHeader);
    padding = (alignment - ((uintptr_t)memory % alignment)) % alignment;
    memory += padding;
    header.base = base;
    header.size = size;
    JsonTestCopy(memory - sizeof(header), &header, sizeof(header));
    ++allocator->allocations;
    return memory;
}

static void VKAPI_CALL JsonTestFree(void* user, void* memory)
{
    JsonTestAllocator* allocator = (JsonTestAllocator*)user;
    JsonTestAllocationHeader header;

    if (NULL != memory)
    {
        JsonTestCopy(&header, (uint8_t*)memory - sizeof(header), sizeof(header));
        free(header.base);
        ++allocator->frees;
    }
}

static void* VKAPI_CALL JsonTestReallocate(void* user, void* original, size_t size, size_t alignment, VkSystemAllocationScope scope)
{
    JsonTestAllocationHeader header;
    void* memory;
    size_t copySize;

    if (NULL == original)
    {
        return JsonTestAllocate(user, size, alignment, scope);
    }
    if (0 == size)
    {
        JsonTestFree(user, original);
        return NULL;
    }
    memory = JsonTestAllocate(user, size, alignment, scope);
    if (NULL != memory)
    {
        JsonTestCopy(&header, (uint8_t*)original - sizeof(header), sizeof(header));
        copySize = (header.size < size) ? header.size : size;
        JsonTestCopy(memory, original, copySize);
        JsonTestFree(user, original);
    }
    return memory;
}

static void JsonTestInitAllocator(JsonTestAllocator* allocator)
{
    (*allocator) = (JsonTestAllocator){0};
    allocator->callbacks.pUserData = allocator;
    allocator->callbacks.pfnAllocation = JsonTestAllocate;
    allocator->callbacks.pfnReallocation = JsonTestReallocate;
    allocator->callbacks.pfnFree = JsonTestFree;
}

#endif

#endif
