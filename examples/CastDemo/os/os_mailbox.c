/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * os_mailbox.h defines single-participant ownership and payload-borrow lifetimes.
 * os_atomic.h defines the memory ordering at the exchange boundary.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_mailbox.h"
#include "os/os_compiler.h"
#include "os/os_atomic.h"
#include "os/os_assert.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define MAILBOX_INITIAL_READER (0)
#define MAILBOX_INITIAL_MIDDLE (1)
#define MAILBOX_INITIAL_WRITER (2)
#define MAILBOX_INDEX_MASK     (3)
#define MAILBOX_PENDING        (4)

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

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Initial roles are distinct; no payload bytes are inspected or cleared.
 */
extern FORCE_INLINE void OS_Mailbox_Initialize(OS_Mailbox_t* mailbox)
{
    LOGASSERT(NULL != mailbox);

    OS_Atomic_Initialize32(&mailbox->middle, MAILBOX_INITIAL_MIDDLE);
    mailbox->writer   = MAILBOX_INITIAL_WRITER;
    mailbox->reader   = MAILBOX_INITIAL_READER;
    mailbox->readable = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The query touches only producer-owned metadata, never the consumer's fields.
 */
extern FORCE_INLINE uint32_t OS_Mailbox_GetWriteIndex(const OS_Mailbox_t* mailbox)
{
    uint32_t writer;

    LOGASSERT(NULL != mailbox);
    writer = mailbox->writer;
    switch (writer)
    {
        case MAILBOX_INITIAL_READER: return MAILBOX_INITIAL_READER;
        case MAILBOX_INITIAL_MIDDLE: return MAILBOX_INITIAL_MIDDLE;
        case MAILBOX_INITIAL_WRITER: return MAILBOX_INITIAL_WRITER;
        default:
            LOGASSERT(FALSE);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Readability belongs to the consumer and remains set across empty observations.
 */
extern FORCE_INLINE uint32_t OS_Mailbox_GetReadIndex(const OS_Mailbox_t* mailbox)
{
    uint32_t reader;

    LOGASSERT(NULL != mailbox);
    reader = mailbox->reader;
    LOGASSERT(mailbox->readable);
    switch (reader)
    {
        case MAILBOX_INITIAL_READER: return MAILBOX_INITIAL_READER;
        case MAILBOX_INITIAL_MIDDLE: return MAILBOX_INITIAL_MIDDLE;
        case MAILBOX_INITIAL_WRITER: return MAILBOX_INITIAL_WRITER;
        default:
            LOGASSERT(FALSE);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Release publishes completed payload writes; acquire prevents reuse of a
 *  former reader slot before that consumer has finished its previous borrow.
 */
extern FORCE_INLINE void OS_Mailbox_Publish(OS_Mailbox_t* mailbox)
{
    uint32_t writer;
    uint32_t previous;
    uint32_t index;

    writer   = OS_Mailbox_GetWriteIndex(mailbox);
    previous = OS_Atomic_Exchange32(&mailbox->middle, writer | MAILBOX_PENDING);
    index    = previous & MAILBOX_INDEX_MASK;
    LOGASSERT(OS_MAILBOX_SLOT_COUNT > index);
    mailbox->writer = index;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  After observing pending, only this consumer can clear the flag. Producer
 *  exchanges may replace the value but leave it pending. The acquired reader
 *  slot cannot return to the producer until this consumer exchanges it away.
 */
extern FORCE_INLINE bool_t OS_Mailbox_TryTake(OS_Mailbox_t* mailbox)
{
    uint32_t publication;
    bool_t   available;
    uint32_t previous;
    uint32_t index;

    LOGASSERT(NULL != mailbox);

    publication = OS_Atomic_Load32(&mailbox->middle);
    available   = !!(publication & MAILBOX_PENDING);
    if (available)
    {
        previous = OS_Atomic_Exchange32(&mailbox->middle, mailbox->reader);
        index    = previous & MAILBOX_INDEX_MASK;
        LOGASSERT(previous & MAILBOX_PENDING);
        LOGASSERT(OS_MAILBOX_SLOT_COUNT > index);
        mailbox->reader   = index;
        mailbox->readable = TRUE;
    }

    return available;
}
