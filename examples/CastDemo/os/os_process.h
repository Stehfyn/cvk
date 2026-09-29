#ifndef CAST_OS_PROCESS_H
#define CAST_OS_PROCESS_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Process services expose counted Unicode and fixed-width values.
 * Operating-system APIs and native executable entry points belong to target backends. */

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
 * \brief Reads one process argument into caller-owned native-order UTF-16 units.
 *
 * \par Details:
 *  Index zero selects the program name. No terminator is appended. The caller owns all
 *  output storage; no argv array or command-line conversion allocation is required.
 *
 * \par Requirements:
 *  - 001 Missing output pointers shall assert as developer errors.
 *  - 002 Missing arguments or insufficient capacity shall preserve output and unitCount.
 *  - 003 OS command-line syntax shall be interpreted only by the selected backend and its specification reader.
 *
 * \param[out] unitCount       Non-NULL unit count, disjoint from output
 * \param[out] output          Non-NULL caller-owned units
 * \param[in]  capacity        Available output units
 * \param[in]  argumentIndex   Zero-based argument index
 *
 * \retval     TRUE    The argument was copied.
 * \retval     FALSE   The argument is absent, unavailable or exceeds capacity.
 */
bool_t OS_Process_ReadArgument(uint32_t* unitCount, uint16_t* output, uint32_t capacity, uint32_t argumentIndex);

/*************************************************************************************************/
/**
 * \brief Sets whether process failure may display an interactive OS error dialog.
 *
 * \par Details:
 *  Call during program initialization, before starting worker threads.
 *
 * \par Requirements:
 *  - 001 Other OS error-mode settings shall be preserved.
 *
 * \param[in] interactive   TRUE permits error dialogs; FALSE suppresses them.
 *
 * \retval    None
 */
void OS_Process_SetInteractive(bool_t interactive);

/*************************************************************************************************/
/**
 * \brief Terminates the process with the supplied status.
 *
 * \par Requirements:
 *  - 001 The function shall not return or invoke a CRT shutdown sequence.
 *
 * \param[in] status   Process completion code
 *
 * \retval    None
 */
OS_NORETURN void OS_Process_Exit(uint32_t status);

/*************************************************************************************************/
/**
 * \brief Supplies the application body called by the target's CRT-free entry adapter.
 *
 * \par Details:
 *  The executable provides this function. The backend owns native startup and exit.
 *
 * \retval 0               Application completed successfully.
 * \retval 1..UINT32_MAX   Application-specific failure status.
 */
uint32_t OS_Application_Main(void);

#endif /* CAST_OS_PROCESS_H */
