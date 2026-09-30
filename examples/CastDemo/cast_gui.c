/* cast_gui.c - CastDemo: a small Win32 window that lists the Cast receivers on the network and
** plays a video on the selected one, with a Play/Pause, Stop and seek toolbar.
**
** The window thread owns the controls. Discovery and the Cast session each run on a worker
** thread and publish into startup-owned context or event-mailbox storage.
**
**   CastDemo [file.mp4]                 preselects the file */
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#include <Windows.h> // IWYU pragma: keep
#include <CommCtrl.h>
#include <commdlg.h>
#include <ole2.h>
#include <shellapi.h>
#include <Uxtheme.h>
#include <dwmapi.h>
#include <winnt.h>
#include <windef.h>
#include <intsafe.h>
#include <minwindef.h>
#include <basetsd.h>
#include <errhandlingapi.h>
#include <winerror.h>
#include <WinBase.h>
#include <stringapiset.h>
#include <WinNls.h>
#include <dcommon.h>
#include <dxgiformat.h>
#include <Unknwnbase.h>
#include <WinUser.h>
#include <minwinbase.h>
#include <windowsx.h>
#include <wingdi.h>
#include <processthreadsapi.h>
#include <fileapi.h>
#include <handleapi.h>
#include <sysinfoapi.h>
#include <dxgi1_6.h>
#include <dxgi.h>
#include <combaseapi.h>
#include <objbase.h>
#include <synchapi.h>
#include <libloaderapi.h>

#include <stdint.h>
#include <stddef.h>

#include "cast_media.h"
#include "gpu_device.h"
#include "gpu_transcode.h"
#include <swapchain.h>
#include <cdcomp.h>
#include "cast_session.h"
#include "os/os_assert.h"
#include "os/os_memory.h"
#include "os/os_process.h"
#include "media_server.h"
#include "cast_network.h"
#include "os/os_network.h"
#include "mp4_index.h"
#include "proxy.h"
#include "ssdp.h"
#include "subtitles.h"
#include "audio_gain.h"
#include "cast_events.h"
#include "os/os_thread.h"
#include "os/win32/os_text.h"
#include "os/win32/os_windowsx.h"
#include "os/os_compiler.h"
#include "os/os_types.h"
#include "crt/crt_memory.h"
#include "utl/utl_decimal.h"
#include "utl/utl_unicode.h"
#include "utl/utl_url.h"
#include "caption.h"
#include "window.h"
#include "cd2d.h"
#include "cdw.h"
#include "cdxgi.h"

#ifdef _MSC_VER
#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

#define PREFERRED_DEVICE    "XGIMI"
#define DEFAULT_SERVER_PORT (8642)
#define DISCOVERY_MS        (3000)
#define SEEK_RANGE          (1000)
#define DARK_COMBO_SUBCLASS_ID (1)
#define DARK_HEADER_SUBCLASS_ID (1)
#define GUI_SOURCE_CAPACITY (1024)
#define GUI_URL_CAPACITY (1200)
#define GUI_MEDIA_TYPE_CAPACITY (64)
#define GUI_ERROR_CAPACITY (256)
#define GUI_ADDRESS_CAPACITY (64)
#define GUI_DEFAULT_DPI (96)
#define GUI_WIDE_TEXT_CAPACITY (512)
#define GUI_DECIMAL_DIGITS (10)
#define GUI_NUMBER_TEXT_CAPACITY (20)
#define GUI_ROUNDING_HALF (0.5)
#define GUI_SECONDS_PER_HOUR (3600)
#define GUI_SECONDS_PER_MINUTE (60)
#define GUI_FONT_SIZE (13)
#define GUI_PIXEL_CENTER (0.5F)
#define GUI_CHILD_INITIAL_SIZE (10)
#define GUI_COMBO_TEXT_CAPACITY (128)
#define GUI_CONTROL_TEXT_INSET (6)
#define GUI_CONTROL_TEXT_CAPACITY (512)
#define GUI_ICON_STROKE_WIDTH (2.0F)
#define GUI_COMBO_ITEM_HEIGHT (22)
#define GUI_COMBO_FIELD_HEIGHT (20)
#define GUI_FILE_LABEL_WIDTH (44)
#define GUI_SUBTITLE_LABEL_WIDTH (68)
#define GUI_GAIN_LABEL_WIDTH (112)
#define GUI_GAIN_ROW_RESERVED_WIDTH (300)
#define GUI_MOTION_SLIDER_RESERVED_WIDTH (52)
#define GUI_CONTROL_ROW_COUNT (5)
#define GUI_SHORT_TEXT_CAPACITY (96)
#define GUI_PROXY_WIDTH_ROUNDING (8)
#define GUI_PROXY_WIDTH_ALIGNMENT (16)
#define GUI_PROXY_MIN_WIDTH (240)
#define GUI_ORIGINAL_GAIN_PERCENT (100)
#define GUI_GAIN_PAGE_SIZE (25)
#define GUI_DEVICE_COLUMN_WIDTH (200)
#define GUI_DEVICE_TEXT_CAPACITY (160)
#define GUI_DEVICE_NAME_CAPACITY (256)
#define GUI_AUTOMATIC_TEXT_CAPACITY (380)
#define GUI_LABEL_CAPACITY (320)
#define GUI_EXTENSION_CAPACITY (16)
#define GUI_HTTP_PREFIX_LENGTH (7)
#define GUI_HTTPS_PREFIX_LENGTH (8)
#define GUI_PREVIEW_REPORT_CAPACITY (1800)
#define GUI_NOMINAL_FPS (24.0)
#define GUI_BITS_PER_BYTE (8.0)
#define GUI_KBPS_DIVISOR (1000.0)
#define GUI_MIN_BITRATE_SECONDS (0.1)
#define GUI_DEFAULT_DURATION_SECONDS (600.0)
#define GUI_DEFAULT_PREVIEW_RATIO (0.25)
#define GUI_PREVIEW_SECONDS (8)
#define GUI_PREVIEW_FPS (24)
#define GUI_TIME_TEXT_CAPACITY (32)
#define GUI_STATE_TEXT_CAPACITY (64)
#define GUI_STATUS_TEXT_CAPACITY (128)
#define GUI_MILLISECONDS_PER_SECOND (1000.0)
#define GUI_DARK_MODE_ATTRIBUTE (20)
#define GUI_PROGRESS_TEXT_CAPACITY (100)
#define GUI_MIN_WINDOW_WIDTH (620)
#define GUI_MIN_WINDOW_HEIGHT (584)
#define GUI_SUBTITLE_POLL_MS (350)
#define GUI_GAIN_POLL_MS (500)
#define GUI_GAIN_STATE_CAPACITY (192)
#define GUI_INITIAL_WINDOW_WIDTH (720)
#define GUI_INITIAL_WINDOW_HEIGHT (740)

enum ControlId
{
    ID_LIST = 100,
    ID_LIST_LABEL,
    ID_REFRESH,
    ID_WINDOWS_CAST,
    ID_FILE_LABEL,
    ID_FILE,
    ID_BROWSE,
    ID_SUBTITLE_LABEL,
    ID_SUBTITLE,
    ID_SUBTITLE_BROWSE,
    ID_GAIN_LABEL,
    ID_GAIN,
    ID_GAIN_STATE,
    ID_CAST,
    ID_HINT,
    ID_PLAY_PAUSE,
    ID_STOP,
    ID_SEEK,
    ID_TIME,
    ID_STATUS,
    ID_CFG_LABEL,
    ID_RES_SLIDER,
    ID_RES_VALUE,
    ID_QP_SLIDER,
    ID_QP_VALUE,
    ID_GOP_SLIDER,
    ID_GOP_VALUE,
    ID_MOTION_CHECK,
    ID_MOTION_SLIDER,
    ID_MOTION_VALUE,
    ID_PREVIEW,
    ID_CFG_READOUT
};

typedef struct DiscoveryResult
{
    uint32_t   count;
    uint32_t   error;
    CastDevice devices[CAST_MAX_DEVICES];
} DiscoveryResult;

typedef struct SessionEvents
{
    CastEvents_t mailbox;
    unsigned     generation;
} SessionEvents;

/* A quality-preview transcode job and its result. One preview runs at a time, so both live in the
** App with fixed capacity rather than being allocated per click. */
typedef struct PreviewResult
{
    int      ok;
    unsigned width;
    unsigned height;
    unsigned frames;
    unsigned kbps;
    int      motionPercent;
    WCHAR    path[MAX_PATH];
} PreviewResult;

typedef struct PreviewJob
{
    GpuDevice          gpuDevice; ///< Startup-owned device and query workspace.
    HWND               hWnd;
    PreviewResult*     pResult;
    volatile LONG*     pCancel;
    GpuTranscodeConfig config;
    unsigned           startFrame;
    unsigned           frames;
    WCHAR              source[MAX_PATH];
    WCHAR              dest[MAX_PATH];
} PreviewJob;

typedef struct GainVariant
{
    struct GainVariant* next;
    MediaServer*        server;
    WCHAR               path[MAX_PATH];
} GainVariant;

typedef struct NativeText
{
    LPTSTR   buffer;
    uint32_t capacity;
    uint32_t count;
    bool_t   complete;
} NativeText;

typedef struct ByteText
{
    char*    buffer;
    uint32_t capacity;
    uint32_t count;
    bool_t   complete;
} ByteText;

typedef struct App
{
    HINSTANCE hInstance;
    HWND      hWnd;
    IMMERSIVEWINDOW immersiveWindow;
    SWAPCHAIN       swapChain;
    CAPTION         caption;
    OS_Thread_t     presenterWorker;
    ID2D1Factory*          paintFactory;
    ID2D1DCRenderTarget*  paintTarget;
    ID2D1SolidColorBrush* paintBrush;
    IDWriteFactory*        textFactory;
    IDWriteTextFormat*     textFormat;
    IDWriteInlineObject*   trimmingSign;
    HWND      hListLabel;
    HWND      hList;
    HWND      hRefresh;
    HWND      hWindowsCast;
    HWND      hFileLabel;
    HWND      hFile;
    HWND      hBrowse;
    HWND                  hSubtitleLabel, hSubtitle, hSubtitleBrowse;
    HWND                  hGainLabel, hGain, hGainState;
    HBRUSH                backgroundBrush, fieldBrush;
    SubtitleTrack         subtitles[SUBTITLE_MAX_TRACKS];
    Subtitles_Workspace_t subtitleWorkspace;
    byte_t                subtitleScratch[SUBTITLE_TEXT_CAPACITY];
    byte_t                subtitleOutput[SUBTITLE_TEXT_CAPACITY];
    unsigned              subtitleCount;
    TCHAR                 subtitleSource[GUI_SOURCE_CAPACITY];
    WCHAR                 originalSource[MAX_PATH];
    char                  originalUrl[GUI_URL_CAPACITY], originalType[GUI_MEDIA_TYPE_CAPACITY];
    OS_Thread_t gainWorker;
    bool_t      gainBusy;
    bool_t      gainRestartPending;
    bool_t      gainDiscard;
    unsigned    gainActiveSerial;
    volatile LONG         gainCancel, gainProgress;
    unsigned              gainPercent, gainSerial;
    int gainOk;
    int                   preparingAudio, audioNeedsConversion;
    CastSessionConfig   pendingConfig;
    char                gainError[GUI_ERROR_CAPACITY];
    WCHAR               gainOutput[MAX_PATH];
    GainVariant         gainVariantStorage[MEDIA_SERVER_CAPACITY];
    GainVariant*        gainVariants;
    HWND                hCast;
    HWND                hHint;
    HWND                hPlayPause;
    HWND                hStop;
    HWND                hSeek;
    HWND                hTime;
    HWND                hStatus;
    HWND                hCfgLabel;
    HWND                hResSlider;
    HWND                hResValue;
    HWND                hQpSlider;
    HWND                hQpValue;
    HWND                hGopSlider;
    HWND                hGopValue;
    HWND                hMotionCheck;
    HWND                hMotionSlider;
    HWND                hMotionValue;
    HWND                hPreview;
    HWND                hCfgReadout;
    HFONT               hFont;
    UINT                dpi;
    /* receivers */
    DiscoveryResult devices;
    DiscoveryResult discovered;
    Ssdp_t          discovery;
    OS_Network_t    routeNetwork; ///< UI-owned startup query resource, separate from discovery worker state.
    OS_Thread_t     discoveryWorker;
    SessionEvents   events;
    int             closing;
    int             finishPending;
    TCHAR           finishReason[CAST_EVENTS_TEXT_CAPACITY];
    int             discovering;
    /* the active session */
    CastSession_t  session;  ///< Storage established at startup and reused by every cast.
    CastSession_t* pSession; ///< Active alias; NULL between casts.
    OS_Thread_t    sessionWorker;
    bool_t         sessionBusy;
    MediaServer*   pServer;
    MediaServer_Workspace_t serving;
    Proxy_t        proxy; ///< Startup-owned proxy state, scratch file and persistent worker.
    WCHAR          proxySource[MAX_PATH];
    char           proxyLocal[GUI_ADDRESS_CAPACITY];
    unsigned short        proxyPort;
    GpuTranscodeConfig transcode; /* live transcoder settings from the config sliders */
    double             keyframes[MP4_INDEX_MAX_KEYFRAMES];
    CastStatus         status;
    ULONGLONG          statusTick; /* when status.currentTime was reported, for interpolation */
    int                seeking;    /* the user is dragging the slider */
    int                stopping;
    OS_Thread_t        previewWorker; ///< Startup-owned preview execution resource.
    volatile LONG      previewCancel;
    int                previewBusy;
    PreviewJob         previewJob; /* fixed storage for the running preview, no per-click alloc */
    PreviewResult      previewResult;
    bool_t windowDestroyed; ///< UI-owned completion of the native window lifetime.
} App;

/** Fixed native child-window definition, with its destination in the application context. */
typedef struct CastGui_ControlDefinition_s
{
    size_t  destinationOffset; ///< Offset of the receiving HWND member in App.
    LPCTSTR windowClass;       ///< Native window class in the active character set.
    LPCTSTR text;              ///< Initial caption in the active character set.
    DWORD   style;             ///< Control-specific child-window styles.
    DWORD   extendedStyle;     ///< Extended child-window styles.
    int     id;                ///< Notification ID owned by the immersive window.
} CastGui_ControlDefinition_t;

#define POSITION_TIMER   (1)
#define POSITION_TICK_MS (50)
#define SUBTITLE_TIMER   (2)
#define GAIN_TIMER       (3)
#define GAIN_ERROR_CAPACITY (256)
#define GAIN_PORT_CAPACITY (5)
#define GAIN_URL_SCHEME ("http")
#define GAIN_URL_RESOURCE ("/gain.mp4")
#define GAIN_MEDIA_TYPE ("video/mp4")
#define GUI_NATIVE_TEXT_CAPACITY (2048)
#define SEEK_BAR_SUBCLASS_ID (1)

#define DARK_BACKGROUND RGB(24, 26, 31)
#define DARK_FIELD      RGB(36, 39, 46)
#define DARK_TEXT       RGB(232, 235, 240)
#define DARK_MUTED      RGB(151, 160, 174)

/** Fixed controls in creation and tab order. Layout and runtime state are applied after creation. */
static const CastGui_ControlDefinition_t c_controls[] = {
    {offsetof(App, hListLabel), WC_STATIC, TEXT("Cast receivers on this network"), SS_LEFT, 0, ID_LIST_LABEL},
    {offsetof(App, hRefresh), WC_BUTTON, TEXT("Refresh"), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_REFRESH},
    {offsetof(App, hWindowsCast), WC_BUTTON, TEXT("Windows Cast..."), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_WINDOWS_CAST},
    {offsetof(App, hList),
     WC_LISTVIEW,
     TEXT(""),
     LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS | LVS_NOSORTHEADER | WS_TABSTOP,
     WS_EX_CLIENTEDGE,
     ID_LIST},
    {offsetof(App, hFileLabel), WC_STATIC, TEXT("Video"), SS_LEFT, 0, ID_FILE_LABEL},
    {offsetof(App, hFile), WC_EDIT, TEXT(""), ES_AUTOHSCROLL | WS_TABSTOP, WS_EX_CLIENTEDGE, ID_FILE},
    {offsetof(App, hBrowse), WC_BUTTON, TEXT("Browse..."), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_BROWSE},
    {offsetof(App, hSubtitleLabel), WC_STATIC, TEXT("Subtitles"), SS_LEFT, 0, ID_SUBTITLE_LABEL},
    {offsetof(App, hSubtitle),
     WC_COMBOBOX,
     TEXT(""),
     CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
     0,
     ID_SUBTITLE},
    {offsetof(App, hSubtitleBrowse), WC_BUTTON, TEXT("Browse..."), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_SUBTITLE_BROWSE},
    {offsetof(App, hGainLabel), WC_STATIC, TEXT("Audio gain 100%"), SS_LEFT, 0, ID_GAIN_LABEL},
    {offsetof(App, hGain), TRACKBAR_CLASS, TEXT(""), TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_GAIN},
    {offsetof(App, hGainState), WC_STATIC, TEXT("Original audio"), SS_RIGHT | SS_ENDELLIPSIS, 0, ID_GAIN_STATE},
    {offsetof(App, hCast), WC_BUTTON, TEXT("Cast"), BS_DEFPUSHBUTTON | WS_TABSTOP, 0, ID_CAST},
    {offsetof(App, hHint),
     WC_STATIC,
     TEXT("A file is served to the receiver over HTTP from this PC; an http(s) URL is sent as is."),
     SS_LEFT | SS_ENDELLIPSIS,
     0,
     ID_HINT},
    {offsetof(App, hPlayPause), WC_BUTTON, TEXT("Play"), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_PLAY_PAUSE},
    {offsetof(App, hStop), WC_BUTTON, TEXT("Stop"), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_STOP},
    {offsetof(App, hSeek), TRACKBAR_CLASS, TEXT(""), TBS_HORZ | TBS_NOTICKS | TBS_BOTH | WS_TABSTOP, 0, ID_SEEK},
    {offsetof(App, hTime), WC_STATIC, TEXT(""), SS_RIGHT, 0, ID_TIME},
    {offsetof(App, hStatus), WC_STATIC, TEXT(""), SS_LEFT | SS_ENDELLIPSIS, 0, ID_STATUS},
    {offsetof(App, hCfgLabel),
     WC_STATIC,
     TEXT("Proxy transcoder - adjust, then Preview to test quality on the receiver"),
     SS_LEFT | SS_ENDELLIPSIS,
     0,
     ID_CFG_LABEL},
    {offsetof(App, hResValue), WC_STATIC, TEXT(""), SS_LEFT, 0, ID_RES_VALUE},
    {offsetof(App, hResSlider), TRACKBAR_CLASS, TEXT(""), TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_RES_SLIDER},
    {offsetof(App, hQpValue), WC_STATIC, TEXT(""), SS_LEFT, 0, ID_QP_VALUE},
    {offsetof(App, hQpSlider), TRACKBAR_CLASS, TEXT(""), TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_QP_SLIDER},
    {offsetof(App, hGopValue), WC_STATIC, TEXT(""), SS_LEFT, 0, ID_GOP_VALUE},
    {offsetof(App, hGopSlider), TRACKBAR_CLASS, TEXT(""), TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_GOP_SLIDER},
    {offsetof(App, hMotionCheck), WC_BUTTON, TEXT("Optical flow"), BS_AUTOCHECKBOX | WS_TABSTOP, 0, ID_MOTION_CHECK},
    {offsetof(App, hMotionSlider), TRACKBAR_CLASS, TEXT(""), TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, 0, ID_MOTION_SLIDER},
    {offsetof(App, hMotionValue), WC_STATIC, TEXT(""), SS_LEFT, 0, ID_MOTION_VALUE},
    {offsetof(App, hPreview), WC_BUTTON, TEXT("Preview"), BS_PUSHBUTTON | WS_TABSTOP, 0, ID_PREVIEW},
    {offsetof(App, hCfgReadout), WC_STATIC, TEXT(""), SS_LEFT | SS_ENDELLIPSIS, 0, ID_CFG_READOUT}};

/*************************************************************************************************/
/**
 * \brief Borrows the startup context already attached to a live window.
 *
 * \par Requirements:
 *  - 001 WM_NCCREATE shall have attached the context before any caller uses this query.
 *  - 002 The entry point shall retain the context until native destruction and worker closure.
 *
 * \param[in] window   Native application window
 *
 * \retval    non-NULL   Borrowed startup context; a missing binding asserts.
 */
static FORCE_INLINE App* GetApp(HWND window);

/*************************************************************************************************/
/**
 * \par Details:
 *  The native window owns only a borrowed pointer, never the context allocation.
 */
static FORCE_INLINE App* GetApp(HWND window)
{
    App* app;

    app = (App*)GetProp(window, TEXT("cvkCastApp"));
    LOGASSERT(NULL != app);
    return app;
}

/*************************************************************************************************/
/**
 * \brief Acquires completed job contexts and dispatches their results on the UI thread.
 *
 * \par Requirements:
 *  - 001 Running job storage shall remain untouched until completion or FAILED is acquired.
 *  - 002 Pending gain restarts shall consume only the latest slider value.
 *
 * \param[in,out] app   Initialized application and its persistent workers
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void PollJobs(App* app);

static FORCE_INLINE LRESULT CALLBACK SeekBarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData);
static FORCE_INLINE void SeekBar_OnNcDestroy(HWND hWnd);
static FORCE_INLINE void SeekBar_OnMouseMove(HWND hWnd, int x, int y, UINT keyFlags);
static FORCE_INLINE void SeekBar_OnLButtonDown(HWND hWnd, BOOL doubleClick, int x, int y, UINT keyFlags);
static FORCE_INLINE void SeekBar_OnLButtonUp(HWND hWnd, int x, int y, UINT keyFlags);
static FORCE_INLINE void RefreshSubtitles(App* pApp);
static FORCE_INLINE void ScheduleGain(App* app);
static FORCE_INLINE void OnPreviewDone(HWND hWnd, const PreviewResult* pResult);
static FORCE_INLINE void FinishSession(App* pApp, LPCTSTR pReason);

/*************************************************************************************************/
/**
 * \brief Presents the startup-owned window until its persistent worker is stopped.
 *
 * \param[in,out] parameter   Required application context retained through worker closure
 *
 * \retval        None No value is returned.
 */
static FORCE_INLINE void PresentCastWindow(void* parameter);

/*************************************************************************************************/
/**
 * \brief Reads native control text into the application's fixed UTF-16 representation.
 *
 * \param[in]     control    Native control
 * \param[out]    output     GUI_SOURCE_CAPACITY UTF-16 units including the terminator
 *
 * \retval        TRUE       Complete text was read.
 * \retval        FALSE      Native text or output capacity was unsuitable.
 */
static FORCE_INLINE bool_t ReadControlText(HWND control, WCHAR* output);

/*************************************************************************************************/
/**
 * \brief Converts fixed UTF-16 application text for a native control.
 *
 * \param[in] control   Native control
 * \param[in] text      Terminated UTF-16 text
 *
 * \retval    TRUE      Conversion and native update succeeded.
 * \retval    FALSE     The text was unrepresentable or too long, or the update failed.
 */
static FORCE_INLINE bool_t WriteControlText(HWND control, const WCHAR* text);

/*************************************************************************************************/
/**
 * \brief Adds a fixed UTF-16 label to a native combo box.
 *
 * \param[in] combo   Native combo box
 * \param[in] text    Terminated UTF-16 label
 *
 * \retval    TRUE    Conversion and insertion succeeded.
 * \retval    FALSE   The label was unrepresentable or too long, or insertion failed.
 */
static FORCE_INLINE bool_t AddComboText(HWND combo, const WCHAR* text);

/*************************************************************************************************/
/**
 * \brief Deletes a fixed UTF-16 application path through the native text API.
 *
 * \param[in] path   Terminated UTF-16 path
 *
 * \retval    TRUE   The native file was deleted.
 * \retval    FALSE  Conversion or deletion failed; GetLastError identifies the cause.
 */
static FORCE_INLINE bool_t DeleteUtf16File(const WCHAR* path);

/*************************************************************************************************/
/**
 * \par Details:
 *  The native workspace bounds the control read before the encoding conversion.
 */
static FORCE_INLINE bool_t ReadControlText(HWND control, WCHAR* output)
{
    TCHAR   native[GUI_NATIVE_TEXT_CAPACITY];
    int32_t length;
    bool_t  fits;
    bool_t  read;

    LOGASSERT(NULL != output);

    length = GetWindowTextLength(control);
    fits   = (0 <= length) && (length < GUI_NATIVE_TEXT_CAPACITY);
    read   = FALSE;
    output[0] = L'\0';
    if (fits)
    {
        length = GetWindowText(control, native, GUI_NATIVE_TEXT_CAPACITY);
        read = (0 <= length) && OS_Text_ReadNative((uint16_t*)output, GUI_SOURCE_CAPACITY, native);
    }
    return read;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A failed conversion leaves an empty native value rather than substituting characters.
 */
static FORCE_INLINE bool_t WriteControlText(HWND control, const WCHAR* text)
{
    TCHAR  native[GUI_NATIVE_TEXT_CAPACITY];
    bool_t converted;
    bool_t written;

    converted = OS_Text_WriteNative(native, GUI_NATIVE_TEXT_CAPACITY, (const uint16_t*)text);
    written   = SetWindowText(control, native);
    return converted && written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  The combo owns a copy after the synchronous insertion message returns.
 */
static FORCE_INLINE bool_t AddComboText(HWND combo, const WCHAR* text)
{
    TCHAR   native[GUI_NATIVE_TEXT_CAPACITY];
    bool_t  converted;
    LRESULT index;

    converted = OS_Text_WriteNative(native, GUI_NATIVE_TEXT_CAPACITY, (const uint16_t*)text);
    index     = CB_ERR;
    if (converted)
    {
        index = SendMessage(combo, CB_ADDSTRING, 0, (LPARAM)native);
    }
    return converted && (CB_ERR != index) && (CB_ERRSPACE != index);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Native conversion failure has an explicit Windows error for existing cleanup paths.
 */
static FORCE_INLINE bool_t DeleteUtf16File(const WCHAR* path)
{
    TCHAR  native[MAX_PATH];
    bool_t converted;
    bool_t deleted;

    converted = OS_Text_WriteNative(native, MAX_PATH, (const uint16_t*)path);
    deleted   = FALSE;
    if (converted)
    {
        deleted = DeleteFile(native);
    }
    else
    {
        SetLastError(ERROR_NO_UNICODE_TRANSLATION);
    }
    return deleted;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Converts 96-DPI logical units using the window's current DPI.
 */
static FORCE_INLINE int Scale(const App* pApp, int dips)
{
    return MulDiv(dips, (int)pApp->dpi, GUI_DEFAULT_DPI);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Writes an empty Windows string when the source is NULL or conversion fails.
 */
static FORCE_INLINE void Utf8ToWide(const char* pText, WCHAR* pWide, int capacity)
{
    const char* safe_text = (NULL != pText) ? pText : "";

    if (0 == MultiByteToWideChar(CP_UTF8, 0, safe_text, -1, pWide, capacity))
    {
        pWide[0] = L'\0';
    }
}

static FORCE_INLINE void Utf8ToNative(const char* pText, LPTSTR pNative, uint32_t capacity)
{
    WCHAR wide[CAST_EVENTS_TEXT_CAPACITY];
    bool_t converted;
    uint32_t safe_capacity;

    if (0 == capacity)
    {
        return;
    }
    safe_capacity = (capacity <= ARRAYSIZE(wide)) ? capacity : ARRAYSIZE(wide);
    Utf8ToWide(pText, wide, (int)safe_capacity);
    converted = OS_Text_WriteNative(pNative, safe_capacity, (const uint16_t*)wide);
    if (!converted)
    {
        pNative[0] = TEXT('\0');
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Converts a terminated Windows string into UTF-8; failure clears the output.
 */
static FORCE_INLINE void ToUtf8(const WCHAR* pText, char* pOut, uint32_t capacity)
{
    if (0 == WideCharToMultiByte(CP_UTF8, 0, pText, -1, pOut, (int)capacity, NULL, NULL))
    {
        pOut[0] = 0;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void SetStatus(App* pApp, LPCTSTR pText)
{
    (void)SetWindowText(pApp->hStatus, pText);
}

static FORCE_INLINE void NativeText_Initialize(NativeText* text, LPTSTR buffer, uint32_t capacity)
{
    LOGASSERT(NULL != text);
    LOGASSERT(NULL != buffer);
    LOGASSERT(0 < capacity);

    text->buffer   = buffer;
    text->capacity = capacity;
    text->count    = 0;
    text->complete = TRUE;
    buffer[0]      = TEXT('\0');
}

static FORCE_INLINE void NativeText_AppendCharacter(NativeText* text, TCHAR character)
{
    bool_t has_space;

    has_space = (text->count < text->capacity - 1);
    if (has_space)
    {
        text->buffer[text->count] = character;
        ++text->count;
        text->buffer[text->count] = TEXT('\0');
    }
    else
    {
        text->complete = FALSE;
    }
}

static FORCE_INLINE void NativeText_AppendString(NativeText* text, LPCTSTR value)
{
    uint32_t index;

    for (index = 0; TEXT('\0') != value[index]; ++index)
    {
        NativeText_AppendCharacter(text, value[index]);
    }
}

static FORCE_INLINE void NativeText_AppendUnsigned(NativeText* text, uint32_t value, uint32_t minDigits)
{
    TCHAR    digits[GUI_DECIMAL_DIGITS];
    uint32_t count;
    uint32_t digit;

    LOGASSERT((0 < minDigits) && (minDigits <= ARRAYSIZE(digits)));
    count = 0;
    for (;;)
    {
        digit = value % GUI_DECIMAL_DIGITS;
        digits[count] = (TCHAR)(TEXT('0') + digit);
        ++count;
        value /= GUI_DECIMAL_DIGITS;
        if ((0 == value) && (minDigits <= count))
        {
            break;
        }
    }
    for (; 0 < count;)
    {
        --count;
        NativeText_AppendCharacter(text, digits[count]);
    }
}

static FORCE_INLINE void NativeText_AppendSigned(NativeText* text, int32_t value)
{
    uint32_t magnitude;

    if (0 > value)
    {
        NativeText_AppendCharacter(text, TEXT('-'));
        magnitude = (uint32_t)(-(int64_t)value);
    }
    else
    {
        magnitude = (uint32_t)value;
    }
    NativeText_AppendUnsigned(text, magnitude, 1);
}

static FORCE_INLINE bool_t CopyUtf16(WCHAR* output, uint32_t outputCapacity, const WCHAR* input, uint32_t inputCapacity)
{
    uint32_t count;
    bool_t   fits;

    LOGASSERT(NULL != output);
    LOGASSERT(0 < outputCapacity);
    count = Utl_Unicode_GetUtf16StringUnitCount((const uint16_t*)input, inputCapacity);
    fits  = (UTL_UNICODE_NO_TERMINATOR != count) && (count < outputCapacity);
    if (fits)
    {
        CRT_Memory_Copy(output, input, ((uint64_t)count + 1) * sizeof(WCHAR));
    }
    else
    {
        output[0] = L'\0';
    }
    return fits;
}

static FORCE_INLINE bool_t Utf16StartsWithNoCase(const WCHAR* text, uint32_t capacity, const WCHAR* prefix, uint32_t prefixLength)
{
    uint32_t count;
    bool_t   matches;

    count   = Utl_Unicode_GetUtf16StringUnitCount((const uint16_t*)text, capacity);
    matches = (UTL_UNICODE_NO_TERMINATOR != count) && (prefixLength <= count);
    if (matches)
    {
        matches = (CSTR_EQUAL == CompareStringOrdinal(text, (int32_t)prefixLength, prefix, (int32_t)prefixLength, TRUE));
    }
    return matches;
}

static FORCE_INLINE const WCHAR* Utf16FindLast(const WCHAR* text, uint32_t capacity, WCHAR character)
{
    const WCHAR* result;
    uint32_t     count;
    uint32_t     index;

    result = NULL;
    count  = Utl_Unicode_GetUtf16StringUnitCount((const uint16_t*)text, capacity);
    if (UTL_UNICODE_NO_TERMINATOR != count)
    {
        for (index = 0; index < count; ++index)
        {
            if (character == text[index])
            {
                result = &text[index];
            }
        }
    }
    return result;
}

static FORCE_INLINE bool_t Utf16Contains(const WCHAR* text, uint32_t capacity, WCHAR character)
{
    uint32_t count;
    uint32_t index;
    bool_t   found;

    found = FALSE;
    count = Utl_Unicode_GetUtf16StringUnitCount((const uint16_t*)text, capacity);
    if (UTL_UNICODE_NO_TERMINATOR != count)
    {
        for (index = 0; index < count; ++index)
        {
            if (character == text[index])
            {
                found = TRUE;
                break;
            }
        }
    }
    return found;
}

static FORCE_INLINE void ByteText_Initialize(ByteText* text, char* buffer, uint32_t capacity)
{
    LOGASSERT(NULL != text);
    LOGASSERT(NULL != buffer);
    LOGASSERT(0 < capacity);

    text->buffer   = buffer;
    text->capacity = capacity;
    text->count    = 0;
    text->complete = TRUE;
    buffer[0]      = '\0';
}

static FORCE_INLINE void ByteText_AppendCharacter(ByteText* text, char character)
{
    bool_t has_space;

    has_space = (text->count < (text->capacity - 1));
    if (has_space)
    {
        text->buffer[text->count] = character;
        ++text->count;
        text->buffer[text->count] = '\0';
    }
    else
    {
        text->complete = FALSE;
    }
}

static FORCE_INLINE void ByteText_AppendString(ByteText* text, const char* value, uint32_t valueCapacity)
{
    uint32_t index;
    bool_t   terminated;

    terminated = FALSE;
    for (index = 0; index < valueCapacity; ++index)
    {
        if ('\0' == value[index])
        {
            terminated = TRUE;
            break;
        }
        ByteText_AppendCharacter(text, value[index]);
    }
    text->complete = text->complete && terminated;
}

static FORCE_INLINE void ByteText_AppendUnsigned(ByteText* text, uint32_t value)
{
    byte_t   digits[GUI_NUMBER_TEXT_CAPACITY];
    uint32_t count;
    uint32_t index;

    count = Utl_Decimal_WriteUint64(value, digits, ARRAYSIZE(digits));
    for (index = 0; index < count; ++index)
    {
        ByteText_AppendCharacter(text, (char)digits[index]);
    }
}

static FORCE_INLINE bool_t CopyAscii(char* output, uint32_t outputCapacity, const char* input, uint32_t inputCapacity)
{
    ByteText text;

    ByteText_Initialize(&text, output, outputCapacity);
    ByteText_AppendString(&text, input, inputCapacity);
    return text.complete;
}

static FORCE_INLINE const char* FindAscii(const char* text, uint32_t capacity, const char* token, uint32_t tokenLength)
{
    const char* result;
    uint32_t    index;
    uint32_t    offset;
    bool_t      matches;

    result = NULL;
    for (index = 0; (index < capacity) && ('\0' != text[index]); ++index)
    {
        matches = (tokenLength <= (capacity - index));
        if (matches)
        {
            for (offset = 0; offset < tokenLength; ++offset)
            {
                if (token[offset] != text[index + offset])
                {
                    matches = FALSE;
                    break;
                }
            }
        }
        if (matches)
        {
            result = &text[index];
            break;
        }
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Searches successive byte positions using a case-insensitive prefix comparison.
 */
static FORCE_INLINE char FoldAscii(char value)
{
    char result;

    result = value;
    if (('A' <= value) && (value <= 'Z'))
    {
        result = (char)(value + ('a' - 'A'));
    }
    return result;
}

static FORCE_INLINE bool_t ContainsNoCase(const char* haystack, uint32_t capacity, const char* needle, uint32_t needleLength)
{
    uint32_t index;
    uint32_t offset;
    bool_t   found;

    found = FALSE;
    for (index = 0; (index < capacity) && ('\0' != haystack[index]); ++index)
    {
        bool_t matches;

        matches = (needleLength <= (capacity - index));
        if (matches)
        {
            for (offset = 0; offset < needleLength; ++offset)
            {
                if (FoldAscii(haystack[index + offset]) != FoldAscii(needle[offset]))
                {
                    matches = FALSE;
                    break;
                }
            }
        }
        if (matches)
        {
            found = TRUE;
            break;
        }
    }
    return found;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Rounds to the nearest second and includes an hours field only for durations
 *  of at least one hour.
 */
static FORCE_INLINE void FormatTime(double seconds, LPTSTR pOut, uint32_t capacity)
{
    int32_t    total;
    NativeText text;
    bool_t     positive;
    bool_t     fits;

    positive = (0.0 < seconds);
    fits     = (seconds < ((double)INT32_MAX - 1.0));
    total    = 0;
    if (positive && fits)
    {
        total = (int32_t)(seconds + GUI_ROUNDING_HALF);
    }
    else if (positive)
    {
        total = INT32_MAX;
    }
    NativeText_Initialize(&text, pOut, capacity);
    if (GUI_SECONDS_PER_HOUR <= total)
    {
        NativeText_AppendUnsigned(&text, (uint32_t)(total / GUI_SECONDS_PER_HOUR), 1);
        NativeText_AppendCharacter(&text, TEXT(':'));
        NativeText_AppendUnsigned(&text, (uint32_t)((total / GUI_SECONDS_PER_MINUTE) % GUI_SECONDS_PER_MINUTE), 2);
    }
    else
    {
        NativeText_AppendUnsigned(&text, (uint32_t)(total / GUI_SECONDS_PER_MINUTE), 1);
    }
    NativeText_AppendCharacter(&text, TEXT(':'));
    NativeText_AppendUnsigned(&text, (uint32_t)(total % GUI_SECONDS_PER_MINUTE), 2);
}

/* ------------------------------------------------------------------ controls and layout */

static const D2D1_COLOR_F c_castBackgroundColor = {24.0F / 255.0F, 26.0F / 255.0F, 31.0F / 255.0F, 1.0F};
static const D2D1_COLOR_F c_castFieldColor = {36.0F / 255.0F, 39.0F / 255.0F, 46.0F / 255.0F, 1.0F};
static const D2D1_COLOR_F c_castSelectedColor = {48.0F / 255.0F, 91.0F / 255.0F, 146.0F / 255.0F, 1.0F};
static const D2D1_COLOR_F c_castTextColor = {232.0F / 255.0F, 235.0F / 255.0F, 240.0F / 255.0F, 1.0F};
static const D2D1_COLOR_F c_castMutedColor = {151.0F / 255.0F, 160.0F / 255.0F, 174.0F / 255.0F, 1.0F};
static const D2D1_COLOR_F c_castAccentColor = {82.0F / 255.0F, 156.0F / 255.0F, 245.0F / 255.0F, 1.0F};
static const D2D1_COLOR_F c_castBorderColor = {62.0F / 255.0F, 68.0F / 255.0F, 78.0F / 255.0F, 1.0F};

static FORCE_INLINE void InitControlPaint(App* app)
{
    D2D1_PIXEL_FORMAT format;

    cd2dCreateFactorySingleThreaded(NULL, &app->paintFactory);
    cd2dInitPixelFormat(&format, DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
    cd2dFactoryCreateDCRenderTarget(app->paintFactory,
                                    D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                    format,
                                    (FLOAT)USER_DEFAULT_SCREEN_DPI,
                                    (FLOAT)USER_DEFAULT_SCREEN_DPI,
                                    D2D1_RENDER_TARGET_USAGE_NONE,
                                    D2D1_FEATURE_LEVEL_DEFAULT,
                                    &app->paintTarget);
    cd2dRenderTargetCreateSolidColorBrush((ID2D1RenderTarget*)app->paintTarget, &c_castTextColor, NULL, &app->paintBrush);
    cdwCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &app->textFactory);
}

static FORCE_INLINE void UpdateControlTextFormat(App* app)
{
    int fontSize;

    cdwUnknownReleaseSafe((IUnknown*)app->trimmingSign);
    cdwUnknownReleaseSafe((IUnknown*)app->textFormat);
    app->trimmingSign = NULL;
    app->textFormat   = NULL;
    fontSize = Scale(app, GUI_FONT_SIZE);
    cdwFactoryCreateTextFormat(app->textFactory,
                               L"Segoe UI",
                               NULL,
                               DWRITE_FONT_WEIGHT_NORMAL,
                               DWRITE_FONT_STYLE_NORMAL,
                               DWRITE_FONT_STRETCH_NORMAL,
                               (FLOAT)fontSize,
                               L"",
                               &app->textFormat);
    cdwTextFormatSetParagraphAlignment(app->textFormat, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    cdwTextFormatSetWordWrapping(app->textFormat, DWRITE_WORD_WRAPPING_NO_WRAP);
    cdwFactoryCreateEllipsisTrimmingSign(app->textFactory, app->textFormat, &app->trimmingSign);
    cdwTextFormatSetTrimming(app->textFormat, DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0, app->trimmingSign);
}

static FORCE_INLINE void BeginControlPaint(App* app, HDC dc, const RECT* bounds)
{
    cd2dDCRenderTargetBindDC(app->paintTarget, dc, bounds);
    cd2dRenderTargetBeginDraw((ID2D1RenderTarget*)app->paintTarget);
}

static FORCE_INLINE void EndControlPaint(App* app)
{
    cd2dRenderTargetEndDrawAsserted((ID2D1RenderTarget*)app->paintTarget);
}

static FORCE_INLINE void FillControlRect(App* app, const RECT* rect, const D2D1_COLOR_F* color)
{
    cd2dSolidColorBrushSetColor(app->paintBrush, color);
    cd2dRenderTargetFillRectangle((ID2D1RenderTarget*)app->paintTarget,
                                  (FLOAT)rect->left,
                                  (FLOAT)rect->top,
                                  (FLOAT)rect->right,
                                  (FLOAT)rect->bottom,
                                  (ID2D1Brush*)app->paintBrush);
}

static FORCE_INLINE void DrawControlRect(App* app, const RECT* rect, const D2D1_COLOR_F* color)
{
    cd2dSolidColorBrushSetColor(app->paintBrush, color);
    cd2dRenderTargetDrawRectangle((ID2D1RenderTarget*)app->paintTarget,
                                  (FLOAT)rect->left + GUI_PIXEL_CENTER,
                                  (FLOAT)rect->top + GUI_PIXEL_CENTER,
                                  (FLOAT)rect->right - GUI_PIXEL_CENTER,
                                  (FLOAT)rect->bottom - GUI_PIXEL_CENTER,
                                  (ID2D1Brush*)app->paintBrush,
                                  1.0F,
                                  NULL);
}

static FORCE_INLINE void DrawControlText(App* app, LPCTSTR text, const RECT* rect, const D2D1_COLOR_F* color, DWRITE_TEXT_ALIGNMENT alignment)
{
    WCHAR  wide[GUI_WIDE_TEXT_CAPACITY];
    bool_t converted;

    converted = OS_Text_ReadNative((uint16_t*)wide, ARRAYSIZE(wide), text);
    if (!converted)
    {
        wide[0] = L'\0';
    }
    cdwTextFormatSetTextAlignment(app->textFormat, alignment);
    cd2dSolidColorBrushSetColor(app->paintBrush, color);
    cd2dRenderTargetDrawText((ID2D1RenderTarget*)app->paintTarget,
                             wide,
                             (UINT32)lstrlenW(wide),
                             app->textFormat,
                             (FLOAT)rect->left,
                             (FLOAT)rect->top,
                             (FLOAT)rect->right,
                             (FLOAT)rect->bottom,
                             (ID2D1Brush*)app->paintBrush,
                             D2D1_DRAW_TEXT_OPTIONS_CLIP,
                             DWRITE_MEASURING_MODE_NATURAL);
}

static FORCE_INLINE void DestroyControlPaint(App* app)
{
    cdwUnknownReleaseSafe((IUnknown*)app->trimmingSign);
    cdwUnknownReleaseSafe((IUnknown*)app->textFormat);
    cdwUnknownReleaseSafe((IUnknown*)app->textFactory);
    cd2dUnknownReleaseSafe((IUnknown*)app->paintBrush);
    cd2dUnknownReleaseSafe((IUnknown*)app->paintTarget);
    cd2dUnknownReleaseSafe((IUnknown*)app->paintFactory);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Changes ordinary buttons to owner-draw controls and requests the dark Explorer
 *  theme after creating the child window.
 */
static FORCE_INLINE HWND CreateChild(App* pApp, LPCTSTR pClass, LPCTSTR pText, DWORD style, DWORD exStyle, int id)
{
    HWND child;
    if ((0 == lstrcmp(pClass, WC_BUTTON)) && ((style & BS_TYPEMASK) != BS_AUTOCHECKBOX))
    {
        style = (style & ~(DWORD)BS_TYPEMASK) | (DWORD)BS_OWNERDRAW;
    }
    child = CreateWindowEx(exStyle,
                            pClass,
                            pText,
                            WS_CHILD | WS_VISIBLE | style,
                            0,
                            0,
                            GUI_CHILD_INITIAL_SIZE,
                            GUI_CHILD_INITIAL_SIZE,
                            pApp->hWnd,
                            (HMENU)(INT_PTR)id, // NOLINT(performance-no-int-to-ptr): Win32 child control IDs use the HMENU slot.
                            pApp->hInstance,
                            NULL);
    LOGASSERT(SUCCEEDED(SetWindowTheme(child, L"DarkMode_Explorer", NULL)));

    return child;
}

/* The list-view owns its header, so header NM_CUSTOMDRAW never reaches WindowProc.
** Paint the header itself, including the unused area after the last column. Native
** header processing still handles column resizing and hit testing. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Paints the header cells and trailing unused area directly because the parent
 *  list-view owns header notifications. Native processing retains hit testing
 *  and column resizing.
 */
static FORCE_INLINE void PaintDarkHeader(HWND hWnd, HDC dc, App* pApp)
{
    RECT client;
    GetClientRect(hWnd, &client);
    BeginControlPaint(pApp, dc, &client);
    FillControlRect(pApp, &client, &c_castFieldColor);
    for (int i = 0; i < Header_GetItemCount(hWnd); ++i)
    {
        TCHAR                 native[GUI_COMBO_TEXT_CAPACITY];
        HDITEM                item;
        RECT                  rect;
        RECT                  divider;
        DWRITE_TEXT_ALIGNMENT alignment;
        ZeroMemory(&item, sizeof(item));
        item.mask       = HDI_TEXT | HDI_FORMAT;
        item.pszText    = native;
        item.cchTextMax = GUI_COMBO_TEXT_CAPACITY;
        if (!Header_GetItem(hWnd, i, &item) || !Header_GetItemRect(hWnd, i, &rect))
        {
            continue;
        }
        divider      = rect;
        divider.left = divider.right - 1;
        FillControlRect(pApp, &divider, &c_castBackgroundColor);
        InflateRect(&rect, -Scale(pApp, GUI_CONTROL_TEXT_INSET), 0);
        if (0 != (item.fmt & HDF_RIGHT))
        {
            alignment = DWRITE_TEXT_ALIGNMENT_TRAILING;
        }
        else if (0 != (item.fmt & HDF_CENTER))
        {
            alignment = DWRITE_TEXT_ALIGNMENT_CENTER;
        }
        else
        {
            alignment = DWRITE_TEXT_ALIGNMENT_LEADING;
        }
        DrawControlText(pApp, native, &rect, &c_castMutedColor, alignment);
    }
    EndControlPaint(pApp);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Handles paint and print-client requests with the dark header renderer and
 *  removes the subclass during nonclient destruction.
 */
static FORCE_INLINE LRESULT CALLBACK DarkHeaderProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData);

static FORCE_INLINE void DarkHeader_OnPaint(HWND hWnd)
{
    PAINTSTRUCT paint;
    HDC         dc;
    App*        pApp;

    pApp = GetApp(GetParent(GetParent(hWnd)));
    dc   = BeginPaint(hWnd, &paint);
    PaintDarkHeader(hWnd, dc, pApp);
    (void)EndPaint(hWnd, &paint);
}

static FORCE_INLINE BOOL DarkHeader_OnEraseBackground(HWND hWnd, HDC dc)
{
    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(dc);
    return TRUE;
}

static FORCE_INLINE void DarkHeader_OnNcDestroy(HWND hWnd)
{
    (void)RemoveWindowSubclass(hWnd, DarkHeaderProc, DARK_HEADER_SUBCLASS_ID);
    FORWARD_WM_NCDESTROY(hWnd, DefSubclassProc);
}

static FORCE_INLINE void DarkHeader_OnPrintClient(HWND hWnd, HDC dc, UINT flags)
{
    App* pApp;

    UNREFERENCED_PARAMETER(flags);
    pApp = GetApp(GetParent(GetParent(hWnd)));
    PaintDarkHeader(hWnd, dc, pApp);
}

static FORCE_INLINE LRESULT CALLBACK DarkHeaderProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData)
{
    UNREFERENCED_PARAMETER(idSubclass);
    UNREFERENCED_PARAMETER(refData);

    switch (message)
    {
        HANDLE_MSG(hWnd, WM_PAINT,       DarkHeader_OnPaint);
        HANDLE_MSG(hWnd, WM_ERASEBKGND, DarkHeader_OnEraseBackground);
        HANDLE_MSG(hWnd, WM_NCDESTROY,   DarkHeader_OnNcDestroy);
        HANDLE_MSG(hWnd, WM_PRINTCLIENT, DarkHeader_OnPrintClient); // NOLINT(performance-no-int-to-ptr): WPARAM carries an HDC.
    default:
        break;
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Chooses selection and disabled colors, draws the counted combo item text and
 *  adds the focus rectangle when requested by the item state.
 */
static FORCE_INLINE void DrawSubtitleItem(HWND combo, RECT rect, int index, UINT state, App* pApp)
{
    TCHAR               text[GUI_CONTROL_TEXT_CAPACITY] = {0};
    bool_t              selected;
    const D2D1_COLOR_F* fill;
    const D2D1_COLOR_F* foreground;
    LRESULT             length;

    selected   = (0 != (state & ODS_SELECTED)) && (0 == (state & ODS_COMBOBOXEDIT));
    fill       = selected ? &c_castSelectedColor : &c_castFieldColor;
    foreground = (0 != (state & ODS_DISABLED)) ? &c_castMutedColor : &c_castTextColor;
    FillControlRect(pApp, &rect, fill);
    length = SendMessage(combo, CB_GETLBTEXTLEN, (WPARAM)index, 0);
    if (index >= 0 && length >= 0 && length < GUI_CONTROL_TEXT_CAPACITY)
    {
        SendMessage(combo, CB_GETLBTEXT, (WPARAM)index, (LPARAM)text);
    }
    InflateRect(&rect, -Scale(pApp, GUI_CONTROL_TEXT_INSET), 0);
    DrawControlText(pApp, text, &rect, foreground, DWRITE_TEXT_ALIGNMENT_LEADING);
    if ((state & ODS_FOCUS) && !(state & ODS_NOFOCUSRECT))
    {
        DrawControlRect(pApp, &rect, &c_castAccentColor);
    }
}

static FORCE_INLINE void PaintSubtitleItem(HWND combo, HDC dc, RECT bounds, int index, UINT state, App* pApp)
{
    RECT rect;

    rect.left = 0;
    rect.top = 0;
    rect.right = bounds.right - bounds.left;
    rect.bottom = bounds.bottom - bounds.top;
    BeginControlPaint(pApp, dc, &bounds);
    DrawSubtitleItem(combo, rect, index, state, pApp);
    EndControlPaint(pApp);
}

/* Owner-draw covers the list items; draw the closed field, border and arrow too,
** because visual styles otherwise retain a white dropdown button. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Draws the closed selection field, border and arrow in addition to owner-drawn
 *  list items, preventing the dropdown button from retaining its light theme.
 */
static FORCE_INLINE void PaintDarkCombo(HWND hWnd, HDC dc, App* pApp)
{
    const int           enabled    = IsWindowEnabled(hWnd);
    const int           focused    = GetFocus() == hWnd;
    const int           arrowWidth = Scale(pApp, 24);
    RECT                client;
    RECT                text;
    const D2D1_COLOR_F* border;
    const D2D1_COLOR_F* foreground;
    D2D1_POINT_2F       left;
    D2D1_POINT_2F       middle;
    D2D1_POINT_2F       right;
    int                 x;
    int                 y;

    GetClientRect(hWnd, &client);
    BeginControlPaint(pApp, dc, &client);
    FillControlRect(pApp, &client, &c_castFieldColor);
    text = client;
    InflateRect(&text, -1, -1);
    text.right -= arrowWidth;
    DrawSubtitleItem(hWnd, text, (int)SendMessage(hWnd, CB_GETCURSEL, 0, 0), ODS_COMBOBOXEDIT | (enabled ? 0 : ODS_DISABLED), pApp);
    border = focused && enabled ? &c_castAccentColor : &c_castBorderColor;
    DrawControlRect(pApp, &client, border);
    x          = client.right - (arrowWidth / 2);
    y = (client.top + client.bottom) / 2;
    left.x = (FLOAT)(x - Scale(pApp, 4));
    left.y = (FLOAT)(y - Scale(pApp, 2));
    middle.x = (FLOAT)x;
    middle.y = (FLOAT)(y + Scale(pApp, 2));
    right.x = (FLOAT)(x + Scale(pApp, 4));
    right.y = left.y;
    foreground = enabled ? &c_castTextColor : &c_castMutedColor;
    cd2dSolidColorBrushSetColor(pApp->paintBrush, foreground);
    cd2dRenderTargetDrawLine((ID2D1RenderTarget*)pApp->paintTarget, left, middle, (ID2D1Brush*)pApp->paintBrush, GUI_ICON_STROKE_WIDTH, NULL);
    cd2dRenderTargetDrawLine((ID2D1RenderTarget*)pApp->paintTarget, middle, right, (ID2D1Brush*)pApp->paintBrush, GUI_ICON_STROKE_WIDTH, NULL);
    EndControlPaint(pApp);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Supplies dark painting and requests repaint after focus, enable, selection,
 *  dropdown and theme changes handled by the default subclass procedure.
 */
static FORCE_INLINE LRESULT CALLBACK DarkComboProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData);

static FORCE_INLINE void DarkCombo_OnSetFocus(HWND hWnd, HWND previousFocus)
{
    FORWARD_WM_SETFOCUS(hWnd, previousFocus, DefSubclassProc);
    (void)InvalidateRect(hWnd, NULL, FALSE);
}

static FORCE_INLINE void DarkCombo_OnKillFocus(HWND hWnd, HWND nextFocus)
{
    FORWARD_WM_KILLFOCUS(hWnd, nextFocus, DefSubclassProc);
    (void)InvalidateRect(hWnd, NULL, FALSE);
}

static FORCE_INLINE void DarkCombo_OnEnable(HWND hWnd, BOOL enabled)
{
    FORWARD_WM_ENABLE(hWnd, enabled, DefSubclassProc);
    (void)InvalidateRect(hWnd, NULL, FALSE);
}

static FORCE_INLINE void DarkCombo_OnPaint(HWND hWnd)
{
    PAINTSTRUCT paint;
    HDC         dc;
    App*        pApp;

    pApp = GetApp(GetParent(hWnd));
    dc   = BeginPaint(hWnd, &paint);
    PaintDarkCombo(hWnd, dc, pApp);
    (void)EndPaint(hWnd, &paint);
}

static FORCE_INLINE BOOL DarkCombo_OnEraseBackground(HWND hWnd, HDC dc)
{
    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(dc);
    return TRUE;
}

static FORCE_INLINE void DarkCombo_OnNcDestroy(HWND hWnd)
{
    (void)RemoveWindowSubclass(hWnd, DarkComboProc, DARK_COMBO_SUBCLASS_ID);
    FORWARD_WM_NCDESTROY(hWnd, DefSubclassProc);
}

static FORCE_INLINE LRESULT DarkCombo_OnSetCurSel(HWND hWnd, int index)
{
    LRESULT result;

    result = FORWARD_CB_SETCURSEL(hWnd, index, DefSubclassProc);
    (void)InvalidateRect(hWnd, NULL, FALSE);
    return result;
}

static FORCE_INLINE LRESULT DarkCombo_OnShowDropdown(HWND hWnd, BOOL show)
{
    LRESULT result;

    result = FORWARD_CB_SHOWDROPDOWN(hWnd, show, DefSubclassProc);
    (void)InvalidateRect(hWnd, NULL, FALSE);
    return result;
}

static FORCE_INLINE void DarkCombo_OnPrintClient(HWND hWnd, HDC dc, UINT flags)
{
    App* pApp;

    UNREFERENCED_PARAMETER(flags);
    pApp = GetApp(GetParent(hWnd));
    PaintDarkCombo(hWnd, dc, pApp);
}

static FORCE_INLINE LRESULT DarkCombo_OnThemeChanged(HWND hWnd)
{
    LRESULT result;

    result = FORWARD_WM_THEMECHANGED(hWnd, DefSubclassProc);
    (void)InvalidateRect(hWnd, NULL, FALSE);
    return result;
}

static FORCE_INLINE LRESULT CALLBACK DarkComboProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData)
{
    UNREFERENCED_PARAMETER(idSubclass);
    UNREFERENCED_PARAMETER(refData);

    switch (message)
    {
        HANDLE_MSG(hWnd, WM_SETFOCUS,      DarkCombo_OnSetFocus);
        HANDLE_MSG(hWnd, WM_KILLFOCUS,     DarkCombo_OnKillFocus);
        HANDLE_MSG(hWnd, WM_ENABLE,        DarkCombo_OnEnable);
        HANDLE_MSG(hWnd, WM_PAINT,         DarkCombo_OnPaint);
        HANDLE_MSG(hWnd, WM_ERASEBKGND,    DarkCombo_OnEraseBackground);
        HANDLE_MSG(hWnd, WM_NCDESTROY,     DarkCombo_OnNcDestroy);
        HANDLE_MSG(hWnd, CB_SETCURSEL,     DarkCombo_OnSetCurSel);
        HANDLE_MSG(hWnd, CB_SHOWDROPDOWN,  DarkCombo_OnShowDropdown);
        HANDLE_MSG(hWnd, WM_PRINTCLIENT,   DarkCombo_OnPrintClient); // NOLINT(performance-no-int-to-ptr): WPARAM carries an HDC.
        HANDLE_MSG(hWnd, WM_THEMECHANGED,  DarkCombo_OnThemeChanged);
    default:
        break;
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Creates the current DPI's message font, replaces the old font and sends it to
 *  all child controls before updating combo item heights.
 */
static FORCE_INLINE void ApplyFont(App* pApp)
{
    NONCLIENTMETRICS metrics;
    HWND             hChild;

    UpdateControlTextFormat(pApp);

    metrics.cbSize = sizeof(metrics);
    if (SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0, pApp->dpi))
    {
        HFONT hFont = CreateFontIndirect(&metrics.lfMessageFont);

        if (NULL != hFont)
        {
            if (NULL != pApp->hFont)
            {
                DeleteObject(pApp->hFont);
            }
            pApp->hFont = hFont;
        }
    }
    for (hChild = GetWindow(pApp->hWnd, GW_CHILD); NULL != hChild; hChild = GetWindow(hChild, GW_HWNDNEXT))
    {
        SendMessage(hChild, WM_SETFONT, (WPARAM)pApp->hFont, TRUE);
    }
    SendMessage(pApp->hSubtitle, CB_SETITEMHEIGHT, 0, Scale(pApp, GUI_COMBO_ITEM_HEIGHT));
    SendMessage(pApp->hSubtitle, CB_SETITEMHEIGHT, (WPARAM)-1, Scale(pApp, GUI_COMBO_FIELD_HEIGHT));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Computes DPI-scaled rows from the client rectangle and reserves the remaining
 *  height for the receiver list, then moves controls and adjusts column widths.
 */
static FORCE_INLINE void Layout(App* pApp)
{
    RECT      client;
    const int margin  = Scale(pApp, 12);
    const int gap     = Scale(pApp, 8);
    const int rowH    = Scale(pApp, 24);
    const int buttonW = Scale(pApp, 96);
    const int smallW  = Scale(pApp, 72);
    const int labelH  = Scale(pApp, 18);
    const int timeW   = Scale(pApp, 150);
    const int valueW  = Scale(pApp, 108);
    const int cfgH               = labelH + gap + (5 * (rowH + gap)); /* label + res + qp + gop + motion + actions */
    const int labelOffset = Scale(pApp, 4);
    const int buttonOffset = Scale(pApp, 2);
    const int listMinHeight = Scale(pApp, 60);
    const int fileLabelWidth = Scale(pApp, 40);
    const int subtitleLabelWidth = Scale(pApp, 64);
    const int subtitleHeight = Scale(pApp, 220);
    const int gainStateWidth = Scale(pApp, 180);
    const int motionValueWidth = Scale(pApp, 48);
    const int addressColumnWidth = Scale(pApp, 120);
    const int modelColumnWidth = Scale(pApp, 90);
    const int columnPadding = Scale(pApp, 24);
    int       width;
    int       sliderX;
    int       sliderWidth;
    int       buttonX;
    int       refreshX;
    int       browseX;
    int       labelY;
    int       headerY;
    int       buttonY;
    int       listLabelWidth;
    int       fileX;
    int       fileWidth;
    int       subtitleX;
    int       subtitleWidth;
    int       gainX;
    int       gainWidth;
    int       gainStateX;
    int       hintX;
    int       hintWidth;
    int       stopX;
    int       seekX;
    int       seekWidth;
    int       timeX;
    int       motionSliderWidth;
    int       motionValueX;
    int       receiverColumnWidth;
    int       y;
    int       listH;

    GetClientRect(pApp->hWnd, &client);
    width               = client.right - (2 * margin);
    sliderX = margin + valueW + gap;
    sliderWidth = width - valueW - gap;
    buttonX = client.right - margin - buttonW;
    refreshX = buttonX - gap - buttonW;
    browseX = buttonX;
    listLabelWidth = refreshX - gap - margin;
    fileX = margin + Scale(pApp, GUI_FILE_LABEL_WIDTH);
    fileWidth = buttonX - gap - fileX;
    subtitleX = margin + Scale(pApp, GUI_SUBTITLE_LABEL_WIDTH);
    subtitleWidth = buttonX - gap - subtitleX;
    gainX = margin + Scale(pApp, GUI_GAIN_LABEL_WIDTH);
    gainWidth = width - Scale(pApp, GUI_GAIN_ROW_RESERVED_WIDTH);
    gainStateX = client.right - margin - gainStateWidth;
    hintX = margin + buttonW + gap;
    hintWidth = width - buttonW - gap;
    stopX = margin + smallW + gap;
    seekX = stopX + smallW + gap;
    timeX = client.right - margin - timeW;
    seekWidth = timeX - gap - seekX;
    motionSliderWidth = sliderWidth - Scale(pApp, GUI_MOTION_SLIDER_RESERVED_WIDTH);
    motionValueX = client.right - margin - motionValueWidth;
    receiverColumnWidth = width - addressColumnWidth - modelColumnWidth - columnPadding;
    /* bottom-up: config panel, status, toolbar, hint, cast, file; the list takes the rest */
    listH = client.bottom - pApp->immersiveWindow.metrics.cyCaption - (margin + labelH + gap) - (GUI_CONTROL_ROW_COUNT * (gap + rowH)) -
            (gap + labelH) - (gap + cfgH) - margin;
    if (listH < listMinHeight)
    {
        listH = listMinHeight;
    }
    y = pApp->immersiveWindow.metrics.cyCaption + margin;
    headerY = y + Scale(pApp, 3);
    buttonY = y - buttonOffset;
    MoveWindow(pApp->hListLabel, margin, headerY, listLabelWidth, labelH, TRUE);
    MoveWindow(pApp->hRefresh, refreshX, buttonY, buttonW, rowH, TRUE);
    MoveWindow(pApp->hWindowsCast, buttonX, buttonY, buttonW, rowH, TRUE);
    y += labelH + gap;
    MoveWindow(pApp->hList, margin, y, width, listH, TRUE);
    y += listH + gap;
    labelY = y + labelOffset;
    MoveWindow(pApp->hFileLabel, margin, labelY, fileLabelWidth, labelH, TRUE);
    MoveWindow(pApp->hFile, fileX, y, fileWidth, rowH, TRUE);
    MoveWindow(pApp->hBrowse, browseX, y, buttonW, rowH, TRUE);
    y += rowH + gap;
    labelY = y + labelOffset;
    MoveWindow(pApp->hSubtitleLabel, margin, labelY, subtitleLabelWidth, labelH, TRUE);
    MoveWindow(pApp->hSubtitle, subtitleX, y, subtitleWidth, subtitleHeight, TRUE);
    MoveWindow(pApp->hSubtitleBrowse, browseX, y, buttonW, rowH, TRUE);
    y += rowH + gap;
    labelY = y + labelOffset;
    MoveWindow(pApp->hGainLabel, margin, labelY, valueW, labelH, TRUE);
    MoveWindow(pApp->hGain, gainX, y, gainWidth, rowH, TRUE);
    MoveWindow(pApp->hGainState, gainStateX, labelY, gainStateWidth, labelH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hCast, margin, y, buttonW, rowH, TRUE);
    labelY = y + labelOffset;
    MoveWindow(pApp->hHint, hintX, labelY, hintWidth, labelH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hPlayPause, margin, y, smallW, rowH, TRUE);
    labelY = y + labelOffset;
    MoveWindow(pApp->hStop, stopX, y, smallW, rowH, TRUE);
    MoveWindow(pApp->hSeek, seekX, y, seekWidth, rowH, TRUE);
    MoveWindow(pApp->hTime, timeX, labelY, timeW, labelH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hStatus, margin, y, width, labelH, TRUE);
    y += labelH + gap;
    /* The transcoder configuration panel. */
    MoveWindow(pApp->hCfgLabel, margin, y, width, labelH, TRUE);
    y += labelH + gap;
    labelY = y + labelOffset;
    MoveWindow(pApp->hResValue, margin, labelY, valueW, labelH, TRUE);
    MoveWindow(pApp->hResSlider, sliderX, y, sliderWidth, rowH, TRUE);
    y += rowH + gap;
    labelY = y + labelOffset;
    MoveWindow(pApp->hQpValue, margin, labelY, valueW, labelH, TRUE);
    MoveWindow(pApp->hQpSlider, sliderX, y, sliderWidth, rowH, TRUE);
    y += rowH + gap;
    labelY = y + labelOffset;
    MoveWindow(pApp->hGopValue, margin, labelY, valueW, labelH, TRUE);
    MoveWindow(pApp->hGopSlider, sliderX, y, sliderWidth, rowH, TRUE);
    y += rowH + gap;
    buttonY = y + buttonOffset;
    labelY = y + labelOffset;
    MoveWindow(pApp->hMotionCheck, margin, buttonY, valueW, rowH, TRUE);
    MoveWindow(pApp->hMotionSlider, sliderX, y, motionSliderWidth, rowH, TRUE);
    MoveWindow(pApp->hMotionValue, motionValueX, labelY, motionValueWidth, labelH, TRUE);
    y += rowH + gap;
    MoveWindow(pApp->hPreview, margin, y, buttonW, rowH, TRUE);
    labelY = y + labelOffset;
    MoveWindow(pApp->hCfgReadout, hintX, labelY, hintWidth, labelH, TRUE);
    ListView_SetColumnWidth(pApp->hList, 0, receiverColumnWidth);
    ListView_SetColumnWidth(pApp->hList, 1, addressColumnWidth);
    ListView_SetColumnWidth(pApp->hList, 2, modelColumnWidth);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Derives control availability from selection, session, preparation and preview
 *  state, then updates the Play/Pause caption.
 */
static FORCE_INLINE void UpdateControls(App* pApp)
{
    int32_t selected;
    int32_t fileLength;
    bool_t  hasSession;
    bool_t  preparingAudio;
    bool_t  busy;
    bool_t  hasDevice;
    bool_t  hasFile;
    bool_t  hasMedia;
    bool_t  hasDuration;
    bool_t  hasOriginalSource;
    bool_t  playing;
    bool_t  canCast;
    bool_t  canStop;
    bool_t  canPause;
    bool_t  canSeek;
    bool_t  canPreview;
    bool_t  canGain;
    LPCTSTR playCaption;

    selected          = ListView_GetNextItem(pApp->hList, -1, LVNI_SELECTED);
    fileLength        = GetWindowTextLength(pApp->hFile);
    hasSession        = (NULL != pApp->pSession);
    preparingAudio    = !!pApp->preparingAudio;
    busy              = hasSession || preparingAudio;
    hasDevice         = (-1 != selected);
    hasFile           = (0 < fileLength);
    hasMedia          = !!pApp->status.hasMedia;
    hasDuration       = (0.0 < pApp->status.duration);
    hasOriginalSource = (L'\0' != pApp->originalSource[0]);
    playing           = (CAST_STATE_PLAYING == pApp->status.state) || (CAST_STATE_BUFFERING == pApp->status.state);
    canCast           = (!busy) && hasDevice && hasFile;
    canStop           = busy && (!pApp->stopping);
    canPause          = hasSession && hasMedia;
    canSeek           = canPause && hasDuration;
    canPreview        = (!preparingAudio) && (!pApp->previewBusy);
    canGain           = (!hasSession) || hasOriginalSource;
    playCaption       = playing ? TEXT("Pause") : TEXT("Play");

    (void)EnableWindow(pApp->hCast, canCast);
    (void)EnableWindow(pApp->hRefresh, !pApp->discovering);
    (void)EnableWindow(pApp->hStop, canStop);
    (void)EnableWindow(pApp->hPlayPause, canPause);
    (void)EnableWindow(pApp->hSeek, canSeek);
    (void)EnableWindow(pApp->hSubtitle, !busy);
    (void)EnableWindow(pApp->hSubtitleBrowse, !busy);
    (void)EnableWindow(pApp->hFile, !busy);
    (void)EnableWindow(pApp->hBrowse, !busy);
    (void)EnableWindow(pApp->hPreview, canPreview);
    (void)EnableWindow(pApp->hGain, canGain);
    (void)SetWindowText(pApp->hPlayPause, playCaption);
}

/* Rewrites the slider value captions from the current transcoder config. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Formats slider captions from the stored transcoder configuration.
 */
static FORCE_INLINE void UpdateConfigLabels(App* pApp)
{
    TCHAR      text[GUI_SHORT_TEXT_CAPACITY];
    NativeText writer;

    NativeText_Initialize(&writer, text, ARRAYSIZE(text));
    NativeText_AppendString(&writer, TEXT("Width  "));
    NativeText_AppendUnsigned(&writer, pApp->transcode.width, 1);
    NativeText_AppendString(&writer, TEXT(" px"));
    (void)SetWindowText(pApp->hResValue, text);
    NativeText_Initialize(&writer, text, ARRAYSIZE(text));
    NativeText_AppendString(&writer, TEXT("Quality  QP "));
    NativeText_AppendSigned(&writer, pApp->transcode.qpInter);
    (void)SetWindowText(pApp->hQpValue, text);
    NativeText_Initialize(&writer, text, ARRAYSIZE(text));
    NativeText_AppendString(&writer, TEXT("Keyframe  "));
    NativeText_AppendUnsigned(&writer, pApp->transcode.gopLength, 1);
    NativeText_AppendCharacter(&writer, TEXT('f'));
    (void)SetWindowText(pApp->hGopValue, text);
    NativeText_Initialize(&writer, text, ARRAYSIZE(text));
    NativeText_AppendSigned(&writer, pApp->transcode.motionStrength);
    NativeText_AppendCharacter(&writer, TEXT('%'));
    (void)SetWindowText(pApp->hMotionValue, text);
}

/* Reads the sliders and checkbox into the transcoder config, snapping where needed, then refreshes
** the captions. Called whenever a control changes so pApp->transcode always mirrors the panel. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Snaps width to a multiple of sixteen, derives intra QP from inter QP and
 *  updates motion controls before refreshing the captions.
 */
static FORCE_INLINE void ReadConfigFromSliders(App* pApp)
{
    int    width;
    int    qp;
    int    qpIntra;
    bool_t motionAdaptive;

    width = (int)SendMessage(pApp->hResSlider, TBM_GETPOS, 0, 0);
    width = (((width + GUI_PROXY_WIDTH_ROUNDING) / GUI_PROXY_WIDTH_ALIGNMENT) * GUI_PROXY_WIDTH_ALIGNMENT); /* proxy width is a multiple of 16 */
    if (width < GUI_PROXY_MIN_WIDTH)
    {
        width = GUI_PROXY_MIN_WIDTH;
    }
    pApp->transcode.width          = (unsigned)width;
    qp                             = (int)SendMessage(pApp->hQpSlider, TBM_GETPOS, 0, 0);
    pApp->transcode.qpInter        = qp;
    qpIntra                        = qp - 4;
    if (qpIntra < 1)
    {
        qpIntra = 1;
    }
    pApp->transcode.qpIntra        = qpIntra; /* keyframes a little sharper */
    pApp->transcode.gopLength      = (unsigned)SendMessage(pApp->hGopSlider, TBM_GETPOS, 0, 0);
    pApp->transcode.motionStrength = (int)SendMessage(pApp->hMotionSlider, TBM_GETPOS, 0, 0);
    motionAdaptive                 = (BST_CHECKED == SendMessage(pApp->hMotionCheck, BM_GETCHECK, 0, 0));
    pApp->transcode.motionAdaptive = motionAdaptive;
    EnableWindow(pApp->hMotionSlider, pApp->transcode.motionAdaptive);
    UpdateConfigLabels(pApp);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Creates the receiver list, media controls and transcoder settings, initializes
 *  their ranges/defaults and installs the painting and seek subclasses.
 */
static FORCE_INLINE void CreateControls(App* pApp)
{
    uint32_t                           index;
    LVCOLUMN                           column;
    const CastGui_ControlDefinition_t* control;
    HWND                               child;

    for (index = 0; index < ARRAYSIZE(c_controls); ++index)
    {
        control = &c_controls[index];
        child   = CreateChild(pApp, control->windowClass, control->text, control->style, control->extendedStyle, control->id);
        CRT_Memory_Copy((byte_t*)pApp + control->destinationOffset, (const byte_t*)&child, sizeof(HWND));
    }

    SetWindowSubclass(pApp->hSubtitle, DarkComboProc, DARK_COMBO_SUBCLASS_ID, (DWORD_PTR)pApp);
    SendMessage(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)TEXT("Automatic"));
    SendMessage(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)TEXT("Off"));
    SendMessage(pApp->hSubtitle, CB_SETCURSEL, 0, 0);
    SendMessage(pApp->hGain, TBM_SETRANGE, TRUE, MAKELPARAM(0, 400));
    SendMessage(pApp->hGain, TBM_SETPOS, TRUE, GUI_ORIGINAL_GAIN_PERCENT);
    SendMessage(pApp->hGain, TBM_SETPAGESIZE, 0, GUI_GAIN_PAGE_SIZE);
    ListView_SetBkColor(pApp->hList, DARK_FIELD);
    ListView_SetTextBkColor(pApp->hList, DARK_FIELD);
    ListView_SetTextColor(pApp->hList, DARK_TEXT);

    /* Transcoder (proxy) configuration panel: sliders whose settings drive the GPU proxy encode. */
    GpuTranscode_InitializeConfig(&pApp->transcode);
    SendMessage(pApp->hResSlider, TBM_SETRANGE, TRUE, MAKELPARAM(320, 1920));
    SendMessage(pApp->hResSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.width);
    SendMessage(pApp->hQpSlider, TBM_SETRANGE, TRUE, MAKELPARAM(18, 40));
    SendMessage(pApp->hQpSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.qpInter);
    SendMessage(pApp->hGopSlider, TBM_SETRANGE, TRUE, MAKELPARAM(6, 90));
    SendMessage(pApp->hGopSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.gopLength);
    SendMessage(pApp->hMotionSlider, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    SendMessage(pApp->hMotionSlider, TBM_SETPOS, TRUE, (LPARAM)pApp->transcode.motionStrength);
    SendMessage(pApp->hMotionCheck, BM_SETCHECK, pApp->transcode.motionAdaptive ? BST_CHECKED : BST_UNCHECKED, 0);

    ListView_SetExtendedListViewStyle(pApp->hList, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    ZeroMemory(&column, sizeof(column));
    column.mask    = LVCF_TEXT | LVCF_WIDTH;
    column.cx      = GUI_DEVICE_COLUMN_WIDTH;
    column.pszText = TEXT("Receiver");
    ListView_InsertColumn(pApp->hList, 0, &column);
    column.pszText = TEXT("Address");
    ListView_InsertColumn(pApp->hList, 1, &column);
    column.pszText = TEXT("Model");
    ListView_InsertColumn(pApp->hList, 2, &column);
    {
        LRESULT headerResult;
        HWND    header;

        headerResult = SendMessage(pApp->hList, LVM_GETHEADER, 0, 0);
        header       = (HWND)headerResult; // NOLINT(performance-no-int-to-ptr): LVM_GETHEADER returns an HWND in LRESULT.
        SetWindowSubclass(header, DarkHeaderProc, DARK_HEADER_SUBCLASS_ID, (DWORD_PTR)pApp);
    }
    SendMessage(pApp->hSeek, TBM_SETRANGE, TRUE, MAKELPARAM(0, SEEK_RANGE));
    /* A click on the channel should jump to the cursor and scrub, not page toward it. */
    SetWindowSubclass(pApp->hSeek, SeekBarProc, SEEK_BAR_SUBCLASS_ID, (DWORD_PTR)pApp);
    ApplyFont(pApp);
    Layout(pApp);
    UpdateConfigLabels(pApp);
    EnableWindow(pApp->hMotionSlider, pApp->transcode.motionAdaptive);
    UpdateControls(pApp);
}

/* ------------------------------------------------------------------ discovery */

/*************************************************************************************************/
/**
 * \par Details:
 *  Writes discovery results into App-owned storage used by the persistent worker.
 */
static FORCE_INLINE void DiscoveryJob(void* pParameter)
{
    App* pApp = (App*)pParameter;

    pApp->discovered.count = Ssdp_DiscoverCastDevices(&pApp->discovery, pApp->discovered.devices, CAST_MAX_DEVICES, DISCOVERY_MS);
    pApp->discovered.error = Ssdp_GetError(&pApp->discovery);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Suppresses duplicate or closing-time requests and restores UI state if the
 *  persistent worker rejects submission.
 */
static FORCE_INLINE void StartDiscovery(App* pApp)
{
    if (pApp->discovering || pApp->closing)
    {
        return;
    }
    pApp->discovering = 1;
    SetStatus(pApp, TEXT("Searching for Cast receivers..."));
    UpdateControls(pApp);
    if (0 == OS_Thread_Submit(&pApp->discoveryWorker))
    {
        pApp->discovering = 0;
        SetStatus(pApp, TEXT("Cannot start discovery."));
        UpdateControls(pApp);
        return;
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Replaces receiver rows from the completed result, prefers the configured name
 *  match and refreshes status and control availability.
 */
static FORCE_INLINE void OnDiscovered(App* pApp, const DiscoveryResult* pResult)
{
    unsigned   index;
    int        preferred = -1;
    TCHAR      text[GUI_DEVICE_TEXT_CAPACITY];
    bool_t     failed;
    NativeText writer;

    pApp->discovering = 0;
    if (NULL != pResult)
    {
        pApp->devices = *pResult;
    }
    else
    {
        pApp->devices.count = 0;
        pApp->devices.error = 0;
    }
    ListView_DeleteAllItems(pApp->hList);
    for (index = 0; index < pApp->devices.count; ++index)
    {
        const CastDevice* pDevice = &pApp->devices.devices[index];
        LVITEM item;
        TCHAR   nativeName[GUI_DEVICE_NAME_CAPACITY];
        TCHAR   nativeAddress[GUI_ADDRESS_CAPACITY];
        TCHAR   nativeModel[GUI_DEVICE_NAME_CAPACITY];

        Utf8ToNative(pDevice->friendlyName, nativeName, ARRAYSIZE(nativeName));
        Utf8ToNative(pDevice->host, nativeAddress, ARRAYSIZE(nativeAddress));
        Utf8ToNative(pDevice->model, nativeModel, ARRAYSIZE(nativeModel));

        ZeroMemory(&item, sizeof(item));
        item.mask    = LVIF_TEXT;
        item.iItem   = (int)index;
        item.pszText = nativeName;
        ListView_InsertItem(pApp->hList, &item);
        ListView_SetItemText(pApp->hList, (int)index, 1, nativeAddress)
        ListView_SetItemText(pApp->hList, (int)index, 2, nativeModel)
        if ((preferred < 0) &&
            ContainsNoCase(pDevice->friendlyName, ARRAYSIZE(pDevice->friendlyName), PREFERRED_DEVICE, ARRAYSIZE(PREFERRED_DEVICE) - 1))
        {
            preferred = (int)index;
        }
    }
    if (pApp->devices.count > 0)
    {
        const int select = (preferred >= 0) ? preferred : 0;

        ListView_SetItemState(pApp->hList, select, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED)
        NativeText_Initialize(&writer, text, ARRAYSIZE(text));
        NativeText_AppendUnsigned(&writer, pApp->devices.count, 1);
        NativeText_AppendString(&writer, TEXT(" receiver"));
        if (1 != pApp->devices.count)
        {
            NativeText_AppendCharacter(&writer, TEXT('s'));
        }
        NativeText_AppendString(&writer, TEXT(" found."));
    }
    else
    {
        NativeText_Initialize(&writer, text, ARRAYSIZE(text));
        NativeText_AppendString(&writer, TEXT("No Cast receiver answered. Is the projector on and on this network?"));
    }
    failed = (0 == pApp->devices.count) && (0 != pApp->devices.error);
    if (failed)
    {
        SetStatus(pApp, TEXT("Discovery failed. Check the network connection and retry."));
    }
    else
    {
        SetStatus(pApp, text);
    }
    UpdateControls(pApp);
}

/* ------------------------------------------------------------------ the session */

/* The session publishes bounded snapshots; the UI consumes them on its timer. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Copies session events into the generation-tagged mailbox for timer-driven
 *  consumption by the UI thread.
 */
static FORCE_INLINE void OnSessionEvent(void* pUser, CastEvent event, const CastStatus* pStatus, const char* pText)
{
    SessionEvents* pEvents = (SessionEvents*)pUser;

    (void)CastEvents_Publish(&pEvents->mailbox, pEvents->generation, event, pStatus, pText);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Runs the Cast session until its conversation ends.
 */
static FORCE_INLINE void SessionJob(void* context)
{
    App* app;

    app = (App*)context;
    LOGASSERT(NULL != app->pSession);
    (void)CastSession_Run(app->pSession);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Skips unchanged source text and remote URLs, enumerates local tracks and
 *  rebuilds the combo with Automatic and Off entries before explicit tracks.
 */
static FORCE_INLINE void RefreshSubtitles(App* pApp)
{
    TCHAR        source[GUI_SOURCE_CAPACITY];
    WCHAR        mediaSource[GUI_SOURCE_CAPACITY];
    TCHAR        automatic[GUI_AUTOMATIC_TEXT_CAPACITY];
    TCHAR        label[GUI_LABEL_CAPACITY];
    const WCHAR* selectedLabel;
    NativeText   writer;
    bool_t       sourceReady;
    bool_t       isRemote;
    int32_t      length;
    int32_t      readLength;

    length      = GetWindowTextLength(pApp->hFile);
    sourceReady = (0 <= length) && (length < (int32_t)ARRAYSIZE(source));
    if (sourceReady)
    {
        readLength  = GetWindowText(pApp->hFile, source, ARRAYSIZE(source));
        sourceReady = (0 <= readLength) && OS_Text_ReadNative((uint16_t*)mediaSource, ARRAYSIZE(mediaSource), source);
    }
    if (!sourceReady)
    {
        source[0]      = TEXT('\0');
        mediaSource[0] = L'\0';
    }
    if (0 == lstrcmp(source, pApp->subtitleSource))
    {
        return;
    }
    (void)lstrcpyn(pApp->subtitleSource, source, ARRAYSIZE(pApp->subtitleSource));
    isRemote = sourceReady && (4 <= length) &&
               (CSTR_EQUAL == CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, source, 4, TEXT("http"), 4));
    pApp->subtitleCount = isRemote ? 0 : Subtitles_Find(mediaSource, pApp->subtitles);
    SendMessage(pApp->hSubtitle, CB_RESETCONTENT, 0, 0);
    selectedLabel = (0 < pApp->subtitleCount) ? pApp->subtitles[0].label : L"no supported subtitles found";
    (void)OS_Text_WriteNative(label, ARRAYSIZE(label), (const uint16_t*)selectedLabel);
    NativeText_Initialize(&writer, automatic, ARRAYSIZE(automatic));
    NativeText_AppendString(&writer, TEXT("Automatic - "));
    NativeText_AppendString(&writer, label);
    (void)SendMessage(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)automatic);
    SendMessage(pApp->hSubtitle, CB_ADDSTRING, 0, (LPARAM)TEXT("Off"));
    for (unsigned i = 0; i < pApp->subtitleCount; ++i)
    {
        (void)AddComboText(pApp->hSubtitle, pApp->subtitles[i].label);
    }
    SendMessage(pApp->hSubtitle, CB_SETCURSEL, 0, 0);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Appends a selected SRT/WebVTT file to the bounded track list and selects it
 *  in the subtitle combo.
 */
static FORCE_INLINE void BrowseSubtitles(App* pApp)
{
    OPENFILENAME dialog;
    TCHAR        nativePath[MAX_PATH];
    WCHAR        path[MAX_PATH];
    bool_t       converted;
    bool_t       selected;
    bool_t       hasCapacity;

    RefreshSubtitles(pApp);
    ZeroMemory(&dialog, sizeof(dialog));
    nativePath[0] = TEXT('\0');
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner   = pApp->hWnd;
    dialog.lpstrFilter = TEXT("Subtitles\0*.srt;*.vtt\0");
    dialog.lpstrFile   = nativePath;
    dialog.nMaxFile    = MAX_PATH;
    dialog.lpstrTitle  = TEXT("Choose subtitles");
    dialog.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    selected    = !!GetOpenFileName(&dialog);
    hasCapacity = (pApp->subtitleCount < SUBTITLE_MAX_TRACKS);
    if (selected && hasCapacity)
    {
        converted = OS_Text_ReadNative((uint16_t*)path, MAX_PATH, nativePath);
        if (converted)
        {
            Subtitles_InitializeFileTrack(path, &pApp->subtitles[pApp->subtitleCount]);
            (void)AddComboText(pApp->hSubtitle, pApp->subtitles[pApp->subtitleCount++].label);
            SendMessage(pApp->hSubtitle, CB_SETCURSEL, (WPARAM)pApp->subtitleCount + 1, 0);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Runs conversion against the submitted context. OS_Thread publishes completion
 *  only after this callback returns.
 */
static FORCE_INLINE void GainJob(void* context)
{
    App* app;

    app         = (App*)context;
    app->gainOk = AudioGain_Convert(app->originalSource,
                                    app->gainOutput,
                                    app->gainPercent,
                                    &app->gainCancel,
                                    &app->gainProgress,
                                    app->gainError,
                                    sizeof(app->gainError));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Latches cancellation without waiting. A running callback retains its output
 *  path until PollJobs acquires completion and deletes unclaimed output.
 */
static FORCE_INLINE void CancelGain(App* app)
{
    KillTimer(app->hWnd, GAIN_TIMER);
    if (!app->gainDiscard)
    {
        ++app->gainSerial;
    }
    app->gainRestartPending = FALSE;
    app->gainDiscard        = TRUE;
    if (app->gainBusy)
    {
        (void)InterlockedExchange(&app->gainCancel, TRUE);
    }
    else if (app->gainOutput[0])
    {
        (void)DeleteUtf16File(app->gainOutput);
        app->gainOutput[0] = TEXT('\0');
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Uses owner access to the completed gain context to restore original audio
 *  or submit conversion of the latest slider value to the startup worker.
 */
static bool_t PrepareGainOutputPath(App* app)
{
    DWORD      length;
    TCHAR      nativeDirectory[MAX_PATH];
    TCHAR      nativePath[MAX_PATH];
    NativeText pathWriter;
    bool_t     valid_path;

    length     = GetTempPath(MAX_PATH, nativeDirectory);
    valid_path = (0 < length) && (length < MAX_PATH);
    if (valid_path)
    {
        NativeText_Initialize(&pathWriter, nativePath, ARRAYSIZE(nativePath));
        NativeText_AppendString(&pathWriter, nativeDirectory);
        NativeText_AppendString(&pathWriter, TEXT("cvkcast_gain_"));
        NativeText_AppendUnsigned(&pathWriter, GetCurrentProcessId(), 1);
        NativeText_AppendCharacter(&pathWriter, TEXT('_'));
        NativeText_AppendUnsigned(&pathWriter, app->gainSerial, 1);
        NativeText_AppendString(&pathWriter, TEXT(".mp4"));
        valid_path = pathWriter.complete;
        if (valid_path)
        {
            valid_path = OS_Text_ReadNative((uint16_t*)app->gainOutput, MAX_PATH, nativePath);
        }
    }
    return valid_path;
}

static FORCE_INLINE void StartGain(App* app)
{
    uint32_t   percent;
    bool_t     has_session;
    bool_t     has_source;
    bool_t     can_prepare;
    bool_t     use_original;
    bool_t     valid_path;

    LOGASSERT(!app->gainBusy);
    LOGASSERT(!app->closing);
    LOGASSERT(!app->stopping);
    LOGASSERT(!app->finishPending);

    percent                 = (uint32_t)SendMessage(app->hGain, TBM_GETPOS, 0, 0);
    app->gainRestartPending = FALSE;
    app->gainDiscard        = FALSE;
    has_session             = (NULL != app->pSession) || app->preparingAudio;
    has_source              = !!app->originalSource[0];
    can_prepare             = has_session && has_source;
    if (!can_prepare)
    {
        return;
    }
    use_original = (GUI_ORIGINAL_GAIN_PERCENT == percent) && (!app->audioNeedsConversion) && (!app->preparingAudio);
    if (use_original)
    {
        CastSession_ReplaceMedia(app->pSession, app->originalUrl, app->originalType);
        SetWindowText(app->hGainState, TEXT("Original audio"));
        return;
    }
    valid_path = PrepareGainOutputPath(app);
    if (valid_path)
    {
        app->gainPercent = percent;
        (void)InterlockedExchange(&app->gainCancel, 0);
        (void)InterlockedExchange(&app->gainProgress, 0);
        app->gainActiveSerial = app->gainSerial;
        app->gainBusy         = OS_Thread_Submit(&app->gainWorker);
        SetWindowText(app->hGainState, app->gainBusy ? TEXT("Preparing gain...") : TEXT("Cannot start audio worker"));
        if ((!app->gainBusy) && app->preparingAudio)
        {
            FinishSession(app, TEXT("Cannot start audio preparation."));
        }
    }
    else
    {
        SetWindowText(app->hGainState, TEXT("Cannot create gain file"));
        if (app->preparingAudio)
        {
            FinishSession(app, TEXT("Cannot create the audio preparation file."));
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Coalesces gain changes while cancellation is pending. The timer starts the
 *  latest slider value only after the previous callback releases its context.
 */
static FORCE_INLINE void ScheduleGain(App* app)
{
    bool_t can_schedule;

    CancelGain(app);
    can_schedule            = (!app->closing) && (!app->stopping) && (!app->finishPending);
    app->gainRestartPending = can_schedule;
    if (can_schedule && (!app->gainBusy))
    {
        StartGain(app);
    }
}

/*************************************************************************************************/
/**
 * \brief Reads the numeric host from an existing locally served media URL.
 *
 * \par Details:
 *  The counted URL and IPv4 readers validate every component before the
 *  output is written. Output is empty on failure.
 *
 * \param[in]  source           Terminated source URL
 * \param[in]  sourceCapacity   Readable source bytes, including termination
 * \param[out] output           Numeric host text
 * \param[in]  outputCapacity   Writable bytes, including termination
 *
 * \retval TRUE    A complete terminated IPv4 host was written.
 * \retval FALSE   The URL or output capacity is unsuitable.
 */
static FORCE_INLINE bool_t ReadGainLocalAddress(const char_t* source, uint32_t sourceCapacity, char_t* output, uint32_t outputCapacity)
{
    Utl_Url_t    original_url;
    uint32_t     original_url_bytes;
    uint32_t     host_address;
    uint32_t     host_bytes;
    bool_t       is_terminated;
    bool_t       can_parse_url;
    bool_t       parsed_url;
    bool_t       is_ipv4_host;
    bool_t       valid_host;
    bool_t       wrote_host;

    LOGASSERT(NULL != source);
    LOGASSERT(NULL != output);
    LOGASSERT(0 < outputCapacity);

    host_address = 0;
    Utl_Url_Initialize(&original_url);
    original_url_bytes = Utl_Unicode_GetUtf8StringByteCount(source, sourceCapacity);
    is_terminated      = (UTL_UNICODE_NO_TERMINATOR != original_url_bytes);
    can_parse_url      = is_terminated && (0 < original_url_bytes);
    parsed_url         = FALSE;
    if (can_parse_url)
    {
        parsed_url = Utl_Url_Read(&original_url, (const byte_t*)source, original_url_bytes);
    }
    is_ipv4_host = parsed_url && (UTL_URL_HOST_IPV4 == original_url.hostKind);
    valid_host   = FALSE;
    if (is_ipv4_host)
    {
        valid_host = Utl_Url_ReadIpv4(original_url.host, &host_address);
    }
    CRT_Memory_Set(output, 0, outputCapacity);
    host_bytes = 0;
    if (valid_host)
    {
        host_bytes = Utl_Url_WriteIpv4(host_address, (byte_t*)output, outputCapacity - 1);
    }
    wrote_host = (0 < host_bytes);

    return wrote_host;
}

/*************************************************************************************************/
/**
 * \brief Writes a served gain URL from counted, validated components.
 *
 * \par Details:
 *  The caller provides a numeric IPv4 host and a native listener port. Output
 *  stays terminated even if the fixed writer rejects the representation.
 *
 * \param[in]  local            Terminated numeric IPv4 address
 * \param[in]  localCapacity    Readable host bytes including termination
 * \param[in]  port             Assigned media listener port
 * \param[out] output           Fixed URL storage
 * \param[in]  outputCapacity   Writable bytes including termination
 * \param[out] byteCount        Serialized URL bytes excluding termination
 *
 * \retval TRUE    The complete URL was written.
 * \retval FALSE   A component or output bound was invalid.
 */
static FORCE_INLINE bool_t WriteGainUrl(const char_t* local, uint32_t localCapacity, uint16_t port, char_t* output, uint32_t outputCapacity, uint32_t* byteCount)
{
    Utl_Url_t url;
    byte_t    port_text[GAIN_PORT_CAPACITY];
    uint32_t  host_bytes;
    uint32_t  port_bytes;
    bool_t    valid_host;
    bool_t    valid_port;
    bool_t    written;

    LOGASSERT(NULL != local);
    LOGASSERT(NULL != output);
    LOGASSERT(NULL != byteCount);
    LOGASSERT(0 < outputCapacity);

    Utl_Url_Initialize(&url);
    host_bytes = Utl_Unicode_GetUtf8StringByteCount(local, localCapacity);
    port_bytes = Utl_Decimal_WriteUint64(port, port_text, sizeof(port_text));
    valid_host = (UTL_UNICODE_NO_TERMINATOR != host_bytes) && (0 < host_bytes);
    valid_port = (0 < port_bytes);
    url.scheme.data      = (const byte_t*)GAIN_URL_SCHEME;
    url.scheme.byteCount = sizeof(GAIN_URL_SCHEME) - 1;
    url.host.data        = (const byte_t*)local;
    url.host.byteCount   = valid_host ? host_bytes : 0;
    url.port.data        = port_text;
    url.port.byteCount   = port_bytes;
    url.path.data        = (const byte_t*)GAIN_URL_RESOURCE;
    url.path.byteCount   = sizeof(GAIN_URL_RESOURCE) - 1;
    url.hostKind         = UTL_URL_HOST_IPV4;
    url.hasScheme        = TRUE;
    url.hasAuthority     = TRUE;
    url.hasPort          = TRUE;
    CRT_Memory_Set(output, 0, outputCapacity);
    (*byteCount) = 0;
    written = FALSE;
    if (valid_host && valid_port)
    {
        written = Utl_Url_Write(&url, (byte_t*)output, outputCapacity - 1, byteCount);
    }

    return written;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Consumes an acquired completion, serves successful converted output and
 *  either starts the prepared session or requests a media replacement.
 */
static FORCE_INLINE void FinishGain(App* app, unsigned serial)
{
    GainVariant* variant;
    uint32_t     index;
    bool_t       has_local_address;
    bool_t       has_variant;
    bool_t       can_serve;
    bool_t       wrote_url;
    uint32_t     url_bytes;
    char         local[CAST_SESSION_HOST_CAPACITY];
    char         error[GAIN_ERROR_CAPACITY];
    char         url[CAST_MEDIA_URL_CAPACITY];
    LOGASSERT(!app->gainBusy);
    LOGASSERT(serial == app->gainSerial);
    if ((!app->pSession && !app->preparingAudio) || !app->gainOk)
    {
        TCHAR message[GAIN_ERROR_CAPACITY];

        Utf8ToNative(app->gainError, message, GAIN_ERROR_CAPACITY);
        (void)SetWindowText(app->hGainState, TEXT("Audio preparation failed"));
        if (app->preparingAudio)
        {
            FinishSession(app, message);
        }
        else
        {
            CancelGain(app);
            SetStatus(app, message);
        }
        return;
    }
    variant = NULL;
    for (index = 0; index < MEDIA_SERVER_CAPACITY; ++index)
    {
        if (TEXT('\0') == app->gainVariantStorage[index].path[0])
        {
            variant = &app->gainVariantStorage[index];
            break;
        }
    }
    /* Use the same reachable local address selected for the original file. */
    has_local_address = ReadGainLocalAddress(app->originalUrl, sizeof(app->originalUrl), local, sizeof(local));
    has_variant = (NULL != variant);
    can_serve   = has_variant && has_local_address;
    if (!can_serve)
    {
        CancelGain(app);
        if (app->preparingAudio)
        {
            FinishSession(app, TEXT("Cannot serve prepared audio."));
        }
        (void)SetWindowText(app->hGainState, TEXT("Cannot serve audio gain"));
        return;
    }
    variant->server = MediaServer_Start(&app->serving, app->gainOutput, NULL, 0, GAIN_MEDIA_TYPE, error, sizeof(error));
    if (!variant->server)
    {
        CancelGain(app);
        if (app->preparingAudio)
        {
            FinishSession(app, TEXT("Cannot serve prepared audio."));
        }
        (void)SetWindowText(app->hGainState, TEXT("Cannot serve audio gain"));
        return;
    }
    (void)CopyUtf16(variant->path, MAX_PATH, app->gainOutput, ARRAYSIZE(app->gainOutput));
    app->gainOutput[0] = 0;
    variant->next      = app->gainVariants;
    app->gainVariants  = variant;
    wrote_url = WriteGainUrl(local, sizeof(local), MediaServer_GetPort(variant->server), url, sizeof(url), &url_bytes);
    LOGASSERT(wrote_url);
    if (app->preparingAudio)
    {
        CRT_Memory_Copy(app->pendingConfig.contentUrl, url, (uint64_t)url_bytes + 1U);
        CRT_Memory_Copy(app->pendingConfig.contentType, GAIN_MEDIA_TYPE, sizeof(GAIN_MEDIA_TYPE));
        ++app->events.generation;
        CastEvents_Begin(&app->events.mailbox, app->events.generation);
        CastSession_Prepare(&app->session, &app->pendingConfig);
        app->pSession    = &app->session;
        app->sessionBusy = OS_Thread_Submit(&app->sessionWorker);
        if (!app->sessionBusy)
        {
            FinishSession(app, TEXT("Cannot start the Cast session."));
            return;
        }
        app->preparingAudio = 0;
        ZeroMemory(&app->status, sizeof(app->status));
        UpdateControls(app);
        (void)SetWindowText(app->hGainState, TEXT("AAC audio ready"));
    }
    else
    {
        CastSession_ReplaceMedia(app->pSession, url, GAIN_MEDIA_TYPE);
        (void)SetWindowText(app->hGainState, TEXT("Gain ready - switching"));
    }
}

static bool_t LoadCastSubtitle(App* pApp, const SubtitleTrack** ppSubtitle,
                                const byte_t** ppVtt, uint64_t* pVttSize,
                                char* error)
{
    int32_t choice;
    bool_t automatic_selected;
    bool_t track_selected;
    TCHAR message[GUI_ERROR_CAPACITY];

    RefreshSubtitles(pApp);
    choice = (int32_t)SendMessage(pApp->hSubtitle, CB_GETCURSEL, 0, 0);
    automatic_selected = (0 == choice) && (0 < pApp->subtitleCount);
    track_selected = (2 <= choice) && ((uint32_t)(choice - 2) < pApp->subtitleCount);
    if (automatic_selected)
    {
        *ppSubtitle = &pApp->subtitles[0];
    }
    else if (track_selected)
    {
        *ppSubtitle = &pApp->subtitles[choice - 2];
    }
    if (NULL == *ppSubtitle)
    {
        return TRUE;
    }
    if (!Subtitles_Load(&pApp->subtitleWorkspace, *ppSubtitle, error, GUI_ERROR_CAPACITY))
    {
        Utf8ToNative(error, message, GUI_ERROR_CAPACITY);
        SetStatus(pApp, message);
        return FALSE;
    }
    *ppVtt = Subtitles_GetText(&pApp->subtitleWorkspace);
    *pVttSize = Subtitles_GetByteCount(&pApp->subtitleWorkspace);
    return TRUE;
}

static FORCE_INLINE void PrepareRemoteCastContent(const WCHAR* source, CastSessionConfig* pConfig)
{
    const WCHAR* pExtension;
    const char* contentType;
    bool_t hasExtension;
    bool_t extensionIsPath;

    pExtension = Utf16FindLast(source, GUI_SOURCE_CAPACITY, L'.');
    hasExtension = (NULL != pExtension);
    extensionIsPath = FALSE;
    if (hasExtension)
    {
        extensionIsPath = Utf16Contains(pExtension,
                                         (uint32_t)((source + GUI_SOURCE_CAPACITY) - pExtension), L'/');
    }
    ToUtf8(source, pConfig->contentUrl, sizeof(pConfig->contentUrl));
    ToUtf8(source, pConfig->title, sizeof(pConfig->title));
    contentType = "video/mp4";
    if (hasExtension && !extensionIsPath)
    {
        contentType = MediaServer_GetContentTypeForExtension(pExtension);
    }
    (void)CopyAscii(pConfig->contentType, ARRAYSIZE(pConfig->contentType),
                    contentType, CAST_SESSION_TYPE_CAPACITY);
}

static bool_t PrepareLocalCastContent(App* pApp, const CastDevice* pDevice,
                                      const WCHAR* source, const byte_t* vtt,
                                      uint64_t vttSize, CastSessionConfig* pConfig,
                                      char* localAddress, char* error)
{
    const WCHAR* pName;
    const WCHAR* pExtension;
    char extension[GUI_EXTENSION_CAPACITY];
    ByteText urlWriter;
    TCHAR message[GUI_ERROR_CAPACITY];

    pName = Utf16FindLast(source, GUI_SOURCE_CAPACITY, L'\\');
    pExtension = Utf16FindLast(source, GUI_SOURCE_CAPACITY, L'.');
    pName = (NULL != pName) ? pName + 1 : source;
    if (NULL != pExtension && pExtension < pName)
    {
        pExtension = NULL;
    }
    ToUtf8(pName, pConfig->title, sizeof(pConfig->title));
    ToUtf8(NULL != pExtension ? pExtension : L"", extension, sizeof(extension));
    (void)CopyAscii(pConfig->contentType, ARRAYSIZE(pConfig->contentType),
                    MediaServer_GetContentTypeForExtension(pExtension), CAST_SESSION_TYPE_CAPACITY);
    pConfig->servingLocally = 1;
    if (!CastNetwork_ReadLocalAddress(&pApp->routeNetwork, pDevice->host,
                                      sizeof(pDevice->host), localAddress, GUI_ADDRESS_CAPACITY))
    {
        SetStatus(pApp, TEXT("Cannot determine the local address facing the receiver."));
        return FALSE;
    }
    pApp->pServer = MediaServer_StartWithSubtitles(&pApp->serving, source, NULL,
                                                   DEFAULT_SERVER_PORT, pConfig->contentType,
                                                   vtt, vttSize, error, GUI_ERROR_CAPACITY);
    if (NULL == pApp->pServer)
    {
        Utf8ToNative(error, message, GUI_ERROR_CAPACITY);
        SetStatus(pApp, message);
        return FALSE;
    }
    ByteText_Initialize(&urlWriter, pConfig->contentUrl, ARRAYSIZE(pConfig->contentUrl));
    ByteText_AppendString(&urlWriter, "http://", ARRAYSIZE("http://"));
    ByteText_AppendString(&urlWriter, localAddress, GUI_ADDRESS_CAPACITY);
    ByteText_AppendCharacter(&urlWriter, ':');
    ByteText_AppendUnsigned(&urlWriter, MediaServer_GetPort(pApp->pServer));
    ByteText_AppendString(&urlWriter, "/media", ARRAYSIZE("/media"));
    ByteText_AppendString(&urlWriter, extension, ARRAYSIZE(extension));
    pConfig->keyframeCount = Mp4Index_ReadKeyframes(source, pApp->keyframes, MP4_INDEX_MAX_KEYFRAMES);
    pConfig->pKeyframes = pApp->keyframes;
    (void)CopyUtf16(pApp->proxySource, MAX_PATH, source, GUI_SOURCE_CAPACITY);
    (void)CopyAscii(pApp->proxyLocal, ARRAYSIZE(pApp->proxyLocal), localAddress, GUI_ADDRESS_CAPACITY);
    pApp->proxyPort = (unsigned short)(MediaServer_GetPort(pApp->pServer) + 1);
    (void)CopyUtf16(pApp->originalSource, MAX_PATH, source, GUI_SOURCE_CAPACITY);
    return TRUE;
}

static bool_t ServeCastSubtitles(App* pApp, const CastDevice* pDevice,
                                 const byte_t* vtt, uint64_t vttSize,
                                 const SubtitleTrack* subtitle,
                                 CastSessionConfig* pConfig,
                                 char* localAddress, char* error)
{
    ByteText urlWriter;

    if (NULL == pApp->pServer)
    {
        if (CastNetwork_ReadLocalAddress(&pApp->routeNetwork, pDevice->host,
                                         sizeof(pDevice->host), localAddress, GUI_ADDRESS_CAPACITY))
        {
            pApp->pServer = MediaServer_StartWithSubtitles(&pApp->serving, NULL, NULL,
                                                           DEFAULT_SERVER_PORT, "video/mp4",
                                                           vtt, vttSize, error, GUI_ERROR_CAPACITY);
        }
        if (NULL == pApp->pServer)
        {
            SetStatus(pApp, TEXT("Cannot serve subtitles to the receiver."));
            return FALSE;
        }
    }
    ByteText_Initialize(&urlWriter, pConfig->subtitleUrl, ARRAYSIZE(pConfig->subtitleUrl));
    ByteText_AppendString(&urlWriter, "http://", ARRAYSIZE("http://"));
    ByteText_AppendString(&urlWriter, localAddress, GUI_ADDRESS_CAPACITY);
    ByteText_AppendCharacter(&urlWriter, ':');
    ByteText_AppendUnsigned(&urlWriter, MediaServer_GetPort(pApp->pServer));
    ByteText_AppendString(&urlWriter, "/subtitles.vtt", ARRAYSIZE("/subtitles.vtt"));
    (void)CopyAscii(pConfig->subtitleLanguage, ARRAYSIZE(pConfig->subtitleLanguage),
                    subtitle->language, ARRAYSIZE(subtitle->language));
    return TRUE;
}

static bool_t PrepareCastAudio(App* pApp, const WCHAR* source,
                               int wantsProxy, const CastSessionConfig* pConfig,
                               char* error)
{
    bool_t needs_gain;
    TCHAR message[GUI_ERROR_CAPACITY];

    (void)CopyAscii(pApp->originalUrl, ARRAYSIZE(pApp->originalUrl),
                    pConfig->contentUrl, ARRAYSIZE(pConfig->contentUrl));
    (void)CopyAscii(pApp->originalType, ARRAYSIZE(pApp->originalType),
                    pConfig->contentType, ARRAYSIZE(pConfig->contentType));
    pApp->audioNeedsConversion = 0;
    if (!wantsProxy || L'\0' == pApp->originalSource[0])
    {
        return TRUE;
    }
    pApp->audioNeedsConversion = AudioGain_NeedsConversion(source, error, GUI_ERROR_CAPACITY);
    if (0 > pApp->audioNeedsConversion)
    {
        Utf8ToNative(error, message, GUI_ERROR_CAPACITY);
        FinishSession(pApp, message);
        return FALSE;
    }
    needs_gain = !!pApp->audioNeedsConversion ||
                 (GUI_ORIGINAL_GAIN_PERCENT != SendMessage(pApp->hGain, TBM_GETPOS, 0, 0));
    if (needs_gain)
    {
        pApp->pendingConfig = *pConfig;
        pApp->preparingAudio = 1;
        pApp->proxyLocal[0] = '\0';
        pApp->stopping = 0;
        SetStatus(pApp, TEXT("Preparing AAC audio before playback; video quality is preserved. Stop cancels."));
        UpdateControls(pApp);
        ScheduleGain(pApp);
        return FALSE;
    }
    return TRUE;
}

static void StartPreparedCastSession(App* pApp, const CastSessionConfig* pConfig, int wantsProxy)
{
    bool_t needs_gain;

    ++pApp->events.generation;
    CastEvents_Begin(&pApp->events.mailbox, pApp->events.generation);
    CastSession_Prepare(&pApp->session, pConfig);
    pApp->pSession = &pApp->session;
    pApp->sessionBusy = OS_Thread_Submit(&pApp->sessionWorker);
    if (!pApp->sessionBusy)
    {
        FinishSession(pApp, TEXT("Cannot start the Cast session."));
        return;
    }
    if (wantsProxy && '\0' != pApp->proxyLocal[0])
    {
        Proxy_Request_t request;

        request.serving = &pApp->serving;
        request.source = pApp->proxySource;
        request.sourceCapacity = MAX_PATH;
        request.localAddress = pApp->proxyLocal;
        request.localAddressCapacity = sizeof(pApp->proxyLocal);
        request.config = &pApp->transcode;
        request.port = pApp->proxyPort;
        request.session = pApp->pSession;
        (void)Proxy_Start(&pApp->proxy, &request);
    }
    else
    {
        pApp->proxyLocal[0] = '\0';
    }
    ZeroMemory(&pApp->status, sizeof(pApp->status));
    pApp->stopping = 0;
    UpdateControls(pApp);
    needs_gain = wantsProxy &&
                 (GUI_ORIGINAL_GAIN_PERCENT != SendMessage(pApp->hGain, TBM_GETPOS, 0, 0));
    if (needs_gain)
    {
        ScheduleGain(pApp);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Prepares the selected receiver and source, optional subtitle serving and
 *  audio conversion. Normal casting may also start a scrub proxy; previews use
 *  the supplied transcoded file directly.
 */
static FORCE_INLINE void StartCastSource(App* pApp, const WCHAR* pSourceIn, uint32_t sourceCapacity, int buildProxy)
{
    int32_t selected;
    uint32_t deviceCount;
    uint32_t safeSelected;
    const CastDevice* pDevice;
    CastSessionConfig config;
    WCHAR source[GUI_SOURCE_CAPACITY];
    char error[GUI_ERROR_CAPACITY];
    char localAddress[GUI_ADDRESS_CAPACITY];
    const byte_t* vtt = NULL;
    uint64_t vttSize = 0;
    const SubtitleTrack* subtitle = NULL;
    bool_t selection_in_range;
    bool_t can_start;
    bool_t source_ready;
    bool_t wants_proxy;
    bool_t is_remote;

    selected = ListView_GetNextItem(pApp->hList, -1, LVNI_SELECTED);
    deviceCount = pApp->devices.count;
    safeSelected = 0;
    selection_in_range = FALSE;
    if (0 <= selected)
    {
        selection_in_range = ((uint32_t)selected < deviceCount);
        if (selection_in_range)
        {
            safeSelected = (uint32_t)selected;
        }
    }
    pDevice = &pApp->devices.devices[safeSelected];
    can_start = !pApp->closing && !pApp->finishPending && !pApp->gainBusy &&
                NULL == pApp->pSession && !pApp->preparingAudio && selection_in_range;
    if (!can_start)
    {
        return;
    }
    source_ready = CopyUtf16(source, ARRAYSIZE(source), pSourceIn, sourceCapacity);
    if (!source_ready || L'\0' == source[0])
    {
        return;
    }
    wants_proxy = (0 != buildProxy);
    ZeroMemory(&config, sizeof(config));
    (void)CopyAscii(config.host, ARRAYSIZE(config.host), pDevice->host, ARRAYSIZE(pDevice->host));
    config.callback = OnSessionEvent;
    config.pUser = &pApp->events;
    if (wants_proxy && !LoadCastSubtitle(pApp, &subtitle, &vtt, &vttSize, error))
    {
        return;
    }
    pApp->originalSource[0] = L'\0';
    is_remote = Utf16StartsWithNoCase(source, ARRAYSIZE(source), L"http://", GUI_HTTP_PREFIX_LENGTH) ||
                Utf16StartsWithNoCase(source, ARRAYSIZE(source), L"https://", GUI_HTTPS_PREFIX_LENGTH);
    if (is_remote)
    {
        PrepareRemoteCastContent(source, &config);
    }
    else if (!PrepareLocalCastContent(pApp, pDevice, source, vtt, vttSize,
                                      &config, localAddress, error))
    {
        return;
    }
    if (NULL != vtt && !ServeCastSubtitles(pApp, pDevice, vtt, vttSize,
                                           subtitle, &config, localAddress, error))
    {
        return;
    }
    if (!PrepareCastAudio(pApp, source, wants_proxy, &config, error))
    {
        return;
    }
    StartPreparedCastSession(pApp, &config, wants_proxy);
}

/* The Cast button: play the chosen file with the background scrub proxy. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Reads the source edit control and starts normal casting with proxy generation.
 */
static FORCE_INLINE void StartCast(App* pApp)
{
    WCHAR source[GUI_SOURCE_CAPACITY];

    (void)ReadControlText(pApp->hFile, source);
    StartCastSource(pApp, source, ARRAYSIZE(source), 1);
}

/* A short transcode of the source at the current settings, run on a worker thread so the sliders
** stay responsive. OS_Thread publishes the App-owned result after callback return. */
static unsigned ParsePreviewFrames(const char* report)
{
    const char* pMarker;
    const char* pDigits;
    uint64_t parsed;
    uint32_t digitsRead;
    uint32_t digitCount;
    bool_t is_digit;
    bool_t numberReady;

    pMarker = FindAscii(report, GUI_PREVIEW_REPORT_CAPACITY,
                        "video frames from", ARRAYSIZE("video frames from") - 1);
    if (NULL == pMarker)
    {
        return 0;
    }
    for (pDigits = pMarker; pDigits > report; --pDigits)
    {
        is_digit = ('0' <= pDigits[-1]) && ('9' >= pDigits[-1]);
        if (is_digit)
        {
            break;
        }
    }
    for (;;)
    {
        if (pDigits == report)
        {
            break;
        }
        is_digit = ('0' <= pDigits[-1]) && ('9' >= pDigits[-1]);
        if (!is_digit)
        {
            break;
        }
        --pDigits;
    }
    digitCount = (uint32_t)(pMarker - pDigits);
    numberReady = Utl_Decimal_ReadUint64(&parsed, &digitsRead,
                                         (const byte_t*)pDigits, digitCount);
    if (numberReady && digitsRead == digitCount && parsed <= UINT32_MAX)
    {
        return (unsigned)parsed;
    }
    return 0;
}

static int32_t ParsePreviewMotion(const char* report)
{
    const char* pMarker;
    const char* pDigits;
    uint64_t parsed;
    uint32_t digitsRead;
    uint32_t digitCount;
    bool_t numberReady;

    pMarker = FindAscii(report, GUI_PREVIEW_REPORT_CAPACITY,
                        "avg motion ", ARRAYSIZE("avg motion ") - 1);
    if (NULL == pMarker)
    {
        return 0;
    }
    pDigits = pMarker + (ARRAYSIZE("avg motion ") - 1);
    digitCount = (uint32_t)((report + GUI_PREVIEW_REPORT_CAPACITY) - pDigits);
    numberReady = Utl_Decimal_ReadUint64(&parsed, &digitsRead,
                                         (const byte_t*)pDigits, digitCount);
    if (numberReady && parsed <= INT32_MAX)
    {
        return (int32_t)parsed;
    }
    return 0;
}

static unsigned ReadPreviewKbps(const PreviewJob* pJob, unsigned produced)
{
    HANDLE hFile;
    TCHAR nativePath[MAX_PATH];
    LARGE_INTEGER size;
    double seconds;

    if (!OS_Text_WriteNative(nativePath, MAX_PATH, (const uint16_t*)pJob->dest))
    {
        return 0;
    }
    hFile = CreateFile(nativePath, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (INVALID_HANDLE_VALUE == hFile)
    {
        return 0;
    }
    size.QuadPart = 0;
    GetFileSizeEx(hFile, &size);
    CloseHandle(hFile);
    seconds = (produced > 0) ? (double)produced / GUI_NOMINAL_FPS : 1.0;
    return (unsigned)((double)size.QuadPart * GUI_BITS_PER_BYTE / GUI_KBPS_DIVISOR /
                      (seconds > GUI_MIN_BITRATE_SECONDS ? seconds : GUI_MIN_BITRATE_SECONDS));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Transcodes the requested clip into App-owned result storage, derives the
 *  preview readout from its report/file size. The owner acquires it through OS_Thread.
 */
static FORCE_INLINE void PreviewJobRun(void* context)
{
    PreviewJob* pJob = (PreviewJob*)context;
    PreviewResult* pResult = pJob->pResult;
    char report[GUI_PREVIEW_REPORT_CAPACITY];

    ZeroMemory(pResult, sizeof(*pResult));
    (void)CopyUtf16(pResult->path, MAX_PATH, pJob->dest, ARRAYSIZE(pJob->dest));
    pResult->ok = GpuTranscode_WriteMp4(&pJob->gpuDevice,
                                        pJob->source,
                                        pJob->dest,
                                        &pJob->config,
                                        pJob->startFrame,
                                        pJob->frames,
                                        pJob->pCancel,
                                        report,
                                        sizeof(report));
    if (pResult->ok)
    {
        pResult->width = pJob->config.width;
        pResult->frames = ParsePreviewFrames(report);
        pResult->motionPercent = ParsePreviewMotion(report);
        pResult->kbps = ReadPreviewKbps(pJob, pResult->frames);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Captures the current settings and estimates a clip start from playback position,
 *  then starts a worker using App-owned job and result storage.
 */
static FORCE_INLINE void StartPreview(App* pApp)
{
    WCHAR       source[MAX_PATH];
    DWORD       length;
    TCHAR       nativeTemp[MAX_PATH];
    TCHAR       nativePath[MAX_PATH];
    NativeText  pathWriter;
    PreviewJob* pJob;
    int         position;
    double      ratio;
    double      duration;
    bool_t      isClosing;
    bool_t      previewBusy;
    bool_t      preparingAudio;
    bool_t      canPrepare;
    bool_t      sourceRead;
    bool_t      hasSource;
    bool_t      isRemote;
    bool_t      hasLocalSource;
    bool_t      pathReady;
    bool_t      sourceCopied;
    bool_t      hasDuration;
    bool_t      hasPosition;
    bool_t      submitted;

    isClosing       = !!pApp->closing;
    previewBusy     = !!pApp->previewBusy;
    preparingAudio  = !!pApp->preparingAudio;
    canPrepare      = (!isClosing) && (!previewBusy) && (!preparingAudio);
    hasLocalSource  = FALSE;
    pathReady       = FALSE;
    pJob            = &pApp->previewJob;
    if (canPrepare)
    {
        sourceRead    = ReadControlText(pApp->hFile, source);
        hasSource     = sourceRead && (L'\0' != source[0]);
        isRemote      = hasSource && Utf16StartsWithNoCase(source, ARRAYSIZE(source), L"http", 4);
        hasLocalSource = hasSource && (!isRemote);
        if (!hasLocalSource)
        {
            (void)SetWindowText(pApp->hCfgReadout, TEXT("Preview needs a local video file."));
        }
    }
    if (hasLocalSource)
    {
        ReadConfigFromSliders(pApp);
        length    = GetTempPath(MAX_PATH, nativeTemp);
        pathReady = (0 < length) && (length < MAX_PATH);
        if (pathReady)
        {
            NativeText_Initialize(&pathWriter, nativePath, ARRAYSIZE(nativePath));
            NativeText_AppendString(&pathWriter, nativeTemp);
            NativeText_AppendString(&pathWriter, TEXT("cvkcast_preview_"));
            NativeText_AppendUnsigned(&pathWriter, GetCurrentProcessId(), 1);
            NativeText_AppendString(&pathWriter, TEXT(".mp4"));
            pathReady = pathWriter.complete;
        }
        if (pathReady)
        {
            pathReady = OS_Text_ReadNative((uint16_t*)pJob->dest, MAX_PATH, nativePath);
        }
        if (pathReady)
        {
            sourceCopied = CopyUtf16(pJob->source, MAX_PATH, source, ARRAYSIZE(source));
            pathReady    = sourceCopied;
        }
    }
    if (pathReady)
    {
        pJob->config        = pApp->transcode;
        pJob->hWnd          = pApp->hWnd;
        pJob->pResult       = &pApp->previewResult;
        pApp->previewCancel = 0;
        pJob->pCancel       = &pApp->previewCancel;
        /* Start near the current playback position, or a quarter in. */
        position    = (int)SendMessage(pApp->hSeek, TBM_GETPOS, 0, 0);
        hasDuration = (0.0 < pApp->status.duration);
        hasPosition = (0 < position);
        duration    = hasDuration ? pApp->status.duration : GUI_DEFAULT_DURATION_SECONDS;
        ratio       = GUI_DEFAULT_PREVIEW_RATIO;
        if (hasDuration && hasPosition)
        {
            ratio = ((double)position / (double)SEEK_RANGE);
        }
        pJob->startFrame = (unsigned)((ratio * GUI_NOMINAL_FPS) * duration);
        pJob->frames     = GUI_PREVIEW_SECONDS * GUI_PREVIEW_FPS;

        pApp->previewBusy = TRUE;
        (void)EnableWindow(pApp->hPreview, FALSE);
        (void)SetWindowText(pApp->hCfgReadout, TEXT("Building preview..."));
        submitted = OS_Thread_Submit(&pApp->previewWorker);
        if (!submitted)
        {
            pApp->previewBusy = FALSE;
            (void)EnableWindow(pApp->hPreview, TRUE);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Cancels preparation or posts a single Stop request for the active session.
 */
static FORCE_INLINE void StopCast(App* pApp)
{
    if (pApp->preparingAudio)
    {
        FinishSession(pApp, TEXT("Audio preparation cancelled."));
        return;
    }
    if (NULL != pApp->pSession && !pApp->stopping)
    {
        pApp->stopping = 1;
        CastSession_Post(pApp->pSession, CAST_COMMAND_STOP, 0);
        UpdateControls(pApp);
    }
}

/* Moves the slider and the readout to a position, unless the user holds the slider. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Leaves the slider under user control during dragging; otherwise updates its
 *  position and formatted state/time readout.
 */
static FORCE_INLINE void ShowPosition(App* pApp, double seconds)
{
    TCHAR duration[GUI_TIME_TEXT_CAPACITY];
    TCHAR position[GUI_TIME_TEXT_CAPACITY];
    TCHAR state[GUI_STATE_TEXT_CAPACITY];
    TCHAR text[GUI_STATUS_TEXT_CAPACITY];
    NativeText writer;
    HWND hSeek;
    double durationSeconds;
    bool_t seeking;

    hSeek           = pApp->hSeek;
    durationSeconds = pApp->status.duration;
    seeking         = pApp->seeking;
    if (seeking)
    {
        return;
    }
    if (durationSeconds > 0)
    {
        if (seconds > durationSeconds)
        {
            seconds = durationSeconds;
        }
        SendMessage(hSeek,
                    TBM_SETPOS,
                    TRUE,
                    (LPARAM)(LONG)((seconds / durationSeconds * SEEK_RANGE) + GUI_ROUNDING_HALF));
    }
    else
    {
        SendMessage(hSeek, TBM_SETPOS, TRUE, 0);
    }
    if (pApp->status.hasMedia)
    {
        FormatTime(seconds, position, ARRAYSIZE(position));
        FormatTime(pApp->status.duration, duration, ARRAYSIZE(duration));
        Utf8ToNative(pApp->status.stateName, state, ARRAYSIZE(state));
        NativeText_Initialize(&writer, text, ARRAYSIZE(text));
        NativeText_AppendString(&writer, state);
        NativeText_AppendString(&writer, TEXT("  "));
        NativeText_AppendString(&writer, position);
        NativeText_AppendString(&writer, TEXT(" / "));
        NativeText_AppendString(&writer, duration);
    }
    else
    {
        Utf8ToNative(pApp->status.stateName, text, ARRAYSIZE(text));
    }
    (void)SetWindowText(pApp->hTime, text);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Copies the latest status and records its receipt tick before refreshing the
 *  position display and controls.
 */
static FORCE_INLINE void OnStatus(App* pApp, const CastStatus* pStatus)
{
    if (NULL == pStatus)
    {
        return;
    }
    pApp->status     = *pStatus;
    pApp->statusTick = GetTickCount64();
    ShowPosition(pApp, pApp->status.currentTime);
    UpdateControls(pApp);
}

/* Between the receiver's reports, advance the display at playback rate. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Extrapolates the displayed position between receiver reports while playback
 *  is active and the user is not dragging.
 */
static FORCE_INLINE void OnPositionTimer(App* pApp)
{
    bool_t   hasSession;
    bool_t   playing;
    bool_t   hasMedia;
    bool_t   seeking;
    bool_t   advancePosition;
    uint64_t elapsed;
    double   seconds;

    hasSession      = (NULL != pApp->pSession);
    playing         = (CAST_STATE_PLAYING == pApp->status.state);
    hasMedia        = !!pApp->status.hasMedia;
    seeking         = !!pApp->seeking;
    advancePosition = hasSession && playing && hasMedia && (!seeking);
    if (advancePosition)
    {
        elapsed = GetTickCount64() - pApp->statusTick;
        seconds = pApp->status.currentTime + ((double)elapsed / GUI_MILLISECONDS_PER_SECOND);
        ShowPosition(pApp, seconds);
    }
}

/* Reaps the session thread; safe to call when no session exists. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Defers session destruction until its callback has returned, then stops serving
 *  and conversion resources and clears session UI state.
 */
static void RetireGainVariant(App* pApp)
{
    GainVariant* variant;
    bool_t       variant_stopped;
    bool_t       file_removed;
    uint32_t     error;

    variant         = pApp->gainVariants;
    variant_stopped = MediaServer_TryStop(&variant->server);
    if (variant_stopped)
    {
        file_removed = DeleteUtf16File(variant->path);
        if (!file_removed)
        {
            error        = GetLastError();
            file_removed = (ERROR_FILE_NOT_FOUND == error) || (ERROR_PATH_NOT_FOUND == error);
        }
        if (file_removed)
        {
            pApp->gainVariants = variant->next;
            variant->next = NULL;
            variant->path[0] = TEXT('\0');
        }
    }
}

static FORCE_INLINE void FinishSession(App* pApp, LPCTSTR pReason)
{
    bool_t       session_complete;
    bool_t       session_failed;
    bool_t       proxy_stopped;
    bool_t       server_stopped;
    bool_t       variants_stopped;
    bool_t       workers_complete;
    bool_t       servers_complete;
    bool_t       can_release;
    bool_t       has_reason;
    bool_t       saved_reason;

    if (pApp->sessionBusy)
    {
        session_complete = OS_Thread_TakeResult(&pApp->sessionWorker);
        session_failed   = (OS_THREAD_FAILED == OS_Thread_GetState(&pApp->sessionWorker));
        if (session_complete || session_failed)
        {
            pApp->sessionBusy = FALSE;
        }
    }
    CancelGain(pApp);
    proxy_stopped    = Proxy_TryStop(&pApp->proxy);
    server_stopped   = MediaServer_TryStop(&pApp->pServer);
    variants_stopped = (NULL == pApp->gainVariants);
    workers_complete = (!pApp->sessionBusy) && (!pApp->gainBusy);
    if (workers_complete && (!variants_stopped))
    {
        /* Retire at most one gain variant per cleanup attempt. */
        RetireGainVariant(pApp);
        variants_stopped = (NULL == pApp->gainVariants);
    }
    servers_complete = proxy_stopped && server_stopped && variants_stopped;
    can_release      = workers_complete && servers_complete;
    if (!can_release)
    {
        pApp->finishPending = TRUE;
        has_reason          = (NULL != pReason);
        saved_reason        = (pReason == pApp->finishReason);
        if (has_reason && (!saved_reason))
        {
            (void)lstrcpyn(pApp->finishReason, pReason, CAST_EVENTS_TEXT_CAPACITY);
        }
    }
    else
    {
        pApp->proxyLocal[0] = 0;
        if (NULL != pApp->pSession)
        {
            CastSession_Reset(pApp->pSession);
        }
        pApp->pSession             = NULL;
        pApp->originalSource[0]    = 0;
        pApp->preparingAudio       = 0;
        pApp->audioNeedsConversion = 0;
        ZeroMemory(&pApp->pendingConfig, sizeof(pApp->pendingConfig));
        (void)SetWindowText(pApp->hGainState, TEXT(""));
        pApp->stopping      = 0;
        pApp->finishPending = 0;
        ZeroMemory(&pApp->status, sizeof(pApp->status));
        SendMessage(pApp->hSeek, TBM_SETPOS, TRUE, 0);
        (void)SetWindowText(pApp->hTime, TEXT(""));
        if (NULL != pReason)
        {
            SetStatus(pApp, pReason);
        }
        UpdateControls(pApp);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Consumes one mailbox snapshot on the UI thread and retries deferred session
 *  cleanup after handling status, log and terminal events.
 */
static FORCE_INLINE void PollSessionEvents(App* pApp)
{
    CastEvents_Snapshot_t snapshot;
    TCHAR                 nativeText[CAST_EVENTS_TEXT_CAPACITY];

    if (CastEvents_Take(&pApp->events.mailbox, &snapshot))
    {
        if (snapshot.present & CAST_EVENTS_STATUS)
        {
            OnStatus(pApp, &snapshot.status);
        }
        if (snapshot.present & CAST_EVENTS_LOG)
        {
            Utf8ToNative(snapshot.log, nativeText, CAST_EVENTS_TEXT_CAPACITY);
            SetStatus(pApp, nativeText);
        }
        if (snapshot.present & CAST_EVENTS_ENDED)
        {
            Utf8ToNative(snapshot.reason, nativeText, CAST_EVENTS_TEXT_CAPACITY);
            FinishSession(pApp, nativeText);
        }
    }
    if (pApp->finishPending)
    {
        FinishSession(pApp, pApp->finishReason);
    }
}

/* Moves to a fraction of the duration. final: the drag ended, so seek exactly and resume;
** otherwise a live scrub preview (paused, keyframe-snapped in the session). */
/*************************************************************************************************/
/**
 * \par Details:
 *  Clamps a timeline fraction and posts either a scrub update or a final seek.
 *  The local slider/readout follows the requested target immediately.
 */
static FORCE_INLINE void SeekToRatio(App* pApp, double ratio, int final)
{
    CastSession_t* session;
    double         durationSeconds;
    double seconds;

    session         = pApp->pSession;
    durationSeconds = pApp->status.duration;
    if (NULL == session || durationSeconds <= 0)
    {
        return;
    }
    if (ratio < 0.0)
    {
        ratio = 0.0;
    }
    if (ratio > 1.0)
    {
        ratio = 1.0;
    }
    seconds = durationSeconds * ratio;
    SendMessage(pApp->hSeek, TBM_SETPOS, TRUE, (LPARAM)(LONG)((ratio * SEEK_RANGE) + GUI_ROUNDING_HALF));
    if (final)
    {
        pApp->seeking = 0;
        CastSession_Post(pApp->pSession, CAST_COMMAND_SEEK, seconds);
        pApp->status.currentTime = seconds;
        pApp->statusTick         = GetTickCount64();
        ShowPosition(pApp, seconds);
    }
    else
    {
        TCHAR      preview[GUI_TIME_TEXT_CAPACITY];
        TCHAR      duration[GUI_TIME_TEXT_CAPACITY];
        TCHAR      text[GUI_SHORT_TEXT_CAPACITY];
        NativeText writer;

        pApp->seeking = 1;
        CastSession_Post(pApp->pSession, CAST_COMMAND_SCRUB, seconds);
        FormatTime(seconds, preview, ARRAYSIZE(preview));
        FormatTime(pApp->status.duration, duration, ARRAYSIZE(duration));
        NativeText_Initialize(&writer, text, ARRAYSIZE(text));
        NativeText_AppendString(&writer, TEXT("SEEK  "));
        NativeText_AppendString(&writer, preview);
        NativeText_AppendString(&writer, TEXT(" / "));
        NativeText_AppendString(&writer, duration);
        (void)SetWindowText(pApp->hTime, text);
    }
}

/* The click position as a fraction of the channel: how far along the media the cursor points. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Maps the mouse coordinate into the trackbar channel's horizontal span.
 *  A nonpositive span produces zero.
 */
static FORCE_INLINE double GetRatioFromClick(HWND hSeek, int x)
{
    RECT    channel;
    double  span;
    double  offset;
    bool_t  has_span;
    double  ratio;

    SendMessage(hSeek, TBM_GETCHANNELRECT, 0, (LPARAM)&channel);
    span     = (double)channel.right - (double)channel.left;
    offset   = (double)x - (double)channel.left;
    has_span = 0.0 < span;
    ratio    = 0.0;
    if (has_span)
    {
        ratio = offset / span;
    }
    return ratio;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Releases the subclass before forwarding native nonclient destruction.
 */
static FORCE_INLINE void SeekBar_OnNcDestroy(HWND hWnd)
{
    (void)RemoveWindowSubclass(hWnd, SeekBarProc, SEEK_BAR_SUBCLASS_ID);
    FORWARD_WM_NCDESTROY(hWnd, DefSubclassProc);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  A captured button drag updates the preview without ending the seek.
 */
static FORCE_INLINE void SeekBar_OnMouseMove(HWND hWnd, int x, int y, UINT keyFlags)
{
    App*   app;
    bool_t has_capture;
    bool_t button_down;
    bool_t dragging;
    double ratio;

    UNREFERENCED_PARAMETER(y);

    has_capture = (hWnd == GetCapture());
    button_down = !!(keyFlags & MK_LBUTTON);
    dragging    = has_capture && button_down;
    if (dragging)
    {
        app   = GetApp(GetParent(hWnd));
        ratio = GetRatioFromClick(hWnd, x);
        SeekToRatio(app, ratio, FALSE);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Starts a captured scrub at the clicked channel position.
 */
static FORCE_INLINE void SeekBar_OnLButtonDown(HWND hWnd, BOOL doubleClick, int x, int y, UINT keyFlags)
{
    App*   app;
    double ratio;

    UNREFERENCED_PARAMETER(doubleClick);
    UNREFERENCED_PARAMETER(y);
    UNREFERENCED_PARAMETER(keyFlags);

    app   = GetApp(GetParent(hWnd));
    ratio = GetRatioFromClick(hWnd, x);
    (void)SetFocus(hWnd);
    (void)SetCapture(hWnd);
    SeekToRatio(app, ratio, FALSE);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Completes a captured seek. Uncaptured releases retain trackbar default handling.
 */
static FORCE_INLINE void SeekBar_OnLButtonUp(HWND hWnd, int x, int y, UINT keyFlags)
{
    App*   app;
    bool_t has_capture;
    double ratio;

    has_capture = (hWnd == GetCapture());
    if (has_capture)
    {
        app   = GetApp(GetParent(hWnd));
        ratio = GetRatioFromClick(hWnd, x);
        (void)ReleaseCapture();
        SeekToRatio(app, ratio, TRUE);
    }
    else
    {
        FORWARD_WM_LBUTTONUP(hWnd, x, y, keyFlags, DefSubclassProc);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Uses typed mouse crackers and forwards all other messages to the subclass chain.
 */
static FORCE_INLINE LRESULT CALLBACK SeekBarProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR idSubclass, DWORD_PTR refData)
{
    UNREFERENCED_PARAMETER(idSubclass);
    UNREFERENCED_PARAMETER(refData);

    switch (message)
    {
        HANDLE_MSG(hWnd, WM_NCDESTROY,   SeekBar_OnNcDestroy);
        HANDLE_MSG(hWnd, WM_MOUSEMOVE,   SeekBar_OnMouseMove);
        HANDLE_MSG(hWnd, WM_LBUTTONDOWN, SeekBar_OnLButtonDown);
        HANDLE_MSG(hWnd, WM_LBUTTONUP,   SeekBar_OnLButtonUp);
    default:
        break;
    }
    return DefSubclassProc(hWnd, message, wParam, lParam);
}

/* Keyboard (arrows, page keys) still scrolls the slider through the default proc; seek to
** wherever it lands, exactly. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Translates keyboard-driven slider movement into a final seek; mouse thumb
 *  tracking is handled by the subclass.
 */
static FORCE_INLINE void OnSeekScroll(App* pApp, UINT code)
{
    const int position = (int)SendMessage(pApp->hSeek, TBM_GETPOS, 0, 0);
    CastSession_t* session = pApp->pSession;
    double duration = pApp->status.duration;

    if (NULL == session || duration <= 0 || TB_THUMBTRACK == code)
    {
        return; /* mouse tracking is handled by the subclass */
    }
    CastSession_Post(session, CAST_COMMAND_SEEK, duration * position / SEEK_RANGE);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Opens the media file dialog and copies a successful selection to the source
 *  control. An existing URL is not used as the initial local filename.
 */
static FORCE_INLINE void Browse(App* pApp)
{
    TCHAR        file[MAX_PATH];
    OPENFILENAME dialog;
    int32_t      length;
    bool_t       is_url;
    bool_t       fits;

    length  = GetWindowTextLength(pApp->hFile);
    fits    = (0 <= length) && (length < (int32_t)ARRAYSIZE(file));
    file[0] = TEXT('\0');
    if (fits)
    {
        (void)GetWindowText(pApp->hFile, file, ARRAYSIZE(file));
    }
    is_url = fits && (4 <= length) &&
             (CSTR_EQUAL == CompareString(LOCALE_INVARIANT, NORM_IGNORECASE, file, 4, TEXT("http"), 4));
    if (is_url)
    {
        file[0] = TEXT('\0');
    }
    ZeroMemory(&dialog, sizeof(dialog));
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner   = pApp->hWnd;
    dialog.lpstrFilter = TEXT("Video files\0*.mp4;*.m4v;*.mov;*.webm;*.mkv\0Audio ")
                         TEXT("files\0*.mp3;*.m4a;*.aac;*.flac;*.wav;*.ogg\0All files\0*.*\0");
    dialog.lpstrFile  = file;
    dialog.nMaxFile   = MAX_PATH;
    dialog.lpstrTitle = TEXT("Choose a video to cast");
    dialog.Flags      = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileName(&dialog))
    {
        (void)SetWindowText(pApp->hFile, file);
        UpdateControls(pApp);
    }
}

/* Windows' own flyout for wireless (Miracast) displays: the path for a projector that is a
** Windows display rather than a Cast receiver. */
/*************************************************************************************************/
/**
 * \par Details:
 *  Opens the Windows wireless-display discovery URI through the shell.
 */
static FORCE_INLINE void OpenWindowsCast(App* pApp)
{
    ShellExecute(pApp->hWnd, TEXT("open"), TEXT("ms-settings-connectabledevices:devicediscovery"), NULL, NULL, SW_SHOWNORMAL);
}

/* ------------------------------------------------------------------ the window */

/*************************************************************************************************/
/**
 * \par Details:
 *  Establishes completion polling before acquiring worker resources. Failure to
 *  create the timer rejects window creation without starting any worker.
 *  A partial worker initialization is closed by the established timer.
 */
static FORCE_INLINE BOOL CastWindow_OnCreate(HWND hWnd, LPCREATESTRUCT pCreate)
{
    App*     pApp;
    UINT_PTR timer;
    bool_t   timer_ready;
    BOOL     dark;
    bool_t   serving_ready;
    bool_t   storage_ready;
    bool_t   discovery_storage_ready;
    bool_t   route_ready;
    bool_t   discovery_ready;
    bool_t   gain_ready;
    bool_t   session_ready;
    bool_t   preview_ready;
    bool_t   presenter_ready;
    bool_t   state_ready;
    bool_t   execution_ready;
    bool_t   workers_ready;

    UNREFERENCED_PARAMETER(pCreate);

    pApp        = GetApp(hWnd);
    pApp->hWnd  = hWnd;
    timer       = SetTimer(hWnd, POSITION_TIMER, POSITION_TICK_MS, NULL);
    timer_ready = (0 != timer);
    if (timer_ready)
    {
        dark                  = TRUE;
        pApp->dpi             = GetDpiForWindow(hWnd);
        pApp->backgroundBrush = CreateSolidBrush(DARK_BACKGROUND);
        pApp->fieldBrush      = CreateSolidBrush(DARK_FIELD);
        InitControlPaint(pApp);
        UpdateControlTextFormat(pApp);
        CastEvents_Initialize(&pApp->events.mailbox);
        Subtitles_Initialize(&pApp->subtitleWorkspace, pApp->subtitleScratch, pApp->subtitleOutput, SUBTITLE_TEXT_CAPACITY);
        /* Optional proxy startup failure keeps original-stream casting available. */
        (void)Proxy_Initialize(&pApp->proxy);
        serving_ready           = MediaServer_Initialize(&pApp->serving);
        storage_ready           = CastSession_Initialize(&pApp->session);
        discovery_storage_ready = Ssdp_Initialize(&pApp->discovery);
        route_ready             = OS_Network_Initialize(&pApp->routeNetwork);
        discovery_ready         = OS_Thread_Initialize(&pApp->discoveryWorker, DiscoveryJob, pApp);
        gain_ready              = OS_Thread_Initialize(&pApp->gainWorker, GainJob, pApp);
        session_ready           = OS_Thread_Initialize(&pApp->sessionWorker, SessionJob, pApp);
        preview_ready           = OS_Thread_Initialize(&pApp->previewWorker, PreviewJobRun, &pApp->previewJob);
        presenter_ready         = OS_Thread_Initialize(&pApp->presenterWorker, PresentCastWindow, pApp);
        state_ready             = serving_ready && storage_ready && discovery_storage_ready && route_ready;
        execution_ready         = discovery_ready && gain_ready && session_ready && preview_ready && presenter_ready;
        workers_ready           = state_ready && execution_ready;
        DwmSetWindowAttribute(hWnd, GUI_DARK_MODE_ATTRIBUTE /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
        CreateControls(pApp);
        if (workers_ready)
        {
            StartDiscovery(pApp);
        }
        else
        {
            pApp->closing = TRUE;
            EnableWindow(hWnd, FALSE);
        }
    }

    return timer_ready;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Fills the client area with the retained background brush.
 */
static FORCE_INLINE BOOL CastWindow_OnEraseBackground(HWND hWnd, HDC dc)
{
    UNREFERENCED_PARAMETER(hWnd);
    UNREFERENCED_PARAMETER(dc);
    return TRUE;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Selects field or background colors and returns the corresponding retained
 *  brush, using muted text for disabled controls.
 */
static FORCE_INLINE HBRUSH CastWindow_ControlColor(HWND hWnd, HDC dc, HWND hControl, int type)
{
    App* pApp;
    int  field;

    pApp = GetApp(hWnd);

    field = (type == CTLCOLOR_EDIT) || (type == CTLCOLOR_LISTBOX) || (hControl == pApp->hSubtitle);
    SetTextColor(dc, IsWindowEnabled(hControl) ? DARK_TEXT : DARK_MUTED);
    SetBkColor(dc, field ? DARK_FIELD : DARK_BACKGROUND);
    return field ? pApp->fieldBrush : pApp->backgroundBrush;
}

static FORCE_INLINE HBRUSH CastWindow_OnCtlColorEdit(HWND hWnd, HDC dc, HWND hControl, int type)
{
    return CastWindow_ControlColor(hWnd, dc, hControl, type);
}

static FORCE_INLINE HBRUSH CastWindow_OnCtlColorListBox(HWND hWnd, HDC dc, HWND hControl, int type)
{
    return CastWindow_ControlColor(hWnd, dc, hControl, type);
}

static FORCE_INLINE HBRUSH CastWindow_OnCtlColorButton(HWND hWnd, HDC dc, HWND hControl, int type)
{
    return CastWindow_ControlColor(hWnd, dc, hControl, type);
}

static FORCE_INLINE HBRUSH CastWindow_OnCtlColorStatic(HWND hWnd, HDC dc, HWND hControl, int type)
{
    return CastWindow_ControlColor(hWnd, dc, hControl, type);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Sets the subtitle combo's DPI-scaled item height and forwards other requests.
 */
static FORCE_INLINE void CastWindow_OnMeasureItem(HWND hWnd, MEASUREITEMSTRUCT* item)
{
    App* pApp;

    pApp = GetApp(hWnd);
    if (item->CtlType == ODT_COMBOBOX && item->CtlID == ID_SUBTITLE)
    {
        item->itemHeight = (UINT)Scale(pApp, GUI_COMBO_ITEM_HEIGHT);
        return;
    }
    FORWARD_WM_MEASUREITEM(hWnd, item, DefWindowProc);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Draws subtitle entries or dark buttons with selection/focus state; forwards
 *  unhandled control types to default window processing.
 */
static FORCE_INLINE void CastWindow_OnDrawItem(HWND hWnd, const DRAWITEMSTRUCT* item)
{
    App*                pApp;
    RECT                rect;
    const D2D1_COLOR_F* fill;
    const D2D1_COLOR_F* foreground;
    TCHAR               label[GUI_STATUS_TEXT_CAPACITY];

    pApp = GetApp(hWnd);
    if (item->CtlType == ODT_COMBOBOX && item->CtlID == ID_SUBTITLE)
    {
        PaintSubtitleItem(item->hwndItem, item->hDC, item->rcItem, item->itemID == (UINT)-1 ? -1 : (int)item->itemID, item->itemState, pApp);
        return;
    }
    if (item->CtlType != ODT_BUTTON)
    {
        FORWARD_WM_DRAWITEM(hWnd, item, DefWindowProc);
        return;
    }
    rect.left = 0;
    rect.top = 0;
    rect.right = item->rcItem.right - item->rcItem.left;
    rect.bottom = item->rcItem.bottom - item->rcItem.top;
    fill = (0 != (item->itemState & ODS_SELECTED)) ? &c_castSelectedColor : &c_castFieldColor;
    foreground = (0 != (item->itemState & ODS_DISABLED)) ? &c_castMutedColor : &c_castTextColor;
    BeginControlPaint(pApp, item->hDC, &item->rcItem);
    FillControlRect(pApp, &rect, fill);
    (void)GetWindowText(item->hwndItem, label, ARRAYSIZE(label));
    DrawControlText(pApp, label, &rect, foreground, DWRITE_TEXT_ALIGNMENT_CENTER);
    if (0 != (item->itemState & ODS_FOCUS))
    {
        InflateRect(&rect, -3, -3);
        DrawControlRect(pApp, &rect, &c_castAccentColor);
    }
    EndControlPaint(pApp);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Callback publication replaces posted completion pointers. Cancellation keeps
 *  the context and output path alive until the callback has returned.
 */
static void PollGainJob(App* app)
{
    bool_t failed;
    bool_t completed;
    bool_t discard;

    if (app->gainBusy)
    {
        completed = OS_Thread_TakeResult(&app->gainWorker);
        failed    = (OS_THREAD_FAILED == OS_Thread_GetState(&app->gainWorker));
        if (completed || failed)
        {
            app->gainBusy = FALSE;
            discard       = app->gainDiscard || app->closing || app->finishPending || app->stopping;
            if (discard)
            {
                if (app->gainOutput[0])
                {
                    (void)DeleteUtf16File(app->gainOutput);
                    app->gainOutput[0] = TEXT('\0');
                }
            }
            else
            {
                if (failed)
                {
                    app->gainOk = FALSE;
                }
                FinishGain(app, app->gainActiveSerial);
            }
        }
    }
}

static FORCE_INLINE void PollJobs(App* app)
{
    bool_t failed;
    bool_t completed;
    bool_t restart;

    failed = (OS_THREAD_FAILED == OS_Thread_GetState(&app->sessionWorker));
    if (app->sessionBusy && failed)
    {
        FinishSession(app, TEXT("Session worker stopped."));
    }

    PollGainJob(app);
    if (app->previewBusy)
    {
        completed = OS_Thread_TakeResult(&app->previewWorker);
        failed    = (OS_THREAD_FAILED == OS_Thread_GetState(&app->previewWorker));
        if (completed || failed)
        {
            if (failed)
            {
                app->previewResult.ok = FALSE;
            }
            OnPreviewDone(app->hWnd, &app->previewResult);
        }
    }
    restart =
        app->gainRestartPending && (!app->gainBusy) && (!app->closing) && (!app->finishPending) && (!app->stopping);
    if (restart)
    {
        StartGain(app);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Polls worker/session progress, advances the position readout and handles
 *  debounced subtitle/gain jobs. Closing polls all worker exits without waiting.
 */
static void TryCloseWindow(HWND hWnd, App* pApp)
{
    bool_t discovery_closed;
    bool_t gain_closed;
    bool_t session_closed;
    bool_t preview_closed;
    bool_t presenter_closed;
    bool_t jobs_closed;
    bool_t can_release;
    bool_t can_destroy;
    bool_t session_storage_closed;
    bool_t discovery_storage_closed;
    bool_t route_closed;
    bool_t proxy_closed;
    bool_t serving_closed;
    bool_t destroyed;

    discovery_closed = OS_Thread_TryClose(&pApp->discoveryWorker);
    gain_closed      = FALSE;
    session_closed   = FALSE;
    preview_closed   = FALSE;
    if (!pApp->gainBusy)
    {
        gain_closed = OS_Thread_TryClose(&pApp->gainWorker);
    }
    if (!pApp->sessionBusy)
    {
        session_closed = OS_Thread_TryClose(&pApp->sessionWorker);
    }
    if (!pApp->previewBusy)
    {
        preview_closed = OS_Thread_TryClose(&pApp->previewWorker);
    }
    presenter_closed = OS_Thread_TryClose(&pApp->presenterWorker);
    jobs_closed = discovery_closed && gain_closed && session_closed && preview_closed && presenter_closed;
    can_release = jobs_closed && (!pApp->pSession) && (!pApp->finishPending);
    can_destroy = FALSE;
    if (can_release)
    {
        session_storage_closed   = CastSession_TryClose(&pApp->session);
        discovery_storage_closed = Ssdp_TryClose(&pApp->discovery);
        route_closed             = OS_Network_TryClose(&pApp->routeNetwork);
        proxy_closed             = Proxy_TryClose(&pApp->proxy);
        serving_closed           = FALSE;
        if (proxy_closed)
        {
            serving_closed = MediaServer_TryClose(&pApp->serving);
        }
        can_destroy = session_storage_closed && discovery_storage_closed && route_closed && proxy_closed && serving_closed;
    }
    if (can_destroy)
    {
        destroyed = !!DestroyWindow(hWnd);
        LOGASSERT(destroyed);
    }
}

static FORCE_INLINE void CastWindow_OnTimer(HWND hWnd, UINT id)
{
    App*   pApp = GetApp(hWnd);
    bool_t discovery_failed;

    if (POSITION_TIMER == id)
    {
        PollSessionEvents(pApp);
        PollJobs(pApp);
        if (pApp->finishPending)
        {
            FinishSession(pApp, pApp->finishReason);
        }
        if (pApp->closing)
        {
            TryCloseWindow(hWnd, pApp);
            return;
        }
        if (OS_Thread_TakeResult(&pApp->discoveryWorker))
        {
            OnDiscovered(pApp, &pApp->discovered);
        }
        discovery_failed = (OS_THREAD_FAILED == OS_Thread_GetState(&pApp->discoveryWorker));
        if (pApp->discovering && discovery_failed)
        {
            pApp->discovering = FALSE;
            SetStatus(pApp, TEXT("Discovery worker stopped."));
            UpdateControls(pApp);
        }
        OnPositionTimer(pApp);
        if (pApp->gainBusy)
        {
            TCHAR      progress[GUI_PROGRESS_TEXT_CAPACITY];
            NativeText writer;
            LONG       gainProgress;

            gainProgress = InterlockedCompareExchange(&pApp->gainProgress, 0, 0);
            NativeText_Initialize(&writer, progress, ARRAYSIZE(progress));
            NativeText_AppendString(&writer, TEXT("Preparing AAC ("));
            NativeText_AppendUnsigned(&writer, pApp->gainPercent, 1);
            NativeText_AppendString(&writer, TEXT("%): "));
            NativeText_AppendSigned(&writer, gainProgress);
            NativeText_AppendCharacter(&writer, TEXT('%'));
            (void)SetWindowText(pApp->hGainState, progress);
        }
    }
    else if (id == SUBTITLE_TIMER)
    {
        KillTimer(hWnd, SUBTITLE_TIMER);
        if (!pApp->closing && !pApp->finishPending && !pApp->gainBusy && !pApp->pSession && !pApp->preparingAudio)
        {
            RefreshSubtitles(pApp);
        }
    }
    else if (id == GAIN_TIMER)
    {
        KillTimer(hWnd, GAIN_TIMER);
        if (!pApp->closing)
        {
            ScheduleGain(pApp);
        }
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  None
 */
static FORCE_INLINE void CastWindow_OnSize(HWND hWnd, UINT state, int width, int height)
{
    App* pApp;

    UNREFERENCED_PARAMETER(state);
    UNREFERENCED_PARAMETER(width);
    UNREFERENCED_PARAMETER(height);

    pApp = GetApp(hWnd);
    Layout(pApp);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Applies the DPI-scaled minimum tracking size.
 */
static FORCE_INLINE void CastWindow_OnGetMinMaxInfo(HWND hWnd, MINMAXINFO* pInfo)
{
    App* pApp;

    pApp = GetApp(hWnd);

    pInfo->ptMinTrackSize.x = Scale(pApp, GUI_MIN_WINDOW_WIDTH);
    pInfo->ptMinTrackSize.y = Scale(pApp, GUI_MIN_WINDOW_HEIGHT) + pApp->immersiveWindow.metrics.cyCaption;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Accepts the suggested window rectangle and refreshes fonts and layout using
 *  the new vertical DPI.
 */
static FORCE_INLINE void CastWindow_OnDpiChanged(HWND hWnd, UINT dpiX, UINT dpiY, const RECT* pSuggested)
{
    App* pApp;

    UNREFERENCED_PARAMETER(dpiX);
    UNREFERENCED_PARAMETER(pSuggested);

    pApp = GetApp(hWnd);
    pApp->dpi = dpiY;
    ApplyFont(pApp);
    Layout(pApp);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Routes control commands and file-edit notifications to their application
 *  operations; forwards unhandled commands.
 */
static FORCE_INLINE void CastWindow_OnCommand(HWND hWnd, int id, HWND hControl, UINT codeNotify)
{
    App* pApp;

    pApp = GetApp(hWnd);
    switch (id)
    {
    case ID_REFRESH:
        StartDiscovery(pApp);
        return;
    case ID_WINDOWS_CAST:
        OpenWindowsCast(pApp);
        return;
    case ID_BROWSE:
        Browse(pApp);
        return;
    case ID_SUBTITLE_BROWSE:
        BrowseSubtitles(pApp);
        return;
    case ID_SUBTITLE:
        InvalidateRect(pApp->hSubtitle, NULL, FALSE);
        return;
    case ID_CAST:
        StartCast(pApp);
        return;
    case ID_PLAY_PAUSE:
        if (NULL != pApp->pSession)
        {
            CastSession_Post(pApp->pSession, CAST_COMMAND_TOGGLE, 0);
        }
        return;
    case ID_STOP:
        StopCast(pApp);
        return;
    case ID_FILE:
        if (EN_CHANGE == codeNotify)
        {
            UpdateControls(pApp);
            SetTimer(hWnd, SUBTITLE_TIMER, GUI_SUBTITLE_POLL_MS, NULL);
        }
        return;
    case ID_MOTION_CHECK:
        ReadConfigFromSliders(pApp);
        return;
    case ID_PREVIEW:
        StartPreview(pApp);
        return;
    default:
        break;
    }
    FORWARD_WM_COMMAND(hWnd, id, hControl, codeNotify, DefWindowProc);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Supplies slider custom drawing and receiver-list selection/double-click
 *  behavior, then forwards notifications requiring default processing.
 */
static FORCE_INLINE LRESULT CastWindow_OnNotify(HWND hWnd, int idFrom, NMHDR* pHeader)
{
    App* pApp;

    pApp = GetApp(hWnd);
    if (NM_CUSTOMDRAW == pHeader->code)
    {
        NMCUSTOMDRAW* draw   = (NMCUSTOMDRAW*)pHeader;
        int           slider = pHeader->hwndFrom == pApp->hGain || pHeader->hwndFrom == pApp->hSeek ||
                               pHeader->hwndFrom == pApp->hResSlider || pHeader->hwndFrom == pApp->hQpSlider ||
                               pHeader->hwndFrom == pApp->hGopSlider || pHeader->hwndFrom == pApp->hMotionSlider;
        if (slider)
        {
            if (draw->dwDrawStage == CDDS_PREPAINT)
            {
                return CDRF_NOTIFYITEMDRAW;
            }
            if (draw->dwDrawStage == CDDS_ITEMPREPAINT)
            {
                RECT                rect;
                const D2D1_COLOR_F* fill;

                rect.left = 0;
                rect.top = 0;
                rect.right = draw->rc.right - draw->rc.left;
                rect.bottom = draw->rc.bottom - draw->rc.top;
                if (TBCD_THUMB == draw->dwItemSpec)
                {
                    fill = IsWindowEnabled(pHeader->hwndFrom) ? &c_castAccentColor : &c_castMutedColor;
                }
                else
                {
                    fill = &c_castFieldColor;
                }
                BeginControlPaint(pApp, draw->hdc, &draw->rc);
                FillControlRect(pApp, &rect, fill);
                EndControlPaint(pApp);
                return CDRF_SKIPDEFAULT;
            }
        }
    }

    if (pHeader->hwndFrom == pApp->hList)
    {
        if (LVN_ITEMCHANGED == pHeader->code)
        {
            UpdateControls(pApp);
        }
        else if (NM_DBLCLK == pHeader->code && IsWindowEnabled(pApp->hCast))
        {
            StartCast(pApp);
        }
    }
    return FORWARD_WM_NOTIFY(hWnd, idFrom, pHeader, DefWindowProc);
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Updates gain captions and its debounce timer, routes seeking, or copies
 *  transcoder slider values into configuration.
 */
static FORCE_INLINE void CastWindow_OnHScroll(HWND hWnd, HWND hControl, UINT code, int position)
{
    App* pApp;

    UNREFERENCED_PARAMETER(position);

    pApp = GetApp(hWnd);

    if (hControl == pApp->hGain)
    {
        TCHAR      text[GUI_STATE_TEXT_CAPACITY];
        NativeText writer;
        unsigned   percent = (unsigned)SendMessage(pApp->hGain, TBM_GETPOS, 0, 0);
        NativeText_Initialize(&writer, text, ARRAYSIZE(text));
        NativeText_AppendString(&writer, TEXT("Audio gain "));
        NativeText_AppendUnsigned(&writer, percent, 1);
        NativeText_AppendCharacter(&writer, TEXT('%'));
        (void)SetWindowText(pApp->hGainLabel, text);
        if (pApp->pSession || pApp->preparingAudio)
        {
            SetTimer(hWnd, GAIN_TIMER, GUI_GAIN_POLL_MS, NULL);
        }
        else
        {
            (void)SetWindowText(pApp->hGainState, (GUI_ORIGINAL_GAIN_PERCENT == percent) ? TEXT("Original audio") : TEXT("Applies during playback"));
        }
    }
    else if (hControl == pApp->hSeek)
    {
        OnSeekScroll(pApp, code);
    }
    else if (hControl == pApp->hResSlider || hControl == pApp->hQpSlider || hControl == pApp->hGopSlider ||
             hControl == pApp->hMotionSlider)
    {
        ReadConfigFromSliders(pApp);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Publishes a successful preview's readout and starts it only when no Cast
 *  session is active. Closing suppresses preview playback.
 */
static FORCE_INLINE void OnPreviewDone(HWND hWnd, const PreviewResult* pResult)
{
    App* pApp;

    pApp = GetApp(hWnd);

    pApp->previewBusy = 0;
    EnableWindow(pApp->hPreview, !pApp->preparingAudio);
    if (!pApp->closing && NULL != pResult && pResult->ok && pResult->frames > 0)
    {
        TCHAR      text[GUI_GAIN_STATE_CAPACITY];
        NativeText writer;

        NativeText_Initialize(&writer, text, ARRAYSIZE(text));
        NativeText_AppendUnsigned(&writer, pResult->width, 1);
        NativeText_AppendString(&writer, TEXT(" px, QP "));
        NativeText_AppendSigned(&writer, pApp->transcode.qpInter);
        NativeText_AppendString(&writer, TEXT(", ~"));
        NativeText_AppendUnsigned(&writer, pResult->kbps, 1);
        NativeText_AppendString(&writer, TEXT(" kbps, motion "));
        NativeText_AppendSigned(&writer, pResult->motionPercent);
        NativeText_AppendString(&writer, TEXT("% - playing on the receiver"));
        (void)SetWindowText(pApp->hCfgReadout, text);
        /* Show the transcoded clip on the receiver: no proxy, since the clip is the output. */
        if (NULL == pApp->pSession)
        {
            StartCastSource(pApp, pResult->path, ARRAYSIZE(pResult->path), 0);
        }
    }
    else
    {
        (void)SetWindowText(pApp->hCfgReadout, TEXT("Preview failed (need a local AVC MP4 with an H.264 track)."));
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Latches closing, requests worker cancellation and session Stop, and starts
 *  cleanup. The position timer completes deferred window destruction.
 */
static FORCE_INLINE void CastWindow_OnClose(HWND hWnd)
{
    App* app;

    app = GetApp(hWnd);
    if (!app->closing)
    {
        app->closing = TRUE;
        EnableWindow(hWnd, FALSE);
        CancelGain(app);
        (void)InterlockedExchange(&app->previewCancel, TRUE);
        if (NULL != app->pSession)
        {
            CastSession_Post(app->pSession, CAST_COMMAND_STOP, 0);
        }
        (void)OS_Thread_Stop(&app->discoveryWorker);
        (void)OS_Thread_Stop(&app->gainWorker);
        (void)OS_Thread_Stop(&app->sessionWorker);
        (void)OS_Thread_Stop(&app->previewWorker);
        (void)OS_Thread_Stop(&app->presenterWorker);
        FinishSession(app, NULL);
    }
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Deletes the retained theme brushes and posts the thread's quit message.
 */
static FORCE_INLINE void CastWindow_OnDestroy(HWND hWnd)
{
    App* pApp;

    pApp = GetApp(hWnd);

    pApp->windowDestroyed = TRUE;
    PostQuitMessage(0);
}

static FORCE_INLINE BOOL CastWindow_OnNcCreate(HWND window, LPCREATESTRUCT create)
{
    IMMERSIVEWINDOW* immersiveWindow;
    App*             app;

    immersiveWindow = (IMMERSIVEWINDOW*)create->lpCreateParams;
    app = (App*)immersiveWindow->clientData;
    app->hWnd = window;
    return SetProp(window, TEXT("cvkCastApp"), app);
}

static FORCE_INLINE void CastWindow_OnNcDestroy(HWND window)
{
    (void)RemoveProp(window, TEXT("cvkCastApp"));
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Uses the main-window message crackers and forwards unhandled messages to
 *  the ImmersiveWindow's default processing.
 */
static FORCE_INLINE LRESULT CALLBACK WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
        HANDLE_MSG(hWnd, WM_CREATE,          CastWindow_OnCreate);
        HANDLE_MSG(hWnd, WM_DESTROY,         CastWindow_OnDestroy);
        HANDLE_MSG(hWnd, WM_SIZE,            CastWindow_OnSize);
        HANDLE_MSG(hWnd, WM_CLOSE,           CastWindow_OnClose);
        HANDLE_MSG(hWnd, WM_ERASEBKGND,      CastWindow_OnEraseBackground);
        HANDLE_MSG(hWnd, WM_GETMINMAXINFO,   CastWindow_OnGetMinMaxInfo);
        HANDLE_MSG(hWnd, WM_DRAWITEM,        CastWindow_OnDrawItem);
        HANDLE_MSG(hWnd, WM_MEASUREITEM,     CastWindow_OnMeasureItem);
        HANDLE_MSG(hWnd, WM_NOTIFY,          CastWindow_OnNotify);
        HANDLE_MSG(hWnd, WM_NCCREATE,        CastWindow_OnNcCreate);
        HANDLE_MSG(hWnd, WM_NCDESTROY,       CastWindow_OnNcDestroy);
        HANDLE_MSG(hWnd, WM_COMMAND,         CastWindow_OnCommand);
        HANDLE_MSG(hWnd, WM_TIMER,           CastWindow_OnTimer);
        HANDLE_MSG(hWnd, WM_HSCROLL,         CastWindow_OnHScroll);
        HANDLE_MSG(hWnd, WM_CTLCOLOREDIT,    CastWindow_OnCtlColorEdit);
        HANDLE_MSG(hWnd, WM_CTLCOLORLISTBOX, CastWindow_OnCtlColorListBox);
        HANDLE_MSG(hWnd, WM_CTLCOLORBTN,     CastWindow_OnCtlColorButton);
        HANDLE_MSG(hWnd, WM_CTLCOLORSTATIC,  CastWindow_OnCtlColorStatic);
        HANDLE_MSG(hWnd, WM_DPICHANGED,      CastWindow_OnDpiChanged); // NOLINT(performance-no-int-to-ptr): LPARAM carries a RECT pointer.
    default:
        break;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

static FORCE_INLINE void PresentCastWindow(void* parameter)
{
    App*                      app;
    IDXGIFactory6*            factory;
    IDXGIAdapter1*            adapter;
    IMMERSIVEPAINTSTRUCT      paint;
    ID2D1DeviceContext*       context;
    HRESULT                   comResult;
    bool_t                    stopping;
    static const D2D1_COLOR_F background = {24.0F / 255.0F, 26.0F / 255.0F, 31.0F / 255.0F, 1.0F};

    app = (App*)parameter;
    comResult = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    LOGASSERT(SUCCEEDED(comResult));
    cdxgiSetupDefaultFactory(&factory);
    cdxgiFactoryGetMinimumPowerAdapter(factory, &adapter);
    InitSwapChain(&app->swapChain, (IDXGIAdapter*)adapter);
    cdxgiUnknownReleaseSafe((IUnknown*)adapter);
    cdxgiUnknownReleaseSafe((IUnknown*)factory);
    InitCaption(&app->caption, &app->immersiveWindow, app->swapChain.context);
    app->immersiveWindow.caption = &app->caption;
    cdcompSetupDevice((IDXGIDevice*)app->swapChain.dxgiDevice, app->hWnd, FALSE, &app->swapChain.compositionDevice, &app->swapChain.compositionTarget, &app->swapChain.visual);
    cdcompVisualSetContent(app->swapChain.visual, (IUnknown*)app->swapChain.swapChain);
    app->immersiveWindow.swapChain = &app->swapChain;

    stopping = OS_Thread_IsStopping(&app->presenterWorker);
    for (; !stopping;)
    {
        context = BeginImmersivePaint(&app->immersiveWindow, &paint);
        cd2dRenderTargetClear((ID2D1RenderTarget*)context, &background);
        EndImmersivePaint(&app->immersiveWindow, &paint);
        Sleep(USER_TIMER_MINIMUM);
        stopping = OS_Thread_IsStopping(&app->presenterWorker);
    }

    app->immersiveWindow.caption = NULL;
    app->immersiveWindow.swapChain = NULL;
    DestroyCaption(&app->caption);
    DestroySwapChain(&app->swapChain);
    CoUninitialize();
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Initializes DPI, OLE, Winsock and common controls, creates the application
 *  window, then runs dialog-key translation and the window message loop.
 */
static int RunMessageLoop(App* app, HWND window)
{
    MSG     message;
    BOOL    message_result;
    bool_t  pump_failed;
    bool_t  has_message;
    bool_t  is_quit;
    int32_t result;

    pump_failed = FALSE;
    result      = 1;
    for (;;)
    {
        if (app->windowDestroyed)
        {
            break;
        }
        if (pump_failed)
        {
            has_message = !!PeekMessage(&message, NULL, 0, 0, PM_REMOVE);
            if (!has_message)
            {
                CastWindow_OnTimer(window, POSITION_TIMER);
                (void)MsgWaitForMultipleObjectsEx(0, NULL, POSITION_TICK_MS, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
            }
        }
        else
        {
            message_result = GetMessage(&message, NULL, 0, 0);
            has_message    = (0 < message_result);
            is_quit        = (0 == message_result);
            if (!has_message)
            {
                if (is_quit)
                {
                    result = (int32_t)message.wParam;
                }
                pump_failed = TRUE;
                CastWindow_OnClose(window);
            }
        }
        if (has_message)
        {
            if (!IsDialogMessage(window, &message))
            {
                TranslateMessage(&message);
                DispatchMessage(&message);
            }
        }
    }
    if (!pump_failed)
    {
        result = 0;
    }
    return result;
}

static FORCE_INLINE int RunWindow(App* app, HINSTANCE instance, int showCommand)
{
    HRESULT                   ole_result;
    bool_t                    ole_ready;
    WSADATA                   wsa_data;
    int32_t                   network_result;
    bool_t                    network_ready;
    int32_t                   result;
    INITCOMMONCONTROLSEX      controls;
    IMMERSIVEWINDOWCREATEINFO createInfo;
    bool_t                    controls_ready;
    HWND                      window;
    bool_t                    window_ready;
    uint32_t                  argumentCount;
    WCHAR                     argument[MAX_PATH];
    bool_t                    hasArgument;
    bool_t                    unregistered;

    LOGASSERT(NULL != app);

    (void)SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    ole_result    = OleInitialize(NULL);
    ole_ready     = SUCCEEDED(ole_result);
    network_ready = FALSE;
    controls_ready = FALSE;
    result        = 1;
    if (ole_ready)
    {
        network_result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
        network_ready  = (0 == network_result);
    }
    if (network_ready)
    {
        controls.dwSize = sizeof(controls);
        controls.dwICC  = ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
        controls_ready  = !!InitCommonControlsEx(&controls);
    }
    if (controls_ready)
    {
        app->hInstance = instance;
        app->dpi       = GetDpiForSystem();
        createInfo.clientProc = WindowProc;
        createInfo.clientData = app;
        createInfo.title = L"CastDemo";
        createInfo.width = Scale(app, GUI_INITIAL_WINDOW_WIDTH);
        createInfo.height = Scale(app, GUI_INITIAL_WINDOW_HEIGHT);
        createInfo.showCommand = showCommand;
        CreateImmersiveWindow(&app->immersiveWindow, &createInfo);
        window = app->immersiveWindow.hwnd;
        window_ready = (NULL != window);
        if (window_ready)
        {
            if (!app->closing && !OS_Thread_Submit(&app->presenterWorker))
            {
                CastWindow_OnClose(window);
            }
            argumentCount = 0;
            hasArgument   = OS_Process_ReadArgument(&argumentCount, (uint16_t*)argument, MAX_PATH - 1, 1);
            if (hasArgument)
            {
                argument[argumentCount] = L'\0';
                (void)WriteControlText(app->hFile, argument);
                UpdateControls(app);
            }
            result = RunMessageLoop(app, window);
        }
        if (NULL != app->hFont)
        {
            (void)DeleteObject(app->hFont);
        }
        (void)DeleteObject(app->backgroundBrush);
        (void)DeleteObject(app->fieldBrush);
        DestroyControlPaint(app);
        unregistered = !!UnregisterClass(WC_IMMERSIVEWINDOW, instance);
        LOGASSERT(unregistered);
    }
    if (network_ready)
    {
        (void)WSACleanup();
    }
    if (ole_ready)
    {
        OleUninitialize();
    }
    return result;
}

/*************************************************************************************************/
/**
 * \par Details:
 *  Retains the context outside the window procedure until all workers and the
 *  native window have stopped using it. Native memory-release failure is retried.
 */
uint32_t OS_Application_Main(void)
{
    OS_Memory_t memory;
    bool_t      ready;
    int32_t     result;
    App*        app;
    bool_t      closed;
    HINSTANCE   instance;
    STARTUPINFO startup;
    int32_t     showCommand;

    instance    = (HINSTANCE)GetModuleHandle(NULL);
    showCommand = SW_SHOWDEFAULT;
    ZeroMemory(&startup, sizeof(startup));
    startup.cb = sizeof(startup);
    GetStartupInfo(&startup);
    if (0 != (startup.dwFlags & STARTF_USESHOWWINDOW))
    {
        showCommand = startup.wShowWindow;
    }

    ready  = OS_Memory_Initialize(&memory, sizeof(App));
    result = 1;
    if (ready)
    {
        app    = (App*)OS_Memory_GetAddress(&memory);
        GpuDevice_Initialize(&app->previewJob.gpuDevice);
        result = RunWindow(app, instance, showCommand);
    }
    for (;;)
    {
        closed = OS_Memory_TryClose(&memory);
        if (closed)
        {
            break;
        }
        Sleep(POSITION_TICK_MS);
    }
    return (uint32_t)result;
}
