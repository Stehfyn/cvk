/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * ISO/IEC 14496-1:2010, 7.2.2.2, 7.2.6.5, 7.2.6.6 and 8.3.3:
 * expandable descriptors, elementary-stream metadata and decoder configuration.
 * https://www.iso.org/standard/55688.html
 * Object and stream code-point registry: https://mp4ra.org/registered-types/object-types
 * Descriptor payloads for codecs and optional extensions remain counted borrows;
 * their codec-specific semantics belong to the corresponding specification module.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_mpeg4.h"
#include "utl/utl_mpeg4systems.h"
#include "utl/utl_unicode.h"
#include "crt/crt_memory.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include <stdint.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define LENGTH_BITS         (7)
#define LENGTH_MASK         (0x7F)
#define LENGTH_MORE         (0x80)
#define LENGTH_MAX_BYTES    (4)
#define MIN_PREFIX_BYTES    (2)
#define INTEGER_BYTES       (4)
#define IDENTIFIER_BYTES    (2)
#define BUFFER_BYTES        (3)
#define BUFFER_MASK         (0xFFFFFF)
#define DECODER_FIXED_BYTES (13)
#define STREAM_MASK         (0x3F)
#define STREAM_SHIFT        (2)
#define STREAM_UPSTREAM     (0x02)
#define STREAM_RESERVED     (0x01)
#define ES_FIXED_BYTES      (3)
#define ES_DEPENDENCY       (0x80)
#define ES_URL              (0x40)
#define ES_CLOCK            (0x20)
#define PRIORITY_MASK       (0x1F)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Bounded input cursor shared by optional descriptor fields. */
typedef struct DescriptorCursor_s
{
    Utl_Mpeg4_Span_t bytes;  ///< Stable enclosing body.
    uint32_t         offset; ///< Next field, always within bytes.
} DescriptorCursor_t;

/*************************************************************************************************/
/** \brief Complete descriptor prefix model for preflighted output. */
typedef struct DescriptorPrefix_s
{
    uint8_t  tag;          ///< Valid descriptor tag.
    uint32_t payloadBytes; ///< Body extent within the specification limit.
} DescriptorPrefix_t;

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
 * \brief Checks a counted storage precondition.
 *
 * \par Details:
 *  Storage precondition checked.
 *
 * \par Requirements:
 *  - 001 A nonempty span shall have readable storage; misuse shall assert.
 *
 * \param[in] span   Counted borrowed bytes
 *
 * \retval None
 */
static FORCE_INLINE void AssertSpan(Utl_Mpeg4_Span_t span);

/*************************************************************************************************/
/**
 * \brief Derives the shortest expandable-length width.
 *
 * \par Requirements:
 *  - 001 The payload shall fit the BaseDescriptor limit.
 *
 * \param[in] count   Body extent
 *
 * \retval 1..4 Length bytes.
 */
static FORCE_INLINE uint32_t GetLengthByteCount(uint32_t count);

/*************************************************************************************************/
/**
 * \brief Determines whether a tag belongs to the descriptor code-point space.
 *
 * \par Requirements:
 *  - 001 Reserved future and private tags shall remain representable; forbidden tags shall fail.
 *
 * \param[in] tag   Descriptor code point
 *
 * \retval TRUE Permitted tag.
 * \retval FALSE Forbidden tag.
 */
static FORCE_INLINE bool_t IsTag(uint8_t tag);

/*************************************************************************************************/
/**
 * \brief Validates a complete component sequence and singleton multiplicity.
 *
 * \par Requirements:
 *  - 001 Every component shall fit its parent; unknown descriptors shall be skipped by their encoded extent.
 *  - 002 A nonzero singleton tag shall occur at most once; a zero selector shall impose no multiplicity constraint.
 *
 * \param[in] bytes       Complete component bytes
 * \param[in] singleton   Optional singleton tag
 *
 * \retval TRUE Complete sequence without a duplicate singleton.
 * \retval FALSE Truncated, malformed or duplicate component.
 */
static FORCE_INLINE bool_t IsSequence(Utl_Mpeg4_Span_t bytes, uint8_t singleton);

/*************************************************************************************************/
/**
 * \brief Validates mandatory elementary-stream component descriptors.
 *
 * \par Requirements:
 *  - 001 Exactly one decoder configuration and one nonempty SL configuration shall be present.
 *  - 002 The decoder's typed fields and complete children shall be validated.
 *  - 003 Codec data, SL profile data and unknown extensions shall remain opaque to this structural layer.
 *
 * \param[in] children   Complete component sequence
 *
 * \retval TRUE Mandatory components and all enclosing extents are valid.
 * \retval FALSE Missing, duplicate, malformed or truncated component.
 */
static FORCE_INLINE bool_t IsElementaryChildren(Utl_Mpeg4_Span_t children);

/*************************************************************************************************/
/**
 * \brief Reads an unsigned big-endian field from a bounded cursor.
 *
 * \par Requirements:
 *  - 001 The initialized cursor shall be within its span; width shall be one through four.
 *  - 002 Short input shall preserve value and cursor.
 *
 * \param[out]    value    Required integer destination
 * \param[in,out] cursor   Required initialized input cursor
 * \param[in]     width    Field byte width
 *
 * \retval TRUE Field read.
 * \retval FALSE Field truncated.
 */
static FORCE_INLINE bool_t ReadInteger(uint32_t* value, DescriptorCursor_t* cursor, uint32_t width);

/*************************************************************************************************/
/**
 * \brief Reads an ES descriptor's optional counted URL field.
 *
 * \par Requirements:
 *  - 001 The initialized cursor shall remain within the parent body.
 *  - 002 Truncation shall preserve both output and cursor; Unicode validation belongs to model validation.
 *
 * \param[out]    url      Required counted result
 * \param[in,out] cursor   Required initialized input cursor
 *
 * \retval TRUE Complete URL field published.
 * \retval FALSE Truncated field.
 */
static FORCE_INLINE bool_t ReadUrl(Utl_Mpeg4_Span_t* url, DescriptorCursor_t* cursor);

/*************************************************************************************************/
/**
 * \brief Writes a validated big-endian field into preflighted output.
 *
 * \par Details:
 *  Field and cursor written.
 *
 * \par Requirements:
 *  - 001 Width shall be one through four and the caller shall reserve that many writable bytes.
 *  - 002 The value shall fit the selected field width.
 *
 * \param[out]    output   Preflighted output bytes
 * \param[in,out] cursor   Required output offset
 * \param[in]     field    Field value and byte width
 *
 * \retval None
 */
static FORCE_INLINE void WriteInteger(byte_t* output, uint32_t* cursor, Utl_Mpeg4_Integer_t field);

/*************************************************************************************************/
/**
 * \brief Writes a shortest descriptor prefix after complete preflight.
 *
 * \par Requirements:
 *  - 001 Tag and body count shall already be valid and output shall hold the complete prefix.
 *
 * \param[out] output   Writable prefix
 * \param[in]  prefix   Required complete prefix model
 *
 * \retval 2..5 Prefix byte count.
 */
static FORCE_INLINE uint32_t WritePrefix(byte_t* output, const DescriptorPrefix_t* prefix);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void AssertSpan(Utl_Mpeg4_Span_t span)
{
    bool_t has_data;
    bool_t empty;
    bool_t valid;

    has_data = (NULL != span.data);
    empty    = (0 == span.byteCount);
    valid    = has_data || empty;
    LOGASSERT(valid);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetLengthByteCount(uint32_t count)
{
    uint32_t width;

    LOGASSERT(count <= UTL_MPEG4SYSTEMS_MAX_PAYLOAD);
    width = 1;
    for (;;)
    {
        if (count <= LENGTH_MASK)
        {
            break;
        }
        count >>= LENGTH_BITS;
        ++width;
    }
    return width;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsTag(uint8_t tag)
{
    bool_t nonzero;
    bool_t below_forbidden;

    nonzero         = (0 < tag);
    below_forbidden = (tag < UINT8_MAX);
    return nonzero && below_forbidden;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Progress is at least the tag and one length byte per successful component.
 */
static FORCE_INLINE bool_t IsSequence(Utl_Mpeg4_Span_t bytes, uint8_t singleton)
{
    uint32_t                      offset;
    bool_t                        valid;
    bool_t                        seen;
    Utl_Mpeg4_Span_t              remaining;
    Utl_Mpeg4Systems_Descriptor_t child;
    bool_t                        selected;

    AssertSpan(bytes);
    offset = 0;
    valid  = TRUE;
    seen   = FALSE;
    for (;;)
    {
        if (offset == bytes.byteCount)
        {
            break;
        }
        remaining.data      = &bytes.data[offset];
        remaining.byteCount = bytes.byteCount - offset;
        valid               = Utl_Mpeg4Systems_ReadDescriptor(&child, remaining);
        if (!valid)
        {
            break;
        }
        selected = (singleton == child.tag);
        if (selected)
        {
            valid = !seen;
            seen  = TRUE;
        }
        if (!valid)
        {
            break;
        }
        offset += child.encodedByteCount;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Component lookup also validates trailing unknown descriptors. The returned
 *  decoder borrows only its enclosing body's bytes, never a sibling's bytes.
 */
static FORCE_INLINE bool_t IsElementaryChildren(Utl_Mpeg4_Span_t children)
{
    bool_t                           valid;
    Utl_Mpeg4Systems_Descriptor_t    descriptor;
    Utl_Mpeg4Systems_DecoderConfig_t config;

    valid = Utl_Mpeg4Systems_FindDescriptor(&descriptor, children, UTL_MPEG4SYSTEMS_TAG_DECODER_CONFIG);
    if (valid)
    {
        valid = Utl_Mpeg4Systems_ReadDecoderConfig(&config, &descriptor);
    }
    if (valid)
    {
        valid = Utl_Mpeg4Systems_FindDescriptor(&descriptor, children, UTL_MPEG4SYSTEMS_TAG_SL_CONFIG);
    }
    if (valid)
    {
        valid = (0 < descriptor.payload.byteCount);
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The remaining extent is derived by subtraction before advancing the cursor.
 */
static FORCE_INLINE bool_t ReadInteger(uint32_t* value, DescriptorCursor_t* cursor, uint32_t width)
{
    bool_t           fits;
    Utl_Mpeg4_Span_t bytes;
    bool_t           read;
    uint64_t         candidate;

    LOGASSERT(NULL != value);
    LOGASSERT(NULL != cursor);
    LOGASSERT(cursor->offset <= cursor->bytes.byteCount);
    LOGASSERT(0 < width);
    LOGASSERT(width <= INTEGER_BYTES);
    fits = (width <= cursor->bytes.byteCount - cursor->offset);
    if (fits)
    {
        bytes.data      = &cursor->bytes.data[cursor->offset];
        bytes.byteCount = width;
        read            = Utl_Mpeg4_ReadUnsigned(&candidate, bytes);
        LOGASSERT(read);
        cursor->offset += width;
        *value         = (uint32_t)candidate;
    }
    return fits;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The local cursor prevents publication of a consumed length without its URL bytes.
 */
static FORCE_INLINE bool_t ReadUrl(Utl_Mpeg4_Span_t* url, DescriptorCursor_t* cursor)
{
    DescriptorCursor_t candidate;
    bool_t             valid;
    uint32_t           length;

    candidate = *cursor;
    valid     = ReadInteger(&length, &candidate, 1);
    if (valid)
    {
        valid = (length <= candidate.bytes.byteCount - candidate.offset);
        if (valid)
        {
            url->data        = &candidate.bytes.data[candidate.offset];
            url->byteCount   = length;
            candidate.offset += length;
            *cursor          = candidate;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteInteger(byte_t* output, uint32_t* cursor, Utl_Mpeg4_Integer_t field)
{
    uint32_t count;

    LOGASSERT(NULL != output);
    LOGASSERT(NULL != cursor);
    LOGASSERT(0 < field.byteCount);
    LOGASSERT(field.byteCount <= INTEGER_BYTES);
    count = Utl_Mpeg4_WriteUnsigned(field, &output[*cursor], field.byteCount);
    LOGASSERT(field.byteCount == count);
    *cursor += count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t WritePrefix(byte_t* output, const DescriptorPrefix_t* prefix)
{
    uint32_t width;
    uint32_t index;
    uint32_t shift;
    byte_t   group;

    width     = GetLengthByteCount(prefix->payloadBytes);
    output[0] = prefix->tag;
    for (index = 0; index < width; ++index)
    {
        shift = (width - index - 1) * LENGTH_BITS;
        group = (byte_t)((prefix->payloadBytes >> shift) & LENGTH_MASK);
        if (index + 1 < width)
        {
            group |= LENGTH_MORE;
        }
        output[index + 1] = group;
    }
    return width + 1;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4Systems_GetDescriptorByteCount(const Utl_Mpeg4Systems_Descriptor_t* descriptor)
{
    bool_t   valid_tag;
    bool_t   valid_size;
    bool_t   valid;
    uint32_t count;

    LOGASSERT(NULL != descriptor);
    AssertSpan(descriptor->payload);
    valid_tag  = IsTag(descriptor->tag);
    valid_size = (descriptor->payload.byteCount <= UTL_MPEG4SYSTEMS_MAX_PAYLOAD);
    valid      = valid_tag && valid_size;
    count      = 0;
    if (valid)
    {
        count = 1 + GetLengthByteCount(descriptor->payload.byteCount) + descriptor->payload.byteCount;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The reserved stream bit is generated by the writer, not supplied by the caller.
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4Systems_GetDecoderConfigByteCount(const Utl_Mpeg4Systems_DecoderConfig_t* config)
{
    bool_t   has_object;
    bool_t   has_stream;
    bool_t   stream_fits;
    bool_t   buffer_fits;
    bool_t   children_fit;
    bool_t   valid;
    uint32_t count;
    uint32_t body;

    LOGASSERT(NULL != config);
    AssertSpan(config->children);
    has_object   = (0 < config->objectType);
    has_stream   = (0 < config->streamType);
    stream_fits  = (config->streamType <= STREAM_MASK);
    buffer_fits  = (config->bufferBytes <= BUFFER_MASK);
    children_fit = (config->children.byteCount <= UTL_MPEG4SYSTEMS_MAX_PAYLOAD - DECODER_FIXED_BYTES);
    valid        = has_object && has_stream && stream_fits && buffer_fits && children_fit;
    count        = 0;
    if (valid)
    {
        valid = IsSequence(config->children, UTL_MPEG4SYSTEMS_TAG_DECODER_INFO);
        if (valid)
        {
            body  = DECODER_FIXED_BYTES + config->children.byteCount;
            count = 1 + GetLengthByteCount(body) + body;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Component bytes remain borrowed so unknown extensions and original ordering are preserved.
 */
uint32_t Utl_Mpeg4Systems_GetElementaryStreamByteCount(const Utl_Mpeg4Systems_ElementaryStream_t* stream)
{
    bool_t   valid_priority;
    bool_t   valid_url;
    uint32_t fixed;
    bool_t   fits;
    bool_t   valid;
    uint32_t count;
    uint32_t body;

    LOGASSERT(NULL != stream);
    AssertSpan(stream->children);
    valid_priority = (stream->priority <= PRIORITY_MASK);
    valid_url      = TRUE;
    fixed          = ES_FIXED_BYTES;
    if (stream->hasDependency)
    {
        fixed += IDENTIFIER_BYTES;
    }
    if (stream->hasUrl)
    {
        AssertSpan(stream->url);
        valid_url = (stream->url.byteCount <= UINT8_MAX);
        if (valid_url)
        {
            valid_url = Utl_Unicode_IsValidUtf8(stream->url.data, stream->url.byteCount);
            fixed     += 1 + stream->url.byteCount;
        }
    }
    if (stream->hasClock)
    {
        fixed += IDENTIFIER_BYTES;
    }
    fits  = (stream->children.byteCount <= UTL_MPEG4SYSTEMS_MAX_PAYLOAD - fixed);
    valid = valid_priority && valid_url && fits;
    count = 0;
    if (valid)
    {
        valid = IsElementaryChildren(stream->children);
        if (valid)
        {
            body  = fixed + stream->children.byteCount;
            count = 1 + GetLengthByteCount(body) + body;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Unknown components advance by their complete encoded extent without interpreting their payload.
 */
extern FORCE_INLINE bool_t Utl_Mpeg4Systems_FindDescriptor(Utl_Mpeg4Systems_Descriptor_t* descriptor,
                                       Utl_Mpeg4_Span_t               sequence,
                                       uint8_t                        tag)
{
    Utl_Mpeg4Systems_Descriptor_t candidate;
    uint32_t                      offset;
    bool_t                        valid;
    bool_t                        found;
    Utl_Mpeg4_Span_t              remaining;
    Utl_Mpeg4Systems_Descriptor_t child;
    bool_t                        matches;

    LOGASSERT(NULL != descriptor);
    LOGASSERT(IsTag(tag));
    AssertSpan(sequence);
    candidate.tag               = 0;
    candidate.payload.data      = NULL;
    candidate.payload.byteCount = 0;
    candidate.encodedByteCount  = 0;
    offset                      = 0;
    valid                       = TRUE;
    found                       = FALSE;
    for (;;)
    {
        if (offset == sequence.byteCount)
        {
            break;
        }
        remaining.data      = &sequence.data[offset];
        remaining.byteCount = sequence.byteCount - offset;
        valid               = Utl_Mpeg4Systems_ReadDescriptor(&child, remaining);
        if (!valid)
        {
            break;
        }
        matches = (tag == child.tag);
        if (matches)
        {
            valid     = !found;
            candidate = child;
            found     = TRUE;
        }
        if (!valid)
        {
            break;
        }
        offset += child.encodedByteCount;
    }
    valid = valid && found;
    if (valid)
    {
        *descriptor = candidate;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The output count retains the original prefix width so sibling traversal never assumes shortest encoding.
 */
extern FORCE_INLINE bool_t Utl_Mpeg4Systems_ReadDescriptor(Utl_Mpeg4Systems_Descriptor_t* descriptor, Utl_Mpeg4_Span_t bytes)
{
    bool_t                        has_prefix;
    bool_t                        valid;
    Utl_Mpeg4Systems_Descriptor_t candidate;
    uint32_t                      offset;
    uint32_t                      length;
    bool_t                        complete;
    uint32_t                      groups;
    byte_t                        group;
    bool_t                        fits;

    LOGASSERT(NULL != descriptor);
    AssertSpan(bytes);
    has_prefix = (MIN_PREFIX_BYTES <= bytes.byteCount);
    valid      = FALSE;
    if (has_prefix)
    {
        candidate.tag = bytes.data[0];
        offset        = 1;
        length        = 0;
        complete      = FALSE;
        for (groups = 0; groups < LENGTH_MAX_BYTES; ++groups)
        {
            if (offset == bytes.byteCount)
            {
                break;
            }
            group = bytes.data[offset];
            ++offset;
            length = (length << LENGTH_BITS) | (group & LENGTH_MASK);
            if (!(group & LENGTH_MORE))
            {
                complete = TRUE;
                break;
            }
        }
        fits  = (length <= bytes.byteCount - offset);
        valid = complete && fits;
        if (valid)
        {
            valid = IsTag(candidate.tag);
            if (valid)
            {
                candidate.payload.data      = &bytes.data[offset];
                candidate.payload.byteCount = length;
                candidate.encodedByteCount  = offset + length;
                *descriptor                 = candidate;
            }
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Fixed-width fields are loaded only after their complete thirteen-byte extent is established.
 */
extern FORCE_INLINE bool_t Utl_Mpeg4Systems_ReadDecoderConfig(Utl_Mpeg4Systems_DecoderConfig_t*    config,
                                          const Utl_Mpeg4Systems_Descriptor_t* descriptor)
{
    bool_t                           correct_tag;
    bool_t                           has_fixed;
    bool_t                           valid;
    Utl_Mpeg4Systems_DecoderConfig_t candidate;
    byte_t                           flags;
    DescriptorCursor_t               cursor;
    bool_t                           read;
    uint32_t                         value;
    uint32_t                         count;

    LOGASSERT(NULL != config);
    LOGASSERT(NULL != descriptor);
    AssertSpan(descriptor->payload);
    correct_tag = (UTL_MPEG4SYSTEMS_TAG_DECODER_CONFIG == descriptor->tag);
    has_fixed   = (DECODER_FIXED_BYTES <= descriptor->payload.byteCount);
    valid       = correct_tag && has_fixed;
    if (valid)
    {
        candidate.objectType = descriptor->payload.data[0];
        flags                = descriptor->payload.data[1];
        candidate.streamType = (uint8_t)(flags >> STREAM_SHIFT);
        candidate.upstream   = (flags & STREAM_UPSTREAM) ? TRUE : FALSE;
        cursor.bytes         = descriptor->payload;
        cursor.offset        = 2;
        read                 = ReadInteger(&value, &cursor, BUFFER_BYTES);
        LOGASSERT(read);
        candidate.bufferBytes = value;
        read                  = ReadInteger(&value, &cursor, INTEGER_BYTES);
        LOGASSERT(read);
        candidate.maximumBitrate = value;
        read                     = ReadInteger(&value, &cursor, INTEGER_BYTES);
        LOGASSERT(read);
        candidate.averageBitrate     = value;
        candidate.children.data      = &descriptor->payload.data[cursor.offset];
        candidate.children.byteCount = descriptor->payload.byteCount - cursor.offset;
        valid                        = (flags & STREAM_RESERVED) ? TRUE : FALSE;
        if (valid)
        {
            count = Utl_Mpeg4Systems_GetDecoderConfigByteCount(&candidate);
            valid = (0 < count);
            if (valid)
            {
                *config = candidate;
            }
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  All optional fields receive deterministic inactive values; only a complete candidate is published.
 */
extern FORCE_INLINE bool_t Utl_Mpeg4Systems_ReadElementaryStream(Utl_Mpeg4Systems_ElementaryStream_t* stream,
                                             const Utl_Mpeg4Systems_Descriptor_t* descriptor)
{
    bool_t                              correct_tag;
    bool_t                              has_fixed;
    bool_t                              valid;
    DescriptorCursor_t                  cursor;
    uint32_t                            value;
    Utl_Mpeg4Systems_ElementaryStream_t candidate;
    byte_t                              flags;
    uint32_t                            count;

    LOGASSERT(NULL != stream);
    LOGASSERT(NULL != descriptor);
    AssertSpan(descriptor->payload);
    correct_tag = (UTL_MPEG4SYSTEMS_TAG_ES == descriptor->tag);
    has_fixed   = (ES_FIXED_BYTES <= descriptor->payload.byteCount);
    valid       = correct_tag && has_fixed;
    if (valid)
    {
        cursor.bytes  = descriptor->payload;
        cursor.offset = 0;
        valid         = ReadInteger(&value, &cursor, IDENTIFIER_BYTES);
        LOGASSERT(valid);
        candidate.streamId = (uint16_t)value;
        flags              = descriptor->payload.data[cursor.offset];
        ++cursor.offset;
        candidate.priority      = flags & PRIORITY_MASK;
        candidate.hasDependency = (flags & ES_DEPENDENCY) ? TRUE : FALSE;
        candidate.dependsOn     = 0;
        candidate.hasUrl        = (flags & ES_URL) ? TRUE : FALSE;
        candidate.url.data      = NULL;
        candidate.url.byteCount = 0;
        candidate.hasClock      = (flags & ES_CLOCK) ? TRUE : FALSE;
        candidate.clockId       = 0;
        if (candidate.hasDependency)
        {
            valid               = ReadInteger(&value, &cursor, IDENTIFIER_BYTES);
            candidate.dependsOn = (uint16_t)value;
        }
        if (valid && candidate.hasUrl)
        {
            valid = ReadUrl(&candidate.url, &cursor);
        }
        if (valid && candidate.hasClock)
        {
            valid             = ReadInteger(&value, &cursor, IDENTIFIER_BYTES);
            candidate.clockId = (uint16_t)value;
        }
        if (valid)
        {
            candidate.children.data      = &descriptor->payload.data[cursor.offset];
            candidate.children.byteCount = descriptor->payload.byteCount - cursor.offset;
            count                        = Utl_Mpeg4Systems_GetElementaryStreamByteCount(&candidate);
            valid                        = (0 < count);
            if (valid)
            {
                *stream = candidate;
            }
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4Systems_WriteDescriptor(const Utl_Mpeg4Systems_Descriptor_t* descriptor,
                                          byte_t*                              output,
                                          uint32_t                             capacity)
{
    uint32_t           count;
    bool_t             valid;
    bool_t             fits;
    bool_t             can_write;
    uint32_t           written;
    DescriptorPrefix_t prefix;
    uint32_t           cursor;

    LOGASSERT(NULL != output);
    count     = Utl_Mpeg4Systems_GetDescriptorByteCount(descriptor);
    valid     = (0 < count);
    fits      = (count <= capacity);
    can_write = valid && fits;
    written   = 0;
    if (can_write)
    {
        prefix.tag          = descriptor->tag;
        prefix.payloadBytes = descriptor->payload.byteCount;
        cursor              = WritePrefix(output, &prefix);
        if (0 < descriptor->payload.byteCount)
        {
            CRT_Memory_Copy(&output[cursor], descriptor->payload.data, descriptor->payload.byteCount);
        }
        written = count;
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The reserved bit is always one. Size queries and writing share the same complete validation.
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4Systems_WriteDecoderConfig(const Utl_Mpeg4Systems_DecoderConfig_t* config,
                                             byte_t*                                 output,
                                             uint32_t                                capacity)
{
    uint32_t            count;
    bool_t              valid;
    bool_t              fits;
    bool_t              can_write;
    uint32_t            written;
    DescriptorPrefix_t  prefix;
    uint32_t            cursor;
    Utl_Mpeg4_Integer_t field;
    uint32_t            flags;

    LOGASSERT(NULL != output);
    count     = Utl_Mpeg4Systems_GetDecoderConfigByteCount(config);
    valid     = (0 < count);
    fits      = (count <= capacity);
    can_write = valid && fits;
    written   = 0;
    if (can_write)
    {
        prefix.tag          = UTL_MPEG4SYSTEMS_TAG_DECODER_CONFIG;
        prefix.payloadBytes = DECODER_FIXED_BYTES + config->children.byteCount;
        cursor              = WritePrefix(output, &prefix);
        field.value         = config->objectType;
        field.byteCount     = 1;
        WriteInteger(output, &cursor, field);
        flags = ((uint32_t)config->streamType << STREAM_SHIFT) | STREAM_RESERVED;
        if (config->upstream)
        {
            flags |= STREAM_UPSTREAM;
        }
        field.value     = flags;
        field.byteCount = 1;
        WriteInteger(output, &cursor, field);
        field.value     = config->bufferBytes;
        field.byteCount = BUFFER_BYTES;
        WriteInteger(output, &cursor, field);
        field.value     = config->maximumBitrate;
        field.byteCount = INTEGER_BYTES;
        WriteInteger(output, &cursor, field);
        field.value     = config->averageBitrate;
        field.byteCount = INTEGER_BYTES;
        WriteInteger(output, &cursor, field);
        if (0 < config->children.byteCount)
        {
            CRT_Memory_Copy(&output[cursor], config->children.data, config->children.byteCount);
        }
        written = count;
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Optional fields follow flag order; inactive optional storage is never inspected.
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4Systems_WriteElementaryStream(const Utl_Mpeg4Systems_ElementaryStream_t* stream,
                                                byte_t*                                    output,
                                                uint32_t                                   capacity)
{
    uint32_t            count;
    bool_t              valid;
    bool_t              fits;
    bool_t              can_write;
    uint32_t            written;
    uint32_t            body;
    uint32_t            flags;
    DescriptorPrefix_t  prefix;
    uint32_t            cursor;
    Utl_Mpeg4_Integer_t field;

    LOGASSERT(NULL != output);
    count     = Utl_Mpeg4Systems_GetElementaryStreamByteCount(stream);
    valid     = (0 < count);
    fits      = (count <= capacity);
    can_write = valid && fits;
    written   = 0;
    if (can_write)
    {
        body  = ES_FIXED_BYTES + stream->children.byteCount;
        flags = stream->priority;
        if (stream->hasDependency)
        {
            body  += IDENTIFIER_BYTES;
            flags |= ES_DEPENDENCY;
        }
        if (stream->hasUrl)
        {
            body  += 1 + stream->url.byteCount;
            flags |= ES_URL;
        }
        if (stream->hasClock)
        {
            body  += IDENTIFIER_BYTES;
            flags |= ES_CLOCK;
        }
        prefix.tag          = UTL_MPEG4SYSTEMS_TAG_ES;
        prefix.payloadBytes = body;
        cursor              = WritePrefix(output, &prefix);
        field.value         = stream->streamId;
        field.byteCount     = IDENTIFIER_BYTES;
        WriteInteger(output, &cursor, field);
        field.value     = flags;
        field.byteCount = 1;
        WriteInteger(output, &cursor, field);
        if (stream->hasDependency)
        {
            field.value     = stream->dependsOn;
            field.byteCount = IDENTIFIER_BYTES;
            WriteInteger(output, &cursor, field);
        }
        if (stream->hasUrl)
        {
            field.value     = stream->url.byteCount;
            field.byteCount = 1;
            WriteInteger(output, &cursor, field);
            if (0 < stream->url.byteCount)
            {
                CRT_Memory_Copy(&output[cursor], stream->url.data, stream->url.byteCount);
                cursor += stream->url.byteCount;
            }
        }
        if (stream->hasClock)
        {
            field.value     = stream->clockId;
            field.byteCount = IDENTIFIER_BYTES;
            WriteInteger(output, &cursor, field);
        }
        if (0 < stream->children.byteCount)
        {
            CRT_Memory_Copy(&output[cursor], stream->children.data, stream->children.byteCount);
        }
        written = count;
    }
    return written;
}
