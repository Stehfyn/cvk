#ifndef VULKAN_VA_ZERO_COPY_GFX_SWAPCHAIN_H
#define VULKAN_VA_ZERO_COPY_GFX_SWAPCHAIN_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_image.h"
#include "gfx/gfx_surface.h"

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
 * \brief Selects the operation performed when an acquired image is submitted.
 */
typedef enum Gfx_Swapchain_Kind_e
{
    GFX_SWAPCHAIN_KIND_CONVERT, ///< Compute-convert swapchain images into the storage surface pixel format.
} Gfx_Swapchain_Kind_t;

/*************************************************************************************************/
/**
 * \brief Selects the native swapchain backend.
 */
typedef enum Gfx_Swapchain_Hint_e
{
    GFX_SWAPCHAIN_HINT_VULKAN, ///< Vulkan backend.
} Gfx_Swapchain_Hint_t;

/*************************************************************************************************/
/**
 * \brief Specifies a swapchain's operation, backend, backing images, and target surface.
 */
typedef struct Gfx_Swapchain_CreateInfo_s
{
    Gfx_Swapchain_Kind_t kind;        ///< Operation performed on each submitted image.
    Gfx_Swapchain_Hint_t hint;        ///< Backend used to process image submissions.
    Gfx_Image_Format_t   imageFormat; ///< Pixel layout of each backing image.
    uint32_t             width;       ///< Width of each swapchain-owned backing image.
    uint32_t             height;      ///< Height of each swapchain-owned backing image.
    uint32_t       imageCount; ///< Number of swapchain-owned backing images, from 1 through CVK_MAX_SWAPCHAIN_IMAGES.
    Gfx_Surface_t* surface;    ///< Target surface; must outlive the swapchain.
} Gfx_Swapchain_CreateInfo_t;

/*************************************************************************************************/
/**
 * \brief Owns a ring of backing images acquired by the caller and submitted to a target surface.
 */
typedef struct Gfx_Swapchain_s Gfx_Swapchain_t;

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
 * \brief Creates the backing-image ring and the resources needed to submit them.
 *
 * \param[out] swapchain       Receives a non-NULL swapchain retained for process lifetime
 * \param[in]  createInfo      Non-NULL configuration with a compatible target surface
 *
 * \retval    None
 */
void Gfx_Swapchain_Create(Gfx_Swapchain_t** swapchain, const Gfx_Swapchain_CreateInfo_t* createInfo);

/*************************************************************************************************/
/**
 * \brief Acquires the next available logical image in round-robin order.
 *
 * \param[in,out] swapchain      Non-NULL swapchain with no outstanding acquired image
 * \param[in]     timeout        Vulkan slot-completion timeout in nanoseconds
 * \param[out]    image          Receives the acquired backing image owned by swapchain
 *
 * \retval    None
 */
void Gfx_Swapchain_AcquireNextImage(Gfx_Swapchain_t* swapchain, uint64_t timeout, Gfx_Image_t** image);

/*************************************************************************************************/
/**
 * \brief Submits an acquired image for the swapchain's operation and retains it until the work completes.
 *
 * \param[in,out] swapchain      Non-NULL swapchain from which image was acquired
 * \param[in,out] image          Acquired image with recorded or submitted pixel writes
 *
 * \retval    None
 */
void Gfx_Swapchain_SubmitImage(Gfx_Swapchain_t* swapchain, Gfx_Image_t* image);

#endif /* VULKAN_VA_ZERO_COPY_GFX_SWAPCHAIN_H */
