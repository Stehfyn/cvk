/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_adapter.h"
#include "cd3d12.h"
#include "cdxgi.h"

#include "os/os_assert.h"
#include "os/os_malloc.h"
#include "os/os_types.h"
#include "utl/utl_defines.h"

#include <d3d12sdklayers.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define ADAPTER_NAME_CAPACITY      (256u)  ///< UTF-8 adapter-name capacity including the terminator.
#define DXGI_MESSAGE_BYTE_CAPACITY (4096u) ///< Stack capacity for one serialized DXGI debug message.
#define DXGICHECK(expression)                                                                                          \
    (CheckDxgiResult((expression),                                                                                     \
                     #expression,                                                                                      \
                     __FILE__,                                                                                         \
                     __LINE__)) ///< Makes a failed DXGI operation fatal at its call site.
#define HRCHECK(expression)                                                                                            \
    (CheckHresult((expression),                                                                                        \
                  #expression,                                                                                         \
                  __FILE__,                                                                                            \
                  __LINE__)) ///< Checks D3D12 and diagnostic calls without draining the DXGI queue.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Word-aligned storage for one serialized DXGI diagnostic message.
 */
typedef union DxgiMessageStorage_u
{
    uint64_t alignment;                         ///< Forces alignment suitable for DXGI_INFO_QUEUE_MESSAGE.
    byte_t   bytes[DXGI_MESSAGE_BYTE_CAPACITY]; ///< Serialized message storage.
} DxgiMessageStorage_t;

/*************************************************************************************************/
/**
 * \brief Display name and device identifier for a physical graphics adapter.
 */
struct Gfx_Adapter_t
{
    char     name[ADAPTER_NAME_CAPACITY]; ///< Null-terminated UTF-8 DXGI adapter description.
    uint64_t id;                          ///< Adapter LUID bits used for cross-API matching.
};

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static BOOL            g_dxgi_debug_initialized = FALSE;
static IDXGIInfoQueue* g_dxgi_info_queue        = NULL;

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Enables the D3D12 debug layer and all DRED diagnostics.
 *
 * \retval    None No value is returned.
 */
static void EnableD3d12DebugLogging(void);

/*************************************************************************************************/
/**
 * \brief Maps a DXGI info-queue severity to its log label.
 *
 * \param[in] severity        Corruption, error, warning, info, or message severity
 *
 * \return Static lowercase label; unsupported enum values are fatal.
 */
static const char* GetDxgiMessageSeverityName(DXGI_INFO_QUEUE_MESSAGE_SEVERITY severity);

/*************************************************************************************************/
/**
 * \brief Serializes, prints, and clears every queued DXGI diagnostic.
 *
 * \retval    None No value is returned.
 */
static void ReportDxgiMessages(void);

/*************************************************************************************************/
/**
 * \brief Initializes the process DXGI diagnostic queue.
 *
 * \retval    None No value is returned.
 */
static void InitializeDxgiDebug(void);

/*************************************************************************************************/
/**
 * \brief Selects DXGI factory creation flags.
 *
 * \return DXGI_CREATE_FACTORY_DEBUG.
 */
static UINT GetDxgiFactoryFlags(void);

/*************************************************************************************************/
/**
 * \brief Reports queued diagnostics and converts a failed HRESULT into a fatal log.
 *
 * \param[in] hr                 HRESULT returned by the checked DXGI operation
 * \param[in] pExpression        Source expression displayed on failure
 * \param[in] pFile              Source file reported to Os_LogFatal
 * \param[in] line               Source line reported to Os_LogFatal
 *
 * \retval    None No value is returned.
 */
static void CheckDxgiResult(HRESULT hr, const char* pExpression, const char* pFile, int line);

/*************************************************************************************************/
/**
 * \brief Converts a failed HRESULT into a fatal diagnostic without querying further diagnostics.
 *
 * \param[in] hr                 Result returned by the checked operation
 * \param[in] pExpression        Source expression displayed on failure
 * \param[in] pFile              Source file reported to Os_LogFatal
 * \param[in] line               Source line reported to Os_LogFatal
 *
 * \retval    None No value is returned.
 */
static void CheckHresult(HRESULT hr, const char* pExpression, const char* pFile, int line);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void EnableD3d12DebugLogging(void)
{
    ID3D12Debug*                              debug;
    ID3D12DeviceRemovedExtendedDataSettings1* dred_settings;

    debug = NULL;
    HRCHECK(cd3d12GetDebugInterfaceAsDebug(&debug));
    cd3d12DebugEnableDebugLayer(debug);
    (void)cd3d12UnknownRelease((IUnknown*)debug);

    dred_settings = NULL;
    HRCHECK(cd3d12GetDebugInterfaceAsDeviceRemovedExtendedDataSettings1(&dred_settings));
    cd3d12DeviceRemovedExtendedDataSettingsSetAutoBreadcrumbsEnablement(
        (ID3D12DeviceRemovedExtendedDataSettings*)dred_settings,
        D3D12_DRED_ENABLEMENT_FORCED_ON);
    cd3d12DeviceRemovedExtendedDataSettingsSetPageFaultEnablement(
        (ID3D12DeviceRemovedExtendedDataSettings*)dred_settings,
        D3D12_DRED_ENABLEMENT_FORCED_ON);
    cd3d12DeviceRemovedExtendedDataSettingsSetBreadcrumbContextEnablement(dred_settings,
                                                                         D3D12_DRED_ENABLEMENT_FORCED_ON);
    (void)cd3d12UnknownRelease((IUnknown*)dred_settings);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static const char* GetDxgiMessageSeverityName(DXGI_INFO_QUEUE_MESSAGE_SEVERITY severity)
{
    switch (severity)
    {
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION:
        return "corruption";
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR:
        return "error";
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING:
        return "warning";
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_INFO:
        return "info";
    case DXGI_INFO_QUEUE_MESSAGE_SEVERITY_MESSAGE:
        return "message";
    default:
        LOGFATAL("unsupported DXGI message severity");
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ReportDxgiMessages(void)
{
    DxgiMessageStorage_t message_storage;
    uint64_t             message_count;
    uint64_t             message_index;

    if (NULL == g_dxgi_info_queue)
    {
        return;
    }
    message_count = cdxgiInfoQueueGetNumStoredMessagesAllowedByRetrievalFilters(g_dxgi_info_queue, DXGI_DEBUG_ALL);
    for (message_index = 0; message_index < message_count; ++message_index)
    {
        DXGI_INFO_QUEUE_MESSAGE* message;
        size_t                   message_size;

        message_size = sizeof(message_storage.bytes);
        HRCHECK(cdxgiInfoQueueGetMessage(g_dxgi_info_queue, DXGI_DEBUG_ALL, message_index, NULL, &message_size));
        LOGASSERT(sizeof(message_storage.bytes) >= message_size);

        message = (DXGI_INFO_QUEUE_MESSAGE*)message_storage.bytes;
        HRCHECK(cdxgiInfoQueueGetMessage(g_dxgi_info_queue, DXGI_DEBUG_ALL, message_index, message, &message_size));
        LOGSTART(stderr);
        LOGPRINTS("DXGI ");
        LOGPRINTS(GetDxgiMessageSeverityName(message->Severity));
        LOGPRINTS(" [category ");
        LOGPRINTI((int64_t)((int)message->Category));
        LOGPRINTS(", id ");
        LOGPRINTI((int64_t)((int)message->ID));
        LOGPRINTS("]: ");
        LOGPRINTS(message->pDescription);
        LOGPRINTS("\n");
        LOGEND();
    }
    cdxgiInfoQueueClearStoredMessages(g_dxgi_info_queue, DXGI_DEBUG_ALL);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void InitializeDxgiDebug(void)
{
    if (FALSE == g_dxgi_debug_initialized)
    {
        HRCHECK(cdxgiGetDebugInterface1AsInfoQueue(0, &g_dxgi_info_queue));
        cdxgiInfoQueueSetMuteDebugOutput(g_dxgi_info_queue, DXGI_DEBUG_ALL, TRUE);
        g_dxgi_debug_initialized = TRUE;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static UINT GetDxgiFactoryFlags(void)
{
    return DXGI_CREATE_FACTORY_DEBUG;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CheckDxgiResult(HRESULT hr, const char* pExpression, const char* pFile, int line)
{
    if (FAILED(hr))
    {
        LOGSTART(stderr);
        LOGPRINTS(pFile);
        LOGPRINTS("(");
        LOGPRINTI((int64_t)(line));
        LOGPRINTS("): ");
        LOGPRINTS(pExpression);
        LOGPRINTS(" failed with HRESULT 0x");
        LOGPRINTX((uint64_t)((unsigned long)hr), 8, TRUE);
        LOGPRINTS("\n");
        LOGEND();
    }
    ReportDxgiMessages();
    CheckHresult(hr, pExpression, pFile, line);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CheckHresult(HRESULT hr, const char* pExpression, const char* pFile, int line)
{
    if (FAILED(hr))
    {
        char message[512];

        LOGSTARTBUFFER(message, sizeof(message));
        LOGPRINTS(pExpression);
        LOGPRINTS(" failed with HRESULT 0x");
        LOGPRINTX((uint64_t)(uint32_t)hr, 8, TRUE);
        LOGEND();
        Os_LogFatal(message, pFile, line);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Adapter_Enumerate(Gfx_Adapter_t** adapters, size_t adapterCapacity, size_t* adapterCount)
{
    IDXGIFactory6* factory;
    unsigned int   ordinal;

    LOGASSERT(NULL != adapters);
    LOGASSERT(0 != adapterCapacity);
    LOGASSERT(NULL != adapterCount);
    EnableD3d12DebugLogging();
    InitializeDxgiDebug();
    factory = NULL;
    cdxgiCreateFactory2AsFactory6(GetDxgiFactoryFlags(), &factory);
    LOGASSERT(NULL != factory);

    (*adapterCount) = 0;
    for (ordinal = 0; (*adapterCount) < adapterCapacity; ++ordinal)
    {
        IDXGIAdapter1*     adapter;
        DXGI_ADAPTER_DESC1 description;
        HRESULT            hr;

        adapter = NULL;
        hr      = cdxgiFactoryEnumAdapterByGpuPreferenceAsAdapter1(factory,
                                                                   ordinal,
                                                                   DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                                   &adapter);
        if (DXGI_ERROR_NOT_FOUND == hr)
        {
            break;
        }
        DXGICHECK(hr);
        (void)memset(&description, 0, sizeof(description));
        cdxgiAdapterGetDesc1(adapter, &description);
        if (0 == (description.Flags & DXGI_ADAPTER_FLAG_SOFTWARE))
        {
            Gfx_Adapter_t* output;

            output = (Gfx_Adapter_t*)OS_CALLOC(sizeof((*output)));
            (void)memcpy(&output->id, &description.AdapterLuid, sizeof(description.AdapterLuid));
            if (0 == WideCharToMultiByte(CP_UTF8,
                                         0,
                                         description.Description,
                                         -1,
                                         output->name,
                                         (int)sizeof(output->name),
                                         NULL,
                                         NULL))
            {
                (void)strcpy_s(output->name, sizeof(output->name), "<unprintable adapter>");
            }
            adapters[(*adapterCount)] = output;
            ++(*adapterCount);
        }
        (void)cdxgiUnknownRelease((IUnknown*)adapter);
    }
    (void)cdxgiUnknownRelease((IUnknown*)factory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
const char* Gfx_Adapter_GetName(const Gfx_Adapter_t* adapter)
{
    LOGASSERT(NULL != adapter);

    return adapter->name;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint64_t Gfx_Adapter_GetId(const Gfx_Adapter_t* adapter)
{
    LOGASSERT(NULL != adapter);

    return adapter->id;
}
