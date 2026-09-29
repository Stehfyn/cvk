/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define CVK_DIAGNOSTIC_DECIMAL_BASE (10) ///< Radix of a source line in a diagnostic.
#define CVK_DIAGNOSTIC_DIGIT_COUNT  (20) ///< Maximum decimal digits in a uint64_t.
#define CVK_DIAGNOSTIC_SIGN_COUNT   (1)  ///< Optional minus sign.
#define CVK_DIAGNOSTIC_END_COUNT    (1)  ///< String terminator.
#define CVK_DIAGNOSTIC_CAPACITY    (CVK_DIAGNOSTIC_DIGIT_COUNT + CVK_DIAGNOSTIC_SIGN_COUNT + CVK_DIAGNOSTIC_END_COUNT)

#define CVK_DIAGNOSTIC_CHAR_ZERO      ('0')
#define CVK_DIAGNOSTIC_CHAR_MINUS     ('-')
#define CVK_DIAGNOSTIC_CHAR_END       ('\0')
#define CVK_DIAGNOSTIC_CHAR_SEPARATOR (':')
#define CVK_DIAGNOSTIC_CHAR_LF        ('\n')
#define CVK_DIAGNOSTIC_PREFIX         (": cvk fatal: ")

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

typedef int  bool_t; ///< Truth value of a named implementation condition.
typedef char char_t; ///< Character in a terminated execution-character-set string.

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
 * \brief Formats a signed source line in caller-owned storage.
 *
 * \param[out] pDigits   Storage for CVK_DIAGNOSTIC_CAPACITY characters.
 * \param[in]  line      Source line to format.
 *
 * \retval    char_t*    Start of the terminated decimal string inside pDigits.
 */
static CVK_FORCE_INLINE char_t* cvkFormatDiagnosticLine(char_t pDigits[CVK_DIAGNOSTIC_CAPACITY],
                                                      int    line);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Forming the magnitude through -(line + 1) also represents INT_MIN without
 *  signed overflow. Every uint64_t needs at most twenty decimal digits; the
 *  fixed buffer additionally reserves a sign and a terminator.
 */
static CVK_FORCE_INLINE char_t* cvkFormatDiagnosticLine(char_t pDigits[CVK_DIAGNOSTIC_CAPACITY],
                                                      int    line)
{
    char_t*  cursor;
    uint64_t magnitude;
    uint32_t digit;
    bool_t   is_negative;
    bool_t   has_digits;

    is_negative = (0 > line);
    magnitude   = (uint64_t)line;
    cursor      = pDigits + CVK_DIAGNOSTIC_CAPACITY - 1;
    *cursor     = CVK_DIAGNOSTIC_CHAR_END;
    if (is_negative)
    {
        magnitude = (uint64_t)(-(line + 1)) + 1;
    }
    for (;;)
    {
        digit      = (uint32_t)(magnitude % CVK_DIAGNOSTIC_DECIMAL_BASE);
        magnitude  = magnitude / CVK_DIAGNOSTIC_DECIMAL_BASE;
        has_digits = (0 < magnitude);
        --cursor;
        *cursor = (char_t)(CVK_DIAGNOSTIC_CHAR_ZERO + digit);
        if (!has_digits)
        {
            break;
        }
    }
    if (is_negative)
    {
        --cursor;
        *cursor = CVK_DIAGNOSTIC_CHAR_MINUS;
    }
    return cursor;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The diagnostic uses only automatic formatting storage and preserves the
 *  execution character set of __FILE__ and the stringized expression.
 */
CVK_NORETURN void cvkFatal(const char* pExpression,
                           const char* pFile,
                           int         line)
{
    char_t  digits[CVK_DIAGNOSTIC_CAPACITY];
    char_t* text;

    text = cvkFormatDiagnosticLine(digits, line);
    (void)fputs(pFile, stderr);
    (void)fputc(CVK_DIAGNOSTIC_CHAR_SEPARATOR, stderr);
    (void)fputs(text, stderr);
    (void)fputs(CVK_DIAGNOSTIC_PREFIX, stderr);
    (void)fputs(pExpression, stderr);
    (void)fputc(CVK_DIAGNOSTIC_CHAR_LF, stderr);
    abort();
}
