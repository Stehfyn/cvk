/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_vulkan.h"
#include "os/os_assert.h"
#include "os/os_types.h"
#include <vulkan/utility/vk_format_utils.h>
#include <vulkan/vk_enum_string_helper.h>

#include <stdio.h>
#include <string.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define DEFAULT_SURFACE_FORMAT_INDEX (0U) ///< Deterministic fallback surface-format index.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

const VkComponentMapping c_vkIdentityComponentMapping = {
    VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY,
    VK_COMPONENT_SWIZZLE_IDENTITY,
};

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/* Scratch storage for serialized device setup; only the current counts are valid. */
static VkExtensionProperties c_vkDeviceExtensionProperties[VK_MAX_DEVICE_EXTENSIONS];
static const char*           c_vkDeviceExtensionNames[VK_MAX_DEVICE_EXTENSIONS];

static const VkDisplayPlaneAlphaFlagBitsKHR c_vkAlphaModes[] = {
    VK_DISPLAY_PLANE_ALPHA_OPAQUE_BIT_KHR,
    VK_DISPLAY_PLANE_ALPHA_GLOBAL_BIT_KHR,
    VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_BIT_KHR,
    VK_DISPLAY_PLANE_ALPHA_PER_PIXEL_PREMULTIPLIED_BIT_KHR,
};

static const VkCompositeAlphaFlagBitsKHR c_vkCompositeAlphaModes[] = {
    VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
    VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
    VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
};

static const VkSurfaceTransformFlagBitsKHR c_vkSurfaceTransforms[] = {
    VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
    VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR,
    VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR,
    VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR,
    VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_BIT_KHR,
    VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_90_BIT_KHR,
    VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_180_BIT_KHR,
    VK_SURFACE_TRANSFORM_HORIZONTAL_MIRROR_ROTATE_270_BIT_KHR,
    VK_SURFACE_TRANSFORM_INHERIT_BIT_KHR,
};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Loads the Vulkan loader and global entry points on first use.
 */
static void Initialize(void);

/*************************************************************************************************/
/**
 * \brief Aligns a mapped-memory offset down to a noncoherent atom boundary.
 *
 * \param[in] offset                     Requested byte offset into the allocation
 * \param[in] nonCoherentAtomSize        Nonzero power-of-two VkPhysicalDeviceLimits value
 *
 * \return Greatest aligned offset not exceeding offset.
 */
static VkDeviceSize GetNonCoherentMappedOffset(VkDeviceSize offset, VkDeviceSize nonCoherentAtomSize);

/*************************************************************************************************/
/**
 * \brief Tests an enumerated extension array for one exact extension name.
 *
 * \param[in] propertyCount                 Number of accessible VkExtensionProperties entries
 * \param[in] pProperties                 Enumerated properties; may be null only when count is zero
 * \param[in] pRequiredExtensionName        Null-terminated name fitting VK_MAX_EXTENSION_NAME_SIZE
 *
 * \retval VK_TRUE  An entry's complete fixed-width name matches.
 * \retval VK_FALSE No exact match exists.
 */
static VkBool32 GetExtensionSupport(uint32_t propertyCount, const VkExtensionProperties* pProperties, const char* pRequiredExtensionName);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void Initialize(void)
{
    if (NULL == vkGetInstanceProcAddr)
    {
        VKCHECK(volkInitialize());
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static VkDeviceSize GetNonCoherentMappedOffset(VkDeviceSize offset, VkDeviceSize nonCoherentAtomSize)
{
    LOGASSERT(0 != nonCoherentAtomSize);
    LOGASSERT(0 == (nonCoherentAtomSize & (nonCoherentAtomSize - 1u)));

    return offset & ~(nonCoherentAtomSize - 1u);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static VkBool32 GetExtensionSupport(uint32_t propertyCount, const VkExtensionProperties* pProperties, const char* pRequiredExtensionName)
{
    const void* extension_name_end;
    uint32_t property_index;

    LOGASSERT(NULL != pRequiredExtensionName);
    extension_name_end = memchr(pRequiredExtensionName, '\0', VK_MAX_EXTENSION_NAME_SIZE);
    LOGASSERT(NULL != extension_name_end);
    LOGASSERT(0 == propertyCount || NULL != pProperties);

    for (property_index = 0; property_index < propertyCount; ++property_index)
    {
        if (0 == strncmp(pProperties[property_index].extensionName, pRequiredExtensionName, VK_MAX_EXTENSION_NAME_SIZE))
        {
            return VK_TRUE;
        }
    }
    return VK_FALSE;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CheckError(VkResult result)
{
    LOGASSERT(VK_SUCCESS == result);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_InitAllocator(const VkAllocationCallbacks** ppAllocator)
{
    LOGASSERT(NULL != ppAllocator);

    (*ppAllocator) = NULL;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
PFN_vkVoidFunction Gfx_Vulkan_GetInstanceProcAddr(VkInstance instance, const char* pName)
{
    PFN_vkVoidFunction function;

    LOGASSERT(NULL != pName);

    Initialize();
    function = vkGetInstanceProcAddr(instance, pName);
    LOGASSERT(NULL != function);

    return function;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
PFN_vkVoidFunction Gfx_Vulkan_GetDeviceProcAddr(VkDevice device, const char* pName)
{
    PFN_vkVoidFunction function;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != pName);

    function = vkGetDeviceProcAddr(device, pName);
    LOGASSERT(NULL != function);

    return function;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateInstance(const void*                  pNext,
                               VkInstanceCreateFlags        flags,
                               const VkApplicationInfo*     pApplicationInfo,
                               uint32_t                     enabledLayerCount,
                               const char* const*           ppEnabledLayerNames,
                               uint32_t                     enabledExtensionCount,
                               const char* const*           ppEnabledExtensionNames,
                               const VkAllocationCallbacks* pAllocator,
                               VkInstance*                  pInstance)
{
    VkInstanceCreateInfo create_info;

    LOGASSERT(0 == enabledLayerCount || NULL != ppEnabledLayerNames);
    LOGASSERT(0 == enabledExtensionCount || NULL != ppEnabledExtensionNames);
    LOGASSERT(NULL != pInstance);

    create_info.sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    create_info.pNext                   = pNext;
    create_info.flags                   = flags;
    create_info.pApplicationInfo        = pApplicationInfo;
    create_info.enabledLayerCount       = enabledLayerCount;
    create_info.ppEnabledLayerNames     = ppEnabledLayerNames;
    create_info.enabledExtensionCount   = enabledExtensionCount;
    create_info.ppEnabledExtensionNames = ppEnabledExtensionNames;
    Initialize();
    VKCHECK(vkCreateInstance(&create_info, pAllocator, pInstance));
    LOGASSERT(VK_NULL_HANDLE != (*pInstance));
    volkLoadInstance((*pInstance));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VKAPI_ATTR VkBool32 VKAPI_CALL Gfx_Vulkan_WriteDebugUtilsMessage(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                                 VkDebugUtilsMessageTypeFlagsEXT             messageTypes,
                                                                 const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                                 void*                                       pUserData)
{
    const char* message_id_name;
    const char* message;
    int32_t     message_id_number;

    (void)pUserData;
    message_id_name   = NULL != pCallbackData && NULL != pCallbackData->pMessageIdName ? pCallbackData->pMessageIdName : "";
    message           = NULL != pCallbackData && NULL != pCallbackData->pMessage ? pCallbackData->pMessage : "";
    message_id_number = NULL != pCallbackData ? pCallbackData->messageIdNumber : 0;
    LOGSTART(stderr);
    LOGPRINTS("Vulkan debug utils: severity=0x");
    LOGPRINTX((uint64_t)((unsigned int)messageSeverity), 8, FALSE);
    LOGPRINTS(" types=0x");
    LOGPRINTX((uint64_t)((unsigned int)messageTypes), 8, FALSE);
    LOGPRINTS(" id=");
    LOGPRINTS(message_id_name);
    LOGPRINTS(" number=");
    LOGPRINTI((int64_t)(message_id_number));
    LOGPRINTS(" message=");
    LOGPRINTS(message);
    LOGPRINTS("\n");
    LOGEND();
    return VK_FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VKAPI_ATTR VkBool32 VKAPI_CALL Gfx_Vulkan_WriteDebugReportMessage(VkDebugReportFlagsEXT      flags,
                                                                  VkDebugReportObjectTypeEXT objectType,
                                                                  uint64_t                   object,
                                                                  size_t                     location,
                                                                  int32_t                    messageCode,
                                                                  const char*                pLayerPrefix,
                                                                  const char*                pMessage,
                                                                  void*                      pUserData)
{
    (void)pUserData;
    LOGSTART(stderr);
    LOGPRINTS("Vulkan debug report: flags=0x");
    LOGPRINTX((uint64_t)((unsigned int)flags), 8, FALSE);
    LOGPRINTS(" objectType=");
    LOGPRINTI((int64_t)((int)objectType));
    LOGPRINTS(" object=");
    LOGPRINTU((uint64_t)((unsigned long long)object));
    LOGPRINTS(" location=");
    LOGPRINTU((uint64_t)(location));
    LOGPRINTS(" code=");
    LOGPRINTI((int64_t)(messageCode));
    LOGPRINTS(" layer=");
    LOGPRINTS(NULL != pLayerPrefix ? pLayerPrefix : "");
    LOGPRINTS(" message=");
    LOGPRINTS(NULL != pMessage ? pMessage : "");
    LOGPRINTS("\n");
    LOGEND();
    return VK_FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateDebugUtilsMessenger(VkInstance                           instance,
                                          const void*                          pNext,
                                          VkDebugUtilsMessengerCreateFlagsEXT  flags,
                                          VkDebugUtilsMessageSeverityFlagsEXT  messageSeverity,
                                          VkDebugUtilsMessageTypeFlagsEXT      messageType,
                                          PFN_vkDebugUtilsMessengerCallbackEXT pfnCallback,
                                          void*                                pUserData,
                                          const VkAllocationCallbacks*         pAllocator,
                                          VkDebugUtilsMessengerEXT*            pMessenger)
{
    VkDebugUtilsMessengerCreateInfoEXT create_info;

    LOGASSERT(VK_NULL_HANDLE != instance);
    LOGASSERT((VkDebugUtilsMessageSeverityFlagsEXT)0 != messageSeverity);
    LOGASSERT((VkDebugUtilsMessageTypeFlagsEXT)0 != messageType);
    LOGASSERT(NULL != pfnCallback);
    LOGASSERT(NULL != pMessenger);

    create_info.sType           = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    create_info.pNext           = pNext;
    create_info.flags           = flags;
    create_info.messageSeverity = messageSeverity;
    create_info.messageType     = messageType;
    create_info.pfnUserCallback = pfnCallback;
    create_info.pUserData       = pUserData;
    LOGASSERT(NULL != vkCreateDebugUtilsMessengerEXT);
    VKCHECK(vkCreateDebugUtilsMessengerEXT(instance, &create_info, pAllocator, pMessenger));
    LOGASSERT(VK_NULL_HANDLE != (*pMessenger));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateDebugReportCallback(VkInstance                   instance,
                                          const void*                  pNext,
                                          VkDebugReportFlagsEXT        flags,
                                          PFN_vkDebugReportCallbackEXT pfnCallback,
                                          void*                        pUserData,
                                          const VkAllocationCallbacks* pAllocator,
                                          VkDebugReportCallbackEXT*    pCallback)
{
    VkDebugReportCallbackCreateInfoEXT create_info;

    LOGASSERT(VK_NULL_HANDLE != instance);
    LOGASSERT((VkDebugReportFlagsEXT)0 != flags);
    LOGASSERT(NULL != pfnCallback);
    LOGASSERT(NULL != pCallback);

    create_info.sType       = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
    create_info.pNext       = pNext;
    create_info.flags       = flags;
    create_info.pfnCallback = pfnCallback;
    create_info.pUserData   = pUserData;
    LOGASSERT(NULL != vkCreateDebugReportCallbackEXT);
    VKCHECK(vkCreateDebugReportCallbackEXT(instance, &create_info, pAllocator, pCallback));
    LOGASSERT(VK_NULL_HANDLE != (*pCallback));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateDebugInstance(const void*                         pNext,
                                    VkInstanceCreateFlags               instanceFlags,
                                    const VkApplicationInfo*            pApplicationInfo,
                                    uint32_t                            enabledLayerCount,
                                    const char* const*                  ppEnabledLayerNames,
                                    uint32_t                            enabledExtensionCount,
                                    const char* const*                  ppEnabledExtensionNames,
                                    VkDebugUtilsMessengerCreateFlagsEXT debugUtilsFlags,
                                    VkDebugUtilsMessageSeverityFlagsEXT debugUtilsMessageSeverity,
                                    VkDebugUtilsMessageTypeFlagsEXT     debugUtilsMessageType,
                                    VkDebugReportFlagsEXT               debugReportFlags,
                                    const VkAllocationCallbacks*        pAllocator,
                                    VkInstance*                         pInstance)
{
    const char*                        instance_extensions[VK_MAX_INSTANCE_EXTENSIONS];
    VkExtensionProperties              available_extensions[VK_MAX_INSTANCE_EXTENSIONS];
    VkDebugReportCallbackCreateInfoEXT debug_report_create_info;
    VkDebugUtilsMessengerCreateInfoEXT debug_utils_create_info;
    VkDebugReportCallbackEXT           debug_report_callback;
    VkDebugUtilsMessengerEXT           debug_utils_messenger;
    const void*                        p_diagnostic_create_info;
    VkBool32                           debug_report_supported;
    VkBool32                           debug_utils_supported;
    uint32_t                           instance_extension_count;
    uint32_t                           available_extension_count;
    uint32_t                           extension_index;

    LOGASSERT(0 == enabledLayerCount || NULL != ppEnabledLayerNames);
    LOGASSERT(VK_MAX_INSTANCE_EXTENSIONS >= enabledExtensionCount);
    LOGASSERT(0 == enabledExtensionCount || NULL != ppEnabledExtensionNames);
    LOGASSERT(NULL != pInstance);

    for (extension_index = 0; extension_index < enabledExtensionCount; ++extension_index)
    {
        instance_extensions[extension_index] = ppEnabledExtensionNames[extension_index];
    }
    instance_extension_count = enabledExtensionCount;

    Gfx_Vulkan_GetInstanceExtensionProperties(NULL, &available_extension_count, available_extensions);
    debug_report_supported   = Gfx_Vulkan_GetInstanceExtensionSupport(available_extension_count, available_extensions, VK_EXT_DEBUG_REPORT_EXTENSION_NAME);
    debug_utils_supported    = Gfx_Vulkan_GetInstanceExtensionSupport(available_extension_count, available_extensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    p_diagnostic_create_info = pNext;

    if (debug_report_supported)
    {
        LOGASSERT(VK_MAX_INSTANCE_EXTENSIONS > instance_extension_count);
        instance_extensions[instance_extension_count++] = VK_EXT_DEBUG_REPORT_EXTENSION_NAME;
        debug_report_create_info.sType                  = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT;
        debug_report_create_info.pNext                  = pNext;
        debug_report_create_info.flags                  = debugReportFlags;
        debug_report_create_info.pfnCallback            = Gfx_Vulkan_WriteDebugReportMessage;
        debug_report_create_info.pUserData              = NULL;
        p_diagnostic_create_info                        = &debug_report_create_info;
    }
    else if (debug_utils_supported)
    {
        LOGASSERT(VK_MAX_INSTANCE_EXTENSIONS > instance_extension_count);
        instance_extensions[instance_extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        debug_utils_create_info.sType                   = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        debug_utils_create_info.pNext                   = pNext;
        debug_utils_create_info.flags                   = debugUtilsFlags;
        debug_utils_create_info.messageSeverity         = debugUtilsMessageSeverity;
        debug_utils_create_info.messageType             = debugUtilsMessageType;
        debug_utils_create_info.pfnUserCallback         = Gfx_Vulkan_WriteDebugUtilsMessage;
        debug_utils_create_info.pUserData               = NULL;
        p_diagnostic_create_info                        = &debug_utils_create_info;
    }

    Gfx_Vulkan_CreateInstance(p_diagnostic_create_info, instanceFlags, pApplicationInfo, enabledLayerCount, ppEnabledLayerNames, instance_extension_count, instance_extensions, pAllocator, pInstance);

    if (debug_report_supported)
    {
        Gfx_Vulkan_CreateDebugReportCallback((*pInstance), NULL, debugReportFlags, Gfx_Vulkan_WriteDebugReportMessage, NULL, pAllocator, &debug_report_callback);
    }
    else if (debug_utils_supported)
    {
        Gfx_Vulkan_CreateDebugUtilsMessenger((*pInstance),
                                             NULL,
                                             debugUtilsFlags,
                                             debugUtilsMessageSeverity,
                                             debugUtilsMessageType,
                                             Gfx_Vulkan_WriteDebugUtilsMessage,
                                             NULL,
                                             pAllocator,
                                             &debug_utils_messenger);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties pProperties[VK_MAX_INSTANCE_EXTENSIONS])
{
    LOGASSERT(NULL != pPropertyCount);
    LOGASSERT(NULL != pProperties);

    Initialize();
    (*pPropertyCount) = 0;
    VKCHECK(vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, NULL));
    LOGASSERT(VK_MAX_INSTANCE_EXTENSIONS >= (*pPropertyCount));

    VKCHECK(vkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkBool32 Gfx_Vulkan_GetInstanceExtensionSupport(uint32_t propertyCount, const VkExtensionProperties* pProperties, const char* pRequiredExtensionName)
{
    LOGASSERT(VK_MAX_INSTANCE_EXTENSIONS >= propertyCount);
    return GetExtensionSupport(propertyCount, pProperties, pRequiredExtensionName);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice pPhysicalDevices[VK_MAX_PHYSICAL_DEVICES])
{
    uint32_t physical_device_index;

    LOGASSERT(VK_NULL_HANDLE != instance);
    LOGASSERT(NULL != pPhysicalDeviceCount);
    LOGASSERT(NULL != pPhysicalDevices);

    (*pPhysicalDeviceCount) = 0;
    VKCHECK(vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, NULL));
    LOGASSERT(VK_MAX_PHYSICAL_DEVICES >= (*pPhysicalDeviceCount));

    VKCHECK(vkEnumeratePhysicalDevices(instance, pPhysicalDeviceCount, pPhysicalDevices));
    for (physical_device_index = 0; physical_device_index < (*pPhysicalDeviceCount); ++physical_device_index)
    {
        LOGASSERT(VK_NULL_HANDLE != pPhysicalDevices[physical_device_index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties pProperties[VK_MAX_DEVICE_EXTENSIONS])
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pPropertyCount);
    LOGASSERT(NULL != pProperties);

    (*pPropertyCount) = 0;
    VKCHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, NULL));
    LOGASSERT(VK_MAX_DEVICE_EXTENSIONS >= (*pPropertyCount));

    VKCHECK(vkEnumerateDeviceExtensionProperties(physicalDevice, pLayerName, pPropertyCount, pProperties));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkBool32 Gfx_Vulkan_GetDeviceExtensionSupport(uint32_t propertyCount, const VkExtensionProperties* pProperties, const char* pRequiredExtensionName)
{
    LOGASSERT(VK_MAX_DEVICE_EXTENSIONS >= propertyCount);
    return GetExtensionSupport(propertyCount, pProperties, pRequiredExtensionName);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkBool32 Gfx_Vulkan_GetDeviceExtensionsSupport(uint32_t propertyCount, const VkExtensionProperties* pProperties, uint32_t requiredExtensionCount, const char* const* ppRequiredExtensionNames)
{
    VkBool32 supported;
    uint32_t required_extension_index;

    LOGASSERT(VK_MAX_DEVICE_EXTENSIONS >= propertyCount);
    LOGASSERT(0 == propertyCount || NULL != pProperties);
    LOGASSERT(0 == requiredExtensionCount || NULL != ppRequiredExtensionNames);

    supported = VK_TRUE;
    for (required_extension_index = 0; required_extension_index < requiredExtensionCount; ++required_extension_index)
    {
        LOGASSERT(NULL != ppRequiredExtensionNames[required_extension_index]);
        supported &= Gfx_Vulkan_GetDeviceExtensionSupport(propertyCount, pProperties, ppRequiredExtensionNames[required_extension_index]);
    }
    return supported;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, void* pNext, VkPhysicalDeviceProperties2* pProperties)
{
    VkPhysicalDeviceProperties2 properties;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pProperties);

    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = pNext;
    vkGetPhysicalDeviceProperties2(physicalDevice, &properties);

    (*pProperties) = properties;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceIdProperties(VkPhysicalDevice physicalDevice, void* pNext, VkPhysicalDeviceIDProperties* pProperties)
{
    VkPhysicalDeviceIDProperties properties;
    VkPhysicalDeviceProperties2  device_properties;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pProperties);

    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    properties.pNext = pNext;
    Gfx_Vulkan_GetPhysicalDeviceProperties2(physicalDevice, &properties, &device_properties);

    (*pProperties) = properties;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkBool32 Gfx_Vulkan_GetPhysicalDeviceLuid(VkPhysicalDevice physicalDevice, uint8_t pDeviceLuid[VK_LUID_SIZE])
{
    VkPhysicalDeviceIDProperties properties;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pDeviceLuid);

    Gfx_Vulkan_GetPhysicalDeviceIdProperties(physicalDevice, NULL, &properties);
    if (VK_FALSE == properties.deviceLUIDValid)
    {
        return VK_FALSE;
    }

    (void)memcpy(pDeviceLuid, properties.deviceLUID, VK_LUID_SIZE);
    return VK_TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDevicePciBusInfoProperties(VkPhysicalDevice physicalDevice, void* pNext, VkPhysicalDevicePCIBusInfoPropertiesEXT* pProperties)
{
    VkPhysicalDevicePCIBusInfoPropertiesEXT properties;
    VkPhysicalDeviceProperties2             device_properties;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pProperties);

    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PCI_BUS_INFO_PROPERTIES_EXT;
    properties.pNext = pNext;
    Gfx_Vulkan_GetPhysicalDeviceProperties2(physicalDevice, &properties, &device_properties);

    (*pProperties) = properties;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkPhysicalDevice Gfx_Vulkan_GetPhysicalDevice(VkInstance instance, uint64_t adapterId, uint32_t minimumApiVersion, uint32_t requiredExtensionCount, const char* const* ppRequiredExtensions)
{
    VkPhysicalDevice      devices[VK_MAX_PHYSICAL_DEVICES];
    VkExtensionProperties extensions[VK_MAX_DEVICE_EXTENSIONS];
    uint32_t              device_count;
    uint32_t              extension_count;
    uint32_t              device_index;

    LOGASSERT(VK_NULL_HANDLE != instance);
    LOGASSERT(0 != minimumApiVersion);
    LOGASSERT(VK_MAX_DEVICE_EXTENSIONS >= requiredExtensionCount);
    LOGASSERT(0 == requiredExtensionCount || NULL != ppRequiredExtensions);

    Gfx_Vulkan_GetPhysicalDevices(instance, &device_count, devices);
    for (device_index = 0; device_index < device_count; ++device_index)
    {
        VkPhysicalDeviceProperties2             properties;
        VkPhysicalDevicePCIBusInfoPropertiesEXT pci_properties;
        uint8_t                                 device_luid[VK_LUID_SIZE];

        Gfx_Vulkan_GetPhysicalDeviceProperties2(devices[device_index], NULL, &properties);
        if (minimumApiVersion > properties.properties.apiVersion)
        {
            continue;
        }
        Gfx_Vulkan_GetDeviceExtensionProperties(devices[device_index], NULL, &extension_count, extensions);
        if (VK_FALSE == Gfx_Vulkan_GetDeviceExtensionsSupport(extension_count, extensions, requiredExtensionCount, ppRequiredExtensions))
        {
            continue;
        }
        if (VK_TRUE == Gfx_Vulkan_GetPhysicalDeviceLuid(devices[device_index], device_luid))
        {
            if (0 != memcmp(device_luid, &adapterId, VK_LUID_SIZE))
            {
                continue;
            }
        }
        else
        {
            if (VK_FALSE == Gfx_Vulkan_GetDeviceExtensionSupport(extension_count, extensions, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME))
            {
                continue;
            }
            Gfx_Vulkan_GetPhysicalDevicePciBusInfoProperties(devices[device_index], NULL, &pci_properties);
            if (((uint32_t)(adapterId >> 24U) != pci_properties.pciDomain) || ((uint32_t)((adapterId >> 16U) & UINT8_MAX) != pci_properties.pciBus) ||
                ((uint32_t)((adapterId >> 8U) & UINT8_MAX) != pci_properties.pciDevice) || ((uint32_t)(adapterId & UINT8_MAX) != pci_properties.pciFunction))
            {
                continue;
            }
        }
        return devices[device_index];
    }
    LOGFATAL("adapter has no Vulkan physical device supporting the required API version and extensions");
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, VkQueueFamilyProperties pQueueFamilyProperties[VK_MAX_QUEUE_FAMILIES])
{
    uint32_t property_count;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pQueueFamilyProperties);

    property_count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &property_count, NULL);
    LOGASSERT(VK_MAX_QUEUE_FAMILIES >= property_count);

    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &property_count, pQueueFamilyProperties);
    return property_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties)
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(NULL != pFormatProperties);

    vkGetPhysicalDeviceFormatProperties(physicalDevice, format, pFormatProperties);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkFormat Gfx_Vulkan_SelectPhysicalDeviceSurfaceFormat(VkPhysicalDevice     physicalDevice,
                                                      VkSurfaceKHR         surface,
                                                      uint32_t             preferredFormatCount,
                                                      const VkFormat*      pPreferredFormats,
                                                      VkFormatFeatureFlags requiredOptimalTilingFeatures)
{
    VkSurfaceFormatKHR surface_formats[VK_MAX_SURFACE_FORMATS];
    VkFormatProperties properties;
    uint32_t           surface_format_count;
    uint32_t           surface_format_index;
    uint32_t           preferred_format_index;
    VkBool32           surface_format_supported;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != surface);
    LOGASSERT(0 != preferredFormatCount);
    LOGASSERT(NULL != pPreferredFormats);

    Gfx_Vulkan_GetPhysicalDeviceSurfaceFormats(physicalDevice, surface, &surface_format_count, surface_formats);
    for (preferred_format_index = 0; preferred_format_index < preferredFormatCount; ++preferred_format_index)
    {
        LOGASSERT(VK_FORMAT_UNDEFINED != pPreferredFormats[preferred_format_index]);
        surface_format_supported = VK_FALSE;
        if ((1U == surface_format_count) && (VK_FORMAT_UNDEFINED == surface_formats[0].format))
        {
            surface_format_supported = VK_TRUE;
        }
        else
        {
            for (surface_format_index = 0; surface_format_index < surface_format_count; ++surface_format_index)
            {
                if (pPreferredFormats[preferred_format_index] == surface_formats[surface_format_index].format)
                {
                    surface_format_supported = VK_TRUE;
                    break;
                }
            }
        }
        if (VK_FALSE == surface_format_supported)
        {
            continue;
        }
        Gfx_Vulkan_GetPhysicalDeviceFormatProperties(physicalDevice, pPreferredFormats[preferred_format_index], &properties);
        if (requiredOptimalTilingFeatures == (properties.optimalTilingFeatures & requiredOptimalTilingFeatures))
        {
            return pPreferredFormats[preferred_format_index];
        }
    }
    LOGFATAL("Vulkan presentation surface supports no preferred format with the required features");
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, void* pNext, VkFormatProperties2* pFormatProperties)
{
    VkFormatProperties2 properties;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(NULL != pFormatProperties);

    (void)memset(&properties, 0, sizeof(properties));
    properties.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2;
    properties.pNext = pNext;
    vkGetPhysicalDeviceFormatProperties2(physicalDevice, format, &properties);

    (*pFormatProperties) = properties;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice                        physicalDevice,
                                                        const VkPhysicalDeviceImageFormatInfo2* pInfo,
                                                        void*                                   pPropertiesNext,
                                                        VkImageFormatProperties2*               pImageFormatProperties)
{
    VkImageFormatProperties2 properties;
    VkResult                 result;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pInfo);
    LOGASSERT(NULL != pImageFormatProperties);

    (void)memset(&properties, 0, sizeof(properties));
    properties.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
    properties.pNext = pPropertiesNext;
    result           = vkGetPhysicalDeviceImageFormatProperties2(physicalDevice, pInfo, &properties);
    if (VK_SUCCESS == result)
    {
        (*pImageFormatProperties) = properties;
    }
    else
    {
        (void)memset(pImageFormatProperties, 0, sizeof((*pImageFormatProperties)));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceImageFormatInfo2(VkPhysicalDevice          physicalDevice,
                                                  const void*               pNext,
                                                  VkImageCreateFlags        flags,
                                                  VkImageType               imageType,
                                                  VkFormat                  format,
                                                  VkImageTiling             tiling,
                                                  VkImageUsageFlags         usage,
                                                  void*                     pPropertiesNext,
                                                  VkImageFormatProperties2* pImageFormatProperties)
{
    VkPhysicalDeviceImageFormatInfo2 image_info;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT((VkImageUsageFlags)0 != usage);
    LOGASSERT(NULL != pImageFormatProperties);

    image_info.sType  = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
    image_info.pNext  = pNext;
    image_info.flags  = flags;
    image_info.format = format;
    image_info.type   = imageType;
    image_info.tiling = tiling;
    image_info.usage  = usage;
    Gfx_Vulkan_GetPhysicalDeviceImageFormatProperties2(physicalDevice, &image_info, pPropertiesNext, pImageFormatProperties);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceImageMemoryProperties(VkPhysicalDevice            physicalDevice,
                                                       const void*                 pNext,
                                                       VkImageCreateFlags          flags,
                                                       VkImageType                 imageType,
                                                       VkFormat                    format,
                                                       VkImageTiling               tiling,
                                                       VkImageUsageFlags           usage,
                                                       VkExternalMemoryProperties* pExternalMemoryProperties)
{
    VkExternalImageFormatProperties external_properties;
    VkImageFormatProperties2        image_properties;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT((VkImageUsageFlags)0 != usage);
    LOGASSERT(NULL != pExternalMemoryProperties);

    (void)memset(&external_properties, 0, sizeof(external_properties));
    external_properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
    external_properties.pNext = NULL;
    Gfx_Vulkan_GetPhysicalDeviceImageFormatInfo2(physicalDevice, pNext, flags, imageType, format, tiling, usage, &external_properties, &image_properties);

    (*pExternalMemoryProperties) = external_properties.externalMemoryProperties;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceExternalImageMemoryProperties(VkPhysicalDevice                   physicalDevice,
                                                               const void*                        pNext,
                                                               VkImageCreateFlags                 flags,
                                                               VkImageType                        imageType,
                                                               VkFormat                           format,
                                                               VkImageTiling                      tiling,
                                                               VkImageUsageFlags                  usage,
                                                               VkExternalMemoryHandleTypeFlagBits handleType,
                                                               VkExternalMemoryProperties*        pExternalMemoryProperties)
{
    VkPhysicalDeviceExternalImageFormatInfo external_info;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT((VkImageUsageFlags)0 != usage);
    LOGASSERT(NULL != pExternalMemoryProperties);

    external_info.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
    external_info.pNext      = pNext;
    external_info.handleType = handleType;
    Gfx_Vulkan_GetPhysicalDeviceImageMemoryProperties(physicalDevice, &external_info, flags, imageType, format, tiling, usage, pExternalMemoryProperties);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Vulkan_GetPhysicalDeviceExternalImageImportSupport(VkPhysicalDevice                   physicalDevice,
                                                              const void*                        pNext,
                                                              VkImageCreateFlags                 flags,
                                                              VkImageType                        imageType,
                                                              VkFormat                           format,
                                                              VkImageTiling                      tiling,
                                                              VkImageUsageFlags                  usage,
                                                              VkExternalMemoryHandleTypeFlagBits handleType,
                                                              bool_t*                            pDedicatedOnly)
{
    VkExternalMemoryProperties properties;
    bool_t                     is_importable;
    bool_t                     is_compatible;

    LOGASSERT(NULL != pDedicatedOnly);

    Gfx_Vulkan_GetPhysicalDeviceExternalImageMemoryProperties(physicalDevice, pNext, flags, imageType, format, tiling, usage, handleType, &properties);

    is_importable     = (properties.externalMemoryFeatures & (VkExternalMemoryFeatureFlags)VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT);
    is_compatible     = (properties.compatibleHandleTypes & (VkExternalMemoryHandleTypeFlags)handleType);
    (*pDedicatedOnly) = (properties.externalMemoryFeatures & (VkExternalMemoryFeatureFlags)VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT);

    return is_importable && is_compatible;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Vulkan_GetPhysicalDeviceExternalImageExportSupport(VkPhysicalDevice                   physicalDevice,
                                                              const void*                        pNext,
                                                              VkImageCreateFlags                 flags,
                                                              VkImageType                        imageType,
                                                              VkFormat                           format,
                                                              VkImageTiling                      tiling,
                                                              VkImageUsageFlags                  usage,
                                                              VkExternalMemoryHandleTypeFlagBits handleType,
                                                              bool_t*                            pDedicatedOnly)
{
    VkExternalMemoryProperties properties;
    bool_t                     is_exportable;
    bool_t                     is_compatible;

    LOGASSERT(NULL != pDedicatedOnly);

    Gfx_Vulkan_GetPhysicalDeviceExternalImageMemoryProperties(physicalDevice, pNext, flags, imageType, format, tiling, usage, handleType, &properties);
    is_exportable     = (properties.externalMemoryFeatures & (VkExternalMemoryFeatureFlags)VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT);
    is_compatible     = (properties.compatibleHandleTypes & (VkExternalMemoryHandleTypeFlags)handleType);
    (*pDedicatedOnly) = (properties.externalMemoryFeatures & (VkExternalMemoryFeatureFlags)VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT);

    return is_exportable && is_compatible;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceDrmFormatModifierPropertiesList(VkPhysicalDevice                      physicalDevice,
                                                                 void*                                 pNext,
                                                                 VkFormat                              format,
                                                                 uint32_t                              drmFormatModifierCount,
                                                                 VkDrmFormatModifierPropertiesEXT*     pDrmFormatModifierProperties,
                                                                 VkDrmFormatModifierPropertiesListEXT* pProperties)
{
    VkDrmFormatModifierPropertiesListEXT modifier_list;
    VkFormatProperties2                  format_properties;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(NULL != pProperties);

    modifier_list.sType                        = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT;
    modifier_list.pNext                        = pNext;
    modifier_list.drmFormatModifierCount       = drmFormatModifierCount;
    modifier_list.pDrmFormatModifierProperties = pDrmFormatModifierProperties;
    Gfx_Vulkan_GetPhysicalDeviceFormatProperties2(physicalDevice, format, &modifier_list, &format_properties);

    (*pProperties) = modifier_list;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceDrmFormatModifierProperties(VkPhysicalDevice                 physicalDevice,
                                                             void*                            pNext,
                                                             VkFormat                         format,
                                                             uint32_t*                        pPropertyCount,
                                                             VkDrmFormatModifierPropertiesEXT pProperties[VK_MAX_DRM_FORMAT_MODIFIERS])
{
    VkDrmFormatModifierPropertiesListEXT modifier_list;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(NULL != pPropertyCount);
    LOGASSERT(NULL != pProperties);

    Gfx_Vulkan_GetPhysicalDeviceDrmFormatModifierPropertiesList(physicalDevice,
                                                                pNext,
                                                                format,
                                                                0,
                                                                NULL,
                                                                &modifier_list);
    LOGASSERT(VK_MAX_DRM_FORMAT_MODIFIERS >= modifier_list.drmFormatModifierCount);

    Gfx_Vulkan_GetPhysicalDeviceDrmFormatModifierPropertiesList(physicalDevice,
                                                                pNext,
                                                                format,
                                                                modifier_list.drmFormatModifierCount,
                                                                pProperties,
                                                                &modifier_list);
    (*pPropertyCount) = modifier_list.drmFormatModifierCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Vulkan_GetDrmFormatModifierProperties(uint32_t propertyCount, const VkDrmFormatModifierPropertiesEXT* pProperties, uint64_t drmFormatModifier, VkDrmFormatModifierPropertiesEXT* pModifierProperties)
{
    uint32_t modifier_index;

    LOGASSERT(0 == propertyCount || NULL != pProperties);
    LOGASSERT(NULL != pModifierProperties);

    for (modifier_index = 0; modifier_index < propertyCount; ++modifier_index)
    {
        if (drmFormatModifier == pProperties[modifier_index].drmFormatModifier)
        {
            (*pModifierProperties) = pProperties[modifier_index];
            return TRUE;
        }
    }
    return FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties)
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pMemoryProperties);

    vkGetPhysicalDeviceMemoryProperties(physicalDevice, pMemoryProperties);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceBufferProperties(VkPhysicalDevice                          physicalDevice,
                                                  const VkPhysicalDeviceExternalBufferInfo* pInfo,
                                                  void*                                     pPropertiesNext,
                                                  VkExternalBufferProperties*               pExternalBufferProperties)
{
    VkExternalBufferProperties properties;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pInfo);
    LOGASSERT(NULL != pExternalBufferProperties);

    properties.sType = VK_STRUCTURE_TYPE_EXTERNAL_BUFFER_PROPERTIES;
    properties.pNext = pPropertiesNext;
    vkGetPhysicalDeviceExternalBufferProperties(physicalDevice, pInfo, &properties);

    (*pExternalBufferProperties) = properties;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice                   physicalDevice,
                                                          const void*                        pNext,
                                                          VkBufferCreateFlags                flags,
                                                          VkBufferUsageFlags                 usage,
                                                          VkExternalMemoryHandleTypeFlagBits handleType,
                                                          void*                              pPropertiesNext,
                                                          VkExternalBufferProperties*        pExternalBufferProperties)
{
    VkPhysicalDeviceExternalBufferInfo info;
    VkExternalBufferProperties         properties;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pExternalBufferProperties);

    info.sType      = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_BUFFER_INFO;
    info.pNext      = pNext;
    info.flags      = flags;
    info.usage      = usage;
    info.handleType = handleType;
    Gfx_Vulkan_GetPhysicalDeviceBufferProperties(physicalDevice, &info, pPropertiesNext, &properties);

    (*pExternalBufferProperties) = properties;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceExternalBufferMemoryProperties(VkPhysicalDevice                   physicalDevice,
                                                                const void*                        pNext,
                                                                VkBufferCreateFlags                flags,
                                                                VkBufferUsageFlags                 usage,
                                                                VkExternalMemoryHandleTypeFlagBits handleType,
                                                                VkExternalMemoryProperties*        pExternalMemoryProperties)
{
    VkExternalBufferProperties properties;

    LOGASSERT(NULL != pExternalMemoryProperties);

    Gfx_Vulkan_GetPhysicalDeviceExternalBufferProperties(physicalDevice, pNext, flags, usage, handleType, NULL, &properties);

    (*pExternalMemoryProperties) = properties.externalMemoryProperties;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Vulkan_GetPhysicalDeviceExternalBufferImportSupport(VkPhysicalDevice                   physicalDevice,
                                                               const void*                        pNext,
                                                               VkBufferCreateFlags                flags,
                                                               VkBufferUsageFlags                 usage,
                                                               VkExternalMemoryHandleTypeFlagBits handleType,
                                                               bool_t*                            pDedicatedOnly)
{
    VkExternalMemoryProperties properties;
    bool_t                     is_importable;
    bool_t                     is_compatible;

    LOGASSERT(NULL != pDedicatedOnly);

    Gfx_Vulkan_GetPhysicalDeviceExternalBufferMemoryProperties(physicalDevice, pNext, flags, usage, handleType, &properties);
    is_importable     = (properties.externalMemoryFeatures & (VkExternalMemoryFeatureFlags)VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT);
    is_compatible     = (properties.compatibleHandleTypes & (VkExternalMemoryHandleTypeFlags)handleType);
    (*pDedicatedOnly) = (properties.externalMemoryFeatures & (VkExternalMemoryFeatureFlags)VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT);

    return is_importable && is_compatible;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Vulkan_GetPhysicalDeviceExternalBufferExportSupport(VkPhysicalDevice                   physicalDevice,
                                                               const void*                        pNext,
                                                               VkBufferCreateFlags                flags,
                                                               VkBufferUsageFlags                 usage,
                                                               VkExternalMemoryHandleTypeFlagBits handleType,
                                                               bool_t*                            pDedicatedOnly)
{
    VkExternalMemoryProperties properties;
    bool_t                     is_exportable;
    bool_t                     is_compatible;

    LOGASSERT(NULL != pDedicatedOnly);

    Gfx_Vulkan_GetPhysicalDeviceExternalBufferMemoryProperties(physicalDevice, pNext, flags, usage, handleType, &properties);
    is_exportable     = (properties.externalMemoryFeatures & (VkExternalMemoryFeatureFlags)VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT);
    is_compatible     = (properties.compatibleHandleTypes & (VkExternalMemoryHandleTypeFlags)handleType);
    (*pDedicatedOnly) = (properties.externalMemoryFeatures & (VkExternalMemoryFeatureFlags)VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT);

    return is_exportable && is_compatible;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceExternalBufferMemoryFeatures(VkPhysicalDevice                   physicalDevice,
                                                              const void*                        pNext,
                                                              VkBufferCreateFlags                flags,
                                                              VkBufferUsageFlags                 usage,
                                                              VkExternalMemoryHandleTypeFlagBits handleType,
                                                              VkExternalMemoryFeatureFlags*      pExternalMemoryFeatures)
{
    VkExternalMemoryProperties properties;

    LOGASSERT(NULL != pExternalMemoryFeatures);

    Gfx_Vulkan_GetPhysicalDeviceExternalBufferMemoryProperties(physicalDevice, pNext, flags, usage, handleType, &properties);

    (*pExternalMemoryFeatures) = properties.externalMemoryFeatures;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetMemoryTypeIndex(uint32_t memoryTypeCount, const VkMemoryType* pMemoryTypes, uint32_t memoryTypeBits, VkMemoryPropertyFlags properties)
{
    uint32_t memory_type_index;

    LOGASSERT(0 != memoryTypeCount);
    LOGASSERT(VK_MAX_MEMORY_TYPES >= memoryTypeCount);
    LOGASSERT(NULL != pMemoryTypes);

    for (memory_type_index = 0; memory_type_index < memoryTypeCount; ++memory_type_index)
    {
        if (memoryTypeBits & (1u << memory_type_index))
        {
            if (pMemoryTypes[memory_type_index].propertyFlags == properties)
            {
                return memory_type_index;
            }
        }
    }

    return VK_INVALID;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetCompatibleMemoryTypeIndex(uint32_t memoryTypeCount, const VkMemoryType* pMemoryTypes, uint32_t memoryTypeBits, VkMemoryPropertyFlags requiredProperties)
{
    uint32_t memory_type_index;

    LOGASSERT(0 != memoryTypeCount);
    LOGASSERT(VK_MAX_MEMORY_TYPES >= memoryTypeCount);
    LOGASSERT(NULL != pMemoryTypes);

    for (memory_type_index = 0; memory_type_index < memoryTypeCount; ++memory_type_index)
    {
        if (memoryTypeBits & (1u << memory_type_index))
        {
            if ((pMemoryTypes[memory_type_index].propertyFlags & requiredProperties) == requiredProperties)
            {
                return memory_type_index;
            }
        }
    }

    return VK_INVALID;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetQueueFamilyIndex(uint32_t propertyCount, const VkQueueFamilyProperties* pProperties, VkQueueFlags requiredFlags)
{
    uint32_t property_index;

    LOGASSERT(0 != propertyCount);
    LOGASSERT(VK_MAX_QUEUE_FAMILIES >= propertyCount);
    LOGASSERT(NULL != pProperties);
    LOGASSERT((VkQueueFlags)0 != requiredFlags);

    for (property_index = 0; property_index < propertyCount; ++property_index)
    {
        if (0 != pProperties[property_index].queueCount && requiredFlags == (pProperties[property_index].queueFlags & requiredFlags))
        {
            return property_index;
        }
    }

    return VK_INVALID;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateDevice(VkPhysicalDevice                physicalDevice,
                             const void*                     pNext,
                             VkDeviceCreateFlags             flags,
                             uint32_t                        queueCreateInfoCount,
                             const VkDeviceQueueCreateInfo*  pQueueCreateInfos,
                             uint32_t                        enabledLayerCount,
                             const char* const*              ppEnabledLayerNames,
                             uint32_t                        enabledExtensionCount,
                             const char* const*              ppEnabledExtensionNames,
                             const VkPhysicalDeviceFeatures* pEnabledFeatures,
                             const VkAllocationCallbacks*    pAllocator,
                             VkDevice*                       pDevice)
{
    VkDeviceCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(0 != queueCreateInfoCount);
    LOGASSERT(NULL != pQueueCreateInfos);
    LOGASSERT(0 == enabledLayerCount || NULL != ppEnabledLayerNames);
    LOGASSERT(0 == enabledExtensionCount || NULL != ppEnabledExtensionNames);
    LOGASSERT(NULL != pDevice);

    create_info.sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    create_info.pNext                   = pNext;
    create_info.flags                   = flags;
    create_info.queueCreateInfoCount    = queueCreateInfoCount;
    create_info.pQueueCreateInfos       = pQueueCreateInfos;
    create_info.enabledLayerCount       = enabledLayerCount;
    create_info.ppEnabledLayerNames     = ppEnabledLayerNames;
    create_info.enabledExtensionCount   = enabledExtensionCount;
    create_info.ppEnabledExtensionNames = ppEnabledExtensionNames;
    create_info.pEnabledFeatures        = pEnabledFeatures;
    VKCHECK(vkCreateDevice(physicalDevice, &create_info, pAllocator, pDevice));
    LOGASSERT(VK_NULL_HANDLE != (*pDevice));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupDevice(VkPhysicalDevice                physicalDevice,
                            const void*                     pNext,
                            VkDeviceCreateFlags             flags,
                            uint32_t                        queueCreateInfoCount,
                            const VkDeviceQueueCreateInfo*  pQueueCreateInfos,
                            uint32_t                        enabledLayerCount,
                            const char* const*              ppEnabledLayerNames,
                            uint32_t                        requestedExtensionCount,
                            const char* const*              ppRequestedExtensionNames,
                            const VkPhysicalDeviceFeatures* pEnabledFeatures,
                            const VkAllocationCallbacks*    pAllocator,
                            VkDevice*                       pDevice)
{
    uint32_t available_extension_count;
    uint32_t enabled_extension_count;
    uint32_t requested_extension_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(0 != queueCreateInfoCount);
    LOGASSERT(NULL != pQueueCreateInfos);
    LOGASSERT(VK_MAX_DEVICE_EXTENSIONS >= requestedExtensionCount);
    LOGASSERT(0 == requestedExtensionCount || NULL != ppRequestedExtensionNames);
    LOGASSERT(NULL != pDevice);

    Gfx_Vulkan_GetDeviceExtensionProperties(physicalDevice, NULL, &available_extension_count, c_vkDeviceExtensionProperties);
    enabled_extension_count = 0;
    for (requested_extension_index = 0; requested_extension_index < requestedExtensionCount; ++requested_extension_index)
    {
        LOGASSERT(NULL != ppRequestedExtensionNames[requested_extension_index]);
        if (VK_FALSE != Gfx_Vulkan_GetDeviceExtensionSupport(available_extension_count, c_vkDeviceExtensionProperties, ppRequestedExtensionNames[requested_extension_index]))
        {
            c_vkDeviceExtensionNames[enabled_extension_count++] = ppRequestedExtensionNames[requested_extension_index];
        }
    }
    Gfx_Vulkan_CreateDevice(physicalDevice,
                            pNext,
                            flags,
                            queueCreateInfoCount,
                            pQueueCreateInfos,
                            enabledLayerCount,
                            ppEnabledLayerNames,
                            enabled_extension_count,
                            c_vkDeviceExtensionNames,
                            pEnabledFeatures,
                            pAllocator,
                            pDevice);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupDeviceQueue(VkPhysicalDevice                physicalDevice,
                                 const void*                     pNext,
                                 VkDeviceCreateFlags             flags,
                                 const void*                     pQueueNext,
                                 VkDeviceQueueCreateFlags        queueFlags,
                                 uint32_t                        queueFamilyIndex,
                                 float                           queuePriority,
                                 uint32_t                        enabledLayerCount,
                                 const char* const*              ppEnabledLayerNames,
                                 uint32_t                        requestedExtensionCount,
                                 const char* const*              ppRequestedExtensionNames,
                                 const VkPhysicalDeviceFeatures* pEnabledFeatures,
                                 const VkAllocationCallbacks*    pAllocator,
                                 VkDevice*                       pDevice,
                                 VkQueue*                        pQueue)
{
    VkDeviceQueueCreateInfo queue_create_info;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_INVALID != queueFamilyIndex);
    LOGASSERT(0.0f <= queuePriority);
    LOGASSERT(1.0f >= queuePriority);
    LOGASSERT(NULL != pDevice);
    LOGASSERT(NULL != pQueue);

    queue_create_info.sType            = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queue_create_info.pNext            = pQueueNext;
    queue_create_info.flags            = queueFlags;
    queue_create_info.queueFamilyIndex = queueFamilyIndex;
    queue_create_info.queueCount       = 1;
    queue_create_info.pQueuePriorities = &queuePriority;
    Gfx_Vulkan_SetupDevice(physicalDevice,
                           pNext,
                           flags,
                           1,
                           &queue_create_info,
                           enabledLayerCount,
                           ppEnabledLayerNames,
                           requestedExtensionCount,
                           ppRequestedExtensionNames,
                           pEnabledFeatures,
                           pAllocator,
                           pDevice);
    Gfx_Vulkan_GetDeviceQueue((*pDevice), queueFamilyIndex, 0, pQueue);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_INVALID != queueFamilyIndex);
    LOGASSERT(NULL != pQueue);

    vkGetDeviceQueue(device, queueFamilyIndex, queueIndex, pQueue);
    LOGASSERT(VK_NULL_HANDLE != (*pQueue));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_DeviceWaitIdle(VkDevice device)
{
    LOGASSERT(VK_NULL_HANDLE != device);

    VKCHECK(vkDeviceWaitIdle(device));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetFormatPlaneCount(VkFormat format)
{
    LOGASSERT(VK_FORMAT_UNDEFINED != format);

    return vkuFormatPlaneCount(format);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkImageAspectFlagBits Gfx_Vulkan_GetFormatPlaneAspect(VkFormat format, uint32_t planeIndex)
{
    bool_t format_is_depth_or_stencil;
    uint32_t plane_count;

    plane_count = vkuFormatPlaneCount(format);
    LOGASSERT(plane_count > planeIndex);

    if (1u == plane_count)
    {
        LOGASSERT(0 == planeIndex);
        format_is_depth_or_stencil = vkuFormatIsDepthOrStencil(format);
        LOGASSERT(!format_is_depth_or_stencil);
        return (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT;
    }

    return (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_PLANE_0_BIT << planeIndex);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkImageAspectFlagBits Gfx_Vulkan_GetMemoryPlaneAspect(uint32_t planeIndex)
{
    LOGASSERT(VK_MAX_DRM_FORMAT_MODIFIER_PLANES > planeIndex);

    return (VkImageAspectFlagBits)(VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT << planeIndex);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkImageAspectFlags Gfx_Vulkan_GetFormatAspectMask(VkFormat format)
{
    bool_t format_is_color;
    VkImageAspectFlags aspect_mask;
    uint32_t           plane_count;
    uint32_t           plane_index;

    LOGASSERT(VK_FORMAT_UNDEFINED != format);

    aspect_mask = (VkImageAspectFlags)0;
    if (vkuFormatHasDepth(format))
    {
        aspect_mask |= (VkImageAspectFlags)VK_IMAGE_ASPECT_DEPTH_BIT;
    }
    if (vkuFormatHasStencil(format))
    {
        aspect_mask |= (VkImageAspectFlags)VK_IMAGE_ASPECT_STENCIL_BIT;
    }
    if ((VkImageAspectFlags)0 != aspect_mask)
    {
        return aspect_mask;
    }
    plane_count = vkuFormatPlaneCount(format);
    LOGASSERT(0 != plane_count);

    if (1 == plane_count)
    {
        format_is_color = vkuFormatIsColor(format);
        LOGASSERT(format_is_color);

        return (VkImageAspectFlags)VK_IMAGE_ASPECT_COLOR_BIT;
    }
    for (plane_index = 0; plane_index < plane_count; ++plane_index)
    {
        aspect_mask |= (VkImageAspectFlags)Gfx_Vulkan_GetFormatPlaneAspect(format, plane_index);
    }

    return aspect_mask;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkImageSubresourceLayers Gfx_Vulkan_GetFormatSubresourceLayers(VkFormat format, uint32_t mipLevel, uint32_t baseArrayLayer, uint32_t layerCount)
{
    VkImageSubresourceLayers subresource;

    LOGASSERT(0 != layerCount);

    subresource.aspectMask     = Gfx_Vulkan_GetFormatAspectMask(format);
    subresource.mipLevel       = mipLevel;
    subresource.baseArrayLayer = baseArrayLayer;
    subresource.layerCount     = layerCount;

    return subresource;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkImageSubresourceLayers Gfx_Vulkan_GetFormatBaseSubresourceLayers(VkFormat format)
{
    return Gfx_Vulkan_GetFormatSubresourceLayers(format, 0, 0, 1);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkImageSubresourceRange Gfx_Vulkan_GetFormatSubresourceRange(VkFormat format, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount)
{
    VkImageSubresourceRange subresource;

    LOGASSERT(0 != levelCount);
    LOGASSERT(0 != layerCount);

    subresource.aspectMask     = Gfx_Vulkan_GetFormatAspectMask(format);
    subresource.baseMipLevel   = baseMipLevel;
    subresource.levelCount     = levelCount;
    subresource.baseArrayLayer = baseArrayLayer;
    subresource.layerCount     = layerCount;

    return subresource;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkImageSubresourceRange Gfx_Vulkan_GetFormatBaseSubresourceRange(VkFormat format)
{
    return Gfx_Vulkan_GetFormatSubresourceRange(format, 0, 1, 0, 1);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each plane array must hold three entries; unused entries are zeroed.
 */
uint32_t Gfx_Vulkan_CalculateFormatPlaneLayouts(VkFormat      format,
                                                uint32_t      width,
                                                uint32_t      height,
                                                uint32_t      depth,
                                                uint32_t      layerCount,
                                                VkDeviceSize  rowAlignment,
                                                VkDeviceSize  planeOffsets[],
                                                uint32_t      planeStrides[],
                                                VkExtent3D    planeExtents[],
                                                VkDeviceSize* pSize)
{
    VkDeviceSize plane_offset;
    VkDeviceSize row_byte_count;
    VkDeviceSize aligned_row_byte_count;
    VkDeviceSize plane_byte_count;
    VkExtent3D   block_extent;
    VkFormat     plane_format;
    uint32_t     block_columns;
    uint32_t     block_rows;
    uint32_t     block_slices;
    uint32_t     plane_count;
    uint32_t     plane_index;

    LOGASSERT(0 != width);
    LOGASSERT(0 != height);
    LOGASSERT(0 != depth);
    LOGASSERT(0 != layerCount);
    LOGASSERT(0 != rowAlignment);
    LOGASSERT(NULL != planeOffsets);
    LOGASSERT(NULL != planeStrides);
    LOGASSERT(NULL != planeExtents);
    LOGASSERT(NULL != pSize);

    plane_count = vkuFormatPlaneCount(format);
    LOGASSERT(0 != plane_count);
    LOGASSERT(VKU_FORMAT_MAX_PLANES >= plane_count);

    plane_offset = VK_NO_OFFSET;
    (void)memset(planeOffsets, 0, VKU_FORMAT_MAX_PLANES * sizeof(planeOffsets[0]));
    (void)memset(planeStrides, 0, VKU_FORMAT_MAX_PLANES * sizeof(planeStrides[0]));
    (void)memset(planeExtents, 0, VKU_FORMAT_MAX_PLANES * sizeof(planeExtents[0]));
    for (plane_index = 0; plane_index < plane_count; ++plane_index)
    {
        if (1u == plane_count)
        {
            plane_format                     = format;
            planeExtents[plane_index].width  = width;
            planeExtents[plane_index].height = height;
        }
        else
        {
            VkExtent2D            divisors;
            VkImageAspectFlagBits plane_aspect;

            plane_aspect = Gfx_Vulkan_GetFormatPlaneAspect(format, plane_index);
            plane_format = vkuFindMultiplaneCompatibleFormat(format, plane_aspect);
            LOGASSERT(VK_FORMAT_UNDEFINED != plane_format);

            divisors = vkuFindMultiplaneExtentDivisors(format, plane_aspect);
            LOGASSERT(0 != divisors.width);
            LOGASSERT(0 != divisors.height);

            planeExtents[plane_index].width  = (width + divisors.width - 1u) / divisors.width;
            planeExtents[plane_index].height = (height + divisors.height - 1u) / divisors.height;
        }
        planeExtents[plane_index].depth = depth;
        block_extent                    = vkuFormatTexelBlockExtent(plane_format);
        LOGASSERT(0 != block_extent.width);
        LOGASSERT(0 != block_extent.height);
        LOGASSERT(0 != block_extent.depth);

        block_columns  = (planeExtents[plane_index].width + block_extent.width - 1u) / block_extent.width;
        block_rows     = (planeExtents[plane_index].height + block_extent.height - 1u) / block_extent.height;
        block_slices   = (planeExtents[plane_index].depth + block_extent.depth - 1u) / block_extent.depth;
        row_byte_count = (VkDeviceSize)block_columns * vkuFormatTexelBlockSize(plane_format);
        LOGASSERT(row_byte_count <= UINT64_MAX - (rowAlignment - 1u));

        aligned_row_byte_count = ((row_byte_count + rowAlignment - 1u) / rowAlignment) * rowAlignment;
        LOGASSERT(UINT32_MAX >= aligned_row_byte_count);
        LOGASSERT(aligned_row_byte_count <= UINT64_MAX / block_rows);

        plane_byte_count = aligned_row_byte_count * block_rows;
        LOGASSERT(plane_byte_count <= UINT64_MAX / block_slices);

        plane_byte_count *= block_slices;
        LOGASSERT(plane_byte_count <= UINT64_MAX / layerCount);

        plane_byte_count *= layerCount;
        LOGASSERT(plane_offset <= UINT64_MAX - plane_byte_count);

        planeOffsets[plane_index] = plane_offset;
        planeStrides[plane_index] = (uint32_t)aligned_row_byte_count;
        plane_offset += plane_byte_count;
    }
    (*pSize) = plane_offset;
    return plane_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkDeviceSize Gfx_Vulkan_GetFormatSize(uint32_t width, uint32_t height, VkFormat format)
{
    VkDeviceSize plane_offsets[VKU_FORMAT_MAX_PLANES];
    uint32_t     plane_strides[VKU_FORMAT_MAX_PLANES];
    VkExtent3D   plane_extents[VKU_FORMAT_MAX_PLANES];
    VkDeviceSize byte_count;

    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(0 != width);
    LOGASSERT(0 != height);

    (void)Gfx_Vulkan_CalculateFormatPlaneLayouts(format, width, height, 1, 1, 1, plane_offsets, plane_strides, plane_extents, &byte_count);
    return byte_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetFormatPlaneLayout(VkFormat      format,
                                     uint32_t      width,
                                     uint32_t      height,
                                     VkDeviceSize  rowAlignment,
                                     uint32_t*     pYStride,
                                     uint32_t*     pChromaOffset,
                                     uint32_t*     pVOffset,
                                     uint32_t*     pChromaStride,
                                     VkDeviceSize* pSize)
{
    VkDeviceSize plane_offsets[VKU_FORMAT_MAX_PLANES];
    uint32_t     plane_strides[VKU_FORMAT_MAX_PLANES];
    VkExtent3D   plane_extents[VKU_FORMAT_MAX_PLANES];
    uint32_t     y_plane_index;
    uint32_t     u_plane_index;
    uint32_t     v_plane_index;
    uint32_t     plane_count;

    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(0 != width);
    LOGASSERT(0 != height);
    LOGASSERT(0 != rowAlignment);
    LOGASSERT(NULL != pYStride);
    LOGASSERT(NULL != pChromaOffset);
    LOGASSERT(NULL != pVOffset);
    LOGASSERT(NULL != pChromaStride);
    LOGASSERT(NULL != pSize);

    y_plane_index = vkuGetPlaneIndex(VK_IMAGE_ASPECT_PLANE_0_BIT);
    u_plane_index = vkuGetPlaneIndex(VK_IMAGE_ASPECT_PLANE_1_BIT);
    v_plane_index = vkuGetPlaneIndex(VK_IMAGE_ASPECT_PLANE_2_BIT);
    LOGASSERT(VKU_FORMAT_INVALID_INDEX != y_plane_index);
    LOGASSERT(VKU_FORMAT_INVALID_INDEX != u_plane_index);
    LOGASSERT(VKU_FORMAT_INVALID_INDEX != v_plane_index);

    plane_count = Gfx_Vulkan_CalculateFormatPlaneLayouts(format, width, height, 1, 1, rowAlignment, plane_offsets, plane_strides, plane_extents, pSize);
    LOGASSERT(y_plane_index < plane_count);
    LOGASSERT(u_plane_index < plane_count);
    LOGASSERT(UINT32_MAX >= plane_offsets[u_plane_index]);

    (*pYStride)      = plane_strides[y_plane_index];
    (*pChromaOffset) = (uint32_t)plane_offsets[u_plane_index];
    (*pChromaStride) = plane_strides[u_plane_index];
    (*pVOffset)      = VK_NO_OFFSET;
    if (v_plane_index < plane_count)
    {
        LOGASSERT(UINT32_MAX >= plane_offsets[v_plane_index]);
        LOGASSERT(plane_strides[u_plane_index] == plane_strides[v_plane_index]);

        (*pVOffset) = (uint32_t)plane_offsets[v_plane_index];
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateImage(VkDevice                     device,
                            const void*                  pNext,
                            VkImageCreateFlags           flags,
                            VkImageType                  imageType,
                            VkFormat                     format,
                            VkExtent3D                   extent,
                            uint32_t                     mipLevels,
                            uint32_t                     arrayLayers,
                            VkSampleCountFlagBits        samples,
                            VkImageTiling                tiling,
                            VkImageUsageFlags            usage,
                            VkSharingMode                sharingMode,
                            uint32_t                     queueFamilyIndexCount,
                            const uint32_t*              pQueueFamilyIndices,
                            VkImageLayout                initialLayout,
                            const VkAllocationCallbacks* pAllocator,
                            VkImage*                     pImage)
{
    VkImageCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(0 != extent.width);
    LOGASSERT(0 != extent.height);
    LOGASSERT(0 != extent.depth);
    LOGASSERT(0 != mipLevels);
    LOGASSERT(0 != arrayLayers);
    LOGASSERT(0 != samples);
    LOGASSERT((VkImageUsageFlags)0 != usage);
    LOGASSERT(0 == queueFamilyIndexCount || NULL != pQueueFamilyIndices);
    LOGASSERT(NULL != pImage);

    create_info.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.pNext                 = pNext;
    create_info.flags                 = flags;
    create_info.imageType             = imageType;
    create_info.format                = format;
    create_info.extent                = extent;
    create_info.mipLevels             = mipLevels;
    create_info.arrayLayers           = arrayLayers;
    create_info.samples               = samples;
    create_info.tiling                = tiling;
    create_info.usage                 = usage;
    create_info.sharingMode           = sharingMode;
    create_info.queueFamilyIndexCount = queueFamilyIndexCount;
    create_info.pQueueFamilyIndices   = pQueueFamilyIndices;
    create_info.initialLayout         = initialLayout;
    VKCHECK(vkCreateImage(device, &create_info, pAllocator, pImage));
    LOGASSERT(VK_NULL_HANDLE != (*pImage));
}

/*************************************************************************************************/
/**
 * \brief Creates an image with caller-selected external-memory handle types.
 *
 * \par Details:
 *  pNext follows the external-memory image create info in the input chain.
 */
void Gfx_Vulkan_CreateExternalImage(VkDevice                        device,
                                    const void*                     pNext,
                                    VkExternalMemoryHandleTypeFlags handleTypes,
                                    VkImageCreateFlags              flags,
                                    VkImageType                     imageType,
                                    VkFormat                        format,
                                    VkExtent3D                      extent,
                                    uint32_t                        mipLevels,
                                    uint32_t                        arrayLayers,
                                    VkSampleCountFlagBits           samples,
                                    VkImageTiling                   tiling,
                                    VkImageUsageFlags               usage,
                                    VkSharingMode                   sharingMode,
                                    uint32_t                        queueFamilyIndexCount,
                                    const uint32_t*                 pQueueFamilyIndices,
                                    VkImageLayout                   initialLayout,
                                    const VkAllocationCallbacks*    pAllocator,
                                    VkImage*                        pImage)
{
    VkExternalMemoryImageCreateInfo external_info;

    LOGASSERT(handleTypes);

    external_info.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.pNext       = pNext;
    external_info.handleTypes = handleTypes;
    Gfx_Vulkan_CreateImage(device,
                           &external_info,
                           flags,
                           imageType,
                           format,
                           extent,
                           mipLevels,
                           arrayLayers,
                           samples,
                           tiling,
                           usage,
                           sharingMode,
                           queueFamilyIndexCount,
                           pQueueFamilyIndices,
                           initialLayout,
                           pAllocator,
                           pImage);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupImage(VkPhysicalDevice             physicalDevice,
                           VkDevice                     device,
                           const void*                  pImageNext,
                           VkImageCreateFlags           flags,
                           VkImageType                  imageType,
                           VkFormat                     format,
                           VkExtent3D                   extent,
                           uint32_t                     mipLevels,
                           uint32_t                     arrayLayers,
                           VkSampleCountFlagBits        samples,
                           VkImageTiling                tiling,
                           VkImageUsageFlags            usage,
                           VkSharingMode                sharingMode,
                           uint32_t                     queueFamilyIndexCount,
                           const uint32_t*              pQueueFamilyIndices,
                           VkImageLayout                initialLayout,
                           const void*                  pMemoryNext,
                           VkMemoryPropertyFlags        properties,
                           const VkAllocationCallbacks* pAllocator,
                           VkImage*                     pImage,
                           VkDeviceMemory*              pMemory)
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(0 != extent.width);
    LOGASSERT(0 != extent.height);
    LOGASSERT(0 != extent.depth);
    LOGASSERT(0 != mipLevels);
    LOGASSERT(0 != arrayLayers);
    LOGASSERT((VkImageUsageFlags)0 != usage);
    LOGASSERT(NULL != pImage);
    LOGASSERT(NULL != pMemory);

    Gfx_Vulkan_CreateImage(device,
                           pImageNext,
                           flags,
                           imageType,
                           format,
                           extent,
                           mipLevels,
                           arrayLayers,
                           samples,
                           tiling,
                           usage,
                           sharingMode,
                           queueFamilyIndexCount,
                           pQueueFamilyIndices,
                           initialLayout,
                           pAllocator,
                           pImage);
    Gfx_Vulkan_AllocateImageMemory(physicalDevice, device, (*pImage), pMemoryNext, properties, pAllocator, pMemory);
    Gfx_Vulkan_BindImageMemory(device, (*pImage), (*pMemory), VK_NO_OFFSET);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements)
{
    VkMemoryRequirements requirements;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT(NULL != pMemoryRequirements);

    vkGetImageMemoryRequirements(device, image, &requirements);

    (*pMemoryRequirements) = requirements;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetDeviceImageMemoryRequirements2(VkDevice               device,
                                                  const void*            pNext,
                                                  const void*            pImageNext,
                                                  VkImageCreateFlags     flags,
                                                  VkImageType            imageType,
                                                  VkFormat               format,
                                                  VkExtent3D             extent,
                                                  uint32_t               mipLevels,
                                                  uint32_t               arrayLayers,
                                                  VkSampleCountFlagBits  samples,
                                                  VkImageTiling          tiling,
                                                  VkImageUsageFlags      usage,
                                                  VkSharingMode          sharingMode,
                                                  uint32_t               queueFamilyIndexCount,
                                                  const uint32_t*        pQueueFamilyIndices,
                                                  VkImageLayout          initialLayout,
                                                  VkImageAspectFlagBits  planeAspect,
                                                  void*                  pMemoryNext,
                                                  VkMemoryRequirements2* pMemoryRequirements)
{
    VkImageCreateInfo               create_info;
    VkDeviceImageMemoryRequirements image_info;
    VkMemoryRequirements2           requirements;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(0 != extent.width && 0 != extent.height && 0 != extent.depth);
    LOGASSERT(0 != mipLevels && 0 != arrayLayers && 0 != samples);
    LOGASSERT((VkImageUsageFlags)0 != usage);
    LOGASSERT(0 == queueFamilyIndexCount || NULL != pQueueFamilyIndices);
    LOGASSERT(NULL != pMemoryRequirements);
    LOGASSERT(NULL != vkGetDeviceImageMemoryRequirementsKHR);

    create_info.sType                 = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    create_info.pNext                 = pImageNext;
    create_info.flags                 = flags;
    create_info.imageType             = imageType;
    create_info.format                = format;
    create_info.extent                = extent;
    create_info.mipLevels             = mipLevels;
    create_info.arrayLayers           = arrayLayers;
    create_info.samples               = samples;
    create_info.tiling                = tiling;
    create_info.usage                 = usage;
    create_info.sharingMode           = sharingMode;
    create_info.queueFamilyIndexCount = queueFamilyIndexCount;
    create_info.pQueueFamilyIndices   = pQueueFamilyIndices;
    create_info.initialLayout         = initialLayout;

    image_info.sType       = VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS;
    image_info.pNext       = pNext;
    image_info.pCreateInfo = &create_info;
    image_info.planeAspect = planeAspect;

    requirements.sType                             = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    requirements.pNext                             = pMemoryNext;
    requirements.memoryRequirements.size           = 0;
    requirements.memoryRequirements.alignment      = 0;
    requirements.memoryRequirements.memoryTypeBits = 0;
    vkGetDeviceImageMemoryRequirementsKHR(device, &image_info, &requirements);
    (*pMemoryRequirements) = requirements;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetExternalImageMemoryRequirements(VkDevice                        device,
                                                   const void*                     pNext,
                                                   VkExternalMemoryHandleTypeFlags handleTypes,
                                                   VkImageCreateFlags              flags,
                                                   VkImageType                     imageType,
                                                   VkFormat                        format,
                                                   VkExtent3D                      extent,
                                                   uint32_t                        mipLevels,
                                                   uint32_t                        arrayLayers,
                                                   VkSampleCountFlagBits           samples,
                                                   VkImageTiling                   tiling,
                                                   VkImageUsageFlags               usage,
                                                   VkSharingMode                   sharingMode,
                                                   uint32_t                        queueFamilyIndexCount,
                                                   const uint32_t*                 pQueueFamilyIndices,
                                                   VkImageLayout                   initialLayout,
                                                   VkImageAspectFlagBits           planeAspect,
                                                   VkMemoryRequirements*           pMemoryRequirements)
{
    VkExternalMemoryImageCreateInfo external_info;
    VkMemoryRequirements2           requirements;

    LOGASSERT(0 != handleTypes);
    LOGASSERT(NULL != pMemoryRequirements);

    external_info.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
    external_info.pNext       = pNext;
    external_info.handleTypes = handleTypes;

    Gfx_Vulkan_GetDeviceImageMemoryRequirements2(device,
                                                 NULL,
                                                 &external_info,
                                                 flags,
                                                 imageType,
                                                 format,
                                                 extent,
                                                 mipLevels,
                                                 arrayLayers,
                                                 samples,
                                                 tiling,
                                                 usage,
                                                 sharingMode,
                                                 queueFamilyIndexCount,
                                                 pQueueFamilyIndices,
                                                 initialLayout,
                                                 planeAspect,
                                                 NULL,
                                                 &requirements);
    (*pMemoryRequirements) = requirements.memoryRequirements;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, void* pMemoryNext, VkMemoryRequirements2* pMemoryRequirements)
{
    VkMemoryRequirements2 requirements;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != pInfo);
    LOGASSERT(NULL != pMemoryRequirements);

    requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
    requirements.pNext = pMemoryNext;
    vkGetImageMemoryRequirements2(device, pInfo, &requirements);

    (*pMemoryRequirements) = requirements;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetImageMemoryRequirementsInfo2(VkDevice device, const void* pNext, VkImage image, void* pMemoryNext, VkMemoryRequirements2* pMemoryRequirements)
{
    VkImageMemoryRequirementsInfo2 image_info;
    VkMemoryRequirements2          requirements;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT(NULL != pMemoryRequirements);

    image_info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
    image_info.pNext = pNext;
    image_info.image = image;
    Gfx_Vulkan_GetImageMemoryRequirements2(device, &image_info, pMemoryNext, &requirements);

    (*pMemoryRequirements) = requirements;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetImagePlaneMemoryRequirements2(VkDevice               device,
                                                 const void*            pImageNext,
                                                 VkImage                image,
                                                 VkImageAspectFlagBits  planeAspect,
                                                 void*                  pMemoryNext,
                                                 VkMemoryRequirements2* pMemoryRequirements)
{
    VkImagePlaneMemoryRequirementsInfo plane_info;
    VkMemoryRequirements2              requirements;

    LOGASSERT(planeAspect);
    LOGASSERT(NULL != pMemoryRequirements);

    plane_info.sType       = VK_STRUCTURE_TYPE_IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO;
    plane_info.pNext       = pImageNext;
    plane_info.planeAspect = planeAspect;
    Gfx_Vulkan_GetImageMemoryRequirementsInfo2(device, &plane_info, image, pMemoryNext, &requirements);

    (*pMemoryRequirements) = requirements;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetImagePlaneMemoryRequirements(VkDevice device, VkImage image, VkImageAspectFlagBits planeAspect, VkMemoryRequirements* pMemoryRequirements)
{
    VkMemoryRequirements2 requirements;

    LOGASSERT(NULL != pMemoryRequirements);

    Gfx_Vulkan_GetImagePlaneMemoryRequirements2(device, NULL, image, planeAspect, NULL, &requirements);

    (*pMemoryRequirements) = requirements.memoryRequirements;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateMemory(VkDevice device, const void* pNext, VkDeviceSize allocationSize, uint32_t memoryTypeIndex, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory)
{
    VkMemoryAllocateInfo allocate_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != allocationSize);
    LOGASSERT(VK_INVALID != memoryTypeIndex);
    LOGASSERT(NULL != pMemory);

    allocate_info.sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocate_info.pNext           = pNext;
    allocate_info.allocationSize  = allocationSize;
    allocate_info.memoryTypeIndex = memoryTypeIndex;
    VKCHECK(vkAllocateMemory(device, &allocate_info, pAllocator, pMemory));
    LOGASSERT(VK_NULL_HANDLE != (*pMemory));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateDedicatedMemory(VkDevice                     device,
                                        const void*                  pNext,
                                        VkImage                      image,
                                        VkBuffer                     buffer,
                                        VkDeviceSize                 allocationSize,
                                        uint32_t                     memoryTypeIndex,
                                        const VkAllocationCallbacks* pAllocator,
                                        VkDeviceMemory*              pMemory)
{
    VkMemoryDedicatedAllocateInfo dedicated_info;

    LOGASSERT(VK_NULL_HANDLE == image || VK_NULL_HANDLE == buffer);

    dedicated_info.sType  = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
    dedicated_info.pNext  = pNext;
    dedicated_info.image  = image;
    dedicated_info.buffer = buffer;
    Gfx_Vulkan_AllocateMemory(device, &dedicated_info, allocationSize, memoryTypeIndex, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateImageMemory(VkPhysicalDevice             physicalDevice,
                                    VkDevice                     device,
                                    VkImage                      image,
                                    const void*                  pNext,
                                    VkMemoryPropertyFlags        properties,
                                    const VkAllocationCallbacks* pAllocator,
                                    VkDeviceMemory*              pMemory)
{
    VkDeviceSize allocation_size;
    uint32_t     memory_type_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT(NULL != pMemory);

    Gfx_Vulkan_GetImageMemoryAllocationInfo(physicalDevice, device, image, UINT32_MAX, properties, &allocation_size, &memory_type_index);

    Gfx_Vulkan_AllocateMemory(device, pNext, allocation_size, memory_type_index, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateDedicatedImageMemory(VkDevice                     device,
                                             const void*                  pNext,
                                             VkImage                      image,
                                             VkDeviceSize                 allocationSize,
                                             uint32_t                     memoryTypeIndex,
                                             const VkAllocationCallbacks* pAllocator,
                                             VkDeviceMemory*              pMemory)
{
    LOGASSERT(VK_NULL_HANDLE != image);

    Gfx_Vulkan_AllocateDedicatedMemory(device, pNext, image, VK_NULL_HANDLE, allocationSize, memoryTypeIndex, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT(VK_NULL_HANDLE != memory);

    VKCHECK(vkBindImageMemory(device, image, memory, memoryOffset));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != bindInfoCount);
    LOGASSERT(NULL != pBindInfos);

    VKCHECK(vkBindImageMemory2(device, bindInfoCount, pBindInfos));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_BindImageMemories(VkDevice device, const void* const* ppNext, VkImage image, uint32_t memoryCount, const VkDeviceMemory* pMemories, const VkDeviceSize* pMemoryOffsets)
{
    VkBindImageMemoryInfo bind_infos[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t              memory_index;

    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT(memoryCount);
    LOGASSERT(VK_MAX_DRM_FORMAT_MODIFIER_PLANES >= memoryCount);
    LOGASSERT(NULL != pMemories);

    for (memory_index = 0; memory_index < memoryCount; ++memory_index)
    {
        LOGASSERT(VK_NULL_HANDLE != pMemories[memory_index]);

        bind_infos[memory_index].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO;
        bind_infos[memory_index].pNext = NULL;
        if (NULL != ppNext)
        {
            bind_infos[memory_index].pNext = ppNext[memory_index];
        }
        bind_infos[memory_index].image        = image;
        bind_infos[memory_index].memory       = pMemories[memory_index];
        bind_infos[memory_index].memoryOffset = VK_NO_OFFSET;
        if (NULL != pMemoryOffsets)
        {
            bind_infos[memory_index].memoryOffset = pMemoryOffsets[memory_index];
        }
    }
    Gfx_Vulkan_BindImageMemory2(device, memoryCount, bind_infos);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_BindImagePlaneMemories(VkDevice                     device,
                                       const void* const*           ppNext,
                                       VkImage                      image,
                                       uint32_t                     planeCount,
                                       const VkImageAspectFlagBits* pPlaneAspects,
                                       const VkDeviceMemory*        pMemories,
                                       const VkDeviceSize*          pMemoryOffsets)
{
    VkBindImagePlaneMemoryInfo plane_infos[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    const void*                nexts[VK_MAX_DRM_FORMAT_MODIFIER_PLANES];
    uint32_t                   plane_index;

    LOGASSERT(planeCount);
    LOGASSERT(VK_MAX_DRM_FORMAT_MODIFIER_PLANES >= planeCount);
    LOGASSERT(NULL != pPlaneAspects);

    for (plane_index = 0; plane_index < planeCount; ++plane_index)
    {
        LOGASSERT(pPlaneAspects[plane_index]);

        plane_infos[plane_index].sType = VK_STRUCTURE_TYPE_BIND_IMAGE_PLANE_MEMORY_INFO;
        plane_infos[plane_index].pNext = NULL;
        if (NULL != ppNext)
        {
            plane_infos[plane_index].pNext = ppNext[plane_index];
        }
        plane_infos[plane_index].planeAspect = pPlaneAspects[plane_index];
        nexts[plane_index]                   = &plane_infos[plane_index];
    }
    Gfx_Vulkan_BindImageMemories(device, nexts, image, planeCount, pMemories, pMemoryOffsets);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateImageView(VkDevice                     device,
                                const void*                  pNext,
                                VkImageViewCreateFlags       flags,
                                VkImage                      image,
                                VkImageViewType              viewType,
                                VkFormat                     format,
                                VkComponentMapping           components,
                                VkImageSubresourceRange      subresourceRange,
                                const VkAllocationCallbacks* pAllocator,
                                VkImageView*                 pView)
{
    VkImageViewCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT((VkImageAspectFlags)0 != subresourceRange.aspectMask);
    LOGASSERT(0 != subresourceRange.levelCount);
    LOGASSERT(0 != subresourceRange.layerCount);
    LOGASSERT(NULL != pView);

    create_info.sType            = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    create_info.pNext            = pNext;
    create_info.flags            = flags;
    create_info.image            = image;
    create_info.viewType         = viewType;
    create_info.format           = format;
    create_info.components       = components;
    create_info.subresourceRange = subresourceRange;
    VKCHECK(vkCreateImageView(device, &create_info, pAllocator, pView));
    LOGASSERT(VK_NULL_HANDLE != (*pView));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateBuffer(VkDevice                     device,
                             const void*                  pNext,
                             VkBufferCreateFlags          flags,
                             VkDeviceSize                 size,
                             VkBufferUsageFlags           usage,
                             VkSharingMode                sharingMode,
                             uint32_t                     queueFamilyIndexCount,
                             const uint32_t*              pQueueFamilyIndices,
                             const VkAllocationCallbacks* pAllocator,
                             VkBuffer*                    pBuffer)
{
    VkBufferCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != size);
    LOGASSERT((VkBufferUsageFlags)0 != usage);
    LOGASSERT(0 == queueFamilyIndexCount || NULL != pQueueFamilyIndices);
    LOGASSERT(NULL != pBuffer);

    create_info.sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    create_info.pNext                 = pNext;
    create_info.flags                 = flags;
    create_info.size                  = size;
    create_info.usage                 = usage;
    create_info.sharingMode           = sharingMode;
    create_info.queueFamilyIndexCount = queueFamilyIndexCount;
    create_info.pQueueFamilyIndices   = pQueueFamilyIndices;
    VKCHECK(vkCreateBuffer(device, &create_info, pAllocator, pBuffer));
    LOGASSERT(VK_NULL_HANDLE != (*pBuffer));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupBuffer(VkPhysicalDevice             physicalDevice,
                            VkDevice                     device,
                            const void*                  pBufferNext,
                            VkBufferCreateFlags          flags,
                            VkDeviceSize                 size,
                            VkBufferUsageFlags           usage,
                            VkSharingMode                sharingMode,
                            uint32_t                     queueFamilyIndexCount,
                            const uint32_t*              pQueueFamilyIndices,
                            const void*                  pMemoryNext,
                            VkMemoryPropertyFlags        properties,
                            const VkAllocationCallbacks* pAllocator,
                            VkBuffer*                    pBuffer,
                            VkDeviceMemory*              pMemory)
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != size);
    LOGASSERT((VkBufferUsageFlags)0 != usage);
    LOGASSERT(NULL != pBuffer);
    LOGASSERT(NULL != pMemory);

    Gfx_Vulkan_CreateBuffer(device, pBufferNext, flags, size, usage, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices, pAllocator, pBuffer);
    Gfx_Vulkan_AllocateBufferMemory(physicalDevice, device, (*pBuffer), pMemoryNext, properties, pAllocator, pMemory);
    Gfx_Vulkan_BindBufferMemory(device, (*pBuffer), (*pMemory), VK_NO_OFFSET);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupPlaneBuffer(VkPhysicalDevice             physicalDevice,
                                 VkDevice                     device,
                                 const void*                  pBufferNext,
                                 VkBufferCreateFlags          flags,
                                 VkFormat                     format,
                                 uint32_t                     width,
                                 uint32_t                     height,
                                 VkDeviceSize                 rowAlignment,
                                 VkBufferUsageFlags           usage,
                                 VkSharingMode                sharingMode,
                                 uint32_t                     queueFamilyIndexCount,
                                 const uint32_t*              pQueueFamilyIndices,
                                 const void*                  pMemoryNext,
                                 VkMemoryPropertyFlags        properties,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkBuffer*                    pBuffer,
                                 VkDeviceMemory*              pMemory,
                                 VkDeviceSize*                pSize,
                                 uint32_t*                    pYStride,
                                 uint32_t*                    pChromaOffset,
                                 uint32_t*                    pVOffset,
                                 uint32_t*                    pChromaStride)
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_FORMAT_UNDEFINED != format);
    LOGASSERT(0 != width);
    LOGASSERT(0 != height);
    LOGASSERT(0 != rowAlignment);
    LOGASSERT((VkBufferUsageFlags)0 != usage);
    LOGASSERT(0 == queueFamilyIndexCount || NULL != pQueueFamilyIndices);
    LOGASSERT(NULL != pBuffer);
    LOGASSERT(NULL != pMemory);
    LOGASSERT(NULL != pSize);
    LOGASSERT(NULL != pYStride);
    LOGASSERT(NULL != pChromaOffset);
    LOGASSERT(NULL != pVOffset);
    LOGASSERT(NULL != pChromaStride);

    Gfx_Vulkan_GetFormatPlaneLayout(format, width, height, rowAlignment, pYStride, pChromaOffset, pVOffset, pChromaStride, pSize);
    Gfx_Vulkan_SetupBuffer(physicalDevice, device, pBufferNext, flags, (*pSize), usage, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices, pMemoryNext, properties, pAllocator, pBuffer, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateExternalMemoryBuffer(VkDevice                        device,
                                           const void*                     pNext,
                                           VkExternalMemoryHandleTypeFlags handleTypes,
                                           VkBufferCreateFlags             flags,
                                           VkDeviceSize                    size,
                                           VkBufferUsageFlags              usage,
                                           VkSharingMode                   sharingMode,
                                           uint32_t                        queueFamilyIndexCount,
                                           const uint32_t*                 pQueueFamilyIndices,
                                           const VkAllocationCallbacks*    pAllocator,
                                           VkBuffer*                       pBuffer)
{
    VkExternalMemoryBufferCreateInfo external_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT((VkExternalMemoryHandleTypeFlags)0 != handleTypes);
    LOGASSERT(0 != size);
    LOGASSERT((VkBufferUsageFlags)0 != usage);
    LOGASSERT(0 == queueFamilyIndexCount || NULL != pQueueFamilyIndices);
    LOGASSERT(NULL != pBuffer);

    external_info.sType       = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO;
    external_info.pNext       = pNext;
    external_info.handleTypes = handleTypes;
    Gfx_Vulkan_CreateBuffer(device, &external_info, flags, size, usage, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices, pAllocator, pBuffer);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT(NULL != pMemoryRequirements);

    vkGetBufferMemoryRequirements(device, buffer, pMemoryRequirements);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateBufferMemory(VkPhysicalDevice             physicalDevice,
                                     VkDevice                     device,
                                     VkBuffer                     buffer,
                                     const void*                  pNext,
                                     VkMemoryPropertyFlags        properties,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkDeviceMemory*              pMemory)
{
    VkDeviceSize allocation_size;
    uint32_t     memory_type_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT(NULL != pMemory);

    Gfx_Vulkan_GetBufferMemoryAllocationInfo(physicalDevice, device, buffer, UINT32_MAX, properties, &allocation_size, &memory_type_index);

    Gfx_Vulkan_AllocateMemory(device, pNext, allocation_size, memory_type_index, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateDedicatedBufferMemory(VkPhysicalDevice             physicalDevice,
                                              VkDevice                     device,
                                              const void*                  pNext,
                                              VkBuffer                     buffer,
                                              VkMemoryPropertyFlags        properties,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDeviceMemory*              pMemory)
{
    VkDeviceSize allocation_size;
    uint32_t     memory_type_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT(NULL != pMemory);

    Gfx_Vulkan_GetBufferMemoryAllocationInfo(physicalDevice, device, buffer, UINT32_MAX, properties, &allocation_size, &memory_type_index);

    Gfx_Vulkan_AllocateDedicatedMemory(device, pNext, VK_NULL_HANDLE, buffer, allocation_size, memory_type_index, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateExportableBufferMemory(VkPhysicalDevice                physicalDevice,
                                               VkDevice                        device,
                                               VkBuffer                        buffer,
                                               const void*                     pNext,
                                               VkExternalMemoryHandleTypeFlags handleTypes,
                                               VkMemoryPropertyFlags           properties,
                                               const VkAllocationCallbacks*    pAllocator,
                                               VkDeviceMemory*                 pMemory)
{
    VkExportMemoryAllocateInfo export_info;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT((VkExternalMemoryHandleTypeFlags)0 != handleTypes);
    LOGASSERT(NULL != pMemory);

    export_info.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.pNext       = pNext;
    export_info.handleTypes = handleTypes;
    Gfx_Vulkan_AllocateBufferMemory(physicalDevice, device, buffer, &export_info, properties, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateExportableDedicatedBufferMemory(VkPhysicalDevice                physicalDevice,
                                                        VkDevice                        device,
                                                        VkBuffer                        buffer,
                                                        const void*                     pNext,
                                                        VkExternalMemoryHandleTypeFlags handleTypes,
                                                        VkMemoryPropertyFlags           properties,
                                                        const VkAllocationCallbacks*    pAllocator,
                                                        VkDeviceMemory*                 pMemory)
{
    VkExportMemoryAllocateInfo export_info;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT((VkExternalMemoryHandleTypeFlags)0 != handleTypes);
    LOGASSERT(NULL != pMemory);

    export_info.sType       = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO;
    export_info.pNext       = pNext;
    export_info.handleTypes = handleTypes;
    Gfx_Vulkan_AllocateDedicatedBufferMemory(physicalDevice, device, &export_info, buffer, properties, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateExternalBufferMemory(VkPhysicalDevice             physicalDevice,
                                             VkDevice                     device,
                                             VkBuffer                     buffer,
                                             uint32_t                     externalMemoryTypeBits,
                                             const void*                  pNext,
                                             VkMemoryPropertyFlags        properties,
                                             const VkAllocationCallbacks* pAllocator,
                                             VkDeviceMemory*              pMemory)
{
    VkDeviceSize allocation_size;
    uint32_t     memory_type_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT(0 != externalMemoryTypeBits);
    LOGASSERT(NULL != pNext);
    LOGASSERT(NULL != pMemory);

    Gfx_Vulkan_GetBufferMemoryAllocationInfo(physicalDevice, device, buffer, externalMemoryTypeBits, properties, &allocation_size, &memory_type_index);

    Gfx_Vulkan_AllocateMemory(device, pNext, allocation_size, memory_type_index, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateDedicatedExternalBufferMemory(VkPhysicalDevice             physicalDevice,
                                                      VkDevice                     device,
                                                      const void*                  pNext,
                                                      VkBuffer                     buffer,
                                                      uint32_t                     externalMemoryTypeBits,
                                                      VkMemoryPropertyFlags        properties,
                                                      const VkAllocationCallbacks* pAllocator,
                                                      VkDeviceMemory*              pMemory)
{
    VkDeviceSize allocation_size;
    uint32_t     memory_type_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != pNext);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT(0 != externalMemoryTypeBits);
    LOGASSERT(NULL != pMemory);

    Gfx_Vulkan_GetBufferMemoryAllocationInfo(physicalDevice, device, buffer, externalMemoryTypeBits, properties, &allocation_size, &memory_type_index);

    Gfx_Vulkan_AllocateDedicatedMemory(device, pNext, VK_NULL_HANDLE, buffer, allocation_size, memory_type_index, pAllocator, pMemory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupExportableMemoryBuffer(VkPhysicalDevice                physicalDevice,
                                            VkDevice                        device,
                                            const void*                     pBufferNext,
                                            VkExternalMemoryHandleTypeFlags handleTypes,
                                            VkBufferCreateFlags             flags,
                                            VkDeviceSize                    size,
                                            VkBufferUsageFlags              usage,
                                            VkSharingMode                   sharingMode,
                                            uint32_t                        queueFamilyIndexCount,
                                            const uint32_t*                 pQueueFamilyIndices,
                                            const void*                     pMemoryNext,
                                            VkMemoryPropertyFlags           properties,
                                            const VkAllocationCallbacks*    pAllocator,
                                            VkBuffer*                       pBuffer,
                                            VkDeviceMemory*                 pMemory)
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT((VkExternalMemoryHandleTypeFlags)0 != handleTypes);
    LOGASSERT(0 != size);
    LOGASSERT((VkBufferUsageFlags)0 != usage);
    LOGASSERT(0 == queueFamilyIndexCount || NULL != pQueueFamilyIndices);
    LOGASSERT(NULL != pBuffer);
    LOGASSERT(NULL != pMemory);

    Gfx_Vulkan_CreateExternalMemoryBuffer(device, pBufferNext, handleTypes, flags, size, usage, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices, pAllocator, pBuffer);
    Gfx_Vulkan_AllocateExportableBufferMemory(physicalDevice, device, (*pBuffer), pMemoryNext, handleTypes, properties, pAllocator, pMemory);
    Gfx_Vulkan_BindBufferMemory(device, (*pBuffer), (*pMemory), VK_NO_OFFSET);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupExportedDedicatedMemoryBuffer(VkPhysicalDevice                physicalDevice,
                                                   VkDevice                        device,
                                                   const void*                     pBufferNext,
                                                   VkExternalMemoryHandleTypeFlags handleTypes,
                                                   VkBufferCreateFlags             flags,
                                                   VkDeviceSize                    size,
                                                   VkBufferUsageFlags              usage,
                                                   VkSharingMode                   sharingMode,
                                                   uint32_t                        queueFamilyIndexCount,
                                                   const uint32_t*                 pQueueFamilyIndices,
                                                   const void*                     pMemoryNext,
                                                   VkMemoryPropertyFlags           properties,
                                                   const VkAllocationCallbacks*    pAllocator,
                                                   VkBuffer*                       pBuffer,
                                                   VkDeviceMemory*                 pMemory)
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT((VkExternalMemoryHandleTypeFlags)0 != handleTypes);
    LOGASSERT(0 != size);
    LOGASSERT((VkBufferUsageFlags)0 != usage);
    LOGASSERT(0 == queueFamilyIndexCount || NULL != pQueueFamilyIndices);
    LOGASSERT(NULL != pBuffer);
    LOGASSERT(NULL != pMemory);

    Gfx_Vulkan_CreateExternalMemoryBuffer(device, pBufferNext, handleTypes, flags, size, usage, sharingMode, queueFamilyIndexCount, pQueueFamilyIndices, pAllocator, pBuffer);
    Gfx_Vulkan_AllocateExportableDedicatedBufferMemory(physicalDevice, device, (*pBuffer), pMemoryNext, handleTypes, properties, pAllocator, pMemory);
    Gfx_Vulkan_BindBufferMemory(device, (*pBuffer), (*pMemory), VK_NO_OFFSET);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT(VK_NULL_HANDLE != memory);

    VKCHECK(vkBindBufferMemory(device, buffer, memory, memoryOffset));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != memory);
    LOGASSERT(0 != size);
    LOGASSERT(NULL != ppData);

    (*ppData) = NULL;
    VKCHECK(vkMapMemory(device, memory, offset, size, flags, ppData));
    LOGASSERT(NULL != (*ppData));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_UnmapMemory(VkDevice device, VkDeviceMemory memory)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != memory);

    vkUnmapMemory(device, memory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_InvalidateMappedMemoryRanges(VkDevice device, uint32_t rangeCount, const VkMappedMemoryRange* pRanges)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != rangeCount);
    LOGASSERT(NULL != pRanges);

    VKCHECK(vkInvalidateMappedMemoryRanges(device, rangeCount, pRanges));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_InvalidateMappedMemoryRange(VkDevice device, const void* pNext, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size)
{
    VkMappedMemoryRange range;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != memory);
    LOGASSERT(0 != size);

    range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.pNext  = pNext;
    range.memory = memory;
    range.offset = offset;
    range.size   = size;
    Gfx_Vulkan_InvalidateMappedMemoryRanges(device, 1, &range);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_FlushMappedMemoryRanges(VkDevice device, uint32_t rangeCount, const VkMappedMemoryRange* pRanges)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != rangeCount);
    LOGASSERT(NULL != pRanges);

    VKCHECK(vkFlushMappedMemoryRanges(device, rangeCount, pRanges));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_FlushMappedMemoryRange(VkDevice device, const void* pNext, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size)
{
    VkMappedMemoryRange range;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != memory);
    LOGASSERT(0 != size);

    range.sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    range.pNext  = pNext;
    range.memory = memory;
    range.offset = offset;
    range.size   = size;
    Gfx_Vulkan_FlushMappedMemoryRanges(device, 1, &range);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_ReadMemory(VkDevice         device,
                           const void*      pNext,
                           VkDeviceMemory   memory,
                           VkDeviceSize     offset,
                           VkDeviceSize     size,
                           VkDeviceSize     mappedSize,
                           VkMemoryMapFlags flags,
                           VkDeviceSize     nonCoherentAtomSize,
                           void*            dst)
{
    byte_t*      src;
    VkDeviceSize mapped_offset;
    VkDeviceSize data_offset;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != memory);
    LOGASSERT(0 != size);
    LOGASSERT(VK_WHOLE_SIZE != size);
    LOGASSERT(mappedSize);
    LOGASSERT(NULL != dst);
    LOGASSERT(SIZE_MAX >= size);

    mapped_offset = GetNonCoherentMappedOffset(offset, nonCoherentAtomSize);
    data_offset   = offset - mapped_offset;
    LOGASSERT(SIZE_MAX >= data_offset);
    LOGASSERT(data_offset <= SIZE_MAX - size);
    LOGASSERT(data_offset <= mappedSize);
    LOGASSERT(size <= mappedSize - data_offset);

    Gfx_Vulkan_MapMemory(device, memory, mapped_offset, mappedSize, flags, (void**)&src);

    Gfx_Vulkan_InvalidateMappedMemoryRange(device, pNext, memory, mapped_offset, mappedSize);

    (void)memcpy(dst, src + (size_t)data_offset, (size_t)size);

    Gfx_Vulkan_UnmapMemory(device, memory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_WriteMemory(VkDevice         device,
                            const void*      pNext,
                            VkDeviceMemory   memory,
                            VkDeviceSize     offset,
                            VkDeviceSize     size,
                            VkDeviceSize     mappedSize,
                            VkMemoryMapFlags flags,
                            VkDeviceSize     nonCoherentAtomSize,
                            const void*      src)
{
    byte_t*      dst;
    VkDeviceSize mapped_offset;
    VkDeviceSize data_offset;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != memory);
    LOGASSERT(0 != size);
    LOGASSERT(VK_WHOLE_SIZE != size);
    LOGASSERT(mappedSize);
    LOGASSERT(NULL != src);
    LOGASSERT(SIZE_MAX >= size);

    mapped_offset = GetNonCoherentMappedOffset(offset, nonCoherentAtomSize);
    data_offset   = offset - mapped_offset;
    LOGASSERT(SIZE_MAX >= data_offset);
    LOGASSERT(data_offset <= SIZE_MAX - size);
    LOGASSERT(data_offset <= mappedSize);
    LOGASSERT(size <= mappedSize - data_offset);

    Gfx_Vulkan_MapMemory(device, memory, mapped_offset, mappedSize, flags, (void**)&dst);

    (void)memcpy(dst + (size_t)data_offset, src, (size_t)size);
    Gfx_Vulkan_FlushMappedMemoryRange(device, pNext, memory, mapped_offset, mappedSize);
    Gfx_Vulkan_UnmapMemory(device, memory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_ReadCoherentMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void* dst)
{
    void* src;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != memory);
    LOGASSERT(0 != size);
    LOGASSERT(VK_WHOLE_SIZE != size);
    LOGASSERT(NULL != dst);
    LOGASSERT(SIZE_MAX >= size);

    Gfx_Vulkan_MapMemory(device, memory, offset, size, flags, &src);

    (void)memcpy(dst, src, (size_t)size);

    Gfx_Vulkan_UnmapMemory(device, memory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_WriteCoherentMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, const void* src)
{
    void* dst;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != memory);
    LOGASSERT(0 != size);
    LOGASSERT(VK_WHOLE_SIZE != size);
    LOGASSERT(NULL != src);
    LOGASSERT(SIZE_MAX >= size);

    Gfx_Vulkan_MapMemory(device, memory, offset, size, flags, &dst);

    (void)memcpy(dst, src, (size_t)size);

    Gfx_Vulkan_UnmapMemory(device, memory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetImageSubresourceLayout(VkDevice device, VkImage image, VkImageSubresource subresource, VkSubresourceLayout* pLayout)
{
    VkSubresourceLayout layout;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT(NULL != pLayout);

    vkGetImageSubresourceLayout(device, image, &subresource, &layout);

    (*pLayout) = layout;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateDescriptorSetLayout(VkDevice                            device,
                                          const void*                         pNext,
                                          VkDescriptorSetLayoutCreateFlags    flags,
                                          uint32_t                            bindingCount,
                                          const VkDescriptorSetLayoutBinding* pBindings,
                                          const VkAllocationCallbacks*        pAllocator,
                                          VkDescriptorSetLayout*              pSetLayout)
{
    VkDescriptorSetLayoutCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 == bindingCount || NULL != pBindings);
    LOGASSERT(NULL != pSetLayout);

    create_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    create_info.pNext        = pNext;
    create_info.flags        = flags;
    create_info.bindingCount = bindingCount;
    create_info.pBindings    = pBindings;
    VKCHECK(vkCreateDescriptorSetLayout(device, &create_info, pAllocator, pSetLayout));
    LOGASSERT(VK_NULL_HANDLE != (*pSetLayout));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateDescriptorPool(VkDevice                     device,
                                     const void*                  pNext,
                                     VkDescriptorPoolCreateFlags  flags,
                                     uint32_t                     maxSets,
                                     uint32_t                     poolSizeCount,
                                     const VkDescriptorPoolSize*  pPoolSizes,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkDescriptorPool*            pDescriptorPool)
{
    VkDescriptorPoolCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != maxSets);
    LOGASSERT(0 == poolSizeCount || NULL != pPoolSizes);
    LOGASSERT(NULL != pDescriptorPool);

    create_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    create_info.pNext         = pNext;
    create_info.flags         = flags;
    create_info.maxSets       = maxSets;
    create_info.poolSizeCount = poolSizeCount;
    create_info.pPoolSizes    = pPoolSizes;
    VKCHECK(vkCreateDescriptorPool(device, &create_info, pAllocator, pDescriptorPool));
    LOGASSERT(VK_NULL_HANDLE != (*pDescriptorPool));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateDescriptorSets(VkDevice                     device,
                                       const void*                  pNext,
                                       VkDescriptorPool             descriptorPool,
                                       uint32_t                     descriptorSetCount,
                                       const VkDescriptorSetLayout* pSetLayouts,
                                       VkDescriptorSet*             pDescriptorSets)
{
    VkDescriptorSetAllocateInfo allocate_info;
    uint32_t                    descriptor_set_index;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != descriptorPool);
    LOGASSERT(0 != descriptorSetCount);
    LOGASSERT(NULL != pSetLayouts);
    LOGASSERT(NULL != pDescriptorSets);

    allocate_info.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate_info.pNext              = pNext;
    allocate_info.descriptorPool     = descriptorPool;
    allocate_info.descriptorSetCount = descriptorSetCount;
    allocate_info.pSetLayouts        = pSetLayouts;
    VKCHECK(vkAllocateDescriptorSets(device, &allocate_info, pDescriptorSets));
    for (descriptor_set_index = 0; descriptor_set_index < descriptorSetCount; ++descriptor_set_index)
    {
        LOGASSERT(VK_NULL_HANDLE != pDescriptorSets[descriptor_set_index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateDescriptorSet(VkDevice                         device,
                                    const void*                      pLayoutNext,
                                    VkDescriptorSetLayoutCreateFlags layoutFlags,
                                    uint32_t                         bindingCount,
                                    const uint32_t*                  pBindings,
                                    const VkDescriptorType*          pDescriptorTypes,
                                    const uint32_t*                  pDescriptorCounts,
                                    const VkShaderStageFlags*        pStageFlags,
                                    const VkSampler* const*          ppImmutableSamplers,
                                    const void*                      pPoolNext,
                                    VkDescriptorPoolCreateFlags      poolFlags,
                                    const VkAllocationCallbacks*     pAllocator,
                                    VkDescriptorSetLayout*           pDescriptorSetLayout,
                                    VkDescriptorPool*                pDescriptorPool,
                                    VkDescriptorSet*                 pDescriptorSet)
{
    VkDescriptorSetLayoutBinding bindings[VK_MAX_DESCRIPTOR_SET_BINDINGS];
    VkDescriptorPoolSize         pool_sizes[VK_MAX_DESCRIPTOR_SET_BINDINGS];
    uint32_t                     pool_size_count;
    uint32_t                     binding_index;
    uint32_t                     pool_size_index;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != bindingCount);
    LOGASSERT(VK_MAX_DESCRIPTOR_SET_BINDINGS >= bindingCount);
    LOGASSERT(NULL != pBindings);
    LOGASSERT(NULL != pDescriptorTypes);
    LOGASSERT(NULL != pDescriptorCounts);
    LOGASSERT(NULL != pStageFlags);
    LOGASSERT(NULL != pDescriptorSetLayout);
    LOGASSERT(NULL != pDescriptorPool);
    LOGASSERT(NULL != pDescriptorSet);

    (void)memset(pool_sizes, 0, sizeof(pool_sizes));
    pool_size_count = 0;
    for (binding_index = 0; binding_index < bindingCount; ++binding_index)
    {
        LOGASSERT(0 != pDescriptorCounts[binding_index]);
        LOGASSERT((VkShaderStageFlags)0 != pStageFlags[binding_index]);
        bindings[binding_index].binding            = pBindings[binding_index];
        bindings[binding_index].descriptorType     = pDescriptorTypes[binding_index];
        bindings[binding_index].descriptorCount    = pDescriptorCounts[binding_index];
        bindings[binding_index].stageFlags         = pStageFlags[binding_index];
        bindings[binding_index].pImmutableSamplers = NULL != ppImmutableSamplers ? ppImmutableSamplers[binding_index] : NULL;
        for (pool_size_index = 0; pool_size_index < pool_size_count; ++pool_size_index)
        {
            if (pool_sizes[pool_size_index].type == pDescriptorTypes[binding_index])
            {
                break;
            }
        }
        if (pool_size_count == pool_size_index)
        {
            pool_sizes[pool_size_index].type = pDescriptorTypes[binding_index];
            ++pool_size_count;
        }
        LOGASSERT(pool_sizes[pool_size_index].descriptorCount <= UINT32_MAX - pDescriptorCounts[binding_index]);
        pool_sizes[pool_size_index].descriptorCount += pDescriptorCounts[binding_index];
    }
    Gfx_Vulkan_CreateDescriptorSetLayout(device, pLayoutNext, layoutFlags, bindingCount, bindings, pAllocator, pDescriptorSetLayout);
    Gfx_Vulkan_CreateDescriptorPool(device, pPoolNext, poolFlags, 1, pool_size_count, pool_sizes, pAllocator, pDescriptorPool);
    Gfx_Vulkan_AllocateDescriptorSets(device, NULL, (*pDescriptorPool), 1, pDescriptorSetLayout, pDescriptorSet);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_UpdateDescriptorSets(VkDevice                    device,
                                     uint32_t                    descriptorWriteCount,
                                     const VkWriteDescriptorSet* pDescriptorWrites,
                                     uint32_t                    descriptorCopyCount,
                                     const VkCopyDescriptorSet*  pDescriptorCopies)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 == descriptorWriteCount || NULL != pDescriptorWrites);
    LOGASSERT(0 == descriptorCopyCount || NULL != pDescriptorCopies);

    vkUpdateDescriptorSets(device, descriptorWriteCount, pDescriptorWrites, descriptorCopyCount, pDescriptorCopies);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_UpdateDescriptorSet(VkDevice                      device,
                                    const void*                   pNext,
                                    VkDescriptorSet               dstSet,
                                    uint32_t                      dstBinding,
                                    uint32_t                      dstArrayElement,
                                    uint32_t                      descriptorCount,
                                    VkDescriptorType              descriptorType,
                                    const VkDescriptorImageInfo*  pImageInfo,
                                    const VkDescriptorBufferInfo* pBufferInfo,
                                    const VkBufferView*           pTexelBufferView)
{
    VkWriteDescriptorSet write;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != dstSet);
    LOGASSERT(descriptorCount);

    write.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.pNext            = pNext;
    write.dstSet           = dstSet;
    write.dstBinding       = dstBinding;
    write.dstArrayElement  = dstArrayElement;
    write.descriptorCount  = descriptorCount;
    write.descriptorType   = descriptorType;
    write.pImageInfo       = pImageInfo;
    write.pBufferInfo      = pBufferInfo;
    write.pTexelBufferView = pTexelBufferView;
    Gfx_Vulkan_UpdateDescriptorSets(device, 1, &write, 0, NULL);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_UpdateImageDescriptorSet(VkDevice         device,
                                         const void*      pNext,
                                         VkDescriptorSet  dstSet,
                                         uint32_t         dstBinding,
                                         uint32_t         dstArrayElement,
                                         VkDescriptorType descriptorType,
                                         VkSampler        sampler,
                                         VkImageView      imageView,
                                         VkImageLayout    imageLayout)
{
    VkDescriptorImageInfo image_info;

    LOGASSERT(VK_DESCRIPTOR_TYPE_SAMPLER == descriptorType || VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER == descriptorType || VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE == descriptorType ||
              VK_DESCRIPTOR_TYPE_STORAGE_IMAGE == descriptorType || VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT == descriptorType);
    LOGASSERT(VK_DESCRIPTOR_TYPE_SAMPLER == descriptorType || VK_NULL_HANDLE != imageView);

    image_info.sampler     = sampler;
    image_info.imageView   = imageView;
    image_info.imageLayout = imageLayout;
    Gfx_Vulkan_UpdateDescriptorSet(device, pNext, dstSet, dstBinding, dstArrayElement, 1, descriptorType, &image_info, NULL, NULL);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_UpdateBufferDescriptorSet(VkDevice         device,
                                          const void*      pNext,
                                          VkDescriptorSet  dstSet,
                                          uint32_t         dstBinding,
                                          uint32_t         dstArrayElement,
                                          VkDescriptorType descriptorType,
                                          VkBuffer         buffer,
                                          VkDeviceSize     offset,
                                          VkDeviceSize     range)
{
    VkDescriptorBufferInfo buffer_info;

    LOGASSERT(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER == descriptorType || VK_DESCRIPTOR_TYPE_STORAGE_BUFFER == descriptorType || VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC == descriptorType ||
              VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC == descriptorType);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT(0 != range);

    buffer_info.buffer = buffer;
    buffer_info.offset = offset;
    buffer_info.range  = range;
    Gfx_Vulkan_UpdateDescriptorSet(device, pNext, dstSet, dstBinding, dstArrayElement, 1, descriptorType, NULL, &buffer_info, NULL);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreatePipelineLayout(VkDevice                     device,
                                     const void*                  pNext,
                                     VkPipelineLayoutCreateFlags  flags,
                                     uint32_t                     setLayoutCount,
                                     const VkDescriptorSetLayout* pSetLayouts,
                                     uint32_t                     pushConstantRangeCount,
                                     const VkPushConstantRange*   pPushConstantRanges,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkPipelineLayout*            pPipelineLayout)
{
    VkPipelineLayoutCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 == setLayoutCount || NULL != pSetLayouts);
    LOGASSERT(0 == pushConstantRangeCount || NULL != pPushConstantRanges);
    LOGASSERT(NULL != pPipelineLayout);

    create_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    create_info.pNext                  = pNext;
    create_info.flags                  = flags;
    create_info.setLayoutCount         = setLayoutCount;
    create_info.pSetLayouts            = pSetLayouts;
    create_info.pushConstantRangeCount = pushConstantRangeCount;
    create_info.pPushConstantRanges    = pPushConstantRanges;
    VKCHECK(vkCreatePipelineLayout(device, &create_info, pAllocator, pPipelineLayout));
    LOGASSERT(VK_NULL_HANDLE != (*pPipelineLayout));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateComputePipelineLayout(VkDevice                     device,
                                            const void*                  pNext,
                                            VkPipelineLayoutCreateFlags  flags,
                                            uint32_t                     setLayoutCount,
                                            const VkDescriptorSetLayout* pSetLayouts,
                                            uint32_t                     pushConstantOffset,
                                            uint32_t                     pushConstantSize,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkPipelineLayout*            pPipelineLayout)
{
    VkPushConstantRange push_constant_range;
    uint32_t            push_constant_range_count;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 == setLayoutCount || NULL != pSetLayouts);
    LOGASSERT(NULL != pPipelineLayout);
    LOGASSERT(0 == pushConstantOffset % sizeof(uint32_t));
    LOGASSERT(0 == pushConstantSize % sizeof(uint32_t));

    push_constant_range_count = 0;
    if (pushConstantSize)
    {
        push_constant_range_count = 1;
    }

    push_constant_range.stageFlags = (VkShaderStageFlags)VK_SHADER_STAGE_COMPUTE_BIT;
    push_constant_range.offset     = pushConstantOffset;
    push_constant_range.size       = pushConstantSize;
    Gfx_Vulkan_CreatePipelineLayout(device, pNext, flags, setLayoutCount, pSetLayouts, push_constant_range_count, &push_constant_range, pAllocator, pPipelineLayout);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateShaderModule(VkDevice                     device,
                                   const void*                  pNext,
                                   VkShaderModuleCreateFlags    flags,
                                   size_t                       codeSize,
                                   const uint32_t*              pCode,
                                   const VkAllocationCallbacks* pAllocator,
                                   VkShaderModule*              pShaderModule)
{
    VkShaderModuleCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(codeSize);
    LOGASSERT(0 == codeSize % sizeof((*pCode)));
    LOGASSERT(NULL != pCode);
    LOGASSERT(NULL != pShaderModule);

    create_info.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    create_info.pNext    = pNext;
    create_info.flags    = flags;
    create_info.codeSize = codeSize;
    create_info.pCode    = pCode;
    VKCHECK(vkCreateShaderModule(device, &create_info, pAllocator, pShaderModule));
    LOGASSERT(VK_NULL_HANDLE != (*pShaderModule));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateComputePipelines(VkDevice                           device,
                                       VkPipelineCache                    pipelineCache,
                                       uint32_t                           createInfoCount,
                                       const VkComputePipelineCreateInfo* pCreateInfos,
                                       const VkAllocationCallbacks*       pAllocator,
                                       VkPipeline*                        pPipelines)
{
    uint32_t pipeline_index;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != createInfoCount);
    LOGASSERT(NULL != pCreateInfos);
    LOGASSERT(NULL != pPipelines);

    VKCHECK(vkCreateComputePipelines(device, pipelineCache, createInfoCount, pCreateInfos, pAllocator, pPipelines));
    for (pipeline_index = 0; pipeline_index < createInfoCount; ++pipeline_index)
    {
        LOGASSERT(VK_NULL_HANDLE != pPipelines[pipeline_index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateComputePipeline(VkDevice                        device,
                                      VkPipelineCache                 pipelineCache,
                                      const void*                     pNext,
                                      VkPipelineCreateFlags           flags,
                                      VkPipelineShaderStageCreateInfo stage,
                                      VkPipelineLayout                layout,
                                      VkPipeline                      basePipelineHandle,
                                      int32_t                         basePipelineIndex,
                                      const VkAllocationCallbacks*    pAllocator,
                                      VkPipeline*                     pPipeline)
{
    VkComputePipelineCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_SHADER_STAGE_COMPUTE_BIT == stage.stage);
    LOGASSERT(VK_NULL_HANDLE != layout);
    LOGASSERT(NULL != pPipeline);

    create_info.sType              = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    create_info.pNext              = pNext;
    create_info.flags              = flags;
    create_info.stage              = stage;
    create_info.layout             = layout;
    create_info.basePipelineHandle = basePipelineHandle;
    create_info.basePipelineIndex  = basePipelineIndex;
    Gfx_Vulkan_CreateComputePipelines(device, pipelineCache, 1, &create_info, pAllocator, pPipeline);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupComputePipeline(VkDevice                         device,
                                     const void*                      pPipelineLayoutNext,
                                     VkPipelineLayoutCreateFlags      pipelineLayoutFlags,
                                     uint32_t                         setLayoutCount,
                                     const VkDescriptorSetLayout*     pSetLayouts,
                                     uint32_t                         pushConstantOffset,
                                     uint32_t                         pushConstantSize,
                                     const void*                      pShaderModuleNext,
                                     VkShaderModuleCreateFlags        shaderModuleFlags,
                                     size_t                           shaderCodeSize,
                                     const uint32_t*                  pShaderCode,
                                     VkPipelineCache                  pipelineCache,
                                     const void*                      pPipelineNext,
                                     VkPipelineCreateFlags            pipelineFlags,
                                     const void*                      pStageNext,
                                     VkPipelineShaderStageCreateFlags stageFlags,
                                     const char*                      pEntryPoint,
                                     const VkSpecializationInfo*      pSpecializationInfo,
                                     VkPipeline                       basePipelineHandle,
                                     int32_t                          basePipelineIndex,
                                     const VkAllocationCallbacks*     pAllocator,
                                     VkShaderModule*                  pShaderModule,
                                     VkPipelineLayout*                pPipelineLayout,
                                     VkPipeline*                      pPipeline)
{
    VkPipelineShaderStageCreateInfo stage;

    LOGASSERT(NULL != pEntryPoint);
    LOGASSERT('\0' != pEntryPoint[0]);

    Gfx_Vulkan_CreateShaderModule(device, pShaderModuleNext, shaderModuleFlags, shaderCodeSize, pShaderCode, pAllocator, pShaderModule);
    stage.sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stage.pNext               = pStageNext;
    stage.flags               = stageFlags;
    stage.stage               = VK_SHADER_STAGE_COMPUTE_BIT;
    stage.module              = (*pShaderModule);
    stage.pName               = pEntryPoint;
    stage.pSpecializationInfo = pSpecializationInfo;
    Gfx_Vulkan_CreateComputePipelineLayout(device, pPipelineLayoutNext, pipelineLayoutFlags, setLayoutCount, pSetLayouts, pushConstantOffset, pushConstantSize, pAllocator, pPipelineLayout);
    Gfx_Vulkan_CreateComputePipeline(device, pipelineCache, pPipelineNext, pipelineFlags, stage, (*pPipelineLayout), basePipelineHandle, basePipelineIndex, pAllocator, pPipeline);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreatePipelineCache(VkDevice                     device,
                                    const void*                  pNext,
                                    VkPipelineCacheCreateFlags   flags,
                                    size_t                       initialDataSize,
                                    const void*                  pInitialData,
                                    const VkAllocationCallbacks* pAllocator,
                                    VkPipelineCache*             pPipelineCache)
{
    VkPipelineCacheCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 == initialDataSize || NULL != pInitialData);
    LOGASSERT(NULL != pPipelineCache);

    create_info.sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    create_info.pNext           = pNext;
    create_info.flags           = flags;
    create_info.initialDataSize = initialDataSize;
    create_info.pInitialData    = pInitialData;
    VKCHECK(vkCreatePipelineCache(device, &create_info, pAllocator, pPipelineCache));
    LOGASSERT(VK_NULL_HANDLE != (*pPipelineCache));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateCommandPool(VkDevice device, const void* pNext, VkCommandPoolCreateFlags flags, uint32_t queueFamilyIndex, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool)
{
    VkCommandPoolCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_INVALID != queueFamilyIndex);
    LOGASSERT(NULL != pCommandPool);

    create_info.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    create_info.pNext            = pNext;
    create_info.flags            = flags;
    create_info.queueFamilyIndex = queueFamilyIndex;
    VKCHECK(vkCreateCommandPool(device, &create_info, pAllocator, pCommandPool));
    LOGASSERT(VK_NULL_HANDLE != (*pCommandPool));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AllocateCommandBuffers(VkDevice device, const void* pNext, VkCommandPool commandPool, VkCommandBufferLevel level, uint32_t commandBufferCount, VkCommandBuffer* pCommandBuffers)
{
    VkCommandBufferAllocateInfo allocate_info;
    uint32_t                    command_buffer_index;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != commandPool);
    LOGASSERT(0 != commandBufferCount);
    LOGASSERT(NULL != pCommandBuffers);

    allocate_info.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate_info.pNext              = pNext;
    allocate_info.commandPool        = commandPool;
    allocate_info.level              = level;
    allocate_info.commandBufferCount = commandBufferCount;
    VKCHECK(vkAllocateCommandBuffers(device, &allocate_info, pCommandBuffers));
    for (command_buffer_index = 0; command_buffer_index < commandBufferCount; ++command_buffer_index)
    {
        LOGASSERT(VK_NULL_HANDLE != pCommandBuffers[command_buffer_index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupCommandPool(VkDevice                     device,
                                 const void*                  pCommandPoolNext,
                                 VkCommandPoolCreateFlags     commandPoolFlags,
                                 uint32_t                     queueFamilyIndex,
                                 const void*                  pCommandBufferNext,
                                 VkCommandBufferLevel         commandBufferLevel,
                                 uint32_t                     commandBufferCount,
                                 const VkAllocationCallbacks* pAllocator,
                                 VkCommandPool*               pCommandPool,
                                 VkCommandBuffer*             pCommandBuffers)
{
    Gfx_Vulkan_CreateCommandPool(device, pCommandPoolNext, commandPoolFlags, queueFamilyIndex, pAllocator, pCommandPool);
    Gfx_Vulkan_AllocateCommandBuffers(device, pCommandBufferNext, (*pCommandPool), commandBufferLevel, commandBufferCount, pCommandBuffers);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != commandPool);

    VKCHECK(vkResetCommandPool(device, commandPool, flags));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_BeginCommandBuffer(VkCommandBuffer commandBuffer, const void* pNext, VkCommandBufferUsageFlags flags, const VkCommandBufferInheritanceInfo* pInheritanceInfo)
{
    VkCommandBufferBeginInfo begin_info;

    LOGASSERT(VK_NULL_HANDLE != commandBuffer);

    begin_info.sType            = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin_info.pNext            = pNext;
    begin_info.flags            = flags;
    begin_info.pInheritanceInfo = pInheritanceInfo;
    VKCHECK(vkBeginCommandBuffer(commandBuffer, &begin_info));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);

    VKCHECK(vkEndCommandBuffer(commandBuffer));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdPipelineBarrier(VkCommandBuffer              commandBuffer,
                                   VkPipelineStageFlags         srcStageMask,
                                   VkPipelineStageFlags         dstStageMask,
                                   VkDependencyFlags            dependencyFlags,
                                   uint32_t                     memoryBarrierCount,
                                   const VkMemoryBarrier*       pMemoryBarriers,
                                   uint32_t                     bufferMemoryBarrierCount,
                                   const VkBufferMemoryBarrier* pBufferMemoryBarriers,
                                   uint32_t                     imageMemoryBarrierCount,
                                   const VkImageMemoryBarrier*  pImageMemoryBarriers)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT((VkPipelineStageFlags)0 != srcStageMask);
    LOGASSERT((VkPipelineStageFlags)0 != dstStageMask);
    LOGASSERT(0 == memoryBarrierCount || NULL != pMemoryBarriers);
    LOGASSERT(0 == bufferMemoryBarrierCount || NULL != pBufferMemoryBarriers);
    LOGASSERT(0 == imageMemoryBarrierCount || NULL != pImageMemoryBarriers);

    vkCmdPipelineBarrier(commandBuffer,
                         srcStageMask,
                         dstStageMask,
                         dependencyFlags,
                         memoryBarrierCount,
                         pMemoryBarriers,
                         bufferMemoryBarrierCount,
                         pBufferMemoryBarriers,
                         imageMemoryBarrierCount,
                         pImageMemoryBarriers);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdImageQueueOwnershipBarrier(VkCommandBuffer         commandBuffer,
                                              VkPipelineStageFlags    srcStageMask,
                                              VkPipelineStageFlags    dstStageMask,
                                              VkDependencyFlags       dependencyFlags,
                                              const void*             pNext,
                                              VkAccessFlags           srcAccessMask,
                                              VkAccessFlags           dstAccessMask,
                                              VkImageLayout           oldLayout,
                                              VkImageLayout           newLayout,
                                              uint32_t                srcQueueFamilyIndex,
                                              uint32_t                dstQueueFamilyIndex,
                                              VkImage                 image,
                                              VkImageSubresourceRange subresourceRange)
{
    VkImageMemoryBarrier barrier;

    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT((VkPipelineStageFlags)0 != srcStageMask);
    LOGASSERT((VkPipelineStageFlags)0 != dstStageMask);
    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT((VkImageAspectFlags)0 != subresourceRange.aspectMask);
    LOGASSERT(0 != subresourceRange.levelCount);
    LOGASSERT(0 != subresourceRange.layerCount);

    barrier.sType               = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.pNext               = pNext;
    barrier.srcAccessMask       = srcAccessMask;
    barrier.dstAccessMask       = dstAccessMask;
    barrier.oldLayout           = oldLayout;
    barrier.newLayout           = newLayout;
    barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
    barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
    barrier.image               = image;
    barrier.subresourceRange    = subresourceRange;
    Gfx_Vulkan_CmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, 0, NULL, 0, NULL, 1, &barrier);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdImageBarrier(VkCommandBuffer         commandBuffer,
                                VkPipelineStageFlags    srcStageMask,
                                VkPipelineStageFlags    dstStageMask,
                                VkDependencyFlags       dependencyFlags,
                                const void*             pNext,
                                VkAccessFlags           srcAccessMask,
                                VkAccessFlags           dstAccessMask,
                                VkImageLayout           oldLayout,
                                VkImageLayout           newLayout,
                                VkImage                 image,
                                VkImageSubresourceRange subresourceRange)
{
    Gfx_Vulkan_CmdImageQueueOwnershipBarrier(commandBuffer,
                                             srcStageMask,
                                             dstStageMask,
                                             dependencyFlags,
                                             pNext,
                                             srcAccessMask,
                                             dstAccessMask,
                                             oldLayout,
                                             newLayout,
                                             VK_QUEUE_FAMILY_IGNORED,
                                             VK_QUEUE_FAMILY_IGNORED,
                                             image,
                                             subresourceRange);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdAcquireImageFromExternal(VkCommandBuffer         commandBuffer,
                                            VkPipelineStageFlags    dstStageMask,
                                            VkDependencyFlags       dependencyFlags,
                                            const void*             pNext,
                                            VkAccessFlags           dstAccessMask,
                                            VkImageLayout           oldLayout,
                                            VkImageLayout           newLayout,
                                            uint32_t                dstQueueFamilyIndex,
                                            VkImage                 image,
                                            VkImageSubresourceRange subresourceRange)
{
    Gfx_Vulkan_CmdImageQueueOwnershipBarrier(commandBuffer,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                             dstStageMask,
                                             dependencyFlags,
                                             pNext,
                                             (VkAccessFlags)0,
                                             dstAccessMask,
                                             oldLayout,
                                             newLayout,
                                             VK_QUEUE_FAMILY_EXTERNAL,
                                             dstQueueFamilyIndex,
                                             image,
                                             subresourceRange);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdReleaseImageToExternal(VkCommandBuffer         commandBuffer,
                                          VkPipelineStageFlags    srcStageMask,
                                          VkDependencyFlags       dependencyFlags,
                                          const void*             pNext,
                                          VkAccessFlags           srcAccessMask,
                                          VkImageLayout           oldLayout,
                                          VkImageLayout           newLayout,
                                          uint32_t                srcQueueFamilyIndex,
                                          VkImage                 image,
                                          VkImageSubresourceRange subresourceRange)
{
    Gfx_Vulkan_CmdImageQueueOwnershipBarrier(commandBuffer,
                                             srcStageMask,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                             dependencyFlags,
                                             pNext,
                                             srcAccessMask,
                                             (VkAccessFlags)0,
                                             oldLayout,
                                             newLayout,
                                             srcQueueFamilyIndex,
                                             VK_QUEUE_FAMILY_EXTERNAL,
                                             image,
                                             subresourceRange);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdBufferQueueOwnershipBarrier(VkCommandBuffer      commandBuffer,
                                               VkPipelineStageFlags srcStageMask,
                                               VkPipelineStageFlags dstStageMask,
                                               VkDependencyFlags    dependencyFlags,
                                               const void*          pNext,
                                               VkAccessFlags        srcAccessMask,
                                               VkAccessFlags        dstAccessMask,
                                               uint32_t             srcQueueFamilyIndex,
                                               uint32_t             dstQueueFamilyIndex,
                                               VkBuffer             buffer,
                                               VkDeviceSize         offset,
                                               VkDeviceSize         size)
{
    VkBufferMemoryBarrier barrier;

    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT((VkPipelineStageFlags)0 != srcStageMask);
    LOGASSERT((VkPipelineStageFlags)0 != dstStageMask);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT(0 != size);

    barrier.sType               = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
    barrier.pNext               = pNext;
    barrier.srcAccessMask       = srcAccessMask;
    barrier.dstAccessMask       = dstAccessMask;
    barrier.srcQueueFamilyIndex = srcQueueFamilyIndex;
    barrier.dstQueueFamilyIndex = dstQueueFamilyIndex;
    barrier.buffer              = buffer;
    barrier.offset              = offset;
    barrier.size                = size;
    Gfx_Vulkan_CmdPipelineBarrier(commandBuffer, srcStageMask, dstStageMask, dependencyFlags, 0, NULL, 1, &barrier, 0, NULL);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdBufferBarrier(VkCommandBuffer      commandBuffer,
                                 VkPipelineStageFlags srcStageMask,
                                 VkPipelineStageFlags dstStageMask,
                                 VkDependencyFlags    dependencyFlags,
                                 const void*          pNext,
                                 VkAccessFlags        srcAccessMask,
                                 VkAccessFlags        dstAccessMask,
                                 VkBuffer             buffer,
                                 VkDeviceSize         offset,
                                 VkDeviceSize         size)
{
    Gfx_Vulkan_CmdBufferQueueOwnershipBarrier(commandBuffer,
                                              srcStageMask,
                                              dstStageMask,
                                              dependencyFlags,
                                              pNext,
                                              srcAccessMask,
                                              dstAccessMask,
                                              VK_QUEUE_FAMILY_IGNORED,
                                              VK_QUEUE_FAMILY_IGNORED,
                                              buffer,
                                              offset,
                                              size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdAcquireBufferFromExternal(VkCommandBuffer      commandBuffer,
                                             VkPipelineStageFlags dstStageMask,
                                             VkDependencyFlags    dependencyFlags,
                                             const void*          pNext,
                                             VkAccessFlags        dstAccessMask,
                                             uint32_t             dstQueueFamilyIndex,
                                             VkBuffer             buffer,
                                             VkDeviceSize         offset,
                                             VkDeviceSize         size)
{
    Gfx_Vulkan_CmdBufferQueueOwnershipBarrier(commandBuffer,
                                              (VkPipelineStageFlags)VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                              dstStageMask,
                                              dependencyFlags,
                                              pNext,
                                              (VkAccessFlags)0,
                                              dstAccessMask,
                                              VK_QUEUE_FAMILY_EXTERNAL,
                                              dstQueueFamilyIndex,
                                              buffer,
                                              offset,
                                              size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdReleaseBufferToExternal(VkCommandBuffer      commandBuffer,
                                           VkPipelineStageFlags srcStageMask,
                                           VkDependencyFlags    dependencyFlags,
                                           const void*          pNext,
                                           VkAccessFlags        srcAccessMask,
                                           uint32_t             srcQueueFamilyIndex,
                                           VkBuffer             buffer,
                                           VkDeviceSize         offset,
                                           VkDeviceSize         size)
{
    Gfx_Vulkan_CmdBufferQueueOwnershipBarrier(commandBuffer,
                                              srcStageMask,
                                              (VkPipelineStageFlags)VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                              dependencyFlags,
                                              pNext,
                                              srcAccessMask,
                                              (VkAccessFlags)0,
                                              srcQueueFamilyIndex,
                                              VK_QUEUE_FAMILY_EXTERNAL,
                                              buffer,
                                              offset,
                                              size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdAcquireComputeWriteBufferQueueOwnership(VkCommandBuffer      commandBuffer,
                                                           VkPipelineStageFlags srcStageMask,
                                                           VkDependencyFlags    dependencyFlags,
                                                           const void*          pNext,
                                                           VkAccessFlags        srcAccessMask,
                                                           uint32_t             srcQueueFamilyIndex,
                                                           uint32_t             dstQueueFamilyIndex,
                                                           VkBuffer             buffer,
                                                           VkDeviceSize         offset,
                                                           VkDeviceSize         size)
{
    Gfx_Vulkan_CmdBufferQueueOwnershipBarrier(commandBuffer,
                                              srcStageMask,
                                              (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                              dependencyFlags,
                                              pNext,
                                              srcAccessMask,
                                              (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                                              srcQueueFamilyIndex,
                                              dstQueueFamilyIndex,
                                              buffer,
                                              offset,
                                              size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdAcquireComputeWriteBuffer(VkCommandBuffer      commandBuffer,
                                             VkPipelineStageFlags srcStageMask,
                                             VkDependencyFlags    dependencyFlags,
                                             const void*          pNext,
                                             VkAccessFlags        srcAccessMask,
                                             VkBuffer             buffer,
                                             VkDeviceSize         offset,
                                             VkDeviceSize         size)
{
    Gfx_Vulkan_CmdBufferBarrier(commandBuffer,
                                srcStageMask,
                                (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                dependencyFlags,
                                pNext,
                                srcAccessMask,
                                (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                                buffer,
                                offset,
                                size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdAcquireComputeWriteBufferFromExternal(VkCommandBuffer   commandBuffer,
                                                         VkDependencyFlags dependencyFlags,
                                                         const void*       pNext,
                                                         uint32_t          dstQueueFamilyIndex,
                                                         VkBuffer          buffer,
                                                         VkDeviceSize      offset,
                                                         VkDeviceSize      size)
{
    Gfx_Vulkan_CmdAcquireBufferFromExternal(commandBuffer,
                                            (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                            dependencyFlags,
                                            pNext,
                                            (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                                            dstQueueFamilyIndex,
                                            buffer,
                                            offset,
                                            size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdAcquireComputeWriteBufferFromHostRead(VkCommandBuffer commandBuffer, VkDependencyFlags dependencyFlags, const void* pNext, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
{
    Gfx_Vulkan_CmdAcquireComputeWriteBuffer(commandBuffer, (VkPipelineStageFlags)VK_PIPELINE_STAGE_HOST_BIT, dependencyFlags, pNext, (VkAccessFlags)VK_ACCESS_HOST_READ_BIT, buffer, offset, size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdAcquireComputeWriteBufferFromTransferRead(VkCommandBuffer commandBuffer, VkDependencyFlags dependencyFlags, const void* pNext, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
{
    Gfx_Vulkan_CmdAcquireComputeWriteBuffer(commandBuffer, (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT, dependencyFlags, pNext, (VkAccessFlags)VK_ACCESS_TRANSFER_READ_BIT, buffer, offset, size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdReleaseComputeBufferQueueOwnership(VkCommandBuffer      commandBuffer,
                                                      VkPipelineStageFlags dstStageMask,
                                                      VkDependencyFlags    dependencyFlags,
                                                      const void*          pNext,
                                                      VkAccessFlags        dstAccessMask,
                                                      uint32_t             srcQueueFamilyIndex,
                                                      uint32_t             dstQueueFamilyIndex,
                                                      VkBuffer             buffer,
                                                      VkDeviceSize         offset,
                                                      VkDeviceSize         size)
{
    Gfx_Vulkan_CmdBufferQueueOwnershipBarrier(commandBuffer,
                                              (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                              dstStageMask,
                                              dependencyFlags,
                                              pNext,
                                              (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                                              dstAccessMask,
                                              srcQueueFamilyIndex,
                                              dstQueueFamilyIndex,
                                              buffer,
                                              offset,
                                              size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdReleaseComputeBuffer(VkCommandBuffer      commandBuffer,
                                        VkPipelineStageFlags dstStageMask,
                                        VkDependencyFlags    dependencyFlags,
                                        const void*          pNext,
                                        VkAccessFlags        dstAccessMask,
                                        VkBuffer             buffer,
                                        VkDeviceSize         offset,
                                        VkDeviceSize         size)
{
    Gfx_Vulkan_CmdBufferBarrier(commandBuffer,
                                (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                dstStageMask,
                                dependencyFlags,
                                pNext,
                                (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                                dstAccessMask,
                                buffer,
                                offset,
                                size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdReleaseComputeBufferToExternal(VkCommandBuffer   commandBuffer,
                                                  VkDependencyFlags dependencyFlags,
                                                  const void*       pNext,
                                                  uint32_t          srcQueueFamilyIndex,
                                                  VkBuffer          buffer,
                                                  VkDeviceSize      offset,
                                                  VkDeviceSize      size)
{
    Gfx_Vulkan_CmdReleaseBufferToExternal(commandBuffer,
                                          (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                          dependencyFlags,
                                          pNext,
                                          (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                                          srcQueueFamilyIndex,
                                          buffer,
                                          offset,
                                          size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdReleaseComputeBufferToHostRead(VkCommandBuffer commandBuffer, VkDependencyFlags dependencyFlags, const void* pNext, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size)
{
    Gfx_Vulkan_CmdReleaseComputeBuffer(commandBuffer, (VkPipelineStageFlags)VK_PIPELINE_STAGE_HOST_BIT, dependencyFlags, pNext, (VkAccessFlags)VK_ACCESS_HOST_READ_BIT, buffer, offset, size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetBufferImageCopyRegions(VkDeviceSize             bufferOffset,
                                              VkDeviceSize             bufferRowAlignment,
                                              VkFormat                 imageFormat,
                                              VkOffset3D               imageOffset,
                                              VkExtent3D               imageExtent,
                                              VkImageSubresourceLayers imageSubresource,
                                              VkBufferImageCopy*       pRegions,
                                              VkDeviceSize*            pTransferByteCount,
                                              VkImageAspectFlags*      pAspectMask)
{
    struct VKU_FORMAT_MULTIPLANE_COMPATIBILITY compatibility;
    VkBufferImageCopy                          region;
    VkDeviceSize                               plane_offsets[VKU_FORMAT_MAX_PLANES];
    uint32_t                                   plane_strides[VKU_FORMAT_MAX_PLANES];
    VkExtent3D                                 plane_extents[VKU_FORMAT_MAX_PLANES];
    VkDeviceSize                               transfer_byte_count;
    VkImageAspectFlags                         aspect_mask;
    VkFormat                                   plane_format;
    VkExtent3D                                 plane_block_extent;
    VkDeviceSize                               tight_row_byte_count;
    uint32_t                                   plane_texel_size;
    uint32_t                                   plane_count;
    uint32_t                                   plane_index;

    LOGASSERT(bufferRowAlignment);
    LOGASSERT(0 <= imageOffset.x);
    LOGASSERT(0 <= imageOffset.y);
    LOGASSERT(0 <= imageOffset.z);
    LOGASSERT(imageExtent.width);
    LOGASSERT(imageExtent.height);
    LOGASSERT(imageExtent.depth);
    LOGASSERT(imageSubresource.aspectMask);
    LOGASSERT(imageSubresource.layerCount);
    LOGASSERT(NULL != pRegions);
    LOGASSERT(NULL != pTransferByteCount);
    LOGASSERT(NULL != pAspectMask);

    plane_count = Gfx_Vulkan_CalculateFormatPlaneLayouts(imageFormat,
                                                         imageExtent.width,
                                                         imageExtent.height,
                                                         imageExtent.depth,
                                                         imageSubresource.layerCount,
                                                         bufferRowAlignment,
                                                         plane_offsets,
                                                         plane_strides,
                                                         plane_extents,
                                                         &transfer_byte_count);
    LOGASSERT(bufferOffset <= UINT64_MAX - transfer_byte_count);

    compatibility = vkuGetFormatCompatibility(imageFormat);
    aspect_mask   = (VkImageAspectFlags)0;
    for (plane_index = 0; plane_index < plane_count; ++plane_index)
    {
        if (1u == plane_count)
        {
            plane_format         = imageFormat;
            region.imageOffset.x = imageOffset.x;
            region.imageOffset.y = imageOffset.y;
        }
        else
        {
            LOGASSERT(0 == ((uint32_t)imageOffset.x % compatibility.per_plane[plane_index].width_divisor));
            LOGASSERT(0 == ((uint32_t)imageOffset.y % compatibility.per_plane[plane_index].height_divisor));

            plane_format         = compatibility.per_plane[plane_index].compatible_format;
            region.imageOffset.x = imageOffset.x / (int32_t)compatibility.per_plane[plane_index].width_divisor;
            region.imageOffset.y = imageOffset.y / (int32_t)compatibility.per_plane[plane_index].height_divisor;
        }
        plane_texel_size = vkuFormatTexelBlockSize(plane_format);
        LOGASSERT(0 != plane_texel_size);
        LOGASSERT(0 == (plane_strides[plane_index] % plane_texel_size));

        plane_block_extent                     = vkuFormatTexelBlockExtent(plane_format);
        tight_row_byte_count                   = (VkDeviceSize)((plane_extents[plane_index].width + plane_block_extent.width - 1u) / plane_block_extent.width) * plane_texel_size;
        region.bufferOffset                    = bufferOffset + plane_offsets[plane_index];
        region.bufferRowLength                 = tight_row_byte_count == plane_strides[plane_index] ? 0 : (plane_strides[plane_index] / plane_texel_size) * plane_block_extent.width;
        region.bufferImageHeight               = 0;
        region.imageSubresource.aspectMask     = (VkImageAspectFlags)Gfx_Vulkan_GetFormatPlaneAspect(imageFormat, plane_index);
        region.imageSubresource.mipLevel       = imageSubresource.mipLevel;
        region.imageSubresource.baseArrayLayer = imageSubresource.baseArrayLayer;
        region.imageSubresource.layerCount     = imageSubresource.layerCount;
        region.imageOffset.z                   = imageOffset.z;
        region.imageExtent                     = plane_extents[plane_index];
        aspect_mask |= region.imageSubresource.aspectMask;
        pRegions[plane_index] = region;
    }

    (*pTransferByteCount) = transfer_byte_count;
    (*pAspectMask)        = aspect_mask;
    return plane_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferBufferToImageQueueOwnership(VkCommandBuffer          commandBuffer,
                                                       VkDependencyFlags        dependencyFlags,
                                                       const void*              pBufferBarrierNext,
                                                       VkPipelineStageFlags     bufferSrcStageMask,
                                                       VkAccessFlags            bufferSrcAccessMask,
                                                       uint32_t                 bufferSrcQueueFamilyIndex,
                                                       uint32_t                 bufferDstQueueFamilyIndex,
                                                       VkBuffer                 buffer,
                                                       VkDeviceSize             bufferOffset,
                                                       VkDeviceSize             bufferRowAlignment,
                                                       VkImage                  image,
                                                       VkFormat                 imageFormat,
                                                       VkImageLayout            transferImageLayout,
                                                       VkOffset3D               imageOffset,
                                                       VkExtent3D               imageExtent,
                                                       VkImageSubresourceLayers imageSubresource,
                                                       const void*              pImageAcquireBarrierNext,
                                                       VkPipelineStageFlags     imageSrcStageMask,
                                                       VkAccessFlags            imageSrcAccessMask,
                                                       VkImageLayout            initialImageLayout,
                                                       uint32_t                 imageAcquireSrcQueueFamilyIndex,
                                                       uint32_t                 imageAcquireDstQueueFamilyIndex,
                                                       const void*              pImageBarrierNext,
                                                       VkPipelineStageFlags     imageDstStageMask,
                                                       VkAccessFlags            imageDstAccessMask,
                                                       VkImageLayout            finalImageLayout,
                                                       uint32_t                 imageSrcQueueFamilyIndex,
                                                       uint32_t                 imageDstQueueFamilyIndex)
{
    VkBufferImageCopy       copy_regions[VKU_FORMAT_MAX_PLANES];
    VkImageSubresourceRange subresource_range;
    VkDeviceSize            transfer_byte_count;
    VkImageAspectFlags      aspect_mask;
    uint32_t                plane_count;

    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT((VkPipelineStageFlags)0 != bufferSrcStageMask);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT((VkPipelineStageFlags)0 != imageSrcStageMask);
    LOGASSERT((VkPipelineStageFlags)0 != imageDstStageMask);

    plane_count = Gfx_Vulkan_GetBufferImageCopyRegions(bufferOffset, bufferRowAlignment, imageFormat, imageOffset, imageExtent, imageSubresource, copy_regions, &transfer_byte_count, &aspect_mask);

    subresource_range.aspectMask     = aspect_mask;
    subresource_range.baseMipLevel   = imageSubresource.mipLevel;
    subresource_range.levelCount     = 1;
    subresource_range.baseArrayLayer = imageSubresource.baseArrayLayer;
    subresource_range.layerCount     = imageSubresource.layerCount;
    Gfx_Vulkan_CmdImageQueueOwnershipBarrier(commandBuffer,
                                             imageSrcStageMask,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             dependencyFlags,
                                             pImageAcquireBarrierNext,
                                             imageSrcAccessMask,
                                             (VkAccessFlags)VK_ACCESS_TRANSFER_WRITE_BIT,
                                             initialImageLayout,
                                             transferImageLayout,
                                             imageAcquireSrcQueueFamilyIndex,
                                             imageAcquireDstQueueFamilyIndex,
                                             image,
                                             subresource_range);
    Gfx_Vulkan_CmdBufferQueueOwnershipBarrier(commandBuffer,
                                              bufferSrcStageMask,
                                              (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                                              dependencyFlags,
                                              pBufferBarrierNext,
                                              bufferSrcAccessMask,
                                              (VkAccessFlags)VK_ACCESS_TRANSFER_READ_BIT,
                                              bufferSrcQueueFamilyIndex,
                                              bufferDstQueueFamilyIndex,
                                              buffer,
                                              bufferOffset,
                                              transfer_byte_count);
    Gfx_Vulkan_CmdCopyBufferToImage(commandBuffer, buffer, image, transferImageLayout, plane_count, copy_regions);
    Gfx_Vulkan_CmdImageQueueOwnershipBarrier(commandBuffer,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             imageDstStageMask,
                                             dependencyFlags,
                                             pImageBarrierNext,
                                             (VkAccessFlags)VK_ACCESS_TRANSFER_WRITE_BIT,
                                             imageDstAccessMask,
                                             transferImageLayout,
                                             finalImageLayout,
                                             imageSrcQueueFamilyIndex,
                                             imageDstQueueFamilyIndex,
                                             image,
                                             subresource_range);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferBufferToImageQueueOwnership2D(VkCommandBuffer          commandBuffer,
                                                         VkDependencyFlags        dependencyFlags,
                                                         const void*              pBufferBarrierNext,
                                                         VkPipelineStageFlags     bufferSrcStageMask,
                                                         VkAccessFlags            bufferSrcAccessMask,
                                                         uint32_t                 bufferSrcQueueFamilyIndex,
                                                         uint32_t                 bufferDstQueueFamilyIndex,
                                                         VkBuffer                 buffer,
                                                         VkDeviceSize             bufferOffset,
                                                         VkDeviceSize             bufferRowAlignment,
                                                         VkImage                  image,
                                                         VkFormat                 imageFormat,
                                                         VkImageLayout            transferImageLayout,
                                                         VkOffset2D               imageOffset,
                                                         VkExtent2D               imageExtent,
                                                         VkImageSubresourceLayers imageSubresource,
                                                         const void*              pImageAcquireBarrierNext,
                                                         VkPipelineStageFlags     imageSrcStageMask,
                                                         VkAccessFlags            imageSrcAccessMask,
                                                         VkImageLayout            initialImageLayout,
                                                         uint32_t                 imageAcquireSrcQueueFamilyIndex,
                                                         uint32_t                 imageAcquireDstQueueFamilyIndex,
                                                         const void*              pImageBarrierNext,
                                                         VkPipelineStageFlags     imageDstStageMask,
                                                         VkAccessFlags            imageDstAccessMask,
                                                         VkImageLayout            finalImageLayout,
                                                         uint32_t                 imageSrcQueueFamilyIndex,
                                                         uint32_t                 imageDstQueueFamilyIndex)
{
    VkOffset3D image_offset;
    VkExtent3D image_extent;

    image_offset.x      = imageOffset.x;
    image_offset.y      = imageOffset.y;
    image_offset.z      = 0;
    image_extent.width  = imageExtent.width;
    image_extent.height = imageExtent.height;
    image_extent.depth  = 1;
    Gfx_Vulkan_CmdTransferBufferToImageQueueOwnership(commandBuffer,
                                                      dependencyFlags,
                                                      pBufferBarrierNext,
                                                      bufferSrcStageMask,
                                                      bufferSrcAccessMask,
                                                      bufferSrcQueueFamilyIndex,
                                                      bufferDstQueueFamilyIndex,
                                                      buffer,
                                                      bufferOffset,
                                                      bufferRowAlignment,
                                                      image,
                                                      imageFormat,
                                                      transferImageLayout,
                                                      image_offset,
                                                      image_extent,
                                                      imageSubresource,
                                                      pImageAcquireBarrierNext,
                                                      imageSrcStageMask,
                                                      imageSrcAccessMask,
                                                      initialImageLayout,
                                                      imageAcquireSrcQueueFamilyIndex,
                                                      imageAcquireDstQueueFamilyIndex,
                                                      pImageBarrierNext,
                                                      imageDstStageMask,
                                                      imageDstAccessMask,
                                                      finalImageLayout,
                                                      imageSrcQueueFamilyIndex,
                                                      imageDstQueueFamilyIndex);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferBufferToImage(VkCommandBuffer          commandBuffer,
                                         VkDependencyFlags        dependencyFlags,
                                         const void*              pBufferBarrierNext,
                                         VkPipelineStageFlags     bufferSrcStageMask,
                                         VkAccessFlags            bufferSrcAccessMask,
                                         VkBuffer                 buffer,
                                         VkDeviceSize             bufferOffset,
                                         VkDeviceSize             bufferRowAlignment,
                                         VkImage                  image,
                                         VkFormat                 imageFormat,
                                         VkImageLayout            transferImageLayout,
                                         VkOffset3D               imageOffset,
                                         VkExtent3D               imageExtent,
                                         VkImageSubresourceLayers imageSubresource,
                                         const void*              pImageAcquireBarrierNext,
                                         VkPipelineStageFlags     imageSrcStageMask,
                                         VkAccessFlags            imageSrcAccessMask,
                                         VkImageLayout            initialImageLayout,
                                         const void*              pImageBarrierNext,
                                         VkPipelineStageFlags     imageDstStageMask,
                                         VkAccessFlags            imageDstAccessMask,
                                         VkImageLayout            finalImageLayout)
{
    Gfx_Vulkan_CmdTransferBufferToImageQueueOwnership(commandBuffer,
                                                      dependencyFlags,
                                                      pBufferBarrierNext,
                                                      bufferSrcStageMask,
                                                      bufferSrcAccessMask,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      buffer,
                                                      bufferOffset,
                                                      bufferRowAlignment,
                                                      image,
                                                      imageFormat,
                                                      transferImageLayout,
                                                      imageOffset,
                                                      imageExtent,
                                                      imageSubresource,
                                                      pImageAcquireBarrierNext,
                                                      imageSrcStageMask,
                                                      imageSrcAccessMask,
                                                      initialImageLayout,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      pImageBarrierNext,
                                                      imageDstStageMask,
                                                      imageDstAccessMask,
                                                      finalImageLayout,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      VK_QUEUE_FAMILY_IGNORED);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferBufferToImage2D(VkCommandBuffer          commandBuffer,
                                           VkDependencyFlags        dependencyFlags,
                                           const void*              pBufferBarrierNext,
                                           VkPipelineStageFlags     bufferSrcStageMask,
                                           VkAccessFlags            bufferSrcAccessMask,
                                           VkBuffer                 buffer,
                                           VkDeviceSize             bufferOffset,
                                           VkDeviceSize             bufferRowAlignment,
                                           VkImage                  image,
                                           VkFormat                 imageFormat,
                                           VkImageLayout            transferImageLayout,
                                           VkOffset2D               imageOffset,
                                           VkExtent2D               imageExtent,
                                           VkImageSubresourceLayers imageSubresource,
                                           const void*              pImageAcquireBarrierNext,
                                           VkPipelineStageFlags     imageSrcStageMask,
                                           VkAccessFlags            imageSrcAccessMask,
                                           VkImageLayout            initialImageLayout,
                                           const void*              pImageBarrierNext,
                                           VkPipelineStageFlags     imageDstStageMask,
                                           VkAccessFlags            imageDstAccessMask,
                                           VkImageLayout            finalImageLayout)
{
    VkOffset3D image_offset;
    VkExtent3D image_extent;

    image_offset.x      = imageOffset.x;
    image_offset.y      = imageOffset.y;
    image_offset.z      = 0;
    image_extent.width  = imageExtent.width;
    image_extent.height = imageExtent.height;
    image_extent.depth  = 1;
    Gfx_Vulkan_CmdTransferBufferToImage(commandBuffer,
                                        dependencyFlags,
                                        pBufferBarrierNext,
                                        bufferSrcStageMask,
                                        bufferSrcAccessMask,
                                        buffer,
                                        bufferOffset,
                                        bufferRowAlignment,
                                        image,
                                        imageFormat,
                                        transferImageLayout,
                                        image_offset,
                                        image_extent,
                                        imageSubresource,
                                        pImageAcquireBarrierNext,
                                        imageSrcStageMask,
                                        imageSrcAccessMask,
                                        initialImageLayout,
                                        pImageBarrierNext,
                                        imageDstStageMask,
                                        imageDstAccessMask,
                                        finalImageLayout);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferBufferToExternalImage(VkCommandBuffer          commandBuffer,
                                                 VkDependencyFlags        dependencyFlags,
                                                 const void*              pBufferBarrierNext,
                                                 VkPipelineStageFlags     bufferSrcStageMask,
                                                 VkAccessFlags            bufferSrcAccessMask,
                                                 VkBuffer                 buffer,
                                                 VkDeviceSize             bufferOffset,
                                                 VkDeviceSize             bufferRowAlignment,
                                                 VkImage                  image,
                                                 VkFormat                 imageFormat,
                                                 VkImageLayout            transferImageLayout,
                                                 VkOffset3D               imageOffset,
                                                 VkExtent3D               imageExtent,
                                                 VkImageSubresourceLayers imageSubresource,
                                                 const void*              pImageAcquireBarrierNext,
                                                 VkImageLayout            initialImageLayout,
                                                 const void*              pImageReleaseBarrierNext,
                                                 VkImageLayout            finalImageLayout,
                                                 uint32_t                 queueFamilyIndex)
{
    Gfx_Vulkan_CmdTransferBufferToImageQueueOwnership(commandBuffer,
                                                      dependencyFlags,
                                                      pBufferBarrierNext,
                                                      bufferSrcStageMask,
                                                      bufferSrcAccessMask,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      buffer,
                                                      bufferOffset,
                                                      bufferRowAlignment,
                                                      image,
                                                      imageFormat,
                                                      transferImageLayout,
                                                      imageOffset,
                                                      imageExtent,
                                                      imageSubresource,
                                                      pImageAcquireBarrierNext,
                                                      (VkPipelineStageFlags)VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                                      (VkAccessFlags)0,
                                                      initialImageLayout,
                                                      VK_QUEUE_FAMILY_EXTERNAL,
                                                      queueFamilyIndex,
                                                      pImageReleaseBarrierNext,
                                                      (VkPipelineStageFlags)VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                                      (VkAccessFlags)0,
                                                      finalImageLayout,
                                                      queueFamilyIndex,
                                                      VK_QUEUE_FAMILY_EXTERNAL);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferComputeBufferToImageQueueOwnership(VkCommandBuffer          commandBuffer,
                                                              VkDependencyFlags        dependencyFlags,
                                                              const void*              pBufferBarrierNext,
                                                              uint32_t                 bufferSrcQueueFamilyIndex,
                                                              uint32_t                 bufferDstQueueFamilyIndex,
                                                              VkBuffer                 buffer,
                                                              VkDeviceSize             bufferOffset,
                                                              VkDeviceSize             bufferRowAlignment,
                                                              VkImage                  image,
                                                              VkFormat                 imageFormat,
                                                              VkImageLayout            transferImageLayout,
                                                              VkOffset3D               imageOffset,
                                                              VkExtent3D               imageExtent,
                                                              VkImageSubresourceLayers imageSubresource,
                                                              const void*              pImageAcquireBarrierNext,
                                                              VkPipelineStageFlags     imageSrcStageMask,
                                                              VkAccessFlags            imageSrcAccessMask,
                                                              VkImageLayout            initialImageLayout,
                                                              uint32_t                 imageAcquireSrcQueueFamilyIndex,
                                                              uint32_t                 imageAcquireDstQueueFamilyIndex,
                                                              const void*              pImageBarrierNext,
                                                              VkPipelineStageFlags     imageDstStageMask,
                                                              VkAccessFlags            imageDstAccessMask,
                                                              VkImageLayout            finalImageLayout,
                                                              uint32_t                 imageSrcQueueFamilyIndex,
                                                              uint32_t                 imageDstQueueFamilyIndex)
{
    Gfx_Vulkan_CmdTransferBufferToImageQueueOwnership(commandBuffer,
                                                      dependencyFlags,
                                                      pBufferBarrierNext,
                                                      (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                                      (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                                                      bufferSrcQueueFamilyIndex,
                                                      bufferDstQueueFamilyIndex,
                                                      buffer,
                                                      bufferOffset,
                                                      bufferRowAlignment,
                                                      image,
                                                      imageFormat,
                                                      transferImageLayout,
                                                      imageOffset,
                                                      imageExtent,
                                                      imageSubresource,
                                                      pImageAcquireBarrierNext,
                                                      imageSrcStageMask,
                                                      imageSrcAccessMask,
                                                      initialImageLayout,
                                                      imageAcquireSrcQueueFamilyIndex,
                                                      imageAcquireDstQueueFamilyIndex,
                                                      pImageBarrierNext,
                                                      imageDstStageMask,
                                                      imageDstAccessMask,
                                                      finalImageLayout,
                                                      imageSrcQueueFamilyIndex,
                                                      imageDstQueueFamilyIndex);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferComputeBufferToImageQueueOwnership2D(VkCommandBuffer          commandBuffer,
                                                                VkDependencyFlags        dependencyFlags,
                                                                const void*              pBufferBarrierNext,
                                                                uint32_t                 bufferSrcQueueFamilyIndex,
                                                                uint32_t                 bufferDstQueueFamilyIndex,
                                                                VkBuffer                 buffer,
                                                                VkDeviceSize             bufferOffset,
                                                                VkDeviceSize             bufferRowAlignment,
                                                                VkImage                  image,
                                                                VkFormat                 imageFormat,
                                                                VkImageLayout            transferImageLayout,
                                                                VkOffset2D               imageOffset,
                                                                VkExtent2D               imageExtent,
                                                                VkImageSubresourceLayers imageSubresource,
                                                                const void*              pImageAcquireBarrierNext,
                                                                VkPipelineStageFlags     imageSrcStageMask,
                                                                VkAccessFlags            imageSrcAccessMask,
                                                                VkImageLayout            initialImageLayout,
                                                                uint32_t                 imageAcquireSrcQueueFamilyIndex,
                                                                uint32_t                 imageAcquireDstQueueFamilyIndex,
                                                                const void*              pImageBarrierNext,
                                                                VkPipelineStageFlags     imageDstStageMask,
                                                                VkAccessFlags            imageDstAccessMask,
                                                                VkImageLayout            finalImageLayout,
                                                                uint32_t                 imageSrcQueueFamilyIndex,
                                                                uint32_t                 imageDstQueueFamilyIndex)
{
    Gfx_Vulkan_CmdTransferBufferToImageQueueOwnership2D(commandBuffer,
                                                        dependencyFlags,
                                                        pBufferBarrierNext,
                                                        (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                                        (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                                                        bufferSrcQueueFamilyIndex,
                                                        bufferDstQueueFamilyIndex,
                                                        buffer,
                                                        bufferOffset,
                                                        bufferRowAlignment,
                                                        image,
                                                        imageFormat,
                                                        transferImageLayout,
                                                        imageOffset,
                                                        imageExtent,
                                                        imageSubresource,
                                                        pImageAcquireBarrierNext,
                                                        imageSrcStageMask,
                                                        imageSrcAccessMask,
                                                        initialImageLayout,
                                                        imageAcquireSrcQueueFamilyIndex,
                                                        imageAcquireDstQueueFamilyIndex,
                                                        pImageBarrierNext,
                                                        imageDstStageMask,
                                                        imageDstAccessMask,
                                                        finalImageLayout,
                                                        imageSrcQueueFamilyIndex,
                                                        imageDstQueueFamilyIndex);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferComputeBufferToExternalImage(VkCommandBuffer          commandBuffer,
                                                        VkDependencyFlags        dependencyFlags,
                                                        const void*              pBufferBarrierNext,
                                                        VkBuffer                 buffer,
                                                        VkDeviceSize             bufferOffset,
                                                        VkDeviceSize             bufferRowAlignment,
                                                        VkImage                  image,
                                                        VkFormat                 imageFormat,
                                                        VkImageLayout            transferImageLayout,
                                                        VkOffset3D               imageOffset,
                                                        VkExtent3D               imageExtent,
                                                        VkImageSubresourceLayers imageSubresource,
                                                        const void*              pImageAcquireBarrierNext,
                                                        VkImageLayout            initialImageLayout,
                                                        const void*              pImageReleaseBarrierNext,
                                                        VkImageLayout            finalImageLayout,
                                                        uint32_t                 queueFamilyIndex)
{
    Gfx_Vulkan_CmdTransferBufferToExternalImage(commandBuffer,
                                                dependencyFlags,
                                                pBufferBarrierNext,
                                                (VkPipelineStageFlags)VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                                                (VkAccessFlags)VK_ACCESS_SHADER_WRITE_BIT,
                                                buffer,
                                                bufferOffset,
                                                bufferRowAlignment,
                                                image,
                                                imageFormat,
                                                transferImageLayout,
                                                imageOffset,
                                                imageExtent,
                                                imageSubresource,
                                                pImageAcquireBarrierNext,
                                                initialImageLayout,
                                                pImageReleaseBarrierNext,
                                                finalImageLayout,
                                                queueFamilyIndex);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferComputeBufferToExternalImage2D(VkCommandBuffer          commandBuffer,
                                                          VkDependencyFlags        dependencyFlags,
                                                          const void*              pBufferBarrierNext,
                                                          VkBuffer                 buffer,
                                                          VkDeviceSize             bufferOffset,
                                                          VkDeviceSize             bufferRowAlignment,
                                                          VkImage                  image,
                                                          VkFormat                 imageFormat,
                                                          VkImageLayout            transferImageLayout,
                                                          VkOffset2D               imageOffset,
                                                          VkExtent2D               imageExtent,
                                                          VkImageSubresourceLayers imageSubresource,
                                                          const void*              pImageAcquireBarrierNext,
                                                          VkImageLayout            initialImageLayout,
                                                          const void*              pImageReleaseBarrierNext,
                                                          VkImageLayout            finalImageLayout,
                                                          uint32_t                 queueFamilyIndex)
{
    VkOffset3D image_offset;
    VkExtent3D image_extent;

    image_offset.x      = imageOffset.x;
    image_offset.y      = imageOffset.y;
    image_offset.z      = 0;
    image_extent.width  = imageExtent.width;
    image_extent.height = imageExtent.height;
    image_extent.depth  = 1;
    Gfx_Vulkan_CmdTransferComputeBufferToExternalImage(commandBuffer,
                                                       dependencyFlags,
                                                       pBufferBarrierNext,
                                                       buffer,
                                                       bufferOffset,
                                                       bufferRowAlignment,
                                                       image,
                                                       imageFormat,
                                                       transferImageLayout,
                                                       image_offset,
                                                       image_extent,
                                                       imageSubresource,
                                                       pImageAcquireBarrierNext,
                                                       initialImageLayout,
                                                       pImageReleaseBarrierNext,
                                                       finalImageLayout,
                                                       queueFamilyIndex);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferImageToBufferQueueOwnership(VkCommandBuffer          commandBuffer,
                                                       VkDependencyFlags        dependencyFlags,
                                                       const void*              pImageAcquireBarrierNext,
                                                       VkPipelineStageFlags     imageSrcStageMask,
                                                       VkAccessFlags            imageSrcAccessMask,
                                                       VkImageLayout            initialImageLayout,
                                                       uint32_t                 imageAcquireSrcQueueFamilyIndex,
                                                       uint32_t                 imageAcquireDstQueueFamilyIndex,
                                                       VkImage                  image,
                                                       VkFormat                 imageFormat,
                                                       VkImageLayout            transferImageLayout,
                                                       VkOffset3D               imageOffset,
                                                       VkExtent3D               imageExtent,
                                                       VkImageSubresourceLayers imageSubresource,
                                                       const void*              pBufferAcquireBarrierNext,
                                                       VkPipelineStageFlags     bufferSrcStageMask,
                                                       VkAccessFlags            bufferSrcAccessMask,
                                                       uint32_t                 bufferAcquireSrcQueueFamilyIndex,
                                                       uint32_t                 bufferAcquireDstQueueFamilyIndex,
                                                       VkBuffer                 buffer,
                                                       VkDeviceSize             bufferOffset,
                                                       VkDeviceSize             bufferRowAlignment,
                                                       const void*              pImageBarrierNext,
                                                       VkPipelineStageFlags     imageDstStageMask,
                                                       VkAccessFlags            imageDstAccessMask,
                                                       VkImageLayout            finalImageLayout,
                                                       uint32_t                 imageSrcQueueFamilyIndex,
                                                       uint32_t                 imageDstQueueFamilyIndex,
                                                       const void*              pBufferBarrierNext,
                                                       VkPipelineStageFlags     bufferDstStageMask,
                                                       VkAccessFlags            bufferDstAccessMask,
                                                       uint32_t                 bufferSrcQueueFamilyIndex,
                                                       uint32_t                 bufferDstQueueFamilyIndex)
{
    VkBufferImageCopy       copy_regions[VKU_FORMAT_MAX_PLANES];
    VkImageSubresourceRange subresource_range;
    VkDeviceSize            transfer_byte_count;
    VkImageAspectFlags      aspect_mask;
    uint32_t                plane_count;

    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT((VkPipelineStageFlags)0 != imageSrcStageMask);
    LOGASSERT(VK_NULL_HANDLE != image);
    LOGASSERT((VkPipelineStageFlags)0 != bufferSrcStageMask);
    LOGASSERT(VK_NULL_HANDLE != buffer);
    LOGASSERT((VkPipelineStageFlags)0 != imageDstStageMask);
    LOGASSERT((VkPipelineStageFlags)0 != bufferDstStageMask);

    plane_count = Gfx_Vulkan_GetBufferImageCopyRegions(bufferOffset, bufferRowAlignment, imageFormat, imageOffset, imageExtent, imageSubresource, copy_regions, &transfer_byte_count, &aspect_mask);

    subresource_range.aspectMask     = aspect_mask;
    subresource_range.baseMipLevel   = imageSubresource.mipLevel;
    subresource_range.levelCount     = 1;
    subresource_range.baseArrayLayer = imageSubresource.baseArrayLayer;
    subresource_range.layerCount     = imageSubresource.layerCount;
    Gfx_Vulkan_CmdImageQueueOwnershipBarrier(commandBuffer,
                                             imageSrcStageMask,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             dependencyFlags,
                                             pImageAcquireBarrierNext,
                                             imageSrcAccessMask,
                                             (VkAccessFlags)VK_ACCESS_TRANSFER_READ_BIT,
                                             initialImageLayout,
                                             transferImageLayout,
                                             imageAcquireSrcQueueFamilyIndex,
                                             imageAcquireDstQueueFamilyIndex,
                                             image,
                                             subresource_range);
    Gfx_Vulkan_CmdBufferQueueOwnershipBarrier(commandBuffer,
                                              bufferSrcStageMask,
                                              (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                                              dependencyFlags,
                                              pBufferAcquireBarrierNext,
                                              bufferSrcAccessMask,
                                              (VkAccessFlags)VK_ACCESS_TRANSFER_WRITE_BIT,
                                              bufferAcquireSrcQueueFamilyIndex,
                                              bufferAcquireDstQueueFamilyIndex,
                                              buffer,
                                              bufferOffset,
                                              transfer_byte_count);
    Gfx_Vulkan_CmdCopyImageToBuffer(commandBuffer, image, transferImageLayout, buffer, plane_count, copy_regions);
    Gfx_Vulkan_CmdImageQueueOwnershipBarrier(commandBuffer,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             imageDstStageMask,
                                             dependencyFlags,
                                             pImageBarrierNext,
                                             (VkAccessFlags)VK_ACCESS_TRANSFER_READ_BIT,
                                             imageDstAccessMask,
                                             transferImageLayout,
                                             finalImageLayout,
                                             imageSrcQueueFamilyIndex,
                                             imageDstQueueFamilyIndex,
                                             image,
                                             subresource_range);
    Gfx_Vulkan_CmdBufferQueueOwnershipBarrier(commandBuffer,
                                              (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                                              bufferDstStageMask,
                                              dependencyFlags,
                                              pBufferBarrierNext,
                                              (VkAccessFlags)VK_ACCESS_TRANSFER_WRITE_BIT,
                                              bufferDstAccessMask,
                                              bufferSrcQueueFamilyIndex,
                                              bufferDstQueueFamilyIndex,
                                              buffer,
                                              bufferOffset,
                                              transfer_byte_count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferImageToBufferQueueOwnership2D(VkCommandBuffer          commandBuffer,
                                                         VkDependencyFlags        dependencyFlags,
                                                         const void*              pImageAcquireBarrierNext,
                                                         VkPipelineStageFlags     imageSrcStageMask,
                                                         VkAccessFlags            imageSrcAccessMask,
                                                         VkImageLayout            initialImageLayout,
                                                         uint32_t                 imageAcquireSrcQueueFamilyIndex,
                                                         uint32_t                 imageAcquireDstQueueFamilyIndex,
                                                         VkImage                  image,
                                                         VkFormat                 imageFormat,
                                                         VkImageLayout            transferImageLayout,
                                                         VkOffset2D               imageOffset,
                                                         VkExtent2D               imageExtent,
                                                         VkImageSubresourceLayers imageSubresource,
                                                         const void*              pBufferAcquireBarrierNext,
                                                         VkPipelineStageFlags     bufferSrcStageMask,
                                                         VkAccessFlags            bufferSrcAccessMask,
                                                         uint32_t                 bufferAcquireSrcQueueFamilyIndex,
                                                         uint32_t                 bufferAcquireDstQueueFamilyIndex,
                                                         VkBuffer                 buffer,
                                                         VkDeviceSize             bufferOffset,
                                                         VkDeviceSize             bufferRowAlignment,
                                                         const void*              pImageBarrierNext,
                                                         VkPipelineStageFlags     imageDstStageMask,
                                                         VkAccessFlags            imageDstAccessMask,
                                                         VkImageLayout            finalImageLayout,
                                                         uint32_t                 imageSrcQueueFamilyIndex,
                                                         uint32_t                 imageDstQueueFamilyIndex,
                                                         const void*              pBufferBarrierNext,
                                                         VkPipelineStageFlags     bufferDstStageMask,
                                                         VkAccessFlags            bufferDstAccessMask,
                                                         uint32_t                 bufferSrcQueueFamilyIndex,
                                                         uint32_t                 bufferDstQueueFamilyIndex)
{
    VkOffset3D image_offset;
    VkExtent3D image_extent;

    image_offset.x      = imageOffset.x;
    image_offset.y      = imageOffset.y;
    image_offset.z      = 0;
    image_extent.width  = imageExtent.width;
    image_extent.height = imageExtent.height;
    image_extent.depth  = 1;
    Gfx_Vulkan_CmdTransferImageToBufferQueueOwnership(commandBuffer,
                                                      dependencyFlags,
                                                      pImageAcquireBarrierNext,
                                                      imageSrcStageMask,
                                                      imageSrcAccessMask,
                                                      initialImageLayout,
                                                      imageAcquireSrcQueueFamilyIndex,
                                                      imageAcquireDstQueueFamilyIndex,
                                                      image,
                                                      imageFormat,
                                                      transferImageLayout,
                                                      image_offset,
                                                      image_extent,
                                                      imageSubresource,
                                                      pBufferAcquireBarrierNext,
                                                      bufferSrcStageMask,
                                                      bufferSrcAccessMask,
                                                      bufferAcquireSrcQueueFamilyIndex,
                                                      bufferAcquireDstQueueFamilyIndex,
                                                      buffer,
                                                      bufferOffset,
                                                      bufferRowAlignment,
                                                      pImageBarrierNext,
                                                      imageDstStageMask,
                                                      imageDstAccessMask,
                                                      finalImageLayout,
                                                      imageSrcQueueFamilyIndex,
                                                      imageDstQueueFamilyIndex,
                                                      pBufferBarrierNext,
                                                      bufferDstStageMask,
                                                      bufferDstAccessMask,
                                                      bufferSrcQueueFamilyIndex,
                                                      bufferDstQueueFamilyIndex);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferImageToBuffer(VkCommandBuffer          commandBuffer,
                                         VkDependencyFlags        dependencyFlags,
                                         const void*              pImageAcquireBarrierNext,
                                         VkPipelineStageFlags     imageSrcStageMask,
                                         VkAccessFlags            imageSrcAccessMask,
                                         VkImageLayout            initialImageLayout,
                                         VkImage                  image,
                                         VkFormat                 imageFormat,
                                         VkImageLayout            transferImageLayout,
                                         VkOffset3D               imageOffset,
                                         VkExtent3D               imageExtent,
                                         VkImageSubresourceLayers imageSubresource,
                                         const void*              pBufferAcquireBarrierNext,
                                         VkPipelineStageFlags     bufferSrcStageMask,
                                         VkAccessFlags            bufferSrcAccessMask,
                                         VkBuffer                 buffer,
                                         VkDeviceSize             bufferOffset,
                                         VkDeviceSize             bufferRowAlignment,
                                         const void*              pImageBarrierNext,
                                         VkPipelineStageFlags     imageDstStageMask,
                                         VkAccessFlags            imageDstAccessMask,
                                         VkImageLayout            finalImageLayout,
                                         const void*              pBufferBarrierNext,
                                         VkPipelineStageFlags     bufferDstStageMask,
                                         VkAccessFlags            bufferDstAccessMask)
{
    Gfx_Vulkan_CmdTransferImageToBufferQueueOwnership(commandBuffer,
                                                      dependencyFlags,
                                                      pImageAcquireBarrierNext,
                                                      imageSrcStageMask,
                                                      imageSrcAccessMask,
                                                      initialImageLayout,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      image,
                                                      imageFormat,
                                                      transferImageLayout,
                                                      imageOffset,
                                                      imageExtent,
                                                      imageSubresource,
                                                      pBufferAcquireBarrierNext,
                                                      bufferSrcStageMask,
                                                      bufferSrcAccessMask,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      buffer,
                                                      bufferOffset,
                                                      bufferRowAlignment,
                                                      pImageBarrierNext,
                                                      imageDstStageMask,
                                                      imageDstAccessMask,
                                                      finalImageLayout,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      pBufferBarrierNext,
                                                      bufferDstStageMask,
                                                      bufferDstAccessMask,
                                                      VK_QUEUE_FAMILY_IGNORED,
                                                      VK_QUEUE_FAMILY_IGNORED);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferImageToBuffer2D(VkCommandBuffer          commandBuffer,
                                           VkDependencyFlags        dependencyFlags,
                                           const void*              pImageAcquireBarrierNext,
                                           VkPipelineStageFlags     imageSrcStageMask,
                                           VkAccessFlags            imageSrcAccessMask,
                                           VkImageLayout            initialImageLayout,
                                           VkImage                  image,
                                           VkFormat                 imageFormat,
                                           VkImageLayout            transferImageLayout,
                                           VkOffset2D               imageOffset,
                                           VkExtent2D               imageExtent,
                                           VkImageSubresourceLayers imageSubresource,
                                           const void*              pBufferAcquireBarrierNext,
                                           VkPipelineStageFlags     bufferSrcStageMask,
                                           VkAccessFlags            bufferSrcAccessMask,
                                           VkBuffer                 buffer,
                                           VkDeviceSize             bufferOffset,
                                           VkDeviceSize             bufferRowAlignment,
                                           const void*              pImageBarrierNext,
                                           VkPipelineStageFlags     imageDstStageMask,
                                           VkAccessFlags            imageDstAccessMask,
                                           VkImageLayout            finalImageLayout,
                                           const void*              pBufferBarrierNext,
                                           VkPipelineStageFlags     bufferDstStageMask,
                                           VkAccessFlags            bufferDstAccessMask)
{
    VkOffset3D image_offset;
    VkExtent3D image_extent;

    image_offset.x      = imageOffset.x;
    image_offset.y      = imageOffset.y;
    image_offset.z      = 0;
    image_extent.width  = imageExtent.width;
    image_extent.height = imageExtent.height;
    image_extent.depth  = 1;
    Gfx_Vulkan_CmdTransferImageToBuffer(commandBuffer,
                                        dependencyFlags,
                                        pImageAcquireBarrierNext,
                                        imageSrcStageMask,
                                        imageSrcAccessMask,
                                        initialImageLayout,
                                        image,
                                        imageFormat,
                                        transferImageLayout,
                                        image_offset,
                                        image_extent,
                                        imageSubresource,
                                        pBufferAcquireBarrierNext,
                                        bufferSrcStageMask,
                                        bufferSrcAccessMask,
                                        buffer,
                                        bufferOffset,
                                        bufferRowAlignment,
                                        pImageBarrierNext,
                                        imageDstStageMask,
                                        imageDstAccessMask,
                                        finalImageLayout,
                                        pBufferBarrierNext,
                                        bufferDstStageMask,
                                        bufferDstAccessMask);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferImageToImageQueueOwnership(VkCommandBuffer          commandBuffer,
                                                      VkDependencyFlags        dependencyFlags,
                                                      const void*              pSrcAcquireBarrierNext,
                                                      VkPipelineStageFlags     srcStageMask,
                                                      VkAccessFlags            srcAccessMask,
                                                      VkImageLayout            srcInitialLayout,
                                                      VkImageLayout            srcTransferLayout,
                                                      uint32_t                 srcAcquireQueueFamilyIndex,
                                                      uint32_t                 srcTransferQueueFamilyIndex,
                                                      VkImage                  srcImage,
                                                      VkImageSubresourceLayers srcSubresource,
                                                      VkOffset3D               srcOffset,
                                                      const void*              pDstAcquireBarrierNext,
                                                      VkPipelineStageFlags     dstStageMask,
                                                      VkAccessFlags            dstAccessMask,
                                                      VkImageLayout            dstInitialLayout,
                                                      VkImageLayout            dstTransferLayout,
                                                      uint32_t                 dstAcquireQueueFamilyIndex,
                                                      uint32_t                 dstTransferQueueFamilyIndex,
                                                      VkImage                  dstImage,
                                                      VkImageSubresourceLayers dstSubresource,
                                                      VkOffset3D               dstOffset,
                                                      VkExtent3D               extent,
                                                      const void*              pSrcBarrierNext,
                                                      VkPipelineStageFlags     srcDstStageMask,
                                                      VkAccessFlags            srcDstAccessMask,
                                                      VkImageLayout            srcFinalLayout,
                                                      uint32_t                 srcReleaseQueueFamilyIndex,
                                                      uint32_t                 srcDstQueueFamilyIndex,
                                                      const void*              pDstBarrierNext,
                                                      VkPipelineStageFlags     dstDstStageMask,
                                                      VkAccessFlags            dstDstAccessMask,
                                                      VkImageLayout            dstFinalLayout,
                                                      uint32_t                 dstReleaseQueueFamilyIndex,
                                                      uint32_t                 dstDstQueueFamilyIndex)
{
    VkImageSubresourceRange src_range;
    VkImageSubresourceRange dst_range;

    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT((VkPipelineStageFlags)0 != srcStageMask);
    LOGASSERT((VkPipelineStageFlags)0 != dstStageMask);
    LOGASSERT(VK_NULL_HANDLE != srcImage);
    LOGASSERT(VK_NULL_HANDLE != dstImage);
    LOGASSERT((VkImageAspectFlags)0 != srcSubresource.aspectMask);
    LOGASSERT((VkImageAspectFlags)0 != dstSubresource.aspectMask);
    LOGASSERT(0 != srcSubresource.layerCount);
    LOGASSERT(0 != dstSubresource.layerCount);
    LOGASSERT((VkPipelineStageFlags)0 != srcDstStageMask);
    LOGASSERT((VkPipelineStageFlags)0 != dstDstStageMask);

    src_range.aspectMask     = srcSubresource.aspectMask;
    src_range.baseMipLevel   = srcSubresource.mipLevel;
    src_range.levelCount     = 1;
    src_range.baseArrayLayer = srcSubresource.baseArrayLayer;
    src_range.layerCount     = srcSubresource.layerCount;
    dst_range.aspectMask     = dstSubresource.aspectMask;
    dst_range.baseMipLevel   = dstSubresource.mipLevel;
    dst_range.levelCount     = 1;
    dst_range.baseArrayLayer = dstSubresource.baseArrayLayer;
    dst_range.layerCount     = dstSubresource.layerCount;
    Gfx_Vulkan_CmdImageQueueOwnershipBarrier(commandBuffer,
                                             srcStageMask,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             dependencyFlags,
                                             pSrcAcquireBarrierNext,
                                             srcAccessMask,
                                             (VkAccessFlags)VK_ACCESS_TRANSFER_READ_BIT,
                                             srcInitialLayout,
                                             srcTransferLayout,
                                             srcAcquireQueueFamilyIndex,
                                             srcTransferQueueFamilyIndex,
                                             srcImage,
                                             src_range);
    Gfx_Vulkan_CmdImageQueueOwnershipBarrier(commandBuffer,
                                             dstStageMask,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             dependencyFlags,
                                             pDstAcquireBarrierNext,
                                             dstAccessMask,
                                             (VkAccessFlags)VK_ACCESS_TRANSFER_WRITE_BIT,
                                             dstInitialLayout,
                                             dstTransferLayout,
                                             dstAcquireQueueFamilyIndex,
                                             dstTransferQueueFamilyIndex,
                                             dstImage,
                                             dst_range);
    Gfx_Vulkan_CmdCopyImage(commandBuffer, srcImage, srcTransferLayout, srcSubresource, srcOffset, dstImage, dstTransferLayout, dstSubresource, dstOffset, extent);
    Gfx_Vulkan_CmdImageQueueOwnershipBarrier(commandBuffer,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             srcDstStageMask,
                                             dependencyFlags,
                                             pSrcBarrierNext,
                                             (VkAccessFlags)VK_ACCESS_TRANSFER_READ_BIT,
                                             srcDstAccessMask,
                                             srcTransferLayout,
                                             srcFinalLayout,
                                             srcReleaseQueueFamilyIndex,
                                             srcDstQueueFamilyIndex,
                                             srcImage,
                                             src_range);
    Gfx_Vulkan_CmdImageQueueOwnershipBarrier(commandBuffer,
                                             (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                                             dstDstStageMask,
                                             dependencyFlags,
                                             pDstBarrierNext,
                                             (VkAccessFlags)VK_ACCESS_TRANSFER_WRITE_BIT,
                                             dstDstAccessMask,
                                             dstTransferLayout,
                                             dstFinalLayout,
                                             dstReleaseQueueFamilyIndex,
                                             dstDstQueueFamilyIndex,
                                             dstImage,
                                             dst_range);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferImageToImageQueueOwnership2D(VkCommandBuffer          commandBuffer,
                                                        VkDependencyFlags        dependencyFlags,
                                                        const void*              pSrcAcquireBarrierNext,
                                                        VkPipelineStageFlags     srcStageMask,
                                                        VkAccessFlags            srcAccessMask,
                                                        VkImageLayout            srcInitialLayout,
                                                        VkImageLayout            srcTransferLayout,
                                                        uint32_t                 srcAcquireQueueFamilyIndex,
                                                        uint32_t                 srcTransferQueueFamilyIndex,
                                                        VkImage                  srcImage,
                                                        VkImageSubresourceLayers srcSubresource,
                                                        VkOffset2D               srcOffset,
                                                        const void*              pDstAcquireBarrierNext,
                                                        VkPipelineStageFlags     dstStageMask,
                                                        VkAccessFlags            dstAccessMask,
                                                        VkImageLayout            dstInitialLayout,
                                                        VkImageLayout            dstTransferLayout,
                                                        uint32_t                 dstAcquireQueueFamilyIndex,
                                                        uint32_t                 dstTransferQueueFamilyIndex,
                                                        VkImage                  dstImage,
                                                        VkImageSubresourceLayers dstSubresource,
                                                        VkOffset2D               dstOffset,
                                                        VkExtent2D               extent,
                                                        const void*              pSrcBarrierNext,
                                                        VkPipelineStageFlags     srcDstStageMask,
                                                        VkAccessFlags            srcDstAccessMask,
                                                        VkImageLayout            srcFinalLayout,
                                                        uint32_t                 srcReleaseQueueFamilyIndex,
                                                        uint32_t                 srcDstQueueFamilyIndex,
                                                        const void*              pDstBarrierNext,
                                                        VkPipelineStageFlags     dstDstStageMask,
                                                        VkAccessFlags            dstDstAccessMask,
                                                        VkImageLayout            dstFinalLayout,
                                                        uint32_t                 dstReleaseQueueFamilyIndex,
                                                        uint32_t                 dstDstQueueFamilyIndex)
{
    VkOffset3D src_offset;
    VkOffset3D dst_offset;
    VkExtent3D image_extent;

    src_offset.x        = srcOffset.x;
    src_offset.y        = srcOffset.y;
    src_offset.z        = 0;
    dst_offset.x        = dstOffset.x;
    dst_offset.y        = dstOffset.y;
    dst_offset.z        = 0;
    image_extent.width  = extent.width;
    image_extent.height = extent.height;
    image_extent.depth  = 1;
    Gfx_Vulkan_CmdTransferImageToImageQueueOwnership(commandBuffer,
                                                     dependencyFlags,
                                                     pSrcAcquireBarrierNext,
                                                     srcStageMask,
                                                     srcAccessMask,
                                                     srcInitialLayout,
                                                     srcTransferLayout,
                                                     srcAcquireQueueFamilyIndex,
                                                     srcTransferQueueFamilyIndex,
                                                     srcImage,
                                                     srcSubresource,
                                                     src_offset,
                                                     pDstAcquireBarrierNext,
                                                     dstStageMask,
                                                     dstAccessMask,
                                                     dstInitialLayout,
                                                     dstTransferLayout,
                                                     dstAcquireQueueFamilyIndex,
                                                     dstTransferQueueFamilyIndex,
                                                     dstImage,
                                                     dstSubresource,
                                                     dst_offset,
                                                     image_extent,
                                                     pSrcBarrierNext,
                                                     srcDstStageMask,
                                                     srcDstAccessMask,
                                                     srcFinalLayout,
                                                     srcReleaseQueueFamilyIndex,
                                                     srcDstQueueFamilyIndex,
                                                     pDstBarrierNext,
                                                     dstDstStageMask,
                                                     dstDstAccessMask,
                                                     dstFinalLayout,
                                                     dstReleaseQueueFamilyIndex,
                                                     dstDstQueueFamilyIndex);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferImageToImage(VkCommandBuffer          commandBuffer,
                                        VkDependencyFlags        dependencyFlags,
                                        const void*              pSrcAcquireBarrierNext,
                                        VkPipelineStageFlags     srcStageMask,
                                        VkAccessFlags            srcAccessMask,
                                        VkImageLayout            srcInitialLayout,
                                        VkImageLayout            srcTransferLayout,
                                        VkImage                  srcImage,
                                        VkImageSubresourceLayers srcSubresource,
                                        VkOffset3D               srcOffset,
                                        const void*              pDstAcquireBarrierNext,
                                        VkPipelineStageFlags     dstStageMask,
                                        VkAccessFlags            dstAccessMask,
                                        VkImageLayout            dstInitialLayout,
                                        VkImageLayout            dstTransferLayout,
                                        VkImage                  dstImage,
                                        VkImageSubresourceLayers dstSubresource,
                                        VkOffset3D               dstOffset,
                                        VkExtent3D               extent,
                                        const void*              pSrcBarrierNext,
                                        VkPipelineStageFlags     srcDstStageMask,
                                        VkAccessFlags            srcDstAccessMask,
                                        VkImageLayout            srcFinalLayout,
                                        const void*              pDstBarrierNext,
                                        VkPipelineStageFlags     dstDstStageMask,
                                        VkAccessFlags            dstDstAccessMask,
                                        VkImageLayout            dstFinalLayout)
{
    Gfx_Vulkan_CmdTransferImageToImageQueueOwnership(commandBuffer,
                                                     dependencyFlags,
                                                     pSrcAcquireBarrierNext,
                                                     srcStageMask,
                                                     srcAccessMask,
                                                     srcInitialLayout,
                                                     srcTransferLayout,
                                                     VK_QUEUE_FAMILY_IGNORED,
                                                     VK_QUEUE_FAMILY_IGNORED,
                                                     srcImage,
                                                     srcSubresource,
                                                     srcOffset,
                                                     pDstAcquireBarrierNext,
                                                     dstStageMask,
                                                     dstAccessMask,
                                                     dstInitialLayout,
                                                     dstTransferLayout,
                                                     VK_QUEUE_FAMILY_IGNORED,
                                                     VK_QUEUE_FAMILY_IGNORED,
                                                     dstImage,
                                                     dstSubresource,
                                                     dstOffset,
                                                     extent,
                                                     pSrcBarrierNext,
                                                     srcDstStageMask,
                                                     srcDstAccessMask,
                                                     srcFinalLayout,
                                                     VK_QUEUE_FAMILY_IGNORED,
                                                     VK_QUEUE_FAMILY_IGNORED,
                                                     pDstBarrierNext,
                                                     dstDstStageMask,
                                                     dstDstAccessMask,
                                                     dstFinalLayout,
                                                     VK_QUEUE_FAMILY_IGNORED,
                                                     VK_QUEUE_FAMILY_IGNORED);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferImageToImage2D(VkCommandBuffer          commandBuffer,
                                          VkDependencyFlags        dependencyFlags,
                                          const void*              pSrcAcquireBarrierNext,
                                          VkPipelineStageFlags     srcStageMask,
                                          VkAccessFlags            srcAccessMask,
                                          VkImageLayout            srcInitialLayout,
                                          VkImageLayout            srcTransferLayout,
                                          VkImage                  srcImage,
                                          VkImageSubresourceLayers srcSubresource,
                                          VkOffset2D               srcOffset,
                                          const void*              pDstAcquireBarrierNext,
                                          VkPipelineStageFlags     dstStageMask,
                                          VkAccessFlags            dstAccessMask,
                                          VkImageLayout            dstInitialLayout,
                                          VkImageLayout            dstTransferLayout,
                                          VkImage                  dstImage,
                                          VkImageSubresourceLayers dstSubresource,
                                          VkOffset2D               dstOffset,
                                          VkExtent2D               extent,
                                          const void*              pSrcBarrierNext,
                                          VkPipelineStageFlags     srcDstStageMask,
                                          VkAccessFlags            srcDstAccessMask,
                                          VkImageLayout            srcFinalLayout,
                                          const void*              pDstBarrierNext,
                                          VkPipelineStageFlags     dstDstStageMask,
                                          VkAccessFlags            dstDstAccessMask,
                                          VkImageLayout            dstFinalLayout)
{
    VkOffset3D src_offset;
    VkOffset3D dst_offset;
    VkExtent3D image_extent;

    src_offset.x        = srcOffset.x;
    src_offset.y        = srcOffset.y;
    src_offset.z        = 0;
    dst_offset.x        = dstOffset.x;
    dst_offset.y        = dstOffset.y;
    dst_offset.z        = 0;
    image_extent.width  = extent.width;
    image_extent.height = extent.height;
    image_extent.depth  = 1;
    Gfx_Vulkan_CmdTransferImageToImage(commandBuffer,
                                       dependencyFlags,
                                       pSrcAcquireBarrierNext,
                                       srcStageMask,
                                       srcAccessMask,
                                       srcInitialLayout,
                                       srcTransferLayout,
                                       srcImage,
                                       srcSubresource,
                                       src_offset,
                                       pDstAcquireBarrierNext,
                                       dstStageMask,
                                       dstAccessMask,
                                       dstInitialLayout,
                                       dstTransferLayout,
                                       dstImage,
                                       dstSubresource,
                                       dst_offset,
                                       image_extent,
                                       pSrcBarrierNext,
                                       srcDstStageMask,
                                       srcDstAccessMask,
                                       srcFinalLayout,
                                       pDstBarrierNext,
                                       dstDstStageMask,
                                       dstDstAccessMask,
                                       dstFinalLayout);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferBlitImage(VkCommandBuffer          commandBuffer,
                                     VkDependencyFlags        dependencyFlags,
                                     const void*              pSrcAcquireBarrierNext,
                                     VkPipelineStageFlags     srcStageMask,
                                     VkAccessFlags            srcAccessMask,
                                     VkImageLayout            srcInitialLayout,
                                     VkImageLayout            srcTransferLayout,
                                     VkImage                  srcImage,
                                     VkImageSubresourceLayers srcSubresource,
                                     VkOffset3D               srcOffset,
                                     VkExtent3D               srcExtent,
                                     const void*              pDstAcquireBarrierNext,
                                     VkPipelineStageFlags     dstStageMask,
                                     VkAccessFlags            dstAccessMask,
                                     VkImageLayout            dstInitialLayout,
                                     VkImageLayout            dstTransferLayout,
                                     VkImage                  dstImage,
                                     VkImageSubresourceLayers dstSubresource,
                                     VkOffset3D               dstOffset,
                                     VkExtent3D               dstExtent,
                                     VkFilter                 filter,
                                     const void*              pSrcBarrierNext,
                                     VkPipelineStageFlags     srcDstStageMask,
                                     VkAccessFlags            srcDstAccessMask,
                                     VkImageLayout            srcFinalLayout,
                                     const void*              pDstBarrierNext,
                                     VkPipelineStageFlags     dstDstStageMask,
                                     VkAccessFlags            dstDstAccessMask,
                                     VkImageLayout            dstFinalLayout)
{
    VkImageSubresourceRange src_range;
    VkImageSubresourceRange dst_range;

    src_range.aspectMask     = srcSubresource.aspectMask;
    src_range.baseMipLevel   = srcSubresource.mipLevel;
    src_range.levelCount     = 1;
    src_range.baseArrayLayer = srcSubresource.baseArrayLayer;
    src_range.layerCount     = srcSubresource.layerCount;
    dst_range.aspectMask     = dstSubresource.aspectMask;
    dst_range.baseMipLevel   = dstSubresource.mipLevel;
    dst_range.levelCount     = 1;
    dst_range.baseArrayLayer = dstSubresource.baseArrayLayer;
    dst_range.layerCount     = dstSubresource.layerCount;
    Gfx_Vulkan_CmdImageBarrier(commandBuffer,
                               srcStageMask,
                               (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                               dependencyFlags,
                               pSrcAcquireBarrierNext,
                               srcAccessMask,
                               (VkAccessFlags)VK_ACCESS_TRANSFER_READ_BIT,
                               srcInitialLayout,
                               srcTransferLayout,
                               srcImage,
                               src_range);
    Gfx_Vulkan_CmdImageBarrier(commandBuffer,
                               dstStageMask,
                               (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                               dependencyFlags,
                               pDstAcquireBarrierNext,
                               dstAccessMask,
                               (VkAccessFlags)VK_ACCESS_TRANSFER_WRITE_BIT,
                               dstInitialLayout,
                               dstTransferLayout,
                               dstImage,
                               dst_range);
    Gfx_Vulkan_CmdBlitImage(commandBuffer, srcImage, srcTransferLayout, srcSubresource, srcOffset, srcExtent, dstImage, dstTransferLayout, dstSubresource, dstOffset, dstExtent, filter);
    Gfx_Vulkan_CmdImageBarrier(commandBuffer,
                               (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                               srcDstStageMask,
                               dependencyFlags,
                               pSrcBarrierNext,
                               (VkAccessFlags)VK_ACCESS_TRANSFER_READ_BIT,
                               srcDstAccessMask,
                               srcTransferLayout,
                               srcFinalLayout,
                               srcImage,
                               src_range);
    Gfx_Vulkan_CmdImageBarrier(commandBuffer,
                               (VkPipelineStageFlags)VK_PIPELINE_STAGE_TRANSFER_BIT,
                               dstDstStageMask,
                               dependencyFlags,
                               pDstBarrierNext,
                               (VkAccessFlags)VK_ACCESS_TRANSFER_WRITE_BIT,
                               dstDstAccessMask,
                               dstTransferLayout,
                               dstFinalLayout,
                               dstImage,
                               dst_range);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdTransferBlitImage2D(VkCommandBuffer          commandBuffer,
                                       VkDependencyFlags        dependencyFlags,
                                       const void*              pSrcAcquireBarrierNext,
                                       VkPipelineStageFlags     srcStageMask,
                                       VkAccessFlags            srcAccessMask,
                                       VkImageLayout            srcInitialLayout,
                                       VkImageLayout            srcTransferLayout,
                                       VkImage                  srcImage,
                                       VkImageSubresourceLayers srcSubresource,
                                       VkOffset2D               srcOffset,
                                       VkExtent2D               srcExtent,
                                       const void*              pDstAcquireBarrierNext,
                                       VkPipelineStageFlags     dstStageMask,
                                       VkAccessFlags            dstAccessMask,
                                       VkImageLayout            dstInitialLayout,
                                       VkImageLayout            dstTransferLayout,
                                       VkImage                  dstImage,
                                       VkImageSubresourceLayers dstSubresource,
                                       VkOffset2D               dstOffset,
                                       VkExtent2D               dstExtent,
                                       VkFilter                 filter,
                                       const void*              pSrcBarrierNext,
                                       VkPipelineStageFlags     srcDstStageMask,
                                       VkAccessFlags            srcDstAccessMask,
                                       VkImageLayout            srcFinalLayout,
                                       const void*              pDstBarrierNext,
                                       VkPipelineStageFlags     dstDstStageMask,
                                       VkAccessFlags            dstDstAccessMask,
                                       VkImageLayout            dstFinalLayout)
{
    VkOffset3D src_offset;
    VkExtent3D src_extent;
    VkOffset3D dst_offset;
    VkExtent3D dst_extent;

    src_offset.x      = srcOffset.x;
    src_offset.y      = srcOffset.y;
    src_offset.z      = 0;
    src_extent.width  = srcExtent.width;
    src_extent.height = srcExtent.height;
    src_extent.depth  = 1;
    dst_offset.x      = dstOffset.x;
    dst_offset.y      = dstOffset.y;
    dst_offset.z      = 0;
    dst_extent.width  = dstExtent.width;
    dst_extent.height = dstExtent.height;
    dst_extent.depth  = 1;
    Gfx_Vulkan_CmdTransferBlitImage(commandBuffer,
                                    dependencyFlags,
                                    pSrcAcquireBarrierNext,
                                    srcStageMask,
                                    srcAccessMask,
                                    srcInitialLayout,
                                    srcTransferLayout,
                                    srcImage,
                                    srcSubresource,
                                    src_offset,
                                    src_extent,
                                    pDstAcquireBarrierNext,
                                    dstStageMask,
                                    dstAccessMask,
                                    dstInitialLayout,
                                    dstTransferLayout,
                                    dstImage,
                                    dstSubresource,
                                    dst_offset,
                                    dst_extent,
                                    filter,
                                    pSrcBarrierNext,
                                    srcDstStageMask,
                                    srcDstAccessMask,
                                    srcFinalLayout,
                                    pDstBarrierNext,
                                    dstDstStageMask,
                                    dstDstAccessMask,
                                    dstFinalLayout);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != srcBuffer);
    LOGASSERT(VK_NULL_HANDLE != dstImage);
    LOGASSERT(0 != regionCount);
    LOGASSERT(NULL != pRegions);

    vkCmdCopyBufferToImage(commandBuffer, srcBuffer, dstImage, dstImageLayout, regionCount, pRegions);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != srcImage);
    LOGASSERT(VK_NULL_HANDLE != dstBuffer);
    LOGASSERT(0 != regionCount);
    LOGASSERT(NULL != pRegions);

    vkCmdCopyImageToBuffer(commandBuffer, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdCopyImageRegions(VkCommandBuffer    commandBuffer,
                                    VkImage            srcImage,
                                    VkImageLayout      srcImageLayout,
                                    VkImage            dstImage,
                                    VkImageLayout      dstImageLayout,
                                    uint32_t           regionCount,
                                    const VkImageCopy* pRegions)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != srcImage);
    LOGASSERT(VK_NULL_HANDLE != dstImage);
    LOGASSERT(0 != regionCount);
    LOGASSERT(NULL != pRegions);

    vkCmdCopyImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdCopyImage(VkCommandBuffer          commandBuffer,
                             VkImage                  srcImage,
                             VkImageLayout            srcImageLayout,
                             VkImageSubresourceLayers srcSubresource,
                             VkOffset3D               srcOffset,
                             VkImage                  dstImage,
                             VkImageLayout            dstImageLayout,
                             VkImageSubresourceLayers dstSubresource,
                             VkOffset3D               dstOffset,
                             VkExtent3D               extent)
{
    VkImageCopy region;

    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != srcImage);
    LOGASSERT(VK_NULL_HANDLE != dstImage);
    LOGASSERT((VkImageAspectFlags)0 != srcSubresource.aspectMask);
    LOGASSERT(0 != srcSubresource.layerCount);
    LOGASSERT((VkImageAspectFlags)0 != dstSubresource.aspectMask);
    LOGASSERT(0 != dstSubresource.layerCount);
    LOGASSERT(0 != extent.width);
    LOGASSERT(0 != extent.height);
    LOGASSERT(0 != extent.depth);

    region.srcSubresource = srcSubresource;
    region.srcOffset      = srcOffset;
    region.dstSubresource = dstSubresource;
    region.dstOffset      = dstOffset;
    region.extent         = extent;
    Gfx_Vulkan_CmdCopyImageRegions(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, 1, &region);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdBlitImageRegions(VkCommandBuffer    commandBuffer,
                                    VkImage            srcImage,
                                    VkImageLayout      srcImageLayout,
                                    VkImage            dstImage,
                                    VkImageLayout      dstImageLayout,
                                    uint32_t           regionCount,
                                    const VkImageBlit* pRegions,
                                    VkFilter           filter)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != srcImage);
    LOGASSERT(VK_NULL_HANDLE != dstImage);
    LOGASSERT(0 != regionCount);
    LOGASSERT(NULL != pRegions);

    vkCmdBlitImage(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, regionCount, pRegions, filter);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdBlitImage(VkCommandBuffer          commandBuffer,
                             VkImage                  srcImage,
                             VkImageLayout            srcImageLayout,
                             VkImageSubresourceLayers srcSubresource,
                             VkOffset3D               srcOffset,
                             VkExtent3D               srcExtent,
                             VkImage                  dstImage,
                             VkImageLayout            dstImageLayout,
                             VkImageSubresourceLayers dstSubresource,
                             VkOffset3D               dstOffset,
                             VkExtent3D               dstExtent,
                             VkFilter                 filter)
{
    VkImageBlit region;

    LOGASSERT(0 != srcExtent.width);
    LOGASSERT(0 != srcExtent.height);
    LOGASSERT(0 != srcExtent.depth);
    LOGASSERT(0 != dstExtent.width);
    LOGASSERT(0 != dstExtent.height);
    LOGASSERT(0 != dstExtent.depth);

    region.srcSubresource  = srcSubresource;
    region.srcOffsets[0]   = srcOffset;
    region.srcOffsets[1].x = srcOffset.x + (int32_t)srcExtent.width;
    region.srcOffsets[1].y = srcOffset.y + (int32_t)srcExtent.height;
    region.srcOffsets[1].z = srcOffset.z + (int32_t)srcExtent.depth;
    region.dstSubresource  = dstSubresource;
    region.dstOffsets[0]   = dstOffset;
    region.dstOffsets[1].x = dstOffset.x + (int32_t)dstExtent.width;
    region.dstOffsets[1].y = dstOffset.y + (int32_t)dstExtent.height;
    region.dstOffsets[1].z = dstOffset.z + (int32_t)dstExtent.depth;
    Gfx_Vulkan_CmdBlitImageRegions(commandBuffer, srcImage, srcImageLayout, dstImage, dstImageLayout, 1, &region, filter);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdBlitImage2D(VkCommandBuffer          commandBuffer,
                               VkImage                  srcImage,
                               VkImageLayout            srcImageLayout,
                               VkImageSubresourceLayers srcSubresource,
                               VkOffset2D               srcOffset,
                               VkExtent2D               srcExtent,
                               VkImage                  dstImage,
                               VkImageLayout            dstImageLayout,
                               VkImageSubresourceLayers dstSubresource,
                               VkOffset2D               dstOffset,
                               VkExtent2D               dstExtent,
                               VkFilter                 filter)
{
    VkOffset3D src_offset;
    VkExtent3D src_extent;
    VkOffset3D dst_offset;
    VkExtent3D dst_extent;

    src_offset.x      = srcOffset.x;
    src_offset.y      = srcOffset.y;
    src_offset.z      = 0;
    src_extent.width  = srcExtent.width;
    src_extent.height = srcExtent.height;
    src_extent.depth  = 1;
    dst_offset.x      = dstOffset.x;
    dst_offset.y      = dstOffset.y;
    dst_offset.z      = 0;
    dst_extent.width  = dstExtent.width;
    dst_extent.height = dstExtent.height;
    dst_extent.depth  = 1;
    Gfx_Vulkan_CmdBlitImage(commandBuffer, srcImage, srcImageLayout, srcSubresource, src_offset, src_extent, dstImage, dstImageLayout, dstSubresource, dst_offset, dst_extent, filter);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != pipeline);

    vkCmdBindPipeline(commandBuffer, pipelineBindPoint, pipeline);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdBindComputePipeline(VkCommandBuffer commandBuffer, VkPipeline pipeline)
{
    Gfx_Vulkan_CmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdBindDescriptorSets(VkCommandBuffer        commandBuffer,
                                      VkPipelineBindPoint    pipelineBindPoint,
                                      VkPipelineLayout       layout,
                                      uint32_t               firstSet,
                                      uint32_t               descriptorSetCount,
                                      const VkDescriptorSet* pDescriptorSets,
                                      uint32_t               dynamicOffsetCount,
                                      const uint32_t*        pDynamicOffsets)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != layout);
    LOGASSERT(0 != descriptorSetCount);
    LOGASSERT(NULL != pDescriptorSets);
    LOGASSERT(0 == dynamicOffsetCount || NULL != pDynamicOffsets);

    vkCmdBindDescriptorSets(commandBuffer, pipelineBindPoint, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdBindComputeDescriptorSets(VkCommandBuffer        commandBuffer,
                                             VkPipelineLayout       layout,
                                             uint32_t               firstSet,
                                             uint32_t               descriptorSetCount,
                                             const VkDescriptorSet* pDescriptorSets,
                                             uint32_t               dynamicOffsetCount,
                                             const uint32_t*        pDynamicOffsets)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != layout);
    LOGASSERT(0 != descriptorSetCount);
    LOGASSERT(NULL != pDescriptorSets);
    LOGASSERT(0 == dynamicOffsetCount || NULL != pDynamicOffsets);

    Gfx_Vulkan_CmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != layout);
    LOGASSERT((VkShaderStageFlags)0 != stageFlags);
    LOGASSERT(0 == offset % sizeof(uint32_t));
    LOGASSERT(0 != size);
    LOGASSERT(0 == size % sizeof(uint32_t));
    LOGASSERT(NULL != pValues);

    vkCmdPushConstants(commandBuffer, layout, stageFlags, offset, size, pValues);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdPushComputeConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, uint32_t offset, uint32_t size, const void* pValues)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != layout);
    LOGASSERT(0 == offset % sizeof(uint32_t));
    LOGASSERT(0 != size);
    LOGASSERT(0 == size % sizeof(uint32_t));
    LOGASSERT(NULL != pValues);

    Gfx_Vulkan_CmdPushConstants(commandBuffer, layout, (VkShaderStageFlags)VK_SHADER_STAGE_COMPUTE_BIT, offset, size, pValues);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(0 != groupCountX);
    LOGASSERT(0 != groupCountY);
    LOGASSERT(0 != groupCountZ);

    vkCmdDispatch(commandBuffer, groupCountX, groupCountY, groupCountZ);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdDispatchItems(VkCommandBuffer commandBuffer, VkExtent3D itemCount, VkExtent3D itemsPerInvocation, VkExtent3D localSize)
{
    uint32_t items_per_group_x;
    uint32_t items_per_group_y;
    uint32_t items_per_group_z;
    uint32_t group_count_x;
    uint32_t group_count_y;
    uint32_t group_count_z;

    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(0 != itemCount.width);
    LOGASSERT(0 != itemCount.height);
    LOGASSERT(0 != itemCount.depth);
    LOGASSERT(0 != itemsPerInvocation.width);
    LOGASSERT(0 != itemsPerInvocation.height);
    LOGASSERT(0 != itemsPerInvocation.depth);
    LOGASSERT(0 != localSize.width);
    LOGASSERT(0 != localSize.height);
    LOGASSERT(0 != localSize.depth);
    LOGASSERT(itemsPerInvocation.width <= UINT32_MAX / localSize.width);
    LOGASSERT(itemsPerInvocation.height <= UINT32_MAX / localSize.height);
    LOGASSERT(itemsPerInvocation.depth <= UINT32_MAX / localSize.depth);

    items_per_group_x = itemsPerInvocation.width * localSize.width;
    items_per_group_y = itemsPerInvocation.height * localSize.height;
    items_per_group_z = itemsPerInvocation.depth * localSize.depth;
    LOGASSERT(itemCount.width <= UINT32_MAX - (items_per_group_x - 1u));
    LOGASSERT(itemCount.height <= UINT32_MAX - (items_per_group_y - 1u));
    LOGASSERT(itemCount.depth <= UINT32_MAX - (items_per_group_z - 1u));

    group_count_x = (itemCount.width + items_per_group_x - 1u) / items_per_group_x;
    group_count_y = (itemCount.height + items_per_group_y - 1u) / items_per_group_y;
    group_count_z = (itemCount.depth + items_per_group_z - 1u) / items_per_group_z;
    Gfx_Vulkan_CmdDispatch(commandBuffer, group_count_x, group_count_y, group_count_z);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdDispatchItems2D(VkCommandBuffer commandBuffer, VkExtent2D itemCount, VkExtent2D itemsPerInvocation, VkExtent2D localSize)
{
    VkExtent3D item_count;
    VkExtent3D items_per_invocation;
    VkExtent3D local_size;

    item_count.width            = itemCount.width;
    item_count.height           = itemCount.height;
    item_count.depth            = 1;
    items_per_invocation.width  = itemsPerInvocation.width;
    items_per_invocation.height = itemsPerInvocation.height;
    items_per_invocation.depth  = 1;
    local_size.width            = localSize.width;
    local_size.height           = localSize.height;
    local_size.depth            = 1;
    Gfx_Vulkan_CmdDispatchItems(commandBuffer, item_count, items_per_invocation, local_size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdDispatchComputeItems(VkCommandBuffer        commandBuffer,
                                        VkPipeline             pipeline,
                                        VkPipelineLayout       layout,
                                        uint32_t               firstSet,
                                        uint32_t               descriptorSetCount,
                                        const VkDescriptorSet* pDescriptorSets,
                                        uint32_t               dynamicOffsetCount,
                                        const uint32_t*        pDynamicOffsets,
                                        uint32_t               pushConstantOffset,
                                        uint32_t               pushConstantSize,
                                        const void*            pPushConstants,
                                        VkExtent3D             itemCount,
                                        VkExtent3D             itemsPerInvocation,
                                        VkExtent3D             localSize)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != pipeline);
    LOGASSERT(VK_NULL_HANDLE != layout);
    LOGASSERT(0 == descriptorSetCount || NULL != pDescriptorSets);
    LOGASSERT(0 == dynamicOffsetCount || NULL != pDynamicOffsets);
    LOGASSERT(0 == pushConstantSize || NULL != pPushConstants);

    Gfx_Vulkan_CmdBindComputePipeline(commandBuffer, pipeline);
    Gfx_Vulkan_CmdBindComputeDescriptorSets(commandBuffer, layout, firstSet, descriptorSetCount, pDescriptorSets, dynamicOffsetCount, pDynamicOffsets);
    if (0 != pushConstantSize)
    {
        Gfx_Vulkan_CmdPushComputeConstants(commandBuffer, layout, pushConstantOffset, pushConstantSize, pPushConstants);
    }
    Gfx_Vulkan_CmdDispatchItems(commandBuffer, itemCount, itemsPerInvocation, localSize);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdDispatchComputeItems2D(VkCommandBuffer        commandBuffer,
                                          VkPipeline             pipeline,
                                          VkPipelineLayout       layout,
                                          uint32_t               firstSet,
                                          uint32_t               descriptorSetCount,
                                          const VkDescriptorSet* pDescriptorSets,
                                          uint32_t               dynamicOffsetCount,
                                          const uint32_t*        pDynamicOffsets,
                                          uint32_t               pushConstantOffset,
                                          uint32_t               pushConstantSize,
                                          const void*            pPushConstants,
                                          VkExtent2D             itemCount,
                                          VkExtent2D             itemsPerInvocation,
                                          VkExtent2D             localSize)
{
    VkExtent3D item_count;
    VkExtent3D items_per_invocation;
    VkExtent3D local_size;

    item_count.width            = itemCount.width;
    item_count.height           = itemCount.height;
    item_count.depth            = 1;
    items_per_invocation.width  = itemsPerInvocation.width;
    items_per_invocation.height = itemsPerInvocation.height;
    items_per_invocation.depth  = 1;
    local_size.width            = localSize.width;
    local_size.height           = localSize.height;
    local_size.depth            = 1;
    Gfx_Vulkan_CmdDispatchComputeItems(commandBuffer,
                                       pipeline,
                                       layout,
                                       firstSet,
                                       descriptorSetCount,
                                       pDescriptorSets,
                                       dynamicOffsetCount,
                                       pDynamicOffsets,
                                       pushConstantOffset,
                                       pushConstantSize,
                                       pPushConstants,
                                       item_count,
                                       items_per_invocation,
                                       local_size);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateQueryPool(VkDevice                      device,
                                const void*                   pNext,
                                VkQueryPoolCreateFlags        flags,
                                VkQueryType                   queryType,
                                uint32_t                      queryCount,
                                VkQueryPipelineStatisticFlags pipelineStatistics,
                                const VkAllocationCallbacks*  pAllocator,
                                VkQueryPool*                  pQueryPool)
{
    VkQueryPoolCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != queryCount);
    LOGASSERT(NULL != pQueryPool);

    create_info.sType              = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    create_info.pNext              = pNext;
    create_info.flags              = flags;
    create_info.queryType          = queryType;
    create_info.queryCount         = queryCount;
    create_info.pipelineStatistics = pipelineStatistics;
    VKCHECK(vkCreateQueryPool(device, &create_info, pAllocator, pQueryPool));
    LOGASSERT(VK_NULL_HANDLE != (*pQueryPool));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != queryPool);
    LOGASSERT(0 != queryCount);
    LOGASSERT(NULL != pData);

    VKCHECK(vkGetQueryPoolResults(device, queryPool, firstQuery, queryCount, dataSize, pData, stride, flags));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateQueryPool64(VkDevice device, const void* pNext, VkQueryPoolCreateFlags flags, uint32_t queryCount, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool)
{
    Gfx_Vulkan_CreateQueryPool(device, pNext, flags, VK_QUERY_TYPE_TIMESTAMP, queryCount, (VkQueryPipelineStatisticFlags)0, pAllocator, pQueryPool);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetQueryPoolResults64(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkQueryResultFlags flags, uint64_t* pResults)
{
    VkQueryResultFlags result_flags;

    LOGASSERT(0 != queryCount);
    LOGASSERT(NULL != pResults);

    result_flags = (VkQueryResultFlags)((VkFlags)flags | (VkFlags)VK_QUERY_RESULT_64_BIT);
    Gfx_Vulkan_GetQueryPoolResults(device, queryPool, firstQuery, queryCount, (size_t)queryCount * sizeof((*pResults)), pResults, sizeof((*pResults)), result_flags);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT(VK_NULL_HANDLE != queryPool);
    LOGASSERT(0 != queryCount);

    vkCmdResetQueryPool(commandBuffer, queryPool, firstQuery, queryCount);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);
    LOGASSERT((VkPipelineStageFlags)0 != (VkPipelineStageFlags)pipelineStage);
    LOGASSERT(VK_NULL_HANDLE != queryPool);

    vkCmdWriteTimestamp(commandBuffer, pipelineStage, queryPool, query);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateSemaphore(VkDevice device, const void* pNext, VkSemaphoreCreateFlags flags, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore)
{
    VkSemaphoreCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != pSemaphore);

    create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    create_info.pNext = pNext;
    create_info.flags = flags;
    VKCHECK(vkCreateSemaphore(device, &create_info, pAllocator, pSemaphore));
    LOGASSERT(VK_NULL_HANDLE != (*pSemaphore));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateFence(VkDevice device, const void* pNext, VkFenceCreateFlags flags, const VkAllocationCallbacks* pAllocator, VkFence* pFence)
{
    VkFenceCreateInfo create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(NULL != pFence);

    create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    create_info.pNext = pNext;
    create_info.flags = flags;
    VKCHECK(vkCreateFence(device, &create_info, pAllocator, pFence));
    LOGASSERT(VK_NULL_HANDLE != (*pFence));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != fenceCount);
    LOGASSERT(NULL != pFences);

    VKCHECK(vkWaitForFences(device, fenceCount, pFences, waitAll, timeout));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_WaitForFence(VkDevice device, VkFence fence, uint64_t timeout)
{
    LOGASSERT(VK_NULL_HANDLE != fence);

    Gfx_Vulkan_WaitForFences(device, 1, &fence, VK_TRUE, timeout);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(0 != fenceCount);
    LOGASSERT(NULL != pFences);

    VKCHECK(vkResetFences(device, fenceCount, pFences));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_ResetFence(VkDevice device, VkFence fence)
{
    LOGASSERT(VK_NULL_HANDLE != fence);

    Gfx_Vulkan_ResetFences(device, 1, &fence);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_QueueSubmitInfos(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence)
{
    LOGASSERT(VK_NULL_HANDLE != queue);
    LOGASSERT(0 == submitCount || NULL != pSubmits);

    VKCHECK(vkQueueSubmit(queue, submitCount, pSubmits, fence));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_QueueSubmit(VkQueue                     queue,
                            const void*                 pNext,
                            uint32_t                    waitSemaphoreCount,
                            const VkSemaphore*          pWaitSemaphores,
                            const VkPipelineStageFlags* pWaitDstStageMask,
                            uint32_t                    commandBufferCount,
                            const VkCommandBuffer*      pCommandBuffers,
                            uint32_t                    signalSemaphoreCount,
                            const VkSemaphore*          pSignalSemaphores,
                            VkFence                     fence)
{
    VkSubmitInfo submit_info;

    LOGASSERT(VK_NULL_HANDLE != queue);
    LOGASSERT(0 == waitSemaphoreCount || NULL != pWaitSemaphores);
    LOGASSERT(0 == waitSemaphoreCount || NULL != pWaitDstStageMask);
    LOGASSERT(0 == commandBufferCount || NULL != pCommandBuffers);
    LOGASSERT(0 == signalSemaphoreCount || NULL != pSignalSemaphores);

    submit_info.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.pNext                = pNext;
    submit_info.waitSemaphoreCount   = waitSemaphoreCount;
    submit_info.pWaitSemaphores      = pWaitSemaphores;
    submit_info.pWaitDstStageMask    = pWaitDstStageMask;
    submit_info.commandBufferCount   = commandBufferCount;
    submit_info.pCommandBuffers      = pCommandBuffers;
    submit_info.signalSemaphoreCount = signalSemaphoreCount;
    submit_info.pSignalSemaphores    = pSignalSemaphores;
    Gfx_Vulkan_QueueSubmitInfos(queue, 1, &submit_info, fence);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_QueueSubmitCommandBuffer(VkQueue                     queue,
                                         const void*                 pNext,
                                         uint32_t                    waitSemaphoreCount,
                                         const VkSemaphore*          pWaitSemaphores,
                                         const VkPipelineStageFlags* pWaitDstStageMasks,
                                         VkCommandBuffer             commandBuffer,
                                         uint32_t                    signalSemaphoreCount,
                                         const VkSemaphore*          pSignalSemaphores,
                                         VkFence                     fence)
{
    LOGASSERT(VK_NULL_HANDLE != commandBuffer);

    Gfx_Vulkan_QueueSubmit(queue, pNext, waitSemaphoreCount, pWaitSemaphores, pWaitDstStageMasks, 1, &commandBuffer, signalSemaphoreCount, pSignalSemaphores, fence);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_QueueSubmitTimelineSemaphore(VkQueue                     queue,
                                             const void*                 pNext,
                                             uint32_t                    waitSemaphoreCount,
                                             const VkSemaphore*          pWaitSemaphores,
                                             const VkPipelineStageFlags* pWaitDstStageMask,
                                             const uint64_t*             pWaitSemaphoreValues,
                                             uint32_t                    commandBufferCount,
                                             const VkCommandBuffer*      pCommandBuffers,
                                             uint32_t                    signalSemaphoreCount,
                                             const VkSemaphore*          pSignalSemaphores,
                                             const uint64_t*             pSignalSemaphoreValues,
                                             VkFence                     fence)
{
    VkTimelineSemaphoreSubmitInfo semaphore_values;

    semaphore_values.sType                     = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    semaphore_values.pNext                     = pNext;
    semaphore_values.waitSemaphoreValueCount   = waitSemaphoreCount;
    semaphore_values.pWaitSemaphoreValues      = pWaitSemaphoreValues;
    semaphore_values.signalSemaphoreValueCount = signalSemaphoreCount;
    semaphore_values.pSignalSemaphoreValues    = pSignalSemaphoreValues;
    Gfx_Vulkan_QueueSubmit(queue, &semaphore_values, waitSemaphoreCount, pWaitSemaphores, pWaitDstStageMask, commandBufferCount, pCommandBuffers, signalSemaphoreCount, pSignalSemaphores, fence);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_QueueWaitIdle(VkQueue queue)
{
    LOGASSERT(VK_NULL_HANDLE != queue);

    VKCHECK(vkQueueWaitIdle(queue));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfaceSupport(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported)
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_INVALID != queueFamilyIndex);
    LOGASSERT(VK_NULL_HANDLE != surface);
    LOGASSERT(NULL != pSupported);

    VKCHECK(vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, queueFamilyIndex, surface, pSupported));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfaceCapabilities(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pCapabilities)
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != surface);
    LOGASSERT(NULL != pCapabilities);

    VKCHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, pCapabilities));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfaceFormats(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR pSurfaceFormats[VK_MAX_SURFACE_FORMATS])
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != surface);
    LOGASSERT(NULL != pSurfaceFormatCount);
    LOGASSERT(NULL != pSurfaceFormats);

    (*pSurfaceFormatCount) = 0;
    VKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, pSurfaceFormatCount, NULL));
    LOGASSERT(VK_MAX_SURFACE_FORMATS >= (*pSurfaceFormatCount));

    VKCHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, pSurfaceFormatCount, pSurfaceFormats));
    LOGASSERT(0 != (*pSurfaceFormatCount));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfaceFormatProperties(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkFormat imageFormat, VkSurfaceFormatKHR* pSurfaceFormat)
{
    VkSurfaceFormatKHR surface_formats[VK_MAX_SURFACE_FORMATS];
    uint32_t           surface_format_count;
    uint32_t           surface_format_index;

    LOGASSERT(VK_FORMAT_UNDEFINED != imageFormat);
    LOGASSERT(NULL != pSurfaceFormat);

    Gfx_Vulkan_GetPhysicalDeviceSurfaceFormats(physicalDevice, surface, &surface_format_count, surface_formats);
    if (1 == surface_format_count && VK_FORMAT_UNDEFINED == surface_formats[DEFAULT_SURFACE_FORMAT_INDEX].format)
    {
        (*pSurfaceFormat)      = surface_formats[DEFAULT_SURFACE_FORMAT_INDEX];
        pSurfaceFormat->format = imageFormat;
        return;
    }
    for (surface_format_index = 0; surface_format_index < surface_format_count; ++surface_format_index)
    {
        if (imageFormat == surface_formats[surface_format_index].format)
        {
            (*pSurfaceFormat) = surface_formats[surface_format_index];
            return;
        }
    }
    LOGSTART(stderr);
    LOGPRINTS("Requested presentation format: ");
    LOGPRINTS(string_VkFormat(imageFormat));
    LOGPRINTS("; supported formats:");
    for (surface_format_index = 0; surface_format_index < surface_format_count; ++surface_format_index)
    {
        LOGPRINTS(" ");
        LOGPRINTS(string_VkFormat(surface_formats[surface_format_index].format));
    }
    LOGPRINTS("\n");
    LOGEND();
    LOGFATAL("Vulkan presentation surface does not support the requested image format");
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfacePresentModes(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR pPresentModes[VK_MAX_PRESENT_MODES])
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != surface);
    LOGASSERT(NULL != pPresentModeCount);
    LOGASSERT(NULL != pPresentModes);

    (*pPresentModeCount) = 0;
    VKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, pPresentModeCount, NULL));
    LOGASSERT(VK_MAX_PRESENT_MODES >= (*pPresentModeCount));

    VKCHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, pPresentModeCount, pPresentModes));
    LOGASSERT(0 != (*pPresentModeCount));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfacePresentModeSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkPresentModeKHR presentMode, VkBool32* pSupported)
{
    VkPresentModeKHR present_modes[VK_MAX_PRESENT_MODES];
    uint32_t         present_mode_count;
    uint32_t         present_mode_index;

    LOGASSERT(NULL != pSupported);

    Gfx_Vulkan_GetPhysicalDeviceSurfacePresentModes(physicalDevice, surface, &present_mode_count, present_modes);
    (*pSupported) = VK_FALSE;
    for (present_mode_index = 0; present_mode_index < present_mode_count; ++present_mode_index)
    {
        if (presentMode == present_modes[present_mode_index])
        {
            (*pSupported) = VK_TRUE;
            return;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetPhysicalDeviceDisplayProperties(VkPhysicalDevice physicalDevice, VkDisplayPropertiesKHR pProperties[VK_MAX_DISPLAYS])
{
    uint32_t property_count;
    uint32_t property_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pProperties);

    property_count = 0;
    VKCHECK(vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &property_count, NULL));
    LOGASSERT(VK_MAX_DISPLAYS >= property_count);

    VKCHECK(vkGetPhysicalDeviceDisplayPropertiesKHR(physicalDevice, &property_count, pProperties));
    for (property_index = 0; property_index < property_count; ++property_index)
    {
        LOGASSERT(VK_NULL_HANDLE != pProperties[property_index].display);
    }
    return property_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetDisplayModeProperties(VkPhysicalDevice physicalDevice, VkDisplayKHR display, VkDisplayModePropertiesKHR pProperties[VK_MAX_DISPLAY_MODES])
{
    uint32_t property_count;
    uint32_t property_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != display);
    LOGASSERT(NULL != pProperties);

    property_count = 0;
    VKCHECK(vkGetDisplayModePropertiesKHR(physicalDevice, display, &property_count, NULL));
    LOGASSERT(VK_MAX_DISPLAY_MODES >= property_count);

    VKCHECK(vkGetDisplayModePropertiesKHR(physicalDevice, display, &property_count, pProperties));
    for (property_index = 0; property_index < property_count; ++property_index)
    {
        LOGASSERT(VK_NULL_HANDLE != pProperties[property_index].displayMode);
    }
    return property_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetPhysicalDeviceDisplayPlaneProperties(VkPhysicalDevice physicalDevice, VkDisplayPlanePropertiesKHR pProperties[VK_MAX_DISPLAY_PLANES])
{
    uint32_t property_count;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(NULL != pProperties);

    property_count = 0;
    VKCHECK(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physicalDevice, &property_count, NULL));
    LOGASSERT(VK_MAX_DISPLAY_PLANES >= property_count);

    VKCHECK(vkGetPhysicalDeviceDisplayPlanePropertiesKHR(physicalDevice, &property_count, pProperties));
    return property_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetDisplayPlaneSupportedDisplays(VkPhysicalDevice physicalDevice, uint32_t planeIndex, VkDisplayKHR pDisplays[VK_MAX_DISPLAYS])
{
    uint32_t display_count;
    uint32_t display_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_INVALID != planeIndex);
    LOGASSERT(NULL != pDisplays);

    display_count = 0;
    VKCHECK(vkGetDisplayPlaneSupportedDisplaysKHR(physicalDevice, planeIndex, &display_count, NULL));
    LOGASSERT(VK_MAX_DISPLAYS >= display_count);

    VKCHECK(vkGetDisplayPlaneSupportedDisplaysKHR(physicalDevice, planeIndex, &display_count, pDisplays));
    for (display_index = 0; display_index < display_count; ++display_index)
    {
        LOGASSERT(VK_NULL_HANDLE != pDisplays[display_index]);
    }
    return display_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetCompatibleDisplayPlaneIndex(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t planeCount, const VkDisplayPlanePropertiesKHR* pPlaneProperties)
{
    VkDisplayKHR supported_displays[VK_MAX_DISPLAYS];
    uint32_t     supported_display_count;
    uint32_t     supported_display_index;
    uint32_t     plane_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != display);
    LOGASSERT(planeCount);
    LOGASSERT(NULL != pPlaneProperties);

    for (plane_index = 0; plane_index < planeCount; ++plane_index)
    {
        if (VK_NULL_HANDLE == pPlaneProperties[plane_index].currentDisplay || display == pPlaneProperties[plane_index].currentDisplay)
        {
            supported_display_count = Gfx_Vulkan_GetDisplayPlaneSupportedDisplays(physicalDevice, plane_index, supported_displays);
            for (supported_display_index = 0; supported_display_index < supported_display_count; ++supported_display_index)
            {
                if (display == supported_displays[supported_display_index])
                {
                    break;
                }
            }
            if (supported_display_count != supported_display_index)
            {
                break;
            }
        }
    }
    LOGASSERT(planeCount != plane_index);

    return plane_index;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetDisplayPlaneCapabilities(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR* pCapabilities)
{
    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != mode);
    LOGASSERT(VK_INVALID != planeIndex);
    LOGASSERT(NULL != pCapabilities);

    VKCHECK(vkGetDisplayPlaneCapabilitiesKHR(physicalDevice, mode, planeIndex, pCapabilities));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateHeadlessSurface(VkInstance instance, const void* pNext, VkHeadlessSurfaceCreateFlagsEXT flags, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
    VkHeadlessSurfaceCreateInfoEXT create_info;

    LOGASSERT(VK_NULL_HANDLE != instance);
    LOGASSERT(NULL != pSurface);

    create_info.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    create_info.pNext = pNext;
    create_info.flags = flags;
    (*pSurface)       = VK_NULL_HANDLE;
    VKCHECK(vkCreateHeadlessSurfaceEXT(instance, &create_info, pAllocator, pSurface));
    LOGASSERT(VK_NULL_HANDLE != (*pSurface));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateDisplayPlaneSurface(VkInstance                     instance,
                                          const void*                    pNext,
                                          VkDisplaySurfaceCreateFlagsKHR flags,
                                          VkDisplayModeKHR               displayMode,
                                          uint32_t                       planeIndex,
                                          uint32_t                       planeStackIndex,
                                          VkSurfaceTransformFlagBitsKHR  transform,
                                          float                          globalAlpha,
                                          VkDisplayPlaneAlphaFlagBitsKHR alphaMode,
                                          VkExtent2D                     imageExtent,
                                          const VkAllocationCallbacks*   pAllocator,
                                          VkSurfaceKHR*                  pSurface)
{
    VkDisplaySurfaceCreateInfoKHR create_info;

    LOGASSERT(VK_NULL_HANDLE != instance);
    LOGASSERT(VK_NULL_HANDLE != displayMode);
    LOGASSERT(VK_INVALID != planeIndex);
    LOGASSERT(0 != transform);
    LOGASSERT(0 != alphaMode);
    LOGASSERT(0 != imageExtent.width);
    LOGASSERT(0 != imageExtent.height);
    LOGASSERT(NULL != pSurface);

    create_info.sType           = VK_STRUCTURE_TYPE_DISPLAY_SURFACE_CREATE_INFO_KHR;
    create_info.pNext           = pNext;
    create_info.flags           = flags;
    create_info.displayMode     = displayMode;
    create_info.planeIndex      = planeIndex;
    create_info.planeStackIndex = planeStackIndex;
    create_info.transform       = transform;
    create_info.globalAlpha     = globalAlpha;
    create_info.alphaMode       = alphaMode;
    create_info.imageExtent     = imageExtent;
    (*pSurface)                 = VK_NULL_HANDLE;
    VKCHECK(vkCreateDisplayPlaneSurfaceKHR(instance, &create_info, pAllocator, pSurface));
    LOGASSERT(VK_NULL_HANDLE != (*pSurface));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkDisplayPlaneAlphaFlagBitsKHR Gfx_Vulkan_GetDisplayPlaneAlphaMode(VkDisplayPlaneAlphaFlagsKHR supportedAlpha)
{
    uint32_t alpha_mode_index;

    for (alpha_mode_index = 0; alpha_mode_index < VK_COUNT_OF(c_vkAlphaModes); ++alpha_mode_index)
    {
        if (supportedAlpha & (VkDisplayPlaneAlphaFlagsKHR)c_vkAlphaModes[alpha_mode_index])
        {
            return c_vkAlphaModes[alpha_mode_index];
        }
    }

    LOGFATAL("Vulkan display plane exposes no supported alpha mode");
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkCompositeAlphaFlagBitsKHR Gfx_Vulkan_GetSurfaceCompositeAlpha(VkCompositeAlphaFlagsKHR supportedCompositeAlpha)
{
    uint32_t composite_alpha_mode_index;

    for (composite_alpha_mode_index = 0; composite_alpha_mode_index < VK_COUNT_OF(c_vkCompositeAlphaModes); ++composite_alpha_mode_index)
    {
        if ((VkCompositeAlphaFlagsKHR)0 != (supportedCompositeAlpha & (VkCompositeAlphaFlagsKHR)c_vkCompositeAlphaModes[composite_alpha_mode_index]))
        {
            return c_vkCompositeAlphaModes[composite_alpha_mode_index];
        }
    }
    LOGFATAL("Vulkan presentation surface exposes no composite alpha mode");
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VkSurfaceTransformFlagBitsKHR Gfx_Vulkan_GetSurfaceTransform(VkSurfaceTransformFlagsKHR supportedTransforms)
{
    uint32_t transform_index;

    for (transform_index = 0; transform_index < VK_COUNT_OF(c_vkSurfaceTransforms); ++transform_index)
    {
        if (supportedTransforms & (VkSurfaceTransformFlagsKHR)c_vkSurfaceTransforms[transform_index])
        {
            return c_vkSurfaceTransforms[transform_index];
        }
    }

    LOGFATAL("Vulkan display exposes no supported surface transform");
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateDirectDisplaySurface(VkPhysicalDevice             physicalDevice,
                                           VkInstance                   instance,
                                           uint32_t                     displayIndex,
                                           uint32_t                     modeIndex,
                                           const VkAllocationCallbacks* pAllocator,
                                           VkExtent2D*                  pImageExtent,
                                           VkSurfaceKHR*                pSurface)
{
    VkDisplayPropertiesKHR         display_properties[VK_MAX_DISPLAYS];
    VkDisplayModePropertiesKHR     mode_properties[VK_MAX_DISPLAY_MODES];
    VkDisplayPlanePropertiesKHR    plane_properties[VK_MAX_DISPLAY_PLANES];
    VkDisplayPlaneCapabilitiesKHR  plane_capabilities;
    VkDisplayModeKHR               display_mode;
    VkDisplayKHR                   display;
    VkSurfaceTransformFlagBitsKHR  transform;
    VkDisplayPlaneAlphaFlagBitsKHR alpha_mode;
    uint32_t                       display_count;
    uint32_t                       mode_count;
    uint32_t                       plane_count;
    uint32_t                       plane_index;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != instance);
    LOGASSERT(NULL != pImageExtent);
    LOGASSERT(NULL != pSurface);

    display_count = Gfx_Vulkan_GetPhysicalDeviceDisplayProperties(physicalDevice, display_properties);
    LOGASSERT(0 != display_count);
    LOGASSERT(display_count > displayIndex);

    display    = display_properties[displayIndex].display;
    mode_count = Gfx_Vulkan_GetDisplayModeProperties(physicalDevice, display, mode_properties);
    LOGASSERT(0 != mode_count);
    LOGASSERT(mode_count > modeIndex);

    display_mode    = mode_properties[modeIndex].displayMode;
    (*pImageExtent) = mode_properties[modeIndex].parameters.visibleRegion;
    plane_count     = Gfx_Vulkan_GetPhysicalDeviceDisplayPlaneProperties(physicalDevice, plane_properties);
    LOGASSERT(0 != plane_count);

    plane_index = Gfx_Vulkan_GetCompatibleDisplayPlaneIndex(physicalDevice, display, plane_count, plane_properties);
    Gfx_Vulkan_GetDisplayPlaneCapabilities(physicalDevice, display_mode, plane_index, &plane_capabilities);
    transform   = Gfx_Vulkan_GetSurfaceTransform(display_properties[displayIndex].supportedTransforms);
    alpha_mode  = Gfx_Vulkan_GetDisplayPlaneAlphaMode(plane_capabilities.supportedAlpha);
    (*pSurface) = VK_NULL_HANDLE;
    Gfx_Vulkan_CreateDisplayPlaneSurface(instance,
                                         NULL,
                                         (VkDisplaySurfaceCreateFlagsKHR)0,
                                         display_mode,
                                         plane_index,
                                         plane_properties[plane_index].currentStackIndex,
                                         transform,
                                         1.0f,
                                         alpha_mode,
                                         (*pImageExtent),
                                         pAllocator,
                                         pSurface);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_CreateSwapchain(VkDevice                      device,
                                const void*                   pNext,
                                VkSwapchainCreateFlagsKHR     flags,
                                VkSurfaceKHR                  surface,
                                uint32_t                      minimumImageCount,
                                VkFormat                      imageFormat,
                                VkColorSpaceKHR               imageColorSpace,
                                VkExtent2D                    imageExtent,
                                uint32_t                      imageArrayLayers,
                                VkImageUsageFlags             imageUsage,
                                VkSharingMode                 imageSharingMode,
                                uint32_t                      queueFamilyIndexCount,
                                const uint32_t*               pQueueFamilyIndices,
                                VkSurfaceTransformFlagBitsKHR preTransform,
                                VkCompositeAlphaFlagBitsKHR   compositeAlpha,
                                VkPresentModeKHR              presentMode,
                                VkBool32                      clipped,
                                VkSwapchainKHR                oldSwapchain,
                                const VkAllocationCallbacks*  pAllocator,
                                VkSwapchainKHR*               pSwapchain)
{
    VkSwapchainCreateInfoKHR create_info;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != surface);
    LOGASSERT(0 != minimumImageCount);
    LOGASSERT(VK_FORMAT_UNDEFINED != imageFormat);
    LOGASSERT(0 != imageExtent.width);
    LOGASSERT(0 != imageExtent.height);
    LOGASSERT(0 != imageArrayLayers);
    LOGASSERT((VkImageUsageFlags)0 != imageUsage);
    LOGASSERT(0 == queueFamilyIndexCount || NULL != pQueueFamilyIndices);
    LOGASSERT(0 != preTransform);
    LOGASSERT(0 != compositeAlpha);
    LOGASSERT(NULL != pSwapchain);

    create_info.sType                 = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    create_info.pNext                 = pNext;
    create_info.flags                 = flags;
    create_info.surface               = surface;
    create_info.minImageCount         = minimumImageCount;
    create_info.imageFormat           = imageFormat;
    create_info.imageColorSpace       = imageColorSpace;
    create_info.imageExtent           = imageExtent;
    create_info.imageArrayLayers      = imageArrayLayers;
    create_info.imageUsage            = imageUsage;
    create_info.imageSharingMode      = imageSharingMode;
    create_info.queueFamilyIndexCount = queueFamilyIndexCount;
    create_info.pQueueFamilyIndices   = pQueueFamilyIndices;
    create_info.preTransform          = preTransform;
    create_info.compositeAlpha        = compositeAlpha;
    create_info.presentMode           = presentMode;
    create_info.clipped               = clipped;
    create_info.oldSwapchain          = oldSwapchain;
    VKCHECK(vkCreateSwapchainKHR(device, &create_info, pAllocator, pSwapchain));
    LOGASSERT(VK_NULL_HANDLE != (*pSwapchain));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_SetupSwapchain(VkPhysicalDevice             physicalDevice,
                               VkDevice                     device,
                               const void*                  pNext,
                               VkSwapchainCreateFlagsKHR    flags,
                               VkSurfaceKHR                 surface,
                               uint32_t                     queueFamilyIndex,
                               uint32_t                     minimumImageCount,
                               VkFormat                     imageFormat,
                               VkExtent2D                   imageExtent,
                               uint32_t                     imageArrayLayers,
                               VkImageUsageFlags            imageUsage,
                               VkSharingMode                imageSharingMode,
                               uint32_t                     queueFamilyIndexCount,
                               const uint32_t*              pQueueFamilyIndices,
                               VkPresentModeKHR             presentMode,
                               VkBool32                     clipped,
                               VkSwapchainKHR               oldSwapchain,
                               const VkAllocationCallbacks* pAllocator,
                               VkSwapchainKHR*              pSwapchain)
{
    VkSurfaceCapabilitiesKHR      capabilities;
    VkSurfaceFormatKHR            surface_format;
    VkSurfaceTransformFlagBitsKHR transform;
    VkCompositeAlphaFlagBitsKHR   composite_alpha;
    VkBool32                      presentation_supported;
    VkBool32                      present_mode_supported;

    LOGASSERT(VK_NULL_HANDLE != physicalDevice);
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != surface);
    LOGASSERT(0 != minimumImageCount);
    LOGASSERT(VK_FORMAT_UNDEFINED != imageFormat);
    LOGASSERT(0 != imageExtent.width);
    LOGASSERT(0 != imageExtent.height);
    LOGASSERT(0 != imageArrayLayers);
    LOGASSERT((VkImageUsageFlags)0 != imageUsage);
    LOGASSERT(0 == queueFamilyIndexCount || NULL != pQueueFamilyIndices);
    LOGASSERT(NULL != pSwapchain);

    Gfx_Vulkan_GetPhysicalDeviceSurfaceSupport(physicalDevice, queueFamilyIndex, surface, &presentation_supported);
    LOGASSERT(VK_TRUE == presentation_supported);

    Gfx_Vulkan_GetPhysicalDeviceSurfaceCapabilities(physicalDevice, surface, &capabilities);
    LOGASSERT(imageUsage == (capabilities.supportedUsageFlags & imageUsage));
    LOGASSERT(imageArrayLayers <= capabilities.maxImageArrayLayers);
    LOGASSERT(capabilities.minImageCount <= minimumImageCount);
    LOGASSERT(0 == capabilities.maxImageCount || capabilities.maxImageCount >= minimumImageCount);

    Gfx_Vulkan_GetPhysicalDeviceSurfaceFormatProperties(physicalDevice, surface, imageFormat, &surface_format);
    Gfx_Vulkan_GetPhysicalDeviceSurfacePresentModeSupport(physicalDevice, surface, presentMode, &present_mode_supported);
    LOGASSERT(VK_TRUE == present_mode_supported);

    if (UINT32_MAX != capabilities.currentExtent.width)
    {
        LOGASSERT(imageExtent.width == capabilities.currentExtent.width);
        LOGASSERT(imageExtent.height == capabilities.currentExtent.height);
    }
    LOGASSERT(capabilities.minImageExtent.width <= imageExtent.width && capabilities.maxImageExtent.width >= imageExtent.width);
    LOGASSERT(capabilities.minImageExtent.height <= imageExtent.height && capabilities.maxImageExtent.height >= imageExtent.height);

    transform       = Gfx_Vulkan_GetSurfaceTransform(capabilities.supportedTransforms);
    composite_alpha = Gfx_Vulkan_GetSurfaceCompositeAlpha(capabilities.supportedCompositeAlpha);
    Gfx_Vulkan_CreateSwapchain(device,
                               pNext,
                               flags,
                               surface,
                               minimumImageCount,
                               surface_format.format,
                               surface_format.colorSpace,
                               imageExtent,
                               imageArrayLayers,
                               imageUsage,
                               imageSharingMode,
                               queueFamilyIndexCount,
                               pQueueFamilyIndices,
                               transform,
                               composite_alpha,
                               presentMode,
                               clipped,
                               oldSwapchain,
                               pAllocator,
                               pSwapchain);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage pSwapchainImages[VK_MAX_SWAPCHAIN_IMAGES])
{
    uint32_t swapchain_image_index;

    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != swapchain);
    LOGASSERT(NULL != pSwapchainImageCount);
    LOGASSERT(NULL != pSwapchainImages);

    (*pSwapchainImageCount) = 0;
    VKCHECK(vkGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, NULL));
    LOGASSERT(VK_MAX_SWAPCHAIN_IMAGES >= (*pSwapchainImageCount));

    VKCHECK(vkGetSwapchainImagesKHR(device, swapchain, pSwapchainImageCount, pSwapchainImages));
    LOGASSERT(0 != (*pSwapchainImageCount));
    for (swapchain_image_index = 0; swapchain_image_index < (*pSwapchainImageCount); ++swapchain_image_index)
    {
        LOGASSERT(VK_NULL_HANDLE != pSwapchainImages[swapchain_image_index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
    LOGASSERT(VK_NULL_HANDLE != device);
    LOGASSERT(VK_NULL_HANDLE != swapchain);
    LOGASSERT(VK_NULL_HANDLE != semaphore || VK_NULL_HANDLE != fence);
    LOGASSERT(NULL != pImageIndex);

    VKCHECK(vkAcquireNextImageKHR(device, swapchain, timeout, semaphore, fence, pImageIndex));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_QueuePresent(VkQueue               queue,
                             const void*           pNext,
                             uint32_t              waitSemaphoreCount,
                             const VkSemaphore*    pWaitSemaphores,
                             uint32_t              swapchainCount,
                             const VkSwapchainKHR* pSwapchains,
                             const uint32_t*       pImageIndices,
                             VkResult*             pResults)
{
    VkPresentInfoKHR present_info;

    LOGASSERT(VK_NULL_HANDLE != queue);
    LOGASSERT(0 == waitSemaphoreCount || NULL != pWaitSemaphores);
    LOGASSERT(0 != swapchainCount);
    LOGASSERT(NULL != pSwapchains);
    LOGASSERT(NULL != pImageIndices);

    present_info.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present_info.pNext              = pNext;
    present_info.waitSemaphoreCount = waitSemaphoreCount;
    present_info.pWaitSemaphores    = pWaitSemaphores;
    present_info.swapchainCount     = swapchainCount;
    present_info.pSwapchains        = pSwapchains;
    present_info.pImageIndices      = pImageIndices;
    present_info.pResults           = pResults;
    VKCHECK(vkQueuePresentKHR(queue, &present_info));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_QueuePresentImage(VkQueue queue, const void* pNext, uint32_t waitSemaphoreCount, const VkSemaphore* pWaitSemaphores, VkSwapchainKHR swapchain, uint32_t imageIndex, VkResult* pResult)
{
    Gfx_Vulkan_QueuePresent(queue, pNext, waitSemaphoreCount, pWaitSemaphores, 1, &swapchain, &imageIndex, pResult);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Gfx_Vulkan_GetPhysicalDeviceCompatibleMemoryTypeIndex(VkPhysicalDevice physicalDevice, uint32_t memoryTypeBits, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memory_properties;

    Gfx_Vulkan_GetPhysicalDeviceMemoryProperties(physicalDevice, &memory_properties);
    return Gfx_Vulkan_GetCompatibleMemoryTypeIndex(memory_properties.memoryTypeCount, memory_properties.memoryTypes, memoryTypeBits, properties);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetImageMemoryAllocationInfo(VkPhysicalDevice      physicalDevice,
                                             VkDevice              device,
                                             VkImage               image,
                                             uint32_t              memoryTypeBits,
                                             VkMemoryPropertyFlags properties,
                                             VkDeviceSize*         pAllocationSize,
                                             uint32_t*             pMemoryTypeIndex)
{
    VkMemoryRequirements2 requirements;

    LOGASSERT(NULL != pAllocationSize);
    LOGASSERT(NULL != pMemoryTypeIndex);

    Gfx_Vulkan_GetImageMemoryRequirementsInfo2(device, NULL, image, NULL, &requirements);
    (*pAllocationSize)  = requirements.memoryRequirements.size;
    (*pMemoryTypeIndex) = Gfx_Vulkan_GetPhysicalDeviceCompatibleMemoryTypeIndex(physicalDevice, requirements.memoryRequirements.memoryTypeBits & memoryTypeBits, properties);
    LOGASSERT(VK_INVALID != (*pMemoryTypeIndex));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vulkan_GetBufferMemoryAllocationInfo(VkPhysicalDevice      physicalDevice,
                                              VkDevice              device,
                                              VkBuffer              buffer,
                                              uint32_t              memoryTypeBits,
                                              VkMemoryPropertyFlags properties,
                                              VkDeviceSize*         pAllocationSize,
                                              uint32_t*             pMemoryTypeIndex)
{
    VkMemoryRequirements requirements;

    LOGASSERT(NULL != pAllocationSize);
    LOGASSERT(NULL != pMemoryTypeIndex);

    Gfx_Vulkan_GetBufferMemoryRequirements(device, buffer, &requirements);
    (*pAllocationSize)  = requirements.size;
    (*pMemoryTypeIndex) = Gfx_Vulkan_GetPhysicalDeviceCompatibleMemoryTypeIndex(physicalDevice, requirements.memoryTypeBits & memoryTypeBits, properties);
    LOGASSERT(VK_INVALID != (*pMemoryTypeIndex));
}
