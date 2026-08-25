#include "Tweaks/IntelVrr/IntelVrrRangeTweak.h"
#include "Tweaks/IntelVrr/IntelVrrResultStore.h"
#include <iostream>
#include <memory>
#include <windows.h>
#include <fstream>

using namespace clawhud;
namespace
{
bool Check(bool value, const char* message) { if (!value) std::cerr << "FAILED: " << message << '\n'; return value; }
class FakeClient final : public IIntelArcSyncClient
{
public:
    bool initialize{ true }; std::vector<IntelDisplayOutput> outputs{ { nullptr, nullptr, {} } }; IntelArcSyncCapability capability{ true, 48, 120, 0, 0 }; IntelArcSyncProfileState profile{}; bool setSuccess{ true }; bool validReadback{ true }; std::shared_ptr<int> setCalls{ std::make_shared<int>(0) };
    bool Initialize() override { return initialize; }
    std::vector<IntelDisplayOutput> EnumerateDisplayOutputs() override { return outputs; }
    bool GetMonitorCapability(const IntelDisplayOutput&, IntelArcSyncCapability& value) override { value = capability; return initialize; }
    bool GetArcSyncProfile(const IntelDisplayOutput&, IntelArcSyncProfileState& value) override { value = profile; return initialize; }
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
        auto fake = std::make_shared<FakeClient>(); fake->profile = { IntelArcSyncProfile::Recommended, 48, 60, 0, 0 }; fake->validReadback = false; IntelVrrRangeTweak tweak([fake] { return std::unique_ptr<IIntelArcSyncClient>(new FakeClient(*fake)); }, [] { return std::vector<PanelIdentity>{ Affected() }; });
        auto result = tweak.Run(true); ok &= Check(result.status == IntelVrrRunStatus::VerificationFailed && *fake->setCalls == 1, "invalid readback is verification failure");
    }
    {
        const auto saved = IntelVrrResultStore::Load(); ok &= Check(saved && saved->status == IntelVrrRunStatus::VerificationFailed, "last result round-trips");
        std::wofstream corrupt(resultDirectory + L"\\tweaks-intel-vrr-result.ini", std::ios::trunc); corrupt << L"not a result"; corrupt.close();
        ok &= Check(!IntelVrrResultStore::Load(), "corrupt result falls back safely");
    }
    return ok ? 0 : 1;
}
