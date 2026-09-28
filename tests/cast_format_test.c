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

#include <windows.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define FORMAT_TEST_SENTINEL (0xA5)

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
static const char_t   c_failure[] = "Cast format checks failed; exit code identifies the check.\r\n";
static const char_t   c_success[] = "Cast format checks passed (CRT-free entry).\r\n";

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
 * \brief Terminates on a failed check with a stable diagnostic exit code.
 * \param[in] passed   Predicate being checked
 * \param[in] code     Failure identifier
 */
static void Check(bool_t passed, uint32_t code);

/*************************************************************************************************/
/**
 * \brief Compares counted buffers without a CRT dependency.
 * \param[in] first       First readable buffer
 * \param[in] second      Second readable buffer
 * \param[in] byteCount   Number of bytes to compare
 * \retval TRUE    All counted bytes agree.
 * \retval FALSE   At least one byte differs.
 */
static bool_t EqualBytes(const byte_t* first, const byte_t* second, uint32_t byteCount);

/*************************************************************************************************/
/** \brief Checks BMFF boundary arithmetic, header forms and transactional failure. */
static void CheckMp4(void);

/*************************************************************************************************/
/** \brief Checks all EBML integer widths, truncation and marker semantics. */
static void CheckVints(void);

/*************************************************************************************************/
/** \brief Checks Matroska IDs, size sentinels and enclosing boundaries. */
static void CheckMatroska(void);

/*************************************************************************************************/
/** \brief Checks protobuf integer overflow, all wire forms and nested group matching. */
static void CheckProtobuf(void);

void WinMainStartup(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/** \par Details: Only Win32 output and process termination are needed on failure. */
static void Check(bool_t passed, uint32_t code)
{
    DWORD written;

    if (!passed)
    {
        WriteFile(GetStdHandle(STD_ERROR_HANDLE), c_failure, sizeof(c_failure) - 1, &written, NULL);
        ExitProcess(code);
    }
}

/*************************************************************************************************/
/** \par Details: The caller establishes both readable spans. */
static bool_t EqualBytes(const byte_t* first, const byte_t* second, uint32_t byteCount)
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
/** \par Details: Partial headers must preserve every published output field. */
static void CheckMp4(void)
{
    Utl_Mpeg4_Span_t bytes;
    Utl_Mpeg4_BoxHeader_t header;
    Utl_Mpeg4_BoxHeader_t saved;
    byte_t              output[UTL_MPEG4_MAX_BOX_HEADER_BYTES];
    uint64_t            large_size;
    uint32_t            width;
    uint32_t            index;
    bool_t              accepted;
    bool_t              equal;

    bytes.data = c_mp4Ordinary;

    bytes.byteCount = sizeof(c_mp4Ordinary);

    accepted = Utl_Mpeg4_ReadBoxHeader(&header, bytes, 16, 16);
    Check(accepted, 101);
    Check(16 == header.byteCount, 102);
    Check(0x66726565 == header.type, 103);
    Check(8 == Utl_Mpeg4_GetBoxHeaderByteCount(&header), 104);
    Check(!header.hasLargeSize && !header.extendsToEnd, 105);
    width = Utl_Mpeg4_WriteBoxHeader(&header, output, sizeof(output));
    Check(sizeof(c_mp4Ordinary) == width, 106);
    equal = EqualBytes(output, c_mp4Ordinary, width);
    Check(equal, 107);
    bytes.data = c_mp4ToEnd;
    bytes.byteCount = sizeof(c_mp4ToEnd);
    accepted = Utl_Mpeg4_ReadBoxHeader(&header, bytes, UINT64_MAX, UINT64_MAX);
    Check(accepted && header.extendsToEnd, 108);
    Check(UINT64_MAX == header.byteCount, 109);
    width = Utl_Mpeg4_WriteBoxHeader(&header, output, sizeof(output));
    equal = EqualBytes(output, c_mp4ToEnd, sizeof(c_mp4ToEnd));
    Check((sizeof(c_mp4ToEnd) == width) && equal, 110);

    large_size = ((uint64_t)1 << 32) + 32;
    bytes.data = c_mp4LargeUuid;
    bytes.byteCount = sizeof(c_mp4LargeUuid);
    accepted = Utl_Mpeg4_ReadBoxHeader(&header, bytes, large_size, large_size);
    Check(accepted, 111);
    Check(large_size == header.byteCount, 112);
    Check(32 == Utl_Mpeg4_GetBoxHeaderByteCount(&header), 113);
    Check(header.hasLargeSize && !header.extendsToEnd, 114);
    width = Utl_Mpeg4_WriteBoxHeader(&header, output, sizeof(output));
    equal = EqualBytes(output, c_mp4LargeUuid, sizeof(c_mp4LargeUuid));
    Check((sizeof(c_mp4LargeUuid) == width) && equal, 115);
    saved = header;
    index = 0;
    for (;;)
    {
        bytes.data = c_mp4LargeUuid;
        bytes.byteCount = index;
        accepted = Utl_Mpeg4_ReadBoxHeader(&header, bytes, large_size, large_size);
        Check(!accepted, 116);
        Check(saved.byteCount == header.byteCount, 117);
        Check(saved.type == header.type, 118);
        Check(Utl_Mpeg4_GetBoxHeaderByteCount(&saved) == Utl_Mpeg4_GetBoxHeaderByteCount(&header), 119);
        equal = EqualBytes(saved.userType, header.userType, UTL_MPEG4_USER_TYPE_BYTES);
        Check(equal && header.hasLargeSize && !header.extendsToEnd, 120);
        ++index;
        if (!(sizeof(c_mp4LargeUuid) > index))
        {
            break;
        }
    }
    bytes.data = c_mp4LargeUuid;
    bytes.byteCount = sizeof(c_mp4LargeUuid);
    accepted = Utl_Mpeg4_ReadBoxHeader(&header, bytes, large_size - 1, large_size - 1);
    Check(!accepted, 121);
    bytes.data = c_mp4Ordinary;
    bytes.byteCount = sizeof(c_mp4Ordinary);
    accepted = Utl_Mpeg4_ReadBoxHeader(&header, bytes, 15, 15);
    Check(!accepted, 122);
    bytes.data = c_mp4ToEnd;
    bytes.byteCount = sizeof(c_mp4ToEnd);
    accepted = Utl_Mpeg4_ReadBoxHeader(&header, bytes, 7, 7);
    Check(!accepted, 123);

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
        Check(0 == width, 124);
        ++index;
        if (!(sizeof(output) > index))
        {
            break;
        }
    }
    saved.extendsToEnd = TRUE;
    width              = Utl_Mpeg4_WriteBoxHeader(&saved, output, sizeof(output));
    Check(0 == width, 125);
    saved.extendsToEnd = FALSE;
    saved.hasLargeSize = FALSE;
    width              = Utl_Mpeg4_WriteBoxHeader(&saved, output, sizeof(output));
    Check(0 == width, 126);
    saved.byteCount = 23;
    width           = Utl_Mpeg4_WriteBoxHeader(&saved, output, sizeof(output));
    Check(0 == width, 127);
    index = 0;
    for (;;)
    {
        Check(FORMAT_TEST_SENTINEL == output[index], 128);
        ++index;
        if (!(sizeof(output) > index))
        {
            break;
        }
    }
    bytes.data = NULL;
    bytes.byteCount = 0;
    accepted = Utl_Mpeg4_ReadBoxHeader(&header, bytes, 16, 16);
    Check(!accepted, 129);
    bytes.data = c_mp4Ordinary;
    bytes.byteCount = sizeof(c_mp4Ordinary);
    accepted = Utl_Mpeg4_ReadBoxHeader(&header, bytes, 17, 16);
    Check(!accepted, 130);
}

/*************************************************************************************************/
/** \par Details: Known independent encodings cover every width and truncated prefix. */
static void CheckVints(void)
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
            encoded[index] = 0xFF;
            ++index;
            if (!(sizeof(encoded) > index))
            {
                break;
            }
        }
        encoded[0] = (byte_t)(0xFF >> (width - 1));
        maximum    = ((uint64_t)1 << (7 * width)) - 1;
        accepted   = Utl_Matroska_ReadVint(&value, encoded, width);
        Check(accepted && value.allDataBitsSet, 201);
        Check(maximum == value.value, 202);
        Check(width == value.byteCount, 203);
        prefix = 0;
        for (;;)
        {
            accepted = Utl_Matroska_ReadVint(&value, encoded, prefix);
            Check(!accepted, 204);
            Check((maximum == value.value) && (width == value.byteCount), 205);
            ++prefix;
            if (!(prefix < width))
            {
                break;
            }
        }
        --encoded[width - 1];
        accepted = Utl_Matroska_ReadVint(&value, encoded, width);
        Check(accepted && !value.allDataBitsSet, 206);
        Check((maximum - 1) == value.value, 207);
        ++width;
        if (!(UTL_MATROSKA_MAX_SIZE_BYTES >= width))
        {
            break;
        }
    }
    encoded[0] = 0;
    accepted   = Utl_Matroska_ReadVint(&value, encoded, sizeof(encoded));
    Check(!accepted, 208);
    accepted = Utl_Matroska_ReadVint(NULL, encoded, sizeof(encoded));
    Check(!accepted, 209);
    accepted = Utl_Matroska_ReadVint(&value, NULL, 1);
    Check(!accepted, 210);
}

/*************************************************************************************************/
/** \par Details: Invalid writes must preserve the entire destination, not just its first byte. */
static void CheckMatroska(void)
{
    Utl_Matroska_ElementHeader_t header;
    Utl_Matroska_ElementHeader_t decoded;
    byte_t                       output[UTL_MATROSKA_MAX_HEADER_BYTES];
    uint32_t                     index;
    uint32_t                     width;
    uint32_t                     size_width;
    bool_t                       accepted;
    bool_t                       equal;

    accepted = Utl_Matroska_ReadElementHeader(&header, c_matroskaSegment, sizeof(c_matroskaSegment), 10);
    Check(accepted && !header.unknownSize, 301);
    Check(UTL_MATROSKA_ID_SEGMENT == header.id, 302);
    Check((5 == header.payloadByteCount) && (5 == header.headerByteCount), 303);
    width = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
    equal = EqualBytes(output, c_matroskaSegment, sizeof(c_matroskaSegment));
    Check((sizeof(c_matroskaSegment) == width) && equal, 304);
    accepted = Utl_Matroska_ReadElementHeader(&decoded, c_matroskaSegment, sizeof(c_matroskaSegment), 9);
    Check(!accepted, 305);
    accepted = Utl_Matroska_ReadElementHeader(&header, c_matroskaUnknown, sizeof(c_matroskaUnknown), UINT64_MAX);
    Check(accepted && header.unknownSize, 306);
    Check((0 == header.payloadByteCount) && (8 == header.sizeByteCount), 307);
    width = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
    equal = EqualBytes(output, c_matroskaUnknown, sizeof(c_matroskaUnknown));
    Check((sizeof(c_matroskaUnknown) == width) && equal, 308);
    index = 0;
    for (;;)
    {
        decoded  = header;
        accepted = Utl_Matroska_ReadElementHeader(&decoded, c_matroskaUnknown, index, UINT64_MAX);
        Check(!accepted, 309);
        Check(header.id == decoded.id, 310);
        Check(header.payloadByteCount == decoded.payloadByteCount, 311);
        Check(header.headerByteCount == decoded.headerByteCount, 312);
        Check((header.sizeByteCount == decoded.sizeByteCount) && decoded.unknownSize, 313);
        ++index;
        if (!(sizeof(c_matroskaUnknown) > index))
        {
            break;
        }
    }

    index = 0;
    for (;;)
    {
        header.id          = c_validIds[index];
        header.unknownSize = FALSE;
        size_width         = 1;
        for (;;)
        {
            header.sizeByteCount    = size_width;
            header.payloadByteCount = ((uint64_t)1 << (7 * size_width)) - 2;
            width                   = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
            Check(0 < width, 314);
            accepted = Utl_Matroska_ReadElementHeader(&decoded, output, width, UINT64_MAX);
            Check(accepted && !decoded.unknownSize, 315);
            Check(header.id == decoded.id, 316);
            Check(header.payloadByteCount == decoded.payloadByteCount, 317);
            Check(size_width == decoded.sizeByteCount, 318);
            ++size_width;
            if (!(UTL_MATROSKA_MAX_SIZE_BYTES >= size_width))
            {
                break;
            }
        }
        ++index;
        if (!(ARRAYSIZE(c_validIds) > index))
        {
            break;
        }
    }

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
        Check(0 == width, 319);
        ++index;
        if (!(ARRAYSIZE(c_invalidIds) > index))
        {
            break;
        }
    }
    header.id               = 0x80;
    header.payloadByteCount = 127;
    width                   = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
    Check(0 == width, 320);
    header.payloadByteCount = 0;
    header.sizeByteCount    = 0;
    width                   = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
    Check(0 == width, 321);
    header.sizeByteCount = 9;
    width                = Utl_Matroska_WriteElementHeader(&header, output, sizeof(output));
    Check(0 == width, 322);
    header.sizeByteCount = 8;
    width                = Utl_Matroska_WriteElementHeader(&header, output, 8);
    Check(0 == width, 323);
    index = 0;
    for (;;)
    {
        Check(FORMAT_TEST_SENTINEL == output[index], 324);
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
static void CheckProtobuf(void)
{
    byte_t               encoded[(UTL_PROTOBUF_MAX_GROUP_DEPTH + 1) * 2];
    uint32_t             case_index;
    uint32_t             width;
    uint32_t             consumed;
    uint32_t             prefix;
    uint32_t             index;
    uint64_t             value;
    Utl_Protobuf_Field_t field;
    Utl_Protobuf_Field_t decoded;
    bool_t               accepted;
    bool_t               equal;

    case_index = 0;
    for (;;)
    {
        width = Utl_Protobuf_WriteVarint(c_protoIntegers[case_index], encoded, sizeof(encoded));
        Check(0 < width, 401);
        value    = 0;
        consumed = 0;
        accepted = Utl_Protobuf_ReadVarint(&value, &consumed, encoded, width);
        Check(accepted && (c_protoIntegers[case_index] == value), 402);
        Check(width == consumed, 403);
        prefix = 0;
        for (;;)
        {
            accepted = Utl_Protobuf_ReadVarint(&value, &consumed, encoded, prefix);
            Check(!accepted, 404);
            Check((c_protoIntegers[case_index] == value) && (width == consumed), 405);
            ++prefix;
            if (prefix == width)
            {
                break;
            }
        }
        ++case_index;
        if (ARRAYSIZE(c_protoIntegers) == case_index)
        {
            break;
        }
    }
    Check((10 == width) && (1 == encoded[9]), 406);
    encoded[9] = 2;
    accepted   = Utl_Protobuf_ReadVarint(&value, &consumed, encoded, width);
    Check(!accepted && (UINT64_MAX == value), 407);
    encoded[9] = 0x81;
    accepted   = Utl_Protobuf_ReadVarint(&value, &consumed, encoded, width);
    Check(!accepted, 408);
    encoded[0] = 0x80;
    encoded[1] = 0;
    accepted   = Utl_Protobuf_ReadVarint(&value, &consumed, encoded, 2);
    Check(accepted && (0 == value) && (2 == consumed), 409);
    width = Utl_Protobuf_WriteVarint(150, encoded, sizeof(encoded));
    Check((2 == width) && (0x96 == encoded[0]) && (1 == encoded[1]), 410);

    accepted = Utl_Protobuf_ReadField(&field, c_protoBytes, sizeof(c_protoBytes));
    Check(accepted && (2 == field.number) && (UTL_PROTOBUF_BYTES == field.wireType), 411);
    Check((3 == field.byteCount) && (sizeof(c_protoBytes) == field.encodedByteCount), 412);
    width = Utl_Protobuf_WriteField(&field, encoded, sizeof(encoded));
    equal = EqualBytes(encoded, c_protoBytes, sizeof(c_protoBytes));
    Check(equal && (sizeof(c_protoBytes) == width), 413);
    accepted = Utl_Protobuf_ReadField(&field, c_protoBadLength, sizeof(c_protoBadLength));
    Check(!accepted && (2 == field.number), 414);
    accepted = Utl_Protobuf_ReadField(&field, c_protoBadTag, sizeof(c_protoBadTag));
    Check(!accepted, 415);
    encoded[0] = 0;
    accepted   = Utl_Protobuf_ReadField(&field, encoded, 1);
    Check(!accepted, 416);
    encoded[0] = 0x0F;
    accepted   = Utl_Protobuf_ReadField(&field, encoded, 1);
    Check(!accepted, 417);

    field.number       = UTL_PROTOBUF_MAX_FIELD_NUMBER;
    field.integerValue = UINT64_MAX;
    field.wireType     = UTL_PROTOBUF_FIXED64;
    width              = Utl_Protobuf_WriteField(&field, encoded, sizeof(encoded));
    accepted           = Utl_Protobuf_ReadField(&decoded, encoded, width);
    Check(accepted && (UINT64_MAX == decoded.integerValue), 418);
    Check(field.number == decoded.number, 419);
    field.wireType = UTL_PROTOBUF_FIXED32;
    width          = Utl_Protobuf_WriteField(&field, encoded, sizeof(encoded));
    Check(0 == width, 420);
    field.integerValue = UINT32_MAX;
    width              = Utl_Protobuf_WriteField(&field, encoded, sizeof(encoded));
    accepted           = Utl_Protobuf_ReadField(&decoded, encoded, width);
    Check(accepted && (UINT32_MAX == decoded.integerValue), 421);
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
        Check(0 == consumed, 422);
        ++prefix;
        if (prefix == width)
        {
            break;
        }
    }
    index = 0;
    for (;;)
    {
        Check(FORMAT_TEST_SENTINEL == encoded[index], 423);
        ++index;
        if (sizeof(encoded) == index)
        {
            break;
        }
    }

    accepted = Utl_Protobuf_SkipField(&consumed, c_protoGroups, sizeof(c_protoGroups));
    Check(accepted && (sizeof(c_protoGroups) == consumed), 424);
    prefix = 0;
    for (;;)
    {
        accepted = Utl_Protobuf_SkipField(&consumed, c_protoGroups, prefix);
        Check(!accepted && (sizeof(c_protoGroups) == consumed), 425);
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
    encoded[sizeof(c_protoGroups) - 1] = 0x4C;
    accepted                           = Utl_Protobuf_SkipField(&consumed, encoded, sizeof(c_protoGroups));
    Check(!accepted, 426);
    index = 0;
    for (;;)
    {
        encoded[index]                                = 0x0B;
        encoded[UTL_PROTOBUF_MAX_GROUP_DEPTH + index] = 0x0C;
        ++index;
        if (UTL_PROTOBUF_MAX_GROUP_DEPTH == index)
        {
            break;
        }
    }
    accepted = Utl_Protobuf_SkipField(&consumed, encoded, UTL_PROTOBUF_MAX_GROUP_DEPTH * 2);
    Check(accepted, 427);
    encoded[UTL_PROTOBUF_MAX_GROUP_DEPTH] = 0x0B;
    accepted = Utl_Protobuf_SkipField(&consumed, encoded, UTL_PROTOBUF_MAX_GROUP_DEPTH * 2);
    Check(!accepted, 428);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Runs wire-format checks using a linker-selected entry without CRT startup.
 * \par Details: Returning is deliberately avoided; ExitProcess owns process termination.
 */
void WinMainStartup(void)
{
    DWORD written;

    CheckMp4();
    CheckVints();
    CheckMatroska();
    CheckProtobuf();
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), c_success, sizeof(c_success) - 1, &written, NULL);
    ExitProcess(0);
}
