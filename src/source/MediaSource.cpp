#include "MediaSource.h"

using Microsoft::WRL::MakeAndInitialize;

HRESULT MediaSource::RuntimeClassInitialize(IMFAttributes* activatorAttributes)
{
    RETURN_IF_FAILED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
    m_mfStarted = true;

    RETURN_IF_FAILED(MFCreateEventQueue(&m_queue));
    RETURN_IF_FAILED(MFCreateAttributes(&m_attributes, 4));
    if (activatorAttributes) RETURN_IF_FAILED(activatorAttributes->CopyAllItems(m_attributes.Get()));

    RETURN_IF_FAILED(MakeAndInitialize<MediaStream>(&m_stream, this, 0));
    ComPtr<IMFStreamDescriptor> streamDescriptor;
    RETURN_IF_FAILED(m_stream->GetStreamDescriptor(&streamDescriptor));
    IMFStreamDescriptor* descriptors[] = {streamDescriptor.Get()};
    RETURN_IF_FAILED(MFCreatePresentationDescriptor(ARRAYSIZE(descriptors), descriptors, &m_presentation));
    RETURN_IF_FAILED(m_presentation->SelectStream(0));

    Trace(L"Media source created");
    return S_OK;
}

MediaSource::~MediaSource()
{
    Shutdown();
    if (m_mfStarted) MFShutdown();
}

// IMFMediaEventGenerator ---------------------------------------------------------------------

IFACEMETHODIMP MediaSource::BeginGetEvent(IMFAsyncCallback* callback, IUnknown* state)
{
    return m_queue->BeginGetEvent(callback, state);
}

IFACEMETHODIMP MediaSource::EndGetEvent(IMFAsyncResult* result, IMFMediaEvent** event)
{
    return m_queue->EndGetEvent(result, event);
}

IFACEMETHODIMP MediaSource::GetEvent(DWORD flags, IMFMediaEvent** event)
{
    return m_queue->GetEvent(flags, event);
}

IFACEMETHODIMP MediaSource::QueueEvent(MediaEventType type, REFGUID extendedType, HRESULT status, const PROPVARIANT* eventValue)
{
    return m_queue->QueueEventParamVar(type, extendedType, status, eventValue);
}

// IMFMediaSource -----------------------------------------------------------------------------

IFACEMETHODIMP MediaSource::GetCharacteristics(DWORD* characteristics)
{
    if (!characteristics) return E_POINTER;
    std::lock_guard lock(m_mutex);
    if (m_shutdown) return MF_E_SHUTDOWN;
    *characteristics = MFMEDIASOURCE_IS_LIVE;
    return S_OK;
}

IFACEMETHODIMP MediaSource::CreatePresentationDescriptor(IMFPresentationDescriptor** descriptor)
{
    if (!descriptor) return E_POINTER;
    std::lock_guard lock(m_mutex);
    if (m_shutdown) return MF_E_SHUTDOWN;
    return m_presentation->Clone(descriptor);
}

IFACEMETHODIMP MediaSource::Start(IMFPresentationDescriptor* descriptor, const GUID* timeFormat, const PROPVARIANT*)
{
    if (!descriptor) return E_INVALIDARG;
    if (timeFormat && *timeFormat != GUID_NULL) return MF_E_UNSUPPORTED_TIME_FORMAT;

    std::lock_guard lock(m_mutex);
    if (m_shutdown) return MF_E_SHUTDOWN;

    // Cloned presentation descriptors share our stream descriptor, so the media type the
    // caller selected is visible to the stream.
    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> streamDescriptor;
    RETURN_IF_FAILED(descriptor->GetStreamDescriptorByIndex(0, &selected, &streamDescriptor));

    if (selected)
    {
        RETURN_IF_FAILED(m_presentation->SelectStream(0));
        RETURN_IF_FAILED(m_stream->Start());
        RETURN_IF_FAILED(m_queue->QueueEventParamUnk(m_streamAnnounced ? MEUpdatedStream : MENewStream,
            GUID_NULL, S_OK, static_cast<IMFMediaStream*>(m_stream.Get())));
        m_streamAnnounced = true;
    }
    else
    {
        RETURN_IF_FAILED(m_presentation->DeselectStream(0));
        if (m_streamAnnounced) RETURN_IF_FAILED(m_stream->Stop());
    }

    PROPVARIANT startTime;
    PropVariantInit(&startTime);
    startTime.vt = VT_I8;
    startTime.hVal.QuadPart = MFGetSystemTime();
    return m_queue->QueueEventParamVar(MESourceStarted, GUID_NULL, S_OK, &startTime);
}

IFACEMETHODIMP MediaSource::Stop()
{
    std::lock_guard lock(m_mutex);
    if (m_shutdown) return MF_E_SHUTDOWN;
    if (m_streamAnnounced) RETURN_IF_FAILED(m_stream->Stop());
    return m_queue->QueueEventParamVar(MESourceStopped, GUID_NULL, S_OK, nullptr);
}

IFACEMETHODIMP MediaSource::Pause()
{
    // Live cameras cannot pause.
    return MF_E_INVALID_STATE_TRANSITION;
}

IFACEMETHODIMP MediaSource::Shutdown()
{
    std::lock_guard lock(m_mutex);
    if (m_shutdown) return MF_E_SHUTDOWN;
    m_shutdown = true;
    if (m_stream)
    {
        m_stream->Shutdown();
        m_stream.Reset();
    }
    if (m_queue) m_queue->Shutdown();
    Trace(L"Media source shut down");
    return S_OK;
}

// IMFMediaSourceEx ---------------------------------------------------------------------------

IFACEMETHODIMP MediaSource::GetSourceAttributes(IMFAttributes** attributes)
{
    if (!attributes) return E_POINTER;
    std::lock_guard lock(m_mutex);
    if (m_shutdown) return MF_E_SHUTDOWN;
    return m_attributes.CopyTo(attributes);
}

IFACEMETHODIMP MediaSource::GetStreamAttributes(DWORD streamId, IMFAttributes** attributes)
{
    if (!attributes) return E_POINTER;
    if (streamId != 0) return MF_E_INVALIDSTREAMNUMBER;
    std::lock_guard lock(m_mutex);
    if (m_shutdown) return MF_E_SHUTDOWN;
    ComPtr<IMFStreamDescriptor> streamDescriptor;
    RETURN_IF_FAILED(m_stream->GetStreamDescriptor(&streamDescriptor));
    return streamDescriptor.CopyTo(attributes);
}

IFACEMETHODIMP MediaSource::SetD3DManager(IUnknown*)
{
    // We produce system-memory samples only.
    return S_OK;
}

// IMFGetService ------------------------------------------------------------------------------

IFACEMETHODIMP MediaSource::GetService(REFGUID, REFIID, LPVOID* object)
{
    if (!object) return E_POINTER;
    *object = nullptr;
    return MF_E_UNSUPPORTED_SERVICE;
}

// IKsControl ---------------------------------------------------------------------------------

IFACEMETHODIMP_(NTSTATUS) MediaSource::KsProperty(PKSPROPERTY, ULONG, PVOID, ULONG, ULONG*)
{
    return KS_NOT_SUPPORTED;
}

IFACEMETHODIMP_(NTSTATUS) MediaSource::KsMethod(PKSMETHOD, ULONG, PVOID, ULONG, ULONG*)
{
    return KS_NOT_SUPPORTED;
}

IFACEMETHODIMP_(NTSTATUS) MediaSource::KsEvent(PKSEVENT, ULONG, PVOID, ULONG, ULONG*)
{
    return KS_NOT_SUPPORTED;
}
