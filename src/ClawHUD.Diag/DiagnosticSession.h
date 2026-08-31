#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <future>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Api2Evidence.h"

std::wstring DiagnosticQueryProcessImagePath(HANDLE process);

class DiagnosticSession
{
public:
    ~DiagnosticSession();
    bool Start();
    void Stop() noexcept;
    bool Running() const noexcept { return running_; }
    std::filesystem::path LogPath() const;

private:
    static void CALLBACK WinEventProc(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    bool StartWinEventThread();
    void StopWinEventThread() noexcept;
    void RecordWinEvent(DWORD event, HWND hwnd, LONG objectId, LONG childId) noexcept;
    void SampleApi2ObservedPids() noexcept;
    void WatchSteamRunningAppId() noexcept;
    void SampleTopGpu() noexcept;
    void WriteRecord(std::string type, std::string fields) noexcept;
    static std::string WindowFields(HWND hwnd, DWORD* processId = nullptr) noexcept;
    static std::string Json(std::wstring_view value) noexcept;
    static std::string Hex(HWND hwnd) noexcept;
    void ObserveProcess(DWORD processId, std::string_view reason) noexcept;
    void WriteSummary() noexcept;
    std::vector<DWORD> ObservedPids() noexcept;
    bool IsObserved(DWORD processId) noexcept;
    void MarkFirst(DWORD processId, std::string_view milestone) noexcept;

    std::filesystem::path path_;
    std::filesystem::path summaryPath_;
    std::ofstream log_;
    std::chrono::steady_clock::time_point startedAt_;
    std::mutex logMutex_;
    std::jthread api2Sampler_;
    std::jthread pdhSampler_;
    std::jthread steamWatcher_;
    std::jthread winEventThread_;
    std::atomic<DWORD> winEventThreadId_{};
    HWINEVENTHOOK foregroundHook_{};
    HWINEVENTHOOK windowHooks_[5]{};
    std::atomic_bool running_{};
    std::uint64_t sequence_{};
    std::atomic_uint32_t previousSteamAppId_{};
    std::mutex observedMutex_;
    struct PidTimeline
    {
        std::int64_t firstSeenMs{-1};
        std::int64_t firstWindowCreateMs{-1};
        std::int64_t firstWindowShowMs{-1};
        std::int64_t firstForegroundMs{-1};
        std::int64_t firstTopGpuMs{-1};
        std::int64_t firstApi2SwapchainMs{-1};
        std::int64_t firstSwapchainMs{-1};
        std::int64_t firstDisplayedFpsMs{-1};
        std::int64_t firstPresentedFpsMs{-1};
        std::int64_t lastForegroundMs{-1};
        std::int64_t lastRendererEvidenceMs{-1};
        std::int64_t lastWindowHideMs{-1};
        std::int64_t lastWindowDestroyMs{-1};
        std::uint32_t steamAppIdAtFirstSeen{};
        bool microsoftGameIdentity{};
        bool processExited{};
        std::string exe;
        std::string imagePath;
        std::uint64_t processStartFileTime{};
    };
    struct CachedWindow { DWORD processId{}; std::string fields; };
    std::unordered_map<DWORD, PidTimeline> timelines_;
    std::unordered_map<HWND, CachedWindow> windowCache_;
    Api2Evidence api2_;
    static std::atomic<DiagnosticSession*> active_;
    HWND previousForeground_{};
    DWORD previousForegroundPid_{};
};
