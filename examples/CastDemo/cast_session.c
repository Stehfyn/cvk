/*=================================================================================================
** 1. References
**===============================================================================================*/

/* The CASTV2 conversation with the default media receiver (CC1AD845).
**
**   CONNECT receiver-0, LAUNCH CC1AD845
**   RECEIVER_STATUS names the application's transportId
**   CONNECT transportId; the application announces MEDIA_STATUS to the new sender
**   LOAD the URL; MEDIA_STATUS follows every change and every GET_STATUS
**   PING/PONG keep the channel alive; STOP closes the application at the end
**
** Receiver replies are JSON read with the cvk JSON reader. */
/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <winsock2.h>
#include <windows.h>

#include <float.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "cast_channel.h"
#include "cast_media.h"
#include "cast_session.h"
#include "cast_commands.h"
#include "os/os_compiler.h"
#include "os/os_assert.h"
#include "os/os_memory.h"
#include "os/os_atomic.h"
#include "crt/crt_memory.h"
#include "utl/utl_castjson.h"
#include "utl/utl_unicode.h"
#include "cvk_json.h"
#include "mp4_index.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define NS_CONNECTION "urn:x-cast:com.google.cast.tp.connection"
#define NS_HEARTBEAT  "urn:x-cast:com.google.cast.tp.heartbeat"
#define NS_RECEIVER   "urn:x-cast:com.google.cast.receiver"
#define NS_MEDIA      "urn:x-cast:com.google.cast.media"
#define NS_PREFIX_LEN (sizeof("urn:x-cast:com.google.cast.") - 1)

#define DEFAULT_MEDIA_RECEIVER  "CC1AD845"
#define SENDER_ID               "sender-0"
#define RECEIVER_ID             "receiver-0"
#define HEARTBEAT_MS            (5000)
#define STATUS_POLL_MS          (1000)
#define STATUS_POLL_FAST_MS     (60) /* while a seek settles */
#define LOAD_FALLBACK_MS        (3000)
#define RECEIVE_SLICE_MS        (5)    /* command pickup latency while waiting for the receiver */
#define SEEK_INTERVAL_MS        (60)   /* newest scrub position goes out at most this often */
#define SEEK_CONFIRM_MS         (2000) /* hold the target until the receiver reports near it */
#define SEEK_CONFIRM_TOLERANCE  (1.5)
#define SCRUB_TO_PROXY_MS       (140)  /* sustained scrub before dropping to the low-res proxy */
#define SETTLE_MS               (1400) /* quiet time before upgrading back to full quality */
#define JSON_VALUE_CAPACITY     (4096)
#define LOAD_PAYLOAD_CAPACITY   (20000)
#define SESSION_RUN_IDLE        (0)
#define SESSION_RUN_ACTIVE      (1)
#define SESSION_RUN_COMPLETE    (2)
#define SESSION_ID_CAPACITY     (128)
#define LOAD_SUBTITLE_TRACK_ID  (1)
#define LOAD_SUBTITLE_NAME      "Subtitles"
#define LOAD_DEFAULT_LANGUAGE   "und"
#define LOAD_STREAM_NONE        "NONE"
#define LOAD_STREAM_BUFFERED    "BUFFERED"
#define LOAD_STREAM_LIVE        "LIVE"
#define LOAD_MILLISECONDS_LIMIT (18446744073709551616.0) /* Exclusive uint64_t conversion limit. */

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

typedef struct CastSession_State_s
{
    CastMedia_Channel_t proxyUpdates;       ///< Proxy worker to runner, latest complete pair.
    CastMedia_Channel_t replacementUpdates; ///< GUI owner to runner, latest complete pair.
    const CastMedia_t*  proxy;              ///< Runner borrow, stable until its next proxy Take.
    CastSessionConfig   config;
    CastChannel*        pChannel;
    char                loadPayload[LOAD_PAYLOAD_CAPACITY];
    int32_t             requestId;
    char                sessionId[SESSION_ID_CAPACITY];   /* the receiver application instance */
    char                transportId[SESSION_ID_CAPACITY]; /* where media messages go */
    bool_t              joined;
    bool_t              loadPending;  /* LOAD waits for the application's first MEDIA_STATUS */
    uint64_t            loadDeadline; /* ... or for this tick count, whichever comes first */
    int64_t             mediaSessionId;
    CastStatus          status;
    bool_t              finished;
    /* scrubbing: coalesce queued seeks, rate-limit sends, mask stale positions */
    bool_t    seekQueued;
    float64_t seekQueuedSeconds;
    bool_t    seekQueuedResume; /* resume playback after the queued seek */
    uint64_t  lastSeekTick;
    bool_t    seekPending;
    float64_t seekTarget;
    uint64_t  seekPendingSince;
    bool_t    wantPlayAfterSeek; /* a resuming seek went out: force PLAY if it lands paused */
    uint64_t  playAfterSeekSince;
    bool_t    scrubbing; /* between the first SCRUB and the SEEK that ends the drag */
    bool_t    playingBeforeScrub;
    uint64_t  resumeSince; /* a resuming seek went out: time until PLAYING is logged */
    /* real-resume measurement: time until the picture actually advances past the target, which
    ** is what the eye sees, not the receiver's optimistic PLAYING report */
    bool_t    measurePending;
    float64_t measureTarget;
    uint64_t  measureSince;
    bool_t    measureRebuffered;
    /* adaptive quality: scrub on a low-res proxy for instant response, settle back to full
    ** quality with hysteresis. Proxy and full stream share one timeline (same content). */
    bool_t         onProxy;          /* the currently loaded stream is the proxy */
    bool_t         switchedToProxy;  /* the one-time switch from the full stream to the proxy is done */
    uint64_t       scrubRunSince;    /* first scrub of the current drag, 0 when not dragging */
    uint64_t       lastActivityTick; /* newest scrub or seek */
    bool_t         settlePending;    /* on the proxy after a drag; upgrade when settled */
    bool_t         playingWanted;    /* whether playback should be running after a stream switch */
    bool_t         awaitingLoad;     /* a LOAD is in flight; the old mediaSessionId is dead until it lands */
    int64_t        preLoadSessionId; /* the mediaSessionId before that LOAD, to detect the new one */
    SRWLOCK        commandLock;
    CastCommands_t commands;
} CastSession_State_t;

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
 * \brief Initializes meaningful state for one prepared cast.
 *
 * \par Requirements:
 *  - 001 The owner shall exclude runners and publishers and retain the startup region.
 *  - 002 Configuration storage shall be disjoint; referenced keyframes and callback context shall remain alive until Reset.
 *  - 003 Every control field and readable string shall be initialized without clearing unused capacity or allocating.
 *
 * \param[out] state    Required persistent implementation storage
 * \param[in]  config   Required validated caller configuration
 *
 * \retval     None   One idle cast is prepared.
 */
static FORCE_INLINE void InitializeState(CastSession_State_t* state, const CastSessionConfig* config);

/*************************************************************************************************/
/**
 * \brief Determines whether settled receiver state permits a media source change.
 *
 * \par Requirements:
 *  - 001 Only the runner shall query its prepared receiver state.
 *  - 002 In-flight loading, scrubbing or seeking shall prohibit a source change.
 *  - 003 A media session in PLAYING or PAUSED state shall be required.
 *
 * \param[in] state   Required runner-owned prepared state
 *
 * \retval    TRUE    The receiver is ready for a source change.
 * \retval    FALSE   A transition is pending or no active media session is available.
 */
static FORCE_INLINE bool_t IsMediaSwitchReady(const CastSession_State_t* state);

/*************************************************************************************************/
/**
 * \brief Owns one receiver conversation until terminal channel cleanup.
 *
 * \par Requirements:
 *  - 001 The wrapper shall have acquired exclusive runner ownership after Prepare.
 *  - 002 Callback and keyframe borrows shall remain valid until the owner acquires completion.
 *  - 003 Every normal or failed conversation shall leave the channel pointer clear.
 *
 * \param[in,out] pSession   Required prepared implementation state
 *
 * \retval        0   Playback finished or was stopped by request.
 * \retval        1   The channel or receiver operation failed.
 */
static FORCE_INLINE int32_t RunSession(CastSession_State_t* pSession);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Command reset discards prior Stop and positions. Empty string terminators
 *  make retained payload bytes inaccessible until each bounded producer writes them.
 */
static FORCE_INLINE void InitializeState(CastSession_State_t* state, const CastSessionConfig* config)
{
    CRT_Memory_Copy(&state->config, config, sizeof(CastSessionConfig));
    state->pChannel            = NULL;
    state->loadPayload[0]      = 0;
    state->requestId           = 0;
    state->sessionId[0]        = 0;
    state->transportId[0]      = 0;
    state->joined              = FALSE;
    state->loadPending         = FALSE;
    state->loadDeadline        = 0;
    state->mediaSessionId      = 0;
    state->status.state        = CAST_STATE_CONNECTING;
    state->status.stateName[0] = 0;
    state->status.currentTime  = 0.0;
    state->status.duration     = 0.0;
    state->status.hasMedia     = FALSE;
    state->finished            = FALSE;
    state->seekQueued          = FALSE;
    state->seekQueuedSeconds   = 0.0;
    state->seekQueuedResume    = FALSE;
    state->lastSeekTick        = 0;
    state->seekPending         = FALSE;
    state->seekTarget          = 0.0;
    state->seekPendingSince    = 0;
    state->wantPlayAfterSeek   = FALSE;
    state->playAfterSeekSince  = 0;
    state->scrubbing           = FALSE;
    state->playingBeforeScrub  = FALSE;
    state->resumeSince         = 0;
    state->measurePending      = FALSE;
    state->measureTarget       = 0.0;
    state->measureSince        = 0;
    state->measureRebuffered   = FALSE;
    state->onProxy             = FALSE;
    state->switchedToProxy     = FALSE;
    state->scrubRunSince       = 0;
    state->lastActivityTick    = 0;
    state->settlePending       = FALSE;
    state->playingWanted       = FALSE;
    state->awaitingLoad        = FALSE;
    state->preLoadSessionId    = 0;
    state->proxy               = NULL;
    CastMedia_Initialize(&state->proxyUpdates);
    CastMedia_Initialize(&state->replacementUpdates);
    InitializeSRWLock(&state->commandLock);
    CastCommands_Reset(&state->commands);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Named questions describe only runner-owned fields; no synchronization is needed.
 */
static FORCE_INLINE bool_t IsMediaSwitchReady(const CastSession_State_t* state)
{
    bool_t settled;
    bool_t has_session;
    bool_t playing;
    bool_t paused;
    bool_t active;

    settled     = (!state->awaitingLoad) && (!state->scrubbing) && (!state->seekPending);
    has_session = (0 != state->mediaSessionId);
    playing     = (CAST_STATE_PLAYING == state->status.state);
    paused      = (CAST_STATE_PAUSED == state->status.state);
    active      = playing || paused;

    return settled && has_session && active;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Formats one bounded diagnostic and invokes the configured event callback
 *  before the local text buffer goes out of scope.
 */
static void Log(CastSession_State_t* pSession, const char* pFormat, ...)
{
    va_list arguments;
    char    text[1600];

    va_start(arguments, pFormat);
    (void)vsnprintf(text, sizeof(text), pFormat, arguments);
    va_end(arguments);
    pSession->config.callback(pSession->config.pUser, CAST_EVENT_LOG, NULL, text);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Passes the current status snapshot to the configured event callback.
 */
static void EmitStatus(CastSession_State_t* pSession)
{
    pSession->config.callback(pSession->config.pUser, CAST_EVENT_STATUS, &pSession->status, NULL);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Updates the state enum and display name, then publishes the status.
 */
static void SetPhase(CastSession_State_t* pSession, CastPlayerState state, const char* pName)
{
    pSession->status.state = state;
    (void)snprintf(pSession->status.stateName, sizeof(pSession->status.stateName), "%s", pName);
    EmitStatus(pSession);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Borrows a JSON payload into a Cast envelope, optionally logs it and sends it
 *  through the channel. Transport failure is reported through the session log.
 */
static int SendJson(CastSession_State_t* pSession,
                    const char*          pNamespace,
                    const char*          pDestination,
                    const char*          pPayload)
{
    CastMessage_t message;

    CastMessage_Initialize(&message, SENDER_ID, pDestination, pNamespace, pPayload);
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

/*************************************************************************************************/
/**
 * \par Details:
 *  Formats LOAD before updating the loading state. Records the previous media
 *  session ID so subsequent status can identify the replacement media session.
 */
static FORCE_INLINE bool_t LoadMedia(CastSession_State_t* pSession,
                                     const char*          pUrl,
                                     const char*          pType,
                                     double               startSeconds,
                                     int                  autoplay,
                                     const char*          pStreamType)
{
    bool_t is_valid;

    if (startSeconds < 0)
    {
        startSeconds = 0;
    }
    is_valid = (0 < CastSession_FormatLoad(&pSession->config,
                                           pSession->sessionId,
                                           ++pSession->requestId,
                                           pUrl,
                                           pType,
                                           startSeconds,
                                           autoplay,
                                           pStreamType,
                                           pSession->loadPayload,
                                           LOAD_PAYLOAD_CAPACITY));
    if (is_valid)
    {
        SetPhase(pSession, CAST_STATE_LOADING, "LOADING");
        pSession->playingWanted    = autoplay;
        pSession->awaitingLoad     = 1; /* the media session id changes; hold seeks until the new one lands */
        pSession->preLoadSessionId = pSession->mediaSessionId;
        pSession->seekPending      = 0;
        is_valid                   = SendJson(pSession, NS_MEDIA, pSession->transportId, pSession->loadPayload);
    }

    return is_valid;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Clears proxy/settling state and loads the configured full-quality source
 *  from the beginning with autoplay and BUFFERED semantics.
 */
static int Load(CastSession_State_t* pSession)
{
    Log(pSession, "loading %s", pSession->config.contentUrl);
    pSession->onProxy       = 0;
    pSession->settlePending = 0;
    return LoadMedia(pSession, pSession->config.contentUrl, pSession->config.contentType, 0, 1, "BUFFERED");
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Suppresses commands until a media session and transport are available and no
 *  LOAD is pending; otherwise sends a request with the next request ID.
 */
static int SendMediaCommand(CastSession_State_t* pSession, const char* pType)
{
    char payload[256];

    if (0 == pSession->mediaSessionId || 0 == pSession->transportId[0] || pSession->awaitingLoad)
    {
        return 1;
    }
    (void)snprintf(payload,
                   sizeof(payload),
                   "{\"type\":\"%s\",\"requestId\":%d,\"mediaSessionId\":%lld}",
                   pType,
                   ++pSession->requestId,
                   (long long)pSession->mediaSessionId);
    return SendJson(pSession, NS_MEDIA, pSession->transportId, payload);
}

/* A paused seek only decodes to the target frame and paints it; a resuming seek also refills
** the playback buffer. Scrubbing sends paused seeks and resumes once at the end. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Clamps the target to the known timeline, records seek-confirmation state and
 *  publishes the target immediately before sending SEEK with the requested
 *  resume state.
 */
static int Seek(CastSession_State_t* pSession, double seconds, int resume)
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
    (void)snprintf(
        payload,
        sizeof(payload),
        "{\"type\":\"SEEK\",\"requestId\":%d,\"mediaSessionId\":%lld,\"currentTime\":%.3f,\"resumeState\":\"%s\"}",
        ++pSession->requestId,
        (long long)pSession->mediaSessionId,
        seconds,
        resume ? "PLAYBACK_START" : "PLAYBACK_PAUSE");
    pSession->lastSeekTick       = GetTickCount64();
    pSession->seekPending        = 1;
    pSession->seekTarget         = seconds;
    pSession->seekPendingSince   = pSession->lastSeekTick;
    pSession->resumeSince        = resume ? pSession->lastSeekTick : 0;
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
/*************************************************************************************************/
/**
 * \par Details:
 *  Sends only the newest queued position after the rate-limit interval and after
 *  an in-flight LOAD has completed.
 */
static int FlushSeek(CastSession_State_t* pSession, ULONGLONG now)
{
    if (!pSession->seekQueued || pSession->awaitingLoad || now - pSession->lastSeekTick < SEEK_INTERVAL_MS)
    {
        return 1; /* hold the newest scrub target until any in-flight LOAD lands */
    }
    pSession->seekQueued = 0;
    return Seek(pSession, pSession->seekQueuedSeconds, pSession->seekQueuedResume);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Copies a counted JSON string into a terminated buffer, truncating to capacity
 *  minus one. Missing or non-string values produce an empty string.
 */
static FORCE_INLINE void CopyJsonString(const cvkJsonValue* pValue, char* pOut, uint64_t capacity)
{
    bool_t   has_value;
    bool_t   is_string;
    uint64_t length;

    LOGASSERT(NULL != pOut);
    LOGASSERT(0 < capacity);

    pOut[0]   = 0;
    has_value = (NULL != pValue);
    if (has_value)
    {
        is_string = (CVK_JSON_STRING == pValue->kind);
        if (is_string)
        {
            length = pValue->textLength;
            if (capacity <= length)
            {
                length = capacity - 1;
            }
            if (0 < length)
            {
                CRT_Memory_Copy(pOut, pValue->pText, length);
            }
            pOut[length] = 0;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Locates the default media receiver application, records its session and
 *  transport IDs, then connects to its virtual channel and defers LOAD until
 *  media status or the fallback deadline.
 */
static int OnReceiverStatus(CastSession_State_t* pSession, const cvkJsonDocument* pDocument, const cvkJsonValue* pRoot)
{
    const cvkJsonValue* pStatus       = cvkGetJsonMember(pDocument, pRoot, "status");
    const cvkJsonValue* pApplications = cvkGetJsonMember(pDocument, pStatus, "applications");
    const cvkJsonValue* pApplication  = NULL;
    uint32_t index;

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

        CopyJsonString(cvkGetJsonMember(pDocument, pApplication, "sessionId"),
                       pSession->sessionId,
                       sizeof(pSession->sessionId));
        CopyJsonString(cvkGetJsonMember(pDocument, pApplication, "transportId"),
                       pSession->transportId,
                       sizeof(pSession->transportId));
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

/*************************************************************************************************/
/**
 * \par Details:
 *  Maps known receiver state names and uses the loading flag to distinguish
 *  the fallback LOADING and IDLE states.
 */
static CastPlayerState GetStateFromName(const char* pName, int loading)
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

/*************************************************************************************************/
/**
 * \par Details:
 *  Interprets the receiver's media status, reconciles pending load/seek state and
 *  updates the playback snapshot published to the event callback.
 */
static int OnMediaStatus(CastSession_State_t* pSession, const cvkJsonDocument* pDocument, const cvkJsonValue* pRoot)
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
    if (pSession->awaitingLoad && 0 != pSession->mediaSessionId &&
        pSession->mediaSessionId != pSession->preLoadSessionId)
    {
        pSession->awaitingLoad = 0; /* the new media session is live; seeks may resume on it */
    }
    {
        const double rawTime = cvkGetJsonDouble(cvkGetJsonMember(pDocument, pStatus, "currentTime"));
        char rawState[32];

        CopyJsonString(cvkGetJsonMember(pDocument, pStatus, "playerState"), rawState, sizeof(rawState));
        if (pSession->measurePending && 0 == strcmp(rawState, "BUFFERING"))
        {
            pSession->measureRebuffered = 1; /* the "PLAYING" was optimistic; it stalled again */
        }
        /* The eye sees motion only when the reported time actually advances past the target. */
        if (pSession->measurePending && 0 == strcmp(rawState, "PLAYING") && rawTime > pSession->measureTarget + 0.30)
        {
            Log(pSession,
                "  seek->motion %llu ms%s",
                (unsigned long long)(GetTickCount64() - pSession->measureSince),
                pSession->measureRebuffered ? " (re-buffered once)" : "");
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
            Log(pSession,
                "seek to %.1f s confirmed by the receiver after %llu ms",
                pSession->seekTarget,
                (unsigned long long)(GetTickCount64() - pSession->seekPendingSince));
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
        Log(pSession,
            "playing again %llu ms after the seek",
            (unsigned long long)(GetTickCount64() - pSession->resumeSince));
        pSession->resumeSince = 0;
    }
    (void)snprintf(pSession->status.stateName, sizeof(pSession->status.stateName), "%s", name);
    pSession->status.state = GetStateFromName(name, 0 == strcmp(name, "LOADING"));
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

/*************************************************************************************************/
/**
 * \par Details:
 *  Logs the receiver's reason and optional detailed error code. Locally served
 *  media also includes the source URL in the connection diagnostic.
 */
static void ExplainMediaError(CastSession_State_t*   pSession,
                              const cvkJsonDocument* pDocument,
                              const cvkJsonValue*    pRoot,
                              const char*            pType)
{
    const cvkJsonValue* pCode = cvkGetJsonMember(pDocument, pRoot, "detailedErrorCode");
    char reason[128];

    CopyJsonString(cvkGetJsonMember(pDocument, pRoot, "reason"), reason, sizeof(reason));
    if (NULL != pCode)
    {
        Log(pSession,
            "%s%s%s (detailedErrorCode %lld)",
            pType,
            reason[0] ? ": " : "",
            reason,
            (long long)cvkGetJsonInt64(pCode));
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
/*************************************************************************************************/
/**
 * \par Details:
 *  Routes text JSON by Cast namespace to heartbeat, connection, receiver and
 *  media handlers. Binary and unrelated messages leave the session running.
 */
static int HandleMessage(CastSession_State_t* pSession, const CastMessage_t* pMessage)
{
    static cvkJsonValue values[JSON_VALUE_CAPACITY];
    cvkJsonDocument     document;
    const cvkJsonValue* pRoot;
    const cvkJsonValue* pType;

    if (pSession->config.verbose)
    {
        const size_t skip = (pMessage->namespaceLength > NS_PREFIX_LEN) ? NS_PREFIX_LEN : 0;

        Log(pSession,
            "<< %.*s %.*s",
            (int)(pMessage->namespaceLength - skip),
            pMessage->pNamespace + skip,
            (int)pMessage->payloadLength,
            pMessage->pPayload);
    }
    if (pMessage->binary || 0 == pMessage->payloadLength || '{' != pMessage->pPayload[0])
    {
        return 1;
    }
    document.valueCount =
        cvkParseJsonDocument(pMessage->pPayload, pMessage->payloadLength, values, JSON_VALUE_CAPACITY);
    document.pValues = values;
    pRoot            = &values[0];
    pType            = cvkGetJsonMember(&document, pRoot, "type");
    if (CastMessage_IsNamespace(pMessage, NS_HEARTBEAT))
    {
        if (cvkJsonStringEquals(pType, "PING"))
        {
            return SendJson(pSession, NS_HEARTBEAT, RECEIVER_ID, "{\"type\":\"PONG\"}");
        }
        return 1;
    }
    if (CastMessage_IsNamespace(pMessage, NS_CONNECTION))
    {
        if (cvkJsonStringEquals(pType, "CLOSE"))
        {
            Log(pSession, "the receiver closed the connection");
            return 0;
        }
        return 1;
    }
    if (CastMessage_IsNamespace(pMessage, NS_RECEIVER))
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
    if (CastMessage_IsNamespace(pMessage, NS_MEDIA))
    {
        if (cvkJsonStringEquals(pType, "MEDIA_STATUS"))
        {
            return OnMediaStatus(pSession, &document, pRoot);
        }
        if (cvkJsonStringEquals(pType, "LOAD_FAILED") || cvkJsonStringEquals(pType, "LOAD_CANCELLED") ||
            cvkJsonStringEquals(pType, "INVALID_REQUEST") || cvkJsonStringEquals(pType, "ERROR"))
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
/*************************************************************************************************/
/**
 * \par Details:
 *  Sends receiver STOP for the active application, then drains channel messages
 *  while the local shutdown deadline has not elapsed.
 */
static void Leave(CastSession_State_t* pSession)
{
    char      payload[256];
    ULONGLONG deadline;

    if (0 == pSession->sessionId[0] || NULL == pSession->pChannel)
    {
        return;
    }
    (void)snprintf(payload,
                   sizeof(payload),
                   "{\"type\":\"STOP\",\"requestId\":%d,\"sessionId\":\"%s\"}",
                   ++pSession->requestId,
                   pSession->sessionId);
    if (!SendJson(pSession, NS_RECEIVER, RECEIVER_ID, payload))
    {
        return;
    }
    deadline = GetTickCount64() + 1000;
    for (;;)
    {
        CastMessage_t message;

        if (GetTickCount64() >= deadline)
        {
            break;
        }
        if (CastChannel_Receive(pSession->pChannel, &message, 200) < 0)
        {
            break;
        }
    }
}

/* Applies queued commands. Returns 0 when a STOP was requested, -1 when sending failed. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Takes at most one queue capacity of commands per call, releasing the lock
 *  before network work. Scrub positions are coalesced; the final seek preserves
 *  the gesture's intended resume state.
 */
static int ProcessCommands(CastSession_State_t* pSession)
{
    unsigned processed;

    for (processed = 0U; processed < CAST_COMMAND_CAPACITY; ++processed)
    {
        CastQueuedCommand_t command;
        int                 ok = 1;

        AcquireSRWLockExclusive(&pSession->commandLock);
        if (0 == CastCommands_Take(&pSession->commands, &command))
        {
            ReleaseSRWLockExclusive(&pSession->commandLock);
            return 1;
        }
        ReleaseSRWLockExclusive(&pSession->commandLock);
        switch (command.command)
        {
        case CAST_COMMAND_PLAY:
            ok = SendMediaCommand(pSession, "PLAY");
            break;
        case CAST_COMMAND_PAUSE:
            ok = SendMediaCommand(pSession, "PAUSE");
            break;
        case CAST_COMMAND_TOGGLE:
            ok = SendMediaCommand(
                pSession,
                (CAST_STATE_PLAYING == pSession->status.state || CAST_STATE_BUFFERING == pSession->status.state)
                    ? "PAUSE"
                    : "PLAY");
            break;
        case CAST_COMMAND_SCRUB:
        {
            const ULONGLONG now = GetTickCount64();

            if (!pSession->scrubbing)
            {
                pSession->scrubbing = 1;
                pSession->playingBeforeScrub =
                    (CAST_STATE_PLAYING == pSession->status.state || CAST_STATE_BUFFERING == pSession->status.state);
                pSession->scrubRunSince = now;
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
            const int resume = pSession->scrubbing ? pSession->playingBeforeScrub
                                                   : (CAST_STATE_PLAYING == pSession->status.state ||
                                                      CAST_STATE_BUFFERING == pSession->status.state);

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
        default:
            return -1;
        }
        if (!ok)
        {
            return -1;
        }
    }
    return 1;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Owns the channel conversation and processes queued commands, incoming messages,
 *  heartbeat/status timing and stream changes until termination, then publishes
 *  the terminal event.
 */
static FORCE_INLINE int32_t RunSession(CastSession_State_t* pSession)
{
    char      error[256];
    char      payload[128];
    ULONGLONG lastHeartbeat;
    ULONGLONG lastPoll;
    const char* pReason        = NULL;
    int         result         = 1;
    int         proxyAnnounced = 0;
    bool_t   needs_fast_poll;
    uint64_t poll_interval;
    bool_t   poll_due;

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
            (pSession->config.pKeyframes[pSession->config.keyframeCount - 1] - pSession->config.pKeyframes[0]) /
                (pSession->config.keyframeCount - 1));
    }
    (void)snprintf(payload,
                   sizeof(payload),
                   "{\"type\":\"LAUNCH\",\"requestId\":%d,\"appId\":\"%s\"}",
                   ++pSession->requestId,
                   DEFAULT_MEDIA_RECEIVER);
    if (!SendJson(pSession, NS_CONNECTION, RECEIVER_ID, "{\"type\":\"CONNECT\"}") ||
        !SendJson(pSession, NS_RECEIVER, RECEIVER_ID, payload))
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
        CastMessage_t message;
        const int          received = CastChannel_Receive(pSession->pChannel, &message, RECEIVE_SLICE_MS);
        ULONGLONG          now;
        int                commands;
        bool_t             proxyReady;
        const CastMedia_t* update;
        bool_t             ready;
        bool_t             has_replacement;
        bool_t             first_switch;
        bool_t             can_switch;
        bool_t             was_playing;

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
        needs_fast_poll =
            pSession->seekPending || pSession->resumeSince || (CAST_STATE_BUFFERING == pSession->status.state);
        poll_interval = needs_fast_poll ? STATUS_POLL_FAST_MS : STATUS_POLL_MS;
        poll_due      = (now - lastPoll >= poll_interval);
        if (pSession->mediaSessionId && poll_due)
        {
            lastPoll = now;
            if (!SendMediaCommand(pSession, "GET_STATUS"))
            {
                pReason = CastChannel_GetError(pSession->pChannel);
                goto done;
            }
        }
        update = CastMedia_Take(&pSession->proxyUpdates);
        if (NULL != update)
        {
            pSession->proxy = update;
        }
        proxyReady = (NULL != pSession->proxy);
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
        ready = IsMediaSwitchReady(pSession);
        if (ready)
        {
            update          = CastMedia_Take(&pSession->replacementUpdates);
            has_replacement = (NULL != update);
            if (has_replacement)
            {
                has_replacement = (0 < update->urlByteCount);
            }
            if (has_replacement)
            {
                CRT_Memory_Copy(pSession->config.contentUrl, update->url, (uint64_t)update->urlByteCount + 1);
                CRT_Memory_Copy(pSession->config.contentType, update->type, (uint64_t)update->typeByteCount + 1);
                pSession->proxy           = NULL;
                pSession->onProxy         = FALSE;
                pSession->switchedToProxy = TRUE;
                proxyReady                = FALSE;
                was_playing               = (CAST_STATE_PLAYING == pSession->status.state);
                Log(pSession, "applying audio gain at %.1f s", pSession->status.currentTime);
                if (!LoadMedia(pSession,
                               pSession->config.contentUrl,
                               pSession->config.contentType,
                               pSession->status.currentTime,
                               was_playing,
                               LOAD_STREAM_BUFFERED))
                {
                    pReason = CastChannel_GetError(pSession->pChannel);
                    goto done;
                }
            }
        }
        /* Once the proxy is ready, move onto it a single time and stay there. It is a short-GOP
        ** stream, so from here every seek is a cheap in-stream SEEK: no LOAD, no audio reset. The
        ** proxy is loaded as BUFFERED, not LIVE: its fragmented init segment carries the full movie
        ** duration, and only BUFFERED makes the receiver honor that duration and play the timeline
        ** linearly. LIVE made the receiver ignore the duration (the trackbar showed only the few
        ** seconds transcoded so far) and continuously chase the growing live edge, which is what
        ** made the picture bounce all over the timeline. */
        ready        = IsMediaSwitchReady(pSession);
        first_switch = (!pSession->onProxy) && (!pSession->switchedToProxy);
        can_switch   = proxyReady && first_switch && ready;
        if (can_switch)
        {
            was_playing = (CAST_STATE_PLAYING == pSession->status.state);
            Log(pSession,
                "switching to the low-latency proxy at %.1f s; seeks are instant from here",
                pSession->status.currentTime);
            pSession->onProxy         = TRUE;
            pSession->switchedToProxy = TRUE;
            pSession->playingWanted   = was_playing;
            if (!LoadMedia(pSession,
                           pSession->proxy->url,
                           pSession->proxy->type,
                           pSession->status.currentTime,
                           was_playing,
                           LOAD_STREAM_BUFFERED))
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
    /* Channel error text belongs to the channel. Preserve it before closing. */
    (void)snprintf(error, sizeof(error), "%s", (NULL != pReason) ? pReason : "Session ended.");
    CastChannel_Close(pSession->pChannel);
    pSession->pChannel        = NULL;
    pSession->status.hasMedia = 0;
    SetPhase(pSession, CAST_STATE_ENDED, "ENDED");
    pSession->config.callback(pSession->config.pUser, CAST_EVENT_ENDED, NULL, error);
    return result;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  Only startup establishes native session storage. Individual casts reuse it.
 *  Native residency/resource failure remains distinguishable from caller misuse.
 */
bool_t CastSession_Initialize(CastSession_t* session)
{
    bool_t initialized;

    LOGASSERT(NULL != session);

    session->state    = NULL;
    session->prepared = FALSE;
    OS_Atomic_Initialize32(&session->running, SESSION_RUN_IDLE);
    initialized = OS_Memory_Initialize(&session->memory, sizeof(CastSession_State_t));
    if (initialized)
    {
        session->state           = (CastSession_State_t*)OS_Memory_GetAddress(&session->memory);
        session->state->pChannel = NULL;
    }

    return initialized;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Preparation spans runner completion until the owner explicitly resets the object.
 */
bool_t CastSession_IsPrepared(const CastSession_t* session)
{
    LOGASSERT(NULL != session);

    return session->prepared;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Acquire observation pairs with the final runner release. External publishers
 *  and the worker's own result slot have separate lifetime obligations.
 */
bool_t CastSession_IsRunning(const CastSession_t* session)
{
    uint32_t running;

    LOGASSERT(NULL != session);

    running = OS_Atomic_Load32(&session->running);

    return (SESSION_RUN_ACTIVE == running);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  State reset touches only meaningful scalar fields and string terminators;
 *  large payload capacity is retained without clearing or reallocating it.
 */
void CastSession_Prepare(CastSession_t* session, const CastSessionConfig* config)
{
    bool_t initialized;
    bool_t running;
    bool_t prepared;
    bool_t has_keyframes;

    LOGASSERT(NULL != session);
    LOGASSERT(NULL != config);
    LOGASSERT(NULL != config->callback);

    initialized   = OS_Memory_IsReady(&session->memory);
    running       = CastSession_IsRunning(session);
    prepared      = CastSession_IsPrepared(session);
    has_keyframes = (NULL != config->pKeyframes) || (0 == config->keyframeCount);
    LOGASSERT(initialized);
    LOGASSERT(!running);
    LOGASSERT(!prepared);
    LOGASSERT(has_keyframes);
    LOGASSERT(NULL == session->state->pChannel);

    InitializeState(session->state, config);
    OS_Atomic_Store32(&session->running, SESSION_RUN_IDLE);
    session->prepared = TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Counted UTF-8 fields feed the specification writer directly; no escaped text
 *  or complete track is materialized on the stack. Preflight preserves output on failure.
 *  Millisecond rounding is explicit and independent of CRT locale or formatting.
 */
uint32_t CastSession_FormatLoad(const CastSessionConfig* config,
                                const char*              sessionId,
                                int32_t                  requestId,
                                const char*              url,
                                const char*              type,
                                double                   start,
                                bool_t                   autoplay,
                                const char*              streamType,
                                char*                    output,
                                uint32_t                 capacity)
{
    Utl_CastJson_Load_t     load;
    Utl_CastJson_Subtitle_t subtitle;
    uint32_t                stream_length;
    bool_t                  is_buffered;
    bool_t                  is_live;
    bool_t                  is_none;
    bool_t                  valid_stream;
    bool_t                  has_subtitle;
    bool_t                  valid_subtitle;
    bool_t                  valid_text;
    bool_t                  valid_request;
    bool_t                  valid_start;
    bool_t                  valid_capacity;
    bool_t                  can_encode;
    uint32_t                written;
    double                  milliseconds;

    LOGASSERT(NULL != config);
    LOGASSERT(NULL != sessionId);
    LOGASSERT(NULL != url);
    LOGASSERT(NULL != type);
    LOGASSERT(NULL != streamType);
    LOGASSERT(NULL != output);

    load.sessionId.data        = sessionId;
    load.sessionId.byteCount   = Utl_Unicode_GetUtf8StringByteCount(sessionId, SESSION_ID_CAPACITY);
    load.contentId.data        = url;
    load.contentId.byteCount   = Utl_Unicode_GetUtf8StringByteCount(url, sizeof(config->contentUrl));
    load.contentType.data      = type;
    load.contentType.byteCount = Utl_Unicode_GetUtf8StringByteCount(type, sizeof(config->contentType));
    load.title.data            = config->title;
    load.title.byteCount       = Utl_Unicode_GetUtf8StringByteCount(config->title, sizeof(config->title));
    load.autoplay              = autoplay;
    load.subtitle              = NULL;
    stream_length              = Utl_Unicode_GetUtf8StringByteCount(streamType, sizeof(LOAD_STREAM_BUFFERED));
    is_buffered                = FALSE;
    is_live                    = FALSE;
    is_none                    = FALSE;
    if (sizeof(LOAD_STREAM_BUFFERED) - 1 == stream_length)
    {
        is_buffered = (0 == CRT_Memory_Compare(streamType, LOAD_STREAM_BUFFERED, stream_length));
    }
    if (sizeof(LOAD_STREAM_LIVE) - 1 == stream_length)
    {
        is_live = (0 == CRT_Memory_Compare(streamType, LOAD_STREAM_LIVE, stream_length));
        is_none = (0 == CRT_Memory_Compare(streamType, LOAD_STREAM_NONE, stream_length));
    }
    load.stream = UTL_CASTJSON_STREAM_NONE;
    if (is_buffered)
    {
        load.stream = UTL_CASTJSON_STREAM_BUFFERED;
    }
    if (is_live)
    {
        load.stream = UTL_CASTJSON_STREAM_LIVE;
    }
    valid_stream = is_buffered || is_live || is_none;

    subtitle.contentId.data      = config->subtitleUrl;
    subtitle.contentId.byteCount = Utl_Unicode_GetUtf8StringByteCount(config->subtitleUrl, sizeof(config->subtitleUrl));
    has_subtitle                 = (0 < subtitle.contentId.byteCount);
    valid_subtitle               = (UTL_UNICODE_NO_TERMINATOR != subtitle.contentId.byteCount);
    if (has_subtitle && valid_subtitle)
    {
        subtitle.trackId        = LOAD_SUBTITLE_TRACK_ID;
        subtitle.name.data      = LOAD_SUBTITLE_NAME;
        subtitle.name.byteCount = sizeof(LOAD_SUBTITLE_NAME) - 1;
        subtitle.language.data  = config->subtitleLanguage;
        subtitle.language.byteCount =
            Utl_Unicode_GetUtf8StringByteCount(config->subtitleLanguage, sizeof(config->subtitleLanguage));
        valid_subtitle = (UTL_UNICODE_NO_TERMINATOR != subtitle.language.byteCount);
        if (0 == subtitle.language.byteCount)
        {
            subtitle.language.data      = LOAD_DEFAULT_LANGUAGE;
            subtitle.language.byteCount = sizeof(LOAD_DEFAULT_LANGUAGE) - 1;
        }
        load.subtitle = &subtitle;
    }

    valid_text = (UTL_UNICODE_NO_TERMINATOR != load.sessionId.byteCount) &&
                     (UTL_UNICODE_NO_TERMINATOR != load.contentId.byteCount) &&
                     (UTL_UNICODE_NO_TERMINATOR != load.contentType.byteCount) &&
                     (UTL_UNICODE_NO_TERMINATOR != load.title.byteCount);
    valid_request  = (0 <= requestId);
    valid_start    = (-DBL_MAX <= start) && (start < LOAD_MILLISECONDS_LIMIT / UTL_CASTJSON_MILLISECONDS_PER_SECOND);
    valid_capacity = (0 < capacity);
    can_encode     = valid_stream && valid_subtitle && valid_text && valid_request && valid_start && valid_capacity;
    written        = 0;
    if (can_encode)
    {
        if (0.0 > start)
        {
            start = 0.0;
        }
        milliseconds = start * UTL_CASTJSON_MILLISECONDS_PER_SECOND + 0.5;
        valid_start  = (milliseconds < LOAD_MILLISECONDS_LIMIT);
        if (valid_start)
        {
            load.requestId         = (uint32_t)requestId;
            load.startMilliseconds = (uint64_t)milliseconds;
            written                = Utl_CastJson_WriteLoad(&load, output, capacity - 1);
            if (0 < written)
            {
                output[written] = 0;
            }
        }
    }

    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The proxy worker publishes one coherent pair without waiting for the runner.
 *  A suspended consumer retains its previous slot until it takes a new pair.
 */
void CastSession_SetProxy(CastSession_t* owner, const char* pUrl, const char* pContentType)
{
    LOGASSERT(NULL != owner);
    LOGASSERT(CastSession_IsPrepared(owner));

    CastMedia_Publish(&owner->state->proxyUpdates, pUrl, pContentType);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The GUI owner publishes into its retained channel; the runner takes the latest
 *  complete pair once the receiver is ready for a source change. No lock is acquired.
 */
void CastSession_ReplaceMedia(CastSession_t* owner, const char* url, const char* contentType)
{
    LOGASSERT(NULL != owner);
    LOGASSERT(CastSession_IsPrepared(owner));

    CastMedia_Publish(&owner->state->replacementUpdates, url, contentType);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The wrapper publishes runner ownership on every internal return path.
 *  The caller still acquires its worker completion before reset or storage closure.
 */
int32_t CastSession_Run(CastSession_t* session)
{
    bool_t   prepared;
    uint32_t previous;
    int32_t  result;

    LOGASSERT(NULL != session);

    prepared = CastSession_IsPrepared(session);
    LOGASSERT(prepared);
    previous = OS_Atomic_CompareExchange32(&session->running, SESSION_RUN_IDLE, SESSION_RUN_ACTIVE);
    LOGASSERT(SESSION_RUN_IDLE == previous);
    result = RunSession(session->state);
    OS_Atomic_Store32(&session->running, SESSION_RUN_COMPLETE);

    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Serializes access to the command queue and returns whether the queue accepted
 *  the command.
 */
bool_t CastSession_Post(CastSession_t* owner, CastCommand command, double seconds)
{
    CastSession_State_t* pSession;
    bool_t               accepted;

    LOGASSERT(NULL != owner);
    LOGASSERT(CastSession_IsPrepared(owner));

    pSession = owner->state;
    AcquireSRWLockExclusive(&pSession->commandLock);
    accepted = CastCommands_Push(&pSession->commands, command, seconds);
    ReleaseSRWLockExclusive(&pSession->commandLock);

    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Callback and borrowed configuration lifetime ends only after the owner's
 *  quiescence obligation, not merely when a status event reports completion.
 */
void CastSession_Reset(CastSession_t* session)
{
    bool_t running;
    bool_t has_state;

    LOGASSERT(NULL != session);

    running = CastSession_IsRunning(session);
    LOGASSERT(!running);
    has_state = (NULL != session->state);
    if (has_state)
    {
        LOGASSERT(NULL == session->state->pChannel);
    }
    session->prepared = FALSE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The wrapper remains live outside the released region, so successful release
 *  can clear its pointer safely. Memory errors remain available on the retained owner.
 */
bool_t CastSession_TryClose(CastSession_t* session)
{
    bool_t running;
    bool_t prepared;
    bool_t can_close;
    bool_t closed;

    LOGASSERT(NULL != session);

    running   = CastSession_IsRunning(session);
    prepared  = CastSession_IsPrepared(session);
    can_close = (!running) && (!prepared);
    closed    = FALSE;
    if (can_close)
    {
        closed = OS_Memory_TryClose(&session->memory);
        if (closed)
        {
            session->state = NULL;
        }
    }

    return closed;
}
