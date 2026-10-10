#pragma once

#include <windows.h>

#include <mfapi.h>
#include <mfidl.h>
#include <mferror.h>
#include <ks.h>
#include <ksmedia.h>
#include <ksproxy.h> // IKsControl (user mode)
#include <wrl/client.h>
#include <wrl/implements.h>
#include <wrl/module.h>

#include "Shared.h"

using Microsoft::WRL::ChainInterfaces;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

#define RETURN_IF_FAILED(expr)            \
    do                                    \
    {                                     \
        const HRESULT hr_ = (expr);       \
        if (FAILED(hr_)) return hr_;      \
    } while (0)

// Frame Server queries a few KS property sets; we support none of them.
#define KS_NOT_SUPPORTED HRESULT_FROM_WIN32(ERROR_SET_NOT_FOUND)

// Output frame rate of the virtual camera.
constexpr UINT32 kFrameRate = 30;
constexpr LONGLONG kFrameDuration = 10'000'000 / kFrameRate; // 100 ns units

void Trace(const wchar_t* format, ...);
