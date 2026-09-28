cmake_minimum_required(VERSION 3.25)
project(CastDiagnostics LANGUAGES C)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_C_COMPILER_ID STREQUAL "Clang")
    message(FATAL_ERROR "This diagnostic backend requires Linux and Clang; Windows application coverage is separate")
endif()

enable_testing()
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
get_filename_component(cast_source "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
get_filename_component(cast_root "${cast_source}/../.." ABSOLUTE)

set(CAST_DIAGNOSTIC_MODE "memcheck" CACHE STRING "memcheck, address, memory or thread")
set_property(CACHE CAST_DIAGNOSTIC_MODE PROPERTY STRINGS memcheck address memory thread)
set(cast_sanitizer_options)
if(CAST_DIAGNOSTIC_MODE STREQUAL "address")
    list(APPEND cast_sanitizer_options
        -fsanitize=address,undefined,integer,implicit-conversion,local-bounds,float-divide-by-zero
        -fsanitize-address-use-after-scope -fsanitize-address-use-after-return=always
        -fno-sanitize-recover=all)
elseif(CAST_DIAGNOSTIC_MODE STREQUAL "memory")
    list(APPEND cast_sanitizer_options -fsanitize=memory -fsanitize-memory-track-origins=2
        -fno-sanitize-recover=all)
elseif(CAST_DIAGNOSTIC_MODE STREQUAL "thread")
    list(APPEND cast_sanitizer_options -fsanitize=thread -fno-sanitize-recover=all)
elseif(NOT CAST_DIAGNOSTIC_MODE STREQUAL "memcheck")
    message(FATAL_ERROR "Unknown CAST_DIAGNOSTIC_MODE: ${CAST_DIAGNOSTIC_MODE}")
endif()

# These are diagnostic processes: sanitizer startup and its runtime are required.
# Production's CRT-free entry/link policy remains unchanged. Every owned source,
# including the portable memory backend, receives the same instrumentation.
add_compile_options(-Weverything -Werror -Wno-padded -g -fno-omit-frame-pointer
    -fPIE -ffreestanding ${cast_sanitizer_options})
add_link_options(-fuse-ld=lld -pie ${cast_sanitizer_options})

file(GLOB cast_wire_sources CONFIGURE_DEPENDS "${cast_source}/utl/*.c")
add_library(cast-diagnostic-formats STATIC ${cast_wire_sources}
    "${cast_source}/utl/posix/utl_arguments.c"
    "${cast_source}/cast_http.c"
    "${cast_source}/crt/crt_memory.c"
    "${cast_source}/crt/portable/crt_memory.c"
    "${cast_source}/os/clang/os_assert.c")
target_include_directories(cast-diagnostic-formats PUBLIC "${cast_source}")

set(cast_fixture_names
    mpeg4 mpeg4audio mpeg4systems chromecast json webvtt srt ssdp url
    http_policy xml crt arguments_posix format protocol http)
find_program(CAST_VALGRIND_EXECUTABLE NAMES valgrind REQUIRED)
foreach(fixture IN LISTS cast_fixture_names)
    set(target "cvk-cast-${fixture}-test")
    add_executable(${target} "${cast_root}/tests/cast_${fixture}_test.c" entry.c)
    target_link_libraries(${target} PRIVATE cast-diagnostic-formats)
    if(CAST_DIAGNOSTIC_MODE STREQUAL "memcheck")
        add_test(NAME "cast-${fixture}" COMMAND "${CAST_VALGRIND_EXECUTABLE}"
            --tool=memcheck --leak-check=full --show-leak-kinds=all
            --errors-for-leak-kinds=all --track-origins=yes --track-fds=yes
            --expensive-definedness-checks=yes --error-limit=no --num-callers=40
            --default-suppressions=no --error-exitcode=99
            "--log-file=${CMAKE_CURRENT_BINARY_DIR}/${fixture}.valgrind.log"
            $<TARGET_FILE:${target}>)
    else()
        add_test(NAME "cast-${fixture}" COMMAND ${target})
        set_tests_properties("cast-${fixture}" PROPERTIES ENVIRONMENT
            "ASAN_OPTIONS=detect_leaks=1:detect_stack_use_after_return=1:strict_string_checks=1:halt_on_error=1;UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1;MSAN_OPTIONS=track_origins=2:halt_on_error=1:exit_code=99;TSAN_OPTIONS=halt_on_error=1:exitcode=99")
    endif()
    set_tests_properties("cast-${fixture}" PROPERTIES TIMEOUT 300)
endforeach()

include("${cast_source}/BuildPolicy.cmake")
cast_apply_directory_policy("${CMAKE_CURRENT_SOURCE_DIR}")
