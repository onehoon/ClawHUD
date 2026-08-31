#include "DiagnosticSession.h"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
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

// Child mode: create a hidden top-level window, later show it, then exit. Run
// out-of-process so DiagnosticSession (which skips its own PID) can observe it.
int RunWindowHelper()
{
    const HWND window = CreateWindowExW(0, L"STATIC", L"ClawHUD diag helper",
        WS_OVERLAPPEDWINDOW, 40, 40, 160, 80, nullptr, nullptr,
        GetModuleHandleW(nullptr), nullptr);
    if (!window) return 2;
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    ShowWindow(window, SW_SHOWNOACTIVATE);
    std::this_thread::sleep_for(std::chrono::milliseconds(450));
    DestroyWindow(window);
    return 0;
}

std::int64_t SummaryField(const std::string& record, const std::string& key)
{
    const std::regex pattern("\"" + key + "\":(null|-?\\d+)");
    std::smatch match;
    if (!std::regex_search(record, match, pattern)) return -2;
    return match[1] == "null" ? -1 : std::stoll(match[1]);
}
}

int main(int argc, char** argv)
{
    if (argc > 1 && std::string(argv[1]) == "--window-helper")
        return RunWindowHelper();

    DiagnosticSession session;
    assert(session.Start());

    // In-process: exercises the top-level filter and child-window exclusion.
    const HWND window = CreateWindowExW(0, L"STATIC", L"ClawHUD diagnostic event test",
        WS_OVERLAPPEDWINDOW, 40, 40, 160, 80, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(window);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    const HWND child = CreateWindowExW(0, L"STATIC", L"child", WS_CHILD | WS_VISIBLE,
        0, 0, 40, 20, window, nullptr, GetModuleHandleW(nullptr), nullptr);
    assert(child);

    // Out-of-process: a hidden top-level window whose CREATE happens before the
    // PID is admitted; the later SHOW must hydrate firstWindowCreateMs.
    wchar_t self[MAX_PATH]{};
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    std::wstring commandLine = L"\"" + std::wstring(self) + L"\" --window-helper";
    STARTUPINFOW startup{ sizeof(startup) };
    PROCESS_INFORMATION child_process{};
    const bool spawned = CreateProcessW(self, commandLine.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child_process) != FALSE;
    assert(spawned);
    const DWORD helperPid = child_process.dwProcessId;

    WaitForSingleObject(child_process.hProcess, 4000);
    CloseHandle(child_process.hThread);
    CloseHandle(child_process.hProcess);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    DestroyWindow(window);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    const auto log = session.LogPath();
    session.Stop();

    std::ifstream input(log, std::ios::binary);
    const std::string content((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());

    assert(content.find("\"type\":\"window_create\"") != std::string::npos);
    assert(content.find("\"type\":\"window_show\"") != std::string::npos);
    assert(content.find("\"exe\":") != std::string::npos);
    assert(content.find("\"imagePath\":") != std::string::npos);
    assert(content.find("\"topLevel\":true") != std::string::npos);
    assert(content.find("\"windowWidth\":") != std::string::npos);
    assert(content.find("\"monitorWidth\":") != std::string::npos);
    // The in-process child control never reaches the top-level stream.
    assert(content.find(HwndToken(child)) == std::string::npos);

    // The helper PID's summary has both lifecycle milestones, create before show.
    std::string helperSummary;
    std::istringstream lines(content);
    for (std::string line; std::getline(lines, line);)
        if (line.find("\"type\":\"pid_summary\"") != std::string::npos &&
            line.find("\"pid\":" + std::to_string(helperPid) + ",") != std::string::npos)
            helperSummary = line;
    assert(!helperSummary.empty());
    const auto created = SummaryField(helperSummary, "firstWindowCreateMs");
    const auto shown = SummaryField(helperSummary, "firstWindowShowMs");
    assert(created >= 0 && shown >= 0);
    assert(created <= shown);

    auto summary = log;
    summary.replace_filename(log.stem().wstring() + L"-summary.txt");
    assert(std::filesystem::exists(summary));
    assert(std::filesystem::file_size(summary) > 0);
    std::filesystem::remove(log);
    std::filesystem::remove(summary);
}
