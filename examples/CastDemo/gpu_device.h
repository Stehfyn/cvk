/* gpu_device.h - one Vulkan device shared by the GPU decoder and encoder.
**
** cvk (through volk) dispatches Vulkan calls via a single global device table, so a process can
** only drive one VkDevice at a time. The decoder and encoder therefore share this device, which
** carries the video decode, video encode and transfer queues and the extensions both need. */
#ifndef CAST_GPU_DEVICE_H
#define CAST_GPU_DEVICE_H

#include <stddef.h>

#include <volk.h>

typedef struct GpuDevice
{
    VkInstance       instance;
    VkPhysicalDevice physical;
    VkDevice         device;
    uint32_t         decodeFamily;
    uint32_t         encodeFamily;
    uint32_t         transferFamily;
    uint32_t         transferFamily2; /* a second transfer queue (dedicated DMA family if present) */
    VkQueue          decodeQueue;
    VkQueue          encodeQueue;
    VkQueue          transferQueue;
    VkQueue          transferQueue2;  /* so decode readback and encode upload never contend */
    int              hasDecode;
    int              hasEncode;
#ifndef NDEBUG
    VkDebugUtilsMessengerEXT messenger;
#endif
} GpuDevice;

/* Creates the shared device with the requested video capabilities plus a transfer queue. Writes a
** reason to pReport on failure. Returns NULL if the requested capabilities are unavailable. */
GpuDevice* GpuDevice_Create(int needDecode, int needEncode, char* pReport, size_t reportCapacity);

void GpuDevice_Destroy(GpuDevice* pDevice);

#endif
