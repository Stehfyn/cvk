# ImmersiveWindowDemo

Media Foundation decodes video using a multithread-protected D3D11 device.
CVK converts NV12 to BGRA on the Vulkan device with the same adapter LUID.
D2D scales the cached native-resolution BGRA texture and draws the caption
directly into one `CreateSwapChainForComposition` swap chain. DirectComposition
hosts that swap chain in the borderless window.

Each present contains a complete video and caption image. The two
flip-sequential buffers are client-sized, fully repainted, and paced by the
frame-latency waitable object with a maximum latency of one. The D2D target is
unbound and released before `ResizeBuffers`, then recreated from buffer zero.
`WM_NCCALCSIZE` renders the proposed client dimensions before the HWND moves;
`WM_WINDOWPOSCHANGED` checks the committed dimensions. The visual tree stays
fixed during resizing; there is no independently committed video transform or
caption position. No desktop-sized render target, whole-desktop copy,
`DwmFlush`, or GPU query polling is used.

Resize presents discard queued frames with outdated dimensions using
`DXGI_PRESENT_RESTART`. On leaving the move/size loop, the window redraws its
final client rectangle and current caption hover state, then waits for the
frame-latency signal before normal playback resumes. That signal is retained
for the next render, avoiding a second wait for the same available frame slot.

NV12 conversion and the native-size GPU copy run only when a new decoded frame
is adopted. Resizing and caption changes reuse that cached BGRA texture. Video
and caption are drawn together, including clearing previous caption pixels.
Hover, leave, theme, maximize and minimize state changes request a complete
frame; input-triggered paints are dispatched immediately with `UpdateWindow`.
The media timer advances playback inside native move/size/menu loops. DWM
composes at display refresh independently of the media timer. Paused and
minimized playback does not repeatedly convert or copy the same video frame.

The client occupies the entire window. Native nonclient rendering and native
window visual styles are disabled. Native move/resize capture stays with
`DefWindowProc`; caption and edge hit tests use screen coordinates directly.
Space pauses/resumes, Escape closes, playback loops, and the starting window
is 1280x720. Playback is video-only. The existing Efficiency mode CPU policy
(EcoQoS and idle priority) is retained.

D3D11 copies decoded NV12 planes to shared R8 and RG8 textures with unfiltered
shader loads. Vulkan imports those single-plane textures and converts directly
into shared BGRA. Keyed mutexes and external queue ownership barriers protect
the D3D11/Vulkan handoff. This avoids importing the native NVIDIA driver's
multi-planar NV12 layout. A system-memory decode fallback remains for formats
the GPU decoder cannot handle; validation builds report which path is active.

The original scaled `VideoStream_Draw` API remains available to the reference
comparison test. The window uses `VideoStream_CopyFrame`, which does one native
conversion pass. It does not run the reference scaling shader.

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

The test also compares native conversion copies at a nonzero destination offset
and verifies that pixels outside that destination remain unchanged.

Set `IMMERSIVE_TRACE` to a log filename to record composition presentation timings. The pixel test needs a real compatible GPU; it is disabled unless
`CVK_DEMO_TEST_VIDEO` is set and is separate from the GPU-independent CVK tests.

API contracts used by the window:

- [Composition swap-chain creation](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition)
- [Back-buffer reference lifetime during resize](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers)
- [Frame-latency waitable object](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject)
- [Discarding queued presentations after resize](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-present)
- [DirectComposition architecture](https://learn.microsoft.com/en-us/windows/win32/directcomp/architecture-and-components)
- [DXVA through the Source Reader device manager](https://learn.microsoft.com/en-us/windows/win32/medfound/mf-source-reader-d3d-manager)
