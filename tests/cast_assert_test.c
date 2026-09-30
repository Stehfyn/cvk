/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Contract misuse must take the production fail-fast path in Release builds.
 * Each invocation selects one deliberate programming error; the Python runner
 * checks the Windows exception exit code independently of normal functional tests.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_assert.h"
#include "cast_session.h"
#include "os/os_atomic.h"
#include "os/os_ringbuffer.h"
#include "os/os_types.h"
#include <stdint.h>
#include "os/os_process.h"
#include "os/os_queue.h"
#include "os/os_mailbox.h"
#include "crt/crt_memory.h"
#include "utl/utl_http.h"
#include "utl/utl_arguments.h"
#include "utl/utl_unicode.h"
#include "cast_commands.h"
#include "cast_events.h"
#include "os/os_thread.h"
#include "utl/utl_json.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define ASSERT_CASE_NULL_READER         ('h')
#define ASSERT_CASE_NULL_FIELDS         ('s')
#define ASSERT_CASE_INVALID_KIND        ('k')
#define ASSERT_CASE_NULL_HEAD           ('r')
#define ASSERT_CASE_NULL_INPUT          ('b')
#define ASSERT_CASE_SHRINKING_PREFIX    ('p')
#define ASSERT_CASE_MOVED_INPUT         ('m')
#define ASSERT_CASE_NULL_DATE           ('d')
#define ASSERT_CASE_NULL_DATE_OUTPUT    ('o')
#define ASSERT_CASE_NULL_EVENTS         ('e')
#define ASSERT_CASE_NULL_COMMANDS       ('c')
#define ASSERT_CASE_NULL_WORKER         ('w')
#define ASSERT_CASE_NULL_CALLBACK       ('j')
#define ASSERT_CASE_NULL_JSON_READER        ('v')
#define ASSERT_CASE_NULL_JSON_VALUES         ('x')
#define ASSERT_CASE_SUCCESS             ('t')
#define ASSERT_CASE_NULL_UNIT_COUNT     ('n')
#define ASSERT_CASE_NULL_UNIT_OUTPUT    ('q')
#define ASSERT_CASE_NULL_UNIT_INPUT     ('i')
#define ASSERT_CASE_NULL_UNICODE        ('u')
#define ASSERT_CASE_NULL_OS_COUNT       ('a')
#define ASSERT_CASE_NULL_OS_OUTPUT      ('z')
#define ASSERT_CASE_NULL_RECORDS        ('R')
#define ASSERT_CASE_NULL_UNITS          ('N')
#define ASSERT_CASE_INVALID_RECORDS     ('C')
#define ASSERT_CASE_NO_MATCH            (2)
#define ASSERT_CASE_RETURNED            (3)
#define ASSERT_CASE_NULL_RING           ('0')
#define ASSERT_CASE_NULL_RING_BYTES     ('1')
#define ASSERT_CASE_ZERO_RING           ('2')
#define ASSERT_CASE_RING_OVERFLOW       ('3')
#define ASSERT_CASE_RING_UNDERFLOW      ('4')
#define ASSERT_CASE_RING_READ_OFFSET    ('5')
#define ASSERT_CASE_RING_WRITE_OFFSET   ('6')
#define ASSERT_CASE_NULL_QUEUE          ('7')
#define ASSERT_CASE_NULL_QUEUE_BYTES    ('8')
#define ASSERT_CASE_ZERO_QUEUE_WIDTH    ('9')
#define ASSERT_CASE_ZERO_QUEUE          ('A')
#define ASSERT_CASE_QUEUE_OVERFLOW      ('B')
#define ASSERT_CASE_NULL_QUEUE_OUTPUT   ('D')
#define ASSERT_CASE_NULL_QUEUE_INPUT    ('E')
#define ASSERT_CASE_NULL_COMMAND_PUSH   ('F')
#define ASSERT_CASE_NULL_COMMAND_TAKE   ('G')
#define ASSERT_CASE_NULL_COMMAND_OUTPUT ('H')
#define ASSERT_CASE_NULL_SPAN_COUNT     ('I')
#define ASSERT_CASE_CRT_COPY_INPUT      ('J')
#define ASSERT_CASE_CRT_COPY_OUTPUT     ('K')
#define ASSERT_CASE_CRT_MOVE_INPUT      ('L')
#define ASSERT_CASE_CRT_MOVE_OUTPUT     ('M')
#define ASSERT_CASE_CRT_FIRST           ('O')
#define ASSERT_CASE_CRT_SECOND          ('P')
#define ASSERT_CASE_CRT_SET_OUTPUT      ('Q')
#define ASSERT_CASE_ATOMIC_INITIALIZE32 ('!')
#define ASSERT_CASE_ATOMIC_INITIALIZE64 ('#')
#define ASSERT_CASE_ATOMIC_LOAD32       ('$')
#define ASSERT_CASE_ATOMIC_LOAD64       ('%')
#define ASSERT_CASE_ATOMIC_STORE32      ('&')
#define ASSERT_CASE_ATOMIC_STORE64      ('(')
#define ASSERT_CASE_ATOMIC_EXCHANGE32   (')')
#define ASSERT_CASE_ATOMIC_EXCHANGE64   ('*')
#define ASSERT_CASE_ATOMIC_COMPARE32    ('+')
#define ASSERT_CASE_ATOMIC_COMPARE64    (',')
#define ASSERT_CASE_ATOMIC_ADD32        ('-')
#define ASSERT_CASE_ATOMIC_ADD64        ('.')
#define ASSERT_CASE_ATOMIC_ALIGN32      ('/')
#define ASSERT_CASE_ATOMIC_ALIGN64      (':')
#define ASSERT_CASE_EVENT_BEGIN         (';')
#define ASSERT_CASE_EVENT_PUBLISH       ('<')
#define ASSERT_CASE_EVENT_TAKE          ('=')
#define ASSERT_CASE_EVENT_OUTPUT        ('>')
#define ASSERT_CASE_MAILBOX_INITIALIZE  ('S')
#define ASSERT_CASE_MAILBOX_EMPTY_READ  ('T')
#define ASSERT_CASE_MAILBOX_WRITE       ('U')
#define ASSERT_CASE_MAILBOX_PUBLISH     ('V')
#define ASSERT_CASE_MAILBOX_TAKE        ('W')

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/** Retained input prefixes for reader ownership checks. */
static const char_t          c_input[]    = "GET / HTTP/1.1\r\nHost: x\r\n\r\n";
static const char_t          c_other[]    = "GET / HTTP/1.1\r\nHost: y\r\n\r\n";
static const uint16_t        c_argument[] = {'t'};

/** Initialization-owned mailbox fixture; avoids a large transient test stack frame. */
static CastEvents_t          s_events;
static CastEvents_Snapshot_t s_snapshot;

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Supplies a valid callback without doing background work.
 *
 * \param[in] context   Unused callback context
 *
 * \retval    None No value is returned.
 */
static void EmptyJob(void* context);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void EmptyJob(void* context)
{
    (void)context;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  A single decoded UTF-16 argument selects one invariant violation. Process
 *  services isolate native argument access, error-dialog policy and termination.
 */
uint32_t OS_Application_Main(void)
{
    uint32_t               unit_count;
    bool_t                 has_argument;
    uint16_t               argument[1];
    bool_t                 valid_selector;
    uint16_t               selected;
    Utl_Arguments_Record_t record;
    Utl_Arguments_t        arguments;
    OS_Mailbox_t           mailbox;
    uint64_t               alignment_storage[2];
    byte_t                 bytes[2];
    OS_RingBuffer_t        ring;
    OS_Queue_t             queue;
    CastQueuedCommand_t    queued_command;
    CastCommands_t         commands;
    Utl_Http_Head_t        head;
    Utl_Http_Field_t       fields[2];
    OS_Thread_t            worker;
    Utl_Http_HeadReader_t  reader;
    Utl_Http_Date_t        date;
    Utl_Json_Reader_t json_reader;
    Utl_Json_Value_t json_values[1];

    OS_Process_SetInteractive(FALSE);
    unit_count     = 0;
    has_argument   = OS_Process_ReadArgument(&unit_count, argument, 1, 1);
    valid_selector = has_argument && (1 == unit_count);
    if (!valid_selector)
    {
        return ASSERT_CASE_NO_MATCH;
    }
    selected              = argument[0];
    record.units          = c_argument;
    record.unitCount      = 1;
    arguments.records     = &record;
    arguments.recordCount = 1;
    switch (selected)
    {
    case ASSERT_CASE_MAILBOX_INITIALIZE:
        OS_Mailbox_Initialize(NULL);
        break;
    case ASSERT_CASE_MAILBOX_EMPTY_READ:
        OS_Mailbox_Initialize(&mailbox);
        (void)OS_Mailbox_GetReadIndex(&mailbox);
        break;
    case ASSERT_CASE_MAILBOX_WRITE:
        (void)OS_Mailbox_GetWriteIndex(NULL);
        break;
    case ASSERT_CASE_MAILBOX_PUBLISH:
        OS_Mailbox_Publish(NULL);
        break;
    case ASSERT_CASE_MAILBOX_TAKE:
        (void)OS_Mailbox_TryTake(NULL);
        break;
    case ASSERT_CASE_ATOMIC_INITIALIZE32:
        OS_Atomic_Initialize32(NULL, 0);
        break;
    case ASSERT_CASE_ATOMIC_INITIALIZE64:
        OS_Atomic_Initialize64(NULL, 0);
        break;
    case ASSERT_CASE_ATOMIC_LOAD32:
        (void)OS_Atomic_Load32(NULL);
        break;
    case ASSERT_CASE_ATOMIC_LOAD64:
        (void)OS_Atomic_Load64(NULL);
        break;
    case ASSERT_CASE_ATOMIC_STORE32:
        OS_Atomic_Store32(NULL, 0);
        break;
    case ASSERT_CASE_ATOMIC_STORE64:
        OS_Atomic_Store64(NULL, 0);
        break;
    case ASSERT_CASE_ATOMIC_EXCHANGE32:
        (void)OS_Atomic_Exchange32(NULL, 0);
        break;
    case ASSERT_CASE_ATOMIC_EXCHANGE64:
        (void)OS_Atomic_Exchange64(NULL, 0);
        break;
    case ASSERT_CASE_ATOMIC_COMPARE32:
        (void)OS_Atomic_CompareExchange32(NULL, 0, 0);
        break;
    case ASSERT_CASE_ATOMIC_COMPARE64:
        (void)OS_Atomic_CompareExchange64(NULL, 0, 0);
        break;
    case ASSERT_CASE_ATOMIC_ADD32:
        (void)OS_Atomic_FetchAdd32(NULL, 0);
        break;
    case ASSERT_CASE_ATOMIC_ADD64:
        (void)OS_Atomic_FetchAdd64(NULL, 0);
        break;
    case ASSERT_CASE_ATOMIC_ALIGN32:
        OS_Atomic_Initialize32((OS_Atomic32_t*)((byte_t*)alignment_storage + 1), 0);
        break;
    case ASSERT_CASE_NULL_RING:
        OS_RingBuffer_Initialize(NULL, bytes, 2);
        break;
    case ASSERT_CASE_NULL_RING_BYTES:
        OS_RingBuffer_Initialize(&ring, NULL, 2);
        break;
    case ASSERT_CASE_ZERO_RING:
        OS_RingBuffer_Initialize(&ring, bytes, 0);
        break;
    case ASSERT_CASE_RING_OVERFLOW:
        OS_RingBuffer_Initialize(&ring, bytes, 2);
        OS_RingBuffer_Produce(&ring, 3);
        break;
    case ASSERT_CASE_RING_UNDERFLOW:
        OS_RingBuffer_Initialize(&ring, bytes, 2);
        OS_RingBuffer_Consume(&ring, 1);
        break;
    case ASSERT_CASE_RING_READ_OFFSET:
        OS_RingBuffer_Initialize(&ring, bytes, 2);
        (void)OS_RingBuffer_GetReadableSpan(&unit_count, &ring, 1);
        break;
    case ASSERT_CASE_RING_WRITE_OFFSET:
        OS_RingBuffer_Initialize(&ring, bytes, 2);
        (void)OS_RingBuffer_GetWritableSpan(&unit_count, &ring, 3);
        break;
    case ASSERT_CASE_NULL_QUEUE:
        OS_Queue_Initialize(NULL, bytes, 1, 2);
        break;
    case ASSERT_CASE_NULL_QUEUE_BYTES:
        OS_Queue_Initialize(&queue, NULL, 1, 2);
        break;
    case ASSERT_CASE_ZERO_QUEUE_WIDTH:
        OS_Queue_Initialize(&queue, bytes, 0, 2);
        break;
    case ASSERT_CASE_ATOMIC_ALIGN64:
        OS_Atomic_Initialize64((OS_Atomic64_t*)((byte_t*)alignment_storage + 1), 0);
        break;
    case ASSERT_CASE_EVENT_BEGIN:
        CastEvents_Begin(NULL, 0);
        break;
    case ASSERT_CASE_EVENT_PUBLISH:
        (void)CastEvents_Publish(NULL, 0, CAST_EVENT_LOG, NULL, NULL);
        break;
    case ASSERT_CASE_EVENT_TAKE:
        (void)CastEvents_Take(NULL, &s_snapshot);
        break;
    case ASSERT_CASE_EVENT_OUTPUT:
        CastEvents_Initialize(&s_events);
        (void)CastEvents_Take(&s_events, NULL);
        break;
    case ASSERT_CASE_ZERO_QUEUE:
        OS_Queue_Initialize(&queue, bytes, 1, 0);
        break;
    case ASSERT_CASE_QUEUE_OVERFLOW:
        OS_Queue_Initialize(&queue, bytes, 2, UINT32_MAX);
        break;
    case ASSERT_CASE_INVALID_RECORDS:
        arguments.recordCount = 0;
        (void)Utl_Arguments_Read(&unit_count, argument, 1, &arguments, 0);
        break;
    case ASSERT_CASE_NULL_QUEUE_OUTPUT:
        OS_Queue_Initialize(&queue, bytes, 1, 2);
        (void)OS_Queue_Read(NULL, &queue);
        break;
    case ASSERT_CASE_NULL_QUEUE_INPUT:
        OS_Queue_Initialize(&queue, bytes, 1, 2);
        (void)OS_Queue_Write(&queue, NULL);
        break;
    case ASSERT_CASE_NULL_COMMAND_PUSH:
        (void)CastCommands_Push(NULL, CAST_COMMAND_STOP, 0.0);
        break;
    case ASSERT_CASE_NULL_COMMAND_TAKE:
        (void)CastCommands_Take(NULL, &queued_command);
        break;
    case ASSERT_CASE_NULL_COMMAND_OUTPUT:
        CastCommands_Reset(&commands);
        (void)CastCommands_Take(&commands, NULL);
        break;
    case ASSERT_CASE_NULL_SPAN_COUNT:
        OS_RingBuffer_Initialize(&ring, bytes, 2);
        (void)OS_RingBuffer_GetReadableSpan(NULL, &ring, 0);
        break;
    case ASSERT_CASE_CRT_COPY_INPUT:
        CRT_Memory_Copy(bytes, NULL, 0);
        break;
    case ASSERT_CASE_CRT_COPY_OUTPUT:
        CRT_Memory_Copy(NULL, bytes, 0);
        break;
    case ASSERT_CASE_CRT_MOVE_INPUT:
        CRT_Memory_Move(bytes, NULL, 0);
        break;
    case ASSERT_CASE_CRT_MOVE_OUTPUT:
        CRT_Memory_Move(NULL, bytes, 0);
        break;
    case ASSERT_CASE_NULL_UNITS:
        record.units = NULL;
        (void)Utl_Arguments_Read(&unit_count, argument, 1, &arguments, 0);
        break;
    case ASSERT_CASE_CRT_FIRST:
        (void)CRT_Memory_Compare(NULL, bytes, 0);
        break;
    case ASSERT_CASE_CRT_SECOND:
        (void)CRT_Memory_Compare(bytes, NULL, 0);
        break;
    case ASSERT_CASE_CRT_SET_OUTPUT:
        CRT_Memory_Set(NULL, 0, 0);
        break;
    case ASSERT_CASE_NULL_RECORDS:
        arguments.records = NULL;
        (void)Utl_Arguments_Read(&unit_count, argument, 1, &arguments, 0);
        break;
    case ASSERT_CASE_NULL_OS_COUNT:
        (void)OS_Process_ReadArgument(NULL, argument, 1, 1);
        break;
    case ASSERT_CASE_NULL_INPUT:
        head.fields        = fields;
        head.fieldCapacity = 2;
        (void)Utl_Http_ReadHead(&head, UTL_HTTP_REQUEST, NULL, 1);
        break;
    case ASSERT_CASE_NULL_COMMANDS:
        CastCommands_Reset(NULL);
        break;
    case ASSERT_CASE_NULL_DATE:
        (void)Utl_Http_GetDateByteCount(NULL);
        break;
    case ASSERT_CASE_NULL_EVENTS:
        CastEvents_Initialize(NULL);
        break;
    case ASSERT_CASE_NULL_READER:
        Utl_Http_InitializeHeadReader(NULL, UTL_HTTP_REQUEST, fields, 2);
        break;
    case ASSERT_CASE_NULL_UNIT_INPUT:
        (void)Utl_Arguments_Read(&unit_count, argument, 1, NULL, 0);
        break;
    case ASSERT_CASE_NULL_CALLBACK:
        (void)OS_Thread_Initialize(&worker, NULL, NULL);
        break;
    case ASSERT_CASE_INVALID_KIND:
        Utl_Http_InitializeHeadReader(&reader, (Utl_Http_Kind_t)99, fields, 2);
        break;
    case ASSERT_CASE_MOVED_INPUT:
        Utl_Http_InitializeHeadReader(&reader, UTL_HTTP_REQUEST, fields, 2);
        (void)Utl_Http_ReadHeadIncremental(&reader, (const byte_t*)c_input, 4);
        (void)Utl_Http_ReadHeadIncremental(&reader, (const byte_t*)c_other, sizeof(c_other) - 1);
        break;
    case ASSERT_CASE_NULL_UNIT_COUNT:
        (void)Utl_Arguments_Read(NULL, argument, 1, &arguments, 0);
        break;
    case ASSERT_CASE_NULL_DATE_OUTPUT:
        date.year   = 2026;
        date.month  = 9;
        date.day    = 27;
        date.hour   = 0;
        date.minute = 0;
        date.second = 0;
        (void)Utl_Http_WriteDate(&date, NULL, UTL_HTTP_DATE_BYTE_COUNT);
        break;
    case ASSERT_CASE_SHRINKING_PREFIX:
        Utl_Http_InitializeHeadReader(&reader, UTL_HTTP_REQUEST, fields, 2);
        (void)Utl_Http_ReadHeadIncremental(&reader, (const byte_t*)c_input, 4);
        (void)Utl_Http_ReadHeadIncremental(&reader, (const byte_t*)c_input, 3);
        break;
    case ASSERT_CASE_NULL_UNIT_OUTPUT:
        (void)Utl_Arguments_Read(&unit_count, NULL, 1, &arguments, 0);
        break;
    case ASSERT_CASE_NULL_HEAD:
        (void)Utl_Http_ReadHead(NULL, UTL_HTTP_REQUEST, (const byte_t*)c_input, sizeof(c_input) - 1);
        break;
    case ASSERT_CASE_NULL_FIELDS:
        Utl_Http_InitializeHeadReader(&reader, UTL_HTTP_REQUEST, NULL, 1);
        break;
    case ASSERT_CASE_SUCCESS:
        Utl_Http_InitializeHeadReader(&reader, UTL_HTTP_REQUEST, NULL, 0);
        CastCommands_Reset(&commands);
        CastEvents_Initialize(&s_events);
        Utl_Json_InitializeReader(&json_reader, json_values, 1);
        LOGASSERT(UTL_HTTP_INCOMPLETE == reader.result);
        LOGASSERT(0 == OS_Queue_GetEntryCount(&commands.queue));
        LOGASSERT(0 == OS_Atomic_Load32(&s_events.terminal));
        return 0;
    case ASSERT_CASE_NULL_UNICODE:
        (void)Utl_Unicode_GetUtf16StringUnitCount(NULL, 0);
        break;
    case ASSERT_CASE_NULL_JSON_READER:
        Utl_Json_InitializeReader(NULL, json_values, 1);
        break;
    case ASSERT_CASE_NULL_WORKER:
        (void)OS_Thread_Initialize(NULL, EmptyJob, NULL);
        break;
    case ASSERT_CASE_NULL_JSON_VALUES:
        Utl_Json_InitializeReader(&json_reader, NULL, 1);
        break;
    case ASSERT_CASE_NULL_OS_OUTPUT:
        (void)OS_Process_ReadArgument(&unit_count, NULL, 1, 1);
        break;
    default:
        return ASSERT_CASE_NO_MATCH;
    }
    return ASSERT_CASE_RETURNED;
}
