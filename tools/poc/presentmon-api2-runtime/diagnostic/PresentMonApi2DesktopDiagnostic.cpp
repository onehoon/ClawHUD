#define PRESENTMONAPI2_EXPORTS
#define NOMINMAX
#include "PresentMonAPI.h"

#include <windows.h>
#include <dxgi.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "dxgi.lib")

using namespace std::chrono_literals;

static const char* StatusName(PM_STATUS s) {
    static const char* n[] = { "SUCCESS", "FAILURE", "BAD_ARGUMENT", "BAD_HANDLE", "SERVICE_ERROR", "INVALID_ETL_FILE", "INVALID_PID", "ALREADY_TRACKING_PROCESS", "UNABLE_TO_CREATE_NSM", "INVALID_ADAPTER_ID", "OUT_OF_RANGE", "INSUFFICIENT_BUFFER", "PIPE_ERROR", "SESSION_NOT_OPEN", "MIDDLEWARE_MISSING_PATH", "NONEXISTENT_FILE_PATH", "MIDDLEWARE_INVALID_SIGNATURE", "MIDDLEWARE_MISSING_ENDPOINT", "MIDDLEWARE_VERSION_LOW", "MIDDLEWARE_VERSION_HIGH", "MIDDLEWARE_SERVICE_MISMATCH", "QUERY_MALFORMED", "MODE_MISMATCH", "FEATURE_DISABLED" };
    return s >= 0 && s < static_cast<PM_STATUS>(std::size(n)) ? n[s] : "UNKNOWN";
}

static std::string JsonEscape(const std::string& s) {
    std::ostringstream o;
    for (unsigned char c : s) {
        if (c == '"') o << "\\\"";
        else if (c == '\\') o << "\\\\";
        else if (c == '\n') o << "\\n";
        else if (c < 0x20) o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(c);
        else o << c;
    }
    return o.str();
}

static std::string StringValue(const PM_INTROSPECTION_STRING* p) { return p && p->pData ? p->pData : ""; }
static std::string Hex(const uint8_t* p, uint32_t n) {
    std::ostringstream o; o << std::hex << std::setfill('0');
    for (uint32_t i = 0; p && i < n; ++i) o << std::setw(2) << static_cast<unsigned>(p[i]);
    return o.str();
}

static const char* MetricName(PM_METRIC m) {
    static const char* n[] = { "APPLICATION", "SWAP_CHAIN_ADDRESS", "GPU_VENDOR", "GPU_NAME", "CPU_VENDOR", "CPU_NAME", "CPU_START_TIME", "CPU_START_QPC", "CPU_FRAME_TIME", "CPU_BUSY", "CPU_WAIT", "DISPLAYED_FPS", "PRESENTED_FPS", "GPU_TIME", "GPU_BUSY", "GPU_WAIT", "DROPPED_FRAMES", "DISPLAYED_TIME", "SYNC_INTERVAL", "PRESENT_FLAGS", "PRESENT_MODE", "PRESENT_RUNTIME", "ALLOWS_TEARING", "GPU_LATENCY", "DISPLAY_LATENCY", "CLICK_TO_PHOTON_LATENCY", "GPU_SUSTAINED_POWER_LIMIT", "GPU_POWER", "GPU_VOLTAGE", "GPU_FREQUENCY", "GPU_TEMPERATURE", "GPU_FAN_SPEED", "GPU_UTILIZATION", "GPU_RENDER_COMPUTE_UTILIZATION", "GPU_MEDIA_UTILIZATION", "GPU_POWER_LIMITED", "GPU_TEMPERATURE_LIMITED", "GPU_CURRENT_LIMITED", "GPU_VOLTAGE_LIMITED", "GPU_UTILIZATION_LIMITED", "GPU_MEM_POWER", "GPU_MEM_VOLTAGE", "GPU_MEM_FREQUENCY", "GPU_MEM_EFFECTIVE_FREQUENCY", "GPU_MEM_TEMPERATURE", "GPU_MEM_SIZE", "GPU_MEM_USED", "GPU_MEM_UTILIZATION", "GPU_MEM_MAX_BANDWIDTH", "GPU_MEM_WRITE_BANDWIDTH", "GPU_MEM_READ_BANDWIDTH", "GPU_MEM_POWER_LIMITED", "GPU_MEM_TEMPERATURE_LIMITED", "GPU_MEM_CURRENT_LIMITED", "GPU_MEM_VOLTAGE_LIMITED", "GPU_MEM_UTILIZATION_LIMITED", "CPU_UTILIZATION", "CPU_POWER_LIMIT", "CPU_POWER", "CPU_TEMPERATURE", "CPU_FREQUENCY", "CPU_CORE_UTILITY", "APPLICATION_FPS", "FRAME_TYPE", "ANIMATION_ERROR", "ALL_INPUT_TO_PHOTON_LATENCY", "INSTRUMENTED_LATENCY", "ANIMATION_TIME", "GPU_EFFECTIVE_FREQUENCY", "GPU_VOLTAGE_REGULATOR_TEMPERATURE", "GPU_MEM_EFFECTIVE_BANDWIDTH", "GPU_OVERVOLTAGE_PERCENT", "GPU_TEMPERATURE_PERCENT", "GPU_POWER_PERCENT", "GPU_FAN_SPEED_PERCENT", "GPU_CARD_POWER", "PRESENT_START_TIME", "PRESENT_START_QPC", "BETWEEN_PRESENTS", "IN_PRESENT_API", "BETWEEN_DISPLAY_CHANGE", "UNTIL_DISPLAYED", "RENDER_PRESENT_LATENCY", "BETWEEN_SIMULATION_START", "PC_LATENCY", "DISPLAYED_FRAME_TIME", "BETWEEN_APP_START", "PRESENTED_FRAME_TIME", "FLIP_DELAY", "PROCESS_ID", "SESSION_START_QPC" };
    return m >= 0 && m < PM_METRIC_COUNT_ ? n[m] : "UNKNOWN_METRIC";
}

struct Api {
    HMODULE module{};
#define API(name) decltype(&name) name{}
    API(pmGetApiVersion); API(pmOpenSession); API(pmCloseSession); API(pmStartTrackingProcess); API(pmStopTrackingProcess);
    API(pmGetIntrospectionRoot); API(pmFreeIntrospectionRoot); API(pmSetTelemetryPollingPeriod); API(pmSetEtwFlushPeriod);
    API(pmRegisterDynamicQuery); API(pmFreeDynamicQuery); API(pmPollDynamicQuery); API(pmPollStaticQuery);
    API(pmRegisterFrameQuery); API(pmConsumeFrames); API(pmFreeFrameQuery);
#undef API
    bool Load(const std::filesystem::path& path, std::ostream& log) {
        module = LoadLibraryW(path.c_str());
        log << "loader_path=" << path.string() << " load=" << (module ? "SUCCESS" : "FAIL") << " error=" << GetLastError() << "\n";
        if (!module) return false;
#define RESOLVE(name) name = reinterpret_cast<decltype(name)>(GetProcAddress(module, #name)); log << "symbol " #name "=" << (name ? "PRESENT" : "MISSING") << "\n";
        RESOLVE(pmGetApiVersion); RESOLVE(pmOpenSession); RESOLVE(pmCloseSession); RESOLVE(pmStartTrackingProcess); RESOLVE(pmStopTrackingProcess);
        RESOLVE(pmGetIntrospectionRoot); RESOLVE(pmFreeIntrospectionRoot); RESOLVE(pmSetTelemetryPollingPeriod); RESOLVE(pmSetEtwFlushPeriod);
        RESOLVE(pmRegisterDynamicQuery); RESOLVE(pmFreeDynamicQuery); RESOLVE(pmPollDynamicQuery); RESOLVE(pmPollStaticQuery);
        RESOLVE(pmRegisterFrameQuery); RESOLVE(pmConsumeFrames); RESOLVE(pmFreeFrameQuery);
#undef RESOLVE
        return pmGetApiVersion && pmOpenSession && pmCloseSession;
    }
    ~Api() { if (module) FreeLibrary(module); }
};

static std::string ReadValue(const uint8_t* blob, const PM_QUERY_ELEMENT& q, PM_DATA_TYPE type) {
    if (!blob) return "<none>";
    std::ostringstream o;
    if (type == PM_DATA_TYPE_DOUBLE && q.dataSize >= sizeof(double)) { double v; std::memcpy(&v, blob + q.dataOffset, sizeof(v)); o << v; }
    else if (type == PM_DATA_TYPE_UINT64 && q.dataSize >= sizeof(uint64_t)) { uint64_t v; std::memcpy(&v, blob + q.dataOffset, sizeof(v)); o << v; }
    else if (type == PM_DATA_TYPE_UINT32 && q.dataSize >= sizeof(uint32_t)) { uint32_t v; std::memcpy(&v, blob + q.dataOffset, sizeof(v)); o << v; }
    else if (type == PM_DATA_TYPE_INT32 && q.dataSize >= sizeof(int32_t)) { int32_t v; std::memcpy(&v, blob + q.dataOffset, sizeof(v)); o << v; }
    else if (type == PM_DATA_TYPE_BOOL && q.dataSize >= sizeof(bool)) { bool v; std::memcpy(&v, blob + q.dataOffset, sizeof(v)); o << (v ? "true" : "false"); }
    else if (type == PM_DATA_TYPE_ENUM && q.dataSize >= sizeof(int32_t)) { int32_t v; std::memcpy(&v, blob + q.dataOffset, sizeof(v)); o << v; }
    else if (type == PM_DATA_TYPE_STRING) o << "<string size=" << q.dataSize << ">";
    else o << "<raw size=" << q.dataSize << ">";
    return o.str();
}

static void WriteIntrospection(const PM_INTROSPECTION_ROOT* root, const std::filesystem::path& path) {
    std::ofstream out(path);
    out << "{\n  \"devices\": [\n";
    if (root && root->pDevices) for (size_t i = 0; i < root->pDevices->size; ++i) {
        auto d = static_cast<const PM_INTROSPECTION_DEVICE*>(root->pDevices->pData[i]);
        if (!d) continue;
        out << "    {\"id\":" << d->id << ",\"type\":" << static_cast<int>(d->type) << ",\"vendor\":" << static_cast<int>(d->vendor) << ",\"name\":\"" << JsonEscape(StringValue(d->pName)) << "\",\"luid\":\"" << (d->pLuid ? Hex(d->pLuid->pData, d->pLuid->size) : "") << "\"},\n";
    }
    out << "  ],\n  \"metrics\": [\n";
    if (root && root->pMetrics) for (size_t i = 0; i < root->pMetrics->size; ++i) {
        auto m = static_cast<const PM_INTROSPECTION_METRIC*>(root->pMetrics->pData[i]);
        if (!m) continue;
        out << "    {\"id\":" << static_cast<int>(m->id) << ",\"name\":\"" << MetricName(m->id) << "\",\"type\":" << static_cast<int>(m->type) << ",\"unit\":" << static_cast<int>(m->unit) << ",\"preferredUnit\":" << static_cast<int>(m->preferredUnitHint) << ",\"stats\":[";
        if (m->pStatInfo) for (size_t s = 0; s < m->pStatInfo->size; ++s) { auto si = static_cast<const PM_INTROSPECTION_STAT_INFO*>(m->pStatInfo->pData[s]); if (si) out << static_cast<int>(si->stat) << (s + 1 == m->pStatInfo->size ? "" : ","); }
        out << "],\"devices\":[";
        if (m->pDeviceMetricInfo) for (size_t d = 0; d < m->pDeviceMetricInfo->size; ++d) { auto mi = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(m->pDeviceMetricInfo->pData[d]); if (mi) out << "{\"deviceId\":" << mi->deviceId << ",\"availability\":" << static_cast<int>(mi->availability) << ",\"arraySize\":" << mi->arraySize << "}" << (d + 1 == m->pDeviceMetricInfo->size ? "" : ","); }
        out << "]},\n";
    }
    out << "  ],\n  \"enums\": [\n";
    if (root && root->pEnums) for (size_t i = 0; i < root->pEnums->size; ++i) { auto e = static_cast<const PM_INTROSPECTION_ENUM*>(root->pEnums->pData[i]); if (e) out << "    {\"id\":" << static_cast<int>(e->id) << ",\"symbol\":\"" << JsonEscape(StringValue(e->pSymbol)) << "\",\"keys\":\"" << (e->pKeys ? std::to_string(e->pKeys->size) : "0") << "\"},\n"; }
    out << "  ]\n}\n";
}

struct MetricQuery { PM_QUERY_ELEMENT element{}; PM_DATA_TYPE type{ PM_DATA_TYPE_VOID }; std::string name; };

int wmain(int argc, wchar_t** argv) {
    uint32_t pid = 0;
    std::filesystem::path outDir = std::filesystem::current_path();
    for (int i = 1; i < argc; ++i) {
        if (!wcscmp(argv[i], L"--pid") && i + 1 < argc) pid = static_cast<uint32_t>(_wtoi(argv[++i]));
        else if (!wcscmp(argv[i], L"--out") && i + 1 < argc) outDir = argv[++i];
    }
    std::filesystem::create_directories(outDir);
    std::ofstream log(outDir / "presentmon-api2-diagnostic-rtx4070.log", std::ios::trunc);
    log << "PresentMon API2 RTX 4070 desktop diagnostic\nwall_time=" << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << "\nrequested_pid=" << pid << "\n";
    Api api;
    const auto loaderPath = std::filesystem::absolute(argv[0]).parent_path() / L"PresentMonAPI2Loader.dll";
    if (!api.Load(loaderPath, log)) { std::wcerr << L"PresentMonAPI2Loader.dll load failed\n"; return 2; }
    PM_VERSION version{}; PM_STATUS s = api.pmGetApiVersion(&version);
    log << "pmGetApiVersion status=" << StatusName(s) << " raw=" << static_cast<int>(s) << " version=" << version.major << "." << version.minor << "." << version.patch << "\n";
    PM_SESSION_HANDLE session{}; s = api.pmOpenSession(&session); log << "pmOpenSession status=" << StatusName(s) << " raw=" << static_cast<int>(s) << "\n"; if (s != PM_STATUS_SUCCESS) return 3;
    const PM_INTROSPECTION_ROOT* root{}; s = api.pmGetIntrospectionRoot(session, &root); log << "pmGetIntrospectionRoot status=" << StatusName(s) << " raw=" << static_cast<int>(s) << "\n";
    if (s != PM_STATUS_SUCCESS || !root) { api.pmCloseSession(session); return 4; }
    WriteIntrospection(root, outDir / "presentmon-api2-introspection-rtx4070.json");
    log << "devices=" << (root->pDevices ? root->pDevices->size : 0) << " metrics=" << (root->pMetrics ? root->pMetrics->size : 0) << " enums=" << (root->pEnums ? root->pEnums->size : 0) << "\n";
    std::ofstream all(outDir / "presentmon-api2-all-metrics-rtx4070.txt", std::ios::trunc);
    all << "PresentMon API2 complete metric attempt\n";
    std::vector<MetricQuery> dynamic;
    if (root->pMetrics) for (size_t i = 0; i < root->pMetrics->size; ++i) {
        auto m = static_cast<const PM_INTROSPECTION_METRIC*>(root->pMetrics->pData[i]);
        if (!m || (m->type != PM_METRIC_TYPE_DYNAMIC && m->type != PM_METRIC_TYPE_DYNAMIC_FRAME) || !m->pDeviceMetricInfo) continue;
        PM_STAT stat = PM_STAT_NEWEST_POINT; if (m->pStatInfo && m->pStatInfo->size) stat = static_cast<const PM_INTROSPECTION_STAT_INFO*>(m->pStatInfo->pData[0])->stat;
        for (size_t di = 0; di < m->pDeviceMetricInfo->size; ++di) { auto mi = static_cast<const PM_INTROSPECTION_DEVICE_METRIC_INFO*>(m->pDeviceMetricInfo->pData[di]); if (!mi || mi->availability != PM_METRIC_AVAILABILITY_AVAILABLE) continue; for (uint32_t ai = 0; ai < std::max(1u, mi->arraySize); ++ai) dynamic.push_back({ PM_QUERY_ELEMENT{ m->id, stat, mi->deviceId, ai, 0, 0 }, m->pTypeInfo ? m->pTypeInfo->polledType : PM_DATA_TYPE_VOID, MetricName(m->id) }); }
    }
    if (pid) { s = api.pmStartTrackingProcess(session, pid); log << "pmStartTrackingProcess pid=" << pid << " status=" << StatusName(s) << " raw=" << static_cast<int>(s) << "\n"; }
    for (uint32_t period : {100u, 250u, 500u, 1000u}) { PM_STATUS ps = api.pmSetTelemetryPollingPeriod(session, 0, period); log << "polling_period requested_ms=" << period << " status=" << StatusName(ps) << " raw=" << static_cast<int>(ps) << "\n"; }
    PM_STATUS flush = api.pmSetEtwFlushPeriod(session, 100); log << "etw_flush_period requested_ms=100 status=" << StatusName(flush) << " raw=" << static_cast<int>(flush) << "\n";
    PM_DYNAMIC_QUERY_HANDLE dq{}; s = api.pmRegisterDynamicQuery(session, &dq, dynamic.empty() ? nullptr : &dynamic[0].element, dynamic.size(), 1000, 1020); log << "pmRegisterDynamicQuery elements=" << dynamic.size() << " status=" << StatusName(s) << " raw=" << static_cast<int>(s) << "\n";
    std::vector<uint8_t> blob; if (s == PM_STATUS_SUCCESS && dq) { auto maxEnd = size_t(0); for (auto& q : dynamic) maxEnd = std::max(maxEnd, static_cast<size_t>(q.element.dataOffset + q.element.dataSize)); blob.resize(std::max<size_t>(maxEnd, 4096)); for (uint32_t sample = 0; sample < 20; ++sample) { uint32_t n = 1; auto before = std::chrono::steady_clock::now(); PM_STATUS ps = api.pmPollDynamicQuery(dq, pid, blob.data(), &n); auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - before).count(); all << "sample=" << sample << " query_status=" << StatusName(ps) << " raw=" << static_cast<int>(ps) << " swapchains=" << n << " query_elapsed_ms=" << elapsed << "\n"; if (ps == PM_STATUS_SUCCESS) for (auto& q : dynamic) all << q.name << " metric_id=" << static_cast<int>(q.element.metric) << " device=" << q.element.deviceId << " array=" << q.element.arrayIndex << " value=" << ReadValue(blob.data(), q.element, q.type) << "\n"; std::this_thread::sleep_for(250ms); } api.pmFreeDynamicQuery(dq); }
    if (s != PM_STATUS_SUCCESS) {
        all << "batch_dynamic_query_failed_fallback=per_metric\n";
        for (auto& q : dynamic) {
            PM_DYNAMIC_QUERY_HANDLE one{}; PM_QUERY_ELEMENT element = q.element;
            PM_STATUS rs = api.pmRegisterDynamicQuery(session, &one, &element, 1, 1000, 1020);
            all << "metric_register name=" << q.name << " metric_id=" << static_cast<int>(element.metric) << " device=" << element.deviceId << " status=" << StatusName(rs) << " raw=" << static_cast<int>(rs) << "\n";
            if (rs == PM_STATUS_SUCCESS && one) {
                std::vector<uint8_t> oneBlob(std::max<size_t>(static_cast<size_t>(element.dataOffset + element.dataSize), 4096));
                for (uint32_t sample = 0; sample < 20; ++sample) { uint32_t n = 1; PM_STATUS ps = api.pmPollDynamicQuery(one, pid, oneBlob.data(), &n); all << "metric_sample name=" << q.name << " sample=" << sample << " status=" << StatusName(ps) << " raw=" << static_cast<int>(ps) << " value=" << (ps == PM_STATUS_SUCCESS ? ReadValue(oneBlob.data(), element, q.type) : "<not_appended>") << "\n"; std::this_thread::sleep_for(250ms); }
                api.pmFreeDynamicQuery(one);
            }
        }
    }
    for (auto& q : dynamic) { uint8_t staticBlob[512]{}; PM_STATUS ps = api.pmPollStaticQuery(session, &q.element, pid, staticBlob); all << "static metric=" << q.name << " metric_id=" << static_cast<int>(q.element.metric) << " device=" << q.element.deviceId << " status=" << StatusName(ps) << " raw=" << static_cast<int>(ps) << " value=" << ReadValue(staticBlob, q.element, q.type) << "\n"; }
    std::vector<PM_QUERY_ELEMENT> frames; if (root->pMetrics) for (size_t i = 0; i < root->pMetrics->size; ++i) { auto m = static_cast<const PM_INTROSPECTION_METRIC*>(root->pMetrics->pData[i]); if (m && m->type == PM_METRIC_TYPE_FRAME_EVENT) frames.push_back({ m->id, PM_STAT_NONE, 0, 0, 0, 0 }); }
    PM_FRAME_QUERY_HANDLE fq{}; uint32_t frameBlobSize = 0; s = api.pmRegisterFrameQuery(session, &fq, frames.empty() ? nullptr : frames.data(), frames.size(), &frameBlobSize); all << "pmRegisterFrameQuery elements=" << frames.size() << " status=" << StatusName(s) << " raw=" << static_cast<int>(s) << " blob_size=" << frameBlobSize << "\n";
    if (s == PM_STATUS_SUCCESS && fq) { std::vector<uint8_t> fb(std::max<uint32_t>(frameBlobSize, 1) * 128); uint32_t count = 128; PM_STATUS ps = api.pmConsumeFrames(fq, pid, fb.data(), &count); all << "pmConsumeFrames status=" << StatusName(ps) << " raw=" << static_cast<int>(ps) << " count=" << count << "\n"; api.pmFreeFrameQuery(fq); }
    if (pid) { s = api.pmStopTrackingProcess(session, pid); log << "pmStopTrackingProcess pid=" << pid << " status=" << StatusName(s) << " raw=" << static_cast<int>(s) << "\n"; }
    api.pmFreeIntrospectionRoot(root); s = api.pmCloseSession(session); log << "pmCloseSession status=" << StatusName(s) << " raw=" << static_cast<int>(s) << "\n";
    std::cout << "Diagnostic complete: " << (outDir / "presentmon-api2-diagnostic-rtx4070.log") << "\n";
    return 0;
}
