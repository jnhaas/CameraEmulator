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
}
