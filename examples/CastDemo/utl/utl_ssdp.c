/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * UPnP Device Architecture 1.0/1.1, sections 1.1 and 1.3:
 * https://upnp.org/specs/arch/UPnP-arch-DeviceArchitecture-v1.1.pdf
 * This module implements the IPv4 multicast M-SEARCH exchange used by CastDemo.
 * HTTP octet syntax belongs to utl_http. URL resolution, device XML, datagram
 * transport, expiry scheduling and advertisement subscriptions are separate layers.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_compiler.h"
#include "utl/utl_ssdp.h"
#include "utl/utl_decimal.h"
#include "os/os_assert.h"
#include "crt/crt_memory.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

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
 * \brief Constructs a borrowed HTTP octet view.
 *
 * \par Requirements:
 *  - 001 Required inputs shall refer to live counted storage.
 *
 * \param[in] data        Borrowed bytes
 * \param[in] byteCount   Readable extent
 *
 * \retval    text   Unmodified counted view.
 */
static FORCE_INLINE Utl_Http_Text_t Text(const byte_t* data, uint32_t byteCount);

/*************************************************************************************************/
/**
 * \brief Checks a nonempty visible ASCII component.
 *
 * \par Requirements:
 *  - 001 Required inputs shall refer to live counted storage.
 *
 * \param[in] text   Borrowed component
 *
 * \retval    TRUE    Every byte is visible ASCII.
 * \retval    FALSE   Empty, missing or nonvisible bytes.
 */
static FORCE_INLINE bool_t IsVisible(Utl_Http_Text_t text);

/*************************************************************************************************/
/**
 * \brief Checks a case-sensitive counted prefix.
 *
 * \par Requirements:
 *  - 001 Required inputs shall refer to live counted storage.
 *
 * \param[in] text     Borrowed input
 * \param[in] prefix   Required prefix
 *
 * \retval    TRUE    Prefix matches.
 * \retval    FALSE   Missing or mismatched prefix.
 */
static FORCE_INLINE bool_t HasPrefix(Utl_Http_Text_t text, Utl_Http_Text_t prefix);

/*************************************************************************************************/
/**
 * \brief Reads one required singleton discovery field.
 *
 * \par Requirements:
 *  - 001 Required inputs shall refer to live counted storage.
 *
 * \param[out] value   Output preserved on failure
 * \param[in]  head    Parsed head
 * \param[in]  name    Required field name
 *
 * \retval     TRUE    Exactly one field is present.
 * \retval     FALSE   The field is absent or duplicated.
 */
static FORCE_INLINE bool_t ReadUniqueField(Utl_Http_Text_t* value, const Utl_Http_Head_t* head, Utl_Http_Text_t name);

/*************************************************************************************************/
/**
 * \brief Reads the single max-age directive while skipping extensions.
 *
 * \par Requirements:
 *  - 001 Required inputs shall refer to live counted storage.
 *
 * \param[out] maximumAge   Preserved on failure
 * \param[in]  text         Counted Cache-Control field value
 *
 * \retval     TRUE    Exactly one representable decimal max-age was found.
 * \retval     FALSE   Missing, duplicate, malformed or overflowing max-age.
 */
static FORCE_INLINE bool_t ReadMaximumAge(uint64_t* maximumAge, Utl_Http_Text_t text);

/*************************************************************************************************/
/**
 * \brief Checks the USN UUID and its target-dependent suffix.
 *
 * \par Requirements:
 *  - 001 Required inputs shall refer to live counted storage.
 *
 * \param[out] uuid     UUID view preserved on failure
 * \param[in]  usn      Counted USN
 * \param[in]  target   Validated non-all search target
 *
 * \retval     TRUE    Identity and target agree.
 * \retval     FALSE   Missing UUID or mismatched identity suffix.
 */
static FORCE_INLINE bool_t ReadIdentity(Utl_Http_Text_t* uuid, Utl_Http_Text_t usn, Utl_Http_Text_t target);

/*************************************************************************************************/
/**
 * \brief Binds a validated search to four fixed HTTP field slots.
 *
 * \par Requirements:
 *  - 001 Required inputs shall refer to live counted storage.
 *
 * \param[out] head       Caller-owned HTTP model
 * \param[out] fields     Four caller-owned slots
 * \param[out] waitText   Ten writable decimal bytes
 * \param[in]  search     Previously validated search
 *
 * \retval     None
 */
static FORCE_INLINE void InitializeSearchHead(Utl_Http_Head_t*         head,
                                              Utl_Http_Field_t*        fields,
                                              byte_t*                  waitText,
                                              const Utl_Ssdp_Search_t* search);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Http_Text_t Text(const byte_t* data, uint32_t byteCount)
{
    Utl_Http_Text_t text;

    text.data      = data;
    text.byteCount = byteCount;

    return text;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No locale or Unicode classification is used for SSDP URI tokens.
 */
static FORCE_INLINE bool_t IsVisible(Utl_Http_Text_t text)
{
    bool_t   is_valid;
    uint32_t index;
    byte_t   value;

    is_valid = (NULL != text.data) && (0 < text.byteCount);
    for (index = 0; is_valid && (index < text.byteCount); ++index)
    {
        value    = (byte_t)text.data[index];
        is_valid = (UTL_SSDP_VISIBLE_FIRST <= value) && (value <= UTL_SSDP_VISIBLE_LAST);
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t HasPrefix(Utl_Http_Text_t text, Utl_Http_Text_t prefix)
{
    bool_t matches;

    matches = (prefix.byteCount <= text.byteCount);
    if (matches)
    {
        matches = Utl_Http_IsTextEqual(Text(text.data, prefix.byteCount), prefix, FALSE);
    }

    return matches;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Field names use HTTP case folding; repeated singleton fields are ambiguous and rejected.
 */
static FORCE_INLINE bool_t ReadUniqueField(Utl_Http_Text_t* value, const Utl_Http_Head_t* head, Utl_Http_Text_t name)
{
    uint32_t first;
    bool_t   is_valid;
    uint32_t second;

    first    = Utl_Http_FindField(head, name, 0);
    is_valid = (UTL_HTTP_NO_FIELD != first);
    if (is_valid)
    {
        second   = Utl_Http_FindField(head, name, first + 1);
        is_valid = (UTL_HTTP_NO_FIELD == second);
    }
    if (is_valid)
    {
        (*value) = head->fields[first].value;
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Comma splitting respects quoted extension values and escapes. Other directives
 *  are ignored as required by UDA; no network cache or deadline is mutated.
 */
static FORCE_INLINE bool_t ReadMaximumAge(uint64_t* maximumAge, Utl_Http_Text_t text)
{
    bool_t          is_valid;
    uint32_t        index;
    bool_t          quoted;
    bool_t          escaped;
    uint32_t        begin;
    bool_t          found;
    uint64_t        age;
    uint32_t        consumed;
    bool_t          at_end;
    byte_t          value;
    bool_t          at_separator;
    uint32_t        first;
    uint32_t        last;
    uint32_t        equals;
    uint32_t        name_end;
    Utl_Http_Text_t name;
    bool_t          is_maximum_age;

    is_valid = (0 < text.byteCount);
    index    = 0;
    quoted   = FALSE;
    escaped  = FALSE;
    begin    = 0;
    found    = FALSE;
    age      = 0;
    consumed = 0;
    for (; is_valid; ++index)
    {
        at_end       = (index == text.byteCount);
        value        = at_end ? UTL_HTTP_CHAR_COMMA : text.data[index];
        at_separator = at_end || ((!quoted) && (UTL_HTTP_CHAR_COMMA == value));
        if (at_separator)
        {
            is_valid = !(quoted || escaped);
            if (is_valid)
            {
                first = begin;
                last  = index;
                for (; first < last; ++first)
                {
                    if ((UTL_HTTP_CHAR_SPACE != text.data[first]) && (UTL_HTTP_CHAR_TAB != text.data[first]))
                    {
                        break;
                    }
                }
                for (; first < last; --last)
                {
                    if ((UTL_HTTP_CHAR_SPACE != text.data[last - 1]) && (UTL_HTTP_CHAR_TAB != text.data[last - 1]))
                    {
                        break;
                    }
                }
                equals = first;
                for (; equals < last; ++equals)
                {
                    if (UTL_HTTP_CHAR_EQUALS == text.data[equals])
                    {
                        break;
                    }
                }
                name_end = equals;
                for (; first < name_end; --name_end)
                {
                    if ((UTL_HTTP_CHAR_SPACE != text.data[name_end - 1]) &&
                        (UTL_HTTP_CHAR_TAB != text.data[name_end - 1]))
                    {
                        break;
                    }
                }
                name = Text(text.data + first, name_end - first);
                is_maximum_age =
                    Utl_Http_IsTextEqual(name,
                                         Text((const byte_t*)UTL_SSDP_MAX_AGE, sizeof(UTL_SSDP_MAX_AGE) - 1),
                                         TRUE);
                if (is_maximum_age)
                {
                    is_valid = (!found) && (equals < last);
                    if (is_valid)
                    {
                        first = equals + 1;
                        for (; first < last; ++first)
                        {
                            if ((UTL_HTTP_CHAR_SPACE != text.data[first]) && (UTL_HTTP_CHAR_TAB != text.data[first]))
                            {
                                break;
                            }
                        }
                        is_valid = Utl_Decimal_ReadUint64(&age, &consumed, text.data + first, last - first);
                    }
                    if (is_valid)
                    {
                        is_valid = (consumed == (last - first));
                        found    = is_valid;
                    }
                }
            }
            if (at_end)
            {
                break;
            }
            begin = index + 1;
        }
        else if (escaped)
        {
            escaped = FALSE;
        }
        else if (quoted && (UTL_HTTP_CHAR_BACKSLASH == value))
        {
            escaped = TRUE;
        }
        else if (UTL_HTTP_CHAR_QUOTE == value)
        {
            quoted = !quoted;
        }
    }
    is_valid = is_valid && found;
    if (is_valid)
    {
        (*maximumAge) = age;
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  UDA 1.1 requires compatibility with noncanonical UPnP 1.0 UUID strings.
 *  UUID searches use an unsuffixed USN; device/service searches require ::target.
 */
static FORCE_INLINE bool_t ReadIdentity(Utl_Http_Text_t* uuid, Utl_Http_Text_t usn, Utl_Http_Text_t target)
{
    Utl_Http_Text_t prefix;
    bool_t          valid_usn;
    bool_t          uuid_target;
    bool_t          matches;
    uint32_t        index;
    bool_t          has_suffix;
    uint32_t        suffix;

    prefix      = Text((const byte_t*)UTL_SSDP_UUID_PREFIX, sizeof(UTL_SSDP_UUID_PREFIX) - 1);
    valid_usn   = IsVisible(usn) && HasPrefix(usn, prefix) && (prefix.byteCount < usn.byteCount);
    uuid_target = HasPrefix(target, prefix);
    matches     = FALSE;
    if (valid_usn)
    {
        if (uuid_target)
        {
            matches = Utl_Http_IsTextEqual(usn, target, FALSE);
            if (matches)
            {
                (*uuid) = usn;
            }
        }
        else
        {
            for (index = prefix.byteCount; index < (usn.byteCount - 1); ++index)
            {
                has_suffix = (UTL_HTTP_CHAR_COLON == usn.data[index]) && (UTL_HTTP_CHAR_COLON == usn.data[index + 1]);
                if (has_suffix)
                {
                    suffix  = index + 2;
                    matches = (index > prefix.byteCount);
                    matches =
                        matches && Utl_Http_IsTextEqual(Text(usn.data + suffix, usn.byteCount - suffix), target, FALSE);
                    if (matches)
                    {
                        (*uuid) = Text(usn.data, index);
                    }
                    break;
                }
            }
        }
    }

    return matches;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All borrowed fields remain live only through the immediate size or write call.
 */
static FORCE_INLINE void InitializeSearchHead(Utl_Http_Head_t*         head,
                                              Utl_Http_Field_t*        fields,
                                              byte_t*                  waitText,
                                              const Utl_Ssdp_Search_t* search)
{
    uint32_t written;

    CRT_Memory_Set(head, 0, sizeof(Utl_Http_Head_t));
    written = Utl_Decimal_WriteUint64(search->maximumWait, waitText, UTL_SSDP_WAIT_DIGITS);
    LOGASSERT(0 < written);
    fields[0].name            = Text((const byte_t*)UTL_HTTP_FIELD_HOST, sizeof(UTL_HTTP_FIELD_HOST) - 1);
    fields[0].value           = Text((const byte_t*)UTL_SSDP_MULTICAST_HOST, sizeof(UTL_SSDP_MULTICAST_HOST) - 1);
    fields[1].name            = Text((const byte_t*)UTL_SSDP_FIELD_MAN, sizeof(UTL_SSDP_FIELD_MAN) - 1);
    fields[1].value           = Text((const byte_t*)UTL_SSDP_DISCOVER, sizeof(UTL_SSDP_DISCOVER) - 1);
    fields[2].name            = Text((const byte_t*)UTL_SSDP_FIELD_MX, sizeof(UTL_SSDP_FIELD_MX) - 1);
    fields[2].value.data      = waitText;
    fields[2].value.byteCount = written;
    fields[3].name            = Text((const byte_t*)UTL_SSDP_FIELD_ST, sizeof(UTL_SSDP_FIELD_ST) - 1);
    fields[3].value           = search->target;
    head->fields              = fields;
    head->fieldCapacity       = UTL_SSDP_SEARCH_FIELD_COUNT;
    head->fieldCount          = UTL_SSDP_SEARCH_FIELD_COUNT;
    head->kind                = UTL_HTTP_REQUEST;
    head->major               = 1;
    head->minor               = 1;
    head->method              = Text((const byte_t*)UTL_SSDP_METHOD_SEARCH, sizeof(UTL_SSDP_METHOD_SEARCH) - 1);
    head->target              = Text((const byte_t*)UTL_SSDP_REQUEST_TARGET, sizeof(UTL_SSDP_REQUEST_TARGET) - 1);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Target storage is borrowed until every size/write call is complete.
 */
void Utl_Ssdp_InitializeSearch(Utl_Ssdp_Search_t* search, Utl_Http_Text_t target, uint32_t maximumWait)
{
    LOGASSERT(NULL != search);
    LOGASSERT(Utl_Ssdp_IsTarget(target));
    LOGASSERT(maximumWait > 0);

    search->target      = target;
    search->maximumWait = maximumWait;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  UUID identifiers accept the noncanonical visible strings required for UPnP 1.0
 *  compatibility. Device/service URNs require nonempty domain/type and positive version.
 */
bool_t Utl_Ssdp_IsTarget(Utl_Http_Text_t target)
{
    bool_t          is_valid;
    bool_t          is_all;
    bool_t          is_root;
    Utl_Http_Text_t prefix;
    bool_t          is_uuid;
    uint32_t        index;
    bool_t          has_suffix;
    uint32_t        part;
    uint32_t        begin;
    bool_t          found;
    Utl_Http_Text_t component;
    bool_t          is_device;
    bool_t          is_service;
    bool_t          nonzero;
    byte_t          value;
    bool_t          is_digit;

    is_valid = IsVisible(target);
    if (is_valid)
    {
        is_all  = Utl_Http_IsTextEqual(target, Text((const byte_t*)UTL_SSDP_ALL, sizeof(UTL_SSDP_ALL) - 1), FALSE);
        is_root = Utl_Http_IsTextEqual(target,
                                       Text((const byte_t*)UTL_SSDP_ROOT_DEVICE, sizeof(UTL_SSDP_ROOT_DEVICE) - 1),
                                       FALSE);
        if (!(is_all || is_root))
        {
            prefix  = Text((const byte_t*)UTL_SSDP_UUID_PREFIX, sizeof(UTL_SSDP_UUID_PREFIX) - 1);
            is_uuid = HasPrefix(target, prefix);
            if (is_uuid)
            {
                is_valid = (prefix.byteCount < target.byteCount);
                for (index = prefix.byteCount; is_valid && (index < (target.byteCount - 1)); ++index)
                {
                    has_suffix =
                        (UTL_HTTP_CHAR_COLON == target.data[index]) && (UTL_HTTP_CHAR_COLON == target.data[index + 1]);
                    is_valid = !has_suffix;
                }
            }
            else
            {
                prefix   = Text((const byte_t*)UTL_SSDP_URN_PREFIX, sizeof(UTL_SSDP_URN_PREFIX) - 1);
                is_valid = HasPrefix(target, prefix);
                index    = prefix.byteCount;
                for (part = 0; is_valid && (part < UTL_SSDP_URN_COMPONENT_COUNT); ++part)
                {
                    begin = index;
                    found = FALSE;
                    for (; index < target.byteCount; ++index)
                    {
                        if (UTL_HTTP_CHAR_COLON == target.data[index])
                        {
                            found = TRUE;
                            break;
                        }
                    }
                    is_valid = found && (begin < index);
                    if (is_valid && (UTL_SSDP_URN_KIND_INDEX == part))
                    {
                        component = Text(target.data + begin, index - begin);
                        is_device = Utl_Http_IsTextEqual(
                            component,
                            Text((const byte_t*)UTL_SSDP_DEVICE_KIND, sizeof(UTL_SSDP_DEVICE_KIND) - 1),
                                 FALSE);
                        is_service = Utl_Http_IsTextEqual(
                            component,
                            Text((const byte_t*)UTL_SSDP_SERVICE_KIND, sizeof(UTL_SSDP_SERVICE_KIND) - 1),
                                 FALSE);
                        is_valid = is_device || is_service;
                    }
                    ++index;
                }
                nonzero = FALSE;
                for (; is_valid && (index < target.byteCount); ++index)
                {
                    value    = target.data[index];
                    is_digit = (UTL_HTTP_CHAR_DIGIT_FIRST <= value) && (value <= UTL_HTTP_CHAR_DIGIT_LAST);
                    is_valid = is_digit;
                    nonzero  = nonzero || (UTL_HTTP_CHAR_DIGIT_FIRST != value);
                }
                is_valid = is_valid && nonzero;
            }
        }
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The count includes the final blank line and excludes any C string terminator.
 */
uint32_t Utl_Ssdp_GetSearchByteCount(const Utl_Ssdp_Search_t* search)
{
    bool_t           valid_target;
    bool_t           valid_wait;
    bool_t           is_valid;
    uint32_t         byte_count;
    Utl_Http_Head_t  head;
    Utl_Http_Field_t fields[UTL_SSDP_SEARCH_FIELD_COUNT];
    byte_t           wait_text[UTL_SSDP_WAIT_DIGITS];

    LOGASSERT(NULL != search);

    valid_target = Utl_Ssdp_IsTarget(search->target);
    valid_wait   = (search->maximumWait > 0);
    is_valid     = valid_target && valid_wait;
    byte_count   = 0;
    if (is_valid)
    {
        InitializeSearchHead(&head, fields, wait_text, search);
        byte_count = Utl_Http_GetHeadByteCount(&head);
    }

    return byte_count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The input head is already parsed by utl_http. Datagram completion and transport
 *  address selection belong to the caller. Unrecognized extension fields are retained
 *  in the original head and ignored by this version.
 */
bool_t Utl_Ssdp_ReadSearch(Utl_Ssdp_Search_t* search, const Utl_Http_Head_t* head)
{
    Utl_Http_Text_t   host;
    Utl_Http_Text_t   man;
    Utl_Http_Text_t   wait;
    uint64_t          maximum_wait;
    uint32_t          consumed;
    Utl_Ssdp_Search_t candidate;
    bool_t            valid_head;
    bool_t            valid_version;
    bool_t            is_valid;
    bool_t            valid_method;
    bool_t            valid_target;
    bool_t            has_host;
    bool_t            has_man;
    bool_t            has_wait;
    bool_t            has_target;
    bool_t            has_address;
    bool_t            has_search;
    bool_t            valid_host;
    bool_t            valid_man;
    bool_t            fits_wait;

    LOGASSERT(NULL != search);
    LOGASSERT(NULL != head);

    host         = Text(NULL, 0);
    man          = Text(NULL, 0);
    wait         = Text(NULL, 0);
    maximum_wait = 0;
    consumed     = 0;
    CRT_Memory_Set(&candidate, 0, sizeof(Utl_Ssdp_Search_t));
    valid_head    = (0 < Utl_Http_GetHeadByteCount(head));
    valid_version = (1 == head->major) && (1 == head->minor);
    is_valid      = valid_head && valid_version && (UTL_HTTP_REQUEST == head->kind);
    if (is_valid)
    {
        valid_method =
            Utl_Http_IsTextEqual(head->method,
                                 Text((const byte_t*)UTL_SSDP_METHOD_SEARCH, sizeof(UTL_SSDP_METHOD_SEARCH) - 1),
                                 FALSE);
        valid_target =
            Utl_Http_IsTextEqual(head->target,
                                 Text((const byte_t*)UTL_SSDP_REQUEST_TARGET, sizeof(UTL_SSDP_REQUEST_TARGET) - 1),
                                 FALSE);
        is_valid = valid_method && valid_target;
    }
    if (is_valid)
    {
        has_host =
            ReadUniqueField(&host, head, Text((const byte_t*)UTL_HTTP_FIELD_HOST, sizeof(UTL_HTTP_FIELD_HOST) - 1));
        has_man    = ReadUniqueField(&man, head, Text((const byte_t*)UTL_SSDP_FIELD_MAN, sizeof(UTL_SSDP_FIELD_MAN) - 1));
        has_wait   = ReadUniqueField(&wait, head, Text((const byte_t*)UTL_SSDP_FIELD_MX, sizeof(UTL_SSDP_FIELD_MX) - 1));
        has_target = ReadUniqueField(&candidate.target,
                                     head,
                                     Text((const byte_t*)UTL_SSDP_FIELD_ST, sizeof(UTL_SSDP_FIELD_ST) - 1));
        has_address = has_host && has_man;
        has_search  = has_wait && has_target;
        is_valid    = has_address && has_search;
    }
    if (is_valid)
    {
        valid_host =
            Utl_Http_IsTextEqual(host,
                                 Text((const byte_t*)UTL_SSDP_MULTICAST_HOST, sizeof(UTL_SSDP_MULTICAST_HOST) - 1),
                                 FALSE);
        valid_man =
            Utl_Http_IsTextEqual(man, Text((const byte_t*)UTL_SSDP_DISCOVER, sizeof(UTL_SSDP_DISCOVER) - 1), FALSE);
        valid_target = Utl_Ssdp_IsTarget(candidate.target);
        is_valid     = valid_host && valid_man && valid_target;
    }
    if (is_valid)
    {
        is_valid = (0 < wait.byteCount);
        if (is_valid)
        {
            is_valid = Utl_Decimal_ReadUint64(&maximum_wait, &consumed, wait.data, wait.byteCount);
        }
        if (is_valid)
        {
            fits_wait = (0 < maximum_wait) && (maximum_wait <= UINT32_MAX);
            is_valid  = fits_wait && (consumed == wait.byteCount);
        }
    }
    if (is_valid)
    {
        candidate.maximumWait = (uint32_t)maximum_wait;
        (*search)             = candidate;
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The model covers the UPnP 1.0-compatible required search fields. UDA 1.1 boot/config
 *  and search-port extensions remain available in the original head. URL and server
 *  product semantics require their own modules; this function does not resolve URLs.
 */
bool_t Utl_Ssdp_ReadResponse(Utl_Ssdp_Response_t* response, const Utl_Http_Head_t* head)
{
    Utl_Http_Text_t     cache;
    Utl_Http_Text_t     extension;
    Utl_Ssdp_Response_t candidate;
    bool_t              valid_head;
    bool_t              valid_version;
    bool_t              valid_status;
    bool_t              is_valid;
    bool_t              has_target;
    bool_t              has_usn;
    bool_t              has_location;
    bool_t              has_server;
    bool_t              has_cache;
    bool_t              has_extension;
    bool_t              has_identity;
    bool_t              has_description;
    bool_t              has_lifetime;
    bool_t              valid_target;
    bool_t              is_all;
    bool_t              valid_description;
    bool_t              valid_extension;

    LOGASSERT(NULL != response);
    LOGASSERT(NULL != head);

    cache     = Text(NULL, 0);
    extension = Text(NULL, 0);
    CRT_Memory_Set(&candidate, 0, sizeof(Utl_Ssdp_Response_t));
    valid_head    = (0 < Utl_Http_GetHeadByteCount(head));
    valid_version = (1 == head->major) && (1 == head->minor);
    valid_status  = (UTL_HTTP_RESPONSE == head->kind) && (UTL_HTTP_STATUS_OK == head->status);
    is_valid      = valid_head && valid_version && valid_status;
    if (is_valid)
    {
        is_valid = Utl_Http_IsTextEqual(head->reason,
                                        Text((const byte_t*)UTL_SSDP_REASON_OK, sizeof(UTL_SSDP_REASON_OK) - 1),
                                        FALSE);
    }
    if (is_valid)
    {
        has_target = ReadUniqueField(&candidate.target,
                                     head,
                                     Text((const byte_t*)UTL_SSDP_FIELD_ST, sizeof(UTL_SSDP_FIELD_ST) - 1));
        has_usn = ReadUniqueField(&candidate.usn,
                                  head,
                                  Text((const byte_t*)UTL_SSDP_FIELD_USN, sizeof(UTL_SSDP_FIELD_USN) - 1));
        has_location =
            ReadUniqueField(&candidate.location,
                            head,
                            Text((const byte_t*)UTL_SSDP_FIELD_LOCATION, sizeof(UTL_SSDP_FIELD_LOCATION) - 1));
        has_server = ReadUniqueField(&candidate.server,
                                     head,
                                     Text((const byte_t*)UTL_SSDP_FIELD_SERVER, sizeof(UTL_SSDP_FIELD_SERVER) - 1));
        has_cache =
            ReadUniqueField(&cache, head, Text((const byte_t*)UTL_SSDP_FIELD_CACHE, sizeof(UTL_SSDP_FIELD_CACHE) - 1));
        has_extension =
            ReadUniqueField(&extension, head, Text((const byte_t*)UTL_SSDP_FIELD_EXT, sizeof(UTL_SSDP_FIELD_EXT) - 1));
        has_identity    = has_target && has_usn;
        has_description = has_location && has_server;
        has_lifetime    = has_cache && has_extension;
        is_valid        = has_identity && has_description && has_lifetime;
    }
    if (is_valid)
    {
        valid_target = Utl_Ssdp_IsTarget(candidate.target);
        is_all =
            Utl_Http_IsTextEqual(candidate.target, Text((const byte_t*)UTL_SSDP_ALL, sizeof(UTL_SSDP_ALL) - 1), FALSE);
        valid_description = IsVisible(candidate.location) && (0 < candidate.server.byteCount);
        valid_extension   = (0 == extension.byteCount);
        is_valid          = valid_target && (!is_all) && valid_description && valid_extension;
    }
    if (is_valid)
    {
        is_valid = ReadIdentity(&candidate.uuid, candidate.usn, candidate.target);
    }
    if (is_valid)
    {
        is_valid = ReadMaximumAge(&candidate.maximumAge, cache);
    }
    if (is_valid)
    {
        (*response) = candidate;
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The writer first validates and measures the complete head. It never appends a
 *  C terminator or partially publishes a datagram on capacity failure.
 */
uint32_t Utl_Ssdp_WriteSearch(const Utl_Ssdp_Search_t* search, byte_t* buffer, uint32_t capacity)
{
    uint32_t         required;
    bool_t           fits;
    uint32_t         written;
    Utl_Http_Head_t  head;
    Utl_Http_Field_t fields[UTL_SSDP_SEARCH_FIELD_COUNT];
    byte_t           wait_text[UTL_SSDP_WAIT_DIGITS];

    LOGASSERT(NULL != search);
    LOGASSERT(NULL != buffer);

    required = Utl_Ssdp_GetSearchByteCount(search);
    fits     = (0 < required) && (required <= capacity);
    written  = 0;
    if (fits)
    {
        InitializeSearchHead(&head, fields, wait_text, search);
        written = Utl_Http_WriteHead(&head, buffer, capacity);
    }

    return written;
}
