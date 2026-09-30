#ifndef OS_WIN32_TEXT_H
#define OS_WIN32_TEXT_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Native Win32 text uses the selected TCHAR encoding. Fixed UTF-16 belongs at
 *  interfaces such as Media Foundation, DirectWrite and the OS argument reader.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h> // IWYU pragma: keep
#include <winnt.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

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
 * \brief Reads a native Win32 string into caller-owned UTF-16 storage.
 *
 * \par Requirements:
 *  - 001 Pointers shall be valid and disjoint; capacity shall include a terminator.
 *  - 002 Native conversion calls shall be bounded to INT_MAX units.
 *  - 003 Input shall be terminated within INT_MAX units.
 *  - 004 ANSI builds shall decode the active Windows code page without accepting malformed input.
 *  - 005 No allocation shall occur. Failure shall leave an empty terminated output.
 *
 * \param[out] output     UTF-16 destination
 * \param[in]  capacity   Destination units including its terminator
 * \param[in]  text       Terminated native text
 *
 * \retval TRUE    Complete terminated text was written.
 * \retval FALSE   The encoding or destination capacity was unsuitable.
 */
bool_t OS_Text_ReadNative(uint16_t* output,
                         uint32_t capacity,
                         LPCTSTR  text);

/*************************************************************************************************/
/**
 * \brief Writes a UTF-16 string into caller-owned native Win32 storage.
 *
 * \par Requirements:
 *  - 001 Pointers shall be valid and disjoint; capacity shall include a terminator.
 *  - 002 Native conversion calls shall be bounded to INT_MAX units.
 *  - 003 Input shall be terminated within INT_MAX units.
 *  - 004 ANSI builds shall reject best-fit and default-character substitutions.
 *  - 005 No allocation shall occur. Failure shall leave an empty terminated output.
 *
 * \param[out] output     Native destination
 * \param[in]  capacity   Destination TCHAR units including its terminator
 * \param[in]  text       Terminated UTF-16 text
 *
 * \retval TRUE    Complete terminated text was written.
 * \retval FALSE   Text was unrepresentable or exceeded the destination.
 */
bool_t OS_Text_WriteNative(LPTSTR          output,
                          uint32_t        capacity,
                          const uint16_t* text);

#endif /* OS_WIN32_TEXT_H */
