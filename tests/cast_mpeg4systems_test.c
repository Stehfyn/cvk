/*=================================================================================================
** 1. References
**===============================================================================================*/

/** MPEG-4 Systems descriptor vectors, parent extents and transactional output checks. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_mpeg4.h"
#include "utl/utl_mpeg4systems.h"
#include "crt/crt_memory.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_CAPACITY    (512)
#define TEST_SENTINEL    (0xA5)
#define TEST_INFO        "\x05\x02\x12\x10"
#define TEST_PADDED_INFO "\x05\x80\x80\x80\x02\x12\x10"
#define TEST_DECODER     "\x04\x11\x40\x15\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" TEST_INFO
#define TEST_SL          "\x06\x01\x02"
#define TEST_CHILDREN    TEST_DECODER TEST_SL
#define TEST_ES          "\x03\x19\x00\x00\x00" TEST_CHILDREN
#define TEST_OPTIONAL_ES                                                                                               \
    "\x03\x21\x00\x01\xE5\x00\x02\x03"                                                                                 \
    "abc"                                                                                                              \
    "\x00\x03" TEST_CHILDREN
#define TEST_UNKNOWN             "\xC0\x01\xFF"
#define TEST_DUPLICATE_INFO      TEST_INFO TEST_UNKNOWN TEST_INFO
#define TEST_BAD_LENGTH          "\x05\x80\x80\x80\x80\x02\x12\x10"
#define TEST_OVERRUN             "\x03\x03\x00\x00\x00" TEST_CHILDREN
#define TEST_BAD_URL             "\x03\x21\x00\x01\xE5\x00\x02\x03\xED\xA0\x80\x00\x03" TEST_CHILDREN
#define TEST_PRIORITY            (5)
#define TEST_STREAM_ID           (1)
#define TEST_DEPENDENCY_ID       (2)
#define TEST_CLOCK_ID            (3)
#define TEST_LENGTH_BOUNDARY     (128)
#define TEST_LENGTH_PREFIX_BYTES (3)
#define TEST_SHORT_DEPENDENCY    "\x03\x04\x00\x01\x80\x00"
#define TEST_CHILD_OVERRUN                                                                                             \
    "\x03\x19\x00\x00\x00\x04\x40\x40\x15\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" TEST_INFO TEST_SL
#define TEST_BUFFER_OVERFLOW (0x1000000)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Entry-owned fixture storage reused by sequential cases. */
typedef struct TestContext_s
{
    byte_t output[TEST_CAPACITY];   ///< Writable destination.
    byte_t original[TEST_CAPACITY]; ///< Whole-output rejection sentinel.
    byte_t payload[TEST_CAPACITY];  ///< Stable byte input for length-width boundaries.
} TestContext_t;

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
 * \brief Creates a counted fixture borrow without a terminator scan.
 *
 * \par Requirements:
 *  - 001 Input shall remain readable throughout the associated fixture.
 *
 * \param[in] bytes   Required literal or retained byte array
 * \param[in] count   Exact readable bytes
 *
 * \retval span Complete counted borrow.
 */
static FORCE_INLINE Utl_Mpeg4_Span_t Bytes(const byte_t* bytes, uint32_t count);

/*************************************************************************************************/
/**
 * \brief Checks known descriptor bytes and all shorter destination capacities.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The context shall be initialized and exclusively owned by the fixture.
 *  - 002 Failed writes shall preserve every destination byte.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None
 */
static FORCE_INLINE void CheckDescriptor(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks the independent dependency, URL and clock fixture fields.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The model shall be borrowed from the complete optional-field fixture.
 *
 * \param[in] stream   Required complete model
 *
 * \retval None
 */
static FORCE_INLINE void CheckOptionalFields(const Utl_Mpeg4Systems_ElementaryStream_t* stream);

/*************************************************************************************************/
/**
 * \brief Checks all fixed and optional fields against the independent ES vectors.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The model shall be borrowed from one complete known fixture.
 *
 * \param[in] stream   Required complete model
 *
 * \retval None
 */
static FORCE_INLINE void CheckStreamFields(const Utl_Mpeg4Systems_ElementaryStream_t* stream);

/*************************************************************************************************/
/**
 * \brief Checks the independent audio decoder requirements.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The model shall be borrowed from the known MPEG-4 audio descriptor.
 *
 * \param[in] decoder   Required complete model
 *
 * \retval None
 */
static FORCE_INLINE void CheckDecoderFields(const Utl_Mpeg4Systems_DecoderConfig_t* decoder);

/*************************************************************************************************/
/**
 * \brief Checks a typed ES descriptor and its decoder configuration.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The known wire vector shall be independently specified.
 *  - 002 Typed read/write shall retain every expected byte and optional field.
 *
 * \param[in,out] context   Required fixture storage
 * \param[in]     input     Complete known wire vector
 *
 * \retval None
 */
static FORCE_INLINE void CheckStream(TestContext_t* context, Utl_Mpeg4_Span_t input);

/*************************************************************************************************/
/**
 * \brief Checks truncation, invalid nested extents and singleton duplication.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Rejected reads shall preserve all previously published result fields.
 *  - 002 A child outside its enclosing ES payload shall never be selected as that ES's decoder.
 *
 * \retval None
 */
static FORCE_INLINE void CheckRejected(void);

/*************************************************************************************************/
/**
 * \brief Checks the seven-bit expandable-length boundary.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Input and output shall be retained disjoint arrays.
 *  - 002 Length width shall change at 128 without losing payload bytes.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None
 */
static FORCE_INLINE void CheckLengths(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks malformed fields inside complete descriptor envelopes.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 A prior valid ES model shall survive every rejected nested field unchanged.
 *
 * \retval None
 */
static FORCE_INLINE void CheckMalformedParts(void);

/*************************************************************************************************/
/**
 * \brief Checks whole-buffer preservation for a rejected decoder model.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The invalid model and output shall remain stable and disjoint.
 *
 * \param[in,out] context   Required fixture storage
 * \param[in]     decoder   Required invalid model
 *
 * \retval None
 */
static FORCE_INLINE void CheckRejectedDecoder(TestContext_t* context, const Utl_Mpeg4Systems_DecoderConfig_t* decoder);

/*************************************************************************************************/
/**
 * \brief Checks decoder field domains and duplicate codec components.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Each invalid model shall fail before writing any output.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None
 */
static FORCE_INLINE void CheckInvalidDecoder(TestContext_t* context);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Mpeg4_Span_t Bytes(const byte_t* bytes, uint32_t count)
{
    Utl_Mpeg4_Span_t span;

    span.data      = bytes;
    span.byteCount = count;
    return span;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The padded input uses four length groups; the writer emits one length group.
 */
static FORCE_INLINE void CheckDescriptor(TestContext_t* context)
{
    Utl_Mpeg4Systems_Descriptor_t descriptor;
    uint32_t                      capacity;
    uint32_t                      count;

    LOGASSERT(Utl_Mpeg4Systems_ReadDescriptor(&descriptor,
              Bytes((const byte_t*)TEST_PADDED_INFO, sizeof(TEST_PADDED_INFO) - 1)));
    LOGASSERT(sizeof(TEST_PADDED_INFO) - 1 == descriptor.encodedByteCount);
    LOGASSERT(UTL_MPEG4SYSTEMS_TAG_DECODER_INFO == descriptor.tag);
    LOGASSERT(sizeof(TEST_INFO) - 1 == Utl_Mpeg4Systems_GetDescriptorByteCount(&descriptor));
    for (capacity = 0; capacity < sizeof(TEST_INFO) - 1; ++capacity)
    {
        CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
        LOGASSERT(0 == Utl_Mpeg4Systems_WriteDescriptor(&descriptor, context->output, capacity));
        LOGASSERT(0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
    }
    count = Utl_Mpeg4Systems_WriteDescriptor(&descriptor, context->output, sizeof(TEST_INFO) - 1);
    LOGASSERT(sizeof(TEST_INFO) - 1 == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, TEST_INFO, count));
    LOGASSERT(TEST_SENTINEL == context->output[sizeof(TEST_INFO) - 1]);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckOptionalFields(const Utl_Mpeg4Systems_ElementaryStream_t* stream)
{
    LOGASSERT(TEST_STREAM_ID == stream->streamId);
    LOGASSERT(TEST_PRIORITY == stream->priority);
    LOGASSERT(TEST_DEPENDENCY_ID == stream->dependsOn);
    LOGASSERT(stream->hasUrl);
    LOGASSERT(sizeof("abc") - 1 == stream->url.byteCount);
    LOGASSERT(0 == CRT_Memory_Compare(stream->url.data, "abc", stream->url.byteCount));
    LOGASSERT(stream->hasClock);
    LOGASSERT(TEST_CLOCK_ID == stream->clockId);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckStreamFields(const Utl_Mpeg4Systems_ElementaryStream_t* stream)
{
    if (stream->hasDependency)
    {
        CheckOptionalFields(stream);
    }
    else
    {
        LOGASSERT(0 == stream->streamId);
        LOGASSERT(0 == stream->priority);
        LOGASSERT(!stream->hasUrl);
        LOGASSERT(!stream->hasClock);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckDecoderFields(const Utl_Mpeg4Systems_DecoderConfig_t* decoder)
{
    LOGASSERT(UTL_MPEG4SYSTEMS_OBJECT_AUDIO == decoder->objectType);
    LOGASSERT(UTL_MPEG4SYSTEMS_STREAM_AUDIO == decoder->streamType);
    LOGASSERT(!decoder->upstream);
    LOGASSERT(0 == decoder->bufferBytes);
    LOGASSERT(0 == decoder->maximumBitrate);
    LOGASSERT(0 == decoder->averageBitrate);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Typed fields are compared with the independent vector, then canonical output
 *  is checked byte-for-byte. The nested codec body remains opaque at this layer.
 */
static FORCE_INLINE void CheckStream(TestContext_t* context, Utl_Mpeg4_Span_t input)
{
    Utl_Mpeg4Systems_Descriptor_t       descriptor;
    Utl_Mpeg4Systems_ElementaryStream_t stream;
    Utl_Mpeg4Systems_DecoderConfig_t    decoder;
    uint32_t                            count;
    uint32_t                            capacity;

    LOGASSERT(Utl_Mpeg4Systems_ReadDescriptor(&descriptor, input));
    LOGASSERT(input.byteCount == descriptor.encodedByteCount);
    LOGASSERT(Utl_Mpeg4Systems_ReadElementaryStream(&stream, &descriptor));
    LOGASSERT(sizeof(TEST_CHILDREN) - 1 == stream.children.byteCount);
    CheckStreamFields(&stream);
    LOGASSERT(Utl_Mpeg4Systems_FindDescriptor(&descriptor, stream.children, UTL_MPEG4SYSTEMS_TAG_DECODER_CONFIG));
    LOGASSERT(Utl_Mpeg4Systems_ReadDecoderConfig(&decoder, &descriptor));
    CheckDecoderFields(&decoder);
    count = Utl_Mpeg4Systems_WriteDecoderConfig(&decoder, context->output, sizeof(context->output));
    LOGASSERT(sizeof(TEST_DECODER) - 1 == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, TEST_DECODER, count));
    for (capacity = 0; capacity < input.byteCount; ++capacity)
    {
        CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
        LOGASSERT(0 == Utl_Mpeg4Systems_WriteElementaryStream(&stream, context->output, capacity));
        LOGASSERT(0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
    }
    count = Utl_Mpeg4Systems_WriteElementaryStream(&stream, context->output, input.byteCount);
    LOGASSERT(input.byteCount == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, input.data, count));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Prefix failure and malformed-child rejection are distinct: a short valid
 *  parent may not borrow bytes from a complete sibling following that parent.
 */
static FORCE_INLINE void CheckRejected(void)
{
    Utl_Mpeg4Systems_Descriptor_t       descriptor;
    Utl_Mpeg4Systems_Descriptor_t       original;
    uint32_t                            prefix;
    Utl_Mpeg4Systems_ElementaryStream_t stream;

    LOGASSERT(Utl_Mpeg4Systems_ReadDescriptor(&descriptor, Bytes((const byte_t*)TEST_INFO, sizeof(TEST_INFO) - 1)));
    original = descriptor;
    for (prefix = 0; prefix < sizeof(TEST_ES) - 1; ++prefix)
    {
        LOGASSERT(!Utl_Mpeg4Systems_ReadDescriptor(&descriptor, Bytes((const byte_t*)TEST_ES, prefix)));
        LOGASSERT(original.tag == descriptor.tag);
        LOGASSERT(original.payload.data == descriptor.payload.data);
        LOGASSERT(original.payload.byteCount == descriptor.payload.byteCount);
        LOGASSERT(original.encodedByteCount == descriptor.encodedByteCount);
    }
    LOGASSERT(!Utl_Mpeg4Systems_ReadDescriptor(&descriptor,
              Bytes((const byte_t*)TEST_BAD_LENGTH, sizeof(TEST_BAD_LENGTH) - 1)));
    LOGASSERT(
        !Utl_Mpeg4Systems_FindDescriptor(&descriptor,
                                         Bytes((const byte_t*)TEST_DUPLICATE_INFO, sizeof(TEST_DUPLICATE_INFO) - 1),
                                         UTL_MPEG4SYSTEMS_TAG_DECODER_INFO));
    LOGASSERT(original.payload.data == descriptor.payload.data);
    LOGASSERT(Utl_Mpeg4Systems_ReadDescriptor(&descriptor, Bytes((const byte_t*)TEST_ES, sizeof(TEST_ES) - 1)));
    LOGASSERT(Utl_Mpeg4Systems_ReadElementaryStream(&stream, &descriptor));
    LOGASSERT(
        Utl_Mpeg4Systems_ReadDescriptor(&descriptor, Bytes((const byte_t*)TEST_OVERRUN, sizeof(TEST_OVERRUN) - 1)));
    LOGASSERT(!Utl_Mpeg4Systems_ReadElementaryStream(&stream, &descriptor));
    LOGASSERT(sizeof(TEST_CHILDREN) - 1 == stream.children.byteCount);
    LOGASSERT(
        Utl_Mpeg4Systems_ReadDescriptor(&descriptor, Bytes((const byte_t*)TEST_BAD_URL, sizeof(TEST_BAD_URL) - 1)));
    LOGASSERT(!Utl_Mpeg4Systems_ReadElementaryStream(&stream, &descriptor));
    LOGASSERT(!stream.hasUrl);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The independently expected prefix for 128 bytes is tag, 0x81, 0x00.
 */
static FORCE_INLINE void CheckLengths(TestContext_t* context)
{
    Utl_Mpeg4Systems_Descriptor_t descriptor;
    uint32_t                      count;
    Utl_Mpeg4Systems_Descriptor_t decoded;

    descriptor.tag               = UTL_MPEG4SYSTEMS_TAG_DECODER_INFO;
    descriptor.payload.data      = context->payload;
    descriptor.payload.byteCount = TEST_LENGTH_BOUNDARY;
    count                        = Utl_Mpeg4Systems_WriteDescriptor(&descriptor, context->output, sizeof(context->output));
    LOGASSERT(TEST_LENGTH_BOUNDARY + TEST_LENGTH_PREFIX_BYTES == count);
    LOGASSERT(0 == CRT_Memory_Compare(context->output, "\x05\x81\x00", TEST_LENGTH_PREFIX_BYTES));
    LOGASSERT(Utl_Mpeg4Systems_ReadDescriptor(&decoded, Bytes(context->output, count)));
    LOGASSERT(TEST_LENGTH_BOUNDARY == decoded.payload.byteCount);
    LOGASSERT(0 == CRT_Memory_Compare(decoded.payload.data, context->payload, TEST_LENGTH_BOUNDARY));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each envelope is complete; rejection must occur at the nested field boundary.
 */
static FORCE_INLINE void CheckMalformedParts(void)
{
    Utl_Mpeg4Systems_Descriptor_t       descriptor;
    Utl_Mpeg4Systems_ElementaryStream_t stream;
    Utl_Mpeg4_Span_t                    original;

    LOGASSERT(Utl_Mpeg4Systems_ReadDescriptor(&descriptor, Bytes((const byte_t*)TEST_ES, sizeof(TEST_ES) - 1)));
    LOGASSERT(Utl_Mpeg4Systems_ReadElementaryStream(&stream, &descriptor));
    original = stream.children;
    LOGASSERT(Utl_Mpeg4Systems_ReadDescriptor(
        &descriptor,
        Bytes((const byte_t*)TEST_SHORT_DEPENDENCY, sizeof(TEST_SHORT_DEPENDENCY) - 1)));
    LOGASSERT(!Utl_Mpeg4Systems_ReadElementaryStream(&stream, &descriptor));
    LOGASSERT(original.data == stream.children.data);
    LOGASSERT(original.byteCount == stream.children.byteCount);
    LOGASSERT(
        Utl_Mpeg4Systems_ReadDescriptor(&descriptor,
                                        Bytes((const byte_t*)TEST_CHILD_OVERRUN, sizeof(TEST_CHILD_OVERRUN) - 1)));
    LOGASSERT(!Utl_Mpeg4Systems_ReadElementaryStream(&stream, &descriptor));
    LOGASSERT(original.data == stream.children.data);
    LOGASSERT(original.byteCount == stream.children.byteCount);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckRejectedDecoder(TestContext_t* context, const Utl_Mpeg4Systems_DecoderConfig_t* decoder)
{
    CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
    LOGASSERT(0 == Utl_Mpeg4Systems_GetDecoderConfigByteCount(decoder));
    LOGASSERT(0 == Utl_Mpeg4Systems_WriteDecoderConfig(decoder, context->output, sizeof(context->output)));
    LOGASSERT(0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each mutation changes a different semantic field or component multiplicity.
 */
static FORCE_INLINE void CheckInvalidDecoder(TestContext_t* context)
{
    Utl_Mpeg4Systems_Descriptor_t    descriptor;
    Utl_Mpeg4Systems_DecoderConfig_t decoder;

    LOGASSERT(
        Utl_Mpeg4Systems_ReadDescriptor(&descriptor, Bytes((const byte_t*)TEST_DECODER, sizeof(TEST_DECODER) - 1)));
    LOGASSERT(Utl_Mpeg4Systems_ReadDecoderConfig(&decoder, &descriptor));
    decoder.objectType = 0;
    CheckRejectedDecoder(context, &decoder);
    decoder.objectType = UTL_MPEG4SYSTEMS_OBJECT_AUDIO;
    decoder.streamType = 0;
    CheckRejectedDecoder(context, &decoder);
    decoder.streamType  = UTL_MPEG4SYSTEMS_STREAM_AUDIO;
    decoder.bufferBytes = TEST_BUFFER_OVERFLOW;
    CheckRejectedDecoder(context, &decoder);
    decoder.bufferBytes = 0;
    decoder.children    = Bytes((const byte_t*)TEST_DUPLICATE_INFO, sizeof(TEST_DUPLICATE_INFO) - 1);
    CheckRejectedDecoder(context, &decoder);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The native CRT-free adapter owns entry and exit. Fixtures require no media,
 *  network, heap allocation or process-global state.
 */
uint32_t OS_Application_Main(void)
{
    TestContext_t context;

    CRT_Memory_Set(context.original, TEST_SENTINEL, sizeof(context.original));
    CRT_Memory_Set(context.payload, TEST_SENTINEL, sizeof(context.payload));
    CheckDescriptor(&context);
    CheckStream(&context, Bytes((const byte_t*)TEST_ES, sizeof(TEST_ES) - 1));
    CheckStream(&context, Bytes((const byte_t*)TEST_OPTIONAL_ES, sizeof(TEST_OPTIONAL_ES) - 1));
    CheckRejected();
    CheckLengths(&context);
    CheckMalformedParts();
    CheckInvalidDecoder(&context);
    return 0;
}
