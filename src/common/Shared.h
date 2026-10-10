#pragma once

#include <windows.h>
#include <shlobj.h>
#include <string>

// CLSID of the media source activator. Both forms must stay in sync.
#define CAMERAEMULATOR_CLSID_UUID   "9C5E4B7A-3D21-4F8E-A6B0-1E7C52D9F3A4"
#define CAMERAEMULATOR_CLSID_STRING L"{9C5E4B7A-3D21-4F8E-A6B0-1E7C52D9F3A4}"

#define CAMERAEMULATOR_FRIENDLY_NAME L"CameraEmulator"

namespace CameraEmulator
{
    // Folder shared between the control app and the Frame Server service. The service runs
    // as LOCAL SERVICE and cannot read the user's profile, so we use %ProgramData%.
    inline std::wstring SharedFolder()
    {
        std::wstring folder = L"C:\\ProgramData";
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_ProgramData, 0, nullptr, &path)))
        {
            folder = path;
        }
        CoTaskMemFree(path);
        return folder + L"\\CameraEmulator";
    }

    // The image streamed by the virtual camera. Replacing it hot-swaps the picture.
    inline std::wstring SharedImagePath()
    {
        return SharedFolder() + L"\\image.png";
    }

    // INI file with live settings, re-read by the source along with the image.
    inline std::wstring SharedSettingsPath()
    {
        return SharedFolder() + L"\\settings.ini";
    }

    constexpr wchar_t kSettingsSection[] = L"camera";
    constexpr wchar_t kLayoutKey[] = L"layout";

    // Used by the apps only (not the source).
    constexpr wchar_t kScreenSection[] = L"screen";
    constexpr wchar_t kLastRegionKey[] = L"lastRegion"; // x,y,w,h of the last broadcast zone
    constexpr wchar_t kStopKeyKey[] = L"stopKey";       // e.g. ctrl+alt+x
    constexpr wchar_t kStartKeyKey[] = L"startKey";     // tray app only, e.g. ctrl+alt+c

    // How the image is placed in the frame.
    //   android: fit into the region the Android emulator keeps when it rotates webcam frames
    //            for a portrait device (a left-anchored crop of H*H/W x H pixels), so the whole
    //            image shows up in the emulated camera.
    //   fit:     letterbox into the whole frame, for regular desktop apps.
    constexpr wchar_t kLayoutAndroid[] = L"android";
    constexpr wchar_t kLayoutFit[] = L"fit";
}
