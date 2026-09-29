/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * XML 1.0 Fifth Edition and Namespaces in XML 1.0 Third Edition.
 * Independent valid/invalid vectors, exact outputs, capacity guards and repeated reuse.
 * https://www.w3.org/TR/xml/
 * https://www.w3.org/TR/xml-names/
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_xml.h"
#include "utl/utl_upnp.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include "crt/crt_memory.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define XML_TEST_NODES           (64)
#define XML_TEST_ATTRIBUTES      (32)
#define XML_TEST_OUTPUT          (128)
#define XML_TEST_GUARD           (0xA5)
#define XML_TEST_COUNT           (777)
#define XML_TEST_CYCLES          (100)
#define XML_TEST_PREFIX_DOCUMENT ("<r a='x'><y>text</y></r>")

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Independent parser and normalized simple-text expectation. */
typedef struct Case_s
{
    const char_t*    source;      ///< Complete counted input.
    const char_t*    text;        ///< Expected simple content, or NULL when not inspected.
    uint32_t         sourceBytes; ///< Input extent, including deliberate zero bytes.
    uint32_t         textBytes;   ///< Exact expected UTF-8 output extent.
    Utl_Xml_Result_t result;      ///< Expected parse outcome.
} Case_t;

/*************************************************************************************************/
/** \brief Independent root-device display selection expectation. */
typedef struct DescriptionCase_s
{
    const char_t* source;      ///< Complete namespace-well-formed description.
    uint32_t      sourceBytes; ///< Exact input extent.
    bool_t        valid;       ///< Expected display-identity selection result.
} DescriptionCase_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static Utl_Xml_Node_t      g_nodes[XML_TEST_NODES];
static Utl_Xml_Attribute_t g_attributes[XML_TEST_ATTRIBUTES];
static Utl_Xml_Document_t  g_document;
static byte_t              g_output[XML_TEST_OUTPUT];

static const Case_t        c_cases[] = {
    {"<x/>", "", sizeof("<x/>") - 1, sizeof("") - 1, UTL_XML_COMPLETE},
    {"<?xml version='1.0' encoding='uTf-8' standalone='yes'?><!--a--><p:x xmlns:p='urn:x'/>",
     "",
     sizeof("<?xml version='1.0' encoding='uTf-8' standalone='yes'?><!--a--><p:x xmlns:p='urn:x'/>") - 1,
     sizeof("") - 1,
     UTL_XML_COMPLETE},
    {"<x>a\r\nb\rc\nd</x>",
     "a\nb\nc\nd",
     sizeof("<x>a\r\nb\rc\nd</x>") - 1,
     sizeof("a\nb\nc\nd") - 1,
     UTL_XML_COMPLETE},
    {"<x>A<![CDATA[&B<]]>C<!--comment-->D<?p data?>E</x>",
     "A&B<CDE",
     sizeof("<x>A<![CDATA[&B<]]>C<!--comment-->D<?p data?>E</x>") - 1,
     sizeof("A&B<CDE") - 1,
     UTL_XML_COMPLETE},
    {"<x>&#xD;&#xA;&#9;</x>", "\r\n\t", sizeof("<x>&#xD;&#xA;&#9;</x>") - 1, sizeof("\r\n\t") - 1, UTL_XML_COMPLETE},
    {"<x>&lt;&gt;&amp;&quot;&apos;&#65;</x>",
     "<>&\"'A",
     sizeof("<x>&lt;&gt;&amp;&quot;&apos;&#65;</x>") - 1,
     sizeof("<>&\"'A") - 1,
     UTL_XML_COMPLETE},
    {"<x>&#x1F642;</x>",
     "\xF0"
     "\x9F"
     "\x99"
     "\x82"
     "",
     sizeof("<x>&#x1F642;</x>") - 1,
     sizeof("\xF0"
            "\x9F"
            "\x99"
            "\x82"
            "") -
         1,
     UTL_XML_COMPLETE},
    {"\xEF"
     "\xBB"
     "\xBF"
     "<x/>",
     "",
     sizeof("\xEF"
            "\xBB"
            "\xBF"
            "<x/>") -
         1,
     sizeof("") - 1,
     UTL_XML_COMPLETE},
    {"<x xmlns=\"urn:a\" xmlns:p=\"urn:b\" p:a=\"x\" a=\"y\"/>",
     "",
     sizeof("<x xmlns=\"urn:a\" xmlns:p=\"urn:b\" p:a=\"x\" a=\"y\"/>") - 1,
     sizeof("") - 1,
     UTL_XML_COMPLETE},
    {"<x xmlns=\"urn:a\"><y xmlns=\"\"/></x>",
     NULL,
     sizeof("<x xmlns=\"urn:a\"><y xmlns=\"\"/></x>") - 1,
     0,
     UTL_XML_COMPLETE},
    {"<x xmlns:p=\"urn:a\"><p:a xmlns:p=\"urn:b\"/><p:b/></x>",
     NULL,
     sizeof("<x xmlns:p=\"urn:a\"><p:a xmlns:p=\"urn:b\"/><p:b/></x>") - 1,
     0,
     UTL_XML_COMPLETE},
    {"<p:\xC3"
     "\xA9"
     " xmlns:p=\"urn:x\"/>",
     "",
     sizeof("<p:\xC3"
            "\xA9"
            " xmlns:p=\"urn:x\"/>") -
         1,
     sizeof("") - 1,
     UTL_XML_COMPLETE},
    {"<x xml:lang=\"en\"/>", "", sizeof("<x xml:lang=\"en\"/>") - 1, sizeof("") - 1, UTL_XML_COMPLETE},
    {"<x xmlns:xml=\"http://www.w3.org/XML/1998/namespace\"/>",
     "",
     sizeof("<x xmlns:xml=\"http://www.w3.org/XML/1998/namespace\"/>") - 1,
     sizeof("") - 1,
     UTL_XML_COMPLETE},
    {"<?p:x?><x/>", NULL, sizeof("<?p:x?><x/>") - 1, 0, UTL_XML_INVALID},
    {"<?p?><x/>", "", sizeof("<?p?><x/>") - 1, 0, UTL_XML_COMPLETE},
    {"<x>]]&gt;</x>", "]]>", sizeof("<x>]]&gt;</x>") - 1, sizeof("]]>") - 1, UTL_XML_COMPLETE},
    {"", NULL, sizeof("") - 1, 0, UTL_XML_INVALID},
    {" ", NULL, sizeof(" ") - 1, 0, UTL_XML_INVALID},
    {"<x>", NULL, sizeof("<x>") - 1, 0, UTL_XML_INVALID},
    {"<x><y></x>", NULL, sizeof("<x><y></x>") - 1, 0, UTL_XML_INVALID},
    {"<x/><y/>", NULL, sizeof("<x/><y/>") - 1, 0, UTL_XML_INVALID},
    {"<x/>tail", NULL, sizeof("<x/>tail") - 1, 0, UTL_XML_INVALID},
    {"<![CDATA[x]]><x/>", NULL, sizeof("<![CDATA[x]]><x/>") - 1, 0, UTL_XML_INVALID},
    {"<x>a]]>b</x>", NULL, sizeof("<x>a]]>b</x>") - 1, 0, UTL_XML_INVALID},
    {"<x>&unknown;</x>", NULL, sizeof("<x>&unknown;</x>") - 1, 0, UTL_XML_INVALID},
    {"<x>&amp</x>", NULL, sizeof("<x>&amp</x>") - 1, 0, UTL_XML_INVALID},
    {"<x>&#0;</x>", NULL, sizeof("<x>&#0;</x>") - 1, 0, UTL_XML_INVALID},
    {"<x>&#xD800;</x>", NULL, sizeof("<x>&#xD800;</x>") - 1, 0, UTL_XML_INVALID},
    {"<x>&#x110000;</x>", NULL, sizeof("<x>&#x110000;</x>") - 1, 0, UTL_XML_INVALID},
    {"<x>&#99999999999999999;</x>", NULL, sizeof("<x>&#99999999999999999;</x>") - 1, 0, UTL_XML_INVALID},
    {"<x>&#X41;</x>", NULL, sizeof("<x>&#X41;</x>") - 1, 0, UTL_XML_INVALID},
    {"<x>&#;</x>", NULL, sizeof("<x>&#;</x>") - 1, 0, UTL_XML_INVALID},
    {"<!--a--b--><x/>", NULL, sizeof("<!--a--b--><x/>") - 1, 0, UTL_XML_INVALID},
    {"<!--a---><x/>", NULL, sizeof("<!--a---><x/>") - 1, 0, UTL_XML_INVALID},
    {"<?XML version=\"1.0\"?><x/>", NULL, sizeof("<?XML version=\"1.0\"?><x/>") - 1, 0, UTL_XML_INVALID},
    {"<?xml?><x/>", NULL, sizeof("<?xml?><x/>") - 1, 0, UTL_XML_INVALID},
    {"<!--x--><?xml version=\"1.0\"?><x/>",
     NULL,
     sizeof("<!--x--><?xml version=\"1.0\"?><x/>") - 1,
     0,
     UTL_XML_INVALID},
    {"<x><?xml version=\"1.0\"?></x>", NULL, sizeof("<x><?xml version=\"1.0\"?></x>") - 1, 0, UTL_XML_INVALID},
    {"<?xml version=\"1.0\" standalone=\"yes\" encoding=\"UTF-8\"?><x/>",
     NULL,
     sizeof("<?xml version=\"1.0\" standalone=\"yes\" encoding=\"UTF-8\"?><x/>") - 1,
     0,
     UTL_XML_INVALID},
    {"<?xml version=\"1.1\"?><x/>", NULL, sizeof("<?xml version=\"1.1\"?><x/>") - 1, 0, UTL_XML_UNSUPPORTED},
    {"<?xml version=\"1.0\" encoding=\"UTF-16\"?><x/>",
     NULL,
     sizeof("<?xml version=\"1.0\" encoding=\"UTF-16\"?><x/>") - 1,
     0,
     UTL_XML_UNSUPPORTED},
    {"<!DOCTYPE x><x/>", NULL, sizeof("<!DOCTYPE x><x/>") - 1, 0, UTL_XML_UNSUPPORTED},
    {"\xFF"
     "\xFE"
     "<\x00"
     "x\x00"
     "/\x00"
     ">\x00"
     "",
     NULL,
     sizeof("\xFF"
            "\xFE"
            "<\x00"
            "x\x00"
            "/\x00"
            ">\x00"
            "") -
         1,
     0,
     UTL_XML_UNSUPPORTED},
    {"<x>a\x00"
     "b</x>",
     NULL,
     sizeof("<x>a\x00"
            "b</x>") -
         1,
     0,
     UTL_XML_INVALID},
    {"<x>\x01"
     "</x>",
     NULL,
     sizeof("<x>\x01"
            "</x>") -
         1,
     0,
     UTL_XML_INVALID},
    {"<x>\xC0"
     "\x80"
     "</x>",
     NULL,
     sizeof("<x>\xC0"
            "\x80"
            "</x>") -
         1,
     0,
     UTL_XML_INVALID},
    {"<x a=\"1\"a=\"2\"/>", NULL, sizeof("<x a=\"1\"a=\"2\"/>") - 1, 0, UTL_XML_INVALID},
    {"<x a=\"1\" a=\"2\"/>", NULL, sizeof("<x a=\"1\" a=\"2\"/>") - 1, 0, UTL_XML_INVALID},
    {"<x a=\"<\"/>", NULL, sizeof("<x a=\"<\"/>") - 1, 0, UTL_XML_INVALID},
    {"<x xmlns:a=\"urn:x\" xmlns:b=\"urn:x\" a:y=\"1\" b:y=\"2\"/>",
     NULL,
     sizeof("<x xmlns:a=\"urn:x\" xmlns:b=\"urn:x\" a:y=\"1\" b:y=\"2\"/>") - 1,
     0,
     UTL_XML_INVALID},
    {"<p:x/>", NULL, sizeof("<p:x/>") - 1, 0, UTL_XML_INVALID},
    {"<x p:a=\"1\"/>", NULL, sizeof("<x p:a=\"1\"/>") - 1, 0, UTL_XML_INVALID},
    {"<xmlns:x/>", NULL, sizeof("<xmlns:x/>") - 1, 0, UTL_XML_INVALID},
    {"<x xmlns:xml=\"urn:wrong\"/>", NULL, sizeof("<x xmlns:xml=\"urn:wrong\"/>") - 1, 0, UTL_XML_INVALID},
    {"<x xmlns:p=\"http://www.w3.org/XML/1998/namespace\"/>",
     NULL,
     sizeof("<x xmlns:p=\"http://www.w3.org/XML/1998/namespace\"/>") - 1,
     0,
     UTL_XML_INVALID},
    {"<x xmlns=\"http://www.w3.org/XML/1998/namespace\"/>",
     NULL,
     sizeof("<x xmlns=\"http://www.w3.org/XML/1998/namespace\"/>") - 1,
     0,
     UTL_XML_INVALID},
    {"<x xmlns:p=\"http://www.w3.org/2000/xmlns/\"/>",
     NULL,
     sizeof("<x xmlns:p=\"http://www.w3.org/2000/xmlns/\"/>") - 1,
     0,
     UTL_XML_INVALID},
    {"<x xmlns:xmlns=\"urn:x\"/>", NULL, sizeof("<x xmlns:xmlns=\"urn:x\"/>") - 1, 0, UTL_XML_INVALID},
    {"<x xmlns:p=\"\"/>", NULL, sizeof("<x xmlns:p=\"\"/>") - 1, 0, UTL_XML_INVALID},
    {"<:x/>", NULL, sizeof("<:x/>") - 1, 0, UTL_XML_INVALID},
    {"<x:/>", NULL, sizeof("<x:/>") - 1, 0, UTL_XML_INVALID},
    {"<a:b:c/>", NULL, sizeof("<a:b:c/>") - 1, 0, UTL_XML_INVALID},
    {"<a:1x xmlns:a=\"urn:x\"/>", NULL, sizeof("<a:1x xmlns:a=\"urn:x\"/>") - 1, 0, UTL_XML_INVALID},
};

/*************************************************************************************************/
/**
 * \brief Display-identity cases independent of the XML grammar cases.
 *
 * \par Details:
 *  Missing model preserves the existing display behavior; this is not complete schema validation.
 */
static const DescriptionCase_t c_descriptionCases[] = {
    {"<root xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName>TV</friendlyName></device></root>",
     sizeof("<root xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName>TV</friendlyName></device></root>") -
         1,
     TRUE},
    {"<root xmlns='urn:schemas-upnp-org:device-&#49;-0'><device><friendlyName>TV</friendlyName></device></root>",
     sizeof(
         "<root xmlns='urn:schemas-upnp-org:device-&#49;-0'><device><friendlyName>TV</friendlyName></device></root>") -
         1,
     TRUE},
    {"<root xmlns='urn:wrong'><device><friendlyName>TV</friendlyName></device></root>",
     sizeof("<root xmlns='urn:wrong'><device><friendlyName>TV</friendlyName></device></root>") - 1,
     FALSE},
    {"<root xmlns='urn:schemas-upnp-org:device-1-0'/>",
     sizeof("<root xmlns='urn:schemas-upnp-org:device-1-0'/>") - 1,
     FALSE},
    {"<root xmlns='urn:schemas-upnp-org:device-1-0'><device/></root>",
     sizeof("<root xmlns='urn:schemas-upnp-org:device-1-0'><device/></root>") - 1,
     FALSE},
    {"<root xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName/></device></root>",
     sizeof("<root xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName/></device></root>") - 1,
     FALSE},
    {"<root "
     "xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName><part>TV</part></friendlyName></device></root>",
     sizeof("<root "
            "xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName><part>TV</part></friendlyName></device></"
            "root>") -
         1,
     FALSE},
    {"<root xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName xmlns=''>TV</friendlyName></device></root>",
     sizeof("<root xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName "
            "xmlns=''>TV</friendlyName></device></root>") -
         1,
     FALSE},
    {"<root xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName>TV</friendlyName></device><device/></root>",
     sizeof(
         "<root "
         "xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName>TV</friendlyName></device><device/></root>") -
         1,
     FALSE},
    {"<root "
     "xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName>TV</friendlyName><modelName>A</"
     "modelName><modelName>B</modelName></device></root>",
     sizeof("<root "
            "xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName>TV</friendlyName><modelName>A</"
            "modelName><modelName>B</modelName></device></root>") -
         1,
     FALSE},
    {"<root "
     "xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName>TV</friendlyName><modelName><part>A</part></"
     "modelName></device></root>",
     sizeof("<root "
            "xmlns='urn:schemas-upnp-org:device-1-0'><device><friendlyName>TV</friendlyName><modelName><part>A</part></"
            "modelName></device></root>") -
         1,
     FALSE},
};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Checks every unwritten output byte.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 First shall be within the fixed output.
 * \param[in] first   Beginning of the guard tail
 * \retval    None
 */
static FORCE_INLINE void CheckGuard(uint32_t first);

/*************************************************************************************************/
/**
 * \brief Checks exact normalized content and every short destination capacity.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Document shall contain the current completed case.
 * \param[in] test   Independent expected bytes
 * \retval    None
 */
static FORCE_INLINE void CheckText(const Case_t* test);

/*************************************************************************************************/
/**
 * \brief Checks parse outcome and failure publication.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Input and expected result shall be independent test data.
 * \param[in] test   Complete parser expectation
 * \retval    None
 */
static FORCE_INLINE void CheckCase(const Case_t* test);

/*************************************************************************************************/
/**
 * \brief Checks every incomplete prefix and recovery after repeated rejection.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Only the complete independent document shall be accepted.
 * \retval None
 */
static FORCE_INLINE void CheckPrefixes(void);

/*************************************************************************************************/
/**
 * \brief Checks node and attribute capacity failure with subsequent owner reuse.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Zero-capacity storage shall be valid initialization and fail only when a record is needed.
 * \retval None
 */
static FORCE_INLINE void CheckCapacity(void);

/*************************************************************************************************/
/**
 * \brief Checks scope shadowing and raw-versus-reference attribute whitespace.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Sibling scopes and unprefixed attribute namespaces shall remain independent.
 * \retval None
 */
static FORCE_INLINE void CheckNamespaces(void);

/*************************************************************************************************/
/**
 * \brief Checks root-device identity selection and output preservation.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Nested devices and vendor fields shall not override direct schema fields.
 * \retval None
 */
static FORCE_INLINE void CheckDescription(void);

/*************************************************************************************************/
/**
 * \brief Checks consistent malformed-text rejection across comparison, sizing and writing.
 *
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Raw closing delimiters shall fail in content and CDATA, but remain literal comparison data.
 *  - 002 Encoded closing delimiters and raw attribute data shall retain their permitted meanings.
 *  - 003 Invalid text shall preserve both the output bytes and the count.
 *
 * \retval None
 */
static FORCE_INLINE void CheckTextKinds(void);

/*************************************************************************************************/
/**
 * \brief Checks one independently specified display identity case.
 *
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Each source shall be a complete namespace-well-formed XML document.
 *  - 002 Rejected identity shall preserve every output field.
 *  - 003 Accepted cases shall yield the expected name and an omitted model.
 *
 * \param[in] test   Expected display-identity result
 *
 * \retval    None
 */
static FORCE_INLINE void CheckDescriptionCase(const DescriptionCase_t* test);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The first byte beyond capacity is always checked.
 */
static FORCE_INLINE void CheckGuard(uint32_t first)
{
    uint32_t index;
    for (index = first; index < XML_TEST_OUTPUT; ++index)
    {
        LOGASSERT(XML_TEST_GUARD == (byte_t)g_output[index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Input fragments, references and CDATA are compared with independently specified output.
 */
static FORCE_INLINE void CheckText(const Case_t* test)
{
    bool_t   valid;
    uint32_t count;
    uint32_t capacity;
    int32_t  compared;
    valid = Utl_Xml_GetElementTextByteCount(g_document.root, &count);
    LOGASSERT(valid);
    LOGASSERT(test->textBytes == count);
    LOGASSERT(count < XML_TEST_OUTPUT);
    for (capacity = 0; capacity < test->textBytes; ++capacity)
    {
        CRT_Memory_Set(g_output, XML_TEST_GUARD, sizeof(g_output));
        count = XML_TEST_COUNT;
        valid = Utl_Xml_WriteElementText(g_document.root, g_output, capacity, &count);
        LOGASSERT(!valid);
        LOGASSERT(XML_TEST_COUNT == count);
        CheckGuard(0);
    }
    CRT_Memory_Set(g_output, XML_TEST_GUARD, sizeof(g_output));
    valid = Utl_Xml_WriteElementText(g_document.root, g_output, test->textBytes, &count);
    LOGASSERT(valid);
    LOGASSERT(test->textBytes == count);
    compared = CRT_Memory_Compare(g_output, test->text, count);
    LOGASSERT(0 == compared);
    CheckGuard(count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Failed attempts leave no consumable root or record count.
 */
static FORCE_INLINE void CheckCase(const Case_t* test)
{
    Utl_Xml_Result_t result;
    result = Utl_Xml_ReadDocument(&g_document, (const byte_t*)test->source, test->sourceBytes);
    LOGASSERT(test->result == result);
    LOGASSERT(result == g_document.result);
    LOGASSERT(g_document.errorOffset <= test->sourceBytes);
    if (UTL_XML_COMPLETE == result)
    {
        LOGASSERT(NULL != g_document.root);
        LOGASSERT(0 < g_document.nodeCount);
        if (NULL != test->text)
        {
            CheckText(test);
        }
    }
    else
    {
        LOGASSERT(NULL == g_document.root);
        LOGASSERT(0 == g_document.nodeCount);
        LOGASSERT(0 == g_document.attributeCount);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The repeated read uses the same startup storage without reset allocation.
 */
static FORCE_INLINE void CheckPrefixes(void)
{
    uint32_t         cycle;
    uint32_t         index;
    Utl_Xml_Result_t result;
    for (cycle = 0; cycle < XML_TEST_CYCLES; ++cycle)
    {
        for (index = 0; index < (sizeof(XML_TEST_PREFIX_DOCUMENT) - 1); ++index)
        {
            result = Utl_Xml_ReadDocument(&g_document, (const byte_t*)XML_TEST_PREFIX_DOCUMENT, index);
            LOGASSERT(UTL_XML_INVALID == result);
            LOGASSERT(NULL == g_document.root);
        }
        result = Utl_Xml_ReadDocument(&g_document, (const byte_t*)XML_TEST_PREFIX_DOCUMENT, sizeof(XML_TEST_PREFIX_DOCUMENT) - 1);
        LOGASSERT(UTL_XML_COMPLETE == result);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Capacities are restored through the initializer before subsequent cases.
 */
static FORCE_INLINE void CheckCapacity(void)
{
    Utl_Xml_Storage_t storage;
    Utl_Xml_Result_t  result;
    storage              = g_document.storage;
    storage.nodeCapacity = 0;
    storage.nodes        = NULL;
    Utl_Xml_InitializeDocument(&g_document, storage);
    result = Utl_Xml_ReadDocument(&g_document, (const byte_t*)"<x/>", sizeof("<x/>") - 1);
    LOGASSERT(UTL_XML_CAPACITY == result);
    LOGASSERT(NULL == g_document.root);
    storage.nodeCapacity      = XML_TEST_NODES;
    storage.nodes             = g_nodes;
    storage.attributeCapacity = 0;
    storage.attributes        = NULL;
    Utl_Xml_InitializeDocument(&g_document, storage);
    result = Utl_Xml_ReadDocument(&g_document, (const byte_t*)"<x a='b'/>", sizeof("<x a='b'/>") - 1);
    LOGASSERT(UTL_XML_CAPACITY == result);
    storage.attributeCapacity = XML_TEST_ATTRIBUTES;
    storage.attributes        = g_attributes;
    Utl_Xml_InitializeDocument(&g_document, storage);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Expected values do not come from the parser or serializer.
 */
static FORCE_INLINE void CheckNamespaces(void)
{
    Utl_Xml_Result_t      result;
    const Utl_Xml_Node_t* first;
    const Utl_Xml_Node_t* second;
    Utl_Xml_Text_t        expected;
    bool_t                valid;
    uint32_t              count;
    int32_t               compared;
    result = Utl_Xml_ReadDocument(&g_document,
                                  (const byte_t*)"<x xmlns:p='urn:a'><p:a xmlns:p='urn:b'/><p:b/></x>",
                                  sizeof("<x xmlns:p='urn:a'><p:a xmlns:p='urn:b'/><p:b/></x>") - 1);
    LOGASSERT(UTL_XML_COMPLETE == result);
    first = g_document.root->firstChild;
    LOGASSERT(NULL != first);
    second = first->next;
    LOGASSERT(NULL != second);
    expected.data      = (const byte_t*)"urn:b";
    expected.byteCount = sizeof("urn:b") - 1;
    expected.kind      = UTL_XML_TEXT_LITERAL;
    LOGASSERT(Utl_Xml_IsTextEqual(first->expanded.space, expected));
    expected.data = (const byte_t*)"urn:a";
    LOGASSERT(Utl_Xml_IsTextEqual(second->expanded.space, expected));
    result = Utl_Xml_ReadDocument(&g_document,
                                  (const byte_t*)"<x xmlns='urn:x' a='A&#xA;\tB\r\nC'/>",
                                  sizeof("<x xmlns='urn:x' a='A&#xA;\tB\r\nC'/>") - 1);
    LOGASSERT(UTL_XML_COMPLETE == result);
    LOGASSERT(0 == g_document.root->lastAttribute->expanded.space.byteCount);
    valid = Utl_Xml_WriteText(g_document.root->lastAttribute->value, g_output, XML_TEST_OUTPUT, &count);
    LOGASSERT(valid);
    LOGASSERT((sizeof("A\n B C") - 1) == count);
    compared = CRT_Memory_Compare(g_output, "A\n B C", count);
    LOGASSERT(0 == compared);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A duplicate name rejects the complete identity while preserving a previous result.
 */
static FORCE_INLINE void CheckDescription(void)
{
    Utl_Xml_Result_t       result;
    bool_t                 valid;
    Utl_Upnp_Description_t description;
    uint32_t               count;
    int32_t                compared;
    Utl_Upnp_Description_t saved;
    result = Utl_Xml_ReadDocument(
        &g_document,
        (const byte_t*)"<u:root xmlns:u='"
        "urn:schemas-upnp-org:device-1-0"
        "'><u:device>"
        "<u:deviceList><u:device><u:friendlyName>Wrong</u:friendlyName></u:device></u:deviceList>"
        "<v:friendlyName xmlns:v='urn:vendor'>Wrong</v:friendlyName>"
        "<u:friendlyName>A&amp;<![CDATA[B]]></u:friendlyName><u:modelName>Model</u:modelName>"
        "</u:device></u:root>",
        sizeof("<u:root xmlns:u='"
               "urn:schemas-upnp-org:device-1-0"
               "'><u:device>"
               "<u:deviceList><u:device><u:friendlyName>Wrong</u:friendlyName></u:device></u:deviceList>"
               "<v:friendlyName xmlns:v='urn:vendor'>Wrong</v:friendlyName>"
               "<u:friendlyName>A&amp;<![CDATA[B]]></u:friendlyName><u:modelName>Model</u:modelName>"
               "</u:device></u:root>") -
            1);
    LOGASSERT(UTL_XML_COMPLETE == result);
    valid = Utl_Upnp_ReadDescription(&g_document, &description);
    LOGASSERT(valid);
    valid = Utl_Xml_WriteElementText(description.friendlyName, g_output, XML_TEST_OUTPUT, &count);
    LOGASSERT(valid);
    LOGASSERT((sizeof("A&B") - 1) == count);
    compared = CRT_Memory_Compare(g_output, "A&B", count);
    LOGASSERT(0 == compared);
    saved  = description;
    result = Utl_Xml_ReadDocument(&g_document,
                                  (const byte_t*)"<root xmlns='"
                                  "urn:schemas-upnp-org:device-1-0"
                                  "'><device><friendlyName>A</friendlyName>"
                                  "<friendlyName>B</friendlyName></device></root>",
                                  sizeof("<root xmlns='"
                                         "urn:schemas-upnp-org:device-1-0"
                                         "'><device><friendlyName>A</friendlyName>"
                                         "<friendlyName>B</friendlyName></device></root>") -
                                      1);
    LOGASSERT(UTL_XML_COMPLETE == result);
    valid = Utl_Upnp_ReadDescription(&g_document, &description);
    LOGASSERT(!valid);
    LOGASSERT(saved.device == description.device);
    LOGASSERT(saved.friendlyName == description.friendlyName);
    LOGASSERT(saved.modelName == description.modelName);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All representations share the same independent scalar sequence expectation.
 */
static FORCE_INLINE void CheckTextKinds(void)
{
    Utl_Xml_Text_t raw;
    Utl_Xml_Text_t expected;
    uint32_t       count;
    bool_t         valid;
    Utl_Xml_Text_t encoded;
    int32_t        compared;
    raw.data      = (const byte_t*)"]]>";
    raw.byteCount = sizeof("]]>") - 1;
    raw.kind      = UTL_XML_TEXT_LITERAL;
    expected      = raw;
    LOGASSERT(Utl_Xml_IsTextEqual(raw, expected));
    raw.kind = UTL_XML_TEXT_ATTRIBUTE;
    LOGASSERT(Utl_Xml_IsTextEqual(raw, expected));
    raw.kind = UTL_XML_TEXT_CONTENT;
    LOGASSERT(!Utl_Xml_IsTextEqual(raw, expected));
    LOGASSERT(!Utl_Xml_IsTextEqual(raw, raw));
    count = XML_TEST_COUNT;
    valid = Utl_Xml_GetTextByteCount(raw, &count);
    LOGASSERT(!valid);
    LOGASSERT(XML_TEST_COUNT == count);
    CRT_Memory_Set(g_output, XML_TEST_GUARD, sizeof(g_output));
    valid = Utl_Xml_WriteText(raw, g_output, XML_TEST_OUTPUT, &count);
    LOGASSERT(!valid);
    LOGASSERT(XML_TEST_COUNT == count);
    CheckGuard(0);
    raw.kind = UTL_XML_TEXT_CDATA;
    LOGASSERT(!Utl_Xml_IsTextEqual(raw, expected));
    valid = Utl_Xml_WriteText(raw, g_output, XML_TEST_OUTPUT, &count);
    LOGASSERT(!valid);
    LOGASSERT(XML_TEST_COUNT == count);
    CheckGuard(0);
    encoded.data      = (const byte_t*)"]]&gt;";
    encoded.byteCount = sizeof("]]&gt;") - 1;
    encoded.kind      = UTL_XML_TEXT_CONTENT;
    LOGASSERT(Utl_Xml_IsTextEqual(encoded, expected));
    valid = Utl_Xml_WriteText(encoded, g_output, XML_TEST_OUTPUT, &count);
    LOGASSERT(valid);
    LOGASSERT(expected.byteCount == count);
    compared = CRT_Memory_Compare(g_output, expected.data, count);
    LOGASSERT(0 == compared);
    CheckGuard(count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Sentinel borrows are taken from the current document, so rejection never depends on an expired borrow.
 */
static FORCE_INLINE void CheckDescriptionCase(const DescriptionCase_t* test)
{
    Utl_Xml_Result_t       result;
    Utl_Upnp_Description_t description;
    bool_t                 valid;
    uint32_t               count;
    int32_t                compared;
    result = Utl_Xml_ReadDocument(&g_document, (const byte_t*)test->source, test->sourceBytes);
    LOGASSERT(UTL_XML_COMPLETE == result);
    Utl_Upnp_InitializeDescription(&description);
    LOGASSERT(NULL == description.device);
    LOGASSERT(NULL == description.friendlyName);
    LOGASSERT(NULL == description.modelName);
    description.device       = g_document.root;
    description.friendlyName = g_document.root;
    description.modelName    = g_document.root;
    valid                    = Utl_Upnp_ReadDescription(&g_document, &description);
    LOGASSERT(test->valid == valid);
    if (valid)
    {
        LOGASSERT(NULL == description.modelName);
        valid = Utl_Xml_WriteElementText(description.friendlyName, g_output, XML_TEST_OUTPUT, &count);
        LOGASSERT(valid);
        LOGASSERT((sizeof("TV") - 1) == count);
        compared = CRT_Memory_Compare(g_output, "TV", count);
        LOGASSERT(0 == compared);
    }
    else
    {
        LOGASSERT(g_document.root == description.device);
        LOGASSERT(g_document.root == description.friendlyName);
        LOGASSERT(g_document.root == description.modelName);
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  All fixture state uses fixed storage and the application has no system CRT entry.
 */
uint32_t OS_Application_Main(void)
{
    Utl_Xml_Storage_t storage;
    uint32_t          index;
    OS_Process_SetInteractive(FALSE);
    storage.nodes             = g_nodes;
    storage.attributes        = g_attributes;
    storage.nodeCapacity      = XML_TEST_NODES;
    storage.attributeCapacity = XML_TEST_ATTRIBUTES;
    Utl_Xml_InitializeDocument(&g_document, storage);
    for (index = 0; index < (sizeof(c_cases) / sizeof(Case_t)); ++index)
    {
        CheckCase(&c_cases[index]);
    }
    CheckPrefixes();
    CheckCapacity();
    CheckNamespaces();
    CheckTextKinds();
    CheckDescription();
    for (index = 0; index < (sizeof(c_descriptionCases) / sizeof(DescriptionCase_t)); ++index)
    {
        CheckDescriptionCase(&c_descriptionCases[index]);
    }
    return 0;
}
