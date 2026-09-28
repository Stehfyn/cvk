/*=================================================================================================
** 1. References
**===============================================================================================*/

/* Independent RFC 9110/9112 wire vectors and malformed framing cases. */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_http.h"
#include <windows.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief A complete request/response head and its independently expected framing. */
typedef struct FramingVector_s
{
    const char_t*       text;
    uint32_t            byteCount;
    Utl_Http_Kind_t     kind;
    const char_t*       method;
    uint32_t            methodBytes;
    bool_t              accepted;
    Utl_Http_BodyKind_t body;
    uint64_t            length;
    bool_t              keepAlive;
    bool_t              otherCoding;
} FramingVector_t;

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static const FramingVector_t c_framing[] = {
    {"GET / HTTP/1.1\r\nHost: x\r\n\r\n", 27, UTL_HTTP_REQUEST, "", 0, TRUE, UTL_HTTP_BODY_NONE, 0, TRUE, FALSE},
    {"GET / HTTP/1.1\r\n\r\n", 18, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nHost: y\r\n\r\n", 36, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 005\r\n\r\n", 67, UTL_HTTP_REQUEST, "", 0, TRUE, UTL_HTTP_BODY_LENGTH, 5, TRUE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 5, 005\r\n\r\n", 51, UTL_HTTP_REQUEST, "", 0, TRUE, UTL_HTTP_BODY_LENGTH, 5, TRUE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n", 65, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 5junk\r\n\r\n", 50, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nContent-Length: 18446744073709551616\r\n\r\n", 65, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n", 55, UTL_HTTP_REQUEST, "", 0, TRUE, UTL_HTTP_BODY_CHUNKED, 0, TRUE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: custom;p=\"a,b\", chunked\r\n\r\n", 71, UTL_HTTP_REQUEST, "", 0, TRUE, UTL_HTTP_BODY_CHUNKED, 0, TRUE, TRUE},
    {"GET / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: gzip\r\nTransfer-Encoding: Chunked\r\n\r\n", 80, UTL_HTTP_REQUEST, "", 0, TRUE, UTL_HTTP_BODY_CHUNKED, 0, TRUE, TRUE},
    {"GET / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked, gzip\r\n\r\n", 61, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked, chunked\r\n\r\n", 64, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked;p=x\r\n\r\n", 59, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: gzip\r\n\r\n", 52, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nContent-Length: 0\r\n\r\n", 74, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.0\r\nTransfer-Encoding: chunked\r\n\r\n", 46, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nConnection: Upgrade, CLOSE\r\n\r\n", 55, UTL_HTTP_REQUEST, "", 0, TRUE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/1.1\r\nHost: x\r\nConnection: disclosure\r\n\r\n", 51, UTL_HTTP_REQUEST, "", 0, TRUE, UTL_HTTP_BODY_NONE, 0, TRUE, FALSE},
    {"GET / HTTP/1.0\r\nConnection: Keep-Alive\r\n\r\n", 42, UTL_HTTP_REQUEST, "", 0, TRUE, UTL_HTTP_BODY_NONE, 0, TRUE, FALSE},
    {"GET / HTTP/1.0\r\nConnection: keep-alive\r\nConnection: close\r\n\r\n", 61, UTL_HTTP_REQUEST, "", 0, TRUE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"GET / HTTP/2.0\r\nHost: x\r\n\r\n", 27, UTL_HTTP_REQUEST, "", 0, FALSE, UTL_HTTP_BODY_NONE, 0, FALSE, FALSE},
    {"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n", 39, UTL_HTTP_RESPONSE, "GET", 3, TRUE, UTL_HTTP_BODY_LENGTH, 10, TRUE, FALSE},
    {"HTTP/1.1 200 OK\r\n\r\n", 19, UTL_HTTP_RESPONSE, "GET", 3, TRUE, UTL_HTTP_BODY_CLOSE, 0, FALSE, FALSE},
    {"HTTP/1.0 200 OK\r\nConnection: keep-alive\r\n\r\n", 43, UTL_HTTP_RESPONSE, "GET", 3, TRUE, UTL_HTTP_BODY_CLOSE, 0, FALSE, FALSE},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip\r\n\r\n", 44, UTL_HTTP_RESPONSE, "GET", 3, TRUE, UTL_HTTP_BODY_CLOSE, 0, FALSE, TRUE},
    {"HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip,chunked\r\n\r\n", 52, UTL_HTTP_RESPONSE, "GET", 3, TRUE, UTL_HTTP_BODY_CHUNKED, 0, TRUE, TRUE},
    {"HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\n", 39, UTL_HTTP_RESPONSE, "HEAD", 4, TRUE, UTL_HTTP_BODY_NONE, 0, TRUE, FALSE},
    {"HTTP/1.1 204 No Content\r\nContent-Length: invalid\r\n\r\n", 52, UTL_HTTP_RESPONSE, "GET", 3, TRUE, UTL_HTTP_BODY_NONE, 0, TRUE, FALSE},
    {"HTTP/1.1 304 Not Modified\r\nTransfer-Encoding: gzip\r\n\r\n", 54, UTL_HTTP_RESPONSE, "GET", 3, TRUE, UTL_HTTP_BODY_NONE, 0, TRUE, FALSE},
    {"HTTP/1.1 100 Continue\r\n\r\n", 25, UTL_HTTP_RESPONSE, "POST", 4, TRUE, UTL_HTTP_BODY_NONE, 0, TRUE, FALSE},
    {"HTTP/1.1 200 Connection Established\r\nContent-Length: invalid\r\nTransfer-Encoding: invalid\r\n\r\n", 92, UTL_HTTP_RESPONSE, "CONNECT", 7, TRUE, UTL_HTTP_BODY_TUNNEL, 0, FALSE, FALSE},
    {"HTTP/1.1 407 Proxy Authentication Required\r\nContent-Length: 0\r\n\r\n", 65, UTL_HTTP_RESPONSE, "CONNECT", 7, TRUE, UTL_HTTP_BODY_LENGTH, 0, TRUE, FALSE}};
static const char_t   c_badHeads[][96]     = {"GET / HTTP/1.1\n\n",
                                              "GET / HTTP/1.1\rX",
                                              "GET  / HTTP/1.1\r\n\r\n",
                                              "GET / HTTP/1.1 extra\r\n\r\n",
                                              "GET /#fragment HTTP/1.1\r\n\r\n",
                                              "GE(T / HTTP/1.1\r\n\r\n",
                                              "GET / HTTP/1.1\r\nHost : x\r\n\r\n",
                                              "GET / HTTP/1.1\r\n folded\r\n\r\n",
                                              "GET / HTTP/1.1\r\n: x\r\n\r\n",
                                              "GET / HTTP/1.1\r\nX: a\000b\r\n\r\n",
                                              "GET / HTTP/1.1\r\nX: a\177b\r\n\r\n"};
static const uint32_t c_badHeadBytes[]     = {16, 16, 19, 24, 27, 19, 28, 27, 23, 26, 26};
static const char_t   c_badResponses[][40] = {"HTTP/1.1 099 Low\r\n\r\n",
                                              "HTTP/1.1 600 High\r\n\r\n",
                                              "HTTP/1.1 2x0 Bad\r\n\r\n",
                                              "HTTP/1.1 20 Short\r\n\r\n",
                                              "HTTP/1.1 200XBad\r\n\r\n",
                                              "HTTP/1.1\t200 Bad\r\n\r\n",
                                              "HTTP/1.1 200 Bad\177\r\n\r\n"};
static const uint32_t c_badResponseBytes[] = {20, 21, 20, 21, 20, 20, 21};
static const char_t   c_input[]            = "\r\nGET /media.mp4 HTTP/1.1\r\nHost:\tx \t\r\nRange: bytes=0-9\r\nConnection: close\r\n\r\nTAIL";
static const char_t   c_canonical[]        = "GET /media.mp4 HTTP/1.1\r\nHost: x\r\nRange: bytes=0-9\r\nConnection: close\r\n\r\n";
static const char_t   c_success[]          = "HTTP head, framing and capacity checks passed (CRT-free).\r\n";
static const char_t   c_failure[]          = "HTTP check failed; exit code is the source line.\r\n";

/** Independent calendar and protocol date vectors, including leap-century boundaries. */
static const Utl_Http_Date_t c_dates[] =
    {{1994, 11, 6, 8, 49, 37}, {2000, 2, 29, 0, 0, 0}, {1900, 3, 1, 23, 59, 59}, {1970, 1, 1, 0, 0, 0}, {2026, 9, 27, 12, 34, 56}, {9999, 12, 31, 23, 59, 59}, {2016, 12, 31, 23, 59, 60}};
static const char_t c_dateText[][UTL_HTTP_DATE_BYTE_COUNT + 1] = {"Sun, 06 Nov 1994 08:49:37 GMT",
                                                                  "Tue, 29 Feb 2000 00:00:00 GMT",
                                                                  "Thu, 01 Mar 1900 23:59:59 GMT",
                                                                  "Thu, 01 Jan 1970 00:00:00 GMT",
                                                                  "Sun, 27 Sep 2026 12:34:56 GMT",
                                                                  "Fri, 31 Dec 9999 23:59:59 GMT",
                                                                  "Sat, 31 Dec 2016 23:59:60 GMT"};

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Reports a failed vector through the process exit code.
 *
 * \param[in] passed   Expected condition
 * \param[in] line     Source line
 *
 * \retval    None
 */
static void Check(bool_t passed, uint32_t line);

/*************************************************************************************************/
/**
 * \brief Checks counted heads, prefix truncation, field lookup and output preservation.
 *
 *
 * \retval None
 */
static void CheckHeads(void);

/*************************************************************************************************/
/**
 * \brief Checks independent framing and connection vectors.
 *
 *
 * \retval None
 */
static void CheckFraming(void);

/*************************************************************************************************/
/**
 * \brief Checks Gregorian dates, fixed-width output and capacity failure.
 *
 * \retval None
 */
static void CheckDates(void);

/*************************************************************************************************/
/**
 * \brief Checks incremental scan progress and terminal-state preservation.
 *
 * \retval None
 */
static void CheckIncremental(void);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Only Kernel32 output and termination are used.
 */
static void Check(bool_t passed, uint32_t line)
{
    DWORD written;

    if (!passed)
    {
        (void)WriteFile(GetStdHandle(STD_ERROR_HANDLE), c_failure, sizeof(c_failure) - 1, &written, NULL);
        ExitProcess(line);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The expected canonical request is an independent wire vector, including its exact separators.
 */
static void CheckHeads(void)
{
    Utl_Http_Head_t   head;
    Utl_Http_Field_t  fields[8];
    uint32_t          prefix;
    Utl_Http_Result_t result;
    Utl_Http_Text_t   name;
    uint32_t          index;
    char_t            output[128];
    uint32_t          count;

    head.fields        = fields;
    head.fieldCapacity = 8;
    for (prefix = 0; prefix < (sizeof(c_input) - 5); ++prefix)
    {
        result = Utl_Http_ReadHead(&head, UTL_HTTP_REQUEST, c_input, prefix);
        Check(UTL_HTTP_INCOMPLETE == result, __LINE__);
        Check((0 == head.fieldCount) && (0 == head.byteCount), __LINE__);
    }
    result = Utl_Http_ReadHead(&head, UTL_HTTP_REQUEST, c_input, sizeof(c_input) - 1);
    Check(UTL_HTTP_COMPLETE == result, __LINE__);
    Check((3 == head.fieldCount) && ((sizeof(c_input) - 5) == head.byteCount), __LINE__);
    Check((1 == head.major) && (1 == head.minor), __LINE__);
    Check((3 == head.method.byteCount) && (10 == head.target.byteCount), __LINE__);
    name.data      = "rAnGe";
    name.byteCount = 5;
    Check(1 == Utl_Http_FindField(&head, name, 0), __LINE__);
    Check(UTL_HTTP_NO_FIELD == Utl_Http_FindField(&head, name, 2), __LINE__);
    Check((sizeof(c_canonical) - 1) == Utl_Http_GetHeadByteCount(&head), __LINE__);
    for (index = 0; index < sizeof(output); ++index)
    {
        output[index] = (char_t)('a' + (index % 26));
    }
    for (prefix = 0; prefix < (sizeof(c_canonical) - 1); ++prefix)
    {
        Check(0 == Utl_Http_WriteHead(&head, output, prefix), __LINE__);
        for (index = 0; index < sizeof(output); ++index)
        {
            Check((char_t)('a' + (index % 26)) == output[index], __LINE__);
        }
    }
    count = Utl_Http_WriteHead(&head, output, sizeof(c_canonical) - 1);
    Check((sizeof(c_canonical) - 1) == count, __LINE__);
    for (index = 0; index < count; ++index)
    {
        Check(c_canonical[index] == output[index], __LINE__);
    }
    Check((char_t)('a' + (count % 26)) == output[count], __LINE__);
    head.fieldCapacity = 2;
    Check(UTL_HTTP_CAPACITY == Utl_Http_ReadHead(&head, UTL_HTTP_REQUEST, c_input, sizeof(c_input) - 1), __LINE__);
    Check((0 == head.fieldCount) && (0 == head.byteCount), __LINE__);
    head.fieldCapacity = 8;
    for (index = 0; index < (sizeof(c_badHeads) / sizeof(c_badHeads[0])); ++index)
    {
        Check(UTL_HTTP_INVALID == Utl_Http_ReadHead(&head, UTL_HTTP_REQUEST, c_badHeads[index], c_badHeadBytes[index]), __LINE__);
        Check((0 == head.fieldCount) && (0 == head.byteCount), __LINE__);
    }
    for (index = 0; index < (sizeof(c_badResponses) / sizeof(c_badResponses[0])); ++index)
    {
        Check(UTL_HTTP_INVALID == Utl_Http_ReadHead(&head, UTL_HTTP_RESPONSE, c_badResponses[index], c_badResponseBytes[index]), __LINE__);
        Check((0 == head.fieldCount) && (0 == head.byteCount), __LINE__);
    }
    Check(UTL_HTTP_INCOMPLETE == Utl_Http_ReadHead(&head, UTL_HTTP_REQUEST, NULL, 0), __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Rejected derivations must preserve every framing output field; response writers are read back.
 */
static void CheckFraming(void)
{
    uint32_t               index;
    const FramingVector_t* vector;
    Utl_Http_Head_t        head;
    Utl_Http_Field_t       fields[8];
    Utl_Http_Text_t        method;
    Utl_Http_Framing_t     framing;
    bool_t                 accepted;
    uint32_t               count;
    char_t                 output[256];

    for (index = 0; index < (sizeof(c_framing) / sizeof(c_framing[0])); ++index)
    {
        vector             = &c_framing[index];
        head.fields        = fields;
        head.fieldCapacity = 8;
        Check(UTL_HTTP_COMPLETE == Utl_Http_ReadHead(&head, vector->kind, vector->text, vector->byteCount), __LINE__);
        method.data                    = vector->method;
        method.byteCount               = vector->methodBytes;
        framing.kind                   = UTL_HTTP_BODY_TUNNEL;
        framing.contentLength          = 73;
        framing.keepAlive              = TRUE;
        framing.hasOtherTransferCoding = TRUE;
        accepted                       = Utl_Http_DeriveFraming(&framing, &head, method);
        Check(vector->accepted == accepted, __LINE__);
        if (accepted)
        {
            Check(vector->body == framing.kind, __LINE__);
            Check(vector->length == framing.contentLength, __LINE__);
            Check(vector->keepAlive == framing.keepAlive, __LINE__);
            Check(vector->otherCoding == framing.hasOtherTransferCoding, __LINE__);
        }
        else
        {
            Check(UTL_HTTP_BODY_TUNNEL == framing.kind, __LINE__);
            Check((73 == framing.contentLength) && framing.keepAlive && framing.hasOtherTransferCoding, __LINE__);
        }
        count = Utl_Http_WriteHead(&head, output, sizeof(output));
        Check(0 < count, __LINE__);
        Check(UTL_HTTP_COMPLETE == Utl_Http_ReadHead(&head, vector->kind, output, count), __LINE__);
        Check(count == head.byteCount, __LINE__);
        Check(accepted == Utl_Http_DeriveFraming(&framing, &head, method), __LINE__);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Expected strings are independent IMF-fixdate vectors; every short output capacity preserves its sentinel.
 */
static void CheckDates(void)
{
    uint32_t        index;
    uint32_t        byte_index;
    char_t          output[UTL_HTTP_DATE_BYTE_COUNT + 1];
    uint32_t        capacity;
    Utl_Http_Date_t invalid;

    for (index = 0; index < (sizeof(c_dates) / sizeof(c_dates[0])); ++index)
    {
        for (byte_index = 0; byte_index < sizeof(output); ++byte_index)
        {
            output[byte_index] = '?';
        }
        Check(UTL_HTTP_DATE_BYTE_COUNT == Utl_Http_GetDateByteCount(&c_dates[index]), __LINE__);
        for (capacity = 0; capacity < UTL_HTTP_DATE_BYTE_COUNT; ++capacity)
        {
            Check(0 == Utl_Http_WriteDate(&c_dates[index], output, capacity), __LINE__);
            for (byte_index = 0; byte_index < sizeof(output); ++byte_index)
            {
                Check('?' == output[byte_index], __LINE__);
            }
        }
        Check(UTL_HTTP_DATE_BYTE_COUNT == Utl_Http_WriteDate(&c_dates[index], output, UTL_HTTP_DATE_BYTE_COUNT), __LINE__);
        for (byte_index = 0; byte_index < UTL_HTTP_DATE_BYTE_COUNT; ++byte_index)
        {
            Check(c_dateText[index][byte_index] == output[byte_index], __LINE__);
        }
        Check('?' == output[UTL_HTTP_DATE_BYTE_COUNT], __LINE__);
    }
    invalid      = c_dates[1];
    invalid.year = 1900;
    Check(0 == Utl_Http_GetDateByteCount(&invalid), __LINE__);
    invalid       = c_dates[0];
    invalid.month = 0;
    Check(0 == Utl_Http_GetDateByteCount(&invalid), __LINE__);
    invalid.month = 13;
    Check(0 == Utl_Http_GetDateByteCount(&invalid), __LINE__);
    invalid     = c_dates[0];
    invalid.day = 0;
    Check(0 == Utl_Http_GetDateByteCount(&invalid), __LINE__);
    invalid.month = 4;
    invalid.day   = 31;
    Check(0 == Utl_Http_GetDateByteCount(&invalid), __LINE__);
    invalid      = c_dates[0];
    invalid.year = 10000;
    Check(0 == Utl_Http_GetDateByteCount(&invalid), __LINE__);
    invalid      = c_dates[0];
    invalid.hour = 24;
    Check(0 == Utl_Http_GetDateByteCount(&invalid), __LINE__);
    invalid        = c_dates[0];
    invalid.minute = 60;
    Check(0 == Utl_Http_GetDateByteCount(&invalid), __LINE__);
    invalid        = c_dates[0];
    invalid.second = 61;
    Check(0 == Utl_Http_GetDateByteCount(&invalid), __LINE__);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Append-only single-octet feeds prove scan progress, boundary retention and terminal idempotence.
 */
static void CheckIncremental(void)
{
    uint32_t              boundary;
    Utl_Http_HeadReader_t reader;
    Utl_Http_Field_t      fields[8];
    uint32_t              prefix;
    Utl_Http_Result_t     result;
    uint32_t              index;

    boundary = sizeof(c_input) - 5;
    Utl_Http_InitializeHeadReader(&reader, UTL_HTTP_REQUEST, fields, 8);
    for (prefix = 0; prefix < boundary; ++prefix)
    {
        result = Utl_Http_ReadHeadIncremental(&reader, c_input, prefix);
        Check(UTL_HTTP_INCOMPLETE == result, __LINE__);
        Check(prefix == reader.scan, __LINE__);
        Check(0 == reader.head.byteCount, __LINE__);
    }
    Check(UTL_HTTP_COMPLETE == Utl_Http_ReadHeadIncremental(&reader, c_input, sizeof(c_input) - 1), __LINE__);
    Check((boundary == reader.scan) && (boundary == reader.head.byteCount), __LINE__);
    Check(3 == reader.head.fieldCount, __LINE__);
    Check(UTL_HTTP_COMPLETE == Utl_Http_ReadHeadIncremental(&reader, NULL, 0), __LINE__);
    Check((boundary == reader.scan) && (3 == reader.head.fieldCount), __LINE__);
    for (index = 0; index < (sizeof(c_badHeads) / sizeof(c_badHeads[0])); ++index)
    {
        Utl_Http_InitializeHeadReader(&reader, UTL_HTTP_REQUEST, fields, 8);
        result = UTL_HTTP_INCOMPLETE;
        for (prefix = 0; prefix <= c_badHeadBytes[index]; ++prefix)
        {
            result = Utl_Http_ReadHeadIncremental(&reader, c_badHeads[index], prefix);
            if (UTL_HTTP_INCOMPLETE != result)
            {
                break;
            }
        }
        Check(UTL_HTTP_INVALID == result, __LINE__);
        Check(UTL_HTTP_INVALID == Utl_Http_ReadHeadIncremental(&reader, c_input, sizeof(c_input) - 1), __LINE__);
        Check((0 == reader.head.fieldCount) && (0 == reader.head.byteCount), __LINE__);
    }
    Utl_Http_InitializeHeadReader(&reader, UTL_HTTP_REQUEST, fields, 2);
    Check(UTL_HTTP_CAPACITY == Utl_Http_ReadHeadIncremental(&reader, c_input, sizeof(c_input) - 1), __LINE__);
    Check((0 == reader.head.fieldCount) && (0 == reader.head.byteCount), __LINE__);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Runs HTTP checks without CRT startup or runtime dependencies.
 */
void WinMainStartup(void)
{
    DWORD written;

    CheckHeads();
    CheckFraming();
    CheckDates();
    CheckIncremental();
    (void)WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), c_success, sizeof(c_success) - 1, &written, NULL);
    ExitProcess(0);
}
