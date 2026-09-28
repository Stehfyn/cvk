# Hand-written runtime support. Native ABI sources are selected here, not in C.
if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    enable_language(ASM_MASM)
    add_subdirectory(msvc/x64)
    set(crt_native_sources $<TARGET_OBJECTS:crt-native> msvc/crt_float.c)
else()
    message(FATAL_ERROR "The hand-written CRT needs a verified memory backend for this target")
endif()

add_library(crt STATIC crt_memory.c crt_memory.h crt_memory_backend.h ${crt_native_sources})
target_include_directories(crt PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_compile_options(crt PRIVATE "$<$<COMPILE_LANGUAGE:C>:/GS-;/Zl>")
target_link_libraries(crt-abi INTERFACE crt)

# Resolve compiler-generated references before LTCG while retaining whole-program optimization.
# https://learn.microsoft.com/en-us/cpp/error-messages/tool-errors/linker-tools-error-lnk1237
target_link_options(crt INTERFACE /INCLUDE:_fltused)
