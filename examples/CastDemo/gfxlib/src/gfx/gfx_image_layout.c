/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Format storage and subsampling definitions: pinned libva va.h VA_FOURCC declarations. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_image_layout.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_types.h"

#include <stddef.h>
#include <stdint.h>
#include <va/va.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define BYTE_BIT_COUNT          (8) ///< Bits in a native image byte.
#define HIGH_DEPTH_SAMPLE_BYTES (2) ///< Storage bytes per 10-, 12- or 16-bit channel sample.
#define MAX_IMAGE_PLANES        (3) ///< Maximum plane count in the retained gfxlib format set.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Storage factors for one supported multi-plane native format. */
typedef struct ImageFormat_s
{
    uint32_t planeCount;      ///< Required native plane count.
    uint32_t sampleByteCount; ///< Bytes per full-resolution channel sample.
    uint32_t widthMultiple;   ///< Required divisibility of image width.
    uint32_t heightMultiple;  ///< Required divisibility of image height.
    uint32_t horizontalScale; ///< Full-resolution row bytes per chroma row byte.
    uint32_t verticalScale;   ///< Full-resolution rows per chroma row.
} ImageFormat_t;

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
 * \brief Derives the plane and subsampling factors for a supported multi-plane format.
 *
 * \par Requirements:
 *  - 001 Format shall belong to the retained multi-plane format set; misuse shall assert.
 *  - 002 Every returned factor shall be positive and define exact integer row geometry.
 *
 * \param[in] fourcc   Native multi-plane format code
 *
 * \retval ImageFormat_t   Complete storage factors for the selected format.
 */
static ImageFormat_t GetFormatGeometry(uint32_t fourcc);

/*************************************************************************************************/
/**
 * \brief Initializes active row geometry from native pixel format metadata.
 *
 * \par Requirements:
 *  - 001 Image shall have positive dimensions and one to three planes.
 *  - 002 Layout shall be zero initialized before this operation.
 *  - 003 Unsupported formats, plane counts or subsampling dimensions shall assert.
 *  - 004 Plane ordering shall match the native descriptor without color conversion.
 *
 * \param[out] layout  Unpublished layout receiving active row sizes and counts
 * \param[in]  image   Native format and dimensions
 *
 * \retval None
 */
static void InitializeGeometry(Gfx_Image_Layout_t* layout, const VAImage* image);

/*************************************************************************************************/
/**
 * \brief Completes one plane's packed offset after checking its native row extent.
 *
 * \par Requirements:
 *  - 001 Plane shall have positive active row geometry and populated native pitch and offset.
 *  - 002 Layout shall hold the native extent and the packed byte count of preceding planes.
 *  - 003 Native and packed end offsets shall be checked with widened arithmetic before narrowing.
 *
 * \param[in,out] plane   Plane receiving its validated packed offset
 * \param[in,out] layout  Unpublished layout accumulating the packed extent
 *
 * \retval None
 */
static void InitializePlane(Gfx_Image_Plane_t* plane, Gfx_Image_Layout_t* layout);

/*************************************************************************************************/
/**
 * \brief Copies one validated plane in the selected packing direction.
 *
 * \par Requirements:
 *  - 001 Plane shall be initialized; input and output shall satisfy the whole-layout contracts.
 *  - 002 Contiguous rows shall be copied together; pitched rows shall exclude native padding.
 *  - 003 Every calculated offset shall designate an active row in the validated extent.
 *
 * \param[in]  plane         Immutable plane geometry
 * \param[out] output        Base of destination storage
 * \param[in]  input         Base of disjoint source storage
 * \param[in]  packedOutput  TRUE to pack native rows, FALSE to unpack host rows
 *
 * \retval None
 */
static void CopyPlane(const Gfx_Image_Plane_t* plane, byte_t* output, const byte_t* input, bool_t packedOutput);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Defaults describe three full-resolution 8-bit planes. Each format changes only
 *  the factors that differ. Semiplanar formats preserve interleaved chroma row widths.
 */
static ImageFormat_t GetFormatGeometry(uint32_t fourcc)
{
    ImageFormat_t format;

    format.planeCount      = 3;
    format.sampleByteCount = 1;
    format.widthMultiple   = 1;
    format.heightMultiple  = 1;
    format.horizontalScale = 1;
    format.verticalScale   = 1;
    switch (fourcc)
    {
    case VA_FOURCC_NV12:
    case VA_FOURCC_NV21:
        format.planeCount     = 2;
        format.widthMultiple  = 2;
        format.heightMultiple = 2;
        format.verticalScale  = 2;
        break;
    case VA_FOURCC_P010:
    case VA_FOURCC_P012:
    case VA_FOURCC_P016:
        format.planeCount      = 2;
        format.sampleByteCount = HIGH_DEPTH_SAMPLE_BYTES;
        format.widthMultiple   = 2;
        format.heightMultiple  = 2;
        format.verticalScale   = 2;
        break;
    case VA_FOURCC_P208:
        format.planeCount    = 2;
        format.widthMultiple = 2;
        break;
    case VA_FOURCC_NV11:
        format.planeCount      = 2;
        format.widthMultiple   = 4;
        format.horizontalScale = 2;
        break;
    case VA_FOURCC_I420:
    case VA_FOURCC_IYUV:
    case VA_FOURCC_YV12:
    case VA_FOURCC_IMC3:
        format.widthMultiple   = 2;
        format.heightMultiple  = 2;
        format.horizontalScale = 2;
        format.verticalScale   = 2;
        break;
    case VA_FOURCC_I010:
        format.sampleByteCount = HIGH_DEPTH_SAMPLE_BYTES;
        format.widthMultiple   = 2;
        format.heightMultiple  = 2;
        format.horizontalScale = 2;
        format.verticalScale   = 2;
        break;
    case VA_FOURCC_YV16:
    case VA_FOURCC_422H:
        format.widthMultiple   = 2;
        format.horizontalScale = 2;
        break;
    case VA_FOURCC_422V:
        format.heightMultiple = 2;
        format.verticalScale  = 2;
        break;
    case VA_FOURCC_411P:
        format.widthMultiple   = 4;
        format.horizontalScale = 4;
        break;
    case VA_FOURCC_411R:
        format.heightMultiple = 4;
        format.verticalScale  = 4;
        break;
    case VA_FOURCC_YV24:
    case VA_FOURCC_444P:
    case VA_FOURCC_RGBP:
    case VA_FOURCC_BGRP:
        break;
    case VA_FOURCC_Q416:
        format.sampleByteCount = HIGH_DEPTH_SAMPLE_BYTES;
        break;
    default:
        OS_AssertFailure("supported VA image format", __FILE__, __LINE__);
    }
    return format;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Single-plane rows use the declared bits per pixel, rounded to a complete byte.
 *  Multi-plane formats derive active rows independently of provider-selected pitch.
 */
static void InitializeGeometry(Gfx_Image_Layout_t* layout, const VAImage* image)
{
    uint32_t      width;
    uint32_t      height;
    uint64_t      row_bits;
    uint64_t      row_bytes;
    ImageFormat_t format;

    width  = image->width;
    height = image->height;
    if (1 == image->num_planes)
    {
        LOGASSERT(0 < image->format.bits_per_pixel);
        row_bits  = (uint64_t)width * image->format.bits_per_pixel;
        row_bytes = (row_bits + BYTE_BIT_COUNT - 1) / BYTE_BIT_COUNT;
        LOGASSERT(row_bytes <= UINT32_MAX);
        layout->firstPlane.rowByteCount = (uint32_t)row_bytes;
        layout->firstPlane.rowCount     = height;
    }
    else
    {
        format = GetFormatGeometry(image->format.fourcc);
        LOGASSERT(format.planeCount == image->num_planes);
        LOGASSERT(0 == width % format.widthMultiple);
        LOGASSERT(0 == height % format.heightMultiple);
        layout->firstPlane.rowByteCount  = width * format.sampleByteCount;
        layout->secondPlane.rowByteCount = layout->firstPlane.rowByteCount / format.horizontalScale;
        layout->firstPlane.rowCount      = height;
        layout->secondPlane.rowCount     = height / format.verticalScale;
        if (3 == image->num_planes)
        {
            layout->thirdPlane.rowByteCount = layout->secondPlane.rowByteCount;
            layout->thirdPlane.rowCount     = layout->secondPlane.rowCount;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Bounding the last active byte permits native row and plane padding without copying it.
 *  Row counts originate in 16-bit native dimensions; products are evaluated in uint64_t.
 */
static void InitializePlane(Gfx_Image_Plane_t* plane, Gfx_Image_Layout_t* layout)
{
    uint64_t native_end;
    uint64_t plane_bytes;
    uint64_t packed_end;

    LOGASSERT(0 < plane->rowCount);
    LOGASSERT(0 < plane->rowByteCount);
    LOGASSERT(plane->rowByteCount <= plane->nativePitch);

    native_end =
        (uint64_t)plane->nativeOffset + ((uint64_t)(plane->rowCount - 1) * plane->nativePitch) + plane->rowByteCount;
    plane_bytes = (uint64_t)plane->rowByteCount * plane->rowCount;
    packed_end  = (uint64_t)layout->packedByteCount + plane_bytes;
    LOGASSERT(native_end <= layout->nativeByteCount);
    LOGASSERT(packed_end <= UINT32_MAX);

    plane->packedOffset     = layout->packedByteCount;
    layout->packedByteCount = (uint32_t)packed_end;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Selects pitches once. The row loop forms pointers only for existing active rows;
 *  it never advances a mapped pointer beyond the final row into inaccessible trailing padding.
 */
static void CopyPlane(const Gfx_Image_Plane_t* plane, byte_t* output, const byte_t* input, bool_t packedOutput)
{
    uint32_t output_offset;
    uint32_t input_offset;
    uint32_t output_pitch;
    uint32_t input_pitch;
    bool_t   is_contiguous;
    uint64_t plane_bytes;
    uint32_t row_index;
    uint64_t output_row;
    uint64_t input_row;

    output_offset = packedOutput ? plane->packedOffset : plane->nativeOffset;
    input_offset  = packedOutput ? plane->nativeOffset : plane->packedOffset;
    output_pitch  = packedOutput ? plane->rowByteCount : plane->nativePitch;
    input_pitch   = packedOutput ? plane->nativePitch : plane->rowByteCount;
    is_contiguous = (plane->rowByteCount == plane->nativePitch);
    if (is_contiguous)
    {
        plane_bytes = (uint64_t)plane->rowByteCount * plane->rowCount;
        CRT_Memory_Copy(output + output_offset, input + input_offset, plane_bytes);
    }
    else
    {
        for (row_index = 0; row_index < plane->rowCount; ++row_index)
        {
            output_row = (uint64_t)output_offset + ((uint64_t)row_index * output_pitch);
            input_row  = (uint64_t)input_offset + ((uint64_t)row_index * input_pitch);
            CRT_Memory_Copy(output + output_row, input + input_row, plane->rowByteCount);
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Build a candidate so assertion failure cannot publish partial geometry. Only active
 *  native plane fields are read; unused fields in driver output may be unspecified.
 */
void Gfx_Image_InitializeLayout(Gfx_Image_Layout_t* layout, const VAImage* image)
{
    Gfx_Image_Layout_t candidate;

    LOGASSERT(NULL != layout);
    LOGASSERT(NULL != image);
    LOGASSERT(0 < image->width);
    LOGASSERT(0 < image->height);
    LOGASSERT(0 < image->num_planes);
    LOGASSERT(image->num_planes <= MAX_IMAGE_PLANES);

    CRT_Memory_Set(&candidate, 0, sizeof(candidate));
    candidate.planeCount      = image->num_planes;
    candidate.nativeByteCount = image->data_size;
    InitializeGeometry(&candidate, image);

    candidate.firstPlane.nativeOffset = image->offsets[0];
    candidate.firstPlane.nativePitch  = image->pitches[0];
    InitializePlane(&candidate.firstPlane, &candidate);
    if (1 < candidate.planeCount)
    {
        candidate.secondPlane.nativeOffset = image->offsets[1];
        candidate.secondPlane.nativePitch  = image->pitches[1];
        InitializePlane(&candidate.secondPlane, &candidate);
    }
    if (2 < candidate.planeCount)
    {
        candidate.thirdPlane.nativeOffset = image->offsets[2];
        candidate.thirdPlane.nativePitch  = image->pitches[2];
        InitializePlane(&candidate.thirdPlane, &candidate);
    }
    (*layout) = candidate;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Capacity is checked before the first copy. No native format derivation occurs during transfer.
 */
void Gfx_Image_ReadLayout(const Gfx_Image_Layout_t* layout, void* output, uint64_t capacity, const void* input)
{
    byte_t*       output_bytes;
    const byte_t* input_bytes;

    LOGASSERT(NULL != layout);
    LOGASSERT(NULL != output);
    LOGASSERT(NULL != input);
    LOGASSERT(0 < layout->packedByteCount);
    LOGASSERT(layout->packedByteCount <= capacity);

    output_bytes = (byte_t*)output;
    input_bytes  = (const byte_t*)input;
    CopyPlane(&layout->firstPlane, output_bytes, input_bytes, TRUE);
    if (1 < layout->planeCount)
    {
        CopyPlane(&layout->secondPlane, output_bytes, input_bytes, TRUE);
    }
    if (2 < layout->planeCount)
    {
        CopyPlane(&layout->thirdPlane, output_bytes, input_bytes, TRUE);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The same retained geometry defines packed input and active native output; padding is untouched.
 */
void Gfx_Image_WriteLayout(const Gfx_Image_Layout_t* layout, const void* input, uint64_t capacity, void* output)
{
    byte_t*       output_bytes;
    const byte_t* input_bytes;

    LOGASSERT(NULL != layout);
    LOGASSERT(NULL != output);
    LOGASSERT(NULL != input);
    LOGASSERT(0 < layout->packedByteCount);
    LOGASSERT(layout->packedByteCount <= capacity);

    output_bytes = (byte_t*)output;
    input_bytes  = (const byte_t*)input;
    CopyPlane(&layout->firstPlane, output_bytes, input_bytes, FALSE);
    if (1 < layout->planeCount)
    {
        CopyPlane(&layout->secondPlane, output_bytes, input_bytes, FALSE);
    }
    if (2 < layout->planeCount)
    {
        CopyPlane(&layout->thirdPlane, output_bytes, input_bytes, FALSE);
    }
}
