#pragma once

#include "Common.h"
#include <wincodec.h>
#include <string>
#include <vector>

// Produces frames for one media type from the shared PNG. The image is letterboxed to the
// output size and re-read whenever the file changes on disk. If no image is available, a
// color-bar test pattern is shown instead.
//
// Not thread-safe: owned and used by a single worker thread (COM must be initialized on it).
class FrameGenerator
{
public:
    FrameGenerator(const GUID& subtype, UINT32 width, UINT32 height);

    // Returns the current frame in the output format.
    const std::vector<BYTE>& CurrentFrame();

private:
    void ReloadIfChanged();
    HRESULT DecodeImage(std::vector<BYTE>& bgra);
    void DrawTestPattern(std::vector<BYTE>& bgra) const;
    void Encode(std::vector<BYTE>& bgra);

    GUID m_subtype;
    UINT32 m_width;
    UINT32 m_height;
    std::wstring m_imagePath;
    ComPtr<IWICImagingFactory> m_wic;
    std::vector<BYTE> m_frame;

    ULONGLONG m_nextCheckTick = 0;
    bool m_showingImage = false;
    FILETIME m_imageWriteTime{};
    ULONGLONG m_imageSize = 0;
};
