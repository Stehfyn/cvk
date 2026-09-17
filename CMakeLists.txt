cmake_minimum_required(VERSION 3.25)
project(cvk LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Pinned Vulkan headers and registry (git submodules).
add_subdirectory(external/Vulkan-Headers EXCLUDE_FROM_ALL)
# Treat third-party headers as system headers while checking cvk itself as C99.
set_target_properties(Vulkan-Headers PROPERTIES SYSTEM TRUE)

# Volk and cvk use the same pinned headers and platform entry points.
set(VOLK_PULL_IN_VULKAN OFF)
if(WIN32)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_WIN32_KHR)
elseif(UNIX)
    set(VOLK_STATIC_DEFINES VK_USE_PLATFORM_XLIB_KHR)
endif()
add_subdirectory(external/volk EXCLUDE_FROM_ALL)
set_target_properties(volk PROPERTIES SYSTEM TRUE C_EXTENSIONS ON)
target_link_libraries(volk PUBLIC Vulkan::Headers)
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)

find_package(Python3 COMPONENTS Interpreter)

# Apply IPO to our libraries and executables in Release, including the final link.
# Set this after adding third-party projects so their build settings are untouched.
include(CheckIPOSupported)
check_ipo_supported(LANGUAGES C)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)

# Give the compositional wrappers MSVC's more aggressive Release inlining budget.
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    # Remove the default /Ob2 to avoid an override warning on every compilation.
    string(REGEX REPLACE "[/-]Ob[0-3]" "" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
endif()
add_compile_options($<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,MSVC>>:/Ob3>)
# Prefer full LTO for this generated, monolithic source: it can discard unreachable
# wrapper families before optimizing them, and exposes every live tier for inlining.
# Override CMake's Clang ThinLTO default at both compilation and the final link.
add_compile_options($<$<AND:$<CONFIG:Release>,$<COMPILE_LANG_AND_ID:C,Clang,AppleClang>>:-flto=full>)
add_link_options($<$<AND:$<CONFIG:Release>,$<LINK_LANG_AND_ID:C,Clang,AppleClang>>:-flto=full>)

# Regenerate the core, JSON, and platform pairs from the registries and policies.
# Generated files are committed; run this target after changing the policy or generator.
if(Python3_Interpreter_FOUND)
    add_custom_target(cvk-generate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate.py
                --policy ${CMAKE_CURRENT_SOURCE_DIR}/cvk.policy.json
                --out-dir ${CMAKE_CURRENT_SOURCE_DIR}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate.py
                --policy ${CMAKE_CURRENT_SOURCE_DIR}/scvk.policy.json
                --out-dir ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating cvk from the Vulkan registry"
        VERBATIM)

    # Rule zero: the generated files must equal a fresh generation.
    enable_testing()
    add_test(NAME cvk-generated-files-match
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_generated.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Everything under include/ and src/ is generated. Core is platform-free; each platform has
# its own generated pair that includes cvk.h. Tier 3 protocols (scripts/protocols/) are folded
# into the platform pairs by the generator; "posix" is the pair for descriptor protocols.
set(CVK_SOURCES src/cvk.c src/cvk_json.c)
if(WIN32)
    list(APPEND CVK_SOURCES src/win32/cvk_win32.c src/win32/cvk_win32_json.c)
elseif(UNIX)
    list(APPEND CVK_SOURCES src/xlib/cvk_xlib.c src/xlib/cvk_xlib_json.c src/posix/cvk_posix.c)
endif()

add_library(cvk STATIC ${CVK_SOURCES})
target_include_directories(cvk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
# Only the header-only format utilities are needed; keep their C++ libraries out of the build.
target_include_directories(cvk SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan-Utility-Libraries/include)
target_link_libraries(cvk PUBLIC Vulkan::Headers PRIVATE volk::volk)
target_compile_definitions(cvk PUBLIC VK_NO_PROTOTYPES)

if(MSVC)
    target_compile_options(cvk PRIVATE /W4)
else()
    target_compile_options(cvk PRIVATE -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration)
endif()

# JSON parsing and mock Volk dispatch need no GPU or installed Vulkan loader.
enable_testing()
add_executable(cvk-pipeline-json-test tests/pipeline_json_test.c)
target_link_libraries(cvk-pipeline-json-test PRIVATE cvk)
add_test(NAME cvk-pipeline-json-parse COMMAND cvk-pipeline-json-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/cube.pc.json)

add_executable(cvk-volk-dispatch-test tests/volk_dispatch_test.c)
target_link_libraries(cvk-volk-dispatch-test PRIVATE cvk volk::volk)
add_test(NAME cvk-volk-dispatch COMMAND cvk-volk-dispatch-test)

add_executable(cvk-json-integer-test tests/json_integer_test.c)
target_link_libraries(cvk-json-integer-test PRIVATE cvk)
add_test(NAME cvk-json-integers COMMAND cvk-json-integer-test)

add_executable(cvk-format-test tests/format_test.c)
target_link_libraries(cvk-format-test PRIVATE cvk)
add_test(NAME cvk-format-layouts COMMAND cvk-format-test)

# Vulkan SC profile: the same generator over the SC registry (external/VulkanSC-Headers),
# prefix scvk, a standalone library against the SC headers. Format protocols are Vulkan-only.
set(SCVK_SOURCES src/scvk.c src/scvk_json.c)
if(WIN32)
    list(APPEND SCVK_SOURCES src/win32/scvk_win32.c)
elseif(UNIX)
    list(APPEND SCVK_SOURCES src/posix/scvk_posix.c)
endif()
add_library(scvk STATIC ${SCVK_SOURCES})
target_include_directories(scvk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(scvk SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanSC-Headers/include)
if(MSVC)
    target_compile_options(scvk PRIVATE /W4)
else()
    target_compile_options(scvk PRIVATE -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration)
endif()
