#ifndef OS_QUEUE_H
#define OS_QUEUE_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Caller-owned fixed storage follows C:/dev/xml/include/utl/utl_xml.h.
 * Circular storage is provided by os_ringbuffer; synchronization belongs to callers.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_ringbuffer.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Caller-owned bounded FIFO of fixed-size object representations.
 *
 * \par Details:
 *  Initialize before use. Do not modify members or copy a live queue.
 *  All calls require exclusive ownership, including publication of input/output.
 *  This is an externally serialized queue, not an internally atomic SPSC/MPMC queue.
 *  No payload pointer is followed or released; value ownership belongs to the caller.
 *  Cancellation stops users before Reset. There are no waiters or wakeup resources.
 *  Entry count is at most capacity; ring positions/counts are entry-width multiples.
 */
typedef struct OS_Queue_s
{
    OS_RingBuffer_t ring;           ///< Bound byte storage; mutate only through OS_Queue functions.
    uint32_t        entryByteCount; ///< Positive representation width, fixed until reinitialization.
} OS_Queue_t;

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
 * \brief Binds an empty FIFO to fixed caller-owned entry storage.
 *
 * \par Details:
 *  Construct at owner initialization. Entries may be a typed array or byte storage;
 *  this module accesses only object-representation bytes and requires no added alignment.
 *
 * \par Requirements:
 *  - 001 Missing pointers, zero entry width and zero capacity shall assert.
 *  - 002 The capacity-times-width product exceeding UINT32_MAX shall assert before multiplication.
 *  - 003 Entries shall provide the required writable byte extent, disjoint from queue state.
 *  - 004 Initialization shall retain storage without clearing, allocating or creating resources.
 *
 * \param[out]    queue            Non-NULL state, exclusively accessed
 * \param[in,out] entries          Non-NULL caller-owned storage, alive until all users stop
 * \param[in]     entryByteCount   Positive fixed object-representation width
 * \param[in]     capacity         Positive number of entries
 *
 * \retval        None
 */
void OS_Queue_Initialize(OS_Queue_t* queue, void* entries, uint32_t entryByteCount, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Queries the number of occupied entries.
 *
 * \par Requirements:
 *  - 001 The initialized queue shall be required; a missing pointer shall assert.
 *  - 002 The query shall not inspect or modify payload storage.
 *
 * \param[in] queue   Non-NULL initialized exclusively accessed queue
 *
 * \retval    0..capacity   Occupied entry count.
 */
uint32_t OS_Queue_GetEntryCount(const OS_Queue_t* queue);

/*************************************************************************************************/
/**
 * \brief Queries the initialized entry capacity.
 *
 * \par Requirements:
 *  - 001 The initialized queue shall be required; a missing pointer shall assert.
 *  - 002 Every allocated entry slot shall be usable.
 *
 * \param[in] queue   Non-NULL initialized exclusively accessed queue
 *
 * \retval    1..UINT32_MAX   Entry capacity limited by the backing byte extent.
 */
uint32_t OS_Queue_GetCapacity(const OS_Queue_t* queue);

/*************************************************************************************************/
/**
 * \brief Copies one occupied entry without removing it.
 *
 * \par Requirements:
 *  - 001 Required pointers shall assert; index outside the occupied range shall fail unchanged.
 *  - 002 The output shall provide entryByteCount writable bytes, disjoint from queue and backing storage.
 *  - 003 Work shall be bounded by one entry width, independent of occupancy.
 *
 * \param[out] entry   Non-NULL disjoint output representation
 * \param[in]  queue   Non-NULL initialized exclusively accessed queue
 * \param[in]  index   Logical occupied index, oldest entry is zero
 *
 * \retval     TRUE    One entry was copied.
 * \retval     FALSE   Index was absent; output is unchanged.
 */
bool_t OS_Queue_GetEntry(void* entry, const OS_Queue_t* queue, uint32_t index);

/*************************************************************************************************/
/**
 * \brief Discards all entries while retaining the bound storage.
 *
 * \par Details:
 *  The owner must exclude every user before reset. The queue owns no payload resources;
 *  any required payload release must be performed by the caller before discarding entries.
 *
 * \par Requirements:
 *  - 001 The initialized queue shall be required; a missing pointer shall assert.
 *  - 002 Repeated reset shall produce the same empty state without clearing backing bytes.
 *  - 003 Reset shall take constant work without resource creation, waiting or I/O.
 *
 * \param[in,out] queue   Non-NULL initialized exclusively accessed queue
 *
 * \retval        None
 */
void OS_Queue_Reset(OS_Queue_t* queue);

/*************************************************************************************************/
/**
 * \brief Replaces one occupied entry without changing FIFO order.
 *
 * \par Requirements:
 *  - 001 Required pointers shall assert; index outside the occupied range shall fail unchanged.
 *  - 002 The input shall provide entryByteCount readable bytes, disjoint from queue and backing storage.
 *  - 003 Work shall be bounded by one entry width, independent of occupancy.
 *
 * \param[in,out] queue   Non-NULL initialized exclusively accessed queue
 * \param[in]     index   Logical occupied index, oldest entry is zero
 * \param[in]     entry   Non-NULL disjoint input representation
 *
 * \retval        TRUE    One occupied entry was replaced.
 * \retval        FALSE   Index was absent; queue is unchanged.
 */
bool_t OS_Queue_Replace(OS_Queue_t* queue, uint32_t index, const void* entry);

/*************************************************************************************************/
/**
 * \brief Copies and removes the oldest occupied entry.
 *
 * \par Requirements:
 *  - 001 Required pointers shall assert; an empty queue shall fail unchanged.
 *  - 002 The output shall provide entryByteCount writable bytes, disjoint from queue and backing storage.
 *  - 003 No remaining entry shall move; work shall be bounded by one entry width.
 *  - 004 Publication and removal shall complete under caller-provided exclusive access.
 *
 * \param[out]    entry   Non-NULL disjoint output representation
 * \param[in,out] queue   Non-NULL initialized exclusively accessed queue
 *
 * \retval        TRUE    The oldest entry was copied and removed.
 * \retval        FALSE   Queue was empty; output and queue are unchanged.
 */
bool_t OS_Queue_Read(void* entry, OS_Queue_t* queue);

/*************************************************************************************************/
/**
 * \brief Appends one entry when capacity is available.
 *
 * \par Requirements:
 *  - 001 Required pointers shall assert; a full queue shall fail unchanged.
 *  - 002 The input shall provide entryByteCount readable bytes, disjoint from queue and backing storage.
 *  - 003 No occupied entry shall move; work shall be bounded by one entry width.
 *  - 004 Only the completely copied entry shall be committed.
 *
 * \param[in,out] queue   Non-NULL initialized exclusively accessed queue
 * \param[in]     entry   Non-NULL disjoint input representation
 *
 * \retval        TRUE    One entry was appended.
 * \retval        FALSE   Queue was full; all queue state and bytes are unchanged.
 */
bool_t OS_Queue_Write(OS_Queue_t* queue, const void* entry);

/*************************************************************************************************/
/**
 * \brief Removes one occupied entry while preserving survivor order.
 *
 * \par Details:
 *  This policy-support operation may move earlier entries. Use Read for ordinary FIFO
 *  consumption. Payload resource destruction belongs to the caller.
 *
 * \par Requirements:
 *  - 001 The initialized queue shall be required; a missing pointer shall assert.
 *  - 002 An absent index shall fail without modifying storage or state.
 *  - 003 At most index entries shall move; removing index zero shall move no entries.
 *  - 004 The operation shall allocate nothing and shall perform no waits or I/O.
 *
 * \param[in,out] queue   Non-NULL initialized exclusively accessed queue
 * \param[in]     index   Logical occupied index, oldest entry is zero
 *
 * \retval        TRUE    The entry was removed.
 * \retval        FALSE   Index was absent; queue is unchanged.
 */
bool_t OS_Queue_Remove(OS_Queue_t* queue, uint32_t index);

#endif /* OS_QUEUE_H */
