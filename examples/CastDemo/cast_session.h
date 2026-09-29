#ifndef CAST_SESSION_H
#define CAST_SESSION_H

/*=================================================================================================
** 1. References
**===============================================================================================*/

/**
 * One playback session on a Cast receiver: connect, launch the default receiver,
 * load a media URL, relay status and accept transport commands. Run executes on
 * the session worker; external producers publish through Post and media setters.
 * Persistent session storage includes reusable Cast channel buffers. Native
 * network, SChannel and playback dependencies require separate allocation audits.
 */

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <stdint.h>
#include "os/os_types.h"
#include "os/os_atomic.h"
#include "os/os_memory.h"
#include "cast_media.h"
#include "utl/utl_json.h"

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define CAST_SESSION_STATE_NAME_CAPACITY (32)                       ///< Application status text, including termination.
#define CAST_SESSION_HOST_CAPACITY       (64)                       ///< Receiver address text, including termination.
#define CAST_SESSION_URL_CAPACITY        (CAST_MEDIA_URL_CAPACITY)  ///< Served URL, including termination.
#define CAST_SESSION_TYPE_CAPACITY       (CAST_MEDIA_TYPE_CAPACITY) ///< MIME type text, including termination.
#define CAST_SESSION_TITLE_CAPACITY      (512)                      ///< Receiver display title, including termination.
#define CAST_SESSION_LANGUAGE_CAPACITY   (16)                       ///< Subtitle language text, including termination.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

typedef enum CastPlayerState
{
    CAST_STATE_CONNECTING,
    CAST_STATE_LAUNCHING,
    CAST_STATE_LOADING,
    CAST_STATE_BUFFERING,
    CAST_STATE_PLAYING,
    CAST_STATE_PAUSED,
    CAST_STATE_IDLE,
    CAST_STATE_ENDED
} CastPlayerState;

typedef struct CastStatus
{
    CastPlayerState state;
    char            stateName[CAST_SESSION_STATE_NAME_CAPACITY]; /* receiver playerState or session phase */
    float64_t       currentTime;                                 /* seconds */
    float64_t       duration;                                    /* seconds, 0 until the receiver reports it */
    bool_t          hasMedia; /* a media session exists: PLAY, PAUSE and SEEK are valid */
} CastStatus;

typedef enum CastEvent
{
    CAST_EVENT_LOG,    /* pText: one line of progress or diagnostics */
    CAST_EVENT_STATUS, /* pStatus: the current playback status */
    CAST_EVENT_ENDED   /* pText: why the session ended */
} CastEvent;

typedef void (*CastEventProc)(void* pUser, CastEvent event, const CastStatus* pStatus, const char* pText);

typedef struct CastSessionConfig
{
    char             host[CAST_SESSION_HOST_CAPACITY];        /* dotted IPv4 address of the receiver */
    char             contentUrl[CAST_SESSION_URL_CAPACITY];   /* settled full-quality stream */
    char             contentType[CAST_SESSION_TYPE_CAPACITY]; /* media type of the full-quality content */
    char             title[CAST_SESSION_TITLE_CAPACITY];      /* shown on the receiver's screen */
    char             subtitleUrl[CAST_SESSION_URL_CAPACITY];  /* optional WebVTT across loop/proxy LOADs */
    char             subtitleLanguage[CAST_SESSION_LANGUAGE_CAPACITY];
    bool_t           loop;           /* reload when the media finishes */
    bool_t           verbose;        /* log every message sent and received */
    bool_t           servingLocally; /* add the firewall hint to a LOAD failure */
    const float64_t* pKeyframes;     /* optional, ascending seconds: scrub previews snap to these */
    uint32_t         keyframeCount;
    CastEventProc    callback;
    void*            pUser;
} CastSessionConfig;

typedef enum CastCommand
{
    CAST_COMMAND_PLAY,
    CAST_COMMAND_PAUSE,
    CAST_COMMAND_TOGGLE, /* pause when playing, otherwise play */
    CAST_COMMAND_SCRUB,  /* seconds: show this position while the user still holds the slider */
    CAST_COMMAND_SEEK,   /* seconds: the new position; playback resumes if it was playing before */
    CAST_COMMAND_STOP    /* close the receiver application and end the session */
} CastCommand;

/*************************************************************************************************/
/**
 * \brief Caller-owned lifetime for persistent session state.
 *
 * \par Details:
 *  Initialize during startup, Prepare before each cast and Reset only after the
 *  runner and all external publishers are quiescent. TryClose belongs to shutdown.
 *  Do not copy, move or directly modify fields while initialized. Native state is
 *  private to the implementation; no native types enter this interface.
 */
typedef struct CastSession_s
{
    OS_Memory_t                 memory;   ///< Startup storage lifetime.
    struct CastSession_State_s* state;    ///< Private implementation state.
    OS_Atomic32_t               running;  ///< Runner acquisition/release publication.
    bool_t                      prepared; ///< Owner-controlled configuration lifetime.
} CastSession_t;

/*=================================================================================================
** 3.3 Constants
**===============================================================================================*/

/*=================================================================================================
** 3.4 Variables
**===============================================================================================*/

/*=================================================================================================
** 3.5 Functions
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Establishes persistent session storage during application startup.
 *
 * \par Requirements:
 *  - 001 Required caller-owned session storage shall assert when missing.
 *  - 002 The owner shall provide fresh storage or an object whose TryClose succeeded.
 *  - 003 Native storage shall be acquired only here; failed startup shall leave TryClose valid.
 *  - 004 No session configuration or callback shall be used until Prepare.
 *
 * \param[out] session   Owner retained through all jobs and successful closure
 *
 * \retval     TRUE    Persistent storage is available.
 * \retval     FALSE   Startup storage failed; call TryClose before discarding the owner.
 */
bool_t CastSession_Initialize(CastSession_t* session);

/*************************************************************************************************/
/**
 * \brief Queries whether a configuration is prepared for use.
 *
 * \par Requirements:
 *  - 001 The lifecycle owner shall serialize this query with Prepare, Reset and TryClose.
 *
 * \param[in] session   Required initialized owner
 *
 * \retval    TRUE    A prepared configuration remains active.
 * \retval    FALSE   Startup, reset or closure leaves no active configuration.
 */
bool_t CastSession_IsPrepared(const CastSession_t* session);

/*************************************************************************************************/
/**
 * \brief Observes whether the session runner is accessing its state.
 *
 * \par Requirements:
 *  - 001 The owner shall remain alive; an acquired worker completion is still required before reusing job context.
 *
 * \param[in] session   Required initialized owner
 *
 * \retval    TRUE    The runner is active.
 * \retval    FALSE   The runner has not begun or has returned from its internal operation.
 */
bool_t CastSession_IsRunning(const CastSession_t* session);

/*************************************************************************************************/
/**
 * \brief Prepares one cast using the existing startup storage.
 *
 * \par Details:
 *  A complete initialized configuration is prepared.
 *
 * \par Requirements:
 *  - 001 Required owner, configuration and callback shall assert when missing.
 *  - 002 The owner shall have initialized successfully and exclude every producer and runner during preparation.
 *  - 003 The previous cast shall be reset; active or closing state shall assert as misuse.
 *  - 004 Configuration and keyframe storage shall obey their documented lifetimes and remain disjoint from session state.
 *  - 005 No allocation or release shall occur.
 *
 * \param[in,out] session   Initialized idle lifetime owner
 * \param[in]     config    Configuration copied by value; referenced keyframes and callback context stay borrowed
 *
 * \retval        None
 */
void CastSession_Prepare(CastSession_t* session, const CastSessionConfig* config);

/*************************************************************************************************/
/**
 * \brief Serializes the session's LOAD profile as complete counted JSON.
 *
 * \par Requirements:
 *  - 001 Required configuration, text and output pointers shall assert when missing.
 *  - 002 The model and all input strings shall remain stable and disjoint from output.
 *  - 003 Configuration strings shall terminate within their documented capacities; the session ID shall remain counted bytes.
 *  - 004 Finite negative times shall clamp to zero; other times shall round to the nearest millisecond, with ties upward.
 *  - 005 Invalid encoding, stream kinds, times, request IDs or capacity shall preserve output and return zero.
 *  - 006 No allocation or CRT operation shall occur.
 *
 * \param[in]  config       Required configuration with bounded title and optional subtitle strings
 * \param[in]  sessionId    Counted decoded application session ID
 * \param[in]  requestId    Positive request identifier; zero denotes exhausted correlation storage
 * \param[in]  url          Terminated content URL, within the configuration URL capacity
 * \param[in]  type         Terminated MIME type, within the configuration MIME capacity
 * \param[in]  start        Finite seconds, representable as uint64_t milliseconds after rounding
 * \param[in]  autoplay     Whether playback should start after loading
 * \param[in]  streamType   Terminated NONE, BUFFERED or LIVE token
 * \param[out] output       Required caller-owned disjoint storage
 * \param[in]  capacity     Writable byte capacity; no terminator is appended
 *
 * \retval     nonzero   Exact JSON bytes written.
 * \retval     0         Invalid model or insufficient output; output remains unchanged.
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
                               uint32_t                 capacity);

/*************************************************************************************************/
/**
 * \brief Publishes the prepared low-latency scrub proxy.
 *
 * \par Details:
 *  Proxy publication is complete.
 *
 * \par Requirements:
 *  - 001 The owner shall remain prepared until this producer is quiescent.
 *  - 002 The caller shall provide stable terminated URL and MIME strings, disjoint from session state.
 *  - 003 The proxy worker shall be the sole proxy producer; the runner shall be the sole consumer.
 *  - 004 Complete pairs shall publish without locks, waits, allocation or partial-reader visibility.
 *
 * \param[in,out] owner          Required prepared owner
 * \param[in]     pUrl           Required URL, fewer than 1200 bytes excluding its terminator
 * \param[in]     pContentType   Required MIME type, fewer than 64 bytes excluding its terminator
 *
 * \retval        None
 */
void CastSession_SetProxy(CastSession_t* owner, const char* pUrl, const char* pContentType);

/*************************************************************************************************/
/**
 * \brief Publishes a media replacement for the runner to consume.
 *
 * \par Details:
 *  Replacement publication is complete; playback switch remains asynchronous.
 *
 * \par Requirements:
 *  - 001 The owner shall remain prepared until this producer is quiescent.
 *  - 002 The caller shall provide stable terminated URL and MIME strings, disjoint from session state.
 *  - 003 The runner shall preserve playback position, pause state and subtitles when replacing the source.
 *  - 004 The GUI owner shall be the sole replacement producer; the runner shall be the sole consumer.
 *  - 005 Complete pairs shall publish without locks or waits; unread replacements shall coalesce to the latest pair.
 *
 * \param[in,out] owner         Required prepared owner
 * \param[in]     url           Required URL, fewer than 1200 bytes excluding its terminator
 * \param[in]     contentType   Required MIME type, fewer than 64 bytes excluding its terminator
 *
 * \retval        None
 */
void CastSession_ReplaceMedia(CastSession_t* owner, const char* url, const char* contentType);

/*************************************************************************************************/
/**
 * \brief Runs a prepared receiver conversation on its persistent worker.
 *
 * \par Requirements:
 *  - 001 Missing preparation, repeated Run without Reset/Prepare or concurrent runners shall assert as programmer misuse.
 *  - 002 The owner and borrowed callback/keyframe storage shall remain alive throughout execution.
 *  - 003 The caller shall acquire worker completion before Reset or closure; event delivery alone is insufficient.
 *  - 004 Termination shall clear the channel pointer before runner ownership is released.
 *
 * \param[in,out] session   Required prepared owner with exclusive runner ownership
 *
 * \retval        0   Playback finished or was stopped by request.
 * \retval        1   Channel or receiver operation failed.
 */
int32_t CastSession_Run(CastSession_t* session);

/*************************************************************************************************/
/**
 * \brief Publishes one command into the bounded per-cast queue.
 *
 * \par Requirements:
 *  - 001 The owner shall remain prepared until all publishers are quiescent.
 *  - 002 Stop shall remain latched until Reset and Prepare establish a new cast.
 *  - 003 Unsupported commands, invalid positions and ordinary post-Stop commands shall be rejected.
 *  - 004 Queue admission and position coalescing shall follow CastCommands policy.
 *
 * \param[in,out] owner     Required prepared owner
 * \param[in]     command   Transport or position action
 * \param[in]     seconds   Finite nonnegative seconds for SEEK/SCRUB; otherwise unused
 *
 * \retval        TRUE    The command was accepted, including repeated Stop.
 * \retval        FALSE   Admission failed without altering the queued commands.
 */
bool_t CastSession_Post(CastSession_t* owner, CastCommand command, double seconds);

/*************************************************************************************************/
/**
 * \brief Ends a prepared cast while retaining its startup storage.
 *
 * \par Details:
 *  Preparation is cleared; storage remains available for reuse.
 *
 * \par Requirements:
 *  - 001 The owner shall acquire worker completion and stop every external publisher before resetting.
 *  - 002 A running operation or unreleased channel shall assert as misuse.
 *  - 003 Repeated reset of an initialized idle owner shall remain valid.
 *  - 004 No allocation, native release or callback shall occur.
 *
 * \param[in,out] session   Required initialized owner with no active users
 *
 * \retval        None
 */
void CastSession_Reset(CastSession_t* session);

/*************************************************************************************************/
/**
 * \brief Releases persistent session storage at application shutdown.
 *
 * \par Requirements:
 *  - 001 The owner shall have stopped publishers, acquired job completion and reset preparation.
 *  - 002 Prepared or running objects shall reject closure without changing storage.
 *  - 003 Failed native release shall retain the owner for retry and prohibit reuse.
 *  - 004 Repeated closure after success shall return TRUE.
 *
 * \param[in,out] session   Required initialized owner, including after failed Initialize
 *
 * \retval        TRUE    No session storage remains.
 * \retval        FALSE   Active preparation/work or native release failure requires later closure.
 */
bool_t CastSession_TryClose(CastSession_t* session);

#endif
