/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Independent expected-value checks for the hand-written CRT memory library. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "crt/crt_memory.h"
#include "crt/crt_format.h"
#include "os/os_types.h"
#include <stdint.h>
#include "os/os_assert.h"
#include "os/os_process.h"
#include "os/os_compiler.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_LENGTH             (129)
#define TEST_ALIGNMENT          (16)
#define TEST_MOVE_OFFSETS       (33)
#define TEST_CAPACITY           (TEST_LENGTH + TEST_MOVE_OFFSETS)
#define TEST_SENTINEL           (0xDA)
#define TEST_VALUE_STEP         (51)
#define TEST_PATTERN_MULTIPLIER (71)
#define TEST_PATTERN_DIVISOR    (7)
#define TEST_PATTERN_BIAS       (193)
#define TEST_FORMAT_CAPACITY     (80)
#define TEST_FORMAT_HEX          (0x12ABUL)
#define TEST_FORMAT_POSITIVE     (42ULL)
#define TEST_FORMAT_NEGATIVE     (-42LL)
#define TEST_FORMAT_FRACTION     (1.25)

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
 * \brief Derives an independent nonconstant byte pattern.
 *
 * \par Requirements:
 *  - 001 Failed expectations shall use the always-active production assertion.
 *
 * \param[in] index   Bounded fixture index
 *
 * \retval    0..255   Fixture byte value.
 */
static FORCE_INLINE byte_t Pattern(uint32_t index);

/*************************************************************************************************/
/**
 * \brief Checks one copy placement and preservation of the source and surrounding bytes.
 *
 * \par Requirements:
 *  - 001 Both arrays shall provide TEST_CAPACITY bytes; the selected spans shall fit.
 *
 * \param[in]     input          Independent source pattern
 * \param[in,out] output         Destination fixture
 * \param[in]     length         Selected byte count
 * \param[in]     input_offset   Source placement
 * \param[in]     output_offset  Destination placement
 *
 * \retval        None
 */
static FORCE_INLINE void CheckCopy(const byte_t* input,
                                   byte_t*       output,
                                   uint32_t      length,
                                   uint32_t      input_offset,
                                   uint32_t      output_offset);

/*************************************************************************************************/
/**
 * \brief Checks one fill placement and preservation of surrounding bytes.
 *
 * \par Requirements:
 *  - 001 The destination shall provide TEST_CAPACITY bytes; the selected span shall fit.
 *
 * \param[in,out] output         Destination fixture
 * \param[in]     length         Selected byte count
 * \param[in]     output_offset  Destination placement
 * \param[in]     value          Fill byte
 *
 * \retval        None
 */
static FORCE_INLINE void CheckSet(byte_t* output, uint32_t length, uint32_t output_offset, byte_t value);

/*************************************************************************************************/
/**
 * \brief Checks exact extents and all small source/destination alignments.
 *
 * \par Requirements:
 *  - 001 Failed expectations shall use the always-active production assertion.
 *
 * \retval None
 */
static FORCE_INLINE void CheckCopyAndSet(void);

static FORCE_INLINE void CheckFormat(void);

/*************************************************************************************************/
/**
 * \brief Checks equal, disjoint and both overlapping pointer orders.
 *
 * \par Requirements:
 *  - 001 Failed expectations shall use the always-active production assertion.
 *
 * \retval None
 */
static FORCE_INLINE void CheckMove(void);

/*************************************************************************************************/
/**
 * \brief Checks the first differing unsigned byte at every position.
 *
 * \par Requirements:
 *  - 001 Failed expectations shall use the always-active production assertion.
 *
 * \retval None
 */
static FORCE_INLINE void CheckCompare(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Vary adjacent bytes and high bits so byte order and unsigned comparisons matter.
 */
static FORCE_INLINE byte_t Pattern(uint32_t index)
{
    return (byte_t)(((index * TEST_PATTERN_MULTIPLIER) + (index / TEST_PATTERN_DIVISOR) + TEST_PATTERN_BIAS) &
                    UINT8_MAX);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Expectations are derived from the original arithmetic pattern, not the copied output.
 */
static FORCE_INLINE void CheckCopy(const byte_t* input,
                                   byte_t*       output,
                                   uint32_t      length,
                                   uint32_t      input_offset,
                                   uint32_t      output_offset)
{
    uint32_t index;
    bool_t   after_start;
    bool_t   before_end;
    bool_t   in_output;
    byte_t   expected;

    for (index = 0; index < TEST_CAPACITY; ++index)
    {
        output[index] = TEST_SENTINEL;
    }
    CRT_Memory_Copy(&output[output_offset], &input[input_offset], length);
    for (index = 0; index < TEST_CAPACITY; ++index)
    {
        after_start = (output_offset <= index);
        before_end  = ((output_offset + length) > index);
        in_output   = after_start && before_end;
        expected    = TEST_SENTINEL;
        if (in_output)
        {
            expected = Pattern(input_offset + index - output_offset);
        }
        LOGASSERT(expected == output[index]);
        LOGASSERT(Pattern(index) == input[index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every byte outside the selected destination retains its sentinel.
 */
static FORCE_INLINE void CheckSet(byte_t* output, uint32_t length, uint32_t output_offset, byte_t value)
{
    uint32_t index;
    bool_t   after_start;
    bool_t   before_end;
    bool_t   in_output;
    byte_t   expected;

    for (index = 0; index < TEST_CAPACITY; ++index)
    {
        output[index] = TEST_SENTINEL;
    }
    CRT_Memory_Set(&output[output_offset], value, length);
    for (index = 0; index < TEST_CAPACITY; ++index)
    {
        after_start = (output_offset <= index);
        before_end  = ((output_offset + length) > index);
        in_output   = after_start && before_end;
        expected    = TEST_SENTINEL;
        if (in_output)
        {
            expected = value;
        }
        LOGASSERT(expected == output[index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Expected bytes are derived arithmetically rather than copied by the tested
 *  routines. Sentinels detect overwrites; lengths cover all scalar/vector tails.
 */
static FORCE_INLINE void CheckCopyAndSet(void)
{
    uint32_t index;
    byte_t   input[TEST_CAPACITY];
    uint32_t length;
    uint32_t input_offset;
    uint32_t output_offset;
    byte_t   output[TEST_CAPACITY];
    uint32_t value;

    for (index = 0; index < TEST_CAPACITY; ++index)
    {
        input[index] = Pattern(index);
    }
    for (length = 0; length <= TEST_LENGTH; ++length)
    {
        for (input_offset = 0; input_offset < TEST_ALIGNMENT; ++input_offset)
        {
            for (output_offset = 0; output_offset < TEST_ALIGNMENT; ++output_offset)
            {
                CheckCopy(input, output, length, input_offset, output_offset);
            }
        }
        for (output_offset = 0; output_offset < TEST_ALIGNMENT; ++output_offset)
        {
            for (value = 0; value <= UINT8_MAX; value += TEST_VALUE_STEP)
            {
                CheckSet(output, length, output_offset, (byte_t)value);
            }
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every result is compared with the original input's arithmetic fixture, including
 *  bytes outside the destination. Offsets include overlap distances below one vector.
 */
static FORCE_INLINE void CheckMove(void)
{
    uint32_t length;
    uint32_t input_offset;
    uint32_t output_offset;
    uint32_t index;
    byte_t   buffer[TEST_CAPACITY];
    bool_t   in_output;
    byte_t   expected;

    for (length = 0; length <= TEST_LENGTH; ++length)
    {
        for (input_offset = 0; input_offset < TEST_MOVE_OFFSETS; ++input_offset)
        {
            for (output_offset = 0; output_offset < TEST_MOVE_OFFSETS; ++output_offset)
            {
                for (index = 0; index < TEST_CAPACITY; ++index)
                {
                    buffer[index] = Pattern(index);
                }
                CRT_Memory_Move(&buffer[output_offset], &buffer[input_offset], length);
                for (index = 0; index < TEST_CAPACITY; ++index)
                {
                    in_output = (index >= output_offset) && (index < (output_offset + length));
                    expected  = Pattern(index);
                    if (in_output)
                    {
                        expected = Pattern(input_offset + index - output_offset);
                    }
                    LOGASSERT(expected == buffer[index]);
                }
            }
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each mismatch is introduced into an independently initialized equal pair.
 *  Later differences have the opposite sign so returning a later match is detected.
 */
static FORCE_INLINE void CheckCompare(void)
{
    uint32_t length;
    uint32_t index;
    byte_t   first[TEST_CAPACITY];
    byte_t   second[TEST_CAPACITY];
    int32_t  comparison;
    uint32_t different;

    for (length = 0; length <= TEST_LENGTH; ++length)
    {
        for (index = 0; index < TEST_CAPACITY; ++index)
        {
            first[index]  = Pattern(index);
            second[index] = Pattern(index);
        }
        comparison = CRT_Memory_Compare(first, second, length);
        LOGASSERT(0 == comparison);
        for (different = 0; different < length; ++different)
        {
            first[different]  = 0;
            second[different] = UINT8_MAX;
            if ((different + 1) < length)
            {
                first[different + 1]  = UINT8_MAX;
                second[different + 1] = 0;
            }
            comparison = CRT_Memory_Compare(first, second, length);
            LOGASSERT(-255 == comparison);
            comparison = CRT_Memory_Compare(second, first, length);
            LOGASSERT(255 == comparison);
            first[different]  = Pattern(different);
            second[different] = Pattern(different);
            if ((different + 1) < length)
            {
                first[different + 1]  = Pattern(different + 1);
                second[different + 1] = Pattern(different + 1);
            }
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Checks bounded diagnostics and the numeric formats used by CastDemo. */
static FORCE_INLINE void CheckFormat(void)
{
    char_t output[TEST_FORMAT_CAPACITY];
    bool_t complete;
    int32_t comparison;

    complete = CRT_Format_Write(output, (uint32_t)sizeof(output), "%.*s %08lX %llu %lld %.1f %.2f %%",
                                3, "abcdef", TEST_FORMAT_HEX, TEST_FORMAT_POSITIVE,
                                TEST_FORMAT_NEGATIVE, TEST_FORMAT_FRACTION, TEST_FORMAT_FRACTION);
    LOGASSERT(complete);
    comparison = CRT_Memory_Compare(output, "abc 000012AB 42 -42 1.3 1.25 %",
                                    sizeof("abc 000012AB 42 -42 1.3 1.25 %"));
    LOGASSERT(0 == comparison);

    complete = CRT_Format_Copy(output, 4U, "longer");
    LOGASSERT(!complete);
    comparison = CRT_Memory_Compare(output, "lon", sizeof("lon"));
    LOGASSERT(0 == comparison);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All fixtures use fixed caller-owned storage and the selected native runtime backend.
 */
uint32_t OS_Application_Main(void)
{
    OS_Process_SetInteractive(FALSE);
    CheckCopyAndSet();
    CheckMove();
    CheckCompare();
    CheckFormat();

    return 0;
}
