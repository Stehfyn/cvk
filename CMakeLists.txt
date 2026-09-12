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
endif()

add_library(cvk STATIC src/cvk.c)
target_include_directories(cvk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cvk PUBLIC Vulkan::Headers)

if(MSVC)
    target_compile_options(cvk PRIVATE /W4)
else()
    target_compile_options(cvk PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
    target_compile_definitions(cvk PUBLIC VK_USE_PLATFORM_WIN32_KHR=1)
elseif(UNIX)
    target_compile_definitions(cvk PUBLIC VK_USE_PLATFORM_XLIB_KHR=1)
endif()
