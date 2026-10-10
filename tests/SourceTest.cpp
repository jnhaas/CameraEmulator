// Loads CameraEmulatorSource.dll directly (no registration, no Frame Server), pulls one RGB32
// frame from the media source and writes it to a BMP.
//
//   SourceTest.exe <path\to\CameraEmulatorSource.dll> <output.bmp> [width height [seconds]]   (default 1280 720, 10 frames)

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <wrl/client.h>
#include <cstdio>
#include <algorithm>
#include <vector>

#include "Shared.h"

using Microsoft::WRL::ComPtr;

#define CHECK(expr)                                                                         \
    do                                                                                      \
    {                                                                                       \
        const HRESULT hr_ = (expr);                                                         \
        if (FAILED(hr_))                                                                    \
        {                                                                                   \
            fwprintf(stderr, L"%S failed: 0x%08X\n", #expr, static_cast<unsigned>(hr_));    \
            return 1;                                                                       \
        }                                                                                   \
    } while (0)

static HRESULT WaitForEvent(IMFMediaEventGenerator* generator, MediaEventType expected, IMFMediaEvent** result)
{
    for (int i = 0; i < 20; ++i)
    {
        ComPtr<IMFMediaEvent> event;
        HRESULT hr = generator->GetEvent(0, &event);
        if (FAILED(hr)) return hr;
        MediaEventType type = MEUnknown;
        event->GetType(&type);
        if (type == expected)
        {
            *result = event.Detach();
            return S_OK;
        }
    }
    return E_FAIL;
}

static bool WriteBmp(const wchar_t* path, const BYTE* pixels, UINT32 width, UINT32 height)
{
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    const DWORD imageSize = width * height * 4;
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + imageSize;
    info.biSize = sizeof(info);
    info.biWidth = static_cast<LONG>(width);
    info.biHeight = -static_cast<LONG>(height); // top-down
    info.biPlanes = 1;
    info.biBitCount = 32;
    info.biCompression = BI_RGB;

    FILE* out = nullptr;
    if (_wfopen_s(&out, path, L"wb") != 0) return false;
    fwrite(&file, sizeof(file), 1, out);
    fwrite(&info, sizeof(info), 1, out);
    fwrite(pixels, imageSize, 1, out);
    fclose(out);
    return true;
}

int wmain(int argc, wchar_t** argv)
{
    if (argc != 3 && argc != 5 && argc != 6)
    {
        fwprintf(stderr, L"Usage: %s <CameraEmulatorSource.dll> <output.bmp> [width height]\n", argv[0]);
        return 2;
    }

    const UINT32 kWidth = argc >= 5 ? static_cast<UINT32>(_wtoi(argv[3])) : 1280;
    const UINT32 kHeight = argc >= 5 ? static_cast<UINT32>(_wtoi(argv[4])) : 720;
    CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    CHECK(MFStartup(MF_VERSION));

    HMODULE dll = LoadLibraryW(argv[1]);
    if (!dll) CHECK(HRESULT_FROM_WIN32(GetLastError()));
    auto getClassObject = reinterpret_cast<decltype(&DllGetClassObject)>(GetProcAddress(dll, "DllGetClassObject"));
    if (!getClassObject) CHECK(E_NOINTERFACE);

    CLSID clsid;
    CHECK(CLSIDFromString(CAMERAEMULATOR_CLSID_STRING, &clsid));
    ComPtr<IClassFactory> factory;
    CHECK(getClassObject(clsid, IID_PPV_ARGS(&factory)));
    ComPtr<IMFActivate> activate;
    CHECK(factory->CreateInstance(nullptr, IID_PPV_ARGS(&activate)));
    ComPtr<IMFMediaSource> source;
    CHECK(activate->ActivateObject(IID_PPV_ARGS(&source)));

    // Select the RGB32 media type of the requested size so the frame can be written as a BMP directly.
    ComPtr<IMFPresentationDescriptor> pd;
    CHECK(source->CreatePresentationDescriptor(&pd));
    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> sd;
    CHECK(pd->GetStreamDescriptorByIndex(0, &selected, &sd));
    ComPtr<IMFMediaTypeHandler> handler;
    CHECK(sd->GetMediaTypeHandler(&handler));
    DWORD typeCount = 0;
    CHECK(handler->GetMediaTypeCount(&typeCount));
    wprintf(L"Media types: %lu\n", typeCount);
    ComPtr<IMFMediaType> rgbType;
    for (DWORD i = 0; i < typeCount && !rgbType; ++i)
    {
        ComPtr<IMFMediaType> type;
        CHECK(handler->GetMediaTypeByIndex(i, &type));
        GUID subtype;
        UINT32 w = 0, h = 0;
        type->GetGUID(MF_MT_SUBTYPE, &subtype);
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
        wprintf(L"  [%lu] %s %ux%u\n", i, subtype == MFVideoFormat_NV12 ? L"NV12" : L"RGB32", w, h);
        if (subtype == MFVideoFormat_RGB32 && w == kWidth && h == kHeight) rgbType = type;
    }
    if (!rgbType) CHECK(MF_E_INVALIDMEDIATYPE);
    CHECK(handler->SetCurrentMediaType(rgbType.Get()));

    PROPVARIANT start;
    PropVariantInit(&start);
    CHECK(source->Start(pd.Get(), nullptr, &start));

    ComPtr<IMFMediaEvent> newStream;
    CHECK(WaitForEvent(source.Get(), MENewStream, &newStream));
    PROPVARIANT value;
    PropVariantInit(&value);
    CHECK(newStream->GetValue(&value));
    ComPtr<IMFMediaStream> stream;
    CHECK(value.punkVal->QueryInterface(IID_PPV_ARGS(&stream)));
    PropVariantClear(&value);

    ComPtr<IMFMediaEvent> event;
    CHECK(WaitForEvent(source.Get(), MESourceStarted, &event));
    CHECK(WaitForEvent(stream.Get(), MEStreamStarted, &event));

    // Pull a few samples to exercise pacing; keep the last one.
    // Optional duration in seconds, e.g. to give CameraEmulator.exe --update --screen time to attach.
    const int sampleCount = argc == 6 ? std::max(1, _wtoi(argv[5]) * 30) : 10;
    const LONGLONG before = MFGetSystemTime();
    ComPtr<IMFSample> sample;
    for (int i = 0; i < sampleCount; ++i)
    {
        CHECK(stream->RequestSample(nullptr));
        CHECK(WaitForEvent(stream.Get(), MEMediaSample, &event));
        CHECK(event->GetValue(&value));
        sample.Reset();
        CHECK(value.punkVal->QueryInterface(IID_PPV_ARGS(&sample)));
        PropVariantClear(&value);
    }
    const double elapsedMs = (MFGetSystemTime() - before) / 10000.0;
    wprintf(L"Received %d samples in %.0f ms (%.1f fps)\n", sampleCount, elapsedMs, sampleCount * 1000.0 / elapsedMs);

    ComPtr<IMFMediaBuffer> buffer;
    CHECK(sample->ConvertToContiguousBuffer(&buffer));
    BYTE* data = nullptr;
    DWORD length = 0;
    CHECK(buffer->Lock(&data, nullptr, &length));
    wprintf(L"Sample size: %lu bytes (expected %u)\n", length, kWidth * kHeight * 4u);
    const bool written = length == kWidth * kHeight * 4u && WriteBmp(argv[2], data, kWidth, kHeight);
    buffer->Unlock();
    if (written) wprintf(L"Wrote %s\n", argv[2]);

    CHECK(source->Stop());
    CHECK(activate->ShutdownObject());
    stream.Reset();
    sample.Reset();
    buffer.Reset();
    event.Reset();
    newStream.Reset();
    source.Reset();
    activate.Reset();
    factory.Reset();

    MFShutdown();
    CoUninitialize();
    return written ? 0 : 1;
}
