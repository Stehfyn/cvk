#ifndef CAST_COMMANDS_H
#define CAST_COMMANDS_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Application queue policy for commands declared by cast_session.h.
 * The queue does not implement Cast wire messages; that belongs to the protocol layer.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cast_session.h"
#include "os/os_types.h"
#include "os/os_queue.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define CAST_COMMAND_CAPACITY (8) ///< Application queue capacity, not a protocol limit.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief One value-owned command; no entry retains external storage.
 */
typedef struct CastQueuedCommand_s
{
    CastCommand command; ///< Transport or position action.
    float64_t   seconds; ///< Timeline seconds for SEEK/SCRUB; otherwise unused.
} CastQueuedCommand_t;

/*************************************************************************************************/
/**
 * \brief Caller-owned bounded command storage.
 *
 * \par Details:
 *  Initialize with Reset before use. The caller serializes every operation.
 *  The queue holds at most capacity entries in FIFO order without shifting on Take.
 *  Stop remains latched until Reset, even after its one pending event is taken.
 */
typedef struct CastCommands_s
{
    CastQueuedCommand_t entries[CAST_COMMAND_CAPACITY]; ///< FIFO entries.
    OS_Queue_t          queue;                          ///< Fixed storage bound to entries.
    bool_t              stopped;                        ///< Reject ordinary commands until Reset.
    bool_t              stopPending;                    ///< Stop still needs to be delivered.
} CastCommands_t;

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
 * \brief Initializes or reuses an empty command queue.
 *
 * \par Requirements:
 *  - 001 This function shall clear the occupied count and both stop flags.
 *  - 002 This function shall not allocate, free, wait or retain external resources.
 *  - 003 The caller shall exclude concurrent access during reset.
 *  - 004 Missing storage is caller misuse and shall assert in every build.
 *
 * \param[out] commands   Non-NULL caller-owned storage
 *
 * \retval     None No value is returned.
 */
void CastCommands_Reset(CastCommands_t* commands);

/*************************************************************************************************/
/**
 * \brief Admits a command according to fixed-capacity gesture and Stop policy.
 *
 * \par Details:
 *  Adjacent position updates coalesce, except a SCRUB after a final SEEK.
 *  At capacity, position commands replace the oldest position; a final SEEK may
 *  displace the oldest transport command if there is no queued position.
 *  NaN, infinity and negative position values are invalid. Nonposition commands
 *  do not use seconds. Repeated Stop succeeds without adding another Stop event.
 *
 * \par Requirements:
 *  - 001 Stop shall clear ordinary entries and remain latched until Reset.
 *  - 002 Unsupported, invalid and post-stop ordinary commands shall fail unchanged.
 *  - 003 No operation shall inspect or move more than CAST_COMMAND_CAPACITY entries.
 *  - 004 This function shall allocate nothing, call no CRT and perform no I/O.
 *
 * \param[in,out] commands   Non-NULL initialized queue, exclusively accessed
 * \param[in]     command    Action to admit
 * \param[in]     seconds    Playback position in seconds for SEEK/SCRUB
 *
 * \retval        TRUE    The command was accepted, including an already-latched Stop.
 * \retval        FALSE   The command was rejected; queue state is unchanged.
 */
bool_t CastCommands_Push(CastCommands_t* commands, CastCommand command, float64_t seconds);

/*************************************************************************************************/
/**
 * \brief Takes one pending command into caller-owned output.
 *
 * \par Requirements:
 *  - 001 A pending Stop shall be delivered once before ordinary commands.
 *  - 002 Ordinary commands shall be removed in FIFO order.
 *  - 003 An empty queue shall leave command unchanged.
 *  - 004 This function shall use bounded value copies without allocation or I/O.
 *
 * \param[in,out] commands   Non-NULL initialized queue, exclusively accessed
 * \param[out]    command    Non-NULL output disjoint from queue storage
 *
 * \retval        TRUE    One command was published.
 * \retval        FALSE   No command was available.
 */
bool_t CastCommands_Take(CastCommands_t* commands, CastQueuedCommand_t* command);

#endif /* CAST_COMMANDS_H */
