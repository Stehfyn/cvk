/* Exercise the generated wrappers with mock loader entry points; no GPU is used. */
#include "cvk.h"
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#define CHECK(expression)                                                                                                                                                                              \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        if (!(expression))                                                                                                                                                                             \
        {                                                                                                                                                                                              \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);                                                                                                       \
            exit(1);                                                                                                                                                                                   \
        }                                                                                                                                                                                              \
    } while (0)

static CVK_THREAD_LOCAL unsigned device_lookups;
static CVK_THREAD_LOCAL unsigned instance_lookups;
static CVK_THREAD_LOCAL unsigned observed;
static _Atomic(unsigned)         driver_version = 1;
static mtx_t                     mutex;
static cnd_t                     condition;
static unsigned                  phase;

static VKAPI_ATTR void VKAPI_CALL release_first(VkDevice device)
{
    CHECK(device == (VkDevice)(uintptr_t)1);
    observed = 1;
}

static VKAPI_ATTR void VKAPI_CALL release_second(VkDevice device)
{
    (void)device;
    observed = 2;
}

static VKAPI_ATTR VkResult VKAPI_CALL acquire_display(VkPhysicalDevice physicalDevice, int32_t drmFd, VkDisplayKHR display)
{
    CHECK(physicalDevice == (VkPhysicalDevice)(uintptr_t)3);
    CHECK(drmFd == 4);
    CHECK(display == (VkDisplayKHR)(uintptr_t)5);
    ++observed;
    return VK_SUCCESS;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetDeviceProcAddr(VkDevice device, const char* pName)
{
    CHECK(strcmp(pName, "vkReleaseProfilingLockKHR") == 0);
    ++device_lookups;
    return (PFN_vkVoidFunction)(device == (VkDevice)(uintptr_t)1 && atomic_load(&driver_version) == 1 ? release_first : release_second);
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    CHECK(instance == (VkInstance)(uintptr_t)1 || instance == (VkInstance)(uintptr_t)2);
    CHECK(strcmp(pName, "vkAcquireDrmDisplayEXT") == 0);
    ++instance_lookups;
    return (PFN_vkVoidFunction)acquire_display;
}

VKAPI_ATTR void VKAPI_CALL vkDestroyDevice(VkDevice device, const VkAllocationCallbacks* pAllocator)
{
    (void)device;
    (void)pAllocator;
    atomic_store(&driver_version, 2);
}

VKAPI_ATTR void VKAPI_CALL vkDestroyInstance(VkInstance instance, const VkAllocationCallbacks* pAllocator)
{
    (void)instance;
    (void)pAllocator;
}

static int worker(void* argument)
{
    VkDevice device = (VkDevice)(uintptr_t)1;
    (void)argument;
    cvkReleaseProfilingLock(device);
    cvkReleaseProfilingLock(device);
    CHECK(device_lookups == 1 && observed == 1);

    CHECK(mtx_lock(&mutex) == thrd_success);
    phase = 1;
    CHECK(cnd_signal(&condition) == thrd_success);
    while (phase != 2)
        CHECK(cnd_wait(&condition, &mutex) == thrd_success);
    CHECK(mtx_unlock(&mutex) == thrd_success);

    /* Main destroyed and recreated the same numeric handle on another thread. */
    cvkReleaseProfilingLock(device);
    CHECK(device_lookups == 2 && observed == 2);
    cvkReleaseProfilingLock(device);
    CHECK(device_lookups == 2);
    return 0;
}

int main(void)
{
    VkDevice   first    = (VkDevice)(uintptr_t)1;
    VkDevice   second   = (VkDevice)(uintptr_t)2;
    VkInstance instance = (VkInstance)(uintptr_t)1;
    thrd_t     thread;
    int        result;

    cvkReleaseProfilingLock(first);
    cvkReleaseProfilingLock(first);
    CHECK(device_lookups == 1 && observed == 1);
    cvkReleaseProfilingLock(second);
    CHECK(device_lookups == 2 && observed == 2);
    cvkReleaseProfilingLock(first);
    CHECK(device_lookups == 3 && observed == 1);

    cvkAcquireDrmDisplay(instance, (VkPhysicalDevice)(uintptr_t)3, 4, (VkDisplayKHR)(uintptr_t)5);
    cvkAcquireDrmDisplay(instance, (VkPhysicalDevice)(uintptr_t)3, 4, (VkDisplayKHR)(uintptr_t)5);
    CHECK(instance_lookups == 1);
    cvkAcquireDrmDisplay((VkInstance)(uintptr_t)2, (VkPhysicalDevice)(uintptr_t)3, 4, (VkDisplayKHR)(uintptr_t)5);
    CHECK(instance_lookups == 2);
    cvkDestroyInstance((VkInstance)(uintptr_t)2, NULL);
    cvkAcquireDrmDisplay((VkInstance)(uintptr_t)2, (VkPhysicalDevice)(uintptr_t)3, 4, (VkDisplayKHR)(uintptr_t)5);
    CHECK(instance_lookups == 3);

    CHECK(mtx_init(&mutex, mtx_plain) == thrd_success);
    CHECK(cnd_init(&condition) == thrd_success);
    CHECK(thrd_create(&thread, worker, NULL) == thrd_success);
    CHECK(mtx_lock(&mutex) == thrd_success);
    while (phase != 1)
        CHECK(cnd_wait(&condition, &mutex) == thrd_success);
    cvkDestroyDevice(first, NULL);
    phase = 2;
    CHECK(cnd_signal(&condition) == thrd_success);
    CHECK(mtx_unlock(&mutex) == thrd_success);
    CHECK(thrd_join(thread, &result) == thrd_success && result == 0);
    cnd_destroy(&condition);
    mtx_destroy(&mutex);

    cvkReleaseProfilingLock(first);
    CHECK(device_lookups == 4 && observed == 2);
    cvkInvalidateCallCache();
    cvkReleaseProfilingLock(first);
    CHECK(device_lookups == 5);
    return 0;
}
