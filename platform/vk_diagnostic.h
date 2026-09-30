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
#include <stddef.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#ifndef FORCE_INLINE
#ifdef __clang__
#define FORCE_INLINE __attribute__((always_inline))
#elif defined(_MSC_VER)
#define FORCE_INLINE __forceinline
#else
#define FORCE_INLINE inline __attribute__((always_inline))
#endif
#endif

#ifdef _MSC_VER
#define VK_DIAGNOSTIC_NORETURN __declspec(noreturn)
#else
#define VK_DIAGNOSTIC_NORETURN __attribute__((noreturn))
#endif

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Clears a typed destination without a C runtime dependency.
 * \param[out] pMemory Writable destination.
 * \param[in] byteCount Number of bytes to clear.
 * \retval None No value is returned.
 */
void cvkZeroMemory(void* pMemory, size_t byteCount);

/** Copies a counted nonoverlapping byte span without requiring a C runtime. */
void cvkCopyMemory(void* pDestination, const void* pSource, size_t byteCount);

/*************************************************************************************************/
/**
 * \brief Writes a bounded terminated diagnostic byte string to standard error.
 * \param[in] text  Source string, or NULL for no output.
 * \retval    None No value is returned.
 */
void FORCE_INLINE cvkDiagnosticWrite(const char* text);

/*************************************************************************************************/
/**
 * \brief Writes counted diagnostic bytes to standard error.
 * \par Requirements:
 *  - 001 The caller shall provide byteCount readable bytes when text is non-NULL.
 *  - 002 NULL or empty input shall produce no output.
 * \param[in] text       Counted text; no terminator is required.
 * \param[in] byteCount  Readable byte extent.
 * \retval None No value is returned.
 */
void cvkDiagnosticWriteBytes(const char* text, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Writes the shortest unsigned decimal representation.
 * \par Requirements:
 *  - 001 Formatting shall use bounded automatic storage and the decimal module.
 *  - 002 No CRT formatting or allocation shall occur.
 * \param[in] value  Value to display.
 * \retval None No value is returned.
 */
void cvkDiagnosticWriteDecimal(uint64_t value);

/*************************************************************************************************/
/**
 * \brief Writes an unsigned value as eight hexadecimal digits.
 * \param[in] value  Value to format.
 * \retval    None No value is returned.
 */
void cvkDiagnosticWriteHex(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Ends the process after a fatal diagnostic.
 * \retval    None No value is returned.
 */
VK_DIAGNOSTIC_NORETURN void cvkDiagnosticExit(void);

void scvkDiagnosticWrite(const char* text);
void scvkDiagnosticWriteHex(uint32_t value);
VK_DIAGNOSTIC_NORETURN void scvkDiagnosticExit(void);

#endif
