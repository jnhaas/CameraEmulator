#pragma once

#include <windows.h>

// Shared memory used to pass screen captures from CameraEmulator.exe (user session) to the media
// source (Frame Server service, session 0).
//
// Standard users cannot create objects in the Global namespace, but the service can: the source
// creates the section and the app opens it. When the source runs outside the service (test
// harnesses), creating the Global section fails and both sides fall back to the Local name.
//
// Layout: a ScreenHeader, then BGRX pixels (top-down, stride = width * 4) at kScreenHeaderSize.
// The writer makes `sequence` odd while it updates the frame (seqlock), so readers can detect
// torn frames and skip them.
namespace CameraEmulator
{
    constexpr wchar_t kScreenSectionGlobal[] = L"Global\\CameraEmulatorScreen";
    constexpr wchar_t kScreenSectionLocal[] = L"Local\\CameraEmulatorScreen";

    // SYSTEM, LOCAL SERVICE and administrators: full access; interactive users: read/write.
    // Low mandatory label so the medium-integrity app can write to it.
    constexpr wchar_t kScreenSectionSddl[] = L"D:(A;;GA;;;SY)(A;;GA;;;LS)(A;;GA;;;BA)(A;;GRGW;;;IU)S:(ML;;NW;;;LW)";

    constexpr UINT32 kScreenMagic = 0x534D4543; // "CEMS"
    constexpr UINT32 kScreenMaxWidth = 3840;
    constexpr UINT32 kScreenMaxHeight = 2160;
    constexpr SIZE_T kScreenHeaderSize = 4096;
    constexpr SIZE_T kScreenSectionSize = kScreenHeaderSize + SIZE_T{kScreenMaxWidth} * kScreenMaxHeight * 4;

    // The feed counts as stopped when no frame was written for this long.
    constexpr ULONGLONG kScreenStaleMs = 2000;

    struct ScreenHeader
    {
        UINT32 magic;
        volatile LONG sequence;
        UINT32 width;
        UINT32 height;
        volatile ULONGLONG lastUpdateTick; // GetTickCount64, which is system-wide
    };

    inline BYTE* ScreenPixels(void* view)
    {
        return static_cast<BYTE*>(view) + kScreenHeaderSize;
    }
}
