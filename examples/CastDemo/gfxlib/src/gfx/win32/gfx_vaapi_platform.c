/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <windows.h>
#include <d3d12.h>
#include <va/va_win32.h>

#include "gfx/gfx_vaapi_platform.h"
#include "gfx/gfx_vaapi.h"
#include "utl/utl_defines.h"
#include "os/os_assert.h"

#include <string.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

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
 * \brief Creates a persistent VA display connection for a Windows graphics adapter.
 *
 * \param[in] pAdapterLuid Non-NULL locally unique identifier for the selected adapter
 *
 * \return Non-NULL VA display suitable for Gfx_Vaapi_Initialize().
 */
static VADisplay GetDisplayWin32(const LUID* pAdapterLuid);

/*************************************************************************************************/
/**
 * \brief Imports a Windows NT handle as a VA encoder surface.
 *
 * \param[in]  dpy         Initialized VA display
 * \param[in]  format      VA_RT_FORMAT_* render-target format
 * \param[in]  pixelFormat VA four-character pixel format
 * \param[in]  width       Nonzero surface width in pixels
 * \param[in]  height      Nonzero surface height in pixels
 * \param[in]  handle      Valid NT handle for the external allocation
 * \param[out] surfaceId   Initially invalid storage receiving the imported surface
 *
 * \retval    None
 */
static void ImportSurfaceWin32(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, HANDLE handle, VASurfaceID* surfaceId);

/*************************************************************************************************/
/**
 * \brief Imports a Windows NT handle into an export-capable VA surface and replaces the handle with a VA export.
 *
 * \param[in]  dpy           Initialized VA display
 * \param[in]  format        VA_RT_FORMAT_* render-target format
 * \param[in]  pixelFormat   VA four-character pixel format
 * \param[in]  width         Nonzero surface width in pixels
 * \param[in]  height        Nonzero surface height in pixels
 * \param[in]  importHandle  Valid NT handle imported by the created surface
 * \param[out] surfaceId     Initially invalid storage receiving the created surface
 * \param[out] pExportHandle Initially NULL storage receiving an owned VA export
 *
 * \retval    None
 */
static void CreateExportedSurfaceWin32(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, HANDLE importHandle, VASurfaceID* surfaceId, HANDLE* pExportHandle);

/*************************************************************************************************/
/**
 * \brief Imports a D3D12 resource as a VA encoder surface.
 *
 * \param[in]  dpy         Initialized VA display
 * \param[in]  format      VA render-target format
 * \param[in]  pixelFormat VA four-character pixel format
 * \param[in]  width       Surface width in pixels
 * \param[in]  height      Surface height in pixels
 * \param[in]  pResource   D3D12 texture resource imported by the VA surface
 * \param[out] surfaceId   Receives the created VA surface
 *
 * \retval    None
 */
static void ImportSurfaceD3D12ResourceWin32(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, ID3D12Resource* pResource, VASurfaceID* surfaceId);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static VADisplay GetDisplayWin32(const LUID* pAdapterLuid)
{
    VADisplay dpy;

    dpy = vaGetDisplayWin32(pAdapterLuid);
    LOGASSERT(NULL != dpy);

    return dpy;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ImportSurfaceWin32(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, HANDLE handle, VASurfaceID* surfaceId)
{
    VASurfaceAttrib attrib_list[4];
    void*           handles[1];

    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != handle);
    LOGASSERT(NULL != surfaceId);
    LOGASSERT(VA_INVALID_SURFACE == (*surfaceId));

    handles[0] = handle;
    (void)memset(attrib_list, 0, sizeof(attrib_list));
    attrib_list[0].type          = VASurfaceAttribMemoryType;
    attrib_list[0].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[0].value.type    = VAGenericValueTypeInteger;
    attrib_list[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE;
    attrib_list[1].type          = VASurfaceAttribExternalBufferDescriptor;
    attrib_list[1].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[1].value.type    = VAGenericValueTypePointer;
    attrib_list[1].value.value.p = handles;
    attrib_list[2].type          = VASurfaceAttribPixelFormat;
    attrib_list[2].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[2].value.type    = VAGenericValueTypeInteger;
    attrib_list[2].value.value.i = (int)pixelFormat;
    attrib_list[3].type          = VASurfaceAttribUsageHint;
    attrib_list[3].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[3].value.type    = VAGenericValueTypeInteger;
    attrib_list[3].value.value.i = VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER;
    Gfx_Vaapi_CreateSurfaces(dpy, format, width, height, surfaceId, 1, attrib_list, (unsigned int)COUNT_OF(attrib_list));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateExportedSurfaceWin32(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, HANDLE importHandle, VASurfaceID* surfaceId, HANDLE* pExportHandle)
{
    VASurfaceAttrib attrib_list[2];

    LOGASSERT(NULL != importHandle);
    LOGASSERT(NULL != pExportHandle);
    LOGASSERT(NULL == (*pExportHandle));

    (void)memset(attrib_list, 0, sizeof(attrib_list));
    attrib_list[0].type          = VASurfaceAttribMemoryType;
    attrib_list[0].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[0].value.type    = VAGenericValueTypeInteger;
    attrib_list[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE;
    attrib_list[1].type          = VASurfaceAttribExternalBufferDescriptor;
    attrib_list[1].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[1].value.type    = VAGenericValueTypePointer;
    attrib_list[1].value.value.p = &importHandle;
    Gfx_Vaapi_CreateExportableSurface(dpy, format, pixelFormat, width, height, surfaceId, attrib_list, (unsigned int)COUNT_OF(attrib_list));
    Gfx_Vaapi_ExportSurfaceHandle(dpy, (*surfaceId), VA_SURFACE_ATTRIB_MEM_TYPE_NTHANDLE, VA_EXPORT_SURFACE_WRITE_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS, pExportHandle);
    LOGASSERT(NULL != (*pExportHandle));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Supplies the D3D12 texture through the native-resource memory type. The VA driver retains its
 *  own reference while the surface exists.
 */
static void ImportSurfaceD3D12ResourceWin32(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, ID3D12Resource* pResource, VASurfaceID* surfaceId)
{
    VASurfaceAttrib attrib_list[4];

    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != pResource);
    LOGASSERT(NULL != surfaceId);
    LOGASSERT(VA_INVALID_SURFACE == (*surfaceId));

    attrib_list[0].type          = VASurfaceAttribMemoryType;
    attrib_list[0].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[0].value.type    = VAGenericValueTypeInteger;
    attrib_list[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_D3D12_RESOURCE;
    attrib_list[1].type          = VASurfaceAttribExternalBufferDescriptor;
    attrib_list[1].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[1].value.type    = VAGenericValueTypePointer;
    attrib_list[1].value.value.p = &pResource;
    attrib_list[2].type          = VASurfaceAttribPixelFormat;
    attrib_list[2].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[2].value.type    = VAGenericValueTypeInteger;
    attrib_list[2].value.value.i = (int)pixelFormat;
    attrib_list[3].type          = VASurfaceAttribUsageHint;
    attrib_list[3].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[3].value.type    = VAGenericValueTypeInteger;
    attrib_list[3].value.value.i = VA_SURFACE_ATTRIB_USAGE_HINT_ENCODER;
    Gfx_Vaapi_CreateSurfaces(dpy, format, width, height, surfaceId, 1, attrib_list, (unsigned int)COUNT_OF(attrib_list));
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
VADisplay Gfx_Vaapi_Platform_GetDisplay(uint64_t adapterId)
{
    LUID adapter_luid;

    (void)memcpy(&adapter_luid, &adapterId, sizeof(adapter_luid));

    return GetDisplayWin32(&adapter_luid);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_Platform_CreateExportedSurface(VADisplay    dpy,
                                              uint32_t     format,
                                              uint32_t     pixelFormat,
                                              uint32_t     width,
                                              uint32_t     height,
                                              void**       sharedHandle,
                                              void*        drmPrimeDescriptor,
                                              VASurfaceID* surfaceId)
{
    HANDLE  import_handle;
    HANDLE* shared_handle;
    BOOL    handle_closed;

    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != surfaceId);

    (void)drmPrimeDescriptor;
    shared_handle = (HANDLE*)sharedHandle;
    LOGASSERT(NULL != shared_handle);
    LOGASSERT(NULL != (*shared_handle));

    import_handle    = (*shared_handle);
    (*shared_handle) = NULL;
    CreateExportedSurfaceWin32(dpy, format, pixelFormat, width, height, import_handle, surfaceId, shared_handle);
    handle_closed = CloseHandle(import_handle);
    LOGASSERT(handle_closed);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Vaapi_Platform_ImportSurface(VADisplay    dpy,
                                      uint32_t     format,
                                      uint32_t     pixelFormat,
                                      uint32_t     width,
                                      uint32_t     height,
                                      void*        sharedImageResource,
                                      void**       sharedHandle,
                                      const void*  drmPrimeDescriptor,
                                      VASurfaceID* surfaceId)
{
    (void)drmPrimeDescriptor;

    LOGASSERT(NULL != surfaceId);
    (*surfaceId) = VA_INVALID_SURFACE;

    if (NULL != sharedImageResource)
    {
        LOGASSERT(NULL != dpy);
        LOGASSERT(NULL != surfaceId);

        ImportSurfaceD3D12ResourceWin32(dpy, format, pixelFormat, width, height, (ID3D12Resource*)sharedImageResource, surfaceId);
    }
    else
    {
        LOGASSERT(NULL != dpy);
        LOGASSERT(NULL != surfaceId);
        LOGASSERT(NULL != sharedHandle);
        LOGASSERT(NULL != (*sharedHandle));

        ImportSurfaceWin32(dpy, format, pixelFormat, width, height, (HANDLE)(*sharedHandle), surfaceId);
    }
}
