#pragma once

#include <windows.h>

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

class App;

struct HudVisibilityState
{
    bool mockHudEnabled{};
    std::optional<bool> manualOverride;
    bool visible{};
};

enum class DiagnosticHudMode
{
    Off,
    Static,
    Dynamic,
};

enum class VrrDiagnosticState
{
    Idle,
    WaitingForTrigger,
    Running,
};

constexpr bool VrrDiagnosticStopReportsCancellation(VrrDiagnosticState state) noexcept
{
    return state == VrrDiagnosticState::WaitingForTrigger;
}

constexpr bool VrrDiagnosticCanWaitForF8(bool hotkeyRegistered) noexcept
{
    return hotkeyRegistered;
}

constexpr bool DiagnosticHudModeUsesPeriodicUpdates(DiagnosticHudMode mode) noexcept
{
    return mode == DiagnosticHudMode::Dynamic;
}

class VrrDiagnostic
{
public:
    VrrDiagnostic(App& app, HWND notifyWindow) : app_(app), notifyWindow_(notifyWindow) {}
    ~VrrDiagnostic() { Stop(); }
    bool Start();
    void Stop();
    bool Running() const { return running_.load(); }
    bool WaitingForTrigger() const
    {
        return state_.load() == VrrDiagnosticState::WaitingForTrigger;
    }
    bool TriggerFromForeground();

private:
    void Run();
    bool RunImpl(DWORD targetPid, HWND foregroundWindow, const std::wstring& targetPath);
    void Status(const wchar_t* text) const;
    bool Capture(const std::filesystem::path& executable, DWORD pid, const std::filesystem::path& csv,
        const std::string& session, std::wofstream& log);
    App& app_;
    HWND notifyWindow_{};
    std::thread worker_;
    std::atomic_bool stop_{};
    std::atomic_bool running_{};
    std::atomic<VrrDiagnosticState> state_{ VrrDiagnosticState::Idle };
    std::mutex triggerMutex_;
    std::condition_variable triggerCondition_;
    std::optional<DWORD> targetPid_;
    HWND targetWindow_{};
    std::wstring targetPath_;
    HudVisibilityState savedHudState_{};
    bool hudStateSaved_{};
};

constexpr UINT kVrrDiagnosticStatus = WM_APP + 21;
