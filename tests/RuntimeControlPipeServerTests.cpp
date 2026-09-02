#include "RuntimeControlPipeServer.h"
#include "RuntimeControlPipeSecurity.h"
#include "RuntimeControlDispatchBridge.h"
#include "ClawHudControlCodec.h"
#include "ClawHudControlProtocol.h"

#include <windows.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace ctl = clawhud::control;
using Bytes = std::vector<std::uint8_t>;

namespace
{
int g_failures = 0;

void Check(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++g_failures;
    }
}

std::wstring TestPipeName()
{
    static std::atomic<int> counter{0};
    return L"\\\\.\\pipe\\ClawHUD.Control.test." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(counter.fetch_add(1));
}

ctl::ControlRequest EmptyRequest(ctl::Operation op, std::uint32_t id)
{
    ctl::ControlRequest r;
    r.operation = op;
    r.requestId = id;
    return r;
}

ctl::ControlRequest BoolRequest(ctl::Operation op, std::uint32_t id, bool flag)
{
    ctl::ControlRequest r;
    r.operation = op;
    r.requestId = id;
    r.flag = flag;
    return r;
}

Bytes Encode(const ctl::ControlRequest& request)
{
    auto frame = ctl::EncodeControlRequest(request);
    Check(frame.has_value(), "test helper: request encodes");
    return frame.value_or(Bytes{});
}

HANDLE ConnectClient(const std::wstring& name)
{
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        const HANDLE handle = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0,
            nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle != INVALID_HANDLE_VALUE)
        {
            DWORD mode = PIPE_READMODE_MESSAGE;
            SetNamedPipeHandleState(handle, &mode, nullptr, nullptr);
            return handle;
        }
        const DWORD error = GetLastError();
        if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PIPE_BUSY)
            return INVALID_HANDLE_VALUE;
        Sleep(10);
    }
    return INVALID_HANDLE_VALUE;
}

// One request -> one response, or nullopt if the server sent nothing.
std::optional<Bytes> Roundtrip(const std::wstring& name, const Bytes& request)
{
    const HANDLE handle = ConnectClient(name);
    if (handle == INVALID_HANDLE_VALUE)
        return std::nullopt;

    DWORD written{};
    WriteFile(handle, request.data(), static_cast<DWORD>(request.size()), &written, nullptr);

    std::array<std::uint8_t, ctl::kMaxFrameBytes> buffer{};
    DWORD read{};
    const BOOL ok = ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
        &read, nullptr);
    CloseHandle(handle);
    if (!ok || read == 0)
        return std::nullopt;
    return Bytes(buffer.begin(), buffer.begin() + read);
}

ctl::ControlResponse OkRuntimeInfoResponse(const ctl::ControlRequest& request)
{
    ctl::ControlResponse response;
    response.operationId = static_cast<std::uint16_t>(request.operation);
    response.requestId = request.requestId;
    response.status = ctl::ControlStatus::Ok;
    ctl::WireRuntimeInfo info;
    info.applicationVersion = "0.1.0";
    info.minimumProtocolVersion = 1;
    info.maximumProtocolVersion = 1;
    info.launchMode = static_cast<std::uint8_t>(ctl::WireLaunchMode::Standalone);
    info.runtimeState = static_cast<std::uint8_t>(ctl::WireRuntimeState::Ready);
    response.runtimeInfo = info;
    return response;
}

ctl::WireSettingsSnapshot SampleSnapshot()
{
    ctl::WireSettingsSnapshot s;
    s.hudSizeOffset = 1;
    s.hudFont = static_cast<std::uint8_t>(ctl::WireFont::Unispace);
    s.visibilityMode = static_cast<std::uint8_t>(ctl::WireVisibilityMode::Always);
    s.alignment = static_cast<std::uint8_t>(ctl::WireAlignment::Center);
    s.backgroundMode = static_cast<std::uint8_t>(ctl::WireBackgroundMode::ContentWidth);
    s.backgroundOpacityPercent = 70;
    return s;
}

// ---- 18.1 endpoint derivation --------------------------------------

void EndpointDerivation()
{
    const auto first = ctl::ControlPipeName();
    const auto second = ctl::ControlPipeName();
    Check(first.has_value() && second.has_value(), "production pipe name resolves");
    Check(first == second, "production pipe name is deterministic");

    const auto session = ctl::CurrentProcessSessionId();
    Check(session.has_value(), "session id resolves");
    if (first && session)
    {
        const std::wstring expected =
            L"\\\\.\\pipe\\ClawHUD.Control." + std::to_wstring(*session);
        Check(*first == expected, "pipe name is \\\\.\\pipe\\ClawHUD.Control.<sessionId>");
        Check(first->find(std::to_wstring(GetCurrentProcessId())) == std::wstring::npos ||
            std::to_wstring(GetCurrentProcessId()) == std::to_wstring(*session),
            "pipe name has no PID component");
        Check(first->find(L"test") == std::wstring::npos, "production name has no test suffix");
    }
}

// ---- 18.13 security descriptor -------------------------------------

void SecurityDescriptor()
{
    clawhud::control::ControlPipeSecurity security;
    Check(security.Build(), "security descriptor builds");
    Check(security.Attributes() != nullptr, "security attributes available after build");

    const std::wstring sddl = security.Sddl();
    const auto sid = ctl::CurrentUserSidString();
    Check(sid.has_value(), "current user SID resolves");
    Check(sddl.rfind(L"D:P(A;;GA;;;", 0) == 0, "DACL is protected, current-user full access");
    if (sid)
        Check(sddl.find(*sid) != std::wstring::npos, "DACL names the current user SID");
    Check(sddl.find(L"S-1-1-0") == std::wstring::npos, "DACL does not grant Everyone");
    Check(sddl.find(L"S-1-5-11") == std::wstring::npos, "DACL does not grant Authenticated Users");
    Check(sddl.find(L"S-1-5-32-545") == std::wstring::npos, "DACL does not grant BUILTIN\\Users");
    Check(sddl.find(L";BU)") == std::wstring::npos && sddl.find(L";AU)") == std::wstring::npos &&
            sddl.find(L";WD)") == std::wstring::npos,
        "DACL does not grant BU/AU/WD aliases");
}

// ---- 18.14 session gate helper -----------------------------------

void SessionGate()
{
    Check(ctl::SessionsMatch(1, 1), "same session allowed");
    Check(!ctl::SessionsMatch(1, 2), "different session rejected");
}

// ---- 18.2 / 18.3 read-only round trips --------------------------

void ReadOnlyRoundTrips()
{
    clawhud::RuntimeControlPipeServer server;
    std::atomic<int> dispatchCount{0};
    const std::wstring name = TestPipeName();
    const bool started = server.Start(
        [&](const ctl::ControlRequest& request) -> ctl::ControlResponse
        {
            dispatchCount.fetch_add(1);
            if (request.operation == ctl::Operation::GetRuntimeInfo)
                return OkRuntimeInfoResponse(request);
            ctl::ControlResponse response;
            response.operationId = static_cast<std::uint16_t>(request.operation);
            response.requestId = request.requestId;
            response.status = ctl::ControlStatus::Ok;
            response.snapshot = SampleSnapshot();
            return response;
        },
        name);
    Check(started, "pipe server starts on a test endpoint");
    Check(server.PipeName() == name, "server reports the test endpoint");

    {
        const auto raw = Roundtrip(name, Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 101)));
        Check(raw.has_value(), "GetRuntimeInfo produced a response");
        if (raw)
        {
            const auto decoded = ctl::DecodeControlResponse(*raw);
            Check(decoded.ok && decoded.value.status == ctl::ControlStatus::Ok,
                "GetRuntimeInfo response is Ok");
            Check(decoded.value.requestId == 101, "GetRuntimeInfo requestId preserved");
            Check(decoded.value.runtimeInfo.has_value(), "GetRuntimeInfo payload present");
        }
    }
    {
        const auto raw = Roundtrip(name,
            Encode(EmptyRequest(ctl::Operation::GetSettingsSnapshot, 202)));
        Check(raw.has_value(), "GetSettingsSnapshot produced a response");
        if (raw)
        {
            const auto decoded = ctl::DecodeControlResponse(*raw);
            Check(decoded.ok && decoded.value.status == ctl::ControlStatus::Ok,
                "GetSettingsSnapshot response is Ok");
            Check(decoded.value.snapshot.has_value(), "authoritative snapshot returned");
        }
    }
    Check(dispatchCount.load() == 2, "each read-only request reached the dispatch callback once");

    server.Stop();
}

// ---- 18.4 mutation gate ---------------------------------------

void MutationGate()
{
    clawhud::RuntimeControlPipeServer server;
    std::atomic<int> dispatchCount{0};
    const std::wstring name = TestPipeName();
    server.Start([&](const ctl::ControlRequest&) -> ctl::ControlResponse
        {
            dispatchCount.fetch_add(1);
            return {};
        }, name);

    const ctl::Operation mutations[] = {
        ctl::Operation::SetStartWithWindows, ctl::Operation::SetHudEnabled,
        ctl::Operation::SetHudVisibilityMode, ctl::Operation::SetHudSizeOffset,
        ctl::Operation::SetHudFont, ctl::Operation::SetHudAlignment,
        ctl::Operation::SetHudBackgroundMode, ctl::Operation::PreviewHudOpacity,
        ctl::Operation::CommitHudOpacity, ctl::Operation::SetIntelVrrRangeFixEnabled,
        ctl::Operation::RequestShutdown,
    };

    for (ctl::Operation op : mutations)
    {
        ctl::ControlRequest request;
        request.operation = op;
        request.requestId = 55;
        switch (op)
        {
        case ctl::Operation::SetStartWithWindows:
        case ctl::Operation::SetHudEnabled:
        case ctl::Operation::SetIntelVrrRangeFixEnabled:
            request.flag = true;
            break;
        case ctl::Operation::SetHudVisibilityMode:
        case ctl::Operation::SetHudFont:
        case ctl::Operation::SetHudAlignment:
        case ctl::Operation::SetHudBackgroundMode:
            request.wireEnum = 1;
            break;
        case ctl::Operation::SetHudSizeOffset:
            request.sizeOffset = 1;
            break;
        case ctl::Operation::PreviewHudOpacity:
        case ctl::Operation::CommitHudOpacity:
            request.opacityPercent = 70;
            break;
        default:
            break;
        }

        const auto raw = Roundtrip(name, Encode(request));
        Check(raw.has_value(), "mutation op received a response");
        if (raw)
        {
            const auto decoded = ctl::DecodeControlResponse(*raw);
            Check(decoded.ok && decoded.value.status == ctl::ControlStatus::RuntimeUnavailable,
                "blocked mutation returns RuntimeUnavailable");
            Check(decoded.value.requestId == 55, "blocked mutation preserves requestId");
        }
    }

    Check(dispatchCount.load() == 0, "no blocked mutation ever reached the dispatch callback");
    server.Stop();
}

// ---- 18.5 unknown operation correlation --------------------------

void UnknownOperationCorrelation()
{
    clawhud::RuntimeControlPipeServer server;
    const std::wstring name = TestPipeName();
    server.Start([](const ctl::ControlRequest&) -> ctl::ControlResponse { return {}; }, name);

    Bytes frame = Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 42));
    frame[10] = 250; // operation low byte -> unknown

    const auto raw = Roundtrip(name, frame);
    Check(raw.has_value(), "unknown operation produced a correlated response");
    if (raw)
    {
        const auto decoded = ctl::DecodeControlResponse(*raw);
        Check(decoded.ok && decoded.value.status == ctl::ControlStatus::UnknownOperation,
            "unknown operation returns UnknownOperation");
        Check(decoded.value.operationId == 250, "raw unknown operationId echoed");
        Check(decoded.value.requestId == 42, "requestId echoed");
    }
    server.Stop();
}

// ---- 18.6 invalid known value correlation -----------------------

void InvalidValueCorrelation()
{
    clawhud::RuntimeControlPipeServer server;
    const std::wstring name = TestPipeName();
    server.Start([](const ctl::ControlRequest&) -> ctl::ControlResponse { return {}; }, name);

    ctl::ControlRequest request;
    request.operation = ctl::Operation::SetHudFont;
    request.requestId = 77;
    request.wireEnum = 1;
    Bytes frame = Encode(request);
    frame[24] = 9; // payload byte: invalid font value

    const auto raw = Roundtrip(name, frame);
    Check(raw.has_value(), "invalid known value produced a correlated response");
    if (raw)
    {
        const auto decoded = ctl::DecodeControlResponse(*raw);
        Check(decoded.ok && decoded.value.status == ctl::ControlStatus::InvalidValue,
            "invalid value returns InvalidValue");
        Check(decoded.value.requestId == 77, "requestId preserved on InvalidValue");
    }
    server.Stop();
}

// ---- 18.7 uncorrelatable malformed frame ------------------------

void UncorrelatableMalformed()
{
    clawhud::RuntimeControlPipeServer server;
    std::atomic<int> dispatchCount{0};
    const std::wstring name = TestPipeName();
    server.Start([&](const ctl::ControlRequest&) -> ctl::ControlResponse
        {
            dispatchCount.fetch_add(1);
            return OkRuntimeInfoResponse(EmptyRequest(ctl::Operation::GetRuntimeInfo, 1));
        }, name);

    Bytes frame = Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 5));
    frame[0] = 'X'; // bad magic -> no trustworthy identity
    const auto raw = Roundtrip(name, frame);
    Check(!raw.has_value(), "bad-magic frame gets no fabricated response");

    // Server still serves the next valid client.
    const auto good = Roundtrip(name, Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 6)));
    Check(good.has_value(), "server stays alive after a malformed frame");
    if (good)
        Check(ctl::DecodeControlResponse(*good).value.status == ctl::ControlStatus::Ok,
            "next valid client still gets Ok");
    server.Stop();
}

// ---- 18.8 oversized pipe message ------------------------------

void OversizedMessage()
{
    clawhud::RuntimeControlPipeServer server;
    std::atomic<int> dispatchCount{0};
    const std::wstring name = TestPipeName();
    server.Start([&](const ctl::ControlRequest&) -> ctl::ControlResponse
        {
            dispatchCount.fetch_add(1);
            return OkRuntimeInfoResponse(EmptyRequest(ctl::Operation::GetRuntimeInfo, 1));
        }, name);

    const HANDLE handle = ConnectClient(name);
    Check(handle != INVALID_HANDLE_VALUE, "oversized-test client connects");
    if (handle != INVALID_HANDLE_VALUE)
    {
        Bytes huge(ctl::kMaxFrameBytes + 512, 0xAB);
        DWORD written{};
        WriteFile(handle, huge.data(), static_cast<DWORD>(huge.size()), &written, nullptr);
        std::array<std::uint8_t, 256> buffer{};
        DWORD read{};
        const BOOL ok = ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()),
            &read, nullptr);
        Check(!ok || read == 0, "oversized message gets no response");
        CloseHandle(handle);
    }

    Check(dispatchCount.load() == 0, "oversized message never dispatched");
    const auto good = Roundtrip(name, Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 9)));
    Check(good.has_value(), "server accepts a valid client after an oversized message");
    server.Stop();
}

// ---- 18.9 stop while waiting for a connection ------------------

void StopWhileWaitingForConnection()
{
    clawhud::RuntimeControlPipeServer server;
    server.Start([](const ctl::ControlRequest&) -> ctl::ControlResponse { return {}; },
        TestPipeName());
    Check(server.Running(), "server running with no client");
    server.Stop(); // must return; ctest timeout catches a hang
    Check(!server.Running(), "server stopped with no client");
}

// ---- 18.10 stop while client connected but idle ---------------

void StopWhileClientIdle()
{
    clawhud::RuntimeControlPipeServer server;
    const std::wstring name = TestPipeName();
    server.Start([](const ctl::ControlRequest&) -> ctl::ControlResponse { return {}; }, name);

    const HANDLE handle = ConnectClient(name);
    Check(handle != INVALID_HANDLE_VALUE, "idle client connects");
    Sleep(30); // let the worker enter its blocking read
    server.Stop();
    Check(!server.Running(), "server stopped while a client was connected and idle");
    if (handle != INVALID_HANDLE_VALUE)
        CloseHandle(handle);
}

// ---- 18.12 restart on the same endpoint -----------------------

void RestartOnSameEndpoint()
{
    const std::wstring name = TestPipeName();
    {
        clawhud::RuntimeControlPipeServer server;
        Check(server.Start([](const ctl::ControlRequest& r) { return OkRuntimeInfoResponse(r); },
                  name),
            "first server starts");
        server.Stop();
    }
    {
        clawhud::RuntimeControlPipeServer server;
        Check(server.Start([](const ctl::ControlRequest& r) { return OkRuntimeInfoResponse(r); },
                  name),
            "restarted server starts on the same endpoint");
        const auto raw = Roundtrip(name, Encode(EmptyRequest(ctl::Operation::GetRuntimeInfo, 1)));
        Check(raw.has_value() && ctl::DecodeControlResponse(*raw).value.status == ctl::ControlStatus::Ok,
            "restarted server serves a valid request");
        server.Stop();
    }
}

// ---- 18.11 dispatch wait + shutdown ordering ------------------

void DispatchWaitShutdownOrdering()
{
    clawhud::RuntimeControlDispatchBridge bridge;
    // Registered "main thread" is one that never drains: a dispatched request
    // stays pending until bridge.Stop().
    bridge.Start(std::thread::id{}, [] { return true; },
        [](const ctl::ControlRequest& request)
        {
            ctl::ControlResponse response;
            response.operationId = static_cast<std::uint16_t>(request.operation);
            response.requestId = request.requestId;
            response.status = ctl::ControlStatus::Ok;
            response.snapshot = SampleSnapshot();
            return response;
        });

    clawhud::RuntimeControlPipeServer server;
    const std::wstring name = TestPipeName();
    server.Start([&](const ctl::ControlRequest& request) { return bridge.Dispatch(request); }, name);

    std::atomic<bool> clientDone{false};
    std::thread client([&]
    {
        Roundtrip(name, Encode(EmptyRequest(ctl::Operation::GetSettingsSnapshot, 1)));
        clientDone = true;
    });

    Sleep(50); // let the pipe worker reach bridge.Dispatch()

    // Production shutdown order.
    bridge.Stop();
    server.Stop();
    client.join();

    Check(!server.Running(), "server joined without deadlock during shutdown ordering");
    Check(clientDone.load(), "client thread released");
}
}

int main()
{
    EndpointDerivation();
    SecurityDescriptor();
    SessionGate();
    ReadOnlyRoundTrips();
    MutationGate();
    UnknownOperationCorrelation();
    InvalidValueCorrelation();
    UncorrelatableMalformed();
    OversizedMessage();
    StopWhileWaitingForConnection();
    StopWhileClientIdle();
    RestartOnSameEndpoint();
    DispatchWaitShutdownOrdering();

    if (g_failures != 0)
    {
        std::cerr << g_failures << " check(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "RuntimeControlPipeServerTests: all checks passed\n";
    return EXIT_SUCCESS;
}
