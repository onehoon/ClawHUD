#include "StartupTaskRegistration.h"

#include "StartupExecutablePath.h"

#include <windows.h>
#include <taskschd.h>
#include <sddl.h>
#include <shellapi.h>

#include <wrl/client.h>

#include <cwctype>
#include <vector>

// CLSID_TaskScheduler / IID_ITaskService and friends are declared extern in
// <taskschd.h>; taskschd.lib provides their storage.
#pragma comment(lib, "taskschd.lib")

using Microsoft::WRL::ComPtr;

namespace clawhud
{
namespace
{
// Bounded self-elevated helper wait, matching the existing
// PresentMonRuntimeBootstrap installer-launch pattern (no detached elevated
// process, no unbounded wait).
constexpr DWORD kHelperWaitMs = 60 * 1000;
// Bounded best-effort reap after TerminateProcess on a timed-out helper.
constexpr DWORD kTerminateReapMs = 5 * 1000;
// Read-only settle window after a privileged mutation: Task Scheduler has an
// observed short read-after-write visibility lag on this device family.
constexpr DWORD kSettleWindowMs = 2000;
constexpr DWORD kSettleIntervalMs = 150;

// RAII COM apartment scope that tolerates a thread already initialized with a
// different concurrency model (RPC_E_CHANGED_MODE): COM is still usable in
// that case, but this scope must not call CoUninitialize since it never
// incremented the per-thread init count.
class ComScope
{
public:
    ComScope() noexcept : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComScope()
    {
        if (SUCCEEDED(hr_))
            CoUninitialize();
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
    bool Usable() const noexcept { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

private:
    HRESULT hr_;
};

struct Bstr
{
    BSTR value{};
    explicit Bstr(std::wstring_view text) : value(SysAllocString(std::wstring(text).c_str())) {}
    ~Bstr() { if (value) SysFreeString(value); }
    Bstr(const Bstr&) = delete;
    Bstr& operator=(const Bstr&) = delete;
};

bool PathsEqualCaseInsensitive(std::wstring_view a, std::wstring_view b) noexcept
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::towlower(a[i]) != std::towlower(b[i])) return false;
    return true;
}

// SID strings compare case-insensitively like every other Windows identifier
// ClawHUD manages here.
bool ResolveIdentityToSid(std::wstring_view text, std::vector<BYTE>& sidBuffer) noexcept
{
    try
    {
        const std::wstring value(text);
        PSID sid{};
        if (ConvertStringSidToSidW(value.c_str(), &sid))
        {
            const DWORD length = GetLengthSid(sid);
            const auto* bytes = reinterpret_cast<const BYTE*>(sid);
            sidBuffer.assign(bytes, bytes + length);
            LocalFree(sid);
            return true;
        }
        if (value.empty()) return false;

        DWORD sidSize = 0;
        DWORD domainSize = 0;
        SID_NAME_USE use{};
        LookupAccountNameW(nullptr, value.c_str(), nullptr, &sidSize,
            nullptr, &domainSize, &use);
        if (sidSize == 0) return false;
        sidBuffer.resize(sidSize);
        std::wstring domain(domainSize, L'\0');
        if (!LookupAccountNameW(nullptr, value.c_str(), sidBuffer.data(), &sidSize,
                domain.data(), &domainSize, &use))
        {
            sidBuffer.clear();
            return false;
        }
        return true;
    }
    catch (...) { return false; }
}

bool UserIdsEquivalent(std::wstring_view a, std::wstring_view b) noexcept
{
    if (PathsEqualCaseInsensitive(a, b)) return true;
    std::vector<BYTE> sidA;
    std::vector<BYTE> sidB;
    return ResolveIdentityToSid(a, sidA) && ResolveIdentityToSid(b, sidB) &&
        EqualSid(sidA.data(), sidB.data()) != FALSE;
}

std::wstring CurrentUserSidString() noexcept
{
    HANDLE token{};
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};

    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0)
    {
        CloseHandle(token);
        return {};
    }

    std::vector<BYTE> buffer(size);
    const bool read = GetTokenInformation(token, TokenUser, buffer.data(), size, &size) != FALSE;
    CloseHandle(token);
    if (!read)
        return {};

    const auto* user = reinterpret_cast<const TOKEN_USER*>(buffer.data());
    LPWSTR sidString{};
    if (!ConvertSidToStringSidW(user->User.Sid, &sidString))
        return {};
    std::wstring result(sidString);
    LocalFree(sidString);
    return result;
}

// Connects to the local Task Scheduler and opens its root folder. `service`
// is populated too since RegisterStartupTask also needs it (for NewTask).
HRESULT OpenTaskServiceAndRootFolder(
    ComPtr<ITaskService>& service, ComPtr<ITaskFolder>& root) noexcept
{
    HRESULT hr = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&service));
    if (FAILED(hr)) return hr;

    VARIANT empty{};
    VariantInit(&empty);
    hr = service->Connect(empty, empty, empty, empty);
    if (FAILED(hr)) return hr;

    Bstr rootPath(L"\\");
    return service->GetFolder(rootPath.value, &root);
}

bool ReadStartupTaskSnapshot(StartupTaskSnapshot& out) noexcept
{
    out = {};
    try
    {
        ComScope com;
        if (!com.Usable()) return false;

        ComPtr<ITaskService> service;
        ComPtr<ITaskFolder> root;
        HRESULT hr = OpenTaskServiceAndRootFolder(service, root);
        if (FAILED(hr)) return false;

        ComPtr<IRegisteredTask> task;
        Bstr taskName(kStartupTaskName);
        hr = root->GetTask(taskName.value, &task);
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
            hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
        {
            out.present = false;
            return true;
        }
        if (FAILED(hr)) return false;
        out.present = true;

        VARIANT_BOOL enabled{};
        if (FAILED(task->get_Enabled(&enabled))) return false;
        out.enabled = enabled == VARIANT_TRUE;

        ComPtr<ITaskDefinition> definition;
        if (FAILED(task->get_Definition(&definition))) return false;

        ComPtr<IPrincipal> principal;
        if (FAILED(definition->get_Principal(&principal))) return false;
        {
            BSTR userId{};
            if (FAILED(principal->get_UserId(&userId))) return false;
            out.principalUserId = userId ? userId : L"";
            if (userId) SysFreeString(userId);

            TASK_LOGON_TYPE logonType{};
            if (FAILED(principal->get_LogonType(&logonType))) return false;
            out.interactiveTokenLogonType = logonType == TASK_LOGON_INTERACTIVE_TOKEN;

            TASK_RUNLEVEL_TYPE runLevel{};
            if (FAILED(principal->get_RunLevel(&runLevel))) return false;
            out.leastPrivilegeRunLevel = runLevel == TASK_RUNLEVEL_LUA;
        }

        ComPtr<ITaskSettings> settings;
        if (FAILED(definition->get_Settings(&settings))) return false;
        {
            VARIANT_BOOL disallow{};
            VARIANT_BOOL stop{};
            if (FAILED(settings->get_DisallowStartIfOnBatteries(&disallow))) return false;
            if (FAILED(settings->get_StopIfGoingOnBatteries(&stop))) return false;
            out.disallowStartIfOnBatteries = disallow == VARIANT_TRUE;
            out.stopIfGoingOnBatteries = stop == VARIANT_TRUE;

            BSTR limit{};
            if (FAILED(settings->get_ExecutionTimeLimit(&limit))) return false;
            out.executionTimeLimit = limit ? limit : L"";
            if (limit) SysFreeString(limit);
        }

        ComPtr<ITriggerCollection> triggers;
        if (FAILED(definition->get_Triggers(&triggers))) return false;
        LONG triggerCount = 0;
        if (FAILED(triggers->get_Count(&triggerCount))) return false;
        for (LONG i = 1; i <= triggerCount; ++i)
        {
            ComPtr<ITrigger> trigger;
            if (FAILED(triggers->get_Item(i, &trigger))) continue;
            TASK_TRIGGER_TYPE2 type{};
            if (FAILED(trigger->get_Type(&type)) || type != TASK_TRIGGER_LOGON) continue;
            ComPtr<ILogonTrigger> logonTrigger;
            if (FAILED(trigger.As(&logonTrigger))) continue;
            BSTR userId{};
            if (SUCCEEDED(logonTrigger->get_UserId(&userId)))
            {
                out.logonTriggerUserId = userId ? userId : L"";
                if (userId) SysFreeString(userId);
            }
            break;
        }

        ComPtr<IActionCollection> actions;
        if (FAILED(definition->get_Actions(&actions))) return false;
        LONG actionCount = 0;
        if (FAILED(actions->get_Count(&actionCount)) || actionCount < 1) return false;
        ComPtr<IAction> action;
        if (FAILED(actions->get_Item(1, &action))) return false;
        ComPtr<IExecAction> execAction;
        if (FAILED(action.As(&execAction))) return false;
        {
            BSTR path{};
            if (SUCCEEDED(execAction->get_Path(&path)))
            {
                out.execPath = path ? path : L"";
                if (path) SysFreeString(path);
            }
            BSTR arguments{};
            if (SUCCEEDED(execAction->get_Arguments(&arguments)))
            {
                out.arguments = arguments ? arguments : L"";
                if (arguments) SysFreeString(arguments);
            }
            BSTR workingDirectory{};
            if (SUCCEEDED(execAction->get_WorkingDirectory(&workingDirectory)))
            {
                out.workingDirectory = workingDirectory ? workingDirectory : L"";
                if (workingDirectory) SysFreeString(workingDirectory);
            }
        }

        return true;
    }
    catch (...)
    {
        return false;
    }
}

// Elevated-child-only: creates or updates the fixed "ClawHUD" task to exactly
// `desired`. Never touches any other scheduled task.
bool RegisterStartupTask(const DesiredStartupTask& desired) noexcept
{
    try
    {
        ComScope com;
        if (!com.Usable()) return false;

        ComPtr<ITaskService> service;
        ComPtr<ITaskFolder> root;
        HRESULT hr = OpenTaskServiceAndRootFolder(service, root);
        if (FAILED(hr)) return false;

        ComPtr<ITaskDefinition> definition;
        if (FAILED(service->NewTask(0, &definition))) return false;

        ComPtr<IRegistrationInfo> registrationInfo;
        if (SUCCEEDED(definition->get_RegistrationInfo(&registrationInfo)))
        {
            Bstr description(L"Starts ClawHUD after Windows logon.");
            registrationInfo->put_Description(description.value);
        }

        ComPtr<IPrincipal> principal;
        if (FAILED(definition->get_Principal(&principal))) return false;
        Bstr userId(desired.userId);
        if (FAILED(principal->put_UserId(userId.value))) return false;
        if (FAILED(principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN))) return false;
        if (FAILED(principal->put_RunLevel(TASK_RUNLEVEL_LUA))) return false;

        ComPtr<ITaskSettings> settings;
        if (FAILED(definition->get_Settings(&settings))) return false;
        if (FAILED(settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE))) return false;
        if (FAILED(settings->put_StopIfGoingOnBatteries(VARIANT_FALSE))) return false;
        Bstr executionTimeLimit(L"PT0S");
        if (FAILED(settings->put_ExecutionTimeLimit(executionTimeLimit.value))) return false;
        if (FAILED(settings->put_Enabled(VARIANT_TRUE))) return false;

        ComPtr<ITriggerCollection> triggers;
        if (FAILED(definition->get_Triggers(&triggers))) return false;
        ComPtr<ITrigger> trigger;
        if (FAILED(triggers->Create(TASK_TRIGGER_LOGON, &trigger))) return false;
        ComPtr<ILogonTrigger> logonTrigger;
        if (FAILED(trigger.As(&logonTrigger))) return false;
        if (FAILED(logonTrigger->put_UserId(userId.value))) return false;

        ComPtr<IActionCollection> actions;
        if (FAILED(definition->get_Actions(&actions))) return false;
        ComPtr<IAction> action;
        if (FAILED(actions->Create(TASK_ACTION_EXEC, &action))) return false;
        ComPtr<IExecAction> execAction;
        if (FAILED(action.As(&execAction))) return false;
        Bstr path(desired.execPath);
        Bstr workingDirectory(desired.workingDirectory);
        if (FAILED(execAction->put_Path(path.value))) return false;
        if (FAILED(execAction->put_WorkingDirectory(workingDirectory.value))) return false;

        VARIANT userIdVariant{};
        VariantInit(&userIdVariant);
        userIdVariant.vt = VT_BSTR;
        userIdVariant.bstrVal = SysAllocString(desired.userId.c_str());
        VARIANT emptyPassword{};
        VariantInit(&emptyPassword);
        VARIANT emptySddl{};
        VariantInit(&emptySddl);

        Bstr taskName(kStartupTaskName);
        ComPtr<IRegisteredTask> registered;
        hr = root->RegisterTaskDefinition(taskName.value, definition.Get(),
            TASK_CREATE_OR_UPDATE, userIdVariant, emptyPassword,
            TASK_LOGON_INTERACTIVE_TOKEN, emptySddl, &registered);
        VariantClear(&userIdVariant);
        return SUCCEEDED(hr);
    }
    catch (...)
    {
        return false;
    }
}

// Elevated-child-only: deletes the fixed "ClawHUD" task. Absence is success.
// Never enumerates or touches any other scheduled task.
bool DeleteStartupTaskIfPresent() noexcept
{
    try
    {
        ComScope com;
        if (!com.Usable()) return false;

        ComPtr<ITaskService> service;
        ComPtr<ITaskFolder> root;
        HRESULT hr = OpenTaskServiceAndRootFolder(service, root);
        if (FAILED(hr)) return false;

        Bstr taskName(kStartupTaskName);
        hr = root->DeleteTask(taskName.value, 0);
        if (hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ||
            hr == HRESULT_FROM_WIN32(ERROR_PATH_NOT_FOUND))
            return true;
        return SUCCEEDED(hr);
    }
    catch (...)
    {
        return false;
    }
}

// Launches `exe` elevated (UAC) with `commandArg <userSid>` and waits up to
// kHelperWaitMs. false on elevation failure/cancellation or an unbounded
// wait; the caller must treat that as "no mutation happened", never retry,
// and never leave a detached process.
bool RunElevatedHelper(const std::filesystem::path& exe, std::wstring_view commandArg,
    const std::wstring& userSid, DWORD& exitCode) noexcept
{
    const std::wstring parameters =
        std::wstring(commandArg) + L" \"" + userSid + L"\"";

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS;
    info.lpVerb = L"runas";
    info.lpFile = exe.c_str();
    info.lpParameters = parameters.c_str();
    info.nShow = SW_HIDE;
    if (!ShellExecuteExW(&info) || !info.hProcess)
        return false;

    const DWORD wait = WaitForSingleObject(info.hProcess, kHelperWaitMs);
    if (wait != WAIT_OBJECT_0)
    {
        if (wait == WAIT_TIMEOUT)
        {
            // Never leave a detached elevated process past the bound: force
            // it down before releasing the handle, then reap (best-effort,
            // itself bounded) so it cannot keep running -- and possibly still
            // mutate the task -- after this function has already reported
            // failure.
            TerminateProcess(info.hProcess, ERROR_TIMEOUT);
            WaitForSingleObject(info.hProcess, kTerminateReapMs);
        }
        CloseHandle(info.hProcess);
        return false;
    }
    const bool read = GetExitCodeProcess(info.hProcess, &exitCode) != FALSE;
    CloseHandle(info.hProcess);
    return read;
}

struct ComplianceSettleResult
{
    bool compliant{};
    bool readSucceeded{};
    StartupTaskSnapshot snapshot;
};

ComplianceSettleResult SettleUntilCompliant(const DesiredStartupTask& desired) noexcept
{
    const ULONGLONG start = GetTickCount64();
    ComplianceSettleResult result;
    for (;;)
    {
        StartupTaskSnapshot snapshot;
        if (ReadStartupTaskSnapshot(snapshot))
        {
            result.readSucceeded = true;
            result.snapshot = snapshot;
            if (EvaluateStartupTaskCompliance(snapshot, desired).IsCompliant())
            {
                result.compliant = true;
                return result;
            }
        }
        if (GetTickCount64() - start >= kSettleWindowMs)
            return result;
        Sleep(kSettleIntervalMs);
    }
}

bool SettleUntilAbsent() noexcept
{
    const ULONGLONG start = GetTickCount64();
    for (;;)
    {
        StartupTaskSnapshot snapshot;
        if (ReadStartupTaskSnapshot(snapshot) && !snapshot.present)
            return true;
        if (GetTickCount64() - start >= kSettleWindowMs)
            return false;
        Sleep(kSettleIntervalMs);
    }
}
}

DesiredStartupTask MakeDesiredStartupTask(
    const std::filesystem::path& resolvedExecutable, std::wstring_view userId)
{
    DesiredStartupTask desired;
    desired.execPath = resolvedExecutable.wstring();
    desired.workingDirectory = resolvedExecutable.parent_path().wstring();
    desired.userId = std::wstring(userId);
    return desired;
}

StartupTaskComplianceResult EvaluateStartupTaskCompliance(
    const StartupTaskSnapshot& snapshot, const DesiredStartupTask& desired) noexcept
{
    StartupTaskComplianceResult result;
    if (!snapshot.present)
    {
        result.mismatches = StartupTaskMismatch::TaskMissing;
        return result;
    }
    const auto add = [&result](StartupTaskMismatch flag) noexcept
    { result.mismatches = result.mismatches | flag; };
    if (!snapshot.enabled) add(StartupTaskMismatch::TaskDisabled);
    if (!PathsEqualCaseInsensitive(snapshot.execPath, desired.execPath))
        add(StartupTaskMismatch::ExecPath);
    if (!snapshot.arguments.empty()) add(StartupTaskMismatch::Arguments);
    if (!PathsEqualCaseInsensitive(snapshot.workingDirectory, desired.workingDirectory))
        add(StartupTaskMismatch::WorkingDirectory);
    if (!UserIdsEquivalent(snapshot.principalUserId, desired.userId))
        add(StartupTaskMismatch::PrincipalUser);
    if (!UserIdsEquivalent(snapshot.logonTriggerUserId, desired.userId))
        add(StartupTaskMismatch::LogonTriggerUser);
    if (!snapshot.interactiveTokenLogonType)
        add(StartupTaskMismatch::InteractiveTokenLogonType);
    if (!snapshot.leastPrivilegeRunLevel)
        add(StartupTaskMismatch::LeastPrivilegeRunLevel);
    if (snapshot.disallowStartIfOnBatteries)
        add(StartupTaskMismatch::DisallowStartIfOnBatteries);
    if (snapshot.stopIfGoingOnBatteries)
        add(StartupTaskMismatch::StopIfGoingOnBatteries);
    if (snapshot.executionTimeLimit != L"PT0S")
        add(StartupTaskMismatch::ExecutionTimeLimit);
    return result;
}

bool IsStartupTaskCompliant(const StartupTaskSnapshot& snapshot,
    const DesiredStartupTask& desired) noexcept
{
    return EvaluateStartupTaskCompliance(snapshot, desired).IsCompliant();
}

namespace
{
std::wstring FormatComplianceFailure(const StartupTaskSnapshot& snapshot,
    const DesiredStartupTask& desired)
{
    const auto result = EvaluateStartupTaskCompliance(snapshot, desired);
    if (HasStartupTaskMismatch(result.mismatches, StartupTaskMismatch::TaskMissing))
        return L"mismatches=TaskMissing";

    std::wstring names;
    const auto addName = [&names](StartupTaskMismatch flag, std::wstring_view name)
    {
        if (!HasStartupTaskMismatch(flag, StartupTaskMismatch::None))
        {
            if (!names.empty()) names += L",";
            names += name;
        }
    };
    const auto add = [&result, &addName](StartupTaskMismatch flag, std::wstring_view name)
    {
        if (HasStartupTaskMismatch(result.mismatches, flag)) addName(flag, name);
    };
    add(StartupTaskMismatch::TaskDisabled, L"TaskDisabled");
    add(StartupTaskMismatch::ExecPath, L"ExecPath");
    add(StartupTaskMismatch::Arguments, L"Arguments");
    add(StartupTaskMismatch::WorkingDirectory, L"WorkingDirectory");
    add(StartupTaskMismatch::PrincipalUser, L"PrincipalUser");
    add(StartupTaskMismatch::LogonTriggerUser, L"LogonTriggerUser");
    add(StartupTaskMismatch::InteractiveTokenLogonType, L"InteractiveTokenLogonType");
    add(StartupTaskMismatch::LeastPrivilegeRunLevel, L"LeastPrivilegeRunLevel");
    add(StartupTaskMismatch::DisallowStartIfOnBatteries, L"DisallowStartIfOnBatteries");
    add(StartupTaskMismatch::StopIfGoingOnBatteries, L"StopIfGoingOnBatteries");
    add(StartupTaskMismatch::ExecutionTimeLimit, L"ExecutionTimeLimit");

    std::wstring message = L"mismatches=" + names;
    if (HasStartupTaskMismatch(result.mismatches, StartupTaskMismatch::ExecPath))
        message += L"; execPath expected=\"" + desired.execPath + L"\" actual=\"" + snapshot.execPath + L"\"";
    if (HasStartupTaskMismatch(result.mismatches, StartupTaskMismatch::WorkingDirectory))
        message += L"; workingDirectory expected=\"" + desired.workingDirectory + L"\" actual=\"" + snapshot.workingDirectory + L"\"";
    if (HasStartupTaskMismatch(result.mismatches, StartupTaskMismatch::PrincipalUser))
        message += L"; principalUser expected=\"" + desired.userId + L"\" actual=\"" + snapshot.principalUserId + L"\"";
    if (HasStartupTaskMismatch(result.mismatches, StartupTaskMismatch::LogonTriggerUser))
        message += L"; logonTriggerUser expected=\"" + desired.userId + L"\" actual=\"" + snapshot.logonTriggerUserId + L"\"";
    if (HasStartupTaskMismatch(result.mismatches, StartupTaskMismatch::ExecutionTimeLimit))
        message += L"; executionTimeLimit expected=\"PT0S\" actual=\"" + snapshot.executionTimeLimit + L"\"";
    if (HasStartupTaskMismatch(result.mismatches, StartupTaskMismatch::Arguments))
        message += L"; arguments actual=\"" + snapshot.arguments + L"\"";
    return message;
}
}

StartupTaskResult SynchronizeStartupTask(
    bool enabled, const std::filesystem::path& processExecutable)
{
    const auto userSid = CurrentUserSidString();
    if (userSid.empty())
        return { false, L"failed to resolve current user SID" };

    StartupTaskSnapshot snapshot;
    if (!ReadStartupTaskSnapshot(snapshot))
        return { false, L"failed to read startup task state" };

    const auto resolved = ResolveStartupExecutable(processExecutable);

    if (!enabled)
    {
        if (!snapshot.present)
            return { true, L"startup task already absent" };

        DWORD exitCode{};
        if (!RunElevatedHelper(resolved.path, kRemoveStartupTaskArg, userSid, exitCode) ||
            exitCode != 0)
            return { false, L"elevated task removal failed or was cancelled" };
        if (!SettleUntilAbsent())
            return { false, L"task removal could not be verified" };
        return { true, L"startup task removed" };
    }

    const auto desired = MakeDesiredStartupTask(resolved.path, userSid);
    if (IsStartupTaskCompliant(snapshot, desired))
        return { true, L"startup task already compliant" };

    DWORD exitCode{};
    if (!RunElevatedHelper(resolved.path, kEnsureStartupTaskArg, userSid, exitCode) ||
        exitCode != 0)
        return { false, L"elevated task registration failed or was cancelled" };
    const auto settle = SettleUntilCompliant(desired);
    if (!settle.compliant)
    {
        if (!settle.readSucceeded)
            return { false, L"task registration could not be verified; readback unavailable" };
        return { false, L"task registration could not be verified; " +
            FormatComplianceFailure(settle.snapshot, desired) };
    }
    return { true, L"startup task registered" };
}

StartupTaskHelperArgs ParseStartupTaskHelperArgs(
    std::span<const std::wstring_view> args) noexcept
{
    if (args.empty())
        return {};

    StartupTaskHelperCommand command{};
    if (args[0] == kEnsureStartupTaskArg)
        command = StartupTaskHelperCommand::Ensure;
    else if (args[0] == kRemoveStartupTaskArg)
        command = StartupTaskHelperCommand::Remove;
    else
        return {};

    if (args.size() < 2 || args[1].empty())
        return { StartupTaskHelperCommand::Invalid, {} };

    const std::wstring sidArgument(args[1]);
    PSID sid{};
    if (!ConvertStringSidToSidW(sidArgument.c_str(), &sid))
        return { StartupTaskHelperCommand::Invalid, {} };
    LocalFree(sid);

    return { command, sidArgument };
}

std::optional<int> TryRunStartupTaskHelperCommand(std::span<const std::wstring_view> args)
{
    const auto parsed = ParseStartupTaskHelperArgs(args);
    switch (parsed.command)
    {
    case StartupTaskHelperCommand::None:
        return std::nullopt;
    case StartupTaskHelperCommand::Invalid:
        return 1;
    case StartupTaskHelperCommand::Ensure:
    {
        wchar_t modulePath[MAX_PATH]{};
        const DWORD chars = GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath));
        if (chars == 0 || chars >= ARRAYSIZE(modulePath))
            return 1;
        const auto resolved = ResolveStartupExecutable(modulePath);
        const auto desired = MakeDesiredStartupTask(resolved.path, parsed.userSid);
        return RegisterStartupTask(desired) ? 0 : 1;
    }
    case StartupTaskHelperCommand::Remove:
        return DeleteStartupTaskIfPresent() ? 0 : 1;
    }
    return std::nullopt;
}
}
