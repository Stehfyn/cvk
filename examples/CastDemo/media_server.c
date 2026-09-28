/* media_server.c - Winsock listener, one thread per request, ReadFile/send streaming. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media_server.h"

#define REQUEST_CAPACITY (8192)
#define TRANSMIT_MAX     (60u * 1024u * 1024u) /* one TransmitFile call; larger ranges loop */
#define SEND_BUFFER_SIZE (1024 * 1024)         /* keeps a Wi-Fi link full after a seek despite its round trips */

/* Shared, refcounted state for a growing file, so a streaming connection can read the live
** completion flag after MediaServer_Stop has freed the server. The server holds one reference; each
** connection to a growing server holds one for its lifetime; the last release frees it. */
typedef struct LiveState
{
    volatile LONG refcount;
    volatile LONG complete;
} LiveState;

typedef struct SubtitleData
{
    volatile LONG refcount;
    size_t size;
    char data[1];
} SubtitleData;

static void SubtitleData_Release(SubtitleData* subtitles)
{
    if (subtitles && !InterlockedDecrement(&subtitles->refcount)) free(subtitles);
}

#define CORS_HEADERS "Access-Control-Allow-Origin: *\r\nAccess-Control-Allow-Methods: GET, HEAD, OPTIONS\r\n" \
                     "Access-Control-Allow-Headers: Content-Type, Accept-Encoding, Range\r\n" \
                     "Access-Control-Expose-Headers: Content-Length, Content-Range, Accept-Ranges\r\n"

static void LiveState_AddRef(LiveState* pLive)
{
    if (NULL != pLive)
    {
        InterlockedIncrement(&pLive->refcount);
    }
}

static void LiveState_Release(LiveState* pLive)
{
    if (NULL != pLive && 0 == InterlockedDecrement(&pLive->refcount))
    {
        free(pLive);
    }
}

struct MediaServer
{
    SOCKET         listener;
    HANDLE         thread;
    volatile LONG  stopping;
    WCHAR          file[MAX_PATH];
    char           contentType[64];
    unsigned short port;
    uint64_t       size;
    LiveState*     live; /* non-NULL when the file is still being written (growing proxy) */
    SubtitleData*  subtitles;
};

/* Everything a request thread needs, copied so the server may be freed while it streams. */
typedef struct Connection
{
    SOCKET     socket;
    HANDLE     hFile; /* opened once, reused for every keep-alive range on this connection */
    WCHAR      file[MAX_PATH];
    char       contentType[64];
    uint64_t   size;
    LiveState* live; /* non-NULL for a growing file: stream to the live end, not a fixed size */
    SubtitleData* subtitles;
} Connection;

static int ParseUint64(const char* pText, size_t length, uint64_t* pValue)
{
    uint64_t value = 0;
    size_t   index;

    if (0 == length || length > 19)
    {
        return 0;
    }
    for (index = 0; index < length; ++index)
    {
        if (pText[index] < '0' || pText[index] > '9')
        {
            return 0;
        }
        value = value * 10 + (uint64_t)(pText[index] - '0');
    }
    *pValue = value;
    return 1;
}

int MediaServer_ParseRange(const char* pValue, uint64_t size, uint64_t* pFirst, uint64_t* pLast)
{
    const char* pDash;
    const char* pEnd;
    uint64_t    first;
    uint64_t    last;

    while (' ' == *pValue || '\t' == *pValue)
    {
        ++pValue;
    }
    if (0 != _strnicmp(pValue, "bytes=", 6))
    {
        return 0;
    }
    pValue += 6;
    pEnd  = pValue + strcspn(pValue, ", \t\r\n");
    pDash = memchr(pValue, '-', (size_t)(pEnd - pValue));
    if (NULL == pDash)
    {
        return 0;
    }
    if (pDash == pValue)
    {
        /* suffix: the last N bytes */
        uint64_t suffix;

        if (!ParseUint64(pDash + 1, (size_t)(pEnd - pDash - 1), &suffix) || 0 == suffix)
        {
            return 0;
        }
        if (0 == size)
        {
            return -1;
        }
        first = (suffix >= size) ? 0 : size - suffix;
        last  = size - 1;
    }
    else
    {
        if (!ParseUint64(pValue, (size_t)(pDash - pValue), &first))
        {
            return 0;
        }
        if (pDash + 1 == pEnd)
        {
            last = (size > 0) ? size - 1 : 0;
        }
        else if (!ParseUint64(pDash + 1, (size_t)(pEnd - pDash - 1), &last) || last < first)
        {
            return 0;
        }
        if (first >= size)
        {
            return -1;
        }
        if (last >= size)
        {
            last = size - 1;
        }
    }
    *pFirst = first;
    *pLast  = last;
    return 1;
}

const char* MediaServer_ContentTypeForExtension(const wchar_t* pExtension)
{
    static const struct
    {
        const wchar_t* pExtension;
        const char*    pType;
    } kTypes[] = {
        {L".mp4", "video/mp4"},
        {L".m4v", "video/mp4"},
        {L".mov", "video/quicktime"},
        {L".webm", "video/webm"},
        {L".mkv", "video/x-matroska"},
        {L".mp3", "audio/mpeg"},
        {L".m4a", "audio/mp4"},
        {L".aac", "audio/aac"},
        {L".flac", "audio/flac"},
        {L".wav", "audio/wav"},
        {L".ogg", "audio/ogg"},
        {L".jpg", "image/jpeg"},
        {L".jpeg", "image/jpeg"},
        {L".png", "image/png"},
        {L".gif", "image/gif"},
    };
    size_t index;

    if (NULL != pExtension)
    {
        for (index = 0; index < sizeof(kTypes) / sizeof(kTypes[0]); ++index)
        {
            if (0 == _wcsicmp(pExtension, kTypes[index].pExtension))
            {
                return kTypes[index].pType;
            }
        }
    }
    return "application/octet-stream";
}

int MediaServer_LocalAddressToward(const char* pHost, char* pOut, size_t capacity)
{
    struct sockaddr_in remote;
    struct sockaddr_in local;
    int                length = sizeof(local);
    SOCKET             s;
    int                ok;

    ZeroMemory(&remote, sizeof(remote));
    remote.sin_family = AF_INET;
    remote.sin_port   = htons(9); /* any port: a UDP connect only selects the route */
    if (1 != inet_pton(AF_INET, pHost, &remote.sin_addr))
    {
        return 0;
    }
    s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (INVALID_SOCKET == s)
    {
        return 0;
    }
    ok = (0 == connect(s, (struct sockaddr*)&remote, sizeof(remote))) && (0 == getsockname(s, (struct sockaddr*)&local, &length)) && (NULL != inet_ntop(AF_INET, &local.sin_addr, pOut, capacity));
    closesocket(s);
    return ok;
}

static int SendAll(SOCKET s, const void* pData, size_t length)
{
    const char* pCursor = (const char*)pData;

    while (length > 0)
    {
        const int sent = send(s, pCursor, (int)length, 0);

        if (sent <= 0)
        {
            return 0;
        }
        pCursor += sent;
        length -= (size_t)sent;
    }
    return 1;
}

static int FindHeader(const char* pRequest, const char* pName, char* pOut, size_t capacity)
{
    const size_t nameLength = strlen(pName);
    const char*  pLine      = strstr(pRequest, "\r\n");

    while (NULL != pLine)
    {
        const char* pEnd;
        size_t      lineLength;

        pLine += 2;
        pEnd       = strstr(pLine, "\r\n");
        lineLength = (NULL != pEnd) ? (size_t)(pEnd - pLine) : strlen(pLine);
        if (0 == lineLength)
        {
            break;
        }
        if (lineLength > nameLength && ':' == pLine[nameLength] && 0 == _strnicmp(pLine, pName, nameLength))
        {
            const char* pValue = pLine + nameLength + 1;
            size_t      valueLength;

            while (pValue < pLine + lineLength && (' ' == *pValue || '\t' == *pValue))
            {
                ++pValue;
            }
            valueLength = (size_t)(pLine + lineLength - pValue);
            if (valueLength >= capacity)
            {
                valueLength = capacity - 1;
            }
            memcpy(pOut, pValue, valueLength);
            pOut[valueLength] = 0;
            return 1;
        }
        pLine = pEnd;
    }
    return 0;
}

/* Sends bytes [first, first+remaining) straight from the file cache to the socket with no
** user-space copy. Returns bytes actually transmitted (which is *pComplete==1 when it equals the
** whole range; a short count means the receiver closed the connection, normal for range probes). */
static uint64_t StreamFile(const Connection* pConnection, uint64_t first, uint64_t remaining, int* pComplete)
{
    uint64_t sent = 0;

    *pComplete = 0;
    if (INVALID_HANDLE_VALUE == pConnection->hFile)
    {
        return 0;
    }
    while (remaining > 0)
    {
        LARGE_INTEGER offset;
        const DWORD   want = (remaining < TRANSMIT_MAX) ? (DWORD)remaining : TRANSMIT_MAX;

        offset.QuadPart = (LONGLONG)(first + sent);
        if (!SetFilePointerEx(pConnection->hFile, offset, NULL, FILE_BEGIN) || !TransmitFile(pConnection->socket, pConnection->hFile, want, 0, NULL, NULL, 0))
        {
            return sent;
        }
        sent += want;
        remaining -= want;
    }
    *pComplete = 1;
    return sent;
}

/* Milliseconds a streaming connection waits for the growing file to gain more bytes before
** re-checking, and the longest it will sit with no growth and no completion before giving up. */
#define GROW_POLL_MS  8u
#define GROW_IDLE_MAX 60000u

/* Streams a growing file from byte offset first to its live end and keeps going as it grows,
** blocking on GROW_POLL_MS polls until the file is marked complete and fully sent, the client
** disconnects (a send fails), or the file stalls for GROW_IDLE_MAX with no completion. */
static uint64_t StreamGrowingFile(const Connection* pConnection, uint64_t first)
{
    uint64_t sent      = 0;
    unsigned idleWaits = 0;

    if (INVALID_HANDLE_VALUE == pConnection->hFile)
    {
        return 0;
    }
    for (;;)
    {
        LARGE_INTEGER fileSize;
        uint64_t      available;

        if (!GetFileSizeEx(pConnection->hFile, &fileSize))
        {
            return sent;
        }
        available = (uint64_t)fileSize.QuadPart;
        if (first + sent < available)
        {
            LARGE_INTEGER  offset;
            const uint64_t remaining = available - (first + sent);
            const DWORD    want      = (remaining < TRANSMIT_MAX) ? (DWORD)remaining : TRANSMIT_MAX;

            offset.QuadPart = (LONGLONG)(first + sent);
            if (!SetFilePointerEx(pConnection->hFile, offset, NULL, FILE_BEGIN) || !TransmitFile(pConnection->socket, pConnection->hFile, want, 0, NULL, NULL, 0))
            {
                return sent; /* the receiver closed the connection */
            }
            sent += want;
            idleWaits = 0;
            continue;
        }
        /* Caught up with what has been written so far. */
        if (NULL == pConnection->live || 0 != pConnection->live->complete)
        {
            return sent; /* the file is done growing and fully sent */
        }
        if (idleWaits * GROW_POLL_MS >= GROW_IDLE_MAX)
        {
            return sent; /* the writer stalled without completing; do not hang forever */
        }
        ++idleWaits;
        Sleep(GROW_POLL_MS);
    }
}

/* Answers one request for a growing file: the total length is unknown, so the response carries no
** Content-Length and closes the connection at end of stream (RFC 7230 3.3.3). Playback is
** progressive from the fragmented-MP4 init segment, so a byte offset from a Range header is honored
** but the body always runs to the live end. Always returns 0 (the connection does not persist). */
static int HandleGrowingRequest(const Connection* pConnection, const char* pRequest)
{
    char     header[1024];
    char     range[128];
    char     target[256];
    int      head;
    uint64_t first = 0;
    uint64_t last  = 0;

    head = (0 == strncmp(pRequest, "HEAD ", 5));
    if (!head && 0 != strncmp(pRequest, "GET ", 4))
    {
        static const char kNotAllowed[] = "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, HEAD\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

        SendAll(pConnection->socket, kNotAllowed, sizeof(kNotAllowed) - 1);
        return 0;
    }
    (void)snprintf(target, sizeof(target), "%.*s", (int)strcspn(pRequest + (head ? 5 : 4), " \r\n"), pRequest + (head ? 5 : 4));
    if (FindHeader(pRequest, "Range", range, sizeof(range)) && 0 == _strnicmp(range, "bytes=", 6))
    {
        LARGE_INTEGER fileSize;
        uint64_t      current = 0;

        if (GetFileSizeEx(pConnection->hFile, &fileSize))
        {
            current = (uint64_t)fileSize.QuadPart;
        }
        if (MediaServer_ParseRange(range, current > 0 ? current : ~(uint64_t)0, &first, &last) < 0)
        {
            first = 0;
        }
    }
    (void)snprintf(header,
                   sizeof(header),
                   "HTTP/1.1 200 OK\r\n"
                   "Content-Type: %s\r\n"
                   "Accept-Ranges: none\r\n"
                   CORS_HEADERS
                   "Connection: close\r\n\r\n",
                   pConnection->contentType);
    if (!SendAll(pConnection->socket, header, strlen(header)))
    {
        return 0;
    }
    if (head)
    {
        (void)fprintf(stderr, "server: %s %s -> 200 growing (header only)\n", "HEAD", target);
        return 0;
    }
    {
        const uint64_t sent = StreamGrowingFile(pConnection, first);

        (void)fprintf(stderr, "server: GET %s -> 200 growing, sent %.2f MB from offset %llu\n", target, (double)sent / (1024.0 * 1024.0), (unsigned long long)first);
    }
    return 0;
}

/* Answers one request. Returns 1 when the connection may carry another (HTTP/1.1 keep-alive),
** which spares the receiver a TCP handshake on every range it fetches after a seek. */
static int HandleRequest(const Connection* pConnection, const char* pRequest)
{
    char     header[4096];
    char     range[128];
    char     target[256];
    char     connection[32];
    int      head;
    int      keepAlive;
    int      rangeResult = 0;
    uint64_t first       = 0;
    uint64_t last        = 0;
    uint64_t length;
    int      status;

    head      = (0 == strncmp(pRequest, "HEAD ", 5));
    keepAlive = (NULL != strstr(pRequest, " HTTP/1.1\r\n")) && !(FindHeader(pRequest, "Connection", connection, sizeof(connection)) && 0 == _stricmp(connection, "close"));
    if (!head && 0 != strncmp(pRequest, "GET ", 4))
    {
        static const char kNotAllowed[] = "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, HEAD\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

        SendAll(pConnection->socket, kNotAllowed, sizeof(kNotAllowed) - 1);
        return 0;
    }
    (void)snprintf(target, sizeof(target), "%.*s", (int)strcspn(pRequest + (head ? 5 : 4), " \r\n"), pRequest + (head ? 5 : 4));
    if (FindHeader(pRequest, "Range", range, sizeof(range)))
    {
        rangeResult = MediaServer_ParseRange(range, pConnection->size, &first, &last);
    }
    if (rangeResult < 0)
    {
        (void)snprintf(header,
                       sizeof(header),
                       "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */%llu\r\nContent-Length: 0\r\nConnection: %s\r\n\r\n",
                       (unsigned long long)pConnection->size,
                       keepAlive ? "keep-alive" : "close");
        (void)fprintf(stderr, "server: %s %s -> 416 (%s)\n", head ? "HEAD" : "GET", target, range);
        return SendAll(pConnection->socket, header, strlen(header)) && keepAlive;
    }
    if (0 == rangeResult)
    {
        first  = 0;
        last   = (pConnection->size > 0) ? pConnection->size - 1 : 0;
        length = pConnection->size;
        status = 200;
    }
    else
    {
        length = last - first + 1;
        status = 206;
    }
    (void)snprintf(header,
                   sizeof(header),
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %llu\r\n"
                   "Accept-Ranges: bytes\r\n"
                   CORS_HEADERS
                   "Connection: %s\r\n",
                   status,
                   (200 == status) ? "OK" : "Partial Content",
                   pConnection->contentType,
                   (unsigned long long)length,
                   keepAlive ? "keep-alive" : "close");
    if (206 == status)
    {
        (void)snprintf(header + strlen(header),
                       sizeof(header) - strlen(header),
                       "Content-Range: bytes %llu-%llu/%llu\r\n",
                       (unsigned long long)first,
                       (unsigned long long)last,
                       (unsigned long long)pConnection->size);
    }
    (void)snprintf(header + strlen(header), sizeof(header) - strlen(header), "\r\n");
    if (!SendAll(pConnection->socket, header, strlen(header)))
    {
        return 0;
    }
    if (head || 0 == length)
    {
        (void)fprintf(stderr, "server: %s %s -> %d bytes %llu-%llu (header only)\n", head ? "HEAD" : "GET", target, status, (unsigned long long)first, (unsigned long long)last);
        return keepAlive;
    }
    {
        LARGE_INTEGER  frequency;
        LARGE_INTEGER  begin;
        LARGE_INTEGER  end;
        int            complete;
        const uint64_t sent = (QueryPerformanceFrequency(&frequency), QueryPerformanceCounter(&begin), StreamFile(pConnection, first, length, &complete));

        QueryPerformanceCounter(&end);
        {
            const double seconds = (double)(end.QuadPart - begin.QuadPart) / (double)frequency.QuadPart;

            (void)fprintf(stderr,
                          "server: %s %s -> %d, sent %.2f of %.2f MB in %.0f ms = %.1f MB/s%s\n",
                          head ? "HEAD" : "GET",
                          target,
                          status,
                          (double)sent / (1024.0 * 1024.0),
                          (double)length / (1024.0 * 1024.0),
                          seconds * 1000.0,
                          (seconds > 0) ? (double)sent / (1024.0 * 1024.0) / seconds : 0.0,
                          complete ? "" : " (client closed early)");
        }
        return complete && keepAlive;
    }
}

static int HandleSubtitleRequest(const Connection* connection, const char* request)
{
    char header[1024], range[128];
    uint64_t first = 0, last = 0, length;
    int status = 200, ranged = 0;
    const SubtitleData* subtitles = connection->subtitles;
    if (!subtitles)
        return SendAll(connection->socket, "HTTP/1.1 404 Not Found\r\n" CORS_HEADERS "Content-Length: 0\r\nConnection: close\r\n\r\n",
                       sizeof("HTTP/1.1 404 Not Found\r\n" CORS_HEADERS "Content-Length: 0\r\nConnection: close\r\n\r\n") - 1), 0;
    length = subtitles->size;
    if (FindHeader(request, "Range", range, sizeof(range))) ranged = MediaServer_ParseRange(range, length, &first, &last);
    if (ranged < 0)
    {
        snprintf(header, sizeof(header), "HTTP/1.1 416 Range Not Satisfiable\r\n" CORS_HEADERS "Content-Range: bytes */%zu\r\nContent-Length: 0\r\nConnection: close\r\n\r\n", subtitles->size);
        SendAll(connection->socket, header, strlen(header)); return 0;
    }
    if (ranged) { length = last - first + 1; status = 206; }
    snprintf(header, sizeof(header), "HTTP/1.1 %d %s\r\nContent-Type: text/vtt; charset=utf-8\r\n" CORS_HEADERS
             "Content-Length: %llu\r\nAccept-Ranges: bytes\r\nConnection: keep-alive\r\n", status, ranged ? "Partial Content" : "OK", (unsigned long long)length);
    if (ranged) snprintf(header + strlen(header), sizeof(header) - strlen(header), "Content-Range: bytes %llu-%llu/%zu\r\n", (unsigned long long)first, (unsigned long long)last, subtitles->size);
    strcat_s(header, sizeof(header), "\r\n");
    return SendAll(connection->socket, header, strlen(header)) && (!strncmp(request, "HEAD ", 5) || SendAll(connection->socket, subtitles->data + first, (size_t)length));
}

static DWORD WINAPI ServeConnection(LPVOID pParameter)
{
    Connection* pConnection = (Connection*)pParameter;
    const BOOL  noDelay     = TRUE;
    const int   sendBuffer  = SEND_BUFFER_SIZE;
    char        request[REQUEST_CAPACITY];
    int         used = 0;

    /* Headers and the first body chunk leave in separate sends; never let Nagle pair them with a delayed ACK. */
    setsockopt(pConnection->socket, IPPROTO_TCP, TCP_NODELAY, (const char*)&noDelay, sizeof(noDelay));
    setsockopt(pConnection->socket, SOL_SOCKET, SO_SNDBUF, (const char*)&sendBuffer, sizeof(sendBuffer));
    /* Share read AND write: a growing proxy file is still held open for writing by the muxer, so
    ** without FILE_SHARE_WRITE this open would fail with a sharing violation. */
    pConnection->hFile = CreateFileW(pConnection->file, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    request[0]         = 0;
    for (;;)
    {
        char* pEnd;
        int   requestLength;

        while (NULL == (pEnd = strstr(request, "\r\n\r\n")))
        {
            int received;

            if (used >= (int)sizeof(request) - 1)
            {
                goto done;
            }
            received = recv(pConnection->socket, request + used, (int)sizeof(request) - 1 - used, 0);
            if (received <= 0)
            {
                goto done;
            }
            used += received;
            request[used] = 0;
        }
        requestLength = (int)(pEnd + 4 - request);
        if (!strncmp(request, "OPTIONS ", 8))
        {
            static const char response[] = "HTTP/1.1 204 No Content\r\n" CORS_HEADERS "Content-Length: 0\r\nConnection: keep-alive\r\n\r\n";
            if (!SendAll(pConnection->socket, response, sizeof(response) - 1)) break;
        }
        else if (!strncmp(request, "GET /subtitles.vtt ", 19) || !strncmp(request, "HEAD /subtitles.vtt ", 20))
        {
            if (!HandleSubtitleRequest(pConnection, request)) break;
        }
        else if (!pConnection->file[0])
        {
            static const char response[] = "HTTP/1.1 404 Not Found\r\n" CORS_HEADERS "Content-Length: 0\r\nConnection: close\r\n\r\n";
            SendAll(pConnection->socket, response, sizeof(response)-1); break;
        }
        else if (NULL != pConnection->live)
        {
            (void)HandleGrowingRequest(pConnection, request);
            break; /* a growing response streams to end of file, then closes */
        }
        else if (!HandleRequest(pConnection, request))
        {
            break;
        }
        /* drop the answered request; a pipelined next request may already be buffered */
        memmove(request, request + requestLength, (size_t)(used - requestLength));
        used -= requestLength;
        request[used] = 0;
    }
done:
    if (INVALID_HANDLE_VALUE != pConnection->hFile)
    {
        CloseHandle(pConnection->hFile);
    }
    shutdown(pConnection->socket, SD_SEND);
    closesocket(pConnection->socket);
    LiveState_Release(pConnection->live);
    SubtitleData_Release(pConnection->subtitles);
    free(pConnection);
    return 0;
}

static DWORD WINAPI AcceptLoop(LPVOID pParameter)
{
    MediaServer* pServer = (MediaServer*)pParameter;

    for (;;)
    {
        SOCKET      s = accept(pServer->listener, NULL, NULL);
        Connection* pConnection;
        HANDLE      hThread;

        if (INVALID_SOCKET == s)
        {
            if (pServer->stopping)
            {
                break;
            }
            continue;
        }
        pConnection = (Connection*)calloc(1, sizeof(*pConnection));
        if (NULL == pConnection)
        {
            closesocket(s);
            continue;
        }
        pConnection->socket = s;
        pConnection->size   = pServer->size;
        pConnection->live   = pServer->live;
        pConnection->subtitles = pServer->subtitles;
        if (pConnection->subtitles) InterlockedIncrement(&pConnection->subtitles->refcount);
        LiveState_AddRef(pConnection->live);
        memcpy(pConnection->file, pServer->file, sizeof(pConnection->file));
        memcpy(pConnection->contentType, pServer->contentType, sizeof(pConnection->contentType));
        hThread = CreateThread(NULL, 0, ServeConnection, pConnection, 0, NULL);
        if (NULL == hThread)
        {
            closesocket(s);
            LiveState_Release(pConnection->live);
            SubtitleData_Release(pConnection->subtitles);
            free(pConnection);
            continue;
        }
        CloseHandle(hThread);
    }
    return 0;
}

static MediaServer* StartInternal(const wchar_t* pFile, const char* pBindAddress, unsigned short port, const char* pContentType, int growing, const char* pVtt, size_t vttSize, char* pError, size_t errorCapacity)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    MediaServer*              pServer;
    struct sockaddr_in        address;
    int                       addressLength = sizeof(address);
    const BOOL                exclusive     = TRUE;

    ZeroMemory(&attributes, sizeof(attributes));
    if (pFile && (!GetFileAttributesExW(pFile, GetFileExInfoStandard, &attributes) || (FILE_ATTRIBUTE_DIRECTORY & attributes.dwFileAttributes)))
    {
        (void)snprintf(pError, errorCapacity, "cannot open the media file");
        return NULL;
    }
    pServer = (MediaServer*)calloc(1, sizeof(*pServer));
    if (NULL == pServer)
    {
        (void)snprintf(pError, errorCapacity, "out of memory");
        return NULL;
    }
    pServer->size = ((uint64_t)attributes.nFileSizeHigh << 32) | attributes.nFileSizeLow;
    (void)snprintf(pServer->contentType, sizeof(pServer->contentType), "%s", pContentType);
    if (pFile && wcslen(pFile) >= MAX_PATH)
    {
        (void)snprintf(pError, errorCapacity, "the media path is too long");
        free(pServer);
        return NULL;
    }
    if (pFile) wcscpy_s(pServer->file, MAX_PATH, pFile);

    pServer->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (INVALID_SOCKET == pServer->listener)
    {
        (void)snprintf(pError, errorCapacity, "socket failed (%d)", WSAGetLastError());
        free(pServer);
        return NULL;
    }
    setsockopt(pServer->listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char*)&exclusive, sizeof(exclusive));
    ZeroMemory(&address, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port   = htons(port);
    if (NULL == pBindAddress || 1 != inet_pton(AF_INET, pBindAddress, &address.sin_addr))
    {
        address.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    if (0 != bind(pServer->listener, (struct sockaddr*)&address, sizeof(address)) || 0 != listen(pServer->listener, SOMAXCONN) ||
        0 != getsockname(pServer->listener, (struct sockaddr*)&address, &addressLength))
    {
        (void)snprintf(pError, errorCapacity, "cannot listen on port %u (%d)", (unsigned)port, WSAGetLastError());
        closesocket(pServer->listener);
        free(pServer);
        return NULL;
    }
    pServer->port = ntohs(address.sin_port);
    if (growing)
    {
        pServer->live = (LiveState*)calloc(1, sizeof(*pServer->live));
        if (NULL == pServer->live)
        {
            (void)snprintf(pError, errorCapacity, "out of memory");
            closesocket(pServer->listener);
            free(pServer);
            return NULL;
        }
        pServer->live->refcount = 1; /* the server's own reference */
    }
    if (pVtt && vttSize)
    {
        pServer->subtitles = (SubtitleData*)malloc(sizeof(SubtitleData) + vttSize);
        if (!pServer->subtitles)
        {
            snprintf(pError, errorCapacity, "out of memory");
            closesocket(pServer->listener); LiveState_Release(pServer->live); free(pServer); return NULL;
        }
        pServer->subtitles->refcount = 1;
        pServer->subtitles->size = vttSize;
        memcpy(pServer->subtitles->data, pVtt, vttSize);
    }
    pServer->thread = CreateThread(NULL, 0, AcceptLoop, pServer, 0, NULL);
    if (NULL == pServer->thread)
    {
        (void)snprintf(pError, errorCapacity, "CreateThread failed (%lu)", GetLastError());
        closesocket(pServer->listener);
        LiveState_Release(pServer->live);
        SubtitleData_Release(pServer->subtitles);
        free(pServer);
        return NULL;
    }
    return pServer;
}

MediaServer* MediaServer_Start(const wchar_t* pFile, const char* pBindAddress, unsigned short port, const char* pContentType, char* pError, size_t errorCapacity)
{
    return StartInternal(pFile, pBindAddress, port, pContentType, 0, NULL, 0, pError, errorCapacity);
}

MediaServer* MediaServer_StartWithSubtitles(const wchar_t* pFile, const char* pBindAddress, unsigned short port, const char* pContentType,
                                          const char* pVtt, size_t vttSize, char* pError, size_t errorCapacity)
{
    return StartInternal(pFile, pBindAddress, port, pContentType, 0, pVtt, vttSize, pError, errorCapacity);
}

MediaServer* MediaServer_StartGrowing(const wchar_t* pFile, const char* pBindAddress, unsigned short port, const char* pContentType, char* pError, size_t errorCapacity)
{
    return StartInternal(pFile, pBindAddress, port, pContentType, 1, NULL, 0, pError, errorCapacity);
}

void MediaServer_MarkComplete(MediaServer* pServer)
{
    if (NULL != pServer && NULL != pServer->live)
    {
        InterlockedExchange(&pServer->live->complete, 1);
    }
}

unsigned short MediaServer_GetPort(const MediaServer* pServer)
{
    return pServer->port;
}

void MediaServer_Stop(MediaServer* pServer)
{
    if (NULL == pServer)
    {
        return;
    }
    InterlockedExchange(&pServer->stopping, 1);
    if (NULL != pServer->live)
    {
        InterlockedExchange(&pServer->live->complete, 1); /* let streaming connections finish */
    }
    closesocket(pServer->listener); /* unblocks accept */
    WaitForSingleObject(pServer->thread, 2000);
    CloseHandle(pServer->thread);
    LiveState_Release(pServer->live); /* connections in flight keep their own reference */
    SubtitleData_Release(pServer->subtitles);
    free(pServer);
}
