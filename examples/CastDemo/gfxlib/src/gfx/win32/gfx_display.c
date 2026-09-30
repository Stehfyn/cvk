/*=================================================================================================
** 1. References
**===============================================================================================*/

/*=================================================================================================
** 2. INCLUDE FILES
**===============================================================================================*/

#include <Windows.h>

#include "gfx/gfx_display_internal.h"
#include "os/os_assert.h"
#include "os/os_malloc.h"

#include <string.h>

/*=================================================================================================
** 3. DECLARATIONS
**
** 3.1 Macros
**===============================================================================================*/

#define ARROW_CURSOR_RESOURCE_ID (32512u) ///< System resource identifier for the standard arrow cursor.

/*=================================================================================================
** 3.2 Types
**===============================================================================================*/

/*=================================================================================================
** 3.3 External global variables
**===============================================================================================*/

/*=================================================================================================
** 3.4 Static global variables
**===============================================================================================*/

static const wchar_t c_displayWindowClassName[] = L"VulkanVaZeroCopyDisplay";
static const wchar_t c_displayWindowName[]      = L"Vulkan VA zero-copy";

/*=================================================================================================
** 3.5 Static function prototypes
**===============================================================================================*/

/*************************************************************************************************/
/**
 * \brief Converts terminal window messages into application quit state.
 *
 * \param[in] window         Window receiving the message
 * \param[in] message        Win32 message identifier
 * \param[in] wparam         Message-specific word parameter
 * \param[in] lparam         Message-specific long parameter
 *
 * \return Zero for handled WM_CLOSE and WM_DESTROY messages; otherwise the result from
 *         DefWindowProcW().
 */
static LRESULT CALLBACK ProcessWindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam);

/*************************************************************************************************/
/**
 * \brief Verifies the common display creation contract.
 *
 * \param[in] pCreateInfo        Non-NULL configuration selecting a supported display kind
 *
 * \retval    None No value is returned.
 */
static void ValidateCreateInfo(const Gfx_Display_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Creates and shows the native Win32 presentation window.
 *
 * \param[out] pDisplay           Receives client dimensions, module handle, and window handle
 * \param[in]  pCreateInfo        Requested client extent
 *
 * \retval    None No value is returned.
 */
static void CreateWindowedDisplay(Gfx_Display_t* pDisplay, const Gfx_Display_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Initializes a display and creates native state when windowed presentation is selected.
 *
 * \param[out] pDisplay           Zero-initialized display storage to initialize as running
 * \param[in]  pCreateInfo        Valid display-kind selection
 *
 * \retval    None No value is returned.
 */
static void CreateDisplay(Gfx_Display_t* pDisplay, const Gfx_Display_CreateInfo_t* pCreateInfo);

/*************************************************************************************************/
/**
 * \brief Drains pending window messages and updates the display run state.
 *
 * \param[in,out] pDisplay        Display whose Win32 queue is processed; marked stopped on WM_QUIT
 *
 * \retval TRUE                   The display remains active
 * \retval FALSE                  A quit message has stopped the display
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
static LRESULT CALLBACK ProcessWindowMessage(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    switch (message)
    {
    case WM_CLOSE:
        PostQuitMessage(0);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProcW(window, message, wparam, lparam);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static void ValidateCreateInfo(const Gfx_Display_CreateInfo_t* pCreateInfo)
{
    LOGASSERT(NULL != pCreateInfo);
    LOGASSERT((GFX_DISPLAY_KIND_WINDOWED == pCreateInfo->kind) || (GFX_DISPLAY_KIND_DIRECT == pCreateInfo->kind) ||
              (GFX_DISPLAY_KIND_HEADLESS == pCreateInfo->kind));
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
    int         window_class_registered;
    BOOL        rectangle_adjusted;
    BOOL        window_updated;
    BOOL        rectangle_queried;
    WNDCLASSEXW window_class;
    RECT        client_rectangle;
    RECT        window_rectangle;
    HINSTANCE   module;
    HWND        window;
    ATOM        window_class_atom;
    DWORD       window_style;

    module = GetModuleHandleW(NULL);
    LOGASSERT(NULL != module);

    (void)memset(&window_class, 0, sizeof(window_class));
    window_class.cbSize        = sizeof(window_class);
    window_class.style         = CS_HREDRAW | CS_VREDRAW;
    window_class.lpfnWndProc   = ProcessWindowMessage;
    window_class.hInstance     = module;
    window_class.hCursor       = LoadCursorW(NULL, MAKEINTRESOURCEW(ARROW_CURSOR_RESOURCE_ID));
    window_class.lpszClassName = c_displayWindowClassName;
    window_class_atom          = RegisterClassExW(&window_class);
    window_class_registered    = (0 != window_class_atom) || (ERROR_CLASS_ALREADY_EXISTS == GetLastError());
    LOGASSERT(window_class_registered);

    window_style            = WS_OVERLAPPEDWINDOW;
    window_rectangle.left   = 0;
    window_rectangle.top    = 0;
    window_rectangle.right  = (LONG)pCreateInfo->width;
    window_rectangle.bottom = (LONG)pCreateInfo->height;
    rectangle_adjusted      = AdjustWindowRectEx(&window_rectangle, window_style, FALSE, 0);
    LOGASSERT(FALSE != rectangle_adjusted);
    window = CreateWindowExW(0,
                             c_displayWindowClassName,
                             c_displayWindowName,
                             window_style,
                             CW_USEDEFAULT,
                             CW_USEDEFAULT,
                             window_rectangle.right - window_rectangle.left,
                             window_rectangle.bottom - window_rectangle.top,
                             NULL,
                             NULL,
                             module,
                             NULL);
    LOGASSERT(NULL != window);

    ShowWindow(window, SW_SHOW);
    window_updated = UpdateWindow(window);
    LOGASSERT(FALSE != window_updated);
    rectangle_queried = GetClientRect(window, &client_rectangle);
    LOGASSERT(FALSE != rectangle_queried);
    LOGASSERT((0 < client_rectangle.right) && (0 < client_rectangle.bottom));

    pDisplay->width         = (uint32_t)client_rectangle.right;
    pDisplay->height        = (uint32_t)client_rectangle.bottom;
    pDisplay->nativeDisplay = (uintptr_t)module;
    pDisplay->nativeWindow  = (uintptr_t)window;
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
    MSG message;

    LOGASSERT(NULL != pDisplay);

    if (GFX_DISPLAY_KIND_WINDOWED != pDisplay->kind)
    {
        return pDisplay->running;
    }
    while (FALSE != PeekMessageW(&message, NULL, 0, 0, PM_REMOVE))
    {
        if (WM_QUIT == message.message)
        {
            pDisplay->running = FALSE;
            break;
        }
        (void)TranslateMessage(&message);
        (void)DispatchMessageW(&message);
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
