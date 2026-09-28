/*=================================================================================================
** 1. References
**===============================================================================================*/

/** POSIX exec argument-vector boundaries are already established by the caller.
 * https://pubs.opengroup.org/onlinepubs/9799919799/functions/exec.html
 * https://man7.org/linux/man-pages/man2/execve.2.html
 * This reader operates on the OS boundary's counted Unicode records; it never reparses shell syntax. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_arguments.h"
#include "os/os_assert.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

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

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Each source record is one complete argument. A descending copy preserves exact
 *  units and avoids the compiler replacing a forward loop with a CRT memcpy call.
 */
bool_t Utl_Arguments_Read(uint32_t*              unitCount,
                          uint16_t*              output,
                          uint32_t               capacity,
                          const Utl_Arguments_t* arguments,
                          uint32_t               argumentIndex)
{
    bool_t                        has_records;
    bool_t                        found;
    const Utl_Arguments_Record_t* record;
    uint32_t                      remaining;

    LOGASSERT(NULL != unitCount);
    LOGASSERT(NULL != output);
    LOGASSERT(NULL != arguments);
    has_records = (0 == arguments->recordCount) || (NULL != arguments->records);
    LOGASSERT(has_records);
    found = (argumentIndex < arguments->recordCount);
    if (found)
    {
        record = &arguments->records[argumentIndex];
        LOGASSERT(NULL != record->units);
        found = (record->unitCount <= capacity);
        if (found)
        {
            remaining = record->unitCount;
            for (;;)
            {
                if (0 == remaining)
                {
                    break;
                }
                --remaining;
                output[remaining] = record->units[remaining];
            }
            (*unitCount) = record->unitCount;
        }
    }
    return found;
}
