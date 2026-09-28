/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * Compatibility boundary between the current Cast session and utl_castv2.
 * Wire syntax, required fields, UTF-8 validation and framing belong to utl_castv2.
 * This adapter retains no storage and performs no allocation, CRT calls or I/O.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "os/os_compiler.h"
#include "cast_message.h"
#include "os/os_assert.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

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
 * \brief Counts terminated text within the maximum representable body span.
 *
 * \par Requirements:
 *  - 001 The operation shall allocate nothing, call no CRT and perform no I/O.
 *  - 002 Counted spans shall remain caller-owned and valid for the duration of use.
 *
 * \param[in] text   Non-NULL terminated text
 *
 * \retval    0..UTL_CASTV2_MAX_BODY   Bytes before NUL, saturated at the body limit.
 */
static FORCE_INLINE uint32_t GetTextByteCount(const char_t* text);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  A saturated field leaves no space for required envelope metadata, so serialization fails.
 */
static FORCE_INLINE uint32_t GetTextByteCount(const char_t* text)
{
    uint32_t length;

    length = 0;
    for (;;)
    {
        if (CAST_MESSAGE_MAX_BODY == length)
        {
            break;
        }
        if ('\0' == text[length])
        {
            break;
        }
        ++length;
    }
    return length;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Counting stops at the body limit. An over-limit field cannot fit a complete envelope and Encode rejects it.
 */
void CastMessage_Initialize(CastMessage_t* message,
                            const char_t*  source,
                            const char_t*  destination,
                            const char_t*  nameSpace,
                            const char_t*  payload)
{
    LOGASSERT(NULL != message);
    LOGASSERT(NULL != source);
    LOGASSERT(NULL != destination);
    LOGASSERT(NULL != nameSpace);
    LOGASSERT(NULL != payload);

    message->pSource           = source;
    message->sourceLength      = GetTextByteCount(source);
    message->pDestination      = destination;
    message->destinationLength = GetTextByteCount(destination);
    message->pNamespace        = nameSpace;
    message->namespaceLength   = GetTextByteCount(nameSpace);
    message->pPayload          = payload;
    message->payloadLength     = GetTextByteCount(payload);
    message->binary            = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Length is compared before any counted bytes; no CRT string operation is used.
 */
bool_t CastMessage_IsNamespace(const CastMessage_t* message, const char_t* nameSpace)
{
    bool_t   has_inputs;
    bool_t   is_equal;
    uint32_t length;
    uint32_t index;

    has_inputs = (NULL != message) && (NULL != nameSpace);
    is_equal   = FALSE;
    if (has_inputs)
    {
        length   = GetTextByteCount(nameSpace);
        is_equal = (length == message->namespaceLength);
        if (is_equal)
        {
            is_equal = (0 == length) || (NULL != message->pNamespace);
        }
        if (is_equal)
        {
            for (index = 0; index < length; ++index)
            {
                if (nameSpace[index] != message->pNamespace[index])
                {
                    is_equal = FALSE;
                    break;
                }
            }
        }
    }
    return is_equal;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Both text and binary output use the same typed model and bounded writer.
 */
uint32_t CastMessage_Encode(const CastMessage_t* message, byte_t* buffer, uint32_t capacity)
{
    uint32_t             written;
    Utl_CastV2_Message_t model;

    written = 0;
    if (NULL != message)
    {
        model.source.data           = (const byte_t*)message->pSource;
        model.source.byteCount      = message->sourceLength;
        model.destination.data      = (const byte_t*)message->pDestination;
        model.destination.byteCount = message->destinationLength;
        model.nameSpace.data        = (const byte_t*)message->pNamespace;
        model.nameSpace.byteCount   = message->namespaceLength;
        model.payloadType           = UTL_CASTV2_STRING;
        model.payload.data          = (const byte_t*)message->pPayload;
        model.payload.byteCount     = message->payloadLength;
        if (message->binary)
        {
            model.payloadType = UTL_CASTV2_BINARY;
        }
        written = Utl_CastV2_WriteFrame(&model, buffer, capacity);
    }
    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Only a successful specification parse publishes the borrowed application view.
 */
bool_t CastMessage_Decode(const byte_t* buffer, uint32_t byteCount, CastMessage_t* message)
{
    bool_t               is_valid;
    Utl_CastV2_Message_t model;

    is_valid = (NULL != message);
    if (is_valid)
    {
        is_valid = Utl_CastV2_ReadMessage(&model, buffer, byteCount);
        if (is_valid)
        {
            message->pSource           = (const char_t*)model.source.data;
            message->sourceLength      = model.source.byteCount;
            message->pDestination      = (const char_t*)model.destination.data;
            message->destinationLength = model.destination.byteCount;
            message->pNamespace        = (const char_t*)model.nameSpace.data;
            message->namespaceLength   = model.nameSpace.byteCount;
            message->pPayload          = (const char_t*)model.payload.data;
            message->payloadLength     = model.payload.byteCount;
            message->binary            = (UTL_CASTV2_BINARY == model.payloadType);
        }
    }
    return is_valid;
}
