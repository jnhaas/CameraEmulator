cmake_minimum_required(VERSION 3.20)

# Product version: change it here. The 4th part of the file version is the build number (the
# number of git commits), so every commit gets a higher file version.
project(CameraEmulator VERSION 1.0.0 LANGUAGES CXX RC)
set(CAMERAEMULATOR_COMPANY "FinalAnalytics")

if(NOT WIN32)
    message(FATAL_ERROR "CameraEmulator only builds on Windows 11.")
endif()

# Version details from git, refreshed each time CMake configures (tools\package.bat always does).
set(CAMERAEMULATOR_BUILD 0)
set(CAMERAEMULATOR_COMMIT "local")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE git_count OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE git_result ERROR_QUIET)
    if(git_result EQUAL 0)
        set(CAMERAEMULATOR_BUILD ${git_count})
    endif()
    execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE git_commit OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE git_result ERROR_QUIET)
    if(git_result EQUAL 0)
        set(CAMERAEMULATOR_COMMIT ${git_commit})
    endif()
endif()
set(CAMERAEMULATOR_FILE_VERSION "${PROJECT_VERSION}.${CAMERAEMULATOR_BUILD}")
set(CAMERAEMULATOR_PRODUCT_VERSION "${PROJECT_VERSION} (${CAMERAEMULATOR_COMMIT})")
string(TIMESTAMP CAMERAEMULATOR_YEAR "%Y")
message(STATUS "CameraEmulator version ${CAMERAEMULATOR_FILE_VERSION} (${CAMERAEMULATOR_COMMIT})")

set(GENERATED_DIR ${CMAKE_BINARY_DIR}/generated)
configure_file(src/common/Version.h.in ${GENERATED_DIR}/Version.h @ONLY)
# For tools\package.bat: names the zip file.
file(WRITE ${GENERATED_DIR}/version.txt "${CAMERAEMULATOR_FILE_VERSION}")

# Adds a VERSIONINFO resource to a binary (Explorer: Properties > Details).
function(add_version_info target description)
    get_target_property(type ${target} TYPE)
    if(type STREQUAL "SHARED_LIBRARY")
        set(VER_FILETYPE "VFT_DLL")
        set(VER_FILENAME "${target}.dll")
    else()
        set(VER_FILETYPE "VFT_APP")
        set(VER_FILENAME "${target}.exe")
    endif()
    set(VER_DESCRIPTION "${description}")
    set(VER_INTERNAL_NAME "${target}")
    set(VER_FILEFLAGS "0")
    configure_file(${CMAKE_SOURCE_DIR}/src/common/Version.rc.in ${GENERATED_DIR}/${target}.version.rc @ONLY)
    target_sources(${target} PRIVATE ${GENERATED_DIR}/${target}.version.rc)
endfunction()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: the source DLL is loaded by the Frame Server service, keep it self-contained.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_compile_definitions(UNICODE _UNICODE NOMINMAX)
add_compile_options(/W4 /permissive- /EHsc)

# Media Foundation media source, loaded by the Frame Server service.
add_library(CameraEmulatorSource SHARED
    src/source/dllmain.cpp
    src/source/Activator.cpp
    src/source/MediaSource.cpp
    src/source/MediaStream.cpp
    src/source/FrameGenerator.cpp
    src/source/ScreenFeed.cpp
    src/source/CameraEmulatorSource.def
    src/source/CameraEmulatorSource.rc
)
target_include_directories(CameraEmulatorSource PRIVATE src/common)
target_link_libraries(CameraEmulatorSource PRIVATE mfplat mfuuid windowscodecs ole32 shell32 advapi32 runtimeobject)

# Control app: publishes the PNG and registers the virtual camera.
add_executable(CameraEmulator src/app/main.cpp src/app/CameraControl.cpp src/app/ScreenCapture.cpp src/app/CameraEmulator.rc)
target_include_directories(CameraEmulator PRIVATE src/common)
target_link_libraries(CameraEmulator PRIVATE mf mfplat mfuuid mfsensorgroup ole32 shell32 user32 gdi32 shcore)

# Test harness: loads the source DLL directly and dumps a frame, no Frame Server involved.
add_executable(SourceTest tests/SourceTest.cpp)
target_include_directories(SourceTest PRIVATE src/common)
target_link_libraries(SourceTest PRIVATE mfplat mfuuid ole32 shell32)
add_dependencies(SourceTest CameraEmulatorSource)

# Reads the registered virtual camera like the Android emulator does (needs CameraEmulator.exe running).
add_executable(ReaderTest tests/ReaderTest.cpp)
target_link_libraries(ReaderTest PRIVATE mfplat mfuuid mfreadwrite mf ole32)

# Tray app: the same features as CameraEmulator.exe from a notification area icon, no console.
add_executable(CameraEmulatorTray WIN32 src/tray/TrayApp.cpp src/app/CameraControl.cpp src/app/ScreenCapture.cpp src/tray/CameraEmulatorTray.rc)
target_include_directories(CameraEmulatorTray PRIVATE src/common src/app)
target_link_libraries(CameraEmulatorTray PRIVATE mf mfplat mfuuid mfsensorgroup ole32 shell32 user32 gdi32 shcore advapi32)

# End-user help, opened from the tray menu: keep it next to the executables.
add_custom_command(TARGET CameraEmulatorTray POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/docs/CameraEmulator-Help.html $<TARGET_FILE_DIR:CameraEmulatorTray>)

# Version resources of the shipped binaries, and Version.h for the apps.
add_version_info(CameraEmulatorSource "CameraEmulator virtual camera source")
add_version_info(CameraEmulator "CameraEmulator command line")
add_version_info(CameraEmulatorTray "CameraEmulator")
target_include_directories(CameraEmulator PRIVATE ${GENERATED_DIR})
target_include_directories(CameraEmulatorTray PRIVATE ${GENERATED_DIR})
