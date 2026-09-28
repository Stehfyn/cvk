/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Native MSVC x64 compiler-memory ABI verification, independently of public wrappers. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_assert.h"
#include "os/os_process.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_CAPACITY   (129)
#define TEST_HIGH_VALUE (0x1A3)
#define TEST_LOW_BYTE   (0xA3)

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
 * \brief Compares two extents through the compiler's x64 ABI.
 *
 * \par Details:
 *  These required symbol spellings are declared only in this native ABI test.
 *
 * \par Requirements:
 *  - 001 The linked owned adapter shall preserve native arguments and results.
 *
 * \param[in] first   Readable first extent
 * \param[in] second   Readable second extent
 * \param[in] byteCount   Readable bytes per input
 *
 * \retval -255..255   First unsigned-byte difference.
 */
int32_t memcmp(const void* first, const void* second, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Copies disjoint extents through the compiler's x64 ABI.
 *
 * \par Details:
 *  These required symbol spellings are declared only in this native ABI test.
 *
 * \par Requirements:
 *  - 001 The linked owned adapter shall preserve native arguments and results.
 *
 * \param[out] output   Writable extent
 * \param[in] input   Disjoint readable extent
 * \param[in] byteCount   Exact bytes to copy
 *
 * \retval output   Original destination.
 */
void* memcpy(void* output, const void* input, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Moves possibly overlapping extents through the compiler's x64 ABI.
 *
 * \par Details:
 *  These required symbol spellings are declared only in this native ABI test.
 *
 * \par Requirements:
 *  - 001 The linked owned adapter shall preserve native arguments and results.
 *
 * \param[out] output   Writable extent
 * \param[in] input   Readable extent
 * \param[in] byteCount   Exact bytes to move
 *
 * \retval output   Original destination.
 */
void* memmove(void* output, const void* input, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Fills an extent through the compiler's x64 ABI.
 *
 * \par Details:
 *  These required symbol spellings are declared only in this native ABI test.
 *
 * \par Requirements:
 *  - 001 The linked owned adapter shall preserve native arguments and results.
 *
 * \param[out] output   Writable extent
 * \param[in] value   Int value converted to an unsigned byte
 * \param[in] byteCount   Exact bytes to fill
 *
 * \retval output   Original destination.
 */
void* memset(void* output, int32_t value, uint64_t byteCount);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Volatile function pointers force calls through the ABI adapter symbols rather than
 *  compiler intrinsic substitution. The target's native size extent is explicitly uint64_t.
 */
uint32_t OS_Application_Main(void)
{
    int32_t  (*volatile compare_bytes)(const void*, const void*, uint64_t);
    void*    (*volatile copy_bytes)(void*, const void*, uint64_t);
    void*    (*volatile move_bytes)(void*, const void*, uint64_t);
    void*    (*volatile set_bytes)(void*, int32_t, uint64_t);
    byte_t   input[TEST_CAPACITY];
    byte_t   output[TEST_CAPACITY];
    uint32_t index;
    void*    returned;
    int32_t  comparison;

    OS_Process_SetInteractive(FALSE);
    compare_bytes = memcmp;
    copy_bytes    = memcpy;
    move_bytes    = memmove;
    set_bytes     = memset;
    for (index = 0; index < TEST_CAPACITY; ++index)
    {
        input[index] = (byte_t)index;
    }
    returned = copy_bytes(output, input, TEST_CAPACITY);
    LOGASSERT(output == returned);
    for (index = 0; index < TEST_CAPACITY; ++index)
    {
        LOGASSERT((byte_t)index == output[index]);
    }
    comparison = compare_bytes(input, output, TEST_CAPACITY);
    LOGASSERT(0 == comparison);
    returned = set_bytes(output, -1, TEST_CAPACITY);
    LOGASSERT(output == returned);
    for (index = 0; index < TEST_CAPACITY; ++index)
    {
        LOGASSERT(UINT8_MAX == output[index]);
    }
    comparison = compare_bytes(input, output, TEST_CAPACITY);
    LOGASSERT(-255 == comparison);
    comparison = compare_bytes(output, input, TEST_CAPACITY);
    LOGASSERT(255 == comparison);
    returned = set_bytes(output, TEST_HIGH_VALUE, TEST_CAPACITY);
    LOGASSERT(output == returned);
    for (index = 0; index < TEST_CAPACITY; ++index)
    {
        LOGASSERT(TEST_LOW_BYTE == output[index]);
    }
    returned = move_bytes(input + 1, input, TEST_CAPACITY - 1);
    LOGASSERT((input + 1) == returned);
    for (index = 1; index < TEST_CAPACITY; ++index)
    {
        LOGASSERT((byte_t)(index - 1) == input[index]);
    }
    returned = move_bytes(input, input + 1, TEST_CAPACITY - 1);
    LOGASSERT(input == returned);
    for (index = 0; index < (TEST_CAPACITY - 1); ++index)
    {
        LOGASSERT((byte_t)index == input[index]);
    }

    return 0;
}
