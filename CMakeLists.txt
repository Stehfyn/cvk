cmake_minimum_required(VERSION 3.25)
project(cvk LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Pinned Vulkan headers and registry (git submodule).
add_subdirectory(external/Vulkan-Headers EXCLUDE_FROM_ALL)

find_package(Python3 COMPONENTS Interpreter)

# Regenerate include/cvk.h and src/cvk.c from the registry and cvk.policy.json.
# Generated files are committed; run this target after changing the policy or generator.
if(Python3_Interpreter_FOUND)
    add_custom_target(cvk-generate
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/generate.py
                --policy ${CMAKE_CURRENT_SOURCE_DIR}/cvk.policy.json
                --out-dir ${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating cvk from the Vulkan registry"
        VERBATIM)

    # Rule zero: the generated files must equal a fresh generation.
    enable_testing()
    add_test(NAME cvk-generated-files-match
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/check_generated.py
                --policy ${CMAKE_CURRENT_SOURCE_DIR}/cvk.policy.json
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Core is platform-free; each platform has its own generated pair that includes cvk.h.
set(CVK_SOURCES src/cvk.c)
if(WIN32)
    list(APPEND CVK_SOURCES src/cvk_win32.c)
elseif(UNIX)
    list(APPEND CVK_SOURCES src/cvk_xlib.c)
endif()

add_library(cvk STATIC ${CVK_SOURCES})
target_include_directories(cvk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cvk PUBLIC Vulkan::Headers)

if(MSVC)
    target_compile_options(cvk PRIVATE /W4)
else()
    target_compile_options(cvk PRIVATE -Wall -Wextra -Wpedantic)
endif()
