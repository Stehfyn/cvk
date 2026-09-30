# Owned graphics code retains the same analysis policy as the rest of CastDemo.
# The pinned CVK/cva/cdx dependencies provide native API operations; only cva/cdx
# and their third-party headers receive the external warning policy.
add_library(gfxlib STATIC
    src/gfx/gfx_encoder.c
    src/gfx/gfx_image.c
    src/gfx/gfx_image_layout.c
    src/gfx/gfx_renderer.c
    src/gfx/gfx_surface.c
    src/gfx/gfx_swapchain.c)
add_library(gfx::gfx ALIAS gfxlib)
target_include_directories(gfxlib
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(gfxlib PUBLIC cvk cva::cva volk::volk PRIVATE cast-formats crt)

if(WIN32)
    set(gfx_native_directory win32)
    target_compile_definitions(gfxlib PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN CINTERFACE COBJMACROS VK_USE_PLATFORM_WIN32_KHR)
    target_link_libraries(gfxlib PRIVATE cdxgi cd3d12 user32 gdi32 dxguid)
elseif(UNIX AND NOT APPLE)
    set(gfx_native_directory linux)
    find_package(X11 REQUIRED)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GFX_LIBDRM REQUIRED IMPORTED_TARGET libdrm)
    target_compile_definitions(gfxlib PRIVATE VK_USE_PLATFORM_XLIB_KHR)
    target_link_libraries(gfxlib PRIVATE X11::X11 PkgConfig::GFX_LIBDRM)
else()
    message(FATAL_ERROR "No gfxlib native implementation exists for this target")
endif()

foreach(source gfx_adapter gfx_display gfx_surface gfx_swapchain_platform gfx_image_platform)
    target_sources(gfxlib PRIVATE "src/gfx/${gfx_native_directory}/${source}.c")
endforeach()
