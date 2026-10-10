// CameraEmulator control app.
//
//   CameraEmulator.exe [--layout android|fit] [--screen [x,y,w,h | wxh]] [image.png]
//       Apply the settings (optional) and expose the virtual camera until Ctrl+C is pressed.
//       --screen broadcasts a zone of the screen instead of the image: give it as x,y,w,h or wxh
//       (centered) in physical pixels, or omit it to drag a rectangle. The zone can then be moved
//       by dragging its red frame.
//   CameraEmulator.exe --update [--layout android|fit] [--screen [x,y,w,h | wxh]] [image.png]
//       Change the settings of a running camera and exit. With --screen, keep broadcasting the
//       zone to that camera until Ctrl+C.
//   CameraEmulator.exe --set-image image.png
//       Same as --update image.png.

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
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

    // Accepts "x,y,width,height", or "widthxheight" for a region centered on the monitor under
    // the mouse.
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

    void PrintRegion(const wchar_t* prefix, const RECT& region)
    {
        wprintf(L"%s %ld,%ld,%ld,%ld\n", prefix, region.left, region.top, region.right - region.left, region.bottom - region.top);
    }

    // Broadcasts the screen region, if any, until Ctrl+C. The region can be moved by dragging
    // its frame.
    void RunUntilStopped(const RECT* screenRegion)
    {
        ScreenBroadcaster broadcaster;
        RegionBorder border;
        RECT region{};
        if (screenRegion)
        {
            region = *screenRegion;
            if (!broadcaster.Start(region))
            {
                fwprintf(stderr, L"Invalid screen region.\n");
                return;
            }
            border.Show(region, [&](const RECT& moved) {
                region = moved;
                broadcaster.MoveTo(POINT{moved.left, moved.top});
            });
            PrintRegion(L"Broadcasting screen region", region);
            wprintf(L"Drag the red frame to move it.\n");
        }
        WaitForStop();
        if (screenRegion) PrintRegion(L"Last region (reuse with --screen):", region);
    }

    bool VirtualCamerasSupported()
    {
        BOOL supported = FALSE;
        if (FAILED(MFIsVirtualCameraTypeSupported(MFVirtualCameraType_SoftwareCameraSource, &supported)) || !supported)
        {
            fwprintf(stderr, L"Virtual cameras are not supported on this system (Windows 11 required).\n");
            return false;
        }
        return true;
    }

    HRESULT CreateCamera(MFVirtualCameraLifetime lifetime, ComPtr<IMFVirtualCamera>& camera)
    {
        return MFCreateVirtualCamera(MFVirtualCameraType_SoftwareCameraSource, lifetime, MFVirtualCameraAccess_CurrentUser,
            CAMERAEMULATOR_FRIENDLY_NAME, CAMERAEMULATOR_CLSID_STRING, nullptr, 0, &camera);
    }

    // True if a CameraEmulator camera is already visible: installed with --install, or exposed
    // by another running instance.
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
        for (UINT32 i = 0; i < count; ++i)
        {
            wchar_t* name = nullptr;
            UINT32 length = 0;
            // Windows appends a localized suffix to virtual cameras, e.g. "CameraEmulator (Windows Virtual Camera)".
            const size_t prefixLength = wcslen(CAMERAEMULATOR_FRIENDLY_NAME);
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

    // System lifetime: the camera persists (across reboots) until --uninstall, so the Android
    // emulator always finds it. Frames come from the source DLL in Frame Server and do not need
    // this app to be running.
    int InstallCamera()
    {
        if (!VirtualCamerasSupported()) return 1;
        ComPtr<IMFVirtualCamera> camera;
        HRESULT hr = CreateCamera(MFVirtualCameraLifetime_System, camera);
        if (FAILED(hr)) return Fail(L"MFCreateVirtualCamera", hr);
        hr = camera->Start(nullptr);
        camera->Shutdown();
        if (FAILED(hr))
        {
            Fail(L"IMFVirtualCamera::Start", hr);
            fwprintf(stderr, L"Is CameraEmulatorSource.dll registered? Run 'regsvr32 CameraEmulatorSource.dll' as administrator.\n");
            return 1;
        }
        wprintf(L"Virtual camera \"%s\" installed. It stays available until you run --uninstall.\n", CAMERAEMULATOR_FRIENDLY_NAME);
        return 0;
    }

    int UninstallCamera()
    {
        if (!VirtualCamerasSupported()) return 1;
        ComPtr<IMFVirtualCamera> camera;
        HRESULT hr = CreateCamera(MFVirtualCameraLifetime_System, camera);
        if (FAILED(hr)) return Fail(L"MFCreateVirtualCamera", hr);
        hr = camera->Remove();
        camera->Shutdown();
        if (FAILED(hr)) return Fail(L"IMFVirtualCamera::Remove", hr);
        wprintf(L"Virtual camera \"%s\" uninstalled.\n", CAMERAEMULATOR_FRIENDLY_NAME);
        return 0;
    }

    int RunCamera(const RECT* screenRegion)
    {
        if (!VirtualCamerasSupported()) return 1;

        // Session lifetime: the camera disappears when this process exits.
        ComPtr<IMFVirtualCamera> camera;
        HRESULT hr = CreateCamera(MFVirtualCameraLifetime_Session, camera);
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
    // Unbuffered output, so status lines show up immediately when redirected to a file or pipe.
    setvbuf(stdout, nullptr, _IONBF, 0);

    bool updateOnly = false;
    bool install = false;
    bool uninstall = false;
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
        else if (_wcsicmp(argv[i], L"--install") == 0)
        {
            install = true;
        }
        else if (_wcsicmp(argv[i], L"--uninstall") == 0)
        {
            uninstall = true;
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
    const int modes = (install ? 1 : 0) + (uninstall ? 1 : 0) + (updateOnly ? 1 : 0);
    if (modes > 1 || ((install || uninstall) && screen)) validArgs = false;
    if (!validArgs)
    {
        fwprintf(stderr,
            L"Usage:\n"
            L"  %s --install [--layout android|fit] [image.png]\n"
            L"      install the camera permanently (survives restarts of this app and reboots)\n"
            L"  %s --uninstall\n"
            L"      remove the installed camera\n"
            L"  %s [--layout android|fit] [--screen [x,y,w,h | wxh]] [image.png]\n"
            L"      apply the settings; if the camera is not installed, expose it until Ctrl+C;\n"
            L"      with --screen, broadcast the zone until Ctrl+C\n"
            L"  %s --update [--layout android|fit] [--screen [x,y,w,h | wxh]] [image.png]\n"
            L"      only apply the settings; with --screen, broadcast to the existing camera until Ctrl+C\n"
            L"\n"
            L"Layouts: android (default) compensates the Android emulator's crop of webcam frames\n"
            L"         for portrait devices; fit letterboxes the image for regular apps.\n"
            L"--screen broadcasts a zone of the screen, in physical pixels: x,y,w,h, or wxh to center\n"
            L"         it on the monitor under the mouse. Without a value, drag a rectangle with the\n"
            L"         mouse (Esc cancels). Drag the red frame to move the zone while it runs. The\n"
            L"         image is shown again when the broadcast stops.\n",
            argv[0], argv[0], argv[0], argv[0]);
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

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr)) return Fail(L"CoInitializeEx", hr);
    hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) return Fail(L"MFStartup", hr);

    g_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    SetConsoleCtrlHandler(OnConsoleCtrl, TRUE);

    int result = 0;
    if (install)
    {
        result = InstallCamera();
    }
    else if (uninstall)
    {
        result = UninstallCamera();
    }
    else if (updateOnly || CameraExists())
    {
        // The camera is installed or exposed by another instance: never create a duplicate.
        if (!updateOnly) wprintf(L"Camera \"%s\" already exists; settings applied.\n", CAMERAEMULATOR_FRIENDLY_NAME);
        if (screen)
        {
            wprintf(L"Broadcasting to the existing camera. Press Ctrl+C to stop.\n");
            RunUntilStopped(&screenRegion);
        }
    }
    else
    {
        result = RunCamera(screen ? &screenRegion : nullptr);
    }

    MFShutdown();
    CoUninitialize();
    return result;
}
