#ifndef OS_CONSOLE_NATIVE_H
#define OS_CONSOLE_NATIVE_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Documented console entry absent from the Windows SDK declarations/import library:
 * https://learn.microsoft.com/en-us/windows/console/readconsoleinputex
 * The build supplies its import library and selects the A/W spelling for the
 * generic ReadConsoleInputEx declaration consistently with the target text mode.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define OS_CONSOLE_READ_NOWAIT (0x0002) ///< Documented immediate-return read flag.

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
 * \brief Imports the native console read entry with its documented ABI.
 *
 * \param[in]  input      Console input handle
 * \param[out] records    Writable native records
 * \param[in]  capacity   Number of writable records
 * \param[out] count      Number of records published
 * \param[in]  flags      Documented console read flags
 *
 * \retval     nonzero   Native operation succeeded.
 * \retval     zero      GetLastError reports native failure.
 */
__declspec(dllimport) BOOL WINAPI
ReadConsoleInputEx(HANDLE input, PINPUT_RECORD records, DWORD capacity, LPDWORD count, USHORT flags);

#endif /* OS_CONSOLE_NATIVE_H */
