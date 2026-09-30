/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Win32 diagnostic backend. It owns no persistent storage or runtime state. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#define WIN32_LEAN_AND_MEAN
#include <Windows.h> // IWYU pragma: keep
#include <stddef.h>
#include "vk_diagnostic.h"
#include "utl/utl_decimal.h"
#include "os/os_types.h"

#include <fileapi.h>
#include <handleapi.h>
#include <minwindef.h>
#include <processenv.h>
#include <processthreadsapi.h>
#include <stdint.h>
#include <WinBase.h>
#include <winnt.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define CVK_DIAGNOSTIC_HEX_COUNT (8)
#define CVK_DIAGNOSTIC_HEX_MASK  (15)
#define CVK_DIAGNOSTIC_HEX_SHIFT (4)
#define CVK_DIAGNOSTIC_DECIMAL_DIGIT_COUNT (10)
#define CVK_DIAGNOSTIC_UINT64_DIGITS       (20)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 4. PUBLIC FUNCTIONS
**===============================================================================================*/

void cvkZeroMemory(void* pMemory, size_t byteCount)
{
    SecureZeroMemory(pMemory, byteCount);
}

void cvkCopyMemory(void* pDestination, const void* pSource, size_t byteCount)
{
    unsigned char*       destination = (unsigned char*)pDestination;
    const unsigned char* source      = (const unsigned char*)pSource;
    size_t               index;

    for (index = 0; index < byteCount; ++index)
    {
        destination[index] = source[index];
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Writes a null-terminated diagnostic without a C runtime.
 */
extern FORCE_INLINE void cvkDiagnosticWrite(const char* text)
{
    DWORD length;
    DWORD written;
    HANDLE output;

    if (0 != text)
    {
        length = (DWORD)lstrlenA(text);
        output = GetStdHandle(STD_ERROR_HANDLE);
        if ((0 != output) && (INVALID_HANDLE_VALUE != output))
        {
            (void)WriteFile(output, text, length, &written, 0);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Writes a counted byte span without requiring a trailing null.
 */
extern FORCE_INLINE void cvkDiagnosticWriteBytes(const char* text, uint32_t byteCount)
{
    bool_t has_text;
    bool_t has_bytes;
    HANDLE output;
    bool_t has_output;
    bool_t valid_output;
    DWORD  written;

    has_text  = (0 != text);
    has_bytes = (0 < byteCount);
    if (has_text && has_bytes)
    {
        output       = GetStdHandle(STD_ERROR_HANDLE);
        has_output   = (0 != output);
        valid_output = (INVALID_HANDLE_VALUE != output);
        if (has_output && valid_output)
        {
            (void)WriteFile(output, text, byteCount, &written, 0);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The decimal specification module writes into bounded caller-owned storage.
 */
extern FORCE_INLINE void cvkDiagnosticWriteDecimal(uint64_t value)
{
    byte_t   digits[CVK_DIAGNOSTIC_UINT64_DIGITS];
    uint32_t count;

    count = Utl_Decimal_WriteUint64(value, digits, sizeof(digits));
    cvkDiagnosticWriteBytes((const char*)digits, count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Converts each hexadecimal digit in caller-local storage.
 */
extern FORCE_INLINE void cvkDiagnosticWriteHex(uint32_t value)
{
    char     digits[CVK_DIAGNOSTIC_HEX_COUNT + 1];
    char*    cursor;
    uint32_t nibble;
    uint32_t index;

    cursor  = digits + CVK_DIAGNOSTIC_HEX_COUNT;
    *cursor = 0;
    for (index = 0; index < CVK_DIAGNOSTIC_HEX_COUNT; ++index)
    {
        nibble = value & CVK_DIAGNOSTIC_HEX_MASK;
        --cursor;
        *cursor = (char)((nibble < CVK_DIAGNOSTIC_DECIMAL_DIGIT_COUNT)
                             ? ('0' + nibble)
                             : ('a' + nibble - CVK_DIAGNOSTIC_DECIMAL_DIGIT_COUNT));
        value >>= CVK_DIAGNOSTIC_HEX_SHIFT;
    }
    cvkDiagnosticWrite(digits);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The caller has already written the diagnostic; Win32 owns process termination.
 */
VK_DIAGNOSTIC_NORETURN void cvkDiagnosticExit(void)
{
    ExitProcess(1);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  SCVK uses the same byte-oriented Win32 backend without a CVK library dependency.
 */
void scvkDiagnosticWrite(const char* text)
{
    cvkDiagnosticWrite(text);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  SCVK uses the same fixed-width hexadecimal formatter.
 */
void scvkDiagnosticWriteHex(uint32_t value)
{
    cvkDiagnosticWriteHex(value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  SCVK fatal diagnostics terminate through the same Win32 process operation.
 */
VK_DIAGNOSTIC_NORETURN void scvkDiagnosticExit(void)
{
    ExitProcess(1);
}
