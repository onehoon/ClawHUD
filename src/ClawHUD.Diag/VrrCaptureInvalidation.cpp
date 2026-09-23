#include "VrrCaptureInvalidation.h"

#include <utility>

std::atomic<VrrForegroundEventHook*> VrrForegroundEventHook::active_{};

void VrrForegroundChangeTracker::Reset(DWORD targetProcessId) noexcept
{
    epochActive_.store(false, std::memory_order_release);
    epochEnded_.store(false, std::memory_order_release);
    epochEndKnown_.store(false, std::memory_order_release);
    foreignForegroundObserved_.store(false, std::memory_order_release);
    targetProcessId_.store(targetProcessId, std::memory_order_release);
}

void VrrForegroundChangeTracker::BeginMeasurementEpoch(DWORD startTimeMs) noexcept
{
    foreignForegroundObserved_.store(false, std::memory_order_release);
    epochStartTimeMs_.store(startTimeMs, std::memory_order_relaxed);
    epochEndTimeMs_.store(0, std::memory_order_relaxed);
    epochEnded_.store(false, std::memory_order_relaxed);
    epochEndKnown_.store(false, std::memory_order_relaxed);
    epochActive_.store(true, std::memory_order_release);
}

void VrrForegroundChangeTracker::SetMeasurementEndBoundary(DWORD endTimeMs) noexcept
{
    if (!epochActive_.load(std::memory_order_acquire) ||
        epochEnded_.load(std::memory_order_acquire))
        return;
    epochEndTimeMs_.store(endTimeMs, std::memory_order_relaxed);
    epochEndKnown_.store(true, std::memory_order_release);
}

void VrrForegroundChangeTracker::EndMeasurementEpoch(DWORD endTimeMs) noexcept
{
    if (!epochActive_.load(std::memory_order_acquire) ||
        epochEnded_.load(std::memory_order_acquire))
        return;
    epochEndTimeMs_.store(endTimeMs, std::memory_order_relaxed);
    epochEndKnown_.store(true, std::memory_order_release);
    epochEnded_.store(true, std::memory_order_release);
}

void VrrForegroundChangeTracker::ObserveForegroundProcess(
    DWORD processId, DWORD eventTimeMs) noexcept
{
    if (!processId || !epochActive_.load(std::memory_order_acquire) ||
        processId == targetProcessId_.load(std::memory_order_acquire))
        return;

    // WinEvent event times are GetTickCount values. Unsigned subtraction keeps
    // this comparison valid across the DWORD tick-count wrap for short epochs.
    const auto startElapsed = static_cast<DWORD>(eventTimeMs -
        epochStartTimeMs_.load(std::memory_order_relaxed));
    if (startElapsed >= 0x80000000u) return;
    if (epochEndKnown_.load(std::memory_order_acquire))
    {
        const auto endElapsed = static_cast<DWORD>(
            epochEndTimeMs_.load(std::memory_order_relaxed) -
            epochStartTimeMs_.load(std::memory_order_relaxed));
        if (endElapsed >= 0x80000000u || startElapsed >= endElapsed) return;
    }
    foreignForegroundObserved_.store(true, std::memory_order_release);
}

bool VrrForegroundChangeTracker::ForeignForegroundObserved() const noexcept
{
    return foreignForegroundObserved_.load(std::memory_order_acquire);
}

VrrForegroundEventHook::~VrrForegroundEventHook()
{
    Stop();
}

bool VrrForegroundEventHook::Start(DWORD targetProcessId) noexcept
{
    Stop();
    if (!targetProcessId) return false;

    shutdownEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!shutdownEvent_) return false;
    tracker_.Reset(targetProcessId);

    std::promise<bool> ready;
    auto result = ready.get_future();
    try
    {
        thread_ = std::thread(&VrrForegroundEventHook::ThreadMain, this,
            std::move(ready));
    }
    catch (...)
    {
        CloseHandle(shutdownEvent_);
        shutdownEvent_ = nullptr;
        return false;
    }

    bool registered{};
    try
    {
        registered = result.get();
    }
    catch (...)
    {
        Stop();
        return false;
    }
    if (!registered)
    {
        Stop();
        return false;
    }
    return true;
}

void VrrForegroundEventHook::BeginMeasurementEpoch(DWORD startTimeMs) noexcept
{
    tracker_.BeginMeasurementEpoch(startTimeMs);
}

void VrrForegroundEventHook::EndMeasurementEpoch(DWORD endTimeMs) noexcept
{
    tracker_.EndMeasurementEpoch(endTimeMs);
}

void VrrForegroundEventHook::SetMeasurementEndBoundary(DWORD endTimeMs) noexcept
{
    tracker_.SetMeasurementEndBoundary(endTimeMs);
}

bool VrrForegroundEventHook::ForeignForegroundObserved() const noexcept
{
    return tracker_.ForeignForegroundObserved();
}

bool VrrForegroundEventHook::Running() const noexcept
{
    return running_.load(std::memory_order_acquire);
}

void VrrForegroundEventHook::Stop() noexcept
{
    tracker_.EndMeasurementEpoch(GetTickCount());
    if (thread_.joinable())
    {
        if (shutdownEvent_ && !SetEvent(shutdownEvent_))
        {
            const auto id = threadId_.load(std::memory_order_acquire);
            if (id) PostThreadMessageW(id, WM_QUIT, 0, 0);
        }
        thread_.join();
    }
    auto* expected = this;
    active_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    running_.store(false, std::memory_order_release);
    threadId_.store(0, std::memory_order_release);
    if (shutdownEvent_)
    {
        CloseHandle(shutdownEvent_);
        shutdownEvent_ = nullptr;
    }
}

void CALLBACK VrrForegroundEventHook::WinEventProc(HWINEVENTHOOK, DWORD event,
    HWND window, LONG, LONG, DWORD, DWORD eventTimeMs)
{
    if (event != EVENT_SYSTEM_FOREGROUND || !window) return;
    DWORD processId{};
    if (!GetWindowThreadProcessId(window, &processId) || !processId) return;
    // The callback only performs a bounded PID lookup and publishes one bit.
    auto* self = active_.load(std::memory_order_acquire);
    if (self) self->tracker_.ObserveForegroundProcess(processId, eventTimeMs);
}

void VrrForegroundEventHook::ThreadMain(std::promise<bool> ready) noexcept
{
    MSG message{};
    PeekMessageW(&message, nullptr, WM_USER, WM_USER, PM_NOREMOVE);
    threadId_.store(GetCurrentThreadId(), std::memory_order_release);
    auto* expected = static_cast<VrrForegroundEventHook*>(nullptr);
    if (!active_.compare_exchange_strong(expected, this, std::memory_order_acq_rel))
    {
        ready.set_value(false);
        threadId_.store(0, std::memory_order_release);
        return;
    }
    hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, &VrrForegroundEventHook::WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    running_.store(hook_ != nullptr, std::memory_order_release);
    try
    {
        ready.set_value(hook_ != nullptr);
    }
    catch (...)
    {
        if (hook_)
        {
            UnhookWinEvent(hook_);
            hook_ = nullptr;
        }
        expected = this;
        active_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        running_.store(false, std::memory_order_release);
        threadId_.store(0, std::memory_order_release);
        return;
    }
    if (!hook_)
    {
        expected = this;
        active_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
        threadId_.store(0, std::memory_order_release);
        return;
    }

    for (;;)
    {
        const auto wait = MsgWaitForMultipleObjects(1, &shutdownEvent_, FALSE,
            INFINITE, QS_ALLINPUT);
        if (wait == WAIT_OBJECT_0)
        {
            // Out-of-context WinEvent callbacks already queued before the
            // epoch end must run before the owner thread unhooks and exits.
            while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
            {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            break;
        }
        if (wait != WAIT_OBJECT_0 + 1) break;

        const auto received = GetMessageW(&message, nullptr, 0, 0);
        if (received <= 0) break;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    UnhookWinEvent(hook_);
    hook_ = nullptr;
    expected = this;
    active_.compare_exchange_strong(expected, nullptr, std::memory_order_acq_rel);
    running_.store(false, std::memory_order_release);
    threadId_.store(0, std::memory_order_release);
}
