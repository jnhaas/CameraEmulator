cmake_minimum_required(VERSION 3.20)
project(CameraEmulator LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "CameraEmulator only builds on Windows 11.")
endif()

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
)
target_include_directories(CameraEmulatorSource PRIVATE src/common)
target_link_libraries(CameraEmulatorSource PRIVATE mfplat mfuuid windowscodecs ole32 shell32 advapi32 runtimeobject)

# Control app: publishes the PNG and registers the virtual camera.
add_executable(CameraEmulator src/app/main.cpp src/app/CameraControl.cpp src/app/ScreenCapture.cpp)
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
add_executable(CameraEmulatorTray WIN32 src/tray/TrayApp.cpp src/app/CameraControl.cpp src/app/ScreenCapture.cpp)
target_include_directories(CameraEmulatorTray PRIVATE src/common src/app)
target_link_libraries(CameraEmulatorTray PRIVATE mf mfplat mfuuid mfsensorgroup ole32 shell32 user32 gdi32 shcore advapi32)
