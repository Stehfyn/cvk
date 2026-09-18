/* proxy.c - builds the low-latency scrub proxy in-process with the GPU Vulkan-Video transcoder
** (no external tools), then serves the resulting MP4. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gpu_transcode.h"
#include "media_server.h"
#include "proxy.h"

struct Proxy
{
    HANDLE             thread;
    volatile LONG      stopping;
    MediaServer*       pServer;
    CastSession*       pSession;
    GpuTranscodeConfig config;
    WCHAR              source[MAX_PATH];
    WCHAR              output[MAX_PATH];
    char               localAddress[64];
    unsigned short     port;
};

/* The proxy encode: a GPU H.264 re-encode with a short GOP for fast seeks and the source AAC passed
** straight through, written as a fragmented MP4. Runs entirely on the GPU through cvk's Vulkan Video
** wrappers. The whole file is transcoded first, then served as a complete file: a receiver only
** treats an HTTP source as a fixed-duration, seekable VOD (honoring the container's full duration
** and not chasing a live edge) when the response carries a real Content-Length, which is knowable
** only once the constant-QP encode has finished and the final size is fixed. The original stream
** plays meanwhile, so playback still starts immediately; the switch happens when the proxy is
** ready. */
static DWORD WINAPI ProxyThread(LPVOID pParameter)
{
    Proxy* pProxy = (Proxy*)pParameter;
    char   report[1800];
    char   error[256];
    char   url[1300];

    if (!GpuTranscode_ToFragmentedMp4(pProxy->source, pProxy->output, &pProxy->config, 0, NULL, NULL, &pProxy->stopping, report, sizeof(report)) || 0 != pProxy->stopping)
    {
        return 0; /* transcode failed or was cancelled: keep playing the full stream */
    }
    pProxy->pServer = MediaServer_Start(pProxy->output, pProxy->localAddress[0] ? pProxy->localAddress : NULL, pProxy->port, "video/mp4", error, sizeof(error));
    if (NULL == pProxy->pServer)
    {
        return 0;
    }
    (void)snprintf(url, sizeof(url), "http://%s:%u/proxy.mp4", pProxy->localAddress, (unsigned)MediaServer_GetPort(pProxy->pServer));
    CastSession_SetProxy(pProxy->pSession, url, "video/mp4");
    return 0;
}

Proxy* Proxy_Start(const wchar_t* pSourceFile, const GpuTranscodeConfig* pConfig, const char* pLocalAddress, unsigned short port, CastSession* pSession)
{
    Proxy* pProxy = (Proxy*)calloc(1, sizeof(*pProxy));
    DWORD  length;

    if (NULL == pProxy)
    {
        return NULL;
    }
    if (NULL != pConfig)
    {
        pProxy->config = *pConfig;
    }
    else
    {
        GpuTranscode_DefaultConfig(&pProxy->config);
    }
    pProxy->pSession = pSession;
    pProxy->port     = port;
    (void)snprintf(pProxy->localAddress, sizeof(pProxy->localAddress), "%s", pLocalAddress ? pLocalAddress : "");
    if (wcslen(pSourceFile) >= MAX_PATH)
    {
        free(pProxy);
        return NULL;
    }
    wcscpy_s(pProxy->source, MAX_PATH, pSourceFile);
    length = GetTempPathW(MAX_PATH, pProxy->output);
    if (0 == length || length > MAX_PATH - 32)
    {
        free(pProxy);
        return NULL;
    }
    (void)_snwprintf_s(pProxy->output + length, MAX_PATH - length, _TRUNCATE, L"cvkcast_proxy_%lu.mp4", GetCurrentProcessId());
    pProxy->thread = CreateThread(NULL, 0, ProxyThread, pProxy, 0, NULL);
    if (NULL == pProxy->thread)
    {
        free(pProxy);
        return NULL;
    }
    return pProxy;
}

void Proxy_Stop(Proxy* pProxy)
{
    if (NULL == pProxy)
    {
        return;
    }
    InterlockedExchange(&pProxy->stopping, 1);
    if (NULL != pProxy->thread)
    {
        WaitForSingleObject(pProxy->thread, INFINITE); /* the transcode checks stopping and unwinds */
        CloseHandle(pProxy->thread);
    }
    MediaServer_Stop(pProxy->pServer);
    if (0 != pProxy->output[0])
    {
        DeleteFileW(pProxy->output);
    }
    free(pProxy);
}
