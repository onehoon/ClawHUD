#include "IntelGraphicsApiProbe.h"
#include "RuntimeLogger.h"

#include <algorithm>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <vector>

namespace clawhud
{
namespace
{
using Result = std::uint32_t;
using ApiHandle = void*;
using DeviceHandle = void*;

using InitFn = Result(__cdecl*)(void*, ApiHandle*);
using CloseFn = Result(__cdecl*)(ApiHandle);
using EnumerateDevicesFn = Result(__cdecl*)(ApiHandle, std::uint32_t*, DeviceHandle*);
using GetSet3DFn = Result(__cdecl*)(DeviceHandle, void*);

constexpr Result kSuccess = 0;
constexpr std::uint32_t kIgclApiVersion = (1u << 16) | 1u;
constexpr std::uint32_t kLiveStateFeature = 19;
constexpr std::uint32_t kCustomValueType = 5;
constexpr std::uint32_t kDx9 = 1u << 0;
constexpr std::uint32_t kDx11 = 1u << 1;
constexpr std::uint32_t kDx12 = 1u << 2;
constexpr std::uint32_t kVulkan = 1u << 3;

struct ApplicationId
{
    std::uint32_t data1{};
    std::uint16_t data2{};
    std::uint16_t data3{};
    std::uint8_t data4[8]{};
};

struct InitArgs
{
    std::uint32_t size{};
    std::uint8_t version{};
    std::uint32_t appVersion{};
    std::uint32_t flags{};
    std::uint32_t supportedVersion{};
    ApplicationId applicationUid{};
};

struct FeatureRequest
{
    std::uint32_t size{};
    std::uint8_t version{};
    std::uint32_t featureType{};
    char* applicationName{};
    std::int8_t applicationNameLength{};
    bool set{};
    std::uint32_t valueType{};
    std::uint64_t value{};
    std::int32_t customValueSize{};
    void* customValue{};
};

struct LiveState
{
    std::uint32_t gfxApi{};
    std::uint32_t targetFps{};
    std::uint32_t framePacingStatus{};
    std::uint32_t reserved[4]{};
};

template <typename T>
T Resolve(HMODULE module, const char* name)
{
    return reinterpret_cast<T>(GetProcAddress(module, name));
}

void Debug(const std::wstring& message)
{
    clawhud::RuntimeLogger::Log(clawhud::RuntimeLogLevel::Info, message);
}

std::wstring HexMask(std::uint32_t value)
{
    std::wostringstream stream;
    stream << L"0x" << std::uppercase << std::hex
        << std::setw(8) << std::setfill(L'0') << value;
    return stream.str();
}

std::optional<std::string> ProcessApplicationName(DWORD processId)
{
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return std::nullopt;

    std::vector<wchar_t> path(32768);
    DWORD length = static_cast<DWORD>(path.size());
    const bool queried = QueryFullProcessImageNameW(process, 0, path.data(), &length);
    CloseHandle(process);
    if (!queried || !length)
        return std::nullopt;

    const std::wstring filename = std::filesystem::path(
        path.data(), path.data() + length).filename().wstring();
    if (filename.empty())
        return std::nullopt;

    const int required = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS,
        filename.data(), static_cast<int>(filename.size()), nullptr, 0, nullptr, nullptr);
    if (required <= 0 || required > 127)
        return std::nullopt;
    std::string result(required, '\0');
    if (!WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, filename.data(),
        static_cast<int>(filename.size()), result.data(), required, nullptr, nullptr))
        return std::nullopt;
    return result;
}
}

IntelGraphicsApiState DecodeGraphicsApiMask(std::uint32_t rawMask) noexcept
{
    return {rawMask, (rawMask & kDx9) != 0, (rawMask & kDx11) != 0,
        (rawMask & kDx12) != 0, (rawMask & kVulkan) != 0};
}

std::optional<std::wstring> ResolveGraphicsApi(
    const IntelGraphicsApiState& state)
{
    const int active = static_cast<int>(state.dx9) +
        static_cast<int>(state.dx11) + static_cast<int>(state.dx12) +
        static_cast<int>(state.vulkan);
    if (active != 1)
        return std::nullopt;
    if (state.dx12) return L"DX12";
    if (state.dx11) return L"DX11";
    if (state.dx9) return L"DX9";
    return L"Vulkan";
}

IntelGraphicsApiProbe::~IntelGraphicsApiProbe()
{
    Shutdown();
}

void IntelGraphicsApiProbe::Reset() noexcept
{
    Shutdown();
}

bool IntelGraphicsApiProbe::Initialize()
{
    if (apiHandle_)
        return true;
    library_ = LoadLibraryExW(L"ControlLib.dll", nullptr,
        LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library_)
        return false;

    const auto init = Resolve<InitFn>(library_, "ctlInit");
    const auto close = Resolve<CloseFn>(library_, "ctlClose");
    const auto enumerate = Resolve<EnumerateDevicesFn>(library_, "ctlEnumerateDevices");
    const auto getSet = Resolve<GetSet3DFn>(library_, "ctlGetSet3DFeature");
    if (!init || !close || !enumerate || !getSet)
    {
        Shutdown();
        return false;
    }

    InitArgs args{};
    args.size = sizeof(args);
    args.appVersion = kIgclApiVersion;
    if (init(&args, &apiHandle_) != kSuccess)
    {
        Shutdown();
        return false;
    }
    return true;
}

void IntelGraphicsApiProbe::Shutdown() noexcept
{
    if (apiHandle_ && library_)
    {
        if (const auto close = Resolve<CloseFn>(library_, "ctlClose"))
            close(apiHandle_);
    }
    apiHandle_ = nullptr;
    if (library_)
        FreeLibrary(library_);
    library_ = nullptr;
}

std::optional<std::wstring> IntelGraphicsApiProbe::Query(DWORD processId)
{
    const auto appName = ProcessApplicationName(processId);
    if (!appName || !Initialize())
        return std::nullopt;

    Debug(L"IGCL API probe: PID=" + std::to_wstring(processId) +
        L" app=" + std::wstring(appName->begin(), appName->end()));

    const auto enumerate = Resolve<EnumerateDevicesFn>(library_, "ctlEnumerateDevices");
    const auto getSet = Resolve<GetSet3DFn>(library_, "ctlGetSet3DFeature");
    std::uint32_t count{};
    if (!enumerate || !getSet || enumerate(apiHandle_, &count, nullptr) != kSuccess || !count)
        return std::nullopt;
    std::vector<DeviceHandle> adapters(count);
    if (enumerate(apiHandle_, &count, adapters.data()) != kSuccess)
        return std::nullopt;

    std::optional<std::wstring> resolved;
    bool invalidState = false;
    for (std::uint32_t index = 0; index < count; ++index)
    {
        LiveState liveState{};
        FeatureRequest request{};
        request.size = sizeof(request);
        request.featureType = kLiveStateFeature;
        request.applicationName = const_cast<char*>(appName->data());
        request.applicationNameLength = static_cast<std::int8_t>(appName->size());
        request.valueType = kCustomValueType;
        request.customValueSize = sizeof(liveState);
        request.customValue = &liveState;
        const Result result = getSet(adapters[index], &request);
        if (result != kSuccess)
            continue;

        const auto state = DecodeGraphicsApiMask(liveState.gfxApi);
        Debug(L"IGCL Adapter[" + std::to_wstring(index) + L"] GfxApi=" +
            HexMask(liveState.gfxApi));
        const auto api = ResolveGraphicsApi(state);
        if (!api)
        {
            Debug(L"IGCL Graphics API unresolved: zero or mixed mask");
            invalidState = true;
            continue;
        }
        if (resolved && *resolved != *api)
        {
            Debug(L"IGCL Graphics API unresolved: adapter disagreement");
            return std::nullopt;
        }
        resolved = api;
    }
    if (invalidState)
        return std::nullopt;
    if (resolved)
        Debug(L"Graphics API resolved: " + *resolved);
    return resolved;
}
}
