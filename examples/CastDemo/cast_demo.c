#define _CRT_SECURE_NO_WARNINGS
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
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "cast_channel.h"
#include "cast_session.h"
#include "media_server.h"
#include "mp4_index.h"
#include "gpu_device.h"
#include "gpu_decode.h"
#include "gpu_encode.h"
#include "gpu_transcode.h"
#include "h264_params.h"
#include "mp4_demux.h"
#include "proxy.h"
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
    double         seek;   /* > 0: seek here three seconds into playback, to time the round trip */
    double         scrub;  /* > 0: preview this position (paused, keyframe-snapped) at three seconds, release at five */
    unsigned       bounce; /* > 0: after playback starts, jump to this many varied positions, timing each */
    int            loop;
    int            list;
    int            verbose;
} Options;

static volatile LONG      g_interrupted;
static volatile ULONGLONG g_playingSince; /* first PLAYING status, for --seconds */
static char               g_lastState[32];
static ULONGLONG          g_lastProgress;
static int                g_runResult;
static volatile double    g_duration; /* media duration once the receiver reports it */

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
                  "            [--seconds N] [--seek T] [--scrub T] [--bounce N] [--loop] [--list] [--verbose]\n"
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
        else if (0 == wcscmp(pArgument, L"--seek") && hasValue)
        {
            pOptions->seek = wcstod(argv[++index], NULL);
        }
        else if (0 == wcscmp(pArgument, L"--scrub") && hasValue)
        {
            pOptions->scrub = wcstod(argv[++index], NULL);
        }
        else if (0 == wcscmp(pArgument, L"--bounce") && hasValue)
        {
            pOptions->bounce = (unsigned)wcstoul(argv[++index], NULL, 10);
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
        if (pStatus->duration > 0)
        {
            g_duration = pStatus->duration;
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
    MediaServer*      pServer    = NULL;
    Proxy*            pProxy     = NULL;
    double*           pKeyframes = NULL;
    HANDLE            hThread;
    WSADATA           wsaData;
    char              error[256];
    char              localAddress[64];
    int               stopping = 0;

    SetConsoleOutputCP(CP_UTF8);
    setvbuf(stdout, NULL, _IONBF, 0);
    {
        int argIndex;

        for (argIndex = 1; argIndex < argc; ++argIndex)
        {
            if (0 == wcscmp(argv[argIndex], L"--gpuprobe"))
            {
                GpuTranscodeCaps caps;
                char             report[1024];

                GpuTranscode_Probe(&caps, report, sizeof(report));
                (void)printf("%s", report);
                return caps.available ? 0 : 1;
            }
            if (0 == wcscmp(argv[argIndex], L"--encodeinit"))
            {
                GpuDevice*  pGpu;
                GpuEncoder* pEncoder;
                char        device_report[256];
                char        report[512];

                pGpu = GpuDevice_Create(0, 1, device_report, sizeof(device_report));
                (void)printf("%s", device_report);
                if (NULL == pGpu)
                {
                    return 1;
                }
                pEncoder = GpuEncode_Create(pGpu, 480, 272, 600, 12, 24, 28, 30, 1, report, sizeof(report));
                (void)printf("%s", report);
                GpuEncode_Destroy(pEncoder);
                GpuDevice_Destroy(pGpu);
                return pEncoder ? 0 : 1;
            }
            if (0 == wcscmp(argv[argIndex], L"--encodeframe"))
            {
                GpuDevice*     pGpu;
                GpuEncoder*    pEncoder;
                char           device_report[256];
                char           report[512];
                const unsigned encode_width  = 480;
                const unsigned encode_height = 272;
                unsigned       frame_count;
                uint8_t*       luma;
                uint8_t*       chroma;
                unsigned       frame;
                uint64_t       total;

                frame_count = 30;
                if (argIndex + 1 < argc)
                {
                    frame_count = (unsigned)_wtoi(argv[argIndex + 1]);
                }
                pGpu = GpuDevice_Create(0, 1, device_report, sizeof(device_report));
                (void)printf("%s", device_report);
                if (NULL == pGpu)
                {
                    return 1;
                }
                pEncoder = GpuEncode_Create(pGpu, encode_width, encode_height, 600, 12, 24, 28, 30, 1, report, sizeof(report));
                (void)printf("%s", report);
                if (NULL == pEncoder)
                {
                    GpuDevice_Destroy(pGpu);
                    return 1;
                }
                luma   = (uint8_t*)malloc((size_t)encode_width * encode_height);
                chroma = (uint8_t*)malloc((size_t)encode_width * (encode_height / 2));
                total  = 0;
                for (frame = 0; frame < frame_count; ++frame)
                {
                    const uint8_t* coded;
                    uint32_t       coded_size;
                    unsigned       y;

                    for (y = 0; y < encode_height; ++y)
                    {
                        unsigned x;

                        for (x = 0; x < encode_width; ++x)
                        {
                            luma[(size_t)y * encode_width + x] = (uint8_t)(x + y + frame * 4u);
                        }
                    }
                    for (y = 0; y < encode_height / 2u; ++y)
                    {
                        unsigned x;

                        for (x = 0; x < encode_width; ++x)
                        {
                            chroma[(size_t)y * encode_width + x] = (uint8_t)(128 + ((int)frame * 2));
                        }
                    }
                    coded = GpuEncode_Frame(pEncoder, luma, encode_width, chroma, encode_width, 0, 0, &coded_size);
                    total += coded_size;
                    if (coded && coded_size >= 5)
                    {
                        int start_code;
                        int nal_type;

                        start_code = (coded[0] == 0 && coded[1] == 0 && coded[2] == 0 && coded[3] == 1) || (coded[0] == 0 && coded[1] == 0 && coded[2] == 1);
                        nal_type   = (coded[0] == 0 && coded[1] == 0 && coded[2] == 1) ? (coded[3] & 0x1F) : (coded[4] & 0x1F);
                        (void)printf("frame %2u: ok %5u bytes, %s, first NAL type %d\n", frame, coded_size, start_code ? "annex-b" : "no start code", nal_type);
                    }
                    else
                    {
                        (void)printf("frame %2u: FAIL %u bytes\n", frame, coded_size);
                    }
                }
                (void)printf("encoded %u frames, %llu bytes total, avg %llu B/frame\n", frame_count, (unsigned long long)total, (unsigned long long)(frame_count ? total / frame_count : 0));
                free(luma);
                free(chroma);
                GpuEncode_Destroy(pEncoder);
                GpuDevice_Destroy(pGpu);
                return 0;
            }
            if (0 == wcscmp(argv[argIndex], L"--transcode") && argIndex + 1 < argc)
            {
                GpuTranscoder*     pTranscoder;
                GpuTranscodeConfig tconfig;
                char               report[1600];
                unsigned           frame_limit;
                unsigned           frame;
                const uint8_t*     header;
                uint32_t           header_size;
                FILE*              out;
                uint64_t           total;
                unsigned           keyframes;

                GpuTranscode_DefaultConfig(&tconfig);
                frame_limit = (argIndex + 2 < argc) ? (unsigned)wcstoul(argv[argIndex + 2], NULL, 10) : 60u;
                pTranscoder = GpuTranscode_Open(argv[argIndex + 1], &tconfig, report, sizeof(report));
                (void)printf("%s", report);
                if (NULL == pTranscoder)
                {
                    return 1;
                }
                if (frame_limit > GpuTranscode_FrameCount(pTranscoder))
                {
                    frame_limit = GpuTranscode_FrameCount(pTranscoder);
                }
                out    = fopen("proxy.264", "wb");
                header = GpuTranscode_Header(pTranscoder, &header_size);
                if (out && header && header_size)
                {
                    (void)fwrite(header, 1, header_size, out);
                }
                total     = 0;
                keyframes = 0;
                for (frame = 0; frame < frame_limit; ++frame)
                {
                    const uint8_t* coded;
                    uint32_t       coded_size;
                    int            keyframe;

                    coded = GpuTranscode_NextFrame(pTranscoder, &coded_size, &keyframe);
                    if (NULL == coded)
                    {
                        (void)printf("frame %u: end of stream or failure\n", frame);
                        break;
                    }
                    if (out)
                    {
                        (void)fwrite(coded, 1, coded_size, out);
                    }
                    total += coded_size;
                    keyframes += (unsigned)(keyframe != 0);
                    if (frame < 8 || (frame % 30) == 0)
                    {
                        (void)printf("frame %3u: %5u bytes%s\n", frame, coded_size, keyframe ? " (keyframe)" : "");
                    }
                }
                if (out)
                {
                    (void)fclose(out);
                }
                (void)printf("transcoded %u frames (%u keyframes), %llu bytes -> proxy.264 (%ux%u)\n", frame, keyframes, (unsigned long long)total, GpuTranscode_OutWidth(pTranscoder), GpuTranscode_OutHeight(pTranscoder));
                GpuTranscode_Close(pTranscoder);
                return 0;
            }
            if (0 == wcscmp(argv[argIndex], L"--fragmp4") && argIndex + 2 < argc)
            {
                char               report[1800];
                GpuTranscodeConfig fragConfig;
                unsigned           max_frames;
                int                ok;

                GpuTranscode_DefaultConfig(&fragConfig);
                max_frames = (argIndex + 3 < argc) ? (unsigned)wcstoul(argv[argIndex + 3], NULL, 10) : 0u;
                ok         = GpuTranscode_ToFragmentedMp4(argv[argIndex + 1], argv[argIndex + 2], &fragConfig, max_frames, NULL, NULL, NULL, report, sizeof(report));
                (void)printf("%s", report);
                return ok ? 0 : 1;
            }
            if (0 == wcscmp(argv[argIndex], L"--proxymp4") && argIndex + 2 < argc)
            {
                char               report[1800];
                GpuTranscodeConfig proxyConfig;
                unsigned           max_frames;
                int                ok;

                GpuTranscode_DefaultConfig(&proxyConfig);
                max_frames = (argIndex + 3 < argc) ? (unsigned)wcstoul(argv[argIndex + 3], NULL, 10) : 0u;
                ok         = GpuTranscode_ToMp4(argv[argIndex + 1], argv[argIndex + 2], &proxyConfig, 0, max_frames, NULL, report, sizeof(report));
                (void)printf("%s", report);
                return ok ? 0 : 1;
            }
            if (0 == wcscmp(argv[argIndex], L"--decodeinit") && argIndex + 1 < argc)
            {
                Mp4Demux*                        pDemux = Mp4Demux_Open(argv[argIndex + 1]);
                StdVideoH264SequenceParameterSet sps;
                StdVideoH264PictureParameterSet  pps;
                unsigned                         w, h;
                uint32_t                         spsSize, ppsSize;
                char                             report[512];
                char                             deviceReport[256];
                GpuDevice*                       pGpu;
                GpuDecoder*                      pDecoder;

                if (NULL == pDemux)
                {
                    (void)printf("not an AVC MP4\n");
                    return 1;
                }
                if (!H264_ParseSps(Mp4Demux_Sps(pDemux, &spsSize), spsSize, &sps, &w, &h) || !H264_ParsePps(Mp4Demux_Pps(pDemux, &ppsSize), ppsSize, &pps))
                {
                    (void)printf("SPS/PPS parse failed\n");
                    Mp4Demux_Close(pDemux);
                    return 1;
                }
                pGpu = GpuDevice_Create(1, 0, deviceReport, sizeof(deviceReport));
                (void)printf("%s", deviceReport);
                if (NULL == pGpu)
                {
                    Mp4Demux_Close(pDemux);
                    return 1;
                }
                pDecoder = GpuDecode_Create(pGpu, &sps, &pps, w, h, sps.max_num_ref_frames + 2u, Mp4Demux_NalLengthSize(pDemux), report, sizeof(report));
                (void)printf("%s", report);
                GpuDecode_Destroy(pDecoder);
                GpuDevice_Destroy(pGpu);
                Mp4Demux_Close(pDemux);
                return pDecoder ? 0 : 1;
            }
            if (0 == wcscmp(argv[argIndex], L"--decode") && argIndex + 1 < argc)
            {
                Mp4Demux*                        pDemux = Mp4Demux_Open(argv[argIndex + 1]);
                StdVideoH264SequenceParameterSet sps;
                StdVideoH264PictureParameterSet  pps;
                unsigned                         w, h;
                uint32_t                         spsSize, ppsSize;
                char                             report[512];
                char                             deviceReport[256];
                GpuDevice*                       pGpu;
                GpuDecoder*                      pDecoder;
                uint8_t*                         pSampleBuffer;
                unsigned                         frameCount = (argIndex + 2 < argc) ? (unsigned)wcstoul(argv[argIndex + 2], NULL, 10) : 8;
                unsigned                         frame;

                if (NULL == pDemux)
                {
                    (void)printf("not an AVC MP4\n");
                    return 1;
                }
                if (!H264_ParseSps(Mp4Demux_Sps(pDemux, &spsSize), spsSize, &sps, &w, &h) || !H264_ParsePps(Mp4Demux_Pps(pDemux, &ppsSize), ppsSize, &pps))
                {
                    (void)printf("SPS/PPS parse failed\n");
                    Mp4Demux_Close(pDemux);
                    return 1;
                }
                pGpu = GpuDevice_Create(1, 0, deviceReport, sizeof(deviceReport));
                (void)printf("%s", deviceReport);
                if (NULL == pGpu)
                {
                    Mp4Demux_Close(pDemux);
                    return 1;
                }
                pDecoder      = GpuDecode_Create(pGpu, &sps, &pps, w, h, sps.max_num_ref_frames + 2u, Mp4Demux_NalLengthSize(pDemux), report, sizeof(report));
                pSampleBuffer = (uint8_t*)malloc(4u * 1024u * 1024u);
                (void)printf("%s", report);
                if (NULL == pDecoder || NULL == pSampleBuffer)
                {
                    (void)printf("decoder or buffer allocation failed\n");
                    GpuDecode_Destroy(pDecoder);
                    GpuDevice_Destroy(pGpu);
                    free(pSampleBuffer);
                    Mp4Demux_Close(pDemux);
                    return 1;
                }
                if (frameCount > Mp4Demux_SampleCount(pDemux))
                {
                    frameCount = Mp4Demux_SampleCount(pDemux);
                }
                for (frame = 0; frame < frameCount; ++frame)
                {
                    uint32_t bytes = Mp4Demux_ReadSample(pDemux, frame, pSampleBuffer, 4u * 1024u * 1024u);

                    if (0 == bytes || !GpuDecode_DecodeFrame(pDecoder, pSampleBuffer, bytes))
                    {
                        (void)printf("frame %u: decode failed\n", frame);
                        break;
                    }
                    {
                        unsigned       lw, lh, ls, x, y;
                        const uint8_t* pLuma   = GpuDecode_Luma(pDecoder, &lw, &lh, &ls);
                        uint64_t       sum     = 0;
                        uint64_t       nonzero = 0;

                        for (y = 0; y < lh; ++y)
                        {
                            for (x = 0; x < lw; ++x)
                            {
                                const uint8_t v = pLuma[(size_t)y * ls + x];

                                sum += v;
                                nonzero += (v != 0);
                            }
                        }
                        (void)printf("frame %u: %ux%u luma mean %.1f, %.0f%% nonzero\n", frame, lw, lh, (double)sum / ((double)lw * lh), 100.0 * (double)nonzero / ((double)lw * lh));
                    }
                }
                free(pSampleBuffer);
                GpuDecode_Destroy(pDecoder);
                GpuDevice_Destroy(pGpu);
                Mp4Demux_Close(pDemux);
                return 0;
            }
            if (0 == wcscmp(argv[argIndex], L"--demux") && argIndex + 1 < argc)
            {
                Mp4Demux* pDemux = Mp4Demux_Open(argv[argIndex + 1]);
                uint32_t  spsSize, ppsSize;
                unsigned  keyframes = 0;
                unsigned  i;

                if (NULL == pDemux)
                {
                    (void)printf("not an AVC MP4, or unreadable\n");
                    return 1;
                }
                (void)Mp4Demux_Sps(pDemux, &spsSize);
                (void)Mp4Demux_Pps(pDemux, &ppsSize);
                for (i = 0; i < Mp4Demux_SampleCount(pDemux); ++i)
                {
                    keyframes += Mp4Demux_Sample(pDemux, i)->keyframe;
                }
                (void)printf("video %ux%u, timescale %u, NAL length %d, SPS %u B, PPS %u B\n",
                             Mp4Demux_Width(pDemux),
                             Mp4Demux_Height(pDemux),
                             Mp4Demux_Timescale(pDemux),
                             Mp4Demux_NalLengthSize(pDemux),
                             spsSize,
                             ppsSize);
                {
                    StdVideoH264SequenceParameterSet sps;
                    StdVideoH264PictureParameterSet  pps;
                    unsigned                         w, h;

                    if (H264_ParseSps(Mp4Demux_Sps(pDemux, &spsSize), spsSize, &sps, &w, &h) && H264_ParsePps(Mp4Demux_Pps(pDemux, &ppsSize), ppsSize, &pps))
                    {
                        (void)printf("SPS: profile %d level %d, %ux%u, %u ref frames, poc type %d; PPS: id %u, cabac %u, init_qp %d\n",
                                     (int)sps.profile_idc,
                                     (int)sps.level_idc,
                                     w,
                                     h,
                                     sps.max_num_ref_frames,
                                     (int)sps.pic_order_cnt_type,
                                     pps.pic_parameter_set_id,
                                     pps.flags.entropy_coding_mode_flag,
                                     26 + pps.pic_init_qp_minus26);
                    }
                    else
                    {
                        (void)printf("SPS/PPS parse failed\n");
                    }
                }
                (void)printf("%u samples, %u keyframes; audio codec %d, %u Hz, %u ch\n",
                             Mp4Demux_SampleCount(pDemux),
                             keyframes,
                             (int)Mp4Demux_AudioCodec(pDemux),
                             Mp4Demux_AudioSampleRate(pDemux),
                             Mp4Demux_AudioChannels(pDemux));
                for (i = 0; i < 3 && i < Mp4Demux_SampleCount(pDemux); ++i)
                {
                    const Mp4Sample* s = Mp4Demux_Sample(pDemux, i);

                    (void)printf("  sample %u: offset %llu size %u dts %lld key %d\n", i, (unsigned long long)s->offset, s->size, (long long)s->decodeTime, s->keyframe);
                }
                Mp4Demux_Close(pDemux);
                return 0;
            }
        }
    }
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
        pKeyframes = (double*)malloc(MP4_INDEX_MAX_KEYFRAMES * sizeof(double));
        if (NULL != pKeyframes)
        {
            config.keyframeCount = Mp4Index_ReadKeyframes(options.file, pKeyframes, MP4_INDEX_MAX_KEYFRAMES);
            config.pKeyframes    = pKeyframes;
        }
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
    /* Build and serve the low-latency scrub proxy in the background (local files only). */
    if (config.servingLocally)
    {
        pProxy = Proxy_Start(options.file, NULL, localAddress, (unsigned short)(MediaServer_GetPort(pServer) + 1), pSession);
        if (NULL != pProxy)
        {
            (void)printf("building responsive scrub proxy in the background...\n");
        }
    }
    SetConsoleCtrlHandler(OnConsoleControl, TRUE);
    {
        unsigned       bounceDone = 0;
        ULONGLONG      nextBounce = 0;
        const unsigned dwellMs    = 2800; /* let each resume complete before the next jump */

        while (WAIT_TIMEOUT == WaitForSingleObject(hThread, 50))
        {
            int stop = g_interrupted;

            /* Bounce: jump across the timeline many times so the numbers reflect scrubbing, not one
        ** cold seek. Positions follow the golden ratio so successive jumps are far apart. */
            if (options.bounce > 0 && 0 != g_playingSince && g_duration > 0)
            {
                const ULONGLONG t = GetTickCount64();

                if (0 == nextBounce)
                {
                    nextBounce = g_playingSince + 3000;
                }
                if (bounceDone < options.bounce && t >= nextBounce)
                {
                    const double fromFrac = 0.03 + 0.94 * fmod(0.137 + (bounceDone + 0) * 0.6180339887, 1.0);
                    const double toFrac   = 0.03 + 0.94 * fmod(0.137 + (bounceDone + 1) * 0.6180339887, 1.0);
                    const double seconds  = toFrac * g_duration;
                    int          step;

                    ++bounceDone;
                    (void)printf("bounce %u/%u -> %.1f s (%.0f%%)\n", bounceDone, options.bounce, seconds, toFrac * 100.0);
                    /* Simulate a real drag: sweep scrub previews toward the target over ~360 ms, then
                ** release. This exercises the proxy switch and hysteresis, not just a lone seek. */
                    for (step = 1; step <= 6; ++step)
                    {
                        const double f = fromFrac + (toFrac - fromFrac) * step / 6.0;

                        CastSession_Post(pSession, CAST_COMMAND_SCRUB, f * g_duration);
                        Sleep(60);
                    }
                    CastSession_Post(pSession, CAST_COMMAND_SEEK, seconds); /* release */
                    nextBounce = t + dwellMs;
                }
                else if (bounceDone >= options.bounce && t >= nextBounce)
                {
                    (void)printf("bounce run complete\n");
                    stop = 1;
                }
            }

            if (options.seek > 0 && 0 != g_playingSince && GetTickCount64() >= g_playingSince + 3000)
            {
                (void)printf("seeking to %.1f s\n", options.seek);
                CastSession_Post(pSession, CAST_COMMAND_SEEK, options.seek);
                options.seek = 0;
            }
            if (options.scrub > 0 && 0 != g_playingSince && GetTickCount64() >= g_playingSince + 3000)
            {
                (void)printf("scrubbing to %.1f s (paused preview at the keyframe before it)\n", options.scrub);
                CastSession_Post(pSession, CAST_COMMAND_SCRUB, options.scrub);
                options.seek  = options.scrub; /* the release: an exact, resuming seek two seconds later */
                options.scrub = 0;
                g_playingSince += 2000;
            }
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
    }
    CloseHandle(hThread);
    Proxy_Stop(pProxy);
    CastSession_Destroy(pSession);
    MediaServer_Stop(pServer);
    free(pKeyframes);
    WSACleanup();
    return g_runResult;
}
