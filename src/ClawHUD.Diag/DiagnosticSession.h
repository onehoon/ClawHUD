#pragma once

#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>
#include <unordered_map>

#include "Api2Evidence.h"

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
    void RecordWinEvent(DWORD event, HWND hwnd, LONG objectId, LONG childId) noexcept;
    void SampleLoop() noexcept;
    void WatchSteamRunningAppId() noexcept;
    void SampleTopGpu() noexcept;
    void WriteRecord(std::string type, std::string fields) noexcept;
    static std::string WindowFields(HWND hwnd, DWORD* processId = nullptr) noexcept;
    static std::string Json(std::wstring_view value) noexcept;
    static std::string Hex(HWND hwnd) noexcept;
    void ObserveProcess(DWORD processId, std::string_view reason) noexcept;
    void WriteSummary() noexcept;

    std::filesystem::path path_;
    std::ofstream log_;
    std::chrono::steady_clock::time_point startedAt_;
    std::mutex logMutex_;
    std::jthread sampler_;
    std::jthread steamWatcher_;
    HWINEVENTHOOK foregroundHook_{};
    HWINEVENTHOOK windowHooks_[5]{};
    std::atomic_bool running_{};
    std::uint64_t sequence_{};
    std::uint32_t previousSteamAppId_{};
    std::mutex observedMutex_;
    std::unordered_map<DWORD, std::int64_t> firstSeenMs_;
    Api2Evidence api2_;
    std::chrono::steady_clock::time_point nextApi2Sample_{};
    static DiagnosticSession* active_;
};
