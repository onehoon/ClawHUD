#include "ProductionProcessLifetime.h"

namespace clawhud
{
namespace
{
struct ProductionProcessLifetimeWatcherContext
{
    DWORD processId{};
    std::uint64_t generation{};
    ProductionProcessLifetimeWatcher::ExitCallback callback;
};
}

struct ProductionProcessLifetimeWatcher::WaitContext
    : ProductionProcessLifetimeWatcherContext
{
};

ProductionProcessExitAction DecideProductionProcessExit(
    const GameDetectionContext& context,
    DWORD processId, std::uint64_t generation) noexcept
{
    if (context.candidateProcessId == 0 ||
        context.candidateProcessId != processId ||
        context.generation != generation)
        return ProductionProcessExitAction::Ignore;

    switch (context.state)
    {
    case GameDetectionState::Verifying:
    case GameDetectionState::Ready:
        return ProductionProcessExitAction::ReleaseCandidate;
    case GameDetectionState::Committed:
        return ProductionProcessExitAction::ReleaseCommitted;
    case GameDetectionState::Idle:
    case GameDetectionState::Armed:
        return ProductionProcessExitAction::Ignore;
    }
    return ProductionProcessExitAction::Ignore;
}

ProductionProcessLifetimeWatcher::~ProductionProcessLifetimeWatcher()
{
    Disarm();
}

ProductionProcessLifetimeWatcher::ProductionProcessLifetimeWatcher() = default;

bool ProductionProcessLifetimeWatcher::Arm(
    DWORD processId, std::uint64_t generation, ExitCallback callback) noexcept
{
    Disarm();
    if (processId == 0 || !callback)
        return false;

    HANDLE process = OpenProcess(
        SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
    if (!process)
        return false;

    std::unique_ptr<WaitContext> context;
    try
    {
        context = std::make_unique<WaitContext>();
        context->processId = processId;
        context->generation = generation;
        context->callback = std::move(callback);
    }
    catch (...)
    {
        CloseHandle(process);
        return false;
    }

    HANDLE waitHandle{};
    if (!RegisterWaitForSingleObject(&waitHandle, process, WaitCallback,
        context.get(), INFINITE, WT_EXECUTEONLYONCE))
    {
        CloseHandle(process);
        return false;
    }

    processHandle_ = process;
    waitHandle_ = waitHandle;
    context_ = std::move(context);
    return true;
}

void ProductionProcessLifetimeWatcher::Disarm() noexcept
{
    if (waitHandle_)
    {
        UnregisterWaitEx(waitHandle_, INVALID_HANDLE_VALUE);
        waitHandle_ = nullptr;
    }
    if (processHandle_)
    {
        CloseHandle(processHandle_);
        processHandle_ = nullptr;
    }
    context_.reset();
}

VOID CALLBACK ProductionProcessLifetimeWatcher::WaitCallback(
    PVOID context, BOOLEAN timedOut)
{
    if (timedOut || !context)
        return;
    auto* waitContext = static_cast<WaitContext*>(context);
    try
    {
        if (waitContext->callback)
            waitContext->callback(waitContext->processId, waitContext->generation);
    }
    catch (...)
    {
        // A thread-pool callback must not allow allocation/callback failures
        // to escape into the process.
    }
}
}
