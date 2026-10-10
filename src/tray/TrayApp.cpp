// CameraEmulatorTray: system tray front end for the CameraEmulator virtual camera, no console.
//
// Same features as CameraEmulator.exe, from the tray icon's menu: choose the image, broadcast a
// zone of the screen (stopped from the menu or with the global stop shortcut), pick the layout,
// keep the camera installed, start with Windows.
//
// If the camera is not installed, the tray app exposes it while it runs (session lifetime).

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <mfapi.h>
#include <wrl/client.h>
#include <algorithm>
#include <cstdarg>
#include <memory>
#include <string>
#include <vector>

#include "CameraControl.h"
#include "ScreenCapture.h"
#include "Shared.h"

using Microsoft::WRL::ComPtr;
using namespace CameraControl;

namespace
{
    constexpr UINT kTrayMessage = WM_APP + 1;
    constexpr UINT kTrayIconId = 1;
    constexpr int kStopHotkeyId = 1;
    constexpr wchar_t kWindowClass[] = L"CameraEmulatorTray";
    constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    constexpr wchar_t kRunValue[] = L"CameraEmulator";

    enum Command : UINT
    {
        CmdChooseImage = 100,
        CmdSelectZone,
        CmdLastZone,
        CmdZone640x480,
        CmdZone800x600,
        CmdZone1280x720,
        CmdStopBroadcast,
        CmdLayoutAndroid,
        CmdLayoutFit,
        CmdKeepInstalled,
        CmdStartWithWindows,
        CmdOpenFolder,
        CmdExit,
    };

    // Tray icon: a camera, with a red "on air" dot while a zone is broadcast. Drawn at runtime so
    // it is sharp at any DPI.
    HICON CreateCameraIcon(int size, bool live)
    {
        struct Color { BYTE r, g, b; };
        auto inCircle = [](double x, double y, double cx, double cy, double radius) {
            return (x - cx) * (x - cx) + (y - cy) * (y - cy) <= radius * radius;
        };
        auto inRoundRect = [](double x, double y, double l, double t, double r, double b, double radius) {
            const double dx = std::max({l + radius - x, 0.0, x - (r - radius)});
            const double dy = std::max({t + radius - y, 0.0, y - (b - radius)});
            return x >= l && x <= r && y >= t && y <= b && dx * dx + dy * dy <= radius * radius;
        };
        // Top-most shape wins.
        auto shade = [&](double x, double y, Color& color) {
            if (live && inCircle(x, y, 0.80, 0.22, 0.17)) { color = {232, 17, 35}; return true; }
            if (inCircle(x, y, 0.50, 0.57, 0.12)) { color = {0, 120, 215}; return true; }
            if (inCircle(x, y, 0.50, 0.57, 0.21)) { color = {45, 45, 45}; return true; }
            if (inRoundRect(x, y, 0.12, 0.33, 0.88, 0.81, 0.07)) { color = {245, 245, 245}; return true; }
            if (inRoundRect(x, y, 0.05, 0.26, 0.95, 0.88, 0.12) || inRoundRect(x, y, 0.30, 0.15, 0.60, 0.30, 0.03))
            {
                color = {45, 45, 45};
                return true;
            }
            return false;
        };

        constexpr int kSamples = 4; // 4x4 supersampling for anti-aliasing
        std::vector<UINT32> pixels(static_cast<size_t>(size) * size);
        for (int py = 0; py < size; ++py)
        {
            for (int px = 0; px < size; ++px)
            {
                int hits = 0, r = 0, g = 0, b = 0;
                for (int sy = 0; sy < kSamples; ++sy)
                {
                    for (int sx = 0; sx < kSamples; ++sx)
                    {
                        Color color{};
                        if (shade((px + (sx + 0.5) / kSamples) / size, (py + (sy + 0.5) / kSamples) / size, color))
                        {
                            ++hits;
                            r += color.r;
                            g += color.g;
                            b += color.b;
                        }
                    }
                }
                if (hits == 0) continue;
                const UINT32 alpha = static_cast<UINT32>(hits * 255 / (kSamples * kSamples));
                pixels[static_cast<size_t>(py) * size + px] = (alpha << 24) | ((r / hits) << 16) | ((g / hits) << 8) | (b / hits);
            }
        }

        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = size;
        info.bmiHeader.biHeight = -size;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;
        void* bits = nullptr;
        HBITMAP color = CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!color) return LoadIconW(nullptr, IDI_APPLICATION);
        memcpy(bits, pixels.data(), pixels.size() * sizeof(UINT32));
        HBITMAP mask = CreateBitmap(size, size, 1, 1, nullptr); // unused with 32-bit alpha, but required

        ICONINFO iconInfo{TRUE, 0, 0, mask, color};
        HICON icon = CreateIconIndirect(&iconInfo);
        DeleteObject(color);
        DeleteObject(mask);
        return icon ? icon : LoadIconW(nullptr, IDI_APPLICATION);
    }

    std::wstring ExecutablePath()
    {
        wchar_t path[MAX_PATH] = {};
        GetModuleFileNameW(nullptr, path, ARRAYSIZE(path));
        return path;
    }

    bool StartsWithWindows()
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
        const bool exists = RegQueryValueExW(key, kRunValue, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
        RegCloseKey(key);
        return exists;
    }

    HRESULT SetStartWithWindows(bool enable)
    {
        HKEY key = nullptr;
        LSTATUS status = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
        if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
        if (enable)
        {
            const std::wstring command = L"\"" + ExecutablePath() + L"\"";
            status = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
                static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        }
        else
        {
            status = RegDeleteValueW(key, kRunValue);
            if (status == ERROR_FILE_NOT_FOUND) status = ERROR_SUCCESS;
        }
        RegCloseKey(key);
        return HRESULT_FROM_WIN32(status);
    }

    // The tray app has no console: diagnostics go to %ProgramData%\CameraEmulator\tray.log.
    void WriteLog(const wchar_t* message)
    {
        SYSTEMTIME now;
        GetLocalTime(&now);
        wchar_t line[640];
        swprintf_s(line, L"%04u-%02u-%02u %02u:%02u:%02u.%03u %s\r\n", now.wYear, now.wMonth, now.wDay,
            now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, message);

        static const std::wstring path = CameraEmulator::SharedFolder() + L"\\tray.log";
        HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return;
        char utf8[1280];
        const int length = WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8, sizeof(utf8), nullptr, nullptr);
        DWORD written = 0;
        if (length > 1) WriteFile(file, utf8, static_cast<DWORD>(length - 1), &written, nullptr);
        CloseHandle(file);
    }

    void Log(const wchar_t* format, ...)
    {
        wchar_t message[512];
        va_list args;
        va_start(args, format);
        vswprintf_s(message, format, args);
        va_end(args);
        WriteLog(message);
    }

    std::wstring ErrorText(const wchar_t* what, HRESULT hr)
    {
        wchar_t text[256];
        swprintf_s(text, L"%s failed (0x%08X).", what, static_cast<unsigned>(hr));
        return text;
    }

    class TrayApp
    {
    public:
        int Run(HINSTANCE instance);

    private:
        static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
        LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

        void StartCamera();
        HRESULT StartSessionCamera();
        void StopSessionCamera();
        void SetKeepInstalled(bool keep);

        void AddTrayIcon();
        void UpdateTrayIcon();
        void Notify(const std::wstring& text, bool error);
        std::wstring StatusText() const;
        void ShowMenu();
        void OnCommand(UINT command);
        void ChooseImage();
        void StartBroadcast(RECT region);
        void StopBroadcast();
        void Exit();

        HWND m_window = nullptr;
        UINT m_taskbarCreated = 0;
        HICON m_icon = nullptr;
        HICON m_liveIcon = nullptr;
        ComPtr<IMFVirtualCamera> m_sessionCamera; // only when the camera is not installed

        std::unique_ptr<ScreenBroadcaster> m_broadcaster;
        std::unique_ptr<RegionBorder> m_border;
        RECT m_region{};
        Hotkey m_stopKey;
        bool m_hotkeyRegistered = false;
    };

    int TrayApp::Run(HINSTANCE instance)
    {
        ParseHotkey(ReadSetting(CameraEmulator::kScreenSection, CameraEmulator::kStopKeyKey, L"ctrl+alt+x").c_str(), m_stopKey);

        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = WindowProc;
        windowClass.hInstance = instance;
        windowClass.lpszClassName = kWindowClass;
        RegisterClassExW(&windowClass);

        // A hidden top-level window (not message-only): the tray menu needs a window that can
        // become foreground, so it closes when the user clicks elsewhere.
        m_window = CreateWindowExW(WS_EX_TOOLWINDOW, kWindowClass, L"CameraEmulator", WS_POPUP, 0, 0, 0, 0,
            nullptr, nullptr, instance, this);
        if (!m_window) return 1;
        m_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

        AddTrayIcon();
        StartCamera();

        MSG message;
        while (GetMessageW(&message, nullptr, 0, 0) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return 0;
    }

    void TrayApp::StartCamera()
    {
        if (!VirtualCamerasSupported())
        {
            Notify(L"Virtual cameras are not supported on this system (Windows 11 required).", true);
            return;
        }
        if (CameraExists())
        {
            // Installed (possibly before the installed flag existed) or exposed by another instance.
            if (!IsInstalled()) WriteSetting(CameraEmulator::kSettingsSection, L"installed", L"1");
            Log(L"Camera already exists; using it");
            return;
        }
        const HRESULT hr = IsInstalled() ? InstallCamera() : StartSessionCamera();
        if (FAILED(hr)) Notify(ErrorText(L"Starting the camera", hr) + L" " + kRegisterHint, true);
        else Log(IsInstalled() ? L"Installed camera re-created" : L"Temporary camera started (not installed)");
    }

    HRESULT TrayApp::StartSessionCamera()
    {
        ComPtr<IMFVirtualCamera> camera;
        HRESULT hr = CreateCamera(MFVirtualCameraLifetime_Session, camera);
        if (FAILED(hr)) return hr;
        hr = camera->Start(nullptr);
        if (FAILED(hr))
        {
            camera->Shutdown();
            return hr;
        }
        m_sessionCamera = camera;
        return S_OK;
    }

    void TrayApp::StopSessionCamera()
    {
        if (!m_sessionCamera) return;
        m_sessionCamera->Remove();
        m_sessionCamera->Shutdown();
        m_sessionCamera.Reset();
    }

    void TrayApp::SetKeepInstalled(bool keep)
    {
        HRESULT hr;
        if (keep)
        {
            StopSessionCamera(); // replaced by the installed camera, never both
            hr = InstallCamera();
            if (FAILED(hr)) StartSessionCamera();
        }
        else
        {
            hr = UninstallCamera();
            if (SUCCEEDED(hr)) hr = StartSessionCamera(); // keep a camera while the tray app runs
        }
        if (FAILED(hr)) Notify(ErrorText(keep ? L"Installing the camera" : L"Uninstalling the camera", hr), true);
    }

    void TrayApp::AddTrayIcon()
    {
        const UINT dpi = GetDpiForWindow(m_window);
        const int size = GetSystemMetricsForDpi(SM_CXSMICON, dpi);
        if (m_icon) DestroyIcon(m_icon);
        if (m_liveIcon) DestroyIcon(m_liveIcon);
        m_icon = CreateCameraIcon(size, false);
        m_liveIcon = CreateCameraIcon(size, true);

        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = m_window;
        data.uID = kTrayIconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.uCallbackMessage = kTrayMessage;
        data.hIcon = m_broadcaster ? m_liveIcon : m_icon;
        wcsncpy_s(data.szTip, StatusText().c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_ADD, &data);
        data.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &data);
    }

    void TrayApp::UpdateTrayIcon()
    {
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = m_window;
        data.uID = kTrayIconId;
        data.uFlags = NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        data.hIcon = m_broadcaster ? m_liveIcon : m_icon;
        wcsncpy_s(data.szTip, StatusText().c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    void TrayApp::Notify(const std::wstring& text, bool error)
    {
        Log(L"%s: %s", error ? L"Error" : L"Info", text.c_str());
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = m_window;
        data.uID = kTrayIconId;
        data.uFlags = NIF_INFO;
        data.dwInfoFlags = error ? NIIF_ERROR : NIIF_INFO;
        wcsncpy_s(data.szInfoTitle, L"CameraEmulator", _TRUNCATE);
        wcsncpy_s(data.szInfo, text.c_str(), _TRUNCATE);
        Shell_NotifyIconW(NIM_MODIFY, &data);
    }

    std::wstring TrayApp::StatusText() const
    {
        if (m_broadcaster)
        {
            return L"CameraEmulator - broadcasting a " + std::to_wstring(m_region.right - m_region.left) + L"x" +
                   std::to_wstring(m_region.bottom - m_region.top) + L" zone";
        }
        const std::wstring image = PublishedImageName();
        return L"CameraEmulator - showing " + (image.empty() ? std::wstring(L"color bars") : image);
    }

    void TrayApp::ShowMenu()
    {
        HMENU menu = CreatePopupMenu();
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, StatusText().c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CmdChooseImage, L"Choose image...");

        HMENU zones = CreatePopupMenu();
        AppendMenuW(zones, MF_STRING, CmdSelectZone, L"Select zone...");
        RECT lastRegion{};
        const std::wstring last = ReadSetting(CameraEmulator::kScreenSection, CameraEmulator::kLastRegionKey);
        if (!last.empty() && ParseRegion(last.c_str(), lastRegion))
        {
            AppendMenuW(zones, MF_STRING, CmdLastZone, (L"Last zone (" + last + L")").c_str());
        }
        AppendMenuW(zones, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(zones, MF_STRING, CmdZone640x480, L"640 x 480, centered");
        AppendMenuW(zones, MF_STRING, CmdZone800x600, L"800 x 600, centered");
        AppendMenuW(zones, MF_STRING, CmdZone1280x720, L"1280 x 720, centered");
        AppendMenuW(menu, MF_POPUP | (m_broadcaster ? MF_GRAYED : 0), reinterpret_cast<UINT_PTR>(zones), L"Broadcast screen zone");
        AppendMenuW(menu, MF_STRING | (m_broadcaster ? 0 : MF_GRAYED), CmdStopBroadcast,
            (L"Stop broadcast\t" + m_stopKey.text).c_str());

        const bool android = AndroidLayout();
        HMENU layouts = CreatePopupMenu();
        AppendMenuW(layouts, MF_STRING | (android ? MF_CHECKED : 0), CmdLayoutAndroid, L"Android emulator (portrait)");
        AppendMenuW(layouts, MF_STRING | (android ? 0 : MF_CHECKED), CmdLayoutFit, L"Fit (desktop apps)");
        AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(layouts), L"Layout");

        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (IsInstalled() ? MF_CHECKED : 0), CmdKeepInstalled, L"Keep the camera when this app is closed");
        AppendMenuW(menu, MF_STRING | (StartsWithWindows() ? MF_CHECKED : 0), CmdStartWithWindows, L"Start with Windows");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, CmdOpenFolder, L"Open settings folder");
        AppendMenuW(menu, MF_STRING, CmdExit, L"Exit");

        POINT cursor{};
        GetCursorPos(&cursor);
        SetForegroundWindow(m_window); // so the menu closes when clicking elsewhere
        const UINT command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, cursor.x, cursor.y, 0, m_window, nullptr);
        PostMessageW(m_window, WM_NULL, 0, 0);
        DestroyMenu(menu); // also destroys the submenus
        if (command) OnCommand(command);
    }

    void TrayApp::OnCommand(UINT command)
    {
        RECT region{};
        switch (command)
        {
        case CmdChooseImage:
            ChooseImage();
            break;
        case CmdSelectZone:
            if (SelectScreenRegion(region)) StartBroadcast(region);
            break;
        case CmdLastZone:
            if (ParseRegion(ReadSetting(CameraEmulator::kScreenSection, CameraEmulator::kLastRegionKey).c_str(), region)) StartBroadcast(region);
            break;
        case CmdZone640x480:
            StartBroadcast(CenteredRegion(640, 480));
            break;
        case CmdZone800x600:
            StartBroadcast(CenteredRegion(800, 600));
            break;
        case CmdZone1280x720:
            StartBroadcast(CenteredRegion(1280, 720));
            break;
        case CmdStopBroadcast:
            StopBroadcast();
            break;
        case CmdLayoutAndroid:
        case CmdLayoutFit:
        {
            const HRESULT hr = PublishLayout(command == CmdLayoutFit ? CameraEmulator::kLayoutFit : CameraEmulator::kLayoutAndroid);
            if (FAILED(hr)) Notify(ErrorText(L"Saving the layout", hr), true);
            break;
        }
        case CmdKeepInstalled:
            SetKeepInstalled(!IsInstalled());
            break;
        case CmdStartWithWindows:
        {
            const HRESULT hr = SetStartWithWindows(!StartsWithWindows());
            if (FAILED(hr)) Notify(ErrorText(L"Changing the startup setting", hr), true);
            break;
        }
        case CmdOpenFolder:
            ShellExecuteW(nullptr, L"open", CameraEmulator::SharedFolder().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case CmdExit:
            Exit();
            break;
        }
        UpdateTrayIcon();
    }

    void TrayApp::ChooseImage()
    {
        ComPtr<IFileOpenDialog> dialog;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog)))) return;
        const COMDLG_FILTERSPEC filters[] = {
            {L"Images", L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff"},
            {L"All files", L"*.*"},
        };
        dialog->SetFileTypes(ARRAYSIZE(filters), filters);
        dialog->SetTitle(L"Image shown by the CameraEmulator camera");
        if (FAILED(dialog->Show(m_window))) return; // includes cancel

        ComPtr<IShellItem> item;
        PWSTR path = nullptr;
        if (FAILED(dialog->GetResult(&item)) || FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) return;
        const HRESULT hr = PublishImage(path);
        CoTaskMemFree(path);
        if (FAILED(hr)) Notify(ErrorText(L"Publishing the image", hr), true);
    }

    void TrayApp::StartBroadcast(RECT region)
    {
        StopBroadcast();
        auto broadcaster = std::make_unique<ScreenBroadcaster>();
        if (!broadcaster->Start(region))
        {
            Notify(L"Invalid screen zone.", true);
            return;
        }
        m_broadcaster = std::move(broadcaster);
        m_region = region;
        m_border = std::make_unique<RegionBorder>();
        m_border->Show(region, [this](const RECT& moved) {
            m_region = moved;
            if (m_broadcaster) m_broadcaster->MoveTo(POINT{moved.left, moved.top});
        });

        m_hotkeyRegistered = RegisterHotKey(m_window, kStopHotkeyId, m_stopKey.modifiers | MOD_NOREPEAT, m_stopKey.key) != FALSE;
        Log(L"Broadcasting %s, stop shortcut %s %s", FormatRegion(region).c_str(), m_stopKey.text.c_str(), m_hotkeyRegistered ? L"registered" : L"NOT registered");
        if (!m_hotkeyRegistered)
        {
            Notify(m_stopKey.text + L" is used by another application: stop the broadcast from this menu.", false);
        }
        UpdateTrayIcon();
    }

    void TrayApp::StopBroadcast()
    {
        if (!m_broadcaster) return;
        if (m_hotkeyRegistered) UnregisterHotKey(m_window, kStopHotkeyId);
        m_hotkeyRegistered = false;
        m_border.reset();
        m_broadcaster.reset();
        Log(L"Broadcast stopped at %s", FormatRegion(m_region).c_str());
        WriteSetting(CameraEmulator::kScreenSection, CameraEmulator::kLastRegionKey, FormatRegion(m_region).c_str());
        UpdateTrayIcon();
    }

    void TrayApp::Exit()
    {
        StopBroadcast();
        StopSessionCamera();
        NOTIFYICONDATAW data{sizeof(data)};
        data.hWnd = m_window;
        data.uID = kTrayIconId;
        Shell_NotifyIconW(NIM_DELETE, &data);
        DestroyWindow(m_window);
    }

    LRESULT CALLBACK TrayApp::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCCREATE)
        {
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
        }
        auto* self = reinterpret_cast<TrayApp*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self && self->m_window == window) return self->HandleMessage(message, wParam, lParam);
        return DefWindowProcW(window, message, wParam, lParam);
    }

    LRESULT TrayApp::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == m_taskbarCreated && m_taskbarCreated != 0)
        {
            AddTrayIcon(); // Explorer restarted
            return 0;
        }
        switch (message)
        {
        case kTrayMessage:
            switch (LOWORD(lParam))
            {
            case NIN_SELECT:
            case NIN_KEYSELECT:
            case WM_CONTEXTMENU:
                ShowMenu();
                break;
            }
            return 0;
        case WM_HOTKEY:
            if (wParam == kStopHotkeyId)
            {
                Log(L"Stop shortcut pressed");
                StopBroadcast();
            }
            return 0;
        case WM_COMMAND:
            // Menu commands can also be posted to the window (scripting, tests).
            OnCommand(LOWORD(wParam));
            return 0;
        case WM_DPICHANGED:
            AddTrayIcon();
            return 0;
        case WM_ENDSESSION:
            if (wParam) Exit();
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(m_window, message, wParam, lParam);
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    // Screen coordinates and captures in physical pixels on every monitor.
    const BOOL dpiAware = SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const DWORD dpiError = dpiAware ? 0 : GetLastError();

    HANDLE singleInstance = CreateMutexW(nullptr, TRUE, L"Local\\CameraEmulatorTray");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        MessageBoxW(nullptr, L"CameraEmulator is already running in the notification area.", L"CameraEmulator", MB_ICONINFORMATION);
        return 0;
    }

    SetScreenCaptureLogger(WriteLog);
    Log(L"Tray app started (per-monitor DPI awareness: %s, error %lu)", dpiAware ? L"yes" : L"no", dpiError);

    // STA: required by the file open dialog.
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) return 1;
    if (FAILED(MFStartup(MF_VERSION)))
    {
        CoUninitialize();
        return 1;
    }

    int result = 0;
    {
        TrayApp app;
        result = app.Run(instance);
    }

    MFShutdown();
    CoUninitialize();
    if (singleInstance) CloseHandle(singleInstance);
    return result;
}
