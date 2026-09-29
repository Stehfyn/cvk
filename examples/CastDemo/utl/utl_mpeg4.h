#ifndef UTL_MPEG4_H
#define UTL_MPEG4_H

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

#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UTL_MPEG4_BOX_HEADER_BYTES       (8)          ///< Ordinary Box prefix.
#define UTL_MPEG4_LARGE_BOX_HEADER_BYTES (16)         ///< Prefix including largesize.
#define UTL_MPEG4_USER_TYPE_BYTES        (16)         ///< Extended UUID type width.
#define UTL_MPEG4_MAX_BOX_HEADER_BYTES   (32)         ///< Large UUID prefix.
#define UTL_MPEG4_FULLBOX_FIELDS_BYTES   (4)          ///< Version and 24-bit flags after Box.
#define UTL_MPEG4_FILE_TYPE_FIELDS_BYTES (8)          ///< Major brand and minor version.
#define UTL_MPEG4_BRAND_BYTES            (4)          ///< One big-endian compatible brand.
#define UTL_MPEG4_BOX_UUID               (0x75756964) ///< uuid.
#define UTL_MPEG4_BOX_FTYP               (0x66747970) ///< ftyp.
#define UTL_MPEG4_BOX_MDAT               (0x6D646174) ///< mdat.
#define UTL_MPEG4_BRAND_ISOM             (0x69736F6D) ///< ISO base media, original version.
#define UTL_MPEG4_BRAND_ISO2             (0x69736F32) ///< ISO base media, second version.
#define UTL_MPEG4_BRAND_ISO5             (0x69736F35) ///< ISO base media, fifth version.
#define UTL_MPEG4_BRAND_ISO6             (0x69736F36) ///< ISO base media, sixth version.
#define UTL_MPEG4_BRAND_AVC1             (0x61766331) ///< AVC file format.
#define UTL_MPEG4_BRAND_MP41             (0x6D703431) ///< MP4 version one.

#define UTL_MPEG4_BOX_MOOV                   (0x6D6F6F76) ///< Movie container.
#define UTL_MPEG4_BOX_TRAK                   (0x7472616B) ///< Track container.
#define UTL_MPEG4_BOX_MDIA                   (0x6D646961) ///< Media container.
#define UTL_MPEG4_BOX_MDHD                   (0x6D646864) ///< Media header.
#define UTL_MPEG4_BOX_HDLR                   (0x68646C72) ///< Handler reference.
#define UTL_MPEG4_BOX_MINF                   (0x6D696E66) ///< Media information.
#define UTL_MPEG4_BOX_STBL                   (0x7374626C) ///< Sample-table container.
#define UTL_MPEG4_BOX_STTS                   (0x73747473) ///< Decoding time to sample.
#define UTL_MPEG4_BOX_STSS                   (0x73747373) ///< Sync-sample numbers.
#define UTL_MPEG4_HANDLER_VIDEO              (0x76696465) ///< vide handler type.
#define UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES  (8)          ///< FullBox fields and entry count.
#define UTL_MPEG4_TIME_TO_SAMPLE_ENTRY_BYTES (8)          ///< Sample count and decode-time delta.
#define UTL_MPEG4_SYNC_SAMPLE_ENTRY_BYTES    (4)          ///< One-based sync-sample number.

#define UTL_MPEG4_BOX_MOOF           (0x6D6F6F66) ///< Movie fragment.
#define UTL_MPEG4_BOX_MFHD           (0x6D666864) ///< Movie fragment sequence header.
#define UTL_MPEG4_BOX_TRAF           (0x74726166) ///< Track fragment.
#define UTL_MPEG4_BOX_TFHD           (0x74666864) ///< Track fragment defaults.
#define UTL_MPEG4_BOX_TFDT           (0x74666474) ///< Track fragment decode time.
#define UTL_MPEG4_BOX_TRUN           (0x7472756E) ///< Track fragment run.
#define UTL_MPEG4_TFHD_BASE_OFFSET   (0x000001)   ///< Explicit base-data-offset.
#define UTL_MPEG4_TFHD_DESCRIPTION   (0x000002)   ///< Sample description index.
#define UTL_MPEG4_TFHD_DURATION      (0x000008)   ///< Default sample duration.
#define UTL_MPEG4_TFHD_SIZE          (0x000010)   ///< Default sample size.
#define UTL_MPEG4_TFHD_FLAGS         (0x000020)   ///< Default sample flags.
#define UTL_MPEG4_TFHD_EMPTY         (0x010000)   ///< Empty duration, with no track runs.
#define UTL_MPEG4_TFHD_MOOF_BASE     (0x020000)   ///< Moof-relative base; requires iso5 or later brands.
#define UTL_MPEG4_TRUN_DATA_OFFSET   (0x000001)   ///< Signed relative data offset.
#define UTL_MPEG4_TRUN_FIRST_FLAGS   (0x000004)   ///< Flags for the first sample only.
#define UTL_MPEG4_TRUN_DURATION      (0x000100)   ///< Per-sample decoding duration.
#define UTL_MPEG4_TRUN_SIZE          (0x000200)   ///< Per-sample byte count.
#define UTL_MPEG4_TRUN_FLAGS         (0x000400)   ///< Per-sample flags; excludes first-sample-flags.
#define UTL_MPEG4_TRUN_COMPOSITION   (0x000800)   ///< Per-sample composition-time offset.
#define UTL_MPEG4_SAMPLE_INDEPENDENT (0x02000000) ///< Depends on no other sample; sync sample.
#define UTL_MPEG4_SAMPLE_DEPENDENT   (0x01010000) ///< Depends on other samples and is not sync.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Counted stable bytes with caller-managed lifetime. */
typedef struct Utl_Mpeg4_Span_s
{
    const byte_t* data;      ///< Required when byteCount is nonzero.
    uint32_t      byteCount; ///< Readable memory extent, independent of file size.
} Utl_Mpeg4_Span_t;

/*************************************************************************************************/
/** \brief Box size and type, independent of payload storage and native I/O. */
typedef struct Utl_Mpeg4_BoxHeader_s
{
    uint64_t byteCount;                           ///< Complete resolved box extent including its header.
    uint32_t type;                                ///< Big-endian four-character code; unknown types are retained.
    byte_t   userType[UTL_MPEG4_USER_TYPE_BYTES]; ///< Active only for uuid.
    bool_t   extendsToEnd;                        ///< Wire size zero, extending to file end, not parent end.
    bool_t   hasLargeSize;                        ///< Wire size one; mutually exclusive with extendsToEnd.
} Utl_Mpeg4_BoxHeader_t;

/*************************************************************************************************/
/** \brief FullBox extension following the Box header. */
typedef struct Utl_Mpeg4_FullBoxFields_s
{
    uint8_t  version; ///< Unknown versions remain representable for caller dispatch.
    uint32_t flags;   ///< Low 24 bits; higher bits invalidate a writer model.
} Utl_Mpeg4_FullBoxFields_t;

/*************************************************************************************************/
/** \brief Complete FileTypeBox payload with all compatible brand codes borrowed. */
typedef struct Utl_Mpeg4_FileType_s
{
    uint32_t         majorBrand;       ///< Printable code, including unknown registered brands.
    uint32_t         minorVersion;     ///< Informative value, never a compatibility threshold.
    Utl_Mpeg4_Span_t compatibleBrands; ///< Whole big-endian four-byte codes, possibly empty.
} Utl_Mpeg4_FileType_t;

/*************************************************************************************************/
/** \brief Unsigned MPEG-4 field paired with its serialized bit width. */
typedef struct Utl_Mpeg4_BitField_s
{
    uint64_t value;    ///< Unsigned value fitting bitCount bits.
    uint32_t bitCount; ///< One through 64 bits, most significant bit first.
} Utl_Mpeg4_BitField_t;

/*************************************************************************************************/
/** \brief Unsigned MPEG-4 integer paired with its serialized byte width. */
typedef struct Utl_Mpeg4_Integer_s
{
    uint64_t value;     ///< Unsigned value fitting byteCount bytes.
    uint32_t byteCount; ///< One through eight big-endian bytes.
} Utl_Mpeg4_Integer_t;

/*************************************************************************************************/
/** \brief Caller-owned bounded MSB-first reader with sticky truncation. */
typedef struct Utl_Mpeg4_BitReader_s
{
    Utl_Mpeg4_Span_t bytes;    ///< Borrowed readable extent, stable for reader lifetime.
    uint64_t         position; ///< Consumed bits, never beyond bytes.byteCount times eight.
    bool_t           valid;    ///< FALSE after the first truncated field, until reinitialized.
} Utl_Mpeg4_BitReader_t;

/*************************************************************************************************/
/** \brief Caller-owned bounded writer or non-dereferencing bit counter. */
typedef struct Utl_Mpeg4_BitWriter_s
{
    byte_t*  output;   ///< NULL only for a counter initialized by InitializeBitCounter.
    uint32_t capacity; ///< Writable bytes; UINT32_MAX is the counter's sizing limit.
    uint64_t position; ///< Consumed bits, never beyond capacity times eight.
    bool_t   valid;    ///< Sticky invalid-field or capacity-exhaustion outcome.
} Utl_Mpeg4_BitWriter_t;

/*************************************************************************************************/
/** \brief Box type paired with its complete externally framed payload extent. */
typedef struct Utl_Mpeg4_BoxPayload_s
{
    uint32_t type;      ///< Enclosing box type.
    uint64_t byteCount; ///< Complete payload bytes; resident prefix storage is supplied separately.
} Utl_Mpeg4_BoxPayload_t;

/*************************************************************************************************/
/** \brief Typed stts/stss table prefix; entry bytes can be read in bounded pages. */
typedef struct Utl_Mpeg4_SampleTable_s
{
    uint32_t type;       ///< stts or stss; other box layouts require their own models.
    uint32_t entryCount; ///< Complete declared number of table entries.
} Utl_Mpeg4_SampleTable_t;

/*************************************************************************************************/
/** \brief Entry-owned decoding-time accumulator with explicit remaining work. */
typedef struct Utl_Mpeg4_TimeToSampleCursor_s
{
    uint32_t remaining;   ///< Number of declared runs not yet consumed.
    uint64_t sampleCount; ///< Samples in all consumed runs.
    uint64_t duration;    ///< Decode-time sum in media ticks, bounded by this API's uint64_t domain.
} Utl_Mpeg4_TimeToSampleCursor_t;

/*************************************************************************************************/
/** \brief One decoded run and its derived position in the media timeline. */
typedef struct Utl_Mpeg4_TimeToSampleRun_s
{
    uint64_t sampleOffset; ///< Zero-based number of preceding samples.
    uint64_t decodeTime;   ///< First sample's decoding time in media ticks.
    uint32_t sampleCount;  ///< Consecutive samples having the same delta; zero runs remain representable.
    uint32_t sampleDelta;  ///< Nonnegative time delta; zero is permitted by the syntax.
} Utl_Mpeg4_TimeToSampleRun_t;

/*************************************************************************************************/
/** \brief Entry-owned sync-sample ordering validator. */
typedef struct Utl_Mpeg4_SyncSampleCursor_s
{
    uint32_t remaining;  ///< Number of declared samples not yet consumed.
    uint32_t lastSample; ///< Previously consumed one-based number; zero before the first entry.
} Utl_Mpeg4_SyncSampleCursor_t;

/*************************************************************************************************/
/** \brief Sample fields selected by the enclosing trun flags. */
typedef struct Utl_Mpeg4_FragmentSample_s
{
    uint32_t duration;          ///< Decode duration when present.
    uint32_t byteCount;         ///< Sample bytes when present.
    uint32_t flags;             ///< ISO/IEC 14496-12 section 8.8.3 sample flags when present.
    int64_t  compositionOffset; ///< Unsigned 32-bit domain for version zero; signed 32-bit for version one.
} Utl_Mpeg4_FragmentSample_t;

/*************************************************************************************************/
/** \brief One complete track run with borrowed optional sample records. */
typedef struct Utl_Mpeg4_TrackRun_s
{
    uint8_t                           version;          ///< Zero or one, selecting composition offset interpretation.
    uint32_t                          flags;            ///< Known UTL_MPEG4_TRUN options.
    int32_t                           dataOffset;       ///< Relative byte offset when present.
    uint32_t                          firstSampleFlags; ///< First-sample override when present.
    const Utl_Mpeg4_FragmentSample_t* samples;          ///< Stable array when per-sample fields are selected.
    uint32_t                          sampleCount;      ///< Rows in the run, including implicit empty rows.
} Utl_Mpeg4_TrackRun_t;

/*************************************************************************************************/
/** \brief Track defaults, optional decoding time and borrowed runs. */
typedef struct Utl_Mpeg4_TrackFragment_s
{
    uint32_t                    trackId;           ///< Positive ID; document owner resolves its movie track.
    uint32_t                    flags;             ///< Known UTL_MPEG4_TFHD options.
    uint64_t                    baseDataOffset;    ///< Explicit base when present.
    uint32_t                    descriptionIndex;  ///< Positive sample-description index when present.
    uint32_t                    defaultDuration;   ///< Default duration when present.
    uint32_t                    defaultByteCount;  ///< Default sample bytes when present.
    uint32_t                    defaultFlags;      ///< Default sample flags when present.
    bool_t                      hasDecodeTime;     ///< Includes tfdt between tfhd and all runs.
    uint8_t                     decodeTimeVersion; ///< Zero or one when tfdt is present.
    uint64_t                    decodeTime;        ///< Absolute media decode time, without edit mapping.
    const Utl_Mpeg4_TrackRun_t* runs;              ///< Stable ordered run array.
    uint32_t                    runCount;          ///< Zero for duration-is-empty.
} Utl_Mpeg4_TrackFragment_t;

/*************************************************************************************************/
/** \brief One movie fragment; payload bytes remain separately owned. */
typedef struct Utl_Mpeg4_MovieFragment_s
{
    uint32_t                         sequenceNumber; ///< Document owner enforces strict increase across fragments.
    const Utl_Mpeg4_TrackFragment_t* tracks;         ///< Stable ordered fragment array; repeated track IDs are allowed.
    uint32_t                         trackCount;     ///< At least one track fragment.
} Utl_Mpeg4_MovieFragment_t;

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
 * \brief Initializes a complete empty ordinary box model.
 *
 * \par Details:
 *  Empty box initialized.
 *
 * \par Requirements:
 *  - 001 Missing owner shall assert.
 *  - 002 Every field shall be initialized without allocation; UUID type bytes shall start at zero.
 *
 * \param[out] header   Required exclusively writable owner
 * \param[in]  type     Box type code
 *
 * \retval None
 */
void Utl_Mpeg4_InitializeBoxHeader(Utl_Mpeg4_BoxHeader_t* header, uint32_t type);

/*************************************************************************************************/
/**
 * \brief Initializes a caller-owned MPEG-4 bit reader.
 *
 * \par Details:
 *  Reader initialized.
 *
 * \par Requirements:
 *  - 001 Missing owner or nonempty input storage shall assert.
 *  - 002 The stable source shall remain readable for the entire reader lifetime.
 *  - 003 Reinitialization shall reset both cursor and sticky status.
 *
 * \param[out] reader   Required exclusively writable owner
 * \param[in]  bytes    Stable complete memory extent
 *
 * \retval None
 */
void Utl_Mpeg4_InitializeBitReader(Utl_Mpeg4_BitReader_t* reader, Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Initializes a bounded caller-owned MPEG-4 bit writer.
 *
 * \par Details:
 *  Writer initialized.
 *
 * \par Requirements:
 *  - 001 Missing owner or writable storage shall assert, even for zero capacity.
 *  - 002 Initialization shall not modify output; the first field touching a byte shall initialize that byte.
 *  - 003 Owner and output shall be disjoint and exclusively owned.
 *
 * \param[out] writer     Required writer owner
 * \param[out] output     Required writable storage, untouched during initialization
 * \param[in]  capacity   Complete writable extent
 *
 * \retval None
 */
void Utl_Mpeg4_InitializeBitWriter(Utl_Mpeg4_BitWriter_t* writer, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Initializes a bit counter using the same validation as a bounded writer.
 *
 * \par Details:
 *  Counter initialized.
 *
 * \par Requirements:
 *  - 001 Missing owner shall assert.
 *  - 002 Counting shall not allocate, dereference source bytes or require output storage.
 *  - 003 The measured byte extent shall be bounded by UINT32_MAX.
 *
 * \param[out] writer   Required exclusively writable owner
 *
 * \retval None
 */
void Utl_Mpeg4_InitializeBitCounter(Utl_Mpeg4_BitWriter_t* writer);

/*************************************************************************************************/
/**
 * \brief Initializes an empty model for a supported fixed-entry sample table.
 *
 * \par Details:
 *  Empty table initialized.
 *
 * \par Requirements:
 *  - 001 Missing storage or an unsupported table kind shall assert as caller misuse.
 *  - 002 A zero-entry model shall be valid and serializable.
 *
 * \param[out] table   Required exclusively writable owner
 * \param[in]  type    stts or stss
 *
 * \retval None
 */
void Utl_Mpeg4_InitializeSampleTable(Utl_Mpeg4_SampleTable_t* table, uint32_t type);

/*************************************************************************************************/
/**
 * \brief Initializes a decoding-time cursor from a validated stts prefix.
 *
 * \par Details:
 *  Cursor initialized.
 *
 * \par Requirements:
 *  - 001 Missing owners and an incorrect table kind shall assert.
 *  - 002 Every member shall be initialized without retaining file or table storage.
 *
 * \param[out] cursor   Required exclusively writable owner
 * \param[in]  table    Required stts prefix model
 *
 * \retval None
 */
void Utl_Mpeg4_InitializeTimeToSampleCursor(Utl_Mpeg4_TimeToSampleCursor_t* cursor,
                                            const Utl_Mpeg4_SampleTable_t*  table);

/*************************************************************************************************/
/**
 * \brief Initializes the ordering validator for a present stss table.
 *
 * \par Details:
 *  Cursor initialized.
 *
 * \par Requirements:
 *  - 001 Missing owners and an incorrect table kind shall assert.
 *  - 002 A present empty table shall represent no sync samples; an absent table means all samples are sync samples.
 *
 * \param[out] cursor   Required exclusively writable owner
 * \param[in]  table    Required present stss prefix model
 *
 * \retval None
 */
void Utl_Mpeg4_InitializeSyncSampleCursor(Utl_Mpeg4_SyncSampleCursor_t* cursor, const Utl_Mpeg4_SampleTable_t* table);

/*************************************************************************************************/
/**
 * \brief Initializes a valid empty version-zero run.
 *
 * \par Details:
 *  Valid empty run initialized.
 *
 * \par Requirements:
 *  - 001 Missing owner storage shall assert; no resource or payload is acquired.
 *
 * \param[out] run   Required exclusive owner
 *
 * \retval None
 */
void Utl_Mpeg4_InitializeTrackRun(Utl_Mpeg4_TrackRun_t* run);

/*************************************************************************************************/
/**
 * \brief Initializes a track fragment containing only its required defaults header.
 *
 * \par Details:
 *  Valid empty track fragment initialized.
 *
 * \par Requirements:
 *  - 001 Missing owner or zero track ID shall assert as development errors.
 *  - 002 The document owner shall resolve the ID to a movie track before writing.
 *
 * \param[out] track     Required exclusive owner
 * \param[in]  trackId   Positive track identity
 *
 * \retval None
 */
void Utl_Mpeg4_InitializeTrackFragment(Utl_Mpeg4_TrackFragment_t* track, uint32_t trackId);

/*************************************************************************************************/
/**
 * \brief Initializes a sequence-one movie fragment borrowing initialized tracks.
 *
 * \par Details:
 *  Fragment initialized without allocation.
 *
 * \par Requirements:
 *  - 001 Missing owners or an empty track array shall assert.
 *  - 002 Every track shall be initialized; all borrowed models shall remain stable and disjoint from output.
 *
 * \param[out] fragment     Required exclusive owner
 * \param[in]  tracks       Required stable initialized array
 * \param[in]  trackCount   Readable track fragments
 *
 * \retval None
 */
void Utl_Mpeg4_InitializeMovieFragment(Utl_Mpeg4_MovieFragment_t*       fragment,
                                       const Utl_Mpeg4_TrackFragment_t* tracks,
                                       uint32_t                         trackCount);

/*************************************************************************************************/
/**
 * \brief Derives the prefix extent after validating the size model.
 *
 * \par Requirements:
 *  - 001 Missing model shall assert.
 *  - 002 Conflicting size forms, too-small extents and ordinary-size overflow shall fail.
 *  - 003 File-end placement shall be checked by the caller when serializing a size-zero model.
 *
 * \param[in] header   Required stable model
 *
 * \retval 8..32 Complete header bytes.
 * \retval 0 Invalid model.
 */
uint32_t Utl_Mpeg4_GetBoxHeaderByteCount(const Utl_Mpeg4_BoxHeader_t* header);

/*************************************************************************************************/
/**
 * \brief Validates the generic FullBox fields before serialization.
 *
 * \par Requirements:
 *  - 001 Only the 24-bit flags width shall be checked here; version-specific semantics belong to each box reader.
 *
 * \param[in] fields   Required stable model
 *
 * \retval 4 Valid fields.
 * \retval 0 Flags exceed their wire width.
 */
uint32_t Utl_Mpeg4_GetFullBoxFieldsByteCount(const Utl_Mpeg4_FullBoxFields_t* fields);

/*************************************************************************************************/
/**
 * \brief Derives the complete FileTypeBox payload size.
 *
 * \par Requirements:
 *  - 001 All compatible code extents and printable bytes shall validate before sizing.
 *  - 002 Unknown codes, order, duplicates and an empty compatible list shall be retained.
 *  - 003 Registration and claimed conformance are outside this wire-syntax check.
 *
 * \param[in] fileType   Required stable payload model
 *
 * \retval nonzero Payload bytes, excluding the Box header.
 * \retval 0 Invalid model or overflowing memory extent.
 */
uint32_t Utl_Mpeg4_GetFileTypeByteCount(const Utl_Mpeg4_FileType_t* fileType);

/*************************************************************************************************/
/**
 * \brief Queries a compatible code by zero-based index.
 *
 * \par Requirements:
 *  - 001 Missing owners shall assert.
 *  - 002 Invalid models or absent indices shall preserve the result.
 *
 * \param[out] brand      Required result
 * \param[in]  fileType   Required stable complete model
 * \param[in]  index      Zero-based compatible-code index
 *
 * \retval TRUE Code published.
 * \retval FALSE Invalid model or absent index.
 */
bool_t Utl_Mpeg4_GetCompatibleBrand(uint32_t* brand, const Utl_Mpeg4_FileType_t* fileType, uint32_t index);

/*************************************************************************************************/
/**
 * \brief Validates an unsigned integer value against its explicit wire width.
 *
 * \par Requirements:
 *  - 001 Zero or excessive widths and values outside the selected width shall derive zero.
 *  - 002 All shifts shall remain strictly below the integer width.
 *
 * \param[in] field   Value paired with its byte width
 *
 * \retval 1..8 Valid serialized byte count.
 * \retval 0 Invalid width or overflowing value.
 */
uint32_t Utl_Mpeg4_GetIntegerByteCount(Utl_Mpeg4_Integer_t field);

/*************************************************************************************************/
/**
 * \brief Derives the complete byte extent including a final partially filled byte.
 *
 * \par Requirements:
 *  - 001 The owner shall have been initialized and used only by the bitstream operations.
 *  - 002 Sticky failure shall derive zero; the valid member distinguishes failure from an empty stream.
 *
 * \param[in] writer   Required stable initialized writer or counter
 *
 * \retval count Rounded-up serialized bytes, or zero on failure.
 */
uint32_t Utl_Mpeg4_GetBitWriterByteCount(const Utl_Mpeg4_BitWriter_t* writer);

/*************************************************************************************************/
/**
 * \brief Derives the fixed entry width of a modeled sample table.
 *
 * \par Requirements:
 *  - 001 Unknown table types shall not be interpreted as either supported layout.
 *
 * \param[in] type   Box type code
 *
 * \retval 4..8 Modeled entry bytes.
 * \retval 0 Unsupported box type.
 */
uint32_t Utl_Mpeg4_GetSampleTableEntryByteCount(uint32_t type);

/*************************************************************************************************/
/**
 * \brief Derives the complete known table payload extent without loading entries.
 *
 * \par Requirements:
 *  - 001 The multiplication shall widen before evaluation, preserving the entire uint32_t count domain.
 *  - 002 Compatible future trailing fields are outside the derived known extent.
 *
 * \param[in] table   Required stable model
 *
 * \retval nonzero Known payload bytes including FullBox fields, excluding Box prefix.
 * \retval 0 Unsupported table type.
 */
uint64_t Utl_Mpeg4_GetSampleTableByteCount(const Utl_Mpeg4_SampleTable_t* table);

/*************************************************************************************************/
/**
 * \brief Derives a one-based sample number's decoding time within a known run.
 *
 * \par Requirements:
 *  - 001 Zero, out-of-run indices and unrepresentable times shall preserve output.
 *  - 002 The result is media decoding time; composition offsets and movie edits require separate mappings.
 *
 * \param[out] time     Required disjoint result in media ticks
 * \param[in]  run      Required stable run model
 * \param[in]  sample   One-based sample number
 *
 * \retval TRUE Decode time published.
 * \retval FALSE Sample absent or result exceeds this API domain.
 */
bool_t Utl_Mpeg4_GetSampleDecodeTime(uint64_t* time, const Utl_Mpeg4_TimeToSampleRun_t* run, uint64_t sample);

/*************************************************************************************************/
/**
 * \brief Validates a known complete trun and derives its serialized extent.
 *
 * \par Requirements:
 *  - 001 Unknown flags, conflicting first/per-sample flags and invalid selected values shall return zero.
 *  - 002 Counts shall widen before multiplication; box sizes exceeding UINT32_MAX are outside this memory-writer API.
 *  - 003 Representable models selecting nonempty sample rows shall require a readable array; implicit empty rows need no array.
 *
 * \param[in] run   Required stable complete run model
 *
 * \retval nonzero Complete box bytes.
 * \retval 0 Invalid or unsupported model.
 */
uint32_t Utl_Mpeg4_GetTrackRunByteCount(const Utl_Mpeg4_TrackRun_t* run);

/*************************************************************************************************/
/**
 * \brief Validates and measures one complete traf without accessing media payloads.
 *
 * \par Requirements:
 *  - 001 tfhd options and optional tfdt version/range shall validate.
 *  - 002 An empty-duration track shall contain no runs.
 *  - 003 Model arrays shall remain stable, initialized and readable for every selected count.
 *
 * \param[in] track   Required track fragment
 *
 * \retval nonzero Complete box bytes.
 * \retval 0 Invalid model or unrepresentable extent.
 */
uint32_t Utl_Mpeg4_GetTrackFragmentByteCount(const Utl_Mpeg4_TrackFragment_t* track);

/*************************************************************************************************/
/**
 * \brief Preflights the complete known moof layout before any output is written.
 *
 * \par Requirements:
 *  - 001 Every selected nested field and count shall validate before a nonzero size is returned.
 *  - 002 The owner shall separately enforce sequence ordering, movie track/description references, data extents, brands and edit compatibility.
 *  - 003 This model serializes mfhd, tfhd, optional tfdt and all known trun fields; encryption, grouping and extension boxes remain separate work.
 *
 * \param[in] fragment   Required stable complete fragment
 *
 * \retval nonzero Complete moof bytes, excluding media payloads.
 * \retval 0 Invalid or unrepresentable fragment.
 */
uint32_t Utl_Mpeg4_GetMovieFragmentByteCount(const Utl_Mpeg4_MovieFragment_t* fragment);

/*************************************************************************************************/
/**
 * \brief Reads a bounded Box prefix with distinct parent and file boundaries.
 *
 * \par Requirements:
 *  - 001 Missing result or nonempty storage shall assert.
 *  - 002 Only the complete prefix must be resident; payload bytes need not be in memory.
 *  - 003 Size zero shall resolve to file end and shall fail when that escapes the parent.
 *  - 004 Unknown types shall be retained, permitting extent-based skipping.
 *  - 005 Truncation, conflicting boundaries and invalid sizes shall preserve every output field.
 *
 * \param[out] header            Required disjoint result
 * \param[in]  bytes             Stable readable bytes beginning at the size field
 * \param[in]  parentRemaining   Bytes from box start to enclosing boundary
 * \param[in]  fileRemaining     Bytes from box start to file end
 *
 * \retval TRUE Complete prefix published.
 * \retval FALSE Truncated or invalid box.
 */
bool_t Utl_Mpeg4_ReadBoxHeader(Utl_Mpeg4_BoxHeader_t* header,
                               Utl_Mpeg4_Span_t       bytes,
                               uint64_t               parentRemaining,
                               uint64_t               fileRemaining);

/*************************************************************************************************/
/**
 * \brief Reads the version and flags immediately following a Box prefix.
 *
 * \par Requirements:
 *  - 001 Truncation shall preserve output; unknown versions shall remain available to dispatch.
 *  - 002 Each specific box reader shall skip unsupported versions instead of interpreting them as a known version.
 *
 * \param[out] fields   Required disjoint result
 * \param[in]  bytes    Stable FullBox extension and optional following payload
 *
 * \retval TRUE Generic fields published.
 * \retval FALSE Truncated extension.
 */
bool_t Utl_Mpeg4_ReadFullBoxFields(Utl_Mpeg4_FullBoxFields_t* fields, Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Reads the complete FileTypeBox payload as a bounded borrow.
 *
 * \par Requirements:
 *  - 001 Input shall be the exact payload extent, excluding its Box prefix.
 *  - 002 All codes shall validate before publication; failure shall preserve output.
 *  - 003 Input shall remain alive and stable for the result lifetime.
 *  - 004 File-level placement, cardinality and legacy missing-ftyp defaults belong to the document reader.
 *
 * \param[out] fileType   Required disjoint result
 * \param[in]  bytes      Complete stable payload
 *
 * \retval TRUE Complete payload model published.
 * \retval FALSE Malformed or truncated payload.
 */
bool_t Utl_Mpeg4_ReadFileType(Utl_Mpeg4_FileType_t* fileType, Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Reads one complete unsigned MPEG-4 integer in network byte order.
 *
 * \par Requirements:
 *  - 001 One through eight bytes shall be accepted; other extents shall preserve output.
 *  - 002 Required output and nonempty input storage shall assert when missing.
 *
 * \param[out] value   Required disjoint result
 * \param[in]  bytes   Complete one-through-eight-byte field
 *
 * \retval TRUE Value published.
 * \retval FALSE Invalid field extent.
 */
bool_t Utl_Mpeg4_ReadUnsigned(uint64_t* value, Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Reads a complete MSB-first bit field through a bounded cursor.
 *
 * \par Requirements:
 *  - 001 The initialized owner and a one-through-64-bit syntax width shall assert on misuse.
 *  - 002 Truncation shall latch invalid status and leave the cursor unchanged; subsequent reads shall remain failed.
 *  - 003 Each loop shall consume a byte portion, at most nine iterations for a field.
 *
 * \param[in,out] reader     Required initialized owner with stable source
 * \param[in]     bitCount   Declared syntax width, one through 64
 *
 * \retval value Complete unsigned field when valid remains TRUE.
 * \retval 0 Truncation or sticky failure; check the valid member.
 */
uint64_t Utl_Mpeg4_ReadBits(Utl_Mpeg4_BitReader_t* reader, uint32_t bitCount);

/*************************************************************************************************/
/**
 * \brief Reads a stts/stss prefix and validates all declared entry extents against the enclosing payload.
 *
 * \par Requirements:
 *  - 001 The complete entry bytes need not be resident; only the fixed prefix is required.
 *  - 002 Unknown types/versions, nonzero flags and truncated extents shall preserve output.
 *  - 003 Entries shall subsequently be consumed by their typed cursors to validate ordering and arithmetic.
 *  - 004 Future trailing payload bytes are outside the declared entry sequence and remain owned by the enclosing box.
 *
 * \param[out] table     Required disjoint prefix result
 * \param[in]  bytes     Stable resident payload prefix
 * \param[in]  payload   Enclosing box type and complete payload extent
 *
 * \retval TRUE Known prefix and entry extent published.
 * \retval FALSE Unsupported or invalid prefix/extent.
 */
bool_t Utl_Mpeg4_ReadSampleTableHeader(Utl_Mpeg4_SampleTable_t* table,
                                       Utl_Mpeg4_Span_t         bytes,
                                       Utl_Mpeg4_BoxPayload_t   payload);

/*************************************************************************************************/
/**
 * \brief Consumes one complete decoding-time run and derives its starting coordinates.
 *
 * \par Requirements:
 *  - 001 The cursor shall be initialized from the associated stts header.
 *  - 002 Zero sample counts and zero deltas shall remain representable; each successful read consumes exactly one entry.
 *  - 003 Truncation, completion and uint64_t accumulator exhaustion shall preserve both cursor and output.
 *  - 004 Accumulator exhaustion is an API representability failure, not a new normative format limit.
 *
 * \param[in,out] cursor   Required exclusively owned cursor
 * \param[out]    run      Required disjoint result
 * \param[in]     bytes    Complete entry prefix, optionally followed by later bytes
 *
 * \retval TRUE Run published and cursor advanced.
 * \retval FALSE Entry unavailable or cumulative coordinates unrepresentable.
 */
bool_t Utl_Mpeg4_ReadTimeToSampleRun(Utl_Mpeg4_TimeToSampleCursor_t* cursor,
                                     Utl_Mpeg4_TimeToSampleRun_t*    run,
                                     Utl_Mpeg4_Span_t                bytes);

/*************************************************************************************************/
/**
 * \brief Consumes one strictly increasing one-based sync-sample number.
 *
 * \par Requirements:
 *  - 001 The cursor shall be initialized from the associated present stss header.
 *  - 002 Zero, duplicate or descending numbers shall preserve both cursor and output.
 *  - 003 The caller shall additionally check that every number belongs to the track's sample domain.
 *
 * \param[in,out] cursor   Required exclusively owned ordering cursor
 * \param[out]    sample   Required disjoint one-based result
 * \param[in]     bytes    Complete entry prefix
 *
 * \retval TRUE Sample published and cursor advanced.
 * \retval FALSE Invalid number, truncated input or exhausted table.
 */
bool_t Utl_Mpeg4_ReadSyncSample(Utl_Mpeg4_SyncSampleCursor_t* cursor, uint32_t* sample, Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Serializes a validated Box prefix without writing its payload.
 *
 * \par Requirements:
 *  - 001 Model and output shall be present, stable and disjoint.
 *  - 002 Invalid model or insufficient capacity shall preserve the entire output.
 *  - 003 The caller shall ensure a size-zero box ends at file end.
 *
 * \param[in]  header     Required stable model
 * \param[out] output     Required disjoint writable storage
 * \param[in]  capacity   Writable bytes
 *
 * \retval nonzero Complete prefix bytes.
 * \retval 0 Invalid model or insufficient capacity.
 */
uint32_t Utl_Mpeg4_WriteBoxHeader(const Utl_Mpeg4_BoxHeader_t* header, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes the generic FullBox extension after preflight.
 *
 * \par Requirements:
 *  - 001 Stable model and output shall be disjoint.
 *  - 002 Invalid flags or insufficient capacity shall preserve all output bytes.
 *
 * \param[in]  fields     Required stable model
 * \param[out] output     Required disjoint storage
 * \param[in]  capacity   Writable bytes
 *
 * \retval 4 Complete extension written.
 * \retval 0 Invalid model or insufficient capacity.
 */
uint32_t Utl_Mpeg4_WriteFullBoxFields(const Utl_Mpeg4_FullBoxFields_t* fields, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes a complete FileTypeBox payload with all compatible codes retained.
 *
 * \par Requirements:
 *  - 001 Output shall be disjoint from the stable model and borrowed codes.
 *  - 002 All model fields and capacity shall validate before the first output store.
 *  - 003 No allocation, native operation or external CRT shall occur.
 *
 * \param[in]  fileType   Required complete stable payload model
 * \param[out] output     Required disjoint storage
 * \param[in]  capacity   Writable bytes
 *
 * \retval nonzero Payload bytes, excluding its Box header.
 * \retval 0 Invalid model or insufficient capacity; output unchanged.
 */
uint32_t Utl_Mpeg4_WriteFileType(const Utl_Mpeg4_FileType_t* fileType, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes a complete unsigned MPEG-4 integer in network byte order.
 *
 * \par Requirements:
 *  - 001 Required output shall assert when absent.
 *  - 002 Invalid model or insufficient capacity shall preserve the complete destination.
 *
 * \param[in]  field      Value paired with serialized byte width
 * \param[out] output     Required writable storage
 * \param[in]  capacity   Writable bytes
 *
 * \retval 1..8 Complete bytes written.
 * \retval 0 Invalid model or insufficient capacity.
 */
uint32_t Utl_Mpeg4_WriteUnsigned(Utl_Mpeg4_Integer_t field, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes or measures one complete MSB-first field after preflight.
 *
 * \par Details:
 *  Inspect valid for the sticky result.
 *
 * \par Requirements:
 *  - 001 The initialized owner and a one-through-64-bit syntax width shall assert on misuse.
 *  - 002 An out-of-width value or insufficient capacity shall latch failure without changing cursor or output.
 *  - 003 Later operations shall preserve the sticky failure until reinitialized.
 *  - 004 Untouched bytes shall remain unchanged; trailing bits in the final touched byte shall be zero.
 *
 * \param[in,out] writer   Required initialized writer or counter
 * \param[in]     field    Complete unsigned field
 *
 * \retval None
 */
void Utl_Mpeg4_WriteBits(Utl_Mpeg4_BitWriter_t* writer, Utl_Mpeg4_BitField_t field);

/*************************************************************************************************/
/**
 * \brief Writes or measures a complete byte-aligned MPEG-4 byte sequence.
 *
 * \par Details:
 *  Inspect valid for the sticky result.
 *
 * \par Requirements:
 *  - 001 An active initialized owner shall be byte-aligned; misuse shall assert. Sticky failure shall remain a no-op.
 *  - 002 Source shall remain stable and disjoint from output.
 *  - 003 Insufficient capacity shall latch failure without changing output or cursor.
 *  - 004 Counting shall validate the extent without reading source bytes.
 *
 * \param[in,out] writer   Required initialized writer or counter
 * \param[in]     bytes    Stable complete byte sequence
 *
 * \retval None
 */
void Utl_Mpeg4_WriteAlignedBytes(Utl_Mpeg4_BitWriter_t* writer, Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Writes a typed version-zero sample-table prefix.
 *
 * \par Requirements:
 *  - 001 Unsupported models and insufficient capacity shall preserve the entire output.
 *  - 002 This writes only the prefix; the caller shall subsequently serialize exactly entryCount typed entries.
 *
 * \param[in]  table      Required stable model
 * \param[out] output     Required disjoint prefix storage
 * \param[in]  capacity   Writable bytes
 *
 * \retval 8 Complete prefix bytes.
 * \retval 0 Unsupported model or insufficient capacity.
 */
uint32_t Utl_Mpeg4_WriteSampleTableHeader(const Utl_Mpeg4_SampleTable_t* table, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes a complete typed moof after validating all selected nested fields and output capacity.
 *
 * \par Requirements:
 *  - 001 Required output shall assert; it shall be disjoint from every stable borrowed model and sample array.
 *  - 002 Invalid models or insufficient capacity shall preserve every destination byte.
 *  - 003 No allocation, native operation, backpatching or external CRT shall occur.
 *  - 004 The caller shall honor the document-level requirements stated by GetMovieFragmentByteCount.
 *
 * \param[in]  fragment   Complete stable model
 * \param[out] output     Required writable destination
 * \param[in]  capacity   Writable bytes
 *
 * \retval nonzero Complete moof bytes.
 * \retval 0 Invalid model or insufficient capacity; output unchanged.
 */
uint32_t Utl_Mpeg4_WriteMovieFragment(const Utl_Mpeg4_MovieFragment_t* fragment, byte_t* output, uint32_t capacity);

#endif /* UTL_MPEG4_H */
