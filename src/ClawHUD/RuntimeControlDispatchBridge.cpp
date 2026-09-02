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
        Complete(request, TerminalResponse(request->request, ctl::ControlStatus::ShuttingDown));
}

bool RuntimeControlDispatchBridge::Accepting() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return accepting_;
}

ctl::ControlResponse RuntimeControlDispatchBridge::TerminalResponse(
    const ctl::ControlRequest& request, ctl::ControlStatus status) const
{
    ctl::ControlResponse response;
    response.operationId = static_cast<std::uint16_t>(request.operation);
    response.requestId = request.requestId;
    response.status = status;
    return response;
}

void RuntimeControlDispatchBridge::Complete(const std::shared_ptr<Pending>& pending,
    ctl::ControlResponse response)
{
    std::lock_guard<std::mutex> lock(pending->mutex);
    if (pending->completed)
        return;
    pending->response = std::move(response);
    pending->completed = true;
    pending->done.notify_all();
}

ctl::ControlResponse RuntimeControlDispatchBridge::Dispatch(const ctl::ControlRequest& request)
{
    std::shared_ptr<Pending> pending;
    Handler handler;
    WakeCallback wake;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!accepting_)
            return TerminalResponse(request, ctl::ControlStatus::ShuttingDown);

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
                       : TerminalResponse(request, ctl::ControlStatus::RuntimeUnavailable);

    // Wake outside the lock: the wake callback (or the main thread it releases)
    // may re-enter the bridge.
    const bool woke = wake && wake();
    if (!woke)
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            std::erase(queue_, pending);
        }
        Complete(pending, TerminalResponse(request, ctl::ControlStatus::RuntimeUnavailable));
    }

    std::unique_lock<std::mutex> lock(pending->mutex);
    pending->done.wait(lock, [&] { return pending->completed; });
    return pending->response;
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
        ctl::ControlResponse response = handler
            ? handler(pending->request)
            : TerminalResponse(pending->request, ctl::ControlStatus::RuntimeUnavailable);
        Complete(pending, std::move(response));
    }
}
}
