#pragma once

#include <atomic>
#include <functional>
#include <thread>
#include <chrono>
#include "IntelVrr/IntelVrrRunResult.h"

namespace clawhud
{
class TweakStartupCoordinator
{
public:
    using AttemptFn = std::function<IntelVrrRunResult(int attemptNumber)>;
    using WaitFn = std::function<bool(std::chrono::seconds)>;
    ~TweakStartupCoordinator();
    void Start(bool enabled);
    void Stop();
    static int RunRetrySequence(const AttemptFn& attempt, const WaitFn& wait)
    {
        const std::chrono::seconds delays[] = { std::chrono::seconds{ 2 }, std::chrono::seconds{ 5 }, std::chrono::seconds{ 15 } };
        for (int index = 0; index < 4; ++index)
        {
            const auto result = attempt(index + 1);
            if (result.status != IntelVrrRunStatus::Unavailable) return index + 1;
            if (index < 3 && !wait(delays[index])) return index + 1;
        }
        return 4;
    }

private:
    void Run(bool enabled);
    std::atomic_bool stopping_{};
    std::thread worker_;
};
}
