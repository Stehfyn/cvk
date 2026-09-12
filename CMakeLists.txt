cmake_minimum_required(VERSION 3.25)
project(cvk LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Pinned Vulkan headers and registry, and the utility headers (format tables) (git submodules).
add_subdirectory(external/Vulkan-Headers EXCLUDE_FROM_ALL)
add_subdirectory(external/Vulkan-Utility-Libraries EXCLUDE_FROM_ALL)

find_package(Python3 COMPONENTS Interpreter)

# Regenerate include/cvk.h and src/cvk.c from the registry and cvk.policy.json.
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
set(CVK_SOURCES src/cvk.c)
if(WIN32)
    list(APPEND CVK_SOURCES src/cvk_win32.c)
elseif(UNIX)
    list(APPEND CVK_SOURCES src/cvk_xlib.c src/cvk_posix.c)
endif()

add_library(cvk STATIC ${CVK_SOURCES})
target_include_directories(cvk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(cvk PUBLIC Vulkan::Headers)
# Utility headers are third-party: a system include so their own pedantic warnings do not surface.
target_include_directories(cvk SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/Vulkan-Utility-Libraries/include)

if(MSVC)
    target_compile_options(cvk PRIVATE /W4)
else()
    target_compile_options(cvk PRIVATE -Wall -Wextra -Wpedantic)
endif()

# The JSON tier's test: parses a Vulkan SC pipeline JSON into structures; needs the loader to link.
find_package(Vulkan QUIET)
if(Vulkan_FOUND AND Vulkan_VERSION VERSION_GREATER_EQUAL 1.4)
    add_executable(cvk-pipeline-json-test tests/pipeline_json_test.c)
    target_link_libraries(cvk-pipeline-json-test PRIVATE cvk Vulkan::Vulkan)
    add_test(NAME cvk-pipeline-json-parse COMMAND cvk-pipeline-json-test ${CMAKE_CURRENT_SOURCE_DIR}/tests/cube.pc.json)
else()
    message(STATUS "Vulkan 1.4 loader not found (the library links its core commands): the pipeline JSON test is not built")
endif()

# Vulkan SC profile: the same generator over the SC registry (external/VulkanSC-Headers),
# prefix scvk, a standalone library against the SC headers. The format protocols need the
# utility headers, which describe formats the SC headers lack, so that file is absent from it.
set(SCVK_SOURCES src/scvk.c)
if(WIN32)
    list(APPEND SCVK_SOURCES src/scvk_win32.c)
elseif(UNIX)
    list(APPEND SCVK_SOURCES src/scvk_posix.c)
endif()
add_library(scvk STATIC ${SCVK_SOURCES})
target_include_directories(scvk PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(scvk SYSTEM PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/external/VulkanSC-Headers/include)
if(MSVC)
    target_compile_options(scvk PRIVATE /W4)
else()
    target_compile_options(scvk PRIVATE -Wall -Wextra -Wpedantic)
endif()
