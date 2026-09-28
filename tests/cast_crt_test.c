/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Independent expected-value checks for the hand-written CRT memory library. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_process.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_LENGTH       (129)
#define TEST_ALIGNMENT    (16)
#define TEST_MOVE_OFFSETS (33)
#define TEST_CAPACITY     (TEST_LENGTH + TEST_MOVE_OFFSETS)
#define TEST_SENTINEL     (0xDA)
#define TEST_VALUE_STEP   (51)

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
static byte_t Pattern(uint32_t index);

/*************************************************************************************************/
/**
 * \brief Checks exact extents and all small source/destination alignments.
 *
 * \par Requirements:
 *  - 001 Failed expectations shall use the always-active production assertion.
 *
 * \retval None
 */
static void CheckCopyAndSet(void);

/*************************************************************************************************/
/**
 * \brief Checks equal, disjoint and both overlapping pointer orders.
 *
 * \par Requirements:
 *  - 001 Failed expectations shall use the always-active production assertion.
 *
 * \retval None
 */
static void CheckMove(void);

/*************************************************************************************************/
/**
 * \brief Checks the first differing unsigned byte at every position.
 *
 * \par Requirements:
 *  - 001 Failed expectations shall use the always-active production assertion.
 *
 * \retval None
 */
static void CheckCompare(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Vary adjacent bytes and high bits so byte order and unsigned comparisons matter.
 */
static byte_t Pattern(uint32_t index)
{
    return (byte_t)(((index * 71) + (index / 7) + 193) & UINT8_MAX);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Expected bytes are derived arithmetically rather than copied by the tested
 *  routines. Sentinels detect overwrites; lengths cover all scalar/vector tails.
 */
static void CheckCopyAndSet(void)
{
    uint32_t index;
    byte_t   input[TEST_CAPACITY];
    uint32_t length;
    uint32_t input_offset;
    uint32_t output_offset;
    byte_t   output[TEST_CAPACITY];
    bool_t   in_output;
    byte_t   expected;
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
                for (index = 0; index < TEST_CAPACITY; ++index)
                {
                    output[index] = TEST_SENTINEL;
                }
                CRT_Memory_Copy(&output[output_offset], &input[input_offset], length);
                for (index = 0; index < TEST_CAPACITY; ++index)
                {
                    in_output = (index >= output_offset) && (index < (output_offset + length));
                    expected  = TEST_SENTINEL;
                    if (in_output)
                    {
                        expected = Pattern(input_offset + index - output_offset);
                    }
                    LOGASSERT(expected == output[index]);
                    LOGASSERT(Pattern(index) == input[index]);
                }
            }
        }
        for (output_offset = 0; output_offset < TEST_ALIGNMENT; ++output_offset)
        {
            for (value = 0; value <= UINT8_MAX; value += TEST_VALUE_STEP)
            {
                for (index = 0; index < TEST_CAPACITY; ++index)
                {
                    output[index] = TEST_SENTINEL;
                }
                CRT_Memory_Set(&output[output_offset], (byte_t)value, length);
                for (index = 0; index < TEST_CAPACITY; ++index)
                {
                    in_output = (index >= output_offset) && (index < (output_offset + length));
                    expected  = TEST_SENTINEL;
                    if (in_output)
                    {
                        expected = (byte_t)value;
                    }
                    LOGASSERT(expected == output[index]);
                }
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
static void CheckMove(void)
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
static void CheckCompare(void)
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

    return 0;
}
