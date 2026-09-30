/*=================================================================================================
** 1. References
**===============================================================================================*/

/* mp4_index.c - walks moov/trak/mdia/minf/stbl and converts the stss sync-sample numbers to
** decode times through streamed stts/stss entries. Composition offsets and edits are not yet
** mapped, so returned media decode times can differ materially from the presentation timeline. */
/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h> // IWYU pragma: keep
#include <minwindef.h>
#include <fileapi.h>
#include <WinBase.h>
#include <handleapi.h>

#include <stdint.h>
#include <winnt.h>

#include "mp4_index.h"
#include "utl/utl_mpeg4.h"
#include "os/os_assert.h"
#include "os/os_compiler.h"
#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define TABLE_PAGE_BYTES (4096) ///< Fixed resident bytes per streamed table.
#define MP4_INDEX_U32_HIGH_SHIFT      (24U)
#define MP4_INDEX_U32_MID_SHIFT       (16U)
#define MP4_INDEX_U32_LOW_SHIFT       (8U)
#define MP4_INDEX_HANDLER_MIN_BYTES   (12U)
#define MP4_INDEX_HANDLER_TYPE_OFFSET (8U)
#define MP4_INDEX_MDHD_HEADER_BYTES   (16U)
#define MP4_INDEX_MDHD_V1_BYTES       (24U)
#define MP4_INDEX_MDHD_V1_EXTRA_BYTES (8U)
#define MP4_INDEX_MDHD_V1_TICKS       (20U)
#define MP4_INDEX_MDHD_V0_TICKS       (12U)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

typedef struct Box
{
    uint64_t start;
    uint64_t payload;
    uint64_t end;
    uint64_t fileEnd;
    uint32_t type;
} Box;

/*************************************************************************************************/
/** \brief Distinguishes a legitimately absent optional box from invalid structure. */
typedef enum BoxSearch_e
{
    BOX_ABSENT,
    BOX_FOUND,
    BOX_INVALID
} BoxSearch_t;

/*************************************************************************************************/
/** \brief Fixed storage for a streamed stts/stss table. */
typedef struct TableReader_s
{
    HANDLE                  file;                   ///< Borrowed readable file handle.
    Utl_Mpeg4_SampleTable_t table;                  ///< Validated table prefix.
    uint64_t                next;                   ///< Absolute next page offset, within the validated entry extent.
    uint32_t                remaining;              ///< Entries not yet borrowed by the consumer.
    uint32_t                width;                  ///< Entry bytes derived from the table type.
    uint32_t                position;               ///< Next unread byte in the resident page.
    uint32_t                loaded;                 ///< Valid resident page bytes.
    bool_t                  failed;                 ///< Sticky native or wire failure.
    byte_t                  page[TABLE_PAGE_BYTES]; ///< Caller-owned fixed page storage.
} TableReader_t;

/*************************************************************************************************/
/** \brief Unpublished caller output with explicit capacity and scale. */
typedef struct KeyframeOutput_s
{
    double*  times;     ///< Exclusive scratch destination; only returned count is publishable.
    uint32_t capacity;  ///< Writable timestamp slots.
    uint32_t count;     ///< Retained complete-index entries.
    uint32_t timescale; ///< Positive media ticks per second.
} KeyframeOutput_t;

/*************************************************************************************************/
/** \brief Retained timing run for monotonic sync-sample lookup. */
typedef struct TimingLookup_s
{
    TableReader_t*                 reader; ///< Borrowed stts page reader.
    Utl_Mpeg4_TimeToSampleCursor_t cursor; ///< Remaining runs and cumulative coordinates.
    Utl_Mpeg4_TimeToSampleRun_t    run;    ///< Most recently validated run.
    bool_t                         hasRun; ///< TRUE only after run has been initialized by its reader.
} TimingLookup_t;

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
 * \brief Reads a bounded native prefix and publishes validated MPEG-4 box coordinates.
 *
 * \par Requirements:
 *  - 001 The caller shall supply a valid file handle and exclusively writable result.
 *  - 002 Offset and parent boundaries shall be checked against file end before native I/O.
 *  - 003 The common specification reader shall validate all size forms; output shall remain unchanged on failure.
 *  - 004 File extent shall propagate to descendants without querying the file again.
 *
 * \param[in]  hFile       Required open readable file
 * \param[in]  offset      Absolute candidate box start
 * \param[in]  parentEnd   Exclusive parent end
 * \param[in]  fileEnd     Exclusive file end
 * \param[out] pBox        Required result
 *
 * \retval TRUE Complete coordinates published.
 * \retval FALSE Invalid extent, malformed header or native read failure.
 */
static FORCE_INLINE bool_t ReadBox(HANDLE hFile, uint64_t offset, uint64_t parentEnd, uint64_t fileEnd, Box* pBox);

/*************************************************************************************************/
/**
 * \brief Finds a singleton immediate child while validating the complete enclosing sequence.
 *
 * \par Requirements:
 *  - 001 Invalid child extents and duplicate requested types shall be distinguished from absence.
 *  - 002 Output shall be published only after the complete sibling sequence validates.
 *
 * \param[in]  hFile     Borrowed readable file
 * \param[in]  pParent   Required enclosing bounds
 * \param[in]  type      Singleton box type sought
 * \param[out] pChild    Required disjoint result
 *
 * \retval BOX_FOUND Exactly one child published.
 * \retval BOX_ABSENT No matching child in a valid sequence.
 * \retval BOX_INVALID Invalid sequence or duplicate child.
 */
static FORCE_INLINE BoxSearch_t FindChild(HANDLE hFile, const Box* pParent, uint32_t type, Box* pChild);

/*************************************************************************************************/
/**
 * \brief Reads a table prefix and establishes a fixed-page input owner.
 *
 * \par Requirements:
 *  - 001 Required owners and a valid enclosing Box shall be supplied.
 *  - 002 All declared entry extents shall validate through the common MPEG-4 reader before owner publication.
 *  - 003 The retained file handle is borrowed; this operation creates no resource or heap storage.
 *
 * \param[out] reader   Required exclusively owned page reader
 * \param[in]  file     Borrowed open readable file
 * \param[in]  box      Required stable table bounds
 *
 * \retval TRUE Complete prefix and page state published.
 * \retval FALSE Invalid prefix or failed native read.
 */
static FORCE_INLINE bool_t ReadTable(TableReader_t* reader, HANDLE file, const Box* box);

/*************************************************************************************************/
/**
 * \brief Reads one complete table entry from a bounded reusable page.
 *
 * \par Requirements:
 *  - 001 The owner shall be established by ReadTable and used by one worker.
 *  - 002 Every page read shall contain whole entries and stay within the prevalidated declared extent.
 *  - 003 A returned borrow shall remain valid only until the next page refill.
 *  - 004 Native failure shall latch and preserve output; exhausted tables shall be idempotent.
 *
 * \param[in,out] reader   Required initialized page owner
 * \param[out]    bytes    Required disjoint entry borrow
 *
 * \retval TRUE Complete entry borrowed.
 * \retval FALSE End of table or sticky read failure.
 */
static FORCE_INLINE bool_t ReadTableEntry(TableReader_t* reader, Utl_Mpeg4_Span_t* bytes);

/*************************************************************************************************/
/**
 * \brief Consumes a complete decoding-time run through the specification cursor.
 *
 * \par Requirements:
 *  - 001 Wire failure or an unrepresentable cumulative timeline shall latch input failure.
 *
 * \param[in,out] reader   Required initialized stts input
 * \param[in,out] cursor   Required associated timing cursor
 * \param[out]    run      Required complete run result
 *
 * \retval TRUE Run consumed.
 * \retval FALSE Input or timeline failure.
 */
static FORCE_INLINE bool_t ReadTimeRun(TableReader_t* reader, Utl_Mpeg4_TimeToSampleCursor_t* cursor, Utl_Mpeg4_TimeToSampleRun_t* run);

/*************************************************************************************************/
/**
 * \brief Validates all remaining timing runs after requested output is filled.
 *
 * \par Requirements:
 *  - 001 A truncated or overflowing suffix shall not leave an apparently valid prefix index.
 *
 * \param[in,out] reader   Required timing input
 * \param[in,out] cursor   Required timing cursor
 *
 * \retval TRUE Every declared timing entry consumed.
 * \retval FALSE Invalid or unreadable suffix.
 */
static FORCE_INLINE bool_t DrainTimeRuns(TableReader_t* reader, Utl_Mpeg4_TimeToSampleCursor_t* cursor);

/*************************************************************************************************/
/**
 * \brief Advances a bounded timing lookup to a strictly increasing selected sample.
 *
 * \par Requirements:
 *  - 001 The owner shall retain the current run between lookups.
 *  - 002 Invalid native or timing input shall latch in the associated page reader.
 *  - 003 Absent samples shall not publish a timestamp.
 *
 * \param[in,out] lookup   Required exclusively owned lookup state
 * \param[in]     sample   One-based selected sample
 * \param[out]    time     Required disjoint media-time result
 *
 * \retval TRUE Selected sample found.
 * \retval FALSE Sample absent or invalid input.
 */
static FORCE_INLINE bool_t FindSampleTime(TimingLookup_t* lookup, uint32_t sample, uint64_t* time);

/*************************************************************************************************/
/**
 * \brief Maps strictly ordered sync-sample numbers through streamed decode-time runs.
 *
 * \par Requirements:
 *  - 001 Each declared sync entry shall validate, including when the index cannot be retained.
 *  - 002 Every selected sample shall belong to a timing run; cumulative overflow shall fail.
 *  - 003 A present empty stss table shall produce an empty index.
 *
 * \param[in,out] timing            Required stts input
 * \param[in,out] synchronization   Required present stss input
 * \param[in,out] output            Required unpublished output
 *
 * \retval TRUE Complete tables validated.
 * \retval FALSE Ordering, range, timeline or native failure.
 */
static FORCE_INLINE bool_t ReadSelectedSyncTimes(TableReader_t* timing, TableReader_t* synchronization, KeyframeOutput_t* output);

/*************************************************************************************************/
/**
 * \brief Builds a bounded index with two fixed pages and no table allocation.
 *
 * \par Requirements:
 *  - 001 The caller shall treat times as unpublished scratch until a nonzero count is returned.
 *  - 002 Invalid data shall return zero, never a partially validated prefix count.
 *  - 003 Absent stss shall preserve exact seeking after complete timing validation.
 *  - 004 An index exceeding output capacity shall publish zero; a truncated index could misdirect later seeks.
 *  - 005 Native synchronous I/O and GUI scheduling remain separate unresolved realtime requirements.
 *
 * \param[in]  file          Borrowed readable file
 * \param[in]  sampleTable   Required stbl bounds
 * \param[in]  timescale     Positive media units per second
 * \param[out] times         Exclusively owned scratch output
 * \param[in]  capacity      Writable timestamp count
 *
 * \param[out] complete      TRUE after all declared entries validate, even without a retained index
 *
 * \retval count Complete index count, or zero on failure, absent/empty stss or insufficient capacity.
 */
static FORCE_INLINE uint32_t ReadTrackTimes(HANDLE file, const Box* sampleTable, uint32_t timescale, double* times, uint32_t capacity, bool_t* complete);

/*************************************************************************************************/
/**
 * \brief Selects the first video track whose complete sample tables validate.
 *
 * \par Requirements:
 *  - 001 The caller shall supply a readable native handle and validated root extent.
 *  - 002 The selected track shall remain selected when exact seeking is required because its index is absent or exceeds capacity.
 *  - 003 Only a complete retained index shall publish a nonzero count.
 *
 * \param[in]  file       Borrowed readable native handle
 * \param[in]  root       Required file extent
 * \param[out] times      Unpublished timestamp scratch
 * \param[in]  capacity   Writable timestamp slots
 *
 * \retval nonzero Complete retained index count.
 * \retval 0 No retained index; callers shall keep the requested seek time.
 */
static FORCE_INLINE uint32_t ReadMovieTimes(HANDLE file, const Box* root, double* times, uint32_t capacity);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Combines four big-endian bytes into an unsigned 32-bit value.
 */
static FORCE_INLINE uint32_t Be32(const byte_t* p)
{
    return ((uint32_t)p[0] << MP4_INDEX_U32_HIGH_SHIFT) | ((uint32_t)p[1] << MP4_INDEX_U32_MID_SHIFT) |
           ((uint32_t)p[2] << MP4_INDEX_U32_LOW_SHIFT) | (uint32_t)p[3];
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Moves the file pointer to an absolute byte offset and requires an exact read.
 */
static FORCE_INLINE bool_t ReadAt(HANDLE hFile, uint64_t offset, void* pOut, DWORD size)
{
    LARGE_INTEGER position;
    DWORD         received;
    bool_t        moved;
    bool_t        valid;
    bool_t        read;
    bool_t        complete;
    DWORD         requested;
    DWORD         origin;
    byte_t        discard = 0;
    void*         destination;

    LOGASSERT(NULL != pOut);
    position.QuadPart = (0 == size) ? 0 : (LONGLONG)offset;
    origin            = (0 == size) ? FILE_CURRENT : FILE_BEGIN;
    received          = 0;
    moved             = SetFilePointerEx(hFile, position, NULL, origin);
    requested         = moved ? size : 0;
    destination       = moved ? pOut : &discard;
    read              = ReadFile(hFile, destination, requested, &received, NULL);
    complete          = (received == size);
    valid             = read && complete;
    return (0 == size) || (moved && valid);
}

/* Reads the box header at offset. Returns 0 at the end of the parent or on a malformed size. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Reads a bounded header prefix and delegates box-size validation to utl_mpeg4
 *  before publishing absolute payload and end offsets.
 */
static FORCE_INLINE bool_t ReadBox(HANDLE hFile, uint64_t offset, uint64_t parentEnd, uint64_t fileEnd, Box* pBox)
{
    bool_t                offset_in_parent;
    bool_t                parent_in_file;
    bool_t                is_valid;
    uint64_t              remaining;
    bool_t                has_header;
    bool_t                read;
    DWORD                 read_bytes;
    byte_t                bytes[UTL_MPEG4_MAX_BOX_HEADER_BYTES];
    Utl_Mpeg4_Span_t      prefix;
    Utl_Mpeg4_BoxHeader_t header;
    uint32_t              header_bytes;

    LOGASSERT(NULL != pBox);
    LOGASSERT(NULL != hFile);
    LOGASSERT(INVALID_HANDLE_VALUE != hFile);
    offset_in_parent = (offset <= parentEnd);
    parent_in_file   = (parentEnd <= fileEnd);
    is_valid         = offset_in_parent && parent_in_file;
    remaining        = 0;
    read_bytes       = 0;
    if (is_valid)
    {
        remaining  = parentEnd - offset;
        has_header = (UTL_MPEG4_BOX_HEADER_BYTES <= remaining);
        is_valid   = has_header;
        if (has_header)
        {
            read_bytes = UTL_MPEG4_MAX_BOX_HEADER_BYTES;
            if (remaining < read_bytes)
            {
                read_bytes = (DWORD)remaining;
            }
        }
    }
    read     = ReadAt(hFile, offset, bytes, read_bytes);
    is_valid = is_valid && read;
    if (is_valid)
    {
        prefix.data      = bytes;
        prefix.byteCount = read_bytes;
        is_valid         = Utl_Mpeg4_ReadBoxHeader(&header, prefix, remaining, fileEnd - offset);
    }
    if (is_valid)
    {
        header_bytes  = Utl_Mpeg4_GetBoxHeaderByteCount(&header);
        pBox->fileEnd = fileEnd;
        pBox->start   = offset;
        pBox->type    = header.type;
        pBox->payload = offset + header_bytes;
        pBox->end     = offset + header.byteCount;
    }
    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Validates the complete immediate-child sequence, publishing exactly one
 *  matching child and distinguishing absence from duplicate or malformed boxes.
 */
static FORCE_INLINE BoxSearch_t FindChild(HANDLE hFile, const Box* pParent, uint32_t type, Box* pChild)
{
    Box         selected;
    uint64_t    offset;
    BoxSearch_t status;
    bool_t      valid;
    Box         candidate;
    bool_t      found;

    LOGASSERT(NULL != pParent);
    LOGASSERT(NULL != pChild);
    selected.start   = pParent->payload;
    selected.payload = pParent->payload;
    selected.end     = pParent->payload;
    selected.fileEnd = pParent->fileEnd;
    selected.type    = 0;
    offset           = pParent->payload;
    status           = BOX_ABSENT;
    for (;;)
    {
        if (offset == pParent->end)
        {
            break;
        }
        valid = ReadBox(hFile, offset, pParent->end, pParent->fileEnd, &candidate);
        if (!valid)
        {
            status = BOX_INVALID;
            break;
        }
        found = (candidate.type == type);
        if (found)
        {
            if (BOX_FOUND == status)
            {
                status = BOX_INVALID;
                break;
            }
            selected = candidate;
            status   = BOX_FOUND;
        }
        offset = candidate.end;
    }
    if (BOX_FOUND == status)
    {
        *pChild = selected;
    }
    return status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadTable(TableReader_t* reader, HANDLE file, const Box* box)
{
    Utl_Mpeg4_SampleTable_t table;
    uint64_t                payload_bytes;
    bool_t                  ordered;
    bool_t                  has_header;
    bool_t                  valid;
    byte_t                  bytes[UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES];
    Utl_Mpeg4_Span_t        prefix;
    Utl_Mpeg4_BoxPayload_t  payload;
    DWORD                   read_bytes;
    bool_t                  read;

    LOGASSERT(NULL != reader);
    LOGASSERT(NULL != box);
    Utl_Mpeg4_InitializeSampleTable(&table, box->type);
    ordered       = (box->payload <= box->end);
    payload_bytes = ordered ? (box->end - box->payload) : 0;
    has_header    = (UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES <= payload_bytes);
    valid         = has_header;
    read_bytes    = valid ? sizeof(bytes) : 0;
    read          = ReadAt(file, box->payload, bytes, read_bytes);
    valid         = valid && read;
    if (valid)
    {
        prefix.data       = bytes;
        prefix.byteCount  = sizeof(bytes);
        payload.type      = box->type;
        payload.byteCount = payload_bytes;
        valid             = Utl_Mpeg4_ReadSampleTableHeader(&table, prefix, payload);
    }
    if (valid)
    {
        reader->file      = file;
        reader->table     = table;
        reader->next      = box->payload + UTL_MPEG4_SAMPLE_TABLE_HEADER_BYTES;
        reader->remaining = table.entryCount;
        reader->width     = Utl_Mpeg4_GetSampleTableEntryByteCount(table.type);
        reader->position  = 0;
        reader->loaded    = 0;
        reader->failed    = FALSE;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadTableEntry(TableReader_t* reader, Utl_Mpeg4_Span_t* bytes)
{
    bool_t   has_entry;
    bool_t   page_fits;
    bool_t   in_page;
    bool_t   can_read;
    uint32_t width;
    uint32_t entries;
    uint32_t count;
    bool_t   read;

    LOGASSERT(NULL != reader);
    LOGASSERT(NULL != bytes);
    LOGASSERT(0 != reader->width);
    width       = reader->width;
    page_fits   = (width <= sizeof(reader->page));
    in_page     = (reader->position <= reader->loaded);
    has_entry   = (0 != reader->remaining);
    can_read    = has_entry && page_fits && in_page && !reader->failed;
    if (can_read)
    {
        if (reader->position == reader->loaded)
        {
            entries = sizeof(reader->page) / width;
            if (reader->remaining < entries)
            {
                entries = reader->remaining;
            }
            count          = entries * width;
            read           = ReadAt(reader->file, reader->next, reader->page, count);
            reader->failed = !read;
            can_read       = read;
            if (read)
            {
                reader->next     += count;
                reader->position = 0;
                reader->loaded   = count;
            }
        }
        if (can_read)
        {
            bytes->data      = &reader->page[reader->position];
            bytes->byteCount = reader->width;
            reader->position += reader->width;
            --reader->remaining;
        }
    }
    return can_read;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadTimeRun(TableReader_t*                  reader,
                                       Utl_Mpeg4_TimeToSampleCursor_t* cursor,
                                       Utl_Mpeg4_TimeToSampleRun_t*    run)
{
    bool_t           valid;
    Utl_Mpeg4_Span_t bytes;

    valid = ReadTableEntry(reader, &bytes);
    if (valid)
    {
        valid          = Utl_Mpeg4_ReadTimeToSampleRun(cursor, run, bytes);
        reader->failed = !valid;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t DrainTimeRuns(TableReader_t* reader, Utl_Mpeg4_TimeToSampleCursor_t* cursor)
{
    bool_t                      valid;
    Utl_Mpeg4_TimeToSampleRun_t run;

    valid = TRUE;
    for (;;)
    {
        if (0 == cursor->remaining)
        {
            break;
        }
        valid = ReadTimeRun(reader, cursor, &run);
        if (!valid)
        {
            break;
        }
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t FindSampleTime(TimingLookup_t* lookup, uint32_t sample, uint64_t* time)
{
    bool_t found;
    bool_t valid;

    LOGASSERT(NULL != lookup);
    LOGASSERT(NULL != time);
    found = FALSE;
    for (;;)
    {
        if (lookup->hasRun)
        {
            found = Utl_Mpeg4_GetSampleDecodeTime(time, &lookup->run, sample);
        }
        if (found)
        {
            break;
        }
        if (0 == lookup->cursor.remaining)
        {
            break;
        }
        valid          = ReadTimeRun(lookup->reader, &lookup->cursor, &lookup->run);
        lookup->hasRun = valid;
        if (!valid)
        {
            break;
        }
    }
    return found;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t ReadSelectedSyncTimes(TableReader_t*    timing,
                                                 TableReader_t*    synchronization,
                                                 KeyframeOutput_t* output)
{
    TimingLookup_t               lookup;
    Utl_Mpeg4_SyncSampleCursor_t sync_cursor;
    bool_t                       valid;
    uint32_t                     sample;
    Utl_Mpeg4_Span_t             bytes;
    bool_t                       found;
    uint64_t                     time;

    lookup.reader = timing;
    lookup.hasRun = FALSE;
    Utl_Mpeg4_InitializeTimeToSampleCursor(&lookup.cursor, &timing->table);
    Utl_Mpeg4_InitializeSyncSampleCursor(&sync_cursor, &synchronization->table);
    valid = TRUE;
    for (;;)
    {
        if (0 == sync_cursor.remaining)
        {
            break;
        }
        sample = 0;
        valid  = ReadTableEntry(synchronization, &bytes);
        if (valid)
        {
            valid = Utl_Mpeg4_ReadSyncSample(&sync_cursor, &sample, bytes);
        }
        if (!valid)
        {
            break;
        }
        found = FindSampleTime(&lookup, sample, &time);
        valid = valid && found;
        if (!valid)
        {
            break;
        }
        if (output->count < output->capacity)
        {
            output->times[output->count] = (double)time / output->timescale;
            ++output->count;
        }
    }
    if (valid)
    {
        valid = DrainTimeRuns(timing, &lookup.cursor);
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t
ReadTrackTimes(HANDLE     file,
               const Box* sampleTable,
               uint32_t   timescale,
               double*    times,
               uint32_t   capacity,
               bool_t*    complete)
{
    BoxSearch_t                    status;
    Box                            box;
    bool_t                         valid;
    TableReader_t                  timing;
    uint32_t                       count;
    KeyframeOutput_t               output;
    Utl_Mpeg4_TimeToSampleCursor_t cursor;
    TableReader_t                  synchronization;
    bool_t                         fits;

    LOGASSERT(0 < timescale);
    LOGASSERT(NULL != complete);
    *complete = FALSE;
    status    = FindChild(file, sampleTable, UTL_MPEG4_BOX_STTS, &box);
    valid     = (BOX_FOUND == status);
    if (valid)
    {
        valid = ReadTable(&timing, file, &box);
    }
    count = 0;
    if (valid)
    {
        output.times     = times;
        output.capacity  = capacity;
        output.count     = 0;
        output.timescale = timescale;
        status           = FindChild(file, sampleTable, UTL_MPEG4_BOX_STSS, &box);
        if (BOX_ABSENT == status)
        {
            Utl_Mpeg4_InitializeTimeToSampleCursor(&cursor, &timing.table);
            valid = DrainTimeRuns(&timing, &cursor);
        }
        else if (BOX_FOUND == status)
        {
            valid = ReadTable(&synchronization, file, &box);
            if (valid)
            {
                fits = (synchronization.table.entryCount <= capacity);
                if (!fits)
                {
                    output.capacity = 0;
                }
                valid = ReadSelectedSyncTimes(&timing, &synchronization, &output);
            }
        }
        else
        {
            valid = FALSE;
        }
        if (valid)
        {
            count     = output.count;
            *complete = TRUE;
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Selects a video track and maps sync-sample numbers through decode-time runs
 *  to seconds, bounded by the caller's output capacity.
 */
static FORCE_INLINE bool_t ReadMdiaTimescale(HANDLE hFile, const Box* pMdia, uint32_t* pTimescale)
{
    byte_t buffer[MP4_INDEX_MDHD_V1_BYTES];
    Box mdhd;
    bool_t valid;

    valid = (BOX_FOUND == FindChild(hFile, pMdia, UTL_MPEG4_BOX_MDHD, &mdhd));
    if (!valid)
    {
        return FALSE;
    }
    valid = (MP4_INDEX_MDHD_HEADER_BYTES <= (mdhd.end - mdhd.payload)) &&
            ReadAt(hFile, mdhd.payload, buffer, MP4_INDEX_MDHD_HEADER_BYTES);
    if (!valid)
    {
        return FALSE;
    }
    if (1 == buffer[0])
    {
        valid = (MP4_INDEX_MDHD_V1_BYTES <= (mdhd.end - mdhd.payload)) &&
                ReadAt(hFile, mdhd.payload + MP4_INDEX_MDHD_HEADER_BYTES,
                       buffer + MP4_INDEX_MDHD_HEADER_BYTES,
                       MP4_INDEX_MDHD_V1_EXTRA_BYTES);
        if (valid)
        {
            *pTimescale = Be32(buffer + MP4_INDEX_MDHD_V1_TICKS);
        }
    }
    else
    {
        valid = (0 == buffer[0]);
        if (valid)
        {
            *pTimescale = Be32(buffer + MP4_INDEX_MDHD_V0_TICKS);
        }
    }
    return valid && (0 < *pTimescale);
}

static FORCE_INLINE uint32_t
KeyframesFromTrack(HANDLE hFile, const Box* pTrack, double* pTimes, unsigned capacity, bool_t* complete)
{
    uint32_t timescale;
    uint32_t count;
    bool_t   is_valid;
    Box      mdia;
    Box      hdlr = {0};
    byte_t   buffer[MP4_INDEX_MDHD_V1_BYTES];
    Box      minf;
    Box      stbl;

    LOGASSERT(NULL != complete);
    *complete = FALSE;
    timescale = 0;
    count     = 0;
    is_valid  = (BOX_FOUND == FindChild(hFile, pTrack, UTL_MPEG4_BOX_MDIA, &mdia));
    if (is_valid)
    {
        is_valid = (BOX_FOUND == FindChild(hFile, &mdia, UTL_MPEG4_BOX_HDLR, &hdlr));
    }
    if (is_valid)
    {
        is_valid = (MP4_INDEX_HANDLER_MIN_BYTES <= (hdlr.end - hdlr.payload)) &&
                   ReadAt(hFile, hdlr.payload, buffer, MP4_INDEX_HANDLER_MIN_BYTES);
        if (is_valid)
        {
            is_valid = (UTL_MPEG4_HANDLER_VIDEO == Be32(buffer + MP4_INDEX_HANDLER_TYPE_OFFSET));
        }
    }
    if (is_valid)
    {
        is_valid = ReadMdiaTimescale(hFile, &mdia, &timescale);
    }
    if (is_valid)
    {
        is_valid = (BOX_FOUND == FindChild(hFile, &mdia, UTL_MPEG4_BOX_MINF, &minf));
    }
    if (is_valid)
    {
        is_valid = (BOX_FOUND == FindChild(hFile, &minf, UTL_MPEG4_BOX_STBL, &stbl));
    }
    if (is_valid)
    {
        count = ReadTrackTimes(hFile, &stbl, timescale, pTimes, capacity, complete);
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t ReadMovieTimes(HANDLE file, const Box* root, double* times, uint32_t capacity)
{
    BoxSearch_t status;
    Box         movie;
    uint32_t    count;
    uint64_t    offset;
    bool_t      valid;
    Box         track;
    bool_t      complete;

    LOGASSERT(NULL != root);
    status = FindChild(file, root, UTL_MPEG4_BOX_MOOV, &movie);
    count  = 0;
    if (BOX_FOUND == status)
    {
        offset = movie.payload;
        for (;;)
        {
            if (offset == movie.end)
            {
                break;
            }
            valid = ReadBox(file, offset, movie.end, movie.fileEnd, &track);
            if (!valid)
            {
                break;
            }
            if (UTL_MPEG4_BOX_TRAK == track.type)
            {
                complete = FALSE;
                count    = KeyframesFromTrack(file, &track, times, capacity, &complete);
                if (complete)
                {
                    break;
                }
            }
            offset = track.end;
        }
    }
    return count;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Opens the file, locates movie tracks and collects the first available video
 *  sync-sample index before closing the handle.
 */
unsigned Mp4Index_ReadKeyframes(const TCHAR* pFile, double* pTimes, unsigned capacity)
{
    bool_t        has_storage;
    bool_t        empty;
    bool_t        valid_storage;
    uint32_t      count;
    HANDLE        file;
    bool_t        valid;
    LARGE_INTEGER size;
    Box           root;

    LOGASSERT(NULL != pFile);
    has_storage   = (NULL != pTimes);
    empty         = (0 == capacity);
    valid_storage = has_storage || empty;
    LOGASSERT(valid_storage);
    count = 0;
    if (!empty)
    {
        file  = CreateFile(pFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_RANDOM_ACCESS, NULL);
        valid = (INVALID_HANDLE_VALUE != file);
        if (valid)
        {
            valid = GetFileSizeEx(file, &size);
            if (valid)
            {
                valid = (0 <= size.QuadPart);
            }
            if (valid)
            {
                root.start   = 0;
                root.payload = 0;
                root.end     = (uint64_t)size.QuadPart;
                root.fileEnd = root.end;
                root.type    = 0;
                count        = ReadMovieTimes(file, &root, pTimes, capacity);
            }
            CloseHandle(file);
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Uses binary search for the last timestamp at or before the request. Empty
 *  indexes and times before the first indexed sample leave the request unchanged.
 */
double Mp4Index_SnapToKeyframe(const double* pTimes, unsigned count, double seconds)
{
    double   result;
    bool_t   has_index;
    bool_t   can_snap;
    uint32_t low;
    uint32_t high;
    uint32_t middle;

    result    = seconds;
    has_index = (0 < count);
    can_snap  = has_index && (pTimes[0] <= seconds);
    if (can_snap)
    {
        low  = 0;
        high = count;
        for (;;)
        {
            if (1 >= (high - low))
            {
                break;
            }
            middle = low + ((high - low) / 2);
            if (pTimes[middle] <= seconds)
            {
                low = middle;
            }
            else
            {
                high = middle;
            }
        }
        result = pTimes[low];
    }

    return result;
}
