/* cast_session.c - the CASTV2 conversation with the default media receiver (CC1AD845).
**
**   CONNECT receiver-0, LAUNCH CC1AD845
**   RECEIVER_STATUS names the application's transportId
**   CONNECT transportId; the application announces MEDIA_STATUS to the new sender
**   LOAD the URL; MEDIA_STATUS follows every change and every GET_STATUS
**   PING/PONG keep the channel alive; STOP closes the application at the end
**
** Receiver replies are JSON read with the cvk JSON reader. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cast_channel.h"
#include "cast_session.h"
#include "cvk_json.h"

#define NS_CONNECTION "urn:x-cast:com.google.cast.tp.connection"
#define NS_HEARTBEAT  "urn:x-cast:com.google.cast.tp.heartbeat"
#define NS_RECEIVER   "urn:x-cast:com.google.cast.receiver"
#define NS_MEDIA      "urn:x-cast:com.google.cast.media"
#define NS_PREFIX_LEN (sizeof("urn:x-cast:com.google.cast.") - 1)

#define DEFAULT_MEDIA_RECEIVER "CC1AD845"
#define SENDER_ID              "sender-0"
#define RECEIVER_ID            "receiver-0"
#define HEARTBEAT_MS           (5000)
#define STATUS_POLL_MS         (1000)
#define LOAD_FALLBACK_MS       (3000)
#define RECEIVE_SLICE_MS       (100)
#define JSON_VALUE_CAPACITY    (4096)
#define COMMAND_QUEUE_LENGTH   (8)

typedef struct QueuedCommand
{
    CastCommand command;
    double      seconds;
} QueuedCommand;

struct CastSession
{
    CastSessionConfig config;
    CastChannel*      pChannel;
    int               requestId;
    char              sessionId[128];   /* the receiver application instance */
    char              transportId[128]; /* where media messages go */
    int               joined;
    int               loadPending;  /* LOAD waits for the application's first MEDIA_STATUS */
    ULONGLONG         loadDeadline; /* ... or for this tick count, whichever comes first */
    int64_t           mediaSessionId;
    CastStatus        status;
    int               finished;
    CRITICAL_SECTION  lock;
    QueuedCommand     queue[COMMAND_QUEUE_LENGTH];
    unsigned          queueHead;
    unsigned          queueCount;
};

static void Log(CastSession* pSession, const char* pFormat, ...)
{
    char    text[1600];
    va_list arguments;

    va_start(arguments, pFormat);
    (void)vsnprintf(text, sizeof(text), pFormat, arguments);
    va_end(arguments);
    pSession->config.callback(pSession->config.pUser, CAST_EVENT_LOG, NULL, text);
}

static void EmitStatus(CastSession* pSession)
{
    pSession->config.callback(pSession->config.pUser, CAST_EVENT_STATUS, &pSession->status, NULL);
}

static void SetPhase(CastSession* pSession, CastPlayerState state, const char* pName)
{
    pSession->status.state = state;
    (void)snprintf(pSession->status.stateName, sizeof(pSession->status.stateName), "%s", pName);
    EmitStatus(pSession);
}

static int SendJson(CastSession* pSession, const char* pNamespace, const char* pDestination, const char* pPayload)
{
    CastMessage message;

    CastMessage_Set(&message, SENDER_ID, pDestination, pNamespace, pPayload);
    if (pSession->config.verbose)
    {
        Log(pSession, ">> %s -> %s %s", pNamespace + NS_PREFIX_LEN, pDestination, pPayload);
    }
    if (!CastChannel_Send(pSession->pChannel, &message))
    {
        Log(pSession, "cast: %s", CastChannel_GetError(pSession->pChannel));
        return 0;
    }
    return 1;
}

/* Escapes a string for inclusion inside a JSON string literal. */
static void EscapeJson(const char* pText, char* pOut, size_t capacity)
{
    size_t used = 0;

    for (; *pText && used + 7 < capacity; ++pText)
    {
        const unsigned char c = (unsigned char)*pText;

        if ('"' == c || '\\' == c)
        {
            pOut[used++] = '\\';
            pOut[used++] = (char)c;
        }
        else if (c < 0x20)
        {
            used += (size_t)snprintf(pOut + used, capacity - used, "\\u%04X", c);
        }
        else
        {
            pOut[used++] = (char)c;
        }
    }
    pOut[used] = 0;
}

static int Load(CastSession* pSession)
{
    char payload[3000];
    char title[1100];

    EscapeJson(pSession->config.title, title, sizeof(title));
    (void)snprintf(payload, sizeof(payload),
                   "{\"type\":\"LOAD\",\"requestId\":%d,\"sessionId\":\"%s\",\"autoplay\":true,\"currentTime\":0,"
                   "\"media\":{\"contentId\":\"%s\",\"contentType\":\"%s\",\"streamType\":\"BUFFERED\","
                   "\"metadata\":{\"metadataType\":0,\"title\":\"%s\"}}}",
                   ++pSession->requestId, pSession->sessionId, pSession->config.contentUrl, pSession->config.contentType, title);
    Log(pSession, "loading %s", pSession->config.contentUrl);
    SetPhase(pSession, CAST_STATE_LOADING, "LOADING");
    return SendJson(pSession, NS_MEDIA, pSession->transportId, payload);
}

static int SendMediaCommand(CastSession* pSession, const char* pType)
{
    char payload[256];

    if (0 == pSession->mediaSessionId || 0 == pSession->transportId[0])
    {
        return 1;
    }
    (void)snprintf(payload, sizeof(payload), "{\"type\":\"%s\",\"requestId\":%d,\"mediaSessionId\":%lld}", pType, ++pSession->requestId, (long long)pSession->mediaSessionId);
    return SendJson(pSession, NS_MEDIA, pSession->transportId, payload);
}

static int Seek(CastSession* pSession, double seconds)
{
    char payload[256];

    if (0 == pSession->mediaSessionId || 0 == pSession->transportId[0])
    {
        return 1;
    }
    if (seconds < 0)
    {
        seconds = 0;
    }
    (void)snprintf(payload, sizeof(payload), "{\"type\":\"SEEK\",\"requestId\":%d,\"mediaSessionId\":%lld,\"currentTime\":%.3f}", ++pSession->requestId,
                   (long long)pSession->mediaSessionId, seconds);
    return SendJson(pSession, NS_MEDIA, pSession->transportId, payload);
}

static void CopyJsonString(const cvkJsonValue* pValue, char* pOut, size_t capacity)
{
    size_t length = (NULL != pValue && CVK_JSON_STRING == pValue->kind) ? pValue->textLength : 0;

    if (length >= capacity)
    {
        length = capacity - 1;
    }
    if (length > 0)
    {
        memcpy(pOut, pValue->pText, length);
    }
    pOut[length] = 0;
}

static int OnReceiverStatus(CastSession* pSession, const cvkJsonDocument* pDocument, const cvkJsonValue* pRoot)
{
    const cvkJsonValue* pStatus       = cvkGetJsonMember(pDocument, pRoot, "status");
    const cvkJsonValue* pApplications = cvkGetJsonMember(pDocument, pStatus, "applications");
    const cvkJsonValue* pApplication  = NULL;
    uint32_t            index;

    for (index = 0; NULL != pApplications && index < pApplications->childCount; ++index)
    {
        const cvkJsonValue* pCandidate = cvkGetJsonElement(pDocument, pApplications, index);

        if (cvkJsonStringEquals(cvkGetJsonMember(pDocument, pCandidate, "appId"), DEFAULT_MEDIA_RECEIVER))
        {
            pApplication = pCandidate;
            break;
        }
    }
    if (NULL == pApplication)
    {
        if (pSession->joined)
        {
            Log(pSession, "the media receiver was closed on the device");
            return 0;
        }
        return 1; /* still launching */
    }
    if (!pSession->joined)
    {
        char statusText[128];

        CopyJsonString(cvkGetJsonMember(pDocument, pApplication, "sessionId"), pSession->sessionId, sizeof(pSession->sessionId));
        CopyJsonString(cvkGetJsonMember(pDocument, pApplication, "transportId"), pSession->transportId, sizeof(pSession->transportId));
        CopyJsonString(cvkGetJsonMember(pDocument, pApplication, "statusText"), statusText, sizeof(statusText));
        pSession->joined = 1;
        Log(pSession, "media receiver running: %s (session %s)", statusText, pSession->sessionId);
        /* A second virtual connection, to the application. It announces its media state to
        ** the new sender once it is ready; the LOAD waits for that (or a short fallback). */
        pSession->loadPending  = 1;
        pSession->loadDeadline = GetTickCount64() + LOAD_FALLBACK_MS;
        return SendJson(pSession, NS_CONNECTION, pSession->transportId, "{\"type\":\"CONNECT\"}");
    }
    return 1;
}

static CastPlayerState StateFromName(const char* pName, int loading)
{
    if (0 == strcmp(pName, "PLAYING"))
    {
        return CAST_STATE_PLAYING;
    }
    if (0 == strcmp(pName, "PAUSED"))
    {
        return CAST_STATE_PAUSED;
    }
    if (0 == strcmp(pName, "BUFFERING"))
    {
        return CAST_STATE_BUFFERING;
    }
    return loading ? CAST_STATE_LOADING : CAST_STATE_IDLE;
}

static int OnMediaStatus(CastSession* pSession, const cvkJsonDocument* pDocument, const cvkJsonValue* pRoot)
{
    const cvkJsonValue* pList   = cvkGetJsonMember(pDocument, pRoot, "status");
    const cvkJsonValue* pStatus = cvkGetJsonElement(pDocument, pList, 0);
    const cvkJsonValue* pDuration;
    const cvkJsonValue* pExtended;
    char                name[32];
    char                idleReason[32];
    char                extendedName[32];
    int                 changed;

    if (pSession->loadPending)
    {
        pSession->loadPending = 0;
        return Load(pSession);
    }
    if (NULL == pStatus)
    {
        return 1;
    }
    pSession->mediaSessionId  = cvkGetJsonInt64(cvkGetJsonMember(pDocument, pStatus, "mediaSessionId"));
    pSession->status.hasMedia = (0 != pSession->mediaSessionId);
    pSession->status.currentTime = cvkGetJsonDouble(cvkGetJsonMember(pDocument, pStatus, "currentTime"));
    pDuration                    = cvkGetJsonMember(pDocument, cvkGetJsonMember(pDocument, pStatus, "media"), "duration");
    if (NULL != pDuration && CVK_JSON_NUMBER == pDuration->kind)
    {
        pSession->status.duration = cvkGetJsonDouble(pDuration);
    }
    CopyJsonString(cvkGetJsonMember(pDocument, pStatus, "playerState"), name, sizeof(name));
    CopyJsonString(cvkGetJsonMember(pDocument, pStatus, "idleReason"), idleReason, sizeof(idleReason));
    /* While the receiver fetches the media it stays IDLE and reports LOADING in extendedStatus. */
    pExtended = cvkGetJsonMember(pDocument, pStatus, "extendedStatus");
    CopyJsonString(cvkGetJsonMember(pDocument, pExtended, "playerState"), extendedName, sizeof(extendedName));
    if (0 == strcmp(name, "IDLE") && 0 == strcmp(extendedName, "LOADING"))
    {
        (void)snprintf(name, sizeof(name), "LOADING");
    }
    changed = (0 != strcmp(name, pSession->status.stateName));
    (void)snprintf(pSession->status.stateName, sizeof(pSession->status.stateName), "%s", name);
    pSession->status.state = StateFromName(name, 0 == strcmp(name, "LOADING"));
    if (changed && idleReason[0])
    {
        Log(pSession, "%s %s", name, idleReason);
    }
    EmitStatus(pSession);
    if (0 == strcmp(name, "IDLE") && 0 == strcmp(idleReason, "FINISHED"))
    {
        if (pSession->config.loop)
        {
            return Load(pSession);
        }
        pSession->finished = 1;
        return 0;
    }
    if (0 == strcmp(name, "IDLE") && 0 == strcmp(idleReason, "ERROR"))
    {
        Log(pSession, "the receiver could not play the media");
        return 0;
    }
    return 1;
}

static void ExplainMediaError(CastSession* pSession, const cvkJsonDocument* pDocument, const cvkJsonValue* pRoot, const char* pType)
{
    const cvkJsonValue* pCode = cvkGetJsonMember(pDocument, pRoot, "detailedErrorCode");
    char                reason[128];

    CopyJsonString(cvkGetJsonMember(pDocument, pRoot, "reason"), reason, sizeof(reason));
    if (NULL != pCode)
    {
        Log(pSession, "%s%s%s (detailedErrorCode %lld)", pType, reason[0] ? ": " : "", reason, (long long)cvkGetJsonInt64(pCode));
    }
    else
    {
        Log(pSession, "%s%s%s", pType, reason[0] ? ": " : "", reason);
    }
    if (pSession->config.servingLocally)
    {
        Log(pSession,
            "The receiver fetches %s from this PC. If Windows Defender Firewall blocked the connection, "
            "allow the program on this network or add an inbound rule for that TCP port.",
            pSession->config.contentUrl);
    }
}

/* Returns 0 when the session is over. */
static int HandleMessage(CastSession* pSession, const CastMessage* pMessage)
{
    static cvkJsonValue values[JSON_VALUE_CAPACITY];
    cvkJsonDocument     document;
    const cvkJsonValue* pRoot;
    const cvkJsonValue* pType;

    if (pSession->config.verbose)
    {
        const size_t skip = (pMessage->namespaceLength > NS_PREFIX_LEN) ? NS_PREFIX_LEN : 0;

        Log(pSession, "<< %.*s %.*s", (int)(pMessage->namespaceLength - skip), pMessage->pNamespace + skip, (int)pMessage->payloadLength, pMessage->pPayload);
    }
    if (pMessage->binary || 0 == pMessage->payloadLength || '{' != pMessage->pPayload[0])
    {
        return 1;
    }
    document.valueCount = cvkParseJsonDocument(pMessage->pPayload, pMessage->payloadLength, values, JSON_VALUE_CAPACITY);
    document.pValues    = values;
    pRoot               = &values[0];
    pType               = cvkGetJsonMember(&document, pRoot, "type");
    if (CastMessage_NamespaceIs(pMessage, NS_HEARTBEAT))
    {
        if (cvkJsonStringEquals(pType, "PING"))
        {
            return SendJson(pSession, NS_HEARTBEAT, RECEIVER_ID, "{\"type\":\"PONG\"}");
        }
        return 1;
    }
    if (CastMessage_NamespaceIs(pMessage, NS_CONNECTION))
    {
        if (cvkJsonStringEquals(pType, "CLOSE"))
        {
            Log(pSession, "the receiver closed the connection");
            return 0;
        }
        return 1;
    }
    if (CastMessage_NamespaceIs(pMessage, NS_RECEIVER))
    {
        if (cvkJsonStringEquals(pType, "RECEIVER_STATUS"))
        {
            return OnReceiverStatus(pSession, &document, pRoot);
        }
        if (cvkJsonStringEquals(pType, "LAUNCH_ERROR"))
        {
            char reason[128];

            CopyJsonString(cvkGetJsonMember(&document, pRoot, "reason"), reason, sizeof(reason));
            Log(pSession, "the receiver refused to launch the media receiver: %s", reason);
            return 0;
        }
        return 1;
    }
    if (CastMessage_NamespaceIs(pMessage, NS_MEDIA))
    {
        if (cvkJsonStringEquals(pType, "MEDIA_STATUS"))
        {
            return OnMediaStatus(pSession, &document, pRoot);
        }
        if (cvkJsonStringEquals(pType, "LOAD_FAILED") || cvkJsonStringEquals(pType, "LOAD_CANCELLED") || cvkJsonStringEquals(pType, "INVALID_REQUEST") ||
            cvkJsonStringEquals(pType, "ERROR"))
        {
            char type[32];

            CopyJsonString(pType, type, sizeof(type));
            ExplainMediaError(pSession, &document, pRoot, type);
            return 0;
        }
        return 1;
    }
    return 1;
}

/* Closes the receiver application so the device returns to its home screen. */
static void Leave(CastSession* pSession)
{
    char      payload[256];
    ULONGLONG deadline;

    if (0 == pSession->sessionId[0] || NULL == pSession->pChannel)
    {
        return;
    }
    (void)snprintf(payload, sizeof(payload), "{\"type\":\"STOP\",\"requestId\":%d,\"sessionId\":\"%s\"}", ++pSession->requestId, pSession->sessionId);
    if (!SendJson(pSession, NS_RECEIVER, RECEIVER_ID, payload))
    {
        return;
    }
    deadline = GetTickCount64() + 1000;
    while (GetTickCount64() < deadline)
    {
        CastMessage message;

        if (CastChannel_Receive(pSession->pChannel, &message, 200) < 0)
        {
            break;
        }
    }
}

/* Applies queued commands. Returns 0 when a STOP was requested, -1 when sending failed. */
static int ProcessCommands(CastSession* pSession)
{
    for (;;)
    {
        QueuedCommand command;
        int           ok = 1;

        EnterCriticalSection(&pSession->lock);
        if (0 == pSession->queueCount)
        {
            LeaveCriticalSection(&pSession->lock);
            return 1;
        }
        command            = pSession->queue[pSession->queueHead];
        pSession->queueHead = (pSession->queueHead + 1) % COMMAND_QUEUE_LENGTH;
        --pSession->queueCount;
        LeaveCriticalSection(&pSession->lock);
        switch (command.command)
        {
        case CAST_COMMAND_PLAY:
            ok = SendMediaCommand(pSession, "PLAY");
            break;
        case CAST_COMMAND_PAUSE:
            ok = SendMediaCommand(pSession, "PAUSE");
            break;
        case CAST_COMMAND_TOGGLE:
            ok = SendMediaCommand(pSession, (CAST_STATE_PLAYING == pSession->status.state || CAST_STATE_BUFFERING == pSession->status.state) ? "PAUSE" : "PLAY");
            break;
        case CAST_COMMAND_SEEK:
            ok = Seek(pSession, command.seconds);
            break;
        case CAST_COMMAND_STOP:
            return 0;
        }
        if (!ok)
        {
            return -1;
        }
    }
}

CastSession* CastSession_Create(const CastSessionConfig* pConfig)
{
    CastSession* pSession = (CastSession*)calloc(1, sizeof(*pSession));

    if (NULL == pSession)
    {
        return NULL;
    }
    pSession->config = *pConfig;
    InitializeCriticalSection(&pSession->lock);
    return pSession;
}

int CastSession_Run(CastSession* pSession)
{
    char        error[256];
    char        payload[128];
    ULONGLONG   lastHeartbeat;
    ULONGLONG   lastPoll;
    const char* pReason = NULL;
    int         result  = 1;

    SetPhase(pSession, CAST_STATE_CONNECTING, "CONNECTING");
    pSession->pChannel = CastChannel_Connect(pSession->config.host, CAST_PORT, error, sizeof(error));
    if (NULL == pSession->pChannel)
    {
        Log(pSession, "cast: %s", error);
        SetPhase(pSession, CAST_STATE_ENDED, "ENDED");
        pSession->config.callback(pSession->config.pUser, CAST_EVENT_ENDED, NULL, error);
        return 1;
    }
    Log(pSession, "connected to %s:%u over TLS", pSession->config.host, (unsigned)CAST_PORT);
    (void)snprintf(payload, sizeof(payload), "{\"type\":\"LAUNCH\",\"requestId\":%d,\"appId\":\"%s\"}", ++pSession->requestId, DEFAULT_MEDIA_RECEIVER);
    if (!SendJson(pSession, NS_CONNECTION, RECEIVER_ID, "{\"type\":\"CONNECT\"}") || !SendJson(pSession, NS_RECEIVER, RECEIVER_ID, payload))
    {
        pReason = CastChannel_GetError(pSession->pChannel);
        goto done;
    }
    Log(pSession, "launching the default media receiver");
    SetPhase(pSession, CAST_STATE_LAUNCHING, "LAUNCHING");
    lastHeartbeat = GetTickCount64();
    lastPoll      = lastHeartbeat;
    for (;;)
    {
        CastMessage message;
        const int   received = CastChannel_Receive(pSession->pChannel, &message, RECEIVE_SLICE_MS);
        ULONGLONG   now;
        int         commands;

        if (received < 0)
        {
            pReason = CastChannel_GetError(pSession->pChannel);
            Log(pSession, "cast: %s", pReason);
            goto done;
        }
        if (received > 0 && !HandleMessage(pSession, &message))
        {
            if (pSession->finished)
            {
                result  = 0;
                pReason = "playback finished";
            }
            else
            {
                pReason = "the receiver ended the session";
            }
            break;
        }
        now = GetTickCount64();
        if (pSession->loadPending && now >= pSession->loadDeadline)
        {
            pSession->loadPending = 0;
            if (!Load(pSession))
            {
                pReason = CastChannel_GetError(pSession->pChannel);
                goto done;
            }
        }
        if (now - lastHeartbeat >= HEARTBEAT_MS)
        {
            lastHeartbeat = now;
            if (!SendJson(pSession, NS_HEARTBEAT, RECEIVER_ID, "{\"type\":\"PING\"}"))
            {
                pReason = CastChannel_GetError(pSession->pChannel);
                goto done;
            }
        }
        if (0 != pSession->mediaSessionId && now - lastPoll >= STATUS_POLL_MS)
        {
            lastPoll = now;
            if (!SendMediaCommand(pSession, "GET_STATUS"))
            {
                pReason = CastChannel_GetError(pSession->pChannel);
                goto done;
            }
        }
        commands = ProcessCommands(pSession);
        if (commands < 0)
        {
            pReason = CastChannel_GetError(pSession->pChannel);
            goto done;
        }
        if (0 == commands)
        {
            result  = 0;
            pReason = "stopped";
            break;
        }
    }
    Leave(pSession);
done:
    CastChannel_Close(pSession->pChannel);
    pSession->pChannel        = NULL;
    pSession->status.hasMedia = 0;
    SetPhase(pSession, CAST_STATE_ENDED, "ENDED");
    pSession->config.callback(pSession->config.pUser, CAST_EVENT_ENDED, NULL, pReason);
    return result;
}

void CastSession_Post(CastSession* pSession, CastCommand command, double seconds)
{
    EnterCriticalSection(&pSession->lock);
    if (pSession->queueCount < COMMAND_QUEUE_LENGTH)
    {
        const unsigned tail = (pSession->queueHead + pSession->queueCount) % COMMAND_QUEUE_LENGTH;

        pSession->queue[tail].command = command;
        pSession->queue[tail].seconds = seconds;
        ++pSession->queueCount;
    }
    LeaveCriticalSection(&pSession->lock);
}

void CastSession_Destroy(CastSession* pSession)
{
    if (NULL == pSession)
    {
        return;
    }
    CastChannel_Close(pSession->pChannel);
    DeleteCriticalSection(&pSession->lock);
    free(pSession);
}
