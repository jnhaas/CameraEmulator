#include "Activator.h"
#include "MediaSource.h"

using Microsoft::WRL::MakeAndInitialize;

namespace
{
    // WRL's default factory skips RuntimeClassInitialize; this one runs it.
    class ActivatorFactory : public Microsoft::WRL::ClassFactory<>
    {
    public:
        IFACEMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** object) override
        {
            if (!object) return E_POINTER;
            *object = nullptr;
            if (outer) return CLASS_E_NOAGGREGATION;
            ComPtr<Activator> activator;
            RETURN_IF_FAILED(MakeAndInitialize<Activator>(&activator));
            return activator.CopyTo(riid, object);
        }
    };
}

HRESULT Activator::RuntimeClassInitialize()
{
    return MFCreateAttributes(&m_attributes, 4);
}

IFACEMETHODIMP Activator::ActivateObject(REFIID riid, void** object)
{
    if (!object) return E_POINTER;
    *object = nullptr;
    if (!m_source)
    {
        HRESULT hr = MakeAndInitialize<MediaSource>(&m_source, m_attributes.Get());
        if (FAILED(hr))
        {
            Trace(L"Failed to create media source: 0x%08X", hr);
            return hr;
        }
    }
    return m_source->QueryInterface(riid, object);
}

IFACEMETHODIMP Activator::ShutdownObject()
{
    if (m_source)
    {
        m_source->Shutdown();
        m_source.Reset();
    }
    return S_OK;
}

IFACEMETHODIMP Activator::DetachObject()
{
    m_source.Reset();
    return S_OK;
}

CoCreatableClassWithFactory(Activator, ActivatorFactory);
