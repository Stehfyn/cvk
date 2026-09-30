/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Application command admission and ordering policy.
 * Public storage and ownership contracts are declared in cast_commands.h.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_compiler.h"
#include "cast_commands.h"
#include "cast_session.h"
#include "os/os_assert.h"
#include "os/os_queue.h"
#include "os/os_types.h"

#include <float.h>
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
 * \brief Classifies commands that carry a playback position.
 *
 * \param[in] command   Candidate command value
 *
 * \retval    TRUE    SEEK or SCRUB.
 * \retval    FALSE   Another value.
 */
static FORCE_INLINE bool_t IsPosition(CastCommand command);

/*************************************************************************************************/
/**
 * \brief Checks whether the queue implements the command value.
 *
 * \param[in] command   Candidate command value
 *
 * \retval    TRUE    One of the six supported command values.
 * \retval    FALSE   An unsupported value.
 */
static FORCE_INLINE bool_t IsSupported(CastCommand command);

/*************************************************************************************************/
/**
 * \brief Determines whether a new position supersedes the queue's tail.
 *
 * \param[in] commands   Initialized queue with no more than capacity entries
 * \param[in] command    Candidate command
 *
 * \retval    TRUE    The tail can be replaced without losing a previous final seek.
 * \retval    FALSE   The tail must remain.
 */
static FORCE_INLINE bool_t CanReplaceTail(const CastCommands_t* commands, CastCommand command);

/*************************************************************************************************/
/**
 * \brief Makes room for a position when the queue is saturated.
 *
 * \param[in,out] commands   Initialized queue
 * \param[in]     command    Command whose priority controls eviction
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void MakePositionRoom(CastCommands_t* commands, CastCommand command);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsPosition(CastCommand command)
{
    bool_t is_seek;
    bool_t is_scrub;

    is_seek  = (CAST_COMMAND_SEEK == command);
    is_scrub = (CAST_COMMAND_SCRUB == command);

    return is_seek || is_scrub;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Transport, position and stop commands form the complete supported set.
 */
static FORCE_INLINE bool_t IsSupported(CastCommand command)
{
    bool_t is_play;
    bool_t is_pause;
    bool_t is_toggle;
    bool_t is_transport;
    bool_t is_position;
    bool_t is_stop;
    bool_t is_supported;

    is_play      = (CAST_COMMAND_PLAY == command);
    is_pause     = (CAST_COMMAND_PAUSE == command);
    is_toggle    = (CAST_COMMAND_TOGGLE == command);
    is_transport = is_play || is_pause || is_toggle;
    is_position  = IsPosition(command);
    is_stop      = (CAST_COMMAND_STOP == command);
    is_supported = is_transport || is_position || is_stop;

    return is_supported;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A SCRUB after a final SEEK starts a new gesture and must retain that SEEK.
 */
static FORCE_INLINE bool_t CanReplaceTail(const CastCommands_t* commands, CastCommand command)
{
    uint32_t            count;
    bool_t              has_tail;
    bool_t              is_position;
    bool_t              can_replace;
    bool_t              has_entry;
    CastQueuedCommand_t tail;
    bool_t              tail_is_position;
    bool_t              starts_new_gesture;

    count       = OS_Queue_GetEntryCount(&commands->queue);
    has_tail    = !!count;
    is_position = IsPosition(command);
    can_replace = FALSE;
    if (has_tail && is_position)
    {
        has_entry = OS_Queue_GetEntry(&tail, &commands->queue, count - 1);
        LOGASSERT(has_entry);
        tail_is_position   = IsPosition(tail.command);
        starts_new_gesture = (CAST_COMMAND_SCRUB == command) && (CAST_COMMAND_SEEK == tail.command);
        can_replace        = tail_is_position && (!starts_new_gesture);
    }

    return can_replace;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A final SEEK may displace the oldest transport command when no queued
 *  position is available to replace. SCRUB has no such priority.
 */
static FORCE_INLINE void MakePositionRoom(CastCommands_t* commands, CastCommand command)
{
    bool_t              is_position;
    uint32_t            count;
    bool_t              is_full;
    uint32_t            remove;
    uint32_t            index;
    bool_t              has_entry;
    CastQueuedCommand_t entry;
    bool_t              entry_is_position;
    bool_t              found_position;
    bool_t              is_final_seek;
    bool_t              can_remove;
    bool_t              removed;

    is_position = IsPosition(command);
    count       = OS_Queue_GetEntryCount(&commands->queue);
    is_full     = (CAST_COMMAND_CAPACITY == count);
    if (is_position && is_full)
    {
        remove = CAST_COMMAND_CAPACITY;
        for (index = 0; index < count; ++index)
        {
            has_entry = OS_Queue_GetEntry(&entry, &commands->queue, index);
            LOGASSERT(has_entry);
            entry_is_position = IsPosition(entry.command);
            if (entry_is_position)
            {
                remove = index;
                break;
            }
        }
        found_position = (remove < count);
        is_final_seek  = (CAST_COMMAND_SEEK == command);
        if ((!found_position) && is_final_seek)
        {
            remove = 0;
        }
        can_remove = (remove < count);
        if (can_remove)
        {
            removed = OS_Queue_Remove(&commands->queue, remove);
            LOGASSERT(removed);
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Old entry bytes become unoccupied; no external resource is owned by an entry.
 */
void CastCommands_Reset(CastCommands_t* commands)
{
    LOGASSERT(NULL != commands);

    OS_Queue_Initialize(&commands->queue, commands->entries, sizeof(CastQueuedCommand_t), CAST_COMMAND_CAPACITY);
    commands->stopped     = FALSE;
    commands->stopPending = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Stop clears queued work and latches once. Other commands are validated before
 *  coalescing or capacity handling; rejected commands leave the queue unchanged.
 */
bool_t CastCommands_Push(CastCommands_t* commands, CastCommand command, float64_t seconds)
{
    bool_t              is_stop;
    bool_t              is_supported;
    bool_t              is_position;
    bool_t              is_nonnegative;
    bool_t              is_finite;
    bool_t              position_is_valid;
    bool_t              can_accept;
    bool_t              accepted;
    bool_t              replace_tail;
    CastQueuedCommand_t entry;
    uint32_t            count;

    LOGASSERT(NULL != commands);

    is_stop           = (CAST_COMMAND_STOP == command);
    is_supported      = IsSupported(command);
    is_position       = IsPosition(command);
    is_nonnegative    = (0.0 <= seconds);
    is_finite         = (DBL_MAX >= seconds);
    position_is_valid = (!is_position) || (is_nonnegative && is_finite);
    can_accept        = is_supported && position_is_valid && (!commands->stopped);
    accepted          = FALSE;
    if (is_stop)
    {
        if (!commands->stopped)
        {
            OS_Queue_Reset(&commands->queue);
            commands->stopped     = TRUE;
            commands->stopPending = TRUE;
        }
        accepted = TRUE;
    }
    else if (can_accept)
    {
        replace_tail  = CanReplaceTail(commands, command);
        entry.command = command;
        entry.seconds = seconds;
        if (replace_tail)
        {
            count    = OS_Queue_GetEntryCount(&commands->queue);
            accepted = OS_Queue_Replace(&commands->queue, count - 1, &entry);
            LOGASSERT(accepted);
        }
        else
        {
            MakePositionRoom(commands, command);
            accepted = OS_Queue_Write(&commands->queue, &entry);
        }
    }
    else
    {
        /* Invalid or post-stop commands leave the queue unchanged. */
    }

    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Publishes a pending Stop before ordinary entries. Taking an ordinary command
 *  advances the ring without moving survivors; an empty queue preserves output.
 */
bool_t CastCommands_Take(CastCommands_t* commands, CastQueuedCommand_t* command)
{
    bool_t available;

    LOGASSERT(NULL != commands);
    LOGASSERT(NULL != command);

    if (commands->stopPending)
    {
        command->command      = CAST_COMMAND_STOP;
        command->seconds      = 0.0;
        commands->stopPending = FALSE;
        available             = TRUE;
    }
    else
    {
        available = OS_Queue_Read(command, &commands->queue);
    }

    return available;
}
