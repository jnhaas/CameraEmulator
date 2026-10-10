#pragma once

#include <windows.h>
#include <atomic>
#include <thread>

// Lets the user drag a rectangle over the whole virtual screen. Returns false if cancelled
// (Esc or right click). Coordinates are physical pixels: the process must be per-monitor DPI aware.
bool SelectScreenRegion(RECT& region);

// Captures a screen region at 30 fps and publishes it to the media source through the shared
// memory section described in ScreenShare.h.
class ScreenBroadcaster
{
public:
    ~ScreenBroadcaster() { Stop(); }

    bool Start(const RECT& region);
    void Stop();

private:
    void Run();
    void EnsureSection();
    void Publish(const void* pixels);
    void CloseSection();

    RECT m_region{};
    HANDLE m_stopEvent = nullptr;
    std::thread m_thread;

    HANDLE m_mapping = nullptr;
    void* m_view = nullptr;
    bool m_viewIsGlobal = false;
    ULONGLONG m_nextGlobalProbe = 0;
};

// Click-through, topmost frame drawn just outside the broadcast region. Excluded from screen
// capture. Must be created and pumped on the same thread.
class RegionBorder
{
public:
    ~RegionBorder() { Hide(); }

    void Show(const RECT& region);
    void Hide();

private:
    HWND m_window = nullptr;
};
