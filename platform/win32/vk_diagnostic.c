/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Win32 diagnostic backend. It owns no persistent storage or runtime state. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "vk_diagnostic.h"
#include "utl/utl_decimal.h"
#include "os/os_types.h"

#include <Windows.h> // IWYU pragma: keep
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

    if (NULL != text)
    {
        length = (DWORD)lstrlenA(text);
        output = GetStdHandle(STD_ERROR_HANDLE);
        if ((NULL != output) && (INVALID_HANDLE_VALUE != output))
        {
            (void)WriteFile(output, text, length, &written, NULL);
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

    has_text  = (NULL != text);
    has_bytes = (0 < byteCount);
    if (has_text && has_bytes)
    {
        output       = GetStdHandle(STD_ERROR_HANDLE);
        has_output   = (NULL != output);
        valid_output = (INVALID_HANDLE_VALUE != output);
        if (has_output && valid_output)
        {
            (void)WriteFile(output, text, byteCount, &written, NULL);
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
