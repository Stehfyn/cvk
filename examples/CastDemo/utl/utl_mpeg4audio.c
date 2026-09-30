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
#include "utl/utl_mpeg4audio.h"
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

#define FREQUENCY_96000 (96000)
#define INDEX_96000     (0)
#define FREQUENCY_88200 (88200)
#define INDEX_88200     (1)
#define FREQUENCY_64000 (64000)
#define INDEX_64000     (2)
#define FREQUENCY_48000 (48000)
#define INDEX_48000     (3)
#define FREQUENCY_44100 (44100)
#define INDEX_44100     (4)
#define FREQUENCY_32000 (32000)
#define INDEX_32000     (5)
#define FREQUENCY_24000 (24000)
#define INDEX_24000     (6)
#define FREQUENCY_22050 (22050)
#define INDEX_22050     (7)
#define FREQUENCY_16000 (16000)
#define INDEX_16000     (8)
#define FREQUENCY_12000 (12000)
#define INDEX_12000     (9)
#define FREQUENCY_11025 (11025)
#define INDEX_11025     (10)
#define FREQUENCY_8000  (8000)
#define INDEX_8000      (11)
#define FREQUENCY_7350  (7350)
#define INDEX_7350      (12)

#define NOMINAL_96000_MIN (92017)
#define NOMINAL_88200_MIN (75132)
#define NOMINAL_64000_MIN (55426)
#define NOMINAL_48000_MIN (46009)
#define NOMINAL_44100_MIN (37566)
#define NOMINAL_32000_MIN (27713)
#define NOMINAL_24000_MIN (23004)
#define NOMINAL_22050_MIN (18783)
#define NOMINAL_16000_MIN (13856)
#define NOMINAL_12000_MIN (11502)
#define NOMINAL_11025_MIN (9391)

#define ELEMENT_TAG_COUNT (16)

#define BYTE_BITS               (8)
#define BIT_MASK                (1)
#define MAX_FIELD_BITS          (24)
#define OBJECT_BITS             (5)
#define OBJECT_ESCAPE           (31)
#define OBJECT_EXT_BASE         (32)
#define OBJECT_EXT_BITS         (6)
#define FREQUENCY_BITS          (4)
#define FREQUENCY_LAST_INDEX    (12)
#define EXPLICIT_FREQUENCY_BITS (24)
#define EXPLICIT_FREQUENCY_MAX  (0xFFFFFF)
#define DEFAULT_FREQUENCY_INDEX (3)
#define DEFAULT_FREQUENCY       (48000)
#define CHANNEL_BITS            (4)
#define CHANNEL_SINGLE          (1)
#define CHANNEL_PAIR            (2)
#define CHANNEL_EIGHT           (8)
#define LAYOUT_EIGHT_CHANNELS   (7)
#define FLAG_BITS               (1)
#define DELAY_BITS              (14)
#define TAG_BITS                (4)
#define PROGRAM_OBJECT_BITS     (2)
#define ELEMENT_COUNT_BITS      (4)
#define LFE_COUNT_BITS          (2)
#define DATA_COUNT_BITS         (3)
#define MATRIX_BITS             (2)
#define COMMENT_MAX             (255)
#define SYNC_BITS               (11)
#define SYNC_MIN_BITS           (16)
#define PS_MIN_BITS             (12)
#define SBR_SYNC                (0x2B7)
#define PS_SYNC                 (0x548)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

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
 * \brief Derives an indexed or explicitly signaled sampling frequency.
 *
 * \par Requirements:
 *  - 001 Reserved indices, zero rates and overflowing explicit rates shall derive zero.
 *
 * \param[in] frequency   Required stable frequency model
 *
 * \retval nonzero Sampling frequency in hertz.
 * \retval 0 Invalid frequency.
 */
static FORCE_INLINE uint32_t GetFrequency(const Utl_Mpeg4Audio_Frequency_t* frequency);

/*************************************************************************************************/
/**
 * \brief Derives the nominal table frequency for an explicitly signaled sample rate.
 *
 * \par Requirements:
 *  - 001 The ranges shall follow ISO/IEC 14496-3:2009 Table 4.82; the actual output rate shall remain unchanged.
 *
 * \param[in] rate   Actual positive frequency
 *
 * \retval frequency Nominal table frequency.
 */
static FORCE_INLINE uint32_t GetNominalFrequency(uint32_t rate);

/*************************************************************************************************/
/**
 * \brief Recognizes the modeled general-audio core objects.
 *
 * \par Requirements:
 *  - 001 Other object types shall remain unsupported, never inferred as AAC LC.
 *
 * \param[in] objectType   Decoded underlying object type
 *
 * \retval TRUE Modeled object.
 * \retval FALSE Other object.
 */
static FORCE_INLINE bool_t IsCoreObject(uint8_t objectType);

/*************************************************************************************************/
/**
 * \brief Counts channels in a prevalidated program element sequence.
 *
 * \par Requirements:
 *  - 001 Only the declared elements shall be read; counts shall fit the specification array.
 *
 * \param[in] elements   Required initialized array
 * \param[in] count      Declared element count
 *
 * \retval count Number of single and paired channels.
 */
static FORCE_INLINE uint32_t GetElementChannels(const Utl_Mpeg4Audio_Element_t* elements, uint32_t count);

/*************************************************************************************************/
/**
 * \brief Derives the core channel count of a completely validated model.
 *
 * \par Requirements:
 *  - 001 Program array bounds shall already be validated.
 *  - 002 Associated-data and coupling elements shall not count as output channels.
 *
 * \param[in] config   Required prevalidated configuration
 *
 * \retval count Declared core channels.
 */
static FORCE_INLINE uint32_t GetCoreChannels(const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Validates all PCE bounds before reading its arrays.
 *
 * \par Requirements:
 *  - 001 Nonempty comment storage shall be required; missing storage shall assert.
 *  - 002 Every declared array count and indexed frequency shall fit its specification domain.
 *
 * \param[in] program   Required stable program
 *
 * \retval TRUE Bounds valid.
 * \retval FALSE Invalid bound or frequency.
 */
static FORCE_INLINE bool_t IsProgram(const Utl_Mpeg4Audio_Program_t* program);

/*************************************************************************************************/
/**
 * \brief Validates explicit, implicit and synchronized SBR/PS state combinations.
 *
 * \par Requirements:
 *  - 001 Unknown shall remain distinct from absent.
 *  - 002 An extension rate shall be required only when SBR is explicitly present.
 *
 * \param[in] config   Required stable model
 *
 * \retval TRUE Representable signaling.
 * \retval FALSE Inconsistent or invalid signaling.
 */
static FORCE_INLINE bool_t IsSignaling(const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Checks configuration semantics and all traversal bounds.
 *
 * \par Requirements:
 *  - 001 Unsupported objects and inconsistent signaling shall not be serialized.
 *  - 002 SSR shall not select a short frame; all active program extents shall be checked.
 *
 * \param[in] config   Required stable model
 *
 * \retval TRUE Semantically representable model.
 * \retval FALSE Invalid or unsupported model.
 */
static FORCE_INLINE bool_t IsConfiguration(const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Checks uniqueness of single and paired element references across PCE groups.
 *
 * \par Requirements:
 *  - 001 All tags and array counts shall be width-validated before this query.
 *  - 002 Single and paired elements shall have separate tag namespaces.
 *
 * \param[in]     elements   Required selected elements
 * \param[in]     count      Validated count
 * \param[in,out] seen       Required shared single/pair tag mask
 *
 * \retval TRUE No duplicate selector.
 * \retval FALSE Repeated selector.
 */
static FORCE_INLINE bool_t AreElementTags(const Utl_Mpeg4Audio_Element_t* elements, uint32_t count, uint32_t* seen);

/*************************************************************************************************/
/**
 * \brief Checks unique low-frequency or associated-data selectors.
 *
 * \par Requirements:
 *  - 001 Array counts and four-bit tags shall already be validated before querying.
 *
 * \param[in] tags    Required complete selected array
 * \param[in] count   Validated selector count
 *
 * \retval TRUE All selectors unique.
 * \retval FALSE Duplicate selector.
 */
static FORCE_INLINE bool_t AreUniqueTags(const uint8_t* tags, uint32_t count);

/*************************************************************************************************/
/**
 * \brief Checks the independent coupling-element tag namespace.
 *
 * \par Requirements:
 *  - 001 Complete field-width and array validation shall precede this query.
 *
 * \param[in] program   Required bounded program
 *
 * \retval TRUE Unique coupling selectors.
 * \retval FALSE Duplicate selector.
 */
static FORCE_INLINE bool_t AreCouplingTags(const Utl_Mpeg4Audio_Program_t* program);

/*************************************************************************************************/
/**
 * \brief Checks the PCE nominal frequency against its enclosing actual rate.
 *
 * \par Requirements:
 *  - 001 The PCE shall use a valid indexed rate, never the explicit-frequency escape.
 *  - 002 An exact indexed frequency or the specified nominal mapping shall match the actual rate.
 *
 * \param[in] config   Required field-validated configuration
 *
 * \retval TRUE Consistent rate.
 * \retval FALSE Contradictory PCE rate.
 */
static FORCE_INLINE bool_t IsProgramFrequency(const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Checks PCE selectors, object identity and nominal sampling frequency.
 *
 * \par Requirements:
 *  - 001 Complete field-width validation shall precede this query.
 *  - 002 The PCE object index shall describe the enclosing core object.
 *  - 003 Referenced elements shall be unique within their own tag namespaces.
 *  - 004 The PCE sampling index shall match the actual rate or its specified nominal mapping.
 *
 * \param[in] config   Required field-validated configuration
 *
 * \retval TRUE Consistent object and unique channel selectors.
 * \retval FALSE Inconsistent PCE.
 */
static FORCE_INLINE bool_t IsProgramContext(const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Checks cross-field relationships after scalar and capacity validation.
 *
 * \par Requirements:
 *  - 001 Program counts and all selectors shall already fit their encoded widths.
 *  - 002 Explicit parametric stereo shall require a mono core.
 *
 * \param[in] config   Required field-validated model
 *
 * \retval TRUE Consistent selected fields.
 * \retval FALSE Inconsistent program or PS core.
 */
static FORCE_INLINE bool_t IsContext(const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Reads one complete most-significant-bit-first field.
 *
 * \par Requirements:
 *  - 001 The cursor shall be initialized over stable bytes; width shall be one through 24.
 *  - 002 Truncation shall set sticky failure without accessing or advancing beyond the input.
 *
 * \param[in,out] reader     Required initialized cursor
 * \param[in]     bitCount   Field width
 *
 * \retval value Field value or zero on sticky failure.
 */
static FORCE_INLINE uint32_t ReadBits(Utl_Mpeg4_BitReader_t* reader, uint32_t bitCount);

/*************************************************************************************************/
/**
 * \brief Reads an ordinary or escaped audio object type.
 *
 * \par Requirements:
 *  - 001 Escaped object values shall consume all six extension bits before classification.
 *
 * \param[in,out] reader   Required initialized cursor
 *
 * \retval value Object code; reader validity records truncation.
 */
static FORCE_INLINE uint8_t ReadObject(Utl_Mpeg4_BitReader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads and validates the complete sampling-frequency syntax.
 *
 * \par Details:
 *  Candidate field and sticky validity updated.
 *
 * \par Requirements:
 *  - 001 Escape shall require all 24 frequency bits; reserved indices and zero frequencies shall fail.
 *
 * \param[out]    frequency   Required candidate field
 * \param[in,out] reader      Required initialized cursor
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void ReadFrequency(Utl_Mpeg4Audio_Frequency_t* frequency, Utl_Mpeg4_BitReader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads a specification-bounded channel map.
 *
 * \par Details:
 *  Elements decoded; reader records truncation.
 *
 * \par Requirements:
 *  - 001 Count shall be previously read from its four-bit field and fit the supplied array.
 *
 * \param[out]    elements   Required complete destination array
 * \param[in,out] reader     Required initialized cursor
 * \param[in]     count      Element count
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void ReadElements(Utl_Mpeg4Audio_Element_t* elements,
                                      Utl_Mpeg4_BitReader_t*    reader,
                                      uint32_t                  count);

/*************************************************************************************************/
/**
 * \brief Reads the program mixdown fields selected by presence flags.
 *
 * \par Details:
 *  Selected mixdown fields read.
 *
 * \par Requirements:
 *  - 001 Inactive optional values shall remain in their initialized state.
 *
 * \param[in,out] program   Required initialized candidate
 * \param[in,out] reader    Required initialized cursor
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void ReadProgramMixdown(Utl_Mpeg4Audio_Program_t* program, Utl_Mpeg4_BitReader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads the complete PCE syntax including arrays, relative alignment and comments.
 *
 * \par Details:
 *  Candidate decoded; sticky validity records truncation.
 *
 * \par Requirements:
 *  - 001 All array counts shall come from bounded specification fields.
 *  - 002 Comment bytes shall be borrowed only after complete extent validation.
 *  - 003 Byte alignment shall be relative to the beginning of AudioSpecificConfig.
 *
 * \param[in,out] program   Required initialized candidate
 * \param[in,out] reader    Required initialized cursor
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void ReadProgram(Utl_Mpeg4Audio_Program_t* program, Utl_Mpeg4_BitReader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads general audio fields for a supported core object.
 *
 * \par Requirements:
 *  - 001 Core-coder delay and PCE shall be consumed only when selected.
 *  - 002 A nonzero general extension flag is outside the modeled Main/LC/SSR/LTP profile.
 *
 * \param[in,out] config   Required initialized candidate
 * \param[in,out] reader   Required initialized cursor
 *
 * \retval COMPLETE Modeled general syntax.
 * \retval UNSUPPORTED General extension syntax.
 */
static FORCE_INLINE Utl_Mpeg4Audio_Status_t ReadGeneral(Utl_Mpeg4Audio_Config_t* config, Utl_Mpeg4_BitReader_t* reader);

/*************************************************************************************************/
/**
 * \brief Reads optional PS signaling after a complete SBR extension.
 *
 * \par Requirements:
 *  - 001 An unrecognized marker shall remain unsupported without inventing a presence value.
 *
 * \param[in,out] config   Required candidate
 * \param[in,out] reader   Required cursor
 *
 * \retval COMPLETE Known or absent PS extension.
 * \retval UNSUPPORTED Unknown marker.
 */
static FORCE_INLINE Utl_Mpeg4Audio_Status_t ReadStereoExtension(Utl_Mpeg4Audio_Config_t* config,
                                                                Utl_Mpeg4_BitReader_t*   reader);

/*************************************************************************************************/
/**
 * \brief Reads backward-compatible SBR and optional PS signaling.
 *
 * \par Requirements:
 *  - 001 Unknown extension markers or objects shall be reported as unsupported.
 *  - 002 Explicit absence shall be distinct from unsignaled presence.
 *
 * \param[in,out] config   Required candidate with implicit signaling
 * \param[in,out] reader   Required initialized cursor
 *
 * \retval COMPLETE Known or absent extension.
 * \retval UNSUPPORTED Unknown extension syntax.
 */
static FORCE_INLINE Utl_Mpeg4Audio_Status_t ReadSyncExtension(Utl_Mpeg4Audio_Config_t* config,
                                                              Utl_Mpeg4_BitReader_t*   reader);

/*************************************************************************************************/
/**
 * \brief Validates the terminal byte alignment after modeled configuration syntax.
 *
 * \par Requirements:
 *  - 001 At most seven bits shall be treated as terminal padding without assigning them codec semantics.
 *  - 002 Additional bytes shall remain unsupported rather than silently discarded.
 *
 * \param[in,out] reader   Required valid cursor
 *
 * \retval COMPLETE Empty or terminal alignment.
 * \retval UNSUPPORTED Additional syntax.
 */
static FORCE_INLINE Utl_Mpeg4Audio_Status_t ReadPadding(Utl_Mpeg4_BitReader_t* reader);

/*************************************************************************************************/
/**
 * \brief Measures or writes the selected indexed/explicit frequency.
 *
 * \par Details:
 *  Complete frequency syntax measured or written.
 *
 * \par Requirements:
 *  - 001 The frequency shall be semantically valid before serialization.
 *
 * \param[in,out] writer      Required cursor
 * \param[in]     frequency   Required validated frequency
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteFrequency(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4Audio_Frequency_t* frequency);

/*************************************************************************************************/
/**
 * \brief Measures or writes selected program channel elements.
 *
 * \par Details:
 *  Selected element bits measured or written.
 *
 * \par Requirements:
 *  - 001 The count shall fit the declared array; inactive entries shall not be read.
 *
 * \param[in,out] writer     Required cursor
 * \param[in]     elements   Required selected elements
 * \param[in]     count      Validated element count
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteElements(Utl_Mpeg4_BitWriter_t*          writer,
                                       const Utl_Mpeg4Audio_Element_t* elements,
                                       uint32_t                        count);

/*************************************************************************************************/
/**
 * \brief Measures or writes only the present mixdown fields.
 *
 * \par Details:
 *  Selected mixdown syntax measured or written.
 *
 * \par Requirements:
 *  - 001 Inactive optional fields shall not be inspected.
 *
 * \param[in,out] writer    Required cursor
 * \param[in]     program   Required program
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteProgramMixdown(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4Audio_Program_t* program);

/*************************************************************************************************/
/**
 * \brief Measures or writes the complete PCE including opaque comment bytes.
 *
 * \par Details:
 *  PCE measured or written.
 *
 * \par Requirements:
 *  - 001 Array and comment counts shall be validated before traversal.
 *  - 002 Byte alignment shall be relative to the beginning of AudioSpecificConfig.
 *
 * \param[in,out] writer    Required cursor
 * \param[in]     program   Required bounded model
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteProgram(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4Audio_Program_t* program);

/*************************************************************************************************/
/**
 * \brief Measures or writes the optional backward-compatible extension.
 *
 * \par Details:
 *  Extension measured or written.
 *
 * \par Requirements:
 *  - 001 Signaling and presence combinations shall already be validated.
 *
 * \param[in,out] writer   Required cursor
 * \param[in]     config   Required validated model
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteExtension(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4Audio_Config_t* config);

/*************************************************************************************************/
/**
 * \brief Measures or writes a complete configuration with the selected signaling form.
 *
 * \par Details:
 *  Complete unpadded bit count measured or written.
 *
 * \par Requirements:
 *  - 001 Semantic validation shall precede serialization; field widths are checked during measurement.
 *  - 002 Only fields active for this model shall be inspected.
 *
 * \param[in,out] writer   Required cursor
 * \param[in]     config   Required stable model
 *
 * \retval None No value is returned.
 */
static FORCE_INLINE void WriteConfiguration(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4Audio_Config_t* config);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetFrequency(const Utl_Mpeg4Audio_Frequency_t* frequency)
{
    uint32_t rate;

    LOGASSERT(NULL != frequency);
    rate = 0;
    switch (frequency->index)
    {
    case INDEX_96000:
        rate = FREQUENCY_96000;
        break;
    case INDEX_88200:
        rate = FREQUENCY_88200;
        break;
    case INDEX_64000:
        rate = FREQUENCY_64000;
        break;
    case INDEX_48000:
        rate = FREQUENCY_48000;
        break;
    case INDEX_44100:
        rate = FREQUENCY_44100;
        break;
    case INDEX_32000:
        rate = FREQUENCY_32000;
        break;
    case INDEX_24000:
        rate = FREQUENCY_24000;
        break;
    case INDEX_22050:
        rate = FREQUENCY_22050;
        break;
    case INDEX_16000:
        rate = FREQUENCY_16000;
        break;
    case INDEX_12000:
        rate = FREQUENCY_12000;
        break;
    case INDEX_11025:
        rate = FREQUENCY_11025;
        break;
    case INDEX_8000:
        rate = FREQUENCY_8000;
        break;
    case INDEX_7350:
        rate = FREQUENCY_7350;
        break;

    case UTL_MPEG4AUDIO_FREQUENCY_ESCAPE:
        if (frequency->hertz <= EXPLICIT_FREQUENCY_MAX)
        {
            rate = frequency->hertz;
        }
        break;
    default:
        break;
    }
    return rate;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetNominalFrequency(uint32_t rate)
{
    uint32_t nominal;
    if (NOMINAL_96000_MIN <= rate)
    {
        nominal = FREQUENCY_96000;
    }
    else if (NOMINAL_88200_MIN <= rate)
    {
        nominal = FREQUENCY_88200;
    }
    else if (NOMINAL_64000_MIN <= rate)
    {
        nominal = FREQUENCY_64000;
    }
    else if (NOMINAL_48000_MIN <= rate)
    {
        nominal = FREQUENCY_48000;
    }
    else if (NOMINAL_44100_MIN <= rate)
    {
        nominal = FREQUENCY_44100;
    }
    else if (NOMINAL_32000_MIN <= rate)
    {
        nominal = FREQUENCY_32000;
    }
    else if (NOMINAL_24000_MIN <= rate)
    {
        nominal = FREQUENCY_24000;
    }
    else if (NOMINAL_22050_MIN <= rate)
    {
        nominal = FREQUENCY_22050;
    }
    else if (NOMINAL_16000_MIN <= rate)
    {
        nominal = FREQUENCY_16000;
    }
    else if (NOMINAL_12000_MIN <= rate)
    {
        nominal = FREQUENCY_12000;
    }
    else if (NOMINAL_11025_MIN <= rate)
    {
        nominal = FREQUENCY_11025;
    }
    else
    {
        nominal = FREQUENCY_8000;
    }
    return nominal;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsCoreObject(uint8_t objectType)
{
    bool_t above_reserved;
    bool_t within_core;

    above_reserved = (UTL_MPEG4AUDIO_MAIN <= objectType);
    within_core    = (objectType <= UTL_MPEG4AUDIO_LTP);
    return above_reserved && within_core;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetElementChannels(const Utl_Mpeg4Audio_Element_t* elements, uint32_t count)
{
    uint32_t channels;
    uint32_t index;

    LOGASSERT(NULL != elements);
    LOGASSERT(count <= UTL_MPEG4AUDIO_ELEMENT_LIMIT);
    channels = 0;
    for (index = 0; index < count; ++index)
    {
        channels += elements[index].paired ? CHANNEL_PAIR : CHANNEL_SINGLE;
    }
    return channels;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint32_t GetCoreChannels(const Utl_Mpeg4Audio_Config_t* config)
{
    uint32_t channels;

    LOGASSERT(NULL != config);
    channels = config->channelConfiguration;
    if (LAYOUT_EIGHT_CHANNELS == channels)
    {
        channels = CHANNEL_EIGHT;
    }
    else if (0 == channels)
    {
        channels = GetElementChannels(config->program.front, config->program.frontCount);
        channels += GetElementChannels(config->program.side, config->program.sideCount);
        channels += GetElementChannels(config->program.back, config->program.backCount);
        channels += config->program.lfeCount;
    }
    else
    {
        /* Standard layouts one through six have the same number of channels. */
    }
    return channels;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsProgram(const Utl_Mpeg4Audio_Program_t* program)
{
    bool_t front_fits;
    bool_t side_fits;
    bool_t back_fits;
    bool_t lfe_fits;
    bool_t data_fits;
    bool_t coupling_fits;
    bool_t comment_fits;
    bool_t frequency_valid;
    bool_t comment_empty;
    bool_t has_comment;
    bool_t storage_valid;

    front_fits      = (program->frontCount <= UTL_MPEG4AUDIO_ELEMENT_LIMIT);
    side_fits       = (program->sideCount <= UTL_MPEG4AUDIO_ELEMENT_LIMIT);
    back_fits       = (program->backCount <= UTL_MPEG4AUDIO_ELEMENT_LIMIT);
    lfe_fits        = (program->lfeCount <= UTL_MPEG4AUDIO_LFE_LIMIT);
    data_fits       = (program->dataCount <= UTL_MPEG4AUDIO_DATA_LIMIT);
    coupling_fits   = (program->couplingCount <= UTL_MPEG4AUDIO_ELEMENT_LIMIT);
    comment_fits    = (program->comment.byteCount <= COMMENT_MAX);
    frequency_valid = (program->frequencyIndex <= FREQUENCY_LAST_INDEX);
    comment_empty   = (0 == program->comment.byteCount);
    has_comment     = (NULL != program->comment.data);
    storage_valid   = comment_empty || has_comment;
    LOGASSERT(storage_valid);
    return front_fits && side_fits && back_fits && lfe_fits && data_fits && coupling_fits && comment_fits &&
           frequency_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsSignaling(const Utl_Mpeg4Audio_Config_t* config)
{
    bool_t sbr_unknown;
    bool_t ps_unknown;
    bool_t sbr_absent;
    bool_t sbr_present;
    bool_t ps_absent;
    bool_t ps_present;
    bool_t ps_known;
    bool_t explicit_ps;
    bool_t absent_sync;
    bool_t present_sync;
    bool_t valid;

    sbr_unknown  = (UTL_MPEG4AUDIO_UNKNOWN == config->sbr);
    ps_unknown   = (UTL_MPEG4AUDIO_UNKNOWN == config->ps);
    sbr_absent   = (UTL_MPEG4AUDIO_ABSENT == config->sbr);
    sbr_present  = (UTL_MPEG4AUDIO_PRESENT == config->sbr);
    ps_absent    = (UTL_MPEG4AUDIO_ABSENT == config->ps);
    ps_present   = (UTL_MPEG4AUDIO_PRESENT == config->ps);
    ps_known     = ps_unknown || ps_absent || ps_present;
    explicit_ps  = ps_unknown || ps_present;
    absent_sync  = sbr_absent && ps_unknown;
    present_sync = sbr_present && ps_known;
    valid        = FALSE;
    if (UTL_MPEG4AUDIO_IMPLICIT == config->signaling)
    {
        valid = sbr_unknown && ps_unknown;
    }
    else if (UTL_MPEG4AUDIO_EXPLICIT == config->signaling)
    {
        valid = sbr_present && explicit_ps;
    }
    else if (UTL_MPEG4AUDIO_SYNC == config->signaling)
    {
        valid = absent_sync || present_sync;
    }
    else
    {
        /* Unknown signaling representations are not serialized. */
    }
    if (sbr_present)
    {
        if (0 == GetFrequency(&config->extensionFrequency))
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
static FORCE_INLINE bool_t IsConfiguration(const Utl_Mpeg4Audio_Config_t* config)
{
    bool_t core_valid;
    bool_t frequency_valid;
    bool_t layout_valid;
    bool_t signaling_valid;
    bool_t is_ssr;
    bool_t frame_valid;
    bool_t valid;

    core_valid      = IsCoreObject(config->objectType);
    frequency_valid = (0 < GetFrequency(&config->frequency));
    layout_valid    = (config->channelConfiguration <= LAYOUT_EIGHT_CHANNELS);
    signaling_valid = IsSignaling(config);
    is_ssr          = (UTL_MPEG4AUDIO_SSR == config->objectType);
    frame_valid     = !(is_ssr && config->shortFrame);
    valid           = core_valid && frequency_valid && layout_valid && signaling_valid && frame_valid;
    if (0 == config->channelConfiguration)
    {
        if (!IsProgram(&config->program))
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
static FORCE_INLINE bool_t AreElementTags(const Utl_Mpeg4Audio_Element_t* elements, uint32_t count, uint32_t* seen)
{
    bool_t   valid;
    uint32_t index;
    uint32_t selector;
    uint32_t bit;
    bool_t   unique;

    LOGASSERT(NULL != elements);
    LOGASSERT(NULL != seen);
    LOGASSERT(count <= UTL_MPEG4AUDIO_ELEMENT_LIMIT);
    valid = TRUE;
    for (index = 0; index < count; ++index)
    {
        LOGASSERT(elements[index].tag < ELEMENT_TAG_COUNT);
        selector = elements[index].tag;
        if (elements[index].paired)
        {
            selector += ELEMENT_TAG_COUNT;
        }
        bit    = (uint32_t)1 << selector;
        unique = !(*seen & bit);
        valid  = valid && unique;
        *seen  |= bit;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t AreUniqueTags(const uint8_t* tags, uint32_t count)
{
    uint32_t seen;
    bool_t   valid;
    uint32_t index;
    uint32_t bit;
    bool_t   unique;
    LOGASSERT(NULL != tags);
    LOGASSERT(count <= ELEMENT_TAG_COUNT);
    seen  = 0;
    valid = TRUE;
    for (index = 0; index < count; ++index)
    {
        LOGASSERT(tags[index] < ELEMENT_TAG_COUNT);
        bit    = (uint32_t)1 << tags[index];
        unique = !(seen & bit);
        valid  = valid && unique;
        seen   |= bit;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t AreCouplingTags(const Utl_Mpeg4Audio_Program_t* program)
{
    uint32_t seen;
    bool_t   valid;
    uint32_t index;
    uint32_t bit;
    bool_t   unique;
    LOGASSERT(NULL != program);
    LOGASSERT(program->couplingCount <= UTL_MPEG4AUDIO_ELEMENT_LIMIT);
    seen  = 0;
    valid = TRUE;
    for (index = 0; index < program->couplingCount; ++index)
    {
        LOGASSERT(program->coupling[index].tag < ELEMENT_TAG_COUNT);
        bit    = (uint32_t)1 << program->coupling[index].tag;
        unique = !(seen & bit);
        valid  = valid && unique;
        seen   |= bit;
    }
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsProgramFrequency(const Utl_Mpeg4Audio_Config_t* config)
{
    Utl_Mpeg4Audio_Frequency_t program_frequency;
    uint32_t                   actual;
    uint32_t                   nominal;
    uint32_t                   signaled;
    bool_t                     exact;
    bool_t                     mapped;
    program_frequency.index = config->program.frequencyIndex;
    program_frequency.hertz = 0;
    actual                  = GetFrequency(&config->frequency);
    nominal                 = GetNominalFrequency(actual);
    signaled                = GetFrequency(&program_frequency);
    exact                   = (actual == signaled);
    mapped                  = (nominal == signaled);
    return exact || mapped;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsProgramContext(const Utl_Mpeg4Audio_Config_t* config)
{
    uint32_t seen;
    bool_t   front_unique;
    bool_t   side_unique;
    bool_t   back_unique;
    bool_t   lfe_unique;
    bool_t   data_unique;
    bool_t   coupling_unique;
    bool_t   object_matches;
    bool_t   frequency_matches;
    bool_t   valid;
    seen              = 0;
    front_unique      = AreElementTags(config->program.front, config->program.frontCount, &seen);
    side_unique       = AreElementTags(config->program.side, config->program.sideCount, &seen);
    back_unique       = AreElementTags(config->program.back, config->program.backCount, &seen);
    lfe_unique        = AreUniqueTags(config->program.lfe, config->program.lfeCount);
    data_unique       = AreUniqueTags(config->program.data, config->program.dataCount);
    coupling_unique   = AreCouplingTags(&config->program);
    object_matches    = ((uint32_t)config->program.objectIndex + 1 == config->objectType);
    frequency_matches = IsProgramFrequency(config);
    valid             = front_unique && side_unique && back_unique && lfe_unique && data_unique && coupling_unique &&
                        object_matches && frequency_matches;
    return valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE bool_t IsContext(const Utl_Mpeg4Audio_Config_t* config)
{
    bool_t valid;

    valid = TRUE;
    if (0 == config->channelConfiguration)
    {
        valid = IsProgramContext(config);
    }
    if (UTL_MPEG4AUDIO_PRESENT == config->ps)
    {
        if (CHANNEL_SINGLE != GetCoreChannels(config))
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
static FORCE_INLINE uint32_t ReadBits(Utl_Mpeg4_BitReader_t* reader, uint32_t bitCount)
{
    uint64_t value;

    LOGASSERT(bitCount <= MAX_FIELD_BITS);
    value = Utl_Mpeg4_ReadBits(reader, bitCount);
    return (uint32_t)value;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE uint8_t ReadObject(Utl_Mpeg4_BitReader_t* reader)
{
    uint32_t object;

    object = ReadBits(reader, OBJECT_BITS);
    if (OBJECT_ESCAPE == object)
    {
        object = OBJECT_EXT_BASE + ReadBits(reader, OBJECT_EXT_BITS);
    }
    return (uint8_t)object;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void ReadFrequency(Utl_Mpeg4Audio_Frequency_t* frequency, Utl_Mpeg4_BitReader_t* reader)
{
    LOGASSERT(NULL != frequency);
    frequency->index = (uint8_t)ReadBits(reader, FREQUENCY_BITS);
    frequency->hertz = 0;
    if (UTL_MPEG4AUDIO_FREQUENCY_ESCAPE == frequency->index)
    {
        frequency->hertz = ReadBits(reader, EXPLICIT_FREQUENCY_BITS);
    }
    frequency->hertz = GetFrequency(frequency);
    if (0 == frequency->hertz)
    {
        reader->valid = FALSE;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void ReadElements(Utl_Mpeg4Audio_Element_t* elements, Utl_Mpeg4_BitReader_t* reader, uint32_t count)
{
    uint32_t index;

    LOGASSERT(NULL != elements);
    LOGASSERT(count <= UTL_MPEG4AUDIO_ELEMENT_LIMIT);
    for (index = 0; index < count; ++index)
    {
        elements[index].paired = (ReadBits(reader, FLAG_BITS) == BIT_MASK);
        elements[index].tag    = (uint8_t)ReadBits(reader, TAG_BITS);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void ReadProgramMixdown(Utl_Mpeg4Audio_Program_t* program, Utl_Mpeg4_BitReader_t* reader)
{
    program->hasMonoMixdown = (ReadBits(reader, FLAG_BITS) == BIT_MASK);
    if (program->hasMonoMixdown)
    {
        program->monoTag = (uint8_t)ReadBits(reader, TAG_BITS);
    }
    program->hasStereoMixdown = (ReadBits(reader, FLAG_BITS) == BIT_MASK);
    if (program->hasStereoMixdown)
    {
        program->stereoTag = (uint8_t)ReadBits(reader, TAG_BITS);
    }
    program->hasMatrixMixdown = (ReadBits(reader, FLAG_BITS) == BIT_MASK);
    if (program->hasMatrixMixdown)
    {
        program->matrixIndex    = (uint8_t)ReadBits(reader, MATRIX_BITS);
        program->pseudoSurround = (ReadBits(reader, FLAG_BITS) == BIT_MASK);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void ReadProgram(Utl_Mpeg4Audio_Program_t* program, Utl_Mpeg4_BitReader_t* reader)
{
    uint32_t index;
    uint32_t padding;
    uint32_t comment_bytes;
    uint64_t remaining;
    bool_t   fits;
    bool_t   can_borrow;

    LOGASSERT(NULL != program);
    program->tag            = (uint8_t)ReadBits(reader, TAG_BITS);
    program->objectIndex    = (uint8_t)ReadBits(reader, PROGRAM_OBJECT_BITS);
    program->frequencyIndex = (uint8_t)ReadBits(reader, FREQUENCY_BITS);
    program->frontCount     = (uint8_t)ReadBits(reader, ELEMENT_COUNT_BITS);
    program->sideCount      = (uint8_t)ReadBits(reader, ELEMENT_COUNT_BITS);
    program->backCount      = (uint8_t)ReadBits(reader, ELEMENT_COUNT_BITS);
    program->lfeCount       = (uint8_t)ReadBits(reader, LFE_COUNT_BITS);
    program->dataCount      = (uint8_t)ReadBits(reader, DATA_COUNT_BITS);
    program->couplingCount  = (uint8_t)ReadBits(reader, ELEMENT_COUNT_BITS);
    ReadProgramMixdown(program, reader);
    ReadElements(program->front, reader, program->frontCount);
    ReadElements(program->side, reader, program->sideCount);
    ReadElements(program->back, reader, program->backCount);
    for (index = 0; index < program->lfeCount; ++index)
    {
        program->lfe[index] = (uint8_t)ReadBits(reader, TAG_BITS);
    }
    for (index = 0; index < program->dataCount; ++index)
    {
        program->data[index] = (uint8_t)ReadBits(reader, TAG_BITS);
    }
    for (index = 0; index < program->couplingCount; ++index)
    {
        program->coupling[index].independent = (ReadBits(reader, FLAG_BITS) == BIT_MASK);
        program->coupling[index].tag         = (uint8_t)ReadBits(reader, TAG_BITS);
    }
    padding = (BYTE_BITS - (uint32_t)(reader->position % BYTE_BITS)) % BYTE_BITS;
    if (0 < padding)
    {
        (void)ReadBits(reader, padding);
    }
    comment_bytes = ReadBits(reader, BYTE_BITS);
    remaining     = ((uint64_t)reader->bytes.byteCount * BYTE_BITS) - reader->position;
    fits          = ((uint64_t)comment_bytes * BYTE_BITS <= remaining);
    can_borrow    = reader->valid && fits;
    reader->valid = can_borrow;
    if (can_borrow)
    {
        program->comment.byteCount = comment_bytes;
        if (0 < comment_bytes)
        {
            program->comment.data = &reader->bytes.data[(uint32_t)(reader->position / BYTE_BITS)];
        }
        reader->position += (uint64_t)comment_bytes * BYTE_BITS;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Mpeg4Audio_Status_t ReadGeneral(Utl_Mpeg4Audio_Config_t* config, Utl_Mpeg4_BitReader_t* reader)
{
    uint32_t                extension;
    Utl_Mpeg4Audio_Status_t status;

    config->shortFrame   = (ReadBits(reader, FLAG_BITS) == BIT_MASK);
    config->hasCoreCoder = (ReadBits(reader, FLAG_BITS) == BIT_MASK);
    if (config->hasCoreCoder)
    {
        config->coreCoderDelay = (uint16_t)ReadBits(reader, DELAY_BITS);
    }
    extension = ReadBits(reader, FLAG_BITS);
    status    = UTL_MPEG4AUDIO_COMPLETE;
    if (0 == config->channelConfiguration)
    {
        ReadProgram(&config->program, reader);
    }
    if (0 < extension)
    {
        status = UTL_MPEG4AUDIO_UNSUPPORTED;
    }
    return status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Mpeg4Audio_Status_t ReadStereoExtension(Utl_Mpeg4Audio_Config_t* config,
                                                                Utl_Mpeg4_BitReader_t*   reader)
{
    uint64_t                remaining;
    Utl_Mpeg4Audio_Status_t status;
    uint32_t                sync;

    remaining = ((uint64_t)reader->bytes.byteCount * BYTE_BITS) - reader->position;
    status    = UTL_MPEG4AUDIO_COMPLETE;
    if (PS_MIN_BITS <= remaining)
    {
        sync = ReadBits(reader, SYNC_BITS);
        if (PS_SYNC == sync)
        {
            config->ps = ReadBits(reader, FLAG_BITS) ? UTL_MPEG4AUDIO_PRESENT : UTL_MPEG4AUDIO_ABSENT;
        }
        else
        {
            status = UTL_MPEG4AUDIO_UNSUPPORTED;
        }
    }
    return status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Mpeg4Audio_Status_t ReadSyncExtension(Utl_Mpeg4Audio_Config_t* config,
                                                              Utl_Mpeg4_BitReader_t*   reader)
{
    uint64_t                remaining;
    Utl_Mpeg4Audio_Status_t status;
    uint32_t                sync;
    uint8_t                 object;

    remaining = ((uint64_t)reader->bytes.byteCount * BYTE_BITS) - reader->position;
    status    = UTL_MPEG4AUDIO_COMPLETE;
    if (SYNC_MIN_BITS <= remaining)
    {
        sync   = ReadBits(reader, SYNC_BITS);
        status = UTL_MPEG4AUDIO_UNSUPPORTED;
        if (SBR_SYNC == sync)
        {
            object = ReadObject(reader);
            if (UTL_MPEG4AUDIO_SBR == object)
            {
                config->signaling = UTL_MPEG4AUDIO_SYNC;
                config->sbr       = ReadBits(reader, FLAG_BITS) ? UTL_MPEG4AUDIO_PRESENT : UTL_MPEG4AUDIO_ABSENT;
                status            = UTL_MPEG4AUDIO_COMPLETE;
                if (UTL_MPEG4AUDIO_PRESENT == config->sbr)
                {
                    ReadFrequency(&config->extensionFrequency, reader);
                    if (reader->valid)
                    {
                        status = ReadStereoExtension(config, reader);
                    }
                }
            }
        }
    }
    return status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Mpeg4Audio_Status_t ReadPadding(Utl_Mpeg4_BitReader_t* reader)
{
    uint64_t                remaining;
    Utl_Mpeg4Audio_Status_t status;

    remaining = ((uint64_t)reader->bytes.byteCount * BYTE_BITS) - reader->position;
    status    = UTL_MPEG4AUDIO_UNSUPPORTED;
    if (remaining < BYTE_BITS)
    {
        status = UTL_MPEG4AUDIO_COMPLETE;
        if (0 < remaining)
        {
            (void)ReadBits(reader, (uint32_t)remaining);
        }
    }
    return status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteFrequency(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4Audio_Frequency_t* frequency)
{
    Utl_Mpeg4_BitField_t field;

    field.value    = frequency->index;
    field.bitCount = FREQUENCY_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    if (UTL_MPEG4AUDIO_FREQUENCY_ESCAPE == frequency->index)
    {
        field.value    = frequency->hertz;
        field.bitCount = EXPLICIT_FREQUENCY_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteElements(Utl_Mpeg4_BitWriter_t*          writer,
                                       const Utl_Mpeg4Audio_Element_t* elements,
                                       uint32_t                        count)
{
    uint32_t             index;
    Utl_Mpeg4_BitField_t field;

    LOGASSERT(count <= UTL_MPEG4AUDIO_ELEMENT_LIMIT);
    for (index = 0; index < count; ++index)
    {
        field.value    = elements[index].paired ? 1 : 0;
        field.bitCount = FLAG_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
        field.value    = elements[index].tag;
        field.bitCount = TAG_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteProgramMixdown(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4Audio_Program_t* program)
{
    Utl_Mpeg4_BitField_t field;

    field.value    = program->hasMonoMixdown ? 1 : 0;
    field.bitCount = FLAG_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    if (program->hasMonoMixdown)
    {
        field.value    = program->monoTag;
        field.bitCount = TAG_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
    }
    field.value    = program->hasStereoMixdown ? 1 : 0;
    field.bitCount = FLAG_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    if (program->hasStereoMixdown)
    {
        field.value    = program->stereoTag;
        field.bitCount = TAG_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
    }
    field.value    = program->hasMatrixMixdown ? 1 : 0;
    field.bitCount = FLAG_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    if (program->hasMatrixMixdown)
    {
        field.value    = program->matrixIndex;
        field.bitCount = MATRIX_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
        field.value    = program->pseudoSurround ? 1 : 0;
        field.bitCount = FLAG_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteProgram(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4Audio_Program_t* program)
{
    Utl_Mpeg4_BitField_t field;
    uint32_t             index;
    uint32_t             padding;

    field.value    = program->tag;
    field.bitCount = TAG_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    field.value    = program->objectIndex;
    field.bitCount = PROGRAM_OBJECT_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    field.value    = program->frequencyIndex;
    field.bitCount = FREQUENCY_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    field.value    = program->frontCount;
    field.bitCount = ELEMENT_COUNT_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    field.value    = program->sideCount;
    field.bitCount = ELEMENT_COUNT_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    field.value    = program->backCount;
    field.bitCount = ELEMENT_COUNT_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    field.value    = program->lfeCount;
    field.bitCount = LFE_COUNT_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    field.value    = program->dataCount;
    field.bitCount = DATA_COUNT_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    field.value    = program->couplingCount;
    field.bitCount = ELEMENT_COUNT_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    WriteProgramMixdown(writer, program);
    WriteElements(writer, program->front, program->frontCount);
    WriteElements(writer, program->side, program->sideCount);
    WriteElements(writer, program->back, program->backCount);
    for (index = 0; index < program->lfeCount; ++index)
    {
        field.value    = program->lfe[index];
        field.bitCount = TAG_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
    }
    for (index = 0; index < program->dataCount; ++index)
    {
        field.value    = program->data[index];
        field.bitCount = TAG_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
    }
    for (index = 0; index < program->couplingCount; ++index)
    {
        field.value    = program->coupling[index].independent ? 1 : 0;
        field.bitCount = FLAG_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
        field.value    = program->coupling[index].tag;
        field.bitCount = TAG_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
    }
    padding = (uint32_t)((BYTE_BITS - (writer->position % BYTE_BITS)) % BYTE_BITS);
    if (0 < padding)
    {
        field.value    = 0;
        field.bitCount = padding;
        Utl_Mpeg4_WriteBits(writer, field);
    }
    field.value    = program->comment.byteCount;
    field.bitCount = BYTE_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    Utl_Mpeg4_WriteAlignedBytes(writer, program->comment);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteExtension(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4Audio_Config_t* config)
{
    Utl_Mpeg4_BitField_t field;

    if (UTL_MPEG4AUDIO_SYNC == config->signaling)
    {
        field.value    = SBR_SYNC;
        field.bitCount = SYNC_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
        field.value    = UTL_MPEG4AUDIO_SBR;
        field.bitCount = OBJECT_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
        field.value    = (UTL_MPEG4AUDIO_PRESENT == config->sbr) ? 1 : 0;
        field.bitCount = FLAG_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
        if (UTL_MPEG4AUDIO_PRESENT == config->sbr)
        {
            WriteFrequency(writer, &config->extensionFrequency);
            if (UTL_MPEG4AUDIO_UNKNOWN != config->ps)
            {
                field.value    = PS_SYNC;
                field.bitCount = SYNC_BITS;
                Utl_Mpeg4_WriteBits(writer, field);
                field.value    = (UTL_MPEG4AUDIO_PRESENT == config->ps) ? 1 : 0;
                field.bitCount = FLAG_BITS;
                Utl_Mpeg4_WriteBits(writer, field);
            }
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void WriteConfiguration(Utl_Mpeg4_BitWriter_t* writer, const Utl_Mpeg4Audio_Config_t* config)
{
    uint32_t             leading_object;
    Utl_Mpeg4_BitField_t field;

    leading_object = config->objectType;
    if (UTL_MPEG4AUDIO_EXPLICIT == config->signaling)
    {
        leading_object = (UTL_MPEG4AUDIO_PRESENT == config->ps) ? UTL_MPEG4AUDIO_PS : UTL_MPEG4AUDIO_SBR;
    }
    field.value    = leading_object;
    field.bitCount = OBJECT_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    WriteFrequency(writer, &config->frequency);
    field.value    = config->channelConfiguration;
    field.bitCount = CHANNEL_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    if (UTL_MPEG4AUDIO_EXPLICIT == config->signaling)
    {
        WriteFrequency(writer, &config->extensionFrequency);
        field.value    = config->objectType;
        field.bitCount = OBJECT_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
    }
    field.value    = config->shortFrame ? 1 : 0;
    field.bitCount = FLAG_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    field.value    = config->hasCoreCoder ? 1 : 0;
    field.bitCount = FLAG_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    if (config->hasCoreCoder)
    {
        field.value    = config->coreCoderDelay;
        field.bitCount = DELAY_BITS;
        Utl_Mpeg4_WriteBits(writer, field);
    }
    field.value    = 0;
    field.bitCount = FLAG_BITS;
    Utl_Mpeg4_WriteBits(writer, field);
    if (0 == config->channelConfiguration)
    {
        WriteProgram(writer, &config->program);
    }
    WriteExtension(writer, config);
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE void Utl_Mpeg4Audio_InitializeConfig(Utl_Mpeg4Audio_Config_t* config)
{
    LOGASSERT(NULL != config);
    config->objectType               = UTL_MPEG4AUDIO_LC;
    config->frequency.index          = DEFAULT_FREQUENCY_INDEX;
    config->frequency.hertz          = DEFAULT_FREQUENCY;
    config->channelConfiguration     = CHANNEL_PAIR;
    config->shortFrame               = FALSE;
    config->hasCoreCoder             = FALSE;
    config->coreCoderDelay           = 0;
    config->program.tag              = 0;
    config->program.objectIndex      = 0;
    config->program.frequencyIndex   = 0;
    config->program.frontCount       = 0;
    config->program.sideCount        = 0;
    config->program.backCount        = 0;
    config->program.lfeCount         = 0;
    config->program.dataCount        = 0;
    config->program.couplingCount    = 0;
    config->program.hasMonoMixdown   = FALSE;
    config->program.monoTag          = 0;
    config->program.hasStereoMixdown = FALSE;
    config->program.stereoTag        = 0;
    config->program.hasMatrixMixdown = FALSE;
    config->program.matrixIndex      = 0;
    config->program.pseudoSurround   = FALSE;
    CRT_Memory_Set(config->program.front, 0, sizeof(config->program.front));
    CRT_Memory_Set(config->program.side, 0, sizeof(config->program.side));
    CRT_Memory_Set(config->program.back, 0, sizeof(config->program.back));
    CRT_Memory_Set(config->program.lfe, 0, sizeof(config->program.lfe));
    CRT_Memory_Set(config->program.data, 0, sizeof(config->program.data));
    CRT_Memory_Set(config->program.coupling, 0, sizeof(config->program.coupling));
    config->program.comment.data      = NULL;
    config->program.comment.byteCount = 0;
    config->signaling                 = UTL_MPEG4AUDIO_IMPLICIT;
    config->sbr                       = UTL_MPEG4AUDIO_UNKNOWN;
    config->ps                        = UTL_MPEG4AUDIO_UNKNOWN;
    config->extensionFrequency.index  = 0;
    config->extensionFrequency.hertz  = 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4Audio_GetSampleRate(const Utl_Mpeg4Audio_Config_t* config)
{
    uint32_t rate;

    LOGASSERT(NULL != config);
    rate = 0;
    if (0 < Utl_Mpeg4Audio_GetConfigByteCount(config))
    {
        rate = GetFrequency(&config->frequency);
        if (UTL_MPEG4AUDIO_PRESENT == config->sbr)
        {
            rate = GetFrequency(&config->extensionFrequency);
        }
    }
    return rate;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
extern FORCE_INLINE uint32_t Utl_Mpeg4Audio_GetChannelCount(const Utl_Mpeg4Audio_Config_t* config)
{
    uint32_t channels;

    LOGASSERT(NULL != config);
    channels = 0;
    if (0 < Utl_Mpeg4Audio_GetConfigByteCount(config))
    {
        channels = GetCoreChannels(config);
        if (UTL_MPEG4AUDIO_PRESENT == config->ps)
        {
            channels = CHANNEL_PAIR;
        }
    }
    return channels;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Mpeg4Audio_GetConfigByteCount(const Utl_Mpeg4Audio_Config_t* config)
{
    bool_t                valid;
    uint32_t              count;
    Utl_Mpeg4_BitWriter_t writer;

    LOGASSERT(NULL != config);
    valid = IsConfiguration(config);
    count = 0;
    if (valid)
    {
        Utl_Mpeg4_InitializeBitCounter(&writer);
        WriteConfiguration(&writer, config);
        if (writer.valid)
        {
            writer.valid = IsContext(config);
        }
        if (writer.valid)
        {
            count = Utl_Mpeg4_GetBitWriterByteCount(&writer);
        }
    }
    return count;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
Utl_Mpeg4Audio_Status_t Utl_Mpeg4Audio_ReadConfig(Utl_Mpeg4Audio_Config_t* config, Utl_Mpeg4_Span_t bytes)
{
    bool_t                  has_storage;
    bool_t                  empty;
    bool_t                  valid_storage;
    Utl_Mpeg4_BitReader_t   reader;
    Utl_Mpeg4Audio_Config_t candidate;
    uint8_t                 leading_object;
    bool_t                  is_sbr;
    bool_t                  is_ps;
    bool_t                  has_extension;
    bool_t                  core_supported;
    Utl_Mpeg4Audio_Status_t status;
    bool_t                  complete;
    bool_t                  can_check;

    LOGASSERT(NULL != config);
    has_storage   = (NULL != bytes.data);
    empty         = (0 == bytes.byteCount);
    valid_storage = has_storage || empty;
    LOGASSERT(valid_storage);
    Utl_Mpeg4_InitializeBitReader(&reader, bytes);
    Utl_Mpeg4Audio_InitializeConfig(&candidate);
    leading_object       = ReadObject(&reader);
    candidate.objectType = leading_object;
    ReadFrequency(&candidate.frequency, &reader);
    candidate.channelConfiguration = (uint8_t)ReadBits(&reader, CHANNEL_BITS);
    is_sbr                         = (UTL_MPEG4AUDIO_SBR == leading_object);
    is_ps                          = (UTL_MPEG4AUDIO_PS == leading_object);
    has_extension                  = is_sbr || is_ps;
    if (has_extension)
    {
        candidate.signaling = UTL_MPEG4AUDIO_EXPLICIT;
        candidate.sbr       = UTL_MPEG4AUDIO_PRESENT;
        candidate.ps        = is_ps ? UTL_MPEG4AUDIO_PRESENT : UTL_MPEG4AUDIO_UNKNOWN;
        ReadFrequency(&candidate.extensionFrequency, &reader);
        candidate.objectType = ReadObject(&reader);
    }
    core_supported = IsCoreObject(candidate.objectType);
    status         = UTL_MPEG4AUDIO_UNSUPPORTED;
    if (core_supported)
    {
        status    = ReadGeneral(&candidate, &reader);
        complete  = (UTL_MPEG4AUDIO_COMPLETE == status);
        can_check = complete && reader.valid;
        if (can_check)
        {
            if (!has_extension)
            {
                status = ReadSyncExtension(&candidate, &reader);
            }
        }
    }
    complete  = (UTL_MPEG4AUDIO_COMPLETE == status);
    can_check = complete && reader.valid;
    if (can_check)
    {
        status = ReadPadding(&reader);
        if (0 == Utl_Mpeg4Audio_GetConfigByteCount(&candidate))
        {
            status = UTL_MPEG4AUDIO_INVALID;
        }
    }
    if (!reader.valid)
    {
        status = UTL_MPEG4AUDIO_INVALID;
    }
    if (UTL_MPEG4AUDIO_COMPLETE == status)
    {
        *config = candidate;
    }
    return status;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
uint32_t Utl_Mpeg4Audio_WriteConfig(const Utl_Mpeg4Audio_Config_t* config, byte_t* output, uint32_t capacity)
{
    uint32_t              count;
    bool_t                valid;
    bool_t                fits;
    bool_t                can_write;
    uint32_t              written;
    Utl_Mpeg4_BitWriter_t writer;

    LOGASSERT(NULL != config);
    LOGASSERT(NULL != output);
    count     = Utl_Mpeg4Audio_GetConfigByteCount(config);
    valid     = (0 < count);
    fits      = (count <= capacity);
    can_write = valid && fits;
    written   = 0;
    if (can_write)
    {
        Utl_Mpeg4_InitializeBitWriter(&writer, output, capacity);
        WriteConfiguration(&writer, config);
        LOGASSERT(writer.valid);
        LOGASSERT(count == (writer.position + BYTE_BITS - 1) / BYTE_BITS);
        written = count;
    }
    return written;
}
