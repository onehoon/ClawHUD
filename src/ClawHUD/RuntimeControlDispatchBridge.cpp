#include "RuntimeControlDispatchBridge.h"

#include <algorithm>
#include <utility>

namespace clawhud
{
namespace ctl = clawhud::control;

RuntimeControlDispatchBridge::~RuntimeControlDispatchBridge()
{
    Stop();
}

void RuntimeControlDispatchBridge::Start(std::thread::id mainThreadId, WakeCallback wake,
    Handler handler)
{
    std::lock_guard<std::mutex> lock(mutex_);
    mainThreadId_ = mainThreadId;
    wake_ = std::move(wake);
    handler_ = std::move(handler);
    accepting_ = true;
}

void RuntimeControlDispatchBridge::Stop()
{
    std::deque<std::shared_ptr<Pending>> pending;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        accepting_ = false;
        pending.swap(queue_);
    }
    for (const auto& request : pending)
        Complete(request, TerminalResult(request->request, ctl::ControlStatus::ShuttingDown));
}

bool RuntimeControlDispatchBridge::Accepting() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return accepting_;
}

RuntimeControlExecutionResult RuntimeControlDispatchBridge::TerminalResult(
    const ctl::ControlRequest& request, ctl::ControlStatus status) const
{
    RuntimeControlExecutionResult result;
    result.response.operationId = static_cast<std::uint16_t>(request.operation);
    result.response.requestId = request.requestId;
    result.response.status = status;
    result.shutdownAfterResponse = false;
    return result;
}

void RuntimeControlDispatchBridge::Complete(const std::shared_ptr<Pending>& pending,
    RuntimeControlExecutionResult result)
{
    std::lock_guard<std::mutex> lock(pending->mutex);
    if (pending->completed)
        return;
    pending->result = std::move(result);
    pending->completed = true;
    pending->done.notify_all();
}

RuntimeControlExecutionResult RuntimeControlDispatchBridge::Dispatch(
    const ctl::ControlRequest& request)
{
    std::shared_ptr<Pending> pending;
    Handler handler;
    WakeCallback wake;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_)
            return TerminalResult(request, ctl::ControlStatus::ShuttingDown);

        if (std::this_thread::get_id() == mainThreadId_)
        {
            // Self-dispatch: run synchronously through the same handler path so
            // there is one implementation and no wait-on-self deadlock.
            handler = handler_;
        }
        else
        {
            pending = std::make_shared<Pending>();
            pending->request = request;
            queue_.push_back(pending);
            wake = wake_;
        }
    }

    if (!pending)
        return handler ? handler(request)
                       : TerminalResult(request, ctl::ControlStatus::RuntimeUnavailable);

    // Wake outside the lock: the wake callback (or the main thread it releases)
    // may re-enter the bridge.
    const bool woke = wake && wake();
    if (!woke)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::erase(queue_, pending);
        }
        Complete(pending, TerminalResult(request, ctl::ControlStatus::RuntimeUnavailable));
    }

    std::unique_lock<std::mutex> lock(pending->mutex);
    pending->done.wait(lock, [&] { return pending->completed; });
    return pending->result;
}

void RuntimeControlDispatchBridge::DrainOnMainThread()
{
    std::deque<std::shared_ptr<Pending>> batch;
    Handler handler;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        batch.swap(queue_);
        handler = handler_;
    }
    for (const auto& pending : batch)
    {
        {
            std::lock_guard<std::mutex> lock(pending->mutex);
            if (pending->completed)
                continue;
        }
        RuntimeControlExecutionResult result = handler
            ? handler(pending->request)
            : TerminalResult(pending->request, ctl::ControlStatus::RuntimeUnavailable);
        Complete(pending, std::move(result));
    }
}
}
