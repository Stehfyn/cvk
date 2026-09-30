/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * RFC 9559, sections 5, 10 and 11: Matroska metadata, blocks and timestamps.
 * https://www.rfc-editor.org/rfc/rfc9559.html
 * This adapter retains native file mapping; startup-owned I/O remains open.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h> // IWYU pragma: keep
#include <stdint.h>
#include <minwindef.h>
#include <memoryapi.h>
#include <handleapi.h>
#include <WinBase.h>
#include <fileapi.h>
#include <float.h>
#include <winnt.h>
#include "mkv_subtitles.h"
#include "os/os_types.h"
#include "utl/utl_matroska.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "crt/crt_memory.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define MKV_MAX_CUE_BYTES          (UINT64_C(1024) * UINT64_C(1024))  ///< Maximum accepted cue payload.
#define MKV_BITS_PER_BYTE          (8)            ///< Bits in an EBML octet.
#define MKV_TERMINATOR             ('\0')         ///< Metadata string terminator.
#define MKV_CODEC_CAPACITY         (64)           ///< Terminated codec identifier capacity.
#define MKV_TRACK_INVALID          (-1)           ///< Malformed track result.
#define MKV_TRACK_SUPPORTED        (1)            ///< Supported text track result.
#define MKV_BYTE_RADIX             (256)          ///< Big-endian timestamp byte radix.
#define MKV_SIGNED_TIME_FIRST      (32768)        ///< First negative two-byte timestamp representation.
#define MKV_TIME_MODULUS           (65536)        ///< Two-byte timestamp representation modulus.
#define MKV_NANOSECONDS_PER_SECOND (1000000000.0) ///< Nanosecond conversion factor.
#define MKV_MAX_CUE_SECONDS        (36000000.0)   ///< Application timeline limit.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Native mapped input and its explicitly tracked resource identities.
 */
typedef struct Reader_s
{
    HANDLE        file;    ///< INVALID_HANDLE_VALUE when no file is owned.
    HANDLE        mapping; ///< NULL when no mapping handle is owned.
    const byte_t* data;    ///< Borrowed view address; NULL when unmapped.
    uint64_t      size;    ///< Mapped readable byte count.
} Reader_t;

/*************************************************************************************************/
/**
 * \brief Bounded element payload within the mapped input.
 */
typedef struct Element_s
{
    uint32_t id;      ///< Encoded element ID.
    uint64_t data;    ///< First payload byte offset.
    uint64_t end;     ///< Exclusive payload limit.
    bool_t   unknown; ///< The encoded size was unknown.
} Element_t;

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
 * \brief Releases each acquired mapping resource and clears successful releases.
 *
 * \par Requirements:
 *  - 001 A repeated close after successful release shall be a no-op.
 *  - 002 A failed native release shall retain its resource identity for retry.
 *
 * \param[in,out] reader   Initialized reader, including after partial open
 *
 * \retval        TRUE    Every acquired resource was released.
 * \retval        FALSE   At least one release failed; its identity remains in reader.
 */
static FORCE_INLINE bool_t CloseReader(Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Initializes a native read-only file view.
 *
 * \par Requirements:
 *  - 001 The output shall be initialized before any fallible resource acquisition.
 *  - 002 The caller shall invoke CloseReader after either result.
 *
 * \param[in]  path     Required terminated native file path
 * \param[out] reader   Required reader storage
 *
 * \retval     TRUE    Mapped view is readable.
 * \retval     FALSE   Setup failed; reader still requires CloseReader.
 */
static FORCE_INLINE bool_t OpenReader(const TCHAR* path, Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads one bounded variable integer and advances a cursor only on success.
 *
 * \par Requirements:
 *  - 001 All required input pointers and counted extents shall be valid for this operation.
 *
 * \param[in]     data      Readable input bytes
 * \param[in]     limit     Readable extent
 * \param[in,out] cursor    Current input offset
 * \param[out]    value     Decoded result, preserved on failure
 * \param[in]     id        Retain the EBML ID marker when TRUE
 * \param[out]    unknown   Optional unknown-size result
 *
 * \retval        TRUE    A complete permitted VINT was decoded.
 * \retval        FALSE   Cursor, encoding or width is invalid.
 */
static FORCE_INLINE bool_t
Vint(const byte_t* data, uint64_t limit, uint64_t* cursor, uint64_t* value, bool_t id, bool_t* unknown);

/*************************************************************************************************/
/**
 * \brief Reads one bounded Matroska element header.
 *
 * \par Requirements:
 *  - 001 All required input pointers and counted extents shall be valid for this operation.
 *
 * \param[in]  reader    Live mapped input
 * \param[in]  offset    Element header offset
 * \param[in]  limit     Parent end offset
 * \param[out] element   Result preserved on failure
 *
 * \retval     TRUE    Header and payload bounds are valid.
 * \retval     FALSE   Invalid bounds, header or unknown-size element kind.
 */
static FORCE_INLINE bool_t ReadElement(const Reader_t* reader, uint64_t offset, uint64_t limit, Element_t* element);

/*************************************************************************************************/
/**
 * \brief Reads an unsigned EBML payload of at most eight bytes.
 *
 * \par Requirements:
 *  - 001 All required input pointers and counted extents shall be valid for this operation.
 *
 * \param[in]  reader    Live mapped input
 * \param[in]  element   Previously bounded element
 * \param[out] value     Decoded integer; preserved on failure
 *
 * \retval     TRUE    Integer decoded, including zero for an empty payload.
 * \retval     FALSE   Payload exceeds the integer width.
 */
static FORCE_INLINE bool_t Uint(const Reader_t* reader, const Element_t* element, uint64_t* value);

/*************************************************************************************************/
/**
 * \brief Copies bounded metadata into a terminated character buffer.
 *
 * \par Requirements:
 *  - 001 All required input pointers and counted extents shall be valid for this operation.
 *
 * \param[in]  reader     Live mapped input
 * \param[in]  element    Previously bounded element
 * \param[out] out        Required writable destination
 * \param[in]  capacity   Positive destination byte count
 *
 * \retval     TRUE   Terminated metadata copied, truncated when necessary.
 */
static FORCE_INLINE bool_t String(const Reader_t* reader, const Element_t* element, char* out, uint64_t capacity);

/*************************************************************************************************/
/**
 * \brief Locates Segment after a valid first EBML element.
 *
 * \par Requirements:
 *  - 001 All required input pointers and counted extents shall be valid for this operation.
 *
 * \param[in]  reader    Live mapped input
 * \param[out] segment   Result preserved on failure
 *
 * \retval     TRUE    Segment located.
 * \retval     FALSE   Invalid header, malformed traversal or missing Segment.
 */
static FORCE_INLINE bool_t Segment(const Reader_t* reader, Element_t* segment);

/*************************************************************************************************/
/**
 * \brief Reads text-track metadata and classifies codec support.
 *
 * \par Requirements:
 *  - 001 All required input pointers and counted extents shall be valid for this operation.
 *
 * \param[in]  reader   Live mapped input
 * \param[in]  entry    Bounded TrackEntry
 * \param[out] track    Candidate metadata; use only for a supported result
 *
 * \retval     MKV_TRACK_INVALID     Malformed entry or invalid timing scale.
 * \retval     0                     Well-formed entry with unsupported type or codec.
 * \retval     MKV_TRACK_SUPPORTED   Supported UTF-8 subtitle entry.
 */
static FORCE_INLINE int32_t Track(const Reader_t* reader, const Element_t* entry, MkvSubtitleTrack* track);

/*************************************************************************************************/
/**
 * \brief Reads supported track metadata and Segment timestamp scale.
 *
 * \par Requirements:
 *  - 001 All required input pointers and counted extents shall be valid for this operation.
 *
 * \param[in]  reader           Live mapped input
 * \param[in]  segment          Bounded Segment
 * \param[in]  selectedTrack    Optional required track number; NULL collects all supported tracks
 * \param[out] tracks           Writable slots or NULL at zero capacity
 * \param[in]  capacity         Writable record count
 * \param[out] count            Number of records; zero on malformed metadata
 * \param[out] timestampScale   Segment tick size in nanoseconds
 *
 * \retval     TRUE    Track metadata read.
 * \retval     FALSE   Missing Tracks or malformed metadata; count is zero.
 */
static FORCE_INLINE bool_t Metadata(const Reader_t*   reader,
                                    const Element_t*  segment,
                                    const uint64_t*   selectedTrack,
                                    MkvSubtitleTrack* tracks,
                                    uint32_t          capacity,
                                    uint32_t*         count,
                                    uint64_t*         timestampScale);

/*************************************************************************************************/
/**
 * \brief Delivers a selected un-laced text block with scaled cue timing.
 *
 * \par Requirements:
 *  - 001 All required input pointers and counted extents shall be valid for this operation.
 *
 * \param[in]     reader        Live mapped input
 * \param[in]     block         Bounded Block or SimpleBlock
 * \param[in]     track         Selected supported track
 * \param[in]     clusterTime   Cluster timestamp in Segment ticks
 * \param[in]     scale         Nanoseconds per Segment tick
 * \param[in]     duration      Block duration in Track ticks, or zero when absent
 * \param[in]     callback      Required synchronous cue consumer
 * \param[in,out] user          Caller context
 *
 * \retval        TRUE    Block delivered or belongs to an unselected track.
 * \retval        FALSE   Malformed block, unsupported timing or callback rejection.
 */
static FORCE_INLINE bool_t EmitBlock(const Reader_t*         reader,
                                     const Element_t*        block,
                                     const MkvSubtitleTrack* track,
                                     uint64_t                clusterTime,
                                     uint64_t                scale,
                                     uint64_t                duration,
                                     MkvSubtitleProc         callback,
                                     void*                   user);

/*************************************************************************************************/
/**
 * \brief Identifies Segment children that terminate an unknown-sized cluster.
 *
 * \par Requirements:
 *  - 001 All required input pointers and counted extents shall be valid for this operation.
 *
 * \param[in] id   Encoded EBML element ID
 *
 * \retval    TRUE    Recognized Segment child.
 * \retval    FALSE   Other element.
 */
static FORCE_INLINE bool_t TopLevel(uint32_t id);

/*************************************************************************************************/
/**
 * \brief Traverses bounded cluster children and delivers selected cues.
 *
 * \par Requirements:
 *  - 001 All required input pointers and counted extents shall be valid for this operation.
 *
 * \param[in]     reader     Live mapped input
 * \param[in]     cluster    Bounded Cluster
 * \param[in]     track      Selected supported track
 * \param[in]     scale      Nanoseconds per Segment tick
 * \param[in]     callback   Required synchronous cue consumer
 * \param[in,out] user       Caller context
 * \param[out]    next       Next Segment child offset
 *
 * \retval        TRUE    Cluster traversal completed.
 * \retval        FALSE   Malformed child, unsupported cue or callback rejection.
 */
static FORCE_INLINE bool_t Cluster(const Reader_t*         reader,
                                   const Element_t*        cluster,
                                   const MkvSubtitleTrack* track,
                                   uint64_t                scale,
                                   MkvSubtitleProc         callback,
                                   void*                   user,
                                   uint64_t*               next);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Unmaps the file view and closes the mapping and file handles that were opened.
 */
static FORCE_INLINE bool_t CloseReader(Reader_t* reader)
{
    bool_t is_valid;
    bool_t released;

    is_valid = TRUE;
    if (NULL != reader->data)
    {
        released = UnmapViewOfFile(reader->data);
        is_valid = released;
        if (released)
        {
            reader->data = NULL;
            reader->size = 0;
        }
    }
    if (NULL != reader->mapping)
    {
        released = CloseHandle(reader->mapping);
        is_valid = is_valid && released;
        if (released)
        {
            reader->mapping = NULL;
        }
    }
    if (INVALID_HANDLE_VALUE != reader->file)
    {
        released = CloseHandle(reader->file);
        is_valid = is_valid && released;
        if (released)
        {
            reader->file = INVALID_HANDLE_VALUE;
        }
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Opens a read-only mapped view so traversal can skip media payloads by length.
 *  The caller always invokes CloseReader, including after partial setup failure.
 */
static FORCE_INLINE bool_t OpenReader(const TCHAR* path, Reader_t* reader)
{
    LARGE_INTEGER size;
    bool_t        is_valid;

    reader->file    = INVALID_HANDLE_VALUE;
    reader->mapping = NULL;
    reader->data    = NULL;
    reader->size    = 0;
    reader->file    = CreateFile(path,
                                 GENERIC_READ,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 NULL,
                                 OPEN_EXISTING,
                                 FILE_FLAG_RANDOM_ACCESS,
                                 NULL);
    is_valid        = (INVALID_HANDLE_VALUE != reader->file);
    if (is_valid)
    {
        is_valid = GetFileSizeEx(reader->file, &size);
        if (is_valid)
        {
            is_valid = (0 < size.QuadPart) && ((uint64_t)size.QuadPart <= UINTPTR_MAX);
        }
        if (is_valid)
        {
            reader->size    = (uint64_t)size.QuadPart;
            reader->mapping = CreateFileMapping(reader->file, NULL, PAGE_READONLY, 0, 0, NULL);
            is_valid        = (NULL != reader->mapping);
        }
        if (is_valid)
        {
            reader->data = (const byte_t*)MapViewOfFile(reader->mapping, FILE_MAP_READ, 0, 0, 0);
            is_valid     = (NULL != reader->data);
        }
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Adapts the counted VINT reader to the legacy cursor interface, preserving the
 *  marker for IDs and reporting the unknown-size pattern for sizes.
 */
static FORCE_INLINE bool_t
Vint(const byte_t* data, uint64_t limit, uint64_t* cursor, uint64_t* value, bool_t id, bool_t* unknown)
{
    bool_t              cursor_in_bounds;
    bool_t              is_valid;
    Utl_Matroska_Vint_t result;

    cursor_in_bounds = ((*cursor) < limit);
    is_valid         = cursor_in_bounds;
    if (is_valid)
    {
        is_valid = Utl_Matroska_ReadVint(&result, data + (*cursor), limit - (*cursor));
        if (is_valid)
        {
            if (id)
            {
                is_valid = (UTL_MATROSKA_MAX_ID_BYTES >= result.byteCount);
            }
        }
        if (is_valid)
        {
            (*value) = result.value;
            if (id)
            {
                (*value) = result.encodedValue;
            }
            (*cursor) += result.byteCount;
            if (NULL != unknown)
            {
                (*unknown) = (!id) && result.allDataBitsSet;
            }
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Validates parent bounds through the element-header library. Unknown sizes
 *  are accepted only for Segment and Cluster by this application adapter.
 */
static FORCE_INLINE bool_t ReadElement(const Reader_t* reader, uint64_t offset, uint64_t limit, Element_t* element)
{
    Utl_Matroska_ElementHeader_t header;
    bool_t                       valid_limit;
    bool_t                       valid_offset;
    bool_t                       is_valid;
    bool_t                       is_segment;
    bool_t                       is_cluster;
    bool_t                       unknown_allowed;

    header.id               = 0;
    header.headerByteCount  = 0;
    header.sizeByteCount    = 0;
    header.payloadByteCount = 0;
    header.unknownSize      = FALSE;
    valid_limit             = (limit <= reader->size);
    valid_offset            = (offset <= limit);
    is_valid                = valid_limit && valid_offset;
    if (is_valid)
    {
        is_valid = Utl_Matroska_ReadElementHeader(&header, reader->data + offset, limit - offset, limit - offset);
    }
    if (is_valid)
    {
        is_segment      = (UTL_MATROSKA_ID_SEGMENT == header.id);
        is_cluster      = (UTL_MATROSKA_ID_CLUSTER == header.id);
        unknown_allowed = (!header.unknownSize) || is_segment || is_cluster;
        is_valid        = unknown_allowed;
        if (is_valid)
        {
            element->id      = header.id;
            element->data    = offset + header.headerByteCount;
            element->unknown = header.unknownSize;
            element->end     = limit;
            if (!header.unknownSize)
            {
                element->end = element->data + header.payloadByteCount;
            }
        }
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Accumulates up to eight big-endian bytes into an unsigned value.
 */
static FORCE_INLINE bool_t Uint(const Reader_t* reader, const Element_t* element, uint64_t* value)
{
    uint64_t length;
    bool_t   is_valid;
    uint64_t result;
    uint64_t index;

    length   = element->end - element->data;
    is_valid = (length <= sizeof(uint64_t));
    result   = 0;
    if (is_valid)
    {
        for (index = element->data; index < element->end; ++index)
        {
            result = (result << MKV_BITS_PER_BYTE) | reader->data[index];
        }
        (*value) = result;
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Copies element bytes into a terminated destination, truncating to its capacity.
 */
static FORCE_INLINE bool_t String(const Reader_t* reader, const Element_t* element, char* out, uint64_t capacity)
{
    uint64_t length;

    LOGASSERT(NULL != out);
    LOGASSERT(0 < capacity);

    length = element->end - element->data;
    if (capacity <= length)
    {
        length = capacity - 1;
    }
    CRT_Memory_Copy(out, reader->data + element->data, length);
    out[length] = MKV_TERMINATOR;

    return TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Requires an EBML header first, then skips top-level elements until Segment.
 */
static FORCE_INLINE bool_t Segment(const Reader_t* reader, Element_t* segment)
{
    uint64_t  offset;
    bool_t    found;
    bool_t    is_valid;
    Element_t element;

    offset   = 0;
    found    = FALSE;
    is_valid = ReadElement(reader, offset, reader->size, &element);
    if (is_valid)
    {
        is_valid = (UTL_MATROSKA_ID_EBML == element.id);
        offset   = element.end;
        for (; is_valid && (offset < reader->size);)
        {
            is_valid = ReadElement(reader, offset, reader->size, &element);
            if (is_valid)
            {
                found = (UTL_MATROSKA_ID_SEGMENT == element.id);
                if (found)
                {
                    (*segment) = element;
                    break;
                }
                offset = element.end;
            }
        }
    }

    return found;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Reads subtitle metadata and prefers LanguageBCP47 when present. Only unencoded
 *  S_TEXT/UTF8 subtitle tracks with valid timing scale are selected.
 */
static FORCE_INLINE int32_t Track(const Reader_t* reader, const Element_t* entry, MkvSubtitleTrack* track)
{
    uint64_t  type;
    bool_t    encoded;
    char_t    codec[MKV_CODEC_CAPACITY];
    char_t    ietf[MKV_SUBTITLE_LANGUAGE_CAPACITY];
    bool_t    is_valid;
    uint64_t  offset;
    Element_t element;
    uint64_t  bits;
    uint64_t  length;
    uint32_t  narrow_bits;
    float32_t narrow_scale;
    bool_t    valid_scale;
    int32_t   result;
    bool_t    is_subtitle;
    bool_t    is_utf8;
    bool_t    has_number;

    type    = 0;
    encoded = FALSE;
    CRT_Memory_Set(codec, 0, sizeof(codec));
    CRT_Memory_Set(ietf, 0, sizeof(ietf));
    CRT_Memory_Set(track, 0, sizeof(MkvSubtitleTrack));
    CRT_Memory_Copy(track->language, UTL_MATROSKA_LANGUAGE_DEFAULT, sizeof(UTL_MATROSKA_LANGUAGE_DEFAULT));
    track->timestampScale = UTL_MATROSKA_TRACK_SCALE_DEFAULT;
    is_valid              = TRUE;
    for (offset = entry->data; is_valid && (offset < entry->end);)
    {
        is_valid = ReadElement(reader, offset, entry->end, &element);
        if (is_valid)
        {
            switch (element.id)
            {
            case UTL_MATROSKA_ID_TRACK_NUMBER:
                is_valid = Uint(reader, &element, &track->number);
                break;
            case UTL_MATROSKA_ID_TRACK_TYPE:
                is_valid = Uint(reader, &element, &type);
                break;
            case UTL_MATROSKA_ID_CODEC_ID:
                (void)String(reader, &element, codec, sizeof(codec));
                break;
            case UTL_MATROSKA_ID_LANGUAGE:
                (void)String(reader, &element, track->language, sizeof(track->language));
                break;
            case UTL_MATROSKA_ID_LANGUAGE_BCP47:
                (void)String(reader, &element, ietf, sizeof(ietf));
                break;
            case UTL_MATROSKA_ID_NAME:
                (void)String(reader, &element, track->name, sizeof(track->name));
                break;
            case UTL_MATROSKA_ID_DEFAULT_DURATION:
                is_valid = Uint(reader, &element, &track->defaultDuration);
                break;
            case UTL_MATROSKA_ID_CODEC_DELAY:
                is_valid = Uint(reader, &element, &track->codecDelay);
                break;
            case UTL_MATROSKA_ID_CONTENT_ENCODINGS:
                encoded = TRUE;
                break;
            case UTL_MATROSKA_ID_TRACK_TIMESTAMP_SCALE:
                bits     = 0;
                length   = element.end - element.data;
                is_valid = Uint(reader, &element, &bits);
                if (is_valid)
                {
                    if (sizeof(float32_t) == length)
                    {
                        narrow_bits = (uint32_t)bits;
                        CRT_Memory_Copy(&narrow_scale, &narrow_bits, sizeof(float32_t));
                        track->timestampScale = (double)narrow_scale;
                    }
                    else if (sizeof(float64_t) == length)
                    {
                        CRT_Memory_Copy(&track->timestampScale, &bits, sizeof(float64_t));
                    }
                    else
                    {
                        is_valid = FALSE;
                    }
                }
                break;
            default:
                break;
            }
            offset = element.end;
        }
    }
    if (ietf[0])
    {
        CRT_Memory_Copy(track->language, ietf, sizeof(ietf));
    }
    valid_scale = (0.0 < track->timestampScale) && (track->timestampScale <= DBL_MAX);
    is_valid    = is_valid && valid_scale;
    result      = MKV_TRACK_INVALID;
    if (is_valid)
    {
        is_subtitle = (UTL_MATROSKA_TRACK_TYPE_SUBTITLE == type);
        is_utf8     = (0 == CRT_Memory_Compare(codec, UTL_MATROSKA_CODEC_UTF8, sizeof(UTL_MATROSKA_CODEC_UTF8)));
        has_number  = (0 < track->number);
        result      = is_subtitle && is_utf8 && has_number && (!encoded);
    }

    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Collects supported tracks and Segment timestamp scale, using Matroska defaults
 *  when absent and stopping once the required metadata is available.
 *  A requested track is filtered during traversal so extraction needs one slot.
 */
static FORCE_INLINE bool_t Metadata(const Reader_t*   reader,
                                    const Element_t*  segment,
                                    const uint64_t*   selectedTrack,
                                    MkvSubtitleTrack* tracks,
                                    uint32_t          capacity,
                                    uint32_t*         count,
                                    uint64_t*         timestampScale)
{
    bool_t           have_tracks;
    bool_t           have_info;
    bool_t           is_valid;
    uint64_t         offset;
    Element_t        element;
    uint64_t         position;
    Element_t        entry;
    int32_t          result;
    MkvSubtitleTrack track;
    bool_t           supported;
    bool_t           selected;
    bool_t           has_capacity;
    bool_t           can_store;
    bool_t           is_complete;

    have_tracks       = FALSE;
    have_info         = FALSE;
    is_valid          = TRUE;
    (*count)          = 0;
    (*timestampScale) = UTL_MATROSKA_TIMESTAMP_SCALE_DEFAULT;
    for (offset = segment->data; is_valid && (offset < segment->end);)
    {
        is_valid = ReadElement(reader, offset, segment->end, &element);
        if (is_valid)
        {
            if (UTL_MATROSKA_ID_TRACKS == element.id)
            {
                for (position = element.data; is_valid && (position < element.end);)
                {
                    is_valid = ReadElement(reader, position, element.end, &entry);
                    if (is_valid)
                    {
                        if (UTL_MATROSKA_ID_TRACK_ENTRY == entry.id)
                        {
                            result    = Track(reader, &entry, &track);
                            is_valid  = (MKV_TRACK_INVALID != result);
                            supported = (MKV_TRACK_SUPPORTED == result);
                            selected  = (NULL == selectedTrack);
                            if (supported && (NULL != selectedTrack))
                            {
                                selected = (track.number == (*selectedTrack));
                            }
                            has_capacity = ((*count) < capacity);
                            can_store    = supported && selected && has_capacity;
                            if (can_store)
                            {
                                tracks[(*count)] = track;
                                ++(*count);
                            }
                        }
                        position = entry.end;
                    }
                }
                have_tracks = is_valid;
            }
            else if (UTL_MATROSKA_ID_INFO == element.id)
            {
                for (position = element.data; is_valid && (position < element.end);)
                {
                    is_valid = ReadElement(reader, position, element.end, &entry);
                    if (is_valid)
                    {
                        if (UTL_MATROSKA_ID_TIMESTAMP_SCALE == entry.id)
                        {
                            is_valid = Uint(reader, &entry, timestampScale);
                            if (is_valid)
                            {
                                is_valid = (0 < (*timestampScale));
                            }
                        }
                        position = entry.end;
                    }
                }
                have_info = is_valid;
            }
            is_complete = have_tracks && (have_info || (UTL_MATROSKA_ID_CLUSTER == element.id));
            if (is_complete)
            {
                break;
            }
            offset = element.end;
        }
    }
    is_valid = is_valid && have_tracks;
    if (!is_valid)
    {
        (*count) = 0;
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Selects the requested track, rejects laced subtitle blocks and converts block
 *  timestamps using Segment scale, track scale and codec delay before the callback.
 */
static FORCE_INLINE bool_t EmitBlock(const Reader_t*         reader,
                                     const Element_t*        block,
                                     const MkvSubtitleTrack* track,
                                     uint64_t                clusterTime,
                                     uint64_t                scale,
                                     uint64_t                duration,
                                     MkvSubtitleProc         callback,
                                     void*                   user)
{
    uint64_t offset;
    uint64_t number;
    int32_t  relative;
    bool_t   is_valid;
    double   end;
    bool_t   is_selected;
    bool_t   has_header;
    bool_t   is_laced;
    uint32_t encoded_time;
    double   start;
    bool_t   valid_start;
    bool_t   valid_end;
    bool_t   has_text;

    offset   = block->data;
    number   = 0;
    relative = 0;
    is_valid = Vint(reader->data, block->end, &offset, &number, 0, NULL);
    if (is_valid)
    {
        is_selected = (number == track->number);
        if (is_selected)
        {
            has_header = (UTL_MATROSKA_BLOCK_HEADER_BYTES <= (block->end - offset));
            is_valid   = has_header;
            if (is_valid)
            {
                is_laced = !!(reader->data[offset + UTL_MATROSKA_BLOCK_FLAGS_OFFSET] & UTL_MATROSKA_BLOCK_LACING_MASK);
                is_valid = !is_laced;
            }
            if (is_valid)
            {
                encoded_time = ((uint32_t)reader->data[offset] * MKV_BYTE_RADIX) + reader->data[offset + 1];
                relative     = (int32_t)encoded_time;
                if (MKV_SIGNED_TIME_FIRST <= encoded_time)
                {
                    relative -= MKV_TIME_MODULUS;
                }
                offset   += UTL_MATROSKA_BLOCK_HEADER_BYTES;
                is_valid  = ((block->end - offset) <= MKV_MAX_CUE_BYTES);
            }
            if (is_valid)
            {
                start       = (((double)clusterTime + ((double)relative * track->timestampScale)) * (double)scale /
                               MKV_NANOSECONDS_PER_SECOND) -
                              ((double)track->codecDelay / MKV_NANOSECONDS_PER_SECOND);
                end         = start + ((duration ? (double)duration * (double)scale * track->timestampScale
                                                 : (double)track->defaultDuration) /
                                       MKV_NANOSECONDS_PER_SECOND);
                valid_start = (-DBL_MAX <= start) && (start <= DBL_MAX);
                valid_end   = (start < end) && (end <= MKV_MAX_CUE_SECONDS);
                is_valid    = valid_start && valid_end;
                has_text    = (offset < block->end) && (0.0 < end);
                if (is_valid && has_text)
                {
                    if (start < 0.0)
                    {
                        start = 0.0;
                    }
                    is_valid = callback(user, start, end, reader->data + offset, block->end - offset);
                }
            }
        }
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Identifies the Segment-level elements used to terminate unknown-sized clusters.
 */
static FORCE_INLINE bool_t TopLevel(uint32_t id)
{
    bool_t is_media;
    bool_t is_index;
    bool_t is_metadata;

    is_media = (UTL_MATROSKA_ID_CLUSTER == id) || (UTL_MATROSKA_ID_INFO == id) || (UTL_MATROSKA_ID_TRACKS == id);
    is_index = (UTL_MATROSKA_ID_SEEK_HEAD == id) || (UTL_MATROSKA_ID_CUES == id);
    is_metadata =
        (UTL_MATROSKA_ID_ATTACHMENTS == id) || (UTL_MATROSKA_ID_CHAPTERS == id) || (UTL_MATROSKA_ID_TAGS == id);

    return is_media || is_index || is_metadata;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Combines Block and BlockDuration regardless of their order in BlockGroup.
 *  An unknown-sized cluster ends when the next recognized Segment child begins.
 */
static FORCE_INLINE bool_t Cluster(const Reader_t*         reader,
                                   const Element_t*        cluster,
                                   const MkvSubtitleTrack* track,
                                   uint64_t                scale,
                                   MkvSubtitleProc         callback,
                                   void*                   user,
                                   uint64_t*               next)
{
    uint64_t  time;
    bool_t    have_time;
    bool_t    is_valid;
    uint64_t  offset;
    Element_t element;
    bool_t    next_cluster;
    bool_t    has_block;
    uint64_t  duration;
    Element_t block;
    uint64_t  position;
    Element_t child;

    time      = 0;
    have_time = FALSE;
    is_valid  = TRUE;
    (*next)   = cluster->end;
    for (offset = cluster->data; is_valid && (offset < cluster->end);)
    {
        is_valid = ReadElement(reader, offset, cluster->end, &element);
        if (is_valid)
        {
            next_cluster = cluster->unknown && TopLevel(element.id);
            if (next_cluster)
            {
                (*next) = offset;
                break;
            }
            if (UTL_MATROSKA_ID_CLUSTER_TIMESTAMP == element.id)
            {
                is_valid  = Uint(reader, &element, &time);
                have_time = is_valid;
            }
            else if (UTL_MATROSKA_ID_BLOCK_GROUP == element.id)
            {
                is_valid  = have_time;
                has_block = FALSE;
                duration  = 0;
                CRT_Memory_Set(&block, 0, sizeof(Element_t));
                for (position = element.data; is_valid && (position < element.end);)
                {
                    is_valid = ReadElement(reader, position, element.end, &child);
                    if (is_valid)
                    {
                        if (UTL_MATROSKA_ID_BLOCK == child.id)
                        {
                            block     = child;
                            has_block = TRUE;
                        }
                        else if (UTL_MATROSKA_ID_BLOCK_DURATION == child.id)
                        {
                            is_valid = Uint(reader, &child, &duration);
                        }
                        position = child.end;
                    }
                }
                if (is_valid && has_block)
                {
                    is_valid = EmitBlock(reader, &block, track, time, scale, duration, callback, user);
                }
            }
            else if (UTL_MATROSKA_ID_SIMPLE_BLOCK == element.id)
            {
                is_valid = have_time;
                if (is_valid)
                {
                    is_valid = EmitBlock(reader, &element, track, time, scale, 0, callback, user);
                }
            }
            offset = element.end;
        }
    }

    return is_valid;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Maps the input, locates Segment and collects supported track metadata before
 *  closing the reader. Failed traversal publishes a zero count.
 */
uint32_t MkvSubtitles_Find(const TCHAR* path, MkvSubtitleTrack* tracks, uint32_t capacity)
{
    uint32_t  count;
    bool_t    is_valid;
    Reader_t  reader;
    Element_t segment;
    uint64_t  scale;
    bool_t    closed;

    LOGASSERT(NULL != path);
    LOGASSERT((NULL != tracks) || (0 == capacity));

    count    = 0;
    is_valid = OpenReader(path, &reader);
    if (is_valid)
    {
        is_valid = Segment(&reader, &segment);
        if (is_valid)
        {
            is_valid = Metadata(&reader, &segment, NULL, tracks, capacity, &count, &scale);
        }
    }
    closed = CloseReader(&reader);
    if (!(is_valid && closed))
    {
        count = 0;
    }

    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Finds the selected track's metadata, traverses Segment clusters and dispatches
 *  its cue spans while the mapped input remains alive.
 */
bool_t MkvSubtitles_Read(const TCHAR* path, uint64_t trackNumber, MkvSubtitleProc callback, void* user)
{
    uint32_t         count;
    uint64_t         scale;
    bool_t           is_valid;
    Reader_t         reader;
    Element_t        segment;
    MkvSubtitleTrack track;
    uint64_t         offset;
    Element_t        element;
    bool_t           closed;

    LOGASSERT(NULL != path);
    LOGASSERT(NULL != callback);

    count    = 0;
    scale    = 0;
    is_valid = OpenReader(path, &reader);
    if (is_valid)
    {
        is_valid = Segment(&reader, &segment);
        if (is_valid)
        {
            is_valid = Metadata(&reader, &segment, &trackNumber, &track, 1, &count, &scale);
        }
        if (is_valid)
        {
            is_valid = (0 < count);
        }
        if (is_valid)
        {
            for (offset = segment.data; is_valid && (offset < segment.end);)
            {
                is_valid = ReadElement(&reader, offset, segment.end, &element);
                if (is_valid)
                {
                    offset = element.end;
                    if (UTL_MATROSKA_ID_CLUSTER == element.id)
                    {
                        is_valid = Cluster(&reader, &element, &track, scale, callback, user, &offset);
                    }
                }
            }
        }
    }
    closed = CloseReader(&reader);

    return is_valid && closed;
}
