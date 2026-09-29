#ifndef VK_DIAGNOSTIC_H
#define VK_DIAGNOSTIC_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Byte-oriented, allocation-free diagnostic output for CVK's generated implementation. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <stdint.h>

#ifdef _MSC_VER
#define VK_DIAGNOSTIC_NORETURN __declspec(noreturn)
#else
#define VK_DIAGNOSTIC_NORETURN __attribute__((noreturn))
#endif

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Writes a bounded terminated diagnostic byte string to standard error.
 * \param[in] text  Source string, or NULL for no output.
 * \retval    None
 */
void cvkDiagnosticWrite(const char* text);

/*************************************************************************************************/
/**
 * \brief Writes an unsigned value as eight hexadecimal digits.
 * \param[in] value  Value to format.
 * \retval    None
 */
void cvkDiagnosticWriteHex(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Ends the process after a fatal diagnostic.
 * \retval    None
 */
VK_DIAGNOSTIC_NORETURN void cvkDiagnosticExit(void);

void scvkDiagnosticWrite(const char* text);
void scvkDiagnosticWriteHex(uint32_t value);
VK_DIAGNOSTIC_NORETURN void scvkDiagnosticExit(void);

#endif
