/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Microsoft __fastfail intrinsic:
 * https://learn.microsoft.com/en-us/cpp/intrinsics/fastfail
 * Stack-local diagnostic storage avoids a shared mutable failure record.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_assert.h"

#include <windows.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Diagnostic values retained on the failing thread's stack for crash inspection.
 */
typedef struct OS_AssertContext_s
{
    const char_t* expression; ///< Static failed expression.
    const char_t* file;       ///< Static source filename.
    uint32_t      line;       ///< One-based source line.
} OS_AssertContext_t;

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
 *  Volatile stores retain the caller's context without locks or shared state.
 *  The intrinsic raises a noncontinuable fail-fast exception; no runtime helper is called.
 */
OS_NORETURN void OS_AssertFailure(const char_t* expression, const char_t* file, uint32_t line)
{
    volatile OS_AssertContext_t context;

    context.expression = expression;
    context.file       = file;
    context.line       = line;
    (void)context.line;
#if defined(_MSC_VER)
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
#else
    RaiseFailFastException(NULL, NULL, 0);
    (void)TerminateProcess(GetCurrentProcess(), STATUS_STACK_BUFFER_OVERRUN);
    for (;;)
    {
        /* A failing termination API must never resume the violated invariant. */
    }
#endif
}
