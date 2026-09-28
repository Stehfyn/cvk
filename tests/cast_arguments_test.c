/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Independent Microsoft command-line examples and UTF-16 boundaries.
 * Fixtures exercise public utilities; process startup and failure handling use OS interfaces. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_assert.h"
#include "os/os_process.h"
#include "utl/utl_arguments.h"
#include "utl/utl_unicode.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TEST_CAPACITY                           (128)
#define TEST_SENTINEL                           (0xA55A)
#define ARGUMENT_CASE(text, argument, expected) {text, sizeof(text) - 1, argument, expected, sizeof(expected) - 1}

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Independent counted ASCII input and expected decoded argument.
 */
typedef struct ArgumentCase_s
{
    const char_t* input;         ///< Complete command-line fixture.
    uint32_t      inputCount;    ///< Counted ASCII bytes.
    uint32_t      argument;      ///< Zero-based requested argument.
    const char_t* expected;      ///< Independently specified output.
    uint32_t      expectedCount; ///< Expected output units.
} ArgumentCase_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static const ArgumentCase_t c_cases[]         = {ARGUMENT_CASE("\"C:\\a b\\test.exe\" t", 0, "C:\\a b\\test.exe"),
                                                 ARGUMENT_CASE("app \"a b c\" d e", 1, "a b c"),
                                                 ARGUMENT_CASE("app \"a b c\" d e", 2, "d"),
                                                 ARGUMENT_CASE("app \"a b c\" d e", 3, "e"),
                                                 ARGUMENT_CASE("app \"ab\\\"c\" \"\\\\\" d", 1, "ab\"c"),
                                                 ARGUMENT_CASE("app \"ab\\\"c\" \"\\\\\" d", 2, "\\"),
                                                 ARGUMENT_CASE("app a\\\\\\b d\"e f\"g h", 1, "a\\\\\\b"),
                                                 ARGUMENT_CASE("app a\\\\\\b d\"e f\"g h", 2, "de fg"),
                                                 ARGUMENT_CASE("app a\\\\\\\"b c d", 1, "a\\\"b"),
                                                 ARGUMENT_CASE("app a\\\\\\\\\"b c\" d e", 1, "a\\\\b c"),
                                                 ARGUMENT_CASE("app a\"b\"\" c d", 1, "ab\" c d"),
                                                 ARGUMENT_CASE("app \"\"", 1, ""),
                                                 ARGUMENT_CASE("app \"unterminated input", 1, "unterminated input"),
                                                 ARGUMENT_CASE("app \\\"quoted\\\"", 1, "\"quoted\""),
                                                 ARGUMENT_CASE("app tail\\\\", 1, "tail\\\\"),
                                                 ARGUMENT_CASE(" \tapp \t \"t\" \t", 0, ""),
                                                 ARGUMENT_CASE(" \tapp \t \"t\" \t", 1, "app"),
                                                 ARGUMENT_CASE(" \tapp \t \"t\" \t", 2, "t"),
                                                 ARGUMENT_CASE("\"a\"b\" c\" d", 0, "ab c"),
                                                 ARGUMENT_CASE("app \"a\"\"b\"", 1, "a\"b"),
                                                 ARGUMENT_CASE("app ^", 1, "^")};
static const uint16_t       c_supplementary[] = {'p', ' ', '"', 0xD83D, 0xDE00, '"'};
static const uint16_t       c_terminated[]    = {'a', 0xD800, 0, 'x'};
static const uint16_t       c_missing[]       = {'p', ' ', '\t'};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Checks one independent vector at every insufficient and exact capacity.
 *
 * \param[in] fixture   Counted input and expected argument
 *
 * \retval    None
 */
static void CheckArgument(const ArgumentCase_t* fixture);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  ASCII fixtures are widened by their known counted length. Sentinels detect any
 *  write on failure, output overrun, or unwanted terminator.
 */
static void CheckArgument(const ArgumentCase_t* fixture)
{
    Utl_Arguments_Record_t record;
    uint16_t               input[TEST_CAPACITY];
    Utl_Arguments_t        arguments;
    uint32_t               index;
    uint32_t               capacity;
    uint16_t               output[TEST_CAPACITY];
    uint32_t               count;
    bool_t                 found;

    LOGASSERT(fixture->inputCount < TEST_CAPACITY);
    LOGASSERT(fixture->expectedCount < TEST_CAPACITY);
    record.units          = input;
    record.unitCount      = fixture->inputCount;
    arguments.records     = &record;
    arguments.recordCount = 1;
    for (index = 0; index < fixture->inputCount; ++index)
    {
        input[index] = (uint16_t)(byte_t)fixture->input[index];
    }
    for (capacity = 0; capacity <= fixture->expectedCount; ++capacity)
    {
        for (index = 0; index < TEST_CAPACITY; ++index)
        {
            output[index] = TEST_SENTINEL;
        }
        count = TEST_SENTINEL;
        found = Utl_Arguments_Read(&count, output, capacity, &arguments, fixture->argument);
        if (capacity < fixture->expectedCount)
        {
            LOGASSERT(!found);
            LOGASSERT(TEST_SENTINEL == count);
            for (index = 0; index < TEST_CAPACITY; ++index)
            {
                LOGASSERT(TEST_SENTINEL == output[index]);
            }
        }
        else
        {
            LOGASSERT(found);
            LOGASSERT(fixture->expectedCount == count);
            for (index = 0; index < count; ++index)
            {
                LOGASSERT((uint16_t)(byte_t)fixture->expected[index] == output[index]);
            }
            LOGASSERT(TEST_SENTINEL == output[count]);
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Supplementary and unpaired UTF-16 units remain counted data; the string-length
 *  query tests termination independently of Unicode scalar validation.
 */
uint32_t OS_Application_Main(void)
{
    uint32_t               index;
    uint16_t               output[3];
    uint32_t               count;
    Utl_Arguments_Record_t record;
    Utl_Arguments_t        arguments;
    bool_t                 found;

    OS_Process_SetInteractive(FALSE);
    for (index = 0; index < (sizeof(c_cases) / sizeof(c_cases[0])); ++index)
    {
        CheckArgument(&c_cases[index]);
    }
    output[0]             = TEST_SENTINEL;
    output[1]             = TEST_SENTINEL;
    output[2]             = TEST_SENTINEL;
    count                 = TEST_SENTINEL;
    record.units          = c_supplementary;
    record.unitCount      = sizeof(c_supplementary) / sizeof(c_supplementary[0]);
    arguments.records     = &record;
    arguments.recordCount = 1;
    found                 = Utl_Arguments_Read(&count, output, 2, &arguments, 1);
    LOGASSERT(found);
    LOGASSERT(2 == count);
    LOGASSERT(0xD83D == output[0]);
    LOGASSERT(0xDE00 == output[1]);
    LOGASSERT(TEST_SENTINEL == output[2]);
    count            = TEST_SENTINEL;
    output[0]        = TEST_SENTINEL;
    record.units     = c_missing;
    record.unitCount = sizeof(c_missing) / sizeof(c_missing[0]);
    found            = Utl_Arguments_Read(&count, output, 3, &arguments, 1);
    LOGASSERT(!found);
    LOGASSERT(TEST_SENTINEL == count);
    LOGASSERT(TEST_SENTINEL == output[0]);
    record.unitCount = 0;
    found            = Utl_Arguments_Read(&count, output, 3, &arguments, 1);
    LOGASSERT(!found);
    LOGASSERT(TEST_SENTINEL == count);
    found = Utl_Arguments_Read(&count, output, 3, &arguments, 0);
    LOGASSERT(found);
    LOGASSERT(0 == count);
    for (index = 0; index <= 2; ++index)
    {
        LOGASSERT(UTL_UNICODE_NO_TERMINATOR == Utl_Unicode_GetUtf16StringUnitCount(c_terminated, index));
    }
    LOGASSERT(2 == Utl_Unicode_GetUtf16StringUnitCount(c_terminated, 3));
    LOGASSERT(2 == Utl_Unicode_GetUtf16StringUnitCount(c_terminated, 4));
    LOGASSERT(0 == Utl_Unicode_GetUtf16StringUnitCount(&c_terminated[2], 1));
    return 0;
}
