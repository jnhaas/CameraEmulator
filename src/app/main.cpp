// CameraEmulator console app. See CameraEmulatorTray.exe for the same features in the system tray.
//
//   CameraEmulator.exe --install [--layout android|fit] [image.png]
//       Install the camera permanently (survives restarts of this app and reboots).
//   CameraEmulator.exe --uninstall
//       Remove the installed camera.
//   CameraEmulator.exe [--layout android|fit] [--screen [x,y,w,h | wxh] [--stop-key KEYS]] [image.png]
//       Apply the settings. If no camera exists, expose one until Ctrl+C. With --screen, broadcast
//       a zone of the screen (drag a rectangle if no value is given) until Ctrl+C or the stop key;
//       the zone can be moved by dragging its red frame.
//   CameraEmulator.exe --update [...]
//       Only apply the settings; with --screen, broadcast to the existing camera.
//   CameraEmulator.exe --set-image image.png
//       Same as --update image.png.

#include <windows.h>
#include <mfapi.h>
#include <cstdio>
#include <string>

#include "CameraControl.h"
#include "ScreenCapture.h"
#include "Shared.h"

using Microsoft::WRL::ComPtr;
using namespace CameraControl;

namespace
{
    constexpr int kStopHotkeyId = 1;
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

    // Waits for Ctrl+C (returns true) or the stop hotkey (returns false) while pumping messages
    // for the windows of this thread (region border).
    bool WaitForStop()
    {
        while (MsgWaitForMultipleObjects(1, &g_stopEvent, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1)
        {
            MSG message;
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                if (message.message == WM_HOTKEY && message.wParam == kStopHotkeyId) return false;
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return true;
    }

    // Broadcasts the screen region, if any, until Ctrl+C or the stop hotkey. The region can be
    // moved by dragging its frame. If this process hosts the camera (hostsCamera), the hotkey
    // only stops the broadcast and Ctrl+C is still needed to remove the camera.
    void RunUntilStopped(const RECT* screenRegion, const Hotkey& stopKey, bool hostsCamera)
    {
        if (!screenRegion)
        {
            WaitForStop();
            return;
        }

        ScreenBroadcaster broadcaster;
        RegionBorder border;
        RECT region = *screenRegion;
        if (!broadcaster.Start(region))
        {
            fwprintf(stderr, L"Invalid screen region.\n");
            return;
        }
        border.Show(region, [&](const RECT& moved) {
            region = moved;
            broadcaster.MoveTo(POINT{moved.left, moved.top});
        });
        wprintf(L"Broadcasting screen region %s\nDrag the red frame to move it.\n", FormatRegion(region).c_str());

        const bool hotkeyRegistered = RegisterHotKey(nullptr, kStopHotkeyId, stopKey.modifiers | MOD_NOREPEAT, stopKey.key) != FALSE;
        if (hotkeyRegistered) wprintf(L"Press %s (anywhere) to stop the broadcast.\n", stopKey.text.c_str());
        else fwprintf(stderr, L"%s is already used by another application; use --stop-key to pick another shortcut.\n", stopKey.text.c_str());

        const bool ctrlC = WaitForStop();
        if (hotkeyRegistered) UnregisterHotKey(nullptr, kStopHotkeyId);
        border.Hide();
        broadcaster.Stop();
        WriteSetting(CameraEmulator::kScreenSection, CameraEmulator::kLastRegionKey, FormatRegion(region).c_str());
        wprintf(L"Broadcast stopped. Last region (reuse with --screen): %s\n", FormatRegion(region).c_str());

        if (!ctrlC && hostsCamera)
        {
            wprintf(L"The camera shows the image again. Press Ctrl+C to remove the camera.\n");
            WaitForStop();
        }
    }

    int RunCamera(const RECT* screenRegion, const Hotkey& stopKey)
    {
        // Session lifetime: the camera disappears when this process exits.
        ComPtr<IMFVirtualCamera> camera;
        HRESULT hr = CreateCamera(MFVirtualCameraLifetime_Session, camera);
        if (FAILED(hr)) return Fail(L"MFCreateVirtualCamera", hr);

        hr = camera->Start(nullptr);
        if (FAILED(hr))
        {
            Fail(L"IMFVirtualCamera::Start", hr);
            fwprintf(stderr, L"%s\n", kRegisterHint);
            camera->Shutdown();
            return 1;
        }

        wprintf(L"Virtual camera \"%s\" is running. Press Ctrl+C to stop.\n", CAMERAEMULATOR_FRIENDLY_NAME);
        RunUntilStopped(screenRegion, stopKey, true);

        wprintf(L"Removing virtual camera...\n");
        hr = camera->Remove();
        if (FAILED(hr)) Fail(L"IMFVirtualCamera::Remove", hr);
        camera->Shutdown();
        return 0;
    }

    void PrintUsage(const wchar_t* exe)
    {
        fwprintf(stderr,
            L"Usage:\n"
            L"  %s --install [--layout android|fit] [image.png]\n"
            L"      install the camera permanently (survives restarts of this app and reboots)\n"
            L"  %s --uninstall\n"
            L"      remove the installed camera\n"
            L"  %s [--layout android|fit] [--screen [x,y,w,h | wxh] [--stop-key KEYS]] [image.png]\n"
            L"      apply the settings; if the camera is not installed, expose it until Ctrl+C;\n"
            L"      with --screen, broadcast the zone until Ctrl+C or the stop key\n"
            L"  %s --update [--layout android|fit] [--screen [x,y,w,h | wxh]] [image.png]\n"
            L"      only apply the settings; with --screen, broadcast to the existing camera until\n"
            L"      Ctrl+C or the stop key\n"
            L"\n"
            L"Layouts: android (default) compensates the Android emulator's crop of webcam frames\n"
            L"         for portrait devices; fit letterboxes the image for regular apps.\n"
            L"--screen broadcasts a zone of the screen, in physical pixels: x,y,w,h, or wxh to center\n"
            L"         it on the monitor under the mouse. Without a value, drag a rectangle with the\n"
            L"         mouse (Esc cancels). Drag the red frame to move the zone while it runs. The\n"
            L"         image is shown again when the broadcast stops.\n"
            L"--stop-key KEYS  global shortcut that stops the broadcast (default ctrl+alt+x), e.g.\n"
            L"         ctrl+shift+f12: modifiers ctrl, alt, shift, win plus a letter, digit or F1-F24.\n",
            exe, exe, exe, exe);
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
    Hotkey stopKey;
    ParseHotkey(ReadSetting(CameraEmulator::kScreenSection, CameraEmulator::kStopKeyKey, L"ctrl+alt+x").c_str(), stopKey);
    bool screen = false;
    bool screenRegionGiven = false;
    RECT screenRegion{};
    bool validArgs = true;
    for (int i = 1; i < argc && validArgs; ++i)
    {
        if (_wcsicmp(argv[i], L"--update") == 0) updateOnly = true;
        else if (_wcsicmp(argv[i], L"--install") == 0) install = true;
        else if (_wcsicmp(argv[i], L"--uninstall") == 0) uninstall = true;
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
        else if (_wcsicmp(argv[i], L"--stop-key") == 0 && i + 1 < argc)
        {
            validArgs = ParseHotkey(argv[++i], stopKey);
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
        PrintUsage(argv[0]);
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
        const wchar_t* value = _wcsicmp(layout, CameraEmulator::kLayoutFit) == 0 ? CameraEmulator::kLayoutFit : CameraEmulator::kLayoutAndroid;
        const HRESULT hr = PublishLayout(value);
        if (FAILED(hr)) return Fail(L"Saving the layout", hr);
        wprintf(L"Layout set to %s\n", value);
    }
    if (image)
    {
        const HRESULT hr = PublishImage(image);
        if (FAILED(hr)) return Fail(L"Publishing the image", hr);
        wprintf(L"Image published to %s\n", CameraEmulator::SharedImagePath().c_str());
    }
    else if (PublishedImageName().empty())
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
    if (!VirtualCamerasSupported())
    {
        fwprintf(stderr, L"Virtual cameras are not supported on this system (Windows 11 required).\n");
        result = 1;
    }
    else if (install)
    {
        hr = InstallCamera();
        if (FAILED(hr))
        {
            result = Fail(L"Installing the camera", hr);
            fwprintf(stderr, L"%s\n", kRegisterHint);
        }
        else
        {
            wprintf(L"Virtual camera \"%s\" installed. It stays available until you run --uninstall.\n", CAMERAEMULATOR_FRIENDLY_NAME);
        }
    }
    else if (uninstall)
    {
        hr = UninstallCamera();
        if (FAILED(hr)) result = Fail(L"Uninstalling the camera", hr);
        else wprintf(L"Virtual camera \"%s\" uninstalled.\n", CAMERAEMULATOR_FRIENDLY_NAME);
    }
    else if (updateOnly || CameraExists())
    {
        // The camera is installed or exposed by another instance: never create a duplicate.
        if (!updateOnly) wprintf(L"Camera \"%s\" already exists; settings applied.\n", CAMERAEMULATOR_FRIENDLY_NAME);
        if (screen)
        {
            wprintf(L"Broadcasting to the existing camera. Press Ctrl+C to stop.\n");
            RunUntilStopped(&screenRegion, stopKey, false);
        }
    }
    else
    {
        result = RunCamera(screen ? &screenRegion : nullptr, stopKey);
    }

    MFShutdown();
    CoUninitialize();
    return result;
}
