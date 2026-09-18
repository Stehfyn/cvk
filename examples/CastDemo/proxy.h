/* proxy.h - builds a low-latency scrub proxy for a local media file and serves it.
**
** The proxy is a low-resolution, short-GOP, no-B-frame H.264 transcode with the source AAC audio
** passed through. It is cheap for the receiver to buffer and decode, so seeks into it are
** near-instant. A background thread runs the in-process GPU Vulkan-Video transcoder (no external
** tools), then serves the result and hands its URL to the session, which scrubs on it and upgrades
** back to the full-quality stream once the user settles. */
#ifndef CAST_PROXY_H
#define CAST_PROXY_H

#include <wchar.h>

#include "cast_session.h"
#include "gpu_transcode.h"

typedef struct Proxy Proxy;

/* Starts building and serving the proxy for pSourceFile with the given transcode settings, bound to
** pLocalAddress:port. When ready it calls CastSession_SetProxy on pSession. pConfig may be NULL for
** the defaults. Returns NULL on error (the session then scrubs on the full stream). */
Proxy* Proxy_Start(const wchar_t* pSourceFile, const GpuTranscodeConfig* pConfig, const char* pLocalAddress, unsigned short port, CastSession* pSession);

/* Stops the proxy server, ends the transcode if still running, and removes the temporary file. */
void Proxy_Stop(Proxy* pProxy);

#endif
