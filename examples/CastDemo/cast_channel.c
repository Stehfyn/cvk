/* cast_channel.c - Winsock transport, SChannel TLS client, CASTV2 framing. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#define SECURITY_WIN32
#include <security.h>
#include <schannel.h>

#include <stdlib.h>
#include <string.h>

#include "cast_channel.h"
#include "crt/crt_memory.h"
#include "crt/crt_format.h"
#include "os/os_assert.h"
#include "os/os_memory.h"

/* One TLS record is at most 16 KiB of payload plus header and trailer. */
#define INCOMING_CAPACITY  (32 * 1024)
#define PLAINTEXT_CAPACITY (UTL_CASTV2_MAX_FRAME + 16 * 1024)
#define CHUNK_CAPACITY     (20 * 1024)
#define HANDSHAKE_TOKEN_CAPACITY (64 * 1024)

struct CastChannel
{
    SOCKET                    socket;
    CredHandle                credentials;
    CtxtHandle                context;
    bool_t                    haveCredentials;
    bool_t                    haveContext;
    bool_t                    active;
    uint32_t                  maxHandshakeToken;
    PSecPkgInfoW              packageInfo;
    SecPkgContext_StreamSizes sizes;
    char                      error[256];
    /* ciphertext received but not yet decrypted */
    byte_t incoming[INCOMING_CAPACITY];
    size_t incomingUsed;
    /* decrypted stream, framed into CastMessages */
    byte_t plain[PLAINTEXT_CAPACITY];
    size_t plainUsed;
    size_t plainConsumed;
    /* one encoded frame and one encrypted record */
    byte_t frame[UTL_CASTV2_MAX_FRAME];
    byte_t chunk[CHUNK_CAPACITY];
    byte_t handshakeToken[HANDSHAKE_TOKEN_CAPACITY];
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
        (void)CRT_Format_Write(pError, (uint32_t)capacity, "%s (0x%08lX)", pWhat, code);
    }
    else
    {
        (void)CRT_Format_Copy(pError, (uint32_t)capacity, pWhat);
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
    const byte_t* pCursor = (const byte_t*)pData;

    for (;;)
    {
        int sent;

        if (0 == length)
        {
            break;
        }
        sent = send(s, (const char*)pCursor, (int)length, 0);

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
    struct addrinfo hints;
    struct addrinfo* pResults = NULL;
    struct addrinfo* pAddress;
    SOCKET           s = INVALID_SOCKET;
    char service[8];

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    (void)CRT_Format_Write(service, (uint32_t)sizeof(service), "%u", (unsigned)port);
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
 *  Publishes the negotiated TLS record limits only when the fixed record buffer
 *  can hold the complete header, maximum plaintext and trailer.
 */
static bool_t ReadStreamSizes(CastChannel* pChannel, char* pError, size_t errorCapacity)
{
    SECURITY_STATUS status;
    bool_t         has_header;
    bool_t         has_trailer;
    bool_t         has_payload;
    bool_t         ready;

    status = QueryContextAttributesW(&pChannel->context, SECPKG_ATTR_STREAM_SIZES, &pChannel->sizes);
    ready  = (SEC_E_OK == status);
    if (!ready)
    {
        SetError(pError, errorCapacity, "cannot query negotiated TLS record sizes", (unsigned long)status);
    }
    else
    {
        has_header = (pChannel->sizes.cbHeader <= sizeof(pChannel->chunk));
        has_trailer = has_header &&
                      (pChannel->sizes.cbTrailer <= (sizeof(pChannel->chunk) - pChannel->sizes.cbHeader));
        has_payload = has_trailer &&
                      (pChannel->sizes.cbMaximumMessage <=
                       (sizeof(pChannel->chunk) - pChannel->sizes.cbHeader - pChannel->sizes.cbTrailer));
        ready = has_payload;
    }
    if ((SEC_E_OK == status) && !ready)
    {
        SetError(pError, errorCapacity, "TLS record sizes exceed the channel buffers", (unsigned long)status);
    }

    return ready;
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
        ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION | ISC_REQ_USE_SUPPLIED_CREDS;
    SCHANNEL_CRED   credentials;
    TimeStamp       expiry;
    SECURITY_STATUS status;
    DWORD           granted;
    WCHAR           host[256];
    SecBuffer       outBuffer;
    SecBufferDesc   outDesc;
    bool_t          has_output;
    bool_t          has_bytes;
    bool_t          can_send;
    bool_t          was_sent;
    DWORD           send_error;

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

    outBuffer.cbBuffer   = pChannel->maxHandshakeToken;
    outBuffer.BufferType = SECBUFFER_TOKEN;
    outBuffer.pvBuffer   = pChannel->handshakeToken;
    outDesc.ulVersion    = SECBUFFER_VERSION;
    outDesc.cBuffers     = 1;
    outDesc.pBuffers     = &outBuffer;
    status               = InitializeSecurityContextW(&pChannel->credentials, NULL, host, requested, 0, 0, NULL, 0, &pChannel->context, &outDesc, &granted, NULL);
    if ((SEC_I_CONTINUE_NEEDED != status) && (SEC_E_OK != status))
    {
        SetError(pError, errorCapacity, "InitializeSecurityContext failed to start", (unsigned long)status);
        return 0;
    }
    pChannel->haveContext = 1;
    has_output = (pChannel->handshakeToken == outBuffer.pvBuffer);
    has_bytes  = 0 < outBuffer.cbBuffer;
    can_send   = has_output && (outBuffer.cbBuffer <= pChannel->maxHandshakeToken);
    was_sent   = can_send && (!has_bytes) && (SEC_E_OK == status);
    send_error = ERROR_INVALID_DATA;
    if (can_send && has_bytes)
    {
        was_sent = SendAll(pChannel->socket, outBuffer.pvBuffer, outBuffer.cbBuffer);
        if (!was_sent)
        {
            send_error = (DWORD)WSAGetLastError();
        }
    }
    if (!was_sent)
    {
        SetError(pError, errorCapacity, "cannot send the ClientHello", send_error);
        return 0;
    }
    if (SEC_E_OK == status)
    {
        return ReadStreamSizes(pChannel, pError, errorCapacity);
    }

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
            outBuffer.cbBuffer      = pChannel->maxHandshakeToken;
            outBuffer.BufferType    = SECBUFFER_TOKEN;
            outBuffer.pvBuffer      = pChannel->handshakeToken;
            status                  = InitializeSecurityContextW(&pChannel->credentials, &pChannel->context, NULL, requested, 0, 0, &inDesc, 0, NULL, &outDesc, &granted, NULL);
            if (SEC_E_INCOMPLETE_MESSAGE == status)
            {
                break; /* the record is not complete: receive more */
            }
            has_output = (pChannel->handshakeToken == outBuffer.pvBuffer);
            has_bytes = (0 < outBuffer.cbBuffer);
            can_send = has_output && (outBuffer.cbBuffer <= pChannel->maxHandshakeToken);
            if (!can_send)
            {
                SetError(pError, errorCapacity, "TLS handshake token exceeds startup storage", 0);
                return 0;
            }
            if (has_bytes)
            {
                const int sent = SendAll(pChannel->socket, outBuffer.pvBuffer, outBuffer.cbBuffer);

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
                return ReadStreamSizes(pChannel, pError, errorCapacity);
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
        const byte_t*           pExtra;
        uint32_t                extraLength;
        SecBuffer               buffers[4];
        int                     index;
        SecBufferDesc           desc;
        SECURITY_STATUS         status;
        bool_t                  has_consumed_record;
        Utl_CastV2_FrameState_t frame_state;
        uint32_t                frame_bytes;

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
                pExtra      = (const byte_t*)buffers[index].pvBuffer;
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
 *  Reserves and locks the channel buffers before any connection is attempted.
 */
bool_t CastChannel_Initialize(CastChannel_Workspace_t* workspace)
{
    bool_t ready;
    SECURITY_STATUS queryStatus;
    SECURITY_STATUS releaseStatus;
    PSecPkgInfoW packageInfo;
    bool_t validPackage;
    bool_t validCapacity;

    LOGASSERT(NULL != workspace);

    workspace->channel = NULL;
    ready = OS_Memory_Initialize(&workspace->memory, sizeof(CastChannel));
    if (ready)
    {
        workspace->channel = (CastChannel*)OS_Memory_GetAddress(&workspace->memory);
        workspace->channel->socket = INVALID_SOCKET;
        packageInfo = NULL;
        queryStatus = QuerySecurityPackageInfoW(UNISP_NAME_W, &packageInfo);
        validPackage = (SEC_E_OK == queryStatus) && (NULL != packageInfo);
        validCapacity = FALSE;
        if (validPackage)
        {
            workspace->channel->maxHandshakeToken = packageInfo->cbMaxToken;
            validCapacity = (0 < packageInfo->cbMaxToken) &&
                            (packageInfo->cbMaxToken <= HANDSHAKE_TOKEN_CAPACITY);
        }
        workspace->channel->packageInfo = packageInfo;
        releaseStatus = SEC_E_OK;
        if (NULL != packageInfo)
        {
            releaseStatus = FreeContextBuffer(packageInfo);
            if (SEC_E_OK == releaseStatus)
            {
                workspace->channel->packageInfo = NULL;
            }
        }
        ready = validCapacity && (SEC_E_OK == releaseStatus);
    }

    return ready;
}

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
 *  Reuses startup-owned channel storage for each connection attempt.
 */
CastChannel* CastChannel_Connect(CastChannel_Workspace_t* workspace,
                                 const char*              pHost,
                                 unsigned short           port,
                                 char*                    pError,
                                 size_t                   errorCapacity)
{
    CastChannel* pChannel;
    SOCKET       s;
    bool_t       resources_closed;
    uint32_t     maxHandshakeToken;

    LOGASSERT(NULL != workspace);
    LOGASSERT(NULL != workspace->channel);

    pChannel = workspace->channel;
    LOGASSERT(!pChannel->active);
    CastChannel_Close(pChannel);
    resources_closed = (INVALID_SOCKET == pChannel->socket) &&
                       (!pChannel->haveContext) &&
                       (!pChannel->haveCredentials);
    if (!resources_closed)
    {
        SetError(pError, errorCapacity, "previous channel cleanup remains pending", 0);
        return NULL;
    }
    maxHandshakeToken = pChannel->maxHandshakeToken;
    LOGASSERT(0 < maxHandshakeToken);
    LOGASSERT(maxHandshakeToken <= HANDSHAKE_TOKEN_CAPACITY);
    CRT_Memory_Set(pChannel, 0, sizeof(*pChannel));
    pChannel->maxHandshakeToken = maxHandshakeToken;
    pChannel->socket = INVALID_SOCKET;
    s = ConnectTcp(pHost, port, pError, errorCapacity);

    if (INVALID_SOCKET == s)
    {
        return NULL;
    }
    pChannel->socket = s;
    if (!Handshake(pChannel, pHost, pError, errorCapacity))
    {
        CastChannel_Close(pChannel);
        return NULL;
    }
    pChannel->active = TRUE;
    return pChannel;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Encodes one Cast frame, splits it at the negotiated TLS record payload limit,
 *  then encrypts and sends each record in order.
 */
int CastChannel_Send(CastChannel* pChannel, const Utl_CastV2_Message_t* pMessage)
{
    const size_t frameLength = Utl_CastV2_WriteFrame(pMessage, pChannel->frame, sizeof(pChannel->frame));
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
        size_t          recordLength;
        bool_t          hasRecordCapacity;
        bool_t          sent;

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
        CRT_Memory_Copy(pChannel->chunk + pChannel->sizes.cbHeader, pChannel->frame + offset, chunkLength);
        status = EncryptMessage(&pChannel->context, 0, &desc, 0);
        if (SEC_E_OK != status)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "EncryptMessage failed", (unsigned long)status);
            return 0;
        }
        recordLength = (size_t)buffers[0].cbBuffer + (size_t)buffers[1].cbBuffer + (size_t)buffers[2].cbBuffer;
        hasRecordCapacity = recordLength <= sizeof(pChannel->chunk);
        sent = FALSE;
        if (hasRecordCapacity)
        {
            sent = SendAll(pChannel->socket, pChannel->chunk, recordLength);
        }
        if (!sent)
        {
            if (hasRecordCapacity)
            {
                SetError(pChannel->error, sizeof(pChannel->error), "send failed", (unsigned long)WSAGetLastError());
            }
            else
            {
                SetError(pChannel->error, sizeof(pChannel->error), "TLS record exceeds the channel buffer", 0);
            }
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
int CastChannel_Receive(CastChannel* pChannel, Utl_CastV2_Message_t* pMessage, unsigned timeoutMs)
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
        uint32_t                frame_bytes;
        uint32_t                previous_plain_bytes;
        ULONGLONG               now;
        struct timeval          timeout;
        fd_set                  readable;
        int                     ready;
        int                     received;
        bool_t                  has_new_plaintext;

        /* plainUsed is bounded by PLAINTEXT_CAPACITY at every append. */
        frame_state = Utl_CastV2_InspectFrame(&frame_bytes, pChannel->plain, (uint32_t)pChannel->plainUsed);
        if (UTL_CASTV2_FRAME_INVALID == frame_state)
        {
            SetError(pChannel->error, sizeof(pChannel->error), "invalid CASTV2 frame length", 0);
            return -1;
        }
        if (UTL_CASTV2_FRAME_COMPLETE == frame_state)
        {
            if (!Utl_CastV2_ReadMessage(pMessage,
                                      &pChannel->plain[UTL_CASTV2_HEADER_BYTES],
                                      frame_bytes - UTL_CASTV2_HEADER_BYTES))
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
 *  Retries the Schannel handles and socket recorded as live. Failed native
 *  cleanup retains ownership in startup storage for the next attempt.
 */
void CastChannel_Close(CastChannel* pChannel)
{
    SECURITY_STATUS security_status;
    int32_t         socket_status;

    if (NULL == pChannel)
    {
        return;
    }
    pChannel->active = FALSE;
    if (pChannel->haveContext)
    {
        security_status = DeleteSecurityContext(&pChannel->context);
        if (SEC_E_OK == security_status)
        {
            pChannel->haveContext = FALSE;
        }
    }
    if ((!pChannel->haveContext) && pChannel->haveCredentials)
    {
        security_status = FreeCredentialsHandle(&pChannel->credentials);
        if (SEC_E_OK == security_status)
        {
            pChannel->haveCredentials = FALSE;
        }
    }
    if (INVALID_SOCKET != pChannel->socket)
    {
        (void)shutdown(pChannel->socket, SD_BOTH);
        socket_status = closesocket(pChannel->socket);
        if (0 == socket_status)
        {
            pChannel->socket = INVALID_SOCKET;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Native cleanup can be retried while the caller retains the workspace.
 */
bool_t CastChannel_TryClose(CastChannel_Workspace_t* workspace)
{
    bool_t resources_closed;
    bool_t closed;
    SECURITY_STATUS releaseStatus;

    LOGASSERT(NULL != workspace);

    resources_closed = (NULL == workspace->channel);
    if (!resources_closed)
    {
        LOGASSERT(!workspace->channel->active);
        CastChannel_Close(workspace->channel);
        resources_closed = (INVALID_SOCKET == workspace->channel->socket) &&
                           (!workspace->channel->haveContext) &&
                           (!workspace->channel->haveCredentials);
        if (resources_closed && (NULL != workspace->channel->packageInfo))
        {
            releaseStatus = FreeContextBuffer(workspace->channel->packageInfo);
            if (SEC_E_OK == releaseStatus)
            {
                workspace->channel->packageInfo = NULL;
            }
            resources_closed = (NULL == workspace->channel->packageInfo);
        }
    }
    closed = FALSE;
    if (resources_closed)
    {
        closed = OS_Memory_TryClose(&workspace->memory);
    }
    if (closed)
    {
        workspace->channel = NULL;
    }

    return closed;
}
