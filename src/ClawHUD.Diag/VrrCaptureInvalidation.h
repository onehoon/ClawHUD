#pragma once

#include <windows.h>

#include <atomic>
#include <future>
#include <thread>

class VrrForegroundChangeTracker
{
public:
    void Reset(DWORD targetProcessId) noexcept;
    void BeginMeasurementEpoch() noexcept;
    void EndMeasurementEpoch() noexcept;
    void ObserveForegroundProcess(DWORD processId) noexcept;
    bool ForeignForegroundObserved() const noexcept;

private:
    std::atomic<DWORD> targetProcessId_{};
    std::atomic_bool epochActive_{};
    std::atomic_bool foreignForegroundObserved_{};
};

class VrrForegroundEventHook
{
public:
    VrrForegroundEventHook() = default;
    ~VrrForegroundEventHook();

    VrrForegroundEventHook(const VrrForegroundEventHook&) = delete;
    VrrForegroundEventHook& operator=(const VrrForegroundEventHook&) = delete;

    bool Start(DWORD targetProcessId) noexcept;
    void BeginMeasurementEpoch() noexcept;
    bool ForeignForegroundObserved() const noexcept;
    bool Running() const noexcept;
    void Stop() noexcept;

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND window,
        LONG objectId, LONG childId, DWORD eventThread, DWORD eventTime);
    void ThreadMain(std::promise<bool> ready) noexcept;
    static std::atomic<VrrForegroundEventHook*> active_;

    VrrForegroundChangeTracker tracker_;
    std::thread thread_;
    std::atomic<DWORD> threadId_{};
    std::atomic_bool running_{};
    HANDLE shutdownEvent_{};
    HWINEVENTHOOK hook_{};
};
