# The root project provides the shared pinned cdx dependency for both examples.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(CVK_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
set(reference_dir "${PROJECT_SOURCE_DIR}/external/cdx/examples/ImmersiveWindowDemo")
set(shader_headers)
foreach(shader fullscreen.vert nv12.frag bicubic.frag)
    string(REPLACE "." "_" symbol ${shader})
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${symbol}.h")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${CVK_GLSLC}" -O --target-env=vulkan1.1
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${shader}" -o "${output}.spv"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_spirv.py"
                "${output}.spv" "${output}" "${symbol}"
        DEPENDS "shaders/${shader}" embed_spirv.py VERBATIM)
    list(APPEND shader_headers "${output}")
endforeach()

set(pipeline_headers)
foreach(pipeline nv12 bicubic)
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${pipeline}_pipeline_json.h")
    add_custom_command(OUTPUT "${output}"
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_text.py"
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/${pipeline}.pipeline.json"
                "${output}" "${pipeline}_pipeline_json"
        DEPENDS "shaders/${pipeline}.pipeline.json" embed_text.py VERBATIM)
    list(APPEND pipeline_headers "${output}")
endforeach()

add_library(immersive-video STATIC video.c video.h renderer.c renderer.h ${shader_headers} ${pipeline_headers})
if(MSVC)
    target_compile_options(immersive-video PRIVATE /GS- /Zl /wd4711)
endif()
target_include_directories(immersive-video PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(immersive-video SYSTEM PUBLIC "${reference_dir}")
target_link_libraries(immersive-video PUBLIC cvk volk::volk cdx11 cdxgi cmf vk-diagnostic PRIVATE d3dcompiler)
option(CVK_DEMO_UNICODE "Select Unicode for the generic Win32 demo interfaces" ON)
if(CVK_DEMO_UNICODE)
    target_compile_definitions(immersive-video PUBLIC UNICODE _UNICODE)
    target_link_libraries(immersive-video PUBLIC cast-text-unicode)
else()
    target_link_libraries(immersive-video PUBLIC cast-text-ansi)
endif()
option(CVK_DEMO_VALIDATION "Enable Vulkan validation in ImmersiveWindowDemo" OFF)
if(CVK_DEMO_VALIDATION)
    target_compile_definitions(immersive-video PRIVATE CVK_DEMO_VALIDATION)
endif()

add_executable(ImmersiveWindowDemo WIN32 demo.c immersive_window.c
    "${PROJECT_SOURCE_DIR}/examples/CastDemo/os/win32/os_entry.c" presentation.h
    "${reference_dir}/immersive_window.h"
    shaders/fullscreen.vert shaders/nv12.frag shaders/bicubic.frag)
set_source_files_properties(shaders/fullscreen.vert shaders/nv12.frag shaders/bicubic.frag
    PROPERTIES HEADER_FILE_ONLY TRUE)
set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ImmersiveWindowDemo)
target_link_libraries(ImmersiveWindowDemo PRIVATE immersive-video cdcomp cd2d cdw
    dwmapi uxtheme gdi32 user32 shell32 comctl32 ole32 uuid cast-no-crt)
# Optional real-GPU comparison against the pinned reference implementation.
set(CVK_DEMO_TEST_VIDEO "" CACHE FILEPATH "Video fixture for the optional CDX/CVK pixel comparison")
if(CVK_DEMO_TEST_VIDEO)
    add_library(immersive-reference-video STATIC "${reference_dir}/video.c")
    cvk_external_target(immersive-reference-video)
    target_link_libraries(immersive-reference-video PUBLIC cdx11 cdxgi cmf d3dcompiler)
    target_compile_definitions(immersive-reference-video PRIVATE
        VideoStream_Open=ReferenceVideoStream_Open VideoStream_Close=ReferenceVideoStream_Close
        VideoStream_GetInfo=ReferenceVideoStream_GetInfo VideoStream_NextFrame=ReferenceVideoStream_NextFrame
        VideoStream_Draw=ReferenceVideoStream_Draw VideoStream_Prefetch=ReferenceVideoStream_Prefetch)
    add_executable(cvk-immersive-video-test "${PROJECT_SOURCE_DIR}/tests/immersive_video_test.c")
    target_link_libraries(cvk-immersive-video-test PRIVATE immersive-video immersive-reference-video ole32 uuid)
    add_test(NAME cvk-immersive-video-pixels COMMAND cvk-immersive-video-test "${CVK_DEMO_TEST_VIDEO}")
    set_tests_properties(cvk-immersive-video-pixels PROPERTIES TIMEOUT 120)
endif()

