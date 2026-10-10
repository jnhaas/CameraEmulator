// CameraEmulator control app.
//
//   CameraEmulator.exe [--layout android|fit] [--screen [x,y,w,h]] [image.png]
//       Apply the settings (optional) and expose the virtual camera until Ctrl+C is pressed.
//       --screen broadcasts a zone of the screen instead of the image: give it in physical
//       pixels, or omit it to drag a rectangle with the mouse.
//   CameraEmulator.exe --update [--layout android|fit] [--screen [x,y,w,h]] [image.png]
//       Change the settings of a running camera and exit. With --screen, keep broadcasting the
//       zone to that camera until Ctrl+C.
//   CameraEmulator.exe --set-image image.png
//       Same as --update image.png.

#include <windows.h>
#include <mfapi.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>
#include <cstdio>
#include <string>

#include "ScreenCapture.h"
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

    HRESULT EnsureSharedFolder()
    {
        const std::wstring folder = CameraEmulator::SharedFolder();
        if (!CreateDirectoryW(folder.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        return S_OK;
    }

    HRESULT PublishLayout(const wchar_t* layout)
    {
        HRESULT hr = EnsureSharedFolder();
        if (FAILED(hr)) return hr;
        if (!WritePrivateProfileStringW(CameraEmulator::kSettingsSection, CameraEmulator::kLayoutKey, layout,
                CameraEmulator::SharedSettingsPath().c_str()))
        {
            return HRESULT_FROM_WIN32(GetLastError());
        }
        wprintf(L"Layout set to %s\n", layout);
        return S_OK;
    }

    // Copies the image into the folder read by the media source. The replace is atomic so the
    // source never decodes a half-written file.
    HRESULT PublishImage(const wchar_t* source)
    {
        HRESULT hr = EnsureSharedFolder();
        if (FAILED(hr)) return hr;

        const std::wstring folder = CameraEmulator::SharedFolder();
        const std::wstring temp = folder + L"\\image.tmp";
        const std::wstring target = CameraEmulator::SharedImagePath();
        if (!CopyFileW(source, temp.c_str(), FALSE)) return HRESULT_FROM_WIN32(GetLastError());
        if (!MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING))
        {
            hr = HRESULT_FROM_WIN32(GetLastError());
            DeleteFileW(temp.c_str());
            return hr;
        }
        wprintf(L"Image published to %s\n", target.c_str());
        return S_OK;
    }

    // Waits for Ctrl+C while pumping messages for the windows of this thread (region border).
    void WaitForStop()
    {
        while (MsgWaitForMultipleObjects(1, &g_stopEvent, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
    }

    bool ParseRegion(const wchar_t* text, RECT& region)
    {
        int x = 0, y = 0, width = 0, height = 0;
        wchar_t extra = 0;
        if (swscanf_s(text, L"%d,%d,%d,%d%c", &x, &y, &width, &height, &extra, 1) != 4 || width <= 0 || height <= 0) return false;
        region = RECT{x, y, x + width, y + height};
        return true;
    }

    // Broadcasts the screen region, if any, until Ctrl+C.
    void RunUntilStopped(const RECT* screenRegion)
    {
        ScreenBroadcaster broadcaster;
        RegionBorder border;
        if (screenRegion)
        {
            if (!broadcaster.Start(*screenRegion))
            {
                fwprintf(stderr, L"Invalid screen region.\n");
                return;
            }
            border.Show(*screenRegion);
            wprintf(L"Screen region %ld,%ld,%ld,%ld will be broadcast while a client uses the camera.\n",
                screenRegion->left, screenRegion->top, screenRegion->right - screenRegion->left,
                screenRegion->bottom - screenRegion->top);
        }
        WaitForStop();
    }

    int RunCamera(const RECT* screenRegion)
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
        RunUntilStopped(screenRegion);

        wprintf(L"Removing virtual camera...\n");
        hr = camera->Remove();
        if (FAILED(hr)) Fail(L"IMFVirtualCamera::Remove", hr);
        camera->Shutdown();
        return 0;
    }
}

int wmain(int argc, wchar_t** argv)
{
    // Screen coordinates and captures in physical pixels on every monitor.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    bool updateOnly = false;
    const wchar_t* image = nullptr;
    const wchar_t* layout = nullptr;
    bool screen = false;
    bool screenRegionGiven = false;
    RECT screenRegion{};
    bool validArgs = true;
    for (int i = 1; i < argc && validArgs; ++i)
    {
        if (_wcsicmp(argv[i], L"--update") == 0)
        {
            updateOnly = true;
        }
        else if (_wcsicmp(argv[i], L"--screen") == 0 && !screen)
        {
            screen = true;
            if (i + 1 < argc && ParseRegion(argv[i + 1], screenRegion))
            {
                screenRegionGiven = true;
                ++i;
            }
        }
        else if (_wcsicmp(argv[i], L"--set-image") == 0 && i + 1 < argc && !image)
        {
            updateOnly = true;
            image = argv[++i];
        }
        else if (_wcsicmp(argv[i], L"--layout") == 0 && i + 1 < argc && !layout)
        {
            layout = argv[++i];
            validArgs = _wcsicmp(layout, CameraEmulator::kLayoutAndroid) == 0 || _wcsicmp(layout, CameraEmulator::kLayoutFit) == 0;
        }
        else if (argv[i][0] != L'-' && !image)
        {
            image = argv[i];
        }
        else
        {
            validArgs = false;
        }
    }
    if (!validArgs)
    {
        fwprintf(stderr,
            L"Usage:\n"
            L"  %s [--layout android|fit] [--screen [x,y,w,h]] [image.png]  run the virtual camera\n"
            L"  %s --update [--layout android|fit] [--screen [x,y,w,h]] [image.png]\n"
            L"      change a running camera; with --screen, broadcast to it until Ctrl+C\n"
            L"\n"
            L"Layouts: android (default) compensates the Android emulator's crop of webcam frames\n"
            L"         for portrait devices; fit letterboxes the image for regular apps.\n"
            L"--screen broadcasts a zone of the screen (physical pixels); without coordinates, drag\n"
            L"         a rectangle with the mouse (Esc cancels). The image is shown again if the\n"
            L"         broadcast stops.\n",
            argv[0], argv[0]);
        return 2;
    }

    if (screen && !screenRegionGiven)
    {
        wprintf(L"Drag a rectangle around the zone to broadcast (Esc to cancel)...\n");
        if (!SelectScreenRegion(screenRegion))
        {
            wprintf(L"Cancelled.\n");
            return 1;
        }
    }

    if (layout)
    {
        const HRESULT hr = PublishLayout(_wcsicmp(layout, CameraEmulator::kLayoutFit) == 0 ? CameraEmulator::kLayoutFit : CameraEmulator::kLayoutAndroid);
        if (FAILED(hr)) return Fail(L"Saving the layout", hr);
    }
    if (image)
    {
        const HRESULT hr = PublishImage(image);
        if (FAILED(hr)) return Fail(L"Publishing the image", hr);
    }
    else if (GetFileAttributesW(CameraEmulator::SharedImagePath().c_str()) == INVALID_FILE_ATTRIBUTES)
    {
        wprintf(L"No image published yet; the camera will show color bars.\n");
    }
    if (updateOnly && !screen) return 0;

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);

    if (updateOnly)
    {
        // Feed a camera that is already running (in another CameraEmulator.exe).
        wprintf(L"Broadcasting to the running camera. Press Ctrl+C to stop.\n");
        RunUntilStopped(&screenRegion);
        return 0;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return Fail(L"CoInitializeEx", hr);
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return Fail(L"MFStartup", hr);

    const int result = RunCamera(screen ? &screenRegion : nullptr);

    MFShutdown();
    CoUninitialize();
    return result;
}
