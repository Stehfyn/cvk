/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Caller-owned typed models follow C:/dev/xml/include/utl/utl_arinc661.h.
 * CastSession_Run publishes on one callback thread; the GUI consumes on its timer.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_compiler.h"
#include "cast_events.h"
#include "cast_session.h"
#include "os/os_assert.h"
#include "os/os_atomic.h"
#include "os/os_mailbox.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define CAST_EVENTS_TERMINAL_EMPTY    (0)    ///< No terminal event has been published.
#define CAST_EVENTS_TERMINAL_PENDING  (1)    ///< Stable terminal reason is available.
#define CAST_EVENTS_TERMINAL_CONSUMED (2)    ///< Terminal reason was consumed once.
#define CAST_EVENTS_TERMINATOR        ('\0') ///< Byte-oriented text terminator.

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
 * \brief Copies bounded optional event text and appends its terminator.
 *
 * \par Requirements:
 *  - 001 Output shall provide CAST_EVENTS_TEXT_CAPACITY writable bytes.
 *  - 002 Non-NULL input shall be terminated or provide capacity minus one readable bytes.
 *  - 003 Output and non-NULL input shall not overlap.
 *
 * \param[out] output   Non-NULL owned event-text storage
 * \param[in]  input    Optional readable text; NULL means empty
 *
 * \retval     None No value is returned.
 */
static FORCE_INLINE void CopyText(char_t output[CAST_EVENTS_TEXT_CAPACITY], const char_t* input);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The retained byte-oriented truncation policy copies at most capacity minus one
 *  bytes. It performs no formatting, allocation, Unicode conversion or I/O.
 */
static FORCE_INLINE void CopyText(char_t output[CAST_EVENTS_TEXT_CAPACITY], const char_t* input)
{
    uint32_t index;

    index = 0;
    if (NULL != input)
    {
        for (index = 0; index < (CAST_EVENTS_TEXT_CAPACITY - 1); ++index)
        {
            if (CAST_EVENTS_TERMINATOR == input[index])
            {
                break;
            }
            output[index] = input[index];
        }
    }
    output[index] = CAST_EVENTS_TERMINATOR;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Initialize only ownership metadata; no uncommitted payload may be read.
 */
extern FORCE_INLINE void CastEvents_Initialize(CastEvents_t* events)
{
    LOGASSERT(NULL != events);

    CastEvents_Begin(events, 0);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All role indexes and publication words are reset while no other thread can
 *  access them. Generation and ended are producer-owned after this operation.
 */
extern FORCE_INLINE void CastEvents_Begin(CastEvents_t* events, uint32_t generation)
{
    LOGASSERT(NULL != events);

    OS_Mailbox_Initialize(&events->status.channel);
    OS_Mailbox_Initialize(&events->log.channel);
    OS_Atomic_Initialize32(&events->terminal, CAST_EVENTS_TERMINAL_EMPTY);
    events->generation = generation;
    events->ended      = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Write only the producer-owned back slot, then exchange ownership with the middle
 *  slot. Terminal reason has one immutable publication and cannot be overwritten.
 *  Supported event categories select their own publication path.
 */
extern FORCE_INLINE bool_t CastEvents_Publish(CastEvents_t*     events,
                          uint32_t          generation,
                          CastEvent         event,
                          const CastStatus* status,
                          const char_t*     text)
{
    bool_t   is_current;
    bool_t   can_publish;
    bool_t   accepted;
    bool_t   is_log;
    bool_t   is_status;
    bool_t   is_terminal;
    uint32_t slot;

    LOGASSERT(NULL != events);

    is_current  = (generation == events->generation);
    can_publish = is_current && (!events->ended);
    accepted    = FALSE;
    if (can_publish)
    {
        is_log      = (CAST_EVENT_LOG == event);
        is_status   = (CAST_EVENT_STATUS == event);
        is_terminal = (CAST_EVENT_ENDED == event);
        if (is_log)
        {
            slot = OS_Mailbox_GetWriteIndex(&events->log.channel);
            CopyText(events->log.values[slot], text);
            OS_Mailbox_Publish(&events->log.channel);
            accepted = TRUE;
        }
        else if (is_status)
        {
            if (NULL != status)
            {
                slot = OS_Mailbox_GetWriteIndex(&events->status.channel);
                events->status.values[slot] = (*status);
                OS_Mailbox_Publish(&events->status.channel);
                accepted = TRUE;
            }
        }
        else if (is_terminal)
        {
            CopyText(events->reason, text);
            events->ended = TRUE;
            OS_Atomic_Store32(&events->terminal, CAST_EVENTS_TERMINAL_PENDING);
            accepted = TRUE;
        }
        else
        {
            /* Unsupported categories cannot publish a payload. */
        }
    }

    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Acquire terminal first. If present, its release guarantees the producer's final
 *  status/log publications precede the subsequent channel reads. Otherwise a racing
 *  terminal event remains pending for the next call. No category uses a version counter.
 */
extern FORCE_INLINE bool_t CastEvents_Take(CastEvents_t* events, CastEvents_Snapshot_t* snapshot)
{
    uint32_t terminal;
    uint32_t present;
    bool_t   has_terminal;
    bool_t   has_status;
    uint32_t slot;
    bool_t   has_log;
    bool_t   available;

    LOGASSERT(NULL != events);
    LOGASSERT(NULL != snapshot);

    terminal     = OS_Atomic_Load32(&events->terminal);
    present      = 0;
    has_terminal = (CAST_EVENTS_TERMINAL_PENDING == terminal);
    if (has_terminal)
    {
        OS_Atomic_Store32(&events->terminal, CAST_EVENTS_TERMINAL_CONSUMED);
        CopyText(snapshot->reason, events->reason);
        present |= CAST_EVENTS_ENDED;
    }
    has_status = OS_Mailbox_TryTake(&events->status.channel);
    if (has_status)
    {
        slot = OS_Mailbox_GetReadIndex(&events->status.channel);
        snapshot->status = events->status.values[slot];
        present |= CAST_EVENTS_STATUS;
    }
    has_log = OS_Mailbox_TryTake(&events->log.channel);
    if (has_log)
    {
        slot = OS_Mailbox_GetReadIndex(&events->log.channel);
        CopyText(snapshot->log, events->log.values[slot]);
        present |= CAST_EVENTS_LOG;
    }
    available = !!present;
    if (available)
    {
        snapshot->present = present;
    }

    return available;
}
