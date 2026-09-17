/* immersive_window.c - the ImmersiveWindow32 control on the cdx libraries. See immersive_window.h.
**
** Structure, in the order of the original BorderlessWindow32/ImmersiveWindow.c:
**   1. private state and the per-window record
**   2. device and swap-chain setup (OnInitD2D) and teardown
**   3. caption geometry and the frame-timing helpers
**   4. dark and light mode through the uxtheme private ordinals
**   5. the message handlers
**   6. the caption drawing and the public paint entry points
**   7. the window procedure */

#include "immersive_window.h"

#include <windowsx.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <shellapi.h>

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

#define RECTWIDTH(rc)    (labs((rc).right - (rc).left))
#define RECTHEIGHT(rc)   (labs((rc).bottom - (rc).top))
#define CLAMP(x, lo, hi) (max(min((x), (hi)), (lo)))

#define CAPTION_HEIGHT_DIPS       30
#define CAPTION_BUTTON_WIDTH_DIPS 45

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
    IDXGIFactory2*       pDXGIFactory2;
    IDXGISwapChain2*     pDXGISwapChain2;
    IDXGISurface2*       pDXGISurface2;
    HANDLE               hFrameLatencyWaitableObject;
#ifndef NDEBUG
    ID3D11Debug* pD3D11Debug;
#endif
    /* Direct2D */
    ID2D1Factory2*        pD2D1Factory2;
    ID2D1Device1*         pD2D1Device1;
    ID2D1DeviceContext*   pD2D1DeviceContext;
    ID2D1Bitmap1*         pD2D1TargetBitmap;
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
    DEVICEDRAWPROC pPreProc;
    DEVICEDRAWPROC pProc;
    RESIZEINFO     ri;
    BOOL           fLightMode;
    BOOL           fDarkMode;
    CAPTIONBUTTON  eHotCaptionButton;
    /* the geometry of the last presented frame, stamped with the composition frame it targets */
    SIZE     szContent;
    LONGLONG llTargetFrame;
    BOOL     fContentValid;
    BOOL     fInResizePaint;
} IMMERSIVEWINDOW, *LPIMMERSIVEWINDOW;

/* uxtheme private ordinals and the composition attribute, resolved once */
typedef enum tagPREFERREDAPPMODE
{
    PAM_DEFAULT,
    PAM_ALLOWDARK,
    PAM_FORCEDARK,
    PAM_FORCELIGHT,
    PAM_LAST
} PREFERREDAPPMODE;
typedef PREFERREDAPPMODE(WINAPI* PFNSETPREFERREDAPPMODE)(PREFERREDAPPMODE);
typedef VOID(WINAPI* PFNFLUSHMENUTHEME)(VOID);
typedef VOID(WINAPI* PFNINVALIDATEAPPTHEME)(VOID);
typedef VOID(WINAPI* PFNREFRESHIMMERSIVECOLORPOLICYSTATE)(VOID);
static PFNSETPREFERREDAPPMODE              g_SetPreferredAppMode;
static PFNFLUSHMENUTHEME                   g_FlushMenuTheme;
static PFNINVALIDATEAPPTHEME               g_InvalidateAppTheme;
static PFNREFRESHIMMERSIVECOLORPOLICYSTATE g_RefreshImmersiveColorPolicyState;

/* D3DKMT vertical blank access (gdi32) */
typedef UINT D3DKMT_HANDLE;
typedef UINT D3DDDI_VIDEO_PRESENT_SOURCE_ID;
typedef struct _D3DKMT_OPENADAPTERFROMHDC
{
    HDC                            hDc;
    D3DKMT_HANDLE                  hAdapter;
    LUID                           AdapterLuid;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} D3DKMT_OPENADAPTERFROMHDC;
typedef struct _D3DKMT_WAITFORVERTICALBLANKEVENT
{
    D3DKMT_HANDLE                  hAdapter;
    D3DKMT_HANDLE                  hDevice;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
} D3DKMT_WAITFORVERTICALBLANKEVENT;
typedef struct _D3DKMT_GETSCANLINE
{
    D3DKMT_HANDLE                  hAdapter;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId;
    BOOLEAN                        InVerticalBlank;
    UINT                           ScanLine;
} D3DKMT_GETSCANLINE;
EXTERN_C LONG APIENTRY D3DKMTOpenAdapterFromHdc(D3DKMT_OPENADAPTERFROMHDC*);
EXTERN_C LONG APIENTRY D3DKMTWaitForVerticalBlankEvent(const D3DKMT_WAITFORVERTICALBLANKEVENT*);
EXTERN_C LONG APIENTRY D3DKMTGetScanLine(D3DKMT_GETSCANLINE*);

static BOOL g_fActive;

/* IMMERSIVE_TRACE=<file>: timestamps of the resize path against the compositor's clock */
static HANDLE   g_hTrace;
static LONGLONG g_traceOrigin;

static LONGLONG Now(void)
{
    LARGE_INTEGER li;
    QueryPerformanceCounter(&li);
    return li.QuadPart;
}

static void Trace(const char* pWhat, HRESULT hr)
{
    char          line[256];
    DWORD         written;
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
    wsprintfA(line, "%8ld us  %-28s hr=%08lX  vblank=%ld us  compose=%ld us\r\n", (long)((now - g_traceOrigin) * 1000000 / frequency.QuadPart), pWhat, (unsigned long)hr,
              (long)(((LONGLONG)timing.qpcVBlank - now) * 1000000 / frequency.QuadPart), (long)(((LONGLONG)timing.qpcCompose - now) * 1000000 / frequency.QuadPart));
    WriteFile(g_hTrace, line, lstrlenA(line), &written, NULL);
}

static LPIMMERSIVEWINDOW GetState(HWND hWnd)
{
    return (LPIMMERSIVEWINDOW)GetWindowLongPtr(hWnd, 0);
}

static void Check(BOOL condition, const char* pWhat)
{
    if (!condition)
    {
        OutputDebugStringA("ImmersiveWindow: ");
        OutputDebugStringA(pWhat);
        OutputDebugStringA(" failed\n");
        ExitProcess(1);
    }
}

/* MyRoundf of the original: the larger of the truncation and the rounded value */
static int RoundToInt(FLOAT f)
{
    return max((int)f, (int)(f + 0.5f));
}

/* ------------------------------------------------------------------------------------------ 2 */

static void LoadAppIcon(HWND hWnd, LPIMMERSIVEWINDOW pw)
{
    WCHAR       szImage[MAX_PATH];
    DWORD       cchImage = MAX_PATH;
    HICON       hIcon;
    ICONINFO    ii;
    BITMAP      bm;
    HDC         hScreen;
    HDC         hMem;
    BITMAPINFO  bi;
    void*       pBits;
    HBITMAP     hDib;
    HGDIOBJ     hOld;
    D2D1_SIZE_U size;

    QueryFullProcessImageNameW(GetCurrentProcess(), 0, szImage, &cchImage);
    hIcon = ExtractIconW(GetModuleHandleW(NULL), szImage, 0);
    if (NULL == hIcon || (HICON)1 == hIcon)
    {
        hIcon = LoadIconW(NULL, IDI_APPLICATION);
    }
    SendMessageW(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
    SendMessageW(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);

    /* draw the icon into a top-down 32-bit DIB and wrap it as a Direct2D bitmap */
    ZeroMemory(&ii, sizeof(ii));
    GetIconInfo(hIcon, &ii);
    GetObjectW(ii.hbmColor, sizeof(bm), &bm);
    hScreen = GetDC(NULL);
    hMem    = CreateCompatibleDC(hScreen);
    ReleaseDC(NULL, hScreen);
    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth       = bm.bmWidth;
    bi.bmiHeader.biHeight      = -bm.bmHeight;
    bi.bmiHeader.biPlanes      = 1;
    bi.bmiHeader.biBitCount    = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    pBits                      = NULL;
    hDib                       = CreateDIBSection(hMem, &bi, DIB_RGB_COLORS, &pBits, NULL, 0);
    hOld                       = SelectObject(hMem, hDib);
    DrawIconEx(hMem, 0, 0, hIcon, bm.bmWidth, bm.bmHeight, 0, NULL, DI_NORMAL);
    GdiFlush();
    cd2dInitSizeU(&size, (UINT32)bm.bmWidth, (UINT32)bm.bmHeight);
    cd2dDeviceContextCreateBitmapFromMemory(pw->pD2D1DeviceContext, size, pBits, (UINT32)bm.bmWidth * 4, DXGI_FORMAT_B8G8R8A8_UNORM, &pw->pAppIconBitmap);
    SelectObject(hMem, hOld);
    DeleteObject(hDib);
    DeleteDC(hMem);
    DeleteObject(ii.hbmColor);
    DeleteObject(ii.hbmMask);
}

static void SetupFonts(LPIMMERSIVEWINDOW pw)
{
    static const UINT32 symbolCodePoints[3] = {0xE28C, 0x263C, 0xE706};
    IDWriteFactory*     factory;

    cdwCreateFactoryAsFactory1(DWRITE_FACTORY_TYPE_SHARED, &pw->pDWriteFactory1);
    factory = (IDWriteFactory*)pw->pDWriteFactory1;
    cdwFactoryGetSystemFontCollectionCached(factory, &pw->pFontCollection);

    Check(cdwFontCollectionGetFontFamilyByName(pw->pFontCollection, L"Segoe MDL2 Assets", &pw->pFontFamily), "Segoe MDL2 Assets");
    cdwFontFamilyGetFirstMatchingFont(pw->pFontFamily, DWRITE_FONT_WEIGHT_THIN, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &pw->pFont);
    cdwFontCreateFontFace(pw->pFont, &pw->pFontFace);
    cdwFontFaceGetGlyphIndices(pw->pFontFace, g_codePoints, GLYPH_COUNT, pw->glyphs);

    Check(cdwFontCollectionGetFontFamilyByName(pw->pFontCollection, L"Segoe UI Symbol", &pw->pSymbolFamily), "Segoe UI Symbol");
    cdwFontFamilyGetFirstMatchingFont(pw->pSymbolFamily, DWRITE_FONT_WEIGHT_THIN, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &pw->pSymbolFont);
    cdwFontCreateFontFace(pw->pSymbolFont, &pw->pSymbolFontFace);
    cdwFontFaceGetGlyphIndices(pw->pSymbolFontFace, symbolCodePoints, 3, pw->symbolGlyphs);

    cdwFactoryCreateTextFormat(factory, L"Segoe MDL2 Assets", pw->pFontCollection, DWRITE_FONT_WEIGHT_THIN, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 48.0f, L"", &pw->pTextFormat);
}

static void SetupBrushes(LPIMMERSIVEWINDOW pw)
{
    ID2D1RenderTarget* target   = (ID2D1RenderTarget*)pw->pD2D1DeviceContext;
    const COLORREF     inactive = GetSysColor(COLOR_BTNSHADOW);
    const COLORREF     accent   = GetSysColor(COLOR_MENUHILIGHT);

    cd2dRenderTargetCreateSolidColorBrushRGBA(target, 1.0f, 1.0f, 1.0f, 0.95f, &pw->pLightBrush);
    cd2dRenderTargetCreateSolidColorBrushRGBA(target, 43.0f / 255.0f, 43.0f / 255.0f, 43.0f / 255.0f, 0.85f, &pw->pDarkBrush);
    cd2dRenderTargetCreateSolidColorBrushRGBA(target, 196.0f / 255.0f, 43.0f / 255.0f, 28.0f / 255.0f, 1.0f, &pw->pRedBrush);
    cd2dRenderTargetCreateSolidColorBrushRGBA(target, GetRValue(inactive) / 255.0f, GetGValue(inactive) / 255.0f, GetBValue(inactive) / 255.0f, 1.0f, &pw->pInactiveBrush);
    cd2dRenderTargetCreateSolidColorBrushRGBA(target, GetRValue(accent) / 255.0f, GetGValue(accent) / 255.0f, GetBValue(accent) / 255.0f, 1.0f, &pw->pColorizationBrush);
    cd2dRenderTargetCreateSolidColorBrushRGBA(target, GetRValue(accent) / 255.0f, GetGValue(accent) / 255.0f, GetBValue(accent) / 255.0f, 0.1f, &pw->pFadedColorizationBrush);
    cd2dFactory1CreateStrokeStyle((ID2D1Factory1*)pw->pD2D1Factory2,
                                  D2D1_CAP_STYLE_SQUARE,
                                  D2D1_CAP_STYLE_SQUARE,
                                  D2D1_CAP_STYLE_SQUARE,
                                  D2D1_LINE_JOIN_ROUND,
                                  0.0f,
                                  D2D1_DASH_STYLE_SOLID,
                                  0.0f,
                                  D2D1_STROKE_TRANSFORM_TYPE_HAIRLINE,
                                  NULL,
                                  0,
                                  &pw->pStrokeStyle);
    cd2dDeviceContextCreateGaussianBlurEffect(pw->pD2D1DeviceContext, &pw->pBlurEffect);
}

/* The device, the composition swap chain sized to the desktop, the Direct2D target over its
** back buffer, and the DirectComposition visual tree that shows it in the window. */
static LRESULT OnInitD2D(HWND hWnd, LPIMMERSIVEWINDOW pw)
{
    /* no SINGLETHREADED: the Media Foundation decoder of video.c works the device from its own threads */
    static const UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT | D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_PREVENT_INTERNAL_THREADING_OPTIMIZATIONS |
                                    D3D11_CREATE_DEVICE_DISABLE_GPU_TIMEOUT | D3D11_CREATE_DEVICE_PREVENT_ALTERING_LAYER_SETTINGS_FROM_REGISTRY
#ifndef NDEBUG
                                    | D3D11_CREATE_DEVICE_DEBUG
#endif
        ;
    static const UINT              swapChainFlags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING | DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
    static const D3D_FEATURE_LEVEL levels[]       = {D3D_FEATURE_LEVEL_10_0, D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_12_0, D3D_FEATURE_LEVEL_12_1};
    D3D_FEATURE_LEVEL              level;
    DXGI_SAMPLE_DESC               single;
    IDXGISwapChain1*               swapChain1;
    D2D1_FACTORY_OPTIONS           options;
    HDC                            hDC;
    UINT                           width;
    UINT                           height;

    if (NULL != pw->pD3D11Device)
    {
        return 0;
    }

    Check(SUCCEEDED(cdx11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, deviceFlags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION, &pw->pD3D11Device, &level, &pw->pD3D11DeviceContext)),
          "D3D11CreateDevice");
#ifndef NDEBUG
    cdx11DeviceGetDebug(pw->pD3D11Device, &pw->pD3D11Debug);
#endif
    Check(SUCCEEDED(cdxgiUnknownQueryInterfaceAsDevice1((IUnknown*)pw->pD3D11Device, &pw->pDXGIDevice1)), "IDXGIDevice1");

    /* the swap chain covers the desktop so that a resize never reallocates it; DXGI_SCALING_STRETCH
    ** with the visual clipped to the window shows the top-left window-sized part */
    hDC    = GetDC(NULL);
    width  = (UINT)GetDeviceCaps(hDC, DESKTOPHORZRES);
    height = (UINT)GetDeviceCaps(hDC, DESKTOPVERTRES);
    ReleaseDC(NULL, hDC);
    cdxgiInitSingleSampleDesc(&single);
#ifndef NDEBUG
    cdxgiCreateFactory2(DXGI_CREATE_FACTORY_DEBUG, &pw->pDXGIFactory2);
#else
    cdxgiCreateFactory2(0, &pw->pDXGIFactory2);
#endif
    cdxgiFactoryCreateSwapChainForComposition(pw->pDXGIFactory2,
                                              (IUnknown*)pw->pDXGIDevice1,
                                              width,
                                              height,
                                              DXGI_FORMAT_B8G8R8A8_UNORM,
                                              FALSE,
                                              single,
                                              DXGI_USAGE_RENDER_TARGET_OUTPUT,
                                              5,
                                              DXGI_SCALING_STRETCH,
                                              DXGI_SWAP_EFFECT_FLIP_DISCARD,
                                              DXGI_ALPHA_MODE_PREMULTIPLIED,
                                              swapChainFlags,
                                              NULL,
                                              &swapChain1);
    Check(SUCCEEDED(cdxgiUnknownQueryInterfaceAsSwapChain2((IUnknown*)swapChain1, &pw->pDXGISwapChain2)), "IDXGISwapChain2");
    cdxgiUnknownRelease((IUnknown*)swapChain1);
    cdxgiFactoryMakeWindowAssociation((IDXGIFactory*)pw->pDXGIFactory2, hWnd, DXGI_MWA_NO_WINDOW_CHANGES | DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_PRINT_SCREEN);
    pw->hFrameLatencyWaitableObject = cdxgiSwapChainGetFrameLatencyWaitableObject(pw->pDXGISwapChain2);

    /* Direct2D over the back buffer */
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
    cd2dCreateFactoryAsFactory2(D2D1_FACTORY_TYPE_SINGLE_THREADED, &options, &pw->pD2D1Factory2);
    cd2dFactory2CreateDevice(pw->pD2D1Factory2, (IDXGIDevice*)pw->pDXGIDevice1, &pw->pD2D1Device1);
    cd2dSetupDefaultDeviceContext((ID2D1Device*)pw->pD2D1Device1, &pw->pD2D1DeviceContext);
    cdxgiSwapChainGetBufferAsSurface2((IDXGISwapChain*)pw->pDXGISwapChain2, 0, &pw->pDXGISurface2);
    cd2dDeviceContextCreateTargetBitmapFromSurface(pw->pD2D1DeviceContext, (IDXGISurface*)pw->pDXGISurface2, DXGI_FORMAT_B8G8R8A8_UNORM, &pw->pD2D1TargetBitmap);
    cd2dDeviceContextSetTarget(pw->pD2D1DeviceContext, (ID2D1Image*)pw->pD2D1TargetBitmap);

    /* the composition tree: one visual whose content is the swap chain, rooted in the window */
    cdcompCreateDevice((IDXGIDevice*)pw->pDXGIDevice1, &pw->pDCompDevice);
    cdcompDeviceSetupHwndContent(pw->pDCompDevice, hWnd, TRUE, (IUnknown*)pw->pDXGISwapChain2, &pw->pDCompTarget, &pw->pDCompVisual);

    SetupBrushes(pw);
    SetupFonts(pw);
    LoadAppIcon(hWnd, pw);
    return 0;
}

static void OnShutdownD2D(LPIMMERSIVEWINDOW pw)
{
#define RELEASE(p)                                                                                                                                                                                     \
    do                                                                                                                                                                                                 \
    {                                                                                                                                                                                                  \
        if (NULL != (p))                                                                                                                                                                               \
        {                                                                                                                                                                                              \
            cdxgiUnknownRelease((IUnknown*)(p));                                                                                                                                                       \
            (p) = NULL;                                                                                                                                                                                \
        }                                                                                                                                                                                              \
    } while (0)
    RELEASE(pw->pAppIconBitmap);
    RELEASE(pw->pTextFormat);
    RELEASE(pw->pSymbolFontFace);
    RELEASE(pw->pSymbolFont);
    RELEASE(pw->pSymbolFamily);
    RELEASE(pw->pFontFace);
    RELEASE(pw->pFont);
    RELEASE(pw->pFontFamily);
    RELEASE(pw->pFontCollection);
    RELEASE(pw->pDWriteFactory1);
    RELEASE(pw->pBlurEffect);
    RELEASE(pw->pStrokeStyle);
    RELEASE(pw->pFadedColorizationBrush);
    RELEASE(pw->pColorizationBrush);
    RELEASE(pw->pInactiveBrush);
    RELEASE(pw->pRedBrush);
    RELEASE(pw->pDarkBrush);
    RELEASE(pw->pLightBrush);
    RELEASE(pw->pDCompVisual);
    RELEASE(pw->pDCompTarget);
    RELEASE(pw->pDCompDevice);
    if (NULL != pw->pD2D1DeviceContext)
    {
        cd2dDeviceContextClearTarget(pw->pD2D1DeviceContext);
    }
    RELEASE(pw->pD2D1TargetBitmap);
    RELEASE(pw->pD2D1DeviceContext);
    RELEASE(pw->pD2D1Device1);
    RELEASE(pw->pD2D1Factory2);
    RELEASE(pw->pDXGISurface2);
    RELEASE(pw->pDXGISwapChain2);
#ifndef NDEBUG
    if (NULL != pw->pD3D11Debug)
    {
        cdx11DebugReportLiveDeviceObjects(pw->pD3D11Debug, D3D11_RLDO_DETAIL);
        RELEASE(pw->pD3D11Debug);
    }
#endif
    RELEASE(pw->pD3D11DeviceContext);
    RELEASE(pw->pD3D11Device);
    RELEASE(pw->pDXGIDevice1);
    RELEASE(pw->pDXGIFactory2);
#undef RELEASE
}

/* ------------------------------------------------------------------------------------------ 3 */

static BOOL GetWindowBorders(HWND hWnd, LPSIZE lpsz)
{
    const UINT dpi = GetDpiForWindow(hWnd);
    lpsz->cx       = GetSystemMetricsForDpi(SM_CXFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    lpsz->cy       = GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
    return TRUE;
}

static BOOL GetCaptionRect(HWND hWnd, LPRECT lprc)
{
    const UINT dpi     = GetDpiForWindow(hWnd);
    const int  nHeight = MulDiv(CAPTION_HEIGHT_DIPS, (int)dpi, 96);
    SIZE       border;
    const BOOL fMaximized = IsMaximized(hWnd);

    if (!GetWindowRect(hWnd, lprc))
    {
        return FALSE;
    }
    GetWindowBorders(hWnd, &border);
    lprc->top += border.cy * !fMaximized;
    lprc->left += border.cx * !fMaximized;
    lprc->right -= border.cx * !fMaximized;
    lprc->bottom = lprc->top + nHeight;
    return TRUE;
}

static BOOL GetCaptionButtonRectFast(LPRECT lprc, int nWidth, CAPTIONBUTTON eButton)
{
    int slot;
    switch (eButton)
    {
    case CB_CLOSEBUTTON:
        slot = 0;
        break;
    case CB_MAXBUTTON:
        slot = 1;
        break;
    case CB_MINBUTTON:
        slot = 2;
        break;
    case CB_LIGHTDARKBUTTON:
        slot = 3;
        break;
    default:
        return FALSE;
    }
    lprc->left = (lprc->right - nWidth) + 1;
    lprc->left -= slot * nWidth + (slot ? 1 : 0);
    lprc->right -= slot * nWidth + (slot ? 1 : 0);
    return TRUE;
}

static BOOL GetCaptionButtonRect(HWND hWnd, LPRECT lprc, CAPTIONBUTTON eButton)
{
    if (!GetCaptionRect(hWnd, lprc))
    {
        return FALSE;
    }
    return GetCaptionButtonRectFast(lprc, MulDiv(CAPTION_BUTTON_WIDTH_DIPS, (int)GetDpiForWindow(hWnd), 96), eButton);
}

static BOOL GetWindowContentRect(HWND hWnd, LPRECT lprc)
{
    RECT rcClient;
    if (!GetWindowRect(hWnd, lprc) || !GetClientRect(hWnd, &rcClient))
    {
        return FALSE;
    }
    lprc->right  = lprc->left + RECTWIDTH(rcClient);
    lprc->bottom = lprc->top + RECTHEIGHT(rcClient);
    return TRUE;
}

static BOOL ContentRectHasArea(LPCRECT lprc)
{
    return (lprc->right > lprc->left) && (lprc->bottom > lprc->top);
}

static BOOL ContentSizeMatches(LPIMMERSIVEWINDOW pw, LPCRECT lprc)
{
    return pw->fContentValid && (pw->szContent.cx == lprc->right - lprc->left) && (pw->szContent.cy == lprc->bottom - lprc->top);
}

static BOOL ContentTargetsCurrentFrame(LPIMMERSIVEWINDOW pw, LPCRECT lprc)
{
    if (!ContentSizeMatches(pw, lprc) || NULL == pw->pDCompDevice)
    {
        return FALSE;
    }
    return pw->llTargetFrame == cdcompDeviceGetNextEstimatedFrameTime(pw->pDCompDevice);
}

static BOOL StampPresentedContent(HWND hWnd, LPIMMERSIVEWINDOW pw)
{
    RECT rcContent;

    if (!GetWindowContentRect(hWnd, &rcContent) || !ContentRectHasArea(&rcContent))
    {
        return FALSE;
    }
    pw->szContent.cx  = RECTWIDTH(rcContent);
    pw->szContent.cy  = RECTHEIGHT(rcContent);
    pw->fContentValid = TRUE;
    if (NULL != pw->pDCompDevice)
    {
        pw->llTargetFrame = cdcompDeviceGetNextEstimatedFrameTime(pw->pDCompDevice);
    }
    return TRUE;
}

static BOOL RenderGeometryFrame(HWND hWnd, LPIMMERSIVEWINDOW pw, BOOL fRestart, BOOL fVsync)
{
    BOOL fPresented;
    if (NULL == pw->pD3D11DeviceContext || pw->fInResizePaint)
    {
        return FALSE;
    }
    pw->fInResizePaint = TRUE;
    Trace("geometry frame begin", 0);
    if (pw->pPreProc)
    {
        pw->pPreProc(hWnd);
    }
    Trace("geometry frame predraw done", 0);
    BeginImmersivePaint(hWnd);
    Trace("geometry frame drawn", 0);
    fPresented         = EndImmersivePaint(hWnd, fRestart, fVsync);
    Trace("geometry frame presented", fPresented);
    pw->fInResizePaint = FALSE;
    return fPresented;
}

/* ------------------------------------------------------------------------------------------ 4 */

static void ResolvePrivateThemeApis(void)
{
    HMODULE uxtheme = GetModuleHandleW(L"uxtheme.dll");
    if (NULL == uxtheme)
    {
        uxtheme = LoadLibraryW(L"uxtheme.dll");
    }
    if (NULL != uxtheme)
    {
        g_SetPreferredAppMode              = (PFNSETPREFERREDAPPMODE)(void*)GetProcAddress(uxtheme, MAKEINTRESOURCEA(135));
        g_FlushMenuTheme                   = (PFNFLUSHMENUTHEME)(void*)GetProcAddress(uxtheme, MAKEINTRESOURCEA(136));
        g_InvalidateAppTheme               = (PFNINVALIDATEAPPTHEME)(void*)GetProcAddress(uxtheme, MAKEINTRESOURCEA(115));
        g_RefreshImmersiveColorPolicyState = (PFNREFRESHIMMERSIVECOLORPOLICYSTATE)(void*)GetProcAddress(uxtheme, MAKEINTRESOURCEA(104));
    }
}

static void ApplyAppMode(HWND hWnd, BOOL fDark)
{
    if (g_SetPreferredAppMode)
    {
        g_SetPreferredAppMode(fDark ? PAM_ALLOWDARK : PAM_DEFAULT);
    }
    SetPropW(hWnd, L"UseImmersiveDarkModeColors", (HANDLE)(ULONG_PTR)fDark);
    if (g_RefreshImmersiveColorPolicyState)
    {
        g_RefreshImmersiveColorPolicyState();
    }
    if (g_InvalidateAppTheme)
    {
        g_InvalidateAppTheme();
    }
    if (g_FlushMenuTheme)
    {
        g_FlushMenuTheme();
    }
    SetWindowTheme(hWnd, L"DarkMode_Explorer", fDark ? NULL : L" ");
}

static HRESULT EnableBlurBehind(HWND hWnd)
{
    DWM_BLURBEHIND bb;
    HRESULT        hr;
    ZeroMemory(&bb, sizeof(bb));
    bb.dwFlags  = DWM_BB_ENABLE | DWM_BB_BLURREGION;
    bb.fEnable  = TRUE;
    bb.hRgnBlur = CreateRectRgn(0, 0, -1, -1);
    hr          = DwmEnableBlurBehindWindow(hWnd, &bb);
    DeleteObject(bb.hRgnBlur);
    return hr;
}

/* ------------------------------------------------------------------------------------------ 5 */

static BOOL OnNCCreate(HWND hWnd, LPCREATESTRUCT lpcs)
{
    LPIMMERSIVEWINDOW pw;
    BOOL              fAllowNcPaint = FALSE;
    POINT             pt;
    MONITORINFO       mi;

    EnableNonClientDpiScaling(hWnd);
    DwmSetWindowAttribute(hWnd, DWMWA_ALLOW_NCPAINT, &fAllowNcPaint, sizeof(fAllowNcPaint));

    pw = (LPIMMERSIVEWINDOW)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IMMERSIVEWINDOW));
    Check(NULL != pw, "HeapAlloc");
    SetWindowLongPtr(hWnd, 0, (LONG_PTR)pw);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lpcs->lpCreateParams);

    /* BCS_CENTERED: center the window on the monitor under the cursor */
    mi.cbSize = sizeof(mi);
    if ((BCS_CENTERED & lpcs->style) && GetCursorPos(&pt) && GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi))
    {
        const LONG x = mi.rcWork.left + (RECTWIDTH(mi.rcWork) - lpcs->cx) / 2;
        const LONG y = mi.rcWork.top + (RECTHEIGHT(mi.rcWork) - lpcs->cy) / 2;
        MoveWindow(hWnd, x, y, lpcs->cx, lpcs->cy, TRUE);
    }
    return (BOOL)DefWindowProcW(hWnd, WM_NCCREATE, 0, (LPARAM)lpcs);
}

static void SyncFrameChange(HWND hWnd)
{
    SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER);
}

static BOOL OnCreate(HWND hWnd, LPCREATESTRUCT lpcs)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    pw->fDarkMode        = TRUE;
    SyncFrameChange(hWnd);
    SendMessageW(hWnd, WM_INITD2D, 0, 0);
    FORWARD_WM_CREATE(hWnd, lpcs, DefWindowProcW);
    return TRUE;
}

static void OnDestroy(HWND hWnd)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    OnShutdownD2D(pw);
    PostQuitMessage(0);
}

static void OnNCDestroy(HWND hWnd)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    SetWindowLongPtr(hWnd, 0, 0);
    if (NULL != pw)
    {
        HeapFree(GetProcessHeap(), 0, pw);
    }
    FORWARD_WM_NCDESTROY(hWnd, DefWindowProcW);
}

static void OnSize(HWND hWnd, UINT state, int cx, int cy)
{
    if (SIZE_MAXIMIZED == state)
    {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(wp);
        GetWindowPlacement(hWnd, &wp);
        SetWindowPlacement(hWnd, &wp);
    }
    (void)cx;
    (void)cy;
}

static void OnActivate(HWND hWnd, UINT state, HWND hwndActDeact, BOOL fMinimized)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    g_fActive            = (WA_INACTIVE != state);
    if (!fMinimized)
    {
        const MARGINS margins = {1, 1, -1, 1};
        DwmExtendFrameIntoClientArea(hWnd, &margins);
        EnableBlurBehind(hWnd);
        ApplyAppMode(hWnd, pw->fDarkMode);
    }
    FORWARD_WM_ACTIVATE(hWnd, state, hwndActDeact, fMinimized, DefWindowProcW);
}

static void OnClose(HWND hWnd)
{
    DestroyWindow(hWnd);
}

static void OnPaint(HWND hWnd)
{
    /* WS_EX_NOREDIRECTIONBITMAP: the window has no GDI surface, so there is nothing to paint */
    ValidateRect(hWnd, NULL);
}

static BOOL OnEraseBkgnd(HWND hWnd, HDC hDC)
{
    RECT rcUpdate;
    GetUpdateRect(hWnd, &rcUpdate, FALSE);
    ValidateRect(hWnd, &rcUpdate);
    (void)hDC;
    return TRUE;
}

static void OnGetMinMaxInfo(HWND hWnd, LPMINMAXINFO lpmmi)
{
    RECT          rcCaption;
    RECT          rcButton;
    SIZE          border;
    GUITHREADINFO gti;
    HMONITOR      hMonitor;
    MONITORINFO   mi;

    GetCaptionRect(hWnd, &rcCaption);
    GetWindowBorders(hWnd, &border);
    GetCaptionButtonRect(hWnd, &rcButton, CB_CLOSEBUTTON);
    lpmmi->ptMinTrackSize.x = 5 * RECTWIDTH(rcButton) + 2 * border.cy;
    lpmmi->ptMinTrackSize.y = RECTHEIGHT(rcCaption) + border.cy;

    gti.cbSize = sizeof(gti);
    if (GetGUIThreadInfo(GetCurrentThreadId(), &gti) && gti.hwndMoveSize == hWnd)
    {
        return;
    }
    hMonitor  = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    if (NULL != hMonitor && GetMonitorInfoW(hMonitor, &mi))
    {
        lpmmi->ptMaxPosition.x  = labs(mi.rcMonitor.left - mi.rcWork.left);
        lpmmi->ptMaxPosition.y  = labs(mi.rcMonitor.top - mi.rcWork.top) + 1;
        lpmmi->ptMaxTrackSize.x = RECTWIDTH(mi.rcWork);
        lpmmi->ptMaxTrackSize.y = RECTHEIGHT(mi.rcWork) - 1;
        lpmmi->ptMaxSize        = lpmmi->ptMaxTrackSize;
    }
}

static UINT OnNCCalcSize(HWND hWnd, BOOL fCalcValidRects, NCCALCSIZE_PARAMS* lpcsp)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    RECT              rcCaption;
    RECT              rcContent;
    RECT              rcCurrent;
    BOOL              fCurrentValid;
    BOOL              fSizeChanging;

    if (!fCalcValidRects)
    {
        return 0;
    }
    Trace("WM_NCCALCSIZE", 0);
    /* keep the client area exactly the window rectangle minus the side and bottom borders */
    lpcsp->rgrc[1] = lpcsp->rgrc[2];
    if (IsMaximized(hWnd))
    {
        MONITORINFO mi;
        mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST), &mi))
        {
            lpcsp->rgrc[0] = mi.rcWork;
        }
    }
    else if (GetCaptionRect(hWnd, &rcCaption))
    {
        SIZE border;
        GetWindowBorders(hWnd, &border);
        lpcsp->rgrc[0].left += border.cx;
        lpcsp->rgrc[0].right -= border.cx;
        lpcsp->rgrc[0].bottom -= border.cy;
    }

    /* WM_NCCALCSIZE runs before the new geometry is committed: latch a frame at the current
    ** size so that the compositor never shows future-layout pixels in the old window */
    rcContent     = lpcsp->rgrc[0];
    fCurrentValid = GetWindowContentRect(hWnd, &rcCurrent);
    fSizeChanging = !fCurrentValid || (RECTWIDTH(rcCurrent) != RECTWIDTH(rcContent)) || (RECTHEIGHT(rcCurrent) != RECTHEIGHT(rcContent));
    if (NULL != pw && ContentRectHasArea(&rcContent) && fSizeChanging)
    {
        if (!fCurrentValid || !ContentTargetsCurrentFrame(pw, &rcCurrent))
        {
            RenderGeometryFrame(hWnd, pw, IMMERSIVE_PAINT_RESTART, IMMERSIVE_PAINT_SYNC_0);
        }
        DwmFlush();
        Trace("WM_NCCALCSIZE flushed", 0);
    }
    return 0;
}

static UINT OnNCHitTest(HWND hWnd, int x, int y)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
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
    cursor.y = y + (!fMaximized * border.cy) - !fMaximized;
    GetWindowRect(hWnd, &rcWindow);
    result = NCHIT_LEFT * (cursor.x == CLAMP(cursor.x, rcWindow.left, rcWindow.left + (border.cx + 5) * !fMaximized)) |
             NCHIT_TOP * (cursor.y == CLAMP(cursor.y, rcWindow.top - 2, rcWindow.top + (border.cy + 5) * !fMaximized)) |
             NCHIT_RIGHT * (cursor.x == CLAMP(cursor.x, rcWindow.right - (border.cx + 5) * !fMaximized, rcWindow.right)) |
             NCHIT_BOTTOM * (cursor.y == CLAMP(cursor.y, rcWindow.bottom - 5, rcWindow.bottom + border.cy));
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
            pw->eHotCaptionButton = CB_MAXBUTTON;
            return HTMAXBUTTON;
        }
        if (PtInRect(&rcMin, cursor))
        {
            pw->eHotCaptionButton = CB_MINBUTTON;
            return HTMINBUTTON;
        }
        if (PtInRect(&rcClose, cursor))
        {
            pw->eHotCaptionButton = CB_CLOSEBUTTON;
            return HTCLOSE;
        }
        if (PtInRect(&rcLightDark, cursor))
        {
            pw->eHotCaptionButton = CB_LIGHTDARKBUTTON;
            return HTMENU;
        }
        if (PtInRect(&rcCaption, cursor))
        {
            pw->eHotCaptionButton = CB_CAPTION;
            return HTCAPTION;
        }
        pw->eHotCaptionButton = CB_NONE;
        return HTCLIENT;
    default:
        return HTNOWHERE;
    }
}

static void OnNCPaint(HWND hWnd, HRGN hrgn)
{
    ValidateRgn(hWnd, hrgn);
    FORWARD_WM_NCPAINT(hWnd, hrgn, DefWindowProcW);
}

static BOOL OnNCActivate(HWND hWnd, BOOL fActive, HWND hwndActDeact, BOOL fMinimized)
{
    (void)fActive;
    (void)hwndActDeact;
    (void)fMinimized;
    /* always tell DefWindowProc the window is active so that it never paints an inactive frame */
    return (BOOL)DefWindowProcW(hWnd, WM_NCACTIVATE, TRUE, 0L);
}

static void OnNCMouseMove(HWND hWnd, int x, int y, UINT codeHitTest)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    switch (codeHitTest)
    {
    case HTTOP:
    case HTTOPLEFT:
    case HTTOPRIGHT:
    case HTBOTTOM:
    case HTBOTTOMLEFT:
    case HTBOTTOMRIGHT:
    case HTLEFT:
    case HTRIGHT:
        pw->eHotCaptionButton = CB_NONE;
        return;
    default:
    {
        SIZE  border;
        POINT cursor;
        RECT  rcCaption, rcMin, rcMax, rcClose, rcLightDark;
        GetWindowBorders(hWnd, &border);
        cursor.x = x;
        cursor.y = y + (!IsMaximized(hWnd) * border.cy) - 1;
        GetCaptionRect(hWnd, &rcCaption);
        GetCaptionButtonRect(hWnd, &rcMin, CB_MINBUTTON);
        GetCaptionButtonRect(hWnd, &rcMax, CB_MAXBUTTON);
        GetCaptionButtonRect(hWnd, &rcClose, CB_CLOSEBUTTON);
        GetCaptionButtonRect(hWnd, &rcLightDark, CB_LIGHTDARKBUTTON);
        if (PtInRect(&rcMin, cursor))
        {
            pw->eHotCaptionButton = CB_MINBUTTON;
        }
        else if (PtInRect(&rcMax, cursor))
        {
            pw->eHotCaptionButton = CB_MAXBUTTON;
        }
        else if (PtInRect(&rcClose, cursor))
        {
            pw->eHotCaptionButton = CB_CLOSEBUTTON;
        }
        else if (PtInRect(&rcLightDark, cursor))
        {
            pw->eHotCaptionButton = CB_LIGHTDARKBUTTON;
        }
        else if (PtInRect(&rcCaption, cursor))
        {
            pw->eHotCaptionButton = CB_CAPTION;
        }
        FORWARD_WM_NCMOUSEMOVE(hWnd, x, y, codeHitTest, DefWindowProcW);
    }
    }
}

static void OnNCLButtonDown(HWND hWnd, BOOL fDoubleClick, int x, int y, UINT codeHitTest)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    (void)fDoubleClick;
    if (CB_NONE == pw->eHotCaptionButton || CB_CAPTION == pw->eHotCaptionButton)
    {
        FORWARD_WM_NCLBUTTONDOWN(hWnd, FALSE, x, y, codeHitTest, DefWindowProcW);
    }
}

static void OnNCLButtonUp(HWND hWnd, int x, int y, UINT codeHitTest)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    static ULONGLONG  ullLastToggle;
    ULONGLONG         ullNow;
    (void)codeHitTest;
    switch (pw->eHotCaptionButton)
    {
    case CB_CLOSEBUTTON:
        SendMessageW(hWnd, WM_CLOSE, 0, 0);
        break;
    case CB_MAXBUTTON:
        pw->eHotCaptionButton = CB_NONE;
        PostMessageW(hWnd, WM_SYSCOMMAND, IsMaximized(hWnd) ? (SC_RESTORE | HTMAXBUTTON) : (SC_MAXIMIZE | HTMAXBUTTON), 0);
        break;
    case CB_MINBUTTON:
        pw->eHotCaptionButton = CB_NONE;
        PostMessageW(hWnd, WM_SYSCOMMAND, SC_MINIMIZE | HTMINBUTTON, 0);
        break;
    case CB_LIGHTDARKBUTTON:
        ullNow = GetTickCount64();
        if (ullNow - ullLastToggle > 150)
        {
            pw->fDarkMode  = !pw->fDarkMode;
            pw->fLightMode = !pw->fDarkMode;
            ApplyAppMode(hWnd, pw->fDarkMode);
            ullLastToggle = ullNow;
        }
        break;
    case CB_CAPTION:
        FORWARD_WM_NCLBUTTONUP(hWnd, x, y, HTCAPTION, DefWindowProcW);
        break;
    default:
        break;
    }
}

static void OnNCLButtonDblClk(HWND hWnd, BOOL fDoubleClick, int x, int y, UINT codeHitTest)
{
    (void)fDoubleClick;
    (void)x;
    (void)y;
    if (HTCAPTION == codeHitTest)
    {
        PostMessageW(hWnd, WM_SYSCOMMAND, IsMaximized(hWnd) ? (SC_RESTORE | HTMAXBUTTON) : (SC_MAXIMIZE | HTMAXBUTTON), 0);
    }
}

static void OnNCRButtonUp(HWND hWnd, int x, int y, UINT codeHitTest)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    if (CB_CAPTION == pw->eHotCaptionButton)
    {
        POINT pt;
        HMENU hMenu = GetSystemMenu(hWnd, FALSE);
        UINT  cmd;
        GetCursorPos(&pt);
        SetMenuDefaultItem(hMenu, (UINT)-1, TRUE);
        cmd = (UINT)TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RETURNCMD, pt.x, pt.y, hWnd, NULL);
        if (0 != cmd)
        {
            PostMessageW(hWnd, WM_SYSCOMMAND, cmd, 0);
        }
    }
    else
    {
        FORWARD_WM_NCRBUTTONUP(hWnd, x, y, codeHitTest, DefWindowProcW);
    }
}

static void OnNCMouseLeave(HWND hWnd)
{
    GetState(hWnd)->eHotCaptionButton = CB_NONE;
}

static void OnKeyUp(HWND hWnd, UINT vk, BOOL fDown, int cRepeat, UINT flags)
{
    (void)fDown;
    if (VK_ESCAPE == vk && GetForegroundWindow() == hWnd)
    {
        SendMessageW(hWnd, WM_CLOSE, 0, 0);
    }
    FORWARD_WM_KEYUP(hWnd, vk, cRepeat, flags, DefWindowProcW);
}

static void OnSysCommand(HWND hWnd, UINT cmd, int x, int y)
{
    switch (GET_SC_WPARAM(cmd))
    {
    case SC_CLOSE:
        SendMessageW(hWnd, WM_CLOSE, 0, 0);
        /* falls through */
    case SC_MOVE:
        PostMessageW(hWnd, WM_MOUSEMOVE, 0, 0);
        break;
    default:
        break;
    }
    FORWARD_WM_SYSCOMMAND(hWnd, cmd, x, y, DefWindowProcW);
}

static void OnTimer(HWND hWnd, UINT id)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    RECT              rcContent;
    if ((0 == id || 0x69 == id) && GetWindowContentRect(hWnd, &rcContent) && ContentRectHasArea(&rcContent) && !ContentTargetsCurrentFrame(pw, &rcContent))
    {
        RenderGeometryFrame(hWnd, pw, IMMERSIVE_PAINT_SEQUENCE, IMMERSIVE_PAINT_SYNC_1);
    }
}

static void OnEnterSizeMove(HWND hWnd)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    pw->ri.fMoving       = FALSE;
    pw->ri.fResizing     = FALSE;
    SetTimer(hWnd, 0x69, USER_TIMER_MINIMUM, NULL);
}

static void OnExitSizeMove(HWND hWnd)
{
    KillTimer(hWnd, 0x69);
    OnTimer(hWnd, 0);
}

/* a menu loop (the system menu) is modal like a size loop: the timer keeps the frame current */
static void OnEnterMenuLoop(HWND hWnd, BOOL fIsTrackPopupMenu)
{
    (void)fIsTrackPopupMenu;
    SetTimer(hWnd, 0x69, USER_TIMER_MINIMUM, NULL);
}

static void OnExitMenuLoop(HWND hWnd, BOOL fIsShortcutMenu)
{
    (void)fIsShortcutMenu;
    KillTimer(hWnd, 0x69);
    OnTimer(hWnd, 0);
}

static void OnMouseMove(HWND hWnd, int x, int y, UINT keyFlags)
{
    (void)hWnd;
    (void)x;
    (void)y;
    (void)keyFlags;
}

static void OnMouseLeave(HWND hWnd)
{
    GetState(hWnd)->eHotCaptionButton = CB_NONE;
}

static void OnSysColorChange(HWND hWnd)
{
    FORWARD_WM_SYSCOLORCHANGE(hWnd, DefWindowProcW);
}

static BOOL OnSetCursor(HWND hWnd, HWND hwndCursor, UINT codeHitTest, UINT msg)
{
    return FORWARD_WM_SETCURSOR(hWnd, hwndCursor, codeHitTest, msg, DefWindowProcW);
}

static void OnParentNotify(HWND hWnd, UINT msg, HWND hwndChild, int idChild)
{
    FORWARD_WM_PARENTNOTIFY(hWnd, msg, hwndChild, idChild, DefWindowProcW);
}

static BOOL OnMoving(HWND hWnd, LPRECT lprc)
{
    GetState(hWnd)->ri.fMoving = TRUE;
    return (BOOL)DefWindowProcW(hWnd, WM_MOVING, 0, (LPARAM)lprc);
}

static BOOL OnSizing(HWND hWnd, UINT nEdge, LPRECT lprc)
{
    GetState(hWnd)->ri.fResizing = TRUE;
    return (BOOL)DefWindowProcW(hWnd, WM_SIZING, nEdge, (LPARAM)lprc);
}

static void OnWindowPosChanged(HWND hWnd, const LPWINDOWPOS lpwpos)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    RECT              rcContent;
    if (NULL == pw || NULL == lpwpos || ((SWP_NOMOVE | SWP_NOSIZE) == (lpwpos->flags & (SWP_NOMOVE | SWP_NOSIZE))) || !GetWindowContentRect(hWnd, &rcContent) || !ContentRectHasArea(&rcContent))
    {
        return;
    }
    /* the geometry is committed: a size mismatch needs a frame, a pure move carries the visual */
    Trace("WM_WINDOWPOSCHANGED", 0);
    if (!ContentSizeMatches(pw, &rcContent))
    {
        RenderGeometryFrame(hWnd, pw, IMMERSIVE_PAINT_RESTART, IMMERSIVE_PAINT_SYNC_1);
    }
    Trace("WM_WINDOWPOSCHANGED done", 0);
}

static void OnDwmNcRenderingChanged(HWND hWnd, BOOL fEnabled)
{
    if (fEnabled)
    {
        const BOOL fAllowNcPaint = FALSE;
        const BOOL fPassive      = TRUE;
        DwmSetWindowAttribute(hWnd, DWMWA_ALLOW_NCPAINT, &fAllowNcPaint, sizeof(fAllowNcPaint));
        DwmSetWindowAttribute(hWnd, DWMWA_PASSIVE_UPDATE_MODE, &fPassive, sizeof(fPassive));
    }
    DefWindowProcW(hWnd, WM_DWMNCRENDERINGCHANGED, (WPARAM)fEnabled, 0);
}

/* ------------------------------------------------------------------------------------------ 6 */

static void DrawGlyph(LPIMMERSIVEWINDOW pw, IDWriteFontFace* pFace, UINT16 glyph, FLOAT emSize, FLOAT x, FLOAT y, ID2D1Brush* pBrush, DWRITE_MEASURING_MODE mode)
{
    DWRITE_GLYPH_OFFSET offset = {0.0f, 0.0f};
    D2D1_POINT_2F       origin;
    cd2dInitPoint2F(&origin, x, y);
    cd2dDeviceContextDrawGlyphRun(pw->pD2D1DeviceContext, origin, pFace, emSize, 1, &glyph, NULL, &offset, FALSE, 0, NULL, pBrush, mode);
}

static void FillButton(LPIMMERSIVEWINDOW pw, const RECT* prc, ID2D1SolidColorBrush* pBrush, FLOAT dx)
{
    ID2D1RenderTarget* target = (ID2D1RenderTarget*)pw->pD2D1DeviceContext;
    cd2dRenderTargetDrawRectangle(target, (FLOAT)prc->left + dx, (FLOAT)prc->top, (FLOAT)prc->right + dx, (FLOAT)prc->bottom, (ID2D1Brush*)pBrush, 1.0f, (ID2D1StrokeStyle*)pw->pStrokeStyle);
    cd2dRenderTargetFillRectangle(target, (FLOAT)prc->left + dx, (FLOAT)prc->top, (FLOAT)prc->right + dx, (FLOAT)prc->bottom, (ID2D1Brush*)pBrush);
}

static void ToLogical(RECT* prc)
{
    prc->right  = RECTWIDTH(*prc);
    prc->bottom = RECTHEIGHT(*prc);
    prc->left   = 0;
    prc->top    = 0;
}

static void PaintCaption(HWND hWnd, LPIMMERSIVEWINDOW pw)
{
    const UINT            dpi         = GetDpiForWindow(hWnd);
    const int             buttonWidth = MulDiv(CAPTION_BUTTON_WIDTH_DIPS, (int)dpi, 96);
    const FLOAT           fontSize    = (FLOAT)MulDiv(10, (int)dpi, 96);
    const FLOAT           scale       = 1.5f;
    const int             inset       = RoundToInt(0.5f * fontSize);
    const int             inset2      = RoundToInt(0.5f * fontSize * scale);
    const BOOL            fMaximized  = IsMaximized(hWnd);
    ID2D1SolidColorBrush* text        = pw->fDarkMode ? pw->pLightBrush : pw->pDarkBrush;
    ID2D1SolidColorBrush* fill        = pw->pColorizationBrush;
    RECT                  rcCaption, rcClose, rcMax, rcMin, rcLightDark;
    LONG                  captionWidth;
    SIZE                  pad;

    GetCaptionRect(hWnd, &rcCaption);
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
        FillButton(pw, &rcClose, pw->pRedBrush, 0.5f);
    }
    DrawGlyph(pw,
              pw->pFontFace,
              pw->glyphs[GLYPH_CLOSE],
              fontSize * 1.25f,
              rcClose.left + 0.5f * (RECTWIDTH(rcClose) - 1) - inset,
              rcClose.top + 0.5f * RECTHEIGHT(rcClose) + inset2 - 1,
              (ID2D1Brush*)text,
              DWRITE_MEASURING_MODE_GDI_NATURAL);
    DrawGlyph(pw,
              pw->pFontFace,
              pw->glyphs[GLYPH_CLOSE],
              fontSize * 1.25f,
              rcClose.left + 0.5f * (RECTWIDTH(rcClose) - 1) - inset,
              rcClose.top + 0.5f * RECTHEIGHT(rcClose) + inset2 - 1,
              (ID2D1Brush*)text,
              DWRITE_MEASURING_MODE_GDI_NATURAL);

    /* maximize / restore */
    ToLogical(&rcMax);
    OffsetRect(&rcMax, captionWidth - 2 * RECTWIDTH(rcMax) - 2, 0);
    if (CB_MAXBUTTON == pw->eHotCaptionButton)
    {
        FillButton(pw, &rcMax, fill, 0.0f);
    }
    pad.cx = MulDiv(18, (int)dpi, 96);
    pad.cy = MulDiv(10, (int)dpi, 96);
    InflateRect(&rcMax, -pad.cx, -pad.cy);
    rcMax.bottom -= 1;
    rcMax.right -= 1;
    rcMax.left -= 1;
    DrawGlyph(pw,
              pw->pFontFace,
              pw->glyphs[fMaximized ? GLYPH_RESTORE : GLYPH_MAXIMIZE],
              fontSize,
              (FLOAT)rcMax.left,
              rcMax.top + 0.5f * RECTHEIGHT(rcMax) + inset,
              (ID2D1Brush*)text,
              DWRITE_MEASURING_MODE_NATURAL);

    /* minimize */
    ToLogical(&rcMin);
    OffsetRect(&rcMin, captionWidth - 3 * RECTWIDTH(rcMin) - 3, 0);
    if (CB_MINBUTTON == pw->eHotCaptionButton)
    {
        FillButton(pw, &rcMin, fill, 0.0f);
    }
    DrawGlyph(pw,
              pw->pFontFace,
              pw->glyphs[GLYPH_MINIMIZE],
              fontSize,
              rcMin.left + 0.5f * RECTWIDTH(rcMin) - inset,
              rcMin.top + 0.5f * RECTHEIGHT(rcMin) + inset,
              (ID2D1Brush*)text,
              DWRITE_MEASURING_MODE_NATURAL);

    /* light / dark toggle */
    ToLogical(&rcLightDark);
    OffsetRect(&rcLightDark, captionWidth - 4 * RECTWIDTH(rcLightDark) - 2, 0);
    if (CB_LIGHTDARKBUTTON == pw->eHotCaptionButton)
    {
        FillButton(pw, &rcLightDark, fill, 0.0f);
    }
    DrawGlyph(pw,
              pw->pFontFace,
              pw->glyphs[pw->fLightMode ? GLYPH_DARKMODE3 : GLYPH_LIGHTMODE1],
              fontSize * scale,
              rcLightDark.left + 0.5f * RECTWIDTH(rcLightDark) - inset2,
              rcLightDark.top + 0.5f * RECTHEIGHT(rcLightDark) + inset2,
              (ID2D1Brush*)text,
              DWRITE_MEASURING_MODE_NATURAL);
}

LPVOID WINAPI BeginImmersivePaint(HWND hWnd)
{
    LPIMMERSIVEWINDOW  pw = GetState(hWnd);
    ID2D1RenderTarget* target;

    if (NULL == pw || NULL == pw->pD3D11DeviceContext)
    {
        return NULL;
    }
    target = (ID2D1RenderTarget*)pw->pD2D1DeviceContext;
    if (pw->pProc)
    {
        pw->pProc(hWnd);
    }
    cd2dRenderTargetBeginDraw(target);
    cd2dRenderTargetSetIdentityTransform(target);
    cd2dRenderTargetSetTextAntialiasMode(target, D2D1_TEXT_ANTIALIAS_MODE_CLEARTYPE);
    cd2dRenderTargetSetAntialiasMode(target, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    PaintCaption(hWnd, pw);
    Check(SUCCEEDED(cd2dRenderTargetEndDraw(target, NULL, NULL)), "EndDraw");
    return NULL;
}

BOOL WINAPI EndImmersivePaint(HWND hWnd, BOOL fRestart, BOOL fVsync)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    IDXGISwapChain*   sc;
    HRESULT           hr;

    if (NULL == pw || NULL == pw->pD3D11DeviceContext)
    {
        return FALSE;
    }
    /* Exactly one successful present for the buffer we painted. FLIP_DISCARD
    ** does not preserve back-buffer contents for a second presentation. Do not
    ** drop a geometry frame with DO_NOT_WAIT and then stamp it as current. */
    sc = (IDXGISwapChain*)pw->pDXGISwapChain2;
    (void)fVsync;
    hr = cdxgiSwapChainPresent(sc, 1, fRestart ? DXGI_PRESENT_RESTART : 0);
    Trace("present", hr);
    return S_OK == hr ? StampPresentedContent(hWnd, pw) : FALSE;
}

BOOL SetDevicePreDrawCallback(HWND hWnd, DEVICEDRAWPROC pProc)
{
    GetState(hWnd)->pPreProc = pProc;
    return TRUE;
}

BOOL SetDeviceDrawCallback(HWND hWnd, DEVICEDRAWPROC pProc)
{
    GetState(hWnd)->pProc = pProc;
    return TRUE;
}

BOOL GetDeviceAndSwapchain(HWND hWnd, void** device, void** deviceContext, void** swapchain, void** d2dDeviceContext, void** dxgiSwapChain)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    if (NULL == pw || NULL == pw->pD3D11Device)
    {
        return FALSE;
    }
    *device           = pw->pD3D11Device;
    *deviceContext    = pw->pD3D11DeviceContext;
    *swapchain        = pw->pDXGISwapChain2;
    *d2dDeviceContext = pw->pD2D1DeviceContext;
    *dxgiSwapChain    = pw->pDXGISwapChain2;
    return TRUE;
}

BOOL GetResizeInfo(HWND hWnd, LPRESIZEINFO lpri)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    if (NULL == pw || NULL == lpri)
    {
        return FALSE;
    }
    *lpri = pw->ri;
    return TRUE;
}

VOID WINAPI WaitForNextFrameResource(HWND hWnd)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    if (NULL != pw && NULL != pw->hFrameLatencyWaitableObject)
    {
        WaitForSingleObject(pw->hFrameLatencyWaitableObject, 0);
    }
}

BOOL CALLBACK WaitForVerticalBlank(HWND hWnd)
{
    static D3DKMT_WAITFORVERTICALBLANKEVENT vbe;
    D3DKMT_GETSCANLINE                      scan;

    if (0 == vbe.hAdapter)
    {
        D3DKMT_OPENADAPTERFROMHDC open;
        open.hDc = GetDC(hWnd);
        if (0 != D3DKMTOpenAdapterFromHdc(&open))
        {
            ReleaseDC(hWnd, open.hDc);
            return FALSE;
        }
        ReleaseDC(hWnd, open.hDc);
        vbe.hAdapter      = open.hAdapter;
        vbe.VidPnSourceId = open.VidPnSourceId;
        vbe.hDevice       = 0;
    }
    if (0 != D3DKMTWaitForVerticalBlankEvent(&vbe))
    {
        return FALSE;
    }
    scan.hAdapter      = vbe.hAdapter;
    scan.VidPnSourceId = vbe.VidPnSourceId;
    do
    {
        scan.InVerticalBlank = FALSE;
        scan.ScanLine        = 0;
        if (0 != D3DKMTGetScanLine(&scan))
        {
            return FALSE;
        }
    } while (scan.InVerticalBlank);
    return TRUE;
}

/* ------------------------------------------------------------------------------------------ 7 */

#define HANDLE_WM_NCMOUSELEAVE(hwnd, wParam, lParam, fn)          ((fn)((hwnd)), 0L)
#define HANDLE_WM_ENTERSIZEMOVE(hwnd, wParam, lParam, fn)         ((fn)((hwnd)), 0L)
#define HANDLE_WM_EXITSIZEMOVE(hwnd, wParam, lParam, fn)          ((fn)((hwnd)), 0L)
#define HANDLE_WM_MOVING(hwnd, wParam, lParam, fn)                (LRESULT)((fn)((hwnd), (LPRECT)(lParam)))
#define HANDLE_WM_SIZING(hwnd, wParam, lParam, fn)                (LRESULT)((fn)((hwnd), (UINT)(wParam), (LPRECT)(lParam)))
#define HANDLE_WM_DWMNCRENDERINGCHANGED(hwnd, wParam, lParam, fn) ((fn)((hwnd), (BOOL)(wParam)), 0L)
#define HANDLE_WM_INITD2D(hwnd, wParam, lParam, fn)               (LRESULT)((fn)((hwnd), GetState(hwnd)))
#define HANDLE_WM_MOUSELEAVE(hwnd, wParam, lParam, fn)            ((fn)((hwnd)), 0L)
#define HANDLE_WM_ENTERMENULOOP(hwnd, wParam, lParam, fn)         ((fn)((hwnd), (BOOL)(wParam)), 0L)
#define HANDLE_WM_EXITMENULOOP(hwnd, wParam, lParam, fn)          ((fn)((hwnd), (BOOL)(wParam)), 0L)

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
        HANDLE_MSG(hWnd, WM_SIZE, OnSize);
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
        HANDLE_MSG(hWnd, WM_SYSCOMMAND, OnSysCommand);
        HANDLE_MSG(hWnd, WM_TIMER, OnTimer);
        HANDLE_MSG(hWnd, WM_MOUSEMOVE, OnMouseMove);
        HANDLE_MSG(hWnd, WM_PARENTNOTIFY, OnParentNotify);
        HANDLE_MSG(hWnd, WM_ENTERMENULOOP, OnEnterMenuLoop);
        HANDLE_MSG(hWnd, WM_EXITMENULOOP, OnExitMenuLoop);
        HANDLE_MSG(hWnd, WM_MOUSELEAVE, OnMouseLeave);
        HANDLE_MSG(hWnd, WM_ENTERSIZEMOVE, OnEnterSizeMove);
        HANDLE_MSG(hWnd, WM_EXITSIZEMOVE, OnExitSizeMove);
        HANDLE_MSG(hWnd, WM_DWMNCRENDERINGCHANGED, OnDwmNcRenderingChanged);
        HANDLE_MSG(hWnd, WM_INITD2D, OnInitD2D);
    default:
        return DefWindowProcW(hWnd, uMsg, wParam, lParam);
    }
}

VOID WINAPI InitImmersiveControls(VOID)
{
    WNDCLASSW wc;
    WCHAR     tracePath[MAX_PATH];
    if (GetEnvironmentVariableW(L"IMMERSIVE_TRACE", tracePath, MAX_PATH))
    {
        g_hTrace      = CreateFileW(tracePath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        g_traceOrigin = Now();
    }
    ResolvePrivateThemeApis();
    ZeroMemory(&wc, sizeof(wc));
    wc.style         = CS_OWNDC | CS_BYTEALIGNWINDOW | CS_BYTEALIGNCLIENT;
    wc.lpfnWndProc   = DefImmersiveProc;
    wc.cbWndExtra    = sizeof(LONG_PTR);
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = IMMERSIVE_CLASSW;
    Check(0 != RegisterClassW(&wc), "RegisterClassW");
    if (g_SetPreferredAppMode)
    {
        g_SetPreferredAppMode(PAM_ALLOWDARK);
    }
}
