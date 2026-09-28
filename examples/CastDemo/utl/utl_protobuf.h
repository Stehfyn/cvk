#ifndef UTL_PROTOBUF_H
#define UTL_PROTOBUF_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Protocol Buffers encoding guide, wire types and condensed reference card:
 * https://protobuf.dev/programming-guides/encoding/
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

#define UTL_PROTOBUF_MAX_VARINT_BYTES (10)         ///< Maximum unsigned 64-bit varint width.
#define UTL_PROTOBUF_MAX_FIELD_NUMBER (0x1FFFFFFF) ///< Field number occupies 29 tag bits.
#define UTL_PROTOBUF_MAX_GROUP_DEPTH  (64)         ///< Local workspace limit, not a wire-format limit.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Protocol Buffers wire-type values from the encoding guide. */
typedef enum Utl_Protobuf_WireType_e
{
    UTL_PROTOBUF_VARINT      = 0,
    UTL_PROTOBUF_FIXED64     = 1,
    UTL_PROTOBUF_BYTES       = 2,
    UTL_PROTOBUF_GROUP_START = 3,
    UTL_PROTOBUF_GROUP_END   = 4,
    UTL_PROTOBUF_FIXED32     = 5
} Utl_Protobuf_WireType_t;

/*************************************************************************************************/
/**
 * \brief One typed wire record with borrowed length-delimited bytes.
 *
 * \par Details:
 *  The schema assigns meaning to integer bits and counted bytes. Fixed-width
 *  numbers use little-endian wire order; integerValue preserves their bit pattern.
 *  A group marker is one record, not its nested content. SkipField handles groups.
 */
typedef struct Utl_Protobuf_Field_s
{
    const byte_t*           bytes;            ///< Borrowed data for BYTES; otherwise NULL.
    uint64_t                integerValue;     ///< VARINT, FIXED64 or FIXED32 value.
    uint32_t                number;           ///< Field number, one through 2^29 - 1.
    Utl_Protobuf_WireType_t wireType;         ///< Encoding of this record.
    uint32_t                byteCount;        ///< Counted BYTES payload; otherwise zero.
    uint32_t                encodedByteCount; ///< Total record width, including its tag.
} Utl_Protobuf_Field_t;

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
 * \brief Computes one validated record's shortest encoded size.
 *
 * \par Details:
 *  encodedByteCount is ignored; it describes a previous read, not an output request.
 *  Fixed32 values must fit 32 bits; a nonempty BYTES value needs a source pointer.
 *
 * \par Requirements:
 *  - 001 Invalid fields or sizes exceeding uint32_t shall return zero.
 *  - 002 The function shall not read payload bytes or allocate storage.
 *
 * \param[in] field   Non-NULL typed record
 *
 * \retval    1..UINT32_MAX   Required encoded record byte count.
 * \retval    0               The record is invalid or its encoded size exceeds uint32_t.
 */
uint32_t Utl_Protobuf_GetFieldByteCount(const Utl_Protobuf_Field_t* field);

/*************************************************************************************************/
/**
 * \brief Reads one unsigned base-128 varint.
 *
 * \par Details:
 *  Accepts nonminimal encodings permitted by protobuf readers. The tenth byte may
 *  contain only bit zero, preventing overflow and continuation beyond 64 bits.
 *
 * \par Requirements:
 *  - 001 This function shall inspect at most ten bytes of the counted input.
 *  - 002 This function shall reject truncated, overflowing or unterminated integers.
 *  - 003 Failure shall leave both output values unchanged.
 *  - 004 This function shall allocate nothing and call no CRT or OS functions.
 *
 * \param[out] value       Non-NULL decoded value
 * \param[out] bytesRead   Non-NULL number of consumed bytes
 * \param[in]  buffer      Counted readable input, non-NULL
 * \param[in]  byteCount   Readable byte count
 *
 * \retval     TRUE    A complete integer was published.
 * \retval     FALSE   A pointer, encoding or input bound was invalid.
 */
bool_t Utl_Protobuf_ReadVarint(uint64_t* value, uint32_t* bytesRead, const byte_t* buffer, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Writes the shortest unsigned base-128 varint.
 *
 * \par Details:
 *  Capacity is checked before the first output write.
 *
 * \par Requirements:
 *  - 001 Success shall write between one and ten bytes.
 *  - 002 Failure shall leave the complete output buffer unchanged.
 *  - 003 This function shall allocate no storage and perform no I/O.
 *
 * \param[in]  value      Integer to encode
 * \param[out] buffer     Non-NULL writable output
 * \param[in]  capacity   Available output bytes
 *
 * \retval     1..UTL_PROTOBUF_MAX_VARINT_BYTES   Number of encoded varint bytes written.
 * \retval     0                                  The output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t Utl_Protobuf_WriteVarint(uint64_t value, byte_t* buffer, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Reads one wire record into caller-owned typed storage.
 *
 * \par Details:
 *  All six wire types are represented. A BYTES result borrows the source span.
 *  Embedded-message interpretation and signed/float conversion belong to the schema.
 *  GROUP_START and GROUP_END return only their tag; callers validate group context.
 *
 * \par Requirements:
 *  - 001 Field zero, oversized tags and wire types six/seven shall be rejected.
 *  - 002 Counted/fixed data and every varint shall fit the input without overflow.
 *  - 003 Failure shall leave field unchanged.
 *  - 004 Success shall retain no ownership, allocate nothing and call no CRT.
 *
 * \param[out] field       Non-NULL result
 * \param[in]  buffer      Non-NULL counted wire bytes
 * \param[in]  byteCount   Readable byte count
 *
 * \retval     TRUE    One complete record was decoded.
 * \retval     FALSE   An argument, encoding or bound was invalid.
 */
bool_t Utl_Protobuf_ReadField(Utl_Protobuf_Field_t* field, const byte_t* buffer, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Writes one validated record into caller-owned output.
 *
 * \par Details:
 *  BYTES source and output must not overlap. Group records write only their tags;
 *  a schema writer supplies the content and matching end marker separately.
 *
 * \par Requirements:
 *  - 001 Validation and capacity checks shall finish before output is changed.
 *  - 002 The writer shall emit shortest varints and little-endian fixed values.
 *  - 003 This function shall use no CRT, allocation or I/O.
 *
 * \param[in]  field      Non-NULL typed record
 * \param[out] buffer     Non-NULL output storage, disjoint from payload input
 * \param[in]  capacity   Writable byte count
 *
 * \retval     1..UINT32_MAX   Number of encoded record bytes written.
 * \retval     0               The record, output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t Utl_Protobuf_WriteField(const Utl_Protobuf_Field_t* field, byte_t* buffer, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Skips one complete field, including balanced deprecated groups.
 *
 * \par Details:
 *  Uses a fixed local stack of UTL_PROTOBUF_MAX_GROUP_DEPTH field numbers.
 *  Group endings must match their corresponding start field numbers.
 *
 * \par Requirements:
 *  - 001 The function shall reject stray/mismatched end markers and truncated groups.
 *  - 002 Exceeding the documented local nesting capacity shall fail without recursion.
 *  - 003 Failure shall preserve bytesRead; success shall count the entire field.
 *  - 004 No allocation, CRT or I/O shall be used.
 *
 * \param[out] bytesRead   Non-NULL complete field byte count
 * \param[in]  buffer      Non-NULL wire bytes
 * \param[in]  byteCount   Readable byte count
 *
 * \retval     TRUE    The complete field fits and has valid wire syntax.
 * \retval     FALSE   A bound, nesting, syntax or argument check failed.
 */
bool_t Utl_Protobuf_SkipField(uint32_t* bytesRead, const byte_t* buffer, uint32_t byteCount);

#endif /* UTL_PROTOBUF_H */
