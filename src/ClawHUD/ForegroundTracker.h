#pragma once

#include <windows.h>

#include <functional>

class ForegroundTracker
{
public:
    using ChangedCallback = std::function<void(bool)>;
    using ForegroundChangedCallback = std::function<void(HWND, DWORD)>;

    ~ForegroundTracker();
    bool Start(HWND dispatchWindow, UINT reconcileMessage, ChangedCallback callback,
        ForegroundChangedCallback foregroundChanged = {});
    void Stop() noexcept;
    void Reconcile();
    void SetTrackedProcessId(DWORD processId);
    DWORD TrackedProcessId() const noexcept { return trackedProcessId_; }
    bool ForegroundIsTrackedProcess() const noexcept { return foregroundMatches_; }
    static bool PidsMatch(DWORD foregroundProcessId, DWORD trackedProcessId) noexcept;

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND window,
        LONG object, LONG child, DWORD eventThread, DWORD eventTime);
    bool TrackedProcessIsAlive() const noexcept;

    static ForegroundTracker* active_;
    HWINEVENTHOOK hook_{};
    HWND dispatchWindow_{};
    UINT reconcileMessage_{};
    DWORD trackedProcessId_{};
    HANDLE trackedProcess_{};
    HWND lastForegroundWindow_{};
    DWORD lastForegroundProcessId_{};
    bool foregroundMatches_{};
    ChangedCallback changed_;
    ForegroundChangedCallback foregroundChanged_;
};
