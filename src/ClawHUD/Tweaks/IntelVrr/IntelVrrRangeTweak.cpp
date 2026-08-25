#include "IntelVrrRangeTweak.h"
#include "IntelVrrResultStore.h"
#include <cmath>
#include <sstream>
#include <iomanip>
#include <windows.h>
#include <algorithm>

namespace clawhud
{
namespace { constexpr float NativeMin = 48.0f, NativeMax = 120.0f, Tolerance = 0.1f; bool Native(float min, float max) { return std::abs(min - NativeMin) <= Tolerance && std::abs(max - NativeMax) <= Tolerance; } std::string Range(float min, float max) { std::ostringstream s; s << min << "-" << max << " Hz"; return s.str(); } std::string NowUtc() { SYSTEMTIME t{}; GetSystemTime(&t); std::ostringstream s; s << std::setfill('0') << std::setw(4) << t.wYear << '-' << std::setw(2) << t.wMonth << '-' << std::setw(2) << t.wDay << 'T' << std::setw(2) << t.wHour << ':' << std::setw(2) << t.wMinute << ':' << std::setw(2) << t.wSecond << 'Z'; return s.str(); } }
IntelVrrRangeTweak::IntelVrrRangeTweak(ClientFactory clientFactory, PanelProvider panelProvider) : clientFactory_(std::move(clientFactory)), panelProvider_(std::move(panelProvider)) {}
IntelVrrRunResult IntelVrrRangeTweak::Run(bool enabled)
{
    log_.clear(); loggedCallCount_ = 0; IntelVrrRunResult result{}; result.timestampUtc = NowUtc();
    auto finish = [&](IntelVrrRunResult value) { log_.push_back(std::string("Result: ") + IntelVrrRunStatusName(value.status) + " - " + value.message); IntelVrrResultStore::Save(value); return value; };
    if (!enabled) { result.status = IntelVrrRunStatus::Disabled; result.message = "Disabled by user."; log_.push_back("Toggle is disabled. No action taken."); return finish(result); }
    std::vector<PanelIdentity> panels; try { panels = panelProvider_(); } catch (...) { result.status = IntelVrrRunStatus::Unavailable; result.message = "Could not determine the panel identity."; return finish(result); }
    log_.push_back("WMI monitor count=" + std::to_string(panels.size())); for (std::size_t i = 0; i < panels.size(); ++i) { const auto& p = panels[i]; log_.push_back("Monitor[" + std::to_string(i) + "]: Active=" + std::to_string(p.active) + ", ManufacturerName=" + p.manufacturer + ", ProductCodeID=" + p.productCode + ", UserFriendlyName=" + p.panelName + ", Match=" + std::to_string(IsAffectedPanel(p))); }
    auto panel = std::find_if(panels.begin(), panels.end(), IsAffectedPanel); if (panel == panels.end()) { result.status = IntelVrrRunStatus::UnsupportedPanel; result.message = "This panel is not the affected MSI Claw 8 display."; return finish(result); } result.panelName = panel->panelName;
    auto client = clientFactory_(); if (!client) { result.status = IntelVrrRunStatus::Unavailable; result.message = "Intel Graphics Control Library is not available."; return finish(result); }
    auto flush = [&] { for (; loggedCallCount_ < client->CallLog().size(); ++loggedCallCount_) log_.push_back(client->CallLog()[loggedCallCount_].ToString()); };
    if (!client->Initialize()) { flush(); result.status = IntelVrrRunStatus::Unavailable; result.message = "Intel Graphics Control Library is not available."; return finish(result); }
    auto outputs = client->EnumerateDisplayOutputs(); flush(); log_.push_back("IGCL reported " + std::to_string(outputs.size()) + " display output(s).");
    std::vector<std::pair<IntelDisplayOutput, IntelArcSyncCapability>> candidates; for (const auto& output : outputs) { IntelArcSyncCapability capability{}; const bool read = client->GetMonitorCapability(output, capability); flush(); log_.push_back("Capability: read=" + std::to_string(read) + ", supported=" + std::to_string(capability.supported) + ", range=" + Range(capability.minimumHz, capability.maximumHz)); if (read && capability.supported) candidates.push_back({ output, capability }); }
    if (candidates.empty()) { result.status = IntelVrrRunStatus::Unavailable; result.message = "Arc Sync is not available on this display."; return finish(result); }
    std::vector<std::pair<IntelDisplayOutput, IntelArcSyncCapability>> native; for (const auto& c : candidates) if (Native(c.second.minimumHz, c.second.maximumHz)) native.push_back(c);
    if (candidates.size() > 1 && native.size() != 1) { result.status = IntelVrrRunStatus::AmbiguousDisplay; result.message = "Multiple displays matched; skipped to avoid changing the wrong one."; return finish(result); }
    const auto& selected = candidates.size() == 1 ? candidates.front() : native.front(); if (!Native(selected.second.minimumHz, selected.second.maximumHz)) { result.status = IntelVrrRunStatus::UnsupportedPanel; result.message = "Display capability range does not match the expected panel class."; return finish(result); }
    IntelArcSyncProfileState profile{}; if (!client->GetArcSyncProfile(selected.first, profile)) { flush(); result.status = IntelVrrRunStatus::Unavailable; result.message = "Could not read the current Arc Sync profile."; return finish(result); } flush(); result.rangeBefore = Range(profile.minimumHz, profile.maximumHz); log_.push_back(std::string("Current profile=") + IntelArcSyncProfileName(profile.profile) + ", range=" + result.rangeBefore);
    if (profile.profile == IntelArcSyncProfile::Excellent && Native(profile.minimumHz, profile.maximumHz)) { result.status = IntelVrrRunStatus::AlreadyCorrect; result.message = "Already using the native VRR range."; result.rangeAfter = result.rangeBefore; return finish(result); }
    if (profile.profile == IntelArcSyncProfile::Custom || profile.profile == IntelArcSyncProfile::Off) { result.status = IntelVrrRunStatus::SkippedUserProfile; result.message = "Preserved existing user profile."; result.rangeAfter = result.rangeBefore; return finish(result); }
    if ((profile.profile == IntelArcSyncProfile::Recommended || profile.profile == IntelArcSyncProfile::Good || profile.profile == IntelArcSyncProfile::Compatible || profile.profile == IntelArcSyncProfile::Vesa) && std::abs(profile.minimumHz - selected.second.minimumHz) <= Tolerance && std::abs(profile.maximumHz - selected.second.maximumHz) <= Tolerance) { result.status = IntelVrrRunStatus::AlreadyCorrect; result.message = "Current profile already reports the full native range; no mutation needed."; result.rangeAfter = result.rangeBefore; return finish(result); }
    if (profile.profile != IntelArcSyncProfile::Excellent && profile.profile != IntelArcSyncProfile::Recommended && profile.profile != IntelArcSyncProfile::Good && profile.profile != IntelArcSyncProfile::Compatible && profile.profile != IntelArcSyncProfile::Vesa)
    { result.status = IntelVrrRunStatus::UnsupportedPanel; result.message = "Unrecognized Arc Sync profile state."; return finish(result); }
    std::string error; if (!client->SetArcSyncProfile(selected.first, IntelArcSyncProfile::Excellent, error)) { flush(); result.status = IntelVrrRunStatus::ApplyFailed; result.message = "Failed to apply profile: " + error; return finish(result); } flush();
    IntelArcSyncProfileState verify{}; const bool verifiedRead = client->GetArcSyncProfile(selected.first, verify); flush(); if (!verifiedRead) { result.status = IntelVrrRunStatus::VerificationFailed; result.message = "Applied the profile but could not verify it took effect."; result.rangeAfter = "unknown"; return finish(result); } result.rangeAfter = Range(verify.minimumHz, verify.maximumHz);
    log_.push_back(std::string("Verify profile=") + IntelArcSyncProfileName(verify.profile) + ", range=" + result.rangeAfter);
    if (verify.profile != IntelArcSyncProfile::Excellent || !Native(verify.minimumHz, verify.maximumHz)) { result.status = IntelVrrRunStatus::VerificationFailed; result.message = "Applied the profile but could not verify it took effect."; return finish(result); }
    result.status = IntelVrrRunStatus::Applied; result.message = "Restored the native VRR range."; return finish(result);
}
}
