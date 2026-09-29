/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Independent URI examples from RFC 3986 sections 1.1.2 and 5.4, plus malformed
 * grammar, host, capacity, encoded-byte and transactional-publication checks.
 * https://www.rfc-editor.org/rfc/rfc3986.html
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_url.h"
#include "utl/utl_http.h"
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

#define URL_TEST_CAPACITY      (256)
#define URL_TEST_GUARD         (0xA5)
#define URL_TEST_COUNT         (123)
#define URL_TEST_PORT          (4242)
#define URL_TEST_IPV4_SENTINEL (0xABCDEF01)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Independent accepted/rejected input and expected host grammar.
 */
typedef struct Case_s
{
    const char_t*      text;      ///< Literal input with an explicit count.
    uint32_t           byteCount; ///< Input extent excluding its test terminator.
    bool_t             valid;     ///< Expected generic-grammar result.
    Utl_Url_HostKind_t host;      ///< Expected kind for valid input.
} Case_t;

/*************************************************************************************************/
/**
 * \brief Independent HTTP endpoint and origin-form expectation.
 */
typedef struct HttpCase_s
{
    const char_t* text;        ///< Counted URI-reference.
    const char_t* target;      ///< Expected target, or NULL for rejection.
    uint32_t      byteCount;   ///< Complete input extent.
    uint32_t      targetBytes; ///< Exact output extent.
    bool_t        secure;      ///< Expected HTTPS flag.
    uint16_t      port;        ///< Effective transport port.
} HttpCase_t;

/*************************************************************************************************/
/** \brief Independent numeric IPv4 interpretation and exact syntax expectation. */
typedef struct Ipv4Case_s
{
    const char_t* text;      ///< Exact candidate text.
    uint32_t      byteCount; ///< Extent, including deliberate embedded zero bytes.
    uint32_t      address;   ///< Expected first-octet-most-significant value.
    bool_t        valid;     ///< Expected acceptance.
} Ipv4Case_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static byte_t       g_output[URL_TEST_CAPACITY];
static byte_t       g_decoded[URL_TEST_CAPACITY];

static const Case_t c_cases[] = {
    {"", sizeof("") - 1, TRUE, UTL_URL_HOST_NONE},
    {"ftp://ftp.is.co.za/rfc/rfc1808.txt", sizeof("ftp://ftp.is.co.za/rfc/rfc1808.txt") - 1, TRUE, UTL_URL_HOST_NAME},
    {"http://www.ietf.org/rfc/rfc2396.txt", sizeof("http://www.ietf.org/rfc/rfc2396.txt") - 1, TRUE, UTL_URL_HOST_NAME},
    {"ldap://[2001:db8::7]/c=GB?objectClass?one",
     sizeof("ldap://[2001:db8::7]/c=GB?objectClass?one") - 1,
     TRUE,
     UTL_URL_HOST_IPV6},
    {"mailto:John.Doe@example.com", sizeof("mailto:John.Doe@example.com") - 1, TRUE, UTL_URL_HOST_NONE},
    {"news:comp.infosystems.www.servers.unix",
     sizeof("news:comp.infosystems.www.servers.unix") - 1,
     TRUE,
     UTL_URL_HOST_NONE},
    {"tel:+1-816-555-1212", sizeof("tel:+1-816-555-1212") - 1, TRUE, UTL_URL_HOST_NONE},
    {"telnet://192.0.2.16:80/", sizeof("telnet://192.0.2.16:80/") - 1, TRUE, UTL_URL_HOST_IPV4},
    {"urn:oasis:names:specification:docbook:dtd:xml:4.1.2",
     sizeof("urn:oasis:names:specification:docbook:dtd:xml:4.1.2") - 1,
     TRUE,
     UTL_URL_HOST_NONE},
    {"g:h", sizeof("g:h") - 1, TRUE, UTL_URL_HOST_NONE},
    {"g", sizeof("g") - 1, TRUE, UTL_URL_HOST_NONE},
    {"./g", sizeof("./g") - 1, TRUE, UTL_URL_HOST_NONE},
    {"../g", sizeof("../g") - 1, TRUE, UTL_URL_HOST_NONE},
    {"g/../h", sizeof("g/../h") - 1, TRUE, UTL_URL_HOST_NONE},
    {"/./g", sizeof("/./g") - 1, TRUE, UTL_URL_HOST_NONE},
    {"//g", sizeof("//g") - 1, TRUE, UTL_URL_HOST_NAME},
    {"?y", sizeof("?y") - 1, TRUE, UTL_URL_HOST_NONE},
    {"#s", sizeof("#s") - 1, TRUE, UTL_URL_HOST_NONE},
    {"g?y#s", sizeof("g?y#s") - 1, TRUE, UTL_URL_HOST_NONE},
    {";x", sizeof(";x") - 1, TRUE, UTL_URL_HOST_NONE},
    {"//@:?#", sizeof("//@:?#") - 1, TRUE, UTL_URL_HOST_NAME},
    {"http://host//a/%2f?x=%3F#z", sizeof("http://host//a/%2f?x=%3F#z") - 1, TRUE, UTL_URL_HOST_NAME},
    {"h://a%2eb:00080/", sizeof("h://a%2eb:00080/") - 1, TRUE, UTL_URL_HOST_NAME},
    {"http://host:65536", sizeof("http://host:65536") - 1, TRUE, UTL_URL_HOST_NAME},
    {"http://256.0.0.1/", sizeof("http://256.0.0.1/") - 1, TRUE, UTL_URL_HOST_NAME},
    {"http://01.2.3.4/", sizeof("http://01.2.3.4/") - 1, TRUE, UTL_URL_HOST_NAME},
    {"http://0.0.0.0/", sizeof("http://0.0.0.0/") - 1, TRUE, UTL_URL_HOST_IPV4},
    {"http://255.255.255.255/", sizeof("http://255.255.255.255/") - 1, TRUE, UTL_URL_HOST_IPV4},
    {"http://[::]/", sizeof("http://[::]/") - 1, TRUE, UTL_URL_HOST_IPV6},
    {"http://[::1]/", sizeof("http://[::1]/") - 1, TRUE, UTL_URL_HOST_IPV6},
    {"http://[1::]/", sizeof("http://[1::]/") - 1, TRUE, UTL_URL_HOST_IPV6},
    {"http://[1:2:3:4:5:6:7:8]/", sizeof("http://[1:2:3:4:5:6:7:8]/") - 1, TRUE, UTL_URL_HOST_IPV6},
    {"http://[::ffff:192.0.2.128]/", sizeof("http://[::ffff:192.0.2.128]/") - 1, TRUE, UTL_URL_HOST_IPV6},
    {"http://[1:2:3:4:5:6:192.0.2.1]/", sizeof("http://[1:2:3:4:5:6:192.0.2.1]/") - 1, TRUE, UTL_URL_HOST_IPV6},
    {"http://[v1.a:b]/", sizeof("http://[v1.a:b]/") - 1, TRUE, UTL_URL_HOST_FUTURE},
    {"http://[Vf.a!$&'()*+,;=:]/", sizeof("http://[Vf.a!$&'()*+,;=:]/") - 1, TRUE, UTL_URL_HOST_FUTURE},
    {":x", sizeof(":x") - 1, FALSE, UTL_URL_HOST_NONE},
    {"1a:x", sizeof("1a:x") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://host:abc/", sizeof("http://host:abc/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://a@b@c/", sizeof("http://a@b@c/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://host:12:34/", sizeof("http://host:12:34/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://a b/", sizeof("http://a b/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://a\\b/", sizeof("http://a\\b/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"a/%", sizeof("a/%") - 1, FALSE, UTL_URL_HOST_NONE},
    {"a/%0", sizeof("a/%0") - 1, FALSE, UTL_URL_HOST_NONE},
    {"a/%0g", sizeof("a/%0g") - 1, FALSE, UTL_URL_HOST_NONE},
    {"a/%g0", sizeof("a/%g0") - 1, FALSE, UTL_URL_HOST_NONE},
    {"a#b#c", sizeof("a#b#c") - 1, FALSE, UTL_URL_HOST_NONE},
    {"a?[x]", sizeof("a?[x]") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[]/", sizeof("http://[]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[::1", sizeof("http://[::1") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[::1]junk/", sizeof("http://[::1]junk/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[1:2:3:4:5:6:7]/", sizeof("http://[1:2:3:4:5:6:7]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[1:2:3:4:5:6:7:8:9]/", sizeof("http://[1:2:3:4:5:6:7:8:9]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[1:2:3:4:5:6:7:8::]/", sizeof("http://[1:2:3:4:5:6:7:8::]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[1::2::3]/", sizeof("http://[1::2::3]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[:::]/", sizeof("http://[:::]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[:1]/", sizeof("http://[:1]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[::1:]/", sizeof("http://[::1:]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[::ffff:256.0.0.1]/", sizeof("http://[::ffff:256.0.0.1]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[::ffff:01.2.3.4]/", sizeof("http://[::ffff:01.2.3.4]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[::192.0.2.1:1]/", sizeof("http://[::192.0.2.1:1]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[12345::]/", sizeof("http://[12345::]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[v.a]/", sizeof("http://[v.a]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[v1.]/", sizeof("http://[v1.]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[v1.%20]/", sizeof("http://[v1.%20]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://[fe80::1%25eth0]/", sizeof("http://[fe80::1%25eth0]/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://a\0b/", sizeof("http://a\0b/") - 1, FALSE, UTL_URL_HOST_NONE},
    {"http://a/\x80", sizeof("http://a/\x80") - 1, FALSE, UTL_URL_HOST_NONE}};

static const HttpCase_t c_httpCases[] = {
    {"http://host", "/", sizeof("http://host") - 1, sizeof("/") - 1, FALSE, UTL_HTTP_DEFAULT_PORT},
    {"HTTPS://host:", "/", sizeof("HTTPS://host:") - 1, sizeof("/") - 1, TRUE, UTL_HTTP_SECURE_PORT},
    {"http://host?", "/?", sizeof("http://host?") - 1, sizeof("/?") - 1, FALSE, UTL_HTTP_DEFAULT_PORT},
    {"http://host?x=%2F#omit",
     "/?x=%2F",
     sizeof("http://host?x=%2F#omit") - 1,
     sizeof("/?x=%2F") - 1,
     FALSE,
     UTL_HTTP_DEFAULT_PORT},
    {"hTtPs://host:4242//a%2fb??x#omit",
     "//a%2fb??x",
     sizeof("hTtPs://host:4242//a%2fb??x#omit") - 1,
     sizeof("//a%2fb??x") - 1,
     TRUE,
     URL_TEST_PORT},
    {"http://host:00080/a", "/a", sizeof("http://host:00080/a") - 1, sizeof("/a") - 1, FALSE, UTL_HTTP_DEFAULT_PORT},
    {"http://[::1]:65535/", "/", sizeof("http://[::1]:65535/") - 1, sizeof("/") - 1, FALSE, UINT16_MAX},
    {"http://[v1.a]:0/", "/", sizeof("http://[v1.a]:0/") - 1, sizeof("/") - 1, FALSE, 0},
    {"ftp://host/a", NULL, sizeof("ftp://host/a") - 1, 0, FALSE, 0},
    {"//host/a", NULL, sizeof("//host/a") - 1, 0, FALSE, 0},
    {"http:a", NULL, sizeof("http:a") - 1, 0, FALSE, 0},
    {"http:///a", NULL, sizeof("http:///a") - 1, 0, FALSE, 0},
    {"http://@host/a", NULL, sizeof("http://@host/a") - 1, 0, FALSE, 0},
    {"http://user:password@host/a", NULL, sizeof("http://user:password@host/a") - 1, 0, FALSE, 0},
    {"http://host:65536/a", NULL, sizeof("http://host:65536/a") - 1, 0, FALSE, 0},
};

static const char_t     c_model[]   = "HtTp://user:pass@[2001:db8::1]:0080/a%2Fb?x=1??#tail";
static const char_t     c_encoded[] = "%00%2f%FF%41+%25";
static const byte_t     c_decoded[] = {0, 0x2F, 0xFF, 0x41, 0x2B, 0x25};

static const Ipv4Case_t c_ipv4Cases[] = {
    {"0.0.0.0", sizeof("0.0.0.0") - 1, 0, TRUE},
    {"127.0.0.1", sizeof("127.0.0.1") - 1, 0x7F000001, TRUE},
    {"192.0.2.1", sizeof("192.0.2.1") - 1, 0xC0000201, TRUE},
    {"255.255.255.255", sizeof("255.255.255.255") - 1, UINT32_MAX, TRUE},
    {"1.2.3.4", sizeof("1.2.3.4") - 1, 0x01020304, TRUE},
    {"10.20.30.40", sizeof("10.20.30.40") - 1, 0x0A141E28, TRUE},
    {"255.0.128.1", sizeof("255.0.128.1") - 1, 0xFF008001, TRUE},
    {"", sizeof("") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1", sizeof("1") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.2.3", sizeof("1.2.3") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.2.3.", sizeof("1.2.3.") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {".1.2.3", sizeof(".1.2.3") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.2.3.4.5", sizeof("1.2.3.4.5") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"01.2.3.4", sizeof("01.2.3.4") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.02.3.4", sizeof("1.02.3.4") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.2.003.4", sizeof("1.2.003.4") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.2.3.04", sizeof("1.2.3.04") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"256.0.0.1", sizeof("256.0.0.1") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.256.0.1", sizeof("1.256.0.1") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.2.256.1", sizeof("1.2.256.1") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.2.3.256", sizeof("1.2.3.256") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"0000.2.3.4", sizeof("0000.2.3.4") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"0x7f.0.0.1", sizeof("0x7f.0.0.1") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"+1.2.3.4", sizeof("+1.2.3.4") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"-1.2.3.4", sizeof("-1.2.3.4") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {" 1.2.3.4", sizeof(" 1.2.3.4") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.2.3.4 ", sizeof("1.2.3.4 ") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.2.3.4:80", sizeof("1.2.3.4:80") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1..3.4", sizeof("1..3.4") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
    {"1.2.3.4\0x", sizeof("1.2.3.4\0x") - 1, URL_TEST_IPV4_SENTINEL, FALSE},
};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Checks one counted component against an independently expected value.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 The expected value shall contain exactly byteCount readable bytes.
 * \param[in] text        Actual component
 * \param[in] expected    Required expected bytes
 * \param[in] byteCount   Expected extent
 * \retval    None
 */
static FORCE_INLINE void CheckText(Utl_Url_Text_t text, const char_t* expected, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Checks that output guard bytes remain unchanged.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Offset shall be within the retained output extent.
 * \param[in] offset   First byte that must retain its guard value
 * \retval    None
 */
static FORCE_INLINE void CheckGuard(uint32_t offset);

/*************************************************************************************************/
/**
 * \brief Exercises every insufficient capacity and exact byte recomposition.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Input shall be a valid case smaller than fixture output capacity.
 * \param[in] test   Independent expected syntax
 * \param[in] url    Parsed input model
 * \retval    None
 */
static FORCE_INLINE void CheckWrite(const Case_t* test, const Utl_Url_t* url);

/*************************************************************************************************/
/**
 * \brief Checks that a rejected read preserves the populated sentinel model.
 *
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Component presence, exact bytes and host grammar shall remain unchanged.
 *
 * \param[in] url   Candidate after rejected input
 *
 * \retval    None
 */
static FORCE_INLINE void CheckUnchanged(const Utl_Url_t* url);

/*************************************************************************************************/
/**
 * \brief Exercises accepted and rejected complete syntax without padding comparisons.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Invalid reads shall preserve the known nonempty model's recomposition and presence flags.
 * \retval None
 */
static FORCE_INLINE void CheckCases(void);

/*************************************************************************************************/
/**
 * \brief Checks component boundaries, empty delimiters and native port derivation.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Generic syntax shall remain separate from native port range limits.
 * \retval None
 */
static FORCE_INLINE void CheckComponents(void);

/*************************************************************************************************/
/**
 * \brief Checks independent percent-decoded octets and transactional failures.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Literal plus, NUL, high octets and percent shall preserve their byte meanings.
 * \retval None
 */
static FORCE_INLINE void CheckDecoding(void);

/*************************************************************************************************/
/**
 * \brief Checks complete origin targets and every short capacity.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Query escapes, empty paths and fragments shall match independent expected bytes.
 * \param[in] test   Required independent expectation
 * \retval    None
 */
static FORCE_INLINE void CheckHttpTarget(const HttpCase_t* test);

/*************************************************************************************************/
/**
 * \brief Checks rejection without partial output publication.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Every endpoint field, target byte and count shall survive HTTP profile rejection.
 * \param[in] test   Required independent expectation
 * \retval    None
 */
static FORCE_INLINE void CheckHttpFailure(const HttpCase_t* test);

/*************************************************************************************************/
/**
 * \brief Checks HTTP endpoint derivation and request targets.
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Generic syntax shall remain separate from HTTP and native transport restrictions.
 * \retval None
 */
static FORCE_INLINE void CheckHttpCases(void);

/*************************************************************************************************/
/**
 * \brief Checks IPv4 writer output and every short capacity.
 *
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Failure shall preserve guarded output and success shall match independent bytes.
 *
 * \param[in] test   Independently valid canonical case
 *
 * \retval    None
 */
static FORCE_INLINE void CheckIpv4Output(const Ipv4Case_t* test);

/*************************************************************************************************/
/**
 * \brief Checks canonical IPv4 parsing and guarded serialization.
 *
 * \par Details:
 *  Assertions pass or the process fails.
 *
 * \par Requirements:
 *  - 001 Independent values and guards shall be checked without relying on a round trip alone.
 *
 * \retval None
 */
static FORCE_INLINE void CheckIpv4(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Empty spans may have a NULL base; no zero-length memory call requires it.
 */
static FORCE_INLINE void CheckText(Utl_Url_Text_t text, const char_t* expected, uint32_t byteCount)
{
    int32_t compared;

    LOGASSERT(byteCount == text.byteCount);
    if (0 < byteCount)
    {
        compared = CRT_Memory_Compare(text.data, expected, byteCount);
        LOGASSERT(0 == compared);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The entire unused tail is checked, including the first byte beyond capacity.
 */
static FORCE_INLINE void CheckGuard(uint32_t offset)
{
    uint32_t index;

    LOGASSERT(URL_TEST_CAPACITY >= offset);
    for (index = offset; index < URL_TEST_CAPACITY; ++index)
    {
        LOGASSERT(URL_TEST_GUARD == (byte_t)g_output[index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Expected serialized bytes are input vectors, independent of serializer output.
 */
static FORCE_INLINE void CheckWrite(const Case_t* test, const Utl_Url_t* url)
{
    uint32_t count;
    bool_t   accepted;
    uint32_t capacity;
    int32_t  compared;

    count    = URL_TEST_COUNT;
    accepted = Utl_Url_GetByteCount(url, &count);
    LOGASSERT(accepted);
    LOGASSERT(test->byteCount == count);
    LOGASSERT(URL_TEST_CAPACITY > count);
    for (capacity = 0; capacity < test->byteCount; ++capacity)
    {
        CRT_Memory_Set(g_output, URL_TEST_GUARD, sizeof(g_output));
        count    = URL_TEST_COUNT;
        accepted = Utl_Url_Write(url, g_output, capacity, &count);
        LOGASSERT(!accepted);
        LOGASSERT(URL_TEST_COUNT == count);
        CheckGuard(0);
    }
    CRT_Memory_Set(g_output, URL_TEST_GUARD, sizeof(g_output));
    accepted = Utl_Url_Write(url, g_output, test->byteCount, &count);
    LOGASSERT(accepted);
    LOGASSERT(test->byteCount == count);
    compared = CRT_Memory_Compare(g_output, test->text, count);
    LOGASSERT(0 == compared);
    CheckGuard(count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Comparison does not inspect unspecified structure padding.
 */
static FORCE_INLINE void CheckUnchanged(const Utl_Url_t* url)
{
    bool_t   accepted;
    uint32_t count;
    int32_t  compared;

    accepted = Utl_Url_Write(url, g_output, URL_TEST_CAPACITY, &count);
    LOGASSERT(accepted);
    LOGASSERT((sizeof(c_model) - 1) == count);
    compared = CRT_Memory_Compare(g_output, c_model, count);
    LOGASSERT(0 == compared);
    LOGASSERT(url->hasScheme && url->hasAuthority && url->hasUserInfo);
    LOGASSERT(url->hasPort && url->hasQuery && url->hasFragment);
    LOGASSERT(UTL_URL_HOST_IPV6 == url->hostKind);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Invalid input follows a fully populated model. Recomposition and component
 *  presence catch partial mutation without relying on unspecified object padding.
 */
static FORCE_INLINE void CheckCases(void)
{
    uint32_t  index;
    bool_t    accepted;
    Utl_Url_t url;

    for (index = 0; index < (sizeof(c_cases) / sizeof(Case_t)); ++index)
    {
        accepted = Utl_Url_Read(&url, (const byte_t*)c_model, sizeof(c_model) - 1);
        LOGASSERT(accepted);
        accepted = Utl_Url_Read(&url, (const byte_t*)c_cases[index].text, c_cases[index].byteCount);
        LOGASSERT(c_cases[index].valid == accepted);
        if (accepted)
        {
            LOGASSERT(c_cases[index].host == url.hostKind);
            CheckWrite(&c_cases[index], &url);
        }
        else
        {
            CheckUnchanged(&url);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A component's absence differs from an explicitly empty value. A port that is
 *  too large for native networking remains valid generic URI syntax.
 */
static FORCE_INLINE void CheckComponents(void)
{
    bool_t    accepted;
    Utl_Url_t url;
    uint16_t  port;
    uint32_t  count;

    accepted = Utl_Url_Read(&url, (const byte_t*)c_model, sizeof(c_model) - 1);
    LOGASSERT(accepted);
    CheckText(url.scheme, "HtTp", sizeof("HtTp") - 1);
    CheckText(url.userInfo, "user:pass", sizeof("user:pass") - 1);
    CheckText(url.host, "2001:db8::1", sizeof("2001:db8::1") - 1);
    CheckText(url.port, "0080", sizeof("0080") - 1);
    CheckText(url.path, "/a%2Fb", sizeof("/a%2Fb") - 1);
    CheckText(url.query, "x=1??", sizeof("x=1??") - 1);
    CheckText(url.fragment, "tail", sizeof("tail") - 1);
    port     = URL_TEST_PORT;
    accepted = Utl_Url_GetPort(&url, &port);
    LOGASSERT(accepted);
    LOGASSERT(80 == port);
    accepted = Utl_Url_Read(&url, (const byte_t*)"http://host:65535", sizeof("http://host:65535") - 1);
    LOGASSERT(accepted);
    accepted = Utl_Url_GetPort(&url, &port);
    LOGASSERT(accepted);
    LOGASSERT(UINT16_MAX == port);
    accepted = Utl_Url_Read(&url, (const byte_t*)"http://host:65536", sizeof("http://host:65536") - 1);
    LOGASSERT(accepted);
    port     = URL_TEST_PORT;
    accepted = Utl_Url_GetPort(&url, &port);
    LOGASSERT(!accepted);
    LOGASSERT(URL_TEST_PORT == port);
    accepted = Utl_Url_Read(&url, (const byte_t*)"//@:?#", sizeof("//@:?#") - 1);
    LOGASSERT(accepted);
    LOGASSERT(url.hasAuthority && url.hasUserInfo && url.hasPort && url.hasQuery && url.hasFragment);
    LOGASSERT(0 == url.userInfo.byteCount);
    LOGASSERT(0 == url.host.byteCount);
    LOGASSERT(0 == url.port.byteCount);
    LOGASSERT(0 == url.query.byteCount);
    LOGASSERT(0 == url.fragment.byteCount);
    accepted = Utl_Url_GetPort(&url, &port);
    LOGASSERT(!accepted);
    LOGASSERT(URL_TEST_PORT == port);
    Utl_Url_Initialize(&url);
    LOGASSERT(Utl_Url_IsValid(&url));
    accepted = Utl_Url_Read(&url, NULL, 0);
    LOGASSERT(accepted);
    count    = URL_TEST_COUNT;
    accepted = Utl_Url_Write(&url, NULL, 0, &count);
    LOGASSERT(accepted);
    LOGASSERT(0 == count);
    url.path.data      = (const byte_t*)"a:b";
    url.path.byteCount = sizeof("a:b") - 1;
    CRT_Memory_Set(g_output, URL_TEST_GUARD, sizeof(g_output));
    count    = URL_TEST_COUNT;
    accepted = Utl_Url_Write(&url, g_output, URL_TEST_CAPACITY, &count);
    LOGASSERT(!accepted);
    LOGASSERT(URL_TEST_COUNT == count);
    CheckGuard(0);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The expected bytes are explicit octets rather than the result of re-encoding.
 */
static FORCE_INLINE void CheckDecoding(void)
{
    Utl_Url_Text_t text;
    uint32_t       count;
    bool_t         accepted;
    uint32_t       capacity;
    uint32_t       index;
    int32_t        compared;

    text.data      = (const byte_t*)c_encoded;
    text.byteCount = sizeof(c_encoded) - 1;
    count          = URL_TEST_COUNT;
    accepted       = Utl_Url_GetDecodedByteCount(text, &count);
    LOGASSERT(accepted);
    LOGASSERT(sizeof(c_decoded) == count);
    for (capacity = 0; capacity < sizeof(c_decoded); ++capacity)
    {
        CRT_Memory_Set(g_decoded, URL_TEST_GUARD, sizeof(g_decoded));
        count    = URL_TEST_COUNT;
        accepted = Utl_Url_WriteDecoded(text, g_decoded, capacity, &count);
        LOGASSERT(!accepted);
        LOGASSERT(URL_TEST_COUNT == count);
        for (index = 0; index < URL_TEST_CAPACITY; ++index)
        {
            LOGASSERT(URL_TEST_GUARD == g_decoded[index]);
        }
    }
    accepted = Utl_Url_WriteDecoded(text, g_decoded, sizeof(c_decoded), &count);
    LOGASSERT(accepted);
    LOGASSERT(sizeof(c_decoded) == count);
    compared = CRT_Memory_Compare(g_decoded, c_decoded, count);
    LOGASSERT(0 == compared);
    LOGASSERT(URL_TEST_GUARD == g_decoded[count]);
    text.data      = (const byte_t*)"%41%4g";
    text.byteCount = sizeof("%41%4g") - 1;
    CRT_Memory_Set(g_decoded, URL_TEST_GUARD, sizeof(g_decoded));
    count    = URL_TEST_COUNT;
    accepted = Utl_Url_WriteDecoded(text, g_decoded, URL_TEST_CAPACITY, &count);
    LOGASSERT(!accepted);
    LOGASSERT(URL_TEST_COUNT == count);
    for (index = 0; index < URL_TEST_CAPACITY; ++index)
    {
        LOGASSERT(URL_TEST_GUARD == g_decoded[index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All insufficient capacities preserve the entire guarded output and count.
 */
static FORCE_INLINE void CheckHttpTarget(const HttpCase_t* test)
{
    bool_t    accepted;
    Utl_Url_t url;
    uint32_t  count;
    uint32_t  capacity;
    int32_t   compared;

    accepted = Utl_Url_Read(&url, (const byte_t*)test->text, test->byteCount);
    LOGASSERT(accepted);
    accepted = Utl_Http_GetOriginTargetByteCount(&url, &count);
    LOGASSERT(accepted);
    LOGASSERT(test->targetBytes == count);
    for (capacity = 0; capacity < test->targetBytes; ++capacity)
    {
        CRT_Memory_Set(g_output, URL_TEST_GUARD, sizeof(g_output));
        count    = URL_TEST_COUNT;
        accepted = Utl_Http_WriteOriginTarget(&url, g_output, capacity, &count);
        LOGASSERT(!accepted);
        LOGASSERT(URL_TEST_COUNT == count);
        CheckGuard(0);
    }
    accepted = Utl_Http_WriteOriginTarget(&url, g_output, test->targetBytes, &count);
    LOGASSERT(accepted);
    LOGASSERT(test->targetBytes == count);
    compared = CRT_Memory_Compare(g_output, test->target, count);
    LOGASSERT(0 == compared);
    CheckGuard(count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Sentinel endpoint fields detect partial publication before rejecting a URI.
 */
static FORCE_INLINE void CheckHttpFailure(const HttpCase_t* test)
{
    bool_t              accepted;
    Utl_Url_t           url;
    Utl_Http_Endpoint_t endpoint;
    uint32_t            count;

    accepted = Utl_Url_Read(&url, (const byte_t*)test->text, test->byteCount);
    LOGASSERT(accepted);
    endpoint.host.data      = (const byte_t*)test->text;
    endpoint.host.byteCount = test->byteCount;
    endpoint.port           = URL_TEST_PORT;
    endpoint.secure         = TRUE;
    accepted                = Utl_Http_GetUrlEndpoint(&url, &endpoint);
    LOGASSERT(!accepted);
    LOGASSERT((const byte_t*)test->text == endpoint.host.data);
    LOGASSERT(test->byteCount == endpoint.host.byteCount);
    LOGASSERT(URL_TEST_PORT == endpoint.port);
    LOGASSERT(endpoint.secure);
    count    = URL_TEST_COUNT;
    accepted = Utl_Http_GetOriginTargetByteCount(&url, &count);
    LOGASSERT(!accepted);
    LOGASSERT(URL_TEST_COUNT == count);
    CRT_Memory_Set(g_output, URL_TEST_GUARD, sizeof(g_output));
    accepted = Utl_Http_WriteOriginTarget(&url, g_output, URL_TEST_CAPACITY, &count);
    LOGASSERT(!accepted);
    LOGASSERT(URL_TEST_COUNT == count);
    CheckGuard(0);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Port zero and IPvFuture remain generic values; native consumers decide support.
 */
static FORCE_INLINE void CheckHttpCases(void)
{
    uint32_t            index;
    const HttpCase_t*   test;
    bool_t              accepted;
    Utl_Url_t           url;
    Utl_Http_Endpoint_t endpoint;

    for (index = 0; index < (sizeof(c_httpCases) / sizeof(HttpCase_t)); ++index)
    {
        test = &c_httpCases[index];
        if (NULL == test->target)
        {
            CheckHttpFailure(test);
        }
        else
        {
            accepted = Utl_Url_Read(&url, (const byte_t*)test->text, test->byteCount);
            LOGASSERT(accepted);
            accepted = Utl_Http_GetUrlEndpoint(&url, &endpoint);
            LOGASSERT(accepted);
            LOGASSERT(test->port == endpoint.port);
            LOGASSERT(test->secure == endpoint.secure);
            LOGASSERT(url.host.data == endpoint.host.data);
            LOGASSERT(url.host.byteCount == endpoint.host.byteCount);
            CheckHttpTarget(test);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Each short capacity is checked across the entire buffer, then exact canonical output is compared.
 */
static FORCE_INLINE void CheckIpv4Output(const Ipv4Case_t* test)
{
    uint32_t count;
    uint32_t capacity;
    int32_t  comparison;
    count = Utl_Url_GetIpv4ByteCount(test->address);
    LOGASSERT(test->byteCount == count);
    for (capacity = 0; capacity < count; ++capacity)
    {
        CRT_Memory_Set(g_output, URL_TEST_GUARD, sizeof(g_output));
        LOGASSERT(0 == Utl_Url_WriteIpv4(test->address, g_output, capacity));
        CheckGuard(0);
    }
    CRT_Memory_Set(g_output, URL_TEST_GUARD, sizeof(g_output));
    LOGASSERT(count == Utl_Url_WriteIpv4(test->address, g_output, count));
    comparison = CRT_Memory_Compare(g_output, test->text, count);
    LOGASSERT(0 == comparison);
    CheckGuard(count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Canonical literals independently establish byte order, syntax rejection and exact writer lengths.
 */
static FORCE_INLINE void CheckIpv4(void)
{
    uint32_t          index;
    const Ipv4Case_t* test;
    Utl_Url_Text_t    text;
    uint32_t          address;
    bool_t            valid;
    for (index = 0; index < sizeof(c_ipv4Cases) / sizeof(Ipv4Case_t); ++index)
    {
        test           = &c_ipv4Cases[index];
        text.data      = (const byte_t*)test->text;
        text.byteCount = test->byteCount;
        address        = URL_TEST_IPV4_SENTINEL;
        valid          = Utl_Url_ReadIpv4(text, &address);
        LOGASSERT(test->valid == valid);
        LOGASSERT(test->address == address);
        if (valid)
        {
            CheckIpv4Output(test);
        }
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The fixture owns fixed buffers and uses no system CRT or network operations.
 */
uint32_t OS_Application_Main(void)
{
    OS_Process_SetInteractive(FALSE);
    CheckCases();
    CheckComponents();
    CheckDecoding();
    CheckHttpCases();
    CheckIpv4();

    return 0;
}
