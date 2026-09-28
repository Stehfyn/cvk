# Python bindings

CVK's procedural functions, with Python arguments and return values:

```python
device, queue = cvk.setup_compute_device_queue(physical_device)

buffer, memory = cvk.setup_host_visible_buffer(
    physical_device,
    device,
    size=4096,
    usage=cvk.VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
)
```

These call `cvkSetupComputeDeviceQueue` and `cvkSetupHostVisibleBuffer` directly.
The generator uses CVK's existing parameter metadata to return output handles,
infer array counts, convert Python strings/lists, and default extension chains,
allocators, flags, and exclusive sharing. Required inputs remain required.
Names use Python's `snake_case`. Vulkan constants keep their `VK_` names.

## Run

From a recursive checkout with a 64-bit Python 3.10+ and a C compiler:

```sh
python -m pip install .
python examples/python/fill_buffer.py
```

The example creates a device, fills a buffer on the GPU, reads `[42, 42, 42, 42]`,
and explicitly destroys its resources. It needs a Vulkan driver. No shader compiler.

The installed package has no Python dependencies. Build dependencies are installed
by pip. Windows source builds need an MSVC/LLVM developer environment.

## Binding rules

- Output pointers become return values. Multiple outputs become a tuple.
- Fixed-capacity enumerations return lists with the actual number of entries.
- Array arguments accept Python lists; associated counts are inferred.
- Builders return Vulkan ctypes structures and retain the inputs they reference.
- Functions with a native non-void return retain that value first in the result.
- Variable byte buffers and outputs without a known size remain explicit. Their
  signatures show the buffer arguments; use ctypes storage just as with the C API.
- Ownership and Vulkan synchronization remain explicit, as in CVK. There is no
  separate Python device, buffer, or pipeline object API.

`cvk.raw` also exposes every C function under its original name and signature.
All 5,195 core + JSON functions are bound. CMake can additionally generate the
platform headers selected by `CVK_PLATFORMS`; Vulkan SC is not included.

`cvk.initialize()`, `cvk.load_instance(instance)`, and `cvk.load_device(device)`
provide the Volk initialization required by CVK. The first returns a `VkResult`.
CVK retains its native fatal error policy. Python conversion errors raise exceptions.

## Development build

```sh
python -m pip install libclang
cmake -S . -B build/python-native -DCVK_BUILD_PYTHON=ON -DCVK_BUILD_EXAMPLES=OFF -DCVK_PLATFORMS=
cmake --build build/python-native --target cvk-python --config Release
```

In PowerShell:

```powershell
$env:PYTHONPATH = (Resolve-Path build/python-native/python).Path
python examples/python/fill_buffer.py
python tests/test_python_bindings.py
```

Set `CVK_TEST_GPU=1` to include the GPU example in the tests.
Generation uses libclang and CVK's existing generator metadata. Native layouts
are checked at import, and generated C adapters handle structs/unions passed by value.
