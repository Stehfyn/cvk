/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Win32 native text conversion. Unicode builds copy UTF-16; ANSI builds use
 *  the active Windows code page and reject lossy filename conversions.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/win32/os_text.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include "crt/crt_memory.h"
#include "utl/utl_unicode.h"

#include <Windows.h> // IWYU pragma: keep
#include <winnt.h>
#include <minwindef.h>
#include <stringapiset.h>
#include <WinNls.h>
#include <WinBase.h>
#include <limits.h>
#include <stdint.h>

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

enum
{
    NATIVE_CAPACITY_SIGN_SHIFT = (sizeof(uint32_t) * CHAR_BIT) - 1
};

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Native units are measured through the generic Win32 API. UTF-16 interfaces
 *  of the conversion functions are used only inside this encoding boundary.
 */
extern FORCE_INLINE bool_t OS_Text_ReadNative(uint16_t* output,
                                            uint32_t capacity,
                                            LPCTSTR  text)
{
    uint32_t count;
    int      required;
    int      written;
    int      native_capacity;
    bool_t   is_unicode;
    bool_t   has_capacity;
    bool_t   fits;
    bool_t   valid_text;
    bool_t   converted;
    bool_t   accepted;
    uint64_t copy_bytes;
    uint16_t scratch;
    uint16_t* destination;

    /* Saturate at the largest count accepted by Win32 conversion APIs. */
    native_capacity = (int)((capacity | (0U - (capacity >> NATIVE_CAPACITY_SIGN_SHIFT))) & INT_MAX);
    has_capacity    = 0 < capacity;
    LOGASSERT(NULL != output);
    LOGASSERT(NULL != text);
    destination = has_capacity ? output : &scratch;
    destination[0] = 0;
    is_unicode = sizeof(TCHAR) == sizeof(uint16_t);
    if (is_unicode)
    {
        count = (uint32_t)lstrlen(text);
        fits  = count < (uint32_t)native_capacity;
        valid_text = Utl_Unicode_IsValidUtf16((const uint16_t*)text, count);
        accepted   = fits && valid_text;
        copy_bytes = (((uint64_t)count + 1) * sizeof(uint16_t)) * (uint64_t)accepted;
        CRT_Memory_Copy(destination, text, copy_bytes);
    }
    else
    {
        required = MultiByteToWideChar(CP_ACP, MB_ERR_INVALID_CHARS, (LPCCH)text, -1, NULL, 0);
        fits      = required <= native_capacity;
        converted = 0 < required;
        written  = MultiByteToWideChar(CP_ACP,
                                      MB_ERR_INVALID_CHARS,
                                      (LPCCH)text,
                                      -1,
                                      (LPWSTR)destination,
                                      native_capacity);
        accepted = converted && fits && (written == required);
    }
    if (!accepted)
    {
        destination[0] = 0;
    }
    LOGASSERT(has_capacity);
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  UTF-8 active code pages require strict UTF-16 validation and no default-character
 *  parameter. Other ANSI pages reject both best-fit mapping and default substitution.
 */
extern FORCE_INLINE bool_t OS_Text_WriteNative(LPTSTR          output,
                                             uint32_t        capacity,
                                             const uint16_t* text)
{
    uint32_t count;
    UINT     code_page;
    DWORD    flags;
    BOOL     defaulted;
    LPBOOL   used_default;
    int      required;
    int      written;
    int      native_capacity;
    bool_t   is_unicode;
    bool_t   is_utf8;
    bool_t   has_capacity;
    bool_t   fits;
    bool_t   valid_text;
    bool_t   converted;
    bool_t   lossless;
    bool_t   accepted;
    uint64_t copy_bytes;
    TCHAR    scratch;
    LPTSTR   destination;

    /* Saturate at the largest count accepted by Win32 conversion APIs. */
    native_capacity = (int)((capacity | (0U - (capacity >> NATIVE_CAPACITY_SIGN_SHIFT))) & INT_MAX);
    has_capacity    = 0 < capacity;
    LOGASSERT(NULL != output);
    LOGASSERT(NULL != text);
    destination = has_capacity ? output : &scratch;
    destination[0] = TEXT('\0');
    is_unicode = sizeof(TCHAR) == sizeof(uint16_t);
    if (is_unicode)
    {
        count = Utl_Unicode_GetUtf16StringUnitCount(text, INT_MAX);
        fits  = count < (uint32_t)native_capacity;
        valid_text = Utl_Unicode_IsValidUtf16(text, count);
        accepted   = fits && valid_text;
        copy_bytes = (((uint64_t)count + 1) * sizeof(uint16_t)) * (uint64_t)accepted;
        CRT_Memory_Copy(destination, text, copy_bytes);
    }
    else
    {
        code_page    = GetACP();
        is_utf8      = CP_UTF8 == code_page;
        flags        = is_utf8 ? WC_ERR_INVALID_CHARS : WC_NO_BEST_FIT_CHARS;
        defaulted    = FALSE;
        used_default = is_utf8 ? NULL : &defaulted;
        required     = WideCharToMultiByte(code_page, flags, (LPCWCH)text, -1, NULL, 0, NULL, used_default);
        fits         = required <= native_capacity;
        converted    = 0 < required;
        lossless     = !defaulted;
        written = WideCharToMultiByte(code_page,
                                     flags,
                                     (LPCWCH)text,
                                     -1,
                                     (LPSTR)destination,
                                     native_capacity,
                                     NULL,
                                     used_default);
        converted = converted && (written == required);
        lossless  = lossless && !defaulted;
        accepted  = converted && lossless && fits;
    }
    if (!accepted)
    {
        destination[0] = TEXT('\0');
    }
    LOGASSERT(has_capacity);
    return accepted;
}
