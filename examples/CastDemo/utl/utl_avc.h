#ifndef UTL_AVC_H
#define UTL_AVC_H

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
#include "os/os_types.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define UTL_AVC_NAL_SPS             (7)     ///< Sequence parameter set NAL type.
#define UTL_AVC_NAL_PPS             (8)     ///< Picture parameter set NAL type.
#define UTL_AVC_NAL_SPS_EXTENSION   (13)    ///< Sequence parameter set extension NAL type.
#define UTL_AVC_MAX_SEQUENCE_SETS   (31)    ///< Five-bit initial sequence array count.
#define UTL_AVC_MAX_PICTURE_SETS    (255)   ///< Eight-bit initial picture array count.
#define UTL_AVC_MAX_EXTENSION_SETS  (255)   ///< Eight-bit sequence-extension count.
#define UTL_AVC_MAX_PARAMETER_BYTES (65535) ///< Sixteen-bit configuration NAL extent.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Reader status, including the explicit legacy omission of high-profile fields. */
typedef enum Utl_Avc_Status_e
{
    UTL_AVC_INVALID = 0,
    UTL_AVC_COMPLETE,
    UTL_AVC_LEGACY,
    UTL_AVC_UNSUPPORTED_VERSION
} Utl_Avc_Status_t;

/*************************************************************************************************/
/** \brief Complete encoded parameter array, without its outer count field. */
typedef struct Utl_Avc_ParameterSets_s
{
    Utl_Mpeg4_Span_t bytes; ///< Counted sequence of two-byte lengths and NAL bodies.
    uint32_t         count; ///< Number of entries; zero requires an empty byte sequence.
} Utl_Avc_ParameterSets_t;

/*************************************************************************************************/
/** \brief Typed configuration fields and bounded borrows of all NAL arrays. */
typedef struct Utl_Avc_Config_s
{
    uint8_t                 profile;       ///< AVCProfileIndication; bitstream profile relationships belong to the AVC payload layer.
    uint8_t                 compatibility; ///< Profile constraint flags.
    uint8_t                 level;         ///< AVCLevelIndication.
    uint8_t                 lengthBytes;   ///< One, two or four bytes in each sample NAL length prefix.
    Utl_Avc_ParameterSets_t sequence;      ///< All sequence-array entries, including permitted SEI/reserved NAL units.
    Utl_Avc_ParameterSets_t picture;       ///< All picture-array entries in their original order.
    bool_t                  hasHighFields; ///< FALSE for legacy records omitting the required high-profile suffix.
    uint8_t                 chromaFormat;  ///< Two-bit chroma indication, active only with hasHighFields.
    uint8_t                 lumaDepth;     ///< Actual sample bits, eight through twelve, active only with hasHighFields.
    uint8_t                 chromaDepth;   ///< Actual sample bits, eight through twelve, active only with hasHighFields.
    Utl_Avc_ParameterSets_t extensions;    ///< High-profile sequence-extension entries, when present.
    Utl_Mpeg4_Span_t        trailing;      ///< Unrecognized compatible trailing data, preserved verbatim.
} Utl_Avc_Config_t;

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
 * \brief Initializes an empty caller-owned model without inventing a codec profile.
 *
 * \par Requirements:
 *  - 001 A missing owner shall assert.
 *  - 002 All members shall be initialized; the caller shall supply a valid length width and actual codec fields before serialization.
 *
 * \param[out] config   Required exclusively writable owner
 *
 * \retval None Empty model initialized.
 */
void Utl_Avc_InitializeConfig(Utl_Avc_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Derives one encoded configuration NAL entry size.
 *
 * \par Requirements:
 *  - 001 Nonempty storage shall assert when missing; NAL syntax beyond its header remains a separate layer.
 *  - 002 The body shall be nonempty, fit sixteen bits and have a zero forbidden bit.
 *
 * \param[in] parameter   Stable NAL body including header
 *
 * \retval nonzero Complete entry byte count.
 * \retval 0 Invalid NAL extent/header.
 */
uint32_t Utl_Avc_GetParameterByteCount(Utl_Mpeg4_Span_t parameter);

/*************************************************************************************************/
/**
 * \brief Derives a complete version-one record size after validating every known field and entry.
 *
 * \par Requirements:
 *  - 001 Missing required model/storage shall assert; invalid external fields shall return zero.
 *  - 002 All arrays shall be checked through their exact declared end before output sizing.
 *  - 003 High fields required by the cited revision shall be present for standard output.
 *  - 004 Future trailing bytes shall be preserved; total-size overflow shall fail.
 *
 * \param[in] config   Required stable model and borrows
 *
 * \retval nonzero Complete required bytes.
 * \retval 0 Invalid, incomplete or overflowing model.
 */
uint32_t Utl_Avc_GetConfigByteCount(const Utl_Avc_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Finds the first requested NAL type after validating the complete array.
 *
 * \par Requirements:
 *  - 001 Required output/model and a five-bit selector shall assert on misuse.
 *  - 002 Missing matches or malformed entries, including suffix entries, shall preserve output.
 *  - 003 SEI and reserved entries shall be skipped by type without altering their stored bytes.
 *
 * \param[out] parameter   Required selected borrowed NAL
 * \param[in]  sets        Required stable complete array
 * \param[in]  nalType     Requested five-bit NAL type
 *
 * \retval TRUE First match published.
 * \retval FALSE Invalid array or no match.
 */
bool_t Utl_Avc_FindParameter(Utl_Mpeg4_Span_t* parameter, const Utl_Avc_ParameterSets_t* sets, uint8_t nalType);

/*************************************************************************************************/
/**
 * \brief Reads the full known version-one configuration without partial publication.
 *
 * \par Requirements:
 *  - 001 Required result and nonempty input storage shall assert when missing.
 *  - 002 Input shall remain stable, disjoint from output and alive for all published borrows.
 *  - 003 All known fields, reserved bits, widths and array extents shall validate before output publication.
 *  - 004 Unknown versions and invalid input shall preserve output.
 *  - 005 Complete old high-profile records missing all high fields shall publish with LEGACY status; no values shall be invented.
 *  - 006 Permitted future trailing bytes shall remain represented without interpreting their unknown semantics.
 *
 * \param[out] config   Required configuration result
 * \param[in]  bytes    Externally framed complete record
 *
 * \retval COMPLETE Complete known fields published.
 * \retval LEGACY Published record lacks required high metadata.
 * \retval INVALID Malformed/truncated known fields; output unchanged.
 * \retval UNSUPPORTED_VERSION Unknown configuration version; output unchanged.
 */
Utl_Avc_Status_t Utl_Avc_ReadConfig(Utl_Avc_Config_t* config, Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Writes one two-byte-length configuration parameter entry.
 *
 * \par Requirements:
 *  - 001 Required output and nonempty input storage shall assert when missing.
 *  - 002 Stable source shall be disjoint from output; validation and capacity checks shall precede all output mutation.
 *
 * \param[in]  parameter   Stable complete NAL body
 * \param[out] output      Required disjoint storage
 * \param[in]  capacity    Writable bytes
 *
 * \retval nonzero Complete entry bytes.
 * \retval 0 Invalid NAL or insufficient capacity; output unchanged.
 */
uint32_t Utl_Avc_WriteParameter(Utl_Mpeg4_Span_t parameter, byte_t* output, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Writes the complete known version-one record and preserves future suffix bytes.
 *
 * \par Requirements:
 *  - 001 Required model/output and active borrows shall assert when missing.
 *  - 002 All model fields and total capacity shall validate before the first store; failure shall preserve the entire output.
 *  - 003 Input borrows shall remain stable and disjoint from output.
 *  - 004 No allocation, native operation or external CRT shall occur.
 *
 * \param[in]  config     Required complete stable model
 * \param[out] output     Required disjoint output
 * \param[in]  capacity   Writable bytes
 *
 * \retval nonzero Complete bytes written.
 * \retval 0 Invalid/incomplete model or insufficient capacity.
 */
uint32_t Utl_Avc_WriteConfig(const Utl_Avc_Config_t* config, byte_t* output, uint32_t capacity);

#endif /* UTL_AVC_H */
