#ifndef CAST_OS_ASSERT_H
#define CAST_OS_ASSERT_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Invariant-failure convention follows C:/dev/xml/include/os/os_assert.h.
 *  Assertions remain active in optimized builds and use no CRT or allocation.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#include "os/os_compiler.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define LOGASSERT(expression) ((expression) ? (void)0 : OS_AssertFailure(#expression, __FILE__, __LINE__))

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Preserves diagnostic context on the failing thread's stack and terminates.
 *
 * \par Requirements:
 *  - 001 Invariant failure shall not return, allocate, format text, wait, or call CRT.
 *  - 002 Expression, source file and line shall remain in volatile local storage for a debugger.
 *  - 003 Release builds shall use the same assertion behavior as other builds.
 *
 * \param[in] expression   Static expression text from LOGASSERT
 * \param[in] file         Static source file name from LOGASSERT
 * \param[in] line         One-based source line
 *
 * \retval    None No value is returned.
 */
OS_NORETURN void OS_AssertFailure(const char_t* expression, const char_t* file, uint32_t line);

#endif /* CAST_OS_ASSERT_H */
