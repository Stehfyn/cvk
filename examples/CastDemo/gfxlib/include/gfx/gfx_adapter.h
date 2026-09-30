#ifndef VULKAN_VA_ZERO_COPY_GFX_ADAPTER_H
#define VULKAN_VA_ZERO_COPY_GFX_ADAPTER_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <stddef.h>
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Identifies a physical graphics adapter across the APIs that expose it.
 */
typedef struct Gfx_Adapter_t Gfx_Adapter_t;

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
 * \brief Enumerates usable graphics adapters in platform preference order.
 *
 * \param[out] adapters          Array receiving up to adapterCapacity persistent adapter objects
 * \param[in]  adapterCapacity   Number of pointer slots available in adapters; must be nonzero
 * \param[out] adapterCount      Number of adapter pointers written
 *
 * \retval    None No value is returned.
 */
void Gfx_Adapter_Enumerate(Gfx_Adapter_t** adapters, size_t adapterCapacity, size_t* adapterCount);

/*************************************************************************************************/
/**
 * \brief Returns the adapter label.
 *
 * \param[in] adapter            Non-NULL adapter returned by Gfx_Adapter_Enumerate()
 *
 * \return DXGI description on Windows or DRM render-node path on Linux; valid for the adapter's process lifetime.
 */
const char* Gfx_Adapter_GetName(const Gfx_Adapter_t* adapter);

/*************************************************************************************************/
/**
 * \brief Returns the adapter identifier used to correlate Vulkan and VA-API devices.
 *
 * \param[in] adapter            Non-NULL adapter returned by Gfx_Adapter_Enumerate()
 *
 * \return Packed PCI identity on Linux or the adapter LUID bits on Windows.
 */
uint64_t Gfx_Adapter_GetId(const Gfx_Adapter_t* adapter);

#endif /* VULKAN_VA_ZERO_COPY_GFX_ADAPTER_H */
