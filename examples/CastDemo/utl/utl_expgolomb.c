/*=================================================================================================
** 1. References
**===============================================================================================*/

/** See utl_expgolomb.h for the syntax clauses and supported integer domains. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_expgolomb.h"
#include "utl/utl_mpeg4.h"
#include "os/os_assert.h"
#include "os/os_types.h"

#include <stddef.h>
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define BYTE_BIT_COUNT         (8)  ///< Storage bits in one MPEG-4 byte.
#define MAX_PREFIX_BITS        (32) ///< Longest prefix required by either supported 32-bit value domain.
#define MAX_SIGNED_CODE_NUMBER ((uint64_t)UINT32_MAX + 1) ///< se(v) code number for INT32_MIN.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

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
 * \brief Maps a signed value to the nonnegative codeNum defined by H.264 Table 9-3.
 *
 * \par Requirements:
 *  - 001 Mapping shall widen before negation or multiplication.
 *  - 002 No signed arithmetic shall overflow for any int32_t input.
 *
 * \param[in] value  Signed syntax value
 *
 * \retval 0..MAX_SIGNED_CODE_NUMBER  Mapped code number.
 */
static uint64_t GetSignedCodeNumber(int32_t value);

/*************************************************************************************************/
/**
 * \brief Counts the leading zeros and binary suffix for one code number.
 *
 * \par Requirements:
 *  - 001 codeNumber shall not exceed MAX_SIGNED_CODE_NUMBER; misuse shall assert.
 *  - 002 The shift loop shall execute no more than MAX_PREFIX_BITS times.
 *
 * \param[in] codeNumber  Nonnegative mapped syntax value
 *
 * \retval 1..65  Complete codeword length in bits.
 */
static uint32_t GetCodeBitCount(uint64_t codeNumber);

/*************************************************************************************************/
/**
 * \brief Parses a bounded order-zero code number with sticky error propagation.
 *
 * \par Requirements:
 *  - 001 The caller shall provide an initialized reader.
 *  - 002 A prefix exceeding MAX_PREFIX_BITS shall be rejected before a wider suffix read.
 *  - 003 Truncation shall leave reader.valid FALSE and produce zero.
 *
 * \param[in,out] reader  Shared reader advanced through the bounded codeword
 *
 * \retval 0..8589934590  Code number when valid; zero otherwise.
 */
static uint64_t ReadCodeNumber(Utl_Mpeg4_BitReader_t* reader);

/*************************************************************************************************/
/**
 * \brief Emits a mapped code number after checking the entire destination extent.
 *
 * \par Requirements:
 *  - 001 The caller shall provide an initialized writer and a representable code number.
 *  - 002 The prefix and suffix shall be reserved together before any output is modified.
 *  - 003 Writes shall use the shared MPEG-4 bit writer and its counter semantics.
 *
 * \param[in,out] writer      Initialized shared writer or counter
 * \param[in]     codeNumber  Value not exceeding MAX_SIGNED_CODE_NUMBER
 *
 * \retval None No value is returned.
 */
static void WriteCodeNumber(Utl_Mpeg4_BitWriter_t* writer, uint64_t codeNumber);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Widening to int64_t makes the magnitude of INT32_MIN representable.
 */
static uint64_t GetSignedCodeNumber(int32_t value)
{
    uint64_t code_number;

    if (0 < value)
    {
        code_number = ((uint64_t)value * 2) - 1;
    }
    else
    {
        code_number = (uint64_t)(-(int64_t)value) * 2;
    }
    return code_number;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A binary suffix of N bits follows N-1 zero prefix bits. codeNum + 1 is at most 33 bits.
 */
static uint32_t GetCodeBitCount(uint64_t codeNumber)
{
    uint64_t code;
    uint32_t suffix_bits;

    LOGASSERT(MAX_SIGNED_CODE_NUMBER >= codeNumber);
    code = codeNumber + 1;
    for (suffix_bits = 1; 1 < code; ++suffix_bits)
    {
        code >>= 1;
    }
    return (suffix_bits * 2) - 1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The leading one is consumed by the prefix scan. The suffix then contributes prefix_bits bits.
 *  All shifts use uint64_t operands and a count no greater than 32.
 */
static uint64_t ReadCodeNumber(Utl_Mpeg4_BitReader_t* reader)
{
    uint64_t code_number;
    uint32_t prefix_bits;
    uint64_t prefix_bit;
    bool_t   is_prefix_zero;
    bool_t   can_continue;
    bool_t   valid_prefix;
    uint64_t suffix;

    code_number = 0;
    for (prefix_bits = 0; prefix_bits <= MAX_PREFIX_BITS; ++prefix_bits)
    {
        prefix_bit     = Utl_Mpeg4_ReadBits(reader, 1);
        is_prefix_zero = (0 == prefix_bit);
        can_continue   = reader->valid && is_prefix_zero;
        if (FALSE == can_continue)
        {
            break;
        }
    }

    valid_prefix  = (prefix_bits <= MAX_PREFIX_BITS);
    reader->valid = reader->valid && valid_prefix;
    if (reader->valid)
    {
        suffix = 0;
        if (0 < prefix_bits)
        {
            suffix = Utl_Mpeg4_ReadBits(reader, prefix_bits);
        }
        if (reader->valid)
        {
            code_number = (((uint64_t)1 << prefix_bits) - 1) + suffix;
        }
    }
    return code_number;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Whole-codeword reservation prevents a prefix-only write on capacity failure. The longest
 *  codeword has 65 bits, so its zero prefix and binary suffix are emitted separately.
 */
static void WriteCodeNumber(Utl_Mpeg4_BitWriter_t* writer, uint64_t codeNumber)
{
    uint32_t             bit_count;
    uint64_t             remaining;
    bool_t               has_capacity;
    Utl_Mpeg4_BitField_t field;

    LOGASSERT(writer->position <= (uint64_t)writer->capacity * BYTE_BIT_COUNT);
    bit_count     = GetCodeBitCount(codeNumber);
    remaining     = ((uint64_t)writer->capacity * BYTE_BIT_COUNT) - writer->position;
    has_capacity  = (bit_count <= remaining);
    writer->valid = writer->valid && has_capacity;
    if (writer->valid)
    {
        field.value    = 0;
        field.bitCount = bit_count / 2;
        if (0 < field.bitCount)
        {
            Utl_Mpeg4_WriteBits(writer, field);
        }
        field.value = codeNumber + 1;
        ++field.bitCount;
        Utl_Mpeg4_WriteBits(writer, field);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_ExpGolomb_GetUnsignedBitCount(uint32_t value)
{
    return GetCodeBitCount(value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_ExpGolomb_GetSignedBitCount(int32_t value)
{
    return GetCodeBitCount(GetSignedCodeNumber(value));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A syntactically complete code can exceed the public value domain. That is an ordinary
 *  invalid-input result, independent of programmer misuse of the initialized reader.
 */
uint32_t Utl_ExpGolomb_ReadUnsigned(Utl_Mpeg4_BitReader_t* reader)
{
    uint64_t code_number;
    bool_t   is_representable;
    uint32_t value;

    LOGASSERT(NULL != reader);
    code_number      = ReadCodeNumber(reader);
    is_representable = (UINT32_MAX >= code_number);
    reader->valid    = reader->valid && is_representable;
    value            = 0;
    if (reader->valid)
    {
        value = (uint32_t)code_number;
    }
    return value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Odd code numbers map to positive values. Even numbers map to nonpositive values.
 *  The bounded parser's code domain fits int64_t before the final int32_t range check.
 */
int32_t Utl_ExpGolomb_ReadSigned(Utl_Mpeg4_BitReader_t* reader)
{
    uint64_t code_number;
    int64_t  mapped_value;
    bool_t   fits_minimum;
    bool_t   fits_maximum;
    bool_t   is_representable;
    int32_t  value;

    LOGASSERT(NULL != reader);
    code_number  = ReadCodeNumber(reader);
    mapped_value = -(int64_t)(code_number / 2);
    if (code_number & 1)
    {
        mapped_value = (int64_t)((code_number + 1) / 2);
    }
    fits_minimum     = (INT32_MIN <= mapped_value);
    fits_maximum     = (INT32_MAX >= mapped_value);
    is_representable = fits_minimum && fits_maximum;
    reader->valid    = reader->valid && is_representable;
    value            = 0;
    if (reader->valid)
    {
        value = (int32_t)mapped_value;
    }
    return value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Utl_ExpGolomb_WriteUnsigned(Utl_Mpeg4_BitWriter_t* writer, uint32_t value)
{
    LOGASSERT(NULL != writer);
    WriteCodeNumber(writer, value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Utl_ExpGolomb_WriteSigned(Utl_Mpeg4_BitWriter_t* writer, int32_t value)
{
    LOGASSERT(NULL != writer);
    WriteCodeNumber(writer, GetSignedCodeNumber(value));
}
