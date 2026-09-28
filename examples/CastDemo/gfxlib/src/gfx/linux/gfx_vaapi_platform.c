/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#define _POSIX_C_SOURCE (200809L)

#include "gfx/gfx_vaapi_platform.h"
#include "gfx/gfx_vaapi.h"
#include "utl/utl_defines.h"
#include "os/os_assert.h"

#include <fcntl.h>
#include <glob.h>
#include <string.h>
#include <unistd.h>
#include <va/va_drm.h>
#include <va/va_drmcommon.h>
#include <xf86drm.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define PCI_DOMAIN_SHIFT (24U) ///< Bit offset of the PCI domain in a packed adapter identifier.
#define PCI_BUS_SHIFT    (16U) ///< Bit offset of the PCI bus in a packed adapter identifier.
#define PCI_DEVICE_SHIFT (8U)  ///< Bit offset of the PCI device in a packed adapter identifier.

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
 * \brief Creates a persistent VA display connection for a DRM render-node file descriptor.
 *
 * \param[in] fd Open DRM device descriptor retained by the caller
 *
 * \return Non-NULL VA display suitable for Gfx_Vaapi_Initialize().
 */
static VADisplay GetDisplayDrm(int fd);

/*************************************************************************************************/
/**
 * \brief Imports a DRM PRIME 2 descriptor as a VA encoder surface.
 *
 * \param[in]  dpy         Initialized VA display
 * \param[in]  format      VA_RT_FORMAT_* render-target format
 * \param[in]  pixelFormat VA four-character pixel format
 * \param[in]  width       Nonzero surface width in pixels
 * \param[in]  height      Nonzero surface height in pixels
 * \param[in]  descriptor  DRM object and plane layout to import
 * \param[out] surfaceId   Initially invalid storage receiving the imported surface
 *
 * \retval    None
 */
static void ImportSurfaceDrm(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, const VADRMPRIMESurfaceDescriptor* descriptor, VASurfaceID* surfaceId);

/*************************************************************************************************/
/**
 * \brief Exports a VA surface as a writable composed DRM PRIME 2 descriptor.
 *
 * \param[in]  dpy        Initialized VA display
 * \param[in]  surfaceId  Valid surface to export
 * \param[out] descriptor Cleared storage receiving the DRM object and plane layout
 *
 * \retval    None
 */
static void ExportSurfaceDrm(VADisplay dpy, VASurfaceID surfaceId, VADRMPRIMESurfaceDescriptor* descriptor);

/*************************************************************************************************/
/**
 * \brief Creates an export-capable VA surface and exports its composed DRM PRIME 2 layout.
 *
 * \param[in]  dpy         Initialized VA display
 * \param[in]  format      VA_RT_FORMAT_* render-target format
 * \param[in]  pixelFormat VA four-character pixel format
 * \param[in]  width       Nonzero surface width in pixels
 * \param[in]  height      Nonzero surface height in pixels
 * \param[out] surfaceId   Initially invalid storage receiving the created surface
 * \param[out] descriptor  Cleared storage receiving the DRM object and plane layout
 *
 * \retval    None
 */
static void CreateExportedSurfaceDrm(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, VASurfaceID* surfaceId, VADRMPRIMESurfaceDescriptor* descriptor);

/*************************************************************************************************/
/**
 * \brief Packs a Linux DRM PCI address into the repository's stable adapter identifier.
 *
 * \param[in] domain          PCI domain
 * \param[in] bus             PCI bus
 * \param[in] device          PCI device
 * \param[in] function        PCI function
 *
 * \return domain:bus:device:function packed as 40:8:8:8 bits from most to least significant.
 */
static uint64_t PackPciAddress(uint32_t domain, uint32_t bus, uint32_t device, uint32_t function);

/*************************************************************************************************/
/**
 * \brief Opens the DRM render node matching a packed adapter identifier.
 *
 * \param[in] adapterId        Packed PCI adapter identity
 *
 * \return Open DRM render-node file descriptor retained by the VA display.
 */
static int32_t OpenRenderNodeDrmFd(uint64_t adapterId);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static VADisplay GetDisplayDrm(int fd)
{
    VADisplay dpy;

    LOGASSERT(0 <= fd);

    dpy = vaGetDisplayDRM(fd);
    LOGASSERT(NULL != dpy);

    return dpy;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ImportSurfaceDrm(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, const VADRMPRIMESurfaceDescriptor* descriptor, VASurfaceID* surfaceId)
{
    VASurfaceAttrib attrib_list[4];

    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != descriptor);
    LOGASSERT(NULL != surfaceId);
    LOGASSERT(VA_INVALID_SURFACE == (*surfaceId));

    (void)memset(attrib_list, 0, sizeof(attrib_list));
    attrib_list[0].type          = VASurfaceAttribMemoryType;
    attrib_list[0].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[0].value.type    = VAGenericValueTypeInteger;
    attrib_list[0].value.value.i = VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2;
    attrib_list[1].type          = VASurfaceAttribExternalBufferDescriptor;
    attrib_list[1].flags         = VA_SURFACE_ATTRIB_SETTABLE;
    attrib_list[1].value.type    = VAGenericValueTypePointer;
    attrib_list[1].value.value.p = (void*)descriptor;
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
static void ExportSurfaceDrm(VADisplay dpy, VASurfaceID surfaceId, VADRMPRIMESurfaceDescriptor* descriptor)
{
    LOGASSERT(NULL != dpy);
    LOGASSERT(VA_INVALID_SURFACE != surfaceId);
    LOGASSERT(NULL != descriptor);

    (void)memset(descriptor, 0, sizeof((*descriptor)));
    Gfx_Vaapi_ExportSurfaceHandle(dpy, surfaceId, VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2, VA_EXPORT_SURFACE_WRITE_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS, descriptor);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateExportedSurfaceDrm(VADisplay dpy, uint32_t format, uint32_t pixelFormat, uint32_t width, uint32_t height, VASurfaceID* surfaceId, VADRMPRIMESurfaceDescriptor* descriptor)
{
    LOGASSERT(NULL != descriptor);

    Gfx_Vaapi_CreateExportableSurface(dpy, format, pixelFormat, width, height, surfaceId, NULL, 0);
    ExportSurfaceDrm(dpy, (*surfaceId), descriptor);

    LOGASSERT(pixelFormat == descriptor->fourcc);
    LOGASSERT(width == descriptor->width && height == descriptor->height);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static uint64_t PackPciAddress(uint32_t domain, uint32_t bus, uint32_t device, uint32_t function)
{
    LOGASSERT(UINT8_MAX >= bus);
    LOGASSERT(UINT8_MAX >= device);
    LOGASSERT(UINT8_MAX >= function);

    return ((uint64_t)domain << PCI_DOMAIN_SHIFT) | ((uint64_t)bus << PCI_BUS_SHIFT) | ((uint64_t)device << PCI_DEVICE_SHIFT) | function;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Enumerates DRM render nodes and retains the descriptor whose PCI identity matches adapterId.
 */
static int32_t OpenRenderNodeDrmFd(uint64_t adapterId)
{
    int32_t drm_fd;
    glob_t  render_nodes;
    size_t  node_index;
    int32_t glob_result;

    drm_fd = -1;
    (void)memset(&render_nodes, 0, sizeof(render_nodes));
    glob_result = glob("/dev/dri/renderD*", GLOB_NOSORT, NULL, &render_nodes);
    LOGASSERT(0 == glob_result);

    for (node_index = 0; node_index < render_nodes.gl_pathc; ++node_index)
    {
        drmDevicePtr device;
        uint64_t     device_id;
        int32_t      drm_result;

        drm_fd = open(render_nodes.gl_pathv[node_index], O_RDWR | O_CLOEXEC);
        if (0 <= drm_fd)
        {
            device     = NULL;
            drm_result = drmGetDevice2(drm_fd, 0, &device);
            if ((0 == drm_result) && (NULL != device) && (DRM_BUS_PCI == device->bustype))
            {
                device_id = PackPciAddress(device->businfo.pci->domain, device->businfo.pci->bus, device->businfo.pci->dev, device->businfo.pci->func);
                if (adapterId == device_id)
                {
                    drmFreeDevice(&device);
                    break;
                }
            }

            drmFreeDevice(&device);
            (void)close(drm_fd);
            drm_fd = -1;
        }
    }
    globfree(&render_nodes);
    LOGASSERT(0 <= drm_fd);

    return drm_fd;
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
    int32_t drm_fd;

    drm_fd = OpenRenderNodeDrmFd(adapterId);

    return GetDisplayDrm(drm_fd);
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
    (void)sharedHandle;
    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != drmPrimeDescriptor);
    LOGASSERT(NULL != surfaceId);
    (*surfaceId) = VA_INVALID_SURFACE;
    CreateExportedSurfaceDrm(dpy, format, pixelFormat, width, height, surfaceId, (VADRMPRIMESurfaceDescriptor*)drmPrimeDescriptor);
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
    (void)sharedImageResource;
    (void)sharedHandle;
    LOGASSERT(NULL != dpy);
    LOGASSERT(NULL != drmPrimeDescriptor);
    LOGASSERT(NULL != surfaceId);
    (*surfaceId) = VA_INVALID_SURFACE;
    ImportSurfaceDrm(dpy, format, pixelFormat, width, height, (const VADRMPRIMESurfaceDescriptor*)drmPrimeDescriptor, surfaceId);
}
