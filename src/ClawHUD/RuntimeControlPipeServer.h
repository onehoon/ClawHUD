#pragma once

// CH-RTF-6/7 — secure, local, current-user/session Control pipe.
//
// One dedicated worker owns every blocking pipe call. Every decoded protocol-v1
// operation (reads and mutations) is forwarded to the injected dispatch
// callback (App: RuntimeControlDispatchBridge::Dispatch) and therefore executes
// on the ClawHUD main thread. The server has no App/runtime authority.
//
// RequestShutdown: the worker delivers the Ok response, closes the connection,
// then invokes a narrow shutdown-ready callback that may only PostMessage the
// runtime window. It never calls App::Exit().

#include <windows.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>

#include "ClawHudControlProtocol.h"
#include "RuntimeControlExecutionResult.h"
#include "RuntimeControlPipeSecurity.h"

namespace clawhud
{
class RuntimeControlPipeServer
{
public:
    using DispatchCallback =
        std::function<RuntimeControlExecutionResult(const control::ControlRequest&)>;
    // Runs on the pipe worker after a successfully delivered RequestShutdown
    // response. May ONLY post the shutdown-ready message to the runtime window.
    using ShutdownReadyCallback = std::function<bool()>;

    RuntimeControlPipeServer() = default;
    ~RuntimeControlPipeServer();

    RuntimeControlPipeServer(const RuntimeControlPipeServer&) = delete;
    RuntimeControlPipeServer& operator=(const RuntimeControlPipeServer&) = delete;

    // `pipeNameOverride` is for tests only; production passes it empty and the
    // deterministic per-session name is used. Returns false (and logs) if the
    // endpoint, security descriptor, or first pipe instance cannot be created;
    // that is non-fatal to Standalone ClawHUD.
    bool Start(DispatchCallback dispatch, ShutdownReadyCallback shutdownReady = {},
        const std::wstring& pipeNameOverride = {});

    // Cancels any blocked pipe I/O and joins the worker. Idempotent.
    void Stop();

    bool Running() const noexcept { return running_.load(); }
    std::wstring PipeName() const { return pipeName_; }

private:
    HANDLE CreateInstance(bool firstInstance);
    void WorkerMain(HANDLE firstPipe);
    // Returns true only when a RequestShutdown response was fully delivered and
    // the post-response shutdown-ready callback should now fire.
    bool ServeClient(HANDLE pipe);
    // Drives one overlapped op to completion or to a stop. `startResult` /
    // `startError` are the ReadFile/WriteFile/ConnectNamedPipe return. Returns a
    // Win32 error (ERROR_SUCCESS on completion; ERROR_OPERATION_ABORTED on stop).
    DWORD AwaitOverlapped(HANDLE pipe, OVERLAPPED& overlapped, BOOL startResult,
        DWORD startError, DWORD& bytes);

    DispatchCallback dispatch_;
    ShutdownReadyCallback shutdownReady_;
    std::wstring pipeName_;
    control::ControlPipeSecurity security_;
    DWORD sessionId_{};

    std::thread worker_;
    std::atomic<bool> running_{false};
    HANDLE stopEvent_{};
    mutable std::mutex mutex_;
};
}
