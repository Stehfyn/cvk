#include "vk_diagnostic.h"
#include "utl/utl_decimal.h"
#include "os/os_types.h"

#include <unistd.h>

#define VK_DIAGNOSTIC_MAX_BYTES (1024)
#define VK_DIAGNOSTIC_HEX_COUNT (8)
#define VK_DIAGNOSTIC_HEX_MASK  (15)
#define VK_DIAGNOSTIC_HEX_SHIFT (4)
#define VK_DIAGNOSTIC_UINT64_DIGITS (20)

void cvkZeroMemory(void* pMemory, size_t byteCount)
{
    __builtin_memset(pMemory, 0, byteCount);
}

void cvkCopyMemory(void* pDestination, const void* pSource, size_t byteCount)
{
    __builtin_memcpy(pDestination, pSource, byteCount);
}

static void WriteText(const char* text)
{
    unsigned int length;

    if (NULL != text)
    {
        length = 0;
        for (; length < VK_DIAGNOSTIC_MAX_BYTES; ++length)
        {
            if (0 == text[length])
            {
                break;
            }
        }
        if (0 < length)
        {
            (void)write(STDERR_FILENO, text, length);
        }
    }
}

static void WriteHex(unsigned int value)
{
    const char alphabet[] = "0123456789abcdef";
    char       digits[VK_DIAGNOSTIC_HEX_COUNT + 1];
    int        index;

    digits[VK_DIAGNOSTIC_HEX_COUNT] = 0;
    for (index = VK_DIAGNOSTIC_HEX_COUNT - 1; 0 <= index; --index)
    {
        digits[index] = alphabet[value & VK_DIAGNOSTIC_HEX_MASK];
        value >>= VK_DIAGNOSTIC_HEX_SHIFT;
    }
    WriteText(digits);
}

void cvkDiagnosticWrite(const char* text)
{
    WriteText(text);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Writes a counted text span without a trailing terminator or C runtime.
 */
extern FORCE_INLINE void cvkDiagnosticWriteBytes(const char* text, uint32_t byteCount)
{
    bool_t has_text;
    bool_t has_bytes;

    has_text  = (NULL != text);
    has_bytes = (0 < byteCount);
    if (has_text && has_bytes)
    {
        (void)write(STDERR_FILENO, text, byteCount);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The decimal module writes into bounded caller-owned storage.
 */
extern FORCE_INLINE void cvkDiagnosticWriteDecimal(uint64_t value)
{
    byte_t   digits[VK_DIAGNOSTIC_UINT64_DIGITS];
    uint32_t count;

    count = Utl_Decimal_WriteUint64(value, digits, sizeof(digits));
    cvkDiagnosticWriteBytes((const char*)digits, count);
}

void cvkDiagnosticWriteHex(uint32_t value)
{
    WriteHex(value);
}

VK_DIAGNOSTIC_NORETURN void cvkDiagnosticExit(void)
{
    _exit(1);
}

void scvkDiagnosticWrite(const char* text)
{
    WriteText(text);
}

void scvkDiagnosticWriteHex(uint32_t value)
{
    WriteHex(value);
}

VK_DIAGNOSTIC_NORETURN void scvkDiagnosticExit(void)
{
    _exit(1);
}
