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

// Goes to OutputDebugString (DebugView) and, best effort, to %ProgramData%\CameraEmulator\source.log.
void Trace(const wchar_t* format, ...)
{
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t message[512];
    const int prefix = swprintf_s(message, L"[CameraEmulator] %02u:%02u:%02u.%03u pid %lu: ",
        now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCurrentProcessId());
    va_list args;
    va_start(args, format);
    vswprintf_s(message + prefix, ARRAYSIZE(message) - prefix - 2, format, args);
    va_end(args);
    wcscat_s(message, L"\r\n");
    OutputDebugStringW(message);

    static const std::wstring logPath = CameraEmulator::SharedFolder() + L"\\source.log";
    HANDLE log = CreateFileW(logPath.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log != INVALID_HANDLE_VALUE)
    {
        char utf8[1024];
        const int length = WideCharToMultiByte(CP_UTF8, 0, message, -1, utf8, sizeof(utf8), nullptr, nullptr);
        DWORD written = 0;
        if (length > 1) WriteFile(log, utf8, static_cast<DWORD>(length - 1), &written, nullptr);
        CloseHandle(log);
    }
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
