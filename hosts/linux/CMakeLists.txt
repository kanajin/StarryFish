cmake_minimum_required(VERSION 3.21)
project(StarryFishLinux LANGUAGES CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8
   OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64)$")
  message(FATAL_ERROR "StarryFish Linux host requires Linux x86_64")
endif()

if(NOT STARRYFISH_INPUT_FINGERPRINT MATCHES "^[0-9a-f]+$")
  message(FATAL_ERROR "Use python -B build.py build; a source fingerprint is required")
endif()
if(NOT IS_ABSOLUTE "${STARRYFISH_OUTPUT_DIR}")
  message(FATAL_ERROR "build.py must supply an absolute STARRYFISH_OUTPUT_DIR")
endif()
set(USE_SANDBOX ON CACHE BOOL "Chromium sandbox is required" FORCE)

set(CEF_ROOT "" CACHE PATH "Path to an official extracted CEF standard distribution")
if(NOT CEF_ROOT OR NOT EXISTS "${CEF_ROOT}/CMakeLists.txt")
  message(FATAL_ERROR "Set CEF_ROOT to an extracted official CEF Linux standard distribution")
endif()

list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)
add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" libcef_dll_wrapper)
ADD_LOGICAL_TARGET(libcef_lib "${CEF_LIB_DEBUG}" "${CEF_LIB_RELEASE}")
SET_CEF_TARGET_OUT_DIR()

add_executable(starryfish-linux src/main.cpp src/platform_paths.cpp)
SET_EXECUTABLE_TARGET_PROPERTIES(starryfish-linux)
add_dependencies(starryfish-linux libcef_dll_wrapper)
target_compile_features(starryfish-linux PRIVATE cxx_std_17)
target_compile_options(starryfish-linux PRIVATE -Wall -Wextra -Werror)
target_compile_definitions(starryfish-linux PRIVATE CEF_USE_SANDBOX)
target_link_libraries(starryfish-linux PRIVATE libcef_lib libcef_dll_wrapper ${CEF_STANDARD_LIBS})

# Keep the CEF shared libraries beside the executable and resolve them through
# $ORIGIN. Keep the helper as an ordinary file; no privileged build steps.
set_target_properties(starryfish-linux PROPERTIES
  OUTPUT_NAME StarryFish
  INSTALL_RPATH "$ORIGIN"
  BUILD_WITH_INSTALL_RPATH TRUE
  RUNTIME_OUTPUT_DIRECTORY "${STARRYFISH_OUTPUT_DIR}")

set(CEF_TARGET_OUT_DIR "${STARRYFISH_OUTPUT_DIR}")

COPY_FILES(starryfish-linux "${CEF_BINARY_FILES}" "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
COPY_FILES(starryfish-linux "${CEF_RESOURCE_FILES}" "${CEF_RESOURCE_DIR}" "${CEF_TARGET_OUT_DIR}")
if(EXISTS "${CEF_BINARY_DIR}/libminigbm.so")
  COPY_FILES(starryfish-linux libminigbm.so "${CEF_BINARY_DIR}" "${CEF_TARGET_OUT_DIR}")
endif()
# Builds never sudo/chown/chmod a setuid helper. CachyOS namespace sandbox
# support must be checked at runtime. An unsupported sandbox is a blocker.

enable_testing()
add_executable(LinuxPlatformChecks checks/platform_paths.cpp src/platform_paths.cpp)
set_target_properties(LinuxPlatformChecks PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${STARRYFISH_OUTPUT_DIR}/checks")
target_include_directories(LinuxPlatformChecks PRIVATE src)
target_compile_features(LinuxPlatformChecks PRIVATE cxx_std_17)
target_compile_options(LinuxPlatformChecks PRIVATE -Wall -Wextra -Werror)
add_test(NAME LinuxPlatformChecks COMMAND LinuxPlatformChecks)
