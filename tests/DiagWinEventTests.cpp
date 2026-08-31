#include "DiagnosticSession.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

namespace
{
std::string HwndToken(HWND hwnd)
{
    std::ostringstream out;
    out << "0x" << std::hex << reinterpret_cast<std::uintptr_t>(hwnd);
    return out.str();
}
}

int main()
{
    DiagnosticSession session;
    assert(session.Start());

    const HWND window = CreateWindowExW(0, L"STATIC", L"ClawHUD diagnostic event test",
        WS_OVERLAPPEDWINDOW, 40, 40, 160, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(window);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    // A child control: its create/show events must be filtered out of the
    // top-level lifecycle stream.
    const HWND child = CreateWindowExW(0, L"STATIC", L"child", WS_CHILD | WS_VISIBLE,
        0, 0, 40, 20, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(child);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    DestroyWindow(window);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto log = session.LogPath();
    session.Stop();

    std::ifstream input(log, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    assert(content.find("\"type\":\"window_create\"") != std::string::npos);
    assert(content.find("\"type\":\"window_show\"") != std::string::npos);
    // Records carry the process identity and explicit geometry fields.
    assert(content.find("\"exe\":") != std::string::npos);
    assert(content.find("\"imagePath\":") != std::string::npos);
    assert(content.find("\"topLevel\":true") != std::string::npos);
    assert(content.find("\"windowWidth\":") != std::string::npos);
    // The child control never reaches the top-level stream.
    assert(content.find(HwndToken(child)) == std::string::npos);

    auto summary = log;
    summary.replace_filename(log.stem().wstring() + L"-summary.txt");
    assert(std::filesystem::exists(summary));
    assert(std::filesystem::file_size(summary) > 0);
    std::filesystem::remove(log);
    std::filesystem::remove(summary);
}
