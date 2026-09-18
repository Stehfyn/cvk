/* cast_message.h - CASTV2 frames: a 4-byte big-endian body length followed by the CastMessage
** protocol buffer (Chromium extensions/common/api/cast_channel/cast_channel.proto).
**
** Only the fields the default media receiver uses are produced: protocol_version 0,
** source_id, destination_id, namespace, payload_type STRING and payload_utf8. Decoding
** tolerates unknown fields and reports a BINARY payload without interpreting it. */
#ifndef CAST_MESSAGE_H
#define CAST_MESSAGE_H

#include <stddef.h>
#include <stdint.h>

/* The protocol caps one body at 64 KiB. */
#define CAST_MESSAGE_MAX_BODY  (65536)
#define CAST_MESSAGE_MAX_FRAME (4 + CAST_MESSAGE_MAX_BODY)

typedef struct CastMessage
{
    const char* pSource;
    size_t      sourceLength;
    const char* pDestination;
    size_t      destinationLength;
    const char* pNamespace;
    size_t      namespaceLength;
    const char* pPayload; /* UTF-8 JSON text, or the raw bytes when binary is set */
    size_t      payloadLength;
    int         binary;
} CastMessage;

/* Points the message at four NUL-terminated strings. */
void CastMessage_Set(CastMessage* pMessage, const char* pSource, const char* pDestination, const char* pNamespace, const char* pPayload);

/* Writes the length prefix and body. Returns the frame size, or 0 when it does not fit. */
size_t CastMessage_Encode(const CastMessage* pMessage, uint8_t* pOut, size_t capacity);

/* Parses one body (without the length prefix). The message borrows the body storage.
** Returns 1 when source, destination, namespace and payload were all present. */
int CastMessage_Decode(const uint8_t* pBody, size_t length, CastMessage* pMessage);

int CastMessage_NamespaceIs(const CastMessage* pMessage, const char* pNamespace);

#endif
