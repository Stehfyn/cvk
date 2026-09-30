#pragma once

#include <windows.h>
#include <uxtheme.h>
#pragma warning(push)
#pragma warning(disable : 4201) /* nameless struct/union in the DirectX headers */
#include <wincodec.h>
#include <cd2d.h>
#pragma warning(pop)
#include "window.h" /* IMMERSIVEWINDOW, which owns the CAPTION; CAPTIONLAYOUT and CAPTIONBUTTON, which the recording follows */

typedef struct CAPTIONPLATFORM CAPTIONPLATFORM; /* caption.c: the Windows 10 / Windows 11 function table */

/* "DWMWindow" theme class part count: the caption-button atlas uDWM composites from. */
#define CAPTION_ATLAS_PARTS 92

/* The light/dark toggle glyph, by the mode it is shown in. */
typedef enum CAPTIONGLYPH
{
    CG_DARK_MODE = 0,
    CG_LIGHT_MODE,
    CG_COUNT
} CAPTIONGLYPH;

/* The DWMWindow caption atlas, decoded from the current theme. */
typedef struct CAPTIONATLAS
{
    ID2D1Bitmap* bitmap;
    RECT         partRects[CAPTION_ATLAS_PARTS];     /* TMT_ATLASRECT: the part's four-state strip */
    MARGINS      sizingMargins[CAPTION_ATLAS_PARTS]; /* TMT_SIZINGMARGINS (background parts only)  */
} CAPTIONATLAS;

/* The caption title, laid out and rendered for the frame as the coverage the draw fills through. */
typedef struct CAPTIONTEXT
{
    ID2D1Bitmap1* mask;   /* the title's coverage, the room wide by the band tall; NULL when there is no
                             title, or no room for one                                                 */
    D2D1_RECT_F   rfDest; /* where it is filled: after the icon's gap, the full band                    */
    SIZE          size;   /* the room and band it was laid out for                                     */
} CAPTIONTEXT;

/* The caption's draw, recorded as a command list, with the geometry and window state it was recorded
   for. A frame whose layout or state differs, or whose resources were rebuilt, records it again. */
typedef struct CAPTIONRECORDING
{
    ID2D1CommandList* list;          /* the bitmap fills, replayed per frame; NULL until first recorded */
    CAPTIONLAYOUT     layout;        /* the geometry recorded for                                       */
    CAPTIONBUTTON     hotButton;     /* the window state recorded for: hover, press, theme, activation, */
    CAPTIONBUTTON     pressedButton; /* and whether the window is maximized                             */
    BOOL              fLightMode;
    BOOL              fActive;
    BOOL              fZoomed;
} CAPTIONRECORDING;

/* The caption DWM would have drawn for the borderless window: the system icon, the DWrite title, the
   caption-button sprites straight out of the DWMWindow theme atlas, the light/dark toggle glyph, and a
   1px top border on Windows 10. Its resources are created by InitCaption and, as the window's caption
   revision, the DPI or the width move, by PrepareWindowCaption before a frame's draw opens, which also
   records the draw (bitmap fills alone) into a command list whenever the geometry or the window state
   moved; nothing is created or rasterized while drawing, which replays the recording. Several KB (the
   atlas tables), so keep it off the stack. */
typedef struct CAPTION
{
    const IMMERSIVEWINDOW* window;     /* owned by the window thread                                  */
    const CAPTIONPLATFORM* platform;   /* Windows 10 or 11, picked once by InitCaption                */
    IWICImagingFactory*    wic;        /* decodes the icon and the atlas                              */
    IDWriteFactory*        dwrite;     /* the toggle font and the title                               */
    IDWriteRenderingParams2* textParams;  /* unadjusted toggle glyph coverage                         */
    IDWriteRenderingParams2* titleParams; /* DWM's title gamma and grayscale contrast                 */
    ID2D1SolidColorBrush*  brush;      /* shared; recolored before every use                          */
    IDWriteFontFace*       fontFace;   /* the light/dark toggle glyph font                            */
    UINT16                 glyphIndices[CG_COUNT];
    ID2D1Bitmap*           iconBitmap; /* the caption system icon (IMMERSIVEWINDOW.captionIcon)       */
    ID2D1Bitmap1*          toggleMasks[CG_COUNT]; /* the toggle glyph's coverage per mode, one button
                                                     cell each, rendered for the DPI                  */
    SIZE                   toggleCell; /* the button cell the toggle masks were rendered for          */
    CAPTIONTEXT            text;
    CAPTIONATLAS           atlas;
    CAPTIONRECORDING       recording;
    UINT                   revision;   /* the captionRevision the icon, atlas and title follow        */
    UINT                   dpi;        /* the DPI the toggle masks and the title's font follow        */
} CAPTION;

/* The calling thread must belong to a COM apartment (WIC), and the caption then belongs to that thread
   and this context. */
void
InitCaption(
    CAPTION*               caption,
    const IMMERSIVEWINDOW* window,
    ID2D1DeviceContext*    context);

/* Before a frame's draw opens: bring the resources up to date with the window's caption revision and
   the frame's layout (its DPI and width), and the recording up to date with both and the window's
   state. Rebuilds and recording retarget the context. */
void
PrepareWindowCaption(
    CAPTION*             caption,
    ID2D1DeviceContext*  context,
    const CAPTIONLAYOUT* layout);

/* Inside the frame's draw: replay the caption as prepared. */
void
DrawWindowCaption(
    CAPTION*            caption,
    ID2D1DeviceContext* context);
