/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Independent wire expectations for the production read-only HTTP serving policy.
 * RFC 9110 methods/ranges and RFC 9112 framing/persistence.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cast_http.h"
#include "utl/utl_http.h"
#include "crt/crt_memory.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_process.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define POLICY_TEST_REPRESENTATION_BYTES (10)
#define POLICY_TEST_FIELDS               (16)
#define POLICY_TEST_BYTES                (2048)
#define POLICY_TEST_GUARD                (0xA5)
#define POLICY_TEST_SENTINEL             (777)
#define POLICY_TEST_CYCLES               (100)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Complete independent input and serving expectation. */
typedef struct RequestCase_s
{
    const char_t*          wire;      ///< Counted request bytes.
    uint32_t               byteCount; ///< Exact input extent, including deliberate NUL.
    CastHttp_Result_t      result;    ///< Expected terminal decision.
    uint16_t               status;    ///< Rejection status, otherwise zero.
    bool_t                 headOnly;  ///< Expected HEAD handling.
    bool_t                 options;   ///< Expected OPTIONS handling.
    bool_t                 subtitles; ///< Expected subtitle route.
    bool_t                 keepAlive; ///< Expected connection persistence.
    CastHttp_RangeResult_t range;     ///< Range outcome for ten-byte content.
    uint64_t               offset;    ///< Expected interval or preserved sentinel.
    uint64_t               count;     ///< Expected interval size or preserved sentinel.
} RequestCase_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static const char_t        c_first[]  = "GET /video HTTP/1.1\r\nHost: x\r\n\r\n";
static const char_t        c_second[] = "HEAD /subtitles.vtt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";

static CastHttp_Request_t  g_request;
static Utl_Http_Field_t    g_fields[POLICY_TEST_FIELDS];
static char_t              g_input[POLICY_TEST_BYTES];
static char_t              g_output[POLICY_TEST_BYTES];

static const RequestCase_t c_requests[] = {
    {"GET /video HTTP/1.1\r\nHost: x\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET / HTTP/1.0\r\n\r\n",
     sizeof("GET / HTTP/1.0\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n",
     sizeof("GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nConnection: Upgrade, CLOSE\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nConnection: Upgrade, CLOSE\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\nConnection: close\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nConnection: keep-alive\r\nConnection: close\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nConnection: disclosure\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nConnection: disclosure\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"HEAD /video HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4\r\n\r\n",
     sizeof("HEAD /video HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     TRUE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"OPTIONS * HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4\r\n\r\n",
     sizeof("OPTIONS * HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     TRUE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /subtitles.vtt HTTP/1.1\r\nHost: x\r\n\r\n",
     sizeof("GET /subtitles.vtt HTTP/1.1\r\nHost: x\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     TRUE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"HEAD /subtitles.vtt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n",
     sizeof("HEAD /subtitles.vtt HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     TRUE,
     FALSE,
     TRUE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nContent-Length: 00\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nContent-Length: 0\r\nContent-Length: 00\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_PARTIAL,
     2,
     3},
    {"GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=-3\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=-3\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_PARTIAL,
     7,
     3},
    {"GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=99-,2-4\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=99-,2-4\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_PARTIAL,
     2,
     3},
    {"GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=0-999\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=0-999\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_PARTIAL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=99-\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=99-\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_UNSATISFIABLE,
     777,
     777},
    {"GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4,bad\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4,bad\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nRange: items=2-4\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nRange: items=2-4\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4\r\nRange: bytes=5-6\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4\r\nRange: bytes=5-6\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4\r\nIf-Range: \"old\"\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nRange: bytes=2-4\r\nIf-Range: \"old\"\r\n\r\n") - 1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nRange: "
     "bytes=0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-"
     "0,0-0,0-0,0-0,0-0,0-0,0-0\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nRange: "
            "bytes=0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0,"
            "0-0,0-0,0-0,0-0,0-0,0-0,0-0,0-0\r\n\r\n") -
         1,
     CAST_HTTP_READY,
     0,
     FALSE,
     FALSE,
     FALSE,
     TRUE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"POST /video HTTP/1.1\r\nHost: x\r\n\r\n",
     sizeof("POST /video HTTP/1.1\r\nHost: x\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     405,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"get /video HTTP/1.1\r\nHost: x\r\n\r\n",
     sizeof("get /video HTTP/1.1\r\nHost: x\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     405,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET / HTTP/1.1\r\n\r\n",
     sizeof("GET / HTTP/1.1\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nHost: y\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nHost: y\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\n\r\nxGET / HTTP/1.1\r\nHost: x\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\n\r\nxGET / HTTP/1.1\r\nHost: x\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"HEAD /video HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\n\r\n",
     sizeof("HEAD /video HTTP/1.1\r\nHost: x\r\nContent-Length: 1\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nContent-Length: nope\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nContent-Length: nope\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/2.0\r\nHost: x\r\n\r\n",
     sizeof("GET /video HTTP/2.0\r\nHost: x\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     505,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET / HTTP/1.1\nHost: x\n\n",
     sizeof("GET / HTTP/1.1\nHost: x\n\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nX: a\000b\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nX: a\000b\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nHost : y\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nHost : y\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
    {"GET /video HTTP/1.1\r\nHost: x\r\nConnection: close;bad\r\n\r\n",
     sizeof("GET /video HTTP/1.1\r\nHost: x\r\nConnection: close;bad\r\n\r\n") - 1,
     CAST_HTTP_REJECTED,
     400,
     FALSE,
     FALSE,
     FALSE,
     FALSE,
     CAST_HTTP_RANGE_ALL,
     0,
     10},
};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Checks that every unwritten output byte retains its sentinel.
 *
 * \par Requirements:
 *  - 001 First shall be within the fixed output extent.
 *
 * \param[in] first   First unwritten byte
 *
 * \retval    None   Assertions pass or the process fails.
 */
static FORCE_INLINE void CheckGuard(uint32_t first);

/*************************************************************************************************/
/**
 * \brief Checks the expected request decision and selected representation interval.
 *
 * \par Requirements:
 *  - 001 Expected decisions shall come from independent wire vectors.
 *  - 002 Terminal repeated reads shall preserve the result without consuming new data.
 *
 * \param[in] test   Independent expected result
 *
 * \retval    None   Assertions pass or the process fails.
 */
static FORCE_INLINE void CheckRequest(const RequestCase_t* test);

/*************************************************************************************************/
/**
 * \brief Checks fragmented heads, exact pipeline boundaries and reuse after rejection.
 *
 * \par Requirements:
 *  - 001 The first ready request shall stop exactly before the second request.
 *  - 002 Every shorter prefix shall remain incomplete.
 *  - 003 A capacity failure shall latch until reinitialization.
 *
 *
 * \retval None   Assertions pass or the process fails.
 */
static FORCE_INLINE void CheckPipeline(void);

/*************************************************************************************************/
/**
 * \brief Checks independent exact response bytes and every short output capacity.
 *
 * \par Requirements:
 *  - 001 Preflight size shall equal the independent wire extent.
 *  - 002 Every insufficient capacity shall preserve all bytes, including guards.
 *
 * \param[in] response        Typed representation metadata
 * \param[in] expected        Independent expected bytes
 * \param[in] expectedBytes   Exact expected extent
 *
 * \retval    None   Assertions pass or the process fails.
 */
static FORCE_INLINE void CheckResponse(const CastHttp_Response_t* response,
                                       const char_t*              expected,
                                       uint32_t                   expectedBytes);

/*************************************************************************************************/
/**
 * \brief Checks response kinds and invalid-model preservation.
 *
 * \par Requirements:
 *  - 001 Expected wire bytes shall be independent literals.
 *  - 002 Invalid interval arithmetic and field injection shall fail without writes.
 *
 *
 * \retval None   Assertions pass or the process fails.
 */
static FORCE_INLINE void CheckResponses(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  The first byte outside the admitted capacity is also checked.
 */
static FORCE_INLINE void CheckGuard(uint32_t first)
{
    uint32_t index;
    for (index = first; index < POLICY_TEST_BYTES; ++index)
    {
        LOGASSERT(POLICY_TEST_GUARD == (byte_t)g_output[index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Every case starts from the same retained storage; rejected input never becomes a ready request.
 */
static FORCE_INLINE void CheckRequest(const RequestCase_t* test)
{
    CastHttp_Result_t        result;
    Utl_Http_RangeInterval_t interval;
    CastHttp_RangeResult_t   range;
    CastHttp_InitializeRequest(&g_request, g_fields, POLICY_TEST_FIELDS);
    result = CastHttp_ReadRequest(&g_request, test->wire, test->byteCount);
    LOGASSERT(test->result == result);
    LOGASSERT(test->status == g_request.errorStatus);
    LOGASSERT(test->keepAlive == g_request.keepAlive);
    if (CAST_HTTP_READY == result)
    {
        LOGASSERT(test->headOnly == g_request.headOnly);
        LOGASSERT(test->options == g_request.options);
        LOGASSERT(test->subtitles == g_request.subtitles);
        interval.offset    = POLICY_TEST_SENTINEL;
        interval.byteCount = POLICY_TEST_SENTINEL;
        range              = CastHttp_GetRange(&g_request, POLICY_TEST_REPRESENTATION_BYTES, &interval);
        LOGASSERT(test->range == range);
        LOGASSERT(test->offset == interval.offset);
        LOGASSERT(test->count == interval.byteCount);
    }
    LOGASSERT(result == CastHttp_ReadRequest(&g_request, NULL, 0));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The second request is known independent input, not generated by the implementation.
 */
static FORCE_INLINE void CheckPipeline(void)
{
    uint32_t          count;
    uint32_t          index;
    CastHttp_Result_t result;
    CRT_Memory_Copy(g_input, c_first, sizeof(c_first) - 1);
    CRT_Memory_Copy(g_input + sizeof(c_first) - 1, c_second, sizeof(c_second) - 1);
    count = sizeof(c_first) + sizeof(c_second) - 2;
    CastHttp_InitializeRequest(&g_request, g_fields, POLICY_TEST_FIELDS);
    for (index = 0; index < sizeof(c_first) - 1; ++index)
    {
        result = CastHttp_ReadRequest(&g_request, g_input, index);
        LOGASSERT(CAST_HTTP_INCOMPLETE == result);
        LOGASSERT(0 == g_request.reader.head.byteCount);
    }
    result = CastHttp_ReadRequest(&g_request, g_input, count);
    LOGASSERT(CAST_HTTP_READY == result);
    LOGASSERT((sizeof(c_first) - 1) == g_request.reader.head.byteCount);
    count -= g_request.reader.head.byteCount;
    CRT_Memory_Move(g_input, g_input + g_request.reader.head.byteCount, count);
    CastHttp_InitializeRequest(&g_request, g_fields, POLICY_TEST_FIELDS);
    result = CastHttp_ReadRequest(&g_request, g_input, count);
    LOGASSERT(CAST_HTTP_READY == result);
    LOGASSERT(g_request.headOnly);
    LOGASSERT(g_request.subtitles);
    LOGASSERT(!g_request.keepAlive);
    CastHttp_InitializeRequest(&g_request, NULL, 0);
    result = CastHttp_ReadRequest(&g_request, c_first, sizeof(c_first) - 1);
    LOGASSERT(CAST_HTTP_REJECTED == result);
    LOGASSERT(UTL_HTTP_STATUS_HEADERS_TOO_LARGE == g_request.errorStatus);
    LOGASSERT(CAST_HTTP_REJECTED == CastHttp_ReadRequest(&g_request, c_second, sizeof(c_second) - 1));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Full-byte comparisons also prove CRLF termination and omission of forbidden Content-Length.
 */
static FORCE_INLINE void CheckResponse(const CastHttp_Response_t* response,
                                       const char_t*              expected,
                                       uint32_t                   expectedBytes)
{
    uint32_t count;
    uint32_t capacity;
    int32_t  compared;
    count = CastHttp_GetResponseByteCount(response);
    LOGASSERT(expectedBytes == count);
    LOGASSERT(count < POLICY_TEST_BYTES);
    for (capacity = 0; capacity < count; ++capacity)
    {
        CRT_Memory_Set(g_output, POLICY_TEST_GUARD, sizeof(g_output));
        LOGASSERT(0 == CastHttp_WriteResponse(response, g_output, capacity));
        CheckGuard(0);
    }
    CRT_Memory_Set(g_output, POLICY_TEST_GUARD, sizeof(g_output));
    LOGASSERT(count == CastHttp_WriteResponse(response, g_output, count));
    compared = CRT_Memory_Compare(g_output, expected, count);
    LOGASSERT(0 == compared);
    CheckGuard(count);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All response kinds use the same public serializer as the native server.
 */
static FORCE_INLINE void CheckResponses(void)
{
    CastHttp_Response_t response;
    CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_OK);
    response.contentType.data      = "video/mp4";
    response.contentType.byteCount = sizeof("video/mp4") - 1;
    response.representationBytes   = POLICY_TEST_REPRESENTATION_BYTES;
    response.interval.offset       = 0;
    response.interval.byteCount    = POLICY_TEST_REPRESENTATION_BYTES;
    response.keepAlive             = TRUE;
    response.growing               = FALSE;
    CheckResponse(
        &response,
        "HTTP/1.1 200 \r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\nContent-Length: 10\r\nConnection: "
        "keep-alive\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, HEAD, "
        "OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
        "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n",
        sizeof("HTTP/1.1 200 \r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\nContent-Length: 10\r\nConnection: "
               "keep-alive\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, HEAD, "
               "OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
               "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n") -
            1);
    CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_PARTIAL_CONTENT);
    response.contentType.data      = "video/mp4";
    response.contentType.byteCount = sizeof("video/mp4") - 1;
    response.representationBytes   = POLICY_TEST_REPRESENTATION_BYTES;
    response.interval.offset       = 2;
    response.interval.byteCount    = 3;
    response.keepAlive             = TRUE;
    response.growing               = FALSE;
    CheckResponse(
        &response,
        "HTTP/1.1 206 \r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\nContent-Length: 3\r\nContent-Range: bytes "
        "2-4/10\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, "
        "HEAD, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
        "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n",
        sizeof(
            "HTTP/1.1 206 \r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\nContent-Length: 3\r\nContent-Range: "
            "bytes 2-4/10\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: "
            "GET, HEAD, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
            "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n") -
            1);
    response.rangeCompleteLengthKnown = FALSE;
    CheckResponse(
        &response,
        "HTTP/1.1 206 \r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\nContent-Length: 3\r\nContent-Range: bytes "
        "2-4/*\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, "
        "HEAD, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
        "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n",
        sizeof(
            "HTTP/1.1 206 \r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\nContent-Length: 3\r\nContent-Range: "
            "bytes 2-4/*\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: "
            "GET, HEAD, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
            "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n") -
            1);
    CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_RANGE_NOT_SATISFIABLE);
    response.contentType.data      = "video/mp4";
    response.contentType.byteCount = sizeof("video/mp4") - 1;
    response.representationBytes   = POLICY_TEST_REPRESENTATION_BYTES;
    response.interval.offset       = 0;
    response.interval.byteCount    = 0;
    response.keepAlive             = TRUE;
    response.growing               = FALSE;
    CheckResponse(
        &response,
        "HTTP/1.1 416 \r\nContent-Length: 0\r\nContent-Range: bytes */10\r\nConnection: "
        "keep-alive\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, HEAD, "
        "OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
        "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n",
        sizeof("HTTP/1.1 416 \r\nContent-Length: 0\r\nContent-Range: bytes */10\r\nConnection: "
               "keep-alive\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, HEAD, "
               "OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
               "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n") -
            1);
    CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_NO_CONTENT);
    response.contentType.data      = "video/mp4";
    response.contentType.byteCount = sizeof("video/mp4") - 1;
    response.representationBytes   = POLICY_TEST_REPRESENTATION_BYTES;
    response.interval.offset       = 0;
    response.interval.byteCount    = 0;
    response.keepAlive             = TRUE;
    response.growing               = FALSE;
    CheckResponse(
        &response,
        "HTTP/1.1 204 \r\nAllow: GET, HEAD, OPTIONS\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: "
        "*\r\nAccess-Control-Allow-Methods: GET, HEAD, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, "
        "Accept-Encoding, Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n",
        sizeof("HTTP/1.1 204 \r\nAllow: GET, HEAD, OPTIONS\r\nConnection: keep-alive\r\nAccess-Control-Allow-Origin: "
               "*\r\nAccess-Control-Allow-Methods: GET, HEAD, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, "
               "Accept-Encoding, Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, "
               "Accept-Ranges\r\n\r\n") -
            1);
    CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_OK);
    response.contentType.data      = "video/mp4";
    response.contentType.byteCount = sizeof("video/mp4") - 1;
    response.representationBytes   = POLICY_TEST_REPRESENTATION_BYTES;
    response.interval.offset       = 0;
    response.interval.byteCount    = 0;
    response.keepAlive             = TRUE;
    response.growing               = TRUE;
    CheckResponse(
        &response,
        "HTTP/1.1 200 \r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\nConnection: "
        "close\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, HEAD, "
        "OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
        "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n",
        sizeof("HTTP/1.1 200 \r\nContent-Type: video/mp4\r\nAccept-Ranges: bytes\r\nConnection: "
               "close\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, HEAD, "
               "OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
               "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n") -
            1);
    CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_METHOD_NOT_ALLOWED);
    response.contentType.data      = "video/mp4";
    response.contentType.byteCount = sizeof("video/mp4") - 1;
    response.representationBytes   = POLICY_TEST_REPRESENTATION_BYTES;
    response.interval.offset       = 0;
    response.interval.byteCount    = 0;
    response.keepAlive             = FALSE;
    response.growing               = FALSE;
    CheckResponse(
        &response,
        "HTTP/1.1 405 \r\nContent-Length: 0\r\nAllow: GET, HEAD, OPTIONS\r\nConnection: "
        "close\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, HEAD, "
        "OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
        "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n",
        sizeof("HTTP/1.1 405 \r\nContent-Length: 0\r\nAllow: GET, HEAD, OPTIONS\r\nConnection: "
               "close\r\nAccess-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, HEAD, "
               "OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, Accept-Encoding, "
               "Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n") -
            1);
    CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_BAD_REQUEST);
    response.contentType.data      = "video/mp4";
    response.contentType.byteCount = sizeof("video/mp4") - 1;
    response.representationBytes   = POLICY_TEST_REPRESENTATION_BYTES;
    response.interval.offset       = 0;
    response.interval.byteCount    = 0;
    response.keepAlive             = FALSE;
    response.growing               = FALSE;
    CheckResponse(
        &response,
        "HTTP/1.1 400 \r\nContent-Length: 0\r\nConnection: close\r\nAccess-Control-Allow-Origin: "
        "*\r\nAccess-Control-Allow-Methods: GET, HEAD, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, "
        "Accept-Encoding, Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n\r\n",
        sizeof("HTTP/1.1 400 \r\nContent-Length: 0\r\nConnection: close\r\nAccess-Control-Allow-Origin: "
               "*\r\nAccess-Control-Allow-Methods: GET, HEAD, OPTIONS\r\nAccess-Control-Allow-Headers: Content-Type, "
               "Accept-Encoding, Range\r\nAccess-Control-Expose-Headers: Content-Length, Content-Range, "
               "Accept-Ranges\r\n\r\n") -
            1);
    CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_PARTIAL_CONTENT);
    response.contentType.data      = "video/mp4";
    response.contentType.byteCount = sizeof("video/mp4") - 1;
    response.representationBytes   = UINT64_MAX;
    response.interval.offset       = UINT64_MAX;
    response.interval.byteCount    = 1;
    CRT_Memory_Set(g_output, POLICY_TEST_GUARD, sizeof(g_output));
    LOGASSERT(0 == CastHttp_GetResponseByteCount(&response));
    LOGASSERT(0 == CastHttp_WriteResponse(&response, g_output, POLICY_TEST_BYTES));
    CheckGuard(0);
    response.interval.offset       = 0;
    response.contentType.data      = "video/mp4\r\nX: bad";
    response.contentType.byteCount = sizeof("video/mp4\r\nX: bad") - 1;
    LOGASSERT(0 == CastHttp_WriteResponse(&response, g_output, POLICY_TEST_BYTES));
    CheckGuard(0);
    CastHttp_InitializeResponse(&response, UTL_HTTP_STATUS_RANGE_NOT_SATISFIABLE);
    response.rangeCompleteLengthKnown = FALSE;
    LOGASSERT(0 == CastHttp_GetResponseByteCount(&response));
    LOGASSERT(0 == CastHttp_WriteResponse(&response, g_output, POLICY_TEST_BYTES));
    CheckGuard(0);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  All fixtures use fixed storage and the no-CRT application entry.
 */
uint32_t OS_Application_Main(void)
{
    uint32_t index;
    OS_Process_SetInteractive(FALSE);
    for (index = 0; index < sizeof(c_requests) / sizeof(RequestCase_t); ++index)
    {
        CheckRequest(&c_requests[index]);
    }
    for (index = 0; index < POLICY_TEST_CYCLES; ++index)
    {
        CheckPipeline();
    }
    CheckResponses();
    return 0;
}
