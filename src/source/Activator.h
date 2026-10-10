#pragma once

#include "Common.h"

class MediaSource;

// COM class registered under CAMERAEMULATOR_CLSID. Frame Server creates it and calls
// ActivateObject to obtain the media source.
class __declspec(uuid(CAMERAEMULATOR_CLSID_UUID)) Activator
    : public RuntimeClass<RuntimeClassFlags<ClassicCom>, ChainInterfaces<IMFActivate, IMFAttributes>>
{
public:
    HRESULT RuntimeClassInitialize();

    // IMFActivate
    IFACEMETHODIMP ActivateObject(REFIID riid, void** object) override;
    IFACEMETHODIMP ShutdownObject() override;
    IFACEMETHODIMP DetachObject() override;

    // IMFAttributes, forwarded to an internal store.
    IFACEMETHODIMP GetItem(REFGUID key, PROPVARIANT* data) override { return Store()->GetItem(key, data); }
    IFACEMETHODIMP GetItemType(REFGUID key, MF_ATTRIBUTE_TYPE* type) override { return Store()->GetItemType(key, type); }
    IFACEMETHODIMP CompareItem(REFGUID key, REFPROPVARIANT data, BOOL* result) override { return Store()->CompareItem(key, data, result); }
    IFACEMETHODIMP Compare(IMFAttributes* theirs, MF_ATTRIBUTES_MATCH_TYPE type, BOOL* result) override { return Store()->Compare(theirs, type, result); }
    IFACEMETHODIMP GetUINT32(REFGUID key, UINT32* data) override { return Store()->GetUINT32(key, data); }
    IFACEMETHODIMP GetUINT64(REFGUID key, UINT64* data) override { return Store()->GetUINT64(key, data); }
    IFACEMETHODIMP GetDouble(REFGUID key, double* data) override { return Store()->GetDouble(key, data); }
    IFACEMETHODIMP GetGUID(REFGUID key, GUID* data) override { return Store()->GetGUID(key, data); }
    IFACEMETHODIMP GetStringLength(REFGUID key, UINT32* length) override { return Store()->GetStringLength(key, length); }
    IFACEMETHODIMP GetString(REFGUID key, LPWSTR data, UINT32 size, UINT32* length) override { return Store()->GetString(key, data, size, length); }
    IFACEMETHODIMP GetAllocatedString(REFGUID key, LPWSTR* data, UINT32* length) override { return Store()->GetAllocatedString(key, data, length); }
    IFACEMETHODIMP GetBlobSize(REFGUID key, UINT32* size) override { return Store()->GetBlobSize(key, size); }
    IFACEMETHODIMP GetBlob(REFGUID key, UINT8* buffer, UINT32 size, UINT32* blobSize) override { return Store()->GetBlob(key, buffer, size, blobSize); }
    IFACEMETHODIMP GetAllocatedBlob(REFGUID key, UINT8** buffer, UINT32* size) override { return Store()->GetAllocatedBlob(key, buffer, size); }
    IFACEMETHODIMP GetUnknown(REFGUID key, REFIID riid, LPVOID* object) override { return Store()->GetUnknown(key, riid, object); }
    IFACEMETHODIMP SetItem(REFGUID key, REFPROPVARIANT data) override { return Store()->SetItem(key, data); }
    IFACEMETHODIMP DeleteItem(REFGUID key) override { return Store()->DeleteItem(key); }
    IFACEMETHODIMP DeleteAllItems() override { return Store()->DeleteAllItems(); }
    IFACEMETHODIMP SetUINT32(REFGUID key, UINT32 data) override { return Store()->SetUINT32(key, data); }
    IFACEMETHODIMP SetUINT64(REFGUID key, UINT64 data) override { return Store()->SetUINT64(key, data); }
    IFACEMETHODIMP SetDouble(REFGUID key, double data) override { return Store()->SetDouble(key, data); }
    IFACEMETHODIMP SetGUID(REFGUID key, REFGUID data) override { return Store()->SetGUID(key, data); }
    IFACEMETHODIMP SetString(REFGUID key, LPCWSTR data) override { return Store()->SetString(key, data); }
    IFACEMETHODIMP SetBlob(REFGUID key, const UINT8* buffer, UINT32 size) override { return Store()->SetBlob(key, buffer, size); }
    IFACEMETHODIMP SetUnknown(REFGUID key, IUnknown* data) override { return Store()->SetUnknown(key, data); }
    IFACEMETHODIMP LockStore() override { return Store()->LockStore(); }
    IFACEMETHODIMP UnlockStore() override { return Store()->UnlockStore(); }
    IFACEMETHODIMP GetCount(UINT32* count) override { return Store()->GetCount(count); }
    IFACEMETHODIMP GetItemByIndex(UINT32 index, GUID* key, PROPVARIANT* data) override { return Store()->GetItemByIndex(index, key, data); }
    IFACEMETHODIMP CopyAllItems(IMFAttributes* destination) override { return Store()->CopyAllItems(destination); }

private:
    IMFAttributes* Store() const { return m_attributes.Get(); }

    ComPtr<IMFAttributes> m_attributes;
    ComPtr<MediaSource> m_source;
};
