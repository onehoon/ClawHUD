#pragma once

// CH-RTF-6 — secure, local, current-user/session, read-only Control pipe.
//
// One dedicated worker owns every blocking pipe call. External clients reach
// only GetRuntimeInfo / GetSettingsSnapshot; every known mutation returns
// RuntimeUnavailable without touching the dispatch bridge. The server has no
// App/runtime authority - it only forwards read-only requests to the injected
// dispatch callback (App: RuntimeControlDispatchBridge::Dispatch).

#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "ClawHudControlProtocol.h"
#include "RuntimeControlPipeSecurity.h"

namespace clawhud
{
class RuntimeControlPipeServer
{
public:
    using DispatchCallback =
        std::function<control::ControlResponse(const control::ControlRequest&)>;

    RuntimeControlPipeServer() = default;
    ~RuntimeControlPipeServer();

    RuntimeControlPipeServer(const RuntimeControlPipeServer&) = delete;
    RuntimeControlPipeServer& operator=(const RuntimeControlPipeServer&) = delete;

    // `pipeNameOverride` is for tests only; production passes it empty and the
    // deterministic per-session name is used. Returns false (and logs) if the
    // endpoint, security descriptor, or first pipe instance cannot be created;
    // that is non-fatal to Standalone ClawHUD.
    bool Start(DispatchCallback dispatch, const std::wstring& pipeNameOverride = {});

    // Cancels any blocked pipe I/O and joins the worker. Idempotent.
    void Stop();

    bool Running() const noexcept { return running_.load(); }
    std::wstring PipeName() const { return pipeName_; }

private:
    HANDLE CreateInstance(bool firstInstance);
    void WorkerMain(HANDLE firstPipe);
    void ServeClient(HANDLE pipe);
    // Drives one overlapped op to completion or to a stop. `startResult` /
    // `startError` are the ReadFile/WriteFile/ConnectNamedPipe return. Returns a
    // Win32 error (ERROR_SUCCESS on completion; ERROR_OPERATION_ABORTED on stop).
    DWORD AwaitOverlapped(HANDLE pipe, OVERLAPPED& overlapped, BOOL startResult,
        DWORD startError, DWORD& bytes);

    DispatchCallback dispatch_;
    std::wstring pipeName_;
    control::ControlPipeSecurity security_;
    DWORD sessionId_{};

    std::thread worker_;
    std::atomic<bool> running_{false};
    HANDLE stopEvent_{};
    mutable std::mutex mutex_;
};
}
