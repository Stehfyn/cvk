/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Independent SSDP wire vectors for UDA 1.1 sections 1.1 and 1.3. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_ssdp.h"
#include "os/os_types.h"
#include "utl/utl_http.h"
#include <stdint.h>
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include "crt/crt_memory.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define SSDP_TEST_SENTINEL        (0x5A)
#define SSDP_TEST_FIELDS          (16)
#define SSDP_TEST_OUTPUT_BYTES    (512)
#define SSDP_TEST_REQUIRED_FIELDS (6)
#define SSDP_TEST_CACHE_SLOT      (0)
#define SSDP_TEST_EXT_SLOT        (1)
#define SSDP_TEST_ST_SLOT         (4)
#define SSDP_TEST_USN_SLOT        (5)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Independent target syntax expectation.
 */
typedef struct TargetCase_s
{
    const char_t* text;      ///< Counted target bytes.
    uint32_t      byteCount; ///< Explicit extent.
    bool_t        valid;     ///< Expected grammar result.
} TargetCase_t;

/*************************************************************************************************/
/**
 * \brief Independent lifetime directive expectation.
 */
typedef struct CacheCase_s
{
    const char_t* text;      ///< Counted directive list.
    uint32_t      byteCount; ///< Explicit extent.
    bool_t        valid;     ///< Expected parse result.
    uint64_t      age;       ///< Expected successful lifetime.
} CacheCase_t;

/*************************************************************************************************/
/** \brief Caller-owned HTTP fields and serialization storage for one test run. */
typedef struct Workspace_s
{
    Utl_Http_Head_t  head;                           ///< Parsed message view.
    Utl_Http_Field_t fields[SSDP_TEST_FIELDS];       ///< Header field slots.
    byte_t           output[SSDP_TEST_OUTPUT_BYTES]; ///< Counted serialization buffer.
} Workspace_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

/** Independent expected protocol fields and datagrams. */
static const char_t       c_target[]        = "urn:dial-multiscreen-org:service:dial:1";
static const char_t       c_uuid[]          = "uuid:legacy-receiver";
static const char_t       c_unknown[]       = "X-unknown";
static const char_t       c_search[]        = "M-SEARCH * HTTP/1.1\r\n"
                                              "Host: 239.255.255.250:1900\r\n"
                                              "MAN: \"ssdp:discover\"\r\n"
                                              "MX: 2\r\n"
                                              "ST: urn:dial-multiscreen-org:service:dial:1\r\n\r\n";
static const char_t       c_maximumSearch[] = "M-SEARCH * HTTP/1.1\r\n"
                                              "Host: 239.255.255.250:1900\r\n"
                                              "MAN: \"ssdp:discover\"\r\n"
                                              "MX: 4294967295\r\n"
                                              "ST: urn:dial-multiscreen-org:service:dial:1\r\n\r\n";
static const char_t       c_response[]      = "HTTP/1.1 200 OK\r\n"
                                              "cAcHe-CoNtRoL: max-age=1800\r\n"
                                              "EXT:\r\n"
                                              "LOCATION: http://192.0.2.8:8008/ssdp/device-desc.xml\r\n"
                                              "SERVER: Test/1 UPnP/1.0 Fixture/1\r\n"
                                              "ST: urn:dial-multiscreen-org:service:dial:1\r\n"
                                              "USN: uuid:legacy-receiver::urn:dial-multiscreen-org:service:dial:1\r\n"
                                              "X-unknown: ignored\r\n\r\n";

static const TargetCase_t c_targets[] = {
    {"ssdp:all", sizeof("ssdp:all") - 1, TRUE},
    {"upnp:rootdevice", sizeof("upnp:rootdevice") - 1, TRUE},
    {"uuid:legacy-receiver", sizeof("uuid:legacy-receiver") - 1, TRUE},
    {"urn:schemas-upnp-org:device:MediaRenderer:1", sizeof("urn:schemas-upnp-org:device:MediaRenderer:1") - 1, TRUE},
    {"urn:dial-multiscreen-org:service:dial:1", sizeof("urn:dial-multiscreen-org:service:dial:1") - 1, TRUE},
    {"", sizeof("") - 1, FALSE},
    {"UUID:receiver", sizeof("UUID:receiver") - 1, FALSE},
    {"uuid:", sizeof("uuid:") - 1, FALSE},
    {"uuid:a::b", sizeof("uuid:a::b") - 1, FALSE},
    {"urn::service:dial:1", sizeof("urn::service:dial:1") - 1, FALSE},
    {"urn:domain:other:type:1", sizeof("urn:domain:other:type:1") - 1, FALSE},
    {"urn:domain:service::1", sizeof("urn:domain:service::1") - 1, FALSE},
    {"urn:domain:service:type:0", sizeof("urn:domain:service:type:0") - 1, FALSE},
    {"urn:domain:service:type:-1", sizeof("urn:domain:service:type:-1") - 1, FALSE},
    {"urn:domain:service:type:1x", sizeof("urn:domain:service:type:1x") - 1, FALSE},
    {"urn:domain:service:type:1 ", sizeof("urn:domain:service:type:1 ") - 1, FALSE},
    {"ssdp:all\r\nx:y", sizeof("ssdp:all\r\nx:y") - 1, FALSE},
};

static const CacheCase_t c_cache[] = {
    {"max-age=1800", sizeof("max-age=1800") - 1, TRUE, 1800},
    {"MaX-aGe = 0", sizeof("MaX-aGe = 0") - 1, TRUE, 0},
    {"max-age=18446744073709551615", sizeof("max-age=18446744073709551615") - 1, TRUE, UINT64_MAX},
    {"ignored=\"a,b\", max-age = 37, extension=x", sizeof("ignored=\"a,b\", max-age = 37, extension=x") - 1, TRUE, 37},
    {"ignored=\"a\\\",b\", max-age=52", sizeof("ignored=\"a\\\",b\", max-age=52") - 1, TRUE, 52},
    {"max-age=1, max-age=2", sizeof("max-age=1, max-age=2") - 1, FALSE, 0},
    {"max-age=18446744073709551616", sizeof("max-age=18446744073709551616") - 1, FALSE, 0},
    {"max-age=-1", sizeof("max-age=-1") - 1, FALSE, 0},
    {"max-age=\"10\"", sizeof("max-age=\"10\"") - 1, FALSE, 0},
    {"max-age=1x", sizeof("max-age=1x") - 1, FALSE, 0},
    {"max-age=", sizeof("max-age=") - 1, FALSE, 0},
    {"other=99", sizeof("other=99") - 1, FALSE, 0},
    {"max-age=1, extension=\"unterminated", sizeof("max-age=1, extension=\"unterminated") - 1, FALSE, 0},
    {"", sizeof("") - 1, FALSE, 0},
};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Checks every insufficient search-output capacity.
 *
 * \param[in,out] workspace   Owned output storage
 * \param[in,out] search      Initialized query
 * \param[in,out] expected    Exact encoded extent
 *
 * \retval    None
 */
static FORCE_INLINE void CheckSearchCapacities(Workspace_t*             workspace,
                                               const Utl_Ssdp_Search_t* search,
                                               uint32_t                 expected);

/*************************************************************************************************/
/**
 * \brief Checks target grammar, exact serialization and every short output capacity.
 *
 * \par Requirements:
 *  - 001 Production assertions shall remain active in optimized tests.
 *
 * \param[in,out] workspace   Caller-owned message and output storage
 *
 * \retval None
 */
static FORCE_INLINE void CheckSearch(Workspace_t* workspace);

/*************************************************************************************************/
/**
 * \brief Parses the independent search-response fixture into fixed field slots.
 *
 * \par Requirements:
 *  - 001 Production assertions shall remain active in optimized tests.
 *
 * \param[in,out] workspace   Caller-owned message and output storage
 *
 * \retval None
 */
static FORCE_INLINE void ReadFixture(Workspace_t* workspace);

/*************************************************************************************************/
/**
 * \brief Checks expiry directives and preservation on invalid values.
 *
 * \param[in,out] workspace   Owned parsing storage
 * \param[in,out] response    Current result record
 * \param[in,out] before      Last successfully published record
 *
 * \retval    None
 */
static FORCE_INLINE void CheckCacheDirectives(Workspace_t*         workspace,
                                              Utl_Ssdp_Response_t* response,
                                              Utl_Ssdp_Response_t* before);

/*************************************************************************************************/
/**
 * \brief Checks every truncated response prefix without publishing partial results.
 *
 * \param[in,out] workspace   Caller-owned field storage
 * \param[in,out] response    Last valid response, preserved by every failed read
 *
 * \retval    None
 */
static FORCE_INLINE void CheckResponsePrefixes(Workspace_t* workspace, Utl_Ssdp_Response_t* response);

/*************************************************************************************************/
/**
 * \brief Checks required fields, identity, expiry, extension handling and prefix failures.
 *
 * \par Requirements:
 *  - 001 Production assertions shall remain active in optimized tests.
 *
 * \param[in,out] workspace   Caller-owned message and output storage
 *
 * \retval None
 */
static FORCE_INLINE void CheckResponses(Workspace_t* workspace);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Checks every insufficient search-output capacity.
 */
static FORCE_INLINE void CheckSearchCapacities(Workspace_t*             workspace,
                                               const Utl_Ssdp_Search_t* search,
                                               uint32_t                 expected)
{
    uint32_t capacity;
    uint32_t index;

    for (capacity = 0; capacity < expected; ++capacity)
    {
        CRT_Memory_Set(workspace->output, SSDP_TEST_SENTINEL, sizeof(workspace->output));
        LOGASSERT(0 == Utl_Ssdp_WriteSearch(search, workspace->output, capacity));
        for (index = 0; index < sizeof(workspace->output); ++index)
        {
            LOGASSERT(SSDP_TEST_SENTINEL == (byte_t)workspace->output[index]);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Independent expected wire data and unchanged-output checks exercise specification boundaries.
 */
static FORCE_INLINE void CheckSearch(Workspace_t* workspace)
{
    Utl_Http_Text_t   target;
    Utl_Ssdp_Search_t search;
    uint32_t          expected;
    uint32_t          written;
    Utl_Ssdp_Search_t decoded;
    Utl_Ssdp_Search_t before;
    uint32_t          index;
    Utl_Http_Result_t result;

    target.data      = (const byte_t*)c_target;
    target.byteCount = sizeof(c_target) - 1;
    Utl_Ssdp_InitializeSearch(&search, target, 2);
    expected = sizeof(c_search) - 1;
    LOGASSERT(expected == Utl_Ssdp_GetSearchByteCount(&search));
    CheckSearchCapacities(workspace, &search, expected);
    written = Utl_Ssdp_WriteSearch(&search, workspace->output, expected);
    LOGASSERT(0 == CRT_Memory_Compare(c_search, workspace->output, expected));
    LOGASSERT(SSDP_TEST_SENTINEL == (byte_t)workspace->output[expected]);
    workspace->head.fields        = workspace->fields;
    workspace->head.fieldCapacity = SSDP_TEST_FIELDS;
    LOGASSERT(UTL_HTTP_COMPLETE ==
              Utl_Http_ReadHead(&workspace->head, UTL_HTTP_REQUEST, (const byte_t*)workspace->output, expected));
    LOGASSERT(Utl_Ssdp_ReadSearch(&decoded, &workspace->head));
    LOGASSERT(2 == decoded.maximumWait);
    LOGASSERT(Utl_Http_IsTextEqual(target, decoded.target, FALSE));
    LOGASSERT(expected == written);

    before = decoded;
    for (index = 0; index < expected; ++index)
    {
        result = Utl_Http_ReadHead(&workspace->head, UTL_HTTP_REQUEST, (const byte_t*)c_search, index);
        LOGASSERT(UTL_HTTP_INCOMPLETE == result);
        LOGASSERT(!Utl_Ssdp_ReadSearch(&decoded, &workspace->head));
        LOGASSERT(0 == CRT_Memory_Compare(&before, &decoded, sizeof(Utl_Ssdp_Search_t)));
    }
    for (index = 0; index < (sizeof(c_targets) / sizeof(TargetCase_t)); ++index)
    {
        target.data      = (const byte_t*)c_targets[index].text;
        target.byteCount = c_targets[index].byteCount;
        LOGASSERT(c_targets[index].valid == Utl_Ssdp_IsTarget(target));
    }
    search.maximumWait = UINT32_MAX;
    written            = Utl_Ssdp_WriteSearch(&search, workspace->output, sizeof(workspace->output));
    LOGASSERT(0 == CRT_Memory_Compare(c_maximumSearch, workspace->output, sizeof(c_maximumSearch) - 1));
    LOGASSERT(UTL_HTTP_COMPLETE == Utl_Http_ReadHead(&workspace->head,
                                                  UTL_HTTP_REQUEST,
                                                  (const byte_t*)workspace->output,
                                                  sizeof(c_maximumSearch) - 1));
    LOGASSERT(Utl_Ssdp_ReadSearch(&decoded, &workspace->head));
    LOGASSERT(UINT32_MAX == decoded.maximumWait);
    LOGASSERT(sizeof(c_maximumSearch) - 1 == written);
    search.maximumWait = 0;
    LOGASSERT(0 == Utl_Ssdp_GetSearchByteCount(&search));
    LOGASSERT(0 == Utl_Ssdp_WriteSearch(&search, workspace->output, sizeof(workspace->output)));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Independent expected wire data and unchanged-output checks exercise specification boundaries.
 */
static FORCE_INLINE void ReadFixture(Workspace_t* workspace)
{
    workspace->head.fields        = workspace->fields;
    workspace->head.fieldCapacity = SSDP_TEST_FIELDS;
    LOGASSERT(
        UTL_HTTP_COMPLETE ==
        Utl_Http_ReadHead(&workspace->head, UTL_HTTP_RESPONSE, (const byte_t*)c_response, sizeof(c_response) - 1));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Checks expiry directives and preservation on invalid values.
 */
static FORCE_INLINE void CheckCacheDirectives(Workspace_t*         workspace,
                                              Utl_Ssdp_Response_t* response,
                                              Utl_Ssdp_Response_t* before)
{
    uint32_t index;
    bool_t   accepted;

    for (index = 0; index < (sizeof(c_cache) / sizeof(CacheCase_t)); ++index)
    {
        ReadFixture(workspace);
        workspace->fields[SSDP_TEST_CACHE_SLOT].value.data      = (const byte_t*)c_cache[index].text;
        workspace->fields[SSDP_TEST_CACHE_SLOT].value.byteCount = c_cache[index].byteCount;
        accepted                                                = Utl_Ssdp_ReadResponse(response, &workspace->head);
        LOGASSERT(c_cache[index].valid == accepted);
        if (accepted)
        {
            LOGASSERT(c_cache[index].age == response->maximumAge);
            *before = *response;
        }
        else
        {
            LOGASSERT(0 == CRT_Memory_Compare(before, response, sizeof(Utl_Ssdp_Response_t)));
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every prefix stops before the head terminator and must retain the last complete response.
 */
static FORCE_INLINE void CheckResponsePrefixes(Workspace_t* workspace, Utl_Ssdp_Response_t* response)
{
    Utl_Ssdp_Response_t before;
    uint32_t            index;
    Utl_Http_Result_t   result;

    before = *response;
    for (index = 0; index < (sizeof(c_response) - 1); ++index)
    {
        result = Utl_Http_ReadHead(&workspace->head, UTL_HTTP_RESPONSE, (const byte_t*)c_response, index);
        LOGASSERT(UTL_HTTP_INCOMPLETE == result);
        LOGASSERT(!Utl_Ssdp_ReadResponse(response, &workspace->head));
        LOGASSERT(0 == CRT_Memory_Compare(&before, response, sizeof(Utl_Ssdp_Response_t)));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Independent expected wire data and unchanged-output checks exercise specification boundaries.
 */
static FORCE_INLINE void CheckResponses(Workspace_t* workspace)
{
    Utl_Ssdp_Response_t response;
    Utl_Http_Text_t     target;
    Utl_Ssdp_Response_t before;
    uint32_t            index;

    ReadFixture(workspace);
    LOGASSERT(Utl_Ssdp_ReadResponse(&response, &workspace->head));
    LOGASSERT(1800 == response.maximumAge);
    target.data      = (const byte_t*)c_target;
    target.byteCount = sizeof(c_target) - 1;
    LOGASSERT(Utl_Http_IsTextEqual(target, response.target, FALSE));
    LOGASSERT((sizeof(c_uuid) - 1) == response.uuid.byteCount);
    LOGASSERT(0 == CRT_Memory_Compare(c_uuid, response.uuid.data, response.uuid.byteCount));
    before = response;
    CheckCacheDirectives(workspace, &response, &before);
    for (index = 0; index < SSDP_TEST_REQUIRED_FIELDS; ++index)
    {
        ReadFixture(workspace);
        workspace->fields[index].name.data      = (const byte_t*)c_unknown;
        workspace->fields[index].name.byteCount = sizeof(c_unknown) - 1;
        LOGASSERT(!Utl_Ssdp_ReadResponse(&response, &workspace->head));
        LOGASSERT(0 == CRT_Memory_Compare(&before, &response, sizeof(Utl_Ssdp_Response_t)));
        ReadFixture(workspace);
        workspace->fields[workspace->head.fieldCount] = workspace->fields[index];
        ++workspace->head.fieldCount;
        LOGASSERT(!Utl_Ssdp_ReadResponse(&response, &workspace->head));
        LOGASSERT(0 == CRT_Memory_Compare(&before, &response, sizeof(Utl_Ssdp_Response_t)));
    }
    ReadFixture(workspace);
    workspace->fields[SSDP_TEST_ST_SLOT].value.data      = (const byte_t*)UTL_SSDP_ALL;
    workspace->fields[SSDP_TEST_ST_SLOT].value.byteCount = sizeof(UTL_SSDP_ALL) - 1;
    LOGASSERT(!Utl_Ssdp_ReadResponse(&response, &workspace->head));
    ReadFixture(workspace);
    workspace->fields[SSDP_TEST_ST_SLOT].value.data      = (const byte_t*)UTL_SSDP_ROOT_DEVICE;
    workspace->fields[SSDP_TEST_ST_SLOT].value.byteCount = sizeof(UTL_SSDP_ROOT_DEVICE) - 1;
    LOGASSERT(!Utl_Ssdp_ReadResponse(&response, &workspace->head));
    ReadFixture(workspace);
    workspace->fields[SSDP_TEST_ST_SLOT].value.data      = (const byte_t*)c_uuid;
    workspace->fields[SSDP_TEST_ST_SLOT].value.byteCount = sizeof(c_uuid) - 1;
    workspace->fields[SSDP_TEST_USN_SLOT].value          = workspace->fields[SSDP_TEST_ST_SLOT].value;
    LOGASSERT(Utl_Ssdp_ReadResponse(&response, &workspace->head));
    LOGASSERT((sizeof(c_uuid) - 1) == response.uuid.byteCount);
    before = response;
    ReadFixture(workspace);
    workspace->head.status = UTL_HTTP_STATUS_NO_CONTENT;
    LOGASSERT(!Utl_Ssdp_ReadResponse(&response, &workspace->head));
    ReadFixture(workspace);
    workspace->head.minor = 0;
    LOGASSERT(!Utl_Ssdp_ReadResponse(&response, &workspace->head));
    ReadFixture(workspace);
    workspace->fields[SSDP_TEST_EXT_SLOT].value.data      = (const byte_t*)c_unknown;
    workspace->fields[SSDP_TEST_EXT_SLOT].value.byteCount = sizeof(c_unknown) - 1;
    LOGASSERT(!Utl_Ssdp_ReadResponse(&response, &workspace->head));
    CheckResponsePrefixes(workspace, &response);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Run independent wire, rejection and ownership checks without the system CRT.
 */
uint32_t OS_Application_Main(void)
{
    Workspace_t workspace;

    OS_Process_SetInteractive(FALSE);
    CheckSearch(&workspace);
    CheckResponses(&workspace);

    return 0;
}
