#include "vk_diagnostic.h"

#include <unistd.h>

#define VK_DIAGNOSTIC_MAX_BYTES (1024)
#define VK_DIAGNOSTIC_HEX_COUNT (8)
#define VK_DIAGNOSTIC_HEX_MASK  (15)
#define VK_DIAGNOSTIC_HEX_SHIFT (4)

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
