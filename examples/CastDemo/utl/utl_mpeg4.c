/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Shared MSB-first fields used by ISO/IEC 14496-1 Systems and 14496-3 Audio.
 * ISO/IEC 14496-12:2012, 4.2: network byte/bit ordering, Box and FullBox; 4.3: FileTypeBox.
 * https://www.iso.org/standard/61988.html
 * https://github.com/MPEGGroup/isobmff
 * ISO/IEC 14496-12:2012, 8.6.1.2 and 8.6.2: decoding-time runs and sync samples.
 * ISO/IEC 14496-12:2012, 8.8.3 through 8.8.8 and 8.8.12: typed movie-fragment serialization.
 * Composition/edit mappings, fragment readers, codecs and document conformance remain separate work.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "utl/utl_mpeg4.h"
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

#define INTEGER_BITS        (64)
#define BYTE_BITS           (8)
#define BYTE_MASK           (0xFF)
#define INTEGER32_BYTES     (4)
#define INTEGER64_BYTES     (8)
#define SIZE_TO_FILE_END    (0)
#define SIZE_LARGE          (1)
#define FULLBOX_FLAGS_BYTES (3)
#define FULLBOX_FLAGS_MASK  (0xFFFFFF)
#define PRINTABLE_FIRST     (0x20)
#define PRINTABLE_LAST      (0x7E)

#define FRAGMENT_FULLBOX_BYTES        (UTL_MPEG4_BOX_HEADER_BYTES + UTL_MPEG4_FULLBOX_FIELDS_BYTES)
#define FRAGMENT_COUNTED_BOX_BYTES    (FRAGMENT_FULLBOX_BYTES + INTEGER32_BYTES)
#define TFHD_KNOWN_FLAGS              (0x03003B)
#define TRUN_KNOWN_FLAGS              (0x000F05)
#define SAMPLE_RESERVED_MASK          (0xF0000000)
#define SAMPLE_DEPENDS_SHIFT          (24)
#define SAMPLE_DEPENDED_SHIFT         (22)
#define SAMPLE_REDUNDANCY_SHIFT       (20)
#define SAMPLE_DEPENDENCY_MASK        (3)
#define SAMPLE_DEPENDENCY_RESERVED    (3)
#define SAMPLE_DEPENDENCY_INDEPENDENT (2)
#define SAMPLE_LEADING_FLAG           (0x04000000) ///< Low is_leading bit selects either leading-sample form.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Preflighted ordinary fragment box, including its prefix in the byte count. */
typedef struct FragmentBox_s
{
    uint32_t type;      ///< Known fragment box type.
    uint32_t byteCount; ///< Complete validated ordinary box extent.
} FragmentBox_t;

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
 * \brief Asserts the counted-storage ownership contract.
 *
 * \par Details:
 *  Contract checked.
 *
 * \par Requirements:
 *  - 001 Nonempty missing storage shall assert.
 *
 * \param[in] bytes   Stable readable extent
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void AssertSpan(Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Checks the four printable code bytes required by FileTypeBox.
 *
 * \par Requirements:
 *  - 001 Unknown printable codes shall remain accepted; no registry allowlist shall be imposed.
 *
 * \param[in] brand   Big-endian brand code
 *
 * \retval TRUE All bytes printable.
 * \retval FALSE A byte is outside the printable code range.
 */
static FORCE_INLINE bool_t IsBrand(uint32_t brand);

/*************************************************************************************************/
/**
 * \brief Reads a preflighted big-endian unsigned field.
 *
 * \par Requirements:
 *  - 001 The caller shall provide one through eight readable bytes.
 *
 * \param[in] bytes   Preflighted readable field
 * \param[in] width   Field width
 *
 * \retval value Decoded unsigned field.
 */
static FORCE_INLINE uint64_t ReadUnsigned(const byte_t* bytes, uint32_t width);

/*************************************************************************************************/
/**
 * \brief Writes a preflighted unsigned big-endian field.
 *
 * \par Details:
 *  Field written.
 *
 * \par Requirements:
 *  - 001 Width, capacity and value representability shall be established before mutation.
 *
 * \param[out] bytes   Complete writable field
 * \param[in]  field   Value paired with its validated wire width
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteUnsigned(byte_t* bytes, Utl_Mpeg4_Integer_t field);

/*************************************************************************************************/
/**
 * \brief Writes a preflighted bit field in complete byte portions.
 *
 * \par Details:
 *  Field and cursor advanced.
 *
 * \par Requirements:
 *  - 001 The caller shall validate the complete field value and output capacity first.
 *  - 002 Partial bytes shall have been initialized by an earlier write on this owner.
 *  - 003 A newly touched byte shall be initialized with zero trailing alignment bits.
 *
 * \param[in,out] writer   Required preflighted writable owner
 * \param[in]     field    Complete validated field
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteBitField(Utl_Mpeg4_BitWriter_t* writer, Utl_Mpeg4_BitField_t field);

/*************************************************************************************************/
/**
 * \brief Checks reserved sample flag fields from sections 8.8.3 and 8.6.4.
 *
 * \par Requirements:
 *  - 001 Leading-sample, padding and priority bits shall remain preserved.
 *  - 002 Reserved dependency values and reserved upper bits shall reject.
 *
 * \param[in] flags   Encoded sample flags
 *
 * \retval TRUE Known flag encoding.
 * \retval FALSE Reserved encoding.
 */
static FORCE_INLINE bool_t IsSampleFlags(uint32_t flags);

/*************************************************************************************************/
/**
 * \brief Derives a known tfhd encoding, including all optional fields.
 *
 * \par Requirements:
 *  - 001 Required track storage shall be established by the caller.
 *  - 002 Unknown flag bits and invalid selected values shall return zero.
 *
 * \param[in] track   Stable track model
 *
 * \retval nonzero Complete tfhd bytes.
 * \retval 0 Invalid or unsupported model.
 */
static FORCE_INLINE uint32_t GetFragmentHeaderByteCount(const Utl_Mpeg4_TrackFragment_t* track);

/*************************************************************************************************/
/**
 * \brief Derives the selected version of the complete tfdt box.
 *
 * \par Requirements:
 *  - 001 Version-zero narrowing shall be rejected before serialization.
 *
 * \param[in] track   Stable track with an enabled tfdt
 *
 * \retval 16 Version-zero box.
 * \retval 20 Version-one box.
 * \retval 0 Invalid version or value.
 */
static FORCE_INLINE uint32_t GetDecodeTimeByteCount(const Utl_Mpeg4_TrackFragment_t* track);

/*************************************************************************************************/
/**
 * \brief Derives the known selected sample-row width.
 *
 * \par Requirements:
 *  - 001 The enclosing query shall reject unknown flags.
 *
 * \param[in] flags   Known track-run flags
 *
 * \retval 0..16 Bytes in each encoded sample row.
 */
static FORCE_INLINE uint32_t GetRunEntryByteCount(uint32_t flags);

/*************************************************************************************************/
/**
 * \brief Checks only selected sample fields before any output store.
 *
 * \par Requirements:
 *  - 001 Unselected members shall not affect validity.
 *  - 002 Composition offsets shall fit their signed or unsigned 32-bit wire domain.
 *
 * \param[in] sample   Stable sample record
 * \param[in] run      Validated version and selected fields
 *
 * \retval TRUE Selected values representable.
 * \retval FALSE Invalid selected field.
 */
static FORCE_INLINE bool_t IsFragmentSample(const Utl_Mpeg4_FragmentSample_t* sample, const Utl_Mpeg4_TrackRun_t* run);

/*************************************************************************************************/
/**
 * \brief Appends a preflighted 32-bit network field through the shared bit writer.
 *
 * \par Details:
 *  Writer advanced.
 *
 * \par Requirements:
 *  - 001 The caller shall retain exclusive output ownership and complete preflight.
 *
 * \param[in,out] writer   Required initialized writer
 * \param[in]     value    Complete field value
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteFragment32(Utl_Mpeg4_BitWriter_t* writer, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Appends a preflighted 64-bit network field through the shared bit writer.
 *
 * \par Details:
 *  Writer advanced.
 *
 * \par Requirements:
 *  - 001 The caller shall retain exclusive output ownership and complete preflight.
 *
 * \param[in,out] writer   Required initialized writer
 * \param[in]     value    Complete field value
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteFragment64(Utl_Mpeg4_BitWriter_t* writer, uint64_t value);

/*************************************************************************************************/
/**
 * \brief Appends a preflighted ordinary Box header.
 *
 * \par Details:
 *  Header appended.
 *
 * \par Requirements:
 *  - 001 The model shall pair a known type with its complete ordinary box extent.
 *  - 002 The complete outer query shall prove the size is representable.
 *
 * \param[in,out] writer   Required initialized writer
 * \param[in]     box      Paired type and complete extent
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteFragmentBox(Utl_Mpeg4_BitWriter_t* writer, FragmentBox_t box);

/*************************************************************************************************/
/**
 * \brief Appends preflighted version and flags.
 *
 * \par Details:
 *  Fields appended.
 *
 * \par Requirements:
 *  - 001 The enclosing query shall validate box-specific version and option relationships.
 *
 * \param[in,out] writer   Required writer
 * \param[in]     fields   Known FullBox fields
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteFragmentFields(Utl_Mpeg4_BitWriter_t* writer, Utl_Mpeg4_FullBoxFields_t fields);

/*************************************************************************************************/
/**
 * \brief Serializes a validated tfhd in specification field order.
 *
 * \par Details:
 *  Complete tfhd appended.
 *
 * \par Requirements:
 *  - 001 All fields and output extent shall have been preflighted.
 *
 * \param[in,out] writer   Required writer
 * \param[in]     track    Stable validated track
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteFragmentHeader(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4_TrackFragment_t* track);

/*************************************************************************************************/
/**
 * \brief Serializes a preflighted tfdt without narrowing unknown values.
 *
 * \par Details:
 *  Complete tfdt appended.
 *
 * \par Requirements:
 *  - 001 The parent writer shall place this after tfhd and before all trun boxes.
 *
 * \param[in,out] writer   Required writer
 * \param[in]     track    Stable track with known decode-time version
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteFragmentDecodeTime(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4_TrackFragment_t* track);

/*************************************************************************************************/
/**
 * \brief Serializes selected sample fields after complete validation.
 *
 * \par Details:
 *  Selected sample fields appended.
 *
 * \par Requirements:
 *  - 001 Signed offsets shall encode by the defined modulo conversion to uint32_t.
 *
 * \param[in,out] writer   Required writer
 * \param[in]     sample   Stable validated sample
 * \param[in]     run      Stable selected field layout
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteFragmentSample(Utl_Mpeg4_BitWriter_t*            writer,
                                             const Utl_Mpeg4_FragmentSample_t* sample,
                                             const Utl_Mpeg4_TrackRun_t*       run);

/*************************************************************************************************/
/**
 * \brief Serializes a complete preflighted trun, including signed offsets.
 *
 * \par Details:
 *  Complete trun appended.
 *
 * \par Requirements:
 *  - 001 Implicit empty rows shall not dereference sample storage.
 *
 * \param[in,out] writer   Required writer
 * \param[in]     run      Stable validated run
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteFragmentRun(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4_TrackRun_t* run);

/*************************************************************************************************/
/**
 * \brief Serializes one validated traf in required child order.
 *
 * \par Details:
 *  Complete traf appended.
 *
 * \par Requirements:
 *  - 001 No model mutation shall occur between outer preflight and completion.
 *
 * \param[in,out] writer   Required writer
 * \param[in]     track    Stable validated track
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteTrackFragment(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4_TrackFragment_t* track);

/*************************************************************************************************/
/**
 * \brief Derives a run extent without rereading sample values.
 *
 * \par Requirements:
 *  - 001 Counts shall widen before multiplication.
 *  - 002 The writer shall use this only after complete model validation.
 *
 * \param[in] run   Stable run layout
 *
 * \retval count Complete ordinary run extent.
 */
static FORCE_INLINE uint64_t GetRunByteCount(const Utl_Mpeg4_TrackRun_t* run);

/*************************************************************************************************/
/**
 * \brief Derives an already validated track extent without repeated sample validation.
 *
 * \par Requirements:
 *  - 001 The complete public preflight shall have succeeded and all borrowed models shall remain unchanged.
 *
 * \param[in] track   Stable validated track
 *
 * \retval count Complete validated traf extent.
 */
static FORCE_INLINE uint32_t GetTrackByteCount(const Utl_Mpeg4_TrackFragment_t* track);

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
    bool_t empty;
    bool_t present;
    bool_t valid;

    empty   = (0 == bytes.byteCount);
    present = (NULL != bytes.data);
    valid   = empty || present;
    LOGASSERT(valid);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsBrand(uint32_t brand)
{
    bool_t   valid;
    uint32_t index;
    byte_t   value;
    bool_t   above_control;
    bool_t   below_delete;
    bool_t   printable;

    valid = TRUE;
    for (index = 0; index < UTL_MPEG4_BRAND_BYTES; ++index)
    {
        value         = (byte_t)(brand & BYTE_MASK);
        above_control = (PRINTABLE_FIRST <= value);
        below_delete  = (PRINTABLE_LAST >= value);
        printable     = above_control && below_delete;
        valid         = valid && printable;
        brand         >>= BYTE_BITS;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint64_t ReadUnsigned(const byte_t* bytes, uint32_t width)
{
    uint64_t value;
    uint32_t index;

    LOGASSERT(NULL != bytes);
    LOGASSERT(0 < width);
    LOGASSERT(INTEGER64_BYTES >= width);
    value = 0;
    for (index = 0; index < width; ++index)
    {
        value = (value << BYTE_BITS) | bytes[index];
    }
    return value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteUnsigned(byte_t* bytes, Utl_Mpeg4_Integer_t field)
{
    uint32_t remaining;

    LOGASSERT(NULL != bytes);
    LOGASSERT(0 < field.byteCount);
    LOGASSERT(INTEGER64_BYTES >= field.byteCount);
    remaining = field.byteCount;
    for (;;)
    {
        --remaining;
        bytes[remaining] = (byte_t)(field.value & BYTE_MASK);
        field.value      >>= BYTE_BITS;
        if (0 == remaining)
        {
            break;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteBitField(Utl_Mpeg4_BitWriter_t* writer, Utl_Mpeg4_BitField_t field)
{
    uint32_t pending;
    uint32_t byte_index;
    uint32_t bit_offset;
    uint32_t take;
    uint32_t shift;
    uint32_t mask;
    uint32_t piece;
    byte_t   previous;

    pending = field.bitCount;
    for (;;)
    {
        byte_index = (uint32_t)(writer->position / BYTE_BITS);
        bit_offset = (uint32_t)(writer->position % BYTE_BITS);
        take       = BYTE_BITS - bit_offset;
        if (take > pending)
        {
            take = pending;
        }
        pending -= take;
        shift   = BYTE_BITS - bit_offset - take;
        LOGASSERT(0 < take);
        LOGASSERT(take <= BYTE_BITS);
        mask     = (uint32_t)BYTE_MASK >> (BYTE_BITS - take);
        piece    = (uint32_t)((field.value >> pending) & mask);
        previous = 0;
        if (0 < bit_offset)
        {
            previous = writer->output[byte_index];
        }
        writer->output[byte_index] = previous | (byte_t)(piece << shift);
        writer->position           += take;
        if (0 == pending)
        {
            break;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsSampleFlags(uint32_t flags)
{
    bool_t   valid;
    uint32_t depends;
    uint32_t depended;
    uint32_t redundancy;
    bool_t   valid_depends;
    bool_t   valid_depended;
    bool_t   valid_redundancy;

    valid = TRUE;
    if (flags & SAMPLE_RESERVED_MASK)
    {
        valid = FALSE;
    }
    depends          = (flags >> SAMPLE_DEPENDS_SHIFT) & SAMPLE_DEPENDENCY_MASK;
    depended         = (flags >> SAMPLE_DEPENDED_SHIFT) & SAMPLE_DEPENDENCY_MASK;
    redundancy       = (flags >> SAMPLE_REDUNDANCY_SHIFT) & SAMPLE_DEPENDENCY_MASK;
    valid_depends    = (SAMPLE_DEPENDENCY_RESERVED != depends);
    valid_depended   = (SAMPLE_DEPENDENCY_RESERVED != depended);
    valid_redundancy = (SAMPLE_DEPENDENCY_RESERVED != redundancy);
    valid            = valid && valid_depends && valid_depended && valid_redundancy;
    if (flags & SAMPLE_LEADING_FLAG)
    {
        if (SAMPLE_DEPENDENCY_INDEPENDENT == depends)
        {
            valid = FALSE;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetFragmentHeaderByteCount(const Utl_Mpeg4_TrackFragment_t* track)
{
    bool_t   valid;
    uint32_t count;
    bool_t   valid_flags;

    valid = (0 < track->trackId);
    if (track->flags & ~(uint32_t)TFHD_KNOWN_FLAGS)
    {
        valid = FALSE;
    }
    count = FRAGMENT_COUNTED_BOX_BYTES;
    if (track->flags & UTL_MPEG4_TFHD_BASE_OFFSET)
    {
        count += INTEGER64_BYTES;
    }
    if (track->flags & UTL_MPEG4_TFHD_DESCRIPTION)
    {
        if (0 == track->descriptionIndex)
        {
            valid = FALSE;
        }
        count += INTEGER32_BYTES;
    }
    if (track->flags & UTL_MPEG4_TFHD_DURATION)
    {
        count += INTEGER32_BYTES;
    }
    if (track->flags & UTL_MPEG4_TFHD_SIZE)
    {
        count += INTEGER32_BYTES;
    }
    if (track->flags & UTL_MPEG4_TFHD_FLAGS)
    {
        valid_flags = IsSampleFlags(track->defaultFlags);
        valid       = valid && valid_flags;
        count       += INTEGER32_BYTES;
    }
    if (!valid)
    {
        count = 0;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetDecodeTimeByteCount(const Utl_Mpeg4_TrackFragment_t* track)
{
    uint32_t count;

    count = 0;
    if (0 == track->decodeTimeVersion)
    {
        if (track->decodeTime <= UINT32_MAX)
        {
            count = FRAGMENT_FULLBOX_BYTES + INTEGER32_BYTES;
        }
    }
    else if (1 == track->decodeTimeVersion)
    {
        count = FRAGMENT_FULLBOX_BYTES + INTEGER64_BYTES;
    }
    else
    {
        /* Future versions require a separate layout. */
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetRunEntryByteCount(uint32_t flags)
{
    uint32_t count;

    count = 0;
    if (flags & UTL_MPEG4_TRUN_DURATION)
    {
        count += INTEGER32_BYTES;
    }
    if (flags & UTL_MPEG4_TRUN_SIZE)
    {
        count += INTEGER32_BYTES;
    }
    if (flags & UTL_MPEG4_TRUN_FLAGS)
    {
        count += INTEGER32_BYTES;
    }
    if (flags & UTL_MPEG4_TRUN_COMPOSITION)
    {
        count += INTEGER32_BYTES;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsFragmentSample(const Utl_Mpeg4_FragmentSample_t* sample, const Utl_Mpeg4_TrackRun_t* run)
{
    bool_t valid;
    bool_t nonnegative;
    bool_t fits_unsigned;
    bool_t fits_lower;
    bool_t fits_upper;

    valid = TRUE;
    if (run->flags & UTL_MPEG4_TRUN_FLAGS)
    {
        valid = IsSampleFlags(sample->flags);
    }
    if (run->flags & UTL_MPEG4_TRUN_COMPOSITION)
    {
        if (0 == run->version)
        {
            nonnegative   = (0 <= sample->compositionOffset);
            fits_unsigned = (sample->compositionOffset <= UINT32_MAX);
            valid         = valid && nonnegative && fits_unsigned;
        }
        else
        {
            fits_lower = (INT32_MIN <= sample->compositionOffset);
            fits_upper = (sample->compositionOffset <= INT32_MAX);
            valid      = valid && fits_lower && fits_upper;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteFragment32(Utl_Mpeg4_BitWriter_t* writer, uint32_t value)
{
    Utl_Mpeg4_BitField_t field;

    field.value    = value;
    field.bitCount = INTEGER32_BYTES * BYTE_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteFragment64(Utl_Mpeg4_BitWriter_t* writer, uint64_t value)
{
    Utl_Mpeg4_BitField_t field;

    field.value    = value;
    field.bitCount = INTEGER64_BYTES * BYTE_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteFragmentBox(Utl_Mpeg4_BitWriter_t* writer, FragmentBox_t box)
{
    WriteFragment32(writer, box.byteCount);
    WriteFragment32(writer, box.type);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteFragmentFields(Utl_Mpeg4_BitWriter_t* writer, Utl_Mpeg4_FullBoxFields_t fields)
{
    Utl_Mpeg4_BitField_t field;

    field.value    = fields.version;
    field.bitCount = BYTE_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    field.value    = fields.flags;
    field.bitCount = FULLBOX_FLAGS_BYTES * BYTE_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteFragmentHeader(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4_TrackFragment_t* track)
{
    FragmentBox_t             box;
    Utl_Mpeg4_FullBoxFields_t fields;

    box.type      = UTL_MPEG4_BOX_TFHD;
    box.byteCount = GetFragmentHeaderByteCount(track);
    WriteFragmentBox(writer, box);
    fields.version = 0;
    fields.flags   = track->flags;
    WriteFragmentFields(writer, fields);
    WriteFragment32(writer, track->trackId);
    if (track->flags & UTL_MPEG4_TFHD_BASE_OFFSET)
    {
        WriteFragment64(writer, track->baseDataOffset);
    }
    if (track->flags & UTL_MPEG4_TFHD_DESCRIPTION)
    {
        WriteFragment32(writer, track->descriptionIndex);
    }
    if (track->flags & UTL_MPEG4_TFHD_DURATION)
    {
        WriteFragment32(writer, track->defaultDuration);
    }
    if (track->flags & UTL_MPEG4_TFHD_SIZE)
    {
        WriteFragment32(writer, track->defaultByteCount);
    }
    if (track->flags & UTL_MPEG4_TFHD_FLAGS)
    {
        WriteFragment32(writer, track->defaultFlags);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteFragmentDecodeTime(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4_TrackFragment_t* track)
{
    FragmentBox_t             box;
    Utl_Mpeg4_FullBoxFields_t fields;

    box.type      = UTL_MPEG4_BOX_TFDT;
    box.byteCount = GetDecodeTimeByteCount(track);
    WriteFragmentBox(writer, box);
    fields.version = track->decodeTimeVersion;
    fields.flags   = 0;
    WriteFragmentFields(writer, fields);
    if (0 == track->decodeTimeVersion)
    {
        WriteFragment32(writer, (uint32_t)track->decodeTime);
    }
    else
    {
        WriteFragment64(writer, track->decodeTime);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteFragmentSample(Utl_Mpeg4_BitWriter_t*            writer,
                                             const Utl_Mpeg4_FragmentSample_t* sample,
                                             const Utl_Mpeg4_TrackRun_t*       run)
{
    if (run->flags & UTL_MPEG4_TRUN_DURATION)
    {
        WriteFragment32(writer, sample->duration);
    }
    if (run->flags & UTL_MPEG4_TRUN_SIZE)
    {
        WriteFragment32(writer, sample->byteCount);
    }
    if (run->flags & UTL_MPEG4_TRUN_FLAGS)
    {
        WriteFragment32(writer, sample->flags);
    }
    if (run->flags & UTL_MPEG4_TRUN_COMPOSITION)
    {
        WriteFragment32(writer, (uint32_t)sample->compositionOffset);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteFragmentRun(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4_TrackRun_t* run)
{
    FragmentBox_t             box;
    Utl_Mpeg4_FullBoxFields_t fields;
    uint32_t                  width;
    uint32_t                  index;

    box.type      = UTL_MPEG4_BOX_TRUN;
    box.byteCount = (uint32_t)GetRunByteCount(run);
    WriteFragmentBox(writer, box);
    fields.version = run->version;
    fields.flags   = run->flags;
    WriteFragmentFields(writer, fields);
    WriteFragment32(writer, run->sampleCount);
    if (run->flags & UTL_MPEG4_TRUN_DATA_OFFSET)
    {
        WriteFragment32(writer, (uint32_t)run->dataOffset);
    }
    if (run->flags & UTL_MPEG4_TRUN_FIRST_FLAGS)
    {
        WriteFragment32(writer, run->firstSampleFlags);
    }
    width = GetRunEntryByteCount(run->flags);
    if (0 < width)
    {
        for (index = 0; index < run->sampleCount; ++index)
        {
            WriteFragmentSample(writer, &run->samples[index], run);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteTrackFragment(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4_TrackFragment_t* track)
{
    FragmentBox_t box;
    uint32_t      index;

    box.type      = UTL_MPEG4_BOX_TRAF;
    box.byteCount = GetTrackByteCount(track);
    WriteFragmentBox(writer, box);
    WriteFragmentHeader(writer, track);
    if (track->hasDecodeTime)
    {
        WriteFragmentDecodeTime(writer, track);
    }
    for (index = 0; index < track->runCount; ++index)
    {
        WriteFragmentRun(writer, &track->runs[index]);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint64_t GetRunByteCount(const Utl_Mpeg4_TrackRun_t* run)
{
    uint64_t count;
    uint32_t width;

    count = FRAGMENT_COUNTED_BOX_BYTES;
    if (run->flags & UTL_MPEG4_TRUN_DATA_OFFSET)
    {
        count += INTEGER32_BYTES;
    }
    if (run->flags & UTL_MPEG4_TRUN_FIRST_FLAGS)
    {
        count += INTEGER32_BYTES;
    }
    width = GetRunEntryByteCount(run->flags);
    count += (uint64_t)run->sampleCount * width;
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetTrackByteCount(const Utl_Mpeg4_TrackFragment_t* track)
{
    uint64_t count;
    uint32_t index;

    count = UTL_MPEG4_BOX_HEADER_BYTES + (uint64_t)GetFragmentHeaderByteCount(track);
    if (track->hasDecodeTime)
    {
        count += GetDecodeTimeByteCount(track);
    }
    for (index = 0; index < track->runCount; ++index)
    {
        count += GetRunByteCount(&track->runs[index]);
    }
    LOGASSERT(count <= UINT32_MAX);
    return (uint32_t)count;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_InitializeBoxHeader(Utl_Mpeg4_BoxHeader_t* header, uint32_t type)
{
    LOGASSERT(NULL != header);
    header->type      = type;
    header->byteCount = UTL_MPEG4_BOX_HEADER_BYTES;
    CRT_Memory_Set(header->userType, 0, UTL_MPEG4_USER_TYPE_BYTES);
    header->extendsToEnd = FALSE;
    header->hasLargeSize = FALSE;
    if (UTL_MPEG4_BOX_UUID == type)
    {
        header->byteCount += UTL_MPEG4_USER_TYPE_BYTES;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_InitializeBitReader(Utl_Mpeg4_BitReader_t* reader, Utl_Mpeg4_Span_t bytes)
{
    LOGASSERT(NULL != reader);
    AssertSpan(bytes);
    reader->bytes    = bytes;
    reader->position = 0;
    reader->valid    = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_InitializeBitWriter(Utl_Mpeg4_BitWriter_t* writer, byte_t* output, uint32_t capacity)
{
    LOGASSERT(NULL != writer);
    LOGASSERT(NULL != output);
    writer->output   = output;
    writer->capacity = capacity;
    writer->position = 0;
    writer->valid    = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_InitializeBitCounter(Utl_Mpeg4_BitWriter_t* writer)
{
    LOGASSERT(NULL != writer);
    writer->output   = NULL;
    writer->capacity = UINT32_MAX;
    writer->position = 0;
    writer->valid    = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_InitializeSampleTable(Utl_Mpeg4_SampleTable_t* table, uint32_t type)
{
    uint32_t width;

    LOGASSERT(NULL != table);
    width = Utl_Mpeg4_GetSampleTableEntryByteCount(type);
    LOGASSERT(0 < width);
    table->type       = type;
    table->entryCount = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_InitializeTimeToSampleCursor(Utl_Mpeg4_TimeToSampleCursor_t* cursor,
                                            const Utl_Mpeg4_SampleTable_t*  table)
{
    LOGASSERT(NULL != cursor);
    LOGASSERT(NULL != table);
    LOGASSERT(UTL_MPEG4_BOX_STTS == table->type);
    cursor->remaining   = table->entryCount;
    cursor->sampleCount = 0;
    cursor->duration    = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_InitializeSyncSampleCursor(Utl_Mpeg4_SyncSampleCursor_t* cursor, const Utl_Mpeg4_SampleTable_t* table)
{
    LOGASSERT(NULL != cursor);
    LOGASSERT(NULL != table);
    LOGASSERT(UTL_MPEG4_BOX_STSS == table->type);
    cursor->remaining  = table->entryCount;
    cursor->lastSample = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_InitializeTrackRun(Utl_Mpeg4_TrackRun_t* run)
{
    LOGASSERT(NULL != run);
    run->version          = 0;
    run->flags            = 0;
    run->dataOffset       = 0;
    run->firstSampleFlags = 0;
    run->samples          = NULL;
    run->sampleCount      = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_InitializeTrackFragment(Utl_Mpeg4_TrackFragment_t* track, uint32_t trackId)
{
    LOGASSERT(NULL != track);
    LOGASSERT(0 < trackId);
    track->trackId           = trackId;
    track->flags             = 0;
    track->baseDataOffset    = 0;
    track->descriptionIndex  = 1;
    track->defaultDuration   = 0;
    track->defaultByteCount  = 0;
    track->defaultFlags      = 0;
    track->hasDecodeTime     = FALSE;
    track->decodeTimeVersion = 0;
    track->decodeTime        = 0;
    track->runs              = NULL;
    track->runCount          = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_InitializeMovieFragment(Utl_Mpeg4_MovieFragment_t*       fragment,
                                       const Utl_Mpeg4_TrackFragment_t* tracks,
                                       uint32_t                         trackCount)
{
    LOGASSERT(NULL != fragment);
    LOGASSERT(NULL != tracks);
    LOGASSERT(0 < trackCount);
    fragment->sequenceNumber = 1;
    fragment->tracks         = tracks;
    fragment->trackCount     = trackCount;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_GetBoxHeaderByteCount(const Utl_Mpeg4_BoxHeader_t* header)
{
    uint32_t count;
    bool_t   is_uuid;
    bool_t   conflicting_sizes;
    bool_t   fits_small;
    bool_t   fits_field;
    bool_t   includes_header;
    bool_t   valid;

    LOGASSERT(NULL != header);
    count = UTL_MPEG4_BOX_HEADER_BYTES;
    if (header->hasLargeSize)
    {
        count = UTL_MPEG4_LARGE_BOX_HEADER_BYTES;
    }
    is_uuid = (UTL_MPEG4_BOX_UUID == header->type);
    if (is_uuid)
    {
        count += UTL_MPEG4_USER_TYPE_BYTES;
    }
    conflicting_sizes = header->extendsToEnd && header->hasLargeSize;
    fits_small        = (header->byteCount <= UINT32_MAX);
    fits_field        = header->extendsToEnd || header->hasLargeSize || fits_small;
    includes_header   = (count <= header->byteCount);
    valid             = !conflicting_sizes && fits_field && includes_header;
    if (!valid)
    {
        count = 0;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_GetFullBoxFieldsByteCount(const Utl_Mpeg4_FullBoxFields_t* fields)
{
    uint32_t count;
    bool_t   valid;

    LOGASSERT(NULL != fields);
    count = 0;
    valid = (FULLBOX_FLAGS_MASK >= fields->flags);
    if (valid)
    {
        count = UTL_MPEG4_FULLBOX_FIELDS_BYTES;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_GetFileTypeByteCount(const Utl_Mpeg4_FileType_t* fileType)
{
    bool_t   whole_brands;
    bool_t   fits_size;
    bool_t   valid_brand;
    bool_t   valid;
    uint32_t position;
    uint32_t count;
    uint32_t brand;

    LOGASSERT(NULL != fileType);
    AssertSpan(fileType->compatibleBrands);
    whole_brands = (0 == (fileType->compatibleBrands.byteCount % UTL_MPEG4_BRAND_BYTES));
    fits_size    = (fileType->compatibleBrands.byteCount <= UINT32_MAX - UTL_MPEG4_FILE_TYPE_FIELDS_BYTES);
    valid_brand  = IsBrand(fileType->majorBrand);
    valid        = whole_brands && fits_size && valid_brand;
    position     = 0;
    count        = 0;
    for (;;)
    {
        if (!valid)
        {
            break;
        }
        if (position == fileType->compatibleBrands.byteCount)
        {
            break;
        }
        brand    = (uint32_t)ReadUnsigned(&fileType->compatibleBrands.data[position], UTL_MPEG4_BRAND_BYTES);
        valid    = IsBrand(brand);
        position += UTL_MPEG4_BRAND_BYTES;
    }
    if (valid)
    {
        count = UTL_MPEG4_FILE_TYPE_FIELDS_BYTES + fileType->compatibleBrands.byteCount;
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE bool_t Utl_Mpeg4_GetCompatibleBrand(uint32_t* brand, const Utl_Mpeg4_FileType_t* fileType, uint32_t index)
{
    uint32_t count;
    bool_t   valid_model;
    bool_t   has_index;
    bool_t   valid;

    LOGASSERT(NULL != brand);
    count       = Utl_Mpeg4_GetFileTypeByteCount(fileType);
    valid_model = (0 < count);
    has_index   = (index < (fileType->compatibleBrands.byteCount / UTL_MPEG4_BRAND_BYTES));
    valid       = valid_model && has_index;
    if (valid)
    {
        *brand = (uint32_t)ReadUnsigned(&fileType->compatibleBrands.data[(uint64_t)index * UTL_MPEG4_BRAND_BYTES],
                                        UTL_MPEG4_BRAND_BYTES);
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_GetIntegerByteCount(Utl_Mpeg4_Integer_t field)
{
    bool_t   nonempty;
    bool_t   within_integer;
    bool_t   valid_width;
    uint32_t count;
    uint32_t unused_bits;
    uint64_t maximum;
    bool_t   fits;

    nonempty       = (0 < field.byteCount);
    within_integer = (INTEGER64_BYTES >= field.byteCount);
    valid_width    = nonempty && within_integer;
    count          = 0;
    if (valid_width)
    {
        unused_bits = INTEGER_BITS - (field.byteCount * BYTE_BITS);
        maximum     = UINT64_MAX >> unused_bits;
        fits        = (field.value <= maximum);
        if (fits)
        {
            count = field.byteCount;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_GetBitWriterByteCount(const Utl_Mpeg4_BitWriter_t* writer)
{
    uint32_t count;

    LOGASSERT(NULL != writer);
    LOGASSERT(writer->position <= (uint64_t)writer->capacity * BYTE_BITS);
    count = 0;
    if (writer->valid)
    {
        count = (uint32_t)((writer->position + BYTE_BITS - 1) / BYTE_BITS);
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_GetSampleTableEntryByteCount(uint32_t type)
{
    uint32_t count;

    count = 0;
    if (UTL_MPEG4_BOX_STTS == type)
    {
        count = UTL_MPEG4_TIME_TO_SAMPLE_ENTRY_BYTES;
    }
    else if (UTL_MPEG4_BOX_STSS == type)
    {
        count = UTL_MPEG4_SYNC_SAMPLE_ENTRY_BYTES;
    }
    else
    {
        /* Other boxes have different entry layouts. */
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint64_t Utl_Mpeg4_GetSampleTableByteCount(const Utl_Mpeg4_SampleTable_t* table)
{
    uint32_t width;
    uint64_t count;

    LOGASSERT(NULL != table);
    width = Utl_Mpeg4_GetSampleTableEntryByteCount(table->type);
    count = 0;
    if (0 < width)
    {
        count = UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES + ((uint64_t)table->entryCount * width);
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE bool_t Utl_Mpeg4_GetSampleDecodeTime(uint64_t* time, const Utl_Mpeg4_TimeToSampleRun_t* run, uint64_t sample)
{
    bool_t   valid;
    uint64_t ordinal;
    uint64_t relative;
    uint64_t offset;

    LOGASSERT(NULL != time);
    LOGASSERT(NULL != run);
    valid    = (0 < sample);
    ordinal  = 0;
    relative = 0;
    if (valid)
    {
        ordinal = sample - 1;
        valid   = (run->sampleOffset <= ordinal);
    }
    if (valid)
    {
        relative = ordinal - run->sampleOffset;
        valid    = (relative < run->sampleCount);
    }
    if (valid)
    {
        offset = relative * run->sampleDelta;
        valid  = (offset <= UINT64_MAX - run->decodeTime);
        if (valid)
        {
            *time = run->decodeTime + offset;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_GetTrackRunByteCount(const Utl_Mpeg4_TrackRun_t* run)
{
    bool_t   valid;
    uint64_t count;
    bool_t   valid_flags;
    uint32_t width;
    bool_t   has_rows;
    bool_t   has_fields;
    bool_t   reads_rows;
    uint32_t index;
    uint32_t result;

    LOGASSERT(NULL != run);
    valid = (run->version <= 1);
    if (run->flags & ~(uint32_t)TRUN_KNOWN_FLAGS)
    {
        valid = FALSE;
    }
    count = FRAGMENT_COUNTED_BOX_BYTES;
    if (run->flags & UTL_MPEG4_TRUN_DATA_OFFSET)
    {
        count += INTEGER32_BYTES;
    }
    if (run->flags & UTL_MPEG4_TRUN_FIRST_FLAGS)
    {
        valid_flags = IsSampleFlags(run->firstSampleFlags);
        valid       = valid && valid_flags;
        if (run->flags & UTL_MPEG4_TRUN_FLAGS)
        {
            valid = FALSE;
        }
        count += INTEGER32_BYTES;
    }
    width = GetRunEntryByteCount(run->flags);
    count += (uint64_t)run->sampleCount * width;
    if (UINT32_MAX < count)
    {
        valid = FALSE;
    }
    has_rows   = (0 < run->sampleCount);
    has_fields = (0 < width);
    reads_rows = valid && has_rows && has_fields;
    if (reads_rows)
    {
        LOGASSERT(NULL != run->samples);
        for (index = 0; index < run->sampleCount; ++index)
        {
            valid = IsFragmentSample(&run->samples[index], run);
            if (!valid)
            {
                break;
            }
        }
    }
    result = 0;
    if (valid)
    {
        result = (uint32_t)count;
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_GetTrackFragmentByteCount(const Utl_Mpeg4_TrackFragment_t* track)
{
    uint32_t header_bytes;
    bool_t   valid;
    uint64_t count;
    uint32_t decode_bytes;
    uint32_t index;
    uint32_t run_bytes;
    uint32_t result;

    LOGASSERT(NULL != track);
    header_bytes = GetFragmentHeaderByteCount(track);
    valid        = (0 < header_bytes);
    count        = UTL_MPEG4_BOX_HEADER_BYTES + (uint64_t)header_bytes;
    if (track->flags & UTL_MPEG4_TFHD_EMPTY)
    {
        if (0 < track->runCount)
        {
            valid = FALSE;
        }
    }
    if (track->hasDecodeTime)
    {
        decode_bytes = GetDecodeTimeByteCount(track);
        if (0 == decode_bytes)
        {
            valid = FALSE;
        }
        count += decode_bytes;
    }
    if (valid)
    {
        if (0 < track->runCount)
        {
            LOGASSERT(NULL != track->runs);
        }
        for (index = 0; index < track->runCount; ++index)
        {
            run_bytes = Utl_Mpeg4_GetTrackRunByteCount(&track->runs[index]);
            count     += run_bytes;
            if (0 == run_bytes)
            {
                valid = FALSE;
            }
            if (UINT32_MAX < count)
            {
                valid = FALSE;
            }
            if (!valid)
            {
                break;
            }
        }
    }
    result = 0;
    if (valid)
    {
        result = (uint32_t)count;
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_GetMovieFragmentByteCount(const Utl_Mpeg4_MovieFragment_t* fragment)
{
    bool_t   valid;
    uint64_t count;
    uint32_t index;
    uint32_t track_bytes;
    uint32_t result;

    LOGASSERT(NULL != fragment);
    valid = (0 < fragment->trackCount);
    count = UTL_MPEG4_BOX_HEADER_BYTES + FRAGMENT_COUNTED_BOX_BYTES;
    if (valid)
    {
        LOGASSERT(NULL != fragment->tracks);
        for (index = 0; index < fragment->trackCount; ++index)
        {
            track_bytes = Utl_Mpeg4_GetTrackFragmentByteCount(&fragment->tracks[index]);
            count       += track_bytes;
            if (0 == track_bytes)
            {
                valid = FALSE;
            }
            if (UINT32_MAX < count)
            {
                valid = FALSE;
            }
            if (!valid)
            {
                break;
            }
        }
    }
    result = 0;
    if (valid)
    {
        result = (uint32_t)count;
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE bool_t Utl_Mpeg4_ReadBoxHeader(Utl_Mpeg4_BoxHeader_t* header,
                                                 Utl_Mpeg4_Span_t       bytes,
                                                 uint64_t              parentRemaining,
                                                 uint64_t              fileRemaining)
{
    bool_t                has_prefix;
    bool_t                parent_in_file;
    bool_t                valid;
    Utl_Mpeg4_BoxHeader_t candidate;
    uint32_t              size_field;
    uint32_t              prefix;
    bool_t                has_large;
    bool_t                is_uuid;
    uint32_t              uuid_offset;
    bool_t                has_uuid;
    uint32_t              uuid_mask;
    uint32_t              uuid_bytes;
    uint32_t              count;
    bool_t                fits_parent;

    LOGASSERT(NULL != header);
    AssertSpan(bytes);
    has_prefix     = (UTL_MPEG4_BOX_HEADER_BYTES <= bytes.byteCount);
    parent_in_file = (parentRemaining <= fileRemaining);
    valid          = has_prefix && parent_in_file;
    if (valid)
    {
        Utl_Mpeg4_InitializeBoxHeader(&candidate,
                                      (uint32_t)ReadUnsigned(&bytes.data[INTEGER32_BYTES], INTEGER32_BYTES));
        size_field             = (uint32_t)ReadUnsigned(bytes.data, INTEGER32_BYTES);
        candidate.extendsToEnd = (SIZE_TO_FILE_END == size_field);
        candidate.hasLargeSize = (SIZE_LARGE == size_field);
        candidate.byteCount    = size_field;
        prefix                 = UTL_MPEG4_BOX_HEADER_BYTES;
        if (candidate.hasLargeSize)
        {
            prefix    = UTL_MPEG4_LARGE_BOX_HEADER_BYTES;
            has_large = (prefix <= bytes.byteCount);
            valid     = has_large;
            if (has_large)
            {
                candidate.byteCount = ReadUnsigned(&bytes.data[UTL_MPEG4_BOX_HEADER_BYTES], INTEGER64_BYTES);
            }
        }
        else if (candidate.extendsToEnd)
        {
            candidate.byteCount = fileRemaining;
        }
        else
        {
            /* The ordinary size field already supplies the complete extent. */
        }
        is_uuid = (UTL_MPEG4_BOX_UUID == candidate.type);
        if (is_uuid)
        {
            uuid_offset  = prefix;
            prefix      += UTL_MPEG4_USER_TYPE_BYTES;
            has_uuid     = (prefix <= bytes.byteCount);
            valid        = valid && has_uuid;
            uuid_mask    = 0 - (uint32_t)has_uuid;
            uuid_offset &= uuid_mask;
            uuid_bytes   = UTL_MPEG4_USER_TYPE_BYTES & uuid_mask;
            CRT_Memory_Copy(candidate.userType, &bytes.data[uuid_offset], uuid_bytes);
        }
        count       = Utl_Mpeg4_GetBoxHeaderByteCount(&candidate);
        fits_parent = (candidate.byteCount <= parentRemaining);
        has_prefix  = (0 < count);
        valid       = valid && fits_parent && has_prefix;
        if (valid)
        {
            *header = candidate;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE bool_t Utl_Mpeg4_ReadFullBoxFields(Utl_Mpeg4_FullBoxFields_t* fields, Utl_Mpeg4_Span_t bytes)
{
    bool_t                    valid;
    Utl_Mpeg4_FullBoxFields_t candidate;

    LOGASSERT(NULL != fields);
    AssertSpan(bytes);
    valid = (UTL_MPEG4_FULLBOX_FIELDS_BYTES <= bytes.byteCount);
    if (valid)
    {
        candidate.version = bytes.data[0];
        candidate.flags   = (uint32_t)ReadUnsigned(&bytes.data[1], FULLBOX_FLAGS_BYTES);
        *fields           = candidate;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE bool_t Utl_Mpeg4_ReadFileType(Utl_Mpeg4_FileType_t* fileType, Utl_Mpeg4_Span_t bytes)
{
    bool_t               valid;
    Utl_Mpeg4_FileType_t candidate;
    uint32_t             count;

    LOGASSERT(NULL != fileType);
    AssertSpan(bytes);
    valid = (UTL_MPEG4_FILE_TYPE_FIELDS_BYTES <= bytes.byteCount);
    if (valid)
    {
        candidate.majorBrand                 = (uint32_t)ReadUnsigned(bytes.data, INTEGER32_BYTES);
        candidate.minorVersion               = (uint32_t)ReadUnsigned(&bytes.data[INTEGER32_BYTES], INTEGER32_BYTES);
        candidate.compatibleBrands.data      = &bytes.data[UTL_MPEG4_FILE_TYPE_FIELDS_BYTES];
        candidate.compatibleBrands.byteCount = bytes.byteCount - UTL_MPEG4_FILE_TYPE_FIELDS_BYTES;
        count                                = Utl_Mpeg4_GetFileTypeByteCount(&candidate);
        valid                                = (0 < count);
        if (valid)
        {
            *fileType = candidate;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE bool_t Utl_Mpeg4_ReadUnsigned(uint64_t* value, Utl_Mpeg4_Span_t bytes)
{
    bool_t nonempty;
    bool_t within_integer;
    bool_t valid;

    LOGASSERT(NULL != value);
    AssertSpan(bytes);
    nonempty       = (0 < bytes.byteCount);
    within_integer = (INTEGER64_BYTES >= bytes.byteCount);
    valid          = nonempty && within_integer;
    if (valid)
    {
        *value = ReadUnsigned(bytes.data, bytes.byteCount);
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint64_t Utl_Mpeg4_ReadBits(Utl_Mpeg4_BitReader_t* reader, uint32_t bitCount)
{
    uint64_t remaining;
    bool_t   fits;
    bool_t   can_read;
    uint64_t value;
    uint32_t pending;
    uint32_t byte_index;
    uint32_t bit_offset;
    uint32_t take;
    uint32_t shift;
    uint32_t mask;
    uint32_t piece;

    LOGASSERT(NULL != reader);
    LOGASSERT(0 < bitCount);
    LOGASSERT(INTEGER_BITS >= bitCount);
    LOGASSERT(reader->position <= (uint64_t)reader->bytes.byteCount * BYTE_BITS);
    remaining     = ((uint64_t)reader->bytes.byteCount * BYTE_BITS) - reader->position;
    fits          = (bitCount <= remaining);
    can_read      = reader->valid && fits;
    reader->valid = can_read;
    value         = 0;
    if (can_read)
    {
        pending = bitCount;
        for (;;)
        {
            byte_index = (uint32_t)(reader->position / BYTE_BITS);
            bit_offset = (uint32_t)(reader->position % BYTE_BITS);
            take       = BYTE_BITS - bit_offset;
            if (take > pending)
            {
                take = pending;
            }
            shift            = BYTE_BITS - bit_offset - take;
            mask             = ((uint32_t)1 << take) - 1;
            piece            = ((uint32_t)reader->bytes.data[byte_index] >> shift) & mask;
            value            = (value << take) | piece;
            reader->position += take;
            pending          -= take;
            if (0 == pending)
            {
                break;
            }
        }
    }
    return value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE bool_t Utl_Mpeg4_ReadSampleTableHeader(Utl_Mpeg4_SampleTable_t* table,
                                       Utl_Mpeg4_Span_t         bytes,
                                       Utl_Mpeg4_BoxPayload_t   payload)
{
    uint32_t                  width;
    bool_t                    known_type;
    bool_t                    has_prefix;
    bool_t                    valid;
    Utl_Mpeg4_FullBoxFields_t fields;
    bool_t                    supported_version;
    bool_t                    valid_flags;
    Utl_Mpeg4_SampleTable_t   candidate;
    uint64_t                  required;

    LOGASSERT(NULL != table);
    AssertSpan(bytes);
    width      = Utl_Mpeg4_GetSampleTableEntryByteCount(payload.type);
    known_type = (0 < width);
    has_prefix = (UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES <= bytes.byteCount);
    valid      = known_type && has_prefix;
    if (valid)
    {
        valid = Utl_Mpeg4_ReadFullBoxFields(&fields, bytes);
        LOGASSERT(valid);
        supported_version = (0 == fields.version);
        valid_flags       = (0 == fields.flags);
        valid             = supported_version && valid_flags;
        if (valid)
        {
            candidate.type       = payload.type;
            candidate.entryCount = (uint32_t)ReadUnsigned(&bytes.data[UTL_MPEG4_FULLBOX_FIELDS_BYTES], INTEGER32_BYTES);
            required             = Utl_Mpeg4_GetSampleTableByteCount(&candidate);
            valid                = (required <= payload.byteCount);
            if (valid)
            {
                *table = candidate;
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
extern FORCE_INLINE bool_t Utl_Mpeg4_ReadTimeToSampleRun(Utl_Mpeg4_TimeToSampleCursor_t* cursor,
                                     Utl_Mpeg4_TimeToSampleRun_t*    run,
                                     Utl_Mpeg4_Span_t                bytes)
{
    bool_t                      has_entry;
    bool_t                      has_bytes;
    bool_t                      valid;
    Utl_Mpeg4_TimeToSampleRun_t candidate;
    uint64_t                    duration;
    bool_t                      samples_fit;
    bool_t                      duration_fits;

    LOGASSERT(NULL != cursor);
    LOGASSERT(NULL != run);
    AssertSpan(bytes);
    has_entry = (0 < cursor->remaining);
    has_bytes = (UTL_MPEG4_TIME_TO_SAMPLE_ENTRY_BYTES <= bytes.byteCount);
    valid     = has_entry && has_bytes;
    if (valid)
    {
        candidate.sampleOffset = cursor->sampleCount;
        candidate.decodeTime   = cursor->duration;
        candidate.sampleCount  = (uint32_t)ReadUnsigned(bytes.data, INTEGER32_BYTES);
        candidate.sampleDelta  = (uint32_t)ReadUnsigned(&bytes.data[INTEGER32_BYTES], INTEGER32_BYTES);
        duration               = (uint64_t)candidate.sampleCount * candidate.sampleDelta;
        samples_fit            = (candidate.sampleCount <= UINT64_MAX - cursor->sampleCount);
        duration_fits          = (duration <= UINT64_MAX - cursor->duration);
        valid                  = samples_fit && duration_fits;
        if (valid)
        {
            cursor->sampleCount += candidate.sampleCount;
            cursor->duration    += duration;
            --cursor->remaining;
            *run = candidate;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE bool_t Utl_Mpeg4_ReadSyncSample(Utl_Mpeg4_SyncSampleCursor_t* cursor, uint32_t* sample, Utl_Mpeg4_Span_t bytes)
{
    bool_t   has_entry;
    bool_t   has_bytes;
    bool_t   valid;
    uint32_t candidate;

    LOGASSERT(NULL != cursor);
    LOGASSERT(NULL != sample);
    AssertSpan(bytes);
    has_entry = (0 < cursor->remaining);
    has_bytes = (UTL_MPEG4_SYNC_SAMPLE_ENTRY_BYTES <= bytes.byteCount);
    valid     = has_entry && has_bytes;
    if (valid)
    {
        candidate = (uint32_t)ReadUnsigned(bytes.data, INTEGER32_BYTES);
        valid     = (cursor->lastSample < candidate);
        if (valid)
        {
            cursor->lastSample = candidate;
            --cursor->remaining;
            *sample = candidate;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_WriteBoxHeader(const Utl_Mpeg4_BoxHeader_t* header, byte_t* output, uint32_t capacity)
{
    uint32_t            count;
    bool_t              valid_model;
    bool_t              fits_output;
    bool_t              can_write;
    uint32_t            written;
    uint32_t            uuid_offset;
    uint32_t            size_field;
    Utl_Mpeg4_Integer_t field;

    LOGASSERT(NULL != output);
    count       = Utl_Mpeg4_GetBoxHeaderByteCount(header);
    valid_model = (0 < count);
    fits_output = (count <= capacity);
    can_write   = valid_model && fits_output;
    written     = 0;
    if (can_write)
    {
        uuid_offset = UTL_MPEG4_BOX_HEADER_BYTES;
        if (header->hasLargeSize)
        {
            size_field  = SIZE_LARGE;
            uuid_offset = UTL_MPEG4_LARGE_BOX_HEADER_BYTES;
        }
        else if (!header->extendsToEnd)
        {
            size_field = (uint32_t)header->byteCount;
        }
        else
        {
            size_field = SIZE_TO_FILE_END;
        }
        field.value     = size_field;
        field.byteCount = INTEGER32_BYTES;
        WriteUnsigned(output, field);
        field.value     = header->type;
        field.byteCount = INTEGER32_BYTES;
        WriteUnsigned(&output[INTEGER32_BYTES], field);
        if (header->hasLargeSize)
        {
            field.value     = header->byteCount;
            field.byteCount = INTEGER64_BYTES;
            WriteUnsigned(&output[UTL_MPEG4_BOX_HEADER_BYTES], field);
        }
        if (UTL_MPEG4_BOX_UUID == header->type)
        {
            CRT_Memory_Copy(&output[uuid_offset], header->userType, UTL_MPEG4_USER_TYPE_BYTES);
        }
        written = count;
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_WriteFullBoxFields(const Utl_Mpeg4_FullBoxFields_t* fields, byte_t* output, uint32_t capacity)
{
    uint32_t            count;
    bool_t              valid_model;
    bool_t              fits_output;
    bool_t              can_write;
    uint32_t            written;
    Utl_Mpeg4_Integer_t field;

    LOGASSERT(NULL != output);
    count       = Utl_Mpeg4_GetFullBoxFieldsByteCount(fields);
    valid_model = (0 < count);
    fits_output = (count <= capacity);
    can_write   = valid_model && fits_output;
    written     = 0;
    if (can_write)
    {
        output[0]       = fields->version;
        field.value     = fields->flags;
        field.byteCount = FULLBOX_FLAGS_BYTES;
        WriteUnsigned(&output[1], field);
        written = count;
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_WriteFileType(const Utl_Mpeg4_FileType_t* fileType, byte_t* output, uint32_t capacity)
{
    uint32_t            count;
    bool_t              valid_model;
    bool_t              fits_output;
    bool_t              can_write;
    uint32_t            written;
    Utl_Mpeg4_Integer_t field;
    byte_t              empty = 0;
    const byte_t*       brands;

    LOGASSERT(NULL != output);
    count       = Utl_Mpeg4_GetFileTypeByteCount(fileType);
    valid_model = (0 < count);
    fits_output = (count <= capacity);
    can_write   = valid_model && fits_output;
    written     = 0;
    if (can_write)
    {
        field.value     = fileType->majorBrand;
        field.byteCount = INTEGER32_BYTES;
        WriteUnsigned(output, field);
        field.value     = fileType->minorVersion;
        field.byteCount = INTEGER32_BYTES;
        WriteUnsigned(&output[INTEGER32_BYTES], field);
        brands = (0 < fileType->compatibleBrands.byteCount) ? fileType->compatibleBrands.data : &empty;
        CRT_Memory_Copy(&output[UTL_MPEG4_FILE_TYPE_FIELDS_BYTES], brands,
                        fileType->compatibleBrands.byteCount);
        written = count;
    }
    return written;
}
/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_WriteUnsigned(Utl_Mpeg4_Integer_t field, byte_t* output, uint32_t capacity)
{
    uint32_t count;
    bool_t   valid_model;
    bool_t   fits;
    bool_t   can_write;
    uint32_t written;

    LOGASSERT(NULL != output);
    count       = Utl_Mpeg4_GetIntegerByteCount(field);
    valid_model = (0 < count);
    fits        = (count <= capacity);
    can_write   = valid_model && fits;
    written     = 0;
    if (can_write)
    {
        WriteUnsigned(output, field);
        written = count;
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_WriteBits(Utl_Mpeg4_BitWriter_t* writer, Utl_Mpeg4_BitField_t field)
{
    uint64_t remaining;
    uint64_t maximum;
    bool_t   fits_value;
    bool_t   fits_output;
    bool_t   can_write;

    LOGASSERT(NULL != writer);
    LOGASSERT(0 < field.bitCount);
    LOGASSERT(INTEGER_BITS >= field.bitCount);
    LOGASSERT(writer->position <= (uint64_t)writer->capacity * BYTE_BITS);
    remaining     = ((uint64_t)writer->capacity * BYTE_BITS) - writer->position;
    maximum       = UINT64_MAX >> (INTEGER_BITS - field.bitCount);
    fits_value    = (field.value <= maximum);
    fits_output   = (field.bitCount <= remaining);
    can_write     = writer->valid && fits_value && fits_output;
    writer->valid = can_write;
    if (can_write)
    {
        if (NULL != writer->output)
        {
            WriteBitField(writer, field);
        }
        else
        {
            writer->position += field.bitCount;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4_WriteAlignedBytes(Utl_Mpeg4_BitWriter_t* writer, Utl_Mpeg4_Span_t bytes)
{
    bool_t   inactive;
    bool_t   aligned;
    bool_t   valid_alignment;
    uint64_t position;
    bool_t   fits;
    bool_t   can_write;
    bool_t   has_output;
    bool_t   nonempty;
    bool_t   copy;

    LOGASSERT(NULL != writer);
    AssertSpan(bytes);
    inactive        = !writer->valid;
    aligned         = (0 == (writer->position % BYTE_BITS));
    valid_alignment = inactive || aligned;
    LOGASSERT(valid_alignment);
    LOGASSERT(writer->position <= (uint64_t)writer->capacity * BYTE_BITS);
    position      = writer->position / BYTE_BITS;
    fits          = (bytes.byteCount <= writer->capacity - position);
    can_write     = writer->valid && fits;
    writer->valid = can_write;
    if (can_write)
    {
        has_output = (NULL != writer->output);
        nonempty   = (0 < bytes.byteCount);
        copy       = has_output && nonempty;
        if (copy)
        {
            CRT_Memory_Copy(&writer->output[position], bytes.data, bytes.byteCount);
        }
        writer->position += (uint64_t)bytes.byteCount * BYTE_BITS;
    }
}
/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4_WriteSampleTableHeader(const Utl_Mpeg4_SampleTable_t* table, byte_t* output, uint32_t capacity)
{
    uint64_t            count;
    bool_t              valid_model;
    bool_t              fits;
    bool_t              can_write;
    uint32_t            written;
    Utl_Mpeg4_Integer_t field;

    LOGASSERT(NULL != output);
    count       = Utl_Mpeg4_GetSampleTableByteCount(table);
    valid_model = (0 < count);
    fits        = (UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES <= capacity);
    can_write   = valid_model && fits;
    written     = 0;
    if (can_write)
    {
        field.value     = 0;
        field.byteCount = UTL_MPEG4_FULLBOX_FIELDS_BYTES;
        WriteUnsigned(output, field);
        field.value     = table->entryCount;
        field.byteCount = INTEGER32_BYTES;
        WriteUnsigned(&output[UTL_MPEG4_FULLBOX_FIELDS_BYTES], field);
        written = UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES;
    }
    return written;
}
/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Mpeg4_WriteMovieFragment(const Utl_Mpeg4_MovieFragment_t* fragment, byte_t* output, uint32_t capacity)
{
    uint32_t                  count;
    bool_t                    valid_model;
    bool_t                    fits;
    bool_t                    can_write;
    uint32_t                  written;
    Utl_Mpeg4_BitWriter_t     writer;
    FragmentBox_t             box;
    Utl_Mpeg4_FullBoxFields_t fields;
    uint32_t                  index;

    LOGASSERT(NULL != output);
    count       = Utl_Mpeg4_GetMovieFragmentByteCount(fragment);
    valid_model = (0 < count);
    fits        = (count <= capacity);
    can_write   = valid_model && fits;
    written     = 0;
    if (can_write)
    {
        Utl_Mpeg4_InitializeBitWriter(&writer, output, capacity);
        box.type      = UTL_MPEG4_BOX_MOOF;
        box.byteCount = count;
        WriteFragmentBox(&writer, box);
        box.type      = UTL_MPEG4_BOX_MFHD;
        box.byteCount = FRAGMENT_COUNTED_BOX_BYTES;
        WriteFragmentBox(&writer, box);
        fields.version = 0;
        fields.flags   = 0;
        WriteFragmentFields(&writer, fields);
        WriteFragment32(&writer, fragment->sequenceNumber);
        for (index = 0; index < fragment->trackCount; ++index)
        {
            WriteTrackFragment(&writer, &fragment->tracks[index]);
        }
        written = Utl_Mpeg4_GetBitWriterByteCount(&writer);
        LOGASSERT(writer.valid);
        LOGASSERT(count == written);
    }
    return written;
}
