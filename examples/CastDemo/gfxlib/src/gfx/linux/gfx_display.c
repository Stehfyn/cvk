/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include "gfx/gfx_display_internal.h"
#include "os/os_assert.h"
#include "os/os_malloc.h"

#include <X11/Xlib.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static const char c_displayWindowName[] = "Vulkan VA zero-copy";

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Validates the requested display environment.
 *
 * \param[in] pCreateInfo        Request for windowed X11, direct-display, or headless operation
 *
 * \retval    None
 */
static void ValidateCreateInfo(const Gfx_Display_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Creates an X11 window with the requested extent and WM close protocol.
 *
 * \param[out] pDisplay        Display receiving X Display, Window, WM_DELETE_WINDOW atom, and screen extent
 * \param[in] pCreateInfo      Requested window extent
 *
 * \retval    None
 */
static void CreateWindowedDisplay(Gfx_Display_t* pDisplay, const Gfx_Display_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Initializes common display state and any kind-specific native objects.
 *
 * \param[out] pDisplay        Zero-initialized display marked running
 * \param[in] pCreateInfo      Validated Linux display kind
 *
 * \retval    None
 */
static void CreateDisplay(Gfx_Display_t* pDisplay, const Gfx_Display_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Drains X11 events and detects native window closure.
 *
 * \param[in,out] pDisplay        Display whose running flag clears on server removal or WM_DELETE_WINDOW
 *
 * \return Current running state; non-windowed displays return it without native event processing.
 */
static bool_t ProcessDisplayEvents(Gfx_Display_t* pDisplay);

/*=================================================================================================
** 4. PRIVATE FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ValidateCreateInfo(const Gfx_Display_CreateInfo_t* pCreateInfo)
{
    LOGASSERT(NULL != pCreateInfo);
    LOGASSERT((GFX_DISPLAY_KIND_WINDOWED == pCreateInfo->kind) || (GFX_DISPLAY_KIND_DIRECT == pCreateInfo->kind) || (GFX_DISPLAY_KIND_HEADLESS == pCreateInfo->kind));
    LOGASSERT(0 != pCreateInfo->width);
    LOGASSERT(0 != pCreateInfo->height);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateWindowedDisplay(Gfx_Display_t* pDisplay, const Gfx_Display_CreateInfo_t* pCreateInfo)
{
    int input_selected;
    int window_named;
    int protocols_set;
    int window_mapped;
    int attributes_queried;
    Display*          dpy;
    Window            window;
    Atom              delete_message;
    XEvent            event;
    XWindowAttributes attributes;
    int               screen;

    dpy = XOpenDisplay(NULL);
    LOGASSERT(NULL != dpy);

    screen = DefaultScreen(dpy);
    window = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 0, 0, pCreateInfo->width, pCreateInfo->height, 0, BlackPixel(dpy, screen), BlackPixel(dpy, screen));
    LOGASSERT(0 != window);
    input_selected = XSelectInput(dpy, window, StructureNotifyMask | ExposureMask | KeyPressMask);
    LOGASSERT(0 != input_selected);
    window_named = XStoreName(dpy, window, c_displayWindowName);
    LOGASSERT(0 != window_named);

    delete_message = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    LOGASSERT(None != delete_message);
    protocols_set = XSetWMProtocols(dpy, window, &delete_message, 1);
    LOGASSERT(0 != protocols_set);
    window_mapped = XMapWindow(dpy, window);
    LOGASSERT(0 != window_mapped);
    do
    {
        (void)XWindowEvent(dpy, window, StructureNotifyMask, &event);
    } while (MapNotify != event.type);
    attributes_queried = XGetWindowAttributes(dpy, window, &attributes);
    LOGASSERT(0 != attributes_queried);
    LOGASSERT((0 < attributes.width) && (0 < attributes.height));

    pDisplay->nativeDisplay = (uintptr_t)dpy;
    pDisplay->nativeWindow  = (uintptr_t)window;
    pDisplay->nativeEvent   = (uintptr_t)delete_message;
    pDisplay->width         = (uint32_t)attributes.width;
    pDisplay->height        = (uint32_t)attributes.height;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void CreateDisplay(Gfx_Display_t* pDisplay, const Gfx_Display_CreateInfo_t* pCreateInfo)
{
    ValidateCreateInfo(pCreateInfo);
    pDisplay->kind    = pCreateInfo->kind;
    pDisplay->width   = pCreateInfo->width;
    pDisplay->height  = pCreateInfo->height;
    pDisplay->running = TRUE;
    if (GFX_DISPLAY_KIND_WINDOWED == pCreateInfo->kind)
    {
        CreateWindowedDisplay(pDisplay, pCreateInfo);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static bool_t ProcessDisplayEvents(Gfx_Display_t* pDisplay)
{
    Display* dpy;
    XEvent   event;

    LOGASSERT(NULL != pDisplay);

    if (GFX_DISPLAY_KIND_WINDOWED != pDisplay->kind)
    {
        return pDisplay->running;
    }
    dpy = (Display*)pDisplay->nativeDisplay;
    LOGASSERT(NULL != dpy);

    while (0 != XPending(dpy))
    {
        (void)XNextEvent(dpy, &event);
        if (DestroyNotify == event.type || (ClientMessage == event.type && (Atom)event.xclient.data.l[0] == (Atom)pDisplay->nativeEvent))
        {
            pDisplay->running = FALSE;
        }
    }
    return pDisplay->running;
}

/*=================================================================================================
** 5. PUBLIC FUNCTIONS
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
void Gfx_Display_Create(Gfx_Display_t** ppDisplay, const Gfx_Display_CreateInfo_t* pCreateInfo)
{
    LOGASSERT(NULL != ppDisplay);
    LOGASSERT(NULL != pCreateInfo);

    (*ppDisplay) = (Gfx_Display_t*)OS_CALLOC(sizeof((**ppDisplay)));
    CreateDisplay((*ppDisplay), pCreateInfo);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
bool_t Gfx_Display_ProcessEvents(Gfx_Display_t* pDisplay)
{
    return ProcessDisplayEvents(pDisplay);
}
