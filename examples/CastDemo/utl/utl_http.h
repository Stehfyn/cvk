#ifndef UTL_HTTP_H
#define UTL_HTTP_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * HTTP/1.1 syntax, framing and chunked transfer coding: RFC 9112 sections 2 through 9.
 * https://www.rfc-editor.org/rfc/rfc9112.html
 * HTTP ranges: RFC 9110 sections 14.1, 14.2 and 14.4.
 * HTTP fields, tokens and connection options: RFC 9110 sections 5, 7.6.1 and 8.6.
 * https://www.rfc-editor.org/rfc/rfc9110.html
 *
 * Octet-oriented, counted models use borrowed or caller-owned storage. No CRT,
 * allocation, OS calls, sockets or retained global state occur here. CRLF is required;
 * obsolete folding and whitespace before a field colon are rejected. HTTP URI
 * endpoints and origin-form targets follow RFC 9110 section 4.2 and RFC 9112 section 3.2.1.
 * Authentication, caching, content codings and application method policy are separate.
 * uint64_t body counts and caller capacities are local bounds, not HTTP grammar limits.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_types.h"
#include "utl/utl_url.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UTL_HTTP_NO_FIELD          (UINT32_MAX) ///< Search result when no matching field exists.
#define UTL_HTTP_MAX_EMPTY_MEMBERS (32)         ///< Local bound for empty list members.

/* RFC 9110 sections 4.2.1 and 4.2.2. */
#define UTL_HTTP_SCHEME        ("http")
#define UTL_HTTP_SECURE_SCHEME ("https")
#define UTL_HTTP_DEFAULT_PORT  (80)
#define UTL_HTTP_SECURE_PORT   (443)

/* RFC 9110 sections 5.6.2 through 5.6.4; RFC 9112 sections 2, 3 and 5. */
#define UTL_HTTP_CHAR_NUL         ('\0') ///< Internal string terminator; forbidden in HTTP fields.
#define UTL_HTTP_CHAR_TAB         ('\t') ///< HTAB in OWS, quoted text and field values.
#define UTL_HTTP_CHAR_LF          ('\n') ///< LF in CRLF.
#define UTL_HTTP_CHAR_CR          ('\r') ///< CR in CRLF.
#define UTL_HTTP_CHAR_SPACE       (' ')  ///< SP separator and whitespace.
#define UTL_HTTP_CHAR_DEL         (0x7F) ///< DEL; forbidden in field values and reason phrases.
#define UTL_HTTP_CHAR_EXCLAMATION ('!')  ///< tchar punctuation.
#define UTL_HTTP_CHAR_HASH        ('#')  ///< tchar punctuation; URI fragment delimiter.
#define UTL_HTTP_CHAR_DOLLAR      ('$')  ///< tchar punctuation.
#define UTL_HTTP_CHAR_PERCENT     ('%')  ///< tchar punctuation.
#define UTL_HTTP_CHAR_AMP         ('&')  ///< tchar punctuation.
#define UTL_HTTP_CHAR_APOSTROPHE  ('\'') ///< tchar punctuation.
#define UTL_HTTP_CHAR_ASTERISK    ('*')  ///< tchar and range wildcard.
#define UTL_HTTP_CHAR_PLUS        ('+')  ///< tchar punctuation.
#define UTL_HTTP_CHAR_HYPHEN      ('-')  ///< tchar and byte-range delimiter.
#define UTL_HTTP_CHAR_DOT         ('.')  ///< tchar and version delimiter.
#define UTL_HTTP_CHAR_CARET       ('^')  ///< tchar punctuation.
#define UTL_HTTP_CHAR_UNDERSCORE  ('_')  ///< tchar punctuation.
#define UTL_HTTP_CHAR_BACKTICK    ('`')  ///< tchar punctuation.
#define UTL_HTTP_CHAR_PIPE        ('|')  ///< tchar punctuation.
#define UTL_HTTP_CHAR_TILDE       ('~')  ///< tchar punctuation.
#define UTL_HTTP_CHAR_COLON       (':')  ///< Field-name separator.
#define UTL_HTTP_CHAR_QUOTE       ('"')  ///< Quoted-string delimiter.
#define UTL_HTTP_CHAR_BACKSLASH   ('\\') ///< Quoted-pair escape.
#define UTL_HTTP_CHAR_SLASH       ('/')  ///< Version and Content-Range delimiter.
#define UTL_HTTP_CHAR_COMMA       (',')  ///< List delimiter.
#define UTL_HTTP_CHAR_SEMICOLON   (';')  ///< Parameter delimiter.
#define UTL_HTTP_CHAR_EQUALS      ('=')  ///< Parameter and Range separator.
#define UTL_HTTP_CHAR_DIGIT_FIRST ('0')  ///< First ASCII decimal digit.
#define UTL_HTTP_CHAR_DIGIT_LAST  ('9')  ///< Last ASCII decimal digit.
#define UTL_HTTP_CHAR_UPPER_FIRST ('A')  ///< First ASCII uppercase letter.
#define UTL_HTTP_CHAR_UPPER_LAST  ('Z')  ///< Last ASCII uppercase letter.
#define UTL_HTTP_CHAR_LOWER_FIRST ('a')  ///< First ASCII lowercase letter.
#define UTL_HTTP_CHAR_LOWER_LAST  ('z')  ///< Last ASCII lowercase letter.

/* HTTP grammar tokens and field/method names used by this module. */
#define UTL_HTTP_LINE_END                ("\r\n")              ///< CRLF line terminator.
#define UTL_HTTP_FIELD_SEPARATOR         (": ")                ///< Canonical colon and SP.
#define UTL_HTTP_VERSION_PREFIX          ("HTTP/")             ///< Case-sensitive HTTP-name and slash.
#define UTL_HTTP_FIELD_HOST              ("Host")              ///< Host field name.
#define UTL_HTTP_FIELD_CONNECTION        ("Connection")        ///< Connection field name.
#define UTL_HTTP_FIELD_CONTENT_LENGTH    ("Content-Length")    ///< Content-Length field name.
#define UTL_HTTP_FIELD_TRANSFER_ENCODING ("Transfer-Encoding") ///< Transfer-Encoding field name.
#define UTL_HTTP_FIELD_RANGE             ("Range")             ///< Range field name.
#define UTL_HTTP_FIELD_CONTENT_RANGE     ("Content-Range")     ///< Content-Range field name.
#define UTL_HTTP_FIELD_CONTENT_TYPE      ("Content-Type")      ///< Representation media type.
#define UTL_HTTP_FIELD_ACCEPT_RANGES     ("Accept-Ranges")     ///< Supported range units.
#define UTL_HTTP_FIELD_ALLOW             ("Allow")             ///< Supported methods.
#define UTL_HTTP_FIELD_IF_RANGE          ("If-Range")          ///< Range validator.
#define UTL_HTTP_FIELD_EXPECT            ("Expect")            ///< Request expectations.
#define UTL_HTTP_FIELD_DATE              ("Date")              ///< Message origination time.
#define UTL_HTTP_METHOD_HEAD             ("HEAD")              ///< HEAD method.
#define UTL_HTTP_METHOD_CONNECT          ("CONNECT")           ///< CONNECT method.
#define UTL_HTTP_METHOD_GET              ("GET")               ///< GET method.
#define UTL_HTTP_METHOD_OPTIONS          ("OPTIONS")           ///< OPTIONS method.
#define UTL_HTTP_CODING_CHUNKED          ("chunked")           ///< Chunked transfer coding.
#define UTL_HTTP_CONNECTION_CLOSE        ("close")             ///< Connection close option.
#define UTL_HTTP_CONNECTION_KEEP_ALIVE   ("keep-alive")        ///< HTTP/1.0 persistence option.
#define UTL_HTTP_RANGE_UNIT              ("bytes")             ///< Byte range unit.
#define UTL_HTTP_DATE_GMT                ("GMT")               ///< IMF-fixdate UTC designator.
#define UTL_HTTP_DATE_BYTE_COUNT         (29)                  ///< IMF-fixdate wire width.

/* RFC 9112 sections 2 and 4: fixed-width parts of HTTP-version and status-line. */
#define UTL_HTTP_VERSION_MAJOR_OFFSET       (sizeof(UTL_HTTP_VERSION_PREFIX) - 1)
#define UTL_HTTP_VERSION_DOT_OFFSET         (UTL_HTTP_VERSION_MAJOR_OFFSET + 1)
#define UTL_HTTP_VERSION_MINOR_OFFSET       (UTL_HTTP_VERSION_DOT_OFFSET + 1)
#define UTL_HTTP_VERSION_BYTE_COUNT         (UTL_HTTP_VERSION_MINOR_OFFSET + 1)
#define UTL_HTTP_VERSION_MAX_DIGIT          (9)
#define UTL_HTTP_STATUS_DIGIT_COUNT         (3)
#define UTL_HTTP_STATUS_OFFSET              (UTL_HTTP_VERSION_BYTE_COUNT + 1)
#define UTL_HTTP_REASON_OFFSET              (UTL_HTTP_STATUS_OFFSET + UTL_HTTP_STATUS_DIGIT_COUNT + 1)
#define UTL_HTTP_LINE_END_BYTE_COUNT        (sizeof(UTL_HTTP_LINE_END) - 1)
#define UTL_HTTP_FIELD_SEPARATOR_BYTE_COUNT (sizeof(UTL_HTTP_FIELD_SEPARATOR) - 1)

/* RFC 9110 section 15: status bounds used for syntax and body-length precedence. */
#define UTL_HTTP_STATUS_FIRST                 (100)
#define UTL_HTTP_STATUS_LAST                  (599)
#define UTL_HTTP_STATUS_INFORMATIONAL_LAST    (199)
#define UTL_HTTP_STATUS_SUCCESS_FIRST         (200)
#define UTL_HTTP_STATUS_SUCCESS_LAST          (299)
#define UTL_HTTP_STATUS_OK                    (200)
#define UTL_HTTP_STATUS_NO_CONTENT            (204)
#define UTL_HTTP_STATUS_PARTIAL_CONTENT       (206)
#define UTL_HTTP_STATUS_NOT_MODIFIED          (304)
#define UTL_HTTP_STATUS_BAD_REQUEST           (400)
#define UTL_HTTP_STATUS_NOT_FOUND             (404)
#define UTL_HTTP_STATUS_METHOD_NOT_ALLOWED    (405)
#define UTL_HTTP_STATUS_RANGE_NOT_SATISFIABLE (416)
#define UTL_HTTP_STATUS_EXPECTATION_FAILED    (417)
#define UTL_HTTP_STATUS_HEADERS_TOO_LARGE     (431)
#define UTL_HTTP_STATUS_SERVER_ERROR          (500)
#define UTL_HTTP_STATUS_VERSION_NOT_SUPPORTED (505)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief A borrowed HTTP octet span; not a terminated or decoded Unicode string. */
typedef struct Utl_Http_Text_s
{
    const byte_t* data;      ///< Borrowed bytes, NULL only when byteCount is zero.
    uint32_t      byteCount; ///< Counted bytes.
} Utl_Http_Text_t;

/*************************************************************************************************/
/**
 * \brief UTC civil time used by RFC 9110 section 5.6.7 IMF-fixdate.
 */
typedef struct Utl_Http_Date_s
{
    uint16_t year;   ///< Gregorian year, 1 through 9999; serialized with four digits.
    uint8_t  month;  ///< Month, 1 through 12.
    uint8_t  day;    ///< Day, validated against month and Gregorian leap-year rules.
    uint8_t  hour;   ///< UTC hour, 0 through 23.
    uint8_t  minute; ///< Minute, 0 through 59.
    uint8_t  second; ///< Second, 0 through 60, including a leap-second representation.
} Utl_Http_Date_t;

/*************************************************************************************************/
/** \brief One field line with optional whitespace removed from its value. */
typedef struct Utl_Http_Field_s
{
    Utl_Http_Text_t name;  ///< ASCII token, compared without case sensitivity.
    Utl_Http_Text_t value; ///< Counted field-value bytes, possibly empty.
} Utl_Http_Field_t;

/*************************************************************************************************/
/** \brief Expected direction of an HTTP start line. */
typedef enum Utl_Http_Kind_e
{
    UTL_HTTP_REQUEST  = 0,
    UTL_HTTP_RESPONSE = 1
} Utl_Http_Kind_t;

/*************************************************************************************************/
/** \brief Parsing outcome; capacity limits are distinct from invalid wire syntax. */
typedef enum Utl_Http_Result_e
{
    UTL_HTTP_INCOMPLETE = 0,
    UTL_HTTP_COMPLETE   = 1,
    UTL_HTTP_DATA       = 2,
    UTL_HTTP_INVALID    = 3,
    UTL_HTTP_CAPACITY   = 4
} Utl_Http_Result_t;

/*************************************************************************************************/
/** \brief Caller-owned start-line and field model.
 *  Bind fields and fieldCapacity before reading. A failed read clears fieldCount
 *  and byteCount; the backing array may have been used as scratch. Borrowed spans
 *  remain valid only while the original input remains unchanged.
 */
typedef struct Utl_Http_Head_s
{
    Utl_Http_Field_t* fields;        ///< Caller-owned field slots, retained in wire order.
    uint32_t          fieldCapacity; ///< Available field slots.
    uint32_t          fieldCount;    ///< Published field count.
    Utl_Http_Kind_t   kind;          ///< Request or response.
    uint8_t           major;         ///< Single decimal HTTP major version digit.
    uint8_t           minor;         ///< Single decimal HTTP minor version digit.
    Utl_Http_Text_t   method;        ///< Case-sensitive request method; empty for responses.
    Utl_Http_Text_t   target;        ///< Request-target octets; URI semantics belong to the URI layer.
    uint16_t          status;        ///< Response status 100 through 599; zero for requests.
    Utl_Http_Text_t   reason;        ///< Optional response reason phrase; empty for requests.
    uint32_t          byteCount;     ///< Parsed bytes including final CRLF; zero on failure.
} Utl_Http_Head_t;

/*************************************************************************************************/
/**
 * \brief Append-only HTTP head reader with persistent bounded scan state.
 * \par Details:
 *  Initialize once for each message. Keep the input address and all previously
 *  supplied bytes unchanged until completion. head is publishable only when
 *  result is UTL_HTTP_COMPLETE; its field slots may hold scratch before that.
 */
typedef struct Utl_Http_HeadReader_s
{
    Utl_Http_Head_t   head;              ///< Candidate bound to caller-owned field storage.
    uint32_t          scan;              ///< Next unread offset in the retained input.
    uint32_t          lineStart;         ///< First byte of the current incomplete line.
    uint32_t          leadingEmptyLines; ///< Leading request CRLF count.
    bool_t            inFields;          ///< Start-line was validated.
    bool_t            pendingCr;         ///< Last consumed byte was CR, awaiting LF.
    const byte_t*     input;             ///< Retained input address after the first consumed byte.
    Utl_Http_Result_t result;            ///< INCOMPLETE or a latched terminal result.
} Utl_Http_HeadReader_t;

/*************************************************************************************************/
/** \brief Message-body boundary selected by RFC 9112 section 6.3. */
typedef enum Utl_Http_BodyKind_e
{
    UTL_HTTP_BODY_NONE    = 0,
    UTL_HTTP_BODY_LENGTH  = 1,
    UTL_HTTP_BODY_CHUNKED = 2,
    UTL_HTTP_BODY_CLOSE   = 3,
    UTL_HTTP_BODY_TUNNEL  = 4
} Utl_Http_BodyKind_t;

/*************************************************************************************************/
/** \brief Derived framing, independent of the connection or storage implementation. */
typedef struct Utl_Http_Framing_s
{
    Utl_Http_BodyKind_t kind;                   ///< Body delimiter.
    uint64_t            contentLength;          ///< LENGTH bytes; zero for other modes.
    bool_t              keepAlive;              ///< Connection persistence permitted by this message.
    bool_t              hasOtherTransferCoding; ///< A separate decoder is needed after removing chunks.
} Utl_Http_Framing_t;

/*************************************************************************************************/
/** \brief Streaming body state, owned by one caller and reset for each message.
 *  Fields are implementation state. line is caller-owned scratch for chunk size,
 *  extensions and trailer lines. It is never allocated or grown by this module.
 *  Returned data spans borrow the current ReadBody input, never this scratch.
 */
typedef struct Utl_Http_Body_s
{
    Utl_Http_BodyKind_t kind;
    uint32_t            stage;
    uint64_t            remaining;
    uint64_t            contentBytes;
    byte_t*             line;
    uint32_t            lineCapacity;
    uint32_t            lineCount;
    uint32_t            metadataBytes;
    uint32_t            metadataLimit;
    bool_t              hasCarriageReturn;
    Utl_Http_Result_t   terminalResult;
} Utl_Http_Body_t;

/*************************************************************************************************/
/** \brief The three byte-range request forms from RFC 9110 section 14.1. */
typedef enum Utl_Http_RangeKind_e
{
    UTL_HTTP_RANGE_CLOSED = 0,
    UTL_HTTP_RANGE_OPEN   = 1,
    UTL_HTTP_RANGE_SUFFIX = 2
} Utl_Http_RangeKind_t;

/*************************************************************************************************/
/** \brief A typed byte-range specifier; unused number members are ignored. */
typedef struct Utl_Http_Range_s
{
    Utl_Http_RangeKind_t kind;         ///< Closed interval, open interval or suffix.
    uint64_t             first;        ///< First byte offset for CLOSED and OPEN.
    uint64_t             last;         ///< Inclusive final offset for CLOSED.
    uint64_t             suffixLength; ///< Maximum trailing byte count for SUFFIX.
} Utl_Http_Range_t;

/*************************************************************************************************/
/**
 * \brief Caller-owned range-set storage.
 * \par Details:
 *  Bind ranges and capacity before reading. Entries before count are valid.
 *  A failed read clears count; backing entries may have been used as scratch.
 */
typedef struct Utl_Http_RangeSet_s
{
    Utl_Http_Range_t* ranges;   ///< Writable caller-owned specifier array.
    uint32_t          capacity; ///< Available entries.
    uint32_t          count;    ///< Published specifier count.
} Utl_Http_RangeSet_t;

/*************************************************************************************************/
/** \brief A resolved slice, including a possible empty suffix of empty content. */
typedef struct Utl_Http_RangeInterval_s
{
    uint64_t offset;    ///< First byte offset; zero for an empty representation.
    uint64_t byteCount; ///< Number of representation bytes in the slice.
} Utl_Http_RangeInterval_t;

/*************************************************************************************************/
/** \brief Byte Content-Range value for a satisfied or unsatisfied response. */
typedef struct Utl_Http_ContentRange_s
{
    bool_t   hasRange;          ///< TRUE for first-last; FALSE for the unsatisfied '*'.
    bool_t   hasCompleteLength; ///< TRUE for a numeric total; FALSE for unknown '*'.
    uint64_t first;             ///< Inclusive start when hasRange is TRUE.
    uint64_t last;              ///< Inclusive end when hasRange is TRUE.
    uint64_t completeLength;    ///< Total selected-representation bytes when known.
} Utl_Http_ContentRange_t;

/*************************************************************************************************/
/**
 * \brief Borrowed HTTP authority and its effective transport port.
 */
typedef struct Utl_Http_Endpoint_s
{
    Utl_Url_Text_t host;   ///< Encoded name or unbracketed address literal; no resolution occurs here.
    uint16_t       port;   ///< Explicit port, including zero, or the scheme's default port.
    bool_t         secure; ///< TRUE for HTTPS.
} Utl_Http_Endpoint_t;

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
 * \brief Resets an incremental head reader and binds fixed field slots.
 *
 * \par Requirements:
 *  - 001 Every logical field shall reset; no input or field-array allocation shall occur.
 *  - 002 NULL slots shall be allowed only for zero capacity; invalid caller arguments shall assert.
 *  - 003 Reinitialization shall release no storage and shall be safe after any result.
 *
 * \param[out] reader          Caller-owned state, disjoint from fields
 * \param[in]  kind            Expected request or response
 * \param[in]  fields          Caller-owned array retained through completion
 * \param[in]  fieldCapacity   Number of writable slots
 *
 * \retval     None   No result value.
 */
void Utl_Http_InitializeHeadReader(Utl_Http_HeadReader_t* reader,
                                   Utl_Http_Kind_t        kind,
                                   Utl_Http_Field_t*      fields,
                                   uint32_t               fieldCapacity);

/*************************************************************************************************/
/**
 * \brief Validates a nonempty HTTP token.
 *
 * \par Requirements:
 *  - 001 Only RFC 9110 tchar bytes shall be accepted.
 *  - 002 Empty or unreadable spans shall fail without allocation or I/O.
 *
 * \param[in] text   Readable counted octets
 *
 * \retval    TRUE    For a nonempty token.
 * \retval    FALSE   Otherwise.
 */
bool_t Utl_Http_IsToken(Utl_Http_Text_t text);

/*************************************************************************************************/
/**
 * \brief Validates field-value or reason-phrase octets.
 *
 * \par Requirements:
 *  - 001 SP, HTAB, visible ASCII and obs-text shall be accepted.
 *  - 002 CR, LF, NUL, DEL and other controls shall fail; empty values shall succeed.
 *
 * \param[in] text   Readable counted octets
 *
 * \retval    TRUE    For a safe field-value span.
 * \retval    FALSE   Otherwise.
 */
bool_t Utl_Http_IsFieldValue(Utl_Http_Text_t text);

/*************************************************************************************************/
/**
 * \brief Compares two complete counted spans.
 *
 * \par Requirements:
 *  - 001 Comparison shall be byte-exact unless ASCII case folding is requested.
 *  - 002 Locale, Unicode conversion, allocation and CRT shall not be used.
 *
 * \param[in] left         First readable span
 * \param[in] right        Second readable span
 * \param[in] ignoreCase   TRUE for ASCII-insensitive name comparison
 *
 * \retval    TRUE    For complete equality.
 * \retval    FALSE   Otherwise.
 */
bool_t Utl_Http_IsTextEqual(Utl_Http_Text_t left, Utl_Http_Text_t right, bool_t ignoreCase);

/*************************************************************************************************/
/**
 * \brief Derives the canonical wire size of a validated HTTP head.
 *
 * \par Requirements:
 *  - 001 Start-line and every field shall be syntactically valid before a size is returned.
 *  - 002 The size shall include CRLF line endings and the final empty line.
 *  - 003 Invalid models or totals beyond uint32_t shall return zero.
 *
 * \param[in] head   Readable model and borrowed spans
 *
 * \retval    1..UINT32_MAX   Canonical head byte count including the terminating empty line.
 * \retval    0               The model is invalid or the encoded size exceeds uint32_t.
 */
uint32_t Utl_Http_GetHeadByteCount(const Utl_Http_Head_t* head);

/*************************************************************************************************/
/**
 * \brief Derives the IMF-fixdate width after validating a Gregorian UTC value.
 *
 * \par Requirements:
 *  - 001 Month lengths and Gregorian leap years shall be checked before table access.
 *  - 002 The four-digit year and time components shall be representable; second 60 shall be allowed.
 *  - 003 Invalid civil values shall yield zero; a NULL object is caller misuse and shall assert.
 *
 * \param[in] date   Readable UTC civil value
 *
 * \retval    UTL_HTTP_DATE_BYTE_COUNT   Number of IMF-fixdate bytes required.
 * \retval    0                          The civil time is invalid.
 */
uint32_t Utl_Http_GetDateByteCount(const Utl_Http_Date_t* date);

/*************************************************************************************************/
/**
 * \brief Derives HTTP/1.x body boundaries and connection persistence.
 *
 * \par Requirements:
 *  - 001 RFC 9112 body-length precedence shall distinguish HEAD/bodyless responses, CONNECT tunnels, chunked, length and close-delimited bodies.
 *  - 002 Duplicate Content-Length values shall be accepted only when every complete numeric value agrees.
 *  - 003 Transfer-Encoding plus Content-Length, HTTP/1.0 Transfer-Encoding and nonfinal chunked shall be rejected.
 *  - 004 Request transfer codings shall end in chunked; response codings without chunked shall use connection closure.
 *  - 005 Connection tokens shall be parsed as a complete case-insensitive list; close shall override keep-alive.
 *  - 006 HTTP/1.1 requests shall have exactly one Host field; URI/authority validation remains the URI layer's responsibility.
 *  - 007 Failure shall preserve framing; no allocation, CRT or I/O shall occur.
 *
 * \param[out] framing      Published result on success
 * \param[in]  head         Readable validated request or response model
 * \param[in]  responseTo   Associated case-sensitive request method for responses; ignored for requests
 *
 * \retval     TRUE    For supported, unambiguous framing.
 * \retval     FALSE   Otherwise.
 */
bool_t Utl_Http_DeriveFraming(Utl_Http_Framing_t* framing, const Utl_Http_Head_t* head, Utl_Http_Text_t responseTo);

/*************************************************************************************************/
/**
 * \brief Derives one satisfiable slice of a selected representation.
 *
 * \par Requirements:
 *  - 001 Closed ends shall be clipped to the representation; open ranges shall extend to its end.
 *  - 002 Suffixes larger than the representation shall select the whole representation.
 *  - 003 A nonzero suffix of empty content shall succeed with offset and byteCount both zero.
 *  - 004 Invalid or unsatisfiable input shall preserve interval; no allocation, CRT or I/O shall occur.
 *
 * \param[out] interval              Non-NULL result, unchanged on failure
 * \param[in]  range                 Non-NULL typed specifier
 * \param[in]  representationBytes   Current representation length
 *
 * \retval     TRUE    For a satisfiable slice.
 * \retval     FALSE   Otherwise.
 */
bool_t Utl_Http_ResolveRange(Utl_Http_RangeInterval_t* interval,
                             const Utl_Http_Range_t*   range,
                             uint64_t                  representationBytes);

/*************************************************************************************************/
/**
 * \brief Derives a validated range-set's canonical field-value size.
 *
 * \par Requirements:
 *  - 001 Invalid kinds, reversed endpoints, empty sets and invalid count/capacity shall yield zero.
 *  - 002 Sizes beyond uint32_t shall yield zero; no payload bytes shall be written.
 *
 * \param[in] request   Non-NULL model with readable ranges
 *
 * \retval    1..UINT32_MAX   Canonical Range field-value byte count.
 * \retval    0               The range model is invalid or its encoded size exceeds uint32_t.
 */
uint32_t Utl_Http_GetRangeByteCount(const Utl_Http_RangeSet_t* request);

/*************************************************************************************************/
/**
 * \brief Derives a validated Content-Range field-value size.
 *
 * \par Requirements:
 *  - 001 A satisfied range shall have ordered endpoints and any known total shall exceed last.
 *  - 002 An unsatisfied range shall have a known total, including zero.
 *  - 003 Invalid models shall yield zero.
 *
 * \param[in] content   Non-NULL typed field
 *
 * \retval    1..UINT32_MAX   Canonical Content-Range field-value byte count.
 * \retval    0               The Content-Range model is invalid.
 */
uint32_t Utl_Http_GetContentRangeByteCount(const Utl_Http_ContentRange_t* content);

/*************************************************************************************************/
/**
 * \brief Derives an HTTP or HTTPS endpoint from a complete URI-reference.
 *
 * \par Requirements:
 *  - 001 Missing required pointers shall assert; output shall be disjoint from the model and its spans.
 *  - 002 Invalid syntax, unsupported schemes, empty hosts and userinfo shall preserve output.
 *  - 003 Absent or empty ports shall select the scheme default; numeric overflow shall fail.
 *  - 004 Host decoding, resolution, native host support and port-zero policy shall remain with the consumer.
 *
 * \param[in]  url        Required stable model
 * \param[out] endpoint   Required disjoint endpoint output
 *
 * \retval     TRUE    The complete endpoint is published.
 * \retval     FALSE   Output is unchanged.
 */
bool_t Utl_Http_GetUrlEndpoint(const Utl_Url_t* url, Utl_Http_Endpoint_t* endpoint);

/*************************************************************************************************/
/**
 * \brief Derives the exact HTTP origin-form target length.
 *
 * \par Requirements:
 *  - 001 Required pointers shall assert; count output shall be disjoint from the model and its spans.
 *  - 002 The endpoint shall satisfy GetUrlEndpoint; overflow shall preserve the count.
 *  - 003 An empty path shall contribute one slash; a present query shall retain its question mark.
 *  - 004 The fragment shall contribute no bytes.
 *
 * \param[in]  url         Required stable model
 * \param[out] byteCount   Required disjoint count output
 *
 * \retval     TRUE    The exact nonzero count is published.
 * \retval     FALSE   Output is unchanged.
 */
bool_t Utl_Http_GetOriginTargetByteCount(const Utl_Url_t* url, uint32_t* byteCount);

/*************************************************************************************************/
/**
 * \brief Finds the next matching field without hiding duplicate fields.
 *
 * \par Requirements:
 *  - 001 Field names shall use ASCII-insensitive complete comparison.
 *  - 002 Search shall start at startIndex and retain received field order.
 *
 * \param[in] head         Validated head with readable field slots
 * \param[in] name         Counted field name
 * \param[in] startIndex   First slot to examine
 *
 * \retval    0..fieldCount-1     Index of the first matching field at or after startIndex.
 * \retval    UTL_HTTP_NO_FIELD   No matching field was found or the supplied model is invalid.
 */
uint32_t Utl_Http_FindField(const Utl_Http_Head_t* head, Utl_Http_Text_t name, uint32_t startIndex);

/*************************************************************************************************/
/**
 * \brief Consumes only newly appended bytes of a retained HTTP head.
 *
 * \par Requirements:
 *  - 001 Each octet shall advance scan once; completed lines shall be parsed once.
 *  - 002 Strict CRLF, start-line, field and capacity checks shall match ReadHead.
 *  - 003 COMPLETE, INVALID and CAPACITY shall latch until initialization; repeated calls shall preserve the terminal state.
 *  - 004 On failure, head.fieldCount and head.byteCount shall be zero; completion shall stop before any body or pipelined bytes.
 *  - 005 NULL state, unreadable nonempty input, changed input address or a shrinking prefix shall assert as caller misuse.
 *
 * \param[in,out] reader      Initialized state with retained field storage
 * \param[in]     buffer      Same retained input address on every call, disjoint from reader and slots
 * \param[in]     byteCount   Available prefix, never smaller than previously consumed scan
 *
 * \retval        UTL_HTTP_COMPLETE     The head is complete; head.byteCount identifies the first body byte.
 * \retval        UTL_HTTP_INCOMPLETE   More input is required to finish the head.
 * \retval        UTL_HTTP_INVALID      The input violates the supported HTTP grammar.
 * \retval        UTL_HTTP_CAPACITY     Field slots or the leading-empty-line limit were exhausted.
 */
Utl_Http_Result_t Utl_Http_ReadHeadIncremental(Utl_Http_HeadReader_t* reader, const byte_t* buffer, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Reads one HTTP head and borrows its start-line and field spans.
 *
 * \par Requirements:
 *  - 001 Input shall be parsed as counted octets using strict CRLF terminators.
 *  - 002 Malformed names, whitespace before colon, obsolete folding and forbidden controls shall fail.
 *  - 003 Up to UTL_HTTP_MAX_EMPTY_MEMBERS leading CRLF lines shall be ignored for requests.
 *  - 004 The complete head shall be published only on success; failure clears fieldCount and byteCount.
 *  - 005 Callers shall bind field storage before reading; no allocation, CRT or I/O shall occur.
 *  - 006 NULL head, unreadable nonempty input, invalid kind or invalid field storage shall assert as caller misuse.
 *
 * \param[in,out] head        Model with fields and fieldCapacity bound; backing slots may be scratch on failure
 * \param[in]     kind        Expected request or response
 * \param[in]     buffer      Retained readable input, disjoint from model and field slots
 * \param[in]     byteCount   Available bytes, possibly followed by body or another message
 *
 * \retval        UTL_HTTP_COMPLETE     The head is complete; head.byteCount identifies the first body byte.
 * \retval        UTL_HTTP_INCOMPLETE   More input is required to finish the head.
 * \retval        UTL_HTTP_INVALID      The input violates the supported HTTP grammar.
 * \retval        UTL_HTTP_CAPACITY     Field slots or the leading-empty-line limit were exhausted.
 */
Utl_Http_Result_t Utl_Http_ReadHead(Utl_Http_Head_t* head,
                                    Utl_Http_Kind_t  kind,
                                    const byte_t*    buffer,
                                    uint32_t         byteCount);

/*************************************************************************************************/
/**
 * \brief Reads a complete byte Range field value into caller-owned typed slots.
 *
 * \par Requirements:
 *  - 001 The complete list shall be validated; trailing garbage and reversed endpoints shall fail.
 *  - 002 All three forms, ASCII-insensitive bytes unit, outer OWS and list OWS shall be accepted.
 *  - 003 At most UTL_HTTP_MAX_EMPTY_MEMBERS empty list members shall be tolerated.
 *  - 004 Unsupported units, uint64_t overflow or insufficient slots shall fail with count zero.
 *  - 005 Failure may change backing slots but shall publish no entries; no allocation, CRT or I/O shall occur.
 *
 * \param[in,out] request     Non-NULL model with ranges/capacity bound before use
 * \param[in]     buffer      Non-NULL counted field value, disjoint from slots
 * \param[in]     byteCount   Complete value byte count, excluding CRLF
 *
 * \retval        TRUE    For a nonempty fully parsed range set.
 * \retval        FALSE   Otherwise.
 */
bool_t Utl_Http_ReadRange(Utl_Http_RangeSet_t* request, const byte_t* buffer, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Reads a byte Content-Range value.
 *
 * \par Requirements:
 *  - 001 Satisfied known/unknown totals and unsatisfied known totals shall be supported.
 *  - 002 Numeric overflow, invalid order, invalid known total and unconsumed data shall fail.
 *  - 003 No allocation, CRT or I/O shall occur.
 *
 * \param[out] content     Non-NULL typed result, unchanged on failure
 * \param[in]  buffer      Non-NULL complete counted field value
 * \param[in]  byteCount   Readable bytes excluding CRLF
 *
 * \retval     TRUE    For a validated field value.
 * \retval     FALSE   Otherwise.
 */
bool_t Utl_Http_ReadContentRange(Utl_Http_ContentRange_t* content, const byte_t* buffer, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Writes a complete canonical HTTP head into caller storage.
 *
 * \par Requirements:
 *  - 001 Every source span, field and the total output capacity shall be validated before writes.
 *  - 002 Names and field order shall be preserved; separator whitespace and CRLF shall be canonical.
 *  - 003 No body or terminator shall be appended; failure shall leave output unchanged.
 *
 * \param[in]  head       Readable model, disjoint from output
 * \param[out] buffer     Writable caller-owned output
 * \param[in]  capacity   Available bytes
 *
 * \retval     1..UINT32_MAX   Number of complete HTTP head bytes written.
 * \retval     0               The model, output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t Utl_Http_WriteHead(const Utl_Http_Head_t* head, byte_t* buffer, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes a canonical IMF-fixdate without a terminator.
 *
 * \par Requirements:
 *  - 001 The complete civil value and output capacity shall be checked before any write.
 *  - 002 The weekday shall be derived from the Gregorian date; names and GMT shall use protocol English.
 *  - 003 Failure shall preserve output; no locale, allocation, CRT or OS operations shall occur.
 *
 * \param[in]  date       Readable UTC civil value, disjoint from output
 * \param[out] buffer     Writable output, disjoint from date
 * \param[in]  capacity   Writable bytes
 *
 * \retval     UTL_HTTP_DATE_BYTE_COUNT   Number of IMF-fixdate bytes written.
 * \retval     0                          The civil time is invalid or capacity is insufficient; output remains unchanged.
 */
uint32_t Utl_Http_WriteDate(const Utl_Http_Date_t* date, byte_t* buffer, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes a canonical byte Range field value.
 *
 * \par Requirements:
 *  - 001 Validation and complete capacity checks shall precede output writes.
 *  - 002 Success shall emit bytes= followed by comma-separated shortest numeric forms.
 *  - 003 Failure shall leave output unchanged; no allocation, CRT or I/O shall occur.
 *
 * \param[in]  request    Non-NULL readable model, disjoint from output
 * \param[out] buffer     Non-NULL writable bytes
 * \param[in]  capacity   Available bytes; no terminator is appended
 *
 * \retval     1..UINT32_MAX   Number of canonical Range field-value bytes written.
 * \retval     0               The model, output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t Utl_Http_WriteRange(const Utl_Http_RangeSet_t* request, byte_t* buffer, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes a canonical byte Content-Range value.
 *
 * \par Requirements:
 *  - 001 Model and capacity validation shall finish before output writes.
 *  - 002 The writer shall emit the satisfied or unsatisfied grammar exactly.
 *  - 003 Failure shall preserve output; no allocation, CRT or I/O shall occur.
 *
 * \param[in]  content    Non-NULL readable model, disjoint from output
 * \param[out] buffer     Non-NULL writable bytes
 * \param[in]  capacity   Available bytes; no terminator is appended
 *
 * \retval     1..UINT32_MAX   Number of canonical Content-Range field-value bytes written.
 * \retval     0               The model, output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t Utl_Http_WriteContentRange(const Utl_Http_ContentRange_t* content, byte_t* buffer, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes a direct-origin request target without a terminating byte.
 *
 * \par Requirements:
 *  - 001 Required model/count and nonempty output storage shall assert.
 *  - 002 All output storage shall be disjoint from the model and its borrowed spans.
 *  - 003 Invalid endpoint syntax, overflow or insufficient capacity shall preserve all output.
 *  - 004 Path escapes and queries shall be preserved; an empty path shall become '/'.
 *  - 005 The fragment shall be omitted, including its delimiter.
 *
 * \param[in]  url         Required stable model
 * \param[out] output      Writable bytes; NULL only at zero capacity
 * \param[in]  capacity    Writable extent
 * \param[out] byteCount   Required disjoint exact-count output
 *
 * \retval     TRUE    Complete target bytes and count are published.
 * \retval     FALSE   All output is unchanged.
 */
bool_t Utl_Http_WriteOriginTarget(const Utl_Url_t* url, byte_t* output, uint32_t capacity, uint32_t* byteCount);

#endif /* UTL_HTTP_H */
