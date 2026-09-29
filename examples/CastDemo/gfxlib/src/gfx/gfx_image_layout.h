#ifndef GFX_IMAGE_LAYOUT_H
#define GFX_IMAGE_LAYOUT_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Private image packing contract. Native formats and plane ordering follow libva va.h. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"

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

/*************************************************************************************************/
/** \brief Validated active rows in one native plane and its packed counterpart. */
typedef struct Gfx_Image_Plane_s
{
    uint32_t nativeOffset; ///< First active byte in mapped native storage.
    uint32_t nativePitch;  ///< Distance between native row starts, including padding.
    uint32_t packedOffset; ///< First active byte in packed host storage.
    uint32_t rowByteCount; ///< Active bytes per row; no native padding.
    uint32_t rowCount;     ///< Positive number of rows for an active plane.
} Gfx_Image_Plane_t;

/*************************************************************************************************/
/**
 * \brief Immutable transfer geometry retained by a storage image owner.
 *
 * \par Details:
 *  Plane names preserve native ordering, including planar RGB and reversed chroma formats.
 *  Unused planes are zero. Consumers shall not modify the initialized layout.
 */
typedef struct Gfx_Image_Layout_s
{
    Gfx_Image_Plane_t firstPlane;      ///< Packed channels or first native plane.
    Gfx_Image_Plane_t secondPlane;     ///< Second native plane when present.
    Gfx_Image_Plane_t thirdPlane;      ///< Third native plane when present.
    uint32_t          planeCount;      ///< One, two or three active planes.
    uint32_t          packedByteCount; ///< Combined active bytes, excluding all padding.
    uint32_t          nativeByteCount; ///< Complete mapped native extent.
} Gfx_Image_Layout_t;

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
 * \brief Initializes retained geometry for a supported derived VA image.
 *
 * \par Requirements:
 *  - 001 Both pointers shall be non-NULL; initialization shall occur during image construction.
 *  - 002 Dimensions shall be positive and satisfy the format's chroma subsampling divisibility.
 *  - 003 The native descriptor shall contain the format's one, two or three expected planes.
 *  - 004 Each pitch shall cover its active row and every active row shall fit data_size.
 *  - 005 The combined packed extent shall fit uint32_t; arithmetic shall widen before multiplication.
 *  - 006 Invalid construction inputs shall assert before publishing any output layout.
 *  - 007 Initialization shall allocate nothing and shall not call a native provider.
 *
 * \par Details:
 *  Supports the imported gfxlib format set. This initializer requires a valid native descriptor;
 *  it is not a recovery boundary for malformed driver output or unsupported image formats.
 *
 * \param[out] layout  Owner's retained layout, published only after all extent checks
 * \param[in]  image   Supported native descriptor whose storage and geometry remain unchanged
 *
 * \retval None   No result value.
 */
void Gfx_Image_InitializeLayout(Gfx_Image_Layout_t* layout, const VAImage* image);

/*************************************************************************************************/
/**
 * \brief Reads active native rows into packed host storage.
 *
 * \par Requirements:
 *  - 001 Layout shall be initialized and immutable; all pointers shall be non-NULL.
 *  - 002 Input shall cover nativeByteCount bytes and shall not overlap output.
 *  - 003 Output capacity shall cover packedByteCount; misuse shall assert before any output write.
 *  - 004 Only active bytes shall be copied, in native plane order; trailing output shall be preserved.
 *  - 005 Work shall be bounded by the retained row counts with no allocation or native calls.
 *
 * \param[in]  layout    Initialized transfer geometry
 * \param[out] output    Caller-owned packed destination
 * \param[in]  capacity  Writable output extent in bytes
 * \param[in]  input     Synchronized mapped native storage
 *
 * \retval None   No result value.
 */
void Gfx_Image_ReadLayout(const Gfx_Image_Layout_t* layout, void* output, uint64_t capacity, const void* input);

/*************************************************************************************************/
/**
 * \brief Writes packed host rows into native image storage.
 *
 * \par Requirements:
 *  - 001 Layout shall be initialized and immutable; all pointers shall be non-NULL.
 *  - 002 Output shall cover nativeByteCount bytes and shall not overlap input.
 *  - 003 Input capacity shall cover packedByteCount; misuse shall assert before any output write.
 *  - 004 Native padding and bytes outside active rows shall remain unchanged.
 *  - 005 Work shall be bounded by the retained row counts with no allocation or native calls.
 *
 * \param[in]  layout    Initialized transfer geometry
 * \param[in]  input     Caller-owned packed source
 * \param[in]  capacity  Readable input extent in bytes
 * \param[out] output    Synchronized mapped native storage
 *
 * \retval None   No result value.
 */
void Gfx_Image_WriteLayout(const Gfx_Image_Layout_t* layout, const void* input, uint64_t capacity, void* output);

#endif /* GFX_IMAGE_LAYOUT_H */
