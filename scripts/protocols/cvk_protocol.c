/* Tier 3 protocols (SPEC.md section 8b): source of truth kept with the generator and folded
** into the generated cvk.h/.c. Composed from the generated surface only. */

#include <stdio.h>

/**
 * \brief VK_EXT_debug_utils callback that prints one message to stderr and never aborts the call.
 *
 * \par Details:
 *  Prints the severity and type masks in hexadecimal, the message identifier name when present,
 *  and the message text. Suitable as pfnUserCallback for cvkCreateDebugInstance.
 *
 * \param[in] messageSeverity  Severity bit of the message
 * \param[in] messageTypes     General, validation, and performance type mask
 * \param[in] pCallbackData    Message identifier and text supplied by the loader, or NULL
 * \param[in] pUserData        Unused application context
 *
 * \retval VK_FALSE The triggering Vulkan call is never aborted.
 */
VKAPI_ATTR VkBool32 VKAPI_CALL cvkWriteDebugUtilsMessage(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                         VkDebugUtilsMessageTypeFlagsEXT             messageTypes,
                                                         const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                         void*                                       pUserData)
{
    const char* message_id_name;
    const char* message;

    (void)pUserData;
    message_id_name = ((NULL != pCallbackData) && (NULL != pCallbackData->pMessageIdName)) ? pCallbackData->pMessageIdName : "";
    message         = ((NULL != pCallbackData) && (NULL != pCallbackData->pMessage)) ? pCallbackData->pMessage : "";
    (void)fprintf(stderr, "vulkan severity 0x%x type 0x%x %s: %s\n", (unsigned)messageSeverity, (unsigned)messageTypes, message_id_name, message);
    return VK_FALSE;
}

/**
 * \brief Creates an instance with VK_EXT_debug_utils enabled and a messenger receiving every message, creation included.
 *
 * \par Details:
 *  Appends VK_EXT_debug_utils to the requested extensions, asserts the instance supports every
 *  resulting extension, creates the instance with the messenger create info chained so that
 *  messages from instance creation itself are delivered, then creates the messenger that
 *  receives all later messages. The instance and messenger are the caller's to destroy.
 *
 * \param[in]  pNext                    Optional extension chain for VkInstanceCreateInfo, or NULL
 * \param[in]  flags                    VkInstanceCreateInfo::flags
 * \param[in]  pApplicationInfo         Optional application and engine metadata, or NULL
 * \param[in]  enabledLayerCount        Number of names in ppEnabledLayerNames
 * \param[in]  ppEnabledLayerNames      Layer names to enable, or NULL when the count is zero
 * \param[in]  enabledExtensionCount    Number of names in ppEnabledExtensionNames, below CVK_MAX_INSTANCE_EXTENSIONS
 * \param[in]  ppEnabledExtensionNames  Extension names to enable, or NULL when the count is zero
 * \param[in]  messageSeverity          Severities delivered to the callback
 * \param[in]  messageType              Types delivered to the callback
 * \param[in]  pfnUserCallback          Callback receiving the messages, for example cvkWriteDebugUtilsMessage
 * \param[in]  pUserData                Context passed to the callback
 * \param[in]  pAllocator               Optional allocation callbacks, or NULL
 * \param[out] pInstance                Receives the created VkInstance
 * \param[out] pMessenger               Receives the created VkDebugUtilsMessengerEXT
 *
 * \retval None
 */
void cvkCreateDebugInstance(const void*                          pNext,
                            VkInstanceCreateFlags                flags,
                            const VkApplicationInfo*             pApplicationInfo,
                            uint32_t                             enabledLayerCount,
                            const char* const*                   ppEnabledLayerNames,
                            uint32_t                             enabledExtensionCount,
                            const char* const*                   ppEnabledExtensionNames,
                            VkDebugUtilsMessageSeverityFlagsEXT  messageSeverity,
                            VkDebugUtilsMessageTypeFlagsEXT      messageType,
                            PFN_vkDebugUtilsMessengerCallbackEXT pfnUserCallback,
                            void*                                pUserData,
                            const VkAllocationCallbacks*         pAllocator,
                            VkInstance*                          pInstance,
                            VkDebugUtilsMessengerEXT*            pMessenger)
{
    const char* extensions[CVK_MAX_INSTANCE_EXTENSIONS];
    uint32_t    extension_count;
    uint32_t    extension_index;
    VkBool32    supported;

    /* the debug utilities extension joins the requested set */
    CVK_ASSERT(CVK_MAX_INSTANCE_EXTENSIONS > enabledExtensionCount);
    for (extension_index = 0; extension_index < enabledExtensionCount; ++extension_index)
    {
        extensions[extension_index] = ppEnabledExtensionNames[extension_index];
    }
    extension_count               = enabledExtensionCount;
    extensions[extension_count++] = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;

    /* capability: every extension must be supported; nothing is dropped silently */
    supported = cvkGetInstanceExtensionSupport(NULL, extension_count, extensions);
    CVK_ASSERT(supported);

    /* create with the messenger info chained, so creation itself is reported */
    cvkCreateInstanceWithDebugUtilsMessengerInfo(pNext,
                                                 0,
                                                 messageSeverity,
                                                 messageType,
                                                 pfnUserCallback,
                                                 pUserData,
                                                 flags,
                                                 pApplicationInfo,
                                                 enabledLayerCount,
                                                 ppEnabledLayerNames,
                                                 extension_count,
                                                 extensions,
                                                 pAllocator,
                                                 pInstance);

    /* the messenger for every later message */
    cvkCreateDebugUtilsMessenger((*pInstance), NULL, 0, messageSeverity, messageType, pfnUserCallback, pUserData, pAllocator, pMessenger);
}

/**
 * \brief Finds the physical device whose locally unique identifier matches.
 *
 * \par Details:
 *  Enumerates the instance's physical devices and compares each device's LUID from
 *  VkPhysicalDeviceIDProperties when the device reports one as valid.
 *
 * \param[in] instance  Instance whose physical devices are searched
 * \param[in] pLuid     Locally unique identifier of VK_LUID_SIZE bytes
 *
 * \return The matching physical device, or VK_NULL_HANDLE when none matches.
 */
VkPhysicalDevice cvkGetPhysicalDeviceByLuid(VkInstance instance, const uint8_t pLuid[VK_LUID_SIZE])
{
    VkPhysicalDevice             physical_devices[CVK_MAX_PHYSICAL_DEVICES];
    VkPhysicalDeviceIDProperties physical_device_id_properties;
    uint32_t                     physical_device_count;
    uint32_t                     physical_device_index;

    physical_device_count = cvkEnumeratePhysicalDevices(instance, physical_devices);
    for (physical_device_index = 0; physical_device_index < physical_device_count; ++physical_device_index)
    {
        cvkGetPhysicalDeviceIDProperties(physical_devices[physical_device_index], NULL, &physical_device_id_properties);
        if (physical_device_id_properties.deviceLUIDValid && (0 == memcmp(physical_device_id_properties.deviceLUID, pLuid, VK_LUID_SIZE)))
        {
            return physical_devices[physical_device_index];
        }
    }
    return VK_NULL_HANDLE;
}

/**
 * \brief Finds the physical device at a PCI address.
 *
 * \par Details:
 *  Enumerates the instance's physical devices and, for each device supporting
 *  VK_EXT_pci_bus_info, compares its domain, bus, device, and function.
 *
 * \param[in] instance     Instance whose physical devices are searched
 * \param[in] pciDomain    PCI domain of the adapter
 * \param[in] pciBus       PCI bus of the adapter
 * \param[in] pciDevice    PCI device of the adapter
 * \param[in] pciFunction  PCI function of the adapter
 *
 * \return The matching physical device, or VK_NULL_HANDLE when none matches.
 */
VkPhysicalDevice cvkGetPhysicalDeviceByPciBusInfo(VkInstance instance, uint32_t pciDomain, uint32_t pciBus, uint32_t pciDevice, uint32_t pciFunction)
{
    static const char* const                pci_bus_info_extension = VK_EXT_PCI_BUS_INFO_EXTENSION_NAME;
    VkPhysicalDevice                        physical_devices[CVK_MAX_PHYSICAL_DEVICES];
    VkPhysicalDevicePCIBusInfoPropertiesEXT physical_device_pci_bus_info_properties;
    uint32_t                                physical_device_count;
    uint32_t                                physical_device_index;

    physical_device_count = cvkEnumeratePhysicalDevices(instance, physical_devices);
    for (physical_device_index = 0; physical_device_index < physical_device_count; ++physical_device_index)
    {
        if (!cvkGetPhysicalDeviceExtensionSupport(physical_devices[physical_device_index], NULL, 1, &pci_bus_info_extension))
        {
            continue;
        }
        cvkGetPhysicalDevicePCIBusInfoProperties(physical_devices[physical_device_index], NULL, &physical_device_pci_bus_info_properties);
        if ((pciDomain == physical_device_pci_bus_info_properties.pciDomain) && (pciBus == physical_device_pci_bus_info_properties.pciBus) &&
            (pciDevice == physical_device_pci_bus_info_properties.pciDevice) && (pciFunction == physical_device_pci_bus_info_properties.pciFunction))
        {
            return physical_devices[physical_device_index];
        }
    }
    return VK_NULL_HANDLE;
}
