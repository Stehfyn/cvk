/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#define _POSIX_C_SOURCE (200809L) ///< Exposes POSIX.1-2008 interfaces used by this translation unit.

#include "gfx/gfx_adapter.h"

#include "os/os_assert.h"
#include "os/os_malloc.h"
#include "utl/utl_defines.h"

#include <fcntl.h>
#include <glob.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define ADAPTER_NAME_CAPACITY (256u) ///< UTF-8 adapter-name capacity including the terminator.
#define PCI_DOMAIN_SHIFT      (24U)  ///< Bit offset of the PCI domain in a packed adapter identifier.
#define PCI_BUS_SHIFT         (16U)  ///< Bit offset of the PCI bus in a packed adapter identifier.
#define PCI_DEVICE_SHIFT      (8U)   ///< Bit offset of the PCI device in a packed adapter identifier.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Render-node path and device identifier for a physical graphics adapter.
 */
struct Gfx_Adapter_t
{
    char     name[ADAPTER_NAME_CAPACITY]; ///< Null-terminated DRM render-node path.
    uint64_t id;                          ///< Packed PCI domain, bus, device, and function identity.
};

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

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

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

    return ((uint64_t)domain << PCI_DOMAIN_SHIFT) | ((uint64_t)bus << PCI_BUS_SHIFT) |
           ((uint64_t)device << PCI_DEVICE_SHIFT) | function;
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
    glob_t render_nodes;
    size_t node_index;

    LOGASSERT(NULL != adapters);
    LOGASSERT(0 != adapterCapacity);
    LOGASSERT(NULL != adapterCount);

    (*adapterCount) = 0;
    (void)memset(&render_nodes, 0, sizeof(render_nodes));
    if (0 != glob("/dev/dri/renderD*", GLOB_NOSORT, NULL, &render_nodes))
    {
        return;
    }
    for (node_index = 0; node_index < render_nodes.gl_pathc && (*adapterCount) < adapterCapacity; ++node_index)
    {
        Gfx_Adapter_t* adapter;
        drmDevicePtr   device;
        struct stat    status;
        int            fd;
        size_t         path_length;

        if (0 != stat(render_nodes.gl_pathv[node_index], &status) || 0 == S_ISCHR(status.st_mode))
        {
            continue;
        }
        fd = open(render_nodes.gl_pathv[node_index], O_RDWR | O_CLOEXEC);
        if (0 > fd)
        {
            continue;
        }
        device = NULL;
        if (0 != drmGetDevice2(fd, 0, &device) || NULL == device || DRM_BUS_PCI != device->bustype ||
            NULL == device->businfo.pci)
        {
            if (NULL != device)
            {
                drmFreeDevice(&device);
            }
            (void)close(fd);
            continue;
        }
        path_length = strlen(render_nodes.gl_pathv[node_index]);
        LOGASSERT(ADAPTER_NAME_CAPACITY > path_length);

        adapter = (Gfx_Adapter_t*)OS_CALLOC(sizeof((*adapter)));
        (void)memcpy(adapter->name, render_nodes.gl_pathv[node_index], path_length + 1u);
        adapter->id               = PackPciAddress(device->businfo.pci->domain,
                                                   device->businfo.pci->bus,
                                                   device->businfo.pci->dev,
                                                   device->businfo.pci->func);
        adapters[(*adapterCount)] = adapter;
        ++(*adapterCount);
        drmFreeDevice(&device);
        (void)close(fd);
    }
    globfree(&render_nodes);
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
