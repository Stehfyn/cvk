/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Independent AudioSpecificConfig field encodings from ISO/IEC 14496-3:2009,
 * 1.6.2.1 and 4.4.1; configuration module contracts and rejection behavior.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_mpeg4.h"
#include "utl/utl_mpeg4audio.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_CAPACITY          (512)
#define TEST_SENTINEL          (0xA5)
#define TEST_RATE_44100        (44100)
#define TEST_RATE_48000        (48000)
#define TEST_RATE_EXPLICIT     (12345)
#define TEST_STEREO            (2)
#define TEST_MONO              (1)
#define TEST_DELAY_OVERFLOW    (0x4000)
#define TEST_RATE_OVERFLOW     (0x1000000)
#define TEST_COMMENT_LIMIT     (255)
#define TEST_LC                "\x12\x10"
#define TEST_EXPLICIT_RATE     "\x17\x80\x18\x1C\x90"
#define TEST_CORE_DELAY        "\x12\x13\xFF\xF8"
#define TEST_PCE               "\x11\x80\x04\xC4\x00\x00\x20\x00"
#define TEST_PCE_COMMENT       "\x11\x80\x04\xC4\x00\x00\x20\x03\x00\x41\xFF"
#define TEST_HE                "\x2B\x92\x08\x00"
#define TEST_PS                "\xEB\x8A\x08\x00"
#define TEST_SBR_SYNC          "\x13\x90\x56\xE5\xA0"
#define TEST_PS_SYNC           "\x13\x88\x56\xE5\xA5\x48\x80"
#define TEST_SBR_ABSENT        "\x12\x10\x56\xE5\x00"
#define TEST_RESERVED_RATE     "\x16\x90"
#define TEST_ZERO_RATE         "\x17\x80\x00\x00\x10"
#define TEST_ALIGNMENT         "\x2B\x92\x08\x01"
#define TEST_PS_STEREO         "\xEB\x92\x08\x00"
#define TEST_CELP              "\x42\x00"
#define TEST_UNKNOWN_EXTENSION "\x12\x10\xFF\xFF\xFF"

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Entry-owned reusable fixture storage. */
typedef struct TestContext_s
{
    byte_t output[TEST_CAPACITY];                        ///< Output bytes.
    byte_t before[TEST_CAPACITY];                        ///< Whole-buffer rejection sentinel.
    byte_t payload[TEST_CAPACITY];                       ///< Stable comment bytes.
    byte_t beforeModel[sizeof(Utl_Mpeg4Audio_Config_t)]; ///< Exact object representation before rejection.
} TestContext_t;

/*************************************************************************************************/
/** \brief Independent input and expected domain values. */
typedef struct TestVector_s
{
    Utl_Mpeg4_Span_t bytes;     ///< Complete known wire bytes.
    uint32_t         rate;      ///< Expected signaled output rate.
    uint32_t         channels;  ///< Expected output channels.
    bool_t           mandatory; ///< Every shorter prefix omits mandatory syntax.
} TestVector_t;

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
 * \brief Checks both rejection classification and unchanged complete output.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The entry-owned fixture context shall remain exclusive and initialized.
 *  - 002 Any violated expected result shall assert.
 *
 * \param[in,out] context    Required fixture storage
 * \param[in]     bytes      Exact malformed/unsupported input
 * \param[in]     expected   Expected rejection category
 *
 * \retval None
 */
static FORCE_INLINE void CheckRejected(TestContext_t*          context,
                                       Utl_Mpeg4_Span_t        bytes,
                                       Utl_Mpeg4Audio_Status_t expected);

/*************************************************************************************************/
/**
 * \brief Checks every insufficient destination capacity against a whole-buffer sentinel.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The entry-owned fixture context shall remain exclusive and initialized.
 *  - 002 Any violated expected result shall assert.
 *
 * \param[in,out] context   Required fixture storage
 * \param[in]     config    Required valid stable model
 *
 * \retval None
 */
static FORCE_INLINE void CheckShortWrites(TestContext_t* context, const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Checks independent wire bytes, derived semantics and capacity/truncation boundaries.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The entry-owned fixture context shall remain exclusive and initialized.
 *  - 002 Any violated expected result shall assert.
 *
 * \param[in,out] context   Required fixture storage
 * \param[in]     vector    Required independent expected vector
 *
 * \retval None
 */
static FORCE_INLINE void CheckVector(TestContext_t* context, const TestVector_t* vector);

/*************************************************************************************************/
/**
 * \brief Checks LC, explicit frequency, core delay and complete PCE vectors.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The entry-owned fixture context shall remain exclusive and initialized.
 *  - 002 Any violated expected result shall assert.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None
 */
static FORCE_INLINE void CheckBaseVectors(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks explicit HE/PS and synchronized presence/absence without inferring unknown tools.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The entry-owned fixture context shall remain exclusive and initialized.
 *  - 002 Any violated expected result shall assert.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None
 */
static FORCE_INLINE void CheckExtensionVectors(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks malformed and unsupported inputs as distinct unchanged-output outcomes.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The entry-owned fixture context shall remain exclusive and initialized.
 *  - 002 Any violated expected result shall assert.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None
 */
static FORCE_INLINE void CheckMalformed(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks complete output preservation for inconsistent writer models.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The entry-owned fixture context shall remain exclusive and initialized.
 *  - 002 Any violated expected result shall assert.
 *
 * \param[in,out] context   Required fixture storage
 * \param[in]     config    Required invalid model
 *
 * \retval None
 */
static FORCE_INLINE void CheckInvalidModel(TestContext_t* context, const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Checks width, array, duplicate-selector and profile relationships before output mutation.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The entry-owned fixture context shall remain exclusive and initialized.
 *  - 002 Any violated expected result shall assert.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None
 */
static FORCE_INLINE void CheckModelBoundaries(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks maximum opaque comments and complete capacity preflight.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The entry-owned fixture context shall remain exclusive and initialized.
 *  - 002 Any violated expected result shall assert.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None
 */
static FORCE_INLINE void CheckProgramComment(TestContext_t* context);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckRejected(TestContext_t* context, Utl_Mpeg4_Span_t bytes, Utl_Mpeg4Audio_Status_t expected)
{
    Utl_Mpeg4Audio_Config_t result;
    Utl_Mpeg4Audio_Status_t status;

    Utl_Mpeg4Audio_InitializeConfig(&result);
    CRT_Memory_Copy(context->beforeModel, &result, sizeof(result));
    status = Utl_Mpeg4Audio_ReadConfig(&result, bytes);
    LOGASSERT(expected == status);
    LOGASSERT(0 == CRT_Memory_Compare(context->beforeModel, &result, sizeof(result)));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckShortWrites(TestContext_t* context, const Utl_Mpeg4Audio_Config_t* config)
{
    uint32_t count;
    uint32_t capacity;
    uint32_t written;

    count = Utl_Mpeg4Audio_GetConfigByteCount(config);
    LOGASSERT(0 < count);
    for (capacity = 0; capacity < count; ++capacity)
    {
        CRT_Memory_Set(context->output, TEST_SENTINEL, sizeof(context->output));
        CRT_Memory_Copy(context->before, context->output, sizeof(context->output));
        written = Utl_Mpeg4Audio_WriteConfig(config, context->output, capacity);
        LOGASSERT(0 == written);
        LOGASSERT(0 == CRT_Memory_Compare(context->before, context->output, sizeof(context->output)));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckVector(TestContext_t* context, const TestVector_t* vector)
{
    Utl_Mpeg4Audio_Status_t status;
    Utl_Mpeg4Audio_Config_t config;
    uint32_t                written;
    Utl_Mpeg4_Span_t        prefix;
    uint32_t                count;

    status = Utl_Mpeg4Audio_ReadConfig(&config, vector->bytes);
    LOGASSERT(UTL_MPEG4AUDIO_COMPLETE == status);
    LOGASSERT(vector->rate == Utl_Mpeg4Audio_GetSampleRate(&config));
    LOGASSERT(vector->channels == Utl_Mpeg4Audio_GetChannelCount(&config));
    LOGASSERT(vector->bytes.byteCount == Utl_Mpeg4Audio_GetConfigByteCount(&config));
    CRT_Memory_Set(context->output, TEST_SENTINEL, sizeof(context->output));
    written = Utl_Mpeg4Audio_WriteConfig(&config, context->output, vector->bytes.byteCount);
    LOGASSERT(vector->bytes.byteCount == written);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, vector->bytes.data, written));
    LOGASSERT(TEST_SENTINEL == context->output[written]);
    CheckShortWrites(context, &config);
    if (vector->mandatory)
    {
        prefix.data = vector->bytes.data;
        for (count = 0; count < vector->bytes.byteCount; ++count)
        {
            prefix.byteCount = count;
            CheckRejected(context, prefix, UTL_MPEG4AUDIO_INVALID);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckBaseVectors(TestContext_t* context)
{
    TestVector_t vector;

    vector.bytes.data      = (const byte_t*)TEST_LC;
    vector.bytes.byteCount = sizeof(TEST_LC) - 1;
    vector.rate            = TEST_RATE_44100;
    vector.channels        = TEST_STEREO;
    vector.mandatory       = TRUE;
    CheckVector(context, &vector);
    vector.bytes.data      = (const byte_t*)TEST_EXPLICIT_RATE;
    vector.bytes.byteCount = sizeof(TEST_EXPLICIT_RATE) - 1;
    vector.rate            = TEST_RATE_EXPLICIT;
    CheckVector(context, &vector);
    vector.bytes.data      = (const byte_t*)TEST_CORE_DELAY;
    vector.bytes.byteCount = sizeof(TEST_CORE_DELAY) - 1;
    vector.rate            = TEST_RATE_44100;
    CheckVector(context, &vector);
    vector.bytes.data      = (const byte_t*)TEST_PCE;
    vector.bytes.byteCount = sizeof(TEST_PCE) - 1;
    vector.rate            = TEST_RATE_48000;
    CheckVector(context, &vector);
    vector.bytes.data      = (const byte_t*)TEST_PCE_COMMENT;
    vector.bytes.byteCount = sizeof(TEST_PCE_COMMENT) - 1;
    CheckVector(context, &vector);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckExtensionVectors(TestContext_t* context)
{
    TestVector_t            vector;
    Utl_Mpeg4Audio_Status_t status;
    Utl_Mpeg4Audio_Config_t config;

    vector.bytes.data      = (const byte_t*)TEST_HE;
    vector.bytes.byteCount = sizeof(TEST_HE) - 1;
    vector.rate            = TEST_RATE_44100;
    vector.channels        = TEST_STEREO;
    vector.mandatory       = TRUE;
    CheckVector(context, &vector);
    vector.bytes.data      = (const byte_t*)TEST_PS;
    vector.bytes.byteCount = sizeof(TEST_PS) - 1;
    CheckVector(context, &vector);
    status = Utl_Mpeg4Audio_ReadConfig(&config, vector.bytes);
    LOGASSERT(UTL_MPEG4AUDIO_COMPLETE == status);
    LOGASSERT(UTL_MPEG4AUDIO_PRESENT == config.ps);
    LOGASSERT(TEST_MONO == config.channelConfiguration);
    vector.bytes.data      = (const byte_t*)TEST_SBR_SYNC;
    vector.bytes.byteCount = sizeof(TEST_SBR_SYNC) - 1;
    vector.mandatory       = FALSE;
    CheckVector(context, &vector);
    vector.bytes.data      = (const byte_t*)TEST_PS_SYNC;
    vector.bytes.byteCount = sizeof(TEST_PS_SYNC) - 1;
    CheckVector(context, &vector);
    vector.bytes.data      = (const byte_t*)TEST_SBR_ABSENT;
    vector.bytes.byteCount = sizeof(TEST_SBR_ABSENT) - 1;
    CheckVector(context, &vector);
    status = Utl_Mpeg4Audio_ReadConfig(&config, vector.bytes);
    LOGASSERT(UTL_MPEG4AUDIO_COMPLETE == status);
    LOGASSERT(UTL_MPEG4AUDIO_ABSENT == config.sbr);
    LOGASSERT(UTL_MPEG4AUDIO_UNKNOWN == config.ps);
    vector.bytes.data      = (const byte_t*)TEST_ALIGNMENT;
    vector.bytes.byteCount = sizeof(TEST_ALIGNMENT) - 1;
    status                 = Utl_Mpeg4Audio_ReadConfig(&config, vector.bytes);
    LOGASSERT(UTL_MPEG4AUDIO_COMPLETE == status);
    LOGASSERT(sizeof(TEST_HE) - 1 == Utl_Mpeg4Audio_WriteConfig(&config, context->output, sizeof(context->output)));
    LOGASSERT(0 == CRT_Memory_Compare(context->output, TEST_HE, sizeof(TEST_HE) - 1));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckMalformed(TestContext_t* context)
{
    Utl_Mpeg4_Span_t bytes;

    bytes.data      = (const byte_t*)TEST_RESERVED_RATE;
    bytes.byteCount = sizeof(TEST_RESERVED_RATE) - 1;
    CheckRejected(context, bytes, UTL_MPEG4AUDIO_INVALID);
    bytes.data      = (const byte_t*)TEST_ZERO_RATE;
    bytes.byteCount = sizeof(TEST_ZERO_RATE) - 1;
    CheckRejected(context, bytes, UTL_MPEG4AUDIO_INVALID);
    bytes.data      = (const byte_t*)TEST_PS_STEREO;
    bytes.byteCount = sizeof(TEST_PS_STEREO) - 1;
    CheckRejected(context, bytes, UTL_MPEG4AUDIO_INVALID);
    bytes.data      = (const byte_t*)TEST_CELP;
    bytes.byteCount = sizeof(TEST_CELP) - 1;
    CheckRejected(context, bytes, UTL_MPEG4AUDIO_UNSUPPORTED);
    bytes.data      = (const byte_t*)TEST_UNKNOWN_EXTENSION;
    bytes.byteCount = sizeof(TEST_UNKNOWN_EXTENSION) - 1;
    CheckRejected(context, bytes, UTL_MPEG4AUDIO_UNSUPPORTED);
    bytes.data      = NULL;
    bytes.byteCount = 0;
    CheckRejected(context, bytes, UTL_MPEG4AUDIO_INVALID);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckInvalidModel(TestContext_t* context, const Utl_Mpeg4Audio_Config_t* config)
{
    uint32_t written;

    CRT_Memory_Set(context->output, TEST_SENTINEL, sizeof(context->output));
    CRT_Memory_Copy(context->before, context->output, sizeof(context->output));
    LOGASSERT(0 == Utl_Mpeg4Audio_GetConfigByteCount(config));
    written = Utl_Mpeg4Audio_WriteConfig(config, context->output, sizeof(context->output));
    LOGASSERT(0 == written);
    LOGASSERT(0 == CRT_Memory_Compare(context->before, context->output, sizeof(context->output)));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckModelBoundaries(TestContext_t* context)
{
    Utl_Mpeg4Audio_Config_t config;
    Utl_Mpeg4_Span_t        bytes;
    Utl_Mpeg4Audio_Status_t status;

    Utl_Mpeg4Audio_InitializeConfig(&config);
    LOGASSERT(TEST_RATE_48000 == Utl_Mpeg4Audio_GetSampleRate(&config));
    config.objectType = UTL_MPEG4AUDIO_SSR;
    config.shortFrame = TRUE;
    CheckInvalidModel(context, &config);
    Utl_Mpeg4Audio_InitializeConfig(&config);
    config.hasCoreCoder   = TRUE;
    config.coreCoderDelay = TEST_DELAY_OVERFLOW;
    CheckInvalidModel(context, &config);
    Utl_Mpeg4Audio_InitializeConfig(&config);
    config.frequency.index = UTL_MPEG4AUDIO_FREQUENCY_ESCAPE;
    config.frequency.hertz = TEST_RATE_OVERFLOW;
    CheckInvalidModel(context, &config);
    bytes.data      = (const byte_t*)TEST_PCE;
    bytes.byteCount = sizeof(TEST_PCE) - 1;
    status          = Utl_Mpeg4Audio_ReadConfig(&config, bytes);
    LOGASSERT(UTL_MPEG4AUDIO_COMPLETE == status);
    config.program.sideCount = 1;
    config.program.side[0]   = config.program.front[0];
    CheckInvalidModel(context, &config);
    config.program.sideCount  = 0;
    config.program.frontCount = UTL_MPEG4AUDIO_ELEMENT_LIMIT + 1;
    CheckInvalidModel(context, &config);
    config.program.frontCount  = 1;
    config.program.objectIndex = UTL_MPEG4AUDIO_SSR - 1;
    CheckInvalidModel(context, &config);
    config.program.objectIndex    = UTL_MPEG4AUDIO_LC - 1;
    config.program.frequencyIndex = 0;
    CheckInvalidModel(context, &config);
    config.program.frequencyIndex = config.frequency.index;
    config.program.lfeCount       = 2;
    config.program.lfe[0]         = 0;
    config.program.lfe[1]         = 0;
    CheckInvalidModel(context, &config);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckProgramComment(TestContext_t* context)
{
    Utl_Mpeg4_Span_t        bytes;
    Utl_Mpeg4Audio_Status_t status;
    Utl_Mpeg4Audio_Config_t config;
    uint32_t                count;
    uint32_t                written;
    Utl_Mpeg4Audio_Config_t decoded;

    bytes.data      = (const byte_t*)TEST_PCE;
    bytes.byteCount = sizeof(TEST_PCE) - 1;
    status          = Utl_Mpeg4Audio_ReadConfig(&config, bytes);
    LOGASSERT(UTL_MPEG4AUDIO_COMPLETE == status);
    CRT_Memory_Set(context->payload, TEST_SENTINEL, TEST_COMMENT_LIMIT);
    config.program.comment.data      = context->payload;
    config.program.comment.byteCount = TEST_COMMENT_LIMIT;
    count                            = Utl_Mpeg4Audio_GetConfigByteCount(&config);
    LOGASSERT(sizeof(TEST_PCE) - 1 + TEST_COMMENT_LIMIT == count);
    written = Utl_Mpeg4Audio_WriteConfig(&config, context->output, sizeof(context->output));
    LOGASSERT(count == written);
    bytes.data      = context->output;
    bytes.byteCount = written;
    status          = Utl_Mpeg4Audio_ReadConfig(&decoded, bytes);
    LOGASSERT(UTL_MPEG4AUDIO_COMPLETE == status);
    LOGASSERT(TEST_COMMENT_LIMIT == decoded.program.comment.byteCount);
    LOGASSERT(0 == CRT_Memory_Compare(context->payload, decoded.program.comment.data, TEST_COMMENT_LIMIT));
    CheckShortWrites(context, &config);
    config.program.comment.byteCount = TEST_COMMENT_LIMIT + 1;
    CheckInvalidModel(context, &config);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t OS_Application_Main(void)
{
    TestContext_t context;

    CheckBaseVectors(&context);
    CheckExtensionVectors(&context);
    CheckMalformed(&context);
    CheckModelBoundaries(&context);
    CheckProgramComment(&context);
    return 0;
}
