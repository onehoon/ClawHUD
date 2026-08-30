#include "PresentMonApi2Diagnostic.h"
#include "PresentMonApi2Api.h"

#include "RuntimeLogger.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>
#include <thread>
#include <vector>

namespace clawhud
{
namespace
{
using Session = PM_SESSION_HANDLE;
using DynamicQuery = PM_DYNAMIC_QUERY_HANDLE;
using FrameQuery = PM_FRAME_QUERY_HANDLE;

using GetVersion = PM_STATUS(__cdecl*)(PM_VERSION*);
using OpenSession = PM_STATUS(__cdecl*)(Session*);
using CloseSession = PM_STATUS(__cdecl*)(Session);
using StartTracking = PM_STATUS(__cdecl*)(Session, std::uint32_t);
using StopTracking = PM_STATUS(__cdecl*)(Session, std::uint32_t);
using GetRoot = PM_STATUS(__cdecl*)(Session, const PM_INTROSPECTION_ROOT**);
using FreeRoot = PM_STATUS(__cdecl*)(const PM_INTROSPECTION_ROOT*);
using SetPeriod = PM_STATUS(__cdecl*)(Session, std::uint32_t, std::uint32_t);
using SetFlush = PM_STATUS(__cdecl*)(Session, std::uint32_t);
using RegisterDynamic = PM_STATUS(__cdecl*)(Session, DynamicQuery*, PM_QUERY_ELEMENT*,
    std::uint64_t, double, double);
using FreeDynamic = PM_STATUS(__cdecl*)(DynamicQuery);
using PollDynamic = PM_STATUS(__cdecl*)(DynamicQuery, std::uint32_t, std::uint8_t*, std::uint32_t*);
using PollStatic = PM_STATUS(__cdecl*)(Session, const PM_QUERY_ELEMENT*, std::uint32_t, std::uint8_t*);
using RegisterFrame = PM_STATUS(__cdecl*)(Session, FrameQuery*, PM_QUERY_ELEMENT*,
    std::uint64_t, std::uint32_t*);
using ConsumeFrames = PM_STATUS(__cdecl*)(FrameQuery, std::uint32_t, std::uint8_t*, std::uint32_t*);
using FreeFrame = PM_STATUS(__cdecl*)(FrameQuery);

const char* StatusName(PM_STATUS status) noexcept
{
    static constexpr std::array<const char*, 24> names{
        "SUCCESS", "FAILURE", "BAD_ARGUMENT", "BAD_HANDLE", "SERVICE_ERROR",
        "INVALID_ETL_FILE", "INVALID_PID", "ALREADY_TRACKING_PROCESS",
        "UNABLE_TO_CREATE_NSM", "INVALID_ADAPTER_ID", "OUT_OF_RANGE",
        "INSUFFICIENT_BUFFER", "PIPE_ERROR", "SESSION_NOT_OPEN",
        "MIDDLEWARE_MISSING_PATH", "NONEXISTENT_FILE_PATH",
        "MIDDLEWARE_INVALID_SIGNATURE", "MIDDLEWARE_MISSING_ENDPOINT",
        "MIDDLEWARE_VERSION_LOW", "MIDDLEWARE_VERSION_HIGH",
        "MIDDLEWARE_SERVICE_MISMATCH", "QUERY_MALFORMED", "MODE_MISMATCH",
        "FEATURE_DISABLED" };
    return status >= 0 && static_cast<size_t>(status) < names.size()
        ? names[status] : "UNKNOWN";
}

const char* MetricName(int metric) noexcept
{
    static constexpr std::array<const char*, PM_METRIC_COUNT_> names{
        "APPLICATION", "SWAP_CHAIN_ADDRESS", "GPU_VENDOR", "GPU_NAME",
        "CPU_VENDOR", "CPU_NAME", "CPU_START_TIME", "CPU_START_QPC",
        "CPU_FRAME_TIME", "CPU_BUSY", "CPU_WAIT", "DISPLAYED_FPS",
        "PRESENTED_FPS", "GPU_TIME", "GPU_BUSY", "GPU_WAIT", "DROPPED_FRAMES",
        "DISPLAYED_TIME", "SYNC_INTERVAL", "PRESENT_FLAGS", "PRESENT_MODE",
        "PRESENT_RUNTIME", "ALLOWS_TEARING", "GPU_LATENCY", "DISPLAY_LATENCY",
        "CLICK_TO_PHOTON_LATENCY", "GPU_SUSTAINED_POWER_LIMIT", "GPU_POWER",
        "GPU_VOLTAGE", "GPU_FREQUENCY", "GPU_TEMPERATURE", "GPU_FAN_SPEED",
        "GPU_UTILIZATION", "GPU_RENDER_COMPUTE_UTILIZATION", "GPU_MEDIA_UTILIZATION",
        "GPU_POWER_LIMITED", "GPU_TEMPERATURE_LIMITED", "GPU_CURRENT_LIMITED",
        "GPU_VOLTAGE_LIMITED", "GPU_UTILIZATION_LIMITED", "GPU_MEM_POWER",
        "GPU_MEM_VOLTAGE", "GPU_MEM_FREQUENCY", "GPU_MEM_EFFECTIVE_FREQUENCY",
        "GPU_MEM_TEMPERATURE", "GPU_MEM_SIZE", "GPU_MEM_USED",
        "GPU_MEM_UTILIZATION", "GPU_MEM_MAX_BANDWIDTH", "GPU_MEM_WRITE_BANDWIDTH",
        "GPU_MEM_READ_BANDWIDTH", "GPU_MEM_POWER_LIMITED",
        "GPU_MEM_TEMPERATURE_LIMITED", "GPU_MEM_CURRENT_LIMITED",
        "GPU_MEM_VOLTAGE_LIMITED", "GPU_MEM_UTILIZATION_LIMITED", "CPU_UTILIZATION",
        "CPU_POWER_LIMIT", "CPU_POWER", "CPU_TEMPERATURE", "CPU_FREQUENCY",
        "CPU_CORE_UTILITY", "APPLICATION_FPS", "FRAME_TYPE", "ANIMATION_ERROR",
        "ALL_INPUT_TO_PHOTON_LATENCY", "INSTRUMENTED_LATENCY", "ANIMATION_TIME",
        "GPU_EFFECTIVE_FREQUENCY", "GPU_VOLTAGE_REGULATOR_TEMPERATURE",
        "GPU_MEM_EFFECTIVE_BANDWIDTH", "GPU_OVERVOLTAGE_PERCENT",
        "GPU_TEMPERATURE_PERCENT", "GPU_POWER_PERCENT", "GPU_FAN_SPEED_PERCENT",
        "GPU_CARD_POWER", "PRESENT_START_TIME", "PRESENT_START_QPC",
        "BETWEEN_PRESENTS", "IN_PRESENT_API", "BETWEEN_DISPLAY_CHANGE",
        "UNTIL_DISPLAYED", "RENDER_PRESENT_LATENCY", "BETWEEN_SIMULATION_START",
        "PC_LATENCY", "DISPLAYED_FRAME_TIME", "BETWEEN_APP_START",
        "PRESENTED_FRAME_TIME", "FLIP_DELAY", "PROCESS_ID",
        "SESSION_START_QPC" };
    return metric >= 0 && metric < static_cast<int>(names.size()) ? names[metric] : "UNKNOWN";
}

std::string Text(const PM_INTROSPECTION_STRING* value)
{
    return value && value->pData ? value->pData : "";
}
std::string Json(const std::string& value)
{
    std::ostringstream out;
    for (const unsigned char c : value)
    {
        if (c == '"') out << "\\\"";
        else if (c == '\\') out << "\\\\";
        else if (c == '\n') out << "\\n";
        else if (c == '\r') out << "\\r";
        else if (c < 0x20) out << "\\u" << std::hex << std::setw(4)
            << std::setfill('0') << static_cast<unsigned>(c);
        else out << c;
    }
    return out.str();
}
std::string Csv(const std::string& value)
{
    std::string result = "\"";
    for (const char c : value) result += c == '"' ? "\"\"" : std::string(1, c);
    result += '"';
    return result;
}
std::wstring Stamp()
{
    SYSTEMTIME time{}; GetLocalTime(&time); wchar_t text[32]{};
    swprintf_s(text, L"%04u%02u%02u-%02u%02u%02u", time.wYear, time.wMonth,
        time.wDay, time.wHour, time.wMinute, time.wSecond);
    return text;
}

std::filesystem::path FindPresentMonApi2Loader()
{
    wchar_t modulePath[MAX_PATH]{};
    const DWORD moduleChars = GetModuleFileNameW(nullptr, modulePath, MAX_PATH);
    if (moduleChars != 0 && moduleChars < MAX_PATH)
    {
        const auto appLocal = std::filesystem::path(modulePath).parent_path()
            / L"PresentMonAPI2Loader.dll";
        if (std::filesystem::exists(appLocal))
            return appLocal;
    }

    wchar_t programFiles[MAX_PATH]{};
    const DWORD programFilesChars = GetEnvironmentVariableW(
        L"ProgramFiles", programFiles, MAX_PATH);
    if (programFilesChars != 0 && programFilesChars < MAX_PATH)
    {
        const auto sdkLoader = std::filesystem::path(programFiles)
            / L"Intel" / L"PresentMon" / L"SDK" / L"PresentMonAPI2Loader.dll";
        if (std::filesystem::exists(sdkLoader))
            return sdkLoader;
    }

    return {};
}
std::string Narrow(const std::wstring& value)
{
    if (value.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<size_t>(std::max(size, 0)), '\0');
    if (size) WideCharToMultiByte(CP_UTF8, 0, value.data(),
        static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
    return result;
}
std::string Hex(const std::uint8_t* data, std::uint32_t size)
{
    std::ostringstream out; out << std::hex << std::setfill('0');
    for (std::uint32_t i = 0; data && i < size; ++i) out << std::setw(2)
        << static_cast<unsigned>(data[i]);
    return out.str();
}
std::filesystem::path Output(const std::filesystem::path& dir,
    const std::wstring& timestamp, const wchar_t* suffix)
{
    return dir / (L"api2-" + timestamp + suffix);
}
bool ProcessAlive(DWORD processId)
{
    if (!processId) return false;
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, processId);
    if (!process) return false;
    const bool alive = WaitForSingleObject(process, 0) == WAIT_TIMEOUT;
    CloseHandle(process);
    return alive;
}
std::wstring MiddlewarePath()
{
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SOFTWARE\\INTEL\\PresentMon\\Service", 0, KEY_READ | KEY_WOW64_64KEY,
        &key) != ERROR_SUCCESS)
        return {};
    wchar_t value[1024]{}; DWORD size = sizeof(value); DWORD type{};
    const auto result = RegQueryValueExW(key, L"sharedMiddlewarePath", nullptr,
        &type, reinterpret_cast<BYTE*>(value), &size);
    RegCloseKey(key);
    return result == ERROR_SUCCESS && type == REG_SZ ? value : L"";
}
const wchar_t* ServiceState()
{
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return L"UNAVAILABLE";
    SC_HANDLE service = OpenServiceW(manager, L"PresentMonSharedService", SERVICE_QUERY_STATUS);
    if (!service) { CloseServiceHandle(manager); return L"NOT_FOUND"; }
    SERVICE_STATUS_PROCESS status{}; DWORD size{};
    const bool queried = QueryServiceStatusEx(service, SC_STATUS_PROCESS_INFO,
        reinterpret_cast<BYTE*>(&status), sizeof(status), &size) != FALSE;
    CloseServiceHandle(service); CloseServiceHandle(manager);
    if (!queried) return L"QUERY_FAILED";
    return status.dwCurrentState == SERVICE_RUNNING ? L"RUNNING" : L"NOT_RUNNING";
}
template<class F> auto Proc(HMODULE module, const char* name)
{
    return reinterpret_cast<F>(GetProcAddress(module, name));
}

struct QueryRecord
{
    PM_QUERY_ELEMENT element{};
    PM_DATA_TYPE type{ PM_DATA_TYPE_VOID };
    PM_UNIT unit{};
    std::string name;
};
struct DynamicSlot
{
    QueryRecord query;
    DynamicQuery handle{};
    std::vector<std::uint8_t> blob;
    bool hasSample{};
    bool hasNonZeroSample{};
    bool queryFailed{};
};

std::string Value(const std::uint8_t* blob, const PM_QUERY_ELEMENT& element, int type)
{
    if (!blob) return {};
    std::ostringstream out; out << std::setprecision(17);
    if (type == PM_DATA_TYPE_DOUBLE && element.dataSize >= sizeof(double))
    { double value{}; std::memcpy(&value, blob + element.dataOffset, sizeof(value)); out << value; }
    else if (type == PM_DATA_TYPE_UINT64 && element.dataSize >= sizeof(std::uint64_t))
    { std::uint64_t value{}; std::memcpy(&value, blob + element.dataOffset, sizeof(value)); out << value; }
    else if (type == PM_DATA_TYPE_UINT32 && element.dataSize >= sizeof(std::uint32_t))
    { std::uint32_t value{}; std::memcpy(&value, blob + element.dataOffset, sizeof(value)); out << value; }
    else if (type == PM_DATA_TYPE_INT32 && element.dataSize >= sizeof(std::int32_t))
    { std::int32_t value{}; std::memcpy(&value, blob + element.dataOffset, sizeof(value)); out << value; }
    else if (type == PM_DATA_TYPE_BOOL && element.dataSize >= sizeof(bool))
    { bool value{}; std::memcpy(&value, blob + element.dataOffset, sizeof(value)); out << (value ? "true" : "false"); }
    else if (type == PM_DATA_TYPE_ENUM && element.dataSize >= sizeof(std::int32_t))
    { std::int32_t value{}; std::memcpy(&value, blob + element.dataOffset, sizeof(value)); out << value; }
    else out << "<raw:" << element.dataSize << ">";
    return out.str();
}

std::string StaticValue(const std::uint8_t* blob, PM_DATA_TYPE type)
{
    if (!blob) return {};
    std::ostringstream out;
    out << std::setprecision(17);
    switch (type)
    {
    case PM_DATA_TYPE_DOUBLE:
    {
        double value{};
        std::memcpy(&value, blob, sizeof(value));
        out << value;
        break;
    }
    case PM_DATA_TYPE_UINT64:
    {
        std::uint64_t value{};
        std::memcpy(&value, blob, sizeof(value));
        out << value;
        break;
    }
    case PM_DATA_TYPE_UINT32:
    {
        std::uint32_t value{};
        std::memcpy(&value, blob, sizeof(value));
        out << value;
        break;
    }
    case PM_DATA_TYPE_INT32:
    case PM_DATA_TYPE_ENUM:
    {
        std::int32_t value{};
        std::memcpy(&value, blob, sizeof(value));
        out << value;
        break;
    }
    case PM_DATA_TYPE_BOOL:
    {
        bool value{};
        std::memcpy(&value, blob, sizeof(value));
        out << (value ? "true" : "false");
        break;
    }
    case PM_DATA_TYPE_STRING:
        return reinterpret_cast<const char*>(blob);
    default:
        return "<unsupported-static-type>";
    }
    return out.str();
}

void WriteIntrospection(const PM_INTROSPECTION_ROOT* root, std::ofstream& out)
{
    out << "{\n\"devices\":[";
    if (root && root->pDevices) for (size_t i = 0; i < root->pDevices->size; ++i)
    {
        const auto* device = static_cast<const PM_INTROSPECTION_DEVICE*>(root->pDevices->pData[i]);
        if (!device) continue;
        if (i) out << ',';
        out << "{\"id\":" << device->id << ",\"type\":" << device->type
            << ",\"vendor\":" << device->vendor << ",\"name\":\""
            << Json(Text(device->pName)) << "\",\"luid\":\""
            << (device->pLuid ? Hex(device->pLuid->pData, device->pLuid->size) : "") << "\"}";
    }
    out << "],\"metrics\":[";
    if (root && root->pMetrics) for (size_t i = 0; i < root->pMetrics->size; ++i)
    {
        const auto* metric = static_cast<const PM_INTROSPECTION_METRIC*>(root->pMetrics->pData[i]);
        if (!metric) continue;
        if (i) out << ',';
        out << "{\"id\":" << metric->id << ",\"name\":\"" << MetricName(metric->id)
            << "\",\"type\":" << metric->type << ",\"unit\":" << metric->unit
            << ",\"preferredUnit\":" << metric->preferredUnitHint << ",\"dataType\":"
            << (metric->pTypeInfo ? metric->pTypeInfo->polledType : PM_DATA_TYPE_VOID)
            << ",\"frameType\":" << (metric->pTypeInfo ? metric->pTypeInfo->frameType : PM_DATA_TYPE_VOID)
            << ",\"statistics\":[";
        if (metric->pStatInfo) for (size_t s = 0; s < metric->pStatInfo->size; ++s)
        { if (s) out << ','; auto* info = static_cast<const PM_INTROSPECTION_STAT_INFO*>(metric->pStatInfo->pData[s]); out << (info ? info->stat : -1); }
        out << "],\"devices\":[";
        if (metric->pDeviceMetricInfo) for (size_t d = 0; d < metric->pDeviceMetricInfo->size; ++d)
        { if (d) out << ','; auto* info = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(metric->pDeviceMetricInfo->pData[d]); if (info) out << "{\"deviceId\":" << info->deviceId << ",\"availability\":" << info->availability << ",\"arraySize\":" << info->arraySize << "}"; }
        out << "]}";
    }
    out << "],\"enums\":[";
    if (root && root->pEnums) for (size_t i = 0; i < root->pEnums->size; ++i)
    {
        const auto* enumeration = static_cast<const PM_INTROSPECTION_ENUM*>(root->pEnums->pData[i]);
        if (!enumeration) continue;
        if (i) out << ',';
        out << "{\"id\":" << enumeration->id << ",\"symbol\":\"" << Json(Text(enumeration->pSymbol)) << "\",\"description\":\"" << Json(Text(enumeration->pDescription)) << "\",\"keys\":[";
        if (enumeration->pKeys) for (size_t k = 0; k < enumeration->pKeys->size; ++k)
        { if (k) out << ','; const auto* key = static_cast<const PM_INTROSPECTION_ENUM_KEY*>(enumeration->pKeys->pData[k]); if (key) out << "{\"id\":" << key->id << ",\"symbol\":\"" << Json(Text(key->pSymbol)) << "\",\"name\":\"" << Json(Text(key->pName)) << "\",\"shortName\":\"" << Json(Text(key->pShortName)) << "\",\"description\":\"" << Json(Text(key->pDescription)) << "\"}"; }
        out << "]}";
    }
    out << "],\"units\":[";
    if (root && root->pUnits) for (size_t i = 0; i < root->pUnits->size; ++i)
    { if (i) out << ','; const auto* unit = static_cast<const PM_INTROSPECTION_UNIT*>(root->pUnits->pData[i]); if (unit) out << "{\"id\":" << unit->id << ",\"baseUnitId\":" << unit->baseUnitId << ",\"scale\":" << unit->scale << "}"; }
    out << "]}\n";
}
}

const char* Api2MetricResultName(Api2MetricResult result) noexcept
{
    switch (result)
    {
    case Api2MetricResult::Working: return "WORKING";
    case Api2MetricResult::Static: return "STATIC";
    case Api2MetricResult::ZeroOnly: return "ZERO_ONLY";
    case Api2MetricResult::Unavailable: return "UNAVAILABLE";
    case Api2MetricResult::Invalid: return "INVALID";
    case Api2MetricResult::QueryFailed: return "QUERY_FAILED";
    default: return "NOT_APPLICABLE";
    }
}
Api2MetricResult ClassifyApi2Metric(bool available, bool querySucceeded,
    bool hasSample, bool hasNonZeroSample, bool dynamic) noexcept
{
    if (!available) return Api2MetricResult::Unavailable;
    if (!querySucceeded) return Api2MetricResult::QueryFailed;
    if (!hasSample) return Api2MetricResult::Invalid;
    if (!dynamic) return Api2MetricResult::Static;
    return hasNonZeroSample ? Api2MetricResult::Working : Api2MetricResult::ZeroOnly;
}
bool Api2MetricFailureIsNonFatal() noexcept { return true; }
bool Api2FrameConsumeZeroIsNonFatal(std::uint32_t) noexcept { return true; }
bool Api2TargetPidIsUsable(DWORD processId, DWORD currentProcessId) noexcept
{ return processId != 0 && processId != currentProcessId; }
std::filesystem::path Api2DiagnosticOutputPath(const std::filesystem::path& directory,
    const std::wstring& timestamp, const wchar_t* suffix)
{ return Output(directory, timestamp, suffix); }

PresentMonApi2Diagnostic::~PresentMonApi2Diagnostic()
{
    Stop();
}
void PresentMonApi2Diagnostic::Status(const wchar_t* status) const
{
    auto* copy = new std::wstring(status ? status : L"Idle");
    if (!notifyWindow_ || !PostMessageW(notifyWindow_, kPresentMonApi2DiagnosticStatus,
        reinterpret_cast<WPARAM>(copy), 0)) delete copy;
}
bool PresentMonApi2Diagnostic::Start()
{
    if (running_.exchange(true)) return false;
    stop_ = false;
    try { worker_ = std::thread(&PresentMonApi2Diagnostic::Run, this); }
    catch (...) { running_ = false; return false; }
    return true;
}
void PresentMonApi2Diagnostic::Stop() noexcept
{
    stop_ = true;
    if (worker_.joinable())
    {
        worker_.join();
    }
    running_ = false;
}

void PresentMonApi2Diagnostic::Run()
{
    bool success = false;
    const auto complete = [this](bool result)
    {
        if (notifyWindow_)
            PostMessageW(notifyWindow_, kPresentMonApi2DiagnosticCompleted,
                result ? 1 : 0, 0);
    };
    std::filesystem::path directory;
    std::wstring stamp = Stamp();
    try { directory = LogDirectory(); std::filesystem::create_directories(directory); }
    catch (...) { Status(L"PresentMon API2 log directory unavailable"); running_ = false; complete(false); return; }
    const auto logPath = Output(directory, stamp, L".log");
    const auto jsonPath = Output(directory, stamp, L"-introspection.json");
    const auto metricsPath = Output(directory, stamp, L"-metrics.csv");
    const auto framesPath = Output(directory, stamp, L"-frames.csv");
    std::ofstream log(logPath), metrics(metricsPath), frames(framesPath);
    log << "=== PresentMon API2 Runtime ===\n" << "output_log=" << logPath.string() << "\n";
    log << "output_introspection=" << jsonPath.string() << "\n"
        << "output_metrics=" << metricsPath.string() << "\n"
        << "output_frames=" << framesPath.string() << "\n";
    log << "middleware_path=" << Narrow(MiddlewarePath()) << "\n";
    log << "service_state=" << Narrow(ServiceState()) << "\n";
    metrics << "TimestampMs,MetricId,MetricName,DeviceId,ArrayIndex,Statistic,Value,Unit,Status\n";
    frames << "TimestampMs,ProcessId,MetricId,MetricName,DeviceId,ArrayIndex,Value,Unit\n";
    Status(L"Waiting 5 seconds...");
    for (int i = 0; i < 50 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (stop_) { log << "cancelled=true\n"; running_ = false; complete(false); return; }

    const auto loaderPath = FindPresentMonApi2Loader();
    SetLastError(ERROR_SUCCESS);
    HMODULE loader = loaderPath.empty() ? nullptr : LoadLibraryExW(
        loaderPath.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    log << "loader_path=" << Narrow(loaderPath.wstring())
        << " loader=" << (loader ? "LOADED" : "MISSING")
        << " error=" << GetLastError() << "\n";
    if (!loader)
    { log << "PresentMon API2 runtime not available.\nInstall the PresentMon SDK/runtime and retry.\n"; Status(L"Runtime unavailable"); running_ = false; complete(false); return; }
    const auto getVersion = Proc<GetVersion>(loader, "pmGetApiVersion");
    const auto openSession = Proc<OpenSession>(loader, "pmOpenSession");
    const auto closeSession = Proc<CloseSession>(loader, "pmCloseSession");
    const auto startTracking = Proc<StartTracking>(loader, "pmStartTrackingProcess");
    const auto stopTracking = Proc<StopTracking>(loader, "pmStopTrackingProcess");
    const auto getRoot = Proc<GetRoot>(loader, "pmGetIntrospectionRoot");
    const auto freeRoot = Proc<FreeRoot>(loader, "pmFreeIntrospectionRoot");
    const auto setPeriod = Proc<SetPeriod>(loader, "pmSetTelemetryPollingPeriod");
    const auto setFlush = Proc<SetFlush>(loader, "pmSetEtwFlushPeriod");
    const auto registerDynamic = Proc<RegisterDynamic>(loader, "pmRegisterDynamicQuery");
    const auto freeDynamic = Proc<FreeDynamic>(loader, "pmFreeDynamicQuery");
    const auto pollDynamic = Proc<PollDynamic>(loader, "pmPollDynamicQuery");
    const auto pollStatic = Proc<PollStatic>(loader, "pmPollStaticQuery");
    const auto registerFrame = Proc<RegisterFrame>(loader, "pmRegisterFrameQuery");
    const auto consumeFrames = Proc<ConsumeFrames>(loader, "pmConsumeFrames");
    const auto freeFrame = Proc<FreeFrame>(loader, "pmFreeFrameQuery");
    log << "symbols pmGetApiVersion=" << (getVersion ? "PRESENT" : "MISSING")
        << " pmOpenSession=" << (openSession ? "PRESENT" : "MISSING")
        << " pmGetIntrospectionRoot=" << (getRoot ? "PRESENT" : "MISSING") << "\n";
    PM_VERSION version{}; PM_STATUS status = getVersion ? getVersion(&version) : PM_STATUS_FAILURE;
    log << "pmGetApiVersion status=" << StatusName(status) << " raw=" << status
        << " version=" << version.major << '.' << version.minor << '.' << version.patch << "\n";
    Session session{}; status = openSession ? openSession(&session) : PM_STATUS_FAILURE;
    log << "pmOpenSession status=" << StatusName(status) << " raw=" << status << "\n";
    if (status != PM_STATUS_SUCCESS || !session || !getRoot || !freeRoot)
    { log << "Session/introspection unavailable; no metric capture performed.\n"; FreeLibrary(loader); running_ = false; complete(false); return; }
    const auto close = [&] { if (closeSession) closeSession(session); };
    const PM_INTROSPECTION_ROOT* root{}; status = getRoot(session, &root);
    log << "pmGetIntrospectionRoot status=" << StatusName(status) << " raw=" << status << "\n";
    if (status != PM_STATUS_SUCCESS || !root)
    { close(); FreeLibrary(loader); running_ = false; complete(false); return; }
    std::ofstream introspection(jsonPath); WriteIntrospection(root, introspection);
    log << "introspection_file=" << jsonPath.string() << " devices="
        << (root->pDevices ? root->pDevices->size : 0) << " metrics="
        << (root->pMetrics ? root->pMetrics->size : 0) << " enums="
        << (root->pEnums ? root->pEnums->size : 0) << "\n";
    log << "\n=== Devices ===\n";
    if (root->pDevices) for (size_t i = 0; i < root->pDevices->size; ++i)
    {
        const auto* device = static_cast<const PM_INTROSPECTION_DEVICE*>(root->pDevices->pData[i]);
        if (!device) continue;
        log << "id=" << device->id << " type=" << device->type << " vendor="
            << device->vendor << " name=" << Text(device->pName) << " luid="
            << (device->pLuid ? Hex(device->pLuid->pData, device->pLuid->size) : "") << "\n";
    }
    log << "\n=== Metric Availability ===\n";
    if (root->pMetrics) for (size_t i = 0; i < root->pMetrics->size; ++i)
    {
        const auto* metric = static_cast<const PM_INTROSPECTION_METRIC*>(root->pMetrics->pData[i]);
        if (!metric || !metric->pDeviceMetricInfo) continue;
        for (size_t d = 0; d < metric->pDeviceMetricInfo->size; ++d)
        {
            const auto* info = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(metric->pDeviceMetricInfo->pData[d]);
            if (info) log << "metric=" << MetricName(metric->id) << " id=" << metric->id
                << " device=" << info->deviceId << " availability=" << info->availability
                << " arraySize=" << info->arraySize << "\n";
        }
    }

    HWND foreground = GetForegroundWindow(); DWORD pid{};
    if (foreground) GetWindowThreadProcessId(foreground, &pid);
    if (!Api2TargetPidIsUsable(pid, GetCurrentProcessId())) pid = 0;
    log << "target_pid=" << pid << "\n";
    std::vector<QueryRecord> dynamic;
    if (root->pMetrics) for (size_t i = 0; i < root->pMetrics->size; ++i)
    {
        const auto* metric = static_cast<const PM_INTROSPECTION_METRIC*>(root->pMetrics->pData[i]);
        if (!metric || (metric->type != PM_METRIC_TYPE_DYNAMIC && metric->type != PM_METRIC_TYPE_DYNAMIC_FRAME) || !metric->pDeviceMetricInfo) continue;
        PM_STAT stat = PM_STAT_NEWEST_POINT;
        if (metric->pStatInfo && metric->pStatInfo->size) stat = static_cast<const PM_INTROSPECTION_STAT_INFO*>(metric->pStatInfo->pData[0])->stat;
        for (size_t d = 0; d < metric->pDeviceMetricInfo->size; ++d)
        {
            const auto* info = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(metric->pDeviceMetricInfo->pData[d]);
            if (!info || info->availability != PM_METRIC_AVAILABILITY_AVAILABLE) continue;
            for (std::uint32_t a = 0; a < std::max(1u, info->arraySize); ++a)
                dynamic.push_back({ { metric->id, stat, info->deviceId, a, 0, 0 }, metric->pTypeInfo ? metric->pTypeInfo->polledType : PM_DATA_TYPE_VOID, metric->unit, MetricName(metric->id) });
        }
    }
    std::vector<QueryRecord> staticQueries;
    if (root->pMetrics) for (size_t i = 0; i < root->pMetrics->size; ++i)
    {
        const auto* metric = static_cast<const PM_INTROSPECTION_METRIC*>(root->pMetrics->pData[i]);
        if (!metric || metric->type != PM_METRIC_TYPE_STATIC || !metric->pDeviceMetricInfo) continue;
        for (size_t d = 0; d < metric->pDeviceMetricInfo->size; ++d)
        {
            const auto* info = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(metric->pDeviceMetricInfo->pData[d]);
            if (!info || info->availability != PM_METRIC_AVAILABILITY_AVAILABLE) continue;
            for (std::uint32_t a = 0; a < std::max(1u, info->arraySize); ++a)
                staticQueries.push_back({ { metric->id, PM_STAT_NONE, info->deviceId, a, 0, 0 }, metric->pTypeInfo ? metric->pTypeInfo->polledType : PM_DATA_TYPE_VOID, metric->unit, MetricName(metric->id) });
        }
    }
    log << "available_dynamic_queries=" << dynamic.size() << "\n";
    if (pid && startTracking)
    { status = startTracking(session, pid); log << "pmStartTrackingProcess pid=" << pid << " status=" << StatusName(status) << " raw=" << status << "\n"; }
    else log << "frame_fps_pid_bound_tests=SKIPPED_NO_TARGET_PID\n";
    if (setPeriod) { status = setPeriod(session, 0, 250); log << "pmSetTelemetryPollingPeriod status=" << StatusName(status) << " raw=" << status << " period_ms=250\n"; }
    if (setFlush) { status = setFlush(session, 100); log << "pmSetEtwFlushPeriod status=" << StatusName(status) << " raw=" << status << " period_ms=100\n"; }

    std::vector<DynamicSlot> dynamicSlots;
    if (registerDynamic && !dynamic.empty())
    {
        for (auto query : dynamic)
        {
            DynamicQuery handle{};
            status = registerDynamic(session, &handle, &query.element, 1, 1000, 1020);
            log << "dynamic_query metric=" << query.name << " device=" << query.element.deviceId
                << " array=" << query.element.arrayIndex << " status=" << StatusName(status)
                << " raw=" << status << "\n";
            if (status == PM_STATUS_SUCCESS && handle)
            {
                const auto size = static_cast<size_t>(std::max<std::uint64_t>(
                    query.element.dataOffset + query.element.dataSize, 4096));
                dynamicSlots.push_back({ std::move(query), handle, std::vector<std::uint8_t>(size) });
            }
        }
    }
    for (const auto& query : staticQueries)
    {
        std::vector<std::uint8_t> staticBlob(4096);
        const PM_STATUS polled = pollStatic
            ? pollStatic(session, &query.element, pid, staticBlob.data())
            : PM_STATUS_FAILURE;
        const auto value = polled == PM_STATUS_SUCCESS
            ? StaticValue(staticBlob.data(), query.type) : std::string{};
        metrics << 0 << ',' << query.element.metric << ',' << Csv(query.name) << ',' << query.element.deviceId << ',' << query.element.arrayIndex << ',' << query.element.stat << ',' << Csv(value) << ',' << query.unit << ',' << StatusName(polled) << "\n";
        log << "static_query metric=" << query.name << " device=" << query.element.deviceId << " array=" << query.element.arrayIndex << " status=" << StatusName(polled) << " raw=" << polled << " classification=" << Api2MetricResultName(ClassifyApi2Metric(true, polled == PM_STATUS_SUCCESS, polled == PM_STATUS_SUCCESS, true, false)) << "\n";
    }
    std::vector<QueryRecord> frameQueries;
    if (root->pMetrics) for (size_t i = 0; i < root->pMetrics->size; ++i)
    {
        const auto* metric = static_cast<const PM_INTROSPECTION_METRIC*>(root->pMetrics->pData[i]);
        if (metric && metric->type == PM_METRIC_TYPE_FRAME_EVENT)
            frameQueries.push_back({ { metric->id, PM_STAT_NONE, 0, 0, 0, 0 }, metric->pTypeInfo ? metric->pTypeInfo->frameType : PM_DATA_TYPE_VOID, metric->unit, MetricName(metric->id) });
    }
    std::vector<PM_QUERY_ELEMENT> frameElements;
    for (const auto& query : frameQueries) frameElements.push_back(query.element);
    FrameQuery frameQuery{}; std::uint32_t frameBlobSize{};
    if (registerFrame && !frameElements.empty())
    {
        status = registerFrame(session, &frameQuery, frameElements.data(), frameElements.size(), &frameBlobSize);
        log << "pmRegisterFrameQuery elements=" << frameElements.size() << " status=" << StatusName(status) << " raw=" << status << " blob_size=" << frameBlobSize << "\n";
        if (status == PM_STATUS_SUCCESS)
            for (size_t i = 0; i < frameQueries.size(); ++i)
                frameQueries[i].element = frameElements[i];
    }
    const auto captureStart = std::chrono::steady_clock::now();
    Status(L"Capturing API2 for 15 seconds...");
    auto nextTelemetry = captureStart;
    bool trackingStopped{};
    for (int sample = 0; sample < 150 && !stop_; ++sample)
    {
        const auto now = std::chrono::steady_clock::now();
        const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now - captureStart).count();
        if (pid && !ProcessAlive(pid))
        {
            log << "target_process_exited timestamp_ms=" << timestamp << " pid=" << pid << "\n";
            if (stopTracking && !trackingStopped) { stopTracking(session, pid); trackingStopped = true; }
            pid = 0;
        }
        if (pollDynamic && pid && now >= nextTelemetry)
        {
            for (auto& slot : dynamicSlots)
            {
                std::uint32_t swapChains = 1;
                const PM_STATUS poll = pollDynamic(slot.handle, pid, slot.blob.data(), &swapChains);
                const auto value = Value(slot.blob.data(), slot.query.element, slot.query.type);
                metrics << timestamp << ',' << slot.query.element.metric << ',' << Csv(slot.query.name) << ',' << slot.query.element.deviceId << ',' << slot.query.element.arrayIndex << ',' << slot.query.element.stat << ',' << Csv(value) << ',' << slot.query.unit << ',' << StatusName(poll) << "\n";
                if (poll == PM_STATUS_SUCCESS)
                {
                    slot.hasSample = true;
                    slot.hasNonZeroSample = slot.hasNonZeroSample ||
                        (value != "0" && value != "0.0" && value != "false");
                }
                else slot.queryFailed = true;
                if (poll != PM_STATUS_SUCCESS)
                    log << "dynamic_sample metric=" << slot.query.name << " timestamp_ms=" << timestamp << " status=" << StatusName(poll) << " raw=" << poll << "\n";
            }
            nextTelemetry += std::chrono::milliseconds(250);
        }
        if (frameQuery && consumeFrames && pid)
        {
            std::vector<std::uint8_t> frameBlob(std::max<std::uint32_t>(frameBlobSize, 1) * 64);
            std::uint32_t count = 64;
            const PM_STATUS consumed = consumeFrames(frameQuery, pid, frameBlob.data(), &count);
            log << "frame_consume timestamp_ms=" << timestamp << " status=" << StatusName(consumed) << " raw=" << consumed << " count=" << count << "\n";
            if (consumed == PM_STATUS_SUCCESS && count)
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    const auto* record = frameBlob.data() + static_cast<size_t>(i) * frameBlobSize;
                    for (const auto& query : frameQueries)
                    {
                        frames << timestamp << ',' << pid << ','
                            << query.element.metric << ',' << Csv(query.name) << ','
                            << query.element.deviceId << ',' << query.element.arrayIndex << ','
                            << Csv(Value(record, query.element, query.type)) << ','
                            << query.unit << "\n";
                    }
                }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    for (auto& slot : dynamicSlots) if (freeDynamic) freeDynamic(slot.handle);
    for (const auto& slot : dynamicSlots)
        log << "metric_classification name=" << slot.query.name << " device="
            << slot.query.element.deviceId << " array=" << slot.query.element.arrayIndex
            << " result=" << Api2MetricResultName(ClassifyApi2Metric(true, !slot.queryFailed,
                slot.hasSample, slot.hasNonZeroSample, true)) << "\n";
    if (frameQuery && freeFrame) freeFrame(frameQuery);
    if (pid && stopTracking && !trackingStopped) { status = stopTracking(session, pid); log << "pmStopTrackingProcess pid=" << pid << " status=" << StatusName(status) << " raw=" << status << "\n"; }
    freeRoot(root); close(); FreeLibrary(loader); success = !stop_;
    log << "capture_complete=" << (success ? "true" : "false") << "\n";
    if (success) { Status(L"Completed"); MessageBeep(MB_OK); }
    else Status(L"Cancelled");
    running_ = false;
    complete(success);
}
}
