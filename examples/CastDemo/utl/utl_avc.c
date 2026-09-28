/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * ISO/IEC 14496-15:2010, 5.2.3, 5.2.4 and 5.3.4: AVC configuration and NAL framing.
 * https://www.iso.org/standard/55980.html
 * https://github.com/MPEGGroup/CMAF/issues/10 documents legacy omitted high fields.
 * Configuration NAL arrays and future trailing bytes are preserved as borrows.
 * SPS/PPS/slice payload semantics and later profile revisions remain separate work.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_mpeg4.h"
#include "utl/utl_avc.h"
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

#define CONFIG_VERSION          (1)
#define CONFIG_PREFIX_BYTES     (6)
#define CONFIG_FIXED_BYTES      (7)
#define HIGH_FIXED_BYTES        (4)
#define PARAMETER_LENGTH_BYTES  (2)
#define BYTE_BITS               (8)
#define BYTE_MASK               (0xFF)
#define LENGTH_ONE              (1)
#define LENGTH_TWO              (2)
#define LENGTH_FOUR             (4)
#define LENGTH_MASK             (0x03)
#define LENGTH_RESERVED         (0xFC)
#define SEQUENCE_MASK           (0x1F)
#define SEQUENCE_RESERVED       (0xE0)
#define NAL_TYPE_MASK           (0x1F)
#define FORBIDDEN_BIT           (0x80)
#define PROFILE_HIGH            (100)
#define PROFILE_HIGH_TEN        (110)
#define PROFILE_HIGH_422        (122)
#define PROFILE_HIGH_444_LEGACY (144)
#define CHROMA_MASK             (0x03)
#define DEPTH_MASK              (0x07)
#define DEPTH_RESERVED          (0xF8)
#define DEPTH_MIN               (8)
#define DEPTH_MAX               (12)
#define VERSION_OFFSET          (0)
#define PROFILE_OFFSET          (1)
#define COMPATIBILITY_OFFSET    (2)
#define LEVEL_OFFSET            (3)
#define LENGTH_OFFSET           (4)
#define SEQUENCE_OFFSET         (5)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Bounded record cursor; valid is sticky after truncation. */
typedef struct AvcCursor_s
{
    Utl_Mpeg4_Span_t bytes;  ///< Stable original record.
    uint32_t         offset; ///< Next byte, always inside the original extent.
    bool_t           valid;  ///< FALSE after a field would exceed the extent.
} AvcCursor_t;

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
 * \brief Checks counted storage preconditions.
 *
 * \par Requirements:
 *  - 001 Nonempty spans shall have stable accessible storage; programmer misuse shall assert.
 *
 * \param[in] bytes   Complete borrowed extent
 *
 * \retval None Preconditions checked.
 */
static FORCE_INLINE void AssertSpan(Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Recognizes the three permitted sample NAL length widths.
 *
 * \par Requirements:
 *  - 001 A three-byte width shall not be accepted.
 *
 * \param[in] width   Byte width
 *
 * \retval TRUE One, two or four.
 * \retval FALSE Forbidden width.
 */
static FORCE_INLINE bool_t IsLengthWidth(uint8_t width);

/*************************************************************************************************/
/**
 * \brief Recognizes the high-field condition in the cited record revision.
 *
 * \par Requirements:
 *  - 001 Later unrecognized profile-specific suffixes shall remain preserved trailing data.
 *
 * \param[in] profile   Configuration profile byte
 *
 * \retval TRUE Modeled high-field syntax.
 * \retval FALSE No modeled high-field suffix.
 */
static FORCE_INLINE bool_t HasHighSyntax(uint8_t profile);

/*************************************************************************************************/
/**
 * \brief Validates every entry and the exact declared extent of an array.
 *
 * \par Requirements:
 *  - 001 The generic count shall fit eight bits; enclosing models impose tighter sequence limits.
 *  - 002 No suffix or extra undeclared entry shall be ignored.
 *
 * \param[in] sets   Required stable array model
 *
 * \retval TRUE Complete valid array.
 * \retval FALSE Invalid count, entry or extent.
 */
static FORCE_INLINE bool_t IsParameterSets(const Utl_Avc_ParameterSets_t* sets);

/*************************************************************************************************/
/**
 * \brief Validates selected high-profile output fields and their array.
 *
 * \par Requirements:
 *  - 001 Writers shall require high fields for the cited high-profile syntax.
 *  - 002 Inactive fields shall not be read; a legacy record must obtain these fields before standard serialization.
 *
 * \param[in] config   Required stable configuration
 *
 * \retval TRUE Consistent complete field set.
 * \retval FALSE Missing or invalid high fields.
 */
static FORCE_INLINE bool_t IsHighFields(const Utl_Avc_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Reads one byte from a bounded record cursor.
 *
 * \par Requirements:
 *  - 001 The cursor shall be initialized over stable storage.
 *  - 002 Truncation shall be sticky and shall not advance or read beyond the input.
 *
 * \param[in,out] cursor   Required initialized cursor
 *
 * \retval value Byte value; zero on sticky failure.
 */
static FORCE_INLINE uint32_t ReadByte(AvcCursor_t* cursor);

/*************************************************************************************************/
/**
 * \brief Reads one nonempty bounded configuration NAL entry.
 *
 * \par Requirements:
 *  - 001 The output shall remain unchanged on invalid header or incomplete body.
 *  - 002 Reserved NAL types shall remain representable; forbidden_zero_bit shall remain zero.
 *
 * \param[out]    parameter   Required borrowed result
 * \param[in,out] cursor      Required initialized record cursor
 *
 * \retval TRUE Complete entry published.
 * \retval FALSE Invalid/truncated entry.
 */
static FORCE_INLINE bool_t ReadEntry(Utl_Mpeg4_Span_t* parameter, AvcCursor_t* cursor);

/*************************************************************************************************/
/**
 * \brief Consumes an entire declared array into an unpublished candidate view.
 *
 * \par Requirements:
 *  - 001 Count shall be width-validated before entry.
 *  - 002 The candidate shall not be published when the cursor reports invalid input.
 *
 * \param[out]    sets     Required candidate view
 * \param[in,out] cursor   Required initialized cursor
 * \param[in]     count    Declared bounded count
 *
 * \retval None Candidate and sticky cursor status updated.
 */
static FORCE_INLINE void ReadParameterSets(Utl_Avc_ParameterSets_t* sets, AvcCursor_t* cursor, uint32_t count);

/*************************************************************************************************/
/**
 * \brief Reads complete high-profile metadata and its sequence-extension array.
 *
 * \par Requirements:
 *  - 001 All reserved bits and depth ranges shall be checked.
 *  - 002 Partial fields shall remain local to the unpublished configuration.
 *
 * \param[in,out] config   Required initialized candidate
 * \param[in,out] cursor   Required initialized cursor
 *
 * \retval None Candidate fields and validity updated.
 */
static FORCE_INLINE void ReadHighFields(Utl_Avc_Config_t* config, AvcCursor_t* cursor);

/*************************************************************************************************/
/**
 * \brief Copies one preflighted complete span.
 *
 * \par Requirements:
 *  - 001 The complete output extent and arithmetic shall have been validated before entry.
 *  - 002 Nonempty input shall be stable and disjoint from output.
 *
 * \param[out]    output     Required preflighted destination
 * \param[in,out] position   Required bounded output cursor
 * \param[in]     bytes      Validated stable span
 *
 * \retval None Bytes copied and cursor advanced.
 */
static FORCE_INLINE void WriteSpan(byte_t* output, uint32_t* position, Utl_Mpeg4_Span_t bytes);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void AssertSpan(Utl_Mpeg4_Span_t bytes)
{
    bool_t has_storage;
    bool_t empty;
    bool_t valid;
    has_storage = (NULL != bytes.data);
    empty       = (0 == bytes.byteCount);
    valid       = has_storage || empty;
    LOGASSERT(valid);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsLengthWidth(uint8_t width)
{
    bool_t one;
    bool_t two;
    bool_t four;
    one  = (LENGTH_ONE == width);
    two  = (LENGTH_TWO == width);
    four = (LENGTH_FOUR == width);
    return one || two || four;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t HasHighSyntax(uint8_t profile)
{
    bool_t high;
    bool_t high_ten;
    bool_t high_422;
    bool_t high_444;
    high     = (PROFILE_HIGH == profile);
    high_ten = (PROFILE_HIGH_TEN == profile);
    high_422 = (PROFILE_HIGH_422 == profile);
    high_444 = (PROFILE_HIGH_444_LEGACY == profile);
    return high || high_ten || high_422 || high_444;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsParameterSets(const Utl_Avc_ParameterSets_t* sets)
{
    AvcCursor_t      cursor;
    bool_t           valid;
    uint32_t         index;
    Utl_Mpeg4_Span_t parameter;
    bool_t           complete;
    LOGASSERT(NULL != sets);
    AssertSpan(sets->bytes);
    cursor.bytes  = sets->bytes;
    cursor.offset = 0;
    cursor.valid  = TRUE;
    valid         = (sets->count <= UTL_AVC_MAX_PICTURE_SETS);
    if (valid)
    {
        for (index = 0; index < sets->count; ++index)
        {
            valid = ReadEntry(&parameter, &cursor);
            if (!valid)
            {
                break;
            }
        }
    }
    complete = (cursor.offset == cursor.bytes.byteCount);
    return valid && complete;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsHighFields(const Utl_Avc_Config_t* config)
{
    bool_t profile_uses_high;
    bool_t valid;
    bool_t chroma_fits;
    bool_t luma_minimum;
    bool_t luma_maximum;
    bool_t chroma_minimum;
    bool_t chroma_maximum;
    bool_t extensions_valid;
    profile_uses_high = HasHighSyntax(config->profile);
    valid             = !profile_uses_high;
    if (config->hasHighFields)
    {
        chroma_fits      = (config->chromaFormat <= CHROMA_MASK);
        luma_minimum     = (DEPTH_MIN <= config->lumaDepth);
        luma_maximum     = (config->lumaDepth <= DEPTH_MAX);
        chroma_minimum   = (DEPTH_MIN <= config->chromaDepth);
        chroma_maximum   = (config->chromaDepth <= DEPTH_MAX);
        extensions_valid = IsParameterSets(&config->extensions);
        valid            = profile_uses_high && chroma_fits && luma_minimum && luma_maximum && chroma_minimum && chroma_maximum &&
                extensions_valid;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t ReadByte(AvcCursor_t* cursor)
{
    bool_t   available;
    bool_t   can_read;
    uint32_t value;
    LOGASSERT(NULL != cursor);
    LOGASSERT(cursor->offset <= cursor->bytes.byteCount);
    available     = (cursor->offset < cursor->bytes.byteCount);
    can_read      = cursor->valid && available;
    value         = 0;
    cursor->valid = can_read;
    if (can_read)
    {
        value = cursor->bytes.data[cursor->offset];
        ++cursor->offset;
    }
    return value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadEntry(Utl_Mpeg4_Span_t* parameter, AvcCursor_t* cursor)
{
    uint32_t         length;
    uint32_t         remaining;
    bool_t           nonempty;
    bool_t           fits;
    bool_t           valid;
    Utl_Mpeg4_Span_t candidate;
    uint8_t          header;
    LOGASSERT(NULL != parameter);
    LOGASSERT(NULL != cursor);
    length    = ReadByte(cursor) << BYTE_BITS;
    length    |= ReadByte(cursor);
    remaining = cursor->bytes.byteCount - cursor->offset;
    nonempty  = (0 < length);
    fits      = (length <= remaining);
    valid     = cursor->valid && nonempty && fits;
    if (valid)
    {
        candidate.data      = &cursor->bytes.data[cursor->offset];
        candidate.byteCount = length;
        header              = candidate.data[0];
        valid               = !(header & FORBIDDEN_BIT);
        if (valid)
        {
            cursor->offset += length;
            *parameter     = candidate;
        }
    }
    cursor->valid = valid;
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void ReadParameterSets(Utl_Avc_ParameterSets_t* sets, AvcCursor_t* cursor, uint32_t count)
{
    uint32_t         begin;
    uint32_t         index;
    bool_t           valid;
    Utl_Mpeg4_Span_t parameter;
    LOGASSERT(NULL != sets);
    LOGASSERT(count <= UTL_AVC_MAX_PICTURE_SETS);
    begin = cursor->offset;
    for (index = 0; index < count; ++index)
    {
        valid = ReadEntry(&parameter, cursor);
        if (!valid)
        {
            break;
        }
    }
    sets->count           = count;
    sets->bytes.data      = NULL;
    sets->bytes.byteCount = cursor->offset - begin;
    if (0 < sets->bytes.byteCount)
    {
        sets->bytes.data = &cursor->bytes.data[begin];
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void ReadHighFields(Utl_Avc_Config_t* config, AvcCursor_t* cursor)
{
    uint32_t chroma;
    uint32_t luma_depth;
    uint32_t chroma_depth;
    uint32_t count;
    bool_t   chroma_reserved;
    bool_t   luma_reserved;
    bool_t   depth_reserved;
    bool_t   valid;
    chroma                = ReadByte(cursor);
    luma_depth            = ReadByte(cursor);
    chroma_depth          = ReadByte(cursor);
    count                 = ReadByte(cursor);
    chroma_reserved       = (LENGTH_RESERVED == (chroma & LENGTH_RESERVED));
    luma_reserved         = (DEPTH_RESERVED == (luma_depth & DEPTH_RESERVED));
    depth_reserved        = (DEPTH_RESERVED == (chroma_depth & DEPTH_RESERVED));
    valid                 = cursor->valid && chroma_reserved && luma_reserved && depth_reserved;
    cursor->valid         = valid;
    config->hasHighFields = TRUE;
    config->chromaFormat  = (uint8_t)(chroma & CHROMA_MASK);
    config->lumaDepth     = (uint8_t)(DEPTH_MIN + (luma_depth & DEPTH_MASK));
    config->chromaDepth   = (uint8_t)(DEPTH_MIN + (chroma_depth & DEPTH_MASK));
    if (valid)
    {
        ReadParameterSets(&config->extensions, cursor, count);
        valid         = IsHighFields(config);
        cursor->valid = cursor->valid && valid;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteSpan(byte_t* output, uint32_t* position, Utl_Mpeg4_Span_t bytes)
{
    LOGASSERT(NULL != output);
    LOGASSERT(NULL != position);
    if (0 < bytes.byteCount)
    {
        CRT_Memory_Copy(&output[*position], bytes.data, bytes.byteCount);
        *position += bytes.byteCount;
    }
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Utl_Avc_InitializeConfig(Utl_Avc_Config_t* config)
{
    LOGASSERT(NULL != config);
    config->profile                    = 0;
    config->compatibility              = 0;
    config->level                      = 0;
    config->lengthBytes                = 0;
    config->sequence.bytes.data        = NULL;
    config->sequence.bytes.byteCount   = 0;
    config->sequence.count             = 0;
    config->picture.bytes.data         = NULL;
    config->picture.bytes.byteCount    = 0;
    config->picture.count              = 0;
    config->hasHighFields              = FALSE;
    config->chromaFormat               = 0;
    config->lumaDepth                  = 0;
    config->chromaDepth                = 0;
    config->extensions.bytes.data      = NULL;
    config->extensions.bytes.byteCount = 0;
    config->extensions.count           = 0;
    config->trailing.data              = NULL;
    config->trailing.byteCount         = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Avc_GetParameterByteCount(Utl_Mpeg4_Span_t parameter)
{
    bool_t   nonempty;
    bool_t   fits;
    bool_t   valid;
    uint32_t count;
    AssertSpan(parameter);
    nonempty = (0 < parameter.byteCount);
    fits     = (parameter.byteCount <= UTL_AVC_MAX_PARAMETER_BYTES);
    valid    = nonempty && fits;
    count    = 0;
    if (valid)
    {
        valid = !(parameter.data[0] & FORBIDDEN_BIT);
        if (valid)
        {
            count = PARAMETER_LENGTH_BYTES + parameter.byteCount;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Avc_GetConfigByteCount(const Utl_Avc_Config_t* config)
{
    bool_t   width_valid;
    bool_t   sequence_fits;
    bool_t   sequence_valid;
    bool_t   picture_valid;
    bool_t   high_valid;
    bool_t   valid;
    uint32_t count;
    uint64_t total;
    LOGASSERT(NULL != config);
    AssertSpan(config->trailing);
    width_valid    = IsLengthWidth(config->lengthBytes);
    sequence_fits  = (config->sequence.count <= UTL_AVC_MAX_SEQUENCE_SETS);
    sequence_valid = IsParameterSets(&config->sequence);
    picture_valid  = IsParameterSets(&config->picture);
    high_valid     = IsHighFields(config);
    valid          = width_valid && sequence_fits && sequence_valid && picture_valid && high_valid;
    count          = 0;
    if (valid)
    {
        total = CONFIG_FIXED_BYTES;
        total += config->sequence.bytes.byteCount;
        total += config->picture.bytes.byteCount;
        total += config->trailing.byteCount;
        if (config->hasHighFields)
        {
            total += HIGH_FIXED_BYTES;
            total += config->extensions.bytes.byteCount;
        }
        if (total <= UINT32_MAX)
        {
            count = (uint32_t)total;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Utl_Avc_FindParameter(Utl_Mpeg4_Span_t* parameter, const Utl_Avc_ParameterSets_t* sets, uint8_t nalType)
{
    bool_t           valid;
    bool_t           found;
    Utl_Mpeg4_Span_t selected;
    AvcCursor_t      cursor;
    uint32_t         index;
    Utl_Mpeg4_Span_t candidate;
    bool_t           matches;
    bool_t           select;
    LOGASSERT(NULL != parameter);
    LOGASSERT(NULL != sets);
    LOGASSERT(nalType <= NAL_TYPE_MASK);
    valid              = IsParameterSets(sets);
    found              = FALSE;
    selected.data      = NULL;
    selected.byteCount = 0;
    if (valid)
    {
        cursor.bytes  = sets->bytes;
        cursor.offset = 0;
        cursor.valid  = TRUE;
        for (index = 0; index < sets->count; ++index)
        {
            valid = ReadEntry(&candidate, &cursor);
            LOGASSERT(valid);
            matches = ((candidate.data[0] & NAL_TYPE_MASK) == nalType);
            select  = matches && !found;
            if (select)
            {
                selected = candidate;
                found    = TRUE;
            }
        }
    }
    if (found)
    {
        *parameter = selected;
    }
    return found;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
Utl_Avc_Status_t Utl_Avc_ReadConfig(Utl_Avc_Config_t* config, Utl_Mpeg4_Span_t bytes)
{
    AvcCursor_t      cursor;
    Utl_Avc_Config_t candidate;
    uint32_t         version;
    Utl_Avc_Status_t status;
    uint32_t         length;
    uint32_t         sequence;
    bool_t           length_reserved;
    bool_t           sequence_reserved;
    bool_t           width_valid;
    bool_t           valid;
    uint32_t         picture_count;
    bool_t           high_syntax;
    bool_t           has_suffix;
    LOGASSERT(NULL != config);
    AssertSpan(bytes);
    cursor.bytes  = bytes;
    cursor.offset = 0;
    cursor.valid  = TRUE;
    Utl_Avc_InitializeConfig(&candidate);
    version = ReadByte(&cursor);
    status  = UTL_AVC_UNSUPPORTED_VERSION;
    if (CONFIG_VERSION == version)
    {
        candidate.profile       = (uint8_t)ReadByte(&cursor);
        candidate.compatibility = (uint8_t)ReadByte(&cursor);
        candidate.level         = (uint8_t)ReadByte(&cursor);
        length                  = ReadByte(&cursor);
        sequence                = ReadByte(&cursor);
        candidate.lengthBytes   = (uint8_t)((length & LENGTH_MASK) + 1);
        length_reserved         = (LENGTH_RESERVED == (length & LENGTH_RESERVED));
        sequence_reserved       = (SEQUENCE_RESERVED == (sequence & SEQUENCE_RESERVED));
        width_valid             = IsLengthWidth(candidate.lengthBytes);
        valid                   = cursor.valid && length_reserved && sequence_reserved && width_valid;
        cursor.valid            = valid;
        if (valid)
        {
            ReadParameterSets(&candidate.sequence, &cursor, sequence & SEQUENCE_MASK);
            picture_count = ReadByte(&cursor);
            if (cursor.valid)
            {
                ReadParameterSets(&candidate.picture, &cursor, picture_count);
            }
        }
        status      = UTL_AVC_COMPLETE;
        high_syntax = HasHighSyntax(candidate.profile);
        has_suffix  = (cursor.offset < bytes.byteCount);
        if (high_syntax)
        {
            if (has_suffix)
            {
                if (cursor.valid)
                {
                    ReadHighFields(&candidate, &cursor);
                }
            }
            else
            {
                status = UTL_AVC_LEGACY;
            }
        }
        if (cursor.valid)
        {
            candidate.trailing.byteCount = bytes.byteCount - cursor.offset;
            if (0 < candidate.trailing.byteCount)
            {
                candidate.trailing.data = &bytes.data[cursor.offset];
            }
            *config = candidate;
        }
    }
    if (!cursor.valid)
    {
        status = UTL_AVC_INVALID;
    }
    return status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Avc_WriteParameter(Utl_Mpeg4_Span_t parameter, byte_t* output, uint32_t capacity)
{
    uint32_t count;
    bool_t   valid;
    bool_t   fits;
    bool_t   can_write;
    uint32_t written;
    LOGASSERT(NULL != output);
    count     = Utl_Avc_GetParameterByteCount(parameter);
    valid     = (0 < count);
    fits      = (count <= capacity);
    can_write = valid && fits;
    written   = 0;
    if (can_write)
    {
        output[0] = (byte_t)(parameter.byteCount >> BYTE_BITS);
        output[1] = (byte_t)(parameter.byteCount & BYTE_MASK);
        CRT_Memory_Copy(&output[PARAMETER_LENGTH_BYTES], parameter.data, parameter.byteCount);
        written = count;
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Avc_WriteConfig(const Utl_Avc_Config_t* config, byte_t* output, uint32_t capacity)
{
    uint32_t count;
    bool_t   valid;
    bool_t   fits;
    bool_t   can_write;
    uint32_t written;
    uint32_t position;
    LOGASSERT(NULL != config);
    LOGASSERT(NULL != output);
    count     = Utl_Avc_GetConfigByteCount(config);
    valid     = (0 < count);
    fits      = (count <= capacity);
    can_write = valid && fits;
    written   = 0;
    if (can_write)
    {
        output[VERSION_OFFSET]       = CONFIG_VERSION;
        output[PROFILE_OFFSET]       = config->profile;
        output[COMPATIBILITY_OFFSET] = config->compatibility;
        output[LEVEL_OFFSET]         = config->level;
        output[LENGTH_OFFSET]        = (byte_t)(LENGTH_RESERVED | ((uint32_t)config->lengthBytes - 1));
        output[SEQUENCE_OFFSET]      = (byte_t)(SEQUENCE_RESERVED | config->sequence.count);
        position                     = CONFIG_PREFIX_BYTES;
        WriteSpan(output, &position, config->sequence.bytes);
        output[position] = (byte_t)config->picture.count;
        ++position;
        WriteSpan(output, &position, config->picture.bytes);
        if (config->hasHighFields)
        {
            output[position] = (byte_t)(LENGTH_RESERVED | config->chromaFormat);
            ++position;
            output[position] = (byte_t)(DEPTH_RESERVED | ((uint32_t)config->lumaDepth - DEPTH_MIN));
            ++position;
            output[position] = (byte_t)(DEPTH_RESERVED | ((uint32_t)config->chromaDepth - DEPTH_MIN));
            ++position;
            output[position] = (byte_t)config->extensions.count;
            ++position;
            WriteSpan(output, &position, config->extensions.bytes);
        }
        WriteSpan(output, &position, config->trailing);
        LOGASSERT(count == position);
        written = count;
    }
    return written;
}
