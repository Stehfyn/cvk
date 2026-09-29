/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Wire examples exercise ISO BMFF Box syntax and RFC 8794/RFC 9559 element headers. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_mpeg4.h"
#include "utl/utl_matroska.h"
#include "utl/utl_protobuf.h"

#include "os/os_assert.h"
#include "os/os_process.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define FORMAT_TEST_SENTINEL           (0xA5)
#define FORMAT_TEST_BOX_BYTES          (16)
#define FORMAT_TEST_FREE_BOX_TYPE      (0x66726565)
#define FORMAT_TEST_LARGE_OFFSET_BITS  (32)
#define FORMAT_TEST_UUID_BASE_BYTES    (24)
#define FORMAT_TEST_VINT_DATA_BITS     (7)
#define FORMAT_TEST_SEGMENT_PAYLOAD    (5)
#define FORMAT_TEST_ELEMENT_ID         (0x80)
#define FORMAT_TEST_SHORT_SIZE_LIMIT   (127)
#define FORMAT_TEST_PROTO_CONTINUE     (0x80)
#define FORMAT_TEST_PROTO_CONTINUE_ONE (0x81)
#define FORMAT_TEST_PROTO_VALUE        (150)
#define FORMAT_TEST_PROTO_FIRST_BYTE   (0x96)
#define FORMAT_TEST_PROTO_INVALID_TAG  (0x0F)
#define FORMAT_TEST_GROUP_START        (0x0B)
#define FORMAT_TEST_GROUP_END          (0x0C)
#define FORMAT_TEST_GROUP_WRONG_END    (0x4C)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static const byte_t c_mp4Ordinary[] = {0, 0, 0, 16, 'f', 'r', 'e', 'e'};
static const byte_t c_mp4ToEnd[]    = {0, 0, 0, 0, 'm', 'd', 'a', 't'};
static const byte_t c_mp4LargeUuid[] =
    {0, 0, 0, 1, 'u', 'u', 'i', 'd', 0, 0, 0, 1, 0, 0, 0, 32, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
static const byte_t   c_matroskaSegment[] = {0x18, 0x53, 0x80, 0x67, 0x85};
static const byte_t   c_matroskaUnknown[] = {0x1F, 0x43, 0xB6, 0x75, 0x01, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
static const uint32_t c_validIds[]        = {0x80, 0xFE, 0x407F, 0x7FFE, 0x203FFF, 0x3FFFFE, 0x101FFFFF, 0x1FFFFFFE};
static const uint32_t c_invalidIds[] =
    {0, 0x7F, 0xFF, 0x4000, 0x407E, 0x7FFF, 0x200000, 0x203FFE, 0x3FFFFF, 0x101FFFFE, 0x1FFFFFFF};

static const uint64_t c_protoIntegers[]  = {0, 1, 127, 128, 150, 16384, UINT32_MAX, UINT64_MAX};
static const byte_t   c_protoGroups[]    = {0x43, 0x48, 0x96, 0x01, 0x53, 0x58, 0x01, 0x54, 0x44};
static const byte_t   c_protoBadLength[] = {0x0A, 0x80, 0x80, 0x80, 0x80, 0x10};
static const byte_t   c_protoBadTag[]    = {0x80, 0x80, 0x80, 0x80, 0x10, 0};
static const byte_t   c_protoBytes[]     = {0x12, 3, 'a', 'b', 'c'};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Terminates on a failed check with a source-line diagnostic exit code.
 * \param[in] passed   Predicate being checked
 * \param[in] code     Source line of the failed expectation
 *
 * \retval None
 */
static FORCE_INLINE void Check(bool_t passed, uint32_t code);

/*************************************************************************************************/
/**
 * \brief Compares counted buffers without a CRT dependency.
 * \param[in] first       First readable buffer
 * \param[in] second      Second readable buffer
 * \param[in] byteCount   Number of bytes to compare
 * \retval TRUE    All counted bytes agree.
 * \retval FALSE   At least one byte differs.
 */
static FORCE_INLINE bool_t EqualBytes(const byte_t* first, const byte_t* second, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Checks every valid ID across all supported size widths.
 *
 * \par Requirements:
 *  - 001 Every failed expectation shall terminate through the production assertion.
 *
 * \retval None
 */
static FORCE_INLINE void CheckMatroskaSizes(void);

/*************************************************************************************************/
/**
 * \brief Checks varint boundaries, malformed final octets and every truncated prefix.
 *
 * \par Requirements:
 *  - 001 Every failed expectation shall terminate through the production assertion.
 *
 * \retval None
 */
static FORCE_INLINE void CheckProtobufIntegers(void);

/*************************************************************************************************/
/**
 * \brief Checks nested groups, mismatched endings and the nesting limit.
 *
 * \par Requirements:
 *  - 001 Every failed expectation shall terminate through the production assertion.
 *
 * \retval None
 */
static FORCE_INLINE void CheckProtobufGroups(void);

/*************************************************************************************************/
/**
 * \brief Checks BMFF boundary arithmetic, header forms and transactional failure.
 *
 * \par Requirements:
 *  - 001 Every failed expectation shall terminate through the production assertion.
 *
 * \retval None
 */
static FORCE_INLINE void CheckMp4(void);

/*************************************************************************************************/
/**
 * \brief Checks all EBML integer widths, truncation and marker semantics.
 *
 * \par Requirements:
 *  - 001 Every failed expectation shall terminate through the production assertion.
 *
 * \retval None
 */
static FORCE_INLINE void CheckVints(void);

/*************************************************************************************************/
/**
 * \brief Checks Matroska IDs, size sentinels and enclosing boundaries.
 *
 * \par Requirements:
 *  - 001 Every failed expectation shall terminate through the production assertion.
 *
 * \retval None
 */
static FORCE_INLINE void CheckMatroska(void);

/*************************************************************************************************/
/**
 * \brief Checks protobuf integer overflow, all wire forms and nested group matching.
 *
 * \par Requirements:
 *  - 001 Every failed expectation shall terminate through the production assertion.
 *
 * \retval None
 */
static FORCE_INLINE void CheckProtobuf(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The production assertion records the source line and terminates on failure.
 */
static FORCE_INLINE void Check(bool_t passed, uint32_t code)
{

    if (!passed)
    {
        OS_AssertFailure("fixture check", __FILE__, code);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The caller establishes both readable spans.
 */
static FORCE_INLINE bool_t EqualBytes(const byte_t* first, const byte_t* second, uint32_t byteCount)
{
    uint32_t index;
    bool_t   equal;

    equal = TRUE;
    for (index = 0; index < byteCount; ++index)
    {
        if (first[index] != second[index])
        {
            equal = FALSE;
            break;
        }
    }
    return equal;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Checks every valid ID across all supported size widths.
 */
static FORCE_INLINE void CheckMatroskaSizes(void)
{
    Utl_Matroska_ElementHeader_t header;
    Utl_Matroska_ElementHeader_t decoded;
    byte_t                       output[UTL_MATROSKA_MAX_HEADER_BYTES];
    uint32_t                     index;
    uint32_t                     size_width;
    uint32_t                     width;
    bool_t                       accepted;

    index = 0;
    for (;;)
    {
        header.id          = c_validIds[index];
        header.unknownSize = FALSE;
        size_width         = 1;
        for (;;)
        {
            header.sizeByteCount    = size_width;
            header.payloadByteCount = ((uint64_t)1 << (FORMAT_TEST_VINT_DATA_BITS * size_width)) - 2;
            width                   = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
            Check(0 < width, __LINE__);
            accepted = Utl_Matroska_ReadElementHeader(&decoded, output, width, UINT64_MAX);
            Check(accepted && !decoded.unknownSize, __LINE__);
            Check(header.id == decoded.id, __LINE__);
            Check(header.payloadByteCount == decoded.payloadByteCount, __LINE__);
            Check(size_width == decoded.sizeByteCount, __LINE__);
            ++size_width;
            if (!(UTL_MATROSKA_MAX_SIZE_BYTES >= size_width))
            {
                break;
            }
        }
        ++index;
        if (!((sizeof(c_validIds) / sizeof(c_validIds[0])) > index))
        {
            break;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Checks varint boundaries, malformed final octets and every truncated prefix.
 */
static FORCE_INLINE void CheckProtobufIntegers(void)
{
    byte_t   encoded[UTL_PROTOBUF_MAX_VARINT_BYTES];
    uint32_t case_index;
    uint32_t width;
    uint32_t consumed;
    uint32_t prefix;
    uint64_t value;
    bool_t   accepted;

    case_index = 0;
    for (;;)
    {
        width = Utl_Protobuf_WriteVarint(c_protoIntegers[case_index], encoded, sizeof(encoded));
        Check(0 < width, __LINE__);
        value    = 0;
        consumed = 0;
        accepted = Utl_Protobuf_ReadVarint(&value, &consumed, encoded, width);
        Check(accepted && (c_protoIntegers[case_index] == value), __LINE__);
        Check(width == consumed, __LINE__);
        prefix = 0;
        for (;;)
        {
            accepted = Utl_Protobuf_ReadVarint(&value, &consumed, encoded, prefix);
            Check(!accepted, __LINE__);
            Check((c_protoIntegers[case_index] == value) && (width == consumed), __LINE__);
            ++prefix;
            if (prefix == width)
            {
                break;
            }
        }
        ++case_index;
        if ((sizeof(c_protoIntegers) / sizeof(c_protoIntegers[0])) == case_index)
        {
            break;
        }
    }
    Check((UTL_PROTOBUF_MAX_VARINT_BYTES == width) && (1 == encoded[UTL_PROTOBUF_MAX_VARINT_BYTES - 1]), __LINE__);
    encoded[UTL_PROTOBUF_MAX_VARINT_BYTES - 1] = 2;
    accepted                                   = Utl_Protobuf_ReadVarint(&value, &consumed, encoded, width);
    Check(!accepted && (UINT64_MAX == value), __LINE__);
    encoded[UTL_PROTOBUF_MAX_VARINT_BYTES - 1] = FORMAT_TEST_PROTO_CONTINUE_ONE;
    accepted                                   = Utl_Protobuf_ReadVarint(&value, &consumed, encoded, width);
    Check(!accepted, __LINE__);
    encoded[0] = FORMAT_TEST_PROTO_CONTINUE;
    encoded[1] = 0;
    accepted   = Utl_Protobuf_ReadVarint(&value, &consumed, encoded, 2);
    Check(accepted && (0 == value) && (2 == consumed), __LINE__);
    width = Utl_Protobuf_WriteVarint(FORMAT_TEST_PROTO_VALUE, encoded, sizeof(encoded));
    Check((2 == width) && (FORMAT_TEST_PROTO_FIRST_BYTE == encoded[0]) && (1 == encoded[1]), __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Checks nested groups, mismatched endings and the nesting limit.
 */
static FORCE_INLINE void CheckProtobufGroups(void)
{
    byte_t   encoded[(UTL_PROTOBUF_MAX_GROUP_DEPTH + 1) * 2];
    uint32_t consumed;
    uint32_t prefix;
    uint32_t index;
    bool_t   accepted;

    accepted = Utl_Protobuf_SkipField(&consumed, c_protoGroups, sizeof(c_protoGroups));
    Check(accepted && (sizeof(c_protoGroups) == consumed), __LINE__);
    prefix = 0;
    for (;;)
    {
        accepted = Utl_Protobuf_SkipField(&consumed, c_protoGroups, prefix);
        Check(!accepted && (sizeof(c_protoGroups) == consumed), __LINE__);
        ++prefix;
        if (sizeof(c_protoGroups) == prefix)
        {
            break;
        }
    }
    index = 0;
    for (;;)
    {
        encoded[index] = c_protoGroups[index];
        ++index;
        if (sizeof(c_protoGroups) == index)
        {
            break;
        }
    }
    encoded[sizeof(c_protoGroups) - 1] = FORMAT_TEST_GROUP_WRONG_END;
    accepted                           = Utl_Protobuf_SkipField(&consumed, encoded, sizeof(c_protoGroups));
    Check(!accepted, __LINE__);
    index = 0;
    for (;;)
    {
        encoded[index]                                = FORMAT_TEST_GROUP_START;
        encoded[UTL_PROTOBUF_MAX_GROUP_DEPTH + index] = FORMAT_TEST_GROUP_END;
        ++index;
        if (UTL_PROTOBUF_MAX_GROUP_DEPTH == index)
        {
            break;
        }
    }
    accepted = Utl_Protobuf_SkipField(&consumed, encoded, UTL_PROTOBUF_MAX_GROUP_DEPTH * 2);
    Check(accepted, __LINE__);
    encoded[UTL_PROTOBUF_MAX_GROUP_DEPTH] = FORMAT_TEST_GROUP_START;
    accepted = Utl_Protobuf_SkipField(&consumed, encoded, UTL_PROTOBUF_MAX_GROUP_DEPTH * 2);
    Check(!accepted, __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Partial headers must preserve every published output field.
 */
static FORCE_INLINE void CheckMp4(void)
{
    Utl_Mpeg4_Span_t      bytes;
    Utl_Mpeg4_BoxHeader_t header;
    Utl_Mpeg4_BoxHeader_t saved;
    byte_t                output[UTL_MPEG4_MAX_BOX_HEADER_BYTES];
    uint64_t              large_size;
    uint32_t              width;
    uint32_t              index;
    bool_t                accepted;
    bool_t                equal;

    bytes.data = c_mp4Ordinary;

    bytes.byteCount = sizeof(c_mp4Ordinary);

    accepted = Utl_Mpeg4_ReadBoxHeader(&header, bytes, FORMAT_TEST_BOX_BYTES, FORMAT_TEST_BOX_BYTES);
    Check(accepted, __LINE__);
    Check(FORMAT_TEST_BOX_BYTES == header.byteCount, __LINE__);
    Check(FORMAT_TEST_FREE_BOX_TYPE == header.type, __LINE__);
    Check(sizeof(c_mp4Ordinary) == Utl_Mpeg4_GetBoxHeaderByteCount(&header), __LINE__);
    Check(!header.hasLargeSize && !header.extendsToEnd, __LINE__);
    width = Utl_Mpeg4_WriteBoxHeader(&header, output, sizeof(output));
    Check(sizeof(c_mp4Ordinary) == width, __LINE__);
    equal = EqualBytes(output, c_mp4Ordinary, width);
    Check(equal, __LINE__);
    bytes.data      = c_mp4ToEnd;
    bytes.byteCount = sizeof(c_mp4ToEnd);
    accepted        = Utl_Mpeg4_ReadBoxHeader(&header, bytes, UINT64_MAX, UINT64_MAX);
    Check(accepted && header.extendsToEnd, __LINE__);
    Check(UINT64_MAX == header.byteCount, __LINE__);
    width = Utl_Mpeg4_WriteBoxHeader(&header, output, sizeof(output));
    equal = EqualBytes(output, c_mp4ToEnd, sizeof(c_mp4ToEnd));
    Check((sizeof(c_mp4ToEnd) == width) && equal, __LINE__);

    large_size      = ((uint64_t)1 << FORMAT_TEST_LARGE_OFFSET_BITS) + sizeof(c_mp4LargeUuid);
    bytes.data      = c_mp4LargeUuid;
    bytes.byteCount = sizeof(c_mp4LargeUuid);
    accepted        = Utl_Mpeg4_ReadBoxHeader(&header, bytes, large_size, large_size);
    Check(accepted, __LINE__);
    Check(large_size == header.byteCount, __LINE__);
    Check(sizeof(c_mp4LargeUuid) == Utl_Mpeg4_GetBoxHeaderByteCount(&header), __LINE__);
    Check(header.hasLargeSize && !header.extendsToEnd, __LINE__);
    width = Utl_Mpeg4_WriteBoxHeader(&header, output, sizeof(output));
    equal = EqualBytes(output, c_mp4LargeUuid, sizeof(c_mp4LargeUuid));
    Check((sizeof(c_mp4LargeUuid) == width) && equal, __LINE__);
    saved = header;
    index = 0;
    for (;;)
    {
        bytes.data      = c_mp4LargeUuid;
        bytes.byteCount = index;
        accepted        = Utl_Mpeg4_ReadBoxHeader(&header, bytes, large_size, large_size);
        Check(!accepted, __LINE__);
        Check(saved.byteCount == header.byteCount, __LINE__);
        Check(saved.type == header.type, __LINE__);
        Check(Utl_Mpeg4_GetBoxHeaderByteCount(&saved) == Utl_Mpeg4_GetBoxHeaderByteCount(&header), __LINE__);
        equal = EqualBytes(saved.userType, header.userType, UTL_MPEG4_USER_TYPE_BYTES);
        Check(equal && header.hasLargeSize && !header.extendsToEnd, __LINE__);
        ++index;
        if (!(sizeof(c_mp4LargeUuid) > index))
        {
            break;
        }
    }
    bytes.data      = c_mp4LargeUuid;
    bytes.byteCount = sizeof(c_mp4LargeUuid);
    accepted        = Utl_Mpeg4_ReadBoxHeader(&header, bytes, large_size - 1, large_size - 1);
    Check(!accepted, __LINE__);
    bytes.data      = c_mp4Ordinary;
    bytes.byteCount = sizeof(c_mp4Ordinary);
    accepted        = Utl_Mpeg4_ReadBoxHeader(&header, bytes, FORMAT_TEST_BOX_BYTES - 1, FORMAT_TEST_BOX_BYTES - 1);
    Check(!accepted, __LINE__);
    bytes.data      = c_mp4ToEnd;
    bytes.byteCount = sizeof(c_mp4ToEnd);
    accepted        = Utl_Mpeg4_ReadBoxHeader(&header, bytes, sizeof(c_mp4Ordinary) - 1, sizeof(c_mp4Ordinary) - 1);
    Check(!accepted, __LINE__);

    index = 0;
    for (;;)
    {
        output[index] = FORMAT_TEST_SENTINEL;
        ++index;
        if (!(sizeof(output) > index))
        {
            break;
        }
    }
    index = 0;
    for (;;)
    {
        width = Utl_Mpeg4_WriteBoxHeader(&saved, output, index);
        Check(0 == width, __LINE__);
        ++index;
        if (!(sizeof(output) > index))
        {
            break;
        }
    }
    saved.extendsToEnd = TRUE;
    width              = Utl_Mpeg4_WriteBoxHeader(&saved, output, sizeof(output));
    Check(0 == width, __LINE__);
    saved.extendsToEnd = FALSE;
    saved.hasLargeSize = FALSE;
    width              = Utl_Mpeg4_WriteBoxHeader(&saved, output, sizeof(output));
    Check(0 == width, __LINE__);
    saved.byteCount = FORMAT_TEST_UUID_BASE_BYTES - 1;
    width           = Utl_Mpeg4_WriteBoxHeader(&saved, output, sizeof(output));
    Check(0 == width, __LINE__);
    index = 0;
    for (;;)
    {
        Check(FORMAT_TEST_SENTINEL == output[index], __LINE__);
        ++index;
        if (!(sizeof(output) > index))
        {
            break;
        }
    }
    bytes.data      = NULL;
    bytes.byteCount = 0;
    accepted        = Utl_Mpeg4_ReadBoxHeader(&header, bytes, FORMAT_TEST_BOX_BYTES, FORMAT_TEST_BOX_BYTES);
    Check(!accepted, __LINE__);
    bytes.data      = c_mp4Ordinary;
    bytes.byteCount = sizeof(c_mp4Ordinary);
    accepted        = Utl_Mpeg4_ReadBoxHeader(&header, bytes, FORMAT_TEST_BOX_BYTES + 1, FORMAT_TEST_BOX_BYTES);
    Check(!accepted, __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Known independent encodings cover every width and truncated prefix.
 */
static FORCE_INLINE void CheckVints(void)
{
    Utl_Matroska_Vint_t value;
    byte_t              encoded[UTL_MATROSKA_MAX_SIZE_BYTES];
    uint32_t            width;
    uint32_t            index;
    uint32_t            prefix;
    uint64_t            maximum;
    bool_t              accepted;

    width = 1;
    for (;;)
    {
        index = 0;
        for (;;)
        {
            encoded[index] = UINT8_MAX;
            ++index;
            if (!(sizeof(encoded) > index))
            {
                break;
            }
        }
        encoded[0] = (byte_t)(UINT8_MAX >> (width - 1));
        maximum    = ((uint64_t)1 << (FORMAT_TEST_VINT_DATA_BITS * width)) - 1;
        accepted   = Utl_Matroska_ReadVint(&value, encoded, width);
        Check(accepted && value.allDataBitsSet, __LINE__);
        Check(maximum == value.value, __LINE__);
        Check(width == value.byteCount, __LINE__);
        prefix = 0;
        for (;;)
        {
            accepted = Utl_Matroska_ReadVint(&value, encoded, prefix);
            Check(!accepted, __LINE__);
            Check((maximum == value.value) && (width == value.byteCount), __LINE__);
            ++prefix;
            if (!(prefix < width))
            {
                break;
            }
        }
        --encoded[width - 1];
        accepted = Utl_Matroska_ReadVint(&value, encoded, width);
        Check(accepted && !value.allDataBitsSet, __LINE__);
        Check((maximum - 1) == value.value, __LINE__);
        ++width;
        if (!(UTL_MATROSKA_MAX_SIZE_BYTES >= width))
        {
            break;
        }
    }
    encoded[0] = 0;
    accepted   = Utl_Matroska_ReadVint(&value, encoded, sizeof(encoded));
    Check(!accepted, __LINE__);
    accepted = Utl_Matroska_ReadVint(NULL, encoded, sizeof(encoded));
    Check(!accepted, __LINE__);
    accepted = Utl_Matroska_ReadVint(&value, NULL, 1);
    Check(!accepted, __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Invalid writes must preserve the entire destination, not just its first byte.
 */
static FORCE_INLINE void CheckMatroska(void)
{
    Utl_Matroska_ElementHeader_t header;
    Utl_Matroska_ElementHeader_t decoded;
    byte_t                       output[UTL_MATROSKA_MAX_HEADER_BYTES];
    uint32_t                     index;
    uint32_t                     width;
    bool_t                       accepted;
    bool_t                       equal;

    accepted = Utl_Matroska_ReadElementHeader(&header,
                                              c_matroskaSegment,
                                              sizeof(c_matroskaSegment),
                                              sizeof(c_matroskaSegment) + FORMAT_TEST_SEGMENT_PAYLOAD);
    Check(accepted && !header.unknownSize, __LINE__);
    Check(UTL_MATROSKA_ID_SEGMENT == header.id, __LINE__);
    Check((FORMAT_TEST_SEGMENT_PAYLOAD == header.payloadByteCount) &&
              (sizeof(c_matroskaSegment) == header.headerByteCount),
          __LINE__);
    width = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
    equal = EqualBytes(output, c_matroskaSegment, sizeof(c_matroskaSegment));
    Check((sizeof(c_matroskaSegment) == width) && equal, __LINE__);
    accepted = Utl_Matroska_ReadElementHeader(&decoded,
                                              c_matroskaSegment,
                                              sizeof(c_matroskaSegment),
                                              sizeof(c_matroskaSegment) + FORMAT_TEST_SEGMENT_PAYLOAD - 1);
    Check(!accepted, __LINE__);
    accepted = Utl_Matroska_ReadElementHeader(&header, c_matroskaUnknown, sizeof(c_matroskaUnknown), UINT64_MAX);
    Check(accepted && header.unknownSize, __LINE__);
    Check((0 == header.payloadByteCount) && (UTL_MATROSKA_MAX_SIZE_BYTES == header.sizeByteCount), __LINE__);
    width = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
    equal = EqualBytes(output, c_matroskaUnknown, sizeof(c_matroskaUnknown));
    Check((sizeof(c_matroskaUnknown) == width) && equal, __LINE__);
    index = 0;
    for (;;)
    {
        decoded  = header;
        accepted = Utl_Matroska_ReadElementHeader(&decoded, c_matroskaUnknown, index, UINT64_MAX);
        Check(!accepted, __LINE__);
        Check(header.id == decoded.id, __LINE__);
        Check(header.payloadByteCount == decoded.payloadByteCount, __LINE__);
        Check(header.headerByteCount == decoded.headerByteCount, __LINE__);
        Check((header.sizeByteCount == decoded.sizeByteCount) && decoded.unknownSize, __LINE__);
        ++index;
        if (!(sizeof(c_matroskaUnknown) > index))
        {
            break;
        }
    }

    CheckMatroskaSizes();

    index = 0;
    for (;;)
    {
        output[index] = FORMAT_TEST_SENTINEL;
        ++index;
        if (!(sizeof(output) > index))
        {
            break;
        }
    }
    header.unknownSize      = FALSE;
    header.sizeByteCount    = 1;
    header.payloadByteCount = 0;
    index                   = 0;
    for (;;)
    {
        header.id = c_invalidIds[index];
        width     = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
        Check(0 == width, __LINE__);
        ++index;
        if (!((sizeof(c_invalidIds) / sizeof(c_invalidIds[0])) > index))
        {
            break;
        }
    }
    header.id               = FORMAT_TEST_ELEMENT_ID;
    header.payloadByteCount = FORMAT_TEST_SHORT_SIZE_LIMIT;
    width                   = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
    Check(0 == width, __LINE__);
    header.payloadByteCount = 0;
    header.sizeByteCount    = 0;
    width                   = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
    Check(0 == width, __LINE__);
    header.sizeByteCount = UTL_MATROSKA_MAX_SIZE_BYTES + 1;
    width                = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
    Check(0 == width, __LINE__);
    header.sizeByteCount = UTL_MATROSKA_MAX_SIZE_BYTES;
    width                = Utl_Matroska_WriteElementHeader(&header, output, UTL_MATROSKA_MAX_SIZE_BYTES);
    Check(0 == width, __LINE__);
    index = 0;
    for (;;)
    {
        Check(FORMAT_TEST_SENTINEL == output[index], __LINE__);
        ++index;
        if (!(sizeof(output) > index))
        {
            break;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Boundary values and malformed vectors are independent of the writer. Group
 *  tests exercise truncation, mismatched endings and the fixed nesting limit.
 */
static FORCE_INLINE void CheckProtobuf(void)
{
    byte_t               encoded[(UTL_PROTOBUF_MAX_GROUP_DEPTH + 1) * 2];
    uint32_t             width;
    uint32_t             consumed;
    uint32_t             prefix;
    uint32_t             index;
    Utl_Protobuf_Field_t field;
    Utl_Protobuf_Field_t decoded;
    bool_t               accepted;
    bool_t               equal;

    CheckProtobufIntegers();

    accepted = Utl_Protobuf_ReadField(&field, c_protoBytes, sizeof(c_protoBytes));
    Check(accepted && (2 == field.number) && (UTL_PROTOBUF_BYTES == field.wireType), __LINE__);
    Check((3 == field.byteCount) && (sizeof(c_protoBytes) == field.encodedByteCount), __LINE__);
    width = Utl_Protobuf_WriteField(&field, encoded, sizeof(encoded));
    equal = EqualBytes(encoded, c_protoBytes, sizeof(c_protoBytes));
    Check(equal && (sizeof(c_protoBytes) == width), __LINE__);
    accepted = Utl_Protobuf_ReadField(&field, c_protoBadLength, sizeof(c_protoBadLength));
    Check(!accepted && (2 == field.number), __LINE__);
    accepted = Utl_Protobuf_ReadField(&field, c_protoBadTag, sizeof(c_protoBadTag));
    Check(!accepted, __LINE__);
    encoded[0] = 0;
    accepted   = Utl_Protobuf_ReadField(&field, encoded, 1);
    Check(!accepted, __LINE__);
    encoded[0] = FORMAT_TEST_PROTO_INVALID_TAG;
    accepted   = Utl_Protobuf_ReadField(&field, encoded, 1);
    Check(!accepted, __LINE__);

    field.number       = UTL_PROTOBUF_MAX_FIELD_NUMBER;
    field.integerValue = UINT64_MAX;
    field.wireType     = UTL_PROTOBUF_FIXED64;
    width              = Utl_Protobuf_WriteField(&field, encoded, sizeof(encoded));
    accepted           = Utl_Protobuf_ReadField(&decoded, encoded, width);
    Check(accepted && (UINT64_MAX == decoded.integerValue), __LINE__);
    Check(field.number == decoded.number, __LINE__);
    field.wireType = UTL_PROTOBUF_FIXED32;
    width          = Utl_Protobuf_WriteField(&field, encoded, sizeof(encoded));
    Check(0 == width, __LINE__);
    field.integerValue = UINT32_MAX;
    width              = Utl_Protobuf_WriteField(&field, encoded, sizeof(encoded));
    accepted           = Utl_Protobuf_ReadField(&decoded, encoded, width);
    Check(accepted && (UINT32_MAX == decoded.integerValue), __LINE__);
    index = 0;
    for (;;)
    {
        encoded[index] = FORMAT_TEST_SENTINEL;
        ++index;
        if (sizeof(encoded) == index)
        {
            break;
        }
    }
    prefix = 0;
    for (;;)
    {
        consumed = Utl_Protobuf_WriteField(&field, encoded, prefix);
        Check(0 == consumed, __LINE__);
        ++prefix;
        if (prefix == width)
        {
            break;
        }
    }
    index = 0;
    for (;;)
    {
        Check(FORMAT_TEST_SENTINEL == encoded[index], __LINE__);
        ++index;
        if (sizeof(encoded) == index)
        {
            break;
        }
    }

    CheckProtobufGroups();
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Runs wire-format checks using a linker-selected entry without CRT startup.
 * \par Details:
 *  The platform entry receives the fixture result.
 */
uint32_t OS_Application_Main(void)
{

    CheckMp4();
    CheckVints();
    CheckMatroska();
    CheckProtobuf();
    return 0;
}
