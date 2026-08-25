#pragma once

#include <windows.h>

#include <atomic>
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
    App& app_;
    HWND notifyWindow_{};
    std::thread worker_;
    std::atomic_bool stop_{};
    std::atomic_bool running_{};
};

constexpr UINT kVrrDiagnosticStatus = WM_APP + 21;
