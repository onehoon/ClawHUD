#pragma once

#include <windows.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace clawhud
{
// Standalone process-exit watcher. The foreground-first architecture no longer
// arms it (foreground/window events drive re-evaluation, and known-game cache
// identity is process-generation aware), but the class is kept until R7 does
// the final tracked-PID / lifetime compatibility cleanup.
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
