/* cast_message.c - hand-rolled protocol buffer encoding of CastMessage; no generated code. */
#include "cast_message.h"

#include <string.h>

enum
{
    WIRE_VARINT  = 0,
    WIRE_FIXED64 = 1,
    WIRE_LENGTH  = 2,
    WIRE_FIXED32 = 5
};

enum
{
    FIELD_PROTOCOL_VERSION = 1,
    FIELD_SOURCE_ID        = 2,
    FIELD_DESTINATION_ID   = 3,
    FIELD_NAMESPACE        = 4,
    FIELD_PAYLOAD_TYPE     = 5,
    FIELD_PAYLOAD_UTF8     = 6,
    FIELD_PAYLOAD_BINARY   = 7
};

enum
{
    PAYLOAD_STRING = 0,
    PAYLOAD_BINARY = 1
};

static size_t VarintSize(uint64_t value)
{
    size_t size = 1;

    while (value >= 0x80)
    {
        value >>= 7;
        ++size;
    }
    return size;
}

static size_t PutVarint(uint8_t* pOut, uint64_t value)
{
    size_t size = 0;

    while (value >= 0x80)
    {
        pOut[size++] = (uint8_t)(value | 0x80);
        value >>= 7;
    }
    pOut[size++] = (uint8_t)value;
    return size;
}

static size_t StringFieldSize(size_t length)
{
    return 1 + VarintSize(length) + length;
}

static size_t PutStringField(uint8_t* pOut, unsigned field, const char* pText, size_t length)
{
    size_t size = 0;

    pOut[size++] = (uint8_t)((field << 3) | WIRE_LENGTH);
    size += PutVarint(pOut + size, length);
    if (length > 0)
    {
        memcpy(pOut + size, pText, length);
    }
    return size + length;
}

static size_t PutVarintField(uint8_t* pOut, unsigned field, uint64_t value)
{
    pOut[0] = (uint8_t)((field << 3) | WIRE_VARINT);
    return 1 + PutVarint(pOut + 1, value);
}

static int GetVarint(const uint8_t* pBody, size_t length, size_t* pPosition, uint64_t* pValue)
{
    uint64_t value = 0;
    unsigned shift = 0;

    while (*pPosition < length && shift < 64)
    {
        const uint8_t byte = pBody[(*pPosition)++];

        value |= (uint64_t)(byte & 0x7F) << shift;
        if (0 == (byte & 0x80))
        {
            *pValue = value;
            return 1;
        }
        shift += 7;
    }
    return 0;
}

void CastMessage_Set(CastMessage* pMessage, const char* pSource, const char* pDestination, const char* pNamespace, const char* pPayload)
{
    pMessage->pSource           = pSource;
    pMessage->sourceLength      = strlen(pSource);
    pMessage->pDestination      = pDestination;
    pMessage->destinationLength = strlen(pDestination);
    pMessage->pNamespace        = pNamespace;
    pMessage->namespaceLength   = strlen(pNamespace);
    pMessage->pPayload          = pPayload;
    pMessage->payloadLength     = strlen(pPayload);
    pMessage->binary            = 0;
}

size_t CastMessage_Encode(const CastMessage* pMessage, uint8_t* pOut, size_t capacity)
{
    const size_t body = 2 + StringFieldSize(pMessage->sourceLength) + StringFieldSize(pMessage->destinationLength) + StringFieldSize(pMessage->namespaceLength) + 2 +
                        StringFieldSize(pMessage->payloadLength);
    size_t size;

    if (pMessage->binary || body > CAST_MESSAGE_MAX_BODY || capacity < 4 + body)
    {
        return 0;
    }
    pOut[0] = (uint8_t)(body >> 24);
    pOut[1] = (uint8_t)(body >> 16);
    pOut[2] = (uint8_t)(body >> 8);
    pOut[3] = (uint8_t)body;
    size    = 4;
    size += PutVarintField(pOut + size, FIELD_PROTOCOL_VERSION, 0);
    size += PutStringField(pOut + size, FIELD_SOURCE_ID, pMessage->pSource, pMessage->sourceLength);
    size += PutStringField(pOut + size, FIELD_DESTINATION_ID, pMessage->pDestination, pMessage->destinationLength);
    size += PutStringField(pOut + size, FIELD_NAMESPACE, pMessage->pNamespace, pMessage->namespaceLength);
    size += PutVarintField(pOut + size, FIELD_PAYLOAD_TYPE, PAYLOAD_STRING);
    size += PutStringField(pOut + size, FIELD_PAYLOAD_UTF8, pMessage->pPayload, pMessage->payloadLength);
    return size;
}

int CastMessage_Decode(const uint8_t* pBody, size_t length, CastMessage* pMessage)
{
    size_t position = 0;

    memset(pMessage, 0, sizeof(*pMessage));
    while (position < length)
    {
        uint64_t tag;
        uint64_t value;
        unsigned field;
        unsigned wire;

        if (!GetVarint(pBody, length, &position, &tag))
        {
            return 0;
        }
        field = (unsigned)(tag >> 3);
        wire  = (unsigned)(tag & 7);
        switch (wire)
        {
        case WIRE_VARINT:
            if (!GetVarint(pBody, length, &position, &value))
            {
                return 0;
            }
            if (FIELD_PROTOCOL_VERSION == field && 0 != value)
            {
                return 0;
            }
            if (FIELD_PAYLOAD_TYPE == field)
            {
                pMessage->binary = (PAYLOAD_BINARY == value);
            }
            break;
        case WIRE_LENGTH:
            if (!GetVarint(pBody, length, &position, &value) || value > length - position)
            {
                return 0;
            }
            switch (field)
            {
            case FIELD_SOURCE_ID:
                pMessage->pSource      = (const char*)pBody + position;
                pMessage->sourceLength = (size_t)value;
                break;
            case FIELD_DESTINATION_ID:
                pMessage->pDestination      = (const char*)pBody + position;
                pMessage->destinationLength = (size_t)value;
                break;
            case FIELD_NAMESPACE:
                pMessage->pNamespace      = (const char*)pBody + position;
                pMessage->namespaceLength = (size_t)value;
                break;
            case FIELD_PAYLOAD_UTF8:
            case FIELD_PAYLOAD_BINARY:
                pMessage->pPayload      = (const char*)pBody + position;
                pMessage->payloadLength = (size_t)value;
                break;
            default:
                break;
            }
            position += (size_t)value;
            break;
        case WIRE_FIXED64:
            if (length - position < 8)
            {
                return 0;
            }
            position += 8;
            break;
        case WIRE_FIXED32:
            if (length - position < 4)
            {
                return 0;
            }
            position += 4;
            break;
        default:
            return 0;
        }
    }
    return NULL != pMessage->pSource && NULL != pMessage->pDestination && NULL != pMessage->pNamespace && NULL != pMessage->pPayload;
}

int CastMessage_NamespaceIs(const CastMessage* pMessage, const char* pNamespace)
{
    const size_t length = strlen(pNamespace);

    return pMessage->namespaceLength == length && 0 == memcmp(pMessage->pNamespace, pNamespace, length);
}
