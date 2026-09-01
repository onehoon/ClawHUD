#pragma once

#include <windows.h>

#include <functional>

// Foreground event source only. Reports actual EVENT_SYSTEM_FOREGROUND HWND/PID
// changes to its callback and de-duplicates repeated observations of the same
// foreground. It never selects, matches, retains, or validates a game target:
// current-game authority lives in GameSessionController / ForegroundGameDetector.
class ForegroundTracker
{
public:
    using ForegroundChangedCallback = std::function<void(HWND, DWORD)>;

    ~ForegroundTracker();
    bool Start(HWND dispatchWindow, UINT reconcileMessage,
        ForegroundChangedCallback foregroundChanged);
    void Stop() noexcept;
    // Re-reads the current foreground and invokes the callback when the observed
    // HWND/PID changed since the last observation. Also used for resume-recovery
    // reconciliation to catch a missed foreground transition.
    void Reconcile();

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND window,
        LONG object, LONG child, DWORD eventThread, DWORD eventTime);

    static ForegroundTracker* active_;
    HWINEVENTHOOK hook_{};
    HWND dispatchWindow_{};
    UINT reconcileMessage_{};
    HWND lastForegroundWindow_{};
    DWORD lastForegroundProcessId_{};
    ForegroundChangedCallback foregroundChanged_;
};
