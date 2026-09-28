#ifndef VULKAN_VA_ZERO_COPY_GFX_VULKAN_H
#define VULKAN_VA_ZERO_COPY_GFX_VULKAN_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#include "utl/utl_defines.h"
#include <volk.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#ifndef VK_VERSION_1_2
#error gfx_vulkan requires Vulkan 1.2 headers
#endif

#define VKCHECK(expression) (Gfx_Vulkan_CheckError((expression))) ///< Makes every non-success VkResult fatal.
#define VK_COUNT_OF(array)  ((uint32_t)COUNT_OF(array))           ///< Array element count in Vulkan's count type.

#define VK_INVALID                        (UINT32_MAX) ///< Sentinel for an unavailable index.
#define VK_INFINITE                       (UINT64_MAX) ///< Timeout value for an unbounded Vulkan wait.
#define VK_NO_OFFSET                      (0U)   ///< Zero byte offset for whole-allocation operations.
#define VK_MAX_PHYSICAL_DEVICES           (32U)  ///< Local physical-device enumeration capacity.
#define VK_MAX_QUEUE_FAMILIES             (64U)  ///< Local queue-family enumeration capacity.
#define VK_MAX_INSTANCE_EXTENSIONS        (64U)  ///< Local instance-extension enumeration capacity.
#define VK_MAX_DEVICE_EXTENSIONS          (512U) ///< Local device-extension enumeration capacity.
#define VK_MAX_DESCRIPTOR_SET_BINDINGS    (64U)  ///< Maximum bindings accepted by descriptor helpers.
#define VK_MAX_DRM_FORMAT_MODIFIER_PLANES (4U)   ///< Maximum DRM modifier plane count supported here.
#define VK_MAX_DRM_FORMAT_MODIFIERS       (64U)  ///< Maximum DRM modifiers enumerated for one format.
#define VK_MAX_DISPLAYS                   (32U)  ///< Local Vulkan display enumeration capacity.
#define VK_MAX_DISPLAY_MODES              (64U)  ///< Local display-mode enumeration capacity.
#define VK_MAX_DISPLAY_PLANES             (32U)  ///< Local display-plane enumeration capacity.
#define VK_MAX_SURFACE_FORMATS            (64U)  ///< Local surface-format enumeration capacity.
#define VK_MAX_PRESENT_MODES              (16U)  ///< Local presentation-mode enumeration capacity.
#define VK_MAX_SWAPCHAIN_IMAGES           (16U)  ///< Local KHR swapchain-image enumeration capacity.
#define VK_MAX_INSTANCE_LAYERS            (64U)  ///< Local instance-layer enumeration capacity.
#define VK_MAX_INSTANCE_EXTENSIONS        (64U)  ///< Local instance-extension enumeration capacity.
#define VK_MAX_DEVICE_LAYERS              (64U)  ///< Local device-layer enumeration capacity.
#define VK_MAX_DEVICE_EXTENSIONS          (512U) ///< Local device-extension enumeration capacity.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

extern const VkComponentMapping c_vkIdentityComponentMapping; ///< Identity RGBA image-view swizzle.

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Makes any VkResult other than VK_SUCCESS a fatal error.
 *
 * \param[in] result              Result returned by a Vulkan command
 *
 * \retval    None
 */
void Gfx_Vulkan_CheckError(VkResult result);

/*************************************************************************************************/
/**
 * \brief Selects the repository's Vulkan allocation policy.
 *
 * \param[out] ppAllocator        Receives NULL because custom allocation is unsupported
 *
 * \retval    None
 */
void Gfx_Vulkan_InitAllocator(const VkAllocationCallbacks** ppAllocator);

/*************************************************************************************************/
/**
 * \brief Resolves a required Vulkan instance or global command.
 *
 * \param[in] instance            Instance used for lookup, or VK_NULL_HANDLE for global commands
 * \param[in] pName               Non-NULL, null-terminated command name
 *
 * \return Non-NULL function pointer; an unavailable command is fatal.
 */
PFN_vkVoidFunction Gfx_Vulkan_GetInstanceProcAddr(VkInstance instance, const char* pName);

/*************************************************************************************************/
/**
 * \brief Resolves a device command by name.
 *
 * \param[in] device              Non-null logical device used for lookup
 * \param[in] pName               Non-null, null-terminated command name
 * \return Non-null function pointer; an unavailable command is fatal.
 */
PFN_vkVoidFunction Gfx_Vulkan_GetDeviceProcAddr(VkDevice device, const char* pName);

/*************************************************************************************************/
/**
 * \brief Creates a persistent Vulkan instance with caller-supplied application metadata.
 *
 * \par Details:
 *  Layer and extension arrays must contain their declared counts. Every supplied string must remain
 *  valid through the call. pApplicationInfo may be NULL. The returned instance is retained for process lifetime.
 *
 * \param[in]  pNext                      Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags                      Instance creation flags
 * \param[in]  pApplicationInfo           Optional application and engine metadata, or NULL
 * \param[in]  enabledLayerCount          Number of names in ppEnabledLayerNames
 * \param[in]  ppEnabledLayerNames        Array of enabled layer names containing enabledLayerCount entries, or NULL when the count is zero
 * \param[in]  enabledExtensionCount      Number of names in ppEnabledExtensionNames
 * \param[in]  ppEnabledExtensionNames    Array of enabled extension names containing enabledExtensionCount entries, or NULL when the count is zero
 * \param[in]  pAllocator                 Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pInstance                  Receives the created Vulkan instance
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateInstance(const void*                  pNext,
                               VkInstanceCreateFlags        flags,
                               const VkApplicationInfo*     pApplicationInfo,
                               uint32_t                     enabledLayerCount,
                               const char* const*           ppEnabledLayerNames,
                               uint32_t                     enabledExtensionCount,
                               const char* const*           ppEnabledExtensionNames,
                               const VkAllocationCallbacks* pAllocator,
                               VkInstance*                  pInstance);

/*************************************************************************************************/
/**
 * \brief Optional callback that prints one VK_EXT_debug_utils record to stderr.
 *
 * \param[in] messageSeverity     Vulkan severity bit included in hexadecimal output
 * \param[in] messageTypes        General, validation, and/or performance type mask
 * \param[in] pCallbackData       Optional message ID, number, and text supplied by the loader
 * \param[in] pUserData           Unused application callback context
 *
 * \retval VK_FALSE The triggering Vulkan call is never aborted.
 */
VKAPI_ATTR VkBool32 VKAPI_CALL Gfx_Vulkan_WriteDebugUtilsMessage(VkDebugUtilsMessageSeverityFlagBitsEXT      messageSeverity,
                                                                 VkDebugUtilsMessageTypeFlagsEXT             messageTypes,
                                                                 const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
                                                                 void*                                       pUserData);

/*************************************************************************************************/
/**
 * \brief Optional callback that prints one legacy VK_EXT_debug_report record to stderr.
 *
 * \param[in] flags               Debug-report severity/type flags printed in hexadecimal
 * \param[in] objectType          Vulkan object category associated with the report
 * \param[in] object              Raw object handle value
 * \param[in] location            Layer-defined message location
 * \param[in] messageCode         Layer-defined diagnostic code
 * \param[in] pLayerPrefix        Optional reporting layer name
 * \param[in] pMessage            Optional diagnostic text
 * \param[in] pUserData           Unused application callback context
 *
 * \retval VK_FALSE The triggering Vulkan call is never aborted.
 */
VKAPI_ATTR VkBool32 VKAPI_CALL Gfx_Vulkan_WriteDebugReportMessage(VkDebugReportFlagsEXT      flags,
                                                                  VkDebugReportObjectTypeEXT objectType,
                                                                  uint64_t                   object,
                                                                  size_t                     location,
                                                                  int32_t                    messageCode,
                                                                  const char*                pLayerPrefix,
                                                                  const char*                pMessage,
                                                                  void*                      pUserData);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VK_EXT_debug_utils messenger with caller-selected filters.
 *
 * \par Details:
 *  The extension must be enabled on instance; pfnCallback is invoked synchronously by Vulkan.
 *
 * \param[in]  instance           Vulkan instance owning the queried or created objects
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags              Debug utils messenger creation flags
 * \param[in]  messageSeverity    Severity classes delivered to pfnCallback
 * \param[in]  messageType        Message categories delivered to pfnCallback
 * \param[in]  pfnCallback        Non-NULL diagnostic callback
 * \param[in]  pUserData          Opaque callback context supplied when the callback was created
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pMessenger         Receives the created debug-utils messenger
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateDebugUtilsMessenger(VkInstance                           instance,
                                          const void*                          pNext,
                                          VkDebugUtilsMessengerCreateFlagsEXT  flags,
                                          VkDebugUtilsMessageSeverityFlagsEXT  messageSeverity,
                                          VkDebugUtilsMessageTypeFlagsEXT      messageType,
                                          PFN_vkDebugUtilsMessengerCallbackEXT pfnCallback,
                                          void*                                pUserData,
                                          const VkAllocationCallbacks*         pAllocator,
                                          VkDebugUtilsMessengerEXT*            pMessenger);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VK_EXT_debug_report callback for legacy validation output.
 *
 * \par Details:
 *  The extension must be enabled on instance and pfnCallback must be non-NULL.
 *
 * \param[in]  instance           Vulkan instance owning the queried or created objects
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags              Message classes delivered to pfnCallback
 * \param[in]  pfnCallback        Non-NULL diagnostic callback
 * \param[in]  pUserData          Opaque callback context supplied when the callback was created
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pCallback          Receives the created legacy debug-report callback
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateDebugReportCallback(VkInstance                   instance,
                                          const void*                  pNext,
                                          VkDebugReportFlagsEXT        flags,
                                          PFN_vkDebugReportCallbackEXT pfnCallback,
                                          void*                        pUserData,
                                          const VkAllocationCallbacks* pAllocator,
                                          VkDebugReportCallbackEXT*    pCallback);

/*************************************************************************************************/
/**
 * \brief Creates a Vulkan instance with the preferred supported diagnostic callback.
 *
 * \par Details:
 *  VK_EXT_debug_report is preferred. VK_EXT_debug_utils is used as a fallback. When neither
 *  extension is supported, the instance is created without a diagnostic callback.
 *
 * \param[in]  pNext                       Optional extension chain attached after the diagnostic create info, or NULL
 * \param[in]  instanceFlags               Flags controlling instance creation
 * \param[in]  pApplicationInfo            Optional application and engine metadata, or NULL
 * \param[in]  enabledLayerCount           Number of names in ppEnabledLayerNames
 * \param[in]  ppEnabledLayerNames         Array of enabled layer names containing enabledLayerCount entries, or NULL when the count is zero
 * \param[in]  enabledExtensionCount       Number of names in ppEnabledExtensionNames
 * \param[in]  ppEnabledExtensionNames     Array of enabled extension names containing enabledExtensionCount entries, or NULL when the count is zero
 * \param[in]  debugUtilsFlags              Flags controlling debug-utils messenger creation
 * \param[in]  debugUtilsMessageSeverity   Debug-utils message severities to forward
 * \param[in]  debugUtilsMessageType       Debug-utils message types to forward
 * \param[in]  debugReportFlags            Legacy debug-report message classes to forward
 * \param[in]  pAllocator                  Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pInstance                   Receives the created Vulkan instance
 *
 * \retval    None
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
                                    VkInstance*                         pInstance);

/*************************************************************************************************/
/**
 * \brief Gets instance extensions in fixed-capacity caller storage.
 *
 * \param[in]  pLayerName          Optional layer name that scopes enumeration, or NULL for implementation-wide properties
 * \param[out] pPropertyCount      Receives the number of initialized properties
 * \param[out] pProperties         Receives up to VK_MAX_INSTANCE_EXTENSIONS properties
 *
 * \retval    None
 */
void Gfx_Vulkan_GetInstanceExtensionProperties(const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties pProperties[VK_MAX_INSTANCE_EXTENSIONS]);

/*************************************************************************************************/
/**
 * \brief Tests an enumerated instance-extension array for one exact extension name.
 *
 * \param[in] propertyCount             Number of valid entries in pProperties
 * \param[in] pProperties               Enumerated instance-extension properties
 * \param[in] pRequiredExtensionName    Non-null, null-terminated extension name to find
 *
 * \retval VK_TRUE  A matching property exists.
 * \retval VK_FALSE The required extension is absent.
 */
VkBool32 Gfx_Vulkan_GetInstanceExtensionSupport(uint32_t propertyCount, const VkExtensionProperties* pProperties, const char* pRequiredExtensionName);

/*************************************************************************************************/
/**
 * \brief Gets physical devices from a Vulkan instance in implementation order.
 *
 * \param[in]  instance                Vulkan instance owning the queried or created objects
 * \param[out] pPhysicalDeviceCount    Receives the number of initialized physical-device handles
 * \param[out] pPhysicalDevices        Receives up to VK_MAX_PHYSICAL_DEVICES enumerated physical-device handles
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDevices(VkInstance instance, uint32_t* pPhysicalDeviceCount, VkPhysicalDevice pPhysicalDevices[VK_MAX_PHYSICAL_DEVICES]);

/*************************************************************************************************/
/**
 * \brief Gets extensions exposed by one physical device or layer.
 *
 * \param[in]  physicalDevice      Non-null physical device queried or used to select compatible resources
 * \param[in]  pLayerName          Optional layer name that scopes enumeration, or NULL for implementation-wide properties
 * \param[out] pPropertyCount      Receives the number of initialized properties
 * \param[out] pProperties         Receives up to VK_MAX_DEVICE_EXTENSIONS properties
 *
 * \retval    None
 */
void Gfx_Vulkan_GetDeviceExtensionProperties(VkPhysicalDevice physicalDevice, const char* pLayerName, uint32_t* pPropertyCount, VkExtensionProperties pProperties[VK_MAX_DEVICE_EXTENSIONS]);

/*************************************************************************************************/
/**
 * \brief Tests an enumerated extension array for one exact extension name.
 *
 * \param[in] propertyCount             Number of valid entries in pProperties
 * \param[in] pProperties               Array of propertyCount enumerated device-extension properties
 * \param[in] pRequiredExtensionName    Non-null, null-terminated extension name to find
 *
 * \retval VK_TRUE                A matching property exists
 * \retval VK_FALSE               The required extension is absent
 */
VkBool32 Gfx_Vulkan_GetDeviceExtensionSupport(uint32_t propertyCount, const VkExtensionProperties* pProperties, const char* pRequiredExtensionName);

/*************************************************************************************************/
/**
 * \brief Tests whether every name in a required-extension array is present.
 *
 * \param[in] propertyCount               Number of valid entries in pProperties
 * \param[in] pProperties                 Array of propertyCount enumerated device-extension properties
 * \param[in] requiredExtensionCount      Number of names in ppRequiredExtensionNames
 * \param[in] ppRequiredExtensionNames    Array of required extension names containing requiredExtensionCount entries, or NULL when the count is zero
 *
 * \retval VK_TRUE                All required extensions are present
 * \retval VK_FALSE               At least one required extension is absent
 */
VkBool32 Gfx_Vulkan_GetDeviceExtensionsSupport(uint32_t propertyCount, const VkExtensionProperties* pProperties, uint32_t requiredExtensionCount, const char* const* ppRequiredExtensionNames);

/*************************************************************************************************/
/**
 * \brief Populates VkPhysicalDeviceProperties2 and its caller-provided output chain.
 *
 * \par Details:
 *  pNext is an optional writable output chain.
 *
 * \param[in]     physicalDevice Non-null physical device queried or used to select compatible resources
 * \param[in,out] pNext          Optional writable extension chain attached to the queried Vulkan output structure, or NULL
 * \param[out]    pProperties    Receives the physical-device properties and output-chain header
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceProperties2(VkPhysicalDevice physicalDevice, void* pNext, VkPhysicalDeviceProperties2* pProperties);

/*************************************************************************************************/
/**
 * \brief Populates stable device UUID and LUID identity properties.
 *
 * \param[in]     physicalDevice Non-null physical device queried or used to select compatible resources
 * \param[in,out] pNext          Optional writable extension chain attached to the queried Vulkan output structure, or NULL
 * \param[out]    pProperties    Receives device/driver UUIDs and any valid device LUID
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceIdProperties(VkPhysicalDevice physicalDevice, void* pNext, VkPhysicalDeviceIDProperties* pProperties);

/*************************************************************************************************/
/**
 * \brief Retrieves a Vulkan physical device's LUID when available.
 *
 * \param[in]  physicalDevice        Vulkan physical device to query
 * \param[out] pDeviceLuid           Receives VK_LUID_SIZE bytes on success; unchanged when no LUID is available
 *
 * \return VK_TRUE when the LUID is available, otherwise VK_FALSE.
 */
VkBool32 Gfx_Vulkan_GetPhysicalDeviceLuid(VkPhysicalDevice physicalDevice, uint8_t pDeviceLuid[VK_LUID_SIZE]);

/*************************************************************************************************/
/**
 * \brief Populates VK_EXT_pci_bus_info domain, bus, device, and function identity.
 *
 * \param[in]     physicalDevice Non-null physical device queried or used to select compatible resources
 * \param[in,out] pNext          Optional writable extension chain attached to the queried Vulkan output structure, or NULL
 * \param[out]    pProperties    Receives PCI domain, bus, device, and function values
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDevicePciBusInfoProperties(VkPhysicalDevice physicalDevice, void* pNext, VkPhysicalDevicePCIBusInfoPropertiesEXT* pProperties);

/*************************************************************************************************/
/**
 * \brief Gets the Vulkan physical device matching a platform adapter and application requirements.
 *
 * \par Details:
 *  Matches the adapter LUID when exposed by Vulkan. Otherwise, matches the packed PCI domain, bus,
 *  device, and function identity after requiring VK_EXT_pci_bus_info.
 *
 * \param[in] instance                    Non-null Vulkan instance used to enumerate physical devices
 * \param[in] adapterId                   Adapter LUID bits or packed PCI identity
 * \param[in] minimumApiVersion           Minimum Vulkan API version accepted by the caller
 * \param[in] requiredExtensionCount      Number of names in ppRequiredExtensions
 * \param[in] ppRequiredExtensions        Required device-extension names, or NULL when the count is zero
 *
 * \return Matching physical device; absence is fatal.
 */
VkPhysicalDevice Gfx_Vulkan_GetPhysicalDevice(VkInstance instance, uint64_t adapterId, uint32_t minimumApiVersion, uint32_t requiredExtensionCount, const char* const* ppRequiredExtensions);

/*************************************************************************************************/
/**
 * \brief Enumerates queue-family properties into fixed-capacity caller storage.
 *
 * \param[in]  physicalDevice            Non-null physical device queried or used to select compatible resources
 * \param[out] pQueueFamilyProperties    Receives up to VK_MAX_QUEUE_FAMILIES queue-family property records
 *
 * \return Number of initialized properties, never exceeding VK_MAX_QUEUE_FAMILIES.
 */
uint32_t Gfx_Vulkan_GetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice physicalDevice, VkQueueFamilyProperties pQueueFamilyProperties[VK_MAX_QUEUE_FAMILIES]);

/*************************************************************************************************/
/**
 * \brief Returns linear, optimal, and buffer feature support for one Vulkan format.
 *
 * \param[in]  physicalDevice       Non-null physical device queried or used to select compatible resources
 * \param[in]  format               Vulkan format whose layout or capabilities are requested
 * \param[out] pFormatProperties    Receives linear-tiling, optimal-tiling, and buffer format features
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties);

/*************************************************************************************************/
/**
 * \brief Selects the first preferred surface format with the required optimal-tiling features.
 *
 * \par Details:
 *  A single VK_FORMAT_UNDEFINED surface entry accepts every preferred format. Absence of a
 *  compatible format is fatal.
 *
 * \param[in] physicalDevice                  Physical device queried for format capabilities
 * \param[in] surface                         Presentation surface queried for supported formats
 * \param[in] preferredFormatCount            Number of formats in pPreferredFormats
 * \param[in] pPreferredFormats               Formats in descending client preference order
 * \param[in] requiredOptimalTilingFeatures   Features every selected optimal-tiled image must support
 *
 * \return Selected entry from pPreferredFormats.
 */
VkFormat Gfx_Vulkan_SelectPhysicalDeviceSurfaceFormat(VkPhysicalDevice     physicalDevice,
                                                      VkSurfaceKHR         surface,
                                                      uint32_t             preferredFormatCount,
                                                      const VkFormat*      pPreferredFormats,
                                                      VkFormatFeatureFlags requiredOptimalTilingFeatures);

/*************************************************************************************************/
/**
 * \brief Populates extensible format properties for one Vulkan format.
 *
 * \par Details:
 *  pNext is an optional writable output chain.
 *
 * \param[in]     physicalDevice    Non-null physical device queried for format support
 * \param[in]     format            Vulkan format whose capabilities are requested
 * \param[in,out] pNext             Optional writable output extension chain, or NULL
 * \param[out]    pFormatProperties Receives the extensible format properties
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice, VkFormat format, void* pNext, VkFormatProperties2* pFormatProperties);

/*************************************************************************************************/
/**
 * \brief Queries extensible image-format properties from a complete input description.
 *
 * \par Details:
 *  A failed query zeroes pImageFormatProperties. pPropertiesNext is an optional writable output chain.
 *
 * \param[in]     physicalDevice         Non-null physical device queried for image support
 * \param[in]     pInfo                  Complete image-format query input
 * \param[in,out] pPropertiesNext        Optional writable output extension chain, or NULL
 * \param[out]    pImageFormatProperties Receives the extensible image-format properties on
 *                                         success, or a zeroed structure on failure
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice                        physicalDevice,
                                                        const VkPhysicalDeviceImageFormatInfo2* pInfo,
                                                        void*                                   pPropertiesNext,
                                                        VkImageFormatProperties2*               pImageFormatProperties);

/*************************************************************************************************/
/**
 * \brief Queries image-format properties from individual image parameters.
 *
 * \param[in]     physicalDevice         Non-null physical device queried for image support
 * \param[in]     pNext                  Optional image-format input chain, or NULL
 * \param[in]     flags                  Image creation flags
 * \param[in]     imageType              Image dimensionality
 * \param[in]     format                 Vulkan image format
 * \param[in]     tiling                 Image tiling mode
 * \param[in]     usage                  Image usage mask
 * \param[in,out] pPropertiesNext        Optional writable output extension chain, or NULL
 * \param[out]    pImageFormatProperties Receives the extensible image-format properties on
 *                                         success, or a zeroed structure on failure
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceImageFormatInfo2(VkPhysicalDevice          physicalDevice,
                                                  const void*               pNext,
                                                  VkImageCreateFlags        flags,
                                                  VkImageType               imageType,
                                                  VkFormat                  format,
                                                  VkImageTiling             tiling,
                                                  VkImageUsageFlags         usage,
                                                  void*                     pPropertiesNext,
                                                  VkImageFormatProperties2* pImageFormatProperties);

/*************************************************************************************************/
/**
 * \brief Retrieves external-memory properties for an image configuration.
 *
 * \par Details:
 *  Unsupported configurations produce zeroed external-memory properties.
 *
 * \param[in]  physicalDevice               Non-null physical device queried for image support
 * \param[in]  pNext                        Optional image-format input chain, or NULL
 * \param[in]  flags                        Image creation flags
 * \param[in]  imageType                    Image dimensionality
 * \param[in]  format                       Vulkan image format
 * \param[in]  tiling                       Image tiling mode
 * \param[in]  usage                        Image usage mask
 * \param[out] pExternalMemoryProperties    Receives external-memory properties, or zeroed properties
 *                                          when the image configuration is unsupported
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceImageMemoryProperties(VkPhysicalDevice            physicalDevice,
                                                       const void*                 pNext,
                                                       VkImageCreateFlags          flags,
                                                       VkImageType                 imageType,
                                                       VkFormat                    format,
                                                       VkImageTiling               tiling,
                                                       VkImageUsageFlags           usage,
                                                       VkExternalMemoryProperties* pExternalMemoryProperties);

/*************************************************************************************************/
/**
 * \brief Queries external-memory properties for an exact image configuration.
 *
 * \param[in]  physicalDevice               Non-null physical device queried for image support
 * \param[in]  pNext                        Optional image-format input chain, or NULL
 * \param[in]  flags                        Image creation flags
 * \param[in]  imageType                    Image dimensionality
 * \param[in]  format                       Vulkan image format
 * \param[in]  tiling                       Image tiling mode
 * \param[in]  usage                        Image usage mask
 * \param[in]  handleType                   External-memory handle type
 * \param[out] pExternalMemoryProperties    Receives external-memory properties, or zeroed properties when unsupported
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceExternalImageMemoryProperties(VkPhysicalDevice                   physicalDevice,
                                                               const void*                        pNext,
                                                               VkImageCreateFlags                 flags,
                                                               VkImageType                        imageType,
                                                               VkFormat                           format,
                                                               VkImageTiling                      tiling,
                                                               VkImageUsageFlags                  usage,
                                                               VkExternalMemoryHandleTypeFlagBits handleType,
                                                               VkExternalMemoryProperties*        pExternalMemoryProperties);

/*************************************************************************************************/
/**
 * \brief Reports whether an external-memory handle can be imported for an image configuration.
 *
 * \par Details:
 *  Requires both VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT and compatibility with the queried
 *  handle type.
 *
 * \param[in] physicalDevice    Non-null physical device queried for image support
 * \param[in] pNext             Optional image-format input chain, or NULL
 * \param[in] flags             Image creation flags
 * \param[in] imageType         Image dimensionality
 * \param[in] format            Vulkan image format
 * \param[in] tiling            Image tiling mode
 * \param[in] usage             Image usage mask
 * \param[in] handleType        External-memory handle type
 * \param[out] pDedicatedOnly   Receives TRUE when the configuration requires a dedicated allocation
 *
 * \retval TRUE                 The image configuration supports importing the handle type
 * \retval FALSE                The image configuration does not support importing the handle type
 */
bool_t Gfx_Vulkan_GetPhysicalDeviceExternalImageImportSupport(VkPhysicalDevice                   physicalDevice,
                                                              const void*                        pNext,
                                                              VkImageCreateFlags                 flags,
                                                              VkImageType                        imageType,
                                                              VkFormat                           format,
                                                              VkImageTiling                      tiling,
                                                              VkImageUsageFlags                  usage,
                                                              VkExternalMemoryHandleTypeFlagBits handleType,
                                                              bool_t*                            pDedicatedOnly);

/*************************************************************************************************/
/**
 * \brief Reports whether an external-memory handle can be exported for an image configuration.
 *
 * \par Details:
 *  Requires both VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT and compatibility with the queried
 *  handle type.
 *
 * \param[in] physicalDevice    Non-null physical device queried for image support
 * \param[in] pNext             Optional image-format input chain, or NULL
 * \param[in] flags             Image creation flags
 * \param[in] imageType         Image dimensionality
 * \param[in] format            Vulkan image format
 * \param[in] tiling            Image tiling mode
 * \param[in] usage             Image usage mask
 * \param[in] handleType        External-memory handle type
 * \param[out] pDedicatedOnly   Receives TRUE when the configuration requires a dedicated allocation
 *
 * \retval TRUE                 The image configuration supports exporting the handle type
 * \retval FALSE                The image configuration does not support exporting the handle type
 */
bool_t Gfx_Vulkan_GetPhysicalDeviceExternalImageExportSupport(VkPhysicalDevice                   physicalDevice,
                                                              const void*                        pNext,
                                                              VkImageCreateFlags                 flags,
                                                              VkImageType                        imageType,
                                                              VkFormat                           format,
                                                              VkImageTiling                      tiling,
                                                              VkImageUsageFlags                  usage,
                                                              VkExternalMemoryHandleTypeFlagBits handleType,
                                                              bool_t*                            pDedicatedOnly);

/*************************************************************************************************/
/**
 * \brief Issues one DRM format modifier properties-list query.
 *
 * \par Details:
 *  Pass zero and NULL for a count-only query, or the array capacity and storage for a fill query.
 *
 * \param[in]     physicalDevice               Non-null physical device queried for modifier support
 * \param[in,out] pNext                        Optional writable output extension chain, or NULL
 * \param[in]     format                       Vulkan format whose modifiers are requested
 * \param[in]     drmFormatModifierCount       Modifier array capacity, or zero for a count-only query
 * \param[out]    pDrmFormatModifierProperties Modifier array storage, or NULL for a count-only query
 * \param[out]    pProperties                  Receives the queried list with its resulting count
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceDrmFormatModifierPropertiesList(VkPhysicalDevice                      physicalDevice,
                                                                 void*                                 pNext,
                                                                 VkFormat                              format,
                                                                 uint32_t                              drmFormatModifierCount,
                                                                 VkDrmFormatModifierPropertiesEXT*     pDrmFormatModifierProperties,
                                                                 VkDrmFormatModifierPropertiesListEXT* pProperties);

/*************************************************************************************************/
/**
 * \brief Queries the DRM format modifiers supported for a Vulkan format.
 *
 * \par Details:
 *  The caller's array must hold VK_MAX_DRM_FORMAT_MODIFIERS entries. A larger reported count is fatal.
 *
 * \param[in]     physicalDevice Non-null physical device queried for modifier support
 * \param[in,out] pNext          Optional writable output extension chain, or NULL
 * \param[in]     format         Vulkan format whose modifiers are requested
 * \param[out]    pPropertyCount Receives the number of modifier properties written
 * \param[out]    pProperties    Receives properties in storage for VK_MAX_DRM_FORMAT_MODIFIERS entries
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceDrmFormatModifierProperties(VkPhysicalDevice                 physicalDevice,
                                                             void*                            pNext,
                                                             VkFormat                         format,
                                                             uint32_t*                        pPropertyCount,
                                                             VkDrmFormatModifierPropertiesEXT pProperties[VK_MAX_DRM_FORMAT_MODIFIERS]);

/*************************************************************************************************/
/**
 * \brief Finds one DRM format modifier in caller-supplied properties.
 *
 * \param[in]  propertyCount        Number of modifier properties to search
 * \param[in]  pProperties          Modifier properties, or NULL when propertyCount is zero
 * \param[in]  drmFormatModifier    Exact DRM modifier to find
 * \param[out] pModifierProperties  Receives matching properties; unchanged when no match exists
 *
 * \retval TRUE                    A matching modifier was found
 * \retval FALSE                   No matching modifier was found
 */
bool_t Gfx_Vulkan_GetDrmFormatModifierProperties(uint32_t propertyCount, const VkDrmFormatModifierPropertiesEXT* pProperties, uint64_t drmFormatModifier, VkDrmFormatModifierPropertiesEXT* pModifierProperties);

/*************************************************************************************************/
/**
 * \brief Returns all physical-device memory heaps and types.
 *
 * \param[in]  physicalDevice       Non-null physical device queried or used to select compatible resources
 * \param[out] pMemoryProperties    Receives every memory heap and memory type exposed by the physical device
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceMemoryProperties(VkPhysicalDevice physicalDevice, VkPhysicalDeviceMemoryProperties* pMemoryProperties);

/*************************************************************************************************/
/**
 * \brief Queries a physical device for the properties of an external-memory buffer configuration.
 *
 * \param[in]     physicalDevice  Valid physical device to query
 * \param[in]     pInfo           Complete external-buffer query with a valid sType, usage,
 *                                   and single external-memory handle type
 * \param[in,out] pPropertiesNext Optional writable output extension chain, or NULL
 * \param[out]    pProperties     Receives the external-memory compatibility, feature,
 *                                   and compatible-handle properties
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceBufferProperties(VkPhysicalDevice physicalDevice, const VkPhysicalDeviceExternalBufferInfo* pInfo, void* pPropertiesNext, VkExternalBufferProperties* pProperties);

/*************************************************************************************************/
/**
 * \brief Queries external-buffer properties from individual buffer parameters.
 *
 * \param[in]     physicalDevice  Valid physical device to query
 * \param[in]     pNext           Optional input extension chain attached to the composed
 *                                   VkPhysicalDeviceExternalBufferInfo, or NULL
 * \param[in]     flags           Buffer creation flags for the proposed buffer; zero is valid
 * \param[in]     usage           Nonzero usage mask for the proposed buffer
 * \param[in]     handleType      Single external-memory handle type to query
 * \param[in,out] pPropertiesNext Optional writable output extension chain attached to the
 *                                   returned VkExternalBufferProperties, or NULL
 * \param[out]    pProperties     Receives the external-buffer properties
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceExternalBufferProperties(VkPhysicalDevice                   physicalDevice,
                                                          const void*                        pNext,
                                                          VkBufferCreateFlags                flags,
                                                          VkBufferUsageFlags                 usage,
                                                          VkExternalMemoryHandleTypeFlagBits handleType,
                                                          void*                              pPropertiesNext,
                                                          VkExternalBufferProperties*        pProperties);

/*************************************************************************************************/
/**
 * \brief Returns external-memory properties for a proposed buffer configuration.
 *
 * \param[in]  physicalDevice               Valid physical device to query
 * \param[in]  pNext                        Optional input extension chain attached to the composed
 *                                          VkPhysicalDeviceExternalBufferInfo, or NULL
 * \param[in]  flags                        Buffer creation flags for the proposed buffer; zero is valid
 * \param[in]  usage                        Nonzero usage mask for the proposed buffer
 * \param[in]  handleType                   Single external-memory handle type to query
 * \param[out] pExternalMemoryProperties    Receives the external-memory properties
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceExternalBufferMemoryProperties(VkPhysicalDevice                   physicalDevice,
                                                                const void*                        pNext,
                                                                VkBufferCreateFlags                flags,
                                                                VkBufferUsageFlags                 usage,
                                                                VkExternalMemoryHandleTypeFlagBits handleType,
                                                                VkExternalMemoryProperties*        pExternalMemoryProperties);

/*************************************************************************************************/
/**
 * \brief Reports whether an external-memory handle can be imported for a buffer configuration.
 *
 * \par Details:
 *  Requires both VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT and compatibility with the queried
 *  handle type.
 *
 * \param[in]  physicalDevice     Valid physical device to query
 * \param[in]  pNext              Optional input extension chain, or NULL
 * \param[in]  flags              Buffer creation flags; zero is valid
 * \param[in]  usage              Nonzero buffer usage mask
 * \param[in]  handleType         Single external-memory handle type to query
 * \param[out] pDedicatedOnly     Receives TRUE when the configuration requires a dedicated allocation
 *
 * \retval TRUE                   The buffer configuration supports importing the handle type
 * \retval FALSE                  The buffer configuration does not support importing the handle type
 */
bool_t Gfx_Vulkan_GetPhysicalDeviceExternalBufferImportSupport(VkPhysicalDevice                   physicalDevice,
                                                               const void*                        pNext,
                                                               VkBufferCreateFlags                flags,
                                                               VkBufferUsageFlags                 usage,
                                                               VkExternalMemoryHandleTypeFlagBits handleType,
                                                               bool_t*                            pDedicatedOnly);

/*************************************************************************************************/
/**
 * \brief Reports whether an external-memory handle can be exported for a buffer configuration.
 *
 * \par Details:
 *  Requires both VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT and compatibility with the queried
 *  handle type.
 *
 * \param[in]  physicalDevice     Valid physical device to query
 * \param[in]  pNext              Optional input extension chain, or NULL
 * \param[in]  flags              Buffer creation flags; zero is valid
 * \param[in]  usage              Nonzero buffer usage mask
 * \param[in]  handleType         Single external-memory handle type to query
 * \param[out] pDedicatedOnly     Receives TRUE when the configuration requires a dedicated allocation
 *
 * \retval TRUE                   The buffer configuration supports exporting the handle type
 * \retval FALSE                  The buffer configuration does not support exporting the handle type
 */
bool_t Gfx_Vulkan_GetPhysicalDeviceExternalBufferExportSupport(VkPhysicalDevice                   physicalDevice,
                                                               const void*                        pNext,
                                                               VkBufferCreateFlags                flags,
                                                               VkBufferUsageFlags                 usage,
                                                               VkExternalMemoryHandleTypeFlagBits handleType,
                                                               bool_t*                            pDedicatedOnly);

/*************************************************************************************************/
/**
 * \brief Retrieves the external-memory feature mask for a proposed buffer configuration.
 *
 * \par Details:
 *  The returned mask reports importability, exportability, and any dedicated-allocation requirement.
 *
 * \param[in]  physicalDevice             Valid physical device to query
 * \param[in]  pNext                      Optional input extension chain attached to the composed
 *                                        VkPhysicalDeviceExternalBufferInfo, or NULL
 * \param[in]  flags                      Buffer creation flags for the proposed buffer; zero is valid
 * \param[in]  usage                      Nonzero usage mask for the proposed buffer
 * \param[in]  handleType                 Single external-memory handle type to query
 * \param[out] pExternalMemoryFeatures    Receives the supported external-memory feature flags
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceExternalBufferMemoryFeatures(VkPhysicalDevice                   physicalDevice,
                                                              const void*                        pNext,
                                                              VkBufferCreateFlags                flags,
                                                              VkBufferUsageFlags                 usage,
                                                              VkExternalMemoryHandleTypeFlagBits handleType,
                                                              VkExternalMemoryFeatureFlags*      pExternalMemoryFeatures);

/*************************************************************************************************/
/**
 * \brief Selects the first allowed memory type whose property flags exactly match the requested flags.
 *
 * \param[in] memoryTypeCount     Number of valid entries in pMemoryTypes
 * \param[in] pMemoryTypes        Physical-device memory-type array containing memoryTypeCount entries
 * \param[in] memoryTypeBits      Bit mask of memory types compatible with the resource
 * \param[in] properties          Required Vulkan memory-property flags
 *
 * \return Matching memory-type index, or VK_INVALID when no type matches.
 */
uint32_t Gfx_Vulkan_GetMemoryTypeIndex(uint32_t memoryTypeCount, const VkMemoryType* pMemoryTypes, uint32_t memoryTypeBits, VkMemoryPropertyFlags properties);

/*************************************************************************************************/
/**
 * \brief Selects the first compatible memory type containing every required property bit.
 *
 * \param[in] memoryTypeCount       Number of valid entries in pMemoryTypes
 * \param[in] pMemoryTypes          Physical-device memory-type array containing memoryTypeCount entries
 * \param[in] memoryTypeBits        Bit mask of memory types compatible with the resource
 * \param[in] requiredProperties    Memory-property bits that every selected type must contain
 *
 * \return Matching memory-type index, or VK_INVALID when no type matches.
 */
uint32_t Gfx_Vulkan_GetCompatibleMemoryTypeIndex(uint32_t memoryTypeCount, const VkMemoryType* pMemoryTypes, uint32_t memoryTypeBits, VkMemoryPropertyFlags requiredProperties);

/*************************************************************************************************/
/**
 * \brief Selects the first queue family containing every required capability bit.
 *
 * \param[in] propertyCount       Number of valid entries in pProperties
 * \param[in] pProperties         Array of propertyCount enumerated queue-family properties
 * \param[in] requiredFlags       Capability bits that the selected queue family must contain
 *
 * \return First nonempty queue family supporting requiredFlags, or VK_INVALID if none matches.
 */
uint32_t Gfx_Vulkan_GetQueueFamilyIndex(uint32_t propertyCount, const VkQueueFamilyProperties* pProperties, VkQueueFlags requiredFlags);

/*************************************************************************************************/
/**
 * \brief Creates a persistent logical device from complete Vulkan queue and extension arrays.
 *
 * \param[in]  physicalDevice             Non-null physical device queried or used to select compatible resources
 * \param[in]  pNext                      Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags                      Device creation flags
 * \param[in]  queueCreateInfoCount       Number of entries in pQueueCreateInfos
 * \param[in]  pQueueCreateInfos          Array of queue-create descriptions containing queueCreateInfoCount entries
 * \param[in]  enabledLayerCount          Number of names in ppEnabledLayerNames
 * \param[in]  ppEnabledLayerNames        Array of enabled layer names containing enabledLayerCount entries, or NULL when the count is zero
 * \param[in]  enabledExtensionCount      Number of names in ppEnabledExtensionNames
 * \param[in]  ppEnabledExtensionNames    Array of enabled extension names containing enabledExtensionCount entries, or NULL when the count is zero
 * \param[in]  pEnabledFeatures           Optional core physical-device feature set enabled on the logical device
 * \param[in]  pAllocator                 Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pDevice                    Receives the created logical device
 *
 * \retval    None
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
                             VkDevice*                       pDevice);

/*************************************************************************************************/
/**
 * \brief Creates a logical device after verifying every requested extension is supported.
 *
 * \par Details:
 *  Only supported requested extensions are enabled. Enabled device layers are forwarded unchanged.
 *  Calls must be serialized, including calls to SetupDeviceQueue, because extension scratch arrays are shared.
 *
 * \param[in]  physicalDevice               Non-null physical device queried or used to select compatible resources
 * \param[in]  pNext                        Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags                        Object creation flags
 * \param[in]  queueCreateInfoCount         Number of entries in pQueueCreateInfos
 * \param[in]  pQueueCreateInfos            Array of queue-create descriptions containing queueCreateInfoCount entries
 * \param[in]  enabledLayerCount            Number of names in ppEnabledLayerNames
 * \param[in]  ppEnabledLayerNames          Array of enabled device layer names containing enabledLayerCount entries, or NULL when the count is zero
 * \param[in]  requestedExtensionCount      Number of names in ppRequestedExtensionNames
 * \param[in]  ppRequestedExtensionNames    Array of requested extension names containing requestedExtensionCount entries, or NULL when the count is zero
 * \param[in]  pEnabledFeatures             Optional core physical-device feature set enabled on the logical device
 * \param[in]  pAllocator                   Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pDevice                      Receives the created logical device
 *
 * \retval    None
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
                            VkDevice*                       pDevice);

/*************************************************************************************************/
/**
 * \brief Creates a logical device and returns its single queue.
 *
 * \par Details:
 *  Creates queue zero in the selected family. Only supported requested extensions are enabled.
 *  Calls must be serialized, including calls to SetupDevice, because extension scratch arrays are shared.
 *
 * \param[in]  physicalDevice               Non-null physical device queried or used to select compatible resources
 * \param[in]  pNext                        Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags                        Object creation flags
 * \param[in]  pQueueNext                   Optional extension chain attached to the queue-create description, or NULL
 * \param[in]  queueFlags                   Queue creation flags
 * \param[in]  queueFamilyIndex             Queue-family index selected for the operation
 * \param[in]  queuePriority                Priority assigned to the composed device queue
 * \param[in]  enabledLayerCount            Number of names in ppEnabledLayerNames
 * \param[in]  ppEnabledLayerNames          Array of enabled device layer names containing enabledLayerCount entries, or NULL when the count is zero
 * \param[in]  requestedExtensionCount      Number of names in ppRequestedExtensionNames
 * \param[in]  ppRequestedExtensionNames    Array of requested extension names containing requestedExtensionCount entries, or NULL when the count is zero
 * \param[in]  pEnabledFeatures             Optional core physical-device feature set enabled on the logical device
 * \param[in]  pAllocator                   Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pDevice                      Receives the created logical device
 * \param[out] pQueue                       Receives queue zero from queueFamilyIndex
 *
 * \retval    None
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
                                 VkQueue*                        pQueue);

/*************************************************************************************************/
/**
 * \brief Returns an existing queue from a logical device and queue-family index.
 *
 * \param[in]  device              Non-null logical device that owns the referenced resources
 * \param[in]  queueFamilyIndex    Queue-family index selected for the operation
 * \param[in]  queueIndex          Queue index within the selected family
 * \param[out] pQueue              Receives the requested queue handle
 *
 * \retval    None
 */
void Gfx_Vulkan_GetDeviceQueue(VkDevice device, uint32_t queueFamilyIndex, uint32_t queueIndex, VkQueue* pQueue);

/*************************************************************************************************/
/**
 * \brief Waits until every queue owned by a logical device is idle.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 *
 * \retval    None
 */
void Gfx_Vulkan_DeviceWaitIdle(VkDevice device);

/*************************************************************************************************/
/**
 * \brief Returns the logical plane count for a supported packed or multi-planar format.
 *
 * \param[in] format              Vulkan format whose layout or capabilities are requested
 *
 * \return Plane count; unsupported formats are fatal.
 */
uint32_t Gfx_Vulkan_GetFormatPlaneCount(VkFormat format);

/*************************************************************************************************/
/**
 * \brief Returns the color or plane aspect used to copy a logical format plane.
 *
 * \param[in] format              Vulkan format whose layout or capabilities are requested
 * \param[in] planeIndex          Zero-based plane index
 *
 * \return COLOR for single-plane color formats or PLANE_0/1/2 for multi-planar formats; depth/stencil and invalid indices are fatal.
 */
VkImageAspectFlagBits Gfx_Vulkan_GetFormatPlaneAspect(VkFormat format, uint32_t planeIndex);

/*************************************************************************************************/
/**
 * \brief Maps a memory-plane index to VK_IMAGE_ASPECT_MEMORY_PLANE_*_BIT_EXT.
 *
 * \param[in] planeIndex          Zero-based plane index
 *
 * \return Matching memory-plane aspect; an out-of-range index is fatal.
 */
VkImageAspectFlagBits Gfx_Vulkan_GetMemoryPlaneAspect(uint32_t planeIndex);

/*************************************************************************************************/
/**
 * \brief Returns the complete image aspect mask required for a supported format.
 *
 * \param[in] format              Vulkan format whose layout or capabilities are requested
 *
 */
VkImageAspectFlags Gfx_Vulkan_GetFormatAspectMask(VkFormat format);

/*************************************************************************************************/
/**
 * \brief Derives image subresource layers from a format and layer selection.
 *
 * \param[in]  format           Vulkan format defining the image aspects
 * \param[in]  mipLevel         Selected mip level
 * \param[in]  baseArrayLayer   First selected array layer
 * \param[in]  layerCount       Number of selected array layers
 * \return Derived subresource layers
 */
VkImageSubresourceLayers Gfx_Vulkan_GetFormatSubresourceLayers(VkFormat format, uint32_t mipLevel, uint32_t baseArrayLayer, uint32_t layerCount);

/*************************************************************************************************/
/**
 * \brief Derives the base image subresource layers for a format.
 *
 * \param[in] format              Vulkan format defining the image aspects
 *
 * \return Base subresource layers for one array layer.
 */
VkImageSubresourceLayers Gfx_Vulkan_GetFormatBaseSubresourceLayers(VkFormat format);

/*************************************************************************************************/
/**
 * \brief Derives an image subresource range from a format and mip/layer selection.
 *
 * \param[in]  format           Vulkan format defining the image aspects
 * \param[in]  baseMipLevel     First selected mip level
 * \param[in]  levelCount       Number of selected mip levels
 * \param[in]  baseArrayLayer   First selected array layer
 * \param[in]  layerCount       Number of selected array layers
 * \return Derived subresource range
 */
VkImageSubresourceRange Gfx_Vulkan_GetFormatSubresourceRange(VkFormat format, uint32_t baseMipLevel, uint32_t levelCount, uint32_t baseArrayLayer, uint32_t layerCount);

/*************************************************************************************************/
/**
 * \brief Derives the base image subresource range for a format.
 *
 * \param[in] format              Vulkan format defining the image aspects
 *
 * \return Base subresource range for one mip level and one array layer.
 */
VkImageSubresourceRange Gfx_Vulkan_GetFormatBaseSubresourceRange(VkFormat format);

/*************************************************************************************************/
/**
 * \brief Calculates tightly concatenated, row-aligned layouts for every format plane.
 *
 * \par Details:
 *  Each plane array must hold three entries; unused entries are zeroed.
 *
 * \param[in]  format             Defined single- or multi-planar Vulkan format
 * \param[in]  width              Full image width in texels
 * \param[in]  height             Full image height in texels
 * \param[in]  depth              Full image depth in texels
 * \param[in]  layerCount         Array-layer count included in each plane allocation
 * \param[in]  rowAlignment       Positive byte alignment applied independently to every row
 * \param[out] planeOffsets       Byte offset of each plane in the concatenated allocation
 * \param[out] planeStrides       Aligned row size for each plane, restricted to uint32_t
 * \param[out] planeExtents       Subsampled texel extent for each plane
 * \param[out] pSize              Total bytes occupied by all planes and layers
 *
 * \return Number of valid entries written to the plane arrays.
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
                                                VkDeviceSize* pSize);

/*************************************************************************************************/
/**
 * \brief Computes the tightly packed byte size of a supported two-dimensional format.
 *
 * \param[in] width               Image width in texels
 * \param[in] height              Image height in texels
 * \param[in] format              Vulkan format whose layout or capabilities are requested
 *
 * \return Exact byte count with overflow and format validity checked.
 */
VkDeviceSize Gfx_Vulkan_GetFormatSize(uint32_t width, uint32_t height, VkFormat format);

/*************************************************************************************************/
/**
 * \brief Computes aligned YUV plane strides, offsets, and total buffer size.
 *
 * \par Details:
 *  rowAlignment must be a nonzero power of two. Outputs not applicable to a format are zero.
 *
 * \param[in]  format             Vulkan format whose layout or capabilities are requested
 * \param[in]  width              Image width in texels
 * \param[in]  height             Image height in texels
 * \param[in]  rowAlignment       Required byte alignment for each computed plane row pitch
 * \param[out] pYStride           Receives the luma-plane row stride in bytes
 * \param[out] pChromaOffset      Receives the byte offset of the first chroma plane
 * \param[out] pVOffset           Receives the byte offset of the V plane for three-plane formats
 * \param[out] pChromaStride      Receives the chroma-plane row stride in bytes
 * \param[out] pSize              Receives the total byte size of the computed format or allocation layout
 *
 * \retval    None
 */
void Gfx_Vulkan_GetFormatPlaneLayout(VkFormat      format,
                                     uint32_t      width,
                                     uint32_t      height,
                                     VkDeviceSize  rowAlignment,
                                     uint32_t*     pYStride,
                                     uint32_t*     pChromaOffset,
                                     uint32_t*     pVOffset,
                                     uint32_t*     pChromaStride,
                                     VkDeviceSize* pSize);

/*************************************************************************************************/
/**
 * \brief Creates a persistent unbound Vulkan image from explicit VkImageCreateInfo fields.
 *
 * \param[in]  device                   Non-null logical device that owns the referenced resources
 * \param[in]  pNext                    Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags                    Image creation flags
 * \param[in]  imageType                Dimensionality of the image to create
 * \param[in]  format                   Vulkan format whose layout or capabilities are requested
 * \param[in]  extent                   Image dimensions in texels
 * \param[in]  mipLevels                Number of mip levels in the image
 * \param[in]  arrayLayers              Number of array layers in the image
 * \param[in]  samples                  Sample count used for image creation
 * \param[in]  tiling                   Image tiling mode used for creation
 * \param[in]  usage                    Permitted image uses
 * \param[in]  sharingMode              Exclusive or concurrent resource-sharing mode
 * \param[in]  queueFamilyIndexCount    Number of queue-family indices; ignored for exclusive sharing
 * \param[in]  pQueueFamilyIndices      Queue-family index array containing queueFamilyIndexCount entries, or NULL when the count is zero
 * \param[in]  initialLayout            Initial layout assigned when the image is created
 * \param[in]  pAllocator               Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pImage                   Receives the created image
 *
 * \retval    None
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
                            VkImage*                     pImage);

/*************************************************************************************************/
/**
 * \brief Creates an image with caller-selected external-memory handle types.
 *
 * \par Details:
 *  pNext follows the external-memory image create info in the input chain.
 *
 * \param[in]  device                   Non-null logical device that owns the referenced resources
 * \param[in]  pNext                    Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  handleTypes              External-memory handle-type mask associated with the resource
 * \param[in]  flags                    External image creation flags
 * \param[in]  imageType                Dimensionality of the image to create
 * \param[in]  format                   Vulkan format whose layout or capabilities are requested
 * \param[in]  extent                   Image dimensions in texels
 * \param[in]  mipLevels                Number of mip levels in the image
 * \param[in]  arrayLayers              Number of array layers in the image
 * \param[in]  samples                  Sample count used for image creation
 * \param[in]  tiling                   Image tiling mode used for creation
 * \param[in]  usage                    Permitted image uses
 * \param[in]  sharingMode              Exclusive or concurrent resource-sharing mode
 * \param[in]  queueFamilyIndexCount    Number of queue-family indices; ignored for exclusive sharing
 * \param[in]  pQueueFamilyIndices      Queue-family index array containing queueFamilyIndexCount entries, or NULL when the count is zero
 * \param[in]  initialLayout            Initial layout assigned when the image is created
 * \param[in]  pAllocator               Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pImage                   Receives the created image
 *
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
                                    VkImage*                        pImage);

/*************************************************************************************************/
/**
 * \brief Creates an image, selects compatible memory, allocates it, and binds it at offset zero.
 *
 * \param[in]  physicalDevice           Non-null physical device queried or used to select compatible resources
 * \param[in]  device                   Non-null logical device that owns the referenced resources
 * \param[in]  pImageNext               Optional extension chain attached to the composed image input structure, or NULL
 * \param[in]  flags                    Object creation flags
 * \param[in]  imageType                Dimensionality of the image to create
 * \param[in]  format                   Vulkan format whose layout or capabilities are requested
 * \param[in]  extent                   Image dimensions in texels
 * \param[in]  mipLevels                Number of mip levels in the image
 * \param[in]  arrayLayers              Number of array layers in the image
 * \param[in]  samples                  Sample count used for image creation
 * \param[in]  tiling                   Image tiling mode used for creation
 * \param[in]  usage                    Permitted image uses
 * \param[in]  sharingMode              Exclusive or concurrent resource-sharing mode
 * \param[in]  queueFamilyIndexCount    Number of queue-family indices; ignored for exclusive sharing
 * \param[in]  pQueueFamilyIndices      Queue-family index array containing queueFamilyIndexCount entries, or NULL when the count is zero
 * \param[in]  initialLayout            Initial layout assigned when the image is created
 * \param[in]  pMemoryNext              Optional extension chain attached to the composed memory structure, or NULL
 * \param[in]  properties               Required Vulkan memory-property flags
 * \param[in]  pAllocator               Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pImage                   Receives the created image
 * \param[out] pMemory                  Receives the allocated device memory
 *
 * \retval    None
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
                           VkDeviceMemory*              pMemory);

/*************************************************************************************************/
/**
 * \brief Returns size, alignment, and compatible memory types for an image.
 *
 * \param[in]  device                 Non-null logical device that owns the referenced resources
 * \param[in]  image                  Vulkan image whose resources are queried or modified
 * \param[out] pMemoryRequirements    Receives the resource size, alignment, and compatible memory-type mask
 *
 * \retval    None
 */
void Gfx_Vulkan_GetImageMemoryRequirements(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements);

/*************************************************************************************************/
/**
 * \brief Queries memory requirements using an image description and optional output chain.
 *
 * \par Details:
 *  Requires VK_KHR_maintenance4 on device.
 *
 * \param[in]     device                Logical device on which the image will be created
 * \param[in]     pNext                 Extension chain for VkDeviceImageMemoryRequirements
 * \param[in]     pImageNext            Image creation extension chain, or NULL
 * \param[in]     flags                 Image creation flags
 * \param[in]     imageType             Image dimensionality
 * \param[in]     format                Pixel format
 * \param[in]     extent                Image dimensions
 * \param[in]     mipLevels             Number of mip levels
 * \param[in]     arrayLayers           Number of array layers
 * \param[in]     samples               Samples per texel
 * \param[in]     tiling                Image tiling
 * \param[in]     usage                 Image usage flags
 * \param[in]     sharingMode           Queue sharing mode
 * \param[in]     queueFamilyIndexCount Number of queue family indices
 * \param[in]     pQueueFamilyIndices   Queue family indices for concurrent sharing
 * \param[in]     initialLayout         Initial image layout
 * \param[in]     planeAspect           Plane to query for disjoint images; zero for the whole image
 * \param[in,out] pMemoryNext           Writable output extension chain, or NULL
 * \param[out]    pMemoryRequirements   Receives size, alignment, and compatible memory types
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
                                                  VkMemoryRequirements2* pMemoryRequirements);

/*************************************************************************************************/
/**
 * \brief Returns memory requirements from an external image description without creating an image.
 *
 * \par Details:
 *  Requires VK_KHR_maintenance4 on device.
 *
 * \param[in]  device                    Logical device on which the image will be created
 * \param[in]  pNext                     Extension chain following the external-memory image info
 * \param[in]  handleTypes               External-memory handle types used by the image
 * \param[in]  flags                     Image creation flags
 * \param[in]  imageType                 Image dimensionality
 * \param[in]  format                    Pixel format
 * \param[in]  extent                    Image dimensions
 * \param[in]  mipLevels                 Number of mip levels
 * \param[in]  arrayLayers               Number of array layers
 * \param[in]  samples                   Samples per texel
 * \param[in]  tiling                    Image tiling
 * \param[in]  usage                     Image usage flags
 * \param[in]  sharingMode               Queue sharing mode
 * \param[in]  queueFamilyIndexCount     Number of queue family indices
 * \param[in]  pQueueFamilyIndices       Queue family indices for concurrent sharing
 * \param[in]  initialLayout             Initial image layout
 * \param[in]  planeAspect               Plane to query for disjoint images; zero for the whole image
 * \param[out] pMemoryRequirements       Receives size, alignment, and compatible memory types
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
                                                   VkMemoryRequirements*           pMemoryRequirements);

/*************************************************************************************************/
/**
 * \brief Queries image memory requirements using caller-supplied input info.
 *
 * \param[in]     device              Non-null logical device that owns the referenced resources
 * \param[in]     pInfo               Non-null Vulkan query structure consumed by the operation
 * \param[in,out] pMemoryNext         Optional extension chain attached to the composed memory structure, or NULL
 * \param[out]    pMemoryRequirements Receives the resource size, alignment, and compatible memory-type mask
 *
 */
void Gfx_Vulkan_GetImageMemoryRequirements2(VkDevice device, const VkImageMemoryRequirementsInfo2* pInfo, void* pMemoryNext, VkMemoryRequirements2* pMemoryRequirements);

/*************************************************************************************************/
/**
 * \brief Queries image memory requirements from an image and input extension chain.
 *
 * \param[in]     device              Non-null logical device that owns the referenced resources
 * \param[in]     pNext               Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]     image               Vulkan image whose resources are queried or modified
 * \param[in,out] pMemoryNext         Optional extension chain attached to the composed memory structure, or NULL
 * \param[out]    pMemoryRequirements Receives the resource size, alignment, and compatible memory-type mask
 *
 */
void Gfx_Vulkan_GetImageMemoryRequirementsInfo2(VkDevice device, const void* pNext, VkImage image, void* pMemoryNext, VkMemoryRequirements2* pMemoryRequirements);

/*************************************************************************************************/
/**
 * \brief Adds a plane selection to an extended image memory-requirements query.
 *
 * \par Details:
 *  pImageNext follows the plane input structure; pMemoryNext is an optional writable output chain.
 *
 * \param[in]     device              Non-null logical device that owns the referenced resources
 * \param[in]     pImageNext          Optional extension chain attached to the composed image input structure, or NULL
 * \param[in]     image               Vulkan image whose resources are queried or modified
 * \param[in]     planeAspect         Single plane aspect selected for the query
 * \param[in,out] pMemoryNext         Optional extension chain attached to the composed memory structure, or NULL
 * \param[out]    pMemoryRequirements Receives the resource size, alignment, and compatible memory-type mask
 *
 * \retval    None
 */
void Gfx_Vulkan_GetImagePlaneMemoryRequirements2(VkDevice               device,
                                                 const void*            pImageNext,
                                                 VkImage                image,
                                                 VkImageAspectFlagBits  planeAspect,
                                                 void*                  pMemoryNext,
                                                 VkMemoryRequirements2* pMemoryRequirements);

/*************************************************************************************************/
/**
 * \brief Returns memory requirements for one plane of a disjoint multi-planar image.
 *
 * \param[in]  device                 Non-null logical device that owns the referenced resources
 * \param[in]  image                  Vulkan image whose resources are queried or modified
 * \param[in]  planeAspect            Single plane aspect selected for the query
 * \param[out] pMemoryRequirements    Receives the resource size, alignment, and compatible memory-type mask
 *
 * \retval    None
 */
void Gfx_Vulkan_GetImagePlaneMemoryRequirements(VkDevice device, VkImage image, VkImageAspectFlagBits planeAspect, VkMemoryRequirements* pMemoryRequirements);

/*************************************************************************************************/
/**
 * \brief Allocates persistent Vulkan device memory from an explicit memory-type index.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  allocationSize     Number of bytes to allocate
 * \param[in]  memoryTypeIndex    Physical-device memory-type index used for the allocation
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pMemory            Receives the allocated device memory
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateMemory(VkDevice device, const void* pNext, VkDeviceSize allocationSize, uint32_t memoryTypeIndex, const VkAllocationCallbacks* pAllocator, VkDeviceMemory* pMemory);

/*************************************************************************************************/
/**
 * \brief Allocates memory with explicit dedicated image and buffer handles.
 *
 * \par Details:
 *  At most one of image and buffer may be non-null.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  image              Image receiving dedicated memory, or VK_NULL_HANDLE
 * \param[in]  buffer             Buffer receiving dedicated memory, or VK_NULL_HANDLE
 * \param[in]  allocationSize     Number of bytes to allocate
 * \param[in]  memoryTypeIndex    Physical-device memory-type index used for the allocation
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pMemory            Receives the allocated device memory
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateDedicatedMemory(VkDevice                     device,
                                        const void*                  pNext,
                                        VkImage                      image,
                                        VkBuffer                     buffer,
                                        VkDeviceSize                 allocationSize,
                                        uint32_t                     memoryTypeIndex,
                                        const VkAllocationCallbacks* pAllocator,
                                        VkDeviceMemory*              pMemory);

/*************************************************************************************************/
/**
 * \brief Allocates persistent memory compatible with an image and required property flags.
 *
 * \param[in]  physicalDevice     Non-null physical device queried or used to select compatible resources
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  image              Vulkan image whose resources are queried or modified
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  properties         Required Vulkan memory-property flags
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pMemory            Receives the allocated device memory
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateImageMemory(VkPhysicalDevice             physicalDevice,
                                    VkDevice                     device,
                                    VkImage                      image,
                                    const void*                  pNext,
                                    VkMemoryPropertyFlags        properties,
                                    const VkAllocationCallbacks* pAllocator,
                                    VkDeviceMemory*              pMemory);

/*************************************************************************************************/
/**
 * \brief Binds a Vulkan memory allocation to an image at an aligned byte offset.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] image               Vulkan image whose resources are queried or modified
 * \param[in] memory              Device-memory allocation containing the requested range
 * \param[in] memoryOffset        Byte offset in the allocation at which the resource is bound
 *
 * \retval    None
 */
void Gfx_Vulkan_BindImageMemory(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);

/*************************************************************************************************/
/**
 * \brief Allocates dedicated image memory using a caller-selected memory-type index.
 *
 * \param[in]  device                 Logical device owning the image
 * \param[in]  pNext                  Optional allocation extension chain, or NULL
 * \param[in]  image                  Image receiving the dedicated allocation
 * \param[in]  allocationSize         Number of bytes to allocate
 * \param[in]  memoryTypeIndex        Compatible physical-device memory-type index selected by the caller
 * \param[in]  pAllocator             Optional Vulkan allocation callbacks
 * \param[out] pMemory                Receives allocated memory; binding remains the caller's responsibility
 *
 * \retval None
 */
void Gfx_Vulkan_AllocateDedicatedImageMemory(VkDevice                     device,
                                             const void*                  pNext,
                                             VkImage                      image,
                                             VkDeviceSize                 allocationSize,
                                             uint32_t                     memoryTypeIndex,
                                             const VkAllocationCallbacks* pAllocator,
                                             VkDeviceMemory*              pMemory);

/*************************************************************************************************/
/**
 * \brief Binds memory allocations to the supplied images in one call.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] bindInfoCount       Number of entries in pBindInfos
 * \param[in] pBindInfos          Image-memory bindings containing bindInfoCount entries
 *
 * \retval    None
 */
void Gfx_Vulkan_BindImageMemory2(VkDevice device, uint32_t bindInfoCount, const VkBindImageMemoryInfo* pBindInfos);

/*************************************************************************************************/
/**
 * \brief Builds memory bindings for one image and submits them in one call.
 *
 * \par Details:
 *  memoryCount must not exceed VK_MAX_DRM_FORMAT_MODIFIER_PLANES. ppNext may be NULL or point to
 *  memoryCount input chains. pMemoryOffsets may be NULL to select VK_NO_OFFSET for every binding.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] ppNext              Array of optional extension chains parallel to the generated Vulkan structures, or NULL
 * \param[in] image               Vulkan image whose resources are queried or modified
 * \param[in] memoryCount         Number of memory bindings described by the parallel input arrays
 * \param[in] pMemories           Device-memory handles parallel to the generated binding structures
 * \param[in] pMemoryOffsets      Optional memory offsets parallel to pMemories; NULL selects VK_NO_OFFSET for every binding
 *
 */
void Gfx_Vulkan_BindImageMemories(VkDevice device, const void* const* ppNext, VkImage image, uint32_t memoryCount, const VkDeviceMemory* pMemories, const VkDeviceSize* pMemoryOffsets);

/*************************************************************************************************/
/**
 * \brief Adds plane selections to memory bindings and submits all planes in one call.
 *
 * \par Details:
 *  planeCount must not exceed VK_MAX_DRM_FORMAT_MODIFIER_PLANES. ppNext optionally supplies the
 *  chain following each plane structure. A NULL pMemoryOffsets selects VK_NO_OFFSET for all planes.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] ppNext              Array of optional extension chains parallel to the generated Vulkan structures, or NULL
 * \param[in] image               Vulkan image whose resources are queried or modified
 * \param[in] planeCount          Number of valid plane entries supplied to the operation
 * \param[in] pPlaneAspects       Plane aspects parallel to pMemories
 * \param[in] pMemories           Device-memory handles parallel to the generated binding structures
 * \param[in] pMemoryOffsets      Optional memory offsets parallel to pMemories; NULL selects VK_NO_OFFSET for every binding
 *
 */
void Gfx_Vulkan_BindImagePlaneMemories(VkDevice                     device,
                                       const void* const*           ppNext,
                                       VkImage                      image,
                                       uint32_t                     planeCount,
                                       const VkImageAspectFlagBits* pPlaneAspects,
                                       const VkDeviceMemory*        pMemories,
                                       const VkDeviceSize*          pMemoryOffsets);

/*************************************************************************************************/
/**
 * \brief Creates a persistent image view over explicitly selected components and subresources.
 *
 * \param[in]  device              Non-null logical device that owns the referenced resources
 * \param[in]  pNext               Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags               Image view creation flags
 * \param[in]  image               Vulkan image whose resources are queried or modified
 * \param[in]  viewType            Dimensionality of the image view to create
 * \param[in]  format              Vulkan format whose layout or capabilities are requested
 * \param[in]  components          Channel swizzle applied by the image view
 * \param[in]  subresourceRange    Image aspects, mip levels, and array layers exposed by the view
 * \param[in]  pAllocator          Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pView               Receives the created image view
 *
 * \retval    None
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
                                VkImageView*                 pView);

/*************************************************************************************************/
/**
 * \brief Creates a persistent unbound Vulkan buffer from explicit VkBufferCreateInfo fields.
 *
 * \param[in]  device                   Non-null logical device that owns the referenced resources
 * \param[in]  pNext                    Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags                    Buffer creation flags
 * \param[in]  size                     Nonzero buffer size in bytes
 * \param[in]  usage                    Permitted image uses
 * \param[in]  sharingMode              Exclusive or concurrent resource-sharing mode
 * \param[in]  queueFamilyIndexCount    Number of queue-family indices; ignored for exclusive sharing
 * \param[in]  pQueueFamilyIndices      Queue-family index array containing queueFamilyIndexCount entries, or NULL when the count is zero
 * \param[in]  pAllocator               Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pBuffer                  Receives the created buffer
 *
 * \retval    None
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
                             VkBuffer*                    pBuffer);

/*************************************************************************************************/
/**
 * \brief Creates a buffer, allocates compatible memory, and binds it at offset zero.
 *
 * \param[in]  physicalDevice           Non-null physical device queried or used to select compatible resources
 * \param[in]  device                   Non-null logical device that owns the referenced resources
 * \param[in]  pBufferNext              Optional extension chain attached to the composed buffer structure, or NULL
 * \param[in]  flags                    Object creation flags
 * \param[in]  size                     Nonzero buffer size in bytes
 * \param[in]  usage                    Permitted image uses
 * \param[in]  sharingMode              Exclusive or concurrent resource-sharing mode
 * \param[in]  queueFamilyIndexCount    Number of queue-family indices; ignored for exclusive sharing
 * \param[in]  pQueueFamilyIndices      Queue-family index array containing queueFamilyIndexCount entries, or NULL when the count is zero
 * \param[in]  pMemoryNext              Optional extension chain attached to the composed memory structure, or NULL
 * \param[in]  properties               Required Vulkan memory-property flags
 * \param[in]  pAllocator               Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pBuffer                  Receives the created buffer
 * \param[out] pMemory                  Receives the allocated device memory
 *
 * \retval    None
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
                            VkDeviceMemory*              pMemory);

/*************************************************************************************************/
/**
 * \brief Computes a YUV plane layout and creates matching bound buffer storage.
 *
 * \par Details:
 *  Stride and offset outputs describe the exact linear layout backed by the returned allocation.
 *
 * \param[in]  physicalDevice           Non-null physical device queried or used to select compatible resources
 * \param[in]  device                   Non-null logical device that owns the referenced resources
 * \param[in]  pBufferNext              Optional extension chain attached to the composed buffer structure, or NULL
 * \param[in]  flags                    Object creation flags
 * \param[in]  format                   Vulkan format whose layout or capabilities are requested
 * \param[in]  width                    Image width in texels
 * \param[in]  height                   Image height in texels
 * \param[in]  rowAlignment             Required byte alignment for each computed plane row pitch
 * \param[in]  usage                    Permitted image uses
 * \param[in]  sharingMode              Exclusive or concurrent resource-sharing mode
 * \param[in]  queueFamilyIndexCount    Number of queue-family indices; ignored for exclusive sharing
 * \param[in]  pQueueFamilyIndices      Queue-family index array containing queueFamilyIndexCount entries, or NULL when the count is zero
 * \param[in]  pMemoryNext              Optional extension chain attached to the composed memory structure, or NULL
 * \param[in]  properties               Required Vulkan memory-property flags
 * \param[in]  pAllocator               Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pBuffer                  Receives the created buffer
 * \param[out] pMemory                  Receives the allocated device memory
 * \param[out] pSize                    Receives the total byte size of the computed format or allocation layout
 * \param[out] pYStride                 Receives the luma-plane row stride in bytes
 * \param[out] pChromaOffset            Receives the byte offset of the first chroma plane
 * \param[out] pVOffset                 Receives the byte offset of the V plane for three-plane formats
 * \param[out] pChromaStride            Receives the chroma-plane row stride in bytes
 *
 * \retval    None
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
                                 uint32_t*                    pChromaStride);

/*************************************************************************************************/
/**
 * \brief Creates an unbound buffer whose memory may use any requested external handle type.
 *
 * \param[in]  device                   Non-null logical device that owns the referenced resources
 * \param[in]  pNext                    Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  handleTypes              External-memory handle-type mask associated with the resource
 * \param[in]  flags                    External memory buffer creation flags
 * \param[in]  size                     Nonzero buffer size in bytes
 * \param[in]  usage                    Permitted image uses
 * \param[in]  sharingMode              Exclusive or concurrent resource-sharing mode
 * \param[in]  queueFamilyIndexCount    Number of queue-family indices; ignored for exclusive sharing
 * \param[in]  pQueueFamilyIndices      Queue-family index array containing queueFamilyIndexCount entries, or NULL when the count is zero
 * \param[in]  pAllocator               Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pBuffer                  Receives the created buffer
 *
 * \retval    None
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
                                           VkBuffer*                       pBuffer);

/*************************************************************************************************/
/**
 * \brief Returns size, alignment, and compatible memory types for a buffer.
 *
 * \param[in]  device                 Non-null logical device that owns the referenced resources
 * \param[in]  buffer                 Vulkan buffer whose resources are queried or modified
 * \param[out] pMemoryRequirements    Receives the resource size, alignment, and compatible memory-type mask
 *
 * \retval    None
 */
void Gfx_Vulkan_GetBufferMemoryRequirements(VkDevice device, VkBuffer buffer, VkMemoryRequirements* pMemoryRequirements);

/*************************************************************************************************/
/**
 * \brief Allocates persistent memory compatible with a buffer and required property flags.
 *
 * \param[in]  physicalDevice     Non-null physical device queried or used to select compatible resources
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  buffer             Vulkan buffer whose resources are queried or modified
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  properties         Required Vulkan memory-property flags
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pMemory            Receives the allocated device memory
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateBufferMemory(VkPhysicalDevice             physicalDevice,
                                     VkDevice                     device,
                                     VkBuffer                     buffer,
                                     const void*                  pNext,
                                     VkMemoryPropertyFlags        properties,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkDeviceMemory*              pMemory);

/*************************************************************************************************/
/**
 * \brief Allocates dedicated memory compatible with an existing buffer.
 *
 * \param[in]  physicalDevice    Physical device used for memory-type selection
 * \param[in]  device            Logical device owning the buffer
 * \param[in]  pNext             Optional allocation extension chain, or NULL
 * \param[in]  buffer            Non-null buffer receiving the dedicated allocation
 * \param[in]  properties        Required memory-property flags
 * \param[in]  pAllocator        Optional Vulkan allocation callbacks, or NULL
 * \param[out] pMemory           Receives the dedicated memory allocation
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateDedicatedBufferMemory(VkPhysicalDevice             physicalDevice,
                                              VkDevice                     device,
                                              const void*                  pNext,
                                              VkBuffer                     buffer,
                                              VkMemoryPropertyFlags        properties,
                                              const VkAllocationCallbacks* pAllocator,
                                              VkDeviceMemory*              pMemory);

/*************************************************************************************************/
/**
 * \brief Allocates exportable memory compatible with an existing buffer.
 *
 * \param[in]  physicalDevice  Physical device used for memory-type selection
 * \param[in]  device          Logical device owning the buffer
 * \param[in]  buffer          Non-null buffer receiving the exportable allocation
 * \param[in]  pNext           Optional allocation chain following the export info, or NULL
 * \param[in]  handleTypes     Nonzero external-memory handle-type mask
 * \param[in]  properties      Required memory-property flags
 * \param[in]  pAllocator      Optional Vulkan allocation callbacks, or NULL
 * \param[out] pMemory         Receives the exportable memory allocation
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateExportableBufferMemory(VkPhysicalDevice                physicalDevice,
                                               VkDevice                        device,
                                               VkBuffer                        buffer,
                                               const void*                     pNext,
                                               VkExternalMemoryHandleTypeFlags handleTypes,
                                               VkMemoryPropertyFlags           properties,
                                               const VkAllocationCallbacks*    pAllocator,
                                               VkDeviceMemory*                 pMemory);

/*************************************************************************************************/
/**
 * \brief Allocates exportable dedicated memory compatible with an existing buffer.
 *
 * \param[in]  physicalDevice  Physical device used for memory-type selection
 * \param[in]  device          Logical device owning the buffer
 * \param[in]  buffer          Non-null buffer receiving the exportable dedicated allocation
 * \param[in]  pNext           Optional allocation chain following the export info, or NULL
 * \param[in]  handleTypes     Nonzero external-memory handle-type mask
 * \param[in]  properties      Required memory-property flags
 * \param[in]  pAllocator      Optional Vulkan allocation callbacks, or NULL
 * \param[out] pMemory         Receives the exportable dedicated memory allocation
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateExportableDedicatedBufferMemory(VkPhysicalDevice                physicalDevice,
                                                        VkDevice                        device,
                                                        VkBuffer                        buffer,
                                                        const void*                     pNext,
                                                        VkExternalMemoryHandleTypeFlags handleTypes,
                                                        VkMemoryPropertyFlags           properties,
                                                        const VkAllocationCallbacks*    pAllocator,
                                                        VkDeviceMemory*                 pMemory);

/*************************************************************************************************/
/**
 * \brief Allocates buffer memory from the intersection of Vulkan and external type masks.
 *
 * \param[in]  physicalDevice            Non-null physical device queried or used to select compatible resources
 * \param[in]  device                    Non-null logical device that owns the referenced resources
 * \param[in]  buffer                    Vulkan buffer whose resources are queried or modified
 * \param[in]  externalMemoryTypeBits    Bit mask of memory types compatible with the external allocation
 * \param[in]  pNext                     Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  properties                Required Vulkan memory-property flags
 * \param[in]  pAllocator                Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pMemory                   Receives the allocated device memory
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateExternalBufferMemory(VkPhysicalDevice             physicalDevice,
                                             VkDevice                     device,
                                             VkBuffer                     buffer,
                                             uint32_t                     externalMemoryTypeBits,
                                             const void*                  pNext,
                                             VkMemoryPropertyFlags        properties,
                                             const VkAllocationCallbacks* pAllocator,
                                             VkDeviceMemory*              pMemory);

/*************************************************************************************************/
/**
 * \brief Allocates dedicated buffer memory compatible with Vulkan and an external memory object.
 *
 * \param[in]  physicalDevice            Physical device used for memory-type selection
 * \param[in]  device                    Logical device owning the buffer
 * \param[in]  pNext                     Required external-memory allocation extension chain
 * \param[in]  buffer                    Non-null buffer receiving the dedicated allocation
 * \param[in]  externalMemoryTypeBits    Memory types compatible with the external allocation
 * \param[in]  properties                Required memory-property flags
 * \param[in]  pAllocator                Optional Vulkan allocation callbacks, or NULL
 * \param[out] pMemory                   Receives the dedicated external memory allocation
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateDedicatedExternalBufferMemory(VkPhysicalDevice             physicalDevice,
                                                      VkDevice                     device,
                                                      const void*                  pNext,
                                                      VkBuffer                     buffer,
                                                      uint32_t                     externalMemoryTypeBits,
                                                      VkMemoryPropertyFlags        properties,
                                                      const VkAllocationCallbacks* pAllocator,
                                                      VkDeviceMemory*              pMemory);

/*************************************************************************************************/
/**
 * \brief Creates, allocates, and binds a buffer backed by exportable memory.
 *
 * \param[in]  physicalDevice         Physical device used for memory-type selection
 * \param[in]  device                 Logical device owning the buffer
 * \param[in]  pBufferNext            Optional chain following the external buffer create info
 * \param[in]  handleTypes            Nonzero external-memory handle-type mask
 * \param[in]  flags                  Buffer creation flags
 * \param[in]  size                   Nonzero buffer size in bytes
 * \param[in]  usage                  Nonzero buffer usage mask
 * \param[in]  sharingMode            Exclusive or concurrent sharing mode
 * \param[in]  queueFamilyIndexCount  Number of queue-family indices
 * \param[in]  pQueueFamilyIndices    Queue-family index array, or NULL when the count is zero
 * \param[in]  pMemoryNext            Optional chain following the export memory allocation info
 * \param[in]  properties             Required memory-property flags
 * \param[in]  pAllocator             Optional Vulkan allocation callbacks, or NULL
 * \param[out] pBuffer                Receives the created buffer
 * \param[out] pMemory                Receives its bound memory allocation
 *
 * \retval    None
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
                                            VkDeviceMemory*                 pMemory);

/*************************************************************************************************/
/**
 * \brief Creates, allocates, and binds a buffer backed by exportable dedicated memory.
 *
 * \param[in]  physicalDevice         Physical device used for memory-type selection
 * \param[in]  device                 Logical device owning the buffer
 * \param[in]  pBufferNext            Optional chain following the external buffer create info
 * \param[in]  handleTypes            Nonzero external-memory handle-type mask
 * \param[in]  flags                  Buffer creation flags
 * \param[in]  size                   Nonzero buffer size in bytes
 * \param[in]  usage                  Nonzero buffer usage mask
 * \param[in]  sharingMode            Exclusive or concurrent sharing mode
 * \param[in]  queueFamilyIndexCount  Number of queue-family indices
 * \param[in]  pQueueFamilyIndices    Queue-family index array, or NULL when the count is zero
 * \param[in]  pMemoryNext            Optional chain following the export memory allocation info
 * \param[in]  properties             Required memory-property flags
 * \param[in]  pAllocator             Optional Vulkan allocation callbacks, or NULL
 * \param[out] pBuffer                Receives the created buffer
 * \param[out] pMemory                Receives its bound dedicated memory allocation
 *
 * \retval    None
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
                                                   VkDeviceMemory*                 pMemory);

/*************************************************************************************************/
/**
 * \brief Binds a Vulkan memory allocation to a buffer at an aligned byte offset.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] buffer              Vulkan buffer whose resources are queried or modified
 * \param[in] memory              Device-memory allocation containing the requested range
 * \param[in] memoryOffset        Byte offset in the allocation at which the resource is bound
 *
 * \retval    None
 */
void Gfx_Vulkan_BindBufferMemory(VkDevice device, VkBuffer buffer, VkDeviceMemory memory, VkDeviceSize memoryOffset);

/*************************************************************************************************/
/**
 * \brief Maps a host-visible memory range and returns its byte-addressable pointer.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  memory             Device-memory allocation containing the requested range
 * \param[in]  offset             Byte offset from the beginning of the allocation or resource
 * \param[in]  size               Size in bytes, or VK_WHOLE_SIZE where supported
 * \param[in]  flags              Memory mapping flags
 * \param[out] ppData             Receives the host pointer for the mapped range
 *
 * \retval    None
 */
void Gfx_Vulkan_MapMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void** ppData);

/*************************************************************************************************/
/**
 * \brief Ends the active host mapping of a Vulkan memory allocation.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] memory              Device-memory allocation containing the requested range
 *
 * \retval    None
 */
void Gfx_Vulkan_UnmapMemory(VkDevice device, VkDeviceMemory memory);

/*************************************************************************************************/
/**
 * \brief Invalidates mapped noncoherent memory ranges for subsequent host reads.
 *
 * \param[in] device       Logical device owning every range
 * \param[in] rangeCount   Number of entries in pRanges
 * \param[in] pRanges      Mapped memory ranges to invalidate
 *
 * \retval    None
 */
void Gfx_Vulkan_InvalidateMappedMemoryRanges(VkDevice device, uint32_t rangeCount, const VkMappedMemoryRange* pRanges);

/*************************************************************************************************/
/**
 * \brief Invalidates one mapped noncoherent memory range for a subsequent host read.
 *
 * \param[in] device   Logical device owning the range
 * \param[in] pNext    Optional mapped-memory-range extension chain, or NULL
 * \param[in] memory   Memory allocation containing the mapped range
 * \param[in] offset   Atom-aligned mapped byte offset
 * \param[in] size     Mapped byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_InvalidateMappedMemoryRange(VkDevice device, const void* pNext, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size);

/*************************************************************************************************/
/**
 * \brief Flushes mapped noncoherent memory ranges after host writes.
 *
 * \param[in] device       Logical device owning every range
 * \param[in] rangeCount   Number of entries in pRanges
 * \param[in] pRanges      Mapped memory ranges to flush
 *
 * \retval    None
 */
void Gfx_Vulkan_FlushMappedMemoryRanges(VkDevice device, uint32_t rangeCount, const VkMappedMemoryRange* pRanges);

/*************************************************************************************************/
/**
 * \brief Flushes one mapped noncoherent memory range after a host write.
 *
 * \param[in] device   Logical device owning the range
 * \param[in] pNext    Optional mapped-memory-range extension chain, or NULL
 * \param[in] memory   Memory allocation containing the mapped range
 * \param[in] offset   Atom-aligned mapped byte offset
 * \param[in] size     Mapped byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_FlushMappedMemoryRange(VkDevice device, const void* pNext, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size);

/*************************************************************************************************/
/**
 * \brief Invalidates an atom-aligned noncoherent range and copies it into host storage.
 *
 * \par Details:
 *  The mapping starts at offset rounded down to nonCoherentAtomSize. mappedSize is measured
 *  from that aligned offset and must cover the copy. It must meet noncoherent range alignment
 *  requirements or reach the allocation end. VK_WHOLE_SIZE maps through the allocation end.
 *
 * \param[in]  device                 Non-null logical device that owns the referenced resources
 * \param[in]  pNext                  Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  memory                 Device-memory allocation containing the requested range
 * \param[in]  offset                 Byte offset from the beginning of the allocation or resource
 * \param[in]  size                   Nonzero number of bytes to copy; VK_WHOLE_SIZE is invalid
 * \param[in]  mappedSize             Byte count mapped and synchronized from the aligned offset, or VK_WHOLE_SIZE
 * \param[in]  flags                  Memory mapping flags
 * \param[in]  nonCoherentAtomSize    Physical-device nonCoherentAtomSize used to align flush or invalidate ranges
 * \param[out] dst                    Non-null destination byte storage large enough for size bytes
 *
 * \retval    None
 */
void Gfx_Vulkan_ReadMemory(VkDevice         device,
                           const void*      pNext,
                           VkDeviceMemory   memory,
                           VkDeviceSize     offset,
                           VkDeviceSize     size,
                           VkDeviceSize     mappedSize,
                           VkMemoryMapFlags flags,
                           VkDeviceSize     nonCoherentAtomSize,
                           void*            dst);

/*************************************************************************************************/
/**
 * \brief Copies host bytes into mapped noncoherent memory and flushes an atom-aligned range.
 *
 * \par Details:
 *  The mapping starts at offset rounded down to nonCoherentAtomSize. mappedSize is measured
 *  from that aligned offset and must cover the copy. It must meet noncoherent range alignment
 *  requirements or reach the allocation end. VK_WHOLE_SIZE maps through the allocation end.
 *
 * \param[in] device                 Non-null logical device that owns the referenced resources
 * \param[in] pNext                  Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in] memory                 Device-memory allocation containing the requested range
 * \param[in] offset                 Byte offset from the beginning of the allocation or resource
 * \param[in] size                   Nonzero number of bytes to copy; VK_WHOLE_SIZE is invalid
 * \param[in] mappedSize             Byte count mapped and synchronized from the aligned offset, or VK_WHOLE_SIZE
 * \param[in] flags                  Memory mapping flags
 * \param[in] nonCoherentAtomSize    Physical-device nonCoherentAtomSize used to align flush or invalidate ranges
 * \param[in] src                    Non-null source byte storage containing size bytes
 *
 * \retval    None
 */
void Gfx_Vulkan_WriteMemory(VkDevice         device,
                            const void*      pNext,
                            VkDeviceMemory   memory,
                            VkDeviceSize     offset,
                            VkDeviceSize     size,
                            VkDeviceSize     mappedSize,
                            VkMemoryMapFlags flags,
                            VkDeviceSize     nonCoherentAtomSize,
                            const void*      src);

/*************************************************************************************************/
/**
 * \brief Copies a coherent mapped-memory range into host storage without cache maintenance.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  memory             Device-memory allocation containing the requested range
 * \param[in]  offset             Byte offset from the beginning of the allocation or resource
 * \param[in]  size               Nonzero number of bytes to copy; VK_WHOLE_SIZE is invalid
 * \param[in]  flags              Memory mapping flags
 * \param[out] dst                Non-null destination byte storage large enough for size bytes
 *
 * \retval    None
 */
void Gfx_Vulkan_ReadCoherentMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, void* dst);

/*************************************************************************************************/
/**
 * \brief Copies host bytes into coherent mapped memory without explicit cache maintenance.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] memory              Device-memory allocation containing the requested range
 * \param[in] offset              Byte offset from the beginning of the allocation or resource
 * \param[in] size                Nonzero number of bytes to copy; VK_WHOLE_SIZE is invalid
 * \param[in] flags               Memory mapping flags
 * \param[in] src                 Non-null source byte storage containing size bytes
 *
 * \retval    None
 */
void Gfx_Vulkan_WriteCoherentMemory(VkDevice device, VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, VkMemoryMapFlags flags, const void* src);

/*************************************************************************************************/
/**
 * \brief Returns offset, size, row pitch, array pitch, and depth pitch for a linear subresource.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  image              Vulkan image whose resources are queried or modified
 * \param[in]  subresource        Aspect, mip level, and array layer whose layout is queried
 * \param[out] pLayout            Receives the queried image subresource layout
 *
 * \retval    None
 */
void Gfx_Vulkan_GetImageSubresourceLayout(VkDevice device, VkImage image, VkImageSubresource subresource, VkSubresourceLayout* pLayout);

/*************************************************************************************************/
/**
 * \brief Creates a persistent descriptor-set layout from complete binding records.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags              Descriptor set layout creation flags
 * \param[in]  bindingCount       Number of descriptor bindings supplied for the layout
 * \param[in]  pBindings          Descriptor-set-layout bindings containing bindingCount entries
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pSetLayout         Receives the created descriptor-set layout
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateDescriptorSetLayout(VkDevice                            device,
                                          const void*                         pNext,
                                          VkDescriptorSetLayoutCreateFlags    flags,
                                          uint32_t                            bindingCount,
                                          const VkDescriptorSetLayoutBinding* pBindings,
                                          const VkAllocationCallbacks*        pAllocator,
                                          VkDescriptorSetLayout*              pSetLayout);

/*************************************************************************************************/
/**
 * \brief Creates a persistent descriptor pool with explicit set and descriptor capacities.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags              Descriptor pool creation flags
 * \param[in]  maxSets            Maximum number of descriptor sets allocated from the pool
 * \param[in]  poolSizeCount      Number of entries in pPoolSizes
 * \param[in]  pPoolSizes         Descriptor pool-size array containing poolSizeCount entries
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pDescriptorPool    Receives the created descriptor pool
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateDescriptorPool(VkDevice                     device,
                                     const void*                  pNext,
                                     VkDescriptorPoolCreateFlags  flags,
                                     uint32_t                     maxSets,
                                     uint32_t                     poolSizeCount,
                                     const VkDescriptorPoolSize*  pPoolSizes,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkDescriptorPool*            pDescriptorPool);

/*************************************************************************************************/
/**
 * \brief Allocates descriptor sets from a persistent pool using parallel layout entries.
 *
 * \param[in]  device                Non-null logical device that owns the referenced resources
 * \param[in]  pNext                 Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  descriptorPool        Non-null descriptor pool that supplies the descriptor sets
 * \param[in]  descriptorSetCount    Number of descriptor sets to allocate or bind
 * \param[in]  pSetLayouts           Descriptor-set-layout array containing the declared set or descriptor-set count
 * \param[out] pDescriptorSets       Descriptor-set array containing descriptorSetCount entries
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateDescriptorSets(VkDevice                     device,
                                       const void*                  pNext,
                                       VkDescriptorPool             descriptorPool,
                                       uint32_t                     descriptorSetCount,
                                       const VkDescriptorSetLayout* pSetLayouts,
                                       VkDescriptorSet*             pDescriptorSets);

/*************************************************************************************************/
/**
 * \brief Creates a one-set descriptor layout, matching pool, and allocated descriptor set.
 *
 * \par Details:
 *  All per-binding arrays contain bindingCount parallel elements.
 *
 * \param[in]  device                  Non-null logical device that owns the referenced resources
 * \param[in]  pLayoutNext             Optional extension chain attached to the composed descriptor-set-layout structure, or NULL
 * \param[in]  layoutFlags             Descriptor-set layout creation flags
 * \param[in]  bindingCount            Number of descriptor bindings supplied for the layout
 * \param[in]  pBindings               Descriptor-set-layout bindings containing bindingCount entries
 * \param[in]  pDescriptorTypes        Descriptor types parallel to the binding array
 * \param[in]  pDescriptorCounts       Descriptor counts parallel to the binding array
 * \param[in]  pStageFlags             Shader-stage masks parallel to the binding array
 * \param[in]  ppImmutableSamplers     Optional immutable-sampler arrays parallel to the binding array
 * \param[in]  pPoolNext               Optional extension chain attached to the composed descriptor-pool structure, or NULL
 * \param[in]  poolFlags               Descriptor-pool creation flags
 * \param[in]  pAllocator              Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pDescriptorSetLayout    Receives the created descriptor-set layout
 * \param[out] pDescriptorPool         Receives the created descriptor pool
 * \param[out] pDescriptorSet          Receives the allocated descriptor set
 *
 * \retval    None
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
                                    VkDescriptorSet*                 pDescriptorSet);

/*************************************************************************************************/
/**
 * \brief Applies arrays of descriptor writes and descriptor copies.
 *
 * \param[in] device               Logical device owning the descriptor sets
 * \param[in] descriptorWriteCount Number of entries in pDescriptorWrites
 * \param[in] pDescriptorWrites    Descriptor writes, or NULL when the count is zero
 * \param[in] descriptorCopyCount  Number of entries in pDescriptorCopies
 * \param[in] pDescriptorCopies    Descriptor copies, or NULL when the count is zero
 *
 * \retval    None
 */
void Gfx_Vulkan_UpdateDescriptorSets(VkDevice                    device,
                                     uint32_t                    descriptorWriteCount,
                                     const VkWriteDescriptorSet* pDescriptorWrites,
                                     uint32_t                    descriptorCopyCount,
                                     const VkCopyDescriptorSet*  pDescriptorCopies);

/*************************************************************************************************/
/**
 * \brief Writes descriptors at a set binding and array element using caller-supplied write fields.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] pNext               Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in] dstSet              Non-null destination descriptor set to update
 * \param[in] dstBinding          First destination binding to update
 * \param[in] dstArrayElement     First array element within dstBinding to update
 * \param[in] descriptorCount     Number of consecutive descriptors to update
 * \param[in] descriptorType      Type of descriptor written at the selected binding
 * \param[in] pImageInfo          Image descriptor records, or NULL when descriptorType does not consume them
 * \param[in] pBufferInfo         Buffer descriptor records, or NULL when descriptorType does not consume them
 * \param[in] pTexelBufferView    Texel-buffer views, or NULL when descriptorType does not consume them
 *
 * \retval    None
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
                                    const VkBufferView*           pTexelBufferView);

/*************************************************************************************************/
/**
 * \brief Writes one sampler/image descriptor at a set binding and array element.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] pNext               Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in] dstSet              Non-null destination descriptor set to update
 * \param[in] dstBinding          First destination binding to update
 * \param[in] dstArrayElement     First array element within dstBinding to update
 * \param[in] descriptorType      Type of descriptor written at the selected binding
 * \param[in] sampler             Sampler for sampler descriptors; ignored by other descriptor types
 * \param[in] imageView           Image view referenced by the descriptor
 * \param[in] imageLayout         Layout the image will have when the descriptor is accessed
 *
 * \retval    None
 */
void Gfx_Vulkan_UpdateImageDescriptorSet(VkDevice         device,
                                         const void*      pNext,
                                         VkDescriptorSet  dstSet,
                                         uint32_t         dstBinding,
                                         uint32_t         dstArrayElement,
                                         VkDescriptorType descriptorType,
                                         VkSampler        sampler,
                                         VkImageView      imageView,
                                         VkImageLayout    imageLayout);

/*************************************************************************************************/
/**
 * \brief Writes one buffer descriptor range at a set binding and array element.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] pNext               Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in] dstSet              Non-null destination descriptor set to update
 * \param[in] dstBinding          First destination binding to update
 * \param[in] dstArrayElement     First array element within dstBinding to update
 * \param[in] descriptorType      Type of descriptor written at the selected binding
 * \param[in] buffer              Vulkan buffer whose resources are queried or modified
 * \param[in] offset              Byte offset from the beginning of the allocation or resource
 * \param[in] range               Descriptor-visible byte range, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_UpdateBufferDescriptorSet(VkDevice         device,
                                          const void*      pNext,
                                          VkDescriptorSet  dstSet,
                                          uint32_t         dstBinding,
                                          uint32_t         dstArrayElement,
                                          VkDescriptorType descriptorType,
                                          VkBuffer         buffer,
                                          VkDeviceSize     offset,
                                          VkDeviceSize     range);

/*************************************************************************************************/
/**
 * \brief Creates a persistent pipeline layout from descriptor layouts and push-constant ranges.
 *
 * \param[in]  device                    Non-null logical device that owns the referenced resources
 * \param[in]  pNext                     Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags                     Pipeline layout creation flags
 * \param[in]  setLayoutCount            Number of entries in pSetLayouts
 * \param[in]  pSetLayouts               Descriptor-set-layout array containing the declared set or descriptor-set count
 * \param[in]  pushConstantRangeCount    Number of entries in pPushConstantRanges
 * \param[in]  pPushConstantRanges       Push-constant ranges containing pushConstantRangeCount entries
 * \param[in]  pAllocator                Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pPipelineLayout           Receives the created pipeline layout
 *
 * \retval    None
 */
void Gfx_Vulkan_CreatePipelineLayout(VkDevice                     device,
                                     const void*                  pNext,
                                     VkPipelineLayoutCreateFlags  flags,
                                     uint32_t                     setLayoutCount,
                                     const VkDescriptorSetLayout* pSetLayouts,
                                     uint32_t                     pushConstantRangeCount,
                                     const VkPushConstantRange*   pPushConstantRanges,
                                     const VkAllocationCallbacks* pAllocator,
                                     VkPipelineLayout*            pPipelineLayout);

/*************************************************************************************************/
/**
 * \brief Creates a compute pipeline layout with one optional compute push-constant range.
 *
 * \par Details:
 *  A zero pushConstantSize omits the range; otherwise offset and size must meet device limits.
 *
 * \param[in]  device                Non-null logical device that owns the referenced resources
 * \param[in]  pNext                 Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags                 Compute pipeline layout creation flags
 * \param[in]  setLayoutCount        Number of entries in pSetLayouts
 * \param[in]  pSetLayouts           Descriptor-set-layout array containing the declared set or descriptor-set count
 * \param[in]  pushConstantOffset    Byte offset within the push-constant range
 * \param[in]  pushConstantSize      Number of push-constant bytes
 * \param[in]  pAllocator            Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pPipelineLayout       Receives the created pipeline layout
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateComputePipelineLayout(VkDevice                     device,
                                            const void*                  pNext,
                                            VkPipelineLayoutCreateFlags  flags,
                                            uint32_t                     setLayoutCount,
                                            const VkDescriptorSetLayout* pSetLayouts,
                                            uint32_t                     pushConstantOffset,
                                            uint32_t                     pushConstantSize,
                                            const VkAllocationCallbacks* pAllocator,
                                            VkPipelineLayout*            pPipelineLayout);

/*************************************************************************************************/
/**
 * \brief Creates a shader module from SPIR-V; codeSize must be nonzero and divisible by four.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags              Shader module creation flags
 * \param[in]  codeSize           SPIR-V byte count; nonzero and divisible by four
 * \param[in]  pCode              Non-null array of codeSize bytes containing valid SPIR-V words
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pShaderModule      Receives the created shader module
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateShaderModule(VkDevice                     device,
                                   const void*                  pNext,
                                   VkShaderModuleCreateFlags    flags,
                                   size_t                       codeSize,
                                   const uint32_t*              pCode,
                                   const VkAllocationCallbacks* pAllocator,
                                   VkShaderModule*              pShaderModule);

/*************************************************************************************************/
/**
 * \brief Creates an array of compute pipelines from complete Vulkan create infos.
 *
 * \param[in]  device           Logical device creating the pipelines
 * \param[in]  pipelineCache    Pipeline cache, or VK_NULL_HANDLE
 * \param[in]  createInfoCount  Number of entries in pCreateInfos and pPipelines
 * \param[in]  pCreateInfos     Complete compute-pipeline create infos
 * \param[in]  pAllocator       Optional Vulkan allocation callbacks, or NULL
 * \param[out] pPipelines       Receives createInfoCount compute pipelines
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateComputePipelines(VkDevice                           device,
                                       VkPipelineCache                    pipelineCache,
                                       uint32_t                           createInfoCount,
                                       const VkComputePipelineCreateInfo* pCreateInfos,
                                       const VkAllocationCallbacks*       pAllocator,
                                       VkPipeline*                        pPipelines);

/*************************************************************************************************/
/**
 * \brief Creates a compute pipeline from a caller-supplied stage and base-pipeline selection.
 *
 * \param[in]  device                Non-null logical device that owns the referenced resources
 * \param[in]  pipelineCache         Pipeline cache used for creation, or VK_NULL_HANDLE
 * \param[in]  pNext                 Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags                 Compute pipeline creation flags
 * \param[in]  stage                 Compute shader-stage description
 * \param[in]  layout                Non-null pipeline layout defining descriptor sets or push constants
 * \param[in]  basePipelineHandle    Base pipeline handle, or VK_NULL_HANDLE when derivative creation does not use one
 * \param[in]  basePipelineIndex     Base-pipeline index, or -1 when derivative creation does not use one
 * \param[in]  pAllocator            Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pPipeline             Receives the created compute pipeline
 *
 * \retval    None
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
                                      VkPipeline*                     pPipeline);

/*************************************************************************************************/
/**
 * \brief Creates a shader module, compute stage, pipeline layout, and compute pipeline.
 *
 * \param[in]  device                 Non-null logical device that owns the resources
 * \param[in]  pPipelineLayoutNext    Optional pipeline-layout extension chain, or NULL
 * \param[in]  pipelineLayoutFlags    Pipeline-layout creation flags
 * \param[in]  setLayoutCount         Number of entries in pSetLayouts
 * \param[in]  pSetLayouts            Descriptor-set layouts, or NULL when the count is zero
 * \param[in]  pushConstantOffset     Compute push-constant byte offset
 * \param[in]  pushConstantSize       Compute push-constant byte count, or zero
 * \param[in]  pShaderModuleNext      Optional shader-module extension chain, or NULL
 * \param[in]  shaderModuleFlags      Shader-module creation flags
 * \param[in]  shaderCodeSize         SPIR-V byte count
 * \param[in]  pShaderCode            Non-null SPIR-V words
 * \param[in]  pipelineCache          Pipeline cache, or VK_NULL_HANDLE
 * \param[in]  pPipelineNext          Optional compute-pipeline extension chain, or NULL
 * \param[in]  pipelineFlags          Compute-pipeline creation flags
 * \param[in]  pStageNext             Optional shader-stage extension chain, or NULL
 * \param[in]  stageFlags             Shader-stage creation flags
 * \param[in]  pEntryPoint            Non-empty compute entry-point name
 * \param[in]  pSpecializationInfo    Optional specialization constants, or NULL
 * \param[in]  basePipelineHandle     Base pipeline, or VK_NULL_HANDLE
 * \param[in]  basePipelineIndex      Base pipeline index, or -1
 * \param[in]  pAllocator             Optional Vulkan allocation callbacks, or NULL
 * \param[out] pShaderModule          Receives the created shader module
 * \param[out] pPipelineLayout        Receives the created pipeline layout
 * \param[out] pPipeline              Receives the created compute pipeline
 *
 * \retval    None
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
                                     VkPipeline*                      pPipeline);

/*************************************************************************************************/
/**
 * \brief Creates a persistent pipeline cache from optional implementation-compatible initial data.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags              Pipeline cache creation flags
 * \param[in]  initialDataSize    Number of bytes in pInitialData; zero creates an empty cache
 * \param[in]  pInitialData       Implementation-compatible pipeline-cache bytes, or NULL when initialDataSize is zero
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pPipelineCache     Receives the created pipeline cache
 *
 * \retval    None
 */
void Gfx_Vulkan_CreatePipelineCache(VkDevice                     device,
                                    const void*                  pNext,
                                    VkPipelineCacheCreateFlags   flags,
                                    size_t                       initialDataSize,
                                    const void*                  pInitialData,
                                    const VkAllocationCallbacks* pAllocator,
                                    VkPipelineCache*             pPipelineCache);

/*************************************************************************************************/
/**
 * \brief Creates a persistent command pool for one queue family.
 *
 * \param[in]  device              Non-null logical device that owns the referenced resources
 * \param[in]  pNext               Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags               Command pool creation flags
 * \param[in]  queueFamilyIndex    Queue-family index selected for the operation
 * \param[in]  pAllocator          Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pCommandPool        Receives the created command pool
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateCommandPool(VkDevice device, const void* pNext, VkCommandPoolCreateFlags flags, uint32_t queueFamilyIndex, const VkAllocationCallbacks* pAllocator, VkCommandPool* pCommandPool);

/*************************************************************************************************/
/**
 * \brief Allocates persistent primary or secondary command buffers from a command pool.
 *
 * \param[in]  device                Non-null logical device that owns the referenced resources
 * \param[in]  pNext                 Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  commandPool           Non-null command pool that owns the command-buffer storage
 * \param[in]  level                 Primary or secondary command-buffer level
 * \param[in]  commandBufferCount    Number of command buffers to allocate
 * \param[out] pCommandBuffers       Command-buffer array containing commandBufferCount entries, or NULL when the count is zero
 *
 * \retval    None
 */
void Gfx_Vulkan_AllocateCommandBuffers(VkDevice device, const void* pNext, VkCommandPool commandPool, VkCommandBufferLevel level, uint32_t commandBufferCount, VkCommandBuffer* pCommandBuffers);

/*************************************************************************************************/
/**
 * \brief Creates a command pool and allocates command buffers from it.
 *
 * \param[in]  device                Non-null logical device that owns the referenced resources
 * \param[in]  pCommandPoolNext      Optional extension chain attached to the composed command-pool structure, or NULL
 * \param[in]  commandPoolFlags      Command-pool creation flags
 * \param[in]  queueFamilyIndex      Queue-family index selected for the operation
 * \param[in]  pCommandBufferNext    Optional extension chain attached to the composed command-buffer structure, or NULL
 * \param[in]  commandBufferLevel    Primary or secondary command-buffer level
 * \param[in]  commandBufferCount    Number of command buffers to allocate from the created pool
 * \param[in]  pAllocator            Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pCommandPool          Receives the created command pool
 * \param[out] pCommandBuffers       Receives commandBufferCount allocated command buffers
 *
 * \retval    None
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
                                 VkCommandBuffer*             pCommandBuffers);

/*************************************************************************************************/
/**
 * \brief Resets a command pool and every command buffer allocated from it.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] commandPool         Non-null command pool that owns the command-buffer storage
 * \param[in] flags               Command-pool reset flags
 *
 * \retval    None
 */
void Gfx_Vulkan_ResetCommandPool(VkDevice device, VkCommandPool commandPool, VkCommandPoolResetFlags flags);

/*************************************************************************************************/
/**
 * \brief Begins command recording with an optional extension chain, explicit usage, and optional inheritance information.
 *
 * \param[in] commandBuffer       Command buffer ready to begin recording
 * \param[in] pNext               Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in] flags               Command-buffer usage flags
 * \param[in] pInheritanceInfo    Optional secondary-command-buffer inheritance information
 *
 * \retval    None
 */
void Gfx_Vulkan_BeginCommandBuffer(VkCommandBuffer commandBuffer, const void* pNext, VkCommandBufferUsageFlags flags, const VkCommandBufferInheritanceInfo* pInheritanceInfo);

/*************************************************************************************************/
/**
 * \brief Ends recording and makes a command buffer executable.
 *
 * \param[in] commandBuffer       Command buffer currently recording
 *
 * \retval    None
 */
void Gfx_Vulkan_EndCommandBuffer(VkCommandBuffer commandBuffer);

/*************************************************************************************************/
/**
 * \brief Records execution and memory dependencies between pipeline stages.
 *
 * \param[in] commandBuffer               Command buffer receiving the recorded commands
 * \param[in] srcStageMask                Stages whose prior accesses must complete
 * \param[in] dstStageMask                Stages whose subsequent accesses depend on the barrier
 * \param[in] dependencyFlags             Flags specifying the scope of the dependency
 * \param[in] memoryBarrierCount          Number of entries in pMemoryBarriers
 * \param[in] pMemoryBarriers             Global memory barriers containing memoryBarrierCount entries, or NULL when the count is zero
 * \param[in] bufferMemoryBarrierCount    Number of entries in pBufferMemoryBarriers
 * \param[in] pBufferMemoryBarriers       Buffer barriers containing bufferMemoryBarrierCount entries, or NULL when the count is zero
 * \param[in] imageMemoryBarrierCount     Number of entries in pImageMemoryBarriers
 * \param[in] pImageMemoryBarriers        Image barriers containing imageMemoryBarrierCount entries, or NULL when the count is zero
 *
 * \retval    None
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
                                   const VkImageMemoryBarrier*  pImageMemoryBarriers);

/*************************************************************************************************/
/**
 * \brief Constructs and records an image barrier with explicit queue ownership.
 *
 * \param[in] commandBuffer          Command buffer receiving the recorded commands
 * \param[in] srcStageMask           Stages whose prior accesses must complete
 * \param[in] dstStageMask           Stages whose subsequent accesses depend on the barrier
 * \param[in] dependencyFlags        Flags specifying the scope of the dependency
 * \param[in] pNext                  Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in] srcAccessMask          Prior accesses made available by the barrier
 * \param[in] dstAccessMask          Subsequent accesses made visible by the barrier
 * \param[in] oldLayout              Image layout expected before the barrier
 * \param[in] newLayout              Image layout established by the barrier
 * \param[in] srcQueueFamilyIndex    Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] dstQueueFamilyIndex    Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] image                  Vulkan image whose resources are queried or modified
 * \param[in] subresourceRange       Image aspects, mip levels, and array layers exposed by the view
 *
 * \retval    None
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
                                              VkImageSubresourceRange subresourceRange);

/*************************************************************************************************/
/**
 * \brief Records an image barrier while retaining its current ownership.
 *
 * \par Details:
 *  Preserves queue-family ownership; both queue-family indices are VK_QUEUE_FAMILY_IGNORED.
 *
 * \param[in] commandBuffer      Command buffer receiving the barrier
 * \param[in] srcStageMask       Stage producing the previous image state
 * \param[in] dstStageMask       Stage consuming the next image state
 * \param[in] dependencyFlags    Dependency flags
 * \param[in] pNext              Optional image barrier extension chain, or NULL
 * \param[in] srcAccessMask      Previous image accesses
 * \param[in] dstAccessMask      Subsequent image accesses
 * \param[in] oldLayout          Image layout before the barrier
 * \param[in] newLayout          Image layout after the barrier
 * \param[in] image              Image transitioned by the barrier
 * \param[in] subresourceRange   Image subresources transitioned by the barrier
 *
 * \retval    None
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
                                VkImageSubresourceRange subresourceRange);

/*************************************************************************************************/
/**
 * \brief Acquires an externally owned image.
 *
 * \par Details:
 *  Acquires ownership from VK_QUEUE_FAMILY_EXTERNAL with no source access and TOP_OF_PIPE as the source stage.
 *
 * \param[in] commandBuffer          Command buffer receiving the barrier
 * \param[in] dstStageMask           Stage consuming the acquired image
 * \param[in] dependencyFlags        Dependency flags
 * \param[in] pNext                  Optional image barrier extension chain, or NULL
 * \param[in] dstAccessMask          Accesses consuming the acquired image
 * \param[in] oldLayout              Image layout before acquisition
 * \param[in] newLayout              Image layout after acquisition
 * \param[in] dstQueueFamilyIndex    Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] image                  Image acquired from external ownership
 * \param[in] subresourceRange       Image subresources acquired by the barrier
 *
 * \retval    None
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
                                            VkImageSubresourceRange subresourceRange);

/*************************************************************************************************/
/**
 * \brief Releases an image to external ownership.
 *
 * \par Details:
 *  Releases ownership to VK_QUEUE_FAMILY_EXTERNAL with no destination access and BOTTOM_OF_PIPE as the destination stage.
 *
 * \param[in] commandBuffer          Command buffer receiving the barrier
 * \param[in] srcStageMask           Stage producing the released image
 * \param[in] dependencyFlags        Dependency flags
 * \param[in] pNext                  Optional image barrier extension chain, or NULL
 * \param[in] srcAccessMask          Accesses producing the released image
 * \param[in] oldLayout              Image layout before release
 * \param[in] newLayout              Image layout after release
 * \param[in] srcQueueFamilyIndex    Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] image                  Image released to external ownership
 * \param[in] subresourceRange       Image subresources released by the barrier
 *
 * \retval    None
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
                                          VkImageSubresourceRange subresourceRange);

/*************************************************************************************************/
/**
 * \brief Constructs and records a buffer barrier with explicit queue ownership.
 *
 * \param[in] commandBuffer          Command buffer receiving the recorded commands
 * \param[in] srcStageMask           Stages whose prior accesses must complete
 * \param[in] dstStageMask           Stages whose subsequent accesses depend on the barrier
 * \param[in] dependencyFlags        Flags specifying the scope of the dependency
 * \param[in] pNext                  Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in] srcAccessMask          Prior accesses made available by the barrier
 * \param[in] dstAccessMask          Subsequent accesses made visible by the barrier
 * \param[in] srcQueueFamilyIndex    Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] dstQueueFamilyIndex    Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] buffer                 Vulkan buffer whose resources are queried or modified
 * \param[in] offset                 Byte offset from the beginning of the allocation or resource
 * \param[in] size                   Size in bytes, or VK_WHOLE_SIZE where supported
 *
 * \retval    None
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
                                               VkDeviceSize         size);

/*************************************************************************************************/
/**
 * \brief Records a buffer barrier while retaining its current ownership.
 *
 * \par Details:
 *  Preserves queue-family ownership; both queue-family indices are VK_QUEUE_FAMILY_IGNORED.
 *
 * \param[in] commandBuffer      Command buffer receiving the barrier
 * \param[in] srcStageMask       Stage producing the previous buffer state
 * \param[in] dstStageMask       Stage consuming the next buffer state
 * \param[in] dependencyFlags    Dependency flags
 * \param[in] pNext              Optional buffer barrier extension chain, or NULL
 * \param[in] srcAccessMask      Previous buffer accesses
 * \param[in] dstAccessMask      Subsequent buffer accesses
 * \param[in] buffer             Buffer synchronized by the barrier
 * \param[in] offset             First byte in the synchronized range
 * \param[in] size               Synchronized byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
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
                                 VkDeviceSize         size);

/*************************************************************************************************/
/**
 * \brief Acquires an externally owned buffer.
 *
 * \par Details:
 *  Acquires ownership from VK_QUEUE_FAMILY_EXTERNAL with no source access and TOP_OF_PIPE as the source stage.
 *
 * \param[in] commandBuffer          Command buffer receiving the barrier
 * \param[in] dstStageMask           Stage consuming the acquired buffer
 * \param[in] dependencyFlags        Dependency flags
 * \param[in] pNext                  Optional buffer barrier extension chain, or NULL
 * \param[in] dstAccessMask          Accesses consuming the acquired buffer
 * \param[in] dstQueueFamilyIndex    Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] buffer                 Buffer acquired from external ownership
 * \param[in] offset                 First byte in the acquired range
 * \param[in] size                   Acquired byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdAcquireBufferFromExternal(VkCommandBuffer      commandBuffer,
                                             VkPipelineStageFlags dstStageMask,
                                             VkDependencyFlags    dependencyFlags,
                                             const void*          pNext,
                                             VkAccessFlags        dstAccessMask,
                                             uint32_t             dstQueueFamilyIndex,
                                             VkBuffer             buffer,
                                             VkDeviceSize         offset,
                                             VkDeviceSize         size);

/*************************************************************************************************/
/**
 * \brief Releases a buffer to external ownership.
 *
 * \par Details:
 *  Releases ownership to VK_QUEUE_FAMILY_EXTERNAL with no destination access and BOTTOM_OF_PIPE as the destination stage.
 *
 * \param[in] commandBuffer          Command buffer receiving the barrier
 * \param[in] srcStageMask           Stage producing the released buffer
 * \param[in] dependencyFlags        Dependency flags
 * \param[in] pNext                  Optional buffer barrier extension chain, or NULL
 * \param[in] srcAccessMask          Accesses producing the released buffer
 * \param[in] srcQueueFamilyIndex    Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] buffer                 Buffer released to external ownership
 * \param[in] offset                 First byte in the released range
 * \param[in] size                   Released byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdReleaseBufferToExternal(VkCommandBuffer      commandBuffer,
                                           VkPipelineStageFlags srcStageMask,
                                           VkDependencyFlags    dependencyFlags,
                                           const void*          pNext,
                                           VkAccessFlags        srcAccessMask,
                                           uint32_t             srcQueueFamilyIndex,
                                           VkBuffer             buffer,
                                           VkDeviceSize         offset,
                                           VkDeviceSize         size);

/*************************************************************************************************/
/**
 * \brief Transitions a buffer from caller-supplied state to compute shader writes.
 *
 * \param[in] commandBuffer       Command buffer receiving the barrier
 * \param[in] srcStageMask        Stage that previously accessed the buffer
 * \param[in] dependencyFlags     Dependency flags
 * \param[in] pNext               Optional buffer barrier extension chain, or NULL
 * \param[in] srcAccessMask       Previous buffer accesses
 * \param[in] srcQueueFamilyIndex Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] dstQueueFamilyIndex Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] buffer              Buffer transitioned for compute writes
 * \param[in] offset              First byte in the transitioned range
 * \param[in] size                Transitioned byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
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
                                                           VkDeviceSize         size);

/*************************************************************************************************/
/**
 * \brief Transitions a buffer to compute shader writes without an ownership transfer.
 *
 * \param[in] commandBuffer   Command buffer receiving the barrier
 * \param[in] srcStageMask    Stage that previously accessed the buffer
 * \param[in] dependencyFlags Dependency flags
 * \param[in] pNext           Optional buffer barrier extension chain, or NULL
 * \param[in] srcAccessMask   Previous buffer accesses
 * \param[in] buffer          Buffer transitioned for compute writes
 * \param[in] offset          First byte in the transitioned range
 * \param[in] size            Transitioned byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdAcquireComputeWriteBuffer(VkCommandBuffer      commandBuffer,
                                             VkPipelineStageFlags srcStageMask,
                                             VkDependencyFlags    dependencyFlags,
                                             const void*          pNext,
                                             VkAccessFlags        srcAccessMask,
                                             VkBuffer             buffer,
                                             VkDeviceSize         offset,
                                             VkDeviceSize         size);

/*************************************************************************************************/
/**
 * \brief Acquires an externally owned buffer for compute shader writes.
 *
 * \param[in] commandBuffer       Command buffer receiving the barrier
 * \param[in] dependencyFlags     Dependency flags
 * \param[in] pNext               Optional buffer barrier extension chain, or NULL
 * \param[in] dstQueueFamilyIndex Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] buffer              Buffer transitioned for compute writes
 * \param[in] offset              First byte in the transitioned range
 * \param[in] size                Transitioned byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdAcquireComputeWriteBufferFromExternal(VkCommandBuffer   commandBuffer,
                                                         VkDependencyFlags dependencyFlags,
                                                         const void*       pNext,
                                                         uint32_t          dstQueueFamilyIndex,
                                                         VkBuffer          buffer,
                                                         VkDeviceSize      offset,
                                                         VkDeviceSize      size);

/*************************************************************************************************/
/**
 * \brief Transitions a host-read buffer for compute shader writes.
 *
 * \param[in] commandBuffer   Command buffer receiving the barrier
 * \param[in] dependencyFlags Dependency flags
 * \param[in] pNext           Optional buffer barrier extension chain, or NULL
 * \param[in] buffer          Buffer transitioned for compute writes
 * \param[in] offset          First byte in the transitioned range
 * \param[in] size            Transitioned byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdAcquireComputeWriteBufferFromHostRead(VkCommandBuffer commandBuffer, VkDependencyFlags dependencyFlags, const void* pNext, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);

/*************************************************************************************************/
/**
 * \brief Transitions a transfer-read buffer for compute shader writes.
 *
 * \param[in] commandBuffer   Command buffer receiving the barrier
 * \param[in] dependencyFlags Dependency flags
 * \param[in] pNext           Optional buffer barrier extension chain, or NULL
 * \param[in] buffer          Buffer transitioned for compute writes
 * \param[in] offset          First byte in the transitioned range
 * \param[in] size            Transitioned byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdAcquireComputeWriteBufferFromTransferRead(VkCommandBuffer commandBuffer, VkDependencyFlags dependencyFlags, const void* pNext, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);

/*************************************************************************************************/
/**
 * \brief Transitions a compute shader-written buffer to caller-supplied state.
 *
 * \param[in] commandBuffer          Command buffer receiving the barrier
 * \param[in] dstStageMask          Stage that next consumes the buffer
 * \param[in] dependencyFlags       Dependency flags
 * \param[in] pNext                 Optional buffer barrier extension chain, or NULL
 * \param[in] dstAccessMask         Subsequent buffer accesses
 * \param[in] srcQueueFamilyIndex   Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] dstQueueFamilyIndex   Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] buffer                Compute shader-written buffer
 * \param[in] offset                First byte in the transitioned range
 * \param[in] size                  Transitioned byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
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
                                                      VkDeviceSize         size);

/*************************************************************************************************/
/**
 * \brief Transitions a compute shader-written buffer without an ownership transfer.
 *
 * \param[in] commandBuffer      Command buffer receiving the barrier
 * \param[in] dstStageMask       Stage that next consumes the buffer
 * \param[in] dependencyFlags    Dependency flags
 * \param[in] pNext              Optional buffer barrier extension chain, or NULL
 * \param[in] dstAccessMask      Subsequent buffer accesses
 * \param[in] buffer             Compute shader-written buffer
 * \param[in] offset             First byte in the transitioned range
 * \param[in] size               Transitioned byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdReleaseComputeBuffer(VkCommandBuffer      commandBuffer,
                                        VkPipelineStageFlags dstStageMask,
                                        VkDependencyFlags    dependencyFlags,
                                        const void*          pNext,
                                        VkAccessFlags        dstAccessMask,
                                        VkBuffer             buffer,
                                        VkDeviceSize         offset,
                                        VkDeviceSize         size);

/*************************************************************************************************/
/**
 * \brief Releases a compute shader-written buffer to external ownership.
 *
 * \param[in] commandBuffer         Command buffer receiving the barrier
 * \param[in] dependencyFlags      Dependency flags
 * \param[in] pNext                Optional buffer barrier extension chain, or NULL
 * \param[in] srcQueueFamilyIndex  Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] buffer               Compute shader-written buffer
 * \param[in] offset               First byte in the transitioned range
 * \param[in] size                 Transitioned byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdReleaseComputeBufferToExternal(VkCommandBuffer   commandBuffer,
                                                  VkDependencyFlags dependencyFlags,
                                                  const void*       pNext,
                                                  uint32_t          srcQueueFamilyIndex,
                                                  VkBuffer          buffer,
                                                  VkDeviceSize      offset,
                                                  VkDeviceSize      size);

/*************************************************************************************************/
/**
 * \brief Makes a compute shader-written buffer available for host reads.
 *
 * \param[in] commandBuffer     Command buffer receiving the barrier
 * \param[in] dependencyFlags  Dependency flags
 * \param[in] pNext            Optional buffer barrier extension chain, or NULL
 * \param[in] buffer           Compute shader-written buffer
 * \param[in] offset           First byte in the transitioned range
 * \param[in] size             Transitioned byte count, or VK_WHOLE_SIZE
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdReleaseComputeBufferToHostRead(VkCommandBuffer commandBuffer, VkDependencyFlags dependencyFlags, const void* pNext, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size);

/*************************************************************************************************/
/**
 * \brief Computes buffer-image copy regions for all format planes without recording commands.
 *
 * \par Details:
 *  pRegions must hold Gfx_Vulkan_GetFormatPlaneCount(imageFormat) elements.
 *  Returns the region count and writes the transfer byte count (excluding bufferOffset) and combined aspect mask.
 *  Offsets must be nonnegative and aligned to each plane's subsampling divisors.
 *
 * \param[in]  bufferOffset          Byte offset in the transfer buffer
 * \param[in]  bufferRowAlignment    Required byte alignment for buffer rows used by the transfer
 * \param[in]  imageFormat           Format of the image participating in the transfer
 * \param[in]  imageOffset           Origin of the image copy region in texels
 * \param[in]  imageExtent           Dimensions of the image copy region in texels
 * \param[in]  imageSubresource      Image aspects, mip level, and array layers copied
 * \param[out] pRegions              Array with capacity for one region per format plane
 * \param[out] pTransferByteCount    Receives the total number of buffer bytes covered by the generated regions
 * \param[out] pAspectMask           Receives the union of image aspects covered by the generated regions
 *
 */
uint32_t Gfx_Vulkan_GetBufferImageCopyRegions(VkDeviceSize             bufferOffset,
                                              VkDeviceSize             bufferRowAlignment,
                                              VkFormat                 imageFormat,
                                              VkOffset3D               imageOffset,
                                              VkExtent3D               imageExtent,
                                              VkImageSubresourceLayers imageSubresource,
                                              VkBufferImageCopy*       pRegions,
                                              VkDeviceSize*            pTransferByteCount,
                                              VkImageAspectFlags*      pAspectMask);

/*************************************************************************************************/
/**
 * \brief Records buffer acquisition, image acquisition, a full buffer-to-image copy, and release.
 *
 * \par Details:
 *  Copy regions account for imageFormat plane geometry and bufferRowAlignment. The image ends in finalImageLayout.
 *
 * \param[in] commandBuffer                      Command buffer receiving the recorded commands
 * \param[in] dependencyFlags                    Flags specifying the scope of the dependency
 * \param[in] pBufferBarrierNext                 Optional extension chain attached to the composed buffer barrier, or NULL
 * \param[in] bufferSrcStageMask                 Stages of buffer accesses preceding the copy
 * \param[in] bufferSrcAccessMask                Buffer accesses preceding the copy
 * \param[in] bufferSrcQueueFamilyIndex          Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] bufferDstQueueFamilyIndex          Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] buffer                             Vulkan buffer whose resources are queried or modified
 * \param[in] bufferOffset                       Byte offset in the transfer buffer
 * \param[in] bufferRowAlignment                 Required byte alignment for buffer rows used by the transfer
 * \param[in] image                              Vulkan image whose resources are queried or modified
 * \param[in] imageFormat                        Format of the image participating in the transfer
 * \param[in] transferImageLayout                Image layout used while the transfer command executes
 * \param[in] imageOffset                        Origin of the image copy region in texels
 * \param[in] imageExtent                        Dimensions of the image copy region in texels
 * \param[in] imageSubresource                   Image aspects, mip level, and array layers copied
 * \param[in] pImageAcquireBarrierNext           Optional extension chain attached to the composed image-acquire barrier, or NULL
 * \param[in] imageSrcStageMask                  Stages of image accesses preceding the copy
 * \param[in] imageSrcAccessMask                 Image accesses preceding the copy
 * \param[in] initialImageLayout                 Image layout expected before the recorded transfer
 * \param[in] imageAcquireSrcQueueFamilyIndex    Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageAcquireDstQueueFamilyIndex    Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] pImageBarrierNext                  Optional extension chain attached to the composed final image barrier, or NULL
 * \param[in] imageDstStageMask                  Stages of image accesses following the copy
 * \param[in] imageDstAccessMask                 Image accesses following the copy
 * \param[in] finalImageLayout                   Image layout established after the recorded transfer
 * \param[in] imageSrcQueueFamilyIndex           Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageDstQueueFamilyIndex           Queue-family ownership after the barrier, or a Vulkan special queue-family index
 *
 * \retval    None
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
                                                       uint32_t                 imageDstQueueFamilyIndex);

/*************************************************************************************************/
/**
 * \brief Records a two-dimensional buffer-to-image transfer with explicit synchronization.
 *
 * \param[in] commandBuffer                      Command buffer receiving the transfer
 * \param[in] dependencyFlags                    Dependency flags shared by the barriers
 * \param[in] pBufferBarrierNext                 Optional buffer barrier extension chain, or NULL
 * \param[in] bufferSrcStageMask                 Stage producing the source buffer data
 * \param[in] bufferSrcAccessMask                Access producing the source buffer data
 * \param[in] bufferSrcQueueFamilyIndex          Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] bufferDstQueueFamilyIndex          Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] buffer                             Source buffer
 * \param[in] bufferOffset                       Source buffer byte offset
 * \param[in] bufferRowAlignment                 Source buffer row alignment
 * \param[in] image                              Destination image
 * \param[in] imageFormat                        Destination image format
 * \param[in] transferImageLayout                Destination layout during transfer
 * \param[in] imageOffset                        Two-dimensional destination offset
 * \param[in] imageExtent                        Two-dimensional transfer extent
 * \param[in] imageSubresource                   Destination mip level and array layers
 * \param[in] pImageAcquireBarrierNext           Optional image acquisition barrier extension chain
 * \param[in] imageSrcStageMask                  Stage producing the prior image state
 * \param[in] imageSrcAccessMask                 Access producing the prior image state
 * \param[in] initialImageLayout                 Image layout before transfer
 * \param[in] imageAcquireSrcQueueFamilyIndex    Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageAcquireDstQueueFamilyIndex    Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] pImageBarrierNext                  Optional final image barrier extension chain
 * \param[in] imageDstStageMask                  Stage consuming the transferred image
 * \param[in] imageDstAccessMask                 Access consuming the transferred image
 * \param[in] finalImageLayout                   Image layout after transfer
 * \param[in] imageSrcQueueFamilyIndex           Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageDstQueueFamilyIndex           Queue-family ownership after the barrier, or a Vulkan special queue-family index
 *
 * \retval    None
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
                                                         uint32_t                 imageDstQueueFamilyIndex);

/*************************************************************************************************/
/**
 * \brief Transfers a buffer into an image while retaining current ownership.
 *
 * \par Details:
 *  Preserves ownership of the source buffer and destination image.
 *
 * \param[in] commandBuffer              Command buffer receiving the transfer
 * \param[in] dependencyFlags            Dependency flags shared by all barriers
 * \param[in] pBufferBarrierNext         Optional source-buffer barrier extension chain
 * \param[in] bufferSrcStageMask         Stage producing the source buffer
 * \param[in] bufferSrcAccessMask        Access producing the source buffer
 * \param[in] buffer                     Source buffer
 * \param[in] bufferOffset               Source buffer byte offset
 * \param[in] bufferRowAlignment         Source buffer row alignment
 * \param[in] image                      Destination image
 * \param[in] imageFormat                Destination image format
 * \param[in] transferImageLayout        Destination image layout during transfer
 * \param[in] imageOffset                Destination image offset
 * \param[in] imageExtent                Transfer extent
 * \param[in] imageSubresource           Destination mip level and array layers
 * \param[in] pImageAcquireBarrierNext   Optional pre-copy image barrier extension chain
 * \param[in] imageSrcStageMask          Stage producing the previous image state
 * \param[in] imageSrcAccessMask         Access producing the previous image state
 * \param[in] initialImageLayout         Image layout before transfer
 * \param[in] pImageBarrierNext          Optional post-copy image barrier extension chain
 * \param[in] imageDstStageMask          Stage consuming the transferred image
 * \param[in] imageDstAccessMask         Access consuming the transferred image
 * \param[in] finalImageLayout           Image layout after transfer
 *
 * \retval    None
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
                                         VkImageLayout            finalImageLayout);

/*************************************************************************************************/
/**
 * \brief Transfers a buffer into a two-dimensional image while retaining current ownership.
 *
 * \param[in] commandBuffer              Command buffer receiving the transfer
 * \param[in] dependencyFlags            Dependency flags shared by all barriers
 * \param[in] pBufferBarrierNext         Optional source-buffer barrier extension chain
 * \param[in] bufferSrcStageMask         Stage producing the source buffer
 * \param[in] bufferSrcAccessMask        Access producing the source buffer
 * \param[in] buffer                     Source buffer
 * \param[in] bufferOffset               Source buffer byte offset
 * \param[in] bufferRowAlignment         Source buffer row alignment
 * \param[in] image                      Destination image
 * \param[in] imageFormat                Destination image format
 * \param[in] transferImageLayout        Destination image layout during transfer
 * \param[in] imageOffset                Two-dimensional destination offset
 * \param[in] imageExtent                Two-dimensional transfer extent
 * \param[in] imageSubresource           Destination mip level and array layers
 * \param[in] pImageAcquireBarrierNext   Optional pre-copy image barrier extension chain
 * \param[in] imageSrcStageMask          Stage producing the previous image state
 * \param[in] imageSrcAccessMask         Access producing the previous image state
 * \param[in] initialImageLayout         Image layout before transfer
 * \param[in] pImageBarrierNext          Optional post-copy image barrier extension chain
 * \param[in] imageDstStageMask          Stage consuming the transferred image
 * \param[in] imageDstAccessMask         Access consuming the transferred image
 * \param[in] finalImageLayout           Image layout after transfer
 *
 * \retval    None
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
                                           VkImageLayout            finalImageLayout);

/*************************************************************************************************/
/**
 * \brief Transfers a buffer into an image acquired from and released to external ownership.
 *
 * \par Details:
 *  The buffer retains its queue-family ownership. The image is acquired from VK_QUEUE_FAMILY_EXTERNAL
 *  and released back to it after the copy.
 *
 * \param[in] commandBuffer              Command buffer receiving the transfer
 * \param[in] dependencyFlags            Dependency flags shared by all barriers
 * \param[in] pBufferBarrierNext         Optional source-buffer barrier extension chain
 * \param[in] bufferSrcStageMask         Stage producing the source buffer
 * \param[in] bufferSrcAccessMask        Access producing the source buffer
 * \param[in] buffer                     Source buffer
 * \param[in] bufferOffset               Source buffer byte offset
 * \param[in] bufferRowAlignment         Source buffer row alignment
 * \param[in] image                      Externally owned destination image
 * \param[in] imageFormat                Destination image format
 * \param[in] transferImageLayout        Destination image layout during transfer
 * \param[in] imageOffset                Destination image offset
 * \param[in] imageExtent                Transfer extent
 * \param[in] imageSubresource           Destination mip level and array layers
 * \param[in] pImageAcquireBarrierNext   Optional external-acquire barrier extension chain
 * \param[in] initialImageLayout         Image layout before acquisition
 * \param[in] pImageReleaseBarrierNext   Optional external-release barrier extension chain
 * \param[in] finalImageLayout           Image layout after release
 * \param[in] queueFamilyIndex           Queue family performing the transfer
 *
 * \retval    None
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
                                                 uint32_t                 queueFamilyIndex);

/*************************************************************************************************/
/**
 * \brief Transfers a compute shader-written buffer into an image and transitions the image.
 *
 * \param[in] commandBuffer                         Command buffer receiving the transfer
 * \param[in] dependencyFlags                       Dependency flags shared by all barriers
 * \param[in] pBufferBarrierNext                    Optional buffer barrier extension chain
 * \param[in] bufferSrcQueueFamilyIndex             Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] bufferDstQueueFamilyIndex             Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] buffer                                Compute shader-written source buffer
 * \param[in] bufferOffset                          Source buffer byte offset
 * \param[in] bufferRowAlignment                    Source buffer row alignment
 * \param[in] image                                 Destination image
 * \param[in] imageFormat                           Destination image format
 * \param[in] transferImageLayout                   Destination image layout during transfer
 * \param[in] imageOffset                           Destination image offset
 * \param[in] imageExtent                           Destination image extent
 * \param[in] imageSubresource                      Destination mip level and array layers
 * \param[in] pImageAcquireBarrierNext              Optional pre-copy image barrier extension chain
 * \param[in] imageSrcStageMask                     Stage producing the destination image state
 * \param[in] imageSrcAccessMask                    Previous destination image accesses
 * \param[in] initialImageLayout                    Destination image layout before transfer
 * \param[in] imageAcquireSrcQueueFamilyIndex       Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageAcquireDstQueueFamilyIndex       Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] pImageBarrierNext                     Optional post-copy image barrier extension chain
 * \param[in] imageDstStageMask                     Stage consuming the destination image
 * \param[in] imageDstAccessMask                    Subsequent destination image accesses
 * \param[in] finalImageLayout                      Destination image layout after transfer
 * \param[in] imageSrcQueueFamilyIndex              Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageDstQueueFamilyIndex              Queue-family ownership after the barrier, or a Vulkan special queue-family index
 *
 * \retval    None
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
                                                              uint32_t                 imageDstQueueFamilyIndex);

/*************************************************************************************************/
/**
 * \brief Transfers a compute shader-written buffer into a two-dimensional image.
 *
 * \param[in] commandBuffer                         Command buffer receiving the transfer
 * \param[in] dependencyFlags                       Dependency flags shared by the barriers
 * \param[in] pBufferBarrierNext                    Optional source-buffer barrier extension chain
 * \param[in] bufferSrcQueueFamilyIndex             Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] bufferDstQueueFamilyIndex             Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] buffer                                Compute shader-written source buffer
 * \param[in] bufferOffset                          Source buffer byte offset
 * \param[in] bufferRowAlignment                    Source buffer row alignment
 * \param[in] image                                 Destination image
 * \param[in] imageFormat                           Destination image format
 * \param[in] transferImageLayout                   Destination image layout during transfer
 * \param[in] imageOffset                           Two-dimensional destination offset
 * \param[in] imageExtent                           Two-dimensional transfer extent
 * \param[in] imageSubresource                      Destination mip level and array layers
 * \param[in] pImageAcquireBarrierNext              Optional pre-copy image barrier extension chain
 * \param[in] imageSrcStageMask                     Stage producing the prior image state
 * \param[in] imageSrcAccessMask                    Access producing the prior image state
 * \param[in] initialImageLayout                    Image layout before transfer
 * \param[in] imageAcquireSrcQueueFamilyIndex       Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageAcquireDstQueueFamilyIndex       Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] pImageBarrierNext                     Optional final image barrier extension chain
 * \param[in] imageDstStageMask                     Stage consuming the image after transfer
 * \param[in] imageDstAccessMask                    Access consuming the image after transfer
 * \param[in] finalImageLayout                      Image layout after transfer
 * \param[in] imageSrcQueueFamilyIndex              Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageDstQueueFamilyIndex              Queue-family ownership after the barrier, or a Vulkan special queue-family index
 *
 * \retval    None
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
                                                                uint32_t                 imageDstQueueFamilyIndex);

/*************************************************************************************************/
/**
 * \brief Transfers a compute shader-written buffer into an externally owned image.
 *
 * \param[in] commandBuffer              Command buffer receiving the transfer
 * \param[in] dependencyFlags            Dependency flags shared by all barriers
 * \param[in] pBufferBarrierNext         Optional source-buffer barrier extension chain
 * \param[in] buffer                     Compute shader-written source buffer
 * \param[in] bufferOffset               Source buffer byte offset
 * \param[in] bufferRowAlignment         Source buffer row alignment
 * \param[in] image                      Externally owned destination image
 * \param[in] imageFormat                Destination image format
 * \param[in] transferImageLayout        Destination image layout during transfer
 * \param[in] imageOffset                Destination image offset
 * \param[in] imageExtent                Transfer extent
 * \param[in] imageSubresource           Destination mip level and array layers
 * \param[in] pImageAcquireBarrierNext   Optional external-acquire barrier extension chain
 * \param[in] initialImageLayout         Image layout before acquisition
 * \param[in] pImageReleaseBarrierNext   Optional external-release barrier extension chain
 * \param[in] finalImageLayout           Image layout after release
 * \param[in] queueFamilyIndex           Queue family performing the transfer
 *
 * \retval    None
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
                                                        uint32_t                 queueFamilyIndex);

/*************************************************************************************************/
/**
 * \brief Transfers a compute shader-written buffer into a two-dimensional external image.
 *
 * \param[in] commandBuffer              Command buffer receiving the transfer
 * \param[in] dependencyFlags            Dependency flags shared by all barriers
 * \param[in] pBufferBarrierNext         Optional source-buffer barrier extension chain
 * \param[in] buffer                     Compute shader-written source buffer
 * \param[in] bufferOffset               Source buffer byte offset
 * \param[in] bufferRowAlignment         Source buffer row alignment
 * \param[in] image                      Externally owned destination image
 * \param[in] imageFormat                Destination image format
 * \param[in] transferImageLayout        Destination image layout during transfer
 * \param[in] imageOffset                Two-dimensional destination offset
 * \param[in] imageExtent                Two-dimensional transfer extent
 * \param[in] imageSubresource           Destination mip level and array layers
 * \param[in] pImageAcquireBarrierNext   Optional external-acquire barrier extension chain
 * \param[in] initialImageLayout         Image layout before acquisition
 * \param[in] pImageReleaseBarrierNext   Optional external-release barrier extension chain
 * \param[in] finalImageLayout           Image layout after release
 * \param[in] queueFamilyIndex           Queue family performing the transfer
 *
 * \retval    None
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
                                                          uint32_t                 queueFamilyIndex);

/*************************************************************************************************/
/**
 * \brief Transitions an image and buffer, copies all format planes to the buffer, and transitions both for subsequent use.
 *
 * \par Details:
 *  Copy regions account for format plane geometry and row alignment. imageSubresource selects one mip level and one or more layers.
 *
 * \param[in] commandBuffer                         Command buffer receiving the transfer
 * \param[in] dependencyFlags                       Dependency flags shared by all barriers
 * \param[in] pImageAcquireBarrierNext              Optional pre-copy image barrier extension chain
 * \param[in] imageSrcStageMask                     Stage producing the image before the copy
 * \param[in] imageSrcAccessMask                    Accesses producing the image before the copy
 * \param[in] initialImageLayout                    Image layout before the copy
 * \param[in] imageAcquireSrcQueueFamilyIndex       Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageAcquireDstQueueFamilyIndex       Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] image                                  Source image
 * \param[in] imageFormat                            Source image format
 * \param[in] transferImageLayout                    Image layout used during the copy
 * \param[in] imageOffset                            Source image offset
 * \param[in] imageExtent                            Source image extent
 * \param[in] imageSubresource                       Source mip level and array layers
 * \param[in] pBufferAcquireBarrierNext             Optional pre-copy buffer barrier extension chain
 * \param[in] bufferSrcStageMask                    Stage accessing the buffer before the copy
 * \param[in] bufferSrcAccessMask                   Accesses to the buffer before the copy
 * \param[in] bufferAcquireSrcQueueFamilyIndex      Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] bufferAcquireDstQueueFamilyIndex      Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] buffer                                 Destination buffer
 * \param[in] bufferOffset                           Destination buffer byte offset
 * \param[in] bufferRowAlignment                     Required buffer row alignment
 * \param[in] pImageBarrierNext                     Optional post-copy image barrier extension chain
 * \param[in] imageDstStageMask                     Stage consuming the image after the copy
 * \param[in] imageDstAccessMask                    Accesses consuming the image after the copy
 * \param[in] finalImageLayout                      Image layout after the copy
 * \param[in] imageSrcQueueFamilyIndex              Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageDstQueueFamilyIndex              Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] pBufferBarrierNext                    Optional post-copy buffer barrier extension chain
 * \param[in] bufferDstStageMask                    Stage consuming the buffer after the copy
 * \param[in] bufferDstAccessMask                   Accesses consuming the buffer after the copy
 * \param[in] bufferSrcQueueFamilyIndex             Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] bufferDstQueueFamilyIndex             Queue-family ownership after the barrier, or a Vulkan special queue-family index
 *
 * \retval    None
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
                                                       uint32_t                 bufferDstQueueFamilyIndex);

/*************************************************************************************************/
/**
 * \brief Records a two-dimensional image-to-buffer transfer with explicit synchronization.
 *
 * \param[in] commandBuffer                       Command buffer receiving the transfer
 * \param[in] dependencyFlags                     Dependency flags shared by the barriers
 * \param[in] pImageAcquireBarrierNext            Optional image acquisition barrier extension chain
 * \param[in] imageSrcStageMask                   Stage producing the source image
 * \param[in] imageSrcAccessMask                  Access producing the source image
 * \param[in] initialImageLayout                  Source image layout before transfer
 * \param[in] imageAcquireSrcQueueFamilyIndex     Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageAcquireDstQueueFamilyIndex     Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] image                               Source image
 * \param[in] imageFormat                         Source image format
 * \param[in] transferImageLayout                 Source image layout during transfer
 * \param[in] imageOffset                         Two-dimensional source offset
 * \param[in] imageExtent                         Two-dimensional transfer extent
 * \param[in] imageSubresource                    Source mip level and array layers
 * \param[in] pBufferAcquireBarrierNext           Optional buffer acquisition barrier extension chain
 * \param[in] bufferSrcStageMask                  Stage producing the prior buffer state
 * \param[in] bufferSrcAccessMask                 Access producing the prior buffer state
 * \param[in] bufferAcquireSrcQueueFamilyIndex    Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] bufferAcquireDstQueueFamilyIndex    Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] buffer                              Destination buffer
 * \param[in] bufferOffset                        Destination buffer byte offset
 * \param[in] bufferRowAlignment                  Destination buffer row alignment
 * \param[in] pImageBarrierNext                   Optional final image barrier extension chain
 * \param[in] imageDstStageMask                   Stage consuming the source image after transfer
 * \param[in] imageDstAccessMask                  Access consuming the source image after transfer
 * \param[in] finalImageLayout                    Source image layout after transfer
 * \param[in] imageSrcQueueFamilyIndex            Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] imageDstQueueFamilyIndex            Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] pBufferBarrierNext                  Optional final buffer barrier extension chain
 * \param[in] bufferDstStageMask                  Stage consuming the destination buffer
 * \param[in] bufferDstAccessMask                 Access consuming the destination buffer
 * \param[in] bufferSrcQueueFamilyIndex           Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] bufferDstQueueFamilyIndex           Queue-family ownership after the barrier, or a Vulkan special queue-family index
 *
 * \retval    None
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
                                                         uint32_t                 bufferDstQueueFamilyIndex);

/*************************************************************************************************/
/**
 * \brief Transfers an image into a buffer while retaining current ownership.
 *
 * \par Details:
 *  Preserves ownership of the source image and destination buffer.
 *
 * \param[in] commandBuffer                Command buffer receiving the transfer
 * \param[in] dependencyFlags              Dependency flags shared by all barriers
 * \param[in] pImageAcquireBarrierNext     Optional pre-copy image barrier extension chain
 * \param[in] imageSrcStageMask            Stage producing the source image
 * \param[in] imageSrcAccessMask           Access producing the source image
 * \param[in] initialImageLayout           Image layout before transfer
 * \param[in] image                        Source image
 * \param[in] imageFormat                  Source image format
 * \param[in] transferImageLayout          Image layout during transfer
 * \param[in] imageOffset                  Source image offset
 * \param[in] imageExtent                  Transfer extent
 * \param[in] imageSubresource             Source mip level and array layers
 * \param[in] pBufferAcquireBarrierNext    Optional pre-copy buffer barrier extension chain
 * \param[in] bufferSrcStageMask           Stage producing the previous buffer state
 * \param[in] bufferSrcAccessMask          Access producing the previous buffer state
 * \param[in] buffer                       Destination buffer
 * \param[in] bufferOffset                 Destination buffer byte offset
 * \param[in] bufferRowAlignment           Destination buffer row alignment
 * \param[in] pImageBarrierNext            Optional post-copy image barrier extension chain
 * \param[in] imageDstStageMask            Stage consuming the source image after transfer
 * \param[in] imageDstAccessMask           Access consuming the source image after transfer
 * \param[in] finalImageLayout             Image layout after transfer
 * \param[in] pBufferBarrierNext           Optional post-copy buffer barrier extension chain
 * \param[in] bufferDstStageMask           Stage consuming the destination buffer
 * \param[in] bufferDstAccessMask          Access consuming the destination buffer
 *
 * \retval    None
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
                                         VkAccessFlags            bufferDstAccessMask);

/*************************************************************************************************/
/**
 * \brief Transfers a two-dimensional image into a buffer while retaining current ownership.
 *
 * \param[in] commandBuffer                Command buffer receiving the transfer
 * \param[in] dependencyFlags              Dependency flags shared by all barriers
 * \param[in] pImageAcquireBarrierNext     Optional pre-copy image barrier extension chain
 * \param[in] imageSrcStageMask            Stage producing the source image
 * \param[in] imageSrcAccessMask           Access producing the source image
 * \param[in] initialImageLayout           Image layout before transfer
 * \param[in] image                        Source image
 * \param[in] imageFormat                  Source image format
 * \param[in] transferImageLayout          Image layout during transfer
 * \param[in] imageOffset                  Two-dimensional source offset
 * \param[in] imageExtent                  Two-dimensional transfer extent
 * \param[in] imageSubresource             Source mip level and array layers
 * \param[in] pBufferAcquireBarrierNext    Optional pre-copy buffer barrier extension chain
 * \param[in] bufferSrcStageMask           Stage producing the previous buffer state
 * \param[in] bufferSrcAccessMask          Access producing the previous buffer state
 * \param[in] buffer                       Destination buffer
 * \param[in] bufferOffset                 Destination buffer byte offset
 * \param[in] bufferRowAlignment           Destination buffer row alignment
 * \param[in] pImageBarrierNext            Optional post-copy image barrier extension chain
 * \param[in] imageDstStageMask            Stage consuming the source image after transfer
 * \param[in] imageDstAccessMask           Access consuming the source image after transfer
 * \param[in] finalImageLayout             Image layout after transfer
 * \param[in] pBufferBarrierNext           Optional post-copy buffer barrier extension chain
 * \param[in] bufferDstStageMask           Stage consuming the destination buffer
 * \param[in] bufferDstAccessMask          Access consuming the destination buffer
 *
 * \retval    None
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
                                           VkAccessFlags            bufferDstAccessMask);

/*************************************************************************************************/
/**
 * \brief Transitions two images, copies one region, and transitions both images for subsequent use.
 *
 * \par Details:
 *  Source and destination images have independent layout transitions and queue-family ownership transfers.
 *
 * \param[in] commandBuffer                    Command buffer receiving the transfer
 * \param[in] dependencyFlags                  Dependency flags shared by all barriers
 * \param[in] pSrcAcquireBarrierNext           Optional pre-copy source barrier extension chain
 * \param[in] srcStageMask                     Stage producing the source image
 * \param[in] srcAccessMask                    Accesses producing the source image
 * \param[in] srcInitialLayout                 Source layout before the copy
 * \param[in] srcTransferLayout                Source layout during the copy
 * \param[in] srcAcquireQueueFamilyIndex       Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] srcTransferQueueFamilyIndex      Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] srcImage                         Source image
 * \param[in] srcSubresource                   Source mip level and array layers
 * \param[in] srcOffset                        Source image offset
 * \param[in] pDstAcquireBarrierNext           Optional pre-copy destination barrier extension chain
 * \param[in] dstStageMask                     Stage producing the destination image state
 * \param[in] dstAccessMask                    Accesses to the destination before the copy
 * \param[in] dstInitialLayout                 Destination layout before the copy
 * \param[in] dstTransferLayout                Destination layout during the copy
 * \param[in] dstAcquireQueueFamilyIndex       Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] dstTransferQueueFamilyIndex      Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] dstImage                         Destination image
 * \param[in] dstSubresource                   Destination mip level and array layers
 * \param[in] dstOffset                        Destination image offset
 * \param[in] extent                           Copy extent
 * \param[in] pSrcBarrierNext                  Optional post-copy source barrier extension chain
 * \param[in] srcDstStageMask                  Stage consuming the source after the copy
 * \param[in] srcDstAccessMask                 Accesses consuming the source after the copy
 * \param[in] srcFinalLayout                   Source layout after the copy
 * \param[in] srcReleaseQueueFamilyIndex       Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] srcDstQueueFamilyIndex           Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] pDstBarrierNext                  Optional post-copy destination barrier extension chain
 * \param[in] dstDstStageMask                  Stage consuming the destination after the copy
 * \param[in] dstDstAccessMask                 Accesses consuming the destination after the copy
 * \param[in] dstFinalLayout                   Destination layout after the copy
 * \param[in] dstReleaseQueueFamilyIndex       Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] dstDstQueueFamilyIndex           Queue-family ownership after the barrier, or a Vulkan special queue-family index
 *
 * \retval    None
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
                                                      uint32_t                 dstDstQueueFamilyIndex);

/*************************************************************************************************/
/**
 * \brief Transfers one two-dimensional image region into another with explicit synchronization.
 *
 * \param[in] commandBuffer                    Command buffer receiving the transfer
 * \param[in] dependencyFlags                  Dependency flags shared by all barriers
 * \param[in] pSrcAcquireBarrierNext           Optional pre-copy source barrier extension chain
 * \param[in] srcStageMask                     Stage producing the source image
 * \param[in] srcAccessMask                    Access producing the source image
 * \param[in] srcInitialLayout                 Source layout before transfer
 * \param[in] srcTransferLayout                Source layout during transfer
 * \param[in] srcAcquireQueueFamilyIndex       Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] srcTransferQueueFamilyIndex      Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] srcImage                         Source image
 * \param[in] srcSubresource                   Source mip level and array layers
 * \param[in] srcOffset                        Two-dimensional source offset
 * \param[in] pDstAcquireBarrierNext           Optional pre-copy destination barrier extension chain
 * \param[in] dstStageMask                     Stage producing the prior destination state
 * \param[in] dstAccessMask                    Access producing the prior destination state
 * \param[in] dstInitialLayout                 Destination layout before transfer
 * \param[in] dstTransferLayout                Destination layout during transfer
 * \param[in] dstAcquireQueueFamilyIndex       Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] dstTransferQueueFamilyIndex      Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] dstImage                         Destination image
 * \param[in] dstSubresource                   Destination mip level and array layers
 * \param[in] dstOffset                        Two-dimensional destination offset
 * \param[in] extent                           Two-dimensional transfer extent
 * \param[in] pSrcBarrierNext                  Optional final source barrier extension chain
 * \param[in] srcDstStageMask                  Stage consuming the source after transfer
 * \param[in] srcDstAccessMask                 Access consuming the source after transfer
 * \param[in] srcFinalLayout                   Source layout after transfer
 * \param[in] srcReleaseQueueFamilyIndex       Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] srcDstQueueFamilyIndex           Queue-family ownership before the barrier, or a Vulkan special queue-family index
 * \param[in] pDstBarrierNext                  Optional final destination barrier extension chain
 * \param[in] dstDstStageMask                  Stage consuming the destination after transfer
 * \param[in] dstDstAccessMask                 Access consuming the destination after transfer
 * \param[in] dstFinalLayout                   Destination layout after transfer
 * \param[in] dstReleaseQueueFamilyIndex       Queue-family ownership after the barrier, or a Vulkan special queue-family index
 * \param[in] dstDstQueueFamilyIndex           Queue-family ownership after the barrier, or a Vulkan special queue-family index
 *
 * \retval    None
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
                                                        uint32_t                 dstDstQueueFamilyIndex);

/*************************************************************************************************/
/**
 * \brief Transfers between images while retaining current ownership.
 *
 * \par Details:
 *  Preserves ownership of both images.
 *
 * \param[in] commandBuffer              Command buffer receiving the transfer
 * \param[in] dependencyFlags            Dependency flags shared by all barriers
 * \param[in] pSrcAcquireBarrierNext     Optional pre-copy source barrier extension chain
 * \param[in] srcStageMask               Stage producing the source image
 * \param[in] srcAccessMask              Access producing the source image
 * \param[in] srcInitialLayout           Source layout before transfer
 * \param[in] srcTransferLayout          Source layout during transfer
 * \param[in] srcImage                   Source image
 * \param[in] srcSubresource             Source mip level and array layers
 * \param[in] srcOffset                  Source image offset
 * \param[in] pDstAcquireBarrierNext     Optional pre-copy destination barrier extension chain
 * \param[in] dstStageMask               Stage producing the destination image state
 * \param[in] dstAccessMask              Access producing the destination image state
 * \param[in] dstInitialLayout           Destination layout before transfer
 * \param[in] dstTransferLayout          Destination layout during transfer
 * \param[in] dstImage                   Destination image
 * \param[in] dstSubresource             Destination mip level and array layers
 * \param[in] dstOffset                  Destination image offset
 * \param[in] extent                     Transfer extent
 * \param[in] pSrcBarrierNext            Optional post-copy source barrier extension chain
 * \param[in] srcDstStageMask            Stage consuming the source image after transfer
 * \param[in] srcDstAccessMask           Access consuming the source image after transfer
 * \param[in] srcFinalLayout             Source layout after transfer
 * \param[in] pDstBarrierNext            Optional post-copy destination barrier extension chain
 * \param[in] dstDstStageMask            Stage consuming the destination image after transfer
 * \param[in] dstDstAccessMask           Access consuming the destination image after transfer
 * \param[in] dstFinalLayout             Destination layout after transfer
 *
 * \retval    None
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
                                        VkImageLayout            dstFinalLayout);

/*************************************************************************************************/
/**
 * \brief Transfers between two-dimensional images while retaining current ownership.
 *
 * \param[in] commandBuffer              Command buffer receiving the transfer
 * \param[in] dependencyFlags            Dependency flags shared by all barriers
 * \param[in] pSrcAcquireBarrierNext     Optional pre-copy source barrier extension chain
 * \param[in] srcStageMask               Stage producing the source image
 * \param[in] srcAccessMask              Access producing the source image
 * \param[in] srcInitialLayout           Source layout before transfer
 * \param[in] srcTransferLayout          Source layout during transfer
 * \param[in] srcImage                   Source image
 * \param[in] srcSubresource             Source mip level and array layers
 * \param[in] srcOffset                  Two-dimensional source offset
 * \param[in] pDstAcquireBarrierNext     Optional pre-copy destination barrier extension chain
 * \param[in] dstStageMask               Stage producing the destination image state
 * \param[in] dstAccessMask              Access producing the destination image state
 * \param[in] dstInitialLayout           Destination layout before transfer
 * \param[in] dstTransferLayout          Destination layout during transfer
 * \param[in] dstImage                   Destination image
 * \param[in] dstSubresource             Destination mip level and array layers
 * \param[in] dstOffset                  Two-dimensional destination offset
 * \param[in] extent                     Two-dimensional transfer extent
 * \param[in] pSrcBarrierNext            Optional post-copy source barrier extension chain
 * \param[in] srcDstStageMask            Stage consuming the source image after transfer
 * \param[in] srcDstAccessMask           Access consuming the source image after transfer
 * \param[in] srcFinalLayout             Source layout after transfer
 * \param[in] pDstBarrierNext            Optional post-copy destination barrier extension chain
 * \param[in] dstDstStageMask            Stage consuming the destination image after transfer
 * \param[in] dstDstAccessMask           Access consuming the destination image after transfer
 * \param[in] dstFinalLayout             Destination layout after transfer
 *
 * \retval    None
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
                                          VkImageLayout            dstFinalLayout);

/*************************************************************************************************/
/**
 * \brief Blits between images with explicit pre- and post-transfer synchronization.
 *
 * \par Details:
 *  Preserves ownership of both images. Each supplied subresource determines its image's barrier range.
 *
 * \param[in] commandBuffer            Command buffer receiving the transfer
 * \param[in] dependencyFlags          Dependency flags shared by all barriers
 * \param[in] pSrcAcquireBarrierNext   Optional pre-blit source barrier extension chain
 * \param[in] srcStageMask             Stage producing the source image
 * \param[in] srcAccessMask            Access producing the source image
 * \param[in] srcInitialLayout         Source layout before transfer
 * \param[in] srcTransferLayout        Source layout during transfer
 * \param[in] srcImage                 Source image
 * \param[in] srcSubresource           Source mip level and array layers
 * \param[in] srcOffset                Source region origin
 * \param[in] srcExtent                Source region extent
 * \param[in] pDstAcquireBarrierNext   Optional pre-blit destination barrier extension chain
 * \param[in] dstStageMask             Stage producing the destination image state
 * \param[in] dstAccessMask            Access producing the destination image state
 * \param[in] dstInitialLayout         Destination layout before transfer
 * \param[in] dstTransferLayout        Destination layout during transfer
 * \param[in] dstImage                 Destination image
 * \param[in] dstSubresource           Destination mip level and array layers
 * \param[in] dstOffset                Destination region origin
 * \param[in] dstExtent                Destination region extent
 * \param[in] filter                   Reconstruction filter used by the blit
 * \param[in] pSrcBarrierNext          Optional post-blit source barrier extension chain
 * \param[in] srcDstStageMask          Stage consuming the source image after transfer
 * \param[in] srcDstAccessMask         Access consuming the source image after transfer
 * \param[in] srcFinalLayout           Source layout after transfer
 * \param[in] pDstBarrierNext          Optional post-blit destination barrier extension chain
 * \param[in] dstDstStageMask          Stage consuming the destination image after transfer
 * \param[in] dstDstAccessMask         Access consuming the destination image after transfer
 * \param[in] dstFinalLayout           Destination layout after transfer
 *
 * \retval    None
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
                                     VkImageLayout            dstFinalLayout);

/*************************************************************************************************/
/**
 * \brief Blits between two-dimensional images with explicit synchronization.
 *
 * \param[in] commandBuffer            Command buffer receiving the transfer
 * \param[in] dependencyFlags          Dependency flags shared by all barriers
 * \param[in] pSrcAcquireBarrierNext   Optional pre-blit source barrier extension chain
 * \param[in] srcStageMask             Stage producing the source image
 * \param[in] srcAccessMask            Access producing the source image
 * \param[in] srcInitialLayout         Source layout before transfer
 * \param[in] srcTransferLayout        Source layout during transfer
 * \param[in] srcImage                 Source image
 * \param[in] srcSubresource           Source mip level and array layers
 * \param[in] srcOffset                Two-dimensional source region origin
 * \param[in] srcExtent                Two-dimensional source region extent
 * \param[in] pDstAcquireBarrierNext   Optional pre-blit destination barrier extension chain
 * \param[in] dstStageMask             Stage producing the destination image state
 * \param[in] dstAccessMask            Access producing the destination image state
 * \param[in] dstInitialLayout         Destination layout before transfer
 * \param[in] dstTransferLayout        Destination layout during transfer
 * \param[in] dstImage                 Destination image
 * \param[in] dstSubresource           Destination mip level and array layers
 * \param[in] dstOffset                Two-dimensional destination region origin
 * \param[in] dstExtent                Two-dimensional destination region extent
 * \param[in] filter                   Reconstruction filter used by the blit
 * \param[in] pSrcBarrierNext          Optional post-blit source barrier extension chain
 * \param[in] srcDstStageMask          Stage consuming the source image after transfer
 * \param[in] srcDstAccessMask         Access consuming the source image after transfer
 * \param[in] srcFinalLayout           Source layout after transfer
 * \param[in] pDstBarrierNext          Optional post-blit destination barrier extension chain
 * \param[in] dstDstStageMask          Stage consuming the destination image after transfer
 * \param[in] dstDstAccessMask         Access consuming the destination image after transfer
 * \param[in] dstFinalLayout           Destination layout after transfer
 *
 * \retval    None
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
                                       VkImageLayout            dstFinalLayout);

/*************************************************************************************************/
/**
 * \brief Records explicit buffer-to-image copy regions into a command buffer.
 *
 * \param[in]  commandBuffer      Command buffer receiving the recorded commands
 * \param[in]  srcBuffer          Non-null source buffer for the copy
 * \param[in]  dstImage           Non-null destination image for the copy
 * \param[in]  dstImageLayout     Layout of the destination image during the copy
 * \param[in]  regionCount        Number of copy regions in pRegions
 * \param[in]  pRegions           Array of regionCount buffer/image copy regions
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdCopyBufferToImage(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkImage dstImage, VkImageLayout dstImageLayout, uint32_t regionCount, const VkBufferImageCopy* pRegions);

/*************************************************************************************************/
/**
 * \brief Records explicit image-to-buffer copy regions into a command buffer.
 *
 * \param[in]  commandBuffer      Command buffer receiving the recorded commands
 * \param[in]  srcImage           Non-null source image for the copy
 * \param[in]  srcImageLayout     Layout of the source image during the copy
 * \param[in]  dstBuffer          Non-null destination buffer for the copy
 * \param[in]  regionCount        Number of copy regions in pRegions
 * \param[in]  pRegions           Array of regionCount buffer/image copy regions
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions);

/*************************************************************************************************/
/**
 * \brief Records explicit image-to-image copy regions into a command buffer.
 *
 * \param[in] commandBuffer   Command buffer receiving the copy
 * \param[in] srcImage        Source image
 * \param[in] srcImageLayout  Source image layout during the copy
 * \param[in] dstImage        Destination image
 * \param[in] dstImageLayout  Destination image layout during the copy
 * \param[in] regionCount     Number of entries in pRegions
 * \param[in] pRegions        Image-copy regions
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdCopyImageRegions(VkCommandBuffer    commandBuffer,
                                    VkImage            srcImage,
                                    VkImageLayout      srcImageLayout,
                                    VkImage            dstImage,
                                    VkImageLayout      dstImageLayout,
                                    uint32_t           regionCount,
                                    const VkImageCopy* pRegions);

/*************************************************************************************************/
/**
 * \brief Records one same-sized image region copy between explicit subresources and offsets.
 *
 * \param[in] commandBuffer       Command buffer receiving the recorded commands
 * \param[in] srcImage            Non-null source image for the copy
 * \param[in] srcImageLayout      Layout of the source image during the copy
 * \param[in] srcSubresource      Source image aspects, mip level, and array layers
 * \param[in] srcOffset           Source region origin in texels
 * \param[in] dstImage            Non-null destination image for the copy
 * \param[in] dstImageLayout      Layout of the destination image during the copy
 * \param[in] dstSubresource      Destination image aspects, mip level, and array layers
 * \param[in] dstOffset           Destination region origin in texels
 * \param[in] extent              Image dimensions in texels
 *
 * \retval    None
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
                             VkExtent3D               extent);

/*************************************************************************************************/
/**
 * \brief Records explicit image blit regions into a command buffer.
 *
 * \param[in] commandBuffer   Command buffer receiving the blit
 * \param[in] srcImage        Source image
 * \param[in] srcImageLayout  Source image layout during the blit
 * \param[in] dstImage        Destination image
 * \param[in] dstImageLayout  Destination image layout during the blit
 * \param[in] regionCount     Number of entries in pRegions
 * \param[in] pRegions        Image-blit regions
 * \param[in] filter          Reconstruction filter used by the blit
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdBlitImageRegions(VkCommandBuffer    commandBuffer,
                                    VkImage            srcImage,
                                    VkImageLayout      srcImageLayout,
                                    VkImage            dstImage,
                                    VkImageLayout      dstImageLayout,
                                    uint32_t           regionCount,
                                    const VkImageBlit* pRegions,
                                    VkFilter           filter);

/*************************************************************************************************/
/**
 * \brief Records one image blit between explicit three-dimensional regions.
 *
 * \par Details:
 *  The caller must establish the supplied layouts and synchronize access to both images.
 *
 * \param[in] commandBuffer   Command buffer receiving the blit
 * \param[in] srcImage        Source image
 * \param[in] srcImageLayout  Source image layout during the blit
 * \param[in] srcSubresource  Source mip level and array layers
 * \param[in] srcOffset       Source region origin
 * \param[in] srcExtent       Source region extent
 * \param[in] dstImage        Destination image
 * \param[in] dstImageLayout  Destination image layout during the blit
 * \param[in] dstSubresource  Destination mip level and array layers
 * \param[in] dstOffset       Destination region origin
 * \param[in] dstExtent       Destination region extent
 * \param[in] filter          Reconstruction filter used by the blit
 *
 * \retval    None
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
                             VkFilter                 filter);

/*************************************************************************************************/
/**
 * \brief Records one two-dimensional image blit.
 *
 * \param[in] commandBuffer   Command buffer receiving the blit
 * \param[in] srcImage        Source image
 * \param[in] srcImageLayout  Source image layout during the blit
 * \param[in] srcSubresource  Source mip level and array layers
 * \param[in] srcOffset       Source region origin
 * \param[in] srcExtent       Source region extent
 * \param[in] dstImage        Destination image
 * \param[in] dstImageLayout  Destination image layout during the blit
 * \param[in] dstSubresource  Destination mip level and array layers
 * \param[in] dstOffset       Destination region origin
 * \param[in] dstExtent       Destination region extent
 * \param[in] filter          Reconstruction filter used by the blit
 *
 * \retval    None
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
                               VkFilter                 filter);

/*************************************************************************************************/
/**
 * \brief Binds a pipeline at an explicit pipeline bind point.
 *
 * \param[in] commandBuffer      Non-null command buffer receiving the bind
 * \param[in] pipelineBindPoint  Pipeline binding category, such as graphics or compute
 * \param[in] pipeline           Non-null pipeline bound by the command
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdBindPipeline(VkCommandBuffer commandBuffer, VkPipelineBindPoint pipelineBindPoint, VkPipeline pipeline);

/*************************************************************************************************/
/**
 * \brief Binds a compute pipeline to a command buffer.
 *
 * \param[in] commandBuffer       Command buffer receiving the recorded commands
 * \param[in] pipeline            Non-null pipeline bound by the command
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdBindComputePipeline(VkCommandBuffer commandBuffer, VkPipeline pipeline);

/*************************************************************************************************/
/**
 * \brief Binds descriptor sets and dynamic offsets at an explicit pipeline bind point.
 *
 * \param[in] commandBuffer         Command buffer receiving the recorded commands
 * \param[in] pipelineBindPoint     Pipeline binding category, such as graphics or compute
 * \param[in] layout                Non-null pipeline layout defining descriptor sets or push constants
 * \param[in] firstSet              First pipeline-layout set number affected by the bind
 * \param[in] descriptorSetCount    Number of descriptor sets to allocate or bind
 * \param[in] pDescriptorSets       Descriptor-set array containing descriptorSetCount entries
 * \param[in] dynamicOffsetCount    Number of entries in pDynamicOffsets
 * \param[in] pDynamicOffsets       Dynamic descriptor offsets containing dynamicOffsetCount entries, or NULL when the count is zero
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdBindDescriptorSets(VkCommandBuffer        commandBuffer,
                                      VkPipelineBindPoint    pipelineBindPoint,
                                      VkPipelineLayout       layout,
                                      uint32_t               firstSet,
                                      uint32_t               descriptorSetCount,
                                      const VkDescriptorSet* pDescriptorSets,
                                      uint32_t               dynamicOffsetCount,
                                      const uint32_t*        pDynamicOffsets);

/*************************************************************************************************/
/**
 * \brief Binds descriptor sets at the compute pipeline bind point.
 *
 * \param[in] commandBuffer         Command buffer receiving the recorded commands
 * \param[in] layout                Non-null pipeline layout defining descriptor sets or push constants
 * \param[in] firstSet              First pipeline-layout set number affected by the bind
 * \param[in] descriptorSetCount    Number of descriptor sets to allocate or bind
 * \param[in] pDescriptorSets       Descriptor-set array containing descriptorSetCount entries
 * \param[in] dynamicOffsetCount    Number of entries in pDynamicOffsets
 * \param[in] pDynamicOffsets       Dynamic descriptor offsets containing dynamicOffsetCount entries, or NULL when the count is zero
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdBindComputeDescriptorSets(VkCommandBuffer        commandBuffer,
                                             VkPipelineLayout       layout,
                                             uint32_t               firstSet,
                                             uint32_t               descriptorSetCount,
                                             const VkDescriptorSet* pDescriptorSets,
                                             uint32_t               dynamicOffsetCount,
                                             const uint32_t*        pDynamicOffsets);

/*************************************************************************************************/
/**
 * \brief Updates a byte range of push constants for selected shader stages.
 *
 * \param[in] commandBuffer       Command buffer receiving the recorded commands
 * \param[in] layout              Non-null pipeline layout defining descriptor sets or push constants
 * \param[in] stageFlags          Shader stages receiving the push-constant values
 * \param[in] offset              Byte offset from the beginning of the allocation or resource
 * \param[in] size                Push-constant byte count; must be a multiple of four and fit the pipeline layout range
 * \param[in] pValues             Non-null push-constant bytes containing size bytes
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdPushConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues);

/*************************************************************************************************/
/**
 * \brief Updates a byte range of compute-stage push constants.
 *
 * \param[in] commandBuffer       Command buffer receiving the recorded commands
 * \param[in] layout              Non-null pipeline layout defining descriptor sets or push constants
 * \param[in] offset              Byte offset from the beginning of the allocation or resource
 * \param[in] size                Push-constant byte count; must be a multiple of four and fit the pipeline layout range
 * \param[in] pValues             Non-null push-constant bytes containing size bytes
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdPushComputeConstants(VkCommandBuffer commandBuffer, VkPipelineLayout layout, uint32_t offset, uint32_t size, const void* pValues);

/*************************************************************************************************/
/**
 * \brief Dispatches an exact three-dimensional compute workgroup count.
 *
 * \param[in] commandBuffer       Command buffer receiving the recorded commands
 * \param[in] groupCountX         Workgroup count in the X dimension
 * \param[in] groupCountY         Workgroup count in the Y dimension
 * \param[in] groupCountZ         Workgroup count in the Z dimension
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ);

/*************************************************************************************************/
/**
 * \brief Converts logical item extent into rounded-up compute workgroup counts and dispatches.
 *
 * \par Details:
 *  Each local-size component and items-per-invocation component must be nonzero.
 *
 * \param[in] commandBuffer         Command buffer receiving the recorded commands
 * \param[in] itemCount             Logical item extent in each dimension
 * \param[in] itemsPerInvocation    Logical items handled by each shader invocation in each dimension
 * \param[in] localSize             Shader workgroup dimensions in invocations
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdDispatchItems(VkCommandBuffer commandBuffer, VkExtent3D itemCount, VkExtent3D itemsPerInvocation, VkExtent3D localSize);

/*************************************************************************************************/
/**
 * \brief Converts a two-dimensional logical item extent into workgroup counts and dispatches.
 *
 * \param[in] commandBuffer         Command buffer receiving the dispatch
 * \param[in] itemCount             Two-dimensional logical item count
 * \param[in] itemsPerInvocation    Two-dimensional item count processed by one invocation
 * \param[in] localSize             Two-dimensional shader local size
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdDispatchItems2D(VkCommandBuffer commandBuffer, VkExtent2D itemCount, VkExtent2D itemsPerInvocation, VkExtent2D localSize);

/*************************************************************************************************/
/**
 * \brief Binds compute state, writes optional push constants, and dispatches a logical item extent.
 *
 * \par Details:
 *  A zero pushConstantSize omits the push-constant command.
 *
 * \param[in] commandBuffer         Command buffer receiving the commands
 * \param[in] pipeline              Compute pipeline to bind
 * \param[in] layout                Pipeline layout used by descriptors and push constants
 * \param[in] firstSet              First descriptor-set number to bind
 * \param[in] descriptorSetCount    Number of descriptor sets
 * \param[in] pDescriptorSets       Descriptor sets, or NULL when the count is zero
 * \param[in] dynamicOffsetCount    Number of dynamic offsets
 * \param[in] pDynamicOffsets       Dynamic offsets, or NULL when the count is zero
 * \param[in] pushConstantOffset    Compute push-constant byte offset
 * \param[in] pushConstantSize      Compute push-constant byte count, or zero
 * \param[in] pPushConstants        Push-constant bytes, or NULL when the size is zero
 * \param[in] itemCount             Logical work-item count
 * \param[in] itemsPerInvocation    Logical items processed by one shader invocation
 * \param[in] localSize             Shader local workgroup size
 *
 * \retval    None
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
                                        VkExtent3D             localSize);

/*************************************************************************************************/
/**
 * \brief Binds compute state and dispatches a two-dimensional logical item extent.
 *
 * \param[in] commandBuffer          Command buffer receiving the commands
 * \param[in] pipeline               Compute pipeline to bind
 * \param[in] layout                 Pipeline layout used by descriptors and push constants
 * \param[in] firstSet               First descriptor-set number to bind
 * \param[in] descriptorSetCount     Number of descriptor sets
 * \param[in] pDescriptorSets        Descriptor sets, or NULL when the count is zero
 * \param[in] dynamicOffsetCount     Number of dynamic offsets
 * \param[in] pDynamicOffsets        Dynamic offsets, or NULL when the count is zero
 * \param[in] pushConstantOffset     Compute push-constant byte offset
 * \param[in] pushConstantSize       Compute push-constant byte count, or zero
 * \param[in] pPushConstants         Push-constant bytes, or NULL when the size is zero
 * \param[in] itemCount               Two-dimensional logical item count
 * \param[in] itemsPerInvocation      Two-dimensional item count processed by one invocation
 * \param[in] localSize               Two-dimensional shader local size
 *
 * \retval    None
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
                                          VkExtent2D             localSize);

/*************************************************************************************************/
/**
 * \brief Creates a Vulkan query pool from explicit query parameters.
 *
 * \param[in]  device             Non-null logical device that owns the query pool
 * \param[in]  pNext              Optional extension chain attached to the query-pool create info
 * \param[in]  flags              Query-pool creation flags
 * \param[in]  queryType          Kind of result stored by every query
 * \param[in]  queryCount         Nonzero number of queries in the pool
 * \param[in]  pipelineStatistics Pipeline-statistic mask, or zero for other query types
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks, or NULL
 * \param[out] pQueryPool         Receives the created query pool
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateQueryPool(VkDevice                      device,
                                const void*                   pNext,
                                VkQueryPoolCreateFlags        flags,
                                VkQueryType                   queryType,
                                uint32_t                      queryCount,
                                VkQueryPipelineStatisticFlags pipelineStatistics,
                                const VkAllocationCallbacks*  pAllocator,
                                VkQueryPool*                  pQueryPool);

/*************************************************************************************************/
/**
 * \brief Copies a contiguous range of completed query results to host memory.
 *
 * \par Details:
 *  Any non-success result, including VK_NOT_READY, is fatal.
 *
 * \param[in]  device             Logical device owning the query pool
 * \param[in]  queryPool          Query pool containing the requested results
 * \param[in]  firstQuery         First query copied from the pool
 * \param[in]  queryCount         Nonzero number of queries copied
 * \param[in]  dataSize           Writable byte capacity of pData
 * \param[out] pData              Destination for query result records
 * \param[in]  stride             Byte distance between result records
 * \param[in]  flags              Result width, wait, availability, and status flags
 *
 * \retval    None
 */
void Gfx_Vulkan_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, size_t dataSize, void* pData, VkDeviceSize stride, VkQueryResultFlags flags);

/*************************************************************************************************/
/**
 * \brief Creates a timestamp query pool whose results are consumed as 64-bit values.
 *
 * \param[in]  device             Non-null logical device that owns the query pool
 * \param[in]  pNext              Optional extension chain attached to the query-pool create info
 * \param[in]  flags              Query-pool creation flags
 * \param[in]  queryCount         Nonzero number of timestamp queries in the pool
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks, or NULL
 * \param[out] pQueryPool         Receives the created timestamp query pool
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateQueryPool64(VkDevice device, const void* pNext, VkQueryPoolCreateFlags flags, uint32_t queryCount, const VkAllocationCallbacks* pAllocator, VkQueryPool* pQueryPool);

/*************************************************************************************************/
/**
 * \brief Copies contiguous query results into a packed uint64_t array.
 *
 * \par Details:
 *  Each query must produce exactly one result value. flags must not request appended availability
 *  or status values. Results use 64 bits; any non-success query result is fatal.
 *
 * \param[in]  device             Logical device owning the query pool
 * \param[in]  queryPool          Query pool containing the requested results
 * \param[in]  firstQuery         First query copied from the pool
 * \param[in]  queryCount         Nonzero number of queries copied
 * \param[in]  flags              Query-result flags without availability or status values; 64_BIT is added automatically
 * \param[out] pResults           Array receiving one uint64_t value per query
 *
 * \retval    None
 */
void Gfx_Vulkan_GetQueryPoolResults64(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount, VkQueryResultFlags flags, uint64_t* pResults);

/*************************************************************************************************/
/**
 * \brief Records a reset for a contiguous query range.
 *
 * \param[in] commandBuffer       Command buffer receiving the reset
 * \param[in] queryPool           Query pool containing the range
 * \param[in] firstQuery          First query reset
 * \param[in] queryCount          Nonzero number of queries reset
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery, uint32_t queryCount);

/*************************************************************************************************/
/**
 * \brief Records a timestamp at the specified pipeline stage.
 *
 * \param[in] commandBuffer       Command buffer receiving the timestamp
 * \param[in] pipelineStage       Stage whose completion updates the query
 * \param[in] queryPool           Timestamp query pool containing query
 * \param[in] query               Destination query index
 *
 * \retval    None
 */
void Gfx_Vulkan_CmdWriteTimestamp(VkCommandBuffer commandBuffer, VkPipelineStageFlagBits pipelineStage, VkQueryPool queryPool, uint32_t query);

/*************************************************************************************************/
/**
 * \brief Creates a persistent binary or extension-chained semaphore.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags              Semaphore creation flags
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pSemaphore         Receives the created semaphore
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateSemaphore(VkDevice device, const void* pNext, VkSemaphoreCreateFlags flags, const VkAllocationCallbacks* pAllocator, VkSemaphore* pSemaphore);

/*************************************************************************************************/
/**
 * \brief Creates a persistent fence in the requested signaled or unsignaled state.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags              Fence creation flags
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pFence             Receives the created fence
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateFence(VkDevice device, const void* pNext, VkFenceCreateFlags flags, const VkAllocationCallbacks* pAllocator, VkFence* pFence);

/*************************************************************************************************/
/**
 * \brief Waits for one or all fences using a nanosecond timeout.
 *
 * \par Details:
 *  Timeout and other non-success results are fatal.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] fenceCount          Number of entries in pFences
 * \param[in] pFences             Fence array containing fenceCount entries
 * \param[in] waitAll             VK_TRUE to wait for every fence; VK_FALSE to wait for any fence
 * \param[in] timeout             Maximum wait in nanoseconds, or VK_INFINITE for no timeout
 *
 * \retval    None
 */
void Gfx_Vulkan_WaitForFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences, VkBool32 waitAll, uint64_t timeout);

/*************************************************************************************************/
/**
 * \brief Waits for one fence using a nanosecond timeout.
 *
 * \par Details:
 *  Timeout and other non-success results are fatal.
 *
 * \param[in] device   Non-null logical device that owns the fence
 * \param[in] fence    Non-null fence to wait for
 * \param[in] timeout  Maximum wait in nanoseconds, or VK_INFINITE for no timeout
 *
 * \retval    None
 */
void Gfx_Vulkan_WaitForFence(VkDevice device, VkFence fence, uint64_t timeout);

/*************************************************************************************************/
/**
 * \brief Resets every fence in the supplied array to the unsignaled state.
 *
 * \param[in] device              Non-null logical device that owns the referenced resources
 * \param[in] fenceCount          Number of entries in pFences
 * \param[in] pFences             Fence array containing fenceCount entries
 *
 * \retval    None
 */
void Gfx_Vulkan_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence* pFences);

/*************************************************************************************************/
/**
 * \brief Resets one fence to the unsignaled state.
 *
 * \param[in] device  Non-null logical device that owns the fence
 * \param[in] fence   Non-null fence to reset
 *
 * \retval    None
 */
void Gfx_Vulkan_ResetFence(VkDevice device, VkFence fence);

/*************************************************************************************************/
/**
 * \brief Submits an array of complete Vulkan submission descriptions.
 *
 * \param[in] queue        Non-null queue that receives the submissions
 * \param[in] submitCount  Number of entries in pSubmits
 * \param[in] pSubmits     Complete submission array containing submitCount entries
 * \param[in] fence        Fence signaled after every submission completes, or VK_NULL_HANDLE
 *
 * \retval    None
 */
void Gfx_Vulkan_QueueSubmitInfos(VkQueue queue, uint32_t submitCount, const VkSubmitInfo* pSubmits, VkFence fence);

/*************************************************************************************************/
/**
 * \brief Submits command buffers with semaphore synchronization and an optional extension chain.
 *
 * \param[in] queue                   Queue receiving the submissions
 * \param[in] pNext                   Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in] waitSemaphoreCount      Number of entries in pWaitSemaphores and its parallel arrays
 * \param[in] pWaitSemaphores         Wait-semaphore array containing waitSemaphoreCount entries, or NULL when the count is zero
 * \param[in] pWaitDstStageMask       Destination stage masks parallel to pWaitSemaphores
 * \param[in] commandBufferCount      Number of command buffers to allocate
 * \param[in] pCommandBuffers         Command-buffer array containing commandBufferCount entries, or NULL when the count is zero
 * \param[in] signalSemaphoreCount    Number of entries in pSignalSemaphores and its parallel value array
 * \param[in] pSignalSemaphores       Signal-semaphore array containing signalSemaphoreCount entries, or NULL when the count is zero
 * \param[in] fence                   Fence signaled after submitted work completes, or VK_NULL_HANDLE
 *
 * \retval    None
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
                            VkFence                     fence);

/*************************************************************************************************/
/**
 * \brief Submits one command buffer with caller-supplied semaphore arrays.
 *
 * \param[in] queue                 Queue receiving the submissions
 * \param[in] pNext                 Optional extension chain attached to the submit info, or NULL
 * \param[in] waitSemaphoreCount    Number of entries in pWaitSemaphores and pWaitDstStageMasks
 * \param[in] pWaitSemaphores       Wait-semaphore array containing waitSemaphoreCount entries, or NULL when the count is zero
 * \param[in] pWaitDstStageMasks    Destination stage masks parallel to pWaitSemaphores
 * \param[in] commandBuffer         Non-null command buffer submitted to the queue
 * \param[in] signalSemaphoreCount  Number of entries in pSignalSemaphores
 * \param[in] pSignalSemaphores     Signal-semaphore array containing signalSemaphoreCount entries, or NULL when the count is zero
 * \param[in] fence                 Submission completion fence, or VK_NULL_HANDLE
 *
 * \retval    None
 */
void Gfx_Vulkan_QueueSubmitCommandBuffer(VkQueue                     queue,
                                         const void*                 pNext,
                                         uint32_t                    waitSemaphoreCount,
                                         const VkSemaphore*          pWaitSemaphores,
                                         const VkPipelineStageFlags* pWaitDstStageMasks,
                                         VkCommandBuffer             commandBuffer,
                                         uint32_t                    signalSemaphoreCount,
                                         const VkSemaphore*          pSignalSemaphores,
                                         VkFence                     fence);

/*************************************************************************************************/
/**
 * \brief Submits command buffers with timeline semaphore values.
 *
 * \par Details:
 *  Each value array must contain as many entries as its corresponding semaphore array.
 *  A value array may be NULL only when its semaphore count is zero.
 *
 * \param[in] queue                     Queue receiving the submissions
 * \param[in] pNext                     Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in] waitSemaphoreCount        Number of entries in pWaitSemaphores and its parallel arrays
 * \param[in] pWaitSemaphores           Wait-semaphore array containing waitSemaphoreCount entries, or NULL when the count is zero
 * \param[in] pWaitDstStageMask         Destination stage masks parallel to pWaitSemaphores
 * \param[in] pWaitSemaphoreValues      Array of waitSemaphoreCount values; NULL only when that count is zero
 * \param[in] commandBufferCount        Number of command buffers to allocate
 * \param[in] pCommandBuffers           Command-buffer array containing commandBufferCount entries, or NULL when the count is zero
 * \param[in] signalSemaphoreCount      Number of entries in pSignalSemaphores and its parallel value array
 * \param[in] pSignalSemaphores         Signal-semaphore array containing signalSemaphoreCount entries, or NULL when the count is zero
 * \param[in] pSignalSemaphoreValues    Array of signalSemaphoreCount values; NULL only when that count is zero
 * \param[in] fence                     Fence signaled after submitted work completes, or VK_NULL_HANDLE
 *
 * \retval    None
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
                                             VkFence                     fence);

/*************************************************************************************************/
/**
 * \brief Waits synchronously until all previously submitted work on a queue is idle.
 *
 * \param[in] queue               Queue receiving the submissions
 *
 * \retval    None
 */
void Gfx_Vulkan_QueueWaitIdle(VkQueue queue);

/*************************************************************************************************/
/**
 * \brief Reports whether one queue family supports presentation to a surface.
 *
 * \param[in]  physicalDevice      Non-null physical device queried or used to select compatible resources
 * \param[in]  queueFamilyIndex    Queue-family index selected for the operation
 * \param[in]  surface             Native presentation surface being queried
 * \param[out] pSupported          Receives whether the queue family supports presentation to the surface
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfaceSupport(VkPhysicalDevice physicalDevice, uint32_t queueFamilyIndex, VkSurfaceKHR surface, VkBool32* pSupported);

/*************************************************************************************************/
/**
 * \brief Returns extent, image-count, transform, usage, and composite-alpha surface limits.
 *
 * \param[in]  physicalDevice     Non-null physical device queried or used to select compatible resources
 * \param[in]  surface            Native presentation surface being queried
 * \param[out] pCapabilities      Receives the queried surface or display-plane capabilities
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfaceCapabilities(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkSurfaceCapabilitiesKHR* pCapabilities);

/*************************************************************************************************/
/**
 * \brief Enumerates surface format/color-space pairs into fixed-capacity storage.
 *
 * \param[in]  physicalDevice         Non-null physical device queried or used to select compatible resources
 * \param[in]  surface                Native presentation surface being queried
 * \param[out] pSurfaceFormatCount    Receives the nonzero number of initialized entries, at most VK_MAX_SURFACE_FORMATS
 * \param[out] pSurfaceFormats        Receives up to VK_MAX_SURFACE_FORMATS supported format/color-space pairs
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfaceFormats(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pSurfaceFormatCount, VkSurfaceFormatKHR pSurfaceFormats[VK_MAX_SURFACE_FORMATS]);

/*************************************************************************************************/
/**
 * \brief Selects the first surface format matching the required image format.
 *
 * \par Details:
 *  Preserves the matching color space. A single VK_FORMAT_UNDEFINED entry accepts the requested
 *  format with that entry's color space. Absence of a compatible format is fatal.
 *
 * \param[in]  physicalDevice    Physical device queried for surface support
 * \param[in]  surface           Native presentation surface
 * \param[in]  imageFormat       Required non-undefined Vulkan image format
 * \param[out] pSurfaceFormat    Receives the supported format and color-space pair
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfaceFormatProperties(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkFormat imageFormat, VkSurfaceFormatKHR* pSurfaceFormat);

/*************************************************************************************************/
/**
 * \brief Enumerates presentation modes supported by a physical-device surface pair.
 *
 * \param[in]  physicalDevice       Non-null physical device queried or used to select compatible resources
 * \param[in]  surface              Native presentation surface being queried
 * \param[out] pPresentModeCount    Receives the nonzero number of initialized entries, at most VK_MAX_PRESENT_MODES
 * \param[out] pPresentModes        Receives up to VK_MAX_PRESENT_MODES supported presentation modes
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfacePresentModes(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, uint32_t* pPresentModeCount, VkPresentModeKHR pPresentModes[VK_MAX_PRESENT_MODES]);

/*************************************************************************************************/
/**
 * \brief Reports whether the surface supports the requested presentation mode.
 *
 * \par Details:
 *  Checks for an exact match without selecting a fallback.
 *
 * \param[in]  physicalDevice    Physical device queried for surface support
 * \param[in]  surface           Native presentation surface
 * \param[in]  presentMode       Required presentation mode
 * \param[out] pSupported        Receives VK_TRUE for an exact match, otherwise VK_FALSE
 *
 * \retval    None
 */
void Gfx_Vulkan_GetPhysicalDeviceSurfacePresentModeSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface, VkPresentModeKHR presentMode, VkBool32* pSupported);

/*************************************************************************************************/
/**
 * \brief Enumerates native displays exposed by VK_KHR_display.
 *
 * \param[in] physicalDevice      Non-null physical device queried or used to select compatible resources
 * \param[out] pProperties         Array receiving up to VK_MAX_DISPLAYS display properties
 *
 * \return Number of initialized entries, never exceeding VK_MAX_DISPLAYS.
 */
uint32_t Gfx_Vulkan_GetPhysicalDeviceDisplayProperties(VkPhysicalDevice physicalDevice, VkDisplayPropertiesKHR pProperties[VK_MAX_DISPLAYS]);

/*************************************************************************************************/
/**
 * \brief Enumerates resolution and refresh-rate modes exposed by one native display.
 *
 * \param[in] physicalDevice      Non-null physical device queried or used to select compatible resources
 * \param[in] display             Physical display whose modes are queried
 * \param[out] pProperties         Array receiving up to VK_MAX_DISPLAY_MODES mode properties
 *
 * \return Number of initialized entries, never exceeding VK_MAX_DISPLAY_MODES.
 */
uint32_t Gfx_Vulkan_GetDisplayModeProperties(VkPhysicalDevice physicalDevice, VkDisplayKHR display, VkDisplayModePropertiesKHR pProperties[VK_MAX_DISPLAY_MODES]);

/*************************************************************************************************/
/**
 * \brief Enumerates display planes and their current display/stack relationships.
 *
 * \param[in] physicalDevice      Non-null physical device queried or used to select compatible resources
 * \param[out] pProperties         Array receiving up to VK_MAX_DISPLAY_PLANES plane properties
 *
 * \return Number of initialized entries, never exceeding VK_MAX_DISPLAY_PLANES.
 */
uint32_t Gfx_Vulkan_GetPhysicalDeviceDisplayPlaneProperties(VkPhysicalDevice physicalDevice, VkDisplayPlanePropertiesKHR pProperties[VK_MAX_DISPLAY_PLANES]);

/*************************************************************************************************/
/**
 * \brief Enumerates displays that one physical display plane can present to.
 *
 * \param[in]  physicalDevice     Non-null physical device queried or used to select compatible resources
 * \param[in]  planeIndex         Zero-based plane index
 * \param[out] pDisplays          Receives up to VK_MAX_DISPLAYS display handles supported by the plane
 *
 * \return Number of initialized handles, never exceeding VK_MAX_DISPLAYS.
 */
uint32_t Gfx_Vulkan_GetDisplayPlaneSupportedDisplays(VkPhysicalDevice physicalDevice, uint32_t planeIndex, VkDisplayKHR pDisplays[VK_MAX_DISPLAYS]);

/*************************************************************************************************/
/**
 * \brief Selects the preferred supported direct-display alpha mode.
 *
 * \param[in] supportedAlpha      VkDisplayPlaneCapabilitiesKHR alpha-mode mask
 *
 * \return First available mode in opaque, global, per-pixel, premultiplied order; none is fatal.
 */
VkDisplayPlaneAlphaFlagBitsKHR Gfx_Vulkan_GetDisplayPlaneAlphaMode(VkDisplayPlaneAlphaFlagsKHR supportedAlpha);

/*************************************************************************************************/
/**
 * \brief Selects one composite-alpha mode supported by a presentation surface.
 *
 * \param[in] supportedCompositeAlpha    VkSurfaceCapabilitiesKHR support mask
 *
 * \return First supported mode in opaque, pre-multiplied, post-multiplied, inherit order.
 */
VkCompositeAlphaFlagBitsKHR Gfx_Vulkan_GetSurfaceCompositeAlpha(VkCompositeAlphaFlagsKHR supportedCompositeAlpha);

/*************************************************************************************************/
/**
 * \brief Selects the preferred supported surface transform.
 *
 * \param[in] supportedTransforms    VkDisplayPropertiesKHR or VkSurfaceCapabilitiesKHR transform mask
 *
 * \return First available transform, preferring identity before rotations, mirrors, and inherit.
 */
VkSurfaceTransformFlagBitsKHR Gfx_Vulkan_GetSurfaceTransform(VkSurfaceTransformFlagsKHR supportedTransforms);

/*************************************************************************************************/
/**
 * \brief Selects the first plane supporting the display that is unassigned or already assigned to it.
 *
 * \par Details:
 *  pPlaneProperties contains planeCount entries in physical-device enumeration order.
 *
 * \param[in] physicalDevice      Non-null physical device queried or used to select compatible resources
 * \param[in] display             Physical display whose modes are queried
 * \param[in] planeCount          Number of valid plane entries supplied to the operation
 * \param[in] pPlaneProperties    Display-plane properties containing planeCount entries
 *
 * \return Compatible plane index; absence of a compatible plane is fatal.
 */
uint32_t Gfx_Vulkan_GetCompatibleDisplayPlaneIndex(VkPhysicalDevice physicalDevice, VkDisplayKHR display, uint32_t planeCount, const VkDisplayPlanePropertiesKHR* pPlaneProperties);

/*************************************************************************************************/
/**
 * \brief Returns source/destination rectangles and alpha modes supported by a display plane.
 *
 * \param[in]  physicalDevice     Non-null physical device queried or used to select compatible resources
 * \param[in]  mode               Display mode whose plane capabilities are queried
 * \param[in]  planeIndex         Zero-based plane index
 * \param[out] pCapabilities      Receives the queried surface or display-plane capabilities
 *
 * \retval    None
 */
void Gfx_Vulkan_GetDisplayPlaneCapabilities(VkPhysicalDevice physicalDevice, VkDisplayModeKHR mode, uint32_t planeIndex, VkDisplayPlaneCapabilitiesKHR* pCapabilities);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VK_EXT_headless_surface presentation surface.
 *
 * \param[in]  instance           Vulkan instance owning the queried or created objects
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags              Headless surface creation flags
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pSurface           Receives the created presentation surface
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateHeadlessSurface(VkInstance instance, const void* pNext, VkHeadlessSurfaceCreateFlagsEXT flags, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface);

/*************************************************************************************************/
/**
 * \brief Creates a presentation surface for caller-selected display and mode indices.
 *
 * \par Details:
 *  displayIndex and modeIndex select entries from the corresponding Vulkan enumerations.
 *  The selected mode determines pImageExtent. The first compatible plane is used.
 *  Out-of-range indices or absence of a compatible plane are fatal.
 *
 * \param[in]  physicalDevice     Non-null physical device queried or used to select compatible resources
 * \param[in]  instance           Vulkan instance owning the queried or created objects
 * \param[in]  displayIndex       Zero-based display index
 * \param[in]  modeIndex          Zero-based mode index
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pImageExtent       Receives the selected display-mode extent
 * \param[out] pSurface           Receives the created presentation surface
 *
 * \retval    None
 */
void Gfx_Vulkan_CreateDirectDisplaySurface(VkPhysicalDevice             physicalDevice,
                                           VkInstance                   instance,
                                           uint32_t                     displayIndex,
                                           uint32_t                     modeIndex,
                                           const VkAllocationCallbacks* pAllocator,
                                           VkExtent2D*                  pImageExtent,
                                           VkSurfaceKHR*                pSurface);

/*************************************************************************************************/
/**
 * \brief Creates a persistent VK_KHR_display surface from an explicit mode and plane configuration.
 *
 * \param[in]  instance           Vulkan instance owning the queried or created objects
 * \param[in]  pNext              Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags              Display plane surface creation flags
 * \param[in]  displayMode        Mode selected for direct presentation
 * \param[in]  planeIndex         Zero-based plane index
 * \param[in]  planeStackIndex    Zero-based plane stack index
 * \param[in]  transform          Transform applied to the display-plane contents
 * \param[in]  globalAlpha        Global display-plane alpha value
 * \param[in]  alphaMode          How the display plane combines alpha with other planes
 * \param[in]  imageExtent        Dimensions of the image copy region in texels
 * \param[in]  pAllocator         Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pSurface           Receives the created presentation surface
 *
 * \retval    None
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
                                          VkSurfaceKHR*                  pSurface);

/*************************************************************************************************/
/**
 * \brief Creates a persistent Vulkan KHR presentation swapchain from negotiated surface settings.
 *
 * \param[in]  device                   Non-null logical device that owns the referenced resources
 * \param[in]  pNext                    Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  flags                    Swapchain creation flags
 * \param[in]  surface                  Native presentation surface being queried
 * \param[in]  minimumImageCount        Minimum number of images requested from the swapchain
 * \param[in]  imageFormat              Format of the image participating in the transfer
 * \param[in]  imageColorSpace          Presentation color space paired with imageFormat
 * \param[in]  imageExtent              Dimensions of the image copy region in texels
 * \param[in]  imageArrayLayers         Number of array layers in each swapchain image
 * \param[in]  imageUsage               Permitted uses of the swapchain images
 * \param[in]  imageSharingMode         Exclusive or concurrent swapchain-image sharing mode
 * \param[in]  queueFamilyIndexCount    Number of queue-family indices; ignored for exclusive sharing
 * \param[in]  pQueueFamilyIndices      Queue-family index array containing queueFamilyIndexCount entries, or NULL when the count is zero
 * \param[in]  preTransform             Transform applied to images before presentation
 * \param[in]  compositeAlpha           How image alpha is combined with other window-system content
 * \param[in]  presentMode              Presentation scheduling mode
 * \param[in]  clipped                  Whether pixels hidden by other native windows may be discarded
 * \param[in]  oldSwapchain             Previous swapchain being replaced, or VK_NULL_HANDLE
 * \param[in]  pAllocator               Optional Vulkan allocation callbacks used for object creation, or NULL
 * \param[out] pSwapchain               Receives the created swapchain
 *
 * \retval    None
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
                                VkSwapchainKHR*               pSwapchain);

/*************************************************************************************************/
/**
 * \brief Validates requested surface settings and creates a Vulkan KHR swapchain.
 *
 * \par Details:
 *  Keeps the requested format, present mode, image count, and extent. Uses the first matching
 *  surface format and its color space, including the single VK_FORMAT_UNDEFINED case. Selects
 *  the transform through Gfx_Vulkan_GetSurfaceTransform and composite alpha through
 *  Gfx_Vulkan_GetSurfaceCompositeAlpha.
 *
 * \param[in]  physicalDevice           Physical device owning the presentation queue
 * \param[in]  device                   Logical device owning the swapchain
 * \param[in]  pNext                    Optional swapchain creation extension chain
 * \param[in]  flags                    Swapchain creation flags
 * \param[in]  surface                  Native presentation surface
 * \param[in]  queueFamilyIndex         Queue family required to support presentation
 * \param[in]  minimumImageCount        Requested minimum image count, within the surface limits
 * \param[in]  imageFormat              Required image format; must be supported by the surface
 * \param[in]  imageExtent              Required dimensions; must match a fixed surface extent and satisfy its limits
 * \param[in]  imageArrayLayers         Number of array layers in each image
 * \param[in]  imageUsage               Required image usages; every bit must be supported by the surface
 * \param[in]  imageSharingMode         Exclusive or concurrent swapchain image sharing
 * \param[in]  queueFamilyIndexCount    Number of sharing queue family indices
 * \param[in]  pQueueFamilyIndices      Sharing queue family indices, or NULL when the count is zero
 * \param[in]  presentMode              Required presentation mode; no fallback is selected
 * \param[in]  clipped                  Whether obscured pixels may be discarded
 * \param[in]  oldSwapchain             Previous swapchain for replacement, or VK_NULL_HANDLE
 * \param[in]  pAllocator               Optional Vulkan allocation callbacks
 * \param[out] pSwapchain               Receives the created native swapchain
 *
 * \retval    None
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
                               VkSwapchainKHR*              pSwapchain);

/*************************************************************************************************/
/**
 * \brief Enumerates images owned by a Vulkan KHR swapchain.
 *
 * \param[in]  device                  Non-null logical device that owns the referenced resources
 * \param[in]  swapchain               Swapchain whose images are enumerated
 * \param[out] pSwapchainImageCount    Receives the nonzero number of initialized image handles, at most VK_MAX_SWAPCHAIN_IMAGES
 * \param[out] pSwapchainImages        Receives up to VK_MAX_SWAPCHAIN_IMAGES swapchain-owned image handles
 *
 * \retval    None
 */
void Gfx_Vulkan_GetSwapchainImages(VkDevice device, VkSwapchainKHR swapchain, uint32_t* pSwapchainImageCount, VkImage pSwapchainImages[VK_MAX_SWAPCHAIN_IMAGES]);

/*************************************************************************************************/
/**
 * \brief Acquires one KHR swapchain image with semaphore or fence notification.
 *
 * \par Details:
 *  timeout is in nanoseconds. At least one of semaphore and fence must be non-null.
 *
 * \param[in]  device             Non-null logical device that owns the referenced resources
 * \param[in]  swapchain          Swapchain whose images are enumerated
 * \param[in]  timeout            Maximum wait in nanoseconds, or VK_INFINITE for no timeout
 * \param[in]  semaphore          Semaphore signaled by acquisition, or VK_NULL_HANDLE when a fence is used
 * \param[in]  fence              Fence signaled after submitted work completes, or VK_NULL_HANDLE
 * \param[out] pImageIndex        Receives the acquired swapchain image index
 *
 * \retval    None
 */
void Gfx_Vulkan_AcquireNextImage(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout, VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);

/*************************************************************************************************/
/**
 * \brief Presents one or more swapchain images after waiting on the supplied semaphores.
 *
 * \param[in]  queue                 Non-null queue that receives the presentation request
 * \param[in]  pNext                 Optional extension chain attached to the presentation description, or NULL
 * \param[in]  waitSemaphoreCount    Number of entries in pWaitSemaphores
 * \param[in]  pWaitSemaphores       Wait-semaphore array containing waitSemaphoreCount entries, or NULL when the count is zero
 * \param[in]  swapchainCount        Number of entries in pSwapchains and pImageIndices
 * \param[in]  pSwapchains           Swapchain array containing swapchainCount entries
 * \param[in]  pImageIndices         Image-index array parallel to pSwapchains
 * \param[out] pResults              Optional per-swapchain presentation result array, or NULL
 *
 * \retval    None
 */
void Gfx_Vulkan_QueuePresent(VkQueue               queue,
                             const void*           pNext,
                             uint32_t              waitSemaphoreCount,
                             const VkSemaphore*    pWaitSemaphores,
                             uint32_t              swapchainCount,
                             const VkSwapchainKHR* pSwapchains,
                             const uint32_t*       pImageIndices,
                             VkResult*             pResults);

/*************************************************************************************************/
/**
 * \brief Presents one swapchain image after waiting on caller-supplied semaphores.
 *
 * \par Details:
 *  Out-of-date, suboptimal, and other non-success presentation results are fatal.
 *
 * \param[in]  queue                 Queue receiving the submissions
 * \param[in]  pNext                 Optional extension chain attached to the composed Vulkan input structure, or NULL
 * \param[in]  waitSemaphoreCount    Number of entries in pWaitSemaphores
 * \param[in]  pWaitSemaphores       Wait-semaphore array containing waitSemaphoreCount entries, or NULL when the count is zero
 * \param[in]  swapchain             Swapchain whose images are enumerated
 * \param[in]  imageIndex            Swapchain image index to present
 * \param[out] pResult               Receives the result for this swapchain, or NULL
 *
 * \retval    None
 */
void Gfx_Vulkan_QueuePresentImage(VkQueue queue, const void* pNext, uint32_t waitSemaphoreCount, const VkSemaphore* pWaitSemaphores, VkSwapchainKHR swapchain, uint32_t imageIndex, VkResult* pResult);

/*************************************************************************************************/
/**
 * \brief Selects a compatible memory type from a physical device.
 *
 * \param[in]  physicalDevice  Physical device supplying memory properties
 * \param[in]  memoryTypeBits  Mask of compatible memory types
 * \param[in]  properties      Required memory-property flags
 *
 * \return Matching memory-type index, or VK_INVALID when no type matches.
 */
uint32_t Gfx_Vulkan_GetPhysicalDeviceCompatibleMemoryTypeIndex(VkPhysicalDevice physicalDevice, uint32_t memoryTypeBits, VkMemoryPropertyFlags properties);

/*************************************************************************************************/
/**
 * \brief Queries image allocation size and selects a compatible memory type.
 *
 * \param[in]  physicalDevice    Physical device supplying memory properties
 * \param[in]  device            Logical device owning the resource
 * \param[in]  image             Resource to allocate memory for
 * \param[in]  memoryTypeBits    Additional compatible-memory mask; UINT32_MAX imposes no restriction
 * \param[in]  properties        Required memory-property flags
 * \param[out] pAllocationSize   Receives the required allocation size
 * \param[out] pMemoryTypeIndex  Receives a compatible memory-type index
 *
 * \retval    None
 */
void Gfx_Vulkan_GetImageMemoryAllocationInfo(VkPhysicalDevice      physicalDevice,
                                             VkDevice              device,
                                             VkImage               image,
                                             uint32_t              memoryTypeBits,
                                             VkMemoryPropertyFlags properties,
                                             VkDeviceSize*         pAllocationSize,
                                             uint32_t*             pMemoryTypeIndex);

/*************************************************************************************************/
/**
 * \brief Queries buffer allocation size and selects a compatible memory type.
 *
 * \param[in]  physicalDevice    Physical device supplying memory properties
 * \param[in]  device            Logical device owning the resource
 * \param[in]  buffer            Resource to allocate memory for
 * \param[in]  memoryTypeBits    Additional compatible-memory mask; UINT32_MAX imposes no restriction
 * \param[in]  properties        Required memory-property flags
 * \param[out] pAllocationSize   Receives the required allocation size
 * \param[out] pMemoryTypeIndex  Receives a compatible memory-type index
 *
 * \retval    None
 */
void Gfx_Vulkan_GetBufferMemoryAllocationInfo(VkPhysicalDevice      physicalDevice,
                                              VkDevice              device,
                                              VkBuffer              buffer,
                                              uint32_t              memoryTypeBits,
                                              VkMemoryPropertyFlags properties,
                                              VkDeviceSize*         pAllocationSize,
                                              uint32_t*             pMemoryTypeIndex);

#endif /* VULKAN_VA_ZERO_COPY_GFX_VULKAN_H */
