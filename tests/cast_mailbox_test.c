/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Independent latest-value model and retained media-borrow regressions. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cast_media.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include "os/os_mailbox.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define MAILBOX_TEST_STEPS       (12)
#define MAILBOX_TEST_PATTERNS    (1 << MAILBOX_TEST_STEPS)
#define MAILBOX_TEST_REPETITIONS (1000)
#define MAILBOX_TEST_URL_BYTE    ('a')
#define MAILBOX_TEST_TYPE_BYTE   ('b')
#define MAILBOX_TEST_CHANGED     ('z')

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Abstract one-pending-value oracle independent of slot rotation. */
typedef struct Fixture_s
{
    OS_Mailbox_t mailbox;                       ///< Implementation under test.
    uint32_t     values[OS_MAILBOX_SLOT_COUNT]; ///< Caller-owned payload slots.
    uint32_t     published;                     ///< Abstract latest publication.
    uint32_t     acquired;                      ///< Abstract retained value.
    uint32_t     readIndex;                     ///< Borrowed implementation slot.
    bool_t       pending;                       ///< A publication remains unread.
    bool_t       readable;                      ///< The abstract consumer owns a value.
} Fixture_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/** Startup-owned test state, including maximum-size source strings. */
static Fixture_t           s_fixture;
static CastMedia_Channel_t s_proxy;
static CastMedia_Channel_t s_replacement;
static char_t              s_url[CAST_MEDIA_URL_CAPACITY];
static char_t              s_type[CAST_MEDIA_TYPE_CAPACITY];

/** Independent complete publication pairs. */
static const char_t        c_firstUrl[]  = "http://localhost/first.mp4";
static const char_t        c_firstType[] = "video/mp4";
static const char_t        c_lastUrl[]   = "http://localhost/last.webm";
static const char_t        c_lastType[]  = "video/webm";

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Establishes empty implementation and oracle state while quiescent.
 *
 * \par Details:
 *  Both models are empty.
 *
 * \par Requirements:
 *  - 001 No payload shall be read before publication/acquisition.
 *
 * \param[out] fixture   Required retained test state
 *
 * \retval     None
 */
static FORCE_INLINE void InitializeFixture(Fixture_t* fixture);

/*************************************************************************************************/
/**
 * \brief Checks producer ownership and retained-reader stability across publication.
 *
 * \par Details:
 *  A complete new abstract value is pending.
 *
 * \par Requirements:
 *  - 001 Publication values shall be strictly increasing in this bounded test.
 *  - 002 A prior reader slot shall remain stable and disjoint from the writable slot.
 *
 * \param[in,out] fixture   Required initialized fixture
 *
 * \retval        None
 */
static FORCE_INLINE void PublishValue(Fixture_t* fixture);

/*************************************************************************************************/
/**
 * \brief Checks consumer behavior against the abstract latest-value oracle.
 *
 * \par Details:
 *  The observed implementation state matches the oracle.
 *
 * \par Requirements:
 *  - 001 Availability and acquired value shall match the independent oracle.
 *  - 002 Empty reads shall preserve the prior borrow.
 *
 * \param[in,out] fixture   Required initialized fixture
 *
 * \retval        None
 */
static FORCE_INLINE void TakeValue(Fixture_t* fixture);

/*************************************************************************************************/
/**
 * \brief Enumerates all publish/take sequences of a fixed length.
 *
 * \par Details:
 *  Every transition agrees with the independent latest-value model.
 *
 * \par Requirements:
 *  - 001 Every sequence shall begin empty and finish with repeated draining attempts.
 *  - 002 No expected slot indexes shall be derived from the implementation's rotation algorithm.
 *
 * \retval None
 */
static FORCE_INLINE void CheckTransitions(void);

/*************************************************************************************************/
/**
 * \brief Checks one borrowed maximum-size publication without following caller strings.
 *
 * \par Details:
 *  Counts, payloads and terminators remain intact.
 *
 * \par Requirements:
 *  - 001 Every byte and terminator shall match its independent marker.
 *
 * \param[in] media   Required acquired maximum-size pair
 *
 * \retval    None
 */
static FORCE_INLINE void CheckLongPair(const CastMedia_t* media);

/*************************************************************************************************/
/**
 * \brief Checks coherent publication, independent channels and borrow lifetime.
 *
 * \par Details:
 *  Every media-publication requirement is checked.
 *
 * \par Requirements:
 *  - 001 Changing input after publication shall not change the retained result.
 *  - 002 Repeated producer updates shall not overwrite the consumer's current borrow.
 *  - 003 The next successful acquisition shall return the latest complete pair.
 *  - 004 Quiescent initialization shall discard pending publications.
 *
 * \retval None
 */
static FORCE_INLINE void CheckMedia(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Payload slots deliberately remain untouched until producer ownership permits a write.
 */
static FORCE_INLINE void InitializeFixture(Fixture_t* fixture)
{
    OS_Mailbox_Initialize(&fixture->mailbox);
    fixture->published = 0;
    fixture->acquired  = 0;
    fixture->readIndex = 0;
    fixture->pending   = FALSE;
    fixture->readable  = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only the abstract publication counter changes; slot identities are observed, not predicted.
 */
static FORCE_INLINE void PublishValue(Fixture_t* fixture)
{
    uint32_t index;

    index = OS_Mailbox_GetWriteIndex(&fixture->mailbox);
    LOGASSERT(OS_MAILBOX_SLOT_COUNT > index);
    if (fixture->readable)
    {
        LOGASSERT(fixture->readIndex != index);
    }
    ++fixture->published;
    fixture->values[index] = fixture->published;
    OS_Mailbox_Publish(&fixture->mailbox);
    fixture->pending = TRUE;
    if (fixture->readable)
    {
        LOGASSERT(fixture->readIndex == OS_Mailbox_GetReadIndex(&fixture->mailbox));
        LOGASSERT(OS_MAILBOX_SLOT_COUNT > fixture->readIndex);
        LOGASSERT(fixture->acquired == fixture->values[fixture->readIndex]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  An empty observation must neither consume nor invalidate an existing reader borrow.
 */
static FORCE_INLINE void TakeValue(Fixture_t* fixture)
{
    bool_t   available;
    uint32_t index;

    available = OS_Mailbox_TryTake(&fixture->mailbox);
    LOGASSERT(fixture->pending == available);
    if (available)
    {
        index = OS_Mailbox_GetReadIndex(&fixture->mailbox);
        LOGASSERT(OS_MAILBOX_SLOT_COUNT > index);
        LOGASSERT(fixture->published == fixture->values[index]);
        fixture->readIndex = index;
        fixture->acquired  = fixture->published;
        fixture->pending   = FALSE;
        fixture->readable  = TRUE;
    }
    else if (fixture->readable)
    {
        LOGASSERT(fixture->readIndex == OS_Mailbox_GetReadIndex(&fixture->mailbox));
        LOGASSERT(OS_MAILBOX_SLOT_COUNT > fixture->readIndex);
        LOGASSERT(fixture->acquired == fixture->values[fixture->readIndex]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Sequence bits select operations, covering long producer/consumer runs and alternating roles.
 */
static FORCE_INLINE void CheckTransitions(void)
{
    uint32_t pattern;
    uint32_t step;
    uint32_t operation;

    for (pattern = 0; MAILBOX_TEST_PATTERNS > pattern; ++pattern)
    {
        InitializeFixture(&s_fixture);
        TakeValue(&s_fixture);
        for (step = 0; MAILBOX_TEST_STEPS > step; ++step)
        {
            operation = (pattern >> step) & 1;
            if (operation)
            {
                PublishValue(&s_fixture);
            }
            else
            {
                TakeValue(&s_fixture);
            }
        }
        TakeValue(&s_fixture);
        TakeValue(&s_fixture);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every marker byte is checked after producer storage and later write slots have changed.
 */
static FORCE_INLINE void CheckLongPair(const CastMedia_t* media)
{
    uint32_t index;

    LOGASSERT(NULL != media);
    LOGASSERT(CAST_MEDIA_URL_CAPACITY - 1 == media->urlByteCount);
    LOGASSERT(CAST_MEDIA_TYPE_CAPACITY - 1 == media->typeByteCount);
    for (index = 0; CAST_MEDIA_URL_CAPACITY - 1 > index; ++index)
    {
        LOGASSERT(MAILBOX_TEST_URL_BYTE == media->url[index]);
    }
    LOGASSERT(0 == media->url[index]);
    for (index = 0; CAST_MEDIA_TYPE_CAPACITY - 1 > index; ++index)
    {
        LOGASSERT(MAILBOX_TEST_TYPE_BYTE == media->type[index]);
    }
    LOGASSERT(0 == media->type[index]);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A retained maximum-size pair survives a thousand newer publications and source mutation.
 */
static FORCE_INLINE void CheckMedia(void)
{
    const CastMedia_t* retained;
    uint32_t           iteration;
    const CastMedia_t* latest;
    const CastMedia_t* replacement;

    CastMedia_Initialize(&s_proxy);
    CastMedia_Initialize(&s_replacement);
    LOGASSERT(NULL == CastMedia_Take(&s_proxy));
    CastMedia_Publish(&s_proxy, s_url, s_type);
    retained = CastMedia_Take(&s_proxy);
    CheckLongPair(retained);
    CRT_Memory_Set(s_url, MAILBOX_TEST_CHANGED, sizeof(s_url) - 1);
    CRT_Memory_Set(s_type, MAILBOX_TEST_CHANGED, sizeof(s_type) - 1);
    LOGASSERT(NULL == CastMedia_Take(&s_proxy));
    CheckLongPair(retained);
    CastMedia_Publish(&s_replacement, c_firstUrl, c_firstType);
    for (iteration = 0; MAILBOX_TEST_REPETITIONS > iteration; ++iteration)
    {
        CastMedia_Publish(&s_proxy, c_firstUrl, c_firstType);
        CastMedia_Publish(&s_proxy, c_lastUrl, c_lastType);
        CheckLongPair(retained);
    }
    latest = CastMedia_Take(&s_proxy);
    LOGASSERT(NULL != latest);
    LOGASSERT(sizeof(c_lastUrl) - 1 == latest->urlByteCount);
    LOGASSERT(sizeof(c_lastType) - 1 == latest->typeByteCount);
    LOGASSERT(0 == CRT_Memory_Compare(c_lastUrl, latest->url, sizeof(c_lastUrl)));
    LOGASSERT(0 == CRT_Memory_Compare(c_lastType, latest->type, sizeof(c_lastType)));
    LOGASSERT(NULL == CastMedia_Take(&s_proxy));
    replacement = CastMedia_Take(&s_replacement);
    LOGASSERT(NULL != replacement);
    LOGASSERT(sizeof(c_firstUrl) - 1 == replacement->urlByteCount);
    LOGASSERT(sizeof(c_firstType) - 1 == replacement->typeByteCount);
    LOGASSERT(0 == CRT_Memory_Compare(c_firstUrl, replacement->url, sizeof(c_firstUrl)));
    LOGASSERT(0 == CRT_Memory_Compare(c_firstType, replacement->type, sizeof(c_firstType)));
    CastMedia_Publish(&s_proxy, c_firstUrl, c_firstType);
    CastMedia_Initialize(&s_proxy);
    LOGASSERT(NULL == CastMedia_Take(&s_proxy));
    CastMedia_Publish(&s_proxy, c_lastUrl, c_lastType);
    latest = CastMedia_Take(&s_proxy);
    LOGASSERT(NULL != latest);
    LOGASSERT(0 == CRT_Memory_Compare(c_lastUrl, latest->url, sizeof(c_lastUrl)));
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  All payload storage exists at startup. Test initialization establishes maximum-size inputs once.
 */
uint32_t OS_Application_Main(void)
{
    CRT_Memory_Set(s_url, MAILBOX_TEST_URL_BYTE, sizeof(s_url) - 1);
    CRT_Memory_Set(s_type, MAILBOX_TEST_TYPE_BYTE, sizeof(s_type) - 1);
    s_url[CAST_MEDIA_URL_CAPACITY - 1]   = 0;
    s_type[CAST_MEDIA_TYPE_CAPACITY - 1] = 0;
    CheckTransitions();
    CheckMedia();

    return 0;
}
