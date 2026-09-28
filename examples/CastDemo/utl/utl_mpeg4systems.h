#ifndef UTL_MPEG4SYSTEMS_H
#define UTL_MPEG4SYSTEMS_H

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
#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UTL_MPEG4SYSTEMS_MAX_PAYLOAD        (0x0FFFFFFF) ///< Maximum BaseDescriptor body extent.
#define UTL_MPEG4SYSTEMS_TAG_ES             (0x03)       ///< Elementary-stream descriptor.
#define UTL_MPEG4SYSTEMS_TAG_DECODER_CONFIG (0x04)       ///< Decoder configuration descriptor.
#define UTL_MPEG4SYSTEMS_TAG_DECODER_INFO   (0x05)       ///< Opaque codec configuration descriptor.
#define UTL_MPEG4SYSTEMS_TAG_SL_CONFIG      (0x06)       ///< Synchronization-layer configuration.
#define UTL_MPEG4SYSTEMS_OBJECT_AUDIO       (0x40)       ///< ISO/IEC 14496-3 audio configuration.
#define UTL_MPEG4SYSTEMS_STREAM_AUDIO       (0x05)       ///< Audio elementary stream.
#define UTL_MPEG4SYSTEMS_SL_MP4             (0x02)       ///< MP4 file synchronization profile.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief One complete expandable descriptor with its original encoded extent. */
typedef struct Utl_Mpeg4Systems_Descriptor_s
{
    uint8_t          tag;              ///< Descriptor code point; zero and 255 are forbidden.
    Utl_Mpeg4_Span_t payload;          ///< Body excluding tag and expandable length.
    uint32_t         encodedByteCount; ///< Read result including original length width; ignored by writers.
} Utl_Mpeg4Systems_Descriptor_t;

/*************************************************************************************************/
/** \brief Elementary-stream fields and the unchanged sequence of component descriptors. */
typedef struct Utl_Mpeg4Systems_ElementaryStream_s
{
    uint16_t         streamId;      ///< ES_ID; file-profile restrictions belong to the container.
    uint8_t          priority;      ///< Five-bit relative priority.
    bool_t           hasDependency; ///< Whether dependsOn is present.
    uint16_t         dependsOn;     ///< Meaningful only when hasDependency is TRUE.
    bool_t           hasUrl;        ///< Whether the counted UTF-8 URL field is present.
    Utl_Mpeg4_Span_t url;           ///< Meaningful only when hasUrl is TRUE; at most 255 bytes.
    bool_t           hasClock;      ///< Whether clockId is present.
    uint16_t         clockId;       ///< Meaningful only when hasClock is TRUE.
    Utl_Mpeg4_Span_t children;      ///< Complete component-descriptor sequence, preserved verbatim.
} Utl_Mpeg4Systems_ElementaryStream_t;

/*************************************************************************************************/
/** \brief Decoder requirements and their unchanged codec/profile component descriptors. */
typedef struct Utl_Mpeg4Systems_DecoderConfig_s
{
    uint8_t          objectType;     ///< Registered or future nonzero object type indication.
    uint8_t          streamType;     ///< Six-bit nonzero stream category.
    bool_t           upstream;       ///< Direction flag.
    uint32_t         bufferBytes;    ///< 24-bit decoder-buffer requirement.
    uint32_t         maximumBitrate; ///< Maximum bits per second.
    uint32_t         averageBitrate; ///< Average bits per second.
    Utl_Mpeg4_Span_t children;       ///< Complete codec/profile descriptors, preserved verbatim.
} Utl_Mpeg4Systems_DecoderConfig_t;

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
 * \brief Derives the shortest encoding size of one complete descriptor.
 *
 * \par Requirements:
 *  - 001 Required model and nonempty body storage shall assert when missing.
 *  - 002 Forbidden tags or bodies beyond the descriptor limit shall return zero.
 *  - 003 Original encodedByteCount shall not affect canonical output sizing.
 *
 * \param[in] descriptor   Required stable model
 *
 * \retval nonzero Canonical complete byte count.
 * \retval 0 Invalid model.
 */
uint32_t Utl_Mpeg4Systems_GetDescriptorByteCount(const Utl_Mpeg4Systems_Descriptor_t* descriptor);

/*************************************************************************************************/
/**
 * \brief Derives a decoder-configuration descriptor size.
 *
 * \par Requirements:
 *  - 001 Required model and nonempty component storage shall assert when missing.
 *  - 002 Field widths, complete component boundaries and at most one DecoderSpecificInfo shall be validated.
 *  - 003 Unknown component payloads shall remain opaque and unchanged.
 *
 * \param[in] config   Required typed configuration
 *
 * \retval nonzero Complete descriptor bytes.
 * \retval 0 Invalid model.
 */
uint32_t Utl_Mpeg4Systems_GetDecoderConfigByteCount(const Utl_Mpeg4Systems_DecoderConfig_t* config);

/*************************************************************************************************/
/**
 * \brief Derives an elementary-stream descriptor size from typed optional fields.
 *
 * \par Requirements:
 *  - 001 Required model and present nonempty storage shall assert when missing.
 *  - 002 Priority width, URL length/UTF-8, one valid decoder configuration and one nonempty SL configuration shall be required.
 *  - 003 Inactive optional fields shall not be read; context-dependent ID and SL semantics remain with the containing profile.
 *
 * \param[in] stream   Required typed stream metadata
 *
 * \retval nonzero Complete descriptor bytes.
 * \retval 0 Invalid model.
 */
uint32_t Utl_Mpeg4Systems_GetElementaryStreamByteCount(const Utl_Mpeg4Systems_ElementaryStream_t* stream);

/*************************************************************************************************/
/**
 * \brief Finds exactly one selected component in a complete descriptor sequence.
 *
 * \par Requirements:
 *  - 001 Required output and nonempty input storage shall assert when missing.
 *  - 002 The entire sequence shall be checked, including bytes after the match.
 *  - 003 Missing, duplicate or malformed components shall preserve output.
 *
 * \param[out] descriptor   Required selected component
 * \param[in]  sequence     Complete component bytes
 * \param[in]  tag          Requested permitted tag
 *
 * \retval TRUE One match published.
 * \retval FALSE Missing, duplicate or malformed sequence.
 */
bool_t Utl_Mpeg4Systems_FindDescriptor(Utl_Mpeg4Systems_Descriptor_t* descriptor,
                                       Utl_Mpeg4_Span_t               sequence,
                                       uint8_t                        tag);

/*************************************************************************************************/
/**
 * \brief Reads the first complete expandable descriptor from counted bytes.
 *
 * \par Requirements:
 *  - 001 Required model/output pointers and nonempty storage shall assert when missing.
 *  - 002 Malformed or truncated external data shall fail without modifying the output.
 *  - 003 All input borrows shall remain stable and disjoint from output; no allocation, CRT or native call shall occur.
 *  - 004 One through four length groups, including padded encodings, shall be accepted; unterminated or out-of-parent lengths shall fail.
 *
 * \param[out] descriptor   Required result
 * \param[in]  bytes        Complete readable input extent
 *
 * \retval TRUE Complete first descriptor published.
 * \retval FALSE Invalid or truncated first descriptor.
 */
bool_t Utl_Mpeg4Systems_ReadDescriptor(Utl_Mpeg4Systems_Descriptor_t* descriptor, Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Reads typed decoder requirements from a complete descriptor.
 *
 * \par Requirements:
 *  - 001 Required model/output pointers and nonempty storage shall assert when missing.
 *  - 002 Malformed or truncated external data shall fail without modifying the output.
 *  - 003 All input borrows shall remain stable and disjoint from output; no allocation, CRT or native call shall occur.
 *  - 004 The tag, reserved bit, fixed fields and full component sequence shall be checked before publication.
 *
 * \param[out] config       Required result
 * \param[in]  descriptor   Required complete descriptor body
 *
 * \retval TRUE Configuration published.
 * \retval FALSE Wrong tag or invalid configuration.
 */
bool_t Utl_Mpeg4Systems_ReadDecoderConfig(Utl_Mpeg4Systems_DecoderConfig_t*    config,
                                          const Utl_Mpeg4Systems_Descriptor_t* descriptor);

/*************************************************************************************************/
/**
 * \brief Reads optional stream fields and their bounded component sequence.
 *
 * \par Requirements:
 *  - 001 Required model/output pointers and nonempty storage shall assert when missing.
 *  - 002 Malformed or truncated external data shall fail without modifying the output.
 *  - 003 All input borrows shall remain stable and disjoint from output; no allocation, CRT or native call shall occur.
 *  - 004 Dependency, URL and clock fields shall be consumed only when their corresponding flags are present.
 *  - 005 Component spans shall never extend beyond their enclosing descriptor.
 *
 * \param[out] stream       Required result
 * \param[in]  descriptor   Required complete descriptor body
 *
 * \retval TRUE Typed stream metadata published.
 * \retval FALSE Wrong tag, truncated fields or invalid components.
 */
bool_t Utl_Mpeg4Systems_ReadElementaryStream(Utl_Mpeg4Systems_ElementaryStream_t* stream,
                                             const Utl_Mpeg4Systems_Descriptor_t* descriptor);

/*************************************************************************************************/
/**
 * \brief Writes one descriptor with a shortest expandable length.
 *
 * \par Requirements:
 *  - 001 Required model/output pointers shall assert when missing; all borrows shall remain stable and disjoint.
 *  - 002 Complete model and capacity validation shall precede the first output write.
 *  - 003 Malformed model or short capacity shall preserve output; no allocation, CRT or native call shall occur.
 *
 * \param[in]  descriptor   Required stable model
 * \param[out] output       Required disjoint destination
 * \param[in]  capacity     Writable byte count
 *
 * \retval nonzero Complete bytes written.
 * \retval 0 Invalid model or insufficient capacity.
 */
uint32_t Utl_Mpeg4Systems_WriteDescriptor(const Utl_Mpeg4Systems_Descriptor_t* descriptor,
                                          byte_t*                              output,
                                          uint32_t                             capacity);

/*************************************************************************************************/
/**
 * \brief Writes typed decoder requirements and preserved component bytes.
 *
 * \par Requirements:
 *  - 001 Required model/output pointers shall assert when missing; all borrows shall remain stable and disjoint.
 *  - 002 Complete model and capacity validation shall precede the first output write.
 *  - 003 Malformed model or short capacity shall preserve output; no allocation, CRT or native call shall occur.
 *
 * \param[in]  config     Required typed model
 * \param[out] output     Required disjoint destination
 * \param[in]  capacity   Writable byte count
 *
 * \retval nonzero Complete bytes written.
 * \retval 0 Invalid model or insufficient capacity.
 */
uint32_t Utl_Mpeg4Systems_WriteDecoderConfig(const Utl_Mpeg4Systems_DecoderConfig_t* config,
                                             byte_t*                                 output,
                                             uint32_t                                capacity);

/*************************************************************************************************/
/**
 * \brief Writes typed stream fields and preserved component bytes.
 *
 * \par Requirements:
 *  - 001 Required model/output pointers shall assert when missing; all borrows shall remain stable and disjoint.
 *  - 002 Complete model and capacity validation shall precede the first output write.
 *  - 003 Malformed model or short capacity shall preserve output; no allocation, CRT or native call shall occur.
 *
 * \param[in]  stream     Required typed model
 * \param[out] output     Required disjoint destination
 * \param[in]  capacity   Writable byte count
 *
 * \retval nonzero Complete bytes written.
 * \retval 0 Invalid model or insufficient capacity.
 */
uint32_t Utl_Mpeg4Systems_WriteElementaryStream(const Utl_Mpeg4Systems_ElementaryStream_t* stream,
                                                byte_t*                                    output,
                                                uint32_t                                   capacity);

#endif /* UTL_MPEG4SYSTEMS_H */
