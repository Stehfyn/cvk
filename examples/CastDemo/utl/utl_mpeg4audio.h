#ifndef UTL_MPEG4AUDIO_H
#define UTL_MPEG4AUDIO_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * ISO/IEC 14496-3:2009, 1.6.2.1 and 1.6.3: AudioSpecificConfig and signaling.
 * Subpart 4, 4.4.1 and 4.5.1: general audio and program configuration elements.
 * https://www.iso.org/standard/53943.html
 * This module models AAC Main/LC/SSR/LTP and their explicit/backward-compatible
 * SBR/PS signaling. Other audio objects, error-protected syntax and future
 * extensions have an explicit unsupported outcome; they are not decoded as LC.
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

#define UTL_MPEG4AUDIO_MAIN             (1)  ///< AAC Main object type.
#define UTL_MPEG4AUDIO_LC               (2)  ///< AAC Low Complexity object type.
#define UTL_MPEG4AUDIO_SSR              (3)  ///< AAC Scalable Sample Rate object type.
#define UTL_MPEG4AUDIO_LTP              (4)  ///< AAC Long Term Prediction object type.
#define UTL_MPEG4AUDIO_SBR              (5)  ///< Explicit spectral-band replication.
#define UTL_MPEG4AUDIO_PS               (29) ///< Explicit parametric stereo.
#define UTL_MPEG4AUDIO_FREQUENCY_ESCAPE (15) ///< A following 24-bit frequency is present.
#define UTL_MPEG4AUDIO_ELEMENT_LIMIT    (15) ///< Four-bit PCE element count.
#define UTL_MPEG4AUDIO_LFE_LIMIT        (3)  ///< Two-bit PCE low-frequency count.
#define UTL_MPEG4AUDIO_DATA_LIMIT       (7)  ///< Three-bit PCE associated-data count.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/** \brief Reader classification; unsupported input is never published as a complete model. */
typedef enum Utl_Mpeg4Audio_Status_e
{
    UTL_MPEG4AUDIO_INVALID = 0,
    UTL_MPEG4AUDIO_COMPLETE,
    UTL_MPEG4AUDIO_UNSUPPORTED
} Utl_Mpeg4Audio_Status_t;

/*************************************************************************************************/
/** \brief Distinguishes unsignaled tools from explicit absence and presence. */
typedef enum Utl_Mpeg4Audio_Presence_e
{
    UTL_MPEG4AUDIO_UNKNOWN = 0,
    UTL_MPEG4AUDIO_ABSENT,
    UTL_MPEG4AUDIO_PRESENT
} Utl_Mpeg4Audio_Presence_t;

/*************************************************************************************************/
/** \brief Where the SBR/PS fields occur in AudioSpecificConfig. */
typedef enum Utl_Mpeg4Audio_Signaling_e
{
    UTL_MPEG4AUDIO_IMPLICIT = 0,
    UTL_MPEG4AUDIO_EXPLICIT,
    UTL_MPEG4AUDIO_SYNC
} Utl_Mpeg4Audio_Signaling_t;

/*************************************************************************************************/
/** \brief Indexed or explicitly encoded sampling frequency. */
typedef struct Utl_Mpeg4Audio_Frequency_s
{
    uint8_t  index; ///< Zero through twelve, or FREQUENCY_ESCAPE.
    uint32_t hertz; ///< Used only for FREQUENCY_ESCAPE; reader also populates indexed rates.
} Utl_Mpeg4Audio_Frequency_t;

/*************************************************************************************************/
/** \brief Channel element selected by a program configuration. */
typedef struct Utl_Mpeg4Audio_Element_s
{
    bool_t  paired; ///< TRUE for a channel-pair element, FALSE for a single-channel element.
    uint8_t tag;    ///< Four-bit element instance selector.
} Utl_Mpeg4Audio_Element_t;

/*************************************************************************************************/
/** \brief Coupling element and its independently switched coupling indication. */
typedef struct Utl_Mpeg4Audio_Coupling_s
{
    bool_t  independent; ///< Coupling switch flag from the PCE.
    uint8_t tag;         ///< Four-bit coupling element selector.
} Utl_Mpeg4Audio_Coupling_t;

/*************************************************************************************************/
/** \brief Program configuration with specification-bounded arrays and borrowed comments. */
typedef struct Utl_Mpeg4Audio_Program_s
{
    uint8_t                   tag;                                    ///< Four-bit program instance tag.
    uint8_t                   objectIndex;                            ///< Two-bit Main/LC/SSR/LTP index.
    uint8_t                   frequencyIndex;                         ///< Indexed nominal frequency; escape is forbidden.
    uint8_t                   frontCount;                             ///< Populated front elements.
    uint8_t                   sideCount;                              ///< Populated side elements.
    uint8_t                   backCount;                              ///< Populated back elements.
    uint8_t                   lfeCount;                               ///< Populated low-frequency elements.
    uint8_t                   dataCount;                              ///< Populated associated-data selectors.
    uint8_t                   couplingCount;                          ///< Populated coupling elements.
    bool_t                    hasMonoMixdown;                         ///< Whether monoTag is present.
    uint8_t                   monoTag;                                ///< Single-channel mixdown element selector.
    bool_t                    hasStereoMixdown;                       ///< Whether stereoTag is present.
    uint8_t                   stereoTag;                              ///< Channel-pair mixdown element selector.
    bool_t                    hasMatrixMixdown;                       ///< Whether matrixIndex and pseudoSurround are present.
    uint8_t                   matrixIndex;                            ///< Two-bit matrix mixdown index.
    bool_t                    pseudoSurround;                         ///< Matrix mixdown surround flag.
    Utl_Mpeg4Audio_Element_t  front[UTL_MPEG4AUDIO_ELEMENT_LIMIT];    ///< Front channel mapping.
    Utl_Mpeg4Audio_Element_t  side[UTL_MPEG4AUDIO_ELEMENT_LIMIT];     ///< Side channel mapping.
    Utl_Mpeg4Audio_Element_t  back[UTL_MPEG4AUDIO_ELEMENT_LIMIT];     ///< Back channel mapping.
    uint8_t                   lfe[UTL_MPEG4AUDIO_LFE_LIMIT];          ///< Low-frequency element tags.
    uint8_t                   data[UTL_MPEG4AUDIO_DATA_LIMIT];        ///< Associated-data element tags.
    Utl_Mpeg4Audio_Coupling_t coupling[UTL_MPEG4AUDIO_ELEMENT_LIMIT]; ///< Coupling selectors.
    Utl_Mpeg4_Span_t          comment;                                ///< Up to 255 opaque comment bytes, preserved unchanged.
} Utl_Mpeg4Audio_Program_t;

/*************************************************************************************************/
/** \brief Complete modeled AudioSpecificConfig, with no retained resource ownership. */
typedef struct Utl_Mpeg4Audio_Config_s
{
    uint8_t                    objectType;           ///< Underlying Main/LC/SSR/LTP audio object.
    Utl_Mpeg4Audio_Frequency_t frequency;            ///< Core sampling frequency.
    uint8_t                    channelConfiguration; ///< Zero selects program; one through seven select standard layouts.
    bool_t                     shortFrame;           ///< 960 core samples instead of 1024; forbidden for SSR.
    bool_t                     hasCoreCoder;         ///< Whether coreCoderDelay is present.
    uint16_t                   coreCoderDelay;       ///< Fourteen-bit delay, used only with hasCoreCoder.
    Utl_Mpeg4Audio_Program_t   program;              ///< Used only when channelConfiguration is zero.
    Utl_Mpeg4Audio_Signaling_t signaling;            ///< Implicit, leading explicit or trailing sync syntax.
    Utl_Mpeg4Audio_Presence_t  sbr;                  ///< SBR signal, including unknown.
    Utl_Mpeg4Audio_Presence_t  ps;                   ///< Parametric-stereo signal, including unknown.
    Utl_Mpeg4Audio_Frequency_t extensionFrequency;   ///< Used only when SBR is explicitly present.
} Utl_Mpeg4Audio_Config_t;

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
 * \brief Initializes caller-owned configuration to 48 kHz stereo AAC LC.
 *
 * \par Details:
 *  Model initialized.
 *
 * \par Requirements:
 *  - 001 The required destination shall assert when missing.
 *  - 002 All members shall be initialized; no allocation, native operation or external CRT shall occur.
 *  - 003 No pointer representation shall be synthesized by zeroing bytes.
 *
 * \param[out] config   Required exclusively writable model
 *
 * \retval None
 */
void Utl_Mpeg4Audio_InitializeConfig(Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Derives the explicitly configured output sampling frequency.
 *
 * \par Requirements:
 *  - 001 Invalid or unsupported complete models shall derive zero.
 *  - 002 When SBR is unsignaled, this query shall report the core rate without guessing payload extensions.
 *
 * \param[in] config   Required stable model
 *
 * \retval nonzero Configured hertz.
 * \retval 0 Invalid or unsupported configuration.
 */
uint32_t Utl_Mpeg4Audio_GetSampleRate(const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Derives declared output channels, including PCE maps and explicit PS.
 *
 * \par Requirements:
 *  - 001 Standard layout seven shall derive eight channels.
 *  - 002 Unsignaled payload extensions shall not be inferred; invalid models shall derive zero.
 *
 * \param[in] config   Required stable model
 *
 * \retval count Declared channels.
 * \retval 0 Invalid model or empty program.
 */
uint32_t Utl_Mpeg4Audio_GetChannelCount(const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Derives the complete byte-aligned configuration size after validating every active field.
 *
 * \par Requirements:
 *  - 001 Required model and nonempty borrowed storage shall assert when missing.
 *  - 002 Inactive optional fields shall not affect validation or sizing.
 *  - 003 Invalid, out-of-width and unsupported models shall derive zero.
 *
 * \param[in] config   Required stable model and active borrows
 *
 * \retval nonzero Required output bytes.
 * \retval 0 Invalid or unsupported model.
 */
uint32_t Utl_Mpeg4Audio_GetConfigByteCount(const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Reads a complete modeled AudioSpecificConfig without guessing missing fields.
 *
 * \par Requirements:
 *  - 001 Required output and nonempty input storage shall assert when missing.
 *  - 002 Stable input shall be disjoint from output and outlive any published comment borrow.
 *  - 003 Truncation, reserved frequencies and invalid modeled semantics shall fail without modifying output.
 *  - 004 Unmodeled objects and extensions shall return UNSUPPORTED without publishing a partial model.
 *  - 005 Up to seven terminal alignment bits shall be ignored; additional unmodeled syntax shall be reported.
 *
 * \param[out] config   Required complete result
 * \param[in]  bytes    Complete counted configuration
 *
 * \retval COMPLETE Model published.
 * \retval INVALID Malformed or truncated input; output unchanged.
 * \retval UNSUPPORTED Unmodeled syntax; output unchanged.
 */
Utl_Mpeg4Audio_Status_t Utl_Mpeg4Audio_ReadConfig(Utl_Mpeg4Audio_Config_t* config, Utl_Mpeg4_Span_t bytes);

/*************************************************************************************************/
/**
 * \brief Writes a complete configuration after validating all fields and output capacity.
 *
 * \par Requirements:
 *  - 001 Required model/output and active borrows shall assert when missing; input shall remain stable and disjoint.
 *  - 002 Invalid models or short capacity shall preserve every output byte.
 *  - 003 Exactly the derived byte count shall be written; terminal alignment bits shall be zero.
 *  - 004 No allocation, system operation or external CRT shall occur.
 *
 * \param[in]  config     Required stable configuration
 * \param[out] output     Required disjoint destination
 * \param[in]  capacity   Writable byte extent
 *
 * \retval nonzero Complete bytes written.
 * \retval 0 Invalid model or insufficient capacity.
 */
uint32_t Utl_Mpeg4Audio_WriteConfig(const Utl_Mpeg4Audio_Config_t* config, byte_t* output, uint32_t capacity);

#endif /* UTL_MPEG4AUDIO_H */
