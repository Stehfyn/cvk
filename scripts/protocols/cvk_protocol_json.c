/* Tier 3 protocol (SPEC.md section 8c): a JSON reader with no allocation, an arena, and the
** lookups the generated parsers (cvkParseJson*) use. Source of truth kept with the generator
** and folded into the generated cvk_json.h/cvk_json.c. Composed from the generated surface only. */

static CVK_FORCE_INLINE VkBool32 cvkIsJsonTextSpace(char value)
{
    return (' ' == value) || ('\t' == value) || ('\n' == value) || ('\r' == value) || ('\v' == value) || ('\f' == value);
}

/**
 * \brief Advances over JSON whitespace.
 * \param[in,out] pReader Input cursor and bounds.
 * \retval None No value is returned.
 */
static CVK_FORCE_INLINE void cvkSkipJsonSpace(cvkJsonReader* pReader)
{
    char character;
    VkBool32 is_space;
    for (; pReader->pCursor < pReader->pEnd; ++pReader->pCursor)
    {
        character = *pReader->pCursor;
        is_space  = (CVK_JSON_SPACE == character) || (CVK_JSON_TAB == character);
        is_space  = is_space || (CVK_JSON_LF == character) || (CVK_JSON_CR == character);
        if (!is_space)
        {
            break;
        }
    }
}

/**
 * \brief Consumes at least one decimal digit.
 * \param[in,out] pReader Input cursor and bounds.
 * \retval None No value is returned.
 */
static CVK_FORCE_INLINE void cvkReadJsonDigits(cvkJsonReader* pReader)
{
    const char* first;
    char character;
    VkBool32 is_digit;
    first = pReader->pCursor;
    for (; pReader->pCursor < pReader->pEnd; ++pReader->pCursor)
    {
        character = *pReader->pCursor;
        is_digit  = (CVK_JSON_ZERO <= character) && (character <= CVK_JSON_NINE);
        if (!is_digit)
        {
            break;
        }
    }
    CVK_ASSERT(first < pReader->pCursor);
}

/**
 * \brief Consumes a JSON number according to its integer, fraction and exponent grammar.
 * \param[in,out] pReader Input cursor and bounds.
 * \retval None No value is returned.
 */
static CVK_FORCE_INLINE void cvkReadJsonNumber(cvkJsonReader* pReader)
{
    char character;
    VkBool32 has_input;
    VkBool32 is_exponent;
    if (CVK_JSON_MINUS == *pReader->pCursor)
    {
        ++pReader->pCursor;
    }
    CVK_ASSERT(pReader->pCursor < pReader->pEnd);
    if (CVK_JSON_ZERO == *pReader->pCursor)
    {
        ++pReader->pCursor;
    }
    else
    {
        cvkReadJsonDigits(pReader);
    }
    has_input = pReader->pCursor < pReader->pEnd;
    if (has_input && (CVK_JSON_DOT == *pReader->pCursor))
    {
        ++pReader->pCursor;
        cvkReadJsonDigits(pReader);
    }
    has_input = pReader->pCursor < pReader->pEnd;
    if (has_input)
    {
        character   = *pReader->pCursor;
        is_exponent = (CVK_JSON_EXPONENT_LOWER == character) || (CVK_JSON_EXPONENT_UPPER == character);
        if (is_exponent)
        {
            ++pReader->pCursor;
            CVK_ASSERT(pReader->pCursor < pReader->pEnd);
            character = *pReader->pCursor;
            if ((CVK_JSON_PLUS == character) || (CVK_JSON_MINUS == character))
            {
                ++pReader->pCursor;
            }
            cvkReadJsonDigits(pReader);
        }
    }
}

/**
 * \brief Reads a quoted span while preserving its escape spelling.
 * \param[in,out] pReader Input cursor at the opening quote.
 * \param[out] pLength Receives the length excluding quotes.
 * \retval Other The start of the quoted content.
 */
static CVK_FORCE_INLINE const char* cvkReadJsonQuoted(cvkJsonReader* pReader,
                                                     uint32_t* pLength)
{
    const char* first;
    char character;
    VkBool32 is_quote;
    CVK_ASSERT(pReader->pCursor < pReader->pEnd);
    CVK_ASSERT(CVK_JSON_QUOTE == *pReader->pCursor);
    ++pReader->pCursor;
    first = pReader->pCursor;
    for (;;)
    {
        CVK_ASSERT(pReader->pCursor < pReader->pEnd);
        character = *pReader->pCursor;
        is_quote  = CVK_JSON_QUOTE == character;
        if (is_quote)
        {
            break;
        }
        CVK_ASSERT(CVK_JSON_CONTROL_LIMIT <= (uint8_t)character);
        if (CVK_JSON_ESCAPE == character)
        {
            ++pReader->pCursor;
            CVK_ASSERT(pReader->pCursor < pReader->pEnd);
        }
        ++pReader->pCursor;
    }
    *pLength = (uint32_t)(pReader->pCursor - first);
    ++pReader->pCursor;
    return first;
}

/**
 * \brief Consumes a literal and records its kind.
 * \param[in,out] pReader Input cursor and bounds.
 * \param[out] pValue Value receiving the literal kind.
 * \retval None No value is returned.
 */
static CVK_FORCE_INLINE void cvkReadJsonLiteral(cvkJsonReader* pReader,
                                               cvkJsonValue* pValue)
{
    const char* literal;
    uint32_t length;
    switch (*pReader->pCursor)
    {
        case CVK_JSON_TRUE_FIRST:
            literal = CVK_JSON_TRUE_TEXT;
            length = sizeof(CVK_JSON_TRUE_TEXT) - 1;
            pValue->kind = CVK_JSON_TRUE;
            break;
        case CVK_JSON_FALSE_FIRST:
            literal = CVK_JSON_FALSE_TEXT;
            length = sizeof(CVK_JSON_FALSE_TEXT) - 1;
            pValue->kind = CVK_JSON_FALSE;
            break;
        default:
            literal = CVK_JSON_NULL_TEXT;
            length = sizeof(CVK_JSON_NULL_TEXT) - 1;
            pValue->kind = CVK_JSON_NULL;
            break;
    }
    CVK_ASSERT(length <= (uint64_t)(pReader->pEnd - pReader->pCursor));
    CVK_ASSERT(0 == memcmp(pReader->pCursor, literal, length));
    pReader->pCursor += length;
}

/**
 * \brief Appends one token; containers are opened without descending into them.
 * \param[in,out] pReader Input and caller-owned value storage.
 * \retval Other The new value index.
 */
static CVK_FORCE_INLINE uint32_t cvkReadJsonToken(cvkJsonReader* pReader)
{
    cvkJsonValue* value;
    uint32_t index;
    cvkSkipJsonSpace(pReader);
    CVK_ASSERT(pReader->pCursor < pReader->pEnd);
    CVK_ASSERT(pReader->valueCount < pReader->valueCapacity);
    index = pReader->valueCount;
    ++pReader->valueCount;
    value = &pReader->pValues[index];
    cvkZeroMemory(value, sizeof(*value));
    value->firstChild  = CVK_JSON_NONE;
    value->nextSibling = CVK_JSON_NONE;
    switch (*pReader->pCursor)
    {
        case CVK_JSON_OBJECT_OPEN:
            value->kind = CVK_JSON_OBJECT;
            ++pReader->pCursor;
            break;
        case CVK_JSON_ARRAY_OPEN:
            value->kind = CVK_JSON_ARRAY;
            ++pReader->pCursor;
            break;
        case CVK_JSON_QUOTE:
            value->kind = CVK_JSON_STRING;
            value->pText = cvkReadJsonQuoted(pReader, &value->textLength);
            break;
        case CVK_JSON_TRUE_FIRST:
        case CVK_JSON_FALSE_FIRST:
        case CVK_JSON_NULL_FIRST:
            cvkReadJsonLiteral(pReader, value);
            break;
        default:
            value->kind = CVK_JSON_NUMBER;
            value->pText = pReader->pCursor;
            cvkReadJsonNumber(pReader);
            value->textLength = (uint32_t)(pReader->pCursor - value->pText);
            break;
    }
    return index;
}

/**
 * \brief Appends a child and its object key, linking it to the active container.
 * \param[in,out] pReader Input and caller-owned value storage.
 * \param[in] parent Active container index.
 * \retval Other The new child index.
 */
static CVK_FORCE_INLINE uint32_t cvkReadJsonChild(cvkJsonReader* pReader,
                                                uint32_t parent)
{
    const char* key;
    uint32_t key_length;
    uint32_t child;
    uint32_t previous;
    key = NULL;
    key_length = 0;
    if (CVK_JSON_OBJECT == pReader->pValues[parent].kind)
    {
        key = cvkReadJsonQuoted(pReader, &key_length);
        cvkSkipJsonSpace(pReader);
        CVK_ASSERT(pReader->pCursor < pReader->pEnd);
        CVK_ASSERT(CVK_JSON_COLON == *pReader->pCursor);
        ++pReader->pCursor;
    }
    child = cvkReadJsonToken(pReader);
    pReader->pValues[child].pKey = key;
    pReader->pValues[child].keyLength = key_length;
    pReader->pValues[child].nextSibling = parent;
    previous = pReader->pValues[parent].firstChild;
    if (CVK_JSON_NONE != previous)
    {
        pReader->pValues[previous].nextSibling = child;
    }
    pReader->pValues[parent].firstChild = child;
    ++pReader->pValues[parent].childCount;
    return child;
}

/**
 * \brief Consumes a container separator and checks whether the next token closes it.
 * \param[in,out] pReader Input cursor and bounds.
 * \param[in] close Closing delimiter for the active container.
 * \param[in] has_children Whether a preceding value requires a separator.
 * \retval VK_TRUE A closing delimiter is next.
 * \retval VK_FALSE Another child starts at the cursor.
 */
static CVK_FORCE_INLINE VkBool32 cvkReadJsonSeparator(cvkJsonReader* pReader,
                                                      char close,
                                                      VkBool32 has_children)
{
    VkBool32 has_comma;
    VkBool32 is_close;
    VkBool32 can_start;

    cvkSkipJsonSpace(pReader);
    CVK_ASSERT(pReader->pCursor < pReader->pEnd);
    has_comma = CVK_JSON_COMMA == *pReader->pCursor;
    if (has_children && has_comma)
    {
        ++pReader->pCursor;
        cvkSkipJsonSpace(pReader);
        CVK_ASSERT(pReader->pCursor < pReader->pEnd);
        CVK_ASSERT(close != *pReader->pCursor);
    }
    is_close = close == *pReader->pCursor;
    can_start = !has_children || has_comma;
    CVK_ASSERT(is_close || can_start);
    return is_close;
}

/**
 * \brief Parses one JSON value into caller-owned storage using an iterative traversal.
 * \par Details:
 *  Open containers temporarily store their parent and last-child indices in their
 *  links. Closing a container restores the public first-child and sibling links.
 *  Neither depth-dependent stack storage nor allocation is required.
 * \param[in,out] pReader Reader state and caller-owned values.
 * \retval Other The parsed root index.
 */
uint32_t cvkParseJsonValue(cvkJsonReader* pReader)
{
    uint32_t root;
    uint32_t active;
    uint32_t parent;
    uint32_t child;
    cvkJsonValue* value;
    char close;
    VkBool32 is_container;
    VkBool32 has_children;
    VkBool32 is_close;
    root = cvkReadJsonToken(pReader);
    value = &pReader->pValues[root];
    is_container = (CVK_JSON_ARRAY == value->kind) || (CVK_JSON_OBJECT == value->kind);
    active = is_container ? root : CVK_JSON_NONE;
    for (; CVK_JSON_NONE != active;)
    {
        value = &pReader->pValues[active];
        close = (CVK_JSON_OBJECT == value->kind) ? CVK_JSON_OBJECT_CLOSE : CVK_JSON_ARRAY_CLOSE;
        has_children = 0 < value->childCount;
        is_close = cvkReadJsonSeparator(pReader, close, has_children);
        if (is_close)
        {
            ++pReader->pCursor;
            parent = value->nextSibling;
            value->firstChild = has_children ? active + 1 : CVK_JSON_NONE;
            value->nextSibling = CVK_JSON_NONE;
            active = parent;
        }
        else
        {
            child = cvkReadJsonChild(pReader, active);
            value = &pReader->pValues[child];
            is_container = (CVK_JSON_ARRAY == value->kind) || (CVK_JSON_OBJECT == value->kind);
            if (is_container)
            {
                active = child;
            }
            else
            {
                value->nextSibling = CVK_JSON_NONE;
            }
        }
    }
    return root;
}

/**
 * \brief Parses a JSON document into a value array; value zero is the root.
 *
 * \param[in]  pText          The document text; need not be NUL-terminated
 * \param[in]  length         Byte length of the document
 * \param[out] pValues        Receives the values, root first
 * \param[in]  valueCapacity  Capacity of pValues; exceeding it is fatal
 *
 * \retval Number of values written
 */
uint32_t cvkParseJsonDocument(const char* pText, size_t length, cvkJsonValue* pValues, uint32_t valueCapacity)
{
    cvkJsonReader reader;
    uint32_t      root;

    reader.pCursor       = pText;
    reader.pEnd          = pText + length;
    reader.pValues       = pValues;
    reader.valueCapacity = valueCapacity;
    reader.valueCount    = 0;
    root                 = cvkParseJsonValue(&reader);
    CVK_ASSERT(0 == root);
    cvkSkipJsonSpace(&reader);
    CVK_ASSERT(reader.pCursor == reader.pEnd);
    return reader.valueCount;
}

/**
 * \brief Returns the member of an object by name, or NULL when absent.
 *
 * \param[in]  pDocument  The document the object belongs to
 * \param[in]  pObject    The object, or NULL
 * \param[in]  pKey       Member name
 *
 * \retval The member value, or NULL
 */
const cvkJsonValue* cvkGetJsonMember(const cvkJsonDocument* pDocument, const cvkJsonValue* pObject, const char* pKey)
{
    const size_t length = strlen(pKey);
    uint32_t     index;

    if ((NULL == pObject) || (CVK_JSON_OBJECT != pObject->kind))
    {
        return NULL;
    }
    for (index = pObject->firstChild; CVK_JSON_NONE != index; index = pDocument->pValues[index].nextSibling)
    {
        const cvkJsonValue* member = &pDocument->pValues[index];

        if ((member->keyLength == length) && (0 == memcmp(member->pKey, pKey, length)))
        {
            return member;
        }
    }
    return NULL;
}

/**
 * \brief Returns the element of an array by index, or NULL when out of range.
 *
 * \param[in]  pDocument  The document the array belongs to
 * \param[in]  pArray     The array, or NULL
 * \param[in]  index      Element index
 *
 * \retval The element, or NULL
 */
const cvkJsonValue* cvkGetJsonElement(const cvkJsonDocument* pDocument, const cvkJsonValue* pArray, uint32_t index)
{
    uint32_t child;

    if ((NULL == pArray) || ((CVK_JSON_ARRAY != pArray->kind) && (CVK_JSON_OBJECT != pArray->kind)) || (index >= pArray->childCount))
    {
        return NULL;
    }
    for (child = pArray->firstChild; 0 != index; --index)
    {
        child = pDocument->pValues[child].nextSibling;
    }
    return &pDocument->pValues[child];
}

/**
 * \brief Reports whether a value is a string equal to a C string.
 *
 * \param[in]  pValue   The value, or NULL
 * \param[in]  pString  The string to compare with
 *
 * \retval VK_TRUE when equal
 */
VkBool32 cvkJsonStringEquals(const cvkJsonValue* pValue, const char* pString)
{
    const size_t length = strlen(pString);

    return ((NULL != pValue) && (CVK_JSON_STRING == pValue->kind) && (pValue->textLength == length) && (0 == memcmp(pValue->pText, pString, length))) ? VK_TRUE : VK_FALSE;
}

/**
 * \brief Reports whether a value stands for no value: absent, null, or the string "NULL" the pipeline JSON uses for null pointers.
 *
 * \param[in]  pValue  The value, or NULL
 *
 * \retval VK_TRUE when there is no value
 */
VkBool32 cvkIsJsonNull(const cvkJsonValue* pValue)
{
    return ((NULL == pValue) || (CVK_JSON_NULL == pValue->kind) || cvkJsonStringEquals(pValue, "NULL")) ? VK_TRUE : VK_FALSE;
}

/**
 * \brief Parses a decimal, octal, or hexadecimal unsigned integer, saturating on overflow.
 *
 * \param[in]  pText  Null-terminated integer text
 *
 * \retval The integer
 */
uint64_t cvkParseUint64Text(const char* pText)
{
    uint64_t value;
    uint32_t base;
    uint32_t digit;
    VkBool32 negative;

    value = 0;
    base = CVK_JSON_DECIMAL_BASE;
    negative = VK_FALSE;
    for (; cvkIsJsonTextSpace(*pText);)
    {
        ++pText;
    }
    if (('-' == *pText) || ('+' == *pText))
    {
        negative = ('-' == *pText) ? VK_TRUE : VK_FALSE;
        ++pText;
    }
    if ('0' == *pText)
    {
        base = CVK_JSON_OCTAL_BASE;
        ++pText;
        if (('x' == *pText) || ('X' == *pText))
        {
            base = CVK_JSON_HEXADECIMAL_BASE;
            ++pText;
        }
    }
    for (; '\0' != *pText;)
    {
        if (('0' <= *pText) && (*pText <= '9'))
        {
            digit = (uint32_t)(*pText - '0');
        }
        else if (('a' <= *pText) && (*pText <= 'f'))
        {
            digit = (uint32_t)(*pText - 'a') + CVK_JSON_DECIMAL_BASE;
        }
        else if (('A' <= *pText) && (*pText <= 'F'))
        {
            digit = (uint32_t)(*pText - 'A') + CVK_JSON_DECIMAL_BASE;
        }
        else
        {
            break;
        }
        if (digit >= base)
        {
            break;
        }
        if (value > (UINT64_MAX - digit) / base)
        {
            return UINT64_MAX;
        }
        value = (value * base) + digit;
        ++pText;
    }
    return negative ? (uint64_t)0 - value : value;
}

/**
 * \brief Reads a number as an unsigned 64-bit integer; a string holding a number is accepted.
 * \param[in] pValue The value, or NULL for zero
 * \return The integer
 */
uint64_t cvkGetJsonUint64(const cvkJsonValue* pValue)
{
    char buffer[CVK_JSON_INTEGER_TEXT_CAPACITY];

    if ((NULL == pValue) || ((CVK_JSON_NUMBER != pValue->kind) && (CVK_JSON_STRING != pValue->kind)) || (pValue->textLength >= sizeof(buffer)))
    {
        return 0;
    }
    cvkCopyMemory(buffer, pValue->pText, pValue->textLength);
    buffer[pValue->textLength] = '\0';
    return cvkParseUint64Text(buffer);
}

/**
 * \brief Reads a number as a signed 64-bit integer; a string holding a number is accepted.
 *
 * \param[in]  pValue  The value, or NULL for zero
 *
 * \retval The integer
 */
int64_t cvkGetJsonInt64(const cvkJsonValue* pValue)
{
    char buffer[CVK_JSON_INTEGER_TEXT_CAPACITY];
    const char* text;
    uint64_t magnitude;
    VkBool32 negative;

    if ((NULL == pValue) || ((CVK_JSON_NUMBER != pValue->kind) && (CVK_JSON_STRING != pValue->kind)) || (pValue->textLength >= sizeof(buffer)))
    {
        return 0;
    }
    cvkCopyMemory(buffer, pValue->pText, pValue->textLength);
    buffer[pValue->textLength] = '\0';
    text = buffer;
    for (; cvkIsJsonTextSpace(*text);)
    {
        ++text;
    }
    negative = ('-' == *text) ? VK_TRUE : VK_FALSE;
    if (('-' == *text) || ('+' == *text))
    {
        ++text;
    }
    if ((*text < '0') || (*text > '9'))
    {
        return 0;
    }
    magnitude = cvkParseUint64Text(text);
    if (negative)
    {
        return magnitude > (uint64_t)INT64_MAX ? INT64_MIN : -(int64_t)magnitude;
    }
    return magnitude > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)magnitude;
}

static CVK_FORCE_INLINE void cvkAppendJsonDoubleDigit(char digit, VkBool32 fraction, double* pNumber, uint32_t* pSignificant, int32_t* pExponent)
{
    enum
    {
        CVK_JSON_DOUBLE_SIGNIFICANT_DIGITS = 17
    };

    if (fraction && (*pSignificant < CVK_JSON_DOUBLE_SIGNIFICANT_DIGITS))
    {
        --*pExponent;
    }
    if ((0 == *pSignificant) && ('0' == digit))
    {
        return;
    }
    if (*pSignificant < CVK_JSON_DOUBLE_SIGNIFICANT_DIGITS)
    {
        *pNumber = (*pNumber * (double)CVK_JSON_DECIMAL_BASE) + ((double)digit - (double)'0');
        ++*pSignificant;
    }
    else if (!fraction)
    {
        ++*pExponent;
    }
}

static CVK_FORCE_INLINE const char* cvkParseJsonDoubleMantissa(const char* text, double* pNumber, int32_t* pExponent)
{
    uint32_t significant = 0;

    for (; ('0' <= *text) && (*text <= '9'); ++text)
    {
        cvkAppendJsonDoubleDigit(*text, VK_FALSE, pNumber, &significant, pExponent);
    }
    if ('.' == *text)
    {
        ++text;
        for (; ('0' <= *text) && (*text <= '9'); ++text)
        {
            cvkAppendJsonDoubleDigit(*text, VK_TRUE, pNumber, &significant, pExponent);
        }
    }
    return text;
}

static CVK_FORCE_INLINE int32_t cvkParseJsonDoubleExponent(const char* text)
{
    int32_t exponent = 0;
    int32_t sign = 1;
    enum
    {
        CVK_JSON_MAX_DECIMAL_EXPONENT = 400
    };

    if (('e' != *text) && ('E' != *text))
    {
        return 0;
    }
    ++text;
    if (('-' == *text) || ('+' == *text))
    {
        sign = ('-' == *text) ? -1 : 1;
        ++text;
    }
    for (; ('0' <= *text) && (*text <= '9'); ++text)
    {
        if (exponent < CVK_JSON_MAX_DECIMAL_EXPONENT)
        {
            exponent = (exponent * CVK_JSON_DECIMAL_BASE) + (*text - '0');
        }
    }
    return sign * exponent;
}

static CVK_FORCE_INLINE double cvkScaleJsonDouble(double value, int32_t exponent)
{
    int32_t index;
    enum
    {
        CVK_JSON_MAX_DECIMAL_EXPONENT = 400
    };

    if (0.0 == value)
    {
        return value;
    }
    if (exponent > CVK_JSON_MAX_DECIMAL_EXPONENT)
    {
        exponent = CVK_JSON_MAX_DECIMAL_EXPONENT;
    }
    if (exponent < -CVK_JSON_MAX_DECIMAL_EXPONENT)
    {
        exponent = -CVK_JSON_MAX_DECIMAL_EXPONENT;
    }
    if (0 < exponent)
    {
        for (index = 0; index < exponent; ++index)
        {
            value *= (double)CVK_JSON_DECIMAL_BASE;
        }
    }
    else
    {
        for (index = 0; index > exponent; --index)
        {
            value /= (double)CVK_JSON_DECIMAL_BASE;
        }
    }
    return value;
}

/**
 * \brief Reads a number as a double.
 *
 * \param[in]  pValue  The value, or NULL for zero
 *
 * \retval The number
 */
double cvkGetJsonDouble(const cvkJsonValue* pValue)
{
    char buffer[CVK_JSON_REAL_TEXT_CAPACITY];
    const char* text;
    double value;
    int32_t exponent;
    VkBool32 negative;

    if ((NULL == pValue) || ((CVK_JSON_NUMBER != pValue->kind) && (CVK_JSON_STRING != pValue->kind)) || (pValue->textLength >= sizeof(buffer)))
    {
        return 0.0;
    }
    cvkCopyMemory(buffer, pValue->pText, pValue->textLength);
    buffer[pValue->textLength] = '\0';
    text = buffer;
    for (; cvkIsJsonTextSpace(*text);)
    {
        ++text;
    }
    negative = ('-' == *text) ? VK_TRUE : VK_FALSE;
    if (('-' == *text) || ('+' == *text))
    {
        ++text;
    }
    value = 0.0;
    exponent = 0;
    text = cvkParseJsonDoubleMantissa(text, &value, &exponent);
    exponent += cvkParseJsonDoubleExponent(text);
    value = cvkScaleJsonDouble(value, exponent);
    return negative ? -value : value;
}

/**
 * \brief Reads a VkBool32: true, false, "VK_TRUE", "VK_FALSE", or a number.
 *
 * \param[in]  pValue  The value, or NULL for VK_FALSE
 *
 * \retval The boolean
 */
VkBool32 cvkGetJsonBool32(const cvkJsonValue* pValue)
{
    if (NULL == pValue)
    {
        return VK_FALSE;
    }
    if (CVK_JSON_TRUE == pValue->kind || cvkJsonStringEquals(pValue, "VK_TRUE"))
    {
        return VK_TRUE;
    }
    if (CVK_JSON_NUMBER == pValue->kind)
    {
        return (0 != cvkGetJsonInt64(pValue)) ? VK_TRUE : VK_FALSE;
    }
    return VK_FALSE;
}

/**
 * \brief Looks a name up in a name table.
 *
 * \param[in]  pTable      The table
 * \param[in]  tableCount  Number of entries
 * \param[in]  pName       Name to find; not NUL-terminated
 * \param[in]  nameLength  Length of the name
 * \param[out] pValue      Receives the entry's value when found
 *
 * \retval VK_TRUE when found
 */
VkBool32 cvkLookupJsonName(const cvkJsonName* pTable, uint32_t tableCount, const char* pName, uint32_t nameLength, uint64_t* pValue)
{
    uint32_t index;

    for (index = 0; index < tableCount; ++index)
    {
        if ((strlen(pTable[index].pName) == nameLength) && (0 == memcmp(pTable[index].pName, pName, nameLength)))
        {
            (*pValue) = pTable[index].value;
            return VK_TRUE;
        }
    }
    return VK_FALSE;
}

/**
 * \brief Reads an enumerant: a name from the table, or a number.
 *
 * \param[in]  pValue      The value; an unknown name is fatal
 * \param[in]  pTable      Name table of the enumeration
 * \param[in]  tableCount  Number of entries
 *
 * \retval The enumerant's value
 */
uint64_t cvkParseJsonEnumerant(const cvkJsonValue* pValue, const cvkJsonName* pTable, uint32_t tableCount)
{
    uint64_t found;

    if ((NULL == pValue) || (CVK_JSON_NULL == pValue->kind))
    {
        return 0;
    }
    if (CVK_JSON_NUMBER == pValue->kind)
    {
        return (uint64_t)cvkGetJsonInt64(pValue);
    }
    CVK_ASSERT(CVK_JSON_STRING == pValue->kind);
    if (cvkLookupJsonName(pTable, tableCount, pValue->pText, pValue->textLength, &found))
    {
        return found;
    }
    return (uint64_t)cvkGetJsonInt64(pValue); /* a number written as a string ("0") */
}

/**
 * \brief Reads a flag set: a number, "0", or bit names joined with "|".
 *
 * \param[in]  pValue      The value; an unknown bit name is fatal
 * \param[in]  pTable      Name table of the flag bits
 * \param[in]  tableCount  Number of entries
 *
 * \retval The flags
 */
uint64_t cvkParseJsonFlags(const cvkJsonValue* pValue, const cvkJsonName* pTable, uint32_t tableCount)
{
    uint64_t    flags = 0;
    const char* cursor;
    const char* end;

    if ((NULL == pValue) || (CVK_JSON_NULL == pValue->kind))
    {
        return 0;
    }
    if (CVK_JSON_NUMBER == pValue->kind)
    {
        return cvkGetJsonUint64(pValue);
    }
    CVK_ASSERT(CVK_JSON_STRING == pValue->kind);
    cursor   = pValue->pText;
    end = cursor + pValue->textLength;
    for (; cursor < end;)
    {
        const char* token;
        const char* token_end;
        uint64_t    bit;

        for (; (cursor < end) && ((' ' == *cursor) || ('|' == *cursor));)
        {
            ++cursor;
        }
        token = cursor;
        for (; (cursor < end) && (' ' != *cursor) && ('|' != *cursor);)
        {
            ++cursor;
        }
        token_end = cursor;
        if (token_end == token)
        {
            break;
        }
        if ((1 == token_end - token) && ('0' == *token))
        {
            continue;
        }
        if (cvkLookupJsonName(pTable, tableCount, token, (uint32_t)(token_end - token), &bit))
        {
            flags |= bit;
        }
        else
        {
            char buffer[CVK_JSON_INTEGER_TEXT_CAPACITY];

            CVK_ASSERT((size_t)(token_end - token) < sizeof(buffer));
            cvkCopyMemory(buffer, token, (size_t)(token_end - token));
            buffer[token_end - token] = '\0';
            flags |= cvkParseUint64Text(buffer); /* a numeric token */
        }
    }
    return flags;
}

typedef struct cvkArenaBlock
{
    struct cvkArenaBlock* pNext;
    uint8_t               storage[1];
} cvkArenaBlock;

typedef enum cvkArenaBlockCapacity
{
    CVK_JSON_ARENA_BLOCK_SIZE = 4096
} cvkArenaBlockCapacity;

static CVK_FORCE_INLINE void cvkGrowArena(cvkArena* pArena, size_t size, size_t alignment)
{
    size_t         capacity;
    cvkArenaBlock* block;

    CVK_ASSERT(NULL != pArena->pAllocator);
    CVK_ASSERT(NULL != pArena->pAllocator->pfnAllocation);
    CVK_ASSERT(NULL != pArena->pAllocator->pfnFree);
    capacity = (size > CVK_JSON_ARENA_BLOCK_SIZE) ? size : CVK_JSON_ARENA_BLOCK_SIZE;
    CVK_ASSERT(alignment <= SIZE_MAX - sizeof(cvkArenaBlock));
    CVK_ASSERT(capacity <= SIZE_MAX - alignment - sizeof(cvkArenaBlock));
    block = (cvkArenaBlock*)pArena->pAllocator->pfnAllocation(pArena->pAllocator->pUserData,
                                                              sizeof(cvkArenaBlock) + capacity + alignment,
                                                              CVK_JSON_ARENA_ALIGNMENT,
                                                              VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
    CVK_ASSERT(NULL != block);
    block->pNext = pArena->pBlocks;
    pArena->pBlocks = block;
    pArena->pBase = block->storage;
    pArena->size = capacity + alignment;
    pArena->used = 0;
}

/**
 * \brief Allocates aligned storage from an arena; exhausting caller storage is fatal.
 *
 * \param[in,out] pArena     The arena
 * \param[in]     size       Byte count; zero returns NULL
 * \param[in]     alignment  Power-of-two alignment
 *
 * \retval The storage, or NULL for a zero size
 */
void* cvkAllocateArena(cvkArena* pArena, size_t size, size_t alignment)
{
    size_t offset;
    size_t padding;
    size_t remaining;

    CVK_ASSERT(NULL != pArena);
    CVK_ASSERT(0 < alignment);
    CVK_ASSERT(0 == (alignment & (alignment - 1)));
    CVK_ASSERT(pArena->used <= pArena->size);
    if (0 == size)
    {
        return NULL;
    }
    if (NULL != pArena->pBase)
    {
        offset = pArena->used;
        remaining = pArena->size - offset;
        padding = (uintptr_t)(pArena->pBase + offset) % alignment;
        padding = (alignment - padding) % alignment;
        if ((padding <= remaining) && (size <= remaining - padding))
        {
            offset += padding;
            pArena->used = offset + size;
            return pArena->pBase + offset;
        }
    }
    cvkGrowArena(pArena, size, alignment);
    padding = (uintptr_t)pArena->pBase % alignment;
    padding = (alignment - padding) % alignment;
    CVK_ASSERT(size <= pArena->size - padding);
    pArena->used = padding + size;
    return pArena->pBase + padding;
}

/**
 * \brief Frees callback-backed arena blocks and resets the arena.
 * \param[in,out] pArena Arena to release.
 * \retval None No value is returned.
 */
void cvkFreeArena(cvkArena* pArena)
{
    cvkArenaBlock* block;
    cvkArenaBlock* next;

    CVK_ASSERT(NULL != pArena);
    CVK_ASSERT(NULL != pArena->pAllocator);
    block = pArena->pBlocks;
    while (NULL != block)
    {
        next = block->pNext;
        pArena->pAllocator->pfnFree(pArena->pAllocator->pUserData, block);
        block = next;
    }
    pArena->pBlocks = NULL;
    pArena->pBase = NULL;
    pArena->size = 0;
    pArena->used = 0;
}

/**
 * \brief Copies a JSON string into an arena as a NUL-terminated C string, resolving escapes.
 *
 * \param[in,out] pArena  The arena
 * \param[in]     pValue  The string value; a null yields NULL
 *
 * \retval The C string, or NULL
 */
char* cvkCopyJsonString(cvkArena* pArena, const cvkJsonValue* pValue)
{
    char*    out;
    uint32_t index;
    uint32_t written = 0;

    if (cvkIsJsonNull(pValue) || (CVK_JSON_STRING != pValue->kind))
    {
        return NULL;
    }
    out = (char*)cvkAllocateArena(pArena, (size_t)pValue->textLength + 1, 1);
    for (index = 0; index < pValue->textLength; ++index)
    {
        char character = pValue->pText[index];

        if (('\\' == character) && (index + 1 < pValue->textLength))
        {
            character = pValue->pText[++index];
            switch (character)
            {
                case 'n': character = '\n'; break;
                case 't': character = '\t'; break;
                case 'r': character = '\r'; break;
                case 'b': character = '\b'; break;
                case 'f': character = '\f'; break;
                case 'u': character = '?'; index += 4; break;
                default: break;
            }
        }
        out[written++] = character;
    }
    out[written] = '\0';
    return out;
}

/**
 * \brief Returns the JSON object ID recorded for a native handle field.
 *
 * \param[in] pReferences First reference, or NULL for an empty list.
 * \param[in] pHandle Address of the native handle field.
 *
 * \retval 0 No reference was recorded.
 * \retval Other The JSON object ID.
 */
uint64_t cvkGetJsonHandleId(const cvkJsonHandleReference* pReferences,
                          const void*                   pHandle)
{
    uint64_t object_id;

    object_id = 0;
    for (; NULL != pReferences; pReferences = pReferences->pNext)
    {
        if (pReferences->pHandle == pHandle)
        {
            object_id = pReferences->id;
            break;
        }
    }
    return object_id;
}

/**
 * \brief Resolves an object ID into native handle storage and records the ID separately.
 *
 * \par Details:
 *  The caller initializes the native handle to VK_NULL_HANDLE. A resolver writes
 *  that exact native type and leaves it unchanged on failure. Deferred resolution
 *  requires a caller-owned reference-list head. Records use the caller's arena.
 *
 * \param[in] pContext Parsing context and optional native resolver.
 * \param[in] pHandleType Vulkan handle type name.
 * \param[in] pValue JSON object ID, or null for the null handle.
 * \param[in] handleByteCount Byte extent of the native handle type.
 * \param[in,out] pHandle Native handle object, initially VK_NULL_HANDLE.
 *
 * \retval VK_TRUE The ID was null or resolved to a native handle.
 * \retval VK_FALSE The reference remains unresolved and its handle stays null.
 */
VkBool32 cvkResolveJsonHandle(const cvkJsonContext* pContext,
                             const char*           pHandleType,
                             const cvkJsonValue*    pValue,
                             uint32_t              handleByteCount,
                             void*                 pHandle)
{
    cvkJsonHandleReference* reference;
    uint64_t                object_id;
    VkBool32                resolved;

    CVK_ASSERT(NULL != pContext);
    CVK_ASSERT(NULL != pHandleType);
    CVK_ASSERT(NULL != pHandle);
    CVK_ASSERT(0 < handleByteCount);
    object_id       = cvkGetJsonUint64(pValue);
    resolved = (0 == object_id);
    if (0 != object_id)
    {
        if (NULL != pContext->ppHandleReferences)
        {
            reference = (cvkJsonHandleReference*)cvkAllocateArena(pContext->pArena,
                                                                 sizeof(*reference),
                                                                 CVK_JSON_ARENA_ALIGNMENT);
            reference->pNext       = *pContext->ppHandleReferences;
            reference->pHandleType = pHandleType;
            reference->pHandle     = pHandle;
            reference->id          = object_id;
            *pContext->ppHandleReferences = reference;
        }
        if (NULL != pContext->resolveHandle)
        {
            resolved = pContext->resolveHandle(pContext->pUser,
                                               pHandleType,
                                               object_id,
                                               handleByteCount,
                                               pHandle);
        }
        CVK_ASSERT(resolved || (NULL != pContext->ppHandleReferences));
    }
    return resolved;
}
