// CameraEmulator control app.
//
//   CameraEmulator.exe [image.png]           Publish the image (optional) and expose the virtual
//                                            camera until Ctrl+C is pressed.
//   CameraEmulator.exe --set-image image.png Replace the image of a running camera and exit.

#include <windows.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>

#include "Shared.h"

using Microsoft::WRL::ComPtr;

namespace
{
    HANDLE g_stopEvent = nullptr;

    BOOL WINAPI OnConsoleCtrl(DWORD)
    {
        SetEvent(g_stopEvent);
        return TRUE;
    }

    int Fail(const wchar_t* what, HRESULT hr)
    {
        fwprintf(stderr, L"%s failed: 0x%08X\n", what, static_cast<unsigned>(hr));
        return 1;
    }

    // Copies the image into the folder read by the media source. The replace is atomic so the
    // source never decodes a half-written file.
    HRESULT PublishImage(const wchar_t* source)
    {
        const std::wstring folder = CameraEmulator::SharedFolder();
        if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }

        const std::wstring temp = folder + L"\\image.tmp";
        const std::wstring target = CameraEmulator::SharedImagePath();
        if (!CopyFileW(source, temp.c_str(), FALSE)) return HRESULT_FROM_WIN32(GetLastError());
        if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            const HRESULT hr = HRESULT_FROM_WIN32(GetLastError());
            DeleteFileW(temp.c_str());
            return hr;
        }
        wprintf(L"Image published to %s\n", target.c_str());
        return S_OK;
    }

    int RunCamera()
    {
        BOOL supported = FALSE;
        if (FAILED(MFIsVirtualCameraTypeSupported(MFVirtualCameraType_SoftwareCameraSource, &supported)) || !supported)
        {
            fwprintf(stderr, L"Virtual cameras are not supported on this system (Windows 11 required).\n");
            return 1;
        }

        // Session lifetime: the camera disappears when this process exits.
        ComPtr<IMFVirtualCamera> camera;
        HRESULT hr = MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, MFVirtualCameraLifetime_Session,
            MFVirtualCameraAccess_CurrentUser, CAMERAEMULATOR_FRIENDLY_NAME, CAMERAEMULATOR_CLSID_STRING, nullptr, 0, &camera);
        if (FAILED(hr)) return Fail(L"MFCreateVirtualCamera", hr);

        hr = camera->Start(nullptr);
        if (FAILED(hr))
        {
            Fail(L"IMFVirtualCamera::Start", hr);
            fwprintf(stderr, L"Is CameraEmulatorSource.dll registered? Run 'regsvr32 CameraEmulatorSource.dll' as administrator.\n");
            camera->Shutdown();
            return 1;
        }

        wprintf(L"Virtual camera \"%s\" is running. Press Ctrl+C to stop.\n", CAMERAEMULATOR_FRIENDLY_NAME);
        WaitForSingleObject(g_stopEvent, INFINITE);

        wprintf(L"Removing virtual camera...\n");
        hr = camera->Remove();
        if (FAILED(hr)) Fail(L"IMFVirtualCamera::Remove", hr);
        camera->Shutdown();
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    const bool setImageOnly = argc == 3 && _wcsicmp(argv[1], L"--set-image") == 0;
    if (argc > 2 && !setImageOnly)
    {
        fwprintf(stderr, L"Usage:\n  %s [image.png]\n  %s --set-image image.png\n", argv[0], argv[0]);
        return 2;
    }

    const wchar_t* image = setImageOnly ? argv[2] : (argc == 2 ? argv[1] : nullptr);
    if (image)
    {
        const HRESULT hr = PublishImage(image);
        if (FAILED(hr)) return Fail(L"Publishing the image", hr);
    }
    else if (GetFileAttributesW(CameraEmulator::SharedImagePath().c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        wprintf(L"No image published yet; the camera will show color bars.\n");
    }
    if (setImageOnly) return 0;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return Fail(L"CoInitializeEx", hr);
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return Fail(L"MFStartup", hr);

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);

    const int result = RunCamera();

    MFShutdown();
    CoUninitialize();
    return result;
}
