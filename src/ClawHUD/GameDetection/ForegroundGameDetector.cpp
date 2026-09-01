#include "ForegroundGameDetector.h"

#include <utility>

namespace clawhud
{
ForegroundGameDetector::ForegroundGameDetector(
    KnownGameProcessCache& knownGames) noexcept
    : knownGames_(knownGames), processQuery_(QueryGameProcessInstance)
{
}

ForegroundGameDetector::ForegroundGameDetector(
    KnownGameProcessCache& knownGames, ProcessInstanceQuery processQuery)
    : knownGames_(knownGames), processQuery_(std::move(processQuery))
{
}

ForegroundGameEvaluation ForegroundGameDetector::Evaluate(
    const GameScreenObservation& screen) noexcept
{
    const auto admission = EvaluateGameScreenAdmission(screen);
    current_ = {ForegroundGameDecision::Hidden, screen.window, screen.processId,
        std::nullopt, admission.reason};
    if (!admission.admitted)
        return {current_, std::nullopt};

    std::optional<GameProcessInstance> process;
    try
    {
        if (processQuery_)
            process = processQuery_(screen.processId);
    }
    catch (...)
    {
        // A numeric PID without a generation is not cache-safe identity.
    }
    if (!process)
        return {current_, std::nullopt};

    current_.process = process;
    if (steamSession_.Active())
        knownGames_.MarkObservedDuringSteamSession(*process);

    const auto evidence = knownGames_.Lookup(*process);
    if (evidence && IsKnownGameEvidence(*evidence))
    {
        current_.decision = ForegroundGameDecision::Eligible;
        return {current_, std::nullopt};
    }

    current_.decision = ForegroundGameDecision::NeedsRendererVerification;
    return {current_, RequestFor(*process)};
}

void ForegroundGameDetector::CompleteRendererVerification(
    const RendererVerificationCompletion& completion) noexcept
{
    if (completion.verified)
        knownGames_.TryMarkRendererVerified(completion.request.process);
    if (outstandingRequest_ && *outstandingRequest_ == completion.request)
        outstandingRequest_.reset();
}

void ForegroundGameDetector::UpdateSteamSession(std::uint32_t appId) noexcept
{
    if (steamSession_.appId == appId)
        return;
    steamSession_.appId = appId;
    ++steamSession_.generation;
}

RendererVerificationRequest ForegroundGameDetector::RequestFor(
    const GameProcessInstance& process) noexcept
{
    if (outstandingRequest_ && outstandingRequest_->process == process)
        return *outstandingRequest_;
    if (nextRequestId_ == 0)
        nextRequestId_ = 1;
    outstandingRequest_ = RendererVerificationRequest{nextRequestId_++, process};
    return *outstandingRequest_;
}
}
