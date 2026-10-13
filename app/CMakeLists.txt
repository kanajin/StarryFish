add_library(gbrowser_core STATIC
  src/core.cpp
  src/backup.cpp
  src/backup_file.cpp
  src/backup_data.cpp
  src/backup_restore.cpp
  src/backup_capture.cpp
  src/storage_probe.cpp
  src/profile_stage.cpp
  src/component_update.cpp
  src/profile_acl.cpp
  src/job_observation.cpp
  src/backup_preview.cpp
  src/browser_settings.cpp
  src/backup_rekey.cpp
  src/proxy_preset.cpp
  src/proxy_initialization.cpp
  src/extension_page.cpp
  src/tarou_pane.cpp
  src/file_digest.cpp
  src/gbf_policy.cpp
  src/application_layout.cpp)
target_include_directories(gbrowser_core PUBLIC src PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_compile_features(gbrowser_core PUBLIC cxx_std_17)
target_compile_definitions(gbrowser_core PUBLIC UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN GBROWSER_HOST_FINGERPRINT=${GBROWSER_HOST_FINGERPRINT})
target_compile_options(gbrowser_core PRIVATE /utf-8 /W4 /WX /EHsc)
set_property(TARGET gbrowser_core PROPERTY MSVC_RUNTIME_LIBRARY MultiThreaded)
target_link_libraries(gbrowser_core PUBLIC bcrypt shell32 ole32)
