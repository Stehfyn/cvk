/* cast_demo.c - CastDemoCli: play a video on a Google Cast receiver such as the XGIMI Horizon S.
**
** The projector is a Chromecast built-in device, so "connecting" means the CASTV2 protocol:
** find the receiver with SSDP, open a TLS channel to port 8009, launch the default media
** receiver application and send it a LOAD with a URL. The receiver fetches and decodes the
** video itself, so the file is served from this machine by a small HTTP range server.
**
**   CastDemoCli [file.mp4] [options]     default: demo.mp4 beside the executable
**     --device NAME    receiver whose friendly name contains NAME (default "XGIMI")
**     --host ADDRESS   skip discovery and connect to this receiver
**     --url URL        cast this URL instead of serving a local file
**     --port N         HTTP port for the local file (default 8642)
**     --bind ADDRESS   local IPv4 address to serve from (default: the one facing the receiver)
**     --seconds N      stop after N seconds of playback
**     --loop           restart the file when it finishes
**     --list           discover receivers and exit
**     --verbose        print every Cast message sent and received
**
** Keys while playing: Space pauses/resumes, Q or Escape stops and exits. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cast_channel.h"
#include "cast_session.h"
#include "media_server.h"
#include "ssdp.h"

#define DEFAULT_DEVICE_NAME "XGIMI"
#define DEFAULT_SERVER_PORT (8642)
#define DISCOVERY_MS        (3000)
#define PROGRESS_MS         (5000)

typedef struct Options
{
    WCHAR          file[MAX_PATH];
    char           device[128];
    char           host[256];
    char           url[1024];
    char           bind[64];
    unsigned short port;
    unsigned       seconds;
    int            loop;
    int            list;
    int            verbose;
} Options;

static volatile LONG      g_interrupted;
static volatile ULONGLONG g_playingSince; /* first PLAYING status, for --seconds */
static char               g_lastState[32];
static ULONGLONG          g_lastProgress;
static int                g_runResult;

static BOOL WINAPI OnConsoleControl(DWORD type)
{
    (void)type;
    InterlockedExchange(&g_interrupted, 1);
    return TRUE;
}

static void ToUtf8(const wchar_t* pText, char* pOut, size_t capacity)
{
    if (0 == WideCharToMultiByte(CP_UTF8, 0, pText, -1, pOut, (int)capacity, NULL, NULL))
    {
        pOut[0] = 0;
    }
}

static int ContainsNoCase(const char* pHaystack, const char* pNeedle)
{
    const size_t needleLength = strlen(pNeedle);
    size_t       index;

    for (index = 0; pHaystack[index]; ++index)
    {
        if (0 == _strnicmp(pHaystack + index, pNeedle, needleLength))
        {
            return 1;
        }
    }
    return 0 == needleLength;
}

static void Usage(void)
{
    (void)fprintf(stderr,
                  "CastDemoCli [file.mp4] [--device NAME] [--host ADDRESS] [--url URL] [--port N] [--bind ADDRESS]\n"
                  "            [--seconds N] [--loop] [--list] [--verbose]\n"
                  "Plays a video on a Google Cast receiver (default device name: %s).\n",
                  DEFAULT_DEVICE_NAME);
}

static int ParseOptions(int argc, wchar_t** argv, Options* pOptions)
{
    int index;

    ZeroMemory(pOptions, sizeof(*pOptions));
    (void)snprintf(pOptions->device, sizeof(pOptions->device), "%s", DEFAULT_DEVICE_NAME);
    pOptions->port = DEFAULT_SERVER_PORT;
    for (index = 1; index < argc; ++index)
    {
        const wchar_t* pArgument = argv[index];
        const int      hasValue  = index + 1 < argc;

        if (0 == wcscmp(pArgument, L"--device") && hasValue)
        {
            ToUtf8(argv[++index], pOptions->device, sizeof(pOptions->device));
        }
        else if (0 == wcscmp(pArgument, L"--host") && hasValue)
        {
            ToUtf8(argv[++index], pOptions->host, sizeof(pOptions->host));
        }
        else if (0 == wcscmp(pArgument, L"--url") && hasValue)
        {
            ToUtf8(argv[++index], pOptions->url, sizeof(pOptions->url));
        }
        else if (0 == wcscmp(pArgument, L"--bind") && hasValue)
        {
            ToUtf8(argv[++index], pOptions->bind, sizeof(pOptions->bind));
        }
        else if (0 == wcscmp(pArgument, L"--port") && hasValue)
        {
            pOptions->port = (unsigned short)wcstoul(argv[++index], NULL, 10);
        }
        else if (0 == wcscmp(pArgument, L"--seconds") && hasValue)
        {
            pOptions->seconds = (unsigned)wcstoul(argv[++index], NULL, 10);
        }
        else if (0 == wcscmp(pArgument, L"--loop"))
        {
            pOptions->loop = 1;
        }
        else if (0 == wcscmp(pArgument, L"--list"))
        {
            pOptions->list = 1;
        }
        else if (0 == wcscmp(pArgument, L"--verbose"))
        {
            pOptions->verbose = 1;
        }
        else if (L'-' == pArgument[0])
        {
            return 0;
        }
        else
        {
            wcscpy_s(pOptions->file, MAX_PATH, pArgument);
        }
    }
    if (0 == pOptions->file[0])
    {
        /* demo.mp4 beside the executable, as the other example does */
        wchar_t* pSlash;

        GetModuleFileNameW(NULL, pOptions->file, MAX_PATH);
        pSlash = wcsrchr(pOptions->file, L'\\');
        if (NULL != pSlash)
        {
            pSlash[1] = 0;
        }
        else
        {
            pOptions->file[0] = 0;
        }
        wcscat_s(pOptions->file, MAX_PATH, L"demo.mp4");
    }
    return 1;
}

static int ResolveIPv4(const char* pHost, char* pOut, size_t capacity)
{
    struct addrinfo  hints;
    struct addrinfo* pResult = NULL;
    int              ok      = 0;

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (0 == getaddrinfo(pHost, NULL, &hints, &pResult) && NULL != pResult)
    {
        ok = NULL != inet_ntop(AF_INET, &((struct sockaddr_in*)pResult->ai_addr)->sin_addr, pOut, capacity);
        freeaddrinfo(pResult);
    }
    return ok;
}

static int FindDevice(const Options* pOptions, CastDevice* pDevice)
{
    CastDevice devices[CAST_MAX_DEVICES];
    unsigned   count;
    unsigned   index;
    int        found = 0;

    if (pOptions->host[0])
    {
        ZeroMemory(pDevice, sizeof(*pDevice));
        if (!ResolveIPv4(pOptions->host, pDevice->host, sizeof(pDevice->host)))
        {
            (void)fprintf(stderr, "cannot resolve %s\n", pOptions->host);
            return 0;
        }
        if (!Ssdp_DescribeHost(pDevice->host, pDevice))
        {
            (void)snprintf(pDevice->friendlyName, sizeof(pDevice->friendlyName), "%s", pOptions->host);
        }
        return 1;
    }
    (void)printf("searching for Cast receivers...\n");
    count = Ssdp_DiscoverCastDevices(devices, CAST_MAX_DEVICES, DISCOVERY_MS);
    for (index = 0; index < count; ++index)
    {
        const int match = ContainsNoCase(devices[index].friendlyName, pOptions->device);

        (void)printf("  %s%s  %s  (%s)\n", match ? "* " : "  ", devices[index].friendlyName, devices[index].host, devices[index].model);
        if (match && !found)
        {
            *pDevice = devices[index];
            found    = 1;
        }
    }
    if (0 == count)
    {
        (void)printf("  none answered. Is the projector on and on the same network?\n");
    }
    else if (!found && !pOptions->list)
    {
        (void)printf("no receiver name contains \"%s\"; pass --device NAME or --host ADDRESS\n", pOptions->device);
    }
    return found;
}

/* Runs on the session thread: print progress, remember when playback started. */
static void OnSessionEvent(void* pUser, CastEvent event, const CastStatus* pStatus, const char* pText)
{
    const ULONGLONG now = GetTickCount64();

    (void)pUser;
    switch (event)
    {
    case CAST_EVENT_LOG:
        (void)printf("%s\n", pText);
        break;
    case CAST_EVENT_STATUS:
        if (CAST_STATE_PLAYING == pStatus->state && 0 == g_playingSince)
        {
            g_playingSince = now;
        }
        if (pStatus->hasMedia && (0 != strcmp(pStatus->stateName, g_lastState) || now - g_lastProgress >= PROGRESS_MS))
        {
            (void)snprintf(g_lastState, sizeof(g_lastState), "%s", pStatus->stateName);
            g_lastProgress = now;
            (void)printf("%s  %.1f / %.1f s\n", pStatus->stateName, pStatus->currentTime, pStatus->duration);
        }
        break;
    case CAST_EVENT_ENDED:
        (void)printf("%s\n", pText ? pText : "session ended");
        break;
    }
}

static DWORD WINAPI SessionThread(LPVOID pParameter)
{
    g_runResult = CastSession_Run((CastSession*)pParameter);
    return 0;
}

static int ConsoleHasKeys(void)
{
    DWORD mode;

    return GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mode) && _kbhit();
}

int wmain(int argc, wchar_t** argv)
{
    Options           options;
    CastDevice        device;
    CastSessionConfig config;
    CastSession*      pSession;
    MediaServer*      pServer = NULL;
    HANDLE            hThread;
    WSADATA           wsaData;
    char              error[256];
    char              localAddress[64];
    int               stopping = 0;

    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (!ParseOptions(argc, argv, &options))
    {
        Usage();
        return 3;
    }
    if (0 != WSAStartup(MAKEWORD(2, 2), &wsaData))
    {
        (void)fprintf(stderr, "WSAStartup failed\n");
        return 1;
    }
    if (!FindDevice(&options, &device))
    {
        WSACleanup();
        return options.list ? 0 : 2;
    }
    if (options.list)
    {
        WSACleanup();
        return 0;
    }
    (void)printf("using %s at %s\n", device.friendlyName, device.host);

    ZeroMemory(&config, sizeof(config));
    (void)snprintf(config.host, sizeof(config.host), "%s", device.host);
    config.loop     = options.loop;
    config.verbose  = options.verbose;
    config.callback = OnSessionEvent;
    if (options.url[0])
    {
        const char* pExtension = strrchr(options.url, '.');
        wchar_t     extension[16];

        (void)snprintf(config.contentUrl, sizeof(config.contentUrl), "%s", options.url);
        (void)snprintf(config.title, sizeof(config.title), "%s", options.url);
        if (NULL != pExtension && NULL == strchr(pExtension, '/') && 0 != MultiByteToWideChar(CP_UTF8, 0, pExtension, -1, extension, 16))
        {
            (void)snprintf(config.contentType, sizeof(config.contentType), "%s", MediaServer_ContentTypeForExtension(extension));
        }
        else
        {
            (void)snprintf(config.contentType, sizeof(config.contentType), "video/mp4");
        }
    }
    else
    {
        const wchar_t* pName      = wcsrchr(options.file, L'\\');
        const wchar_t* pExtension = wcsrchr(options.file, L'.');
        char           extension[16];

        pName = (NULL != pName) ? pName + 1 : options.file;
        if (NULL != pExtension && pExtension < pName)
        {
            pExtension = NULL;
        }
        ToUtf8(pName, config.title, sizeof(config.title));
        ToUtf8(NULL != pExtension ? pExtension : L"", extension, sizeof(extension));
        (void)snprintf(config.contentType, sizeof(config.contentType), "%s", MediaServer_ContentTypeForExtension(pExtension));
        config.servingLocally = 1;
        if (options.bind[0])
        {
            (void)snprintf(localAddress, sizeof(localAddress), "%s", options.bind);
        }
        else if (!MediaServer_LocalAddressToward(device.host, localAddress, sizeof(localAddress)))
        {
            (void)fprintf(stderr, "cannot determine the local address facing %s; pass --bind ADDRESS\n", device.host);
            WSACleanup();
            return 1;
        }
        pServer = MediaServer_Start(options.file, options.bind[0] ? options.bind : NULL, options.port, config.contentType, error, sizeof(error));
        if (NULL == pServer)
        {
            (void)fprintf(stderr, "server: %s\n", error);
            WSACleanup();
            return 1;
        }
        (void)snprintf(config.contentUrl, sizeof(config.contentUrl), "http://%s:%u/media%s", localAddress, (unsigned)MediaServer_GetPort(pServer), extension);
        (void)printf("serving %ls as %s (%s)\n", options.file, config.contentUrl, config.contentType);
    }

    pSession = CastSession_Create(&config);
    hThread  = (NULL != pSession) ? CreateThread(NULL, 0, SessionThread, pSession, 0, NULL) : NULL;
    if (NULL == hThread)
    {
        (void)fprintf(stderr, "cannot start the session\n");
        CastSession_Destroy(pSession);
        MediaServer_Stop(pServer);
        WSACleanup();
        return 1;
    }
    SetConsoleCtrlHandler(OnConsoleControl, TRUE);
    while (WAIT_TIMEOUT == WaitForSingleObject(hThread, 100))
    {
        int stop = g_interrupted;

        if (options.seconds > 0 && 0 != g_playingSince && GetTickCount64() >= g_playingSince + (ULONGLONG)options.seconds * 1000)
        {
            if (!stopping)
            {
                (void)printf("stopping after %u seconds\n", options.seconds);
            }
            stop = 1;
        }
        while (ConsoleHasKeys())
        {
            const int key = _getch();

            if (' ' == key)
            {
                CastSession_Post(pSession, CAST_COMMAND_TOGGLE, 0);
            }
            else if ('q' == key || 'Q' == key || 27 == key)
            {
                stop = 1;
            }
        }
        if (stop && !stopping)
        {
            stopping = 1;
            CastSession_Post(pSession, CAST_COMMAND_STOP, 0);
        }
    }
    CloseHandle(hThread);
    CastSession_Destroy(pSession);
    MediaServer_Stop(pServer);
    WSACleanup();
    return g_runResult;
}
