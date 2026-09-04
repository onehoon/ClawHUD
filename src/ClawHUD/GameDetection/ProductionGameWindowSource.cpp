#include "ProductionGameWindowSource.h"

#include <utility>

namespace clawhud
{
namespace
{
constexpr std::array<DWORD, 6> kObservedEvents{
    EVENT_OBJECT_CREATE,
    EVENT_OBJECT_SHOW,
    EVENT_OBJECT_HIDE,
    EVENT_OBJECT_LOCATIONCHANGE,
    EVENT_OBJECT_DESTROY,
    // Wake-up only: a top-level title change (game loader -> game window) is
    // observed so GameSessionController can re-reconcile the canonical
    // foreground. It is never treated as game evidence on its own.
    EVENT_OBJECT_NAMECHANGE,
};
}

std::mutex ProductionGameWindowSource::activeMutex_;
ProductionGameWindowSource* ProductionGameWindowSource::active_{};

std::optional<ProductionWindowEventType> MapProductionWindowEvent(DWORD event) noexcept
{
    switch (event)
    {
    case EVENT_OBJECT_CREATE: return ProductionWindowEventType::Create;
    case EVENT_OBJECT_SHOW: return ProductionWindowEventType::Show;
    case EVENT_OBJECT_HIDE: return ProductionWindowEventType::Hide;
    case EVENT_OBJECT_LOCATIONCHANGE: return ProductionWindowEventType::LocationChange;
    case EVENT_OBJECT_DESTROY: return ProductionWindowEventType::Destroy;
    case EVENT_OBJECT_NAMECHANGE: return ProductionWindowEventType::NameChange;
    default: return std::nullopt;
    }
}

bool IsProductionWindowObject(LONG objectId, LONG childId) noexcept
{
    return objectId == OBJID_WINDOW && childId == CHILDID_SELF;
}

bool IsProductionTopLevelObservation(HWND hwnd, HWND immediateRoot) noexcept
{
    return hwnd != nullptr && immediateRoot == hwnd;
}

bool ProductionWindowEventQueue::TryPush(ProductionWindowEvent event)
{
    if (events_.size() >= kCapacity)
    {
        ++droppedCount_;
        return false;
    }
    events_.push_back(std::move(event));
    return true;
}

std::optional<ProductionWindowEvent> ProductionWindowEventQueue::TryPop()
{
    if (events_.empty())
        return std::nullopt;
    ProductionWindowEvent event = std::move(events_.front());
    events_.pop_front();
    return event;
}

std::uint64_t ProductionWindowEventQueue::TakeDroppedCount() noexcept
{
    const auto count = droppedCount_;
    droppedCount_ = 0;
    return count;
}

void ProductionWindowEventQueue::Clear() noexcept
{
    events_.clear();
}

ProductionGameWindowSource::~ProductionGameWindowSource()
{
    Stop();
}

bool ProductionGameWindowSource::Start(EventCallback callback) noexcept
{
    Stop();
    if (!callback)
        return false;

    try
    {
        callback_ = std::move(callback);
        worker_ = std::jthread([this](std::stop_token stop) { WorkerMain(stop); });
        accepting_.store(true, std::memory_order_release);
        {
            std::lock_guard lock(activeMutex_);
            active_ = this;
        }

        for (std::size_t index = 0; index < hooks_.size(); ++index)
        {
            hooks_[index] = SetWinEventHook(kObservedEvents[index], kObservedEvents[index],
                nullptr, &ProductionGameWindowSource::WinEventProc, 0, 0,
                WINEVENT_OUTOFCONTEXT);
            if (!hooks_[index])
            {
                accepting_.store(false, std::memory_order_release);
                UnhookAll();
                {
                    std::lock_guard lock(activeMutex_);
                    if (active_ == this)
                        active_ = nullptr;
                }
                StopWorker();
                callback_ = {};
                return false;
            }
        }
        running_.store(true, std::memory_order_release);
        return true;
    }
    catch (...)
    {
        accepting_.store(false, std::memory_order_release);
        UnhookAll();
        {
            std::lock_guard lock(activeMutex_);
            if (active_ == this)
                active_ = nullptr;
        }
        StopWorker();
        callback_ = {};
        return false;
    }
}

void ProductionGameWindowSource::Stop() noexcept
{
    accepting_.store(false, std::memory_order_release);
    UnhookAll();
    {
        std::lock_guard lock(activeMutex_);
        if (active_ == this)
            active_ = nullptr;
    }
    StopWorker();
    callback_ = {};
    running_.store(false, std::memory_order_release);
}

void ProductionGameWindowSource::StopWorker() noexcept
{
    if (worker_.joinable())
    {
        worker_.request_stop();
        queueWake_.notify_all();
        worker_ = std::jthread{};
    }
    std::lock_guard lock(queueMutex_);
    pendingEvents_.Clear();
    (void)pendingEvents_.TakeDroppedCount();
}

void ProductionGameWindowSource::UnhookAll() noexcept
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

void CALLBACK ProductionGameWindowSource::WinEventProc(HWINEVENTHOOK, DWORD event,
    HWND hwnd, LONG objectId, LONG childId, DWORD eventThreadId, DWORD eventTime)
{
    std::lock_guard lock(activeMutex_);
    if (active_)
        active_->AcceptCallback(event, hwnd, objectId, childId, eventThreadId, eventTime);
}

void ProductionGameWindowSource::AcceptCallback(DWORD event, HWND hwnd, LONG objectId,
    LONG childId, DWORD eventThreadId, DWORD eventTime) noexcept
{
    const auto type = MapProductionWindowEvent(event);
    if (!accepting_.load(std::memory_order_acquire) || !type ||
        !IsProductionWindowObject(objectId, childId) || !hwnd)
        return;

    try
    {
        DWORD processId{};
        const DWORD windowThreadId = GetWindowThreadProcessId(hwnd, &processId);
        const HWND immediateRoot = GetAncestor(hwnd, GA_ROOT);
        if (!IsProductionTopLevelObservation(hwnd, immediateRoot))
            return;

        ProductionWindowEvent raw;
        raw.sequence = nextSequence_.fetch_add(1, std::memory_order_relaxed);
        raw.type = *type;
        raw.window = hwnd;
        raw.processId = processId;
        raw.windowThreadId = windowThreadId ? windowThreadId : eventThreadId;
        raw.immediateRoot = immediateRoot;
        raw.immediateTopLevel = true;
        raw.sourceEventTimeMs = eventTime;
        raw.receivedTickMs = GetTickCount64();
        {
            std::lock_guard lock(queueMutex_);
            if (pendingEvents_.TryPush(std::move(raw)))
                queueWake_.notify_one();
        }
    }
    catch (...)
    {
    }
}

void ProductionGameWindowSource::WorkerMain(std::stop_token stop) noexcept
{
    try
    {
        while (true)
        {
            std::optional<ProductionWindowEvent> event;
            {
                std::unique_lock lock(queueMutex_);
                queueWake_.wait(lock, stop,
                    [this] { return pendingEvents_.Size() != 0; });
                event = pendingEvents_.TryPop();
                if (!event && stop.stop_requested())
                    break;
            }
            if (!event)
                continue;
            if (callback_)
            {
                try { callback_(*event); }
                catch (...) {}
            }
        }
    }
    catch (...)
    {
    }
}
}
