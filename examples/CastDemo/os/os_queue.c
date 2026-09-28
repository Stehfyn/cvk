/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Caller-owned fixed storage follows C:/dev/xml/include/utl/utl_xml.h.
 * Circular storage is provided by os_ringbuffer; synchronization belongs to callers.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_compiler.h"
#include "os/os_queue.h"
#include "os/os_assert.h"
#include "crt/crt_memory.h"

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
 * \brief Derives an occupied slot's physical storage.
 *
 * \par Requirements:
 *  - 001 The initialized queue and occupied logical index shall be required.
 *  - 002 Entry boundaries shall remain contiguous because capacity and first are entry multiples.
 *
 * \param[in] queue   Initialized exclusively accessed queue
 * \param[in] index   Logical occupied index, oldest first
 *
 * \retval    non-NULL   Exactly entryByteCount accessible bytes.
 */
static FORCE_INLINE byte_t* GetSlot(const OS_Queue_t* queue, uint32_t index);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Offset multiplication is bounded by the initialized capacity. Subtract the
 *  distance to the storage end before adding, avoiding unsigned wraparound.
 */
static FORCE_INLINE byte_t* GetSlot(const OS_Queue_t* queue, uint32_t index)
{
    uint32_t offset;
    uint32_t tail_distance;

    offset        = index * queue->entryByteCount;
    tail_distance = queue->ring.capacity - queue->ring.first;
    if (offset < tail_distance)
    {
        offset += queue->ring.first;
    }
    else
    {
        offset -= tail_distance;
    }

    return &queue->ring.bytes[offset];
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The constructor establishes integral entry boundaries in the byte ring. Every
 *  queue mutation preserves those boundaries; callers may not mutate ring members.
 */
void OS_Queue_Initialize(OS_Queue_t* queue, void* entries, uint32_t entryByteCount, uint32_t capacity)
{
    uint32_t byte_capacity;

    LOGASSERT(NULL != queue);
    LOGASSERT(NULL != entries);
    LOGASSERT(entryByteCount);
    LOGASSERT(capacity);
    LOGASSERT(capacity <= (UINT32_MAX / entryByteCount));

    byte_capacity         = capacity * entryByteCount;
    queue->entryByteCount = entryByteCount;
    OS_RingBuffer_Initialize(&queue->ring, entries, byte_capacity);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t OS_Queue_GetEntryCount(const OS_Queue_t* queue)
{
    LOGASSERT(NULL != queue);

    return OS_RingBuffer_GetByteCount(&queue->ring) / queue->entryByteCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t OS_Queue_GetCapacity(const OS_Queue_t* queue)
{
    LOGASSERT(NULL != queue);

    return queue->ring.capacity / queue->entryByteCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t OS_Queue_GetEntry(void* entry, const OS_Queue_t* queue, uint32_t index)
{
    uint32_t      count;
    bool_t        available;
    const byte_t* input;

    LOGASSERT(NULL != entry);
    LOGASSERT(NULL != queue);

    count     = OS_Queue_GetEntryCount(queue);
    available = (index < count);
    if (available)
    {
        input = GetSlot(queue, index);
        CRT_Memory_Copy(entry, input, queue->entryByteCount);
    }

    return available;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void OS_Queue_Reset(OS_Queue_t* queue)
{
    LOGASSERT(NULL != queue);

    OS_RingBuffer_Reset(&queue->ring);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t OS_Queue_Replace(OS_Queue_t* queue, uint32_t index, const void* entry)
{
    uint32_t count;
    bool_t   available;
    byte_t*  output;

    LOGASSERT(NULL != queue);
    LOGASSERT(NULL != entry);

    count     = OS_Queue_GetEntryCount(queue);
    available = (index < count);
    if (available)
    {
        output = GetSlot(queue, index);
        CRT_Memory_Copy(output, entry, queue->entryByteCount);
    }

    return available;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t OS_Queue_Read(void* entry, OS_Queue_t* queue)
{
    bool_t available;

    available = OS_Queue_GetEntry(entry, queue, 0);
    if (available)
    {
        OS_RingBuffer_Consume(&queue->ring, queue->entryByteCount);
    }

    return available;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every slot is contiguous because the ring extent and all position advances are
 *  multiples of the fixed entry width. Copy completes before the count is advanced.
 */
bool_t OS_Queue_Write(OS_Queue_t* queue, const void* entry)
{
    byte_t*  output;
    uint32_t span_count;
    bool_t   available;

    LOGASSERT(NULL != queue);
    LOGASSERT(NULL != entry);

    output    = OS_RingBuffer_GetWritableSpan(&span_count, &queue->ring, 0);
    available = (NULL != output);
    if (available)
    {
        LOGASSERT(queue->entryByteCount <= span_count);
        CRT_Memory_Copy(output, entry, queue->entryByteCount);
        OS_RingBuffer_Produce(&queue->ring, queue->entryByteCount);
    }

    return available;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Move predecessors backward through logical positions into the removed slot,
 *  then consume the duplicate front entry. Descending indices preserve unread inputs
 *  even when physical storage wraps.
 */
bool_t OS_Queue_Remove(OS_Queue_t* queue, uint32_t index)
{
    uint32_t      count;
    bool_t        available;
    uint32_t      remaining;
    byte_t*       output;
    const byte_t* input;

    LOGASSERT(NULL != queue);

    count     = OS_Queue_GetEntryCount(queue);
    available = (index < count);
    if (available)
    {
        for (remaining = index; remaining; --remaining)
        {
            output = GetSlot(queue, remaining);
            input  = GetSlot(queue, remaining - 1);
            CRT_Memory_Copy(output, input, queue->entryByteCount);
        }
        OS_RingBuffer_Consume(&queue->ring, queue->entryByteCount);
    }

    return available;
}
