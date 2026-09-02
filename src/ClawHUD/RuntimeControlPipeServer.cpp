#include "RuntimeControlPipeServer.h"

#include <array>
#include <cstdint>
#include <span>

#include "ClawHudControlCodec.h"
#include "RuntimeLogger.h"

namespace clawhud
{
namespace
{
namespace ctl = clawhud::control;

void Log(clawhud::RuntimeLogLevel level, const std::wstring& message)
{
    clawhud::RuntimeLogger::Log(level, message);
}

ctl::ControlResponse StatusResponse(std::uint16_t operationId, std::uint32_t requestId,
    ctl::ControlStatus status)
{
    ctl::ControlResponse response;
    response.operationId = operationId;
    response.requestId = requestId;
    response.status = status;
    return response;
}
}

RuntimeControlPipeServer::~RuntimeControlPipeServer()
{
    Stop();
}

bool RuntimeControlPipeServer::Start(DispatchCallback dispatch,
    ShutdownReadyCallback shutdownReady, const std::wstring& pipeNameOverride)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (running_.load())
        return true;

    const auto session = ctl::CurrentProcessSessionId();
    if (!session)
    {
        Log(clawhud::RuntimeLogLevel::Error,
            L"Control pipe server unavailable error=session-id-resolve-failed");
        return false;
    }
    sessionId_ = *session;

    if (!pipeNameOverride.empty())
    {
        pipeName_ = pipeNameOverride;
    }
    else
    {
        const auto name = ctl::ControlPipeName();
        if (!name)
        {
            Log(clawhud::RuntimeLogLevel::Error,
                L"Control pipe server unavailable error=endpoint-resolve-failed");
            return false;
        }
        pipeName_ = *name;
    }

    if (!security_.Build())
    {
        Log(clawhud::RuntimeLogLevel::Error,
            L"Control pipe server unavailable error=security-descriptor-build-failed");
        return false;
    }

    const HANDLE firstPipe = CreateInstance(true);
    if (firstPipe == INVALID_HANDLE_VALUE)
    {
        Log(clawhud::RuntimeLogLevel::Error,
            L"Control pipe server unavailable error=create-first-instance-failed code=" +
                std::to_wstring(GetLastError()));
        return false;
    }

    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!stopEvent_)
    {
        CloseHandle(firstPipe);
        Log(clawhud::RuntimeLogLevel::Error,
            L"Control pipe server unavailable error=stop-event-create-failed");
        return false;
    }

    dispatch_ = std::move(dispatch);
    shutdownReady_ = std::move(shutdownReady);
    running_.store(true);
    worker_ = std::thread([this, firstPipe] { WorkerMain(firstPipe); });

    Log(clawhud::RuntimeLogLevel::Info,
        L"Control pipe server started name=" + pipeName_ + L" session=" +
            std::to_wstring(sessionId_));
    return true;
}

void RuntimeControlPipeServer::Stop()
{
    std::thread worker;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!running_.load() && !worker_.joinable())
            return;
        running_.store(false);
        if (stopEvent_)
            SetEvent(stopEvent_);
        worker = std::move(worker_);
    }
    if (worker.joinable())
        worker.join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopEvent_)
        {
            CloseHandle(stopEvent_);
            stopEvent_ = nullptr;
        }
    }
    Log(clawhud::RuntimeLogLevel::Info, L"Control pipe server stopped");
}

HANDLE RuntimeControlPipeServer::CreateInstance(bool firstInstance)
{
    DWORD openMode = PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED;
    if (firstInstance)
        openMode |= FILE_FLAG_FIRST_PIPE_INSTANCE;

    return CreateNamedPipeW(pipeName_.c_str(), openMode,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
        1, ctl::kMaxFrameBytes, ctl::kMaxFrameBytes, 0, security_.Attributes());
}

// Drives one overlapped op to completion or to a stop. `startResult` /
// `startError` are the return of the ReadFile/WriteFile/ConnectNamedPipe call.
// Returns a Win32 error (ERROR_SUCCESS on completion; ERROR_OPERATION_ABORTED
// on stop).
DWORD RuntimeControlPipeServer::AwaitOverlapped(HANDLE pipe, OVERLAPPED& overlapped,
    BOOL startResult, DWORD startError, DWORD& bytes)
{
    bytes = 0;
    if (!startResult && startError != ERROR_IO_PENDING)
        return startError; // e.g. ERROR_MORE_DATA, or an immediate failure

    if (!startResult)
    {
        HANDLE waits[2] = {overlapped.hEvent, stopEvent_};
        if (WaitForMultipleObjects(2, waits, FALSE, INFINITE) != WAIT_OBJECT_0)
        {
            CancelIoEx(pipe, &overlapped);
            DWORD ignored{};
            GetOverlappedResult(pipe, &overlapped, &ignored, TRUE);
            return ERROR_OPERATION_ABORTED;
        }
    }

    if (!GetOverlappedResult(pipe, &overlapped, &bytes, FALSE))
        return GetLastError();
    return ERROR_SUCCESS;
}

void RuntimeControlPipeServer::WorkerMain(HANDLE firstPipe)
{
    HANDLE pipe = firstPipe;

    while (WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0)
    {
        if (pipe == INVALID_HANDLE_VALUE)
        {
            pipe = CreateInstance(true);
            if (pipe == INVALID_HANDLE_VALUE)
            {
                Log(clawhud::RuntimeLogLevel::Error,
                    L"Control pipe server unavailable error=recreate-instance-failed code=" +
                        std::to_wstring(GetLastError()));
                break;
            }
        }

        OVERLAPPED connect{};
        connect.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const BOOL connected = ConnectNamedPipe(pipe, &connect);
        const DWORD connectError = GetLastError();

        bool ready = connected || connectError == ERROR_PIPE_CONNECTED;
        if (!ready && connectError == ERROR_IO_PENDING)
        {
            DWORD bytes{};
            ready = AwaitOverlapped(pipe, connect, FALSE, ERROR_IO_PENDING, bytes) == ERROR_SUCCESS;
        }
        CloseHandle(connect.hEvent);

        if (WaitForSingleObject(stopEvent_, 0) == WAIT_OBJECT_0)
        {
            DisconnectNamedPipe(pipe);
            CloseHandle(pipe);
            pipe = INVALID_HANDLE_VALUE;
            break;
        }

        bool armShutdown = false;
        if (ready)
            armShutdown = ServeClient(pipe);

        // Close the acknowledged connection before posting shutdown so App
        // teardown cannot destroy the server while unread response bytes remain.
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
        pipe = INVALID_HANDLE_VALUE;

        if (armShutdown && WaitForSingleObject(stopEvent_, 0) != WAIT_OBJECT_0)
        {
            if (shutdownReady_ && !shutdownReady_())
                Log(clawhud::RuntimeLogLevel::Warn,
                    L"Control pipe shutdown-ready post failed; runtime stays alive");
        }
    }

    if (pipe != INVALID_HANDLE_VALUE)
    {
        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}

bool RuntimeControlPipeServer::ServeClient(HANDLE pipe)
{
    DWORD clientPid{};
    if (!GetNamedPipeClientProcessId(pipe, &clientPid))
        return false;
    DWORD clientSession{};
    if (!ProcessIdToSessionId(clientPid, &clientSession))
        return false;
    if (!ctl::SessionsMatch(sessionId_, clientSession))
    {
        Log(clawhud::RuntimeLogLevel::Warn, L"Control pipe client rejected session mismatch");
        return false;
    }

    // Bounded read: never allocate from a wire length.
    std::array<std::uint8_t, ctl::kMaxFrameBytes> buffer{};
    OVERLAPPED readOv{};
    readOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const BOOL readStarted = ReadFile(pipe, buffer.data(),
        static_cast<DWORD>(buffer.size()), nullptr, &readOv);
    const DWORD readStartError = GetLastError();
    DWORD bytesRead{};
    const DWORD readError = AwaitOverlapped(pipe, readOv, readStarted, readStartError, bytesRead);
    CloseHandle(readOv.hEvent);

    if (readError == ERROR_MORE_DATA)
    {
        Log(clawhud::RuntimeLogLevel::Warn, L"Control pipe oversized message rejected");
        return false;
    }
    if (readError != ERROR_SUCCESS || bytesRead == 0)
        return false; // client gone / aborted / empty

    const std::span<const std::uint8_t> frame(buffer.data(), bytesRead);
    const auto decoded = ctl::DecodeControlRequest(frame);

    RuntimeControlExecutionResult execution;
    if (decoded.ok)
    {
        // Every decoded protocol-v1 operation (reads and mutations) goes to the
        // main thread through the dispatch bridge. The codec + mapping own the
        // operation policy; the pipe server does not.
        execution = dispatch_(decoded.value);
    }
    else if (decoded.identity)
    {
        execution.response = StatusResponse(decoded.identity->operationId,
            decoded.identity->requestId, decoded.error);
    }
    else
    {
        return false; // uncorrelatable malformed frame: no fabricated response
    }

    const auto encoded = ctl::EncodeControlResponse(execution.response);
    if (!encoded)
    {
        Log(clawhud::RuntimeLogLevel::Warn, L"Control pipe response encode failed");
        return false; // a failed response never arms shutdown
    }

    OVERLAPPED writeOv{};
    writeOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const BOOL writeStarted = WriteFile(pipe, encoded->data(),
        static_cast<DWORD>(encoded->size()), nullptr, &writeOv);
    DWORD bytesWritten{};
    const DWORD writeError = AwaitOverlapped(pipe, writeOv, writeStarted, GetLastError(),
        bytesWritten);
    CloseHandle(writeOv.hEvent);
    if (writeError != ERROR_SUCCESS)
        return false; // response not delivered -> shutdown not armed

    // Wait for the client to consume the response and close its end before
    // disconnecting (DisconnectNamedPipe discards unread data). Cancellable by
    // the stop event.
    std::array<std::uint8_t, 64> drain{};
    OVERLAPPED drainOv{};
    drainOv.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    const BOOL drainStarted = ReadFile(pipe, drain.data(),
        static_cast<DWORD>(drain.size()), nullptr, &drainOv);
    DWORD drained{};
    AwaitOverlapped(pipe, drainOv, drainStarted, GetLastError(), drained);
    CloseHandle(drainOv.hEvent);

    return execution.shutdownAfterResponse;
}
}
