/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Caller-owned storage and contracts follow C:/dev/xml/include/utl/utl_xml.h.
 * This module defines circular storage mechanics; callers own synchronization.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_ringbuffer.h"
#include "os/os_assert.h"

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

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void OS_RingBuffer_Initialize(OS_RingBuffer_t* ring, byte_t* bytes, uint32_t capacity)
{
    LOGASSERT(NULL != ring);
    LOGASSERT(NULL != bytes);
    LOGASSERT(capacity);

    ring->bytes     = bytes;
    ring->capacity  = capacity;
    ring->first     = 0;
    ring->byteCount = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t OS_RingBuffer_GetByteCount(const OS_RingBuffer_t* ring)
{
    LOGASSERT(NULL != ring);

    return ring->byteCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t OS_RingBuffer_GetFreeByteCount(const OS_RingBuffer_t* ring)
{
    LOGASSERT(NULL != ring);

    return ring->capacity - ring->byteCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Subtracting the distance to the allocation end avoids overflow when capacity is
 *  near UINT32_MAX. No byte outside the committed sequence is exposed.
 */
const byte_t* OS_RingBuffer_GetReadableSpan(uint32_t* byteCount, const OS_RingBuffer_t* ring, uint32_t offset)
{
    uint32_t      available;
    const byte_t* bytes;
    uint32_t      tail_distance;
    uint32_t      first;
    uint32_t      contiguous;

    LOGASSERT(NULL != byteCount);
    LOGASSERT(NULL != ring);
    LOGASSERT(offset <= ring->byteCount);

    available = ring->byteCount - offset;
    bytes     = NULL;
    if (available)
    {
        tail_distance = ring->capacity - ring->first;
        if (offset < tail_distance)
        {
            first = ring->first + offset;
        }
        else
        {
            first = offset - tail_distance;
        }
        contiguous = ring->capacity - first;
        if (available < contiguous)
        {
            contiguous = available;
        }
        (*byteCount) = contiguous;
        bytes        = &ring->bytes[first];
    }
    else
    {
        (*byteCount) = 0;
    }

    return bytes;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The combined committed count and offset cannot exceed capacity. Address
 *  arithmetic uses a distance subtraction before adding to the first position.
 */
byte_t* OS_RingBuffer_GetWritableSpan(uint32_t* byteCount, OS_RingBuffer_t* ring, uint32_t offset)
{
    uint32_t free_count;
    uint32_t available;
    byte_t*  bytes;
    uint32_t displacement;
    uint32_t tail_distance;
    uint32_t first;
    uint32_t contiguous;

    LOGASSERT(NULL != byteCount);
    LOGASSERT(NULL != ring);

    free_count = ring->capacity - ring->byteCount;
    LOGASSERT(offset <= free_count);

    available = free_count - offset;
    bytes     = NULL;
    if (available)
    {
        displacement  = ring->byteCount + offset;
        tail_distance = ring->capacity - ring->first;
        if (displacement < tail_distance)
        {
            first = ring->first + displacement;
        }
        else
        {
            first = displacement - tail_distance;
        }
        contiguous = ring->capacity - first;
        if (available < contiguous)
        {
            contiguous = available;
        }
        (*byteCount) = contiguous;
        bytes        = &ring->bytes[first];
    }
    else
    {
        (*byteCount) = 0;
    }

    return bytes;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void OS_RingBuffer_Reset(OS_RingBuffer_t* ring)
{
    LOGASSERT(NULL != ring);

    ring->first     = 0;
    ring->byteCount = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void OS_RingBuffer_Produce(OS_RingBuffer_t* ring, uint32_t byteCount)
{
    LOGASSERT(NULL != ring);
    LOGASSERT(byteCount <= (ring->capacity - ring->byteCount));

    ring->byteCount += byteCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Compute a single wrap using the distance to the storage end. The consumed
 *  count is bounded by capacity, so neither addition overflow nor division is needed.
 */
void OS_RingBuffer_Consume(OS_RingBuffer_t* ring, uint32_t byteCount)
{
    uint32_t tail_distance;

    LOGASSERT(NULL != ring);
    LOGASSERT(byteCount <= ring->byteCount);

    tail_distance = ring->capacity - ring->first;
    if (byteCount < tail_distance)
    {
        ring->first += byteCount;
    }
    else
    {
        ring->first = byteCount - tail_distance;
    }
    ring->byteCount -= byteCount;
}
