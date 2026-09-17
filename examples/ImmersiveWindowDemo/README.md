# ImmersiveWindowDemo

The CDX ImmersiveWindowDemo with its two video rendering passes ported to CVK:
NV12 to BGRA, then the original scaling shader and sampler settings. Both passes
run in Vulkan. Media Foundation still decodes the video, and the original Windows
caption uses Direct2D/DirectWrite over DirectComposition. CDX supplies those Windows
bindings; this is a Vulkan video renderer with native Windows integration.

The command line, 1280x720 starting window, caption glyphs, light/dark toggle,
resize/move handling, Space to pause/resume, Escape to close, and looping playback
come from the pinned CDX example. As in the reference, playback is video-only.
`demo.c` and the public headers are reused directly from the submodule. The local
`immersive_window.c` changes presentation to submit each painted buffer once,
with vertical synchronization, avoiding the reference's double-present path.

Decoded NV12 textures and the BGRA output stay on the GPU. The renderer selects
the Vulkan device with the same adapter LUID as D3D11 and synchronizes shared
textures with keyed mutexes and Vulkan external queue ownership barriers. The
output is allocated at the composition back buffer's desktop size, so dragging a
window edge does not reallocate it. Conversion is cached per frame; scaling is
also cached when neither the frame nor the client size changes.

## Build and run

Requires Windows, a Windows SDK/C compiler, Python 3, and the Vulkan SDK's `glslc`
on PATH (or under `VULKAN_SDK`). Runtime requires Vulkan 1.1, 16x anisotropic
filtering, multi-planar NV12 support, `VK_KHR_external_memory_win32`, and
`VK_KHR_win32_keyed_mutex` on the window's adapter. Shared NV12 and BGRA import
support is checked at startup. Shader binaries are embedded in the executable.

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
at enlarged, reduced, and odd dimensions. A short fixture exercises EOF looping.

```powershell
ffmpeg -f lavfi -i testsrc2=size=640x360:rate=30 -t 2 -c:v libx264 -pix_fmt yuv420p build/testsrc.mp4
cmake -S . -B build/windows-msvc-2026 -DCVK_DEMO_VALIDATION=ON -DCVK_DEMO_TEST_VIDEO=C:/dev/cvk/build/testsrc.mp4
cmake --build build/windows-msvc-2026 --config Debug --target cvk-immersive-video-test
ctest --test-dir build/windows-msvc-2026 -C Debug -R cvk-immersive-video-pixels --output-on-failure
```

Set `IMMERSIVE_TRACE` to a log filename to record geometry draw and presentation
timings. The pixel test needs a real compatible GPU; it is disabled unless
`CVK_DEMO_TEST_VIDEO` is set and is separate from the GPU-independent CVK tests.
