#pragma once

#include <windows.h>

#include <cstdint>

namespace clawhud
{
enum class HudPresentationSubmissionStage { SetBuffer, Present };

struct HudPresentationDiagnosticRecovery
{
    bool noBufferRecovered{};
    std::uint64_t noBufferDurationMs{};
    std::uint64_t noBufferCount{};
    bool submissionRecovered{};
    HudPresentationSubmissionStage previousFailureStage{};
    HRESULT previousFailureHr{ S_OK };
    std::uint64_t submissionDurationMs{};
    std::uint64_t submissionFailureCount{};
    bool heartbeat{};
};

// Pure local diagnostic policy. Its results select Debug records only.
class HudPresentationDiagnosticState
{
public:
    void Reset() noexcept { *this = {}; }
    bool RecordNoBuffer(std::uint64_t now) noexcept
    {
        if (noBufferActive_) { ++consecutiveNoBufferCount_; return false; }
        noBufferActive_ = true; noBufferStartedTickMs_ = now; consecutiveNoBufferCount_ = 1;
        return true;
    }
    bool RecordSubmissionFailure(HudPresentationSubmissionStage stage, HRESULT hr,
        std::uint64_t now) noexcept
    {
        if (submissionFailureActive_ && submissionFailureStage_ == stage && submissionFailureHr_ == hr)
        { ++submissionFailureCount_; return false; }
        submissionFailureActive_ = true; submissionFailureStage_ = stage; submissionFailureHr_ = hr;
        submissionFailureStartedTickMs_ = now; submissionFailureCount_ = 1;
        return true;
    }
    HudPresentationDiagnosticRecovery RecordSuccessfulPresent(std::uint64_t now) noexcept
    {
        HudPresentationDiagnosticRecovery recovery;
        ++successfulPresentCount_; lastSuccessfulPresentTickMs_ = now;
        if (noBufferActive_)
        {
            recovery.noBufferRecovered = true; recovery.noBufferDurationMs = now - noBufferStartedTickMs_;
            recovery.noBufferCount = consecutiveNoBufferCount_;
            noBufferActive_ = false; noBufferStartedTickMs_ = 0; consecutiveNoBufferCount_ = 0;
        }
        if (submissionFailureActive_)
        {
            recovery.submissionRecovered = true; recovery.previousFailureStage = submissionFailureStage_;
            recovery.previousFailureHr = submissionFailureHr_;
            recovery.submissionDurationMs = now - submissionFailureStartedTickMs_;
            recovery.submissionFailureCount = submissionFailureCount_;
            submissionFailureActive_ = false; submissionFailureStartedTickMs_ = 0; submissionFailureCount_ = 0;
        }
        if (lastHeartbeatTickMs_ == 0 || now - lastHeartbeatTickMs_ >= 5000)
        { recovery.heartbeat = true; lastHeartbeatTickMs_ = now; }
        return recovery;
    }
    bool NoBufferActive() const noexcept { return noBufferActive_; }
    std::uint64_t ConsecutiveNoBufferCount() const noexcept { return consecutiveNoBufferCount_; }
    bool SubmissionFailureActive() const noexcept { return submissionFailureActive_; }
    std::uint64_t SubmissionFailureCount() const noexcept { return submissionFailureCount_; }
    std::uint64_t SuccessfulPresentCount() const noexcept { return successfulPresentCount_; }
    std::uint64_t LastSuccessfulPresentTickMs() const noexcept { return lastSuccessfulPresentTickMs_; }
private:
    bool noBufferActive_{}; std::uint64_t noBufferStartedTickMs_{}; std::uint64_t consecutiveNoBufferCount_{};
    bool submissionFailureActive_{}; HudPresentationSubmissionStage submissionFailureStage_{};
    HRESULT submissionFailureHr_{ S_OK }; std::uint64_t submissionFailureStartedTickMs_{};
    std::uint64_t submissionFailureCount_{}; std::uint64_t successfulPresentCount_{};
    std::uint64_t lastSuccessfulPresentTickMs_{}; std::uint64_t lastHeartbeatTickMs_{};
};
}
