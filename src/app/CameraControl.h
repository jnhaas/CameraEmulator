#pragma once

// Operations shared by the console app (CameraEmulator.exe) and the tray app
// (CameraEmulatorTray.exe). Nothing here prints: callers report errors their own way.

#include <windows.h>
#include <mfvirtualcamera.h>
#include <wrl/client.h>
#include <string>

namespace CameraControl
{
    // Settings stored in %ProgramData%\CameraEmulator\settings.ini (see Shared.h).
    std::wstring ReadSetting(const wchar_t* section, const wchar_t* key, const wchar_t* defaultValue = L"");
    HRESULT WriteSetting(const wchar_t* section, const wchar_t* key, const wchar_t* value);

    // Image and layout read by the media source.
    HRESULT PublishImage(const wchar_t* path); // also remembers the original file name
    std::wstring PublishedImageName();         // empty if no image was published
    // Removes the published image: the camera shows the default image embedded in the source DLL.
    HRESULT UseDefaultImage();
    HRESULT PublishLayout(const wchar_t* layout);
    bool AndroidLayout();

    // Screen zones: "x,y,w,h", or "wxh" centered on the monitor under the mouse.
    bool ParseRegion(const wchar_t* text, RECT& region);
    std::wstring FormatRegion(const RECT& region); // "x,y,w,h"

    // Global shortcut that stops the screen broadcast.
    struct Hotkey
    {
        UINT modifiers = MOD_CONTROL | MOD_ALT;
        UINT key = 'X';
        std::wstring text = L"Ctrl+Alt+X";
    };
    // Accepts e.g. "ctrl+alt+x", "ctrl+shift+f12": modifiers (ctrl, alt, shift, win) and one key
    // (letter, digit, or F1-F24), separated by '+'. At least one modifier is required.
    bool ParseHotkey(const wchar_t* text, Hotkey& hotkey);

    // Virtual camera. MF must be started on the calling thread.
    bool VirtualCamerasSupported();
    HRESULT CreateCamera(MFVirtualCameraLifetime lifetime, Microsoft::WRL::ComPtr<IMFVirtualCamera>& camera);
    // True if a CameraEmulator camera is visible: installed, or exposed by a running instance.
    bool CameraExists();
    // System lifetime: the camera persists (across reboots) until uninstalled, and shows the
    // image without any app running. Also recorded in settings.ini (IsInstalled).
    HRESULT InstallCamera();
    HRESULT UninstallCamera();
    bool IsInstalled();

    // Message for failures of Start: usually the source DLL is not registered.
    constexpr wchar_t kRegisterHint[] = L"Is CameraEmulatorSource.dll registered? Run 'regsvr32 CameraEmulatorSource.dll' as administrator.";
}
