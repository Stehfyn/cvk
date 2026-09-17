/* Media Foundation decoder and mapped byte stream from the pinned cdx demo.
** The two-slot decode ring feeds shared NV12 textures; renderer.c performs both
** video passes with cvk. Playback scheduling remains upstream's. */

#include "video.h"

#include <d3d10.h>
#include "renderer.h"

#include "cdx11.h"
#include "cdxgi.h"
#include "cmf.h"



#define VIDEO_RING      VIDEO_RENDERER_FRAMES /* the frame on screen and the next one, decoded one ahead */
#define VIDEO_LOW_WATER (VIDEO_RING - 1) /* refill as soon as a slot is free */

struct VideoStream
{
    ID3D11Device*         pDevice;
    ID3D11DeviceContext*  pContext;
    IMFDXGIDeviceManager* pManager;
    UINT                 resetToken;
    IMFSourceReader*     pReader;
    VideoInfo            info;
    VideoRenderer*       pRenderer;
    int                  ringHead;
    int                  ringCount;
    int                  current;
    BOOL                 fHaveFrame;
    BOOL                 fConverted;
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
    if (InlineIsEqualGUID(riid, &IID_IUnknown))
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

#define PREFETCH_WINDOW (16u << 20)

typedef struct MappedStream
{
    const IMFByteStreamVtbl* lpVtbl;
    LONG                     refs;
    HANDLE                   hFile;
    HANDLE                   hMapping;
    const BYTE*              pView;
    ULONGLONG                size;
    volatile LONGLONG        position;
    volatile LONGLONG        prefetched; /* end of the range asked from the OS so far */
} MappedStream;

static void MappedStream_Prefetch(MappedStream* s, ULONGLONG from)
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

static ULONG MappedStream_Copy(MappedStream* s, BYTE* pb, ULONG cb)
{
    LONGLONG pos = s->position;
    ULONG    n;
    if (NULL == s->pView || (ULONGLONG)pos >= s->size)
    {
        return 0;
    }
    n = (ULONG)min((ULONGLONG)cb, s->size - (ULONGLONG)pos);
    MappedStream_Prefetch(s, (ULONGLONG)pos);
    CopyMemory(pb, s->pView + pos, n);
    InterlockedExchange64(&s->position, pos + n);
    return n;
}

static HRESULT STDMETHODCALLTYPE MappedStream_QueryInterface(IMFByteStream* This, REFIID riid, void** ppv)
{
    if (InlineIsEqualGUID(riid, &IID_IMFByteStream) || InlineIsEqualGUID(riid, &IID_IUnknown))
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

static ULONG STDMETHODCALLTYPE MappedStream_Release(IMFByteStream* This)
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
    MappedStream* s = (MappedStream*)This;
    if (qwPosition > s->size)
    {
        return E_INVALIDARG;
    }
    InterlockedExchange64(&s->position, (LONGLONG)qwPosition);
    return S_OK;
}

static HRESULT STDMETHODCALLTYPE MappedStream_IsEndOfStream(IMFByteStream* This, BOOL* pfEndOfStream)
{
    MappedStream* s = (MappedStream*)This;
    *pfEndOfStream  = (ULONGLONG)s->position >= s->size;
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
    (void)pcbWritten;
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
    (void)pcbWritten;
    return E_NOTIMPL;
}

static HRESULT STDMETHODCALLTYPE MappedStream_Seek(IMFByteStream* This, MFBYTESTREAM_SEEK_ORIGIN SeekOrigin, LONGLONG llSeekOffset, DWORD dwSeekFlags, QWORD* pqwCurrentPosition)
{
    MappedStream* s    = (MappedStream*)This;
    LONGLONG      base = (msoCurrent == SeekOrigin) ? s->position : 0;
    LONGLONG      pos  = base + llSeekOffset;
    (void)dwSeekFlags;
    if (pos < 0 || (ULONGLONG)pos > s->size)
    {
        return E_INVALIDARG;
    }
    InterlockedExchange64(&s->position, pos);
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

static HRESULT STDMETHODCALLTYPE MappedStream_Close(IMFByteStream* This)
{
    MappedStream* s = (MappedStream*)This;
    if (NULL != s->pView)
    {
        UnmapViewOfFile(s->pView);
        s->pView = NULL;
    }
    if (NULL != s->hMapping)
    {
        CloseHandle(s->hMapping);
        s->hMapping = NULL;
    }
    if (INVALID_HANDLE_VALUE != s->hFile && NULL != s->hFile)
    {
        CloseHandle(s->hFile);
        s->hFile = NULL;
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
static IMFByteStream* MappedStream_Create(LPCWSTR pszFile)
{
    MappedStream* s = (MappedStream*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MappedStream));
    LARGE_INTEGER size;
    if (NULL == s)
    {
        return NULL;
    }
    s->lpVtbl = &g_mappedStreamVtbl;
    s->refs   = 1;
    s->hFile  = CreateFileW(pszFile, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (INVALID_HANDLE_VALUE == s->hFile || !GetFileSizeEx(s->hFile, &size) || 0 == size.QuadPart)
    {
        MappedStream_Release((IMFByteStream*)s);
        return NULL;
    }
    s->size     = (ULONGLONG)size.QuadPart;
    s->hMapping = CreateFileMappingW(s->hFile, NULL, PAGE_READONLY, 0, 0, NULL);
    s->pView    = (NULL != s->hMapping) ? (const BYTE*)MapViewOfFile(s->hMapping, FILE_MAP_READ, 0, 0, 0) : NULL;
    if (NULL == s->pView)
    {
        MappedStream_Release((IMFByteStream*)s);
        return NULL;
    }
    MappedStream_Prefetch(s, 0);
    return (IMFByteStream*)s;
}

/* --------------------------------------------------------------------------------- the passes */

static void CreateFrameTextures(VideoStream* pStream)
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
static void ProtectDevice(ID3D11Device* pDevice)
{
    ID3D10Multithread* pMultithread;
    if (SUCCEEDED(cdxgiUnknownQueryInterface((IUnknown*)pDevice, &IID_ID3D10Multithread, (void**)&pMultithread)))
    {
        pMultithread->lpVtbl->SetMultithreadProtected(pMultithread, TRUE);
        RELEASE(pMultithread);
    }
}

static void ReadInfo(VideoStream* pStream)
{
    IMFMediaType* pType;
    cmfSourceReaderGetCurrentVideoMediaType(pStream->pReader, &pType);
    cmfMediaTypeGetFrameSize(pType, &pStream->info.Width, &pStream->info.Height);
    cmfMediaTypeGetFrameRate(pType, &pStream->info.FrameRateNumerator, &pStream->info.FrameRateDenominator);
    RELEASE(pType);
    pStream->info.Duration100ns = cmfSourceReaderGetDuration(pStream->pReader);
}

VideoStream* VideoStream_Open(ID3D11Device* pDevice, ID3D11DeviceContext* pContext, LPCWSTR pszFile)
{
    VideoStream*   pStream;
    IMFAttributes* pAttributes;
    IMFByteStream* pByteStream;
    HRESULT        hr;

    if (INVALID_FILE_ATTRIBUTES == GetFileAttributesW(pszFile))
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

    /* the device manager and the source reader: the inbox decoder (no vendor hardware MFT, which
    ** would decode on threads of its own against this device), DXVA through the manager */
    cmfSetupDeviceManager((IUnknown*)pDevice, &pStream->resetToken, &pStream->pManager);
    cmfCreateSourceReaderAttributes((IUnknown*)pStream->pManager, &pAttributes);
    cmfAttributesSetUINT32(pAttributes, &MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
    pByteStream = MappedStream_Create(pszFile);
    if (NULL != pByteStream)
    {
        cmfCreateSourceReaderFromByteStream(pByteStream, pAttributes, &pStream->pReader);
        RELEASE(pByteStream);
    }
    else
    {
        cmfCreateSourceReaderFromURL(pszFile, pAttributes, &pStream->pReader);
    }
    RELEASE(pAttributes);
    hr = cmfSourceReaderSelectVideoStreamNV12(pStream->pReader);
    if (FAILED(hr))
    {
        OutputDebugStringA("video: the first video stream cannot deliver NV12\n");
        VideoStream_Close(pStream);
        return NULL;
    }
    ReadInfo(pStream);
    pStream->pRenderer = VideoRenderer_Create(pDevice, pContext);
    CreateFrameTextures(pStream);
    FillRing(pStream, 1000);
    return pStream;
}

void VideoStream_GetInfo(const VideoStream* pStream, VideoInfo* pInfo)
{
    *pInfo = pStream->info;
}

/* Copies a decoded sample into a ring slot: from the decoder's texture array when the sample
** carries a DXGI buffer, otherwise by uploading the 2-D system-memory buffer. */
static BOOL UploadSample(VideoStream* pStream, IMFSample* pSample, int slot)
{
    IMFDXGIBuffer* pDXGIBuffer;
    IMF2DBuffer2*  p2DBuffer;
    BOOL           fCopied = FALSE;

    if (SUCCEEDED(cmfSampleGetBufferAsDXGIBuffer(pSample, &pDXGIBuffer)))
    {
        ID3D11Texture2D* pTexture;
        UINT             subresource;
        cmfDXGIBufferGetResource(pDXGIBuffer, &IID_ID3D11Texture2D, (void**)&pTexture);
        cmfDXGIBufferGetSubresourceIndex(pDXGIBuffer, &subresource);
        VideoRenderer_BeginUpload(pStream->pRenderer, slot);
        cdx11DeviceContextCopySubresourceRegion(pStream->pContext, (ID3D11Resource*)VideoRenderer_GetFrame(pStream->pRenderer, slot), 0, 0, 0, 0, (ID3D11Resource*)pTexture, subresource, NULL);
        VideoRenderer_EndUpload(pStream->pRenderer, slot);
        RELEASE(pTexture);
        RELEASE(pDXGIBuffer);
        fCopied = TRUE;
    }
    else if (SUCCEEDED(cmfSampleGetBufferAs2DBuffer2(pSample, &p2DBuffer)))
    {
        BYTE* pScanline0;
        LONG  pitch;
        cmf2DBufferLock2D((IMF2DBuffer*)p2DBuffer, &pScanline0, &pitch);
        if (pitch > 0)
        {
            VideoRenderer_BeginUpload(pStream->pRenderer, slot);
            cdx11DeviceContextUpdateSubresource(pStream->pContext, (ID3D11Resource*)VideoRenderer_GetFrame(pStream->pRenderer, slot), 0, NULL, pScanline0, (UINT)pitch, 0);
            VideoRenderer_EndUpload(pStream->pRenderer, slot);
            fCopied = TRUE;
        }
        cmf2DBufferUnlock2D((IMF2DBuffer*)p2DBuffer);
        RELEASE(p2DBuffer);
    }
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

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&start);
    while (pStream->ringCount < VIDEO_RING)
    {
        QueryPerformanceCounter(&now);
        if (decoded > 0 && (ULONGLONG)(now.QuadPart - start.QuadPart) * 1000 >= (ULONGLONG)budgetMs * frequency.QuadPart)
        {
            return;
        }
        ++decoded;
        IMFSample* pSample = NULL;
        DWORD      flags   = 0;
        LONGLONG   timestamp;
        int        slot;

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
void VideoStream_Prefetch(VideoStream* pStream, DWORD budgetMs)
{
    if (pStream->ringCount <= VIDEO_LOW_WATER)
    {
        FillRing(pStream, budgetMs);
    }
}

/* Takes the next decoded frame from the ring; decodes only when the ring is empty. */
BOOL VideoStream_NextFrame(VideoStream* pStream)
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

void VideoStream_Close(VideoStream* pStream)
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
