# CastDemo: play a video on a Google Cast receiver (XGIMI Horizon S) over the network.
# The session library is shared by the window, the console tool and the test.
add_library(cast-session STATIC cast_session.c cast_session.h cast_channel.c cast_channel.h
    cast_message.c cast_message.h media_server.c media_server.h mp4_index.c mp4_index.h
    subtitles.c subtitles.h audio_gain.c audio_gain.h
    proxy.c proxy.h gpu_transcode.c gpu_transcode.h mp4_demux.c mp4_demux.h
    mp4_mux.c mp4_mux.h h264_params.c h264_params.h gpu_device.c gpu_device.h
    gpu_decode.c gpu_decode.h gpu_encode.c gpu_encode.h ssdp.c ssdp.h)
target_include_directories(cast-session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(cast-session PUBLIC cvk volk::volk ws2_32 mswsock secur32 winhttp iphlpapi mfplat mfreadwrite mfuuid ole32 propsys)

add_executable(CastDemo WIN32 cast_gui.c)
target_compile_definitions(CastDemo PRIVATE UNICODE _UNICODE)
target_link_libraries(CastDemo PRIVATE cast-session comctl32 comdlg32 shell32 ole32 user32 gdi32 uxtheme dwmapi)

add_executable(CastDemoCli cast_demo.c)
target_link_libraries(CastDemoCli PRIVATE cast-session)

# Framing, range and JSON checks that need no receiver or GPU.
add_executable(cvk-cast-message-test "${PROJECT_SOURCE_DIR}/tests/cast_message_test.c")
target_link_libraries(cvk-cast-message-test PRIVATE cast-session)
add_test(NAME cvk-cast-messages COMMAND cvk-cast-message-test)

foreach(target cast-session CastDemo CastDemoCli cvk-cast-message-test)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()
