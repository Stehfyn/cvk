/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * https://learn.microsoft.com/en-us/windows/console/readconsoleinputex
 * https://learn.microsoft.com/en-us/windows/console/setconsolemode
 * https://learn.microsoft.com/en-us/windows/console/key-event-record-str
 * https://learn.microsoft.com/en-us/windows/win32/inputdev/virtual-key-codes
 * The native character union is deliberately not read. Virtual key identity is
 * independent of A/W input encoding; no application playback policy belongs here.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_console.h"
#include "os/win32/os_console_native.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include <basetsd.h>
#include <consoleapi.h>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <minwindef.h>
#include <processenv.h>
#include <processthreadsapi.h>
#include <wincontypes.h>
#include <winerror.h>
#include <winnt.h>
#include <WinBase.h>
#include <WinUser.h>
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define NATIVE_KEY_A          (0x41) ///< Win32 virtual key code, independent of translated text.
#define NATIVE_KEY_Z          (0x5A) ///< Last contiguous Latin-letter virtual key.
#define NATIVE_INVALID_HANDLE (-1)   ///< Win32 failed-handle identity, compared without constructing a pointer.
#define INPUT_MODE_DISABLED (ENABLE_PROCESSED_INPUT | ENABLE_QUICK_EDIT_MODE | ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Derives a portable key identity from a native virtual key code.
 *
 * \param[in] nativeCode   Native virtual-key value
 *
 * \retval    key   Supported identity, or UNKNOWN for other keys.
 */
static FORCE_INLINE OS_Console_KeyCode_t GetKeyCode(uint32_t nativeCode);

/*************************************************************************************************/
/**
 * \brief Derives only the portable modifier bits from native control-key state.
 *
 * \param[in] nativeState   Native modifier mask
 *
 * \retval    modifiers   Normalized shift, control and alt bits.
 */
static FORCE_INLINE uint32_t GetModifiers(uint32_t nativeState);

/*************************************************************************************************/
/**
 * \brief Retains the first native failure and disables further input.
 *
 * \par Details:
 *  Restoration and handle ownership are preserved.
 *
 * \param[in,out] console   Required initialized owner
 * \param[in]     error     Native failure code, normalized when unexpectedly zero
 *
 * \retval        None
 */
static FORCE_INLINE void RecordError(OS_Console_t* console, uint32_t error);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The bounds establish that every letter result names a declared portable enum.
 */
static FORCE_INLINE OS_Console_KeyCode_t GetKeyCode(uint32_t nativeCode)
{
    bool_t               at_least_a;
    bool_t               at_most_z;
    bool_t               is_letter;
    OS_Console_KeyCode_t code;

    at_least_a = (NATIVE_KEY_A <= nativeCode);
    at_most_z  = (nativeCode <= NATIVE_KEY_Z);
    is_letter  = at_least_a && at_most_z;
    code       = OS_CONSOLE_KEY_UNKNOWN;
    if (is_letter)
    {
        code = (OS_Console_KeyCode_t)((uint32_t)OS_CONSOLE_KEY_A + nativeCode - NATIVE_KEY_A);
    }
    else
    {
        switch (nativeCode)
        {
        case VK_CANCEL:
            code = OS_CONSOLE_KEY_CANCEL;
            break;
        case VK_ESCAPE:
            code = OS_CONSOLE_KEY_ESCAPE;
            break;
        case VK_SPACE:
            code = OS_CONSOLE_KEY_SPACE;
            break;
        default:
            break;
        }
    }
    return code;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Left and right keys share portable modifiers; lock and enhanced-key flags do not escape.
 */
static FORCE_INLINE uint32_t GetModifiers(uint32_t nativeState)
{
    uint32_t modifiers;

    modifiers = 0;
    if (nativeState & SHIFT_PRESSED)
    {
        modifiers |= OS_CONSOLE_MODIFIER_SHIFT;
    }
    if (nativeState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED))
    {
        modifiers |= OS_CONSOLE_MODIFIER_CONTROL;
    }
    if (nativeState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED))
    {
        modifiers |= OS_CONSOLE_MODIFIER_ALT;
    }
    return modifiers;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Failure reporting cannot discard a live handle or a pending restoration.
 */
static FORCE_INLINE void RecordError(OS_Console_t* console, uint32_t error)
{
    console->available = FALSE;
    if (ERROR_SUCCESS == console->error)
    {
        console->error = error;
        if (ERROR_SUCCESS == error)
        {
            console->error = ERROR_GEN_FAILURE;
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The duplicated handle isolates native handle lifetime. Input mode remains a
 *  shared console property whose exclusive ownership is a caller precondition.
 */
bool_t OS_Console_Initialize(OS_Console_t* console)
{
    HANDLE standard_input;
    bool_t has_handle;
    bool_t invalid_handle;
    bool_t can_query;
    bool_t ready;
    DWORD  error;
    bool_t is_console;
    DWORD  mode;
    HANDLE process;
    HANDLE owned_input;
    bool_t duplicated;
    DWORD  requested_mode;
    bool_t mode_set;
    bool_t redirected;

    LOGASSERT(NULL != console);

    console->input          = NULL;
    console->originalMode   = 0;
    console->error          = ERROR_SUCCESS;
    console->available      = FALSE;
    console->restorePending = FALSE;
    console->closed         = FALSE;
    standard_input          = GetStdHandle(STD_INPUT_HANDLE);
    has_handle              = (NULL != standard_input);
    invalid_handle          = (NATIVE_INVALID_HANDLE == (LONG_PTR)standard_input);
    can_query               = has_handle && !invalid_handle;
    ready                   = !invalid_handle;
    if (invalid_handle)
    {
        error = GetLastError();
        RecordError(console, error);
    }
    if (can_query)
    {
        is_console = !!GetConsoleMode(standard_input, &mode);
        if (is_console)
        {
            process     = GetCurrentProcess();
            owned_input = NULL;
            duplicated  = !!DuplicateHandle(process,
                                           standard_input,
                                           process,
                                           &owned_input,
                                           0,
                                           FALSE,
                                           DUPLICATE_SAME_ACCESS);
            ready = duplicated;
            if (duplicated)
            {
                console->input          = owned_input;
                console->originalMode   = mode;
                console->restorePending = TRUE;
                requested_mode          = (mode | ENABLE_EXTENDED_FLAGS) & ~(DWORD)INPUT_MODE_DISABLED;
                mode_set                = !!SetConsoleMode(owned_input, requested_mode);
                ready                   = mode_set;
                if (mode_set)
                {
                    console->available = TRUE;
                }
                else
                {
                    error = GetLastError();
                    RecordError(console, error);
                }
            }
            else
            {
                error = GetLastError();
                RecordError(console, error);
            }
        }
        else
        {
            error      = GetLastError();
            redirected = (ERROR_INVALID_HANDLE == error);
            ready      = redirected;
            if (!redirected)
            {
                RecordError(console, error);
            }
        }
    }
    return ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t OS_Console_GetError(const OS_Console_t* console)
{
    LOGASSERT(NULL != console);
    return console->error;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t OS_Console_IsAvailable(const OS_Console_t* console)
{
    LOGASSERT(NULL != console);
    return console->available;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  ReadConsoleInputEx with NOWAIT atomically consumes at most one available
 *  record. An empty input queue cannot turn a prior readiness check into a wait.
 */
OS_Console_ReadResult_t OS_Console_ReadKey(OS_Console_t* console, OS_Console_Key_t* key)
{
    OS_Console_ReadResult_t result;
    bool_t                  has_error;
    bool_t                  read;
    INPUT_RECORD            record;
    DWORD                   count;
    DWORD                   error;
    bool_t                  has_record;
    bool_t                  is_key;
    OS_Console_Key_t        candidate;

    LOGASSERT(NULL != console);
    LOGASSERT(NULL != key);

    result    = OS_CONSOLE_READ_EMPTY;
    has_error = (ERROR_SUCCESS != console->error);
    if (has_error)
    {
        result = OS_CONSOLE_READ_FAILED;
    }
    else if (console->available)
    {
        read = !!ReadConsoleInputEx(console->input, &record, 1, &count, OS_CONSOLE_READ_NOWAIT);
        if (!read)
        {
            error = GetLastError();
            RecordError(console, error);
            result = OS_CONSOLE_READ_FAILED;
        }
        else
        {
            has_record = (1 == count);
            if (has_record)
            {
                is_key = (KEY_EVENT == record.EventType);
                result = OS_CONSOLE_READ_IGNORED;
                if (is_key)
                {
                    candidate.code        = GetKeyCode(record.Event.KeyEvent.wVirtualKeyCode);
                    candidate.modifiers   = GetModifiers(record.Event.KeyEvent.dwControlKeyState);
                    candidate.repeatCount = record.Event.KeyEvent.wRepeatCount;
                    candidate.pressed     = !!record.Event.KeyEvent.bKeyDown;
                    *key                  = candidate;
                    result                = OS_CONSOLE_READ_KEY;
                }
            }
        }
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Restoration and native handle release are separate retained obligations.
 *  No failed operation is cleared or interpreted as successful closure.
 */
bool_t OS_Console_TryClose(OS_Console_t* console)
{
    bool_t restored;
    DWORD  error;
    bool_t has_handle;
    bool_t can_close;
    bool_t released;

    LOGASSERT(NULL != console);

    console->available = FALSE;
    if (!console->closed)
    {
        if (console->restorePending)
        {
            restored = !!SetConsoleMode(console->input, console->originalMode);
            if (restored)
            {
                console->restorePending = FALSE;
            }
            else
            {
                error = GetLastError();
                RecordError(console, error);
            }
        }
        has_handle = (NULL != console->input);
        can_close  = has_handle && !console->restorePending;
        if (can_close)
        {
            released = !!CloseHandle(console->input);
            if (released)
            {
                console->input = NULL;
            }
            else
            {
                error = GetLastError();
                RecordError(console, error);
            }
        }
        console->closed = (NULL == console->input);
    }
    return console->closed;
}
