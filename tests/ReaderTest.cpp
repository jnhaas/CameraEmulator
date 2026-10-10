// Reads one frame from the registered CameraEmulator virtual camera the way the Android
// emulator does (Media Foundation source reader with video processing, RGB32 output at the
// requested size) and writes it to a BMP. Requires CameraEmulator.exe to be running.
//
//   ReaderTest.exe <width> <height> <output.bmp>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <mfreadwrite.h>
#include <wrl/client.h>
#include <cstdio>
#include <cwchar>
#include <cstdlib>

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
    info.biHeight = -static_cast<LONG>(height);
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
    if (argc != 4)
    {
        fwprintf(stderr, L"Usage: %s <width> <height> <output.bmp>\n", argv[0]);
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    const UINT32 width = static_cast<UINT32>(_wtoi(argv[1]));
    const UINT32 height = static_cast<UINT32>(_wtoi(argv[2]));

    CHECK(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    CHECK(MFStartup(MF_VERSION));
    bool written = false;
    {

    ComPtr<IMFAttributes> query;
    CHECK(MFCreateAttributes(&query, 1));
    CHECK(query->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID));
    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    CHECK(MFEnumDeviceSources(query.Get(), &devices, &count));

    ComPtr<IMFMediaSource> source;
    for (UINT32 i = 0; i < count; ++i)
    {
        wchar_t* name = nullptr;
        UINT32 length = 0;
        if (!source && SUCCEEDED(devices[i]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &length)) &&
            wcsstr(name, L"CameraEmulator"))
        {
            devices[i]->ActivateObject(IID_PPV_ARGS(&source));
        }
        CoTaskMemFree(name);
        devices[i]->Release();
    }
    CoTaskMemFree(devices);
    if (!source)
    {
        fwprintf(stderr, L"CameraEmulator camera not found (is CameraEmulator.exe running?)\n");
        return 1;
    }

    // Like the emulator: use the exact native size if offered, else the largest native type.
    ComPtr<IMFPresentationDescriptor> pd;
    CHECK(source->CreatePresentationDescriptor(&pd));
    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> sd;
    CHECK(pd->GetStreamDescriptorByIndex(0, &selected, &sd));
    ComPtr<IMFMediaTypeHandler> handler;
    CHECK(sd->GetMediaTypeHandler(&handler));
    DWORD typeCount = 0;
    CHECK(handler->GetMediaTypeCount(&typeCount));
    ComPtr<IMFMediaType> best;
    UINT64 bestScore = 0;
    for (DWORD i = 0; i < typeCount; ++i)
    {
        ComPtr<IMFMediaType> type;
        CHECK(handler->GetMediaTypeByIndex(i, &type));
        GUID subtype;
        UINT32 w = 0, h = 0;
        type->GetGUID(MF_MT_SUBTYPE, &subtype);
        MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &w, &h);
        if (subtype != MFVideoFormat_NV12) continue;
        const UINT64 score = (w == width && h == height ? 1'000'000'000ull : 0) + static_cast<UINT64>(w) * h;
        if (score > bestScore)
        {
            bestScore = score;
            best = type;
        }
    }
    if (!best) CHECK(MF_E_INVALIDMEDIATYPE);
    UINT32 nativeWidth = 0, nativeHeight = 0;
    MFGetAttributeSize(best.Get(), MF_MT_FRAME_SIZE, &nativeWidth, &nativeHeight);
    wprintf(L"Requested %ux%u, native type %ux%u NV12\n", width, height, nativeWidth, nativeHeight);
    CHECK(handler->SetCurrentMediaType(best.Get()));

    ComPtr<IMFAttributes> readerAttributes;
    CHECK(MFCreateAttributes(&readerAttributes, 1));
    CHECK(readerAttributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE));
    ComPtr<IMFSourceReader> reader;
    CHECK(MFCreateSourceReaderFromMediaSource(source.Get(), readerAttributes.Get(), &reader));

    ComPtr<IMFMediaType> output;
    CHECK(MFCreateMediaType(&output));
    CHECK(output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video));
    CHECK(output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32));
    CHECK(MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, width, height));
    CHECK(reader->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, output.Get()));

    ComPtr<IMFSample> sample;
    for (int attempt = 0; attempt < 30 && !sample; ++attempt)
    {
        DWORD stream = 0, flags = 0;
        LONGLONG timestamp = 0;
        CHECK(reader->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0, &stream, &flags, &timestamp, &sample));
    }
    if (!sample) CHECK(E_FAIL);

    wprintf(L"Got sample\n");
    ComPtr<IMFMediaBuffer> buffer;
    CHECK(sample->ConvertToContiguousBuffer(&buffer));
    BYTE* data = nullptr;
    DWORD length = 0;
    CHECK(buffer->Lock(&data, nullptr, &length));
    written = length >= width * height * 4 && WriteBmp(argv[3], data, width, height);
    buffer->Unlock();
    wprintf(written ? L"Wrote %s\n" : L"Unexpected buffer size for %s\n", argv[3]);

    reader.Reset();
    source->Shutdown();
    } // all MF objects released before shutdown
    MFShutdown();
    CoUninitialize();
    return written ? 0 : 1;
}
