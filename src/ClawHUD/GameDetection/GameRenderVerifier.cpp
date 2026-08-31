#include "GameRenderVerifier.h"

#include "PresentMonTelemetryProvider.h"

#include <chrono>
#include <utility>

namespace clawhud
{
namespace
{
constexpr auto kFramePollInterval = std::chrono::milliseconds(100);
}

GameRenderVerifierEvent MakeGameRenderVerifierEvent(
    DWORD processId, std::uint64_t generation,
    GameRenderVerifierEventType type) noexcept
{
    return {processId, generation, type};
}

GameRenderVerifier::GameRenderVerifier(PresentMonTelemetryProvider& provider)
    : provider_(provider)
{
}

GameRenderVerifier::~GameRenderVerifier()
{
    Stop();
}

bool GameRenderVerifier::Start(DWORD processId, std::uint64_t generation,
    EventCallback callback)
{
    Stop();
    if (processId == 0 || generation == 0 || !callback)
        return false;

    PresentMonProcessLease lease = provider_.AcquireProcess(processId);
    if (!lease)
        return false;

    processId_ = processId;
    generation_ = generation;
    callback_ = std::move(callback);
    stop_.store(false);
    running_.store(true);
    worker_ = std::thread([this, processId, generation,
        lease = std::move(lease)]() mutable
    {
        PollLoop(processId, generation);
        lease.Release();
    });
    return true;
}

void GameRenderVerifier::Stop() noexcept
{
    stop_.store(true);
    if (worker_.joinable())
        worker_.join();
    running_.store(false);
    callback_ = {};
    processId_ = 0;
    generation_ = 0;
}

void GameRenderVerifier::PollLoop(DWORD processId, std::uint64_t generation)
{
    while (!stop_.load())
    {
        const auto displayed = provider_.PollGameRenderDisplayedFrame(processId);
        if (displayed && *displayed)
        {
            try
            {
                if (callback_)
                    callback_(MakeGameRenderVerifierEvent(processId, generation,
                        GameRenderVerifierEventType::FirstDisplayedFrame));
            }
            catch (...)
            {
            }
            break;
        }
        std::this_thread::sleep_for(kFramePollInterval);
    }
    running_.store(false);
}

bool GameRenderVerifier::ApplyRendererEvidence(
    GameDetectionCoordinator& coordinator,
    const GameRenderVerifierEvent& event) noexcept
{
    if (event.type != GameRenderVerifierEventType::FirstDisplayedFrame)
        return false;
    return coordinator.MarkRendererReady(event.processId, event.generation);
}
}
