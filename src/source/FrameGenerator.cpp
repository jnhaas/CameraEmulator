#include "FrameGenerator.h"
#include "resource.h"
#include <algorithm>

namespace
{
    constexpr ULONGLONG kImageCheckIntervalMs = 1000;

    HRESULT ReadWholeFile(const std::wstring& path, std::vector<BYTE>& data)
    {
        // Share everything so the control app can replace the file while we read it.
        HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());

        HRESULT hr = S_OK;
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file, &size)) hr = HRESULT_FROM_WIN32(GetLastError());
        else if (size.QuadPart <= 0 || size.QuadPart > 256 * 1024 * 1024) hr = E_INVALIDARG;
        else
        {
            data.resize(static_cast<size_t>(size.QuadPart));
            DWORD read = 0;
            if (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &read, nullptr)) hr = HRESULT_FROM_WIN32(GetLastError());
            else if (read != data.size()) hr = E_FAIL;
        }
        CloseHandle(file);
        return hr;
    }
}

FrameGenerator::FrameGenerator(const GUID& subtype, UINT32 width, UINT32 height)
    : m_subtype(subtype), m_width(width), m_height(height),
      m_imagePath(CameraEmulator::SharedImagePath()), m_settingsPath(CameraEmulator::SharedSettingsPath())
{
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&m_wic));
    if (FAILED(hr)) Trace(L"CoCreateInstance(WICImagingFactory) failed 0x%08X", hr);
}

const std::vector<BYTE>& FrameGenerator::CurrentFrame()
{
    const ULONGLONG now = GetTickCount64();
    const bool periodicCheck = m_frame.empty() || now >= m_nextCheckTick;
    bool layoutChanged = false;
    if (periodicCheck)
    {
        m_nextCheckTick = now + kImageCheckIntervalMs;
        layoutChanged = RefreshLayout();
    }

    // A live screen capture takes precedence over the image.
    bool newScreenFrame = m_screen.ReadIfNew(m_screenPixels, m_screenWidth, m_screenHeight);
    if (!newScreenFrame && m_showingScreen && layoutChanged && m_screen.IsLive()) newScreenFrame = true;
    if (newScreenFrame)
    {
        std::vector<BYTE> bgra;
        const HRESULT hr = ComposeScreen(bgra);
        if (SUCCEEDED(hr))
        {
            if (!m_showingScreen) Trace(L"Showing screen capture (%ux%u)", m_screenWidth, m_screenHeight);
            Encode(bgra);
            m_showingScreen = true;
            m_showingImage = false;
            return m_frame;
        }
        Trace(L"Failed to compose screen frame: 0x%08X", hr);
    }
    if (m_showingScreen && m_screen.IsLive()) return m_frame; // no new capture yet: repeat the last one

    bool force = layoutChanged;
    if (m_showingScreen)
    {
        Trace(L"Screen capture stopped, back to the image");
        m_showingScreen = false;
        force = true;
    }
    if (periodicCheck || force) ReloadImageIfChanged(force);
    return m_frame;
}

bool FrameGenerator::RefreshLayout()
{
    wchar_t layout[32] = {};
    GetPrivateProfileStringW(CameraEmulator::kSettingsSection, CameraEmulator::kLayoutKey, CameraEmulator::kLayoutAndroid,
        layout, ARRAYSIZE(layout), m_settingsPath.c_str());
    const bool androidLayout = _wcsicmp(layout, CameraEmulator::kLayoutFit) != 0;
    const bool changed = androidLayout != m_androidLayout;
    m_androidLayout = androidLayout;
    if (changed) Trace(L"Layout changed to %s", layout);
    return changed;
}

void FrameGenerator::ReloadImageIfChanged(bool force)
{
    WIN32_FILE_ATTRIBUTE_DATA info{};
    if (!GetFileAttributesExW(m_imagePath.c_str(), GetFileExInfoStandard, &info))
    {
        if (force || m_showingImage || m_frame.empty())
        {
            ShowDefaultImage();
            m_showingImage = false;
        }
        return;
    }

    const ULONGLONG size = (static_cast<ULONGLONG>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    if (!force && m_showingImage && size == m_imageSize && CompareFileTime(&info.ftLastWriteTime, &m_imageWriteTime) == 0) return;

    // Decode from memory so we never hold the file open.
    std::vector<BYTE> file;
    std::vector<BYTE> bgra;
    HRESULT hr = ReadWholeFile(m_imagePath, file);
    if (SUCCEEDED(hr)) hr = DecodeImage(file.data(), file.size(), bgra);
    if (SUCCEEDED(hr))
    {
        Encode(bgra);
        m_showingImage = true;
        m_imageSize = size;
        m_imageWriteTime = info.ftLastWriteTime;
        Trace(L"Loaded %s (%ux%u, %s layout)", m_imagePath.c_str(), m_width, m_height, m_androidLayout ? L"android" : L"fit");
    }
    else
    {
        // Keep the previous frame and retry on the next check (the file may be mid-write).
        Trace(L"Failed to decode %s: 0x%08X", m_imagePath.c_str(), hr);
        if (m_frame.empty() || force) ShowDefaultImage();
    }
}

void FrameGenerator::ShowDefaultImage()
{
    // PNG embedded in this DLL (CameraEmulatorSource.rc). Color bars only if that fails.
    std::vector<BYTE> bgra;
    HRESULT hr = E_FAIL;
    HRSRC resource = FindResourceW(ModuleHandle(), MAKEINTRESOURCEW(IDR_DEFAULT_IMAGE), RT_RCDATA);
    HGLOBAL loaded = resource ? LoadResource(ModuleHandle(), resource) : nullptr;
    const void* data = loaded ? LockResource(loaded) : nullptr;
    if (data) hr = DecodeImage(static_cast<const BYTE*>(data), SizeofResource(ModuleHandle(), resource), bgra);
    if (FAILED(hr))
    {
        Trace(L"Failed to decode the default image: 0x%08X", hr);
        DrawTestPattern(bgra);
    }
    Encode(bgra);
}

HRESULT FrameGenerator::DecodeImage(const BYTE* data, size_t size, std::vector<BYTE>& bgra)
{
    if (!m_wic) return E_NOT_VALID_STATE;

    ComPtr<IWICStream> stream;
    RETURN_IF_FAILED(m_wic->CreateStream(&stream));
    RETURN_IF_FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(data), static_cast<DWORD>(size)));

    ComPtr<IWICBitmapDecoder> decoder;
    RETURN_IF_FAILED(m_wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder));
    ComPtr<IWICBitmapFrameDecode> source;
    RETURN_IF_FAILED(decoder->GetFrame(0, &source));

    // Premultiplied BGRA: dropping alpha afterwards composites transparent areas onto black.
    ComPtr<IWICFormatConverter> converter;
    RETURN_IF_FAILED(m_wic->CreateFormatConverter(&converter));
    RETURN_IF_FAILED(converter->Initialize(source.Get(), GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom));

    return Compose(converter.Get(), WICBitmapInterpolationModeHighQualityCubic, bgra);
}

HRESULT FrameGenerator::ComposeScreen(std::vector<BYTE>& bgra)
{
    if (!m_wic) return E_NOT_VALID_STATE;
    ComPtr<IWICBitmap> bitmap;
    RETURN_IF_FAILED(m_wic->CreateBitmapFromMemory(m_screenWidth, m_screenHeight, GUID_WICPixelFormat32bppBGR,
        m_screenWidth * 4, static_cast<UINT>(m_screenPixels.size()), m_screenPixels.data(), &bitmap));
    // Fant is fast and sharp enough for downscaling screen content at frame rate.
    return Compose(bitmap.Get(), WICBitmapInterpolationModeFant, bgra);
}

HRESULT FrameGenerator::Compose(IWICBitmapSource* source, WICBitmapInterpolationMode interpolation, std::vector<BYTE>& bgra)
{
    UINT sourceWidth = 0, sourceHeight = 0;
    RETURN_IF_FAILED(source->GetSize(&sourceWidth, &sourceHeight));
    if (sourceWidth == 0 || sourceHeight == 0) return E_INVALIDARG;

    // Region of the frame the picture is fitted into. For a portrait device the Android emulator
    // rotates webcam frames by cropping the left H*H/W x H pixels and scaling them up to the
    // whole picture, discarding the rest (camera-format-converters.c, convert_frame_fast).
    const UINT regionWidth = m_androidLayout ? std::min(m_width, m_height * m_height / m_width) : m_width;
    const UINT regionHeight = m_height;

    // Fit inside the region, preserving aspect ratio.
    const double scale = std::min(static_cast<double>(regionWidth) / sourceWidth, static_cast<double>(regionHeight) / sourceHeight);
    const UINT scaledWidth = std::clamp(static_cast<UINT>(sourceWidth * scale + 0.5), 1u, regionWidth);
    const UINT scaledHeight = std::clamp(static_cast<UINT>(sourceHeight * scale + 0.5), 1u, regionHeight);

    ComPtr<IWICBitmapScaler> scaler;
    RETURN_IF_FAILED(m_wic->CreateBitmapScaler(&scaler));
    RETURN_IF_FAILED(scaler->Initialize(source, scaledWidth, scaledHeight, interpolation));

    const UINT stride = m_width * 4;
    bgra.assign(static_cast<size_t>(stride) * m_height, 0);
    const size_t offset = static_cast<size_t>((regionHeight - scaledHeight) / 2) * stride + static_cast<size_t>((regionWidth - scaledWidth) / 2) * 4;
    return scaler->CopyPixels(nullptr, stride, static_cast<UINT>(bgra.size() - offset), bgra.data() + offset);
}

void FrameGenerator::DrawTestPattern(std::vector<BYTE>& bgra) const
{
    // 75% SMPTE-style color bars, BGRA.
    static constexpr BYTE kBars[8][3] = {
        {191, 191, 191}, {0, 191, 191}, {191, 191, 0}, {0, 191, 0},
        {191, 0, 191},   {0, 0, 191},   {191, 0, 0},   {0, 0, 0},
    };
    bgra.resize(static_cast<size_t>(m_width) * m_height * 4);
    for (UINT32 y = 0; y < m_height; ++y)
    {
        BYTE* row = bgra.data() + static_cast<size_t>(y) * m_width * 4;
        for (UINT32 x = 0; x < m_width; ++x)
        {
            const BYTE* bar = kBars[x * 8 / m_width];
            row[x * 4 + 0] = bar[0];
            row[x * 4 + 1] = bar[1];
            row[x * 4 + 2] = bar[2];
            row[x * 4 + 3] = 255;
        }
    }
}

void FrameGenerator::Encode(std::vector<BYTE>& bgra)
{
    if (m_subtype == MFVideoFormat_RGB32)
    {
        for (size_t i = 3; i < bgra.size(); i += 4) bgra[i] = 255;
        m_frame = std::move(bgra);
        return;
    }

    // NV12, BT.601 limited range. Output sizes are always even.
    const UINT32 w = m_width, h = m_height;
    m_frame.resize(static_cast<size_t>(w) * h * 3 / 2);
    BYTE* yPlane = m_frame.data();
    BYTE* uvPlane = yPlane + static_cast<size_t>(w) * h;

    for (UINT32 y = 0; y < h; ++y)
    {
        const BYTE* src = bgra.data() + static_cast<size_t>(y) * w * 4;
        BYTE* dst = yPlane + static_cast<size_t>(y) * w;
        for (UINT32 x = 0; x < w; ++x, src += 4)
        {
            dst[x] = static_cast<BYTE>(((66 * src[2] + 129 * src[1] + 25 * src[0] + 128) >> 8) + 16);
        }
    }

    for (UINT32 y = 0; y < h; y += 2)
    {
        const BYTE* row0 = bgra.data() + static_cast<size_t>(y) * w * 4;
        const BYTE* row1 = row0 + static_cast<size_t>(w) * 4;
        BYTE* dst = uvPlane + static_cast<size_t>(y / 2) * w;
        for (UINT32 x = 0; x < w; x += 2)
        {
            const size_t i = static_cast<size_t>(x) * 4;
            const int b = (row0[i] + row0[i + 4] + row1[i] + row1[i + 4] + 2) / 4;
            const int g = (row0[i + 1] + row0[i + 5] + row1[i + 1] + row1[i + 5] + 2) / 4;
            const int r = (row0[i + 2] + row0[i + 6] + row1[i + 2] + row1[i + 6] + 2) / 4;
            dst[x] = static_cast<BYTE>(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
            dst[x + 1] = static_cast<BYTE>(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
        }
    }
}
