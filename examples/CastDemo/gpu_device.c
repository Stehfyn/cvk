/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Vulkan queue capabilities and codec operations:
 * https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFamilyProperties.html
 * https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFamilyVideoPropertiesKHR.html
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "vk_diagnostic.h"
#include "gpu_device.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "crt/crt_memory.h"
#include "utl/utl_decimal.h"

#include <vulkan/vulkan_core.h>
#include <volk.h>
#include "cvk/cvk.h"
#include <stddef.h>
#include <stdint.h>
#ifndef NDEBUG
#include <vulkan/vk_platform.h>
#include <stdio.h>
#endif

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define MAX_QUEUE_REQUESTS (4)    ///< Two transfer roles and two video roles.
#define MAX_EXTENSIONS     (5)    ///< Common video queue plus decode and encode pairs.
#define REPORT_TERMINATOR  ('\0') ///< Terminator of the caller's UTF-8 diagnostic report.
#ifndef NDEBUG
#define VALIDATION_LAYER ("VK_LAYER_KHRONOS_validation") ///< Optional Khronos validation layer.
#endif
#define DECIMAL_CAPACITY (10) ///< Maximum decimal digits in a uint32_t queue-family index.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Counted report output with one byte reserved for termination.
 */
typedef struct Report_s
{
    char_t*  bytes;    ///< Required caller-owned diagnostic destination.
    uint32_t capacity; ///< Complete writable byte extent.
    uint32_t used;     ///< Written prefix, excluding its terminator.
} Report_t;

/*************************************************************************************************/
/**
 * \brief Requested device extensions retained through prerequisite queries and native construction.
 */
typedef struct DeviceRequest_s
{
    const char* extensions[MAX_EXTENSIONS]; ///< Requested Vulkan extension names.
    uint32_t    count;                      ///< Number of initialized names.
} DeviceRequest_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Appends a counted diagnostic span while reserving its terminator.
 *
 * \par Requirements:
 *  - 001 The initialized report shall have a nonzero capacity and used shall be smaller than capacity.
 *  - 002 Text shall provide byteCount readable bytes, disjoint from the report.
 *  - 003 Truncation shall preserve termination without allocation or CRT formatting.
 *
 * \param[in,out] report    Required initialized output cursor
 * \param[in]     text      Required readable text span
 * \param[in]     byteCount Complete input byte count
 *
 * \retval        None
 */
static FORCE_INLINE void WriteReportText(Report_t* report, const void* text, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Appends a uint32_t diagnostic value through the decimal specification writer.
 *
 * \par Requirements:
 *  - 001 Every input value shall fit the fixed decimal workspace.
 *  - 002 The report shall obey WriteReportText's cursor contract.
 *
 * \param[in,out] report Required initialized output cursor
 * \param[in]     value  Queue-family index
 *
 * \retval        None
 */
static FORCE_INLINE void WriteReportNumber(Report_t* report, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Queries loader API support before requesting a Vulkan instance.
 *
 * \par Requirements:
 *  - 001 The optional version entry point shall be checked before calling its CVK query.
 *  - 002 A Vulkan 1.0 loader shall be rejected without attempting instance creation.
 *
 * \retval TRUE   Vulkan 1.3 is supported.
 * \retval FALSE  The loader or required API version is unavailable.
 */
static FORCE_INLINE bool_t ReadLoaderSupport(void);

#ifndef NDEBUG
/*************************************************************************************************/
/**
 * \brief Receives provider validation messages without requesting native call abortion.
 *
 * \par Requirements:
 *  - 001 Callback data shall remain borrowed only for the duration of the callback.
 *  - 002 The current stderr transport remains pending migration to startup-owned diagnostics.
 *
 * \param[in] severity Native message severity
 * \param[in] type     Native message category
 * \param[in] data     Required native message record
 * \param[in] user     Unused native callback context
 *
 * \retval    VK_FALSE Continue the originating native operation.
 */
static VKAPI_ATTR VkBool32 VKAPI_CALL Validation(VkDebugUtilsMessageSeverityFlagBitsEXT      severity,
                                                 VkDebugUtilsMessageTypeFlagsEXT             type,
                                                 const VkDebugUtilsMessengerCallbackDataEXT* data,
                                                 void*                                       user);
#endif

/*************************************************************************************************/
/**
 * \brief Constructs the requested video extension set.
 *
 * \par Requirements:
 *  - 001 The request shall be required writable storage.
 *  - 002 At most five extension names shall be requested, with no duplicates.
 *
 * \param[out] request    Required request storage
 * \param[in] needDecode   H.264 decode requirement
 * \param[in] needEncode   H.264 encode requirement
 *
 * \retval None
 */
static FORCE_INLINE void InitializeRequest(DeviceRequest_t* request, bool_t needDecode, bool_t needEncode);

/*************************************************************************************************/
/**
 * \brief Creates the instance after loader API preflight.
 *
 * \par Requirements:
 *  - 001 The owner shall be closed and the loader shall support Vulkan 1.3.
 *  - 002 Optional debug layers and extensions shall be queried before requesting them.
 *
 * \param[in,out] pDevice Required closed owner
 *
 * \retval None
 */
static FORCE_INLINE void InitializeNativeInstance(GpuDevice* pDevice);

/*************************************************************************************************/
/**
 * \brief Queries device API, synchronization2 and extension prerequisites.
 *
 * \par Requirements:
 *  - 001 Required inputs shall identify a live instance and a complete request.
 *  - 002 No logical device shall be created by this operation.
 *
 * \param[in,out] pDevice Required owner providing enumeration workspace
 * \param[in] physical    Enumerated physical device
 * \param[in] request     Required extension set
 *
 * \retval TRUE   All non-queue prerequisites are supported.
 * \retval FALSE  At least one prerequisite is unavailable.
 */
static FORCE_INLINE bool_t ReadDeviceSupport(GpuDevice*             pDevice,
                                             VkPhysicalDevice       physical,
                                             const DeviceRequest_t* request);

/*************************************************************************************************/
/**
 * \brief Finds H.264 and transfer families for a supported physical device.
 *
 * \par Requirements:
 *  - 001 Video extensions shall have passed preflight before codec-operation queries.
 *  - 002 Only requested H.264 operations shall select video families.
 *  - 003 A complete family selection shall be committed only when every requested role is supported.
 *
 * \param[in,out] pDevice Required owner receiving complete family selection
 * \param[in] physical    Preflighted physical device
 *
 * \retval TRUE   Every requested queue role has a family.
 * \retval FALSE  A requested codec or transfer family is unavailable.
 */
static FORCE_INLINE bool_t ReadQueueFamilies(GpuDevice* pDevice, VkPhysicalDevice physical);

/*************************************************************************************************/
/**
 * \brief Selects the first physical device meeting the complete request.
 *
 * \par Requirements:
 *  - 001 Each candidate shall pass feature, extension and codec-family queries before selection.
 *  - 002 The owner shall have no selected physical device on entry.
 *
 * \param[in,out] pDevice Required closed owner
 * \param[in] request     Required extension set
 *
 * \retval TRUE   A complete device selection was committed.
 * \retval FALSE  No enumerated device supports the request.
 */
static FORCE_INLINE bool_t FindPhysicalDevice(GpuDevice* pDevice, const DeviceRequest_t* request);

/*************************************************************************************************/
/**
 * \brief Creates exactly one queue per requested distinct family.
 *
 * \par Requirements:
 *  - 001 Device and queue prerequisites shall have passed before this operation.
 *  - 002 An unused decode family shall never suppress an encoder queue request.
 *  - 003 Native queue retrieval shall follow creation of every requested family.
 *
 * \param[in,out] pDevice Required owner with a complete selection
 * \param[in] request     Preflighted extension set
 *
 * \retval None
 */
static FORCE_INLINE void CreateNativeDevice(GpuDevice* pDevice, const DeviceRequest_t* request);

/*************************************************************************************************/
/**
 * \brief Serializes the selected queue-family identifiers.
 *
 * \par Requirements:
 *  - 001 The report cursor shall be initialized and the device shall be completely constructed.
 *  - 002 Writes shall obey the counted report capacity, including termination.
 *
 * \param[in,out] report Required output cursor
 * \param[in] pDevice    Required complete device selection
 *
 * \retval None
 */
static FORCE_INLINE void WriteReadyReport(Report_t* report, const GpuDevice* pDevice);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The terminator is reserved before any text is copied. A full report stays full.
 */
static FORCE_INLINE void WriteReportText(Report_t* report, const void* text, uint32_t byteCount)
{
    uint32_t available;
    bool_t   fits;
    uint32_t copied;

    LOGASSERT(NULL != report);
    LOGASSERT(NULL != text);
    LOGASSERT(NULL != report->bytes);
    LOGASSERT(report->used < report->capacity);
    available = report->capacity - report->used - 1;
    fits      = (byteCount <= available);
    copied    = fits ? byteCount : available;
    CRT_Memory_Copy(&report->bytes[report->used], text, copied);
    report->used                += copied;
    report->bytes[report->used]  = REPORT_TERMINATOR;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every uint32_t value fits the fixed decimal workspace.
 */
static FORCE_INLINE void WriteReportNumber(Report_t* report, uint32_t value)
{
    byte_t   digits[DECIMAL_CAPACITY];
    uint32_t count;

    count = Utl_Decimal_WriteUint64(value, digits, DECIMAL_CAPACITY);
    LOGASSERT(0 < count);
    WriteReportText(report, digits, count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The existing process dispatch owns the loader module; this query never unloads it.
 */
static FORCE_INLINE bool_t ReadLoaderSupport(void)
{
    VkResult result;
    bool_t   ready;
    uint32_t version;
    bool_t   has_version_query;

    result = volkInitialize();
    ready  = (VK_SUCCESS == result);
    if (ready)
    {
        version           = VK_API_VERSION_1_0;
        has_version_query = (NULL != vkEnumerateInstanceVersion);
        if (has_version_query)
        {
            cvkEnumerateInstanceVersion(&version);
        }
        ready = (VK_API_VERSION_1_3 <= version);
    }
    return ready;
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
    cvkDiagnosticWrite("vk: ");
    cvkDiagnosticWrite(data->pMessage);
    cvkDiagnosticWrite("\n");
    return VK_FALSE;
}
#endif

/*************************************************************************************************/
/**
 * \par Details:
 *  Extension-name storage remains live until native device construction returns.
 */
static FORCE_INLINE void InitializeRequest(DeviceRequest_t* request, bool_t needDecode, bool_t needEncode)
{
    LOGASSERT(NULL != request);
    request->count                      = 0;
    request->extensions[request->count] = VK_KHR_VIDEO_QUEUE_EXTENSION_NAME;
    ++request->count;
    if (needDecode)
    {
        request->extensions[request->count] = VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME;
        ++request->count;
        request->extensions[request->count] = VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME;
        ++request->count;
    }
    if (needEncode)
    {
        request->extensions[request->count] = VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME;
        ++request->count;
        request->extensions[request->count] = VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME;
        ++request->count;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  CVK owns instance construction and its native failure contract.
 */
static FORCE_INLINE void InitializeNativeInstance(GpuDevice* pDevice)
{
    VkApplicationInfo app;

    cvkInitApplicationInfo(&app, NULL, "CastDemo transcode", 1, "cvk", 1, VK_API_VERSION_1_3);
#ifndef NDEBUG
    {
        uint32_t    layer_count;
        uint32_t    layer_index;
        bool_t      has_validation;
        bool_t      is_validation;
        const char* layers[1];
        const char* instance_extensions[1];
        bool_t      has_debug_utils;
        uint32_t    requested_layers;
        uint32_t    requested_extensions;

        cvkEnumerateInstanceLayerProperties(&layer_count, pDevice->layers);
        has_validation = FALSE;
        for (layer_index = 0; layer_index < layer_count; ++layer_index)
        {
            is_validation = (0 == CRT_Memory_Compare(pDevice->layers[layer_index].layerName,
                                                     VALIDATION_LAYER,
                                                     sizeof(VALIDATION_LAYER)));
            if (is_validation)
            {
                has_validation = TRUE;
                break;
            }
        }
        layers[0]              = VALIDATION_LAYER;
        instance_extensions[0] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        has_debug_utils =
            (VK_TRUE == cvkGetInstanceExtensionSupport(NULL, 1, instance_extensions, pDevice->extensions));
        requested_layers     = has_validation ? 1 : 0;
        requested_extensions = has_debug_utils ? 1 : 0;
        cvkCreateInstance(NULL,
                          0,
                          &app,
                          requested_layers,
                          layers,
                          requested_extensions,
                          instance_extensions,
                          NULL,
                          &pDevice->instance);
        volkLoadInstance(pDevice->instance);
        if (has_debug_utils)
        {
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
    }
#else
    cvkCreateInstance(NULL, 0, &app, 0, NULL, 0, NULL, NULL, &pDevice->instance);
    volkLoadInstance(pDevice->instance);
#endif
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Unsupported candidates stop before dependent queries.
 */
static FORCE_INLINE bool_t ReadDeviceSupport(GpuDevice*             pDevice,
                                             VkPhysicalDevice       physical,
                                             const DeviceRequest_t* request)
{
    VkPhysicalDeviceProperties               properties;
    bool_t                                   supports_api;
    bool_t                                   supports_sync2;
    VkPhysicalDeviceSynchronization2Features sync2;
    bool_t                                   supports_extensions;
    uint32_t                                 extension_count;

    cvkGetPhysicalDeviceProperties(physical, &properties);
    supports_api        = (VK_API_VERSION_1_3 <= properties.apiVersion);
    supports_sync2      = FALSE;
    supports_extensions = FALSE;
    if (supports_api)
    {
        cvkGetPhysicalDeviceSynchronization2Features(physical, NULL, &sync2);
        supports_sync2 = (VK_TRUE == sync2.synchronization2);
    }
    if (supports_sync2)
    {
        cvkEnumerateDeviceExtensionProperties(physical, NULL, &extension_count, pDevice->extensions);
        supports_extensions =
            (VK_TRUE ==
             cvkGetExtensionsSupport(extension_count, pDevice->extensions, request->count, request->extensions));
    }
    return supports_extensions;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A secondary transfer family is preferred when one exists; otherwise both transfer roles share queue zero.
 */
static FORCE_INLINE bool_t ReadQueueFamilies(GpuDevice* pDevice, VkPhysicalDevice physical)
{
    uint32_t decode_family;
    uint32_t encode_family;
    uint32_t transfer_family2;
    uint32_t family_count;
    uint32_t transfer_family;
    uint32_t family_index;
    bool_t   has_decode;
    bool_t   has_encode;
    bool_t   has_transfer;
    bool_t   supports_decode;
    bool_t   supports_encode;
    bool_t   supports_video;
    bool_t   supports_queues;

    decode_family    = VK_INVALID;
    encode_family    = VK_INVALID;
    transfer_family2 = VK_INVALID;
    if (pDevice->hasDecode)
    {
        decode_family =
            cvkGetPhysicalDeviceVideoQueueFamilyIndex(physical, VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR);
    }
    if (pDevice->hasEncode)
    {
        encode_family =
            cvkGetPhysicalDeviceVideoQueueFamilyIndex(physical, VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR);
    }
    cvkGetPhysicalDeviceQueueFamilyProperties(physical, &family_count, pDevice->families);
    transfer_family = cvkGetQueueFamilyIndex(family_count, pDevice->families, VK_QUEUE_TRANSFER_BIT);
    if (VK_INVALID == transfer_family)
    {
        transfer_family = cvkGetQueueFamilyIndex(family_count, pDevice->families, VK_QUEUE_GRAPHICS_BIT);
    }
    if (VK_INVALID == transfer_family)
    {
        transfer_family = cvkGetQueueFamilyIndex(family_count, pDevice->families, VK_QUEUE_COMPUTE_BIT);
    }
    /* A second, independent transfer queue so decode read-back and encode upload run in
    ** parallel. Prefer a dedicated transfer-only family (a DMA engine); else any other transfer
    ** family; else fall back to the first transfer family. */
    for (family_index = 0; family_index < family_count; ++family_index)
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

        flags                 = pDevice->families[family_index].queueFlags;
        is_primary            = (family_index == transfer_family);
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
            transfer_family2 = family_index;
        }
    }
    has_decode      = (VK_INVALID != decode_family);
    has_encode      = (VK_INVALID != encode_family);
    has_transfer    = (VK_INVALID != transfer_family);
    supports_decode = !pDevice->hasDecode || has_decode;
    supports_encode = !pDevice->hasEncode || has_encode;
    supports_video  = supports_decode && supports_encode;
    supports_queues = supports_video && has_transfer;
    if (supports_queues)
    {
        pDevice->decodeFamily    = decode_family;
        pDevice->encodeFamily    = encode_family;
        pDevice->transferFamily  = transfer_family;
        pDevice->transferFamily2 = (VK_INVALID != transfer_family2) ? transfer_family2 : transfer_family;
    }
    return supports_queues;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Enumeration uses caller-owned fixed-capacity workspace.
 */
static FORCE_INLINE bool_t FindPhysicalDevice(GpuDevice* pDevice, const DeviceRequest_t* request)
{
    uint32_t device_count;
    bool_t   supported;
    uint32_t index;

    cvkEnumeratePhysicalDevices(pDevice->instance, &device_count, pDevice->devices);
    supported = FALSE;
    for (index = 0; index < device_count; ++index)
    {
        supported = ReadDeviceSupport(pDevice, pDevice->devices[index], request);
        if (supported)
        {
            supported = ReadQueueFamilies(pDevice, pDevice->devices[index]);
        }
        if (supported)
        {
            pDevice->physical = pDevice->devices[index];
            break;
        }
    }
    return supported;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The queue priority and creation arrays remain live through the synchronous CVK construction call.
 */
static FORCE_INLINE void CreateNativeDevice(GpuDevice* pDevice, const DeviceRequest_t* request)
{
    float32_t                                priority;
    uint32_t                                 queue_count;
    VkDeviceQueueCreateInfo                  queue_infos[MAX_QUEUE_REQUESTS];
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
    VkPhysicalDeviceSynchronization2Features sync2;

    /* One VkDeviceQueueCreateInfo per distinct family among the ones we use. */
    priority    = 1.0F;
    queue_count = 0;
    cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count], NULL, 0, pDevice->transferFamily, 1, &priority);
    ++queue_count;
    distinct_transfer = (pDevice->transferFamily2 != pDevice->transferFamily);
    if (distinct_transfer)
    {
        cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count], NULL, 0, pDevice->transferFamily2, 1, &priority);
        ++queue_count;
    }
    decode_uses_transfer  = (pDevice->decodeFamily == pDevice->transferFamily);
    decode_uses_transfer2 = (pDevice->decodeFamily == pDevice->transferFamily2);
    decode_has_queue      = decode_uses_transfer || decode_uses_transfer2;
    create_decode         = pDevice->hasDecode && !decode_has_queue;
    if (create_decode)
    {
        cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count], NULL, 0, pDevice->decodeFamily, 1, &priority);
        ++queue_count;
    }
    encode_uses_transfer   = (pDevice->encodeFamily == pDevice->transferFamily);
    encode_uses_transfer2  = (pDevice->encodeFamily == pDevice->transferFamily2);
    encode_uses_decode     = (pDevice->encodeFamily == pDevice->decodeFamily);
    encode_shares_decode   = pDevice->hasDecode && encode_uses_decode;
    encode_shares_transfer = encode_uses_transfer || encode_uses_transfer2;
    encode_has_queue       = encode_shares_decode || encode_shares_transfer;
    create_encode          = pDevice->hasEncode && !encode_has_queue;
    if (create_encode)
    {
        cvkInitDeviceQueueCreateInfo(&queue_infos[queue_count], NULL, 0, pDevice->encodeFamily, 1, &priority);
        ++queue_count;
    }

    cvkInitPhysicalDeviceSynchronization2Features(&sync2, NULL, VK_TRUE);
    cvkCreateDevice(pDevice->physical,
                    &sync2,
                    (VkDeviceCreateFlags)0,
                    queue_count,
                    queue_infos,
                    0,
                    NULL,
                    request->count,
                    request->extensions,
                    NULL,
                    NULL,
                    &pDevice->device);
    volkLoadDevice(pDevice->device);
    if (pDevice->hasDecode)
    {
        cvkGetDeviceQueue(pDevice->device, pDevice->decodeFamily, 0, &pDevice->decodeQueue);
    }
    if (pDevice->hasEncode)
    {
        cvkGetDeviceQueue(pDevice->device, pDevice->encodeFamily, 0, &pDevice->encodeQueue);
    }
    cvkGetDeviceQueue(pDevice->device, pDevice->transferFamily, 0, &pDevice->transferQueue);
    cvkGetDeviceQueue(pDevice->device, pDevice->transferFamily2, 0, &pDevice->transferQueue2);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No allocation or variadic formatting is required.
 */
static FORCE_INLINE void WriteReadyReport(Report_t* report, const GpuDevice* pDevice)
{
    WriteReportText(report, "device ready: decode qf ", sizeof("device ready: decode qf ") - 1);
    WriteReportNumber(report, pDevice->decodeFamily);
    WriteReportText(report, ", encode qf ", sizeof(", encode qf ") - 1);
    WriteReportNumber(report, pDevice->encodeFamily);
    WriteReportText(report, ", transfer qf ", sizeof(", transfer qf ") - 1);
    WriteReportNumber(report, pDevice->transferFamily);
    WriteReportText(report, "/", sizeof("/") - 1);
    WriteReportNumber(report, pDevice->transferFamily2);
    WriteReportText(report, "\n", sizeof("\n") - 1);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Query arrays remain untouched until CVK fills their counted prefixes.
 */
void GpuDevice_Initialize(GpuDevice* pDevice)
{
    LOGASSERT(NULL != pDevice);
    pDevice->instance        = VK_NULL_HANDLE;
    pDevice->physical        = VK_NULL_HANDLE;
    pDevice->device          = VK_NULL_HANDLE;
    pDevice->decodeFamily    = VK_INVALID;
    pDevice->encodeFamily    = VK_INVALID;
    pDevice->transferFamily  = VK_INVALID;
    pDevice->transferFamily2 = VK_INVALID;
    pDevice->decodeQueue     = VK_NULL_HANDLE;
    pDevice->encodeQueue     = VK_NULL_HANDLE;
    pDevice->transferQueue   = VK_NULL_HANDLE;
    pDevice->transferQueue2  = VK_NULL_HANDLE;
    pDevice->hasDecode       = FALSE;
    pDevice->hasEncode       = FALSE;
#ifndef NDEBUG
    pDevice->messenger = VK_NULL_HANDLE;
#endif
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Selects video-capable queue families and creates one queue request per distinct
 *  family. Prefers a separate transfer family for decode readback when available.
 */
GpuDevice* GpuDevice_Create(GpuDevice* pDevice,
                            bool_t     needDecode,
                            bool_t     needEncode,
                            char_t*    pReport,
                            uint32_t   reportCapacity)
{
    Report_t        report;
    DeviceRequest_t request;
    bool_t          loader_ready;
    bool_t          device_found;

    LOGASSERT(NULL != pDevice);
    LOGASSERT(VK_NULL_HANDLE == pDevice->instance);
    LOGASSERT(VK_NULL_HANDLE == pDevice->device);
    LOGASSERT(VK_NULL_HANDLE == pDevice->physical);
    LOGASSERT(NULL != pReport);
    LOGASSERT(0 < reportCapacity);
    report.bytes       = pReport;
    report.capacity    = reportCapacity;
    report.used        = 0;
    pReport[0]         = REPORT_TERMINATOR;
    pDevice->hasDecode = needDecode;
    pDevice->hasEncode = needEncode;
    InitializeRequest(&request, needDecode, needEncode);
    loader_ready = ReadLoaderSupport();
    device_found = FALSE;
    if (loader_ready)
    {
        InitializeNativeInstance(pDevice);
        device_found = FindPhysicalDevice(pDevice, &request);
    }
    if (device_found)
    {
        CreateNativeDevice(pDevice, &request);
        WriteReadyReport(&report, pDevice);
    }
    else
    {
        if (loader_ready)
        {
            WriteReportText(&report,
                            "no Vulkan 1.3 device with synchronization2 and the requested H.264 queues\n",
                            sizeof("no Vulkan 1.3 device with synchronization2 and the requested H.264 queues\n") - 1);
        }
        else
        {
            WriteReportText(&report,
                            "Vulkan 1.3 loader is unavailable\n",
                            sizeof("Vulkan 1.3 loader is unavailable\n") - 1);
        }
        GpuDevice_Destroy(pDevice);
    }
    return device_found ? pDevice : NULL;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Waits for the device, releases the logical device and optional validation
 *  messenger and instance. Caller-owned storage remains available for reuse.
 */
void GpuDevice_Destroy(GpuDevice* pDevice)
{
    if (NULL != pDevice)
    {
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
        GpuDevice_Initialize(pDevice);
    }
}
