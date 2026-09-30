# The root project provides the shared pinned cdx dependency for both examples.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
find_program(CVK_GLSLC glslc HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
set(reference_dir "${PROJECT_SOURCE_DIR}/external/cdx/examples/ImmersiveWindowDemo")
set(shader_headers)
foreach(shader fullscreen.vert nv12.frag)
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

set(pipeline_header "${CMAKE_CURRENT_BINARY_DIR}/nv12_pipeline_json.h")
add_custom_command(OUTPUT "${pipeline_header}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_text.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/nv12.pipeline.json"
            "${pipeline_header}" "c_vkNv12PipelineJson"
    DEPENDS shaders/nv12.pipeline.json embed_text.py VERBATIM)

set(output_pass_header "${CMAKE_CURRENT_BINARY_DIR}/output_renderpass_json.h")
add_custom_command(OUTPUT "${output_pass_header}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_text.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/output.renderpass.json"
            "${output_pass_header}" "c_vkOutputRenderPassJson"
    DEPENDS shaders/output.renderpass.json embed_text.py VERBATIM)

set(plane_shader_header "${CMAKE_CURRENT_BINARY_DIR}/plane_copy_hlsl.h")
add_custom_command(OUTPUT "${plane_shader_header}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/embed_text.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/shaders/plane_copy.hlsl"
            "${plane_shader_header}" "plane_copy_hlsl"
    DEPENDS shaders/plane_copy.hlsl embed_text.py VERBATIM)

add_library(immersive-video STATIC video.c video.h renderer.c renderer.h ${shader_headers} "${pipeline_header}" "${output_pass_header}" "${plane_shader_header}")
if(MSVC)
    target_compile_options(immersive-video PRIVATE /GS- /Zl)
endif()
target_include_directories(immersive-video PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
    PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_include_directories(immersive-video SYSTEM PUBLIC "${reference_dir}")
target_link_libraries(immersive-video PUBLIC cvk volk::volk cd3d11 cdxgi cmf vk-diagnostic PRIVATE d3dcompiler)
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

set(opus3_dir "${CMAKE_CURRENT_SOURCE_DIR}/opus3")
add_library(immersive-opus3-window STATIC
    "${opus3_dir}/caption.c"
    "${opus3_dir}/delayhlpx.c"
    "${opus3_dir}/ShellScalingApiX.c"
    "${opus3_dir}/swapchain.c"
    "${opus3_dir}/versionhelpersx.c"
    "${opus3_dir}/window.c")
target_include_directories(immersive-opus3-window PUBLIC "${opus3_dir}")
target_compile_definitions(immersive-opus3-window PRIVATE UNICODE _UNICODE)
target_link_libraries(immersive-opus3-window PUBLIC cdcomp cd2d cd3d11 cdxgi)
target_compile_options(immersive-opus3-window PRIVATE /GS- /Zl)
add_executable(ImmersiveWindowDemo WIN32
    opus3_main.c
    opus3_main.h
    opus3_video.c
    opus3_video.h
    shaders/fullscreen.vert
    shaders/nv12.frag
    shaders/plane_copy.hlsl)
set_source_files_properties(shaders/fullscreen.vert shaders/nv12.frag shaders/plane_copy.hlsl
    PROPERTIES HEADER_FILE_ONLY TRUE)
set_property(DIRECTORY "${PROJECT_SOURCE_DIR}" PROPERTY VS_STARTUP_PROJECT ImmersiveWindowDemo)
target_compile_options(ImmersiveWindowDemo PRIVATE /GS- /Zl)
target_link_options(ImmersiveWindowDemo PRIVATE /NODEFAULTLIB /ENTRY:WinMainCRTStartup /INCREMENTAL:NO)
target_link_libraries(ImmersiveWindowDemo PRIVATE
    immersive-video
    immersive-opus3-window
    cdcomp
    cd2d
    cdw
    dwmapi
    uxtheme
    gdi32
    user32
    shell32
    comctl32
    ole32
    uuid
    windowscodecs
    d3d11
    d2d1
    dwrite
    dxgi
    dxguid
    dcomp
    mfplat
    mfreadwrite
    mfuuid
    cast-formats
    crt-abi
    BufferOverflowU
    ntdll
    kernel32)
# Optional real-GPU comparison against the pinned reference implementation.
set(CVK_DEMO_TEST_VIDEO "" CACHE FILEPATH "Video fixture for the optional CDX/CVK pixel comparison")
if(CVK_DEMO_TEST_VIDEO)
    add_library(immersive-reference-video STATIC "${reference_dir}/video.c")
    cvk_external_target(immersive-reference-video)
    target_link_libraries(immersive-reference-video PUBLIC cd3d11 cdxgi cmf d3dcompiler)
    target_compile_definitions(immersive-reference-video PRIVATE
        VideoStream_Open=ReferenceVideoStream_Open VideoStream_Close=ReferenceVideoStream_Close
        VideoStream_GetInfo=ReferenceVideoStream_GetInfo VideoStream_NextFrame=ReferenceVideoStream_NextFrame
        VideoStream_Draw=ReferenceVideoStream_Draw VideoStream_Prefetch=ReferenceVideoStream_Prefetch)
    add_executable(cvk-immersive-video-test "${PROJECT_SOURCE_DIR}/tests/immersive_video_test.c")
    target_link_libraries(cvk-immersive-video-test PRIVATE immersive-video immersive-reference-video ole32 uuid)
    add_test(NAME cvk-immersive-video-pixels COMMAND cvk-immersive-video-test "${CVK_DEMO_TEST_VIDEO}")
    set_tests_properties(cvk-immersive-video-pixels PROPERTIES TIMEOUT 120)
endif()

