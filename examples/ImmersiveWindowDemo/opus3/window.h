#ifndef IMMERSIVE_OPUS3_WINDOW_H
#define IMMERSIVE_OPUS3_WINDOW_H

#include <Windows.h>
#include "swapchain.h"

typedef struct CAPTION CAPTION; /* caption.h, which builds on this header's layout and button types */

#define WC_IMMERSIVEWINDOW TEXT("Opus3BorderlessWindow")
#define IMMERSIVE_CAPTION_TITLE_CAPACITY (256)

typedef enum CAPTIONBUTTON
{
    CB_NONE = -1,
    CB_LIGHTDARKBUTTON,
    CB_MINBUTTON,
    CB_MAXBUTTON,
    CB_CLOSEBUTTON,
    CB_ICON,
    CB_COUNT
} CAPTIONBUTTON;

/* The DPI-dependent metrics the caption is laid out from, sampled on the window thread whenever they
   can change (creation, DPI, settings, theme, DWM composition, frame style) and read everywhere else,
   so a frame or a hit test never asks the system for them. */
typedef struct WINDOWMETRICS
{
    UINT dpi;
    SIZE border;          /* the resize border per side: frame plus padded border          */
    SIZE thinBorder;      /* SM_CXBORDER / SM_CYBORDER                                     */
    SIZE smallIcon;       /* SM_CXSMICON / SM_CYSMICON                                     */
    LONG cyCaption;        /* the caption band's height                                          */
    LONG cxCaptionButton;  /* a caption button's width                                           */
    LONG cxCaptionOuter;   /* the columns the cluster's outer buttons (close, minimize) run past  */
                           /* it on their outer side                                             */
    LONG xTitle;           /* the title text's origin                                            */
    LONG cySize;           /* SM_CYSIZE: the system menu's hit square                            */
    LONG cyTopBorder;      /* DWM's visible top border rows: resizing, not buttons               */
} WINDOWMETRICS;

typedef struct IMMERSIVEWINDOW
{
    HWND               hwnd;
    WNDPROC            clientProc;
    void*              clientData;
    WINDOWMETRICS      metrics;
    BOOL               fBorderless;
    BOOL               fShadow;
    BOOL               fFullscreen;
    WINDOWPLACEMENT    windowedPlacement;
    RECT               windowedRect;
    SRWLOCK            presentLock;
    CONDITION_VARIABLE presentCondition;
    RECT               rcClient;
    BOOL               fPresentReady;
    BOOL               fGeometryPending;
    CAPTIONBUTTON      hotButton;
    CAPTIONBUTTON      pressedButton;
    BOOL               fLightMode;
    BOOL               fActive;
    WCHAR              captionTitle[IMMERSIVE_CAPTION_TITLE_CAPACITY];
    HICON              captionIcon;      /* the icon DWM would show, at the DPI's small size          */
    HICON              captionIconOwned; /* captionIcon when it is a copy this window made, to destroy */
    HICON              smallIconSource;  /* caller-owned WM_SETICON source, never the resized copy   */
    UINT               captionRevision; /* bumped by a title, icon, theme or DPI change       */
    SWAPCHAIN*         swapChain;  /* set by the app once bound to hwnd; the paint's target  */
    CAPTION*           caption;    /* set by the app once built; EndImmersivePaint draws it */
    ULONGLONG          paintCount; /* frames painted; paces the visual's border mode        */
} IMMERSIVEWINDOW;

typedef struct IMMERSIVEWINDOWCREATEINFO
{
    WNDPROC      clientProc;
    void*        clientData;
    const WCHAR* title;
    int          width;
    int          height;
    int          showCommand;
} IMMERSIVEWINDOWCREATEINFO;

/* The borderless caption's geometry in client pixels for a client width, laid out from the window's
   cached metrics so that hit-testing and drawing agree. Built per hit test and per frame. */
typedef struct CAPTIONLAYOUT
{
    UINT dpi;
    LONG cyCaption;         /* the caption band's height                                        */
    LONG cyTopBorder;       /* DWM's visible top border rows: resizing, not buttons             */
    LONG xTitle;            /* the title text's origin                                          */
    RECT buttons[CB_COUNT]; /* by CAPTIONBUTTON: the four buttons and, at CB_ICON, the icon slot */
    RECT sysMenu;           /* the system menu's hit area: DWM's, a caption-button-tall square  */
                            /* at the left, one row up from the band's bottom                   */
} CAPTIONLAYOUT;

/* How a caption button is shown: hot while under the cursor with no other button holding the press,
   pressed while under the cursor holding the press. */
typedef enum CAPTIONBUTTONSTATE
{
    CBST_NORMAL = 0,
    CBST_HOT,
    CBST_PRESSED
} CAPTIONBUTTONSTATE;

CAPTIONBUTTONSTATE
GetCaptionButtonState(
    const IMMERSIVEWINDOW* window,
    CAPTIONBUTTON          button);

void
CreateImmersiveWindow(
    IMMERSIVEWINDOW*                 window,
    const IMMERSIVEWINDOWCREATEINFO* createInfo);

/* One frame's state, filled by BeginImmersivePaint and consumed by EndImmersivePaint, as PAINTSTRUCT is
   by Begin/EndPaint. Between the two the window's geometry holds still and the context is inside
   BeginDraw/EndDraw: the caller only draws the client content. */
typedef struct IMMERSIVEPAINTSTRUCT
{
    ID2D1DeviceContext* context;  /* the swap chain's Direct2D context, inside BeginDraw/EndDraw          */
    RECT                rcClient; /* the client area the frame is laid out for                           */
    CAPTIONLAYOUT       layout;   /* the caption's geometry (all zero unless borderless); the client      */
                                  /* content sits below layout.cyCaption                                 */
} IMMERSIVEPAINTSTRUCT;

/* Bracket one frame. BeginImmersivePaint holds the window's geometry still, fills *ps, opens the draw
   on the window's swap chain and erases the client area, returning the context to draw into.
   EndImmersivePaint draws the caption over the client content, closes the draw, presents, commits the
   swap chain (waiting for DWM to take the commit until the window has reported its first presented
   frame) and lets the geometry move again. */
ID2D1DeviceContext*
BeginImmersivePaint(
    IMMERSIVEWINDOW*      window,
    IMMERSIVEPAINTSTRUCT* ps);

void
EndImmersivePaint(
    IMMERSIVEWINDOW*            window,
    const IMMERSIVEPAINTSTRUCT* ps);

#endif /* IMMERSIVE_OPUS3_WINDOW_H */
