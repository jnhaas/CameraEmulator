#include "Common.h"
#include <cstdarg>
#include <cstdio>
#include <string>

using Microsoft::WRL::InProc;
using Microsoft::WRL::Module;

namespace
{
    HMODULE g_module = nullptr;

    constexpr wchar_t kClsidKey[] = L"SOFTWARE\\Classes\\CLSID\\" CAMERAEMULATOR_CLSID_STRING;

    LSTATUS SetStringValue(HKEY key, const wchar_t* name, const std::wstring& value)
    {
        return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
            static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    }
}

// Viewable with DebugView (Capture Global Win32 enabled, since we run inside a service).
void Trace(const wchar_t* format, ...)
{
    wchar_t message[512];
    int prefix = swprintf_s(message, L"[CameraEmulator] ");
    va_list args;
    va_start(args, format);
    vswprintf_s(message + prefix, ARRAYSIZE(message) - prefix - 1, format, args);
    va_end(args);
    wcscat_s(message, L"\n");
    OutputDebugStringW(message);
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_module = module;
        DisableThreadLibraryCalls(module);
    }
    return TRUE;
}

STDAPI DllGetClassObject(REFCLSID clsid, REFIID riid, LPVOID* object)
{
    return Module<InProc>::GetModule().GetClassObject(clsid, riid, object);
}

STDAPI DllCanUnloadNow()
{
    return Module<InProc>::GetModule().Terminate() ? S_OK : S_FALSE;
}

// Frame Server runs as a service, so the class must be registered machine-wide (HKLM).
STDAPI DllRegisterServer()
{
    wchar_t path[MAX_PATH];
    const DWORD length = GetModuleFileNameW(g_module, path, ARRAYSIZE(path));
    if (length == 0 || length == ARRAYSIZE(path)) return HRESULT_FROM_WIN32(GetLastError());

    HKEY clsidKey = nullptr;
    LSTATUS status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, kClsidKey, 0, nullptr, 0, KEY_WRITE, nullptr, &clsidKey, nullptr);
    if (status != ERROR_SUCCESS) return HRESULT_FROM_WIN32(status);
    status = SetStringValue(clsidKey, nullptr, L"CameraEmulator Virtual Camera Source");

    HKEY serverKey = nullptr;
    if (status == ERROR_SUCCESS)
    {
        status = RegCreateKeyExW(clsidKey, L"InprocServer32", 0, nullptr, 0, KEY_WRITE, nullptr, &serverKey, nullptr);
    }
    if (status == ERROR_SUCCESS) status = SetStringValue(serverKey, nullptr, path);
    if (status == ERROR_SUCCESS) status = SetStringValue(serverKey, L"ThreadingModel", L"Both");

    if (serverKey) RegCloseKey(serverKey);
    RegCloseKey(clsidKey);
    return HRESULT_FROM_WIN32(status);
}

STDAPI DllUnregisterServer()
{
    const LSTATUS status = RegDeleteTreeW(HKEY_LOCAL_MACHINE, kClsidKey);
    return status == ERROR_FILE_NOT_FOUND ? S_OK : HRESULT_FROM_WIN32(status);
}
