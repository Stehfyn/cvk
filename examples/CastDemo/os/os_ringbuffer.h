#ifndef OS_RINGBUFFER_H
#define OS_RINGBUFFER_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Caller-owned storage and contracts follow C:/dev/xml/include/utl/utl_xml.h.
 * This module defines circular storage mechanics; callers own synchronization.
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

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Caller-owned circular byte storage with explicitly serialized ownership.
 *
 * \par Details:
 *  Initialize before use; do not modify members directly or copy a live object.
 *  Capacity is positive, first is below capacity, and byteCount never exceeds it.
 *  This model has no internal synchronization and is not a concurrent SPSC queue.
 *  Every call and access through a borrowed span requires the same exclusive owner.
 *  It owns no payload resources. Cancellation requires the owner to stop users before
 *  Reset; no allocation or release occurs during construction, queries or mutations.
 */
typedef struct OS_RingBuffer_s
{
    byte_t*  bytes;     ///< Borrowed caller-owned storage, disjoint from this object.
    uint32_t capacity;  ///< Positive storage extent in bytes.
    uint32_t first;     ///< Physical offset of the oldest committed byte.
    uint32_t byteCount; ///< Committed readable bytes, including a possible wrapped part.
} OS_RingBuffer_t;

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
 * \brief Binds an empty ring to caller-owned byte storage.
 *
 * \par Details:
 *  Construct at owner initialization. Storage and ring must be disjoint and remain
 *  alive until all users have stopped. Stored bytes are not cleared.
 *
 * \par Requirements:
 *  - 001 Missing pointers and zero capacity shall assert as programmer misuse.
 *  - 002 The initialized ring shall contain zero committed bytes and use all capacity bytes.
 *  - 003 No resource creation, allocation, CRT, waits or I/O shall occur.
 *
 * \param[out]    ring       Non-NULL state, exclusively accessed
 * \param[in,out] bytes      Non-NULL caller-owned writable storage
 * \param[in]     capacity   Readable/writable storage extent in bytes, at least one
 *
 * \retval        None
 */
void OS_RingBuffer_Initialize(OS_RingBuffer_t* ring, byte_t* bytes, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Queries the number of committed readable bytes.
 *
 * \par Requirements:
 *  - 001 The initialized ring shall be required; a missing pointer shall assert.
 *  - 002 The query shall not mutate storage or perform I/O.
 *
 * \param[in] ring   Initialized exclusively accessed state
 *
 * \retval    0..capacity   Committed byte count.
 */
uint32_t OS_RingBuffer_GetByteCount(const OS_RingBuffer_t* ring);

/*************************************************************************************************/
/**
 * \brief Queries the number of uncommitted writable bytes.
 *
 * \par Requirements:
 *  - 001 The initialized ring shall be required; a missing pointer shall assert.
 *  - 002 Every storage byte shall be usable; no sentinel slot shall be reserved.
 *
 * \param[in] ring   Initialized exclusively accessed state
 *
 * \retval    0..capacity   Available byte count.
 */
uint32_t OS_RingBuffer_GetFreeByteCount(const OS_RingBuffer_t* ring);

/*************************************************************************************************/
/**
 * \brief Queries a contiguous part of the committed byte sequence.
 *
 * \par Details:
 *  Offset is relative to the oldest readable byte. Query again at offset plus the
 *  returned length to obtain a wrapped remainder. Neither query consumes storage.
 *
 * \par Requirements:
 *  - 001 Required pointers and an offset beyond the committed count shall assert.
 *  - 002 Returned storage shall remain borrowed until the next mutation or reset.
 *  - 003 The caller shall serialize queries, borrowed-span access and mutations together.
 *  - 004 An offset equal to the committed count shall publish zero and return NULL.
 *
 * \param[out] byteCount   Non-NULL contiguous length, disjoint from ring and storage
 * \param[in]  ring        Initialized exclusively accessed state
 * \param[in]  offset      Byte offset from the oldest readable byte
 *
 * \retval     non-NULL   Readable contiguous bytes.
 * \retval     NULL       No bytes remain at offset.
 */
const byte_t* OS_RingBuffer_GetReadableSpan(uint32_t* byteCount, const OS_RingBuffer_t* ring, uint32_t offset);

/*************************************************************************************************/
/**
 * \brief Queries a contiguous part of the available byte sequence.
 *
 * \par Details:
 *  Offset is relative to the next append position. Obtaining a span does not reserve
 *  or publish bytes. The caller writes a prefix, then commits it with Produce.
 *
 * \par Requirements:
 *  - 001 Required pointers and an offset beyond the free count shall assert.
 *  - 002 Borrowed writable bytes shall not overlap committed readable bytes.
 *  - 003 Queries and writes through borrowed spans shall remain exclusively owned until Produce.
 *  - 004 An offset equal to the free count shall publish zero and return NULL.
 *
 * \param[out]    byteCount   Non-NULL contiguous length, disjoint from ring and storage
 * \param[in,out] ring        Initialized exclusively accessed state
 * \param[in]     offset      Byte offset from the next append position
 *
 * \retval        non-NULL   Writable contiguous bytes.
 * \retval        NULL       No bytes remain at offset.
 */
byte_t* OS_RingBuffer_GetWritableSpan(uint32_t* byteCount, OS_RingBuffer_t* ring, uint32_t offset);

/*************************************************************************************************/
/**
 * \brief Discards all committed bytes while retaining storage.
 *
 * \par Details:
 *  Only the owner may reset after excluding all users and borrowed spans. Repeated
 *  reset leaves the same empty state. Payload destruction, if required, is the caller's job.
 *
 * \par Requirements:
 *  - 001 A missing state pointer shall assert.
 *  - 002 Storage ownership and capacity shall not change.
 *  - 003 Reset shall take constant work and shall not clear the backing bytes.
 *
 * \param[in,out] ring   Non-NULL initialized state, exclusively accessed
 *
 * \retval        None
 */
void OS_RingBuffer_Reset(OS_RingBuffer_t* ring);

/*************************************************************************************************/
/**
 * \brief Commits an initialized prefix of the writable byte sequence.
 *
 * \par Details:
 *  Produce is not an atomic publication operation. The caller must exclude access
 *  until the committed bytes and state are both visible through caller synchronization.
 *
 * \par Requirements:
 *  - 001 Missing state and committing beyond free capacity shall assert.
 *  - 002 The caller shall have initialized every byte in the committed prefix.
 *  - 003 A zero count shall leave state unchanged; work shall be constant.
 *
 * \param[in,out] ring        Non-NULL initialized exclusively accessed state
 * \param[in]     byteCount   Initialized prefix length, no greater than FreeByteCount
 *
 * \retval        None
 */
void OS_RingBuffer_Produce(OS_RingBuffer_t* ring, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Releases a prefix of the committed readable sequence.
 *
 * \par Details:
 *  All borrowed spans into consumed bytes expire. A zero count changes nothing.
 *  Consuming the final byte retains the next physical position for subsequent writes.
 *
 * \par Requirements:
 *  - 001 Missing state and consuming beyond the committed count shall assert.
 *  - 002 The operation shall not copy, clear or destroy payload bytes.
 *  - 003 Position arithmetic shall be bounded even at the largest capacity.
 *
 * \param[in,out] ring        Non-NULL initialized exclusively accessed state
 * \param[in]     byteCount   Prefix length, no greater than ByteCount
 *
 * \retval        None
 */
void OS_RingBuffer_Consume(OS_RingBuffer_t* ring, uint32_t byteCount);

#endif /* OS_RINGBUFFER_H */
