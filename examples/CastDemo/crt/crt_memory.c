/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Hand-written counted memory operations. No allocation, initialization resource,
 * system runtime library, locale or hidden payload ownership is involved.
 * Public interfaces use explicit-width counts; the build selects the native ABI.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "crt/crt_memory.h"
#include "os/os_compiler.h"
#include "crt/crt_memory_backend.h"
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
 *  The selected backend compares only bytes within the supplied extent.
 */
extern FORCE_INLINE int32_t CRT_Memory_Compare(const void* first, const void* second, uint64_t byteCount)
{
    LOGASSERT(NULL != first);
    LOGASSERT(NULL != second);

    return CRT_Memory_CompareCore(first, second, byteCount);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Assertions precede the selected backend; the backend owns native implementation details.
 */
extern FORCE_INLINE void CRT_Memory_Copy(void* output, const void* input, uint64_t byteCount)
{
    LOGASSERT(NULL != output);
    LOGASSERT(NULL != input);

    (void)CRT_Memory_CopyCore(output, input, byteCount);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The selected backend chooses a safe traversal order using the target address model.
 */
extern FORCE_INLINE void CRT_Memory_Move(void* output, const void* input, uint64_t byteCount)
{
    LOGASSERT(NULL != output);
    LOGASSERT(NULL != input);

    (void)CRT_Memory_MoveCore(output, input, byteCount);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The selected backend expands the byte value without accessing beyond the output extent.
 */
extern FORCE_INLINE void CRT_Memory_Set(void* output, byte_t value, uint64_t byteCount)
{
    LOGASSERT(NULL != output);

    (void)CRT_Memory_SetCore(output, value, byteCount);
}
