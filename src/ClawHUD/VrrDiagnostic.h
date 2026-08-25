#pragma once

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

class App;

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
    void Status(const wchar_t* text) const;
    bool Capture(const std::filesystem::path& executable, DWORD pid, const std::filesystem::path& csv,
        const std::string& session, std::wofstream& log);
    App& app_;
    HWND notifyWindow_{};
    std::thread worker_;
    std::atomic_bool stop_{};
    std::atomic_bool running_{};
    std::atomic<HANDLE> captureProcess_{ nullptr };
    std::atomic<HANDLE> pocProcess_{ nullptr };
};

constexpr UINT kVrrDiagnosticStatus = WM_APP + 21;
