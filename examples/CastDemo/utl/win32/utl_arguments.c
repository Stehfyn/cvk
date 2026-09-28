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
static FORCE_INLINE uint32_t ReadArgument(uint16_t*       output,
                             const uint16_t* input,
                             uint32_t        inputCount,
                             uint32_t*       cursor,
                             bool_t          isProgram);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Each loop advances cursor or finishes the argument. Output is optional only for this
 *  private measuring pass; the public API preflights capacity before the writing pass.
 */
static FORCE_INLINE uint32_t ReadArgument(uint16_t*       output,
                             const uint16_t* input,
                             uint32_t        inputCount,
                             uint32_t*       cursor,
                             bool_t          isProgram)
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
    bool_t   odd_slashes;
    bool_t   doubled_quote;
    bool_t   has_next;
    bool_t   can_double_quote;

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
        is_space = (ARGUMENT_SPACE == value) || (ARGUMENT_TAB == value);
        at_end   = is_space && (!in_quotes);
        if (at_end)
        {
            break;
        }
        slashes = 0;
        if (!isProgram)
        {
            for (;;)
            {
                has_input = (*cursor < inputCount);
                if (!has_input || (ARGUMENT_BACKSLASH != input[(*cursor)]))
                {
                    break;
                }
                ++slashes;
                ++(*cursor);
            }
        }
        has_input       = (*cursor < inputCount);
        has_quote       = has_input && (ARGUMENT_QUOTE == input[(*cursor)]);
        literal_slashes = has_quote ? (slashes / 2) : slashes;
        for (index = 0; index < literal_slashes; ++index)
        {
            if (NULL != output)
            {
                output[count] = ARGUMENT_BACKSLASH;
            }
            ++count;
        }
        if (!has_input)
        {
            break;
        }
        value = input[(*cursor)];
        emit  = TRUE;
        if (has_quote)
        {
            odd_slashes = (0 != (slashes % 2));
            if (!odd_slashes)
            {
                doubled_quote    = FALSE;
                has_next         = ((*cursor + 1) < inputCount);
                can_double_quote = (!isProgram) && in_quotes && has_next;
                if (can_double_quote)
                {
                    doubled_quote = (ARGUMENT_QUOTE == input[(*cursor) + 1]);
                }
                if (doubled_quote)
                {
                    ++(*cursor);
                }
                else
                {
                    in_quotes = !in_quotes;
                    emit      = FALSE;
                }
            }
        }
        else
        {
            is_space = (ARGUMENT_SPACE == value) || (ARGUMENT_TAB == value);
            at_end   = is_space && (!in_quotes);
            if (at_end)
            {
                break;
            }
        }
        if (emit)
        {
            if (NULL != output)
            {
                output[count] = value;
            }
            ++count;
        }
        ++(*cursor);
    }
    return count;
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
bool_t Utl_Arguments_Read(uint32_t*              unitCount,
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
            is_space = (ARGUMENT_SPACE == input[cursor]) || (ARGUMENT_TAB == input[cursor]);
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
