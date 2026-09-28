/* cast_channel.c - Winsock transport, SChannel TLS client, CASTV2 framing. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cast_channel.h"

/* One TLS record is at most 16 KiB of payload plus header and trailer. */
#define INCOMING_CAPACITY  (32 * 1024)
#define PLAINTEXT_CAPACITY (CAST_MESSAGE_MAX_FRAME + 16 * 1024)
#define CHUNK_CAPACITY     (20 * 1024)

struct CastChannel
{
    SOCKET                    socket;
    CredHandle                credentials;
    CtxtHandle                context;
    int                       haveCredentials;
    int                       haveContext;
    SecPkgContext_StreamSizes sizes;
    char                      error[256];
    /* ciphertext received but not yet decrypted */
    uint8_t incoming[INCOMING_CAPACITY];
    size_t  incomingUsed;
    /* decrypted stream, framed into CastMessages */
    uint8_t plain[PLAINTEXT_CAPACITY];
    size_t  plainUsed;
    size_t  plainConsumed;
    /* one encoded frame and one encrypted record */
    uint8_t frame[CAST_MESSAGE_MAX_FRAME];
    uint8_t chunk[CHUNK_CAPACITY];
};

/*************************************************************************************************/
/**
 * \par Details:
 *  Formats the operation name and an optional hexadecimal status into the
 *  caller's diagnostic buffer.
 */
static void SetError(char* pError, size_t capacity, const char* pWhat, unsigned long code)
{
    if (0 != code)
    {
        (void)snprintf(pError, capacity, "%s (0x%08lX)", pWhat, code);
    }
    else
    {
        (void)snprintf(pError, capacity, "%s", pWhat);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Advances through partial socket writes until all bytes are sent or send
 *  reports an error or a closed connection.
 */
static int SendAll(SOCKET s, const void* pData, size_t length)
{
    const char* pCursor = (const char*)pData;

    for (;;)
    {
        int sent;

        if (0 == length)
        {
            break;
        }
        sent = send(s, pCursor, (int)length, 0);

        if (sent <= 0)
        {
            return 0;
        }
        pCursor += sent;
        length -= (size_t)sent;
    }
    return 1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Tries resolved TCP addresses in order and disables Nagle after connection.
 *  Releases the address list and closes each unsuccessful socket.
 */
static SOCKET ConnectTcp(const char* pHost, unsigned short port, char* pError, size_t errorCapacity)
{
    struct addrinfo  hints;
    struct addrinfo* pResults = NULL;
    struct addrinfo* pAddress;
    SOCKET           s = INVALID_SOCKET;
    char             service[8];

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    (void)snprintf(service, sizeof(service), "%u", (unsigned)port);
    if (0 != getaddrinfo(pHost, service, &hints, &pResults))
    {
        SetError(pError, errorCapacity, "cannot resolve the receiver address", (unsigned long)WSAGetLastError());
        return INVALID_SOCKET;
    }
    for (pAddress = pResults; NULL != pAddress; pAddress = pAddress->ai_next)
    {
        s = socket(pAddress->ai_family, pAddress->ai_socktype, pAddress->ai_protocol);
        if (INVALID_SOCKET == s)
        {
            continue;
        }
        if (0 == connect(s, pAddress->ai_addr, (int)pAddress->ai_addrlen))
        {
            const BOOL noDelay = TRUE;

            /* Control messages are tiny; never let Nagle hold a SEEK behind an unacknowledged PING. */
            setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
            break;
        }
        closesocket(s);
        s = INVALID_SOCKET;
    }
    freeaddrinfo(pResults);
    if (INVALID_SOCKET == s)
    {
        SetError(pError, errorCapacity, "cannot connect to the receiver", (unsigned long)WSAGetLastError());
    }
    return s;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Drives the Schannel client handshake, sends generated tokens and retains
 *  unconsumed ciphertext for the next step. The current Cast connection uses
 *  manual certificate validation with no certificate-chain or host-name check.
 */
static int Handshake(CastChannel* pChannel, const char* pHost, char* pError, size_t errorCapacity)
{
    const DWORD requested =
        ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION | ISC_REQ_USE_SUPPLIED_CREDS;
    SCHANNEL_CRED   credentials;
    TimeStamp       expiry;
    SECURITY_STATUS status;
    DWORD           granted;
    WCHAR           host[256];
    SecBuffer       outBuffer;
    SecBufferDesc   outDesc;

    /* Cast receivers use self-signed certificates: no chain validation, no server-name check. */
    ZeroMemory(&credentials, sizeof(credentials));
    credentials.dwVersion             = SCHANNEL_CRED_VERSION;
    credentials.grbitEnabledProtocols = SP_PROT_TLS1_2_CLIENT;
    credentials.dwFlags               = SCH_CRED_MANUAL_CRED_VALIDATION | SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_NO_SERVERNAME_CHECK | SCH_USE_STRONG_CRYPTO;
    status                            = AcquireCredentialsHandleW(NULL, UNISP_NAME_W, SECPKG_CRED_OUTBOUND, NULL, &credentials, NULL, NULL, &pChannel->credentials, &expiry);
    if (SEC_E_OK != status)
    {
        SetError(pError, errorCapacity, "AcquireCredentialsHandle failed", (unsigned long)status);
        return 0;
    }
    pChannel->haveCredentials = 1;
    if (0 == MultiByteToWideChar(CP_UTF8, 0, pHost, -1, host, (int)(sizeof(host) / sizeof(host[0]))))
    {
        host[0] = 0;
    }

    outBuffer.cbBuffer   = 0;
    outBuffer.BufferType = SECBUFFER_TOKEN;
    outBuffer.pvBuffer   = NULL;
    outDesc.ulVersion    = SECBUFFER_VERSION;
    outDesc.cBuffers     = 1;
    outDesc.pBuffers     = &outBuffer;
    status               = InitializeSecurityContextW(&pChannel->credentials, NULL, host, requested, 0, 0, NULL, 0, &pChannel->context, &outDesc, &granted, NULL);
    if (SEC_I_CONTINUE_NEEDED != status)
    {
        SetError(pError, errorCapacity, "InitializeSecurityContext failed to start", (unsigned long)status);
        return 0;
    }
    pChannel->haveContext = 1;
    if (!SendAll(pChannel->socket, outBuffer.pvBuffer, outBuffer.cbBuffer))
    {
        FreeContextBuffer(outBuffer.pvBuffer);
        SetError(pError, errorCapacity, "cannot send the ClientHello", (unsigned long)WSAGetLastError());
        return 0;
    }
    FreeContextBuffer(outBuffer.pvBuffer);

    pChannel->incomingUsed = 0;
    for (;;)
    {
        int received;

        if (pChannel->incomingUsed >= INCOMING_CAPACITY)
        {
            SetError(pError, errorCapacity, "TLS handshake record too large", 0);
            return 0;
        }
        received = recv(pChannel->socket, (char*)pChannel->incoming + pChannel->incomingUsed, (int)(INCOMING_CAPACITY - pChannel->incomingUsed), 0);
        if (received <= 0)
        {
            SetError(pError, errorCapacity, "connection closed during the TLS handshake", (unsigned long)WSAGetLastError());
            return 0;
        }
        pChannel->incomingUsed += (size_t)received;
        for (;;)
        {
            SecBuffer     inBuffers[2];
            SecBufferDesc inDesc;

            inBuffers[0].cbBuffer   = (unsigned long)pChannel->incomingUsed;
            inBuffers[0].BufferType = SECBUFFER_TOKEN;
            inBuffers[0].pvBuffer   = pChannel->incoming;
            inBuffers[1].cbBuffer   = 0;
            inBuffers[1].BufferType = SECBUFFER_EMPTY;
            inBuffers[1].pvBuffer   = NULL;
            inDesc.ulVersion        = SECBUFFER_VERSION;
            inDesc.cBuffers         = 2;
            inDesc.pBuffers         = inBuffers;
            outBuffer.cbBuffer      = 0;
            outBuffer.BufferType    = SECBUFFER_TOKEN;
            outBuffer.pvBuffer      = NULL;
            status                  = InitializeSecurityContextW(&pChannel->credentials, &pChannel->context, NULL, requested, 0, 0, &inDesc, 0, NULL, &outDesc, &granted, NULL);
            if (SEC_E_INCOMPLETE_MESSAGE == status)
            {
                break; /* the record is not complete: receive more */
            }
            if (NULL != outBuffer.pvBuffer && outBuffer.cbBuffer > 0)
            {
                const int sent = SendAll(pChannel->socket, outBuffer.pvBuffer, outBuffer.cbBuffer);

                FreeContextBuffer(outBuffer.pvBuffer);
                if (!sent)
                {
                    SetError(pError, errorCapacity, "cannot send a handshake message", (unsigned long)WSAGetLastError());
                    return 0;
                }
            }
            if (SECBUFFER_EXTRA == inBuffers[1].BufferType && inBuffers[1].cbBuffer > 0)
            {
                memmove(pChannel->incoming, pChannel->incoming + pChannel->incomingUsed - inBuffers[1].cbBuffer, inBuffers[1].cbBuffer);
                pChannel->incomingUsed = inBuffers[1].cbBuffer;
            }
            else
            {
                pChannel->incomingUsed = 0;
            }
            if (SEC_E_OK == status)
            {
                status = QueryContextAttributesW(&pChannel->context, SECPKG_ATTR_STREAM_SIZES, &pChannel->sizes);
                if (SEC_E_OK != status)
                {
                    SetError(pError, errorCapacity, "QueryContextAttributes(stream sizes) failed", (unsigned long)status);
                    return 0;
                }
                if (pChannel->sizes.cbHeader + pChannel->sizes.cbMaximumMessage + pChannel->sizes.cbTrailer > CHUNK_CAPACITY)
                {
                    SetError(pError, errorCapacity, "TLS record sizes exceed the channel buffers", 0);
                    return 0;
                }
                return 1;
            }
            if (SEC_I_CONTINUE_NEEDED != status)
            {
                SetError(pError, errorCapacity, "TLS handshake failed", (unsigned long)status);
                return 0;
            }
            if (0 == pChannel->incomingUsed)
            {
                break; /* nothing buffered: receive the next flight */
            }
        }
    }
}





/* Decrypts the buffered records into the plaintext stream. Returns 1 when more ciphertext is
** needed, 0 on a fatal condition. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Appends decrypted data to the plaintext buffer and compacts Schannel's
 *  unconsumed ciphertext. An incomplete TLS record remains buffered for more input.
 *  A complete frame returns control before decrypting the following TLS record,
 *  keeping coalesced frames within the fixed plaintext capacity.
 */
static int DecryptBuffered(CastChannel* pChannel)
{
    for (;;)
    {
        const uint8_t*         pExtra;
        uint32_t               extraLength;
        SecBuffer              buffers[4];
        int                    index;
        SecBufferDesc          desc;
        SECURITY_STATUS        status;
        bool_t                 has_consumed_record;
        Utl_CastV2_FrameState_t frame_state;
        uint32_t               frame_bytes;

        if (0 == pChannel->incomingUsed)
        {
            break;
        }
        pExtra               = NULL;
        extraLength          = 0;
        buffers[0].cbBuffer   = (unsigned long)pChannel->incomingUsed;
        buffers[0].BufferType = SECBUFFER_DATA;
        buffers[0].pvBuffer   = pChannel->incoming;
        for (index = 1; index < 4; ++index)
        {
            buffers[index].cbBuffer   = 0;
            buffers[index].BufferType = SECBUFFER_EMPTY;
            buffers[index].pvBuffer   = NULL;
        }
        desc.ulVersion = SECBUFFER_VERSION;
        desc.cBuffers  = 4;
        desc.pBuffers  = buffers;
        status         = DecryptMessage(&pChannel->context, &desc, 0, NULL);
        if (SEC_E_INCOMPLETE_MESSAGE == status)
        {
            return 1;
        }
        if (SEC_I_CONTEXT_EXPIRED == status)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "the receiver closed the TLS session", 0);
            return 0;
        }
        if (SEC_E_OK != status)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "DecryptMessage failed", (unsigned long)status);
            return 0;
        }
        for (index = 0; index < 4; ++index)
        {
            if (SECBUFFER_DATA == buffers[index].BufferType && buffers[index].cbBuffer > 0)
            {
                if (pChannel->plainUsed + buffers[index].cbBuffer > PLAINTEXT_CAPACITY)
                {
                    SetError(pChannel->error, sizeof(pChannel->error), "frame exceeds the CASTV2 limit", 0);
                    return 0;
                }
                memcpy(pChannel->plain + pChannel->plainUsed, buffers[index].pvBuffer, buffers[index].cbBuffer);
                pChannel->plainUsed += buffers[index].cbBuffer;
            }
            else if (SECBUFFER_EXTRA == buffers[index].BufferType && buffers[index].cbBuffer > 0)
            {
                pExtra      = (const uint8_t*)buffers[index].pvBuffer;
                extraLength = buffers[index].cbBuffer;
            }
        }
        has_consumed_record = (extraLength < pChannel->incomingUsed);
        if (!has_consumed_record)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "TLS decryption made no progress", 0);
            return 0;
        }
        if (extraLength > 0)
        {
            memmove(pChannel->incoming, pExtra, extraLength); /* the extra bytes live inside incoming */
        }
        pChannel->incomingUsed = extraLength;
        frame_state           = Utl_CastV2_InspectFrame(&frame_bytes, pChannel->plain, (uint32_t)pChannel->plainUsed);
        if (UTL_CASTV2_FRAME_INCOMPLETE != frame_state)
        {
            return 1;
        }
    }
    return 1;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
const char* CastChannel_GetError(const CastChannel* pChannel)
{
    return pChannel->error;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Connects TCP before allocating the channel and negotiating TLS. Handshake
 *  failure closes the socket and releases the partially initialized channel.
 */
CastChannel* CastChannel_Connect(const char* pHost, unsigned short port, char* pError, size_t errorCapacity)
{
    CastChannel* pChannel;
    SOCKET       s = ConnectTcp(pHost, port, pError, errorCapacity);

    if (INVALID_SOCKET == s)
    {
        return NULL;
    }
    pChannel = (CastChannel*)calloc(1, sizeof(*pChannel));
    if (NULL == pChannel)
    {
        closesocket(s);
        SetError(pError, errorCapacity, "out of memory", 0);
        return NULL;
    }
    pChannel->socket = s;
    if (!Handshake(pChannel, pHost, pError, errorCapacity))
    {
        CastChannel_Close(pChannel);
        return NULL;
    }
    return pChannel;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Encodes one Cast frame, splits it at the negotiated TLS record payload limit,
 *  then encrypts and sends each record in order.
 */
int CastChannel_Send(CastChannel* pChannel, const CastMessage_t* pMessage)
{
    const size_t frameLength = CastMessage_Encode(pMessage, pChannel->frame, sizeof(pChannel->frame));
    size_t       offset      = 0;

    if (0 == frameLength)
    {
        SetError(pChannel->error, sizeof(pChannel->error), "message does not fit one CASTV2 frame", 0);
        return 0;
    }
    for (;;)
    {
        size_t          chunkLength;
        SecBuffer       buffers[4];
        SecBufferDesc   desc;
        SECURITY_STATUS status;

        if (offset == frameLength)
        {
            break;
        }
        chunkLength = (frameLength - offset < pChannel->sizes.cbMaximumMessage) ? frameLength - offset : pChannel->sizes.cbMaximumMessage;
        if (0 == chunkLength)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "TLS record payload limit is zero", 0);
            return 0;
        }
        buffers[0].cbBuffer   = pChannel->sizes.cbHeader;
        buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
        buffers[0].pvBuffer   = pChannel->chunk;
        buffers[1].cbBuffer   = (unsigned long)chunkLength;
        buffers[1].BufferType = SECBUFFER_DATA;
        buffers[1].pvBuffer   = pChannel->chunk + pChannel->sizes.cbHeader;
        buffers[2].cbBuffer   = pChannel->sizes.cbTrailer;
        buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
        buffers[2].pvBuffer   = pChannel->chunk + pChannel->sizes.cbHeader + chunkLength;
        buffers[3].cbBuffer   = 0;
        buffers[3].BufferType = SECBUFFER_EMPTY;
        buffers[3].pvBuffer   = NULL;
        desc.ulVersion        = SECBUFFER_VERSION;
        desc.cBuffers         = 4;
        desc.pBuffers         = buffers;
        memcpy(buffers[1].pvBuffer, pChannel->frame + offset, chunkLength);
        status = EncryptMessage(&pChannel->context, 0, &desc, 0);
        if (SEC_E_OK != status)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "EncryptMessage failed", (unsigned long)status);
            return 0;
        }
        if (!SendAll(pChannel->socket, pChannel->chunk, buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer))
        {
            SetError(pChannel->error, sizeof(pChannel->error), "send failed", (unsigned long)WSAGetLastError());
            return 0;
        }
        offset += chunkLength;
    }
    return 1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Discards the previously borrowed frame on the next call and assembles a Cast
 *  message from buffered plaintext. Only newly decrypted plaintext retries the
 *  frame inspection immediately; an incomplete unchanged frame reaches the
 *  deadline and socket-read path instead of spinning on its existing header.
 */
int CastChannel_Receive(CastChannel* pChannel, CastMessage_t* pMessage, unsigned timeoutMs)
{
    const ULONGLONG deadline = GetTickCount64() + timeoutMs;

    /* Drop the frame handed out by the previous call. */
    if (pChannel->plainConsumed > 0)
    {
        memmove(pChannel->plain, pChannel->plain + pChannel->plainConsumed, pChannel->plainUsed - pChannel->plainConsumed);
        pChannel->plainUsed -= pChannel->plainConsumed;
        pChannel->plainConsumed = 0;
    }
    for (;;)
    {
        Utl_CastV2_FrameState_t frame_state;
        uint32_t               frame_bytes;
        uint32_t               previous_plain_bytes;
        ULONGLONG              now;
        struct timeval         timeout;
        fd_set                 readable;
        int                    ready;
        int                    received;
        bool_t                 has_new_plaintext;

        /* plainUsed is bounded by PLAINTEXT_CAPACITY at every append. */
        frame_state = Utl_CastV2_InspectFrame(&frame_bytes, pChannel->plain, (uint32_t)pChannel->plainUsed);
        if (UTL_CASTV2_FRAME_INVALID == frame_state)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "invalid CASTV2 frame length", 0);
            return -1;
        }
        if (UTL_CASTV2_FRAME_COMPLETE == frame_state)
        {
            if (!CastMessage_Decode(&pChannel->plain[UTL_CASTV2_HEADER_BYTES], frame_bytes - UTL_CASTV2_HEADER_BYTES, pMessage))
            {
                SetError(pChannel->error, sizeof(pChannel->error), "malformed CastMessage", 0);
                return -1;
            }
            pChannel->plainConsumed = frame_bytes;
            return 1;
        }
        previous_plain_bytes = (uint32_t)pChannel->plainUsed;
        if (!DecryptBuffered(pChannel))
        {
            return -1;
        }
        has_new_plaintext = (previous_plain_bytes < pChannel->plainUsed);
        if (has_new_plaintext)
        {
            continue; /* a frame may have completed */
        }
        now = GetTickCount64();
        if (now >= deadline)
        {
            return 0;
        }
        timeout.tv_sec  = (long)((deadline - now) / 1000);
        timeout.tv_usec = (long)((deadline - now) % 1000) * 1000;
        FD_ZERO(&readable);
        FD_SET(pChannel->socket, &readable);
        ready = select(0, &readable, NULL, NULL, &timeout);
        if (0 == ready)
        {
            return 0;
        }
        if (ready < 0)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "select failed", (unsigned long)WSAGetLastError());
            return -1;
        }
        if (pChannel->incomingUsed >= INCOMING_CAPACITY)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "TLS record too large", 0);
            return -1;
        }
        received = recv(pChannel->socket, (char*)pChannel->incoming + pChannel->incomingUsed, (int)(INCOMING_CAPACITY - pChannel->incomingUsed), 0);
        if (0 == received)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "the receiver closed the connection", 0);
            return -1;
        }
        if (received < 0)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "recv failed", (unsigned long)WSAGetLastError());
            return -1;
        }
        pChannel->incomingUsed += (size_t)received;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Releases only the Schannel handles recorded as initialized, then shuts down
 *  and closes the socket before freeing channel storage. NULL is accepted.
 */
void CastChannel_Close(CastChannel* pChannel)
{
    if (NULL == pChannel)
    {
        return;
    }
    if (pChannel->haveContext)
    {
        DeleteSecurityContext(&pChannel->context);
    }
    if (pChannel->haveCredentials)
    {
        FreeCredentialsHandle(&pChannel->credentials);
    }
    if (INVALID_SOCKET != pChannel->socket)
    {
        shutdown(pChannel->socket, SD_BOTH);
        closesocket(pChannel->socket);
    }
    free(pChannel);
}
