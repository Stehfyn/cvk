/* gpu_device.c - the one Vulkan device shared by the GPU decoder and encoder. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <volk.h>
#include "cvk.h"

#include "gpu_device.h"

/*************************************************************************************************/
/**
 * \par Details:
 *  Appends formatted diagnostic text within the unused portion of the report buffer.
 */
static void Report(char* pReport, size_t capacity, const char* pFormat, ...)
{
    va_list arguments;
    size_t  used;

    used = pReport ? strlen(pReport) : 0;
    if (!pReport || used + 1 >= capacity)
    {
        return;
    }
    va_start(arguments, pFormat);
    (void)vsnprintf(pReport + used, capacity - used, pFormat, arguments);
    va_end(arguments);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Searches the advertised extension names for an exact match.
 */
static int HasExtension(const VkExtensionProperties* pExtensions, uint32_t count, const char* pName)
{
    uint32_t i;

    for (i = 0; i < count; ++i)
    {
        if (0 == strcmp(pExtensions[i].extensionName, pName))
        {
            return 1;
        }
    }
    return 0;
}

#ifndef NDEBUG
/*************************************************************************************************/
/**
 * \par Details:
 *  Writes the validation message to stderr and returns without requesting that
 *  Vulkan abort the originating call.
 */
static VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    (void)severity;
    (void)type;
    (void)user;
    fprintf(stderr, "vk: %s\n", data->pMessage);
    return VK_FALSE;
}
#endif

/*************************************************************************************************/
/**
 * \par Details:
 *  Selects video-capable queue families and creates one queue request per distinct
 *  family. Prefers a separate transfer family for decode readback when available.
 */
GpuDevice* GpuDevice_Create(int needDecode, int needEncode, char* pReport, size_t reportCapacity)
{
    GpuDevice*                               pDevice;
    VkApplicationInfo                        app;
    VkPhysicalDevice                         devices[CVK_MAX_PHYSICAL_DEVICES];
    VkQueueFamilyProperties                  families[CVK_MAX_QUEUE_FAMILY_PROPERTIES];
    VkExtensionProperties                    device_extensions[CVK_MAX_PROPERTIES];
    const char*                              extensions[6];
    VkDeviceQueueCreateInfo                  queue_infos[4];
    VkPhysicalDeviceSynchronization2Features sync2;
    float                                    priority;
    uint32_t                                 extension_count;
    uint32_t                                 device_count;
    uint32_t                                 family_count;
    uint32_t                                 queue_count;
    uint32_t                                 required_count;
    uint32_t                                 i;
    int                                      decode_family;
    int                                      encode_family;
    int                                      transfer_family;
    int                                      transfer_family2;

    pDevice = (GpuDevice*)calloc(1, sizeof(*pDevice));
    if (NULL == pDevice)
    {
        return NULL;
    }
    if (reportCapacity > 0)
    {
        pReport[0] = 0;
    }
    pDevice->hasDecode = needDecode;
    pDevice->hasEncode = needEncode;

    required_count             = 0;
    extensions[required_count++] = VK_KHR_VIDEO_QUEUE_EXTENSION_NAME;
    if (needDecode)
    {
        extensions[required_count++] = VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME;
        extensions[required_count++] = VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME;
    }
    if (needEncode)
    {
        extensions[required_count++] = VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME;
        extensions[required_count++] = VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME;
    }

    if (VK_SUCCESS != volkInitialize())
    {
        Report(pReport, reportCapacity, "no Vulkan loader\n");
        free(pDevice);
        return NULL;
    }
    cvkInitApplicationInfo(&app, NULL, "CastDemo transcode", 1, "cvk", 1, VK_API_VERSION_1_3);
#ifndef NDEBUG
    {
        const char* layers[]             = {"VK_LAYER_KHRONOS_validation"};
        const char* instanceExtensions[] = {VK_EXT_DEBUG_UTILS_EXTENSION_NAME};

        cvkCreateInstance(NULL, 0, &app, 1, layers, 1, instanceExtensions, NULL, &pDevice->instance);
        volkLoadInstance(pDevice->instance);
        cvkCreateDebugUtilsMessenger(pDevice->instance,
                                     NULL,
                                     0,
                                     (VkDebugUtilsMessageSeverityFlagsEXT)((VkFlags)VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | (VkFlags)VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT),
                                     (VkDebugUtilsMessageTypeFlagsEXT)((VkFlags)VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | (VkFlags)VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT),
                                     Validation,
                                     NULL,
                                     NULL,
                                     &pDevice->messenger);
    }
#else
    cvkCreateInstance(NULL, 0, &app, 0, NULL, 0, NULL, NULL, &pDevice->instance);
    volkLoadInstance(pDevice->instance);
#endif

    decode_family    = -1;
    encode_family    = -1;
    transfer_family  = -1;
    transfer_family2 = -1;
    cvkEnumeratePhysicalDevices(pDevice->instance, &device_count, devices);
    for (i = 0; i < device_count; ++i)
    {
        uint32_t f;
        int      ok;

        cvkEnumerateDeviceExtensionProperties(devices[i], NULL, &extension_count, device_extensions);
        ok              = 1;
        for (f = 0; f < required_count; ++f)
        {
            if (!HasExtension(device_extensions, extension_count, extensions[f]))
            {
                ok = 0;
                break;
            }
        }
        if (!ok)
        {
            continue;
        }
        decode_family    = -1;
        encode_family    = -1;
        transfer_family  = -1;
        transfer_family2 = -1;
        cvkGetPhysicalDeviceQueueFamilyProperties(devices[i], &family_count, families);
        for (f = 0; f < family_count; ++f)
        {
            const VkQueueFlags flags = families[f].queueFlags;

            if ((flags & (VkQueueFlags)VK_QUEUE_VIDEO_DECODE_BIT_KHR) && decode_family < 0)
            {
                decode_family = (int)f;
            }
            if ((flags & (VkQueueFlags)VK_QUEUE_VIDEO_ENCODE_BIT_KHR) && encode_family < 0)
            {
                encode_family = (int)f;
            }
            if ((flags & (VkQueueFlags)((VkFlags)VK_QUEUE_GRAPHICS_BIT | (VkFlags)VK_QUEUE_TRANSFER_BIT)) && transfer_family < 0)
            {
                transfer_family = (int)f;
            }
        }
        /* A second, independent transfer queue so decode read-back and encode upload run in
        ** parallel. Prefer a dedicated transfer-only family (a DMA engine); else any other transfer
        ** family; else fall back to the first transfer family. */
        for (f = 0; f < family_count; ++f)
        {
            const VkQueueFlags flags = families[f].queueFlags;

            if ((int)f == transfer_family || !(flags & (VkQueueFlags)VK_QUEUE_TRANSFER_BIT))
            {
                continue;
            }
            if (transfer_family2 < 0 || !(flags & (VkQueueFlags)((VkFlags)VK_QUEUE_GRAPHICS_BIT | (VkFlags)VK_QUEUE_COMPUTE_BIT)))
            {
                transfer_family2 = (int)f; /* keep improving toward a dedicated transfer-only family */
            }
        }
        if ((needDecode && decode_family < 0) || (needEncode && encode_family < 0) || transfer_family < 0)
        {
            continue;
        }
        pDevice->physical = devices[i];
        break;
    }
    if (VK_NULL_HANDLE == pDevice->physical)
    {
        Report(pReport, reportCapacity, "no Vulkan device with the requested video queues\n");
        GpuDevice_Destroy(pDevice);
        return NULL;
    }
    pDevice->decodeFamily    = (uint32_t)(decode_family >= 0 ? decode_family : 0);
    pDevice->encodeFamily    = (uint32_t)(encode_family >= 0 ? encode_family : 0);
    pDevice->transferFamily  = (uint32_t)transfer_family;
    pDevice->transferFamily2 = (uint32_t)(transfer_family2 >= 0 ? transfer_family2 : transfer_family);

    /* One VkDeviceQueueCreateInfo per distinct family among the ones we use. */
    priority    = 1.0f;
    queue_count = 0;
    cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count++], NULL, 0, pDevice->transferFamily, 1, &priority);
    if (pDevice->transferFamily2 != pDevice->transferFamily)
    {
        cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count++], NULL, 0, pDevice->transferFamily2, 1, &priority);
    }
    if (needDecode && pDevice->decodeFamily != pDevice->transferFamily && pDevice->decodeFamily != pDevice->transferFamily2)
    {
        cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count++], NULL, 0, pDevice->decodeFamily, 1, &priority);
    }
    if (needEncode && pDevice->encodeFamily != pDevice->transferFamily && pDevice->encodeFamily != pDevice->transferFamily2 && pDevice->encodeFamily != pDevice->decodeFamily)
    {
        cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count++], NULL, 0, pDevice->encodeFamily, 1, &priority);
    }

    cvkInitPhysicalDeviceSynchronization2Features(&sync2, NULL, VK_TRUE);
    cvkCreateDevice(pDevice->physical, &sync2, (VkDeviceCreateFlags)0, queue_count, queue_infos, 0, NULL, required_count, extensions, NULL, NULL, &pDevice->device);
    volkLoadDevice(pDevice->device);
    if (needDecode)
    {
        cvkGetDeviceQueue(pDevice->device, pDevice->decodeFamily, 0, &pDevice->decodeQueue);
    }
    if (needEncode)
    {
        cvkGetDeviceQueue(pDevice->device, pDevice->encodeFamily, 0, &pDevice->encodeQueue);
    }
    cvkGetDeviceQueue(pDevice->device, pDevice->transferFamily, 0, &pDevice->transferQueue);
    cvkGetDeviceQueue(pDevice->device, pDevice->transferFamily2, 0, &pDevice->transferQueue2);

    Report(pReport, reportCapacity, "device ready: decode qf %u, encode qf %u, transfer qf %u/%u\n", pDevice->decodeFamily, pDevice->encodeFamily, pDevice->transferFamily, pDevice->transferFamily2);
    return pDevice;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Waits for the device, releases the logical device and optional validation
 *  messenger, then destroys the instance and owner storage.
 */
void GpuDevice_Destroy(GpuDevice* pDevice)
{
    if (NULL == pDevice)
    {
        return;
    }
    if (VK_NULL_HANDLE != pDevice->device)
    {
        cvkDeviceWaitIdle(pDevice->device);
        cvkDestroyDevice(pDevice->device, NULL);
    }
#ifndef NDEBUG
    if (VK_NULL_HANDLE != pDevice->messenger)
    {
        cvkDestroyDebugUtilsMessenger(pDevice->instance, pDevice->messenger, NULL);
    }
#endif
    if (VK_NULL_HANDLE != pDevice->instance)
    {
        cvkDestroyInstance(pDevice->instance, NULL);
    }
    free(pDevice);
}
