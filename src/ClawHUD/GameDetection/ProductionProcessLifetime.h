#pragma once

#include "GameDetectionCoordinator.h"

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace clawhud
{
enum class ProductionProcessExitAction
{
    Ignore,
    ReleaseCandidate,
    ReleaseCommitted,
};

ProductionProcessExitAction DecideProductionProcessExit(
    const GameDetectionContext& context,
    DWORD processId, std::uint64_t generation) noexcept;

class ProductionProcessLifetimeWatcher
{
public:
    using ExitCallback =
        std::function<void(DWORD processId, std::uint64_t generation)>;

    ProductionProcessLifetimeWatcher();
    ~ProductionProcessLifetimeWatcher();

    ProductionProcessLifetimeWatcher(const ProductionProcessLifetimeWatcher&) = delete;
    ProductionProcessLifetimeWatcher& operator=(
        const ProductionProcessLifetimeWatcher&) = delete;

    bool Arm(DWORD processId, std::uint64_t generation,
        ExitCallback callback) noexcept;
    void Disarm() noexcept;
    bool Armed() const noexcept { return waitHandle_ != nullptr; }

private:
    struct WaitContext;
    static VOID CALLBACK WaitCallback(PVOID context, BOOLEAN timedOut);

    HANDLE processHandle_{};
    HANDLE waitHandle_{};
    std::unique_ptr<WaitContext> context_;
};
}
