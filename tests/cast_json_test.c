/*=================================================================================================
** 1. References
**===============================================================================================*/

/** RFC 8259 number grammar, structural writing and transactional operation boundaries. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_json.h"
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

#define TEST_CAPACITY (512)
#define TEST_FRAMES   (3)
#define TEST_SENTINEL (0xA5)
#define TEST_NESTED   "{\"a\":[null,true,false,{\"b\":\"x\"}],\"n\":18446744073709551615,\"d\":18446744073709551.615}"
#define TEST_TINY_DECIMAL     "0.0000000000000000001"
#define TEST_FRACTION_DIGITS  (3)
#define TEST_READER_VALUES    (64)
#define TEST_TREE_VALUES      (9)
#define TEST_EXTRA_OPERATIONS (3)
#define TEST_ARRAY_OPEN       ('[')
#define TEST_ARRAY_CLOSE      (']')
#define TEST_READ_ESCAPES     "\\\"\\\\\\/\\b\\f\\n\\r\\t\\u0000\\u0041\\uD83D\\uDE42"
#define TEST_DECODED_ESCAPES  "\"\\/\b\f\n\r\t\0A\xF0\x9F\x99\x82"
#define TEST_ESCAPED_MEMBER   "{\"\\u0074ype\":\"P\\u0049NG\",\"type\":\"SECOND\"}"

#define TEST_INTEGER_SENTINEL      (1234567)
#define TEST_INTEGER_MAX           "9223372036854775807"
#define TEST_INTEGER_MIN           "-9223372036854775808"
#define TEST_INTEGER_ABOVE_MAX     "9223372036854775808"
#define TEST_INTEGER_BELOW_MIN     "-9223372036854775809"
#define TEST_INTEGER_SCALED_MAX    "9.223372036854775807e18"
#define TEST_INTEGER_SCALED_MIN    "-922337203685477580800000000000000000000000e-23"
#define TEST_INTEGER_LONG_ONE      "100000000000000000000000000000000000000000000000000e-50"
#define TEST_INTEGER_LONG_FRACTION "100000000000000000000000000000000000000000000000001e-50"
#define TEST_INTEGER_HUGE_ZERO     "-0.00000000000000000000000000e999999999999999999999999999999999999"
#define TEST_INTEGER_HUGE_VALUE    "1e999999999999999999999999999999999999"
#define TEST_INTEGER_TINY_VALUE    "1e-999999999999999999999999999999999999"

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Entry-owned writer, nesting records, output and failure snapshot.
 */
typedef struct JsonTest_s
{
    Utl_Json_Reader_t reader;
    Utl_Json_Value_t  values[TEST_READER_VALUES];
    Utl_Json_Writer_t writer;                  ///< Sole writer under test.
    Utl_Json_Frame_t  frames[TEST_FRAMES];     ///< Bounded nesting storage.
    byte_t            output[TEST_CAPACITY];   ///< Destination surrounded by retained sentinel bytes.
    byte_t            original[TEST_CAPACITY]; ///< Whole-output snapshot before a rejected operation.
} JsonTest_t;

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
 * \brief Starts a quiescent writer case in fixed caller-owned storage.
 *
 * \par Details:
 *  The writer and sentinel are initialized.
 *
 * \param[out] test     Required case storage
 * \param[in]  frames   Number of available records, at most TEST_FRAMES
 *
 * \retval     None No value is returned.
 */
static FORCE_INLINE void InitializeCase(JsonTest_t* test, uint32_t frames);

/*************************************************************************************************/
/**
 * \brief Constructs a counted fixture string without a termination scan.
 *
 * \param[in] data    Borrowed bytes
 * \param[in] count   Readable extent
 *
 * \retval    text   Counted fixture span.
 */
static FORCE_INLINE Utl_Json_Text_t Text(const char_t* data, uint32_t count);

/*************************************************************************************************/
/**
 * \brief Checks an independently chosen number against RFC 8259 syntax.
 *
 * \par Details:
 *  A mismatch asserts.
 *
 * \param[in] expected   Expected classification
 * \param[in] data       Borrowed numeric lexeme
 * \param[in] count      Exact lexeme extent
 *
 * \retval    None No value is returned.
 */
static FORCE_INLINE void CheckNumber(bool_t expected, const char_t* data, uint32_t count);

/*************************************************************************************************/
/**
 * \brief Checks representative productions and malformed boundary forms.
 *
 * \par Details:
 *  A mismatch asserts.
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckNumbers(void);

/*************************************************************************************************/
/**
 * \brief Constructs an independently specified nested document.
 *
 * \par Details:
 *  The writer retains completion or its first error.
 *
 * \param[in,out] writer   Initialized output or counting writer
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void WriteNested(Utl_Json_Writer_t* writer);

/*************************************************************************************************/
/**
 * \brief Checks nested output, counting mode, exact capacity and scalar decimals.
 *
 * \par Details:
 *  A mismatch asserts.
 *
 * \param[in,out] test   Quiescent caller-owned case storage
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void CheckOutput(JsonTest_t* test);

/*************************************************************************************************/
/**
 * \brief Checks rejected grammar operations, terminal status and nesting bounds.
 *
 * \par Details:
 *  A mismatch asserts.
 *
 * \param[in,out] test   Quiescent caller-owned case storage
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void CheckState(JsonTest_t* test);

/*************************************************************************************************/
/**
 * \brief Checks that a completed root rejects subsequent names, strings and numbers.
 *
 * \par Details:
 *  A mismatch asserts.
 *
 * \param[in,out] test   Quiescent caller-owned case storage
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void CheckAfterComplete(JsonTest_t* test);

/*************************************************************************************************/
/**
 * \brief Checks one complete or malformed external JSON text and its publication state.
 *
 * \par Details:
 *  All assertions passed.
 *
 * \par Requirements:
 *  - 001 The reader shall own its fixed record storage for the complete call.
 *  - 002 Failed parsing shall not publish a partial root.
 *
 * \param[in,out] test        Entry-owned fixture
 * \param[in]     input       C literal viewed as counted UTF-8 bytes
 * \param[in]     byteCount   Exact input extent
 * \param[in]     expected    Required syntax result
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckDocument(JsonTest_t*           test,
                                       const char_t*         input,
                                       uint32_t              byteCount,
                                       Utl_Json_ReadStatus_t expected);

/*************************************************************************************************/
/**
 * \brief Checks JSON grammar, scalar validity, capacity and reader reuse.
 *
 * \par Details:
 *  All assertions passed.
 *
 * \par Requirements:
 *  - 001 Malformed external inputs shall return errors without assertions in the library.
 *  - 002 Every truncated container prefix and short record capacity shall be rejected.
 *
 * \param[in,out] test   Entry-owned fixture
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckReaderSyntax(JsonTest_t* test);

/*************************************************************************************************/
/**
 * \brief Checks exact-capacity nonrecursive nesting.
 *
 * \par Requirements:
 *  - 001 The supplied record extent shall accept its exact depth and reject one additional level.
 *
 * \param[in,out] test   Entry-owned fixture
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckReaderDepth(JsonTest_t* test);

/*************************************************************************************************/
/**
 * \brief Checks decoded names, linked child traversal and bounded nonrecursive nesting.
 *
 * \par Details:
 *  All assertions passed.
 *
 * \par Requirements:
 *  - 001 Lookup shall compare decoded names and retain duplicate source order.
 *  - 002 Nesting shall use supplied records up to their exact capacity.
 *
 * \param[in,out] test   Entry-owned fixture
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckReaderTree(JsonTest_t* test);

/*************************************************************************************************/
/**
 * \brief Checks preservation after invalid escape decoding.
 *
 * \par Requirements:
 *  - 001 Invalid escapes shall preserve both outputs and empty input shall perform no write.
 *
 * \param[in,out] test   Entry-owned fixture
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckReaderInvalidStrings(JsonTest_t* test);

/*************************************************************************************************/
/**
 * \brief Checks UTF-8 decoding and preservation on malformed or short output.
 *
 * \par Details:
 *  All assertions passed.
 *
 * \par Requirements:
 *  - 001 Short output and invalid Unicode shall preserve the complete destination and count.
 *  - 002 Escaped U+0000 shall remain a counted byte and not terminate output.
 *
 * \param[in,out] test   Entry-owned fixture
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CheckReaderStrings(JsonTest_t* test);

/*************************************************************************************************/
/**
 * \brief Checks exact integer conversion from one independently specified JSON value.
 *
 * \par Details:
 *  A mismatch asserts in every build.
 *
 * \par Requirements:
 *  - 001 The input shall be complete valid JSON; numeric-domain rejection shall preserve the result sentinel.
 *  - 002 Non-NULL expected storage shall designate the exact signed result; NULL shall designate rejection.
 *
 * \param[in,out] test       Entry-owned reader and records
 * \param[in]     text       Independent complete JSON fixture
 * \param[in]     expected   Exact expected value, or NULL for domain rejection
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void CheckInteger(JsonTest_t* test, Utl_Json_Text_t text, const int64_t* expected);

/*************************************************************************************************/
/**
 * \brief Exercises integer limits, fractional cancellation, huge exponents and wrong value kinds.
 *
 * \par Details:
 *  A mismatch asserts in every build.
 *
 * \par Requirements:
 *  - 001 Boundaries shall be checked with independently chosen values and lexical forms.
 *  - 002 Rejected queries shall preserve output; queries shall not change complete-document status.
 *
 * \param[in,out] test   Entry-owned reader storage
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void CheckIntegers(JsonTest_t* test);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Case reuse is serialized and acquires no new storage or resources.
 */
static FORCE_INLINE void InitializeCase(JsonTest_t* test, uint32_t frames)
{
    LOGASSERT(NULL != test);
    LOGASSERT(frames <= TEST_FRAMES);

    CRT_Memory_Set(test->output, TEST_SENTINEL, sizeof(test->output));
    Utl_Json_InitializeWriter(&test->writer, test->frames, frames, test->output, sizeof(test->output));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Json_Text_t Text(const char_t* data, uint32_t count)
{
    Utl_Json_Text_t text;

    text.data      = (const byte_t*)data;
    text.byteCount = count;
    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Numeric syntax is independent of machine floating-point range.
 */
static FORCE_INLINE void CheckNumber(bool_t expected, const char_t* data, uint32_t count)
{
    Utl_Json_Text_t text;
    bool_t          actual;

    text   = Text(data, count);
    actual = Utl_Json_IsNumber(text);
    LOGASSERT(expected == actual);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Exponents deliberately exceed machine magnitude; malformed syntax must still fail.
 */
static FORCE_INLINE void CheckNumbers(void)
{
    CheckNumber(TRUE, "0", sizeof("0") - 1);
    CheckNumber(TRUE, "-0", sizeof("-0") - 1);
    CheckNumber(TRUE, "1", sizeof("1") - 1);
    CheckNumber(TRUE, "-1.25e-3000", sizeof("-1.25e-3000") - 1);
    CheckNumber(TRUE, "1E+999999", sizeof("1E+999999") - 1);
    CheckNumber(FALSE, "", 0);
    CheckNumber(FALSE, NULL, 1);
    CheckNumber(FALSE, "-", sizeof("-") - 1);
    CheckNumber(FALSE, "+1", sizeof("+1") - 1);
    CheckNumber(FALSE, ".1", sizeof(".1") - 1);
    CheckNumber(FALSE, "1.", sizeof("1.") - 1);
    CheckNumber(FALSE, "01", sizeof("01") - 1);
    CheckNumber(FALSE, "-01", sizeof("-01") - 1);
    CheckNumber(FALSE, "1e", sizeof("1e") - 1);
    CheckNumber(FALSE, "1e+", sizeof("1e+") - 1);
    CheckNumber(FALSE, "1e-", sizeof("1e-") - 1);
    CheckNumber(FALSE, "1e+-2", sizeof("1e+-2") - 1);
    CheckNumber(FALSE, "1  ", sizeof("1  ") - 1);
    CheckNumber(FALSE, "NaN", sizeof("NaN") - 1);
    CheckNumber(FALSE, "Infinity", sizeof("Infinity") - 1);
    CheckNumber(FALSE, "1\0", sizeof("1\0") - 1);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Per-operation errors latch, so the identical operation sequence supports both
 *  successful output and all short-capacity cases without publishing false success.
 */
static FORCE_INLINE void WriteNested(Utl_Json_Writer_t* writer)
{
    Utl_Json_Text_t    text;
    Utl_Json_Decimal_t decimal;

    (void)Utl_Json_BeginObject(writer);
    text = Text("a", 1);
    (void)Utl_Json_WriteName(writer, text);
    (void)Utl_Json_BeginArray(writer);
    (void)Utl_Json_WriteNull(writer);
    (void)Utl_Json_WriteBoolean(writer, TRUE);
    (void)Utl_Json_WriteBoolean(writer, FALSE);
    (void)Utl_Json_BeginObject(writer);
    text = Text("b", 1);
    (void)Utl_Json_WriteName(writer, text);
    text = Text("x", 1);
    (void)Utl_Json_WriteString(writer, text);
    (void)Utl_Json_EndObject(writer);
    (void)Utl_Json_EndArray(writer);
    text = Text("n", 1);
    (void)Utl_Json_WriteName(writer, text);
    (void)Utl_Json_WriteUint64(writer, UINT64_MAX);
    text = Text("d", 1);
    (void)Utl_Json_WriteName(writer, text);
    decimal.coefficient    = UINT64_MAX;
    decimal.fractionDigits = TEST_FRACTION_DIGITS;
    (void)Utl_Json_WriteDecimal(writer, decimal);
    (void)Utl_Json_EndObject(writer);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No terminator is included in output or required capacity. A failed operation
 *  cannot advance its published count or alter bytes on a subsequent write.
 */
static FORCE_INLINE void CheckOutput(JsonTest_t* test)
{
    uint32_t           expected;
    uint32_t           capacity;
    uint32_t           count;
    Utl_Json_Decimal_t decimal;

    expected = sizeof(TEST_NESTED) - 1;
    InitializeCase(test, TEST_FRAMES);
    Utl_Json_InitializeWriter(&test->writer, test->frames, TEST_FRAMES, NULL, UINT32_MAX);
    WriteNested(&test->writer);
    LOGASSERT(Utl_Json_IsComplete(&test->writer));
    LOGASSERT(expected == Utl_Json_GetByteCount(&test->writer));
    Utl_Json_InitializeWriter(&test->writer, test->frames, TEST_FRAMES, test->output, expected);
    WriteNested(&test->writer);
    LOGASSERT(Utl_Json_IsComplete(&test->writer));
    LOGASSERT(0 == CRT_Memory_Compare(test->output, TEST_NESTED, expected));
    for (capacity = 0; capacity < expected; ++capacity)
    {
        InitializeCase(test, TEST_FRAMES);
        Utl_Json_InitializeWriter(&test->writer, test->frames, TEST_FRAMES, test->output, capacity);
        WriteNested(&test->writer);
        LOGASSERT(UTL_JSON_OUTPUT_EXHAUSTED == Utl_Json_GetStatus(&test->writer));
        CRT_Memory_Copy(test->original, test->output, sizeof(test->output));
        count = Utl_Json_GetByteCount(&test->writer);
        LOGASSERT(!Utl_Json_WriteNull(&test->writer));
        LOGASSERT(count == Utl_Json_GetByteCount(&test->writer));
        LOGASSERT(0 == CRT_Memory_Compare(test->output, test->original, sizeof(test->output)));
    }
    InitializeCase(test, 0);
    decimal.coefficient    = 1;
    decimal.fractionDigits = UTL_JSON_MAX_FRACTION_DIGITS;
    LOGASSERT(Utl_Json_WriteDecimal(&test->writer, decimal));
    LOGASSERT(Utl_Json_IsComplete(&test->writer));
    LOGASSERT(sizeof(TEST_TINY_DECIMAL) - 1 == Utl_Json_GetByteCount(&test->writer));
    LOGASSERT(0 == CRT_Memory_Compare(test->output, TEST_TINY_DECIMAL, sizeof(TEST_TINY_DECIMAL) - 1));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Incomplete names, mismatched ends and extra roots fail before writing. The
 *  reader-free scalar path needs no nesting records; a container still does.
 */
static FORCE_INLINE void CheckState(JsonTest_t* test)
{
    Utl_Json_Text_t text;

    text = Text("x", 1);
    InitializeCase(test, TEST_FRAMES);
    LOGASSERT(!Utl_Json_WriteName(&test->writer, text));
    LOGASSERT(UTL_JSON_INVALID_STATE == Utl_Json_GetStatus(&test->writer));
    LOGASSERT(0 == Utl_Json_GetByteCount(&test->writer));
    InitializeCase(test, TEST_FRAMES);
    LOGASSERT(Utl_Json_BeginObject(&test->writer));
    LOGASSERT(Utl_Json_WriteName(&test->writer, text));
    LOGASSERT(!Utl_Json_EndObject(&test->writer));
    LOGASSERT(UTL_JSON_INVALID_STATE == Utl_Json_GetStatus(&test->writer));
    InitializeCase(test, TEST_FRAMES);
    LOGASSERT(Utl_Json_BeginObject(&test->writer));
    LOGASSERT(!Utl_Json_EndArray(&test->writer));
    LOGASSERT(UTL_JSON_INVALID_STATE == Utl_Json_GetStatus(&test->writer));
    InitializeCase(test, 1);
    LOGASSERT(Utl_Json_BeginArray(&test->writer));
    CRT_Memory_Copy(test->original, test->output, sizeof(test->output));
    LOGASSERT(!Utl_Json_BeginObject(&test->writer));
    LOGASSERT(UTL_JSON_DEPTH_EXHAUSTED == Utl_Json_GetStatus(&test->writer));
    LOGASSERT(0 == CRT_Memory_Compare(test->output, test->original, sizeof(test->output)));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every extra operation leaves the completed bytes unchanged and invalidates the
 *  writer's completion status, including operations with otherwise valid arguments.
 */
static FORCE_INLINE void CheckAfterComplete(JsonTest_t* test)
{
    Utl_Json_Text_t text;
    uint32_t        operation;

    text = Text("x", 1);
    for (operation = 0; operation < TEST_EXTRA_OPERATIONS; ++operation)
    {
        InitializeCase(test, 0);
        LOGASSERT(Utl_Json_WriteNull(&test->writer));
        CRT_Memory_Copy(test->original, test->output, sizeof(test->output));
        if (0 == operation)
        {
            LOGASSERT(!Utl_Json_WriteName(&test->writer, text));
        }
        else if (1 == operation)
        {
            LOGASSERT(!Utl_Json_WriteString(&test->writer, text));
        }
        else
        {
            text = Text("1", 1);
            LOGASSERT(!Utl_Json_WriteNumber(&test->writer, text));
        }
        LOGASSERT(UTL_JSON_INVALID_STATE == Utl_Json_GetStatus(&test->writer));
        LOGASSERT(0 == CRT_Memory_Compare(test->output, test->original, sizeof(test->output)));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each read expires earlier borrows. Root publication depends on the complete
 *  syntax result, including capacity failure and trailing-data rejection.
 */
static FORCE_INLINE void CheckDocument(JsonTest_t*           test,
                                       const char_t*         input,
                                       uint32_t              byteCount,
                                       Utl_Json_ReadStatus_t expected)
{
    Utl_Json_Text_t         source;
    Utl_Json_ReadStatus_t   status;
    const Utl_Json_Value_t* root;

    source = Text(input, byteCount);
    status = Utl_Json_ReadDocument(&test->reader, source);
    LOGASSERT(expected == status);
    LOGASSERT(expected == Utl_Json_GetReadStatus(&test->reader));
    LOGASSERT(byteCount >= Utl_Json_GetReadByteCount(&test->reader));
    root = Utl_Json_GetRoot(&test->reader);
    if (UTL_JSON_READ_COMPLETE == expected)
    {
        LOGASSERT(NULL != root);
        LOGASSERT(byteCount == Utl_Json_GetReadByteCount(&test->reader));
    }
    else
    {
        LOGASSERT(NULL == root);
        LOGASSERT(0 == test->reader.valueCount);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Independent malformed literals cover separators, numbers, raw controls,
 *  UTF-8, escape syntax and trailing bytes. Success after rejection verifies reuse.
 */
static FORCE_INLINE void CheckReaderSyntax(JsonTest_t* test)
{
    uint32_t prefix;
    uint32_t capacity;

    Utl_Json_InitializeReader(&test->reader, test->values, TEST_READER_VALUES);
    LOGASSERT(UTL_JSON_READ_READY == Utl_Json_GetReadStatus(&test->reader));
    LOGASSERT(0 == Utl_Json_GetReadByteCount(&test->reader));
    LOGASSERT(NULL == Utl_Json_GetRoot(&test->reader));
    CheckDocument(test, NULL, 0, UTL_JSON_READ_INVALID);
    CheckDocument(test, " \t\r\n", sizeof(" \t\r\n") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "null", sizeof("null") - 1, UTL_JSON_READ_COMPLETE);
    CheckDocument(test, "true", sizeof("true") - 1, UTL_JSON_READ_COMPLETE);
    CheckDocument(test, "false", sizeof("false") - 1, UTL_JSON_READ_COMPLETE);
    CheckDocument(test, "-1.25e+3000", sizeof("-1.25e+3000") - 1, UTL_JSON_READ_COMPLETE);
    CheckDocument(test, "\"\"", sizeof("\"\"") - 1, UTL_JSON_READ_COMPLETE);
    CheckDocument(test, "\"\xF0\x9F\x99\x82\"", sizeof("\"\xF0\x9F\x99\x82\"") - 1, UTL_JSON_READ_COMPLETE);
    CheckDocument(test, "\"\\uD800\"", sizeof("\"\\uD800\"") - 1, UTL_JSON_READ_COMPLETE);
    CheckDocument(test, " \t[]\r\n", sizeof(" \t[]\r\n") - 1, UTL_JSON_READ_COMPLETE);
    CheckDocument(test, "\xEF\xBB\xBF{}", sizeof("\xEF\xBB\xBF{}") - 1, UTL_JSON_READ_COMPLETE);
    CheckDocument(test, "True", sizeof("True") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "nullx", sizeof("nullx") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "[truefalse]", sizeof("[truefalse]") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "[1,]", sizeof("[1,]") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "[,1]", sizeof("[,1]") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "[1 2]", sizeof("[1 2]") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "[}", sizeof("[}") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "{\"a\":1,}", sizeof("{\"a\":1,}") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "{\"a\" 1}", sizeof("{\"a\" 1}") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "{a:1}", sizeof("{a:1}") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "{}[]", sizeof("{}[]") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "{}\0", sizeof("{}\0") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "01", sizeof("01") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "1.", sizeof("1.") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "1e+", sizeof("1e+") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "+1", sizeof("+1") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "\"\\x41\"", sizeof("\"\\x41\"") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "\"\\u12x4\"", sizeof("\"\\u12x4\"") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "\"\\u123\"", sizeof("\"\\u123\"") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "\"\\", sizeof("\"\\") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "\"\n\"", sizeof("\"\n\"") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "\"\0\"", sizeof("\"\0\"") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "\"\xC0\xAF\"", sizeof("\"\xC0\xAF\"") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "\"\xED\xA0\x80\"", sizeof("\"\xED\xA0\x80\"") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "\"\xF4\x90\x80\x80\"", sizeof("\"\xF4\x90\x80\x80\"") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "\"\xF0\x9F\"", sizeof("\"\xF0\x9F\"") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, "\xEF\xBB", sizeof("\xEF\xBB") - 1, UTL_JSON_READ_INVALID);
    CheckDocument(test, " \xEF\xBB\xBF{}", sizeof(" \xEF\xBB\xBF{}") - 1, UTL_JSON_READ_INVALID);
    for (prefix = 0; prefix < sizeof(TEST_NESTED) - 1; ++prefix)
    {
        CheckDocument(test, TEST_NESTED, prefix, UTL_JSON_READ_INVALID);
    }
    for (capacity = 1; capacity < TEST_TREE_VALUES; ++capacity)
    {
        Utl_Json_InitializeReader(&test->reader, test->values, capacity);
        CheckDocument(test, TEST_NESTED, sizeof(TEST_NESTED) - 1, UTL_JSON_READ_EXHAUSTED);
    }
    Utl_Json_InitializeReader(&test->reader, test->values, TEST_TREE_VALUES);
    CheckDocument(test, TEST_NESTED, sizeof(TEST_NESTED) - 1, UTL_JSON_READ_COMPLETE);
    LOGASSERT(TEST_TREE_VALUES == test->reader.valueCount);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The exact-capacity input uses one array per caller-owned record; one fewer record must fail.
 */
static FORCE_INLINE void CheckReaderDepth(JsonTest_t* test)
{
    uint32_t                index;
    Utl_Json_Text_t         source;
    const Utl_Json_Value_t* value;

    Utl_Json_InitializeReader(&test->reader, test->values, TEST_READER_VALUES);
    for (index = 0; index < TEST_READER_VALUES; ++index)
    {
        test->output[index]                      = TEST_ARRAY_OPEN;
        test->output[TEST_READER_VALUES + index] = TEST_ARRAY_CLOSE;
    }
    source.data      = test->output;
    source.byteCount = TEST_READER_VALUES * 2;
    LOGASSERT(UTL_JSON_READ_COMPLETE == Utl_Json_ReadDocument(&test->reader, source));
    LOGASSERT(TEST_READER_VALUES == test->reader.valueCount);
    value = Utl_Json_GetRoot(&test->reader);
    for (index = 1; index < TEST_READER_VALUES; ++index)
    {
        LOGASSERT(1 == value->childCount);
        value = Utl_Json_GetElement(&test->reader, value, 0);
        LOGASSERT(NULL != value);
    }
    LOGASSERT(0 == value->childCount);
    Utl_Json_InitializeReader(&test->reader, test->values, TEST_READER_VALUES - 1);
    LOGASSERT(UTL_JSON_READ_EXHAUSTED == Utl_Json_ReadDocument(&test->reader, source));
    LOGASSERT(NULL == Utl_Json_GetRoot(&test->reader));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Nested containers return to the owning parent after closure. A maximum-depth
 *  input contains one array per record and no recursively called C frame.
 */
static FORCE_INLINE void CheckReaderTree(JsonTest_t* test)
{
    const Utl_Json_Value_t* root;
    const Utl_Json_Value_t* array;
    const Utl_Json_Value_t* value;

    Utl_Json_InitializeReader(&test->reader, test->values, TEST_READER_VALUES);
    CheckDocument(test, TEST_NESTED, sizeof(TEST_NESTED) - 1, UTL_JSON_READ_COMPLETE);
    root = Utl_Json_GetRoot(&test->reader);
    LOGASSERT(UTL_JSON_VALUE_OBJECT == root->kind);
    LOGASSERT(3 == root->childCount);
    array = Utl_Json_FindMember(&test->reader, root, Text("a", 1));
    LOGASSERT(NULL != array);
    LOGASSERT(4 == array->childCount);
    value = Utl_Json_GetElement(&test->reader, array, 0);
    LOGASSERT(NULL != value);
    LOGASSERT(UTL_JSON_VALUE_NULL == value->kind);
    value = Utl_Json_GetElement(&test->reader, array, 1);
    LOGASSERT(NULL != value);
    LOGASSERT(UTL_JSON_VALUE_TRUE == value->kind);
    value = Utl_Json_GetElement(&test->reader, array, 2);
    LOGASSERT(NULL != value);
    LOGASSERT(UTL_JSON_VALUE_FALSE == value->kind);
    value = Utl_Json_GetElement(&test->reader, array, 3);
    value = Utl_Json_FindMember(&test->reader, value, Text("b", 1));
    LOGASSERT(Utl_Json_IsStringEqual(value, Text("x", 1)));
    LOGASSERT(NULL == Utl_Json_GetElement(&test->reader, array, 4));
    LOGASSERT(NULL == Utl_Json_GetElement(&test->reader, root, 0));
    LOGASSERT(NULL == Utl_Json_FindMember(&test->reader, root, Text("absent", sizeof("absent") - 1)));
    CheckDocument(test, TEST_ESCAPED_MEMBER, sizeof(TEST_ESCAPED_MEMBER) - 1, UTL_JSON_READ_COMPLETE);
    root = Utl_Json_GetRoot(&test->reader);
    LOGASSERT(2 == root->childCount);
    value = Utl_Json_FindMember(&test->reader, root, Text("type", sizeof("type") - 1));
    LOGASSERT(Utl_Json_IsStringEqual(value, Text("PING", sizeof("PING") - 1)));
    LOGASSERT(!Utl_Json_IsStringEqual(value, Text("ping", sizeof("ping") - 1)));
    LOGASSERT(!Utl_Json_IsStringEqual(value, Text("\xFF", sizeof("\xFF") - 1)));
    CheckReaderDepth(test);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Rejected escapes preserve the destination and count; the empty extent succeeds without writing.
 */
static FORCE_INLINE void CheckReaderInvalidStrings(JsonTest_t* test)
{
    uint32_t written;

    CRT_Memory_Copy(test->original, test->output, sizeof(test->output));
    written = UINT32_MAX;
    LOGASSERT(
        !Utl_Json_ReadString(&written, test->output, sizeof(test->output), Text("\\uD800", sizeof("\\uD800") - 1)));
    LOGASSERT(
        !Utl_Json_ReadString(&written, test->output, sizeof(test->output), Text("\\uDC00", sizeof("\\uDC00") - 1)));
    LOGASSERT(
        !Utl_Json_ReadString(&written, test->output, sizeof(test->output), Text("\\uD800A", sizeof("\\uD800A") - 1)));
    LOGASSERT(!Utl_Json_ReadString(&written,
                                   test->output,
                                   sizeof(test->output),
                                   Text("\\uD800\\uD800", sizeof("\\uD800\\uD800") - 1)));
    LOGASSERT(!Utl_Json_ReadString(&written, test->output, sizeof(test->output), Text("\\q", sizeof("\\q") - 1)));
    LOGASSERT(!Utl_Json_ReadString(&written, test->output, sizeof(test->output), Text("\xFF", sizeof("\xFF") - 1)));
    LOGASSERT(UINT32_MAX == written);
    LOGASSERT(0 == CRT_Memory_Compare(test->output, test->original, sizeof(test->output)));
    LOGASSERT(!Utl_Json_GetDecodedByteCount(&written, Text("\\uDC00", sizeof("\\uDC00") - 1)));
    LOGASSERT(UINT32_MAX == written);
    LOGASSERT(Utl_Json_ReadString(&written, test->output, 0, Text(NULL, 0)));
    LOGASSERT(0 == written);
    LOGASSERT(0 == CRT_Memory_Compare(test->output, test->original, sizeof(test->output)));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All short output extents preserve sentinels. Supplementary scalars use four
 *  UTF-8 bytes; escaped unpaired UTF-16 remains representable only in the raw tree.
 */
static FORCE_INLINE void CheckReaderStrings(JsonTest_t* test)
{
    Utl_Json_Text_t source;
    uint32_t        required;
    uint32_t        capacity;
    uint32_t        written;

    source = Text(TEST_READ_ESCAPES, sizeof(TEST_READ_ESCAPES) - 1);
    LOGASSERT(Utl_Json_GetDecodedByteCount(&required, source));
    CRT_Memory_Set(test->output, TEST_SENTINEL, sizeof(test->output));
    CRT_Memory_Copy(test->original, test->output, sizeof(test->output));
    for (capacity = 0; capacity < sizeof(TEST_DECODED_ESCAPES) - 1; ++capacity)
    {
        written = UINT32_MAX;
        LOGASSERT(!Utl_Json_ReadString(&written, test->output, capacity, source));
        LOGASSERT(UINT32_MAX == written);
        LOGASSERT(0 == CRT_Memory_Compare(test->output, test->original, sizeof(test->output)));
    }
    LOGASSERT(Utl_Json_ReadString(&written, test->output, sizeof(TEST_DECODED_ESCAPES) - 1, source));
    LOGASSERT(0 == CRT_Memory_Compare(test->output, TEST_DECODED_ESCAPES, sizeof(TEST_DECODED_ESCAPES) - 1));
    LOGASSERT(TEST_SENTINEL == test->output[sizeof(TEST_DECODED_ESCAPES) - 1]);
    LOGASSERT(sizeof(TEST_DECODED_ESCAPES) - 1 == required);
    LOGASSERT(required == written);
    CheckReaderInvalidStrings(test);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Grammar acceptance is separate from integer-domain acceptance. A rejected
 *  numeric conversion retains its result sentinel and the parsed document.
 */
static FORCE_INLINE void CheckInteger(JsonTest_t* test, Utl_Json_Text_t text, const int64_t* expected)
{
    Utl_Json_ReadStatus_t   status;
    const Utl_Json_Value_t* value;
    int64_t                 result;
    bool_t                  accepted;

    status = Utl_Json_ReadDocument(&test->reader, text);
    LOGASSERT(UTL_JSON_READ_COMPLETE == status);
    value    = Utl_Json_GetRoot(&test->reader);
    result   = TEST_INTEGER_SENTINEL;
    accepted = Utl_Json_GetInt64(&result, value);
    if (NULL != expected)
    {
        LOGASSERT(accepted);
        LOGASSERT(*expected == result);
    }
    else
    {
        LOGASSERT(!accepted);
        LOGASSERT(TEST_INTEGER_SENTINEL == result);
    }
    LOGASSERT(UTL_JSON_READ_COMPLETE == Utl_Json_GetReadStatus(&test->reader));
    LOGASSERT(value == Utl_Json_GetRoot(&test->reader));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Long mantissas test that omitted nonzero digits prevent truncation. Exponent
 *  cases test exact cancellation, both signs, leading zeros and bounded scanning.
 */
static FORCE_INLINE void CheckIntegers(JsonTest_t* test)
{
    int64_t expected;
    int64_t result;
    bool_t  accepted;

    Utl_Json_InitializeReader(&test->reader, test->values, TEST_READER_VALUES);
    expected = 0;
    CheckInteger(test, Text("0", sizeof("0") - 1), &expected);
    CheckInteger(test, Text("-0", sizeof("-0") - 1), &expected);
    CheckInteger(test, Text("0.000e-999999999999999999999", sizeof("0.000e-999999999999999999999") - 1), &expected);
    CheckInteger(test, Text(TEST_INTEGER_HUGE_ZERO, sizeof(TEST_INTEGER_HUGE_ZERO) - 1), &expected);
    expected = 1;
    CheckInteger(test, Text("1", sizeof("1") - 1), &expected);
    CheckInteger(test, Text("1.0", sizeof("1.0") - 1), &expected);
    CheckInteger(test, Text("1e+000", sizeof("1e+000") - 1), &expected);
    CheckInteger(test, Text("0.0000100E+5", sizeof("0.0000100E+5") - 1), &expected);
    CheckInteger(test, Text(TEST_INTEGER_LONG_ONE, sizeof(TEST_INTEGER_LONG_ONE) - 1), &expected);
    expected = -1;
    CheckInteger(test, Text("-10e-1", sizeof("-10e-1") - 1), &expected);
    expected = INT64_MAX;
    CheckInteger(test, Text(TEST_INTEGER_MAX, sizeof(TEST_INTEGER_MAX) - 1), &expected);
    CheckInteger(test, Text(TEST_INTEGER_SCALED_MAX, sizeof(TEST_INTEGER_SCALED_MAX) - 1), &expected);
    expected = INT64_MIN;
    CheckInteger(test, Text(TEST_INTEGER_MIN, sizeof(TEST_INTEGER_MIN) - 1), &expected);
    CheckInteger(test, Text(TEST_INTEGER_SCALED_MIN, sizeof(TEST_INTEGER_SCALED_MIN) - 1), &expected);
    CheckInteger(test, Text(TEST_INTEGER_ABOVE_MAX, sizeof(TEST_INTEGER_ABOVE_MAX) - 1), NULL);
    CheckInteger(test, Text(TEST_INTEGER_BELOW_MIN, sizeof(TEST_INTEGER_BELOW_MIN) - 1), NULL);
    CheckInteger(test, Text(TEST_INTEGER_LONG_FRACTION, sizeof(TEST_INTEGER_LONG_FRACTION) - 1), NULL);
    CheckInteger(test, Text(TEST_INTEGER_HUGE_VALUE, sizeof(TEST_INTEGER_HUGE_VALUE) - 1), NULL);
    CheckInteger(test, Text(TEST_INTEGER_TINY_VALUE, sizeof(TEST_INTEGER_TINY_VALUE) - 1), NULL);
    CheckInteger(test, Text("0.1", sizeof("0.1") - 1), NULL);
    CheckInteger(test, Text("1.01", sizeof("1.01") - 1), NULL);
    CheckInteger(test, Text("1e19", sizeof("1e19") - 1), NULL);
    CheckInteger(test, Text("99e-1", sizeof("99e-1") - 1), NULL);
    CheckInteger(test, Text("92233720368547758070e-2", sizeof("92233720368547758070e-2") - 1), NULL);
    CheckInteger(test, Text("1e4294967314", sizeof("1e4294967314") - 1), NULL);
    CheckInteger(test, Text("1e4294967315", sizeof("1e4294967315") - 1), NULL);
    CheckInteger(test, Text("1e-4294967315", sizeof("1e-4294967315") - 1), NULL);
    CheckInteger(test, Text("null", sizeof("null") - 1), NULL);
    CheckInteger(test, Text("true", sizeof("true") - 1), NULL);
    CheckInteger(test, Text("\"1\"", sizeof("\"1\"") - 1), NULL);
    CheckInteger(test, Text("[]", sizeof("[]") - 1), NULL);
    CheckInteger(test, Text("{}", sizeof("{}") - 1), NULL);
    result   = TEST_INTEGER_SENTINEL;
    accepted = Utl_Json_GetInt64(&result, NULL);
    LOGASSERT(!accepted);
    LOGASSERT(TEST_INTEGER_SENTINEL == result);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  All fixture storage belongs to this entry point; the test has no global state.
 */
uint32_t OS_Application_Main(void)
{
    JsonTest_t test;

    CheckNumbers();
    CheckOutput(&test);
    CheckState(&test);
    CheckAfterComplete(&test);
    CheckReaderSyntax(&test);
    CheckReaderTree(&test);
    CheckReaderStrings(&test);
    CheckIntegers(&test);
    return 0;
}
