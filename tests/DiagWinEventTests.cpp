#include "DiagnosticSession.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

int main()
{
    DiagnosticSession session;
    assert(session.Start());
    const HWND window = CreateWindowExW(0, L"STATIC", L"ClawHUD diagnostic event test",
        WS_OVERLAPPEDWINDOW, 40, 40, 160, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(window);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    DestroyWindow(window);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto log = session.LogPath();
    session.Stop();

    std::ifstream input(log, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    assert(content.find("\"type\":\"window_create\"") != std::string::npos);
    assert(content.find("\"type\":\"window_show\"") != std::string::npos);
    auto summary = log;
    summary.replace_filename(log.stem().wstring() + L"-summary.txt");
    assert(std::filesystem::exists(summary));
    assert(std::filesystem::file_size(summary) > 0);
    std::filesystem::remove(log);
    std::filesystem::remove(summary);
}
