#include "ScreenCapture.h"
#include "ScreenShare.h"
#include <algorithm>
#include <cstdio>

using namespace CameraEmulator;

namespace
{
    constexpr LONG kMinRegionSize = 16;
    constexpr LONG kBorderThickness = 3;
    constexpr LONG kFrameIntervalMs = 33;

    // Region selection overlay -------------------------------------------------------------

    struct SelectionState
    {
        POINT origin{};  // top-left of the virtual screen
        POINT start{};
        POINT current{};
        bool dragging = false;
        bool accepted = false;
    };

    RECT NormalizedSelection(const SelectionState& state)
    {
        return RECT{std::min(state.start.x, state.current.x), std::min(state.start.y, state.current.y),
                    std::max(state.start.x, state.current.x), std::max(state.start.y, state.current.y)};
    }

    LRESULT CALLBACK SelectionProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<SelectionState*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        switch (message)
        {
        case WM_CREATE:
            SetWindowLongPtrW(window, GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
            return 0;
        case WM_LBUTTONDOWN:
            state->start = state->current = POINT{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
            state->dragging = true;
            SetCapture(window);
            InvalidateRect(window, nullptr, FALSE);
            return 0;
        case WM_MOUSEMOVE:
            if (state->dragging)
            {
                state->current = POINT{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
                InvalidateRect(window, nullptr, FALSE);
            }
            return 0;
        case WM_LBUTTONUP:
            if (state->dragging)
            {
                state->current = POINT{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
                state->dragging = false;
                ReleaseCapture();
                const RECT selection = NormalizedSelection(*state);
                if (selection.right - selection.left >= kMinRegionSize && selection.bottom - selection.top >= kMinRegionSize)
                {
                    state->accepted = true;
                    DestroyWindow(window);
                }
                else
                {
                    InvalidateRect(window, nullptr, FALSE); // too small: let the user try again
                }
            }
            return 0;
        case WM_RBUTTONDOWN:
            DestroyWindow(window);
            return 0;
        case WM_KEYDOWN:
            if (wParam == VK_ESCAPE) DestroyWindow(window);
            return 0;
        case WM_PAINT:
        {
            PAINTSTRUCT paint;
            HDC dc = BeginPaint(window, &paint);
            RECT client;
            GetClientRect(window, &client);
            FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            if (state->dragging)
            {
                const RECT selection = NormalizedSelection(*state);
                FillRect(dc, &selection, static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
            }
            EndPaint(window, &paint);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    // Border -------------------------------------------------------------------------------

    LRESULT CALLBACK BorderProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_NCHITTEST) return HTTRANSPARENT;
        return DefWindowProcW(window, message, wParam, lParam);
    }
}

bool SelectScreenRegion(RECT& region)
{
    static const wchar_t kClassName[] = L"CameraEmulatorSelection";
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = SelectionProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    SelectionState state;
    state.origin = POINT{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN)};
    HWND window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED, kClassName, L"Select the region to broadcast",
        WS_POPUP, state.origin.x, state.origin.y, GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN),
        nullptr, nullptr, windowClass.hInstance, &state);
    if (!window) return false;

    // Dim the screen; the selection shows up lighter.
    SetLayeredWindowAttributes(window, 0, 110, LWA_ALPHA);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!state.accepted) return false;

    const RECT selection = NormalizedSelection(state);
    region = RECT{selection.left + state.origin.x, selection.top + state.origin.y,
                  selection.right + state.origin.x, selection.bottom + state.origin.y};
    return true;
}

// ScreenBroadcaster --------------------------------------------------------------------------

bool ScreenBroadcaster::Start(const RECT& region)
{
    Stop();
    m_region = region;
    // Larger regions than the shared buffer are clipped to its maximum size.
    m_region.right = std::min(m_region.right, m_region.left + static_cast<LONG>(kScreenMaxWidth));
    m_region.bottom = std::min(m_region.bottom, m_region.top + static_cast<LONG>(kScreenMaxHeight));
    if (m_region.right - m_region.left < kMinRegionSize || m_region.bottom - m_region.top < kMinRegionSize) return false;

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) return false;
    m_thread = std::thread(&ScreenBroadcaster::Run, this);
    return true;
}

void ScreenBroadcaster::Stop()
{
    if (m_stopEvent) SetEvent(m_stopEvent);
    if (m_thread.joinable()) m_thread.join();
    if (m_stopEvent)
    {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

void ScreenBroadcaster::Run()
{
    const LONG width = m_region.right - m_region.left;
    const LONG height = m_region.bottom - m_region.top;

    HDC screen = GetDC(nullptr);
    HDC memory = CreateCompatibleDC(screen);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(info.bmiHeader);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height; // top-down, matches the shared layout
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* pixels = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
    HGDIOBJ previous = bitmap ? SelectObject(memory, bitmap) : nullptr;

    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    LARGE_INTEGER due{};
    due.QuadPart = -10'000LL * kFrameIntervalMs;
    if (timer) SetWaitableTimer(timer, &due, kFrameIntervalMs, nullptr, nullptr, FALSE);

    const HANDLE waits[] = {m_stopEvent, timer};
    while (bitmap && timer && WaitForMultipleObjects(2, waits, FALSE, INFINITE) == WAIT_OBJECT_0 + 1)
    {
        EnsureSection();
        if (!m_view) continue;
        if (BitBlt(memory, 0, 0, width, height, screen, m_region.left, m_region.top, SRCCOPY))
        {
            GdiFlush();
            Publish(pixels);
        }
    }

    if (timer) CloseHandle(timer);
    if (previous) SelectObject(memory, previous);
    if (bitmap) DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(nullptr, screen);
    CloseSection();
}

void ScreenBroadcaster::EnsureSection()
{
    // The section is created by the media source when a client starts streaming. Prefer the
    // Global one (Frame Server); a Local one only exists when the source runs in a test harness.
    const ULONGLONG now = GetTickCount64();
    if (m_viewIsGlobal || now < m_nextGlobalProbe) return;
    m_nextGlobalProbe = now + 500;

    for (const wchar_t* name : {kScreenSectionGlobal, kScreenSectionLocal})
    {
        if (m_view && name == kScreenSectionLocal) return; // already on the Local section
        HANDLE mapping = OpenFileMappingW(FILE_MAP_READ | FILE_MAP_WRITE, FALSE, name);
        if (!mapping) continue;
        void* view = MapViewOfFile(mapping, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, kScreenSectionSize);
        if (!view)
        {
            CloseHandle(mapping);
            continue;
        }
        CloseSection();
        m_mapping = mapping;
        m_view = view;
        m_viewIsGlobal = name == kScreenSectionGlobal;
        wprintf(L"Broadcasting the screen region to the camera (%s section).\n", m_viewIsGlobal ? L"global" : L"local");
        return;
    }
}

void ScreenBroadcaster::Publish(const void* pixels)
{
    auto* header = static_cast<ScreenHeader*>(m_view);
    const UINT32 width = static_cast<UINT32>(m_region.right - m_region.left);
    const UINT32 height = static_cast<UINT32>(m_region.bottom - m_region.top);

    // Seqlock: odd while writing.
    if ((InterlockedIncrement(&header->sequence) & 1) == 0) InterlockedIncrement(&header->sequence);
    header->magic = kScreenMagic;
    header->width = width;
    header->height = height;
    memcpy(ScreenPixels(m_view), pixels, static_cast<size_t>(width) * height * 4);
    header->lastUpdateTick = GetTickCount64();
    InterlockedIncrement(&header->sequence);
}

void ScreenBroadcaster::CloseSection()
{
    if (m_view) UnmapViewOfFile(m_view);
    if (m_mapping) CloseHandle(m_mapping);
    m_view = nullptr;
    m_mapping = nullptr;
    m_viewIsGlobal = false;
}

// RegionBorder -------------------------------------------------------------------------------

void RegionBorder::Show(const RECT& region)
{
    Hide();

    static const wchar_t kClassName[] = L"CameraEmulatorBorder";
    static HBRUSH brush = CreateSolidBrush(RGB(255, 64, 0));
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = BorderProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hbrBackground = brush;
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    const LONG width = region.right - region.left + 2 * kBorderThickness;
    const LONG height = region.bottom - region.top + 2 * kBorderThickness;
    m_window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
        kClassName, L"", WS_POPUP, region.left - kBorderThickness, region.top - kBorderThickness, width, height,
        nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!m_window) return;

    // Only the frame is visible; the inside stays a hole so the content shows through.
    HRGN outer = CreateRectRgn(0, 0, width, height);
    HRGN inner = CreateRectRgn(kBorderThickness, kBorderThickness, width - kBorderThickness, height - kBorderThickness);
    CombineRgn(outer, outer, inner, RGN_DIFF);
    DeleteObject(inner);
    SetWindowRgn(m_window, outer, FALSE); // the window owns the region now

    SetLayeredWindowAttributes(m_window, 0, 255, LWA_ALPHA);
    SetWindowDisplayAffinity(m_window, WDA_EXCLUDEFROMCAPTURE);
    ShowWindow(m_window, SW_SHOWNOACTIVATE);
}

void RegionBorder::Hide()
{
    if (m_window) DestroyWindow(m_window);
    m_window = nullptr;
}
