/* immersive_window.c - the ImmersiveWindow32 control on the cdx libraries. See immersive_window.h.
**
** Structure, in the order of the original BorderlessWindow32/ImmersiveWindow.c:
**   1. private state and the per-window record
**   2. device and composition setup (OnInitD2D) and teardown
**   3. caption geometry and composition layout
**   4. dark and light mode through the DWM window attribute
**   5. the message handlers
**   6. retained caption and video surface updates
**   7. the window procedure */

#include "immersive_window.h"
#include "presentation.h"
#include "os/win32/os_text.h"
#include "os/os_compiler.h"
#include "os/os_assert.h"
#include "os/os_types.h"

#include <Windows.h> // IWYU pragma: keep
#include <windowsx.h>
#include <dwmapi.h>
#include <Uxtheme.h>
#include <shellapi.h>
#include <stringapiset.h>
#include <WinNls.h>
#include <stdint.h>
#include <basetsd.h>
#include <windef.h>
#include <Unknwnbase.h>
#include <limits.h>
#include <dcommon.h>
#include <d3d11.h>
#ifndef NDEBUG
#include <d3d11sdklayers.h>
#endif
#include <d3dcommon.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <dxgi1_3.h>
#include <dxgicommon.h>
#include <dxgiformat.h>
#include <debugapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <heapapi.h>
#include <minwindef.h>
#include <minwinbase.h>
#include <processthreadsapi.h>
#include <processenv.h>
#include <profileapi.h>
#include <synchapi.h>
#include <sysinfoapi.h>
#include <WinBase.h>
#include <WinUser.h>
#include <wingdi.h>
#include <winerror.h>
#include <winnt.h>

#include "cdcomp.h"
#include "cd2d.h"
#include "cdw.h"
#include "cdx11.h"
#include "cdxgi.h"

#pragma comment(lib, "dwmapi")
#pragma comment(lib, "uxtheme")
#pragma comment(lib, "gdi32")
#pragma comment(lib, "user32")
#pragma comment(lib, "shell32")

/* ------------------------------------------------------------------------------------------ 1 */

#define RECTWIDTH(rc)    RectExtent((rc).left, (rc).right)
#define RECTHEIGHT(rc)   RectExtent((rc).top, (rc).bottom)

/*************************************************************************************************/
/**
 * \par Details:
 *  Computes a bounded coordinate difference without signed overflow or a C runtime call.
 */
static FORCE_INLINE LONG RectExtent(LONG start, LONG end)
{
    int64_t delta;
    bool_t  is_negative;
    bool_t  exceeds_maximum;

    delta           = (int64_t)end - (int64_t)start;
    is_negative     = delta < 0;
    if (is_negative)
    {
        delta = -delta;
    }
    exceeds_maximum = delta > LONG_MAX;
    if (exceeds_maximum)
    {
        delta = LONG_MAX;
    }
    return (LONG)delta;
}

#define CAPTION_HEIGHT_DIPS       30
#define CAPTION_BUTTON_WIDTH_DIPS 45
#define TRACE_TEXT_CAPACITY          (256)
#define TRACE_UTF8_CAPACITY          (1024)
#define MICROSECONDS_PER_SECOND      (1000000)
#define REFERENCE_DPI                (96)
#define CAPTION_FONT_SIZE            (48.0F)
#define CAPTION_FONT_DIPS            (10)
#define CAPTION_PAD_X_DIPS           (18)
#define CAPTION_PAD_Y_DIPS           (10)
#define CAPTION_TRACK_BUTTON_COUNT   (5)
#define CAPTION_STROKE_SCALE         (1.25F)
#define THEME_TOGGLE_MILLISECONDS    (150)
#define ICON_COLOR_DEPTH_BITS        (32)
#define HALF_SCALE                   (0.5F)
#define IMMERSIVE_STATE_PROPERTY      TEXT("ImmersiveWindowState")
#define LIGHT_BRUSH_ALPHA            (0.95F)
#define DARK_BRUSH_ALPHA             (0.85F)
#define FADED_BRUSH_ALPHA            (0.1F)
#define DIM_COLOR_CHANNEL           (43.0F)
#define RED_COLOR_CHANNEL           (196.0F)
#define BLUE_COLOR_CHANNEL          (28.0F)
#define MAX_COLOR_CHANNEL           (255.0F)

/* the Segoe MDL2 Assets code points the caption uses, by index in glyphs[] */
enum
{
    GLYPH_SETTINGS1,
    GLYPH_SETTINGS2,
    GLYPH_SETTINGS3,
    GLYPH_LIGHTMODE1,
    GLYPH_LIGHTMODE2,
    GLYPH_DARKMODE1,
    GLYPH_DARKMODE2,
    GLYPH_DARKMODE3,
    GLYPH_LIGHTDARK1,
    GLYPH_LIGHTDARK2,
    GLYPH_MINIMIZE,
    GLYPH_MAXIMIZE,
    GLYPH_RESTORE,
    GLYPH_CLOSE,
    GLYPH_FEEDBACK,
    GLYPH_COUNT
};
static const UINT32 g_codePoints[GLYPH_COUNT] = {0xE115, 0xE713, 0xF8B0, 0xE706, 0xED39, 0xE708, 0xEC46, 0xF0CE, 0xE793, 0xF08C, 0xE921, 0xE922, 0xE923, 0xE947, 0xED15};

typedef enum tagNCHIT
{
    NCHIT_CLIENT = 0x0,
    NCHIT_LEFT   = 0x1,
    NCHIT_RIGHT  = 0x2,
    NCHIT_TOP    = 0x4,
    NCHIT_BOTTOM = 0x8,
} NCHIT;

typedef enum tagCAPTIONBUTTON
{
    CB_NONE,
    CB_CAPTION,
    CB_ICONBUTTON,
    CB_LIGHTDARKBUTTON,
    CB_FEEDBACKBUTTON,
    CB_MINBUTTON,
    CB_MAXBUTTON,
    CB_CLOSEBUTTON
} CAPTIONBUTTON;

typedef struct tagIMMERSIVEWINDOW
{
    /* Direct3D 11 and DXGI */
    ID3D11Device*        pD3D11Device;
    ID3D11DeviceContext* pD3D11DeviceContext;
    IDXGIDevice1*        pDXGIDevice1;
    IDXGISwapChain2*     pSwapChain;
    HANDLE              hFrameLatency;
    ID3D11Texture2D*     pVideoTexture;
    ID2D1Bitmap1*        pVideoBitmap;
    ID2D1Bitmap1*        pTargetBitmap;
#ifndef NDEBUG
    ID3D11Debug* pD3D11Debug;
#endif
    /* Direct2D */
    ID2D1Factory2*        pD2D1Factory2;
    ID2D1Device1*         pD2D1Device1;
    ID2D1DeviceContext*   pD2D1DeviceContext;
    ID2D1SolidColorBrush* pLightBrush;
    ID2D1SolidColorBrush* pDarkBrush;
    ID2D1SolidColorBrush* pRedBrush;
    ID2D1SolidColorBrush* pInactiveBrush;
    ID2D1SolidColorBrush* pColorizationBrush;
    ID2D1SolidColorBrush* pFadedColorizationBrush;
    ID2D1StrokeStyle1*    pStrokeStyle;
    ID2D1Effect*          pBlurEffect;
    ID2D1Bitmap1*         pAppIconBitmap;
    /* DirectComposition */
    IDCompositionDevice* pDCompDevice;
    IDCompositionTarget* pDCompTarget;
    IDCompositionVisual* pDCompVisual;
    /* DirectWrite */
    IDWriteFactory1*       pDWriteFactory1;
    IDWriteFontCollection* pFontCollection;
    IDWriteFontFamily*     pFontFamily;
    IDWriteFont*           pFont;
    IDWriteFontFace*       pFontFace;
    IDWriteFontFamily*     pSymbolFamily;
    IDWriteFont*           pSymbolFont;
    IDWriteFontFace*       pSymbolFontFace;
    IDWriteTextFormat*     pTextFormat;
    UINT16                 glyphs[GLYPH_COUNT];
    UINT16                 symbolGlyphs[3];
    /* application hooks and state */
    IMMERSIVEVIDEOPROC pVideoProc;
    ULONGLONG lastToggleTime;
    RESIZEINFO     ri;
    BOOL           fLightMode;
    BOOL           fDarkMode;
    CAPTIONBUTTON  eHotCaptionButton;
    CAPTIONBUTTON  ePressedCaptionButton;
    SIZE szVideo;
    SIZE szClient;
    UINT captionDpi;
    BOOL fCaptionMaximized;
    BOOL fHaveVideo;
    BOOL fFrameDirty;
    BOOL fRendering;
    BOOL fFrameSlotReady;
} IMMERSIVEWINDOW, *LPIMMERSIVEWINDOW;

static BOOL g_fActive;

/* IMMERSIVE_TRACE=<file>: timestamps of the resize path against the compositor's clock */
static HANDLE   g_hTrace;
static LONGLONG g_traceOrigin;

static FORCE_INLINE LONGLONG Now(void)
{
    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);
    return counter.QuadPart;
}

static void Trace(LPCTSTR pWhat, HRESULT hr)
{
    TCHAR         line[TRACE_TEXT_CAPACITY];
    uint16_t      unicode[TRACE_TEXT_CAPACITY];
    char          encoded[TRACE_UTF8_CAPACITY];
    DWORD         written;
    int           encoded_count;
    bool_t        converted;
    LARGE_INTEGER frequency;
    DWM_TIMING_INFO timing;
    LONGLONG      now;
    if (NULL == g_hTrace)
    {
        return;
    }
    QueryPerformanceFrequency(&frequency);
    now = Now();
    timing.cbSize = sizeof(timing);
    DwmGetCompositionTimingInfo(NULL, &timing);
    wsprintf(line, TEXT("%8ld us  %-28s hr=%08lX  vblank=%ld us  compose=%ld us\r\n"), (long)((now - g_traceOrigin) * MICROSECONDS_PER_SECOND / frequency.QuadPart), pWhat, (unsigned long)hr,
              (long)(((LONGLONG)timing.qpcVBlank - now) * MICROSECONDS_PER_SECOND / frequency.QuadPart), (long)(((LONGLONG)timing.qpcCompose - now) * MICROSECONDS_PER_SECOND / frequency.QuadPart));
    converted = OS_Text_ReadNative(unicode, TRACE_TEXT_CAPACITY, line);
    encoded_count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS,
                                       (LPCWCH)unicode, -1,
                                       encoded, sizeof(encoded), NULL, NULL);
    written = (DWORD)encoded_count;
    written -= (DWORD)(0 < encoded_count);
    written *= (DWORD)converted;
    WriteFile(g_hTrace, encoded, written, &written, NULL);
}

static FORCE_INLINE LPIMMERSIVEWINDOW GetState(HWND hWnd)
{
    return (LPIMMERSIVEWINDOW)GetProp(hWnd, IMMERSIVE_STATE_PROPERTY);
}

static FORCE_INLINE void Check(BOOL condition, LPCTSTR pWhat)
{
    if (!condition)
    {
        OutputDebugString(TEXT("ImmersiveWindow: "));
        OutputDebugString(pWhat);
        OutputDebugString(TEXT(" failed\n"));
        ExitProcess(1);
    }
}

/* MyRoundf of the original: the larger of the truncation and the rounded value */
static FORCE_INLINE int RoundToInt(FLOAT f)
{
    static const FLOAT half = 0.5F;
    int truncated;
    int rounded;

    truncated = (int)f;
    rounded   = (int)(f + half);
    return rounded > truncated ? rounded : truncated;
}

/* ------------------------------------------------------------------------------------------ 2 */

static void LoadAppIcon(HWND hWnd, LPIMMERSIVEWINDOW pw)
{
    TCHAR       szImage[MAX_PATH];
    DWORD       cchImage = MAX_PATH;
    HICON       hIcon;
    ICONINFO    iconInfo;
    BITMAP      bitmap;
    HDC         hScreen;
    HDC         hMem;
    BITMAPINFO  bitmapInfo;
    void*       pBits;
    HBITMAP     hDib;
    HGDIOBJ     hOld;
    D2D1_SIZE_U size;

    hIcon = NULL;
    if (QueryFullProcessImageName(GetCurrentProcess(), 0, szImage, &cchImage))
    {
        hIcon = ExtractIcon(NULL, szImage, 0);
    }
    if (NULL == hIcon || (HICON)1 == hIcon)
    {
        hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }
    SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

    /* draw the icon into a top-down 32-bit DIB and wrap it as a Direct2D bitmap */
    ZeroMemory(&iconInfo, sizeof(iconInfo));
    GetIconInfo(hIcon, &iconInfo);
    GetObject(iconInfo.hbmColor, sizeof(bitmap), &bitmap);
    hScreen = GetDC(NULL);
    hMem    = CreateCompatibleDC(hScreen);
    ReleaseDC(NULL, hScreen);
    ZeroMemory(&bitmapInfo, sizeof(bitmapInfo));
    bitmapInfo.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth       = bitmap.bmWidth;
    bitmapInfo.bmiHeader.biHeight      = -bitmap.bmHeight;
    bitmapInfo.bmiHeader.biPlanes      = 1;
    bitmapInfo.bmiHeader.biBitCount    = ICON_COLOR_DEPTH_BITS;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    pBits                      = NULL;
    hDib                       = CreateDIBSection(hMem, &bitmapInfo, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (NULL != hDib)
    {
        hOld = SelectObject(hMem, hDib);
        DrawIconEx(hMem, 0, 0, hIcon, bitmap.bmWidth, bitmap.bmHeight, 0, NULL, DI_NORMAL);
        GdiFlush();
        cd2dInitSizeU(&size, (UINT32)bitmap.bmWidth, (UINT32)bitmap.bmHeight);
        cd2dDeviceContextCreateBitmapFromMemory(pw->pD2D1DeviceContext, size, pBits, (UINT32)bitmap.bmWidth * 4, DXGI_FORMAT_B8G8R8A8_UNORM, &pw->pAppIconBitmap);
        SelectObject(hMem, hOld);
        DeleteObject(hDib);
    }
    DeleteDC(hMem);
    DeleteObject(iconInfo.hbmColor);
    DeleteObject(iconInfo.hbmMask);
}

static FORCE_INLINE void SetupFonts(LPIMMERSIVEWINDOW pw)
{
    static const UINT32 symbolCodePoints[3] = {0xE28C, 0x263C, 0xE706};
    IDWriteFactory*     factory;

    cdwCreateFactoryAsFactory1(DWRITE_FACTORY_TYPE_SHARED, &pw->pDWriteFactory1);
    factory = (IDWriteFactory*)pw->pDWriteFactory1;
    cdwFactoryGetSystemFontCollectionCached(factory, &pw->pFontCollection);

    Check(cdwFontCollectionGetFontFamilyByName(pw->pFontCollection, L"Segoe MDL2 Assets", &pw->pFontFamily), TEXT("Segoe MDL2 Assets"));
    cdwFontFamilyGetFirstMatchingFont(pw->pFontFamily, DWRITE_FONT_WEIGHT_THIN, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &pw->pFont);
    cdwFontCreateFontFace(pw->pFont, &pw->pFontFace);
    cdwFontFaceGetGlyphIndices(pw->pFontFace, g_codePoints, GLYPH_COUNT, pw->glyphs);

    Check(cdwFontCollectionGetFontFamilyByName(pw->pFontCollection, L"Segoe UI Symbol", &pw->pSymbolFamily), TEXT("Segoe UI Symbol"));
    cdwFontFamilyGetFirstMatchingFont(pw->pSymbolFamily, DWRITE_FONT_WEIGHT_THIN, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &pw->pSymbolFont);
    cdwFontCreateFontFace(pw->pSymbolFont, &pw->pSymbolFontFace);
    cdwFontFaceGetGlyphIndices(pw->pSymbolFontFace, symbolCodePoints, 3, pw->symbolGlyphs);

    cdwFactoryCreateTextFormat(factory, L"Segoe MDL2 Assets", pw->pFontCollection, DWRITE_FONT_WEIGHT_THIN, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, CAPTION_FONT_SIZE, L"", &pw->pTextFormat);
}

static void SetupBrushes(LPIMMERSIVEWINDOW pw)
{
    ID2D1RenderTarget* target   = (ID2D1RenderTarget*)pw->pD2D1DeviceContext;
    const COLORREF     inactive = GetSysColor(COLOR_BTNSHADOW);
    const COLORREF     accent   = GetSysColor(COLOR_MENUHILIGHT);

    cd2dRenderTargetCreateSolidColorBrushRGBA(target, 1.0F, 1.0F, 1.0F, LIGHT_BRUSH_ALPHA, &pw->pLightBrush);
    cd2dRenderTargetCreateSolidColorBrushRGBA(target, DIM_COLOR_CHANNEL / MAX_COLOR_CHANNEL, DIM_COLOR_CHANNEL / MAX_COLOR_CHANNEL, DIM_COLOR_CHANNEL / MAX_COLOR_CHANNEL, DARK_BRUSH_ALPHA, &pw->pDarkBrush);
    cd2dRenderTargetCreateSolidColorBrushRGBA(target, RED_COLOR_CHANNEL / MAX_COLOR_CHANNEL, DIM_COLOR_CHANNEL / MAX_COLOR_CHANNEL, BLUE_COLOR_CHANNEL / MAX_COLOR_CHANNEL, 1.0F, &pw->pRedBrush);
    cd2dRenderTargetCreateSolidColorBrushRGBA(target, GetRValue(inactive) / MAX_COLOR_CHANNEL, GetGValue(inactive) / MAX_COLOR_CHANNEL, GetBValue(inactive) / MAX_COLOR_CHANNEL, 1.0F, &pw->pInactiveBrush);
    cd2dRenderTargetCreateSolidColorBrushRGBA(target, GetRValue(accent) / MAX_COLOR_CHANNEL, GetGValue(accent) / MAX_COLOR_CHANNEL, GetBValue(accent) / MAX_COLOR_CHANNEL, 1.0F, &pw->pColorizationBrush);
    cd2dRenderTargetCreateSolidColorBrushRGBA(target, GetRValue(accent) / MAX_COLOR_CHANNEL, GetGValue(accent) / MAX_COLOR_CHANNEL, GetBValue(accent) / MAX_COLOR_CHANNEL, FADED_BRUSH_ALPHA, &pw->pFadedColorizationBrush);
    cd2dFactory1CreateStrokeStyle((ID2D1Factory1*)pw->pD2D1Factory2,
                                  D2D1_CAP_STYLE_SQUARE,
                                  D2D1_CAP_STYLE_SQUARE,
                                  D2D1_CAP_STYLE_SQUARE,
                                  D2D1_LINE_JOIN_ROUND,
                                  0.0F,
                                  D2D1_DASH_STYLE_SOLID,
                                  0.0F,
                                  D2D1_STROKE_TRANSFORM_TYPE_HAIRLINE,
                                  NULL,
                                  0,
                                  &pw->pStrokeStyle);
    cd2dDeviceContextCreateGaussianBlurEffect(pw->pD2D1DeviceContext, &pw->pBlurEffect);
}

/* One composition swap chain contains the complete video and caption frame.
** The native-resolution video bitmap is updated only when media time advances. */
static LRESULT OnInitD2D(HWND hWnd, LPIMMERSIVEWINDOW pw)
{
    static const UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT
#ifndef NDEBUG
        | D3D11_CREATE_DEVICE_DEBUG
#endif
        ;
    static const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_12_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
    D3D_FEATURE_LEVEL level;
    D2D1_FACTORY_OPTIONS options = {D2D1_DEBUG_LEVEL_NONE};
    IDXGIAdapter* adapter;
    IDXGIFactory2* factory;
    IDXGISwapChain1* swapChain;
    IDXGISwapChain2* swapChain2 = NULL;
    ID3D11Device* d3dDevice = NULL;
    IDXGIDevice1* dxgiDevice = NULL;
    IDCompositionDevice* compositionDevice = NULL;
    IDXGISurface* surface;
    DXGI_SAMPLE_DESC sample = {1, 0};
    RECT client = {0};
    HRESULT interface_result;
    bool_t has_width;
    bool_t has_height;
    BOOL got_client_rect;
    HDC desktop;
    int desktop_width;
    int desktop_height;
    bool_t desktop_available;
    bool_t desktop_released;
    bool_t desktop_size_valid;

    if (NULL != pw->pD3D11Device)
    {
        return 0;
    }
    got_client_rect = GetClientRect(hWnd, &client);
    has_width = (0 < client.right) && got_client_rect;
    has_height = (0 < client.bottom) && got_client_rect;
    pw->szClient.cx = has_width ? client.right : 1;
    pw->szClient.cy = has_height ? client.bottom : 1;
    pw->fFrameDirty = TRUE;
    SetupFonts(pw);
    (void)cdx11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, deviceFlags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                            &d3dDevice, &level, &pw->pD3D11DeviceContext);
    pw->pD3D11Device = d3dDevice;
    Check((NULL != d3dDevice) && (NULL != pw->pD3D11DeviceContext), TEXT("D3D11CreateDevice"));
#ifndef NDEBUG
    cdx11DeviceGetDebug(d3dDevice, &pw->pD3D11Debug);
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    interface_result = cdxgiUnknownQueryInterfaceAsDevice1((IUnknown*)d3dDevice, &dxgiDevice);
    pw->pDXGIDevice1 = dxgiDevice;
    Check(SUCCEEDED(interface_result), TEXT("IDXGIDevice1"));
    cd2dCreateFactoryAsFactory2(D2D1_FACTORY_TYPE_SINGLE_THREADED, &options, &pw->pD2D1Factory2);
    cd2dFactory2CreateDevice(pw->pD2D1Factory2, (IDXGIDevice*)dxgiDevice, &pw->pD2D1Device1);
    cd2dSetupDefaultDeviceContext((ID2D1Device*)pw->pD2D1Device1, &pw->pD2D1DeviceContext);
    SetupBrushes(pw);
    LoadAppIcon(hWnd, pw);

    cdxgiDeviceGetAdapter((IDXGIDevice*)dxgiDevice, &adapter);
    cdxgiObjectGetParentAsFactory2((IDXGIObject*)adapter, &factory);
    desktop = GetDC(NULL);
    desktop_available = (NULL != desktop);
    LOGASSERT(desktop_available);
    desktop_width = GetDeviceCaps(desktop, DESKTOPHORZRES);
    desktop_height = GetDeviceCaps(desktop, DESKTOPVERTRES);
    desktop_released = (0 != ReleaseDC(NULL, desktop));
    Check(desktop_released, TEXT("release primary desktop DC"));
    desktop_size_valid = (0 < desktop_width) && (0 < desktop_height);
    Check(desktop_size_valid, TEXT("primary desktop size"));
    cdxgiFactoryCreateSwapChainForComposition(factory, (IUnknown*)d3dDevice,
        (UINT)desktop_width, (UINT)desktop_height, DXGI_FORMAT_B8G8R8A8_UNORM, FALSE, sample,
        DXGI_USAGE_RENDER_TARGET_OUTPUT, 2, DXGI_SCALING_STRETCH, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
        DXGI_ALPHA_MODE_IGNORE, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT, NULL, &swapChain);
    cdxgiFactoryMakeWindowAssociation((IDXGIFactory*)factory, hWnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    cdxgiSwapChainGetBufferAsSurface((IDXGISwapChain*)swapChain, 0, &surface);
    cd2dDeviceContextCreateTargetBitmapFromSurface(pw->pD2D1DeviceContext, surface, DXGI_FORMAT_B8G8R8A8_UNORM, &pw->pTargetBitmap);
    cdxgiUnknownRelease((IUnknown*)surface);
    cdcompCreateDevice((IDXGIDevice*)dxgiDevice, &compositionDevice);
    pw->pDCompDevice = compositionDevice;
    cdcompDeviceSetupHwndContent(compositionDevice, hWnd, TRUE, (IUnknown*)swapChain, &pw->pDCompTarget, &pw->pDCompVisual);
    interface_result = cdxgiUnknownQueryInterfaceAsSwapChain2((IUnknown*)swapChain, &swapChain2);
    pw->pSwapChain = swapChain2;
    Check(SUCCEEDED(interface_result), TEXT("composition swap chain"));
    cdxgiSwapChainSetMaximumFrameLatencyOne(swapChain2);
    pw->hFrameLatency = cdxgiSwapChainGetFrameLatencyWaitableObject(swapChain2);
    Check(NULL != pw->hFrameLatency, TEXT("composition frame latency handle"));
    cdxgiUnknownRelease((IUnknown*)swapChain);
    cdxgiUnknownRelease((IUnknown*)factory);
    cdxgiUnknownRelease((IUnknown*)adapter);
    return 0;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A null COM interface has no ownership to release.
 */
static FORCE_INLINE void ReleaseUnknown(IUnknown* object)
{
    if (NULL != object)
    {
        cdxgiUnknownRelease(object);
    }
}

static void OnShutdownD2D(LPIMMERSIVEWINDOW pw)
{
    ReleaseUnknown((IUnknown*)pw->pAppIconBitmap);
    pw->pAppIconBitmap = NULL;
    ReleaseUnknown((IUnknown*)pw->pTextFormat);
    pw->pTextFormat = NULL;
    ReleaseUnknown((IUnknown*)pw->pSymbolFontFace);
    pw->pSymbolFontFace = NULL;
    ReleaseUnknown((IUnknown*)pw->pSymbolFont);
    pw->pSymbolFont = NULL;
    ReleaseUnknown((IUnknown*)pw->pSymbolFamily);
    pw->pSymbolFamily = NULL;
    ReleaseUnknown((IUnknown*)pw->pFontFace);
    pw->pFontFace = NULL;
    ReleaseUnknown((IUnknown*)pw->pFont);
    pw->pFont = NULL;
    ReleaseUnknown((IUnknown*)pw->pFontFamily);
    pw->pFontFamily = NULL;
    ReleaseUnknown((IUnknown*)pw->pFontCollection);
    pw->pFontCollection = NULL;
    ReleaseUnknown((IUnknown*)pw->pDWriteFactory1);
    pw->pDWriteFactory1 = NULL;
    ReleaseUnknown((IUnknown*)pw->pBlurEffect);
    pw->pBlurEffect = NULL;
    ReleaseUnknown((IUnknown*)pw->pStrokeStyle);
    pw->pStrokeStyle = NULL;
    ReleaseUnknown((IUnknown*)pw->pFadedColorizationBrush);
    pw->pFadedColorizationBrush = NULL;
    ReleaseUnknown((IUnknown*)pw->pColorizationBrush);
    pw->pColorizationBrush = NULL;
    ReleaseUnknown((IUnknown*)pw->pInactiveBrush);
    pw->pInactiveBrush = NULL;
    ReleaseUnknown((IUnknown*)pw->pRedBrush);
    pw->pRedBrush = NULL;
    ReleaseUnknown((IUnknown*)pw->pDarkBrush);
    pw->pDarkBrush = NULL;
    ReleaseUnknown((IUnknown*)pw->pLightBrush);
    pw->pLightBrush = NULL;
    ReleaseUnknown((IUnknown*)pw->pDCompVisual);
    pw->pDCompVisual = NULL;
    ReleaseUnknown((IUnknown*)pw->pDCompTarget);
    pw->pDCompTarget = NULL;
    ReleaseUnknown((IUnknown*)pw->pDCompDevice);
    pw->pDCompDevice = NULL;
    if (NULL != pw->pD2D1DeviceContext)
    {
        cd2dDeviceContextClearTarget(pw->pD2D1DeviceContext);
    }
    ReleaseUnknown((IUnknown*)pw->pTargetBitmap);
    pw->pTargetBitmap = NULL;
    ReleaseUnknown((IUnknown*)pw->pVideoBitmap);
    pw->pVideoBitmap = NULL;
    ReleaseUnknown((IUnknown*)pw->pVideoTexture);
    pw->pVideoTexture = NULL;
    ReleaseUnknown((IUnknown*)pw->pSwapChain);
    pw->pSwapChain = NULL;
    if (pw->hFrameLatency)
    {
        CloseHandle(pw->hFrameLatency);
        pw->hFrameLatency = NULL;
    }
    ReleaseUnknown((IUnknown*)pw->pD2D1DeviceContext);
    pw->pD2D1DeviceContext = NULL;
    ReleaseUnknown((IUnknown*)pw->pD2D1Device1);
    pw->pD2D1Device1 = NULL;
    ReleaseUnknown((IUnknown*)pw->pD2D1Factory2);
    pw->pD2D1Factory2 = NULL;
#ifndef NDEBUG
    if (NULL != pw->pD3D11Debug)
    {
        cdx11DebugReportLiveDeviceObjects(pw->pD3D11Debug, D3D11_RLDO_DETAIL);
        ReleaseUnknown((IUnknown*)pw->pD3D11Debug);
    pw->pD3D11Debug = NULL;
    }
#endif
    ReleaseUnknown((IUnknown*)pw->pD3D11DeviceContext);
    pw->pD3D11DeviceContext = NULL;
    ReleaseUnknown((IUnknown*)pw->pD3D11Device);
    pw->pD3D11Device = NULL;
    ReleaseUnknown((IUnknown*)pw->pDXGIDevice1);
    pw->pDXGIDevice1 = NULL;
}

/* ------------------------------------------------------------------------------------------ 3 */

static FORCE_INLINE BOOL GetWindowBorders(HWND hWnd, LPSIZE lpsz)
{
    const UINT dpi = GetDpiForWindow(hWnd);
    lpsz->cx       = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    lpsz->cy       = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    return TRUE;
}

static FORCE_INLINE BOOL GetCaptionRect(HWND hWnd, LPRECT lprc)
{
    const UINT dpi     = GetDpiForWindow(hWnd);
    const int  nHeight = MulDiv(CAPTION_HEIGHT_DIPS, (int)dpi, REFERENCE_DPI);
    POINT      origin = {0, 0};

    if (!GetClientRect(hWnd, lprc) || !ClientToScreen(hWnd, &origin))
    {
        return FALSE;
    }
    OffsetRect(lprc, origin.x, origin.y);
    lprc->bottom = lprc->top + nHeight;
    return TRUE;
}

static FORCE_INLINE BOOL GetCaptionButtonRectFast(LPRECT lprc, int nWidth, CAPTIONBUTTON eButton)
{
    int slot;
    if (CB_CLOSEBUTTON == eButton)
    {
        slot = 0;
    }
    else if (CB_MAXBUTTON == eButton)
    {
        slot = 1;
    }
    else if (CB_MINBUTTON == eButton)
    {
        slot = 2;
    }
    else if (CB_LIGHTDARKBUTTON == eButton)
    {
        slot = 3;
    }
    else
    {
        return FALSE;
    }
    lprc->left = (lprc->right - nWidth) + 1;
    lprc->left -= (slot * nWidth) + (slot ? 1 : 0);
    lprc->right -= (slot * nWidth) + (slot ? 1 : 0);
    return TRUE;
}

static FORCE_INLINE BOOL GetCaptionButtonRect(HWND hWnd, LPRECT lprc, CAPTIONBUTTON eButton)
{
    if (!GetCaptionRect(hWnd, lprc))
    {
        return FALSE;
    }
    return GetCaptionButtonRectFast(lprc, MulDiv(CAPTION_BUTTON_WIDTH_DIPS, (int)GetDpiForWindow(hWnd), REFERENCE_DPI), eButton);
}

static void RenderFrame(HWND hWnd, LPIMMERSIVEWINDOW pw, LONG width, LONG height, UINT presentFlags);

static FORCE_INLINE void WaitForFrameSlot(LPIMMERSIVEWINDOW pw)
{
    if (!pw->fFrameSlotReady)
    {
        DWORD result = WaitForSingleObject(pw->hFrameLatency, INFINITE);
        Check(WAIT_OBJECT_0 == result, TEXT("composition frame latency wait"));
        pw->fFrameSlotReady = TRUE;
    }
}

static FORCE_INLINE void InvalidateCaption(HWND hWnd)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    if (NULL != windowState)
    {
        windowState->fFrameDirty = TRUE;
        InvalidateRect(hWnd, NULL, FALSE);
        /* WM_PAINT is normally low priority. Submit this input state now,
        ** including while a native menu or sizing loop owns message dispatch. */
        UpdateWindow(hWnd);
    }
}

/* ------------------------------------------------------------------------------------------ 4 */

static FORCE_INLINE bool_t ApplyAppMode(HWND hWnd, BOOL fDark)
{
    HRESULT result;

    result = DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE,
                                   &fDark, sizeof(fDark));
    return SUCCEEDED(result);
}

/* ------------------------------------------------------------------------------------------ 5 */

static BOOL OnNCCreate(HWND hWnd, LPCREATESTRUCT lpcs)
{
    LPIMMERSIVEWINDOW windowState;
    const enum DWMNCRENDERINGPOLICY ncPolicy = DWMNCRP_DISABLED;
    POINT             cursorPosition;
    MONITORINFO       monitorInfo;
    HRESULT           frame_result;
    BOOL              create_result;
    bool_t            is_frame_ready;
    bool_t            is_window_created;

    EnableNonClientDpiScaling(hWnd);

    windowState = (LPIMMERSIVEWINDOW)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IMMERSIVEWINDOW));
    Check(NULL != windowState, TEXT("HeapAlloc"));
    Check(SetProp(hWnd, IMMERSIVE_STATE_PROPERTY, (HANDLE)windowState), TEXT("SetProp"));
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lpcs->lpCreateParams);
    frame_result = DwmSetWindowAttribute(hWnd, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy));

    /* BCS_CENTERED: center the window on the monitor under the cursor */
    monitorInfo.cbSize = sizeof(monitorInfo);
    if ((BCS_CENTERED & lpcs->style) && GetCursorPos(&cursorPosition) && GetMonitorInfo(MonitorFromPoint(cursorPosition, MONITOR_DEFAULTTONEAREST), &monitorInfo))
    {
        const LONG windowX = monitorInfo.rcWork.left + ((RECTWIDTH(monitorInfo.rcWork) - lpcs->cx) / 2);
        const LONG windowY = monitorInfo.rcWork.top + ((RECTHEIGHT(monitorInfo.rcWork) - lpcs->cy) / 2);
        MoveWindow(hWnd, windowX, windowY, lpcs->cx, lpcs->cy, TRUE);
    }
    create_result = (BOOL)DefWindowProc(hWnd, WM_NCCREATE, 0, (LPARAM)lpcs);
    is_frame_ready = SUCCEEDED(frame_result);
    is_window_created = 0 != create_result;
    return (BOOL)(is_frame_ready & is_window_created);
}

static FORCE_INLINE void SyncFrameChange(HWND hWnd)
{
    SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static FORCE_INLINE BOOL OnCreate(HWND hWnd, LPCREATESTRUCT lpcs)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    HRESULT           theme_result;
    windowState->fDarkMode        = TRUE;
    /* The caption is rendered by PaintCaption. Disabling DWM nonclient
    ** rendering alone leaves the themed nonclient implementation active. */
    theme_result = SetWindowTheme(hWnd, L"", L"");
    SyncFrameChange(hWnd);
    SendMessage(hWnd, WM_INITD2D, 0, 0);
    FORWARD_WM_CREATE(hWnd, lpcs, DefWindowProc);
    Check(SUCCEEDED(theme_result), TEXT("disable native window theme"));
    return TRUE;
}

static FORCE_INLINE void OnDestroy(HWND hWnd)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    OnShutdownD2D(windowState);
    PostQuitMessage(0);
}

static FORCE_INLINE void OnNCDestroy(HWND hWnd)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    (void)RemoveProp(hWnd, IMMERSIVE_STATE_PROPERTY);
    if (NULL != windowState)
    {
        HeapFree(GetProcessHeap(), 0, windowState);
    }
    FORWARD_WM_NCDESTROY(hWnd, DefWindowProc);
}

static FORCE_INLINE void OnActivate(HWND hWnd, UINT state, HWND hwndActDeact, BOOL fMinimized)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    bool_t            mode_applied;

    g_fActive            = (WA_INACTIVE != state);
    mode_applied = TRUE;
    if (!fMinimized)
    {
        mode_applied = ApplyAppMode(hWnd, windowState->fDarkMode);
    }
    FORWARD_WM_ACTIVATE(hWnd, state, hwndActDeact, fMinimized, DefWindowProc);
    Check(mode_applied, TEXT("dark mode caption"));
}

static FORCE_INLINE void OnClose(HWND hWnd)
{
    DestroyWindow(hWnd);
}

static FORCE_INLINE void OnPaint(HWND hWnd)
{
    PAINTSTRUCT paint;
    RECT client;
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    BeginPaint(hWnd, &paint);
    if (NULL != windowState && NULL != windowState->pDCompDevice && !IsIconic(hWnd))
    {
        BOOL valid;
        cdcompDeviceCheckDeviceState(windowState->pDCompDevice, &valid);
        Check(valid, TEXT("DirectComposition device state"));
        GetClientRect(hWnd, &client);
        RenderFrame(hWnd, windowState, client.right, client.bottom, 0);
    }
    EndPaint(hWnd, &paint);
}

static FORCE_INLINE BOOL OnEraseBkgnd(HWND hWnd, HDC hDC)
{
    /* Suppress GDI erasure without consuming the pending composition repaint. */
    (void)hWnd;
    (void)hDC;
    return TRUE;
}

static void OnGetMinMaxInfo(HWND hWnd, LPMINMAXINFO lpmmi)
{
    RECT          rcCaption;
    RECT          rcButton;
    SIZE          border;
    HMONITOR      hMonitor;
    MONITORINFO   monitorInfo;

    GetCaptionRect(hWnd, &rcCaption);
    GetWindowBorders(hWnd, &border);
    GetCaptionButtonRect(hWnd, &rcButton, CB_CLOSEBUTTON);
    lpmmi->ptMinTrackSize.x = (CAPTION_TRACK_BUTTON_COUNT * RECTWIDTH(rcButton)) + (2 * border.cy);
    lpmmi->ptMinTrackSize.y = RECTHEIGHT(rcCaption) + border.cy;

    hMonitor  = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (NULL != hMonitor && GetMonitorInfo(hMonitor, &monitorInfo))
    {
        lpmmi->ptMaxPosition.x  = monitorInfo.rcWork.left - monitorInfo.rcMonitor.left;
        lpmmi->ptMaxPosition.y  = monitorInfo.rcWork.top - monitorInfo.rcMonitor.top;
        lpmmi->ptMaxTrackSize.x = RECTWIDTH(monitorInfo.rcWork);
        lpmmi->ptMaxTrackSize.y = RECTHEIGHT(monitorInfo.rcWork);
        lpmmi->ptMaxSize        = lpmmi->ptMaxTrackSize;
    }
}

static FORCE_INLINE UINT OnNCCalcSize(HWND hWnd, BOOL fCalcValidRects, NCCALCSIZE_PARAMS* lpcsp)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    /* The proposed window rectangle is the complete client rectangle. Produce
    ** its video and caption together before Windows changes the HWND origin. */
    if (fCalcValidRects && NULL != windowState && NULL != windowState->pSwapChain && !IsIconic(hWnd))
    {
        const RECT* proposed = &lpcsp->rgrc[0];
        LONG width = proposed->right - proposed->left;
        LONG height = proposed->bottom - proposed->top;
        if (windowState->szClient.cx != width || windowState->szClient.cy != height)
        {
            RenderFrame(hWnd, windowState, width, height, DXGI_PRESENT_RESTART);
        }
    }
    return 0;
}

static UINT OnNCHitTest(HWND hWnd, int x, int y)
{
    RECT              rcWindow;
    SIZE              border;
    const BOOL        fMaximized = IsMaximized(hWnd);
    POINT             cursor;
    int               result;
    RECT              rcCaption;
    RECT              rcMin;
    RECT              rcMax;
    RECT              rcClose;
    RECT              rcLightDark;

    GetWindowBorders(hWnd, &border);
    cursor.x = x;
    cursor.y = y;
    GetWindowRect(hWnd, &rcWindow);
    if (!PtInRect(&rcWindow, cursor))
    {
        return HTNOWHERE;
    }
    result = NCHIT_CLIENT;
    if (!fMaximized)
    {
        if (x < rcWindow.left + border.cx)
        {
            result |= NCHIT_LEFT;
        }
        if (x >= rcWindow.right - border.cx)
        {
            result |= NCHIT_RIGHT;
        }
        if (y < rcWindow.top + border.cy)
        {
            result |= NCHIT_TOP;
        }
        if (y >= rcWindow.bottom - border.cy)
        {
            result |= NCHIT_BOTTOM;
        }
    }
    switch (result)
    {
    case NCHIT_LEFT:
        return HTLEFT;
    case NCHIT_RIGHT:
        return HTRIGHT;
    case NCHIT_BOTTOM:
        return HTBOTTOM;
    case NCHIT_TOP | NCHIT_LEFT:
        return HTTOPLEFT;
    case NCHIT_TOP | NCHIT_RIGHT:
        return HTTOPRIGHT;
    case NCHIT_BOTTOM | NCHIT_LEFT:
        return HTBOTTOMLEFT;
    case NCHIT_BOTTOM | NCHIT_RIGHT:
        return HTBOTTOMRIGHT;
    case NCHIT_TOP:
        return HTTOP;
    case NCHIT_CLIENT:
        GetCaptionRect(hWnd, &rcCaption);
        GetCaptionButtonRect(hWnd, &rcMin, CB_MINBUTTON);
        GetCaptionButtonRect(hWnd, &rcMax, CB_MAXBUTTON);
        GetCaptionButtonRect(hWnd, &rcClose, CB_CLOSEBUTTON);
        GetCaptionButtonRect(hWnd, &rcLightDark, CB_LIGHTDARKBUTTON);
        if (PtInRect(&rcMax, cursor))
        {
            return HTMAXBUTTON;
        }
        if (PtInRect(&rcMin, cursor))
        {
            return HTMINBUTTON;
        }
        if (PtInRect(&rcClose, cursor))
        {
            return HTCLOSE;
        }
        if (PtInRect(&rcLightDark, cursor))
        {
            return HTMENU;
        }
        if (PtInRect(&rcCaption, cursor))
        {
            return HTCAPTION;
        }
        return HTCLIENT;
    default:
        return HTNOWHERE;
    }
}

static FORCE_INLINE void OnNCPaint(HWND hWnd, HRGN hrgn)
{
    (void)hWnd;
    (void)hrgn;
}

static FORCE_INLINE BOOL OnNCActivate(HWND hWnd, BOOL fActive, HWND hwndActDeact, BOOL fMinimized)
{
    (void)hwndActDeact;
    (void)fMinimized;
    return (BOOL)DefWindowProc(hWnd, WM_NCACTIVATE, fActive, -1);
}

static FORCE_INLINE CAPTIONBUTTON CaptionButtonFromHitTest(UINT codeHitTest)
{
    switch (codeHitTest)
    {
    case HTCLOSE: return CB_CLOSEBUTTON;
    case HTMAXBUTTON: return CB_MAXBUTTON;
    case HTMINBUTTON: return CB_MINBUTTON;
    case HTMENU: return CB_LIGHTDARKBUTTON;
    case HTCAPTION: return CB_CAPTION;
    default: return CB_NONE;
    }
}

static FORCE_INLINE void OnNCMouseMove(HWND hWnd, int x, int y, UINT codeHitTest)
{
    TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE | TME_NONCLIENT, hWnd, 0};
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    CAPTIONBUTTON button = CaptionButtonFromHitTest(codeHitTest);
    if (windowState->eHotCaptionButton != button)
    {
        windowState->eHotCaptionButton = button;
        InvalidateCaption(hWnd);
    }
    TrackMouseEvent(&track);
    FORWARD_WM_NCMOUSEMOVE(hWnd, x, y, codeHitTest, DefWindowProc);
}

static FORCE_INLINE void OnNCLButtonDown(HWND hWnd, BOOL fDoubleClick, int x, int y, UINT codeHitTest)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    windowState->ePressedCaptionButton = CaptionButtonFromHitTest(codeHitTest);
    if (CB_NONE == windowState->ePressedCaptionButton || CB_CAPTION == windowState->ePressedCaptionButton)
    {
        /* DefWindowProc owns native move/size capture. A previous hover must
        ** never prevent the current edge hit from reaching it. */
        FORWARD_WM_NCLBUTTONDOWN(hWnd, fDoubleClick, x, y, codeHitTest, DefWindowProc);
    }
}

static void OnNCLButtonUp(HWND hWnd, int x, int y, UINT codeHitTest)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    ULONGLONG         ullNow;
    bool_t            mode_applied;
    CAPTIONBUTTON pressed = windowState->ePressedCaptionButton;
    windowState->ePressedCaptionButton = CB_NONE;
    if (pressed != CaptionButtonFromHitTest(codeHitTest))
    {
        return;
    }
    if (CB_CLOSEBUTTON == pressed)
    {
        SendMessage(hWnd, WM_CLOSE, 0, 0);
    }
    else if (CB_MAXBUTTON == pressed)
    {
        windowState->eHotCaptionButton = CB_NONE;
        InvalidateCaption(hWnd);
        PostMessage(hWnd, WM_SYSCOMMAND, IsMaximized(hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
    }
    else if (CB_MINBUTTON == pressed)
    {
        windowState->eHotCaptionButton = CB_NONE;
        InvalidateCaption(hWnd);
        PostMessage(hWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
    }
    else if (CB_LIGHTDARKBUTTON == pressed)
    {
        ullNow = GetTickCount64();
        if (ullNow - windowState->lastToggleTime > THEME_TOGGLE_MILLISECONDS)
        {
            windowState->fDarkMode  = !windowState->fDarkMode;
            windowState->fLightMode = !windowState->fDarkMode;
            mode_applied = ApplyAppMode(hWnd, windowState->fDarkMode);
            InvalidateCaption(hWnd);
            windowState->lastToggleTime = ullNow;
            Check(mode_applied, TEXT("dark mode caption"));
        }
    }
    else if (CB_CAPTION == pressed)
    {
        FORWARD_WM_NCLBUTTONUP(hWnd, x, y, HTCAPTION, DefWindowProc);
    }
}

static FORCE_INLINE void OnNCLButtonDblClk(HWND hWnd, BOOL fDoubleClick, int x, int y, UINT codeHitTest)
{
    (void)fDoubleClick;
    (void)x;
    (void)y;
    if (HTCAPTION == codeHitTest)
    {
        PostMessage(hWnd, WM_SYSCOMMAND, IsMaximized(hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
    }
}

static FORCE_INLINE void OnNCRButtonUp(HWND hWnd, int x, int y, UINT codeHitTest)
{
    if (HTCAPTION == codeHitTest)
    {
        POINT cursorPosition;
        HMENU hMenu = GetSystemMenu(hWnd, FALSE);
        UINT  cmd;
        GetCursorPos(&cursorPosition);
        SetMenuDefaultItem(hMenu, (UINT)-1, TRUE);
        cmd = (UINT)TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, cursorPosition.x, cursorPosition.y, hWnd, NULL);
        if (0 != cmd)
        {
            PostMessage(hWnd, WM_SYSCOMMAND, cmd, 0);
        }
    }
    else
    {
        FORWARD_WM_NCRBUTTONUP(hWnd, x, y, codeHitTest, DefWindowProc);
    }
}

static FORCE_INLINE void OnNCMouseLeave(HWND hWnd)
{
    if (GetState(hWnd)->eHotCaptionButton != CB_NONE)
    {
        GetState(hWnd)->eHotCaptionButton = CB_NONE;
        InvalidateCaption(hWnd);
    }
}

static FORCE_INLINE void OnKeyUp(HWND hWnd, UINT vk, BOOL fDown, int cRepeat, UINT flags)
{
    (void)fDown;
    if (VK_ESCAPE == vk && GetForegroundWindow() == hWnd)
    {
        SendMessage(hWnd, WM_CLOSE, 0, 0);
    }
    FORWARD_WM_KEYUP(hWnd, vk, cRepeat, flags, DefWindowProc);
}

static FORCE_INLINE void OnEnterSizeMove(HWND hWnd)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    windowState->ri.fMoving = FALSE;
    windowState->ri.fResizing = FALSE;
}

static FORCE_INLINE void OnExitSizeMove(HWND hWnd)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    RECT client;
    POINT cursor;
    windowState->ri.fMoving = FALSE;
    windowState->ri.fResizing = FALSE;
    windowState->ePressedCaptionButton = CB_NONE;
    windowState->eHotCaptionButton = CB_NONE;
    if (GetCursorPos(&cursor))
    {
        windowState->eHotCaptionButton = CaptionButtonFromHitTest(OnNCHitTest(hWnd, cursor.x, cursor.y));
    }
    if (NULL != windowState->pSwapChain && !IsIconic(hWnd) && GetClientRect(hWnd, &client) && client.right > 0 && client.bottom > 0)
    {
        /* End the modal loop with one complete frame at the committed geometry.
        ** RESTART discards queued presentations from preceding resize sizes.
        ** Retire this present before normal playback resumes; retain the frame
        ** slot token so the next render does not wait for it a second time. */
        RenderFrame(hWnd, windowState, client.right, client.bottom, DXGI_PRESENT_RESTART);
        WaitForFrameSlot(windowState);
        ValidateRect(hWnd, NULL);
    }
}

static FORCE_INLINE void OnMouseMove(HWND hWnd, int x, int y, UINT keyFlags)
{
    if (GetState(hWnd)->eHotCaptionButton != CB_NONE)
    {
        GetState(hWnd)->eHotCaptionButton = CB_NONE;
        InvalidateCaption(hWnd);
    }
    (void)x;
    (void)y;
    (void)keyFlags;
}

static FORCE_INLINE void OnMouseLeave(HWND hWnd)
{
    if (GetState(hWnd)->eHotCaptionButton != CB_NONE)
    {
        GetState(hWnd)->eHotCaptionButton = CB_NONE;
        InvalidateCaption(hWnd);
    }
}

static FORCE_INLINE void OnSysColorChange(HWND hWnd)
{
    FORWARD_WM_SYSCOLORCHANGE(hWnd, DefWindowProc);
}

static FORCE_INLINE BOOL OnSetCursor(HWND hWnd, HWND hwndCursor, UINT codeHitTest, UINT msg)
{
    return FORWARD_WM_SETCURSOR(hWnd, hwndCursor, codeHitTest, msg, DefWindowProc);
}

static FORCE_INLINE void OnParentNotify(HWND hWnd, UINT msg, HWND hwndChild, int idChild)
{
    FORWARD_WM_PARENTNOTIFY(hWnd, msg, hwndChild, idChild, DefWindowProc);
}

static FORCE_INLINE BOOL OnMoving(HWND hWnd, LPARAM lprc)
{
    GetState(hWnd)->ri.fMoving = TRUE;
    return (BOOL)DefWindowProc(hWnd, WM_MOVING, 0, lprc);
}

static FORCE_INLINE BOOL OnSizing(HWND hWnd, UINT nEdge, LPARAM lprc)
{
    GetState(hWnd)->ri.fResizing = TRUE;
    return (BOOL)DefWindowProc(hWnd, WM_SIZING, nEdge, lprc);
}

static FORCE_INLINE void OnWindowPosChanged(HWND hWnd, const WINDOWPOS* lpwpos)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    if (NULL != windowState && NULL != windowState->pSwapChain && !IsIconic(hWnd))
    {
        RECT client;
        GetClientRect(hWnd, &client);
        if (windowState->fFrameDirty || windowState->szClient.cx != client.right || windowState->szClient.cy != client.bottom ||
            windowState->captionDpi != GetDpiForWindow(hWnd) || windowState->fCaptionMaximized != IsMaximized(hWnd))
        {
            RenderFrame(hWnd, windowState, client.right, client.bottom, DXGI_PRESENT_RESTART);
        }
        ValidateRect(hWnd, NULL);
    }
    FORWARD_WM_WINDOWPOSCHANGED(hWnd, lpwpos, DefWindowProc);
}

static FORCE_INLINE void OnDwmNcRenderingChanged(HWND hWnd, BOOL fEnabled)
{
    if (fEnabled)
    {
        const enum DWMNCRENDERINGPOLICY ncPolicy = DWMNCRP_DISABLED;
        Check(SUCCEEDED(DwmSetWindowAttribute(hWnd, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy))), TEXT("disable native frame"));
    }
}

/* ------------------------------------------------------------------------------------------ 6 */

static FORCE_INLINE void DrawGlyph(LPIMMERSIVEWINDOW pw, IDWriteFontFace* pFace, UINT16 glyph, FLOAT emSize, FLOAT x, FLOAT y, ID2D1Brush* pBrush, DWRITE_MEASURING_MODE mode)
{
    DWRITE_GLYPH_OFFSET offset = {0.0F, 0.0F};
    D2D1_POINT_2F       origin;
    cd2dInitPoint2F(&origin, x, y);
    cd2dDeviceContextDrawGlyphRun(pw->pD2D1DeviceContext, origin, pFace, emSize, 1, &glyph, NULL, &offset, FALSE, 0, NULL, pBrush, mode);
}

static FORCE_INLINE void FillButton(LPIMMERSIVEWINDOW pw, const RECT* prc, ID2D1SolidColorBrush* pBrush, FLOAT dx)
{
    ID2D1RenderTarget* target = (ID2D1RenderTarget*)pw->pD2D1DeviceContext;
    cd2dRenderTargetDrawRectangle(target, (FLOAT)prc->left + dx, (FLOAT)prc->top, (FLOAT)prc->right + dx, (FLOAT)prc->bottom, (ID2D1Brush*)pBrush, 1.0F, (ID2D1StrokeStyle*)pw->pStrokeStyle);
    cd2dRenderTargetFillRectangle(target, (FLOAT)prc->left + dx, (FLOAT)prc->top, (FLOAT)prc->right + dx, (FLOAT)prc->bottom, (ID2D1Brush*)pBrush);
}

static FORCE_INLINE void ToLogical(RECT* prc)
{
    prc->right  = RECTWIDTH(*prc);
    prc->bottom = RECTHEIGHT(*prc);
    prc->left   = 0;
    prc->top    = 0;
}

static FORCE_INLINE FLOAT CaptionGlyphCoordinate(LONG origin, LONG extent, LONG offset)
{
    return (FLOAT)origin + (HALF_SCALE * (FLOAT)extent) + (FLOAT)offset;
}

static void PaintCaption(HWND hWnd, LPIMMERSIVEWINDOW pw)
{
    const UINT            dpi         = GetDpiForWindow(hWnd);
    const int             buttonWidth = MulDiv(CAPTION_BUTTON_WIDTH_DIPS, (int)dpi, REFERENCE_DPI);
    const int             fontPixels  = MulDiv(CAPTION_FONT_DIPS, (int)dpi, REFERENCE_DPI);
    const FLOAT           fontSize    = (FLOAT)fontPixels;
    const FLOAT           scale       = 1.5F;
    const int             inset       = RoundToInt(HALF_SCALE * fontSize);
    const int             inset2      = RoundToInt(HALF_SCALE * fontSize * scale);
    const BOOL            fMaximized  = IsMaximized(hWnd);
    ID2D1SolidColorBrush* text        = pw->fDarkMode ? pw->pLightBrush : pw->pDarkBrush;
    ID2D1SolidColorBrush* fill        = pw->pColorizationBrush;
    RECT                  rcCaption;
    RECT                  rcClose;
    RECT                  rcMax;
    RECT                  rcMin;
    RECT                  rcLightDark;
    LONG                  captionWidth;
    SIZE                  pad;

    GetCaptionRect(hWnd, &rcCaption);
    rcCaption.right = rcCaption.left + pw->szClient.cx;
    rcClose = rcMax = rcMin = rcLightDark = rcCaption;
    GetCaptionButtonRectFast(&rcClose, buttonWidth, CB_CLOSEBUTTON);
    GetCaptionButtonRectFast(&rcMax, buttonWidth, CB_MAXBUTTON);
    GetCaptionButtonRectFast(&rcMin, buttonWidth, CB_MINBUTTON);
    GetCaptionButtonRectFast(&rcLightDark, buttonWidth, CB_LIGHTDARKBUTTON);
    captionWidth = RECTWIDTH(rcCaption);

    /* close */
    ToLogical(&rcClose);
    OffsetRect(&rcClose, captionWidth - RECTWIDTH(rcClose) - 1, 0);
    if (CB_CLOSEBUTTON == pw->eHotCaptionButton)
    {
        FillButton(pw, &rcClose, pw->pRedBrush, HALF_SCALE);
    }
    DrawGlyph(pw,
              pw->pFontFace,
              pw->glyphs[GLYPH_CLOSE],
              fontSize * CAPTION_STROKE_SCALE,
              CaptionGlyphCoordinate(rcClose.left, RECTWIDTH(rcClose) - 1, -inset),
              CaptionGlyphCoordinate(rcClose.top, RECTHEIGHT(rcClose), inset2 - 1),
              (ID2D1Brush*)text,
              DWRITE_MEASURING_MODE_GDI_NATURAL);
    DrawGlyph(pw,
              pw->pFontFace,
              pw->glyphs[GLYPH_CLOSE],
              fontSize * CAPTION_STROKE_SCALE,
              CaptionGlyphCoordinate(rcClose.left, RECTWIDTH(rcClose) - 1, -inset),
              CaptionGlyphCoordinate(rcClose.top, RECTHEIGHT(rcClose), inset2 - 1),
              (ID2D1Brush*)text,
              DWRITE_MEASURING_MODE_GDI_NATURAL);

    /* maximize / restore */
    ToLogical(&rcMax);
    OffsetRect(&rcMax, captionWidth - (2 * RECTWIDTH(rcMax)) - 2, 0);
    if (CB_MAXBUTTON == pw->eHotCaptionButton)
    {
        FillButton(pw, &rcMax, fill, 0.0F);
    }
    pad.cx = MulDiv(CAPTION_PAD_X_DIPS, (int)dpi, REFERENCE_DPI);
    pad.cy = MulDiv(CAPTION_PAD_Y_DIPS, (int)dpi, REFERENCE_DPI);
    InflateRect(&rcMax, -pad.cx, -pad.cy);
    rcMax.bottom -= 1;
    rcMax.right -= 1;
    rcMax.left -= 1;
    DrawGlyph(pw,
              pw->pFontFace,
              pw->glyphs[fMaximized ? GLYPH_RESTORE : GLYPH_MAXIMIZE],
              fontSize,
              (FLOAT)rcMax.left,
              CaptionGlyphCoordinate(rcMax.top, RECTHEIGHT(rcMax), inset),
              (ID2D1Brush*)text,
              DWRITE_MEASURING_MODE_NATURAL);

    /* minimize */
    ToLogical(&rcMin);
    OffsetRect(&rcMin, captionWidth - (3 * RECTWIDTH(rcMin)) - 3, 0);
    if (CB_MINBUTTON == pw->eHotCaptionButton)
    {
        FillButton(pw, &rcMin, fill, 0.0F);
    }
    DrawGlyph(pw,
              pw->pFontFace,
              pw->glyphs[GLYPH_MINIMIZE],
              fontSize,
              CaptionGlyphCoordinate(rcMin.left, RECTWIDTH(rcMin), -inset),
              CaptionGlyphCoordinate(rcMin.top, RECTHEIGHT(rcMin), inset),
              (ID2D1Brush*)text,
              DWRITE_MEASURING_MODE_NATURAL);

    /* light / dark toggle */
    ToLogical(&rcLightDark);
    OffsetRect(&rcLightDark, captionWidth - (4 * RECTWIDTH(rcLightDark)) - 2, 0);
    if (CB_LIGHTDARKBUTTON == pw->eHotCaptionButton)
    {
        FillButton(pw, &rcLightDark, fill, 0.0F);
    }
    DrawGlyph(pw,
              pw->pFontFace,
              pw->glyphs[pw->fLightMode ? GLYPH_DARKMODE3 : GLYPH_LIGHTMODE1],
              fontSize * scale,
              CaptionGlyphCoordinate(rcLightDark.left, RECTWIDTH(rcLightDark), -inset2),
              CaptionGlyphCoordinate(rcLightDark.top, RECTHEIGHT(rcLightDark), inset2),
              (ID2D1Brush*)text,
              DWRITE_MEASURING_MODE_NATURAL);
}

static void RenderFrame(HWND hWnd, LPIMMERSIVEWINDOW pw, LONG width, LONG height, UINT presentFlags)
{
    ID2D1RenderTarget* target = (ID2D1RenderTarget*)pw->pD2D1DeviceContext;
    IDXGISwapChain* swapChain = (IDXGISwapChain*)pw->pSwapChain;
    D2D1_RECT_F destination = {0, 0, 0, 0};
    HRESULT result;
    bool_t has_width;
    bool_t has_height;
    UINT safe_width;
    UINT safe_height;

    has_width = 0 < width;
    has_height = 0 < height;
    safe_width = ((UINT)width & (0U - (UINT)has_width)) + (UINT)(!has_width);
    safe_height = ((UINT)height & (0U - (UINT)has_height)) + (UINT)(!has_height);
    if (pw->fRendering)
    {
        return;
    }
    if (NULL == swapChain)
    {
        return;
    }
    destination.right = (FLOAT)safe_width;
    destination.bottom = (FLOAT)safe_height;
    pw->fRendering = TRUE;
    WaitForFrameSlot(pw);
    if (pw->szClient.cx != (LONG)safe_width || pw->szClient.cy != (LONG)safe_height)
    {
        pw->szClient.cx = (LONG)safe_width;
        pw->szClient.cy = (LONG)safe_height;
        presentFlags |= DXGI_PRESENT_RESTART;
    }
    if (!has_width || !has_height)
    {
        pw->fRendering = FALSE;
        return;
    }
    cd2dDeviceContextSetTarget(pw->pD2D1DeviceContext, (ID2D1Image*)pw->pTargetBitmap);
    cd2dRenderTargetSetDpi(target, REFERENCE_DPI, REFERENCE_DPI);
    cd2dRenderTargetBeginDraw(target);
    cd2dRenderTargetSetIdentityTransform(target);
    cd2dRenderTargetClearBlack(target);
    if (pw->fHaveVideo)
    {
        cd2dDeviceContextDrawBitmap(pw->pD2D1DeviceContext, (ID2D1Bitmap*)pw->pVideoBitmap, &destination,
            1.0F, D2D1_INTERPOLATION_MODE_LINEAR, NULL, NULL);
    }
    cd2dRenderTargetSetTextAntialiasMode(target, D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    PaintCaption(hWnd, pw);
    result = cd2dRenderTargetEndDraw(target, NULL, NULL);
    Check(SUCCEEDED(result), TEXT("frame EndDraw"));
    cd2dDeviceContextClearTarget(pw->pD2D1DeviceContext);
    result = cdxgiSwapChainPresent(swapChain, 1, presentFlags);
    pw->fFrameSlotReady = FALSE;
    pw->captionDpi = GetDpiForWindow(hWnd);
    pw->fCaptionMaximized = IsMaximized(hWnd);
    pw->fFrameDirty = FALSE;
    pw->fRendering = FALSE;
    Trace(TEXT("composition present"), result);
    Check(SUCCEEDED(result), TEXT("composition Present"));
}

extern FORCE_INLINE BOOL GetImmersiveDevice(HWND hWnd, ID3D11Device** device, ID3D11DeviceContext** context)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    if (NULL == windowState || NULL == windowState->pD3D11Device)
    {
        return FALSE;
    }
    *device = windowState->pD3D11Device;
    *context = windowState->pD3D11DeviceContext;
    return TRUE;
}

extern FORCE_INLINE void SetImmersiveVideo(HWND hWnd, UINT width, UINT height, IMMERSIVEVIDEOPROC draw)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    DXGI_SAMPLE_DESC sample = {1, 0};
    IDXGISurface* surface;
    D2D1_PIXEL_FORMAT format = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE};
    D2D1_BITMAP_PROPERTIES1 properties;
    Check(width > 0 && height > 0 && NULL != draw, TEXT("video dimensions and callback"));
    if (windowState->pVideoBitmap)
    {
        cdxgiUnknownRelease((IUnknown*)windowState->pVideoBitmap);
    }
    if (windowState->pVideoTexture)
    {
        cdxgiUnknownRelease((IUnknown*)windowState->pVideoTexture);
    }
    cdx11DeviceCreateTexture2D(windowState->pD3D11Device, width, height, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, sample,
        D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, NULL, &windowState->pVideoTexture);
    Check(SUCCEEDED(cdxgiUnknownQueryInterface((IUnknown*)windowState->pVideoTexture, &IID_IDXGISurface, (void**)&surface)), TEXT("video bitmap surface"));
    cd2dInitBitmapProperties1(&properties, format, REFERENCE_DPI, REFERENCE_DPI, D2D1_BITMAP_OPTIONS_NONE, NULL);
    cd2dDeviceContextCreateBitmapFromDxgiSurface(windowState->pD2D1DeviceContext, surface, &properties, &windowState->pVideoBitmap);
    cdxgiUnknownRelease((IUnknown*)surface);
    windowState->szVideo.cx = (LONG)width;
    windowState->szVideo.cy = (LONG)height;
    windowState->pVideoProc = draw;
    windowState->fHaveVideo = FALSE;
}

extern FORCE_INLINE void UpdateImmersiveVideo(HWND hWnd)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    RECT client;
    if (NULL == windowState || NULL == windowState->pVideoTexture || NULL == windowState->pVideoProc)
    {
        return;
    }
    windowState->pVideoProc(hWnd, windowState->pVideoTexture, 0, 0, 0);
    windowState->fHaveVideo = TRUE;
    windowState->fFrameDirty = TRUE;
    if (!IsIconic(hWnd) && GetClientRect(hWnd, &client))
    {
        RenderFrame(hWnd, windowState, client.right, client.bottom, 0);
    }
}

BOOL GetResizeInfo(HWND hWnd, LPRESIZEINFO lpri)
{
    LPIMMERSIVEWINDOW windowState = GetState(hWnd);
    if (NULL == windowState || NULL == lpri)
    {
        return FALSE;
    }
    *lpri = windowState->ri;
    return TRUE;
}

/* ------------------------------------------------------------------------------------------ 7 */

#define HANDLE_WM_NCMOUSELEAVE(hwnd, wParam, lParam, fn)          ((fn)((hwnd)), 0L)
#define HANDLE_WM_ENTERSIZEMOVE(hwnd, wParam, lParam, fn)         ((fn)((hwnd)), 0L)
#define HANDLE_WM_EXITSIZEMOVE(hwnd, wParam, lParam, fn)          ((fn)((hwnd)), 0L)
#define HANDLE_WM_MOVING(hwnd, wParam, lParam, fn)                (LRESULT)((fn)((hwnd), (lParam)))
#define HANDLE_WM_SIZING(hwnd, wParam, lParam, fn)                (LRESULT)((fn)((hwnd), (UINT)(wParam), (lParam)))
#define HANDLE_WM_DWMNCRENDERINGCHANGED(hwnd, wParam, lParam, fn) ((fn)((hwnd), (BOOL)(wParam)), 0L)
#define HANDLE_WM_INITD2D(hwnd, wParam, lParam, fn)               (LRESULT)((fn)((hwnd), GetState(hwnd)))
#define HANDLE_WM_MOUSELEAVE(hwnd, wParam, lParam, fn)            ((fn)((hwnd)), 0L)

LRESULT CALLBACK DefImmersiveProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    switch (uMsg)
    {
        HANDLE_MSG(hWnd, WM_NCCREATE, OnNCCreate);
        HANDLE_MSG(hWnd, WM_CREATE, OnCreate);
        HANDLE_MSG(hWnd, WM_DESTROY, OnDestroy);
        HANDLE_MSG(hWnd, WM_NCDESTROY, OnNCDestroy);
        HANDLE_MSG(hWnd, WM_ACTIVATE, OnActivate);
        HANDLE_MSG(hWnd, WM_PAINT, OnPaint);
        HANDLE_MSG(hWnd, WM_CLOSE, OnClose);
        HANDLE_MSG(hWnd, WM_ERASEBKGND, OnEraseBkgnd);
        HANDLE_MSG(hWnd, WM_SYSCOLORCHANGE, OnSysColorChange);
        HANDLE_MSG(hWnd, WM_SETCURSOR, OnSetCursor);
        HANDLE_MSG(hWnd, WM_GETMINMAXINFO, OnGetMinMaxInfo);
        HANDLE_MSG(hWnd, WM_MOVING, OnMoving);
        HANDLE_MSG(hWnd, WM_SIZING, OnSizing);
        HANDLE_MSG(hWnd, WM_WINDOWPOSCHANGED, OnWindowPosChanged);
        HANDLE_MSG(hWnd, WM_NCCALCSIZE, OnNCCalcSize);
        HANDLE_MSG(hWnd, WM_NCHITTEST, OnNCHitTest);
        HANDLE_MSG(hWnd, WM_NCPAINT, OnNCPaint);
        HANDLE_MSG(hWnd, WM_NCACTIVATE, OnNCActivate);
        HANDLE_MSG(hWnd, WM_NCMOUSEMOVE, OnNCMouseMove);
        HANDLE_MSG(hWnd, WM_NCLBUTTONDOWN, OnNCLButtonDown);
        HANDLE_MSG(hWnd, WM_NCLBUTTONUP, OnNCLButtonUp);
        HANDLE_MSG(hWnd, WM_NCLBUTTONDBLCLK, OnNCLButtonDblClk);
        HANDLE_MSG(hWnd, WM_NCRBUTTONUP, OnNCRButtonUp);
        HANDLE_MSG(hWnd, WM_NCMOUSELEAVE, OnNCMouseLeave);
        HANDLE_MSG(hWnd, WM_KEYUP, OnKeyUp);
        HANDLE_MSG(hWnd, WM_MOUSEMOVE, OnMouseMove);
        HANDLE_MSG(hWnd, WM_PARENTNOTIFY, OnParentNotify);
        HANDLE_MSG(hWnd, WM_MOUSELEAVE, OnMouseLeave);
        HANDLE_MSG(hWnd, WM_ENTERSIZEMOVE, OnEnterSizeMove);
        HANDLE_MSG(hWnd, WM_EXITSIZEMOVE, OnExitSizeMove);
        HANDLE_MSG(hWnd, WM_DWMNCRENDERINGCHANGED, OnDwmNcRenderingChanged);
        HANDLE_MSG(hWnd, WM_INITD2D, OnInitD2D);
    default:
        return DefWindowProc(hWnd, uMsg, wParam, lParam);
    }
}

extern FORCE_INLINE VOID WINAPI InitImmersiveControls(VOID)
{
    WNDCLASS windowClass;
    TCHAR     tracePath[MAX_PATH];
    if (GetEnvironmentVariable(TEXT("IMMERSIVE_TRACE"), tracePath, MAX_PATH))
    {
        g_hTrace      = CreateFile(tracePath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        g_traceOrigin = Now();
    }
    ZeroMemory(&windowClass, sizeof(windowClass));
    windowClass.style         = CS_OWNDC | CS_BYTEALIGNWINDOW | CS_BYTEALIGNCLIENT | CS_DBLCLKS;
    windowClass.lpfnWndProc   = DefImmersiveProc;
    windowClass.cbWndExtra    = 0;
    windowClass.hCursor       = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = IMMERSIVE_CLASS;
    Check(0 != RegisterClass(&windowClass), TEXT("RegisterClass"));
}
