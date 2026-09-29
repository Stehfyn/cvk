/* gpu_device.c - the one Vulkan device shared by the GPU decoder and encoder. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vulkan/vulkan.h>
#include <volk.h>
#include "cvk/cvk.h"

#include "gpu_device.h"
#include "os/os_types.h"

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

#ifndef NDEBUG
/*************************************************************************************************/
/**
 * \par Details:
 *  Writes the validation message to stderr and returns without requesting that
 *  Vulkan abort the originating call.
 */
static VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
                                                 VkDebugUtilsMessageTypeFlagsEXT             type,
                                                 const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                 void*                                       user)
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
    VkPhysicalDeviceProperties               properties;
    bool_t                                   supports_api;
    bool_t                                   supports_sync2;
    bool_t                                   supports_extensions;
    uint32_t                                 decode_family;
    uint32_t                                 encode_family;
    uint32_t                                 transfer_family;
    uint32_t                                 transfer_family2;
    bool_t                                   has_decode;
    bool_t                                   has_encode;
    bool_t                                   has_transfer;
    bool_t                                   supports_decode;
    bool_t                                   supports_encode;
    bool_t                                   supports_video;
    bool_t                                   supports_queues;
    bool_t                                   distinct_transfer;
    bool_t                                   decode_uses_transfer;
    bool_t                                   decode_uses_transfer2;
    bool_t                                   decode_has_queue;
    bool_t                                   create_decode;
    bool_t                                   encode_uses_transfer;
    bool_t                                   encode_uses_transfer2;
    bool_t                                   encode_uses_decode;
    bool_t                                   encode_shares_decode;
    bool_t                                   encode_shares_transfer;
    bool_t                                   encode_has_queue;
    bool_t                                   create_encode;

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

    required_count               = 0;
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
        cvkCreateDebugUtilsMessenger(
            pDevice->instance,
            NULL,
            0,
            (VkDebugUtilsMessageSeverityFlagsEXT)((VkFlags)VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                                                  (VkFlags)VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT),
            (VkDebugUtilsMessageTypeFlagsEXT)((VkFlags)VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                              (VkFlags)VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT),
            Validation,
            NULL,
            NULL,
            &pDevice->messenger);
    }
#else
    cvkCreateInstance(NULL, 0, &app, 0, NULL, 0, NULL, NULL, &pDevice->instance);
    volkLoadInstance(pDevice->instance);
#endif

    decode_family    = VK_INVALID;
    encode_family    = VK_INVALID;
    transfer_family  = VK_INVALID;
    transfer_family2 = VK_INVALID;
    cvkEnumeratePhysicalDevices(pDevice->instance, &device_count, devices);
    for (i = 0; i < device_count; ++i)
    {
        uint32_t f;

        cvkGetPhysicalDeviceProperties(devices[i], &properties);
        supports_api = (VK_API_VERSION_1_3 <= properties.apiVersion);
        if (!supports_api)
        {
            continue;
        }
        cvkGetPhysicalDeviceSynchronization2Features(devices[i], NULL, &sync2);
        supports_sync2 = (VK_TRUE == sync2.synchronization2);
        if (!supports_sync2)
        {
            continue;
        }
        cvkEnumerateDeviceExtensionProperties(devices[i], NULL, &extension_count, device_extensions);
        supports_extensions =
            (VK_TRUE == cvkGetExtensionsSupport(extension_count, device_extensions, required_count, extensions));
        if (!supports_extensions)
        {
            continue;
        }
        decode_family    = VK_INVALID;
        encode_family    = VK_INVALID;
        transfer_family2 = VK_INVALID;
        if (needDecode)
        {
            decode_family =
                cvkGetPhysicalDeviceVideoQueueFamilyIndex(devices[i], VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR);
        }
        if (needEncode)
        {
            encode_family =
                cvkGetPhysicalDeviceVideoQueueFamilyIndex(devices[i], VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR);
        }
        cvkGetPhysicalDeviceQueueFamilyProperties(devices[i], &family_count, families);
        transfer_family = cvkGetQueueFamilyIndex(family_count, families, VK_QUEUE_TRANSFER_BIT);
        if (VK_INVALID == transfer_family)
        {
            transfer_family = cvkGetQueueFamilyIndex(family_count, families, VK_QUEUE_GRAPHICS_BIT);
        }
        if (VK_INVALID == transfer_family)
        {
            transfer_family = cvkGetQueueFamilyIndex(family_count, families, VK_QUEUE_COMPUTE_BIT);
        }
        /* A second, independent transfer queue so decode read-back and encode upload run in
        ** parallel. Prefer a dedicated transfer-only family (a DMA engine); else any other transfer
        ** family; else fall back to the first transfer family. */
        for (f = 0; f < family_count; ++f)
        {
            VkQueueFlags flags;
            bool_t       is_primary;
            bool_t       has_graphics;
            bool_t       has_compute;
            bool_t       has_explicit_transfer;
            bool_t       has_implicit_transfer;
            bool_t       can_transfer;
            bool_t       use_family;
            bool_t       needs_secondary;
            bool_t       is_dedicated;
            bool_t       prefer_family;

            flags                 = families[f].queueFlags;
            is_primary            = (f == transfer_family);
            has_graphics          = !!(flags & VK_QUEUE_GRAPHICS_BIT);
            has_compute           = !!(flags & VK_QUEUE_COMPUTE_BIT);
            has_explicit_transfer = !!(flags & VK_QUEUE_TRANSFER_BIT);
            has_implicit_transfer = has_graphics || has_compute;
            can_transfer          = has_explicit_transfer || has_implicit_transfer;
            use_family            = !is_primary && can_transfer;
            needs_secondary       = (VK_INVALID == transfer_family2);
            is_dedicated          = !has_implicit_transfer;
            prefer_family         = needs_secondary || is_dedicated;
            if (use_family && prefer_family)
            {
                transfer_family2 = f;
            }
        }
        has_decode      = (VK_INVALID != decode_family);
        has_encode      = (VK_INVALID != encode_family);
        has_transfer    = (VK_INVALID != transfer_family);
        supports_decode = !needDecode || has_decode;
        supports_encode = !needEncode || has_encode;
        supports_video  = supports_decode && supports_encode;
        supports_queues = supports_video && has_transfer;
        if (!supports_queues)
        {
            continue;
        }
        pDevice->physical = devices[i];
        break;
    }
    if (VK_NULL_HANDLE == pDevice->physical)
    {
        Report(pReport, reportCapacity, "no Vulkan 1.3 device with synchronization2 and the requested video queues\n");
        GpuDevice_Destroy(pDevice);
        return NULL;
    }
    pDevice->decodeFamily    = decode_family;
    pDevice->encodeFamily    = encode_family;
    pDevice->transferFamily  = transfer_family;
    pDevice->transferFamily2 = (VK_INVALID != transfer_family2) ? transfer_family2 : transfer_family;

    /* One VkDeviceQueueCreateInfo per distinct family among the ones we use. */
    priority    = 1.0f;
    queue_count = 0;
    cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count++], NULL, 0, pDevice->transferFamily, 1, &priority);
    distinct_transfer = (pDevice->transferFamily2 != pDevice->transferFamily);
    if (distinct_transfer)
    {
        cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count++], NULL, 0, pDevice->transferFamily2, 1, &priority);
    }
    decode_uses_transfer  = (pDevice->decodeFamily == pDevice->transferFamily);
    decode_uses_transfer2 = (pDevice->decodeFamily == pDevice->transferFamily2);
    decode_has_queue      = decode_uses_transfer || decode_uses_transfer2;
    create_decode         = needDecode && !decode_has_queue;
    if (create_decode)
    {
        cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count++], NULL, 0, pDevice->decodeFamily, 1, &priority);
    }
    encode_uses_transfer   = (pDevice->encodeFamily == pDevice->transferFamily);
    encode_uses_transfer2  = (pDevice->encodeFamily == pDevice->transferFamily2);
    encode_uses_decode     = (pDevice->encodeFamily == pDevice->decodeFamily);
    encode_shares_decode   = needDecode && encode_uses_decode;
    encode_shares_transfer = encode_uses_transfer || encode_uses_transfer2;
    encode_has_queue       = encode_shares_decode || encode_shares_transfer;
    create_encode          = needEncode && !encode_has_queue;
    if (create_encode)
    {
        cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count++], NULL, 0, pDevice->encodeFamily, 1, &priority);
    }

    cvkInitPhysicalDeviceSynchronization2Features(&sync2, NULL, VK_TRUE);
    cvkCreateDevice(pDevice->physical,
                    &sync2,
                    (VkDeviceCreateFlags)0,
                    queue_count,
                    queue_infos,
                    0,
                    NULL,
                    required_count,
                    extensions,
                    NULL,
                    NULL,
                    &pDevice->device);
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

    Report(pReport,
           reportCapacity,
           "device ready: decode qf %u, encode qf %u, transfer qf %u/%u\n",
           pDevice->decodeFamily,
           pDevice->encodeFamily,
           pDevice->transferFamily,
           pDevice->transferFamily2);
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
