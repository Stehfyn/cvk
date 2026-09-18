/* media_server.h - a minimal HTTP/1.1 file server for one video. The receiver fetches the
** file itself, so the server must answer GET and HEAD with byte ranges (206) and stream the
** body while the receiver plays; each connection runs on its own thread. */
#ifndef CAST_MEDIA_SERVER_H
#define CAST_MEDIA_SERVER_H

#include <stddef.h>
#include <stdint.h>
#include <wchar.h>

typedef struct MediaServer MediaServer;

/* Serves the file on the bind address (dotted IPv4, or NULL for every interface) and port
** (0 chooses one). WSAStartup must have been called. NULL and pError on failure. */
MediaServer* MediaServer_Start(const wchar_t* pFile, const char* pBindAddress, unsigned short port, const char* pContentType, char* pError, size_t errorCapacity);

unsigned short MediaServer_GetPort(const MediaServer* pServer);

/* Stops accepting; connections in flight finish on their own. */
void MediaServer_Stop(MediaServer* pServer);

/* Parses a Range header value ("bytes=first-last", "bytes=first-", "bytes=-suffix") against
** a file of the given size. Returns 1 with the clamped inclusive range, 0 when the header
** should be ignored (whole file), -1 when the range is unsatisfiable. */
int MediaServer_ParseRange(const char* pValue, uint64_t size, uint64_t* pFirst, uint64_t* pLast);

/* Maps a file extension (".mp4") to a media type; "application/octet-stream" when unknown. */
const char* MediaServer_ContentTypeForExtension(const wchar_t* pExtension);

/* The dotted IPv4 address of the local interface that routes to pHost: the address the
** receiver must be given to reach this server. Returns 1 on success. */
int MediaServer_LocalAddressToward(const char* pHost, char* pOut, size_t capacity);

#endif
