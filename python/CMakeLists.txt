if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "CVK Python currently requires a 64-bit build")
endif()
find_package(Python3 REQUIRED COMPONENTS Interpreter)
execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import clang.cindex"
    RESULT_VARIABLE cvk_python_clang_status ERROR_QUIET)
if(NOT cvk_python_clang_status EQUAL 0)
    message(FATAL_ERROR "Python binding generation needs libclang: python -m pip install libclang")
endif()

set(cvk_python_package "${CMAKE_BINARY_DIR}/python/cvk")
set(cvk_python_generator_args "")
foreach(platform IN LISTS CVK_PLATFORMS)
    list(APPEND cvk_python_generator_args --platform "${platform}")
endforeach()
foreach(define IN LISTS cvk_platform_defines)
    list(APPEND cvk_python_generator_args "--clang-arg=-D${define}")
endforeach()
file(GLOB_RECURSE cvk_python_headers "${PROJECT_SOURCE_DIR}/include/*.h"
    "${PROJECT_SOURCE_DIR}/external/Vulkan-Headers/include/*.h")
add_custom_command(
    OUTPUT "${cvk_python_package}/_generated.py" "${cvk_python_package}/_adapters.c"
           "${cvk_python_package}/_exports.def"
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/generate_python.py"
            --out-dir "${cvk_python_package}" ${cvk_python_generator_args}
    DEPENDS "${PROJECT_SOURCE_DIR}/scripts/generate_python.py"
            "${PROJECT_SOURCE_DIR}/scripts/generate.py"
            "${PROJECT_SOURCE_DIR}/scripts/vkxml.py"
            "${PROJECT_SOURCE_DIR}/cvk.policy.json"
            "${PROJECT_SOURCE_DIR}/external/Vulkan-Headers/registry/vk.xml"
            ${cvk_python_headers}
    COMMENT "Generating Python bindings from the CVK headers" VERBATIM)

set(cvk_python_sources "")
foreach(source IN LISTS CVK_SOURCES)
    list(APPEND cvk_python_sources "${PROJECT_SOURCE_DIR}/${source}")
endforeach()
add_library(cvk-python SHARED ${cvk_python_sources} bridge.c
    "${cvk_python_package}/_adapters.c")
if(WIN32)
    # Explicit exports also work with MSVC /GL objects, which CMake's automatic
    # export scanner cannot read. Never export Volk's mutable dispatch data.
    target_sources(cvk-python PRIVATE "${cvk_python_package}/_exports.def")
endif()
target_include_directories(cvk-python PRIVATE "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/external/Vulkan-Utility-Libraries/include")
target_link_libraries(cvk-python PRIVATE volk::volk)
set_target_properties(volk PROPERTIES POSITION_INDEPENDENT_CODE ON)
set_target_properties(cvk-python PROPERTIES
    OUTPUT_NAME _cvk PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY "${cvk_python_package}"
    LIBRARY_OUTPUT_DIRECTORY "${cvk_python_package}")
foreach(config IN LISTS CMAKE_CONFIGURATION_TYPES)
    string(TOUPPER "${config}" config_upper)
    set_target_properties(cvk-python PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config_upper} "${cvk_python_package}"
        LIBRARY_OUTPUT_DIRECTORY_${config_upper} "${cvk_python_package}")
endforeach()

file(GLOB cvk_python_modules CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/cvk/*.py")
foreach(module IN LISTS cvk_python_modules)
    get_filename_component(name "${module}" NAME)
    configure_file("${module}" "${cvk_python_package}/${name}" COPYONLY)
endforeach()
install(TARGETS cvk-python
    RUNTIME DESTINATION cvk COMPONENT python
    LIBRARY DESTINATION cvk COMPONENT python)
install(FILES ${cvk_python_modules} "${cvk_python_package}/_generated.py" DESTINATION cvk COMPONENT python)
foreach(dependency volk Vulkan-Headers Vulkan-Utility-Libraries)
    install(FILES "${PROJECT_SOURCE_DIR}/external/${dependency}/LICENSE.md"
        DESTINATION "cvk/licenses/${dependency}" COMPONENT python)
    if(EXISTS "${PROJECT_SOURCE_DIR}/external/${dependency}/LICENSES")
        install(DIRECTORY "${PROJECT_SOURCE_DIR}/external/${dependency}/LICENSES"
            DESTINATION "cvk/licenses/${dependency}" COMPONENT python)
    endif()
endforeach()

add_test(NAME cvk-python-bindings
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/test_python_bindings.py")
set_tests_properties(cvk-python-bindings PROPERTIES ENVIRONMENT "PYTHONPATH=${CMAKE_BINARY_DIR}/python")
add_test(NAME cvk-python-generator
    COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/tests/test_python_generator.py")
