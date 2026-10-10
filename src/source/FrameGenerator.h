#pragma once

#include "Common.h"
#include "ScreenFeed.h"
#include <wincodec.h>
#include <string>
#include <vector>

// Produces frames for one media type. Sources, by priority:
//   1. the live screen capture published by CameraEmulator.exe --screen (see ScreenShare.h);
//   2. the shared image, re-read whenever the file changes on disk;
//   3. the default image embedded in this DLL (color bars if it cannot be decoded).
// Pictures are fitted into the frame according to the layout setting (see Shared.h), which is
// re-read once per second.
//
// Not thread-safe: owned and used by a single worker thread (COM must be initialized on it).
class FrameGenerator
{
public:
    FrameGenerator(const GUID& subtype, UINT32 width, UINT32 height);

    // Returns the current frame in the output format.
    const std::vector<BYTE>& CurrentFrame();

private:
    bool RefreshLayout();
    void ReloadImageIfChanged(bool force);
    void ShowDefaultImage();
    HRESULT DecodeImage(const BYTE* data, size_t size, std::vector<BYTE>& bgra);
    HRESULT ComposeScreen(std::vector<BYTE>& bgra);
    HRESULT Compose(IWICBitmapSource* source, WICBitmapInterpolationMode interpolation, std::vector<BYTE>& bgra);
    void DrawTestPattern(std::vector<BYTE>& bgra) const;
    void Encode(std::vector<BYTE>& bgra);

    GUID m_subtype;
    UINT32 m_width;
    UINT32 m_height;
    std::wstring m_imagePath;
    std::wstring m_settingsPath;
    bool m_androidLayout = true;
    ComPtr<IWICImagingFactory> m_wic;
    std::vector<BYTE> m_frame;
    ULONGLONG m_nextCheckTick = 0;

    bool m_showingImage = false;
    FILETIME m_imageWriteTime{};
    ULONGLONG m_imageSize = 0;

    ScreenFeed m_screen;
    bool m_showingScreen = false;
    std::vector<BYTE> m_screenPixels;
    UINT32 m_screenWidth = 0;
    UINT32 m_screenHeight = 0;
};
