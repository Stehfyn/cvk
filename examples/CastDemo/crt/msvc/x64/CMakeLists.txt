# Keep native assembly object paths local to the backend for MSBuild/MASM.
add_library(crt-native OBJECT crt_memory.asm crt_float.asm)
add_library(crt-abi STATIC crt_compiler.asm)
target_compile_options(crt-native PRIVATE /WX)
target_compile_options(crt-abi PRIVATE /WX)
