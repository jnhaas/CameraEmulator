#include "ScreenCapture.h"
#include "ScreenShare.h"
#include <algorithm>
#include <cstdio>
#include <shellscalingapi.h>

using namespace CameraEmulator;

namespace
{
    constexpr LONG kMinRegionSize = 16;
    constexpr LONG kBorderThickness = 4; // DIPs
    constexpr LONG kTabHeight = 22;      // DIPs
    constexpr wchar_t kTabText[] = L"CameraEmulator - drag to move";
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

RECT CenteredRegion(LONG width, LONG height)
{
    POINT cursor{};
    GetCursorPos(&cursor);
    MONITORINFO monitor{sizeof(monitor)};
    GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &monitor);
    const RECT& work = monitor.rcWork;
    const LONG left = work.left + (work.right - work.left - width) / 2;
    const LONG top = work.top + (work.bottom - work.top - height) / 2;
    return RECT{left, top, left + width, top + height};
}

// ScreenBroadcaster --------------------------------------------------------------------------

namespace
{
    ULONGLONG PackOrigin(POINT point)
    {
        return (static_cast<ULONGLONG>(static_cast<ULONG>(point.x)) << 32) | static_cast<ULONG>(point.y);
    }

    POINT UnpackOrigin(ULONGLONG packed)
    {
        return POINT{static_cast<LONG>(static_cast<ULONG>(packed >> 32)), static_cast<LONG>(static_cast<ULONG>(packed))};
    }
}

bool ScreenBroadcaster::Start(RECT& region)
{
    Stop();
    // Larger regions than the shared buffer are clipped to its maximum size.
    region.right = std::min(region.right, region.left + static_cast<LONG>(kScreenMaxWidth));
    region.bottom = std::min(region.bottom, region.top + static_cast<LONG>(kScreenMaxHeight));
    if (region.right - region.left < kMinRegionSize || region.bottom - region.top < kMinRegionSize) return false;

    m_width = region.right - region.left;
    m_height = region.bottom - region.top;
    MoveTo(POINT{region.left, region.top});

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!m_stopEvent) return false;
    m_thread = std::thread(&ScreenBroadcaster::Run, this);
    return true;
}

void ScreenBroadcaster::MoveTo(POINT topLeft)
{
    m_origin.store(PackOrigin(topLeft));
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
    const LONG width = m_width;
    const LONG height = m_height;

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
        const POINT origin = UnpackOrigin(m_origin.load());
        if (BitBlt(memory, 0, 0, width, height, screen, origin.x, origin.y, SRCCOPY))
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
    const UINT32 width = static_cast<UINT32>(m_width);
    const UINT32 height = static_cast<UINT32>(m_height);

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
//
// Window layout (client coordinates):
//
//   +--------------+
//   |     tab      |                       <- m_tabHeight
//   +--------------+------------------+
//   |  frame (m_thickness wide)       |
//   |   +-------------------------+   |
//   |   |   broadcast region       |   |    <- hole: clicks go to the windows below
//   |   +-------------------------+   |
//   +---------------------------------+

void RegionBorder::Show(const RECT& region, MoveCallback onMove)
{
    Hide();
    m_onMove = std::move(onMove);
    m_regionSize = SIZE{region.right - region.left, region.bottom - region.top};

    // Size the frame for the DPI of the monitor the region is on.
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(MonitorFromRect(&region, MONITOR_DEFAULTTONEAREST), MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    m_thickness = MulDiv(kBorderThickness, static_cast<int>(dpiY), 96);
    m_tabHeight = MulDiv(kTabHeight, static_cast<int>(dpiY), 96);
    m_font = CreateFontW(-MulDiv(9, static_cast<int>(dpiY), 72), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    HDC screen = GetDC(nullptr);
    HGDIOBJ previousFont = SelectObject(screen, m_font);
    SIZE text{};
    GetTextExtentPoint32W(screen, kTabText, static_cast<int>(wcslen(kTabText)), &text);
    SelectObject(screen, previousFont);
    ReleaseDC(nullptr, screen);
    const LONG width = m_regionSize.cx + 2 * m_thickness;
    m_tabWidth = std::min(width, text.cx + m_tabHeight);

    static const wchar_t kClassName[] = L"CameraEmulatorBorder";
    WNDCLASSEXW windowClass{sizeof(windowClass)};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.hCursor = LoadCursorW(nullptr, IDC_SIZEALL);
    windowClass.lpszClassName = kClassName;
    RegisterClassExW(&windowClass);

    const LONG height = m_tabHeight + m_regionSize.cy + 2 * m_thickness;
    m_window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, kClassName, L"CameraEmulator zone",
        WS_POPUP, region.left - m_thickness, region.top - m_thickness - m_tabHeight, width, height,
        nullptr, nullptr, windowClass.hInstance, this);
    if (!m_window)
    {
        fwprintf(stderr, L"Could not create the zone frame: error %lu\n", GetLastError());
        return;
    }

    // Only the tab and the frame are part of the window; the inside is a hole.
    HRGN shape = CreateRectRgn(0, m_tabHeight, width, height);
    HRGN tab = CreateRectRgn(0, 0, m_tabWidth, m_tabHeight + 1);
    HRGN inner = CreateRectRgn(m_thickness, m_tabHeight + m_thickness, width - m_thickness, height - m_thickness);
    CombineRgn(shape, shape, tab, RGN_OR);
    CombineRgn(shape, shape, inner, RGN_DIFF);
    DeleteObject(tab);
    DeleteObject(inner);
    SetWindowRgn(m_window, shape, FALSE); // the window owns the region now

    SetWindowDisplayAffinity(m_window, WDA_EXCLUDEFROMCAPTURE);
    ShowWindow(m_window, SW_SHOWNOACTIVATE);
}

void RegionBorder::Hide()
{
    if (m_window) DestroyWindow(m_window);
    m_window = nullptr;
    if (m_font) DeleteObject(m_font);
    m_font = nullptr;
}

RECT RegionBorder::RegionFromWindow(const RECT& window) const
{
    const LONG left = window.left + m_thickness;
    const LONG top = window.top + m_tabHeight + m_thickness;
    return RECT{left, top, left + m_regionSize.cx, top + m_regionSize.cy};
}

void RegionBorder::ClampToScreen(RECT& window) const
{
    // Keep the broadcast region on the virtual screen (the frame itself may stick out).
    const RECT region = RegionFromWindow(window);
    const LONG screenLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
    const LONG screenTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
    const LONG screenRight = screenLeft + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const LONG screenBottom = screenTop + GetSystemMetrics(SM_CYVIRTUALSCREEN);

    LONG dx = 0, dy = 0;
    if (region.right > screenRight) dx = screenRight - region.right;
    if (region.left + dx < screenLeft) dx = screenLeft - region.left;
    if (region.bottom > screenBottom) dy = screenBottom - region.bottom;
    if (region.top + dy < screenTop) dy = screenTop - region.top;
    OffsetRect(&window, dx, dy);
}

void RegionBorder::Paint()
{
    PAINTSTRUCT paint;
    HDC dc = BeginPaint(m_window, &paint);
    RECT client;
    GetClientRect(m_window, &client);
    HBRUSH brush = CreateSolidBrush(RGB(255, 64, 0));
    FillRect(dc, &client, brush);
    DeleteObject(brush);

    RECT tab{0, 0, m_tabWidth, m_tabHeight};
    HGDIOBJ previousFont = SelectObject(dc, m_font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(255, 255, 255));
    DrawTextW(dc, kTabText, -1, &tab, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    SelectObject(dc, previousFont);
    EndPaint(m_window, &paint);
}

LRESULT CALLBACK RegionBorder::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE)
    {
        SetWindowLongPtrW(window, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams));
    }
    auto* self = reinterpret_cast<RegionBorder*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (!self) return DefWindowProcW(window, message, wParam, lParam);

    switch (message)
    {
    case WM_NCHITTEST:
        // The whole visible window (tab and frame) acts as a title bar: dragging it moves it.
        return HTCAPTION;
    case WM_SETCURSOR:
        SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        return TRUE;
    case WM_WINDOWPOSCHANGING:
    {
        // Applies to drags as well as programmatic moves.
        auto* position = reinterpret_cast<WINDOWPOS*>(lParam);
        if (!(position->flags & SWP_NOMOVE))
        {
            RECT bounds;
            GetWindowRect(window, &bounds);
            OffsetRect(&bounds, position->x - bounds.left, position->y - bounds.top);
            self->ClampToScreen(bounds);
            position->x = bounds.left;
            position->y = bounds.top;
        }
        return 0;
    }
    case WM_WINDOWPOSCHANGED:
    {
        RECT bounds;
        GetWindowRect(window, &bounds);
        if (self->m_onMove) self->m_onMove(self->RegionFromWindow(bounds));
        return 0;
    }
    case WM_PAINT:
        self->Paint();
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
