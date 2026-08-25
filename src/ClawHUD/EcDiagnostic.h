#pragma once

#include <windows.h>

#include <atomic>
#include <thread>

class EcDiagnostic
{
public:
    explicit EcDiagnostic(HWND notifyWindow) : notifyWindow_(notifyWindow) {}
    ~EcDiagnostic() { Stop(); }
    bool Start();
    void Stop();
    bool Running() const { return running_.load(); }
    void OpenLogFolder() const;

private:
    void Run();
    void Status(const wchar_t* text) const;
    HWND notifyWindow_{};
    std::thread worker_;
    std::atomic_bool stop_{};
    std::atomic_bool running_{};
};

constexpr UINT kEcDiagnosticStatus = WM_APP + 20;
