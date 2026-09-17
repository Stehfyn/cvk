# ImmersiveWindowDemo

The CDX ImmersiveWindowDemo with its two video rendering passes ported to CVK:
NV12 to BGRA, then the original scaling shader and sampler settings. Both passes
run in Vulkan. Media Foundation still decodes the video, and the original Windows
caption uses Direct2D/DirectWrite over DirectComposition. CDX supplies those Windows
bindings; this is a Vulkan video renderer with native Windows integration.

The command line, 1280x720 starting window, caption glyphs, light/dark toggle,
resize/move handling, Space to pause/resume, Escape to close, and looping playback
come from the pinned CDX example. As in the reference, playback is video-only.
On startup the demo enables Windows Efficiency mode (EcoQoS and idle process
priority), which Task Manager identifies with its green leaf on Windows 11.
Windows can favor lower CPU frequencies and other processes under contention.
The original public headers are reused directly from the submodule. Local copies
of `demo.c` and `immersive_window.c` keep the controls and playback flow. Video
and caption render into one offscreen frame. During `WM_NCCALCSIZE`, both use the
proposed client dimensions, and the GPU completes this frame and its copy into
the swap-chain back buffer while the window still has its old geometry. The
DXGI frame-latency object paces each resize frame; there is no second `DwmFlush`
wait after rendering. The completed replacement is presented before returning
from `WM_NCCALCSIZE`. No GPU work or presentation is deferred
until after the HWND changes origin. `WM_WINDOWPOSCHANGED` checks the complete
client rectangle in screen coordinates, including its origin, against the
submitted resize frame and repaints only if the committed geometry differs.
The resize present discards queued frames with outdated dimensions.

The composition chain has two flip-sequential buffers and a one-frame latency
limit. Presentation runs at the display refresh rate, including paused and
repeated video frames; decoding advances at the video's own frame rate. The
swap-chain wait and vsync also pace the modal move, resize, and menu loops.
Those loops invalidate after painting so Windows generates low-priority
`WM_PAINT` messages after pending input, without a posted-message loop or a
10 ms timer limiting high-refresh displays. A modal paint waiting for a frame
slot yields when input arrives. Resize frames also run the video pre-draw
callback, so playback advances even while resize input takes priority over paint.
The client occupies the entire window, with native nonclient rendering disabled
using the nonclient rendering policy and native visual styles disabled on the
HWND. The light/dark toggle changes the custom caption and system-menu colors;
it does not apply an Explorer window theme. Caption and edge hit tests use
screen coordinates without offsets; native move/resize commands use the
message's hit-test result rather than cached hover state.
`WM_PAINT` redraws video and caption; background erasure does not discard repaint
requests. `presentation.h` exposes the shared frame target and drawing dimensions
to the local demo without changing the upstream API.

Decoded NV12 textures and the BGRA output stay on the GPU. D3D11 copies the two
NV12 planes into shared R8 and RG8 textures with unfiltered shader loads; Vulkan
performs color conversion and scaling. This avoids the native NVIDIA driver's
corrupted chroma and device loss when importing a shared multi-planar NV12 image.
The renderer selects
the Vulkan device with the same adapter LUID as D3D11 and synchronizes shared
textures with keyed mutexes and Vulkan external queue ownership barriers. The
output is allocated at the composition back buffer's desktop size, so dragging a
window edge does not reallocate it. Conversion is cached per frame; scaling is
also cached when neither the frame nor the client size changes.

Every presented backing buffer is fully painted. The shader keeps the original
client-area sampling coordinates and extends edge pixels into the area outside
the current client rectangle. This prevents a growing window from exposing
uninitialized or transparent pixels before its new geometry frame arrives.

## Build and run

Requires Windows, a Windows SDK/C compiler, Python 3, and the Vulkan SDK's `glslc`
on PATH (or under `VULKAN_SDK`). Runtime requires Vulkan 1.1, 16x anisotropic
filtering, `VK_KHR_external_memory_win32`, and `VK_KHR_win32_keyed_mutex` on the
window's adapter. Shared R8, RG8, and BGRA import support is checked at startup.
Vulkan shader binaries are embedded in the executable; the D3D11 plane-copy
shaders are compiled at startup with the Windows D3D compiler.

```powershell
git submodule update --init --recursive
cmake -S . -B build/windows-msvc-2026 -G "Visual Studio 18 2026" -A x64 -DCVK_BUILD_EXAMPLES=ON
cmake --build build/windows-msvc-2026 --config Release --target ImmersiveWindowDemo
& .\build\windows-msvc-2026\examples\ImmersiveWindowDemo\Release\ImmersiveWindowDemo.exe "C:\path\to\video.mp4"
```

Open `build/windows-msvc-2026/cvk.slnx` in Visual Studio. `ImmersiveWindowDemo`
is the startup project; reload the solution after regenerating it. Without a file
argument the app opens `demo.mp4` beside the executable, as the CDX demo does.

## Validation

`CVK_DEMO_VALIDATION=ON` enables the Vulkan validation layer and sends messages to
stderr and the debugger. The optional GPU test renders 65 matching decoded frames
through both implementations, comparing every BGRA channel within a tolerance of
three values for independent API filtering/rounding. It also redraws cached frames
at enlarged, reduced, and odd dimensions. The target is larger than the client
area and cleared to transparent magenta first; the test checks that every pixel
of the backing buffer becomes opaque, including the resize margins. A short
fixture exercises EOF looping.

```powershell
ffmpeg -f lavfi -i testsrc2=size=640x360:rate=30 -t 2 -c:v libx264 -pix_fmt yuv420p build/testsrc.mp4
cmake -S . -B build/windows-msvc-2026 -DCVK_DEMO_VALIDATION=ON -DCVK_DEMO_TEST_VIDEO=C:/dev/cvk/build/testsrc.mp4
cmake --build build/windows-msvc-2026 --config Debug --target cvk-immersive-video-test
ctest --test-dir build/windows-msvc-2026 -C Debug -R cvk-immersive-video-pixels --output-on-failure
```

Set `IMMERSIVE_TRACE` to a log filename to record geometry draw and presentation
timings. The pixel test needs a real compatible GPU; it is disabled unless
`CVK_DEMO_TEST_VIDEO` is set and is separate from the GPU-independent CVK tests.
