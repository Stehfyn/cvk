/*=================================================================================================
** 1. References
**===============================================================================================*/

/** Microsoft C command-line argument grammar, including the special argv[0] rule.
 * https://learn.microsoft.com/en-us/cpp/c-language/parsing-c-command-line-arguments
 * Build selection chooses this implementation without target-selection preprocessor branches. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_compiler.h"
#include "os/os_types.h"
#include <stdint.h>
#include "utl/utl_arguments.h"
#include "os/os_assert.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define ARGUMENT_SPACE     (' ')
#define ARGUMENT_TAB       ('\t')
#define ARGUMENT_QUOTE     ('"')
#define ARGUMENT_BACKSLASH ('\\')

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
 * \brief Recognizes a command-line argument separator.
 * \param[in] value   Native UTF-16 code unit
 * \retval    TRUE    The unit is SP or HTAB.
 * \retval    FALSE   The unit is ordinary argument content.
 */
static FORCE_INLINE bool_t IsWhitespace(uint16_t value);

/*************************************************************************************************/
/**
 * \brief Counts and consumes consecutive backslashes at the current position.
 * \par Requirements:
 *  - 001 The cursor shall not exceed the readable input count.
 *  - 002 The first non-backslash unit shall remain unread.
 * \param[in] input         Readable command-line units
 * \param[in] inputCount    Readable unit count
 * \param[in,out] cursor    Current position
 * \retval    0..inputCount   Number of consumed backslashes.
 */
static FORCE_INLINE uint32_t ReadBackslashes(const uint16_t* input, uint32_t inputCount, uint32_t* cursor);

/*************************************************************************************************/
/**
 * \brief Applies quote interpretation after consuming a backslash run.
 * \par Requirements:
 *  - 001 The cursor shall identify a readable quote unit.
 *  - 002 Odd backslash runs shall retain a literal quote.
 *  - 003 Inside ordinary arguments a doubled quote shall emit one literal quote.
 * \param[in] input           Readable command-line units
 * \param[in] inputCount      Readable unit count
 * \param[in,out] cursor      Current quote position
 * \param[in,out] inQuotes    Current quoting state
 * \param[in] isProgram       Program-name grammar selector
 * \param[in] slashes         Preceding backslash count
 * \retval    TRUE    Emit a literal quote.
 * \retval    FALSE   The quote changes quoting state without output.
 */
static FORCE_INLINE bool_t ReadQuote(const uint16_t* input,
                                     uint32_t        inputCount,
                                     uint32_t*       cursor,
                                     bool_t*         inQuotes,
                                     bool_t          isProgram,
                                     uint32_t        slashes);

/*************************************************************************************************/
/**
 * \brief Measures or copies the argument at the current input offset.
 *
 * \param[out]    output       Output units or NULL for measurement
 * \param[in]     input        Readable counted input
 * \param[in]     inputCount   Readable input units
 * \param[in,out] cursor       First unit of the argument; advanced to its end
 * \param[in]     isProgram    TRUE selects the special program-name grammar
 *
 * \retval        0..inputCount   Decoded argument unit count.
 */
static FORCE_INLINE uint32_t
ReadArgument(uint16_t* output, const uint16_t* input, uint32_t inputCount, uint32_t* cursor, bool_t isProgram);

/*************************************************************************************************/
/**
 * \brief Counts one decoded unit and writes it during a preflighted output pass.
 * \par Requirements:
 *  - 001 A non-NULL output shall have room for the decoded argument.
 *  - 002 A NULL output shall select measurement without dereferencing output.
 * \param[out] output       Preflighted storage or NULL
 * \param[in,out] count     Current output unit count
 * \param[in] value         Decoded unit
 * \retval    None
 */
static FORCE_INLINE void WriteUnit(uint16_t* output, uint32_t* count, uint16_t value);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Quoting state determines whether this separator ends the current argument.
 */
static FORCE_INLINE bool_t IsWhitespace(uint16_t value)
{
    bool_t is_space;
    bool_t is_tab;

    is_space = (ARGUMENT_SPACE == value);
    is_tab   = (ARGUMENT_TAB == value);
    return is_space || is_tab;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only the following quote determines how many consumed backslashes are literal.
 */
static FORCE_INLINE uint32_t ReadBackslashes(const uint16_t* input, uint32_t inputCount, uint32_t* cursor)
{
    uint32_t start;
    bool_t   is_backslash;

    LOGASSERT(NULL != input);
    LOGASSERT(NULL != cursor);
    LOGASSERT(*cursor <= inputCount);
    start = *cursor;
    for (; *cursor < inputCount; ++(*cursor))
    {
        is_backslash = (ARGUMENT_BACKSLASH == input[*cursor]);
        if (!is_backslash)
        {
            break;
        }
    }
    return *cursor - start;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A doubled quote consumes its second unit here; the enclosing reader consumes
 *  the current unit. Program names do not use the ordinary doubled-quote rule.
 */
static FORCE_INLINE bool_t ReadQuote(const uint16_t* input,
                                     uint32_t        inputCount,
                                     uint32_t*       cursor,
                                     bool_t*         inQuotes,
                                     bool_t          isProgram,
                                     uint32_t        slashes)
{
    bool_t emit;
    bool_t odd_slashes;
    bool_t has_next;
    bool_t can_double_quote;
    bool_t doubled_quote;

    LOGASSERT(NULL != input);
    LOGASSERT(NULL != cursor);
    LOGASSERT(NULL != inQuotes);
    LOGASSERT(*cursor < inputCount);
    LOGASSERT(ARGUMENT_QUOTE == input[*cursor]);
    emit        = TRUE;
    odd_slashes = !!(slashes % 2);
    if (!odd_slashes)
    {
        has_next         = ((*cursor + 1) < inputCount);
        can_double_quote = !isProgram && *inQuotes && has_next;
        doubled_quote    = FALSE;
        if (can_double_quote)
        {
            doubled_quote = (ARGUMENT_QUOTE == input[*cursor + 1]);
        }
        if (doubled_quote)
        {
            ++(*cursor);
        }
        else
        {
            *inQuotes = !(*inQuotes);
            emit      = FALSE;
        }
    }
    return emit;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each loop advances cursor or finishes the argument. Output is optional only for this
 *  private measuring pass; the public API preflights capacity before the writing pass.
 */
static FORCE_INLINE uint32_t
ReadArgument(uint16_t* output, const uint16_t* input, uint32_t inputCount, uint32_t* cursor, bool_t isProgram)
{
    uint32_t count;
    bool_t   in_quotes;
    bool_t   has_input;
    uint16_t value;
    bool_t   is_space;
    bool_t   at_end;
    uint32_t slashes;
    bool_t   has_quote;
    uint32_t literal_slashes;
    uint32_t index;
    bool_t   emit;

    count     = 0;
    in_quotes = FALSE;
    for (;;)
    {
        has_input = (*cursor < inputCount);
        if (!has_input)
        {
            break;
        }
        value    = input[(*cursor)];
        is_space = IsWhitespace(value);
        at_end   = is_space && (!in_quotes);
        if (at_end)
        {
            break;
        }
        slashes = 0;
        if (!isProgram)
        {
            slashes = ReadBackslashes(input, inputCount, cursor);
        }
        has_input       = (*cursor < inputCount);
        has_quote       = has_input && (ARGUMENT_QUOTE == input[(*cursor)]);
        literal_slashes = has_quote ? (slashes / 2) : slashes;
        for (index = 0; index < literal_slashes; ++index)
        {
            WriteUnit(output, &count, ARGUMENT_BACKSLASH);
        }
        if (!has_input)
        {
            break;
        }
        value = input[(*cursor)];
        emit  = TRUE;
        if (has_quote)
        {
            emit = ReadQuote(input, inputCount, cursor, &in_quotes, isProgram, slashes);
        }
        else
        {
            is_space = IsWhitespace(value);
            at_end   = is_space && (!in_quotes);
            if (at_end)
            {
                break;
            }
        }
        if (emit)
        {
            WriteUnit(output, &count, value);
        }
        ++(*cursor);
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Measuring and writing follow the same decoded-unit sequence.
 */
static FORCE_INLINE void WriteUnit(uint16_t* output, uint32_t* count, uint16_t value)
{
    if (NULL != output)
    {
        output[*count] = value;
    }
    ++(*count);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The selected argument is first measured, then replayed into proven capacity.
 *  An unmatched opening quote retains the remaining units, as specified by Microsoft.
 */
extern FORCE_INLINE bool_t Utl_Arguments_Read(uint32_t*              unitCount,
                          uint16_t*              output,
                          uint32_t               capacity,
                          const Utl_Arguments_t* arguments,
                          uint32_t               argumentIndex)
{
    const uint16_t* input;
    uint32_t        input_count;
    uint32_t        cursor;
    uint32_t        argument;
    bool_t          found;
    bool_t          is_program;
    bool_t          has_input;
    bool_t          is_space;
    uint32_t        start;
    uint32_t        count;

    LOGASSERT(NULL != unitCount);
    LOGASSERT(NULL != output);
    LOGASSERT(NULL != arguments);
    LOGASSERT(1 == arguments->recordCount);
    LOGASSERT(NULL != arguments->records);
    input       = arguments->records[0].units;
    input_count = arguments->records[0].unitCount;
    LOGASSERT(NULL != input);
    cursor   = 0;
    argument = 0;
    found    = FALSE;
    for (;;)
    {
        is_program = (0 == argument);
        for (;;)
        {
            has_input = (cursor < input_count);
            if (is_program || !has_input)
            {
                break;
            }
            is_space = IsWhitespace(input[cursor]);
            if (!is_space)
            {
                break;
            }
            ++cursor;
        }
        if ((!is_program) && (!has_input))
        {
            break;
        }
        start = cursor;
        count = ReadArgument(NULL, input, input_count, &cursor, is_program);
        if (argument == argumentIndex)
        {
            found = (count <= capacity);
            if (found)
            {
                cursor       = start;
                (*unitCount) = ReadArgument(output, input, input_count, &cursor, is_program);
            }
            break;
        }
        ++argument;
    }
    return found;
}
