#include "ScreenFeed.h"
#include <sddl.h>

using namespace CameraEmulator;

ScreenFeed::ScreenFeed()
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(kScreenSectionSddl, SDDL_REVISION_1, &descriptor, nullptr))
    {
        Trace(L"Invalid screen section security descriptor: %lu", GetLastError());
        return;
    }
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), descriptor, FALSE};

    const DWORD sizeHigh = static_cast<DWORD>(static_cast<ULONGLONG>(kScreenSectionSize) >> 32);
    const DWORD sizeLow = static_cast<DWORD>(kScreenSectionSize & 0xFFFFFFFF);
    m_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, sizeHigh, sizeLow, kScreenSectionGlobal);
    if (!m_mapping)
    {
        // Not running in the service (no SeCreateGlobalPrivilege): use the session namespace.
        const DWORD globalError = GetLastError();
        m_mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &attributes, PAGE_READWRITE, sizeHigh, sizeLow, kScreenSectionLocal);
        Trace(L"Global screen section unavailable (%lu), local section %s", globalError, m_mapping ? L"created" : L"failed");
    }
    LocalFree(descriptor);

    if (m_mapping) m_view = MapViewOfFile(m_mapping, FILE_MAP_READ, 0, 0, 0);
    if (m_mapping && !m_view) Trace(L"MapViewOfFile(screen section) failed: %lu", GetLastError());
}

ScreenFeed::~ScreenFeed()
{
    if (m_view) UnmapViewOfFile(m_view);
    if (m_mapping) CloseHandle(m_mapping);
}

bool ScreenFeed::IsLive() const
{
    if (!m_view || Header()->magic != kScreenMagic) return false;
    return GetTickCount64() - Header()->lastUpdateTick <= kScreenStaleMs;
}

bool ScreenFeed::ReadIfNew(std::vector<BYTE>& pixels, UINT32& width, UINT32& height)
{
    if (!IsLive()) return false;

    const ScreenHeader* header = Header();
    const LONG before = ReadAcquire(&header->sequence);
    if (before == m_lastSequence || (before & 1)) return false;

    const UINT32 frameWidth = header->width;
    const UINT32 frameHeight = header->height;
    if (frameWidth == 0 || frameHeight == 0 || frameWidth > kScreenMaxWidth || frameHeight > kScreenMaxHeight) return false;

    const size_t size = static_cast<size_t>(frameWidth) * frameHeight * 4;
    pixels.resize(size);
    memcpy(pixels.data(), ScreenPixels(m_view), size);

    // The writer touched the frame while we copied it: skip, the next one will be complete.
    if (ReadAcquire(&header->sequence) != before) return false;

    m_lastSequence = before;
    width = frameWidth;
    height = frameHeight;
    return true;
}
