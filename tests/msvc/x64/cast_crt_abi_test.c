/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Native MSVC x64 compiler-memory ABI verification, independently of public wrappers. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include <stdint.h>
#include <string.h>
#include "os/os_process.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_CAPACITY    (129)
#define TEST_HIGH_VALUE  (0x1A3)
#define TEST_LOW_BYTE    (0xA3)
#define TEST_STACK_BYTES (8193)

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
 * \brief Exercises a compiler-probed automatic stack extent.
 *
 * \par Requirements:
 *  - 001 Every byte across multiple pages and the final partial page shall retain its expected value.
 *  - 002 Volatile storage shall require actual reads and writes in the native compiler ABI test.
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckStack(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The local extent requires compiler-emitted stack probing before either byte loop executes.
 */
static FORCE_INLINE void CheckStack(void)
{
    uint32_t        index;
    volatile byte_t stack_bytes[TEST_STACK_BYTES];
    byte_t          value;

    for (index = 0; index < TEST_STACK_BYTES; ++index)
    {
        stack_bytes[index] = (byte_t)index;
    }
    for (index = 0; index < TEST_STACK_BYTES; ++index)
    {
        value = stack_bytes[index];
        LOGASSERT((byte_t)index == value);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Volatile function pointers force calls through the ABI adapter symbols rather than
 *  compiler intrinsic substitution. The target's native size extent is explicitly uint64_t.
 *  The standard header declares the ABI; /NODEFAULTLIB resolves each symbol to the
 *  owned assembly implementation, without linking the system C runtime.
 *  A volatile multi-page local additionally requires the compiler's stack probe;
 *  writes and reads cover every byte, including the final partial page.
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

    CheckStack();

    return 0;
}
