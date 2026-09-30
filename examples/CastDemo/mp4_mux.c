/*=================================================================================================
** 1. References
**===============================================================================================*/

/* mp4_mux.c - a minimal progressive MP4 muxer for the GPU proxy: ftyp, a streamed mdat, and a moov
** written on finish with avc1/avcC video and optional mp4a/esds AAC audio. */
/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mp4_mux.h"
#include "utl/utl_mpeg4.h"
#include "utl/utl_mpeg4systems.h"
#include "utl/utl_mpeg4audio.h"
#include "os/os_compiler.h"
#include "os/os_assert.h"
#include "os/os_types.h"
#include "crt/crt_memory.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define AUDIO_CONFIGURATION_CAPACITY (64)
#define AUDIO_DESCRIPTOR_CAPACITY    (128) /* 64 configuration bytes plus complete nested descriptor overhead. */
#define AUDIO_SAMPLE_BITS            (16)
#define AUDIO_RATE_SHIFT             (16)
#define AUDIO_RESERVED_BYTES         (6)
#define AUDIO_DATA_REFERENCE         (1)
#define FILE_TYPE_BYTE_COUNT         (24)
#define FILE_TYPE_MINOR_VERSION      (0x200)
#define FILE_TYPE_PROGRESSIVE_BRANDS "\x69\x73\x6F\x32\x61\x76\x63\x31"
#define FILE_TYPE_FRAGMENTED_BRANDS  "\x69\x73\x6F\x36\x69\x73\x6F\x35"

#define MAX_PARAM_BYTES            (512)
#define MAX_FRAGMENT_SAMPLES       (256)
#define FRAGMENT_TRACK_CAPACITY    (2)
#define FRAGMENT_METADATA_CAPACITY (8192) ///< Above 5272-byte maximum moof and current bounded init segment.
#define MUX_INTEGER32_BYTES        (4)
#define ANNEXB_PREFIX_BYTES        (3)
#define ANNEXB_ZERO_BYTE           (0)
#define ANNEXB_STOP_BYTE           (1)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

typedef struct MuxSample
{
    uint64_t offset;
    uint32_t size;
    uint32_t duration;
    int      keyframe;
} MuxSample;

/* Explicitly bounded assembly state. Legacy payload/progressive buffers still grow; fragment metadata is fixed. */
typedef struct MuxBuffer
{
    byte_t*  data;
    uint32_t size;
    uint32_t capacity;
    bool_t   fixed;  ///< The supplied storage shall never be grown or freed here.
    bool_t   failed; ///< Sticky append failure; later writes are no-ops.
} MuxBuffer;

struct Mp4Mux
{
    HANDLE     file;
    unsigned   width;
    unsigned   height;
    byte_t     sps[MAX_PARAM_BYTES];
    uint32_t   spsSize;
    byte_t     pps[MAX_PARAM_BYTES];
    uint32_t   ppsSize;
    unsigned   videoTimescale;
    int        hasAudio;
    unsigned   audioTimescale;
    unsigned   audioSampleRate;
    unsigned   audioChannels;
    byte_t     asc[AUDIO_CONFIGURATION_CAPACITY];
    uint32_t   ascSize;
    int64_t    mdatStart;
    MuxSample* video;
    uint32_t   videoCount;
    uint32_t   videoCapacity;
    MuxSample* audio;
    uint32_t   audioCount;
    uint32_t   audioCapacity;
    int        failed;

    /* Fragmented (fMP4) mode: an init segment up front, then moof+mdat fragments appended as the
    ** transcode produces them, so the file is playable within seconds instead of after the end. */
    int                        fragmented;
    double                     fullDurationSeconds; /* the whole timeline, so the init segment shows the full seek bar */
    uint32_t                   sequenceNumber;
    uint64_t                   videoBaseTime; /* baseMediaDecodeTime of the next video fragment */
    uint64_t                   audioBaseTime;
    MuxBuffer                  fragVideoData; /* AVCC sample bytes buffered for the current fragment */
    MuxBuffer                  fragAudioData;
    MuxBuffer                  fragMoof; ///< Borrows the owner's fixed metadata storage.
    byte_t                     fragmentMetadata[FRAGMENT_METADATA_CAPACITY];
    Utl_Mpeg4_FragmentSample_t fragVideo[MAX_FRAGMENT_SAMPLES];
    uint32_t                   fragVideoCount;
    Utl_Mpeg4_FragmentSample_t fragAudio[MAX_FRAGMENT_SAMPLES];
    uint32_t                   fragAudioCount;
};

/*************************************************************************************************/
/**
 * \brief Worker-local layout borrowing the retained samples of one pending fragment.
 */
typedef struct FragmentLayout_s
{
    Utl_Mpeg4_TrackFragment_t tracks[FRAGMENT_TRACK_CAPACITY]; ///< Initialized video and audio tracks.
    Utl_Mpeg4_TrackRun_t      runs[FRAGMENT_TRACK_CAPACITY];   ///< Runs borrowing retained sample arrays.
    Utl_Mpeg4_MovieFragment_t fragment;                        ///< Selected nonempty tracks, referencing this layout.
    Utl_Mpeg4_BoxHeader_t     media;                           ///< Complete following media-data box extent.
    uint64_t                  videoTime;                       ///< Next video decode time; published only after native flush succeeds.
    uint64_t                  audioTime;                       ///< Next audio decode time; published only after native flush succeeds.
    uint32_t                  metadataByteCount;               ///< Validated movie-fragment extent.
    uint32_t                  headerByteCount;                 ///< Validated media-data header extent.
} FragmentLayout_t;

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
 * \brief Writes the MP4 audio elementary-stream descriptor from retained configuration.
 *
 * \par Requirements:
 *  - 001 The mux owner shall retain configuration within its array capacity; an absent configuration shall fail without writing.
 *  - 002 Output shall be disjoint; typed Systems writers shall validate every nested extent.
 *  - 003 Local ES fields and SL predefined value shall follow ISO/IEC 14496-14 section 4.1.2.
 *
 * \param[in]  mux        Required mux owner
 * \param[out] output     Required descriptor storage
 * \param[in]  capacity   Writable descriptor bytes
 *
 * \retval nonzero Complete descriptor bytes.
 * \retval 0 Configuration or serialization failed.
 */
static FORCE_INLINE uint32_t WriteAudioDescriptor(const Mp4Mux* mux, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Appends an mp4a sample description containing a typed esds descriptor.
 *
 * \par Details:
 *  Description appended or sticky failure published.
 *
 * \par Requirements:
 *  - 001 The owner and output buffer shall be valid and exclusively owned by the mux worker.
 *  - 002 Descriptor generation shall finish before any enclosing box is appended.
 *  - 003 Failure shall set the mux's sticky failure state.
 *
 * \param[in,out] mux      Required mux owner
 * \param[in,out] buffer   Required legacy box-assembly buffer
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteAudioSampleEntry(Mp4Mux* mux, MuxBuffer* buffer);

/*************************************************************************************************/
/**
 * \brief Appends version zero and explicit flags through the common FullBox codec.
 *
 * \par Details:
 *  Fields appended.
 *
 * \par Requirements:
 *  - 001 The internal caller shall provide representable flags.
 *  - 002 The buffer shall remain exclusively owned; its legacy growth policy is a separate open defect.
 *
 * \param[in,out] buffer   Required legacy output buffer
 * \param[in]     flags    Box-specific 24-bit flags
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void BufferFullBoxVersionZero(MuxBuffer* buffer, uint32_t flags);

/*************************************************************************************************/
/**
 * \brief Writes the complete file-type record through the common MPEG-4 codecs.
 *
 * \par Requirements:
 *  - 001 The mux shall exclusively own its open output stream.
 *  - 002 The existing mode-specific brand list shall be preserved.
 *  - 003 Serialization shall use fixed scratch storage before the native write.
 *
 * \param[in,out] mux          Required open mux owner
 * \param[in]     fragmented   Selects the existing fragmented brand declaration
 *
 * \retval TRUE Complete record written.
 * \retval FALSE Output write failed; mux failure is latched.
 */
static FORCE_INLINE bool_t WriteFileType(Mp4Mux* mux, bool_t fragmented);

/*************************************************************************************************/
/**
 * \brief Appends one complete bounded span and latches failure.
 *
 * \par Details:
 *  Inspect failed; no partial append is published.
 *
 * \par Requirements:
 *  - 001 Missing required storage shall assert.
 *  - 002 Fixed metadata storage shall never grow.
 *  - 003 Capacity or native allocation failure shall preserve this append and latch later operations as no-ops.
 *  - 004 Legacy payload/progressive growth remains a separate open lifecycle defect.
 *
 * \param[in,out] buffer   Required initialized assembly state
 * \param[in]     data     Disjoint input, nullable only when empty
 * \param[in]     count    Readable input bytes
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void BufferBytes(MuxBuffer* buffer, const void* data, uint32_t count);

/*************************************************************************************************/
/**
 * \brief Replaces a previously reserved complete network integer.
 *
 * \par Details:
 *  Existing field replaced or sticky failure preserved.
 *
 * \par Requirements:
 *  - 001 A failed owner shall remain a no-op without reading any output address.
 *  - 002 An active owner shall contain the reserved slot; violations are internal development errors.
 *
 * \param[in,out] buffer   Required assembly owner
 * \param[in]     offset   Reserved byte offset
 * \param[in]     value    Complete replacement value
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void PatchBuffer32(MuxBuffer* buffer, uint32_t offset, uint32_t value);

/*************************************************************************************************/
/**
 * \brief Finishes a reserved ordinary box without indexing a failed buffer.
 *
 * \par Details:
 *  Box size published or sticky failure retained.
 *
 * \par Requirements:
 *  - 001 The opening prefix shall be present unless failure is already latched.
 *  - 002 All assembly counts are uint32_t; larger layouts require a different writer.
 *
 * \param[in,out] buffer     Required assembly owner
 * \param[in]     position   Previously reserved box start
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void BoxClose(MuxBuffer* buffer, uint32_t position);

/*************************************************************************************************/
/**
 * \brief Binds fixed metadata storage already belonging to the mux owner.
 *
 * \par Details:
 *  Fixed metadata storage bound.
 *
 * \par Requirements:
 *  - 001 The complete owner shall be initialized before this operation.
 *  - 002 No native allocation or resource acquisition shall occur here.
 *
 * \param[in,out] mux   Required exclusively owned mux
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void InitializeFragmentStorage(Mp4Mux* mux);

/*************************************************************************************************/
/**
 * \brief Constructs a moof-relative track with one explicit duration/size run.
 *
 * \par Details:
 *  Known fragment layout initialized.
 *
 * \par Requirements:
 *  - 001 The caller shall provide disjoint initialized-lifetime storage and a positive track ID.
 *  - 002 The file shall advertise iso5 or later compatibility.
 *
 * \param[out] track     Required track model
 * \param[out] run       Required associated run model
 * \param[in]  trackId   Movie track identity
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void InitializeFragmentTrack(Utl_Mpeg4_TrackFragment_t* track,
                                                 Utl_Mpeg4_TrackRun_t*      run,
                                                 uint32_t                   trackId);

/*************************************************************************************************/
/**
 * \brief Checks retained payload extent and derives the bounded fragment duration.
 *
 * \par Requirements:
 *  - 001 Counts shall stay within fixed sample storage; sums widen before addition.
 *  - 002 A mismatched retained payload shall preserve output.
 *
 * \param[out] duration    Required media tick result
 * \param[in]  samples     Stable fixed sample array
 * \param[in]  count       Valid records
 * \param[in]  byteCount   Retained payload bytes
 *
 * \retval TRUE Matching payload and duration published.
 * \retval FALSE Metadata/payload mismatch.
 */
static FORCE_INLINE bool_t GetFragmentDuration(uint64_t*                         duration,
                                               const Utl_Mpeg4_FragmentSample_t* samples,
                                               uint32_t                          count,
                                               uint32_t                          byteCount);

/*************************************************************************************************/
/**
 * \brief Writes a complete native span while preserving sticky failure.
 *
 * \par Requirements:
 *  - 001 Empty data shall perform no native operation.
 *  - 002 A failed mux shall perform no later write.
 *  - 003 Native output remains a synchronous-I/O boundary pending OS migration.
 *
 * \param[in,out] mux     Required initialized mux
 * \param[in]     data    Readable bytes, nullable for zero count
 * \param[in]     count   Exact native span bytes
 *
 * \retval TRUE Complete write or active empty span.
 * \retval FALSE Sticky or native failure.
 */
static FORCE_INLINE bool_t WriteFileBytes(Mp4Mux* mux, const void* data, uint32_t count);
static FORCE_INLINE int64_t GetFileOffset(HANDLE file);
static FORCE_INLINE bool_t SetFileOffset(HANDLE file, int64_t offset);

/*************************************************************************************************/
/**
 * \brief Publishes a fragment video record only after complete payload assembly.
 *
 * \par Requirements:
 *  - 001 The caller shall provide an active mux and stable readable bytes.
 *  - 002 Full sample storage or failed payload assembly shall latch mux failure without advancing the sample count.
 *
 * \param[in,out] mux        Required active mux
 * \param[in]     bytes      Stable Annex-B sample
 * \param[in]     duration   Media duration
 * \param[in]     keyframe   Sync-sample indication
 *
 * \retval TRUE Complete sample retained.
 * \retval FALSE Sticky failure; sample count unchanged.
 */
static FORCE_INLINE bool_t AppendFragmentVideo(Mp4Mux* mux, Utl_Mpeg4_Span_t bytes, uint32_t duration, bool_t keyframe);

/*************************************************************************************************/
/**
 * \brief Publishes a fragment audio record only after complete payload assembly.
 *
 * \par Requirements:
 *  - 001 The caller shall provide an active audio mux and stable readable bytes.
 *  - 002 Capacity or payload failure shall preserve the published sample count.
 *
 * \param[in,out] mux        Required active mux
 * \param[in]     bytes      Stable coded sample
 * \param[in]     duration   Media duration
 *
 * \retval TRUE Complete sample retained.
 * \retval FALSE Sticky failure; sample count unchanged.
 */
static FORCE_INLINE bool_t AppendFragmentAudio(Mp4Mux* mux, Utl_Mpeg4_Span_t bytes, uint32_t duration);

/*************************************************************************************************/
/**
 * \brief Writes a counted NAL with its complete network length prefix.
 *
 * \par Requirements:
 *  - 001 The caller shall validate sample-domain byte-count arithmetic before native writes.
 *  - 002 A partial native write shall latch failure.
 *
 * \param[in,out] mux     Required active mux
 * \param[in]     bytes   Stable complete NAL payload
 *
 * \retval TRUE Prefix and payload written.
 * \retval FALSE Sticky native failure.
 */
static FORCE_INLINE bool_t WriteNal(Mp4Mux* mux, Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Writes the legacy progressive Annex-B sample and then records its complete extent.
 *
 * \par Requirements:
 *  - 001 Count arithmetic and native position shall validate before narrowing.
 *  - 002 All failure paths shall latch mux failure.
 *  - 003 Annex-B specification migration and progressive sample-array allocation remain open.
 *
 * \param[in,out] mux        Required active mux
 * \param[in]     bytes      Stable Annex-B bytes
 * \param[in]     duration   Media duration
 * \param[in]     keyframe   Sync-sample indication
 *
 * \retval TRUE Complete sample and record written.
 * \retval FALSE Sticky failure.
 */
static FORCE_INLINE bool_t WriteProgressiveVideo(Mp4Mux*          mux,
                                                 Utl_Mpeg4_Span_t bytes,
                                                 uint32_t         duration,
                                                 bool_t           keyframe);

/*************************************************************************************************/
/**
 * \brief Writes a progressive audio sample and records its validated native position.
 *
 * \par Requirements:
 *  - 001 Negative native positions shall never become unsigned file offsets.
 *  - 002 Native or record-capacity failure shall latch.
 *
 * \param[in,out] mux        Required active audio mux
 * \param[in]     bytes      Stable sample bytes
 * \param[in]     duration   Media duration
 *
 * \retval TRUE Complete sample recorded.
 * \retval FALSE Sticky failure.
 */
static FORCE_INLINE bool_t WriteProgressiveAudio(Mp4Mux* mux, Utl_Mpeg4_Span_t bytes, uint32_t duration);

/*************************************************************************************************/
/**
 * \brief Binds one worker-local fragment layout to the current retained sample records.
 *
 * \par Details:
 *  Every selected model and derived-output slot initialized.
 *
 * \par Requirements:
 *  - 001 The mux shall have at least one pending sample and exclusively own every borrowed array.
 *  - 002 The layout shall remain at a stable address through serialization; its interior pointers shall not outlive it.
 *
 * \param[out] layout   Required worker-local storage
 * \param[in]  mux      Required stable mux owner
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void InitializeFragmentLayout(FragmentLayout_t* layout, const Mp4Mux* mux);

/*************************************************************************************************/
/**
 * \brief Validates retained payload totals and derives the next sequence and decoding times.
 *
 * \par Requirements:
 *  - 001 The layout shall have been initialized against this same unchanged mux.
 *  - 002 Failure shall leave the mux unchanged; layout scratch shall be discarded.
 *  - 003 Every total shall fit before addition or narrowing; failed payload buffers shall be rejected.
 *
 * \param[in,out] layout   Initialized scratch layout
 * \param[in]     mux      Stable retained samples and current sequence
 *
 * \retval TRUE Next sequence and decode times derived.
 * \retval FALSE Payload, time or sequence validation failed.
 */
static FORCE_INLINE bool_t GetFragmentTimeline(FragmentLayout_t* layout, const Mp4Mux* mux);

/*************************************************************************************************/
/**
 * \brief Derives placement for video then audio payloads following one typed movie fragment.
 *
 * \par Requirements:
 *  - 001 The initialized layout shall borrow stable input throughout preflight and serialization.
 *  - 002 The common MPEG-4 queries shall validate all selected metadata before any output write.
 *  - 003 Signed run offsets and fixed output capacity shall be checked before publication into scratch.
 *
 * \param[in,out] layout   Initialized scratch layout
 * \param[in]     mux      Required retained sample and payload owner
 *
 * \retval TRUE Complete writable layout derived.
 * \retval FALSE Invalid timeline, metadata, capacity or offset; scratch is not publishable.
 */
static FORCE_INLINE bool_t GetFragmentLayout(FragmentLayout_t* layout, const Mp4Mux* mux);

/*************************************************************************************************/
/**
 * \brief Serializes validated metadata before writing a complete pending fragment to the native stream.
 *
 * \par Requirements:
 *  - 001 The layout shall have passed GetFragmentLayout and its borrowed arrays shall remain unchanged.
 *  - 002 No native write shall precede complete metadata serialization.
 *  - 003 A native partial write or flush failure cannot be rolled back; the caller shall latch terminal mux failure.
 *  - 004 This operation shall not advance sequence, times or pending sample counts.
 *
 * \param[in,out] mux      Required exclusive native stream and scratch owner
 * \param[in]     layout   Required prevalidated layout
 *
 * \retval TRUE All bytes and the native flush succeeded.
 * \retval FALSE Serialization or native publication failed.
 */
static FORCE_INLINE bool_t WriteFragmentLayout(Mp4Mux* mux, const FragmentLayout_t* layout);

/*************************************************************************************************/
/**
 * \brief Publishes a successfully flushed fragment and releases its pending logical slots for reuse.
 *
 * \par Details:
 *  Progress advanced and pending counts cleared without allocation.
 *
 * \par Requirements:
 *  - 001 The caller shall have completed WriteFragmentLayout successfully.
 *  - 002 All new sequence and time values shall have been prevalidated before native output.
 *
 * \param[in,out] mux      Required exclusive mux owner
 * \param[in]     layout   Successfully published layout
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void CommitFragment(Mp4Mux* mux, const FragmentLayout_t* layout);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void BufferBytes(MuxBuffer* buffer, const void* data, uint32_t count)
{
    bool_t   has_input;
    bool_t   empty;
    bool_t   valid_input;
    bool_t   fits;
    uint32_t required;
    uint32_t capacity;
    byte_t*  grown;

    LOGASSERT(NULL != buffer);
    has_input   = (NULL != data);
    empty       = (0 == count);
    valid_input = has_input || empty;
    LOGASSERT(valid_input);
    if (!buffer->failed)
    {
        fits           = (count <= UINT32_MAX - buffer->size);
        buffer->failed = !fits;
        if (fits)
        {
            required = buffer->size + count;
            if (buffer->capacity < required)
            {
                if (buffer->fixed)
                {
                    buffer->failed = TRUE;
                }
                else
                {
                    capacity = buffer->capacity;
                    if (0 == capacity)
                    {
                        capacity = FRAGMENT_METADATA_CAPACITY;
                    }
                    for (;;)
                    {
                        if (required <= capacity)
                        {
                            break;
                        }
                        if (UINT32_MAX / 2 < capacity)
                        {
                            capacity = required;
                        }
                        else
                        {
                            capacity *= 2;
                        }
                    }
                    grown          = (byte_t*)realloc(buffer->data, capacity);
                    buffer->failed = (NULL == grown);
                    if (!buffer->failed)
                    {
                        buffer->data     = grown;
                        buffer->capacity = capacity;
                    }
                }
            }
            if (!buffer->failed)
            {
                if (!empty)
                {
                    CRT_Memory_Copy(&buffer->data[buffer->size], data, count);
                    buffer->size = required;
                }
            }
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void BufferU8(MuxBuffer* pBuffer, uint8_t value)
{
    BufferBytes(pBuffer, &value, 1);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Appends a 16-bit integer in big-endian byte order.
 */
static void BufferU16(MuxBuffer* pBuffer, uint16_t value)
{
    byte_t bytes[2];

    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
    BufferBytes(pBuffer, bytes, 2);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Appends a 32-bit integer in big-endian byte order.
 */
static void BufferU32(MuxBuffer* pBuffer, uint32_t value)
{
    byte_t bytes[4];

    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
    BufferBytes(pBuffer, bytes, 4);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Appends the high and low 32-bit words in big-endian order.
 */
static void BufferU64(MuxBuffer* pBuffer, uint64_t value)
{
    BufferU32(pBuffer, (uint32_t)(value >> 32));
    BufferU32(pBuffer, (uint32_t)value);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void BufferFullBoxVersionZero(MuxBuffer* buffer, uint32_t flags)
{
    Utl_Mpeg4_FullBoxFields_t fields;
    uint32_t                  count;
    byte_t                    bytes[UTL_MPEG4_FULLBOX_FIELDS_BYTES];

    LOGASSERT(NULL != buffer);
    fields.version = 0;
    fields.flags   = flags;
    count          = Utl_Mpeg4_WriteFullBoxFields(&fields, bytes, sizeof(bytes));
    LOGASSERT(UTL_MPEG4_FULLBOX_FIELDS_BYTES == count);
    BufferBytes(buffer, bytes, count);
}

/* Opens a box with a placeholder size, returning its start offset for BoxClose. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Writes a placeholder size and box type, returning the offset for backpatching.
 */
static FORCE_INLINE uint32_t BoxOpen(MuxBuffer* pBuffer, const char* pType)
{
    uint32_t position;

    position = pBuffer->size;
    BufferU32(pBuffer, 0);
    BufferBytes(pBuffer, pType, 4);
    return position;
}

/* Backpatches a box's size field from its start offset to the current end. */
/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void BoxClose(MuxBuffer* buffer, uint32_t position)
{
    uint32_t count;

    LOGASSERT(NULL != buffer);
    if (!buffer->failed)
    {
        LOGASSERT(position <= buffer->size);
        count = buffer->size - position;
        LOGASSERT(UTL_MPEG4_BOX_HEADER_BYTES <= count);
        PatchBuffer32(buffer, position, count);
    }
}

/* The 3x3 video transform matrix (identity in 16.16 / 2.30 fixed point). */
/*************************************************************************************************/
/**
 * \par Details:
 *  Appends the identity video transform in the required 16.16 and 2.30 fields.
 */
static void BufferMatrix(MuxBuffer* pBuffer)
{
    BufferU32(pBuffer, 0x00010000);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0x00010000);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0x40000000);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t WriteFileBytes(Mp4Mux* mux, const void* data, uint32_t count)
{
    bool_t valid;
    DWORD  written;

    LOGASSERT(NULL != mux);
    valid = !mux->failed;
    if (valid)
    {
        if (0 < count)
        {
            LOGASSERT(NULL != data);
            written     = 0;
            valid       = !!WriteFile(mux->file, data, count, &written, NULL);
            valid       = valid && (count == written);
            mux->failed = !valid;
        }
    }
    return valid;
}

/*************************************************************************************************/
/** \par Details: Reads the current 64-bit native output position. */
static FORCE_INLINE int64_t GetFileOffset(HANDLE file)
{
    LARGE_INTEGER distance;
    LARGE_INTEGER position;
    bool_t        valid;

    distance.QuadPart = 0;
    valid = !!SetFilePointerEx(file, distance, &position, FILE_CURRENT);
    if (!valid)
    {
        return -1;
    }
    return position.QuadPart;
}

/*************************************************************************************************/
/** \par Details: Moves the native output to an existing absolute position. */
static FORCE_INLINE bool_t SetFileOffset(HANDLE file, int64_t offset)
{
    LARGE_INTEGER distance;

    LOGASSERT(0 <= offset);
    distance.QuadPart = offset;
    return !!SetFilePointerEx(file, distance, NULL, FILE_BEGIN);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t WriteFileType(Mp4Mux* mux, bool_t fragmented)
{
    Utl_Mpeg4_BoxHeader_t header;
    Utl_Mpeg4_FileType_t  file_type;
    uint32_t              header_count;
    byte_t                bytes[FILE_TYPE_BYTE_COUNT];
    uint32_t              payload_count;
    bool_t                written;

    LOGASSERT(NULL != mux);
    Utl_Mpeg4_InitializeBoxHeader(&header, UTL_MPEG4_BOX_FTYP);
    header.byteCount                     = FILE_TYPE_BYTE_COUNT;
    file_type.majorBrand                 = UTL_MPEG4_BRAND_ISOM;
    file_type.minorVersion               = FILE_TYPE_MINOR_VERSION;
    file_type.compatibleBrands.data      = (const byte_t*)FILE_TYPE_PROGRESSIVE_BRANDS;
    file_type.compatibleBrands.byteCount = sizeof(FILE_TYPE_PROGRESSIVE_BRANDS) - 1;
    if (fragmented)
    {
        file_type.majorBrand                 = UTL_MPEG4_BRAND_ISO5;
        file_type.compatibleBrands.data      = (const byte_t*)FILE_TYPE_FRAGMENTED_BRANDS;
        file_type.compatibleBrands.byteCount = sizeof(FILE_TYPE_FRAGMENTED_BRANDS) - 1;
    }
    header_count = Utl_Mpeg4_WriteBoxHeader(&header, bytes, sizeof(bytes));
    LOGASSERT(UTL_MPEG4_BOX_HEADER_BYTES == header_count);
    payload_count = Utl_Mpeg4_WriteFileType(&file_type, &bytes[header_count], sizeof(bytes) - header_count);
    LOGASSERT(FILE_TYPE_BYTE_COUNT - UTL_MPEG4_BOX_HEADER_BYTES == payload_count);
    written = WriteFileBytes(mux, bytes, sizeof(bytes));
    return written;
}

/* Drops a leading Annex-B start code (00 00 01 or 00 00 00 01) from a parameter set, copying the
** raw NAL bytes into a fixed buffer. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Removes an optional three- or four-byte Annex B start code and copies the
 *  raw NAL into fixed parameter storage when it fits.
 */
static void StoreParameterSet(const byte_t* pData, uint32_t size, byte_t* pOut, uint32_t* pOutSize)
{
    uint32_t start;

    start = 0;
    if (size >= 4 && 0 == pData[0] && 0 == pData[1] && 0 == pData[2] && 1 == pData[3])
    {
        start = 4;
    }
    else if (size >= 3 && 0 == pData[0] && 0 == pData[1] && 1 == pData[2])
    {
        start = 3;
    }
    if (size - start > MAX_PARAM_BYTES)
    {
        *pOutSize = 0;
        return;
    }
    memcpy(pOut, pData + start, size - start);
    *pOutSize = size - start;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Grows the sample array when full and returns the newly occupied entry.
 */
static MuxSample* AppendSample(MuxSample** ppArray, uint32_t* pCount, uint32_t* pCapacity)
{
    if (*pCount == *pCapacity)
    {
        uint32_t   new_capacity;
        MuxSample* grown;

        new_capacity = *pCapacity ? *pCapacity * 2u : 4096u;
        grown        = (MuxSample*)realloc(*ppArray, (size_t)new_capacity * sizeof(MuxSample));
        if (NULL == grown)
        {
            return NULL;
        }
        *ppArray   = grown;
        *pCapacity = new_capacity;
    }
    return &(*ppArray)[(*pCount)++];
}

/* Rewrites Annex-B NAL units to 4-byte length-prefixed form, appended to a growable buffer.
** Returns the number of bytes appended. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Scans Annex B NAL boundaries and appends four-byte length prefixes with raw
 *  NAL payloads, stripping trailing zero bytes before the next start code.
 */
static uint32_t AppendAvcc(MuxBuffer* pBuffer, const byte_t* pData, uint32_t size)
{
    uint32_t original;
    uint32_t total;
    uint32_t i;
    uint32_t nal_start;

    original  = pBuffer->size;
    total     = 0;
    i         = 0;
    nal_start = 0;
    for (;;)
    {
        if (3 > (size - i))
        {
            break;
        }
        if (0 == pData[i] && 0 == pData[i + 1] && 1 == pData[i + 2])
        {
            if (nal_start > 0 && i > nal_start)
            {
                uint32_t nal_length;

                nal_length = i - nal_start;
                for (;;)
                {
                    if (0 == nal_length)
                    {
                        break;
                    }
                    if (0 != pData[nal_start + nal_length - 1])
                    {
                        break;
                    }
                    --nal_length;
                }
                BufferU32(pBuffer, nal_length);
                BufferBytes(pBuffer, pData + nal_start, nal_length);
            }
            i         += 3;
            nal_start = i;
        }
        else
        {
            ++i;
        }
    }
    if (nal_start > 0 && nal_start < size)
    {
        BufferU32(pBuffer, size - nal_start);
        BufferBytes(pBuffer, pData + nal_start, size - nal_start);
    }
    if (!pBuffer->failed)
    {
        total = pBuffer->size - original;
    }
    return total;
}

/* Emits stts/stsc/stsz/co64 (and stss for video) for one track's sample list. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Run-length encodes durations and writes one-sample-per-chunk sizes/offsets.
 *  Video also receives a one-based sync-sample table.
 */
static void WriteSampleTables(MuxBuffer* pBuffer, const MuxSample* pSamples, uint32_t count, int isVideo)
{
    uint32_t box;
    uint32_t entry_count_position;
    uint32_t entries;
    uint32_t i;
    uint32_t run_count;

    /* stts: run-length of equal durations. */
    box = BoxOpen(pBuffer, "stts");
    BufferFullBoxVersionZero(pBuffer, 0);
    entry_count_position = pBuffer->size;
    BufferU32(pBuffer, 0);
    entries = 0;
    i       = 0;
    for (;;)
    {
        if (i == count)
        {
            break;
        }
        for (run_count = 1; run_count < (count - i); ++run_count)
        {
            if (pSamples[i + run_count].duration != pSamples[i].duration)
            {
                break;
            }
        }
        BufferU32(pBuffer, run_count);
        BufferU32(pBuffer, pSamples[i].duration);
        ++entries;
        i += run_count;
    }
    PatchBuffer32(pBuffer, entry_count_position, entries);
    BoxClose(pBuffer, box);

    /* stsc: one sample per chunk. */
    box = BoxOpen(pBuffer, "stsc");
    BufferFullBoxVersionZero(pBuffer, 0);
    BufferU32(pBuffer, 1);
    BufferU32(pBuffer, 1);
    BufferU32(pBuffer, 1);
    BufferU32(pBuffer, 1);
    BoxClose(pBuffer, box);

    /* stsz: explicit per-sample sizes. */
    box = BoxOpen(pBuffer, "stsz");
    BufferFullBoxVersionZero(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, count);
    for (i = 0; i < count; ++i)
    {
        BufferU32(pBuffer, pSamples[i].size);
    }
    BoxClose(pBuffer, box);

    /* co64: one 64-bit chunk offset per sample. */
    box = BoxOpen(pBuffer, "co64");
    BufferFullBoxVersionZero(pBuffer, 0);
    BufferU32(pBuffer, count);
    for (i = 0; i < count; ++i)
    {
        BufferU64(pBuffer, pSamples[i].offset);
    }
    BoxClose(pBuffer, box);

    /* stss: sync-sample list for video (1-based sample numbers). */
    if (isVideo)
    {
        uint32_t sync_count_position;
        uint32_t sync_count;

        box = BoxOpen(pBuffer, "stss");
        BufferFullBoxVersionZero(pBuffer, 0);
        sync_count_position = pBuffer->size;
        BufferU32(pBuffer, 0);
        sync_count = 0;
        for (i = 0; i < count; ++i)
        {
            if (pSamples[i].keyframe)
            {
                BufferU32(pBuffer, i + 1u);
                ++sync_count;
            }
        }
        PatchBuffer32(pBuffer, sync_count_position, sync_count);
        BoxClose(pBuffer, box);
    }
}

/* avc1 sample entry with an avcC configuration record built from the stored SPS/PPS. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Writes avc1 and its avcC child from stored dimensions and SPS/PPS bytes,
 *  using four-byte NAL length prefixes.
 */
static void WriteVideoSampleEntry(Mp4Mux* pMux, MuxBuffer* pBuffer)
{
    uint32_t box;
    uint32_t avcc;

    box = BoxOpen(pBuffer, "stsd");
    BufferFullBoxVersionZero(pBuffer, 0);
    BufferU32(pBuffer, 1);
    {
        uint32_t avc1;

        avc1 = BoxOpen(pBuffer, "avc1");
        BufferU8(pBuffer, 0);
        BufferU8(pBuffer, 0);
        BufferU8(pBuffer, 0);
        BufferU8(pBuffer, 0);
        BufferU8(pBuffer, 0);
        BufferU8(pBuffer, 0);
        BufferU16(pBuffer, 1); /* data reference index */
        BufferU16(pBuffer, 0);
        BufferU16(pBuffer, 0);
        BufferU32(pBuffer, 0);
        BufferU32(pBuffer, 0);
        BufferU32(pBuffer, 0);
        BufferU16(pBuffer, (uint16_t)pMux->width);
        BufferU16(pBuffer, (uint16_t)pMux->height);
        BufferU32(pBuffer, 0x00480000); /* 72 dpi */
        BufferU32(pBuffer, 0x00480000);
        BufferU32(pBuffer, 0);
        BufferU16(pBuffer, 1); /* frame count */
        {
            byte_t compressor[32];

            memset(compressor, 0, sizeof(compressor));
            BufferBytes(pBuffer, compressor, sizeof(compressor));
        }
        BufferU16(pBuffer, 0x0018); /* depth */
        BufferU16(pBuffer, 0xFFFF); /* predefined */

        avcc = BoxOpen(pBuffer, "avcC");
        BufferU8(pBuffer, 1);                                       /* configuration version */
        BufferU8(pBuffer, pMux->spsSize > 1 ? pMux->sps[1] : 0x4D); /* AVCProfileIndication */
        BufferU8(pBuffer, pMux->spsSize > 2 ? pMux->sps[2] : 0x00); /* profile compatibility */
        BufferU8(pBuffer, pMux->spsSize > 3 ? pMux->sps[3] : 0x1F); /* AVCLevelIndication */
        BufferU8(pBuffer, 0xFF);                                    /* 6 bits reserved | lengthSizeMinusOne = 3 */
        BufferU8(pBuffer, 0xE1);                                    /* 3 bits reserved | numSPS = 1 */
        BufferU16(pBuffer, (uint16_t)pMux->spsSize);
        BufferBytes(pBuffer, pMux->sps, pMux->spsSize);
        BufferU8(pBuffer, 1); /* numPPS */
        BufferU16(pBuffer, (uint16_t)pMux->ppsSize);
        BufferBytes(pBuffer, pMux->pps, pMux->ppsSize);
        BoxClose(pBuffer, avcc);
        BoxClose(pBuffer, avc1);
    }
    BoxClose(pBuffer, box);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Fixed scratch arrays hold complete children before their parent is written.
 *  AudioSpecificConfig is validated and serialized by the Audio module first.
 *  These specification stages use fixed storage and the local CRT_ library.
 */
static FORCE_INLINE uint32_t WriteAudioDescriptor(const Mp4Mux* mux, byte_t* output, uint32_t capacity)
{
    bool_t                              has_configuration;
    uint32_t                            written;
    bool_t                              valid_configuration;
    Utl_Mpeg4_Span_t                    audio_bytes;
    Utl_Mpeg4Audio_Status_t             audio_status;
    Utl_Mpeg4Audio_Config_t             configuration;
    uint32_t                            configuration_count;
    byte_t                              encoded_configuration[AUDIO_CONFIGURATION_CAPACITY];
    Utl_Mpeg4Systems_Descriptor_t       descriptor;
    uint32_t                            decoder_child_count;
    byte_t                              decoder_children[AUDIO_DESCRIPTOR_CAPACITY];
    Utl_Mpeg4Systems_DecoderConfig_t    decoder;
    uint32_t                            decoder_count;
    byte_t                              stream_children[AUDIO_DESCRIPTOR_CAPACITY];
    byte_t                              synchronization;
    uint32_t                            synchronization_count;
    Utl_Mpeg4Systems_ElementaryStream_t stream;

    LOGASSERT(NULL != mux);
    LOGASSERT(NULL != output);
    LOGASSERT(mux->ascSize <= sizeof(mux->asc));
    has_configuration   = (0 < mux->ascSize);
    written             = 0;
    valid_configuration = FALSE;
    if (has_configuration)
    {
        audio_bytes.data      = mux->asc;
        audio_bytes.byteCount = mux->ascSize;
        audio_status          = Utl_Mpeg4Audio_ReadConfig(&configuration, audio_bytes);
        valid_configuration   = (UTL_MPEG4AUDIO_COMPLETE == audio_status);
    }
    if (valid_configuration)
    {
        configuration_count =
            Utl_Mpeg4Audio_WriteConfig(&configuration, encoded_configuration, sizeof(encoded_configuration));
        LOGASSERT(0 < configuration_count);
        descriptor.tag               = UTL_MPEG4SYSTEMS_TAG_DECODER_INFO;
        descriptor.payload.data      = encoded_configuration;
        descriptor.payload.byteCount = configuration_count;
        decoder_child_count          = Utl_Mpeg4Systems_WriteDescriptor(&descriptor, decoder_children, sizeof(decoder_children));
        LOGASSERT(0 < decoder_child_count);
        decoder.objectType         = UTL_MPEG4SYSTEMS_OBJECT_AUDIO;
        decoder.streamType         = UTL_MPEG4SYSTEMS_STREAM_AUDIO;
        decoder.upstream           = FALSE;
        decoder.bufferBytes        = 0;
        decoder.maximumBitrate     = 0;
        decoder.averageBitrate     = 0;
        decoder.children.data      = decoder_children;
        decoder.children.byteCount = decoder_child_count;
        decoder_count              = Utl_Mpeg4Systems_WriteDecoderConfig(&decoder, stream_children, sizeof(stream_children));
        LOGASSERT(0 < decoder_count);
        synchronization              = UTL_MPEG4SYSTEMS_SL_MP4;
        descriptor.tag               = UTL_MPEG4SYSTEMS_TAG_SL_CONFIG;
        descriptor.payload.data      = &synchronization;
        descriptor.payload.byteCount = sizeof(synchronization);
        synchronization_count        = Utl_Mpeg4Systems_WriteDescriptor(&descriptor,
                                                                        &stream_children[decoder_count],
                                                                        sizeof(stream_children) - decoder_count);
        LOGASSERT(0 < synchronization_count);
        stream.streamId           = 0;
        stream.priority           = 0;
        stream.hasDependency      = FALSE;
        stream.hasUrl             = FALSE;
        stream.hasClock           = FALSE;
        stream.children.data      = stream_children;
        stream.children.byteCount = decoder_count + synchronization_count;
        written                   = Utl_Mpeg4Systems_WriteElementaryStream(&stream, output, capacity);
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The descriptor no longer assumes one-byte expandable lengths. Box assembly
 *  still uses the legacy growable buffer and remains an allocation-lifecycle defect.
 */
static FORCE_INLINE void WriteAudioSampleEntry(Mp4Mux* mux, MuxBuffer* buffer)
{
    uint32_t descriptor_count;
    byte_t   descriptor[AUDIO_DESCRIPTOR_CAPACITY];
    uint32_t box;
    uint32_t entry;
    uint32_t index;
    uint32_t esds;

    descriptor_count = WriteAudioDescriptor(mux, descriptor, sizeof(descriptor));
    if (0 < descriptor_count)
    {
        box = BoxOpen(buffer, "stsd");
        BufferFullBoxVersionZero(buffer, 0);
        BufferU32(buffer, 1);
        entry = BoxOpen(buffer, "mp4a");
        for (index = 0; index < AUDIO_RESERVED_BYTES; ++index)
        {
            BufferU8(buffer, 0);
        }
        BufferU16(buffer, AUDIO_DATA_REFERENCE);
        BufferU32(buffer, 0);
        BufferU32(buffer, 0);
        BufferU16(buffer, (uint16_t)mux->audioChannels);
        BufferU16(buffer, AUDIO_SAMPLE_BITS);
        BufferU16(buffer, 0);
        BufferU16(buffer, 0);
        BufferU32(buffer, mux->audioSampleRate << AUDIO_RATE_SHIFT);
        esds = BoxOpen(buffer, "esds");
        BufferFullBoxVersionZero(buffer, 0);
        BufferBytes(buffer, descriptor, descriptor_count);
        BoxClose(buffer, esds);
        BoxClose(buffer, entry);
        BoxClose(buffer, box);
    }
    else
    {
        mux->failed = TRUE;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Sums sample durations into the track's 64-bit tick count.
 */
static uint64_t GetTrackDuration(const MuxSample* pSamples, uint32_t count)
{
    uint64_t total;
    uint32_t i;

    total = 0;
    for (i = 0; i < count; ++i)
    {
        total += pSamples[i].duration;
    }
    return total;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Builds track/media headers, data references, sample description and tables
 *  for the selected video or audio track.
 */
static void WriteTrack(Mp4Mux* pMux, MuxBuffer* pBuffer, int isVideo, uint32_t movieTimescale)
{
    const MuxSample* p_samples;
    uint32_t         count;
    uint32_t         timescale;
    uint64_t         track_duration;
    uint32_t         movie_duration;
    uint32_t         trak;
    uint32_t         box;
    uint32_t         mdia;
    uint32_t         minf;

    if (isVideo)
    {
        p_samples = pMux->video;
        count     = pMux->videoCount;
        timescale = pMux->videoTimescale;
    }
    else
    {
        p_samples = pMux->audio;
        count     = pMux->audioCount;
        timescale = pMux->audioTimescale ? pMux->audioTimescale : pMux->audioSampleRate;
    }
    track_duration = GetTrackDuration(p_samples, count);
    movie_duration = timescale ? (uint32_t)(track_duration * movieTimescale / timescale) : 0;

    trak = BoxOpen(pBuffer, "trak");

    box = BoxOpen(pBuffer, "tkhd");
    BufferU32(pBuffer, 0x00000007); /* enabled | in movie | in preview */
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, isVideo ? 1u : 2u);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, movie_duration);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU16(pBuffer, 0);                      /* layer */
    BufferU16(pBuffer, 0);                      /* alternate group */
    BufferU16(pBuffer, isVideo ? 0u : 0x0100u); /* volume */
    BufferU16(pBuffer, 0);
    BufferMatrix(pBuffer);
    BufferU32(pBuffer, isVideo ? (pMux->width << 16) : 0u);
    BufferU32(pBuffer, isVideo ? (pMux->height << 16) : 0u);
    BoxClose(pBuffer, box);

    mdia = BoxOpen(pBuffer, "mdia");
    box  = BoxOpen(pBuffer, "mdhd");
    BufferFullBoxVersionZero(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, timescale);
    BufferU32(pBuffer, (uint32_t)track_duration);
    BufferU16(pBuffer, 0x55C4); /* language 'und' */
    BufferU16(pBuffer, 0);
    BoxClose(pBuffer, box);

    box = BoxOpen(pBuffer, "hdlr");
    BufferFullBoxVersionZero(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferBytes(pBuffer, isVideo ? "vide" : "soun", 4);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferU32(pBuffer, 0);
    BufferBytes(pBuffer, isVideo ? "VideoHandler" : "SoundHandler", isVideo ? 13u : 13u);
    BoxClose(pBuffer, box);

    minf = BoxOpen(pBuffer, "minf");
    if (isVideo)
    {
        box = BoxOpen(pBuffer, "vmhd");
        BufferU32(pBuffer, 0x00000001);
        BufferU32(pBuffer, 0);
        BufferU16(pBuffer, 0);
        BoxClose(pBuffer, box);
    }
    else
    {
        box = BoxOpen(pBuffer, "smhd");
        BufferFullBoxVersionZero(pBuffer, 0);
        BufferU16(pBuffer, 0);
        BufferU16(pBuffer, 0);
        BoxClose(pBuffer, box);
    }

    box = BoxOpen(pBuffer, "dinf");
    {
        uint32_t dref;
        uint32_t url;

        dref = BoxOpen(pBuffer, "dref");
        BufferFullBoxVersionZero(pBuffer, 0);
        BufferU32(pBuffer, 1);
        url = BoxOpen(pBuffer, "url ");
        BufferU32(pBuffer, 0x00000001); /* self-contained */
        BoxClose(pBuffer, url);
        BoxClose(pBuffer, dref);
    }
    BoxClose(pBuffer, box);

    box = BoxOpen(pBuffer, "stbl");
    if (isVideo)
    {
        WriteVideoSampleEntry(pMux, pBuffer);
    }
    else
    {
        WriteAudioSampleEntry(pMux, pBuffer);
    }
    WriteSampleTables(pBuffer, p_samples, count, isVideo);
    BoxClose(pBuffer, box);

    BoxClose(pBuffer, minf);
    BoxClose(pBuffer, mdia);
    BoxClose(pBuffer, trak);
}

/* trex (track extends) inside mvex: per-track defaults for the fragments that follow. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Appends per-track fragment defaults, leaving duration and size to the
 *  subsequent fragment sample records.
 */
static void WriteTrex(MuxBuffer* pBuffer, uint32_t trackId, uint32_t defaultSampleFlags)
{
    uint32_t box;

    box = BoxOpen(pBuffer, "trex");
    BufferFullBoxVersionZero(pBuffer, 0);
    BufferU32(pBuffer, trackId);
    BufferU32(pBuffer, 1); /* default sample description index */
    BufferU32(pBuffer, 0); /* default sample duration */
    BufferU32(pBuffer, 0); /* default sample size */
    BufferU32(pBuffer, defaultSampleFlags);
    BoxClose(pBuffer, box);
}

/* Writes the fragmented-MP4 init segment: ftyp then a moov whose tracks carry empty sample tables
** and an mvex declaring the movie extends into the fragments that follow. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Writes ftyp and moov with empty sample tables and mvex/trex defaults,
 *  including the configured overall duration when available.
 */
static int WriteFragmentedInit(Mp4Mux* pMux)
{
    uint32_t movie_timescale;
    uint32_t movie_duration;
    uint32_t moov;
    bool_t   ready;
    bool_t   has_storage;
    bool_t   can_write;
    bool_t   written;

    if (!WriteFileType(pMux, TRUE))
    {
        return 0;
    }

    movie_timescale     = 1000;
    movie_duration      = (uint32_t)(pMux->fullDurationSeconds * movie_timescale + 0.5);
    pMux->fragMoof.size = 0;
    moov                = BoxOpen(&pMux->fragMoof, "moov");
    {
        uint32_t mvhd;

        mvhd = BoxOpen(&pMux->fragMoof, "mvhd");
        BufferFullBoxVersionZero(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, movie_timescale);
        BufferU32(&pMux->fragMoof, movie_duration);
        BufferU32(&pMux->fragMoof, 0x00010000);
        BufferU16(&pMux->fragMoof, 0x0100);
        BufferU16(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferMatrix(&pMux->fragMoof);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, 0);
        BufferU32(&pMux->fragMoof, (pMux->hasAudio ? 3u : 2u));
        BoxClose(&pMux->fragMoof, mvhd);
    }
    WriteTrack(pMux, &pMux->fragMoof, 1, movie_timescale); /* empty tables: videoCount is 0 */
    if (pMux->hasAudio)
    {
        WriteTrack(pMux, &pMux->fragMoof, 0, movie_timescale);
    }
    {
        uint32_t mvex;

        mvex = BoxOpen(&pMux->fragMoof, "mvex");
        {
            uint32_t mehd;

            mehd = BoxOpen(&pMux->fragMoof, "mehd");
            BufferU32(&pMux->fragMoof, 0);
            BufferU32(&pMux->fragMoof, movie_duration);
            BoxClose(&pMux->fragMoof, mehd);
        }
        WriteTrex(&pMux->fragMoof, 1, 0x01010000); /* video defaults to a non-sync sample */
        if (pMux->hasAudio)
        {
            WriteTrex(&pMux->fragMoof, 2, 0x02000000);
        }
        BoxClose(&pMux->fragMoof, mvex);
    }
    BoxClose(&pMux->fragMoof, moov);
    pMux->failed = pMux->failed || pMux->fragMoof.failed;
    ready        = !pMux->failed;
    has_storage  = (NULL != pMux->fragMoof.data);
    can_write    = ready && has_storage;
    written      = FALSE;
    if (can_write)
    {
        written = WriteFileBytes(pMux, pMux->fragMoof.data, pMux->fragMoof.size);
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void PatchBuffer32(MuxBuffer* buffer, uint32_t offset, uint32_t value)
{
    Utl_Mpeg4_Integer_t field;
    uint32_t            written;

    LOGASSERT(NULL != buffer);
    if (!buffer->failed)
    {
        LOGASSERT(offset <= buffer->size);
        LOGASSERT(MUX_INTEGER32_BYTES <= buffer->size - offset);
        field.value     = value;
        field.byteCount = MUX_INTEGER32_BYTES;
        written         = Utl_Mpeg4_WriteUnsigned(field, &buffer->data[offset], MUX_INTEGER32_BYTES);
        LOGASSERT(MUX_INTEGER32_BYTES == written);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void InitializeFragmentStorage(Mp4Mux* mux)
{
    LOGASSERT(NULL != mux);
    mux->fragMoof.data     = mux->fragmentMetadata;
    mux->fragMoof.size     = 0;
    mux->fragMoof.capacity = sizeof(mux->fragmentMetadata);
    mux->fragMoof.fixed    = TRUE;
    mux->fragMoof.failed   = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void InitializeFragmentTrack(Utl_Mpeg4_TrackFragment_t* track,
                                                 Utl_Mpeg4_TrackRun_t*      run,
                                                 uint32_t                   trackId)
{
    Utl_Mpeg4_InitializeTrackRun(run);
    run->flags = UTL_MPEG4_TRUN_DATA_OFFSET | UTL_MPEG4_TRUN_DURATION | UTL_MPEG4_TRUN_SIZE;
    Utl_Mpeg4_InitializeTrackFragment(track, trackId);
    track->flags             = UTL_MPEG4_TFHD_MOOF_BASE;
    track->hasDecodeTime     = TRUE;
    track->decodeTimeVersion = 1;
    track->runs              = run;
    track->runCount          = 1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t GetFragmentDuration(uint64_t*                         duration,
                                               const Utl_Mpeg4_FragmentSample_t* samples,
                                               uint32_t                          count,
                                               uint32_t                          byteCount)
{
    uint64_t bytes;
    uint64_t ticks;
    uint32_t index;
    bool_t   valid;

    LOGASSERT(NULL != duration);
    LOGASSERT(NULL != samples);
    LOGASSERT(count <= MAX_FRAGMENT_SAMPLES);
    bytes = 0;
    ticks = 0;
    for (index = 0; index < count; ++index)
    {
        bytes += samples[index].byteCount;
        ticks += samples[index].duration;
    }
    valid = (bytes == byteCount);
    if (valid)
    {
        *duration = ticks;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t AppendFragmentVideo(Mp4Mux* mux, Utl_Mpeg4_Span_t bytes, uint32_t duration, bool_t keyframe)
{
    bool_t                      valid;
    uint32_t                    count;
    bool_t                      has_bytes;
    bool_t                      payload_valid;
    Utl_Mpeg4_FragmentSample_t* sample;

    valid = (mux->fragVideoCount < MAX_FRAGMENT_SAMPLES);
    if (valid)
    {
        count         = AppendAvcc(&mux->fragVideoData, bytes.data, bytes.byteCount);
        has_bytes     = (0 < count);
        payload_valid = !mux->fragVideoData.failed;
        valid         = has_bytes && payload_valid;
        if (valid)
        {
            sample            = &mux->fragVideo[mux->fragVideoCount];
            sample->byteCount = count;
            sample->duration  = duration;
            sample->flags     = UTL_MPEG4_SAMPLE_DEPENDENT;
            if (keyframe)
            {
                sample->flags = UTL_MPEG4_SAMPLE_INDEPENDENT;
            }
            sample->compositionOffset = 0;
            ++mux->fragVideoCount;
        }
    }
    mux->failed = !valid;
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t AppendFragmentAudio(Mp4Mux* mux, Utl_Mpeg4_Span_t bytes, uint32_t duration)
{
    bool_t                      valid;
    Utl_Mpeg4_FragmentSample_t* sample;

    valid = (mux->fragAudioCount < MAX_FRAGMENT_SAMPLES);
    if (valid)
    {
        BufferBytes(&mux->fragAudioData, bytes.data, bytes.byteCount);
        valid = !mux->fragAudioData.failed;
        if (valid)
        {
            sample                    = &mux->fragAudio[mux->fragAudioCount];
            sample->byteCount         = bytes.byteCount;
            sample->duration          = duration;
            sample->flags             = UTL_MPEG4_SAMPLE_INDEPENDENT;
            sample->compositionOffset = 0;
            ++mux->fragAudioCount;
        }
    }
    mux->failed = !valid;
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t WriteNal(Mp4Mux* mux, Utl_Mpeg4_Span_t bytes)
{
    Utl_Mpeg4_Integer_t field;
    uint32_t            count;
    byte_t              prefix[MUX_INTEGER32_BYTES];
    bool_t              valid;

    field.value     = bytes.byteCount;
    field.byteCount = MUX_INTEGER32_BYTES;
    count           = Utl_Mpeg4_WriteUnsigned(field, prefix, sizeof(prefix));
    LOGASSERT(MUX_INTEGER32_BYTES == count);
    valid = WriteFileBytes(mux, prefix, count);
    if (valid)
    {
        valid = WriteFileBytes(mux, bytes.data, bytes.byteCount);
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t WriteProgressiveVideo(Mp4Mux*          mux,
                                                 Utl_Mpeg4_Span_t bytes,
                                                 uint32_t         duration,
                                                 bool_t           keyframe)
{
    int64_t          offset;
    bool_t           valid;
    uint32_t         total;
    uint32_t         index;
    uint32_t         start;
    bool_t           first_zero;
    bool_t           second_zero;
    bool_t           has_stop;
    bool_t           is_start;
    bool_t           has_start;
    bool_t           has_body;
    bool_t           has_nal;
    Utl_Mpeg4_Span_t nal;
    uint64_t         next_count;
    MuxSample*       sample;

    offset = GetFileOffset(mux->file);
    valid  = (0 <= offset);
    total  = 0;
    index  = 0;
    start  = 0;
    for (;;)
    {
        if (!valid)
        {
            break;
        }
        if (bytes.byteCount - index < ANNEXB_PREFIX_BYTES)
        {
            break;
        }
        first_zero  = (ANNEXB_ZERO_BYTE == bytes.data[index]);
        second_zero = (ANNEXB_ZERO_BYTE == bytes.data[index + 1]);
        has_stop    = (ANNEXB_STOP_BYTE == bytes.data[index + 2]);
        is_start    = first_zero && second_zero && has_stop;
        if (is_start)
        {
            has_start = (0 < start);
            has_body  = (start < index);
            has_nal   = has_start && has_body;
            if (has_nal)
            {
                nal.data      = &bytes.data[start];
                nal.byteCount = index - start;
                for (;;)
                {
                    if (0 == nal.byteCount)
                    {
                        break;
                    }
                    if (ANNEXB_ZERO_BYTE != nal.data[nal.byteCount - 1])
                    {
                        break;
                    }
                    --nal.byteCount;
                }
                next_count = (uint64_t)total + MUX_INTEGER32_BYTES + nal.byteCount;
                valid      = (next_count <= UINT32_MAX);
                if (valid)
                {
                    valid = WriteNal(mux, nal);
                    total = (uint32_t)next_count;
                }
            }
            index += ANNEXB_PREFIX_BYTES;
            start = index;
        }
        else
        {
            ++index;
        }
    }
    has_start = (0 < start);
    has_body  = (start < bytes.byteCount);
    has_nal   = valid && has_start && has_body;
    if (has_nal)
    {
        nal.data      = &bytes.data[start];
        nal.byteCount = bytes.byteCount - start;
        next_count    = (uint64_t)total + MUX_INTEGER32_BYTES + nal.byteCount;
        valid         = (next_count <= UINT32_MAX);
        if (valid)
        {
            valid = WriteNal(mux, nal);
            total = (uint32_t)next_count;
        }
    }
    has_body = (0 < total);
    valid    = valid && has_body;
    if (valid)
    {
        sample = AppendSample(&mux->video, &mux->videoCount, &mux->videoCapacity);
        valid  = (NULL != sample);
        if (valid)
        {
            sample->offset   = (uint64_t)offset;
            sample->size     = total;
            sample->duration = duration;
            sample->keyframe = keyframe;
        }
    }
    mux->failed = !valid;
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t WriteProgressiveAudio(Mp4Mux* mux, Utl_Mpeg4_Span_t bytes, uint32_t duration)
{
    int64_t    offset;
    bool_t     valid;
    MuxSample* sample;

    offset = GetFileOffset(mux->file);
    valid  = (0 <= offset);
    if (valid)
    {
        valid = WriteFileBytes(mux, bytes.data, bytes.byteCount);
    }
    if (valid)
    {
        sample = AppendSample(&mux->audio, &mux->audioCount, &mux->audioCapacity);
        valid  = (NULL != sample);
        if (valid)
        {
            sample->offset   = (uint64_t)offset;
            sample->size     = bytes.byteCount;
            sample->duration = duration;
            sample->keyframe = TRUE;
        }
    }
    mux->failed = !valid;
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void InitializeFragmentLayout(FragmentLayout_t* layout, const Mp4Mux* mux)
{
    bool_t   has_video;
    bool_t   has_audio;
    bool_t   has_samples;
    bool_t   has_both;
    uint32_t track_count;

    LOGASSERT(NULL != layout);
    LOGASSERT(NULL != mux);
    has_video   = (0 < mux->fragVideoCount);
    has_audio   = (0 < mux->fragAudioCount);
    has_samples = has_video || has_audio;
    has_both    = has_video && has_audio;
    LOGASSERT(has_samples);
    InitializeFragmentTrack(&layout->tracks[0], &layout->runs[0], 1);
    layout->tracks[0].decodeTime = mux->videoBaseTime;
    layout->runs[0].flags        |= UTL_MPEG4_TRUN_FLAGS;
    layout->runs[0].samples      = mux->fragVideo;
    layout->runs[0].sampleCount  = mux->fragVideoCount;
    InitializeFragmentTrack(&layout->tracks[1], &layout->runs[1], 2);
    layout->tracks[1].decodeTime = mux->audioBaseTime;
    layout->runs[1].samples      = mux->fragAudio;
    layout->runs[1].sampleCount  = mux->fragAudioCount;
    track_count                  = 1;
    if (has_both)
    {
        track_count = FRAGMENT_TRACK_CAPACITY;
    }
    Utl_Mpeg4_InitializeMovieFragment(&layout->fragment, layout->tracks, track_count);
    if (!has_video)
    {
        layout->fragment.tracks = &layout->tracks[1];
    }
    Utl_Mpeg4_InitializeBoxHeader(&layout->media, UTL_MPEG4_BOX_MDAT);
    layout->videoTime         = mux->videoBaseTime;
    layout->audioTime         = mux->audioBaseTime;
    layout->metadataByteCount = 0;
    layout->headerByteCount   = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t GetFragmentTimeline(FragmentLayout_t* layout, const Mp4Mux* mux)
{
    uint64_t video_ticks;
    uint64_t audio_ticks;
    bool_t   video_valid;
    bool_t   audio_valid;
    bool_t   video_fits;
    bool_t   audio_fits;
    bool_t   sequence_fits;
    bool_t   video_buffer_valid;
    bool_t   audio_buffer_valid;
    bool_t   metadata_valid;
    bool_t   valid;

    LOGASSERT(NULL != layout);
    LOGASSERT(NULL != mux);
    video_ticks        = 0;
    audio_ticks        = 0;
    video_valid        = GetFragmentDuration(&video_ticks, mux->fragVideo, mux->fragVideoCount, mux->fragVideoData.size);
    audio_valid        = GetFragmentDuration(&audio_ticks, mux->fragAudio, mux->fragAudioCount, mux->fragAudioData.size);
    video_fits         = (video_ticks <= UINT64_MAX - mux->videoBaseTime);
    audio_fits         = (audio_ticks <= UINT64_MAX - mux->audioBaseTime);
    sequence_fits      = (mux->sequenceNumber < UINT32_MAX);
    video_buffer_valid = !mux->fragVideoData.failed;
    audio_buffer_valid = !mux->fragAudioData.failed;
    metadata_valid     = !mux->fragMoof.failed;
    valid              = video_valid && audio_valid && video_fits && audio_fits && sequence_fits;
    valid              = valid && video_buffer_valid && audio_buffer_valid && metadata_valid;
    if (valid)
    {
        layout->fragment.sequenceNumber = mux->sequenceNumber + 1;
        layout->videoTime               = mux->videoBaseTime + video_ticks;
        layout->audioTime               = mux->audioBaseTime + audio_ticks;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t GetFragmentLayout(FragmentLayout_t* layout, const Mp4Mux* mux)
{
    bool_t   valid;
    uint64_t payload_bytes;
    bool_t   header_valid;
    bool_t   metadata_valid;
    bool_t   metadata_fits;
    uint64_t video_offset;
    uint64_t audio_offset;
    bool_t   video_fits;
    bool_t   audio_fits;

    LOGASSERT(NULL != layout);
    LOGASSERT(NULL != mux);
    valid = GetFragmentTimeline(layout, mux);
    if (valid)
    {
        payload_bytes           = (uint64_t)mux->fragVideoData.size + mux->fragAudioData.size;
        layout->media.byteCount = UTL_MPEG4_BOX_HEADER_BYTES + payload_bytes;
        if (UINT32_MAX < layout->media.byteCount)
        {
            layout->media.hasLargeSize = TRUE;
            layout->media.byteCount    = UTL_MPEG4_LARGE_BOX_HEADER_BYTES + payload_bytes;
        }
        layout->headerByteCount   = Utl_Mpeg4_GetBoxHeaderByteCount(&layout->media);
        layout->metadataByteCount = Utl_Mpeg4_GetMovieFragmentByteCount(&layout->fragment);
        header_valid              = (0 < layout->headerByteCount);
        metadata_valid            = (0 < layout->metadataByteCount);
        metadata_fits             = (layout->metadataByteCount <= sizeof(mux->fragmentMetadata));
        valid                     = header_valid && metadata_valid && metadata_fits;
        video_offset              = (uint64_t)layout->metadataByteCount + layout->headerByteCount;
        audio_offset              = video_offset + mux->fragVideoData.size;
        video_fits                = (video_offset <= INT32_MAX);
        audio_fits                = TRUE;
        if (0 < mux->fragAudioCount)
        {
            audio_fits = (audio_offset <= INT32_MAX);
        }
        valid = valid && video_fits && audio_fits;
        if (valid)
        {
            layout->runs[0].dataOffset = (int32_t)video_offset;
            if (0 < mux->fragAudioCount)
            {
                layout->runs[1].dataOffset = (int32_t)audio_offset;
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
static FORCE_INLINE bool_t WriteFragmentLayout(Mp4Mux* mux, const FragmentLayout_t* layout)
{
    uint32_t written;
    bool_t   valid;
    byte_t   header[UTL_MPEG4_MAX_BOX_HEADER_BYTES];

    LOGASSERT(NULL != mux);
    LOGASSERT(NULL != layout);
    written = Utl_Mpeg4_WriteMovieFragment(&layout->fragment, mux->fragmentMetadata, sizeof(mux->fragmentMetadata));
    valid   = (layout->metadataByteCount == written);
    if (valid)
    {
        written = Utl_Mpeg4_WriteBoxHeader(&layout->media, header, sizeof(header));
        LOGASSERT(layout->headerByteCount == written);
        valid = WriteFileBytes(mux, mux->fragmentMetadata, layout->metadataByteCount);
    }
    if (valid)
    {
        valid = WriteFileBytes(mux, header, layout->headerByteCount);
    }
    if (valid)
    {
        valid = WriteFileBytes(mux, mux->fragVideoData.data, mux->fragVideoData.size);
    }
    if (valid)
    {
        valid = WriteFileBytes(mux, mux->fragAudioData.data, mux->fragAudioData.size);
    }
    if (valid)
    {
        valid = !!FlushFileBuffers(mux->file);
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CommitFragment(Mp4Mux* mux, const FragmentLayout_t* layout)
{
    LOGASSERT(NULL != mux);
    LOGASSERT(NULL != layout);
    mux->sequenceNumber     = layout->fragment.sequenceNumber;
    mux->videoBaseTime      = layout->videoTime;
    mux->audioBaseTime      = layout->audioTime;
    mux->fragVideoCount     = 0;
    mux->fragAudioCount     = 0;
    mux->fragVideoData.size = 0;
    mux->fragAudioData.size = 0;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Allocates the mux owner, opens shared-read output and writes the file-type box
 *  followed by an mdat header whose size will be patched during finalization.
 */
Mp4Mux* Mp4Mux_Create(const TCHAR* pPath,
                      unsigned       width,
                      unsigned       height,
                      const byte_t*  pSps,
                      uint32_t       spsSize,
                      const byte_t*  pPps,
                      uint32_t       ppsSize,
                      unsigned       videoTimescale,
                      int            hasAudio,
                      unsigned       audioTimescale,
                      unsigned       audioSampleRate,
                      unsigned       audioChannels,
                      const byte_t*  pAsc,
                      uint32_t       ascSize)
{
    Mp4Mux* pMux;
    byte_t  mdat_header[8];

    pMux = (Mp4Mux*)calloc(1, sizeof(*pMux));
    if (NULL == pMux)
    {
        return NULL;
    }
    InitializeFragmentStorage(pMux);
    pMux->file = CreateFile(pPath,
                             GENERIC_WRITE,
                             FILE_SHARE_READ,
                             NULL,
                             CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
    if (INVALID_HANDLE_VALUE == pMux->file)
    {
        free(pMux);
        return NULL;
    }
    pMux->width           = width;
    pMux->height          = height;
    pMux->videoTimescale  = videoTimescale ? videoTimescale : 90000u;
    pMux->hasAudio        = hasAudio;
    pMux->audioTimescale  = audioTimescale;
    pMux->audioSampleRate = audioSampleRate;
    pMux->audioChannels   = audioChannels;
    StoreParameterSet(pSps, spsSize, pMux->sps, &pMux->spsSize);
    StoreParameterSet(pPps, ppsSize, pMux->pps, &pMux->ppsSize);
    if (hasAudio && NULL != pAsc && ascSize > 0 && ascSize <= sizeof(pMux->asc))
    {
        memcpy(pMux->asc, pAsc, ascSize);
        pMux->ascSize = ascSize;
    }

    if (!WriteFileType(pMux, FALSE))
    {
        Mp4Mux_Destroy(pMux);
        return NULL;
    }

    /* mdat: size patched on finish; samples stream in after this header. */
    pMux->mdatStart = GetFileOffset(pMux->file);
    memset(mdat_header, 0, sizeof(mdat_header));
    memcpy(mdat_header + 4, "mdat", 4);
    if (!WriteFileBytes(pMux, mdat_header, sizeof(mdat_header)))
    {
        Mp4Mux_Destroy(pMux);
        return NULL;
    }
    return pMux;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Allocates fragment state, stores codec configuration and writes the init
 *  segment before returning shared-read output.
 */
Mp4Mux* Mp4Mux_CreateFragmented(const TCHAR* pPath,
                                unsigned       width,
                                unsigned       height,
                                const byte_t*  pSps,
                                uint32_t       spsSize,
                                const byte_t*  pPps,
                                uint32_t       ppsSize,
                                unsigned       videoTimescale,
                                int            hasAudio,
                                unsigned       audioTimescale,
                                unsigned       audioSampleRate,
                                unsigned       audioChannels,
                                const byte_t*  pAsc,
                                uint32_t       ascSize,
                                double         fullDurationSeconds)
{
    Mp4Mux* pMux;

    pMux = (Mp4Mux*)calloc(1, sizeof(*pMux));
    if (NULL == pMux)
    {
        return NULL;
    }
    InitializeFragmentStorage(pMux);
    pMux->file = CreateFile(pPath,
                             GENERIC_WRITE,
                             FILE_SHARE_READ,
                             NULL,
                             CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
    if (INVALID_HANDLE_VALUE == pMux->file)
    {
        free(pMux);
        return NULL;
    }
    pMux->fragmented          = 1;
    pMux->fullDurationSeconds = fullDurationSeconds;
    pMux->width               = width;
    pMux->height              = height;
    pMux->videoTimescale      = videoTimescale ? videoTimescale : 90000u;
    pMux->hasAudio            = hasAudio;
    pMux->audioTimescale      = audioTimescale;
    pMux->audioSampleRate     = audioSampleRate;
    pMux->audioChannels       = audioChannels;
    StoreParameterSet(pSps, spsSize, pMux->sps, &pMux->spsSize);
    StoreParameterSet(pPps, ppsSize, pMux->pps, &pMux->ppsSize);
    if (hasAudio && NULL != pAsc && ascSize > 0 && ascSize <= sizeof(pMux->asc))
    {
        memcpy(pMux->asc, pAsc, ascSize);
        pMux->ascSize = ascSize;
    }
    if (!WriteFragmentedInit(pMux))
    {
        Mp4Mux_Destroy(pMux);
        return NULL;
    }
    (void)FlushFileBuffers(pMux->file);
    return pMux;
}

/* Writes one movie fragment (moof + mdat) for the samples buffered since the last flush, appended to
** the file and flushed to disk so a progressive server can hand it out immediately. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Preflights every retained record, derived size, signed data offset, decode-time addition and sequence increment.
 *  Metadata uses fixed owner storage; payload buffers and native output still need lifecycle migration.
 */
int Mp4Mux_FlushFragment(Mp4Mux* pMux)
{
    bool_t           has_video;
    bool_t           has_audio;
    bool_t           has_samples;
    bool_t           healthy;
    bool_t           fragmented;
    bool_t           active;
    bool_t           valid;
    FragmentLayout_t layout;

    LOGASSERT(NULL != pMux);
    has_video   = (0 < pMux->fragVideoCount);
    has_audio   = (0 < pMux->fragAudioCount);
    has_samples = has_video || has_audio;
    healthy     = !pMux->failed;
    fragmented  = pMux->fragmented;
    active      = healthy && fragmented && has_samples;
    valid       = FALSE;
    if (active)
    {
        InitializeFragmentLayout(&layout, pMux);
        valid = GetFragmentLayout(&layout, pMux);
        if (valid)
        {
            valid = WriteFragmentLayout(pMux, &layout);
        }
        if (valid)
        {
            CommitFragment(pMux, &layout);
        }
        pMux->failed = !valid;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Routes a complete video sample through the selected output mode.
 *  Payload failures latch before sample-count publication.
 */
int Mp4Mux_AddVideo(Mp4Mux* pMux, const byte_t* pData, uint32_t size, uint32_t duration, int keyframe)
{
    bool_t           has_input;
    bool_t           empty;
    bool_t           valid_input;
    bool_t           valid;
    Utl_Mpeg4_Span_t bytes;

    LOGASSERT(NULL != pMux);
    has_input   = (NULL != pData);
    empty       = (0 == size);
    valid_input = has_input || empty;
    LOGASSERT(valid_input);
    valid = !pMux->failed;
    if (valid)
    {
        bytes.data      = pData;
        bytes.byteCount = size;
        if (pMux->fragmented)
        {
            valid = AppendFragmentVideo(pMux, bytes, duration, keyframe);
        }
        else
        {
            valid = WriteProgressiveVideo(pMux, bytes, duration, keyframe);
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Routes a complete audio sample through the selected output mode.
 *  Payload failures latch before sample-count publication.
 */
int Mp4Mux_AddAudio(Mp4Mux* pMux, const byte_t* pData, uint32_t size, uint32_t duration)
{
    bool_t           has_input;
    bool_t           empty;
    bool_t           valid_input;
    bool_t           valid;
    Utl_Mpeg4_Span_t bytes;

    LOGASSERT(NULL != pMux);
    has_input   = (NULL != pData);
    empty       = (0 == size);
    valid_input = has_input || empty;
    LOGASSERT(valid_input);
    valid = !pMux->failed && pMux->hasAudio;
    if (valid)
    {
        bytes.data      = pData;
        bytes.byteCount = size;
        if (pMux->fragmented)
        {
            valid = AppendFragmentAudio(pMux, bytes, duration);
        }
        else
        {
            valid = WriteProgressiveAudio(pMux, bytes, duration);
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Flushes trailing fragments in fragmented mode. Ordinary mode patches mdat
 *  size and appends movie metadata and completed sample tables.
 */
int Mp4Mux_Finish(Mp4Mux* pMux)
{
    int64_t   mdat_end;
    uint64_t  mdat_size;
    byte_t    patch[4];
    MuxBuffer buffer;
    uint32_t  movie_timescale;
    uint64_t  video_seconds_scaled;
    uint64_t  audio_seconds_scaled;
    uint32_t  movie_duration;
    uint32_t  moov;
    int       ok;

    if (pMux->fragmented)
    {
        (void)Mp4Mux_FlushFragment(pMux); /* emit any trailing samples */
        ok = !!FlushFileBuffers(pMux->file);
        return ok && (!pMux->failed);
    }
    if (pMux->failed || 0 == pMux->videoCount)
    {
        return 0;
    }

    /* Patch the streamed mdat's size field. */
    mdat_end  = GetFileOffset(pMux->file);
    if ((0 > mdat_end) || (mdat_end < pMux->mdatStart))
    {
        return 0;
    }
    mdat_size = (uint64_t)(mdat_end - pMux->mdatStart);
    if (mdat_size > 0xFFFFFFFFu)
    {
        return 0; /* the proxy is not expected to exceed 4 GiB */
    }
    if (!SetFileOffset(pMux->file, pMux->mdatStart))
    {
        return 0;
    }
    patch[0] = (uint8_t)(mdat_size >> 24);
    patch[1] = (uint8_t)(mdat_size >> 16);
    patch[2] = (uint8_t)(mdat_size >> 8);
    patch[3] = (uint8_t)mdat_size;
    if ((!WriteFileBytes(pMux, patch, 4)) || (!SetFileOffset(pMux->file, mdat_end)))
    {
        return 0;
    }

    memset(&buffer, 0, sizeof(buffer));
    movie_timescale = 1000;
    video_seconds_scaled =
        pMux->videoTimescale ? GetTrackDuration(pMux->video, pMux->videoCount) * movie_timescale / pMux->videoTimescale
                             : 0;
    audio_seconds_scaled = (pMux->hasAudio && pMux->audioTimescale) ? GetTrackDuration(pMux->audio, pMux->audioCount) *
                                                                          movie_timescale / pMux->audioTimescale
                                                                    : 0;
    movie_duration =
        (uint32_t)(video_seconds_scaled > audio_seconds_scaled ? video_seconds_scaled : audio_seconds_scaled);

    moov = BoxOpen(&buffer, "moov");
    {
        uint32_t mvhd;

        mvhd = BoxOpen(&buffer, "mvhd");
        BufferFullBoxVersionZero(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, movie_timescale);
        BufferU32(&buffer, movie_duration);
        BufferU32(&buffer, 0x00010000); /* rate 1.0 */
        BufferU16(&buffer, 0x0100);     /* volume 1.0 */
        BufferU16(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferMatrix(&buffer);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, 0);
        BufferU32(&buffer, (pMux->hasAudio && pMux->audioCount > 0) ? 3u : 2u); /* next track ID */
        BoxClose(&buffer, mvhd);
    }
    WriteTrack(pMux, &buffer, 1, movie_timescale);
    if (pMux->hasAudio && pMux->audioCount > 0)
    {
        WriteTrack(pMux, &buffer, 0, movie_timescale);
    }
    BoxClose(&buffer, moov);

    pMux->failed = pMux->failed || buffer.failed;
    ok           = !pMux->failed && (NULL != buffer.data) && WriteFileBytes(pMux, buffer.data, buffer.size);
    free(buffer.data);
    return ok && !pMux->failed;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Closes output and frees ordinary sample arrays, fragment buffers and owner
 *  storage. NULL is accepted.
 */
void Mp4Mux_Destroy(Mp4Mux* pMux)
{
    if (NULL == pMux)
    {
        return;
    }
    if ((NULL != pMux->file) && (INVALID_HANDLE_VALUE != pMux->file))
    {
        (void)CloseHandle(pMux->file);
    }
    free(pMux->video);
    free(pMux->audio);
    free(pMux->fragVideoData.data);
    free(pMux->fragAudioData.data);
    free(pMux);
}
