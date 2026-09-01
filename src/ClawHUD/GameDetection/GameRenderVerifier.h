#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <thread>

namespace clawhud
{
class PresentMonTelemetryProvider;

enum class GameRenderVerifierEventType
{
    FirstDisplayedFrame,
};

struct GameRenderVerifierEvent
{
    DWORD processId{};
    std::uint64_t generation{};
    GameRenderVerifierEventType type{ GameRenderVerifierEventType::FirstDisplayedFrame };
};

GameRenderVerifierEvent MakeGameRenderVerifierEvent(
    DWORD processId, std::uint64_t generation,
    GameRenderVerifierEventType type) noexcept;

// Confirms that a game candidate has produced a displayed frame, using the
// shared production PresentMon API2 frame query rather than a PresentMon.exe
// child process. Runs one bounded lightweight polling thread per target.
class GameRenderVerifier
{
public:
    using EventCallback = std::function<void(const GameRenderVerifierEvent&)>;

    explicit GameRenderVerifier(PresentMonTelemetryProvider& provider);
    ~GameRenderVerifier();

    GameRenderVerifier(const GameRenderVerifier&) = delete;
    GameRenderVerifier& operator=(const GameRenderVerifier&) = delete;

    bool Start(DWORD processId, std::uint64_t generation, EventCallback callback);
    void Stop() noexcept;

    bool Running() const noexcept { return running_.load(); }
    DWORD ProcessId() const noexcept { return processId_; }
    std::uint64_t Generation() const noexcept { return generation_; }

private:
    void PollLoop(DWORD processId, std::uint64_t generation);

    PresentMonTelemetryProvider& provider_;
    std::thread worker_;
    std::atomic_bool stop_{};
    std::atomic_bool running_{};
    DWORD processId_{};
    std::uint64_t generation_{};
    EventCallback callback_;
};
}
