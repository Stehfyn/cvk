#pragma once

#include <windows.h>

/* DPI scaling, in one place. The caption's metrics are authored in DIPs (1/96 inch) and scaled to the
   window's DPI here; every other metric is asked of the system at that DPI (GetSystemMetricsForDpi,
   SystemParametersInfoForDpi) rather than scaled. */

/* DIPs to whole pixels, rounded to nearest: window layout. */
static FORCEINLINE LONG DipsToPixels(LONG dips, UINT dpi)
{
    return MulDiv(dips, (int)dpi, USER_DEFAULT_SCREEN_DPI);
}

/* The DPI scale rounded down and up to whole numbers: what DWM sizes the atlas's edge columns and
   the cluster's outer columns by. */
static FORCEINLINE LONG DpiScaleFloor(UINT dpi)
{
    return (LONG)(dpi / USER_DEFAULT_SCREEN_DPI);
}

static FORCEINLINE LONG DpiScaleCeil(UINT dpi)
{
    return (LONG)((dpi + USER_DEFAULT_SCREEN_DPI - 1u) / USER_DEFAULT_SCREEN_DPI);
}

/* The exact DIP-to-pixel factor: Direct2D drawing, which is done in pixels on a 96-DPI context. */
static FORCEINLINE FLOAT DpiScale(UINT dpi)
{
    return (FLOAT)dpi / (FLOAT)USER_DEFAULT_SCREEN_DPI;
}
