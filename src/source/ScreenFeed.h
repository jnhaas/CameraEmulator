#pragma once

#include "Common.h"
#include "ScreenShare.h"
#include <vector>

// Reader side of the screen-capture shared memory (see ScreenShare.h). Creates the section so
// that CameraEmulator.exe can open it.
class ScreenFeed
{
public:
    ScreenFeed();
    ~ScreenFeed();
    ScreenFeed(const ScreenFeed&) = delete;
    ScreenFeed& operator=(const ScreenFeed&) = delete;

    // True while the app is writing frames.
    bool IsLive() const;

    // Copies the latest frame if it is new and complete. Returns false otherwise.
    bool ReadIfNew(std::vector<BYTE>& pixels, UINT32& width, UINT32& height);

private:
    const CameraEmulator::ScreenHeader* Header() const
    {
        return static_cast<const CameraEmulator::ScreenHeader*>(m_view);
    }

    HANDLE m_mapping = nullptr;
    void* m_view = nullptr;
    LONG m_lastSequence = 0;
};
