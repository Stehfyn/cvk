# Build-selected MSVC x64 atomic instructions; no target selection in shared C.
add_library(cast-os-atomic-native OBJECT os_atomic.asm)
target_compile_options(cast-os-atomic-native PRIVATE /WX)
