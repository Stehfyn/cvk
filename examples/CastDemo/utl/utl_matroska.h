#ifndef UTL_MATROSKA_H
#define UTL_MATROSKA_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * RFC 8794, sections 4 through 7: EBML variable integers, IDs and element sizes.
 * https://www.rfc-editor.org/rfc/rfc8794.html
 * RFC 9559, sections 4.2 and 4.3: Matroska ID 0x80 and ID/size width constraints.
 * https://www.rfc-editor.org/rfc/rfc9559.html
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

#define UTL_MATROSKA_MAX_ID_BYTES     (4)          ///< RFC 9559 section 4.3.
#define UTL_MATROSKA_MAX_SIZE_BYTES   (8)          ///< RFC 9559 section 4.3.
#define UTL_MATROSKA_MAX_HEADER_BYTES (12)         ///< Combined maximum ID and size widths.
#define UTL_MATROSKA_ID_EBML          (0x1A45DFA3) ///< EBML header.
#define UTL_MATROSKA_ID_SEGMENT       (0x18538067) ///< RFC 9559 section 5.1.
#define UTL_MATROSKA_ID_INFO          (0x1549A966) ///< RFC 9559 section 5.1.2.
#define UTL_MATROSKA_ID_CLUSTER       (0x1F43B675) ///< RFC 9559 section 5.1.3.
#define UTL_MATROSKA_ID_TRACKS        (0x1654AE6B) ///< RFC 9559 section 5.1.4.

#define UTL_MATROSKA_ID_TRACK_NUMBER          (0xD7)          ///< TrackNumber, section 5.1.4.1.1.
#define UTL_MATROSKA_ID_TRACK_TYPE            (0x83)          ///< TrackType, section 5.1.4.1.3.
#define UTL_MATROSKA_ID_CODEC_ID              (0x86)          ///< CodecID, section 5.1.4.1.21.
#define UTL_MATROSKA_ID_LANGUAGE              (0x22B59C)      ///< Language, section 5.1.4.1.19.
#define UTL_MATROSKA_ID_LANGUAGE_BCP47        (0x22B59D)      ///< LanguageBCP47, section 5.1.4.1.20.
#define UTL_MATROSKA_ID_NAME                  (0x536E)        ///< Name, section 5.1.4.1.18.
#define UTL_MATROSKA_ID_DEFAULT_DURATION      (0x23E383)      ///< DefaultDuration, section 5.1.4.1.13.
#define UTL_MATROSKA_ID_CODEC_DELAY           (0x56AA)        ///< CodecDelay, section 5.1.4.1.26.
#define UTL_MATROSKA_ID_CONTENT_ENCODINGS     (0x6D80)        ///< ContentEncodings, section 5.1.4.1.31.
#define UTL_MATROSKA_ID_TRACK_TIMESTAMP_SCALE (0x23314F)      ///< TrackTimestampScale, section 5.1.4.1.15.
#define UTL_MATROSKA_ID_TRACK_ENTRY           (0xAE)          ///< TrackEntry, section 5.1.4.1.
#define UTL_MATROSKA_ID_TIMESTAMP_SCALE       (0x2AD7B1)      ///< TimestampScale, section 5.1.2.9.
#define UTL_MATROSKA_ID_CLUSTER_TIMESTAMP     (0xE7)          ///< Timestamp, section 5.1.3.1.
#define UTL_MATROSKA_ID_BLOCK_GROUP           (0xA0)          ///< BlockGroup, section 5.1.3.5.
#define UTL_MATROSKA_ID_BLOCK                 (0xA1)          ///< Block, section 5.1.3.5.1.
#define UTL_MATROSKA_ID_BLOCK_DURATION        (0x9B)          ///< BlockDuration, section 5.1.3.5.3.
#define UTL_MATROSKA_ID_SIMPLE_BLOCK          (0xA3)          ///< SimpleBlock, section 5.1.3.4.
#define UTL_MATROSKA_ID_SEEK_HEAD             (0x114D9B74)    ///< SeekHead, section 5.1.1.
#define UTL_MATROSKA_ID_CUES                  (0x1C53BB6B)    ///< Cues, section 5.1.5.
#define UTL_MATROSKA_ID_ATTACHMENTS           (0x1941A469)    ///< Attachments, section 5.1.6.
#define UTL_MATROSKA_ID_CHAPTERS              (0x1043A770)    ///< Chapters, section 5.1.7.
#define UTL_MATROSKA_ID_TAGS                  (0x1254C367)    ///< Tags, section 5.1.8.
#define UTL_MATROSKA_TRACK_TYPE_SUBTITLE      (0x11)          ///< Subtitle TrackType, section 5.1.4.1.3.
#define UTL_MATROSKA_LANGUAGE_DEFAULT         ("eng")         ///< Language default, section 5.1.4.1.19.
#define UTL_MATROSKA_TRACK_SCALE_DEFAULT      (1.0)           ///< TrackTimestampScale default, section 5.1.4.1.15.
#define UTL_MATROSKA_TIMESTAMP_SCALE_DEFAULT  (1000000)       ///< Nanoseconds per tick, section 5.1.2.9.
#define UTL_MATROSKA_CODEC_UTF8               ("S_TEXT/UTF8") ///< Matroska codec mapping: UTF-8 text subtitles.
#define UTL_MATROSKA_BLOCK_LACING_MASK        (0x06)          ///< Lacing bits, section 10.1.
#define UTL_MATROSKA_BLOCK_HEADER_BYTES       (3)             ///< Timestamp and flags after TrackNumber, section 10.1.
#define UTL_MATROSKA_BLOCK_FLAGS_OFFSET       (2)             ///< Flag offset after TrackNumber, section 10.1.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief EBML variable integer with its encoded form and marker-free data.
 */
typedef struct Utl_Matroska_Vint_s
{
    uint64_t encodedValue;   ///< Entire big-endian integer, including its marker.
    uint64_t value;          ///< Data after removing the width marker.
    uint32_t byteCount;      ///< Encoded width, one through eight.
    bool_t   allDataBitsSet; ///< Unknown length only when the integer denotes an element size.
} Utl_Matroska_Vint_t;

/*************************************************************************************************/
/**
 * \brief Matroska element header; ownership of all payload bytes stays with the caller.
 */
typedef struct Utl_Matroska_ElementHeader_s
{
    uint64_t payloadByteCount; ///< Declared payload length; zero when unknownSize is TRUE.
    uint32_t id;               ///< Encoded element ID including its marker.
    uint32_t headerByteCount;  ///< Combined ID and size byte counts.
    uint32_t sizeByteCount;    ///< Original encoded size width.
    bool_t   unknownSize;      ///< Caller must find the schema-defined end of this master element.
} Utl_Matroska_ElementHeader_t;

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
 * \brief Reads one EBML variable integer from caller-owned bytes.
 *
 * \par Details:
 *  Marker bits determine the width. A value with all data bits set is reported
 *  without interpreting it as an element ID, track number, or size.
 *
 * \par Requirements:
 *  - 001 This function shall accept one through eight encoded bytes.
 *  - 002 This function shall reject a zero first byte and a truncated integer.
 *  - 003 This function shall leave value unchanged on failure and retain no input pointer.
 *
 * \param[out] value       Non-NULL typed result
 * \param[in]  buffer      Non-NULL source bytes
 * \param[in]  byteCount   Readable source byte count
 *
 * \retval     TRUE    On success.
 * \retval     FALSE   For missing pointers, a zero marker, or truncation.
 */
bool_t Utl_Matroska_ReadVint(Utl_Matroska_Vint_t* value, const byte_t* buffer, uint64_t byteCount);

/*************************************************************************************************/
/**
 * \brief Reads a Matroska element ID and size into a typed header.
 *
 * \par Details:
 *  RFC 9559 permits ID 0x80 and limits IDs to four bytes. Known payload sizes
 *  are validated against remaining bytes in the enclosing element. Unknown-size
 *  master semantics are left to the schema reader; remaining is their upper bound.
 *
 * \par Requirements:
 *  - 001 This function shall reject reserved, nonminimal or oversized element IDs.
 *  - 002 This function shall accept size fields from one through eight bytes.
 *  - 003 This function shall reject headers or known payloads that exceed remaining.
 *  - 004 This function shall leave header unchanged on failure and allocate no storage.
 *
 * \param[out] header      Non-NULL result
 * \param[in]  buffer      Non-NULL bytes beginning at the element ID
 * \param[in]  byteCount   Readable header bytes; payload bytes need not be present
 * \param[in]  remaining   Bytes remaining in the enclosing element
 *
 * \retval     TRUE    For a valid bounded header.
 * \retval     FALSE   Otherwise.
 */
bool_t Utl_Matroska_ReadElementHeader(Utl_Matroska_ElementHeader_t* header,
                                      const byte_t*                 buffer,
                                      uint64_t                      byteCount,
                                      uint64_t                      remaining);

/*************************************************************************************************/
/**
 * \brief Writes a Matroska element header into caller-owned storage.
 *
 * \par Details:
 *  The sizeByteCount field selects a width from one through eight. An all-ones
 *  size is emitted only when unknownSize is set. ID width follows its encoded value.
 *
 * \par Requirements:
 *  - 001 This function shall validate the ID, size width and capacity before writing.
 *  - 002 This function shall reject a known size that collides with the unknown-size sentinel.
 *  - 003 This function shall return zero and preserve output bytes on failure.
 *  - 004 This function shall perform no allocation, I/O or CRT calls.
 *
 * \param[in]  header     Non-NULL typed header
 * \param[out] buffer     Non-NULL destination bytes
 * \param[in]  capacity   Writable byte count
 *
 * \retval     2..UTL_MATROSKA_MAX_HEADER_BYTES   Number of element-header bytes written.
 * \retval     0                                  The model, output pointer or capacity is invalid; output remains unchanged.
 */
uint32_t Utl_Matroska_WriteElementHeader(const Utl_Matroska_ElementHeader_t* header, byte_t* buffer, uint64_t capacity);

#endif /* UTL_MATROSKA_H */
