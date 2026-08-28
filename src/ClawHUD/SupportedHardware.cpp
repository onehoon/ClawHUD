#include "SupportedHardware.h"
#include "RuntimeLogger.h"

#include <Windows.h>
#include <Wbemidl.h>

#include <cwctype>
#include <cwchar>
#include <string>

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace
{
constexpr std::wstring_view kSupportedBoards[] =
{
    L"MS-1T42",
    L"MS-1T52",
    L"MS-1T91",
};

void Log(const std::wstring& message)
{
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, message);
}

void LogWarn(const std::wstring& message)
{
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Warn, message);
}

std::wstring TrimWhitespace(std::wstring_view value)
{
    std::size_t first = 0;
    while (first < value.size() && std::iswspace(value[first]))
        ++first;

    std::size_t last = value.size();
    while (last > first && std::iswspace(value[last - 1]))
        --last;

    return std::wstring(value.substr(first, last - first));
}

class ComInitialization
{
public:
    ComInitialization() = default;

    bool Initialize()
    {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(result))
            return false;

        initialized_ = true;
        const HRESULT security = CoInitializeSecurity(
            nullptr,
            -1,
            nullptr,
            nullptr,
            RPC_C_AUTHN_LEVEL_DEFAULT,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE,
            nullptr);
        return SUCCEEDED(security) || security == RPC_E_TOO_LATE;
    }

    ~ComInitialization()
    {
        if (initialized_)
            CoUninitialize();
    }

    ComInitialization(const ComInitialization&) = delete;
    ComInitialization& operator=(const ComInitialization&) = delete;

private:
    bool initialized_{};
};
}

HardwareSupport ClassifyBaseBoardProduct(std::wstring_view boardProduct)
{
    const auto trimmed = TrimWhitespace(boardProduct);
    if (trimmed.empty())
        return HardwareSupport::Indeterminate;

    for (const auto supported : kSupportedBoards)
    {
        if (_wcsicmp(trimmed.c_str(), std::wstring(supported).c_str()) == 0)
            return HardwareSupport::Supported;
    }

    return HardwareSupport::Unsupported;
}

HardwareSupport CheckSupportedHardware()
{
    ComInitialization com;
    if (!com.Initialize())
    {
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return HardwareSupport::Indeterminate;
    }

    ComPtr<IWbemLocator> locator;
    if (FAILED(CoCreateInstance(
            CLSID_WbemLocator,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&locator))))
    {
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return HardwareSupport::Indeterminate;
    }

    ComPtr<IWbemServices> services;
    BSTR namespaceName = SysAllocString(L"ROOT\\CIMV2");
    const HRESULT connectResult = namespaceName
        ? locator->ConnectServer(namespaceName, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &services)
        : E_OUTOFMEMORY;
    SysFreeString(namespaceName);
    if (FAILED(connectResult) || !services || FAILED(CoSetProxyBlanket(
            services.Get(),
            RPC_C_AUTHN_WINNT,
            RPC_C_AUTHZ_NONE,
            nullptr,
            RPC_C_AUTHN_LEVEL_CALL,
            RPC_C_IMP_LEVEL_IMPERSONATE,
            nullptr,
            EOAC_NONE)))
    {
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return HardwareSupport::Indeterminate;
    }

    ComPtr<IEnumWbemClassObject> results;
    BSTR language = SysAllocString(L"WQL");
    BSTR query = SysAllocString(L"SELECT Product FROM Win32_BaseBoard");
    const HRESULT queryResult = language && query
        ? services->ExecQuery(language, query, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
            nullptr, &results)
        : E_OUTOFMEMORY;
    SysFreeString(language);
    SysFreeString(query);
    if (FAILED(queryResult) || !results)
    {
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return HardwareSupport::Indeterminate;
    }

    ComPtr<IWbemClassObject> board;
    ULONG returned = 0;
    if (results->Next(WBEM_INFINITE, 1, &board, &returned) != S_OK || returned != 1 || !board)
    {
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return HardwareSupport::Indeterminate;
    }

    VARIANT product{};
    const HRESULT productResult = board->Get(L"Product", 0, &product, nullptr, nullptr);
    if (FAILED(productResult) || V_VT(&product) != VT_BSTR || !V_BSTR(&product))
    {
        VariantClear(&product);
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
        return HardwareSupport::Indeterminate;
    }

    const std::wstring boardProduct(V_BSTR(&product), SysStringLen(V_BSTR(&product)));
    VariantClear(&product);
    const auto status = ClassifyBaseBoardProduct(boardProduct);
    if (status == HardwareSupport::Supported)
        Log(L"Hardware supported board=" + TrimWhitespace(boardProduct));
    else if (status == HardwareSupport::Unsupported)
        Log(L"Unsupported BaseBoard Product=" + TrimWhitespace(boardProduct));
    else
        LogWarn(L"BaseBoard Product unavailable; startup aborted");
    return status;
}
