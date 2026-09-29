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
#include "os/os_compiler.h"
#include "os/os_assert.h"
#include "os/os_types.h"
#include <stdint.h>

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
extern FORCE_INLINE bool_t Utl_Arguments_Read(uint32_t*              unitCount,
                          uint16_t*              output,
                          uint32_t               capacity,
                          const Utl_Arguments_t* arguments,
                          uint32_t               argumentIndex)
{
    bool_t                        has_records;
    bool_t                        found;
    bool_t                        is_empty;
    bool_t                        has_storage;
    bool_t                        fits_output;
    Utl_Arguments_Record_t        absent;
    const Utl_Arguments_Record_t* record;
    const uint16_t*               units;
    uint32_t                      count;
    uint32_t                      remaining;

    LOGASSERT(NULL != unitCount);
    LOGASSERT(NULL != output);
    LOGASSERT(NULL != arguments);
    is_empty    = (0 == arguments->recordCount);
    has_storage = (NULL != arguments->records);
    has_records = is_empty || has_storage;
    LOGASSERT(has_records);
    absent.units     = NULL;
    absent.unitCount = 0;
    found            = (argumentIndex < arguments->recordCount);
    record           = found ? &arguments->records[argumentIndex] : &absent;
    units            = record->units;
    count            = record->unitCount;
    fits_output      = (count <= capacity);
    if (found)
    {
        LOGASSERT(NULL != units);
        found = fits_output;
        if (found)
        {
            remaining = count;
            for (;;)
            {
                if (0 == remaining)
                {
                    break;
                }
                --remaining;
                output[remaining] = units[remaining];
            }
            (*unitCount) = count;
        }
    }
    return found;
}
