# The pinned cva checkout declares libva in .gitmodules but contains no gitlink.
# Use the separately pinned libva revision from cva.policy.json without modifying
# either upstream checkout. Use cva's supported generation policy to return optional
# diagnostic query failures to the caller without replacing the synchronization result.
set(cva_source "${PROJECT_SOURCE_DIR}/external/cva")
set(cva_headers "${PROJECT_SOURCE_DIR}/external/libva")
set(cva_sdk "${PROJECT_BINARY_DIR}/external/cva/sdk")
set(cva_generated "${PROJECT_BINARY_DIR}/external/cva/generated")
file(READ "${cva_source}/cva.policy.json" cva_policy)
string(JSON cva_revision GET "${cva_policy}" revision)
execute_process(COMMAND git -C "${cva_headers}" rev-parse HEAD
    OUTPUT_VARIABLE cva_header_revision OUTPUT_STRIP_TRAILING_WHITESPACE
    COMMAND_ERROR_IS_FATAL ANY)
if(NOT cva_header_revision STREQUAL cva_revision)
    message(FATAL_ERROR "libva headers must match the pinned cva registry revision: ${cva_revision}")
endif()
file(READ "${cva_headers}/meson.build" cva_meson)
foreach(component major minor micro)
    string(REGEX MATCH "va_api_${component}_version = ([0-9]+)" cva_match "${cva_meson}")
    string(TOUPPER "${component}" component_upper)
    set(VA_API_${component_upper}_VERSION "${CMAKE_MATCH_1}")
endforeach()
set(VA_API_VERSION "${VA_API_MAJOR_VERSION}.${VA_API_MINOR_VERSION}.${VA_API_MICRO_VERSION}")
configure_file("${cva_headers}/va/va_version.h.in" "${cva_sdk}/va/va_version.h" @ONLY)
foreach(header drm/va_drm.h win32/va_win32.h glx/va_glx.h wayland/va_wayland.h)
    get_filename_component(name "${header}" NAME)
    configure_file("${cva_headers}/va/${header}" "${cva_sdk}/va/${name}" COPYONLY)
endforeach()
if(WIN32)
    set(cva_platform win32)
elseif(UNIX AND NOT APPLE)
    set(cva_platform drm)
else()
    message(FATAL_ERROR "No configured cva backend exists for this target")
endif()
find_package(Python3 3.10 COMPONENTS Interpreter REQUIRED)
file(GLOB_RECURSE cva_generator_inputs CONFIGURE_DEPENDS
    "${cva_source}/scripts/*.py" "${cva_source}/scripts/protocols/*.c" "${cva_headers}/va/*.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/scripts/configure_cva.py"
    "${PROJECT_SOURCE_DIR}/cmake/cva-status-policy.json"
    "${cva_source}/cva.policy.json" "${cva_source}/.clang-format" ${cva_generator_inputs})
execute_process(COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/configure_cva.py"
    --platform "${cva_platform}" --output "${cva_generated}"
    COMMAND_ERROR_IS_FATAL ANY)
add_library(cva STATIC EXCLUDE_FROM_ALL "${cva_generated}/src/cva.c" "${cva_generated}/src/cva_json.c")
add_library(cva::cva ALIAS cva)
target_include_directories(cva SYSTEM PUBLIC "${cva_generated}/include" "${cva_headers}" "${cva_sdk}")
target_sources(cva PRIVATE "${cva_generated}/src/${cva_platform}/cva_${cva_platform}.c")
cvk_external_target(cva)
add_custom_target(cva-check-configured
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/configure_cva.py"
        --platform "${cva_platform}" --output "${cva_generated}" --check
    VERBATIM)
add_test(NAME cva-configured-files-match
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/configure_cva.py"
        --platform "${cva_platform}" --output "${cva_generated}" --check)
