cmake_minimum_required(VERSION 3.25)
project(cvk LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Pinned Vulkan headers and registry (git submodules).
add_subdirectory(external/Vulkan-Headers EXCLUDE_FROM_ALL)
# Treat third-party headers as system headers while checking cvk itself as C99.
set_target_properties(Vulkan-Headers PROPERTIES SYSTEM TRUE)

# Generate every platform, but compile only those selected for the target SDK.
set(cvk_default_platforms "")
if(WIN32)
    set(cvk_default_platforms win32)
elseif(ANDROID)
    set(cvk_default_platforms android posix)
elseif(APPLE)
    set(cvk_default_platforms metal posix)
elseif(CMAKE_SYSTEM_NAME STREQUAL "QNX")
    set(cvk_default_platforms screen posix)
elseif(UNIX)
    set(cvk_default_platforms xlib posix)
endif()


set(CVK_PLATFORMS "${cvk_default_platforms}" CACHE STRING "Generated platform pairs to compile (semicolon-separated; empty for core only)")
file(STRINGS external/Vulkan-Headers/registry/vk.xml cvk_platform_registry REGEX "<platform name=")
foreach(platform IN LISTS CVK_PLATFORMS)
    set(platform_define "")
    if(NOT platform STREQUAL "posix")
        foreach(entry IN LISTS cvk_platform_registry)
            if(entry MATCHES "name=\"${platform}\" protect=\"([^\"]+)\"")
                set(platform_define "${CMAKE_MATCH_1}")
            endif()
        endforeach()
        if(NOT platform_define)
            message(FATAL_ERROR "Unknown CVK_PLATFORMS entry: ${platform}")
        endif()
    endif()
    foreach(profile cvk scvk)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/${platform}/${profile}_${platform}.c")
            list(APPEND ${profile}_platform_sources "src/${platform}/${profile}_${platform}.c")
            if(platform_define)
                list(APPEND ${profile}_platform_defines "${platform_define}")
            endif()
            if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/src/${platform}/${profile}_${platform}_json.c")
                list(APPEND ${profile}_platform_sources "src/${platform}/${profile}_${platform}_json.c")
            endif()
        endif()
    endforeach()
endforeach()

# Volk and cvk use the same pinned headers and platform entry points.
set(VOLK_PULL_IN_VULKAN OFF)
set(VOLK_STATIC_DEFINES ${cvk_platform_defines})
add_subdirectory(external/volk EXCLUDE_FROM_ALL)
set_target_properties(volk PROPERTIES SYSTEM TRUE C_EXTENSIONS ON)
target_link_libraries(volk PUBLIC Vulkan::Headers)
target_compile_definitions(volk PUBLIC VK_NO_PROTOTYPES)
if("ubm" IN_LIST CVK_PLATFORMS)
    # Neither pinned umbrella header includes UBM yet; expose it to Volk explicitly.
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/cvk_volk_platforms.h"
        "#include <vulkan/vulkan.h>\n#include <vulkan/vulkan_ubm.h>\n")
    target_compile_definitions(volk PUBLIC VOLK_VULKAN_H_PATH="${CMAKE_CURRENT_BINARY_DIR}/cvk_volk_platforms.h")
endif()

find_package(Python3 COMPONENTS Interpreter)

# Optimize our libraries and executables for speed in every configuration.
# Set this after adding third-party projects so their build settings are untouched.
include(CheckIPOSupported)
check_ipo_supported(LANGUAGES C)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(cvk_configurations Debug Release RelWithDebInfo MinSizeRel ${CMAKE_CONFIGURATION_TYPES} ${CMAKE_BUILD_TYPE})
list(REMOVE_DUPLICATES cvk_configurations)
foreach(configuration IN LISTS cvk_configurations)
    string(TOUPPER "${configuration}" configuration_upper)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_${configuration_upper} TRUE)
endforeach()

# Remove conflicting MSVC defaults, including Debug's incompatible runtime checks.
if(CMAKE_C_COMPILER_ID STREQUAL "MSVC")
    set(cvk_flag_variables CMAKE_C_FLAGS)
    foreach(configuration IN LISTS cvk_configurations)
        string(TOUPPER "${configuration}" configuration_upper)
        list(APPEND cvk_flag_variables CMAKE_C_FLAGS_${configuration_upper})
    endforeach()
    foreach(flag_variable IN LISTS cvk_flag_variables)
        string(REGEX REPLACE "[/-](Ob[0-3]|O[0123dsx]|RTC[1su]+)" "" ${flag_variable} "${${flag_variable}}")
    endforeach()
endif()
add_compile_options($<$<COMPILE_LANG_AND_ID:C,MSVC>:/O2> $<$<COMPILE_LANG_AND_ID:C,MSVC>:/Ob3>)
add_compile_options($<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-O3>)
# Prefer full LTO for this generated, monolithic source: it can discard unreachable
# wrapper families before optimizing them, and exposes every live tier for inlining.
# Override CMake's Clang ThinLTO default at both compilation and the final link.
add_compile_options($<$<COMPILE_LANG_AND_ID:C,Clang,AppleClang>:-flto=full>)
add_link_options($<$<LINK_LANG_AND_ID:C,Clang,AppleClang>:-flto=full>)

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
set(CVK_SOURCES src/cvk.c src/cvk_json.c ${cvk_platform_sources})

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
set(SCVK_SOURCES src/scvk.c src/scvk_json.c ${scvk_platform_sources})
add_library(scvk STATIC ${SCVK_SOURCES})
target_compile_definitions(scvk PRIVATE ${scvk_platform_defines})
target_include_directories(scvk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(scvk SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanSC-Headers/include)
if(MSVC)
    target_compile_options(scvk PRIVATE /W4)
else()
    target_compile_options(scvk PRIVATE -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration)
endif()

option(CVK_BUILD_EXAMPLES "Build the Windows example applications" ${WIN32})
if(CVK_BUILD_EXAMPLES AND WIN32)
    if(NOT "win32" IN_LIST CVK_PLATFORMS)
        message(FATAL_ERROR "The Windows examples require win32 in CVK_PLATFORMS")
    endif()
    add_subdirectory(examples/ImmersiveWindowDemo)
    add_subdirectory(examples/CastDemo)
endif()
