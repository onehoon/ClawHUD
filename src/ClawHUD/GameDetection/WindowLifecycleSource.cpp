#include "WindowLifecycleSource.h"

#include "RuntimeLogger.h"

#include <dwmapi.h>

#include <array>
#include <sstream>

namespace clawhud
{
namespace
{
constexpr std::array<DWORD, 5> kObservedEvents{
    EVENT_OBJECT_CREATE,
    EVENT_OBJECT_DESTROY,
    EVENT_OBJECT_SHOW,
    EVENT_OBJECT_HIDE,
    EVENT_OBJECT_NAMECHANGE,
};

void LogDebug(const std::wstring& message) noexcept
{
    RuntimeLogger::Log(RuntimeLogLevel::Debug, L"[WindowLifecycle] " + message);
}

void LogWarning(const std::wstring& message) noexcept
{
    RuntimeLogger::Log(RuntimeLogLevel::Warn, L"[WindowLifecycle] " + message);
}

std::wstring Hex(std::uintptr_t value)
{
    std::wstringstream stream;
    stream << L"0x" << std::hex << value;
    return stream.str();
}

std::wstring HexSigned(LONG_PTR value)
{
    return Hex(static_cast<std::uintptr_t>(value));
}

std::wstring HexHandle(HWND hwnd)
{
    return Hex(reinterpret_cast<std::uintptr_t>(hwnd));
}

std::wstring EventNameFromType(WindowLifecycleEventType type)
{
    return WindowLifecycleEventName(type);
}

void ReadWindowText(HWND hwnd, std::wstring& title, std::wstring& className) noexcept
{
    try
    {
        std::array<wchar_t, 1025> titleBuffer{};
        const int titleLength = GetWindowTextW(hwnd, titleBuffer.data(),
            static_cast<int>(titleBuffer.size()));
        if (titleLength > 0)
            title.assign(titleBuffer.data(), static_cast<std::size_t>(titleLength));

        std::array<wchar_t, 257> classBuffer{};
        const int classLength = GetClassNameW(hwnd, classBuffer.data(),
            static_cast<int>(classBuffer.size()));
        if (classLength > 0)
            className.assign(classBuffer.data(), static_cast<std::size_t>(classLength));
    }
    catch (...) {}
}
}

std::mutex WindowLifecycleSource::activeMutex_;
WindowLifecycleSource* WindowLifecycleSource::active_{};

std::optional<WindowLifecycleEventType> MapWinEvent(DWORD event) noexcept
{
    switch (event)
    {
    case EVENT_OBJECT_CREATE: return WindowLifecycleEventType::Create;
    case EVENT_OBJECT_DESTROY: return WindowLifecycleEventType::Destroy;
    case EVENT_OBJECT_SHOW: return WindowLifecycleEventType::Show;
    case EVENT_OBJECT_HIDE: return WindowLifecycleEventType::Hide;
    case EVENT_OBJECT_NAMECHANGE: return WindowLifecycleEventType::NameChange;
    default: return std::nullopt;
    }
}

bool IsWindowLifecycleObject(LONG objectId, LONG childId) noexcept
{
    return objectId == OBJID_WINDOW && childId == CHILDID_SELF;
}

std::wstring WindowLifecycleEventName(WindowLifecycleEventType type)
{
    switch (type)
    {
    case WindowLifecycleEventType::Create: return L"CREATE";
    case WindowLifecycleEventType::Destroy: return L"DESTROY";
    case WindowLifecycleEventType::Show: return L"SHOW";
    case WindowLifecycleEventType::Hide: return L"HIDE";
    case WindowLifecycleEventType::NameChange: return L"NAMECHANGE";
    }
    return L"UNKNOWN";
}

std::wstring EscapeWindowLifecycleValue(std::wstring_view value)
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

void WindowLifecycleCache::ReplaceOnCreate(const WindowSnapshot& snapshot)
{
    snapshots_[snapshot.hwnd] = snapshot;
}

void WindowLifecycleCache::Update(const WindowSnapshot& snapshot)
{
    snapshots_[snapshot.hwnd] = snapshot;
}

std::optional<WindowSnapshot> WindowLifecycleCache::Find(HWND hwnd) const
{
    const auto it = snapshots_.find(hwnd);
    return it == snapshots_.end() ? std::nullopt : std::optional{it->second};
}

std::optional<WindowSnapshot> WindowLifecycleCache::EvictIfOverCapacity()
{
    if (snapshots_.size() <= kCapacity) return std::nullopt;
    const auto oldest = snapshots_.begin();
    WindowSnapshot snapshot = oldest->second;
    snapshots_.erase(oldest);
    return snapshot;
}

void WindowLifecycleCache::Remove(HWND hwnd)
{
    snapshots_.erase(hwnd);
}

void WindowLifecycleCache::Clear() noexcept
{
    snapshots_.clear();
}

bool WindowLifecycleQueue::TryPush(RawWindowLifecycleEvent event)
{
    if (events_.size() >= kCapacity)
    {
        ++droppedCount_;
        return false;
    }
    events_.push_back(std::move(event));
    return true;
}

std::optional<RawWindowLifecycleEvent> WindowLifecycleQueue::TryPop()
{
    if (events_.empty()) return std::nullopt;
    RawWindowLifecycleEvent event = std::move(events_.front());
    events_.pop_front();
    return event;
}

std::uint64_t WindowLifecycleQueue::TakeDroppedCount() noexcept
{
    const auto count = droppedCount_;
    droppedCount_ = 0;
    return count;
}

void WindowLifecycleQueue::Clear() noexcept
{
    events_.clear();
}

WindowLifecycleSource::~WindowLifecycleSource()
{
    Stop();
}

bool WindowLifecycleSource::Start() noexcept
{
    Stop();
    try
    {
        worker_ = std::jthread([this](std::stop_token stop) { WorkerMain(stop); });
        accepting_.store(true, std::memory_order_release);
        {
            std::lock_guard lock(activeMutex_);
            active_ = this;
        }

        for (std::size_t index = 0; index < hooks_.size(); ++index)
        {
            hooks_[index] = SetWinEventHook(kObservedEvents[index], kObservedEvents[index],
                nullptr, &WindowLifecycleSource::WinEventProc, 0, 0,
                WINEVENT_OUTOFCONTEXT);
            if (!hooks_[index])
            {
                LogFailure(WindowLifecycleEventName(*MapWinEvent(kObservedEvents[index])).c_str(),
                    GetLastError());
                accepting_.store(false, std::memory_order_release);
                UnhookAll();
                {
                    std::lock_guard lock(activeMutex_);
                    if (active_ == this) active_ = nullptr;
                }
                StopWorker();
                return false;
            }
        }
        running_.store(true, std::memory_order_release);
        LogDebug(L"start.result=SUCCESS");
        return true;
    }
    catch (...)
    {
        accepting_.store(false, std::memory_order_release);
        UnhookAll();
        {
            std::lock_guard lock(activeMutex_);
            if (active_ == this) active_ = nullptr;
        }
        StopWorker();
        LogWarning(L"start.result=API_FAILED stage=UnexpectedException");
        return false;
    }
}

void WindowLifecycleSource::Stop() noexcept
{
    accepting_.store(false, std::memory_order_release);
    UnhookAll();
    {
        std::lock_guard lock(activeMutex_);
        if (active_ == this) active_ = nullptr;
    }
    StopWorker();
    running_.store(false, std::memory_order_release);
    cache_.Clear();
}

void WindowLifecycleSource::StopWorker() noexcept
{
    if (worker_.joinable())
    {
        worker_.request_stop();
        queueWake_.notify_all();
        worker_ = std::jthread{};
    }
    std::uint64_t dropped{};
    {
        std::lock_guard lock(queueMutex_);
        dropped = pendingEvents_.TakeDroppedCount();
        pendingEvents_.Clear();
    }
    if (dropped) LogDropped(dropped);
}

void WindowLifecycleSource::UnhookAll() noexcept
{
    for (auto& hook : hooks_)
    {
        if (hook)
        {
            UnhookWinEvent(hook);
            hook = nullptr;
        }
    }
}

void CALLBACK WindowLifecycleSource::WinEventProc(HWINEVENTHOOK, DWORD event, HWND hwnd,
    LONG objectId, LONG childId, DWORD eventThreadId, DWORD eventTime)
{
    std::lock_guard lock(activeMutex_);
    if (active_) active_->AcceptCallback(event, hwnd, objectId, childId,
        eventThreadId, eventTime);
}

void WindowLifecycleSource::AcceptCallback(DWORD event, HWND hwnd, LONG objectId,
    LONG childId, DWORD eventThreadId, DWORD eventTime) noexcept
{
    if (!accepting_.load(std::memory_order_acquire) || !MapWinEvent(event) ||
        !IsWindowLifecycleObject(objectId, childId) || !hwnd)
        return;

    try
    {
        RawWindowLifecycleEvent raw;
        raw.sequence = nextSequence_.fetch_add(1, std::memory_order_relaxed);
        raw.event = event;
        raw.hwnd = hwnd;
        raw.objectId = objectId;
        raw.childId = childId;
        raw.eventThreadId = eventThreadId;
        raw.sourceEventTimeMs = eventTime;
        raw.receivedTickMs = GetTickCount64();
        raw.immediateWindowThreadId = GetWindowThreadProcessId(hwnd,
            &raw.immediateProcessId);
        {
            std::lock_guard lock(queueMutex_);
            if (pendingEvents_.TryPush(std::move(raw)))
                queueWake_.notify_one();
        }
    }
    catch (...)
    {
        LogWarning(L"event.result=API_FAILED reason=enqueue-exception");
    }
}

bool WindowLifecycleSource::IsLiveTopLevelWindow(HWND hwnd) noexcept
{
    return IsWindow(hwnd) != FALSE && GetAncestor(hwnd, GA_ROOT) == hwnd;
}

std::optional<WindowSnapshot> WindowLifecycleSource::CaptureSnapshot(HWND hwnd,
    DWORD fallbackProcessId, DWORD fallbackThreadId) noexcept
{
    try
    {
        WindowSnapshot snapshot;
        snapshot.hwnd = hwnd;
        snapshot.processId = fallbackProcessId;
        snapshot.windowThreadId = fallbackThreadId;
        DWORD processId{};
        const DWORD threadId = GetWindowThreadProcessId(hwnd, &processId);
        if (threadId) snapshot.windowThreadId = threadId;
        if (processId) snapshot.processId = processId;
        snapshot.title.reserve(1024);
        snapshot.className.reserve(256);
        ReadWindowText(hwnd, snapshot.title, snapshot.className);
        snapshot.style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        snapshot.exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
        snapshot.visible = IsWindowVisible(hwnd) != FALSE;
        snapshot.iconic = IsIconic(hwnd) != FALSE;
        snapshot.owner = GetWindow(hwnd, GW_OWNER);
        snapshot.root = GetAncestor(hwnd, GA_ROOT);
        snapshot.rectangleAvailable = GetWindowRect(hwnd, &snapshot.rectangle) != FALSE;
        DWORD cloaked{};
        snapshot.cloakedAvailable = SUCCEEDED(DwmGetWindowAttribute(hwnd,
            DWMWA_CLOAKED, &cloaked, sizeof(cloaked)));
        snapshot.cloaked = cloaked != 0;
        return snapshot;
    }
    catch (...) { return std::nullopt; }
}

void WindowLifecycleSource::WorkerMain(std::stop_token stop) noexcept
{
    try
    {
        while (true)
        {
            std::optional<RawWindowLifecycleEvent> event;
            {
                std::unique_lock lock(queueMutex_);
                queueWake_.wait(lock, stop, [this] { return pendingEvents_.Size() != 0; });
                event = pendingEvents_.TryPop();
                if (!event && stop.stop_requested()) break;
            }
            if (!event) continue;
            std::uint64_t dropped{};
            {
                std::lock_guard lock(queueMutex_);
                dropped = pendingEvents_.TakeDroppedCount();
            }
            if (dropped) LogDropped(dropped);
            ProcessEvent(*event);
        }
    }
    catch (...)
    {
        LogWarning(L"worker.result=API_FAILED reason=unexpected-exception");
    }
}

void WindowLifecycleSource::ProcessEvent(const RawWindowLifecycleEvent& event) noexcept
{
    try
    {
        const auto type = MapWinEvent(event.event);
        if (!type) return;

        if (*type != WindowLifecycleEventType::Destroy &&
            !IsLiveTopLevelWindow(event.hwnd))
            return;

        if (*type == WindowLifecycleEventType::Destroy)
        {
            if (IsLiveTopLevelWindow(event.hwnd))
            {
                const auto live = CaptureSnapshot(event.hwnd, event.immediateProcessId,
                    event.immediateWindowThreadId);
                if (live)
                {
                    LogEvent(event, *live, L"LIVE");
                    cache_.Remove(event.hwnd);
                    return;
                }
            }
            if (const auto cached = cache_.Find(event.hwnd))
            {
                LogEvent(event, *cached, L"CACHED");
                cache_.Remove(event.hwnd);
                return;
            }
            WindowSnapshot partial;
            partial.hwnd = event.hwnd;
            partial.processId = event.immediateProcessId;
            partial.windowThreadId = event.immediateWindowThreadId;
            partial.root = event.hwnd;
            LogEvent(event, partial, L"PARTIAL");
            return;
        }

        const auto snapshot = CaptureSnapshot(event.hwnd, event.immediateProcessId,
            event.immediateWindowThreadId);
        if (!snapshot) return;
        if (*type == WindowLifecycleEventType::Create)
            cache_.ReplaceOnCreate(*snapshot);
        else
            cache_.Update(*snapshot);
        if (const auto evicted = cache_.EvictIfOverCapacity())
            LogWarning(L"cache.result=EVICTED hwnd=" + HexHandle(evicted->hwnd) +
                L" pid=" + std::to_wstring(evicted->processId));
        LogEvent(event, *snapshot, L"LIVE");
    }
    catch (...)
    {
        LogWarning(L"event.result=API_FAILED reason=worker-exception");
    }
}

void WindowLifecycleSource::LogEvent(const RawWindowLifecycleEvent& event,
    const WindowSnapshot& snapshot, const wchar_t* metadataSource) noexcept
{
    try
    {
        const auto type = MapWinEvent(event.event);
        if (!type) return;
        std::wstringstream message;
        message << L"seq=" << event.sequence
            << L" event=" << EventNameFromType(*type)
            << L" sourceEventTimeMs=" << event.sourceEventTimeMs
            << L" receivedTickMs=" << event.receivedTickMs
            << L" hwnd=" << HexHandle(snapshot.hwnd)
            << L" pid=" << snapshot.processId
            << L" windowThreadId=" << snapshot.windowThreadId
            << L" eventThreadId=" << event.eventThreadId
            << L" title=\"" << EscapeWindowLifecycleValue(snapshot.title) << L"\""
            << L" class=\"" << EscapeWindowLifecycleValue(snapshot.className) << L"\""
            << L" visible=" << snapshot.visible
            << L" iconic=" << snapshot.iconic
            << L" owner=" << HexHandle(snapshot.owner)
            << L" root=" << HexHandle(snapshot.root)
            << L" style=" << HexSigned(snapshot.style)
            << L" exStyle=" << HexSigned(snapshot.exStyle)
            << L" metadataSource=" << metadataSource;
        if (snapshot.rectangleAvailable)
            message << L" rect=" << snapshot.rectangle.left << L"," << snapshot.rectangle.top
                << L"," << snapshot.rectangle.right << L"," << snapshot.rectangle.bottom;
        if (snapshot.cloakedAvailable) message << L" cloaked=" << snapshot.cloaked;
        LogDebug(message.str());
    }
    catch (...) {}
}

void WindowLifecycleSource::LogDropped(std::uint64_t count) noexcept
{
    try
    {
        LogWarning(L"queue.result=DROPPED count=" + std::to_wstring(count));
    }
    catch (...) {}
}

void WindowLifecycleSource::LogFailure(const wchar_t* eventName, DWORD error) noexcept
{
    try
    {
        LogWarning(std::wstring(L"start.result=API_FAILED stage=SetWinEventHook event=") +
            eventName + L" error=" + std::to_wstring(error));
    }
    catch (...) {}
}
}
