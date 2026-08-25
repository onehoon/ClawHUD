#include "ForegroundTracker.h"

#include <utility>

ForegroundTracker* ForegroundTracker::active_ = nullptr;

ForegroundTracker::~ForegroundTracker()
{
    Stop();
}

bool ForegroundTracker::Start(HWND dispatchWindow, UINT reconcileMessage,
    ChangedCallback callback)
{
    if (hook_ || !dispatchWindow || !reconcileMessage)
        return hook_ != nullptr;
    dispatchWindow_ = dispatchWindow;
    reconcileMessage_ = reconcileMessage;
    changed_ = std::move(callback);
    active_ = this;
    hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!hook_)
    {
        active_ = nullptr;
        changed_ = {};
        return false;
    }
    Reconcile();
    return true;
}

void ForegroundTracker::Stop() noexcept
{
    if (hook_)
        UnhookWinEvent(hook_);
    hook_ = nullptr;
    if (active_ == this)
        active_ = nullptr;
    if (trackedProcess_)
        CloseHandle(trackedProcess_);
    trackedProcess_ = nullptr;
    dispatchWindow_ = nullptr;
    reconcileMessage_ = 0;
    trackedProcessId_ = 0;
    foregroundMatches_ = false;
    changed_ = {};
}

void ForegroundTracker::SetTrackedProcessId(DWORD processId)
{
    if (trackedProcess_)
        CloseHandle(trackedProcess_);
    trackedProcess_ = nullptr;
    trackedProcessId_ = processId;
    if (processId)
        trackedProcess_ = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE, processId);
    Reconcile();
}

bool ForegroundTracker::PidsMatch(DWORD foregroundProcessId, DWORD trackedProcessId) noexcept
{
    return trackedProcessId != 0 && foregroundProcessId == trackedProcessId;
}

bool ForegroundTracker::TrackedProcessIsAlive() const noexcept
{
    return !trackedProcess_ || WaitForSingleObject(trackedProcess_, 0) == WAIT_TIMEOUT;
}

void ForegroundTracker::Reconcile()
{
    const HWND foreground = GetForegroundWindow();
    DWORD foregroundProcessId{};
    if (foreground)
        GetWindowThreadProcessId(foreground, &foregroundProcessId);
    const bool matches = PidsMatch(foregroundProcessId, trackedProcessId_) &&
        TrackedProcessIsAlive();
    if (matches == foregroundMatches_)
        return;
    foregroundMatches_ = matches;
    if (changed_)
        changed_(matches);
}

void CALLBACK ForegroundTracker::WinEventProc(HWINEVENTHOOK, DWORD, HWND,
    LONG, LONG, DWORD, DWORD)
{
    auto* tracker = active_;
    if (tracker && tracker->dispatchWindow_)
        PostMessageW(tracker->dispatchWindow_, tracker->reconcileMessage_, 0, 0);
}
