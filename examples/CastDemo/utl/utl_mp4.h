#ifndef UTL_MP4_H
#define UTL_MP4_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * ISO/IEC 14496-12, ISO base media file format, Box and FullBox syntax.
 * Official ISO BMFF reference software:
 * https://github.com/MPEGGroup/isobmff
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
** 
** 3.1 Macros
**===============================================================================================*/

#define UTL_MP4_BOX_HEADER_BYTES       (8)          ///< Size and type fields.
#define UTL_MP4_LARGE_BOX_HEADER_BYTES (16)         ///< Header including largesize.
#define UTL_MP4_USER_TYPE_BYTES        (16)         ///< UUID bytes after the size/type header.
#define UTL_MP4_MAX_BOX_HEADER_BYTES   (32)         ///< Large UUID box header.
#define UTL_MP4_BOX_UUID               (0x75756964) ///< FourCC "uuid".

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Typed ISO BMFF Box header with no retained buffer or file ownership.
 */
typedef struct Utl_Mp4_BoxHeader_s
{
    uint64_t byteCount;                         ///< Entire box size, including its header.
    uint32_t type;                              ///< FourCC represented in big-endian numeric order.
    uint32_t headerByteCount;                   ///< Number of bytes before the payload.
    byte_t   userType[UTL_MP4_USER_TYPE_BYTES]; ///< UUID for a uuid box, otherwise zero.
    bool_t   extendsToEnd;                      ///< Size field was zero; end supplied by caller.
    bool_t   hasLargeSize;                      ///< Size field was one; largesize is present.
} Utl_Mp4_BoxHeader_t;

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
 * \brief Reads one ISO BMFF box header from a counted byte span.
 *
 * \par Details:
 *  The caller supplies bytes beginning at the size field and the number of bytes
 *  remaining in the enclosing file or container. The payload need not be in buffer.
 *  UUID and 64-bit largesize headers are handled independently.
 *
 * \par Requirements:
 *  - 001 This function shall accept ordinary, large-size, end-of-container and UUID headers.
 *  - 002 This function shall reject truncated headers and sizes outside the enclosing boundary.
 *  - 003 This function shall leave header unchanged on failure.
 *  - 004 This function shall allocate no storage and perform no I/O.
 *
 * \param[out] header      Non-NULL destination for the typed header
 * \param[in]  buffer      Non-NULL bytes beginning at the size field
 * \param[in]  byteCount   Number of readable bytes in buffer
 * \param[in]  remaining   Number of bytes remaining in the enclosing boundary
 *
 * \retval     TRUE    On success.
 * \retval     FALSE   For invalid input, truncation, or an invalid size.
 */
bool_t Utl_Mp4_ReadBoxHeader(Utl_Mp4_BoxHeader_t* header, const byte_t* buffer, uint64_t byteCount, uint64_t remaining);

/*************************************************************************************************/
/**
 * \brief Writes one ISO BMFF box header into caller-owned storage.
 *
 * \par Details:
 *  The size flags select the original header form. headerByteCount is derived
 *  from those flags and the box type. UUID bytes are copied only for a uuid box.
 *  No payload bytes are written.
 *
 * \par Requirements:
 *  - 001 This function shall validate sizes before writing any output byte.
 *  - 002 This function shall return zero without changing buffer on failure.
 *  - 003 This function shall use no CRT, heap allocation, or file operation.
 *
 * \param[in]  header     Non-NULL typed box header
 * \param[out] buffer     Non-NULL output storage
 * \param[in]  capacity   Number of writable bytes
 *
 * \retval     8..UTL_MP4_MAX_BOX_HEADER_BYTES   Number of box-header bytes written.
 * \retval     0                                 The model, output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t Utl_Mp4_WriteBoxHeader(const Utl_Mp4_BoxHeader_t* header, byte_t* buffer, uint64_t capacity);

#endif /* UTL_MP4_H */
