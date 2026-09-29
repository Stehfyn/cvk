/* Media Foundation decoder and mapped byte stream from the pinned cdx demo.
** The two-slot decode ring feeds GPU copies into shared planes; renderer.c performs both
** video passes with cvk. Playback scheduling remains upstream's. */

#include "video.h"
#include "os/os_compiler.h"
#include "cdx11.h"
#include "cdxgi.h"
#include "cmf.h"
#include "os/win32/os_text.h"
#include <stdint.h>
#ifdef CVK_DEMO_VALIDATION
#include <stdio.h>
#endif

#include <d3d10.h>
#include <d3d11.h>
#include <mfobjects.h>
#include <mfreadwrite.h>
#include <guiddef.h>
#include <Unknwnbase.h>
#include <minwindef.h>
#include <minwinbase.h>
#include <winnt.h>
#include <winerror.h>
#include <WinBase.h>
#include <basetsd.h>
#include <memoryapi.h>
#include <processthreadsapi.h>
#include <heapapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <debugapi.h>
#include <profileapi.h>
#include "renderer.h"


#define VIDEO_MILLISECONDS_PER_SECOND (1000)
#define VIDEO_RING                    VIDEO_RENDERER_FRAMES /* the frame on screen and the next one, decoded one ahead */
#define VIDEO_LOW_WATER               (VIDEO_RING - 1)      /* refill as soon as a slot is free */

struct VideoStream
{
    ID3D11Device*         pDevice;
    ID3D11DeviceContext*  pContext;
    IMFDXGIDeviceManager* pManager;
    UINT                  resetToken;
    IMFSourceReader*      pReader;
    VideoInfo             info;
    VideoRenderer*        pRenderer;
    int                   ringHead;
    int                   ringCount;
    int                   current;
    BOOL                  fHaveFrame;
    BOOL                  fConverted;
#ifdef CVK_DEMO_VALIDATION
    int decodePath;
#endif
};

#define RELEASE(p)                                                                                                                                                                                     \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        if (NULL != (p))                                                                                                                                                                               \
        {                                                                                                                                                                                              \
            cdxgiUnknownRelease((IUnknown*)(p));                                                                                                                                                       \
            (p) = NULL;                                                                                                                                                                                \
        }                                                                                                                                                                                              \
    } while (0)

static void FillRing(VideoStream* pStream, DWORD budgetMs);

/* ----------------------------------------------------------------- the memory-mapped byte stream */

/* The file as an IMFByteStream over a read-only mapping of the whole file: no read calls, no
** copies into a read buffer, the pages come from the OS cache and are prefetched ahead of the
** parser, so the reader never waits on I/O. BeginRead completes synchronously through an
** IMFAsyncResult, which is what the MP4 source uses. */
typedef struct ReadResult
{
    const IUnknownVtbl* lpVtbl;
    LONG                refs;
    ULONG               count;
} ReadResult;

static HRESULT STDMETHODCALLTYPE ReadResult_QueryInterface(IUnknown* This, REFIID riid, void** ppv)
{
    if (IsEqualGUID(riid, &IID_IUnknown))
    {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE ReadResult_AddRef(IUnknown* This)
{
    return (ULONG)InterlockedIncrement(&((ReadResult*)This)->refs);
}

static ULONG STDMETHODCALLTYPE ReadResult_Release(IUnknown* This)
{
    ULONG refs = (ULONG)InterlockedDecrement(&((ReadResult*)This)->refs);
    if (0 == refs)
    {
        HeapFree(GetProcessHeap(), 0, This);
    }
    return refs;
}

static const IUnknownVtbl g_readResultVtbl = {ReadResult_QueryInterface, ReadResult_AddRef, ReadResult_Release};

#define PREFETCH_WINDOW (16 << 20)

typedef struct MappedStream
{
    const IMFByteStreamVtbl* lpVtbl;
    LONG                     refs;
    HANDLE                   hFile;
    HANDLE                   hMapping;
    BYTE*                    pView;
    ULONGLONG                size;
    volatile LONGLONG        position;
    volatile LONGLONG        prefetched; /* end of the range asked from the OS so far */
} MappedStream;

static FORCE_INLINE void MappedStream_Prefetch(MappedStream* s, ULONGLONG from)
{
    WIN32_MEMORY_RANGE_ENTRY range;
    ULONGLONG                end = from + PREFETCH_WINDOW;
    if (end > s->size)
    {
        end = s->size;
    }
    if ((LONGLONG)from + (LONGLONG)(PREFETCH_WINDOW / 2) < s->prefetched || from >= end)
    {
        return;
    }
    range.VirtualAddress = (PVOID)(s->pView + from);
    range.NumberOfBytes  = (SIZE_T)(end - from);
    PrefetchVirtualMemory(GetCurrentProcess(), 1, &range, 0);
    InterlockedExchange64(&s->prefetched, (LONGLONG)end);
}

static FORCE_INLINE ULONG MappedStream_Copy(MappedStream* s, BYTE* pb, ULONG cb)
{
    BYTE     scratch = 0;
    BYTE*    view;
    LONGLONG pos;
    ULONGLONG offset;
    ULONGLONG remaining;
    ULONG    count;
    BOOL     has_view;
    BOOL     in_range;
    BOOL     can_copy;

    pos = s->position;
    has_view = NULL != s->pView;
    in_range = (ULONGLONG)pos < s->size;
    can_copy = has_view && in_range;
    offset = (ULONGLONG)pos & (0ULL - (ULONGLONG)can_copy);
    remaining = s->size - offset;
    count     = remaining < cb ? (ULONG)remaining : cb;
    count *= (ULONG)can_copy;
    view = has_view ? s->pView : &scratch;
    CopyMemory(pb, view + offset, count);
    if (can_copy)
    {
        MappedStream_Prefetch(s, offset);
        InterlockedExchange64(&s->position, pos + count);
    }
    return count;
}

static HRESULT STDMETHODCALLTYPE MappedStream_QueryInterface(IMFByteStream* This, REFIID riid, void** ppv)
{
    if (IsEqualGUID(riid, &IID_IMFByteStream) || IsEqualGUID(riid, &IID_IUnknown))
    {
        *ppv = This;
        This->lpVtbl->AddRef(This);
        return S_OK;
    }
    *ppv = NULL;
    return E_NOINTERFACE;
}

static ULONG STDMETHODCALLTYPE MappedStream_AddRef(IMFByteStream* This)
{
    return (ULONG)InterlockedIncrement(&((MappedStream*)This)->refs);
}

static HRESULT STDMETHODCALLTYPE MappedStream_Close(IMFByteStream* This);

static FORCE_INLINE ULONG STDMETHODCALLTYPE MappedStream_Release(IMFByteStream* This)
{
    ULONG refs = (ULONG)InterlockedDecrement(&((MappedStream*)This)->refs);
    if (0 == refs)
    {
        MappedStream_Close(This);
        HeapFree(GetProcessHeap(), 0, This);
    }
    return refs;
}

static HRESULT STDMETHODCALLTYPE MappedStream_GetCapabilities(IMFByteStream* This, DWORD* pdwCapabilities)
{
    (void)This;
    *pdwCapabilities = MFBYTESTREAM_IS_READABLE | MFBYTESTREAM_IS_SEEKABLE;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MappedStream_GetLength(IMFByteStream* This, QWORD* pqwLength)
{
    *pqwLength = ((MappedStream*)This)->size;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MappedStream_SetLength(IMFByteStream* This, QWORD qwLength)
{
    (void)This;
    (void)qwLength;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE MappedStream_GetCurrentPosition(IMFByteStream* This, QWORD* pqwPosition)
{
    *pqwPosition = (QWORD)((MappedStream*)This)->position;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MappedStream_SetCurrentPosition(IMFByteStream* This, QWORD qwPosition)
{
    MappedStream* stream = (MappedStream*)This;
    if (qwPosition > stream->size)
    {
        return E_INVALIDARG;
    }
    InterlockedExchange64(&stream->position, (LONGLONG)qwPosition);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MappedStream_IsEndOfStream(IMFByteStream* This, BOOL* pfEndOfStream)
{
    MappedStream* stream = (MappedStream*)This;
    *pfEndOfStream       = (ULONGLONG)stream->position >= stream->size;
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MappedStream_Read(IMFByteStream* This, BYTE* pb, ULONG cb, ULONG* pcbRead)
{
    *pcbRead = MappedStream_Copy((MappedStream*)This, pb, cb);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MappedStream_BeginRead(IMFByteStream* This, BYTE* pb, ULONG cb, IMFAsyncCallback* pCallback, IUnknown* punkState)
{
    ReadResult*     pRead = (ReadResult*)HeapAlloc(GetProcessHeap(), 0, sizeof(ReadResult));
    IMFAsyncResult* pResult;
    if (NULL == pRead)
    {
        return E_OUTOFMEMORY;
    }
    pRead->lpVtbl = &g_readResultVtbl;
    pRead->refs   = 1;
    pRead->count  = MappedStream_Copy((MappedStream*)This, pb, cb);
    cmfCreateAsyncResult((IUnknown*)pRead, pCallback, punkState, &pResult);
    cmfAsyncResultSetStatus(pResult, S_OK);
    cmfInvokeCallback(pResult);
    cmfUnknownRelease((IUnknown*)pResult);
    cmfUnknownRelease((IUnknown*)pRead);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MappedStream_EndRead(IMFByteStream* This, IMFAsyncResult* pResult, ULONG* pcbRead)
{
    IUnknown* pObject;
    (void)This;
    cmfAsyncResultGetObject(pResult, &pObject);
    *pcbRead = ((ReadResult*)pObject)->count;
    cmfUnknownRelease(pObject);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MappedStream_Write(IMFByteStream* This, const BYTE* pb, ULONG cb, ULONG* pcbWritten)
{
    (void)This;
    (void)pb;
    (void)cb;
    if (NULL != pcbWritten)
    {
        *pcbWritten = 0;
    }
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE MappedStream_BeginWrite(IMFByteStream* This, const BYTE* pb, ULONG cb, IMFAsyncCallback* pCallback, IUnknown* punkState)
{
    (void)This;
    (void)pb;
    (void)cb;
    (void)pCallback;
    (void)punkState;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE MappedStream_EndWrite(IMFByteStream* This, IMFAsyncResult* pResult, ULONG* pcbWritten)
{
    (void)This;
    (void)pResult;
    if (NULL != pcbWritten)
    {
        *pcbWritten = 0;
    }
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE MappedStream_Seek(IMFByteStream* This, MFBYTESTREAM_SEEK_ORIGIN SeekOrigin, LONGLONG llSeekOffset, DWORD dwSeekFlags, QWORD* pqwCurrentPosition)
{
    MappedStream* stream = (MappedStream*)This;
    LONGLONG      base   = (msoCurrent == SeekOrigin) ? stream->position : 0;
    LONGLONG      pos    = base + llSeekOffset;
    (void)dwSeekFlags;
    if (pos < 0 || (ULONGLONG)pos > stream->size)
    {
        return E_INVALIDARG;
    }
    InterlockedExchange64(&stream->position, pos);
    if (NULL != pqwCurrentPosition)
    {
        *pqwCurrentPosition = (QWORD)pos;
    }
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MappedStream_Flush(IMFByteStream* This)
{
    (void)This;
    return S_OK;
}

static FORCE_INLINE HRESULT STDMETHODCALLTYPE MappedStream_Close(IMFByteStream* This)
{
    MappedStream* stream = (MappedStream*)This;
    if (NULL != stream->pView)
    {
        UnmapViewOfFile(stream->pView);
        stream->pView = NULL;
    }
    if (NULL != stream->hMapping)
    {
        CloseHandle(stream->hMapping);
        stream->hMapping = NULL;
    }
    if (INVALID_HANDLE_VALUE != stream->hFile && NULL != stream->hFile)
    {
        CloseHandle(stream->hFile);
        stream->hFile = NULL;
    }
    return S_OK;
}

static const IMFByteStreamVtbl g_mappedStreamVtbl = {MappedStream_QueryInterface,
                                                     MappedStream_AddRef,
                                                     MappedStream_Release,
                                                     MappedStream_GetCapabilities,
                                                     MappedStream_GetLength,
                                                     MappedStream_SetLength,
                                                     MappedStream_GetCurrentPosition,
                                                     MappedStream_SetCurrentPosition,
                                                     MappedStream_IsEndOfStream,
                                                     MappedStream_Read,
                                                     MappedStream_BeginRead,
                                                     MappedStream_EndRead,
                                                     MappedStream_Write,
                                                     MappedStream_BeginWrite,
                                                     MappedStream_EndWrite,
                                                     MappedStream_Seek,
                                                     MappedStream_Flush,
                                                     MappedStream_Close};

/* Maps a file read-only and returns it as an IMFByteStream; NULL when it cannot be opened. */
static IMFByteStream* MappedStream_Create(LPCTSTR pszFile)
{
    MappedStream* stream = (MappedStream*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MappedStream));
    LARGE_INTEGER size;
    if (NULL == stream)
    {
        return NULL;
    }
    stream->lpVtbl = &g_mappedStreamVtbl;
    stream->refs   = 1;
    stream->hFile  = CreateFile(pszFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (INVALID_HANDLE_VALUE == stream->hFile || !GetFileSizeEx(stream->hFile, &size) || 0 == size.QuadPart)
    {
        MappedStream_Release((IMFByteStream*)stream);
        return NULL;
    }
    stream->size     = (ULONGLONG)size.QuadPart;
    stream->hMapping = CreateFileMapping(stream->hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    stream->pView    = (NULL != stream->hMapping) ? (BYTE*)MapViewOfFile(stream->hMapping, FILE_MAP_READ, 0, 0, 0) : NULL;
    if (NULL == stream->pView)
    {
        MappedStream_Release((IMFByteStream*)stream);
        return NULL;
    }
    MappedStream_Prefetch(stream, 0);
    return (IMFByteStream*)stream;
}

/* --------------------------------------------------------------------------------- the passes */

static FORCE_INLINE void CreateFrameTextures(VideoStream* pStream)
{
    VideoRenderer_SetSize(pStream->pRenderer, pStream->info.Width, pStream->info.Height);
    pStream->ringHead   = 0;
    pStream->ringCount  = 0;
    pStream->current    = -1;
    pStream->fHaveFrame = FALSE;
    pStream->fConverted = FALSE;
}

/* The DXGI device manager requires the device's multithread protection (ffmpeg's d3d11va
** context set the same on this device). ID3D10Multithread is the one interface of the path no
** library wraps; its vtable is called. */
static FORCE_INLINE void ProtectDevice(ID3D11Device* pDevice)
{
    ID3D10Multithread* pMultithread;
    if (SUCCEEDED(cdxgiUnknownQueryInterface((IUnknown*)pDevice, &IID_ID3D10Multithread, (void**)&pMultithread)))
    {
        pMultithread->lpVtbl->SetMultithreadProtected(pMultithread, TRUE);
        RELEASE(pMultithread);
    }
}

static FORCE_INLINE void ReadInfo(VideoStream* pStream)
{
    IMFMediaType* pType;
    cmfSourceReaderGetCurrentVideoMediaType(pStream->pReader, &pType);
    cmfMediaTypeGetFrameSize(pType, &pStream->info.Width, &pStream->info.Height);
    cmfMediaTypeGetFrameRate(pType, &pStream->info.FrameRateNumerator, &pStream->info.FrameRateDenominator);
    RELEASE(pType);
    pStream->info.Duration100ns = cmfSourceReaderGetDuration(pStream->pReader);
}

VideoStream* VideoStream_Open(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, LPCTSTR pszFile)
{
    VideoStream*   pStream;
    IMFAttributes* pAttributes;
    IMFByteStream* pByteStream;
    HRESULT        result;
    uint16_t       unicode_file[MAX_PATH];

    if (INVALID_FILE_ATTRIBUTES == GetFileAttributes(pszFile))
    {
        return NULL;
    }
    if (!cmfStartupDefault())
    {
        return NULL;
    }
    pStream = (VideoStream*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*pStream));
    if (NULL == pStream)
    {
        return NULL;
    }
    pStream->pDevice  = pDevice;
    pStream->pContext = pContext;
    ProtectDevice(pDevice);

    /* The reader attributes enable hardware transforms and DXVA through this
    ** multithread-protected D3D11 device. Keep the decoded NV12 samples on it. */
    cmfSetupDeviceManager((IUnknown*)pDevice, &pStream->resetToken, &pStream->pManager);
    cmfCreateSourceReaderAttributes((IUnknown*)pStream->pManager, &pAttributes);
    pByteStream = MappedStream_Create(pszFile);
    if (NULL != pByteStream)
    {
        cmfCreateSourceReaderFromByteStream(pByteStream, pAttributes, &pStream->pReader);
        RELEASE(pByteStream);
    }
    else
    {
        /* Media Foundation's URL interface is UTF-16 in both Win32 text modes. */
        if (!OS_Text_ReadNative(unicode_file, MAX_PATH, pszFile))
        {
            RELEASE(pAttributes);
            VideoStream_Close(pStream);
            return NULL;
        }
        cmfCreateSourceReaderFromURL((LPCWSTR)unicode_file, pAttributes, &pStream->pReader);
    }
    RELEASE(pAttributes);
    result = cmfSourceReaderSelectVideoStreamNV12(pStream->pReader);
    if (FAILED(result))
    {
        OutputDebugString(TEXT("video: the first video stream cannot deliver NV12\n"));
        VideoStream_Close(pStream);
        return NULL;
    }
    ReadInfo(pStream);
    pStream->pRenderer = VideoRenderer_Create(pDevice, pContext);
    CreateFrameTextures(pStream);
    FillRing(pStream, VIDEO_MILLISECONDS_PER_SECOND);
    return pStream;
}

extern FORCE_INLINE void VideoStream_GetInfo(const VideoStream* pStream, VideoInfo* pInfo)
{
    *pInfo = pStream->info;
}

/* Copies a decoded sample into a ring slot: from the decoder's texture array when the sample
** carries a DXGI buffer, otherwise by uploading the 2-D system-memory buffer. */
static BOOL UploadSample(VideoStream* pStream, IMFSample* pSample, int slot)
{
    IMFDXGIBuffer* pDXGIBuffer = NULL;
    IMF2DBuffer2*  p2DBuffer = NULL;
    HRESULT        dxgiResult;
    HRESULT        buffer2DResult;
    BOOL           fCopied = FALSE;

    dxgiResult = cmfSampleGetBufferAsDXGIBuffer(pSample, &pDXGIBuffer);
    buffer2DResult = cmfSampleGetBufferAs2DBuffer2(pSample, &p2DBuffer);
    if (SUCCEEDED(dxgiResult))
    {
        ID3D11Texture2D* pTexture;
        UINT             subresource;
        cmfDXGIBufferGetResource(pDXGIBuffer, &IID_ID3D11Texture2D, (void**)&pTexture);
        cmfDXGIBufferGetSubresourceIndex(pDXGIBuffer, &subresource);
#ifdef CVK_DEMO_VALIDATION
        if (pStream->decodePath != 1)
        {
            D3D11_TEXTURE2D_DESC desc;
            cdx11Texture2DGetDesc(pTexture, &desc);
            fprintf(stderr,
                    "video: DXGI decoder output %ux%u, format %u, bind flags 0x%X, decoder binding %s\n",
                    desc.Width,
                    desc.Height,
                    desc.Format,
                    desc.BindFlags,
                    (desc.BindFlags & D3D11_BIND_DECODER) ? "yes" : "no");
            pStream->decodePath = 1;
        }
#endif
        VideoRenderer_BeginUpload(pStream->pRenderer, slot);
        cdx11DeviceContextCopySubresourceRegion(pStream->pContext, (ID3D11Resource*)VideoRenderer_GetFrame(pStream->pRenderer, slot), 0, 0, 0, 0, (ID3D11Resource*)pTexture, subresource, NULL);
        VideoRenderer_EndUpload(pStream->pRenderer, slot);
        RELEASE(pTexture);
        RELEASE(pDXGIBuffer);
        fCopied = TRUE;
    }
    else if (SUCCEEDED(buffer2DResult))
    {
        BYTE* pScanline0;
        LONG  pitch;
#ifdef CVK_DEMO_VALIDATION
        if (pStream->decodePath != 2)
        {
            fprintf(stderr, "video: system-memory decoder output; CPU upload fallback\n");
            pStream->decodePath = 2;
        }
#endif
        cmf2DBufferLock2D((IMF2DBuffer*)p2DBuffer, &pScanline0, &pitch);
        if (pitch > 0)
        {
            VideoRenderer_BeginUpload(pStream->pRenderer, slot);
            cdx11DeviceContextUpdateSubresource(pStream->pContext, (ID3D11Resource*)VideoRenderer_GetFrame(pStream->pRenderer, slot), 0, NULL, pScanline0, (UINT)pitch, 0);
            VideoRenderer_EndUpload(pStream->pRenderer, slot);
            fCopied = TRUE;
        }
        cmf2DBufferUnlock2D((IMF2DBuffer*)p2DBuffer);
    }
    RELEASE(p2DBuffer);
    return fCopied;
}

/* Decodes into the ring, on the calling thread, until it is full or the time budget is spent;
** loops at the end of the stream. A budget of zero decodes at least one frame. */
static void FillRing(VideoStream* pStream, DWORD budgetMs)
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER start;
    LARGE_INTEGER now;
    int           decoded = 0;
    IMFSample*    pSample;
    DWORD         flags;
    LONGLONG      timestamp;
    int           slot;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    for (; pStream->ringCount < VIDEO_RING;)
    {
        QueryPerformanceCounter(&now);
        if (decoded > 0 && (ULONGLONG)(now.QuadPart - start.QuadPart) * VIDEO_MILLISECONDS_PER_SECOND >= (ULONGLONG)budgetMs * (ULONGLONG)frequency.QuadPart)
        {
            return;
        }
        ++decoded;
        pSample = NULL;
        flags   = 0;

        if (FAILED(cmfSourceReaderReadVideoSample(pStream->pReader, &flags, &timestamp, &pSample)))
        {
            return;
        }
        if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED)
        {
            ReadInfo(pStream);
            CreateFrameTextures(pStream);
        }
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM)
        {
            cmfSourceReaderRewind(pStream->pReader);
        }
        if (NULL == pSample)
        {
            continue;
        }
        slot = (pStream->ringHead + pStream->ringCount) % VIDEO_RING;
        if (UploadSample(pStream, pSample, slot))
        {
            ++pStream->ringCount;
        }
        RELEASE(pSample);
    }
}

/* Decodes the next frame ahead of the display within the caller's time budget whenever a slot
** is free. */
extern FORCE_INLINE void VideoStream_Prefetch(VideoStream* pStream, DWORD budgetMs)
{
    if (pStream->ringCount <= VIDEO_LOW_WATER)
    {
        FillRing(pStream, budgetMs);
    }
}

/* Takes the next decoded frame from the ring; decodes only when the ring is empty. */
extern FORCE_INLINE BOOL VideoStream_NextFrame(VideoStream* pStream)
{
    if (0 == pStream->ringCount)
    {
        FillRing(pStream, 0);
        if (0 == pStream->ringCount)
        {
            return FALSE;
        }
    }
    pStream->current  = pStream->ringHead;
    pStream->ringHead = (pStream->ringHead + 1) % VIDEO_RING;
    pStream->ringCount -= 1;
    pStream->fHaveFrame = TRUE;
    pStream->fConverted = FALSE;
    return TRUE;
}

void VideoStream_Draw(VideoStream* pStream, ID3D11RenderTargetView* pTarget, UINT width, UINT height)
{
    if (!pStream->fHaveFrame || 0 == width || 0 == height)
    {
        return;
    }
    VideoRenderer_Draw(pStream->pRenderer, pTarget, pStream->current, !pStream->fConverted, width, height);
    pStream->fConverted = TRUE;
}

extern FORCE_INLINE void VideoStream_CopyFrame(VideoStream* stream, ID3D11Texture2D* target, UINT subresource, UINT x, UINT y)
{
    if (stream->fHaveFrame)
    {
        VideoRenderer_CopyFrame(stream->pRenderer, target, subresource, x, y, stream->current);
    }
}

extern FORCE_INLINE void VideoStream_Close(VideoStream* pStream)
{
    if (NULL == pStream)
    {
        return;
    }
    VideoRenderer_Destroy(pStream->pRenderer);
    RELEASE(pStream->pReader);
    RELEASE(pStream->pManager);
    HeapFree(GetProcessHeap(), 0, pStream);
    cmfShutdown();
}
