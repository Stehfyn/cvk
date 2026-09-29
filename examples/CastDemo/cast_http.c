/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * CastDemo read-only HTTP serving policy over the counted HTTP syntax module.
 * RFC 9112 sections 2, 6.3 and 9.3: framing and persistent message boundaries.
 * https://www.rfc-editor.org/rfc/rfc9112.html
 * RFC 9110 sections 9.3.1, 9.3.2, 9.3.7, 13.1.5 and 14.2: methods and ranges.
 * https://www.rfc-editor.org/rfc/rfc9110.html
 * Requests with content are rejected and closed by this read-only application.
 * General target/Host URI semantics, validators and full CORS policy remain separate work.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "cast_http.h"
#include "utl/utl_http.h"
#include "utl/utl_decimal.h"
#include "os/os_types.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define HTTP_SUBTITLE_TARGET ("/subtitles.vtt")

#define HTTP_RESPONSE_FIELDS     (10)
#define HTTP_DECIMAL_BYTES       (20)
#define HTTP_CONTENT_RANGE_BYTES (80)
#define HTTP_ALLOW_METHODS       ("GET, HEAD, OPTIONS")
#define HTTP_CORS_ORIGIN_NAME    ("Access-Control-Allow-Origin")
#define HTTP_CORS_METHODS_NAME   ("Access-Control-Allow-Methods")
#define HTTP_CORS_HEADERS_NAME   ("Access-Control-Allow-Headers")
#define HTTP_CORS_EXPOSE_NAME    ("Access-Control-Expose-Headers")
#define HTTP_CORS_ORIGIN         ("*")
#define HTTP_CORS_HEADERS        ("Content-Type, Accept-Encoding, Range")
#define HTTP_CORS_EXPOSE         ("Content-Length, Content-Range, Accept-Ranges")

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Bounded ephemeral header workspace; spans remain valid through serialization. */
typedef struct ResponseHead_s
{
    Utl_Http_Head_t  head;                            ///< Complete HTTP model.
    Utl_Http_Field_t fields[HTTP_RESPONSE_FIELDS];    ///< Bounded response fields.
    byte_t           length[HTTP_DECIMAL_BYTES];      ///< Exact decimal Content-Length value.
    byte_t           range[HTTP_CONTENT_RANGE_BYTES]; ///< Exact serialized Content-Range value.
} ResponseHead_t;

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
 * \brief Constructs one borrowed counted HTTP token.
 *
 * \par Requirements:
 *  - 001 Storage shall retain the complete nonempty span.
 *
 * \param[in] data        Stable literal bytes
 * \param[in] byteCount   Exact readable extent
 *
 * \retval    text   Borrowed token.
 */
static FORCE_INLINE Utl_Http_Text_t GetText(const char_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Classifies statuses emitted by this serving profile.
 *
 * \par Requirements:
 *  - 001 Only implemented status semantics shall be admitted.
 *
 * \param[in] status   Named status code
 *
 * \retval    TRUE    Supported response kind.
 * \retval    FALSE   Unsupported response kind.
 */
static FORCE_INLINE bool_t IsResponseStatus(uint16_t status);

/*************************************************************************************************/
/**
 * \brief Checks application response relationships before building fields.
 *
 * \par Requirements:
 *  - 001 Partial intervals shall be nonempty and bounded by their available snapshot.
 *  - 002 A full fixed response shall cover exactly its representation; growing responses shall use status 200 and unknown length.
 *  - 003 Invalid field bytes shall fail before publication; unsatisfied ranges shall require a known current length.
 *
 * \param[in] response   Required immutable model
 *
 * \retval    TRUE    All relationships hold.
 * \retval    FALSE   The response is inconsistent.
 */
static FORCE_INLINE bool_t IsResponseValid(const CastHttp_Response_t* response);

/*************************************************************************************************/
/**
 * \brief Derives the unique unconditional Range field eligible for GET.
 *
 * \par Requirements:
 *  - 001 Duplicate Range fields shall be ignored as an invalid combined range value.
 *  - 002 Any If-Range shall prevent partial selection because this application supplies no representation validator.
 *
 * \param[in] head   Complete validated head
 *
 * \retval    UTL_HTTP_NO_FIELD   No eligible range field.
 * \retval    index               The unique range field index.
 */
static FORCE_INLINE uint32_t GetRangeField(const Utl_Http_Head_t* head);

/*************************************************************************************************/
/**
 * \brief Appends one prevalidated field to the fixed response model.
 *
 * \par Details:
 *  One field is retained.
 *
 * \par Requirements:
 *  - 001 The implementation shall reserve sufficient fixed slots before each append.
 *
 * \param[in,out] output   Private field owner
 * \param[in]     field    Named stable field spans
 *
 * \retval        None
 */
static FORCE_INLINE void AppendField(ResponseHead_t* output, const Utl_Http_Field_t* field);

/*************************************************************************************************/
/**
 * \brief Constructs counted fields for a validated response.
 *
 * \par Details:
 *  The complete head model is established.
 *
 * \par Requirements:
 *  - 001 Caller shall validate the response before deriving range arithmetic.
 *  - 002 The model and all decimal/range storage shall remain together until serialization completes.
 *  - 003 No response body bytes shall be written.
 *
 * \param[out] output     Private fixed workspace
 * \param[in]  response   Validated metadata
 *
 * \retval     None
 */
static FORCE_INLINE void SetResponseHead(ResponseHead_t* output, const CastHttp_Response_t* response);

/*************************************************************************************************/
/**
 * \brief Derives method/framing policy after complete syntactic parsing.
 *
 * \par Requirements:
 *  - 001 Only HTTP/1.0 and HTTP/1.1 shall be admitted by this application profile.
 *  - 002 Ambiguous framing and any positive-length or transfer-coded content shall close without interpreting later bytes.
 *  - 003 Only GET, HEAD and OPTIONS shall be served; policy fields are consumable only after READY publication.
 *
 * \param[in,out] request   Complete parsed head and unpublished policy
 *
 * \retval        0         The policy fields are ready to publish.
 * \retval        nonzero   Final HTTP error status requiring closure.
 */
static FORCE_INLINE uint16_t ReadPolicy(CastHttp_Request_t* request);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  No terminator scan is performed.
 */
static FORCE_INLINE Utl_Http_Text_t GetText(const char_t* data, uint32_t byteCount)
{
    Utl_Http_Text_t text;
    text.data      = (const byte_t*)data;
    text.byteCount = byteCount;
    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No numeric range implies support for unknown response semantics.
 */
static FORCE_INLINE bool_t IsResponseStatus(uint16_t status)
{
    bool_t supported;
    switch (status)
    {
    case UTL_HTTP_STATUS_OK:
    case UTL_HTTP_STATUS_NO_CONTENT:
    case UTL_HTTP_STATUS_PARTIAL_CONTENT:
    case UTL_HTTP_STATUS_BAD_REQUEST:
    case UTL_HTTP_STATUS_NOT_FOUND:
    case UTL_HTTP_STATUS_METHOD_NOT_ALLOWED:
    case UTL_HTTP_STATUS_RANGE_NOT_SATISFIABLE:
    case UTL_HTTP_STATUS_HEADERS_TOO_LARGE:
    case UTL_HTTP_STATUS_SERVER_ERROR:
    case UTL_HTTP_STATUS_VERSION_NOT_SUPPORTED:
        supported = TRUE;
        break;
    default:
        supported = FALSE;
        break;
    }
    return supported;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Subtraction precedes the interval-length comparison so no end addition can overflow.
 */
static FORCE_INLINE bool_t IsResponseValid(const CastHttp_Response_t* response)
{
    bool_t valid;
    bool_t success;
    valid   = IsResponseStatus(response->status);
    success = (UTL_HTTP_STATUS_OK == response->status) || (UTL_HTTP_STATUS_PARTIAL_CONTENT == response->status);
    if (success)
    {
        valid = valid && (0 < response->contentType.byteCount) && Utl_Http_IsFieldValue(response->contentType);
    }
    if (response->growing)
    {
        valid = valid && (UTL_HTTP_STATUS_OK == response->status);
    }
    else if (UTL_HTTP_STATUS_OK == response->status)
    {
        valid = valid && (0 == response->interval.offset) &&
                (response->representationBytes == response->interval.byteCount);
    }
    else if (UTL_HTTP_STATUS_PARTIAL_CONTENT == response->status)
    {
        valid =
            valid && (0 < response->interval.byteCount) && (response->interval.offset < response->representationBytes);
        if (valid)
        {
            valid = (response->interval.byteCount <= (response->representationBytes - response->interval.offset));
        }
    }
    else if (UTL_HTTP_STATUS_RANGE_NOT_SATISFIABLE == response->status)
    {
        valid = valid && response->rangeCompleteLengthKnown;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Range grammar and representation bounds are checked separately by GetRange.
 */
static FORCE_INLINE uint32_t GetRangeField(const Utl_Http_Head_t* head)
{
    uint32_t        index;
    bool_t          get;
    Utl_Http_Text_t name;
    uint32_t        other;
    index = UTL_HTTP_NO_FIELD;
    get   = Utl_Http_IsTextEqual(head->method, GetText(UTL_HTTP_METHOD_GET, sizeof(UTL_HTTP_METHOD_GET) - 1), FALSE);
    if (get)
    {
        name  = GetText(UTL_HTTP_FIELD_IF_RANGE, sizeof(UTL_HTTP_FIELD_IF_RANGE) - 1);
        other = Utl_Http_FindField(head, name, 0);
        if (UTL_HTTP_NO_FIELD == other)
        {
            name  = GetText(UTL_HTTP_FIELD_RANGE, sizeof(UTL_HTTP_FIELD_RANGE) - 1);
            index = Utl_Http_FindField(head, name, 0);
            if (UTL_HTTP_NO_FIELD != index)
            {
                other = Utl_Http_FindField(head, name, index + 1);
                if (UTL_HTTP_NO_FIELD != other)
                {
                    index = UTL_HTTP_NO_FIELD;
                }
            }
        }
    }
    return index;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All field storage is part of the current bounded workspace.
 */
static FORCE_INLINE void AppendField(ResponseHead_t* output, const Utl_Http_Field_t* field)
{
    LOGASSERT(output->head.fieldCount < HTTP_RESPONSE_FIELDS);
    output->fields[output->head.fieldCount] = *field;
    ++output->head.fieldCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Reason phrases are optional; the empty reason avoids duplicating status semantics.
 */
static FORCE_INLINE void SetResponseHead(ResponseHead_t* output, const CastHttp_Response_t* response)
{
    bool_t                  media;
    Utl_Http_Field_t        field;
    uint64_t                length;
    uint32_t                count;
    Utl_Http_ContentRange_t range;
    bool_t                  keep_alive;
    output->head.fields        = output->fields;
    output->head.fieldCapacity = HTTP_RESPONSE_FIELDS;
    output->head.fieldCount    = 0;
    output->head.kind          = UTL_HTTP_RESPONSE;
    output->head.major         = 1;
    output->head.minor         = 1;
    output->head.method        = GetText(NULL, 0);
    output->head.target        = GetText(NULL, 0);
    output->head.status        = response->status;
    output->head.reason        = GetText(NULL, 0);
    output->head.byteCount     = 0;
    media                      = (UTL_HTTP_STATUS_OK == response->status) || (UTL_HTTP_STATUS_PARTIAL_CONTENT == response->status);
    if (media)
    {
        field.name  = GetText(UTL_HTTP_FIELD_CONTENT_TYPE, sizeof(UTL_HTTP_FIELD_CONTENT_TYPE) - 1);
        field.value = response->contentType;
        AppendField(output, &field);
        field.name  = GetText(UTL_HTTP_FIELD_ACCEPT_RANGES, sizeof(UTL_HTTP_FIELD_ACCEPT_RANGES) - 1);
        field.value = GetText(UTL_HTTP_RANGE_UNIT, sizeof(UTL_HTTP_RANGE_UNIT) - 1);
        AppendField(output, &field);
    }
    if ((!response->growing) && (UTL_HTTP_STATUS_NO_CONTENT != response->status))
    {
        length = 0;
        if (media)
        {
            length = response->interval.byteCount;
        }
        count = Utl_Decimal_WriteUint64(length, output->length, HTTP_DECIMAL_BYTES);
        LOGASSERT(0 < count);
        field.name            = GetText(UTL_HTTP_FIELD_CONTENT_LENGTH, sizeof(UTL_HTTP_FIELD_CONTENT_LENGTH) - 1);
        field.value.data      = output->length;
        field.value.byteCount = count;
        AppendField(output, &field);
    }
    if ((UTL_HTTP_STATUS_PARTIAL_CONTENT == response->status) ||
        (UTL_HTTP_STATUS_RANGE_NOT_SATISFIABLE == response->status))
    {
        range.hasRange          = (UTL_HTTP_STATUS_PARTIAL_CONTENT == response->status);
        range.hasCompleteLength = response->rangeCompleteLengthKnown;
        range.completeLength    = response->representationBytes;
        range.first             = response->interval.offset;
        range.last              = 0;
        if (range.hasRange)
        {
            range.last = range.first + response->interval.byteCount - 1;
        }
        count = Utl_Http_WriteContentRange(&range, output->range, HTTP_CONTENT_RANGE_BYTES);
        LOGASSERT(0 < count);
        field.name            = GetText(UTL_HTTP_FIELD_CONTENT_RANGE, sizeof(UTL_HTTP_FIELD_CONTENT_RANGE) - 1);
        field.value.data      = output->range;
        field.value.byteCount = count;
        AppendField(output, &field);
    }
    if ((UTL_HTTP_STATUS_METHOD_NOT_ALLOWED == response->status) || (UTL_HTTP_STATUS_NO_CONTENT == response->status))
    {
        field.name  = GetText(UTL_HTTP_FIELD_ALLOW, sizeof(UTL_HTTP_FIELD_ALLOW) - 1);
        field.value = GetText(HTTP_ALLOW_METHODS, sizeof(HTTP_ALLOW_METHODS) - 1);
        AppendField(output, &field);
    }
    keep_alive = response->keepAlive && (!response->growing);
    if (keep_alive)
    {
        field.name  = GetText(UTL_HTTP_FIELD_CONNECTION, sizeof(UTL_HTTP_FIELD_CONNECTION) - 1);
        field.value = GetText(UTL_HTTP_CONNECTION_KEEP_ALIVE, sizeof(UTL_HTTP_CONNECTION_KEEP_ALIVE) - 1);
        AppendField(output, &field);
    }
    else
    {
        field.name  = GetText(UTL_HTTP_FIELD_CONNECTION, sizeof(UTL_HTTP_FIELD_CONNECTION) - 1);
        field.value = GetText(UTL_HTTP_CONNECTION_CLOSE, sizeof(UTL_HTTP_CONNECTION_CLOSE) - 1);
        AppendField(output, &field);
    }
    field.name  = GetText(HTTP_CORS_ORIGIN_NAME, sizeof(HTTP_CORS_ORIGIN_NAME) - 1);
    field.value = GetText(HTTP_CORS_ORIGIN, sizeof(HTTP_CORS_ORIGIN) - 1);
    AppendField(output, &field);
    field.name  = GetText(HTTP_CORS_METHODS_NAME, sizeof(HTTP_CORS_METHODS_NAME) - 1);
    field.value = GetText(HTTP_ALLOW_METHODS, sizeof(HTTP_ALLOW_METHODS) - 1);
    AppendField(output, &field);
    field.name  = GetText(HTTP_CORS_HEADERS_NAME, sizeof(HTTP_CORS_HEADERS_NAME) - 1);
    field.value = GetText(HTTP_CORS_HEADERS, sizeof(HTTP_CORS_HEADERS) - 1);
    AppendField(output, &field);
    field.name  = GetText(HTTP_CORS_EXPOSE_NAME, sizeof(HTTP_CORS_EXPOSE_NAME) - 1);
    field.value = GetText(HTTP_CORS_EXPOSE, sizeof(HTTP_CORS_EXPOSE) - 1);
    AppendField(output, &field);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Bodyless framing is required before persistent pipelining can be used.
 */
static FORCE_INLINE uint16_t ReadPolicy(CastHttp_Request_t* request)
{
    const Utl_Http_Head_t* head;
    uint16_t               status;
    bool_t                 version;
    bool_t                 framed;
    Utl_Http_Framing_t     framing;
    bool_t                 get;
    bool_t                 supported;
    bool_t                 empty;
    head    = &request->reader.head;
    status  = 0;
    version = (1 == head->major) && (1 >= head->minor);
    if (!version)
    {
        status = UTL_HTTP_STATUS_VERSION_NOT_SUPPORTED;
    }
    else
    {
        framed = Utl_Http_DeriveFraming(&framing, head, GetText(NULL, 0));
        if (!framed)
        {
            status = UTL_HTTP_STATUS_BAD_REQUEST;
        }
        else
        {
            get = Utl_Http_IsTextEqual(head->method,
                                       GetText(UTL_HTTP_METHOD_GET, sizeof(UTL_HTTP_METHOD_GET) - 1),
                                       FALSE);
            request->headOnly = Utl_Http_IsTextEqual(head->method,
                                                     GetText(UTL_HTTP_METHOD_HEAD, sizeof(UTL_HTTP_METHOD_HEAD) - 1),
                                                     FALSE);
            request->options =
                Utl_Http_IsTextEqual(head->method,
                                     GetText(UTL_HTTP_METHOD_OPTIONS, sizeof(UTL_HTTP_METHOD_OPTIONS) - 1),
                                     FALSE);
            supported = get || request->headOnly || request->options;
            empty     = (UTL_HTTP_BODY_NONE == framing.kind) ||
                        ((UTL_HTTP_BODY_LENGTH == framing.kind) && (0 == framing.contentLength));
            if (!supported)
            {
                status = UTL_HTTP_STATUS_METHOD_NOT_ALLOWED;
            }
            else if (!empty)
            {
                status = UTL_HTTP_STATUS_BAD_REQUEST;
            }
            else
            {
                request->keepAlive = framing.keepAlive;
                request->subtitles =
                    Utl_Http_IsTextEqual(head->target,
                                         GetText(HTTP_SUBTITLE_TARGET, sizeof(HTTP_SUBTITLE_TARGET) - 1),
                                         FALSE);
                request->rangeField = GetRangeField(head);
            }
        }
    }
    return status;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Binding storage does not clear unused field records or inspect source bytes.
 */
extern FORCE_INLINE void CastHttp_InitializeRequest(CastHttp_Request_t* request, Utl_Http_Field_t* fields, uint32_t fieldCapacity)
{
    LOGASSERT(NULL != request);
    Utl_Http_InitializeHeadReader(&request->reader, UTL_HTTP_REQUEST, fields, fieldCapacity);
    request->result      = CAST_HTTP_INCOMPLETE;
    request->rangeField  = UTL_HTTP_NO_FIELD;
    request->errorStatus = 0;
    request->headOnly    = FALSE;
    request->options     = FALSE;
    request->subtitles   = FALSE;
    request->keepAlive   = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The complete response uses only caller-owned or bounded automatic storage.
 */
extern FORCE_INLINE void CastHttp_InitializeResponse(CastHttp_Response_t* response, uint16_t status)
{
    LOGASSERT(NULL != response);
    LOGASSERT(IsResponseStatus(status));
    response->contentType              = GetText(NULL, 0);
    response->interval.offset          = 0;
    response->interval.byteCount       = 0;
    response->representationBytes      = 0;
    response->status                   = status;
    response->growing                  = FALSE;
    response->keepAlive                = FALSE;
    response->rangeCompleteLengthKnown = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All members are parsed before the first satisfiable member is selected.
 */
extern FORCE_INLINE CastHttp_RangeResult_t CastHttp_GetRange(const CastHttp_Request_t* request,
                                         uint64_t                  representationBytes,
                                         Utl_Http_RangeInterval_t* interval)
{
    CastHttp_RangeResult_t   result;
    Utl_Http_RangeInterval_t candidate;
    Utl_Http_RangeSet_t      set;
    Utl_Http_Range_t         ranges[CAST_HTTP_RANGE_CAPACITY];
    Utl_Http_Text_t          value;
    bool_t                   parsed;
    uint32_t                 index;
    LOGASSERT(NULL != request);
    LOGASSERT(NULL != interval);
    LOGASSERT(CAST_HTTP_READY == request->result);
    result              = CAST_HTTP_RANGE_ALL;
    candidate.offset    = 0;
    candidate.byteCount = representationBytes;
    if ((UTL_HTTP_NO_FIELD != request->rangeField) && (0 < representationBytes))
    {
        LOGASSERT(request->rangeField < request->reader.head.fieldCount);
        set.ranges   = ranges;
        set.capacity = CAST_HTTP_RANGE_CAPACITY;
        set.count    = 0;
        value        = request->reader.head.fields[request->rangeField].value;
        parsed       = Utl_Http_ReadRange(&set, value.data, value.byteCount);
        if (parsed)
        {
            result = CAST_HTTP_RANGE_UNSATISFIABLE;
            for (index = 0; index < set.count; ++index)
            {
                if (Utl_Http_ResolveRange(&candidate, &ranges[index], representationBytes))
                {
                    result = CAST_HTTP_RANGE_PARTIAL;
                    break;
                }
            }
        }
    }
    if (CAST_HTTP_RANGE_UNSATISFIABLE != result)
    {
        *interval = candidate;
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The complete response uses only caller-owned or bounded automatic storage.
 */
uint32_t CastHttp_GetResponseByteCount(const CastHttp_Response_t* response)
{
    uint32_t       count;
    ResponseHead_t head;
    LOGASSERT(NULL != response);
    count = 0;
    if (IsResponseValid(response))
    {
        SetResponseHead(&head, response);
        count = Utl_Http_GetHeadByteCount(&head.head);
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The underlying terminal head result is not re-read until a new request is initialized.
 */
extern FORCE_INLINE CastHttp_Result_t CastHttp_ReadRequest(CastHttp_Request_t* request, const byte_t* data, uint32_t byteCount)
{
    Utl_Http_Result_t result;
    LOGASSERT(NULL != request);
    if (CAST_HTTP_INCOMPLETE == request->result)
    {
        result = Utl_Http_ReadHeadIncremental(&request->reader, data, byteCount);
        if (UTL_HTTP_CAPACITY == result)
        {
            request->errorStatus = UTL_HTTP_STATUS_HEADERS_TOO_LARGE;
        }
        else if (UTL_HTTP_INVALID == result)
        {
            request->errorStatus = UTL_HTTP_STATUS_BAD_REQUEST;
        }
        else if (UTL_HTTP_COMPLETE == result)
        {
            request->errorStatus = ReadPolicy(request);
            if (0 == request->errorStatus)
            {
                request->result = CAST_HTTP_READY;
            }
        }
        if (0 != request->errorStatus)
        {
            request->keepAlive = FALSE;
            request->result    = CAST_HTTP_REJECTED;
        }
    }
    return request->result;
}
/*************************************************************************************************/
/**
 * \par Details:
 *  The complete response uses only caller-owned or bounded automatic storage.
 */
uint32_t CastHttp_WriteResponse(const CastHttp_Response_t* response, byte_t* output, uint32_t capacity)
{
    uint32_t       count;
    ResponseHead_t head;
    LOGASSERT(NULL != response);
    LOGASSERT((NULL != output) || (0 == capacity));
    count = 0;
    if (IsResponseValid(response))
    {
        SetResponseHead(&head, response);
        count = Utl_Http_WriteHead(&head.head, output, capacity);
    }
    return count;
}
