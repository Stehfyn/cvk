#ifndef CAST_GPU_PROBE_H
#define CAST_GPU_PROBE_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Startup-owned, cached Vulkan Video H.264 capability inventory.
 * Driver work occurs only during initialization and native failure cleanup.
 * This does not establish startup ownership of decoder/encoder sessions.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#include "os/os_memory.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define GPU_PROBE_DEVICE_NAME_CAPACITY (256)
#define GPU_PROBE_REPORT_CAPACITY      (8192)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Whole-snapshot and owner lifetime outcomes.
 */
typedef enum GpuProbe_Status_e
{
    GPU_PROBE_INITIALIZING,
    GPU_PROBE_READY,
    GPU_PROBE_STORAGE_FAILED,
    GPU_PROBE_NATIVE_FAILED,
    GPU_PROBE_API_UNSUPPORTED,
    GPU_PROBE_DISPATCH_FAILED,
    GPU_PROBE_CAPACITY_EXCEEDED,
    GPU_PROBE_CLOSED
} GpuProbe_Status_t;

/*************************************************************************************************/
/**
 * \brief Selected Main-profile capabilities; no Vulkan handles escape the snapshot.
 */
typedef struct GpuProbe_Caps_s
{
    bool_t   available;     ///< A suitable encoder profile and queue were found.
    bool_t   canDecodeH264; ///< The selected device supports the requested progressive decode profile.
    char_t   deviceName[GPU_PROBE_DEVICE_NAME_CAPACITY]; ///< Terminated native UTF-8 device name.
    uint32_t encodeQueueFamily;                          ///< Queue supporting H.264 encode specifically.
    uint32_t decodeQueueFamily;                          ///< Valid only when canDecodeH264 is TRUE.
    uint32_t minCodedWidth;                              ///< Native encode minimum width.
    uint32_t minCodedHeight;                             ///< Native encode minimum height.
    uint32_t maxCodedWidth;                              ///< Native encode maximum width.
    uint32_t maxCodedHeight;                             ///< Native encode maximum height.
    uint32_t maxDpbSlots;                                ///< Native profile DPB limit.
    uint32_t maxActiveReferences;                        ///< Native profile active-reference limit.
} GpuProbe_Caps_t;

/*************************************************************************************************/
/**
 * \brief Stable owner; fields are private to this module and must not be mutated.
 */
typedef struct GpuProbe_s
{
    OS_Memory_t                memory;          ///< Resident enumeration storage acquired at startup.
    struct GpuProbe_Storage_s* storage;         ///< Private native lifetime and enumeration workspace.
    GpuProbe_Caps_t            caps;            ///< Cached selected record, empty after failed collection.
    GpuProbe_Status_t          status;          ///< Cached collection or closure outcome.
    int32_t                    nativeResult;    ///< First fatal native failure; zero otherwise.
    uint32_t                   reportBytes;     ///< Written bytes excluding the final NUL.
    bool_t                     reportTruncated; ///< Saturation latch; later fragments are omitted.
    char_t                     report[GPU_PROBE_REPORT_CAPACITY]; ///< Cached diagnostic bytes.
} GpuProbe_t;

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
 * \brief Collects a bounded Vulkan Video capability snapshot at program startup.
 *
 * \par Requirements:
 *  - 001 Required fresh or completely closed owner storage shall assert when absent.
 *  - 002 The caller shall exclusively own Volk initialization and instance dispatch during this call and any pending closure.
 *  - 003 All enumeration storage and driver work shall be acquired here, before normal interaction.
 *  - 004 Loader/API support shall be queried before instance creation; CVK construction contracts shall remain fatal.
 *  - 005 A completed snapshot without a suitable encoder shall be successful with available FALSE.
 *  - 006 The selected profile shall be H.264 Main, 8-bit 4:2:0; decode support shall additionally require progressive pictures.
 *  - 007 Native instance/loader cleanup shall be attempted before return; partial ownership shall remain closable.
 *  - 008 Unsupported capability queries and insufficient application capacity shall publish no selected capabilities.
 *
 * \param[out] probe   Stable caller-owned storage
 *
 * \retval TRUE    A complete cached snapshot is ready.
 * \retval FALSE   Startup failed; inspect status and retain storage through TryClose.
 */
bool_t GpuProbe_Initialize(GpuProbe_t* probe);

/*************************************************************************************************/
/**
 * \brief Gets the cached snapshot or lifetime status.
 *
 * \par Requirements:
 *  - 001 The owner shall be initialized; no native work shall occur.
 *
 * \param[in] probe   Initialized owner
 *
 * \retval status   Cached status, including after closure.
 */
GpuProbe_Status_t GpuProbe_GetStatus(const GpuProbe_t* probe);

/*************************************************************************************************/
/**
 * \brief Borrows the selected capability record.
 *
 * \par Requirements:
 *  - 001 The owner shall be initialized; the borrow shall remain valid through closure until reinitialization.
 *  - 002 Failed collection shall expose an empty record; no native work or allocation shall occur.
 *
 * \param[in] probe   Initialized owner
 *
 * \retval caps   Immutable record retained in the caller-owned object.
 */
const GpuProbe_Caps_t* GpuProbe_GetCaps(const GpuProbe_t* probe);

/*************************************************************************************************/
/**
 * \brief Borrows the cached terminated UTF-8 diagnostic report.
 *
 * \par Requirements:
 *  - 001 The report shall remain valid through closure until reinitialization.
 *  - 002 Truncation shall be reported separately and shall not cause a buffer overrun.
 *
 * \param[in] probe   Initialized owner
 *
 * \retval report   Immutable terminated bytes; no native work occurs.
 */
const char_t* GpuProbe_GetReport(const GpuProbe_t* probe);

/*************************************************************************************************/
/**
 * \brief Gets the diagnostic byte count excluding termination.
 *
 * \par Requirements:
 *  - 001 The initialized owner shall retain this value through closure.
 *
 * \param[in] probe   Initialized owner
 *
 * \retval bytes   Exact initialized report bytes.
 */
uint32_t GpuProbe_GetReportByteCount(const GpuProbe_t* probe);

/*************************************************************************************************/
/**
 * \brief Gets the first fatal Vulkan result recorded during collection.
 *
 * \par Requirements:
 *  - 001 Ordinary unsupported profiles shall not count as fatal errors.
 *
 * \param[in] probe   Initialized owner
 *
 * \retval 0   No fatal Vulkan result.
 * \retval nonzero   Retained native result.
 */
int32_t GpuProbe_GetNativeResult(const GpuProbe_t* probe);

/*************************************************************************************************/
/**
 * \brief Queries whether an entire diagnostic fragment could not fit.
 *
 * \par Requirements:
 *  - 001 After saturation no later fragments shall be appended.
 *
 * \param[in] probe   Initialized owner
 *
 * \retval TRUE    Diagnostic capacity was exceeded.
 * \retval FALSE   Every generated fragment fit.
 */
bool_t GpuProbe_IsReportTruncated(const GpuProbe_t* probe);

/*************************************************************************************************/
/**
 * \brief Retires startup resources while preserving cached values and failed identities.
 *
 * \par Requirements:
 *  - 001 The caller shall serialize lifetime operations and finish Initialize before closure.
 *  - 002 Native instance/loader retirement shall precede memory release.
 *  - 003 Failed native/storage cleanup shall retain ownership for retry; repeated successful closure shall be harmless.
 *
 * \param[in,out] probe   Initialized owner, including after failed startup
 *
 * \retval TRUE    No owned native resource remains.
 * \retval FALSE   Retain storage and retry.
 */
bool_t GpuProbe_TryClose(GpuProbe_t* probe);

#endif /* CAST_GPU_PROBE_H */
