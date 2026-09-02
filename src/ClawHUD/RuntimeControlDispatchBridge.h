#pragma once

// CH-RTF-5 — main-thread dispatch bridge.
//
// Moves a validated clawhud::control::ControlRequest from a background producer
// (a future IPC worker; a test producer today) to the ClawHUD main thread,
// runs it there, and returns the authoritative response to the waiting
// producer. No transport, no framing, no HWND knowledge — the wake-up is an
// injected callback so App owns the PostMessage and tests own a fake.

#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include "ClawHudControlProtocol.h"

namespace clawhud
{
class RuntimeControlDispatchBridge
{
public:
    // Executes one validated request on the main thread. Called by App from the
    // main-thread wake handler.
    using Handler = std::function<control::ControlResponse(const control::ControlRequest&)>;
    // Wakes the main thread (App: PostMessage to the runtime window). Returns
    // false if the wake could not be delivered.
    using WakeCallback = std::function<bool()>;

    RuntimeControlDispatchBridge() = default;
    ~RuntimeControlDispatchBridge();

    RuntimeControlDispatchBridge(const RuntimeControlDispatchBridge&) = delete;
    RuntimeControlDispatchBridge& operator=(const RuntimeControlDispatchBridge&) = delete;

    // Begins accepting requests. `mainThreadId` is the thread that will call
    // DrainOnMainThread(); a Dispatch() from that thread runs synchronously.
    void Start(std::thread::id mainThreadId, WakeCallback wake, Handler handler);

    // Stops accepting and completes every pending waiter with ShuttingDown.
    // Idempotent.
    void Stop();

    bool Accepting() const;

    // Any thread. Blocks until the request is executed on the main thread or
    // fails deterministically (ShuttingDown / RuntimeUnavailable).
    control::ControlResponse Dispatch(const control::ControlRequest& request);

    // Main thread only. Runs every currently queued request.
    void DrainOnMainThread();

private:
    struct Pending
    {
        control::ControlRequest request;
        std::mutex mutex;
        std::condition_variable done;
        bool completed{};
        control::ControlResponse response;
    };

    control::ControlResponse TerminalResponse(
        const control::ControlRequest& request, control::ControlStatus status) const;
    static void Complete(const std::shared_ptr<Pending>& pending,
        control::ControlResponse response);

    mutable std::mutex mutex_;
    std::deque<std::shared_ptr<Pending>> queue_;
    bool accepting_{};
    std::thread::id mainThreadId_{};
    WakeCallback wake_;
    Handler handler_;
};
}
