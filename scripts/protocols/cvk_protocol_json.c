/* Tier 3 protocol (SPEC.md section 8c): a JSON reader with no allocation, an arena, and the
** lookups the generated parsers (cvkParseJson*) use. Source of truth kept with the generator
** and folded into the generated cvk.h/cvk.c. Composed from the generated surface only. */

#include <stdlib.h>
#include <ctype.h>

/**
 * \brief Parses one JSON value at the reader's cursor into the reader's value array and returns its index.
 *
 * \par Details:
 *  Recursive descent over objects, arrays, strings, numbers, and literals. Strings keep their
 *  escapes as written; numbers keep their text. Malformed input or a full value array is fatal.
 *
 * \param[in,out] pReader  Reader state: cursor, end, value array, capacity, count
 *
 * \retval Index of the parsed value in the reader's value array
 */
uint32_t cvkParseJsonValue(cvkJsonReader* pReader)
{
    uint32_t      index;
    uint32_t      previous;
    uint32_t      child;
    const char*   p;
    cvkJsonValue* value;

    while ((pReader->pCursor < pReader->pEnd) && ((' ' == *pReader->pCursor) || ('\t' == *pReader->pCursor) || ('\n' == *pReader->pCursor) || ('\r' == *pReader->pCursor)))
    {
        ++pReader->pCursor;
    }
    CVK_ASSERT(pReader->pCursor < pReader->pEnd);
    CVK_ASSERT(pReader->valueCount < pReader->valueCapacity);
    index                = pReader->valueCount++;
    value                = &pReader->pValues[index];
    value->kind          = CVK_JSON_NULL;
    value->pText         = NULL;
    value->textLength    = 0;
    value->pKey          = NULL;
    value->keyLength     = 0;
    value->childCount    = 0;
    value->firstChild    = CVK_JSON_NONE;
    value->nextSibling   = CVK_JSON_NONE;
    p                    = pReader->pCursor;
    if ('{' == *p || '[' == *p)
    {
        const char close = ('{' == *p) ? '}' : ']';

        value->kind      = ('{' == *p) ? CVK_JSON_OBJECT : CVK_JSON_ARRAY;
        pReader->pCursor = p + 1;
        previous         = CVK_JSON_NONE;
        for (;;)
        {
            while ((pReader->pCursor < pReader->pEnd) && ((' ' == *pReader->pCursor) || ('\t' == *pReader->pCursor) || ('\n' == *pReader->pCursor) || ('\r' == *pReader->pCursor)))
            {
                ++pReader->pCursor;
            }
            CVK_ASSERT(pReader->pCursor < pReader->pEnd);
            if (close == *pReader->pCursor)
            {
                ++pReader->pCursor;
                break;
            }
            if (CVK_JSON_OBJECT == value->kind)
            {
                const char* key;
                const char* key_end;

                CVK_ASSERT('"' == *pReader->pCursor);
                key     = pReader->pCursor + 1;
                key_end = key;
                while ((key_end < pReader->pEnd) && ('"' != *key_end))
                {
                    key_end += ('\\' == *key_end) ? 2 : 1;
                }
                CVK_ASSERT(key_end < pReader->pEnd);
                pReader->pCursor = key_end + 1;
                while ((pReader->pCursor < pReader->pEnd) && ((' ' == *pReader->pCursor) || ('\t' == *pReader->pCursor) || ('\n' == *pReader->pCursor) || ('\r' == *pReader->pCursor)))
                {
                    ++pReader->pCursor;
                }
                CVK_ASSERT((pReader->pCursor < pReader->pEnd) && (':' == *pReader->pCursor));
                ++pReader->pCursor;
                child                              = cvkParseJsonValue(pReader);
                pReader->pValues[child].pKey       = key;
                pReader->pValues[child].keyLength  = (uint32_t)(key_end - key);
            }
            else
            {
                child = cvkParseJsonValue(pReader);
            }
            value = &pReader->pValues[index]; /* the value array is stable, the pointer is re-read for clarity */
            if (CVK_JSON_NONE == previous)
            {
                value->firstChild = child;
            }
            else
            {
                pReader->pValues[previous].nextSibling = child;
            }
            previous = child;
            ++value->childCount;
            while ((pReader->pCursor < pReader->pEnd) && ((' ' == *pReader->pCursor) || ('\t' == *pReader->pCursor) || ('\n' == *pReader->pCursor) || ('\r' == *pReader->pCursor)))
            {
                ++pReader->pCursor;
            }
            CVK_ASSERT(pReader->pCursor < pReader->pEnd);
            if (',' == *pReader->pCursor)
            {
                ++pReader->pCursor;
                continue;
            }
            CVK_ASSERT(close == *pReader->pCursor);
            ++pReader->pCursor;
            break;
        }
        return index;
    }
    if ('"' == *p)
    {
        const char* end = p + 1;

        while ((end < pReader->pEnd) && ('"' != *end))
        {
            end += ('\\' == *end) ? 2 : 1;
        }
        CVK_ASSERT(end < pReader->pEnd);
        value->kind       = CVK_JSON_STRING;
        value->pText      = p + 1;
        value->textLength = (uint32_t)(end - (p + 1));
        pReader->pCursor  = end + 1;
        return index;
    }
    if ('t' == *p || 'f' == *p || 'n' == *p)
    {
        const char*  literal = ('t' == *p) ? "true" : (('f' == *p) ? "false" : "null");
        const size_t length  = strlen(literal);

        CVK_ASSERT(((size_t)(pReader->pEnd - p) >= length) && (0 == memcmp(p, literal, length)));
        value->kind      = ('t' == *p) ? CVK_JSON_TRUE : (('f' == *p) ? CVK_JSON_FALSE : CVK_JSON_NULL);
        pReader->pCursor = p + length;
        return index;
    }
    {
        const char* end = p;

        while ((end < pReader->pEnd) && (('-' == *end) || ('+' == *end) || ('.' == *end) || ('e' == *end) || ('E' == *end) || (('0' <= *end) && ('9' >= *end))))
        {
            ++end;
        }
        CVK_ASSERT(end > p);
        value->kind       = CVK_JSON_NUMBER;
        value->pText      = p;
        value->textLength = (uint32_t)(end - p);
        pReader->pCursor  = end;
        return index;
    }
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
    while ((reader.pCursor < reader.pEnd) && ((' ' == *reader.pCursor) || ('\t' == *reader.pCursor) || ('\n' == *reader.pCursor) || ('\r' == *reader.pCursor)))
    {
        ++reader.pCursor;
    }
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
 * \brief Reads a number as an unsigned 64-bit integer; a string holding a number is accepted.
 *
 * \param[in]  pValue  The value, or NULL for zero
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
    base = 10;
    negative = VK_FALSE;
    while (isspace((unsigned char)*pText))
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
        base = 8;
        ++pText;
        if (('x' == *pText) || ('X' == *pText))
        {
            base = 16;
            ++pText;
        }
    }
    while ('\0' != *pText)
    {
        if (('0' <= *pText) && (*pText <= '9'))
        {
            digit = (uint32_t)(*pText - '0');
        }
        else if (('a' <= *pText) && (*pText <= 'f'))
        {
            digit = (uint32_t)(*pText - 'a') + 10;
        }
        else if (('A' <= *pText) && (*pText <= 'F'))
        {
            digit = (uint32_t)(*pText - 'A') + 10;
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
        value = value * base + digit;
        ++pText;
    }
    return negative ? (uint64_t)0 - value : value;
}

uint64_t cvkGetJsonUint64(const cvkJsonValue* pValue)
{
    char buffer[32];

    if ((NULL == pValue) || ((CVK_JSON_NUMBER != pValue->kind) && (CVK_JSON_STRING != pValue->kind)) || (pValue->textLength >= sizeof(buffer)))
    {
        return 0;
    }
    memcpy(buffer, pValue->pText, pValue->textLength);
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
    char buffer[32];
    const char* text;
    uint64_t magnitude;
    VkBool32 negative;

    if ((NULL == pValue) || ((CVK_JSON_NUMBER != pValue->kind) && (CVK_JSON_STRING != pValue->kind)) || (pValue->textLength >= sizeof(buffer)))
    {
        return 0;
    }
    memcpy(buffer, pValue->pText, pValue->textLength);
    buffer[pValue->textLength] = '\0';
    text = buffer;
    while (isspace((unsigned char)*text))
    {
        ++text;
    }
    negative = ('-' == *text) ? VK_TRUE : VK_FALSE;
    if (('-' == *text) || ('+' == *text))
    {
        ++text;
    }
    magnitude = cvkParseUint64Text(text);
    if (negative)
    {
        return magnitude > (uint64_t)INT64_MAX ? INT64_MIN : -(int64_t)magnitude;
    }
    return magnitude > (uint64_t)INT64_MAX ? INT64_MAX : (int64_t)magnitude;
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
    char buffer[64];

    if ((NULL == pValue) || ((CVK_JSON_NUMBER != pValue->kind) && (CVK_JSON_STRING != pValue->kind)) || (pValue->textLength >= sizeof(buffer)))
    {
        return 0.0;
    }
    memcpy(buffer, pValue->pText, pValue->textLength);
    buffer[pValue->textLength] = '\0';
    return strtod(buffer, NULL);
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
    const char* p;
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
    p   = pValue->pText;
    end = p + pValue->textLength;
    while (p < end)
    {
        const char* token;
        const char* token_end;
        uint64_t    bit;

        while ((p < end) && ((' ' == *p) || ('|' == *p)))
        {
            ++p;
        }
        token = p;
        while ((p < end) && (' ' != *p) && ('|' != *p))
        {
            ++p;
        }
        token_end = p;
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
            char buffer[32];

            CVK_ASSERT((size_t)(token_end - token) < sizeof(buffer));
            memcpy(buffer, token, (size_t)(token_end - token));
            buffer[token_end - token] = '\0';
            flags |= cvkParseUint64Text(buffer); /* a numeric token */
        }
    }
    return flags;
}

/**
 * \brief Allocates aligned storage from an arena; exhausting it is fatal.
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

    if (0 == size)
    {
        return NULL;
    }
    offset = (pArena->used + alignment - 1) & ~(alignment - 1);
    CVK_ASSERT(offset + size <= pArena->size);
    pArena->used = offset + size;
    return pArena->pBase + offset;
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
    uint32_t i;
    uint32_t n = 0;

    if (cvkIsJsonNull(pValue) || (CVK_JSON_STRING != pValue->kind))
    {
        return NULL;
    }
    out = (char*)cvkAllocateArena(pArena, (size_t)pValue->textLength + 1, 1);
    for (i = 0; i < pValue->textLength; ++i)
    {
        char c = pValue->pText[i];

        if (('\\' == c) && (i + 1 < pValue->textLength))
        {
            c = pValue->pText[++i];
            switch (c)
            {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': c = '?'; i += 4; break;
                default: break;
            }
        }
        out[n++] = c;
    }
    out[n] = '\0';
    return out;
}

/**
 * \brief Resolves a handle written to JSON: a number is an object id given to the context's resolver, an empty string or null is VK_NULL_HANDLE.
 *
 * \param[in]  pContext     Parsing context; without a resolver the id itself is the handle value
 * \param[in]  pHandleType  Vulkan handle type name ("VkPipelineLayout")
 * \param[in]  pValue       The value
 *
 * \retval The handle as an integer
 */
uint64_t cvkResolveJsonHandle(const cvkJsonContext* pContext, const char* pHandleType, const cvkJsonValue* pValue)
{
    uint64_t id;

    if (cvkIsJsonNull(pValue) || ((CVK_JSON_STRING == pValue->kind) && (0 == pValue->textLength)))
    {
        return 0;
    }
    id = cvkGetJsonUint64(pValue);
    return (NULL != pContext->resolveHandle) ? pContext->resolveHandle(pContext->pUser, pHandleType, id) : id;
}
