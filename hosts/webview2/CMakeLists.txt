cmake_minimum_required(VERSION 3.21)
project(GbrowserWebView2 LANGUAGES CXX RC)
if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Native Windows x64 required")
endif()
if(NOT GBROWSER_HOST_FINGERPRINT MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Use build.py; host inputs must have a content fingerprint")
endif()
add_subdirectory(../../app shared-core)
add_executable(GbrowserWebView2 WIN32 src/starryfish.rc src/profile_dialog.cpp src/shortcut_dialog.cpp src/main.cpp src/directory_pin.cpp src/component_catalogue.cpp src/user_state.cpp src/profile_catalogue.cpp src/proxy_preference.cpp)
add_executable(NativeSourceChecks checks/directory_pin.cpp src/directory_pin.cpp src/component_catalogue.cpp src/user_state.cpp src/profile_catalogue.cpp src/proxy_preference.cpp)
target_sources(NativeSourceChecks PRIVATE checks/dual_transition.cpp checks/shutdown.cpp checks/character_shortcuts.cpp)
add_executable(UiThemePreview checks/ui_theme_preview.cpp src/shortcut_dialog.cpp)
target_sources(GbrowserWebView2 PRIVATE src/resource_cache.cpp src/profile_transfer.cpp src/profile_transfer_browser.cpp)
target_sources(NativeSourceChecks PRIVATE src/profile_transfer.cpp checks/profile_transfer.cpp)
target_sources(GbrowserWebView2 PRIVATE src/network_capture.cpp src/capture_mcp.cpp)
target_sources(NativeSourceChecks PRIVATE src/network_capture.cpp src/capture_mcp.cpp checks/network_capture.cpp)
target_sources(NativeSourceChecks PRIVATE src/resource_cache.cpp checks/resource_cache.cpp)
add_executable(ProfileTransferRuntimeChecks src/profile_catalogue.cpp src/directory_pin.cpp checks/profile_transfer_runtime.cpp src/profile_transfer.cpp src/profile_transfer_browser.cpp src/user_state.cpp src/proxy_preference.cpp)
foreach(target GbrowserWebView2 NativeSourceChecks UiThemePreview ProfileTransferRuntimeChecks)
  target_compile_features(${target} PRIVATE cxx_std_17)
  target_compile_options(${target} PRIVATE /utf-8 /W4 /WX /EHsc /cgthreads1)
  target_compile_definitions(${target} PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN
    GBROWSER_HOST_FINGERPRINT=${GBROWSER_HOST_FINGERPRINT})
  set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
  target_include_directories(${target} PRIVATE src ../../app/src .sdk/1.0.4258.31 "${GBROWSER_GENERATED_DIR}")
  target_link_libraries(${target} PRIVATE
    gbrowser_core
    bcrypt shell32 ole32 oleaut32 user32 comctl32 version advapi32 shlwapi crypt32 winhttp windowsapp)
endforeach()
target_link_libraries(UiThemePreview PRIVATE gdiplus dwmapi)
target_link_libraries(GbrowserWebView2 PRIVATE
  gdiplus dwmapi
  "${CMAKE_CURRENT_SOURCE_DIR}/.sdk/1.0.4258.31/WebView2LoaderStatic.lib")

set_target_properties(GbrowserWebView2 PROPERTIES OUTPUT_NAME StarryFish)

target_link_libraries(ProfileTransferRuntimeChecks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/.sdk/1.0.4258.31/WebView2LoaderStatic.lib")

target_compile_definitions(ProfileTransferRuntimeChecks PRIVATE STARRYFISH_TRANSFER_TEST_TRACE)
