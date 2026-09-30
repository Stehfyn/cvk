# ImmersiveWindowDemo

The local `opus3/` window, caption, and swap-chain sources come from
`C:\dev\opus\projects\opus3`. Its independent vblank-paced presenter runs
in the adapted `opus3_main.c`. The window thread handles interactive resizing
while the presenter continues decoding,
drawing, and committing video frames. The swap chain keeps its primary-monitor
size; resizing changes only the visible window area.

The CVK video adapter uses CDX to select the minimum-power DXGI adapter for
the opus3 D3D11 device. Media Foundation decodes on that device, and CVK selects
the Vulkan physical device with the same adapter LUID. CVK converts each new
NV12 frame to a native-resolution shared BGRA texture. Direct2D scales that
texture within the current client area and draws it with the opus3 caption.

## Build and run

Requires Windows, a Windows SDK/C compiler, Python 3, and the Vulkan SDK's
`glslc` on PATH (or under `VULKAN_SDK`). Runtime requires Vulkan 1.1,
`VK_KHR_external_memory_win32`, and `VK_KHR_win32_keyed_mutex` on the selected
adapter. Shared R8, RG8, and BGRA import support is checked at startup.

```powershell
git submodule update --init --recursive
cmake -S . -B build/windows-msvc-2026 -G "Visual Studio 18 2026" -A x64 -DCVK_BUILD_EXAMPLES=ON
cmake --build build/windows-msvc-2026 --config Release --target ImmersiveWindowDemo
& .\build\windows-msvc-2026\examples\ImmersiveWindowDemo\Release\ImmersiveWindowDemo.exe "C:\path\to\video.mp4"
```

Without a file argument, the app opens `demo.mp4` beside the executable.
`CVK_DEMO_VALIDATION=ON` enables Vulkan validation messages in the debugger.

The optional `CVK_DEMO_TEST_VIDEO` setting builds a separate GPU pixel
comparison against the pinned CDX reference video path. It does not exercise
the opus3 window or interactive resize path.
