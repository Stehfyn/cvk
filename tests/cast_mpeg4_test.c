/*=================================================================================================
** 1. References
**===============================================================================================*/

/** ISO/IEC 14496-12:2012, 4.2 and 4.3 independent field encodings and boundary cases. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_mpeg4.h"
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

#define TEST_BITS             "\xBF\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xE0"
#define TEST_LEADING_BITS     (3)
#define TEST_LEADING_VALUE    (5)
#define TEST_TRAILING_BITS    (5)
#define TEST_INTEGER_BITS     (64)
#define TEST_INTEGER_BYTES    (8)
#define TEST_PARENT_END       (64)
#define TEST_FILE_END         (65)
#define TEST_LARGE_SIZE_SHIFT (32)
#define TEST_ORDINARY_SIZE    (16)
#define TEST_UUID_SIZE        (24)
#define TEST_CAPACITY         (64)
#define TEST_SENTINEL         (0xA5)
#define TEST_ORDINARY         "\x00\x00\x00\x10\x66\x72\x65\x65"
#define TEST_TO_END           "\x00\x00\x00\x00\x6D\x64\x61\x74"
#define TEST_UUID_BODY        "\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A\x0B\x0C\x0D\x0E\x0F"
#define TEST_UUID             "\x00\x00\x00\x18\x75\x75\x69\x64" TEST_UUID_BODY
#define TEST_LARGE_UUID       "\x00\x00\x00\x01\x75\x75\x69\x64\x00\x00\x00\x01\x00\x00\x00\x20" TEST_UUID_BODY
#define TEST_FULLBOX          "\x01\xAB\xCD\xEF"
#define TEST_FILETYPE         "\x69\x73\x6F\x6D\x00\x00\x02\x00\x69\x73\x6F\x32\x61\x76\x63\x31"
#define TEST_UNKNOWN_BRAND    "\x61\x62\x63\x64\x00\x00\x00\x00"
#define TEST_BAD_BRAND        "\x01\x62\x63\x64\x00\x00\x00\x00"
#define TEST_FLAGS            (0xABCDEF)
#define TEST_FLAGS_OVERFLOW   (0x1000000)

#define TEST_STTS_HEADER          "\x00\x00\x00\x00\x00\x00\x00\x02"
#define TEST_STTS_MAX_HEADER      "\x00\x00\x00\x00\xFF\xFF\xFF\xFF"
#define TEST_STTS_VERSION         "\x01\x00\x00\x00\x00\x00\x00\x02"
#define TEST_STTS_FLAGS           "\x00\x00\x00\x01\x00\x00\x00\x00"
#define TEST_STTS_FIRST           "\x00\x00\x00\x03\x00\x00\x00\x0A"
#define TEST_STTS_SECOND          "\x00\x00\x00\x02\x00\x00\x00\x14"
#define TEST_STTS_EMPTY_RUN       "\x00\x00\x00\x00\xFF\xFF\xFF\xFF"
#define TEST_STTS_ZERO_DELTA      "\x00\x00\x00\x03\x00\x00\x00\x00"
#define TEST_STTS_MAX_RUN         "\xFF\xFF\xFF\xFF\xFF\xFF\xFF\xFF"
#define TEST_STSS_ZERO            "\x00\x00\x00\x00"
#define TEST_STSS_FIRST           "\x00\x00\x00\x01"
#define TEST_STSS_MIDDLE          "\x00\x00\x00\x03"
#define TEST_STSS_LAST            "\x00\x00\x00\x05"
#define TEST_TIMING_ENTRIES       (2)
#define TEST_SYNC_ENTRIES         (3)
#define TEST_TIMING_PAYLOAD_BYTES (24)
#define TEST_SAMPLE_COUNT         (5)
#define TEST_TOTAL_DURATION       (70)
#define TEST_FIRST_RUN_COUNT      (3)
#define TEST_FIRST_RUN_DELTA      (10)
#define TEST_SECOND_RUN_DELTA     (20)
#define TEST_SECOND_RUN_TIME      (30)
#define TEST_LAST_SAMPLE_TIME     (50)

#define TEST_STSS_HEADER        "\x00\x00\x00\x00\x00\x00\x00\x03"
#define TEST_EMPTY_TABLE        "\x00\x00\x00\x00\x00\x00\x00\x00"
#define TEST_SYNC_PAYLOAD_BYTES (20)
#define TEST_MIDDLE_SAMPLE_TIME (20)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Entry-owned storage reused by sequential cases. */
typedef struct TestContext_s
{
    byte_t output[TEST_CAPACITY];   ///< Writable target.
    byte_t original[TEST_CAPACITY]; ///< Complete rejection sentinel.
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
 * \brief Constructs a counted fixture borrow.
 *
 * \par Requirements:
 *  - 001 The source shall remain readable for the complete fixture operation.
 *
 * \param[in] data    Stable literal bytes
 * \param[in] count   Exact byte count excluding any terminator
 *
 * \retval bytes Counted borrow.
 */
static FORCE_INLINE Utl_Mpeg4_Span_t Bytes(const byte_t* data, uint32_t count);

/*************************************************************************************************/
/**
 * \brief Checks an independent Box encoding, every truncated prefix and every short writer capacity.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The retained box extent shall include the complete literal header.
 *  - 002 Failed writes shall preserve the complete destination, including bytes outside supplied capacity.
 *
 * \param[in,out] context   Required fixture storage
 * \param[in]     bytes     Independent header encoding
 * \param[in]     extent    Resolved box size and enclosing file extent
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckBox(TestContext_t* context, Utl_Mpeg4_Span_t bytes, uint64_t extent);

/*************************************************************************************************/
/**
 * \brief Checks file-end semantics, empty external input and invalid writer models.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 A size-zero child shall not escape its parent to reach file end.
 *  - 002 Conflicting flags, ordinary-size overflow and undersized boxes shall leave output unchanged.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckBoxBoundaries(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks FullBox field widths, unknown-version representation and output preservation.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Generic reading shall retain version one without assuming version-zero payload semantics.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckFullBox(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks a single shorter payload against independent field-width rules.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Only complete major/minor fields followed by whole brands shall be accepted.
 *  - 002 Every published model member shall remain unchanged on rejection.
 *
 * \param[in] model   Required retained complete model
 * \param[in] bytes   Independent prefix under test
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckFileTypePrefix(const Utl_Mpeg4_FileType_t* model, Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Checks every shorter FileTypeBox writer capacity and reader prefix.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Rejection shall preserve every byte in the destination, including the unused tail.
 *
 * \param[in,out] context   Required fixture storage
 * \param[in]     model     Complete file-type model
 * \param[in]     bytes     Independent complete payload encoding
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckFileTypeCapacities(TestContext_t*              context,
                                                 const Utl_Mpeg4_FileType_t* model,
                                                 Utl_Mpeg4_Span_t            bytes);

/*************************************************************************************************/
/**
 * \brief Checks complete file-type syntax, unknown brands and all capacity boundaries.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Complete prefixes with zero or one compatible brand shall remain valid independent payloads.
 *  - 002 Malformed brand extent and invalid code bytes shall fail without partial publication.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckFileType(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks independent unaligned 64-bit MSB-first fields and byte rounding.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 A full-width field shall not cause a width-sized shift.
 *  - 002 Writing into nonzero storage shall initialize each touched byte, including zero trailing bits.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckBitstream(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks field preflight and idempotent reader/writer failure.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 A rejected field shall preserve the cursor and every destination byte.
 *  - 002 Further operations after failure shall remain no-ops until initialization.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckBitstreamFailure(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks complete-width network integers, invalid widths and short destinations.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The full uint64_t range shall remain representable without narrowing.
 *  - 002 Invalid reads shall preserve the previous published value.
 *
 * \param[in,out] context   Required fixture storage
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckUnsigned(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks independent table-prefix encoding and wide declared extents.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Every short input/output prefix shall preserve the published model or destination.
 *  - 002 The declared table extent shall widen before entry multiplication.
 *
 * \param[in,out] context   Required reusable fixture owner
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckTableHeader(TestContext_t* context);

/*************************************************************************************************/
/**
 * \brief Checks unsupported headers and a present empty sync table.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Unsupported syntax shall preserve the prior different-kind model.
 *  - 002 A present zero-entry stss shall accept no sync sample.
 *
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckTableHeaderRejection(void);

/*************************************************************************************************/
/**
 * \brief Checks two independent time runs and one-based sample queries.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Decoded times shall match the manually derived sequence 0, 10, 20, 30, 50.
 *  - 002 Missing samples and an exhausted table shall preserve published results.
 *
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckTimingRuns(void);

/*************************************************************************************************/
/**
 * \brief Checks every incomplete timing-entry prefix.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Every shorter entry shall leave all output and cursor members unchanged.
 *
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckTimingFailure(void);

/*************************************************************************************************/
/**
 * \brief Checks permitted empty runs and zero decoding deltas.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Zero-valued syntax shall not create a non-progressing cursor or an invented format rejection.
 *
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckZeroTiming(void);

/*************************************************************************************************/
/**
 * \brief Checks ordering and completion of one-based sync samples.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Zero, duplicate and descending numbers shall preserve output and cursor.
 *  - 002 The independent sequence one, three, five shall consume exactly three entries.
 *
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckSyncSamples(void);

/*************************************************************************************************/
/**
 * \brief Checks second-run samples and rejection at either boundary.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Invalid queries shall preserve the last published timestamp.
 *
 * \param[in] run   Decoded second run, beginning at sample four
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckSampleQueries(const Utl_Mpeg4_TimeToSampleRun_t* run);

/*************************************************************************************************/
/**
 * \brief Checks cumulative uint64_t duration exhaustion with two maximum-width runs.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 The second run shall reject overflow while preserving the first run and cursor.
 *
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckTimingOverflow(void);

/*************************************************************************************************/
/**
 * \brief Checks every incomplete sync entry and the prohibited sample number zero.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Invalid input shall preserve every cursor member and the result.
 *
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckSyncTruncation(void);

/*************************************************************************************************/
/**
 * \brief Checks the four-byte sync-entry width and a present empty table prefix.
 *
 * \par Details:
 *  Assertion on mismatch.
 *
 * \par Requirements:
 *  - 001 Declared payload truncation shall preserve the previous model.
 *  - 002 The independent sync-header encoding shall round-trip unchanged.
 *
 * \param[in,out] context   Required reusable fixture owner
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckSyncTableHeader(TestContext_t* context);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Mpeg4_Span_t Bytes(const byte_t* data, uint32_t count)
{
    Utl_Mpeg4_Span_t bytes;

    bytes.data      = data;
    bytes.byteCount = count;
    return bytes;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckBox(TestContext_t* context, Utl_Mpeg4_Span_t bytes, uint64_t extent)
{
    bool_t                valid;
    Utl_Mpeg4_BoxHeader_t header;
    uint32_t              count;
    bool_t                equal;
    Utl_Mpeg4_BoxHeader_t saved;
    uint32_t              capacity;
    Utl_Mpeg4_Span_t      short_bytes;

    LOGASSERT(NULL != context);
    valid = Utl_Mpeg4_ReadBoxHeader(&header, bytes, extent, extent);
    LOGASSERT(valid);
    LOGASSERT(extent == header.byteCount);
    count = Utl_Mpeg4_GetBoxHeaderByteCount(&header);
    LOGASSERT(bytes.byteCount == count);
    count = Utl_Mpeg4_WriteBoxHeader(&header, context->output, sizeof(context->output));
    LOGASSERT(bytes.byteCount == count);
    equal = (0 == CRT_Memory_Compare(bytes.data, context->output, count));
    LOGASSERT(equal);
    CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
    saved = header;
    for (capacity = 0; capacity < count; ++capacity)
    {
        short_bytes           = bytes;
        short_bytes.byteCount = capacity;
        valid                 = Utl_Mpeg4_ReadBoxHeader(&header, short_bytes, extent, extent);
        LOGASSERT(!valid);
        LOGASSERT(saved.byteCount == header.byteCount);
        LOGASSERT(saved.type == header.type);
        LOGASSERT(saved.extendsToEnd == header.extendsToEnd);
        LOGASSERT(saved.hasLargeSize == header.hasLargeSize);
        equal = (0 == CRT_Memory_Compare(saved.userType, header.userType, sizeof(saved.userType)));
        LOGASSERT(equal);
        LOGASSERT(0 == Utl_Mpeg4_WriteBoxHeader(&header, context->output, capacity));
        equal = (0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
        LOGASSERT(equal);
    }
    valid = Utl_Mpeg4_ReadBoxHeader(&header, bytes, extent - 1, extent);
    LOGASSERT(!valid);
    LOGASSERT(saved.byteCount == header.byteCount);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckBoxBoundaries(TestContext_t* context)
{
    Utl_Mpeg4_BoxHeader_t header;
    Utl_Mpeg4_Span_t      bytes;
    bool_t                valid;
    uint32_t              count;
    bool_t                equal;

    LOGASSERT(NULL != context);
    Utl_Mpeg4_InitializeBoxHeader(&header, UTL_MPEG4_BOX_MDAT);
    LOGASSERT(UTL_MPEG4_BOX_HEADER_BYTES == header.byteCount);
    bytes = Bytes((const byte_t*)TEST_TO_END, sizeof(TEST_TO_END) - 1);
    valid = Utl_Mpeg4_ReadBoxHeader(&header, bytes, TEST_PARENT_END, TEST_FILE_END);
    LOGASSERT(!valid);
    LOGASSERT(UTL_MPEG4_BOX_HEADER_BYTES == header.byteCount);
    valid = Utl_Mpeg4_ReadBoxHeader(&header, bytes, TEST_FILE_END, TEST_PARENT_END);
    LOGASSERT(!valid);
    valid = Utl_Mpeg4_ReadBoxHeader(&header, bytes, UINT64_MAX, UINT64_MAX);
    LOGASSERT(valid);
    LOGASSERT(header.extendsToEnd);
    LOGASSERT(UINT64_MAX == header.byteCount);
    CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
    header.hasLargeSize = TRUE;
    count               = Utl_Mpeg4_WriteBoxHeader(&header, context->output, sizeof(context->output));
    LOGASSERT(0 == count);
    header.hasLargeSize = FALSE;
    header.extendsToEnd = FALSE;
    count               = Utl_Mpeg4_WriteBoxHeader(&header, context->output, sizeof(context->output));
    LOGASSERT(0 == count);
    header.byteCount = UTL_MPEG4_BOX_HEADER_BYTES - 1;
    count            = Utl_Mpeg4_WriteBoxHeader(&header, context->output, sizeof(context->output));
    LOGASSERT(0 == count);
    equal = (0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
    LOGASSERT(equal);
    Utl_Mpeg4_InitializeBoxHeader(&header, UTL_MPEG4_BOX_UUID);
    count = Utl_Mpeg4_GetBoxHeaderByteCount(&header);
    LOGASSERT(UTL_MPEG4_BOX_HEADER_BYTES + UTL_MPEG4_USER_TYPE_BYTES == count);
    bytes = Bytes(NULL, 0);
    valid = Utl_Mpeg4_ReadBoxHeader(&header, bytes, 0, 0);
    LOGASSERT(!valid);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckFullBox(TestContext_t* context)
{
    Utl_Mpeg4_Span_t          bytes;
    bool_t                    valid;
    Utl_Mpeg4_FullBoxFields_t fields;
    uint32_t                  count;
    bool_t                    equal;
    uint32_t                  capacity;

    LOGASSERT(NULL != context);
    bytes = Bytes((const byte_t*)TEST_FULLBOX, sizeof(TEST_FULLBOX) - 1);
    valid = Utl_Mpeg4_ReadFullBoxFields(&fields, bytes);
    LOGASSERT(valid);
    LOGASSERT(1 == fields.version);
    LOGASSERT(TEST_FLAGS == fields.flags);
    count = Utl_Mpeg4_WriteFullBoxFields(&fields, context->output, sizeof(context->output));
    LOGASSERT(bytes.byteCount == count);
    equal = (0 == CRT_Memory_Compare(bytes.data, context->output, count));
    LOGASSERT(equal);
    CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
    for (capacity = 0; capacity < UTL_MPEG4_FULLBOX_FIELDS_BYTES; ++capacity)
    {
        bytes.byteCount = capacity;
        valid           = Utl_Mpeg4_ReadFullBoxFields(&fields, bytes);
        LOGASSERT(!valid);
        LOGASSERT(1 == fields.version);
        LOGASSERT(TEST_FLAGS == fields.flags);
        count = Utl_Mpeg4_WriteFullBoxFields(&fields, context->output, capacity);
        LOGASSERT(0 == count);
        equal = (0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
        LOGASSERT(equal);
    }
    fields.flags = TEST_FLAGS_OVERFLOW;
    count        = Utl_Mpeg4_WriteFullBoxFields(&fields, context->output, sizeof(context->output));
    LOGASSERT(0 == count);
    equal = (0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
    LOGASSERT(equal);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckFileTypePrefix(const Utl_Mpeg4_FileType_t* model, Utl_Mpeg4_Span_t bytes)
{
    bool_t               complete_prefix;
    bool_t               whole_brands;
    bool_t               expected;
    Utl_Mpeg4_FileType_t candidate;
    bool_t               valid;

    LOGASSERT(NULL != model);
    complete_prefix = (UTL_MPEG4_FILE_TYPE_FIELDS_BYTES <= bytes.byteCount);
    whole_brands    = (0 == (bytes.byteCount % UTL_MPEG4_BRAND_BYTES));
    expected        = complete_prefix && whole_brands;
    candidate       = *model;
    valid           = Utl_Mpeg4_ReadFileType(&candidate, bytes);
    LOGASSERT(expected == valid);
    if (!valid)
    {
        LOGASSERT(model->majorBrand == candidate.majorBrand);
        LOGASSERT(model->minorVersion == candidate.minorVersion);
        LOGASSERT(model->compatibleBrands.data == candidate.compatibleBrands.data);
        LOGASSERT(model->compatibleBrands.byteCount == candidate.compatibleBrands.byteCount);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckFileTypeCapacities(TestContext_t*              context,
                                                 const Utl_Mpeg4_FileType_t* model,
                                                 Utl_Mpeg4_Span_t            bytes)
{
    uint32_t         capacity;
    uint32_t         count;
    bool_t           equal;
    Utl_Mpeg4_Span_t prefix;

    LOGASSERT(NULL != context);
    CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
    for (capacity = 0; capacity < bytes.byteCount; ++capacity)
    {
        count = Utl_Mpeg4_WriteFileType(model, context->output, capacity);
        LOGASSERT(0 == count);
        equal = (0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
        LOGASSERT(equal);
        prefix           = bytes;
        prefix.byteCount = capacity;
        CheckFileTypePrefix(model, prefix);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckFileType(TestContext_t* context)
{
    Utl_Mpeg4_Span_t     bytes;
    bool_t               valid;
    Utl_Mpeg4_FileType_t file_type;
    uint32_t             brand;
    uint32_t             count;
    bool_t               equal;

    LOGASSERT(NULL != context);
    bytes = Bytes((const byte_t*)TEST_FILETYPE, sizeof(TEST_FILETYPE) - 1);
    valid = Utl_Mpeg4_ReadFileType(&file_type, bytes);
    LOGASSERT(valid);
    LOGASSERT(UTL_MPEG4_BRAND_ISOM == file_type.majorBrand);
    LOGASSERT(0x200 == file_type.minorVersion);
    valid = Utl_Mpeg4_GetCompatibleBrand(&brand, &file_type, 0);
    LOGASSERT(valid);
    LOGASSERT(UTL_MPEG4_BRAND_ISO2 == brand);
    valid = Utl_Mpeg4_GetCompatibleBrand(&brand, &file_type, 1);
    LOGASSERT(valid);
    LOGASSERT(UTL_MPEG4_BRAND_AVC1 == brand);
    valid = Utl_Mpeg4_GetCompatibleBrand(&brand, &file_type, UINT32_MAX);
    LOGASSERT(!valid);
    LOGASSERT(UTL_MPEG4_BRAND_AVC1 == brand);
    count = Utl_Mpeg4_WriteFileType(&file_type, context->output, sizeof(context->output));
    LOGASSERT(bytes.byteCount == count);
    equal = (0 == CRT_Memory_Compare(bytes.data, context->output, count));
    LOGASSERT(equal);
    CheckFileTypeCapacities(context, &file_type, bytes);
    bytes = Bytes((const byte_t*)TEST_BAD_BRAND, sizeof(TEST_BAD_BRAND) - 1);
    valid = Utl_Mpeg4_ReadFileType(&file_type, bytes);
    LOGASSERT(!valid);
    LOGASSERT(UTL_MPEG4_BRAND_ISOM == file_type.majorBrand);
    bytes = Bytes((const byte_t*)TEST_UNKNOWN_BRAND, sizeof(TEST_UNKNOWN_BRAND) - 1);
    valid = Utl_Mpeg4_ReadFileType(&file_type, bytes);
    LOGASSERT(valid);
    LOGASSERT(0 == file_type.compatibleBrands.byteCount);
    file_type.majorBrand = 0;
    LOGASSERT(0 == Utl_Mpeg4_WriteFileType(&file_type, context->output, sizeof(context->output)));
    equal = (0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
    LOGASSERT(equal);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckBitstream(TestContext_t* context)
{
    Utl_Mpeg4_Span_t      bytes;
    Utl_Mpeg4_BitReader_t reader;
    uint64_t              value;
    Utl_Mpeg4_BitWriter_t writer;
    Utl_Mpeg4_BitField_t  field;
    uint32_t              count;
    bool_t                equal;

    LOGASSERT(NULL != context);
    bytes = Bytes((const byte_t*)TEST_BITS, sizeof(TEST_BITS) - 1);
    Utl_Mpeg4_InitializeBitReader(&reader, bytes);
    value = Utl_Mpeg4_ReadBits(&reader, TEST_LEADING_BITS);
    LOGASSERT(TEST_LEADING_VALUE == value);
    value = Utl_Mpeg4_ReadBits(&reader, TEST_INTEGER_BITS);
    LOGASSERT(UINT64_MAX == value);
    value = Utl_Mpeg4_ReadBits(&reader, TEST_TRAILING_BITS);
    LOGASSERT(0 == value);
    LOGASSERT(reader.valid);
    CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
    Utl_Mpeg4_InitializeBitWriter(&writer, context->output, sizeof(context->output));
    field.bitCount = TEST_LEADING_BITS;
    field.value    = TEST_LEADING_VALUE;
    Utl_Mpeg4_WriteBits(&writer, field);
    field.bitCount = TEST_INTEGER_BITS;
    field.value    = UINT64_MAX;
    Utl_Mpeg4_WriteBits(&writer, field);
    field.bitCount = TEST_TRAILING_BITS;
    field.value    = 0;
    Utl_Mpeg4_WriteBits(&writer, field);
    LOGASSERT(writer.valid);
    count = Utl_Mpeg4_GetBitWriterByteCount(&writer);
    LOGASSERT(bytes.byteCount == count);
    equal = (0 == CRT_Memory_Compare(bytes.data, context->output, count));
    LOGASSERT(equal);
    Utl_Mpeg4_InitializeBitCounter(&writer);
    field.bitCount = TEST_INTEGER_BITS;
    field.value    = UINT64_MAX;
    Utl_Mpeg4_WriteBits(&writer, field);
    field.bitCount = TEST_LEADING_BITS;
    field.value    = TEST_LEADING_VALUE;
    Utl_Mpeg4_WriteBits(&writer, field);
    count = Utl_Mpeg4_GetBitWriterByteCount(&writer);
    LOGASSERT(bytes.byteCount == count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckBitstreamFailure(TestContext_t* context)
{
    Utl_Mpeg4_Span_t      bytes;
    Utl_Mpeg4_BitReader_t reader;
    uint64_t              value;
    Utl_Mpeg4_BitWriter_t writer;
    Utl_Mpeg4_BitField_t  field;
    byte_t                saved[TEST_CAPACITY];
    bool_t                equal;

    LOGASSERT(NULL != context);
    bytes = Bytes((const byte_t*)TEST_BITS, TEST_INTEGER_BYTES);
    Utl_Mpeg4_InitializeBitReader(&reader, bytes);
    value = Utl_Mpeg4_ReadBits(&reader, TEST_LEADING_BITS);
    LOGASSERT(TEST_LEADING_VALUE == value);
    value = Utl_Mpeg4_ReadBits(&reader, TEST_INTEGER_BITS);
    LOGASSERT(0 == value);
    LOGASSERT(!reader.valid);
    LOGASSERT(TEST_LEADING_BITS == reader.position);
    value = Utl_Mpeg4_ReadBits(&reader, 1);
    LOGASSERT(0 == value);
    LOGASSERT(TEST_LEADING_BITS == reader.position);
    Utl_Mpeg4_InitializeBitWriter(&writer, context->output, TEST_INTEGER_BYTES);
    field.bitCount = TEST_LEADING_BITS;
    field.value    = TEST_LEADING_VALUE;
    Utl_Mpeg4_WriteBits(&writer, field);
    CRT_Memory_Copy(saved, context->output, sizeof(saved));
    field.bitCount = TEST_INTEGER_BITS;
    field.value    = UINT64_MAX;
    Utl_Mpeg4_WriteBits(&writer, field);
    LOGASSERT(!writer.valid);
    LOGASSERT(TEST_LEADING_BITS == writer.position);
    field.bitCount = 1;
    field.value    = 0;
    Utl_Mpeg4_WriteBits(&writer, field);
    Utl_Mpeg4_WriteAlignedBytes(&writer, bytes);
    LOGASSERT(TEST_LEADING_BITS == writer.position);
    equal = (0 == CRT_Memory_Compare(saved, context->output, sizeof(saved)));
    LOGASSERT(equal);
    LOGASSERT(0 == Utl_Mpeg4_GetBitWriterByteCount(&writer));
    Utl_Mpeg4_InitializeBitWriter(&writer, context->output, sizeof(context->output));
    field.bitCount = 1;
    field.value    = 2;
    Utl_Mpeg4_WriteBits(&writer, field);
    LOGASSERT(!writer.valid);
    equal = (0 == CRT_Memory_Compare(saved, context->output, sizeof(saved)));
    LOGASSERT(equal);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckUnsigned(TestContext_t* context)
{
    Utl_Mpeg4_Integer_t field;
    uint32_t            count;
    Utl_Mpeg4_Span_t    bytes;
    bool_t              valid;
    uint64_t            value;
    uint32_t            capacity;
    bool_t              equal;

    LOGASSERT(NULL != context);
    field.value     = UINT64_MAX;
    field.byteCount = TEST_INTEGER_BYTES;
    count           = Utl_Mpeg4_WriteUnsigned(field, context->output, sizeof(context->output));
    LOGASSERT(TEST_INTEGER_BYTES == count);
    bytes = Bytes(context->output, count);
    valid = Utl_Mpeg4_ReadUnsigned(&value, bytes);
    LOGASSERT(valid);
    LOGASSERT(UINT64_MAX == value);
    CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
    for (capacity = 0; capacity < TEST_INTEGER_BYTES; ++capacity)
    {
        count = Utl_Mpeg4_WriteUnsigned(field, context->output, capacity);
        LOGASSERT(0 == count);
        equal = (0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
        LOGASSERT(equal);
    }
    field.byteCount = TEST_INTEGER_BYTES - 1;
    LOGASSERT(0 == Utl_Mpeg4_GetIntegerByteCount(field));
    field.byteCount = TEST_INTEGER_BYTES + 1;
    LOGASSERT(0 == Utl_Mpeg4_GetIntegerByteCount(field));
    bytes.byteCount = 0;
    valid           = Utl_Mpeg4_ReadUnsigned(&value, bytes);
    LOGASSERT(!valid);
    LOGASSERT(UINT64_MAX == value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckTableHeader(TestContext_t* context)
{
    Utl_Mpeg4_SampleTable_t table;
    Utl_Mpeg4_BoxPayload_t  payload;
    Utl_Mpeg4_Span_t        bytes;
    bool_t                  valid;
    bool_t                  equal;
    uint32_t                capacity;
    uint64_t                wide_count;

    LOGASSERT(NULL != context);
    Utl_Mpeg4_InitializeSampleTable(&table, UTL_MPEG4_BOX_STTS);
    LOGASSERT(0 == table.entryCount);
    LOGASSERT(UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES == Utl_Mpeg4_GetSampleTableByteCount(&table));
    payload.type      = UTL_MPEG4_BOX_STTS;
    payload.byteCount = TEST_TIMING_PAYLOAD_BYTES;
    bytes             = Bytes((const byte_t*)TEST_STTS_HEADER, sizeof(TEST_STTS_HEADER) - 1);
    valid             = Utl_Mpeg4_ReadSampleTableHeader(&table, bytes, payload);
    LOGASSERT(valid);
    LOGASSERT(TEST_TIMING_ENTRIES == table.entryCount);
    LOGASSERT(TEST_TIMING_PAYLOAD_BYTES == Utl_Mpeg4_GetSampleTableByteCount(&table));
    LOGASSERT(UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES ==
              Utl_Mpeg4_WriteSampleTableHeader(&table, context->output, sizeof(context->output)));
    equal = (0 == CRT_Memory_Compare(bytes.data, context->output, bytes.byteCount));
    LOGASSERT(equal);
    CRT_Memory_Copy(context->output, context->original, sizeof(context->output));
    for (capacity = 0; capacity < UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES; ++capacity)
    {
        bytes.byteCount = capacity;
        valid           = Utl_Mpeg4_ReadSampleTableHeader(&table, bytes, payload);
        LOGASSERT(!valid);
        LOGASSERT(UTL_MPEG4_BOX_STTS == table.type);
        LOGASSERT(TEST_TIMING_ENTRIES == table.entryCount);
        LOGASSERT(0 == Utl_Mpeg4_WriteSampleTableHeader(&table, context->output, capacity));
        equal = (0 == CRT_Memory_Compare(context->output, context->original, sizeof(context->output)));
        LOGASSERT(equal);
    }
    bytes.byteCount = sizeof(TEST_STTS_HEADER) - 1;
    --payload.byteCount;
    valid = Utl_Mpeg4_ReadSampleTableHeader(&table, bytes, payload);
    LOGASSERT(!valid);
    LOGASSERT(TEST_TIMING_ENTRIES == table.entryCount);
    bytes             = Bytes((const byte_t*)TEST_STTS_MAX_HEADER, sizeof(TEST_STTS_MAX_HEADER) - 1);
    wide_count        = UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES + ((uint64_t)UINT32_MAX * UTL_MPEG4_TIME_TO_SAMPLE_ENTRY_BYTES);
    payload.byteCount = wide_count;
    valid             = Utl_Mpeg4_ReadSampleTableHeader(&table, bytes, payload);
    LOGASSERT(valid);
    LOGASSERT(UINT32_MAX == table.entryCount);
    LOGASSERT(wide_count == Utl_Mpeg4_GetSampleTableByteCount(&table));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckTableHeaderRejection(void)
{
    Utl_Mpeg4_SampleTable_t      table;
    Utl_Mpeg4_BoxPayload_t       payload;
    Utl_Mpeg4_Span_t             bytes;
    bool_t                       valid;
    Utl_Mpeg4_SyncSampleCursor_t cursor;
    uint32_t                     sample;

    Utl_Mpeg4_InitializeSampleTable(&table, UTL_MPEG4_BOX_STSS);
    payload.type      = UTL_MPEG4_BOX_STTS;
    payload.byteCount = TEST_TIMING_PAYLOAD_BYTES;
    bytes             = Bytes((const byte_t*)TEST_STTS_VERSION, sizeof(TEST_STTS_VERSION) - 1);
    valid             = Utl_Mpeg4_ReadSampleTableHeader(&table, bytes, payload);
    LOGASSERT(!valid);
    LOGASSERT(UTL_MPEG4_BOX_STSS == table.type);
    LOGASSERT(0 == table.entryCount);
    bytes = Bytes((const byte_t*)TEST_STTS_FLAGS, sizeof(TEST_STTS_FLAGS) - 1);
    valid = Utl_Mpeg4_ReadSampleTableHeader(&table, bytes, payload);
    LOGASSERT(!valid);
    LOGASSERT(UTL_MPEG4_BOX_STSS == table.type);
    LOGASSERT(0 == table.entryCount);
    bytes        = Bytes((const byte_t*)TEST_STTS_HEADER, sizeof(TEST_STTS_HEADER) - 1);
    payload.type = UTL_MPEG4_BOX_MDAT;
    valid        = Utl_Mpeg4_ReadSampleTableHeader(&table, bytes, payload);
    LOGASSERT(!valid);
    LOGASSERT(UTL_MPEG4_BOX_STSS == table.type);
    LOGASSERT(0 == table.entryCount);
    Utl_Mpeg4_InitializeSyncSampleCursor(&cursor, &table);
    bytes  = Bytes((const byte_t*)TEST_STSS_FIRST, sizeof(TEST_STSS_FIRST) - 1);
    sample = TEST_SENTINEL;
    valid  = Utl_Mpeg4_ReadSyncSample(&cursor, &sample, bytes);
    LOGASSERT(!valid);
    LOGASSERT(TEST_SENTINEL == sample);
    LOGASSERT(0 == cursor.remaining);
    LOGASSERT(0 == cursor.lastSample);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckTimingRuns(void)
{
    Utl_Mpeg4_SampleTable_t        table;
    Utl_Mpeg4_TimeToSampleCursor_t cursor;
    Utl_Mpeg4_Span_t               bytes;
    bool_t                         valid;
    Utl_Mpeg4_TimeToSampleRun_t    run;
    uint64_t                       time;

    Utl_Mpeg4_InitializeSampleTable(&table, UTL_MPEG4_BOX_STTS);
    table.entryCount = TEST_TIMING_ENTRIES;
    Utl_Mpeg4_InitializeTimeToSampleCursor(&cursor, &table);
    bytes = Bytes((const byte_t*)TEST_STTS_FIRST, sizeof(TEST_STTS_FIRST) - 1);
    valid = Utl_Mpeg4_ReadTimeToSampleRun(&cursor, &run, bytes);
    LOGASSERT(valid);
    LOGASSERT(0 == run.sampleOffset);
    LOGASSERT(0 == run.decodeTime);
    LOGASSERT(TEST_FIRST_RUN_COUNT == run.sampleCount);
    LOGASSERT(TEST_FIRST_RUN_DELTA == run.sampleDelta);
    time  = UINT64_MAX;
    valid = Utl_Mpeg4_GetSampleDecodeTime(&time, &run, 1);
    LOGASSERT(valid);
    LOGASSERT(0 == time);
    valid = Utl_Mpeg4_GetSampleDecodeTime(&time, &run, TEST_FIRST_RUN_COUNT);
    LOGASSERT(valid);
    LOGASSERT(TEST_MIDDLE_SAMPLE_TIME == time);
    bytes = Bytes((const byte_t*)TEST_STTS_SECOND, sizeof(TEST_STTS_SECOND) - 1);
    valid = Utl_Mpeg4_ReadTimeToSampleRun(&cursor, &run, bytes);
    LOGASSERT(valid);
    LOGASSERT(TEST_FIRST_RUN_COUNT == run.sampleOffset);
    LOGASSERT(TEST_SECOND_RUN_TIME == run.decodeTime);
    LOGASSERT(TEST_SECOND_RUN_DELTA == run.sampleDelta);
    LOGASSERT(TEST_SAMPLE_COUNT == cursor.sampleCount);
    LOGASSERT(TEST_TOTAL_DURATION == cursor.duration);
    LOGASSERT(0 == cursor.remaining);
    CheckSampleQueries(&run);
    valid = Utl_Mpeg4_ReadTimeToSampleRun(&cursor, &run, bytes);
    LOGASSERT(!valid);
    LOGASSERT(TEST_SAMPLE_COUNT == cursor.sampleCount);
    LOGASSERT(TEST_TOTAL_DURATION == cursor.duration);
    LOGASSERT(0 == cursor.remaining);
    LOGASSERT(TEST_SECOND_RUN_TIME == run.decodeTime);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckTimingFailure(void)
{
    Utl_Mpeg4_SampleTable_t        table;
    Utl_Mpeg4_TimeToSampleCursor_t cursor;
    Utl_Mpeg4_TimeToSampleRun_t    run;
    Utl_Mpeg4_Span_t               bytes;
    uint32_t                       count;
    bool_t                         valid;

    Utl_Mpeg4_InitializeSampleTable(&table, UTL_MPEG4_BOX_STTS);
    table.entryCount = TEST_TIMING_ENTRIES;
    Utl_Mpeg4_InitializeTimeToSampleCursor(&cursor, &table);
    run.sampleOffset = TEST_SENTINEL;
    run.decodeTime   = TEST_SENTINEL;
    run.sampleCount  = TEST_SENTINEL;
    run.sampleDelta  = TEST_SENTINEL;
    bytes            = Bytes((const byte_t*)TEST_STTS_MAX_RUN, sizeof(TEST_STTS_MAX_RUN) - 1);
    for (count = 0; count < UTL_MPEG4_TIME_TO_SAMPLE_ENTRY_BYTES; ++count)
    {
        bytes.byteCount = count;
        valid           = Utl_Mpeg4_ReadTimeToSampleRun(&cursor, &run, bytes);
        LOGASSERT(!valid);
        LOGASSERT(TEST_TIMING_ENTRIES == cursor.remaining);
        LOGASSERT(0 == cursor.sampleCount);
        LOGASSERT(0 == cursor.duration);
        LOGASSERT(TEST_SENTINEL == run.sampleOffset);
        LOGASSERT(TEST_SENTINEL == run.decodeTime);
        LOGASSERT(TEST_SENTINEL == run.sampleCount);
        LOGASSERT(TEST_SENTINEL == run.sampleDelta);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckZeroTiming(void)
{
    Utl_Mpeg4_SampleTable_t        table;
    Utl_Mpeg4_TimeToSampleCursor_t cursor;
    Utl_Mpeg4_Span_t               bytes;
    bool_t                         valid;
    Utl_Mpeg4_TimeToSampleRun_t    run;
    uint64_t                       time;

    Utl_Mpeg4_InitializeSampleTable(&table, UTL_MPEG4_BOX_STTS);
    table.entryCount = TEST_TIMING_ENTRIES;
    Utl_Mpeg4_InitializeTimeToSampleCursor(&cursor, &table);
    bytes = Bytes((const byte_t*)TEST_STTS_EMPTY_RUN, sizeof(TEST_STTS_EMPTY_RUN) - 1);
    valid = Utl_Mpeg4_ReadTimeToSampleRun(&cursor, &run, bytes);
    LOGASSERT(valid);
    LOGASSERT(1 == cursor.remaining);
    LOGASSERT(0 == cursor.sampleCount);
    LOGASSERT(0 == cursor.duration);
    time  = TEST_SENTINEL;
    valid = Utl_Mpeg4_GetSampleDecodeTime(&time, &run, 1);
    LOGASSERT(!valid);
    LOGASSERT(TEST_SENTINEL == time);
    bytes = Bytes((const byte_t*)TEST_STTS_ZERO_DELTA, sizeof(TEST_STTS_ZERO_DELTA) - 1);
    valid = Utl_Mpeg4_ReadTimeToSampleRun(&cursor, &run, bytes);
    LOGASSERT(valid);
    LOGASSERT(0 == cursor.remaining);
    LOGASSERT(TEST_FIRST_RUN_COUNT == cursor.sampleCount);
    LOGASSERT(0 == cursor.duration);
    valid = Utl_Mpeg4_GetSampleDecodeTime(&time, &run, TEST_FIRST_RUN_COUNT);
    LOGASSERT(valid);
    LOGASSERT(0 == time);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckSyncSamples(void)
{
    Utl_Mpeg4_SampleTable_t      table;
    Utl_Mpeg4_SyncSampleCursor_t cursor;
    uint32_t                     sample;
    Utl_Mpeg4_Span_t             bytes;
    bool_t                       valid;

    Utl_Mpeg4_InitializeSampleTable(&table, UTL_MPEG4_BOX_STSS);
    table.entryCount = TEST_SYNC_ENTRIES;
    Utl_Mpeg4_InitializeSyncSampleCursor(&cursor, &table);
    sample = TEST_SENTINEL;
    bytes  = Bytes((const byte_t*)TEST_STSS_FIRST, sizeof(TEST_STSS_FIRST) - 1);
    valid  = Utl_Mpeg4_ReadSyncSample(&cursor, &sample, bytes);
    LOGASSERT(valid);
    LOGASSERT(1 == sample);
    valid = Utl_Mpeg4_ReadSyncSample(&cursor, &sample, bytes);
    LOGASSERT(!valid);
    LOGASSERT(1 == sample);
    LOGASSERT(TEST_SYNC_ENTRIES - 1 == cursor.remaining);
    bytes = Bytes((const byte_t*)TEST_STSS_MIDDLE, sizeof(TEST_STSS_MIDDLE) - 1);
    valid = Utl_Mpeg4_ReadSyncSample(&cursor, &sample, bytes);
    LOGASSERT(valid);
    LOGASSERT(TEST_FIRST_RUN_COUNT == sample);
    bytes = Bytes((const byte_t*)TEST_STSS_FIRST, sizeof(TEST_STSS_FIRST) - 1);
    valid = Utl_Mpeg4_ReadSyncSample(&cursor, &sample, bytes);
    LOGASSERT(!valid);
    LOGASSERT(TEST_FIRST_RUN_COUNT == sample);
    LOGASSERT(TEST_FIRST_RUN_COUNT == cursor.lastSample);
    LOGASSERT(1 == cursor.remaining);
    bytes = Bytes((const byte_t*)TEST_STSS_LAST, sizeof(TEST_STSS_LAST) - 1);
    valid = Utl_Mpeg4_ReadSyncSample(&cursor, &sample, bytes);
    LOGASSERT(valid);
    LOGASSERT(TEST_SAMPLE_COUNT == sample);
    LOGASSERT(0 == cursor.remaining);
    valid = Utl_Mpeg4_ReadSyncSample(&cursor, &sample, bytes);
    LOGASSERT(!valid);
    LOGASSERT(TEST_SAMPLE_COUNT == cursor.lastSample);
    LOGASSERT(0 == cursor.remaining);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckSampleQueries(const Utl_Mpeg4_TimeToSampleRun_t* run)
{
    uint64_t time;
    bool_t   valid;

    LOGASSERT(NULL != run);
    time  = TEST_SENTINEL;
    valid = Utl_Mpeg4_GetSampleDecodeTime(&time, run, TEST_SAMPLE_COUNT);
    LOGASSERT(valid);
    LOGASSERT(TEST_LAST_SAMPLE_TIME == time);
    valid = Utl_Mpeg4_GetSampleDecodeTime(&time, run, TEST_SAMPLE_COUNT + 1);
    LOGASSERT(!valid);
    LOGASSERT(TEST_LAST_SAMPLE_TIME == time);
    valid = Utl_Mpeg4_GetSampleDecodeTime(&time, run, TEST_FIRST_RUN_COUNT);
    LOGASSERT(!valid);
    LOGASSERT(TEST_LAST_SAMPLE_TIME == time);
    valid = Utl_Mpeg4_GetSampleDecodeTime(&time, run, 0);
    LOGASSERT(!valid);
    LOGASSERT(TEST_LAST_SAMPLE_TIME == time);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckTimingOverflow(void)
{
    Utl_Mpeg4_SampleTable_t        table;
    Utl_Mpeg4_TimeToSampleCursor_t cursor;
    Utl_Mpeg4_Span_t               bytes;
    bool_t                         valid;
    Utl_Mpeg4_TimeToSampleRun_t    run;
    uint64_t                       duration;

    Utl_Mpeg4_InitializeSampleTable(&table, UTL_MPEG4_BOX_STTS);
    table.entryCount = TEST_TIMING_ENTRIES;
    Utl_Mpeg4_InitializeTimeToSampleCursor(&cursor, &table);
    bytes = Bytes((const byte_t*)TEST_STTS_MAX_RUN, sizeof(TEST_STTS_MAX_RUN) - 1);
    valid = Utl_Mpeg4_ReadTimeToSampleRun(&cursor, &run, bytes);
    LOGASSERT(valid);
    duration = (uint64_t)UINT32_MAX * UINT32_MAX;
    LOGASSERT(duration == cursor.duration);
    valid = Utl_Mpeg4_ReadTimeToSampleRun(&cursor, &run, bytes);
    LOGASSERT(!valid);
    LOGASSERT(1 == cursor.remaining);
    LOGASSERT(UINT32_MAX == cursor.sampleCount);
    LOGASSERT(duration == cursor.duration);
    LOGASSERT(0 == run.sampleOffset);
    LOGASSERT(0 == run.decodeTime);
    LOGASSERT(UINT32_MAX == run.sampleCount);
    LOGASSERT(UINT32_MAX == run.sampleDelta);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckSyncTruncation(void)
{
    Utl_Mpeg4_SampleTable_t      table;
    Utl_Mpeg4_SyncSampleCursor_t cursor;
    Utl_Mpeg4_Span_t             bytes;
    uint32_t                     sample;
    uint32_t                     count;
    bool_t                       valid;

    Utl_Mpeg4_InitializeSampleTable(&table, UTL_MPEG4_BOX_STSS);
    table.entryCount = TEST_SYNC_ENTRIES;
    Utl_Mpeg4_InitializeSyncSampleCursor(&cursor, &table);
    bytes  = Bytes((const byte_t*)TEST_STSS_FIRST, sizeof(TEST_STSS_FIRST) - 1);
    sample = TEST_SENTINEL;
    for (count = 0; count < UTL_MPEG4_SYNC_SAMPLE_ENTRY_BYTES; ++count)
    {
        bytes.byteCount = count;
        valid           = Utl_Mpeg4_ReadSyncSample(&cursor, &sample, bytes);
        LOGASSERT(!valid);
        LOGASSERT(TEST_SENTINEL == sample);
        LOGASSERT(TEST_SYNC_ENTRIES == cursor.remaining);
        LOGASSERT(0 == cursor.lastSample);
    }
    bytes = Bytes((const byte_t*)TEST_STSS_ZERO, sizeof(TEST_STSS_ZERO) - 1);
    valid = Utl_Mpeg4_ReadSyncSample(&cursor, &sample, bytes);
    LOGASSERT(!valid);
    LOGASSERT(TEST_SENTINEL == sample);
    LOGASSERT(TEST_SYNC_ENTRIES == cursor.remaining);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CheckSyncTableHeader(TestContext_t* context)
{
    Utl_Mpeg4_SampleTable_t table;
    Utl_Mpeg4_BoxPayload_t  payload;
    Utl_Mpeg4_Span_t        bytes;
    bool_t                  valid;
    bool_t                  equal;

    LOGASSERT(NULL != context);
    Utl_Mpeg4_InitializeSampleTable(&table, UTL_MPEG4_BOX_STSS);
    payload.type      = UTL_MPEG4_BOX_STSS;
    payload.byteCount = TEST_SYNC_PAYLOAD_BYTES;
    bytes             = Bytes((const byte_t*)TEST_STSS_HEADER, sizeof(TEST_STSS_HEADER) - 1);
    valid             = Utl_Mpeg4_ReadSampleTableHeader(&table, bytes, payload);
    LOGASSERT(valid);
    LOGASSERT(UTL_MPEG4_BOX_STSS == table.type);
    LOGASSERT(TEST_SYNC_ENTRIES == table.entryCount);
    LOGASSERT(TEST_SYNC_PAYLOAD_BYTES == Utl_Mpeg4_GetSampleTableByteCount(&table));
    LOGASSERT(UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES ==
              Utl_Mpeg4_WriteSampleTableHeader(&table, context->output, sizeof(context->output)));
    equal = (0 == CRT_Memory_Compare(bytes.data, context->output, bytes.byteCount));
    LOGASSERT(equal);
    --payload.byteCount;
    valid = Utl_Mpeg4_ReadSampleTableHeader(&table, bytes, payload);
    LOGASSERT(!valid);
    LOGASSERT(UTL_MPEG4_BOX_STSS == table.type);
    LOGASSERT(TEST_SYNC_ENTRIES == table.entryCount);
    bytes             = Bytes((const byte_t*)TEST_EMPTY_TABLE, sizeof(TEST_EMPTY_TABLE) - 1);
    payload.byteCount = sizeof(TEST_EMPTY_TABLE) - 1;
    valid             = Utl_Mpeg4_ReadSampleTableHeader(&table, bytes, payload);
    LOGASSERT(valid);
    LOGASSERT(UTL_MPEG4_BOX_STSS == table.type);
    LOGASSERT(0 == table.entryCount);
    LOGASSERT(UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES == Utl_Mpeg4_GetSampleTableByteCount(&table));
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
    TestContext_t    context;
    Utl_Mpeg4_Span_t bytes;
    uint64_t         large_size;

    CRT_Memory_Set(context.original, TEST_SENTINEL, sizeof(context.original));
    bytes = Bytes((const byte_t*)TEST_ORDINARY, sizeof(TEST_ORDINARY) - 1);
    CheckBox(&context, bytes, TEST_ORDINARY_SIZE);
    bytes = Bytes((const byte_t*)TEST_UUID, sizeof(TEST_UUID) - 1);
    CheckBox(&context, bytes, TEST_UUID_SIZE);
    bytes      = Bytes((const byte_t*)TEST_LARGE_UUID, sizeof(TEST_LARGE_UUID) - 1);
    large_size = ((uint64_t)1 << TEST_LARGE_SIZE_SHIFT) + UTL_MPEG4_MAX_BOX_HEADER_BYTES;
    CheckBox(&context, bytes, large_size);
    CheckBoxBoundaries(&context);
    CheckFullBox(&context);
    CheckFileType(&context);
    CheckBitstream(&context);
    CheckBitstreamFailure(&context);
    CheckUnsigned(&context);
    CheckTableHeader(&context);
    CheckSyncTableHeader(&context);
    CheckTableHeaderRejection();
    CheckTimingRuns();
    CheckTimingFailure();
    CheckTimingOverflow();
    CheckZeroTiming();
    CheckSyncTruncation();
    CheckSyncSamples();
    return 0;
}
