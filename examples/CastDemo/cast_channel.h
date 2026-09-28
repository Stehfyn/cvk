/* cast_channel.h - a CASTV2 connection: TCP to port 8009, TLS through SChannel, framed
** CastMessages in both directions. Cast receivers present self-signed certificates, so the
** certificate chain is not validated; the channel is only as trusted as the local network. */
#ifndef CAST_CHANNEL_H
#define CAST_CHANNEL_H

#include "utl/utl_castv2.h"

#define CAST_PORT (8009)

typedef struct CastChannel CastChannel;

const char* CastChannel_GetError(const CastChannel* pChannel);

/* Connects and completes the TLS handshake. WSAStartup must have been called.
** On failure returns NULL and describes the problem in pError. */
CastChannel* CastChannel_Connect(const char* pHost, unsigned short port, char* pError, size_t errorCapacity);

/* Encodes, encrypts and sends one message. Returns 1 on success. */
int CastChannel_Send(CastChannel* pChannel, const Utl_CastV2_Message_t* pMessage);

/* Waits up to timeoutMs for a complete frame. Returns 1 with the message borrowing channel
** storage (valid until the next call), 0 on timeout, -1 when the connection failed or closed. */
int CastChannel_Receive(CastChannel* pChannel, Utl_CastV2_Message_t* pMessage, unsigned timeoutMs);

void CastChannel_Close(CastChannel* pChannel);

#endif
