# CastDemo: play a video on a Google Cast receiver (XGIMI Horizon S) over the network.
# The session library is shared by the window, the console tool and the test.
add_subdirectory(crt)
if(MSVC AND CMAKE_C_COMPILER_ARCHITECTURE_ID STREQUAL "x64")
    add_subdirectory(os/msvc/x64)
else()
    message(FATAL_ERROR "The OS atomics need a verified backend for this target")
endif()
# ReadConsoleInputEx has documented exports but no SDK import library. Bind its
# exact ABI at process load, with no runtime module lookup or function-pointer cast.
set(cast_console_import_library "${CMAKE_CURRENT_BINARY_DIR}/console_imports.lib")
add_custom_command(OUTPUT "${cast_console_import_library}"
    BYPRODUCTS "${CMAKE_CURRENT_BINARY_DIR}/console_imports.exp"
    COMMAND "${CMAKE_AR}" /nologo /machine:x64
        "/def:${CMAKE_CURRENT_SOURCE_DIR}/os/win32/console_imports.def"
        "/out:${cast_console_import_library}"
    DEPENDS os/win32/console_imports.def
    VERBATIM)
add_custom_target(cast-console-imports-build DEPENDS "${cast_console_import_library}")
add_library(cast-console-imports STATIC IMPORTED)
set_target_properties(cast-console-imports PROPERTIES IMPORTED_LOCATION "${cast_console_import_library}")
add_dependencies(cast-console-imports cast-console-imports-build)
# Both native text modes compile the same encoding-independent key implementation.
# The application consumes the Unicode object; ANSI remains a required compile check.
foreach(cast_console_mode IN ITEMS unicode ansi)
    add_library(cast-console-${cast_console_mode} OBJECT os/win32/os_console.c)
    target_include_directories(cast-console-${cast_console_mode} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_options(cast-console-${cast_console_mode} PRIVATE /GS- /Zl)
    target_compile_definitions(cast-console-${cast_console_mode} PRIVATE WIN32_LEAN_AND_MEAN)
endforeach()
target_compile_definitions(cast-console-unicode PRIVATE UNICODE _UNICODE ReadConsoleInputEx=ReadConsoleInputExW)
target_compile_definitions(cast-console-ansi PRIVATE ReadConsoleInputEx=ReadConsoleInputExA)
if(WIN32)
    set(cast_arguments_source utl/win32/utl_arguments.c)
elseif(UNIX)
    set(cast_arguments_source utl/posix/utl_arguments.c)
else()
    message(FATAL_ERROR "No CastDemo argument implementation for this target")
endif()
add_library(cast-formats STATIC
    cast_http.c cast_http.h
    utl/utl_defines.h
    utl/utl_mpeg4.c utl/utl_mpeg4.h
    utl/utl_expgolomb.c utl/utl_expgolomb.h
    utl/utl_h264.c utl/utl_h264.h
    utl/utl_matroska.c utl/utl_matroska.h
    utl/utl_mpeg4systems.c utl/utl_mpeg4systems.h
    utl/utl_mpeg4audio.c utl/utl_mpeg4audio.h
    utl/utl_protobuf.c utl/utl_protobuf.h
    utl/utl_unicode.c utl/utl_unicode.h
    utl/utl_webvtt.c utl/utl_webvtt.h
    utl/utl_srt.c utl/utl_srt.h
    utl/utl_url.c utl/utl_url.h
    utl/utl_xml.c utl/utl_xml.h
    utl/utl_upnp.c utl/utl_upnp.h
    utl/utl_castv2.c utl/utl_castv2.h
    utl/utl_json.c utl/utl_json.h
    utl/utl_chromecast.c utl/utl_chromecast.h
    utl/utl_decimal.c utl/utl_decimal.h
    utl/utl_http.c utl/utl_http.h
    utl/utl_ssdp.c utl/utl_ssdp.h
    ${cast_arguments_source} utl/utl_arguments.h
    os/os_types.h os/os_compiler.h os/os_assert.h os/os_process.h
    os/os_atomic.c os/os_atomic.h os/os_atomic_backend.h
    $<TARGET_OBJECTS:cast-os-atomic-native>
    os/os_ringbuffer.c os/os_ringbuffer.h os/os_queue.c os/os_queue.h
    os/os_mailbox.c os/os_mailbox.h
    os/os_thread.h os/win32/os_thread.c
    os/os_memory.h os/win32/os_memory.c
    os/os_console.h os/win32/os_console_native.h
    $<TARGET_OBJECTS:cast-console-unicode>
    os/os_temporaryfile.h os/win32/os_temporaryfile.c
    os/win32/os_assert.c os/win32/os_process.c)
set_source_files_properties(os/win32/os_process.c PROPERTIES COMPILE_DEFINITIONS "UNICODE;_UNICODE;WIN32_LEAN_AND_MEAN")
set_source_files_properties(os/win32/os_assert.c PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
set_source_files_properties(os/win32/os_thread.c PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
set_source_files_properties(os/win32/os_memory.c PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
set_source_files_properties(os/win32/os_temporaryfile.c
    PROPERTIES COMPILE_DEFINITIONS "UNICODE;_UNICODE;WIN32_LEAN_AND_MEAN")
set_source_files_properties(os/win32/os_network.c ssdp.c media_server.c PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
set_source_files_properties(mkv_subtitles.c subtitles.c PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
set_source_files_properties(mp4_demux.c mp4_mux.c mp4_index.c PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
target_include_directories(cast-formats PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
add_dependencies(cast-formats cast-console-ansi)
target_link_libraries(cast-formats PUBLIC crt cast-console-imports)
add_subdirectory(gfxlib)
if(MSVC)
    target_compile_options(cast-formats PRIVATE /GS- /Zl)
endif()

add_library(cast-network STATIC cast_network.c cast_network.h os/os_network.h os/win32/os_network.c)
target_link_libraries(cast-network PUBLIC cast-formats ws2_32)
if(MSVC)
    target_compile_options(cast-network PRIVATE /GS- /Zl)
endif()

add_library(cast-session STATIC cast_session.c cast_session.h cast_channel.c cast_channel.h
    cast_media.c cast_media.h
    cast_events.c cast_events.h cast_commands.c cast_commands.h
    media_server.c media_server.h mp4_index.c mp4_index.h
    subtitles.c subtitles.h audio_gain.c audio_gain.h
    mkv_subtitles.c mkv_subtitles.h
    proxy.c proxy.h gpu_transcode.c gpu_transcode.h gpu_probe.c gpu_probe.h mp4_demux.c mp4_demux.h
    mp4_mux.c mp4_mux.h h264_params.c h264_params.h gpu_device.c gpu_device.h
    gpu_decode.c gpu_decode.h gpu_encode.c gpu_encode.h ssdp.c ssdp.h)
target_include_directories(cast-session PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(cast-session PUBLIC UNICODE _UNICODE)
set_source_files_properties(gpu_probe.c PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
set_source_files_properties(audio_gain.c PROPERTIES COMPILE_DEFINITIONS "COBJMACROS;WIN32_LEAN_AND_MEAN")
set_source_files_properties(cast_session.c PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
target_link_libraries(cast-session PUBLIC cast-formats cast-network cvk volk::volk ws2_32 mswsock secur32 winhttp mfplat mfreadwrite mfuuid ole32 propsys)

add_executable(CastDemo WIN32 cast_gui.c)
target_compile_definitions(CastDemo PRIVATE UNICODE _UNICODE)
target_link_libraries(CastDemo PRIVATE cast-session comctl32 comdlg32 shell32 ole32 user32 gdi32 uxtheme dwmapi)

add_executable(CastDemoCli cast_demo.c)
target_compile_definitions(CastDemoCli PRIVATE WIN32_LEAN_AND_MEAN)
target_link_libraries(CastDemoCli PRIVATE cast-session)

# Framing, range and JSON checks that need no receiver or GPU.
add_executable(cvk-cast-message-test "${PROJECT_SOURCE_DIR}/tests/cast_message_test.c")
target_link_libraries(cvk-cast-message-test PRIVATE cast-session)
add_test(NAME cvk-cast-messages COMMAND cvk-cast-message-test)

add_executable(cvk-cast-subtitle-test "${PROJECT_SOURCE_DIR}/tests/cast_subtitle_test.c")
target_link_libraries(cvk-cast-subtitle-test PRIVATE cast-session)
add_test(NAME cvk-cast-subtitles COMMAND cvk-cast-subtitle-test)

add_executable(cvk-cast-audio-test "${PROJECT_SOURCE_DIR}/tests/cast_audio_test.c")
target_link_libraries(cvk-cast-audio-test PRIVATE cast-session)
add_test(NAME cvk-cast-audio COMMAND cvk-cast-audio-test)

add_executable(cvk-cast-control-test "${PROJECT_SOURCE_DIR}/tests/cast_control_test.c")
target_link_libraries(cvk-cast-control-test PRIVATE cast-session)
add_test(NAME cvk-cast-controls COMMAND cvk-cast-control-test)

if(MSVC)
    # Common CRT-free test policy. Retain IPO, including compiler ABI data objects.
    add_library(cast-no-crt INTERFACE)
    target_link_libraries(cast-no-crt INTERFACE cast-formats crt-abi kernel32)
    target_compile_options(cast-no-crt INTERFACE /GS- /Zl)
    target_link_options(cast-no-crt INTERFACE /NODEFAULTLIB /ENTRY:WinMainStartup)

    add_executable(cvk-cast-console-test "${PROJECT_SOURCE_DIR}/tests/win32/cast_console_test.c"
        os/win32/os_entry.c)
    target_compile_definitions(cvk-cast-console-test PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN)
    target_link_libraries(cvk-cast-console-test PRIVATE cast-no-crt)
    if(Python3_Interpreter_FOUND)
        add_test(NAME cvk-cast-console COMMAND "${Python3_EXECUTABLE}"
            "${PROJECT_SOURCE_DIR}/tests/run_isolated_console.py" $<TARGET_FILE:cvk-cast-console-test>)
    endif()

    add_executable(cvk-cast-thread-test "${PROJECT_SOURCE_DIR}/tests/win32/cast_thread_test.c"
        os/win32/os_entry.c)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/win32/cast_thread_test.c"
        PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
    target_link_libraries(cvk-cast-thread-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-threads COMMAND cvk-cast-thread-test)

    add_executable(cvk-cast-network-test "${PROJECT_SOURCE_DIR}/tests/win32/cast_network_test.c"
        os/win32/os_entry.c)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/win32/cast_network_test.c"
        PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
    target_link_libraries(cvk-cast-network-test PRIVATE cast-network cast-no-crt)
    add_test(NAME cvk-cast-network COMMAND cvk-cast-network-test)

    add_executable(cvk-cast-storage-test "${PROJECT_SOURCE_DIR}/tests/cast_storage_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-storage-test PRIVATE cast-session cast-no-crt)
    add_test(NAME cvk-cast-storage COMMAND cvk-cast-storage-test)

    add_executable(cvk-cast-server-test "${PROJECT_SOURCE_DIR}/tests/win32/cast_server_test.c"
        os/win32/os_entry.c)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/win32/cast_server_test.c"
        PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
    target_link_libraries(cvk-cast-server-test PRIVATE cast-session cast-no-crt)
    add_test(NAME cvk-cast-server COMMAND cvk-cast-server-test)

    add_executable(cvk-cast-temporaryfile-test "${PROJECT_SOURCE_DIR}/tests/win32/cast_temporaryfile_test.c"
        os/win32/os_entry.c)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/win32/cast_temporaryfile_test.c"
        PROPERTIES COMPILE_DEFINITIONS "UNICODE;_UNICODE;WIN32_LEAN_AND_MEAN")
    target_link_libraries(cvk-cast-temporaryfile-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-temporaryfile COMMAND cvk-cast-temporaryfile-test)

    add_executable(cvk-cast-proxy-test "${PROJECT_SOURCE_DIR}/tests/win32/cast_proxy_test.c"
        os/win32/os_entry.c)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/win32/cast_proxy_test.c"
        PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
    target_link_libraries(cvk-cast-proxy-test PRIVATE cast-session cast-no-crt)
    add_test(NAME cvk-cast-proxy COMMAND cvk-cast-proxy-test)


    add_executable(cvk-cast-mpeg4-test "${PROJECT_SOURCE_DIR}/tests/cast_mpeg4_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-mpeg4-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-mpeg4 COMMAND cvk-cast-mpeg4-test)

    add_executable(cvk-cast-mpeg4audio-test "${PROJECT_SOURCE_DIR}/tests/cast_mpeg4audio_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-mpeg4audio-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-mpeg4audio COMMAND cvk-cast-mpeg4audio-test)

    add_executable(cvk-cast-mpeg4systems-test "${PROJECT_SOURCE_DIR}/tests/cast_mpeg4systems_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-mpeg4systems-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-mpeg4systems COMMAND cvk-cast-mpeg4systems-test)

    add_executable(cvk-cast-chromecast-test "${PROJECT_SOURCE_DIR}/tests/cast_chromecast_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-chromecast-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-chromecast COMMAND cvk-cast-chromecast-test)

    add_executable(cvk-cast-json-test "${PROJECT_SOURCE_DIR}/tests/cast_json_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-json-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-json COMMAND cvk-cast-json-test)

    add_executable(cvk-cast-webvtt-test "${PROJECT_SOURCE_DIR}/tests/cast_webvtt_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-webvtt-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-webvtt COMMAND cvk-cast-webvtt-test)

    add_executable(cvk-cast-srt-test "${PROJECT_SOURCE_DIR}/tests/cast_srt_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-srt-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-srt COMMAND cvk-cast-srt-test)

    add_executable(cvk-cast-mailbox-test "${PROJECT_SOURCE_DIR}/tests/cast_mailbox_test.c"
        os/win32/os_entry.c cast_media.c)
    target_link_libraries(cvk-cast-mailbox-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-mailboxes COMMAND cvk-cast-mailbox-test)

    add_executable(cvk-cast-ssdp-test "${PROJECT_SOURCE_DIR}/tests/cast_ssdp_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-ssdp-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-ssdp COMMAND cvk-cast-ssdp-test)

    add_executable(cvk-cast-url-test "${PROJECT_SOURCE_DIR}/tests/cast_url_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-url-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-url COMMAND cvk-cast-url-test)

    add_executable(cvk-cast-http-policy-test "${PROJECT_SOURCE_DIR}/tests/cast_http_policy_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-http-policy-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-http-policy COMMAND cvk-cast-http-policy-test)

    add_executable(cvk-cast-xml-test "${PROJECT_SOURCE_DIR}/tests/cast_xml_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-xml-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-xml COMMAND cvk-cast-xml-test)

    add_executable(cvk-cast-atomic-test "${PROJECT_SOURCE_DIR}/tests/win32/cast_atomic_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-atomic-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-atomics COMMAND cvk-cast-atomic-test)

    add_executable(cvk-cast-events-test "${PROJECT_SOURCE_DIR}/tests/win32/cast_events_test.c"
        os/win32/os_entry.c cast_events.c cast_media.c)
    set_source_files_properties("${PROJECT_SOURCE_DIR}/tests/win32/cast_events_test.c"
        PROPERTIES COMPILE_DEFINITIONS "WIN32_LEAN_AND_MEAN")
    target_link_libraries(cvk-cast-events-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-events COMMAND cvk-cast-events-test)

    add_executable(cvk-cast-crt-test "${PROJECT_SOURCE_DIR}/tests/cast_crt_test.c" os/win32/os_entry.c)
    target_link_libraries(cvk-cast-crt-test PRIVATE cast-no-crt)
    target_link_options(cvk-cast-crt-test PRIVATE
        "/MAP:$<TARGET_FILE_DIR:cvk-cast-crt-test>/cvk-cast-crt-test.map")
    add_test(NAME cvk-cast-crt COMMAND cvk-cast-crt-test)

    add_executable(cvk-cast-crt-guard-test "${PROJECT_SOURCE_DIR}/tests/win32/cast_crt_guard_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-crt-guard-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-crt-guards COMMAND cvk-cast-crt-guard-test)

    add_executable(cvk-cast-crt-abi-test "${PROJECT_SOURCE_DIR}/tests/msvc/x64/cast_crt_abi_test.c"
        os/win32/os_entry.c)
    target_link_libraries(cvk-cast-crt-abi-test PRIVATE cast-no-crt)
    target_link_options(cvk-cast-crt-abi-test PRIVATE
        "/MAP:$<TARGET_FILE_DIR:cvk-cast-crt-abi-test>/cvk-cast-crt-abi-test.map")
    add_test(NAME cvk-cast-crt-abi COMMAND cvk-cast-crt-abi-test)

    add_executable(cvk-cast-queue-test "${PROJECT_SOURCE_DIR}/tests/cast_queue_test.c"
        os/win32/os_entry.c cast_commands.c)
    target_link_libraries(cvk-cast-queue-test PRIVATE cast-no-crt)
    target_link_options(cvk-cast-queue-test PRIVATE
        "/MAP:$<TARGET_FILE_DIR:cvk-cast-queue-test>/cvk-cast-queue-test.map")
    add_test(NAME cvk-cast-queues COMMAND cvk-cast-queue-test)

    add_executable(cvk-cast-format-test "${PROJECT_SOURCE_DIR}/tests/cast_format_test.c" os/win32/os_entry.c)
    target_link_libraries(cvk-cast-format-test PRIVATE cast-no-crt)
    target_link_options(cvk-cast-format-test PRIVATE
        "/MAP:$<TARGET_FILE_DIR:cvk-cast-format-test>/cvk-cast-format-test.map")
    add_test(NAME cvk-cast-formats COMMAND cvk-cast-format-test)

    add_executable(cvk-cast-protocol-test "${PROJECT_SOURCE_DIR}/tests/cast_protocol_test.c" os/win32/os_entry.c)
    target_link_libraries(cvk-cast-protocol-test PRIVATE cast-no-crt)
    target_link_options(cvk-cast-protocol-test PRIVATE
        "/MAP:$<TARGET_FILE_DIR:cvk-cast-protocol-test>/cvk-cast-protocol-test.map")
    add_test(NAME cvk-cast-protocols COMMAND cvk-cast-protocol-test)

    add_executable(cvk-cast-http-test "${PROJECT_SOURCE_DIR}/tests/cast_http_test.c" os/win32/os_entry.c)
    target_link_libraries(cvk-cast-http-test PRIVATE cast-no-crt)
    target_link_options(cvk-cast-http-test PRIVATE
        "/MAP:$<TARGET_FILE_DIR:cvk-cast-http-test>/cvk-cast-http-test.map")
    add_test(NAME cvk-cast-http COMMAND cvk-cast-http-test)

    add_executable(cvk-cast-arguments-test "${PROJECT_SOURCE_DIR}/tests/cast_arguments_test.c" os/win32/os_entry.c)
    target_link_libraries(cvk-cast-arguments-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-arguments COMMAND cvk-cast-arguments-test)

    # Exercise the portable POSIX argument reader independently on this test host.
    add_executable(cvk-cast-arguments-posix-test
        "${PROJECT_SOURCE_DIR}/tests/cast_arguments_posix_test.c"
        utl/posix/utl_arguments.c os/win32/os_entry.c)
    target_link_libraries(cvk-cast-arguments-posix-test PRIVATE cast-no-crt)
    add_test(NAME cvk-cast-arguments-posix COMMAND cvk-cast-arguments-posix-test)

    add_executable(cvk-cast-assert-test "${PROJECT_SOURCE_DIR}/tests/cast_assert_test.c"
        os/win32/os_entry.c cast_commands.c cast_events.c)
    target_link_libraries(cvk-cast-assert-test PRIVATE cast-no-crt)
    if(Python3_Interpreter_FOUND)
        add_test(NAME cvk-cast-api-conventions COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/scripts/audit_cast.py"
            --report "${CMAKE_CURRENT_BINARY_DIR}/cast-api-report.json" --strict-api)
        add_test(NAME cvk-cast-assertions COMMAND ${Python3_EXECUTABLE}
            "${PROJECT_SOURCE_DIR}/tests/check_cast_assertions.py" $<TARGET_FILE:cvk-cast-assert-test>)
    endif()
endif()

foreach(target cast-session CastDemo CastDemoCli cvk-cast-message-test cvk-cast-subtitle-test cvk-cast-audio-test cvk-cast-control-test)
    if(NOT MSVC)
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endforeach()

# The root project applies the required build policy to all owned targets.
