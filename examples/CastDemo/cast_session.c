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

#include <stdint.h>
#include <Windows.h> // IWYU pragma: keep
#include <synchapi.h>
#include <minwindef.h>
#include <sysinfoapi.h>
#include <winnt.h>

#include <float.h>
#include <stdarg.h>
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
#include "crt/crt_format.h"
#include "utl/utl_chromecast.h"
#include "utl/utl_castv2.h"
#include "utl/utl_json.h"
#include "os/os_types.h"
#include "utl/utl_unicode.h"
#include "cvk/cvk_json.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/


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
#define JSON_VALUE_CAPACITY     (4096)
#define LOAD_PAYLOAD_CAPACITY     (20000)
#if LOAD_PAYLOAD_CAPACITY > UINT16_MAX
#error Cast payload count exceeds uint16_t
#endif
#define SESSION_RUN_IDLE          (0)
#define SESSION_RUN_ACTIVE        (1)
#define SESSION_RUN_COMPLETE      (2)
#define SESSION_ID_CAPACITY       (128)
#define SESSION_STATUS_TEXT_CAPACITY (128)
#define LOAD_SUBTITLE_FOREGROUND  (0xFFFFFFFF)
#define LOAD_SUBTITLE_BACKGROUND  (0x00000000)
#define LOAD_SUBTITLE_EDGE_COLOR  (0x000000FF)
#define LOAD_SUBTITLE_FONT_SCALE  (10)
#define LOAD_SUBTITLE_FONT_DIGITS (1)
#define LOAD_SUBTITLE_TRACK_ID    (1)
#define LOAD_SUBTITLE_NAME        "Subtitles"
#define LOAD_DEFAULT_LANGUAGE     "und"
#define LOAD_STREAM_NONE          "NONE"
#define LOAD_STREAM_BUFFERED      "BUFFERED"
#define LOAD_STREAM_LIVE          "LIVE"
#define LOAD_MILLISECONDS_LIMIT   (18446744073709551616.0) /* Exclusive uint64_t conversion limit. */
#define SEEK_FRACTION_DIGITS      (3)
#define LEAVE_DEADLINE_MS         (1000)
#define LEAVE_RECEIVE_MS          (200)
#define SESSION_TIME_ROUND_HALF   (0.5)
#define SESSION_LOG_CAPACITY      (1600)
#define SESSION_STATUS_NAME_CAPACITY (32)
#define SESSION_ERROR_REASON_CAPACITY (128)
#define SESSION_CHANNEL_ERROR_CAPACITY (256)
#define SESSION_SEEK_MOTION_TOLERANCE (0.30)
#define SESSION_PLAY_AFTER_SEEK_TIMEOUT_MS (3000)

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Complete result of deriving an application time in milliseconds.
 */
typedef struct MillisecondPosition_s
{
    bool_t   valid;        ///< TRUE only when the input belongs to the supported finite domain.
    uint64_t milliseconds; ///< Rounded result when valid; zero otherwise.
} MillisecondPosition_t;

/*************************************************************************************************/
/**
 * \brief One application seek intent before schema conversion and transmission.
 */
typedef struct SeekRequest_s
{
    float64_t seconds; ///< Requested timeline position before clamping.
    bool_t    resume;  ///< TRUE resumes playback; FALSE pauses at the target.
} SeekRequest_t;

/*************************************************************************************************/
/**
 * \brief Complete preflight result for an escaped receiver identifier.
 */
typedef struct ReceiverIdentifier_s
{
    Utl_Json_Text_t encoded;   ///< Borrowed encoded JSON string body.
    uint32_t        byteCount; ///< Exact decoded extent when valid.
    bool_t          valid;     ///< Nonempty scalar UTF-8 fitting retained storage.
} ReceiverIdentifier_t;

typedef struct CastSession_State_s
{
    CastMedia_Channel_t proxyUpdates;                        ///< Proxy worker to runner, latest complete pair.
    CastMedia_Channel_t replacementUpdates;                  ///< GUI owner to runner, latest complete pair.
    const CastMedia_t*  proxy;                               ///< Runner borrow, stable until its next proxy Take.
    CastSessionConfig   config;
    CastChannel_Workspace_t channel;
    CastChannel*        pChannel;
    byte_t              loadPayload[LOAD_PAYLOAD_CAPACITY];
    cvkJsonValue        jsonValues[JSON_VALUE_CAPACITY];     ///< Startup-owned response storage, exclusive to the runner.
    uint32_t            requestId;
    byte_t              sessionBytes[SESSION_ID_CAPACITY];   ///< Retained decoded receiver session bytes.
    Utl_Json_Text_t     sessionId;                           ///< Counted borrow of sessionBytes while joined.
    byte_t              transportBytes[SESSION_ID_CAPACITY]; ///< Retained decoded receiver transport bytes.
    Utl_Json_Text_t     transportId;                         ///< Counted borrow of transportBytes while joined.
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
 * \par Details:
 *  One idle cast is prepared.
 *
 * \par Requirements:
 *  - 001 The owner shall exclude runners and publishers and retain the startup region.
 *  - 002 Configuration storage shall be disjoint; referenced keyframes and callback context shall remain alive until Reset.
 *  - 003 Every control field and readable string shall be initialized without clearing unused capacity or allocating.
 *
 * \param[out] state    Required persistent implementation storage
 * \param[in]  config   Required validated caller configuration
 *
 * \retval     None No value is returned.
 */
static FORCE_INLINE void InitializeState(CastSession_State_t* state, const CastSessionConfig* config);

/*************************************************************************************************/
/**
 * \brief Gets the fixed platform receiver endpoint.
 *
 * \par Requirements:
 *  - 001 The returned counted borrow shall refer to immutable program storage.
 *  - 002 No scan, allocation or native call shall occur.
 *
 *
 * \retval endpoint Counted receiver endpoint.
 */
static FORCE_INLINE Utl_Json_Text_t GetReceiverEndpoint(void);

/*************************************************************************************************/
/**
 * \brief Derives the decoded extent of a required receiver identifier.
 *
 * \par Requirements:
 *  - 001 A present value shall be an unchanged record from the current complete receiver document.
 *  - 002 Missing, non-string, malformed, empty or oversized identifiers shall produce an invalid result.
 *  - 003 Encoded input shall remain borrowed; no output storage shall change.
 *
 * \param[in] value   Optional receiver record
 *
 * \retval result Complete validation result with a counted encoded borrow.
 */
static FORCE_INLINE ReceiverIdentifier_t GetReceiverIdentifier(const cvkJsonValue* value);

/*************************************************************************************************/
/**
 * \brief Compares a received envelope namespace with a protocol namespace.
 *
 * \par Requirements:
 *  - 001 The envelope shall be a valid unchanged CastV2 model with live borrowed routing bytes.
 *  - 002 Only an exact counted match to a supported namespace shall succeed.
 *
 * \param[in] message   Required borrowed envelope
 * \param[in] kind      Expected protocol namespace
 *
 * \retval TRUE Equal counted namespace.
 * \retval FALSE Unsupported or different namespace.
 */
static FORCE_INLINE bool_t IsNamespace(const Utl_CastV2_Message_t* message, Utl_Chromecast_Namespace_t kind);

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
 * \brief Determines whether an ordinary media command can address settled media.
 *
 * \par Requirements:
 *  - 001 The runner shall own the initialized state.
 *  - 002 A media ID, transport endpoint and absence of a pending LOAD shall be required.
 *
 * \param[in] session   Required runner-owned state
 *
 * \retval    TRUE    Media commands may be sent.
 * \retval    FALSE   No addressable settled media is available.
 */
static FORCE_INLINE bool_t CanSendMediaCommand(const CastSession_State_t* session);

/*************************************************************************************************/
/**
 * \brief Rounds an application time to its nonnegative millisecond send domain.
 *
 * \par Requirements:
 *  - 001 The complete returned result shall be initialized on every path.
 *  - 002 Negative finite times shall clamp to zero; nonfinite or oversized times shall fail.
 *  - 003 Rounding shall occur only after a floating-point range check; invalid results shall carry zero milliseconds.
 *
 * \param[in] seconds   Application timeline position
 *
 * \retval    result   Valid rounded milliseconds or an explicit invalid result.
 */
static FORCE_INLINE MillisecondPosition_t GetMilliseconds(float64_t seconds);

/*************************************************************************************************/
/**
 * \brief Sends a counted serialized payload through a borrowed CastV2 envelope.
 *
 * \par Requirements:
 *  - 001 The runner shall own a live channel and retain routing names and payload through the send.
 *  - 002 The payload shall contain byteCount readable bytes within the CastV2 body limit.
 *  - 003 No payload terminator or string-length scan shall be required.
 *
 * \param[in,out] session       Required runner-owned session
 * \param[in]     nameSpace     Supported protocol namespace
 * \param[in]     destination   Counted destination endpoint
 * \param[in]     payload       Counted serialized UTF-8 bytes
 * \param[in]     byteCount     Exact payload extent
 *
 * \retval        TRUE    The complete message was sent.
 * \retval        FALSE   Framing or native transport failed.
 */
static FORCE_INLINE bool_t SendJson(CastSession_State_t*       session,
                                    Utl_Chromecast_Namespace_t nameSpace,
                                    Utl_Json_Text_t            destination,
                                    const byte_t*              payload,
                                    uint16_t                   byteCount);

/*************************************************************************************************/
/**
 * \brief Serializes one typed command into the runner's retained byte buffer.
 *
 * \par Requirements:
 *  - 001 The runner shall retain the channel, routing names and command model through the send.
 *  - 002 Invalid fields or insufficient retained capacity shall fail before transport begins.
 *  - 003 Protocol grammar shall belong to utl_chromecast and utl_json; output shall remain counted bytes.
 *
 * \param[in,out] session       Required runner-owned session
 * \param[in]     destination   Counted destination endpoint
 * \param[in]     command       Stable borrowed typed model
 *
 * \retval        TRUE    The complete message was sent.
 * \retval        FALSE   Validation, capacity, framing or native transport failed.
 */
static FORCE_INLINE bool_t SendCommand(CastSession_State_t*            session,
                                       Utl_Json_Text_t                 destination,
                                       const Utl_Chromecast_Command_t* command);

/*************************************************************************************************/
/**
 * \brief Sends a typed connection or heartbeat message.
 *
 * \par Requirements:
 *  - 001 The runner shall own a live channel and the retained serialization buffer.
 *  - 002 Kind shall be CONNECT, PING or PONG; caller misuse shall assert.
 *
 * \param[in,out] session       Required runner-owned state
 * \param[in]     destination   Counted destination endpoint
 * \param[in]     kind          Type-only schema
 *
 * \retval        TRUE    Complete command sent.
 * \retval        FALSE   Serialization or transport failed.
 */
static FORCE_INLINE bool_t SendSimpleCommand(CastSession_State_t*         session,
                                             Utl_Json_Text_t              destination,
                                             Utl_Chromecast_CommandKind_t kind);

/*************************************************************************************************/
/**
 * \brief Takes a unique positive request identifier for the current conversation.
 *
 * \par Requirements:
 *  - 001 Only the runner shall mutate the initialized correlation sequence.
 *  - 002 Exhaustion shall return zero without signed overflow or identifier reuse.
 *
 * \param[in,out] session   Required runner-owned state
 *
 * \retval        positive   New correlation identifier.
 * \retval        0          Signed application sequence is exhausted.
 */
static FORCE_INLINE int32_t TakeRequestId(CastSession_State_t* session);

/*************************************************************************************************/
/**
 * \brief Sends an ordinary typed media command when media is addressable.
 *
 * \par Requirements:
 *  - 001 Kind shall be PLAY, PAUSE, STOP_MEDIA or GET_MEDIA_STATUS; misuse shall assert.
 *  - 002 Missing media, missing transport or an in-flight LOAD shall suppress the command successfully.
 *  - 003 Active commands shall use a unique request ID and retained serialization storage.
 *
 * \param[in,out] session   Required runner-owned state
 * \param[in]     kind      Supported media command schema
 *
 * \retval        TRUE    Command sent or intentionally suppressed during transition.
 * \retval        FALSE   Correlation, serialization or transport failure.
 */
static FORCE_INLINE bool_t SendMediaCommand(CastSession_State_t* session, Utl_Chromecast_CommandKind_t kind);

/*************************************************************************************************/
/**
 * \brief Sends a bounded SEEK and then publishes its pending confirmation state.
 *
 * \par Requirements:
 *  - 001 The runner shall own initialized state; unavailable media shall suppress the command successfully.
 *  - 002 Nonfinite times shall fail; finite times shall clamp to the known timeline and round to milliseconds.
 *  - 003 Position and seek confirmation state shall change only after a successful complete send.
 *
 * \param[in,out] session   Required runner-owned state
 * \param[in]     request   Required borrowed position and playback intent
 *
 * \retval        TRUE    Command sent or suppressed during media transition.
 * \retval        FALSE   Time, correlation, serialization or transport failure.
 */
static FORCE_INLINE bool_t Seek(CastSession_State_t* session, const SeekRequest_t* request);

/*************************************************************************************************/
/**
 * \brief Sends the newest queued seek once loading and rate-limit constraints permit it.
 *
 * \par Requirements:
 *  - 001 The runner shall own initialized session state and its queued seek.
 *  - 002 At most one coalesced seek shall be attempted; a failed send shall retain the queued request.
 *
 * \param[in,out] session   Required runner-owned state
 * \param[in]     now       Current monotonic milliseconds
 *
 * \retval        TRUE    Nothing is due or the queued seek was sent.
 * \retval        FALSE   The due seek failed.
 */
static FORCE_INLINE bool_t FlushSeek(CastSession_State_t* session, uint64_t now);

/*************************************************************************************************/
/**
 * \brief Connects to one validated receiver application before publishing its route.
 *
 * \par Requirements:
 *  - 001 The runner shall own an unjoined session and a stable complete receiver document.
 *  - 002 Both required identifiers shall pass Unicode and capacity preflight before either is decoded.
 *  - 003 Only successful virtual connection transmission shall publish identifiers, joined state and LOAD deadline.
 *
 * \param[in,out] session       Required runner-owned state
 * \param[in]     document      Required current receiver document
 * \param[in]     application   Required receiver application object
 *
 * \retval TRUE Application route published.
 * \retval FALSE Invalid identifiers or failed virtual connection; route remains unpublished.
 */
static FORCE_INLINE bool_t JoinReceiver(CastSession_State_t*   session,
                                        const cvkJsonDocument* document,
                                        const cvkJsonValue*    application);

/*************************************************************************************************/
/**
 * \brief Handles application presence in a receiver status message.
 *
 * \par Requirements:
 *  - 001 The runner shall own initialized conversation state and the complete borrowed document.
 *  - 002 A present applications member shall be an array; malformed status shall fail without publishing a route.
 *  - 003 Missing application before joining shall remain pending; its disappearance after joining shall end the conversation.
 *
 * \param[in,out] session    Required runner-owned state
 * \param[in]     document   Required complete receiver document
 * \param[in]     root       Required receiver response object
 *
 * \retval TRUE Continue the receiver conversation.
 * \retval FALSE Invalid status, closed application or failed connection.
 */
static FORCE_INLINE bool_t OnReceiverStatus(CastSession_State_t*   session,
                                            const cvkJsonDocument* document,
                                            const cvkJsonValue*    root);

/*************************************************************************************************/
/**
 * \brief Requests application shutdown and drains replies until the bounded deadline.
 *
 * \par Details:
 *  Request/drain attempt finished; channel cleanup remains with the runner.
 *
 * \par Requirements:
 *  - 001 The runner shall own the channel and stable counted application session ID.
 *  - 002 Missing session/channel or failed command transmission shall leave without waiting.
 *  - 003 The receiver session ID shall be escaped by the schema writer, never interpolated into JSON.
 *
 * \param[in,out] pSession   Required runner-owned state
 *
 * \retval        None No value is returned.
 */
static void Leave(CastSession_State_t* pSession);

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
 *  Command reset discards prior Stop and positions. Zero identifier counts
 *  keep retained bytes unpublished until a validated connection succeeds.
 */
static FORCE_INLINE void InitializeState(CastSession_State_t* state, const CastSessionConfig* config)
{
    CRT_Memory_Copy(&state->config, config, sizeof(CastSessionConfig));
    state->pChannel              = NULL;
    state->loadPayload[0]        = 0;
    state->requestId             = 0;
    state->sessionId.data        = state->sessionBytes;
    state->sessionId.byteCount   = 0;
    state->transportId.data      = state->transportBytes;
    state->transportId.byteCount = 0;
    state->joined                = FALSE;
    state->loadPending           = FALSE;
    state->loadDeadline          = 0;
    state->mediaSessionId        = 0;
    state->status.state          = CAST_STATE_CONNECTING;
    state->status.stateName[0]   = 0;
    state->status.currentTime    = 0.0;
    state->status.duration       = 0.0;
    state->status.hasMedia       = FALSE;
    state->finished              = FALSE;
    state->seekQueued            = FALSE;
    state->seekQueuedSeconds     = 0.0;
    state->seekQueuedResume      = FALSE;
    state->lastSeekTick          = 0;
    state->seekPending           = FALSE;
    state->seekTarget            = 0.0;
    state->seekPendingSince      = 0;
    state->wantPlayAfterSeek     = FALSE;
    state->playAfterSeekSince    = 0;
    state->scrubbing             = FALSE;
    state->playingBeforeScrub    = FALSE;
    state->resumeSince           = 0;
    state->measurePending        = FALSE;
    state->measureTarget         = 0.0;
    state->measureSince          = 0;
    state->measureRebuffered     = FALSE;
    state->onProxy               = FALSE;
    state->switchedToProxy       = FALSE;
    state->scrubRunSince         = 0;
    state->lastActivityTick      = 0;
    state->settlePending         = FALSE;
    state->playingWanted         = FALSE;
    state->awaitingLoad          = FALSE;
    state->preLoadSessionId      = 0;
    state->proxy                 = NULL;
    CastMedia_Initialize(&state->proxyUpdates);
    CastMedia_Initialize(&state->replacementUpdates);
    InitializeSRWLock(&state->commandLock);
    CastCommands_Reset(&state->commands);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE Utl_Json_Text_t GetReceiverEndpoint(void)
{
    Utl_Json_Text_t endpoint;

    endpoint.data      = (const byte_t*)RECEIVER_ID;
    endpoint.byteCount = sizeof(RECEIVER_ID) - 1;
    return endpoint;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The legacy document supplies an encoded string body; utl_json owns escape decoding.
 */
static FORCE_INLINE ReceiverIdentifier_t GetReceiverIdentifier(const cvkJsonValue* value)
{
    ReceiverIdentifier_t identifier;
    bool_t               has_value;
    bool_t               is_string;
    bool_t               decoded;
    bool_t               nonempty;
    bool_t               fits;

    identifier.encoded.data      = NULL;
    identifier.encoded.byteCount = 0;
    identifier.byteCount         = 0;
    identifier.valid             = FALSE;
    has_value                    = (NULL != value);
    if (has_value)
    {
        is_string = (CVK_JSON_STRING == value->kind);
        if (is_string)
        {
            identifier.encoded.data      = (const byte_t*)value->pText;
            identifier.encoded.byteCount = value->textLength;
            decoded                      = Utl_Json_GetDecodedByteCount(&identifier.byteCount, identifier.encoded);
            nonempty                     = (0 < identifier.byteCount);
            fits                         = (identifier.byteCount <= SESSION_ID_CAPACITY);
            identifier.valid             = decoded && nonempty && fits;
        }
    }
    return identifier;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  No routing byte is interpreted as a terminator.
 */
static FORCE_INLINE bool_t IsNamespace(const Utl_CastV2_Message_t* message, Utl_Chromecast_Namespace_t kind)
{
    Utl_Json_Text_t expected;
    bool_t          supported;
    bool_t          same_count;
    bool_t          can_compare;
    bool_t          matches;

    LOGASSERT(NULL != message);
    expected    = Utl_Chromecast_GetNamespace(kind);
    supported   = (0 < expected.byteCount);
    same_count  = (message->nameSpace.byteCount == expected.byteCount);
    can_compare = supported && same_count;
    matches     = FALSE;
    if (can_compare)
    {
        matches = (0 == CRT_Memory_Compare(message->nameSpace.data, expected.data, expected.byteCount));
    }
    return matches;
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
 *  None
 */
static FORCE_INLINE bool_t CanSendMediaCommand(const CastSession_State_t* session)
{
    bool_t has_media;
    bool_t has_transport;
    bool_t is_settled;
    bool_t can_send;

    has_media     = (0 != session->mediaSessionId);
    has_transport = (0 < session->transportId.byteCount);
    is_settled    = !session->awaitingLoad;
    can_send      = has_media && has_transport && is_settled;
    return can_send;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Both the seconds value and the rounded product are checked before narrowing.
 *  The application retains its existing three-decimal wire-time precision.
 */
static FORCE_INLINE MillisecondPosition_t GetMilliseconds(float64_t seconds)
{
    bool_t                meets_minimum;
    bool_t                below_limit;
    MillisecondPosition_t position;
    float64_t             rounded;

    meets_minimum         = (-DBL_MAX <= seconds);
    below_limit           = (seconds < LOAD_MILLISECONDS_LIMIT / UTL_CHROMECAST_MILLISECONDS_PER_SECOND);
    position.valid        = meets_minimum && below_limit;
    position.milliseconds = 0;
    if (position.valid)
    {
        if (0.0 > seconds)
        {
            seconds = 0.0;
        }
        rounded        = (seconds * UTL_CHROMECAST_MILLISECONDS_PER_SECOND) + SESSION_TIME_ROUND_HALF;
        position.valid = (rounded < LOAD_MILLISECONDS_LIMIT);
        if (position.valid)
        {
            position.milliseconds = (uint64_t)rounded;
        }
    }
    return position;
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
    char    text[SESSION_LOG_CAPACITY];

    text[0] = '\0';
    va_start(arguments, pFormat);
    (void)CRT_Format_VAppend(text, (uint32_t)sizeof(text), pFormat, &arguments);
    va_end(arguments);
    pSession->config.callback(pSession->config.pUser, CAST_EVENT_LOG, NULL, text);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Passes the current status snapshot to the configured event callback.
 */
static FORCE_INLINE void EmitStatus(CastSession_State_t* pSession)
{
    pSession->config.callback(pSession->config.pUser, CAST_EVENT_STATUS, &pSession->status, NULL);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Updates the state enum and display name, then publishes the status.
 */
static FORCE_INLINE void SetPhase(CastSession_State_t* pSession, CastPlayerState state, const char* pName)
{
    pSession->status.state = state;
    (void)CRT_Format_Copy(pSession->status.stateName, (uint32_t)sizeof(pSession->status.stateName), pName);
    EmitStatus(pSession);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Borrows a JSON payload into a Cast envelope, optionally logs it and sends it
 *  through the channel. Transport failure is reported through the session log.
 */
static FORCE_INLINE bool_t SendJson(CastSession_State_t*       session,
                                    Utl_Chromecast_Namespace_t nameSpace,
                                    Utl_Json_Text_t            destination,
                                    const byte_t*              payload,
                                    uint16_t                   byteCount)
{
    Utl_Json_Text_t      namespace_text;
    Utl_CastV2_Message_t message;
    bool_t               sent;
    CastChannel*         channel;
    const char*          channelError;

    namespace_text = Utl_Chromecast_GetNamespace(nameSpace);
    LOGASSERT(0 < namespace_text.byteCount);
    LOGASSERT(0 < destination.byteCount);
    message.source.data           = (const byte_t*)SENDER_ID;
    message.source.byteCount      = sizeof(SENDER_ID) - 1;
    message.destination.data      = destination.data;
    message.destination.byteCount = destination.byteCount;
    message.nameSpace.data        = namespace_text.data;
    message.nameSpace.byteCount   = namespace_text.byteCount;
    message.payloadType           = UTL_CASTV2_STRING;
    message.payload.data          = payload;
    message.payload.byteCount     = byteCount;
    if (session->config.verbose)
    {
        Log(session,
            ">> %.*s -> %.*s %.*s",
            (int32_t)namespace_text.byteCount,
            (const char_t*)namespace_text.data,
            (int32_t)destination.byteCount,
            (const char_t*)destination.data,
            (int32_t)byteCount,
            (const char_t*)payload);
    }
    channel = session->pChannel;
    sent = CastChannel_Send(channel, &message);
    channelError = CastChannel_GetError(channel);
    if (!sent)
    {
        Log(session, "cast: %s", channelError);
    }
    return sent;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The schema writer preflights complete commands. Retained payload storage is
 *  exclusively runner-owned; the send consumes it before the next serialization.
 */
static FORCE_INLINE bool_t SendCommand(CastSession_State_t*            session,
                                       Utl_Json_Text_t                 destination,
                                       const Utl_Chromecast_Command_t* command)
{
    uint32_t                   byte_count;
    bool_t                     encoded;
    bool_t                     sent;
    Utl_Chromecast_Namespace_t name_space;

    byte_count = Utl_Chromecast_WriteCommand(command, session->loadPayload, LOAD_PAYLOAD_CAPACITY);
    encoded    = (0 < byte_count);
    sent       = FALSE;
    if (encoded)
    {
        name_space = Utl_Chromecast_GetCommandNamespace(command->kind);
        sent       = SendJson(session, name_space, destination, session->loadPayload, (uint16_t)byte_count);
    }
    return sent;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Type-only schemas inspect no inactive command data and need no request ID.
 */
static FORCE_INLINE bool_t SendSimpleCommand(CastSession_State_t*         session,
                                             Utl_Json_Text_t              destination,
                                             Utl_Chromecast_CommandKind_t kind)
{
    bool_t                   is_connect;
    bool_t                   is_ping;
    bool_t                   is_pong;
    bool_t                   is_supported;
    Utl_Chromecast_Command_t command;
    bool_t                   sent;

    is_connect   = (UTL_CHROMECAST_COMMAND_CONNECT == kind);
    is_ping      = (UTL_CHROMECAST_COMMAND_PING == kind);
    is_pong      = (UTL_CHROMECAST_COMMAND_PONG == kind);
    is_supported = is_connect || is_ping || is_pong;
    LOGASSERT(is_supported);
    command.kind = kind;
    sent         = SendCommand(session, destination, &command);
    return sent;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Exhaustion is a normal send failure, never signed overflow or silent reuse.
 */
static FORCE_INLINE int32_t TakeRequestId(CastSession_State_t* session)
{
    bool_t  has_next;
    int32_t request;

    has_next = ((uint32_t)INT32_MAX > session->requestId);
    request  = 0;
    if (has_next)
    {
        session->requestId += 1U;
        request = (int32_t)session->requestId;
    }
    return request;
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
    uint32_t byte_count;
    bool_t   is_valid;

    byte_count = CastSession_WriteLoad(&pSession->config,
                                       pSession->sessionId,
                                       TakeRequestId(pSession),
                                       pUrl,
                                       pType,
                                       startSeconds,
                                       autoplay,
                                       pStreamType,
                                       pSession->loadPayload,
                                       LOAD_PAYLOAD_CAPACITY);
    is_valid = (0 < byte_count);
    if (is_valid)
    {
        is_valid = SendJson(pSession,
                            UTL_CHROMECAST_NAMESPACE_MEDIA,
                            pSession->transportId,
                            pSession->loadPayload,
                            (uint16_t)byte_count);
    }
    if (is_valid)
    {
        pSession->playingWanted    = autoplay;
        pSession->awaitingLoad     = 1; /* the media session id changes; hold seeks until the new one lands */
        pSession->preLoadSessionId = pSession->mediaSessionId;
        pSession->seekPending      = 0;
        SetPhase(pSession, CAST_STATE_LOADING, "LOADING");
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
static FORCE_INLINE bool_t SendMediaCommand(CastSession_State_t* session, Utl_Chromecast_CommandKind_t kind)
{
    bool_t                   is_play;
    bool_t                   is_pause;
    bool_t                   is_stop;
    bool_t                   is_status;
    bool_t                   is_supported;
    bool_t                   can_send;
    bool_t                   sent;
    Utl_Chromecast_Command_t command;

    is_play      = (UTL_CHROMECAST_COMMAND_PLAY == kind);
    is_pause     = (UTL_CHROMECAST_COMMAND_PAUSE == kind);
    is_stop      = (UTL_CHROMECAST_COMMAND_STOP_MEDIA == kind);
    is_status    = (UTL_CHROMECAST_COMMAND_GET_MEDIA_STATUS == kind);
    is_supported = is_play || is_pause || is_stop || is_status;
    LOGASSERT(is_supported);
    can_send = CanSendMediaCommand(session);
    sent     = TRUE;
    if (can_send)
    {
        command.kind      = kind;
        command.requestId = (uint32_t)TakeRequestId(session);
        if (is_status)
        {
            command.data.statusSessionId = &session->mediaSessionId;
        }
        else
        {
            command.data.mediaSessionId = session->mediaSessionId;
        }
        sent = SendCommand(session, session->transportId, &command);
    }
    return sent;
}

/* A paused seek only decodes to the target frame and paints it; a resuming seek also refills
** the playback buffer. Scrubbing sends paused seeks and resumes once at the end. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Validates and clamps the target to the known timeline. Seek confirmation and
 *  display state change only after the complete typed command has been sent.
 */
static FORCE_INLINE bool_t Seek(CastSession_State_t* session, const SeekRequest_t* request)
{
    float64_t                seconds;
    bool_t                   resume;
    bool_t                   can_send;
    bool_t                   sent;
    bool_t                   meets_minimum;
    bool_t                   below_maximum;
    bool_t                   finite;
    bool_t                   has_duration;
    bool_t                   beyond_duration;
    MillisecondPosition_t    position;
    Utl_Chromecast_Command_t command;

    LOGASSERT(NULL != request);
    seconds  = request->seconds;
    resume   = request->resume;
    can_send = CanSendMediaCommand(session);
    sent     = TRUE;
    if (can_send)
    {
        meets_minimum = (-DBL_MAX <= seconds);
        below_maximum = (seconds <= DBL_MAX);
        finite        = meets_minimum && below_maximum;
        if (0.0 > seconds)
        {
            seconds = 0.0;
        }
        has_duration    = (0.0 < session->status.duration);
        beyond_duration = (session->status.duration < seconds);
        if (has_duration && beyond_duration)
        {
            seconds = session->status.duration;
        }
        position = GetMilliseconds(seconds);
        sent     = finite && position.valid;
        if (sent)
        {
            command.kind                                 = UTL_CHROMECAST_COMMAND_SEEK;
            command.requestId                            = (uint32_t)TakeRequestId(session);
            command.data.seek.mediaSessionId             = session->mediaSessionId;
            command.data.seek.currentTime.coefficient    = position.milliseconds;
            command.data.seek.currentTime.fractionDigits = SEEK_FRACTION_DIGITS;
            command.data.seek.resume                     = resume ? UTL_CHROMECAST_RESUME_START : UTL_CHROMECAST_RESUME_PAUSE;
            sent                                         = SendCommand(session, session->transportId, &command);
        }
        if (sent)
        {
            session->lastSeekTick       = GetTickCount64();
            session->seekPending        = TRUE;
            session->seekTarget         = seconds;
            session->seekPendingSince   = session->lastSeekTick;
            session->resumeSince        = resume ? session->lastSeekTick : 0;
            session->wantPlayAfterSeek  = resume;
            session->playAfterSeekSince = session->lastSeekTick;
            if (resume)
            {
                session->measurePending    = TRUE;
                session->measureTarget     = seconds;
                session->measureSince      = session->lastSeekTick;
                session->measureRebuffered = FALSE;
            }
            /* Publish a new target only after its complete command was sent. */
            session->status.currentTime = seconds;
            EmitStatus(session);
        }
    }
    return sent;
}

/* Sends the newest queued scrub position once the rate limit allows. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Sends only the newest queued position after the rate-limit interval and after
 *  an in-flight LOAD has completed.
 */
static FORCE_INLINE bool_t FlushSeek(CastSession_State_t* session, uint64_t now)
{
    bool_t        has_seek;
    bool_t        is_settled;
    bool_t        interval_passed;
    bool_t        can_send;
    bool_t        sent;
    SeekRequest_t request;

    has_seek        = session->seekQueued;
    is_settled      = !session->awaitingLoad;
    interval_passed = (SEEK_INTERVAL_MS <= now - session->lastSeekTick);
    can_send        = has_seek && is_settled && interval_passed;
    sent            = TRUE;
    if (can_send)
    {
        request.seconds = session->seekQueuedSeconds;
        request.resume  = session->seekQueuedResume;
        sent            = Seek(session, &request);
        if (sent)
        {
            session->seekQueued = FALSE;
        }
    }
    return sent;
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
    uint64_t remaining;
    const char* source;
    char* destination;

    LOGASSERT(NULL != pOut);
    LOGASSERT(0 < capacity);

    pOut[0]   = 0;
    has_value = (NULL != pValue);
    if (has_value)
    {
        is_string = (CVK_JSON_STRING == pValue->kind);
        if (is_string)
        {
            source = pValue->pText;
            length = pValue->textLength;
            if (capacity <= length)
            {
                length = capacity - 1;
            }
            destination = pOut;
            remaining   = length;
            for (; 0 < remaining; --remaining)
            {
                *destination = *source;
                ++destination;
                ++source;
            }
            *destination = 0;
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Retained byte arrays are scratch until CONNECT succeeds. No terminator is appended.
 */
static FORCE_INLINE bool_t JoinReceiver(CastSession_State_t*   session,
                                        const cvkJsonDocument* document,
                                        const cvkJsonValue*    application)
{
    ReceiverIdentifier_t session_id;
    ReceiverIdentifier_t transport_id;
    bool_t               identifiers_valid;
    bool_t               joined;
    uint32_t             session_count;
    uint32_t             transport_count;
    bool_t               session_decoded;
    bool_t               transport_decoded;
    bool_t               decoded;
    Utl_Json_Text_t      destination;
    char_t               status_text[SESSION_STATUS_TEXT_CAPACITY];

    LOGASSERT(NULL != session);
    LOGASSERT(NULL != document);
    LOGASSERT(NULL != application);
    LOGASSERT(!session->joined);
    session_id        = GetReceiverIdentifier(cvkGetJsonMember(document, application, "sessionId"));
    transport_id      = GetReceiverIdentifier(cvkGetJsonMember(document, application, "transportId"));
    identifiers_valid = session_id.valid && transport_id.valid;
    joined            = FALSE;
    if (identifiers_valid)
    {
        session_count   = 0;
        transport_count = 0;
        session_decoded = Utl_Json_ReadString(&session_count,
                                              session->sessionBytes,
                                              sizeof(session->sessionBytes),
                                              session_id.encoded);
        transport_decoded = Utl_Json_ReadString(&transport_count,
                                                session->transportBytes,
                                                sizeof(session->transportBytes),
                                                transport_id.encoded);
        decoded = session_decoded && transport_decoded;
        LOGASSERT(decoded);
        LOGASSERT(session_id.byteCount == session_count);
        LOGASSERT(transport_id.byteCount == transport_count);
        destination.data      = session->transportBytes;
        destination.byteCount = transport_count;
        joined                = SendSimpleCommand(session, destination, UTL_CHROMECAST_COMMAND_CONNECT);
        if (joined)
        {
            session->sessionId.byteCount   = session_count;
            session->transportId.byteCount = transport_count;
            session->joined                = TRUE;
            session->loadPending           = TRUE;
            session->loadDeadline          = GetTickCount64() + LOAD_FALLBACK_MS;
            CopyJsonString(cvkGetJsonMember(document, application, "statusText"), status_text, sizeof(status_text));
            Log(session,
                "media receiver connected: %s (session %.*s)",
                status_text,
                (int32_t)session_count,
                (const char_t*)session->sessionBytes);
        }
    }
    else
    {
        Log(session, "media receiver supplied an invalid session or transport identifier");
    }
    return joined;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The application route is published by JoinReceiver only after CONNECT succeeds.
 */
static FORCE_INLINE bool_t OnReceiverStatus(CastSession_State_t*   session,
                                            const cvkJsonDocument* document,
                                            const cvkJsonValue*    root)
{
    const cvkJsonValue* status;
    const cvkJsonValue* applications;
    const cvkJsonValue* application;
    bool_t              has_status;
    bool_t              valid_status;
    bool_t              has_applications;
    bool_t              valid_applications;
    bool_t              accepted;
    uint32_t            index;
    const cvkJsonValue* candidate;
    bool_t              matches;
    bool_t              has_application;

    LOGASSERT(NULL != session);
    LOGASSERT(NULL != document);
    LOGASSERT(NULL != root);
    status       = cvkGetJsonMember(document, root, "status");
    applications = cvkGetJsonMember(document, status, "applications");
    application  = NULL;
    has_status   = (NULL != status);
    valid_status = FALSE;
    if (has_status)
    {
        valid_status = (CVK_JSON_OBJECT == status->kind);
    }
    has_applications   = (NULL != applications);
    valid_applications = TRUE;
    if (has_applications)
    {
        valid_applications = (CVK_JSON_ARRAY == applications->kind);
    }
    accepted = valid_status && valid_applications;
    if (accepted && has_applications)
    {
        for (index = 0; index < applications->childCount; ++index)
        {
            candidate = cvkGetJsonElement(document, applications, index);
            matches =
                (0 != cvkJsonStringEquals(cvkGetJsonMember(document, candidate, "appId"), DEFAULT_MEDIA_RECEIVER));
            if (matches)
            {
                application = candidate;
                break;
            }
        }
    }
    if (accepted)
    {
        has_application = (NULL != application);
        if (has_application)
        {
            if (!session->joined)
            {
                accepted = JoinReceiver(session, document, application);
            }
        }
        else if (session->joined)
        {
            Log(session, "the media receiver was closed on the device");
            accepted = FALSE;
        }
        else
        {
            /* Launch is still pending; retain its existing deadline and state. */
        }
    }
    return accepted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Maps known receiver state names and uses the loading flag to distinguish
 *  the fallback LOADING and IDLE states.
 */
static FORCE_INLINE CastPlayerState GetStateFromName(const char* pName, int loading)
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

static FORCE_INLINE void TrackSeekMotion(CastSession_State_t* pSession, const cvkJsonDocument* pDocument, const cvkJsonValue* pStatus)
{
    const double rawTime = cvkGetJsonDouble(cvkGetJsonMember(pDocument, pStatus, "currentTime"));
    char rawState[SESSION_STATUS_NAME_CAPACITY];

    CopyJsonString(cvkGetJsonMember(pDocument, pStatus, "playerState"), rawState, sizeof(rawState));
    if (pSession->measurePending && 0 == strcmp(rawState, "BUFFERING"))
    {
        pSession->measureRebuffered = 1; /* the "PLAYING" was optimistic; it stalled again */
    }
    /* The eye sees motion only when the reported time actually advances past the target. */
    if (pSession->measurePending && 0 == strcmp(rawState, "PLAYING") &&
        rawTime > pSession->measureTarget + SESSION_SEEK_MOTION_TOLERANCE)
    {
        Log(pSession,
            "  seek->motion %llu ms%s",
            (unsigned long long)(GetTickCount64() - pSession->measureSince),
            pSession->measureRebuffered ? " (re-buffered once)" : "");
        pSession->measurePending = 0;
    }
}

static FORCE_INLINE void ReconcileSeekTime(CastSession_State_t* pSession)
{
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
}

static void ResumeAfterSeek(CastSession_State_t* pSession)
{
    bool_t resumeResolved;

    if (!pSession->wantPlayAfterSeek || pSession->seekPending)
    {
        return;
    }
    resumeResolved = (CAST_STATE_PLAYING == pSession->status.state);
    if (CAST_STATE_PAUSED == pSession->status.state)
    {
        Log(pSession, "seek landed paused; sending PLAY so audio and video keep running");
        (void)SendMediaCommand(pSession, UTL_CHROMECAST_COMMAND_PLAY);
        resumeResolved = TRUE;
    }
    if (!resumeResolved)
    {
        resumeResolved = (GetTickCount64() - pSession->playAfterSeekSince > SESSION_PLAY_AFTER_SEEK_TIMEOUT_MS);
    }
    if (resumeResolved)
    {
        pSession->wantPlayAfterSeek = 0;
    }
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
    char                name[SESSION_STATUS_NAME_CAPACITY];
    char                idleReason[SESSION_STATUS_NAME_CAPACITY];
    char                extendedName[SESSION_STATUS_NAME_CAPACITY];
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
    TrackSeekMotion(pSession, pDocument, pStatus);
    pSession->status.currentTime = cvkGetJsonDouble(cvkGetJsonMember(pDocument, pStatus, "currentTime"));
    ReconcileSeekTime(pSession);
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
        (void)CRT_Format_Copy(name, (uint32_t)sizeof(name), "LOADING");
    }
    changed = (0 != strcmp(name, pSession->status.stateName));
    if (changed && 0 != pSession->resumeSince && 0 == strcmp(name, "PLAYING"))
    {
        Log(pSession,
            "playing again %llu ms after the seek",
            (unsigned long long)(GetTickCount64() - pSession->resumeSince));
        pSession->resumeSince = 0;
    }
    (void)CRT_Format_Copy(pSession->status.stateName, (uint32_t)sizeof(pSession->status.stateName), name);
    pSession->status.state = GetStateFromName(name, 0 == strcmp(name, "LOADING"));
    /* A resuming seek should leave the receiver playing after it lands. */
    ResumeAfterSeek(pSession);
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
    char reason[SESSION_ERROR_REASON_CAPACITY];

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
static int HandleMessage(CastSession_State_t* pSession, const Utl_CastV2_Message_t* pMessage)
{
    cvkJsonDocument     document;
    const cvkJsonValue* pRoot;
    const cvkJsonValue* pType;

    if (pSession->config.verbose)
    {
        Log(pSession,
            "<< %.*s %.*s",
            (int)pMessage->nameSpace.byteCount,
            (const char_t*)pMessage->nameSpace.data,
            (int)pMessage->payload.byteCount,
            (const char_t*)pMessage->payload.data);
    }
    if ((UTL_CASTV2_BINARY == pMessage->payloadType) || 0 == pMessage->payload.byteCount || '{' != pMessage->payload.data[0])
    {
        return 1;
    }
    document.valueCount =
        cvkParseJsonDocument((const char_t*)pMessage->payload.data, pMessage->payload.byteCount, pSession->jsonValues, JSON_VALUE_CAPACITY);
    document.pValues = pSession->jsonValues;
    pRoot            = &pSession->jsonValues[0];
    pType            = cvkGetJsonMember(&document, pRoot, "type");
    if (IsNamespace(pMessage, UTL_CHROMECAST_NAMESPACE_HEARTBEAT))
    {
        if (cvkJsonStringEquals(pType, "PING"))
        {
            return SendSimpleCommand(pSession, GetReceiverEndpoint(), UTL_CHROMECAST_COMMAND_PONG);
        }
        return 1;
    }
    if (IsNamespace(pMessage, UTL_CHROMECAST_NAMESPACE_CONNECTION))
    {
        if (cvkJsonStringEquals(pType, "CLOSE"))
        {
            Log(pSession, "the receiver closed the connection");
            return 0;
        }
        return 1;
    }
    if (IsNamespace(pMessage, UTL_CHROMECAST_NAMESPACE_RECEIVER))
    {
        if (cvkJsonStringEquals(pType, "RECEIVER_STATUS"))
        {
            return OnReceiverStatus(pSession, &document, pRoot);
        }
        if (cvkJsonStringEquals(pType, "LAUNCH_ERROR"))
        {
            char reason[SESSION_ERROR_REASON_CAPACITY];

            CopyJsonString(cvkGetJsonMember(&document, pRoot, "reason"), reason, sizeof(reason));
            Log(pSession, "the receiver refused to launch the media receiver: %s", reason);
            return 0;
        }
        return 1;
    }
    if (IsNamespace(pMessage, UTL_CHROMECAST_NAMESPACE_MEDIA))
    {
        if (cvkJsonStringEquals(pType, "MEDIA_STATUS"))
        {
            return OnMediaStatus(pSession, &document, pRoot);
        }
        if (cvkJsonStringEquals(pType, "LOAD_FAILED") || cvkJsonStringEquals(pType, "LOAD_CANCELLED") ||
            cvkJsonStringEquals(pType, "INVALID_REQUEST") || cvkJsonStringEquals(pType, "ERROR"))
        {
            char type[SESSION_STATUS_NAME_CAPACITY];

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
    bool_t                   has_session;
    bool_t                   has_channel;
    bool_t                   can_leave;
    Utl_Chromecast_Command_t command;
    bool_t                   sent;
    uint64_t                 started;
    uint64_t                 elapsed;
    bool_t                   deadline_reached;
    int32_t                  received;
    Utl_CastV2_Message_t     message;

    has_session = (0 < pSession->sessionId.byteCount);
    has_channel = (NULL != pSession->pChannel);
    can_leave   = has_session && has_channel;
    if (can_leave)
    {
        command.kind           = UTL_CHROMECAST_COMMAND_STOP_APPLICATION;
        command.requestId      = (uint32_t)TakeRequestId(pSession);
        command.data.sessionId = pSession->sessionId;
        sent                   = SendCommand(pSession, GetReceiverEndpoint(), &command);
        if (sent)
        {
            started = GetTickCount64();
            for (;;)
            {
                elapsed          = GetTickCount64() - started;
                deadline_reached = (LEAVE_DEADLINE_MS <= elapsed);
                if (deadline_reached)
                {
                    break;
                }
                received = CastChannel_Receive(pSession->pChannel, &message, LEAVE_RECEIVE_MS);
                if (0 > received)
                {
                    break;
                }
            }
        }
    }
}

/* Records a scrub gesture and coalesces proxy seeks to its newest position. */
static FORCE_INLINE void QueueScrub(CastSession_State_t* pSession, double seconds)
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
        pSession->seekQueuedSeconds = seconds;
        pSession->seekQueuedResume  = 0;
    }
    /* Before the proxy is loaded there is nothing cheap to seek: do NOT scrub the deep full
    ** stream (it bounces the picture and re-buffers). The one-time switch handles it. */
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
    unsigned      processed;
    SeekRequest_t seek_request;

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
        switch ((int)command.command)
        {
        case CAST_COMMAND_PLAY:
            ok = SendMediaCommand(pSession, UTL_CHROMECAST_COMMAND_PLAY);
            break;
        case CAST_COMMAND_PAUSE:
            ok = SendMediaCommand(pSession, UTL_CHROMECAST_COMMAND_PAUSE);
            break;
        case CAST_COMMAND_TOGGLE:
            ok = SendMediaCommand(
                pSession,
                (CAST_STATE_PLAYING == pSession->status.state || CAST_STATE_BUFFERING == pSession->status.state)
                    ? UTL_CHROMECAST_COMMAND_PAUSE
                    : UTL_CHROMECAST_COMMAND_PLAY);
            break;
        case CAST_COMMAND_SCRUB:
            QueueScrub(pSession, command.seconds);
            break;
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
                seek_request.seconds = command.seconds;
                seek_request.resume  = resume;
                ok                   = Seek(pSession, &seek_request);
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
static bool_t ServiceSessionTimers(CastSession_State_t* pSession,
                                   ULONGLONG now,
                                   ULONGLONG* pLastHeartbeat,
                                   ULONGLONG* pLastPoll)
{
    bool_t needs_fast_poll;
    uint64_t poll_interval;
    bool_t poll_due;

    if (pSession->loadPending && now >= pSession->loadDeadline)
    {
        pSession->loadPending = 0;
        if (!Load(pSession))
        {
            return FALSE;
        }
    }
    if (now - *pLastHeartbeat >= HEARTBEAT_MS)
    {
        *pLastHeartbeat = now;
        if (!SendSimpleCommand(pSession, GetReceiverEndpoint(), UTL_CHROMECAST_COMMAND_PING))
        {
            return FALSE;
        }
    }
    needs_fast_poll = pSession->seekPending || pSession->resumeSince ||
                      (CAST_STATE_BUFFERING == pSession->status.state);
    poll_interval = needs_fast_poll ? STATUS_POLL_FAST_MS : STATUS_POLL_MS;
    poll_due = (now - *pLastPoll >= poll_interval);
    if (pSession->mediaSessionId && poll_due)
    {
        *pLastPoll = now;
        if (!SendMediaCommand(pSession, UTL_CHROMECAST_COMMAND_GET_MEDIA_STATUS))
        {
            return FALSE;
        }
    }
    return TRUE;
}

static bool_t ApplyReplacement(CastSession_State_t* pSession)
{
    const CastMedia_t* update;
    bool_t was_playing;

    if (!IsMediaSwitchReady(pSession))
    {
        return TRUE;
    }
    update = CastMedia_Take(&pSession->replacementUpdates);
    if (NULL == update || 0 == update->urlByteCount)
    {
        return TRUE;
    }
    CRT_Memory_Copy(pSession->config.contentUrl, update->url, (uint64_t)update->urlByteCount + 1);
    CRT_Memory_Copy(pSession->config.contentType, update->type, (uint64_t)update->typeByteCount + 1);
    pSession->proxy = NULL;
    pSession->onProxy = FALSE;
    pSession->switchedToProxy = TRUE;
    was_playing = (CAST_STATE_PLAYING == pSession->status.state);
    Log(pSession, "applying audio gain at %.1f s", pSession->status.currentTime);
    return LoadMedia(pSession, pSession->config.contentUrl, pSession->config.contentType,
                     pSession->status.currentTime, was_playing, LOAD_STREAM_BUFFERED);
}

static bool_t SwitchToProxy(CastSession_State_t* pSession)
{
    bool_t was_playing;

    if (NULL == pSession->proxy || pSession->onProxy || pSession->switchedToProxy ||
        !IsMediaSwitchReady(pSession))
    {
        return TRUE;
    }
    was_playing = (CAST_STATE_PLAYING == pSession->status.state);
    Log(pSession, "switching to the low-latency proxy at %.1f s; seeks are instant from here",
        pSession->status.currentTime);
    pSession->onProxy = TRUE;
    pSession->switchedToProxy = TRUE;
    pSession->playingWanted = was_playing;
    return LoadMedia(pSession, pSession->proxy->url, pSession->proxy->type,
                     pSession->status.currentTime, was_playing, LOAD_STREAM_BUFFERED);
}

static int ServiceSessionIteration(CastSession_State_t* pSession,
                                   ULONGLONG now,
                                   ULONGLONG* pLastHeartbeat,
                                   ULONGLONG* pLastPoll,
                                   int* pProxyAnnounced)
{
    const CastMedia_t* update;
    int commands;
    bool_t valid;

    if (!ServiceSessionTimers(pSession, now, pLastHeartbeat, pLastPoll))
    {
        return -1;
    }
    update = CastMedia_Take(&pSession->proxyUpdates);
    if (NULL != update)
    {
        pSession->proxy = update;
    }
    if (NULL != pSession->proxy && !*pProxyAnnounced)
    {
        *pProxyAnnounced = 1;
        Log(pSession, "proxy ready: switching over for instant, buffer-free seeking");
    }
    commands = ProcessCommands(pSession);
    valid = (0 <= commands);
    if (valid)
    {
        valid = FlushSeek(pSession, now);
    }
    if (valid)
    {
        valid = ApplyReplacement(pSession);
    }
    if (valid)
    {
        valid = SwitchToProxy(pSession);
    }
    return valid ? commands : -1;
}

static FORCE_INLINE int32_t RunSession(CastSession_State_t* pSession)
{
    char                     error[SESSION_CHANNEL_ERROR_CAPACITY];
    Utl_Chromecast_Command_t launch;
    bool_t                   connected;
    bool_t                   launched;
    ULONGLONG                lastHeartbeat;
    ULONGLONG                lastPoll;
    CastChannel*             channel;
    const char* pReason        = NULL;
    int         result         = 1;
    int         proxyAnnounced = 0;

    SetPhase(pSession, CAST_STATE_CONNECTING, "CONNECTING");
    pSession->pChannel = CastChannel_Connect(&pSession->channel,
                                             pSession->config.host,
                                             CAST_PORT,
                                             error,
                                             sizeof(error));
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
    launch.kind                         = UTL_CHROMECAST_COMMAND_LAUNCH;
    launch.requestId                    = (uint32_t)TakeRequestId(pSession);
    launch.data.applicationId.data      = (const byte_t*)DEFAULT_MEDIA_RECEIVER;
    launch.data.applicationId.byteCount = sizeof(DEFAULT_MEDIA_RECEIVER) - 1;
    connected                           = SendSimpleCommand(pSession, GetReceiverEndpoint(), UTL_CHROMECAST_COMMAND_CONNECT);
    launched                            = FALSE;
    if (connected)
    {
        launched = SendCommand(pSession, GetReceiverEndpoint(), &launch);
    }
    if (!launched)
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
        Utl_CastV2_Message_t message;
        int                received;
        ULONGLONG          now;
        int                commands;
        const char*        channelError;

        channel = pSession->pChannel;
        received = CastChannel_Receive(channel, &message, RECEIVE_SLICE_MS);
        if (received < 0)
        {
            pReason = CastChannel_GetError(channel);
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
        channelError = CastChannel_GetError(channel);
        commands = ServiceSessionIteration(pSession, now, &lastHeartbeat, &lastPoll, &proxyAnnounced);
        if (commands < 0)
        {
            pReason = channelError;
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
    /* Channel error text belongs to the channel. Preserve it before closing. */
    (void)CRT_Format_Copy(error, (uint32_t)sizeof(error), (NULL != pReason) ? pReason : "Session ended.");
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
 *  Only startup establishes native session and Cast channel storage. Individual casts reuse both.
 *  Native residency/resource failure remains distinguishable from caller misuse.
 */
extern FORCE_INLINE bool_t CastSession_Initialize(CastSession_t* session)
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
        initialized = CastChannel_Initialize(&session->state->channel);
    }

    return initialized;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Preparation spans runner completion until the owner explicitly resets the object.
 */
extern FORCE_INLINE bool_t CastSession_IsPrepared(const CastSession_t* session)
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
extern FORCE_INLINE bool_t CastSession_IsRunning(const CastSession_t* session)
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
extern FORCE_INLINE void CastSession_Prepare(CastSession_t* session, const CastSessionConfig* config)
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
uint32_t CastSession_WriteLoad(const CastSessionConfig* config,
                               Utl_Json_Text_t          sessionId,
                               int32_t                  requestId,
                               const char*              url,
                               const char*              type,
                               double                   start,
                               bool_t                   autoplay,
                               const char*              streamType,
                               byte_t*                  output,
                               uint32_t                 capacity)
{
    Utl_Chromecast_Load_t           load;
    uint32_t                        stream_length;
    bool_t                          is_buffered;
    bool_t                          is_live;
    bool_t                          is_none;
    bool_t                          valid_stream;
    Utl_Chromecast_Subtitle_t       subtitle;
    bool_t                          has_subtitle;
    bool_t                          valid_subtitle;
    Utl_Chromecast_TextTrackStyle_t style;
    bool_t                          valid_content_text;
    bool_t                          valid_type_text;
    bool_t                          valid_title_text;
    bool_t                          valid_text;
    bool_t                          valid_request;
    MillisecondPosition_t           position;
    bool_t                          can_encode;
    uint32_t                        written;

    LOGASSERT(NULL != config);
    LOGASSERT(NULL != url);
    LOGASSERT(NULL != type);
    LOGASSERT(NULL != streamType);
    LOGASSERT(NULL != output);

    load.sessionId             = sessionId;
    load.contentId.data        = (const byte_t*)url;
    load.contentId.byteCount   = Utl_Unicode_GetUtf8StringByteCount(url, sizeof(config->contentUrl));
    load.contentType.data      = (const byte_t*)type;
    load.contentType.byteCount = Utl_Unicode_GetUtf8StringByteCount(type, sizeof(config->contentType));
    load.title.data            = (const byte_t*)config->title;
    load.title.byteCount       = Utl_Unicode_GetUtf8StringByteCount(config->title, sizeof(config->title));
    load.autoplay              = autoplay;
    load.subtitle              = NULL;
    load.textTrackStyle        = NULL;
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
    load.stream = UTL_CHROMECAST_STREAM_NONE;
    if (is_buffered)
    {
        load.stream = UTL_CHROMECAST_STREAM_BUFFERED;
    }
    if (is_live)
    {
        load.stream = UTL_CHROMECAST_STREAM_LIVE;
    }
    valid_stream = is_buffered || is_live || is_none;

    subtitle.contentId.data      = (const byte_t*)config->subtitleUrl;
    subtitle.contentId.byteCount = Utl_Unicode_GetUtf8StringByteCount(config->subtitleUrl, sizeof(config->subtitleUrl));
    has_subtitle                 = (0 < subtitle.contentId.byteCount);
    valid_subtitle               = (UTL_UNICODE_NO_TERMINATOR != subtitle.contentId.byteCount);
    if (has_subtitle && valid_subtitle)
    {
        style.foregroundColor          = LOAD_SUBTITLE_FOREGROUND;
        style.backgroundColor          = LOAD_SUBTITLE_BACKGROUND;
        style.edge                     = UTL_CHROMECAST_TEXT_EDGE_OUTLINE;
        style.edgeColor                = LOAD_SUBTITLE_EDGE_COLOR;
        style.fontScale.coefficient    = LOAD_SUBTITLE_FONT_SCALE;
        style.fontScale.fractionDigits = LOAD_SUBTITLE_FONT_DIGITS;
        load.textTrackStyle            = &style;
        subtitle.trackId               = LOAD_SUBTITLE_TRACK_ID;
        subtitle.name.data             = (const byte_t*)LOAD_SUBTITLE_NAME;
        subtitle.name.byteCount        = sizeof(LOAD_SUBTITLE_NAME) - 1;
        subtitle.language.data         = (const byte_t*)config->subtitleLanguage;
        subtitle.language.byteCount    = Utl_Unicode_GetUtf8StringByteCount(config->subtitleLanguage, sizeof(config->subtitleLanguage));
        valid_subtitle = (UTL_UNICODE_NO_TERMINATOR != subtitle.language.byteCount);
        if (0 == subtitle.language.byteCount)
        {
            subtitle.language.data      = (const byte_t*)LOAD_DEFAULT_LANGUAGE;
            subtitle.language.byteCount = sizeof(LOAD_DEFAULT_LANGUAGE) - 1;
        }
        load.subtitle = &subtitle;
    }

    valid_content_text = (UTL_UNICODE_NO_TERMINATOR != load.contentId.byteCount);
    valid_type_text    = (UTL_UNICODE_NO_TERMINATOR != load.contentType.byteCount);
    valid_title_text   = (UTL_UNICODE_NO_TERMINATOR != load.title.byteCount);
    valid_text         = valid_content_text && valid_type_text && valid_title_text;
    valid_request      = (0 < requestId);
    position           = GetMilliseconds(start);
    can_encode         = valid_stream && valid_subtitle && valid_text && valid_request && position.valid;
    written            = 0;
    if (can_encode)
    {
        load.requestId         = (uint32_t)requestId;
        load.startMilliseconds = position.milliseconds;
        written                = Utl_Chromecast_WriteLoad(&load, output, capacity);
    }

    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The proxy worker publishes one coherent pair without waiting for the runner.
 *  A suspended consumer retains its previous slot until it takes a new pair.
 */
extern FORCE_INLINE void CastSession_SetProxy(CastSession_t* owner, const char* pUrl, const char* pContentType)
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
extern FORCE_INLINE void CastSession_ReplaceMedia(CastSession_t* owner, const char* url, const char* contentType)
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
extern FORCE_INLINE bool_t CastSession_Post(CastSession_t* owner, CastCommand command, double seconds)
{
    CastSession_State_t* pSession;
    bool_t               accepted;

    LOGASSERT(NULL != owner);
    pSession = owner->state;
    LOGASSERT(CastSession_IsPrepared(owner));

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
extern FORCE_INLINE void CastSession_Reset(CastSession_t* session)
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
extern FORCE_INLINE bool_t CastSession_TryClose(CastSession_t* session)
{
    bool_t running;
    bool_t prepared;
    bool_t can_close;
    bool_t channel_closed;
    bool_t closed;

    LOGASSERT(NULL != session);

    running   = CastSession_IsRunning(session);
    prepared  = CastSession_IsPrepared(session);
    can_close = (!running) && (!prepared);
    closed    = FALSE;
    if (can_close)
    {
        channel_closed = (NULL == session->state);
        if (!channel_closed)
        {
            channel_closed = CastChannel_TryClose(&session->state->channel);
        }
        if (channel_closed)
        {
            closed = OS_Memory_TryClose(&session->memory);
            if (closed)
            {
                session->state = NULL;
            }
        }
    }

    return closed;
}
