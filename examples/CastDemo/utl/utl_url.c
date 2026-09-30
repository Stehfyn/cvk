/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * RFC 3986 sections 2 through 4 and 5.3, including the collected ABNF in Appendix A.
 * https://www.rfc-editor.org/rfc/rfc3986.html
 * Parsing preserves encoded components; decoding occurs only through explicit byte operations.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_url.h"
#include "utl/utl_decimal.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "crt/crt_memory.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define DECIMAL_RADIX      (10)
#define HEX_RADIX          (16)
#define HEX_LETTER_VALUE   (10)
#define HEX_INVALID        (16)
#define PERCENT_BYTE_COUNT (3)
#define IPV4_PART_COUNT    (4)
#define IPV4_PART_BITS     (8)
#define IPV4_PART_DIGITS   (3)
#define IPV4_PART_MAX      (255)
#define IPV6_PART_COUNT    (8)
#define IPV6_PART_DIGITS   (4)
#define IPV6_IPV4_PARTS    (2)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Component-specific additions to unreserved, sub-delims and pct-encoded.
 */
typedef enum Component_e
{
    COMPONENT_NAME,
    COMPONENT_USER,
    COMPONENT_PATH,
    COMPONENT_QUERY
} Component_t;

/*************************************************************************************************/
/**
 * \brief Caller-verified source slice with distinct offset and extent fields.
 */
typedef struct Slice_s
{
    uint32_t offset;    ///< First source byte.
    uint32_t byteCount; ///< Complete borrowed extent.
} Slice_t;

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
 * \brief Asserts the required storage of one borrowed component.
 *
 * \par Details:
 *  Required storage is present.
 *
 * \par Requirements:
 *  - 001 Nonempty spans shall have readable storage; empty spans may have a NULL base.
 *
 * \param[in] text   Borrowed component
 *
 * \retval    None No value is returned.
 */
static FORCE_INLINE void AssertText(Utl_Url_Text_t text);

/*************************************************************************************************/
/**
 * \brief Gets a borrowed counted slice without arithmetic on an absent empty source.
 *
 * \par Requirements:
 *  - 001 Offset and extent shall lie within the caller-verified source; only zero offset/count may use NULL.
 *
 * \param[in] data    Retained source base
 * \param[in] slice   Verified offset and extent
 *
 * \retval    text   Borrowed slice.
 */
static FORCE_INLINE Utl_Url_Text_t GetText(const byte_t* data, Slice_t slice);

/*************************************************************************************************/
/**
 * \brief Classifies an ASCII alphabetic URI byte.
 *
 * \par Requirements:
 *  - 001 Only ASCII ALPHA shall be accepted.
 *
 * \param[in] value   Candidate syntax byte
 *
 * \retval    TRUE    ASCII alphabetic byte.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t IsAlpha(byte_t value);

/*************************************************************************************************/
/**
 * \brief Classifies an ASCII decimal digit.
 *
 * \par Requirements:
 *  - 001 Only ASCII DIGIT shall be accepted.
 *
 * \param[in] value   Candidate syntax byte
 *
 * \retval    TRUE    ASCII decimal digit.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t IsDigit(byte_t value);

/*************************************************************************************************/
/**
 * \brief Derives one hexadecimal digit value.
 *
 * \par Requirements:
 *  - 001 Both ASCII letter cases shall be accepted; invalid bytes shall remain distinguishable.
 *
 * \param[in] value   Candidate hexadecimal digit
 *
 * \retval    0..15         Hexadecimal value.
 * \retval    HEX_INVALID   Invalid digit.
 */
static FORCE_INLINE uint32_t GetHexValue(byte_t value);

/*************************************************************************************************/
/**
 * \brief Classifies RFC 3986 unreserved syntax.
 *
 * \par Requirements:
 *  - 001 ALPHA, DIGIT, hyphen, period, underscore and tilde shall be accepted.
 *
 * \param[in] value   Candidate syntax byte
 *
 * \retval    TRUE    Unreserved byte.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t IsUnreserved(byte_t value);

/*************************************************************************************************/
/**
 * \brief Classifies the RFC 3986 sub-delims production.
 *
 * \par Requirements:
 *  - 001 Only the eleven specified ASCII delimiters shall be accepted.
 *
 * \param[in] value   Candidate syntax byte
 *
 * \retval    TRUE    Sub-delimiter byte.
 * \retval    FALSE   Otherwise.
 */
static FORCE_INLINE bool_t IsSubDelimiter(byte_t value);

/*************************************************************************************************/
/**
 * \brief Checks a complete scheme component.
 *
 * \par Requirements:
 *  - 001 The first byte shall be ALPHA; subsequent bytes shall follow the scheme production.
 *
 * \param[in] text   Counted scheme syntax
 *
 * \retval    TRUE    Nonempty valid scheme.
 * \retval    FALSE   Invalid scheme.
 */
static FORCE_INLINE bool_t IsScheme(Utl_Url_Text_t text);

/*************************************************************************************************/
/**
 * \brief Checks a complete encoded component using its delimiter grammar.
 *
 * \par Requirements:
 *  - 001 Percent escapes shall consume exactly two hexadecimal digits; non-ASCII bytes shall fail.
 *
 * \param[in] text   Counted component
 * \param[in] kind   Required component grammar
 *
 * \retval    TRUE    Valid complete component, including empty.
 * \retval    FALSE   Invalid syntax.
 */
static FORCE_INLINE bool_t IsComponent(Utl_Url_Text_t text, Component_t kind);

/*************************************************************************************************/
/**
 * \brief Checks generic decimal port syntax.
 *
 * \par Requirements:
 *  - 001 Empty syntax shall be allowed; every present byte shall be DIGIT.
 *
 * \param[in] text   Counted port syntax
 *
 * \retval    TRUE    Valid generic port syntax.
 * \retval    FALSE   Nondigit syntax.
 */
static FORCE_INLINE bool_t IsPort(Utl_Url_Text_t text);

/*************************************************************************************************/
/**
 * \brief Checks canonical dotted-decimal IPv4 host syntax.
 *
 * \par Requirements:
 *  - 001 Four decimal octets shall each be in range; leading zeros in multi-digit octets shall fail.
 *
 * \param[in] text   Counted IPv4 syntax
 *
 * \retval    TRUE    IPv4address production matched.
 * \retval    FALSE   No IPv4address match.
 */
static FORCE_INLINE bool_t IsIpv4(Utl_Url_Text_t text);

/*************************************************************************************************/
/**
 * \brief Checks one IPv6 address part or final embedded IPv4 address.
 *
 * \par Requirements:
 *  - 001 IPv4 shall occur only in the final position and occupy two 16-bit parts.
 *
 * \param[in]  text    Nonempty address part
 * \param[in]  last    Whether this part ends the address
 * \param[out] parts   Required part-count result
 *
 * \retval     TRUE    A one- or two-part width is published.
 * \retval     FALSE   Output is unchanged.
 */
static FORCE_INLINE bool_t GetIpv6PartCount(Utl_Url_Text_t text, bool_t last, uint32_t* parts);

/*************************************************************************************************/
/**
 * \brief Checks a complete IPv6 address without brackets.
 *
 * \par Requirements:
 *  - 001 Exactly eight parts or one compression omitting at_sign least one part shall be accepted.
 *
 * \param[in] text   Counted address content
 *
 * \retval    TRUE    IPv6address production matched.
 * \retval    FALSE   Invalid literal address.
 */
static FORCE_INLINE bool_t IsIpv6(Utl_Url_Text_t text);

/*************************************************************************************************/
/**
 * \brief Checks an IPvFuture literal without brackets.
 *
 * \par Requirements:
 *  - 001 A nonempty hexadecimal version and nonempty future-address syntax shall be required.
 *
 * \param[in] text   Counted future-address content
 *
 * \retval    TRUE    IPvFuture production matched.
 * \retval    FALSE   Invalid future-address literal.
 */
static FORCE_INLINE bool_t IsFuture(Utl_Url_Text_t text);

/*************************************************************************************************/
/**
 * \brief Checks that the selected host grammar matches its bytes.
 *
 * \par Requirements:
 *  - 001 Registered names shall respect first-match-wins IPv4 classification.
 *
 * \param[in] url   Required borrowed model
 *
 * \retval    TRUE    Kind and host syntax agree.
 * \retval    FALSE   Invalid host or kind.
 */
static FORCE_INLINE bool_t IsHost(const Utl_Url_t* url);

/*************************************************************************************************/
/**
 * \brief Validates authority presence and its component relationships.
 *
 * \par Requirements:
 *  - 001 Caller shall establish readable borrowed spans.
 *  - 002 Absent authority shall contain no host, user information or port.
 *
 * \param[in] url   Required model with readable spans
 *
 * \retval    valid   TRUE exactly when authority presence and syntax agree.
 */
static FORCE_INLINE bool_t IsAuthority(const Utl_Url_t* url);

/*************************************************************************************************/
/**
 * \brief Checks path grammar relationships with scheme and authority.
 *
 * \par Requirements:
 *  - 001 Authority paths shall be empty or absolute; a relative first segment shall not contain a colon.
 *
 * \param[in] url   Required borrowed model
 *
 * \retval    TRUE    Path syntax and delimiters agree.
 * \retval    FALSE   Invalid path relationship.
 */
static FORCE_INLINE bool_t IsPath(const Utl_Url_t* url);

/*************************************************************************************************/
/**
 * \brief Reads one RFC 3986 dec-octet at the supplied input position.
 *
 * \par Requirements:
 *  - 001 The initial offset shall lie within the counted input.
 *  - 002 One through three decimal digits shall encode a value no greater than 255.
 *  - 003 Multi-digit values shall have no leading zero.
 *  - 004 Failure shall preserve the offset and value outputs.
 *
 * \param[in]     text     Counted address text
 * \param[in,out] offset   Current input position
 * \param[out]    value    Decoded octet
 *
 * \retval        TRUE     One valid octet was decoded.
 * \retval        FALSE    The octet was absent or invalid.
 */
static FORCE_INLINE bool_t ReadIpv4Octet(Utl_Url_Text_t text, uint32_t* offset, uint32_t* value);

/*************************************************************************************************/
/**
 * \brief Splits a bracketed address literal or registered host from its optional port.
 *
 * \par Requirements:
 *  - 001 Position shall lie within the readable authority span.
 *  - 002 Only a complete host boundary shall publish the next position.
 *
 * \param[in,out] url        Private unpublished candidate
 * \param[in]     text       Complete authority span
 * \param[in,out] position   Host start, replaced by host end on success
 *
 * \retval        valid   TRUE when host delimiters are complete.
 */
static FORCE_INLINE bool_t ReadHost(Utl_Url_t* url, Utl_Url_Text_t text, uint32_t* position);

/*************************************************************************************************/
/**
 * \brief Splits an authority into user information, host and port.
 *
 * \par Requirements:
 *  - 001 Literal brackets and the optional port delimiter shall consume the complete authority.
 *
 * \param[in,out] url    Private candidate with authority present
 * \param[in]     text   Counted authority bytes without leading slashes
 *
 * \retval        TRUE    Complete authority syntax was recognized.
 * \retval        FALSE   Candidate shall not be published.
 */
static FORCE_INLINE bool_t ReadAuthority(Utl_Url_t* url, Utl_Url_Text_t text);

/*************************************************************************************************/
/**
 * \brief Splits path, query and fragment after the authority prefix.
 *
 * \par Details:
 *  Borrowed suffix components are established.
 *
 * \par Requirements:
 *  - 001 The destination shall be an initialized unpublished candidate.
 *  - 002 All bytes shall remain encoded; complete grammar validation follows splitting.
 *
 * \param[in,out] url    Private candidate
 * \param[in]     text   Remaining URI syntax
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void ReadPath(Utl_Url_t* url, Utl_Url_Text_t text);

/*************************************************************************************************/
/**
 * \brief Appends one already-sized component during recomposition.
 *
 * \par Details:
 *  The component bytes are copied and offset advances.
 *
 * \par Requirements:
 *  - 001 Preflight shall have proved total capacity; empty spans shall require no source/output pointer.
 *
 * \param[out]    output   Preflighted destination
 * \param[in,out] offset   Current initialized output count
 * \param[in]     text     Borrowed validated component
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void WriteText(byte_t* output, uint32_t* offset, Utl_Url_Text_t text);

/*************************************************************************************************/
/**
 * \brief Appends one grammar delimiter during recomposition.
 *
 * \par Details:
 *  One byte is written and offset advances.
 *
 * \par Requirements:
 *  - 001 Preflight shall have proved one remaining writable byte.
 *
 * \param[out]    output   Preflighted destination
 * \param[in,out] offset   Current initialized output count
 * \param[in]     value    Grammar delimiter
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void WriteCharacter(byte_t* output, uint32_t* offset, byte_t value);

/*************************************************************************************************/
/**
 * \brief Writes a prevalidated authority and its exact delimiters.
 *
 * \par Details:
 *  Authority bytes are written and the offset advances.
 *
 * \par Requirements:
 *  - 001 The model shall contain an authority whose complete output capacity was checked.
 *  - 002 Output and offset shall be disjoint from every borrowed model span.
 *
 * \param[in]     url      Validated model
 * \param[out]    output   Complete writable destination
 * \param[in,out] offset   Current destination offset
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void WriteAuthority(const Utl_Url_t* url, byte_t* output, uint32_t* offset);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Storage misuse is a development error, independent of URI syntax validity.
 */
static FORCE_INLINE void AssertText(Utl_Url_Text_t text)
{
    LOGASSERT((NULL != text.data) || (0 == text.byteCount));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Empty input is represented without evaluating NULL plus zero.
 */
static FORCE_INLINE Utl_Url_Text_t GetText(const byte_t* data, Slice_t slice)
{
    Utl_Url_Text_t text;

    text.data      = data;
    text.byteCount = slice.byteCount;
    if (0 < slice.offset)
    {
        text.data = data + slice.offset;
    }

    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  URI syntax is ASCII and does not depend on process locale.
 */
static FORCE_INLINE bool_t IsAlpha(byte_t value)
{
    bool_t upper;
    bool_t lower;

    upper = (UTL_URL_CHAR_UPPER_FIRST <= value) && (value <= UTL_URL_CHAR_UPPER_LAST);
    lower = (UTL_URL_CHAR_LOWER_FIRST <= value) && (value <= UTL_URL_CHAR_LOWER_LAST);

    return upper || lower;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsDigit(byte_t value)
{
    return (UTL_URL_CHAR_DIGIT_FIRST <= value) && (value <= UTL_URL_CHAR_DIGIT_LAST);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A distinguished value avoids treating malformed escapes as encoded zero.
 */
static FORCE_INLINE uint32_t GetHexValue(byte_t value)
{
    uint32_t digit;
    bool_t   upper;
    bool_t   lower;

    digit = HEX_INVALID;
    upper = (UTL_URL_CHAR_UPPER_FIRST <= value) && (value <= UTL_URL_CHAR_HEX_UPPER);
    lower = (UTL_URL_CHAR_LOWER_FIRST <= value) && (value <= UTL_URL_CHAR_HEX_LOWER);
    if (IsDigit(value))
    {
        digit = (uint32_t)(value - UTL_URL_CHAR_DIGIT_FIRST);
    }
    else if (upper)
    {
        digit = (uint32_t)(value - UTL_URL_CHAR_UPPER_FIRST) + HEX_LETTER_VALUE;
    }
    else if (lower)
    {
        digit = (uint32_t)(value - UTL_URL_CHAR_LOWER_FIRST) + HEX_LETTER_VALUE;
    }

    return digit;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  RFC 3986 section 2.3.
 */
static FORCE_INLINE bool_t IsUnreserved(byte_t value)
{
    bool_t alphabetic;
    bool_t digit;
    bool_t punctuation;

    alphabetic  = IsAlpha(value);
    digit       = IsDigit(value);
    punctuation = (UTL_URL_CHAR_HYPHEN == value) || (UTL_URL_CHAR_DOT == value) || (UTL_URL_CHAR_UNDERSCORE == value) ||
                  (UTL_URL_CHAR_TILDE == value);

    return alphabetic || digit || punctuation;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  RFC 3986 section 2.2; gen-delims are handled by their individual components.
 */
static FORCE_INLINE bool_t IsSubDelimiter(byte_t value)
{
    bool_t accepted;

    accepted = FALSE;
    switch (value)
    {
    case UTL_URL_CHAR_EXCLAIM:
    case UTL_URL_CHAR_DOLLAR:
    case UTL_URL_CHAR_AMP:
    case UTL_URL_CHAR_QUOTE:
    case UTL_URL_CHAR_LEFT:
    case UTL_URL_CHAR_RIGHT:
    case UTL_URL_CHAR_STAR:
    case UTL_URL_CHAR_PLUS:
    case UTL_URL_CHAR_COMMA:
    case UTL_URL_CHAR_SEMICOLON:
    case UTL_URL_CHAR_EQUAL:
        accepted = TRUE;
        break;
    default:
        break;
    }

    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The first-character rule distinguishes a scheme from a relative path segment.
 */
static FORCE_INLINE bool_t IsScheme(Utl_Url_Text_t text)
{
    bool_t   valid;
    uint32_t index;
    byte_t   value;
    bool_t   alphabetic;
    bool_t   digit;
    bool_t   punctuation;

    valid = (0 < text.byteCount);
    if (valid)
    {
        valid = IsAlpha(text.data[0]);
    }
    for (index = 1; valid && (index < text.byteCount); ++index)
    {
        value       = text.data[index];
        alphabetic  = IsAlpha(value);
        digit       = IsDigit(value);
        punctuation = (UTL_URL_CHAR_PLUS == value) || (UTL_URL_CHAR_HYPHEN == value) || (UTL_URL_CHAR_DOT == value);
        valid       = alphabetic || digit || punctuation;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Query and fragment use the same pchar/slash/question grammar. Authority host
 *  names exclude colon and '@'; user information permits colon but excludes '@'.
 */
static FORCE_INLINE bool_t IsComponent(Utl_Url_Text_t text, Component_t kind)
{
    uint32_t index;
    bool_t   valid;
    byte_t   value;
    uint32_t first;
    uint32_t second;
    bool_t   unreserved;
    bool_t   subdelimiter;
    bool_t   colon;
    bool_t   at_sign;
    bool_t   slash;
    bool_t   question;

    index = 0;
    valid = TRUE;
    for (;;)
    {
        if ((!valid) || (index >= text.byteCount))
        {
            break;
        }
        value = text.data[index];
        if (UTL_URL_CHAR_PERCENT == value)
        {
            valid = (PERCENT_BYTE_COUNT <= (text.byteCount - index));
            if (valid)
            {
                first   = GetHexValue(text.data[index + 1]);
                second  = GetHexValue(text.data[index + 2]);
                valid   = (HEX_INVALID != first) && (HEX_INVALID != second);
                index  += PERCENT_BYTE_COUNT;
            }
        }
        else
        {
            unreserved   = IsUnreserved(value);
            subdelimiter = IsSubDelimiter(value);
            colon        = (COMPONENT_NAME != kind) && (UTL_URL_CHAR_COLON == value);
            at_sign      = ((COMPONENT_PATH == kind) || (COMPONENT_QUERY == kind)) && (UTL_URL_CHAR_AT == value);
            slash        = ((COMPONENT_PATH == kind) || (COMPONENT_QUERY == kind)) && (UTL_URL_CHAR_SLASH == value);
            question     = (COMPONENT_QUERY == kind) && (UTL_URL_CHAR_QUERY == value);
            valid        = unreserved || subdelimiter || colon || at_sign || slash || question;
            ++index;
        }
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The generic URI grammar does not impose a native integer range on a port.
 */
static FORCE_INLINE bool_t IsPort(Utl_Url_Text_t text)
{
    bool_t   valid;
    uint32_t index;

    valid = TRUE;
    for (index = 0; valid && (index < text.byteCount); ++index)
    {
        valid = IsDigit(text.data[index]);
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  At most three decimal digits are evaluated per octet, before range checking.
 */
static FORCE_INLINE bool_t IsIpv4(Utl_Url_Text_t text)
{
    bool_t   valid;
    uint32_t address;
    valid = Utl_Url_ReadIpv4(text, &address);
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A period selects the IPv4 tail grammar, rather than a malformed hex group.
 */
static FORCE_INLINE bool_t GetIpv6PartCount(Utl_Url_Text_t text, bool_t last, uint32_t* parts)
{
    bool_t   dotted;
    uint32_t index;
    uint32_t count;
    bool_t   valid;

    dotted = FALSE;
    for (index = 0; index < text.byteCount; ++index)
    {
        dotted = dotted || (UTL_URL_CHAR_DOT == text.data[index]);
    }
    count = 1;
    if (dotted)
    {
        valid = last && IsIpv4(text);
        count = IPV6_IPV4_PARTS;
    }
    else
    {
        valid = (0 < text.byteCount) && (text.byteCount <= IPV6_PART_DIGITS);
        for (index = 0; valid && (index < text.byteCount); ++index)
        {
            valid = (HEX_INVALID != GetHexValue(text.data[index]));
        }
    }
    if (valid)
    {
        *parts = count;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each explicit hex group consumes one of eight parts; an IPv4 tail consumes
 *  two. A single '::' must replace at_sign least one omitted part.
 */
static FORCE_INLINE bool_t IsIpv6(Utl_Url_Text_t text)
{
    bool_t         valid;
    uint32_t       offset;
    uint32_t       count;
    bool_t         compressed;
    uint32_t       first;
    Slice_t        slice;
    Utl_Url_Text_t part;
    bool_t         last;
    uint32_t       width;
    bool_t         complete;

    valid      = (0 < text.byteCount);
    offset     = 0;
    count      = 0;
    compressed = FALSE;
    if (valid && (UTL_URL_CHAR_COLON == text.data[0]))
    {
        valid = (2 <= text.byteCount);
        if (valid)
        {
            valid      = (UTL_URL_CHAR_COLON == text.data[1]);
            compressed = valid;
            offset     = 2;
        }
    }
    for (;;)
    {
        if ((!valid) || (offset >= text.byteCount))
        {
            break;
        }
        first = offset;
        for (; offset < text.byteCount; ++offset)
        {
            if (UTL_URL_CHAR_COLON == text.data[offset])
            {
                break;
            }
        }
        slice.offset    = first;
        slice.byteCount = offset - first;
        part            = GetText(text.data, slice);
        last            = (offset == text.byteCount);
        valid           = GetIpv6PartCount(part, last, &width);
        if (valid)
        {
            count += width;
            valid  = (count <= IPV6_PART_COUNT);
        }
        if (valid && (!last))
        {
            ++offset;
            valid = (offset < text.byteCount);
            if (valid && (UTL_URL_CHAR_COLON == text.data[offset]))
            {
                valid      = !compressed;
                compressed = TRUE;
                ++offset;
            }
        }
    }
    complete = compressed ? (count < IPV6_PART_COUNT) : (count == IPV6_PART_COUNT);
    valid    = valid && complete;

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Percent encoding is absent from the IPvFuture address production.
 */
static FORCE_INLINE bool_t IsFuture(Utl_Url_Text_t text)
{
    bool_t   valid;
    uint32_t offset;
    uint32_t first;
    bool_t   unreserved;
    bool_t   subdelimiter;
    bool_t   colon;

    valid  = (0 < text.byteCount);
    offset = 1;
    if (valid)
    {
        valid = (UTL_URL_CHAR_FUTURE == text.data[0]) || (UTL_URL_CHAR_FUTURE_UPPER == text.data[0]);
    }
    first = offset;
    for (; valid && (offset < text.byteCount); ++offset)
    {
        if (HEX_INVALID == GetHexValue(text.data[offset]))
        {
            break;
        }
    }
    valid = valid && (first < offset) && (offset < text.byteCount);
    if (valid)
    {
        valid = (UTL_URL_CHAR_DOT == text.data[offset]);
        ++offset;
        valid = valid && (offset < text.byteCount);
    }
    for (; valid && (offset < text.byteCount); ++offset)
    {
        unreserved   = IsUnreserved(text.data[offset]);
        subdelimiter = IsSubDelimiter(text.data[offset]);
        colon        = (UTL_URL_CHAR_COLON == text.data[offset]);
        valid        = unreserved || subdelimiter || colon;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Classification follows the RFC's first-match-wins host rule. Empty authority
 *  hosts use the registered-name production; scheme policy may reject them later.
 */
static FORCE_INLINE bool_t IsHost(const Utl_Url_t* url)
{
    bool_t valid;

    if (UTL_URL_HOST_NAME == url->hostKind)
    {
        valid = IsComponent(url->host, COMPONENT_NAME) && (!IsIpv4(url->host));
    }
    else if (UTL_URL_HOST_IPV4 == url->hostKind)
    {
        valid = IsIpv4(url->host);
    }
    else if (UTL_URL_HOST_IPV6 == url->hostKind)
    {
        valid = IsIpv6(url->host);
    }
    else if (UTL_URL_HOST_FUTURE == url->hostKind)
    {
        valid = IsFuture(url->host);
    }
    else
    {
        valid = FALSE;
    }

    return valid;
}
/*************************************************************************************************/
/**
 * \par Details:
 *  Empty user information and ports are valid only with their presence flags.
 */
static FORCE_INLINE bool_t IsAuthority(const Utl_Url_t* url)
{
    bool_t valid;
    bool_t empty_user;
    bool_t empty_port;
    bool_t empty_host;

    valid      = TRUE;
    empty_user = (!url->hasUserInfo) && (0 == url->userInfo.byteCount);
    empty_port = (!url->hasPort) && (0 == url->port.byteCount);
    empty_host = (UTL_URL_HOST_NONE == url->hostKind) && (0 == url->host.byteCount);
    if (valid && (!url->hasAuthority))
    {
        valid = empty_user && empty_port && empty_host;
    }
    if (valid && url->hasAuthority)
    {
        valid = (UTL_URL_HOST_NONE != url->hostKind) && IsHost(url);
        if (valid)
        {
            valid = empty_user;
            if (url->hasUserInfo)
            {
                valid = IsComponent(url->userInfo, COMPONENT_USER);
            }
        }
        if (valid)
        {
            valid = empty_port;
            if (url->hasPort)
            {
                valid = IsPort(url->port);
            }
        }
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Percent-encoded ':' is data and does not violate the path-noscheme rule.
 */
static FORCE_INLINE bool_t IsPath(const Utl_Url_t* url)
{
    bool_t   valid;
    bool_t   nonempty;
    bool_t   absolute;
    uint32_t index;

    valid    = IsComponent(url->path, COMPONENT_PATH);
    nonempty = (0 < url->path.byteCount);
    if (valid && nonempty)
    {
        absolute = (UTL_URL_CHAR_SLASH == url->path.data[0]);
        if (url->hasAuthority)
        {
            valid = absolute;
        }
        else if (absolute && (1 < url->path.byteCount))
        {
            valid = (UTL_URL_CHAR_SLASH != url->path.data[1]);
        }
        else if ((!absolute) && (!url->hasScheme))
        {
            for (index = 0; valid && (index < url->path.byteCount); ++index)
            {
                if (UTL_URL_CHAR_SLASH == url->path.data[index])
                {
                    break;
                }
                valid = (UTL_URL_CHAR_COLON != url->path.data[index]);
            }
        }
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  At most three digits are accumulated. The caller validates the following dot
 *  or end of address, so an overlong octet cannot consume part of another octet.
 */
static FORCE_INLINE bool_t ReadIpv4Octet(Utl_Url_Text_t text, uint32_t* offset, uint32_t* value)
{
    uint32_t cursor;
    uint32_t first;
    uint32_t candidate;
    uint32_t digits;
    bool_t   has_input;
    bool_t   has_capacity;
    bool_t   can_read;
    bool_t   is_digit;
    bool_t   has_digits;
    bool_t   fits_octet;
    bool_t   valid;
    bool_t   multiple_digits;
    bool_t   starts_with_zero;
    bool_t   leading_zero;

    LOGASSERT(NULL != offset);
    LOGASSERT(NULL != value);
    AssertText(text);
    LOGASSERT(*offset <= text.byteCount);

    cursor    = *offset;
    first     = cursor;
    candidate = 0;
    digits    = 0;
    for (;;)
    {
        has_input    = (cursor < text.byteCount);
        has_capacity = (digits < IPV4_PART_DIGITS);
        can_read     = has_input && has_capacity;
        if (!can_read)
        {
            break;
        }
        is_digit = IsDigit(text.data[cursor]);
        if (!is_digit)
        {
            break;
        }
        candidate = (candidate * DECIMAL_RADIX) + (uint32_t)(text.data[cursor] - UTL_URL_CHAR_DIGIT_FIRST);
        ++cursor;
        ++digits;
    }
    has_digits = !!digits;
    fits_octet = (candidate <= IPV4_PART_MAX);
    valid      = has_digits && fits_octet;
    if (valid)
    {
        multiple_digits  = (1 < digits);
        starts_with_zero = (UTL_URL_CHAR_DIGIT_FIRST == text.data[first]);
        leading_zero     = multiple_digits && starts_with_zero;
        valid            = !leading_zero;
    }
    if (valid)
    {
        *offset = cursor;
        *value  = candidate;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Brackets prevent IPv6 colons from being interpreted as port separators.
 */
static FORCE_INLINE bool_t ReadHost(Utl_Url_t* url, Utl_Url_Text_t text, uint32_t* position)
{
    uint32_t offset;
    bool_t   valid;
    bool_t   literal;
    uint32_t first;
    Slice_t  slice;

    offset  = *position;
    valid   = TRUE;
    literal = FALSE;
    if (offset < text.byteCount)
    {
        literal = (UTL_URL_CHAR_OPEN == text.data[offset]);
    }
    if (literal)
    {
        ++offset;
        first = offset;
        for (; offset < text.byteCount; ++offset)
        {
            if (UTL_URL_CHAR_CLOSE == text.data[offset])
            {
                break;
            }
        }
        valid = (offset < text.byteCount);
        if (valid)
        {
            slice.offset    = first;
            slice.byteCount = offset - first;
            url->host       = GetText(text.data, slice);
            url->hostKind   = UTL_URL_HOST_IPV6;
            if (IsFuture(url->host))
            {
                url->hostKind = UTL_URL_HOST_FUTURE;
            }
            ++offset;
        }
    }
    else
    {
        first = offset;
        for (; offset < text.byteCount; ++offset)
        {
            if (UTL_URL_CHAR_COLON == text.data[offset])
            {
                break;
            }
        }
        slice.offset    = first;
        slice.byteCount = offset - first;
        url->host       = GetText(text.data, slice);
        url->hostKind   = UTL_URL_HOST_NAME;
        if (IsIpv4(url->host))
        {
            url->hostKind = UTL_URL_HOST_IPV4;
        }
    }
    if (valid)
    {
        *position = offset;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Bracketed literals keep internal colons separate from the port delimiter.
 *  A second '@' cannot occur in user information or in a registered name.
 */
static FORCE_INLINE bool_t ReadAuthority(Utl_Url_t* url, Utl_Url_Text_t text)
{
    uint32_t offset;
    uint32_t index;
    Slice_t  slice;
    bool_t   valid;

    offset = 0;
    for (index = 0; index < text.byteCount; ++index)
    {
        if (UTL_URL_CHAR_AT == text.data[index])
        {
            url->hasUserInfo = TRUE;
            slice.offset     = 0;
            slice.byteCount  = index;
            url->userInfo    = GetText(text.data, slice);
            offset           = index + 1;
            break;
        }
    }
    valid = IsComponent(url->userInfo, COMPONENT_USER);
    if (valid)
    {
        valid = ReadHost(url, text, &offset);
    }
    if (valid && (offset < text.byteCount))
    {
        valid = (UTL_URL_CHAR_COLON == text.data[offset]);
        if (valid)
        {
            ++offset;
            url->hasPort    = TRUE;
            slice.offset    = offset;
            slice.byteCount = text.byteCount - offset;
            url->port       = GetText(text.data, slice);
        }
    }
    valid = valid && IsHost(url) && IsPort(url->port);

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The first question mark separates query; the first hash separates fragment.
 */
static FORCE_INLINE void ReadPath(Utl_Url_t* url, Utl_Url_Text_t text)
{
    uint32_t offset;
    uint32_t first;
    byte_t   value;
    Slice_t  slice;

    offset = 0;
    first  = offset;
    for (; offset < text.byteCount; ++offset)
    {
        value = text.data[offset];
        if ((UTL_URL_CHAR_QUERY == value) || (UTL_URL_CHAR_FRAGMENT == value))
        {
            break;
        }
    }
    slice.offset    = first;
    slice.byteCount = offset - first;
    url->path       = GetText(text.data, slice);
    if ((offset < text.byteCount) && (UTL_URL_CHAR_QUERY == text.data[offset]))
    {
        url->hasQuery = TRUE;
        ++offset;
        first = offset;
        for (; offset < text.byteCount; ++offset)
        {
            if (UTL_URL_CHAR_FRAGMENT == text.data[offset])
            {
                break;
            }
        }
        slice.offset    = first;
        slice.byteCount = offset - first;
        url->query      = GetText(text.data, slice);
    }
    if (offset < text.byteCount)
    {
        LOGASSERT(UTL_URL_CHAR_FRAGMENT == text.data[offset]);
        url->hasFragment = TRUE;
        ++offset;
        slice.offset    = offset;
        slice.byteCount = text.byteCount - offset;
        url->fragment   = GetText(text.data, slice);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Complete syntax and size validation precede the first call from Write.
 */
static FORCE_INLINE void WriteText(byte_t* output, uint32_t* offset, Utl_Url_Text_t text)
{
    if (0 < text.byteCount)
    {
        CRT_Memory_Copy(output + *offset, text.data, text.byteCount);
        *offset += text.byteCount;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Complete syntax and size validation precede the first call from Write.
 */
static FORCE_INLINE void WriteCharacter(byte_t* output, uint32_t* offset, byte_t value)
{
    output[*offset] = value;
    ++(*offset);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Literal brackets and empty user/port delimiters are preserved exactly.
 */
static FORCE_INLINE void WriteAuthority(const Utl_Url_t* url, byte_t* output, uint32_t* offset)
{
    bool_t literal;

    WriteCharacter(output, offset, UTL_URL_CHAR_SLASH);
    WriteCharacter(output, offset, UTL_URL_CHAR_SLASH);
    if (url->hasUserInfo)
    {
        WriteText(output, offset, url->userInfo);
        WriteCharacter(output, offset, UTL_URL_CHAR_AT);
    }
    literal = (UTL_URL_HOST_IPV6 == url->hostKind) || (UTL_URL_HOST_FUTURE == url->hostKind);
    if (literal)
    {
        WriteCharacter(output, offset, UTL_URL_CHAR_OPEN);
    }
    WriteText(output, offset, url->host);
    if (literal)
    {
        WriteCharacter(output, offset, UTL_URL_CHAR_CLOSE);
    }
    if (url->hasPort)
    {
        WriteCharacter(output, offset, UTL_URL_CHAR_COLON);
        WriteText(output, offset, url->port);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The empty relative reference is valid and has an empty recomposition.
 */
extern FORCE_INLINE void Utl_Url_Initialize(Utl_Url_t* url)
{
    Utl_Url_Text_t empty;

    LOGASSERT(NULL != url);

    empty.data        = NULL;
    empty.byteCount   = 0;
    url->scheme       = empty;
    url->userInfo     = empty;
    url->host         = empty;
    url->port         = empty;
    url->path         = empty;
    url->query        = empty;
    url->fragment     = empty;
    url->hostKind     = UTL_URL_HOST_NONE;
    url->hasScheme    = FALSE;
    url->hasAuthority = FALSE;
    url->hasUserInfo  = FALSE;
    url->hasPort      = FALSE;
    url->hasQuery     = FALSE;
    url->hasFragment  = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Presence is checked independently from length, preserving empty authority,
 *  user information, port, query and fragment delimiters when allowed.
 */
bool_t Utl_Url_IsValid(const Utl_Url_t* url)
{
    bool_t valid;

    LOGASSERT(NULL != url);
    AssertText(url->scheme);
    AssertText(url->userInfo);
    AssertText(url->host);
    AssertText(url->port);
    AssertText(url->path);
    AssertText(url->query);
    AssertText(url->fragment);

    valid = (0 == url->scheme.byteCount);
    if (url->hasScheme)
    {
        valid = IsScheme(url->scheme);
    }
    if (valid)
    {
        valid = IsAuthority(url);
    }
    if (valid)
    {
        valid = IsPath(url);
    }
    if (valid)
    {
        valid = (0 == url->query.byteCount);
        if (url->hasQuery)
        {
            valid = IsComponent(url->query, COMPONENT_QUERY);
        }
    }
    if (valid)
    {
        valid = (0 == url->fragment.byteCount);
        if (url->hasFragment)
        {
            valid = IsComponent(url->fragment, COMPONENT_QUERY);
        }
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Wide accumulation prevents component sums from wrapping the public count.
 */
extern FORCE_INLINE bool_t Utl_Url_GetByteCount(const Utl_Url_t* url, uint32_t* byteCount)
{
    bool_t   valid;
    uint64_t count;
    bool_t   literal;

    LOGASSERT(NULL != url);
    LOGASSERT(NULL != byteCount);

    valid = Utl_Url_IsValid(url);
    if (valid)
    {
        count  = url->scheme.byteCount;
        count += url->userInfo.byteCount;
        count += url->host.byteCount;
        count += url->port.byteCount;
        count += url->path.byteCount;
        count += url->query.byteCount;
        count += url->fragment.byteCount;
        if (url->hasScheme)
        {
            ++count;
        }
        if (url->hasAuthority)
        {
            count += 2;
        }
        if (url->hasUserInfo)
        {
            ++count;
        }
        literal = (UTL_URL_HOST_IPV6 == url->hostKind) || (UTL_URL_HOST_FUTURE == url->hostKind);
        if (literal)
        {
            count += 2;
        }
        if (url->hasPort)
        {
            ++count;
        }
        if (url->hasQuery)
        {
            ++count;
        }
        if (url->hasFragment)
        {
            ++count;
        }
        valid = (UINT32_MAX >= count);
        if (valid)
        {
            *byteCount = (uint32_t)count;
        }
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Decimal range is checked before multiply/add. A syntactically valid URI port
 *  can exceed this native representation and therefore fail this separate query.
 */
extern FORCE_INLINE bool_t Utl_Url_GetPort(const Utl_Url_t* url, uint16_t* port)
{
    bool_t   valid;
    uint32_t value;
    uint32_t index;
    uint32_t digit;

    LOGASSERT(NULL != url);
    LOGASSERT(NULL != port);
    LOGASSERT((NULL != url->port.data) || (0 == url->port.byteCount));

    valid = url->hasAuthority && url->hasPort && (0 < url->port.byteCount);
    value = 0;
    for (index = 0; valid && (index < url->port.byteCount); ++index)
    {
        valid = IsDigit(url->port.data[index]);
        if (valid)
        {
            digit = (uint32_t)(url->port.data[index] - UTL_URL_CHAR_DIGIT_FIRST);
            valid = (value <= ((UINT16_MAX - digit) / DECIMAL_RADIX));
            if (valid)
            {
                value = (value * DECIMAL_RADIX) + digit;
            }
        }
    }
    if (valid)
    {
        *port = (uint16_t)value;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The four octets contribute their decimal widths and exactly three periods.
 */
extern FORCE_INLINE uint32_t Utl_Url_GetIpv4ByteCount(uint32_t address)
{
    uint32_t count;
    uint32_t part;
    uint32_t shift;
    uint32_t value;
    count = IPV4_PART_COUNT - 1;
    for (part = 0; part < IPV4_PART_COUNT; ++part)
    {
        shift  = (IPV4_PART_COUNT - part - 1) * IPV4_PART_BITS;
        value  = (address >> shift) & IPV4_PART_MAX;
        count += Utl_Decimal_GetUint64ByteCount(value);
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Validation precedes all output publication. Decoding shrinks or preserves size.
 */
extern FORCE_INLINE bool_t Utl_Url_GetDecodedByteCount(Utl_Url_Text_t text, uint32_t* byteCount)
{
    uint32_t index;
    uint32_t count;
    bool_t   valid;
    uint32_t first;
    uint32_t second;

    LOGASSERT(NULL != byteCount);
    LOGASSERT((NULL != text.data) || (0 == text.byteCount));

    index = 0;
    count = 0;
    valid = TRUE;
    for (;;)
    {
        if ((!valid) || (index >= text.byteCount))
        {
            break;
        }
        if (UTL_URL_CHAR_PERCENT == text.data[index])
        {
            valid = (PERCENT_BYTE_COUNT <= (text.byteCount - index));
            if (valid)
            {
                first   = GetHexValue(text.data[index + 1]);
                second  = GetHexValue(text.data[index + 2]);
                valid   = (HEX_INVALID != first) && (HEX_INVALID != second);
                index  += PERCENT_BYTE_COUNT;
            }
        }
        else
        {
            ++index;
        }
        ++count;
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
 *  At most three digits per octet are accumulated before checking the octet bound.
 */
extern FORCE_INLINE bool_t Utl_Url_ReadIpv4(Utl_Url_Text_t text, uint32_t* address)
{
    uint32_t candidate;
    bool_t   valid;
    uint32_t offset;
    uint32_t part;
    uint32_t value;
    bool_t   has_next_part;
    bool_t   needs_separator;
    bool_t   at_end;

    LOGASSERT(NULL != address);
    AssertText(text);
    candidate = 0;
    valid     = TRUE;
    offset    = 0;
    for (part = 0; valid && (part < IPV4_PART_COUNT); ++part)
    {
        valid = ReadIpv4Octet(text, &offset, &value);
        if (valid)
        {
            candidate = (candidate << IPV4_PART_BITS) | value;
        }
        has_next_part   = ((part + 1) < IPV4_PART_COUNT);
        needs_separator = valid && has_next_part;
        if (needs_separator)
        {
            valid = (offset < text.byteCount);
            if (valid)
            {
                valid = (UTL_URL_CHAR_DOT == text.data[offset]);
                ++offset;
            }
        }
    }
    at_end = (offset == text.byteCount);
    valid  = valid && at_end;

    if (valid)
    {
        *address = candidate;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Structural splitting retains component boundaries before validating their
 *  grammars. The private candidate is published only after complete validation.
 */
bool_t Utl_Url_Read(Utl_Url_t* url, const byte_t* data, uint32_t byteCount)
{
    Utl_Url_t      candidate;
    uint32_t       offset;
    uint32_t       index;
    byte_t         value;
    Slice_t        slice;
    bool_t         valid;
    bool_t         authority;
    uint32_t       first;
    Utl_Url_Text_t text;

    LOGASSERT(NULL != url);
    LOGASSERT((NULL != data) || (0 == byteCount));

    Utl_Url_Initialize(&candidate);
    offset = 0;
    for (index = 0; index < byteCount; ++index)
    {
        value = data[index];
        if (UTL_URL_CHAR_COLON == value)
        {
            candidate.hasScheme = TRUE;
            slice.offset        = 0;
            slice.byteCount     = index;
            candidate.scheme    = GetText(data, slice);
            offset              = index + 1;
            break;
        }
        if ((UTL_URL_CHAR_SLASH == value) || (UTL_URL_CHAR_QUERY == value) || (UTL_URL_CHAR_FRAGMENT == value))
        {
            break;
        }
    }
    valid     = TRUE;
    authority = FALSE;
    if (2 <= (byteCount - offset))
    {
        authority = (UTL_URL_CHAR_SLASH == data[offset]) && (UTL_URL_CHAR_SLASH == data[offset + 1]);
    }
    if (authority)
    {
        candidate.hasAuthority  = TRUE;
        offset                 += 2;
        first                   = offset;
        for (; offset < byteCount; ++offset)
        {
            value = data[offset];
            if ((UTL_URL_CHAR_SLASH == value) || (UTL_URL_CHAR_QUERY == value) || (UTL_URL_CHAR_FRAGMENT == value))
            {
                break;
            }
        }
        slice.offset    = first;
        slice.byteCount = offset - first;
        text            = GetText(data, slice);
        valid           = ReadAuthority(&candidate, text);
    }
    if (valid)
    {
        slice.offset    = offset;
        slice.byteCount = byteCount - offset;
        text            = GetText(data, slice);
        ReadPath(&candidate, text);
        valid = Utl_Url_IsValid(&candidate);
    }
    if (valid)
    {
        *url = candidate;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Preflight guarantees each decimal write and separator fits before any output is changed.
 */
extern FORCE_INLINE uint32_t Utl_Url_WriteIpv4(uint32_t address, byte_t* output, uint32_t capacity)
{
    uint32_t count;
    uint32_t written;
    uint32_t part;
    uint32_t shift;
    uint32_t value;
    uint32_t digits;
    LOGASSERT((NULL != output) || (0 == capacity));
    count = Utl_Url_GetIpv4ByteCount(address);
    LOGASSERT(0 < count);
    written = 0;
    if (count <= capacity)
    {
        for (part = 0; part < IPV4_PART_COUNT; ++part)
        {
            if (0 < part)
            {
                output[written] = UTL_URL_CHAR_DOT;
                ++written;
            }
            shift  = (IPV4_PART_COUNT - part - 1) * IPV4_PART_BITS;
            value  = (address >> shift) & IPV4_PART_MAX;
            digits = Utl_Decimal_WriteUint64(value, output + written, capacity - written);
            LOGASSERT(0 < digits);
            written += digits;
        }
        LOGASSERT(count == written);
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  RFC 3986 section 5.3 delimiter recomposition, preserving the caller's syntax.
 */
extern FORCE_INLINE bool_t Utl_Url_Write(const Utl_Url_t* url, byte_t* output, uint32_t capacity, uint32_t* byteCount)
{
    bool_t   valid;
    uint32_t count;
    uint32_t offset;

    LOGASSERT(NULL != url);
    LOGASSERT(NULL != byteCount);
    LOGASSERT((NULL != output) || (0 == capacity));

    valid = Utl_Url_GetByteCount(url, &count);
    if (valid)
    {
        valid = (count <= capacity);
    }
    if (valid)
    {
        offset = 0;
        if (url->hasScheme)
        {
            WriteText(output, &offset, url->scheme);
            WriteCharacter(output, &offset, UTL_URL_CHAR_COLON);
        }
        if (url->hasAuthority)
        {
            WriteAuthority(url, output, &offset);
        }
        WriteText(output, &offset, url->path);
        if (url->hasQuery)
        {
            WriteCharacter(output, &offset, UTL_URL_CHAR_QUERY);
            WriteText(output, &offset, url->query);
        }
        if (url->hasFragment)
        {
            WriteCharacter(output, &offset, UTL_URL_CHAR_FRAGMENT);
            WriteText(output, &offset, url->fragment);
        }
        LOGASSERT(offset == count);
        *byteCount = count;
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Complete escape and capacity validation precedes the first destination write.
 *  Reserved characters must therefore be split into components before this call.
 */
extern FORCE_INLINE bool_t Utl_Url_WriteDecoded(Utl_Url_Text_t text, byte_t* output, uint32_t capacity, uint32_t* byteCount)
{
    bool_t   valid;
    uint32_t count;
    uint32_t offset;
    uint32_t index;
    uint32_t first;
    uint32_t second;

    LOGASSERT(NULL != byteCount);
    LOGASSERT((NULL != output) || (0 == capacity));
    LOGASSERT((NULL != text.data) || (0 == text.byteCount));

    valid = Utl_Url_GetDecodedByteCount(text, &count);
    if (valid)
    {
        valid = (count <= capacity);
    }
    if (valid && (NULL != output))
    {
        offset = 0;
        index  = 0;
        for (; index < text.byteCount; ++offset)
        {
            if (UTL_URL_CHAR_PERCENT == text.data[index])
            {
                first           = GetHexValue(text.data[index + 1]);
                second          = GetHexValue(text.data[index + 2]);
                output[offset]  = (byte_t)((first * HEX_RADIX) + second);
                index          += PERCENT_BYTE_COUNT;
            }
            else
            {
                output[offset] = (byte_t)text.data[index];
                ++index;
            }
        }
        LOGASSERT(offset == count);
    }
    if (valid)
    {
        *byteCount = count;
    }

    return valid;
}
