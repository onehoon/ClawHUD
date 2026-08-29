#include "GameRenderVerifier.h"

#include <utility>

namespace clawhud
{
GameRenderVerifierEvent StampPresentMonHudEvent(
    DWORD processId, std::uint64_t generation,
    const PresentMonHudEvent& event) noexcept
{
    return {processId, generation, event};
}

GameRenderVerifier::~GameRenderVerifier()
{
    Stop();
}

bool GameRenderVerifier::Start(const std::wstring& presentMonExecutable,
    DWORD processId, std::uint64_t generation, EventCallback callback)
{
    Stop();
    if (presentMonExecutable.empty() || processId == 0 || generation == 0 || !callback)
        return false;

    processId_ = processId;
    generation_ = generation;
    callback_ = std::move(callback);

    auto telemetry = std::make_unique<PresentMonHudTelemetry>();
    if (!telemetry->Start(presentMonExecutable, processId,
            [this](const PresentMonHudEvent& event)
            {
                HandleTelemetryEvent(event);
            }))
    {
        callback_ = {};
        processId_ = 0;
        generation_ = 0;
        return false;
    }

    telemetry_ = std::move(telemetry);
    return true;
}

DWORD GameRenderVerifier::Stop() noexcept
{
    DWORD exitCode{};
    if (telemetry_)
    {
        exitCode = telemetry_->Stop();
        telemetry_.reset();
    }
    callback_ = {};
    processId_ = 0;
    generation_ = 0;
    return exitCode;
}

bool GameRenderVerifier::Running() const noexcept
{
    return telemetry_ && telemetry_->Running();
}

void GameRenderVerifier::HandleTelemetryEvent(
    const PresentMonHudEvent& event) noexcept
{
    try
    {
        if (callback_)
            callback_(StampPresentMonHudEvent(processId_, generation_, event));
    }
    catch (...)
    {
    }
}

bool GameRenderVerifier::ApplyRendererEvidence(
    GameDetectionCoordinator& coordinator,
    const GameRenderVerifierEvent& event) noexcept
{
    if (event.event.type != PresentMonHudEventType::FirstDisplayedFrame)
        return false;
    return coordinator.MarkRendererReady(event.processId, event.generation);
}
}
