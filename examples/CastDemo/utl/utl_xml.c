/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * XML 1.0 Fifth Edition: characters, names, markup, references and normalization.
 * https://www.w3.org/TR/2008/REC-xml-20081126/
 * Namespaces in XML 1.0 Third Edition: expanded names, scope and uniqueness.
 * https://www.w3.org/TR/xml-names/
 * Caller-owned bounded node/attribute records borrow immutable source bytes.
 * The current document input is UTF-8; other encodings and DTD processing have
 * explicit unsupported results and remain separate unfinished responsibilities.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_xml.h"
#include "utl/utl_unicode.h"
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

#define XML_CHAR_MAX         (0x10FFFF)
#define XML_CHAR_BMP_FIRST   (0x20)
#define XML_CHAR_BMP_LAST    (0xD7FF)
#define XML_CHAR_HIGH_FIRST  (0xE000)
#define XML_CHAR_HIGH_LAST   (0xFFFD)
#define XML_CHAR_SUPPLEMENT  (0x10000)
#define XML_DECIMAL_RADIX    (10)
#define XML_HEX_RADIX        (16)
#define XML_HEX_LETTER       (10)
#define XML_DIGIT_INVALID    (16)
#define XML_COMMENT_OPEN     ("<!--")
#define XML_COMMENT_DASHES   ("--")
#define XML_CDATA_OPEN       ("<![CDATA[")
#define XML_CDATA_CLOSE      ("]]>")
#define XML_PI_OPEN          ("<?")
#define XML_PI_CLOSE         ("?>")
#define XML_END_OPEN         ("</")
#define XML_DTD_OPEN         ("<!DOCTYPE")
#define XML_VERSION_NAME     ("version")
#define XML_VERSION          ("1.0")
#define XML_ENCODING_NAME    ("encoding")
#define XML_ENCODING_UTF8    ("UTF-8")
#define XML_STANDALONE_NAME  ("standalone")
#define XML_YES              ("yes")
#define XML_NO               ("no")
#define XML_UTF8_BOM_FIRST   (0xEF)
#define XML_UTF8_BOM_SECOND  (0xBB)
#define XML_UTF8_BOM_THIRD   (0xBF)
#define XML_UTF16_BOM_FIRST  (0xFE)
#define XML_UTF16_BOM_SECOND (0xFF)
#define XML_UTF8_BOM_BYTES   (3)
#define XML_UTF16_BOM_BYTES  (2)

#define XML_REFERENCE_AMP  ("amp;")
#define XML_REFERENCE_LT   ("lt;")
#define XML_REFERENCE_GT   ("gt;")
#define XML_REFERENCE_QUOT ("quot;")
#define XML_REFERENCE_APOS ("apos;")

#define XML_ASCII_UPPER_FIRST ('A')
#define XML_ASCII_UPPER_LAST  ('Z')
#define XML_ASCII_CASE_DELTA  ('a' - 'A')

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Inclusive Unicode grammar interval. */
typedef struct Range_s
{
    uint32_t first; ///< Inclusive lower scalar bound.
    uint32_t last;  ///< Inclusive upper scalar bound.
} Range_t;

/*************************************************************************************************/
/** \brief Byte span and current offset, always bounded by the source extent. */
typedef struct Cursor_s
{
    Utl_Xml_Text_t source; ///< Immutable counted source and its interpretation.
    uint32_t       offset; ///< Next unread byte, never beyond source.byteCount.
} Cursor_t;

/*************************************************************************************************/
/** \brief Private in-progress document state; source records publish only at completion. */
typedef struct Reader_s
{
    Utl_Xml_Document_t* document;           ///< Owner of the bounded scratch records.
    Cursor_t            cursor;             ///< Current source position.
    Utl_Xml_Node_t*     parent;             ///< Most recently opened unclosed element.
    Utl_Xml_Node_t*     root;               ///< Candidate root, unpublished until complete.
    Utl_Xml_Result_t    result;             ///< Complete or the explicit rejection category.
    bool_t              declarationAllowed; ///< No content beyond an optional BOM has been consumed.
} Reader_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Unicode intervals for XML 1.0 Fifth Edition NameStartChar, production [4].
 *
 * \par Details:
 *  https://www.w3.org/TR/2008/REC-xml-20081126/#NT-NameStartChar
 */
static const Range_t c_nameStart[] = {
    {':', ':'},
    {'A', 'Z'},
    {'_', '_'},
    {'a', 'z'},
    {0xC0, 0xD6},
    {0xD8, 0xF6},
    {0xF8, 0x2FF},
    {0x370, 0x37D},
    {0x37F, 0x1FFF},
    {0x200C, 0x200D},
    {0x2070, 0x218F},
    {0x2C00, 0x2FEF},
    {0x3001, 0xD7FF},
    {0xF900, 0xFDCF},
    {0xFDF0, 0xFFFD},
    {0x10000, 0xEFFFF},
};
/*************************************************************************************************/
/**
 * \brief Additional Unicode intervals for XML 1.0 Fifth Edition NameChar, production [4a].
 *
 * \par Details:
 *  https://www.w3.org/TR/2008/REC-xml-20081126/#NT-NameChar
 */
static const Range_t c_nameAdditional[] = {
    {'-', '-'},
    {'.', '.'},
    {'0', '9'},
    {0xB7, 0xB7},
    {0x300, 0x36F},
    {0x203F, 0x2040},
};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Asserts the caller-owned text representation.
 *
 * \par Requirements:
 *  - 001 Nonempty text shall have readable storage and every text kind shall be defined.
 *  - 002 Malformed input bytes shall remain ordinary parse failures.
 *
 * \param[in] text   Borrowed representation to inspect
 *
 * \retval    None   Representation requirements hold or the assertion fails.
 */
static FORCE_INLINE void AssertText(Utl_Xml_Text_t text);

/*************************************************************************************************/
/**
 * \brief Constructs a literal counted comparison value.
 *
 * \par Requirements:
 *  - 001 Data shall retain byteCount readable bytes.
 *
 * \param[in] data        Stable bytes
 * \param[in] byteCount   Exact extent
 *
 * \retval    text   Borrowed literal.
 */
static FORCE_INLINE Utl_Xml_Text_t GetLiteral(const byte_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Borrows the already-consumed interval of a bounded cursor.
 *
 * \par Requirements:
 *  - 001 First shall not exceed the current offset; source storage shall remain alive.
 *
 * \param[in] cursor   Valid cursor
 * \param[in] first    Slice start
 *
 * \retval    text   Borrowed literal slice.
 */
static FORCE_INLINE Utl_Xml_Text_t GetSlice(const Cursor_t* cursor, uint32_t first);

/*************************************************************************************************/
/**
 * \brief Gets the next source octet or the forbidden XML zero sentinel.
 *
 * \par Requirements:
 *  - 001 Offset shall be within the counted source extent.
 *
 * \param[in] cursor   Readable source and bounded position
 *
 * \retval    value   Octet or zero at end.
 */
static FORCE_INLINE uint32_t GetByte(const Cursor_t* cursor);

/*************************************************************************************************/
/**
 * \brief Compares counted encoded names without normalization.
 *
 * \par Requirements:
 *  - 001 Nonempty spans shall have stable storage.
 *
 * \param[in] first    First span
 * \param[in] second   Second span
 *
 * \retval    TRUE    Exact byte equality.
 * \retval    FALSE   Counts or encoded bytes differ.
 */
static FORCE_INLINE bool_t IsRawEqual(Utl_Xml_Text_t first, Utl_Xml_Text_t second);

/*************************************************************************************************/
/**
 * \brief Checks a fixed XML token at the current position.
 *
 * \par Requirements:
 *  - 001 Literal shall have byteCount readable bytes; cursor shall be bounded.
 *
 * \param[in] cursor      Current input
 * \param[in] literal     Token bytes
 * \param[in] byteCount   Token extent
 *
 * \retval    TRUE    Complete token equality.
 * \retval    FALSE   The token differs or the remaining source is too short.
 */
static FORCE_INLINE bool_t Matches(const Cursor_t* cursor, const byte_t* literal, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Classifies XML production S.
 *
 * \par Requirements:
 *  - 001 Only tab, CR, LF and space shall match.
 *
 * \param[in] value   Candidate scalar
 *
 * \retval    TRUE    XML whitespace.
 * \retval    FALSE   The value is not XML whitespace.
 */
static FORCE_INLINE bool_t IsWhitespace(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Classifies XML NameStartChar using the specification ranges.
 *
 * \par Requirements:
 *  - 001 Every XML 1.0 Fifth Edition NameStartChar interval shall be checked.
 *
 * \param[in] value   Candidate scalar
 *
 * \retval    TRUE    NameStartChar membership.
 * \retval    FALSE   The value cannot begin an XML Name.
 */
static FORCE_INLINE bool_t IsNameStart(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Classifies XML NameChar.
 *
 * \par Requirements:
 *  - 001 NameStartChar and its specified additional intervals shall match.
 *
 * \param[in] value   Candidate scalar
 *
 * \retval    TRUE    NameChar membership.
 * \retval    FALSE   The value cannot continue an XML Name.
 */
static FORCE_INLINE bool_t IsNameChar(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Derives a hexadecimal digit value.
 *
 * \par Requirements:
 *  - 001 Both letter cases shall match; invalid bytes shall be distinguishable.
 *
 * \param[in] value   Candidate ASCII scalar
 *
 * \retval    digit   Zero through fifteen, or XML_DIGIT_INVALID.
 */
static FORCE_INLINE uint32_t GetDigit(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Compares fixed ASCII declaration tokens without case sensitivity.
 *
 * \par Requirements:
 *  - 001 Both spans shall be readable; only ASCII upper-case letters shall fold.
 *
 * \param[in] first    First token
 * \param[in] second   Second token
 *
 * \retval    TRUE    ASCII-insensitive equality.
 * \retval    FALSE   Lengths or folded bytes differ.
 */
static FORCE_INLINE bool_t IsAsciiEqual(Utl_Xml_Text_t first, Utl_Xml_Text_t second);

/*************************************************************************************************/
/**
 * \brief Splits an XML Name into namespace prefix and NCName local part.
 *
 * \par Requirements:
 *  - 001 Name shall already satisfy XML Name.
 *  - 002 At most one colon shall occur, with nonempty valid NCNames on each side.
 *
 * \param[in]  name     Valid XML Name
 * \param[out] prefix   Empty or prefix span
 * \param[out] local    Local name span
 *
 * \retval     TRUE    QName constraints are satisfied.
 * \retval     FALSE   QName constraints fail; output spans shall not be consumed.
 */
static FORCE_INLINE bool_t SplitName(Utl_Xml_Text_t name, Utl_Xml_Text_t* prefix, Utl_Xml_Text_t* local);

/*************************************************************************************************/
/**
 * \brief Resolves one prefix using the nearest declaration in ancestor scope.
 *
 * \par Requirements:
 *  - 001 Nodes and declaration attributes shall be parser-validated.
 *  - 002 Unprefixed names without a default declaration shall resolve to no namespace.
 *  - 003 Unbound nonempty prefixes shall fail.
 *
 * \param[in]  node     Current element
 * \param[in]  prefix   QName prefix
 * \param[out] space    Resolved normalized URI span
 *
 * \retval     TRUE    A binding or permitted empty default namespace was found.
 * \retval     FALSE   A nonempty prefix is unbound; space is empty.
 */
static FORCE_INLINE bool_t GetNamespace(const Utl_Xml_Node_t* node, Utl_Xml_Text_t prefix, Utl_Xml_Text_t* space);

/*************************************************************************************************/
/**
 * \brief Resolves an ordinary attribute and rejects duplicate expanded names.
 *
 * \par Requirements:
 *  - 001 Earlier attributes shall already have resolved names.
 *  - 002 The default namespace shall not apply to unprefixed attributes.
 *
 * \param[in]     node        Current element
 * \param[in,out] attribute   Current attribute
 *
 * \retval        TRUE    Expanded name is defined and unique.
 * \retval        FALSE   The unpublished attribute has an invalid or duplicate expanded name.
 */
static FORCE_INLINE bool_t SetAttributeNamespace(const Utl_Xml_Node_t* node, Utl_Xml_Attribute_t* attribute);

/*************************************************************************************************/
/**
 * \brief Validates all declarations before resolving names on a complete start tag.
 *
 * \par Requirements:
 *  - 001 Parent elements shall already have valid namespaces.
 *  - 002 Element xmlns prefixes, unresolved prefixes and duplicate expanded attributes shall fail.
 *
 * \param[in,out] node   Unpublished complete start tag
 *
 * \retval        TRUE    Namespace constraints are satisfied.
 * \retval        FALSE   The unpublished element or one of its attributes violates namespace constraints.
 */
static FORCE_INLINE bool_t SetNamespaces(Utl_Xml_Node_t* node);

/*************************************************************************************************/
/**
 * \brief Initializes and links the next retained record without allocating storage.
 *
 * \par Requirements:
 *  - 001 Node capacity shall be checked before accessing a record.
 *  - 002 Only one root element shall be accepted; text requires an enclosing element.
 *
 * \param[in,out] reader   Private parse state
 * \param[in]     kind     Element or text role
 *
 * \retval        node   Initialized record.
 * \retval        NULL   Capacity or root constraint failure.
 */
static FORCE_INLINE Utl_Xml_Node_t* AppendNode(Reader_t* reader, Utl_Xml_NodeKind_t kind);

/*************************************************************************************************/
/**
 * \brief Reads an attribute into the next retained fixed slot.
 *
 * \par Requirements:
 *  - 001 Capacity shall be checked before touching the slot.
 *  - 002 Duplicate encoded names shall fail before linking.
 *
 * \param[in,out] reader   Private parse state
 * \param[in,out] node     Current element
 *
 * \retval        TRUE    One attribute was linked.
 * \retval        FALSE   Capacity or syntax fails; the attribute is not linked.
 */
static FORCE_INLINE bool_t AppendAttribute(Reader_t* reader, Utl_Xml_Node_t* node);

/*************************************************************************************************/
/**
 * \brief Consumes XML whitespace within a source bound.
 *
 * \par Requirements:
 *  - 001 No byte beyond the source shall be read.
 *
 * \param[in,out] cursor   Bounded position
 *
 * \retval        None   Position advances past whitespace.
 */
static FORCE_INLINE void ReadWhitespace(Cursor_t* cursor);

/*************************************************************************************************/
/**
 * \brief Reads a decimal or hexadecimal character reference after its hash.
 *
 * \par Requirements:
 *  - 001 Range shall be checked before arithmetic; at least one digit and a semicolon are required.
 *  - 002 Only XML Char values shall publish a scalar.
 *
 * \param[in,out] cursor   Position after hash
 * \param[out]    scalar   Disjoint result
 *
 * \retval        TRUE    Complete XML character reference.
 * \retval        FALSE   A digit, range, character or terminator constraint fails; scalar is unchanged.
 */
static FORCE_INLINE bool_t ReadNumberReference(Cursor_t* cursor, uint32_t* scalar);

/*************************************************************************************************/
/**
 * \brief Reads a predefined entity or numeric reference.
 *
 * \par Requirements:
 *  - 001 Input shall begin with ampersand.
 *  - 002 Unknown entities shall fail; no external lookup or expansion occurs.
 *
 * \param[in,out] cursor   Current reference
 * \param[out]    scalar   Disjoint scalar result
 *
 * \retval        TRUE    Complete supported reference.
 * \retval        FALSE   The reference is incomplete, unknown or invalid; scalar is unchanged.
 */
static FORCE_INLINE bool_t ReadReference(Cursor_t* cursor, uint32_t* scalar);

/*************************************************************************************************/
/**
 * \brief Reads one literal source scalar and applies XML whitespace normalization.
 *
 * \par Requirements:
 *  - 001 Caller shall distinguish references and markup before consuming literal text.
 *  - 002 The scalar shall satisfy XML Char and strict UTF-8 encoding.
 *  - 003 Nonliteral CRLF/CR shall become LF; raw attribute whitespace shall then become space.
 *
 * \param[in,out] cursor   Exclusive counted source position
 * \param[out]    scalar   Decoded value, unchanged on failure
 *
 * \retval        TRUE    One complete literal scalar was consumed.
 * \retval        FALSE   No complete permitted scalar is present; cursor and scalar are unchanged.
 */
static FORCE_INLINE bool_t ReadLiteralScalar(Cursor_t* cursor, uint32_t* scalar);

/*************************************************************************************************/
/**
 * \brief Reads one normalized scalar according to the span interpretation.
 *
 * \par Requirements:
 *  - 001 Source shall be nonempty at the current position.
 *  - 002 Raw line endings shall normalize before attribute whitespace replacement.
 *  - 003 Character references shall preserve their referenced whitespace values.
 *
 * \param[in,out] cursor   Bounded span cursor
 * \param[out]    scalar   Disjoint scalar result
 *
 * \retval        TRUE    A complete permitted scalar was read.
 * \retval        FALSE   The next source is malformed text; scalar is unchanged.
 */
static FORCE_INLINE bool_t ReadTextScalar(Cursor_t* cursor, uint32_t* scalar);

/*************************************************************************************************/
/**
 * \brief Reads one complete XML Name without consuming its following delimiter.
 *
 * \par Requirements:
 *  - 001 UTF-8 source shall remain bounded; at least one NameStartChar is required.
 *
 * \param[in,out] cursor   Source position
 * \param[out]    name     Borrowed name on success
 *
 * \retval        TRUE    A complete nonempty name was read.
 * \retval        FALSE   No valid nonempty name is present; name is unchanged.
 */
static FORCE_INLINE bool_t ReadName(Cursor_t* cursor, Utl_Xml_Text_t* name);

/*************************************************************************************************/
/**
 * \brief Reads one name, equals sign and quoted attribute value.
 *
 * \par Requirements:
 *  - 001 Attribute output shall be unpublished scratch.
 *  - 002 Malformed references and forbidden raw characters shall fail.
 *
 * \param[in,out] cursor      Attribute start
 * \param[out]    attribute   Private scratch record
 *
 * \retval        TRUE    Complete attribute syntax is established.
 * \retval        FALSE   The attribute is incomplete or malformed; scratch output shall not be consumed.
 */
static FORCE_INLINE bool_t ReadAttribute(Cursor_t* cursor, Utl_Xml_Attribute_t* attribute);

/*************************************************************************************************/
/**
 * \brief Classifies a namespace declaration and validates reserved bindings.
 *
 * \par Requirements:
 *  - 001 Name shall satisfy XML Name; value shall already be validated attribute text.
 *  - 002 Only xml may bind the XML namespace; no prefix may bind the xmlns namespace.
 *  - 003 A prefixed namespace shall not be undeclared.
 *
 * \param[in,out] attribute   Parsed attribute record
 *
 * \retval        TRUE    Declaration constraints or ordinary QName are valid.
 * \retval        FALSE   QName or reserved-binding constraints fail; scratch output is unusable.
 */
static FORCE_INLINE bool_t ReadNamespaceDeclaration(Utl_Xml_Attribute_t* attribute);

/*************************************************************************************************/
/**
 * \brief Reads a bounded span up to a complete fixed terminator.
 *
 * \par Requirements:
 *  - 001 The nonempty terminator shall be a stable literal.
 *  - 002 Missing termination shall fail at the input end.
 *
 * \param[in,out] cursor       Content start
 * \param[in]     terminator   Required closing token
 * \param[out]    text         Borrowed content
 *
 * \retval        TRUE    Closing token was consumed.
 * \retval        FALSE   No terminator is present; cursor reaches the input end and text is unchanged.
 */
static FORCE_INLINE bool_t ReadUntil(Cursor_t* cursor, Utl_Xml_Text_t terminator, Utl_Xml_Text_t* text);

/*************************************************************************************************/
/**
 * \brief Validates and skips one XML comment.
 *
 * \par Requirements:
 *  - 001 Cursor shall point at the comment opener.
 *  - 002 An interior double hyphen or incomplete close shall fail.
 *
 * \param[in,out] reader   Private parse state
 *
 * \retval        TRUE    Complete valid comment.
 * \retval        FALSE   The comment has an interior double hyphen or incomplete close.
 */
static FORCE_INLINE bool_t ReadComment(Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Retains one CDATA content span inside an element.
 *
 * \par Requirements:
 *  - 001 Cursor shall point at the CDATA opener.
 *  - 002 CDATA outside the root element shall fail.
 *
 * \param[in,out] reader   Private parse state
 *
 * \retval        TRUE    Complete CDATA node is staged.
 * \retval        FALSE   CDATA is outside an element, incomplete or exceeds node capacity.
 */
static FORCE_INLINE bool_t ReadCdata(Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads the required leading VersionInfo production.
 *
 * \par Requirements:
 *  - 001 Required leading whitespace and the version pseudo-attribute shall be present.
 *  - 002 Unsupported XML versions shall be distinguished from missing or malformed syntax.
 *
 * \param[in,out] reader   Position immediately after the xml target
 *
 * \retval        TRUE    XML 1.0 VersionInfo was consumed.
 * \retval        FALSE   VersionInfo is malformed or the declared version is unsupported.
 */
static FORCE_INLINE bool_t ReadVersionDeclaration(Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Validates XML version, optional encoding and optional standalone declaration.
 *
 * \par Requirements:
 *  - 001 Version shall appear first; encoding may precede standalone only.
 *  - 002 Only XML 1.0 UTF-8 input is currently supported; unsupported declarations shall be explicit.
 *
 * \param[in,out] reader   Position immediately after the xml target
 *
 * \retval        TRUE    The complete declaration was consumed.
 * \retval        FALSE   Declaration order, syntax, encoding or version is invalid or unsupported.
 */
static FORCE_INLINE bool_t ReadDeclaration(Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads a processing instruction or the one leading XML declaration.
 *
 * \par Requirements:
 *  - 001 The xml target is reserved in every case combination.
 *  - 002 Only a lower-case xml declaration at the beginning may use the reserved target.
 *  - 003 Namespace-aware processing-instruction targets shall be NCNames, without a colon.
 *
 * \param[in,out] reader   Position at processing-instruction opener
 *
 * \retval        TRUE    Complete instruction syntax was consumed.
 * \retval        FALSE   The instruction target, placement or termination is invalid.
 */
static FORCE_INLINE bool_t ReadInstruction(Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads one element start or empty-element tag into fixed records.
 *
 * \par Requirements:
 *  - 001 Attributes require separating whitespace.
 *  - 002 Namespace validation shall precede use as a parent scope.
 *
 * \param[in,out] reader   Position at opening less-than
 *
 * \retval        TRUE    Complete start tag is staged.
 * \retval        FALSE   The tag violates syntax, namespace, root or fixed-capacity constraints.
 */
static FORCE_INLINE bool_t ReadStart(Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Validates an end tag against the current open element.
 *
 * \par Requirements:
 *  - 001 A current parent is required.
 *  - 002 End QName bytes shall exactly match the opening QName.
 *
 * \param[in,out] reader   Position at end-tag opener
 *
 * \retval        TRUE    Matching parent was closed.
 * \retval        FALSE   No current element exists or the closing QName/terminator is invalid.
 */
static FORCE_INLINE bool_t ReadEnd(Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads character data or top-level whitespace.
 *
 * \par Requirements:
 *  - 001 Only XML whitespace may occur outside the root.
 *  - 002 Complete reference and CharData validation shall precede retention.
 *
 * \param[in,out] reader   Position outside markup
 *
 * \retval        TRUE    Character data was consumed.
 * \retval        FALSE   Character data, top-level placement or fixed capacity is invalid.
 */
static FORCE_INLINE bool_t ReadContent(Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Dispatches one XML markup production.
 *
 * \par Requirements:
 *  - 001 Cursor shall point at less-than.
 *  - 002 Unsupported DTD input shall fail explicitly without external I/O.
 *
 * \param[in,out] reader   Private parse state
 *
 * \retval        TRUE    One complete markup production was consumed.
 * \retval        FALSE   Markup is invalid, unsupported or exceeds retained record capacity.
 */
static FORCE_INLINE bool_t ReadMarkup(Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Validates all source scalars before syntactic parsing.
 *
 * \par Requirements:
 *  - 001 Malformed UTF-8 or forbidden XML characters shall report their starting offset.
 *  - 002 UTF-16 signatures shall produce the explicit unsupported result.
 *
 * \param[in,out] reader   Cursor initially at zero
 *
 * \retval        TRUE    Complete UTF-8 XML character stream.
 * \retval        FALSE   Source is malformed or unsupported; rejection position/category is retained.
 */
static FORCE_INLINE bool_t ReadSource(Reader_t* reader);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Source syntax is validated separately so caller misuse cannot become an input error.
 */
static FORCE_INLINE void AssertText(Utl_Xml_Text_t text)
{
    bool_t literal;
    bool_t content;
    bool_t attribute;
    bool_t cdata;
    bool_t valid_kind;
    LOGASSERT((NULL != text.data) || (0 == text.byteCount));
    literal    = (UTL_XML_TEXT_LITERAL == text.kind);
    content    = (UTL_XML_TEXT_CONTENT == text.kind);
    attribute  = (UTL_XML_TEXT_ATTRIBUTE == text.kind);
    cdata      = (UTL_XML_TEXT_CDATA == text.kind);
    valid_kind = literal || content || attribute || cdata;
    LOGASSERT(valid_kind);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Xml_Text_t GetLiteral(const byte_t* data, uint32_t byteCount)
{
    Utl_Xml_Text_t text;
    text.data      = data;
    text.byteCount = byteCount;
    text.kind      = UTL_XML_TEXT_LITERAL;
    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No pointer arithmetic occurs on an absent empty source.
 */
static FORCE_INLINE Utl_Xml_Text_t GetSlice(const Cursor_t* cursor, uint32_t first)
{
    Utl_Xml_Text_t text;
    LOGASSERT(first <= cursor->offset);
    text = cursor->source;
    if (0 < first)
    {
        text.data = cursor->source.data + first;
    }
    text.byteCount = cursor->offset - first;
    text.kind      = UTL_XML_TEXT_LITERAL;
    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Zero is not a valid XML character and cannot hide an accepted source byte.
 */
static FORCE_INLINE uint32_t GetByte(const Cursor_t* cursor)
{
    uint32_t value;
    LOGASSERT(cursor->offset <= cursor->source.byteCount);
    value = 0;
    if (cursor->offset < cursor->source.byteCount)
    {
        value = (byte_t)cursor->source.data[cursor->offset];
    }
    return value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  UTF-8 has a unique shortest encoding for each accepted scalar sequence.
 */
static FORCE_INLINE bool_t IsRawEqual(Utl_Xml_Text_t first, Utl_Xml_Text_t second)
{
    bool_t equal;
    equal = (first.byteCount == second.byteCount);
    if (equal && (0 < first.byteCount))
    {
        equal = (0 == CRT_Memory_Compare(first.data, second.data, first.byteCount));
    }
    return equal;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Partial token input never matches.
 */
static FORCE_INLINE bool_t Matches(const Cursor_t* cursor, const byte_t* literal, uint32_t byteCount)
{
    bool_t   matches;
    uint32_t index;
    matches = (byteCount <= (cursor->source.byteCount - cursor->offset));
    for (index = 0; matches && (index < byteCount); ++index)
    {
        matches = (literal[index] == cursor->source.data[cursor->offset + index]);
    }
    return matches;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsWhitespace(uint32_t value)
{
    bool_t horizontal;
    bool_t vertical;
    horizontal = (UTL_XML_CHAR_TAB == value) || (UTL_XML_CHAR_SPACE == value);
    vertical   = (UTL_XML_CHAR_CR == value) || (UTL_XML_CHAR_LF == value);
    return horizontal || vertical;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsNameStart(uint32_t value)
{
    bool_t   matches;
    uint32_t index;
    matches = FALSE;
    for (index = 0; index < (sizeof(c_nameStart) / sizeof(Range_t)); ++index)
    {
        matches = (c_nameStart[index].first <= value) && (c_nameStart[index].last >= value);
        if (matches)
        {
            break;
        }
    }
    return matches;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsNameChar(uint32_t value)
{
    bool_t   matches;
    uint32_t index;
    matches = IsNameStart(value);
    for (index = 0; (!matches) && (index < (sizeof(c_nameAdditional) / sizeof(Range_t))); ++index)
    {
        matches = (c_nameAdditional[index].first <= value) && (c_nameAdditional[index].last >= value);
    }
    return matches;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetDigit(uint32_t value)
{
    bool_t   decimal;
    bool_t   lower;
    bool_t   upper;
    uint32_t digit;
    decimal = (UTL_XML_CHAR_ZERO <= value) && (UTL_XML_CHAR_NINE >= value);
    lower   = (UTL_XML_CHAR_HEX_A <= value) && (UTL_XML_CHAR_HEX_F >= value);
    upper   = (UTL_XML_CHAR_HEX_UPPER_A <= value) && (UTL_XML_CHAR_HEX_UPPER_F >= value);
    digit   = XML_DIGIT_INVALID;
    if (decimal)
    {
        digit = value - UTL_XML_CHAR_ZERO;
    }
    else if (lower)
    {
        digit = value - UTL_XML_CHAR_HEX_A + XML_HEX_LETTER;
    }
    else if (upper)
    {
        digit = value - UTL_XML_CHAR_HEX_UPPER_A + XML_HEX_LETTER;
    }
    return digit;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsAsciiEqual(Utl_Xml_Text_t first, Utl_Xml_Text_t second)
{
    bool_t   equal;
    uint32_t index;
    uint32_t left;
    uint32_t right;
    equal = (first.byteCount == second.byteCount);
    for (index = 0; equal && (index < first.byteCount); ++index)
    {
        left  = (byte_t)first.data[index];
        right = (byte_t)second.data[index];
        if ((XML_ASCII_UPPER_FIRST <= left) && (XML_ASCII_UPPER_LAST >= left))
        {
            left += XML_ASCII_CASE_DELTA;
        }
        if ((XML_ASCII_UPPER_FIRST <= right) && (XML_ASCII_UPPER_LAST >= right))
        {
            right += XML_ASCII_CASE_DELTA;
        }
        equal = (left == right);
    }
    return equal;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A digit after a colon cannot begin the local NCName.
 */
static FORCE_INLINE bool_t SplitName(Utl_Xml_Text_t name, Utl_Xml_Text_t* prefix, Utl_Xml_Text_t* local)
{
    uint32_t colon;
    bool_t   valid;
    uint32_t index;
    uint32_t scalar;
    uint32_t width;
    colon = name.byteCount;
    valid = (0 < name.byteCount);
    for (index = 0; valid && (index < name.byteCount); ++index)
    {
        if (UTL_XML_CHAR_COLON == name.data[index])
        {
            valid = (colon == name.byteCount) && (0 < index) && ((index + 1) < name.byteCount);
            colon = index;
        }
    }
    if (valid)
    {
        *prefix = GetLiteral(NULL, 0);
        *local  = name;
        if (colon < name.byteCount)
        {
            prefix->data      = name.data;
            prefix->byteCount = colon;
            local->data       = name.data + colon + 1;
            local->byteCount  = name.byteCount - colon - 1;
            valid             = Utl_Unicode_ReadUtf8(&scalar, &width, (const byte_t*)local->data, local->byteCount);
            if (valid)
            {
                valid = IsNameStart(scalar);
            }
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The xml binding is implicit and cannot be shadowed by invalid declarations.
 */
static FORCE_INLINE bool_t GetNamespace(const Utl_Xml_Node_t* node, Utl_Xml_Text_t prefix, Utl_Xml_Text_t* space)
{
    bool_t                     found;
    const Utl_Xml_Node_t*      ancestor;
    uint32_t                   level;
    const Utl_Xml_Attribute_t* attribute;
    uint32_t                   index;
    Utl_Xml_Text_t             declared;
    bool_t                     matches;
    bool_t                     valid;
    *space = GetLiteral(NULL, 0);
    found  = IsRawEqual(prefix, GetLiteral((const byte_t*)UTL_XML_PREFIX_XML, sizeof(UTL_XML_PREFIX_XML) - 1));
    if (found)
    {
        *space = GetLiteral((const byte_t*)UTL_XML_NAMESPACE_XML, sizeof(UTL_XML_NAMESPACE_XML) - 1);
    }
    ancestor = node;
    for (level = 0; (!found) && (level <= node->depth); ++level)
    {
        LOGASSERT(NULL != ancestor);
        attribute = ancestor->firstAttribute;
        for (index = 0; index < ancestor->attributeCount; ++index)
        {
            LOGASSERT(NULL != attribute);
            if (attribute->declaration)
            {
                declared = attribute->expanded.local;
                if (IsRawEqual(attribute->name,
                    GetLiteral((const byte_t*)UTL_XML_PREFIX_XMLNS, sizeof(UTL_XML_PREFIX_XMLNS) - 1)))
                {
                    declared = GetLiteral(NULL, 0);
                }
                matches = IsRawEqual(declared, prefix);
                if (matches)
                {
                    *space = attribute->value;
                    found  = TRUE;
                    break;
                }
            }
            attribute = attribute->next;
        }
        ancestor = ancestor->parent;
    }
    valid = found || (0 == prefix.byteCount);
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Namespace declarations were resolved in a separate first pass.
 */
static FORCE_INLINE bool_t SetAttributeNamespace(const Utl_Xml_Node_t* node, Utl_Xml_Attribute_t* attribute)
{
    bool_t                     valid;
    Utl_Xml_Text_t             prefix;
    const Utl_Xml_Attribute_t* previous;
    uint32_t                   index;
    bool_t                     duplicate;
    valid = TRUE;
    if (!attribute->declaration)
    {
        valid                     = SplitName(attribute->name, &prefix, &attribute->expanded.local);
        attribute->expanded.space = GetLiteral(NULL, 0);
        if (valid && (0 < prefix.byteCount))
        {
            valid = GetNamespace(node, prefix, &attribute->expanded.space);
        }
        previous = node->firstAttribute;
        for (index = 0; valid && (index < node->attributeCount); ++index)
        {
            LOGASSERT(NULL != previous);
            if (previous == attribute)
            {
                break;
            }
            duplicate = IsRawEqual(previous->expanded.local, attribute->expanded.local);
            if (duplicate)
            {
                duplicate = Utl_Xml_IsTextEqual(previous->expanded.space, attribute->expanded.space);
            }
            valid    = !duplicate;
            previous = previous->next;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Declaration position within an attribute list cannot change prefix scope.
 */
static FORCE_INLINE bool_t SetNamespaces(Utl_Xml_Node_t* node)
{
    Utl_Xml_Text_t       prefix;
    bool_t               valid;
    Utl_Xml_Attribute_t* attribute;
    uint32_t             index;
    prefix    = GetLiteral(NULL, 0);
    valid     = TRUE;
    attribute = node->firstAttribute;
    for (index = 0; valid && (index < node->attributeCount); ++index)
    {
        LOGASSERT(NULL != attribute);
        valid     = ReadNamespaceDeclaration(attribute);
        attribute = attribute->next;
    }
    if (valid)
    {
        valid = SplitName(node->name, &prefix, &node->expanded.local);
    }
    if (valid)
    {
        valid = !IsRawEqual(prefix, GetLiteral((const byte_t*)UTL_XML_PREFIX_XMLNS, sizeof(UTL_XML_PREFIX_XMLNS) - 1));
    }
    if (valid)
    {
        valid = GetNamespace(node, prefix, &node->expanded.space);
    }
    attribute = node->firstAttribute;
    for (index = 0; valid && (index < node->attributeCount); ++index)
    {
        LOGASSERT(NULL != attribute);
        valid     = SetAttributeNamespace(node, attribute);
        attribute = attribute->next;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Backing records are unpublished scratch until the whole document succeeds.
 */
static FORCE_INLINE Utl_Xml_Node_t* AppendNode(Reader_t* reader, Utl_Xml_NodeKind_t kind)
{
    Utl_Xml_Document_t* document;
    Utl_Xml_Node_t*     node;
    bool_t              fits;
    bool_t              valid_root;
    document   = reader->document;
    node       = NULL;
    fits       = (document->nodeCount < document->storage.nodeCapacity);
    valid_root = (NULL != reader->parent) || ((NULL == reader->root) && (UTL_XML_ELEMENT == kind));
    if (!valid_root)
    {
        reader->result = UTL_XML_INVALID;
    }
    else if (!fits)
    {
        reader->result = UTL_XML_CAPACITY;
    }
    else
    {
        node = &document->storage.nodes[document->nodeCount];
        CRT_Memory_Set(node, 0, sizeof(Utl_Xml_Node_t));
        node->kind   = kind;
        node->parent = reader->parent;
        ++document->nodeCount;
        if (NULL == reader->parent)
        {
            reader->root = node;
        }
        else
        {
            node->depth = reader->parent->depth + 1;
            if (NULL == reader->parent->lastChild)
            {
                reader->parent->firstChild = node;
            }
            else
            {
                reader->parent->lastChild->next = node;
            }
            reader->parent->lastChild = node;
            ++reader->parent->childCount;
        }
    }
    return node;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Parsed attributes remain private until complete document publication.
 */
static FORCE_INLINE bool_t AppendAttribute(Reader_t* reader, Utl_Xml_Node_t* node)
{
    Utl_Xml_Document_t*        document;
    bool_t                     valid;
    Utl_Xml_Attribute_t*       attribute;
    const Utl_Xml_Attribute_t* previous;
    uint32_t                   index;
    document = reader->document;
    valid    = (document->attributeCount < document->storage.attributeCapacity);
    if (!valid)
    {
        reader->result = UTL_XML_CAPACITY;
    }
    else
    {
        attribute = &document->storage.attributes[document->attributeCount];
        valid     = ReadAttribute(&reader->cursor, attribute);
        previous  = node->firstAttribute;
        for (index = 0; valid && (index < node->attributeCount); ++index)
        {
            LOGASSERT(NULL != previous);
            valid    = !IsRawEqual(previous->name, attribute->name);
            previous = previous->next;
        }
        if (valid)
        {
            if (NULL == node->lastAttribute)
            {
                node->firstAttribute = attribute;
            }
            else
            {
                node->lastAttribute->next = attribute;
            }
            node->lastAttribute = attribute;
            ++node->attributeCount;
            ++document->attributeCount;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void ReadWhitespace(Cursor_t* cursor)
{
    for (;;)
    {
        if (!IsWhitespace(GetByte(cursor)))
        {
            break;
        }
        ++cursor->offset;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The XML maximum bounds every multiply and addition.
 */
static FORCE_INLINE bool_t ReadNumberReference(Cursor_t* cursor, uint32_t* scalar)
{
    uint32_t radix;
    uint32_t first;
    uint32_t value;
    bool_t   valid;
    uint32_t digit;
    radix = XML_DECIMAL_RADIX;
    if (UTL_XML_CHAR_HEX == GetByte(cursor))
    {
        radix = XML_HEX_RADIX;
        ++cursor->offset;
    }
    first = cursor->offset;
    value = 0;
    valid = TRUE;
    for (;;)
    {
        digit = GetDigit(GetByte(cursor));
        if ((!valid) || (digit >= radix))
        {
            break;
        }
        valid = (value <= ((XML_CHAR_MAX - digit) / radix));
        if (valid)
        {
            value = (value * radix) + digit;
            ++cursor->offset;
        }
    }
    valid = valid && (first < cursor->offset) && (UTL_XML_CHAR_SEMICOLON == GetByte(cursor));
    valid = valid && Utl_Xml_IsChar(value);
    if (valid)
    {
        ++cursor->offset;
        *scalar = value;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  DTD-defined entities remain unsupported rather than becoming literal text.
 */
static FORCE_INLINE bool_t ReadReference(Cursor_t* cursor, uint32_t* scalar)
{
    bool_t valid;
    LOGASSERT(UTL_XML_CHAR_AMP == GetByte(cursor));
    ++cursor->offset;
    valid = TRUE;
    if (UTL_XML_CHAR_HASH == GetByte(cursor))
    {
        ++cursor->offset;
        valid = ReadNumberReference(cursor, scalar);
    }
    else if (Matches(cursor, (const byte_t*)XML_REFERENCE_AMP, sizeof(XML_REFERENCE_AMP) - 1))
    {
        cursor->offset += sizeof(XML_REFERENCE_AMP) - 1;
        *scalar        = UTL_XML_CHAR_AMP;
    }
    else if (Matches(cursor, (const byte_t*)XML_REFERENCE_LT, sizeof(XML_REFERENCE_LT) - 1))
    {
        cursor->offset += sizeof(XML_REFERENCE_LT) - 1;
        *scalar        = UTL_XML_CHAR_LT;
    }
    else if (Matches(cursor, (const byte_t*)XML_REFERENCE_GT, sizeof(XML_REFERENCE_GT) - 1))
    {
        cursor->offset += sizeof(XML_REFERENCE_GT) - 1;
        *scalar        = UTL_XML_CHAR_GT;
    }
    else if (Matches(cursor, (const byte_t*)XML_REFERENCE_QUOT, sizeof(XML_REFERENCE_QUOT) - 1))
    {
        cursor->offset += sizeof(XML_REFERENCE_QUOT) - 1;
        *scalar        = UTL_XML_CHAR_QUOT;
    }
    else if (Matches(cursor, (const byte_t*)XML_REFERENCE_APOS, sizeof(XML_REFERENCE_APOS) - 1))
    {
        cursor->offset += sizeof(XML_REFERENCE_APOS) - 1;
        *scalar        = UTL_XML_CHAR_APOS;
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
 *  Character-reference values bypass this stage and retain referenced CR, LF and tab.
 */
static FORCE_INLINE bool_t ReadLiteralScalar(Cursor_t* cursor, uint32_t* scalar)
{
    bool_t   valid;
    uint32_t value;
    uint32_t width;
    bool_t   normalized;
    bool_t   attribute_space;
    valid = Utl_Unicode_ReadUtf8(&value,
                                 &width,
                                 (const byte_t*)cursor->source.data + cursor->offset,
                                 cursor->source.byteCount - cursor->offset);
    if (valid)
    {
        valid = Utl_Xml_IsChar(value);
    }
    if (valid)
    {
        cursor->offset += width;
        normalized     = (UTL_XML_TEXT_LITERAL != cursor->source.kind);
        if (normalized && (UTL_XML_CHAR_CR == value))
        {
            if (UTL_XML_CHAR_LF == GetByte(cursor))
            {
                ++cursor->offset;
            }
            value = UTL_XML_CHAR_LF;
        }
        attribute_space = (UTL_XML_TEXT_ATTRIBUTE == cursor->source.kind) && IsWhitespace(value);
        if (attribute_space)
        {
            value = UTL_XML_CHAR_SPACE;
        }
        *scalar = value;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Literal spans represent comparison strings and names; they do not expand references.
 */
static FORCE_INLINE bool_t ReadTextScalar(Cursor_t* cursor, uint32_t* scalar)
{
    bool_t   escaped;
    bool_t   content;
    bool_t   terminator;
    uint32_t value;
    bool_t   raw_markup;
    bool_t   valid;
    escaped    = (UTL_XML_TEXT_CONTENT == cursor->source.kind) || (UTL_XML_TEXT_ATTRIBUTE == cursor->source.kind);
    content    = (UTL_XML_TEXT_CONTENT == cursor->source.kind) || (UTL_XML_TEXT_CDATA == cursor->source.kind);
    terminator = content && Matches(cursor, (const byte_t*)XML_CDATA_CLOSE, sizeof(XML_CDATA_CLOSE) - 1);
    value      = GetByte(cursor);
    raw_markup = escaped && (UTL_XML_CHAR_LT == value);
    if (terminator || raw_markup)
    {
        valid = FALSE;
    }
    else if (escaped && (UTL_XML_CHAR_AMP == value))
    {
        valid = ReadReference(cursor, scalar);
    }
    else
    {
        valid = ReadLiteralScalar(cursor, scalar);
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Unicode name characters follow the Fifth Edition ranges.
 */
static FORCE_INLINE bool_t ReadName(Cursor_t* cursor, Utl_Xml_Text_t* name)
{
    uint32_t first;
    bool_t   valid;
    uint32_t scalar;
    uint32_t width;
    bool_t   member;
    first = cursor->offset;
    valid = TRUE;
    for (;;)
    {
        if ((!valid) || (cursor->offset >= cursor->source.byteCount))
        {
            break;
        }
        valid = Utl_Unicode_ReadUtf8(&scalar,
                                     &width,
                                     (const byte_t*)cursor->source.data + cursor->offset,
                                     cursor->source.byteCount - cursor->offset);
        if (valid)
        {
            member = IsNameChar(scalar);
            if (first == cursor->offset)
            {
                member = IsNameStart(scalar);
            }
            if (!member)
            {
                break;
            }
            cursor->offset += width;
        }
    }
    valid = valid && (first < cursor->offset);
    if (valid)
    {
        *name = GetSlice(cursor, first);
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Both quote styles are accepted; a referenced quote is data.
 */
static FORCE_INLINE bool_t ReadAttribute(Cursor_t* cursor, Utl_Xml_Attribute_t* attribute)
{
    bool_t   valid;
    uint32_t quote;
    uint32_t first;
    uint32_t count;
    CRT_Memory_Set(attribute, 0, sizeof(Utl_Xml_Attribute_t));
    valid = ReadName(cursor, &attribute->name);
    if (valid)
    {
        ReadWhitespace(cursor);
        valid = (UTL_XML_CHAR_EQ == GetByte(cursor));
    }
    if (valid)
    {
        ++cursor->offset;
        ReadWhitespace(cursor);
        quote = GetByte(cursor);
        valid = (UTL_XML_CHAR_QUOT == quote) || (UTL_XML_CHAR_APOS == quote);
        if (valid)
        {
            ++cursor->offset;
            first = cursor->offset;
            for (; cursor->offset < cursor->source.byteCount; ++cursor->offset)
            {
                if (quote == GetByte(cursor))
                {
                    break;
                }
            }
            valid = (cursor->offset < cursor->source.byteCount);
            if (valid)
            {
                attribute->value      = GetSlice(cursor, first);
                attribute->value.kind = UTL_XML_TEXT_ATTRIBUTE;
                ++cursor->offset;
                valid = Utl_Xml_GetTextByteCount(attribute->value, &count);
            }
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Default namespace undeclaration remains valid; xmlns itself cannot be declared.
 */
static FORCE_INLINE bool_t ReadNamespaceDeclaration(Utl_Xml_Attribute_t* attribute)
{
    bool_t         valid;
    Utl_Xml_Text_t prefix;
    Utl_Xml_Text_t local;
    Utl_Xml_Text_t xml;
    Utl_Xml_Text_t xmlns;
    bool_t         default_name;
    bool_t         prefixed_name;
    bool_t         reserved_xml;
    bool_t         xml_space;
    bool_t         xmlns_space;
    valid = SplitName(attribute->name, &prefix, &local);
    xml   = GetLiteral((const byte_t*)UTL_XML_PREFIX_XML, sizeof(UTL_XML_PREFIX_XML) - 1);
    xmlns = GetLiteral((const byte_t*)UTL_XML_PREFIX_XMLNS, sizeof(UTL_XML_PREFIX_XMLNS) - 1);
    if (valid)
    {
        default_name           = (0 == prefix.byteCount) && IsRawEqual(local, xmlns);
        prefixed_name          = IsRawEqual(prefix, xmlns);
        attribute->declaration = default_name || prefixed_name;
        if (attribute->declaration)
        {
            reserved_xml = prefixed_name && IsRawEqual(local, xml);
            xml_space    = Utl_Xml_IsTextEqual(
                attribute->value,
                GetLiteral((const byte_t*)UTL_XML_NAMESPACE_XML, sizeof(UTL_XML_NAMESPACE_XML) - 1));
            xmlns_space = Utl_Xml_IsTextEqual(
                attribute->value,
                GetLiteral((const byte_t*)UTL_XML_NAMESPACE_XMLNS, sizeof(UTL_XML_NAMESPACE_XMLNS) - 1));
            valid = (!xmlns_space) && (reserved_xml == xml_space);
            if (prefixed_name)
            {
                valid = valid && (!IsRawEqual(local, xmlns)) && (0 < attribute->value.byteCount);
            }
            attribute->expanded.space =
                GetLiteral((const byte_t*)UTL_XML_NAMESPACE_XMLNS, sizeof(UTL_XML_NAMESPACE_XMLNS) - 1);
            attribute->expanded.local = local;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No token prefix at the end is accepted as a complete delimiter.
 */
static FORCE_INLINE bool_t ReadUntil(Cursor_t* cursor, Utl_Xml_Text_t terminator, Utl_Xml_Text_t* text)
{
    uint32_t first;
    bool_t   valid;
    first = cursor->offset;
    valid = FALSE;
    for (; cursor->offset < cursor->source.byteCount; ++cursor->offset)
    {
        valid = Matches(cursor, terminator.data, terminator.byteCount);
        if (valid)
        {
            *text          = GetSlice(cursor, first);
            cursor->offset += terminator.byteCount;
            break;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The first double hyphen must be followed immediately by the closing greater-than sign.
 */
static FORCE_INLINE bool_t ReadComment(Reader_t* reader)
{
    bool_t         valid;
    Utl_Xml_Text_t ignored;
    reader->cursor.offset += sizeof(XML_COMMENT_OPEN) - 1;
    valid                 = ReadUntil(&reader->cursor,
                                      GetLiteral((const byte_t*)XML_COMMENT_DASHES, sizeof(XML_COMMENT_DASHES) - 1),
                                      &ignored);
    if (valid)
    {
        valid = (UTL_XML_CHAR_GT == GetByte(&reader->cursor));
        if (valid)
        {
            ++reader->cursor.offset;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Entity-looking bytes remain literal in CDATA; line endings normalize when consumed.
 */
static FORCE_INLINE bool_t ReadCdata(Reader_t* reader)
{
    Utl_Xml_Text_t  text;
    bool_t          valid;
    Utl_Xml_Node_t* node;
    text                  = GetLiteral(NULL, 0);
    valid                 = (NULL != reader->parent);
    reader->cursor.offset += sizeof(XML_CDATA_OPEN) - 1;
    if (valid)
    {
        valid =
            ReadUntil(&reader->cursor, GetLiteral((const byte_t*)XML_CDATA_CLOSE, sizeof(XML_CDATA_CLOSE) - 1), &text);
    }
    if (valid)
    {
        node  = AppendNode(reader, UTL_XML_TEXT);
        valid = (NULL != node);
        if (valid)
        {
            node->text      = text;
            node->text.kind = UTL_XML_TEXT_CDATA;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  VersionInfo must precede encoding and standalone pseudo-attributes.
 */
static FORCE_INLINE bool_t ReadVersionDeclaration(Reader_t* reader)
{
    bool_t              valid;
    Utl_Xml_Attribute_t attribute;
    valid = IsWhitespace(GetByte(&reader->cursor));
    ReadWhitespace(&reader->cursor);
    if (valid)
    {
        valid = ReadAttribute(&reader->cursor, &attribute);
    }
    if (valid)
    {
        valid = IsRawEqual(attribute.name, GetLiteral((const byte_t*)XML_VERSION_NAME, sizeof(XML_VERSION_NAME) - 1));
    }
    if (valid)
    {
        valid = IsRawEqual(attribute.value, GetLiteral((const byte_t*)XML_VERSION, sizeof(XML_VERSION) - 1));
        if (!valid)
        {
            reader->result = UTL_XML_UNSUPPORTED;
        }
    }

    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Declaration pseudo-attributes do not permit character references.
 */
static FORCE_INLINE bool_t ReadDeclaration(Reader_t* reader)
{
    bool_t              valid;
    bool_t              encoding_seen;
    bool_t              standalone_seen;
    uint32_t            first;
    Utl_Xml_Attribute_t attribute;
    bool_t              encoding;
    bool_t              standalone;
    bool_t              enabled;
    bool_t              disabled;
    valid           = ReadVersionDeclaration(reader);
    encoding_seen   = FALSE;
    standalone_seen = FALSE;
    for (;;)
    {
        first = reader->cursor.offset;
        ReadWhitespace(&reader->cursor);
        if ((!valid) || Matches(&reader->cursor, (const byte_t*)XML_PI_CLOSE, sizeof(XML_PI_CLOSE) - 1))
        {
            break;
        }
        valid = (first < reader->cursor.offset);
        if (valid)
        {
            valid = ReadAttribute(&reader->cursor, &attribute);
        }
        if (valid)
        {
            encoding =
                IsRawEqual(attribute.name, GetLiteral((const byte_t*)XML_ENCODING_NAME, sizeof(XML_ENCODING_NAME) - 1));
            standalone = IsRawEqual(attribute.name,
                                    GetLiteral((const byte_t*)XML_STANDALONE_NAME, sizeof(XML_STANDALONE_NAME) - 1));
            if (encoding && (!encoding_seen) && (!standalone_seen))
            {
                valid = IsAsciiEqual(attribute.value,
                                     GetLiteral((const byte_t*)XML_ENCODING_UTF8, sizeof(XML_ENCODING_UTF8) - 1));
                encoding_seen = TRUE;
                if (!valid)
                {
                    reader->result = UTL_XML_UNSUPPORTED;
                }
            }
            else if (standalone && (!standalone_seen))
            {
                enabled         = IsRawEqual(attribute.value, GetLiteral((const byte_t*)XML_YES, sizeof(XML_YES) - 1));
                disabled        = IsRawEqual(attribute.value, GetLiteral((const byte_t*)XML_NO, sizeof(XML_NO) - 1));
                valid           = enabled || disabled;
                standalone_seen = TRUE;
            }
            else
            {
                valid = FALSE;
            }
        }
    }
    if (valid)
    {
        reader->cursor.offset += sizeof(XML_PI_CLOSE) - 1;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Unknown instructions are validated and ignored as UPnP requires.
 */
static FORCE_INLINE bool_t ReadInstruction(Reader_t* reader)
{
    bool_t         valid;
    Utl_Xml_Text_t target;
    Utl_Xml_Text_t prefix;
    Utl_Xml_Text_t local;
    Utl_Xml_Text_t xml;
    bool_t         declaration;
    Utl_Xml_Text_t ignored;
    reader->cursor.offset += sizeof(XML_PI_OPEN) - 1;
    valid                 = ReadName(&reader->cursor, &target);
    if (valid)
    {
        valid = SplitName(target, &prefix, &local);
        valid = valid && (0 == prefix.byteCount);
    }
    xml = GetLiteral((const byte_t*)UTL_XML_PREFIX_XML, sizeof(UTL_XML_PREFIX_XML) - 1);
    if (valid)
    {
        declaration = IsAsciiEqual(target, xml);
        if (declaration)
        {
            valid = reader->declarationAllowed && IsRawEqual(target, xml);
            if (valid)
            {
                valid = ReadDeclaration(reader);
            }
        }
        else
        {
            valid = IsWhitespace(GetByte(&reader->cursor)) ||
                    Matches(&reader->cursor, (const byte_t*)XML_PI_CLOSE, sizeof(XML_PI_CLOSE) - 1);
            if (valid)
            {
                valid = ReadUntil(&reader->cursor,
                                  GetLiteral((const byte_t*)XML_PI_CLOSE, sizeof(XML_PI_CLOSE) - 1),
                                  &ignored);
            }
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A second document root fails before it can replace the first root.
 */
static FORCE_INLINE bool_t ReadStart(Reader_t* reader)
{
    Utl_Xml_Node_t* node;
    bool_t          valid;
    uint32_t        first;
    uint32_t        value;
    bool_t          empty;
    ++reader->cursor.offset;
    node  = AppendNode(reader, UTL_XML_ELEMENT);
    valid = (NULL != node);
    if (valid)
    {
        valid = ReadName(&reader->cursor, &node->name);
    }
    for (;;)
    {
        first = reader->cursor.offset;
        ReadWhitespace(&reader->cursor);
        value = GetByte(&reader->cursor);
        if ((!valid) || (UTL_XML_CHAR_GT == value) || (UTL_XML_CHAR_SLASH == value))
        {
            break;
        }
        valid = (first < reader->cursor.offset);
        if (valid)
        {
            valid = AppendAttribute(reader, node);
        }
    }
    empty = (UTL_XML_CHAR_SLASH == value);
    if (valid)
    {
        if (empty)
        {
            ++reader->cursor.offset;
        }
        valid = (UTL_XML_CHAR_GT == GetByte(&reader->cursor));
    }
    if (valid)
    {
        ++reader->cursor.offset;
        valid = SetNamespaces(node);
    }
    if (valid && (!empty))
    {
        reader->parent = node;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Namespace aliases do not permit different opening and closing prefix spellings.
 */
static FORCE_INLINE bool_t ReadEnd(Reader_t* reader)
{
    Utl_Xml_Text_t name;
    bool_t         valid;
    name                  = GetLiteral(NULL, 0);
    valid                 = (NULL != reader->parent);
    reader->cursor.offset += sizeof(XML_END_OPEN) - 1;
    if (valid)
    {
        valid = ReadName(&reader->cursor, &name);
    }
    if (valid)
    {
        valid = IsRawEqual(name, reader->parent->name);
    }
    if (valid)
    {
        ReadWhitespace(&reader->cursor);
        valid = (UTL_XML_CHAR_GT == GetByte(&reader->cursor));
    }
    if (valid)
    {
        ++reader->cursor.offset;
        reader->parent = reader->parent->parent;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Retained spans preserve source spelling until a normalized output is requested.
 */
static FORCE_INLINE bool_t ReadContent(Reader_t* reader)
{
    uint32_t        first;
    bool_t          valid;
    uint32_t        value;
    Utl_Xml_Text_t  text;
    uint32_t        count;
    Utl_Xml_Node_t* node;
    first = reader->cursor.offset;
    valid = TRUE;
    for (; valid && (reader->cursor.offset < reader->cursor.source.byteCount); ++reader->cursor.offset)
    {
        value = GetByte(&reader->cursor);
        if (UTL_XML_CHAR_LT == value)
        {
            break;
        }
        if (NULL == reader->parent)
        {
            valid = IsWhitespace(value);
        }
    }
    if (valid && (NULL != reader->parent))
    {
        text      = GetSlice(&reader->cursor, first);
        text.kind = UTL_XML_TEXT_CONTENT;
        valid     = Utl_Xml_GetTextByteCount(text, &count);
        if (valid)
        {
            node  = AppendNode(reader, UTL_XML_TEXT);
            valid = (NULL != node);
            if (valid)
            {
                node->text = text;
            }
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadMarkup(Reader_t* reader)
{
    bool_t valid;
    if (Matches(&reader->cursor, (const byte_t*)XML_COMMENT_OPEN, sizeof(XML_COMMENT_OPEN) - 1))
    {
        valid = ReadComment(reader);
    }
    else if (Matches(&reader->cursor, (const byte_t*)XML_CDATA_OPEN, sizeof(XML_CDATA_OPEN) - 1))
    {
        valid = ReadCdata(reader);
    }
    else if (Matches(&reader->cursor, (const byte_t*)XML_PI_OPEN, sizeof(XML_PI_OPEN) - 1))
    {
        valid = ReadInstruction(reader);
    }
    else if (Matches(&reader->cursor, (const byte_t*)XML_END_OPEN, sizeof(XML_END_OPEN) - 1))
    {
        valid = ReadEnd(reader);
    }
    else if (Matches(&reader->cursor, (const byte_t*)XML_DTD_OPEN, sizeof(XML_DTD_OPEN) - 1))
    {
        reader->result = UTL_XML_UNSUPPORTED;
        valid          = FALSE;
    }
    else
    {
        valid = ReadStart(reader);
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only one leading UTF-8 signature is consumed before parsing markup.
 */
static FORCE_INLINE bool_t ReadSource(Reader_t* reader)
{
    Cursor_t scan;
    bool_t   valid;
    bool_t   little;
    bool_t   big;
    uint32_t scalar;
    uint32_t width;
    scan  = reader->cursor;
    valid = TRUE;
    if (XML_UTF16_BOM_BYTES <= scan.source.byteCount)
    {
        little = ((byte_t)scan.source.data[0] == XML_UTF16_BOM_SECOND) &&
                 ((byte_t)scan.source.data[1] == XML_UTF16_BOM_FIRST);
        big = ((byte_t)scan.source.data[0] == XML_UTF16_BOM_FIRST) &&
                 ((byte_t)scan.source.data[1] == XML_UTF16_BOM_SECOND);
        if (little || big)
        {
            reader->result = UTL_XML_UNSUPPORTED;
            valid          = FALSE;
        }
    }
    for (;;)
    {
        if ((!valid) || (scan.offset >= scan.source.byteCount))
        {
            break;
        }
        valid = Utl_Unicode_ReadUtf8(&scalar,
                                     &width,
                                     (const byte_t*)scan.source.data + scan.offset,
                                     scan.source.byteCount - scan.offset);
        if (valid)
        {
            valid = Utl_Xml_IsChar(scalar);
        }
        if (valid)
        {
            scan.offset += width;
        }
    }
    if (!valid)
    {
        reader->cursor.offset = scan.offset;
    }
    else if (XML_UTF8_BOM_BYTES <= scan.source.byteCount)
    {
        if (((byte_t)scan.source.data[0] == XML_UTF8_BOM_FIRST) &&
            ((byte_t)scan.source.data[1] == XML_UTF8_BOM_SECOND) && ((byte_t)scan.source.data[2] == XML_UTF8_BOM_THIRD))
        {
            reader->cursor.offset = XML_UTF8_BOM_BYTES;
        }
    }
    return valid;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Initialization does not initialize unused record slots.
 */
void Utl_Xml_InitializeDocument(Utl_Xml_Document_t* document, Utl_Xml_Storage_t storage)
{
    LOGASSERT(NULL != document);
    LOGASSERT((NULL != storage.nodes) || (0 == storage.nodeCapacity));
    LOGASSERT((NULL != storage.attributes) || (0 == storage.attributeCapacity));
    document->storage        = storage;
    document->root           = NULL;
    document->nodeCount      = 0;
    document->attributeCount = 0;
    document->errorOffset    = 0;
    document->result         = UTL_XML_EMPTY;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Discouraged but permitted values remain accepted.
 */
bool_t Utl_Xml_IsChar(uint32_t value)
{
    bool_t whitespace;
    bool_t lower;
    bool_t higher;
    bool_t supplement;
    whitespace = IsWhitespace(value);
    lower      = (XML_CHAR_BMP_FIRST <= value) && (XML_CHAR_BMP_LAST >= value);
    higher     = (XML_CHAR_HIGH_FIRST <= value) && (XML_CHAR_HIGH_LAST >= value);
    supplement = (XML_CHAR_SUPPLEMENT <= value) && (XML_CHAR_MAX >= value);
    return whitespace || lower || higher || supplement;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Comparison is case-sensitive and performs no Unicode normalization.
 */
bool_t Utl_Xml_IsTextEqual(Utl_Xml_Text_t first, Utl_Xml_Text_t second)
{
    uint32_t right_value;
    Cursor_t left;
    Cursor_t right;
    bool_t   equal;
    uint32_t left_value;
    AssertText(first);
    AssertText(second);
    right_value  = 0;
    left.source  = first;
    left.offset  = 0;
    right.source = second;
    right.offset = 0;
    equal        = TRUE;
    for (;;)
    {
        if ((!equal) || (left.offset >= first.byteCount) || (right.offset >= second.byteCount))
        {
            break;
        }
        equal = ReadTextScalar(&left, &left_value);
        if (equal)
        {
            equal = ReadTextScalar(&right, &right_value);
        }
        if (equal)
        {
            equal = (left_value == right_value);
        }
    }
    equal = equal && (left.offset == first.byteCount) && (right.offset == second.byteCount);
    return equal;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Text nodes never match an element name.
 */
bool_t Utl_Xml_IsElement(const Utl_Xml_Node_t* node, Utl_Xml_Name_t name)
{
    bool_t matches;
    LOGASSERT(NULL != node);
    AssertText(name.space);
    AssertText(name.local);
    matches = (UTL_XML_ELEMENT == node->kind);
    if (matches)
    {
        matches = Utl_Xml_IsTextEqual(node->expanded.local, name.local);
    }
    if (matches)
    {
        matches = Utl_Xml_IsTextEqual(node->expanded.space, name.space);
    }
    return matches;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Line-ending normalization occurs before raw attribute whitespace replacement.
 */
bool_t Utl_Xml_GetTextByteCount(Utl_Xml_Text_t text, uint32_t* byteCount)
{
    uint32_t scalar;
    Cursor_t cursor;
    uint64_t count;
    bool_t   valid;
    LOGASSERT(NULL != byteCount);
    AssertText(text);
    scalar        = 0;
    cursor.source = text;
    cursor.offset = 0;
    count         = 0;
    valid         = TRUE;
    for (;;)
    {
        if ((!valid) || (cursor.offset >= text.byteCount))
        {
            break;
        }
        valid = ReadTextScalar(&cursor, &scalar);
        if (valid)
        {
            count += Utl_Unicode_GetUtf8ByteCount(scalar);
            valid = (count <= UINT32_MAX);
        }
    }
    if (valid)
    {
        *byteCount = (uint32_t)count;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Comments and processing instructions do not contribute text.
 */
bool_t Utl_Xml_GetElementTextByteCount(const Utl_Xml_Node_t* element, uint32_t* byteCount)
{
    uint32_t              child_bytes;
    bool_t                valid;
    uint64_t              count;
    const Utl_Xml_Node_t* child;
    uint32_t              index;
    LOGASSERT(NULL != element);
    LOGASSERT(UTL_XML_ELEMENT == element->kind);
    LOGASSERT(NULL != byteCount);
    child_bytes = 0;
    valid       = TRUE;
    count       = 0;
    child       = element->firstChild;
    for (index = 0; valid && (index < element->childCount); ++index)
    {
        LOGASSERT(NULL != child);
        valid = (UTL_XML_TEXT == child->kind);
        if (valid)
        {
            valid = Utl_Xml_GetTextByteCount(child->text, &child_bytes);
        }
        if (valid)
        {
            count += child_bytes;
            valid = (count <= UINT32_MAX);
        }
        child = child->next;
    }
    if (valid)
    {
        LOGASSERT(NULL == child);
        *byteCount = (uint32_t)count;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The published node count bounds traversal even under a violated caller model contract.
 */
const Utl_Xml_Node_t* Utl_Xml_FindSibling(const Utl_Xml_Document_t* document,
                                          const Utl_Xml_Node_t*     first,
                                          Utl_Xml_Name_t            name)
{
    const Utl_Xml_Node_t* current;
    const Utl_Xml_Node_t* found;
    uint32_t              index;
    LOGASSERT(NULL != document);
    LOGASSERT(UTL_XML_COMPLETE == document->result);
    AssertText(name.space);
    AssertText(name.local);
    current = first;
    found   = NULL;
    for (index = 0; (NULL != current) && (index < document->nodeCount); ++index)
    {
        if (Utl_Xml_IsElement(current, name))
        {
            found = current;
            break;
        }
        current = current->next;
    }
    return found;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Records are scratch until the final publication; a failed read can be followed by a fresh read.
 */
Utl_Xml_Result_t Utl_Xml_ReadDocument(Utl_Xml_Document_t* document, const byte_t* data, uint32_t byteCount)
{
    Reader_t reader;
    bool_t   valid;
    uint32_t before;
    LOGASSERT(NULL != document);
    LOGASSERT((NULL != data) || (0 == byteCount));
    LOGASSERT((NULL != document->storage.nodes) || (0 == document->storage.nodeCapacity));
    LOGASSERT((NULL != document->storage.attributes) || (0 == document->storage.attributeCapacity));
    document->root            = NULL;
    document->nodeCount       = 0;
    document->attributeCount  = 0;
    reader.document           = document;
    reader.cursor.source      = GetLiteral(data, byteCount);
    reader.cursor.offset      = 0;
    reader.parent             = NULL;
    reader.root               = NULL;
    reader.result             = UTL_XML_COMPLETE;
    reader.declarationAllowed = TRUE;
    valid                     = ReadSource(&reader);
    for (;;)
    {
        if ((!valid) || (reader.cursor.offset >= byteCount))
        {
            break;
        }
        before = reader.cursor.offset;
        if (UTL_XML_CHAR_LT == GetByte(&reader.cursor))
        {
            valid = ReadMarkup(&reader);
        }
        else
        {
            valid = ReadContent(&reader);
        }
        reader.declarationAllowed = FALSE;
        LOGASSERT((!valid) || (before < reader.cursor.offset));
    }
    valid = valid && (NULL != reader.root) && (NULL == reader.parent);
    if (valid)
    {
        document->root = reader.root;
    }
    else
    {
        document->nodeCount      = 0;
        document->attributeCount = 0;
        if (UTL_XML_COMPLETE == reader.result)
        {
            reader.result = UTL_XML_INVALID;
        }
    }
    document->result      = reader.result;
    document->errorOffset = reader.cursor.offset;
    return document->result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Complete validation and sizing precede the first destination write.
 */
bool_t Utl_Xml_WriteText(Utl_Xml_Text_t text, byte_t* output, uint32_t capacity, uint32_t* byteCount)
{
    bool_t   valid;
    uint32_t count;
    Cursor_t cursor;
    uint32_t offset;
    bool_t   decoded;
    uint32_t scalar;
    uint32_t width;
    LOGASSERT(NULL != byteCount);
    LOGASSERT((NULL != output) || (0 == capacity));
    valid = Utl_Xml_GetTextByteCount(text, &count);
    if (valid)
    {
        valid = (count <= capacity);
    }
    if (valid && (NULL != output))
    {
        cursor.source = text;
        cursor.offset = 0;
        offset        = 0;
        for (; cursor.offset < text.byteCount;)
        {
            decoded = ReadTextScalar(&cursor, &scalar);
            LOGASSERT(decoded);
            width = Utl_Unicode_WriteUtf8(scalar, (byte_t*)output + offset, count - offset);
            LOGASSERT(0 < width);
            offset += width;
        }
        LOGASSERT(offset == count);
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
 *  CDATA and entity-decoded text concatenate in document order.
 */
bool_t Utl_Xml_WriteElementText(const Utl_Xml_Node_t* element, byte_t* output, uint32_t capacity, uint32_t* byteCount)
{
    bool_t                valid;
    uint32_t              count;
    const Utl_Xml_Node_t* child;
    uint32_t              offset;
    uint32_t              index;
    bool_t                copied;
    uint32_t              written;
    LOGASSERT(NULL != byteCount);
    LOGASSERT((NULL != output) || (0 == capacity));
    valid = Utl_Xml_GetElementTextByteCount(element, &count);
    if (valid)
    {
        valid = (count <= capacity);
    }
    if (valid && (NULL != output))
    {
        child  = element->firstChild;
        offset = 0;
        for (index = 0; index < element->childCount; ++index)
        {
            LOGASSERT(NULL != child);
            copied = Utl_Xml_WriteText(child->text, output + offset, count - offset, &written);
            LOGASSERT(copied);
            offset += written;
            child  = child->next;
        }
        LOGASSERT(offset == count);
    }
    if (valid)
    {
        *byteCount = count;
    }
    return valid;
}
