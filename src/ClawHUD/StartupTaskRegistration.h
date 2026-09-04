#pragma once

// Task Scheduler startup registration.
//
// ClawHUD owns exactly one fixed Task Scheduler task, "ClawHUD", registered
// in the root folder with a current-user logon trigger. This replaces the
// legacy Startup-folder shortcut, which Windows Full Screen Experience /
// Xbox mode does not reliably launch on cold boot (see
// docs/work-orders/TASK_SCHEDULER_STARTUP_FSE_COMPATIBILITY_WORK_ORDER_2026-09-04.md).
//
// The normal (non-elevated) ClawHUD process only ever reads the task. When
// the desired state is not already reflected, it spawns one bounded
// self-elevated child (this same executable, invoked with one of the
// kEnsureStartupTaskArg / kRemoveStartupTaskArg private commands) to perform
// the single privileged mutation, then independently reads the task back to
// verify it. The elevated child recomputes its own fixed task target; it
// never accepts an arbitrary executable path from the command line.

#include <filesystem>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace clawhud
{
inline constexpr wchar_t kStartupTaskName[] = L"ClawHUD";

// Private main.cpp command-line tokens dispatched before App construction.
// Each takes the intended interactive user's SID as its next argument.
inline constexpr std::wstring_view kEnsureStartupTaskArg = L"--ensure-startup-task";
inline constexpr std::wstring_view kRemoveStartupTaskArg = L"--remove-startup-task";

// Pure snapshot of the properties SynchronizeStartupTask cares about, read
// from the registered "ClawHUD" task. `present == false` means the task does
// not exist; every other field is meaningless in that case.
struct StartupTaskSnapshot
{
    bool present{};
    bool enabled{};
    std::wstring execPath;
    std::wstring arguments;
    std::wstring workingDirectory;
    std::wstring principalUserId;
    std::wstring logonTriggerUserId;
    bool interactiveTokenLogonType{};
    bool leastPrivilegeRunLevel{};
    bool disallowStartIfOnBatteries{};
    bool stopIfGoingOnBatteries{};
    std::wstring executionTimeLimit;
};

// The task ClawHUD wants registered: one exec action at `execPath` with no
// arguments, `workingDirectory` as its working directory, and `userId` (a
// SID string) as both the principal and the logon-trigger user.
struct DesiredStartupTask
{
    std::wstring execPath;
    std::wstring workingDirectory;
    std::wstring userId;
};

enum class StartupTaskMismatch : std::uint32_t
{
    None = 0,
    TaskMissing = 1u << 0,
    TaskDisabled = 1u << 1,
    ExecPath = 1u << 2,
    Arguments = 1u << 3,
    WorkingDirectory = 1u << 4,
    PrincipalUser = 1u << 5,
    LogonTriggerUser = 1u << 6,
    InteractiveTokenLogonType = 1u << 7,
    LeastPrivilegeRunLevel = 1u << 8,
    DisallowStartIfOnBatteries = 1u << 9,
    StopIfGoingOnBatteries = 1u << 10,
    ExecutionTimeLimit = 1u << 11,
};

constexpr StartupTaskMismatch operator|(StartupTaskMismatch left,
    StartupTaskMismatch right) noexcept
{
    return static_cast<StartupTaskMismatch>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

constexpr bool HasStartupTaskMismatch(StartupTaskMismatch value,
    StartupTaskMismatch flag) noexcept
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

struct StartupTaskComplianceResult
{
    StartupTaskMismatch mismatches{StartupTaskMismatch::None};

    constexpr bool IsCompliant() const noexcept
    {
        return mismatches == StartupTaskMismatch::None;
    }
};

StartupTaskComplianceResult EvaluateStartupTaskCompliance(
    const StartupTaskSnapshot& snapshot,
    const DesiredStartupTask& desired) noexcept;

DesiredStartupTask MakeDesiredStartupTask(
    const std::filesystem::path& resolvedExecutable, std::wstring_view userId);

// True when `snapshot` is a present, enabled task matching `desired` on
// every property ClawHUD manages (exec path / working directory compared
// case-insensitively; battery/execution-time-limit/logon-type/run-level held
// to the exact fixed contract). Any drift, including task absence, is false.
// Rewriting an already-compliant task must be avoided -- it costs an
// avoidable UAC prompt.
bool IsStartupTaskCompliant(const StartupTaskSnapshot& snapshot,
    const DesiredStartupTask& desired) noexcept;

struct StartupTaskResult
{
    bool success{};
    std::wstring message;
};

// Normal-process entry point (App::ApplyStartupRegistration and the VeloPack
// uninstall hook). `enabled == false` removes the owned task if present;
// `enabled == true` ensures a compliant registration. Reads the task first;
// if it already reflects the desired state, returns success without
// elevation. Otherwise spawns exactly one bounded elevated helper child and
// independently verifies the result by re-reading the task. Never leaves a
// detached elevated process and never retries beyond the one bounded
// settle window.
StartupTaskResult SynchronizeStartupTask(
    bool enabled, const std::filesystem::path& processExecutable);

// Parsed private helper command line, from ParseStartupTaskHelperArgs.
enum class StartupTaskHelperCommand
{
    // No recognized private command; the caller should proceed with a normal
    // launch (App construction).
    None,
    Ensure,
    Remove,
    // A recognized command token without a valid SID argument. The caller
    // must fail the helper launch and must not construct App.
    Invalid,
};

struct StartupTaskHelperArgs
{
    StartupTaskHelperCommand command{StartupTaskHelperCommand::None};
    std::wstring userSid;
};

// Pure argument parsing: recognizes kEnsureStartupTaskArg / kRemoveStartupTaskArg
// as the first argument and requires a syntactically valid SID string as the
// second. Any other input (including no arguments, or an unrelated argument
// such as "--managed") yields StartupTaskHelperCommand::None so normal
// argument handling is unaffected.
StartupTaskHelperArgs ParseStartupTaskHelperArgs(
    std::span<const std::wstring_view> args) noexcept;

// main.cpp entry point: dispatches a recognized private helper command and
// returns the process exit code (the caller must return it immediately,
// without constructing App). Returns std::nullopt for a normal launch. The
// helper recomputes the fixed task target from its own executable location;
// it does not initialize App, update checking, the hardware gate, the
// PresentMon runtime, the tray, the HUD, telemetry, the EC helper, or
// Control IPC.
std::optional<int> TryRunStartupTaskHelperCommand(
    std::span<const std::wstring_view> args);
}
