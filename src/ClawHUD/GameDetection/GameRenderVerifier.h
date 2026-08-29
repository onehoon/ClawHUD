#pragma once

#include "GameDetectionCoordinator.h"
#include "PresentMonHudTelemetry.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace clawhud
{
struct GameRenderVerifierEvent
{
    DWORD processId{};
    std::uint64_t generation{};
    PresentMonHudEvent event;
};

GameRenderVerifierEvent StampPresentMonHudEvent(
    DWORD processId, std::uint64_t generation,
    const PresentMonHudEvent& event) noexcept;

class GameRenderVerifier
{
public:
    using EventCallback = std::function<void(const GameRenderVerifierEvent&)>;

    ~GameRenderVerifier();

    bool Start(const std::wstring& presentMonExecutable, DWORD processId,
        std::uint64_t generation, EventCallback callback);
    DWORD Stop() noexcept;

    bool Running() const noexcept;
    DWORD ProcessId() const noexcept { return processId_; }
    std::uint64_t Generation() const noexcept { return generation_; }

    static bool ApplyRendererEvidence(
        GameDetectionCoordinator& coordinator,
        const GameRenderVerifierEvent& event) noexcept;

private:
    void HandleTelemetryEvent(const PresentMonHudEvent& event) noexcept;

    std::unique_ptr<PresentMonHudTelemetry> telemetry_;
    DWORD processId_{};
    std::uint64_t generation_{};
    EventCallback callback_;
};
}
