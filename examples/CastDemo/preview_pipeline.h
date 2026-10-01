#ifndef CAST_PREVIEW_PIPELINE_H
#define CAST_PREVIEW_PIPELINE_H

#include <Windows.h>
#include "gpu_transcode.h"
#include "os/os_thread.h"

/* The worker owns Media Foundation. The presenter borrows only completed BGRA frames. */
typedef struct PreviewPipeline
{
    OS_Thread_t        worker;
    SRWLOCK            requestLock;
    SRWLOCK            frameLock;
    WCHAR              path[MAX_PATH];
    GpuTranscodeConfig config;
    unsigned           requestGeneration;
    BYTE*              pixels;
    UINT32             width;
    UINT32             height;
    UINT32             pitch;
    unsigned           frameGeneration;
    volatile LONG      failed;
    bool_t             initialized;
} PreviewPipeline;

bool_t PreviewPipeline_Initialize(PreviewPipeline* preview);
bool_t PreviewPipeline_Start(PreviewPipeline* preview, const WCHAR* path, const GpuTranscodeConfig* config);
void PreviewPipeline_SetConfig(PreviewPipeline* preview, const GpuTranscodeConfig* config);
bool_t PreviewPipeline_TryClose(PreviewPipeline* preview);

#endif
