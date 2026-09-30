// CDX supplies D2D and DWrite declarations that conflict with the SDK headers.
// NOLINTBEGIN(misc-include-cleaner)
/*
 * caption.c -- the caption DWM would have drawn for the borderless window, rendered into the presenter's
 * Direct2D context each frame: the system icon, the DWrite title, the caption-button sprites straight out
 * of the DWMWindow theme atlas, the light/dark toggle glyph, and a 1px top border on Windows 10. The
 * window (window.c) owns the geometry and the hover/press/activation state; this module only reads it.
 *
 * Resources are created at two moments and nowhere else: InitCaption creates what never changes (the
 * factories, the brush, the toggle font), and PrepareWindowCaption, before a frame's draw opens, rebuilds
 * what follows the window -- the icon, the atlas and the title's text on the caption revision, the toggle
 * glyph masks and the title's font on the DPI, the title's room on the frame's width. Text is rasterized
 * then, into coverage masks (the title, and the toggle glyph per mode). PrepareWindowCaption then records
 * the draw -- bitmap fills alone: the atlas sprites, the icon, and the masks through the foreground brush
 * -- into a command list whenever the geometry or the window's state (hover, press, theme, activation,
 * maximized) moved, and drawing replays it.
 *
 * Windows 10 and Windows 11 differ in three places: the toggle glyph font (Segoe MDL2 Assets / Segoe
 * Fluent Icons); the light-mode toggle glyph, which MDL2 draws too heavy and so is rasterized from its
 * eroded outline (the moon bitmap) on Windows 10; and the 1px top border, which DWM draws for the window
 * only on Windows 11. The OS cannot change under a running process, so InitCaption picks a CAPTIONPLATFORM
 * function table once and nothing else asks which Windows this is.
 *
 * Every Win32/COM failure is fatal here (nt_assert.h ASSERT): the program has no fallback path, so each
 * HRESULT is captured and asserted rather than gated on.
 */

#pragma runtime_checks("", off)
#if defined(_MSC_VER) && !defined(__clang__)
#pragma check_stack(off)
#endif
#pragma strict_gs_check(off)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef COBJMACROS
#define COBJMACROS
#endif

#include <Windows.h> // IWYU pragma: keep
#include <initguid.h> // IWYU pragma: keep
#include <WinUser.h>
#include <WinBase.h>
#include <windef.h>
#include <minwindef.h>
#include <winnt.h>
#include <Uxtheme.h>
#include <objbase.h>
#include <combaseapi.h>
#include <WinNls.h>
#include <libloaderapi.h>
#include <stringapiset.h>
#include <winerror.h>
#include <vssym32.h>
#include "caption.h"
#include "window.h"
#include "dpi.h"
#include "nt_assert.h"
#include "versionhelpersx.h"

/* ---- constants --------------------------------------------------------------------------------- */

/* Caption metrics and shades. */
#define CAPTION_GAP_DIP               6     /* title gap from the icon and from the button cluster      */
#define CAPTION_INACTIVE_ALPHA        0.4F  /* title / glyph / sprite alpha while the window is inactive */
#define CAPTION_HOT_ALPHA             0.12F /* light/dark button hover highlight                        */
#define CAPTION_PRESSED_ALPHA         0.24F /* light/dark button pressed highlight                      */
#define CAPTION_BORDER_ACTIVE_ALPHA   0.50F /* Windows 10 top border, active window                     */
#define CAPTION_BORDER_INACTIVE_ALPHA 0.33F /* Windows 10 top border, inactive window                   */

/* Light/dark toggle glyph, by the mode it is shown in. The code points are shared by Segoe Fluent Icons
   (Windows 11) and Segoe MDL2 Assets (Windows 10). */
#define CAPTION_TOGGLE_CP_DARK_MODE  0xE706U /* drawn while in dark mode  */
#define CAPTION_TOGGLE_CP_LIGHT_MODE 0xF0CEU /* drawn while in light mode */
#define CAPTION_TOGGLE_EM_DARK_MODE  11.0F   /* em size (DIPs) in dark mode  */
static const FLOAT CAPTION_TOGGLE_EM_LIGHT_MODE = 14.0F; /* em size (DIPs) in light mode */

/* Windows 10: the MDL2 moon, thinned. */
static const FLOAT CAPTION_MOON_ERODE_DIP = 0.3F; /* stroke thinned by this per side (DIPs) */
#define CAPTION_MOON_PADDING_DIP 1.0F /* bitmap padding around the ink (DIPs)                           */
#define CAPTION_MOON_SUPERSAMPLE 4U   /* rasterized at this many times the pixel size, then downsampled */
static const FLOAT CAPTION_GEOMETRY_TOLERANCE = 0.01F;
static const FLOAT CAPTION_BOTH_SIDES = 2.0F;
#define CAPTION_PIXEL_CEIL_BIAS 0.999F
#define CAPTION_HALF_PIXEL 0.5F
#define CAPTION_BITMAP_DPI 96.0F
#define CAPTION_TITLE_GAMMA 1.8F
#define CAPTION_ATLAS_DPI_120 120U
#define CAPTION_ATLAS_DPI_144 144U
#define CAPTION_ATLAS_DPI_192 192U

/* "DWMWindow" theme class: the caption-button atlas uDWM composites from. TMT_DISKSTREAM is the atlas
   image; per part, TMT_ATLASRECT locates its strip (four states stacked vertically) and the background
   parts add TMT_SIZINGMARGINS for 9-slice stretching. Each glyph part comes in four
   consecutive DPI variants (96/120/144/192); the dark set is the light set offset by 53. An inactive
   background part is its active part + 1. */
#define CAPTION_WP_MINMAX_LIGHT      3 /* min/max background, light theme, active */
#define CAPTION_WP_CLOSE             7 /* close background, active                 */
#define CAPTION_WP_GLYPH_CLOSE       11
#define CAPTION_WP_GLYPH_MAX         19
#define CAPTION_WP_GLYPH_MIN         23
#define CAPTION_WP_GLYPH_RESTORE     27
#define CAPTION_WP_GLYPH_LIGHT_FIRST 11
#define CAPTION_WP_GLYPH_LIGHT_LAST  30
#define CAPTION_WP_GLYPH_DARK_FIRST  64
#define CAPTION_WP_GLYPH_DARK_LAST   83
#define CAPTION_WP_GLYPH_DARK_OFFSET 53
#define CAPTION_WP_MINMAX_DARK       88 /* min/max background, dark theme, active  */
#define CAPTION_WP_INACTIVE          1  /* part id offset: active -> inactive       */
#define CAPTION_ATLAS_STATES         4
#define CAPTION_ATLAS_STATE_NORMAL   0U
#define CAPTION_ATLAS_STATE_HOT      1U
#define CAPTION_ATLAS_STATE_PRESSED  2U

static const D2D1_COLOR_F CAPTION_CF_BLACK       = {0.0F, 0.0F, 0.0F, 1.0F};
static const D2D1_COLOR_F CAPTION_CF_WHITE       = {1.0F, 1.0F, 1.0F, 1.0F};
static const D2D1_COLOR_F CAPTION_CF_DARK_BORDER = {0.3F, 0.3F, 0.3F, 1.0F};
static const D2D1_COLOR_F CAPTION_CF_CLEAR       = {0.0F, 0.0F, 0.0F, 0.0F};

/* ---- types ------------------------------------------------------------------------------------- */

/* What differs between Windows 10 and Windows 11, as one table per OS (instances precede the interface
   section): the toggle glyph font, how a mode's toggle glyph is rendered into its mask (a cleared mask
   the size of the button cell, at the DPI's scale), and whether a top border is drawn. */
typedef struct CAPTIONPLATFORM
{
    LPCWSTR pszToggleFontFamily;
    void (*pfnRenderToggleMask)(CAPTION* pCaption, ID2D1DeviceContext* pDC, ID2D1Bitmap1* pMask, CAPTIONGLYPH glyph, SIZE cell, FLOAT flScale);
    void (*pfnDrawTopBorder)(const CAPTION* pCaption, ID2D1DeviceContext* pDC, const CAPTIONLAYOUT* pLayout);
} CAPTIONPLATFORM;

/* ---- helpers ----------------------------------------------------------------------------------- */

static FORCEINLINE void CaptionRelease(IUnknown** ppUnk)
{
    if (*ppUnk)
    {
        (void)IUnknown_Release(*ppUnk);
        *ppUnk = NULL;
    }
}

static FORCEINLINE D2D1_RECT_F CaptionRectF(const RECT* prc)
{
    D2D1_RECT_F rf;

    rf.left   = (FLOAT)prc->left;
    rf.top    = (FLOAT)prc->top;
    rf.right  = (FLOAT)prc->right;
    rf.bottom = (FLOAT)prc->bottom;
    return rf;
}

/* The theme's foreground: black on the light theme, white on the dark. */
static FORCEINLINE D2D1_COLOR_F CaptionForeground(const IMMERSIVEWINDOW* pWindow)
{
    D2D1_COLOR_F cf;

    if (pWindow->fLightMode)
    {
        cf = CAPTION_CF_BLACK;
    }
    else
    {
        cf = CAPTION_CF_WHITE;
    }
    return cf;
}

/* An inactive window dims its caption, except for an element the cursor lights (fCursorLit: the caller
   says whether hover, or hover and press, counts for it). */
static FORCEINLINE FLOAT CaptionOpacity(const IMMERSIVEWINDOW* pWindow, BOOL fCursorLit)
{
    FLOAT flOpacity;

    if (pWindow->fActive || fCursorLit)
    {
        flOpacity = 1.0F;
    }
    else
    {
        flOpacity = CAPTION_INACTIVE_ALPHA;
    }
    return flOpacity;
}

/* The atlas cell a button's visual state selects: the strip's normal, hot and pressed cells. */
static FORCEINLINE UINT CaptionAtlasState(CAPTIONBUTTONSTATE state)
{
    if (CBST_HOT == state)
    {
        return CAPTION_ATLAS_STATE_HOT;
    }
    if (CBST_PRESSED == state)
    {
        return CAPTION_ATLAS_STATE_PRESSED;
    }
    return CAPTION_ATLAS_STATE_NORMAL;
}

/* The atlas glyph variant for a DPI: parts are authored at 96/120/144/192 as four consecutive ids. */
static FORCEINLINE UINT CaptionAtlasVariant(UINT dpi)
{
    if (dpi >= CAPTION_ATLAS_DPI_192)
    {
        return 3U;
    }
    if (dpi >= CAPTION_ATLAS_DPI_144)
    {
        return 2U;
    }
    if (dpi >= CAPTION_ATLAS_DPI_120)
    {
        return 1U;
    }
    return 0U;
}

/* A WIC source converted to premultiplied BGRA, the format the Direct2D bitmaps here take. */
static FORCEINLINE IWICFormatConverter* CaptionConvertToPbgra(IWICImagingFactory* pWic, IWICBitmapSource* pSource)
{
    IWICFormatConverter* pConverter;

    ASSERT(SUCCEEDED(IWICImagingFactory_CreateFormatConverter(pWic, &pConverter)));
    ASSERT(SUCCEEDED(IWICFormatConverter_Initialize(pConverter, pSource, &GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeMedianCut)));
    return pConverter;
}

static FORCEINLINE ID2D1Bitmap* CaptionCreateBitmapFromWic(ID2D1DeviceContext* pDC, IWICBitmapSource* pSource)
{
    ID2D1Bitmap* pBitmap;

    ASSERT(SUCCEEDED(ID2D1DeviceContext_CreateBitmapFromWicBitmap(pDC, pSource, NULL, &pBitmap)));
    return pBitmap;
}

/* A premultiplied BGRA bitmap of whole pixels the context can draw into: a mask, or a rasterization
   stage. Pixels are DIPs (96 DPI) so drawing into it is in pixels. */
static FORCEINLINE ID2D1Bitmap1* CaptionCreateTargetBitmap(ID2D1DeviceContext* pDC, D2D1_SIZE_U size)
{
    D2D1_BITMAP_PROPERTIES1 bp;
    ID2D1Bitmap1*           pBitmap;

    SecureZeroMemory(&bp, sizeof(bp));
    bp.pixelFormat.format    = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    bp.dpiX                  = CAPTION_BITMAP_DPI;
    bp.dpiY                  = CAPTION_BITMAP_DPI;
    bp.bitmapOptions         = D2D1_BITMAP_OPTIONS_TARGET;
    ASSERT(SUCCEEDED(ID2D1DeviceContext_CreateBitmap1(pDC, size, NULL, 0U, &bp, &pBitmap)));
    return pBitmap;
}

/* Open a draw into a mask: the context retargeted to it and cleared. Returns the target to restore.
   Runs outside the frame's BeginDraw/EndDraw. */
static FORCEINLINE ID2D1Image* CaptionBeginMask(ID2D1DeviceContext* pDC, ID2D1Bitmap1* pMask)
{
    ID2D1Image* pTargetPrev;

    ID2D1DeviceContext_GetTarget(pDC, &pTargetPrev);
    ID2D1DeviceContext_SetTarget(pDC, (ID2D1Image*)pMask);
    ID2D1DeviceContext_BeginDraw(pDC);
    ID2D1DeviceContext_Clear(pDC, &CAPTION_CF_CLEAR);
    return pTargetPrev;
}

/* Close the draw into a mask and put the frame's target back. */
static FORCEINLINE void CaptionEndMask(ID2D1DeviceContext* pDC, ID2D1Image* pTargetPrev)
{
    ASSERT(SUCCEEDED(ID2D1DeviceContext_EndDraw(pDC, NULL, NULL)));
    ID2D1DeviceContext_SetTarget(pDC, pTargetPrev);
    CaptionRelease((IUnknown**)&pTargetPrev);
}

/* The mask already contains rasterized coverage. Composite it as graphics so text gamma/contrast
   are not applied a second time when filling it with the foreground brush. */
static FORCEINLINE void CaptionFillMask(ID2D1DeviceContext* pDC, ID2D1Bitmap1* pMask, ID2D1SolidColorBrush* pBrush, const D2D1_RECT_F* prfDest)
{
    ID2D1DeviceContext_FillOpacityMask(pDC, (ID2D1Bitmap*)pMask, (ID2D1Brush*)pBrush, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, prfDest, NULL);
}

/* Whether two layouts agree: the recording follows the geometry the frame draws to. */
static FORCEINLINE BOOL CaptionLayoutEquals(const CAPTIONLAYOUT* pA, const CAPTIONLAYOUT* pB)
{
    const RECT* prcA;
    const RECT* prcB;
    UINT        cRects;
    BOOL        fEqual;

    fEqual = (pA->dpi == pB->dpi) && (pA->cyCaption == pB->cyCaption) && (pA->cyTopBorder == pB->cyTopBorder) && (pA->xTitle == pB->xTitle) && EqualRect(&pA->sysMenu, &pB->sysMenu);
    prcA   = pA->buttons;
    prcB   = pB->buttons;
    for (cRects = CB_COUNT; cRects != 0U; --cRects, ++prcA, ++prcB)
    {
        fEqual = fEqual && EqualRect(prcA, prcB);
    }
    return fEqual;
}

/* A layout, copied field by field (no CRT here, so no memcpy). */
static FORCEINLINE void CaptionCopyLayout(CAPTIONLAYOUT* pTo, const CAPTIONLAYOUT* pFrom)
{
    RECT*       prcTo;
    const RECT* prcFrom;
    UINT        cRects;

    pTo->dpi         = pFrom->dpi;
    pTo->cyCaption   = pFrom->cyCaption;
    pTo->cyTopBorder = pFrom->cyTopBorder;
    pTo->xTitle      = pFrom->xTitle;
    pTo->sysMenu     = pFrom->sysMenu;
    prcTo            = pTo->buttons;
    prcFrom          = pFrom->buttons;
    for (cRects = CB_COUNT; cRects != 0U; --cRects, ++prcTo, ++prcFrom)
    {
        *prcTo = *prcFrom;
    }
}

/* The title's room in the caption band: from the layout's title origin to the toggle's gap. */
static FORCEINLINE LONG CaptionTitleRoom(const CAPTIONLAYOUT* pLayout, LONG* pxLeft)
{
    LONG cxGap;

    cxGap   = DipsToPixels(CAPTION_GAP_DIP, pLayout->dpi);
    *pxLeft = pLayout->xTitle;
    return pLayout->buttons[CB_LIGHTDARKBUTTON].left - cxGap - *pxLeft;
}

/* ---- text rasterization ----------------------------------------------------------------------- */

/* Grayscale rasterization: the title uses DWM's gamma 1.8 and grayscale contrast 1.0, with the
   actual foreground color; the toggle keeps its unadjusted coverage (gamma 1.0, contrast 0.0). */
static FORCEINLINE IDWriteRenderingParams2* CaptionCreateTextParams(IDWriteFactory* pDWrite, FLOAT flGamma, FLOAT flContrast)
{
    IDWriteFactory2*         pDWrite2;
    IDWriteRenderingParams2* pParams;

    ASSERT(SUCCEEDED(IDWriteFactory_QueryInterface(pDWrite, &IID_IDWriteFactory2, (void**)&pDWrite2)));
    ASSERT(SUCCEEDED(IDWriteFactory2_CreateCustomRenderingParams2(pDWrite2, flGamma, 0.0F, flContrast, 0.0F, DWRITE_PIXEL_GEOMETRY_FLAT, DWRITE_RENDERING_MODE_DEFAULT, DWRITE_GRID_FIT_MODE_DEFAULT, &pParams)));
    CaptionRelease((IUnknown**)&pDWrite2);
    return pParams;
}

/* ---- toggle glyph font ------------------------------------------------------------------------ */

/* The light/dark toggle glyph font: the platform's family, falling back to Segoe MDL2 Assets, which
   every supported Windows ships. Resolves both toggle code points to glyph indices; every one must
   exist. */
static FORCEINLINE IDWriteFontFace* CaptionCreateToggleFont(IDWriteFactory* pDWrite, LPCWSTR pszFamily, UINT16* rgGlyphIndices)
{
    static const UINT32    rgCodePoints[CG_COUNT] = {CAPTION_TOGGLE_CP_DARK_MODE, CAPTION_TOGGLE_CP_LIGHT_MODE};
    IDWriteFontCollection* pCollection;
    IDWriteFontFamily*     pFamily;
    IDWriteFont*           pFont;
    IDWriteFontFace*       pFace;
    UINT32                 iFamily;
    BOOL                   fFamilyExists;
    UINT                   i;

    ASSERT(SUCCEEDED(IDWriteFactory_GetSystemFontCollection(pDWrite, &pCollection, FALSE)));

    iFamily = 0U;
    fFamilyExists = FALSE;
    ASSERT(SUCCEEDED(IDWriteFontCollection_FindFamilyName(pCollection, pszFamily, &iFamily, &fFamilyExists)));
    if (!fFamilyExists)
    {
        ASSERT(SUCCEEDED(IDWriteFontCollection_FindFamilyName(pCollection, L"Segoe MDL2 Assets", &iFamily, &fFamilyExists)));
    }
    ASSERT(fFamilyExists);

    ASSERT(SUCCEEDED(IDWriteFontCollection_GetFontFamily(pCollection, iFamily, &pFamily)));
    ASSERT(SUCCEEDED(IDWriteFontFamily_GetFirstMatchingFont(pFamily, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL, DWRITE_FONT_STYLE_NORMAL, &pFont)));
    ASSERT(SUCCEEDED(IDWriteFont_CreateFontFace(pFont, &pFace)));
    ASSERT(SUCCEEDED(IDWriteFontFace_GetGlyphIndices(pFace, rgCodePoints, ARRAYSIZE(rgCodePoints), rgGlyphIndices)));
    for (i = 0U; i < ARRAYSIZE(rgCodePoints); ++i)
    {
        ASSERT(rgGlyphIndices[i]);
    }

    CaptionRelease((IUnknown**)&pFont);
    CaptionRelease((IUnknown**)&pFamily);
    CaptionRelease((IUnknown**)&pCollection);
    return pFace;
}

/* ---- moon bitmap (Windows 10) ----------------------------------------------------------------- */

/* The MDL2 moon, thinned: the glyph's outline at its em size, eroded by CAPTION_MOON_ERODE_DIP per side
   (the outline widened by twice that, then excluded from the fill), in DIPs from the baseline origin. */
static FORCEINLINE ID2D1PathGeometry* CaptionCreateMoonGeometry(ID2D1Factory* pFactory, IDWriteFontFace* pFace, UINT16 glyphIndex)
{
    ID2D1PathGeometry* pGlyph;
    ID2D1PathGeometry* pBand;
    ID2D1PathGeometry* pMoon;
    ID2D1GeometrySink* pSink;

    ASSERT(SUCCEEDED(ID2D1Factory_CreatePathGeometry(pFactory, &pGlyph)));
    ASSERT(SUCCEEDED(ID2D1PathGeometry_Open(pGlyph, &pSink)));
    ASSERT(SUCCEEDED(IDWriteFontFace_GetGlyphRunOutline(pFace, CAPTION_TOGGLE_EM_LIGHT_MODE, &glyphIndex, NULL, NULL, 1U, FALSE, FALSE, (IDWriteGeometrySink*)pSink)));
    ASSERT(SUCCEEDED(ID2D1GeometrySink_Close(pSink)));
    CaptionRelease((IUnknown**)&pSink);

    ASSERT(SUCCEEDED(ID2D1Factory_CreatePathGeometry(pFactory, &pBand)));
    ASSERT(SUCCEEDED(ID2D1PathGeometry_Open(pBand, &pSink)));
    ASSERT(SUCCEEDED(ID2D1PathGeometry_Widen(pGlyph, CAPTION_BOTH_SIDES * CAPTION_MOON_ERODE_DIP, NULL, NULL, CAPTION_GEOMETRY_TOLERANCE, (ID2D1SimplifiedGeometrySink*)pSink)));
    ASSERT(SUCCEEDED(ID2D1GeometrySink_Close(pSink)));
    CaptionRelease((IUnknown**)&pSink);

    ASSERT(SUCCEEDED(ID2D1Factory_CreatePathGeometry(pFactory, &pMoon)));
    ASSERT(SUCCEEDED(ID2D1PathGeometry_Open(pMoon, &pSink)));
    ASSERT(SUCCEEDED(ID2D1PathGeometry_CombineWithGeometry(pGlyph, (ID2D1Geometry*)pBand, D2D1_COMBINE_MODE_EXCLUDE, NULL, CAPTION_GEOMETRY_TOLERANCE, (ID2D1SimplifiedGeometrySink*)pSink)));
    ASSERT(SUCCEEDED(ID2D1GeometrySink_Close(pSink)));
    CaptionRelease((IUnknown**)&pSink);

    CaptionRelease((IUnknown**)&pBand);
    CaptionRelease((IUnknown**)&pGlyph);
    return pMoon;
}

/* Rasterize the moon geometry into a bitmap of whole pixels at flScale pixels per DIP: filled at
   CAPTION_MOON_SUPERSAMPLE times the size, then downsampled (cubic) into the bitmap the draw blits.
   *pptMoon receives the bitmap's top-left offset from the glyph's baseline origin (DIPs). Retargets
   the context, so this runs outside the frame's BeginDraw/EndDraw. */
static FORCEINLINE ID2D1Bitmap1* CaptionRasterizeMoon(ID2D1DeviceContext* pDC, ID2D1SolidColorBrush* pBrush, ID2D1PathGeometry* pMoon, FLOAT flScale, D2D1_POINT_2F* pptMoon)
{
    ID2D1Image*       pTargetPrev;
    ID2D1Bitmap1*     pHi;
    ID2D1Bitmap1*     pLo;
    D2D1_RECT_F       rfBounds;
    D2D1_MATRIX_3X2_F mx;
    D2D1_SIZE_U       size;
    FLOAT             flSuper;

    ASSERT(SUCCEEDED(ID2D1PathGeometry_GetBounds(pMoon, NULL, &rfBounds)));
    pptMoon->x  = rfBounds.left - CAPTION_MOON_PADDING_DIP;
    pptMoon->y  = rfBounds.top - CAPTION_MOON_PADDING_DIP;
    size.width  = (UINT32)(((rfBounds.right - rfBounds.left + (CAPTION_BOTH_SIDES * CAPTION_MOON_PADDING_DIP)) * flScale) + CAPTION_PIXEL_CEIL_BIAS);
    size.height = (UINT32)(((rfBounds.bottom - rfBounds.top + (CAPTION_BOTH_SIDES * CAPTION_MOON_PADDING_DIP)) * flScale) + CAPTION_PIXEL_CEIL_BIAS);
    pLo         = CaptionCreateTargetBitmap(pDC, size);
    size.width  *= CAPTION_MOON_SUPERSAMPLE;
    size.height *= CAPTION_MOON_SUPERSAMPLE;
    pHi         = CaptionCreateTargetBitmap(pDC, size);

    ID2D1DeviceContext_GetTarget(pDC, &pTargetPrev);
    SecureZeroMemory(&mx, sizeof(mx));

    /* Supersampled: DIPs relative to the baseline origin -> pixels of the large bitmap. */
    flSuper = flScale * (FLOAT)CAPTION_MOON_SUPERSAMPLE;
    mx._11  = flSuper;
    mx._22  = flSuper;
    mx._31  = -pptMoon->x * flSuper;
    mx._32  = -pptMoon->y * flSuper;
    ID2D1SolidColorBrush_SetColor(pBrush, &CAPTION_CF_BLACK);
    ID2D1DeviceContext_SetTarget(pDC, (ID2D1Image*)pHi);
    ID2D1DeviceContext_BeginDraw(pDC);
    ID2D1DeviceContext_Clear(pDC, &CAPTION_CF_CLEAR);
    ID2D1DeviceContext_SetTransform(pDC, &mx);
    ID2D1DeviceContext_FillGeometry(pDC, (ID2D1Geometry*)pMoon, (ID2D1Brush*)pBrush, NULL);
    ASSERT(SUCCEEDED(ID2D1DeviceContext_EndDraw(pDC, NULL, NULL)));

    /* Downsampled: the large bitmap scaled onto the small one. */
    mx._11 = 1.0F / (FLOAT)CAPTION_MOON_SUPERSAMPLE;
    mx._22 = mx._11;
    mx._31 = 0.0F;
    mx._32 = 0.0F;
    ID2D1DeviceContext_SetTarget(pDC, (ID2D1Image*)pLo);
    ID2D1DeviceContext_BeginDraw(pDC);
    ID2D1DeviceContext_Clear(pDC, &CAPTION_CF_CLEAR);
    ID2D1DeviceContext_SetTransform(pDC, &mx);
    ID2D1DeviceContext_DrawImage(pDC, (ID2D1Image*)pHi, NULL, NULL, D2D1_INTERPOLATION_MODE_HIGH_QUALITY_CUBIC, D2D1_COMPOSITE_MODE_SOURCE_OVER);
    ASSERT(SUCCEEDED(ID2D1DeviceContext_EndDraw(pDC, NULL, NULL)));

    /* Back to the frame: identity transform, the swap chain's target. */
    mx._11 = 1.0F;
    mx._22 = 1.0F;
    ID2D1DeviceContext_SetTransform(pDC, &mx);
    ID2D1DeviceContext_SetTarget(pDC, pTargetPrev);

    CaptionRelease((IUnknown**)&pTargetPrev);
    CaptionRelease((IUnknown**)&pHi);
    return pLo;
}

/* ---- atlas ------------------------------------------------------------------------------------- */

/* One state's cell of a part's strip, minus the one-pixel sampling border uDWM also excludes. */
static FORCEINLINE RECT CaptionAtlasStateRect(const CAPTIONATLAS* pAtlas, UINT uPart, UINT uState)
{
    RECT rc;
    LONG cyState;

    rc      = pAtlas->partRects[uPart];
    cyState = (rc.bottom - rc.top) / CAPTION_ATLAS_STATES;
    rc.top += (LONG)uState * cyState;
    rc.bottom = rc.top + cyState;
    (void)InflateRect(&rc, -1, -1);
    return rc;
}

static FORCEINLINE void CaptionReadAtlasPartRect(HTHEME hTheme, CAPTIONATLAS* pAtlas, UINT uPart)
{
    ASSERT(SUCCEEDED(GetThemeRect(hTheme, (int)uPart, 0, TMT_ATLASRECT, &pAtlas->partRects[uPart])));
}

/* 9-slice a background part's state cell over prc: the TMT_SIZINGMARGINS corners copy 1:1 and the
   edges and center stretch, as DrawThemeBackground would. */
static FORCEINLINE void CaptionDrawAtlasBackground(ID2D1DeviceContext* pDC, const CAPTIONATLAS* pAtlas, UINT uPart, UINT uState, const RECT* prc)
{
    RECT        rcSource;
    MARGINS     sizing;
    FLOAT       rgflSourceX[4];
    FLOAT       rgflSourceY[4];
    FLOAT       rgflTargetX[4];
    FLOAT       rgflTargetY[4];
    D2D1_RECT_F rfFrom;
    D2D1_RECT_F rfTo;
    UINT        x;
    UINT        y;

    sizing         = pAtlas->sizingMargins[uPart];
    rcSource       = CaptionAtlasStateRect(pAtlas, uPart, uState);
    rgflSourceX[0] = (FLOAT)rcSource.left;
    rgflSourceX[1] = (FLOAT)(rcSource.left + sizing.cxLeftWidth);
    rgflSourceX[2] = (FLOAT)(rcSource.right - sizing.cxRightWidth);
    rgflSourceX[3] = (FLOAT)rcSource.right;
    rgflSourceY[0] = (FLOAT)rcSource.top;
    rgflSourceY[1] = (FLOAT)(rcSource.top + sizing.cyTopHeight);
    rgflSourceY[2] = (FLOAT)(rcSource.bottom - sizing.cyBottomHeight);
    rgflSourceY[3] = (FLOAT)rcSource.bottom;
    rgflTargetX[0] = (FLOAT)prc->left;
    rgflTargetX[1] = (FLOAT)(prc->left + sizing.cxLeftWidth);
    rgflTargetX[2] = (FLOAT)(prc->right - sizing.cxRightWidth);
    rgflTargetX[3] = (FLOAT)prc->right;
    rgflTargetY[0] = (FLOAT)prc->top;
    rgflTargetY[1] = (FLOAT)(prc->top + sizing.cyTopHeight);
    rgflTargetY[2] = (FLOAT)(prc->bottom - sizing.cyBottomHeight);
    rgflTargetY[3] = (FLOAT)prc->bottom;

    for (y = 0U; y < 3U; ++y)
    {
        for (x = 0U; x < 3U; ++x)
        {
            rfFrom.left   = rgflSourceX[x];
            rfFrom.top    = rgflSourceY[y];
            rfFrom.right  = rgflSourceX[x + 1U];
            rfFrom.bottom = rgflSourceY[y + 1U];
            rfTo.left     = rgflTargetX[x];
            rfTo.top      = rgflTargetY[y];
            rfTo.right    = rgflTargetX[x + 1U];
            rfTo.bottom   = rgflTargetY[y + 1U];
            ID2D1DeviceContext_DrawBitmap(pDC, pAtlas->bitmap, &rfTo, 1.0F, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &rfFrom);
        }
    }
}

/* The glyph sprite, placed the way uDWM places it (measured against DWM's glyphs and button rects at
   every scale from 100% to 250%): unscaled, centered in the button rect rounding down, and the
   minimize glyph one column further right, rounding its centering up. Vertically the rect is the
   whole caption band, whose top rows are the border. */
static FORCEINLINE void CaptionDrawAtlasGlyph(ID2D1DeviceContext* pDC, const CAPTIONATLAS* pAtlas, UINT uPart, UINT uState, const RECT* prc, BOOL fMinimize, FLOAT flOpacity)
{
    RECT        rcSource;
    D2D1_RECT_F rfFrom;
    D2D1_RECT_F rfTo;
    LONG        cxSprite;
    LONG        cySprite;
    LONG        cxSlack;
    LONG        cySlack;
    LONG        x;
    LONG        y;

    rcSource = CaptionAtlasStateRect(pAtlas, uPart, uState);
    cxSprite = rcSource.right - rcSource.left;
    cySprite = rcSource.bottom - rcSource.top;
    cxSlack  = prc->right - prc->left - cxSprite;
    cySlack  = prc->bottom - prc->top - cySprite;
    if (fMinimize)
    {
        x = prc->left + ((cxSlack + 1) / 2) + 1;
    }
    else
    {
        x = prc->left + (cxSlack / 2);
    }
    y = prc->top + (cySlack / 2);

    rfTo.left   = (FLOAT)x;
    rfTo.top    = (FLOAT)y;
    rfTo.right  = (FLOAT)(x + cxSprite);
    rfTo.bottom = (FLOAT)(y + cySprite);
    rfFrom      = CaptionRectF(&rcSource);
    ID2D1DeviceContext_DrawBitmap(pDC, pAtlas->bitmap, &rfTo, flOpacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &rfFrom);
}

/* ---- loading (PrepareWindowCaption only) ------------------------------------------------------ */

/* The system icon, as a premultiplied bitmap of the icon the window resolved (see UpdateCaptionIcon),
   at the icon's own size: the draw stretches it to the slot as DWM does. */
static FORCEINLINE void CaptionLoadIcon(CAPTION* pCaption, ID2D1DeviceContext* pDC)
{
    IWICBitmap*          pSource;
    IWICFormatConverter* pConverter;

    CaptionRelease((IUnknown**)&pCaption->iconBitmap);

    ASSERT(SUCCEEDED(IWICImagingFactory_CreateBitmapFromHICON(pCaption->wic, pCaption->window->captionIcon, &pSource)));
    pConverter           = CaptionConvertToPbgra(pCaption->wic, (IWICBitmapSource*)pSource);
    pCaption->iconBitmap = CaptionCreateBitmapFromWic(pDC, (IWICBitmapSource*)pConverter);

    CaptionRelease((IUnknown**)&pConverter);
    CaptionRelease((IUnknown**)&pSource);
}

/* The DWMWindow caption atlas, decoded out of the current .msstyles (TMT_DISKSTREAM) with every glyph
   part's strip rect and the background parts' rects and 9-slice margins. */
static FORCEINLINE void CaptionLoadAtlas(CAPTION* pCaption, ID2D1DeviceContext* pDC)
{
    static const UINT rgBackgroundParts[] =
        {CAPTION_WP_MINMAX_LIGHT, CAPTION_WP_MINMAX_LIGHT + CAPTION_WP_INACTIVE, CAPTION_WP_CLOSE, CAPTION_WP_CLOSE + CAPTION_WP_INACTIVE, CAPTION_WP_MINMAX_DARK, CAPTION_WP_MINMAX_DARK + CAPTION_WP_INACTIVE};
    CAPTIONATLAS*          pAtlas;
    WCHAR                  szStyles[MAX_PATH];
    HMODULE                hStyles; /* the .msstyles, mapped as a data file */
    HTHEME                 hTheme;  /* its "DWMWindow" class               */
    void*                  pvStream;
    DWORD                  cbStream;
    IWICStream*            pStream;
    IWICBitmapDecoder*     pDecoder;
    IWICBitmapFrameDecode* pFrame;
    IWICFormatConverter*   pConverter;
    const UINT*            puPart;
    UINT                   uPart;
    UINT                   cParts;

    pAtlas = &pCaption->atlas;
    CaptionRelease((IUnknown**)&pAtlas->bitmap);

    /* The atlas image, straight out of the .msstyles. */
    ASSERT(SUCCEEDED(GetCurrentThemeName(szStyles, ARRAYSIZE(szStyles), NULL, 0, NULL, 0)));
    hStyles = LoadLibraryExW(szStyles, NULL, LOAD_LIBRARY_AS_DATAFILE);
    ASSERT(hStyles);
    hTheme = OpenThemeData(NULL, L"DWMWindow");
    ASSERT(hTheme);
    ASSERT(SUCCEEDED(GetThemeStream(hTheme, 0, 0, TMT_DISKSTREAM, &pvStream, &cbStream, hStyles)));

    ASSERT(SUCCEEDED(IWICImagingFactory_CreateStream(pCaption->wic, &pStream)));
    ASSERT(SUCCEEDED(IWICStream_InitializeFromMemory(pStream, (BYTE*)pvStream, cbStream)));
    ASSERT(SUCCEEDED(IWICImagingFactory_CreateDecoderFromStream(pCaption->wic, (IStream*)pStream, NULL, WICDecodeMetadataCacheOnLoad, &pDecoder)));
    ASSERT(SUCCEEDED(IWICBitmapDecoder_GetFrame(pDecoder, 0U, &pFrame)));
    pConverter     = CaptionConvertToPbgra(pCaption->wic, (IWICBitmapSource*)pFrame);
    pAtlas->bitmap = CaptionCreateBitmapFromWic(pDC, (IWICBitmapSource*)pConverter);

    /* The parts: every glyph strip, and the background strips with their 9-slice margins. */
    uPart = CAPTION_WP_GLYPH_LIGHT_FIRST;
    for (cParts = CAPTION_WP_GLYPH_LIGHT_LAST - CAPTION_WP_GLYPH_LIGHT_FIRST + 1U; cParts != 0U; --cParts, ++uPart)
    {
        CaptionReadAtlasPartRect(hTheme, pAtlas, uPart);
    }
    uPart = CAPTION_WP_GLYPH_DARK_FIRST;
    for (cParts = CAPTION_WP_GLYPH_DARK_LAST - CAPTION_WP_GLYPH_DARK_FIRST + 1U; cParts != 0U; --cParts, ++uPart)
    {
        CaptionReadAtlasPartRect(hTheme, pAtlas, uPart);
    }
    puPart = rgBackgroundParts;
    for (cParts = ARRAYSIZE(rgBackgroundParts); cParts != 0U; --cParts, ++puPart)
    {
        uPart = *puPart;
        CaptionReadAtlasPartRect(hTheme, pAtlas, uPart);
        ASSERT(SUCCEEDED(GetThemeMargins(hTheme, NULL, (int)uPart, 0, TMT_SIZINGMARGINS, NULL, &pAtlas->sizingMargins[uPart])));
    }

    CaptionRelease((IUnknown**)&pConverter);
    CaptionRelease((IUnknown**)&pFrame);
    CaptionRelease((IUnknown**)&pDecoder);
    CaptionRelease((IUnknown**)&pStream);
    ASSERT(SUCCEEDED(CloseThemeData(hTheme)));
    ASSERT(FreeLibrary(hStyles));
}

/* Lay the title out and render it into its mask: the DPI's caption font from its non-client metrics,
   no wrapping, centered paragraph, character-granularity ellipsis trimming, in the room between the
   icon and the toggle by the caption band tall, its origin centering the measured text in the band.
   No mask when there is no title or no room for one. Retargets the context, so this runs outside the
   frame's BeginDraw/EndDraw. */
static FORCEINLINE void CaptionLayoutTitle(CAPTION* pCaption, ID2D1DeviceContext* pDC, const CAPTIONLAYOUT* pLayout)
{
    const IMMERSIVEWINDOW*   pWindow;
    CAPTIONTEXT*             pText;
    NONCLIENTMETRICSW        ncm;
    IDWriteTextFormat*       pFormat;
    IDWriteInlineObject*     pEllipsis;
    IDWriteTextLayout*       pLayoutText;
    ID2D1Image*              pTargetPrev;
    DWRITE_TRIMMING          trimming;
    DWRITE_TEXT_METRICS      metrics;
    DWRITE_FONT_STYLE        fontStyle;
    D2D1_TEXT_ANTIALIAS_MODE modeTextPrev;
    D2D1_POINT_2F            ptOrigin;
    D2D1_COLOR_F             cfText;
    D2D1_SIZE_U              size;
    WCHAR                    szLocale[LOCALE_NAME_MAX_LENGTH];
    FLOAT                    flFontSize;
    LONG                     xLeft;
    LONG                     cxAvailable;
    LONG                     cyBand;
    BOOL                     fNothingToLayOut;

    pWindow = pCaption->window;
    pText   = &pCaption->text;
    CaptionRelease((IUnknown**)&pText->mask);

    cxAvailable      = CaptionTitleRoom(pLayout, &xLeft);
    cyBand           = pLayout->cyCaption;
    pText->size.cx   = cxAvailable;
    pText->size.cy   = cyBand;
    fNothingToLayOut = (pWindow->captionTitle[0] == L'\0') | (cxAvailable <= 0);
    if (fNothingToLayOut)
    {
        return;
    }

    SecureZeroMemory(&ncm, sizeof(ncm));
    ncm.cbSize = (DWORD)sizeof(ncm);
    ASSERT(SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, (UINT)sizeof(ncm), &ncm, 0U, pLayout->dpi));
    ASSERT(GetUserDefaultLocaleName(szLocale, ARRAYSIZE(szLocale)));
    flFontSize = (FLOAT)-ncm.lfCaptionFont.lfHeight;
    ASSERT(flFontSize > 0.0F);
    if (ncm.lfCaptionFont.lfItalic)
    {
        fontStyle = DWRITE_FONT_STYLE_ITALIC;
    }
    else
    {
        fontStyle = DWRITE_FONT_STYLE_NORMAL;
    }

    ASSERT(SUCCEEDED(IDWriteFactory_CreateTextFormat(pCaption->dwrite,
                                                     ncm.lfCaptionFont.lfFaceName,
                                                     NULL,
                                                     (DWRITE_FONT_WEIGHT)ncm.lfCaptionFont.lfWeight,
                                                     fontStyle,
                                                     DWRITE_FONT_STRETCH_NORMAL,
                                                     flFontSize,
                                                     szLocale,
                                                     &pFormat)));
    ASSERT(SUCCEEDED(IDWriteTextFormat_SetWordWrapping(pFormat, DWRITE_WORD_WRAPPING_NO_WRAP)));
    ASSERT(SUCCEEDED(IDWriteTextFormat_SetParagraphAlignment(pFormat, DWRITE_PARAGRAPH_ALIGNMENT_CENTER)));
    ASSERT(SUCCEEDED(IDWriteFactory_CreateEllipsisTrimmingSign(pCaption->dwrite, pFormat, &pEllipsis)));
    SecureZeroMemory(&trimming, sizeof(trimming));
    trimming.granularity = DWRITE_TRIMMING_GRANULARITY_CHARACTER;
    ASSERT(SUCCEEDED(IDWriteTextFormat_SetTrimming(pFormat, &trimming, pEllipsis)));
    ASSERT(SUCCEEDED(IDWriteFactory_CreateTextLayout(pCaption->dwrite, pWindow->captionTitle, (UINT32)lstrlenW(pWindow->captionTitle), pFormat, (FLOAT)cxAvailable, (FLOAT)cyBand, &pLayoutText)));
    ASSERT(SUCCEEDED(IDWriteTextLayout_GetMetrics(pLayoutText, &metrics)));

    /* Into the mask: the room wide by the band tall, the text grayscale-antialiased (coverage alone;
       the frame supplies the shade) at the origin centering it in the band. */
    size.width      = (UINT32)cxAvailable;
    size.height     = (UINT32)cyBand;
    ptOrigin.x      = 0.0F;
    ptOrigin.y      = (FLOAT)(LONG)(CAPTION_HALF_PIXEL * ((FLOAT)cyBand - metrics.height)) - metrics.top;
    pText->mask     = CaptionCreateTargetBitmap(pDC, size);
    pTargetPrev     = CaptionBeginMask(pDC, pText->mask);
    modeTextPrev    = ID2D1DeviceContext_GetTextAntialiasMode(pDC);
    ID2D1DeviceContext_SetTextAntialiasMode(pDC, D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    ID2D1DeviceContext_SetTextRenderingParams(pDC, (IDWriteRenderingParams*)pCaption->titleParams);
    cfText = CaptionForeground(pWindow);
    ID2D1SolidColorBrush_SetColor(pCaption->brush, &cfText);
    ID2D1DeviceContext_DrawTextLayout(pDC, ptOrigin, pLayoutText, (ID2D1Brush*)pCaption->brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
    ID2D1DeviceContext_SetTextRenderingParams(pDC, (IDWriteRenderingParams*)pCaption->textParams);
    ID2D1DeviceContext_SetTextAntialiasMode(pDC, modeTextPrev);
    CaptionEndMask(pDC, pTargetPrev);

    pText->rfDest.left   = (FLOAT)xLeft;
    pText->rfDest.top    = 0.0F;
    pText->rfDest.right  = (FLOAT)(xLeft + cxAvailable);
    pText->rfDest.bottom = (FLOAT)cyBand;

    CaptionRelease((IUnknown**)&pLayoutText);
    CaptionRelease((IUnknown**)&pEllipsis);
    CaptionRelease((IUnknown**)&pFormat);
}

/* ---- rendering: icon, title, top border ------------------------------------------------------- */

/* The same DPI-sized, resource-independent icon assigned to the native window, drawn 1:1. */
static FORCEINLINE void CaptionDrawIcon(const CAPTION* pCaption, ID2D1DeviceContext* pDC, const CAPTIONLAYOUT* pLayout)
{
    D2D1_RECT_F rf;

    rf = CaptionRectF(&pLayout->buttons[CB_ICON]);
    ID2D1DeviceContext_DrawBitmap(pDC, pCaption->iconBitmap, &rf, 1.0F, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
}

/* The caption title: its mask filled in the foreground shade. */
static FORCEINLINE void CaptionDrawTitle(const CAPTION* pCaption, ID2D1DeviceContext* pDC)
{
    D2D1_COLOR_F cf;

    if (pCaption->text.mask)
    {
        cf   = CaptionForeground(pCaption->window);
        cf.a = CaptionOpacity(pCaption->window, FALSE);
        ID2D1SolidColorBrush_SetColor(pCaption->brush, &cf);
        CaptionFillMask(pDC, pCaption->text.mask, pCaption->brush, &pCaption->text.rfDest);
    }
}

/* Windows 10's pfnDrawTopBorder: a 1px top border in the theme's shade (black in light mode, grey in
   dark), dimmer when active than inactive. Skipped when maximized. */
static FORCEINLINE void CaptionDrawTopBorder(const CAPTION* pCaption, ID2D1DeviceContext* pDC, const CAPTIONLAYOUT* pLayout)
{
    const IMMERSIVEWINDOW* pWindow;
    D2D1_RECT_F            rf;
    D2D1_COLOR_F           cf;

    pWindow = pCaption->window;
    if (IsZoomed(pWindow->hwnd))
    {
        return;
    }
    rf.left   = 0.0F;
    rf.top    = 0.0F;
    rf.right  = (FLOAT)(pWindow->rcClient.right - pWindow->rcClient.left);
    rf.bottom = (FLOAT)pLayout->cyTopBorder;
    if (pWindow->fLightMode)
    {
        cf = CAPTION_CF_BLACK;
    }
    else
    {
        cf = CAPTION_CF_DARK_BORDER;
    }
    if (pWindow->fActive)
    {
        cf.a = CAPTION_BORDER_ACTIVE_ALPHA;
    }
    else
    {
        cf.a = CAPTION_BORDER_INACTIVE_ALPHA;
    }
    ID2D1SolidColorBrush_SetColor(pCaption->brush, &cf);
    ID2D1DeviceContext_FillRectangle(pDC, &rf, (ID2D1Brush*)pCaption->brush);
}

/* Windows 11's pfnDrawTopBorder: DWM draws the window's. */
static void CaptionDrawNoTopBorder(const CAPTION* pCaption, ID2D1DeviceContext* pDC, const CAPTIONLAYOUT* pLayout)
{
    UNREFERENCED_PARAMETER(pCaption);
    UNREFERENCED_PARAMETER(pDC);
    UNREFERENCED_PARAMETER(pLayout);
}

/* ---- rendering: light/dark toggle ------------------------------------------------------------- */

/* The hover/press highlight behind the toggle, in the theme's own shade. */
static FORCEINLINE void CaptionDrawToggleHighlight(const CAPTION* pCaption, ID2D1DeviceContext* pDC, const RECT* prc, CAPTIONBUTTONSTATE state)
{
    D2D1_RECT_F  rf;
    D2D1_COLOR_F cf;

    if (CBST_NORMAL == state)
    {
        return;
    }
    rf = CaptionRectF(prc);
    cf = CaptionForeground(pCaption->window);
    if (CBST_PRESSED == state)
    {
        cf.a = CAPTION_PRESSED_ALPHA;
    }
    else
    {
        cf.a = CAPTION_HOT_ALPHA;
    }
    ID2D1SolidColorBrush_SetColor(pCaption->brush, &cf);
    ID2D1DeviceContext_FillRectangle(pDC, &rf, (ID2D1Brush*)pCaption->brush);
}

/* The one-glyph run for a mode's toggle glyph, at the mode's em size scaled to the DPI. */
static FORCEINLINE DWRITE_GLYPH_RUN CaptionToggleRun(const CAPTION* pCaption, CAPTIONGLYPH glyph, FLOAT flScale)
{
    DWRITE_GLYPH_RUN run;
    FLOAT            flEm;

    if (CG_LIGHT_MODE == glyph)
    {
        flEm = CAPTION_TOGGLE_EM_LIGHT_MODE;
    }
    else
    {
        flEm = CAPTION_TOGGLE_EM_DARK_MODE;
    }
    SecureZeroMemory(&run, sizeof(run));
    run.fontFace     = pCaption->fontFace;
    run.glyphCount   = 1U;
    run.glyphIndices = &pCaption->glyphIndices[glyph];
    run.fontEmSize   = flEm * flScale;
    return run;
}

/* The run's baseline origin in the button cell: the em box centered on the cell (half a pixel left),
   and in dark mode re-centered vertically on the run's measured ink bounds. */
static FORCEINLINE D2D1_POINT_2F CaptionToggleOrigin(ID2D1DeviceContext* pDC, const DWRITE_GLYPH_RUN* pRun, SIZE cell, CAPTIONGLYPH glyph)
{
    D2D1_POINT_2F ptOrigin;
    D2D1_RECT_F   rfBounds;

    ptOrigin.x = (FLOAT)(LONG)(CAPTION_HALF_PIXEL * ((FLOAT)cell.cx - pRun->fontEmSize)) - CAPTION_HALF_PIXEL;
    ptOrigin.y = (FLOAT)(LONG)(CAPTION_HALF_PIXEL * ((FLOAT)cell.cy + pRun->fontEmSize));
    if (CG_DARK_MODE == glyph)
    {
        ASSERT(SUCCEEDED(ID2D1DeviceContext_GetGlyphRunWorldBounds(pDC, ptOrigin, pRun, DWRITE_MEASURING_MODE_NATURAL, &rfBounds)));
        ptOrigin.y += CAPTION_HALF_PIXEL * ((FLOAT)cell.cy - rfBounds.top - rfBounds.bottom);
    }
    return ptOrigin;
}

/* Windows 11's pfnRenderToggleMask, and Windows 10's for dark mode: the mode's glyph straight from the
   font, grayscale-antialiased into the cleared mask at its baseline origin. */
static FORCEINLINE void CaptionRenderToggleGlyphMask(CAPTION* pCaption, ID2D1DeviceContext* pDC, ID2D1Bitmap1* pMask, CAPTIONGLYPH glyph, SIZE cell, FLOAT flScale)
{
    ID2D1Image*              pTargetPrev;
    DWRITE_GLYPH_RUN         run;
    D2D1_POINT_2F            ptOrigin;
    D2D1_TEXT_ANTIALIAS_MODE modeTextPrev;

    run      = CaptionToggleRun(pCaption, glyph, flScale);
    ptOrigin = CaptionToggleOrigin(pDC, &run, cell, glyph);

    pTargetPrev  = CaptionBeginMask(pDC, pMask);
    modeTextPrev = ID2D1DeviceContext_GetTextAntialiasMode(pDC);
    ID2D1DeviceContext_SetTextAntialiasMode(pDC, D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    ID2D1SolidColorBrush_SetColor(pCaption->brush, &CAPTION_CF_WHITE);
    ID2D1DeviceContext_DrawGlyphRun(pDC, ptOrigin, &run, (ID2D1Brush*)pCaption->brush, DWRITE_MEASURING_MODE_NATURAL);
    ID2D1DeviceContext_SetTextAntialiasMode(pDC, modeTextPrev);
    CaptionEndMask(pDC, pTargetPrev);
}

/* Windows 10's pfnRenderToggleMask: the dark-mode glyph from the font, but in light mode the thinned
   moon, rasterized for the DPI and blitted into the mask pixel-snapped from the run's baseline origin. */
static FORCEINLINE void CaptionRenderToggleMaskMoon(CAPTION* pCaption, ID2D1DeviceContext* pDC, ID2D1Bitmap1* pMask, CAPTIONGLYPH glyph, SIZE cell, FLOAT flScale)
{
    ID2D1Factory*      pFactory;
    ID2D1PathGeometry* pMoon;
    ID2D1Bitmap1*      pMoonBitmap;
    ID2D1Image*        pTargetPrev;
    DWRITE_GLYPH_RUN   run;
    D2D1_POINT_2F      ptOrigin;
    D2D1_POINT_2F      ptMoon;
    D2D1_RECT_F        rf;
    D2D1_SIZE_U        size;

    if (CG_LIGHT_MODE == glyph)
    {
        ID2D1DeviceContext_GetFactory(pDC, &pFactory);
        pMoon       = CaptionCreateMoonGeometry(pFactory, pCaption->fontFace, pCaption->glyphIndices[CG_LIGHT_MODE]);
        pMoonBitmap = CaptionRasterizeMoon(pDC, pCaption->brush, pMoon, flScale, &ptMoon);

        run      = CaptionToggleRun(pCaption, glyph, flScale);
        ptOrigin = CaptionToggleOrigin(pDC, &run, cell, glyph);
        ID2D1Bitmap1_GetPixelSize(pMoonBitmap, &size);
        rf.left   = (FLOAT)(LONG)(ptOrigin.x + (ptMoon.x * flScale) + CAPTION_HALF_PIXEL);
        rf.top    = (FLOAT)(LONG)(ptOrigin.y + (ptMoon.y * flScale) + CAPTION_HALF_PIXEL);
        rf.right  = rf.left + (FLOAT)size.width;
        rf.bottom = rf.top + (FLOAT)size.height;

        pTargetPrev = CaptionBeginMask(pDC, pMask);
        ID2D1DeviceContext_DrawBitmap(pDC, (ID2D1Bitmap*)pMoonBitmap, &rf, 1.0F, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
        CaptionEndMask(pDC, pTargetPrev);

        CaptionRelease((IUnknown**)&pMoonBitmap);
        CaptionRelease((IUnknown**)&pMoon);
        CaptionRelease((IUnknown**)&pFactory);
    }
    else
    {
        CaptionRenderToggleGlyphMask(pCaption, pDC, pMask, glyph, cell, flScale);
    }
}

/* Both modes' toggle glyphs, each rendered into a mask the size of the button cell for the DPI, the
   platform's way. Retargets the context, so this runs outside the frame's BeginDraw/EndDraw. */
static FORCEINLINE void CaptionLoadToggleMasks(CAPTION* pCaption, ID2D1DeviceContext* pDC, SIZE cell, UINT dpi)
{
    ID2D1Bitmap1** ppMask;
    D2D1_SIZE_U    size;
    CAPTIONGLYPH   glyph;
    FLOAT          flScale;
    UINT           cMasks;

    flScale              = DpiScale(dpi);
    size.width           = (UINT32)cell.cx;
    size.height          = (UINT32)cell.cy;
    pCaption->toggleCell = cell;

    ppMask = pCaption->toggleMasks;
    glyph  = CG_DARK_MODE;
    for (cMasks = CG_COUNT; cMasks != 0U; --cMasks, ++ppMask, ++glyph)
    {
        CaptionRelease((IUnknown**)ppMask);
        *ppMask = CaptionCreateTargetBitmap(pDC, size);
        pCaption->platform->pfnRenderToggleMask(pCaption, pDC, *ppMask, glyph, cell, flScale);
    }
}

/* The light/dark toggle: the highlight, then the mode's glyph mask filled in the foreground shade, lit
   by hover or press. */
static FORCEINLINE void CaptionDrawToggle(const CAPTION* pCaption, ID2D1DeviceContext* pDC, const RECT* prc, CAPTIONBUTTONSTATE state)
{
    const IMMERSIVEWINDOW* pWindow;
    ID2D1Bitmap1*          pMask;
    D2D1_COLOR_F           cf;
    D2D1_RECT_F            rf;

    pWindow = pCaption->window;
    CaptionDrawToggleHighlight(pCaption, pDC, prc, state);

    cf   = CaptionForeground(pWindow);
    cf.a = CaptionOpacity(pWindow, CBST_NORMAL != state);
    ID2D1SolidColorBrush_SetColor(pCaption->brush, &cf);
    if (pWindow->fLightMode)
    {
        pMask = pCaption->toggleMasks[CG_LIGHT_MODE];
    }
    else
    {
        pMask = pCaption->toggleMasks[CG_DARK_MODE];
    }
    rf = CaptionRectF(prc);
    CaptionFillMask(pDC, pMask, pCaption->brush, &rf);
}

/* ---- rendering: atlas buttons ----------------------------------------------------------------- */

/* A button's background part: close has its own; minimize and maximize share one per theme. An
   inactive window uses the part after it. */
static FORCEINLINE UINT CaptionBackgroundPart(const IMMERSIVEWINDOW* pWindow, CAPTIONBUTTON button)
{
    UINT uPart;

    if (CB_CLOSEBUTTON == button)
    {
        uPart = CAPTION_WP_CLOSE;
    }
    else if (pWindow->fLightMode)
    {
        uPart = CAPTION_WP_MINMAX_LIGHT;
    }
    else
    {
        uPart = CAPTION_WP_MINMAX_DARK;
    }
    if (!pWindow->fActive)
    {
        uPart += CAPTION_WP_INACTIVE;
    }
    return uPart;
}

/* A button's glyph part: minimize, maximize or restore, close; the dark theme's set; the DPI variant. */
static FORCEINLINE UINT CaptionGlyphPart(const IMMERSIVEWINDOW* pWindow, CAPTIONBUTTON button, BOOL fZoomed, UINT dpi)
{
    UINT uPart;

    if (CB_CLOSEBUTTON == button)
    {
        uPart = CAPTION_WP_GLYPH_CLOSE;
    }
    else if (CB_MINBUTTON == button)
    {
        uPart = CAPTION_WP_GLYPH_MIN;
    }
    else if (fZoomed)
    {
        uPart = CAPTION_WP_GLYPH_RESTORE;
    }
    else
    {
        uPart = CAPTION_WP_GLYPH_MAX;
    }
    if (!pWindow->fLightMode)
    {
        uPart += CAPTION_WP_GLYPH_DARK_OFFSET;
    }
    return uPart + CaptionAtlasVariant(dpi);
}

/* One atlas caption button (minimize / maximize-restore / close): the 9-sliced background for the
   theme, activation and hover/press state, then the glyph sprite, lit by hover only. */
static FORCEINLINE void CaptionDrawAtlasButton(const CAPTION* pCaption, ID2D1DeviceContext* pDC, const RECT* prc, CAPTIONBUTTON button, CAPTIONBUTTONSTATE state, BOOL fZoomed, UINT dpi)
{
    const IMMERSIVEWINDOW* pWindow;
    RECT                   rcGlyph;
    UINT                   uState;
    UINT                   uBackgroundPart;
    UINT                   uGlyphPart;
    BOOL                   fMinimize;

    pWindow         = pCaption->window;
    uState          = CaptionAtlasState(state);
    uBackgroundPart = CaptionBackgroundPart(pWindow, button);
    uGlyphPart      = CaptionGlyphPart(pWindow, button, fZoomed, dpi);
    fMinimize       = CB_MINBUTTON == button;
    rcGlyph         = *prc;
    if (fZoomed && (button == CB_CLOSEBUTTON))
    {
        rcGlyph.right = rcGlyph.left + pWindow->metrics.cxCaptionButton + pWindow->metrics.cxCaptionOuter;
    }

    CaptionDrawAtlasBackground(pDC, &pCaption->atlas, uBackgroundPart, uState, prc);
    CaptionDrawAtlasGlyph(pDC, &pCaption->atlas, uGlyphPart, uState, &rcGlyph, fMinimize, CaptionOpacity(pWindow, CBST_HOT == state));
}

/* The caption button cluster, left to right: light/dark toggle, minimize, maximize/restore, close, each
   in the state the window reports for it. */
static FORCEINLINE void CaptionDrawButtons(const CAPTION* pCaption, ID2D1DeviceContext* pDC, const CAPTIONLAYOUT* pLayout, BOOL fZoomed)
{
    const IMMERSIVEWINDOW* pWindow;
    const RECT*            prc;
    CAPTIONBUTTON          button;
    CAPTIONBUTTONSTATE     state;
    UINT                   cButtons;

    pWindow = pCaption->window;
    prc     = &pLayout->buttons[CB_LIGHTDARKBUTTON];
    state   = GetCaptionButtonState(pWindow, CB_LIGHTDARKBUTTON);
    CaptionDrawToggle(pCaption, pDC, prc, state);

    button = CB_MINBUTTON;
    ++prc;
    for (cButtons = CB_CLOSEBUTTON - CB_MINBUTTON + 1U; cButtons != 0U; --cButtons, ++button, ++prc)
    {
        state = GetCaptionButtonState(pWindow, button);
        CaptionDrawAtlasButton(pCaption, pDC, prc, button, state, fZoomed, pLayout->dpi);
    }
}

/* ---- recording --------------------------------------------------------------------------------- */

/* Record the caption's draw for the layout and the window's current state: the icon, the title, the
   platform's top border, then the button cluster -- bitmap fills and whole-pixel rectangles, so the
   recording runs aliased (which the mask fills require) -- into a fresh command list, and note what it
   was recorded for. Retargets the context, so this runs outside the frame's BeginDraw/EndDraw. */
static FORCEINLINE void CaptionRecord(CAPTION* pCaption, ID2D1DeviceContext* pDC, const CAPTIONLAYOUT* pLayout, BOOL fZoomed)
{
    const IMMERSIVEWINDOW* pWindow;
    CAPTIONRECORDING*      pRecording;
    ID2D1Image*            pTargetPrev;
    D2D1_ANTIALIAS_MODE    modePrev;

    pWindow    = pCaption->window;
    pRecording = &pCaption->recording;
    CaptionRelease((IUnknown**)&pRecording->list);
    ASSERT(SUCCEEDED(ID2D1DeviceContext_CreateCommandList(pDC, &pRecording->list)));

    ID2D1DeviceContext_GetTarget(pDC, &pTargetPrev);
    ID2D1DeviceContext_SetTarget(pDC, (ID2D1Image*)pRecording->list);
    ID2D1DeviceContext_BeginDraw(pDC);
    modePrev = ID2D1DeviceContext_GetAntialiasMode(pDC);
    ID2D1DeviceContext_SetAntialiasMode(pDC, D2D1_ANTIALIAS_MODE_ALIASED);

    CaptionDrawIcon(pCaption, pDC, pLayout);
    CaptionDrawTitle(pCaption, pDC);
    pCaption->platform->pfnDrawTopBorder(pCaption, pDC, pLayout);
    CaptionDrawButtons(pCaption, pDC, pLayout, fZoomed);

    ID2D1DeviceContext_SetAntialiasMode(pDC, modePrev);
    ASSERT(SUCCEEDED(ID2D1DeviceContext_EndDraw(pDC, NULL, NULL)));
    ID2D1DeviceContext_SetTarget(pDC, pTargetPrev);
    CaptionRelease((IUnknown**)&pTargetPrev);
    ASSERT(SUCCEEDED(ID2D1CommandList_Close(pRecording->list)));

    CaptionCopyLayout(&pRecording->layout, pLayout);
    pRecording->hotButton     = pWindow->hotButton;
    pRecording->pressedButton = pWindow->pressedButton;
    pRecording->fLightMode    = pWindow->fLightMode;
    pRecording->fActive       = pWindow->fActive;
    pRecording->fZoomed       = fZoomed;
}

/* ---- platforms --------------------------------------------------------------------------------- */

/* Windows 10: Segoe MDL2 Assets, whose light-mode glyph mask is the moon rasterized per DPI, and a top
   border DWM leaves to the window. */
static const CAPTIONPLATFORM CAPTION_PLATFORM_WINDOWS10 =
{
    L"Segoe MDL2 Assets",
    CaptionRenderToggleMaskMoon,
    CaptionDrawTopBorder,
};

/* Windows 11: Segoe Fluent Icons straight from the font, and DWM's own top border. */
static const CAPTIONPLATFORM CAPTION_PLATFORM_WINDOWS11 =
{
    L"Segoe Fluent Icons",
    CaptionRenderToggleGlyphMask,
    CaptionDrawNoTopBorder,
};

/* ---- interface --------------------------------------------------------------------------------- */

/* Pick the platform and create what never changes: the WIC and DWrite factories, the shared brush and
   the toggle glyph font. Everything that follows the window is built by the first PrepareWindowCaption
   (the revision and DPI start at zero, behind any window's). */
void InitCaption(CAPTION* pCaption, const IMMERSIVEWINDOW* pWindow, ID2D1DeviceContext* pDC)
{
    SecureZeroMemory(pCaption, sizeof(*pCaption));
    pCaption->window = pWindow;
    if (IsWindows11OrGreaterEx())
    {
        pCaption->platform = &CAPTION_PLATFORM_WINDOWS11;
    }
    else
    {
        pCaption->platform = &CAPTION_PLATFORM_WINDOWS10;
    }

    ASSERT(SUCCEEDED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER, &IID_IWICImagingFactory, (void**)&pCaption->wic)));
    ASSERT(SUCCEEDED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory, (IUnknown**)&pCaption->dwrite)));
    pCaption->textParams = CaptionCreateTextParams(pCaption->dwrite, 1.0F, 0.0F);
    pCaption->titleParams = CaptionCreateTextParams(pCaption->dwrite, CAPTION_TITLE_GAMMA, 1.0F);
    ASSERT(SUCCEEDED(ID2D1DeviceContext_CreateSolidColorBrush(pDC, &CAPTION_CF_WHITE, NULL, &pCaption->brush)));
    pCaption->fontFace = CaptionCreateToggleFont(pCaption->dwrite, pCaption->platform->pszToggleFontFamily, pCaption->glyphIndices);
}

/* Bring the resources up to date with the window before the frame draws: a moved caption revision
   reloads the icon and the atlas; a moved DPI or toggle cell re-renders the toggle glyph masks; the
   title is laid out and rendered again when its text, its font's DPI or its room changed. Then the
   recording: made again when any of those were rebuilt, when the layout moved, or when the window's
   state (hover, press, theme, activation, maximized) moved since it was made. */
void PrepareWindowCaption(CAPTION* pCaption, ID2D1DeviceContext* pDC, const CAPTIONLAYOUT* pLayout)
{
    const IMMERSIVEWINDOW*  pWindow;
    const CAPTIONRECORDING* pRecording;
    const RECT*             prcToggle;
    SIZE                    cell;
    LONG                    xLeft;
    BOOL                    fZoomed;
    BOOL                    fCaptionRevisionChanged;
    BOOL                    fDpiChanged;
    BOOL                    fToggleCellChanged;
    BOOL                    fTitleRoomChanged;
    BOOL                    fResourcesRebuilt;
    BOOL                    fLayoutChanged;
    BOOL                    fStateChanged;
    BOOL                    fNeverRecorded;

    pWindow    = pCaption->window;
    pRecording = &pCaption->recording;
    prcToggle  = &pLayout->buttons[CB_LIGHTDARKBUTTON];
    ID2D1DeviceContext_SetTextRenderingParams(pDC, (IDWriteRenderingParams*)pCaption->textParams);
    cell.cx    = prcToggle->right - prcToggle->left;
    cell.cy    = prcToggle->bottom - prcToggle->top;
    fZoomed    = IsZoomed(pWindow->hwnd);

    fCaptionRevisionChanged = pCaption->revision != pWindow->captionRevision;
    fDpiChanged             = pCaption->dpi != pLayout->dpi;
    fToggleCellChanged      = (pCaption->toggleCell.cx != cell.cx) || (pCaption->toggleCell.cy != cell.cy);
    fTitleRoomChanged       = (pCaption->text.size.cx != CaptionTitleRoom(pLayout, &xLeft)) || (pCaption->text.size.cy != pLayout->cyCaption);
    fResourcesRebuilt       = fCaptionRevisionChanged || fDpiChanged || fToggleCellChanged || fTitleRoomChanged;
    fLayoutChanged          = !CaptionLayoutEquals(&pRecording->layout, pLayout);
    fStateChanged           = (pRecording->fLightMode != pWindow->fLightMode) ||
                              (pRecording->fActive != pWindow->fActive) ||
                              (pRecording->hotButton != pWindow->hotButton) ||
                              (pRecording->pressedButton != pWindow->pressedButton) ||
                              (pRecording->fZoomed != fZoomed);
    fNeverRecorded          = NULL == pRecording->list;

    if (fCaptionRevisionChanged)
    {
        CaptionLoadIcon(pCaption, pDC);
        CaptionLoadAtlas(pCaption, pDC);
    }
    if (fDpiChanged || fToggleCellChanged)
    {
        CaptionLoadToggleMasks(pCaption, pDC, cell, pLayout->dpi);
    }
    if (fCaptionRevisionChanged || fDpiChanged || fTitleRoomChanged || (pRecording->fLightMode != pWindow->fLightMode))
    {
        CaptionLayoutTitle(pCaption, pDC, pLayout);
    }
    if (fResourcesRebuilt || fLayoutChanged || fStateChanged || fNeverRecorded)
    {
        CaptionRecord(pCaption, pDC, pLayout, fZoomed);
    }

    pCaption->revision = pWindow->captionRevision;
    pCaption->dpi      = pLayout->dpi;
}

/* Draw the caption into the context, inside the frame's BeginDraw/EndDraw: the recording, replayed at
   the origin. */
extern FORCEINLINE void DrawWindowCaption(CAPTION* pCaption, ID2D1DeviceContext* pDC)
{
    ID2D1DeviceContext_DrawImage(pDC, (ID2D1Image*)pCaption->recording.list, NULL, NULL, D2D1_INTERPOLATION_MODE_LINEAR, D2D1_COMPOSITE_MODE_SOURCE_OVER);
}

void DestroyCaption(CAPTION* pCaption)
{
    CaptionRelease((IUnknown**)&pCaption->recording.list);
    CaptionRelease((IUnknown**)&pCaption->atlas.bitmap);
    CaptionRelease((IUnknown**)&pCaption->text.mask);
    CaptionRelease((IUnknown**)&pCaption->toggleMasks[CG_DARK_MODE]);
    CaptionRelease((IUnknown**)&pCaption->toggleMasks[CG_LIGHT_MODE]);
    CaptionRelease((IUnknown**)&pCaption->iconBitmap);
    CaptionRelease((IUnknown**)&pCaption->fontFace);
    CaptionRelease((IUnknown**)&pCaption->brush);
    CaptionRelease((IUnknown**)&pCaption->titleParams);
    CaptionRelease((IUnknown**)&pCaption->textParams);
    CaptionRelease((IUnknown**)&pCaption->dwrite);
    CaptionRelease((IUnknown**)&pCaption->wic);
}
// NOLINTEND(misc-include-cleaner)
