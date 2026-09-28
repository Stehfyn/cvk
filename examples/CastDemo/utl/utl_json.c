/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * RFC 8259 sections 2-10: https://www.rfc-editor.org/rfc/rfc8259.html
 * JSON syntax is independent of application message schemas.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_json.h"
#include "utl/utl_unicode.h"
#include "utl/utl_decimal.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define JSON_QUOTE                  ('"')
#define JSON_ESCAPE                 ('\\')
#define JSON_UNICODE                ('u')
#define JSON_COMMA                  (',')
#define JSON_COLON                  (':')
#define JSON_POINT                  ('.')
#define JSON_MINUS                  ('-')
#define JSON_PLUS                   ('+')
#define JSON_EXPONENT_LOWER         ('e')
#define JSON_EXPONENT_UPPER         ('E')
#define JSON_OBJECT_BEGIN           ('{')
#define JSON_OBJECT_END             ('}')
#define JSON_ARRAY_BEGIN            ('[')
#define JSON_ARRAY_END              (']')
#define JSON_DIGIT_ZERO             ('0')
#define JSON_DIGIT_NINE             ('9')
#define JSON_HEX_ALPHA              ('A')
#define JSON_FIRST_UNESCAPED        (0x20)
#define JSON_HEX_BITS               (4)
#define JSON_HEX_MASK               (0x0F)
#define JSON_DECIMAL_BASE           (10)
#define JSON_QUOTE_COUNT            (2)
#define JSON_SHORT_ESCAPE_BYTES     (2)
#define JSON_CONTROL_ESCAPE_BYTES   (6)
#define JSON_UINT64_BYTES           (20)
#define JSON_INT64_BYTES            (JSON_UINT64_BYTES + 1)
#define JSON_INT64_DIGITS           (19) ///< Maximum decimal magnitude width of either signed limit.
#define JSON_INTEGER_EXPONENT_LIMIT ((uint64_t)UINT32_MAX + JSON_INT64_DIGITS)
#define JSON_FIXED_BYTES            (JSON_UINT64_BYTES + 1 + UTL_JSON_MAX_FRACTION_DIGITS)
#define JSON_TRUE                   ("true")
#define JSON_FALSE                  ("false")
#define JSON_NULL                   ("null")

#define JSON_SLASH                ('/')
#define JSON_ESCAPE_BACKSPACE     ('b')
#define JSON_ESCAPE_FORMFEED      ('f')
#define JSON_ESCAPE_NEWLINE       ('n')
#define JSON_ESCAPE_RETURN        ('r')
#define JSON_ESCAPE_TAB           ('t')
#define JSON_SPACE                (0x20)
#define JSON_TAB                  (0x09)
#define JSON_NEWLINE              (0x0A)
#define JSON_RETURN               (0x0D)
#define JSON_BACKSPACE            (0x08)
#define JSON_FORMFEED             (0x0C)
#define JSON_HEX_UPPER_LAST       ('F')
#define JSON_HEX_LOWER_FIRST      ('a')
#define JSON_HEX_LOWER_LAST       ('f')
#define JSON_INVALID_HEX          (16)
#define JSON_HEX_LETTER_OFFSET    (10)
#define JSON_ESCAPE_HEX_DIGITS    (4)
#define JSON_SURROGATE_HIGH_FIRST (0xD800)
#define JSON_SURROGATE_HIGH_LAST  (0xDBFF)
#define JSON_SURROGATE_LOW_FIRST  (0xDC00)
#define JSON_SURROGATE_LOW_LAST   (0xDFFF)
#define JSON_SURROGATE_PAIR_BITS  (10)
#define JSON_SUPPLEMENTARY_FIRST  (0x10000)
#define JSON_UTF8_BOM_FIRST       (0xEF)
#define JSON_UTF8_BOM_SECOND      (0xBB)
#define JSON_UTF8_BOM_THIRD       (0xBF)
#define JSON_UTF8_BOM_BYTES       (3)
#define JSON_LITERAL_TRUE_FIRST   ('t')
#define JSON_LITERAL_FALSE_FIRST  ('f')
#define JSON_LITERAL_NULL_FIRST   ('n')

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Local counted-byte cursor shared by JSON grammar and Unicode decoding.
 */
typedef struct JsonCursor_s
{
    Utl_Json_Text_t text;   ///< Complete borrowed input byte span.
    uint32_t        offset; ///< Consumed extent, never greater than byteCount.
} JsonCursor_t;

/*************************************************************************************************/
/** \brief Container grammar state; returning from any child requires a separator. */
typedef enum JsonReadPhase_e
{
    JSON_READ_FIRST,
    JSON_READ_VALUE,
    JSON_READ_SEPARATOR
} JsonReadPhase_t;

/*************************************************************************************************/
/** \brief Transient iterative parser; its parent chain lives in caller-owned value records. */
typedef struct JsonParser_s
{
    Utl_Json_Reader_t*    reader;  ///< Destination lifetime and record capacity.
    JsonCursor_t          cursor;  ///< Counted input with a bounded byte offset.
    uint32_t              parent;  ///< Current open container, or NO_VALUE.
    JsonReadPhase_t       phase;   ///< First child, required child or following separator.
    bool_t                valid;   ///< FALSE latches failure for this read.
    Utl_Json_ReadStatus_t failure; ///< Syntax rejection or exhausted records.
} JsonParser_t;

/*************************************************************************************************/
/**
 * \brief Bounded summary of an exact decimal number for integer-domain conversion.
 *
 * \par Details:
 *  The first nineteen significant digits fit in uint64_t. Later digits are
 *  counted, including their trailing-zero run. This preserves enough information
 *  to accept an exact int64_t after decimal-point and exponent adjustment.
 */
typedef struct JsonInteger_s
{
    bool_t   negative;          ///< Original number sign; negative zero remains representable.
    uint64_t prefix;            ///< At most nineteen leading significant decimal digits.
    uint32_t prefixDigits;      ///< Digits represented by prefix.
    uint32_t significantDigits; ///< Mantissa digits after leading zeros, including trailing zeros.
    uint32_t fractionDigits;    ///< All digits following the decimal point.
    uint32_t trailingZeros;     ///< Trailing zero run within significantDigits.
    uint64_t exponent;          ///< Bounded absolute decimal exponent.
    bool_t   negativeExponent;  ///< Exponent sign.
    bool_t   exponentTooLarge;  ///< Nonzero values cannot fit the target domain even after cancellation.
} JsonInteger_t;

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
 * \brief Derives a candidate integer magnitude after exact decimal-scale validation.
 *
 * \par Requirements:
 *  - 001 Required pointers shall address a validated number summary and disjoint output.
 *  - 002 Nonzero digits discarded by a negative decimal shift shall cause rejection.
 *  - 003 Results shall contain at most nineteen digits; scaling shall require at most nineteen iterations.
 *
 * \param[out] magnitude   Candidate unsigned magnitude, unchanged on rejection
 * \param[in]  number      Stable exact summary
 *
 * \retval     TRUE    Zero or an integral candidate with at most nineteen digits.
 * \retval     FALSE   Fractional value, excessive exponent or excessive width.
 */
static FORCE_INLINE bool_t GetIntegerMagnitude(uint64_t* magnitude, const JsonInteger_t* number);

/*************************************************************************************************/
/**
 * \brief Appends one validated mantissa digit to a bounded integer summary.
 *
 * \par Requirements:
 *  - 001 The summary shall be initialized and digit shall belong to zero through nine.
 *  - 002 At most nineteen significant digits shall be accumulated; remaining digits shall only update counts.
 *  - 003 The total number of admitted digits shall be bounded by the uint32_t source extent.
 *
 * \param[in,out] number   Initialized summary
 * \param[in]     digit    Numeric digit, not an ASCII character
 *
 * \retval        None   Prefix and exact digit counts are updated without overflow.
 */
static FORCE_INLINE void AppendIntegerDigit(JsonInteger_t* number, uint32_t digit);

/*************************************************************************************************/
/**
 * \brief Summarizes a validated exponent without an unbounded integer accumulator.
 *
 * \par Requirements:
 *  - 001 The cursor shall point at the first byte after an exponent marker in valid JSON number syntax.
 *  - 002 Exponent overflow shall latch without stopping the counted scan or overflowing arithmetic.
 *  - 003 Bounds shall allow cancellation against every possible uint32_t fraction extent.
 *
 * \param[in,out] number   Initialized summary
 * \param[in,out] cursor   Remaining validated exponent bytes
 *
 * \retval        None   Exponent sign, magnitude and overflow status are retained.
 */
static FORCE_INLINE void ReadIntegerExponent(JsonInteger_t* number, JsonCursor_t* cursor);

/*************************************************************************************************/
/**
 * \brief Summarizes a previously validated JSON numeric lexeme for exact integer conversion.
 *
 * \par Requirements:
 *  - 001 Source shall be a nonempty numeric lexeme borrowed from a complete parsed document.
 *  - 002 Every summary field shall be initialized, including fields for an absent exponent.
 *  - 003 Every byte shall be inspected at most once; no source copy or allocation shall occur.
 *
 * \param[in] text   Validated counted JSON number, without surrounding whitespace
 *
 * \retval    number   Complete bounded summary; its source is not retained.
 */
static FORCE_INLINE JsonInteger_t ReadIntegerParts(Utl_Json_Text_t text);

/*************************************************************************************************/
/**
 * \brief Reads an optional JSON exponent, including its optional sign.
 *
 * \par Requirements:
 *  - 001 The caller shall supply a cursor within valid counted storage.
 *  - 002 An exponent marker shall require at least one following digit.
 *  - 003 A non-exponent suffix shall remain unconsumed for the caller to reject.
 *
 * \param[in,out] cursor   Number grammar cursor
 *
 * \retval        TRUE    Exponent is absent or lexically complete.
 * \retval        FALSE   An exponent marker lacks its required digits.
 */
static FORCE_INLINE bool_t ReadExponent(JsonCursor_t* cursor);

/*************************************************************************************************/
/**
 * \brief Derives an uppercase hexadecimal digit in its asserted domain.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in] value   A nibble
 *
 * \retval digit   ASCII hexadecimal digit.
 */
static FORCE_INLINE byte_t GetHexDigit(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Consumes a required nonempty decimal digit sequence.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in,out] cursor   Bounded number cursor
 *
 * \retval TRUE    One or more digits consumed.
 * \retval FALSE   No digits present.
 */
static FORCE_INLINE bool_t ReadDigits(JsonCursor_t* cursor);

/*************************************************************************************************/
/**
 * \brief Preflights a complete operation before output or state mutation.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in,out] writer   Ready writer
 * \param[in]     bytes    Complete operation extent
 *
 * \retval TRUE    All bytes fit.
 * \retval FALSE   Capacity failure latched.
 */
static FORCE_INLINE bool_t Reserve(Utl_Json_Writer_t* writer, uint64_t bytes);

/*************************************************************************************************/
/**
 * \brief Copies preflighted bytes or advances the counting pass.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in,out] writer   Preflighted writer
 * \param[in]     text     Disjoint counted source
 *
 * \retval None   Byte count advances.
 */
static FORCE_INLINE void EmitBytes(Utl_Json_Writer_t* writer, Utl_Json_Text_t text);

/*************************************************************************************************/
/**
 * \brief Emits one already reserved syntax byte.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in,out] writer   Preflighted writer
 * \param[in]     value    Syntax byte
 *
 * \retval None   Byte count advances by one.
 */
static FORCE_INLINE void EmitByte(Utl_Json_Writer_t* writer, byte_t value);

/*************************************************************************************************/
/**
 * \brief Emits preflighted validated UTF-8 as a quoted JSON string.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in,out] writer   Preflighted writer
 * \param[in]     text     Valid disjoint UTF-8
 *
 * \retval None   Complete string emitted.
 */
static FORCE_INLINE void EmitString(Utl_Json_Writer_t* writer, Utl_Json_Text_t text);

/*************************************************************************************************/
/**
 * \brief Checks root/member/element grammar and full capacity.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     bytes    Encoded value bytes
 *
 * \retval TRUE    Value and optional comma fit.
 * \retval FALSE   Latched grammar or capacity error.
 */
static FORCE_INLINE bool_t PrepareValue(Utl_Json_Writer_t* writer, uint32_t bytes);

/*************************************************************************************************/
/**
 * \brief Emits a preflighted array element separator when needed.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in,out] writer   Preflighted value position
 *
 * \retval None   Optional comma emitted.
 */
static FORCE_INLINE void EmitPrefix(Utl_Json_Writer_t* writer);

/*************************************************************************************************/
/**
 * \brief Commits one parent value after all its bytes are written.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in,out] writer   Preflighted value position
 *
 * \retval None   Parent grammar advances.
 */
static FORCE_INLINE void CompleteValue(Utl_Json_Writer_t* writer);

/*************************************************************************************************/
/**
 * \brief Emits a validated number or reserved literal.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     text     Already validated complete literal
 *
 * \retval TRUE    Complete value emitted.
 * \retval FALSE   Latched writer error.
 */
static FORCE_INLINE bool_t WriteLiteral(Utl_Json_Writer_t* writer, Utl_Json_Text_t text);

/*************************************************************************************************/
/**
 * \brief Preflights and opens one structured value.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     kind     Required container grammar
 *
 * \retval TRUE    Container opened.
 * \retval FALSE   Latched grammar, capacity or depth error.
 */
static FORCE_INLINE bool_t BeginContainer(Utl_Json_Writer_t* writer, Utl_Json_Container_t kind);

/*************************************************************************************************/
/**
 * \brief Closes only a complete matching container.
 *
 * \par Requirements:
 *  - 001 Input storage and writer invariants shall be established by the calling stage.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     kind     Required matching kind
 *
 * \retval TRUE    Container closed.
 * \retval FALSE   Latched grammar or capacity error.
 */
static FORCE_INLINE bool_t EndContainer(Utl_Json_Writer_t* writer, Utl_Json_Container_t kind);

/*************************************************************************************************/
/**
 * \brief Recognizes only the four JSON whitespace terminals.
 *
 * \par Requirements:
 *  - 001 Only the four RFC 8259 whitespace octets shall compare true.
 *  - 002 Classification shall not depend on locale or native character width.
 *
 * \param[in] value   One input byte
 *
 * \retval TRUE    A JSON whitespace byte.
 * \retval FALSE   Any other byte.
 */
static FORCE_INLINE bool_t IsJsonSpace(byte_t value);

/*************************************************************************************************/
/**
 * \brief Derives one hexadecimal digit or an out-of-domain sentinel.
 *
 * \par Requirements:
 *  - 001 ASCII decimal and both ASCII hexadecimal letter ranges shall map to 0 through 15.
 *  - 002 Every other octet shall yield the invalid-digit sentinel.
 *
 * \param[in] value   One ASCII candidate
 *
 * \retval 0..15              Decoded digit.
 * \retval JSON_INVALID_HEX   Not hexadecimal.
 */
static FORCE_INLINE uint32_t GetHexValue(byte_t value);

/*************************************************************************************************/
/**
 * \brief Decodes the four hexadecimal digits of a JSON Unicode escape.
 *
 * \par Requirements:
 *  - 001 The cursor shall identify a backslash-u prefix within stable counted input.
 *  - 002 Exactly four hexadecimal digits shall be required; surrogate code units shall remain representable.
 *  - 003 The cursor shall remain unchanged and failure shall preserve the output.
 *
 * \param[in]  cursor   Valid candidate escape cursor
 * \param[out] value    Required disjoint code-unit output
 *
 * \retval TRUE    One complete escaped code unit was decoded.
 * \retval FALSE   Missing or malformed hexadecimal digits.
 */
static FORCE_INLINE bool_t ReadHexEscape(const JsonCursor_t* cursor, uint32_t* value);

/*************************************************************************************************/
/**
 * \brief Reads one escaped JSON character or UTF-16 code unit.
 *
 * \par Requirements:
 *  - 001 The cursor shall identify a backslash within stable counted input.
 *  - 002 Only the eight short escapes and backslash-u followed by four hexadecimal digits shall be accepted.
 *  - 003 Cursor and output shall change only after complete escape validation.
 *
 * \param[in,out] cursor   Valid candidate escape cursor
 * \param[out]    value    Required disjoint character or code-unit output
 *
 * \retval TRUE    One complete escape was consumed.
 * \retval FALSE   Missing or malformed escape syntax.
 */
static FORCE_INLINE bool_t ReadJsonEscape(JsonCursor_t* cursor, uint32_t* value);

/*************************************************************************************************/
/**
 * \brief Consumes one raw string character or escape, retaining escaped UTF-16 code units.
 *
 * \par Requirements:
 *  - 001 The cursor shall reference stable counted input with offset no greater than its extent.
 *  - 002 One legal raw scalar or escaped code unit shall be consumed; raw controls, quotes and malformed escapes shall fail.
 *  - 003 Failure shall preserve cursor and output; escaped UTF-16 code units shall remain representable.
 *
 * \param[in,out] cursor   Counted string body or document cursor
 * \param[out]    scalar   Decoded scalar or escaped UTF-16 code unit
 *
 * \retval TRUE    One character or escape consumed.
 * \retval FALSE   Truncated or malformed character.
 */
static FORCE_INLINE bool_t ReadJsonCharacter(JsonCursor_t* cursor, uint32_t* scalar);

/*************************************************************************************************/
/**
 * \brief Consumes one Unicode scalar from a raw JSON string body.
 *
 * \par Requirements:
 *  - 001 The cursor and scalar output shall be valid, disjoint and exclusively borrowed.
 *  - 002 Adjacent escaped surrogate pairs shall combine; unpaired surrogates shall fail.
 *  - 003 Failure shall preserve both the caller cursor and scalar output.
 *
 * \param[in,out] cursor   Raw JSON string-body cursor
 * \param[out]    scalar   Unicode scalar on success
 *
 * \retval TRUE    One representable scalar consumed.
 * \retval FALSE   Invalid or unpaired encoding; outputs preserved.
 */
static FORCE_INLINE bool_t ReadJsonScalar(JsonCursor_t* cursor, uint32_t* scalar);

/*************************************************************************************************/
/**
 * \brief Compares a JSON string body with counted decoded UTF-8 without temporary storage.
 *
 * \par Requirements:
 *  - 001 Nonempty spans shall have readable immutable storage.
 *  - 002 Comparison shall decode escapes and require exact scalar equality over both complete extents.
 *  - 003 Invalid UTF-8 or unpaired escaped surrogate code units shall compare unequal.
 *
 * \param[in] encoded    Raw JSON string body
 * \param[in] expected   Decoded UTF-8 text
 *
 * \retval TRUE    Equal valid Unicode scalar sequences.
 * \retval FALSE   Different sequences or invalid scalar encoding.
 */
static FORCE_INLINE bool_t IsDecodedEqual(Utl_Json_Text_t encoded, Utl_Json_Text_t expected);

/*************************************************************************************************/
/**
 * \brief Advances past JSON whitespace within a counted source.
 *
 * \par Requirements:
 *  - 001 The cursor shall own a valid bounded input offset.
 *  - 002 Only consecutive JSON whitespace shall be consumed; no byte beyond the counted end shall be accessed.
 *
 * \param[in,out] cursor   Valid counted source cursor
 *
 * \retval None   Offset advances without passing the source end.
 */
static FORCE_INLINE void SkipJsonSpace(JsonCursor_t* cursor);

/*************************************************************************************************/
/**
 * \brief Reads a complete quoted JSON string while preserving its raw body.
 *
 * \par Requirements:
 *  - 001 The cursor shall begin at an opening JSON quotation mark or fail normally.
 *  - 002 All raw UTF-8 and escape grammar shall be validated before the output span is published.
 *  - 003 The result shall borrow the raw body without surrounding quotes; a failed cursor may have advanced.
 *
 * \param[in,out] cursor   Input positioned at a possible opening quote
 * \param[out]    text     Raw body span on success
 *
 * \retval TRUE    Both quotes and a valid body were consumed.
 * \retval FALSE   Malformed or truncated string; body output preserved.
 */
static FORCE_INLINE bool_t ReadJsonStringBody(JsonCursor_t* cursor, Utl_Json_Text_t* text);

/*************************************************************************************************/
/**
 * \brief Consumes an exact JSON literal within its counted source.
 *
 * \par Requirements:
 *  - 001 The cursor and literal shall describe live bounded storage.
 *  - 002 An exact remaining-byte prefix shall advance the cursor; rejection shall preserve its offset.
 *
 * \param[in,out] cursor    Input positioned at a literal candidate
 * \param[in]     literal   Required ASCII spelling
 *
 * \retval TRUE    Exact literal consumed.
 * \retval FALSE   Mismatch or truncation; cursor preserved.
 */
static FORCE_INLINE bool_t ReadJsonLiteral(JsonCursor_t* cursor, Utl_Json_Text_t literal);

/*************************************************************************************************/
/**
 * \brief Reads one complete number lexeme without conversion or magnitude limits.
 *
 * \par Requirements:
 *  - 001 The cursor shall identify a readable candidate within its input extent.
 *  - 002 The complete candidate shall match JSON number grammar without restricting numeric magnitude.
 *  - 003 Only success shall publish the borrowed number span; rejected cursor progress may remain.
 *
 * \param[in,out] cursor   Input positioned at a numeric candidate
 * \param[out]    text     Exact validated number lexeme
 *
 * \retval TRUE    Number grammar accepted.
 * \retval FALSE   Invalid number; text output preserved.
 */
static FORCE_INLINE bool_t ReadJsonNumber(JsonCursor_t* cursor, Utl_Json_Text_t* text);

/*************************************************************************************************/
/**
 * \brief Publishes one validated record and updates the container traversal state.
 *
 * \par Requirements:
 *  - 001 The next record at reader valueCount shall be initialized after capacity and syntax validation.
 *  - 002 Existing parent and sibling links shall identify earlier records owned by the same reader.
 *  - 003 Each child shall be linked once; a container shall become the next active parent.
 *
 * \param[in,out] parser      Exclusive valid parser state
 * \param[in]     container   Whether the record opens a new object or array
 *
 * \retval None   The record is published and the next grammar phase is established.
 */
static FORCE_INLINE void AppendJsonValue(JsonParser_t* parser, bool_t container);

/*************************************************************************************************/
/**
 * \brief Appends one value and enters its container without recursion.
 *
 * \par Requirements:
 *  - 001 The parser shall retain exclusive reader storage and valid parent links.
 *  - 002 A record shall be linked only after its primitive syntax or container opener is validated.
 *  - 003 Missing input or record capacity shall latch failure without accessing unavailable storage.
 *
 * \param[in,out] parser   Valid parser at the next value
 * \param[in]     name     Raw object-member name, or an absent span
 *
 * \retval None   One value is appended or failure is latched.
 */
static FORCE_INLINE void ReadJsonValue(JsonParser_t* parser, Utl_Json_Text_t name);

/*************************************************************************************************/
/**
 * \brief Reads a required array value or object name/colon/value sequence.
 *
 * \par Requirements:
 *  - 001 The parser parent shall refer to an admitted open object or array.
 *  - 002 Objects shall require a quoted member name and colon before a value.
 *  - 003 Failure shall latch in the parser and shall prevent child publication.
 *
 * \param[in,out] parser   Valid parser with an open parent
 *
 * \retval None   One child is entered or failure is latched.
 */
static FORCE_INLINE void ReadJsonChild(JsonParser_t* parser);

/*************************************************************************************************/
/**
 * \brief Advances exactly one container grammar stage.
 *
 * \par Requirements:
 *  - 001 The parser parent and phase shall describe an admitted open container.
 *  - 002 Closing delimiters shall match the parent and shall be rejected immediately after a comma.
 *  - 003 Each successful step shall consume input, append a child or return to the parent without C recursion.
 *
 * \param[in,out] parser   Open-container parser after whitespace
 *
 * \retval None   Consumes grammar input, closes a container or rejects input.
 */
static FORCE_INLINE void ReadJsonContainerStep(JsonParser_t* parser);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  A negative decimal shift may discard only zero digits. Once the final width
 *  is at most nineteen, adjusting the retained prefix cannot overflow uint64_t.
 *  Exponents beyond the summary bound cannot cancel a uint32_t fraction extent.
 */
static FORCE_INLINE bool_t GetIntegerMagnitude(uint64_t* magnitude, const JsonInteger_t* number)
{
    uint64_t candidate;
    bool_t   is_zero;
    bool_t   valid;
    int64_t  exponent;
    int64_t  shift;
    int64_t  digits;
    bool_t   has_digits;
    bool_t   fits_digits;
    bool_t   removes_digits;
    bool_t   integral;
    bool_t   bounded_exponent;
    int64_t  adjustment;

    candidate = 0;
    is_zero   = (0 == number->significantDigits);
    valid     = is_zero;
    if (!is_zero)
    {
        exponent = (int64_t)number->exponent;
        if (number->negativeExponent)
        {
            exponent = -exponent;
        }
        shift          = exponent - (int64_t)number->fractionDigits;
        digits         = (int64_t)number->significantDigits + shift;
        has_digits     = (0 < digits);
        fits_digits    = (digits <= JSON_INT64_DIGITS);
        removes_digits = (0 > shift);
        integral       = TRUE;
        if (removes_digits)
        {
            integral = ((uint64_t)(-shift) <= number->trailingZeros);
        }
        bounded_exponent = !number->exponentTooLarge;
        valid            = has_digits && fits_digits && integral && bounded_exponent;
        if (valid)
        {
            candidate  = number->prefix;
            adjustment = digits - (int64_t)number->prefixDigits;
            for (;;)
            {
                if (0 == adjustment)
                {
                    break;
                }
                if (0 < adjustment)
                {
                    candidate *= JSON_DECIMAL_BASE;
                    --adjustment;
                }
                else
                {
                    candidate /= JSON_DECIMAL_BASE;
                    ++adjustment;
                }
            }
        }
    }
    if (valid)
    {
        *magnitude = candidate;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The retained nineteen-digit prefix never exceeds 9999999999999999999.
 *  Omitted digits still update the exact significant width and trailing-zero run.
 */
static FORCE_INLINE void AppendIntegerDigit(JsonInteger_t* number, uint32_t digit)
{
    bool_t is_zero;
    bool_t has_significant;
    bool_t is_significant;
    bool_t has_prefix_capacity;

    is_zero         = (0 == digit);
    has_significant = (0 < number->significantDigits);
    is_significant  = has_significant || !is_zero;
    if (is_significant)
    {
        ++number->significantDigits;
        has_prefix_capacity = (number->prefixDigits < JSON_INT64_DIGITS);
        if (has_prefix_capacity)
        {
            number->prefix = (number->prefix * JSON_DECIMAL_BASE) + digit;
            ++number->prefixDigits;
        }
        if (is_zero)
        {
            ++number->trailingZeros;
        }
        else
        {
            number->trailingZeros = 0;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The exponent is clamped only in the internal summary, with a separate overflow
 *  flag. External values are never saturated into an accepted integer result.
 */
static FORCE_INLINE void ReadIntegerExponent(JsonInteger_t* number, JsonCursor_t* cursor)
{
    byte_t   first;
    bool_t   is_minus;
    bool_t   is_plus;
    bool_t   has_sign;
    uint32_t digit;
    bool_t   fits_exponent;

    first                    = cursor->text.data[cursor->offset];
    is_minus                 = (JSON_MINUS == first);
    is_plus                  = (JSON_PLUS == first);
    has_sign                 = is_minus || is_plus;
    number->negativeExponent = is_minus;
    if (has_sign)
    {
        ++cursor->offset;
    }
    for (;;)
    {
        if (cursor->offset == cursor->text.byteCount)
        {
            break;
        }
        digit = (uint32_t)cursor->text.data[cursor->offset] - JSON_DIGIT_ZERO;
        if (!number->exponentTooLarge)
        {
            fits_exponent = (number->exponent <= (JSON_INTEGER_EXPONENT_LIMIT - digit) / JSON_DECIMAL_BASE);
            if (fits_exponent)
            {
                number->exponent = (number->exponent * JSON_DECIMAL_BASE) + digit;
            }
            else
            {
                number->exponentTooLarge = TRUE;
            }
        }
        ++cursor->offset;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Syntax has already been accepted by the document reader. Sign, decimal point
 *  and exponent markers are classified without reinterpreting malformed input.
 */
static FORCE_INLINE JsonInteger_t ReadIntegerParts(Utl_Json_Text_t text)
{
    JsonInteger_t number;
    JsonCursor_t  cursor;
    bool_t        fractional;
    byte_t        current;
    bool_t        is_point;
    bool_t        is_lower_exponent;
    bool_t        is_upper_exponent;
    bool_t        is_exponent;
    uint32_t      digit;

    number.negative          = (JSON_MINUS == text.data[0]);
    number.prefix            = 0;
    number.prefixDigits      = 0;
    number.significantDigits = 0;
    number.fractionDigits    = 0;
    number.trailingZeros     = 0;
    number.exponent          = 0;
    number.negativeExponent  = FALSE;
    number.exponentTooLarge  = FALSE;
    cursor.text              = text;
    cursor.offset            = number.negative ? 1 : 0;
    fractional               = FALSE;
    for (;;)
    {
        if (cursor.offset == text.byteCount)
        {
            break;
        }
        current           = text.data[cursor.offset];
        is_point          = (JSON_POINT == current);
        is_lower_exponent = (JSON_EXPONENT_LOWER == current);
        is_upper_exponent = (JSON_EXPONENT_UPPER == current);
        is_exponent       = is_lower_exponent || is_upper_exponent;
        ++cursor.offset;
        if (is_exponent)
        {
            ReadIntegerExponent(&number, &cursor);
            break;
        }
        if (is_point)
        {
            fractional = TRUE;
        }
        else
        {
            digit = (uint32_t)current - JSON_DIGIT_ZERO;
            AppendIntegerDigit(&number, digit);
            if (fractional)
            {
                ++number.fractionDigits;
            }
        }
    }
    return number;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE byte_t GetHexDigit(uint32_t value)
{
    uint32_t digit;
    LOGASSERT(value <= JSON_HEX_MASK);
    digit = JSON_DIGIT_ZERO + value;
    if (JSON_DECIMAL_BASE <= value)
    {
        digit = JSON_HEX_ALPHA + value - JSON_DECIMAL_BASE;
    }
    return (byte_t)digit;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadDigits(JsonCursor_t* cursor)
{
    uint32_t begin;
    byte_t   value;
    bool_t   at_least_zero;
    bool_t   at_most_nine;
    bool_t   digit;
    begin = cursor->offset;
    for (; cursor->offset < cursor->text.byteCount; ++cursor->offset)
    {
        value         = cursor->text.data[cursor->offset];
        at_least_zero = (JSON_DIGIT_ZERO <= value);
        at_most_nine  = (value <= JSON_DIGIT_NINE);
        digit         = at_least_zero && at_most_nine;
        if (!digit)
        {
            break;
        }
    }
    return begin < cursor->offset;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  RFC 8259 exponent: an e/E marker, optional sign, and one or more decimal digits.
 */
static FORCE_INLINE bool_t ReadExponent(JsonCursor_t* cursor)
{
    bool_t valid;
    bool_t has_remaining;
    byte_t current;
    bool_t is_lower;
    bool_t is_upper;
    bool_t has_exponent;
    bool_t is_plus;
    bool_t is_minus;
    bool_t has_sign;

    valid         = TRUE;
    has_remaining = (cursor->offset < cursor->text.byteCount);
    if (has_remaining)
    {
        current      = cursor->text.data[cursor->offset];
        is_lower     = (JSON_EXPONENT_LOWER == current);
        is_upper     = (JSON_EXPONENT_UPPER == current);
        has_exponent = is_lower || is_upper;
        if (has_exponent)
        {
            ++cursor->offset;
            has_remaining = (cursor->offset < cursor->text.byteCount);
            if (has_remaining)
            {
                current  = cursor->text.data[cursor->offset];
                is_plus  = (JSON_PLUS == current);
                is_minus = (JSON_MINUS == current);
                has_sign = is_plus || is_minus;
                if (has_sign)
                {
                    ++cursor->offset;
                }
            }
            valid = ReadDigits(cursor);
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t Reserve(Utl_Json_Writer_t* writer, uint64_t bytes)
{
    bool_t fits;
    LOGASSERT(writer->used <= writer->capacity);
    fits = (bytes <= writer->capacity - writer->used);
    if (!fits)
    {
        writer->status = UTL_JSON_OUTPUT_EXHAUSTED;
    }
    return fits;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void EmitBytes(Utl_Json_Writer_t* writer, Utl_Json_Text_t text)
{
    bool_t has_output;
    bool_t has_text;
    bool_t can_copy;

    LOGASSERT(text.byteCount <= writer->capacity - writer->used);
    has_output = (NULL != writer->buffer);
    has_text   = (0 < text.byteCount);
    can_copy   = has_output && has_text;
    if (can_copy)
    {
        CRT_Memory_Copy(&writer->buffer[writer->used], text.data, text.byteCount);
    }
    writer->used += text.byteCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void EmitByte(Utl_Json_Writer_t* writer, byte_t value)
{
    Utl_Json_Text_t text;
    text.data      = &value;
    text.byteCount = 1;
    EmitBytes(writer, text);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void EmitString(Utl_Json_Writer_t* writer, Utl_Json_Text_t text)
{
    uint32_t index;
    byte_t   current;
    bool_t   is_quote;
    bool_t   is_escape;
    bool_t   short_escape;
    bool_t   control;
    EmitByte(writer, JSON_QUOTE);
    for (index = 0; index < text.byteCount; ++index)
    {
        current      = text.data[index];
        is_quote     = (JSON_QUOTE == current);
        is_escape    = (JSON_ESCAPE == current);
        short_escape = is_quote || is_escape;
        control      = (current < JSON_FIRST_UNESCAPED);
        if (short_escape)
        {
            EmitByte(writer, JSON_ESCAPE);
            EmitByte(writer, current);
        }
        else if (control)
        {
            EmitByte(writer, JSON_ESCAPE);
            EmitByte(writer, JSON_UNICODE);
            EmitByte(writer, JSON_DIGIT_ZERO);
            EmitByte(writer, JSON_DIGIT_ZERO);
            EmitByte(writer, GetHexDigit(current >> JSON_HEX_BITS));
            EmitByte(writer, GetHexDigit(current & JSON_HEX_MASK));
        }
        else
        {
            EmitByte(writer, current);
        }
    }
    EmitByte(writer, JSON_QUOTE);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t PrepareValue(Utl_Json_Writer_t* writer, uint32_t bytes)
{
    bool_t            ready;
    bool_t            accepted;
    bool_t            valid_state;
    uint32_t          prefix;
    Utl_Json_Frame_t* frame;
    bool_t            is_array;
    bool_t            needs_comma;
    ready    = (UTL_JSON_READY == writer->status);
    accepted = FALSE;
    if (ready)
    {
        valid_state = !writer->rootWritten;
        prefix      = 0;
        if (0 < writer->depth)
        {
            frame       = &writer->frames[writer->depth - 1];
            is_array    = (UTL_JSON_ARRAY == frame->kind);
            valid_state = is_array || frame->needsValue;
            needs_comma = is_array && frame->hasValue;
            if (needs_comma)
            {
                prefix = 1;
            }
        }
        if (!valid_state)
        {
            writer->status = UTL_JSON_INVALID_STATE;
        }
        else
        {
            accepted = Reserve(writer, (uint64_t)bytes + prefix);
        }
    }
    else if (UTL_JSON_COMPLETE == writer->status)
    {
        writer->status = UTL_JSON_INVALID_STATE;
    }
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void EmitPrefix(Utl_Json_Writer_t* writer)
{
    const Utl_Json_Frame_t* frame;
    bool_t                  is_array;
    bool_t                  comma;
    if (0 < writer->depth)
    {
        frame    = &writer->frames[writer->depth - 1];
        is_array = (UTL_JSON_ARRAY == frame->kind);
        comma    = is_array && frame->hasValue;
        if (comma)
        {
            EmitByte(writer, JSON_COMMA);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CompleteValue(Utl_Json_Writer_t* writer)
{
    Utl_Json_Frame_t* frame;
    if (0 < writer->depth)
    {
        frame             = &writer->frames[writer->depth - 1];
        frame->hasValue   = TRUE;
        frame->needsValue = FALSE;
    }
    else
    {
        writer->rootWritten = TRUE;
        writer->status      = UTL_JSON_COMPLETE;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t WriteLiteral(Utl_Json_Writer_t* writer, Utl_Json_Text_t text)
{
    bool_t accepted;
    accepted = PrepareValue(writer, text.byteCount);
    if (accepted)
    {
        EmitPrefix(writer);
        EmitBytes(writer, text);
        CompleteValue(writer);
    }
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t BeginContainer(Utl_Json_Writer_t* writer, Utl_Json_Container_t kind)
{
    bool_t            accepted;
    bool_t            has_frame;
    bool_t            ready;
    bool_t            depth_exhausted;
    byte_t            delimiter;
    Utl_Json_Frame_t* frame;
    accepted        = FALSE;
    has_frame       = (writer->depth < writer->frameCapacity);
    ready           = (UTL_JSON_READY == writer->status);
    depth_exhausted = ready && !has_frame;
    if (depth_exhausted)
    {
        writer->status = UTL_JSON_DEPTH_EXHAUSTED;
    }
    else
    {
        accepted = PrepareValue(writer, 1);
    }
    if (accepted)
    {
        delimiter = (UTL_JSON_OBJECT == kind) ? JSON_OBJECT_BEGIN : JSON_ARRAY_BEGIN;
        EmitPrefix(writer);
        EmitByte(writer, delimiter);
        CompleteValue(writer);
        frame             = &writer->frames[writer->depth];
        frame->kind       = kind;
        frame->hasValue   = FALSE;
        frame->needsValue = FALSE;
        ++writer->depth;
        writer->status = UTL_JSON_READY;
    }
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t EndContainer(Utl_Json_Writer_t* writer, Utl_Json_Container_t kind)
{
    bool_t                  accepted;
    bool_t                  valid_state;
    const Utl_Json_Frame_t* frame;
    bool_t                  kind_matches;
    byte_t                  delimiter;
    accepted = FALSE;
    if (UTL_JSON_READY == writer->status)
    {
        valid_state = (0 < writer->depth);
        if (valid_state)
        {
            frame        = &writer->frames[writer->depth - 1];
            kind_matches = (kind == frame->kind);
            valid_state  = kind_matches && !frame->needsValue;
        }
        if (!valid_state)
        {
            writer->status = UTL_JSON_INVALID_STATE;
        }
        else
        {
            accepted = Reserve(writer, 1);
            if (accepted)
            {
                delimiter = (UTL_JSON_OBJECT == kind) ? JSON_OBJECT_END : JSON_ARRAY_END;
                EmitByte(writer, delimiter);
                --writer->depth;
                if (0 == writer->depth)
                {
                    writer->status = UTL_JSON_COMPLETE;
                }
            }
        }
    }
    else if (UTL_JSON_COMPLETE == writer->status)
    {
        writer->status = UTL_JSON_INVALID_STATE;
    }
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsJsonSpace(byte_t value)
{
    bool_t is_space;
    bool_t is_tab;
    bool_t is_line;
    bool_t is_return;
    bool_t whitespace;

    is_space   = (JSON_SPACE == value);
    is_tab     = (JSON_TAB == value);
    is_line    = (JSON_NEWLINE == value);
    is_return  = (JSON_RETURN == value);
    whitespace = is_space || is_tab || is_line || is_return;
    return whitespace;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetHexValue(byte_t value)
{
    bool_t   digit_minimum;
    bool_t   digit_maximum;
    bool_t   is_digit;
    bool_t   upper_minimum;
    bool_t   upper_maximum;
    bool_t   is_upper;
    bool_t   lower_minimum;
    bool_t   lower_maximum;
    bool_t   is_lower;
    uint32_t result;

    digit_minimum = (JSON_DIGIT_ZERO <= value);
    digit_maximum = (value <= JSON_DIGIT_NINE);
    is_digit      = digit_minimum && digit_maximum;
    upper_minimum = (JSON_HEX_ALPHA <= value);
    upper_maximum = (value <= JSON_HEX_UPPER_LAST);
    is_upper      = upper_minimum && upper_maximum;
    lower_minimum = (JSON_HEX_LOWER_FIRST <= value);
    lower_maximum = (value <= JSON_HEX_LOWER_LAST);
    is_lower      = lower_minimum && lower_maximum;
    result        = JSON_INVALID_HEX;
    if (is_digit)
    {
        result = (uint32_t)(value - JSON_DIGIT_ZERO);
    }
    else if (is_upper)
    {
        result = (uint32_t)(value - JSON_HEX_ALPHA) + JSON_HEX_LETTER_OFFSET;
    }
    else if (is_lower)
    {
        result = (uint32_t)(value - JSON_HEX_LOWER_FIRST) + JSON_HEX_LETTER_OFFSET;
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The complete six-byte escape extent is checked before any digit load. The
 *  output is published after the last digit; malformed input remains ordinary failure.
 */
static FORCE_INLINE bool_t ReadHexEscape(const JsonCursor_t* cursor, uint32_t* value)
{
    uint32_t remaining;
    bool_t   valid;
    uint32_t result;
    uint32_t index;
    uint32_t digit;
    bool_t   valid_digit;

    remaining = cursor->text.byteCount - cursor->offset;
    valid     = (JSON_CONTROL_ESCAPE_BYTES <= remaining);
    result    = 0;
    if (valid)
    {
        for (index = 0; index < JSON_ESCAPE_HEX_DIGITS; ++index)
        {
            digit       = GetHexValue(cursor->text.data[cursor->offset + JSON_SHORT_ESCAPE_BYTES + index]);
            valid_digit = (JSON_INVALID_HEX != digit);
            if (!valid_digit)
            {
                valid = FALSE;
                break;
            }
            result = (result << JSON_HEX_BITS) | digit;
        }
    }
    if (valid)
    {
        *value = result;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Escape selection and hexadecimal decoding are separate grammar operations.
 *  Failed reads preserve the caller cursor and output; no partial escape is published.
 */
static FORCE_INLINE bool_t ReadJsonEscape(JsonCursor_t* cursor, uint32_t* value)
{
    uint32_t remaining;
    bool_t   valid;
    uint32_t result;
    uint32_t used;
    byte_t   escaped;

    remaining = cursor->text.byteCount - cursor->offset;
    valid     = (JSON_SHORT_ESCAPE_BYTES <= remaining);
    result    = 0;
    used      = JSON_SHORT_ESCAPE_BYTES;
    if (valid)
    {
        escaped = cursor->text.data[cursor->offset + 1];
        switch (escaped)
        {
        case JSON_QUOTE:
        case JSON_ESCAPE:
        case JSON_SLASH:
            result = escaped;
            break;
        case JSON_ESCAPE_BACKSPACE:
            result = JSON_BACKSPACE;
            break;
        case JSON_ESCAPE_FORMFEED:
            result = JSON_FORMFEED;
            break;
        case JSON_ESCAPE_NEWLINE:
            result = JSON_NEWLINE;
            break;
        case JSON_ESCAPE_RETURN:
            result = JSON_RETURN;
            break;
        case JSON_ESCAPE_TAB:
            result = JSON_TAB;
            break;
        case JSON_UNICODE:
            valid = ReadHexEscape(cursor, &result);
            used  = JSON_CONTROL_ESCAPE_BYTES;
            break;
        default:
            valid = FALSE;
            break;
        }
    }
    if (valid)
    {
        cursor->offset += used;
        *value         = result;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Escaped surrogate code units are legal JSON grammar and remain representable.
 *  Raw UTF-8 still requires Unicode scalar encoding. Cursor and output change only on success.
 */
static FORCE_INLINE bool_t ReadJsonCharacter(JsonCursor_t* cursor, uint32_t* scalar)
{
    uint32_t remaining;
    bool_t   valid;
    byte_t   value;
    bool_t   is_escape;
    bool_t   printable;
    bool_t   is_quote;
    uint32_t result;
    uint32_t used;

    remaining = cursor->text.byteCount - cursor->offset;
    valid     = (0 < remaining);
    if (valid)
    {
        value     = cursor->text.data[cursor->offset];
        is_escape = (JSON_ESCAPE == value);
        if (is_escape)
        {
            valid = ReadJsonEscape(cursor, scalar);
        }
        else
        {
            printable = (JSON_FIRST_UNESCAPED <= value);
            is_quote  = (JSON_QUOTE == value);
            valid     = printable && !is_quote;
            if (valid)
            {
                valid = Utl_Unicode_ReadUtf8(&result, &used, cursor->text.data + cursor->offset, remaining);
                if (valid)
                {
                    cursor->offset += used;
                    *scalar        = result;
                }
            }
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A high escaped surrogate must have an adjacent low escaped surrogate.
 *  Unpaired code units remain legal parser data but cannot be emitted as UTF-8.
 */
static FORCE_INLINE bool_t ReadJsonScalar(JsonCursor_t* cursor, uint32_t* scalar)
{
    JsonCursor_t candidate;
    bool_t       valid;
    uint32_t     first;
    bool_t       high_minimum;
    bool_t       high_maximum;
    bool_t       is_high;
    bool_t       low_minimum;
    bool_t       low_maximum;
    bool_t       is_low;
    uint32_t     second;

    candidate = *cursor;
    valid     = ReadJsonCharacter(&candidate, &first);
    if (valid)
    {
        high_minimum = (JSON_SURROGATE_HIGH_FIRST <= first);
        high_maximum = (first <= JSON_SURROGATE_HIGH_LAST);
        is_high      = high_minimum && high_maximum;
        low_minimum  = (JSON_SURROGATE_LOW_FIRST <= first);
        low_maximum  = (first <= JSON_SURROGATE_LOW_LAST);
        is_low       = low_minimum && low_maximum;
        if (is_high)
        {
            valid = ReadJsonCharacter(&candidate, &second);
            if (valid)
            {
                low_minimum = (JSON_SURROGATE_LOW_FIRST <= second);
                low_maximum = (second <= JSON_SURROGATE_LOW_LAST);
                valid       = low_minimum && low_maximum;
                if (valid)
                {
                    first = JSON_SUPPLEMENTARY_FIRST +
                            ((first - JSON_SURROGATE_HIGH_FIRST) << JSON_SURROGATE_PAIR_BITS) +
                            (second - JSON_SURROGATE_LOW_FIRST);
                }
            }
        }
        else if (is_low)
        {
            valid = FALSE;
        }
    }
    if (valid)
    {
        *cursor = candidate;
        *scalar = first;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Both sides are compared by scalar value; escaping differs without affecting equality.
 *  No normalization or case folding is performed.
 */
static FORCE_INLINE bool_t IsDecodedEqual(Utl_Json_Text_t encoded, Utl_Json_Text_t expected)
{
    JsonCursor_t cursor;
    uint32_t     offset;
    bool_t       has_encoded;
    bool_t       has_expected;
    bool_t       equal;
    bool_t       first_valid;
    uint32_t     first;
    bool_t       second_valid;
    uint32_t     second;
    uint32_t     used;

    cursor.text   = encoded;
    cursor.offset = 0;
    offset        = 0;
    for (;;)
    {
        has_encoded  = (cursor.offset < encoded.byteCount);
        has_expected = (offset < expected.byteCount);
        if (!has_encoded)
        {
            equal = !has_expected;
            break;
        }
        if (!has_expected)
        {
            equal = FALSE;
            break;
        }
        first_valid  = ReadJsonScalar(&cursor, &first);
        second_valid = Utl_Unicode_ReadUtf8(&second, &used, expected.data + offset, expected.byteCount - offset);
        equal        = first_valid && second_valid;
        if (equal)
        {
            equal = (first == second);
        }
        if (!equal)
        {
            break;
        }
        offset += used;
    }
    return equal;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each accepted iteration consumes exactly one byte.
 */
static FORCE_INLINE void SkipJsonSpace(JsonCursor_t* cursor)
{
    byte_t value;
    bool_t whitespace;

    for (;;)
    {
        if (cursor->offset == cursor->text.byteCount)
        {
            break;
        }
        value      = cursor->text.data[cursor->offset];
        whitespace = IsJsonSpace(value);
        if (!whitespace)
        {
            break;
        }
        ++cursor->offset;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All escapes and raw UTF-8 are validated. Escaped unpaired UTF-16 units
 *  are retained as allowed by RFC 8259 section 8.2; decoded UTF-8 queries reject them.
 */
static FORCE_INLINE bool_t ReadJsonStringBody(JsonCursor_t* cursor, Utl_Json_Text_t* text)
{
    bool_t   valid;
    bool_t   closed;
    uint32_t begin;
    byte_t   value;
    uint32_t scalar;

    valid  = (cursor->offset < cursor->text.byteCount);
    closed = FALSE;
    if (valid)
    {
        valid = (JSON_QUOTE == cursor->text.data[cursor->offset]);
    }
    if (valid)
    {
        ++cursor->offset;
        begin = cursor->offset;
        for (;;)
        {
            if (cursor->offset == cursor->text.byteCount)
            {
                break;
            }
            value = cursor->text.data[cursor->offset];
            if (JSON_QUOTE == value)
            {
                text->data      = cursor->text.data + begin;
                text->byteCount = cursor->offset - begin;
                ++cursor->offset;
                closed = TRUE;
                break;
            }
            valid = ReadJsonCharacter(cursor, &scalar);
            if (!valid)
            {
                break;
            }
        }
    }
    return closed;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Following token separation is checked by the containing grammar stage.
 */
static FORCE_INLINE bool_t ReadJsonLiteral(JsonCursor_t* cursor, Utl_Json_Text_t literal)
{
    uint32_t remaining;
    bool_t   valid;

    remaining = cursor->text.byteCount - cursor->offset;
    valid     = (literal.byteCount <= remaining);
    if (valid)
    {
        valid = (0 == CRT_Memory_Compare(cursor->text.data + cursor->offset, literal.data, literal.byteCount));
    }
    if (valid)
    {
        cursor->offset += literal.byteCount;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A token ends only at whitespace or a structural separator. IsNumber
 *  validates the entire candidate, rejecting signs, suffixes and incomplete exponents.
 */
static FORCE_INLINE bool_t ReadJsonNumber(JsonCursor_t* cursor, Utl_Json_Text_t* text)
{
    uint32_t        begin;
    byte_t          value;
    bool_t          space;
    bool_t          comma;
    bool_t          object_end;
    bool_t          array_end;
    bool_t          terminal;
    Utl_Json_Text_t candidate;
    bool_t          valid;

    begin = cursor->offset;
    for (;;)
    {
        if (cursor->offset == cursor->text.byteCount)
        {
            break;
        }
        value      = cursor->text.data[cursor->offset];
        space      = IsJsonSpace(value);
        comma      = (JSON_COMMA == value);
        object_end = (JSON_OBJECT_END == value);
        array_end  = (JSON_ARRAY_END == value);
        terminal   = space || comma || object_end || array_end;
        if (terminal)
        {
            break;
        }
        ++cursor->offset;
    }
    candidate.data      = cursor->text.data + begin;
    candidate.byteCount = cursor->offset - begin;
    valid               = Utl_Json_IsNumber(candidate);
    if (valid)
    {
        *text = candidate;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  This publication step cannot fail: capacity, parent links and the new record
 *  were established before entry. Parent tails make each append constant work.
 */
static FORCE_INLINE void AppendJsonValue(JsonParser_t* parser, bool_t container)
{
    uint32_t          index;
    Utl_Json_Value_t* parent;
    uint32_t          previous;

    index = parser->reader->valueCount;
    ++parser->reader->valueCount;
    if (UTL_JSON_NO_VALUE != parser->parent)
    {
        parent   = &parser->reader->values[parser->parent];
        previous = parent->lastChild;
        if (UTL_JSON_NO_VALUE == previous)
        {
            parent->firstChild = index;
        }
        else
        {
            parser->reader->values[previous].nextSibling = index;
        }
        parent->lastChild = index;
        ++parent->childCount;
    }
    parser->phase = JSON_READ_SEPARATOR;
    if (container)
    {
        parser->parent = index;
        parser->phase  = JSON_READ_FIRST;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The parent tail links each child once. Primitive validation completes
 *  before links are published; container completion is verified by the outer loop.
 */
static FORCE_INLINE void ReadJsonValue(JsonParser_t* parser, Utl_Json_Text_t name)
{
    bool_t            has_input;
    bool_t            has_slot;
    uint32_t          index;
    Utl_Json_Value_t* value;
    byte_t            first;
    bool_t            is_container;
    Utl_Json_Text_t   literal;

    has_input     = (parser->cursor.offset < parser->cursor.text.byteCount);
    has_slot      = (parser->reader->valueCount < parser->reader->capacity);
    parser->valid = has_input && has_slot;
    if (!has_slot)
    {
        parser->failure = UTL_JSON_READ_EXHAUSTED;
    }
    if (parser->valid)
    {
        index                 = parser->reader->valueCount;
        value                 = &parser->reader->values[index];
        value->kind           = UTL_JSON_VALUE_NULL;
        value->text.data      = NULL;
        value->text.byteCount = 0;
        value->name           = name;
        value->childCount     = 0;
        value->firstChild     = UTL_JSON_NO_VALUE;
        value->nextSibling    = UTL_JSON_NO_VALUE;
        value->parent         = parser->parent;
        value->lastChild      = UTL_JSON_NO_VALUE;
        first                 = parser->cursor.text.data[parser->cursor.offset];
        is_container          = FALSE;
        switch (first)
        {
        case JSON_OBJECT_BEGIN:
        case JSON_ARRAY_BEGIN:
            value->kind  = (JSON_OBJECT_BEGIN == first) ? UTL_JSON_VALUE_OBJECT : UTL_JSON_VALUE_ARRAY;
            is_container = TRUE;
            ++parser->cursor.offset;
            break;
        case JSON_QUOTE:
            value->kind   = UTL_JSON_VALUE_STRING;
            parser->valid = ReadJsonStringBody(&parser->cursor, &value->text);
            break;
        case JSON_LITERAL_TRUE_FIRST:
        case JSON_LITERAL_FALSE_FIRST:
        case JSON_LITERAL_NULL_FIRST:
            if (JSON_LITERAL_TRUE_FIRST == first)
            {
                literal.data      = (const byte_t*)JSON_TRUE;
                literal.byteCount = sizeof(JSON_TRUE) - 1;
                value->kind       = UTL_JSON_VALUE_TRUE;
            }
            else if (JSON_LITERAL_FALSE_FIRST == first)
            {
                literal.data      = (const byte_t*)JSON_FALSE;
                literal.byteCount = sizeof(JSON_FALSE) - 1;
                value->kind       = UTL_JSON_VALUE_FALSE;
            }
            else
            {
                literal.data      = (const byte_t*)JSON_NULL;
                literal.byteCount = sizeof(JSON_NULL) - 1;
            }
            value->text.data      = parser->cursor.text.data + parser->cursor.offset;
            value->text.byteCount = literal.byteCount;
            parser->valid         = ReadJsonLiteral(&parser->cursor, literal);
            break;
        default:
            value->kind   = UTL_JSON_VALUE_NUMBER;
            parser->valid = ReadJsonNumber(&parser->cursor, &value->text);
            break;
        }
        if (parser->valid)
        {
            AppendJsonValue(parser, is_container);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  An object name and colon are required before ReadJsonValue can append a member.
 */
static FORCE_INLINE void ReadJsonChild(JsonParser_t* parser)
{
    Utl_Json_Text_t name;
    bool_t          is_object;
    bool_t          has_separator;

    name.data      = NULL;
    name.byteCount = 0;
    is_object      = (UTL_JSON_VALUE_OBJECT == parser->reader->values[parser->parent].kind);
    if (is_object)
    {
        parser->valid = ReadJsonStringBody(&parser->cursor, &name);
        if (parser->valid)
        {
            SkipJsonSpace(&parser->cursor);
            has_separator = (parser->cursor.offset < parser->cursor.text.byteCount);
            parser->valid = has_separator;
            if (has_separator)
            {
                parser->valid = (JSON_COLON == parser->cursor.text.data[parser->cursor.offset]);
            }
            if (parser->valid)
            {
                ++parser->cursor.offset;
                SkipJsonSpace(&parser->cursor);
            }
        }
    }
    if (parser->valid)
    {
        ReadJsonValue(parser, name);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A comma requires another child, so closing immediately after a comma
 *  is rejected. Parent links replace recursive C calls for arbitrary admitted depth.
 */
static FORCE_INLINE void ReadJsonContainerStep(JsonParser_t* parser)
{
    const Utl_Json_Value_t* parent;
    bool_t                  has_input;
    byte_t                  close;
    byte_t                  current;
    bool_t                  is_close;
    bool_t                  requires_value;
    bool_t                  can_close;
    bool_t                  is_comma;

    parent        = &parser->reader->values[parser->parent];
    has_input     = (parser->cursor.offset < parser->cursor.text.byteCount);
    parser->valid = has_input;
    if (has_input)
    {
        close          = (UTL_JSON_VALUE_OBJECT == parent->kind) ? JSON_OBJECT_END : JSON_ARRAY_END;
        current        = parser->cursor.text.data[parser->cursor.offset];
        is_close       = (close == current);
        requires_value = (JSON_READ_VALUE == parser->phase);
        can_close      = is_close && !requires_value;
        if (can_close)
        {
            ++parser->cursor.offset;
            parser->parent = parent->parent;
            parser->phase  = JSON_READ_SEPARATOR;
        }
        else if (JSON_READ_SEPARATOR == parser->phase)
        {
            is_comma      = (JSON_COMMA == current);
            parser->valid = is_comma;
            if (is_comma)
            {
                ++parser->cursor.offset;
                parser->phase = JSON_READ_VALUE;
            }
        }
        else
        {
            ReadJsonChild(parser);
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Utl_Json_InitializeWriter(Utl_Json_Writer_t* writer,
                               Utl_Json_Frame_t*  frames,
                               uint32_t           frameCapacity,
                               byte_t*            buffer,
                               uint32_t           capacity)
{
    bool_t has_frames;
    bool_t scalar_only;
    bool_t valid_storage;
    LOGASSERT(NULL != writer);
    has_frames    = (NULL != frames);
    scalar_only   = (0 == frameCapacity);
    valid_storage = has_frames || scalar_only;
    LOGASSERT(valid_storage);
    writer->frames        = frames;
    writer->frameCapacity = frameCapacity;
    writer->depth         = 0;
    writer->buffer        = buffer;
    writer->capacity      = capacity;
    writer->used          = 0;
    writer->status        = UTL_JSON_READY;
    writer->rootWritten   = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Record contents are initialized only when a value occupies them.
 */
void Utl_Json_InitializeReader(Utl_Json_Reader_t* reader, Utl_Json_Value_t* values, uint32_t capacity)
{
    LOGASSERT(NULL != reader);
    LOGASSERT(NULL != values);
    LOGASSERT(0 < capacity);
    reader->values     = values;
    reader->capacity   = capacity;
    reader->valueCount = 0;
    reader->offset     = 0;
    reader->status     = UTL_JSON_READ_READY;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Json_GetByteCount(const Utl_Json_Writer_t* writer)
{
    LOGASSERT(NULL != writer);
    return writer->used;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
Utl_Json_Status_t Utl_Json_GetStatus(const Utl_Json_Writer_t* writer)
{
    LOGASSERT(NULL != writer);
    return writer->status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Json_GetStringByteCount(Utl_Json_Text_t text)
{
    bool_t   valid;
    uint64_t count;
    uint32_t index;
    byte_t   current;
    bool_t   is_quote;
    bool_t   is_escape;
    bool_t   short_escape;
    bool_t   control;
    uint32_t result;
    bool_t   count_fits;
    bool_t   can_publish;
    valid = Utl_Unicode_IsValidUtf8(text.data, text.byteCount);
    count = JSON_QUOTE_COUNT;
    if (valid)
    {
        for (index = 0; index < text.byteCount; ++index)
        {
            current      = text.data[index];
            is_quote     = (JSON_QUOTE == current);
            is_escape    = (JSON_ESCAPE == current);
            short_escape = is_quote || is_escape;
            control      = (current < JSON_FIRST_UNESCAPED);
            if (short_escape)
            {
                count += JSON_SHORT_ESCAPE_BYTES;
            }
            else if (control)
            {
                count += JSON_CONTROL_ESCAPE_BYTES;
            }
            else
            {
                ++count;
            }
        }
    }
    result      = 0;
    count_fits  = (count <= UINT32_MAX);
    can_publish = valid && count_fits;
    if (can_publish)
    {
        result = (uint32_t)count;
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_IsComplete(const Utl_Json_Writer_t* writer)
{
    LOGASSERT(NULL != writer);
    return (UTL_JSON_COMPLETE == writer->status);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_IsNumber(Utl_Json_Text_t text)
{
    JsonCursor_t cursor;
    bool_t       has_storage;
    bool_t       has_text;
    bool_t       valid;
    bool_t       negative;
    bool_t       zero;
    bool_t       has_remaining;
    bool_t       can_read;
    bool_t       fraction;
    bool_t       consumed;
    cursor.text   = text;
    cursor.offset = 0;
    has_storage   = (NULL != text.data);
    has_text      = (0 < text.byteCount);
    valid         = has_storage && has_text;
    if (valid)
    {
        negative = (JSON_MINUS == text.data[cursor.offset]);
        if (negative)
        {
            ++cursor.offset;
        }
        valid = (cursor.offset < text.byteCount);
    }
    if (valid)
    {
        zero = (JSON_DIGIT_ZERO == text.data[cursor.offset]);
        if (zero)
        {
            ++cursor.offset;
        }
        else
        {
            valid = ReadDigits(&cursor);
        }
    }
    has_remaining = (cursor.offset < text.byteCount);
    can_read      = valid && has_remaining;
    if (can_read)
    {
        fraction = (JSON_POINT == text.data[cursor.offset]);
        if (fraction)
        {
            ++cursor.offset;
            valid = ReadDigits(&cursor);
        }
    }
    if (valid)
    {
        valid = ReadExponent(&cursor);
    }
    consumed = (cursor.offset == text.byteCount);
    valid    = valid && consumed;
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
Utl_Json_ReadStatus_t Utl_Json_GetReadStatus(const Utl_Json_Reader_t* reader)
{
    LOGASSERT(NULL != reader);
    return reader->status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Json_GetReadByteCount(const Utl_Json_Reader_t* reader)
{
    LOGASSERT(NULL != reader);
    return reader->offset;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Failed reads cannot expose partially initialized value records.
 */
const Utl_Json_Value_t* Utl_Json_GetRoot(const Utl_Json_Reader_t* reader)
{
    const Utl_Json_Value_t* root;
    LOGASSERT(NULL != reader);
    root = NULL;
    if (UTL_JSON_READ_COMPLETE == reader->status)
    {
        root = &reader->values[0];
    }
    return root;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The caller supplies a value borrowed from this reader, or NULL for an absent optional array.
 */
const Utl_Json_Value_t* Utl_Json_GetElement(const Utl_Json_Reader_t* reader,
                                            const Utl_Json_Value_t*  array,
                                            uint32_t                 index)
{
    const Utl_Json_Value_t* value;
    bool_t                  complete;
    bool_t                  has_array;
    bool_t                  can_query;
    bool_t                  is_array;
    bool_t                  in_range;
    uint32_t                child;
    uint32_t                position;

    LOGASSERT(NULL != reader);
    value     = NULL;
    complete  = (UTL_JSON_READ_COMPLETE == reader->status);
    has_array = (NULL != array);
    can_query = complete && has_array;
    if (can_query)
    {
        is_array  = (UTL_JSON_VALUE_ARRAY == array->kind);
        in_range  = (index < array->childCount);
        can_query = is_array && in_range;
        if (can_query)
        {
            child = array->firstChild;
            for (position = 0; position < index; ++position)
            {
                child = reader->values[child].nextSibling;
            }
            value = &reader->values[child];
        }
    }
    return value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Conversion preserves the exact decimal value. Digits outside the retained
 *  prefix may be discarded only when a negative decimal shift removes zeros.
 *  Signed range is checked before narrowing, with INT64_MIN handled separately.
 */
bool_t Utl_Json_GetInt64(int64_t* result, const Utl_Json_Value_t* value)
{
    bool_t        has_value;
    bool_t        is_number;
    bool_t        valid;
    JsonInteger_t number;
    uint64_t      magnitude;
    uint64_t      limit;
    bool_t        is_minimum;
    int64_t       candidate;

    LOGASSERT(NULL != result);

    has_value = (NULL != value);
    is_number = FALSE;
    if (has_value)
    {
        is_number = (UTL_JSON_VALUE_NUMBER == value->kind);
    }
    valid = FALSE;
    if (is_number)
    {
        LOGASSERT(NULL != value->text.data);
        LOGASSERT(0 < value->text.byteCount);
        number = ReadIntegerParts(value->text);
        valid  = GetIntegerMagnitude(&magnitude, &number);
        if (valid)
        {
            limit = (uint64_t)INT64_MAX;
            if (number.negative)
            {
                ++limit;
            }
            valid = (magnitude <= limit);
            if (valid)
            {
                is_minimum = (magnitude > (uint64_t)INT64_MAX);
                if (is_minimum)
                {
                    candidate = INT64_MIN;
                }
                else
                {
                    candidate = (int64_t)magnitude;
                    if (number.negative)
                    {
                        candidate = -candidate;
                    }
                }
                *result = candidate;
            }
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every source character is checked; scalar output width never exceeds its encoded JSON source width.
 */
bool_t Utl_Json_GetDecodedByteCount(uint32_t* byteCount, Utl_Json_Text_t text)
{
    bool_t       has_data;
    bool_t       is_empty;
    bool_t       valid_storage;
    JsonCursor_t cursor;
    uint32_t     count;
    bool_t       valid;
    uint32_t     scalar;

    LOGASSERT(NULL != byteCount);
    has_data      = (NULL != text.data);
    is_empty      = (0 == text.byteCount);
    valid_storage = has_data || is_empty;
    LOGASSERT(valid_storage);
    cursor.text   = text;
    cursor.offset = 0;
    count         = 0;
    valid         = TRUE;
    for (;;)
    {
        if (cursor.offset == text.byteCount)
        {
            break;
        }
        valid = ReadJsonScalar(&cursor, &scalar);
        if (!valid)
        {
            break;
        }
        count += Utl_Unicode_GetUtf8ByteCount(scalar);
    }
    if (valid)
    {
        *byteCount = count;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Absent or non-string values compare unequal. Unicode escape spelling does not affect equality.
 */
bool_t Utl_Json_IsStringEqual(const Utl_Json_Value_t* value, Utl_Json_Text_t expected)
{
    bool_t has_data;
    bool_t is_empty;
    bool_t valid_storage;
    bool_t equal;

    has_data      = (NULL != expected.data);
    is_empty      = (0 == expected.byteCount);
    valid_storage = has_data || is_empty;
    LOGASSERT(valid_storage);
    equal = FALSE;
    if (NULL != value)
    {
        if (UTL_JSON_VALUE_STRING == value->kind)
        {
            equal = IsDecodedEqual(value->text, expected);
        }
    }
    return equal;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Duplicate names remain represented in source order; this operation selects the first.
 *  Applications needing unique names must enforce that schema requirement separately.
 */
const Utl_Json_Value_t* Utl_Json_FindMember(const Utl_Json_Reader_t* reader,
                                            const Utl_Json_Value_t*  object,
                                            Utl_Json_Text_t          name)
{
    bool_t                  has_data;
    bool_t                  is_empty;
    bool_t                  valid_storage;
    const Utl_Json_Value_t* value;
    bool_t                  complete;
    bool_t                  has_object;
    bool_t                  can_query;
    uint32_t                child;
    const Utl_Json_Value_t* candidate;
    bool_t                  matches;

    LOGASSERT(NULL != reader);
    has_data      = (NULL != name.data);
    is_empty      = (0 == name.byteCount);
    valid_storage = has_data || is_empty;
    LOGASSERT(valid_storage);
    value      = NULL;
    complete   = (UTL_JSON_READ_COMPLETE == reader->status);
    has_object = (NULL != object);
    can_query  = complete && has_object;
    if (can_query)
    {
        can_query = (UTL_JSON_VALUE_OBJECT == object->kind);
        if (can_query)
        {
            child = object->firstChild;
            for (;;)
            {
                if (UTL_JSON_NO_VALUE == child)
                {
                    break;
                }
                candidate = &reader->values[child];
                matches   = IsDecodedEqual(candidate->name, name);
                if (matches)
                {
                    value = candidate;
                    break;
                }
                child = candidate->nextSibling;
            }
        }
    }
    return value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Container parent links supply the parse stack. Each loop consumes input,
 *  closes a container or latches failure. One optional leading UTF-8 signature is ignored.
 */
Utl_Json_ReadStatus_t Utl_Json_ReadDocument(Utl_Json_Reader_t* reader, Utl_Json_Text_t text)
{
    bool_t          has_data;
    bool_t          is_empty;
    bool_t          valid_storage;
    JsonParser_t    parser;
    bool_t          has_signature_space;
    bool_t          first_signature;
    bool_t          second_signature;
    bool_t          third_signature;
    bool_t          has_signature;
    Utl_Json_Text_t name;

    LOGASSERT(NULL != reader);
    LOGASSERT(NULL != reader->values);
    LOGASSERT(0 < reader->capacity);
    has_data      = (NULL != text.data);
    is_empty      = (0 == text.byteCount);
    valid_storage = has_data || is_empty;
    LOGASSERT(valid_storage);
    reader->valueCount   = 0;
    parser.reader        = reader;
    parser.cursor.text   = text;
    parser.cursor.offset = 0;
    parser.parent        = UTL_JSON_NO_VALUE;
    parser.phase         = JSON_READ_FIRST;
    parser.valid         = TRUE;
    parser.failure       = UTL_JSON_READ_INVALID;
    has_signature_space  = (JSON_UTF8_BOM_BYTES <= text.byteCount);
    if (has_signature_space)
    {
        first_signature  = (JSON_UTF8_BOM_FIRST == text.data[0]);
        second_signature = (JSON_UTF8_BOM_SECOND == text.data[1]);
        third_signature  = (JSON_UTF8_BOM_THIRD == text.data[2]);
        has_signature    = first_signature && second_signature && third_signature;
        if (has_signature)
        {
            parser.cursor.offset = JSON_UTF8_BOM_BYTES;
        }
    }
    SkipJsonSpace(&parser.cursor);
    name.data      = NULL;
    name.byteCount = 0;
    ReadJsonValue(&parser, name);
    for (;;)
    {
        if (!parser.valid)
        {
            break;
        }
        SkipJsonSpace(&parser.cursor);
        if (UTL_JSON_NO_VALUE == parser.parent)
        {
            parser.valid = (parser.cursor.offset == text.byteCount);
            break;
        }
        ReadJsonContainerStep(&parser);
    }
    reader->offset = parser.cursor.offset;
    reader->status = UTL_JSON_READ_COMPLETE;
    if (!parser.valid)
    {
        reader->status     = parser.failure;
        reader->valueCount = 0;
    }
    return reader->status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A complete validation/counting pass precedes any output store. No terminator
 *  is appended; embedded U+0000 remains a counted data byte.
 */
bool_t Utl_Json_ReadString(uint32_t* byteCount, byte_t* output, uint32_t capacity, Utl_Json_Text_t text)
{
    bool_t       valid;
    uint32_t     required;
    bool_t       fits;
    JsonCursor_t cursor;
    uint32_t     used;
    bool_t       read;
    uint32_t     scalar;
    uint32_t     written;

    LOGASSERT(NULL != byteCount);
    LOGASSERT(NULL != output);
    valid = Utl_Json_GetDecodedByteCount(&required, text);
    if (valid)
    {
        fits  = (required <= capacity);
        valid = fits;
    }
    if (valid)
    {
        cursor.text   = text;
        cursor.offset = 0;
        used          = 0;
        for (;;)
        {
            if (cursor.offset == text.byteCount)
            {
                break;
            }
            read = ReadJsonScalar(&cursor, &scalar);
            LOGASSERT(read);
            written = Utl_Unicode_WriteUtf8(scalar, output + used, capacity - used);
            LOGASSERT(0 < written);
            used += written;
        }
        LOGASSERT(required == used);
        *byteCount = used;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_WriteName(Utl_Json_Writer_t* writer, Utl_Json_Text_t name)
{
    bool_t            already_complete;
    bool_t            ready;
    bool_t            has_frame;
    Utl_Json_Frame_t* frame;
    bool_t            valid_state;
    bool_t            accepted;
    bool_t            is_object;
    uint32_t          count;
    bool_t            valid_text;
    uint32_t          prefix;

    LOGASSERT(NULL != writer);

    already_complete = (UTL_JSON_COMPLETE == writer->status);
    if (already_complete)
    {
        writer->status = UTL_JSON_INVALID_STATE;
    }
    ready       = (UTL_JSON_READY == writer->status);
    has_frame   = (0 < writer->depth);
    frame       = NULL;
    valid_state = FALSE;
    accepted    = FALSE;
    if (ready)
    {
        if (has_frame)
        {
            frame       = &writer->frames[writer->depth - 1];
            is_object   = (UTL_JSON_OBJECT == frame->kind);
            valid_state = is_object && !frame->needsValue;
        }
        if (!valid_state)
        {
            writer->status = UTL_JSON_INVALID_STATE;
        }
    }
    if (valid_state)
    {
        LOGASSERT(NULL != frame);
        count      = Utl_Json_GetStringByteCount(name);
        valid_text = (0 < count);
        if (valid_text)
        {
            prefix   = frame->hasValue ? 1 : 0;
            accepted = Reserve(writer, (uint64_t)count + prefix + 1);
        }
        else
        {
            writer->status = UTL_JSON_INVALID_TEXT;
        }
    }
    if (accepted)
    {
        LOGASSERT(NULL != frame);
        if (frame->hasValue)
        {
            EmitByte(writer, JSON_COMMA);
        }
        EmitString(writer, name);
        EmitByte(writer, JSON_COLON);
        frame->needsValue = TRUE;
    }
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_WriteString(Utl_Json_Writer_t* writer, Utl_Json_Text_t text)
{
    bool_t   already_complete;
    bool_t   accepted;
    uint32_t count;
    LOGASSERT(NULL != writer);
    already_complete = (UTL_JSON_COMPLETE == writer->status);
    if (already_complete)
    {
        writer->status = UTL_JSON_INVALID_STATE;
    }
    accepted = FALSE;
    if (UTL_JSON_READY == writer->status)
    {
        count = Utl_Json_GetStringByteCount(text);
        if (0 == count)
        {
            writer->status = UTL_JSON_INVALID_TEXT;
        }
        else
        {
            accepted = PrepareValue(writer, count);
            if (accepted)
            {
                EmitPrefix(writer);
                EmitString(writer, text);
                CompleteValue(writer);
            }
        }
    }
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_WriteNumber(Utl_Json_Writer_t* writer, Utl_Json_Text_t number)
{
    bool_t already_complete;
    bool_t accepted;
    bool_t valid;
    LOGASSERT(NULL != writer);
    already_complete = (UTL_JSON_COMPLETE == writer->status);
    if (already_complete)
    {
        writer->status = UTL_JSON_INVALID_STATE;
    }
    accepted = FALSE;
    if (UTL_JSON_READY == writer->status)
    {
        valid = Utl_Json_IsNumber(number);
        if (!valid)
        {
            writer->status = UTL_JSON_INVALID_NUMBER;
        }
        else
        {
            accepted = WriteLiteral(writer, number);
        }
    }
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_WriteUint64(Utl_Json_Writer_t* writer, uint64_t value)
{
    Utl_Json_Text_t number;
    byte_t          digits[JSON_UINT64_BYTES];
    bool_t          accepted;
    LOGASSERT(NULL != writer);
    number.data      = digits;
    number.byteCount = Utl_Decimal_WriteUint64(value, digits, sizeof(digits));
    LOGASSERT(0 < number.byteCount);
    accepted = WriteLiteral(writer, number);
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The local byte buffer holds a sign and the maximum decimal magnitude.
 *  Incrementing before negation covers INT64_MIN without signed overflow.
 *  WriteLiteral validates writer state and capacity before publishing any bytes.
 */
bool_t Utl_Json_WriteInt64(Utl_Json_Writer_t* writer, int64_t value)
{
    bool_t          is_negative;
    uint64_t        magnitude;
    uint32_t        prefix_count;
    byte_t          digits[JSON_INT64_BYTES];
    uint32_t        digit_count;
    Utl_Json_Text_t number;
    bool_t          accepted;

    LOGASSERT(NULL != writer);

    is_negative = (0 > value);
    if (is_negative)
    {
        /* Incrementing before negation keeps INT64_MIN within the signed domain. */
        magnitude    = (uint64_t)(-(value + 1)) + 1;
        prefix_count = 1;
        digits[0]    = JSON_MINUS;
    }
    else
    {
        magnitude    = (uint64_t)value;
        prefix_count = 0;
    }
    digit_count      = Utl_Decimal_WriteUint64(magnitude, digits + prefix_count, JSON_UINT64_BYTES);
    number.data      = digits;
    number.byteCount = prefix_count + digit_count;
    LOGASSERT(0 < digit_count);
    accepted = WriteLiteral(writer, number);
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_WriteDecimal(Utl_Json_Writer_t* writer, Utl_Json_Decimal_t value)
{
    uint64_t        scale;
    uint32_t        index;
    uint32_t        count;
    byte_t          digits[JSON_FIXED_BYTES];
    uint64_t        fraction;
    uint64_t        divisor;
    Utl_Json_Text_t number;
    bool_t          accepted;
    LOGASSERT(NULL != writer);
    LOGASSERT(value.fractionDigits <= UTL_JSON_MAX_FRACTION_DIGITS);
    scale = 1;
    for (index = 0; index < value.fractionDigits; ++index)
    {
        scale *= JSON_DECIMAL_BASE;
    }
    count = Utl_Decimal_WriteUint64(value.coefficient / scale, digits, sizeof(digits));
    LOGASSERT(0 < count);
    if (0 < value.fractionDigits)
    {
        digits[count] = JSON_POINT;
        ++count;
        fraction = value.coefficient % scale;
        divisor  = scale;
        for (index = 0; index < value.fractionDigits; ++index)
        {
            divisor       /= JSON_DECIMAL_BASE;
            digits[count] = (byte_t)(JSON_DIGIT_ZERO + (fraction / divisor));
            ++count;
            fraction %= divisor;
        }
    }
    number.data      = digits;
    number.byteCount = count;
    accepted         = WriteLiteral(writer, number);
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_WriteBoolean(Utl_Json_Writer_t* writer, bool_t value)
{
    Utl_Json_Text_t text;
    bool_t          accepted;
    LOGASSERT(NULL != writer);
    text.data      = value ? (const byte_t*)JSON_TRUE : (const byte_t*)JSON_FALSE;
    text.byteCount = value ? sizeof(JSON_TRUE) - 1 : sizeof(JSON_FALSE) - 1;
    accepted       = WriteLiteral(writer, text);
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_WriteNull(Utl_Json_Writer_t* writer)
{
    Utl_Json_Text_t text;
    bool_t          accepted;
    LOGASSERT(NULL != writer);
    text.data      = (const byte_t*)JSON_NULL;
    text.byteCount = sizeof(JSON_NULL) - 1;
    accepted       = WriteLiteral(writer, text);
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_BeginObject(Utl_Json_Writer_t* writer)
{
    LOGASSERT(NULL != writer);
    return BeginContainer(writer, UTL_JSON_OBJECT);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_EndObject(Utl_Json_Writer_t* writer)
{
    LOGASSERT(NULL != writer);
    return EndContainer(writer, UTL_JSON_OBJECT);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_BeginArray(Utl_Json_Writer_t* writer)
{
    LOGASSERT(NULL != writer);
    return BeginContainer(writer, UTL_JSON_ARRAY);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Json_EndArray(Utl_Json_Writer_t* writer)
{
    LOGASSERT(NULL != writer);
    return EndContainer(writer, UTL_JSON_ARRAY);
}
