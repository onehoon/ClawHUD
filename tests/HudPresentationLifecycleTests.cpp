#include "HudPresentation.h"
#include "HudPresentationDiagnostics.h"
#include "HudPresentationLifecycle.h"

#include <iostream>

int main()
{
    bool ok = true;
    const auto expect = [&](bool condition, const char* name)
    {
        if (!condition) std::cerr << "FAILED: " << name << '\n';
        ok &= condition;
    };
    const auto hidden = clawhud::BuildHudPresentationRefreshPlan(false, true);
    expect(!hidden.recreate && !hidden.restoreVisibility, "no pending change");
    const auto visible = clawhud::BuildHudPresentationRefreshPlan(true, true);
    expect(visible.recreate && visible.restoreVisibility, "visible recreation restores visibility");
    const auto wasHidden = clawhud::BuildHudPresentationRefreshPlan(true, false);
    expect(wasHidden.recreate && !wasHidden.restoreVisibility, "hidden recreation stays hidden");

    clawhud::HudRenderOptions initialized{};
    initialized.barPixelHeight = 30.0f;
    initialized.layout.backgroundOpacity = 0.5f;
    clawhud::HudRenderOptions requested = initialized;
    requested.layout.backgroundOpacity = 0.0f;
    const auto transparent = clawhud::BuildEffectiveHudRenderOptions(requested, initialized, 144.0f);
    expect(transparent.layout.backgroundOpacity == 0.0f,
        "runtime opacity remains requested at render boundary");
    expect(transparent.barPixelHeight == 30.0f && transparent.dpi == 144.0f,
        "initialized geometry and runtime dpi are preserved");
    requested.layout.backgroundOpacity = 1.0f;
    const auto opaque = clawhud::BuildEffectiveHudRenderOptions(requested, initialized, 144.0f);
    expect(opaque.layout.backgroundOpacity == 1.0f,
        "runtime opacity propagates to opaque render boundary");

    clawhud::HudPresentationDiagnosticState diagnostics;
    expect(diagnostics.RecordNoBuffer(100), "first no-buffer frame enters episode");
    expect(!diagnostics.RecordNoBuffer(110) && diagnostics.ConsecutiveNoBufferCount() == 2,
        "repeat no-buffer frame counts without re-entering");
    auto recovery = diagnostics.RecordSuccessfulPresent(120);
    expect(recovery.noBufferRecovered && recovery.noBufferDurationMs == 20 &&
        recovery.noBufferCount == 2, "successful Present recovers no-buffer episode");
    expect(diagnostics.RecordNoBuffer(130), "later no-buffer frame starts a new episode");

    using clawhud::HudPresentationSubmissionStage;
    expect(diagnostics.RecordSubmissionFailure(HudPresentationSubmissionStage::SetBuffer,
        E_FAIL, 140), "first SetBuffer failure enters episode");
    expect(!diagnostics.RecordSubmissionFailure(HudPresentationSubmissionStage::SetBuffer,
        E_FAIL, 150) && diagnostics.SubmissionFailureCount() == 2,
        "same SetBuffer failure counts without re-entering");
    recovery = diagnostics.RecordSuccessfulPresent(160);
    expect(recovery.submissionRecovered &&
        recovery.previousFailureStage == HudPresentationSubmissionStage::SetBuffer &&
        recovery.previousFailureHr == E_FAIL && recovery.submissionFailureCount == 2,
        "successful Present recovers SetBuffer failure episode");
    expect(diagnostics.RecordSubmissionFailure(HudPresentationSubmissionStage::Present,
        E_INVALIDARG, 170), "Present failure is distinct from SetBuffer failure");
    recovery = diagnostics.RecordSuccessfulPresent(180);
    expect(recovery.submissionRecovered &&
        recovery.previousFailureStage == HudPresentationSubmissionStage::Present,
        "successful Present recovers Present failure episode");

    clawhud::HudPresentationDiagnosticState heartbeat;
    expect(heartbeat.RecordSuccessfulPresent(100).heartbeat,
        "first successful Present establishes heartbeat baseline");
    expect(!heartbeat.RecordSuccessfulPresent(5099).heartbeat,
        "successful Presents inside heartbeat interval stay quiet");
    expect(heartbeat.RecordSuccessfulPresent(5100).heartbeat,
        "first successful Present after heartbeat interval logs");
    expect(!heartbeat.RecordSuccessfulPresent(5101).heartbeat,
        "subsequent successful Present inside interval stays quiet");

    return ok ? 0 : 1;
}
