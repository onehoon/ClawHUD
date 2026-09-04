#include "StartupTaskRegistration.h"

#include <windows.h>
#include <sddl.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace
{
void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(EXIT_FAILURE);
    }
}

clawhud::DesiredStartupTask Desired()
{
    return clawhud::MakeDesiredStartupTask(
        L"C:\\Program Files\\ClawHUD\\ClawHUD.exe", L"S-1-5-21-1-2-3-1001");
}

// A snapshot that exactly matches `desired` on every managed property --
// mutated field-by-field below to walk the compliance matrix.
clawhud::StartupTaskSnapshot CompliantSnapshot(const clawhud::DesiredStartupTask& desired)
{
    clawhud::StartupTaskSnapshot snapshot;
    snapshot.present = true;
    snapshot.enabled = true;
    snapshot.execPath = desired.execPath;
    snapshot.arguments = L"";
    snapshot.workingDirectory = desired.workingDirectory;
    snapshot.principalUserId = desired.userId;
    snapshot.logonTriggerUserId = desired.userId;
    snapshot.interactiveTokenLogonType = true;
    snapshot.leastPrivilegeRunLevel = true;
    snapshot.disallowStartIfOnBatteries = false;
    snapshot.stopIfGoingOnBatteries = false;
    snapshot.executionTimeLimit = L"PT0S";
    return snapshot;
}

std::pair<std::wstring, std::wstring> CurrentUserIdentity()
{
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> buffer(size);
    if (size == 0 || !GetTokenInformation(token, TokenUser, buffer.data(), size, &size))
    {
        CloseHandle(token);
        return {};
    }
    CloseHandle(token);

    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR sidText{};
    if (!ConvertSidToStringSidW(user->User.Sid, &sidText))
        return {};
    std::wstring sid(sidText);
    LocalFree(sidText);

    DWORD nameSize = 0;
    DWORD domainSize = 0;
    SID_NAME_USE use{};
    LookupAccountSidW(nullptr, user->User.Sid, nullptr, &nameSize,
        nullptr, &domainSize, &use);
    std::wstring name(nameSize, L'\0');
    std::wstring domain(domainSize, L'\0');
    if (nameSize == 0 || !LookupAccountSidW(nullptr, user->User.Sid,
            name.data(), &nameSize, domain.data(), &domainSize, &use))
        return {};
    name.resize(nameSize);
    domain.resize(domainSize);
    return { sid, domain.empty() ? name : domain + L"\\" + name };
}
}

int main()
{
    using namespace clawhud;

    const auto desired = Desired();
    Check(desired.execPath == L"C:\\Program Files\\ClawHUD\\ClawHUD.exe",
        "MakeDesiredStartupTask derives the exec path");
    Check(desired.workingDirectory == L"C:\\Program Files\\ClawHUD",
        "MakeDesiredStartupTask derives the working directory from the exec path's parent");
    Check(desired.userId == L"S-1-5-21-1-2-3-1001", "MakeDesiredStartupTask keeps the user id");

    // --- compliance matrix ---------------------------------------------
    Check(IsStartupTaskCompliant(CompliantSnapshot(desired), desired), "exact task -> compliant");
    Check(EvaluateStartupTaskCompliance(CompliantSnapshot(desired), desired).IsCompliant(),
        "evaluator agrees with exact compliant task");

    {
        auto s = CompliantSnapshot(desired);
        s.present = false;
        Check(!IsStartupTaskCompliant(s, desired), "missing task -> not compliant");
        Check(HasStartupTaskMismatch(
            EvaluateStartupTaskCompliance(s, desired).mismatches,
            StartupTaskMismatch::TaskMissing), "missing task -> TaskMissing");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.enabled = false;
        Check(!IsStartupTaskCompliant(s, desired), "disabled task -> not compliant");
        Check(HasStartupTaskMismatch(
            EvaluateStartupTaskCompliance(s, desired).mismatches,
            StartupTaskMismatch::TaskDisabled), "disabled task -> TaskDisabled");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.execPath = L"C:\\Wrong\\ClawHUD.exe";
        Check(!IsStartupTaskCompliant(s, desired), "wrong executable path -> not compliant");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.arguments = L"--managed";
        Check(!IsStartupTaskCompliant(s, desired), "unexpected arguments -> not compliant");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.workingDirectory = L"C:\\Wrong";
        Check(!IsStartupTaskCompliant(s, desired), "wrong working directory -> not compliant");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.principalUserId = L"S-1-5-21-9-9-9-9";
        Check(!IsStartupTaskCompliant(s, desired), "wrong principal user -> not compliant");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.logonTriggerUserId = L"S-1-5-21-9-9-9-9";
        Check(!IsStartupTaskCompliant(s, desired), "wrong logon trigger user -> not compliant");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.interactiveTokenLogonType = false;
        Check(!IsStartupTaskCompliant(s, desired), "wrong logon type -> not compliant");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.leastPrivilegeRunLevel = false;
        Check(!IsStartupTaskCompliant(s, desired), "highest-privilege run level -> not compliant");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.disallowStartIfOnBatteries = true;
        Check(!IsStartupTaskCompliant(s, desired),
            "DisallowStartIfOnBatteries=true -> not compliant");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.stopIfGoingOnBatteries = true;
        Check(!IsStartupTaskCompliant(s, desired), "StopIfGoingOnBatteries=true -> not compliant");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.executionTimeLimit = L"PT1H";
        Check(!IsStartupTaskCompliant(s, desired),
            "finite/non-PT0S ExecutionTimeLimit -> not compliant");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.execPath = L"C:\\Wrong\\ClawHUD.exe";
        s.executionTimeLimit = L"PT1H";
        const auto result = EvaluateStartupTaskCompliance(s, desired);
        Check(HasStartupTaskMismatch(result.mismatches, StartupTaskMismatch::ExecPath) &&
                HasStartupTaskMismatch(result.mismatches, StartupTaskMismatch::ExecutionTimeLimit),
            "multiple modeled mismatches combine in evaluator");
    }
    {
        auto s = CompliantSnapshot(desired);
        s.execPath = L"c:\\program files\\clawhud\\clawhud.exe";
        s.workingDirectory = L"c:\\program files\\clawhud";
        s.principalUserId = L"s-1-5-21-1-2-3-1001";
        s.logonTriggerUserId = L"s-1-5-21-1-2-3-1001";
        Check(IsStartupTaskCompliant(s, desired),
            "path and user-id comparison is case-insensitive");
    }
    {
        const auto [sid, account] = CurrentUserIdentity();
        Check(!sid.empty() && !account.empty(), "current user identity resolves");
        const auto currentDesired = MakeDesiredStartupTask(
            L"C:\\Program Files\\ClawHUD\\ClawHUD.exe", sid);
        auto s = CompliantSnapshot(currentDesired);
        s.principalUserId = account;
        s.logonTriggerUserId = account;
        Check(IsStartupTaskCompliant(s, currentDesired),
            "SID and resolved account name represent the same user");
    }

    // --- helper command parsing / dispatch ------------------------------
    const std::wstring validSid = L"S-1-5-18";

    Check(ParseStartupTaskHelperArgs({}).command == StartupTaskHelperCommand::None,
        "no helper command -> normal launch continues");
    {
        const std::wstring_view args[] = { L"--managed" };
        Check(ParseStartupTaskHelperArgs(args).command == StartupTaskHelperCommand::None,
            "unrelated private command (--managed) is unaffected");
    }
    {
        const std::wstring_view args[] = { L"--ensure-startup-task", validSid };
        const auto parsed = ParseStartupTaskHelperArgs(args);
        Check(parsed.command == StartupTaskHelperCommand::Ensure && parsed.userSid == validSid,
            "ensure + valid user -> Ensure operation selected");
    }
    {
        const std::wstring_view args[] = { L"--remove-startup-task", validSid };
        const auto parsed = ParseStartupTaskHelperArgs(args);
        Check(parsed.command == StartupTaskHelperCommand::Remove && parsed.userSid == validSid,
            "remove + valid user -> Remove operation selected");
    }
    {
        const std::wstring_view args[] = { L"--ensure-startup-task" };
        Check(ParseStartupTaskHelperArgs(args).command == StartupTaskHelperCommand::Invalid,
            "ensure without a user -> Invalid, never a silent normal launch");
    }
    {
        const std::wstring_view args[] = { L"--remove-startup-task", L"" };
        Check(ParseStartupTaskHelperArgs(args).command == StartupTaskHelperCommand::Invalid,
            "remove with an empty user argument -> Invalid");
    }
    {
        const std::wstring_view args[] = { L"--ensure-startup-task", L"not-a-sid" };
        Check(ParseStartupTaskHelperArgs(args).command == StartupTaskHelperCommand::Invalid,
            "a syntactically invalid SID argument -> Invalid");
    }

    // TryRunStartupTaskHelperCommand: only the paths that never touch Task
    // Scheduler are covered here. Ensure/Remove COM registration is
    // production integration, verified by on-device smoke (see the
    // Task Scheduler startup work order), not by this unit test.
    Check(!TryRunStartupTaskHelperCommand({}).has_value(),
        "TryRunStartupTaskHelperCommand: no command -> nullopt, caller proceeds to App");
    {
        const std::wstring_view args[] = { L"--ensure-startup-task" };
        const auto exitCode = TryRunStartupTaskHelperCommand(args);
        Check(exitCode.has_value() && *exitCode != 0,
            "TryRunStartupTaskHelperCommand: invalid args fail the helper without App");
    }

    std::cout << "PASS\n";
}
