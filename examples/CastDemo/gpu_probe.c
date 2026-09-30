/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Vulkan queue codec operations and video profile capability chains:
 * https://docs.vulkan.org/spec/latest/chapters/devsandqueues.html
 * https://docs.vulkan.org/refpages/latest/refpages/source/VkVideoEncodeH264CapabilitiesKHR.html
 * Native enumeration is bounded by cvk's declared inventory capacities.
 * Loader/API support is queried before cvkCreateInstance; no optional instance extensions or layers are requested.
 * CVK owns Vulkan construction and its fatal contract; capability queries represent ordinary unsupported profiles.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h> // IWYU pragma: keep
#include <stdint.h>
#include <minwindef.h>
#include <vk_video/vulkan_video_codec_h264std.h>
#include <volk.h>
#include <vulkan/vulkan_core.h>
#include "cvk/cvk.h"
#include "gpu_probe.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include "os/os_memory.h"
#include "utl/utl_decimal.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define REPORT_NUL                ('\0')
#define DECIMAL_CAPACITY          (20)
#define NO_QUEUE_FAMILY           (UINT32_MAX)
#define PROBE_APPLICATION_NAME    ("CastDemo GPU probe")
#define PROBE_ENGINE_NAME         ("cvk")
#define PROBE_APPLICATION_VERSION (1)
#define PROBE_ENGINE_VERSION      (1)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Bounded native arrays; no array scales with a driver-provided count.
 */
typedef struct GpuProbe_Storage_s
{
    VkInstance                      instance;                                       ///< Native instance retained until destruction can be issued.
    bool_t                          loader;                                         ///< Exclusive loader lifetime acquired by this probe.
    uint32_t                        deviceCount;                                    ///< Fully returned physical-device records.
    uint32_t                        extensionCount;                                 ///< Fully returned extension records for the current device.
    uint32_t                        familyCount;                                    ///< Fully returned queue-family records for the current device.
    VkPhysicalDevice                devices[CVK_MAX_PHYSICAL_DEVICES];              ///< Fixed enumeration output.
    VkExtensionProperties           extensions[CVK_MAX_PROPERTIES];                 ///< Reused per-device extension output.
    VkQueueFamilyProperties         families[CVK_MAX_QUEUE_FAMILY_PROPERTIES];      ///< Reused queue metadata.
    VkQueueFamilyProperties2        videoFamilies[CVK_MAX_QUEUE_FAMILY_PROPERTIES]; ///< Base workspace for the codec-property query.
    VkQueueFamilyVideoPropertiesKHR video[CVK_MAX_QUEUE_FAMILY_PROPERTIES];         ///< Codec operations linked into family records.
} GpuProbe_Storage_t;

/*************************************************************************************************/
/**
 * \brief Current-device extension and queue selection, separate from publication.
 */
typedef struct DeviceSupport_s
{
    bool_t   video;        ///< Common video queue extension is advertised.
    bool_t   encode;       ///< Both H.264 encode extensions are advertised.
    bool_t   decode;       ///< Both H.264 decode extensions are advertised.
    uint32_t encodeFamily; ///< First usable H.264 encode family or NO_QUEUE_FAMILY.
    uint32_t decodeFamily; ///< First usable H.264 decode family or NO_QUEUE_FAMILY.
} DeviceSupport_t;

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
 * \brief Classifies ordinary absence of the requested video profile.
 *
 * \par Requirements:
 *  - 001 All other native errors shall remain fatal to snapshot collection.
 *
 * \param[in] result   Native profile result
 *
 * \retval TRUE   Requested profile is unsupported.
 * \retval FALSE   Success or a fatal native error.
 */
static FORCE_INLINE bool_t IsUnsupported(VkResult result);

/*************************************************************************************************/
/**
 * \brief Finds an exact counted extension name including termination.
 *
 * \par Requirements:
 *  - 001 Only completely returned extension records shall be inspected.
 *
 * \param[in] storage   Current-device enumeration
 * \param[in] name   Required counted name
 * \param[in] bytes   Name bytes excluding NUL
 *
 * \retval TRUE   An exact extension is present.
 * \retval FALSE   No extension matches.
 */
static FORCE_INLINE bool_t HasExtension(const GpuProbe_Storage_t* storage, const char_t* name, uint32_t bytes);

/*************************************************************************************************/
/**
 * \brief Appends one complete fragment or latches diagnostic saturation.
 *
 * \par Details:
 *  Report or saturation latch is updated.
 *
 * \par Requirements:
 *  - 001 Extent shall be checked before copying; termination shall always fit.
 *
 * \param[in,out] probe   Initialized report owner
 * \param[in] text   Counted source
 * \param[in] bytes   Source bytes
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void AppendText(GpuProbe_t* probe, const char_t* text, uint32_t bytes);

/*************************************************************************************************/
/**
 * \brief Appends a native numeric property without variadic or CRT formatting.
 *
 * \par Details:
 *  Complete decimal digits are appended or saturation is latched.
 *
 * \par Requirements:
 *  - 001 The decimal temporary shall cover the complete unsigned domain.
 *
 * \param[in,out] probe   Report owner
 * \param[in] value   Numeric property
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void AppendNumber(GpuProbe_t* probe, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Appends the diagnostic spelling of a capability question.
 *
 * \par Details:
 *  The spelling is appended.
 *
 * \par Requirements:
 *  - 001 Both spellings shall be counted constants.
 *
 * \param[in,out] probe   Report owner
 * \param[in] answer   Capability result
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void AppendQuestion(GpuProbe_t* probe, bool_t answer);

/*************************************************************************************************/
/**
 * \brief Records loader or capability-query failure separately from complete success.
 *
 * \par Requirements:
 *  - 001 Incomplete enumeration shall be an explicit capacity failure.
 *  - 002 The first fatal native result shall be preserved.
 *
 * \param[in,out] probe   Snapshot owner
 * \param[in] result   Native operation result
 *
 * \retval TRUE   Complete success.
 * \retval FALSE   Snapshot failed.
 */
static FORCE_INLINE bool_t CheckResult(GpuProbe_t* probe, VkResult result);

/*************************************************************************************************/
/**
 * \brief Rejects a driver count that cannot be represented completely.
 *
 * \par Requirements:
 *  - 001 Rejection shall precede any indexing based on the rejected count.
 *
 * \param[in,out] probe   Snapshot owner
 * \param[in] fits   Validated capacity question
 *
 * \retval TRUE   Capacity is sufficient.
 * \retval FALSE   Snapshot is incomplete.
 */
static FORCE_INLINE bool_t CheckCapacity(GpuProbe_t* probe, bool_t fits);

/*************************************************************************************************/
/**
 * \brief Collects bounded extensions and derives video support.
 *
 * \par Requirements:
 *  - 001 VK_INCOMPLETE shall never be accepted as a complete extension list.
 *
 * \param[in,out] probe   Startup workspace
 * \param[in] physical   Current physical device
 * \param[out] support   Staged extension and queue selection
 *
 * \retval TRUE   Complete extension set.
 * \retval FALSE   Native or capacity failure.
 */
static FORCE_INLINE bool_t ReadExtensions(GpuProbe_t* probe, VkPhysicalDevice physical, DeviceSupport_t* support);

/*************************************************************************************************/
/**
 * \brief Collects queue counts, flags and codec-specific operations.
 *
 * \par Requirements:
 *  - 001 Every native output chain shall be initialized before its query.
 *  - 002 Selection shall require a nonempty queue with the requested H.264 operation.
 *  - 003 A changing enumeration that exceeds collected capacity shall fail.
 *
 * \param[in,out] probe   Startup workspace
 * \param[in] physical   Current physical device
 * \param[in,out] support   Derived family choices
 *
 * \retval TRUE   Complete family set.
 * \retval FALSE   Capacity failure.
 */
static FORCE_INLINE bool_t ReadFamilies(GpuProbe_t* probe, VkPhysicalDevice physical, DeviceSupport_t* support);

/*************************************************************************************************/
/**
 * \brief Queries the complete Main-profile encoder capability chain.
 *
 * \par Requirements:
 *  - 001 Unsupported profiles shall leave available FALSE without claiming native failure.
 *
 * \param[in,out] probe   Error owner
 * \param[in] physical   Candidate device
 * \param[in,out] candidate   Unpublished capability record
 *
 * \retval TRUE   Profile query completed, including ordinary absence.
 * \retval FALSE   Fatal native error.
 */
static FORCE_INLINE bool_t ReadEncodeProfile(GpuProbe_t* probe, VkPhysicalDevice physical, GpuProbe_Caps_t* candidate);

/*************************************************************************************************/
/**
 * \brief Checks progressive H.264 Main decode on the same selected device.
 *
 * \par Requirements:
 *  - 001 Both common and codec-specific output capability structures shall be chained.
 *
 * \param[in,out] probe   Error owner
 * \param[in] physical   Candidate device
 * \param[in,out] candidate   Unpublished capability record
 *
 * \retval TRUE   Profile result is known.
 * \retval FALSE   Fatal native failure.
 */
static FORCE_INLINE bool_t ReadDecodeProfile(GpuProbe_t* probe, VkPhysicalDevice physical, GpuProbe_Caps_t* candidate);

/*************************************************************************************************/
/**
 * \brief Reports limits, queues and blit properties of the selected device.
 *
 * \par Details:
 *  The retained report is extended.
 *
 * \par Requirements:
 *  - 001 Every queue and format query shall refer to the selected physical device.
 *
 * \param[in,out] probe   Selected snapshot and report
 * \param[in] physical   Selected physical device
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void ReportSelected(GpuProbe_t* probe, VkPhysicalDevice physical);

/*************************************************************************************************/
/**
 * \brief Stages one adapter capability record before selection.
 *
 * \par Requirements:
 *  - 001 No selected fields shall escape on native failure.
 *  - 002 Device names shall terminate within the native fixed extent.
 *
 * \param[in,out] probe   Startup snapshot owner
 * \param[in] physical   Current enumerated device
 *
 * \retval TRUE   Complete result, including unavailable profiles.
 * \retval FALSE   Collection failed.
 */
static FORCE_INLINE bool_t ReadDevice(GpuProbe_t* probe, VkPhysicalDevice physical);

/*************************************************************************************************/
/**
 * \brief Acquires exclusive loader and instance ownership during startup.
 *
 * \par Requirements:
 *  - 001 No failed instance shall be passed to dispatch loading.
 *
 * \param[in,out] probe   Resident startup workspace
 *
 * \retval TRUE   Required core dispatch is available.
 * \retval FALSE   Native acquisition failed and ownership is retained.
 */
static FORCE_INLINE bool_t OpenNative(GpuProbe_t* probe);

/*************************************************************************************************/
/**
 * \brief Retires the CVK instance before unloading the exclusively owned loader.
 *
 * \par Requirements:
 *  - 001 CVK destruction shall precede unloading its dispatch provider.
 *
 * \param[in,out] storage   Initialized native lifetime
 *
 * \retval TRUE   No native instance or loader remains.
 * \retval FALSE   Reserved; CVK contract violations are fatal.
 */
static FORCE_INLINE bool_t CloseNative(GpuProbe_Storage_t* storage);

/*************************************************************************************************/
/**
 * \brief Collects complete device records until the first usable encoder is found.
 *
 * \par Requirements:
 *  - 001 Incomplete enumerations shall never expose partial selected capabilities.
 *
 * \param[in,out] probe   Initialized native startup owner
 *
 * \retval TRUE   Complete supported or unsupported result.
 * \retval FALSE   Native or capacity failure.
 */
static FORCE_INLINE bool_t Collect(GpuProbe_t* probe);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsUnsupported(VkResult result)
{
    bool_t unsupported;
    unsupported = (VK_ERROR_VIDEO_PICTURE_LAYOUT_NOT_SUPPORTED_KHR == result) ||
                  (VK_ERROR_VIDEO_PROFILE_OPERATION_NOT_SUPPORTED_KHR == result) ||
                  (VK_ERROR_VIDEO_PROFILE_FORMAT_NOT_SUPPORTED_KHR == result) ||
                  (VK_ERROR_VIDEO_PROFILE_CODEC_NOT_SUPPORTED_KHR == result) ||
                  (VK_ERROR_VIDEO_STD_VERSION_NOT_SUPPORTED_KHR == result);
    return unsupported;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t HasExtension(const GpuProbe_Storage_t* storage, const char_t* name, uint32_t bytes)
{
    bool_t found;
    LOGASSERT(bytes < VK_MAX_EXTENSION_NAME_SIZE);
    LOGASSERT(REPORT_NUL == name[bytes]);
    found = !!cvkGetExtensionSupport(storage->extensionCount, storage->extensions, name);
    return found;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void AppendText(GpuProbe_t* probe, const char_t* text, uint32_t bytes)
{
    uint32_t remaining;
    bool_t   fits;
    LOGASSERT(probe->reportBytes < GPU_PROBE_REPORT_CAPACITY);
    if (!probe->reportTruncated)
    {
        remaining = GPU_PROBE_REPORT_CAPACITY - 1 - probe->reportBytes;
        fits      = (bytes <= remaining);
        if (fits)
        {
            CRT_Memory_Copy(&probe->report[probe->reportBytes], text, bytes);
            probe->reportBytes                += bytes;
            probe->report[probe->reportBytes]  = REPORT_NUL;
        }
        else
        {
            probe->reportTruncated = TRUE;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void AppendNumber(GpuProbe_t* probe, uint32_t value)
{
    byte_t   digits[DECIMAL_CAPACITY];
    uint32_t bytes;
    bytes = Utl_Decimal_WriteUint64(value, digits, sizeof(digits));
    LOGASSERT(0 < bytes);
    AppendText(probe, (const char_t*)digits, bytes);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void AppendQuestion(GpuProbe_t* probe, bool_t answer)
{
    if (answer)
    {
        AppendText(probe, "yes", sizeof("yes") - 1);
    }
    else
    {
        AppendText(probe, "no", sizeof("no") - 1);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t CheckResult(GpuProbe_t* probe, VkResult result)
{
    bool_t succeeded;
    succeeded = (VK_SUCCESS == result);
    if (!succeeded)
    {
        if (0 == probe->nativeResult)
        {
            probe->nativeResult = (int32_t)result;
        }
        probe->status = (VK_INCOMPLETE == result) ? GPU_PROBE_CAPACITY_EXCEEDED : GPU_PROBE_NATIVE_FAILED;
    }
    return succeeded;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t CheckCapacity(GpuProbe_t* probe, bool_t fits)
{
    if (!fits)
    {
        probe->status = GPU_PROBE_CAPACITY_EXCEEDED;
    }
    return fits;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadExtensions(GpuProbe_t* probe, VkPhysicalDevice physical, DeviceSupport_t* support)
{
    GpuProbe_Storage_t* storage;
    bool_t              complete;
    storage               = probe->storage;
    support->video        = FALSE;
    support->encode       = FALSE;
    support->decode       = FALSE;
    support->encodeFamily = NO_QUEUE_FAMILY;
    support->decodeFamily = NO_QUEUE_FAMILY;
    cvkEnumerateDeviceExtensionProperties(physical, NULL, &storage->extensionCount, storage->extensions);
    complete = TRUE;
    if (complete)
    {
        support->video =
            HasExtension(storage, VK_KHR_VIDEO_QUEUE_EXTENSION_NAME, sizeof(VK_KHR_VIDEO_QUEUE_EXTENSION_NAME) - 1);
        support->encode = support->video &&
                          HasExtension(storage,
                                       VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME,
                                       sizeof(VK_KHR_VIDEO_ENCODE_QUEUE_EXTENSION_NAME) - 1) &&
                          HasExtension(storage,
                                       VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME,
                                       sizeof(VK_KHR_VIDEO_ENCODE_H264_EXTENSION_NAME) - 1);
        support->decode = support->video &&
                          HasExtension(storage,
                                       VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME,
                                       sizeof(VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME) - 1) &&
                          HasExtension(storage,
                                       VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME,
                                       sizeof(VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME) - 1);
    }
    return complete;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadFamilies(GpuProbe_t* probe, VkPhysicalDevice physical, DeviceSupport_t* support)
{
    GpuProbe_Storage_t* storage;
    uint32_t            video_count;
    uint32_t            encode_family;
    uint32_t            decode_family;
    bool_t              complete;
    storage = probe->storage;
    cvkGetPhysicalDeviceQueueFamilyProperties(physical, &storage->familyCount, storage->families);
    complete = TRUE;
    if (support->video)
    {
        cvkGetPhysicalDeviceQueueFamilyVideoProperties(physical,
                                                      &video_count,
                                                      storage->videoFamilies,
                                                      storage->video);
        complete = CheckCapacity(probe, video_count == storage->familyCount);
        if (complete)
        {
            encode_family =
                cvkGetVideoQueueFamilyIndex(video_count, storage->video, VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR);
            decode_family =
                cvkGetVideoQueueFamilyIndex(video_count, storage->video, VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR);
            if (support->encode)
            {
                support->encodeFamily = encode_family;
            }
            if (support->decode)
            {
                support->decodeFamily = decode_family;
            }
        }
    }
    return complete;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadEncodeProfile(GpuProbe_t* probe, VkPhysicalDevice physical, GpuProbe_Caps_t* candidate)
{
    VkVideoEncodeH264ProfileInfoKHR  h264;
    VkVideoProfileInfoKHR            profile;
    VkVideoEncodeH264CapabilitiesKHR h264_caps;
    VkVideoEncodeCapabilitiesKHR     encode_caps;
    VkVideoCapabilitiesKHR           caps;
    VkResult                         result;
    bool_t                           succeeded;
    bool_t                           complete;
    CRT_Memory_Set(&h264, 0, sizeof(h264));
    CRT_Memory_Set(&profile, 0, sizeof(profile));
    CRT_Memory_Set(&h264_caps, 0, sizeof(h264_caps));
    CRT_Memory_Set(&encode_caps, 0, sizeof(encode_caps));
    CRT_Memory_Set(&caps, 0, sizeof(caps));
    cvkInitVideoEncodeH264ProfileInfo(&h264, NULL, STD_VIDEO_H264_PROFILE_IDC_MAIN);
    cvkInitVideoProfileInfo(&profile,
                            &h264,
                            VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR,
                            VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
                            VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
                            VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR);
    h264_caps.sType   = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_CAPABILITIES_KHR;
    encode_caps.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_CAPABILITIES_KHR;
    encode_caps.pNext = &h264_caps;
    caps.sType        = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
    caps.pNext        = &encode_caps;
    result            = cvkGetPhysicalDeviceVideoCapabilities(physical, &profile, caps.pNext, &caps);
    succeeded         = (VK_SUCCESS == result);
    complete          = succeeded || IsUnsupported(result);
    if (!complete)
    {
        (void)CheckResult(probe, result);
    }
    if (succeeded)
    {
        candidate->available           = TRUE;
        candidate->minCodedWidth       = caps.minCodedExtent.width;
        candidate->minCodedHeight      = caps.minCodedExtent.height;
        candidate->maxCodedWidth       = caps.maxCodedExtent.width;
        candidate->maxCodedHeight      = caps.maxCodedExtent.height;
        candidate->maxDpbSlots         = caps.maxDpbSlots;
        candidate->maxActiveReferences = caps.maxActiveReferencePictures;
    }
    return complete;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadDecodeProfile(GpuProbe_t* probe, VkPhysicalDevice physical, GpuProbe_Caps_t* candidate)
{
    VkVideoDecodeH264ProfileInfoKHR  h264;
    VkVideoProfileInfoKHR            profile;
    VkVideoDecodeH264CapabilitiesKHR h264_caps;
    VkVideoDecodeCapabilitiesKHR     decode_caps;
    VkVideoCapabilitiesKHR           caps;
    VkResult                         result;
    bool_t                           complete;
    CRT_Memory_Set(&h264, 0, sizeof(h264));
    CRT_Memory_Set(&profile, 0, sizeof(profile));
    CRT_Memory_Set(&h264_caps, 0, sizeof(h264_caps));
    CRT_Memory_Set(&decode_caps, 0, sizeof(decode_caps));
    CRT_Memory_Set(&caps, 0, sizeof(caps));
    cvkInitVideoDecodeH264ProfileInfo(&h264,
                                      NULL,
                                      STD_VIDEO_H264_PROFILE_IDC_MAIN,
                                      VK_VIDEO_DECODE_H264_PICTURE_LAYOUT_PROGRESSIVE_KHR);
    cvkInitVideoProfileInfo(&profile,
                            &h264,
                            VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR,
                            VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR,
                            VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR,
                            VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR);
    h264_caps.sType          = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_CAPABILITIES_KHR;
    decode_caps.sType        = VK_STRUCTURE_TYPE_VIDEO_DECODE_CAPABILITIES_KHR;
    decode_caps.pNext        = &h264_caps;
    caps.sType               = VK_STRUCTURE_TYPE_VIDEO_CAPABILITIES_KHR;
    caps.pNext               = &decode_caps;
    result                   = cvkGetPhysicalDeviceVideoCapabilities(physical, &profile, caps.pNext, &caps);
    candidate->canDecodeH264 = (VK_SUCCESS == result);
    complete                 = candidate->canDecodeH264 || IsUnsupported(result);
    if (!complete)
    {
        (void)CheckResult(probe, result);
    }
    return complete;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void ReportSelected(GpuProbe_t* probe, VkPhysicalDevice physical)
{
    uint32_t                       index;
    const VkQueueFamilyProperties* family;
    VkQueueFlags                   flags;
    VkFormatProperties             format;
    VkFormatFeatureFlags           blit;
    bool_t                         can_blit;
    AppendText(probe, "  coded extent ", sizeof("  coded extent ") - 1);
    AppendNumber(probe, probe->caps.minCodedWidth);
    AppendText(probe, "x", sizeof("x") - 1);
    AppendNumber(probe, probe->caps.minCodedHeight);
    AppendText(probe, "..", sizeof("..") - 1);
    AppendNumber(probe, probe->caps.maxCodedWidth);
    AppendText(probe, "x", sizeof("x") - 1);
    AppendNumber(probe, probe->caps.maxCodedHeight);
    AppendText(probe, ", dpb slots ", sizeof(", dpb slots ") - 1);
    AppendNumber(probe, probe->caps.maxDpbSlots);
    AppendText(probe, ", active refs ", sizeof(", active refs ") - 1);
    AppendNumber(probe, probe->caps.maxActiveReferences);
    AppendText(probe, "\n", sizeof("\n") - 1);
    for (index = 0; index < probe->storage->familyCount; ++index)
    {
        family = &probe->storage->families[index];
        flags  = family->queueFlags;
        AppendText(probe, "  queue family ", sizeof("  queue family ") - 1);
        AppendNumber(probe, index);
        AppendText(probe, ": ", sizeof(": ") - 1);
        AppendNumber(probe, family->queueCount);
        AppendText(probe, " queues", sizeof(" queues") - 1);
        if (flags & VK_QUEUE_GRAPHICS_BIT)
        {
            AppendText(probe, " graphics", sizeof(" graphics") - 1);
        }
        if (flags & VK_QUEUE_COMPUTE_BIT)
        {
            AppendText(probe, " compute", sizeof(" compute") - 1);
        }
        if (flags & VK_QUEUE_TRANSFER_BIT)
        {
            AppendText(probe, " transfer", sizeof(" transfer") - 1);
        }
        if (flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR)
        {
            AppendText(probe, " decode", sizeof(" decode") - 1);
        }
        if (flags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR)
        {
            AppendText(probe, " encode", sizeof(" encode") - 1);
        }
        AppendText(probe, "\n", sizeof("\n") - 1);
    }
    blit = (VkFormatFeatureFlags)((VkFlags)VK_FORMAT_FEATURE_BLIT_SRC_BIT | (VkFlags)VK_FORMAT_FEATURE_BLIT_DST_BIT);
    cvkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_G8_B8R8_2PLANE_420_UNORM, &format);
    can_blit = ((format.optimalTilingFeatures & blit) == blit);
    AppendText(probe, "  blit: nv12 ", sizeof("  blit: nv12 ") - 1);
    AppendQuestion(probe, can_blit);
    cvkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8_UNORM, &format);
    can_blit = ((format.optimalTilingFeatures & blit) == blit);
    AppendText(probe, ", R8 ", sizeof(", R8 ") - 1);
    AppendQuestion(probe, can_blit);
    cvkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8_UNORM, &format);
    can_blit = ((format.optimalTilingFeatures & blit) == blit);
    AppendText(probe, ", R8G8 ", sizeof(", R8G8 ") - 1);
    AppendQuestion(probe, can_blit);
    AppendText(probe, "\n", sizeof("\n") - 1);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadDevice(GpuProbe_t* probe, VkPhysicalDevice physical)
{
    VkPhysicalDeviceProperties2 properties;
    GpuProbe_Caps_t             candidate;
    uint32_t                    name_bytes;
    bool_t                      complete;
    DeviceSupport_t             support;
    bool_t                      eligible;
    bool_t                      needs_profile;
    bool_t                      has_dispatch;
    CRT_Memory_Set(&properties, 0, sizeof(properties));
    CRT_Memory_Set(&candidate, 0, sizeof(candidate));
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    cvkGetPhysicalDeviceProperties2(physical, NULL, &properties);
    for (name_bytes = 0; name_bytes < GPU_PROBE_DEVICE_NAME_CAPACITY; ++name_bytes)
    {
        if (REPORT_NUL == properties.properties.deviceName[name_bytes])
        {
            break;
        }
    }
    complete = CheckCapacity(probe, name_bytes < GPU_PROBE_DEVICE_NAME_CAPACITY);
    if (complete)
    {
        CRT_Memory_Copy(candidate.deviceName, properties.properties.deviceName, (uint64_t)name_bytes + 1);
        complete = ReadExtensions(probe, physical, &support);
    }
    eligible = complete && (VK_API_VERSION_1_3 <= properties.properties.apiVersion);
    if (eligible)
    {
        complete = ReadFamilies(probe, physical, &support);
        if (complete)
        {
            candidate.encodeQueueFamily = support.encodeFamily;
            candidate.decodeQueueFamily = support.decodeFamily;
            needs_profile = (NO_QUEUE_FAMILY != support.encodeFamily) || (NO_QUEUE_FAMILY != support.decodeFamily);
            has_dispatch  = (NULL != vkGetPhysicalDeviceVideoCapabilitiesKHR);
            if (needs_profile && !has_dispatch)
            {
                probe->status = GPU_PROBE_DISPATCH_FAILED;
                complete      = FALSE;
            }
            if (complete && (NO_QUEUE_FAMILY != support.encodeFamily))
            {
                complete = ReadEncodeProfile(probe, physical, &candidate);
            }
            if (complete && (NO_QUEUE_FAMILY != support.decodeFamily))
            {
                complete = ReadDecodeProfile(probe, physical, &candidate);
            }
        }
    }
    if (complete)
    {
        AppendText(probe, candidate.deviceName, name_bytes);
        AppendText(probe, ": encode-h264 ", sizeof(": encode-h264 ") - 1);
        AppendQuestion(probe, candidate.available);
        AppendText(probe, ", decode-h264 ", sizeof(", decode-h264 ") - 1);
        AppendQuestion(probe, candidate.canDecodeH264);
        AppendText(probe, "\n", sizeof("\n") - 1);
        if (candidate.available)
        {
            probe->caps = candidate;
            ReportSelected(probe, physical);
        }
    }
    return complete;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t OpenNative(GpuProbe_t* probe)
{
    VkResult          result;
    bool_t            ready;
    uint32_t          version;
    bool_t            has_version_query;
    bool_t            supports_version;
    VkApplicationInfo application;
    result = volkInitialize();
    ready  = CheckResult(probe, result);
    if (ready)
    {
        probe->storage->loader = TRUE;
        version                = VK_API_VERSION_1_0;
        has_version_query      = (NULL != vkEnumerateInstanceVersion);
        if (has_version_query)
        {
            cvkEnumerateInstanceVersion(&version);
        }
        supports_version = (VK_API_VERSION_1_3 <= version);
        ready            = supports_version;
        if (!supports_version)
        {
            probe->status = GPU_PROBE_API_UNSUPPORTED;
            AppendText(probe, "Vulkan 1.3 is unavailable\n", sizeof("Vulkan 1.3 is unavailable\n") - 1);
        }
    }
    if (ready)
    {
        cvkInitApplicationInfo(&application,
                               NULL,
                               PROBE_APPLICATION_NAME,
                               PROBE_APPLICATION_VERSION,
                               PROBE_ENGINE_NAME,
                               PROBE_ENGINE_VERSION,
                               VK_API_VERSION_1_3);
        cvkCreateInstance(NULL, 0, &application, 0, NULL, 0, NULL, NULL, &probe->storage->instance);
        volkLoadInstanceOnly(probe->storage->instance);
    }
    return ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t CloseNative(GpuProbe_Storage_t* storage)
{
    bool_t has_instance;
    has_instance = (VK_NULL_HANDLE != storage->instance);
    if (has_instance)
    {
        cvkDestroyInstance(storage->instance, NULL);
        storage->instance = VK_NULL_HANDLE;
    }
    if (storage->loader)
    {
        volkFinalize();
        storage->loader = FALSE;
    }
    return TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t Collect(GpuProbe_t* probe)
{
    GpuProbe_Storage_t* storage;
    bool_t              complete;
    uint32_t            index;
    storage = probe->storage;
    cvkEnumeratePhysicalDevices(storage->instance, &storage->deviceCount, storage->devices);
    complete = TRUE;
    for (index = 0; index < storage->deviceCount; ++index)
    {
        complete = ReadDevice(probe, storage->devices[index]);
        if ((!complete) || probe->caps.available)
        {
            break;
        }
    }
    return complete;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t GpuProbe_Initialize(GpuProbe_t* probe)
{
    bool_t ready;
    bool_t native_closed;
    LOGASSERT(NULL != probe);
    probe->storage = NULL;
    CRT_Memory_Set(&probe->caps, 0, sizeof(GpuProbe_Caps_t));
    probe->status          = GPU_PROBE_INITIALIZING;
    probe->nativeResult    = 0;
    probe->reportBytes     = 0;
    probe->reportTruncated = FALSE;
    probe->report[0]       = REPORT_NUL;
    ready                  = OS_Memory_Initialize(&probe->memory, sizeof(GpuProbe_Storage_t));
    if (ready)
    {
        probe->storage           = (GpuProbe_Storage_t*)OS_Memory_GetAddress(&probe->memory);
        probe->storage->instance = VK_NULL_HANDLE;
        probe->storage->loader   = FALSE;
        ready                    = OpenNative(probe);
        if (ready)
        {
            ready = Collect(probe);
        }
        native_closed = CloseNative(probe->storage);
        ready         = ready && native_closed;
    }
    else
    {
        probe->status = GPU_PROBE_STORAGE_FAILED;
    }
    if (ready)
    {
        probe->status = GPU_PROBE_READY;
        if (!probe->caps.available)
        {
            AppendText(probe,
                       "no Vulkan Video H.264 encoder on this system\n",
                       sizeof("no Vulkan Video H.264 encoder on this system\n") - 1);
        }
    }
    else
    {
        CRT_Memory_Set(&probe->caps, 0, sizeof(GpuProbe_Caps_t));
        AppendText(probe, "GPU probe failed; status ", sizeof("GPU probe failed; status ") - 1);
        AppendNumber(probe, (uint32_t)probe->status);
        AppendText(probe, "\n", sizeof("\n") - 1);
    }
    return ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
GpuProbe_Status_t GpuProbe_GetStatus(const GpuProbe_t* probe)
{
    LOGASSERT(NULL != probe);
    return probe->status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
const GpuProbe_Caps_t* GpuProbe_GetCaps(const GpuProbe_t* probe)
{
    LOGASSERT(NULL != probe);
    return &probe->caps;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
const char_t* GpuProbe_GetReport(const GpuProbe_t* probe)
{
    LOGASSERT(NULL != probe);
    return probe->report;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t GpuProbe_GetReportByteCount(const GpuProbe_t* probe)
{
    LOGASSERT(NULL != probe);
    return probe->reportBytes;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
int32_t GpuProbe_GetNativeResult(const GpuProbe_t* probe)
{
    LOGASSERT(NULL != probe);
    return probe->nativeResult;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t GpuProbe_IsReportTruncated(const GpuProbe_t* probe)
{
    LOGASSERT(NULL != probe);
    return probe->reportTruncated;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t GpuProbe_TryClose(GpuProbe_t* probe)
{
    bool_t native_closed;
    bool_t closed;
    LOGASSERT(NULL != probe);
    native_closed = TRUE;
    if (NULL != probe->storage)
    {
        native_closed = CloseNative(probe->storage);
    }
    closed = FALSE;
    if (native_closed)
    {
        probe->storage = NULL;
        closed         = OS_Memory_TryClose(&probe->memory);
    }
    if (closed)
    {
        probe->status = GPU_PROBE_CLOSED;
    }
    return closed;
}
