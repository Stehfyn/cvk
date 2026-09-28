#ifndef UTL_XML_H
#define UTL_XML_H

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

#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UTL_XML_CHAR_TAB         ('\t')
#define UTL_XML_CHAR_LF          ('\n')
#define UTL_XML_CHAR_CR          ('\r')
#define UTL_XML_CHAR_SPACE       (' ')
#define UTL_XML_CHAR_AMP         ('&')
#define UTL_XML_CHAR_LT          ('<')
#define UTL_XML_CHAR_GT          ('>')
#define UTL_XML_CHAR_QUOT        ('"')
#define UTL_XML_CHAR_APOS        ('\'')
#define UTL_XML_CHAR_EQ          ('=')
#define UTL_XML_CHAR_COLON       (':')
#define UTL_XML_CHAR_SLASH       ('/')
#define UTL_XML_CHAR_QUESTION    ('?')
#define UTL_XML_CHAR_HASH        ('#')
#define UTL_XML_CHAR_SEMICOLON   (';')
#define UTL_XML_CHAR_ZERO        ('0')
#define UTL_XML_CHAR_NINE        ('9')
#define UTL_XML_CHAR_HEX_A       ('a')
#define UTL_XML_CHAR_HEX_F       ('f')
#define UTL_XML_CHAR_HEX_UPPER_A ('A')
#define UTL_XML_CHAR_HEX_UPPER_F ('F')
#define UTL_XML_CHAR_HEX         ('x')
#define UTL_XML_PREFIX_XML       ("xml")
#define UTL_XML_PREFIX_XMLNS     ("xmlns")
#define UTL_XML_NAMESPACE_XML    ("http://www.w3.org/XML/1998/namespace")
#define UTL_XML_NAMESPACE_XMLNS  ("http://www.w3.org/2000/xmlns/")

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Interpretation of a borrowed text span. */
typedef enum Utl_Xml_TextKind_e
{
    UTL_XML_TEXT_LITERAL,
    UTL_XML_TEXT_CONTENT,
    UTL_XML_TEXT_ATTRIBUTE,
    UTL_XML_TEXT_CDATA
} Utl_Xml_TextKind_t;

/*************************************************************************************************/
/** \brief Counted immutable XML source, excluding syntactic delimiters. */
typedef struct Utl_Xml_Text_s
{
    const char_t*      data;      ///< NULL only for zero byteCount.
    uint32_t           byteCount; ///< Exact readable UTF-8 source extent.
    Utl_Xml_TextKind_t kind;      ///< Escape and whitespace interpretation.
} Utl_Xml_Text_t;

/*************************************************************************************************/
/** \brief Namespace name and local name, independent of prefix spelling. */
typedef struct Utl_Xml_Name_s
{
    Utl_Xml_Text_t space; ///< Normalized namespace URI; empty for no namespace.
    Utl_Xml_Text_t local; ///< Literal NCName.
} Utl_Xml_Name_t;

/*************************************************************************************************/
/** \brief One borrowed attribute with its resolved name. */
typedef struct Utl_Xml_Attribute_s
{
    Utl_Xml_Text_t              name;        ///< Original QName.
    Utl_Xml_Text_t              value;       ///< Quoted value content, interpreted as ATTRIBUTE.
    Utl_Xml_Name_t              expanded;    ///< Namespace and local name after scope validation.
    struct Utl_Xml_Attribute_s* next;        ///< Next attribute on this element.
    bool_t                      declaration; ///< A namespace declaration.
} Utl_Xml_Attribute_t;

/*************************************************************************************************/
/** \brief Structural role of a retained node. */
typedef enum Utl_Xml_NodeKind_e
{
    UTL_XML_ELEMENT,
    UTL_XML_TEXT
} Utl_Xml_NodeKind_t;

/*************************************************************************************************/
/** \brief Caller-owned element or text record with bounded tree links. */
typedef struct Utl_Xml_Node_s
{
    Utl_Xml_Text_t         name;           ///< QName for elements; empty for text.
    Utl_Xml_Name_t         expanded;       ///< Resolved element name.
    Utl_Xml_Text_t         text;           ///< Character/CDATA content for text nodes.
    struct Utl_Xml_Node_s* parent;         ///< Enclosing element; NULL only for root.
    struct Utl_Xml_Node_s* firstChild;     ///< First child, including text nodes.
    struct Utl_Xml_Node_s* lastChild;      ///< Last child, or NULL.
    struct Utl_Xml_Node_s* next;           ///< Following sibling, or NULL.
    Utl_Xml_Attribute_t*   firstAttribute; ///< First retained attribute.
    Utl_Xml_Attribute_t*   lastAttribute;  ///< Last retained attribute.
    uint32_t               attributeCount; ///< Number of linked attributes.
    uint32_t               childCount;     ///< Number of directly linked child nodes.
    uint32_t               depth;          ///< Number of parents, bounded by the record capacity.
    Utl_Xml_NodeKind_t     kind;           ///< Element or text node.
} Utl_Xml_Node_t;

/*************************************************************************************************/
/** \brief Fixed record arrays established during application initialization. */
typedef struct Utl_Xml_Storage_s
{
    Utl_Xml_Node_t*      nodes;             ///< Writable node records, NULL only at zero capacity.
    Utl_Xml_Attribute_t* attributes;        ///< Writable attribute records, disjoint from nodes/source.
    uint32_t             nodeCapacity;      ///< Fixed node count, including retained text.
    uint32_t             attributeCapacity; ///< Fixed attribute count across the complete document.
} Utl_Xml_Storage_t;

/*************************************************************************************************/
/** \brief Complete parse outcome; no partial document is consumable. */
typedef enum Utl_Xml_Result_e
{
    UTL_XML_EMPTY,
    UTL_XML_COMPLETE,
    UTL_XML_INVALID,
    UTL_XML_CAPACITY,
    UTL_XML_UNSUPPORTED
} Utl_Xml_Result_t;

/*************************************************************************************************/
/** \brief Exclusive reusable document workspace borrowing immutable source bytes. */
typedef struct Utl_Xml_Document_s
{
    Utl_Xml_Storage_t storage;        ///< Retained caller-owned records.
    Utl_Xml_Node_t*   root;           ///< Published only after complete syntax/namespace validation.
    uint32_t          nodeCount;      ///< Zero after failure; backing records may contain scratch values.
    uint32_t          attributeCount; ///< Zero after failure.
    uint32_t          errorOffset;    ///< Source byte offset at rejection, or complete input size.
    Utl_Xml_Result_t  result;         ///< Latest complete operation result.
} Utl_Xml_Document_t;

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
 * \brief Binds fixed XML records and establishes an empty reusable document.
 *
 * \par Requirements:
 *  - 001 Missing required storage shall assert as a development error.
 *  - 002 Storage shall be disjoint from the document and every later input span.
 *  - 003 No allocation, source read or native operation shall occur.
 *
 * \param[out] document   Exclusive owner
 * \param[in]  storage    Retained fixed arrays
 *
 * \retval     None   Empty document with no borrowed input.
 */
void Utl_Xml_InitializeDocument(Utl_Xml_Document_t* document, Utl_Xml_Storage_t storage);

/*************************************************************************************************/
/**
 * \brief Classifies XML 1.0 Fifth Edition Char.
 *
 * \par Requirements:
 *  - 001 All permitted intervals shall match; NUL, surrogates and values above U+10FFFF shall fail.
 *
 * \param[in] value   Unicode code point
 *
 * \retval    TRUE    XML Char membership.
 * \retval    FALSE   The value is excluded by XML Char.
 */
bool_t Utl_Xml_IsChar(uint32_t value);

/*************************************************************************************************/
/**
 * \brief Compares normalized XML scalar sequences without temporary text buffers.
 *
 * \par Requirements:
 *  - 001 Nonempty spans shall have readable storage and every text kind shall be defined.
 *  - 002 References and whitespace shall follow each span kind; malformed input shall not compare equal.
 *
 * \param[in] first    First counted span
 * \param[in] second   Second counted span
 *
 * \retval    TRUE    Complete normalized scalar equality.
 * \retval    FALSE   The normalized sequences differ or either consumed span is malformed.
 */
bool_t Utl_Xml_IsTextEqual(Utl_Xml_Text_t first, Utl_Xml_Text_t second);

/*************************************************************************************************/
/**
 * \brief Matches a completed element by namespace URI and local name.
 *
 * \par Requirements:
 *  - 001 Node shall belong to a successfully parsed unchanged document.
 *  - 002 Prefix spelling shall not affect matching.
 *
 * \param[in] node   Retained node
 * \param[in] name   Expected expanded name
 *
 * \retval    TRUE    Element has the expected expanded name.
 * \retval    FALSE   The node is text or the expanded name differs.
 */
bool_t Utl_Xml_IsElement(const Utl_Xml_Node_t* node, Utl_Xml_Name_t name);

/*************************************************************************************************/
/**
 * \brief Validates a text span and derives its normalized UTF-8 output size.
 *
 * \par Requirements:
 *  - 001 Missing required storage or an undefined text kind shall assert; result storage shall be disjoint from source.
 *  - 002 Invalid characters, malformed references and count overflow shall preserve output.
 *  - 003 No terminator or signature shall count toward output.
 *
 * \param[in]  text        Counted XML text
 * \param[out] byteCount   Exact normalized byte count
 *
 * \retval     TRUE    The exact count is published.
 * \retval     FALSE   The count is unchanged.
 */
bool_t Utl_Xml_GetTextByteCount(Utl_Xml_Text_t text, uint32_t* byteCount);

/*************************************************************************************************/
/**
 * \brief Derives the concatenated UTF-8 size of a simple-content element.
 *
 * \par Requirements:
 *  - 001 Required pointers shall assert; element shall belong to a completed immutable document.
 *  - 002 Nested elements and size overflow shall preserve output.
 *  - 003 Adjacent character data and CDATA shall concatenate without separators.
 *
 * \param[in]  element     Completed element
 * \param[out] byteCount   Disjoint output count
 *
 * \retval     TRUE    The exact count is published.
 * \retval     FALSE   The element is not simple content or exceeds the count bound.
 */
bool_t Utl_Xml_GetElementTextByteCount(const Utl_Xml_Node_t* element, uint32_t* byteCount);

/*************************************************************************************************/
/**
 * \brief Finds the first matching element at one sibling level.
 *
 * \par Requirements:
 *  - 001 Document shall be complete and source/records unchanged.
 *  - 002 First shall be NULL or a node in this document; only its sibling chain shall be inspected.
 *  - 003 Resuming with the result next link shall expose duplicate fields.
 *
 * \param[in] document   Completed document
 * \param[in] first      First sibling to inspect
 * \param[in] name       Expanded name
 *
 * \retval    node   First matching sibling.
 * \retval    NULL   No matching sibling.
 */
const Utl_Xml_Node_t* Utl_Xml_FindSibling(const Utl_Xml_Document_t* document,
                                          const Utl_Xml_Node_t*     first,
                                          Utl_Xml_Name_t            name);

/*************************************************************************************************/
/**
 * \brief Reads a complete UTF-8 XML document into retained caller records.
 *
 * \par Requirements:
 *  - 001 Document shall be initialized and exclusively owned; nonempty source shall have readable storage.
 *  - 002 Source and record arrays shall be disjoint and stable throughout every later query.
 *  - 003 Every attempt invalidates earlier borrows; failure clears root and published counts.
 *  - 004 Malformed syntax, namespace errors, capacity limits and unsupported encodings/DTD shall be distinguished.
 *  - 005 Only one complete root is permitted; comments and instructions are validated and ignored.
 *  - 006 No source mutation, allocation, recursion, CRT call or OS operation shall occur.
 *
 * \param[in,out] document    Initialized retained workspace
 * \param[in]     data        Complete immutable UTF-8 input
 * \param[in]     byteCount   Exact extent
 *
 * \retval        UTL_XML_COMPLETE      A complete namespaced tree is published.
 * \retval        UTL_XML_INVALID       Malformed or incomplete document.
 * \retval        UTL_XML_CAPACITY      Fixed record capacity was exceeded.
 * \retval        UTL_XML_UNSUPPORTED   DTD or unsupported encoding/version was encountered.
 */
Utl_Xml_Result_t Utl_Xml_ReadDocument(Utl_Xml_Document_t* document, const char_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Decodes and normalizes text into caller-owned UTF-8 output.
 *
 * \par Requirements:
 *  - 001 Required output count and nonempty buffer shall assert; all outputs shall be disjoint from source.
 *  - 002 Invalid syntax or insufficient capacity shall preserve every output byte and count.
 *  - 003 No trailing terminator shall be appended.
 *
 * \param[in]  text        Stable counted source
 * \param[out] output      Writable bytes; NULL only at zero capacity
 * \param[in]  capacity    Writable extent
 * \param[out] byteCount   Disjoint exact count
 *
 * \retval     TRUE    Complete normalized bytes are published.
 * \retval     FALSE   Outputs are unchanged.
 */
bool_t Utl_Xml_WriteText(Utl_Xml_Text_t text, char_t* output, uint32_t capacity, uint32_t* byteCount);

/*************************************************************************************************/
/**
 * \brief Writes all simple content of one completed element as UTF-8.
 *
 * \par Requirements:
 *  - 001 Element and source shall remain unchanged and disjoint from outputs.
 *  - 002 Nested elements, invalid text, overflow or insufficient capacity shall preserve every output.
 *  - 003 The result shall contain no added separators or terminator.
 *
 * \param[in]  element     Completed element
 * \param[out] output      Writable bytes; NULL only at zero capacity
 * \param[in]  capacity    Writable extent
 * \param[out] byteCount   Required disjoint exact count
 *
 * \retval     TRUE    Complete concatenated bytes are published.
 * \retval     FALSE   Outputs are unchanged.
 */
bool_t Utl_Xml_WriteElementText(const Utl_Xml_Node_t* element, char_t* output, uint32_t capacity, uint32_t* byteCount);

#endif /* UTL_XML_H */
