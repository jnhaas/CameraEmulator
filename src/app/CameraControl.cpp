#include "CameraControl.h"
#include "ScreenCapture.h"
#include "Shared.h"

#include <mfapi.h>
#include <mfidl.h>
#include <cwctype>

using Microsoft::WRL::ComPtr;

namespace
{
    constexpr wchar_t kImageNameKey[] = L"imageName";
    constexpr wchar_t kInstalledKey[] = L"installed";

    HRESULT EnsureSharedFolder()
    {
        const std::wstring folder = CameraEmulator::SharedFolder();
        if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }
}

namespace CameraControl
{
    std::wstring ReadSetting(const wchar_t* section, const wchar_t* key, const wchar_t* defaultValue)
    {
        wchar_t value[1024] = {};
        GetPrivateProfileStringW(section, key, defaultValue, value, ARRAYSIZE(value), CameraEmulator::SharedSettingsPath().c_str());
        return value;
    }

    HRESULT WriteSetting(const wchar_t* section, const wchar_t* key, const wchar_t* value)
    {
        HRESULT hr = EnsureSharedFolder();
        if (FAILED(hr)) return hr;
        if (!WritePrivateProfileStringW(section, key, value, CameraEmulator::SharedSettingsPath().c_str()))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }

    // Copies the image into the folder read by the media source. The replace is atomic so the
    // source never decodes a half-written file.
    HRESULT PublishImage(const wchar_t* path)
    {
        HRESULT hr = EnsureSharedFolder();
        if (FAILED(hr)) return hr;

        const std::wstring temp = CameraEmulator::SharedFolder() + L"\\image.tmp";
        if (!CopyFileW(path, temp.c_str(), FALSE)) return HRESULT_FROM_WIN32(GetLastError());
        if (!MoveFileExW(temp.c_str(), CameraEmulator::SharedImagePath().c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            DeleteFileW(temp.c_str());
            return hr;
        }

        const wchar_t* name = wcsrchr(path, L'\\');
        const wchar_t* altName = wcsrchr(path, L'/');
        if (altName > name) name = altName;
        WriteSetting(CameraEmulator::kSettingsSection, kImageNameKey, name ? name + 1 : path);
        return S_OK;
    }

    std::wstring PublishedImageName()
    {
        if (GetFileAttributesW(CameraEmulator::SharedImagePath().c_str()) == INVALID_FILE_ATTRIBUTES) return L"";
        std::wstring name = ReadSetting(CameraEmulator::kSettingsSection, kImageNameKey);
        return name.empty() ? L"image.png" : name;
    }

    HRESULT PublishLayout(const wchar_t* layout)
    {
        return WriteSetting(CameraEmulator::kSettingsSection, CameraEmulator::kLayoutKey, layout);
    }

    bool AndroidLayout()
    {
        return _wcsicmp(ReadSetting(CameraEmulator::kSettingsSection, CameraEmulator::kLayoutKey, CameraEmulator::kLayoutAndroid).c_str(),
                   CameraEmulator::kLayoutFit) != 0;
    }

    bool ParseRegion(const wchar_t* text, RECT& region)
    {
        int x = 0, y = 0, width = 0, height = 0;
        wchar_t extra = 0;
        if (swscanf_s(text, L"%d,%d,%d,%d%c", &x, &y, &width, &height, &extra, 1) == 4 && width > 0 && height > 0)
        {
            region = RECT{x, y, x + width, y + height};
            return true;
        }
        if (swscanf_s(text, L"%dx%d%c", &width, &height, &extra, 1) == 2 && width > 0 && height > 0)
        {
            region = CenteredRegion(width, height);
            return true;
        }
        return false;
    }

    std::wstring FormatRegion(const RECT& region)
    {
        return std::to_wstring(region.left) + L"," + std::to_wstring(region.top) + L"," +
               std::to_wstring(region.right - region.left) + L"," + std::to_wstring(region.bottom - region.top);
    }

    bool ParseHotkey(const wchar_t* text, Hotkey& hotkey)
    {
        Hotkey result{0, 0, L""};
        std::wstring remaining = text;
        while (!remaining.empty())
        {
            const size_t plus = remaining.find(L'+');
            std::wstring token = remaining.substr(0, plus);
            remaining = plus == std::wstring::npos ? L"" : remaining.substr(plus + 1);
            if (token.empty()) return false;
            if (!result.text.empty()) result.text += L"+";

            if (_wcsicmp(token.c_str(), L"ctrl") == 0) { result.modifiers |= MOD_CONTROL; result.text += L"Ctrl"; }
            else if (_wcsicmp(token.c_str(), L"alt") == 0) { result.modifiers |= MOD_ALT; result.text += L"Alt"; }
            else if (_wcsicmp(token.c_str(), L"shift") == 0) { result.modifiers |= MOD_SHIFT; result.text += L"Shift"; }
            else if (_wcsicmp(token.c_str(), L"win") == 0) { result.modifiers |= MOD_WIN; result.text += L"Win"; }
            else if (result.key != 0) return false; // only one key
            else if (token.size() == 1 && iswalnum(token[0]))
            {
                result.key = towupper(token[0]);
                result.text += static_cast<wchar_t>(result.key);
            }
            else if ((token[0] == L'f' || token[0] == L'F') && token.size() <= 3)
            {
                const int number = _wtoi(token.c_str() + 1);
                if (number < 1 || number > 24) return false;
                result.key = VK_F1 + number - 1;
                result.text += L"F" + std::to_wstring(number);
            }
            else return false;
        }
        // A bare key would be swallowed system-wide: require at least one modifier.
        if (result.key == 0 || result.modifiers == 0) return false;
        hotkey = result;
        return true;
    }

    bool VirtualCamerasSupported()
    {
        BOOL supported = FALSE;
        return SUCCEEDED(MFIsVirtualCameraTypeSupported(MFVirtualCameraType_SoftwareCameraSource, &supported)) && supported;
    }

    HRESULT CreateCamera(MFVirtualCameraLifetime lifetime, ComPtr<IMFVirtualCamera>& camera)
    {
        return MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, lifetime, MFVirtualCameraAccess_CurrentUser,
            CAMERAEMULATOR_FRIENDLY_NAME, CAMERAEMULATOR_CLSID_STRING, nullptr, 0, &camera);
    }

    bool CameraExists()
    {
        ComPtr<IMFAttributes> query;
        if (FAILED(MFCreateAttributes(&query, 1)) ||
            FAILED(query->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID)))
        {
            return false;
        }
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        if (FAILED(MFEnumDeviceSources(query.Get(), &devices, &count))) return false;

        bool found = false;
        const size_t prefixLength = wcslen(CAMERAEMULATOR_FRIENDLY_NAME);
        for (UINT32 i = 0; i < count; ++i)
        {
            wchar_t* name = nullptr;
            UINT32 length = 0;
            // Windows appends a localized suffix to virtual cameras, e.g. "CameraEmulator (Windows Virtual Camera)".
            if (SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length)) &&
                _wcsnicmp(name, CAMERAEMULATOR_FRIENDLY_NAME, prefixLength) == 0 &&
                (name[prefixLength] == L'\0' || wcsncmp(name + prefixLength, L" (", 2) == 0))
            {
                found = true;
            }
            CoTaskMemFree(name);
            devices[i]->Release();
        }
        CoTaskMemFree(devices);
        return found;
    }

    HRESULT InstallCamera()
    {
        ComPtr<IMFVirtualCamera> camera;
        HRESULT hr = CreateCamera(MFVirtualCameraLifetime_System, camera);
        if (FAILED(hr)) return hr;
        hr = camera->Start(nullptr);
        camera->Shutdown();
        if (SUCCEEDED(hr)) WriteSetting(CameraEmulator::kSettingsSection, kInstalledKey, L"1");
        return hr;
    }

    HRESULT UninstallCamera()
    {
        ComPtr<IMFVirtualCamera> camera;
        HRESULT hr = CreateCamera(MFVirtualCameraLifetime_System, camera);
        if (FAILED(hr)) return hr;
        hr = camera->Remove();
        camera->Shutdown();
        if (SUCCEEDED(hr)) WriteSetting(CameraEmulator::kSettingsSection, kInstalledKey, L"0");
        return hr;
    }

    bool IsInstalled()
    {
        return ReadSetting(CameraEmulator::kSettingsSection, kInstalledKey, L"0") == L"1";
    }
}
