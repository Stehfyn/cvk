/* immersive_window.c - the ImmersiveWindow32 control on the cdx libraries. See immersive_window.h.
**
** Structure, in the order of the original BorderlessWindow32/ImmersiveWindow.c:
**   1. private state and the per-window record
**   2. device and composition setup (OnInitD2D) and teardown
**   3. caption geometry and composition layout
**   4. dark and light mode through the uxtheme private ordinals
**   5. the message handlers
**   6. retained caption and video surface updates
**   7. the window procedure */

#include "immersive_window.h"
#include "presentation.h"

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
    D2D1_FACTORY_OPTIONS options = {0};
    IDXGIAdapter* adapter;
    IDXGIFactory2* factory;
    IDXGISwapChain1* swapChain;
    IDXGISurface* surface;
    DXGI_SAMPLE_DESC sample = {1, 0};
    RECT client;

    if (NULL != pw->pD3D11Device)
        return 0;
    Check(SUCCEEDED(cdx11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, deviceFlags, levels, ARRAYSIZE(levels), D3D11_SDK_VERSION,
                                    &pw->pD3D11Device, &level, &pw->pD3D11DeviceContext)), "D3D11CreateDevice");
#ifndef NDEBUG
    cdx11DeviceGetDebug(pw->pD3D11Device, &pw->pD3D11Debug);
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
    Check(SUCCEEDED(cdxgiUnknownQueryInterfaceAsDevice1((IUnknown*)pw->pD3D11Device, &pw->pDXGIDevice1)), "IDXGIDevice1");
    cd2dCreateFactoryAsFactory2(D2D1_FACTORY_TYPE_SINGLE_THREADED, &options, &pw->pD2D1Factory2);
    cd2dFactory2CreateDevice(pw->pD2D1Factory2, (IDXGIDevice*)pw->pDXGIDevice1, &pw->pD2D1Device1);
    cd2dSetupDefaultDeviceContext((ID2D1Device*)pw->pD2D1Device1, &pw->pD2D1DeviceContext);

    GetClientRect(hWnd, &client);
    pw->szClient.cx = max(1, client.right);
    pw->szClient.cy = max(1, client.bottom);
    cdxgiDeviceGetAdapter((IDXGIDevice*)pw->pDXGIDevice1, &adapter);
    cdxgiObjectGetParentAsFactory2((IDXGIObject*)adapter, &factory);
    cdxgiFactoryCreateSwapChainForComposition(factory, (IUnknown*)pw->pD3D11Device,
        pw->szClient.cx, pw->szClient.cy, DXGI_FORMAT_B8G8R8A8_UNORM, FALSE, sample,
        DXGI_USAGE_RENDER_TARGET_OUTPUT, 2, DXGI_SCALING_STRETCH, DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL,
        DXGI_ALPHA_MODE_IGNORE, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT, NULL, &swapChain);
    Check(SUCCEEDED(cdxgiUnknownQueryInterfaceAsSwapChain2((IUnknown*)swapChain, &pw->pSwapChain)), "composition swap chain");
    cdxgiSwapChainSetMaximumFrameLatencyOne(pw->pSwapChain);
    pw->hFrameLatency = cdxgiSwapChainGetFrameLatencyWaitableObject(pw->pSwapChain);
    Check(NULL != pw->hFrameLatency, "composition frame latency handle");
    cdxgiFactoryMakeWindowAssociation((IDXGIFactory*)factory, hWnd, DXGI_MWA_NO_ALT_ENTER | DXGI_MWA_NO_WINDOW_CHANGES);
    cdxgiUnknownRelease((IUnknown*)swapChain);
    cdxgiUnknownRelease((IUnknown*)factory);
    cdxgiUnknownRelease((IUnknown*)adapter);
    cdxgiSwapChainGetBufferAsSurface((IDXGISwapChain*)pw->pSwapChain, 0, &surface);
    cd2dDeviceContextCreateTargetBitmapFromSurface(pw->pD2D1DeviceContext, surface, DXGI_FORMAT_B8G8R8A8_UNORM, &pw->pTargetBitmap);
    cdxgiUnknownRelease((IUnknown*)surface);
    cdcompCreateDevice((IDXGIDevice*)pw->pDXGIDevice1, &pw->pDCompDevice);
    cdcompDeviceSetupHwndContent(pw->pDCompDevice, hWnd, TRUE, (IUnknown*)pw->pSwapChain, &pw->pDCompTarget, &pw->pDCompVisual);
    pw->fFrameDirty = TRUE;
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
    RELEASE(pw->pTargetBitmap);
    RELEASE(pw->pVideoBitmap);
    RELEASE(pw->pVideoTexture);
    RELEASE(pw->pSwapChain);
    if (pw->hFrameLatency)
    {
        CloseHandle(pw->hFrameLatency);
        pw->hFrameLatency = NULL;
    }
    RELEASE(pw->pD2D1DeviceContext);
    RELEASE(pw->pD2D1Device1);
    RELEASE(pw->pD2D1Factory2);
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
    POINT      origin = {0, 0};

    if (!GetClientRect(hWnd, lprc) || !ClientToScreen(hWnd, &origin))
    {
        return FALSE;
    }
    OffsetRect(lprc, origin.x, origin.y);
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

static void RenderFrame(HWND hWnd, LPIMMERSIVEWINDOW pw, LONG width, LONG height);

static void InvalidateCaption(HWND hWnd)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    if (NULL != pw)
    {
        pw->fFrameDirty = TRUE;
        InvalidateRect(hWnd, NULL, FALSE);
        /* WM_PAINT is normally low priority. Submit this input state now,
        ** including while a native menu or sizing loop owns message dispatch. */
        UpdateWindow(hWnd);
    }
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
}

/* ------------------------------------------------------------------------------------------ 5 */

static BOOL OnNCCreate(HWND hWnd, LPCREATESTRUCT lpcs)
{
    LPIMMERSIVEWINDOW pw;
    const enum DWMNCRENDERINGPOLICY ncPolicy = DWMNCRP_DISABLED;
    POINT             pt;
    MONITORINFO       mi;

    EnableNonClientDpiScaling(hWnd);

    pw = (LPIMMERSIVEWINDOW)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(IMMERSIVEWINDOW));
    Check(NULL != pw, "HeapAlloc");
    SetWindowLongPtr(hWnd, 0, (LONG_PTR)pw);
    SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)lpcs->lpCreateParams);
    Check(SUCCEEDED(DwmSetWindowAttribute(hWnd, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy))), "disable native frame");

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
    SetWindowPos(hWnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static BOOL OnCreate(HWND hWnd, LPCREATESTRUCT lpcs)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    pw->fDarkMode        = TRUE;
    /* The caption is rendered by PaintCaption. Disabling DWM nonclient
    ** rendering alone leaves the themed nonclient implementation active. */
    Check(SUCCEEDED(SetWindowTheme(hWnd, L"", L"")), "disable native window theme");
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

static void OnActivate(HWND hWnd, UINT state, HWND hwndActDeact, BOOL fMinimized)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    g_fActive            = (WA_INACTIVE != state);
    if (!fMinimized)
    {
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
    PAINTSTRUCT paint;
    RECT client;
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    BeginPaint(hWnd, &paint);
    if (NULL != pw && NULL != pw->pDCompDevice && !IsIconic(hWnd))
    {
        BOOL valid;
        cdcompDeviceCheckDeviceState(pw->pDCompDevice, &valid);
        Check(valid, "DirectComposition device state");
        GetClientRect(hWnd, &client);
        RenderFrame(hWnd, pw, client.right, client.bottom);
    }
    EndPaint(hWnd, &paint);
}

static BOOL OnEraseBkgnd(HWND hWnd, HDC hDC)
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
    MONITORINFO   mi;

    GetCaptionRect(hWnd, &rcCaption);
    GetWindowBorders(hWnd, &border);
    GetCaptionButtonRect(hWnd, &rcButton, CB_CLOSEBUTTON);
    lpmmi->ptMinTrackSize.x = 5 * RECTWIDTH(rcButton) + 2 * border.cy;
    lpmmi->ptMinTrackSize.y = RECTHEIGHT(rcCaption) + border.cy;

    hMonitor  = MonitorFromWindow(hWnd, MONITOR_DEFAULTTONEAREST);
    mi.cbSize = sizeof(mi);
    if (NULL != hMonitor && GetMonitorInfoW(hMonitor, &mi))
    {
        lpmmi->ptMaxPosition.x  = mi.rcWork.left - mi.rcMonitor.left;
        lpmmi->ptMaxPosition.y  = mi.rcWork.top - mi.rcMonitor.top;
        lpmmi->ptMaxTrackSize.x = RECTWIDTH(mi.rcWork);
        lpmmi->ptMaxTrackSize.y = RECTHEIGHT(mi.rcWork);
        lpmmi->ptMaxSize        = lpmmi->ptMaxTrackSize;
    }
}

static UINT OnNCCalcSize(HWND hWnd, BOOL fCalcValidRects, NCCALCSIZE_PARAMS* lpcsp)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    /* The proposed window rectangle is the complete client rectangle. Produce
    ** its video and caption together before Windows changes the HWND origin. */
    if (fCalcValidRects && NULL != pw && NULL != pw->pSwapChain && !IsIconic(hWnd))
    {
        const RECT* proposed = &lpcsp->rgrc[0];
        LONG width = proposed->right - proposed->left;
        LONG height = proposed->bottom - proposed->top;
        if (pw->szClient.cx != width || pw->szClient.cy != height)
            RenderFrame(hWnd, pw, width, height);
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
        if (x < rcWindow.left + border.cx) result |= NCHIT_LEFT;
        if (x >= rcWindow.right - border.cx) result |= NCHIT_RIGHT;
        if (y < rcWindow.top + border.cy) result |= NCHIT_TOP;
        if (y >= rcWindow.bottom - border.cy) result |= NCHIT_BOTTOM;
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

static void OnNCPaint(HWND hWnd, HRGN hrgn)
{
    (void)hWnd;
    (void)hrgn;
}

static BOOL OnNCActivate(HWND hWnd, BOOL fActive, HWND hwndActDeact, BOOL fMinimized)
{
    (void)hwndActDeact;
    (void)fMinimized;
    return (BOOL)DefWindowProcW(hWnd, WM_NCACTIVATE, fActive, -1);
}

static CAPTIONBUTTON CaptionButtonFromHitTest(UINT codeHitTest)
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

static void OnNCMouseMove(HWND hWnd, int x, int y, UINT codeHitTest)
{
    TRACKMOUSEEVENT track = {sizeof(track), TME_LEAVE | TME_NONCLIENT, hWnd, 0};
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    CAPTIONBUTTON button = CaptionButtonFromHitTest(codeHitTest);
    if (pw->eHotCaptionButton != button)
    {
        pw->eHotCaptionButton = button;
        InvalidateCaption(hWnd);
    }
    TrackMouseEvent(&track);
    FORWARD_WM_NCMOUSEMOVE(hWnd, x, y, codeHitTest, DefWindowProcW);
}

static void OnNCLButtonDown(HWND hWnd, BOOL fDoubleClick, int x, int y, UINT codeHitTest)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    pw->ePressedCaptionButton = CaptionButtonFromHitTest(codeHitTest);
    if (CB_NONE == pw->ePressedCaptionButton || CB_CAPTION == pw->ePressedCaptionButton)
    {
        /* DefWindowProc owns native move/size capture. A previous hover must
        ** never prevent the current edge hit from reaching it. */
        FORWARD_WM_NCLBUTTONDOWN(hWnd, fDoubleClick, x, y, codeHitTest, DefWindowProcW);
    }
}

static void OnNCLButtonUp(HWND hWnd, int x, int y, UINT codeHitTest)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    static ULONGLONG  ullLastToggle;
    ULONGLONG         ullNow;
    CAPTIONBUTTON pressed = pw->ePressedCaptionButton;
    pw->ePressedCaptionButton = CB_NONE;
    if (pressed != CaptionButtonFromHitTest(codeHitTest))
    {
        return;
    }
    switch (pressed)
    {
    case CB_CLOSEBUTTON:
        SendMessageW(hWnd, WM_CLOSE, 0, 0);
        break;
    case CB_MAXBUTTON:
        pw->eHotCaptionButton = CB_NONE;
        InvalidateCaption(hWnd);
        PostMessageW(hWnd, WM_SYSCOMMAND, IsMaximized(hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
        break;
    case CB_MINBUTTON:
        pw->eHotCaptionButton = CB_NONE;
        InvalidateCaption(hWnd);
        PostMessageW(hWnd, WM_SYSCOMMAND, SC_MINIMIZE, 0);
        break;
    case CB_LIGHTDARKBUTTON:
        ullNow = GetTickCount64();
        if (ullNow - ullLastToggle > 150)
        {
            pw->fDarkMode  = !pw->fDarkMode;
            pw->fLightMode = !pw->fDarkMode;
            ApplyAppMode(hWnd, pw->fDarkMode);
            InvalidateCaption(hWnd);
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
        PostMessageW(hWnd, WM_SYSCOMMAND, IsMaximized(hWnd) ? SC_RESTORE : SC_MAXIMIZE, 0);
    }
}

static void OnNCRButtonUp(HWND hWnd, int x, int y, UINT codeHitTest)
{
    if (HTCAPTION == codeHitTest)
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
    if (GetState(hWnd)->eHotCaptionButton != CB_NONE)
    {
        GetState(hWnd)->eHotCaptionButton = CB_NONE;
        InvalidateCaption(hWnd);
    }
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

static void OnEnterSizeMove(HWND hWnd)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    pw->ri.fMoving = FALSE;
    pw->ri.fResizing = FALSE;
}

static void OnExitSizeMove(HWND hWnd)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    pw->ri.fMoving = FALSE;
    pw->ri.fResizing = FALSE;
}

static void OnMouseMove(HWND hWnd, int x, int y, UINT keyFlags)
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

static void OnMouseLeave(HWND hWnd)
{
    if (GetState(hWnd)->eHotCaptionButton != CB_NONE)
    {
        GetState(hWnd)->eHotCaptionButton = CB_NONE;
        InvalidateCaption(hWnd);
    }
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
    if (NULL != pw && NULL != pw->pSwapChain && !IsIconic(hWnd))
    {
        RECT client;
        GetClientRect(hWnd, &client);
        if (pw->fFrameDirty || pw->szClient.cx != client.right || pw->szClient.cy != client.bottom ||
            pw->captionDpi != GetDpiForWindow(hWnd) || pw->fCaptionMaximized != IsMaximized(hWnd))
            RenderFrame(hWnd, pw, client.right, client.bottom);
        ValidateRect(hWnd, NULL);
    }
    FORWARD_WM_WINDOWPOSCHANGED(hWnd, lpwpos, DefWindowProcW);
}

static void OnDwmNcRenderingChanged(HWND hWnd, BOOL fEnabled)
{
    if (fEnabled)
    {
        const enum DWMNCRENDERINGPOLICY ncPolicy = DWMNCRP_DISABLED;
        Check(SUCCEEDED(DwmSetWindowAttribute(hWnd, DWMWA_NCRENDERING_POLICY, &ncPolicy, sizeof(ncPolicy))), "disable native frame");
    }
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

static void RenderFrame(HWND hWnd, LPIMMERSIVEWINDOW pw, LONG width, LONG height)
{
    ID2D1RenderTarget* target = (ID2D1RenderTarget*)pw->pD2D1DeviceContext;
    D2D1_RECT_F destination = {0, 0, (FLOAT)width, (FLOAT)height};
    HRESULT result;
    if (width <= 0 || height <= 0 || pw->fRendering || NULL == pw->pSwapChain)
        return;
    pw->fRendering = TRUE;
    Check(WAIT_OBJECT_0 == WaitForSingleObject(pw->hFrameLatency, INFINITE), "composition frame latency wait");
    if (pw->szClient.cx != width || pw->szClient.cy != height)
    {
        IDXGISurface* surface;
        /* ResizeBuffers requires every direct AND indirect back-buffer reference
        ** to be released, including the D2D context's current target. */
        cd2dDeviceContextClearTarget(pw->pD2D1DeviceContext);
        cdxgiUnknownRelease((IUnknown*)pw->pTargetBitmap);
        pw->pTargetBitmap = NULL;
        cdxgiSwapChainResizeBuffers((IDXGISwapChain*)pw->pSwapChain, 0, (UINT)width, (UINT)height,
            DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT);
        cdxgiSwapChainGetBufferAsSurface((IDXGISwapChain*)pw->pSwapChain, 0, &surface);
        cd2dDeviceContextCreateTargetBitmapFromSurface(pw->pD2D1DeviceContext, surface, DXGI_FORMAT_B8G8R8A8_UNORM, &pw->pTargetBitmap);
        cdxgiUnknownRelease((IUnknown*)surface);
        pw->szClient.cx = width;
        pw->szClient.cy = height;
    }
    cd2dDeviceContextSetTarget(pw->pD2D1DeviceContext, (ID2D1Image*)pw->pTargetBitmap);
    cd2dRenderTargetSetDpi(target, 96, 96);
    cd2dRenderTargetBeginDraw(target);
    cd2dRenderTargetSetIdentityTransform(target);
    cd2dRenderTargetClearBlack(target);
    if (pw->fHaveVideo)
        cd2dDeviceContextDrawBitmap(pw->pD2D1DeviceContext, (ID2D1Bitmap*)pw->pVideoBitmap, &destination,
            1.0f, D2D1_INTERPOLATION_MODE_LINEAR, NULL, NULL);
    cd2dRenderTargetSetTextAntialiasMode(target, D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    PaintCaption(hWnd, pw);
    result = cd2dRenderTargetEndDraw(target, NULL, NULL);
    Check(SUCCEEDED(result), "frame EndDraw");
    cd2dDeviceContextClearTarget(pw->pD2D1DeviceContext);
    result = cdxgiSwapChainPresent((IDXGISwapChain*)pw->pSwapChain, 1, 0);
    Check(SUCCEEDED(result), "composition Present");
    pw->captionDpi = GetDpiForWindow(hWnd);
    pw->fCaptionMaximized = IsMaximized(hWnd);
    pw->fFrameDirty = FALSE;
    pw->fRendering = FALSE;
    Trace("composition present", result);
}

BOOL GetImmersiveDevice(HWND hWnd, ID3D11Device** device, ID3D11DeviceContext** context)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    if (NULL == pw || NULL == pw->pD3D11Device)
        return FALSE;
    *device = pw->pD3D11Device;
    *context = pw->pD3D11DeviceContext;
    return TRUE;
}

void SetImmersiveVideo(HWND hWnd, UINT width, UINT height, IMMERSIVEVIDEOPROC draw)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    DXGI_SAMPLE_DESC sample = {1, 0};
    IDXGISurface* surface;
    D2D1_PIXEL_FORMAT format = {DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE};
    D2D1_BITMAP_PROPERTIES1 properties;
    Check(width > 0 && height > 0 && NULL != draw, "video dimensions and callback");
    if (pw->pVideoBitmap)
        cdxgiUnknownRelease((IUnknown*)pw->pVideoBitmap);
    if (pw->pVideoTexture)
        cdxgiUnknownRelease((IUnknown*)pw->pVideoTexture);
    cdx11DeviceCreateTexture2D(pw->pD3D11Device, width, height, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, sample,
        D3D11_USAGE_DEFAULT, D3D11_BIND_SHADER_RESOURCE, 0, 0, NULL, &pw->pVideoTexture);
    Check(SUCCEEDED(cdxgiUnknownQueryInterface((IUnknown*)pw->pVideoTexture, &IID_IDXGISurface, (void**)&surface)), "video bitmap surface");
    cd2dInitBitmapProperties1(&properties, format, 96, 96, D2D1_BITMAP_OPTIONS_NONE, NULL);
    cd2dDeviceContextCreateBitmapFromDxgiSurface(pw->pD2D1DeviceContext, surface, &properties, &pw->pVideoBitmap);
    cdxgiUnknownRelease((IUnknown*)surface);
    pw->szVideo.cx = (LONG)width;
    pw->szVideo.cy = (LONG)height;
    pw->pVideoProc = draw;
    pw->fHaveVideo = FALSE;
}

void UpdateImmersiveVideo(HWND hWnd)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    RECT client;
    if (NULL == pw || NULL == pw->pVideoTexture || NULL == pw->pVideoProc)
        return;
    pw->pVideoProc(hWnd, pw->pVideoTexture, 0, 0, 0);
    pw->fHaveVideo = TRUE;
    pw->fFrameDirty = TRUE;
    if (!IsIconic(hWnd) && GetClientRect(hWnd, &client))
        RenderFrame(hWnd, pw, client.right, client.bottom);
}

BOOL GetResizeInfo(HWND hWnd, LPRESIZEINFO lpri)
{
    LPIMMERSIVEWINDOW pw = GetState(hWnd);
    if (NULL == pw || NULL == lpri)
        return FALSE;
    *lpri = pw->ri;
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
    wc.style         = CS_OWNDC | CS_BYTEALIGNWINDOW | CS_BYTEALIGNCLIENT | CS_DBLCLKS;
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
