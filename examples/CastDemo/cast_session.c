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
#include "mp4_index.h"

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
#define STATUS_POLL_FAST_MS    (60) /* while a seek settles */
#define LOAD_FALLBACK_MS       (3000)
#define RECEIVE_SLICE_MS       (5)    /* command pickup latency while waiting for the receiver */
#define SEEK_INTERVAL_MS       (60)   /* newest scrub position goes out at most this often */
#define SEEK_CONFIRM_MS        (2000) /* hold the target until the receiver reports near it */
#define SEEK_CONFIRM_TOLERANCE (1.5)
#define SCRUB_TO_PROXY_MS      (140)  /* sustained scrub before dropping to the low-res proxy */
#define SETTLE_MS              (1400) /* quiet time before upgrading back to full quality */
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
    /* scrubbing: coalesce queued seeks, rate-limit sends, mask stale positions */
    int       seekQueued;
    double    seekQueuedSeconds;
    int       seekQueuedResume; /* resume playback after the queued seek */
    ULONGLONG lastSeekTick;
    int       seekPending;
    double    seekTarget;
    ULONGLONG seekPendingSince;
    int       wantPlayAfterSeek; /* a resuming seek went out: force PLAY if it lands paused */
    ULONGLONG playAfterSeekSince;
    int       scrubbing; /* between the first SCRUB and the SEEK that ends the drag */
    int       playingBeforeScrub;
    ULONGLONG resumeSince; /* a resuming seek went out: time until PLAYING is logged */
    /* real-resume measurement: time until the picture actually advances past the target, which
    ** is what the eye sees, not the receiver's optimistic PLAYING report */
    int       measurePending;
    double    measureTarget;
    ULONGLONG measureSince;
    int       measureRebuffered;
    /* adaptive quality: scrub on a low-res proxy for instant response, settle back to full
    ** quality with hysteresis. Proxy and full stream share one timeline (same content). */
    char             proxyUrl[1200];   /* guarded by lock (set from the transcode thread) */
    char             proxyType[64];    /* guarded by lock */
    int              haveProxy;        /* guarded by lock */
    int              onProxy;          /* the currently loaded stream is the proxy */
    int              switchedToProxy;  /* the one-time switch from the full stream to the proxy is done */
    ULONGLONG        scrubRunSince;    /* first scrub of the current drag, 0 when not dragging */
    ULONGLONG        lastActivityTick; /* newest scrub or seek */
    int              settlePending;    /* on the proxy after a drag; upgrade when settled */
    int              playingWanted;    /* whether playback should be running after a stream switch */
    int              awaitingLoad;     /* a LOAD is in flight; the old mediaSessionId is dead until it lands */
    int64_t          preLoadSessionId; /* the mediaSessionId before that LOAD, to detect the new one */
    CRITICAL_SECTION lock;
    QueuedCommand    queue[COMMAND_QUEUE_LENGTH];
    unsigned         queueHead;
    unsigned         queueCount;
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

/* streamType selects the receiver's buffering profile: BUFFERED is the deep smooth-VOD buffer
** (slow to fill after a seek), LIVE is the shallow low-latency buffer (fills fast, ideal for the
** scrub proxy). We control it, so we pick per stream. */
static int LoadMedia(CastSession* pSession, const char* pUrl, const char* pType, double startSeconds, int autoplay, const char* pStreamType)
{
    char payload[3000];
    char title[1100];

    if (startSeconds < 0)
    {
        startSeconds = 0;
    }
    EscapeJson(pSession->config.title, title, sizeof(title));
    (void)snprintf(payload,
                   sizeof(payload),
                   "{\"type\":\"LOAD\",\"requestId\":%d,\"sessionId\":\"%s\",\"autoplay\":%s,\"currentTime\":%.3f,"
                   "\"media\":{\"contentId\":\"%s\",\"contentType\":\"%s\",\"streamType\":\"%s\","
                   "\"metadata\":{\"metadataType\":0,\"title\":\"%s\"}}}",
                   ++pSession->requestId,
                   pSession->sessionId,
                   autoplay ? "true" : "false",
                   startSeconds,
                   pUrl,
                   pType,
                   pStreamType,
                   title);
    SetPhase(pSession, CAST_STATE_LOADING, "LOADING");
    pSession->playingWanted    = autoplay;
    pSession->awaitingLoad     = 1; /* the media session id changes; hold seeks until the new one lands */
    pSession->preLoadSessionId = pSession->mediaSessionId;
    pSession->seekPending      = 0;
    return SendJson(pSession, NS_MEDIA, pSession->transportId, payload);
}

static int Load(CastSession* pSession)
{
    Log(pSession, "loading %s", pSession->config.contentUrl);
    pSession->onProxy       = 0;
    pSession->settlePending = 0;
    return LoadMedia(pSession, pSession->config.contentUrl, pSession->config.contentType, 0, 1, "BUFFERED");
}

void CastSession_SetProxy(CastSession* pSession, const char* pUrl, const char* pContentType)
{
    EnterCriticalSection(&pSession->lock);
    (void)snprintf(pSession->proxyUrl, sizeof(pSession->proxyUrl), "%s", pUrl);
    (void)snprintf(pSession->proxyType, sizeof(pSession->proxyType), "%s", pContentType);
    pSession->haveProxy = 1;
    LeaveCriticalSection(&pSession->lock);
}

static int SendMediaCommand(CastSession* pSession, const char* pType)
{
    char payload[256];

    if (0 == pSession->mediaSessionId || 0 == pSession->transportId[0] || pSession->awaitingLoad)
    {
        return 1;
    }
    (void)snprintf(payload, sizeof(payload), "{\"type\":\"%s\",\"requestId\":%d,\"mediaSessionId\":%lld}", pType, ++pSession->requestId, (long long)pSession->mediaSessionId);
    return SendJson(pSession, NS_MEDIA, pSession->transportId, payload);
}

/* A paused seek only decodes to the target frame and paints it; a resuming seek also refills
** the playback buffer. Scrubbing sends paused seeks and resumes once at the end. */
static int Seek(CastSession* pSession, double seconds, int resume)
{
    char payload[256];

    if (0 == pSession->mediaSessionId || 0 == pSession->transportId[0] || pSession->awaitingLoad)
    {
        return 1; /* a LOAD is in flight; its currentTime positions us and a seek now is stale */
    }
    if (seconds < 0)
    {
        seconds = 0;
    }
    if (pSession->status.duration > 0 && seconds > pSession->status.duration)
    {
        seconds = pSession->status.duration;
    }
    (void)snprintf(payload,
                   sizeof(payload),
                   "{\"type\":\"SEEK\",\"requestId\":%d,\"mediaSessionId\":%lld,\"currentTime\":%.3f,\"resumeState\":\"%s\"}",
                   ++pSession->requestId,
                   (long long)pSession->mediaSessionId,
                   seconds,
                   resume ? "PLAYBACK_START" : "PLAYBACK_PAUSE");
    pSession->lastSeekTick     = GetTickCount64();
    pSession->seekPending      = 1;
    pSession->seekTarget       = seconds;
    pSession->seekPendingSince = pSession->lastSeekTick;
    pSession->resumeSince      = resume ? pSession->lastSeekTick : 0;
    pSession->wantPlayAfterSeek  = resume;
    pSession->playAfterSeekSince = pSession->lastSeekTick;
    if (resume)
    {
        pSession->measurePending    = 1;
        pSession->measureTarget     = seconds;
        pSession->measureSince      = pSession->lastSeekTick;
        pSession->measureRebuffered = 0;
    }
    /* Report the target at once so the display follows the hand, not the round trip. */
    pSession->status.currentTime = seconds;
    EmitStatus(pSession);
    return SendJson(pSession, NS_MEDIA, pSession->transportId, payload);
}

/* Sends the newest queued scrub position once the rate limit allows. */
static int FlushSeek(CastSession* pSession, ULONGLONG now)
{
    if (!pSession->seekQueued || pSession->awaitingLoad || now - pSession->lastSeekTick < SEEK_INTERVAL_MS)
    {
        return 1; /* hold the newest scrub target until any in-flight LOAD lands */
    }
    pSession->seekQueued = 0;
    return Seek(pSession, pSession->seekQueuedSeconds, pSession->seekQueuedResume);
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
    if (pSession->awaitingLoad && 0 != pSession->mediaSessionId && pSession->mediaSessionId != pSession->preLoadSessionId)
    {
        pSession->awaitingLoad = 0; /* the new media session is live; seeks may resume on it */
    }
    {
        const double rawTime = cvkGetJsonDouble(cvkGetJsonMember(pDocument, pStatus, "currentTime"));
        char         rawState[32];

        CopyJsonString(cvkGetJsonMember(pDocument, pStatus, "playerState"), rawState, sizeof(rawState));
        if (pSession->measurePending && 0 == strcmp(rawState, "BUFFERING"))
        {
            pSession->measureRebuffered = 1; /* the "PLAYING" was optimistic; it stalled again */
        }
        /* The eye sees motion only when the reported time actually advances past the target. */
        if (pSession->measurePending && 0 == strcmp(rawState, "PLAYING") && rawTime > pSession->measureTarget + 0.30)
        {
            Log(pSession, "  seek->motion %llu ms%s", (unsigned long long)(GetTickCount64() - pSession->measureSince), pSession->measureRebuffered ? " (re-buffered once)" : "");
            pSession->measurePending = 0;
        }
    }
    pSession->status.currentTime = cvkGetJsonDouble(cvkGetJsonMember(pDocument, pStatus, "currentTime"));
    if (pSession->seekQueued)
    {
        pSession->status.currentTime = pSession->seekQueuedSeconds; /* a newer target is about to go out */
    }
    else if (pSession->seekPending)
    {
        const double distance = pSession->status.currentTime - pSession->seekTarget;

        if (distance < SEEK_CONFIRM_TOLERANCE && distance > -SEEK_CONFIRM_TOLERANCE)
        {
            pSession->seekPending = 0;
            Log(pSession, "seek to %.1f s confirmed by the receiver after %llu ms", pSession->seekTarget, (unsigned long long)(GetTickCount64() - pSession->seekPendingSince));
        }
        else if (GetTickCount64() - pSession->seekPendingSince > SEEK_CONFIRM_MS)
        {
            pSession->seekPending = 0; /* the receiver never got there; show what it reports */
        }
        else
        {
            pSession->status.currentTime = pSession->seekTarget; /* stale report from before the seek */
        }
    }
    pDuration = cvkGetJsonMember(pDocument, cvkGetJsonMember(pDocument, pStatus, "media"), "duration");
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
    if (changed && 0 != pSession->resumeSince && 0 == strcmp(name, "PLAYING"))
    {
        Log(pSession, "playing again %llu ms after the seek", (unsigned long long)(GetTickCount64() - pSession->resumeSince));
        pSession->resumeSince = 0;
    }
    (void)snprintf(pSession->status.stateName, sizeof(pSession->status.stateName), "%s", name);
    pSession->status.state = StateFromName(name, 0 == strcmp(name, "LOADING"));
    /* A resuming seek should leave the receiver playing; some receivers land it paused after a
    ** burst of scrub seeks and hold audio muted. Once the seek has landed, force PLAY. */
    if (pSession->wantPlayAfterSeek && !pSession->seekPending)
    {
        if (CAST_STATE_PLAYING == pSession->status.state)
        {
            pSession->wantPlayAfterSeek = 0;
        }
        else if (CAST_STATE_PAUSED == pSession->status.state)
        {
            Log(pSession, "seek landed paused; sending PLAY so audio and video keep running");
            (void)SendMediaCommand(pSession, "PLAY");
            pSession->wantPlayAfterSeek = 0;
        }
        else if (GetTickCount64() - pSession->playAfterSeekSince > 3000)
        {
            pSession->wantPlayAfterSeek = 0; /* give up quietly if it never settles */
        }
    }
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
        if (cvkJsonStringEquals(pType, "LOAD_FAILED") || cvkJsonStringEquals(pType, "LOAD_CANCELLED") || cvkJsonStringEquals(pType, "INVALID_REQUEST") || cvkJsonStringEquals(pType, "ERROR"))
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
        command             = pSession->queue[pSession->queueHead];
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
        case CAST_COMMAND_SCRUB:
        {
            const ULONGLONG now = GetTickCount64();

            if (!pSession->scrubbing)
            {
                pSession->scrubbing          = 1;
                pSession->playingBeforeScrub = (CAST_STATE_PLAYING == pSession->status.state || CAST_STATE_BUFFERING == pSession->status.state);
                pSession->scrubRunSince      = now;
            }
            pSession->lastActivityTick = now;
            if (pSession->onProxy)
            {
                /* Coalesce to the newest position; the proxy is the loaded stream, so this is a
                ** plain in-stream SEEK with no reload. Paused during the drag, resumed on release. */
                pSession->seekQueued        = 1;
                pSession->seekQueuedSeconds = command.seconds;
                pSession->seekQueuedResume  = 0;
            }
            /* Before the proxy is loaded there is nothing cheap to seek: do NOT scrub the deep full
            ** stream (it bounces the picture and re-buffers). The one-time switch handles it. */
            break;
        }
        case CAST_COMMAND_SEEK:
        {
            /* A discrete seek ends the gesture. It is always an in-stream SEEK (no LOAD): on the
            ** proxy that is instant and keeps audio and video together; on the full stream, before
            ** the proxy is ready, it is the receiver's own seek. There is no stream switch here, so
            ** audio is never reset and playback never stops to re-buffer for a quality upgrade. */
            const int resume = pSession->scrubbing ? pSession->playingBeforeScrub : (CAST_STATE_PLAYING == pSession->status.state || CAST_STATE_BUFFERING == pSession->status.state);

            pSession->scrubbing        = 0;
            pSession->scrubRunSince    = 0;
            pSession->lastActivityTick = GetTickCount64();
            if (pSession->awaitingLoad)
            {
                /* the one-time proxy LOAD is still landing: defer the release onto the new session */
                pSession->seekQueued        = 1;
                pSession->seekQueuedSeconds = command.seconds;
                pSession->seekQueuedResume  = resume;
            }
            else
            {
                pSession->seekQueued = 0; /* this exact position supersedes any pending scrub */
                ok                   = Seek(pSession, command.seconds, resume);
            }
            break;
        }
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
    const char* pReason        = NULL;
    int         result         = 1;
    int         proxyAnnounced = 0;

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
    if (pSession->config.keyframeCount > 1)
    {
        Log(pSession,
            "scrub previews snap to %u keyframes, %.2f s apart on average",
            pSession->config.keyframeCount,
            (pSession->config.pKeyframes[pSession->config.keyframeCount - 1] - pSession->config.pKeyframes[0]) / (pSession->config.keyframeCount - 1));
    }
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
        int         proxyReady;

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
        if (0 != pSession->mediaSessionId &&
            now - lastPoll >= ((pSession->seekPending || 0 != pSession->resumeSince || CAST_STATE_BUFFERING == pSession->status.state) ? STATUS_POLL_FAST_MS : STATUS_POLL_MS))
        {
            lastPoll = now;
            if (!SendMediaCommand(pSession, "GET_STATUS"))
            {
                pReason = CastChannel_GetError(pSession->pChannel);
                goto done;
            }
        }
        EnterCriticalSection(&pSession->lock);
        proxyReady = pSession->haveProxy;
        LeaveCriticalSection(&pSession->lock);
        if (proxyReady && !proxyAnnounced)
        {
            proxyAnnounced = 1;
            Log(pSession, "proxy ready: switching over for instant, buffer-free seeking");
        }
        commands = ProcessCommands(pSession);
        if (commands < 0 || !FlushSeek(pSession, now))
        {
            pReason = CastChannel_GetError(pSession->pChannel);
            goto done;
        }
        /* Once the proxy is ready, move onto it a single time and stay there. It is the shallow
        ** LIVE, short-GOP stream, so from here every seek is a cheap in-stream SEEK: no LOAD, no
        ** re-buffer, and no audio reset. The full stream only plays until the proxy exists. There is
        ** no switch back to the deep BUFFERED stream, which is what used to stall and drop audio. */
        if (proxyReady && !pSession->onProxy && !pSession->switchedToProxy && !pSession->awaitingLoad && !pSession->scrubbing && !pSession->seekPending && 0 != pSession->mediaSessionId &&
            (CAST_STATE_PLAYING == pSession->status.state || CAST_STATE_PAUSED == pSession->status.state))
        {
            char      proxyUrl[1200];
            char      proxyType[64];
            const int wasPlaying = (CAST_STATE_PLAYING == pSession->status.state);

            EnterCriticalSection(&pSession->lock);
            (void)snprintf(proxyUrl, sizeof(proxyUrl), "%s", pSession->proxyUrl);
            (void)snprintf(proxyType, sizeof(proxyType), "%s", pSession->proxyType);
            LeaveCriticalSection(&pSession->lock);
            Log(pSession, "switching to the low-latency proxy at %.1f s; seeks are instant from here", pSession->status.currentTime);
            pSession->onProxy         = 1;
            pSession->switchedToProxy = 1;
            pSession->playingWanted   = wasPlaying;
            if (!LoadMedia(pSession, proxyUrl, proxyType, pSession->status.currentTime, wasPlaying, "LIVE"))
            {
                pReason = CastChannel_GetError(pSession->pChannel);
                goto done;
            }
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
