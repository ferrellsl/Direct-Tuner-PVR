cmake_minimum_required(VERSION 3.16)
project(pvr.directtuner LANGUAGES CXX VERSION 21.0.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(KODI_INCLUDE_DIR "" CACHE PATH
    "kodi-dev-kit include folder, from the Kodi version the add-on runs on")
if(NOT EXISTS "${KODI_INCLUDE_DIR}/kodi/AddonBase.h")
  message(FATAL_ERROR "Set KODI_INCLUDE_DIR to xbmc/addons/kodi-dev-kit/include in a Kodi 21 source tree")
endif()

add_library(directtuner_core STATIC
  src/core/Common.cpp
  src/core/Device.cpp
  src/core/Guide.cpp
  src/core/Json.cpp
  src/core/Net.cpp
  src/core/Pump.cpp
  src/core/RecordingStore.cpp
  src/core/Scheduler.cpp)
set_target_properties(directtuner_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(WIN32)
  target_compile_definitions(directtuner_core PUBLIC
    TARGET_WINDOWS NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
  target_link_libraries(directtuner_core PUBLIC ws2_32 iphlpapi)
  add_compile_options(/utf-8 /W3)
endif()

add_library(pvr.directtuner SHARED src/addon/Addon.cpp src/addon/PvrClient.cpp)
target_include_directories(pvr.directtuner PRIVATE ${KODI_INCLUDE_DIR})
target_compile_definitions(pvr.directtuner PRIVATE
  BUILD_KODI_ADDON
  ADDON_GLOBAL_VERSION_MAIN_USED
  ADDON_GLOBAL_VERSION_GENERAL_USED
  ADDON_GLOBAL_VERSION_FILESYSTEM_USED
  ADDON_GLOBAL_VERSION_TOOLS_USED
  ADDON_INSTANCE_VERSION_PVR_USED)
target_link_libraries(pvr.directtuner PRIVATE directtuner_core)
set_target_properties(pvr.directtuner PROPERTIES PREFIX "")

add_executable(directtuner_tests tests/CoreTests.cpp)
target_link_libraries(directtuner_tests PRIVATE directtuner_core)

add_executable(directtuner_tool tests/TunerTool.cpp)
target_link_libraries(directtuner_tool PRIVATE directtuner_core)

set(ADDON_VERSION ${PROJECT_VERSION})
set(ADDON_LIBRARY pvr.directtuner.dll)
configure_file(pvr.directtuner/addon.xml.in ${CMAKE_BINARY_DIR}/addon.xml @ONLY)
