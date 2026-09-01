#pragma once

#include "GameProcessInstance.h"
#include "GameScreenAdmission.h"
#include "KnownGameProcessCache.h"

#include <cstdint>
#include <functional>
#include <optional>

namespace clawhud
{
enum class ForegroundGameDecision
{
    Hidden,
    NeedsRendererVerification,
    Eligible,
};

struct CurrentForegroundGame
{
    ForegroundGameDecision decision{ForegroundGameDecision::Hidden};
    HWND window{};
    DWORD processId{};
    std::optional<GameProcessInstance> process;
    GameScreenAdmissionReason admissionReason{
        GameScreenAdmissionReason::NoWindow};
};

struct RendererVerificationRequest
{
    std::uint64_t requestId{};
    GameProcessInstance process{};

    friend bool operator==(const RendererVerificationRequest&,
        const RendererVerificationRequest&) = default;
};

struct RendererVerificationCompletion
{
    RendererVerificationRequest request;
    bool verified{};
};

struct ForegroundGameEvaluation
{
    CurrentForegroundGame current;
    std::optional<RendererVerificationRequest> verificationRequest;
};

struct SteamSessionContext
{
    std::uint32_t appId{};
    std::uint64_t generation{};

    bool Active() const noexcept { return appId != 0; }
};

class ForegroundGameDetector
{
public:
    using ProcessInstanceQuery =
        std::function<std::optional<GameProcessInstance>(DWORD)>;

    explicit ForegroundGameDetector(KnownGameProcessCache& knownGames) noexcept;
    ForegroundGameDetector(KnownGameProcessCache& knownGames,
        ProcessInstanceQuery processQuery);

    ForegroundGameEvaluation Evaluate(
        const GameScreenObservation& screen) noexcept;
    void CompleteRendererVerification(
        const RendererVerificationCompletion& completion) noexcept;
    void UpdateSteamSession(std::uint32_t appId) noexcept;

    const CurrentForegroundGame& Current() const noexcept { return current_; }
    const SteamSessionContext& SteamSession() const noexcept
        { return steamSession_; }

private:
    RendererVerificationRequest RequestFor(
        const GameProcessInstance& process) noexcept;

    KnownGameProcessCache& knownGames_;
    ProcessInstanceQuery processQuery_;
    std::uint64_t nextRequestId_{1};
    std::optional<RendererVerificationRequest> outstandingRequest_;
    CurrentForegroundGame current_;
    SteamSessionContext steamSession_;
};
}
