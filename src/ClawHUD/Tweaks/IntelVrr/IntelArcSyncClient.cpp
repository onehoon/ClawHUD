#include "IntelArcSyncClient.h"

#include <windows.h>
#include <iomanip>
#include <sstream>

namespace clawhud
{
namespace
{
using Result = std::uint32_t; using Handle = void*;
using InitFn = Result(__cdecl*)(void*, Handle*); using CloseFn = Result(__cdecl*)(Handle);
using EnumDevicesFn = Result(__cdecl*)(Handle, std::uint32_t*, Handle*);
using EnumOutputsFn = Result(__cdecl*)(Handle, std::uint32_t*, Handle*);
using InfoFn = Result(__cdecl*)(Handle, void*); using ProfileFn = Result(__cdecl*)(Handle, void*);
struct InitArgs { std::uint32_t size{}; std::uint8_t version{}; std::uint32_t appVersion{}; std::uint32_t flags{}; std::uint32_t supportedVersion{}; GUID uid{}; };
struct Capability { std::uint32_t size{}; std::uint8_t version{}; bool supported{}; float minHz{}; float maxHz{}; std::uint32_t increase{}; std::uint32_t decrease{}; };
struct Profile { std::uint32_t size{}; std::uint8_t version{}; IntelArcSyncProfile profile{}; float maxHz{}; float minHz{}; std::uint32_t increase{}; std::uint32_t decrease{}; };
template<class T> T Resolve(HMODULE module, const char* name) { return reinterpret_cast<T>(GetProcAddress(module, name)); }
}

std::string IntelArcSyncCall::ToString() const
{
    std::ostringstream stream; stream << operation << ": " << resultName << " (0x" << std::hex << std::uppercase << rawResult << ")";
    if (!detail.empty()) stream << ", " << detail; return stream.str();
}

const char* IntelArcSyncProfileName(IntelArcSyncProfile profile)
{
    switch (profile) { case IntelArcSyncProfile::Recommended: return "RECOMMENDED"; case IntelArcSyncProfile::Excellent: return "EXCELLENT";
    case IntelArcSyncProfile::Good: return "GOOD"; case IntelArcSyncProfile::Compatible: return "COMPATIBLE"; case IntelArcSyncProfile::Off: return "OFF";
    case IntelArcSyncProfile::Vesa: return "VESA"; case IntelArcSyncProfile::Custom: return "CUSTOM"; default: return "INVALID/UNKNOWN"; }
}

std::string IntelArcSyncResultName(std::uint32_t result)
{
    switch (result) { case 0: return "CTL_RESULT_SUCCESS"; case 0x40000001: return "CTL_RESULT_ERROR_NOT_INITIALIZED"; case 0x40000002: return "CTL_RESULT_ERROR_ALREADY_INITIALIZED";
    case 0x40000003: return "CTL_RESULT_ERROR_DEVICE_LOST"; case 0x40000006: return "CTL_RESULT_ERROR_INSUFFICIENT_PERMISSIONS"; case 0x40000007: return "CTL_RESULT_ERROR_NOT_AVAILABLE";
    case 0x40000009: return "CTL_RESULT_ERROR_UNSUPPORTED_VERSION"; case 0x4000000A: return "CTL_RESULT_ERROR_UNSUPPORTED_FEATURE"; case 0x4000000B: return "CTL_RESULT_ERROR_INVALID_ARGUMENT";
    case 0x4000000C: return "CTL_RESULT_ERROR_INVALID_API_HANDLE"; case 0x4000000D: return "CTL_RESULT_ERROR_INVALID_NULL_HANDLE"; case 0x4000000E: return "CTL_RESULT_ERROR_INVALID_NULL_POINTER";
    case 0x4000000F: return "CTL_RESULT_ERROR_INVALID_SIZE"; default: return "UNKNOWN"; }
}

void IntelArcSyncClient::Record(const char* operation, std::uint32_t result, std::string detail)
{ calls_.push_back({ operation, result, IntelArcSyncResultName(result), std::move(detail) }); }

bool IntelArcSyncClient::Initialize()
{
    library_ = LoadLibraryExW(L"ControlLib.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32); if (!library_) return false;
    auto init = Resolve<InitFn>(static_cast<HMODULE>(library_), "ctlInit"); auto close = Resolve<CloseFn>(static_cast<HMODULE>(library_), "ctlClose");
    auto enumerate = Resolve<EnumDevicesFn>(static_cast<HMODULE>(library_), "ctlEnumerateDevices");
    if (!init || !close || !enumerate) { FreeLibrary(static_cast<HMODULE>(library_)); library_ = nullptr; return false; }
    InitArgs args{}; args.size = sizeof(args); args.appVersion = 0x00010000; Result result = init(&args, &apiHandle_); Record("ctlInit", result);
    if (result != 0 || !apiHandle_) { Shutdown(); return false; }
    std::uint32_t count{}; result = enumerate(apiHandle_, &count, nullptr); Record("ctlEnumerateDevices", result, "count=" + std::to_string(count));
    if (result != 0 || count == 0) return false; adapters_.resize(count); result = enumerate(apiHandle_, &count, adapters_.data()); Record("ctlEnumerateDevices", result);
    if (result != 0) { Shutdown(); return false; } return true;
}

std::vector<IntelDisplayOutput> IntelArcSyncClient::EnumerateDisplayOutputs()
{
    std::vector<IntelDisplayOutput> result; if (!apiHandle_ || !library_) return result; auto enumerate = Resolve<EnumOutputsFn>(static_cast<HMODULE>(library_), "ctlEnumerateDisplayOutputs"); if (!enumerate) return result;
    for (auto adapter : adapters_) { std::uint32_t count{}; auto code = enumerate(adapter, &count, nullptr); Record("ctlEnumerateDisplayOutputs", code, "count=" + std::to_string(count)); if (code != 0 || !count) continue;
        std::vector<void*> outputs(count); code = enumerate(adapter, &count, outputs.data()); Record("ctlEnumerateDisplayOutputs", code); if (code == 0) for (auto output : outputs) result.push_back({ adapter, output, {} }); }
    return result;
}

bool IntelArcSyncClient::GetMonitorCapability(const IntelDisplayOutput& output, IntelArcSyncCapability& value)
{
    auto fn = Resolve<InfoFn>(static_cast<HMODULE>(library_), "ctlGetIntelArcSyncInfoForMonitor"); if (!fn) return false; Capability native{}; native.size = sizeof(native); auto code = fn(output.output, &native); Record("ctlGetIntelArcSyncInfoForMonitor", code); if (code != 0) return false;
    value = { native.supported, native.minHz, native.maxHz, native.increase, native.decrease }; return true;
}
bool IntelArcSyncClient::GetArcSyncProfile(const IntelDisplayOutput& output, IntelArcSyncProfileState& value)
{
    auto fn = Resolve<ProfileFn>(static_cast<HMODULE>(library_), "ctlGetIntelArcSyncProfile"); if (!fn) return false; Profile native{}; native.size = sizeof(native); auto code = fn(output.output, &native); Record("ctlGetIntelArcSyncProfile", code); if (code != 0) return false;
    value = { native.profile, native.minHz, native.maxHz, native.increase, native.decrease }; return true;
}
bool IntelArcSyncClient::SetArcSyncProfile(const IntelDisplayOutput& output, IntelArcSyncProfile profile, std::string& error)
{
    auto fn = Resolve<ProfileFn>(static_cast<HMODULE>(library_), "ctlSetIntelArcSyncProfile"); if (!fn) { error = "export unavailable"; return false; } Profile native{}; native.size = sizeof(native); native.profile = profile; auto code = fn(output.output, &native); Record("ctlSetIntelArcSyncProfile", code); if (code != 0) { error = IntelArcSyncResultName(code); return false; } return true;
}
void IntelArcSyncClient::Shutdown()
{
    if (apiHandle_ && library_) { auto close = Resolve<CloseFn>(static_cast<HMODULE>(library_), "ctlClose"); if (close) Record("ctlClose", close(apiHandle_)); }
    apiHandle_ = nullptr; adapters_.clear(); if (library_) FreeLibrary(static_cast<HMODULE>(library_)); library_ = nullptr;
}
IntelArcSyncClient::~IntelArcSyncClient() { Shutdown(); }
}
