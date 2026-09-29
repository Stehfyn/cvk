/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Native concurrent verification of the caller-owned single-producer mailbox. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cast_events.h"
#include "cast_media.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"

#include <Windows.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define EVENT_TEST_ITERATIONS    (200000)
#define EVENT_TEST_TIMEOUT       (10000)
#define EVENT_TEST_STACK_BYTES   (65536)
#define EVENT_TEST_GENERATION    (37)
#define EVENT_TEST_HEX_COUNT     (8)
#define EVENT_TEST_HEX_BITS      (4)
#define EVENT_TEST_HEX_RADIX     (16)
#define EVENT_TEST_HEX_MASK      (15)
#define EVENT_TEST_MARKER_COUNT  (26)
#define EVENT_TEST_STATE_COUNT   (CAST_STATE_ENDED + 1)
#define EVENT_TEST_INVALID_EVENT (255)
#define EVENT_TEST_SENTINEL      (0x5A)
#define EVENT_TEST_ZERO          ('0')
#define EVENT_TEST_NINE          ('9')
#define EVENT_TEST_HEX_FIRST     ('a')
#define EVENT_TEST_HEX_LAST      ('f')
#define EVENT_TEST_FIRST_MARKER  ('A')
#define EVENT_TEST_TERMINATOR    ('\0')

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Native harness context borrowed by one producer until it exits.
 */
typedef struct EventProducer_s
{
    CastEvents_t* events; ///< Sole-producer mailbox side.
    HANDLE        gate;   ///< Initialization gate.
} EventProducer_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/** Fixed startup-owned fixtures, including the full-capacity unterminated input. */
static CastEvents_t          s_events;
static CastEvents_Snapshot_t s_snapshot;
static CastEvents_Snapshot_t s_expected;
static CastStatus            s_status;
static char_t                s_text[CAST_EVENTS_TEXT_CAPACITY];
static CastMedia_Channel_t   s_media;
static char_t                s_mediaUrl[CAST_MEDIA_URL_CAPACITY];
static char_t                s_mediaType[CAST_MEDIA_TYPE_CAPACITY];

/** Independent expected strings and hexadecimal alphabet. */
static const char_t          c_first[]    = "first";
static const char_t          c_second[]   = "second";
static const char_t          c_complete[] = "complete";
static const char_t          c_hex[]      = "0123456789abcdef";

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Derives the repeated payload marker from a publication sequence.
 *
 * \par Requirements:
 *  - 001 Caller-owned fixtures shall outlive every native test thread.
 *
 * \param[in] sequence   One-based publication number
 *
 * \retval    A..Z   Expected payload marker.
 */
static FORCE_INLINE char_t SequenceCharacter(uint32_t sequence);

/*************************************************************************************************/
/**
 * \brief Checks and decodes the independent hexadecimal log sequence.
 *
 * \par Requirements:
 *  - 001 Caller-owned fixtures shall outlive every native test thread.
 *
 * \param[in] text   Eight readable hexadecimal bytes
 *
 * \retval    1..EVENT_TEST_ITERATIONS   Valid publication sequence.
 */
static FORCE_INLINE uint32_t ReadSequence(const char_t* text);

/*************************************************************************************************/
/**
 * \brief Writes a status with independently checkable fields.
 *
 * \par Requirements:
 *  - 001 Caller-owned fixtures shall outlive every native test thread.
 *
 * \param[out] status     Producer-owned status fixture
 * \param[in]  sequence   One-based publication number
 *
 * \retval     None
 */
static FORCE_INLINE void WriteStatus(CastStatus* status, uint32_t sequence);

/*************************************************************************************************/
/**
 * \brief Writes a full-capacity log fixture with an encoded sequence.
 *
 * \par Requirements:
 *  - 001 Caller-owned fixtures shall outlive every native test thread.
 *
 * \param[out] text       CAST_EVENTS_TEXT_CAPACITY writable bytes
 * \param[in]  sequence   One-based publication number
 *
 * \retval     None
 */
static FORCE_INLINE void WriteLog(char_t* text, uint32_t sequence);

/*************************************************************************************************/
/**
 * \brief Publishes two differently sized fields carrying the same sequence marker.
 *
 * \par Details:
 *  A maximum-size coherent pair is published.
 *
 * \par Requirements:
 *  - 001 Only the initialized test producer shall access source buffers and publish media.
 *  - 002 Input shall provide the complete EVENT_TEST_HEX_COUNT prefix and repeated marker payload.
 *
 * \param[in] text   Stable complete log-pattern input
 *
 * \retval    None
 */
static FORCE_INLINE void PublishMedia(const char_t* text);

/*************************************************************************************************/
/**
 * \brief Checks a concurrently acquired pair against its independent sequence pattern.
 *
 * \par Requirements:
 *  - 001 Both fields shall describe the same sequence, strictly newer than the previous acquired pair.
 *  - 002 Every payload byte and terminator shall match the declared counts and sequence marker.
 *
 * \param[in] media      Required acquired consumer borrow
 * \param[in] previous   Last acquired sequence, or zero
 *
 * \retval    sequence   Validated current sequence.
 */
static FORCE_INLINE uint32_t CheckMediaPair(const CastMedia_t* media, uint32_t previous);

/*************************************************************************************************/
/**
 * \brief Checks rejection, coalescing, bounded text and quiescent reuse.
 *
 * \par Requirements:
 *  - 001 Caller-owned fixtures shall outlive every native test thread.
 *
 * \retval None
 */
static FORCE_INLINE void CheckSequential(void);

/*************************************************************************************************/
/**
 * \brief Publishes changing status and log payloads from the sole producer.
 *
 * \par Requirements:
 *  - 001 Caller-owned fixtures shall outlive every native test thread.
 *
 * \param[in] context   Non-NULL initialized producer fixture
 *
 * \retval    0   All publications and terminal event completed.
 */
static DWORD WINAPI PublishEvents(LPVOID context);

/*************************************************************************************************/
/**
 * \brief Verifies snapshot coherence, monotonic coalescing and final delivery.
 *
 * \par Requirements:
 *  - 001 Caller-owned fixtures shall outlive every native test thread.
 *
 * \retval None
 */
static FORCE_INLINE void CheckConcurrent(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE char_t SequenceCharacter(uint32_t sequence)
{
    return (char_t)(EVENT_TEST_FIRST_MARKER + (sequence % EVENT_TEST_MARKER_COUNT));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every other payload byte is validated separately to detect torn copies.
 */
static FORCE_INLINE uint32_t ReadSequence(const char_t* text)
{
    uint32_t sequence;
    uint32_t index;
    char_t   value;
    bool_t   is_decimal;
    bool_t   is_letter;
    uint32_t digit;

    sequence = 0;
    for (index = 0; index < EVENT_TEST_HEX_COUNT; ++index)
    {
        value      = text[index];
        is_decimal = (EVENT_TEST_ZERO <= value) && (value <= EVENT_TEST_NINE);
        is_letter  = (EVENT_TEST_HEX_FIRST <= value) && (value <= EVENT_TEST_HEX_LAST);
        LOGASSERT(is_decimal || is_letter);
        digit    = is_decimal ? (uint32_t)(value - EVENT_TEST_ZERO) : (uint32_t)(value - EVENT_TEST_HEX_FIRST + 10);
        sequence = (sequence * EVENT_TEST_HEX_RADIX) + digit;
    }
    LOGASSERT(sequence > 0);
    LOGASSERT(sequence <= EVENT_TEST_ITERATIONS);

    return sequence;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All bytes start defined, including padding; each field encodes the same sequence.
 */
static FORCE_INLINE void WriteStatus(CastStatus* status, uint32_t sequence)
{
    char_t   marker;
    uint32_t index;

    CRT_Memory_Set(status, 0, sizeof(CastStatus));
    marker              = SequenceCharacter(sequence);
    status->state       = (CastPlayerState)(sequence % EVENT_TEST_STATE_COUNT);
    status->currentTime = (double)sequence;
    status->duration    = (float64_t)sequence * 2.0;
    status->hasMedia    = TRUE;
    for (index = 0; index < (sizeof(status->stateName) - 1); ++index)
    {
        status->stateName[index] = marker;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The prefix identifies the publication; the repeated suffix exposes mixed-slot copies.
 */
static FORCE_INLINE void WriteLog(char_t* text, uint32_t sequence)
{
    uint32_t index;
    uint32_t shift;
    uint32_t digit;
    char_t   marker;

    for (index = 0; index < EVENT_TEST_HEX_COUNT; ++index)
    {
        shift       = (EVENT_TEST_HEX_COUNT - index - 1) * EVENT_TEST_HEX_BITS;
        digit       = (sequence >> shift) & EVENT_TEST_HEX_MASK;
        text[index] = c_hex[digit];
    }
    marker = SequenceCharacter(sequence);
    for (index = EVENT_TEST_HEX_COUNT; index < (CAST_EVENTS_TEXT_CAPACITY - 1); ++index)
    {
        text[index] = marker;
    }
    text[CAST_EVENTS_TEXT_CAPACITY - 1] = EVENT_TEST_TERMINATOR;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Rejected and empty operations must preserve the whole output. A log-only delivery
 *  must preserve its caller's status and reason bytes.
 */
static FORCE_INLINE void CheckSequential(void)
{
    uint32_t index;

    CastEvents_Initialize(&s_events);
    CRT_Memory_Set(&s_snapshot, EVENT_TEST_SENTINEL, sizeof(CastEvents_Snapshot_t));
    CRT_Memory_Copy(&s_expected, &s_snapshot, sizeof(CastEvents_Snapshot_t));
    LOGASSERT(!CastEvents_Take(&s_events, &s_snapshot));
    LOGASSERT(0 == CRT_Memory_Compare(&s_expected, &s_snapshot, sizeof(CastEvents_Snapshot_t)));
    LOGASSERT(!CastEvents_Publish(&s_events, 1, CAST_EVENT_LOG, NULL, c_first));
    LOGASSERT(!CastEvents_Publish(&s_events, 0, (CastEvent)EVENT_TEST_INVALID_EVENT, NULL, c_first));
    LOGASSERT(!CastEvents_Publish(&s_events, 0, CAST_EVENT_STATUS, NULL, NULL));
    LOGASSERT(!CastEvents_Take(&s_events, &s_snapshot));
    LOGASSERT(0 == CRT_Memory_Compare(&s_expected, &s_snapshot, sizeof(CastEvents_Snapshot_t)));
    LOGASSERT(CastEvents_Publish(&s_events, 0, CAST_EVENT_LOG, NULL, c_first));
    LOGASSERT(CastEvents_Publish(&s_events, 0, CAST_EVENT_LOG, NULL, c_second));
    LOGASSERT(CastEvents_Take(&s_events, &s_snapshot));
    LOGASSERT(CAST_EVENTS_LOG == s_snapshot.present);
    LOGASSERT(0 == CRT_Memory_Compare(c_second, s_snapshot.log, sizeof(c_second)));
    LOGASSERT(0 == CRT_Memory_Compare(&s_expected.status, &s_snapshot.status, sizeof(CastStatus)));
    LOGASSERT(0 == CRT_Memory_Compare(s_expected.reason, s_snapshot.reason, CAST_EVENTS_TEXT_CAPACITY));

    WriteStatus(&s_status, 1);
    LOGASSERT(CastEvents_Publish(&s_events, 0, CAST_EVENT_STATUS, &s_status, NULL));
    WriteStatus(&s_status, 2);
    LOGASSERT(CastEvents_Publish(&s_events, 0, CAST_EVENT_STATUS, &s_status, NULL));
    CRT_Memory_Set(s_text, EVENT_TEST_FIRST_MARKER, sizeof(s_text));
    LOGASSERT(CastEvents_Publish(&s_events, 0, CAST_EVENT_LOG, NULL, s_text));
    LOGASSERT(CastEvents_Publish(&s_events, 0, CAST_EVENT_ENDED, NULL, c_complete));
    LOGASSERT(!CastEvents_Publish(&s_events, 0, CAST_EVENT_LOG, NULL, c_first));
    LOGASSERT(!CastEvents_Publish(&s_events, 0, CAST_EVENT_STATUS, &s_status, NULL));
    LOGASSERT(!CastEvents_Publish(&s_events, 0, CAST_EVENT_ENDED, NULL, c_first));
    LOGASSERT(CastEvents_Take(&s_events, &s_snapshot));
    LOGASSERT((CAST_EVENTS_STATUS | CAST_EVENTS_LOG | CAST_EVENTS_ENDED) == s_snapshot.present);
    LOGASSERT(0 == CRT_Memory_Compare(&s_status, &s_snapshot.status, sizeof(CastStatus)));
    LOGASSERT(0 == CRT_Memory_Compare(c_complete, s_snapshot.reason, sizeof(c_complete)));
    for (index = 0; index < (CAST_EVENTS_TEXT_CAPACITY - 1); ++index)
    {
        LOGASSERT(EVENT_TEST_FIRST_MARKER == s_snapshot.log[index]);
    }
    LOGASSERT(EVENT_TEST_TERMINATOR == s_snapshot.log[CAST_EVENTS_TEXT_CAPACITY - 1]);
    CRT_Memory_Copy(&s_expected, &s_snapshot, sizeof(CastEvents_Snapshot_t));
    LOGASSERT(!CastEvents_Take(&s_events, &s_snapshot));
    LOGASSERT(0 == CRT_Memory_Compare(&s_expected, &s_snapshot, sizeof(CastEvents_Snapshot_t)));
    CastEvents_Begin(&s_events, 1);
    LOGASSERT(!CastEvents_Publish(&s_events, 0, CAST_EVENT_LOG, NULL, c_first));
    LOGASSERT(CastEvents_Publish(&s_events, 1, CAST_EVENT_LOG, NULL, NULL));
    LOGASSERT(CastEvents_Take(&s_events, &s_snapshot));
    LOGASSERT(CAST_EVENTS_LOG == s_snapshot.present);
    LOGASSERT(EVENT_TEST_TERMINATOR == s_snapshot.log[0]);
    LOGASSERT(CastEvents_Publish(&s_events, 1, CAST_EVENT_LOG, NULL, c_first));
    CastEvents_Begin(&s_events, 2);
    LOGASSERT(!CastEvents_Take(&s_events, &s_snapshot));
    LOGASSERT(CastEvents_Publish(&s_events, 2, CAST_EVENT_ENDED, NULL, NULL));
    LOGASSERT(CastEvents_Take(&s_events, &s_snapshot));
    LOGASSERT(CAST_EVENTS_ENDED == s_snapshot.present);
    LOGASSERT(EVENT_TEST_TERMINATOR == s_snapshot.reason[0]);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Source slots are producer-owned; each field has an independent bounded terminator.
 */
static FORCE_INLINE void PublishMedia(const char_t* text)
{
    CRT_Memory_Copy(s_mediaUrl, text, CAST_MEDIA_URL_CAPACITY - 1);
    CRT_Memory_Copy(s_mediaType, text, CAST_MEDIA_TYPE_CAPACITY - 1);
    s_mediaUrl[CAST_MEDIA_URL_CAPACITY - 1]   = EVENT_TEST_TERMINATOR;
    s_mediaType[CAST_MEDIA_TYPE_CAPACITY - 1] = EVENT_TEST_TERMINATOR;
    CastMedia_Publish(&s_media, s_mediaUrl, s_mediaType);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Distinct fields must agree even if the producer cycles through many subsequent write slots.
 */
static FORCE_INLINE uint32_t CheckMediaPair(const CastMedia_t* media, uint32_t previous)
{
    uint32_t sequence;
    char_t   marker;
    uint32_t index;

    LOGASSERT(NULL != media);
    LOGASSERT(CAST_MEDIA_URL_CAPACITY - 1 == media->urlByteCount);
    LOGASSERT(CAST_MEDIA_TYPE_CAPACITY - 1 == media->typeByteCount);
    sequence = ReadSequence(media->url);
    LOGASSERT(previous < sequence);
    LOGASSERT(sequence == ReadSequence(media->type));
    marker = SequenceCharacter(sequence);
    for (index = EVENT_TEST_HEX_COUNT; CAST_MEDIA_URL_CAPACITY - 1 > index; ++index)
    {
        LOGASSERT(marker == media->url[index]);
    }
    LOGASSERT(EVENT_TEST_TERMINATOR == media->url[index]);
    for (index = EVENT_TEST_HEX_COUNT; CAST_MEDIA_TYPE_CAPACITY - 1 > index; ++index)
    {
        LOGASSERT(marker == media->type[index]);
    }
    LOGASSERT(EVENT_TEST_TERMINATOR == media->type[index]);

    return sequence;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The producer owns its stack payloads and does not wait for the consumer between
 *  publications. Only the initialization gate uses a native wait.
 */
static DWORD WINAPI PublishEvents(LPVOID context)
{
    EventProducer_t* producer;
    DWORD            wait_result;
    uint32_t         sequence;
    CastStatus       status;
    char_t           text[CAST_EVENTS_TEXT_CAPACITY];

    producer = context;
    LOGASSERT(NULL != producer);
    wait_result = WaitForSingleObject(producer->gate, EVENT_TEST_TIMEOUT);
    LOGASSERT(WAIT_OBJECT_0 == wait_result);
    for (sequence = 1; sequence <= EVENT_TEST_ITERATIONS; ++sequence)
    {
        WriteStatus(&status, sequence);
        WriteLog(text, sequence);
        LOGASSERT(CastEvents_Publish(producer->events, EVENT_TEST_GENERATION, CAST_EVENT_STATUS, &status, NULL));
        LOGASSERT(CastEvents_Publish(producer->events, EVENT_TEST_GENERATION, CAST_EVENT_LOG, NULL, text));
        PublishMedia(text);
    }
    LOGASSERT(CastEvents_Publish(producer->events, EVENT_TEST_GENERATION, CAST_EVENT_ENDED, NULL, c_complete));
    LOGASSERT(!CastEvents_Publish(producer->events, EVENT_TEST_GENERATION, CAST_EVENT_LOG, NULL, c_first));

    return 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The consumer checks every payload byte it receives while the producer runs.
 *  Terminal observation must drain the final status and log before quiescent reuse.
 */
static FORCE_INLINE void CheckConcurrent(void)
{
    EventProducer_t    producer;
    HANDLE             thread;
    uint64_t           started;
    uint32_t           last_status;
    uint32_t           last_log;
    uint32_t           last_media;
    bool_t             available;
    const CastMedia_t* media;
    uint32_t           sequence;
    char_t             marker;
    uint32_t           index;
    DWORD              wait_result;

    CastEvents_Begin(&s_events, EVENT_TEST_GENERATION);
    CastMedia_Initialize(&s_media);
    producer.events = &s_events;
    producer.gate   = CreateEvent(NULL, TRUE, FALSE, NULL);
    LOGASSERT(NULL != producer.gate);
    thread = CreateThread(NULL, EVENT_TEST_STACK_BYTES, PublishEvents, &producer, 0, NULL);
    LOGASSERT(NULL != thread);
    started     = GetTickCount64();
    last_status = 0;
    last_log    = 0;
    last_media  = 0;
    LOGASSERT(SetEvent(producer.gate));
    for (;;)
    {
        LOGASSERT((GetTickCount64() - started) < EVENT_TEST_TIMEOUT);
        available = CastEvents_Take(&s_events, &s_snapshot);
        media     = CastMedia_Take(&s_media);
        if (NULL != media)
        {
            last_media = CheckMediaPair(media, last_media);
        }
        if (available)
        {
            if (s_snapshot.present & CAST_EVENTS_STATUS)
            {
                LOGASSERT(s_snapshot.status.currentTime >= 1.0);
                LOGASSERT(s_snapshot.status.currentTime <= EVENT_TEST_ITERATIONS);
                sequence = (uint32_t)s_snapshot.status.currentTime;
                LOGASSERT(sequence > last_status);
                LOGASSERT((double)sequence == s_snapshot.status.currentTime);
                LOGASSERT((float64_t)sequence * 2.0 == s_snapshot.status.duration);
                LOGASSERT((CastPlayerState)(sequence % EVENT_TEST_STATE_COUNT) == s_snapshot.status.state);
                LOGASSERT(TRUE == s_snapshot.status.hasMedia);
                marker = SequenceCharacter(sequence);
                for (index = 0; index < (sizeof(s_snapshot.status.stateName) - 1); ++index)
                {
                    LOGASSERT(marker == s_snapshot.status.stateName[index]);
                }
                LOGASSERT(EVENT_TEST_TERMINATOR == s_snapshot.status.stateName[index]);
                last_status = sequence;
            }
            if (s_snapshot.present & CAST_EVENTS_LOG)
            {
                sequence = ReadSequence(s_snapshot.log);
                LOGASSERT(sequence > last_log);
                marker = SequenceCharacter(sequence);
                for (index = EVENT_TEST_HEX_COUNT; index < (CAST_EVENTS_TEXT_CAPACITY - 1); ++index)
                {
                    LOGASSERT(marker == s_snapshot.log[index]);
                }
                LOGASSERT(EVENT_TEST_TERMINATOR == s_snapshot.log[index]);
                last_log = sequence;
            }
            if (s_snapshot.present & CAST_EVENTS_ENDED)
            {
                LOGASSERT(0 == CRT_Memory_Compare(c_complete, s_snapshot.reason, sizeof(c_complete)));
                LOGASSERT(EVENT_TEST_ITERATIONS == last_status);
                LOGASSERT(EVENT_TEST_ITERATIONS == last_log);
                LOGASSERT(EVENT_TEST_ITERATIONS == last_media);
                break;
            }
        }
    }
    wait_result = WaitForSingleObject(thread, EVENT_TEST_TIMEOUT);
    LOGASSERT(WAIT_OBJECT_0 == wait_result);
    LOGASSERT(CloseHandle(thread));
    LOGASSERT(CloseHandle(producer.gate));
    LOGASSERT(NULL == CastMedia_Take(&s_media));
    LOGASSERT(!CastEvents_Take(&s_events, &s_snapshot));
    CastEvents_Begin(&s_events, EVENT_TEST_GENERATION + 1);
    LOGASSERT(!CastEvents_Take(&s_events, &s_snapshot));
    LOGASSERT(CastEvents_Publish(&s_events, EVENT_TEST_GENERATION + 1, CAST_EVENT_LOG, NULL, c_second));
    LOGASSERT(CastEvents_Take(&s_events, &s_snapshot));
    LOGASSERT(CAST_EVENTS_LOG == s_snapshot.present);
    LOGASSERT(0 == CRT_Memory_Compare(c_second, s_snapshot.log, sizeof(c_second)));
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Run state-contract and concurrent ownership checks with production assertions.
 */
uint32_t OS_Application_Main(void)
{
    OS_Process_SetInteractive(FALSE);
    CheckSequential();
    CheckConcurrent();

    return 0;
}
