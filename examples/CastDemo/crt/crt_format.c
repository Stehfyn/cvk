#include "crt/crt_format.h"

#include <stdint.h>
#include <stdarg.h>

#include "os/os_types.h"
#include "utl/utl_decimal.h"

#define CRT_FORMAT_DECIMAL_CAPACITY (20U)
#define CRT_FORMAT_HEX_SHIFT_FIRST (28U)
#define CRT_FORMAT_HEX_MASK (15U)
#define CRT_FORMAT_HEX_ALPHA_START (10U)
#define CRT_FORMAT_TENTHS (10U)
#define CRT_FORMAT_HUNDREDTHS (100U)
#define CRT_FORMAT_ROUND_HALF (0.5)
#define CRT_FORMAT_UINT64_LIMIT (18446744073709551616.0)

typedef struct CRT_Format_Writer_s
{
    char_t*  output;
    uint32_t capacity;
    uint32_t count;
    bool_t   complete;
} CRT_Format_Writer_t;

static void CRT_Format_Character(CRT_Format_Writer_t* writer, char_t character)
{
    if ((writer->count + 1U) < writer->capacity)
    {
        writer->output[writer->count] = character;
        writer->count += 1U;
        writer->output[writer->count] = '\0';
    }
    else
    {
        writer->complete = FALSE;
    }
}

static void CRT_Format_Text(CRT_Format_Writer_t* writer, const char_t* value, uint32_t limit)
{
    uint32_t index = 0;

    if (NULL == value)
    {
        value = "(null)";
    }
    while ((index < limit) && ('\0' != value[index]))
    {
        CRT_Format_Character(writer, value[index]);
        index += 1U;
    }
}

static void CRT_Format_Unsigned(CRT_Format_Writer_t* writer, uint64_t value)
{
    byte_t   digits[CRT_FORMAT_DECIMAL_CAPACITY];
    uint32_t count;
    uint32_t index;

    count = Utl_Decimal_WriteUint64(value, digits, (uint32_t)sizeof(digits));
    for (index = 0; index < count; index += 1U)
    {
        CRT_Format_Character(writer, (char_t)digits[index]);
    }
}

static void CRT_Format_Signed(CRT_Format_Writer_t* writer, int64_t value)
{
    if (value < 0)
    {
        CRT_Format_Character(writer, '-');
        CRT_Format_Unsigned(writer, (uint64_t)(-(value + 1)) + 1U);
    }
    else
    {
        CRT_Format_Unsigned(writer, (uint64_t)value);
    }
}

static void CRT_Format_Hex32(CRT_Format_Writer_t* writer, uint32_t value)
{
    uint32_t shift;

    for (shift = CRT_FORMAT_HEX_SHIFT_FIRST;; shift -= 4U)
    {
        uint32_t digit = (value >> shift) & CRT_FORMAT_HEX_MASK;
        CRT_Format_Character(writer, (char_t)((digit < CRT_FORMAT_HEX_ALPHA_START)
                                                   ? ('0' + digit)
                                                   : ('A' + digit - CRT_FORMAT_HEX_ALPHA_START)));
        if (0U == shift)
        {
            break;
        }
    }
}

static void CRT_Format_Fixed(CRT_Format_Writer_t* writer, float64_t value, uint32_t fractionalDigits)
{
    uint64_t scale = (1U == fractionalDigits) ? CRT_FORMAT_TENTHS : CRT_FORMAT_HUNDREDTHS;
    uint64_t scaled;
    uint64_t fraction;

    if (value < 0.0)
    {
        CRT_Format_Character(writer, '-');
        value = -value;
    }
    if ((value != value) || (value >= (CRT_FORMAT_UINT64_LIMIT / (float64_t)scale)))
    {
        writer->complete = FALSE;
        return;
    }
    scaled = (uint64_t)((value * (float64_t)scale) + CRT_FORMAT_ROUND_HALF);
    CRT_Format_Unsigned(writer, scaled / scale);
    CRT_Format_Character(writer, '.');
    fraction = scaled % scale;
    if ((2U == fractionalDigits) && (fraction < CRT_FORMAT_TENTHS))
    {
        CRT_Format_Character(writer, '0');
    }
    CRT_Format_Unsigned(writer, fraction);
}

bool_t CRT_Format_Copy(char_t* output, uint32_t capacity, const char_t* text)
{
    CRT_Format_Writer_t writer;

    if ((NULL == output) || (0U == capacity) || (NULL == text))
    {
        return FALSE;
    }
    writer.output = output;
    writer.capacity = capacity;
    writer.count = 0U;
    writer.complete = TRUE;
    output[0] = '\0';
    CRT_Format_Text(&writer, text, UINT32_MAX);
    return writer.complete;
}

static bool_t CRT_Format_Precision(CRT_Format_Writer_t* writer,
                                   const char_t* format,
                                   uint32_t* cursor,
                                   va_list* arguments)
{
    bool_t valid = TRUE;

    *cursor += 1U;
    if (('*' == format[*cursor]) && ('s' == format[*cursor + 1U]))
    {
        int length = va_arg(*arguments, int);
        *cursor += 1U;
        CRT_Format_Text(writer, va_arg(*arguments, const char_t*),
                        (length < 0) ? UINT32_MAX : (uint32_t)length);
    }
    else if ((('1' == format[*cursor]) || ('2' == format[*cursor])) &&
             ('f' == format[*cursor + 1U]))
    {
        uint32_t fractionalDigits = (uint32_t)(format[*cursor] - '0');
        *cursor += 1U;
        CRT_Format_Fixed(writer, va_arg(*arguments, float64_t), fractionalDigits);
    }
    else
    {
        valid = FALSE;
    }
    return valid;
}

static bool_t CRT_Format_Conversion(CRT_Format_Writer_t* writer,
                                    const char_t* format,
                                    uint32_t* cursor,
                                    va_list* arguments)
{
    bool_t valid = TRUE;

    switch (format[*cursor])
    {
        case '%':
            CRT_Format_Character(writer, '%');
            break;
        case 's':
            CRT_Format_Text(writer, va_arg(*arguments, const char_t*), UINT32_MAX);
            break;
        case 'u':
            CRT_Format_Unsigned(writer, va_arg(*arguments, unsigned int));
            break;
        case 'd':
            CRT_Format_Signed(writer, va_arg(*arguments, int));
            break;
        case '0':
            if (('8' == format[*cursor + 1U]) && ('X' == format[*cursor + 2U]))
            {
                CRT_Format_Hex32(writer, va_arg(*arguments, unsigned int));
                *cursor += 2U;
            }
            else if (('8' == format[*cursor + 1U]) && ('l' == format[*cursor + 2U]) &&
                     ('X' == format[*cursor + 3U]))
            {
                CRT_Format_Hex32(writer, va_arg(*arguments, unsigned long));
                *cursor += 3U;
            }
            else
            {
                valid = FALSE;
            }
            break;
        case 'l':
            if (('l' == format[*cursor + 1U]) && ('u' == format[*cursor + 2U]))
            {
                CRT_Format_Unsigned(writer, va_arg(*arguments, unsigned long long));
                *cursor += 2U;
            }
            else if (('l' == format[*cursor + 1U]) && ('d' == format[*cursor + 2U]))
            {
                CRT_Format_Signed(writer, va_arg(*arguments, long long));
                *cursor += 2U;
            }
            else
            {
                valid = FALSE;
            }
            break;
        case '.':
            valid = CRT_Format_Precision(writer, format, cursor, arguments);
            break;
        default:
            valid = FALSE;
            break;
    }
    return valid;
}

bool_t CRT_Format_VAppend(char_t* output, uint32_t capacity, const char_t* format, va_list* arguments)
{
    CRT_Format_Writer_t writer;
    uint32_t cursor = 0U;

    if ((NULL == output) || (0U == capacity) || (NULL == format))
    {
        return FALSE;
    }
    writer.output = output;
    writer.capacity = capacity;
    writer.count = 0U;
    writer.complete = TRUE;
    while ((writer.count < capacity) && ('\0' != output[writer.count]))
    {
        writer.count += 1U;
    }
    if (writer.count == capacity)
    {
        return FALSE;
    }
    while ('\0' != format[cursor])
    {
        if ('%' != format[cursor])
        {
            CRT_Format_Character(&writer, format[cursor]);
        }
        else
        {
            cursor += 1U;
            if (!CRT_Format_Conversion(&writer, format, &cursor, arguments))
            {
                writer.complete = FALSE;
                break;
            }
        }
        cursor += 1U;
    }
    return writer.complete;
}

bool_t CRT_Format_Write(char_t* output, uint32_t capacity, const char_t* format, ...)
{
    va_list arguments;
    bool_t result;

    if ((NULL == output) || (0U == capacity))
    {
        return FALSE;
    }
    output[0] = '\0';
    va_start(arguments, format);
    result = CRT_Format_VAppend(output, capacity, format, &arguments);
    va_end(arguments);
    return result;
}
