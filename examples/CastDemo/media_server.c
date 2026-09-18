/* media_server.c - Winsock listener, one thread per request, ReadFile/send streaming. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media_server.h"

#define REQUEST_CAPACITY (8192)
#define CHUNK_SIZE       (64 * 1024)

struct MediaServer
{
    SOCKET         listener;
    HANDLE         thread;
    volatile LONG  stopping;
    WCHAR          file[MAX_PATH];
    char           contentType[64];
    unsigned short port;
    uint64_t       size;
};

/* Everything a request thread needs, copied so the server may be freed while it streams. */
typedef struct Connection
{
    SOCKET   socket;
    WCHAR    file[MAX_PATH];
    char     contentType[64];
    uint64_t size;
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
        {L".mp4", "video/mp4"},   {L".m4v", "video/mp4"},   {L".mov", "video/quicktime"}, {L".webm", "video/webm"},  {L".mkv", "video/x-matroska"},
        {L".mp3", "audio/mpeg"},  {L".m4a", "audio/mp4"},   {L".aac", "audio/aac"},       {L".flac", "audio/flac"},  {L".wav", "audio/wav"},
        {L".ogg", "audio/ogg"},   {L".jpg", "image/jpeg"},  {L".jpeg", "image/jpeg"},     {L".png", "image/png"},    {L".gif", "image/gif"},
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
    ok = (0 == connect(s, (struct sockaddr*)&remote, sizeof(remote))) && (0 == getsockname(s, (struct sockaddr*)&local, &length)) &&
         (NULL != inet_ntop(AF_INET, &local.sin_addr, pOut, capacity));
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
        const char*  pEnd;
        size_t       lineLength;

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

static void StreamFile(const Connection* pConnection, uint64_t first, uint64_t remaining)
{
    HANDLE        hFile = CreateFileW(pConnection->file, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    LARGE_INTEGER offset;
    uint8_t*      pChunk;

    if (INVALID_HANDLE_VALUE == hFile)
    {
        return;
    }
    pChunk = (uint8_t*)malloc(CHUNK_SIZE);
    offset.QuadPart = (LONGLONG)first;
    if (NULL != pChunk && SetFilePointerEx(hFile, offset, NULL, FILE_BEGIN))
    {
        while (remaining > 0)
        {
            const DWORD want = (remaining < CHUNK_SIZE) ? (DWORD)remaining : CHUNK_SIZE;
            DWORD       read = 0;

            if (!ReadFile(hFile, pChunk, want, &read, NULL) || 0 == read || !SendAll(pConnection->socket, pChunk, read))
            {
                break; /* the receiver closed the connection (normal for range probes) or the file shrank */
            }
            remaining -= read;
        }
    }
    free(pChunk);
    CloseHandle(hFile);
}

static DWORD WINAPI ServeConnection(LPVOID pParameter)
{
    Connection* pConnection = (Connection*)pParameter;
    char        request[REQUEST_CAPACITY];
    char        header[4096];
    char        range[128];
    char        target[256];
    int         used = 0;
    int         head;
    int         rangeResult = 0;
    uint64_t    first       = 0;
    uint64_t    last        = 0;
    uint64_t    length;
    int         status;

    for (;;)
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
        if (NULL != strstr(request, "\r\n\r\n"))
        {
            break;
        }
    }
    head = (0 == strncmp(request, "HEAD ", 5));
    if (!head && 0 != strncmp(request, "GET ", 4))
    {
        static const char kNotAllowed[] = "HTTP/1.1 405 Method Not Allowed\r\nAllow: GET, HEAD\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

        SendAll(pConnection->socket, kNotAllowed, sizeof(kNotAllowed) - 1);
        goto done;
    }
    (void)snprintf(target, sizeof(target), "%.*s", (int)strcspn(request + (head ? 5 : 4), " \r\n"), request + (head ? 5 : 4));
    if (FindHeader(request, "Range", range, sizeof(range)))
    {
        rangeResult = MediaServer_ParseRange(range, pConnection->size, &first, &last);
    }
    if (rangeResult < 0)
    {
        (void)snprintf(header, sizeof(header), "HTTP/1.1 416 Range Not Satisfiable\r\nContent-Range: bytes */%llu\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
                       (unsigned long long)pConnection->size);
        SendAll(pConnection->socket, header, strlen(header));
        (void)fprintf(stderr, "server: %s %s -> 416 (%s)\n", head ? "HEAD" : "GET", target, range);
        goto done;
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
    (void)snprintf(header, sizeof(header),
                   "HTTP/1.1 %d %s\r\n"
                   "Content-Type: %s\r\n"
                   "Content-Length: %llu\r\n"
                   "Accept-Ranges: bytes\r\n"
                   "Access-Control-Allow-Origin: *\r\n"
                   "Connection: close\r\n",
                   status, (200 == status) ? "OK" : "Partial Content", pConnection->contentType, (unsigned long long)length);
    if (206 == status)
    {
        (void)snprintf(header + strlen(header), sizeof(header) - strlen(header), "Content-Range: bytes %llu-%llu/%llu\r\n", (unsigned long long)first, (unsigned long long)last,
                       (unsigned long long)pConnection->size);
    }
    (void)snprintf(header + strlen(header), sizeof(header) - strlen(header), "\r\n");
    (void)fprintf(stderr, "server: %s %s -> %d bytes %llu-%llu\n", head ? "HEAD" : "GET", target, status, (unsigned long long)first, (unsigned long long)last);
    if (!SendAll(pConnection->socket, header, strlen(header)) || head || 0 == length)
    {
        goto done;
    }
    StreamFile(pConnection, first, length);

done:
    shutdown(pConnection->socket, SD_SEND);
    closesocket(pConnection->socket);
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
        memcpy(pConnection->file, pServer->file, sizeof(pConnection->file));
        memcpy(pConnection->contentType, pServer->contentType, sizeof(pConnection->contentType));
        hThread = CreateThread(NULL, 0, ServeConnection, pConnection, 0, NULL);
        if (NULL == hThread)
        {
            closesocket(s);
            free(pConnection);
            continue;
        }
        CloseHandle(hThread);
    }
    return 0;
}

MediaServer* MediaServer_Start(const wchar_t* pFile, const char* pBindAddress, unsigned short port, const char* pContentType, char* pError, size_t errorCapacity)
{
    WIN32_FILE_ATTRIBUTE_DATA attributes;
    MediaServer*              pServer;
    struct sockaddr_in        address;
    int                       addressLength = sizeof(address);
    const BOOL                exclusive     = TRUE;

    if (!GetFileAttributesExW(pFile, GetFileExInfoStandard, &attributes) || (FILE_ATTRIBUTE_DIRECTORY & attributes.dwFileAttributes))
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
    if (wcslen(pFile) >= MAX_PATH)
    {
        (void)snprintf(pError, errorCapacity, "the media path is too long");
        free(pServer);
        return NULL;
    }
    wcscpy_s(pServer->file, MAX_PATH, pFile);

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
    pServer->port   = ntohs(address.sin_port);
    pServer->thread = CreateThread(NULL, 0, AcceptLoop, pServer, 0, NULL);
    if (NULL == pServer->thread)
    {
        (void)snprintf(pError, errorCapacity, "CreateThread failed (%lu)", GetLastError());
        closesocket(pServer->listener);
        free(pServer);
        return NULL;
    }
    return pServer;
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
    closesocket(pServer->listener); /* unblocks accept */
    WaitForSingleObject(pServer->thread, 2000);
    CloseHandle(pServer->thread);
    free(pServer);
}
