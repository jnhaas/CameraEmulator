#pragma once

#include <windows.h>
#include <atomic>
#include <functional>
#include <thread>

// Lets the user drag a rectangle over the whole virtual screen. Returns false if cancelled
// (Esc or right click). Coordinates are physical pixels: the process must be per-monitor DPI aware.
bool SelectScreenRegion(RECT& region);

// Returns a region of the given size centered on the monitor under the mouse cursor.
RECT CenteredRegion(LONG width, LONG height);

// Diagnostics from the capture code. CameraEmulator.exe prints them; the tray app writes them to
// its log file. Default: stdout.
void SetScreenCaptureLogger(void (*logger)(const wchar_t* message));

// Captures a screen region at 30 fps and publishes it to the media source through the shared
// memory section described in ScreenShare.h. The size is fixed at Start; the region can be
// moved at any time from any thread.
class ScreenBroadcaster
{
public:
    ~ScreenBroadcaster() { Stop(); }

    bool Start(RECT& region); // clips the region to the maximum supported size
    void Stop();
    void MoveTo(POINT topLeft);

private:
    void Run();
    void EnsureSection();
    void Publish(const void* pixels);
    void CloseSection();

    LONG m_width = 0;
    LONG m_height = 0;
    std::atomic<ULONGLONG> m_origin{0}; // x in the high 32 bits, y in the low 32 bits
    HANDLE m_stopEvent = nullptr;
    std::thread m_thread;

    HANDLE m_mapping = nullptr;
    void* m_view = nullptr;
    bool m_viewIsGlobal = false;
    ULONGLONG m_nextGlobalProbe = 0;
};

// Topmost frame drawn just outside the broadcast region, with a tab on top. Dragging the frame
// or the tab moves the region; the inside is a hole, so the content stays usable. Excluded from
// screen capture. Must be created and pumped on the same thread.
class RegionBorder
{
public:
    using MoveCallback = std::function<void(const RECT& region)>;

    ~RegionBorder() { Hide(); }

    void Show(const RECT& region, MoveCallback onMove);
    void Hide();

private:
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);
    RECT RegionFromWindow(const RECT& window) const;
    void ClampToScreen(RECT& window) const;
    void Paint();

    HWND m_window = nullptr;
    MoveCallback m_onMove;
    SIZE m_regionSize{};
    LONG m_thickness = 0;
    LONG m_tabHeight = 0;
    LONG m_tabWidth = 0;
    HFONT m_font = nullptr;
};
