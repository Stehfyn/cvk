#ifndef CAST_GPU_DEVICE_H
#define CAST_GPU_DEVICE_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * One Vulkan device shared by the H.264 decoder and encoder.
 * Device and enumeration storage belongs to the program's startup context.
 * The current CVK/Volk dispatch requires serialized native device lifetimes.
 * Native provider construction and blocking destruction remain lifecycle work.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#include <volk.h>
#include "cvk/cvk.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define GPU_SESSION_STORAGE_CAPACITY (65536U)
#define GPU_AUDIO_BUFFER_CAPACITY    (65536U)
#define GPU_SAMPLE_BUFFER_CAPACITY   (8U * 1024U * 1024U)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Stable caller-owned device state and bounded query workspace; do not copy a live owner.
 */
typedef struct GpuDevice
{
    union
    {
        uint64_t alignment;
        byte_t   bytes[GPU_SESSION_STORAGE_CAPACITY];
    } decoderStorage; ///< Startup-owned storage for one decoder session.
    union
    {
        uint64_t alignment;
        byte_t   bytes[GPU_SESSION_STORAGE_CAPACITY];
    } encoderStorage; ///< Startup-owned storage for one encoder session.
    union
    {
        uint64_t alignment;
        byte_t   bytes[GPU_SESSION_STORAGE_CAPACITY];
    } transcoderStorage; ///< Startup-owned storage for one transcode session.
    byte_t                  audioBuffer[GPU_AUDIO_BUFFER_CAPACITY]; ///< Reused AAC sample workspace.
    byte_t                  sampleBuffer[GPU_SAMPLE_BUFFER_CAPACITY]; ///< Reused encoded source sample workspace.
    bool_t                  decoderBusy; ///< One decoder may borrow the storage at a time.
    bool_t                  encoderBusy; ///< One encoder may borrow the storage at a time.
    bool_t                  transcoderBusy; ///< One transcoder may borrow the storage at a time.
    VkInstance              instance;        ///< Native instance, or VK_NULL_HANDLE when closed.
    VkPhysicalDevice        physical;        ///< Selected device with all requested capabilities.
    VkDevice                device;          ///< Shared logical device, or VK_NULL_HANDLE when closed.
    uint32_t                decodeFamily;    ///< H.264 decode family, or VK_INVALID when not requested.
    uint32_t                encodeFamily;    ///< H.264 encode family, or VK_INVALID when not requested.
    uint32_t                transferFamily;  ///< Primary transfer-capable family.
    uint32_t                transferFamily2; ///< Secondary transfer family, possibly the primary.
    VkQueue                 decodeQueue;     ///< Queue zero of the requested decode family.
    VkQueue                 encodeQueue;     ///< Queue zero of the requested encode family.
    VkQueue                 transferQueue;   ///< Primary transfer queue.
    VkQueue                 transferQueue2;  ///< Secondary transfer queue, possibly the primary.
    bool_t                  hasDecode;       ///< Decode was requested for the live device.
    bool_t                  hasEncode;       ///< Encode was requested for the live device.
    VkPhysicalDevice        devices[CVK_MAX_PHYSICAL_DEVICES];         ///< Startup-owned physical-device inventory.
    VkExtensionProperties   extensions[CVK_MAX_PROPERTIES];            ///< Reusable extension-query workspace.
    VkQueueFamilyProperties families[CVK_MAX_QUEUE_FAMILY_PROPERTIES]; ///< Transfer-queue inventory.
#ifndef NDEBUG
    VkDebugUtilsMessengerEXT messenger;                  ///< Optional validation-message subscription.
    VkLayerProperties        layers[CVK_MAX_PROPERTIES]; ///< Startup-owned validation-layer inventory.
#endif
} GpuDevice;

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Establishes empty device state in stable caller-owned startup storage.
 *
 * \par Requirements:
 *  - 001 A missing owner shall assert; the caller shall provide fresh or completely closed storage.
 *  - 002 This operation shall initialize ownership fields without allocating or loading a provider.
 *  - 003 Inventory arrays shall be written by CVK queries before they are read.
 *
 * \param[out] pDevice   Fresh storage retained through every device borrower and Destroy
 *
 * \retval     None
 */
void GpuDevice_Initialize(GpuDevice* pDevice);

/*************************************************************************************************/
/**
 * \brief Queries prerequisites and constructs the requested H.264 and transfer queues.
 *
 * \par Requirements:
 *  - 001 The initialized owner shall be closed, and report storage shall be non-NULL and nonempty; misuse shall assert.
 *  - 002 Loader/device API, synchronization2, extensions and H.264 queue support shall be queried before construction.
 *  - 003 Each requested family shall have exactly one creation record, including encode-only family zero.
 *  - 004 Enumeration and owner storage shall be reused without application allocation.
 *  - 005 Unsupported prerequisites shall leave the owner closed and write a bounded, terminated report.
 *  - 006 The caller shall serialize the process CVK/Volk dispatch lifetime and retain storage through Destroy.
 *
 * \param[in,out] pDevice        Initialized startup-owned device and query workspace
 * \param[in]     needDecode     Whether H.264 decoding is required
 * \param[in]     needEncode     Whether H.264 encoding is required
 * \param[out]    pReport        Required diagnostic destination
 * \param[in]     reportCapacity Complete writable report extent, including termination
 *
 * \retval        pDevice       The requested native queues are available.
 * \retval        NULL          Loader or device capabilities are unavailable; the owner remains reusable.
 */
GpuDevice* GpuDevice_Create(GpuDevice* pDevice,
                            bool_t     needDecode,
                            bool_t     needEncode,
                            char_t*    pReport,
                            uint32_t   reportCapacity);

/*************************************************************************************************/
/**
 * \brief Releases native device resources and restores empty caller-owned state.
 *
 * \par Details:
 *  The current native idle wait is blocking; replacing it remains required by the realtime goal.
 *
 * \par Requirements:
 *  - 001 The caller shall stop every device borrower before destruction.
 *  - 002 A NULL or already closed owner shall have no native release effect.
 *  - 003 Caller-owned storage and query arrays shall never be freed by this operation.
 *
 * \param[in,out] pDevice   Optional device whose borrowers have all stopped
 *
 * \retval        None
 */
void GpuDevice_Destroy(GpuDevice* pDevice);

#endif
