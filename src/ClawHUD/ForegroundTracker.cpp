#include "ForegroundTracker.h"

#include <utility>

ForegroundTracker* ForegroundTracker::active_ = nullptr;

ForegroundTracker::~ForegroundTracker()
{
    Stop();
}

bool ForegroundTracker::Start(HWND dispatchWindow, UINT reconcileMessage,
    ForegroundChangedCallback foregroundChanged)
{
    if (hook_ || !dispatchWindow || !reconcileMessage)
        return hook_ != nullptr;
    dispatchWindow_ = dispatchWindow;
    reconcileMessage_ = reconcileMessage;
    foregroundChanged_ = std::move(foregroundChanged);
    active_ = this;
    hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!hook_)
    {
        active_ = nullptr;
        foregroundChanged_ = {};
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
    dispatchWindow_ = nullptr;
    reconcileMessage_ = 0;
    lastForegroundWindow_ = nullptr;
    lastForegroundProcessId_ = 0;
    foregroundChanged_ = {};
}

void ForegroundTracker::Reconcile()
{
    const HWND foreground = GetForegroundWindow();
    DWORD foregroundProcessId{};
    if (foreground)
        GetWindowThreadProcessId(foreground, &foregroundProcessId);
    if (foreground == lastForegroundWindow_ &&
        foregroundProcessId == lastForegroundProcessId_)
        return;
    lastForegroundWindow_ = foreground;
    lastForegroundProcessId_ = foregroundProcessId;
    if (foregroundChanged_)
        foregroundChanged_(foreground, foregroundProcessId);
}

void CALLBACK ForegroundTracker::WinEventProc(HWINEVENTHOOK, DWORD, HWND,
    LONG, LONG, DWORD, DWORD)
{
    auto* tracker = active_;
    if (tracker && tracker->dispatchWindow_)
        PostMessageW(tracker->dispatchWindow_, tracker->reconcileMessage_, 0, 0);
}
