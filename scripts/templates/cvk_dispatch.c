/* Cache records are thread-local; only the invalidation generation is shared.
 * Vulkan's external synchronization governs handle destruction and reuse.
 * Relaxed atomics suffice: the generation publishes no other shared data. */
#include <stdatomic.h>

static _Atomic(uint64_t) cvk_call_generation = 1;

void cvkInvalidateCallCache(void)
{
    (void)atomic_fetch_add_explicit(&cvk_call_generation, 1, memory_order_relaxed);
}

PFN_vkVoidFunction cvkGetCachedDeviceProcAddr(cvkCallCache* pCache, VkDevice device, const char* pName)
{
    uint64_t           generation;
    PFN_vkVoidFunction function;

    generation = atomic_load_explicit(&cvk_call_generation, memory_order_relaxed);
    if (pCache->function != NULL && pCache->handle == (uintptr_t)device && pCache->generation == generation)
    {
        return pCache->function;
    }
    function           = cvkGetDeviceProcAddr(device, pName);
    pCache->handle     = (uintptr_t)device;
    pCache->generation = generation;
    pCache->function   = function;
    return function;
}

PFN_vkVoidFunction cvkGetCachedInstanceProcAddr(cvkCallCache* pCache, VkInstance instance, const char* pName)
{
    uint64_t           generation;
    PFN_vkVoidFunction function;

    generation = atomic_load_explicit(&cvk_call_generation, memory_order_relaxed);
    if (pCache->function != NULL && pCache->handle == (uintptr_t)instance && pCache->generation == generation)
    {
        return pCache->function;
    }
    function           = cvkGetInstanceProcAddr(instance, pName);
    pCache->handle     = (uintptr_t)instance;
    pCache->generation = generation;
    pCache->function   = function;
    return function;
}
