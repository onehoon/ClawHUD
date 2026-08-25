#pragma once

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
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

private:
    void Run();
    void RunImpl();
    void Status(const wchar_t* text) const;
    bool Capture(const std::filesystem::path& executable, DWORD pid, const std::filesystem::path& csv,
        const std::string& session, std::wofstream& log);
    App& app_;
    HWND notifyWindow_{};
    std::thread worker_;
    std::atomic_bool stop_{};
    std::atomic_bool running_{};
    HudVisibilityState savedHudState_{};
    bool hudStateSaved_{};
};

constexpr UINT kVrrDiagnosticStatus = WM_APP + 21;
