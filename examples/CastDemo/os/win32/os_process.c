/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Windows process backend. Native command-line storage is UTF-16; this source is
 * compiled with UNICODE independently of application/UI character selection.
 * https://learn.microsoft.com/en-us/windows/win32/api/processenv/nf-processenv-getcommandlinew */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_process.h"
#include "os/os_assert.h"
#include "utl/utl_unicode.h"
#include "utl/utl_arguments.h"

#include <windows.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define PROCESS_COMMAND_LINE_CAPACITY (32768) ///< Windows command-line units including the terminator.

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
 *  GetCommandLine returns OS-owned storage. Its native encoding is confined to this
 *  backend; utility code receives counted, native-order UTF-16 units.
 */
bool_t OS_Process_ReadArgument(uint32_t* unitCount, uint16_t* output, uint32_t capacity, uint32_t argumentIndex)
{
    LPCTSTR                command;
    bool_t                 has_text;
    bool_t                 found;
    uint32_t               count;
    Utl_Arguments_Record_t record;
    Utl_Arguments_t        arguments;

    LOGASSERT(NULL != unitCount);
    LOGASSERT(NULL != output);
    command  = GetCommandLine();
    has_text = (NULL != command);
    found    = FALSE;
    if (has_text)
    {
        count = Utl_Unicode_GetUtf16StringUnitCount((const uint16_t*)command, PROCESS_COMMAND_LINE_CAPACITY);
        if (UTL_UNICODE_NO_TERMINATOR != count)
        {
            record.units          = (const uint16_t*)command;
            record.unitCount      = count;
            arguments.records     = &record;
            arguments.recordCount = 1;
            found                 = Utl_Arguments_Read(unitCount, output, capacity, &arguments, argumentIndex);
        }
    }
    return found;
}
/*************************************************************************************************/
/**
 * \par Details:
 *  Only the noninteractive error bits are changed; existing unrelated settings are
 *  retained. Startup is single-threaded, before any application worker exists.
 */
void OS_Process_SetInteractive(bool_t interactive)
{
    UINT mode;
    UINT mask;

    mode = GetErrorMode();
    mask = SEM_NOGPFAULTERRORBOX | SEM_FAILCRITICALERRORS;
    if (interactive)
    {
        mode &= ~mask;
    }
    else
    {
        mode |= mask;
    }
    (void)SetErrorMode(mode);
}
/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
OS_NORETURN void OS_Process_Exit(uint32_t status)
{
    ExitProcess(status);
}
