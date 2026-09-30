#ifndef CAST_EVENTS_H
#define CAST_EVENTS_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Caller-owned typed models follow C:/dev/xml/include/utl/utl_arinc661.h.
 * CastSession_Run publishes on one callback thread; the GUI consumes on its timer.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cast_session.h"
#include "os/os_atomic.h"
#include "os/os_mailbox.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define CAST_EVENTS_TEXT_CAPACITY (1600)                  ///< Fixed event-text extent, including terminator.
#define CAST_EVENTS_BUFFER_COUNT  (OS_MAILBOX_SLOT_COUNT) ///< Disjoint snapshot roles.
#define CAST_EVENTS_STATUS        (1)                     ///< Status payload present.
#define CAST_EVENTS_LOG           (2)                     ///< Log payload present.
#define CAST_EVENTS_ENDED         (4)                     ///< Terminal reason present.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief One acquired collection of event categories in caller-owned storage.
 */
typedef struct CastEvents_Snapshot_s
{
    CastStatus status;                            ///< Valid only when present includes STATUS.
    char_t     log[CAST_EVENTS_TEXT_CAPACITY];    ///< Terminated latest log when LOG is present.
    char_t     reason[CAST_EVENTS_TEXT_CAPACITY]; ///< Terminated end reason when ENDED is present.
    uint32_t   present;                           ///< Delivered category mask.
} CastEvents_Snapshot_t;

/*************************************************************************************************/
/**
 * \brief Fixed status snapshots and their ownership metadata.
 */
typedef struct CastEvents_Status_s
{
    CastStatus   values[CAST_EVENTS_BUFFER_COUNT]; ///< Slot payloads, one per role.
    OS_Mailbox_t channel;                          ///< Slot ownership.
} CastEvents_Status_t;

/*************************************************************************************************/
/**
 * \brief Fixed log snapshots and their ownership metadata.
 */
typedef struct CastEvents_Text_s
{
    char_t       values[CAST_EVENTS_BUFFER_COUNT][CAST_EVENTS_TEXT_CAPACITY]; ///< Terminated slot payloads.
    OS_Mailbox_t channel;                                                     ///< Slot ownership.
} CastEvents_Text_t;

/*************************************************************************************************/
/**
 * \brief Fixed mailbox for one session producer and its GUI consumer.
 *
 * \par Details:
 *  Initialize before publication; Begin requires both roles to be quiescent.
 *  The session's callback thread is the sole producer and the GUI timer is the
 *  sole consumer. Each channel protects a stable reader slot without a lock.
 *  Generation and ended belong to the producer. Terminal reason is immutable
 *  after release publication until the next quiescent Begin. Never copy a live mailbox.
 */
typedef struct CastEvents_s
{
    CastEvents_Status_t status;                            ///< Coalescing status channel.
    CastEvents_Text_t   log;                               ///< Coalescing log channel.
    char_t              reason[CAST_EVENTS_TEXT_CAPACITY]; ///< Single immutable terminal payload.
    OS_Atomic32_t       terminal;                          ///< Empty, pending or consumed terminal state.
    uint32_t            generation;                        ///< Producer generation established by Begin.
    bool_t              ended;                             ///< Producer-only rejection latch.
} CastEvents_t;

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
 * \brief Constructs fixed event storage before producer or consumer access.
 *
 * \par Details:
 *  Payload storage belongs to the caller for the mailbox lifetime. The object must
 *  not move, be copied or be reinitialized while either role can access it.
 *
 * \par Requirements:
 *  - 001 Missing storage shall assert as programmer misuse.
 *  - 002 The initial generation shall be zero with no pending events or terminal latch.
 *  - 003 No allocation, OS resource creation, blocking or I/O shall occur.
 *
 * \param[out] events   Non-NULL exclusively owned fresh storage
 *
 * \retval     None No value is returned.
 */
void CastEvents_Initialize(CastEvents_t* events);

/*************************************************************************************************/
/**
 * \brief Starts a quiescent event generation with no pending deliveries.
 *
 * \par Details:
 *  The owner must stop the previous producer and exclude Take before reuse. A different
 *  generation rejects obsolete publications, but does not make concurrent reuse safe.
 *
 * \par Requirements:
 *  - 001 Missing storage shall assert.
 *  - 002 All prior pending payloads and the ended latch shall be discarded without clearing their bytes.
 *  - 003 Fixed slot ownership shall be reestablished; no resources shall be created or released.
 *
 * \param[in,out] events       Non-NULL quiescent caller-owned storage
 * \param[in]     generation   Opaque caller generation identifier
 *
 * \retval        None No value is returned.
 */
void CastEvents_Begin(CastEvents_t* events, uint32_t generation);

/*************************************************************************************************/
/**
 * \brief Publishes one event from the generation's single producer.
 *
 * \par Details:
 *  Unread status/log publications coalesce to the latest value in each category.
 *  Ended is retained separately until consumed once; after it, further events fail.
 *  Status and text inputs are borrowed only during the call and must be disjoint from
 *  mailbox storage. No second producer may publish concurrently.
 *
 * \par Requirements:
 *  - 001 Missing mailbox storage shall assert.
 *  - 002 A stale generation, unsupported event, missing status payload or ended generation shall fail unchanged.
 *  - 003 Publication shall copy at most one fixed payload and perform one atomic exchange or store.
 *  - 004 The producer shall neither wait for nor overwrite the consumer-owned snapshot.
 *  - 005 An ended event shall release-publish its reason only after prior status/log publications.
 *
 * \param[in,out] events       Non-NULL initialized producer-owned mailbox side
 * \param[in]     generation   Generation associated with this producer
 * \param[in]     event        Requested event kind
 * \param[in]     status       Required for status; ignored for other events
 * \param[in]     text         Optional terminated/bounded log or reason text
 *
 * \retval        TRUE    Event was accepted, possibly superseding an unread status/log.
 * \retval        FALSE   Event was rejected; all pending publications are unchanged.
 */
bool_t CastEvents_Publish(CastEvents_t*     events,
                          uint32_t          generation,
                          CastEvent         event,
                          const CastStatus* status,
                          const char_t*     text);

/*************************************************************************************************/
/**
 * \brief Consumes the latest pending categories without locks or retries.
 *
 * \par Details:
 *  One consumer owns every Take call. On success only present payload members and the
 *  present mask are changed. Concurrent status/log updates may appear on this or the
 *  next call. An observed terminal publication includes the final pending categories.
 *
 * \par Requirements:
 *  - 001 Missing mailbox or output pointers shall assert; output shall be disjoint from mailbox storage.
 *  - 002 No pending publication shall leave the complete output unchanged.
 *  - 003 Each consumed category shall become nonpending until the next producer publication.
 *  - 004 Work shall remain bounded by two channel exchanges, terminal load/store and fixed payload copies.
 *  - 005 No allocation, OS resource creation, waiting or I/O shall occur.
 *
 * \param[in,out] events     Non-NULL initialized consumer-owned mailbox side
 * \param[out]    snapshot   Non-NULL disjoint caller-owned output
 *
 * \retval        TRUE    At least one category was delivered.
 * \retval        FALSE   No publication was observed; output is unchanged.
 */
bool_t CastEvents_Take(CastEvents_t* events, CastEvents_Snapshot_t* snapshot);

#endif /* CAST_EVENTS_H */
