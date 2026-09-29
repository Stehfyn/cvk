#ifndef UTL_URL_H
#define UTL_URL_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * RFC 3986 sections 2 through 4 and 5.3: URI-reference grammar and recomposition.
 * https://www.rfc-editor.org/rfc/rfc3986.html
 * Components borrow counted ASCII URI syntax; percent escapes remain encoded.
 * This layer neither resolves names nor applies scheme-specific access policy.
 * Relative resolution, IRI/IDNA conversion and zone-identifier extensions are separate.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UTL_URL_IPV4_MIN_BYTES (7)    ///< Four single-digit octets and three separators.
#define UTL_URL_IPV4_MAX_BYTES (15)   ///< Four three-digit octets and three separators.
#define UTL_URL_CHAR_NUL       ('\0') ///< String-adapter terminator, absent from counted URI syntax.

/* RFC 3986 sections 2.1 through 2.3 and component delimiters from section 3. */
#define UTL_URL_CHAR_DIGIT_FIRST  ('0')
#define UTL_URL_CHAR_DIGIT_LAST   ('9')
#define UTL_URL_CHAR_UPPER_FIRST  ('A')
#define UTL_URL_CHAR_UPPER_LAST   ('Z')
#define UTL_URL_CHAR_LOWER_FIRST  ('a')
#define UTL_URL_CHAR_LOWER_LAST   ('z')
#define UTL_URL_CHAR_HEX_UPPER    ('F')
#define UTL_URL_CHAR_HEX_LOWER    ('f')
#define UTL_URL_CHAR_PERCENT      ('%')
#define UTL_URL_CHAR_COLON        (':')
#define UTL_URL_CHAR_SLASH        ('/')
#define UTL_URL_CHAR_QUERY        ('?')
#define UTL_URL_CHAR_FRAGMENT     ('#')
#define UTL_URL_CHAR_AT           ('@')
#define UTL_URL_CHAR_OPEN         ('[')
#define UTL_URL_CHAR_CLOSE        (']')
#define UTL_URL_CHAR_HYPHEN       ('-')
#define UTL_URL_CHAR_DOT          ('.')
#define UTL_URL_CHAR_UNDERSCORE   ('_')
#define UTL_URL_CHAR_TILDE        ('~')
#define UTL_URL_CHAR_EXCLAIM      ('!')
#define UTL_URL_CHAR_DOLLAR       ('$')
#define UTL_URL_CHAR_AMP          ('&')
#define UTL_URL_CHAR_QUOTE        ('\'')
#define UTL_URL_CHAR_LEFT         ('(')
#define UTL_URL_CHAR_RIGHT        (')')
#define UTL_URL_CHAR_STAR         ('*')
#define UTL_URL_CHAR_PLUS         ('+')
#define UTL_URL_CHAR_COMMA        (',')
#define UTL_URL_CHAR_SEMICOLON    (';')
#define UTL_URL_CHAR_EQUAL        ('=')
#define UTL_URL_CHAR_FUTURE       ('v')
#define UTL_URL_CHAR_FUTURE_UPPER ('V')

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Borrowed URI syntax bytes, without a terminating byte.
 */
typedef struct Utl_Url_Text_s
{
    const byte_t* data;      ///< Stable readable source; NULL only when byteCount is zero.
    uint32_t      byteCount; ///< Exact component extent, excluding delimiters.
} Utl_Url_Text_t;

/*************************************************************************************************/
/**
 * \brief Host grammar selected by RFC 3986 section 3.2.2.
 */
typedef enum Utl_Url_HostKind_e
{
    UTL_URL_HOST_NONE,
    UTL_URL_HOST_NAME,
    UTL_URL_HOST_IPV4,
    UTL_URL_HOST_IPV6,
    UTL_URL_HOST_FUTURE
} Utl_Url_HostKind_t;

/*************************************************************************************************/
/**
 * \brief URI-reference components with explicit presence for empty delimiters.
 *
 * \par Details:
 *  Scheme excludes ':', userInfo excludes '@', literal hosts exclude brackets,
 *  port excludes ':', query excludes '?' and fragment excludes '#'. Path retains
 *  its slashes. Presence flags distinguish absent components from empty ones.
 *  All borrowed bytes must remain alive and unchanged during queries and writes.
 */
typedef struct Utl_Url_s
{
    Utl_Url_Text_t     scheme;       ///< Scheme syntax when present.
    Utl_Url_Text_t     userInfo;     ///< Encoded authority user information.
    Utl_Url_Text_t     host;         ///< Encoded registered name or address literal content.
    Utl_Url_Text_t     port;         ///< Decimal syntax; empty is permitted by the generic grammar.
    Utl_Url_Text_t     path;         ///< Encoded path.
    Utl_Url_Text_t     query;        ///< Encoded query.
    Utl_Url_Text_t     fragment;     ///< Encoded fragment.
    Utl_Url_HostKind_t hostKind;     ///< Host grammar; NONE without authority.
    bool_t             hasScheme;    ///< A scheme delimiter was present.
    bool_t             hasAuthority; ///< The authority's leading '//' was present.
    bool_t             hasUserInfo;  ///< An authority '@' delimiter was present.
    bool_t             hasPort;      ///< An authority port delimiter was present.
    bool_t             hasQuery;     ///< A query delimiter was present.
    bool_t             hasFragment;  ///< A fragment delimiter was present.
} Utl_Url_t;

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Initializes an empty relative reference without acquiring resources.
 *
 * \par Details:
 *  A valid empty relative reference is established.
 *
 * \par Requirements:
 *  - 001 A missing result shall assert as a development error.
 *  - 002 Every component shall be empty and all presence flags shall be false.
 *  - 003 Initialization shall inspect no source bytes or acquire any resource.
 *
 * \param[out] url   Required object, exclusively owned
 *
 * \retval     None
 */
void Utl_Url_Initialize(Utl_Url_t* url);

/*************************************************************************************************/
/**
 * \brief Checks the complete generic grammar and component relationships.
 *
 * \par Requirements:
 *  - 001 Missing required model/span storage shall assert; malformed syntax shall return FALSE.
 *  - 002 Scheme-specific policies and native port-number ranges shall remain separate.
 *  - 003 Percent escapes shall be validated without decoding or normalizing components.
 *
 * \param[in] url   Required model with stable borrowed spans
 *
 * \retval    TRUE    Every component and delimiter relationship follows the generic grammar.
 * \retval    FALSE   The model contains invalid syntax or inconsistent presence fields.
 */
bool_t Utl_Url_IsValid(const Utl_Url_t* url);

/*************************************************************************************************/
/**
 * \brief Derives the exact recomposed byte count without a terminator.
 *
 * \par Requirements:
 *  - 001 Required input/output pointers shall assert; count storage shall be disjoint from the model.
 *  - 002 Invalid grammar or a total beyond UINT32_MAX shall preserve the output count.
 *
 * \param[in]  url         Required model with stable borrowed spans
 * \param[out] byteCount   Required exact-count output
 *
 * \retval     TRUE    The exact count, possibly zero, is published.
 * \retval     FALSE   No count is published.
 */
bool_t Utl_Url_GetByteCount(const Utl_Url_t* url, uint32_t* byteCount);

/*************************************************************************************************/
/**
 * \brief Derives a native 16-bit port from nonempty decimal port syntax.
 *
 * \par Requirements:
 *  - 001 Required pointers shall assert; caller output shall be disjoint from the model.
 *  - 002 Absent, empty, nondecimal or out-of-range port syntax shall preserve output.
 *  - 003 No default port shall be inferred; that decision belongs to the scheme consumer.
 *
 * \param[in]  url    Required model
 * \param[out] port   Required disjoint port output
 *
 * \retval     TRUE    A value in 0 through 65535 is published.
 * \retval     FALSE   No native port value is available.
 */
bool_t Utl_Url_GetPort(const Utl_Url_t* url, uint16_t* port);

/*************************************************************************************************/
/**
 * \brief Derives the canonical dotted-decimal byte count of an IPv4 address.
 *
 * \par Requirements:
 *  - 001 The numeric address shall place the first octet in bits 31 through 24, independent of host byte order.
 *  - 002 Every numeric address shall have exactly four decimal octets with no leading zeroes.
 *
 * \param[in] address   Numeric IPv4 address
 *
 * \retval    7..15   Exact byte count without a terminator.
 */
uint32_t Utl_Url_GetIpv4ByteCount(uint32_t address);

/*************************************************************************************************/
/**
 * \brief Derives the byte count after decoding every percent escape in one component.
 *
 * \par Requirements:
 *  - 001 Required count and nonempty source storage shall assert.
 *  - 002 Missing or invalid hex digits shall preserve output count.
 *  - 003 Literal bytes, including '+', shall remain literal; form-urlencoded rules shall not apply.
 *
 * \param[in]  text        Counted component syntax
 * \param[out] byteCount   Required disjoint count output
 *
 * \retval     TRUE    The decoded size is published.
 * \retval     FALSE   A malformed percent escape was encountered.
 */
bool_t Utl_Url_GetDecodedByteCount(Utl_Url_Text_t text, uint32_t* byteCount);

/*************************************************************************************************/
/**
 * \brief Reads a complete RFC 3986 IPv4address into an explicit numeric value.
 *
 * \par Requirements:
 *  - 001 Required result and nonempty source storage shall assert on misuse.
 *  - 002 Exactly four decimal octets in 0 through 255 shall be required; extra bytes and leading zeroes shall fail.
 *  - 003 Failure shall preserve address; success shall publish its first octet in bits 31 through 24.
 *  - 004 No allocation, native call or CRT operation shall occur.
 *
 * \param[in]  text      Exact immutable source span
 * \param[out] address   Disjoint numeric result
 *
 * \retval     TRUE    A complete address is published.
 * \retval     FALSE   Invalid syntax; output is unchanged.
 */
bool_t Utl_Url_ReadIpv4(Utl_Url_Text_t text, uint32_t* address);

/*************************************************************************************************/
/**
 * \brief Parses a complete counted URI-reference, preserving encoded component bytes.
 *
 * \par Requirements:
 *  - 001 Required result and nonempty source storage shall assert; result storage shall be disjoint.
 *  - 002 Invalid or incomplete syntax shall preserve every result field.
 *  - 003 Absolute and relative references, empty components and literal host forms shall be distinguished.
 *  - 004 No source mutation, allocation, CRT call, OS call or retained global state shall occur.
 *  - 005 Raw non-ASCII bytes shall fail URI grammar; IRI conversion shall occur before this operation.
 *
 * \param[out] url         Required disjoint result
 * \param[in]  data        Stable counted URI syntax; NULL only at zero byteCount
 * \param[in]  byteCount   Complete readable input extent
 *
 * \retval     TRUE    A complete borrowed model is published.
 * \retval     FALSE   The caller's model is unchanged.
 */
bool_t Utl_Url_Read(Utl_Url_t* url, const byte_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Writes the canonical dotted-decimal form of a numeric IPv4 address.
 *
 * \par Requirements:
 *  - 001 Nonempty output storage shall assert when absent.
 *  - 002 Complete sizing shall precede output writes; insufficient capacity shall preserve output.
 *  - 003 Output shall have four decimal octets, no leading zeroes and no implicit terminator.
 *
 * \param[in]  address    Numeric address, most significant octet first
 * \param[out] output     Disjoint writable bytes
 * \param[in]  capacity   Writable extent
 *
 * \retval     7..15   Complete bytes written.
 * \retval     0       Capacity is insufficient; output is unchanged.
 */
uint32_t Utl_Url_WriteIpv4(uint32_t address, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Recomposes the model without normalization or a trailing terminator.
 *
 * \par Requirements:
 *  - 001 Required model/count and nonempty output storage shall assert.
 *  - 002 Output and count storage shall be disjoint from the model and all its borrowed spans.
 *  - 003 Invalid grammar, count overflow or insufficient capacity shall preserve all outputs.
 *  - 004 Case, escapes, empty delimiters and host brackets shall be preserved by recomposition.
 *
 * \param[in]  url         Required model
 * \param[out] output      Writable extent, NULL only when capacity is zero
 * \param[in]  capacity    Writable bytes
 * \param[out] byteCount   Required disjoint exact-count output
 *
 * \retval     TRUE    The exact URI bytes and count were published.
 * \retval     FALSE   All outputs are unchanged.
 */
bool_t Utl_Url_Write(const Utl_Url_t* url, byte_t* output, uint32_t capacity, uint32_t* byteCount);

/*************************************************************************************************/
/**
 * \brief Decodes percent-encoded bytes after the caller has separated URI components.
 *
 * \par Requirements:
 *  - 001 Required count and nonempty source/output storage shall assert.
 *  - 002 Source, output and count storage shall be disjoint.
 *  - 003 Malformed escapes or insufficient capacity shall preserve all outputs.
 *  - 004 Decoded NUL and other octets shall be preserved as bytes; text validation belongs to the caller.
 *  - 005 No terminator or form-urlencoded '+' conversion shall be appended or applied.
 *
 * \param[in]  text        Counted encoded component
 * \param[out] output      Writable byte extent, NULL only when capacity is zero
 * \param[in]  capacity    Writable bytes
 * \param[out] byteCount   Required disjoint exact-count output
 *
 * \retval     TRUE    Complete decoded bytes and their count were published.
 * \retval     FALSE   All outputs are unchanged.
 */
bool_t Utl_Url_WriteDecoded(Utl_Url_Text_t text, byte_t* output, uint32_t capacity, uint32_t* byteCount);

#endif /* UTL_URL_H */
