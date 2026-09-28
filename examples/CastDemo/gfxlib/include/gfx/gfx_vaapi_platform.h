#ifndef VULKAN_VA_ZERO_COPY_GFX_VAAPI_PLATFORM_H
#define VULKAN_VA_ZERO_COPY_GFX_VAAPI_PLATFORM_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <stdint.h>
#include <va/va.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

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
 * \brief Gets a VA display for the selected physical adapter.
 *
 * \param[in] adapterId        Platform adapter identity selecting the VA display
 *
 * \return Adapter-matched VA display; initialize it with Gfx_Vaapi_Initialize().
 */
VADisplay Gfx_Vaapi_Platform_GetDisplay(uint64_t adapterId);

/*************************************************************************************************/
/**
 * \brief Creates an exported VA surface from the platform-native sharing object.
 *
 * \param[in]  dpy                  Initialized VA display
 * \param[in]  format               VA render-target format
 * \param[in]  pixelFormat          VA pixel fourcc
 * \param[in]  width                Surface width in pixels
 * \param[in]  height               Surface height in pixels
 * \param[out] sharedHandle        Windows shared-handle storage, or NULL
 * \param[out] drmPrimeDescriptor  Linux DMA-BUF descriptor storage, or NULL
 * \param[out] surfaceId           Receives the created VA surface
 *
 * \retval None
 */
void Gfx_Vaapi_Platform_CreateExportedSurface(VADisplay    dpy,
                                              uint32_t     format,
                                              uint32_t     pixelFormat,
                                              uint32_t     width,
                                              uint32_t     height,
                                              void**       sharedHandle,
                                              void*        drmPrimeDescriptor,
                                              VASurfaceID* surfaceId);

/*************************************************************************************************/
/**
 * \brief Imports a platform-native sharing object into a VA surface.
 *
 * \param[in]  dpy                  Initialized VA display
 * \param[in]  format               VA render-target format
 * \param[in]  pixelFormat          VA pixel fourcc
 * \param[in]  width                Surface width in pixels
 * \param[in]  height               Surface height in pixels
 * \param[in]  sharedImageResource Windows D3D12 resource, or NULL
 * \param[in]  sharedHandle        Windows shared-handle storage, or NULL
 * \param[in]  drmPrimeDescriptor  Linux DMA-BUF descriptor, or NULL
 * \param[out] surfaceId           Receives the imported VA surface
 *
 * \retval None
 */
void Gfx_Vaapi_Platform_ImportSurface(VADisplay    dpy,
                                      uint32_t     format,
                                      uint32_t     pixelFormat,
                                      uint32_t     width,
                                      uint32_t     height,
                                      void*        sharedImageResource,
                                      void**       sharedHandle,
                                      const void*  drmPrimeDescriptor,
                                      VASurfaceID* surfaceId);

#endif /* VULKAN_VA_ZERO_COPY_GFX_VAAPI_PLATFORM_H */
