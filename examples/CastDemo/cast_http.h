#ifndef CAST_HTTP_H
#define CAST_HTTP_H

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

#include "utl/utl_http.h"
#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define CAST_HTTP_RANGE_CAPACITY (32) ///< Application range-count bound, not an HTTP limit.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Request progress and terminal serving decision. */
typedef enum CastHttp_Result_e
{
    CAST_HTTP_INCOMPLETE,
    CAST_HTTP_READY,
    CAST_HTTP_REJECTED
} CastHttp_Result_t;

/*************************************************************************************************/
/** \brief Application selection from a complete byte range set. */
typedef enum CastHttp_RangeResult_e
{
    CAST_HTTP_RANGE_ALL,
    CAST_HTTP_RANGE_PARTIAL,
    CAST_HTTP_RANGE_UNSATISFIABLE
} CastHttp_RangeResult_t;

/*************************************************************************************************/
/** \brief Exclusive request state retaining caller field records and input borrows. */
typedef struct CastHttp_Request_s
{
    Utl_Http_HeadReader_t reader;      ///< Counted incremental parser with retained caller slots.
    CastHttp_Result_t     result;      ///< Terminal result, reset only by InitializeRequest.
    uint32_t              rangeField;  ///< Unique usable Range field, or UTL_HTTP_NO_FIELD.
    uint16_t              errorStatus; ///< Rejection status; zero while incomplete or ready.
    bool_t                headOnly;    ///< HEAD: send representation headers without its bytes.
    bool_t                options;     ///< OPTIONS: serve method/CORS metadata.
    bool_t                subtitles;   ///< Exact application subtitle endpoint.
    bool_t                keepAlive;   ///< Framing permits reuse after a complete response.
} CastHttp_Request_t;

/*************************************************************************************************/
/** \brief Complete outgoing response metadata, independent of native sending. */
typedef struct CastHttp_Response_s
{
    Utl_Http_Text_t          contentType;              ///< Stable counted media type; required for successful media responses.
    Utl_Http_RangeInterval_t interval;                 ///< Fixed successful representation slice.
    uint64_t                 representationBytes;      ///< Available snapshot length; bounds every selected interval.
    uint16_t                 status;                   ///< Supported application response status.
    bool_t                   growing;                  ///< Unbounded progressive response: no Content-Length, status 200.
    bool_t                   keepAlive;                ///< Requested persistence; growing responses always close.
    bool_t                   rangeCompleteLengthKnown; ///< Content-Range total is known; FALSE writes an asterisk for 206.
} CastHttp_Response_t;

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
 * \brief Binds retained field slots and begins one request without acquiring resources.
 *
 * \par Details:
 *  An incomplete request is ready for appended input.
 *
 * \par Requirements:
 *  - 001 Required owner and nonempty storage shall assert on misuse.
 *  - 002 Previous input borrows shall be revoked; every policy field shall be initialized.
 *  - 003 Initialization shall allocate nothing and perform no I/O.
 *
 * \param[out]    request         Exclusive retained request state
 * \param[in,out] fields          Disjoint fixed field records
 * \param[in]     fieldCapacity   Number of writable records
 *
 * \retval        None No value is returned.
 */
void CastHttp_InitializeRequest(CastHttp_Request_t* request, Utl_Http_Field_t* fields, uint32_t fieldCapacity);

/*************************************************************************************************/
/**
 * \brief Establishes an empty typed response before representation data is supplied.
 *
 * \par Details:
 *  The empty response is initialized.
 *
 * \par Requirements:
 *  - 001 The result pointer shall assert when missing.
 *  - 002 The status shall be a supported application response status; invalid use shall assert.
 *  - 003 No storage or native resource shall be acquired.
 *
 * \param[out] response   Required exclusive model
 * \param[in]  status     Named HTTP status
 *
 * \retval     None No value is returned.
 */
void CastHttp_InitializeResponse(CastHttp_Response_t* response, uint16_t status);

/*************************************************************************************************/
/**
 * \brief Derives the selected fixed-representation interval from a ready request.
 *
 * \par Requirements:
 *  - 001 Required pointers and READY state shall assert as development errors.
 *  - 002 Only GET shall apply Range; HEAD, OPTIONS and If-Range without a validator shall select the whole representation.
 *  - 003 Invalid, excessive, duplicate or unknown-unit Range fields and empty representations shall select the whole representation.
 *  - 004 A completely validated set shall select its first satisfiable member; unsatisfiable sets shall preserve interval.
 *
 * \param[in]  request               Ready immutable request and retained source
 * \param[in]  representationBytes   Complete representation extent
 * \param[out] interval              Disjoint selected byte extent
 *
 * \retval     CAST_HTTP_RANGE_ALL             The entire representation interval is published.
 * \retval     CAST_HTTP_RANGE_PARTIAL         The selected satisfiable interval is published.
 * \retval     CAST_HTTP_RANGE_UNSATISFIABLE   No interval is published.
 */
CastHttp_RangeResult_t CastHttp_GetRange(const CastHttp_Request_t* request,
                                         uint64_t                  representationBytes,
                                         Utl_Http_RangeInterval_t* interval);

/*************************************************************************************************/
/**
 * \brief Derives a complete validated response-head size.
 *
 * \par Requirements:
 *  - 001 The required model shall assert when missing.
 *  - 002 Invalid media types, inconsistent ranges or unsupported response combinations shall fail.
 *  - 003 204 shall omit Content-Length; unbounded growing output shall be close-delimited.
 *  - 004 A finite partial snapshot may omit the final total; 416 shall require the current complete length.
 *  - 005 No allocation, CRT or I/O shall occur.
 *
 * \param[in] response   Stable typed metadata
 *
 * \retval    nonzero   Exact head extent including final CRLF.
 * \retval    0         The model is invalid.
 */
uint32_t CastHttp_GetResponseByteCount(const CastHttp_Response_t* response);

/*************************************************************************************************/
/**
 * \brief Consumes appended head bytes and derives one read-only serving decision.
 *
 * \par Requirements:
 *  - 001 The retained input address and previously supplied bytes shall remain unchanged until the request is retired.
 *  - 002 The HTTP reader shall stop at the exact head boundary, leaving pipelined bytes unconsumed.
 *  - 003 Malformed framing, unsupported versions/methods and nonempty or transfer-coded bodies shall reject with connection closure.
 *  - 004 READY and REJECTED shall latch until InitializeRequest; only READY may supply serving fields.
 *  - 005 No allocation, CRT call or I/O shall occur.
 *
 * \param[in,out] request     Initialized retained state
 * \param[in]     data        Counted immutable prefix
 * \param[in]     byteCount   Available extent
 *
 * \retval        CAST_HTTP_INCOMPLETE   More head bytes are required.
 * \retval        CAST_HTTP_READY        The bodyless request and policy are complete.
 * \retval        CAST_HTTP_REJECTED     errorStatus describes the final response; the connection shall close.
 */
CastHttp_Result_t CastHttp_ReadRequest(CastHttp_Request_t* request, const byte_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Writes one complete application response head after model and capacity validation.
 *
 * \par Requirements:
 *  - 001 Required model and nonempty output shall assert on missing storage.
 *  - 002 Output shall be disjoint from the model and every borrowed span.
 *  - 003 Failure shall preserve output; no implicit terminator shall be appended.
 *  - 004 Content length/range, persistence, allowed methods and existing CORS fields shall use counted typed values.
 *
 * \param[in]  response   Stable response metadata
 * \param[out] output     Disjoint writable extent
 * \param[in]  capacity   Available bytes
 *
 * \retval     nonzero   Complete head bytes written.
 * \retval     0         Invalid model or insufficient capacity; output is unchanged.
 */
uint32_t CastHttp_WriteResponse(const CastHttp_Response_t* response, byte_t* output, uint32_t capacity);

#endif /* CAST_HTTP_H */
