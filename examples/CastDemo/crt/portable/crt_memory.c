/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Counted memory contracts in crt_memory_backend.h; instrumentable flat-address C backend. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "crt/crt_memory_backend.h"
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
 *  Compares counted bytes in lexical order without reading padding beyond the supplied span.
 */
int32_t CRT_Memory_CompareCore(const void* first, const void* second, uint64_t byteCount)
{
    const byte_t* left;
    const byte_t* right;
    int32_t       result;
    uint64_t      index;

    left   = first;
    right  = second;
    result = 0;
    for (index = 0; index < byteCount; ++index)
    {
        result = (int32_t)left[index] - right[index];
        if (0 != result)
        {
            break;
        }
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The freestanding build prevents substitution with a hosted memory-library call.
 */
void* CRT_Memory_CopyCore(void* output, const void* input, uint64_t byteCount)
{
    byte_t*       destination;
    const byte_t* source;
    uint64_t      index;

    destination = output;
    source      = input;
    for (index = 0; index < byteCount; ++index)
    {
        destination[index] = source[index];
    }
    return output;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The selected flat-address target permits uintptr_t ordering without comparing unrelated pointers.
 */
void* CRT_Memory_MoveCore(void* output, const void* input, uint64_t byteCount)
{
    byte_t*       destination;
    const byte_t* source;
    uintptr_t     destination_address;
    uintptr_t     source_address;
    bool_t        copy_forward;
    uint64_t      index;

    destination         = output;
    source              = input;
    destination_address = (uintptr_t)output;
    source_address      = (uintptr_t)input;
    copy_forward        = (destination_address <= source_address);
    if (copy_forward)
    {
        for (index = 0; index < byteCount; ++index)
        {
            destination[index] = source[index];
        }
    }
    else
    {
        for (index = byteCount; 0 < index;)
        {
            --index;
            destination[index] = source[index];
        }
    }
    return output;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Writes each selected byte; the compiler can instrument every access in this C backend.
 */
void* CRT_Memory_SetCore(void* output, byte_t value, uint64_t byteCount)
{
    byte_t*  destination;
    uint64_t index;

    destination = output;
    for (index = 0; index < byteCount; ++index)
    {
        destination[index] = value;
    }
    return output;
}
