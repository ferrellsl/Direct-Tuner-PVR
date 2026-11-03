cmake_minimum_required(VERSION 3.16)
project(pvr.directtuner LANGUAGES CXX VERSION 21.2.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(KODI_INCLUDE_DIR "" CACHE PATH
    "kodi-dev-kit include folder, from the Kodi version the add-on runs on")
if(NOT EXISTS "${KODI_INCLUDE_DIR}/kodi/AddonBase.h")
  message(FATAL_ERROR "Set KODI_INCLUDE_DIR to xbmc/addons/kodi-dev-kit/include in a Kodi 21 source tree")
endif()

add_library(directtuner_core STATIC
  src/core/Backend.cpp
  src/core/Common.cpp
  src/core/Device.cpp
  src/core/Guide.cpp
  src/core/Json.cpp
  src/core/Net.cpp
  src/core/Pump.cpp
  src/core/RecordingStore.cpp
  src/core/RemoteBackend.cpp
  src/core/Scheduler.cpp
  src/core/Wire.cpp)
set_target_properties(directtuner_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(WIN32)
  target_compile_definitions(directtuner_core PUBLIC
    TARGET_WINDOWS NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
  target_link_libraries(directtuner_core PUBLIC ws2_32 iphlpapi)
  add_compile_options(/utf-8 /W3)
else()
  find_package(Threads REQUIRED)
  target_compile_definitions(directtuner_core PUBLIC _FILE_OFFSET_BITS=64)
  target_link_libraries(directtuner_core PUBLIC Threads::Threads)
  add_compile_options(-Wall -fvisibility=hidden)
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

# Direct Tuner Recorder, the service. Desktop systems only; its source is published separately.
if(NOT ANDROID AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/service)
  get_target_property(CORE_SOURCES directtuner_core SOURCES)
  add_executable(DirectTunerRecorder
    ${CORE_SOURCES}
    src/service/Fetch.cpp
    src/service/HttpServer.cpp
    src/service/Main.cpp
    src/service/Recorder.cpp)
  if(WIN32)
    target_compile_definitions(DirectTunerRecorder PRIVATE
      TARGET_WINDOWS NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
    target_link_libraries(DirectTunerRecorder PRIVATE ws2_32 iphlpapi winhttp advapi32)
    # One file to install: the C++ runtime goes inside the program.
    set_target_properties(DirectTunerRecorder PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded")
  else()
    target_compile_definitions(DirectTunerRecorder PRIVATE _FILE_OFFSET_BITS=64)
    target_link_libraries(DirectTunerRecorder PRIVATE Threads::Threads)
  endif()
endif()

set(ADDON_VERSION ${PROJECT_VERSION})
if(WIN32)
  set(ADDON_PLATFORM windx)
  set(ADDON_LIBRARY pvr.directtuner.dll)
elseif(ANDROID)
  # The APK only carries native code named lib*.so.
  set(ADDON_PLATFORM android)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64")
    set(ADDON_PLATFORM_TAG android-aarch64)
  else()
    set(ADDON_PLATFORM_TAG android-armv7)
  endif()
  set_target_properties(pvr.directtuner PROPERTIES PREFIX "lib")
  set(ADDON_LIBRARY libpvr.directtuner.so)
else()
  set(ADDON_PLATFORM linux)
  set(ADDON_LIBRARY pvr.directtuner.so)
endif()
if(NOT ADDON_PLATFORM_TAG)
  set(ADDON_PLATFORM_TAG ${ADDON_PLATFORM})
endif()
install(TARGETS pvr.directtuner LIBRARY DESTINATION lib/kodi/addons/pvr.directtuner
        RUNTIME DESTINATION lib/kodi/addons/pvr.directtuner)
install(FILES ${CMAKE_BINARY_DIR}/addon.xml pvr.directtuner/icon.png LICENSE.md
        DESTINATION share/kodi/addons/pvr.directtuner)
install(DIRECTORY pvr.directtuner/resources DESTINATION share/kodi/addons/pvr.directtuner)
configure_file(pvr.directtuner/addon.xml.in ${CMAKE_BINARY_DIR}/addon.xml @ONLY)
