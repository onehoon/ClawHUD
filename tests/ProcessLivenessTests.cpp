#include "ProcessLiveness.h"

#include <iostream>
#include <string>

using namespace clawhud;

namespace
{
bool Check(bool condition, const char* message)
{
    if (condition) return true;
    std::cerr << "FAILED: " << message << '\n';
    return false;
}

void ZeroIsNeverAlive(bool& ok)
{
    ok &= Check(!ProcessAlive(0), "PID 0 is never alive");
}

void CurrentProcessIsAlive(bool& ok)
{
    ok &= Check(ProcessAlive(GetCurrentProcessId()),
        "the running test process is alive");
}

void ExitedProcessIsNotAlive(bool& ok)
{
    wchar_t command[] = L"cmd.exe /c exit";
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION info{};
    const BOOL created = CreateProcessW(nullptr, command, nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &info);
    if (!Check(created == TRUE, "spawned a short-lived child process"))
        return;
    WaitForSingleObject(info.hProcess, INFINITE);
    const DWORD childPid = info.dwProcessId;
    CloseHandle(info.hThread);
    CloseHandle(info.hProcess);
    ok &= Check(!ProcessAlive(childPid), "a process that has exited is not alive");
}
}

int main()
{
    bool ok = true;
    ZeroIsNeverAlive(ok);
    CurrentProcessIsAlive(ok);
    ExitedProcessIsNotAlive(ok);
    return ok ? 0 : 1;
}
