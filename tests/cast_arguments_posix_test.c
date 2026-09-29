/*=================================================================================================
** 1. References
**===============================================================================================*/

/** POSIX argv records are independently delimited; quotes and whitespace are literal data. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include <stdint.h>
#include "os/os_process.h"
#include "utl/utl_arguments.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_SENTINEL (0xA55A)
#define TEST_CAPACITY (16)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static const uint16_t               c_program[] = {'p'};
static const uint16_t               c_literal[] = {'"', 'a', ' ', 'b', '\\', '"', '\t'};
static const uint16_t               c_unicode[] = {0xD83D, 0xDE00, 0xD800};
static const Utl_Arguments_Record_t c_records[] = {{c_program, sizeof(c_program) / sizeof(c_program[0])},
                                                   {c_literal, sizeof(c_literal) / sizeof(c_literal[0])},
                                                   {c_program, 0},
                                                   {c_unicode, sizeof(c_unicode) / sizeof(c_unicode[0])}};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Checks one record at one exact or insufficient output capacity.
 *
 * \param[in] arguments   Initialized record sequence
 * \param[in] argument    Selected record ordinal
 * \param[in] capacity    Capacity to exercise
 *
 * \retval    None
 */
static FORCE_INLINE void CheckCapacity(const Utl_Arguments_t* arguments, uint32_t argument, uint32_t capacity);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Preservation is checked over the complete sentinel buffer, including empty records.
 */
static FORCE_INLINE void CheckCapacity(const Utl_Arguments_t* arguments, uint32_t argument, uint32_t capacity)
{
    uint16_t output[TEST_CAPACITY];
    uint32_t count;
    uint32_t index;
    bool_t   found;
    for (index = 0; index < TEST_CAPACITY; ++index)
    {
        output[index] = TEST_SENTINEL;
    }
    count = TEST_SENTINEL;
    found = Utl_Arguments_Read(&count, output, capacity, arguments, argument);
    if (capacity < c_records[argument].unitCount)
    {
        LOGASSERT(!found);
        LOGASSERT(TEST_SENTINEL == count);
        for (index = 0; index < TEST_CAPACITY; ++index)
        {
            LOGASSERT(TEST_SENTINEL == output[index]);
        }
    }
    else
    {
        LOGASSERT(found);
        LOGASSERT(c_records[argument].unitCount == count);
        for (index = 0; index < count; ++index)
        {
            LOGASSERT(c_records[argument].units[index] == output[index]);
        }
        LOGASSERT(TEST_SENTINEL == output[count]);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Every insufficient capacity must preserve storage and count. Exact-capacity reads
 *  must preserve quotes, whitespace, empty records and uninterpreted UTF-16 units.
 */
uint32_t OS_Application_Main(void)
{
    Utl_Arguments_t arguments;
    uint32_t        argument;
    uint32_t        capacity;
    uint16_t        output[TEST_CAPACITY];
    uint32_t        count;
    bool_t          found;

    OS_Process_SetInteractive(FALSE);
    arguments.records     = c_records;
    arguments.recordCount = sizeof(c_records) / sizeof(c_records[0]);
    for (argument = 0; argument < arguments.recordCount; ++argument)
    {
        for (capacity = 0; capacity <= c_records[argument].unitCount; ++capacity)
        {
            CheckCapacity(&arguments, argument, capacity);
        }
    }
    count     = TEST_SENTINEL;
    output[0] = TEST_SENTINEL;
    found     = Utl_Arguments_Read(&count, output, TEST_CAPACITY, &arguments, arguments.recordCount);
    LOGASSERT(!found);
    LOGASSERT(TEST_SENTINEL == count);
    LOGASSERT(TEST_SENTINEL == output[0]);
    found = Utl_Arguments_Read(&count, output, TEST_CAPACITY, &arguments, UINT32_MAX);
    LOGASSERT(!found);
    LOGASSERT(TEST_SENTINEL == count);
    arguments.records     = NULL;
    arguments.recordCount = 0;
    found                 = Utl_Arguments_Read(&count, output, TEST_CAPACITY, &arguments, 0);
    LOGASSERT(!found);
    LOGASSERT(TEST_SENTINEL == count);
    LOGASSERT(TEST_SENTINEL == output[0]);
    return 0;
}
