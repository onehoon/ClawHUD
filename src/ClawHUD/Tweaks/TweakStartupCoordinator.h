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
    ~TweakStartupCoordinator();
    void Start(bool enabled);
    void Stop();
    static int RunRetrySequenceForTests(const std::function<IntelVrrRunResult()>& attempt,
        const std::function<void(std::chrono::seconds)>& sleep)
    {
        const std::chrono::seconds delays[] = { std::chrono::seconds{ 2 }, std::chrono::seconds{ 5 }, std::chrono::seconds{ 15 } };
        for (int index = 0; index < 4; ++index)
        {
            const auto result = attempt();
            if (result.status != IntelVrrRunStatus::Unavailable) return index + 1;
            if (index < 3) sleep(delays[index]);
        }
        return 4;
    }

private:
    void Run(bool enabled);
    std::atomic_bool stopping_{};
    std::thread worker_;
};
}
