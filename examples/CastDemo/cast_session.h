/* cast_session.h - one playback session on a Cast receiver: connect to the device, launch the
** default media receiver, LOAD a URL, relay media status and accept transport commands.
**
** CastSession_Run blocks on the calling thread and reports through the callback on that
** thread. Another thread controls playback with CastSession_Post. */
#ifndef CAST_SESSION_H
#define CAST_SESSION_H

#include <stdint.h>

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
    char            stateName[32]; /* the receiver's playerState, or the session phase */
    double          currentTime;   /* seconds */
    double          duration;      /* seconds, 0 until the receiver reports it */
    int             hasMedia;      /* a media session exists: PLAY, PAUSE and SEEK are valid */
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
    char          host[64];          /* dotted IPv4 address of the receiver */
    char          contentUrl[1200];  /* what the receiver fetches */
    char          contentType[64];   /* media type of the content */
    char          title[512];        /* shown on the receiver's screen */
    int           loop;              /* reload when the media finishes */
    int           verbose;           /* log every message sent and received */
    int           servingLocally;    /* add the firewall hint to a LOAD failure */
    CastEventProc callback;
    void*         pUser;
} CastSessionConfig;

typedef enum CastCommand
{
    CAST_COMMAND_PLAY,
    CAST_COMMAND_PAUSE,
    CAST_COMMAND_TOGGLE, /* pause when playing, otherwise play */
    CAST_COMMAND_SEEK,   /* seconds: the new position */
    CAST_COMMAND_STOP    /* close the receiver application and end the session */
} CastCommand;

typedef struct CastSession CastSession;

/* Copies the configuration. WSAStartup must have been called. */
CastSession* CastSession_Create(const CastSessionConfig* pConfig);

/* Blocks until the media finishes, the receiver leaves, a STOP arrives or the channel fails.
** Returns 0 when playback completed or was stopped on request, 1 on failure. */
int CastSession_Run(CastSession* pSession);

/* Queues a command for the running loop; safe from any thread. */
void CastSession_Post(CastSession* pSession, CastCommand command, double seconds);

void CastSession_Destroy(CastSession* pSession);

#endif
