#ifndef OS_CONSOLE_H
#define OS_CONSOLE_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Caller-owned console keyboard input. Native implementations are selected by
 * the build. Key identities, modifiers and repeat counts are independent of text
 * encoding and application commands. Control-signal registration is not supplied
 * by this interface; native Ctrl+Break/close/shutdown integration remains open.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define OS_CONSOLE_MODIFIER_SHIFT   (1) ///< Either shift key is held.
#define OS_CONSOLE_MODIFIER_CONTROL (2) ///< Either control key is held.
#define OS_CONSOLE_MODIFIER_ALT     (4) ///< Either alt key is held.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Portable key identities used by the console input subset.
 */
typedef enum OS_Console_KeyCode_e
{
    OS_CONSOLE_KEY_UNKNOWN,
    OS_CONSOLE_KEY_CANCEL,
    OS_CONSOLE_KEY_ESCAPE,
    OS_CONSOLE_KEY_SPACE,
    OS_CONSOLE_KEY_A,
    OS_CONSOLE_KEY_B,
    OS_CONSOLE_KEY_C,
    OS_CONSOLE_KEY_D,
    OS_CONSOLE_KEY_E,
    OS_CONSOLE_KEY_F,
    OS_CONSOLE_KEY_G,
    OS_CONSOLE_KEY_H,
    OS_CONSOLE_KEY_I,
    OS_CONSOLE_KEY_J,
    OS_CONSOLE_KEY_K,
    OS_CONSOLE_KEY_L,
    OS_CONSOLE_KEY_M,
    OS_CONSOLE_KEY_N,
    OS_CONSOLE_KEY_O,
    OS_CONSOLE_KEY_P,
    OS_CONSOLE_KEY_Q,
    OS_CONSOLE_KEY_R,
    OS_CONSOLE_KEY_S,
    OS_CONSOLE_KEY_T,
    OS_CONSOLE_KEY_U,
    OS_CONSOLE_KEY_V,
    OS_CONSOLE_KEY_W,
    OS_CONSOLE_KEY_X,
    OS_CONSOLE_KEY_Y,
    OS_CONSOLE_KEY_Z
} OS_Console_KeyCode_t;

/*************************************************************************************************/
/**
 * \brief Result of one bounded input-record attempt.
 */
typedef enum OS_Console_ReadResult_e
{
    OS_CONSOLE_READ_EMPTY,   ///< No record is available, or the owner has no console input.
    OS_CONSOLE_READ_KEY,     ///< A complete key record was published.
    OS_CONSOLE_READ_IGNORED, ///< A non-key record was consumed without changing key output.
    OS_CONSOLE_READ_FAILED   ///< Native input failed; GetError retains its cause.
} OS_Console_ReadResult_t;

/*************************************************************************************************/
/**
 * \brief One key transition; text translation belongs to a different interface.
 */
typedef struct OS_Console_Key_s
{
    OS_Console_KeyCode_t code;        ///< Normalized key identity, including UNKNOWN.
    uint32_t             modifiers;   ///< OS_CONSOLE_MODIFIER bits only.
    uint32_t             repeatCount; ///< Native repeat count, including zero for malformed injected input.
    bool_t               pressed;     ///< TRUE for key-down; FALSE for key-up.
} OS_Console_Key_t;

/*************************************************************************************************/
/**
 * \brief Startup-owned input handle and restoration obligation.
 *
 * \par Details:
 *  Fields belong to the selected backend. Calls are serialized by one owner;
 *  do not copy or move live state. The caller exclusively owns console input-mode
 *  changes through successful closure. No callback borrows this object.
 */
typedef struct OS_Console_s
{
    void*    input;          ///< Owned native input identity, retained through failed release.
    uint32_t originalMode;   ///< Exact input mode to restore before handle closure.
    uint32_t error;          ///< First native failure, retained after closure.
    bool_t   available;      ///< Keyboard input is active.
    bool_t   restorePending; ///< The startup mode change still requires restoration.
    bool_t   closed;         ///< Successful idempotent closure has completed.
} OS_Console_t;

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
 * \brief Establishes keyboard input and its restoration state during startup.
 *
 * \par Requirements:
 *  - 001 Missing owner storage shall assert; storage shall be fresh or completely closed.
 *  - 002 A console input handle shall be duplicated only during this startup operation.
 *  - 003 Redirected or absent standard input shall succeed with keyboard input unavailable.
 *  - 004 Keyboard Ctrl+C shall be delivered as input, not dispatched through a context-free callback.
 *  - 005 Failure shall retain all acquired state for TryClose; no global owner shall be introduced.
 *  - 006 The caller shall exclude other input readers and mode changes for this owner's lifetime.
 *
 * \param[out] console   Required stable lifetime record
 *
 * \retval     TRUE    Startup succeeded; IsAvailable distinguishes optional keyboard presence.
 * \retval     FALSE   Native startup failed; TryClose remains required.
 */
bool_t OS_Console_Initialize(OS_Console_t* console);

/*************************************************************************************************/
/**
 * \brief Gets the first native failure without changing ownership.
 *
 * \param[in] console   Initialized lifetime record
 *
 * \retval    0         No native failure has been recorded.
 * \retval    nonzero   First native failure code.
 */
uint32_t OS_Console_GetError(const OS_Console_t* console);

/*************************************************************************************************/
/**
 * \brief Queries whether keyboard input is available before closure.
 *
 * \param[in] console   Initialized lifetime record
 *
 * \retval    TRUE    Input can be polled.
 * \retval    FALSE   Input is absent, failed or closing.
 */
bool_t OS_Console_IsAvailable(const OS_Console_t* console);

/*************************************************************************************************/
/**
 * \brief Attempts to consume exactly one native input record without waiting.
 *
 * \par Requirements:
 *  - 001 Missing owner or key output shall assert; the caller shall keep their storage disjoint.
 *  - 002 The call shall use a native non-waiting read, never a check-then-blocking-read sequence.
 *  - 003 Only READ_KEY shall change caller output; EMPTY, IGNORED and FAILED shall preserve it.
 *  - 004 The call shall acquire no application storage, handles or threads.
 *  - 005 Native provider timing and allocation behavior remain separate verification obligations.
 *
 * \param[in,out] console   Initialized exclusive input owner
 * \param[out]    key       Disjoint caller-owned result storage
 *
 * \retval        OS_CONSOLE_READ_KEY       Complete key transition published.
 * \retval        OS_CONSOLE_READ_IGNORED   A non-key event was consumed.
 * \retval        OS_CONSOLE_READ_EMPTY     No record or no available keyboard.
 * \retval        OS_CONSOLE_READ_FAILED    Native failure is retained in the owner.
 */
OS_Console_ReadResult_t OS_Console_ReadKey(OS_Console_t* console, OS_Console_Key_t* key);

/*************************************************************************************************/
/**
 * \brief Restores input mode and releases the startup handle, retaining failed work.
 *
 * \par Requirements:
 *  - 001 The caller shall exclude ReadKey and all other owner access during closure.
 *  - 002 Input shall become unavailable on the first attempt.
 *  - 003 Mode restoration shall succeed before the owned handle is closed.
 *  - 004 Each failed native obligation shall remain available for a later retry.
 *  - 005 Repeated calls after success shall perform no native operations.
 *
 * \param[in,out] console   Initialized lifetime record, including failed startup
 *
 * \retval        TRUE    Restoration and release are complete.
 * \retval        FALSE   An obligation remains; retain the owner and retry.
 */
bool_t OS_Console_TryClose(OS_Console_t* console);

#endif /* OS_CONSOLE_H */
