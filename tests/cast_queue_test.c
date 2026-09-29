/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Independent bounded-storage and Cast command-policy checks. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_queue.h"
#include "cast_session.h"
#include "os/os_ringbuffer.h"
#include "os/os_types.h"
#include <stdint.h>
#include "os/os_assert.h"
#include "os/os_process.h"
#include "cast_commands.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_RING_CAPACITY   (31)
#define TEST_QUEUE_CAPACITY  (17)
#define TEST_ENTRY_BYTES     (7)
#define TEST_QUEUE_BYTES     (TEST_QUEUE_CAPACITY * TEST_ENTRY_BYTES)
#define TEST_QUEUE_STEPS     (4096)
#define TEST_COMMAND_CYCLES  (257)
#define TEST_SENTINEL        (0xDA)
#define TEST_RANDOM_SEED     (0x16E2B541)
#define TEST_OPERATION_COUNT (6)
#define TEST_WRITE           (0)
#define TEST_READ            (1)
#define TEST_PEEK            (2)
#define TEST_REPLACE         (3)
#define TEST_REMOVE          (4)

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
 * \brief Checks every small ring layout and every span offset.
 *
 * \par Requirements:
 *  - 001 Every failed expectation shall terminate through the production assertion.
 *
 * \retval None
 */
static void CheckRings(void);

/*************************************************************************************************/
/**
 * \brief Checks fixed-width FIFO operations against an independent linear model.
 *
 * \par Requirements:
 *  - 001 Every failed expectation shall terminate through the production assertion.
 *
 * \retval None
 */
static void CheckQueues(void);

/*************************************************************************************************/
/**
 * \brief Checks gesture priority and FIFO reuse through repeated physical wraps.
 *
 * \par Requirements:
 *  - 001 Every failed expectation shall terminate through the production assertion.
 *
 * \retval None
 */
static void CheckCommandPolicies(void);

/*************************************************************************************************/
/**
 * \brief Checks every removal position in full and wrapped queues.
 *
 * \par Requirements:
 *  - 001 Every failure shall terminate through the production assertion.
 *
 * \retval None
 */
static void CheckSaturation(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  All first positions, occupancies and offsets are checked against independent
 *  linear byte positions. Guard bytes detect writes outside the supplied storage.
 */
static void CheckRings(void)
{
    uint32_t        capacity;
    uint32_t        first;
    uint32_t        occupied;
    uint32_t        index;
    byte_t          storage[TEST_RING_CAPACITY + 2];
    OS_RingBuffer_t ring;
    uint32_t        offset;
    const byte_t*   readable;
    uint32_t        count;
    uint32_t        expected_count;
    uint32_t        physical;
    uint32_t        free_count;
    byte_t*         writable;

    for (capacity = 1; capacity <= TEST_RING_CAPACITY; ++capacity)
    {
        for (first = 0; first < capacity; ++first)
        {
            for (occupied = 0; occupied <= capacity; ++occupied)
            {
                for (index = 0; index < sizeof(storage); ++index)
                {
                    storage[index] = (byte_t)index;
                }
                OS_RingBuffer_Initialize(&ring, &storage[1], capacity);
                OS_RingBuffer_Produce(&ring, first);
                OS_RingBuffer_Consume(&ring, first);
                OS_RingBuffer_Produce(&ring, occupied);
                LOGASSERT(occupied == OS_RingBuffer_GetByteCount(&ring));
                LOGASSERT((capacity - occupied) == OS_RingBuffer_GetFreeByteCount(&ring));
                for (offset = 0; offset <= occupied; ++offset)
                {
                    readable = OS_RingBuffer_GetReadableSpan(&count, &ring, offset);
                    if (offset == occupied)
                    {
                        LOGASSERT(NULL == readable);
                        LOGASSERT(0 == count);
                    }
                    else
                    {
                        expected_count = 0;
                        for (index = offset; index < occupied; ++index)
                        {
                            physical = (first + index) % capacity;
                            if (expected_count && (0 == physical))
                            {
                                break;
                            }
                            LOGASSERT(storage[physical + 1] == readable[expected_count]);
                            ++expected_count;
                        }
                        LOGASSERT(expected_count == count);
                    }
                }
                free_count = capacity - occupied;
                for (offset = 0; offset <= free_count; ++offset)
                {
                    writable = OS_RingBuffer_GetWritableSpan(&count, &ring, offset);
                    if (offset == free_count)
                    {
                        LOGASSERT(NULL == writable);
                        LOGASSERT(0 == count);
                    }
                    else
                    {
                        physical       = (first + occupied + offset) % capacity;
                        expected_count = capacity - physical;
                        if ((free_count - offset) < expected_count)
                        {
                            expected_count = free_count - offset;
                        }
                        LOGASSERT(&storage[physical + 1] == writable);
                        LOGASSERT(expected_count == count);
                    }
                }
                OS_RingBuffer_Produce(&ring, free_count);
                LOGASSERT(capacity == OS_RingBuffer_GetByteCount(&ring));
                OS_RingBuffer_Consume(&ring, capacity);
                LOGASSERT(0 == OS_RingBuffer_GetByteCount(&ring));
                LOGASSERT(first == ring.first);
                OS_RingBuffer_Produce(&ring, 0);
                OS_RingBuffer_Consume(&ring, 0);
                LOGASSERT(first == ring.first);
                OS_RingBuffer_Reset(&ring);
                OS_RingBuffer_Reset(&ring);
                LOGASSERT(0 == ring.first);
                LOGASSERT(0 == OS_RingBuffer_GetByteCount(&ring));
                LOGASSERT(capacity == OS_RingBuffer_GetFreeByteCount(&ring));
                for (index = 0; index < sizeof(storage); ++index)
                {
                    LOGASSERT((byte_t)index == storage[index]);
                }
            }
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Capacities include one and non-powers of two; widths include odd object sizes.
 *  Each deterministic operation is checked immediately, including output/state
 *  preservation on full, empty and out-of-range failures.
 */
static void CheckQueues(void)
{
    uint32_t   capacity;
    uint32_t   width;
    uint32_t   index;
    byte_t     storage[TEST_QUEUE_BYTES + 2];
    OS_Queue_t queue;
    uint32_t   count;
    uint32_t   random;
    uint32_t   step;
    uint32_t   selected;
    uint32_t   operation;
    uint32_t   byte_index;
    byte_t     entry[TEST_ENTRY_BYTES];
    byte_t     output[TEST_ENTRY_BYTES];
    byte_t     previous[TEST_QUEUE_BYTES + 2];
    bool_t     accepted;
    bool_t     expected_result;
    byte_t     expected[TEST_QUEUE_CAPACITY][TEST_ENTRY_BYTES];

    for (capacity = 1; capacity <= TEST_QUEUE_CAPACITY; ++capacity)
    {
        for (width = 1; width <= TEST_ENTRY_BYTES; ++width)
        {
            for (index = 0; index < sizeof(storage); ++index)
            {
                storage[index] = (byte_t)(TEST_SENTINEL ^ index);
            }
            OS_Queue_Initialize(&queue, &storage[1], width, capacity);
            count  = 0;
            random = TEST_RANDOM_SEED;
            LOGASSERT(capacity == OS_Queue_GetCapacity(&queue));
            for (step = 0; step < TEST_QUEUE_STEPS; ++step)
            {
                random ^= random << 13;
                random ^= random >> 17;
                random ^= random << 5;
                selected  = (random >> 8) % (capacity + 2);
                operation = random % TEST_OPERATION_COUNT;
                for (byte_index = 0; byte_index < TEST_ENTRY_BYTES; ++byte_index)
                {
                    entry[byte_index]  = (byte_t)((step + byte_index) & UINT8_MAX);
                    output[byte_index] = TEST_SENTINEL;
                }
                for (index = 0; index < sizeof(storage); ++index)
                {
                    previous[index] = storage[index];
                }
                switch (operation)
                {
                case TEST_WRITE:
                    accepted        = OS_Queue_Write(&queue, entry);
                    expected_result = (count < capacity);
                    LOGASSERT(accepted == expected_result);
                    if (expected_result)
                    {
                        for (byte_index = 0; byte_index < width; ++byte_index)
                        {
                            expected[count][byte_index] = entry[byte_index];
                        }
                        ++count;
                    }
                    break;
                case TEST_READ:
                    accepted        = OS_Queue_Read(output, &queue);
                    expected_result = !!count;
                    LOGASSERT(accepted == expected_result);
                    if (expected_result)
                    {
                        for (byte_index = 0; byte_index < width; ++byte_index)
                        {
                            LOGASSERT(expected[0][byte_index] == output[byte_index]);
                        }
                        --count;
                        for (index = 0; index < count; ++index)
                        {
                            for (byte_index = 0; byte_index < width; ++byte_index)
                            {
                                expected[index][byte_index] = expected[index + 1][byte_index];
                            }
                        }
                    }
                    break;
                case TEST_PEEK:
                    accepted        = OS_Queue_GetEntry(output, &queue, selected);
                    expected_result = (selected < count);
                    LOGASSERT(accepted == expected_result);
                    if (expected_result)
                    {
                        for (byte_index = 0; byte_index < width; ++byte_index)
                        {
                            LOGASSERT(expected[selected][byte_index] == output[byte_index]);
                        }
                    }
                    break;
                case TEST_REPLACE:
                    accepted        = OS_Queue_Replace(&queue, selected, entry);
                    expected_result = (selected < count);
                    LOGASSERT(accepted == expected_result);
                    if (expected_result)
                    {
                        for (byte_index = 0; byte_index < width; ++byte_index)
                        {
                            expected[selected][byte_index] = entry[byte_index];
                        }
                    }
                    break;
                case TEST_REMOVE:
                    accepted        = OS_Queue_Remove(&queue, selected);
                    expected_result = (selected < count);
                    LOGASSERT(accepted == expected_result);
                    if (expected_result)
                    {
                        --count;
                        for (index = selected; index < count; ++index)
                        {
                            for (byte_index = 0; byte_index < width; ++byte_index)
                            {
                                expected[index][byte_index] = expected[index + 1][byte_index];
                            }
                        }
                    }
                    break;
                default:
                    accepted = TRUE;
                    OS_Queue_Reset(&queue);
                    OS_Queue_Reset(&queue);
                    count = 0;
                    break;
                }
                if (!accepted)
                {
                    for (index = 0; index < sizeof(storage); ++index)
                    {
                        LOGASSERT(previous[index] == storage[index]);
                    }
                    for (byte_index = 0; byte_index < TEST_ENTRY_BYTES; ++byte_index)
                    {
                        LOGASSERT(TEST_SENTINEL == output[byte_index]);
                    }
                }
                for (byte_index = width; byte_index < TEST_ENTRY_BYTES; ++byte_index)
                {
                    LOGASSERT(TEST_SENTINEL == output[byte_index]);
                }
                LOGASSERT(count == OS_Queue_GetEntryCount(&queue));
                for (index = 0; index < count; ++index)
                {
                    accepted = OS_Queue_GetEntry(output, &queue, index);
                    LOGASSERT(accepted);
                    for (byte_index = 0; byte_index < width; ++byte_index)
                    {
                        LOGASSERT(expected[index][byte_index] == output[byte_index]);
                    }
                }
                LOGASSERT(TEST_SENTINEL == storage[0]);
                for (index = (capacity * width) + 1; index < sizeof(storage); ++index)
                {
                    LOGASSERT((byte_t)(TEST_SENTINEL ^ index) == storage[index]);
                }
            }
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Independent expected sequences cover final-seek boundaries, saturated scrub
 *  rejection, oldest-position eviction, stop latching and invalid position rejection.
 */
static void CheckCommandPolicies(void)
{
    CastCommands_t      commands;
    uint32_t            cycle;
    uint32_t            index;
    bool_t              accepted;
    CastQueuedCommand_t command;

    CastCommands_Reset(&commands);
    for (cycle = 0; cycle < TEST_COMMAND_CYCLES; ++cycle)
    {
        for (index = 0; index < CAST_COMMAND_CAPACITY; ++index)
        {
            accepted = CastCommands_Push(&commands, CAST_COMMAND_PLAY, (float64_t)index);
            LOGASSERT(accepted);
        }
        accepted = CastCommands_Push(&commands, CAST_COMMAND_SCRUB, 100.0);
        LOGASSERT(!accepted);
        accepted = CastCommands_Push(&commands, CAST_COMMAND_SEEK, 101.0);
        LOGASSERT(accepted);
        for (index = 1; index < CAST_COMMAND_CAPACITY; ++index)
        {
            accepted = CastCommands_Take(&commands, &command);
            LOGASSERT(accepted);
            LOGASSERT(CAST_COMMAND_PLAY == command.command);
            LOGASSERT((float64_t)index == command.seconds);
        }
        accepted = CastCommands_Take(&commands, &command);
        LOGASSERT(accepted);
        LOGASSERT(CAST_COMMAND_SEEK == command.command);
        LOGASSERT(101.0 == command.seconds);

        accepted = CastCommands_Push(&commands, CAST_COMMAND_SEEK, 10.0);
        LOGASSERT(accepted);
        accepted = CastCommands_Push(&commands, CAST_COMMAND_SCRUB, 20.0);
        LOGASSERT(accepted);
        accepted = CastCommands_Push(&commands, CAST_COMMAND_SCRUB, 21.0);
        LOGASSERT(accepted);
        accepted = CastCommands_Take(&commands, &command);
        LOGASSERT(accepted);
        LOGASSERT(CAST_COMMAND_SEEK == command.command);
        LOGASSERT(10.0 == command.seconds);
        accepted = CastCommands_Take(&commands, &command);
        LOGASSERT(accepted);
        LOGASSERT(CAST_COMMAND_SCRUB == command.command);
        LOGASSERT(21.0 == command.seconds);

        accepted = CastCommands_Push(&commands, CAST_COMMAND_TOGGLE, 0.0);
        LOGASSERT(accepted);
        accepted = CastCommands_Push(&commands, CAST_COMMAND_SEEK, 1.0);
        LOGASSERT(accepted);
        for (index = 2; index < CAST_COMMAND_CAPACITY; ++index)
        {
            accepted = CastCommands_Push(&commands, CAST_COMMAND_PAUSE, (float64_t)index);
            LOGASSERT(accepted);
        }
        accepted = CastCommands_Push(&commands, CAST_COMMAND_SCRUB, 200.0);
        LOGASSERT(accepted);
        accepted = CastCommands_Take(&commands, &command);
        LOGASSERT(accepted);
        LOGASSERT(CAST_COMMAND_TOGGLE == command.command);
        for (index = 2; index < CAST_COMMAND_CAPACITY; ++index)
        {
            accepted = CastCommands_Take(&commands, &command);
            LOGASSERT(accepted);
            LOGASSERT(CAST_COMMAND_PAUSE == command.command);
            LOGASSERT((float64_t)index == command.seconds);
        }
        accepted = CastCommands_Take(&commands, &command);
        LOGASSERT(accepted);
        LOGASSERT(CAST_COMMAND_SCRUB == command.command);
        LOGASSERT(200.0 == command.seconds);
        accepted = CastCommands_Push(&commands, CAST_COMMAND_SEEK, -1.0);
        LOGASSERT(!accepted);
        accepted = CastCommands_Push(&commands, CAST_COMMAND_STOP, 0.0);
        LOGASSERT(accepted);
        accepted = CastCommands_Push(&commands, CAST_COMMAND_STOP, 0.0);
        LOGASSERT(accepted);
        accepted = CastCommands_Push(&commands, CAST_COMMAND_PLAY, 0.0);
        LOGASSERT(!accepted);
        accepted = CastCommands_Take(&commands, &command);
        LOGASSERT(accepted);
        LOGASSERT(CAST_COMMAND_STOP == command.command);
        accepted = CastCommands_Take(&commands, &command);
        LOGASSERT(!accepted);
        LOGASSERT(CAST_COMMAND_STOP == command.command);
        LOGASSERT(0.0 == command.seconds);
        CastCommands_Reset(&commands);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Drive each possible physical first slot using ordinary FIFO operations, then
 *  fill all slots. A linear expected index identifies every survivor after removal.
 *  Snapshot bytes verify that normal FIFO reads and rejected writes move no entries.
 */
static void CheckSaturation(void)
{
    uint32_t   capacity;
    uint32_t   width;
    uint32_t   first;
    uint32_t   removed;
    OS_Queue_t queue;
    byte_t     storage[TEST_QUEUE_BYTES];
    uint32_t   byte_index;
    byte_t     entry[TEST_ENTRY_BYTES];
    uint32_t   index;
    bool_t     accepted;
    byte_t     output[TEST_ENTRY_BYTES];
    byte_t     previous[TEST_QUEUE_BYTES];
    uint32_t   original;

    for (capacity = 1; capacity <= TEST_QUEUE_CAPACITY; ++capacity)
    {
        for (width = 1; width <= TEST_ENTRY_BYTES; ++width)
        {
            for (first = 0; first < capacity; ++first)
            {
                for (removed = 0; removed < capacity; ++removed)
                {
                    OS_Queue_Initialize(&queue, storage, width, capacity);
                    for (byte_index = 0; byte_index < width; ++byte_index)
                    {
                        entry[byte_index] = (byte_t)byte_index;
                    }
                    for (index = 0; index < first; ++index)
                    {
                        accepted = OS_Queue_Write(&queue, entry);
                        LOGASSERT(accepted);
                        accepted = OS_Queue_Read(output, &queue);
                        LOGASSERT(accepted);
                    }
                    for (index = 0; index < capacity; ++index)
                    {
                        for (byte_index = 0; byte_index < width; ++byte_index)
                        {
                            entry[byte_index] = (byte_t)(index + byte_index);
                        }
                        accepted = OS_Queue_Write(&queue, entry);
                        LOGASSERT(accepted);
                    }
                    for (index = 0; index < (capacity * width); ++index)
                    {
                        previous[index] = storage[index];
                    }
                    accepted = OS_Queue_Write(&queue, entry);
                    LOGASSERT(!accepted);
                    accepted = OS_Queue_Remove(&queue, UINT32_MAX);
                    LOGASSERT(!accepted);
                    for (index = 0; index < (capacity * width); ++index)
                    {
                        LOGASSERT(previous[index] == storage[index]);
                    }
                    accepted = OS_Queue_Remove(&queue, removed);
                    LOGASSERT(accepted);
                    for (index = 0; index < (capacity * width); ++index)
                    {
                        previous[index] = storage[index];
                    }
                    for (index = 0; index < (capacity - 1); ++index)
                    {
                        original = index;
                        if (index >= removed)
                        {
                            ++original;
                        }
                        accepted = OS_Queue_Read(output, &queue);
                        LOGASSERT(accepted);
                        for (byte_index = 0; byte_index < width; ++byte_index)
                        {
                            LOGASSERT((byte_t)(original + byte_index) == output[byte_index]);
                        }
                    }
                    LOGASSERT(0 == OS_Queue_GetEntryCount(&queue));
                    for (index = 0; index < (capacity * width); ++index)
                    {
                        LOGASSERT(previous[index] == storage[index]);
                    }
                }
            }
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Run storage and policy checks through the selected OS entry.
 */
uint32_t OS_Application_Main(void)
{
    OS_Process_SetInteractive(FALSE);
    CheckRings();
    CheckQueues();
    CheckSaturation();
    CheckCommandPolicies();

    return 0;
}
