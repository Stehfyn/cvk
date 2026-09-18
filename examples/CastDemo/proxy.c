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

/* Called from the transcode once the first fragment is on disk: start serving the growing file and
** hand its URL to the session so playback can switch over within a fraction of a second, while the
** rest of the proxy keeps transcoding and appending fragments in the background. */
static void OnProxyReady(void* pUser)
{
    Proxy* pProxy = (Proxy*)pUser;
    char   error[256];
    char   url[1300];

    if (NULL != pProxy->pServer || 0 != pProxy->stopping)
    {
        return; /* already serving, or shutting down */
    }
    pProxy->pServer = MediaServer_Start(pProxy->output, pProxy->localAddress[0] ? pProxy->localAddress : NULL, pProxy->port, "video/mp4", error, sizeof(error));
    if (NULL == pProxy->pServer)
    {
        return;
    }
    (void)snprintf(url, sizeof(url), "http://%s:%u/proxy.mp4", pProxy->localAddress, (unsigned)MediaServer_GetPort(pProxy->pServer));
    CastSession_SetProxy(pProxy->pSession, url, "video/mp4");
}

/* The proxy encode: 480-wide GPU H.264 with a short GOP for fast seeks and the source AAC passed
** straight through, written as a fragmented MP4 so it is playable within a fraction of a second and
** grows in the background. Runs entirely on the GPU through cvk's Vulkan Video wrappers. */
static DWORD WINAPI ProxyThread(LPVOID pParameter)
{
    Proxy* pProxy = (Proxy*)pParameter;
    char   report[1800];

    (void)GpuTranscode_ToFragmentedMp4(pProxy->source, pProxy->output, &pProxy->config, 0, OnProxyReady, pProxy, &pProxy->stopping, report, sizeof(report));
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
