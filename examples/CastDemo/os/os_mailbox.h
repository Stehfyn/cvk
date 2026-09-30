#ifndef OS_MAILBOX_H
#define OS_MAILBOX_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Caller-owned typed storage follows C:/dev/xml/include/utl/utl_xml.h.
 * Three disjoint slot roles transfer through the acquire/release OS atomic backend.
 * This is a latest-value channel, not a FIFO or a multi-producer queue.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_atomic.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define OS_MAILBOX_SLOT_COUNT (3) ///< Reader, exchange and writer each own one slot.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Fixed ownership metadata for a single producer and a single consumer.
 *
 * \par Details:
 *  The caller owns three disjoint, equally typed payload slots for the complete
 *  channel lifetime. Only the producer accesses writer; only the consumer accesses
 *  reader and readable. The middle atomic transfers ownership, including payload
 *  memory visibility. Do not copy, move or reset metadata or payload while either
 *  participant can access them. Participant handoff requires external quiescence.
 *  Each producer publication replaces any unread middle value. A consumer borrow
 *  remains stable until that consumer next successfully takes a publication.
 */
typedef struct OS_Mailbox_s
{
    OS_Atomic32_t middle;   ///< Private exchange slot with pending flag.
    uint32_t      writer;   ///< Private producer-owned slot index.
    uint32_t      reader;   ///< Private consumer-owned slot index.
    bool_t        readable; ///< Consumer has acquired at least one publication.
} OS_Mailbox_t;

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
 * \brief Establishes disjoint empty slot ownership before concurrent use.
 *
 * \par Details:
 *  One writer, one exchange slot and one empty reader are established.
 *
 * \par Requirements:
 *  - 001 Required, naturally aligned, exclusively owned metadata shall assert when absent.
 *  - 002 All previous participants shall be quiescent before reuse.
 *  - 003 No payload is readable until the first successful TryTake.
 *  - 004 Payload storage shall remain caller-owned; no allocation, OS resources or CRT calls shall occur.
 *
 * \param[out] mailbox   Required quiescent metadata with three retained payload slots
 *
 * \retval     None No value is returned.
 */
void OS_Mailbox_Initialize(OS_Mailbox_t* mailbox);

/*************************************************************************************************/
/**
 * \brief Gets the producer's exclusively writable payload index.
 *
 * \par Requirements:
 *  - 001 Only the initialized channel's single producer shall call this query.
 *  - 002 The returned slot shall be writable until the producer calls Publish.
 *  - 003 Every field consumed by the reader shall be complete before publication.
 *
 * \param[in] mailbox   Required initialized producer-side metadata
 *
 * \retval    0..2   Index within the caller's three payload slots.
 */
uint32_t OS_Mailbox_GetWriteIndex(const OS_Mailbox_t* mailbox);

/*************************************************************************************************/
/**
 * \brief Gets the consumer's stable acquired payload index.
 *
 * \par Requirements:
 *  - 001 Only the initialized channel's single consumer shall call this query.
 *  - 002 No prior successful TryTake shall assert as programmer misuse.
 *  - 003 The borrow shall remain stable through failed TryTake calls and producer publications.
 *  - 004 The next successful TryTake or quiescent reinitialization shall end the borrow.
 *
 * \param[in] mailbox   Required initialized consumer-side metadata
 *
 * \retval    0..2   Index within the caller's three payload slots.
 */
uint32_t OS_Mailbox_GetReadIndex(const OS_Mailbox_t* mailbox);

/*************************************************************************************************/
/**
 * \brief Publishes the producer's completely written payload without waiting.
 *
 * \par Details:
 *  Publication is complete; the producer owns its next slot.
 *
 * \par Requirements:
 *  - 001 The sole producer shall have finished all writes to its current slot.
 *  - 002 Exactly one atomic exchange shall transfer that slot and acquire the previous middle slot.
 *  - 003 An unread middle value may be replaced; the consumer-owned value shall never be overwritten.
 *  - 004 The producer shall use GetWriteIndex again before starting another payload.
 *  - 005 No allocation, wait, retry, native resource operation or payload copy shall occur.
 *
 * \param[in,out] mailbox   Required initialized producer-side metadata
 *
 * \retval        None No value is returned.
 */
void OS_Mailbox_Publish(OS_Mailbox_t* mailbox);

/*************************************************************************************************/
/**
 * \brief Acquires a pending complete payload in one bounded attempt.
 *
 * \par Requirements:
 *  - 001 Only the initialized channel's single consumer shall call this operation.
 *  - 002 An empty observation shall preserve reader ownership and all metadata.
 *  - 003 A pending observation shall exchange once; a newer racing publication may be acquired instead.
 *  - 004 All payload writes preceding the acquired publication shall be visible to the consumer.
 *  - 005 No allocation, wait, retry, native resource operation or payload copy shall occur.
 *
 * \param[in,out] mailbox   Required initialized consumer-side metadata
 *
 * \retval        TRUE    A complete payload is now available through GetReadIndex.
 * \retval        FALSE   No pending publication was observed; any prior borrow remains valid.
 */
bool_t OS_Mailbox_TryTake(OS_Mailbox_t* mailbox);

#endif /* OS_MAILBOX_H */
