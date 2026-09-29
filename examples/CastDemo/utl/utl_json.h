#ifndef UTL_JSON_H
#define UTL_JSON_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * JSON grammar and generation: RFC 8259, sections 2 through 10.
 * https://www.rfc-editor.org/rfc/rfc8259.html
 * This writer emits compact UTF-8 JSON with caller-owned output and nesting frames.
 * The iterative document reader retains raw lexemes and decodes strings without allocation.
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

#define UTL_JSON_NO_VALUE            (UINT32_MAX) ///< Absent value link.
#define UTL_JSON_MAX_FRACTION_DIGITS (19)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Counted borrowed UTF-8 text or ASCII number lexeme.
 */
typedef struct Utl_Json_Text_s
{
    const byte_t* data;      ///< Readable bytes; NULL is allowed only for empty text.
    uint32_t      byteCount; ///< Exact extent, without an implicit terminator.
} Utl_Json_Text_t;

/*************************************************************************************************/
/**
 * \brief Nonnegative fixed-point decimal without binary floating-point rounding.
 *
 * \par Details:
 *  The value is coefficient divided by ten to the power of fractionDigits.
 *  This representation deliberately covers a subset of JSON numbers; WriteNumber
 *  accepts the complete lexical grammar, including exponents and negative values.
 */
typedef struct Utl_Json_Decimal_s
{
    uint64_t coefficient;    ///< Exact unsigned decimal coefficient.
    uint32_t fractionDigits; ///< Decimal places, zero through UTL_JSON_MAX_FRACTION_DIGITS.
} Utl_Json_Decimal_t;

/*************************************************************************************************/
/**
 * \brief Structured value kinds supported by JSON.
 */
typedef enum Utl_Json_Container_e
{
    UTL_JSON_OBJECT,
    UTL_JSON_ARRAY
} Utl_Json_Container_t;

/*************************************************************************************************/
/**
 * \brief Writer progress or terminal failure, retained until reinitialization.
 */
typedef enum Utl_Json_Status_e
{
    UTL_JSON_READY,
    UTL_JSON_COMPLETE,
    UTL_JSON_INVALID_STATE,
    UTL_JSON_INVALID_TEXT,
    UTL_JSON_INVALID_NUMBER,
    UTL_JSON_OUTPUT_EXHAUSTED,
    UTL_JSON_DEPTH_EXHAUSTED
} Utl_Json_Status_t;

/*************************************************************************************************/
/**
 * \brief One caller-owned nesting record; fields are private to the writer.
 */
typedef struct Utl_Json_Frame_s
{
    Utl_Json_Container_t kind;       ///< Matching end delimiter and member grammar.
    bool_t               hasValue;   ///< A prior element/member requires a comma.
    bool_t               needsValue; ///< An object member name has been emitted without its value.
} Utl_Json_Frame_t;

/*************************************************************************************************/
/**
 * \brief Bounded writer or counting pass; no allocation or recursion is used.
 */
typedef struct Utl_Json_Writer_s
{
    Utl_Json_Frame_t* frames;        ///< Borrowed nesting storage through the last operation.
    uint32_t          frameCapacity; ///< Number of writable nesting records.
    uint32_t          depth;         ///< Number of active records.
    byte_t*           buffer;        ///< Output, or NULL for an explicit counting pass.
    uint32_t          capacity;      ///< Maximum output/counting extent.
    uint32_t          used;          ///< Published bytes, excluding any implicit terminator.
    Utl_Json_Status_t status;        ///< Progress or latched failure.
    bool_t            rootWritten;   ///< The single root value has begun.
} Utl_Json_Writer_t;

/*************************************************************************************************/
/**
 * \brief Complete JSON value alternatives; strings and numbers retain their wire lexemes.
 */
typedef enum Utl_Json_ValueKind_e
{
    UTL_JSON_VALUE_NULL,
    UTL_JSON_VALUE_FALSE,
    UTL_JSON_VALUE_TRUE,
    UTL_JSON_VALUE_NUMBER,
    UTL_JSON_VALUE_STRING,
    UTL_JSON_VALUE_ARRAY,
    UTL_JSON_VALUE_OBJECT
} Utl_Json_ValueKind_t;

/*************************************************************************************************/
/**
 * \brief Result of reading one complete counted JSON text.
 */
typedef enum Utl_Json_ReadStatus_e
{
    UTL_JSON_READ_READY,
    UTL_JSON_READ_COMPLETE,
    UTL_JSON_READ_INVALID,
    UTL_JSON_READ_EXHAUSTED
} Utl_Json_ReadStatus_t;

/*************************************************************************************************/
/**
 * \brief One caller-owned value record with immutable source borrows after successful parsing.
 *
 * \par Details:
 *  Text and name exclude surrounding quotes and retain escape spelling. Container
 *  text is empty. Names are present only on object members. Parent/child/sibling
 *  links use value indices or UTL_JSON_NO_VALUE. Callers shall not mutate records.
 */
typedef struct Utl_Json_Value_s
{
    Utl_Json_ValueKind_t kind;        ///< Grammar alternative.
    Utl_Json_Text_t      text;        ///< Raw string body, number or literal lexeme.
    Utl_Json_Text_t      name;        ///< Raw member-name body, or an absent span.
    uint32_t             childCount;  ///< Direct children in source order, including duplicate member names.
    uint32_t             firstChild;  ///< First direct child, or NO_VALUE.
    uint32_t             nextSibling; ///< Next child of the same parent, or NO_VALUE.
    uint32_t             parent;      ///< Containing value, or NO_VALUE for the root.
    uint32_t             lastChild;   ///< Parser-owned tail; avoids repeated child-list traversal.
} Utl_Json_Value_t;

/*************************************************************************************************/
/**
 * \brief Reusable document reader bound to startup-owned record storage.
 *
 * \par Details:
 *  Input and records are borrowed through the last query. Starting another read
 *  invalidates all previous value borrows. Failed reads publish no document;
 *  record contents after failure are scratch, never queryable values.
 */
typedef struct Utl_Json_Reader_s
{
    Utl_Json_Value_t*     values;     ///< Required exclusive record storage.
    uint32_t              capacity;   ///< Positive record capacity, fixed by InitializeReader.
    uint32_t              valueCount; ///< Published count; zero before success and after failure.
    uint32_t              offset;     ///< Bytes consumed before completion or rejection.
    Utl_Json_ReadStatus_t status;     ///< Last read result; initialization sets READY.
} Utl_Json_Reader_t;

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
 * \brief Initializes explicit output or counting mode with caller-owned storage.
 *
 * \par Details:
 *  A fresh writer is ready.
 *
 * \par Requirements:
 *  - 001 Required writer and nonempty frame storage shall assert when absent.
 *  - 002 NULL output shall explicitly select counting mode; capacity shall still bound the result.
 *  - 003 Storage shall remain disjoint from inputs and live throughout writing; no output byte shall change here.
 *  - 004 All subsequent operations shall allocate nothing and perform no CRT or OS calls.
 *
 * \param[out]    writer          Required writer storage
 * \param[in,out] frames          Nesting records; NULL only when frameCapacity is zero
 * \param[in]     frameCapacity   Available records
 * \param[out]    buffer          Writable bytes, or NULL for counting
 * \param[in]     capacity        Output/counting limit
 *
 * \retval None
 */
void Utl_Json_InitializeWriter(Utl_Json_Writer_t* writer,
                               Utl_Json_Frame_t*  frames,
                               uint32_t           frameCapacity,
                               byte_t*            buffer,
                               uint32_t           capacity);

/*************************************************************************************************/
/**
 * \brief Binds reusable caller-owned records before document reads.
 *
 * \par Details:
 *  An empty READY reader is established.
 *
 * \par Requirements:
 *  - 001 Missing storage or zero capacity shall assert as developer misuse.
 *  - 002 No record contents, input, allocation, native resources or global state shall be accessed.
 *  - 003 The caller shall exclude all concurrent access and retain both objects through the last query.
 *
 * \param[out] reader     Required lifetime, disjoint from records
 * \param[out] values     Exclusive storage retained through all document borrows
 * \param[in]  capacity   Positive writable record count
 *
 * \retval None
 */
void Utl_Json_InitializeReader(Utl_Json_Reader_t* reader, Utl_Json_Value_t* values, uint32_t capacity);

/*************************************************************************************************/
/**
 * \brief Gets the already published byte count.
 *
 * \par Requirements:
 *  - 001 The result shall remain available after failure; it does not imply a complete JSON text.
 *
 * \param[in] writer   Initialized writer
 *
 * \retval bytes   Published bytes with no implicit terminator.
 */
uint32_t Utl_Json_GetByteCount(const Utl_Json_Writer_t* writer);

/*************************************************************************************************/
/**
 * \brief Gets exact progress or the first latched failure.
 *
 * \par Requirements:
 *  - 001 Failures shall remain latched until reinitialization.
 *
 * \param[in] writer   Initialized writer
 *
 * \retval status   Current state.
 */
Utl_Json_Status_t Utl_Json_GetStatus(const Utl_Json_Writer_t* writer);

/*************************************************************************************************/
/**
 * \brief Derives the JSON string extent including both quotes.
 *
 * \par Requirements:
 *  - 001 All input shall be shortest-form UTF-8 with scalar values; embedded U+0000 shall be escaped.
 *  - 002 Invalid input or uint32_t count overflow shall yield zero; caller text shall remain unchanged.
 *
 * \param[in] text   Counted UTF-8 string
 *
 * \retval 2..UINT32_MAX   Complete encoded extent.
 * \retval 0               Invalid UTF-8/storage or count overflow.
 */
uint32_t Utl_Json_GetStringByteCount(Utl_Json_Text_t text);

/*************************************************************************************************/
/**
 * \brief Queries whether exactly one complete root value was emitted.
 *
 * \par Requirements:
 *  - 001 Failure or any unclosed container shall yield FALSE.
 *
 * \param[in] writer   Initialized writer
 *
 * \retval TRUE    Complete JSON text.
 * \retval FALSE   Incomplete or failed writer.
 */
bool_t Utl_Json_IsComplete(const Utl_Json_Writer_t* writer);

/*************************************************************************************************/
/**
 * \brief Checks a complete ASCII JSON number without numeric conversion.
 *
 * \par Requirements:
 *  - 001 Signs, integer, fraction and exponent shall follow RFC 8259 section 6 exactly.
 *  - 002 Leading plus, redundant integer zeroes, missing digits, whitespace, NaN and Infinity shall fail.
 *  - 003 Magnitude and exponent shall not be restricted to a machine floating-point domain.
 *
 * \param[in] text   Complete number lexeme
 *
 * \retval TRUE    Complete JSON number grammar.
 * \retval FALSE   Invalid or empty syntax.
 */
bool_t Utl_Json_IsNumber(Utl_Json_Text_t text);

/*************************************************************************************************/
/**
 * \brief Gets the last document-read result.
 *
 * \par Requirements:
 *  - 001 A missing required reader shall assert.
 *  - 002 The last read result shall remain observable without modifying the reader.
 *
 * \param[in] reader   Required initialized reader
 *
 * \retval status   READY, COMPLETE, INVALID or EXHAUSTED.
 */
Utl_Json_ReadStatus_t Utl_Json_GetReadStatus(const Utl_Json_Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Gets the consumed extent at completion or rejection.
 *
 * \par Requirements:
 *  - 001 A missing required reader shall assert.
 *  - 002 The result shall be zero before reading and no greater than the latest input extent afterward.
 *
 * \param[in] reader   Required initialized reader
 *
 * \retval bytes   Offset within the last supplied source; zero before any read.
 */
uint32_t Utl_Json_GetReadByteCount(const Utl_Json_Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Borrows the root only after complete document validation.
 *
 * \par Requirements:
 *  - 001 A missing required reader shall assert.
 *  - 002 Only a complete document shall publish a borrowed root; initialization and rejected reads shall yield NULL.
 *
 * \param[in] reader   Required initialized reader
 *
 * \retval value   Valid root until the next read.
 * \retval NULL    No complete document.
 */
const Utl_Json_Value_t* Utl_Json_GetRoot(const Utl_Json_Reader_t* reader);

/*************************************************************************************************/
/**
 * \brief Borrows one array element in source order.
 *
 * \par Requirements:
 *  - 001 A missing reader shall assert; a non-NULL array shall be an unchanged value borrowed from this reader.
 *  - 002 An absent array, wrong value kind, incomplete document or out-of-range index shall yield NULL.
 *  - 003 The returned borrow shall expire at the next read or owner release.
 *
 * \param[in] reader   Required complete-document owner
 * \param[in] array    Borrowed value, or NULL
 * \param[in] index    Zero-based direct element index
 *
 * \retval value   Matching element.
 * \retval NULL    No complete document, wrong kind or absent element.
 */
const Utl_Json_Value_t* Utl_Json_GetElement(const Utl_Json_Reader_t* reader,
                                            const Utl_Json_Value_t*  array,
                                            uint32_t                 index);

/*************************************************************************************************/
/**
 * \brief Derives an exact signed integer from a parsed JSON number.
 *
 * \par Requirements:
 *  - 001 Missing required output storage shall assert; a non-NULL value shall be a live unchanged parsed record.
 *  - 002 Absent values, other kinds, nonintegral values and values outside int64_t shall fail without changing output.
 *  - 003 Decimal fractions and exponents shall be interpreted exactly, without truncation, saturation or binary rounding.
 *  - 004 Negative zero shall yield zero; large exponents on zero shall not cause overflow.
 *  - 005 Work shall be linear in the source extent plus at most nineteen decimal scaling steps.
 *  - 006 No allocation, recursion, CRT or native operation shall occur.
 *
 * \param[out] result   Required output, disjoint from the value and its borrowed source
 * \param[in]  value    Parsed JSON value, or NULL for an absent optional member
 *
 * \retval     TRUE    Exact integer published.
 * \retval     FALSE   Absent, wrong-kind, fractional or out-of-range value; output unchanged.
 */
bool_t Utl_Json_GetInt64(int64_t* result, const Utl_Json_Value_t* value);

/*************************************************************************************************/
/**
 * \brief Measures a raw JSON string body as decoded UTF-8.
 *
 * \par Requirements:
 *  - 001 Required output and nonempty input storage shall assert when absent.
 *  - 002 Unpaired escaped UTF-16 shall be rejected for UTF-8 conversion, without changing the parsed raw document.
 *  - 003 No terminator, allocation, recursion, CRT or OS operation shall occur.
 *
 * \param[out] byteCount   Required disjoint output, unchanged on failure
 * \param[in]  text        Counted body without surrounding quotes
 *
 * \retval TRUE    Exact decoded byte count published.
 * \retval FALSE   Malformed escape, raw UTF-8 or unpaired surrogate.
 */
bool_t Utl_Json_GetDecodedByteCount(uint32_t* byteCount, Utl_Json_Text_t text);

/*************************************************************************************************/
/**
 * \brief Compares a borrowed string with counted decoded UTF-8.
 *
 * \par Requirements:
 *  - 001 Missing nonempty expected storage shall assert; a non-NULL value shall be a live unchanged parsed record.
 *  - 002 Only string values with identical decoded Unicode scalars shall compare equal.
 *  - 003 No allocation, native call, normalization or case folding shall occur.
 *
 * \param[in] value      Borrowed complete-document value, or NULL
 * \param[in] expected   Decoded UTF-8 text
 *
 * \retval TRUE    Equal scalar sequences.
 * \retval FALSE   Different, absent, wrong-kind or invalid-Unicode value.
 */
bool_t Utl_Json_IsStringEqual(const Utl_Json_Value_t* value, Utl_Json_Text_t expected);

/*************************************************************************************************/
/**
 * \brief Finds the first object member with an equal decoded name.
 *
 * \par Requirements:
 *  - 001 A missing reader or nonempty name storage shall assert.
 *  - 002 A non-NULL object shall be an unchanged value borrowed from this reader.
 *  - 003 Lookup shall compare decoded names and return the first matching direct member in source order.
 *  - 004 Incomplete documents, absent objects, wrong value kinds and absent names shall yield NULL.
 *
 * \param[in] reader   Required initialized owner
 * \param[in] object   Borrowed object from this reader, or NULL
 * \param[in] name     Counted decoded UTF-8 member name
 *
 * \retval value   First matching member.
 * \retval NULL    Incomplete document, absent object, wrong kind or no match.
 */
const Utl_Json_Value_t* Utl_Json_FindMember(const Utl_Json_Reader_t* reader,
                                            const Utl_Json_Value_t*  object,
                                            Utl_Json_Text_t          name);

/*************************************************************************************************/
/**
 * \brief Reads one complete JSON text into bound records without recursive calls.
 *
 * \par Requirements:
 *  - 001 Missing required storage shall assert; malformed external JSON shall return a normal error.
 *  - 002 Every JSON value kind, exact number grammar, name/value separators and string escape form shall be supported.
 *  - 003 Raw UTF-8 shall be scalar-valid; escaped unpaired UTF-16 code units shall remain representable as raw spans.
 *  - 004 No partial root shall be queryable after failure; scratch records may have changed.
 *  - 005 Input work and record use shall be bounded by the supplied byte extent and fixed capacity.
 *  - 006 No allocation, recursion, CRT, native operation or shared global state shall occur.
 *
 * \param[in,out] reader   Initialized exclusive reader; all previous borrows expire
 * \param[in]     text     Stable counted UTF-8 bytes, disjoint from reader and records
 *
 * \retval UTL_JSON_READ_COMPLETE    One complete root with optional surrounding JSON whitespace.
 * \retval UTL_JSON_READ_INVALID     Malformed or truncated syntax, including trailing data.
 * \retval UTL_JSON_READ_EXHAUSTED   Fixed record capacity was reached.
 */
Utl_Json_ReadStatus_t Utl_Json_ReadDocument(Utl_Json_Reader_t* reader, Utl_Json_Text_t text);

/*************************************************************************************************/
/**
 * \brief Decodes a raw JSON string body to counted UTF-8 transactionally.
 *
 * \par Requirements:
 *  - 001 The caller shall retain immutable input and exclusive disjoint output for both passes.
 *  - 002 Missing required pointers shall assert; external syntax and capacity failures shall preserve outputs.
 *  - 003 No allocation, recursion, CRT or OS operation shall occur.
 *
 * \param[out] byteCount   Required disjoint count, unchanged on rejection
 * \param[out] output      Required writable bytes, disjoint from text and count
 * \param[in]  capacity    Writable output capacity
 * \param[in]  text        Raw JSON string body without quotes
 *
 * \retval TRUE    Exact decoded bytes and count published.
 * \retval FALSE   Invalid Unicode/escape or insufficient capacity; outputs unchanged.
 */
bool_t Utl_Json_ReadString(uint32_t* byteCount, byte_t* output, uint32_t capacity, Utl_Json_Text_t text);

/*************************************************************************************************/
/**
 * \brief Writes an object member name and its separator.
 *
 * \par Requirements:
 *  - 001 Required writer storage shall assert when absent.
 *  - 002 An error shall latch without changing this operation's output bytes or nesting state; subsequent writes shall fail unchanged.
 *  - 003 No terminator, allocation, recursion, CRT or OS call shall be introduced.
 *  - 004 The current container shall be an object without a pending member value; empty names and duplicate names are permitted by the grammar.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     name     Immutable UTF-8 member name
 *
 * \retval TRUE    Complete name and colon emitted.
 * \retval FALSE   Latched sequence, text or capacity error.
 */
bool_t Utl_Json_WriteName(Utl_Json_Writer_t* writer, Utl_Json_Text_t name);

/*************************************************************************************************/
/**
 * \brief Writes one validated UTF-8 string value.
 *
 * \par Requirements:
 *  - 001 Required writer storage shall assert when absent.
 *  - 002 An error shall latch without changing this operation's output bytes or nesting state; subsequent writes shall fail unchanged.
 *  - 003 No terminator, allocation, recursion, CRT or OS call shall be introduced.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     text     Immutable UTF-8 value
 *
 * \retval TRUE    Complete value emitted.
 * \retval FALSE   Latched grammar, text or capacity error.
 */
bool_t Utl_Json_WriteString(Utl_Json_Writer_t* writer, Utl_Json_Text_t text);

/*************************************************************************************************/
/**
 * \brief Writes an exact JSON number lexeme.
 *
 * \par Requirements:
 *  - 001 Required writer storage shall assert when absent.
 *  - 002 An error shall latch without changing this operation's output bytes or nesting state; subsequent writes shall fail unchanged.
 *  - 003 No terminator, allocation, recursion, CRT or OS call shall be introduced.
 *  - 004 The complete lexeme shall pass IsNumber; the writer shall preserve its spelling and precision.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     number   Immutable ASCII lexeme
 *
 * \retval TRUE    Complete numeric value emitted.
 * \retval FALSE   Latched grammar, number or capacity error.
 */
bool_t Utl_Json_WriteNumber(Utl_Json_Writer_t* writer, Utl_Json_Text_t number);

/*************************************************************************************************/
/**
 * \brief Writes an exact unsigned integer value.
 *
 * \par Requirements:
 *  - 001 Required writer storage shall assert when absent.
 *  - 002 An error shall latch without changing this operation's output bytes or nesting state; subsequent writes shall fail unchanged.
 *  - 003 No terminator, allocation, recursion, CRT or OS call shall be introduced.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     value    Full uint64_t numeric domain
 *
 * \retval TRUE    Complete value emitted.
 * \retval FALSE   Latched writer failure.
 */
bool_t Utl_Json_WriteUint64(Utl_Json_Writer_t* writer, uint64_t value);

/*************************************************************************************************/
/**
 * \brief Writes an exact signed integer value.
 *
 * \par Requirements:
 *  - 001 Missing required writer storage shall assert.
 *  - 002 Every int64_t value, including INT64_MIN, shall be represented exactly.
 *  - 003 Failure shall latch without changing this operation's output or nesting state.
 *  - 004 No terminator, allocation, CRT or OS operation shall occur.
 *
 * \param[in,out] writer   Initialized exclusive writer
 * \param[in]     value    Full signed 64-bit domain
 *
 * \retval        TRUE    Complete integer emitted.
 * \retval        FALSE   Latched grammar or capacity failure.
 */
bool_t Utl_Json_WriteInt64(Utl_Json_Writer_t* writer, int64_t value);

/*************************************************************************************************/
/**
 * \brief Writes an unsigned scaled integer with an exact decimal fraction.
 *
 * \par Requirements:
 *  - 001 Required writer storage shall assert when absent.
 *  - 002 An error shall latch without changing this operation's output bytes or nesting state; subsequent writes shall fail unchanged.
 *  - 003 No terminator, allocation, recursion, CRT or OS call shall be introduced.
 *  - 004 fractionDigits shall assert unless it is at most UTL_JSON_MAX_FRACTION_DIGITS.
 *  - 005 The value shall be divided by ten to that power; trailing fractional zeroes shall be preserved.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     value    Exact fixed-point coefficient and decimal scale
 *
 * \retval TRUE    Exact fixed-point value emitted.
 * \retval FALSE   Latched writer failure.
 */
bool_t Utl_Json_WriteDecimal(Utl_Json_Writer_t* writer, Utl_Json_Decimal_t value);

/*************************************************************************************************/
/**
 * \brief Writes the JSON true or false literal.
 *
 * \par Requirements:
 *  - 001 Required writer storage shall assert when absent.
 *  - 002 An error shall latch without changing this operation's output bytes or nesting state; subsequent writes shall fail unchanged.
 *  - 003 No terminator, allocation, recursion, CRT or OS call shall be introduced.
 *
 * \param[in,out] writer   Initialized writer
 * \param[in]     value    Boolean value
 *
 * \retval TRUE    Complete literal emitted.
 * \retval FALSE   Latched writer failure.
 */
bool_t Utl_Json_WriteBoolean(Utl_Json_Writer_t* writer, bool_t value);

/*************************************************************************************************/
/**
 * \brief Writes the JSON null literal.
 *
 * \par Requirements:
 *  - 001 Required writer storage shall assert when absent.
 *  - 002 An error shall latch without changing this operation's output bytes or nesting state; subsequent writes shall fail unchanged.
 *  - 003 No terminator, allocation, recursion, CRT or OS call shall be introduced.
 *
 * \param[in,out] writer   Initialized writer
 *
 * \retval TRUE    Complete literal emitted.
 * \retval FALSE   Latched writer failure.
 */
bool_t Utl_Json_WriteNull(Utl_Json_Writer_t* writer);

/*************************************************************************************************/
/**
 * \brief Begins a JSON object at the current position.
 *
 * \par Requirements:
 *  - 001 Required writer storage shall assert when absent.
 *  - 002 An error shall latch without changing this operation's output bytes or nesting state; subsequent writes shall fail unchanged.
 *  - 003 No terminator, allocation, recursion, CRT or OS call shall be introduced.
 *  - 004 The matching grammar state and nesting capacity shall be validated before any delimiter is emitted.
 *
 * \param[in,out] writer   Initialized writer
 *
 * \retval TRUE    Complete delimiter and state transition.
 * \retval FALSE   Latched writer failure.
 */
bool_t Utl_Json_BeginObject(Utl_Json_Writer_t* writer);

/*************************************************************************************************/
/**
 * \brief Ends a JSON object at the current position.
 *
 * \par Requirements:
 *  - 001 Required writer storage shall assert when absent.
 *  - 002 An error shall latch without changing this operation's output bytes or nesting state; subsequent writes shall fail unchanged.
 *  - 003 No terminator, allocation, recursion, CRT or OS call shall be introduced.
 *  - 004 The matching grammar state and nesting capacity shall be validated before any delimiter is emitted.
 *
 * \param[in,out] writer   Initialized writer
 *
 * \retval TRUE    Complete delimiter and state transition.
 * \retval FALSE   Latched writer failure.
 */
bool_t Utl_Json_EndObject(Utl_Json_Writer_t* writer);

/*************************************************************************************************/
/**
 * \brief Begins a JSON array at the current position.
 *
 * \par Requirements:
 *  - 001 Required writer storage shall assert when absent.
 *  - 002 An error shall latch without changing this operation's output bytes or nesting state; subsequent writes shall fail unchanged.
 *  - 003 No terminator, allocation, recursion, CRT or OS call shall be introduced.
 *  - 004 The matching grammar state and nesting capacity shall be validated before any delimiter is emitted.
 *
 * \param[in,out] writer   Initialized writer
 *
 * \retval TRUE    Complete delimiter and state transition.
 * \retval FALSE   Latched writer failure.
 */
bool_t Utl_Json_BeginArray(Utl_Json_Writer_t* writer);

/*************************************************************************************************/
/**
 * \brief Ends a JSON array at the current position.
 *
 * \par Requirements:
 *  - 001 Required writer storage shall assert when absent.
 *  - 002 An error shall latch without changing this operation's output bytes or nesting state; subsequent writes shall fail unchanged.
 *  - 003 No terminator, allocation, recursion, CRT or OS call shall be introduced.
 *  - 004 The matching grammar state and nesting capacity shall be validated before any delimiter is emitted.
 *
 * \param[in,out] writer   Initialized writer
 *
 * \retval TRUE    Complete delimiter and state transition.
 * \retval FALSE   Latched writer failure.
 */
bool_t Utl_Json_EndArray(Utl_Json_Writer_t* writer);

#endif /* UTL_JSON_H */
