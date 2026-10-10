#pragma once

#include "Common.h"
#include "MediaStream.h"
#include <mutex>

// Live media source with one video stream, as required by MFCreateVirtualCamera.
class MediaSource : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    ChainInterfaces<IMFMediaSourceEx, IMFMediaSource, IMFMediaEventGenerator>,
    IMFGetService,
    IKsControl>
{
public:
    HRESULT RuntimeClassInitialize(IMFAttributes* activatorAttributes);
    ~MediaSource() override;

    // IMFMediaEventGenerator
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    IFACEMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* eventValue) override;

    // IMFMediaSource
    IFACEMETHODIMP GetCharacteristics(DWORD* characteristics) override;
    IFACEMETHODIMP CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor) override;
    IFACEMETHODIMP Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat, const PROPVARIANT* startPosition) override;
    IFACEMETHODIMP Stop() override;
    IFACEMETHODIMP Pause() override;
    IFACEMETHODIMP Shutdown() override;

    // IMFMediaSourceEx
    IFACEMETHODIMP GetSourceAttributes(IMFAttributes** attributes) override;
    IFACEMETHODIMP GetStreamAttributes(DWORD streamId, IMFAttributes** attributes) override;
    IFACEMETHODIMP SetD3DManager(IUnknown* manager) override;

    // IMFGetService
    IFACEMETHODIMP GetService(REFGUID service, REFIID riid, LPVOID* object) override;

    // IKsControl
    IFACEMETHODIMP_(NTSTATUS) KsProperty(PKSPROPERTY property, ULONG propertyLength, PVOID data, ULONG dataLength, ULONG* bytesReturned) override;
    IFACEMETHODIMP_(NTSTATUS) KsMethod(PKSMETHOD method, ULONG methodLength, PVOID data, ULONG dataLength, ULONG* bytesReturned) override;
    IFACEMETHODIMP_(NTSTATUS) KsEvent(PKSEVENT event, ULONG eventLength, PVOID data, ULONG dataLength, ULONG* bytesReturned) override;

private:
    std::mutex m_mutex;
    bool m_mfStarted = false;
    bool m_shutdown = false;
    bool m_streamAnnounced = false; // MENewStream already sent
    ComPtr<IMFMediaEventQueue> m_queue;
    ComPtr<IMFAttributes> m_attributes;
    ComPtr<IMFPresentationDescriptor> m_presentation;
    ComPtr<MediaStream> m_stream;
};
