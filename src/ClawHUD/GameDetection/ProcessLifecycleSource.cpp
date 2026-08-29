#include "ProcessLifecycleSource.h"

#include "RuntimeLogger.h"

#include <algorithm>
#include <cerrno>
#include <cwchar>
#include <limits>
#include <sstream>

namespace clawhud
{
namespace
{
struct ScopedBstr
{
    explicit ScopedBstr(const wchar_t* value) : value(SysAllocString(value)) {}
    ~ScopedBstr() { SysFreeString(value); }
    BSTR value{};
};

std::wstring HresultText(HRESULT hr)
{
    wchar_t buffer[11]{};
    swprintf_s(buffer, L"0x%08X", static_cast<unsigned int>(hr));
    return buffer;
}

void LogDebug(const std::wstring& message) noexcept
{
    RuntimeLogger::Log(RuntimeLogLevel::Debug, L"[ProcessLifecycle] " + message);
}

}

bool ParseWmiUint32Variant(const VARIANT& variant,
    std::optional<DWORD>& value) noexcept
{
    if (variant.vt == VT_I4)
    {
        value = static_cast<DWORD>(variant.lVal);
        return true;
    }
    if (variant.vt == VT_UI4)
    {
        value = variant.ulVal;
        return true;
    }
    if (variant.vt == VT_UI8 && variant.ullVal <= std::numeric_limits<DWORD>::max())
    {
        value = static_cast<DWORD>(variant.ullVal);
        return true;
    }
    return false;
}

bool ParseWmiUint64Variant(const VARIANT& variant,
    std::optional<std::uint64_t>& value) noexcept
{
    if (variant.vt == VT_BSTR && variant.bstrVal)
    {
        const wchar_t* text = variant.bstrVal;
        if (!text[0]) return false;
        const bool hexadecimal = text[0] == L'0' &&
            (text[1] == L'x' || text[1] == L'X');
        const wchar_t* number = hexadecimal ? text + 2 : text;
        if (!number[0]) return false;
        errno = 0;
        wchar_t* end{};
        const auto parsed = std::wcstoull(number, &end, hexadecimal ? 16 : 10);
        if (errno == 0 && end && end != number && *end == L'\0')
        {
            value = static_cast<std::uint64_t>(parsed);
            return true;
        }
        return false;
    }
    if (variant.vt == VT_UI8)
    {
        value = variant.ullVal;
        return true;
    }
    if (variant.vt == VT_UI4)
    {
        value = variant.ulVal;
        return true;
    }
    if (variant.vt == VT_I8 && variant.llVal >= 0)
    {
        value = static_cast<std::uint64_t>(variant.llVal);
        return true;
    }
    return false;
}

namespace
{

bool ReadUInt32(IWbemClassObject* object, const wchar_t* name, std::optional<DWORD>& value)
{
    VARIANT variant{};
    VariantInit(&variant);
    const HRESULT hr = object->Get(name, 0, &variant, nullptr, nullptr);
    if (FAILED(hr))
    {
        VariantClear(&variant);
        return false;
    }
    const bool ok = ParseWmiUint32Variant(variant, value);
    VariantClear(&variant);
    return ok;
}

bool ReadUInt64(IWbemClassObject* object, const wchar_t* name,
    std::optional<std::uint64_t>& value)
{
    VARIANT variant{};
    VariantInit(&variant);
    const HRESULT hr = object->Get(name, 0, &variant, nullptr, nullptr);
    if (FAILED(hr))
    {
        VariantClear(&variant);
        return false;
    }
    const bool ok = ParseWmiUint64Variant(variant, value);
    VariantClear(&variant);
    return ok;
}

bool ReadString(IWbemClassObject* object, const wchar_t* name,
    std::optional<std::wstring>& value)
{
    VARIANT variant{};
    VariantInit(&variant);
    const HRESULT hr = object->Get(name, 0, &variant, nullptr, nullptr);
    if (FAILED(hr))
    {
        VariantClear(&variant);
        return false;
    }
    if (variant.vt == VT_BSTR && variant.bstrVal)
        value = std::wstring(variant.bstrVal, SysStringLen(variant.bstrVal));
    else
    {
        VariantClear(&variant);
        return false;
    }
    VariantClear(&variant);
    return true;
}

ProcessLifecycleTraceFields ReadFields(ProcessLifecycleEventType type,
    IWbemClassObject* object)
{
    ProcessLifecycleTraceFields fields;
    ReadUInt32(object, L"ProcessID", fields.processId);
    ReadUInt32(object, L"ParentProcessID", fields.parentProcessId);
    ReadUInt32(object, L"SessionID", fields.sessionId);
    ReadString(object, L"ProcessName", fields.processName);
    ReadUInt64(object, L"TIME_CREATED", fields.sourceTimestamp);
    if (type == ProcessLifecycleEventType::Stop)
        ReadUInt32(object, L"ExitStatus", fields.exitStatus);
    return fields;
}

const wchar_t* EventName(ProcessLifecycleEventType type) noexcept
{
    return type == ProcessLifecycleEventType::Start ? L"START" : L"STOP";
}
}

std::wstring EscapeProcessLifecycleValue(std::wstring_view value)
{
    std::wstring result;
    result.reserve(value.size());
    for (const wchar_t character : value)
    {
        if (character == L'\\') result += L"\\\\";
        else if (character == L'\"') result += L"\\\"";
        else if (character == L'\r') result += L"\\r";
        else if (character == L'\n') result += L"\\n";
        else if (character == L'\t') result += L"\\t";
        else result += character;
    }
    return result;
}

std::optional<ProcessLifecycleEvent> MapProcessLifecycleTraceEvent(
    ProcessLifecycleEventType type, const ProcessLifecycleTraceFields& fields,
    std::uint64_t sequence, ULONGLONG receivedTickMs)
{
    if (!fields.processId || !fields.parentProcessId || !fields.sessionId ||
        !fields.processName || !fields.sourceTimestamp)
        return std::nullopt;
    if (type == ProcessLifecycleEventType::Stop && !fields.exitStatus)
        return std::nullopt;

    ProcessLifecycleEvent event;
    event.sequence = sequence;
    event.type = type;
    event.processId = *fields.processId;
    event.parentProcessId = *fields.parentProcessId;
    event.sessionId = *fields.sessionId;
    event.processName = *fields.processName;
    event.sourceTimestamp = *fields.sourceTimestamp;
    event.receivedTickMs = receivedTickMs;
    if (type == ProcessLifecycleEventType::Stop)
        event.exitStatus = fields.exitStatus;
    return event;
}

class ProcessLifecycleSource::EventSink final : public IWbemObjectSink
{
public:
    EventSink(ProcessLifecycleSource& source, ProcessLifecycleEventType type)
        : source_(source), type_(type) {}

    STDMETHODIMP QueryInterface(REFIID iid, void** object) override
    {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid == IID_IUnknown || iid == IID_IWbemObjectSink)
        {
            *object = static_cast<IWbemObjectSink*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    STDMETHODIMP_(ULONG) AddRef() override
    {
        return static_cast<ULONG>(InterlockedIncrement(&references_));
    }

    STDMETHODIMP_(ULONG) Release() override
    {
        const LONG references = InterlockedDecrement(&references_);
        if (!references) delete this;
        return static_cast<ULONG>(references);
    }

    STDMETHODIMP Indicate(long count, IWbemClassObject** objects) override
    {
        try
        {
            if (!objects || count <= 0) return WBEM_S_NO_ERROR;
            for (long index = 0; index < count; ++index)
                if (objects[index]) source_.HandleEvent(type_, objects[index]);
        }
        catch (...)
        {
            LogDebug(L"callback.result=API_FAILED reason=unexpected-exception");
        }
        return WBEM_S_NO_ERROR;
    }

    STDMETHODIMP SetStatus(LONG, HRESULT, BSTR, IWbemClassObject*) override
    {
        return WBEM_S_NO_ERROR;
    }

private:
    ~EventSink() = default;

    LONG references_{1};
    ProcessLifecycleSource& source_;
    ProcessLifecycleEventType type_;
};

ProcessLifecycleSource::~ProcessLifecycleSource()
{
    Stop();
}

void ProcessLifecycleSource::LogFailure(const wchar_t* operation,
    const wchar_t* stage, HRESULT hr) noexcept
{
    try
    {
        RuntimeLogger::Log(RuntimeLogLevel::Warn,
            std::wstring(L"[ProcessLifecycle] ") + operation +
            L".result=API_FAILED stage=" + stage + L" hr=" + HresultText(hr));
    }
    catch (...) {}
}

HRESULT ProcessLifecycleSource::StartSubscription(ProcessLifecycleEventType type,
    EventSink* sink, Microsoft::WRL::ComPtr<IWbemObjectSink>& retainedSink) noexcept
{
    const wchar_t* queryText = type == ProcessLifecycleEventType::Start
        ? L"SELECT * FROM Win32_ProcessStartTrace"
        : L"SELECT * FROM Win32_ProcessStopTrace";
    ScopedBstr language(L"WQL");
    ScopedBstr query(queryText);
    Microsoft::WRL::ComPtr<IWbemObjectSink> sinkReference(sink);
    const HRESULT hr = services_->ExecNotificationQueryAsync(
        language.value, query.value,
        WBEM_FLAG_SEND_STATUS, nullptr, sinkReference.Get());
    if (FAILED(hr)) return hr;
    retainedSink = std::move(sinkReference);
    return S_OK;
}

bool ProcessLifecycleSource::Start() noexcept
{
    if (Running()) return true;

    try
    {
        const HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(comHr) && comHr != RPC_E_CHANGED_MODE)
        {
            LogFailure(L"start", L"CoInitialize", comHr);
            return false;
        }
        comOwned_ = comHr == S_OK || comHr == S_FALSE;

        HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&locator_));
        if (FAILED(hr))
        {
            LogFailure(L"start", L"CoCreateInstance", hr);
            CleanupWmi();
            return false;
        }

        ScopedBstr namespaceName(L"ROOT\\CIMV2");
        hr = locator_->ConnectServer(namespaceName.value, nullptr, nullptr, nullptr,
            0, nullptr, nullptr, &services_);
        if (FAILED(hr))
        {
            LogFailure(L"start", L"ConnectServer", hr);
            CleanupWmi();
            return false;
        }
        hr = CoSetProxyBlanket(services_.Get(), RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE,
            nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr,
            EOAC_NONE);
        if (FAILED(hr))
        {
            LogFailure(L"start", L"SetProxyBlanket", hr);
            CleanupWmi();
            return false;
        }

        worker_ = std::jthread([this](std::stop_token stop) { WorkerMain(stop); });
        auto* startSink = new EventSink(*this, ProcessLifecycleEventType::Start);
        const HRESULT startSubscriptionHr = StartSubscription(
            ProcessLifecycleEventType::Start, startSink, startSink_);
        startSink->Release();
        if (FAILED(startSubscriptionHr))
        {
            LogFailure(L"start", L"StartSubscription", startSubscriptionHr);
            CleanupWmi();
            return false;
        }
        auto* stopSink = new EventSink(*this, ProcessLifecycleEventType::Stop);
        const HRESULT stopSubscriptionHr = StartSubscription(
            ProcessLifecycleEventType::Stop, stopSink, stopSink_);
        stopSink->Release();
        if (FAILED(stopSubscriptionHr))
        {
            LogFailure(L"start", L"StopSubscription", stopSubscriptionHr);
            CleanupWmi();
            return false;
        }
        running_.store(true, std::memory_order_release);
        LogDebug(L"start.result=SUCCESS");
        return true;
    }
    catch (...)
    {
        LogFailure(L"start", L"UnexpectedException", E_FAIL);
        CleanupWmi();
        return false;
    }
}

void ProcessLifecycleSource::HandleEvent(ProcessLifecycleEventType type,
    IWbemClassObject* object) noexcept
{
    try
    {
        const auto fields = ReadFields(type, object);
        const auto sequence = nextSequence_.fetch_add(1, std::memory_order_relaxed);
        const auto event = MapProcessLifecycleTraceEvent(type, fields, sequence,
            GetTickCount64());
        if (event)
            Enqueue(*event);
        else
            LogDebug(L"event.result=INVALID reason=missing-required-field");
    }
    catch (...)
    {
        LogDebug(L"event.result=API_FAILED reason=unexpected-exception");
    }
}

void ProcessLifecycleSource::Enqueue(ProcessLifecycleEvent event) noexcept
{
    try
    {
        {
            std::lock_guard lock(queueMutex_);
            pendingEvents_.push_back(std::move(event));
        }
        queueWake_.notify_one();
    }
    catch (...)
    {
        LogDebug(L"event.result=API_FAILED reason=queue-failure");
    }
}

void ProcessLifecycleSource::WorkerMain(std::stop_token stop) noexcept
{
    try
    {
        while (true)
        {
            ProcessLifecycleEvent event;
            {
                std::unique_lock lock(queueMutex_);
                queueWake_.wait(lock, stop, [this] { return !pendingEvents_.empty(); });
                if (pendingEvents_.empty() && stop.stop_requested()) return;
                if (pendingEvents_.empty()) continue;
                event = std::move(pendingEvents_.front());
                pendingEvents_.pop_front();
            }
            LogEvent(event);
        }
    }
    catch (...)
    {
        LogDebug(L"worker.result=API_FAILED reason=unexpected-exception");
    }
}

void ProcessLifecycleSource::LogEvent(const ProcessLifecycleEvent& event) noexcept
{
    try
    {
        std::wstringstream message;
        message << L"seq=" << event.sequence << L" event=" << EventName(event.type)
            << L" sourceTime=" << event.sourceTimestamp
            << L" receivedTickMs=" << event.receivedTickMs
            << L" pid=" << event.processId
            << L" parentPid=" << event.parentProcessId
            << L" sessionId=" << event.sessionId
            << L" processName=\"" << EscapeProcessLifecycleValue(event.processName) << L"\"";
        if (event.exitStatus)
            message << L" exitStatus=" << *event.exitStatus;
        LogDebug(message.str());
    }
    catch (...) {}
}

void ProcessLifecycleSource::CleanupWmi() noexcept
{
    running_.store(false, std::memory_order_release);
    if (services_)
    {
        if (startSink_) services_->CancelAsyncCall(startSink_.Get());
        if (stopSink_) services_->CancelAsyncCall(stopSink_.Get());
    }
    startSink_.Reset();
    stopSink_.Reset();
    services_.Reset();
    locator_.Reset();
    if (worker_.joinable())
    {
        worker_.request_stop();
        queueWake_.notify_all();
        worker_ = std::jthread{};
    }
    {
        std::lock_guard lock(queueMutex_);
        pendingEvents_.clear();
    }
    if (comOwned_)
    {
        CoUninitialize();
        comOwned_ = false;
    }
}

void ProcessLifecycleSource::Stop() noexcept
{
    const bool wasRunning = running_.exchange(false, std::memory_order_acq_rel);
    const bool hadResources = wasRunning || locator_ || services_ || worker_.joinable();
    CleanupWmi();
    if (hadResources)
        LogDebug(L"stop.result=SUCCESS");
}
}
