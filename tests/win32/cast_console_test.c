/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Native console ownership and non-waiting input regression fixture.
 * tests/run_isolated_console.py supplies a new hidden console. The fixture checks
 * exclusive process ownership before changing input modes or injecting records.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h>
#include <consoleapi.h>
#include <consoleapi2.h>
#include <consoleapi3.h>
#include <errhandlingapi.h>
#include <handleapi.h>
#include <minwindef.h>
#include <namedpipeapi.h>
#include <processenv.h>
#include <processthreadsapi.h>
#include <wincontypes.h>
#include <winerror.h>
#include <winnt.h>
#include <WinBase.h>
#include <stdint.h>
#include "os/os_console.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include "crt/crt_memory.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_NATIVE_A      (0x41) ///< Independent Win32 virtual key for A.
#define TEST_NATIVE_F1     (0x70) ///< Unsupported portable key used to check UNKNOWN.
#define TEST_REPEAT_COUNT  (3)    ///< Repeat count that must survive normalization.
#define TEST_RETAIN_COUNT  (13)   ///< Sentinel used to detect unintended output publication.
#define TEST_MODE_KEYBOARD ('k')  ///< Launcher selects a fresh hidden console.
#define TEST_MODE_ABSENT   ('n')  ///< Launcher selects a detached process without console handles.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief All owners and native setup records, established before input exercise.
 */
typedef struct ConsoleFixture_s
{
    HANDLE       input;        ///< Borrowed original console input handle.
    DWORD        originalMode; ///< Input mode before initialization.
    OS_Console_t keyboard;     ///< Owned duplicate used for key reads.
    OS_Console_t redirected;   ///< Successfully initialized with pipe input.
    OS_Console_t invalid;      ///< Failed initialization with a native failed-handle identity.
    uint32_t     initialError; ///< Captured native startup error, retained through closure.
} ConsoleFixture_t;

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
 * \brief Initializes isolated keyboard, redirected and failed-start owners.
 *
 * \param[out] fixture   Required lifetime storage
 *
 * \retval     None   Setup succeeds or an assertion terminates this isolated process.
 */
static FORCE_INLINE void InitializeFixture(ConsoleFixture_t* fixture);

/*************************************************************************************************/
/**
 * \brief Checks unchanged output on an expected non-key result.
 *
 * \param[in,out] console   Initialized owner
 * \param[in]     expected  EMPTY, IGNORED or FAILED
 *
 * \retval        None   Every published field retains its independent sentinel.
 */
static FORCE_INLINE void CheckPreservedKey(OS_Console_t* console, OS_Console_ReadResult_t expected);

/*************************************************************************************************/
/**
 * \brief Exercises one-record consumption, repeat counts, modifiers and key-up.
 *
 * \param[in,out] fixture   Initialized exclusive console owner
 *
 * \retval        None   Native results match the expected portable records.
 */
static FORCE_INLINE void CheckInput(ConsoleFixture_t* fixture);

/*************************************************************************************************/
/**
 * \brief Checks complete mode restoration and idempotent owner closure.
 *
 * \param[in,out] fixture   Initialized owners after input exercise
 *
 * \retval        None   Every owner is closed with its first error preserved.
 */
static FORCE_INLINE void CheckClosure(ConsoleFixture_t* fixture);

/*************************************************************************************************/
/**
 * \brief Exercises successful absence of standard input in a detached process.
 *
 * \retval None   Optional input stays unavailable through repeated successful closure.
 */
static FORCE_INLINE void CheckAbsentInput(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  No console mutation occurs before confirming that this is its sole process.
 *  A pipe tests real redirected input. GetCurrentProcess supplies the Win32 -1
 *  pseudo-handle identity without constructing an invalid pointer in C.
 */
static FORCE_INLINE void InitializeFixture(ConsoleFixture_t* fixture)
{
    DWORD  process_count;
    DWORD  process;
    HANDLE pipe_input;
    HANDLE pipe_output;
    DWORD  mode;

    LOGASSERT(NULL != fixture);

    process_count = GetConsoleProcessList(&process, 1);
    LOGASSERT(1 == process_count);
    LOGASSERT(GetCurrentProcessId() == process);
    fixture->input = GetStdHandle(STD_INPUT_HANDLE);
    LOGASSERT(GetConsoleMode(fixture->input, &fixture->originalMode));
    LOGASSERT(FlushConsoleInputBuffer(fixture->input));
    LOGASSERT(CreatePipe(&pipe_input, &pipe_output, NULL, 0));
    LOGASSERT(OS_Console_Initialize(&fixture->keyboard));
    LOGASSERT(OS_Console_IsAvailable(&fixture->keyboard));
    LOGASSERT(GetConsoleMode(fixture->input, &mode));
    LOGASSERT(!(mode & ENABLE_PROCESSED_INPUT));
    LOGASSERT(!(mode & ENABLE_QUICK_EDIT_MODE));
    LOGASSERT(mode & ENABLE_EXTENDED_FLAGS);

    LOGASSERT(SetStdHandle(STD_INPUT_HANDLE, pipe_input));
    LOGASSERT(OS_Console_Initialize(&fixture->redirected));
    LOGASSERT(SetStdHandle(STD_INPUT_HANDLE, GetCurrentProcess()));
    SetLastError(ERROR_SUCCESS);
    LOGASSERT(!OS_Console_Initialize(&fixture->invalid));
    fixture->initialError = OS_Console_GetError(&fixture->invalid);
    LOGASSERT(0 != fixture->initialError);
    LOGASSERT(SetStdHandle(STD_INPUT_HANDLE, fixture->input));
    LOGASSERT(CloseHandle(pipe_input));
    LOGASSERT(CloseHandle(pipe_output));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Field comparisons avoid depending on uninitialized structure padding.
 */
static FORCE_INLINE void CheckPreservedKey(OS_Console_t* console, OS_Console_ReadResult_t expected)
{
    OS_Console_Key_t        key;
    OS_Console_ReadResult_t result;

    key.code        = OS_CONSOLE_KEY_SPACE;
    key.modifiers   = OS_CONSOLE_MODIFIER_ALT;
    key.repeatCount = TEST_RETAIN_COUNT;
    key.pressed     = TRUE;
    result          = OS_Console_ReadKey(console, &key);
    LOGASSERT(expected == result);
    LOGASSERT(OS_CONSOLE_KEY_SPACE == key.code);
    LOGASSERT(OS_CONSOLE_MODIFIER_ALT == key.modifiers);
    LOGASSERT(TEST_RETAIN_COUNT == key.repeatCount);
    LOGASSERT(key.pressed);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Two queued records must require two API calls. A native focus event is consumed
 *  as IGNORED, and an empty queue returns immediately without changing output.
 */
static FORCE_INLINE void CheckInput(ConsoleFixture_t* fixture)
{
    INPUT_RECORD            records[2];
    DWORD                   written;
    OS_Console_ReadResult_t result;
    OS_Console_Key_t        key;
    DWORD                   remaining;

    CheckPreservedKey(&fixture->keyboard, OS_CONSOLE_READ_EMPTY);
    CheckPreservedKey(&fixture->redirected, OS_CONSOLE_READ_EMPTY);
    CheckPreservedKey(&fixture->invalid, OS_CONSOLE_READ_FAILED);
    LOGASSERT(!OS_Console_IsAvailable(&fixture->redirected));
    LOGASSERT(!OS_Console_IsAvailable(&fixture->invalid));

    CRT_Memory_Set(records, 0, sizeof(records));
    records[0].EventType                        = KEY_EVENT;
    records[0].Event.KeyEvent.bKeyDown          = TRUE;
    records[0].Event.KeyEvent.wRepeatCount      = TEST_REPEAT_COUNT;
    records[0].Event.KeyEvent.wVirtualKeyCode   = TEST_NATIVE_A;
    records[0].Event.KeyEvent.dwControlKeyState = SHIFT_PRESSED | RIGHT_CTRL_PRESSED | CAPSLOCK_ON;
    records[1].EventType                        = KEY_EVENT;
    records[1].Event.KeyEvent.bKeyDown          = FALSE;
    records[1].Event.KeyEvent.wRepeatCount      = 1;
    records[1].Event.KeyEvent.wVirtualKeyCode   = TEST_NATIVE_F1;
    records[1].Event.KeyEvent.dwControlKeyState = RIGHT_ALT_PRESSED;
    LOGASSERT(WriteConsoleInput(fixture->input, records, 2, &written));
    LOGASSERT(2 == written);
    result = OS_Console_ReadKey(&fixture->keyboard, &key);
    LOGASSERT(OS_CONSOLE_READ_KEY == result);
    LOGASSERT(OS_CONSOLE_KEY_A == key.code);
    LOGASSERT((OS_CONSOLE_MODIFIER_SHIFT | OS_CONSOLE_MODIFIER_CONTROL) == key.modifiers);
    LOGASSERT(TEST_REPEAT_COUNT == key.repeatCount);
    LOGASSERT(key.pressed);
    LOGASSERT(GetNumberOfConsoleInputEvents(fixture->input, &remaining));
    LOGASSERT(1 == remaining);
    result = OS_Console_ReadKey(&fixture->keyboard, &key);
    LOGASSERT(OS_CONSOLE_READ_KEY == result);
    LOGASSERT(OS_CONSOLE_KEY_UNKNOWN == key.code);
    LOGASSERT(OS_CONSOLE_MODIFIER_ALT == key.modifiers);
    LOGASSERT(1 == key.repeatCount);
    LOGASSERT(!key.pressed);

    CRT_Memory_Set(records, 0, sizeof(records));
    records[0].EventType                  = FOCUS_EVENT;
    records[0].Event.FocusEvent.bSetFocus = FALSE;
    LOGASSERT(WriteConsoleInput(fixture->input, records, 1, &written));
    LOGASSERT(1 == written);
    CheckPreservedKey(&fixture->keyboard, OS_CONSOLE_READ_IGNORED);
    CheckPreservedKey(&fixture->keyboard, OS_CONSOLE_READ_EMPTY);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Mode restoration is compared against the original native value. A second close
 *  and a post-close read must neither reopen input nor erase an earlier error.
 */
static FORCE_INLINE void CheckClosure(ConsoleFixture_t* fixture)
{
    DWORD mode;

    LOGASSERT(OS_Console_TryClose(&fixture->keyboard));
    LOGASSERT(GetConsoleMode(fixture->input, &mode));
    LOGASSERT(fixture->originalMode == mode);
    LOGASSERT(!OS_Console_IsAvailable(&fixture->keyboard));
    LOGASSERT(OS_Console_TryClose(&fixture->keyboard));
    CheckPreservedKey(&fixture->keyboard, OS_CONSOLE_READ_EMPTY);
    LOGASSERT(0 == OS_Console_GetError(&fixture->keyboard));
    LOGASSERT(OS_Console_TryClose(&fixture->redirected));
    LOGASSERT(OS_Console_TryClose(&fixture->redirected));
    LOGASSERT(0 == OS_Console_GetError(&fixture->redirected));
    LOGASSERT(OS_Console_TryClose(&fixture->invalid));
    LOGASSERT(OS_Console_TryClose(&fixture->invalid));
    CheckPreservedKey(&fixture->invalid, OS_CONSOLE_READ_FAILED);
    LOGASSERT(fixture->initialError == OS_Console_GetError(&fixture->invalid));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  DETACHED_PROCESS supplies absent handles at process creation. The fixture
 *  verifies console absence before initializing its optional input owner.
 */
static FORCE_INLINE void CheckAbsentInput(void)
{
    DWORD        process_count;
    DWORD        process;
    OS_Console_t console;

    process_count = GetConsoleProcessList(&process, 1);
    LOGASSERT(0 == process_count);
    LOGASSERT(OS_Console_Initialize(&console));
    LOGASSERT(!OS_Console_IsAvailable(&console));
    CheckPreservedKey(&console, OS_CONSOLE_READ_EMPTY);
    LOGASSERT(OS_Console_TryClose(&console));
    LOGASSERT(OS_Console_TryClose(&console));
    CheckPreservedKey(&console, OS_CONSOLE_READ_EMPTY);
    LOGASSERT(0 == OS_Console_GetError(&console));
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  All storage is automatic and all native setup precedes input exercise.
 */
uint32_t OS_Application_Main(void)
{
    uint32_t         unit_count;
    uint16_t         mode;
    bool_t           is_keyboard;
    bool_t           is_absent;
    bool_t           valid_mode;
    ConsoleFixture_t fixture;

    LOGASSERT(OS_Process_ReadArgument(&unit_count, &mode, 1, 1));
    LOGASSERT(1 == unit_count);
    is_keyboard = (TEST_MODE_KEYBOARD == mode);
    is_absent   = (TEST_MODE_ABSENT == mode);
    valid_mode  = is_keyboard || is_absent;
    LOGASSERT(valid_mode);
    if (is_keyboard)
    {
        InitializeFixture(&fixture);
        CheckInput(&fixture);
        CheckClosure(&fixture);
    }
    else
    {
        CheckAbsentInput();
    }
    return 0;
}
