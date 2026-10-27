#include "Tweaks/IntelVrr/IntelVrrRangeTweak.h"
#include "Tweaks/IntelVrr/Lg27gs65fVrrRangeTweak.h"
#include "Tweaks/IntelVrr/IntelVrrResultStore.h"
#include <iostream>
#include <memory>
#include <windows.h>
#include <fstream>
#include <stdexcept>

using namespace clawhud;
namespace
{
bool Check(bool value, const char* message) { if (!value) std::cerr << "FAILED: " << message << '\n'; return value; }
class FakeClient final : public IIntelArcSyncClient
{
public:
    bool initialize{ true }; std::vector<IntelDisplayOutput> outputs{ { nullptr, nullptr, {} } }; IntelArcSyncCapability capability{ true, 48, 120, 0, 0 }; IntelArcSyncProfileState profile{}; bool setSuccess{ true }; bool validReadback{ true }; bool verificationReadSuccess{ true }; bool secondOutputNarrow{}; int profileReads{}; std::shared_ptr<int> setCalls{ std::make_shared<int>(0) };
    bool Initialize() override { return initialize; }
    std::vector<IntelDisplayOutput> EnumerateDisplayOutputs() override { return outputs; }
    bool GetMonitorCapability(const IntelDisplayOutput& output, IntelArcSyncCapability& value) override { value = secondOutputNarrow && output.output == reinterpret_cast<void*>(1) ? IntelArcSyncCapability{ true, 48, 60, 0, 0 } : capability; return initialize; }
    bool GetArcSyncProfile(const IntelDisplayOutput&, IntelArcSyncProfileState& value) override { ++profileReads; if (profileReads > 1 && !verificationReadSuccess) { calls.push_back({ "ctlGetIntelArcSyncProfile", 0x40000007, "CTL_RESULT_ERROR_NOT_AVAILABLE", "verification" }); return false; } value = profile; return initialize; }
    bool SetArcSyncProfile(const IntelDisplayOutput&, IntelArcSyncProfile value, std::string&) override { ++*setCalls; if (setSuccess && validReadback) profile = { value, 48, 120, 0, 0 }; return setSuccess; }
    const std::vector<IntelArcSyncCall>& CallLog() const override { return calls; }
    void Shutdown() override {}
    std::vector<IntelArcSyncCall> calls;
};
PanelIdentity Affected() { return { "CSW", "0801", "PN8007QB1-2", true, "monitor" }; }
}
int main()
{
    bool ok = true;
    wchar_t tempPath[MAX_PATH]{}; GetTempPathW(MAX_PATH, tempPath); std::wstring resultDirectory = std::wstring(tempPath) + L"ClawHUD-Vrr-Test"; IntelVrrResultStore::SetDataDirectoryOverrideForTests(resultDirectory);
    ok &= Check(IsAffectedPanel(Affected()), "affected panel matches");
    auto inactive = Affected(); inactive.active = false; ok &= Check(!IsAffectedPanel(inactive), "inactive panel does not match");
    auto wrong = Affected(); wrong.manufacturer = "ABC"; ok &= Check(!IsAffectedPanel(wrong), "wrong manufacturer does not match");
    wrong = Affected(); wrong.productCode = "0000"; ok &= Check(!IsAffectedPanel(wrong), "wrong product does not match");
    wrong = Affected(); wrong.panelName = "other"; ok &= Check(!IsAffectedPanel(wrong), "wrong name does not match");

    const auto lg = PanelIdentity{ "GSM", "5C8C", "LG ULTRAGEAR", true, "DISPLAY\\GSM5C8C\\UID4165" };
    ok &= Check(IsLg27gs65fPanel(lg), "LG 27GS65F identity matches");
    ok &= Check(IsLg27gs65fWindowsIdentity(
        LR"(\\?\DISPLAY#GSM5C8C#test-instance#{guid})", L"LG ULTRAGEAR"),
        "LG Windows display-path identity matches");
    ok &= Check(!IsLg27gs65fWindowsIdentity(
        LR"(\\?\DISPLAY#GSM0000#test-instance#{guid})", L"LG ULTRAGEAR"),
        "other LG monitor path must not match");
    ok &= Check(!IsLg27gs65fWindowsIdentity(
        LR"(\\?\DISPLAY#GSM5C8C#test-instance#{guid})", L"Generic PnP Monitor"),
        "unexpected display friendly name must not match");
    auto lgBad = lg; lgBad.active = false;
    ok &= Check(!IsLg27gs65fPanel(lgBad), "inactive LG is ignored");
    lgBad = lg; lgBad.productCode = "0000";
    ok &= Check(!IsLg27gs65fPanel(lgBad), "other LG model does not match");
    lgBad = lg; lgBad.manufacturer = "DEL";
    ok &= Check(!IsLg27gs65fPanel(lgBad), "non-LG manufacturer does not match");
    ok &= Check(!IsAffectedPanel(lg), "LG must not match Claw's internal-panel detector");

    auto lgState = [](std::uint32_t profile, float minimum, float maximum) {
        DiagIntelVrrState s;
        s.initialized = s.enumerationComplete = true;
        s.mappingStatus = DiagIgclTargetMappingStatus::Exact;
        s.targetMatchCount = s.displayOutputCount = s.displayPropertiesSuccessCount = 1;
        s.capabilityResult = s.profileResult = 0;
        s.capability = DiagArcSyncCapability{ true, 48.0f, 180.0f, 0, 0 };
        s.profile = DiagArcSyncProfile{ profile, maximum, minimum, 0, 0 };
        return s;
    };
    using LgDecision = Lg27gs65fProfileDecision;
    ok &= Check(EvaluateLg27gs65fProductionProfile(lgState(1, 90, 180)) ==
        LgDecision::ApplyExcellent, "LG recommended 90-180 needs EXCELLENT");
    ok &= Check(EvaluateLg27gs65fProductionProfile(lgState(2, 48, 180)) ==
        LgDecision::AlreadyCorrect, "LG full EXCELLENT is idempotent");
    ok &= Check(EvaluateLg27gs65fProductionProfile(lgState(1, 48, 180)) ==
        LgDecision::AlreadyCorrect, "LG full RECOMMENDED is preserved");
    for (const auto profile : { 3u, 5u, 7u })
        ok &= Check(EvaluateLg27gs65fProductionProfile(lgState(profile, 90, 180)) ==
            LgDecision::SkipUserProfile, "LG custom/off/nonrecommended profile is preserved");
    ok &= Check(EvaluateLg27gs65fProductionProfile(lgState(1, 60, 180)) ==
        LgDecision::SkipUserProfile, "unexpected LG active range is not overridden");
    auto lgUnsafe = lgState(1, 90, 180);
    lgUnsafe.mappingStatus = DiagIgclTargetMappingStatus::Ambiguous;
    ok &= Check(EvaluateLg27gs65fProductionProfile(lgUnsafe) ==
        LgDecision::Unavailable, "ambiguous IGCL output prevents write");
    lgUnsafe = lgState(1, 90, 180);
    lgUnsafe.capability->maximumHz = 165;
    ok &= Check(EvaluateLg27gs65fProductionProfile(lgUnsafe) ==
        LgDecision::Unsupported, "wrong LG capability prevents write");
    lgUnsafe = lgState(1, 90, 180);
    lgUnsafe.profileResult = 0x40000007;
    ok &= Check(EvaluateLg27gs65fProductionProfile(lgUnsafe) ==
        LgDecision::Unavailable, "IGCL error prevents write");

    auto run = [](IntelArcSyncProfile profile, float min, float max, bool setSuccess = true) {
        auto fake = std::make_shared<FakeClient>(); fake->profile = { profile, min, max, 0, 0 }; fake->setSuccess = setSuccess; auto* raw = fake.get();
        IntelVrrRangeTweak tweak([fake] { return std::unique_ptr<IIntelArcSyncClient>(new FakeClient(*fake)); }, [] { return std::vector<PanelIdentity>{ Affected() }; });
        auto result = tweak.Run(true); return std::pair{ result, *raw->setCalls };
    };
    auto custom = run(IntelArcSyncProfile::Custom, 48, 60); ok &= Check(custom.first.status == IntelVrrRunStatus::SkippedUserProfile && custom.second == 0, "custom is preserved without SET");
    auto off = run(IntelArcSyncProfile::Off, 48, 60); ok &= Check(off.first.status == IntelVrrRunStatus::SkippedUserProfile && off.second == 0, "off is preserved without SET");
    auto correct = run(IntelArcSyncProfile::Excellent, 48, 120); ok &= Check(correct.first.status == IntelVrrRunStatus::AlreadyCorrect && correct.second == 0, "excellent native is already correct");
    auto narrow = run(IntelArcSyncProfile::Recommended, 48, 60); ok &= Check(narrow.first.status == IntelVrrRunStatus::Applied && narrow.second == 1, "narrow range is fixed");
    auto failed = run(IntelArcSyncProfile::Recommended, 48, 60, false); ok &= Check(failed.first.status == IntelVrrRunStatus::ApplyFailed && failed.second == 1, "SET failure is reported");
    {
        auto fake = std::make_shared<FakeClient>(); IntelVrrRangeTweak tweak([fake] { return std::unique_ptr<IIntelArcSyncClient>(new FakeClient(*fake)); }, [] { return std::vector<PanelIdentity>{ Affected() }; });
        auto result = tweak.Run(false); ok &= Check(result.status == IntelVrrRunStatus::Disabled && *fake->setCalls == 0, "disabled performs no mutation");
    }
    {
        auto fake = std::make_shared<FakeClient>(); IntelVrrRangeTweak tweak([fake] { return std::unique_ptr<IIntelArcSyncClient>(new FakeClient(*fake)); }, [] { return std::vector<PanelIdentity>{}; });
        auto result = tweak.Run(true); ok &= Check(result.status == IntelVrrRunStatus::UnsupportedPanel && *fake->setCalls == 0, "unsupported panel performs no mutation");
    }
    {
        auto fake = std::make_shared<FakeClient>(); fake->initialize = false; IntelVrrRangeTweak tweak([fake] { return std::unique_ptr<IIntelArcSyncClient>(new FakeClient(*fake)); }, [] { return std::vector<PanelIdentity>{ Affected() }; });
        auto result = tweak.Run(true); ok &= Check(result.status == IntelVrrRunStatus::Unavailable, "IGCL unavailable is reported");
    }
    {
        auto fake = std::make_shared<FakeClient>(); fake->outputs.clear(); IntelVrrRangeTweak tweak([fake] { return std::unique_ptr<IIntelArcSyncClient>(new FakeClient(*fake)); }, [] { return std::vector<PanelIdentity>{ Affected() }; });
        auto result = tweak.Run(true); ok &= Check(result.status == IntelVrrRunStatus::Unavailable, "no Arc Sync output is unavailable");
    }
    {
        auto fake = std::make_shared<FakeClient>(); fake->outputs.push_back({ nullptr, reinterpret_cast<void*>(1), {} }); IntelVrrRangeTweak tweak([fake] { return std::unique_ptr<IIntelArcSyncClient>(new FakeClient(*fake)); }, [] { return std::vector<PanelIdentity>{ Affected() }; });
        auto result = tweak.Run(true); ok &= Check(result.status == IntelVrrRunStatus::AmbiguousDisplay && *fake->setCalls == 0, "ambiguous outputs perform no mutation");
    }
    {
        auto fake = std::make_shared<FakeClient>(); fake->outputs[0].friendlyName = L"External Panel"; fake->profile = { IntelArcSyncProfile::Recommended, 48, 60, 0, 0 }; IntelVrrRangeTweak tweak([fake] { return std::unique_ptr<IIntelArcSyncClient>(new FakeClient(*fake)); }, [] { return std::vector<PanelIdentity>{ Affected() }; });
        auto result = tweak.Run(true); ok &= Check(result.status == IntelVrrRunStatus::AmbiguousDisplay && *fake->setCalls == 0, "contradictory single-output identity performs no mutation");
    }
    {
        auto fake = std::make_shared<FakeClient>(); fake->outputs[0].friendlyName = L"External Panel"; fake->outputs.push_back({ nullptr, reinterpret_cast<void*>(1), {} }); fake->secondOutputNarrow = true; fake->profile = { IntelArcSyncProfile::Recommended, 48, 60, 0, 0 }; IntelVrrRangeTweak tweak([fake] { return std::unique_ptr<IIntelArcSyncClient>(new FakeClient(*fake)); }, [] { return std::vector<PanelIdentity>{ Affected() }; });
        auto result = tweak.Run(true); ok &= Check(result.status == IntelVrrRunStatus::AmbiguousDisplay && *fake->setCalls == 0, "contradictory native-fallback identity performs no mutation");
    }
    {
        auto fake = std::make_shared<FakeClient>(); fake->profile = { IntelArcSyncProfile::Recommended, 48, 60, 0, 0 }; fake->validReadback = false; IntelVrrRangeTweak tweak([fake] { return std::unique_ptr<IIntelArcSyncClient>(new FakeClient(*fake)); }, [] { return std::vector<PanelIdentity>{ Affected() }; });
        auto result = tweak.Run(true); ok &= Check(result.status == IntelVrrRunStatus::VerificationFailed && *fake->setCalls == 1, "invalid readback is verification failure");
    }
    {
        auto fake = std::make_shared<FakeClient>(); fake->profile = { IntelArcSyncProfile::Recommended, 48, 60, 0, 0 }; fake->verificationReadSuccess = false; IntelVrrRangeTweak tweak([fake] { return std::unique_ptr<IIntelArcSyncClient>(new FakeClient(*fake)); }, [] { return std::vector<PanelIdentity>{ Affected() }; });
        auto result = tweak.Run(true); const auto& log = tweak.LastLog(); bool found = false; for (const auto& line : log) if (line.find("ctlGetIntelArcSyncProfile: CTL_RESULT_ERROR_NOT_AVAILABLE") != std::string::npos) found = true;
        ok &= Check(result.status == IntelVrrRunStatus::VerificationFailed && found, "failed verification GET is logged");
    }
    {
        const auto saved = IntelVrrResultStore::Load(); ok &= Check(saved && saved->status == IntelVrrRunStatus::VerificationFailed, "last result round-trips");
        std::wofstream corrupt(resultDirectory + L"\\tweaks-intel-vrr-result.ini", std::ios::trunc); corrupt << L"not a result"; corrupt.close();
        ok &= Check(!IntelVrrResultStore::Load(), "corrupt result falls back safely");
    }
    {
        IntelVrrRangeTweak tweak([] { return std::unique_ptr<IIntelArcSyncClient>{}; }, []() -> std::vector<PanelIdentity> { throw std::runtime_error("WMI enumeration failed"); });
        auto result = tweak.Run(true); ok &= Check(result.status == IntelVrrRunStatus::Unavailable, "WMI enumeration failure is unavailable");
    }
    return ok ? 0 : 1;
}
