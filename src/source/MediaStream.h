#pragma once

#include "Common.h"
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

// The single video stream of the virtual camera. Sample requests are queued and served by a
// worker thread that paces delivery to kFrameRate.
class MediaStream : public RuntimeClass<RuntimeClassFlags<ClassicCom>,
    ChainInterfaces<IMFMediaStream2, IMFMediaStream, IMFMediaEventGenerator>,
    IKsControl>
{
public:
    HRESULT RuntimeClassInitialize(IMFMediaSource* source, DWORD streamId);
    ~MediaStream() override;

    // IMFMediaEventGenerator
    IFACEMETHODIMP BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state) override;
    IFACEMETHODIMP EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event) override;
    IFACEMETHODIMP GetEvent(DWORD flags, IMFMediaEvent** event) override;
    IFACEMETHODIMP QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* eventValue) override;

    // IMFMediaStream
    IFACEMETHODIMP GetMediaSource(IMFMediaSource** source) override;
    IFACEMETHODIMP GetStreamDescriptor(IMFStreamDescriptor** descriptor) override;
    IFACEMETHODIMP RequestSample(IUnknown* token) override;

    // IMFMediaStream2
    IFACEMETHODIMP SetStreamState(MF_STREAM_STATE state) override;
    IFACEMETHODIMP GetStreamState(MF_STREAM_STATE* state) override;

    // IKsControl
    IFACEMETHODIMP_(NTSTATUS) KsProperty(PKSPROPERTY property, ULONG propertyLength, PVOID data, ULONG dataLength, ULONG* bytesReturned) override;
    IFACEMETHODIMP_(NTSTATUS) KsMethod(PKSMETHOD method, ULONG methodLength, PVOID data, ULONG dataLength, ULONG* bytesReturned) override;
    IFACEMETHODIMP_(NTSTATUS) KsEvent(PKSEVENT event, ULONG eventLength, PVOID data, ULONG dataLength, ULONG* bytesReturned) override;

    // Called by MediaSource.
    HRESULT Start();
    HRESULT Stop();
    void Shutdown();

private:
    void StopWorker();
    void WorkerLoop();

    std::mutex m_mutex;
    std::condition_variable m_cv;
    ComPtr<IMFMediaSource> m_source; // released on Shutdown to break the reference cycle
    ComPtr<IMFMediaEventQueue> m_queue;
    ComPtr<IMFStreamDescriptor> m_descriptor;
    MF_STREAM_STATE m_state = MF_STREAM_STATE_STOPPED;
    bool m_shutdown = false;

    // Format negotiated at Start, read by the worker.
    GUID m_subtype = GUID_NULL;
    UINT32 m_width = 0;
    UINT32 m_height = 0;

    std::thread m_worker;
    bool m_stopWorker = false;
    std::deque<ComPtr<IUnknown>> m_tokens;
};
